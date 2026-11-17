/* The fighter (fighter.h), between its moves and its model: bending the
 * pose towards the opponent (motion.h), turning, how fast a wounded fighter
 * moves, mortal wounds, and where the points of its body and weapons are.
 * With them, because the original file has them here, how far apart the
 * two fighters are and the start of a paired move, which are the fight's. */
#define INCLUDE_ASM_USE_MACRO_INC 1
#include "common.h"
#include "include_asm.h"
#include <psxsdk/inline_o.h>
#include <psxsdk/gtemac.h>
#include "bb2.h"

/* ---- motion (motion.h): bending a decoded pose ---- */

/* How far the upper body turns to face the opponent and how far it leans
 * up or down at it: just under an eighth and a sixteenth of a turn either
 * way. A gun is raised or lowered as far as the body turns. */
#define AIM_TURN_MAX 0x1FF
#define AIM_LEAN_MAX 0xFF
#define GUN_AIM_MAX 0x1FF
/* An aim goes this part of what is left of the way, each frame. */
#define AIM_EASE 8

/* The replay's motion_AimPose: the three angles are the ones recorded
 * (in fourths). */
void motion_AimReplayPose(Fighter *fighter, ReplayFighterFrame *recorded,
                          MotionFrame *from_pose, MotionFrame *to_pose) {
    s16 roster_id;

    if (!(recorded->frame_flags & FIGHTER_OFF_ARM_OK)) {
        motion_HangArm(from_pose);
        motion_HangArm(to_pose);
    }
    math_TurnAngles(&from_pose->bone_angles[MOTION_TORSO],
                    recorded->aim_turn * 4, recorded->aim_lean * 4, 0);
    math_TurnAngles(&to_pose->bone_angles[MOTION_TORSO], recorded->aim_turn * 4,
                    recorded->aim_lean * 4, 0);
    roster_id = fighter->roster_id;
    if ((roster_id == ROSTER_SIDEARM_2) || (roster_id == ROSTER_SIDEARM_1)) {
        from_pose->bone_angles[MOTION_SIDEARM_AIM] += recorded->gun_aim * 4;
        to_pose->bone_angles[MOTION_SIDEARM_AIM] += recorded->gun_aim * 4;
    }
    if (((u16)fighter->weapon_id - WEAPON_ID_FIRST_GUN) < 2U) {
        from_pose->bone_angles[MOTION_GUN_AIM] += recorded->gun_aim * 4;
        to_pose->bone_angles[MOTION_GUN_AIM] += recorded->gun_aim * 4;
    }
}

/* Bends the two poses a fighter is drawn between (motion.h).
 *
 * A fighter standing in its stance turns its upper body to face the
 * opponent and leans up or down towards it, a little more of the way every
 * frame; out of the stance it straightens again. The two characters with a
 * gun in the off hand raise that arm at the opponent while the gun is out,
 * and a fighter whose weapon is a gun does the same with its own while it
 * shoots, from where the gun is. A fighter hurt in the legs trembles. An
 * arm that cannot be used hangs.
 *
 * The distances are table square roots, scaled by the number's leading
 * zeros past the table. */
void motion_AimPose(
    Fighter *fighter, MotionFrame *from_pose, MotionFrame *to_pose) {
    s32 target_bearing_offset;
    /* what an eased angle is on its way to: first how far the body leans,
     * then how far the gun is raised */
    s32 elevation_target;
    /* the third angle of the lean, which is 0 in every case */
    s32 twist_target;
    s32 dx;
    s32 dz;
    /* an angle on its way into the pose, within half a turn either way */
    s32 angle_offset;
    s16 roster_id;

    if (!fighter->off_arm_ok) {
        motion_HangArm(from_pose);
        motion_HangArm(to_pose);
    }
    if (fighter->move_kind == MOVE_KIND_STANCE ||
        fighter->move_kind == MOVE_KIND_STANCE_CHANGE) {
        if (fighter->roster_id == ROSTER_VICTIM) {
            /* it kneels with its head bowed */
            elevation_target = ANGLE_TURN / 16;
            twist_target = 0;
            target_bearing_offset = 0;
        } else {
            s32 dist_sq;
            s32 dist;

            target_bearing_offset =
                (fighter->opponent_bearing - fighter->orientation.vy) &
                ANGLE_MASK;
            if (target_bearing_offset >= ANGLE_TURN / 2) {
                target_bearing_offset -= ANGLE_TURN;
            }
            if (target_bearing_offset < -AIM_TURN_MAX) {
                target_bearing_offset = -AIM_TURN_MAX;
            } else if (target_bearing_offset > AIM_TURN_MAX) {
                target_bearing_offset = AIM_TURN_MAX;
            }
            dx = fighter->other->head_pos.x - fighter->head_pos.x;
            dz = fighter->other->head_pos.z - fighter->head_pos.z;
            dist_sq = dx * dx + dz * dz;
            MATH_SQRT(dist, dist_sq);
            elevation_target =
                ANGLE_TURN / 4 -
                ratan2(dist, fighter->other->head_pos.y - fighter->head_pos.y);
            if (elevation_target < -AIM_LEAN_MAX) {
                elevation_target = -AIM_LEAN_MAX;
            } else if (elevation_target > AIM_LEAN_MAX) {
                elevation_target = AIM_LEAN_MAX;
            }
            twist_target = 0;
        }
    } else {
        twist_target = 0;
        elevation_target = 0;
        target_bearing_offset = 0;
    }

    angle_offset = (target_bearing_offset - fighter->aim_turn) & ANGLE_MASK;
    if (angle_offset >= ANGLE_TURN / 2) {
        angle_offset -= ANGLE_TURN;
    }
    fighter->aim_turn = fighter->aim_turn + angle_offset / AIM_EASE;
    angle_offset = (elevation_target - fighter->aim_lean) & ANGLE_MASK;
    if (angle_offset >= ANGLE_TURN / 2) {
        angle_offset -= ANGLE_TURN;
    }
    fighter->aim_lean = fighter->aim_lean + angle_offset / AIM_EASE;
    math_TurnAngles(&from_pose->bone_angles[MOTION_TORSO], fighter->aim_turn,
                    fighter->aim_lean, twist_target);
    math_TurnAngles(&to_pose->bone_angles[MOTION_TORSO], fighter->aim_turn,
                    fighter->aim_lean, twist_target);

    roster_id = fighter->roster_id;
    if ((roster_id == ROSTER_SIDEARM_2 || roster_id == ROSTER_SIDEARM_1) &&
        fighter->sub_weapon_live_state != WEAPON_LIVE_OFF) {
        angle_offset =
            (ratan2(g_fight_distance,
                    fighter->other->world_pos.y - fighter->world_pos.y) -
             ANGLE_TURN / 4) &
            ANGLE_MASK;
        if (angle_offset >= ANGLE_TURN / 2) {
            angle_offset -= ANGLE_TURN;
        }
        if (angle_offset > GUN_AIM_MAX) {
            angle_offset = GUN_AIM_MAX;
        } else if (angle_offset < -GUN_AIM_MAX) {
            angle_offset = -GUN_AIM_MAX;
        }
        fighter->gun_aim = angle_offset;
        from_pose->bone_angles[MOTION_SIDEARM_AIM] += angle_offset;
        to_pose->bone_angles[MOTION_SIDEARM_AIM] += angle_offset;
    }

    if (((u16)fighter->weapon_id - WEAPON_ID_FIRST_GUN) < 2U &&
        fighter->move_kind == MOVE_KIND_ATTACK) {
        s32 dist_sq;
        s32 dist;

        if (!fighter->gun_pos_valid) {
            fighter->gun_pos.x = fighter->world_pos.x;
            fighter->gun_pos.y = fighter->world_pos.y;
            fighter->gun_pos.z = fighter->world_pos.z;
        }
        dx = fighter->other->world_pos.x - fighter->gun_pos.x;
        dz = fighter->other->world_pos.z - fighter->gun_pos.z;
        dist_sq = dx * dx + dz * dz;
        MATH_SQRT(dist, dist_sq);
        elevation_target =
            (ratan2(dist, fighter->other->world_pos.y - fighter->gun_pos.y) -
             ANGLE_TURN / 4) &
            ANGLE_MASK;
        if (elevation_target >= ANGLE_TURN / 2) {
            elevation_target -= ANGLE_TURN;
        }
        if (elevation_target > GUN_AIM_MAX) {
            elevation_target = GUN_AIM_MAX;
        } else if (elevation_target < -GUN_AIM_MAX) {
            elevation_target = -GUN_AIM_MAX;
        }
        angle_offset = (elevation_target - fighter->gun_aim) & ANGLE_MASK;
        if (angle_offset >= ANGLE_TURN / 2) {
            angle_offset -= ANGLE_TURN;
        }
        fighter->gun_aim = fighter->gun_aim + angle_offset / AIM_EASE;
        from_pose->bone_angles[MOTION_GUN_AIM] += fighter->gun_aim;
        to_pose->bone_angles[MOTION_GUN_AIM] += fighter->gun_aim;
    }

    /* trembling: a small random angle (32 either way), opposite ways in
     * two angles, twice over. Which bones the four angles belong to has not
     * been made out: the legs, by what brings it on (a guess). */
    if (fighter->leg_wounds != 0 && !fighter->is_defeated) {
        /* -32 to 31 */
        angle_offset = (rng_Next() & 0x3F) - 0x20;
        from_pose->bone_angles[0] += angle_offset;
        to_pose->bone_angles[0] += angle_offset;
        from_pose->bone_angles[4] -= angle_offset;
        to_pose->bone_angles[4] -= angle_offset;
        /* -32 to 31 */
        angle_offset = (rng_Next() & 0x3F) - 0x20;
        from_pose->bone_angles[9] += angle_offset;  /* not made out */
        to_pose->bone_angles[9] += angle_offset;    /* not made out */
        from_pose->bone_angles[13] -= angle_offset; /* not made out */
        to_pose->bone_angles[13] -= angle_offset;   /* not made out */
    }
}

/* ---- the fighter (fighter.h) ---- */

/* Begins a turn that will leave the fighter facing `angle`, by the shorter
 * way round. */
void fighter_TurnTo(Fighter *fighter, s32 angle) {
    angle = (angle - fighter->orientation.vy) & ANGLE_MASK;
    if (angle >= ANGLE_TURN / 2) {
        angle -= ANGLE_TURN;
    }
    fighter->turn_left = angle;
}

/* The most either difference may be for the two squares to be added. */
#define DISTANCE_TERM_MAX 0x4000

/* How far apart the two fighters are across the ground. The two differences
 * are halved until both are small enough for their squares to be added. */
static s32 fight_MeasureDistance(void) {
    s32 dx = g_fighters[1].world_pos.x - g_fighters[0].world_pos.x;
    s32 dz = g_fighters[1].world_pos.z - g_fighters[0].world_pos.z;
    s32 halving_count = 0;
    while (dx < -DISTANCE_TERM_MAX || dx > DISTANCE_TERM_MAX ||
           dz < -DISTANCE_TERM_MAX || dz > DISTANCE_TERM_MAX) {
        dx /= 2;
        dz /= 2;
        halving_count += 1;
    }
    {
        s32 dx_squared = dx * dx;
        s32 dz_squared = dz * dz;
        s32 root = SquareRoot0(dx_squared + dz_squared);
        return root << halving_count;
    }
}

/* How fast the fighter's move runs this frame (motion.h): the move's frame
 * counter advances by Fighter.speed, FIGHTER_SPEED_ONE a frame. A fighter
 * has a speed of its own, and its wounds slow it: every move for wounds to
 * the legs and the body, and attacks, outside the frames in which the
 * weapon can hit, also for a lost off arm and for wounds to the sword arm.
 * What is done to a fighter (being parried, locked, thrown down, dying) and
 * its win pose run at full speed. */
void motion_UpdateFrameStep(Fighter *fighter) {
    u16 kind;
    s32 frame;
    s32 frame_step;
    s32 index;
    s32 factor;

    kind = fighter->move_kind;
    frame_step = fighter->base_speed;
    if (kind == MOVE_KIND_PAIRED || kind == MOVE_KIND_LOCK ||
        kind == MOVE_KIND_START || kind == MOVE_KIND_LOCK_EVEN ||
        kind == MOVE_KIND_LOCK_WINNING_1 || kind == MOVE_KIND_LOCK_WINNING_2 ||
        kind == MOVE_KIND_LOCK_LOSING_1 || kind == MOVE_KIND_LOCK_LOSING_2 ||
        kind == MOVE_KIND_DEFLECT || kind == MOVE_KIND_DEFLECTED ||
        kind == MOVE_KIND_BLOCK || kind == MOVE_KIND_STAGGER ||
        kind == MOVE_KIND_WIN || kind == MOVE_KIND_DTH) {
        fighter->speed = FIGHTER_SPEED_ONE;
        return;
    }
    if (MOVE_IS_ATTACK(kind)) {
        frame = fighter->move_frame;
        if ((frame >= fighter->hit_start_frames[0] &&
             frame <= fighter->hit_end_frames[0]) ||
            (frame >= fighter->hit_start_frames[1] &&
             frame <= fighter->hit_end_frames[1])) {
            fighter->speed = FIGHTER_SPEED_ONE;
            return;
        }
        if (!fighter->off_arm_ok) {
            s32 lost_arm_speed = fighter->lost_arm_speed;
            frame_step = (frame_step * lost_arm_speed) >> 12;
        }
        {
            /* the tables go as far as three wounds */
            s16 arm_wounds = fighter->arm_wounds;
            if (arm_wounds >= 4) {
                arm_wounds = 3;
            }
            index = arm_wounds;
        }
        factor = fighter->arm_wound_speeds[index];
        frame_step = (frame_step * factor) >> 12;
    }
    {
        s32 leg_wounds = fighter->leg_wounds;
        s32 body_wounds = fighter->body_wounds;
        s32 wounds = leg_wounds + body_wounds;
        s32 capped = (wounds < 4) ? wounds : 3;
        index = capped;
    }
    factor = fighter->wound_speeds[index];
    frame_step = (frame_step * factor) >> 12;
    fighter->speed = frame_step;
}

/* The command of `move` that begins a paired move, or NULL. */
static PairedMoveEntry *motion_FindPairedMoveEntry(MoveScript *move) {
    u16 flags;
    u16 *command;

    command = move->commands;
    flags = *command;
    while (flags != 0) {
        if (flags & MOVE_COMMAND_PAIRED) {
            return (PairedMoveEntry *)command;
        }
        if (flags & MOVE_COMMAND_WIDE) {
            command += 4;
        } else {
            command += 2;
        }
        flags = *command;
    }
    return NULL;
}

/* ---- the fight (fight.h): the two fighters' distance and their paired
 * moves ---- */

/* Whether the fighter may catch the other in a paired move: not in the
 * modes without them, not against the two opponents that cannot be caught,
 * and Jo only while he carries the sub-weapon his paired moves strike
 * with. */
static s32 fighter_CanStartPairedMove(Fighter *fighter, s32 needs_off_arm) {
    if (g_fight_mode == FIGHT_MODE_PRACTICE ||
        g_fight_mode == FIGHT_MODE_TEAM ||
        g_fight_mode == FIGHT_MODE_CHANBARA) {
        return 0;
    }
    if (g_fight_mode == FIGHT_MODE_STORY && g_story_victim != 0) {
        return 0;
    }
    if (fighter->other->roster_id == ROSTER_VANISHER ||
        fighter->other->roster_id == ROSTER_ARMORED) {
        return 0;
    }
    if (fighter->character == CHARACTER_JO &&
        (fighter->sub_weapon_count == 0 ||
         fighter->sub_weapons[0] != OBJECT_JO_SUB_WEAPON)) {
        return 0;
    }
    if (needs_off_arm) {
        return fighter->off_arm_ok != 0;
    }
    return 1;
}

/* A paired move's command: the bytes after its move id. */
enum {
    PAIRED_MOVE_NEAREST = 0,    /* how far apart the two may be, ... */
    PAIRED_MOVE_FARTHEST = 1,   /* ... in units of PAIRED_MOVE_UNIT */
    PAIRED_MOVE_SEPARATION = 2, /* how far apart the move holds them */
    /* the frame from which the move breaks off if they come apart; and
     * PAIRED_MOVE_NEEDS_OFF_ARM */
    PAIRED_MOVE_BREAK_FRAME = 3,
};

#define PAIRED_MOVE_UNIT 20
#define PAIRED_MOVE_NEEDS_OFF_ARM 0x80
#define PAIRED_MOVE_FRAME_MASK 0x7F
/* How far above or below the other a fighter can be caught. */
#define PAIRED_MOVE_HEIGHT 100

/* Once a frame, ahead of the fighters: puts the two into a paired move.
 * First the move that one already going on has led to (asked for by the
 * fighter's code): both go into it, the one it is done to borrowing the
 * other's motion if the move is in the other's file. Then two that have
 * fallen off something in a paired move: both go into the fall. Otherwise
 * a fighter whose move has just begun, and has a paired move among its
 * commands, catches the other if that one is at the right distance and
 * height and only standing, walking or having its weapon beaten aside:
 * both go into the paired move, which wounds the one caught. */
static void fight_StartPairedMoves(void) {
    Fighter *fighter;
    PairedMoveEntry *entry;
    u8 *paired;
    MoveScript *move;
    s32 nearest;
    s32 farthest;
    s32 height;
    s32 i;
    u16 kind;
    s32 pos[3];

    if (g_fight_paired_move_start_pending != 0) {
        g_fight_paired_move_start_pending = 0;
        g_fight_paired_move_break_frame = MOVE_NO_FRAME;
        if (g_fighters[0].is_defeated) {
            return;
        }
        if (g_fighters[1].is_defeated) {
            return;
        }
        fighter_StartMove(g_fight_paired_move_initiator, g_fight_paired_move,
                          g_fight_paired_move_is_common);
        if (g_fight_paired_move_flags & MOVE_COMMAND_BORROWED) {
            g_fighters[g_fight_paired_move_initiator == 0].borrow_other_motion =
                1;
        }
        fighter_StartMove(g_fight_paired_move_initiator == 0,
                          g_fight_paired_move, g_fight_paired_move_is_common);
        g_fighters[1].move_started = MOVE_START_PAIRED;
        g_fighters[0].move_started = MOVE_START_PAIRED;
        return;
    }
    if (g_fight_falling_fighter != FIGHT_NOBODY_FALLING) {
        fighter = &g_fighters[g_fight_falling_fighter];
        if (g_fight_falling_forward) {
            fighter->pending_event = PAIRED_EVENT_FALL_FORWARD;
            fighter->falling_backward = 0;
        } else {
            fighter->pending_event = PAIRED_EVENT_FALL_BACKWARD;
            fighter->falling_backward = 1;
        }
        if (fighter->is_defeated) {
            fighter->pending_event += PAIRED_EVENT_DEAD;
        }
        fighter->fall_start_y = fighter->foot_pos.vy;
        fighter->other->pending_event = PAIRED_EVENT_FALL_FORWARD;
        fighter->other->falling_backward = 0;
        fighter->other->fall_start_y = fighter->other->foot_pos.vy;
        if (fighter->other->is_defeated) {
            fighter->other->pending_event += PAIRED_EVENT_DEAD;
        }
        g_fight_falling_fighter = FIGHT_NOBODY_FALLING;
        return;
    }
    if (g_fighters[0].pending_event != MOVE_EVENT_NONE) {
        return;
    }
    if (g_fighters[1].pending_event != MOVE_EVENT_NONE) {
        return;
    }
    for (i = 0; i < 2; i++) {
        fighter = &g_fighters[i];
        if (fighter->move_started == MOVE_START_NONE) {
            continue;
        }
        entry = motion_FindPairedMoveEntry(fighter->cur_move);
        if (!entry) {
            continue;
        }
        paired = entry->b;
        nearest = paired[PAIRED_MOVE_NEAREST] * PAIRED_MOVE_UNIT;
        farthest = paired[PAIRED_MOVE_FARTHEST] * PAIRED_MOVE_UNIT;
        height = fighter->foot_pos.vy - fighter->other->foot_pos.vy;
        if (!fighter_CanStartPairedMove(
                fighter,
                paired[PAIRED_MOVE_BREAK_FRAME] & PAIRED_MOVE_NEEDS_OFF_ARM)) {
            continue;
        }
        if (g_fight_distance < nearest) {
            continue;
        }
        if (farthest < g_fight_distance) {
            continue;
        }
        if (height <= -PAIRED_MOVE_HEIGHT || height >= PAIRED_MOVE_HEIGHT) {
            continue;
        }
        kind = fighter->other->move_kind;
        if (kind != MOVE_KIND_STANCE && kind != MOVE_KIND_DEFLECTED &&
            kind != MOVE_KIND_DEFLECT && kind != MOVE_KIND_WALK) {
            continue;
        }
        g_fight_paired_move_initiator = i;
        g_fight_paired_move_start_pending = 0;
        g_fight_falling_fighter = FIGHT_NOBODY_FALLING;
        g_fight_paired_move_separation =
            paired[PAIRED_MOVE_SEPARATION] * PAIRED_MOVE_UNIT;
        g_fight_paired_move_break_frame =
            paired[PAIRED_MOVE_BREAK_FRAME] & PAIRED_MOVE_FRAME_MASK;
        move = motion_GetMove(fighter, entry->id, &fighter->move_is_common);
        fighter_StartMove(i, move, fighter->move_is_common);
        fighter->other->borrow_other_motion = 1;
        fighter->other->move_is_common = fighter->move_is_common;
        fighter_StartMove(i == 0, move, fighter->move_is_common);
        fighter->move_started = MOVE_START_PAIRED;
        fighter->other->move_started = MOVE_START_PAIRED;
        fighter->other->stance = fighter->other->default_stance;
        fighter->other->body_wounds += 1;
        pos[0] = (fighter->world_pos.x + fighter->other->world_pos.x) / 2;
        pos[1] = (fighter->world_pos.y + fighter->other->world_pos.y) / 2;
        pos[2] = (fighter->world_pos.z + fighter->other->world_pos.z) / 2;
        fight_RaiseEvent(i, FIGHT_EVENT_PAIRED_MOVE, pos, NULL);
        return;
    }
}

/* Once a frame, ahead of the fighters: measures how far apart the two are,
 * and puts them into a paired move in the modes that have them. */
void fight_UpdateDistance(void) {
    s32 old_distance = g_fight_distance;
    s32 distance = fight_MeasureDistance();
    s32 mode = g_fight_mode;
    g_fight_distance = distance;
    g_fight_distance_change = distance - old_distance;
    if (mode != FIGHT_MODE_TEAM && mode != FIGHT_MODE_PRACTICE) {
        fight_StartPairedMoves();
    }
    g_fight_lock_requested = 0;
}

/* The most a lob's speed across the ground may be, and what the roots of
 * the sum are kept times, for the fraction. */
#define LOB_SPEED_MAX 300
#define LOB_FRACTION 32

/* The speed across the ground that carries something thrown upwards at
 * `lift`, and falling at `gravity`, from one
 * place to another: LOB_SPEED_MAX at most, which is also what it gets when
 * the target is too high to reach. Only x and z of `velocity_out` are set. */
void math_LobVelocity(
    s32 *from, s32 *to, s32 lift, s32 gravity, s32 *velocity_out) {
    s32 discriminant_root_x32;
    s32 dx;
    s32 dz;
    s32 dist;

    dx = to[0] - from[0];
    dz = to[2] - from[2];
    dist = SquareRoot0(dx * dx + dz * dz);

    if (dist == 0) {
        velocity_out[2] = 0;
        velocity_out[0] = 0;
        return;
    }

    {
        s32 dy;

        dy = to[1] - from[1];

        if (dy == 0) {
            s32 neg = -gravity;
            s32 denom = lift * 2;
            velocity_out[0] = (neg * dx) / denom;
            velocity_out[2] = (neg * dz) / denom;
        } else {
            s32 dy2 = dy * 2;
            s32 horizontal_speed;

            discriminant_root_x32 = lift * lift + gravity * dy2;

            if (discriminant_root_x32 >= 0) {
                s32 vertical_velocity_x32;
                discriminant_root_x32 =
                    SquareRoot0(discriminant_root_x32 << 10);
                vertical_velocity_x32 = lift << 5;
                horizontal_speed =
                    ((vertical_velocity_x32 + discriminant_root_x32) * dist) /
                    dy2 / LOB_FRACTION;

                if (horizontal_speed < 0) {
                    /* the other root. FAKE: kept in dy, which is free */
                    dy = vertical_velocity_x32 - discriminant_root_x32;
                    horizontal_speed = (dy * dist) / dy2 / LOB_FRACTION;
                }
            } else {
                horizontal_speed = LOB_SPEED_MAX;
            }

            if (horizontal_speed > LOB_SPEED_MAX) {
                horizontal_speed = LOB_SPEED_MAX;
            }

            velocity_out[0] = (horizontal_speed * dx) / dist;
            velocity_out[2] = (horizontal_speed * dz) / dist;
        }
    }
}

/* A mortal wound bleeds: remembers the bone of the body point that was
 * hit and, as that bone sees it, the direction the blow came from. */
void fighter_StartBleeding(Fighter *fighter, s32 body_point, s16 *blow_dir) {
    MATRIX inverse;
    s32 dir[3];
    MATRIX *bone_matrix;

    fighter->bleed_timer = 1;
    fighter->bleed_bone = g_fighter_body_point_table[body_point].bone;
    bone_matrix =
        PTR(model_GetBoneMatrices(fighter->index)[fighter->bleed_bone]);
    math_InvertMatrix(bone_matrix, &inverse);
    gte_SetRotMatrix(&inverse);
    dir[0] = blow_dir[0];
    dir[1] = blow_dir[1];
    dir[2] = blow_dir[2];
    gte_ldlv0(dir);
    gte_rtv0();
    gte_stlvnl(&fighter->bleed_dir);
}

/* How long a mortal wound was meant to bleed. */
#define BLEED_FRAMES 150

/* The frame after a mortal wound, blood spurts from it, back along the
 * blow as the bone now lies (but not downwards). The code is written to
 * bleed every eighth frame for BLEED_FRAMES, more weakly each time; the
 * timer is then cleared every frame, so it bleeds once: the original's
 * behaviour. The place is the body point with the bone's number, which is
 * not the point that was hit: the original's behaviour too. */
void fighter_UpdateBleeding(Fighter *fighter) {
    s32 dir[3];
    s16 spurt_dir[3];
    s32 fighter_index;
    s32 strength; /* of ONE: what is left of the bleeding */
    MatrixSlot *bone_matrices;

    fighter_index = fighter->index;
    if (fighter->bleed_timer != 0) {
        fighter->bleed_timer += 1;
        /* every eighth frame, from the second */
        if ((fighter->bleed_timer & 7) == 2) {
            bone_matrices = model_GetBoneMatrices(fighter_index);
            gte_SetRotMatrix(PTR(bone_matrices[fighter->bleed_bone]));
            gte_ldlv0(&fighter->bleed_dir);
            gte_rtv0();
            strength =
                ((BLEED_FRAMES - fighter->bleed_timer) << 12) / BLEED_FRAMES;
            gte_stlvnl(dir);
            spurt_dir[0] = (dir[0] * strength) / ONE;
            spurt_dir[1] = (dir[1] * strength) / ONE;
            spurt_dir[2] = (dir[2] * strength) / ONE;
            /* more down than across: the other way */
            if (spurt_dir[1] > ONE / 2) {
                spurt_dir[0] = -spurt_dir[0];
                spurt_dir[1] = -spurt_dir[1];
                spurt_dir[2] = -spurt_dir[2];
            }
            fight_RaiseEvent(
                fighter_index, FIGHT_EVENT_BLOOD,
                &SPAD->body_points[fighter_index][fighter->bleed_bone].x,
                spurt_dir);
        }
        if (fighter->bleed_timer >= BLEED_FRAMES) {
            fighter->bleed_timer = 0;
        }
        fighter->bleed_timer = 0;
    }
}

/* Makes the fighter's body points from the table's, for its size. The
 * squares of the radii are scaled once, as the radii are, so for a fighter
 * not of full size they are no longer the radii's squares: the original's
 * behaviour. */
void fighter_ScaleBodyPoints(s32 fighter_index, s32 size_scale) {
    BodyPoint *src = g_fighter_body_point_table;
    BodyPoint *dst = g_fighter_body_points[fighter_index];
    s32 i;

    for (i = 0; i < BODY_POINT_COUNT; i++, src++, dst++) {
        dst->has_core = src->has_core;
        dst->bone = src->bone;
        dst->ofs.vx = (src->ofs.vx * size_scale) >> 12;
        dst->ofs.vy = (src->ofs.vy * size_scale) >> 12;
        dst->ofs.vz = (src->ofs.vz * size_scale) >> 12;
        dst->radius = (src->radius * size_scale) >> 12;
        dst->reach_sq = (src->reach_sq * size_scale) >> 12;
        dst->core_radius = (src->core_radius * size_scale) >> 12;
        dst->core_reach_sq = (src->core_reach_sq * size_scale) >> 12;
    }
}

/* The words of a cast's arguments where fighter_PlacePoints keeps them:
 * where it starts and ends, where it hit, the normal there (as halfwords),
 * and the cast's work area. */
enum CastWord {
    CAST_FROM = 0,
    CAST_TO = 4,
    CAST_HIT = 8,
    CAST_NORMAL = 12,
    CAST_WORK = 14,
};

/* The sole is 142 below the foot's bone on a fighter of full size: 71 for
 * each half of its scale. The ground under a foot is looked for from a
 * little above the sole to well below it. */
#define FOOT_SOLE_DEPTH_X2 71
#define FOOT_CAST_ABOVE 100
#define FOOT_CAST_BELOW 2000

/* Works out, from the posed model's bones, where the fighter's points are
 * this frame: the 22 points of its body, the points of its weapon from the
 * weapon hand and, while the off hand's weapon is out, that one's two.
 * Also what the rest of the frame wants to know of the body: which way the
 * top of the torso points, where the head is, and where the two feet are,
 * which way each points and how far below it the ground is (for
 * footprints and splashes). */
void fighter_PlacePoints(Fighter *fighter, Vec3 *body_points,
                         Vec3 *weapon_points, Vec3 *second_points) {
    /* a cast's arguments, on the scratchpad, as words (enum CastWord) */
    s32 *cast = (s32 *)FIGHT_CAST_WORK;
    MatrixSlot *bone_matrices;
    MATRIX *bone_matrix;
    s32 *foot;
    SVECTOR *local_point;
    BodyPoint *body_point;
    Vec3 *point;
    s32 i;

    bone_matrices = model_GetBoneMatrices(fighter->index);
    body_point = g_fighter_body_points[fighter->index];
    point = body_points;
    for (i = 0; i < BODY_POINT_COUNT; i++, body_point++, point++) {
        bone_matrix = PTR(bone_matrices[body_point->bone]);
        gte_ApplyMatrix(bone_matrix, &body_point->ofs, point);
        point->x += bone_matrix->t[0];
        point->y += bone_matrix->t[1];
        point->z += bone_matrix->t[2];
    }

    bone_matrix = PTR(bone_matrices[FIGHTER_BONE_WEAPON_HAND]);
    gte_SetRotMatrix(bone_matrix);
    if (fighter->weapon_model == WEAPON_MODEL_STORY) {
        local_point = g_fighter_story_weapon_points;
    } else {
        local_point = g_fighter_weapon_point_sets[fighter->weapon_id];
#if RB_PORT
        /* a mod's weapon's points (port_weapons.h), as many as the
         * game's: the game's own with none */
        local_point =
            port_WeaponPoints(fighter->index, fighter->weapon_id, local_point);
#endif
    }
    point = weapon_points;
    for (i = 0; i < g_fighter_weapon_point_counts[fighter->weapon_id]; i++,
        local_point++, point++) {
        gte_ApplyRotMatrix(local_point, point);
        point->x += bone_matrix->t[0];
        point->y += bone_matrix->t[1];
        point->z += bone_matrix->t[2];
    }

    if (fighter->sub_weapon_live_state != WEAPON_LIVE_OFF) {
        bone_matrix = PTR(bone_matrices[FIGHTER_BONE_OFF_HAND]);
        gte_SetRotMatrix(bone_matrix);
        local_point = g_fighter_sub_weapon_points[fighter->sub_weapon_kind];
        point = second_points;
        for (i = 0; i < 2; i++, local_point++, point++) {
            gte_ApplyRotMatrix(local_point, point);
            point->x += bone_matrix->t[0];
            point->y += bone_matrix->t[1];
            point->z += bone_matrix->t[2];
        }
    }

    bone_matrix = PTR(bone_matrices[FIGHTER_BONE_TORSO]);
    gte_SetRotMatrix(bone_matrix);
    gte_ldv0(&g_unit_y_vector);
    gte_rtv0();
    fighter->head_pos = body_points[BODY_HEAD];
    gte_stlvnl(&fighter->torso_axis);

    foot = PTR(bone_matrices[FIGHTER_BONE_FOOT_0])->t;
    fighter->foot_points[0].x = foot[0];
    fighter->foot_points[0].y =
        foot[1] + ((fighter->size_scale * FOOT_SOLE_DEPTH_X2) >> 11);
    fighter->foot_points[0].z = foot[2];
    foot = PTR(bone_matrices[FIGHTER_BONE_FOOT_1])->t;
    fighter->foot_points[1].x = foot[0];
    fighter->foot_points[1].y =
        foot[1] + ((fighter->size_scale * FOOT_SOLE_DEPTH_X2) >> 11);
    fighter->foot_points[1].z = foot[2];
    /* the ground under each foot: looked for from a little above the sole
     * to well below it */
    for (i = 0; i < 2; i++) {
        cast[CAST_FROM + 0] = fighter->foot_points[i].x;
        cast[CAST_FROM + 1] = fighter->foot_points[i].y - FOOT_CAST_ABOVE;
        cast[CAST_FROM + 2] = fighter->foot_points[i].z;
        cast[CAST_TO + 0] = fighter->foot_points[i].x;
        cast[CAST_TO + 1] = fighter->foot_points[i].y + FOOT_CAST_BELOW;
        cast[CAST_TO + 2] = fighter->foot_points[i].z;
        if (collision_CastWith(
                &cast[CAST_FROM], &cast[CAST_TO], &cast[CAST_HIT],
                (s16 *)&cast[CAST_NORMAL], (SegmentCast *)&cast[CAST_WORK])) {
            fighter->foot_ground_ys[i] = cast[CAST_HIT + 1];
        } else {
            fighter->foot_ground_ys[i] = cast[CAST_TO + 1];
        }
    }
    bone_matrix = PTR(bone_matrices[FIGHTER_BONE_FOOT_0]);
    /* the way the bone's z axis points, turned about */
    fighter->foot_angles[0].vy =
        ratan2(bone_matrix->m[0][2], bone_matrix->m[2][2]) + ANGLE_TURN / 2;
    bone_matrix = PTR(bone_matrices[FIGHTER_BONE_FOOT_1]);
    fighter->foot_angles[1].vy =
        ratan2(bone_matrix->m[0][2], bone_matrix->m[2][2]) + ANGLE_TURN / 2;
}
