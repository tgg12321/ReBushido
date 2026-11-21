#ifndef PORT_RANKED_H
#define PORT_RANKED_H

/* The ranked ladder's client (src/port/ranked.c; docs/NETPLAY.md, section
 * 17): outside the state, as the lobby's client is.
 *
 * A player is the Ed25519 key their program makes on first use, kept in a
 * file beside the settings (port_RankedSetIdentity); the lobby knows them
 * by its public half and the name they chose. What changes the ladder (a
 * name, a place in the queue, a match's result) is signed with that key.
 * The lobby pairs players of the queue by rating and round trip; a match
 * is a session of two met as a room's match is, by a code and a ticket,
 * played to its end under the ladder's rule (First to N, the stage the
 * lobby drew), and both programs report its result with the match's
 * digest: a hash of its every final input and state hash. Reports that
 * agree are rated (Glicko-2); others are held as disputed, and the
 * lobby may ask for the replays of the match's fights. */

#include "common.h"
#include "port_net.h"
#include "port_room.h"

#define PORT_RANKED_ROWS 8
#define PORT_RANKED_WHY_BYTES 240
#define PORT_RANKED_PATH_BYTES 512
#define PORT_RANKED_PRINT_BYTES 20

/* What the client is doing. */
enum {
    PORT_RANKED_OFF = 0,
    PORT_RANKED_ASKING = 1,  /* who this player is: the lobby's answer */
    PORT_RANKED_IDLE = 2,    /* known, not queued */
    PORT_RANKED_QUEUED = 3,  /* waiting for a match */
    PORT_RANKED_MATCHED = 4, /* a match given: met and played */
    PORT_RANKED_REPORTED = 5, /* its result told, the other's awaited */
    PORT_RANKED_DECIDED = 6, /* rated, disputed or void */
};

/* A match's state at the lobby. */
enum {
    PORT_RANKED_PLAYING = 0,
    PORT_RANKED_RATED = 1,
    PORT_RANKED_DISPUTED = 2,
    PORT_RANKED_VOID = 3,
};

/* Why a match ended, as its report says. */
enum {
    PORT_RANKED_ENDED = 1,      /* played to its end */
    PORT_RANKED_OTHER_LEFT = 2, /* the other left, or went quiet */
    PORT_RANKED_I_LEFT = 3,     /* this player left */
    PORT_RANKED_DESYNC = 4,
    PORT_RANKED_NOT_MET = 5, /* the two never met */
};

/* The ladder's lists. */
enum {
    PORT_RANKED_TOP = 0,    /* from a place on */
    PORT_RANKED_AROUND = 1, /* around the player's own */
    PORT_RANKED_MINE = 2,   /* the player's recent matches */
    PORT_RANKED_RECENT = 3, /* everyone's */
};

/* A row of a list: a player's place, or a match. */
typedef struct {
    u32 rank;
    s32 rating;
    s32 rd;
    s32 wins;
    s32 losses;
    s32 draws;
    s32 provisional;
    char player[PORT_RELAY_PLAYER_BYTES];
    /* a match's */
    u32 when; /* seconds since 1970, UTC */
    s32 winner; /* PORT_ROOM_WON_..., or PORT_ROOM_NOBODY */
    s32 state;  /* PORT_RANKED_RATED, ... */
    s32 delta_first;
    s32 delta_second;
    char second[PORT_RELAY_PLAYER_BYTES];
} PortRankedRow;

typedef struct {
    s32 phase; /* PORT_RANKED_... */
    s32 registered;
    char player[PORT_RELAY_PLAYER_BYTES];
    char print[PORT_RANKED_PRINT_BYTES]; /* the key's first bytes, in hex */
    s32 rating;
    s32 rd;
    s32 provisional;
    u32 rank; /* 0: not yet rated */
    u32 of;
    s32 wins;
    s32 losses;
    s32 draws;
    s32 first_to; /* the ladder's rule: First to N */
    u32 rtt;      /* ms, to the lobby */
    /* in the queue */
    u32 waited; /* seconds */
    u32 queue;  /* players in it */
    /* the match */
    u32 match;
    s32 role; /* PORT_SESSION_HOST or _JOIN */
    s32 stage;
    char opponent[PORT_RELAY_PLAYER_BYTES];
    s32 opponent_rating;
    /* its result */
    s32 state; /* PORT_RANKED_PLAYING, _RATED, ... */
    s32 winner;
    s32 won; /* this player's: 1 won, 0 lost, 2 a draw, -1 none */
    s32 before;
    s32 after;
    s32 other_before;
    s32 other_after;
    s32 uploading;
    u32 uploaded;
    u32 upload_total;
    /* the list last had */
    s32 list_kind;
    u32 list_first;
    s32 list_count;
    u32 list_made; /* a count, new for each list had */
    PortRankedRow rows[PORT_RANKED_ROWS];
    /* what went wrong last, in a player's words, or "" */
    char why[PORT_RANKED_WHY_BYTES];
} PortRankedView;

/* ---- the identity ---- */

/* The identity's file (the window: beside rebushido.cfg; --identity): read
 * as the client starts, made there if it is not. */
extern void port_RankedSetIdentity(const char *path);
extern const char *port_RankedIdentityPath(void);
/* A copy of the identity's file beside it, named by the date: 1 and its
 * path, or 0 with why. */
extern s32 port_RankedBackUp(char *out, u32 room);

/* ---- the client ---- */

/* The lobby at `lobby` (ADDRESS:PORT) asked who this player is, as
 * `player`: 1, or 0 with why in a player's words (no identity, no
 * network, a lobby of another version, no ladder there). Nothing waits
 * past a few seconds. */
extern s32 port_RankedStart(
    const char *lobby, const char *player, char *why, u32 room);
extern void port_RankedStop(void);
extern s32 port_RankedIsOn(void);
/* Each turn of whatever runs: the lobby's answers read, the asks said
 * again until answered, the queue and the match said to be stayed in. */
extern void port_RankedTick(void);
extern void port_RankedGet(PortRankedView *out);
/* Into the queue, and out of it; the name changed; a list of the ladder
 * asked for (its answer in the view's rows). */
extern void port_RankedQueue(void);
extern void port_RankedLeaveQueue(void);
extern void port_RankedRename(const char *player);
extern void port_RankedAskList(s32 kind, u32 first);
/* A wait of up to `wait_ms` for the lobby's next words, each read. */
extern void port_RankedWait(u32 wait_ms);
/* A program with no pages (--ranked-lists): the player, the ladder's top
 * and their own recent matches asked for and printed to the log, as the
 * ladder's page shows them; a few seconds at most. */
extern void port_RankedPrintLists(void);

/* ---- the match ---- */

/* The match the lobby gave, to be played (online.c), once: 1 and it. */
extern s32 port_RankedNext(PortRoomNext *out);
/* Its session's rule (port_HoldMatch's word): First to N. */
extern u32 port_RankedRule(void);
/* Its session begins (the state of the game's start put back), and ends:
 * why (PORT_NETPLAY_END_...), who won (PORT_ROOM_WON_...), whether the
 * two met at all. Its result is then reported. */
extern void port_RankedMatchBegins(void);
extern void port_RankedMatchEnds(s32 end, s32 winner, s32 met);
/* (its stage: port_stage_choice.h's port_RankedStagePlace) */
/* Each frame whose two inputs are final, each final state hash (netplay.c):
 * the match's digest is made of them; and each replay written of it
 * (replay.c), uploaded if the lobby asks. */
extern void port_RankedFrame(u32 frame, u16 first, u16 second);
extern void port_RankedHash(u32 frame, u64 hash);
extern void port_RankedReplayWritten(const char *path);
/* For tests (--ranked-lie): the other player reported the winner. */
extern void port_RankedLie(void);

#endif /* PORT_RANKED_H */
