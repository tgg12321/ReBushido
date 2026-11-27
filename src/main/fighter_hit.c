/* The two fighters against each other (fighter.h): what a hit does to a
 * fighter, blade against blade, blade and bullet against body, keeping the
 * bodies apart, and a paired move's frame. The geometry the tests use is
 * hit_test.c. Loose objects, which followed in the original file, are
 * object.c; a move's marks and sounds and the fight's events,
 * fight_event.c. */
#define INCLUDE_ASM_USE_MACRO_INC 1
#include "common.h"
#include "include_asm.h"
#include <psxsdk/inline_o.h>
#include <psxsdk/gtemac.h>
#include "bb2.h"

/* Defined further down. */
static s32 g_fight_body_radii[2];
static void fighter_TestBodyInSweep(
    Fighter *fighter, s32 frame_from_normal, s32 test_cores);

/* How far apart two fighters' powers are (a row of g_fight_clash_events). */
enum PowerGap {
    POWER_GAP_LARGE = 0,
    POWER_GAP_SMALL = 1,
    POWER_GAP_MEDIUM = 2,
};

/* The parts of the body story mode shows wounds on (g_fight_story_wounds). */
enum StoryWound {
    STORY_WOUND_OFF_ARM = 0x01,
    STORY_WOUND_SWORD_ARM = 0x02,
    STORY_WOUND_LEG_1 = 0x04,
    STORY_WOUND_LEG_2 = 0x08,
    STORY_WOUND_BODY = 0x10,
};

/* The gaps in power at which one class of gap ends: a small one is below
 * the first, a large one above the second. */
#define POWER_GAP_SMALL_END 0x600
#define POWER_GAP_MEDIUM_MAX 0xA00

/* The chest's three body points, a bit each. */
#define BODY_CHEST_BITS 0xE

/* A fighter beaten back in a clash, or struck by what it cannot beat aside,
 * is pushed along the blow by this part of it. */
#define BLOW_PUSH_PART 16

/* The vanisher comes back this many percent of the way to the far corner
 * the first time, so much less each time after, and never less than the
 * least; its vanishings are counted as far as the limit. */
#define VANISH_FIRST_PERCENT 60
#define VANISH_PERCENT_STEP 4
#define VANISH_LEAST_PERCENT 10
#define VANISH_COUNT_MAX 64
/* The corners of the shrine's stage that it makes for: across, one for an
 * opponent on each side of the middle; along, either way. */
#define SHRINE_CORNER_X 12000
#define SHRINE_CORNER_X_BACK (-10000)
#define SHRINE_MIDDLE_X 1000
#define SHRINE_CORNER_Z 6000

/* Bodies: the deepest overlap that is pushed apart in one frame, and what
 * an even share of the push is (the stronger fighter gets less: a quarter
 * of the gap in power less). */
#define PUSH_DEPTH_MAX 128
#define PUSH_SHARE_ONE 0x400
#define PUSH_SHARE_SHIFT 10
/* A runner at more than this speed knocks into the other. */
#define RUN_COLLIDE_SPEED 2500
/* How far apart two headings are when the fighters face each other (more
 * than three eighths of a turn) and when one has its back to the other
 * (less than a quarter). */
#define HEAD_ON_ANGLE (3 * ANGLE_TURN / 8)
#define FROM_BEHIND_ANGLE (ANGLE_TURN / 4)

/* ---- what a hit does ---- */

/* Which class a gap in power is. */
static s32 fight_ClassPowerGap(s32 gap) {
    s32 gap_class;
    if (gap < 0) {
        gap = -gap;
    }
    if (gap <= POWER_GAP_MEDIUM_MAX) {
        if (gap < POWER_GAP_SMALL_END) {
            gap_class = POWER_GAP_SMALL;
        } else {
            gap_class = POWER_GAP_MEDIUM;
        }
    } else {
        gap_class = POWER_GAP_LARGE;
    }
    return gap_class;
}

/* Poses an arm that can no longer be used (motion.h): its four bones get
 * the angles of an arm hanging at the side, three each, as a motion has
 * them. */
void motion_HangArm(MotionFrame *frame) {
    frame->bone_angles[MOTION_ARM + 0] = 0x3F5;
    frame->bone_angles[MOTION_ARM + 1] = 0x2B6;
    frame->bone_angles[MOTION_ARM + 2] = 0x77A;
    frame->bone_angles[MOTION_ARM + 3] = 0xBEE;
    frame->bone_angles[MOTION_ARM + 4] = 0x8C;
    frame->bone_angles[MOTION_ARM + 5] = 0x227;
    frame->bone_angles[MOTION_ARM + 9] = 0x8A0;
    frame->bone_angles[MOTION_ARM + 10] = 0xF0E;
    frame->bone_angles[MOTION_ARM + 6] = 0;
    frame->bone_angles[MOTION_ARM + 7] = 0;
    frame->bone_angles[MOTION_ARM + 8] = 0;
    frame->bone_angles[MOTION_ARM + 11] = 0xBCD;
}

/* Story mode shows the player's wounds on its model from one fight to the
 * next, as extra textures (model_RequestTextures): a bit for each part of
 * the body. The second player's byte is reached from the first's (see
 * fight.h). */
void fighter_MarkStoryWound(s32 slot, s32 body_point) {
    if (g_fight_mode != FIGHT_MODE_STORY) {
        return;
    }
    switch (body_point) {
    case BODY_HEAD:
    case BODY_CHEST:
    case BODY_CHEST + 1:
    case BODY_CHEST + 2:
    case BODY_HIP:
    case BODY_HIP + 1:
        *(&g_fight_story_wounds + slot) |= STORY_WOUND_BODY;
        break;
    case BODY_OFF_ARM:
    case BODY_OFF_ARM + 1:
    case BODY_OFF_ARM + 2:
    case BODY_OFF_ARM + 3:
        *(&g_fight_story_wounds + slot) |= STORY_WOUND_OFF_ARM;
        break;
    case BODY_SWORD_ARM:
    case BODY_SWORD_ARM + 1:
    case BODY_SWORD_ARM + 2:
    case BODY_SWORD_ARM + 3:
        *(&g_fight_story_wounds + slot) |= STORY_WOUND_SWORD_ARM;
        break;
    case BODY_LEGS:
    case BODY_LEGS + 1:
    case BODY_LEGS + 2:
    case BODY_LEGS + 3:
        *(&g_fight_story_wounds + slot) |= STORY_WOUND_LEG_1;
        break;
    case BODY_SECOND_LEG:
    case BODY_SECOND_LEG + 1:
    case BODY_SECOND_LEG + 2:
    case BODY_SECOND_LEG + 3:
        *(&g_fight_story_wounds + slot) |= STORY_WOUND_LEG_2;
        break;
    }
}

/* A wound to a fighter on the ground, or from the thing that hops, by
 * where it landed: the body, the off arm (which is lost), the sword arm or
 * a leg. Not in team battles. */
static void fighter_Wound(Fighter *fighter, s32 body_point, s16 increment) {
    s16 fight_mode;
    fight_mode = g_fight_mode;
    if (fight_mode == FIGHT_MODE_TEAM) {
        return;
    }
    if (body_point < 0 || body_point >= BODY_POINT_COUNT) {
        return;
    }
    switch (body_point) {
    case BODY_HEAD:
    case BODY_CHEST:
    case BODY_CHEST + 1:
    case BODY_CHEST + 2:
    case BODY_HIP:
    case BODY_HIP + 1:
        fighter->body_wounds += increment;
        break;
    case BODY_OFF_ARM:
    case BODY_OFF_ARM + 1:
    case BODY_OFF_ARM + 2:
    case BODY_OFF_ARM + 3:
        fighter->off_arm_ok = 0;
        fighter->sheathed_ready = 0;
        fighter->two_sword_ready = 0;
        break;
    case BODY_SWORD_ARM:
    case BODY_SWORD_ARM + 1:
    case BODY_SWORD_ARM + 2:
    case BODY_SWORD_ARM + 3:
        fighter->arm_wounds += increment;
        break;
    case BODY_LEGS:
    case BODY_LEGS + 1:
    case BODY_LEGS + 2:
    case BODY_LEGS + 3:
    case BODY_SECOND_LEG:
    case BODY_SECOND_LEG + 1:
    case BODY_SECOND_LEG + 2:
    case BODY_SECOND_LEG + 3:
        fighter->leg_wounds += increment;
        break;
    }
}

/* The unit vector that points the other way. */
void math_ReverseUnit(s32 *vector, s16 *unit_out) {
    u32 dist_sq = (vector[0] * vector[0]) + (vector[1] * vector[1]) +
                  (vector[2] * vector[2]);
    s32 vector_length;
    /* MATH_SQRT, written out */
    if (dist_sq < MATH_SQRT_TABLE_SIZE) {
        vector_length = g_math_sqrt8_table[dist_sq] >> 3;
    } else {
        s32 leading_zeros;
        gte_Lzc(dist_sq, &leading_zeros);
        {
            u32 even_zeros = -2;
            u32 shift;
            u32 sqrt_table_index;
            u32 root8;
            even_zeros &= leading_zeros;
            shift = MATH_SQRT_SHIFT - even_zeros;
            sqrt_table_index = dist_sq >> shift;
            shift = shift >> 1;
            root8 = g_math_sqrt8_table[sqrt_table_index];
            vector_length = (root8 << 16) >> (MATH_SQRT_UNSHIFT - shift);
        }
    }
    unit_out[0] = ((-vector[0]) << 12) / vector_length;
    unit_out[1] = ((-vector[1]) << 12) / vector_length;
    unit_out[2] = ((-vector[2]) << 12) / vector_length;
}

/* The vanisher has been struck: it is gone, and comes back at its start
 * position (on the shrine's stage, part of the way to the corner farthest
 * from the opponent: 60% of the way the first time, less each time, never
 * under 10%). Off the shrine's stage the start position is read with half
 * the table's stride (see below). */
static void fighter_Vanish(Fighter *fighter) {
    VECTOR shift;
    VECTOR target_position;
    s32 fighter_index;
    s16 *start_position_cursor;
    s32 percent;
    u8 vanish_count;
    u16 *event_table;
    void *move;
    s32 vx;
    s32 vz;

    fighter_index = fighter->index;
    start_position_cursor = fight_GetStartPositions();
    vanish_count = fighter->vanish_count;
    if (vanish_count < VANISH_COUNT_MAX) {
        fighter->vanish_count = vanish_count + 1;
    }
    percent = VANISH_FIRST_PERCENT -
              ((fighter->vanish_count - 1) * VANISH_PERCENT_STEP);
    if (percent < VANISH_LEAST_PERCENT) {
        percent = VANISH_LEAST_PERCENT;
    }
    if (g_fight_stage == STAGE_TATARA_SHRINE) {
        vx = SHRINE_CORNER_X;
        if (fighter->other->world_pos.x > SHRINE_MIDDLE_X) {
            vx = SHRINE_CORNER_X_BACK;
        }
        target_position.vx = vx;
        vz = SHRINE_CORNER_Z;
        if (fighter->other->world_pos.z > 0) {
            vz = -SHRINE_CORNER_Z;
        }
        target_position.vz = vz;
        /* so many percent of the way from where it is to the corner */
        shift.vx = (target_position.vx * percent +
                    fighter->world_pos.x * (100 - percent)) /
                   100;
        /* and along */
        shift.vz = (target_position.vz * percent +
                    fighter->world_pos.z * (100 - percent)) /
                   100;
    } else {
        /* The stage's first pair of start positions, this fighter's
         * point. A stage has 24 halfwords in the table and this steps 12
         * a stage, so past the first stage it is another stage's place:
         * the original's behaviour. */
#if RB_FIX_VANISH_PLACE
        if (g_rb_settings.bugfixes) {
            /* this stage's place: 24 halfwords a stage */
            start_position_cursor += g_fight_stage * FIGHT_START_STAGE_SIZE +
                                     fighter_index * FIGHT_START_POINT_SIZE;
        } else
#endif
        {
            /* the original's: 12 halfwords a stage */
            start_position_cursor += g_fight_stage * 12 + fighter_index * 3;
        }
        shift.vx = start_position_cursor[0];
        shift.vz = start_position_cursor[2];
    }
    shift.vx -= fighter->world_pos.x;
    shift.vz -= fighter->world_pos.z;
    fighter->world_pos.x += shift.vx;
    fighter->world_pos.z += shift.vz;
    fighter->base_pos.x += shift.vx;
    fighter->base_pos.z += shift.vz;
    fighter->foot_pos.vx += shift.vx;
    fighter->foot_pos.vz += shift.vz;
    fighter->velocity.vx = 0;
    fighter->velocity.vy = 0;
    fighter->velocity.vz = 0;
    fighter->push.vx = 0;
    fighter->push.vy = 0;
    fighter->push.vz = 0;
    event_table = motion_GetMove(
        fighter, fighter->cur_move->event_table_id, &fighter->move_is_common);
    move = motion_GetMove(
        fighter, event_table[MOVE_EVENT_REAPPEAR], &fighter->move_is_common);
    fighter_StartMove(fighter_index, move, fighter->move_is_common);
    fight_RaiseEvent(
        fighter->index, FIGHT_EVENT_VANISH, &fighter->world_pos.x, NULL);
}

/* The sound of a blade or a bullet going in. They are the attacker's
 * sounds: by the weapon (a sheathed blade, a second sword, a sidearm's
 * bullet) or by where it landed. A thrown object has made its own. */
static void fighter_PlayHitSound(
    s32 by_object, Fighter *fighter, s32 body_point, LooseObjectKind *off_hand,
    s32 *hit_point, s32 by_sidearm) {
    Fighter *other;

    if (by_object == 1) {
        return;
    }

    if (by_sidearm) {
        fight_RaiseEvent(
            fighter->other->index, FIGHT_EVENT_OBJECT_HITS, hit_point, NULL);
        return;
    }

    other = fighter->other;
    if (other->stance == other->sheathed_stance) {
        fight_RaiseEvent(
            other->index, FIGHT_EVENT_SHEATHED_HIT, hit_point, NULL);
        return;
    }
    if (other->stance == other->two_sword_stance && off_hand) {
        fight_RaiseEvent(
            other->index, FIGHT_EVENT_SECOND_SWORD_CUT, hit_point, NULL);
        return;
    }

    if (body_point == BODY_HEAD) {
        fight_RaiseEvent(
            fighter->other->index, FIGHT_EVENT_HEAD_CUT, hit_point, NULL);
        return;
    }

    if (body_point < BODY_OFF_ARM) {
        fight_RaiseEvent(
            fighter->other->index, FIGHT_EVENT_BODY_CUT, hit_point, NULL);
        return;
    }

    fight_RaiseEvent(
        fighter->other->index, FIGHT_EVENT_LIMB_CUT, hit_point, NULL);
}

/* Whether the second sword is out in the off hand: between the frames the
 * move's marks give (most moves: throughout), but not while the fighter is
 * down, changing stance, beaten or posing; in a blade lock always. */
s32 fighter_SecondSwordIsOut(Fighter *fighter) {
    s32 result = 0;

    if (fighter->move_kind == MOVE_KIND_DOWN ||
        fighter->move_kind == MOVE_KIND_STANCE_CHANGE ||
        fighter->move_kind == MOVE_KIND_WIN ||
        fighter->move_kind == MOVE_KIND_END_GAME ||
        fighter->move_kind == MOVE_KIND_DTH ||
        fighter->move_frame < fighter->off_hand_start ||
        fighter->off_hand_end < fighter->move_frame) {
        if (fighter->two_sword_stance != -1) {
            u16 kind = fighter->move_kind;
            if (MOVE_IS_LOCK(kind) || kind == MOVE_KIND_LOCK_EXIT) {
                result = 1;
            }
        }
    } else {
        result = 1;
    }
    return result;
}

/* A fighter knocked about with its second sword out lets go of it. An int
 * function that returns nothing: the blade lock's code is that of a call
 * to one (fighter.h). */
s32 fighter_TryDropSecondSword(Fighter *fighter) {
    if (fighter->stance == fighter->two_sword_stance) {
        if (fighter->two_sword_ready) {
            if (fighter_SecondSwordIsOut(fighter)) {
                MatrixSlot *bone_matrices =
                    model_GetBoneMatrices(fighter->index);
                object_DropSubWeapon(
                    fighter,
                    (Vec3 *)PTR(bone_matrices[FIGHTER_BONE_OFF_HAND])->t);
                fighter->two_sword_ready = 0;
                fighter->stance = fighter->default_stance;
            }
        }
    }
}

/* A blow to the head or body that did not kill: a wound, and a stagger. */
static inline s32 fighter_TakeBodyWound(
    s32 by_object, LooseObjectKind *object_kind, Fighter *fighter,
    s32 fighter_index, s32 *hit_point) {
    if (by_object == 1 && object_kind->harm == OBJECT_HARM_DOUBLE) {
        fighter->body_wounds++;
    }
    if (g_fight_mode != FIGHT_MODE_TEAM) {
        fighter->body_wounds++;
    }
    fighter->pending_event = MOVE_EVENT_STAGGER;
    fight_RaiseEvent(fighter_index, FIGHT_EVENT_HIT, hit_point, NULL);
    fighter_TryDropSecondSword(fighter);
    return 0;
}

/* What a hit does to a fighter (fighter.h).
 *
 * The armoured fighter and the vanisher answer in their own way, and a
 * fighter that is already beaten, or the victim, only bleeds. A fighter hit
 * in the first frames of its own attack has not been hit at all: the two
 * attacks clash, and how it comes out of the clash depends on which of the
 * two is the stronger, by how much, and whether the attacks were of the
 * same kind. A fighter on the ground dies of a blow that reaches a core
 * and is wounded by any other.
 *
 * Otherwise it is where the blow landed. One that reaches the core of the
 * head, the chest or the hips kills, and the fighter falls by where it was
 * hit and whether the blow came from in front (`facing` over a quarter of
 * a turn). One that does not, wounds the body; a blow to a limb wounds the
 * limb. Team battles count no wounds.
 *
 * *clashed_out is cleared before anything else, and the blades' test
 * passes no place for it (fight_TestBladesOnBodies): the original's
 * behaviour, a word written at address 0. */
s32 fighter_TakeHit(
    s32 by_object, Fighter *fighter, s32 body_point, s32 facing, s32 kills,
    LooseObjectKind *object_kind, s32 by_sidearm, s32 *clashed_out) {
    /* object_kind, which the function keeps in two variables */
    LooseObjectKind *kind;
    LooseObjectKind *kind_again;
    s16 *blow_dir;
    s32 *hit_point;
    s32 fighter_index;
    Fighter *other;
    s32 from_above;
    s32 not_from_above;
    u16 move_kind;
    s32 gap;
    s32 gap_class;
    u32 weaker;
    s32 same_kind;
    s32 outcome;

    /* x, y and z are three variables, taken here as one vector (fight.h) */
    blow_dir = &g_fight_blow_dir_x;
    fighter_index = fighter->index;
    other = fighter->other;
    kind = object_kind;
    kind_again = object_kind;
    hit_point = &SPAD->body_points[fighter_index][body_point].x;
    *clashed_out = 0;
    if (fighter->roster_id == ROSTER_ARMORED) {
        from_above = (fighter->torso_axis.x * g_fight_blow_dir_x +
                      fighter->torso_axis.y * g_fight_blow_dir_y +
                      fighter->torso_axis.z * g_fight_blow_dir_z) >>
                     12; /* how far the blow runs down the torso, of ONE */
        /* kept to thirteen bits and their sign, as the original has it */
        from_above &= 0x1FFF;
        if (from_above >= ONE) {
            from_above -= 2 * ONE;
        }
        not_from_above = from_above < ONE / 4;
        /* armour: only the chest, and only from above */
        if (!((BODY_CHEST_BITS >> body_point) & 1) || not_from_above) {
            if (!fighter->is_defeated) {
                fighter->pending_event = MOVE_EVENT_ARMOR_HIT;
            }
            if (by_object == 0 && other->pending_event == MOVE_EVENT_NONE) {
                other->pending_event =
                    kind == NULL ? MOVE_EVENT_BLADE_STOPPED
                                 : MOVE_EVENT_BLADE_STOPPED_TWO;
            }
            fight_RaiseEvent(
                fighter_index, FIGHT_EVENT_ARMOR_SPARK, hit_point, NULL);
            return 0;
        }
    }
    if (fighter->roster_id == ROSTER_VANISHER && !fighter->is_defeated &&
        fighter->move_kind != MOVE_KIND_APPEAR) {
        fighter_Vanish(fighter);
        return 2;
    }
    move_kind = fighter->move_kind;
    if (move_kind == MOVE_KIND_END_GAME || move_kind == MOVE_KIND_DTH) {
        fighter_StartBleeding(fighter, body_point, blow_dir);
        fighter_PlayHitSound(
            by_object, fighter, body_point, kind_again, hit_point, by_sidearm);
        return 1;
    }
    /* the victim dies of any blow */
    if (fighter->roster_id == ROSTER_VICTIM) {
        fighter_StartBleeding(fighter, body_point, blow_dir);
        fighter_PlayHitSound(
            by_object, fighter, body_point, kind_again, hit_point, by_sidearm);
        fighter->pending_event = MOVE_EVENT_KILLED_CHEST;
        return 1;
    }
    if ((MOVE_IS_ATTACK(move_kind)) &&
        fighter->move_frame < fighter->clash_frames) {
        if (by_object == 0) {
            gap = other->power - fighter->power;
            gap_class = fight_ClassPowerGap(gap);
            weaker = gap < 0;
            same_kind = fighter->attack_kind == other->attack_kind;
            fighter->attack_kinds_matched = other->attack_kinds_matched =
                same_kind;
            fight_RaiseEvent(
                fighter_index,
                same_kind ? FIGHT_EVENT_MATCHED_CLASH : FIGHT_EVENT_CLASH,
                hit_point, NULL);
            outcome = fighter->pending_event =
                g_fight_clash_events[gap_class][weaker][same_kind];
            if (outcome == MOVE_EVENT_CLASH_EVEN) {
                fight_RaiseEvent(
                    fighter_index == 0, FIGHT_EVENT_CLANG, hit_point, NULL);
                fight_RaiseEvent(
                    fighter_index, FIGHT_EVENT_CLANG, hit_point, NULL);
            } else if (outcome == MOVE_EVENT_CLASH_PUSHED) {
                fight_RaiseEvent(fighter_index == 0, FIGHT_EVENT_HARD_CLANG,
                                 hit_point, NULL);
                fight_RaiseEvent(
                    fighter_index, FIGHT_EVENT_CLANG, hit_point, NULL);
                fight_RaiseEvent(
                    fighter_index, FIGHT_EVENT_GRUNT, hit_point, NULL);
            } else if (outcome == MOVE_EVENT_CLASH_BEATEN) {
                fight_RaiseEvent(fighter_index == 0, FIGHT_EVENT_HARD_CLANG,
                                 hit_point, NULL);
                fight_RaiseEvent(
                    fighter_index, FIGHT_EVENT_HARD_CLANG, hit_point, NULL);
                fight_RaiseEvent(
                    fighter_index, FIGHT_EVENT_GRUNT, hit_point, NULL);
                fighter_TryDropSecondSword(fighter);
            } else {
                return 0;
            }
            if (fighter->pending_event != MOVE_EVENT_CLASH_BEATEN) {
                return 0;
            }
            if (blow_dir[1] * blow_dir[1] <
                blow_dir[0] * blow_dir[0] + blow_dir[2] * blow_dir[2]) {
                fighter_TurnTo(fighter, ratan2(blow_dir[0], blow_dir[2]));
                fighter->push.vx -= blow_dir[0] / BLOW_PUSH_PART;
                fighter->push.vz -= blow_dir[2] / BLOW_PUSH_PART;
            }
            return 0;
        }
        if (by_object == 1) {
            if (kind->clash_outcome == OBJECT_CLASH_ASIDE) {
                fight_RaiseEvent(
                    fighter_index, FIGHT_EVENT_CLASH, hit_point, NULL);
                fight_RaiseEvent(
                    fighter_index, FIGHT_EVENT_CLANG, hit_point, NULL);
                fighter->pending_event = MOVE_EVENT_CLASH_EVEN;
                *clashed_out = by_object;
                return 0;
            }
            if (kind->clash_outcome == OBJECT_CLASH_PUSH) {
                fight_RaiseEvent(
                    fighter_index, FIGHT_EVENT_CLASH, hit_point, NULL);
                fight_RaiseEvent(
                    fighter_index, FIGHT_EVENT_CLANG, hit_point, NULL);
                fighter->pending_event = MOVE_EVENT_CLASH_PUSHED;
                fighter_TurnTo(fighter, ratan2(blow_dir[0], blow_dir[2]));
                fighter->push.vx -= blow_dir[0] / BLOW_PUSH_PART;
                fighter->push.vz -= blow_dir[2] / BLOW_PUSH_PART;
                *clashed_out = by_object;
                return 0;
            }
        }
    }
    if (g_fight_mode == FIGHT_MODE_STORY && fighter_index == 0) {
        fighter_MarkStoryWound(0, body_point);
    }
    fighter_PlayHitSound(
        by_object, fighter, body_point, kind_again, hit_point, by_sidearm);
    move_kind = fighter->move_kind;
    if (move_kind == MOVE_KIND_DOWN || move_kind == MOVE_KIND_GET_UP) {
        /* on the ground the event goes by which way up it lies, not by
         * where the blow landed */
        if (kills) {
            fighter->pending_event =
                fighter->lying_face_up ? MOVE_EVENT_KILLED_HEAD
                                       : MOVE_EVENT_KILLED_CHEST;
            fighter_StartBleeding(fighter, body_point, blow_dir);
            return 1;
        }
        fighter->pending_event = fighter->lying_face_up ? MOVE_EVENT_LEG_WOUND
                                                        : MOVE_EVENT_ARM_WOUND;
        fight_RaiseEvent(fighter_index, FIGHT_EVENT_HIT, hit_point, NULL);
        fighter_Wound(fighter, body_point, 1);
        return 0;
    }
    if (by_object == 1 && kind->flight == OBJECT_HOPS) {
        s16 character = fighter->character;
        /* the four characters that take the hopping thing their own way */
        if (character == CHARACTER_MIKADO ||
            character == CHARACTER_RED_SHADOW || character == CHARACTER_JO ||
            character == CHARACTER_HONGOU) {
            fighter_Wound(
                fighter, body_point, kind->harm == OBJECT_HARM_DOUBLE ? 2 : 1);
            fighter->pending_event = MOVE_EVENT_BITTEN;
            fight_RaiseEvent(fighter_index, FIGHT_EVENT_HIT, hit_point, NULL);
            return 0;
        }
    }
    switch (body_point) {
    case BODY_HEAD:
        if (kills) {
            fighter->pending_event =
                facing > ANGLE_TURN / 4 ? MOVE_EVENT_KILLED_HEAD
                                        : MOVE_EVENT_KILLED_FROM_BEHIND;
            fighter_StartBleeding(fighter, body_point, blow_dir);
            fighter_TryDropSecondSword(fighter);
            return 1;
        }
        return fighter_TakeBodyWound(
            by_object, kind, fighter, fighter_index, hit_point);
    case BODY_CHEST:
    case BODY_CHEST + 1:
    case BODY_CHEST + 2:
        if (kills) {
            fighter->pending_event =
                facing > ANGLE_TURN / 4 ? MOVE_EVENT_KILLED_CHEST
                                        : MOVE_EVENT_KILLED_FROM_BEHIND;
            fighter_StartBleeding(fighter, body_point, blow_dir);
            fighter_TryDropSecondSword(fighter);
            return 1;
        }
        return fighter_TakeBodyWound(
            by_object, kind, fighter, fighter_index, hit_point);
    case BODY_HIP:
    case BODY_HIP + 1:
        if (!kills) {
            return fighter_TakeBodyWound(
                by_object, kind, fighter, fighter_index, hit_point);
        }
        fighter->pending_event =
            facing > ANGLE_TURN / 4 ? MOVE_EVENT_KILLED_HIP
                                    : MOVE_EVENT_KILLED_FROM_BEHIND;
        fighter_StartBleeding(fighter, body_point, blow_dir);
        fighter_TryDropSecondSword(fighter);
        return 1;
    case BODY_OFF_ARM:
    case BODY_OFF_ARM + 1:
    case BODY_OFF_ARM + 2:
    case BODY_OFF_ARM + 3:
        /* the off arm: lost */
        if (g_fight_mode != FIGHT_MODE_TEAM) {
            fighter->off_arm_ok = 0;
        }
        fighter->pending_event = MOVE_EVENT_ARM_WOUND;
        fighter->sheathed_ready = 0;
        fight_RaiseEvent(fighter_index, FIGHT_EVENT_HIT, hit_point, NULL);
        fighter_TryDropSecondSword(fighter);
        fighter->two_sword_ready = 0;
        return 0;
    case BODY_SWORD_ARM:
    case BODY_SWORD_ARM + 1:
    case BODY_SWORD_ARM + 2:
    case BODY_SWORD_ARM + 3:
        /* the sword arm */
        if (g_fight_mode != FIGHT_MODE_TEAM) {
            fighter->arm_wounds++;
        }
        fighter->pending_event = MOVE_EVENT_STAGGER;
        fight_RaiseEvent(fighter_index, FIGHT_EVENT_HIT, hit_point, NULL);
        fighter_TryDropSecondSword(fighter);
        return 0;
    case BODY_LEGS:
    case BODY_LEGS + 1:
    case BODY_LEGS + 2:
    case BODY_LEGS + 3:
    case BODY_SECOND_LEG:
    case BODY_SECOND_LEG + 1:
    case BODY_SECOND_LEG + 2:
    case BODY_SECOND_LEG + 3:
        /* the legs */
        if (g_fight_mode != FIGHT_MODE_TEAM) {
            fighter->leg_wounds++;
        }
        fighter->pending_event = MOVE_EVENT_LEG_WOUND;
        fight_RaiseEvent(fighter_index, FIGHT_EVENT_HIT, hit_point, NULL);
        fighter_TryDropSecondSword(fighter);
        return 0;
    }
}

/* ---- blade against blade ---- */

/* The fighter's blade has met the other's (fight_TestBlades found where).
 * A fighter that is not attacking does nothing to the other's blade. A
 * blade that meets one not in an attack is stopped. Of two attacks the one
 * that has got further wins (enum AttackPhase): the loser's weapon is
 * beaten aside, the way the winner's was moving; two that are level are
 * both stopped, and two that are both striking lock blades, unless one is
 * a gun. Always returns 1. */
static s32 fighter_ResolveBladeContact(Fighter *fighter, s32 *contact_point) {
    Fighter *other;
    u16 kind;
    s32 two_handed;
    s16 move_event;
    s32 result;

    other = fighter->other;
    result = 1;
    kind = fighter->move_kind;
    if (kind == MOVE_KIND_END_GAME) {
        return 1;
    }
    if (kind == MOVE_KIND_DTH) {
        return result;
    }
    {
        u16 other_move_kind = other->move_kind;
        if (other_move_kind == MOVE_KIND_END_GAME ||
            other_move_kind == MOVE_KIND_DTH) {
            return 1;
        }
        {
            s32 met;
            s32 blade;
            s32 other_blade;

            if (kind != MOVE_KIND_RUN && kind != MOVE_KIND_WALK &&
                kind != MOVE_KIND_STEP && kind != MOVE_KIND_ATTACK &&
                kind != MOVE_KIND_CLIMB_ATTACK && kind != MOVE_KIND_ARUN &&
                kind != MOVE_KIND_STANCE && kind != MOVE_KIND_STANCE_CHANGE &&
                kind != MOVE_KIND_DEFLECTED && kind != MOVE_KIND_CROUCH_STEP) {
                return 1;
            }
            two_handed = 0;
            met = g_fight_blades_met;
            blade = (met >> fighter->index) & 1;
            other_blade = (met >> other->index) & 1;
            if (fighter->sub_weapon_live_state != WEAPON_LIVE_OFF) {
                two_handed = blade == 0;
            }
            if (two_handed) {
                s16 roster_id = fighter->roster_id;
                /* a sidearm is no blade: nothing happens */
                if (roster_id == ROSTER_SIDEARM_2 ||
                    roster_id == ROSTER_SIDEARM_1) {
                    return result;
                }
            }
            {
                s16 other_phase = other->attack_phases[other_blade];
                if (other_phase == ATTACK_PHASE_NONE) {
                    if (fighter->attack_phases[blade] > 0) {
                        move_event = MOVE_EVENT_BLADE_STOPPED_TWO;
                        if (!two_handed) {
                            move_event = MOVE_EVENT_BLADE_STOPPED;
                        }
                        fighter->pending_event = move_event;
                    clang:
                        fight_RaiseEvent(fighter->index, FIGHT_EVENT_SPARK,
                                         contact_point, NULL);
                        fight_RaiseEvent(fighter->index, FIGHT_EVENT_CLANG,
                                         contact_point, NULL);
                    }
                    return result;
                }
                {
                    s16 phase = fighter->attack_phases[blade];
                    s16 event;
                    if (phase == other_phase) {
                        /* FAKE: the loop */
                        do {
                            fight_RaiseEvent(fighter->index, FIGHT_EVENT_SPARK,
                                             contact_point, NULL);
                            fight_RaiseEvent(fighter->index, FIGHT_EVENT_CLANG,
                                             contact_point, NULL);
                        } while (0);
                        if (fighter->attack_phases[blade] ==
                            ATTACK_PHASE_STRIKING) {
                            if (WEAPON_IS_GUN(fighter->weapon_id) ||
                                WEAPON_IS_GUN(other->weapon_id)) {
                                fighter->pending_event =
                                    !two_handed ? MOVE_EVENT_BLADE_STOPPED
                                                : MOVE_EVENT_BLADE_STOPPED_TWO;
                            } else {
                                g_fight_lock_requested = 1;
                                g_fight_lock_runner = FIGHT_NOBODY;
                            }
                            return result;
                        }
                        {
                            /* FAKE: through a local */
                            s32 stopped_event_2 =
                                !two_handed ? MOVE_EVENT_BLADE_STOPPED
                                            : MOVE_EVENT_BLADE_STOPPED_TWO;
                            event = stopped_event_2;
                        }
                    } else {
                        if (other_phase < phase) {
                            fighter->pending_event =
                                !two_handed ? MOVE_EVENT_BLADE_STOPPED
                                            : MOVE_EVENT_BLADE_STOPPED_TWO;
                            goto clang;
                        }
                        fight_RaiseEvent(fighter->index, FIGHT_EVENT_HARD_CLANG,
                                         contact_point, NULL);
                        fight_RaiseEvent(fighter->index, FIGHT_EVENT_GRUNT,
                                         contact_point, NULL);
                        /* beaten aside: which way was the other blade going */
                        event = MOVE_EVENT_PARRIED_TWO;
                        if (!two_handed) {
                            s32 heading = -fighter->orientation.vy;
                            /* the blade's motion across the fighter */
                            s32 across =
                                (g_math_sin_table[(heading + ANGLE_TURN / 4) &
                                                  ANGLE_MASK] *
                                     other->blade_motion[other_blade].vx +
                                 g_math_sin_table[heading & ANGLE_MASK] *
                                     other->blade_motion[other_blade].vz) >>
                                12;
                            s32 down = other->blade_motion[other_blade].vy;
                            s32 abs_down = down;
                            s32 abs_across;
                            if (down < 0) {
                                abs_down = -down;
                            }
                            abs_across = across;
                            if (across < 0) {
                                abs_across = -across;
                            }
                            if (abs_across < abs_down) {
                                event = MOVE_EVENT_PARRIED_DOWN;
                                if (down > 0) {
                                    event = MOVE_EVENT_PARRIED_UP;
                                }
                            } else {
                                event = MOVE_EVENT_PARRIED_ACROSS;
                                if (across <= 0) {
                                    event = MOVE_EVENT_PARRIED_BACK;
                                }
                            }
                        }
                    }
                    fighter->pending_event = event;
                }
            }
            return result;
        }
    }
}

/* The stunner's sub-weapon move: at the frame something would be thrown, an
 * opponent in reach who is on its feet staggers. */
static void fight_UpdateStun(void) {
    s32 i;
    Fighter *fighter;
    u16 kind;

    for (i = 0; i < 2; i++) {
        fighter = &g_fighters[i];
        if (fighter->roster_id != ROSTER_ISOHACHI || fighter->same_frame ||
            fighter->move_kind != MOVE_KIND_SUBWEP ||
            fighter->move_frame != fighter->release_frame) {
            continue;
        }
        kind = fighter->other->move_kind;
        if (kind == MOVE_KIND_CLIMB_ATTACK || kind == MOVE_KIND_ARUN ||
            kind == MOVE_KIND_ATTACK || kind == MOVE_KIND_STANCE ||
            kind == MOVE_KIND_MOVE || kind == MOVE_KIND_STANCE_CHANGE ||
            kind == MOVE_KIND_CLIMB || kind == MOVE_KIND_CLIMB_ST ||
            kind == MOVE_KIND_CLIMB_ED || kind == MOVE_KIND_NULL ||
            kind == MOVE_KIND_RELOAD || kind == MOVE_KIND_RUN ||
            kind == MOVE_KIND_RUN_ST || kind == MOVE_KIND_RUN_ED ||
            kind == MOVE_KIND_STEP || kind == MOVE_KIND_SUBWEP ||
            kind == MOVE_KIND_DIRT || kind == MOVE_KIND_CROUCH ||
            kind == MOVE_KIND_CROUCH_STEP || kind == MOVE_KIND_WALK) {
            if (g_fight_distance < g_fight_stun_reach) {
                fighter->other->pending_event = MOVE_EVENT_STUNNED;
                fighter_TryDropSecondSword(fighter->other);
                if (fighter->other->move_kind == MOVE_KIND_STANCE_CHANGE) {
                    fighter->other->stance = fighter->other->default_stance;
                }
            }
        }
    }
}

/* math_TableSqrt (math.h) for a number that is not negative: the same
 * lines without the test of the sign. */
static inline s32 math_TableSqrtPositive(u32 x) {
    s32 lzc_out;

    if (x < MATH_SQRT_TABLE_SIZE) {
        return g_math_sqrt8_table[x] >> 3;
    } else {
        /* FAKE: the leading zeros, then the shift made from them */
        u32 work;

        gte_Lzc(x, &lzc_out);
        work = lzc_out;
        work = MATH_SQRT_SHIFT - (work & ~1);
        x = g_math_sqrt8_table[x >> work];
        return (x << 16) >> (MATH_SQRT_UNSHIFT - (work >> 1));
    }
}

/* ---- body against body ---- */

/* Keeps the two fighters from standing in each other. Each has two spheres,
 * at the chest and between the knees; where one's overlaps the other's the
 * two are pushed apart, the stronger fighter less. Not during the leaping
 * attack.
 *
 * A fighter running at speed that hits the other does more. Two runners
 * that meet head on both go down. A runner that hits a standing fighter
 * from behind knocks it down; one that meets it face to face starts a
 * blade lock, or, if either has a gun, staggers it. */
static void fight_PushBodiesApart(void) {
    s32 push_pair_count;
    s32 i;
    s32 j;
    s32 fighter_index;
    s32 first_push_x;
    s32 first_push_z;
    s32 second_push_x;
    s32 second_push_z;
    s32 dx;
    s32 dy;
    s32 dz;
    s32 radius_sum;
    s32 distance; /* squared, then not */
    s32 overlap_depth;
    s32 quarter_power_difference;
    s32 first_runs_fast;
    s32 second_runs_fast;
    s32 heading_difference;

    if (g_fighters[0].move_kind == MOVE_KIND_HOM_AT ||
        g_fighters[1].move_kind == MOVE_KIND_HOM_AT) {
        return;
    }
    for (fighter_index = 0; fighter_index < 2; fighter_index++) {
        SPAD->body_overlap_points[fighter_index][0].x =
            SPAD->body_points[fighter_index][BODY_CHEST].x;
        SPAD->body_overlap_points[fighter_index][0].y =
            SPAD->body_points[fighter_index][BODY_CHEST].y;
        SPAD->body_overlap_points[fighter_index][0].z =
            SPAD->body_points[fighter_index][BODY_CHEST].z;
        SPAD->body_overlap_points[fighter_index][1].x =
            (SPAD->body_points[fighter_index][BODY_LEGS + 1].x +
             SPAD->body_points[fighter_index][BODY_SECOND_LEG + 1].x) /
            2;
        SPAD->body_overlap_points[fighter_index][1].y =
            (SPAD->body_points[fighter_index][BODY_LEGS + 1].y +
             SPAD->body_points[fighter_index][BODY_SECOND_LEG + 1].y) /
            2;
        SPAD->body_overlap_points[fighter_index][1].z =
            (SPAD->body_points[fighter_index][BODY_LEGS + 1].z +
             SPAD->body_points[fighter_index][BODY_SECOND_LEG + 1].z) /
            2;
    }
    push_pair_count = 0;
    first_push_x = first_push_z = second_push_x = second_push_z = 0;
    for (i = 0; i < 2; i++) {
        for (j = 0; j < 2; j++) {
            dx = SPAD->body_overlap_points[0][i].x -
                 SPAD->body_overlap_points[1][j].x;
            dy = SPAD->body_overlap_points[0][i].y -
                 SPAD->body_overlap_points[1][j].y;
            dz = SPAD->body_overlap_points[0][i].z -
                 SPAD->body_overlap_points[1][j].z;
            radius_sum = g_fight_body_radii[i] + g_fight_body_radii[j];
            if (dx > radius_sum || dx < -radius_sum || dy > radius_sum ||
                dy < -radius_sum || dz > radius_sum || dz < -radius_sum) {
                continue;
            }
            distance = dx * dx + dy * dy + dz * dz;
            if (radius_sum * radius_sum < distance) {
                continue;
            }
            push_pair_count++;
            distance = math_TableSqrtPositive(distance);
            overlap_depth = radius_sum - distance;
            if (overlap_depth > PUSH_DEPTH_MAX) {
                overlap_depth = PUSH_DEPTH_MAX;
            }
            if (distance == 0) {
                distance = 1;
            }
            quarter_power_difference =
                (g_fighters[1].power - g_fighters[0].power) / 4;
            first_push_x +=
                (dx *
                 (overlap_depth * (PUSH_SHARE_ONE + quarter_power_difference)) /
                 distance) >>
                PUSH_SHARE_SHIFT;
            first_push_z +=
                (dz *
                 (overlap_depth * (PUSH_SHARE_ONE + quarter_power_difference)) /
                 distance) >>
                PUSH_SHARE_SHIFT;
            second_push_x -=
                (dx *
                 (overlap_depth * (PUSH_SHARE_ONE - quarter_power_difference)) /
                 distance) >>
                PUSH_SHARE_SHIFT;
            second_push_z -=
                (dz *
                 (overlap_depth * (PUSH_SHARE_ONE - quarter_power_difference)) /
                 distance) >>
                PUSH_SHARE_SHIFT;
        }
    }
    if (push_pair_count == 0) {
        return;
    }
    first_runs_fast = 0;
    if (g_fighters[0].move_kind == MOVE_KIND_RUN ||
        g_fighters[0].move_kind == MOVE_KIND_ARUN ||
        g_fighters[0].move_kind == MOVE_KIND_RUN_ST) {
        if (g_fighters[0].run_speed > RUN_COLLIDE_SPEED) {
            first_runs_fast = 1;
        }
    }
    second_runs_fast = 0;
    if (g_fighters[1].move_kind == MOVE_KIND_RUN ||
        g_fighters[1].move_kind == MOVE_KIND_ARUN ||
        g_fighters[1].move_kind == MOVE_KIND_RUN_ST) {
        if (g_fighters[1].run_speed > RUN_COLLIDE_SPEED) {
            second_runs_fast = 1;
        }
    }
    heading_difference =
        (g_fighters[0].orientation.vy - g_fighters[1].orientation.vy) &
        ANGLE_MASK;
    if (heading_difference >= ANGLE_TURN / 2) {
        heading_difference = ANGLE_TURN - heading_difference;
    }
    if (first_runs_fast) {
        if (second_runs_fast) {
            if (heading_difference > HEAD_ON_ANGLE) {
                fight_RaiseEvent(
                    0, FIGHT_EVENT_COLLIDE, &g_fighters[0].world_pos.x, NULL);
                g_fighters[1].pending_event = MOVE_EVENT_RUNNERS_COLLIDE;
                g_fighters[0].pending_event = MOVE_EVENT_RUNNERS_COLLIDE;
                fighter_TryDropSecondSword(&g_fighters[0]);
                fighter_TryDropSecondSword(&g_fighters[1]);
                push_pair_count = 0;
            }
        } else {
            if (heading_difference < FROM_BEHIND_ANGLE &&
                g_fighters[1].move_kind == MOVE_KIND_STANCE) {
                g_fighters[1].pending_event = MOVE_EVENT_RUN_DOWN;
                fighter_TryDropSecondSword(&g_fighters[1]);
            }
            if (heading_difference > HEAD_ON_ANGLE &&
                g_fighters[1].move_kind == MOVE_KIND_STANCE) {
                if (WEAPON_IS_GUN(g_fighters[0].weapon_id) ||
                    WEAPON_IS_GUN(g_fighters[1].weapon_id)) {
                    fight_RaiseEvent(1, FIGHT_EVENT_COLLIDE,
                                     &g_fighters[1].world_pos.x, NULL);
                    fight_RaiseEvent(
                        1, FIGHT_EVENT_GRUNT, &g_fighters[1].world_pos.x, NULL);
                    g_fighters[1].pending_event = MOVE_EVENT_STAGGER;
                } else {
                    g_fight_lock_requested = 1;
                    g_fight_lock_runner = 0;
                }
            }
        }
    } else if (second_runs_fast) {
        if (heading_difference < FROM_BEHIND_ANGLE &&
            g_fighters[0].move_kind == MOVE_KIND_STANCE) {
            g_fighters[0].pending_event = MOVE_EVENT_RUN_DOWN;
            fighter_TryDropSecondSword(&g_fighters[0]);
        }
        if (heading_difference > HEAD_ON_ANGLE &&
            g_fighters[0].move_kind == MOVE_KIND_STANCE) {
            if (WEAPON_IS_GUN(g_fighters[0].weapon_id) ||
                WEAPON_IS_GUN(g_fighters[1].weapon_id)) {
                fight_RaiseEvent(
                    0, FIGHT_EVENT_COLLIDE, &g_fighters[0].world_pos.x, NULL);
                fight_RaiseEvent(
                    0, FIGHT_EVENT_GRUNT, &g_fighters[0].world_pos.x, NULL);
                g_fighters[0].pending_event = MOVE_EVENT_STAGGER;
            } else {
                g_fight_lock_requested = 1;
                g_fight_lock_runner = 1;
            }
        }
    }
    if (push_pair_count != 0) {
        g_fighters[0].push.vx += first_push_x;
        g_fighters[0].push.vz += first_push_z;
        g_fighters[1].push.vx += second_push_x;
        g_fighters[1].push.vz += second_push_z;
    }
}

/* ---- blade against the stage ---- */

/* Something on the stage that a blade meets (the list
 * resource_GetStageObstacles returns, ended by a zero type): a place, and
 * whether it is a prop that a blade cuts down or something that stops it. */
typedef struct {
    s16 type; /* 0 ends the list; OBSTACLE_PROP; anything else stops a blade */
    s16 used; /* a prop that has been cut down */
    s32 x;
    s32 y;
    s32 z;
} StageObstacle;

#define OBSTACLE_PROP 2

/* A sweep is four points: a blade's two ends now, then last frame. */
#define SWEEP_CORNERS 4

void fight_ResetObstacles(void) {
    StageObstacle *obstacle =
        (StageObstacle *)ADDR(resource_GetStageObstacles());
    while (obstacle->type != 0) {
        obstacle->used = 0;
        obstacle++;
    }
}

/* Whether a blade's sweep this frame passed through an obstacle. `sweeps`
 * holds a fighter's sweeps, four points each (the blade's two ends now and
 * last frame); `sweep` is which. Returns 1 when the blade was stopped, with
 * where in hit_points[0].
 *
 * A prop in the way is cut down, and the blade goes on. A pole arm
 * (`pole_arm`) has two sweeps, its whole length and then its blade: a prop
 * in the way of the first is cut only if the second also passes through
 * it, and otherwise stops the shaft. */
static s32 fight_BladeMeetsObstacle(s32 sweep, s32 pole_arm, Vec3 *sweeps) {
    WeaponHitScratch *scr = &SPAD->tail.weapon_hit;
    StageObstacle *obstacle;
    Vec3 *corner_a;
    Vec3 *corner_b;
    Vec3 *corner_c;
    s32 i;
    s32 corner;
    /* a corner of the sweep by row and column, then: which obstacle, and
     * which triangle of the sweep */
    s32 row_or_obstacle;
    s32 column_or_triangle;

    scr->bbox_max = sweeps[sweep * SWEEP_CORNERS];
    scr->bbox_min = scr->bbox_max;

    for (i = 1; i < SWEEP_CORNERS; i++) {
        row_or_obstacle = i / 2;
        column_or_triangle = i & 1;
        corner =
            sweep * SWEEP_CORNERS + row_or_obstacle * 2 + column_or_triangle;
        if (sweeps[corner].x < scr->bbox_min.x) {
            scr->bbox_min.x = sweeps[corner].x;
        } else if (scr->bbox_max.x < sweeps[corner].x) {
            scr->bbox_max.x = sweeps[corner].x;
        }
        if (sweeps[corner].z < scr->bbox_min.z) {
            scr->bbox_min.z = sweeps[corner].z;
        } else if (scr->bbox_max.z < sweeps[corner].z) {
            scr->bbox_max.z = sweeps[corner].z;
        }
        if (sweeps[corner].y > scr->bbox_max.y) {
            scr->bbox_max.y = sweeps[corner].y;
        }
    }

    obstacle = (StageObstacle *)ADDR(resource_GetStageObstacles());
    for (row_or_obstacle = 0; obstacle->type != 0; row_or_obstacle++,
        obstacle++) {
        if (obstacle->used) {
            continue;
        }
        if (obstacle->y > scr->bbox_max.y) {
            continue;
        }
        if (obstacle->x < scr->bbox_min.x) {
            continue;
        }
        if (scr->bbox_max.x < obstacle->x) {
            continue;
        }
        if (obstacle->z < scr->bbox_min.z) {
            continue;
        }
        if (scr->bbox_max.z < obstacle->z) {
            continue;
        }

        scr->hit_points[0].x = obstacle->x;
        scr->hit_points[0].z = obstacle->z;
        for (column_or_triangle = 0; column_or_triangle < 2;
             column_or_triangle++) {
            if (column_or_triangle == 0) {
                corner_a = &sweeps[sweep * SWEEP_CORNERS];
                corner_b = &sweeps[sweep * SWEEP_CORNERS + 1];
                corner_c = &sweeps[sweep * SWEEP_CORNERS + 2];
            } else {
                corner_a = &sweeps[sweep * SWEEP_CORNERS + 1];
                corner_b = &sweeps[sweep * SWEEP_CORNERS + 2];
                corner_c = &sweeps[sweep * SWEEP_CORNERS + 3];
            }
            if (math_PointInTriangleXZ(
                    (s32 *)corner_a, (s32 *)corner_b, (s32 *)corner_c,
                    &scr->hit_points[0].x)) {
                if (obstacle->type != OBSTACLE_PROP) {
                    scr->hit_points[0].y =
                        (scr->bbox_min.y + scr->bbox_max.y) / 2;
                    return 1;
                }
                if (pole_arm && sweep == 0) {
                    sweep = 1;
                    column_or_triangle = -1;
                    continue;
                }
                stage_CutProp(
                    row_or_obstacle,
                    math_TriangleHeading((VECTOR *)corner_a, (VECTOR *)corner_b,
                                         (VECTOR *)corner_c));
                snd_StartPropSounds(corner_a);
                obstacle->used = 1;
                return 0;
            }
        }
        if (!pole_arm || sweep == 0) {
            continue;
        }
        scr->hit_points[0].y = (scr->bbox_min.y + scr->bbox_max.y) / 2;
        return 1;
    }
    return 0;
}

/* Whether the boxes round the two shapes overlap. */
static inline s32 fight_BoxesOverlap(WeaponHitScratch *scr) {
    return scr->bbox_min.x <= scr->other_bbox_max.x &&
           scr->bbox_max.x >= scr->other_bbox_min.x &&
           scr->bbox_min.y <= scr->other_bbox_max.y &&
           scr->bbox_max.y >= scr->other_bbox_min.y &&
           scr->bbox_min.z <= scr->other_bbox_max.z &&
           scr->bbox_max.z >= scr->other_bbox_min.z;
}

/* ---- blade against blade: the test ---- */

/* fight_TestBlades' sweeps, sixteen points: a fighter's main blade and then
 * its off hand's, the first fighter's and then the second's. */
enum {
    SWEEP_OFF_HAND = SWEEP_CORNERS,
    SWEEP_SECOND_FIGHTER = 2 * SWEEP_CORNERS,
    SWEEP_POINTS = 4 * SWEEP_CORNERS,
};

/* A fighter's blades are not tested in its first frames: there is no last
 * frame's place to sweep from. */
#define SWEEP_FIRST_FRAME 4

/* A weapon that is not out is put this far down, where nothing is. */
#define SWEEP_NOWHERE_Y 100000

/* Whether the two fighters' blades met this frame. A blade is a sweep: the
 * quadrilateral between where it is and where it was last frame, tested as
 * two triangles; a fighter with a weapon out in the off hand has two
 * sweeps. Returns -1, or which sweep of each fighter met: bit 0 for the
 * first fighter's, bit 1 for the second's.
 *
 * First each attacking blade is tried against the stage's obstacles, which
 * stop it with a clang. Then every triangle of one fighter is tried
 * against every triangle of the other, in a frame of the first's own
 * (hit_FrameTriangleByNormal); a triangle too thin to have a frame is
 * tried the other way round. The sweeps are built at half scale where the
 * first sixteen body points are, which are put back afterwards. */
static s32 fight_TestBlades(void) {
    WeaponHitScratch *scr = &SPAD->tail.weapon_hit;
    Vec3 *sweeps = SPAD->body_points[0];
    Vec3 saved[SWEEP_POINTS];
    s32 triangles[2];
    s32 i;
    s32 j;
    s32 other_triangle;
    u32 thin;
    s32 k;
    s32 *words;
    Fighter *fighter;

    if (g_fighters[0].frames_alive < SWEEP_FIRST_FRAME) {
        return -1;
    }
    if (g_fighters[1].frames_alive < SWEEP_FIRST_FRAME) {
        return -1;
    }
    for (k = 0; k < SWEEP_POINTS; k++) {
        saved[k] = SPAD->body_points[0][k];
    }

    for (i = 0; i < 2; i++) {
        fighter = &g_fighters[i];
        if (WEAPON_IS_GUN(fighter->weapon_id)) {
            continue;
        }
        if (!(MOVE_IS_ATTACK(fighter->move_kind))) {
            continue;
        }
        if (fighter->weapon_id == WEAPON_ID_NAGINATA ||
            fighter->weapon_id == WEAPON_ID_YARI) {
            sweeps[0] = SPAD->weapon_points[i][0];
            sweeps[1] = SPAD->weapon_points[i][2];
            sweeps[2] = fighter->prev_weapon_points[0];
            sweeps[3] = fighter->prev_weapon_points[2];
            sweeps[SWEEP_OFF_HAND + 0] = SPAD->weapon_points[i][0];
            sweeps[SWEEP_OFF_HAND + 1] = SPAD->weapon_points[i][1];
            sweeps[SWEEP_OFF_HAND + 2] = fighter->prev_weapon_points[0];
            sweeps[SWEEP_OFF_HAND + 3] = fighter->prev_weapon_points[1];
        } else {
            sweeps[0] = SPAD->weapon_points[i][0];
            sweeps[1] = SPAD->weapon_points[i][1];
            sweeps[2] = fighter->prev_weapon_points[0];
            sweeps[3] = fighter->prev_weapon_points[1];
            if (fighter->sub_weapon_live_state != WEAPON_LIVE_OFF) {
                sweeps[SWEEP_OFF_HAND + 0] = SPAD->second_weapon_points[i][0];
                sweeps[SWEEP_OFF_HAND + 1] = SPAD->second_weapon_points[i][1];
                sweeps[SWEEP_OFF_HAND + 2] =
                    fighter->prev_second_weapon_points[0];
                sweeps[SWEEP_OFF_HAND + 3] =
                    fighter->prev_second_weapon_points[1];
            }
        }
        if (fighter->move_frame >= fighter->hit_start_frames[0] &&
            fighter->move_frame <= fighter->hit_end_frames[0] &&
            fight_BladeMeetsObstacle(0,
                                     fighter->weapon_id == WEAPON_ID_NAGINATA ||
                                         fighter->weapon_id == WEAPON_ID_YARI,
                                     sweeps)) {
            fight_RaiseEvent(i, FIGHT_EVENT_SPARK, &scr->hit_points[0].x, NULL);
            fight_RaiseEvent(
                i, FIGHT_EVENT_HARD_CLANG, &scr->hit_points[0].x, NULL);
            fight_RaiseEvent(i, FIGHT_EVENT_GRUNT, &scr->hit_points[0].x, NULL);
            fighter->pending_event =
                fighter->sub_weapon_live_state != WEAPON_LIVE_OFF
                    ? MOVE_EVENT_BLADE_STOPPED_TWO
                    : MOVE_EVENT_BLADE_STOPPED;
            fighter->attack_armed = 0;
        } else if (fighter->sub_weapon_live_state != WEAPON_LIVE_OFF &&
                   fighter->move_frame >= fighter->hit_start_frames[1] &&
                   fighter->move_frame <= fighter->hit_end_frames[1] &&
                   fight_BladeMeetsObstacle(1, 0, sweeps)) {
            fight_RaiseEvent(i, FIGHT_EVENT_SPARK, &scr->hit_points[0].x, NULL);
            fight_RaiseEvent(
                i, FIGHT_EVENT_HARD_CLANG, &scr->hit_points[0].x, NULL);
            fight_RaiseEvent(i, FIGHT_EVENT_GRUNT, &scr->hit_points[0].x, NULL);
            fighter->pending_event = MOVE_EVENT_BLADE_STOPPED;
            fighter->attack_armed = 0;
        }
    }

    for (i = 0; i < 2; i++) {
        Vec3 *dst = &sweeps[i * SWEEP_SECOND_FIGHTER];
        fighter = &g_fighters[i];
        dst[0] = SPAD->weapon_points[i][0];
        dst[1] = SPAD->weapon_points[i][1];
        dst[2] = fighter->prev_weapon_points[0];
        dst[3] = fighter->prev_weapon_points[1];
        triangles[i] = 2;
        if (fighter->is_defeated ||
            fighter->weapon_live_state == WEAPON_LIVE_OFF ||
            fighter->roster_id == ROSTER_VICTIM) {
            /* a weapon that is not out is put where nothing is */
            dst[0].y = SWEEP_NOWHERE_Y;
            dst[1].y = SWEEP_NOWHERE_Y;
            dst[2].y = SWEEP_NOWHERE_Y;
            dst[3].y = SWEEP_NOWHERE_Y;
        }
        if (fighter->sub_weapon_live_state != WEAPON_LIVE_OFF) {
            dst[SWEEP_OFF_HAND + 0] = SPAD->second_weapon_points[i][0];
            dst[SWEEP_OFF_HAND + 1] = SPAD->second_weapon_points[i][1];
            dst[SWEEP_OFF_HAND + 2] = fighter->prev_second_weapon_points[0];
            dst[SWEEP_OFF_HAND + 3] = fighter->prev_second_weapon_points[1];
            triangles[i] += 2;
        }
    }

    /* at half scale, so that the tests' products fit */
    for (i = 0; i < 2; i++) {
        for (j = 0; j < 4; j++) {
            /* a fighter's eight points, two at a time */
            words = (s32 *)((u8 *)sweeps + (i * 0x60 + j * 0x18));
            words[0] >>= 1;
            words[1] >>= 1;
            words[2] >>= 1;
            words[3] >>= 1;
            words[4] >>= 1;
            words[5] >>= 1; /* the second point's z */
        }
    }

    thin = 0;
    for (j = 0; j < triangles[0]; j++) {
        switch (j) {
        case 0:
            SET_PTR(scr->shape_points[0], &sweeps[2]);
            SET_PTR(scr->shape_points[1], &sweeps[3]);
            SET_PTR(scr->shape_points[2], &sweeps[1]);
            break;
        case 1:
            SET_PTR(scr->shape_points[0], &sweeps[0]);
            SET_PTR(scr->shape_points[1], &sweeps[1]);
            SET_PTR(scr->shape_points[2], &sweeps[2]);
            break;
        case 2:
            SET_PTR(scr->shape_points[0], &sweeps[SWEEP_OFF_HAND + 2]);
            SET_PTR(scr->shape_points[1], &sweeps[SWEEP_OFF_HAND + 3]);
            SET_PTR(scr->shape_points[2], &sweeps[SWEEP_OFF_HAND + 1]);
            break;
        case 3:
            SET_PTR(scr->shape_points[0], &sweeps[SWEEP_OFF_HAND + 0]);
            SET_PTR(scr->shape_points[1], &sweeps[SWEEP_OFF_HAND + 1]);
            SET_PTR(scr->shape_points[2], &sweeps[SWEEP_OFF_HAND + 2]);
            break;
        }
        if (!hit_FrameTriangleByNormal(scr)) {
            thin |= 1 << j;
            continue;
        }
        scr->bbox_max = *PTR(scr->shape_points[0]);
        scr->bbox_min = scr->bbox_max;
        for (k = 1; k < 3; k++) {
            if (PTR(scr->shape_points[k])->x < scr->bbox_min.x) {
                scr->bbox_min.x = PTR(scr->shape_points[k])->x;
            } else if (scr->bbox_max.x < PTR(scr->shape_points[k])->x) {
                scr->bbox_max.x = PTR(scr->shape_points[k])->x;
            }
            if (PTR(scr->shape_points[k])->y < scr->bbox_min.y) {
                scr->bbox_min.y = PTR(scr->shape_points[k])->y;
            } else if (scr->bbox_max.y < PTR(scr->shape_points[k])->y) {
                scr->bbox_max.y = PTR(scr->shape_points[k])->y;
            }
            if (PTR(scr->shape_points[k])->z < scr->bbox_min.z) {
                scr->bbox_min.z = PTR(scr->shape_points[k])->z;
            } else if (scr->bbox_max.z < PTR(scr->shape_points[k])->z) {
                scr->bbox_max.z = PTR(scr->shape_points[k])->z;
            }
        }
        for (other_triangle = 0; other_triangle < triangles[1];
             other_triangle++) {
            switch (other_triangle) {
            case 0:
                SET_PTR(scr->other_shape_points[0],
                        &sweeps[SWEEP_SECOND_FIGHTER + 2]);
                SET_PTR(scr->other_shape_points[1],
                        &sweeps[SWEEP_SECOND_FIGHTER + 3]);
                SET_PTR(scr->other_shape_points[2],
                        &sweeps[SWEEP_SECOND_FIGHTER + 1]);
                break;
            case 1:
                SET_PTR(scr->other_shape_points[0],
                        &sweeps[SWEEP_SECOND_FIGHTER + 0]);
                SET_PTR(scr->other_shape_points[1],
                        &sweeps[SWEEP_SECOND_FIGHTER + 1]);
                SET_PTR(scr->other_shape_points[2],
                        &sweeps[SWEEP_SECOND_FIGHTER + 2]);
                break;
            case 2:
                SET_PTR(scr->other_shape_points[0],
                        &sweeps[SWEEP_SECOND_FIGHTER + SWEEP_OFF_HAND + 2]);
                SET_PTR(scr->other_shape_points[1],
                        &sweeps[SWEEP_SECOND_FIGHTER + SWEEP_OFF_HAND + 3]);
                SET_PTR(scr->other_shape_points[2],
                        &sweeps[SWEEP_SECOND_FIGHTER + SWEEP_OFF_HAND + 1]);
                break;
            case 3:
                SET_PTR(scr->other_shape_points[0],
                        &sweeps[SWEEP_SECOND_FIGHTER + SWEEP_OFF_HAND + 0]);
                SET_PTR(scr->other_shape_points[1],
                        &sweeps[SWEEP_SECOND_FIGHTER + SWEEP_OFF_HAND + 1]);
                SET_PTR(scr->other_shape_points[2],
                        &sweeps[SWEEP_SECOND_FIGHTER + SWEEP_OFF_HAND + 2]);
                break;
            }
            scr->other_bbox_max = *PTR(scr->other_shape_points[0]);
            scr->other_bbox_min = scr->other_bbox_max;
            for (k = 1; k < 3; k++) {
                if (PTR(scr->other_shape_points[k])->x <
                    scr->other_bbox_min.x) {
                    scr->other_bbox_min.x = PTR(scr->other_shape_points[k])->x;
                } else if (scr->other_bbox_max.x <
                           PTR(scr->other_shape_points[k])->x) {
                    scr->other_bbox_max.x = PTR(scr->other_shape_points[k])->x;
                }
                if (PTR(scr->other_shape_points[k])->y <
                    scr->other_bbox_min.y) {
                    scr->other_bbox_min.y = PTR(scr->other_shape_points[k])->y;
                } else if (scr->other_bbox_max.y <
                           PTR(scr->other_shape_points[k])->y) {
                    scr->other_bbox_max.y = PTR(scr->other_shape_points[k])->y;
                }
                if (PTR(scr->other_shape_points[k])->z <
                    scr->other_bbox_min.z) {
                    scr->other_bbox_min.z = PTR(scr->other_shape_points[k])->z;
                } else if (scr->other_bbox_max.z <
                           PTR(scr->other_shape_points[k])->z) {
                    scr->other_bbox_max.z = PTR(scr->other_shape_points[k])->z;
                }
            }
            if (fight_BoxesOverlap(scr) &&
                hit_TrianglesMeet(scr, PTR(scr->other_shape_points[0]),
                                  PTR(scr->other_shape_points[1]),
                                  PTR(scr->other_shape_points[2]))) {
                for (k = 0; k < SWEEP_POINTS; k++) {
                    SPAD->body_points[0][k] = saved[k];
                }
                return ((other_triangle >> 1) * 2) | (j >> 1);
            }
        }
    }
    if (thin == 0) {
        for (k = 0; k < SWEEP_POINTS; k++) {
            SPAD->body_points[0][k] = saved[k];
        }
        return -1;
    }

    for (j = 0; j < triangles[1]; j++) {
        switch (j) {
        case 0:
            SET_PTR(scr->shape_points[0], &sweeps[SWEEP_SECOND_FIGHTER + 2]);
            SET_PTR(scr->shape_points[1], &sweeps[SWEEP_SECOND_FIGHTER + 3]);
            SET_PTR(scr->shape_points[2], &sweeps[SWEEP_SECOND_FIGHTER + 1]);
            break;
        case 1:
            SET_PTR(scr->shape_points[0], &sweeps[SWEEP_SECOND_FIGHTER + 0]);
            SET_PTR(scr->shape_points[1], &sweeps[SWEEP_SECOND_FIGHTER + 1]);
            SET_PTR(scr->shape_points[2], &sweeps[SWEEP_SECOND_FIGHTER + 2]);
            break;
        case 2:
            SET_PTR(scr->shape_points[0],
                    &sweeps[SWEEP_SECOND_FIGHTER + SWEEP_OFF_HAND + 2]);
            SET_PTR(scr->shape_points[1],
                    &sweeps[SWEEP_SECOND_FIGHTER + SWEEP_OFF_HAND + 3]);
            SET_PTR(scr->shape_points[2],
                    &sweeps[SWEEP_SECOND_FIGHTER + SWEEP_OFF_HAND + 1]);
            break;
        case 3:
            SET_PTR(scr->shape_points[0],
                    &sweeps[SWEEP_SECOND_FIGHTER + SWEEP_OFF_HAND + 0]);
            SET_PTR(scr->shape_points[1],
                    &sweeps[SWEEP_SECOND_FIGHTER + SWEEP_OFF_HAND + 1]);
            SET_PTR(scr->shape_points[2],
                    &sweeps[SWEEP_SECOND_FIGHTER + SWEEP_OFF_HAND + 2]);
            break;
        }
        if (!hit_FrameTriangleByNormal(scr)) {
            continue;
        }
        scr->bbox_max = *PTR(scr->shape_points[0]);
        scr->bbox_min = scr->bbox_max;
        for (k = 1; k < 3; k++) {
            if (PTR(scr->shape_points[k])->x < scr->bbox_min.x) {
                scr->bbox_min.x = PTR(scr->shape_points[k])->x;
            } else if (scr->bbox_max.x < PTR(scr->shape_points[k])->x) {
                scr->bbox_max.x = PTR(scr->shape_points[k])->x;
            }
            if (PTR(scr->shape_points[k])->y < scr->bbox_min.y) {
                scr->bbox_min.y = PTR(scr->shape_points[k])->y;
            } else if (scr->bbox_max.y < PTR(scr->shape_points[k])->y) {
                scr->bbox_max.y = PTR(scr->shape_points[k])->y;
            }
            if (PTR(scr->shape_points[k])->z < scr->bbox_min.z) {
                scr->bbox_min.z = PTR(scr->shape_points[k])->z;
            } else if (scr->bbox_max.z < PTR(scr->shape_points[k])->z) {
                scr->bbox_max.z = PTR(scr->shape_points[k])->z;
            }
        }
        for (other_triangle = 0; other_triangle < triangles[0];
             other_triangle++) {
            if (!(thin & (1 << other_triangle))) {
                continue;
            }
            switch (other_triangle) {
            case 0:
                SET_PTR(scr->other_shape_points[0], &sweeps[2]);
                SET_PTR(scr->other_shape_points[1], &sweeps[3]);
                SET_PTR(scr->other_shape_points[2], &sweeps[1]);
                break;
            case 1:
                SET_PTR(scr->other_shape_points[0], &sweeps[0]);
                SET_PTR(scr->other_shape_points[1], &sweeps[1]);
                SET_PTR(scr->other_shape_points[2], &sweeps[2]);
                break;
            case 2:
                SET_PTR(
                    scr->other_shape_points[0], &sweeps[SWEEP_OFF_HAND + 2]);
                SET_PTR(
                    scr->other_shape_points[1], &sweeps[SWEEP_OFF_HAND + 3]);
                SET_PTR(
                    scr->other_shape_points[2], &sweeps[SWEEP_OFF_HAND + 1]);
                break;
            case 3:
                SET_PTR(
                    scr->other_shape_points[0], &sweeps[SWEEP_OFF_HAND + 0]);
                SET_PTR(
                    scr->other_shape_points[1], &sweeps[SWEEP_OFF_HAND + 1]);
                SET_PTR(
                    scr->other_shape_points[2], &sweeps[SWEEP_OFF_HAND + 2]);
                break;
            }
            scr->other_bbox_max = *PTR(scr->other_shape_points[0]);
            scr->other_bbox_min = scr->other_bbox_max;
            for (k = 1; k < 3; k++) {
                if (PTR(scr->other_shape_points[k])->x <
                    scr->other_bbox_min.x) {
                    scr->other_bbox_min.x = PTR(scr->other_shape_points[k])->x;
                } else if (scr->other_bbox_max.x <
                           PTR(scr->other_shape_points[k])->x) {
                    scr->other_bbox_max.x = PTR(scr->other_shape_points[k])->x;
                }
                if (PTR(scr->other_shape_points[k])->y <
                    scr->other_bbox_min.y) {
                    scr->other_bbox_min.y = PTR(scr->other_shape_points[k])->y;
                } else if (scr->other_bbox_max.y <
                           PTR(scr->other_shape_points[k])->y) {
                    scr->other_bbox_max.y = PTR(scr->other_shape_points[k])->y;
                }
                if (PTR(scr->other_shape_points[k])->z <
                    scr->other_bbox_min.z) {
                    scr->other_bbox_min.z = PTR(scr->other_shape_points[k])->z;
                } else if (scr->other_bbox_max.z <
                           PTR(scr->other_shape_points[k])->z) {
                    scr->other_bbox_max.z = PTR(scr->other_shape_points[k])->z;
                }
            }
            if (fight_BoxesOverlap(scr) &&
                hit_TrianglesMeet(scr, PTR(scr->other_shape_points[0]),
                                  PTR(scr->other_shape_points[1]),
                                  PTR(scr->other_shape_points[2]))) {
                for (k = 0; k < SWEEP_POINTS; k++) {
                    SPAD->body_points[0][k] = saved[k];
                }
                return ((j >> 1) * 2) | (other_triangle >> 1);
            }
        }
    }
    for (k = 0; k < SWEEP_POINTS; k++) {
        SPAD->body_points[0][k] = saved[k];
    }
    return -1;
}

/* ---- bullet against body ---- */

/* A shot at a fighter: the line from the gun's butt to its muzzle (which the
 * caller has put in the first two work points), carried on. Marks which
 * points of the fighter's body it passes through, and of those which
 * cores, but only for a gun that points more or less at the fighter. The
 * shot is then cast against the stage: something nearer than the fighter
 * stops it, and dust rises where it lands. Leaves the direction the shot
 * came from for the hit. `quiet`: no muzzle flash or dust. */
static void fighter_TestShot(
    Fighter *target, u32 *hits, u32 *core_hits, s32 quiet) {
    WeaponHitScratch *scr = &SPAD->tail.weapon_hit;
    s32 target_index = target->index;
    Fighter *other = target->other;
    s32 cast_work[64]; /* room for a SegmentCast */
    s32 leading_zeros;
    s32 leading_zeros_2;
    s32 dx; /* the shot's line, then from its start to a place */
    s32 dy;
    s32 dz;
    s32 off_aim;
    /* the line's length across the ground squared (less its rise
     * squared); then the line's length */
    s32 length;
    s32 pitch_adjacent;
    /* FAKE: a copy of length, then a byte of the table of roots */
    s32 root_byte;
    u32 len_sq;
    s32 cast_distance_sq;
    s32 dir_x;
    s32 dir_y;
    s32 dir_z;
    Vec3 *line_start;
    BodyPoint *point;
    s32 i;

    SET_PTR(scr->shape_points[0], &scr->point_slots.work_points[0]);
    SET_PTR(scr->shape_points[1], &scr->point_slots.work_points[1]);
    scr->normal.vector = scr->point_slots.work_points[1];
    dx = PTR(scr->shape_points[1])->x - PTR(scr->shape_points[0])->x;
    dy = PTR(scr->shape_points[1])->y - PTR(scr->shape_points[0])->y;
    dz = PTR(scr->shape_points[1])->z - PTR(scr->shape_points[0])->z;
    off_aim = (other->opponent_bearing - ratan2(dx, dz)) & ANGLE_MASK;
    if (off_aim >= ANGLE_TURN / 2) {
        off_aim = ANGLE_TURN - off_aim;
    }
    length = dx * dx + dz * dz - dy * dy;
    if (length < 0) {
        printf("ILLEGAL GUN MOTION : %d\n", length);
        return;
    }
    root_byte = length;
    /* MATH_SQRT, written out */
    if ((u32)length < MATH_SQRT_TABLE_SIZE) {
        pitch_adjacent = g_math_sqrt8_table[length] >> 3;
    } else {
        gte_Lzc(root_byte, &leading_zeros);
        {
            s32 even_zeros = ~1;
            s32 shift;
            even_zeros &= leading_zeros;
            shift = MATH_SQRT_SHIFT - even_zeros;
            root_byte = g_math_sqrt8_table[(u32)length >> shift];
            pitch_adjacent = (u32)(root_byte << 16) >>
                             (MATH_SQRT_UNSHIFT - ((u32)shift >> 1));
        }
    }
    scr->angles.vx = -ratan2(dy, pitch_adjacent);
    scr->angles.vy = ratan2(dx, dz);
    scr->angles.vz = 0;
    if (!quiet) {
        fight_RaiseEvent(target_index == 0, FIGHT_EVENT_SHOT,
                         &scr->normal.vector.x, &scr->angles.vx);
    }
    len_sq = dx * dx + dy * dy + dz * dz;
    /* MATH_SQRT, written out */
    if (len_sq < MATH_SQRT_TABLE_SIZE) {
        length = g_math_sqrt8_table[len_sq] >> 3;
    } else {
        gte_Lzc(len_sq, &leading_zeros_2);
        {
            s32 even_zeros = ~1;
            s32 shift;
            s32 root8;
            even_zeros &= leading_zeros_2;
            shift = MATH_SQRT_SHIFT - even_zeros;
            root8 = g_math_sqrt8_table[len_sq >> shift];
            length =
                (u32)(root8 << 16) >> (MATH_SQRT_UNSHIFT - ((u32)shift >> 1));
        }
    }
    dir_x = (dx << 12) / length;
    dir_y = (dy << 12) / length;
    dir_z = (dz << 12) / length;
    /* from the muzzle, four times ONE on */
    *PTR(scr->shape_points[0]) = *PTR(scr->shape_points[1]);
    PTR(scr->shape_points[1])->x += dir_x * 4;
    PTR(scr->shape_points[1])->y += dir_y * 4;
    PTR(scr->shape_points[1])->z += dir_z * 4;
    if (off_aim < ANGLE_TURN / 4) {
        hit_FrameSegment(scr);

        point = g_fighter_body_points[target_index];

        for (i = 0; i < BODY_POINT_COUNT; i++, point++) {
            Vec3 *center;
            if (!target->off_arm_ok && i >= BODY_OFF_ARM &&
                i < BODY_SWORD_ARM) {
                continue;
            }
            center = &SPAD->body_points[target_index][i];
            if (hit_SphereMeetsSegment(
                    scr, center, point->radius, point->reach_sq)) {
                s32 bit = 1 << i;
                *hits |= bit;
                if (point->has_core &&
                    hit_SphereMeetsSegment(scr, center, point->core_radius,
                                           point->core_reach_sq)) {
                    *core_hits |= bit;
                }
            }
        }
    }
    /* the cast starts a little behind the muzzle */
    scr->edge_a.x = PTR(scr->shape_points[0])->x - dir_x / 4;
    scr->edge_a.y = PTR(scr->shape_points[0])->y - dir_y / 4;
    scr->edge_a.z = PTR(scr->shape_points[0])->z - dir_z / 4;
    if (collision_CastWith(
            &scr->edge_a.x, &PTR(scr->shape_points[1])->x,
            &scr->hit_points[0].x, &scr->angles.vx, (SegmentCast *)cast_work) &&
        collision_GetSurface() != SURFACE_BARRIER) {
        if (*hits != 0) {
            line_start = PTR(scr->shape_points[0]);
            dx = scr->hit_points[0].x - line_start->x;
            dy = scr->hit_points[0].y - line_start->y;
            dz = scr->hit_points[0].z - line_start->z;
            cast_distance_sq = dx * dx + dy * dy + dz * dz;
            dx = target->world_pos.x - line_start->x;
            dy = target->world_pos.y - line_start->y;
            dz = target->world_pos.z - line_start->z;
            if (cast_distance_sq < dx * dx + dy * dy + dz * dz) {
                *core_hits = 0;
                *hits = 0;
                if (!quiet) {
                    fight_RaiseEvent(target_index == 0, FIGHT_EVENT_DUST,
                                     &scr->hit_points[0].x, NULL);
                }
            }
        } else if (!quiet) {
            fight_RaiseEvent(target_index == 0, FIGHT_EVENT_DUST,
                             &scr->hit_points[0].x, NULL);
        }
    }
    g_fight_blow_dir_x = -dir_x;
    g_fight_blow_dir_y = -dir_y;
    g_fight_blow_dir_z = -dir_z;
}

/* ---- blade against body ---- */

/* A blade whose tip has moved further than this in a frame is tested as
 * four triangles, not two. */
#define SWEEP_SPLIT_DISTANCE 2500
/* The sweep's two points halfway between where the blade was and where it
 * is, after its four corners in the scratch. */
#define SWEEP_HALFWAY SWEEP_CORNERS
/* An attack is winding up in the last frames before it can hit. */
#define WINDING_UP_FRAMES 3

/* Tests each fighter's weapons against the other's body and, where one
 * landed, has the hit take effect.
 *
 * A gun fires at the frame its attack begins to hit, if it is loaded: the
 * main weapon, or a sidearm. A blade that is out is tested every frame as
 * the sweep between where it was and where it is (four triangles, not two,
 * when it has moved far); a pole arm is two sweeps, its blade and its
 * shaft.
 *
 * Of the body points that were reached, the one nearest the blade's tip
 * last frame is the one hit. A blade that is merely in the way (the
 * attack is not in its hitting frames) only pushes the fighter off it, and
 * a runner that meets a standing fighter's blade so starts a blade lock.
 * A hit proper kills if it reached the point's core, but not with the
 * shaft.
 *
 * `paired` 1: the blow of a paired move, which draws blood and nothing
 * else. */
static void fight_TestBladesOnBodies(s32 paired) {
    WeaponHitScratch *scr = &SPAD->tail.weapon_hit;
    /* FAKE: &g_fight_blow_dir_x, written where it is used */
    s16 *blow_dir = &g_fight_blow_dir_x;
    s32 i;

    for (i = 0; i < 2; i++) {
        Fighter *other;
        Fighter *fighter;
        u32 hits;
        u32 core_hits;
        u32 off_hand_hits;
        u32 shaft_hits;
        u32 edge_hits;
        s32 sweeps;
        s32 sweep;
        /* which blade a sweep is of, 0 the main weapon's and 1 the off
         * hand's; then which one the hit was by (a bullet counts as the off
         * hand's) */
        s32 blade;
        /* which two points of the weapon a sweep is between; then how far
         * each end of the blade is from the fighter that holds it, squared */
        s32 near_end;
        s32 far_end;
        s32 triangle_count;
        s32 index; /* a triangle of the sweep, then the body point hit */
        s32 dx;    /* from one place to another, many times over */
        s32 dy;
        s32 dz;
        s32 best;
        /* a body point, then whether the off hand's blade hit */
        s32 point_or_off_hand;
        s32 j;
        BodyPoint *point;
        s32 winding_up;
        s32 shot;
        s32 bit;
        s32 off_aim;
        s32 facing;
        /* how far the blade's tip moved, squared; how near a body point is;
         * how squarely the blade is moving at the fighter */
        s32 work;
        s32 by_sidearm;
        s32 kills;

        if (paired == 1 && g_fight_paired_move_initiator == i) {
            continue;
        }
        fighter = &g_fighters[i];
        other = g_fighters;
        if (i == 0) {
            other++;
        }
        off_hand_hits = 0;
        shaft_hits = 0;
        edge_hits = 0;
        sweeps = 0;
        hits = 0;
        core_hits = 0;
        if (!other->is_defeated &&
            other->weapon_live_state != WEAPON_LIVE_OFF &&
            other->frames_alive != 0) {
            sweeps = other->roster_id != ROSTER_VICTIM;
        }
        if (other->sub_weapon_live_state != WEAPON_LIVE_OFF) {
            sweeps++;
        }
        if (sweeps != 0 && (other->weapon_id == WEAPON_ID_NAGINATA ||
                            other->weapon_id == WEAPON_ID_YARI)) {
            sweeps++;
        }
        if ((WEAPON_IS_GUN(other->weapon_id)) &&
            (MOVE_IS_ATTACK(other->move_kind)) &&
            other->move_frame == other->hit_start_frames[0]) {
            if (other->ammo != 0) {
                other->ammo -= 1;
                scr->point_slots.work_points[0] =
                    SPAD->weapon_points[i == 0][1];
                scr->point_slots.work_points[1] =
                    SPAD->weapon_points[i == 0][0];
                fighter_TestShot(fighter, &hits, &core_hits, 0);
                off_hand_hits |= hits;
            } else {
                fight_RaiseEvent(i == 0, FIGHT_EVENT_EMPTY_GUN,
                                 i == 0 ? &SPAD->weapon_points[1][1].x
                                        : &SPAD->weapon_points[0][1].x,
                                 NULL);
                sweeps = 0;
            }
        }
        if ((other->roster_id == ROSTER_SIDEARM_2 ||
             other->roster_id == ROSTER_SIDEARM_1) &&
            other->off_arm_ok && (MOVE_IS_ATTACK(other->move_kind)) &&
            other->move_frame == other->hit_start_frames[1] &&
            other->ammo != 0) {
            other->ammo -= 1;
            scr->point_slots.work_points[0] =
                SPAD->second_weapon_points[i == 0][1];
            scr->point_slots.work_points[1] =
                SPAD->second_weapon_points[i == 0][0];
            fighter_TestShot(fighter, &hits, &core_hits,
                             other->roster_id == ROSTER_SIDEARM_1);
            fight_RaiseEvent(i == 0, FIGHT_EVENT_THROW,
                             i == 0 ? &SPAD->weapon_points[1][1].x
                                    : &SPAD->weapon_points[0][1].x,
                             NULL);
            off_hand_hits |= hits;
        }
        for (sweep = 0; sweep < sweeps; sweep++) {
            s32 test_cores; /* not for a pole arm's shaft */

            if (sweep == 0) {
                blade = 0;
                near_end = 0;
                far_end = 1;
                test_cores = 1;
            } else if (other->sub_weapon_live_state != WEAPON_LIVE_OFF) {
                blade = 1;
                near_end = 0;
                far_end = 1;
                test_cores = 1;
            } else if (other->weapon_id == WEAPON_ID_NAGINATA ||
                       other->weapon_id == WEAPON_ID_YARI) {
                blade = 0;
                near_end = 1;
                far_end = 2;
                test_cores = 0;
            }
            if (blade == 0) {
                scr->point_slots.work_points[0] =
                    SPAD->weapon_points[i == 0][near_end];
                scr->point_slots.work_points[1] =
                    SPAD->weapon_points[i == 0][far_end];
                scr->point_slots.work_points[2] =
                    other->prev_weapon_points[near_end];
                scr->point_slots.work_points[3] =
                    other->prev_weapon_points[far_end];
            } else {
                scr->point_slots.work_points[0] =
                    SPAD->second_weapon_points[i == 0][near_end];
                scr->point_slots.work_points[1] =
                    SPAD->second_weapon_points[i == 0][far_end];
                scr->point_slots.work_points[2] =
                    other->prev_second_weapon_points[near_end];
                scr->point_slots.work_points[3] =
                    other->prev_second_weapon_points[far_end];
            }
            scr->point_slots.work_points[SWEEP_HALFWAY].x =
                (scr->point_slots.work_points[0].x +
                 scr->point_slots.work_points[2].x) /
                2;
            scr->point_slots.work_points[SWEEP_HALFWAY].y =
                (scr->point_slots.work_points[0].y +
                 scr->point_slots.work_points[2].y) /
                2;
            scr->point_slots.work_points[SWEEP_HALFWAY].z =
                (scr->point_slots.work_points[0].z +
                 scr->point_slots.work_points[2].z) /
                2;
            scr->point_slots.work_points[SWEEP_HALFWAY + 1].x =
                (scr->point_slots.work_points[1].x +
                 scr->point_slots.work_points[3].x) /
                2;
            scr->point_slots.work_points[SWEEP_HALFWAY + 1].y =
                (scr->point_slots.work_points[1].y +
                 scr->point_slots.work_points[3].y) /
                2;
            scr->point_slots.work_points[SWEEP_HALFWAY + 1].z =
                (scr->point_slots.work_points[1].z +
                 scr->point_slots.work_points[3].z) /
                2;
            dx = scr->point_slots.work_points[1].x -
                 scr->point_slots.work_points[3].x;
            dy = scr->point_slots.work_points[1].y -
                 scr->point_slots.work_points[3].y;
            dz = scr->point_slots.work_points[1].z -
                 scr->point_slots.work_points[3].z;
            work = dx * dx + dy * dy + dz * dz;
            triangle_count = 2;
            if (work >= SWEEP_SPLIT_DISTANCE * SWEEP_SPLIT_DISTANCE) {
                triangle_count = 4;
            }
            for (index = 0; index < triangle_count; index++) {
                if (triangle_count == 2) {
                    if (index == 0) {
                        SET_PTR(scr->shape_points[0],
                                &scr->point_slots.work_points[2]);
                        SET_PTR(scr->shape_points[1],
                                &scr->point_slots.work_points[3]);
                        SET_PTR(scr->shape_points[2],
                                &scr->point_slots.work_points[1]);
                    } else {
                        SET_PTR(scr->shape_points[0],
                                &scr->point_slots.work_points[0]);
                        SET_PTR(scr->shape_points[1],
                                &scr->point_slots.work_points[1]);
                        SET_PTR(scr->shape_points[2],
                                &scr->point_slots.work_points[2]);
                    }
                } else {
                    switch (index) {
                    case 0:
                        SET_PTR(scr->shape_points[0],
                                &scr->point_slots.work_points[3]);
                        SET_PTR(scr->shape_points[1],
                                &scr->point_slots.work_points[2]);
                        SET_PTR(
                            scr->shape_points[2],
                            &scr->point_slots.work_points[SWEEP_HALFWAY + 1]);
                        break;
                    case 1:
                        SET_PTR(
                            scr->shape_points[0],
                            &scr->point_slots.work_points[SWEEP_HALFWAY + 1]);
                        SET_PTR(scr->shape_points[1],
                                &scr->point_slots.work_points[SWEEP_HALFWAY]);
                        SET_PTR(scr->shape_points[2],
                                &scr->point_slots.work_points[2]);
                        break;
                    case 2:
                        SET_PTR(
                            scr->shape_points[0],
                            &scr->point_slots.work_points[SWEEP_HALFWAY + 1]);
                        SET_PTR(scr->shape_points[1],
                                &scr->point_slots.work_points[SWEEP_HALFWAY]);
                        SET_PTR(scr->shape_points[2],
                                &scr->point_slots.work_points[1]);
                        break;
                    case 3:
                        SET_PTR(scr->shape_points[0],
                                &scr->point_slots.work_points[1]);
                        SET_PTR(scr->shape_points[1],
                                &scr->point_slots.work_points[0]);
                        SET_PTR(scr->shape_points[2],
                                &scr->point_slots.work_points[SWEEP_HALFWAY]);
                        break;
                    }
                }
                scr->bbox_max = *PTR(scr->shape_points[0]);
                scr->bbox_min = scr->bbox_max;
                for (j = 1; j < 3; j++) {
                    if (PTR(scr->shape_points[j])->x < scr->bbox_min.x) {
                        scr->bbox_min.x = PTR(scr->shape_points[j])->x;
                    } else if (scr->bbox_max.x < PTR(scr->shape_points[j])->x) {
                        scr->bbox_max.x = PTR(scr->shape_points[j])->x;
                    }
                    if (PTR(scr->shape_points[j])->y < scr->bbox_min.y) {
                        scr->bbox_min.y = PTR(scr->shape_points[j])->y;
                    } else if (scr->bbox_max.y < PTR(scr->shape_points[j])->y) {
                        scr->bbox_max.y = PTR(scr->shape_points[j])->y;
                    }
                    if (PTR(scr->shape_points[j])->z < scr->bbox_min.z) {
                        scr->bbox_min.z = PTR(scr->shape_points[j])->z;
                    } else if (scr->bbox_max.z < PTR(scr->shape_points[j])->z) {
                        scr->bbox_max.z = PTR(scr->shape_points[j])->z;
                    }
                }
                fighter_TestBodyInSweep(
                    fighter, hit_FrameTriangle(scr), test_cores);
                hits |= scr->body_hits;
                core_hits |= scr->core_hits;
                if (blade != 0) {
                    off_hand_hits |= scr->body_hits;
                }
                if (sweep == 1 && blade == 0) {
                    shaft_hits |= scr->body_hits;
                } else {
                    edge_hits |= scr->body_hits;
                }
            }
        }
        if (hits == 0) {
            continue;
        }
        point = g_fighter_body_points[i];
        best = 0x7FFFFFFF; /* farther than any */
        scr->normal.vector.x = other->prev_weapon_points[1].x;
        scr->normal.vector.y = other->prev_weapon_points[1].y;
        scr->normal.vector.z = other->prev_weapon_points[1].z;
        for (point_or_off_hand = 0; point_or_off_hand < BODY_POINT_COUNT;
             point_or_off_hand++, point++) {
            if (hits & (1 << point_or_off_hand)) {
                dx = SPAD->body_points[i][point_or_off_hand].x -
                     scr->normal.vector.x;
                dy = SPAD->body_points[i][point_or_off_hand].y -
                     scr->normal.vector.y;
                dz = SPAD->body_points[i][point_or_off_hand].z -
                     scr->normal.vector.z;
                work = dx * dx + dy * dy + dz * dz - point->reach_sq;
                if (work < best) {
                    best = work;
                    index = point_or_off_hand;
                }
            }
        }
        if (paired == 1) {
            math_ReverseUnit(&other->blade_motion[0].vx, blow_dir);
            fight_RaiseEvent(
                i, FIGHT_EVENT_BLOOD, &SPAD->body_points[i][index].x, blow_dir);
            return;
        }
        winding_up = 0;
        if ((other->move_frame >=
                 other->hit_start_frames[0] - WINDING_UP_FRAMES &&
             other->move_frame < other->hit_start_frames[0]) ||
            (other->move_frame >=
                 other->hit_start_frames[1] - WINDING_UP_FRAMES &&
             other->move_frame < other->hit_start_frames[1])) {
            winding_up = 1;
        }
        shot = 0;
        if ((WEAPON_IS_GUN(other->weapon_id) ||
             other->roster_id == ROSTER_SIDEARM_2 ||
             other->roster_id == ROSTER_SIDEARM_1) &&
            (off_hand_hits & (1 << index))) {
            shot = 1;
        }
        /* a blade that is only in the way */
        if (!shot && (!(MOVE_IS_ATTACK(other->move_kind)) || !winding_up)) {
            bit = 1 << index;
            point_or_off_hand = (off_hand_hits & bit) != 0;
            if (point_or_off_hand) {
                dx = SPAD->second_weapon_points[i == 0][0].x -
                     other->world_pos.x;
                dz = SPAD->second_weapon_points[i == 0][0].z -
                     other->world_pos.z;
                near_end = dx * dx + dz * dz;
                dx = SPAD->second_weapon_points[i == 0][1].x -
                     other->world_pos.x;
                dz = SPAD->second_weapon_points[i == 0][1].z -
                     other->world_pos.z;
                far_end = dx * dx + dz * dz;
                dx = SPAD->second_weapon_points[i == 0][1].x -
                     SPAD->second_weapon_points[i == 0][0].x;
                dz = SPAD->second_weapon_points[i == 0][1].z -
                     SPAD->second_weapon_points[i == 0][0].z;
            } else if (shaft_hits & bit) {
                dx = SPAD->weapon_points[i == 0][2].x - other->world_pos.x;
                dz = SPAD->weapon_points[i == 0][2].z - other->world_pos.z;
                near_end = dx * dx + dz * dz;
                dx = SPAD->weapon_points[i == 0][1].x - other->world_pos.x;
                dz = SPAD->weapon_points[i == 0][1].z - other->world_pos.z;
                far_end = dx * dx + dz * dz;
                dx = (SPAD->weapon_points[i == 0][1].x -
                      SPAD->weapon_points[i == 0][2].x) /
                     4;
                dz = (SPAD->weapon_points[i == 0][1].z -
                      SPAD->weapon_points[i == 0][2].z) /
                     4;
            } else {
                dx = SPAD->weapon_points[i == 0][0].x - other->world_pos.x;
                dz = SPAD->weapon_points[i == 0][0].z - other->world_pos.z;
                near_end = dx * dx + dz * dz;
                dx = SPAD->weapon_points[i == 0][1].x - other->world_pos.x;
                dz = SPAD->weapon_points[i == 0][1].z - other->world_pos.z;
                far_end = dx * dx + dz * dz;
                dx = SPAD->weapon_points[i == 0][1].x -
                     SPAD->weapon_points[i == 0][0].x;
                dz = SPAD->weapon_points[i == 0][1].z -
                     SPAD->weapon_points[i == 0][0].z;
            }
            if (far_end < near_end) {
                dx = -dx;
                dz = -dz;
            }
            off_aim = (other->opponent_bearing - ratan2(dx, dz)) & ANGLE_MASK;
            if (off_aim >= ANGLE_TURN / 2) {
                off_aim = ANGLE_TURN - off_aim;
            }
            /* the blade points more at the fighter than away */
            if (off_aim < ANGLE_TURN / 4) {
                /* how far the blade's motion is from straight at the
                 * fighter, and from that how squarely it comes: a quarter
                 * turn's worth when it is straight, nothing from a quarter
                 * turn off. (`blade` is still the last sweep's here; the
                 * push below goes by the blade that touched) */
                work = (ratan2(other->blade_motion[blade].vx,
                               other->blade_motion[blade].vz) +
                        ANGLE_TURN / 2 - fighter->opponent_bearing) &
                       ANGLE_MASK;
                if (work >= ANGLE_TURN / 2) {
                    work = ANGLE_TURN - work;
                }
                work = ANGLE_TURN / 4 - work;
                if (work < 0) {
                    work = 0;
                }
                /* off the blade: a quarter of its length, and with its
                 * motion as far as that is square on */
                dx = dx / 4 +
                     (other->blade_motion[point_or_off_hand].vx * work / 2 >>
                      11);
                /* (the same along z) */
                dz = dz / 4 +
                     (other->blade_motion[point_or_off_hand].vz * work / 2 >>
                      11);
                if (other->move_kind == MOVE_KIND_DAM ||
                    other->move_kind == MOVE_KIND_STAGGER ||
                    other->move_kind == MOVE_KIND_BLOCK ||
                    other->move_kind == MOVE_KIND_DEFLECTED) {
                    fighter->push.vx += dx / 2;
                    fighter->push.vz += dz / 2;
                } else {
                    fighter->push.vx += dx;
                    fighter->push.vz += dz;
                }
                if ((fighter->move_kind == MOVE_KIND_RUN ||
                     fighter->move_kind == MOVE_KIND_ARUN ||
                     fighter->move_kind == MOVE_KIND_RUN_ST) &&
                    other->move_kind == MOVE_KIND_STANCE) {
                    if (WEAPON_IS_GUN(fighter->weapon_id) ||
                        WEAPON_IS_GUN(other->weapon_id)) {
                        other->pending_event = MOVE_EVENT_STAGGER;
                        fight_RaiseEvent(i == 0, FIGHT_EVENT_COLLIDE,
                                         i == 0 ? &g_fighters[1].world_pos.x
                                                : &g_fighters[0].world_pos.x,
                                         NULL);
                        fight_RaiseEvent(i == 0, FIGHT_EVENT_GRUNT,
                                         i == 0 ? &g_fighters[1].world_pos.x
                                                : &g_fighters[0].world_pos.x,
                                         NULL);
                    } else {
                        g_fight_lock_requested = 1;
                        g_fight_lock_runner = i;
                    }
                }
            }
        }
        facing = (fighter->orientation.vy - other->orientation.vy) & ANGLE_MASK;
        if (facing >= ANGLE_TURN / 2) {
            facing = ANGLE_TURN - facing;
        }
        blade = (off_hand_hits & (1 << index)) != 0;
        if (!other->hit_window_open) {
            continue;
        }
        if (other->sub_weapon_live_state != WEAPON_LIVE_OFF &&
            (other->move_frame < other->hit_start_frames[blade] ||
             other->move_frame > other->hit_end_frames[blade])) {
            continue;
        }
        by_sidearm = 0;
        if (!(WEAPON_IS_GUN(other->weapon_id))) {
            if ((other->roster_id == ROSTER_SIDEARM_2 ||
                 other->roster_id == ROSTER_SIDEARM_1) &&
                blade != 0) {
                by_sidearm = 1;
            } else {
                math_ReverseUnit(&other->blade_motion[blade].vx, blow_dir);
            }
        }
        if (!g_fight_every_hit_kills) {
            kills = core_hits & (1 << index);
        } else {
            kills = 1;
        }
        if (shaft_hits & (1 << index) & ~edge_hits) {
            kills = 0;
        }
        /* which blade, in the argument that is a thrown object's kind:
         * fighter_TakeHit reads it as 0 or not when no object struck. No
         * place is given for what it writes first (the original's
         * behaviour: see fighter_TakeHit) */
#if RB_FIX_HIT_NULL_WRITE
        /* (the native port has no memory at address 0 to write to: it
         * gives the place whatever the setting, which changes nothing
         * the game does) */
        if (g_rb_settings.bugfixes || RB_PORT) {
            /* a place for that word, which nothing reads here: it was
             * written at address 0 */
            s32 clashed;
            s32 result = fighter_TakeHit(
                0, fighter, index, facing, kills,
                (LooseObjectKind *)ADDR(blade), by_sidearm, &clashed);

#if RB_PORT
            /* a killing blow, for the player's record (port_stats.h) */
            port_StatsHit(fighter, index, facing, 0, blade, by_sidearm, result);
#endif
            (void)result;
        } else
#endif
        {
            fighter_TakeHit(0, fighter, index, facing, kills,
                            (LooseObjectKind *)ADDR(blade), by_sidearm, NULL);
        }
        other->attack_armed = 0;
    }
}

/* ---- holding the two together ---- */

/* Two that are held too near are pushed apart by this many percent of what
 * is wanting at once; two too far are drawn in by a sixteenth of it. */
#define HOLD_PUSH_PERCENT 80
#define HOLD_PULL_PART 16
/* The push is along the line between them, which is taken as this much
 * shorter than the distance wanted. */
#define HOLD_LINE_LESS 100
/* How far apart a blade lock holds the two. */
#define LOCK_DISTANCE 1100
/* A paired move breaks off when the two are this much further apart than
 * it holds them. */
#define PAIRED_MOVE_SLACK 200
/* Blades lock only between fighters within this height of each other. */
#define LOCK_HEIGHT_MAX 1000

/* Draws the two fighters to a distance apart, quickly if they are too near;
 * returns how far apart they are. */
static s32 fight_HoldDistance(s32 target_separation) {
    s32 separation_x;
    s32 separation_z;
    s32 correction;
    u32 dist_sq;
    s32 distance;
    Fighter *first;
    Fighter *second;

    first = &g_fighters[0];
    second = &g_fighters[1];
    separation_x = first->base_pos.x - second->base_pos.x;
    separation_z = first->base_pos.z - second->base_pos.z;
    dist_sq = (separation_x * separation_x) + (separation_z * separation_z);
    /* MATH_SQRT, written out */
    if (dist_sq < (u32)MATH_SQRT_TABLE_SIZE) {
        distance = g_math_sqrt8_table[dist_sq] >> 3;
    } else {
        s32 leading_zeros;
        u32 shift;
        gte_Lzc(dist_sq, &leading_zeros);
        {
            /* FAKE: shift = MATH_SQRT_SHIFT - (leading_zeros & ~1) */
            u32 even_zeros = -2;
            even_zeros &= leading_zeros;
            shift = MATH_SQRT_SHIFT - even_zeros;
        }
        distance = ((u32)g_math_sqrt8_table[dist_sq >> shift] << 16) >>
                   (MATH_SQRT_UNSHIFT - (shift >> 1));
    }
    if (distance < target_separation) {
        correction = ((target_separation - distance) * HOLD_PUSH_PERCENT) / 100;
    } else {
        correction = (target_separation - distance) / HOLD_PULL_PART;
    }
    first->push.vx =
        (separation_x * correction) / (target_separation - HOLD_LINE_LESS);
    first->push.vz =
        (separation_z * correction) / (target_separation - HOLD_LINE_LESS);
    second->push.vx =
        (separation_x * -correction) / (target_separation - HOLD_LINE_LESS);
    second->push.vz =
        (separation_z * -correction) / (target_separation - HOLD_LINE_LESS);
    return distance;
}

/* The same for a blade lock, at LOCK_DISTANCE; returns how far off that
 * they are. */
s32 fight_HoldLockDistance(void) {
    s32 separation_x;
    s32 separation_z;
    s32 correction;
    u32 dist_sq;
    s32 distance;
    Fighter *first;
    Fighter *second;

    first = &g_fighters[0];
    second = &g_fighters[1];
    separation_x = first->world_pos.x - second->world_pos.x;
    separation_z = first->world_pos.z - second->world_pos.z;
    dist_sq = (separation_x * separation_x) + (separation_z * separation_z);
    /* MATH_SQRT, written out */
    if (dist_sq < (u32)MATH_SQRT_TABLE_SIZE) {
        distance = g_math_sqrt8_table[dist_sq] >> 3;
    } else {
        s32 leading_zeros;
        u32 shift;
        gte_Lzc(dist_sq, &leading_zeros);
        {
            /* FAKE: shift = MATH_SQRT_SHIFT - (leading_zeros & ~1) */
            u32 even_zeros = -2;
            even_zeros &= leading_zeros;
            shift = MATH_SQRT_SHIFT - even_zeros;
        }
        distance = ((u32)g_math_sqrt8_table[dist_sq >> shift] << 16) >>
                   (MATH_SQRT_UNSHIFT - (shift >> 1));
    }
    if (distance < LOCK_DISTANCE) {
        correction = ((LOCK_DISTANCE - distance) * HOLD_PUSH_PERCENT) / 100;
    } else {
        correction = (LOCK_DISTANCE - distance) / HOLD_PULL_PART;
    }
    {
        s32 scale = LOCK_DISTANCE - HOLD_LINE_LESS;
        first->push.vx = (separation_x * correction) / scale;
        first->push.vz = (separation_z * correction) / scale;
        second->push.vx = (separation_x * -correction) / scale;
        second->push.vz = (separation_z * -correction) / scale;
    }
    return distance - LOCK_DISTANCE;
}

/* A frame of a paired move: the two are held at the move's distance and
 * turned to face each other, and at the move's mark its blow lands. If
 * they have come too far apart before then, the move breaks off. */
static void fight_RunPairedMove(void) {
    s32 i;
    Fighter *initiator;
    s32 separation;

    separation = fight_HoldDistance(g_fight_paired_move_separation);

    for (i = 0; i < 2; i++) {
        Fighter *fighter = &g_fighters[i];
        Fighter *other_fighter = fighter->other;
        s32 dx = other_fighter->base_pos.x - fighter->base_pos.x;
        s32 dz = other_fighter->base_pos.z - fighter->base_pos.z;
        fighter_TurnTo(fighter, ratan2(dx, dz));
    }

    {
        s32 index;
        s32 start_pending;
        index = g_fight_paired_move_initiator;
        start_pending = g_fight_paired_move_start_pending;
        initiator = &g_fighters[index];

        if (!start_pending) {
            if (g_fight_falling_fighter == FIGHT_NOBODY_FALLING) {
                if (initiator->paired_hit_frame == initiator->move_frame) {
                    fight_TestBladesOnBodies(1);
                }
            }
        }

        {
            s32 initiator_move_frame;
            initiator_move_frame = initiator->move_frame;
            if (initiator_move_frame < g_fight_paired_move_break_frame) {
                return;
            }
            if (initiator_move_frame >= initiator->paired_hit_frame) {
                return;
            }
            if (g_fight_paired_move_separation + PAIRED_MOVE_SLACK >=
                separation) {
                return;
            }
            {
                Fighter *other;
                other = initiator->other;
                initiator->pending_event = PAIRED_EVENT_BREAK;
                other->pending_event = PAIRED_EVENT_BROKEN;
            }
        }
    }
}

/* Where the blades met: the middle of the two blades that did, three parts
 * where they are now to one where they were. */
static void fight_FindBladeContact(void) {
    WeaponHitScratch *scr = &SPAD->tail.weapon_hit;
    Fighter *first = &g_fighters[0];
    Fighter *second = &g_fighters[1];

    scr->edge_a.x = 0;
    scr->edge_a.y = 0;
    scr->edge_a.z = 0;
    scr->edge_b.x = 0;
    scr->edge_b.y = 0;
    scr->edge_b.z = 0;

    if (g_fight_blades_met & 1) {
        scr->edge_a.x = SPAD->second_weapon_points[0][0].x;
        scr->edge_a.y = SPAD->second_weapon_points[0][0].y;
        scr->edge_a.z = SPAD->second_weapon_points[0][0].z;
        scr->edge_a.x += SPAD->second_weapon_points[0][1].x;
        scr->edge_a.y += SPAD->second_weapon_points[0][1].y;
        scr->edge_a.z += SPAD->second_weapon_points[0][1].z;
        scr->edge_b.x = first->prev_second_weapon_points[0].x;
        scr->edge_b.y = first->prev_second_weapon_points[0].y;
        scr->edge_b.z = first->prev_second_weapon_points[0].z;
        scr->edge_b.x += first->prev_second_weapon_points[1].x;
        scr->edge_b.y += first->prev_second_weapon_points[1].y;
        scr->edge_b.z += first->prev_second_weapon_points[1].z;
    } else {
        scr->edge_a.x = SPAD->weapon_points[0][0].x;
        scr->edge_a.y = SPAD->weapon_points[0][0].y;
        scr->edge_a.z = SPAD->weapon_points[0][0].z;
        scr->edge_a.x += SPAD->weapon_points[0][1].x;
        scr->edge_a.y += SPAD->weapon_points[0][1].y;
        scr->edge_a.z += SPAD->weapon_points[0][1].z;
        scr->edge_b.x = first->prev_weapon_points[0].x;
        scr->edge_b.y = first->prev_weapon_points[0].y;
        scr->edge_b.z = first->prev_weapon_points[0].z;
        scr->edge_b.x += first->prev_weapon_points[1].x;
        scr->edge_b.y += first->prev_weapon_points[1].y;
        scr->edge_b.z += first->prev_weapon_points[1].z;
    }
    if (g_fight_blades_met & 2) {
        scr->edge_a.x += SPAD->second_weapon_points[1][0].x;
        scr->edge_a.y += SPAD->second_weapon_points[1][0].y;
        scr->edge_a.z += SPAD->second_weapon_points[1][0].z;
        scr->edge_b.x += second->prev_second_weapon_points[0].x;
        scr->edge_b.y += second->prev_second_weapon_points[0].y;
        scr->edge_b.z += second->prev_second_weapon_points[0].z;
        scr->edge_a.x += SPAD->second_weapon_points[1][1].x;
        scr->edge_a.y += SPAD->second_weapon_points[1][1].y;
        scr->edge_a.z += SPAD->second_weapon_points[1][1].z;
        scr->edge_b.x += second->prev_second_weapon_points[1].x;
        scr->edge_b.y += second->prev_second_weapon_points[1].y;
        scr->edge_b.z += second->prev_second_weapon_points[1].z;
    } else {
        scr->edge_a.x += SPAD->weapon_points[1][0].x;
        scr->edge_a.y += SPAD->weapon_points[1][0].y;
        scr->edge_a.z += SPAD->weapon_points[1][0].z;
        scr->edge_b.x += second->prev_weapon_points[0].x;
        scr->edge_b.y += second->prev_weapon_points[0].y;
        scr->edge_b.z += second->prev_weapon_points[0].z;
        scr->edge_a.x += SPAD->weapon_points[1][1].x;
        scr->edge_a.y += SPAD->weapon_points[1][1].y;
        scr->edge_a.z += SPAD->weapon_points[1][1].z;
        scr->edge_b.x += second->prev_weapon_points[1].x;
        scr->edge_b.y += second->prev_weapon_points[1].y;
        scr->edge_b.z += second->prev_weapon_points[1].z;
    }
    /* each sum is of four points */
    scr->contact_point.x = ((scr->edge_a.x * 3) + scr->edge_b.x) >> 4;
    scr->contact_point.y = ((scr->edge_a.y * 3) + scr->edge_b.y) >> 4;
    scr->contact_point.z = ((scr->edge_a.z * 3) + scr->edge_b.z) >> 4;
}

/* ---- the frame ---- */

/* A frame of the two fighters against each other, after each has had its
 * own. In a blade lock and in a paired move the two are one, and those
 * have their own code. Otherwise: bodies are kept apart; if the blades met
 * and either was attacking, blade answers blade, and if not, the blades
 * are tested against the bodies; a blade lock that was asked for begins if
 * nothing else has happened to either. Then where the weapons are is kept
 * for the next frame's sweeps, and loose objects and thrown dirt get their
 * turn at the first fighter. */
void fight_UpdateFighterPair(void) {
    Fighter *first = &g_fighters[0];
    Fighter *second = &g_fighters[1];
    s32 i;
    u16 kind;

    kind = g_fighters[0].move_kind;

    if (MOVE_IS_LOCK(kind)) {
        fight_UpdateLock();
    } else if (kind == MOVE_KIND_PAIRED) {
        fight_RunPairedMove();
    } else {
        fight_UpdateStun();
        fight_PushBodiesApart();
        g_fight_blades_met = fight_TestBlades();
        if (g_fight_blades_met >= 0) {
            fight_FindBladeContact();
        }
        if (g_fight_blades_met >= 0 &&
            (first->attack_armed || second->attack_armed)) {
            fighter_ResolveBladeContact(
                first, &SPAD->tail.weapon_hit.contact_point.x);
            fighter_ResolveBladeContact(
                second, &SPAD->tail.weapon_hit.contact_point.x);
            second->attack_armed = 0;
            first->attack_armed = 0;
        } else {
            fight_TestBladesOnBodies(0);
        }
        if (first->frames_alive >= 3 && second->frames_alive >= 3 &&
            g_fight_lock_requested && first->pending_event == MOVE_EVENT_NONE &&
            second->pending_event == MOVE_EVENT_NONE &&
            first->roster_id != ROSTER_VICTIM &&
            second->roster_id != ROSTER_VICTIM) {
            s32 height_gap = first->world_pos.y - second->world_pos.y;
            if (height_gap < 0) {
                height_gap = -height_gap;
            }
            if (height_gap < LOCK_HEIGHT_MAX) {
                first->pending_event = MOVE_EVENT_LOCK;
                second->pending_event = MOVE_EVENT_LOCK;
                second->lock_score = 0;
                first->lock_score = 0;
                g_fight_lock_flash_delay = 0;
                g_fight_lock_frames = 0;
            }
        }
    }

    if (g_fighters[0].move_kind == MOVE_KIND_SURRENDER) {
        g_fight_winner = 1;
        g_sys_screen = SCREEN_KAISHAKU_INIT;
    } else if (g_fighters[1].move_kind == MOVE_KIND_SURRENDER) {
        g_fight_winner = 0;
        g_sys_screen = SCREEN_KAISHAKU_INIT;
    }

    /* Three points whatever the weapon (the original's behaviour): a
     * weapon with two has nothing in the third's place, so what is kept is
     * what other code left there on the scratchpad, an address among it.
     * The next fighter in this place with a pole arm sweeps its shaft from
     * that on its first frame (fight_TestBladesOnBodies), and whether the
     * sweep touches the other fighter then depends on the leftover. */
    for (i = 0; i < 3; i++) {
        g_fighters[0].prev_weapon_points[i] = SPAD->weapon_points[0][i];
        g_fighters[1].prev_weapon_points[i] = SPAD->weapon_points[1][i];
    }

    for (i = 0; i < 2; i++) {
        g_fighters[0].prev_second_weapon_points[i] =
            SPAD->second_weapon_points[0][i];
        g_fighters[1].prev_second_weapon_points[i] =
            SPAD->second_weapon_points[1][i];
    }

    for (i = 0; i < 2; i++) {
        g_fighters[i].chest_pos.x =
            (SPAD->body_points[i][BODY_CHEST].x +
             SPAD->body_points[i][BODY_CHEST + 1].x +
             SPAD->body_points[i][BODY_CHEST + 2].x) /
            3;
        g_fighters[i].chest_pos.y =
            (SPAD->body_points[i][BODY_CHEST].y +
             SPAD->body_points[i][BODY_CHEST + 1].y +
             SPAD->body_points[i][BODY_CHEST + 2].y) /
            3;
        g_fighters[i].chest_pos.z =
            (SPAD->body_points[i][BODY_CHEST].z +
             SPAD->body_points[i][BODY_CHEST + 1].z +
             SPAD->body_points[i][BODY_CHEST + 2].z) /
            3;
        g_fighters[i].hip_pos.x =
            (SPAD->body_points[i][BODY_HIP].x +
             SPAD->body_points[i][BODY_HIP + 1].x) /
            2;
        g_fighters[i].hip_pos.y =
            (SPAD->body_points[i][BODY_HIP].y +
             SPAD->body_points[i][BODY_HIP + 1].y) /
            2;
        g_fighters[i].hip_pos.z =
            (SPAD->body_points[i][BODY_HIP].z +
             SPAD->body_points[i][BODY_HIP + 1].z) /
            2;
    }

    {
        s16 event_before = first->pending_event;
        if (event_before == MOVE_EVENT_NONE) {
            object_HitFighters();
            if (first->pending_event == event_before) {
                object_DirtHitFighters();
            }
        }
    }
}

/* Which of a fighter's body points a sweep's triangle (in the scratch,
 * with its box) reaches, and of those which cores: into the scratch's
 * body_hits and core_hits. `frame_from_normal`: what hit_FrameTriangle
 * returned, 0 for a triangle too thin for a frame of its own. A lost off
 * arm cannot be hit. */
static void fighter_TestBodyInSweep(
    Fighter *fighter, s32 frame_from_normal, s32 test_cores) {
    WeaponHitScratch *scr = &SPAD->tail.weapon_hit;
    s32 fighter_index = fighter->index;
    BodyPoint *points = g_fighter_body_points[fighter_index];
    BodyPoint *point;
    s32 core_hits = 0;
    s32 hits = 0;
    s32 i;

    for (i = 0, point = points; i < BODY_POINT_COUNT; i++, point++) {
        s32 x;
        s32 y;
        s32 z;
        s32 radius;
        s32 hit;

        if (!fighter->off_arm_ok && i >= BODY_OFF_ARM && i < BODY_SWORD_ARM) {
            continue;
        }

        radius = point->radius;
        /* outside the triangle's box?
         * FAKE: kept in the local the test's result goes in */
        hit = 0;
        x = SPAD->body_points[fighter_index][i].x;
        if (scr->bbox_max.x < x - radius || x + radius < scr->bbox_min.x) {
            hit = 1;
        } else {
            y = SPAD->body_points[fighter_index][i].y;
            if (scr->bbox_max.y < y - radius || y + radius < scr->bbox_min.y) {
                hit = 1;
            } else {
                z = SPAD->body_points[fighter_index][i].z;
                if (scr->bbox_max.z < z - radius ||
                    z + radius < scr->bbox_min.z) {
                    hit = 1;
                }
            }
        }
        if (hit) {
            continue;
        }

        if (frame_from_normal) {
            hit = hit_SphereMeetsTriangle(
                0, scr, (s32 *)&SPAD->body_points[fighter_index][i], radius,
                point->reach_sq);
            if (hit) {
                if (point->has_core && test_cores) {
                    if (hit_SphereMeetsTriangle(
                            1, scr, NULL, point->core_radius,
                            point->core_reach_sq)) {
                        core_hits |= 1 << i;
                    }
                }
            }
        } else {
            hit = hit_SphereMeetsSliver(
                0, scr, &SPAD->body_points[fighter_index][i], radius,
                point->reach_sq);
            if (hit) {
                if (point->has_core && test_cores) {
                    if (hit_SphereMeetsSliver(1, scr, NULL, point->core_radius,
                                              point->core_reach_sq)) {
                        core_hits |= 1 << i;
                    }
                }
            }
        }
        if (hit) {
            hits |= 1 << i;
        }
    }

    scr->body_hits = hits;
    scr->core_hits = core_hits;
}

/* The file's small data. */
/* the debug menu's option: every blow that lands kills */
s32 g_fight_every_hit_kills = 0;
/* the two spheres that keep the fighters' bodies apart: chest, knees */
static s32 g_fight_body_radii[2] = {350, 370};
