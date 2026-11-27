#ifndef PORT_STATS_H
#define PORT_STATS_H

/* The native port's record of a player's fights (ROADMAP 30; docs/PORT.md,
 * "Stats"; docs/PLAYING.md, "Stats"): every fight and match finished, kept
 * in a file of its own beside the settings, and the settings menu's Stats
 * page that counts it up.
 *
 * Two parts. src/port/stats.c watches the game: the game calls the hooks
 * below (under RB_PORT) where it decides a fight and where a blow kills,
 * and each step's boundary takes what they noted into the fight being
 * fought. Nothing of the game's state is read back into the game or
 * changed: the hooks only note. What a rollback runs again is noted
 * again, and a fight is counted once, when its end is final (a session's:
 * no rollback can take it back), never in a frame run ahead, a replay
 * played back, a spectator's match or training. src/port/stats_store.c is
 * the file: read at the start, a line a fight, written whole through a
 * temporary file and a rename after each fight; a file it cannot read is
 * kept aside under another name, never written over. Both are outside the
 * state (config/port_state.txt). */

#include "common.h"

struct Fighter;

/* ---- what is kept of a fight ---- */

/* How a fight was decided. */
enum {
    PORT_STATS_HOW_KILL = 0,      /* a fighter fell */
    PORT_STATS_HOW_SURRENDER = 1, /* the kaishaku: the loser gave up */
    PORT_STATS_HOW_POINTS = 2,    /* a group battle's match on points */
    PORT_STATS_HOW_TIME = 3,      /* ... or on the clock running out */
    PORT_STATS_HOWS = 4
};

/* The fight's result for the player. */
enum {
    PORT_STATS_WON = 0,
    PORT_STATS_LOST = 1,
    PORT_STATS_DRAW = 2,
    PORT_STATS_RESULTS = 3,
    PORT_STATS_GOING = 3 /* a match not decided by this fight */
};

/* The kind of the blow that killed: what struck, and how. */
enum {
    PORT_STATS_BLOW_NONE = 0, /* no kill */
    PORT_STATS_BLOW_STRIKE = 1,
    PORT_STATS_BLOW_RUNNING = 2,  /* an attack from a run */
    PORT_STATS_BLOW_LEAPING = 3,  /* the leaping attack */
    PORT_STATS_BLOW_CLIMBING = 4, /* an attack from a climb */
    PORT_STATS_BLOW_OFF_HAND = 5, /* the second sword */
    PORT_STATS_BLOW_THROWN = 6,   /* a thrown sub-weapon */
    PORT_STATS_BLOW_SHOT = 7,     /* a gun or a sidearm */
    PORT_STATS_BLOW_PAIRED = 8,   /* a paired move */
    PORT_STATS_BLOW_OTHER = 9,
    PORT_STATS_BLOWS = 10
};

/* Where the killing blow landed (the body's points, fighter.h). */
enum {
    PORT_STATS_PLACE_NONE = 0,
    PORT_STATS_PLACE_HEAD = 1,
    PORT_STATS_PLACE_CHEST = 2,
    PORT_STATS_PLACE_HIP = 3,
    PORT_STATS_PLACE_SWORD_ARM = 4,
    PORT_STATS_PLACE_OFF_ARM = 5,
    PORT_STATS_PLACE_LEGS = 6,
    PORT_STATS_PLACES = 7
};

/* A kill: the blow, the place, from behind, on the ground. */
typedef struct {
    u8 blow;
    u8 place;
    u8 behind;
    u8 down;
} PortStatsKill;

/* The wounds of a fighter, as the game counts them (Fighter.leg_wounds,
 * arm_wounds, body_wounds, and the off arm lost). */
enum {
    PORT_STATS_WOUND_LEGS = 0,
    PORT_STATS_WOUND_ARM = 1,
    PORT_STATS_WOUND_BODY = 2,
    PORT_STATS_WOUND_OFF_ARM = 3,
    PORT_STATS_WOUNDS = 4
};

/* Online: none, a session of two PCs, a room's match, the ladder's. */
enum {
    PORT_STATS_LOCAL = 0,
    PORT_STATS_ONLINE = 1,
    PORT_STATS_ROOM = 2,
    PORT_STATS_RANKED = 3
};

/* The opponent: a person (`cpu` PORT_STATS_PERSON) or the CPU of a level
 * (PORT_CPU_..., port_cpu.h: 0 the game's own). */
#define PORT_STATS_PERSON (-1)

#define PORT_STATS_NAME_BYTES 24
#define PORT_STATS_REPLAY_BYTES 64
#define PORT_STATS_DATE_BYTES 24

typedef struct {
    /* when it was decided, the PC's own time */
    u16 year;
    u8 month, day, hour, minute, second;
    u8 mode;   /* enum FightMode */
    u8 online; /* PORT_STATS_LOCAL, ... */
    u8 teams;  /* 1, or 2 for two on two */
    s8 cpu;    /* the opponent's: PORT_STATS_PERSON or a CPU */
    u8 side;   /* the player's: 0 the first fighter's, 1 the second's */
    /* the match it was a fight of (port_match.h): its rule and count; the
     * record's number for the match; the fight's place in it, from 1; and
     * where this fight decided it, its result (PORT_STATS_GOING if not) */
    u8 rule;
    u8 count;
    u8 match_fight;
    u8 match_result;
    u32 match;
    s16 stage;
    s16 fighter[2]; /* roster numbers: the player's, the opponent's */
    s16 weapon[2];
    u8 result; /* PORT_STATS_WON, LOST, DRAW */
    u8 how;    /* PORT_STATS_HOW_... */
    /* the kills each way (a group battle's match or a run of Slash may
     * have many), and the first each way in full */
    u8 kills_given;
    u8 kills_taken;
    PortStatsKill given;
    PortStatsKill taken;
    u8 wounds_given[PORT_STATS_WOUNDS];
    u8 wounds_taken[PORT_STATS_WOUNDS];
    u32 frames;                           /* the game's frames, 30 a second */
    char opponent[PORT_STATS_NAME_BYTES]; /* an online one's name, or "" */
    char replay[PORT_STATS_REPLAY_BYTES]; /* its file in replays/, or "" */
    /* a local tournament's fight (port_tournament.h): the tournament's
     * date as its own line has it (`begun`), or "" */
    char tournament[PORT_STATS_DATE_BYTES];
} PortStatsFight;

/* A local tournament over (port_tournament.h): its own line of the
 * record, `tournament`, beside the fights. Its entrants in their order of
 * entry, each a person (`cpu` PORT_STATS_PERSON, the pad) or a CPU of a
 * level, with its place, its matches lost and its seed; its matches in
 * the order they were played, each its part of the bracket
 * (port_bracket.h, PORT_BRACKET_WINNERS...) and round, its two entrants
 * and its winner. */
#define PORT_STATS_TOURNAMENT_ENTRANTS 8
#define PORT_STATS_TOURNAMENT_MATCHES 15

typedef struct {
    char name[PORT_STATS_NAME_BYTES];
    s8 cpu;
    u8 pad;
    u8 place;
    u8 losses;
    u8 seed;
} PortStatsTournamentEntrant;

typedef struct {
    u8 part;
    u8 round;
    u8 first;
    u8 second;
    u8 winner;
} PortStatsTournamentMatch;

typedef struct {
    /* when it was over, the PC's own time; and when it was begun, as the
     * fights of it have it */
    u16 year;
    u8 month, day, hour, minute, second;
    char begun[PORT_STATS_DATE_BYTES];
    u8 count;
    u8 is_double;
    u8 seeding; /* PORT_TOURNAMENT_SEED_... */
    u8 rule;    /* PORT_MATCH_FIRST_TO or PORT_MATCH_STOCK */
    u8 rule_count;
    u8 champion; /* an entrant's number */
    u8 played;   /* matches */
    PortStatsTournamentEntrant entrant[PORT_STATS_TOURNAMENT_ENTRANTS];
    PortStatsTournamentMatch match[PORT_STATS_TOURNAMENT_MATCHES];
} PortStatsTournament;

/* ---- the file (stats_store.c) ---- */

/* The format's version, the file's first line's number: 2 since the
 * tournaments' lines (a version 1 program keeps a file of 2 aside, as it
 * keeps any later version's; this one reads both). */
#define PORT_STATS_VERSION 2
#define PORT_STATS_PATH_BYTES 512

/* The file is `path` (the window: rebushido-stats.txt beside the
 * settings; --stats): read now. A file that cannot be read is renamed
 * PATH.broken-YYYYMMDD-HHMMSS (the log says so) and a new one begun. ""
 * or NULL: no record is kept. */
extern void port_SetStatsFile(const char *path);
extern const char *port_GetStatsFile(void);
/* The fights kept, oldest first; a count that goes up with every change
 * (the page counts again when it moves). */
extern s32 port_StatsCount(void);
extern const PortStatsFight *port_StatsFightAt(s32 index);
extern u32 port_StatsGeneration(void);
/* A fight added and the file written again: FALSE if it was not written
 * (the record in memory has it all the same). Its `match`, where 0, is
 * given: the last match's again for a fight after the first of a match,
 * a new number otherwise. */
extern s32 port_StatsAdd(PortStatsFight *fight);
/* The replay's name given to the fight `index` and the file written. */
extern void port_StatsSetReplay(s32 index, const char *name);
/* The tournaments kept, oldest first; one added and the file written
 * again (FALSE as port_StatsAdd's). */
extern s32 port_StatsTournamentCount(void);
extern const PortStatsTournament *port_StatsTournamentAt(s32 index);
extern s32 port_StatsAddTournament(const PortStatsTournament *tournament);

/* A fight as the file's line, and back: the line's length (0: no room),
 * or FALSE for a line that is not a fight. */
extern u32 port_StatsFormat(const PortStatsFight *fight, char *out, u32 room);
extern s32 port_StatsParse(const char *line, u32 length, PortStatsFight *out);
/* ... and a tournament's */
extern u32 port_StatsFormatTournament(
    const PortStatsTournament *tournament, char *out, u32 room);
extern s32 port_StatsParseTournament(
    const char *line, u32 length, PortStatsTournament *out);

/* ---- the counting (stats_store.c), for the page and the tests ---- */

#define PORT_STATS_KEYS 64
#define PORT_STATS_OPPONENTS 40

/* What is won, lost and drawn of some fights. */
typedef struct {
    u32 fights;
    u32 won;
    u32 lost;
    u32 drawn;
} PortStatsScore;

/* The ways the record is broken down by mode. */
enum {
    PORT_STATS_BY_STORY = 0,
    PORT_STATS_BY_VSCPU = 1,
    PORT_STATS_BY_SLASH = 2,
    PORT_STATS_BY_VERSUS = 3,
    PORT_STATS_BY_GROUP = 4,
    PORT_STATS_BY_LINK = 5,
    PORT_STATS_BY_ONLINE = 6,
    PORT_STATS_BY_RANKED = 7,
    PORT_STATS_BY_TWO_ON_TWO = 8, /* beside its mode's */
    PORT_STATS_BY_TOURNAMENT = 9, /* ... and a tournament's fights */
    PORT_STATS_MODES = 10
};

typedef struct {
    char name[PORT_STATS_NAME_BYTES]; /* "" with a CPU's or a person's */
    s8 cpu;                           /* PORT_STATS_PERSON or the level */
    u8 online;
    PortStatsScore score;
} PortStatsOpponent;

typedef struct {
    PortStatsScore fights;
    PortStatsScore matches; /* each match counted at the fight ending it */
    PortStatsScore by_fighter[PORT_STATS_KEYS]; /* the player's fighter */
    PortStatsScore by_weapon[PORT_STATS_KEYS];  /* the player's weapon */
    PortStatsScore by_stage[PORT_STATS_KEYS];
    PortStatsScore by_against[PORT_STATS_KEYS]; /* the opponent's fighter */
    PortStatsScore by_mode[PORT_STATS_MODES];
    PortStatsScore by_how[PORT_STATS_HOWS]; /* how it was decided */
    PortStatsOpponent opponents[PORT_STATS_OPPONENTS];
    s32 opponent_count;
    /* kills: given and taken, the first of each fight each way by its
     * blow and its place */
    u32 kills_given;
    u32 kills_taken;
    u32 given_by_blow[PORT_STATS_BLOWS];
    u32 given_by_place[PORT_STATS_PLACES];
    u32 taken_by_blow[PORT_STATS_BLOWS];
    u32 taken_by_place[PORT_STATS_PLACES];
    u32 given_behind;
    u32 taken_behind;
    u32 wounds_given[PORT_STATS_WOUNDS];
    u32 wounds_taken[PORT_STATS_WOUNDS];
    /* the longest run of fights won, and the one going now */
    u32 longest_run;
    u32 run_now;
    u32 frames; /* fought, in all */
} PortStatsTally;

extern void port_StatsTally(PortStatsTally *out);
/* The by-mode row a fight counts in (PORT_STATS_BY_...; two on two is
 * counted besides). */
extern s32 port_StatsModeOf(const PortStatsFight *fight);

/* The words of the file and of the page. */
extern const char *port_StatsHowWord(s32 how);
extern const char *port_StatsBlowWord(s32 blow);
extern const char *port_StatsPlaceWord(s32 place);
extern const char *port_StatsModeWord(s32 by_mode);

/* ---- what the game calls (stats.c, under RB_PORT) ---- */

/* A blow struck `victim` (fighter_TakeHit, by a blade or a thrown thing
 * or a shot; `result` its answer, 1 for a kill): what killed it, noted. */
extern void port_StatsHit(
    const struct Fighter *victim, s32 body_point, s32 facing, s32 by_object,
    s32 blade, s32 by_sidearm, s32 result);
/* `fighter` fell this frame (fighter_Update: the move of death begins, or
 * a paired move's blow landed). */
extern void port_StatsFell(const struct Fighter *fighter);
/* The mode decides what follows a fallen fighter (fight.c,
 * fight_AfterDefeat), or a kaishaku's scene is over (fight_after.c): the
 * fight is decided, PORT_STATS_HOW_KILL or _SURRENDER. */
extern void port_StatsFightDecided(s32 how);
/* A group battle's match is recorded (fight_modes.c,
 * fight_RecordTeamMatch), on points or on the clock. */
extern void port_StatsTeamMatch(s32 on_time);

/* ---- the host's (stats.c) ---- */

/* At each step's boundary, before the replay's (replay.c,
 * port_ReplayBoundary): what the step noted taken in, a fight's end
 * counted where it is final. */
extern void port_StatsBoundary(void);
/* A replay's file was written (replay.c): its name, and the input's
 * number at the fight's first boundary out of it, given to its fight. */
extern void port_StatsReplayWritten(const char *path, u32 end_count);
/* A resync (netplay.c; docs/NETPLAY.md, section 21): `back` inputs
 * before now the last frame every machine held the same; `loaded` where
 * this machine put the host's state in. */
extern void port_StatsResynced(u32 back, s32 loaded);

#endif /* PORT_STATS_H */
