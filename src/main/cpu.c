/* The CPU opponent (cpu.h): what it sees, how it gets about, and what it
 * decides to do. The file begins with one function of the scene's. */
#define INCLUDE_ASM_USE_MACRO_INC 1
#include "common.h"
#include "include_asm.h"
#include "bb2.h"

/* Defined further down. */
static CpuProfile g_cpu_profiles[32];
static s32 cpu_Decide(struct Fighter *fighter);
static void cpu_ProbeGround(struct Fighter *fighter);
static s32 cpu_AttackReach(struct Fighter *fighter);
static u8 g_cpu_mirror_walks[4];
static u8 g_cpu_mirror_steps[4];

/* ---- the scene (node.h) ---- */

/* Where the camera is. */
s32 *node_GetCameraPosition(void) { return g_node_camera_root.node.world.t; }

/* ---- the CPU ---- */

/* A roll of the dice against a level or a weight of CPU_LEVEL_MAX. */
#define CPU_ROLL_MASK (CPU_LEVEL_MAX - 1)

/* Chanbara's opponents come in tens, and the CPU is told to forget what it
 * has learned every so many fights of a practice round. */
#define CPU_CHANBARA_TEN 10
#define CPU_PRACTICE_FORGET 5

/* A gun reaches everywhere; a blade reaches its attack's distance and this
 * margin. */
#define CPU_GUN_REACH 30000
#define CPU_REACH_MARGIN 100
/* g_cpu_weapon_ranges is in eighths */
#define CPU_RANGE_UNIT 8
/* What is allowed for the attack the opponent is making, past its reach. */
#define CPU_THREAT_MARGIN 300

/* cpu_ProbeGround: the way is tested this high above the feet; past a wall
 * the ground is looked for 2000 further on (so much of 256 of a sine);
 * down, from this far above the feet to this far below; and a wall
 * further off than CPU_PROBE_WALL_NEAR may be a ledge to climb. */
#define CPU_PROBE_HEIGHT 800
#define CPU_PROBE_PAST_WALL 125
#define CPU_PROBE_FROM_ABOVE 2100
#define CPU_PROBE_TO_BELOW 4100
#define CPU_PROBE_WALL_NEAR 2000

/* The pad's eight directions, as cpu_SteerTo counts them. */
#define CPU_PAD_SECTORS 8

/* cpu_PlanCircling: a place is tested for ground from just above the
 * height of the feet to just below, and this much further out than it is
 * put. */
#define CPU_PLACE_CLEARANCE 5
#define CPU_PLACE_PROBE_MARGIN 800

/* math_IntersectSegments: how far past a segment's ends the crossing may
 * be. */
#define CROSSING_SLACK 50

/* Chanbara's last ten opponents take their sets of attacks from here on in
 * g_cpu_chanbara_attacks, after the two for each earlier ten. */
#define CPU_CHANBARA_LAST_SETS 18

/* Forgets which of the opponent's attacks it has met. */
void cpu_ForgetAttacks(s32 fighter_index) {
    s32 i;
    Fighter *fighter = &g_fighters[fighter_index];
    for (i = 0; i < CPU_KNOWN_ATTACKS; i++) {
        fighter->cpu_known_attacks[i][1] = 0;
        fighter->cpu_known_attacks[i][0] = 0;
    }
}

/* Halves what it remembers of each (between rounds). */
void cpu_FadeAttacks(s32 fighter_index) {
    s32 i;
    Fighter *fighter = &g_fighters[fighter_index];
    for (i = 0; i < CPU_KNOWN_ATTACKS; i++) {
        fighter->cpu_known_attacks[i][1] >>= 1;
    }
}

/* Sets a fighter up to be played by the CPU. Its profile is its
 * character's, and its level the one it was given, except where the mode
 * says otherwise: against the CPU the level rises every five opponents; in
 * practice with the wins, or the opponent is the dummy; chanbara's
 * opponents have profiles of their own and a level that rises along the
 * ladder; a story boss follows its plan for the stage. "Easy" takes about a
 * third off.
 *
 * Then, for the fighter and for its opponent, the reach of each stance is
 * worked out from the attacks in its motion file's list of scripts: the
 * nearest any works from, the farthest, and the farthest of the plain ones
 * (a gun reaches everywhere). */
void cpu_Init(s32 fighter_index, u16 *scripts, u16 *other_scripts) {
    Fighter *fighter = &g_fighters[fighter_index];
    CpuStoryLimits *limits;
    u8 *limits_row;
    u8 base;
    /* a probe, then which of the two fighters */
    s32 index;
    s32 stance;
    Fighter *subject;
    u16 *cursor;
    u16 *list;
    s32 mask_bit_index;
    s32 bit;
    s32 near, far, plain_far;
    /* a level worked out from the fights won; a row of chanbara's
     * limits; then, of an attack, its mask of characters and its
     * distances */
    s32 work;
    u32 script_entry_kind;
    s32 near_reach, far_reach, plain_reach;
    Fighter *other;

    fighter->cpu_profile = fighter->character;
    fighter->cpu_level = fighter->cpu_base_level;
    switch (g_fight_mode) {
    case FIGHT_MODE_VSCPU:
        /* a quarter of the full level, and 3/16 more for every five opponents
         * faced, up to 13/16 */
        fighter->cpu_level =
            (g_vscpu_faced - 1) / 5 * (3 * CPU_LEVEL_MAX / 16) +
            CPU_LEVEL_MAX / 4;
        /* (13/16 at the most) */
        if (fighter->cpu_level > 13 * CPU_LEVEL_MAX / 16) {
            fighter->cpu_level = 13 * CPU_LEVEL_MAX / 16; /* the most */
        }
        break;
    case FIGHT_MODE_STORY:
        if (g_story_henchmen_left == g_story_henchmen_count) {
            cpu_ForgetAttacks(fighter->index);
        }
        if (g_cpu_profiles[fighter->cpu_profile].flags &
            CPU_PROFILE_STORY_BOSS) {
            limits = &g_cpu_story_limits[fighter->stance][g_story_chapter - 1];
            fighter->cpu_known_threshold = limits->known_threshold;
            fighter->cpu_idle_limit = limits->idle_limit;
        }
        break;
    case FIGHT_MODE_PRACTICE:
        if (g_fight_practice_round) {
            work = g_fight_wins_p1 / CPU_PRACTICE_FORGET;
            /* 5/32 of the full level, and 3/32 more for every five wins */
            fighter->cpu_level =
                work * (3 * CPU_LEVEL_MAX / 32) + 5 * CPU_LEVEL_MAX / 32;
            if (fighter->cpu_level > CPU_LEVEL_MAX) {
                fighter->cpu_level = CPU_LEVEL_MAX;
            }
            if (g_fight_wins_p1 % CPU_PRACTICE_FORGET == 0) {
                cpu_ForgetAttacks(fighter->index);
            }
        } else {
            work = g_fight_wins_p1 / 3;
            if (work >= 3) {
                g_fight_wins_p1 = 0;
                work = 0;
            }
            fighter->cpu_profile = CPU_PROFILE_ROW_PRACTICE;
            /* the dummy moves at half speed, then three quarters, then full */
            fighter->base_speed = (work + 2) * (FIGHTER_SPEED_ONE / 4);
            fighter->cpu_level = 0;
            fighter->cpu_known_threshold = 0;
            /* and attacks after 60 frames of standing by, then 45, then 30 */
            fighter->cpu_idle_limit = 60 - work * 15;
        }
        break;
    case FIGHT_MODE_CHANBARA:
        fighter->cpu_profile = g_chanbara_opponents[g_chanbara_count - 1][0] +
                               CPU_PROFILE_ROW_CHANBARA_FIRST;
        base = g_chanbara_count / CPU_CHANBARA_TEN;
        /* 1/32 of the full level, and a little more for every ten opponents;
         * the third and fourth kinds 3/32, and the last kind 1/8 more */
        fighter->cpu_level = base * 16 + CPU_LEVEL_MAX / 32;
        if (g_cpu_profiles[fighter->cpu_profile].flags &
            (CPU_PROFILE_CHANBARA_3 | CPU_PROFILE_CHANBARA_4)) {
            /* (16 a ten, from 3/32) */
            fighter->cpu_level = base * 16 + 3 * CPU_LEVEL_MAX / 32;
        }
        if (g_cpu_profiles[fighter->cpu_profile].flags &
            CPU_PROFILE_CHANBARA_LAST) {
            fighter->cpu_level += CPU_LEVEL_MAX / 8; /* an eighth more */
        }
        if ((g_chanbara_count - 1) % CPU_CHANBARA_TEN == 0) {
            cpu_ForgetAttacks(fighter->index);
        }
        /* two rows of limits for every ten */
        work = g_chanbara_count / CPU_CHANBARA_TEN * 2;
        if (g_chanbara_count % CPU_CHANBARA_TEN == 0) {
            work--;
        }
        limits_row = g_cpu_chanbara_limits[work];
        fighter->cpu_known_threshold = limits_row[0];
        fighter->cpu_idle_limit = limits_row[1];
        break;
    }
    if (cpu_IsEasy() && g_fight_mode != FIGHT_MODE_CHANBARA) {
        /* 11/16 of it on easy */
        fighter->cpu_level = fighter->cpu_level * 11 >> 4;
    }
    if (!(g_cpu_profiles[fighter->cpu_profile].flags & CPU_PROFILE_PLANNED)) {
        /* the count at which it knows an attack: 17 at level 0, down to 1 (the
         * level in sixteenths) */
        fighter->cpu_known_threshold = 17 - (fighter->cpu_level >> 8);
    }
    /* 8 frames of its moves */
    fighter->cpu_action_frames = 8 * FIGHTER_SPEED_ONE / fighter->base_speed;
    /* 16 frames at level 0, down to none */
    fighter->cpu_reaction_delay = 16 - (fighter->cpu_level >> 8);
    if (g_cpu_profiles[fighter->cpu_profile].flags & CPU_PROFILE_STORY_BOSS_1) {
        g_cpu_profiles[fighter->cpu_profile].setback_limit = (rand() & 3) + 1;
    }
    for (index = 0; index < sizeof(fighter->cpu_probes); index++) {
        fighter->cpu_probes[index] = 0;
    }
    fighter->cpu_scripts = scripts;
    for (index = 0; index < 2; index++) {
        if (index) {
            subject = fighter->other;
            cursor = other_scripts;
            list = other_scripts;
            mask_bit_index = subject->character;
        } else {
            subject = fighter;
            cursor = scripts;
            list = scripts;
            mask_bit_index = fighter->cpu_profile;
        }
        /* 450 for each whole size over full size */
        subject->reach_bonus = (subject->size_scale - ONE) * 225 >> 11;
        for (stance = 0; stance < 3; stance++) {
            bit = 1 << mask_bit_index;
            near = 0xFFFF;
            plain_far = 0;
            far = 0;
            if (index == 0) {
                fighter->cpu_stance_scripts[stance] = cursor;
            }
            for (; *cursor != 0; cursor++) {
                u8 *attack = (u8 *)list + *cursor;
                if (attack[CPU_ATTACK_HEAD] == CPU_ATTACK_FOR_SOME) {
                    work = (attack[CPU_ATTACK_HEAD + 4] << 24) |
                           (attack[CPU_ATTACK_HEAD + 3] << 16) |
                           (attack[CPU_ATTACK_HEAD + 2] << 8) |
                           attack[CPU_ATTACK_HEAD + 1];
                    if (!(work & bit)) {
                        continue;
                    }
                }
                /* (a distance of 0 or 0xFF is none) */
                if (attack[CPU_ATTACK_NEAR] != 0 &&
                    attack[CPU_ATTACK_NEAR] != 0xFF) {
                    work = attack[CPU_ATTACK_NEAR];
                    if (work < near) {
                        near = work;
                    }
                }
                if (attack[CPU_ATTACK_FAR] != 0 &&
                    attack[CPU_ATTACK_FAR] != 0xFF) {
                    work = attack[CPU_ATTACK_FAR];
                    if (far < work) {
                        far = work;
                    }
                    script_entry_kind =
                        attack[CPU_ATTACK_FLAGS] & CPU_ATTACK_KIND;
                    /* a plain attack: the low four bits of its class
                     * under 4, and of kind 0, 1 or 7. What those bits and
                     * kinds 1 and 7 are has not been made out. */
                    if (plain_far < work &&
                        (attack[CPU_ATTACK_CLASS] & 0xF) * 4 < 0x10 &&
                        (script_entry_kind < 2 || script_entry_kind == 7)) {
                        plain_far = work;
                    }
                }
            }
            if (subject->weapon_id >= WEAPON_ID_FIRST_GUN) {
                near_reach = 0;
                plain_reach = CPU_GUN_REACH;
                far_reach = CPU_GUN_REACH;
            } else {
                near_reach = far_reach = plain_reach =
                    subject->reach_bonus + CPU_REACH_MARGIN;
                near_reach += near * MOTION_REACH_UNIT;
                far_reach += far * MOTION_REACH_UNIT;
                plain_reach += plain_far * MOTION_REACH_UNIT;
            }
            cursor++;
            subject->attack_min_reach[stance] = near_reach;
            subject->attack_max_reach[stance] = far_reach;
            subject->attack_reach[stance] = plain_reach;
        }
    }
    other = fighter->other;
    fighter->cpu_last_script_stance = -1;
    fighter->cpu_last_script = -1;
    fighter->cpu_incoming_attack = CPU_INCOMING_NONE;
    fighter->cpu_projectile_threat = CPU_PROJECTILE_NONE;
    fighter->cpu_attack_threat = CPU_THREAT_NONE;
    fighter->cpu_script = NULL;
    fighter->cpu_pad.released = 0;
    fighter->cpu_pad.pressed = 0;
    fighter->cpu_pad.held = 0;
    other->stance_class = 0;
    fighter->stance_class = 0;
    other->move_class = 0;
    fighter->move_class = 0;
    other->opponent_angle_abs = 0;
    other->opponent_angle = 0;
    fighter->opponent_angle_abs = 0;
    fighter->opponent_angle = 0;
    fighter->cpu_route.count = 0;
    fighter->cpu_circling = 0;
    fighter->cpu_idle_frames = 0;
    fighter->cpu_setbacks = 0;
    fighter->cpu_threats_met = 0;
    fighter->cpu_attacks_begun = 0;
    fighter->unk_3EE = 0;
    fighter->cpu_range_error_frames = 0;
    fighter->cpu_frames = 0;
    fighter->cpu_mirrors_opponent = 0;
    fighter->cpu_action = 0;
    fighter->cpu_action_timer = 0;
    fighter->unk_3C4 = 0;
    fighter->unk_3C2 = 0;
    fighter->unk_3C1 = 0;
    fighter->unk_3C0 = 0;
    fighter->stance_class = 0;
    fighter->cpu_flags = 0;
    fighter->cpu_pad.unheld = -1;
}

/* A frame of the pad script the CPU is playing: returns the buttons it
 * holds down, or -1 if the script was given up. An attack is given up when
 * it no longer has a chance: the opponent is behind, down, on other
 * ground, or has moved out of the attack's range since it began; a blow at
 * a fallen opponent, when the opponent is up. */
static s32 cpu_StepScript(Fighter *fighter) {
    u8 *script_cursor;
    u8 script_delay_left;
    s32 script_byte;
    s32 pad_button_mask;
    /* from where the attack began to where the opponent is now, and the
     * attack's range */
    s32 dx, dz, range;

    script_delay_left = fighter->cpu_script_wait;
    script_cursor = fighter->cpu_script;
    if (script_delay_left != 0) {
        if (fighter->same_frame == 0) {
            fighter->cpu_script_wait = script_delay_left - 1;
        }
        return fighter->cpu_script_buttons;
    }
    if (fighter->cpu_script_kind == CPU_SCRIPT_KIND_ATTACK &&
        fighter->cpu_profile != CPU_PROFILE_ROW_PASSIVE &&
        (g_cpu_profiles[fighter->cpu_profile].flags &
         CPU_PROFILE_PLANNED_LESSER) == 0) {
        Fighter *other;
        s32 angle_in_range;

        if (fighter->cpu_flags & CPU_OPPONENT_RUNNING) {
            angle_in_range = (fighter->opponent_angle_abs <= ANGLE_TURN / 2);
        } else {
            angle_in_range = (fighter->opponent_angle_abs <= ANGLE_TURN / 4);
        }
        /* (the test of the projectile is for CPU_PROJECTILE_COMING or
         * CPU_PROJECTILE_CLOSE) */
        if (angle_in_range == 0 || (fighter->cpu_flags & CPU_OPPONENT_DOWN) ||
            (fighter->cpu_projectile_threat - 1) < 2U ||
            fighter->cpu_opponent_height != CPU_OPPONENT_LEVEL) {
            fighter->cpu_script = NULL;
            return -1;
        }
        other = fighter->other;
        if (other->move_kind == MOVE_KIND_SURRENDER_ST) {
            fighter->cpu_script = NULL;
            return -1;
        }
        dx = other->world_pos.x - fighter->cpu_script_target_x;
        dz = other->world_pos.z - fighter->cpu_script_target_z;
        range = fighter->cpu_script_range;
        if ((range * range) < ((dx * dx) + (dz * dz))) {
            fighter->cpu_script = NULL;
            return -1;
        }
    } else if (fighter->cpu_script_kind == CPU_SCRIPT_KIND_STRIKE_FALLEN &&
               !(fighter->cpu_flags & CPU_OPPONENT_DOWN)) {
        fighter->cpu_script = NULL;
        return -1;
    }
    while (1) {
        script_byte = *script_cursor;
        script_cursor += 1;
        if (script_byte & CPU_SCRIPT_WAIT) {
            if (script_byte == CPU_SCRIPT_END) {
                fighter->cpu_script = NULL;
                break;
            }
            fighter->cpu_script = script_cursor;
            fighter->cpu_script_wait =
                (script_byte & ~CPU_SCRIPT_WAIT & 0xFF) - 1;
            break;
        }
        pad_button_mask = 1 << (script_byte & CPU_SCRIPT_BUTTON);
        if (script_byte & CPU_SCRIPT_RELEASE) {
            fighter->cpu_script_buttons &= ~pad_button_mask;
        } else {
            fighter->cpu_script_buttons |= pad_button_mask;
        }
    }
    return fighter->cpu_script_buttons;
}

/* Begins a pad script, after `delay` frames. */
static void cpu_StartScript(
    Fighter *fighter, u8 *script, s32 script_kind, s32 delay) {
    fighter->cpu_script = script;
    fighter->cpu_script_kind = script_kind;
    fighter->cpu_script_wait = delay;
    fighter->cpu_script_buttons = 0;
    fighter->cpu_buttons = -1;
}

/* The button that keeps a walk going, for each direction of walk
 * (MOTION_HEAD_CLASS): a bit number of the pad. */
typedef struct {
    u8 bit[4];
} CpuWalkButtons;

static CpuWalkButtons g_cpu_walk_buttons;

/* The CPU's pad for this frame.
 *
 * First what it sees (Fighter.cpu_flags, and the angle to the opponent,
 * which is worked out for one of the two fighters each frame, turn about).
 * Chance comes into it: whether the CPU is eager to attack this frame, alert
 * to a threat, sharp enough to pick the right guard are throws of the dice
 * against its level. It keeps count of how long it has been too far from,
 * or too near, the distance it likes; of whether the opponent is above or
 * below; and of its setbacks.
 *
 * Then threats. An attack by the opponent is noticed when it begins: how
 * near it will come is judged from its reach, and if the CPU has met that
 * attack often enough it knows it, and will guard it. When the attack is
 * over it is counted among the attacks met. Something thrown, or dirt, is
 * a threat while it is on its way.
 *
 * Whether the opponent has left an opening is decided last, and then the
 * buttons: the pad script that is playing, or what cpu_Decide makes of all
 * this. A walk or a run that is under way is kept going while an attack
 * button is down. */
void cpu_BuildPad(s32 fighter_index, PadState *pad_out) {
    Fighter *fighter;
    Fighter *looker;
    Fighter *looked_at;
    PadState pad;
    CpuWalkButtons bits;
    s32 opponent_hit_start_frame, opponent_hit_end_frame;
    /* how far off its preferred distance it is, then which way */
    s32 work;
    /* how much higher the opponent stands; a count of a known attack; a
     * flag; an angle; what the frame's decision gave */
    s32 temp;
    /* a tolerance, the reach of the attack coming, how far off a thrown
     * thing is */
    s32 dist;
    /* the lowest count among the known attacks; a reach and a margin */
    s32 temp3;
    s32 i;

    fighter = &g_fighters[fighter_index];
    fighter->cpu_buttons = 0;
    if (fighter->cpu_frames & 1) {
        looked_at = fighter->other;
        looker = fighter;
    } else {
        looker = fighter->other;
        looked_at = fighter;
    }
    looker->move_class =
        looker->motion_head[MOTION_HEAD_TURN_AND_CLASS] & MOTION_HEAD_CLASS;
    looker->opponent_angle =
        (ratan2(looked_at->world_pos.x - looker->world_pos.x,
                looked_at->world_pos.z - looker->world_pos.z) -
         looker->orientation.vy) &
        ANGLE_MASK;
    if (looker->opponent_angle > ANGLE_TURN / 2) {
        looker->opponent_angle -= ANGLE_TURN;
    }
    looker->opponent_angle_abs =
        looker->opponent_angle < 0 ? -looker->opponent_angle
                                   : looker->opponent_angle;
    if (looker->move_kind == MOVE_KIND_STANCE) {
        looker->stance_class = looker->move_class;
    }

    /* The first term is CPU_EAGER; the shifts make CPU_IMPATIENT, CPU_ALERT,
     * CPU_SHARP, CPU_UNDER_ATTACK, CPU_RUNNING, CPU_OPPONENT_RUNNING, CPU_DOWN
     * and CPU_OPPONENT_DOWN. At the full level it is eager one frame in eight,
     * alert every frame and sharp every other. */
    fighter->cpu_flags =
        (fighter->cpu_flags &
         (CPU_KNOWS_ATTACK | CPU_KNOWS_ATTACK_WELL | CPU_TOLD_TO_CHANGE)) |
        ((rand() & CPU_ROLL_MASK) < (fighter->cpu_level >> 3) &&
         fighter->cpu_frames > CPU_WARMUP_FRAMES) |
        ((g_cpu_profiles[fighter->cpu_profile].flags & CPU_PROFILE_PLANNED)
             ? (fighter->cpu_idle_limit < fighter->cpu_idle_frames) << 2
             : ((rand() & CPU_ROLL_MASK) < (fighter->cpu_level >> 3) &&
                fighter->cpu_frames > CPU_WARMUP_FRAMES)
                   << 2) |
        (((rand() & CPU_ROLL_MASK) < fighter->cpu_level ||
          fighter->cpu_frames < (fighter->cpu_level >> 3))
         << 3) |
        (((rand() & CPU_ROLL_MASK) < (fighter->cpu_level >> 1) ||
          fighter->cpu_frames < (fighter->cpu_level >> 4))
         << 4) |
        ((MOVE_IS_ATTACK(fighter->other->move_kind) ||
          (fighter->move_kind == MOVE_KIND_PAIRED &&
           fighter->cur_move->last_command_frame !=
               fighter->motion_head[MOTION_HEAD_FRAMES] - 1))
         << 7) |
        ((fighter->move_kind == MOVE_KIND_RUN ||
          fighter->move_kind == MOVE_KIND_ARUN ||
          fighter->move_kind == MOVE_KIND_RUN_ST)
         << 8) |
        ((fighter->other->move_kind == MOVE_KIND_RUN ||
          fighter->other->move_kind == MOVE_KIND_ARUN ||
          fighter->other->move_kind == MOVE_KIND_RUN_ST)
         << 9) |
        ((fighter->move_kind == MOVE_KIND_DOWN ||
          fighter->move_kind == MOVE_KIND_END_GAME ||
          fighter->move_kind == MOVE_KIND_DTH)
         << 10) |
        ((fighter->other->move_kind == MOVE_KIND_DOWN ||
          fighter->other->move_kind == MOVE_KIND_END_GAME ||
          fighter->other->move_kind == MOVE_KIND_DTH)
         << 11) |
        (fighter->move_kind == MOVE_KIND_STANCE ? CPU_STANDING : 0) |
        (fighter->other->move_kind == MOVE_KIND_STANCE ? CPU_OPPONENT_STANDING
                                                       : 0) |
        (fighter->move_kind == MOVE_KIND_WALK ? CPU_WALKING : 0) |
        (fighter->other->move_kind == MOVE_KIND_WALK ? CPU_OPPONENT_WALKING
                                                     : 0) |
        (fighter->move_kind == MOVE_KIND_STEP ? CPU_STEPPING : 0);
    fighter->cpu_frames++;

    if (fighter->move_kind != MOVE_KIND_ATTACK &&
        fighter->move_kind != MOVE_KIND_ARUN &&
        fighter->move_kind != MOVE_KIND_HOM_AT &&
        fighter->move_kind != MOVE_KIND_CLIMB_ATTACK &&
        fighter->move_kind != MOVE_KIND_DEFLECTED &&
        fighter->move_kind != MOVE_KIND_DAM &&
        fighter->move_kind != MOVE_KIND_STAGGER) {
        if (fighter->cpu_idle_frames != 0xFF) {
            fighter->cpu_idle_frames++;
        }
    } else {
        fighter->cpu_idle_frames = 0;
    }

    if (((g_cpu_side_step_stances[fighter->other->weapon_id] >>
          fighter->other->stance_class) &
         1) &&
        fighter->other->opponent_angle_abs < (fighter->cpu_level >> 4) &&
        fighter->weapon_id < WEAPON_ID_FIRST_GUN &&
        (fighter->cpu_flags & CPU_OPPONENT_STANDING) &&
        !(g_cpu_profiles[fighter->cpu_profile].flags &
          CPU_PROFILE_PLANNED_LESSER)) {
        fighter->cpu_flags |= CPU_STEPS_ROUND;
    } else {
        fighter->cpu_flags &= ~CPU_STEPS_ROUND;
    }

    fighter->cpu_preferred_distance =
        fighter->attack_min_reach[fighter->stance] +
        (((fighter->attack_reach[fighter->stance] -
           fighter->attack_min_reach[fighter->stance]) *
          g_cpu_profiles[fighter->cpu_profile].range_fraction) >>
         8);
    /* (the tolerance is in units of 25/8) */
    dist = (g_cpu_profiles[fighter->cpu_profile].range_tolerance * 25U) >> 3;
    work = g_fight_distance - fighter->cpu_preferred_distance;
    if (dist < (work >= 0 ? work : -work)) {
        work >>= 31;
        if (work != (fighter->cpu_range_error_frames >> 15)) {
            fighter->cpu_range_error_frames = 0;
        }
        fighter->cpu_range_error_frames += work ? -1 : 1;
    } else {
        fighter->cpu_range_error_frames = 0;
    }

    temp = fighter->other->ground_y - fighter->ground_y;
    if (temp < -CPU_LEVEL_HEIGHT) {
        fighter->cpu_opponent_height = CPU_OPPONENT_ABOVE;
    } else if (temp > CPU_LEVEL_HEIGHT) {
        fighter->cpu_opponent_height = CPU_OPPONENT_BELOW;
    } else {
        fighter->cpu_opponent_height = CPU_OPPONENT_LEVEL;
    }
    if (fighter->cpu_flags &
        (CPU_RUNNING | CPU_DOWN | CPU_STANDING | CPU_WALKING | CPU_STEPPING)) {
        cpu_ProbeGround(fighter);
    }
    if ((fighter->move_frame == 0 &&
         (fighter->move_kind == MOVE_KIND_DAM ||
          fighter->move_kind == MOVE_KIND_DEFLECTED)) ||
        (fighter->other->move_frame == 0 &&
         (fighter->other->move_kind == MOVE_KIND_BLOCK ||
          fighter->other->move_kind == MOVE_KIND_DEFLECTED))) {
        if (fighter->cpu_setbacks != 0xFF) {
            fighter->cpu_setbacks++;
        }
    }

    if (fighter->cpu_flags & CPU_UNDER_ATTACK) {
        opponent_hit_start_frame =
            fighter->other->hit_start_frames[0] != 0xFF
                ? fighter->other->hit_start_frames[0]
                : fighter->other->hit_start_frames[1];
        opponent_hit_end_frame =
            fighter->other->hit_end_frames[0] != 0xFF
                ? fighter->other->hit_end_frames[0]
                : fighter->other->hit_end_frames[1];
    }
    if (!(fighter->cpu_flags & CPU_UNDER_ATTACK) ||
        (fighter->move_kind != MOVE_KIND_PAIRED
             ? (opponent_hit_end_frame < fighter->other->move_frame ||
                opponent_hit_start_frame - fighter->other->move_frame >
                    CPU_THREAT_FRAMES)
             : fighter->cur_move->last_command_frame < fighter->move_frame)) {
        if (fighter->cpu_incoming_attack != CPU_INCOMING_NONE) {
            if (fighter->cpu_known_threshold != 0 &&
                (cpu_IsEasy() == 0 || g_fight_mode == FIGHT_MODE_CHANBARA)) {
                s32 lowest_count_tally_index;
                s32 matching_tally_index;

                temp3 = 256; /* more than any count */
                i = 0;
                lowest_count_tally_index = -1;
                matching_tally_index = -1;

                for (; i < CPU_KNOWN_ATTACKS; i++) {
                    if (fighter->cpu_known_attacks[i][0] ==
                        fighter->cpu_incoming_attack) {
                        matching_tally_index = i;
                    } else {
                        temp = fighter->cpu_known_attacks[i][1] - 1;
                        if (fighter->cpu_known_attacks[i][1] < temp3) {
                            lowest_count_tally_index = i;
                            temp3 = fighter->cpu_known_attacks[i][1];
                        }
                        if (temp < 0) {
                            temp = 0;
                        }
                        fighter->cpu_known_attacks[i][1] = temp;
                    }
                }
                /* an attack met counts 4, a paired move 8; the others it knows
                 * have each lost 1 */
                temp = fighter->move_kind == MOVE_KIND_PAIRED ? 8 : 4;
                if (matching_tally_index == -1) {
                    fighter->cpu_known_attacks[lowest_count_tally_index][0] =
                        fighter->cpu_incoming_attack;
                    fighter->cpu_known_attacks[lowest_count_tally_index][1] =
                        temp;
                } else {
                    temp += fighter->cpu_known_attacks[matching_tally_index][1];
                    if (temp > 0xFF) {
                        temp = 0xFF;
                    }
                    fighter->cpu_known_attacks[matching_tally_index][1] = temp;
                }
            }
            fighter->cpu_incoming_attack = CPU_INCOMING_NONE;
            fighter->cpu_attack_threat = CPU_THREAT_NONE;
            fighter->cpu_flags &= ~(CPU_KNOWS_ATTACK | CPU_KNOWS_ATTACK_WELL);
        }
    } else {
        if (fighter->cpu_incoming_attack != fighter->other->motion_index) {
            if (fighter->move_kind == MOVE_KIND_PAIRED) {
                fighter->cpu_incoming_attack = CPU_INCOMING_PAIRED;
            } else {
                fighter->cpu_incoming_attack =
                    (fighter->other->prev_move_kind != MOVE_KIND_DEFLECT &&
                     fighter->other->prev_move_kind != MOVE_KIND_DEFLECTED)
                        ? fighter->other->motion_index
                        : CPU_INCOMING_AFTER_PARRY;
                fighter->cpu_threat_hit_frame = opponent_hit_start_frame;
                dist = cpu_AttackReach(fighter);
                /* within 800 of its reach, a step back gets clear */
                temp3 = dist + 800;
                if (fighter->cpu_opponent_height != CPU_OPPONENT_LEVEL ||
                    ((fighter->cpu_flags & CPU_RUNNING) &&
                     fighter->opponent_angle_abs > ANGLE_TURN / 4)) {
                    if (dist / 2 >= g_fight_distance) {
                        fighter->cpu_attack_threat = CPU_THREAT_NEAR;
                    } else if (dist >= g_fight_distance) {
                        fighter->cpu_attack_threat = CPU_THREAT_MID;
                    } else {
                        fighter->cpu_attack_threat = CPU_THREAT_FAR;
                    }
                } else {
                    if (dist >= g_fight_distance) {
                        fighter->cpu_attack_threat = CPU_THREAT_NEAR;
                    } else if (temp3 >= g_fight_distance) {
                        fighter->cpu_attack_threat = CPU_THREAT_MID;
                    } else {
                        fighter->cpu_attack_threat = CPU_THREAT_FAR;
                    }
                }
                fighter->cpu_threat_x = fighter->other->world_pos.x;
                fighter->cpu_threat_z = fighter->other->world_pos.z;
                fighter->cpu_threat_range = temp3;
            }
            for (i = 0; i < CPU_KNOWN_ATTACKS; i++) {
                if (fighter->cpu_known_attacks[i][0] ==
                        fighter->cpu_incoming_attack &&
                    fighter->cpu_known_attacks[i][1] != 0 &&
                    fighter->cpu_known_attacks[i][1] >=
                        fighter->cpu_known_threshold) {
                    temp = CPU_KNOWS_ATTACK;
                    if (fighter->cpu_known_attacks[i][1] >=
                        fighter->cpu_known_threshold * 4) {
                        temp = CPU_KNOWS_ATTACK | CPU_KNOWS_ATTACK_WELL;
                    }
                    fighter->cpu_flags |= temp;
                    break;
                }
            }
        }
        if (fighter->cpu_flags & CPU_KNOWS_ATTACK) {
            fighter->cpu_flags |= CPU_ALERT | CPU_SHARP;
        }
    }

    if (fighter->other->move_kind == MOVE_KIND_DIRT &&
        fighter->other->opponent_angle_abs < ANGLE_TURN / 32 &&
        g_fight_distance < CPU_DIRT_RANGE) {
        fighter->cpu_projectile_threat = CPU_PROJECTILE_COMING;
    } else {
        fighter->cpu_projectile_threat = CPU_PROJECTILE_NONE;
        for (i = 0; i < OBJECT_COUNT; i++) {
            LooseObject *pool_object = &g_object_pool[i];

            dist = SquareRoot0((fighter->world_pos.x - pool_object->pos.x) *
                                   (fighter->world_pos.x - pool_object->pos.x) +
                               (fighter->world_pos.z - pool_object->pos.z) *
                                   (fighter->world_pos.z - pool_object->pos.z));
            if (pool_object->kind != OBJECT_NONE &&
                pool_object->can_hit_fighter != 0 &&
                pool_object->owner != fighter->index) {
                temp = (ratan2(fighter->world_pos.x - pool_object->pos.x,
                               fighter->world_pos.z - pool_object->pos.z) -
                        ratan2(pool_object->pos.x - pool_object->prev_pos.x,
                               pool_object->pos.z - pool_object->prev_pos.z)) &
                       ANGLE_MASK;
                if (temp > ANGLE_TURN / 2) {
                    temp -= ANGLE_TURN;
                }
                /* coming straight at it, and within 2000 of its height: close
                 * when it is within 3000 */
                if ((temp < 0 ? -temp : temp) < ANGLE_TURN / 32) {
                    /* and within 2000 of its height */
                    if ((fighter->foot_pos.vy - pool_object->pos.y >= 0)
                            ? (fighter->foot_pos.vy - pool_object->pos.y < 2000)
                            : (pool_object->pos.y - fighter->foot_pos.vy <
                               2000)) {
                        /* close within 3000 */
                        fighter->cpu_projectile_threat =
                            dist < 3000 ? CPU_PROJECTILE_CLOSE
                                        : CPU_PROJECTILE_COMING;
                    }
                }
            }
        }
    }

    if ((fighter->cpu_flags & CPU_EAGER) &&
        !(g_cpu_profiles[fighter->cpu_profile].flags & CPU_PROFILE_PLANNED) &&
        fighter->cpu_attack_threat != CPU_THREAT_NEAR &&
        fighter->cpu_attack_threat != CPU_THREAT_MID &&
        fighter->cpu_projectile_threat != CPU_PROJECTILE_COMING &&
        fighter->cpu_projectile_threat != CPU_PROJECTILE_CLOSE &&
        (((fighter->cpu_flags & CPU_UNDER_ATTACK) &&
          ((fighter->other->queued_move == 0 &&
            opponent_hit_end_frame < fighter->other->move_frame) ||
           opponent_hit_start_frame - fighter->other->move_frame >
               CPU_THREAT_FRAMES)) ||
         fighter->other->move_kind == MOVE_KIND_RELOAD ||
         fighter->other->move_kind == MOVE_KIND_DAM ||
         fighter->other->move_kind == MOVE_KIND_STAGGER ||
         fighter->other->move_kind == MOVE_KIND_DEFLECTED ||
         fighter->other->move_kind == MOVE_KIND_MOVE ||
         (!(g_cpu_profiles[fighter->cpu_profile].flags &
            CPU_PROFILE_OPPORTUNIST) &&
          (((fighter->cpu_flags & CPU_UNDER_ATTACK) &&
            fighter->cpu_attack_threat == CPU_THREAT_FAR) ||
           (fighter->other->move_kind == MOVE_KIND_CROUCH &&
            fighter->other->off_arm_ok == 0) ||
           (fighter->other->move_kind == MOVE_KIND_DIRT &&
            (!SURFACE_IN(fighter->other->surface, SURFACES_LOOSE) ||
             fighter->other->off_arm_ok == 0)) ||
           (fighter->other->move_kind == MOVE_KIND_SUBWEP &&
            ((fighter->other->sub_weapon_count == 0 &&
              fighter->cpu_projectile_threat != CPU_PROJECTILE_COMING &&
              fighter->cpu_projectile_threat != CPU_PROJECTILE_CLOSE) ||
             fighter->other->off_arm_ok == 0)))) ||
         ((g_cpu_profiles[fighter->cpu_profile].flags &
           CPU_PROFILE_OPPORTUNIST) &&
          (fighter->other->move_kind == MOVE_KIND_STANCE_CHANGE ||
           fighter->other->move_kind == MOVE_KIND_GET_UP ||
           fighter->other->move_kind == MOVE_KIND_SJUMP ||
           fighter->other->move_kind == MOVE_KIND_MJUMP ||
           fighter->other->move_kind == MOVE_KIND_DROP ||
           (fighter->other->move_kind == MOVE_KIND_CLIMB_ST &&
            fighter->cpu_opponent_height == CPU_OPPONENT_LEVEL) ||
           fighter->other->opponent_angle_abs > ANGLE_TURN / 4)))) {
        fighter->cpu_flags |= CPU_SEES_OPENING;
    } else {
        fighter->cpu_flags &= ~CPU_SEES_OPENING;
    }

    if (g_cpu_profiles[fighter->cpu_profile].flags & CPU_PROFILE_DUMMY) {
        fighter->cpu_flags &=
            ~(CPU_ALERT | CPU_SHARP | CPU_KNOWS_ATTACK | CPU_KNOWS_ATTACK_WELL);
        if (fighter->move_kind != MOVE_KIND_STANCE_CHANGE &&
            (g_pad_state.held & PADselect)) {
            fighter->cpu_flags |= CPU_TOLD_TO_CHANGE;
            fighter->cpu_attacks_begun = 0;
        }
    }
    if (fighter->weapon_id >= WEAPON_ID_FIRST_GUN) {
        fighter->cpu_flags |=
            CPU_ALERT | CPU_SHARP | CPU_KNOWS_ATTACK | CPU_KNOWS_ATTACK_WELL;
    }

    i = 0;
    do {
        if (fighter->cpu_script != NULL) {
            temp = cpu_StepScript(fighter);
        } else {
            temp = cpu_Decide(fighter);
        }
        i++;
    } while (temp == -1 && i < 4);
    if (temp != -1) {
        pad.held = temp;
    } else {
        pad.held = 0;
    }
    /* an attack button (or one of the two pseudo-buttons) with a walk or a
     * run under way: keep it going */
    if (pad.held & (FIGHTER_PAD_FRONTAL_ATTACK | FIGHTER_PAD_REVERSE_ATTACK |
                    (1 << MOVE_COMMAND_BUTTON_COUNTER) |
                    (1 << MOVE_COMMAND_BUTTON_OFF_ARM))) {
        /* FAKE: u8 bits[4] = {0xd, 0xf, 0xc, 0xe}, a local's initialiser */
        bits = g_cpu_walk_buttons;
        if (fighter->move_kind == MOVE_KIND_WALK) {
            pad.held |= 1 << bits.bit[fighter->move_class];
        } else if (fighter->move_kind == MOVE_KIND_RUN) {
            pad.held |= PADL1;
        }
    }
    pad.held &= 0xFFFF;
    pad.pressed = pad.held & ~fighter->cpu_pad.held;
    pad.unheld = ~pad.held & 0xFFFF;
    pad.released = ~pad.held & fighter->cpu_pad.held;
    pad.type[fighter_index] = PAD_TYPE_DIGITAL;
    fighter->cpu_pad = pad;
    *pad_out = fighter->cpu_pad;
}

/* Feels out the ground round the fighter, two of the eight probes a
 * frame (g_cpu_probe_table): is the way that far clear, and is there
 * ground at the end of it, level with the fighter or lower. The directions
 * are from the line to the middle of the fight, or, running or down, from
 * the way the fighter faces. */
static void cpu_ProbeGround(Fighter *fighter) {
    s32 segment_start[4];
    s32 segment_end[4];
    s32 horizontal_hit_point[4];
    s32 vertical_hit_point[4];
    s16 work[4];
    s32 first_probe_index;
    s32 probe_index;

    first_probe_index = (fighter->cpu_frames & 3) * 2;
    for (probe_index = first_probe_index; probe_index < first_probe_index + 2;
         probe_index++) {
        Fighter *probe_origin_fighter;
        /* the probe's angle, then what the two casts found */
        s32 result;
        s32 probe_distance;
        s16 *probe_sine_ptr;
        s16 *probe_cosine_ptr;
        s32 probe_x;
        s32 probe_z;

        probe_origin_fighter = fighter;
        result = g_cpu_probe_table[probe_index][1] * (ANGLE_TURN / 16);
        if ((result & (CPU_PROBE_FROM_OPPONENT * (ANGLE_TURN / 16))) != 0) {
            probe_origin_fighter = fighter->other;
        }

        if (fighter->move_kind == MOVE_KIND_RUN ||
            fighter->move_kind == MOVE_KIND_DOWN) {
            result += probe_origin_fighter->orientation.vy;
        } else {
            result += ratan2(
                g_camera_main.focus.x - probe_origin_fighter->world_pos.x,
                g_camera_main.focus.z - probe_origin_fighter->world_pos.z);
        }

        probe_sine_ptr = &g_math_sin_table[result & ANGLE_MASK];
        probe_distance = g_cpu_probe_table[probe_index][0] << 8;
        probe_x = probe_origin_fighter->foot_pos.vx +
                  ((probe_distance * *probe_sine_ptr) >> 12);
        probe_cosine_ptr =
            &g_math_sin_table[(result + ANGLE_TURN / 4) & ANGLE_MASK];
        probe_z = probe_origin_fighter->foot_pos.vz +
                  ((probe_distance * *probe_cosine_ptr) >> 12);
        /* is the way clear, CPU_PROBE_HEIGHT above the ground */
        segment_start[0] = probe_origin_fighter->foot_pos.vx;
        segment_start[1] = probe_origin_fighter->foot_pos.vy - CPU_PROBE_HEIGHT;
        segment_start[2] = probe_origin_fighter->foot_pos.vz;
        segment_end[0] = probe_x;
        segment_end[1] = probe_origin_fighter->foot_pos.vy - CPU_PROBE_HEIGHT;
        segment_end[2] = probe_z;

        result = collision_CastWith(
            segment_start, segment_end, horizontal_hit_point, work,
            FIGHT_CAST_WORK);
        if (result != 0) {
            /* blocked: look for ground 2000 further on */
            probe_x += (*probe_sine_ptr * CPU_PROBE_PAST_WALL) >> 8;
            probe_z += (*probe_cosine_ptr * CPU_PROBE_PAST_WALL) >> 8;
        }

        /* is there ground at the end of it: straight down, from above the
         * fighter's feet to below them */
        segment_start[0] = probe_x;
        segment_start[1] =
            probe_origin_fighter->foot_pos.vy - CPU_PROBE_FROM_ABOVE;
        segment_start[2] = probe_z;
        segment_end[0] = probe_x;
        segment_end[1] = probe_origin_fighter->foot_pos.vy + CPU_PROBE_TO_BELOW;
        segment_end[2] = probe_z;

        result |= collision_CastWith(segment_start, segment_end,
                                     vertical_hit_point, work, FIGHT_CAST_WORK)
                  << 1;
        result += 1;
        /* a drop of less than 5 is level ground */
        if (result == CPU_FOUND_DROP &&
            vertical_hit_point[1] - probe_origin_fighter->foot_pos.vy < 5) {
            result = CPU_FOUND_LEVEL;
        } else if (result == CPU_FOUND_BLOCKED) {
            s32 horizontal_hit_dx =
                horizontal_hit_point[0] - probe_origin_fighter->foot_pos.vx;
            s32 horizontal_hit_dz =
                horizontal_hit_point[2] - probe_origin_fighter->foot_pos.vz;
            if (CPU_PROBE_WALL_NEAR * CPU_PROBE_WALL_NEAR <
                horizontal_hit_dx * horizontal_hit_dx +
                    horizontal_hit_dz * horizontal_hit_dz) {
                s32 y = probe_origin_fighter->foot_pos.vy;
                if ((y - vertical_hit_point[1] >= 0
                         ? y - vertical_hit_point[1]
                         : vertical_hit_point[1] - y) > CPU_LEVEL_HEIGHT) {
                    result = CPU_FOUND_BLOCKED_STEP;
                }
            }
        }
        fighter->cpu_probes[probe_index] = result;
    }
}

/* How far the attack the opponent is making reaches: the motion's own
 * reach, the weapon's, and what the opponent's size adds. */
static s32 cpu_AttackReach(Fighter *fighter) {
    Fighter *other = fighter->other;
    s32 base = other->motion_head[MOTION_HEAD_REACH] * MOTION_REACH_UNIT;

    /* (0xFF: the main weapon does not hit in this move; the off hand's does)
     * Which table goes by the CPU's own move, where the opponent's would be
     * expected: the original's behaviour. */
#if RB_PORT
    /* a mod's weapon's reach and range (port_weapons.h): the game's own
     * with none */
    if (other->hit_end_frames[0] != 0xFF) {
        s32 reach =
            fighter->move_is_common == 0
                ? port_WeaponCpuReach(
                      other->index, other->weapon_id,
                      g_cpu_weapon_reaches[other->weapon_id] * 2)
                : port_WeaponCpuRange(
                      other->index, other->weapon_id,
                      g_cpu_weapon_ranges[other->weapon_id] * CPU_RANGE_UNIT);

        base += reach;
    } else {
        base += g_cpu_sub_weapon_reaches[other->sub_weapon_kind] * 2;
    }
#else
    base += other->hit_end_frames[0] != 0xFF
                ? (fighter->move_is_common == 0
                       ? g_cpu_weapon_reaches[other->weapon_id] * 2
                       : g_cpu_weapon_ranges[other->weapon_id] * CPU_RANGE_UNIT)
                : g_cpu_sub_weapon_reaches[other->sub_weapon_kind] * 2;
#endif
    return base + fighter->other->reach_bonus + CPU_THREAT_MARGIN;
}

/* The directions to hold in order to walk to a place: the pad's direction
 * nearest the way there (the pad is turned towards the middle of the
 * fight), a diagonal traded for one of its neighbours every sixteen frames
 * so as not to walk straight at it; with L1 to run when it is far, and R1
 * to climb when there is a ledge in the way. */
static s32 cpu_SteerTo(
    Fighter *fighter, s32 route_target_x, s32 route_target_z, s32 is_far) {
    s32 route_sector;
    s32 focus_bearing;
    s32 angle_sector;
    s32 sector_index;
    /* the angle from the line to the middle of the fight to the way there,
     * then the buttons */
    s32 buttons;

    focus_bearing = ratan2(g_camera_main.focus.x - fighter->world_pos.x,
                           g_camera_main.focus.z - fighter->world_pos.z);
    buttons = focus_bearing - ratan2(route_target_x - fighter->world_pos.x,
                                     route_target_z - fighter->world_pos.z);
    /* which eighth of a turn, each centred on a direction of the pad */
    buttons -= ANGLE_TURN / 16;
    angle_sector = buttons >> 9;
    sector_index = angle_sector & (CPU_PAD_SECTORS - 1);
    route_sector = sector_index;
    if ((is_far == 0) && !(angle_sector & 1)) {
        /* one way for sixteen frames, the other for the next sixteen */
        if (fighter->cpu_frames & 0x10) {
            route_sector = sector_index + 1;
            if (route_sector >= CPU_PAD_SECTORS) {
                route_sector = 0;
            }
        } else {
            route_sector = sector_index - 1;
            if (route_sector < 0) {
                route_sector = CPU_PAD_SECTORS - 1;
            }
        }
    }
    buttons = g_cpu_heading_buttons[route_sector & (CPU_PAD_SECTORS - 1)];
    if (is_far == 1) {
        buttons |= PADL1;
    }
    if (fighter_FindLedge(fighter, &route_sector) != 0) {
        buttons |= PADR1;
    }
    return buttons;
}

/* A point with a fourth word, as the casts take them. */
typedef struct {
    s32 x, y, z, w;
} CastPoint;

/* Plans a way round the opponent, to come at it from another side: places
 * on a circle about it, an eighth of a turn apart, going both ways for as
 * long as each has ground under it and can be walked to from the last. The
 * way that gets further (either, if they tie) becomes the route. Returns
 * how many places. */
static s32 cpu_PlanCircling(Fighter *fighter) {
    CastPoint candidate_point;
    /* the place just below the height of the feet (candidate_point is just
     * above) */
    CastPoint low;
    CastPoint plus_side_point;
    CastPoint minus_side_point;
    s32 hit_point[4];
    s16 work[4];
    s32 route_node_count;
    s8 plus_count;
    /* how many places were found going round each way (plus_count the one way,
     * minus_count the other); then plus_count is the count of the way taken and
     * minus_count says which */
    s8 minus_count;
    u8 plus_side_active;
    u8 minus_side_active;
    s32 orbit_angle_offset;
    s32 probe_radius;
    s32 angle;
    Fighter *opponent;
    s32 probe_dx_scaled;
    s32 probe_dz_scaled;
    s32 probe_x;
    s32 probe_z;

    minus_count = 0;
    plus_count = 0;
    minus_side_active = 1;
    plus_side_active = 1;
    route_node_count = 0;
    plus_side_point.x = fighter->foot_pos.vx;
    plus_side_point.y = fighter->foot_pos.vy - CPU_PLACE_CLEARANCE;
    probe_radius = g_fight_distance + CPU_PLACE_PROBE_MARGIN;
    plus_side_point.z = fighter->foot_pos.vz;
    minus_side_point = plus_side_point;
    for (orbit_angle_offset = ANGLE_TURN / 8;
         orbit_angle_offset <= ANGLE_TURN / 2;
         orbit_angle_offset += ANGLE_TURN / 8) {
        if (plus_side_active) {
            opponent = fighter->other;
            angle = opponent->opponent_bearing + orbit_angle_offset;
            plus_side_active = 0;
            probe_dx_scaled =
                probe_radius * g_math_sin_table[angle & ANGLE_MASK];
            probe_dz_scaled =
                probe_radius *
                g_math_sin_table[(angle + ANGLE_TURN / 4) & ANGLE_MASK];
            probe_x = opponent->foot_pos.vx + (probe_dx_scaled >> 12);
            probe_z = opponent->foot_pos.vz + (probe_dz_scaled >> 12);
            candidate_point.x = probe_x;
            candidate_point.y = fighter->foot_pos.vy - CPU_PLACE_CLEARANCE;
            candidate_point.z = probe_z;
            low.x = probe_x;
            low.y = fighter->foot_pos.vy + CPU_PLACE_CLEARANCE;
            low.z = probe_z;
            if (collision_CastWith(&candidate_point.x, &low.x, hit_point, work,
                                   FIGHT_CAST_WORK) != 0) {
                plus_side_active =
                    collision_CastWith(&plus_side_point.x, &candidate_point.x,
                                       hit_point, work, FIGHT_CAST_WORK) == 0;
            }
            if (plus_side_active) {
                plus_side_point = candidate_point;
                plus_count++;
            }
        }
        if (minus_side_active) {
            opponent = fighter->other;
            angle = opponent->opponent_bearing - orbit_angle_offset;
            minus_side_active = 0;
            probe_dx_scaled =
                probe_radius * g_math_sin_table[angle & ANGLE_MASK];
            probe_dz_scaled =
                probe_radius *
                g_math_sin_table[(angle + ANGLE_TURN / 4) & ANGLE_MASK];
            probe_x = opponent->foot_pos.vx + (probe_dx_scaled >> 12);
            probe_z = opponent->foot_pos.vz + (probe_dz_scaled >> 12);
            candidate_point.x = probe_x;
            candidate_point.y = fighter->foot_pos.vy - CPU_PLACE_CLEARANCE;
            candidate_point.z = probe_z;
            low.x = probe_x;
            low.y = fighter->foot_pos.vy + CPU_PLACE_CLEARANCE;
            low.z = probe_z;
            if (collision_CastWith(&candidate_point.x, &low.x, hit_point, work,
                                   FIGHT_CAST_WORK) != 0) {
                minus_side_active =
                    collision_CastWith(&minus_side_point.x, &candidate_point.x,
                                       hit_point, work, FIGHT_CAST_WORK) == 0;
            }
            if (minus_side_active) {
                minus_side_point = candidate_point;
                minus_count++;
            }
        }
    }
    if (plus_count != 0 || minus_count != 0) {
        if (plus_count == minus_count) {
            if (rand() & 1) {
                plus_count = 0;
            } else {
                minus_count = 0;
            }
        }
        if (plus_count < minus_count) {
            plus_count = minus_count;
            minus_count = 0;
        } else {
            minus_count = 1;
        }
        route_node_count = plus_count--;
        for (orbit_angle_offset = ANGLE_TURN / 8; plus_count >= 0; plus_count--,
            orbit_angle_offset += ANGLE_TURN / 8) {
            s32 base = fighter->other->opponent_bearing;
            if (minus_count != 0) {
                angle = base + orbit_angle_offset;
            } else {
                angle = base - orbit_angle_offset;
            }
            fighter->cpu_route.node[plus_count].x =
                fighter->other->foot_pos.vx +
                ((g_fight_distance * g_math_sin_table[angle & ANGLE_MASK]) >>
                 12);
            fighter->cpu_route.node[plus_count].z =
                fighter->other->foot_pos.vz +
                ((g_fight_distance *
                  g_math_sin_table[(angle + ANGLE_TURN / 4) & ANGLE_MASK]) >>
                 12);
            fighter->cpu_route.node[plus_count].kind = CPU_GOAL_PLACE;
        }
        fighter->cpu_action_timer = 0;
        fighter->cpu_goal_x = fighter->cpu_route.node[0].x;
        fighter->cpu_goal_z = fighter->cpu_route.node[0].z;
        fighter->cpu_goal_kind = fighter->cpu_route.node[0].kind;
    }
    return route_node_count;
}

/* Where two segments cross, in x and z, if they do. Works on the
 * coordinates divided by 8, and lets the crossing lie CROSSING_SLACK of
 * those past a segment's ends. */
static s32 math_IntersectSegments(
    s32 first_start_x, s32 first_start_z, s32 first_end_x, s32 first_end_z,
    s32 second_start_x, s32 second_start_z, s32 second_end_x, s32 second_end_z,
    s32 *intersection_x_out, s32 *intersection_z_out) {
    s32 first_slope;
    s32 x1;
    s32 first_dx;
    s32 first_dz;
    s32 second_dx;
    s32 second_dz;
    s32 second_slope;
    s32 first_intercept;
    s32 intersection_x;
    s32 intersection_z;

    x1 = first_start_x;
    x1 >>= 3;
    first_end_x >>= 3;
    first_start_z >>= 3;
    first_end_z >>= 3;
    first_dx = first_end_x - x1;
    first_dz = first_end_z - first_start_z;
    second_start_x >>= 3;
    second_end_x >>= 3;
    second_start_z >>= 3;
    second_end_z >>= 3;
    second_dx = second_end_x - second_start_x;
    second_dz = second_end_z - second_start_z;
    if ((second_start_x == second_end_x) && (first_start_z == first_end_z)) {
        *intersection_x_out = second_end_x;
        *intersection_z_out = first_end_z;
    } else if ((x1 == first_end_x) && (second_start_z == second_end_z)) {
        *intersection_x_out = first_end_x;
        *intersection_z_out = second_end_z;
    } else if ((second_start_x == second_end_x) && (x1 != first_end_x)) {
        *intersection_x_out = second_end_x;
        *intersection_z_out =
            first_start_z + (first_dz * (second_end_x - x1)) / first_dx;
    } else if ((x1 == first_end_x) && (second_start_x != second_end_x)) {
        *intersection_x_out = x1;
        *intersection_z_out =
            second_start_z + (second_dz * (x1 - second_start_x)) / second_dx;
    } else if (
        (first_start_z == first_end_z) && (second_start_z != second_end_z)) {
        *intersection_z_out = first_end_z;
        *intersection_x_out =
            second_start_x +
            (second_dx * (first_end_z - second_start_z)) / second_dz;
    } else if (
        (second_start_z == second_end_z) && (first_start_z != first_end_z)) {
        *intersection_z_out = second_start_z;
        *intersection_x_out =
            x1 + (first_dx * (second_start_z - first_start_z)) / first_dz;
    } else {
        if (first_end_x == x1) {
            return 0;
        }
        if (second_end_x == second_start_x) {
            return 0;
        }
        first_slope = (first_dz << 7) / first_dx;
        second_slope = (second_dz << 7) / second_dx;
        if (first_slope == second_slope) {
            return 0;
        }
        first_intercept =
            (((first_start_z * first_end_x) - (first_end_z * x1)) << 7) /
            first_dx;
        intersection_x =
            (((((second_start_z * second_end_x) -
                (second_end_z * second_start_x))
               << 7) /
              second_dx) -
             first_intercept) /
            (first_slope - second_slope);
        *intersection_x_out = intersection_x;
        *intersection_z_out =
            ((intersection_x * first_slope) + first_intercept) >> 7;
    }
    if (!((((*intersection_x_out - x1) >= -CROSSING_SLACK) ||
           ((*intersection_x_out - first_end_x) >= -CROSSING_SLACK)) &&
          (((x1 - *intersection_x_out) >= -CROSSING_SLACK) ||
           ((first_end_x - *intersection_x_out) >= -CROSSING_SLACK)) &&
          ((intersection_z = *intersection_z_out,
            ((first_start_z - intersection_z) >= -CROSSING_SLACK)) ||
           ((first_end_z - intersection_z) >= -CROSSING_SLACK)) &&
          (((intersection_z - first_start_z) >= -CROSSING_SLACK) ||
           ((intersection_z - first_end_z) >= -CROSSING_SLACK)) &&
          (((*intersection_x_out - second_start_x) >= -CROSSING_SLACK) ||
           ((*intersection_x_out - second_end_x) >= -CROSSING_SLACK)) &&
          (((second_start_x - *intersection_x_out) >= -CROSSING_SLACK) ||
           ((second_end_x - *intersection_x_out) >= -CROSSING_SLACK)) &&
          (((second_start_z - intersection_z) >= -CROSSING_SLACK) ||
           ((second_end_z - intersection_z) >= -CROSSING_SLACK)) &&
          (((intersection_z - second_start_z) >= -CROSSING_SLACK) ||
           ((intersection_z - second_end_z) >= -CROSSING_SLACK)))) {
        return 0;
    }
    *intersection_x_out = *intersection_x_out << 3;
    *intersection_z_out = *intersection_z_out << 3;
    return 1;
}

/* Whether one of the stage's obstacles lies between the fighter and a
 * place: returns how far off the nearest crossing is (CPU_NO_OBSTACLE if
 * there is none) and leaves which edge of which chain in the route. */
static s32 cpu_FindObstacle(
    Fighter *fighter, NavSet *nav_set, s32 target_x, s32 target_z) {
    s32 crossing_x;
    s32 crossing_z;
    s32 nearest_crossing_distance;
    s16 chain_index;
    s16 edge_vertex_index;
    s16 next_vertex_index;
    s16 edge_count;
    NavChain *nav_chain;
    s32 dx;
    s32 dz;
    s32 crossing_distance;

    nearest_crossing_distance = CPU_NO_OBSTACLE;
    for (chain_index = 0; chain_index < nav_set->nchains; chain_index++) {
        nav_chain = &nav_set->chains[chain_index];
        edge_count = nav_chain->nvtx;
        if (nav_chain->flags & NAV_CHAIN_OPEN) {
            edge_count = nav_chain->nvtx - 1;
        }
        for (edge_vertex_index = 0; edge_vertex_index < edge_count;
             edge_vertex_index++) {
            next_vertex_index = edge_vertex_index + 1;
            if (!(next_vertex_index < nav_chain->nvtx)) {
                next_vertex_index = 0;
            }
            if (math_IntersectSegments(
                    fighter->world_pos.x, fighter->world_pos.z, target_x,
                    target_z, nav_chain->vtx[edge_vertex_index][0],
                    nav_chain->vtx[edge_vertex_index][1],
                    nav_chain->vtx[next_vertex_index][0],
                    nav_chain->vtx[next_vertex_index][1], &crossing_x,
                    &crossing_z) != 0) {
                dx = crossing_x - fighter->world_pos.x;
                dz = crossing_z - fighter->world_pos.z;
                crossing_distance = SquareRoot0(dx * dx + dz * dz);
                if (crossing_distance < nearest_crossing_distance) {
                    nearest_crossing_distance = crossing_distance;
                    fighter->cpu_route.poly = chain_index;
                    fighter->cpu_route.vtx = edge_vertex_index;
                }
            }
        }
    }
    return nearest_crossing_distance;
}

/* The point to walk to in order to get round a corner of an obstacle:
 * out from the corner, along the line that halves its angle, by the
 * chain's margin. */
static void cpu_GetCornerPoint(NavChain *nav_chain, s32 vertex_index,
                               s16 *corner_x_out, s16 *corner_z_out) {
    s16 prev;
    s16 next;
    s16 vertex_x, vertex_z;
    s32 angle_prev, angle_next, scale;
    s16 angle_mid;

    prev = vertex_index - 1;
    vertex_x = nav_chain->vtx[vertex_index][0];
    vertex_z = nav_chain->vtx[vertex_index][1];
    if (prev < 0) {
        prev = nav_chain->nvtx - 1;
    }
    next = vertex_index + 1;
    if (next >= nav_chain->nvtx) {
        next = 0;
    }
    angle_prev = ratan2(nav_chain->vtx[prev][0] - vertex_x,
                        nav_chain->vtx[prev][1] - vertex_z) &
                 ANGLE_MASK;
    angle_next = ratan2(nav_chain->vtx[next][0] - vertex_x,
                        nav_chain->vtx[next][1] - vertex_z) &
                 ANGLE_MASK;
    if (angle_next < angle_prev) {
        s32 antipode = angle_prev + ANGLE_TURN / 2;
        angle_mid = antipode - (angle_prev - angle_next) / 2;
    } else {
        angle_mid = (angle_next - angle_prev) / 2 + angle_prev;
    }
    /* the corner's margin, in the motion files' unit of reach */
    scale = nav_chain->margin * MOTION_REACH_UNIT;
    *corner_x_out =
        vertex_x + ((scale * g_math_sin_table[angle_mid & ANGLE_MASK]) >> 12);
    *corner_z_out =
        vertex_z +
        ((scale *
          g_math_sin_table[(angle_mid + ANGLE_TURN / 4) & ANGLE_MASK]) >>
         12);
}

/* Plans the way round the obstacle that is in the way. From the edge that
 * was crossed it goes round the chain both ways at once, corner by
 * corner, until the target can be seen from where each way has got to
 * (an open chain has ends, and a way that reaches one is given up). The
 * corners of the shorter way are added to the fighter's route, the last
 * first. */
static void cpu_PlanDetour(
    Fighter *fighter, NavSet *nav_set, s32 target_x, s32 target_z) {
    CpuRoute detour_routes[2];
    s16 start_corner_x;
    s16 start_corner_z;
    s16 end_corner_x;
    s16 end_corner_z;
    s32 intersection_x;
    s32 intersection_z;
    s16 backward_point_x;
    s16 backward_point_z;
    s16 forward_point_x;
    s16 forward_point_z;
    u8 backward_active;
    s8 backward_vertex_index;
    s8 forward_vertex_index;
    s16 iter;
    s16 chain_edge_count;
    s32 backward_length;
    s32 forward_length;
    u8 forward_active;
    u8 backward_crosses_edge;
    u8 forward_crosses_edge;
    s16 i;
    s16 next;
    s16 ax;
    s16 az;
    s16 bx;
    s16 bz;
    NavChain *nav_chain;
    /* one way round, the other, then the shorter */
    CpuRoute *route;

    backward_active = 1;
    forward_active = 1;
    nav_chain = &nav_set->chains[fighter->cpu_route.poly];
    backward_point_x = forward_point_x = fighter->world_pos.x;
    backward_point_z = forward_point_z = fighter->world_pos.z;
    detour_routes[1].count = 0;
    detour_routes[0].count = 0;
    forward_length = 0;
    backward_length = 0;
    backward_vertex_index = fighter->cpu_route.vtx;
    forward_vertex_index = backward_vertex_index + 1;
    if (!(forward_vertex_index < nav_chain->nvtx)) {
        if (nav_chain->flags & NAV_CHAIN_OPEN) {
            forward_active = 0;
        } else {
            forward_vertex_index = 0;
        }
    }
    chain_edge_count = nav_chain->nvtx;
    if (nav_chain->flags & NAV_CHAIN_OPEN) {
        chain_edge_count--;
    }
    for (iter = 0; iter < chain_edge_count; iter++) {
        /* a corner of the chain */
        s16 *chain_vertex;
        /* the waypoint being added */
        CpuWaypoint *node;

        forward_crosses_edge = 0;
        backward_crosses_edge = 0;
        for (i = 0; i < chain_edge_count; i++) {
            next = i + 1;
            if (!(next < nav_chain->nvtx)) {
                next = 0;
            }
            chain_vertex = nav_chain->vtx[i];
            ax = chain_vertex[0];
            az = chain_vertex[1];
            cpu_GetCornerPoint(nav_chain, i, &start_corner_x, &start_corner_z);
            chain_vertex = nav_chain->vtx[next];
            bx = chain_vertex[0];
            bz = chain_vertex[1];
            cpu_GetCornerPoint(nav_chain, next, &end_corner_x, &end_corner_z);
            if (backward_active && !backward_crosses_edge) {
                if ((backward_point_x != start_corner_x ||
                     backward_point_z != start_corner_z) &&
                    (backward_point_x != end_corner_x ||
                     backward_point_z != end_corner_z)) {
                    if (math_IntersectSegments(
                            backward_point_x, backward_point_z, target_x,
                            target_z, ax, az, bx, bz, &intersection_x,
                            &intersection_z)) {
                        backward_crosses_edge = 1;
                    }
                }
            }
            if (forward_active && !forward_crosses_edge) {
                if ((forward_point_x != start_corner_x ||
                     forward_point_z != start_corner_z) &&
                    (forward_point_x != end_corner_x ||
                     forward_point_z != end_corner_z)) {
                    if (math_IntersectSegments(
                            forward_point_x, forward_point_z, target_x,
                            target_z, ax, az, bx, bz, &intersection_x,
                            &intersection_z)) {
                        forward_crosses_edge = 1;
                    }
                }
            }
            if (backward_crosses_edge && forward_crosses_edge) {
                break;
            }
        }
        if (backward_active) {
            if (backward_crosses_edge) {
                s32 node_count;

                chain_vertex = nav_chain->vtx[backward_vertex_index];
                backward_length += SquareRoot0(
                    (chain_vertex[0] - backward_point_x) *
                        (chain_vertex[0] - backward_point_x) +
                    (chain_vertex[1] - backward_point_z) *
                        (chain_vertex[1] - backward_point_z));
                cpu_GetCornerPoint(nav_chain, backward_vertex_index,
                                   &backward_point_x, &backward_point_z);
                route = &detour_routes[0];
                node_count = route->count;
                route->count = node_count + 1;
                if (node_count >= CPU_ROUTE_NODES - 1) {
                    backward_active = 0;
                }
                node = &route->node[node_count];
                node->x = backward_point_x;
                node->z = backward_point_z;
                node->kind = nav_chain->kind;
                if (--backward_vertex_index < 0) {
                    if (nav_chain->flags & NAV_CHAIN_OPEN) {
                        backward_active = 0;
                        backward_length = CPU_ANY_DISTANCE;
                    } else {
                        backward_vertex_index = nav_chain->nvtx - 1;
                    }
                }
            } else {
                backward_length += SquareRoot0(
                    (target_x - backward_point_x) *
                        (target_x - backward_point_x) +
                    (target_z - backward_point_z) *
                        (target_z - backward_point_z));
                backward_active = 0;
            }
        }
        if (forward_active) {
            if (forward_crosses_edge) {
                s32 node_count;

                chain_vertex = nav_chain->vtx[forward_vertex_index];
                forward_length += SquareRoot0(
                    (chain_vertex[0] - forward_point_x) *
                        (chain_vertex[0] - forward_point_x) +
                    (chain_vertex[1] - forward_point_z) *
                        (chain_vertex[1] - forward_point_z));
                cpu_GetCornerPoint(nav_chain, forward_vertex_index,
                                   &forward_point_x, &forward_point_z);
                route = &detour_routes[1];
                node_count = route->count;
                route->count = node_count + 1;
                if (node_count >= CPU_ROUTE_NODES - 1) {
                    forward_active = 0;
                }
                node = &route->node[node_count];
                node->x = forward_point_x;
                node->z = forward_point_z;
                node->kind = nav_chain->kind;
                if (!(++forward_vertex_index < nav_chain->nvtx)) {
                    if (nav_chain->flags & NAV_CHAIN_OPEN) {
                        forward_active = 0;
                        forward_length = CPU_ANY_DISTANCE;
                    } else {
                        forward_vertex_index = 0;
                    }
                }
            } else {
                forward_length += SquareRoot0(
                    (target_x - forward_point_x) *
                        (target_x - forward_point_x) +
                    (target_z - forward_point_z) *
                        (target_z - forward_point_z));
                forward_active = 0;
            }
        }
        if (!backward_active && !forward_active) {
            break;
        }
    }
    if (backward_length < forward_length) {
        route = &detour_routes[0];
    } else {
        route = &detour_routes[1];
    }
    for (i = route->count - 1; i >= 0; i--) {
        fighter->cpu_route.node[fighter->cpu_route.count] = route->node[i];
        fighter->cpu_route.count++;
    }
}

#define CPU_SQ(x) ((x) * (x))

/* What the CPU does this frame, when no pad script is playing: returns the
 * buttons to hold, 0 for none, or -1 to be asked again (it has begun a
 * script, or changed its mind). In order of urgency:
 *
 * 1. A threat. An attack that is about to land, or something thrown that
 *    is close, is guarded with one of the two attack buttons: the one that
 *    answers the kind of attack, if the CPU is sharp or knows the attack,
 *    and the wrong one if not. An attack that will only just reach is
 *    stepped back from, and one that can be stepped round is.
 * 2. It is down: it rolls away or gets up. In a blade lock it pushes, and
 *    backs off if there is no ground behind the opponent. Caught in a
 *    paired move it struggles, the better for being unhurt. After certain
 *    moves of its own it follows up (g_cpu_follow_ups).
 * 3. Getting to the opponent: walking or running to it, round an obstacle
 *    if one is in the way, or circling it when it has been pushed to an
 *    edge.
 * 4. Footwork: an action picked at random by the profile's weights (back
 *    and forward, sideways, steps, the leaping attack, doing what the
 *    opponent does), held for a while.
 * 5. An attack: when it is eager and sees an opening, or has waited long
 *    enough, one of its stance's attacks that suits the distance, picked
 *    at random by the profile's weights; a planned opponent may only use
 *    the classes of attack its plan allows. Or it throws dirt or a
 *    sub-weapon.
 * 6. Standing in its stance: it turns to face the opponent, reloads an
 *    empty gun, strikes at an opponent that is down, and after enough
 *    setbacks changes its stance. */
static s32 cpu_Decide(Fighter *fighter) {
    s32 wx;
    u8 *chosen_attack;
    s32 wz;
    s8 best_which_way;
    s16 chosen_attack_index;
    s32 chosen_attack_far;
    s32 effective_cpu_level;
    u8 *guard_script;
    u8 *route_script;
    u8 *action_script;
    u8 *throw_script;
    u8 script_kind;
    /* Each use below says what work1 to work4 hold at that point. work3 is
     * read before anything is stored in it when the CPU mirrors an opponent
     * that is neither walking nor stepping: the action is whatever it was
     * left holding. */
    /* FAKE: some thirty locals of their own, not four shared ones */
    s32 work1;
    s32 work2;
    s32 work3;
    s32 work4;
    s32 footwork;
    s32 far_limit;
    s16 attack_kind;
    s32 waypoint_radius;
    s32 push_button;
    s32 attack_button;
    s32 wide_radius_flag;
    s32 distance_error_streak;
    s16 attack_score;
    NavSet *nav_set;
    s32 tx, tz;
    s32 roll;
    s32 action_index;
    u16 *list;
    u8 *attack;
    u8 *entry_cursor;
    u8 *attack_head;
    u16 script_offset;
    s16 best_attack_score;
    u8 rule_other_kind;
    s8 which_way;
    s32 goal_kind;
    s32 attempt;
    s32 throws;
    s32 any_rule;
    s32 near_enough;
    s8 best_footwork;
    s32 pick_score;

    if (fighter->other->move_kind == MOVE_KIND_END_GAME ||
        fighter->other->move_kind == MOVE_KIND_DTH) {
        return 0;
    }
    /* ---- 1. a threat ---- */
    if (fighter->cpu_profile != CPU_PROFILE_ROW_PASSIVE &&
        ((fighter->cpu_flags &
          (CPU_RUNNING | CPU_STANDING | CPU_WALKING | CPU_STEPPING)) ||
         (fighter->move_kind == MOVE_KIND_BLOCK &&
          (fighter->cpu_attack_threat == CPU_THREAT_MET ||
           fighter->cpu_projectile_threat == CPU_THREAT_MET)))) {
        guard_script = NULL;
        if ((fighter->cpu_attack_threat == CPU_THREAT_NEAR &&
             fighter->other->move_frame + 1 >= fighter->cpu_threat_hit_frame) ||
            fighter->cpu_projectile_threat == CPU_PROJECTILE_CLOSE) {
            if (fighter->cpu_flags & CPU_ALERT) {
                u8 *guards[2];
                s32 flags;
                guards[0] = g_cpu_script_guard_b;
                guards[1] = g_cpu_script_guard_a;
                /* work3: which guard answers the attack; the other one
                 * unless the CPU is sharp or knows the attack. Even sharp,
                 * it gets one threat wrong in every (level in sixteenths
                 * + 2) */
                work3 = fighter->other->attack_kind & 1;
                flags = fighter->cpu_flags;
                if (!(((flags & CPU_KNOWS_ATTACK) ||
                       ((flags & CPU_SHARP) &&
                        fighter->cpu_threats_met %
                                ((fighter->cpu_level >> 8) + 2) !=
                            (fighter->cpu_level >> 8) + 1)) &&
                      (!(g_cpu_profiles[fighter->cpu_profile].flags &
                         CPU_PROFILE_PLANNED) ||
                       (flags & CPU_KNOWS_ATTACK_WELL))) ||
                    (cpu_IsEasy() && g_fight_mode != FIGHT_MODE_CHANBARA)) {
                    work3 = !work3;
                }
                guard_script = guards[work3];
                script_kind = CPU_SCRIPT_KIND_GUARD;
            }
            if (fighter->move_kind == MOVE_KIND_BLOCK) {
                guard_script = NULL;
                fighter->cpu_buttons = 0;
                fighter->cpu_attack_threat = CPU_THREAT_MET;
                fighter->cpu_projectile_threat = CPU_THREAT_MET;
                fighter->cpu_threats_met++;
            }
        } else if (fighter->move_kind == MOVE_KIND_STANCE) {
            script_kind = CPU_SCRIPT_KIND_FOOTWORK;
            /* work3, work1, work2: is there room behind and to either side */
            work3 = fighter->cpu_probes[CPU_PROBE_AWAY] == CPU_FOUND_LEVEL;
            work1 = fighter->cpu_probes[CPU_PROBE_SIDE_A] == CPU_FOUND_LEVEL;
            work2 = fighter->cpu_probes[CPU_PROBE_SIDE_B] == CPU_FOUND_LEVEL;
            if (fighter->cpu_attack_threat == CPU_THREAT_MID) {
                if (fighter->cpu_threat_range * fighter->cpu_threat_range <
                    CPU_SQ(fighter->cpu_threat_x - fighter->world_pos.x) +
                        CPU_SQ(fighter->cpu_threat_z - fighter->world_pos.z)) {
                    fighter->cpu_buttons = 0;
                    fighter->cpu_attack_threat = CPU_THREAT_MET;
                    fighter->cpu_projectile_threat = CPU_THREAT_MET;
                    fighter->cpu_threats_met++;
                } else if (work3 && (fighter->cpu_flags & CPU_ALERT) &&
                           (!(g_cpu_profiles[fighter->cpu_profile].flags &
                              (CPU_PROFILE_DUMMY | CPU_PROFILE_STORY_BOSS |
                               CPU_PROFILE_CHANBARA_LOW)) ||
                            ((g_cpu_profiles[fighter->cpu_profile].flags &
                              CPU_PROFILE_STORY_BOSS) &&
                             g_story_chapter >= CPU_BOSS_EVADES_CHAPTER))) {
                    guard_script = g_cpu_script_step_away;
                }
            }
            if (fighter->cpu_projectile_threat == CPU_PROJECTILE_COMING ||
                (fighter->cpu_attack_threat == CPU_THREAT_NEAR &&
                 /* an attack of class 2 (which has not been made out)
                  * that is still six frames or more from landing */
                 fighter->other->move_class == 2 &&
                 fighter->cpu_threat_hit_frame - fighter->other->move_frame >=
                     6) ||
                (fighter->cpu_attack_threat == CPU_THREAT_MID &&
                 guard_script == NULL)) {
                if ((fighter->cpu_flags & CPU_ALERT) &&
                    (!(g_cpu_profiles[fighter->cpu_profile].flags &
                       CPU_PROFILE_PLANNED_LESSER) ||
                     ((g_cpu_profiles[fighter->cpu_profile].flags &
                       CPU_PROFILE_STORY_BOSS) &&
                      g_story_chapter >= CPU_BOSS_SIDESTEPS_CHAPTER))) {
                    if (fighter->other->opponent_angle > 0) {
                        if (work1) {
                            guard_script = g_cpu_script_step_side_a;
                        } else if (work2) {
                            guard_script = g_cpu_script_step_side_b;
                        }
                    } else {
                        if (work2) {
                            guard_script = g_cpu_script_step_side_b;
                        } else if (work1) {
                            guard_script = g_cpu_script_step_side_a;
                        }
                    }
                    if (guard_script == NULL) {
                        fighter->cpu_attack_threat = CPU_THREAT_NEAR;
                        fighter->cpu_projectile_threat = CPU_PROJECTILE_CLOSE;
                        return -1;
                    }
                    fighter->cpu_attack_threat = CPU_THREAT_MET;
                    fighter->cpu_projectile_threat = CPU_THREAT_MET;
                    fighter->cpu_threats_met++;
                }
            }
        }
        if (guard_script != NULL) {
            cpu_StartScript(fighter, guard_script, script_kind, 0);
            return fighter->cpu_buttons;
        }
    }

    /* ---- 2. down, locked, caught, or following up ---- */
    /* down: roll away from the opponent, or get up. A motion of lying
     * down has a class too. In the common motion file classes 0 and 1 are
     * one motion each of thirty frames, the fighter lying still one way
     * round and the other (so with 0 the side the opponent is on is the
     * other one), and class 2 is four short ones of twelve to fifteen
     * frames, in which there is nothing to do: rolling, by the look of
     * the numbers (a guess). */
    if ((fighter->cpu_flags & CPU_DOWN) && fighter->move_class != 2) {
        if (g_cpu_profiles[fighter->cpu_profile].flags & CPU_PROFILE_DUMMY) {
            fighter->cpu_buttons = FIGHTER_PAD_TOWARD;
        } else if (fighter->cpu_flags & CPU_ALERT) {
            /* work3: which side the opponent is on */
            work3 = fighter->opponent_angle;
            if (fighter->move_class == 0) {
                work3 = (work3 + ANGLE_TURN / 2) & ANGLE_MASK;
                if (work3 > ANGLE_TURN / 2) {
                    work3 -= ANGLE_TURN;
                }
            }
            if (!(g_cpu_profiles[fighter->cpu_profile].flags &
                  (CPU_PROFILE_STORY_BOSS | CPU_PROFILE_CHANBARA_LOW))) {
                if (work3 < 0) {
                    if (fighter->cpu_probes[CPU_PROBE_SIDE_B] ==
                            CPU_FOUND_LEVEL ||
                        fighter->cpu_probes[CPU_PROBE_SIDE_B] ==
                            CPU_FOUND_DROP) {
                        fighter->cpu_buttons = FIGHTER_PAD_SIDE_B;
                    }
                } else {
                    if (fighter->cpu_probes[CPU_PROBE_SIDE_A] ==
                            CPU_FOUND_LEVEL ||
                        fighter->cpu_probes[CPU_PROBE_SIDE_A] ==
                            CPU_FOUND_DROP) {
                        fighter->cpu_buttons = FIGHTER_PAD_SIDE_A;
                    }
                }
            }
            if (!(fighter->cpu_projectile_threat == CPU_PROJECTILE_COMING ||
                  fighter->cpu_projectile_threat == CPU_PROJECTILE_CLOSE) &&
                fighter->cpu_attack_threat != CPU_THREAT_MID &&
                (fighter->cpu_buttons == 0 ||
                 fighter->cpu_opponent_height != CPU_OPPONENT_LEVEL ||
                 fighter->other->move_kind == MOVE_KIND_ATTACK ||
                 fighter->other->move_kind == MOVE_KIND_HOM_ED ||
                 fighter->other->move_kind == MOVE_KIND_RUN ||
                 fighter->other->move_kind == MOVE_KIND_DOWN ||
                 fighter->other->attack_reach[fighter->other->stance] <
                     g_fight_distance)) {
                if (fighter->opponent_angle_abs < ANGLE_TURN / 4 &&
                    fighter->cpu_probes[CPU_PROBE_AWAY] == CPU_FOUND_LEVEL) {
                    fighter->cpu_buttons = FIGHTER_PAD_AWAY;
                } else {
                    fighter->cpu_buttons = FIGHTER_PAD_TOWARD;
                }
            }
        }
    } else {
        u16 state;

        state = fighter->move_kind;
        if (MOVE_IS_LOCK(state)) {
            /* in a blade lock: push with the stance button and now and
             * then an attack; back out sometimes when it is even */
            if (fighter->move_kind == MOVE_KIND_LOCK_EVEN &&
                (rand() & 0xFF) <
                    g_cpu_profiles[fighter->cpu_profile].lock_retreat_chance &&
                fighter->cpu_probes[CPU_PROBE_AWAY] == CPU_FOUND_LEVEL) {
                fighter->cpu_buttons = FIGHTER_PAD_AWAY;
            } else {
                push_button = FIGHTER_PAD_STANCE;
                /* an attack every 18 frames at level 0, every other frame at
                 * the full level */
                if (fighter->cpu_frames % (18 - (fighter->cpu_level >> 8)) ==
                    0) {
                    push_button = FIGHTER_PAD_FRONTAL_ATTACK;
                }
                fighter->cpu_buttons = push_button;
                if (fighter->cpu_probes[CPU_PROBE_AWAY] != CPU_FOUND_LEVEL) {
                    if (fighter->cpu_probes[CPU_PROBE_BACK_B] !=
                        CPU_FOUND_LEVEL) {
                        fighter->cpu_buttons = push_button | FIGHTER_PAD_SIDE_A;
                    } else if (fighter->cpu_probes[CPU_PROBE_BACK_A] !=
                               CPU_FOUND_LEVEL) {
                        fighter->cpu_buttons = push_button | FIGHTER_PAD_SIDE_B;
                    }
                } else if (fighter->cpu_probes[CPU_PROBE_PAST_OPPONENT] ==
                           CPU_FOUND_LEVEL) {
                    fighter->cpu_buttons =
                        (fighter->cpu_profile & 1)
                            ? push_button | FIGHTER_PAD_SIDE_A
                            : push_button | FIGHTER_PAD_SIDE_B;
                }
            }
        } else if (
            state == MOVE_KIND_PAIRED &&
            fighter->index != g_fight_paired_move_initiator &&
            fighter->move_frame == fighter->cur_move->last_command_frame - 1) {
            {
                s32 legs, arm, body;
                legs = fighter->leg_wounds;
                arm = fighter->arm_wounds;
                body = fighter->body_wounds;
                /* caught in a paired move: work3 is its chance to break
                 * out, of CPU_LEVEL_MAX: its level, less a sixteenth for
                 * every wound (a useless off arm counts as four) */
                work3 =
                    CPU_LEVEL_MAX -
                    (((fighter->off_arm_ok == 0 ? legs + 4 + arm : legs + arm) +
                      body)
                     << 8);
                work3 = (fighter->cpu_level * work3) >> 12;
                if ((rand() & CPU_ROLL_MASK) < work3) {
                    fighter->cpu_buttons = FIGHTER_PAD_FRONTAL_ATTACK;
                } else if (fighter->cpu_flags & CPU_KNOWS_ATTACK) {
                    fighter->cpu_buttons = FIGHTER_PAD_FRONTAL_ATTACK;
                }
            }
        } else {
            if ((fighter->ground_y - fighter->foot_pos.vy >= 0
                     ? fighter->ground_y - fighter->foot_pos.vy
                     : fighter->foot_pos.vy - fighter->ground_y) <
                    200 && /* on the ground, or as good as */
                !(g_cpu_profiles[fighter->cpu_profile].flags &
                  (CPU_PROFILE_DUMMY | CPU_PROFILE_CHANBARA_LOW)) &&
                ((!cpu_IsEasy() && g_fight_mode != FIGHT_MODE_CHANBARA) ||
                 g_fight_mode == FIGHT_MODE_CHANBARA)) {
                /* a follow-up to the move it is in (g_cpu_follow_ups). work3:
                 * a threat forces it; work1: the weapon's range; work4: the
                 * rule; work2: the distance within which the rule holds */
                work3 = 0;
                if ((fighter->cpu_attack_threat == CPU_THREAT_NEAR ||
                     fighter->cpu_projectile_threat == CPU_PROJECTILE_CLOSE) &&
                    (fighter->cpu_flags & CPU_ALERT)) {
                    work3 = 1;
                }
                any_rule = 0;
                if ((rand() & CPU_ROLL_MASK) < (fighter->cpu_level >> 3)) {
                    any_rule = fighter->cpu_frames > CPU_WARMUP_FRAMES;
                }
                if (fighter->weapon_id >= WEAPON_ID_FIRST_GUN) {
                    work1 = CPU_ANY_DISTANCE;
                } else {
                    /* (the table is in eighths) */
                    work1 = g_cpu_weapon_ranges[fighter->weapon_id] * 8;
#if RB_PORT
                    work1 = port_WeaponCpuRange(
                        fighter->index, fighter->weapon_id, work1);
#endif
                }
                /* each rule in turn: work4 counts them */
                for (work4 = 0; work4 < sizeof(g_cpu_follow_ups) /
                                            sizeof(g_cpu_follow_ups[0]);
                     work4++) {
                    if (!(g_cpu_follow_ups[work4][CPU_FOLLOW_FLAGS] &
                          CPU_FOLLOW_AT_END) ||
                        fighter->move_frame >=
                            fighter->cur_move->last_command_frame - 2 ||
                        work3) {
                        /* (the table is in sixteenths) */
                        work2 =
                            g_cpu_follow_ups[work4][CPU_FOLLOW_DISTANCE] * 16 +
                            work1 + fighter->reach_bonus;
                        if ((g_fight_distance < work2 &&
                             (!(g_cpu_follow_ups[work4][CPU_FOLLOW_FLAGS] &
                                CPU_FOLLOW_FACING) ||
                              fighter->opponent_angle_abs < ANGLE_TURN / 16) &&
                             (any_rule ||
                              (g_cpu_follow_ups[work4][CPU_FOLLOW_FLAGS] &
                               CPU_FOLLOW_ALWAYS))) ||
                            work3) {
                            if (g_cpu_follow_ups[work4][CPU_FOLLOW_OWN_KIND] ==
                                    fighter->move_kind &&
                                ((rule_other_kind = g_cpu_follow_ups
                                      [work4][CPU_FOLLOW_OTHER_KIND]) ==
                                     CPU_FOLLOW_ANY_KIND ||
                                 rule_other_kind ==
                                     fighter->other->move_kind)) {
                                if (g_cpu_follow_ups[work4][CPU_FOLLOW_FLAGS] &
                                    CPU_FOLLOW_ATTACKS) {
                                    attack_button = FIGHTER_PAD_REVERSE_ATTACK;
                                    if (fighter->cpu_frames & 1) {
                                        attack_button =
                                            FIGHTER_PAD_FRONTAL_ATTACK;
                                    }
                                    fighter->cpu_buttons = attack_button;
                                } else if ((g_cpu_profiles[fighter->cpu_profile]
                                                .flags &
                                            CPU_PROFILE_OPPORTUNIST) &&
                                           !(fighter->stance_class ==
                                                 STANCE_CLASS_TWO_SWORDS ||
                                             fighter->stance_class ==
                                                 STANCE_CLASS_SHEATHED)) {
                                    fighter->cpu_buttons =
                                        (fighter->cpu_frames & 1) * PADR1;
                                }
                                break;
                            }
                        }
                    }
                }
            }
        }
    }

    if (fighter->cpu_buttons != 0) {
        return fighter->cpu_buttons;
    }
    /* alert, and hurt or threatened: stay where it is */
    if ((fighter->cpu_flags & (CPU_ALERT | CPU_RUNNING | CPU_WALKING)) ==
        CPU_ALERT) {
        u16 state;

        state = fighter->move_kind;
        if (state == MOVE_KIND_DAM || state == MOVE_KIND_DEFLECTED ||
            state == MOVE_KIND_STAGGER ||
            (fighter->cpu_attack_threat == CPU_THREAT_NEAR ||
             fighter->cpu_attack_threat == CPU_THREAT_MID) ||
            (fighter->cpu_projectile_threat == CPU_PROJECTILE_COMING ||
             fighter->cpu_projectile_threat == CPU_PROJECTILE_CLOSE)) {
            fighter->cpu_circling = 0;
            fighter->cpu_route.count = 0;
            fighter->cpu_action_timer = 0;
            return 0;
        }
    }

    /* ---- 3. getting to the opponent ---- */
    if (fighter->cpu_flags & (CPU_RUNNING | CPU_STANDING | CPU_WALKING)) {
        nav_set = &g_cpu_stage_obstacles[g_fight_stage];
        if (fighter->cpu_circling == 0) {
            tx = fighter->other->world_pos.x;
            tz = fighter->other->world_pos.z;
            goal_kind = CPU_GOAL_OPPONENT;
        } else {
            tx = fighter->cpu_goal_x;
            tz = fighter->cpu_goal_z;
            goal_kind = fighter->cpu_goal_kind;
        }
        /* It does not go after an opponent that is dropping from a height,
         * nor (the vanisher, or a gun that is empty) after anyone.
         * (The goal is tested twice.) */
        if (!(goal_kind == CPU_GOAL_OPPONENT &&
              (fighter->other->move_kind == MOVE_KIND_DROP ||
               fighter->cpu_profile == CPU_PROFILE_ROW_VANISHER ||
               (fighter->weapon_id >= WEAPON_ID_FIRST_GUN &&
                fighter->ammo == 0)) &&
              goal_kind == CPU_GOAL_OPPONENT)) {
            if (!(fighter->cpu_frames & 7)) { /* every eighth frame */
                fighter->cpu_obstacle_distance =
                    cpu_FindObstacle(fighter, nav_set, tx, tz);
            }
            if (goal_kind == CPU_GOAL_OPPONENT) {
                /* near_enough: how near is near enough. work1: the ground far
                 * ahead */
                work1 = fighter->cpu_probes[CPU_PROBE_FAR_TOWARD];
                if (!(fighter->cpu_flags & CPU_OPPONENT_DOWN) ||
                    g_fight_distance < CPU_STRIKE_FALLEN_RANGE) {
                    if (fighter->cpu_opponent_height == CPU_OPPONENT_BELOW &&
                        !(work1 == CPU_FOUND_NO_FLOOR ||
                          work1 == CPU_FOUND_BLOCKED_NO_FLOOR)) {
                        near_enough =
                            fighter->other
                                ->attack_min_reach[fighter->other->stance];
                    } else if (
                        (fighter->cpu_opponent_height == CPU_OPPONENT_ABOVE ||
                         fighter->cpu_opponent_height == CPU_OPPONENT_BELOW) ||
                        fighter->cpu_opponent_height == 3) {
                        if (!(g_cpu_profiles[fighter->cpu_profile].flags &
                              CPU_PROFILE_CHANBARA_LAST)) {
                            near_enough =
                                fighter->other
                                    ->attack_max_reach[fighter->other->stance];
                        } else {
                            near_enough =
                                fighter->other
                                    ->attack_reach[fighter->other->stance];
                        }
                    } else if (
                        fighter->cpu_obstacle_distance != CPU_NO_OBSTACLE) {
                        if (fighter->cpu_flags & CPU_OPPONENT_RUNNING) {
                            near_enough =
                                fighter->other
                                    ->attack_reach[fighter->other->stance];
                        } else {
                            near_enough =
                                fighter->attack_reach[fighter->stance] +
                                fighter->other
                                    ->attack_reach[fighter->other->stance];
                        }
                    } else {
                        near_enough =
                            fighter->attack_max_reach[fighter->stance] +
                            fighter->other
                                ->attack_max_reach[fighter->other->stance];
                        if (fighter->cpu_flags & CPU_OPPONENT_RUNNING) {
                            if (fighter->other->opponent_angle_abs >
                                3 * ANGLE_TURN / 8) {
                                near_enough = fighter->other->attack_min_reach
                                                  [fighter->other->stance];
                            } else if (fighter->other->opponent_angle_abs >
                                       3 * ANGLE_TURN / 16) {
                                near_enough =
                                    fighter->other
                                        ->attack_reach[fighter->other->stance];
                            } else if (fighter->other->opponent_angle_abs >
                                       ANGLE_TURN / 16) {
                                near_enough = fighter->other->attack_max_reach
                                                  [fighter->other->stance];
                            }
                        }
                    }
                } else {
                    near_enough = CPU_PLACE_RANGE;
                }
            } else {
                near_enough = CPU_PLACE_RANGE;
            }
            if (goal_kind == CPU_GOAL_OPPONENT
                    ? (fighter->cpu_route.count < 2 &&
                       (fighter->cpu_obstacle_distance != CPU_NO_OBSTACLE ||
                        g_fight_distance >= near_enough))
                    : (fighter->cpu_route.count == 0 &&
                       (fighter->cpu_obstacle_distance != CPU_NO_OBSTACLE ||
                        CPU_SQ(fighter->world_pos.x - tx) +
                                CPU_SQ(fighter->world_pos.z - tz) >=
                            CPU_WAYPOINT_RADIUS * CPU_WAYPOINT_RADIUS))) {
                fighter->cpu_route.node[0].x = tx;
                fighter->cpu_route.node[0].z = tz;
                fighter->cpu_route.node[0].kind = goal_kind;
                fighter->cpu_route.count = 1;
                if (fighter->cpu_obstacle_distance != CPU_NO_OBSTACLE &&
                    !(fighter->cpu_frames & 7)) { /* every eighth frame */
                    cpu_PlanDetour(fighter, nav_set, tx, tz);
                }
            }
            if (fighter->cpu_route.count != 0) {
                if (fighter->cpu_route.node[0].kind == CPU_GOAL_OPPONENT
                        ? (fighter->cpu_obstacle_distance == CPU_NO_OBSTACLE &&
                           g_fight_distance < near_enough)
                        : SquareRoot0(CPU_SQ(fighter->world_pos.x - tx) +
                                      CPU_SQ(fighter->world_pos.z - tz)) <
                              CPU_PLACE_RANGE) {
                    fighter->cpu_route.count = 0;
                    fighter->cpu_circling = 0;
                } else {
                    s32 dist;
                    s32 last_route_node_index;
                    /* work1, work2: the ground far ahead and just ahead */
                    work1 = fighter->cpu_probes[CPU_PROBE_FAR_TOWARD];
                    last_route_node_index = fighter->cpu_route.count - 1;
                    work2 = fighter->cpu_probes[CPU_PROBE_TOWARD];
                    wx = fighter->cpu_route.node[last_route_node_index].x;
                    wz = fighter->cpu_route.node[last_route_node_index].z;
                    if (!(work1 == CPU_FOUND_NO_FLOOR ||
                          work1 == CPU_FOUND_BLOCKED_NO_FLOOR)) {
                        route_script = NULL;
                        if (fighter->cpu_route.node[last_route_node_index]
                                .kind == CPU_GOAL_OPPONENT) {
                            /* (a blocked step never leaps: the opponent is
                             * not both beyond its reach and within it) */
                            if ((work2 == CPU_FOUND_DROP &&
                                 g_fight_distance <
                                     fighter->other->attack_reach
                                         [fighter->other->stance]) ||
                                (work2 == CPU_FOUND_BLOCKED_STEP &&
                                 fighter->other->attack_reach[fighter->other
                                                                  ->stance] <
                                     g_fight_distance)) {
                                if (g_fight_distance <
                                    fighter->other->attack_reach
                                        [fighter->other->stance]) {
                                    if (fighter->move_kind == MOVE_KIND_RUN) {
                                        route_script =
                                            g_cpu_script_running_leap;
                                    } else if (fighter->stance_class !=
                                               STANCE_CLASS_SHEATHED) {
                                        route_script = g_cpu_script_leap;
                                    }
                                }
                                if (route_script != NULL) {
                                    cpu_StartScript(
                                        fighter, route_script,
                                        CPU_SCRIPT_KIND_FOOTWORK, 0);
                                }
                            }
                        }
                    }
                    if (fighter->cpu_buttons != 0) {
                        return fighter->cpu_buttons;
                    }
                    if (last_route_node_index == 0) {
                        if (fighter->cpu_route.node[0].kind ==
                            CPU_GOAL_OPPONENT) {
                            dist =
                                SquareRoot0(CPU_SQ(fighter->world_pos.x - tx) +
                                            CPU_SQ(fighter->world_pos.z - tz));
                        } else {
                            dist =
                                SquareRoot0(CPU_SQ(fighter->world_pos.x - wx) +
                                            CPU_SQ(fighter->world_pos.z - wz));
                        }
                    } else {
                        /* how long the route is: work4 a waypoint, work1 and
                         * work2 the place of the second */
                        work4 = last_route_node_index;
                        dist = SquareRoot0(CPU_SQ(wx - fighter->world_pos.x) +
                                           CPU_SQ(wz - fighter->world_pos.z));
                        while (work4 >= 2) {
                            dist += SquareRoot0(
                                CPU_SQ(fighter->cpu_route.node[work4].x -
                                       fighter->cpu_route.node[work4 - 1].x) +
                                CPU_SQ(fighter->cpu_route.node[work4].z -
                                       fighter->cpu_route.node[work4 - 1].z));
                            work4--;
                        }
                        {
                            work1 = fighter->cpu_route.node[1].x;
                            work2 = fighter->cpu_route.node[1].z;
                            if (fighter->cpu_route.node[0].kind ==
                                CPU_GOAL_OPPONENT) {
                                dist += SquareRoot0(
                                    CPU_SQ(work1 - tx) + CPU_SQ(work2 - tz));
                            } else {
                                dist += SquareRoot0(
                                    CPU_SQ(
                                        work1 - fighter->cpu_route.node[0].x) +
                                    CPU_SQ(
                                        work2 - fighter->cpu_route.node[0].z));
                            }
                        }
                    }
                    /* work3: far enough to run */
                    work3 = near_enough < dist;
                    fighter->cpu_buttons = cpu_SteerTo(fighter, wx, wz, work3);
                    waypoint_radius = CPU_WAYPOINT_RADIUS;
                    wide_radius_flag = fighter->cpu_buttons & PADL1;
                    if (wide_radius_flag) {
                        waypoint_radius = CPU_WAYPOINT_RUN_RADIUS;
                    }
                    if (CPU_SQ(fighter->cpu_route
                                   .node[fighter->cpu_route.count - 1]
                                   .x -
                               fighter->world_pos.x) +
                            CPU_SQ(fighter->cpu_route
                                       .node[fighter->cpu_route.count - 1]
                                       .z -
                                   fighter->world_pos.z) <
                        waypoint_radius *
                            (wide_radius_flag ? CPU_WAYPOINT_RUN_RADIUS
                                              : CPU_WAYPOINT_RADIUS)) {
                        if (--fighter->cpu_route.count != 0) {
                            work3 =
                                (ratan2(
                                     fighter->cpu_route
                                             .node[fighter->cpu_route.count - 1]
                                             .x -
                                         fighter->world_pos.x,
                                     fighter->cpu_route
                                             .node[fighter->cpu_route.count - 1]
                                             .z -
                                         fighter->world_pos.z) -
                                 fighter->orientation.vy) &
                                ANGLE_MASK;
                            if (work3 > ANGLE_TURN / 2) {
                                work3 -= ANGLE_TURN;
                            }
                            if ((work3 < 0 ? -work3 : work3) >
                                3 * ANGLE_TURN / 16) {
                                return 0;
                            }
                        }
                    }
                }
            }
            if (fighter->cpu_buttons != 0) {
                return fighter->cpu_buttons;
            }
        }

        /* Crowded: the opponent is in reach and there is no level way back
         * nor to one side (a probe of 2 or more is blocked, or a drop), or
         * no floor behind; or, for three profiles and for a loaded short
         * gun, the opponent is in reach and faces it squarely (within
         * 1/256 of a turn of straight). */
        if ((!(fighter->cpu_flags & CPU_UNDER_ATTACK) &&
             g_fight_distance <
                 fighter->other->attack_reach[fighter->other->stance] &&
             ((fighter->cpu_probes[CPU_PROBE_AWAY] >= 2 &&
               (fighter->cpu_probes[CPU_PROBE_SIDE_A] >= 2 ||
                fighter->cpu_probes[CPU_PROBE_SIDE_B] >= 2)) ||
              fighter->cpu_probes[CPU_PROBE_AWAY] == CPU_FOUND_NO_FLOOR ||
              fighter->cpu_probes[CPU_PROBE_BACK_A] == CPU_FOUND_NO_FLOOR ||
              fighter->cpu_probes[CPU_PROBE_BACK_B] == CPU_FOUND_NO_FLOOR)) ||
            ((g_cpu_profiles[fighter->cpu_profile].flags &
                  CPU_PROFILE_CHANBARA_LAST ||
              /* the two crests beside the select screen's grid (a
               * character's profile is its own number) */
              fighter->cpu_profile == CHARACTER_LAST_1 ||
              fighter->cpu_profile == CHARACTER_LAST_2) &&
             g_fight_distance <
                 fighter->other->attack_reach[fighter->other->stance] &&
             fighter->other->opponent_angle_abs < ANGLE_TURN / 256) ||
            (fighter->weapon_id >= WEAPON_ID_SHORT_GUN &&
             g_fight_distance <
                 fighter->other->attack_reach[fighter->other->stance] &&
             fighter->other->opponent_angle_abs < ANGLE_TURN / 256 &&
             fighter->ammo != 0)) {
            if (!(g_cpu_profiles[fighter->cpu_profile].flags &
                  (CPU_PROFILE_DUMMY | CPU_PROFILE_STORY_BOSS |
                   CPU_PROFILE_CHANBARA_LOW))) {
                if ((fighter->cpu_route.count = cpu_PlanCircling(fighter)) !=
                    0) {
                    fighter->cpu_circling = 2;
                    return -1;
                }
            }
            if (!(g_cpu_profiles[fighter->cpu_profile].flags &
                  (CPU_PROFILE_DUMMY | CPU_PROFILE_CHANBARA_LOW))) {
                /* crowded: step aside or leap in. work3 a coin, work1 and
                 * work2 the room to either side */
                attempt = 0;
                work3 =
                    g_cpu_profiles[fighter->cpu_profile]
                        .footwork_weights[CPU_FOOTWORK_LEAP] < (rand() & 0xFF);
                action_index = -1;
                work1 =
                    fighter->cpu_probes[CPU_PROBE_SIDE_B] == CPU_FOUND_LEVEL;
                work2 =
                    fighter->cpu_probes[CPU_PROBE_SIDE_A] == CPU_FOUND_LEVEL;
                for (; attempt < 2; attempt++, work3++) {
                    if (work3 & 1) {
                        if (fighter->other->opponent_angle < 0) {
                            if (work1) {
                                action_index = CPU_ACTION_STEP_SIDE_B;
                            } else if (work2) {
                                action_index = CPU_ACTION_STEP_SIDE_A;
                            }
                        } else {
                            if (work2) {
                                action_index = CPU_ACTION_STEP_SIDE_A;
                            } else if (work1) {
                                action_index = CPU_ACTION_STEP_SIDE_B;
                            }
                        }
                        if (action_index != -1) {
                            break;
                        }
                    } else if (fighter->stance_class != STANCE_CLASS_SHEATHED &&
                               g_fight_distance > CPU_LEAP_NEAR &&
                               g_fight_distance < CPU_LEAP_FAR) {
                        action_index = CPU_ACTION_LEAP;
                        break;
                    }
                }
                if (action_index != -1) {
                    fighter->cpu_action = action_index;
                    fighter->cpu_mirrors_opponent = 0;
                    fighter->cpu_action_timer = fighter->cpu_action_frames;
                }
            }
        }

        /* ---- 4. footwork ----
         * the action that was picked comes due: work3 */
        if (fighter->cpu_action_timer >= fighter->cpu_action_frames) {
            action_script = NULL;
            if (fighter->cpu_mirrors_opponent != 1) {
                work3 = fighter->cpu_action;
            } else if (fighter->other->move_kind == MOVE_KIND_WALK) {
                /* FAKE: u8 walks[4] = {...}, a local's initialiser */
                u8 walks[4];
                __builtin_memcpy(walks, g_cpu_mirror_walks, 4);
                work3 = walks[fighter->other->move_class];
            } else if (fighter->other->move_kind == MOVE_KIND_STEP) {
                /* FAKE: u8 steps[4] = {...}, a local's initialiser */
                u8 steps[4];
                __builtin_memcpy(steps, g_cpu_mirror_steps, 4);
                work3 = steps[fighter->other->move_class];
            }
            /* mirroring an opponent that neither walks nor steps, work3 is
             * whatever it last held above, or nothing at all: the
             * original's behaviour */
            switch (work3) {
            case CPU_ACTION_HOLD_AWAY:
                if (fighter->cpu_probes[CPU_PROBE_AWAY] == CPU_FOUND_LEVEL) {
                    fighter->cpu_buttons = FIGHTER_PAD_AWAY;
                }
                break;
            case CPU_ACTION_HOLD_TOWARD:
                if (fighter->cpu_probes[CPU_PROBE_TOWARD] == CPU_FOUND_LEVEL) {
                    fighter->cpu_buttons = FIGHTER_PAD_TOWARD;
                }
                break;
            case CPU_ACTION_HOLD_SIDE_B:
                if (fighter->cpu_probes[CPU_PROBE_SIDE_B] == CPU_FOUND_LEVEL) {
                    fighter->cpu_buttons = FIGHTER_PAD_SIDE_B;
                }
                break;
            case CPU_ACTION_HOLD_SIDE_A:
                if (fighter->cpu_probes[CPU_PROBE_SIDE_A] == CPU_FOUND_LEVEL) {
                    fighter->cpu_buttons = FIGHTER_PAD_SIDE_A;
                }
                break;
            case CPU_ACTION_STEP_AWAY:
                if (fighter->cpu_probes[CPU_PROBE_AWAY] == CPU_FOUND_LEVEL &&
                    g_fight_distance <
                        fighter->attack_max_reach[fighter->stance]) {
                    action_script = g_cpu_script_step_away;
                }
                break;
            case CPU_ACTION_STEP_TOWARD:
                if (fighter->cpu_probes[CPU_PROBE_TOWARD] == CPU_FOUND_LEVEL &&
                    fighter->attack_min_reach[fighter->stance] <
                        g_fight_distance) {
                    action_script = g_cpu_script_step_toward;
                }
                break;
            case CPU_ACTION_STEP_SIDE_B:
                if (fighter->cpu_probes[CPU_PROBE_SIDE_B] == CPU_FOUND_LEVEL &&
                    g_fight_distance <
                        fighter->attack_max_reach[fighter->stance]) {
                    action_script = g_cpu_script_step_side_b;
                }
                break;
            case CPU_ACTION_STEP_SIDE_A:
                if (fighter->cpu_probes[CPU_PROBE_SIDE_A] == CPU_FOUND_LEVEL &&
                    g_fight_distance <
                        fighter->attack_max_reach[fighter->stance]) {
                    action_script = g_cpu_script_step_side_a;
                }
                break;
            case CPU_ACTION_LEAP:
                if (!(fighter->cpu_probes[CPU_PROBE_FAR_TOWARD] ==
                          CPU_FOUND_NO_FLOOR ||
                      fighter->cpu_probes[CPU_PROBE_FAR_TOWARD] ==
                          CPU_FOUND_BLOCKED_NO_FLOOR) &&
                    g_fight_distance > CPU_LEAP_NEAR &&
                    g_fight_distance < CPU_LEAP_FAR &&
                    fighter->opponent_angle_abs < ANGLE_TURN / 8) {
                    action_script = g_cpu_script_leap;
                }
                break;
            }
            if ((fighter->cpu_attack_threat == CPU_THREAT_NEAR ||
                 fighter->cpu_attack_threat == CPU_THREAT_MID) ||
                (fighter->cpu_projectile_threat == CPU_PROJECTILE_COMING ||
                 fighter->cpu_projectile_threat == CPU_PROJECTILE_CLOSE)) {
                if (fighter->cpu_buttons != 0) {
                    fighter->cpu_action_timer = fighter->cpu_action_frames + 1;
                } else {
                    fighter->cpu_action_timer = 0;
                    action_script = NULL;
                }
            } else if ((fighter->cpu_flags & CPU_OPPONENT_DOWN) ||
                       fighter->cpu_opponent_height != CPU_OPPONENT_LEVEL ||
                       (fighter->cpu_buttons == FIGHTER_PAD_TOWARD &&
                        g_fight_distance <
                            fighter->other
                                ->attack_min_reach[fighter->other->stance])) {
                action_script = NULL;
                fighter->cpu_action_timer = 0;
                fighter->cpu_buttons = 0;
            }
            if (action_script != NULL) {
                fighter->cpu_action_timer = 0;
                if (fighter->cpu_flags &
                    (CPU_OPPONENT_STANDING | CPU_OPPONENT_WALKING)) {
                    cpu_StartScript(
                        fighter, action_script, CPU_SCRIPT_KIND_FOOTWORK, 0);
                }
            }
        } else if (fighter->cpu_action_timer == 0) {
            s32 pick_chance =
                g_cpu_profiles[fighter->cpu_profile].footwork_chance;
            roll = rand() & 0xFF;
            if (fighter->stance_class == STANCE_CLASS_SHEATHED
                    ? roll < (pick_chance >> 2)
                    : roll < pick_chance) {
                /* pick an action: a weighted throw for each, the best wins
                 * (work2 the best so far) */
                work2 = -1;
                best_footwork = -1;
                /* FAKE: for (footwork = 0; footwork < 7; footwork++), and
                 * continue for goto pick_next */
                footwork = 0;
            pick_loop: {
                pick_score = ((rand() & CPU_ROLL_MASK) *
                              g_cpu_profiles[fighter->cpu_profile]
                                  .footwork_weights[footwork]) >>
                             12;
                if (pick_score != 0) {
                    which_way = 0;
                    switch (footwork) {
                    case CPU_FOOTWORK_WALK:
                    case CPU_FOOTWORK_STEP:
                        if (rand() & 1) {
                            which_way = 1;
                        } else if (fighter->cpu_probes[CPU_PROBE_AWAY] !=
                                   CPU_FOUND_LEVEL) {
                            goto pick_next;
                        }
                        distance_error_streak = fighter->cpu_range_error_frames;
                        if (g_cpu_profiles[fighter->cpu_profile]
                                .range_tolerance <
                            (distance_error_streak >= 0
                                 ? distance_error_streak
                                 : -distance_error_streak)) {
                            /* too long off its distance: half a full weight
                             * more, and the way that mends it */
                            pick_score += 0x80;
                            which_way = distance_error_streak > 0;
                        }
                        if (fighter->weapon_id >= WEAPON_ID_FIRST_GUN &&
                            fighter->ammo == 0 &&
                            footwork == CPU_FOOTWORK_STEP) {
                            /* a gun that is empty: a full weight more to step
                             * back */
                            pick_score += 0x100;
                            which_way = 0;
                        }
                        break;
                    case CPU_FOOTWORK_SIDLE:
                    case CPU_FOOTWORK_SIDE_STEP:
                        if (rand() & 1) {
                            if (fighter->cpu_probes[CPU_PROBE_SIDE_A] !=
                                CPU_FOUND_LEVEL) {
                                goto pick_next;
                            }
                            which_way = 1;
                        } else if (fighter->cpu_probes[CPU_PROBE_SIDE_B] !=
                                   CPU_FOUND_LEVEL) {
                            goto pick_next;
                        }
                        if (fighter->cpu_flags & CPU_STEPS_ROUND) {
                            /* half a full weight more to get round the
                             * opponent's stance */
                            pick_score += 0x80;
                            which_way = fighter->opponent_angle > 0;
                            if (*(which_way
                                      ? &fighter->cpu_probes[CPU_PROBE_SIDE_A]
                                      : &fighter
                                             ->cpu_probes[CPU_PROBE_SIDE_B]) !=
                                CPU_FOUND_LEVEL) {
                                which_way ^= 1;
                            }
                        }
                        break;
                    case CPU_FOOTWORK_LEAP:
                        if ((fighter->cpu_probes[CPU_PROBE_FAR_TOWARD] ==
                                 CPU_FOUND_NO_FLOOR ||
                             fighter->cpu_probes[CPU_PROBE_FAR_TOWARD] ==
                                 CPU_FOUND_BLOCKED_NO_FLOOR) ||
                            !(g_fight_distance >= CPU_LEAP_NEAR &&
                              g_fight_distance <= CPU_LEAP_FAR) ||
                            fighter->opponent_angle_abs > ANGLE_TURN / 8 ||
                            fighter->stance_class == STANCE_CLASS_SHEATHED ||
                            fighter->other->move_kind == MOVE_KIND_LJUMP ||
                            fighter->other->move_kind == MOVE_KIND_CROUCH) {
                            goto pick_next;
                        }
                        break;
                    }
                    /* FAKE: work2 < pick_score */
                    if ((s16)work2 < pick_score) {
                        best_footwork = footwork;
                        work2 = pick_score;
                        best_which_way = which_way;
                    }
                }
            }
            pick_next:
                if (++footwork < CPU_FOOTWORK_COUNT) {
                    goto pick_loop;
                }
                if (best_footwork != -1) {
                    fighter->cpu_action = 0;
                    fighter->cpu_mirrors_opponent = 0;
                    /* for 23 frames of its moves, and at random up to
                     * footwork_chance more */
                    fighter->cpu_action_timer =
                        (((((rand() & CPU_ROLL_MASK) *
                            g_cpu_profiles[fighter->cpu_profile]
                                .footwork_chance) >>
                           12) +
                          23) *
                         FIGHTER_SPEED_ONE) /
                        fighter->base_speed;
                    switch (best_footwork) {
                    case CPU_FOOTWORK_WALK:
                        /* CPU_ACTION_HOLD_AWAY or _TOWARD */
                        fighter->cpu_action = best_which_way != 0;
                        break;
                    case CPU_FOOTWORK_SIDLE:
                        if (best_which_way) {
                            fighter->cpu_action = CPU_ACTION_HOLD_SIDE_A;
                        } else {
                            fighter->cpu_action = CPU_ACTION_HOLD_SIDE_B;
                        }
                        break;
                    case CPU_FOOTWORK_STEP:
                        if (best_which_way) {
                            fighter->cpu_action = CPU_ACTION_STEP_TOWARD;
                        } else {
                            fighter->cpu_action = CPU_ACTION_STEP_AWAY;
                        }
                        break;
                    case CPU_FOOTWORK_SIDE_STEP:
                        if (best_which_way) {
                            fighter->cpu_action = CPU_ACTION_STEP_SIDE_A;
                        } else {
                            fighter->cpu_action = CPU_ACTION_STEP_SIDE_B;
                        }
                        break;
                    case CPU_FOOTWORK_LEAP:
                        fighter->cpu_action = CPU_ACTION_LEAP;
                        break;
                    case CPU_FOOTWORK_MIRROR:
                        fighter->cpu_mirrors_opponent = 1;
                        break;
                    }
                }
            }
        }
        if (fighter->cpu_action_timer != 0) {
            fighter->cpu_action_timer--;
        }
    }

    if (fighter->cpu_buttons != 0 || fighter->cpu_action_timer != 0) {
        return fighter->cpu_buttons;
    }
    /* ---- 5. an attack ---- */
    if ((fighter->cpu_flags & (CPU_SEES_OPENING | CPU_IMPATIENT)) &&
        !(fighter->cpu_flags & CPU_OPPONENT_DOWN)) {
        u16 state;

        state = fighter->move_kind;
        if (state == MOVE_KIND_STANCE ||
            (state == MOVE_KIND_WALK &&
             fighter->move_class >= MOVE_CLASS_SIDE)) {
            if ((fighter->cpu_frames & 1) ||
                fighter->weapon_id >= WEAPON_ID_FIRST_GUN) {
                if (fighter->cpu_opponent_height == CPU_OPPONENT_LEVEL &&
                    fighter->cpu_probes[CPU_PROBE_AWAY] != CPU_FOUND_NO_FLOOR &&
                    (fighter->weapon_id < WEAPON_ID_FIRST_GUN ||
                     fighter->ammo != 0)) {
                    best_attack_score = -1;
                    /* one attack in every (level in sixteenths + 2) is picked
                     * as if at half its level */
                    if (fighter->cpu_attacks_begun %
                            ((fighter->cpu_level >> 8) + 2) ==
                        (fighter->cpu_level >> 8) + 1) {
                        effective_cpu_level = fighter->cpu_level >> 1;
                    } else {
                        effective_cpu_level = fighter->cpu_level;
                    }
                    list = fighter->cpu_stance_scripts[fighter->stance];
                    for (work4 = 0; (script_offset = *list) != 0; list++,
                        work4++) {
                        /* a few bits of a practice pattern; a margin on the
                         * attack's distances; the attack's kind */
                        s32 work5;
                        /* FAKE: 1, in a local */
                        s32 one = 1;
                        entry_cursor =
                            script_offset + (u8 *)fighter->cpu_scripts;
                        attack = entry_cursor;
                        entry_cursor += CPU_ATTACK_HEAD;
                        /* FAKE: entry_cursor itself */
                        attack_head = entry_cursor;
                        if (g_cpu_profiles[fighter->cpu_profile].flags &
                            CPU_PROFILE_PLANNED) {
                            switch (g_fight_mode) {
                            /* a planned opponent: work3 becomes the classes of
                             * attack it may use now */
                            case FIGHT_MODE_CHANBARA:
                                /* two sets for every ten opponents, then
                                 * five for the last ten */
                                if (g_chanbara_count < CHANBARA_LAST_TEN) {
                                    work3 =
                                        g_chanbara_count / CPU_CHANBARA_TEN * 2;
                                    if (g_chanbara_count % CPU_CHANBARA_TEN ==
                                        0) {
                                        work3--;
                                    }
                                } else if (
                                    g_chanbara_count < CHANBARA_LAST_TEN + 3) {
                                    work3 = CPU_CHANBARA_LAST_SETS;
                                } else if (
                                    g_chanbara_count < CHANBARA_LAST_TEN + 5) {
                                    /* the next two */
                                    work3 = CPU_CHANBARA_LAST_SETS + 1;
                                } else if (
                                    g_chanbara_count < CHANBARA_LAST_TEN + 7) {
                                    /* and the two after */
                                    work3 = CPU_CHANBARA_LAST_SETS + 2;
                                } else if (
                                    g_chanbara_count < CHANBARA_OPPONENTS) {
                                    work3 = CPU_CHANBARA_LAST_SETS + 3;
                                } else {
                                    work3 = CPU_CHANBARA_LAST_SETS + 4;
                                }
                                work3 = g_cpu_chanbara_attacks
                                    [fighter->stance_class][work3];
                                break;
                            case FIGHT_MODE_PRACTICE:
                                /* a pattern of classes, a digit each, the
                                 * lowest digit their number; the top bits say
                                 * all at once, otherwise one after another */
                                work1 = g_cpu_practice_attacks
                                    [g_cpu_practice_rows[fighter->weapon_id]
                                                        [fighter->stance_class]]
                                    [g_fight_practice_options];
                                work3 = 0;
                                work5 = work1 >> 27;
                                work2 = work1 & 0xF; /* a digit */
                                if (work2 != 0) {
                                    if (work5) {
                                        while (work2 > 0) {
                                            work1 >>= 4;
                                            /* the next digit's class */
                                            work3 |= one << ((work1 & 0xF) - 1);
                                            work2--;
                                        }
                                    } else {
                                        work1 >>= (fighter->cpu_attacks_begun /
                                                   3 % work2) *
                                                      4 +
                                                  4;
                                        /* that digit's class */
                                        work3 = one << ((work1 & 0xF) - 1);
                                    }
                                }
                                break;
                            default:
                                work3 =
                                    g_cpu_story_limits[fighter->stance_class]
                                                      [g_story_chapter - 1]
                                                          .mask;
                                break;
                            }
                            if ((attack[CPU_ATTACK_CLASS] >> 4) == 0 ||
                                !(work3 &
                                  (one
                                   << ((attack[CPU_ATTACK_CLASS] >> 4) - 1)))) {
                                continue;
                            }
                        }
                        if (!(g_cpu_profiles[fighter->cpu_profile].flags &
                              CPU_PROFILE_REPEATS) &&
                            fighter->cpu_last_script_stance ==
                                fighter->stance &&
                            fighter->cpu_last_script == work4) {
                            continue;
                        }
                        if (attack_head[0] == CPU_ATTACK_FOR_SOME) {
                            /* only for some characters */
                            work3 = attack_head[4] << 24 |
                                    attack_head[3] << 16 | attack_head[2] << 8 |
                                    attack_head[1];
                            if (!(work3 & (one << fighter->cpu_profile))) {
                                continue;
                            }
                            /* past the mask's five bytes */
                            entry_cursor += 5;
                        }
                        if ((attack[CPU_ATTACK_FLAGS] & CPU_ATTACK_NEEDS_ARM) &&
                            fighter->off_arm_ok == 0) {
                            continue;
                        }
                        /* work1 and far_limit: between which distances it
                         * works; work3: whether it will do */
                        work1 = attack[CPU_ATTACK_NEAR] * MOTION_REACH_UNIT;
                        far_limit = attack[CPU_ATTACK_FAR] * MOTION_REACH_UNIT;
                        /* FAKE: no do-while(0) round it */
                        do {
                            attack_kind =
                                attack[CPU_ATTACK_FLAGS] & CPU_ATTACK_KIND;
                        } while (0);
                        work3 = 0;
                        if (attack_kind == 0) {
                            if (work1 < g_fight_distance &&
                                g_fight_distance < far_limit) {
                                if (fighter->other->move_kind ==
                                        MOVE_KIND_STANCE ||
                                    fighter->other->move_kind ==
                                        MOVE_KIND_DEFLECTED ||
                                    fighter->other->move_kind ==
                                        MOVE_KIND_DEFLECT ||
                                    fighter->other->move_kind ==
                                        MOVE_KIND_WALK) {
                                    work3 = 1;
                                }
                            }
                        } else {
                            if ((g_cpu_profiles[fighter->cpu_profile].flags &
                                 (CPU_PROFILE_DUMMY | CPU_PROFILE_CHANBARA)) ||
                                fighter->weapon_id >= WEAPON_ID_FIRST_GUN) {
                                work1 = attack_kind < CPU_ATTACK_KIND_DIRT
                                            ? CPU_ANY_DISTANCE
                                            : 0;
                                far_limit = CPU_ANY_DISTANCE;
                            } else {
                                /* a margin on both distances: 2100 at level 0,
                                 * down to -400 at the full level */
                                work5 =
                                    (((CPU_LEVEL_MAX - effective_cpu_level) *
                                      625) >>
                                     10) -
                                    400;
                                work1 += work5 + fighter->reach_bonus;
                                far_limit += work5 + fighter->reach_bonus;
                            }
                            work5 = attack_kind;
                            if (work5 < CPU_ATTACK_KIND_DIRT) {
                                if (g_fight_distance < work1 &&
                                    !(fighter->cpu_flags & CPU_STEPS_ROUND)) {
                                    if ((fighter->cpu_flags &
                                         CPU_OPPONENT_RUNNING)
                                            ? fighter->opponent_angle_abs <
                                                  ANGLE_TURN / 2
                                            : fighter->opponent_angle_abs <
                                                  ANGLE_TURN / 4) {
                                        work3 = 1;
                                    } else if (fighter->weapon_id >=
                                               WEAPON_ID_FIRST_GUN) {
                                        work3 = 1;
                                    }
                                }
                            } else if (
                                work1 < g_fight_distance &&
                                g_fight_distance < far_limit &&
                                fighter->opponent_angle_abs <
                                    ANGLE_TURN / 8 - ((fighter->cpu_level *
                                                       (ANGLE_TURN / 16)) >>
                                                      12) &&
                                (fighter->cpu_flags &
                                 (CPU_UNDER_ATTACK | CPU_OPPONENT_RUNNING)) !=
                                    (CPU_UNDER_ATTACK | CPU_OPPONENT_RUNNING)) {
                                switch (work5) {
                                case CPU_ATTACK_KIND_DIRT:
                                    if (SURFACE_IN(
                                            fighter->surface, SURFACES_LOOSE)) {
                                        work3 = 1;
                                    }
                                    break;
                                case CPU_ATTACK_KIND_THROW:
                                    if (fighter->cpu_profile ==
                                            CPU_PROFILE_ROW_STUNNER ||
                                        fighter->sub_weapon_count != 0) {
                                        work3 = 1;
                                    }
                                    break;
                                default:
                                    work3 = 1;
                                    break;
                                }
                            }
                        }
                        if (work3) {
                            attack_score = ((rand() & CPU_ROLL_MASK) *
                                            g_cpu_profiles[fighter->cpu_profile]
                                                .attack_weights[attack_kind]) >>
                                           12;
                            if (attack_score != 0 &&
                                best_attack_score < attack_score) {
                                best_attack_score = attack_score;
                                chosen_attack_index = work4;
                                chosen_attack = entry_cursor;
                                chosen_attack_far = far_limit;
                            }
                        }
                    }
                    if (best_attack_score != -1) {
                        cpu_StartScript(
                            fighter, chosen_attack, CPU_SCRIPT_KIND_ATTACK, 0);
                        fighter->cpu_last_script = chosen_attack_index;
                        fighter->cpu_last_script_stance = fighter->stance;
                        fighter->cpu_script_target_x = fighter->world_pos.x;
                        fighter->cpu_script_target_z = fighter->world_pos.z;
                        fighter->cpu_attacks_begun++;
                        fighter->cpu_script_range = chosen_attack_far;
                    }
                }
            } else if (
                state == MOVE_KIND_STANCE && fighter->off_arm_ok != 0 &&
                fighter->stance_class != STANCE_CLASS_SHEATHED &&
                fighter->opponent_angle_abs <
                    ANGLE_TURN / 8 -
                        ((fighter->cpu_level * (ANGLE_TURN / 16)) >> 12) &&
                !(g_fight_mode == FIGHT_MODE_PRACTICE ||
                  g_fight_mode == FIGHT_MODE_CHANBARA)) {
                throw_script = NULL;
                if ((rand() & 0xFF) <
                        (g_cpu_profiles[fighter->cpu_profile]
                             .attack_weights[CPU_ATTACK_KIND_DIRT] >>
                         2) &&
                    SURFACE_IN(fighter->surface, SURFACES_LOOSE) &&
                    fighter->cpu_opponent_height == CPU_OPPONENT_LEVEL &&
                    fighter->other->attack_reach[fighter->other->stance] <
                        g_fight_distance &&
                    g_fight_distance < CPU_DIRT_RANGE) {
                    throw_script = g_cpu_script_throw_dirt;
                } else {
                    /* throw a sub-weapon, from beyond work1 */
                    s32 to_throw;

                    /* the stunner stamps, from within 3000; the others throw
                     * from 300 beyond the opponent's reach */
                    if (fighter->cpu_profile == CPU_PROFILE_ROW_STUNNER) {
                        to_throw = fighter->stun_charges;
                        work1 = fighter->other
                                    ->attack_min_reach[fighter->other->stance];
                    } else {
                        work1 = fighter->other
                                    ->attack_reach[fighter->other->stance] +
                                CPU_THREAT_MARGIN;
                        if (g_cpu_profiles[fighter->cpu_profile].flags &
                            CPU_PROFILE_STORY_BOSS) {
                            to_throw = 0;
                            if (g_story_chapter >= CPU_BOSS_EVADES_CHAPTER) {
                                to_throw = fighter->ammo;
                            }
                        } else {
                            to_throw = fighter->sub_weapon_count;
                        }
                    }
                    throws = 0;
                    if ((rand() & 0xFF) <
                            (g_cpu_profiles[fighter->cpu_profile]
                                 .attack_weights[CPU_ATTACK_KIND_THROW] >>
                             2) &&
                        to_throw != 0 && work1 < g_fight_distance &&
                        fighter->cpu_obstacle_distance == CPU_NO_OBSTACLE &&
                        fighter->cpu_opponent_height == CPU_OPPONENT_LEVEL &&
                        (fighter->cpu_flags &
                         (CPU_SEES_OPENING | CPU_OPPONENT_STANDING |
                          CPU_OPPONENT_WALKING)) &&
                        (fighter->cpu_profile != CPU_PROFILE_ROW_STUNNER ||
                         g_fight_distance < 3000) && /* its stamp's reach */
                        (fighter->two_sword_ready == 0 || to_throw >= 2)) {
                        throws = 1;
                    }
                    if (throws) {
                        if ((g_cpu_profiles[fighter->cpu_profile].flags &
                             CPU_PROFILE_THROWS_TWO) &&
                            to_throw >= 2 && (rand() & 1)) {
                            throw_script = g_cpu_script_throw_twice;
                        } else {
                            throw_script = g_cpu_script_throw;
                        }
                    }
                }
                if (throw_script != NULL) {
                    cpu_StartScript(
                        fighter, throw_script, CPU_SCRIPT_KIND_THROW, 0);
                }
            }
        }
    }

    if (fighter->cpu_buttons != 0) {
        return fighter->cpu_buttons;
    }
    /* ---- 6. standing in its stance ---- */
    if (fighter->move_kind == MOVE_KIND_STANCE) {
        if (fighter->opponent_angle_abs > ANGLE_TURN / 16 &&
            !(fighter->prev_move_kind == MOVE_KIND_WALK ||
              fighter->prev_move_kind == MOVE_KIND_STEP) &&
            fighter->weapon_id < WEAPON_ID_SHORT_GUN &&
            fighter->cpu_profile != CPU_PROFILE_ROW_PASSIVE) {
            fighter->cpu_mirrors_opponent = 0;
            /* for 23 frames of its moves, as footwork is held at the least */
            fighter->cpu_action_timer =
                23 * FIGHTER_SPEED_ONE / fighter->base_speed;
            if (fighter->cpu_probes[CPU_PROBE_SIDE_A] == CPU_FOUND_LEVEL) {
                fighter->cpu_action = CPU_ACTION_HOLD_SIDE_A;
            } else if (
                fighter->cpu_probes[CPU_PROBE_SIDE_B] == CPU_FOUND_LEVEL) {
                fighter->cpu_action = CPU_ACTION_HOLD_SIDE_B;
            } else if (
                fighter->cpu_probes[CPU_PROBE_TOWARD] == CPU_FOUND_LEVEL) {
                fighter->cpu_action = CPU_ACTION_HOLD_TOWARD;
            } else {
                fighter->cpu_action = CPU_ACTION_HOLD_AWAY;
            }
        } else if (fighter->weapon_id >= WEAPON_ID_FIRST_GUN) {
            if (fighter->ammo == 0 && fighter->reloads_left != 0 &&
                fighter->off_arm_ok != 0 &&
                fighter->other->attack_reach[fighter->other->stance] <
                    g_fight_distance) {
                fighter->cpu_buttons = FIGHTER_PAD_STANCE;
            }
        } else if ((fighter->cpu_flags & CPU_OPPONENT_DOWN) &&
                   (g_cpu_profiles[fighter->cpu_profile].flags &
                    CPU_PROFILE_STRIKES_FALLEN) &&
                   g_fight_distance < CPU_STRIKE_FALLEN_RANGE &&
                   fighter->stance_class != STANCE_CLASS_SHEATHED &&
                   (fighter->cpu_opponent_height == CPU_OPPONENT_LEVEL ||
                    fighter->cpu_opponent_height == CPU_OPPONENT_BELOW)) {
            cpu_StartScript(
                fighter, g_cpu_script_strike_fallen,
                CPU_SCRIPT_KIND_STRIKE_FALLEN, fighter->cpu_reaction_delay);
            fighter->cpu_attacks_begun++;
        } else if (
            (fighter->cpu_flags &
             (CPU_EAGER | CPU_OPPONENT_DOWN | CPU_OPPONENT_STANDING |
              CPU_OPPONENT_WALKING)) ||
            fighter->other->move_kind == MOVE_KIND_LJUMP ||
            fighter->other->move_kind == MOVE_KIND_STANCE_CHANGE ||
            fighter->other->move_kind == MOVE_KIND_CLIMB ||
            fighter->other->move_kind == MOVE_KIND_DROP ||
            (fighter->other->move_kind == MOVE_KIND_STEP &&
             /* its own motion's class, where the opponent's (a
              * step away) would be expected: the original's
              * behaviour */
             fighter->move_class == MOVE_CLASS_AWAY)) {
            if (g_cpu_profiles[fighter->cpu_profile].setback_limit != 0 &&
                ((!(g_cpu_profiles[fighter->cpu_profile].flags &
                    CPU_PROFILE_PLANNED) &&
                  fighter->other->attack_reach[fighter->other->stance] <
                      g_fight_distance &&
                  fighter->cpu_setbacks >=
                      g_cpu_profiles[fighter->cpu_profile].setback_limit) ||
                 ((g_cpu_profiles[fighter->cpu_profile].flags &
                   CPU_PROFILE_STORY_BOSS_1) &&
                  fighter->other->attack_min_reach[fighter->other->stance] <
                      g_fight_distance &&
                  fighter->cpu_setbacks >=
                      g_cpu_profiles[fighter->cpu_profile].setback_limit &&
                  /* not in the third of the ordinary stances */
                  fighter->stance_class != 2) ||
                 ((g_cpu_profiles[fighter->cpu_profile].flags &
                   CPU_PROFILE_CHANBARA) &&
                  fighter->other->attack_min_reach[fighter->other->stance] <
                      g_fight_distance &&
                  fighter->cpu_setbacks >=
                      g_cpu_profiles[fighter->cpu_profile].setback_limit &&
                  g_chanbara_count >= CHANBARA_LAST_TEN) ||
                 (fighter->cpu_flags & CPU_TOLD_TO_CHANGE))) {
                fighter->cpu_setbacks = 0;
                fighter->cpu_buttons = FIGHTER_PAD_STANCE;
                fighter->cpu_flags &= ~CPU_TOLD_TO_CHANGE;
            }
        }
    }
    return fighter->cpu_buttons;
}

#undef CPU_SQ

/* The profiles: one for each of the 27 characters (a fighter's profile is
 * its character), then chanbara's five. Each is laid out in four lines:
 *   the flags (the bare numbers are bits nobody has a name for);
 *   unk2, the setback limit, the chance to back out of a lock, where in its
 *     range it stands, how readily it takes up footwork, the range tolerance;
 *   the footwork weights: walk, sidle, step, side step, leap, mirror, unused;
 *   the attack weights: kinds 0 to 4, dirt, throw, kind 7.
 * The chances and weights are out of 256. */
/* clang-format off */
static CpuProfile g_cpu_profiles[32] = {
    /* 0: KANNUKI */
    {0,
       0,   4,   0,  64,   4, 128,
     {128, 255,   0,   0,   0,   0,   0},
     {255, 192, 128,  64,  64,  64,  64,  64}},
    /* 1: TATSUMI */
    {CPU_PROFILE_STRIKES_FALLEN | 0x06,
       3,   4,  32, 128,   8,  64,
     {128, 128, 255, 255,  96, 128,   0},
     { 64, 192, 192, 128,  64, 128,  64, 128}},
    /* 2: MIKADO */
    {CPU_PROFILE_THROWS_TWO | 0x02,
       2,   3, 255, 192,  16, 255,
     { 32, 255,   0,   0,  64,   0,   0},
     { 64, 128, 128, 128, 128,  64, 192, 192}},
    /* 3: UTSUSEMI */
    {0,
       2,   2,  32, 128,   6, 255,
     {128, 255,   0,   0,   0, 128,   0},
     {192, 192, 128,  64,  64,  64,  64, 128}},
    /* 4: RED SHADOW */
    {CPU_PROFILE_STRIKES_FALLEN | 0x06,
       1,   2, 255, 192,  16,  64,
     {255,   0, 255, 255,  96,   0,   0},
     { 64,  64, 192, 128, 128, 192, 192, 192}},
    /* 5: TSUBAME */
    {CPU_PROFILE_STRIKES_FALLEN | 0x04,
       1,   3, 255, 255,  32,  32,
     {255, 255,   0, 255,   0,   0,   0},
     {  0, 255, 128,  64,   0,   0,   0,   0}},
    /* 6: SAZANKA */
    {CPU_PROFILE_REPEATS | CPU_PROFILE_OPPORTUNIST | CPU_PROFILE_THROWS_TWO |
     CPU_PROFILE_STRIKES_FALLEN | 0x06,
       1,   2,   0,  64,  16,  64,
     {255, 255, 255, 255,  64, 255,   0},
     {192,  64, 128, 192, 128, 192, 192,  64}},
    /* 7: SUMINAGASHI */
    {0,
       0,   3,   0,  64,   4, 255,
     {255,   0,   0,   0,   0, 128,   0},
     {255, 192,   0,  64,  64,  64,  64, 128}},
    /* 8: NIGHTSTALKER */
    {CPU_PROFILE_THROWS_TWO | CPU_PROFILE_STRIKES_FALLEN | 0x42,
       0,   2, 255, 192,  16, 128,
     {255, 255, 128, 128, 128, 255,   0},
     {255,  64, 192, 192,  64, 128, 128, 192}},
    /* 9: MATSUMUSHI */
    {CPU_PROFILE_REPEATS | CPU_PROFILE_STRIKES_FALLEN | 0x02,
       3,   3, 255, 128,   8, 255,
     { 32, 255,   0,   0,   0, 255,   0},
     { 64, 128, 192,  64,  64, 128, 128, 128}},
    /* 10: the vanisher (CPU_PROFILE_ROW_VANISHER) */
    {0x02,
       2,   3,  32,   0,   8, 255,
     {128, 128, 128, 128,   0, 255,   0},
     {192, 128, 128,  64,  64,  64,  64, 192}},
    /* 11: the first ninja with a sidearm */
    {CPU_PROFILE_STORY_BOSS_1 | CPU_PROFILE_REPEATS |
     CPU_PROFILE_STRIKES_FALLEN | 0x40,
       9, 255,   0, 128,  32,  48,
     {  0,   0, 255, 128,   0,   0,   0},
     {  0, 255, 255, 255, 255,   0,  64, 255}},
    /* 12: GENGORO */
    {CPU_PROFILE_THROWS_TWO | CPU_PROFILE_STRIKES_FALLEN,
       2,   4,   0, 128,  16, 255,
     { 32, 255,   0,   0,   0, 128,   0},
     {192, 128, 192,  64,  64,  64,  64,  64}},
    /* 13: KAUN */
    {CPU_PROFILE_STRIKES_FALLEN,
       4,   3, 255, 128,   8, 128,
     {128, 255,   0,   0,   0,   0,   0},
     {192, 192, 128,  64,  64, 128,  64, 128}},
    /* 14: JO */
    {CPU_PROFILE_REPEATS | CPU_PROFILE_STRIKES_FALLEN | 0x06,
       0,   2, 255, 192,  24,  64,
     {255, 128, 128, 128,  64,   0,   0},
     {192,  64,  64, 192, 128, 192, 192, 192}},
    /* 15: HONGOU */
    {CPU_PROFILE_STRIKES_FALLEN | 0x02,
       2,   4,   0,  64,   8, 128,
     {255, 128,   0,   0,   0, 255,   0},
     {255,  64, 192, 128,  64,  64,  64, 128}},
    /* 16: KATZE */
    {CPU_PROFILE_OPPORTUNIST | CPU_PROFILE_STRIKES_FALLEN | 0x06,
       0,   0,   0, 255,  64,   0,
     { 32,   0, 255, 255,   0,   0,   0},
     {  0,  64, 128, 255,   0, 255,   0,   0}},
    /* 17: HIGHWAYMAN */
    {0x02,
       2,   3,  32, 128,  16, 255,
     { 32, 255,   0,   0,  32, 128,   0},
     { 64, 128, 192, 128,  64,  64,  64, 192}},
    /* 18: CHIHIRO */
    {CPU_PROFILE_OPPORTUNIST | CPU_PROFILE_STRIKES_FALLEN | 0x06,
       1,   2, 255, 192,  24, 128,
     {128, 128,   0,   0,  32, 128,   0},
     { 64, 192, 128,  64,  64, 192, 192, 192}},
    /* 19: UTAMARU */
    {CPU_PROFILE_THROWS_TWO | CPU_PROFILE_STRIKES_FALLEN | 0x02,
       4,   3,  32, 128,  24, 255,
     { 32, 255, 128, 128,  32,   0,   0},
     { 64,  64, 128, 192,  64, 128, 128, 128}},
    /* 20: TONY UMEDA */
    {CPU_PROFILE_REPEATS | CPU_PROFILE_OPPORTUNIST |
     CPU_PROFILE_STRIKES_FALLEN | 0x46,
       0,   1,  32, 192,  32,  48,
     {  0,   0, 128, 255, 128,   0,   0},
     {192, 128, 192,  64,  64, 192, 192, 192}},
    /* 21: ISOHACHI (CPU_PROFILE_ROW_STUNNER) */
    {CPU_PROFILE_STRIKES_FALLEN,
       0,   2,   0,  64,   4, 255,
     { 32, 255, 128, 128,  32, 255,   0},
     {255, 192, 192,  64,  64, 128, 192,  64}},
    /* 22: the armoured one (CPU_PROFILE_ROW_PASSIVE) */
    {CPU_PROFILE_STRIKES_FALLEN,
       0,   4,   0,  64,   2, 128,
     {128, 128, 128, 128,   0, 128,   0},
     {128, 192, 192, 128, 128, 128,  64,  64}},
    /* 23: the second ninja with a sidearm */
    {CPU_PROFILE_STORY_BOSS_2 | CPU_PROFILE_REPEATS |
     CPU_PROFILE_STRIKES_FALLEN | 0x40,
       9,   0,   0, 128,  32,  48,
     {128,  64,   0,   0, 255,   0,   0},
     {  0, 255, 255, 255, 255, 128,  48, 255}},
    /* 24: CHARACTER_LAST_1, the Narukagami's crest */
    {0,
       9,   1, 255, 128,  48,  16,
     {  0,   0, 255, 255, 255,   0,   0},
     {  0, 255, 255, 255, 255, 255, 255, 255}},
    /* 25: the victim, who never fights: practice's opponent
     * (CPU_PROFILE_ROW_PRACTICE) */
    {CPU_PROFILE_DUMMY | CPU_PROFILE_REPEATS,
       9, 255,   0, 128,   0,   0,
     {  0,   0,   0,   0,   0,   0,   0},
     {  0, 255, 255, 255, 255,   0,   0, 255}},
    /* 26: CHARACTER_LAST_2, the Shainto's crest */
    {0,
       9,   1, 255, 128,  48,  16,
     {  0,   0, 255, 255, 255,   0,   0},
     {  0, 255, 255, 255, 255, 255, 255, 255}},
    /* 27: chanbara's first kind (CPU_PROFILE_ROW_CHANBARA_FIRST) */
    {CPU_PROFILE_CHANBARA_1 | CPU_PROFILE_REPEATS | CPU_PROFILE_STRIKES_FALLEN,
       9,   1,   0, 128,  24, 128,
     {255,   0,   0,   0,   0,   0,   0},
     {  0, 255, 255, 255,   0,   0,   0, 255}},
    /* 28: chanbara's second kind */
    {CPU_PROFILE_CHANBARA_2 | CPU_PROFILE_REPEATS | CPU_PROFILE_STRIKES_FALLEN,
       9,   1,   0, 128,  16, 255,
     {255,   0, 128,   0,  64,   0,   0},
     {  0, 255, 255, 255,   0,   0,   0, 255}},
    /* 29: chanbara's third kind */
    {CPU_PROFILE_CHANBARA_3 | CPU_PROFILE_REPEATS | CPU_PROFILE_STRIKES_FALLEN,
       9,   1, 255, 128,   8,  64,
     {255, 255,   0,   0,  64, 128,   0},
     {  0, 255, 255, 255,   0,   0,   0, 255}},
    /* 30: chanbara's fourth kind */
    {CPU_PROFILE_CHANBARA_4 | CPU_PROFILE_REPEATS | CPU_PROFILE_STRIKES_FALLEN,
       9,   1,   0, 128,  16, 255,
     {128, 255,   0, 128,   0, 128,   0},
     {  0, 255, 255, 255,   0,   0,   0, 255}},
    /* 31: chanbara's last opponent */
    {CPU_PROFILE_CHANBARA_LAST | CPU_PROFILE_REPEATS |
     CPU_PROFILE_STRIKES_FALLEN | 0x02,
       9,   1, 255, 128,  48,  16,
     {  0,   0, 255, 255, 255,   0,   0},
     {255, 255, 255, 255, 255, 255, 255, 255}}
};
/* clang-format on */

/* The file's small data: the initialisers of three locals (cpu_BuildPad's
 * bits, cpu_Decide's walks and steps), which the original's compiler put in
 * small data and this build's would not. */
/* towards, away, side A, side B */
static CpuWalkButtons g_cpu_walk_buttons = {{13, 15, 12, 14}};
static u8 g_cpu_mirror_walks[4] = {
    CPU_ACTION_HOLD_TOWARD, CPU_ACTION_HOLD_AWAY, CPU_ACTION_HOLD_SIDE_B,
    CPU_ACTION_HOLD_SIDE_A};
static u8 g_cpu_mirror_steps[4] = {
    CPU_ACTION_STEP_TOWARD, CPU_ACTION_STEP_AWAY, CPU_ACTION_STEP_SIDE_B,
    CPU_ACTION_STEP_SIDE_A};
