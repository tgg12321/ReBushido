#ifndef CPU_H
#define CPU_H

/* The CPU opponent (cpu.c).
 *
 * A fighter the CPU plays is an ordinary fighter whose pad is made up every
 * frame by cpu_BuildPad. The CPU looks at what the opponent is doing (flags
 * in Fighter.cpu_flags, the threat of an attack or of something thrown),
 * feels out the ground around it (eight probes), and then holds buttons:
 * either a pad script, a short recorded sequence of presses and waits, or
 * what cpu_Decide works out for the frame. Attacks are scripts from the
 * fighter's motion file, one list for each stance, each with the distances
 * it is good for; guards, side steps, throws and the rest are the built-in
 * scripts at the end of this header.
 *
 * How well it plays is its level (0 to 0x1000) and its profile, one for each
 * character and a few more for the opponents of single modes. It remembers
 * which attacks the opponent keeps using and guards those better. To reach
 * an opponent behind something it walks round the stage's obstacles, which
 * each stage lists as chains of corner points. */

#include "common.h"
#include "pad.h"

struct Fighter;

/* ---- getting about ---- */

/* A place the CPU walks to on its way to the opponent. */
typedef struct CpuWaypoint {
    s16 x;
    s16 z;
    u8 kind; /* CPU_GOAL_... */
} CpuWaypoint;

/* CpuWaypoint.kind, Fighter.cpu_goal_kind */
enum {
    CPU_GOAL_OPPONENT = 1, /* the opponent itself: near enough will do */
    CPU_GOAL_PLACE = 2,    /* a place to stand: a point of a circling */
};

#define CPU_ROUTE_NODES 8

/* The CPU's route: which obstacle is in the way (a chain of the stage's set
 * and the edge of it that is crossed) and the waypoints round it, walked
 * from the last to the first. */
typedef struct CpuRoute {
    u8 poly;  /* the chain */
    u8 vtx;   /* the first corner of the edge */
    u8 count; /* waypoints in node[] */
    CpuWaypoint node[CPU_ROUTE_NODES];
} CpuRoute; /* 0x34 bytes */

/* An obstacle of a stage, as the CPU knows it: a chain of corners in x and
 * z. A chain is a ring unless NAV_CHAIN_OPEN. */
typedef struct NavChain {
    u8 flags;
    u8 kind;   /* copied into the waypoints made round it */
    u8 margin; /* how far out from a corner to walk, in units of 40 */
    u8 nvtx;
    s16 (*vtx)[2];
} NavChain;

#define NAV_CHAIN_OPEN 0x80

/* A stage's obstacles. */
typedef struct NavSet {
    u8 nchains;
    NavChain *chains;
    u8 unk8[4]; /* zero for every stage; no code reads it */
} NavSet;

/* Each stage's (cpu_data.c). */
extern NavSet g_cpu_stage_obstacles[STAGE_COUNT];

/* No obstacle is in the way (Fighter.cpu_obstacle_distance). */
#define CPU_NO_OBSTACLE 100000

/* What a probe of the ground found (Fighter.cpu_probes): the way is clear or
 * blocked by something, and there is or is not ground beyond. */
enum CpuProbeResult {
    CPU_FOUND_LEVEL = 0, /* clear, and the ground goes on level */
    CPU_FOUND_NO_FLOOR = 1,
    CPU_FOUND_BLOCKED_NO_FLOOR = 2,
    CPU_FOUND_DROP = 3, /* clear, but the ground is lower there */
    CPU_FOUND_BLOCKED = 4,
    CPU_FOUND_BLOCKED_STEP = 5, /* blocked far off by a change of height */
};

/* The probes, by what cpu_Decide uses them for. The first seven are from the
 * CPU's fighter, the last from the opponent: how far and which way each one
 * looks is in g_cpu_probe_table. "A" and "B" are the two sides, as the pad's
 * FIGHTER_PAD_SIDE_A and _B step to them. */
enum CpuProbe {
    CPU_PROBE_TOWARD = 0,
    CPU_PROBE_SIDE_B = 1,
    CPU_PROBE_BACK_B = 2,
    CPU_PROBE_AWAY = 3,
    CPU_PROBE_BACK_A = 4,
    CPU_PROBE_SIDE_A = 5,
    CPU_PROBE_FAR_TOWARD = 6,
    CPU_PROBE_PAST_OPPONENT = 7, /* the ground behind the opponent */
    CPU_PROBE_COUNT = 8,
};

/* Eight probes: how far (in units of 256) and which way (in sixteenths of a
 * turn; CPU_PROBE_FROM_OPPONENT: from the opponent, not from the fighter). */
#define CPU_PROBE_FROM_OPPONENT 0x10
extern u8 g_cpu_probe_table[CPU_PROBE_COUNT][2];

/* Ground within this much of the fighter's own height is level with it. */
#define CPU_LEVEL_HEIGHT 1000

/* Where the opponent stands (Fighter.cpu_opponent_height). */
enum CpuOpponentHeight {
    CPU_OPPONENT_LEVEL = 0,
    CPU_OPPONENT_ABOVE = 1,
    CPU_OPPONENT_BELOW = 2,
};

/* ---- what it sees ---- */

/* Fighter.cpu_flags, worked out afresh every frame but for the three that
 * last as long as the attack they are about. */
enum CpuFlag {
    CPU_EAGER = 1,           /* this frame's chance to attack came up */
    CPU_SEES_OPENING = 2,    /* and the opponent cannot answer */
    CPU_IMPATIENT = 4,       /* it has waited long enough */
    CPU_ALERT = 8,           /* this frame it reacts to a threat */
    CPU_SHARP = 0x10,        /* and picks the right guard */
    CPU_KNOWS_ATTACK = 0x20, /* the opponent has used this attack often */
    CPU_KNOWS_ATTACK_WELL = 0x40,
    /* the opponent is attacking, or has the CPU in a paired move */
    CPU_UNDER_ATTACK = 0x80,
    CPU_RUNNING = 0x100,
    CPU_OPPONENT_RUNNING = 0x200,
    CPU_DOWN = 0x400,
    CPU_OPPONENT_DOWN = 0x800,
    CPU_STANDING = 0x1000,
    CPU_OPPONENT_STANDING = 0x2000,
    CPU_WALKING = 0x4000,
    CPU_OPPONENT_WALKING = 0x8000,
    CPU_STEPPING = 0x10000,
    /* the opponent stands in a stance that is better stepped round */
    CPU_STEPS_ROUND = 0x20000,
    /* the practice opponent has been told to change its stance */
    CPU_TOLD_TO_CHANGE = 0x40000,
};

/* How near the opponent's attack is to landing (Fighter.cpu_attack_threat). */
enum CpuThreat {
    CPU_THREAT_NONE = 0,
    CPU_THREAT_NEAR = 1, /* it will reach */
    CPU_THREAT_MID = 2,  /* it may: stepping back would clear it */
    CPU_THREAT_FAR = 3,  /* it will fall short */
    CPU_THREAT_MET = 4,  /* dealt with */
};

/* An attack is a threat from this many frames before it can hit. */
#define CPU_THREAT_FRAMES 8

/* The same for something thrown (Fighter.cpu_projectile_threat), which is
 * also CPU_THREAT_MET once dealt with. */
enum CpuProjectile {
    CPU_PROJECTILE_NONE = 0,
    CPU_PROJECTILE_COMING = 1,
    CPU_PROJECTILE_CLOSE = 2,
};

/* Fighter.cpu_incoming_attack when it is not one of the opponent's motions. */
enum {
    CPU_INCOMING_NONE = -1,
    CPU_INCOMING_AFTER_PARRY = 0xFE, /* any attack that follows a parry */
    CPU_INCOMING_PAIRED = 0xFF,      /* being caught in a paired move */
};

#define CPU_KNOWN_ATTACKS 8

/* ---- what it does ---- */

/* Footwork the CPU has decided on (Fighter.cpu_action). */
enum CpuAction {
    CPU_ACTION_HOLD_AWAY = 0,
    CPU_ACTION_HOLD_TOWARD = 1,
    CPU_ACTION_HOLD_SIDE_B = 2,
    CPU_ACTION_HOLD_SIDE_A = 3,
    CPU_ACTION_STEP_AWAY = 4,
    CPU_ACTION_STEP_TOWARD = 5,
    CPU_ACTION_STEP_SIDE_B = 6,
    CPU_ACTION_STEP_SIDE_A = 7,
    CPU_ACTION_LEAP = 8, /* the leaping attack, from some way off */
};

/* Why a pad script is running (Fighter.cpu_script_kind): it decides what
 * may cut the script short. */
enum CpuScriptKind {
    CPU_SCRIPT_KIND_ATTACK = 0,        /* dropped when the opponent gets away */
    CPU_SCRIPT_KIND_STRIKE_FALLEN = 1, /* dropped when the opponent is up */
    CPU_SCRIPT_KIND_THROW = 2,
    CPU_SCRIPT_KIND_GUARD = 3,
    CPU_SCRIPT_KIND_FOOTWORK = 4,
};

/* A pad script is bytes: 0x80 ends it, another byte with bit 7 waits that
 * many frames (less the bit), any other presses the button whose number is
 * in the low four bits, or lets it go if bit 4 is set. */
#define CPU_SCRIPT_END 0x80
#define CPU_SCRIPT_WAIT 0x80
#define CPU_SCRIPT_RELEASE 0x10
#define CPU_SCRIPT_BUTTON 0xF

/* An attack in a motion file's list of CPU scripts: four bytes, then
 * (if the fifth is CPU_ATTACK_FOR_SOME) four of a mask of the characters it
 * is for, then the pad script. */
enum {
    CPU_ATTACK_FLAGS = 0, /* CPU_ATTACK_KIND and CPU_ATTACK_NEEDS_ARM */
    /* how near and how far it works, in MOTION_REACH_UNITs */
    CPU_ATTACK_NEAR = 1,
    CPU_ATTACK_FAR = 2,
    CPU_ATTACK_CLASS = 3, /* high four bits: its class, 1 up */
    CPU_ATTACK_HEAD = 4,
};

#define CPU_ATTACK_KIND 7
#define CPU_ATTACK_NEEDS_ARM 0x80
#define CPU_ATTACK_FOR_SOME 0x40

/* CPU_ATTACK_KIND: kind 0 is used against a standing opponent in range,
 * 1 to 4 when the opponent is nearer than the attack's near distance, and
 * the rest between its distances. */
enum CpuAttackKind {
    CPU_ATTACK_KIND_DIRT = 5,  /* needs loose ground underfoot */
    CPU_ATTACK_KIND_THROW = 6, /* needs a sub-weapon */
};

/* The kinds of footwork a profile weighs (CpuProfile.footwork_weights);
 * each becomes one of the CPU_ACTIONs, by which way there is room. */
enum CpuFootwork {
    CPU_FOOTWORK_WALK = 0,      /* back or forward */
    CPU_FOOTWORK_SIDLE = 1,     /* to either side */
    CPU_FOOTWORK_STEP = 2,      /* a step back or forward */
    CPU_FOOTWORK_SIDE_STEP = 3, /* a step to either side */
    CPU_FOOTWORK_LEAP = 4,
    CPU_FOOTWORK_MIRROR = 5, /* whatever the opponent does */
    CPU_FOOTWORK_COUNT = 7,  /* the seventh is used by no profile */
};

/* Distances the CPU judges by. */
/* farther than anything on a stage: a gun's range, a way that is no way */
#define CPU_ANY_DISTANCE 100000
/* the leaping attack is made from between these */
#define CPU_LEAP_NEAR 3000
#define CPU_LEAP_FAR 5000
/* how far off it will throw dirt, and see dirt thrown at it */
#define CPU_DIRT_RANGE 4500
/* how near a fallen opponent it must be to strike at it */
#define CPU_STRIKE_FALLEN_RANGE 4000
/* near enough to a place it is walking to (and to a fallen opponent it
 * leaves alone) */
#define CPU_PLACE_RANGE 2000
/* a waypoint is reached within this of it, walking and running */
#define CPU_WAYPOINT_RADIUS 300
#define CPU_WAYPOINT_RUN_RADIUS 1000

/* The CPU does not attack in its first frames of a fight. */
#define CPU_WARMUP_FRAMES 60

/* ---- profiles ---- */

/* How a character plays. The weights are chances out of 256. */
typedef struct CpuProfile {
    u16 flags; /* CPU_PROFILE_... */
    /* 0 to 4 for the twenty characters that have a name, 9 for the rest and
     * for chanbara's; no code reads it */
    u8 unk2;
    /* after this many setbacks (hurt, parried, beaten back) it changes its
     * stance; 0: never */
    u8 setback_limit;
    u8 lock_retreat_chance; /* in an even blade lock: to back out of it */
    /* where between its nearest and farthest reach it likes to stand */
    u8 range_fraction;
    u8 footwork_chance; /* how readily it takes up footwork */
    u8 range_tolerance; /* how far off that distance it lets itself be */
    /* each kind of footwork (enum CpuFootwork); the last is not used */
    u8 footwork_weights[CPU_FOOTWORK_COUNT];
    /* each kind of attack (CPU_ATTACK_KIND); [5] and [6] also the chance to
     * throw dirt and to throw a sub-weapon from a standstill */
    u8 attack_weights[8];
} CpuProfile; /* 0x18 bytes */

/* CpuProfile.flags. Only the ones the code tests by themselves are named;
 * what bits 1, 2 and 6 are for is not known. */
#define CPU_PROFILE_STRIKES_FALLEN 1
#define CPU_PROFILE_THROWS_TWO 0x10
/* takes any lapse of the opponent's as an opening */
#define CPU_PROFILE_OPPORTUNIST 0x20
#define CPU_PROFILE_REPEATS 0x80 /* may use the same attack twice running */
/* The opponents that fight to a plan: the attacks they may use are picked by
 * class, and they are patient for a set time (CpuStoryLimits). */
#define CPU_PROFILE_STORY_BOSS 0x300 /* two of story mode's */
#define CPU_PROFILE_STORY_BOSS_1 0x100
#define CPU_PROFILE_STORY_BOSS_2 0x200
#define CPU_PROFILE_CHANBARA 0x7C00 /* chanbara's five, a bit each */
#define CPU_PROFILE_CHANBARA_1 0x400
#define CPU_PROFILE_CHANBARA_2 0x800
#define CPU_PROFILE_CHANBARA_3 0x1000
#define CPU_PROFILE_CHANBARA_4 0x2000
#define CPU_PROFILE_CHANBARA_LAST 0x4000
/* chanbara's first two kinds, the simplest of the planned opponents */
#define CPU_PROFILE_CHANBARA_LOW                                               \
    (CPU_PROFILE_CHANBARA_1 | CPU_PROFILE_CHANBARA_2)
/* all of them but chanbara's last */
#define CPU_PROFILE_PLANNED_LESSER 0xBF00
#define CPU_PROFILE_DUMMY 0x8000 /* practice: stands and does as told */
#define CPU_PROFILE_PLANNED 0xFF00

/* A fighter's profile is its character's, but for practice's opponent and
 * chanbara's. The ones the code asks for by number: */
enum {
    /* the vanisher (ROSTER_VANISHER): never goes after the opponent */
    CPU_PROFILE_ROW_VANISHER = 0xA,
    CPU_PROFILE_ROW_STUNNER = 0x15, /* ISOHACHI */
    CPU_PROFILE_ROW_PASSIVE = 0x16, /* never guards, never turns to face */
    CPU_PROFILE_ROW_PRACTICE =
        0x19, /* the row of the victim, who never fights */
    CPU_PROFILE_ROW_CHANBARA_FIRST = 0x1B,
};

/* Fighter.cpu_level at its highest. */
#define CPU_LEVEL_MAX 0x1000

/* The chapter of the story from which a boss steps back out of an attack's
 * way and uses its sidearm, and the one from which it steps round. */
#define CPU_BOSS_EVADES_CHAPTER 6
#define CPU_BOSS_SIDESTEPS_CHAPTER 7

/* A story boss's plan for one stage of the story, by its stance. */
typedef struct CpuStoryLimits {
    u8 known_threshold; /* Fighter.cpu_known_threshold */
    u8 idle_limit;      /* Fighter.cpu_idle_limit */
    u16 mask;           /* the classes of attack it may use */
} CpuStoryLimits;

extern CpuStoryLimits g_cpu_story_limits[3][8];
/* The same two limits for chanbara, by how far up the ladder. */
extern u8 g_cpu_chanbara_limits[20][2];

/* ---- tables (cpu_data.c) ---- */

/* How far a weapon reaches, for working out whether an attack will land:
 * by weapon for an attack of the fighter's own file (in units of 2) and
 * of the common file (in units of 8, signed), and by sub-weapon for an
 * attack with the off hand. */
extern u8 g_cpu_weapon_reaches[];
extern s8 g_cpu_weapon_ranges[];
extern u8 g_cpu_sub_weapon_reaches[16];
/* For each weapon of the opponent's, a bit for each stance class of its
 * that is better stepped round than met. */
extern u8 g_cpu_side_step_stances[];
/* The directions of the pad, in eighths of a turn. */
extern s32 g_cpu_heading_buttons[8];

/* Moves the CPU follows up: its own move kind, the opponent's, a distance
 * (in units of 16, past the weapon's range) and flags. */
enum {
    CPU_FOLLOW_OWN_KIND = 0,
    CPU_FOLLOW_OTHER_KIND = 1, /* or CPU_FOLLOW_ANY_KIND */
    CPU_FOLLOW_DISTANCE = 2,
    CPU_FOLLOW_FLAGS = 3,
};

#define CPU_FOLLOW_ANY_KIND 0xFF
#define CPU_FOLLOW_AT_END 1  /* only at the move's end */
#define CPU_FOLLOW_ATTACKS 2 /* with an attack */
#define CPU_FOLLOW_ALWAYS 4  /* however the dice fall */
#define CPU_FOLLOW_FACING 8  /* only facing the opponent */
extern u8 g_cpu_follow_ups[8][4];
/* The classes of attack chanbara's opponents may use, [stance class][how
 * far up the ladder]; and practice's patterns of them, [row][option], the
 * row by [weapon][stance class]. */
extern u16 g_cpu_chanbara_attacks[3][23];
extern u8 g_cpu_practice_rows[6][3];
extern s32 g_cpu_practice_attacks[3][8];

/* The built-in pad scripts. */
extern u8 g_cpu_script_guard_a[];     /* attack 1 */
extern u8 g_cpu_script_guard_b[];     /* attack 2 */
extern u8 g_cpu_script_step_side_b[]; /* a direction tapped twice */
extern u8 g_cpu_script_step_side_a[];
extern u8 g_cpu_script_step_toward[];
extern u8 g_cpu_script_step_away[];
extern u8 g_cpu_script_strike_fallen[]; /* R1 */
extern u8 g_cpu_script_leap[];          /* towards, R2, R1 */
extern u8 g_cpu_script_running_leap[];  /* L1, R2, R1 */
extern u8 g_cpu_script_throw[];         /* R2, attack 1 */
extern u8 g_cpu_script_throw_twice[12];
extern u8 g_cpu_script_throw_dirt[]; /* R2, attack 2 */

/* ---- cpu.c ---- */

/* Sets up the fighters the CPU plays (after their motion is attached). */
extern void cpu_InitFighters(void);
extern void cpu_Init(s32 fighter_index, u16 *scripts, u16 *other_scripts);
/* The CPU's pad for this frame. */
extern void cpu_BuildPad(s32 fighter_index, PadState *pad_out);
/* Forgets which attacks the opponent likes; forgets half. */
extern void cpu_ForgetAttacks(s32 fighter_index);
extern void cpu_FadeAttacks(s32 fighter_index);
/* The "easy CPU" option is on (menu). */
extern u32 cpu_IsEasy(void);

#if RB_PORT
/* For the port's CPU (port_cpu.h): the eight probes of the ground round
 * the fighter, two a frame, into Fighter.cpu_probes; how far the attack
 * the opponent is making reaches, by its motion and its weapon. */
extern void cpu_PortProbeGround(struct Fighter *fighter);
extern s32 cpu_PortAttackReach(struct Fighter *fighter);
#endif

#endif /* CPU_H */
