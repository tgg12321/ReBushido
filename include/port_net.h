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
/* The socket closed and the peer forgotten, as a session in the running
 * program ends (online.c): the next one sets its addresses again. */
extern void port_StopNet(void);
/* The socket is of IPv6 (and IPv4 with it): a bind of 0.0.0.0 is one
 * where the system has IPv6. Where it is bound, its port the system's
 * where it chose: 1, or 0. */
extern s32 port_NetIsV6(void);
extern s32 port_GetNetBound(PortNetAddress *at);
/* Links: a socket and the one program it talks to. A guest of a session
 * has one, to its host; the host of a session of four players on four PCs
 * one for each guest, each its own socket (docs/NETPLAY.md, section 18).
 * Every call above and below is of the link chosen, the first unless
 * another is; port_StopNet closes every link and chooses the first. */
#define PORT_NET_LINKS 3
extern void port_NetSelectLink(s32 link);
extern s32 port_NetLink(void);
/* The link chosen talks to its one program alone (port_FixNetPeer). */
extern s32 port_NetIsFixed(void);
/* A program's pads of a session, a bit each, in words for the log: `1`,
 * `1 & 3`. */
extern const char *port_NetPadsText(u32 mask);
/* A datagram for a link that is fixed to another program: given here
 * (the host's session answers a second guest at its first port), or
 * dropped where NULL. */
extern void (*g_port_net_stranger_hook)(
    const PortNetAddress *from, const u8 *bytes, s32 count);
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
/* A session's pads (2 or 4) and where each pad's inputs come from: -1
 * this program (its player or players), or the link they come in on
 * (net.c). The host of four players on four PCs has a link for each
 * guest and passes each one's inputs on to the others (docs/NETPLAY.md,
 * section 18); a guest has one link, to the host. port_SetNetplay is two
 * pads, one link. */
extern void port_SetNetplayPads(s32 pads, const s32 *sources);
extern s32 port_NetplayPads(void);
/* The pad whose player left the session (another program's goodbye says
 * it), or -1. */
extern s32 port_NetplayLeftPad(void);
/* The pads whose players have left a session of four and whose fighters
 * the CPU plays (a bit each; docs/NETPLAY.md, section 19). */
extern u32 port_NetplayCpuPads(void);
extern s32 port_IsNetplayOn(void);
/* Where the other program has gone, this one plays on alone instead of
 * ending (the window's: a player is not thrown out of the game). */
extern void port_SetNetplayGoesOnAlone(s32 on);
/* The session: the game a frame at a time, for ever (src/port/main.c);
 * one begun in the running program (port_SetNetplayInProcess, online.c)
 * until it ends, with why. */
extern s32 port_RunNetplay(void);

enum {
    PORT_NETPLAY_END_PEER_LEFT = 1, /* the other program has gone */
    PORT_NETPLAY_END_SILENT = 2,    /* nothing heard of it for 15 seconds */
    PORT_NETPLAY_END_DESYNC = 3,    /* the two states parted */
    PORT_NETPLAY_END_LEFT = 4,      /* both went back to the top menu */
    PORT_NETPLAY_END_MATCH = 5,     /* a room's match is decided */
    PORT_NETPLAY_END_QUIT = 6,      /* this player left the fight */
};

extern void port_SetNetplayInProcess(s32 on);
/* The player leaves the session where it is, a fight's middle too (the
 * window's keys, port_OnlineLeave): it ends at the next frame, the other
 * told. For tests (--leave-at): at the session's frame `frame`. */
extern void port_NetplayLeave(void);
extern void port_NetplayAskLeave(void);
extern void port_SetNetplayLeaveAt(u32 frame);
/* A resync (docs/NETPLAY.md, section 21). Where a hash of another's is not
 * this program's own, the session goes on, parted, until the host puts
 * its state on every machine: at a press of any player's (F5,
 * port_NetplayAskResync, heard only while parted), or by itself `frames`
 * frames after the parting (180; 0: only at a press; --resync-auto).
 * For tests: --resync-ask-at asks at this program's frame `frame` once
 * it is parted, and --net-part-at makes this program's state another's at
 * its frame `frame` (a word of the RAM the game never reads). */
extern void port_NetplayAskResync(void);
extern void port_SetNetplayResyncAuto(u32 frames);
extern void port_SetNetplayResyncAskAt(u32 frame);
extern void port_SetNetplayPartAt(u32 frame);
/* For tests (--net-plan-pads): the local pad is only the PC's pad of this
 * program's place in the session (a plan presses both pads, each program
 * its own); a player plays with either. */
extern void port_SetNetplayOwnPadOnly(s32 on);
/* The frame whose input the game takes next: netplay's turn (replay.c). */
extern u32 port_NetplayFrameNow(void);
/* A spectator of a room's match (docs/NETPLAY.md, section 16): the state
 * put in is the session's at `frame` (port_ReplayLoadServed); the match's
 * inputs, as they come (watch.c), are run from there, no rollback of its
 * own, every hash held to its players', until the match ends: why
 * (PORT_NETPLAY_END_...). */
extern s32 port_RunWatch(u32 frame);
/* A session over: everything of it forgotten but the states' buffers. */
extern void port_ResetNetplay(void);
/* The run ends: the other is told, and the report (port_Quit). */
extern void port_EndNetplay(void);

/* What netplay is doing, for the window's line (sdl_net.c). */
enum {
    PORT_NETPLAY_PLAYING = 0,
    PORT_NETPLAY_WAITING = 1,   /* for the other's inputs */
    PORT_NETPLAY_LOADING = 2,   /* at a load, which both wait at */
    PORT_NETPLAY_ALONE = 3,     /* the other has gone: this one plays on */
    PORT_NETPLAY_RESYNCING = 4, /* the host's state goes to every machine */
};

typedef struct {
    s32 is_on;
    s32 pad;          /* 0 to 3: the first this program plays */
    s32 pads;         /* the session's, 2 or 4 */
    u32 local_mask;   /* this program's pads, a bit each */
    s32 state;        /* PORT_NETPLAY_... */
    u32 delay;        /* frames */
    u32 ping_ms;      /* the round trip, smoothed */
    u32 rollback;     /* the most frames run again in one rollback lately */
    u32 rollbacks;    /* in all */
    s32 ahead;        /* frames this program is ahead, in sixteenths */
    u32 frame;        /* the game's frame (steps since the session began) */
    u32 waited_ms;    /* how long the present wait has been */
    u32 alone_ms;     /* how long ago the other went, if it has */
    u32 held_ms;      /* how long this program has held back, in all */
    s32 is_session;   /* set up by --host or --join */
    s32 watching;     /* a spectator of a room's match */
    u32 behind;       /* a spectator: the frames it has and has not run */
    u32 leave_ms;     /* a leave asked: how long ago (0: none) */
    s32 cpu_pad;      /* the last pad the CPU took over, or -1 */
    u32 cpu_ms;       /* how long ago it did */
    s32 cpu_on_leave; /* this player's leave gives his fighter to the CPU */
    /* a resync (docs/NETPLAY.md, section 21): the machines' games found
     * parted, and the frames until the host resyncs by itself (0: only at
     * a press, or asked already); the resyncs had, and how long ago the
     * last (0: none) */
    s32 parted;
    u32 resync_percent; /* resyncing: the share of the state had */
    u32 resync_in;
    s32 resync_asked;
    u32 resyncs;
    u32 resynced_ms;
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

#define PORT_SETUP_CODE_BYTES 24

enum {
    PORT_SESSION_NONE = 0,
    PORT_SESSION_HOST = 1,  /* waits for a guest at --host ADDRESS:PORT */
    PORT_SESSION_JOIN = 2,  /* goes to the host at --join ADDRESS:PORT */
    PORT_SESSION_WATCH = 3, /* a spectator of a room's match */
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
 * 1 netplay is on, the host the first pad and the guest the second. It
 * is port_MeetSession and then port_ApplySession; a session begun in the
 * running program (online.c) puts the state of the game's start back
 * between the two. */
extern s32 port_StartSession(const struct PortDisc *disc, u32 delay);
extern s32 port_MeetSession(const struct PortDisc *disc, u32 delay);
extern void port_ApplySession(void);
/* A session over, in the running program: who the other was, the way,
 * the cards as sent and the lobby's words forgotten; the sums, the
 * player's name and --session-differ kept. */
extern void port_ResetSession(void);
/* The player gave up the meeting (the online pages' Back): the meeting's
 * loops, the lobby's among them, end at their next turn with 0. */
extern void port_CancelSession(void);
extern s32 port_SessionIsCanceled(void);
/* The name the other player is shown in the session's lobby. */
extern void port_SetSessionPlayer(const char *name);
/* For tests (--net-seed): the host's seed, not one of its clock's. */
extern void port_SetSessionSeed(u32 seed);
/* The host's rules for the session, sent in its welcome:
 * PORT_SESSION_RULE_... A session begun from the online pages ends where
 * both players go back to the top menu, rather than going on to versus
 * again. */
#define PORT_SESSION_RULE_TOP_MENU_LEAVES 1U
/* A room of several's match (protocol 10): the session ends where the
 * match is decided, the game's menu after its last fight, who won kept
 * in the state (port_SessionResult). */
#define PORT_SESSION_RULE_ONE_MATCH 2U
extern void port_SetSessionRules(u32 rules);
/* The host's --players: pads 2 or 4, on pcs PCs (4 with 4: a PC each,
 * the host meeting three guests and passing each one's inputs to the
 * others; 4 with 2: two players on each of two PCs, each PC a team;
 * docs/NETPLAY.md, section 18). A guest takes them from the welcome. */
extern void port_SetSessionPlayers(s32 pads, s32 pcs);
extern s32 port_SessionPcs(void);
extern s32 port_SessionPads(void);
/* While the host meets one guest (relay.c's loop too): the others that
 * are in kept in, told of the lobby. */
extern void port_SessionKeepOthers(void);
/* Where the game went back to its top menu in a session whose rule says
 * that ends it, or its match was decided: the frame and one, or 0 (the
 * state's, top_menu.c). */
extern u32 port_SessionLeftAt(void);
/* Who won a room's match: 1 or 2 (the first or second pad),
 * PORT_SESSION_RESULT_DRAW, or 0 while it goes on. */
#define PORT_SESSION_RESULT_DRAW 3
extern s32 port_SessionResult(void);
/* --net-seed's seed, if one was given: 1 and it, or 0. */
extern s32 port_GetSessionSeedAsked(u32 *seed);
/* What a room's spectator holds as the players do (the host's switches,
 * rule, the margin): the match's host's, into 64 bytes; and a spectator's
 * taking of them, 0 if they are of another protocol. */
#define PORT_SESSION_SETTINGS_BYTES 64
extern void port_SessionSettings(u8 *out);
extern s32 port_WatchSession(const u8 *in);

/* The session's lobby, before the game (the pre-fight lobby of the
 * online pages): whether this player is ready, from the hook; NULL is
 * ready at once, as a program with no pages is. */
enum {
    PORT_SESSION_LEAVE = -1,
    PORT_SESSION_NOT_YET = 0,
    PORT_SESSION_READY = 1,
};

extern s32 (*g_port_session_ready_hook)(void);

/* How far the meeting has come, for the pages. */
enum {
    PORT_SESSION_PHASE_NONE = 0,
    PORT_SESSION_PHASE_LOBBY_SERVER = 1, /* reaching the lobby server */
    PORT_SESSION_PHASE_WAITING = 2,      /* the host, for a player */
    PORT_SESSION_PHASE_JOINING = 3,      /* the guest, for the host */
    PORT_SESSION_PHASE_CARDS = 4,        /* the cards on their way */
    PORT_SESSION_PHASE_READY = 5,        /* both here: the pre-fight lobby */
    PORT_SESSION_PHASE_WATCHING = 6,     /* a spectator, for the start */
};

#define PORT_SESSION_NAME_BYTES 24

typedef struct {
    s32 role;  /* PORT_SESSION_... */
    s32 phase; /* PORT_SESSION_PHASE_... */
    s32 percent;
    s32 peer_ready; /* the other's (the host's), or every guest's */
    s32 players;    /* the session's: 2 or 4 */
    s32 here;       /* of them, here with the cards (the host's lobby) */
    char name[PORT_SESSION_NAME_BYTES];
    char peer_name[PORT_SESSION_NAME_BYTES];
    char setup[PORT_SETUP_CODE_BYTES];
    const char *code;   /* the room's at the lobby, or "" */
    const char *path;   /* the way between the two, or "" */
    const char *mapped; /* what the router said, or "" */
} PortSessionLobby;

extern void port_GetSessionLobby(PortSessionLobby *out);
extern void port_SetSessionPhase(s32 phase);
/* At frame `frame`, before the game takes `input`: from power on to the
 * versus select screen the same presses on both machines (the menus are
 * no player's), and in a fight no Start (the pause menu is off). `used`
 * is given the other pad's buttons that the frame's input still depends
 * on: a guess of the others is never wrong. */
extern void port_SessionInput(u32 frame, struct PortInput *input, u16 *used);
/* The agreed random seed, into the game, at the session's first frame. */
extern void port_SessionSeed(void);

/* What the game reads of the session it is played in, and a replay's
 * playback from power on of a fight played online holds as it was
 * (docs/PORT.md, "Across builds: the lead-in"): the pads of the session's
 * input (2 or 4; 0 outside a session), with port_match.h's
 * port_SessionIsOn and port_SessionIsRoomMatch. port_SessionGameNow: the
 * rules, pads and seed of the session being played (a player's, not a
 * spectator's), FALSE if none; port_HoldSessionGame holds them for the
 * playback, port_SessionGameSeed puts the held seed into the game as the
 * session's first frame does, port_ReleaseSessionGame lets go. */
extern s32 port_SessionGamePads(void);
extern s32 port_SessionGameNow(u32 *rules, u32 *pads, u32 *seed);
extern void port_HoldSessionGame(u32 rules, u32 pads, u32 seed);
extern void port_SessionGameSeed(void);
extern void port_ReleaseSessionGame(void);

/* What two players compare to know they can meet (--check-online, the
 * Play online page): the protocol, and short forms of the build's, the
 * disc's and the gameplay mods' sums, `P7-BBBB-DDDD-MMMM`, into `out`
 * (PORT_SETUP_CODE_BYTES). The disc is the one port_SetSessionDisc
 * named, and the mods are the ones started. */
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
#define PORT_RELAY_TICKET_BYTES 16

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
/* HMAC-SHA256 of `message` with a key of PORT_RELAY_KEY_BYTES: a guest's
 * proof of a room's password. */
extern void port_RelayHmac(
    const u8 *key, const u8 *message, u32 count, u8 *out);
/* A room of several's match (docs/NETPLAY.md, section 16): met under its
 * code (port_SetRelay's), let in by the ticket the room gave its two
 * players, with no password. */
extern void port_SetRelayTicket(const u8 *ticket);
/* A room of four (lobby version 4; docs/NETPLAY.md, section 18): the
 * host's links each host a seat of it, a guest each; a guest may ask for
 * a seat (a room's match's: its place in the match, 1 to 3), 0 any. */
#define PORT_RELAY_FOUR 4
extern void port_SetRelaySeat(s32 seat);
/* A socket of its own for the server at `server`, out of a session: on
 * this PC alone where the server is. The host's, or -1. */
extern s32 port_RelayOpenSocket(const PortNetAddress *server);
/* --room-key's `SALT:KEY`, in hex: 1, or 0 if it is not one. */
extern s32 port_ParseRoomKey(const char *text, u8 *salt, u8 *key);
/* The port the window's router mapped, the host's first way. */
extern void port_SetRelayMapped(const PortNetAddress *mapped);
/* A session over, in the running program: the lobby, the room and its
 * key forgotten (the next session says them again). */
extern void port_ResetRelay(void);
extern s32 port_IsRelayOn(void);
/* The way the meeting found is the lobby's relay, which passes no more
 * than so many datagrams a second on (a resync's state is sent slower). */
extern s32 port_RelayIsRelayed(void);
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
    s32 players; /* of a room of several, its members */
    s32 locked;
    s32 group; /* a room of several (section 16) */
    s32 seats; /* its players at most: 2, or a room of four's 4 */
    u32 ping;  /* ms: this PC's round trip to the server and its host's */
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
