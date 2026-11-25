#ifndef PORT_ONLINE_H
#define PORT_ONLINE_H

/* A session of netplay begun and ended in the running program
 * (src/port/online.c; docs/NETPLAY.md, section 15): no start again. The
 * state of the game's start is kept as the program begins, before the
 * game's first step; a session puts it back, with the host's cards and
 * seed, and plays from there exactly as a program started for the
 * session would; at its end the player's own state, kept as the session
 * began, is put back, and the game goes on where it was. */
#include "common.h"
#include "port_net.h"

struct PortDisc;

/* What a session is asked to be: what --host, --join and the lobby's
 * options say on the command line, or the window's online pages. */
#define PORT_ONLINE_TEXT_BYTES 64

typedef struct {
    s32 role;                          /* PORT_SESSION_HOST or _JOIN */
    char at[PORT_ONLINE_TEXT_BYTES];   /* the host's own socket, or the
                                          host's address (a room's code
                                          with a lobby) for a guest */
    char bind[PORT_ONLINE_TEXT_BYTES]; /* a guest's own socket, or "" */
    char lobby[PORT_ONLINE_TEXT_BYTES];
    char room[PORT_ONLINE_TEXT_BYTES]; /* a host's room at the lobby */
    char player[PORT_ONLINE_TEXT_BYTES];
    char password[PORT_ONLINE_TEXT_BYTES];
    char key[PORT_ONLINE_TEXT_BYTES * 2]; /* SALT:KEY, or "" */
    char punch_to[PORT_ONLINE_TEXT_BYTES];
    u32 paths; /* --net-paths, or 0 for all */
    u32 delay;
    s32 portmap; /* the host asks its router to open its port */
    /* a room of several's match (docs/NETPLAY.md, section 16): its code
     * (at's, for a guest), its ticket and its number */
    s32 has_ticket;
    u8 ticket[PORT_RELAY_TICKET_BYTES];
    char code[PORT_RELAY_CODE_BYTES];
    s32 room_number;
    /* four players (docs/NETPLAY.md, section 18): the host's players and
     * PCs (--players: 0 or 2 for two; 4 and 4, or 4 and 2), and a room's
     * match guest's seat, its place in the match (1 to 3; 0 any) */
    s32 players;
    s32 pcs;
    s32 seat;
} PortOnlineAsk;

/* A room of several, as a program with no pages is given it (main.c's
 * --room-open and --room-enter, for tests): opened or entered at its
 * first boundary, its matches played and watched as they come. */
typedef struct {
    s32 on;
    char lobby[PORT_ONLINE_TEXT_BYTES];
    char name[PORT_ONLINE_TEXT_BYTES]; /* to open one, or "" */
    char code[PORT_ONLINE_TEXT_BYTES]; /* to enter one */
    char player[PORT_ONLINE_TEXT_BYTES];
    char password[PORT_ONLINE_TEXT_BYTES];
    char bind[PORT_ONLINE_TEXT_BYTES]; /* a match's socket, or "" */
    s32 rule;                          /* PORT_ROOM_RULE_... (the host's) */
    s32 size;        /* the rotation's matches, of 2 or of 4 (the host's) */
    s32 plays;       /* in the rotation, else a spectator */
    s32 watch;       /* watches the room's matches it is not in */
    u32 matches;     /* the program leaves the room, and ends, once the
                        room has had so many results (0: never) */
    s32 side;        /* a match beside: 1 opens one, 2 joins one */
    u32 leave_match; /* leaves its match of this number ... */
    u32 leave_frame; /* ... at this frame of it */
    u32 delay;
    u32 paths;
    /* the ranked ladder instead (--ranked; docs/NETPLAY.md, section 17):
     * queued, its matches played as they come, `matches` of them decided
     * before the program ends; the lists printed then (--ranked-lists);
     * a test's false report of the winner (--ranked-lie) */
    s32 ranked;
    s32 lists;
    s32 lie;
} PortOnlineRoom;

extern void port_OnlineRoomAuto(const PortOnlineRoom *room);
/* A room's match (port_room.h's PortRoomNext) asked for by the pages: it
 * is played or watched at the game's next boundary, by way of `how`'s
 * lobby, with its player's name, socket and delay. NULL, or what is
 * wrong. */
struct PortRoomNext;
extern const char *port_OnlineAskRoom(
    const struct PortRoomNext *next, const PortOnlineAsk *how);
/* Whether the session being met is a room's match: no lobby before it,
 * the room decided. */
extern s32 port_OnlineIsRoomMatch(void);
/* Whether the room is the command line's (--room-open, --room-enter): no
 * pages come up between its matches. */
extern s32 port_OnlineIsRoomAuto(void);
/* A ranked match (port_ranked.h's port_RankedNext) asked for by the
 * pages, as a room's is: NULL, or what is wrong. */
extern const char *port_OnlineAskRanked(
    const struct PortRoomNext *next, const PortOnlineAsk *how);
/* The player asks to leave the session or the watching (the window's
 * keys): once shown, twice left. */
extern void port_OnlineLeave(void);

/* Whether sessions may begin in this program: the window's, or
 * --session-at. Before the game starts (main.c). */
extern void port_OnlineWanted(void);
/* The state of the game's start kept, before the game's first step
 * (main.c): only where sessions are wanted. */
extern void port_OnlineKeepBoot(const struct PortDisc *disc);
extern s32 port_OnlineCanBegin(void);
/* A session asked for: it begins at the game's next boundary. NULL, or
 * what is wrong with the ask, in a player's words. */
extern const char *port_OnlineAskFor(const PortOnlineAsk *ask);
/* --session-at N and --sessions K: the session the command line says
 * begins after N boundaries of the program's own play, K times, one
 * after another; after the last the program ends. */
extern void port_OnlineAt(const PortOnlineAsk *ask, u32 after, u32 times);
/* Each boundary of the program's own play (main.c): a session asked for
 * is met, played and ended here. */
extern void port_OnlineBoundary(void);
/* Whether a session begun in the program is being met or played. */
extern s32 port_OnlineIsBusy(void);

/* What the window is told (PORT_ONLINE_...): the meeting begins, the
 * session begins (the state of the game's start put back), it ends (the
 * player's own put back: `why` says why), the meeting failed (`why`, or
 * NULL where the player gave it up). */
enum {
    PORT_ONLINE_MEETING = 1,
    PORT_ONLINE_BEGINS = 2,
    PORT_ONLINE_ENDS = 3,
    PORT_ONLINE_FAILED = 4,
};

/* (a room's match watched says the same: the waiting for its fight's
 * start is the meeting, PORT_SESSION_PHASE_WATCHING) */

extern void (*g_port_online_hook)(s32 event, const char *why);
/* The window's port mapping, for a host whose ask says so (portmap.c):
 * asked as the meeting begins, given back as the session ends. */
extern void (*g_port_online_map_hook)(const char *bind);
extern void (*g_port_online_unmap_hook)(void);

#endif /* PORT_ONLINE_H */
