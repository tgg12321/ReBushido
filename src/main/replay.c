/* The instant replay shown after a bout. While fighting, every frame's fighter
 * poses go into a ring of REPLAY_FRAMES frames, with the sounds, effects and
 * loose objects of those frames; replay_PlayFrame plays the ring back. */
#include "common.h"
#include "bb2.h"

/* Defined further down. */
static u8 g_replay_object_draw_overflows;
static u8 g_replay_playing_event;

u8 g_shared_buffer[REPLAY_FRAMES * 2 * sizeof(ReplayFighterFrame)];
ReplayObjectDraw g_replay_object_draws[REPLAY_OBJECT_DRAWS];
ReplayEvent g_replay_events[REPLAY_EVENTS];

/* A loose object's record is dropped this long after it stopped being
 * drawn. */
#define REPLAY_DRAW_KEEP_FRAMES 256

/* The field of view the replay is shot with, in degrees. */
#define REPLAY_FOV 45

/* How far over and under the feet fighter_ProbeFloor looks for the floor. */
#define FLOOR_PROBE_REACH 400

/* A pose's frame as the fighter keeps it (Fighter.pose_from_frame): the
 * frame of a motion, and over it the decoder that has the motion open. */
#define POSE_DECODER(pose_frame) (((pose_frame) >> 14) & 3)
#define POSE_FRAME(pose_frame) ((pose_frame) & 0x3FFF)

/* The words of motion_DecodeFrame's work area. */
#define DECODE_WORK_SIZE 200

/* The frame being recorded; once the ring has wrapped this is also the oldest
 * frame. */
u8 g_replay_write_frame;
u8 g_replay_ring_wrapped;
/* How far playback has got, counted from the oldest frame. */
u8 g_replay_playback_frame;
/* Where the search for a free record starts; reset every frame. */
u8 g_replay_event_cursor;
s16 g_replay_object_draw_cursor;

/* A fight begins: the ring is empty, in the shared buffer, and no event or
 * object is on record. */
void replay_Reset(void) {
    s32 i;
    s32 j;

    g_replay_fighter_frames = (ReplayFighterFrame(*)[2])g_shared_buffer;
    g_replay_write_frame = 0;
    g_replay_ring_wrapped = 0;
    for (i = 0; i < REPLAY_EVENTS; i++) {
        g_replay_events[i].frame = REPLAY_EVENT_FREE;
    }
    for (j = 0; j < REPLAY_OBJECT_DRAWS; j++) {
        g_replay_object_draws[j].age = REPLAY_DRAW_FREE;
    }
}

/* Makes room for the frame about to be recorded: drops the events of the frame
 * it overwrites and ages the object records. */
void replay_BeginFrame(void) {
    s32 i;
    ReplayObjectDraw *draw;
    s16 age;
    s16 new_age;

    for (i = 0; i < REPLAY_EVENTS; i++) {
        if (g_replay_events[i].frame == g_replay_write_frame) {
            g_replay_events[i].frame = REPLAY_EVENT_FREE;
        }
    }

    draw = g_replay_object_draws;
    i = 0;
    do {
        age = draw->age;
        if (age != REPLAY_DRAW_FREE) {
            new_age = age + 1;
            draw->age = new_age;
            if (new_age - draw->span > REPLAY_DRAW_KEEP_FRAMES) {
                draw->age = REPLAY_DRAW_FREE;
            }
        }
        i++;
        draw++;
    } while (i < REPLAY_OBJECT_DRAWS);

    g_replay_event_cursor = 0;
    g_replay_object_draw_cursor = 0;
}

/* Records that a loose object was drawn this frame. An object that has not
 * moved since the previous frame extends its existing record. */
void replay_RecordObjectDraw(s32 slot, s32 weapon_model, s32 *pos, s16 *rot) {
    ReplayObjectDraw *draw;
    s32 i;
    s16 index;
    s16 next;
    s32 age;
    u8 span;
    s32 packed;
    s32 rot_x;

    draw = g_replay_object_draws;
    i = 0;
    do {
        age = draw->age;
        if (age != REPLAY_DRAW_FREE) {
            span = draw->span;
            /* drawn last frame, and its span has room for one more */
            if (span == age - 1 && span < 0xFF) {
                /* the slot is the top four bits; the rest is compared as
                 * an angle */
                packed = draw->rot_x_and_slot << 16;
                rot_x = packed >> 16;
                if (((u32)packed >> 28) == slot && draw->pos[0] == pos[0] &&
                    draw->pos[1] == pos[1] && draw->pos[2] == pos[2] &&
                    ((rot_x - rot[0]) & ANGLE_MASK) == 0 &&
                    ((draw->rot_y - rot[1]) & ANGLE_MASK) == 0 &&
                    ((draw->rot_z - rot[2]) & ANGLE_MASK) == 0) {
                    draw->span = span + 1;
                    return;
                }
            }
        }
        i++;
        draw++;
    } while (i < REPLAY_OBJECT_DRAWS);

    index = g_replay_object_draw_cursor;
    draw = &g_replay_object_draws[index];
    while (index < REPLAY_OBJECT_DRAWS) {
        if (draw->age == REPLAY_DRAW_FREE) {
            break;
        }
        next = index + 1;
        g_replay_object_draw_cursor = next;
        draw++;
        index = next;
    }

    if (g_replay_object_draw_cursor == REPLAY_OBJECT_DRAWS) {
        g_replay_object_draw_overflows++;
        return;
    }

    draw->age = 0;
    draw->weapon_model = weapon_model;
    draw->span = 0;
    draw->pos[0] = pos[0];
    draw->pos[1] = pos[1];
    draw->pos[2] = pos[2];
    draw->rot_x_and_slot = (rot[0] & ANGLE_MASK) | (slot << 12);
    draw->rot_y = rot[1];
    draw->rot_z = rot[2];
}

/* Records a sound or effect of this frame. `vector` may be NULL. */
void replay_RecordEvent(s32 variant, s32 kind, s32 *pos, u16 *vector) {
    ReplayEvent *event;
    u8 index;

    if (!g_replay_playing_event) {
        index = g_replay_event_cursor;
        event = &g_replay_events[index];
        for (; index < REPLAY_EVENTS; index++, event++) {
            if (event->frame == REPLAY_EVENT_FREE) {
                break;
            }
            g_replay_event_cursor = index + 1;
        }
        if (g_replay_event_cursor != REPLAY_EVENTS) {
            u8 frame = g_replay_write_frame;
            event->variant = variant;
            event->kind = kind;
            event->frame = frame;
            event->pos[0] = pos[0];
            event->pos[1] = pos[1];
            event->pos[2] = pos[2];
            if (vector != NULL) {
                event->vector[0] = vector[0];
                event->vector[1] = vector[1];
                event->vector[2] = vector[2];
            }
        }
    }
}

/* Records a fighter's frame: where it stands and which way it faces, its
 * face, the pose (the two frames it is between and how far), how it aims,
 * the move it is in and what of it is drawn. */
void replay_RecordFighter(Fighter *fighter) {
    s16 index;
    ReplayFighterFrame *frame;

    index = fighter->index;
    frame = &g_replay_fighter_frames[g_replay_write_frame][index];

    frame->pos_x = fighter->world_pos.x;
    frame->pos_z = fighter->world_pos.z;
    frame->pos_y = fighter->world_pos.y;

    {
        u16 yaw = fighter->orientation.vy;
        u8 face = fighter->face;
        frame->yaw_and_face = (yaw & ANGLE_MASK) | (face << 12);
    }

    frame->ground_y = fighter->ground_y;
    frame->aim_turn = fighter->aim_turn >> 2;
    frame->aim_lean = fighter->aim_lean >> 2;
    frame->gun_aim = fighter->gun_aim >> 2;
    frame->cur_move = fighter->cur_move;
    frame->move_section_bits = 0;

    if (fighter->cur_move_is_common) {
        frame->move_section_bits = REPLAY_MOVE_COMMON;
    }
    if (fighter->cur_move_slot != 0) {
        frame->move_section_bits |= REPLAY_MOVE_SECOND_SLOT;
    }

    frame->pose_from_frame = fighter->pose_from_frame;
    frame->pose_to_frame = fighter->pose_to_frame;
    frame->pose_weight = fighter->pose_weight;
    frame->frame_flags = fighter->frame_flags;
    frame->move_frame = fighter->move_frame;
}

/* The frame is recorded: on to the next place of the ring. */
void replay_AdvanceFrame(void) {
    u8 frame = g_replay_write_frame;
    if (frame == REPLAY_FRAMES - 1) {
        g_replay_write_frame = 0;
        g_replay_ring_wrapped = 1;
    } else {
        g_replay_write_frame = frame + 1;
    }
}

/* Switches the main loop over to replay playback. */
void replay_Start(void) {
    disp_Reset();
    stage_UnfogModel();
    stage_SetFirstPerson(0);
    SetGeomScreen(math_FovToScreenDist(REPLAY_FOV));
    model_SetOutline(0, 0);
    model_SetOutline(1, 0);
    camera_StartReplay();
    eff_Stop();
    g_replay_playback_frame = 0;
    g_sys_screen = SCREEN_REPLAY_UPDATE;
}

/* Finds what the fighter is standing on: casts a short vertical segment
 * through the midpoint of its two ground probe points. *surface_out is the
 * kind of surface (enum Surface; -1: nothing there) and *class_out its class
 * (0 or 1; -1 for nothing or for a surface that has no class). */
static void fighter_ProbeFloor(
    Fighter *fighter, s32 *surface_out, s32 *class_out) {
    s32 start[3];
    s32 end[3];
    s32 hit_point[4];
    s16 hit_normal[4];
    s32 mid_x, mid_y, mid_z;
    s32 surface;

    mid_x = (fighter->foot_points[0].x + fighter->foot_points[1].x) / 2;
    start[0] = mid_x;
    mid_y = (fighter->foot_points[0].y + fighter->foot_points[1].y) / 2;
    start[1] = mid_y - FLOOR_PROBE_REACH;
    mid_z = (fighter->foot_points[0].z + fighter->foot_points[1].z) / 2;
    end[0] = mid_x;
    end[1] = mid_y + FLOOR_PROBE_REACH;
    start[2] = mid_z;
    end[2] = mid_z;
    if (collision_Cast(start, end, hit_point, hit_normal)) {
        surface = collision_GetSurface();
        *surface_out = surface;
        if (surface != SURFACE_BARRIER && surface != SURFACE_PLAIN) {
            *class_out = SURFACE_CLASS(surface);
            return;
        }
    } else {
        *surface_out = -1;
    }
    *class_out = -1;
}

/* Plays one frame of the replay: poses and draws both fighters from the
 * record, re-fires the frame's sounds and effects, draws the loose objects,
 * and when the replay is over (or cross is pressed) moves on to whatever
 * follows the bout in the current mode. */
void replay_PlayFrame(void) {
    MotionFrame poses[3]; /* from, to, and the blend of the two */
    s32 pos[3];
    s16 rot[3];
    u16 decode_work[DECODE_WORK_SIZE];
    s32 floor_surface;
    s32 floor_class;
    s32 frame_index;
    s32 progress; /* 0..0x1000 through the replay */
    s32 i;
    ReplayFighterFrame *frame;
    Fighter *fighter;
    ReplayEvent *event;
    ReplayObjectDraw *draw;
    /* FAKE: two locals, the move's section and the frames left to play */
    s32 work;
    u8 saved_move_frame;
    u8 *saved_motion_head;

    if (g_replay_ring_wrapped) {
        frame_index =
            (g_replay_write_frame + g_replay_playback_frame) % REPLAY_FRAMES;
        progress = (g_replay_playback_frame << 12) / REPLAY_FRAMES;
    } else {
        frame_index = g_replay_playback_frame;
        progress = (frame_index << 12) / g_replay_write_frame;
    }

    /* The camera follows the fighter chosen for the follow-up view. */
    g_fight_squashed_view = scene_GetSquashedView();
    camera_UpdateReplayMain(
        &g_replay_fighter_frames[frame_index][g_fight_winner],
        g_fight_winner == 0 ? &g_replay_fighter_frames[frame_index][1]
                            : &g_replay_fighter_frames[frame_index][0],
        progress);
    camera_UpdateReplaySecond(
        &g_replay_fighter_frames[frame_index][g_fight_winner],
        g_fight_winner == 0 ? &g_replay_fighter_frames[frame_index][1]
                            : &g_replay_fighter_frames[frame_index][0],
        progress);
    camera_BeginReplayScene(progress);

    for (i = 0; i < 2; i++) {
        u16 *motion_entry;

        frame = &g_replay_fighter_frames[frame_index][i];
        fighter = &g_fighters[i];

        /* The pose is rebuilt and the fighter placed. */
        motion_DecodeFrame(
            POSE_DECODER(frame->pose_from_frame),
            POSE_FRAME(frame->pose_from_frame), &poses[0], decode_work);
        motion_DecodeFrame(
            POSE_DECODER(frame->pose_to_frame),
            POSE_FRAME(frame->pose_to_frame), &poses[1], decode_work);
        motion_AimReplayPose(fighter, frame, &poses[0], &poses[1]);
        model_BlendPoses(i, (u8 *)&poses[0], (u8 *)&poses[1],
                         frame->pose_weight, (MATRIX *)&poses[2]);
        pos[0] = frame->pos_x;
        pos[1] = frame->pos_y;
        pos[2] = frame->pos_z;
        rot[0] = 0;
        rot[1] = frame->yaw_and_face;
        rot[2] = 0;
        model_Update(i, MODEL_POSE_BLENDED, pos, rot, 0, frame->ground_y);

        /* The weapon and any carried object, drawn as the frame flags say:
         * in the hand, in the off hand, and each at the side (with
         * FIGHTER_SCABBARD_FULL and FIGHTER_WORN_SUB_FULL, in its
         * scabbard). */
        if (frame->frame_flags & FIGHTER_DRAW_WEAPON) {
            weapon_Queue(fighter->weapon_model, i * 2 + WEAPON_DRAW_HELD, 0, 0);
        }
        if (frame->frame_flags & FIGHTER_DRAW_SUB_WEAPON) {
            weapon_Queue(g_object_weapon_models[fighter->sub_weapon_kind],
                         i * 2 + (WEAPON_DRAW_HELD | 1), 0, 0);
        }
        if (frame->frame_flags & FIGHTER_DRAW_SCABBARD) {
            weapon_QueueWorn(
                fighter->weapon_model, i * 2, (frame->frame_flags >> 4) & 1);
        }
        if (frame->frame_flags & FIGHTER_DRAW_WORN_SUB) {
            weapon_QueueWorn(g_object_weapon_models[fighter->sub_weapon_kind],
                             i * 2 | 1, (frame->frame_flags >> 5) & 1);
        }

        fighter_PlacePoints(
            fighter, SPAD->body_points[i], SPAD->weapon_points[i],
            SPAD->second_weapon_points[i]);
        fighter_ProbeFloor(fighter, &floor_surface, &floor_class);
        if (floor_surface != SURFACE_FIRM) {
            fighter->surface = floor_surface;
            if (floor_class != -1) {
                fighter->surface_class = floor_class;
            }
        }

        /* The move's frame events (footsteps and the like) are run as of
         * the recorded frame, and the live fighter is put back as it was. */
        saved_move_frame = fighter->move_frame;
        fighter->move_frame = frame->move_frame;
        saved_motion_head = fighter->motion_head;
        if (frame->move_section_bits & REPLAY_MOVE_COMMON) {
            motion_entry = &g_motion_common_sections
                                .entries[frame->cur_move->motion_index * 2];
            fighter->motion_head =
                g_motion_common_sections.metadata + motion_entry[1];
        } else {
            work = (frame->move_section_bits >> 1) & 1;
            motion_entry = &g_motion_fighter_sections[work]
                                .entries[frame->cur_move->motion_index * 2];
            fighter->motion_head =
                g_motion_fighter_sections[work].metadata + motion_entry[1];
        }
        if (frame->frame_flags & FIGHTER_FRAME_EVENTS) {
            fighter_RunFrameEvents(fighter);
        }
        fighter->move_frame = saved_move_frame;
        fighter->motion_head = saved_motion_head;

        /* the face, from over the yaw */
        model_SetFace(i, (frame->yaw_and_face >> 12) & 7);
    }

    /* This frame's sounds and effects. */
    event = g_replay_events;
    for (i = 0; i < REPLAY_EVENTS; i++, event++) {
        if (event->frame == frame_index) {
            pos[0] = event->pos[0];
            pos[1] = event->pos[1];
            pos[2] = event->pos[2];
            rot[0] = event->vector[0];
            rot[1] = event->vector[1];
            rot[2] = event->vector[2];
            g_replay_playing_event = 1;
            fight_RaiseEvent(event->variant, event->kind, pos, rot);
            g_replay_playing_event = 0;
        }
    }

    /* The loose objects that were in view this many frames before the end. */
    if (g_replay_ring_wrapped) {
        work = REPLAY_FRAMES - 1 - g_replay_playback_frame;
    } else {
        s32 played = g_replay_playback_frame + 1;
        work = g_replay_write_frame - played;
    }
    draw = g_replay_object_draws;
    for (i = 0; i < REPLAY_OBJECT_DRAWS; i++, draw++) {
        if (draw->age >= work && draw->age - draw->span <= work) {
            pos[0] = draw->pos[0];
            pos[1] = draw->pos[1];
            pos[2] = draw->pos[2];
            rot[0] = draw->rot_x_and_slot;
            rot[1] = draw->rot_y;
            rot[2] = draw->rot_z;
            weapon_Queue(draw->weapon_model, WEAPON_DRAW_FREE, pos, rot);
        }
    }

    scene_End(1);
    snd_UpdateStageSounds();

    g_replay_playback_frame++;
    if ((g_replay_ring_wrapped
             ? g_replay_playback_frame == REPLAY_FRAMES
             : g_replay_playback_frame == g_replay_write_frame) ||
        (g_pad_state.pressed & PAD_ANY(PADRdown))) {
        /* The replay is over: on to what follows the fight. */
        switch (g_fight_mode) {
        case FIGHT_MODE_STORY:
            /* The story (story.h). A fight the player won: after a named
             * opponent a guest gives the player's fighter its place back,
             * and is kept among the characters to unlock; an interlude goes
             * on to its next scene; otherwise the script goes on. */
            if (!g_fighters[0].is_defeated) {
                s32 guest_was_in = g_story_player_roster != STORY_NO_ROSTER;
                if (g_story_opponent_named) {
                    g_fight_rematch_offered = 2;
                    if (g_story_player_roster != STORY_NO_ROSTER) {
                        g_story_unlocks |= UNLOCK_BIT(
                            g_fighter_roster_characters[g_fight_config
                                                            .roster[0]]);
                    }
                    if (guest_was_in) {
                        story_PutPlayerBack();
                    }
                }
                snd_EndFight();
                if (g_story_interlude) {
                    if (g_story_opponent_named) {
                        story_PlayInterludeScene(
                            guest_was_in ? INTERLUDE_GUEST_WON
                                         : INTERLUDE_PLAYER_WON);
                    } else {
                        /* the henchmen are beaten: on to the opponent */
                        story_SetOpponent(
                            g_story_interlude + STORY_INTERLUDE_OPPONENT);
                        story_PlayInterludeScene(
                            guest_was_in ? INTERLUDE_GUEST_MEETS_OPPONENT
                                         : INTERLUDE_PLAYER_MEETS_OPPONENT);
                    }
                } else {
                    story_RunScript();
                }
                return;
            }
            /* A fight the player lost: the menu, unless it was a guest
             * that fell, whose place the player's own fighter takes. */
            g_fight_winner = 1;
            if (g_story_player_roster == STORY_NO_ROSTER) {
                g_sys_screen = SCREEN_AFTER_FIGHT_INIT;
                break;
            }
            snd_EndFight();
            story_PutPlayerBack();
            fight_LoadFighter(0);
            fight_LoadWeapons();
            if (g_story_opponent_named && g_fighters[1].is_defeated) {
                story_PlayInterludeScene(INTERLUDE_PLAYER_WON);
            } else {
                story_PlayInterludeScene(INTERLUDE_GUEST_FELL);
            }
            return;
        case FIGHT_MODE_CHANBARA:
            if (!g_fighters[0].is_defeated) {
                g_sys_screen = SCREEN_BEST_TIMES_INIT;
            } else {
                g_sys_screen = SCREEN_AFTER_FIGHT_INIT;
            }
            break;
        default:
            if (!g_fighters[0].is_defeated || !g_fighters[1].is_defeated) {
                g_sys_screen = SCREEN_WINNER_VIEW_INIT;
            } else {
                g_sys_screen = SCREEN_AFTER_FIGHT_INIT;
            }
            break;
        }
    }
}

/* Small data, in address order. */
/* Set while playback re-fires an event, so that it is not recorded again. */
static u8 g_replay_playing_event = 0;
static u8 g_replay_object_draw_overflows = 0;
ReplayFighterFrame (*g_replay_fighter_frames)[2];
