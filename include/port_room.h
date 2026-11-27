#ifndef PORT_ROOM_H
#define PORT_ROOM_H

/* A room of several at the lobby (docs/NETPLAY.md, section 16): its
 * client (src/port/room.c), a match's feed for its spectators and a
 * spectator's side of it (src/port/watch.c). None of it is the game's
 * state (config/port_state.txt).
 *
 * A room holds up to eight members, players and spectators. The server
 * keeps it and picks its rotation's matches, a fixed turn (A+B, B+C,
 * C+A) or the winner staying, as the room's host sets; two members may
 * also play a match beside it. Each match is a session of two (session.c,
 * online.c), met through the lobby under the match's code and let in by
 * the ticket the room gives its two players; whoever is not playing
 * watches it: the match's host sends the server the match's settings, its
 * latest fight's start (replay.c) and every input and hash, and the server
 * gives them to every spectator, who puts the start in and runs the
 * inputs on (netplay.c, port_RunWatch). */

#include "common.h"
#include "port_net.h"

#define PORT_ROOM_MEMBERS 8
#define PORT_ROOM_SIDES 4
#define PORT_ROOM_CODE_BYTES 8
#define PORT_ROOM_NOBODY 0xFF

/* The rotation, as the room's host sets it. */
enum {
    PORT_ROOM_RULE_OFF = 0,
    PORT_ROOM_RULE_FIXED = 1,  /* A+B, B+C, C+A, ... */
    PORT_ROOM_RULE_WINNER = 2, /* the winner stays, the next comes in */
};

/* A match's winner: its first player (its host), its second, nobody. */
enum {
    PORT_ROOM_WON_FIRST = 0,
    PORT_ROOM_WON_SECOND = 1,
    PORT_ROOM_DRAW = 2,
};

/* A member's flags. */
#define PORT_ROOM_PLAYS 0x01 /* in the rotation (else a spectator) */
#define PORT_ROOM_BUSY 0x02  /* in a match now */
#define PORT_ROOM_HOST 0x04  /* the room's host */

typedef struct {
    s32 number; /* the room's count of matches; 0: none */
    s32 a;      /* its players' ids: the first hosts it */
    s32 b;
    s32 c; /* a match of four's third and fourth, PORT_ROOM_NOBODY */
    s32 d; /* in one of two; the teams a+c against b+d */
    char code[PORT_ROOM_CODE_BYTES];
    u8 ticket[PORT_RELAY_TICKET_BYTES]; /* only a player has it */
    s32 has_ticket;
} PortRoomMatch;

typedef struct {
    s32 id;
    s32 flags; /* PORT_ROOM_... */
    s32 wins;
    u32 rtt; /* ms, to the server */
    char player[PORT_RELAY_PLAYER_BYTES];
} PortRoomMember;

typedef struct {
    s32 in_room;
    char code[PORT_ROOM_CODE_BYTES];
    char name[PORT_RELAY_NAME_BYTES];
    char setup[PORT_SETUP_CODE_BYTES];
    s32 you;  /* this member's id */
    s32 host; /* the room's host's */
    s32 rule; /* PORT_ROOM_RULE_... */
    s32 plays;
    s32 watching;
    u32 rtt;
    s32 count;
    PortRoomMember members[PORT_ROOM_MEMBERS];
    PortRoomMatch match; /* the rotation's now */
    s32 side_count;
    PortRoomMatch sides[PORT_ROOM_SIDES];
    s32 last_number; /* the last result of the rotation's */
    s32 last_a;
    s32 last_b;
    s32 last_winner; /* PORT_ROOM_WON_..., or PORT_ROOM_NOBODY: left */
    s32 size;        /* the rotation's matches: of 2 or of 4 players */
} PortRoomView;

/* ---- the room (room.c) ---- */

/* A room opened at the lobby `lobby` (ADDRESS:PORT), its rotation's
 * matches of `size` players (2, or 4: two on two, docs/NETPLAY.md,
 * section 18), or one entered by its
 * code, with a password or none ("" or NULL): 1, or 0 with why in a
 * player's words. They wait for the server's answer a few seconds. */
extern s32 port_RoomOpen(
    const char *lobby, const char *name, const char *player,
    const char *password, s32 rule, s32 plays, s32 size, char *why, u32 room);
extern s32 port_RoomEnter(
    const char *lobby, const char *code, const char *player,
    const char *password, s32 plays, char *why, u32 room);
/* The room left (the server told), and everything of it forgotten. */
extern void port_RoomLeave(void);
extern s32 port_RoomIsOn(void);
/* Each turn of whatever runs (the game's boundaries, netplay's frames,
 * the pages): the room said to be stayed in now and then, the server's
 * answers read. Nothing waits. */
extern void port_RoomTick(void);
extern void port_RoomGet(PortRoomView *out);
/* A member's name by its id, "" if none. */
extern const char *port_RoomPlayer(s32 id);
/* What a member asks of the room: the rotation (the host's), whether it
 * plays in it, a match beside it opened or joined (its place among the
 * sides), and a match's result (a player's, as it ends). */
extern void port_RoomSetRule(s32 rule);
extern void port_RoomSetPlays(s32 plays);
extern void port_RoomSideOpen(void);
extern void port_RoomSideJoin(s32 side);
extern void port_RoomResult(s32 number, s32 winner);
/* Whether this member watches the room's matches it is not in. */
extern void port_RoomSetWatching(s32 on);

/* What this member is to do now: play a match (as its host, the first
 * player, or its guest), or watch one. A match played or watched is not
 * asked again (port_RoomDone). */
enum {
    PORT_ROOM_NEXT_NONE = 0,
    PORT_ROOM_NEXT_PLAY = 1,
    PORT_ROOM_NEXT_WATCH = 2,
};

typedef struct PortRoomNext {
    s32 what;
    s32 role; /* PORT_SESSION_HOST or _JOIN */
    s32 side; /* a match beside the room's */
    PortRoomMatch match;
    s32 place;   /* this member's in the match: 0 its host, to 3 */
    s32 players; /* the match's: 2 or 4 */
    char first[PORT_RELAY_PLAYER_BYTES];
    char second[PORT_RELAY_PLAYER_BYTES];
    char third[PORT_RELAY_PLAYER_BYTES];
    char fourth[PORT_RELAY_PLAYER_BYTES];
} PortRoomNext;

extern s32 port_RoomNext(PortRoomNext *out);
extern void port_RoomDone(s32 number);
/* A datagram to the server by the room's socket (watch.c); the room's
 * code and this member's token, 1 if in a room; and a wait of up to
 * `wait_ms` for the server's next words, each read. */
extern void port_RoomSend(const void *bytes, u32 count);
extern s32 port_RoomCode(char *code, u8 *token);
extern void port_RoomWait(u32 wait_ms);

/* ---- a match's feed, its host's (watch.c) ---- */

/* This program hosts `match`: its feed is sent from here on. */
extern void port_FeedBegin(const PortRoomMatch *match);
/* A frame whose inputs are final (netplay.c): the four pads' buttons (a
 * match of two's last two 0); a hash
 * that is final; the session's end, and who won (PORT_ROOM_WON_...). */
extern void port_FeedFrame(u32 frame, const u16 *pads);
extern void port_FeedHash(u32 frame, u64 hash);
extern void port_FeedEnd(u32 frame, s32 winner);
/* Each of netplay's turns: what the server has not said it has, sent. */
extern void port_FeedTick(void);
/* The end said until the server has it and every input before it, three
 * seconds at most; then nothing more is sent. */
extern void port_FeedFinish(void);
extern s32 port_FeedIsOn(void);

/* ---- a spectator (watch.c) ---- */

/* `match` watched from here on: its settings and start asked for. */
extern void port_WatchBegin(const PortRoomMatch *match);
extern void port_WatchStop(void);
extern void port_WatchTick(void);
/* The server's datagram of a feed or a scene, by the room's socket. */
extern void port_WatchTake(const u8 *in, s32 bytes);
/* The match's settings (PORT_SESSION_SETTINGS_BYTES) and its players'
 * names, once had; the start, once whole: its bytes. */
extern const u8 *port_WatchSettings(void);
extern s32 port_WatchStart(const u8 **blob, u32 *size);
/* The start is in the state, at netplay's `frame`: inputs from there are
 * asked for. */
extern void port_WatchStarted(u32 frame);
/* Parted from the players at the hashed frame `frame` (docs/NETPLAY.md,
 * section 21): a start of a later frame is asked for (the state the
 * match's host resynced its players with, or the next fight's), and the
 * one had kept but not given (port_WatchStart) until it comes. */
extern void port_WatchAskStart(u32 frame);
/* A frame's inputs, the four pads' into pads: 1 where they have come. The
 * frames had, from the start on; a final hash of the players', 1 where it has
 * come; the session's end, 1 and its frame where said; the time since the
 * server last said anything. */
extern s32 port_WatchInput(u32 frame, u16 *pads);
extern u32 port_WatchKnown(void);
extern s32 port_WatchHash(u32 frame, u64 *hash);
extern s32 port_WatchEnd(u32 *frame, s32 *winner);
extern u32 port_WatchQuietMs(void);
/* Why there is nothing to watch: the server's word (0: there may be). */
extern s32 port_WatchNothing(void);

#endif /* PORT_ROOM_H */
