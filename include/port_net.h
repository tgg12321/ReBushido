#ifndef PORT_NET_H
#define PORT_NET_H

/* The native port's network (src/port/net.c; docs/NETPLAY.md, sections
 * 11 to 13): one socket of UDP datagrams to one other program of the
 * same build. The game's own files never see it: the link cable's calls
 * are in port_platform.h, and netplay goes in by the input's hook. */

#include "common.h"
#include "port_host.h"

/* ---- the socket (net.c) ---- */

/* The longest datagram the port sends or reads: under what a path across
 * the internet carries whole. */
#define PORT_NET_DATAGRAM_MAX 1200

/* `ADDRESS:PORT` (four numbers and a port) into `out`: 1, or 0 if it is
 * not one. Port 0, which a socket to be bound may have (the system
 * chooses one), is the caller's to refuse where it means nothing. */
extern s32 port_ParseNetAddress(const char *text, PortNetAddress *out);
/* Where the run's socket is bound (--net-bind) and the one other program
 * it talks to (--net-peer). Nothing else is listened to. */
extern void port_SetNetAddresses(
    const PortNetAddress *bind, const PortNetAddress *peer);
/* A socket bound to `bind` that waits to be found: the first program of
 * the port that sends it a datagram becomes its peer (--host). */
extern void port_SetNetListen(const PortNetAddress *bind);
/* The peer, once there is one: 1 and its address, or 0. */
extern s32 port_GetNetPeer(PortNetAddress *peer);
/* From now on only the peer is answered (the host has found its guest). */
extern void port_FixNetPeer(void);
/* A worse network on purpose, for tests: each datagram sent waits
 * `latency_ms` before it goes (--net-latency), and `loss` in a hundred
 * are dropped (--net-loss), by the run's own sequence of random numbers. */
extern void port_SetNetTrouble(u32 latency_ms, u32 loss);
/* Opens the socket, if the run asked for one: 1, or 0 if it could not. */
extern s32 port_StartNet(void);
extern s32 port_IsNetOn(void);
/* The link cable's side (--link-side): 0, the first player, or 1. */
extern void port_SetLinkSide(s32 side);

/* A datagram to the other program, by way of --net-latency and
 * --net-loss. */
extern void port_NetSend(const void *bytes, u32 count);
/* The other program's next datagram, waiting up to `wait_ms` for it: its
 * length, or -1 if none came. */
extern s32 port_NetReceive(void *bytes, u32 room, u32 wait_ms);
/* Every datagram of the port's begins with eight bytes: a mark, its kind
 * (NET_KIND_..., net.c; netplay's from PORT_NET_KIND_NETPLAY up, the
 * session's from PORT_NET_KIND_SESSION), a byte of the kind's own and a
 * number. */
#define PORT_NET_HEADER_BYTES 8
#define PORT_NET_KIND_NETPLAY 16
#define PORT_NET_KIND_SESSION 32
extern void port_NetHeader(u8 *out, s32 kind, s32 detail, u32 number);
/* A datagram's kind, its detail and its number; 0 if it is not the port's. */
extern s32 port_NetReadHeader(
    const u8 *in, s32 bytes, s32 *detail, u32 *number);
/* A word of four bytes, the low one first, out and in. */
extern void port_NetPutWord(u8 *out, u32 value);
extern u32 port_NetGetWord(const u8 *in);
/* The PC's clock in milliseconds since the program started: for the
 * network's waits, its ping and its pacing, never for the game. */
extern u32 port_NetClock(void);

/* What the network did, to the error output, as the run ends. */
extern void port_PrintNetReport(void);

/* ---- rollback netplay (src/port/netplay.c; docs/NETPLAY.md, section
 * 12) ---- */

/* This program plays pad `pad` (0 or 1: --netplay 1 or 2) and its
 * presses count `delay` frames on (--net-delay); the other program plays
 * the other pad. */
extern void port_SetNetplay(s32 pad, u32 delay);
extern s32 port_IsNetplayOn(void);
/* Where the other program has gone, this one plays on alone instead of
 * ending (the window's: a player is not thrown out of the game). */
extern void port_SetNetplayGoesOnAlone(s32 on);
/* The session: the game a frame at a time, for ever (src/port/main.c). */
extern void port_RunNetplay(void) __attribute__((noreturn));
/* The run ends: the other is told, and the report (port_Quit). */
extern void port_EndNetplay(void);

/* What netplay is doing, for the window's line (sdl_net.c). */
enum {
    PORT_NETPLAY_PLAYING = 0,
    PORT_NETPLAY_WAITING = 1, /* for the other's inputs */
    PORT_NETPLAY_LOADING = 2, /* at a load, which both wait at */
    PORT_NETPLAY_ALONE = 3,   /* the other has gone: this one plays on */
};

typedef struct {
    s32 is_on;
    s32 pad;        /* 0 or 1 */
    s32 state;      /* PORT_NETPLAY_... */
    u32 delay;      /* frames */
    u32 ping_ms;    /* the round trip, smoothed */
    u32 rollback;   /* the most frames run again in one rollback lately */
    u32 rollbacks;  /* in all */
    s32 ahead;      /* frames this program is ahead, in sixteenths */
    u32 frame;      /* the game's frame (steps since the session began) */
    u32 waited_ms;  /* how long the present wait has been */
    u32 alone_ms;   /* how long ago the other went, if it has */
    u32 held_ms;    /* how long this program has held back, in all */
    s32 is_session; /* set up by --host or --join */
} PortNetplayStats;

extern void port_GetNetplayStats(PortNetplayStats *stats);

/* What the window hangs on netplay; each NULL in the build without one.
 * None changes what the game computes. */
/* This program is ahead of the other: the window holds its next blanks
 * back by this many blanks' time in all (GGPO's time sync). */
extern void (*g_port_net_hold_hook)(u32 blanks);
/* Called over and over while the program waits on the network, in the
 * session's set-up as in the game: `status` says for what, in words. The
 * window reads its events (its close ends the program) and shows it. */
extern void (*g_port_net_wait_hook)(const char *status);
/* A rollback is about to run `blanks` vertical blanks again (`begins` 1),
 * or has run them (0): the window takes the sound it queued of them and
 * has not played back, and queues the corrected sound in its place. */
extern void (*g_port_net_rerun_hook)(s32 begins, u32 blanks);

/* ---- the session (src/port/session.c; docs/NETPLAY.md, section 13)
 * ---- */

enum {
    PORT_SESSION_NONE = 0,
    PORT_SESSION_HOST = 1, /* waits for a guest at --host ADDRESS:PORT */
    PORT_SESSION_JOIN = 2, /* goes to the host at --join ADDRESS:PORT */
};

struct PortDisc;
struct PortInput;

/* The run is a session: hosted at `address`, or joining the host there.
 * `delay` is the host's --net-delay, which both use. */
extern void port_SetSession(s32 role, const PortNetAddress *address);
extern s32 port_GetSessionRole(void);
/* Before the game starts: the other program found, the build, the disc
 * and the protocol held to each other's, the host's memory cards and
 * random seed sent to the guest. 1, or 0 with the reason in the log. On
 * 1 netplay is on, the host the first pad and the guest the second. */
extern s32 port_StartSession(const struct PortDisc *disc, u32 delay);
/* At frame `frame`, before the game takes `input`: from power on to the
 * versus select screen the same presses on both machines (the menus are
 * no player's), and in a fight no Start (the pause menu is off). `used`
 * is given the other pad's buttons that the frame's input still depends
 * on: a guess of the others is never wrong. */
extern void port_SessionInput(u32 frame, struct PortInput *input, u16 *used);
/* The agreed random seed, into the game, at the session's first frame. */
extern void port_SessionSeed(void);

#endif /* PORT_NET_H */
