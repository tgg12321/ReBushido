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
} PortOnlineAsk;

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

extern void (*g_port_online_hook)(s32 event, const char *why);
/* The window's port mapping, for a host whose ask says so (portmap.c):
 * asked as the meeting begins, given back as the session ends. */
extern void (*g_port_online_map_hook)(const char *bind);
extern void (*g_port_online_unmap_hook)(void);

#endif /* PORT_ONLINE_H */
