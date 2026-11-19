/* What happens in a fight, heard and seen, and a stage's own sounds. One
 * file of the original's at least from fighter_PlayFrameEvent to
 * snd_GetStagePropKind: their jump tables are a padding word apart, which
 * only one file's alignment gives. Two parts: the fight's events, and the
 * stage's props and ambience. */
#define INCLUDE_ASM_USE_MACRO_INC 1
#include "common.h"
#include "include_asm.h"
#include <psxsdk/inline_o.h>
#include <psxsdk/gtemac.h>
#include "bb2.h"

/* The first part: a move's marks and the sounds and footprints its motion
 * plays (fighter.h), a sound heard from where the camera is (snd.h), and
 * the fight's events, each a sound or an effect. */

/* ---- the fighter (fighter.h): a move's marks and sounds ---- */

/* Reads the marks of the move the fighter has begun (motion.h): the frames
 * at which its weapons can hit, something is thrown or picked up, and so
 * on. A move without a mark has it at a frame that never comes, or
 * always. */
void fighter_ReadMarks(Fighter *fighter) {
    u8 *marks;
    u8 mark;
    u8 frame;

    marks = fighter->motion_head + MOTION_HEAD_SIZE;
    fighter->hit_start_frames[0] = MOVE_NO_FRAME;
    fighter->hit_end_frames[0] = MOVE_NO_FRAME;
    fighter->hit_start_frames[1] = MOVE_NO_FRAME;
    fighter->hit_end_frames[1] = MOVE_NO_FRAME;
    fighter->paired_hit_frame = 0;
    fighter->release_frame = 0;
    fighter->pickup_frame = 0;
    fighter->off_hand_start = 0;
    fighter->off_hand_end = MOVE_NO_FRAME;
    fighter->counter_frame = MOVE_NO_FRAME;
    fighter->reload_frame = MOVE_NO_FRAME;

    while ((mark = *marks++) != 0) {
        if (mark == MOVE_MARK_FOR_SOME) {
            /* only frame events are for some characters */
            marks += MOVE_MARK_MASK_SIZE + 2;
        } else if (mark < MOVE_MARK_FIRST) {
            marks++;
        } else {
            frame = *marks++;
            switch (mark) {
            case MOVE_MARK_HIT_START:
                fighter->hit_start_frames[0] = frame;
                break;
            case MOVE_MARK_HIT_END:
                fighter->hit_end_frames[0] = frame;
                break;
            case MOVE_MARK_RELEASE:
                fighter->release_frame = frame;
                break;
            case MOVE_MARK_PICKUP:
                fighter->pickup_frame = frame;
                break;
            case MOVE_MARK_FACE:
                fighter->face_frame = frame;
                break;
            case MOVE_MARK_OFF_HAND_START:
                fighter->off_hand_start = frame;
                break;
            case MOVE_MARK_OFF_HAND_END:
                fighter->off_hand_end = frame;
                break;
            case MOVE_MARK_BLADE_2_HIT_START:
                fighter->hit_start_frames[1] = frame;
                break;
            case MOVE_MARK_BLADE_2_HIT_END:
                fighter->hit_end_frames[1] = frame;
                break;
            case MOVE_MARK_PAIRED_HIT:
                fighter->paired_hit_frame = frame;
                break;
            case MOVE_MARK_COUNTER:
                fighter->counter_frame = frame;
                break;
            case MOVE_MARK_RELOAD:
                fighter->reload_frame = frame;
                break;
            }
        }
    }
}

/* A sound is heard at full volume within 8000 of the camera, and from
 * there more faintly, to nothing at SND_HEARING_RANGE. One that is off to a
 * side is fainter in both ears: gone from the far one at SND_PAN_FAR_RANGE
 * to that side, from the near one at SND_PAN_NEAR_RANGE. */
#define SND_HEARING_RANGE 40000
#define SND_FALLOFF_RANGE 32000
#define SND_PAN_NEAR_RANGE 30000
#define SND_PAN_FAR_RANGE 10000

/* The view a sound is heard from. As a story fight begins the henchman's
 * shout is placed before a camera has been chosen (story_HenchmanShout,
 * fight.c), and the original reads the view through a pointer that is
 * still NULL: on the console that is address 0, where the kernel's
 * memory is zeroes as far as the words read here -- the eye at 0x20 and
 * the heading at 0x12 -- which was read out of the emulator. A PC has no
 * memory at address 0, so the port reads the zeroes it keeps for such a
 * place (docs/PORT.md, "Memory"). On the PlayStation this is
 * `g_camera_current` and nothing else. */
#if RB_PORT
static const CameraView *snd_Listener(void) {
    return g_camera_current != NULL ? g_camera_current
                                    : (const CameraView *)g_port_low_memory;
}

#define SND_LISTENER snd_Listener()
#else
#define SND_LISTENER g_camera_current
#endif

/* Plays a sound as heard from the camera. The volume falls with distance
 * (the distance is a table square root; past the table it is scaled by the
 * number's leading zeros). The camera's heading gives how far to one side
 * the sound is: the far ear's volume falls off three times as fast as the
 * near ear's. */
void snd_PlayAt(s32 sound, s32 *position) {
    s32 leading_zeros;
    s32 dx, dy, dz;
    u32 distance;
    s32 distance_scale;
    s16 listener_yaw;
    s32 pan_offset;
    u32 swap_channels;
    s32 right_volume;
    s32 left_volume;

    dx = SND_LISTENER->eye.x - position[0];
    dy = SND_LISTENER->eye.y - position[1];
    dz = SND_LISTENER->eye.z - position[2];

    if (((dx + SND_HEARING_RANGE) > 2U * SND_HEARING_RANGE) ||
        ((dz + SND_HEARING_RANGE) > 2U * SND_HEARING_RANGE)) {
        distance = SND_HEARING_RANGE;
    } else {
        u32 dist_sq = (dx * dx) + (dy * dy) + (dz * dz);
        /* MATH_SQRT, written out */
        if (dist_sq < (u32)MATH_SQRT_TABLE_SIZE) {
            distance = g_math_sqrt8_table[dist_sq] >> 3;
        } else {
            gte_Lzc(dist_sq, &leading_zeros);
            {
                /* FAKE: shift = MATH_SQRT_SHIFT - (leading_zeros & ~1) */
                u32 even_zeros = -2;
                u32 shift;
                u32 index;
                u32 root8;
                even_zeros &= leading_zeros;
                shift = MATH_SQRT_SHIFT - even_zeros;
                index = dist_sq >> shift;
                /* Halve the table-index shift for the result scale. */
                shift = shift >> 1;
                root8 = g_math_sqrt8_table[index];
                distance = (root8 << 16) >> (MATH_SQRT_UNSHIFT - shift);
            }
        }
    }

    /* 0x800 and over is full volume */
    distance_scale =
        (s32)((SND_HEARING_RANGE - distance) << 11) / SND_FALLOFF_RANGE;
    if (distance_scale < 0) {
        distance_scale = 0;
    }

    listener_yaw = SND_LISTENER->angles.vy;
    /* how far the sound is to the camera's side */
    pan_offset =
        ((dx * g_math_sin_table[(listener_yaw + ANGLE_TURN / 4) & ANGLE_MASK]) +
         (dz * g_math_sin_table[listener_yaw & ANGLE_MASK])) >>
        12;
    swap_channels = ~pan_offset;
    swap_channels >>= 31;
    if (pan_offset < 0) {
        pan_offset = -pan_offset;
    }

    right_volume =
        ((SND_PAN_NEAR_RANGE - pan_offset) << 12) / SND_PAN_NEAR_RANGE;
    left_volume = ((SND_PAN_FAR_RANGE - pan_offset) << 12) / SND_PAN_FAR_RANGE;
    if (right_volume < 0) {
        right_volume = 0;
    }
    if (left_volume < 0) {
        left_volume = 0;
    }

    {
        s32 near_volume = right_volume;
        if (swap_channels) {
            right_volume = left_volume;
            left_volume = near_volume;
        }
    }

    right_volume = (distance_scale * right_volume) >> 16;
    left_volume = (distance_scale * left_volume) >> 16;
    if (right_volume > SND_VOLUME_MAX) {
        right_volume = SND_VOLUME_MAX;
    }
    if (left_volume > SND_VOLUME_MAX) {
        left_volume = SND_VOLUME_MAX;
    }

    snd_Play(sound, right_volume, left_volume);
}

/* ---- the fight's events ---- */

/* Something happened in the fight (enum FightEvent): the replay records it,
 * and it is heard or seen. A sound that is a fighter's own is that
 * fighter's; chanbara's opponents share one set of weapon sounds and
 * have a voice each. */
void fight_RaiseEvent(s32 fighter_index, s32 event, s32 *pos, s16 *dir) {
    s32 sound_offset;
    s32 second_sound_offset;

    if (g_fight_mode == FIGHT_MODE_CHANBARA && fighter_index == 1 &&
        (event == FIGHT_EVENT_HEAD_CUT || event == FIGHT_EVENT_BODY_CUT ||
         event == FIGHT_EVENT_LIMB_CUT || event == FIGHT_EVENT_CLANG ||
         event == FIGHT_EVENT_HARD_CLANG ||
         event == FIGHT_EVENT_SECOND_SWORD_CUT ||
         event == FIGHT_EVENT_SHEATHED_HIT ||
         event == FIGHT_EVENT_WEAPON_LANDS)) {
        second_sound_offset = 0;
        sound_offset = 0;
    } else {
        sound_offset = fighter_index * SND_FIGHTER_COUNT;
        second_sound_offset = SND_FIGHTER_COUNT;
    }

    if (g_fight_mode == FIGHT_MODE_CHANBARA && fighter_index == 1 &&
        (event == FIGHT_EVENT_GRUNT || event == FIGHT_EVENT_DEATH_CRY)) {
        sound_offset += g_chanbara_voice_offsets[g_chanbara_kind];
    }

    replay_RecordEvent(fighter_index, event, pos, dir);

    switch (event) {
    case FIGHT_EVENT_BLOOD:
        if (g_fight_mode == FIGHT_MODE_PRACTICE &&
            g_fight_practice_round == 0) {
            eff_StartBluntHit(pos);
            break;
        }
        if (g_fight_mode == FIGHT_MODE_TEAM) {
            eff_StartBluntHit(pos);
            break;
        }
        eff_StartBlood(pos, dir);
        break;
    case FIGHT_EVENT_HIT:
        eff_StartHitFlash(pos);
        break;
    case FIGHT_EVENT_CLASH:
        eff_StartClashStreak(pos, fighter_index);
        break;
    case FIGHT_EVENT_MATCHED_CLASH:
        eff_StartMatchedStreak(pos, fighter_index);
        break;
    case FIGHT_EVENT_SPARK:
        eff_StartClashSpark(pos);
        break;
    case FIGHT_EVENT_LOCK:
        snd_PlayAt(SND_FIGHTER_LOCK, pos);
        snd_PlayAt(second_sound_offset + SND_FIGHTER_LOCK, pos);
        g_fight_lock_flash_delay = eff_StartLockFlash(pos);
        break;
    case FIGHT_EVENT_LOCK_SLOW:
        snd_PlayAt(SND_FIGHTER_LOCK, pos);
        snd_PlayAt(second_sound_offset + SND_FIGHTER_LOCK, pos);
        g_fight_lock_flash_delay = eff_StartLockFlashSlow(pos);
        break;
    case FIGHT_EVENT_LOCK_MEDIUM:
        snd_PlayAt(SND_FIGHTER_LOCK, pos);
        snd_PlayAt(second_sound_offset + SND_FIGHTER_LOCK, pos);
        g_fight_lock_flash_delay = eff_StartLockFlashMedium(pos);
        break;
    case FIGHT_EVENT_LOCK_FAST:
        snd_PlayAt(SND_FIGHTER_LOCK, pos);
        snd_PlayAt(second_sound_offset + SND_FIGHTER_LOCK, pos);
        g_fight_lock_flash_delay = eff_StartLockFlashFast(pos);
        break;
    case FIGHT_EVENT_SPLASH:
        eff_StartSplash(pos);
        break;
    case FIGHT_EVENT_DUST:
        eff_StartDust(pos);
        break;
    case FIGHT_EVENT_SHOT:
        snd_PlayAt(sound_offset + SND_FIGHTER_SHOT, pos);
        eff_StartMuzzleFlash(pos, dir);
        break;
    case FIGHT_EVENT_THROWN_EARTH:
        snd_PlayAt(SND_FIGHT_THROWN_EARTH, pos);
        eff_StartThrownDirt(pos, dir[1]);
        break;
    case FIGHT_EVENT_THROWN_WATER:
        snd_PlayAt(SND_FIGHT_THROWN_WATER, pos);
        eff_StartThrownWater(pos, dir[1]);
        break;
    case FIGHT_EVENT_BULLET_IMPACT:
        snd_PlayAt(sound_offset + SND_FIGHTER_IMPACT, pos);
        eff_StartBulletImpact(pos, dir);
        break;
    case FIGHT_EVENT_PICK_UP:
        snd_PlayAt(sound_offset + SND_FIGHTER_PICK_UP, pos);
        break;
    case FIGHT_EVENT_ARMOR_SPARK:
        eff_StartArmorSpark(pos);
        snd_PlayAt(sound_offset + SND_FIGHTER_IMPACT, pos);
        break;
    case FIGHT_EVENT_COLLIDE:
        snd_PlayAt(SND_FIGHT_COLLIDE, pos);
        break;
    case FIGHT_EVENT_HEAD_CUT:
        snd_PlayAt(sound_offset + SND_FIGHTER_CUT, pos);
        break;
    case FIGHT_EVENT_BODY_CUT:
        snd_PlayAt(sound_offset + SND_FIGHTER_CUT, pos);
        break;
    case FIGHT_EVENT_LIMB_CUT:
        snd_PlayAt(sound_offset + SND_FIGHTER_LIMB_CUT, pos);
        break;
    case FIGHT_EVENT_CLANG:
        snd_PlayAt(sound_offset + SND_FIGHTER_CLANG, pos);
        break;
    case FIGHT_EVENT_HARD_CLANG:
        snd_PlayAt(sound_offset + SND_FIGHTER_HARD_CLANG, pos);
        break;
    case FIGHT_EVENT_SECOND_SWORD_CUT:
        snd_PlayAt(sound_offset + SND_FIGHTER_IMPACT, pos);
        break;
    case FIGHT_EVENT_SHEATHED_HIT:
        snd_PlayAt(sound_offset + SND_FIGHTER_SHEATHED_HIT, pos);
        break;
    case FIGHT_EVENT_WEAPON_LANDS:
        snd_PlayAt(sound_offset + SND_FIGHTER_WEAPON_LANDS, pos);
        break;
    case FIGHT_EVENT_THROW:
        snd_PlayAt(sound_offset + SND_FIGHTER_THROW, pos);
        break;
    case FIGHT_EVENT_OBJECT_HITS:
        snd_PlayAt(sound_offset + SND_FIGHTER_IMPACT, pos);
        break;
    case FIGHT_EVENT_OBJECT_LANDS:
        snd_PlayAt(sound_offset + SND_FIGHTER_OBJECT_LANDS, pos);
        break;
    case FIGHT_EVENT_GRUNT:
        snd_PlayAt(sound_offset + SND_FIGHTER_GRUNT, pos);
        break;
    case FIGHT_EVENT_DEATH_CRY:
        snd_PlayAt(sound_offset + SND_FIGHTER_DEATH_CRY, pos);
        break;
    case FIGHT_EVENT_HOP:
        snd_PlayAt(sound_offset + SND_FIGHTER_IMPACT, pos);
        break;
    case FIGHT_EVENT_VANISH:
        snd_PlayAt(sound_offset + SND_FIGHTER_IMPACT, pos);
        break;
    case FIGHT_EVENT_RELOAD:
        snd_PlayAt(sound_offset + SND_FIGHTER_RELOAD, pos);
        break;
    case FIGHT_EVENT_EMPTY_GUN:
        snd_PlayAt(sound_offset + SND_FIGHTER_EMPTY_GUN, pos);
        break;
    }
}

/* ---- the fighter: what a motion plays ---- */

/* A motion's frame events, by number: a foot coming down, dust, and from 7
 * on a sound. Each is named for the sound it plays; a sound nobody has
 * listened to has its bank and its place there (WEAPON_6: the weapon
 * bank's seventh). A sound's number with FRAME_EVENT_OTHER added is the
 * same sound as the opponent's: a paired move's motion plays both
 * fighters' sounds.
 *
 * Where the motion files have them (their marks, docs/formats/BBM.md; not
 * heard): SHOT, RELOAD and EMPTY_GUN, named for what the guns' code plays
 * from the same places of the bank, are on the bladed weapons' attacks,
 * hundreds of each, and in neither gun's file: for a blade they are most
 * likely swings, as WEAPON_6 and WEAPON_7 beside them. WEAPON_12 falls
 * where a second sword or a sheathed one is drawn and WEAPON_14 where a
 * sheathed one goes back; PICK_UP at the end of stance changes and
 * attacks; the four voices on attacks. No motion has MODEL_3. */
enum FrameEvent {
    /* a foot comes down: a footprint on the beach, a splash in water */
    FRAME_EVENT_FOOT_0 = 0,
    FRAME_EVENT_FOOT_1 = 1,
    /* dust where the head, the chest or the hips meet the ground */
    FRAME_EVENT_DUST_HEAD = 2,
    FRAME_EVENT_DUST_CHEST = 3,
    FRAME_EVENT_DUST_HIP = 4,
    /* the fighter's weapon: chanbara's opponents share one set */
    FRAME_EVENT_SHOT = 7,
    FRAME_EVENT_RELOAD = 8,
    FRAME_EVENT_EMPTY_GUN = 9,
    FRAME_EVENT_WEAPON_6 = 10,
    FRAME_EVENT_WEAPON_7 = 11,
    FRAME_EVENT_PICK_UP = 12,
    FRAME_EVENT_WEAPON_12 = 13,
    FRAME_EVENT_SHEATHED_HIT = 14,
    FRAME_EVENT_WEAPON_12_AGAIN = 15, /* the same sound as 13 */
    FRAME_EVENT_WEAPON_14 = 16,
    FRAME_EVENT_WEAPON_LANDS = 17,
    /* its body and, from VOICE_0, its voice: chanbara's opponents have a
     * voice for each kind */
    FRAME_EVENT_IMPACT = 18,
    FRAME_EVENT_OBJECT_LANDS = 19,
    FRAME_EVENT_MODEL_3 = 20,
    FRAME_EVENT_VOICE_0 = 21,
    FRAME_EVENT_VOICE_1 = 22,
    FRAME_EVENT_VOICE_2 = 23,
    FRAME_EVENT_VOICE_3 = 24,
    FRAME_EVENT_GRUNT = 25,
    FRAME_EVENT_VOICE_5 = 26,
    /* a footstep: four for the kind of fighter, on each class of ground */
    FRAME_EVENT_FOOTSTEP_0 = 27,
    FRAME_EVENT_FOOTSTEP_1 = 28,
    FRAME_EVENT_FOOTSTEP_2 = 29,
    FRAME_EVENT_FOOTSTEP_3 = 30,
    /* the stage's: three for each class of ground, and two more */
    FRAME_EVENT_GROUND_0 = 31,
    FRAME_EVENT_GROUND_1 = 32,
    FRAME_EVENT_GROUND_2 = 33,
    FRAME_EVENT_STAGE_6 = 34,
    FRAME_EVENT_COLLIDE = 35,
    /* four more of the fighter's own */
    FRAME_EVENT_CLANG = 36,
    FRAME_EVENT_KAISHAKU_CUT = 37,
    FRAME_EVENT_DEATH_CRY = 38, /* the voice's seventh */
    FRAME_EVENT_MODEL_11 = 39,
    /* what is added to any of the sounds for the opponent's */
    FRAME_EVENT_OTHER = 33,
};

/* A foot this near the ground under it is down. */
#define FOOT_DOWN_HEIGHT 600

/* Plays one. A foot coming down leaves a footprint only on the beach of
 * Kimigaura Coast, and raises a splash only there and under the lost
 * bridge, in the wet. */
static void fighter_PlayFrameEvent(Fighter *fighter, s32 frame_event) {
    s32 pos[3];
    s32 sound_offset;
    s32 other_sound_offset;
    s32 ground_sound_offset;
    s32 other_ground_sound_offset;
    s32 ground_offset;
    s32 other_ground_offset;

    if (g_fight_mode == FIGHT_MODE_CHANBARA && fighter->index == 1 &&
        (frame_event == FRAME_EVENT_SHOT || frame_event == FRAME_EVENT_RELOAD ||
         frame_event == FRAME_EVENT_EMPTY_GUN ||
         frame_event == FRAME_EVENT_WEAPON_6 ||
         frame_event == FRAME_EVENT_WEAPON_7 ||
         frame_event == FRAME_EVENT_PICK_UP ||
         frame_event == FRAME_EVENT_WEAPON_12 ||
         frame_event == FRAME_EVENT_SHEATHED_HIT ||
         frame_event == FRAME_EVENT_WEAPON_12_AGAIN ||
         frame_event == FRAME_EVENT_WEAPON_14 ||
         frame_event == FRAME_EVENT_WEAPON_LANDS)) {
        other_sound_offset = 0;
        sound_offset = 0;
    } else {
        sound_offset = fighter->index * SND_FIGHTER_COUNT;
        other_sound_offset = fighter->other->index * SND_FIGHTER_COUNT;
    }

    if (g_fight_mode == FIGHT_MODE_CHANBARA &&
        (frame_event == FRAME_EVENT_VOICE_0 ||
         frame_event == FRAME_EVENT_VOICE_1 ||
         frame_event == FRAME_EVENT_VOICE_2 ||
         frame_event == FRAME_EVENT_VOICE_3 ||
         frame_event == FRAME_EVENT_GRUNT ||
         frame_event == FRAME_EVENT_VOICE_5 ||
         frame_event == FRAME_EVENT_DEATH_CRY ||
         frame_event == FRAME_EVENT_OTHER + FRAME_EVENT_VOICE_0 ||
         frame_event == FRAME_EVENT_OTHER + FRAME_EVENT_VOICE_1 ||
         frame_event == FRAME_EVENT_OTHER + FRAME_EVENT_VOICE_2 ||
         frame_event == FRAME_EVENT_OTHER + FRAME_EVENT_VOICE_3 ||
         frame_event == FRAME_EVENT_OTHER + FRAME_EVENT_GRUNT ||
         frame_event == FRAME_EVENT_OTHER + FRAME_EVENT_VOICE_5 ||
         frame_event == FRAME_EVENT_OTHER + FRAME_EVENT_DEATH_CRY)) {
        if (fighter->index == 1) {
            sound_offset += g_chanbara_voice_offsets[g_chanbara_kind];
        } else {
            other_sound_offset += g_chanbara_voice_offsets[g_chanbara_kind];
        }
    }

    /* four footsteps and three of the stage's sounds for each class of
     * ground */
    ground_sound_offset = sound_offset + fighter->surface_class * 4;
    ground_offset = fighter->surface_class * 3;
    other_ground_sound_offset =
        other_sound_offset + fighter->other->surface_class * 4;
    other_ground_offset = fighter->other->surface_class * 3;

    switch (frame_event) {
    case FRAME_EVENT_FOOT_0:
        if (g_fight_stage == STAGE_KIMIGAURA_COAST) {
            if (fighter->foot_ground_ys[0] - FOOT_DOWN_HEIGHT <
                fighter->foot_points[0].y) {
                pos[0] = fighter->foot_points[0].x;
                pos[1] = fighter->foot_ground_ys[0];
                pos[2] = fighter->foot_points[0].z;
                eff_AddFootprint(
                    pos, fighter->foot_angles[0].vy,
                    g_eff_footprint_shape_bits
                        [g_fighter_roster_classes[fighter->roster_id]],
                    fighter->index);
                if (SURFACE_IN(fighter->surface, SURFACES_WET)) {
                    fight_RaiseEvent(
                        fighter->index, FIGHT_EVENT_SPLASH, pos, NULL);
                }
            }
        } else if (g_fight_stage == STAGE_UNDER_LOST_BRIDGE) {
            if (SURFACE_IN(fighter->surface, SURFACES_WET) &&
                fighter->foot_ground_ys[0] - FOOT_DOWN_HEIGHT <
                    fighter->foot_points[0].y) {
                pos[0] = fighter->foot_points[0].x;
                pos[1] = fighter->foot_ground_ys[0];
                pos[2] = fighter->foot_points[0].z;
                fight_RaiseEvent(fighter->index, FIGHT_EVENT_SPLASH, pos, NULL);
            }
        }
        break;
    case FRAME_EVENT_FOOT_1:
        if (g_fight_stage == STAGE_KIMIGAURA_COAST) {
            if (fighter->foot_ground_ys[1] - FOOT_DOWN_HEIGHT <
                fighter->foot_points[1].y) {
                pos[0] = fighter->foot_points[1].x;
                pos[1] = fighter->foot_ground_ys[1];
                pos[2] = fighter->foot_points[1].z;
                eff_AddFootprint(
                    pos, fighter->foot_angles[1].vy,
                    g_eff_footprint_shape_bits
                        [g_fighter_roster_classes[fighter->roster_id]],
                    fighter->index);
                if (SURFACE_IN(fighter->surface, SURFACES_WET)) {
                    fight_RaiseEvent(
                        fighter->index, FIGHT_EVENT_SPLASH, pos, NULL);
                }
            }
        } else if (g_fight_stage == STAGE_UNDER_LOST_BRIDGE) {
            if (SURFACE_IN(fighter->surface, SURFACES_WET) &&
                fighter->foot_ground_ys[1] - FOOT_DOWN_HEIGHT <
                    fighter->foot_points[1].y) {
                pos[0] = fighter->foot_points[1].x;
                pos[1] = fighter->foot_ground_ys[1];
                pos[2] = fighter->foot_points[1].z;
                fight_RaiseEvent(fighter->index, FIGHT_EVENT_SPLASH, pos, NULL);
            }
        }
        break;
    case FRAME_EVENT_DUST_HEAD:
        fight_RaiseEvent(
            fighter->index, FIGHT_EVENT_DUST, &fighter->head_pos.x, NULL);
        break;
    case FRAME_EVENT_DUST_CHEST:
        fight_RaiseEvent(
            fighter->index, FIGHT_EVENT_DUST, &fighter->chest_pos.x, NULL);
        break;
    case FRAME_EVENT_DUST_HIP:
        fight_RaiseEvent(
            fighter->index, FIGHT_EVENT_DUST, &fighter->hip_pos.x, NULL);
        break;
    case FRAME_EVENT_SHOT:
        snd_PlayAt(sound_offset + SND_FIGHTER_SHOT, &fighter->world_pos.x);
        break;
    case FRAME_EVENT_RELOAD:
        snd_PlayAt(sound_offset + SND_FIGHTER_RELOAD, &fighter->world_pos.x);
        break;
    case FRAME_EVENT_EMPTY_GUN:
        snd_PlayAt(sound_offset + SND_FIGHTER_EMPTY_GUN, &fighter->world_pos.x);
        break;
    case FRAME_EVENT_WEAPON_6:
        snd_PlayAt(
            sound_offset + SND_FIGHTER_WEAPON_BANK + 6, &fighter->world_pos.x);
        break;
    case FRAME_EVENT_WEAPON_7:
        snd_PlayAt(
            sound_offset + SND_FIGHTER_WEAPON_BANK + 7, &fighter->world_pos.x);
        break;
    case FRAME_EVENT_PICK_UP:
        snd_PlayAt(sound_offset + SND_FIGHTER_PICK_UP, &fighter->world_pos.x);
        break;
    case FRAME_EVENT_WEAPON_12:
        snd_PlayAt(
            sound_offset + SND_FIGHTER_WEAPON_BANK + 12, &fighter->world_pos.x);
        break;
    case FRAME_EVENT_SHEATHED_HIT:
        snd_PlayAt(
            sound_offset + SND_FIGHTER_SHEATHED_HIT, &fighter->world_pos.x);
        break;
    case FRAME_EVENT_WEAPON_12_AGAIN:
        snd_PlayAt(
            sound_offset + SND_FIGHTER_WEAPON_BANK + 12, &fighter->world_pos.x);
        break;
    case FRAME_EVENT_WEAPON_14:
        snd_PlayAt(
            sound_offset + SND_FIGHTER_WEAPON_BANK + 14, &fighter->world_pos.x);
        break;
    case FRAME_EVENT_WEAPON_LANDS:
        snd_PlayAt(
            sound_offset + SND_FIGHTER_WEAPON_LANDS, &fighter->world_pos.x);
        break;
    case FRAME_EVENT_IMPACT:
        snd_PlayAt(sound_offset + SND_FIGHTER_IMPACT, &fighter->world_pos.x);
        break;
    case FRAME_EVENT_OBJECT_LANDS:
        snd_PlayAt(
            sound_offset + SND_FIGHTER_OBJECT_LANDS, &fighter->world_pos.x);
        break;
    case FRAME_EVENT_MODEL_3:
        snd_PlayAt(
            sound_offset + SND_FIGHTER_MODEL_BANK + 3, &fighter->world_pos.x);
        break;
    case FRAME_EVENT_VOICE_0:
        snd_PlayAt(sound_offset + SND_FIGHTER_VOICE + 0, &fighter->world_pos.x);
        break;
    case FRAME_EVENT_VOICE_1:
        snd_PlayAt(sound_offset + SND_FIGHTER_VOICE + 1, &fighter->world_pos.x);
        break;
    case FRAME_EVENT_VOICE_2:
        snd_PlayAt(sound_offset + SND_FIGHTER_VOICE + 2, &fighter->world_pos.x);
        break;
    case FRAME_EVENT_VOICE_3:
        snd_PlayAt(sound_offset + SND_FIGHTER_VOICE + 3, &fighter->world_pos.x);
        break;
    case FRAME_EVENT_GRUNT:
        snd_PlayAt(sound_offset + SND_FIGHTER_GRUNT, &fighter->world_pos.x);
        break;
    case FRAME_EVENT_VOICE_5:
        snd_PlayAt(sound_offset + SND_FIGHTER_VOICE + 5, &fighter->world_pos.x);
        break;
    case FRAME_EVENT_FOOTSTEP_0:
        snd_PlayAt(ground_sound_offset + SND_FIGHTER_FOOTSTEP + 0,
                   &fighter->world_pos.x);
        break;
    case FRAME_EVENT_FOOTSTEP_1:
        snd_PlayAt(ground_sound_offset + SND_FIGHTER_FOOTSTEP + 1,
                   &fighter->world_pos.x);
        break;
    case FRAME_EVENT_FOOTSTEP_2:
        snd_PlayAt(ground_sound_offset + SND_FIGHTER_FOOTSTEP + 2,
                   &fighter->world_pos.x);
        break;
    case FRAME_EVENT_FOOTSTEP_3:
        snd_PlayAt(ground_sound_offset + SND_FIGHTER_FOOTSTEP + 3,
                   &fighter->world_pos.x);
        break;
    case FRAME_EVENT_GROUND_0:
        snd_PlayAt(ground_offset + SND_STAGE_FIRST + 0, &fighter->world_pos.x);
        break;
    case FRAME_EVENT_GROUND_1:
        snd_PlayAt(ground_offset + SND_STAGE_FIRST + 1, &fighter->world_pos.x);
        break;
    case FRAME_EVENT_GROUND_2:
        snd_PlayAt(ground_offset + SND_STAGE_FIRST + 2, &fighter->world_pos.x);
        break;
    case FRAME_EVENT_STAGE_6:
        snd_PlayAt(SND_STAGE_FIRST + 6, &fighter->world_pos.x);
        break;
    case FRAME_EVENT_COLLIDE:
        snd_PlayAt(SND_FIGHT_COLLIDE, &fighter->world_pos.x);
        break;
    case FRAME_EVENT_CLANG:
        snd_PlayAt(sound_offset + SND_FIGHTER_CLANG, &fighter->world_pos.x);
        break;
    case FRAME_EVENT_KAISHAKU_CUT:
        snd_PlayAt(
            sound_offset + SND_FIGHTER_KAISHAKU_CUT, &fighter->world_pos.x);
        break;
    case FRAME_EVENT_DEATH_CRY:
        snd_PlayAt(sound_offset + SND_FIGHTER_DEATH_CRY, &fighter->world_pos.x);
        break;
    case FRAME_EVENT_MODEL_11:
        snd_PlayAt(
            sound_offset + SND_FIGHTER_MODEL_BANK + 11, &fighter->world_pos.x);
        break;
    case FRAME_EVENT_OTHER + FRAME_EVENT_SHOT:
        snd_PlayAt(other_sound_offset + SND_FIGHTER_SHOT,
                   &fighter->other->world_pos.x);
        break;
    case FRAME_EVENT_OTHER + FRAME_EVENT_RELOAD:
        snd_PlayAt(other_sound_offset + SND_FIGHTER_RELOAD,
                   &fighter->other->world_pos.x);
        break;
    case FRAME_EVENT_OTHER + FRAME_EVENT_EMPTY_GUN:
        snd_PlayAt(other_sound_offset + SND_FIGHTER_EMPTY_GUN,
                   &fighter->other->world_pos.x);
        break;
    case FRAME_EVENT_OTHER + FRAME_EVENT_WEAPON_6:
        snd_PlayAt(other_sound_offset + SND_FIGHTER_WEAPON_BANK + 6,
                   &fighter->other->world_pos.x);
        break;
    case FRAME_EVENT_OTHER + FRAME_EVENT_WEAPON_7:
        snd_PlayAt(other_sound_offset + SND_FIGHTER_WEAPON_BANK + 7,
                   &fighter->other->world_pos.x);
        break;
    case FRAME_EVENT_OTHER + FRAME_EVENT_PICK_UP:
        snd_PlayAt(other_sound_offset + SND_FIGHTER_PICK_UP,
                   &fighter->other->world_pos.x);
        break;
    case FRAME_EVENT_OTHER + FRAME_EVENT_WEAPON_12:
        snd_PlayAt(other_sound_offset + SND_FIGHTER_WEAPON_BANK + 12,
                   &fighter->other->world_pos.x);
        break;
    case FRAME_EVENT_OTHER + FRAME_EVENT_SHEATHED_HIT:
        snd_PlayAt(other_sound_offset + SND_FIGHTER_SHEATHED_HIT,
                   &fighter->other->world_pos.x);
        break;
    case FRAME_EVENT_OTHER + FRAME_EVENT_WEAPON_12_AGAIN:
        snd_PlayAt(other_sound_offset + SND_FIGHTER_WEAPON_BANK + 12,
                   &fighter->other->world_pos.x);
        break;
    case FRAME_EVENT_OTHER + FRAME_EVENT_WEAPON_14:
        snd_PlayAt(other_sound_offset + SND_FIGHTER_WEAPON_BANK + 14,
                   &fighter->other->world_pos.x);
        break;
    case FRAME_EVENT_OTHER + FRAME_EVENT_WEAPON_LANDS:
        snd_PlayAt(other_sound_offset + SND_FIGHTER_WEAPON_LANDS,
                   &fighter->other->world_pos.x);
        break;
    case FRAME_EVENT_OTHER + FRAME_EVENT_IMPACT:
        snd_PlayAt(other_sound_offset + SND_FIGHTER_IMPACT,
                   &fighter->other->world_pos.x);
        break;
    case FRAME_EVENT_OTHER + FRAME_EVENT_OBJECT_LANDS:
        snd_PlayAt(other_sound_offset + SND_FIGHTER_OBJECT_LANDS,
                   &fighter->other->world_pos.x);
        break;
    case FRAME_EVENT_OTHER + FRAME_EVENT_MODEL_3:
        snd_PlayAt(other_sound_offset + SND_FIGHTER_MODEL_BANK + 3,
                   &fighter->other->world_pos.x);
        break;
    case FRAME_EVENT_OTHER + FRAME_EVENT_VOICE_0:
        snd_PlayAt(other_sound_offset + SND_FIGHTER_VOICE + 0,
                   &fighter->other->world_pos.x);
        break;
    case FRAME_EVENT_OTHER + FRAME_EVENT_VOICE_1:
        snd_PlayAt(other_sound_offset + SND_FIGHTER_VOICE + 1,
                   &fighter->other->world_pos.x);
        break;
    case FRAME_EVENT_OTHER + FRAME_EVENT_VOICE_2:
        snd_PlayAt(other_sound_offset + SND_FIGHTER_VOICE + 2,
                   &fighter->other->world_pos.x);
        break;
    case FRAME_EVENT_OTHER + FRAME_EVENT_VOICE_3:
        snd_PlayAt(other_sound_offset + SND_FIGHTER_VOICE + 3,
                   &fighter->other->world_pos.x);
        break;
    case FRAME_EVENT_OTHER + FRAME_EVENT_GRUNT:
        snd_PlayAt(other_sound_offset + SND_FIGHTER_GRUNT,
                   &fighter->other->world_pos.x);
        break;
    case FRAME_EVENT_OTHER + FRAME_EVENT_VOICE_5:
        snd_PlayAt(other_sound_offset + SND_FIGHTER_VOICE + 5,
                   &fighter->other->world_pos.x);
        break;
    case FRAME_EVENT_OTHER + FRAME_EVENT_FOOTSTEP_0:
        snd_PlayAt(other_ground_sound_offset + SND_FIGHTER_FOOTSTEP + 0,
                   &fighter->other->world_pos.x);
        break;
    case FRAME_EVENT_OTHER + FRAME_EVENT_FOOTSTEP_1:
        snd_PlayAt(other_ground_sound_offset + SND_FIGHTER_FOOTSTEP + 1,
                   &fighter->other->world_pos.x);
        break;
    case FRAME_EVENT_OTHER + FRAME_EVENT_FOOTSTEP_2:
        snd_PlayAt(other_ground_sound_offset + SND_FIGHTER_FOOTSTEP + 2,
                   &fighter->other->world_pos.x);
        break;
    case FRAME_EVENT_OTHER + FRAME_EVENT_FOOTSTEP_3:
        snd_PlayAt(other_ground_sound_offset + SND_FIGHTER_FOOTSTEP + 3,
                   &fighter->other->world_pos.x);
        break;
    case FRAME_EVENT_OTHER + FRAME_EVENT_GROUND_0:
        snd_PlayAt(other_ground_offset + SND_STAGE_FIRST + 0,
                   &fighter->other->world_pos.x);
        break;
    case FRAME_EVENT_OTHER + FRAME_EVENT_GROUND_1:
        snd_PlayAt(other_ground_offset + SND_STAGE_FIRST + 1,
                   &fighter->other->world_pos.x);
        break;
    case FRAME_EVENT_OTHER + FRAME_EVENT_GROUND_2:
        snd_PlayAt(other_ground_offset + SND_STAGE_FIRST + 2,
                   &fighter->other->world_pos.x);
        break;
    case FRAME_EVENT_OTHER + FRAME_EVENT_STAGE_6:
        snd_PlayAt(SND_STAGE_FIRST + 6, &fighter->other->world_pos.x);
        break;
    case FRAME_EVENT_OTHER + FRAME_EVENT_COLLIDE:
        snd_PlayAt(SND_FIGHT_COLLIDE, &fighter->other->world_pos.x);
        break;
    case FRAME_EVENT_OTHER + FRAME_EVENT_CLANG:
        snd_PlayAt(other_sound_offset + SND_FIGHTER_CLANG,
                   &fighter->other->world_pos.x);
        break;
    case FRAME_EVENT_OTHER + FRAME_EVENT_KAISHAKU_CUT:
        snd_PlayAt(other_sound_offset + SND_FIGHTER_KAISHAKU_CUT,
                   &fighter->other->world_pos.x);
        break;
    case FRAME_EVENT_OTHER + FRAME_EVENT_DEATH_CRY:
        snd_PlayAt(other_sound_offset + SND_FIGHTER_DEATH_CRY,
                   &fighter->other->world_pos.x);
        break;
    case FRAME_EVENT_OTHER + FRAME_EVENT_MODEL_11:
        snd_PlayAt(other_sound_offset + SND_FIGHTER_MODEL_BANK + 11,
                   &fighter->other->world_pos.x);
        break;
    }
}

/* Plays what the move's motion has for the frame the fighter is at. The
 * frame events are among the motion's marks, in frame order. */
void fighter_RunFrameEvents(Fighter *fighter) {
    u8 *marks = fighter->motion_head + MOTION_HEAD_SIZE + 1;
    u8 mark = marks[-1];
    while (mark != 0) {
        if (mark == MOVE_MARK_FOR_SOME) {
            u32 characters = marks[0] | (marks[1] << 8) | (marks[2] << 16) |
                             (marks[3] << 24);
            if (characters & (1 << fighter->character)) {
                marks += MOVE_MARK_MASK_SIZE;
            } else {
                /* and the mark after it */
                marks += MOVE_MARK_MASK_SIZE + 2;
            }
        } else if (mark < MOVE_MARK_FIRST) {
            u8 event_frame = *marks++;
            s16 current_frame = fighter->move_frame;
            if (event_frame == current_frame) {
                fighter_PlayFrameEvent(fighter, mark - 1);
            } else if (current_frame < event_frame) {
                return;
            }
        } else {
            marks++;
        }
        mark = *marks++;
    }
}

/* ---- the second part: a stage's props and ambience (snd.h) ---- */

/* Where each falling prop's sounds are heard from. */
Vec3 g_snd_prop_sound_positions[SND_PROP_SLOTS];

/* Which set of prop sounds the stage uses (an index into
 * g_snd_prop_first_sounds and g_snd_prop_cue_ticks), for the six stages that
 * have props a weapon can cut down. */
static s32 snd_GetStagePropKind(void) {
    switch (g_fight_stage) {
    case STAGE_SHAINTO_HQ:
        return 0;
    case STAGE_BUS_STOP:
        return 1;
    case STAGE_BAMBOO_THICKET:
        return 2;
    case STAGE_OUTSIDE_MEIKYOKAN:
        return 3;
    case STAGE_LUMBERYARD:
        return 4;
    case STAGE_PURPLE_CAVERN:
        return 5; /* the last set */
    default:
        return 0xFF;
    }
}

void snd_ResetStageSounds(void) {
    s32 i;

    for (i = 0; i < SND_AMBIENCE_SLOTS; i++) {
        g_snd_ambient_timers[i] = 0;
    }
    for (i = 0; i < SND_PROP_SLOTS; i++) {
        g_snd_prop_sound_ticks[i] = 0;
    }
}

/* A prop at `position` has been cut and starts to fall: over the next frames
 * snd_UpdateStageSounds plays its three sounds from there. */
void snd_StartPropSounds(Vec3 *position) {
    s32 i;

    for (i = 0; i < SND_PROP_SLOTS; i++) {
        if (g_snd_prop_sound_ticks[i] == 0) {
            break;
        }
    }
    if (i == SND_PROP_SLOTS) {
        return;
    }
    g_snd_prop_sound_ticks[i] = 1;
    g_snd_prop_sound_positions[i] = *position;
}

/* Once a frame of the fight: carries on the sounds of falling props, and
 * plays the stage's ambient sounds. */
void snd_UpdateStageSounds(void) {
    s32 i;
#if RB_PORT
    /* the sounds a mod's arena is heard with (port_arenas.h) */
    u8 *ambience = g_snd_stage_ambience[port_ArenaSoundStage(g_fight_stage)];
#else
    u8 *ambience = g_snd_stage_ambience[g_fight_stage];
#endif

    for (i = 0; i < SND_PROP_SLOTS; i++) {
        if (g_snd_prop_sound_ticks[i] != 0) {
            s32 kind = snd_GetStagePropKind();
            u8 tick = g_snd_prop_sound_ticks[i];

            if (tick == 1) {
                snd_PlayAt(g_snd_prop_first_sounds[kind],
                           &g_snd_prop_sound_positions[i].x);
            } else if (tick == g_snd_prop_cue_ticks[kind].second) {
                snd_PlayAt(g_snd_prop_first_sounds[kind] + 1,
                           &g_snd_prop_sound_positions[i].x);
            } else if (tick == g_snd_prop_cue_ticks[kind].third) {
                snd_PlayAt(g_snd_prop_first_sounds[kind] + 2,
                           &g_snd_prop_sound_positions[i].x);
            }

            {
                s32 next = g_snd_prop_sound_ticks[i] + 1;
                g_snd_prop_sound_ticks[i] = next;
                if ((u32)g_snd_prop_cue_ticks[kind].third < (next & 0xFF)) {
                    g_snd_prop_sound_ticks[i] = 0;
                }
            }
        }
    }

    if (snd_AmbienceEnabled()) {
        s16 *timers = g_snd_ambient_timers;

        for (i = 0; i < SND_AMBIENCE_SLOTS; i++, ambience += 2) {
            u8 *volume = ambience + 1;
            s32 random_value;
            s32 kind;
            s32 roll;

            random_value = rng_Next();
            /* of 1024, against a chance of as many */
            roll = random_value & 0x3FF;
            kind = *ambience;

            switch (kind) {
            case AMBIENCE_NONE:
            default:
                break;
            case AMBIENCE_LOOP:
                if (timers[i] == 0) {
                    timers[i] = 1;
                    snd_Play(SND_STAGE_AMBIENT_FIRST + i, *volume, *volume);
                }
                break;
            case AMBIENCE_RANDOM_FIRST:
            case AMBIENCE_RANDOM_FIRST + 1:
            case AMBIENCE_RANDOM_FIRST + 2:
            case AMBIENCE_RANDOM_PLACED:
            case AMBIENCE_RANDOM_LAST: {
                s32 timing = kind - AMBIENCE_RANDOM_FIRST;
                s16 count;
                s32 frames;
                s32 quiet_seconds;
                s32 quiet_frames;

                count = timers[i] + 1;
                timers[i] = count;
                frames = count;
                quiet_seconds = g_snd_ambient_timings[timing].quiet_seconds;
                quiet_frames = quiet_seconds * FRAMES_PER_SECOND;

                if (quiet_frames < frames) {
                    s32 chance = ((frames - quiet_frames) << 10) /
                                 (g_snd_ambient_timings[timing].ramp_seconds *
                                  FRAMES_PER_SECOND);

                    if (roll < chance) {
                        timers[i] = 0;
                        kind = *ambience;
                        if ((kind >= AMBIENCE_RANDOM_PLACED &&
                             kind <= AMBIENCE_RANDOM_PLACED + 1)) {
                            snd_PlayAt(SND_STAGE_AMBIENT_FIRST + i,
                                       g_snd_ambient_positions
                                           [kind - AMBIENCE_RANDOM_PLACED]);
                            continue;
                        }
                        snd_Play(SND_STAGE_AMBIENT_FIRST + i, *volume, *volume);
                    }
                }
                break;
            }
            }
        }
    }
}
