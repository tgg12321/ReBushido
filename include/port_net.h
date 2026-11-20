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
/* ... or `[IPv6]:PORT` (an IPv4-mapped one is taken as IPv4). The
 * address as text, as it is parsed, into `out` of `room` bytes
 * (PORT_NET_ADDRESS_TEXT_BYTES is room for any); a cleared one; and
 * whether two are one. */
#define PORT_NET_ADDRESS_TEXT_BYTES 48
extern void port_FormatNetAddress(
    const PortNetAddress *at, char *out, u32 room);
extern void port_ClearNetAddress(PortNetAddress *out);
extern s32 port_NetAddressesEqual(
    const PortNetAddress *a, const PortNetAddress *b);
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
/* The peer is `peer`, fixed, from now on (a relay has said who it is). */
extern void port_SetNetPeerNow(const PortNetAddress *peer);
/* A datagram straight to `to`, past --net-latency and --net-loss, and
 * anybody's next datagram with who sent it (-1 if none came in
 * `wait_ms`): the relay server's few, which are no part of the session
 * (relay.c). */
extern void port_NetSendTo(
    const PortNetAddress *to, const void *bytes, u32 count);
extern s32 port_NetReceiveFrom(
    void *bytes, u32 room, u32 wait_ms, PortNetAddress *from);
/* A worse network on purpose, for tests: each datagram sent waits
 * `latency_ms` before it goes (--net-latency), and `loss` in a hundred
 * are dropped (--net-loss), by the run's own sequence of random numbers. */
extern void port_SetNetTrouble(u32 latency_ms, u32 loss);
/* Opens the socket, if the run asked for one: 1, or 0 if it could not. */
extern s32 port_StartNet(void);
extern s32 port_IsNetOn(void);
/* The socket is of IPv6 (and IPv4 with it): a bind of 0.0.0.0 is one
 * where the system has IPv6. Where it is bound, its port the system's
 * where it chose: 1, or 0. */
extern s32 port_NetIsV6(void);
extern s32 port_GetNetBound(PortNetAddress *at);
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
/* The relay server's (relay.c, tools/relay/): a server passes on every
 * datagram of a kind below this one and reads the rest itself. */
#define PORT_NET_KIND_RELAY 48
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
/* The session could not be set up, and `why` says so in a player's
 * words: the window shows it until the player has read it (a key, or
 * the window's close), and then the program ends. */
extern void (*g_port_net_fail_hook)(const char *why);
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

/* What two players compare to know they can meet (--check-online, the
 * Play online page): the protocol, and short forms of the build's, the
 * disc's and the gameplay mods' sums, `P7-BBBB-DDDD-MMMM`, into `out`
 * (PORT_SETUP_CODE_BYTES). The disc is the one port_SetSessionDisc
 * named, and the mods are the ones started. */
#define PORT_SETUP_CODE_BYTES 24
extern void port_SetSessionDisc(const struct PortDisc *disc);
extern void port_SessionSetupCode(char *out);
/* The disc's sum in full, worked out once (a replay's, port_replay.h). */
extern u64 port_SessionDiscSum(void);
/* Netplay's frames whose inputs are all known and taken as they are
 * (none of them run on a guess still to be put right): a replay of a
 * session is written once its fight's end is among them. */
extern u32 port_NetplayFinalFrames(void);
/* --check-online: the build, the disc, the mods and the protocol, each
 * in full and as the code, to the output. */
extern void port_PrintSessionCheck(void);
/* What the window's port mapping found (the host's router: "UPnP
 * mapped it: give the other player 203.0.113.7:7845"), said where the
 * host waits; and the way the two programs found to each other ("IPv6,
 * direct", "the host's mapped port", "a punched hole", "the relay"),
 * which the window's line shows: "direct" with none said. */
extern void port_SetSessionMapped(const char *text);
extern void port_SetSessionPath(const char *text);
extern const char *port_GetSessionPath(void);
/* --punch-to: the host sends to the other player's public address while
 * it waits, so that both routers let the two through with no lobby. */
extern void port_SetSessionPunchTo(const PortNetAddress *at);
/* --net-paths' words, `public,lan,mapped,v6,relay`, as PORT_NET_PATH_...
 * bits: 0 if a word is none of them. */
extern u32 port_ParseNetPaths(const char *text);

/* For tests (--session-differ): this program says its build, disc,
 * gameplay mods or protocol is another, so that the other turns it away
 * (SESSION_DIFFER_..., session.c). */
enum {
    PORT_SESSION_DIFFER_NONE = 0,
    PORT_SESSION_DIFFER_BUILD = 1,
    PORT_SESSION_DIFFER_DISC = 2,
    PORT_SESSION_DIFFER_MODS = 3,
    PORT_SESSION_DIFFER_PROTOCOL = 4,
};

extern void port_SetSessionDiffer(s32 what);
/* The session's status and its failure, said to the window or the log
 * (session.c): relay.c's as well as the session's own. */
extern void port_SessionStatus(const char *text);
extern void port_SessionFail(const char *why);

/* ---- the lobby and its relay (src/port/relay.c; docs/NETPLAY.md,
 * section 14) ---- */

/* A room's code at the lobby: six letters and digits, and its end. */
#define PORT_RELAY_CODE_BYTES 8
#define PORT_RELAY_NAME_BYTES 25
#define PORT_RELAY_PLAYER_BYTES 17
#define PORT_RELAY_PASSWORD_BYTES 33
#define PORT_RELAY_SALT_BYTES 16
#define PORT_RELAY_KEY_BYTES 32

/* The ways between two players a meeting may try (--net-paths): each
 * candidate's kind, and the server's relay. */
enum {
    PORT_NET_PATH_PUBLIC = 0x01, /* the address the server saw: a punch */
    PORT_NET_PATH_LAN = 0x02,    /* the other's address on its network */
    PORT_NET_PATH_MAPPED = 0x04, /* the port the other's router mapped */
    PORT_NET_PATH_V6 = 0x08,     /* IPv6, either address */
    PORT_NET_PATH_RELAY = 0x10,  /* the server passing the datagrams on */
    PORT_NET_PATHS_ALL = 0x1F,
};

/* The session meets by way of the lobby server at `server` (--lobby),
 * the guest in the room of `code` (--join CODE), the host in a room the
 * server gives a code (--room NAME). `paths`: PORT_NET_PATH_..., 0 for
 * every one. */
extern void port_SetRelay(
    const PortNetAddress *server, const char *code, u32 paths);
/* The host's room's name and the player's own (shown in the list). */
extern void port_SetRelayRoom(const char *name, const char *player);
/* The room's password, as typed (the host's salt is made from the clock;
 * a guest's key is made from the salt the server sends), or the key
 * already made of it, SHA-256(salt || password), with the host's salt. */
extern void port_SetRelayPassword(const char *password);
extern void port_SetRelayKey(const u8 *salt, const u8 *key);
extern void port_RelayRoomKey(const u8 *salt, const char *password, u8 *key);
/* --room-key's `SALT:KEY`, in hex: 1, or 0 if it is not one. */
extern s32 port_ParseRoomKey(const char *text, u8 *salt, u8 *key);
/* The port the window's router mapped, the host's first way. */
extern void port_SetRelayMapped(const PortNetAddress *mapped);
extern s32 port_IsRelayOn(void);
/* The room's code: the one joined, or the one the host was given; ""
 * before there is one. */
extern const char *port_GetRelayCode(void);
/* The two programs found by way of the server: each told the other's
 * addresses, a punch sent to each, the way the server decides (IPv6,
 * IPv4, or its relay) taken. The network's peer is then the other
 * program, or the server; 1, or 0 with the reason said
 * (port_SessionFail). `again`: the host lets the guest it had go and
 * waits for another in the same room. */
extern s32 port_RelayMeet(s32 role, s32 again);

/* A room of the lobby's list. */
typedef struct {
    char code[PORT_RELAY_CODE_BYTES];
    char name[PORT_RELAY_NAME_BYTES];
    char player[PORT_RELAY_PLAYER_BYTES];
    char setup[PORT_SETUP_CODE_BYTES];
    s32 players;
    s32 locked;
    u8 salt[PORT_RELAY_SALT_BYTES];
    u32 age; /* seconds */
} PortRoom;

/* The lobby's rooms, at most `most`, by a socket of their own (out of a
 * session): how many were had, and `total` the server's count; -1 if the
 * server did not answer. */
extern s32 port_RelayListRooms(
    const PortNetAddress *server, PortRoom *rooms, s32 most, s32 *total);
/* The address the server sees a socket of this PC's port `port` at (0:
 * any): 1 and `out`, or 0. What the other player types to punch through
 * to this one with no lobby (--punch-to). */
extern s32 port_RelayWhoAmI(
    const PortNetAddress *server, u16 port, PortNetAddress *out);

#endif /* PORT_NET_H */
