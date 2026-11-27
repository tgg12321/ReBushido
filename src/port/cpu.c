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
} PortCpuLevel;

static PortCpuLevel g_port_cpu_levels[PORT_CPU_LEVELS + 1] = {
    {0},
    /* novice: slow, unsure of the distance, guesses its guards. A
     * beginner's reaction to an attack he must first make out: 11 to 14
     * frames, 0.37 to 0.47 s (at 9 the adept beat it 53% of 96 fights;
     * at 11, 67% of 192: docs/CPU.md, "Measuring the levels") */
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
     .slack = 500},
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
     .learns = TRUE},
    /* veteran: reads the opponent's habits */
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
     .reads_habits = TRUE},
    /* master: a quick player's reaction, never less; draws a hasty
     * opponent out */
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
     .baits = TRUE},
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
/* he is counted as standing within his reach of it within this of it,
 * and an attack he begins as begun there within this */
#define HABIT_STANDING_MARGIN 300
#define HABIT_ATTACK_MARGIN 600
/* an opening is struck at within its own reach and this */
#define OPENING_MARGIN 600
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
};

typedef struct {
    s32 level;  /* 0: the game's CPU plays this place */
    u32 random; /* its dice */
    u32 frames; /* frames it has played */
    u32 last_held;
    PortCpuSeen seen[SEEN_FRAMES];
    /* the script it is playing: which, where in it, how long it waits */
    u8 script;
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
        mind->in_reach_frames = 0;
        mind->in_reach_attacks = 0;
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
    case SCRIPT_GUARD_FRONTAL:
    case SCRIPT_GUARD_REVERSE:
        return CPU_SCRIPT_KIND_GUARD + 1;
    case SCRIPT_NONE:
        return 0;
    }
    return CPU_SCRIPT_KIND_FOOTWORK + 1;
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
};

static const u8 *port_CpuScriptBytes(Fighter *fighter, PortCpuMind *mind) {
    if (mind->script == SCRIPT_ATTACK) {
        return (const u8 *)fighter->cpu_scripts + mind->attack;
    }
    return g_port_cpu_scripts[mind->script];
}

static void port_CpuStart(PortCpuMind *mind, s32 script) {
    mind->script = (u8)script;
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

/* An attack of its stance's list that suits the distance, picked at random
 * by how each has done (from the adept up): its offset in the list, or -1.
 * The list's attacks are the motion file's, each with the distances it is
 * meant for (cpu.h, CPU_ATTACK_...): the moves a player knows. */
static s32 port_CpuPickAttack(Fighter *fighter, PortCpuMind *mind, s32 distance,
                              s32 *index_out, s32 *far_out) {
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
        u8 *script = attack + CPU_ATTACK_HEAD;
        s32 kind = attack[CPU_ATTACK_FLAGS] & CPU_ATTACK_KIND;
        s32 near = attack[CPU_ATTACK_NEAR] * MOTION_REACH_UNIT;
        s32 far = attack[CPU_ATTACK_FAR] * MOTION_REACH_UNIT +
                  fighter->reach_bonus + level->slack;
        s32 fits;
        s32 score;

        if (script[0] == CPU_ATTACK_FOR_SOME) {
            u32 mask = (u32)script[4] << 24 | (u32)script[3] << 16 |
                       (u32)script[2] << 8 | script[1];

            if (!(mask & (1U << fighter->cpu_profile))) {
                continue;
            }
            script += CPU_ATTACK_MASK_BYTES;
        }
        if ((attack[CPU_ATTACK_FLAGS] & CPU_ATTACK_NEEDS_ARM) &&
            fighter->off_arm_ok == 0) {
            continue;
        }
        if (kind == CPU_ATTACK_KIND_DIRT || kind == CPU_ATTACK_KIND_THROW) {
            continue;
        }
        if (kind == 0 || kind == CPU_ATTACK_KIND_LAST) {
            fits = near < distance && distance < far;
        } else {
            /* kinds 1 to 4: for an opponent nearer than its first
             * distance */
            fits = distance < near + fighter->reach_bonus + level->slack;
        }
        if (!fits) {
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
            *far_out = kind == 0 || kind == CPU_ATTACK_KIND_LAST
                           ? far
                           : near + fighter->reach_bonus + level->slack;
        }
    }
    return best;
}

/* Begins an attack that suits the distance, if one does, and counts it
 * tried; FALSE if none does. */
static s32 port_CpuStrike(Fighter *fighter, PortCpuMind *mind, s32 distance) {
    s32 index = -1;
    s32 far = 0;
    s32 offset = port_CpuPickAttack(fighter, mind, distance, &index, &far);

    if (offset < 0) {
        return FALSE;
    }
    port_CpuStart(mind, SCRIPT_ATTACK);
    mind->attack = (u16)offset;
    mind->attack_far = (s16)(far < S16_MOST ? far : S16_MOST);
    mind->own_stance = (s8)fighter->stance;
    mind->own_attack = (s8)index;
    mind->own_wounds = (u8)port_CpuWounds(fighter->other);
    mind->own_live = 1;
    if (fighter->stance < OWN_STANCES && index < OWN_ATTACKS &&
        mind->tried[fighter->stance][index] < 0xFF) {
        mind->tried[fighter->stance][index]++;
    }
    return TRUE;
}

/* Its own attack's end: landed if he is the worse for it. */
static void port_CpuOwnAttackDone(Fighter *fighter, PortCpuMind *mind) {
    if (!mind->own_live || MOVE_IS_ATTACK(fighter->move_kind) ||
        mind->script == SCRIPT_ATTACK) {
        return;
    }
    mind->own_live = 0;
    if ((port_CpuWounds(fighter->other) > mind->own_wounds ||
         fighter->other->is_defeated) &&
        mind->own_stance >= 0 && mind->own_stance < OWN_STANCES &&
        mind->own_attack >= 0 && mind->own_attack < OWN_ATTACKS &&
        mind->landed[mind->own_stance][mind->own_attack] < 0xFF) {
        mind->landed[mind->own_stance][mind->own_attack]++;
    }
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
    s32 my_reach = fighter->attack_reach[fighter->stance];
    s32 want;

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

    /* -- a threat -- */
    if (MOVE_IS_ATTACK(seen->move_kind) && seen->hit_start != 0xFF &&
        seen->move_frame <= seen->hit_end) {
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

            /* time enough to be out of reach: a step back */
            if (to_hit >= 6 && distance + 1500 > seen->reach &&
                port_CpuClear(fighter, CPU_PROBE_AWAY) &&
                fighter->move_kind != MOVE_KIND_RUN &&
                port_CpuChance(mind, level->evade, CHANCE_BYTE)) {
                mind->threat_answered = 1;
                port_CpuStart(mind, SCRIPT_STEP_AWAY);
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
                    mind, kind ? SCRIPT_GUARD_FRONTAL : SCRIPT_GUARD_REVERSE);
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

        /* an attack that missed, and has frames to go */
        if (MOVE_IS_ATTACK(seen->move_kind) &&
            seen->move_frame > seen->hit_end &&
            port_CpuFramesTo(seen, seen->frames, ago) > level->punish_frames) {
            open = TRUE;
        }
        if (open && distance < my_reach + OPENING_MARGIN &&
            angle < ANGLE_TURN / 12) {
            if (mind->noticed_motion != seen->motion_index) {
                mind->noticed_motion = seen->motion_index;
                mind->noticed =
                    port_CpuChance(mind, level->punish, CHANCE_BYTE);
            }
            if (mind->noticed && port_CpuStrike(fighter, mind, distance)) {
                return 0;
            }
        } else if (!open) {
            mind->noticed_motion = 0xFFFF;
        }
        /* down: a blow at him */
        if (seen->move_kind == MOVE_KIND_DOWN &&
            distance < CPU_STRIKE_FALLEN_RANGE && angle < ANGLE_TURN / 12 &&
            port_CpuChance(
                mind, level->punish, CHANCE_BYTE * FALLEN_CHANCE_SHARE)) {
            port_CpuStart(mind, SCRIPT_STRIKE_FALLEN);
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
    if (want < NEAREST_WANTED) {
        want = NEAREST_WANTED;
    }

    /* -- a strike of its own -- */
    if (fighter->move_kind == MOVE_KIND_STANCE && distance < my_reach &&
        angle < ANGLE_TURN / 12 && !MOVE_IS_ATTACK(seen->move_kind) &&
        port_CpuChance(mind, level->initiative * (seen->wounds != 0 ? 2 : 1),
                       CHANCE_FRAME)) {
        if (port_CpuStrike(fighter, mind, distance)) {
            return 0;
        }
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
