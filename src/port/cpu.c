/* The port's own CPU (port_cpu.h; docs/CPU.md): a player of the game who
 * sees the fight as a player does and answers it with a pad.
 *
 * What it sees. Each frame it writes down what can be seen of the
 * opponent: where he stands, the motion he is in and how far into it, and
 * his wounds (a limp, a hanging arm). It acts on what it wrote down its
 * reaction time ago, never on this frame's: a player sees a frame only
 * once it is drawn, and answers it later still. What it knows of a motion
 * it sees (how far it reaches, when its blade will come, which of the two
 * attack buttons meets it) is what a player who knows the game's moves
 * knows; a level that knows less guesses wrong more often, and less often
 * for an attack it has seen before in the match. Of itself it knows what
 * a player knows of his own fighter: everything. It never reads the
 * other's pad, the move he has asked for and not begun, nor the game's
 * dice, and its own dice are its own (a seed in the state).
 *
 * What it does, each frame, in order: what it cannot help (down, a blade
 * lock, caught in a paired move); a threat (an attack seen coming that
 * will reach it: a step back out of reach if there is time, the guard if
 * not); an opening (an attack that missed and has not recovered, a
 * stagger, a reload: a strike if its blade reaches in time); the distance
 * (it walks to where it likes to stand against this opponent, just out of
 * his reach, and turns to face him); and now and then a strike of its
 * own, of an attack that suits the distance, picked by how well each has
 * done in the match. The levels differ in these choices (g_port_cpu_levels),
 * and from the third up it reads the opponent's habits: how readily he
 * strikes at a fighter who comes within his reach, which it then draws
 * out and punishes.
 *
 * Its patience runs out (port_CpuPressure; docs/CPU.md, "Committing"):
 * the longer nothing happens, the more it is behind, the later in the
 * fight, the harder it presses, coming in to within its own reach and
 * striking first, lunging in from just past it, feinting from the
 * veteran up; and from the adept up it meets an opponent who walks into
 * its reach with a blow. So two of it never stand off each other for a
 * whole fight.
 *
 * Everything here is the game's state (config/port_state.txt): the
 * memory of each fighter, its dice, what it saw. A memory is a fighter's,
 * not a place's: in two on two (port_duo.h) the four fighters pass through
 * the game's two places, and each keeps his own. */
#include "common.h"
#include "bb2.h"

/* ---- the levels ---- */

typedef struct {
    /* frames from seeing a thing to answering it (the fight's frames, 30
     * a second), and a few more at random */
    u8 reaction;
    u8 jitter;
    /* of 256: the chance to know which button meets an attack never seen
     * before, and what each time it has been seen adds */
    u8 read;
    u8 read_gain;
    /* how far from where it likes to stand it lets itself be */
    s16 tolerance;
    /* where it likes to stand, past the opponent's reach: at random up
     * to `spread` either way of `margin` */
    s16 margin;
    s16 spread;
    /* of 256: the chance to take an opening it sees; and how many frames
     * a missed attack must have still to go for it to strike at it */
    u8 punish;
    u8 punish_frames;
    /* of 1024, a frame: the chance to strike first, in range */
    u8 initiative;
    /* of 256: the chance to step back from an attack it has time for,
     * rather than meet it */
    u8 evade;
    /* what it allows past an attack's farthest distance as it judges
     * whether it will reach (less than nothing: it keeps inside it) */
    s16 slack;
    /* TRUE: picks its attacks by how they have done; reads the opponent's
     * habits; draws him out */
    u8 learns;
    u8 reads_habits;
    u8 baits;
    /* TRUE: stands off his farthest attack, not his plain ones' reach */
    u8 far_reach;
    /* TRUE: backs off from an attack coming while it waits to guard */
    u8 backs_off;
    /* its patience: frames of nothing happening before it begins to press,
     * and frames more till it presses all it will */
    u16 patience;
    u16 ramp;
    /* of 1024, a frame, pressing all it will: what pressing adds to the
     * chance to strike first, in range; the chance to step in from just
     * past its reach and strike (a lunge); to step into his reach and out
     * again, to draw him (a feint) */
    u8 aggression;
    u8 lunge;
    u8 feint;
    /* of 256: the chance to meet him with a blow as he is seen to walk
     * into its reach */
    u8 meet;
    /* TRUE: pressing, it waits just outside his reach and goes in by
     * lunges, not by walking into a blow */
    u8 outside;
    /* TRUE: weighs each of its ways in (meeting him, striking first,
     * lunging, an opening) by what it has come to against him: landed, or
     * hurt in it (port_CpuTacticWeight) */
    u8 weighs_tactics;
    /* TRUE: strikes at an opening only with an attack it knows to reach
     * him before he is over it (its attacks' speeds, learned as it uses
     * them) */
    u8 times_openings;
    /* of 256: the chance to step aside out of the line of something seen
     * thrown at it (a sub-weapon, dirt, a shot), where there is time */
    u8 dodge;
    /* of 1024 a frame, standing off him: the chance to throw a sub-weapon
     * (where it has one), to throw dirt (standing on loose ground), and,
     * pressing, to leap in with the leaping attack from its distance */
    u8 throws;
    u8 dirt;
    u8 leap;
    /* TRUE: an opening seen beyond its reach (his attack missed far off,
     * he staggers at its dirt, reloads, gets up) is run in on and struck,
     * and a long one far off is leapt at or thrown at */
    u8 rushes;
    /* TRUE: crowded at an edge or a wall with him in reach, it circles
     * round him to open ground, as the game's CPU does
     * (cpu_PortPlanCircling); and with his back to a drop or a wall it
     * presses in on him there */
    u8 circles;
    /* TRUE: an attack it judges will fall short (it stands past the
     * motion's reach by `holds_margin`) is let go by, standing, ready to
     * punish it, not stepped back from or guarded */
    u8 holds;
    s16 holds_margin;
    /* TRUE: reads how he answers it (port_CpuReadAnswers): one who strikes
     * at it as it comes into his reach is feinted at, to make him miss;
     * one who goes straight on from a miss into another attack is not
     * struck at in the first till he cannot */
    u8 reads_answers;
    /* not 0: a strike at him free to answer it (meeting him, striking
     * first, after a lunge's step) is made only with an attack that
     * brings its blade within this many frames, as it knows its moves
     * (port_CpuBladeFrames): a slow one is seen coming and met */
    u8 quick;
} PortCpuLevel;

static PortCpuLevel g_port_cpu_levels[PORT_CPU_LEVELS + 1] = {
    {0},
    /* novice: slow, unsure of the distance, guesses its guards. A
     * beginner's reaction to an attack he must first make out: 11 to 14
     * frames, 0.37 to 0.47 s (at 9 the adept beat it 53% of 96 fights;
     * at 11, 67% of 192: docs/CPU.md, "Measuring the levels"); and
     * impatient, pressing after 1.3 s of nothing and swinging from too
     * far, as a beginner does ("Committing") */
    {.reaction = 11,
     .jitter = 3,
     .read = 96,
     .read_gain = 8,
     .tolerance = 900,
     .margin = 400,
     .spread = 1400,
     .punish = 70,
     .punish_frames = 3,
     .initiative = 22,
     .evade = 60,
     .slack = 500,
     .patience = 40,
     .ramp = 120,
     .aggression = 64,
     .lunge = 24,
     .dodge = 40,
     .throws = 4,
     .dirt = 3,
     .leap = 6},
    /* adept: learns which of its attacks do */
    {.reaction = 8,
     .jitter = 2,
     .read = 150,
     .read_gain = 16,
     .tolerance = 600,
     .margin = 300,
     .spread = 900,
     .punish = 140,
     .punish_frames = 6,
     .initiative = 12,
     .evade = 130,
     .slack = 200,
     .learns = TRUE,
     .patience = 100,
     .ramp = 150,
     .aggression = 48,
     .lunge = 24,
     .meet = 60,
     .dodge = 100,
     .throws = 3,
     .dirt = 2,
     .leap = 4,
     .circles = TRUE},
    /* veteran: reads the opponent's habits; waits 8 s for an opening
     * before it presses, meets him as he walks in, feints */
    {.reaction = 7,
     .jitter = 2,
     .read = 200,
     .read_gain = 24,
     .tolerance = 400,
     .margin = 200,
     .spread = 500,
     .punish = 210,
     .punish_frames = 8,
     .initiative = 6,
     .evade = 190,
     .slack = 0,
     .learns = TRUE,
     .reads_habits = TRUE,
     .patience = 240,
     .ramp = 240,
     .aggression = 56,
     .lunge = 28,
     .feint = 20,
     .meet = 220,
     .dodge = 170,
     .throws = 2,
     .dirt = 2,
     .leap = 2,
     .circles = TRUE},
    /* master: a quick player's reaction, never less; draws a hasty
     * opponent out; waits longest, and pressing stays outside his reach
     * and lunges in */
    {.reaction = 6,
     .jitter = 1,
     .read = 230,
     .read_gain = 32,
     .tolerance = 250,
     .margin = 150,
     .spread = 250,
     .punish = 255,
     .punish_frames = 10,
     .initiative = 2,
     .evade = 250,
     .slack = -300,
     .learns = TRUE,
     .reads_habits = TRUE,
     .baits = TRUE,
     .patience = 300,
     .ramp = 240,
     .aggression = 64,
     .lunge = 32,
     .feint = 32,
     .meet = 250,
     .outside = TRUE,
     .weighs_tactics = TRUE,
     .quick = 22,
     .dodge = 220,
     .throws = 2,
     .dirt = 3,
     .leap = 1,
     .rushes = TRUE,
     .circles = TRUE,
     .reads_answers = TRUE},
};

/* No level reacts quicker than this: a sixth of a second. */
#define REACTION_LEAST 5

/* Distances it judges by, in the game's units. */
/* a fallen opponent this near (and his reach more) is rolled away from */
#define ROLL_AWAY_MARGIN 1000
/* an attack seen coming is a threat within its reach and this */
#define THREAT_MARGIN 600
/* an attack picked is given up if he has gone this far past its reach */
#define GIVE_UP_MARGIN 300
/* an attack of the list is waited for this many frames to begin (it may
 * begin with a step) before it is counted as come to nothing */
#define OWN_BEGIN_FRAMES 30
/* he is counted as standing within his reach of it within this of it,
 * and an attack he begins as begun there within this */
#define HABIT_STANDING_MARGIN 300
#define HABIT_ATTACK_MARGIN 600
/* an opening is struck at within its own reach and this */
#define OPENING_MARGIN 600
/* an opening is struck at with an attack whose blade comes within the
 * frames he has still to go and these */
#define OPENING_GRACE 4
/* an attack's blade frames, guessed before it has used it: one that
 * crouches first, and any other; a script is looked into this far */
#define BLADE_GUESS_CROUCHING 24
#define BLADE_GUESS 16
#define SCRIPT_LOOK_BYTES 16
/* the nearest it ever likes to stand */
#define NEAREST_WANTED 600
/* the master stands this far within a hasty opponent's reach to draw
 * him; any level from the veteran up this far within its own against a
 * patient one */
#define BAIT_DEPTH 200
#define PRESS_DEPTH 400

/* Its habits are read once he has stood within his reach of it this
 * long; he is hasty at an attack a second or more there (in sixteenths
 * of one a second), patient under six sixteenths. */
#define HABIT_FRAMES_LEAST 90
#define HABIT_RATE_ONE 16
#define HABIT_RATE_HASTY 16
#define HABIT_RATE_PATIENT 6
#define FRAMES_A_SECOND 30
/* Pressing (port_CpuPressure): all it will is this; a wound it has more
 * than he has seen to have adds this much; from this frame of the fight
 * the clock adds to it, all by this frame (the tournament judges a fight
 * of two CPUs at 5400: PORT_TOURNAMENT_JUDGE_FRAMES) */
#define PRESSURE_ALL 256
#define PRESSURE_A_WOUND 64
#define PRESSURE_CLOCK_FROM 1800
#define PRESSURE_CLOCK_ALL 4500
/* a lunge is from no further than this past its own reach, and only
 * pressing this much; a feint, from no further than this past his,
 * pressing between these */
#define LUNGE_DISTANCE 1500
/* a level that presses from outside his reach waits this far past it */
#define OUTSIDE_MARGIN 200
#define LUNGE_PRESSURE 96
/* an opening just out of its reach is stepped in on if he has this many
 * frames still to go in it */
#define LUNGE_FRAMES_LEAST 14
#define FEINT_DISTANCE 1200
#define FEINT_PRESSURE_LEAST 32
#define FEINT_PRESSURE_MOST 224
/* From afar (port_CpuFromAfar): a thing thrown or a shot is stepped
 * aside from with this many frames to go or more; a sub-weapon is thrown
 * (from where it cannot strike) from between these, dirt from this near, and a
 * level that rushes throws this many times as readily at an opening; a gun
 * keeps this far past his reach */
#define DODGE_FRAMES_LEAST 4
/* a throw seen begun is stepped aside from this far off and further */
#define DODGE_THROW_NEAR 2500
#define THROW_NEAR 2500
#define THROW_FAR 7000
#define DIRT_NEAR 2000
#define RUSH_THROWS 8
#define GUN_STANDOFF 2000
/* an opening is rushed at if he has this many frames still to go in it,
 * from no further than this, running from this past its reach; a leap
 * is made at one with this many; dirt is followed in on for these */
#define RUSH_FRAMES_LEAST 12
#define RUSH_FAR 6000
#define RUSH_RUN 2500
#define LEAP_FRAMES_LEAST 24
#define DIRT_RUSH_FRAMES 45
/* crowded at an edge, it circles this many frames; with his back to one
 * it goes in on him from within his reach and this */
#define CIRCLE_FRAMES 24
#define CORNER_DISTANCE 1500
/* His answers (port_CpuReadAnswers): a blow of his begun within this many
 * frames of its coming into his reach (and seen within this many of its
 * beginning) meets it; each is read once it has seen this many */
#define ENTRY_FRAMES 24
#define ENTRY_SEEN_FRAMES 4
#define ANSWERS_LEAST 2
/* one who meets it is feinted at this many times as readily; one who
 * follows a miss is struck at in it only with this many frames more */
#define MEETER_FEINTS 4
#define FOLLOWER_FRAMES 6
/* the place it likes is drawn again this often */
#define MARGIN_FRAMES 60
/* this far past where it likes to be it runs */
#define RUN_DISTANCE 9000

/* An attack of the motion file's list of the kinds that are made at the
 * distances between its two (cpu.h, CPU_ATTACK_KIND: 0 and 7). */
#define CPU_ATTACK_KIND_LAST 7
/* the list's mask of characters is this many bytes, with its mark */
#define CPU_ATTACK_MASK_BYTES 5

/* Chances are out of these. */
#define CHANCE_BYTE 256
#define CHANCE_FRAME 1024
#define CHANCE_MOST 255
/* a blow at a fallen opponent: an eighth as likely a frame as an opening */
#define FALLEN_CHANCE_SHARE 8

/* the most a 16-bit count or distance holds */
#define S16_MOST 0x7FFF
/* the golden ratio's, to spread a seed's bits; a seed for no seed */
#define SEED_SPREAD 0x9E3779B9U
#define SEED_NONE 0x2545F491U

/* ---- what it keeps ---- */

/* Nothing thrown is coming (PortCpuSeen.projectile); something thrown is
 * seen coming at it within this of its height. */
#define PROJECTILE_NONE S16_MOST
#define PROJECTILE_HEIGHT 2000

/* How many frames of what it saw it keeps: more than any reaction. */
#define SEEN_FRAMES 16
/* The opponent's attacks it has seen, by motion. */
#define KNOWN_MOTIONS 24
/* Its own attacks' record, by stance and place in the stance's list. */
#define OWN_STANCES 3
#define OWN_ATTACKS 24

/* What can be seen of the opponent in a frame. */
typedef struct {
    s32 x;
    s32 z;
    s32 reach; /* how far the motion he is in reaches (cpu_PortAttackReach) */
    u16 move_kind;
    u16 motion_index;
    s16 move_frame;
    s16 speed;
    s16 stance;
    u8 hit_start; /* the frames of the motion his blade hits in */
    u8 hit_end;
    u8 frames; /* how long the motion is */
    u8 attack_kind;
    u8 wounds; /* legs, arm and body, and 4 for an arm that hangs */
    u8 seen;   /* 1: there is something here */
    /* the ground he stands on, and the frames to it of the nearest thing
     * thrown seen coming at it (a sub-weapon, dirt), PROJECTILE_NONE if
     * none */
    s32 y;
    s16 projectile;
    /* 1: his weapon is a gun, with shots in it */
    u8 gun;
} PortCpuSeen;

typedef struct {
    u16 motion;
    u8 count;
} PortCpuKnown;

/* The scripts of presses it plays (cpu.h's form: a button's number, a
 * wait, a release, the end). */
enum {
    SCRIPT_NONE,
    SCRIPT_ATTACK, /* of the motion file's list: at `attack` */
    SCRIPT_STEP_AWAY,
    SCRIPT_STEP_TOWARD,
    SCRIPT_STEP_SIDE_A,
    SCRIPT_STEP_SIDE_B,
    SCRIPT_GUARD_FRONTAL,
    SCRIPT_GUARD_REVERSE,
    SCRIPT_STRIKE_FALLEN,
    SCRIPT_LEAP,  /* the leaping attack (the game's g_cpu_script_leap) */
    SCRIPT_THROW, /* a sub-weapon thrown (g_cpu_script_throw) */
    SCRIPT_DIRT,  /* dirt thrown (g_cpu_script_throw_dirt) */
};

/* Why it began a script (the log's last field, port_CpuScriptWhy). */
enum {
    WHY_NONE,
    WHY_OPENING, /* an opening seen: a missed attack, a stagger */
    WHY_MEET,    /* he walked into its reach */
    WHY_LUNGE,   /* a step in and a blow, pressing */
    WHY_FIRST,   /* a strike of its own, standing */
    WHY_FALLEN,  /* a blow at him down */
    WHY_EVADE,   /* a step back from his attack */
    WHY_GUARD,   /* the guard as his blade comes */
    WHY_FEINT,   /* a step in and out, to draw him */
    WHY_DODGE,   /* a step aside out of the line of a thing thrown, a shot */
    WHY_THROW,   /* a sub-weapon thrown at him */
    WHY_DIRT,    /* dirt thrown at him */
    WHY_LEAP,    /* the leaping attack, from afar */
    WHY_SHOT,    /* a gun fired at him */
};

/* its ways in, weighed: WHY_OPENING to WHY_FIRST */
#define TACTICS (WHY_FIRST + 1)

typedef struct {
    s32 level;  /* 0: the game's CPU plays this place */
    u32 random; /* its dice */
    u32 frames; /* frames it has played */
    u32 last_held;
    PortCpuSeen seen[SEEN_FRAMES];
    /* the script it is playing: which, where in it, how long it waits */
    u8 script;
    u8 why; /* WHY_...: why it began it */
    u8 script_wait;
    u16 script_at;
    u16 attack; /* an attack's script: its offset in the motion file's list */
    s16 attack_far; /* and the distance it was picked for, at the most */
    u32 script_buttons;
    /* what it is holding for a while, and how long still */
    u32 hold;
    s16 hold_frames;
    /* the threat it is answering: the motion, and where it was begun */
    u16 threat_motion;
    u8 threat_answered;
    u8 reaction; /* this one's reaction, with its jitter */
    s16 noticed; /* frames since it noticed the opponent's motion change */
    u16 noticed_motion;
    /* where it likes to stand past his reach, drawn now and then */
    s16 margin;
    s16 margin_frames;
    /* the opponent's habits: frames he stood within his reach of it, and
     * the attacks he began there */
    u16 in_reach_frames;
    u16 in_reach_attacks;
    u16 habit_motion; /* the attack last counted */
    /* how he answers its coming into his reach: the times it came in, the
     * times he struck within ENTRY_FRAMES of it, and the frames since the
     * last (0: not counting); whether it was in his reach a frame ago */
    u8 entries;
    u8 entry_meets;
    u8 entry_frames;
    u8 was_inside;
    /* what he does after an attack of his that missed: how many it saw
     * end, and how many went straight on into another attack; the attack
     * it is watching end */
    u8 misses;
    u8 follows;
    u16 miss_motion;
    /* whom it learned all this of, and as whom */
    s16 opponent_roster;
    s16 opponent_weapon;
    s16 own_roster;
    PortCpuKnown known[KNOWN_MOTIONS];
    /* its own attacks: begun and landed */
    u8 tried[OWN_STANCES][OWN_ATTACKS];
    u8 landed[OWN_STANCES][OWN_ATTACKS];
    /* the attack of its own under way: which, and the opponent's wounds
     * as it began */
    s8 own_stance;
    s8 own_attack;
    u8 own_wounds;
    u8 own_live;
    /* and: seen to have begun (a list's attack may begin with a crouching
     * step), frames since it was picked, its own wounds then, why */
    u8 own_begun;
    u8 own_age;
    u8 own_my_wounds;
    u8 own_why;
    /* what each way of its in (WHY_OPENING to WHY_FIRST) has come to
     * against him: begun, landed, and hurt in it */
    u8 tactic_tried[TACTICS];
    u8 tactic_landed[TACTICS];
    u8 tactic_hurt[TACTICS];
    /* its attacks' frames from the press to the blade, as it has used them
     * (0: not yet) */
    u8 blade_frames[OWN_STANCES][OWN_ATTACKS];
    /* frames since anything happened (a wound, an attack begun), as it
     * saw them; the wounds and his attack it saw last */
    u16 quiet;
    u16 quiet_motion;
    u8 quiet_mine;
    u8 quiet_his;
    /* a step begun to strike after it (a lunge), or to step back after it
     * (a feint) */
    u8 lunging;
    u8 feinting;
    /* TRUE once he has been seen out of its reach: his walking back into
     * it may be met */
    u8 meet_armed;
    /* the thing thrown at it (or the shot) it has answered */
    u8 projectile_answered;
    /* frames left of a rush at him after its dirt, and of circling round
     * him off an edge, and the way */
    u8 rush_frames;
    u8 circle_frames;
    u8 circle_side;
} PortCpuMind;

static PortCpuMind g_port_cpu[PORT_DUO_FIGHTERS];

/* Who is in place `fighter_index`: the place itself, or in two on two the
 * fighter of the four the duo has put there. */
static s32 port_CpuWho(s32 fighter_index) {
    return port_DuoIsOn() ? port_DuoInSlot(fighter_index != 0)
                          : fighter_index != 0;
}

/* The memory of the fighter in place `fighter_index`. */
static PortCpuMind *port_CpuMindOf(s32 fighter_index) {
    return &g_port_cpu[port_CpuWho(fighter_index)];
}

/* --cpu-tune (port_cpu.h): a level's numbers changed, to try them. */
s32 port_CpuTune(s32 level, const char *field, s32 value) {
    PortCpuLevel *row;

    if (level < 1 || level > PORT_CPU_LEVELS) {
        return FALSE;
    }
    row = &g_port_cpu_levels[level];
#define TUNE(name)                                                             \
    if (port_TextsEqual(field, #name)) {                                       \
        row->name = value;                                                     \
        return TRUE;                                                           \
    }
    TUNE(reaction)
    TUNE(jitter)
    TUNE(read)
    TUNE(read_gain)
    TUNE(tolerance)
    TUNE(margin)
    TUNE(spread)
    TUNE(punish)
    TUNE(punish_frames)
    TUNE(initiative)
    TUNE(evade)
    TUNE(slack)
    TUNE(learns)
    TUNE(reads_habits)
    TUNE(baits)
    TUNE(far_reach)
    TUNE(backs_off)
    TUNE(patience)
    TUNE(ramp)
    TUNE(aggression)
    TUNE(lunge)
    TUNE(feint)
    TUNE(meet)
    TUNE(outside)
    TUNE(weighs_tactics)
    TUNE(times_openings)
    TUNE(dodge)
    TUNE(throws)
    TUNE(dirt)
    TUNE(leap)
    TUNE(rushes)
    TUNE(circles)
    TUNE(holds)
    TUNE(holds_margin)
    TUNE(reads_answers)
    TUNE(quick)
#undef TUNE
    return FALSE;
}

/* ---- dice ---- */

static u32 port_CpuRandom(PortCpuMind *mind) {
    /* xorshift32 */
    u32 x = mind->random;

    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    mind->random = x;
    return x >> 8;
}

/* TRUE `chance` times in `out_of` */
static s32 port_CpuChance(PortCpuMind *mind, s32 chance, s32 out_of) {
    return (s32)(port_CpuRandom(mind) % (u32)out_of) < chance;
}

/* ---- setting up ---- */

void port_CpuInit(s32 fighter_index) {
    PortCpuMind *mind = port_CpuMindOf(fighter_index);
    /* the select screen's choice, or the window's cpu setting (in a
     * session the host's, held: a takeover's too, NETPLAY.md section 19) */
    s32 level = port_MatchCpuLevel(fighter_index);
    u32 seed;
    s32 i;

    /* the practice's partner that only stands there is the game's */
    if (g_fight_mode == FIGHT_MODE_PRACTICE && !g_fight_practice_round) {
        level = PORT_CPU_ORIGINAL;
    }
    mind->level = level;
    if (level == PORT_CPU_ORIGINAL) {
        return;
    }
    /* the dice from the game's own, as the fight begins, and the run's
     * seed: so a replay of the fight throws them alike */
    seed = (u32)rand() << 16;
    seed ^= (u32)rand();
    seed ^= (port_CpuAskedSeed() * SEED_SPREAD) ^
            ((u32)port_CpuWho(fighter_index) << 30);
    mind->random = seed != 0 ? seed : SEED_NONE;
    mind->frames = 0;
    mind->last_held = 0;
    for (i = 0; i < SEEN_FRAMES; i++) {
        mind->seen[i].seen = 0;
    }
    mind->script = SCRIPT_NONE;
    mind->hold = 0;
    mind->hold_frames = 0;
    mind->threat_motion = 0xFFFF;
    mind->threat_answered = 0;
    mind->reaction = g_port_cpu_levels[level].reaction;
    mind->noticed = 0;
    mind->noticed_motion = 0xFFFF;
    mind->margin_frames = 0;
    mind->own_live = 0;
    mind->habit_motion = 0xFFFF;
    mind->entry_frames = 0;
    mind->was_inside = 0;
    mind->miss_motion = 0xFFFF;
    mind->quiet = 0;
    mind->quiet_motion = 0xFFFF;
    mind->quiet_mine = 0;
    mind->quiet_his = 0;
    mind->lunging = 0;
    mind->feinting = 0;
    mind->meet_armed = 0;
    mind->projectile_answered = 0;
    mind->rush_frames = 0;
    mind->circle_frames = 0;
    mind->circle_side = 0;
    /* what it learned of the opponent it keeps from fight to fight of the
     * match; another opponent (another fighter, another weapon), or
     * another fighter of its own, starts it afresh */
    if (mind->opponent_roster != g_fighters[fighter_index].other->roster_id ||
        mind->opponent_weapon != g_fighters[fighter_index].other->weapon_id ||
        mind->own_roster != g_fighters[fighter_index].roster_id) {
        u8 *byte = (u8 *)mind->known;

        for (i = 0; i < (s32)sizeof(mind->known); i++) {
            byte[i] = 0;
        }
        byte = (u8 *)mind->tried;
        for (i = 0; i < (s32)sizeof(mind->tried); i++) {
            byte[i] = 0;
        }
        byte = (u8 *)mind->landed;
        for (i = 0; i < (s32)sizeof(mind->landed); i++) {
            byte[i] = 0;
        }
        for (i = 0; i < TACTICS; i++) {
            mind->tactic_tried[i] = 0;
            mind->tactic_landed[i] = 0;
            mind->tactic_hurt[i] = 0;
        }
        byte = (u8 *)mind->blade_frames;
        for (i = 0; i < (s32)sizeof(mind->blade_frames); i++) {
            byte[i] = 0;
        }
        mind->in_reach_frames = 0;
        mind->in_reach_attacks = 0;
        mind->entries = 0;
        mind->entry_meets = 0;
        mind->misses = 0;
        mind->follows = 0;
        mind->opponent_roster = g_fighters[fighter_index].other->roster_id;
        mind->opponent_weapon = g_fighters[fighter_index].other->weapon_id;
        mind->own_roster = g_fighters[fighter_index].roster_id;
    }
}

/* The script it plays, as the game's CPU's kinds name them (port_cpu.h). */
s32 port_CpuScriptKind(s32 fighter_index) {
    const PortCpuMind *mind = port_CpuMindOf(fighter_index);

    switch (mind->script) {
    case SCRIPT_ATTACK:
        return CPU_SCRIPT_KIND_ATTACK + 1;
    case SCRIPT_STRIKE_FALLEN:
        return CPU_SCRIPT_KIND_STRIKE_FALLEN + 1;
    case SCRIPT_THROW:
    case SCRIPT_DIRT:
        return CPU_SCRIPT_KIND_THROW + 1;
    case SCRIPT_LEAP:
        return CPU_SCRIPT_KIND_ATTACK + 1;
    case SCRIPT_GUARD_FRONTAL:
    case SCRIPT_GUARD_REVERSE:
        return CPU_SCRIPT_KIND_GUARD + 1;
    case SCRIPT_NONE:
        return 0;
    }
    return CPU_SCRIPT_KIND_FOOTWORK + 1;
}

s32 port_CpuScriptWhy(s32 fighter_index) {
    const PortCpuMind *mind = port_CpuMindOf(fighter_index);

    return mind->script != SCRIPT_NONE ? mind->why : WHY_NONE;
}

s32 port_CpuPlays(s32 fighter_index) {
    return g_fighters[fighter_index].is_cpu &&
           port_CpuMindOf(fighter_index)->level != PORT_CPU_ORIGINAL;
}

/* ---- scripts ---- */

#define BUTTON(bit) (1U << (bit))
#define WAIT(frames) (CPU_SCRIPT_WAIT | (frames))
#define RELEASE(button) (CPU_SCRIPT_RELEASE | (button))
/* the fighter's pad's buttons by number (cpu_data.c's) */
#define B_R2 1
#define B_R1 3
#define B_FRONTAL 5
#define B_REVERSE 6
#define B_SIDE_A 12
#define B_TOWARD 13
#define B_SIDE_B 14
#define B_AWAY 15

static const u8 g_port_cpu_scripts[][8] = {
    [SCRIPT_STEP_AWAY] = {B_AWAY, WAIT(1), RELEASE(B_AWAY), WAIT(1), B_AWAY,
                          WAIT(2), CPU_SCRIPT_END},
    [SCRIPT_STEP_TOWARD] = {B_TOWARD, WAIT(1), RELEASE(B_TOWARD), WAIT(1),
                            B_TOWARD, WAIT(2), CPU_SCRIPT_END},
    [SCRIPT_STEP_SIDE_A] = {B_SIDE_A, WAIT(1), RELEASE(B_SIDE_A), WAIT(1),
                            B_SIDE_A, WAIT(2), CPU_SCRIPT_END},
    [SCRIPT_STEP_SIDE_B] = {B_SIDE_B, WAIT(1), RELEASE(B_SIDE_B), WAIT(1),
                            B_SIDE_B, WAIT(2), CPU_SCRIPT_END},
    [SCRIPT_GUARD_FRONTAL] = {B_FRONTAL, WAIT(2), CPU_SCRIPT_END},
    [SCRIPT_GUARD_REVERSE] = {B_REVERSE, WAIT(2), CPU_SCRIPT_END},
    [SCRIPT_STRIKE_FALLEN] = {B_R1, WAIT(2), CPU_SCRIPT_END},
    [SCRIPT_LEAP] = {B_TOWARD, WAIT(5), B_R2, WAIT(4), B_R1, WAIT(2),
                     CPU_SCRIPT_END},
    /* (R2 crouches only from a stand: a walk is let go of first) */
    [SCRIPT_THROW] = {WAIT(2), B_R2, WAIT(5), B_FRONTAL, WAIT(2),
                      CPU_SCRIPT_END},
    [SCRIPT_DIRT] = {WAIT(2), B_R2, WAIT(5), B_REVERSE, WAIT(2),
                     CPU_SCRIPT_END},
};

static const u8 *port_CpuScriptBytes(Fighter *fighter, PortCpuMind *mind) {
    if (mind->script == SCRIPT_ATTACK) {
        return (const u8 *)fighter->cpu_scripts + mind->attack;
    }
    return g_port_cpu_scripts[mind->script];
}

static void port_CpuStart(PortCpuMind *mind, s32 script, s32 why) {
    mind->script = (u8)script;
    mind->why = (u8)why;
    mind->script_at = 0;
    mind->script_wait = 0;
    mind->script_buttons = 0;
    mind->hold = 0;
    mind->hold_frames = 0;
}

/* A frame of the script: the buttons it holds. A wait counts the
 * fighter's own frames, as the game's CPU's does (none in a frame that
 * did not advance). */
static u32 port_CpuStepScript(Fighter *fighter, PortCpuMind *mind) {
    const u8 *bytes = port_CpuScriptBytes(fighter, mind);

    if (mind->script_wait != 0) {
        if (fighter->same_frame == 0) {
            mind->script_wait--;
        }
        return mind->script_buttons;
    }
    while (1) {
        s32 byte = bytes[mind->script_at++];

        if (byte & CPU_SCRIPT_WAIT) {
            if (byte == CPU_SCRIPT_END) {
                mind->script = SCRIPT_NONE;
                break;
            }
            mind->script_wait = (u8)((byte & ~CPU_SCRIPT_WAIT & 0xFF) - 1);
            break;
        }
        if (byte & CPU_SCRIPT_RELEASE) {
            mind->script_buttons &= ~BUTTON(byte & CPU_SCRIPT_BUTTON);
        } else {
            mind->script_buttons |= BUTTON(byte & CPU_SCRIPT_BUTTON);
        }
    }
    return mind->script_buttons;
}

/* ---- seeing ---- */

static s32 port_CpuWounds(const Fighter *fighter) {
    s32 wounds = fighter->leg_wounds + fighter->arm_wounds +
                 fighter->body_wounds + (fighter->off_arm_ok ? 0 : 4);

    return wounds < 0xFF ? wounds : 0xFF;
}

/* The frames till the nearest thing thrown at it arrives, as it can be
 * seen flying: a sub-weapon in flight that has touched nothing, or a
 * handful of dirt, of the other's, coming straight at it and near its
 * height; PROJECTILE_NONE if nothing is. */
static s32 port_CpuProjectile(const Fighter *fighter) {
    s32 nearest = PROJECTILE_NONE;
    s32 i;

    for (i = 0; i < OBJECT_COUNT + DIRT_COUNT; i++) {
        const Vec3 *pos;
        const Vec3 *prev;
        s32 dx;
        s32 dz;
        s32 dy;
        s32 speed;
        s32 angle;
        s32 frames;

        if (i < OBJECT_COUNT) {
            const LooseObject *object = &g_object_pool[i];

            if (object->kind == OBJECT_NONE || !object->can_hit_fighter ||
                object->owner == fighter->index) {
                continue;
            }
            pos = &object->pos;
            prev = &object->prev_pos;
        } else {
            const ThrownDirt *dirt = &g_object_dirt[i - OBJECT_COUNT];

            if (dirt->kind == DIRT_NONE || dirt->owner == fighter->index) {
                continue;
            }
            pos = &dirt->pos;
            prev = &dirt->prev_pos;
        }
        dx = fighter->world_pos.x - pos->x;
        dz = fighter->world_pos.z - pos->z;
        dy = fighter->foot_pos.vy - pos->y;
        if (dy < -PROJECTILE_HEIGHT || dy > PROJECTILE_HEIGHT) {
            continue;
        }
        speed = SquareRoot0((pos->x - prev->x) * (pos->x - prev->x) +
                            (pos->z - prev->z) * (pos->z - prev->z));
        if (speed <= 0) {
            continue;
        }
        angle = (ratan2(dx, dz) - ratan2(pos->x - prev->x, pos->z - prev->z)) &
                ANGLE_MASK;
        if (angle > ANGLE_TURN / 2) {
            angle -= ANGLE_TURN;
        }
        if (angle < -ANGLE_TURN / 32 || angle > ANGLE_TURN / 32) {
            continue;
        }
        frames = SquareRoot0(dx * dx + dz * dz) / speed;
        if (frames < nearest) {
            nearest = frames;
        }
    }
    return nearest;
}

/* What can be seen of the opponent now, written down. */
static void port_CpuLook(Fighter *fighter, PortCpuMind *mind) {
    Fighter *other = fighter->other;
    PortCpuSeen *seen = &mind->seen[mind->frames % SEEN_FRAMES];

    seen->x = other->world_pos.x;
    seen->z = other->world_pos.z;
    seen->move_kind = other->move_kind;
    seen->motion_index = other->motion_index;
    seen->move_frame = other->move_frame;
    seen->speed = other->speed > 0 ? other->speed : FIGHTER_SPEED_ONE;
    seen->stance = other->stance;
    seen->hit_start = other->hit_start_frames[0] != 0xFF
                          ? other->hit_start_frames[0]
                          : other->hit_start_frames[1];
    seen->hit_end = other->hit_end_frames[0] != 0xFF ? other->hit_end_frames[0]
                                                     : other->hit_end_frames[1];
    seen->frames = other->motion_head[MOTION_HEAD_FRAMES];
    seen->attack_kind = other->attack_kind;
    seen->reach =
        MOVE_IS_ATTACK(other->move_kind) ? cpu_PortAttackReach(fighter) : 0;
    seen->wounds = (u8)port_CpuWounds(other);
    seen->seen = 1;
    seen->y = other->ground_y;
    seen->projectile = (s16)port_CpuProjectile(fighter);
    seen->gun = WEAPON_IS_GUN(other->weapon_id) && other->ammo != 0;
}

/* What it saw `ago` frames back, or the oldest it has. A fighter played
 * after his opponent in a frame (the second place; in two on two the
 * higher of the four) would see the opponent's frame before it is drawn:
 * he looks a frame further back, so that every fighter sees alike. */
static const PortCpuSeen *port_CpuSeenAgo(
    Fighter *fighter, PortCpuMind *mind, s32 ago) {
    if (port_CpuWho(fighter->index ^ 1) < port_CpuWho(fighter->index)) {
        ago++;
    }
    if (ago >= SEEN_FRAMES) {
        ago = SEEN_FRAMES - 1;
    }
    if ((u32)ago > mind->frames) {
        ago = (s32)mind->frames;
    }
    return &mind->seen[(mind->frames - (u32)ago) % SEEN_FRAMES];
}

static s32 port_CpuDistance(const Fighter *fighter, const PortCpuSeen *seen) {
    s32 dx = seen->x - fighter->world_pos.x;
    s32 dz = seen->z - fighter->world_pos.z;

    return SquareRoot0(dx * dx + dz * dz);
}

/* How far round from straight ahead it sees the opponent. */
static s32 port_CpuAngle(const Fighter *fighter, const PortCpuSeen *seen) {
    s32 angle = (ratan2(seen->x - fighter->world_pos.x,
                        seen->z - fighter->world_pos.z) -
                 fighter->orientation.vy) &
                ANGLE_MASK;

    if (angle > ANGLE_TURN / 2) {
        angle -= ANGLE_TURN;
    }
    return angle < 0 ? -angle : angle;
}

/* What it remembers of the opponent's motion `motion`: a new entry, in
 * place of the least seen, if it has none. */
static PortCpuKnown *port_CpuKnown(PortCpuMind *mind, s32 motion) {
    PortCpuKnown *least = &mind->known[0];
    s32 i;

    for (i = 0; i < KNOWN_MOTIONS; i++) {
        if (mind->known[i].count != 0 && mind->known[i].motion == motion) {
            return &mind->known[i];
        }
        if (mind->known[i].count < least->count) {
            least = &mind->known[i];
        }
    }
    least->motion = (u16)motion;
    least->count = 0;
    return least;
}

/* ---- moving ---- */

static s32 port_CpuClear(const Fighter *fighter, s32 probe) {
    return fighter->cpu_probes[probe] == CPU_FOUND_LEVEL;
}

/* On its way to him (at `x`, `z`, where it saw him): with an obstacle of
 * the stage across the way, the pad's directions to its corners, the way
 * round it as the game's CPU plans one (cpu_PortPlanDetour); 0 where the
 * way is straight. */
static u32 port_CpuDetour(
    Fighter *fighter, s32 x, s32 z, s32 distance, s32 far) {
    CpuRoute *route = &fighter->cpu_route;
    const CpuWaypoint *next;
    s32 dx;
    s32 dz;

    /* looked for each time it decides where to walk (every few frames,
     * as the game's CPU looks every eighth), and planned again as he moves
     * from where it has got to */
    if (cpu_PortFindObstacle(fighter, x, z) < distance) {
        cpu_PortPlanDetour(fighter, x, z);
    } else {
        route->count = 0;
    }
    /* node[0] is him: the corners are above it */
    while (route->count > 1) {
        next = &route->node[route->count - 1];
        dx = next->x - fighter->world_pos.x;
        dz = next->z - fighter->world_pos.z;
        if (dx * dx + dz * dz >= CPU_WAYPOINT_RADIUS * CPU_WAYPOINT_RADIUS) {
            return (u32)cpu_PortSteerTo(fighter, next->x, next->z, far);
        }
        route->count--;
    }
    return 0;
}

/* The buttons that walk it out to `want` from `distance` (round the
 * stage's obstacles to him, at `x`, `z`), or round to face the opponent;
 * 0 where it is. */
static u32 port_CpuWalk(Fighter *fighter, s32 x, s32 z, s32 distance, s32 want,
                        s32 tolerance, s32 angle) {
    if (distance > want + tolerance) {
        u32 round = port_CpuDetour(
            fighter, x, z, distance, distance > want + RUN_DISTANCE);

        if (round != 0) {
            return round;
        }
    } else {
        fighter->cpu_route.count = 0;
    }
    if (angle > ANGLE_TURN / 16) {
        /* turned from him: a few frames of sidling face it round */
        return FIGHTER_PAD_SIDE_A;
    }
    if (distance > want + tolerance) {
        s32 ahead = fighter->cpu_probes[CPU_PROBE_TOWARD];

        if (ahead == CPU_FOUND_DROP || ahead == CPU_FOUND_BLOCKED_STEP) {
            /* a height between them (a terrace, a step of the ground):
             * down off it, or up it, R1 at its edge as the game's CPU
             * climbs (cpu_SteerTo); sidling along it the two would circle
             * for ever, one above the other */
            s32 heading;

            return FIGHTER_PAD_TOWARD |
                   (fighter_FindLedge(fighter, &heading) ? PADR1 : 0);
        }
        if (!port_CpuClear(fighter, CPU_PROBE_TOWARD)) {
            return port_CpuClear(fighter, CPU_PROBE_SIDE_A)
                       ? FIGHTER_PAD_SIDE_A
                       : FIGHTER_PAD_SIDE_B;
        }
        /* far off, a run */
        if (distance > want + RUN_DISTANCE) {
            return FIGHTER_PAD_TOWARD | PADL1;
        }
        return FIGHTER_PAD_TOWARD;
    }
    if (distance < want - tolerance) {
        if (port_CpuClear(fighter, CPU_PROBE_AWAY)) {
            return FIGHTER_PAD_AWAY;
        }
        /* its back to an edge or a wall: round him instead */
        if (port_CpuClear(fighter, CPU_PROBE_SIDE_A)) {
            return FIGHTER_PAD_SIDE_A;
        }
        if (port_CpuClear(fighter, CPU_PROBE_SIDE_B)) {
            return FIGHTER_PAD_SIDE_B;
        }
    }
    return 0;
}

/* ---- striking ---- */

enum { PICK_ANY, PICK_REACHES };

/* An attack of the motion file's list that it may use (none of dirt or a
 * thrown thing, none another character's, none needing the arm it has
 * lost): its script, or NULL. */
static u8 *port_CpuUsable(const Fighter *fighter, u8 *attack) {
    u8 *script = attack + CPU_ATTACK_HEAD;
    s32 kind = attack[CPU_ATTACK_FLAGS] & CPU_ATTACK_KIND;

    if (script[0] == CPU_ATTACK_FOR_SOME) {
        u32 mask = (u32)script[4] << 24 | (u32)script[3] << 16 |
                   (u32)script[2] << 8 | script[1];

        if (!(mask & (1U << fighter->cpu_profile))) {
            return NULL;
        }
        script += CPU_ATTACK_MASK_BYTES;
    }
    if ((attack[CPU_ATTACK_FLAGS] & CPU_ATTACK_NEEDS_ARM) &&
        fighter->off_arm_ok == 0) {
        return NULL;
    }
    if (kind == CPU_ATTACK_KIND_DIRT || kind == CPU_ATTACK_KIND_THROW) {
        return NULL;
    }
    return script;
}

/* TRUE if its weapon is a gun with shots in it. */
static s32 port_CpuShoots(const Fighter *fighter) {
    return WEAPON_IS_GUN(fighter->weapon_id) && fighter->ammo != 0;
}

/* The farthest an attack of the list is meant for (cpu.h, CPU_ATTACK_...):
 * kinds 0 and 7 between their two distances, kinds 1 to 4 for an opponent
 * nearer than the first; with the fighter's reach and the level's slack. */
static s32 port_CpuAttackFar(
    const Fighter *fighter, const PortCpuLevel *level, const u8 *attack) {
    s32 kind = attack[CPU_ATTACK_FLAGS] & CPU_ATTACK_KIND;
    s32 far = kind == 0 || kind == CPU_ATTACK_KIND_LAST
                  ? attack[CPU_ATTACK_FAR]
                  : attack[CPU_ATTACK_NEAR];

    /* a gun with shots in it fires from any distance, as the game's CPU
     * has it */
    if (port_CpuShoots(fighter)) {
        return CPU_ANY_DISTANCE;
    }
    return far * MOTION_REACH_UNIT + fighter->reach_bonus + level->slack;
}

/* The farthest any attack of its stance's list is meant for: as far as it
 * strikes from (the motion's own reach may be further). */
static s32 port_CpuListReach(
    const Fighter *fighter, const PortCpuLevel *level) {
    u16 *list = fighter->cpu_stance_scripts[fighter->stance];
    s32 most = 0;
    s32 index;

    if (list == NULL) {
        return 0;
    }
    for (index = 0; list[index] != 0; index++) {
        u8 *attack = (u8 *)fighter->cpu_scripts + list[index];
        s32 far;

        if (port_CpuUsable(fighter, attack) == NULL) {
            continue;
        }
        far = port_CpuAttackFar(fighter, level, attack);
        if (far > most) {
            most = far;
        }
    }
    return most;
}

/* How many frames from its press an attack of its list takes to bring
 * its blade, as it knows its moves: as it has seen it do in the match, or
 * else by its buttons (one that crouches first, with R2, comes the later
 * by the step); 0 if it does not judge by that (the levels that do not
 * know their speeds). */
static s32 port_CpuBladeFrames(const Fighter *fighter, const PortCpuMind *mind,
                               s32 index, const u8 *script) {
    s32 at;

    if (fighter->stance < OWN_STANCES && index < OWN_ATTACKS &&
        mind->blade_frames[fighter->stance][index] != 0) {
        return mind->blade_frames[fighter->stance][index];
    }
    for (at = 0; at < SCRIPT_LOOK_BYTES && script[at] != CPU_SCRIPT_END; at++) {
        if (!(script[at] & (CPU_SCRIPT_WAIT | CPU_SCRIPT_RELEASE)) &&
            (script[at] & CPU_SCRIPT_BUTTON) == B_R2) {
            return BLADE_GUESS_CROUCHING;
        }
    }
    return BLADE_GUESS;
}

/* An attack of its stance's list that suits the distance, picked at random
 * by how each has done (from the adept up): its offset in the list, or -1.
 * The list's attacks are the motion file's, each with the distances it is
 * meant for (cpu.h, CPU_ATTACK_...): the moves a player knows. */
static s32 port_CpuPickAttack(Fighter *fighter, PortCpuMind *mind, s32 distance,
                              s32 within, s32 *index_out, s32 *far_out) {
    const PortCpuLevel *level = &g_port_cpu_levels[mind->level];
    u16 *list = fighter->cpu_stance_scripts[fighter->stance];
    s32 best = -1;
    s32 best_score = 0;
    s32 index;

    if (list == NULL) {
        return -1;
    }
    for (index = 0; list[index] != 0; index++) {
        u8 *attack = (u8 *)fighter->cpu_scripts + list[index];
        u8 *script = port_CpuUsable(fighter, attack);
        s32 kind = attack[CPU_ATTACK_FLAGS] & CPU_ATTACK_KIND;
        s32 near = attack[CPU_ATTACK_NEAR] * MOTION_REACH_UNIT;
        s32 far;
        s32 score;

        if (script == NULL) {
            continue;
        }
        far = port_CpuAttackFar(fighter, level, attack);
        if (distance >= far || ((kind == 0 || kind == CPU_ATTACK_KIND_LAST) &&
                                distance <= near && !port_CpuShoots(fighter))) {
            continue;
        }
        /* none too slow for it, as far as it knows its moves */
        if (within >= 0 &&
            port_CpuBladeFrames(fighter, mind, index, script) > within) {
            continue;
        }
        score = 1 + (s32)(port_CpuRandom(mind) & 0xFF);
        if (level->learns && fighter->stance < OWN_STANCES &&
            index < OWN_ATTACKS) {
            s32 tried = mind->tried[fighter->stance][index];
            s32 landed = mind->landed[fighter->stance][index];

            /* how well it has done, (landed + 1) / (tried + 2), as a weight */
            score = score * (4 * landed + 4) / (tried + 2);
        }
        if (score > best_score) {
            best_score = score;
            best = (s32)(script - (u8 *)fighter->cpu_scripts);
            *index_out = index;
            *far_out = far;
        }
    }
    return best;
}

/* Begins an attack that suits the distance (and whose blade comes within
 * `within` frames, if that is not -1, as far as it knows), if one does,
 * and counts it tried; FALSE if none does. */
static s32 port_CpuStrikeWithin(
    Fighter *fighter, PortCpuMind *mind, s32 distance, s32 within, s32 why) {
    s32 index = -1;
    s32 far = 0;
    s32 offset =
        port_CpuPickAttack(fighter, mind, distance, within, &index, &far);

    if (offset < 0) {
        return FALSE;
    }
    port_CpuStart(mind, SCRIPT_ATTACK, why);
    mind->attack = (u16)offset;
    mind->attack_far = (s16)(far < S16_MOST ? far : S16_MOST);
    mind->own_stance = (s8)fighter->stance;
    mind->own_attack = (s8)index;
    mind->own_wounds = (u8)port_CpuWounds(fighter->other);
    mind->own_live = 1;
    mind->own_begun = 0;
    mind->own_age = 0;
    mind->own_my_wounds = (u8)port_CpuWounds(fighter);
    mind->own_why = (u8)why;
    if (why < TACTICS && mind->tactic_tried[why] < 0xFF) {
        mind->tactic_tried[why]++;
    }
    /* something happened: the less patience it has lost */
    mind->quiet /= 2;
    if (fighter->stance < OWN_STANCES && index < OWN_ATTACKS &&
        mind->tried[fighter->stance][index] < 0xFF) {
        mind->tried[fighter->stance][index]++;
    }
    return TRUE;
}

static s32 port_CpuStrike(
    Fighter *fighter, PortCpuMind *mind, s32 distance, s32 why) {
    return port_CpuStrikeWithin(fighter, mind, distance, -1, why);
}

/* Its own attack, once a frame: the frames to its blade as it begins,
 * and at its end, landed if he is the worse for it, or hurt if it is. */
static void port_CpuOwnAttackDone(Fighter *fighter, PortCpuMind *mind) {
    s32 known = mind->own_stance >= 0 && mind->own_stance < OWN_STANCES &&
                mind->own_attack >= 0 && mind->own_attack < OWN_ATTACKS;
    s32 landed;
    s32 hurt;

    if (!mind->own_live) {
        return;
    }
    if (mind->own_age < 0xFF) {
        mind->own_age++;
    }
    if (MOVE_IS_ATTACK(fighter->move_kind)) {
        if (!mind->own_begun) {
            s32 hit_start = fighter->hit_start_frames[0] != 0xFF
                                ? fighter->hit_start_frames[0]
                                : fighter->hit_start_frames[1];
            s32 speed = fighter->speed > 0 ? fighter->speed : FIGHTER_SPEED_ONE;

            mind->own_begun = 1;
            /* how quick it is, as a player knows his moves */
            if (known && hit_start != 0xFF) {
                s32 blade =
                    mind->own_age + hit_start * FIGHTER_SPEED_ONE / speed;

                mind->blade_frames[mind->own_stance][mind->own_attack] =
                    (u8)(blade < 0xFF ? blade : 0xFF);
            }
        }
        return;
    }
    /* a list's attack may begin with a step: waited for a while */
    if (mind->script == SCRIPT_ATTACK ||
        (!mind->own_begun && mind->own_age < OWN_BEGIN_FRAMES)) {
        return;
    }
    mind->own_live = 0;
    landed = port_CpuWounds(fighter->other) > mind->own_wounds ||
             fighter->other->is_defeated;
    hurt = !landed && (port_CpuWounds(fighter) > mind->own_my_wounds ||
                       fighter->is_defeated);
    if (landed && known &&
        mind->landed[mind->own_stance][mind->own_attack] < 0xFF) {
        mind->landed[mind->own_stance][mind->own_attack]++;
    }
    if (mind->own_why < TACTICS) {
        if (landed && mind->tactic_landed[mind->own_why] < 0xFF) {
            mind->tactic_landed[mind->own_why]++;
        }
        if (hurt && mind->tactic_hurt[mind->own_why] < 0xFF) {
            mind->tactic_hurt[mind->own_why]++;
        }
    }
}

/* Of 256: how a way in has done against him, as a weight on its chance
 * (the levels that weigh them): (landed + 1) / (hurt + 1), from a quarter
 * to twice. A way that has got it hurt more than it has landed it uses
 * less; one that lands, more. */
static s32 port_CpuTacticWeight(
    const PortCpuMind *mind, const PortCpuLevel *level, s32 why) {
    s32 weight;

    if (!level->weighs_tactics || why >= TACTICS) {
        return CHANCE_BYTE;
    }
    weight = (mind->tactic_landed[why] + 1) * CHANCE_BYTE /
             (mind->tactic_hurt[why] + 1);
    if (weight < CHANCE_BYTE / 4) {
        weight = CHANCE_BYTE / 4;
    }
    return weight < 2 * CHANCE_BYTE ? weight : 2 * CHANCE_BYTE;
}

/* The most frames to its blade it allows a strike at him free to answer
 * it (meeting him, striking first, after a lunge's step): the level's
 * `quick`, or -1 for any. */
static s32 port_CpuQuick(const PortCpuLevel *level) {
    return level->quick != 0 ? level->quick : -1;
}

/* A chance of 256 weighed by how that way in has done against him. */
static s32 port_CpuWeighed(
    const PortCpuMind *mind, const PortCpuLevel *level, s32 why, s32 chance) {
    chance = chance * port_CpuTacticWeight(mind, level, why) / CHANCE_BYTE;
    return chance < CHANCE_MOST ? chance : CHANCE_MOST;
}

/* ---- deciding ---- */

static s32 port_CpuFree(const Fighter *fighter) {
    return fighter->move_kind == MOVE_KIND_STANCE ||
           fighter->move_kind == MOVE_KIND_WALK ||
           fighter->move_kind == MOVE_KIND_STEP ||
           fighter->move_kind == MOVE_KIND_RUN ||
           fighter->move_kind == MOVE_KIND_MOVE;
}

/* A motion of the opponent's that leaves him open to a blow, as it is
 * seen: hurt, beaten aside, reloading, getting up, changing stance,
 * crouching, throwing. */
static s32 port_CpuSeenOpen(const PortCpuSeen *seen) {
    switch (seen->move_kind) {
    case MOVE_KIND_DAM:
    case MOVE_KIND_STAGGER:
    case MOVE_KIND_DEFLECTED:
    case MOVE_KIND_RELOAD:
    case MOVE_KIND_GET_UP:
    case MOVE_KIND_STANCE_CHANGE:
    case MOVE_KIND_CROUCH:
    case MOVE_KIND_DIRT:
    case MOVE_KIND_SUBWEP:
    case MOVE_KIND_LAND:
    case MOVE_KIND_HOM_ED:
    case MOVE_KIND_RUN_ED:
        return TRUE;
    }
    return FALSE;
}

/* Frames from now to frame `frame` of the motion it saw `ago` frames back
 * at `seen->move_frame`, if he is still in it. */
static s32 port_CpuFramesTo(const PortCpuSeen *seen, s32 frame, s32 ago) {
    return (frame - seen->move_frame) * FIGHTER_SPEED_ONE / seen->speed - ago;
}

/* Once a frame: how long nothing has happened, as it saw it. A wound of
 * either begins the count again; an attack he was seen to begin halves it
 * (as one of its own does, port_CpuStrike), so that pokes from afar that
 * draw no blood do not keep it patient for ever. */
static void port_CpuNoteQuiet(Fighter *fighter, PortCpuMind *mind) {
    const PortCpuSeen *seen = port_CpuSeenAgo(fighter, mind, mind->reaction);
    s32 mine = port_CpuWounds(fighter);

    if (mine != mind->quiet_mine || seen->wounds != mind->quiet_his) {
        mind->quiet_mine = (u8)mine;
        mind->quiet_his = seen->wounds;
        mind->quiet = 0;
    } else if (!MOVE_IS_ATTACK(seen->move_kind)) {
        mind->quiet_motion = 0xFFFF;
    } else if (mind->quiet_motion != seen->motion_index) {
        mind->quiet_motion = seen->motion_index;
        mind->quiet /= 2;
    }
    if (mind->quiet < S16_MOST) {
        mind->quiet++;
    }
}

/* How hard it presses, of PRESSURE_ALL: its patience run out (frames of
 * nothing happening past its level's patience), in half the hurry if it
 * is ahead and more for each wound it has more than it has seen him to
 * have; and more as the fight goes on. All of it from what a player
 * knows: his own wounds, what he saw of the other's, the time. */
static s32 port_CpuPressure(
    const Fighter *fighter, const PortCpuMind *mind, const PortCpuLevel *level,
    const PortCpuSeen *seen) {
    s32 behind = port_CpuWounds(fighter) - seen->wounds;
    s32 ramp = level->ramp > 0 ? level->ramp : 1;
    s32 pressure = 0;

    if (mind->quiet > level->patience) {
        pressure = (mind->quiet - level->patience) * PRESSURE_ALL / ramp;
    }
    if (behind < 0) {
        pressure /= 2;
    } else {
        pressure += behind * PRESSURE_A_WOUND;
    }
    /* ahead or not: one who will not come to it is gone to in the end */
    if (fighter->frames_alive > PRESSURE_CLOCK_FROM) {
        pressure += (fighter->frames_alive - PRESSURE_CLOCK_FROM) *
                    PRESSURE_ALL / (PRESSURE_CLOCK_ALL - PRESSURE_CLOCK_FROM);
    }
    return pressure < PRESSURE_ALL ? pressure : PRESSURE_ALL;
}

/* port_CpuDodge's and port_CpuFromAfar's: a script begun (their buttons
 * else) */
#define DODGE_STARTED 0xFFFFFFFFU

/* A step aside, to whichever side is clear: its script, or SCRIPT_NONE. */
static s32 port_CpuSideStep(const Fighter *fighter, PortCpuMind *mind) {
    s32 first =
        (port_CpuRandom(mind) & 1) ? CPU_PROBE_SIDE_A : CPU_PROBE_SIDE_B;
    s32 second =
        first == CPU_PROBE_SIDE_A ? CPU_PROBE_SIDE_B : CPU_PROBE_SIDE_A;

    if (port_CpuClear(fighter, first)) {
        return first == CPU_PROBE_SIDE_A ? SCRIPT_STEP_SIDE_A
                                         : SCRIPT_STEP_SIDE_B;
    }
    if (port_CpuClear(fighter, second)) {
        return second == CPU_PROBE_SIDE_A ? SCRIPT_STEP_SIDE_A
                                          : SCRIPT_STEP_SIDE_B;
    }
    return SCRIPT_NONE;
}

/* Something thrown at it seen coming (a sub-weapon, dirt), or a gun seen
 * aimed at it and about to fire: a step aside out of its line where there
 * is time (by the level's `dodge`), else, for a thing thrown, the guard as
 * it comes (an attack just begun beats a thrown thing aside). All from
 * what it saw its reaction time back: the thing's flight, the gun raised.
 * DODGE_STARTED with a script begun, or 0. */
static u32 port_CpuDodge(
    Fighter *fighter, PortCpuMind *mind, const PortCpuLevel *level,
    const PortCpuSeen *seen, s32 distance, s32 ago) {
    s32 frames;
    s32 step;

    if (seen->gun && MOVE_IS_ATTACK(seen->move_kind) &&
        seen->hit_start != 0xFF && seen->move_frame <= seen->hit_start) {
        /* the shot comes at its blade's first frame */
        frames = port_CpuFramesTo(seen, seen->hit_start, ago);
        if (mind->threat_motion != seen->motion_index) {
            mind->threat_motion = seen->motion_index;
            mind->projectile_answered = 0;
        }
    } else if (seen->projectile != PROJECTILE_NONE) {
        /* it has flown on meanwhile */
        frames = seen->projectile - ago;
    } else if ((seen->move_kind == MOVE_KIND_SUBWEP ||
                seen->move_kind == MOVE_KIND_DIRT) &&
               distance > DODGE_THROW_NEAR) {
        /* seen winding up to throw, far enough off for the step to be in
         * time */
        frames = DODGE_FRAMES_LEAST;
    } else {
        mind->projectile_answered = 0;
        return 0;
    }
    if (mind->projectile_answered) {
        return 0;
    }
    if (frames >= DODGE_FRAMES_LEAST) {
        mind->projectile_answered = 1;
        if (port_CpuChance(mind, level->dodge, CHANCE_BYTE) &&
            (step = port_CpuSideStep(fighter, mind)) != SCRIPT_NONE) {
            port_CpuStart(mind, step, WHY_DODGE);
            return DODGE_STARTED;
        }
        return 0;
    }
    if (frames <= 1 && !seen->gun) {
        mind->projectile_answered = 1;
        port_CpuStart(mind, SCRIPT_GUARD_FRONTAL, WHY_GUARD);
        return DODGE_STARTED;
    }
    return 0;
}

/* TRUE if he stands on its level, not above or below it. */
static s32 port_CpuSameHeight(const Fighter *fighter, const PortCpuSeen *seen) {
    s32 dy = seen->y - fighter->ground_y;

    return dy > -CPU_LEVEL_HEIGHT && dy < CPU_LEVEL_HEIGHT;
}

/* TRUE if he is seen in a motion that keeps him from answering for a
 * while: an opening (port_CpuSeenOpen), or an attack of his that missed
 * with `frames` or more to go. */
static s32 port_CpuSeenBusy(const PortCpuSeen *seen, s32 ago, s32 frames) {
    if (port_CpuFramesTo(seen, seen->frames, ago) < frames) {
        return FALSE;
    }
    return port_CpuSeenOpen(seen) || (MOVE_IS_ATTACK(seen->move_kind) &&
                                      seen->move_frame > seen->hit_end);
}

/* The ways at range a player has (docs/CPU.md, "From afar"): a gun's shots
 * and its reload; a sub-weapon or dirt thrown at him from beyond his
 * reach; the leaping attack from its distance; an opening seen far off run
 * in on, leapt at or thrown at (the levels that rush); and off an edge or
 * a wall that crowds it, round him (the levels that circle), or in on him
 * where his back is to one. The buttons, DODGE_STARTED with a script
 * begun, or 0; `want`, where it likes to stand, may be brought in. */
static u32 port_CpuFromAfar(
    Fighter *fighter, PortCpuMind *mind, const PortCpuLevel *level,
    const PortCpuSeen *seen, s32 distance, s32 angle, s32 my_reach,
    s32 his_reach, s32 pressure, s32 ago, s32 *want) {
    s32 standing = fighter->move_kind == MOVE_KIND_STANCE;
    /* (a thing is thrown, a shot fired, a leap made from a walk as well) */
    s32 afoot = standing || fighter->move_kind == MOVE_KIND_WALK;
    s32 level_ground = port_CpuSameHeight(fighter, seen);
    s32 his_free = seen->move_kind == MOVE_KIND_STANCE ||
                   seen->move_kind == MOVE_KIND_WALK;
    s32 busy = port_CpuSeenBusy(seen, ago, RUSH_FRAMES_LEAST);
    s32 past = fighter->cpu_probes[CPU_PROBE_PAST_OPPONENT];

    /* -- a gun -- */
    if (WEAPON_IS_GUN(fighter->weapon_id)) {
        if (fighter->ammo == 0) {
            /* reloaded out of his reach (the stance button, as the game's
             * CPU reloads) */
            if (standing && fighter->reloads_left != 0 &&
                fighter->off_arm_ok != 0 &&
                distance > his_reach + THREAT_MARGIN) {
                return FIGHTER_PAD_STANCE;
            }
        } else {
            /* aimed and fired, from where he cannot reach it */
            if (afoot && angle < ANGLE_TURN / 32 && level_ground &&
                distance > his_reach &&
                cpu_PortFindObstacle(fighter, seen->x, seen->z) >= distance &&
                port_CpuChance(
                    mind,
                    busy ? CHANCE_FRAME / 2 : level->initiative + level->throws,
                    CHANCE_FRAME) &&
                port_CpuStrike(fighter, mind, distance, WHY_SHOT)) {
                return DODGE_STARTED;
            }
            /* and kept off him */
            if (*want < his_reach + GUN_STANDOFF) {
                *want = his_reach + GUN_STANDOFF;
            }
        }
        return 0;
    }

    /* -- a sub-weapon thrown -- */
    if (level->throws != 0 && afoot && fighter->sub_weapon_count != 0 &&
        fighter->off_arm_ok != 0 &&
        (fighter->two_sword_ready == 0 || fighter->sub_weapon_count >= 2) &&
        fighter->stance_class != STANCE_CLASS_SHEATHED && level_ground &&
        angle < ANGLE_TURN / 32 && distance > THROW_NEAR &&
        distance > my_reach && distance < THROW_FAR && (his_free || busy) &&
        cpu_PortFindObstacle(fighter, seen->x, seen->z) >= distance &&
        port_CpuChance(
            mind, level->throws * (busy && level->rushes ? RUSH_THROWS : 1),
            CHANCE_FRAME)) {
        port_CpuStart(mind, SCRIPT_THROW, WHY_THROW);
        mind->quiet /= 2;
        return DODGE_STARTED;
    }
    /* -- dirt thrown, and in after it -- */
    if (level->dirt != 0 && afoot &&
        SURFACE_IN(fighter->surface, SURFACES_LOOSE) &&
        fighter->off_arm_ok != 0 && level_ground && angle < ANGLE_TURN / 32 &&
        distance > DIRT_NEAR && distance > my_reach &&
        distance < CPU_DIRT_RANGE && his_free &&
        port_CpuChance(mind, level->dirt, CHANCE_FRAME)) {
        port_CpuStart(mind, SCRIPT_DIRT, WHY_DIRT);
        mind->rush_frames = level->rushes ? DIRT_RUSH_FRAMES : 0;
        mind->quiet /= 2;
        return DODGE_STARTED;
    }
    if (mind->rush_frames > 0) {
        mind->rush_frames--;
    }
    /* -- the leap: pressing, or at an opening far off -- */
    if (level->leap != 0 && afoot && level_ground && distance > CPU_LEAP_NEAR &&
        distance < CPU_LEAP_FAR && angle < ANGLE_TURN / 16 &&
        !MOVE_IS_ATTACK(seen->move_kind) &&
        fighter->cpu_probes[CPU_PROBE_FAR_TOWARD] != CPU_FOUND_NO_FLOOR &&
        fighter->cpu_probes[CPU_PROBE_FAR_TOWARD] !=
            CPU_FOUND_BLOCKED_NO_FLOOR &&
        port_CpuClear(fighter, CPU_PROBE_TOWARD) &&
        (level->rushes
             ? (port_CpuSeenBusy(seen, ago, LEAP_FRAMES_LEAST) &&
                port_CpuChance(mind, level->punish, CHANCE_BYTE))
             : port_CpuChance(
                   mind,
                   level->leap * pressure / PRESSURE_ALL + level->leap / 4,
                   CHANCE_FRAME))) {
        port_CpuStart(mind, SCRIPT_LEAP, WHY_LEAP);
        mind->quiet /= 2;
        return DODGE_STARTED;
    }
    /* -- an opening far off, run in on (the strike is the opening's) -- */
    if (level->rushes && (busy || mind->rush_frames > 0) &&
        distance > my_reach + OPENING_MARGIN && distance < RUSH_FAR &&
        level_ground && *want > my_reach - PRESS_DEPTH) {
        *want = my_reach - PRESS_DEPTH;
        if (angle < ANGLE_TURN / 16 &&
            port_CpuClear(fighter, CPU_PROBE_TOWARD) &&
            fighter->cpu_route.count == 0) {
            return distance > my_reach + RUSH_RUN ? FIGHTER_PAD_TOWARD | PADL1
                                                  : FIGHTER_PAD_TOWARD;
        }
    }
    /* -- an edge -- */
    if (level->circles) {
        /* crowded: in his reach, the ground behind it gone or walled, or
         * both sides walled: round him, along whichever side is clear */
        s32 crowded =
            distance < his_reach &&
            (!port_CpuClear(fighter, CPU_PROBE_AWAY) &&
             (fighter->cpu_probes[CPU_PROBE_AWAY] == CPU_FOUND_NO_FLOOR ||
              fighter->cpu_probes[CPU_PROBE_AWAY] == CPU_FOUND_DROP ||
              fighter->cpu_probes[CPU_PROBE_BACK_A] == CPU_FOUND_NO_FLOOR ||
              fighter->cpu_probes[CPU_PROBE_BACK_B] == CPU_FOUND_NO_FLOOR ||
              !port_CpuClear(fighter, CPU_PROBE_SIDE_A) ||
              !port_CpuClear(fighter, CPU_PROBE_SIDE_B)));

        if (crowded && mind->circle_frames == 0 &&
            !MOVE_IS_ATTACK(seen->move_kind)) {
            mind->circle_frames = CIRCLE_FRAMES;
            mind->circle_side =
                port_CpuClear(fighter, CPU_PROBE_SIDE_A) ? 0
                : port_CpuClear(fighter, CPU_PROBE_SIDE_B)
                    ? 1
                    : (u8)(port_CpuRandom(mind) & 1);
        }
        if (mind->circle_frames > 0) {
            mind->circle_frames--;
            if (!MOVE_IS_ATTACK(seen->move_kind) &&
                (standing || fighter->move_kind == MOVE_KIND_WALK)) {
                /* sidling round him keeps him in front, the edge falling
                 * behind to the side */
                return mind->circle_side ? FIGHTER_PAD_SIDE_B
                                         : FIGHTER_PAD_SIDE_A;
            }
        }
        /* his back to a drop or a wall: he cannot step back from a blow,
         * so in on him */
        if ((past == CPU_FOUND_NO_FLOOR || past == CPU_FOUND_DROP ||
             past == CPU_FOUND_BLOCKED || past == CPU_FOUND_BLOCKED_NO_FLOOR) &&
            distance < his_reach + CORNER_DISTANCE &&
            *want > my_reach - PRESS_DEPTH) {
            *want = my_reach - PRESS_DEPTH;
        }
    }
    return 0;
}

/* Once a frame, for the levels that read his answers: how he answers its
 * coming into his reach (does he strike at it at once: he "meets"), and
 * what he does after an attack of his has missed (does he go straight on
 * into another: he "follows"), as it saw them. */
static void port_CpuReadAnswers(
    PortCpuMind *mind, const PortCpuSeen *seen, s32 distance, s32 his_reach) {
    s32 inside = distance < his_reach;
    s32 attack = MOVE_IS_ATTACK(seen->move_kind);

    if (inside && !mind->was_inside && mind->entry_frames == 0 && !attack) {
        if (mind->entries < 0xFF) {
            mind->entries++;
        }
        mind->entry_frames = 1;
    } else if (mind->entry_frames != 0) {
        if (attack && seen->move_frame < ENTRY_SEEN_FRAMES) {
            if (mind->entry_meets < 0xFF) {
                mind->entry_meets++;
            }
            mind->entry_frames = 0;
        } else if (++mind->entry_frames > ENTRY_FRAMES) {
            mind->entry_frames = 0;
        }
    }
    mind->was_inside = (u8)inside;
    /* an attack of his past its blade: watched to its end */
    if (attack && seen->move_frame > seen->hit_end &&
        mind->miss_motion == 0xFFFF) {
        mind->miss_motion = seen->motion_index;
    } else if (mind->miss_motion != 0xFFFF &&
               (!attack || seen->motion_index != mind->miss_motion)) {
        if (mind->misses < 0xFF) {
            mind->misses++;
        }
        if (attack && mind->follows < 0xFF) {
            mind->follows++;
        }
        mind->miss_motion = 0xFFFF;
    }
}

/* TRUE if he has been seen to strike at it as it comes into his reach,
 * more often than not (a prior of one time in two). */
static s32 port_CpuHeMeets(const PortCpuMind *mind) {
    return mind->entries >= ANSWERS_LEAST &&
           2 * (mind->entry_meets + 1) > mind->entries + 2;
}

/* TRUE if he has been seen to go straight on from an attack that missed
 * into another, more often than not. */
static s32 port_CpuHeFollows(const PortCpuMind *mind) {
    return mind->misses >= ANSWERS_LEAST &&
           2 * (mind->follows + 1) > mind->misses + 2;
}

/* The frame's decision (the file's head says the order): the buttons to
 * hold, or 0 with a script begun. */
static u32 port_CpuDecide(Fighter *fighter, PortCpuMind *mind) {
    const PortCpuLevel *level = &g_port_cpu_levels[mind->level];
    Fighter *other = fighter->other;
    s32 ago = mind->reaction;
    const PortCpuSeen *seen = port_CpuSeenAgo(fighter, mind, ago);
    s32 distance = port_CpuDistance(fighter, seen);
    s32 angle = port_CpuAngle(fighter, seen);
    s32 his_stance = seen->stance < 3 ? seen->stance : 0;
    s32 his_reach = level->far_reach ? other->attack_max_reach[his_stance]
                                     : other->attack_reach[his_stance];
    /* as far as it strikes from: its stance's reach, or its list's
     * farthest attack where that is nearer */
    s32 my_reach = fighter->attack_reach[fighter->stance];
    s32 list_reach = port_CpuListReach(fighter, level);
    s32 standing = fighter->move_kind == MOVE_KIND_STANCE;
    s32 want;
    s32 pressure;
    s32 pressed;
    s32 meeter;

    if (list_reach < my_reach) {
        my_reach = list_reach;
    }

    /* -- what it cannot help -- */
    if (other->is_defeated || seen->move_kind == MOVE_KIND_END_GAME ||
        seen->move_kind == MOVE_KIND_DTH) {
        return 0;
    }
    if (fighter->move_kind == MOVE_KIND_DOWN) {
        /* up, or rolled away from him if he is near */
        if ((mind->frames & 7) != 0) {
            return 0;
        }
        return distance < his_reach + ROLL_AWAY_MARGIN &&
                       port_CpuClear(fighter, CPU_PROBE_AWAY)
                   ? FIGHTER_PAD_AWAY
                   : FIGHTER_PAD_TOWARD;
    }
    if (MOVE_IS_LOCK(fighter->move_kind)) {
        /* push with the stance button, and strike now and then */
        return (mind->frames % (8 - mind->level)) == 0
                   ? FIGHTER_PAD_FRONTAL_ATTACK
                   : FIGHTER_PAD_STANCE;
    }
    if (fighter->move_kind == MOVE_KIND_PAIRED) {
        /* caught: struggle */
        return (mind->frames & 1) ? FIGHTER_PAD_FRONTAL_ATTACK : 0;
    }
    if (!port_CpuFree(fighter)) {
        return 0;
    }

    /* -- something thrown at it, or a gun aimed at it -- */
    {
        u32 dodge = port_CpuDodge(fighter, mind, level, seen, distance, ago);

        if (dodge != 0) {
            return dodge == DODGE_STARTED ? 0 : dodge;
        }
    }

    /* -- a threat -- */
    if (MOVE_IS_ATTACK(seen->move_kind) && seen->hit_start != 0xFF &&
        seen->move_frame <= seen->hit_end && !seen->gun) {
        s32 to_hit = port_CpuFramesTo(seen, seen->hit_start, ago);

        if (mind->threat_motion != seen->motion_index || seen->move_frame < 2) {
            PortCpuKnown *known = port_CpuKnown(mind, seen->motion_index);

            mind->threat_motion = seen->motion_index;
            mind->threat_answered = 0;
            /* seen once more: the better known */
            if (known->count < 0xFF) {
                known->count++;
            }
        }
        if (!mind->threat_answered && distance < seen->reach + THREAT_MARGIN) {
            PortCpuKnown *known = port_CpuKnown(mind, seen->motion_index);
            s32 read = level->read + level->read_gain * known->count;
            s32 kind = seen->attack_kind & 1;

            /* a lunge or a feint under way is given up for it */
            mind->lunging = 0;
            mind->feinting = 0;

            /* short: let go by, to punish it as it misses (seen->reach
             * has the threat margin in it already) */
            if (level->holds &&
                distance > seen->reach - THREAT_MARGIN + level->holds_margin) {
                mind->threat_answered = 1;
                return 0;
            }

            /* time enough to be out of reach: a step back */
            if (to_hit >= 6 && distance + 1500 > seen->reach &&
                port_CpuClear(fighter, CPU_PROBE_AWAY) &&
                fighter->move_kind != MOVE_KIND_RUN &&
                port_CpuChance(mind, level->evade, CHANCE_BYTE)) {
                mind->threat_answered = 1;
                port_CpuStart(mind, SCRIPT_STEP_AWAY, WHY_EVADE);
                return 0;
            }
            /* the guard, as the blade comes */
            if (to_hit <= 1) {
                mind->threat_answered = 1;
                if (!port_CpuChance(
                        mind, read < CHANCE_MOST ? read : CHANCE_MOST,
                        CHANCE_BYTE)) {
                    kind = !kind;
                }
                port_CpuStart(
                    mind, kind ? SCRIPT_GUARD_FRONTAL : SCRIPT_GUARD_REVERSE,
                    WHY_GUARD);
                return 0;
            }
            /* meanwhile backs off, if there is room */
            return level->backs_off && port_CpuClear(fighter, CPU_PROBE_AWAY) &&
                           fighter->move_kind != MOVE_KIND_RUN
                       ? FIGHTER_PAD_AWAY
                       : 0;
        }
    }

    /* -- an opening -- */
    if (fighter->move_kind == MOVE_KIND_STANCE ||
        fighter->move_kind == MOVE_KIND_WALK) {
        s32 open = port_CpuSeenOpen(seen);

        /* an attack that missed, and has frames to go (more, against one
         * who goes on from a miss into another) */
        if (MOVE_IS_ATTACK(seen->move_kind) &&
            seen->move_frame > seen->hit_end &&
            port_CpuFramesTo(seen, seen->frames, ago) >
                level->punish_frames +
                    (level->reads_answers && port_CpuHeFollows(mind)
                         ? FOLLOWER_FRAMES
                         : 0)) {
            open = TRUE;
        }
        if (open && distance < my_reach + OPENING_MARGIN &&
            angle < ANGLE_TURN / 12) {
            /* the frames he has still to go in it, as it saw them, and a
             * few as he comes out of it */
            s32 within = -1;

            if (level->times_openings) {
                within =
                    port_CpuFramesTo(seen, seen->frames, ago) + OPENING_GRACE;
                if (within < 0) {
                    within = 0;
                }
            }

            if (mind->noticed_motion != seen->motion_index) {
                mind->noticed_motion = seen->motion_index;
                mind->noticed = port_CpuChance(
                    mind,
                    port_CpuWeighed(mind, level, WHY_OPENING, level->punish),
                    CHANCE_BYTE);
            }
            if (mind->noticed && port_CpuStrikeWithin(fighter, mind, distance,
                                                      within, WHY_OPENING)) {
                return 0;
            }
        } else if (
            open && level->rushes && distance >= my_reach &&
            distance < my_reach + LUNGE_DISTANCE && angle < ANGLE_TURN / 12 &&
            port_CpuFramesTo(seen, seen->frames, ago) >= LUNGE_FRAMES_LEAST &&
            port_CpuClear(fighter, CPU_PROBE_TOWARD)) {
            /* just out of its reach, and time enough: a step in, and the
             * blow after it */
            if (mind->noticed_motion != seen->motion_index) {
                mind->noticed_motion = seen->motion_index;
                mind->noticed = port_CpuChance(
                    mind,
                    port_CpuWeighed(mind, level, WHY_OPENING, level->punish),
                    CHANCE_BYTE);
                if (mind->noticed) {
                    mind->lunging = 1;
                    port_CpuStart(mind, SCRIPT_STEP_TOWARD, WHY_LUNGE);
                    return 0;
                }
            }
        } else if (!open) {
            mind->noticed_motion = 0xFFFF;
        }
        /* down: a blow at him */
        if (seen->move_kind == MOVE_KIND_DOWN &&
            distance < CPU_STRIKE_FALLEN_RANGE && angle < ANGLE_TURN / 12 &&
            port_CpuChance(
                mind, level->punish, CHANCE_BYTE * FALLEN_CHANCE_SHARE)) {
            port_CpuStart(mind, SCRIPT_STRIKE_FALLEN, WHY_FALLEN);
            return 0;
        }
    }

    /* -- he walks into its reach: met with a blow -- */
    if (distance > my_reach + OPENING_MARGIN) {
        mind->meet_armed = 1;
    } else if (mind->meet_armed && distance < my_reach &&
               (seen->move_kind == MOVE_KIND_WALK ||
                seen->move_kind == MOVE_KIND_RUN ||
                seen->move_kind == MOVE_KIND_STEP) &&
               (standing || fighter->move_kind == MOVE_KIND_WALK) &&
               angle < ANGLE_TURN / 12) {
        mind->meet_armed = 0;
        if (port_CpuChance(
                mind, port_CpuWeighed(mind, level, WHY_MEET, level->meet),
                CHANCE_BYTE) &&
            port_CpuStrikeWithin(
                fighter, mind, distance, port_CpuQuick(level), WHY_MEET)) {
            return 0;
        }
    }

    /* -- after a step: a lunge's blow, a feint's step back -- */
    if (mind->lunging) {
        mind->lunging = 0;
        if (distance < my_reach + OPENING_MARGIN && angle < ANGLE_TURN / 12 &&
            !MOVE_IS_ATTACK(seen->move_kind) &&
            port_CpuStrikeWithin(
                fighter, mind, distance, port_CpuQuick(level), WHY_LUNGE)) {
            return 0;
        }
    }
    if (mind->feinting) {
        mind->feinting = 0;
        if (port_CpuClear(fighter, CPU_PROBE_AWAY)) {
            port_CpuStart(mind, SCRIPT_STEP_AWAY, WHY_FEINT);
            return 0;
        }
    }

    /* -- his habits -- */
    if (level->reads_habits) {
        if (distance < his_reach + HABIT_STANDING_MARGIN &&
            (seen->move_kind == MOVE_KIND_STANCE ||
             seen->move_kind == MOVE_KIND_WALK)) {
            if (mind->in_reach_frames < S16_MOST) {
                mind->in_reach_frames++;
            }
        }
        if (!MOVE_IS_ATTACK(seen->move_kind)) {
            mind->habit_motion = 0xFFFF;
        } else if (distance < his_reach + HABIT_ATTACK_MARGIN &&
                   mind->habit_motion != seen->motion_index) {
            mind->habit_motion = seen->motion_index;
            if (mind->in_reach_attacks < S16_MOST) {
                mind->in_reach_attacks++;
            }
        }
    }
    if (level->reads_answers) {
        port_CpuReadAnswers(mind, seen, distance, his_reach);
    }

    /* -- the distance -- */
    if (mind->margin_frames <= 0) {
        mind->margin =
            (s16)(level->margin +
                  (s32)(port_CpuRandom(mind) % (2 * level->spread + 1)) -
                  level->spread);
        mind->margin_frames = MARGIN_FRAMES;
    }
    mind->margin_frames--;
    want = his_reach + mind->margin;
    if (level->reads_habits && mind->in_reach_frames > HABIT_FRAMES_LEAST) {
        /* a hasty opponent (an attack a second or more within his reach):
         * stood at the edge of it to draw him; a patient one: gone in on */
        s32 rate = mind->in_reach_attacks * FRAMES_A_SECOND * HABIT_RATE_ONE /
                   mind->in_reach_frames;

        if (rate >= HABIT_RATE_HASTY && level->baits) {
            want = his_reach - BAIT_DEPTH;
        } else if (rate < HABIT_RATE_PATIENT) {
            want = my_reach - PRESS_DEPTH;
        }
    }
    /* pressing: in, as far as within its own reach */
    pressure = port_CpuPressure(fighter, mind, level, seen);
    pressed = my_reach - PRESS_DEPTH;
    if (level->outside && pressed < his_reach + OUTSIDE_MARGIN) {
        pressed = his_reach + OUTSIDE_MARGIN;
    }
    if (pressure > 0 && want > pressed) {
        want -= (want - pressed) * pressure / PRESSURE_ALL;
    }
    if (want < NEAREST_WANTED) {
        want = NEAREST_WANTED;
    }

    /* -- from afar: a gun, a thing thrown, dirt, a leap; a rush at an
     * opening; off an edge -- */
    {
        u32 far = port_CpuFromAfar(fighter, mind, level, seen, distance, angle,
                                   my_reach, his_reach, pressure, ago, &want);

        if (far != 0) {
            return far == DODGE_STARTED ? 0 : far;
        }
    }

    /* -- a strike of its own: standing, or pressing and walking in -- */
    /* (pressing, a lesser level swings from as far as it misjudges) */
    if ((standing || (pressure > 0 && fighter->move_kind == MOVE_KIND_WALK)) &&
        distance <
            my_reach + (pressure > 0 && level->slack > 0 ? level->slack : 0) &&
        angle < ANGLE_TURN / 12 && !MOVE_IS_ATTACK(seen->move_kind) &&
        port_CpuChance(
            mind,
            (level->initiative * (seen->wounds != 0 ? 2 : 1) +
             level->aggression * pressure / PRESSURE_ALL) *
                port_CpuTacticWeight(mind, level, WHY_FIRST) / CHANCE_BYTE,
            CHANCE_FRAME)) {
        if (port_CpuStrikeWithin(
                fighter, mind, distance, port_CpuQuick(level), WHY_FIRST)) {
            return 0;
        }
    }
    /* a lunge: just past its reach, a step in and a blow */
    if (pressure >= LUNGE_PRESSURE &&
        (standing || fighter->move_kind == MOVE_KIND_WALK) &&
        distance >= my_reach && distance < my_reach + LUNGE_DISTANCE &&
        angle < ANGLE_TURN / 12 && !MOVE_IS_ATTACK(seen->move_kind) &&
        port_CpuClear(fighter, CPU_PROBE_TOWARD) &&
        port_CpuChance(
            mind,
            level->lunge * pressure / PRESSURE_ALL *
                port_CpuTacticWeight(mind, level, WHY_LUNGE) / CHANCE_BYTE,
            CHANCE_FRAME)) {
        mind->lunging = 1;
        port_CpuStart(mind, SCRIPT_STEP_TOWARD, WHY_LUNGE);
        return 0;
    }
    /* a feint: a step into his reach and out again, to draw a blow that
     * misses (an opening, above) */
    meeter = level->reads_answers && port_CpuHeMeets(mind);
    if (level->feint != 0 &&
        (meeter || (pressure >= FEINT_PRESSURE_LEAST &&
                    pressure <= FEINT_PRESSURE_MOST)) &&
        standing && distance > his_reach &&
        distance < his_reach + FEINT_DISTANCE && angle < ANGLE_TURN / 12 &&
        (seen->move_kind == MOVE_KIND_STANCE ||
         seen->move_kind == MOVE_KIND_WALK) &&
        port_CpuClear(fighter, CPU_PROBE_TOWARD) &&
        port_CpuChance(
            mind, level->feint * (meeter ? MEETER_FEINTS : 1), CHANCE_FRAME)) {
        mind->feinting = 1;
        port_CpuStart(mind, SCRIPT_STEP_TOWARD, WHY_FEINT);
        return 0;
    }

    /* -- standing where it likes -- */
    if (mind->hold_frames > 0) {
        mind->hold_frames--;
        return mind->hold;
    }
    mind->hold = port_CpuWalk(
        fighter, seen->x, seen->z, distance, want, level->tolerance, angle);
    mind->hold_frames = 4;
    return mind->hold;
}

/* ---- the pad ---- */

void port_CpuBuildPad(s32 fighter_index, PadState *pad_out) {
    Fighter *fighter = &g_fighters[fighter_index];
    PortCpuMind *mind = port_CpuMindOf(fighter_index);
    PadState pad;
    u32 held = 0;
    s32 i;

    port_CpuLook(fighter, mind);
    /* the ground about it, as a player sees it */
    cpu_PortProbeGround(fighter);
    fighter->cpu_frames++;
    port_CpuOwnAttackDone(fighter, mind);
    port_CpuNoteQuiet(fighter, mind);
    /* a new reaction time for each thing it answers */
    if (mind->frames % 32 == 0) {
        mind->reaction = (u8)(g_port_cpu_levels[mind->level].reaction +
                              port_CpuRandom(mind) %
                                  (g_port_cpu_levels[mind->level].jitter + 1));
        if (mind->reaction < REACTION_LEAST) {
            mind->reaction = REACTION_LEAST;
        }
    }
    /* an attack not yet begun is given up if he has gone out of its
     * reach meanwhile */
    if (mind->script == SCRIPT_ATTACK && !MOVE_IS_ATTACK(fighter->move_kind) &&
        port_CpuDistance(
            fighter, port_CpuSeenAgo(fighter, mind, mind->reaction)) >
            mind->attack_far + GIVE_UP_MARGIN) {
        mind->script = SCRIPT_NONE;
        mind->own_live = 0;
    }
    for (i = 0; i < 3; i++) {
        if (mind->script != SCRIPT_NONE) {
            held = port_CpuStepScript(fighter, mind);
            break;
        }
        held = port_CpuDecide(fighter, mind);
        if (mind->script == SCRIPT_NONE) {
            break;
        }
    }
    /* a walk under way is kept going while an attack button is down, as
     * a player's thumb stays on the direction */
    if ((held & (FIGHTER_PAD_FRONTAL_ATTACK | FIGHTER_PAD_REVERSE_ATTACK)) &&
        fighter->move_kind == MOVE_KIND_WALK) {
        held |= mind->last_held & FIGHTER_PAD_DIRECTIONS;
    }
    /* the buttons of a pad (the stick buttons are R1 and the first attack,
     * as fighter_ReadPad has them) */
    if (held & BUTTON(MOVE_COMMAND_BUTTON_COUNTER)) {
        held = (held & ~BUTTON(MOVE_COMMAND_BUTTON_COUNTER)) | PADR1;
    }
    if (held & BUTTON(MOVE_COMMAND_BUTTON_OFF_ARM)) {
        held = (held & ~BUTTON(MOVE_COMMAND_BUTTON_OFF_ARM)) |
               FIGHTER_PAD_FRONTAL_ATTACK;
    }
    held &= 0xFFFF & ~(PADselect | PADstart);
    /* the pads' types as a player's pad has them */
    pad = g_pad_state;
    pad.held = held;
    pad.pressed = held & ~mind->last_held;
    pad.released = ~held & mind->last_held;
    pad.unheld = ~held & 0xFFFF;
    pad.type[fighter_index] = PAD_TYPE_DIGITAL;
    mind->last_held = held;
    mind->frames++;
    fighter->cpu_pad = pad;
    *pad_out = pad;
}
