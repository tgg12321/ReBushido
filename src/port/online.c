/* A session of netplay begun and ended in the running program
 * (port_online.h; docs/NETPLAY.md, section 15). Outside the state: what is
 * here is the snapshots themselves and what a session was asked to be.
 *
 * A session has to begin as both programs began it before: from power on,
 * with the host's two memory cards in both slots and the host's seed, or
 * the two do not compute the same. The program does not start again for
 * that. Before the game's first step, as the program begins, the state is
 * kept whole (port_state.h): the game's memory, its stack as the fiber
 * was made, every variable of the game, of Sony's and of the port's
 * console. That is what a program started for the session has at the
 * same moment, but the cards: the session's meeting changes nothing of
 * the state, and the host's cards and switches are put in after it, as
 * port_StartSession puts them in before the game starts.
 *
 * So when a session asked for begins, at a boundary of the program's own
 * play: the meeting (the lobby, the way, the checks, the cards); then the
 * player's own state is kept, the state of the start put back, the host's
 * cards, switches and margin taken (port_ApplySession), the draw distance
 * the console's, and netplay runs from there. When it ends (both back to
 * the top menu, the other gone, a desync) everything of it is let go --
 * the socket, the session's and the lobby's words, netplay's inputs --
 * the player's switches and picture are theirs again, and their own state
 * is put back: the game goes on at the boundary it was at, with their
 * cards.
 *
 * A room of several (port_room.h; docs/NETPLAY.md, section 16) asks the
 * same of a member, a match at a time: a session of two met under the
 * match's code with its ticket, ended where the match is decided, its
 * result told to the room; or a match watched, from the start of its
 * latest fight that its host served, put in as the player's own state is
 * kept, and run on with the players' inputs to the match's end. */
#include "common.h"
#include "port_disc.h"
#include "port_host.h"
#include "port_net.h"
#include "port_online.h"
#include "port_ranked.h"
#include "port_paint.h"
#include "port_replay.h"
#include "port_room.h"
#include "bb2.h"

/* How long a spectator waits for a match's fight to begin, and for the
 * room to show the match again once it shows another. */
#define WATCH_WAIT_MS 300000
#define WATCH_GONE_MS 5000
#define WATCH_POLL_MS 2
/* the server's word that there is no such match to watch (watch.c) */
#define WATCH_NO_SUCH_MATCH 2
/* a boundary of a room's program's own play, with nothing to do in the
 * room, waits at least this long */
#define ROOM_IDLE_MS 2
#define WHY_BYTES 400

void (*g_port_online_hook)(s32 event, const char *why);
void (*g_port_online_map_hook)(const char *bind);
void (*g_port_online_unmap_hook)(void);

static struct {
    s32 wanted;
    const PortDisc *disc;
    u8 *boot; /* the state of the game's start */
    u8 *own;  /* the player's own, as a session began */
    s32 asked;
    s32 busy;
    PortOnlineAsk ask;
    /* --session-at: after how many boundaries, how many times, and the
     * boundaries since the start or the last session */
    u32 after;
    u32 times;
    u32 steps;
    u32 sessions; /* begun in this program */
    /* a room's match asked for: played or watched */
    s32 room_asked;
    PortRoomNext next;
    s32 room_match; /* the session being met is a room's */
    s32 ranked;     /* ... or a ranked match (port_ranked.h) */
    u32 ranked_played;
    u32 ranked_decided;
    u32 ranked_last; /* the last match decided, its number */
    /* a program with no pages: its room (--room-open, --room-enter) */
    PortOnlineRoom room;
    s32 room_opened;
    s32 side_joined;
    s32 side_played;
    /* the room's tournament (room_tournament.c): this match is one of
     * its; the command line's begun, and why not yet */
    s32 tournament;
    s32 tournament_begun;
    char tournament_why[WHY_BYTES];
} g_online;

void port_OnlineWanted(void) { g_online.wanted = 1; }

/* The state of the game's start, once, where sessions are wanted
 * (port_online.h). */
void port_OnlineKeepBoot(const PortDisc *disc) {
    g_online.disc = disc;
    if (!g_online.wanted || g_online.boot != NULL) {
        return;
    }
    g_online.boot = port_HostAllocate(port_StateSize());
    g_online.own = port_HostAllocate(port_StateSize());
    if (g_online.boot == NULL || g_online.own == NULL) {
        port_Log("online: no memory for the state of the game's start: a "
                 "session starts the program again instead\n");
        g_online.boot = NULL;
        return;
    }
    port_StateSave(g_online.boot);
}

s32 port_OnlineCanBegin(void) { return g_online.boot != NULL; }

s32 port_OnlineIsBusy(void) {
    return g_online.busy || g_online.asked || g_online.room_asked;
}

s32 port_OnlineIsRoomMatch(void) { return g_online.room_match; }

s32 port_OnlineIsRoomAuto(void) { return g_online.room.on; }

void port_OnlineLeave(void) { port_NetplayAskLeave(); }

/* A session asked for, its words checked as far as they can be before
 * the meeting (port_online.h). */
const char *port_OnlineAskFor(const PortOnlineAsk *ask) {
    PortNetAddress at;

    if (g_online.boot == NULL) {
        return "This program cannot begin a session as it runs.";
    }
    if (g_online.busy) {
        return "A session is under way.";
    }
    if (ask->lobby[0] != 0 &&
        (!port_ParseNetAddress(ask->lobby, &at) || at.port == 0)) {
        return "The lobby server's address is not one (as 203.0.113.7:7845).";
    }
    if (ask->role == PORT_SESSION_HOST &&
        (!port_ParseNetAddress(ask->at, &at) ||
         (at.port == 0 && ask->lobby[0] == 0))) {
        return "The port to wait at is not one.";
    }
    if (ask->role == PORT_SESSION_JOIN && ask->lobby[0] == 0 &&
        (!port_ParseNetAddress(ask->at, &at) || at.port == 0)) {
        return "The host's address is not one (as 192.168.1.20:7845).";
    }
    if (ask->role == PORT_SESSION_JOIN && ask->lobby[0] != 0 &&
        port_TextLength(ask->at) == 0) {
        return "The room's code is missing.";
    }
    g_online.ask = *ask;
    g_online.asked = 1;
    return NULL;
}

void port_OnlineAt(const PortOnlineAsk *ask, u32 after, u32 times) {
    g_online.ask = *ask;
    g_online.after = after;
    g_online.times = times;
    g_online.steps = 0;
}

/* The ask into the network's, the lobby's and the session's own words, as
 * main.c puts the command line's; the socket opened. 0, said in the log
 * and to the window, if it cannot be. */
static s32 port_OnlineSetUp(void) {
    const PortOnlineAsk *ask = &g_online.ask;
    PortNetAddress lobby;
    PortNetAddress at;
    PortNetAddress bind;
    PortNetAddress punch;
    u8 salt[PORT_RELAY_SALT_BYTES];
    u8 key[PORT_RELAY_KEY_BYTES];
    s32 has_lobby =
        ask->lobby[0] != 0 && port_ParseNetAddress(ask->lobby, &lobby);
    u32 paths = ask->paths;

    port_ClearNetAddress(&bind);
    if (has_lobby && ask->role == PORT_SESSION_JOIN) {
        /* the room's code; the server is the peer until the way to the
         * host is found */
        port_SetRelay(&lobby, ask->at, paths);
        at = lobby;
    } else {
        port_ParseNetAddress(ask->at, &at);
        if (has_lobby) {
            /* a room of several's match is opened under its own code */
            port_SetRelay(&lobby, ask->has_ticket ? ask->code : "", paths);
        }
    }
    if (has_lobby && ask->has_ticket) {
        port_SetRelayTicket(ask->ticket);
    }
    if (has_lobby) {
        port_SetRelayRoom(
            ask->role == PORT_SESSION_HOST ? ask->room : "", ask->player);
        if (ask->key[0] != 0 && port_ParseRoomKey(ask->key, salt, key)) {
            port_SetRelayKey(salt, key);
        } else if (ask->password[0] != 0) {
            port_SetRelayPassword(ask->password);
        }
    }
    if (ask->role == PORT_SESSION_HOST) {
        if (ask->punch_to[0] != 0 && !has_lobby &&
            port_ParseNetAddress(ask->punch_to, &punch)) {
            port_SetSessionPunchTo(&punch);
        }
        port_SetNetListen(&at);
    } else {
        if (ask->bind[0] != 0) {
            port_ParseNetAddress(ask->bind, &bind);
        }
        port_SetNetAddresses(&bind, &at);
    }
    port_SetSession(ask->role, &at);
    port_SetSessionPlayer(ask->player);
    /* four players (docs/NETPLAY.md, section 18): the host's choice, and
     * a room's match guest's seat at the lobby */
    port_SetSessionPlayers(
        ask->role == PORT_SESSION_HOST ? ask->players : 2, ask->pcs);
    if (has_lobby && ask->role == PORT_SESSION_JOIN) {
        port_SetRelaySeat(ask->seat);
    }
    /* a session begun here ends where both go back to the top menu, and
     * a room's match where it is decided */
    port_SetSessionRules(
        ask->role != PORT_SESSION_HOST ? 0
        : ask->has_ticket
            ? PORT_SESSION_RULE_TOP_MENU_LEAVES | PORT_SESSION_RULE_ONE_MATCH
            : PORT_SESSION_RULE_TOP_MENU_LEAVES);
    if (ask->role == PORT_SESSION_HOST && ask->portmap &&
        g_port_online_map_hook != NULL) {
        g_port_online_map_hook(ask->at);
    }
    if (!port_StartNet()) {
        port_SessionFail("This PC's network port could not be opened: is "
                         "another program using it?");
        return 0;
    }
    return 1;
}

/* Everything of a session let go: the next one finds nothing of it. */
static void port_OnlineLetGo(void) {
    if (g_port_online_unmap_hook != NULL) {
        g_port_online_unmap_hook();
    }
    port_StopNet();
    port_ResetNetplay();
    port_ResetSession();
    port_ResetRelay();
}

/* Why a session ended (PORT_NETPLAY_END_...), in a player's words. */
static const char *port_OnlineEndWords(s32 end) {
    switch (end) {
    case PORT_NETPLAY_END_PEER_LEFT:
        return "The other player has left the game.";
    case PORT_NETPLAY_END_SILENT:
        return "The other player stopped answering for 15 seconds.";
    case PORT_NETPLAY_END_DESYNC:
        return "The two games fell out of step (a desync) and could not "
               "be resynced: the log has the frame. Please send it to the "
               "project.";
    case PORT_NETPLAY_END_MATCH:
        return "The match is over.";
    case PORT_NETPLAY_END_QUIT:
        return "You left the fight.";
    default:
        return "You both went back to the Main Menu: the session is over.";
    }
}

/* --session-at's count of sessions, one more gone: after the last the
 * program ends, failed if `failed`. */
static void port_OnlineCountDown(s32 failed) {
    if (g_online.times == 0) {
        return;
    }
    g_online.times--;
    g_online.steps = 0;
    if (g_online.times == 0) {
        port_Log("online: the last session asked for has ended; the program "
                 "ends\n");
        port_Quit(failed ? PORT_EXIT_FAILED : PORT_EXIT_OK);
    }
}

/* ---- a room's match ---- */

/* A room's match begins, the state of the game's start put back: its
 * host sends the match's feed for the spectators; a test's leave set. */
static void port_OnlineMatchBegins(void) {
    const PortRoomNext *next = &g_online.next;

    if (g_online.ranked) {
        g_online.ranked_played++;
        port_Log("ranked: match %d (%s vs %s) begins: this program plays "
                 "player %d\n",
                 next->match.number, next->first, next->second,
                 next->role == PORT_SESSION_HOST ? 1 : 2);
        port_RankedMatchBegins();
        if (g_online.room.on && g_online.room.leave_match != 0 &&
            g_online.room.leave_match == g_online.ranked_played) {
            port_SetNetplayLeaveAt(g_online.room.leave_frame);
        }
        return;
    }
    if (next->players == PORT_RELAY_FOUR) {
        port_Log("room: match %d (%s and %s vs %s and %s) begins: this "
                 "program plays player %d, of two on two\n",
                 next->match.number, next->first, next->third, next->second,
                 next->fourth, next->place + 1);
    } else {
        port_Log("room: match %d (%s vs %s) begins: this program plays "
                 "player %d%s\n",
                 next->match.number, next->first, next->second,
                 next->role == PORT_SESSION_HOST ? 1 : 2,
                 next->side ? ", a match beside the room's" : "");
    }
    if (next->role == PORT_SESSION_HOST) {
        port_FeedBegin(&next->match);
    }
    if (g_online.room.on && g_online.room.leave_match != 0 &&
        (s32)g_online.room.leave_match == next->match.number) {
        port_SetNetplayLeaveAt(g_online.room.leave_frame);
    }
}

/* ... and ends: who won, by why it ended, to the feed and the room. A
 * match of four is its teams': the first and third players against the
 * second and fourth; a player who leaves loses it for his team. */
static void port_OnlineMatchEnds(s32 end) {
    const PortRoomNext *next = &g_online.next;
    s32 first = next->players == PORT_RELAY_FOUR
                    ? next->place % 2 == 0
                    : next->role == PORT_SESSION_HOST;
    s32 gone = port_NetplayLeftPad();
    s32 winner = PORT_ROOM_DRAW;

    if (end == PORT_NETPLAY_END_QUIT && next->players == PORT_RELAY_FOUR &&
        next->place != 0 && !g_online.ranked) {
        /* a guest of four who left: the CPU plays on for him, and the
         * match's end, as it is played out, is the others' to tell the
         * room (docs/NETPLAY.md, section 19); the host tells it where
         * the session ended instead */
        port_Log("room: match %d left; its end is the other players' to "
                 "tell\n",
                 next->match.number);
        port_RoomDone(next->match.number);
        g_online.room_match = FALSE;
        return;
    }
    if (end == PORT_NETPLAY_END_MATCH) {
        s32 result = port_SessionResult();

        winner = result == 1   ? PORT_ROOM_WON_FIRST
                 : result == 2 ? PORT_ROOM_WON_SECOND
                               : PORT_ROOM_DRAW;
    } else if (end == PORT_NETPLAY_END_QUIT) {
        /* this player left: the other won */
        winner = first ? PORT_ROOM_WON_SECOND : PORT_ROOM_WON_FIRST;
    } else if (
        end == PORT_NETPLAY_END_PEER_LEFT || end == PORT_NETPLAY_END_SILENT) {
        /* the other team won where it was the other team's player who
         * went; of two, the one who stays */
        if (next->players == PORT_RELAY_FOUR && gone >= 0) {
            winner = gone % 2 == 0 ? PORT_ROOM_WON_SECOND : PORT_ROOM_WON_FIRST;
        } else {
            winner = first ? PORT_ROOM_WON_FIRST : PORT_ROOM_WON_SECOND;
        }
    }
    if (g_online.ranked) {
        port_RankedMatchEnds(end, winner, TRUE);
        g_online.room_match = FALSE;
        return;
    }
    port_FeedEnd(port_NetplayFrameNow(), winner);
    port_FeedFinish();
    port_RoomResult(next->match.number, winner);
    port_RoomDone(next->match.number);
    g_online.room_match = FALSE;
}

/* A session met, played and ended here. */
static void port_OnlineRun(void) {
#if RB_REMASTER_VIEW
    PortRemasterView view;
    PortRemasterView console;
#endif
    s32 end;

    g_online.busy = 1;
    g_online.sessions++;
    port_Log("online: session %u begins in this program (no start "
             "again)\n",
             g_online.sessions);
    if (g_port_online_hook != NULL) {
        g_port_online_hook(PORT_ONLINE_MEETING, NULL);
    }
    if (!port_OnlineSetUp() ||
        !port_MeetSession(g_online.disc, g_online.ask.delay)) {
        s32 canceled = port_SessionIsCanceled();

        port_Log("online: %s; the program goes on with its own game\n",
                 canceled ? "the meeting was given up" : "no session");
        port_OnlineLetGo();
        if (g_online.room_match && g_online.ranked) {
            /* a ranked match the two never met: void, if both say so */
            port_RankedMatchEnds(0, PORT_ROOM_NOBODY, FALSE);
            g_online.room_match = FALSE;
        } else if (g_online.room_match) {
            /* the room goes on: a match not met is nobody's */
            port_RoomResult(g_online.next.match.number, PORT_ROOM_DRAW);
            port_RoomDone(g_online.next.match.number);
            g_online.room_match = FALSE;
        }
        g_online.busy = 0;
        if (g_port_online_hook != NULL) {
            g_port_online_hook(
                PORT_ONLINE_FAILED, canceled ? NULL : "no session");
        }
        port_OnlineCountDown(TRUE);
        return;
    }
    /* the painters done with what the game gave them: the state is read
     * whole here, video memory with it */
    port_PaintSyncAll();
    port_StateSave(g_online.own);
    port_KeepPlaySwitches();
    port_KeepStageDefaults();
    port_StateLoad(g_online.boot);
    port_ApplySession();
#if RB_REMASTER_VIEW
    /* what the game computes may not follow either player's picture
     * (main.c does the same for a session started so) */
    port_RemasterGetView(&view);
    console = view;
    console.distance = PORT_DISTANCE_ORIGINAL;
    port_RemasterSetView(&console);
#endif
    port_SetNetplayInProcess(1);
    port_Log("online: the state of the game's start put back: the session "
             "plays from power on, as a program started for it would\n");
    if (g_online.room_match) {
        port_OnlineMatchBegins();
    }
    if (g_port_online_hook != NULL) {
        g_port_online_hook(PORT_ONLINE_BEGINS, NULL);
    }
    end = port_RunNetplay();
    if (g_online.room_match) {
        port_OnlineMatchEnds(end);
    }
    port_EndNetplay();
    port_Log("online: session %u has ended: %s\n", g_online.sessions,
             port_OnlineEndWords(end));
    port_PaintSyncAll();
    port_OnlineLetGo();
    port_RestorePlaySwitches();
    port_RestoreStageDefaults();
    port_RemasterFreeMargin();
#if RB_REMASTER_VIEW
    port_RemasterSetView(&view);
#endif
    port_StateLoad(g_online.own);
    port_Log("online: the player's own game put back, where it was; the "
             "program goes on\n");
    g_online.busy = 0;
    if (g_port_online_hook != NULL) {
        g_port_online_hook(PORT_ONLINE_ENDS, port_OnlineEndWords(end));
    }
    port_OnlineCountDown(end == PORT_NETPLAY_END_DESYNC);
}

/* `text` into `out`, cut to `room`. */
static void port_OnlineKeepWord(char *out, u32 room, const char *text) {
    u32 i;

    for (i = 0; text != NULL && text[i] != 0 && i + 1 < room; i++) {
        out[i] = text[i];
    }
    out[i] = 0;
}

/* printf's work into `out`. */
static void port_OnlineText(char *out, u32 room, const char *format, ...) {
    PortArgs args;

    PORT_ARGS_START(args, format);
    port_FormatText(out, room, format, args);
    PORT_ARGS_END(args);
}

/* Why a match watched ended, in a player's words. */
static const char *port_OnlineWatchWords(s32 end) {
    switch (end) {
    case PORT_NETPLAY_END_DESYNC:
        return "What you watched fell out of step with the players' game "
               "(a desync), and no resync came: the log has the frame.";
    case PORT_NETPLAY_END_SILENT:
        return "The match's feed stopped coming for 15 seconds.";
    case PORT_NETPLAY_END_QUIT:
        return "You stopped watching.";
    default:
        return "The match you watched is over.";
    }
}

/* A spectator waits for the match's settings and its fight's start: 1
 * once both are had, or 0 with why (the match over first, the player
 * gave up, the wait too long). */
static s32 port_OnlineWatchWaits(char *why, u32 room) {
    const PortRoomNext *next = &g_online.next;
    char status[WHY_BYTES];
    u32 start = port_NetClock();
    const u8 *blob;
    u32 size;

    port_SetSessionPhase(PORT_SESSION_PHASE_WATCHING);
    port_OnlineText(status, sizeof(status),
                    "Watching %s vs %s: waiting for their fight to begin",
                    next->first, next->second);
    for (;;) {
        PortRoomView view;
        u32 end_frame;
        s32 winner;

        port_RoomTick();
        port_WatchTick();
        if (port_WatchSettings() != NULL && port_WatchStart(&blob, &size)) {
            u32 frame;

            if (!port_ReplayServedNeedsBase(blob, size, &frame)) {
                return 1;
            }
            /* a resync's state told as the difference from a state the
             * match's spectators had (a fight of four's): no way in for
             * one who comes now; the next fight's start is (docs/NETPLAY.md,
             * section 21) */
            port_Log("room: match %d's newest start is a resync's, told from "
                     "a state this spectator has not: its next fight's start "
                     "is waited for\n",
                     next->match.number);
            port_WatchAskStart(frame + 1);
        }
        port_RoomGet(&view);
        if (port_SessionIsCanceled()) {
            why[0] = 0;
            return 0;
        }
        if (!view.in_room || port_WatchNothing() >= WATCH_NO_SUCH_MATCH ||
            port_WatchEnd(&end_frame, &winner) ||
            (!next->side && view.match.number != next->match.number &&
             port_NetClock() - start > WATCH_GONE_MS)) {
            port_OnlineText(why, room, "Match %d ended before its fight began.",
                            next->match.number);
            return 0;
        }
        if (port_NetClock() - start > WATCH_WAIT_MS) {
            port_OnlineText(why, room, "Match %d's fight did not begin.",
                            next->match.number);
            return 0;
        }
        port_SessionStatus(status);
        port_RoomWait(WATCH_POLL_MS);
    }
}

/* A room's match watched here, from its latest fight's start, the
 * player's own game kept and put back after. */
static void port_OnlineWatchRun(void) {
#if RB_REMASTER_VIEW
    PortRemasterView view;
    PortRemasterView console;
#endif
    const PortRoomNext *next = &g_online.next;
    char why[WHY_BYTES];
    const u8 *blob;
    u32 size;
    u32 frame;
    s32 end;

    g_online.busy = 1;
    port_Log("room: watching match %d (%s vs %s)\n", next->match.number,
             next->first, next->second);
    port_WatchBegin(&next->match);
    if (g_port_online_hook != NULL) {
        g_port_online_hook(PORT_ONLINE_MEETING, NULL);
    }
    if (!port_OnlineWatchWaits(why, sizeof(why))) {
        port_Log("room: match %d not watched: %s\n", next->match.number,
                 why[0] != 0 ? why : "the player gave it up");
        port_WatchStop();
        port_RoomDone(next->match.number);
        port_ResetSession();
        g_online.busy = 0;
        if (g_port_online_hook != NULL) {
            g_port_online_hook(PORT_ONLINE_FAILED, why[0] != 0 ? why : NULL);
        }
        return;
    }
    port_WatchStart(&blob, &size);
    port_PaintSyncAll();
    port_StateSave(g_online.own);
    port_KeepPlaySwitches();
    port_KeepStageDefaults();
    if (!port_ReplayLoadServed(blob, size, &frame) ||
        !port_WatchSession(port_WatchSettings())) {
        port_Log("room: match %d's start could not be put in: it is not "
                 "watched\n",
                 next->match.number);
        end = PORT_NETPLAY_END_DESYNC;
    } else {
#if RB_REMASTER_VIEW
        port_RemasterGetView(&view);
        console = view;
        console.distance = PORT_DISTANCE_ORIGINAL;
        port_RemasterSetView(&console);
#endif
        port_WatchStarted(frame);
        if (g_port_online_hook != NULL) {
            g_port_online_hook(PORT_ONLINE_BEGINS, NULL);
        }
        end = port_RunWatch(frame);
        port_EndNetplay();
#if RB_REMASTER_VIEW
        port_RemasterSetView(&view);
#endif
    }
    port_Log("room: match %d watched: %s\n", next->match.number,
             port_OnlineWatchWords(end));
    port_PaintSyncAll();
    port_WatchStop();
    port_ResetNetplay();
    port_ResetSession();
    port_RestorePlaySwitches();
    port_RestoreStageDefaults();
    port_RemasterFreeMargin();
    port_StateLoad(g_online.own);
    port_RoomDone(next->match.number);
    g_online.busy = 0;
    if (g_port_online_hook != NULL) {
        g_port_online_hook(PORT_ONLINE_ENDS, port_OnlineWatchWords(end));
    }
}

/* A room's match into a session's ask: met under its code, with its
 * ticket, by way of the room's lobby. */
static void port_OnlineMatchAsk(
    PortOnlineAsk *ask, const PortRoomNext *next, const char *lobby,
    const char *player, const char *bind, u32 delay, u32 paths) {
    u32 seed;
    u32 i;

    for (i = 0; i < sizeof(*ask); i++) {
        ((u8 *)ask)[i] = 0;
    }
    ask->role = next->role;
    ask->delay = delay;
    ask->paths = paths;
    port_OnlineKeepWord(ask->lobby, sizeof(ask->lobby), lobby);
    port_OnlineKeepWord(ask->player, sizeof(ask->player), player);
    port_OnlineKeepWord(ask->code, sizeof(ask->code), next->match.code);
    if (next->role == PORT_SESSION_HOST) {
        port_OnlineKeepWord(
            ask->at, sizeof(ask->at), bind[0] != 0 ? bind : "0.0.0.0:0");
    } else {
        port_OnlineKeepWord(ask->at, sizeof(ask->at), next->match.code);
        port_OnlineKeepWord(ask->bind, sizeof(ask->bind), bind);
    }
    ask->has_ticket = TRUE;
    for (i = 0; i < PORT_RELAY_TICKET_BYTES; i++) {
        ask->ticket[i] = next->match.ticket[i];
    }
    ask->room_number = next->match.number;
    if (next->players == PORT_RELAY_FOUR) {
        /* a match of four: four PCs, each guest at the seat of its place */
        ask->players = PORT_RELAY_FOUR;
        ask->pcs = PORT_RELAY_FOUR;
        ask->seat = next->place;
    }
    if (port_GetSessionSeedAsked(&seed)) {
        /* a test's seed, another for each match, that each be its own */
        port_SetSessionSeed(seed + (u32)next->match.number);
    }
}

/* A room's match asked for by the pages (port_online.h). */
const char *port_OnlineAskRoom(
    const PortRoomNext *next, const PortOnlineAsk *how) {
    if (g_online.boot == NULL) {
        return "This program cannot begin a session as it runs.";
    }
    if (g_online.busy || g_online.asked || g_online.room_asked) {
        return "A session is under way.";
    }
    /* the lobby, the player's name, the delay: the match's ask is made of
     * them (port_OnlineRoomRun) */
    g_online.ask = *how;
    g_online.next = *next;
    g_online.room_asked = TRUE;
    return NULL;
}

/* A ranked match asked for by the pages (port_online.h). */
const char *port_OnlineAskRanked(
    const PortRoomNext *next, const PortOnlineAsk *how) {
    const char *problem = port_OnlineAskRoom(next, how);

    if (problem == NULL) {
        g_online.ranked = TRUE;
    }
    return problem;
}

void port_OnlineRoomAuto(const PortOnlineRoom *room) {
    g_online.room = *room;
    g_online.room.on = TRUE;
}

/* The ranked ladder, a program with no pages (--ranked): queued at its
 * first boundary, its matches played as the lobby gives them, queued again
 * after each is decided, and the program ended once `matches` are, with
 * the ladder's lists printed first where it was asked (--ranked-lists). */
static void port_OnlineRankedTurn(void) {
    PortOnlineRoom *room = &g_online.room;
    PortRankedView view;
    PortRoomNext next;
    char why[WHY_BYTES];

    if (!g_online.room_opened) {
        if (room->lie) {
            port_RankedLie();
        }
        if (!port_RankedStart(room->lobby, room->player, why, sizeof(why))) {
            port_SessionFail(why);
            port_Quit(PORT_EXIT_FAILED);
        }
        g_online.room_opened = TRUE;
        port_RankedQueue();
    }
    port_RankedTick();
    port_RankedGet(&view);
    if (view.phase == PORT_RANKED_IDLE && view.why[0] != 0) {
        port_SessionFail(view.why);
        port_Quit(PORT_EXIT_FAILED);
    }
    if (port_RankedNext(&next)) {
        g_online.next = next;
        g_online.room_asked = TRUE;
        g_online.ranked = TRUE;
        return;
    }
    if (view.phase == PORT_RANKED_DECIDED && !view.uploading &&
        view.match != g_online.ranked_last) {
        g_online.ranked_last = view.match;
        g_online.ranked_decided++;
        if (room->matches != 0 && g_online.ranked_decided >= room->matches) {
            if (room->lists) {
                port_RankedPrintLists();
            }
            port_Log("ranked: %u matches decided; the program ends\n",
                     g_online.ranked_decided);
            port_RankedStop();
            port_Quit(PORT_EXIT_OK);
        }
        port_RankedQueue();
    }
    /* nothing to do: the program's own game, which nothing paces with no
     * window, is let run no faster than this */
    port_RankedWait(ROOM_IDLE_MS);
}

/* The program's room, a program with no pages (--room-open, --room-enter):
 * opened or entered at its first boundary, a match beside it asked for,
 * its matches played and watched as they come, and the program ended once
 * the room has had the results it was to wait for. */
static void port_OnlineRoomTurn(void) {
    PortOnlineRoom *room = &g_online.room;
    PortRoomView view;
    PortRoomNext next;
    char why[WHY_BYTES];
    s32 i;

    if (!room->on) {
        port_RoomTick();
        return;
    }
    if (room->ranked) {
        port_OnlineRankedTurn();
        return;
    }
    if (!g_online.room_opened) {
        s32 ok =
            room->name[0] != 0
                ? port_RoomOpen(
                      room->lobby, room->name, room->player, room->password,
                      room->rule, room->plays, room->size, why, sizeof(why))
                : port_RoomEnter(room->lobby, room->code, room->player,
                                 room->password, room->plays, why, sizeof(why));

        if (!ok) {
            port_SessionFail(why);
            port_Quit(PORT_EXIT_FAILED);
        }
        g_online.room_opened = TRUE;
        port_RoomSetWatching(room->watch);
        if (room->side == 1) {
            port_RoomSideOpen();
        }
    }
    port_RoomTick();
    port_RoomGet(&view);
    if (!view.in_room) {
        return;
    }
    if (room->side == 2 && !g_online.side_joined) {
        for (i = 0; i < view.side_count; i++) {
            if (view.sides[i].b == PORT_ROOM_NOBODY &&
                view.sides[i].a != view.you) {
                port_Log("room: joining %s's match beside the room's\n",
                         port_RoomPlayer(view.sides[i].a));
                port_RoomSideJoin(i);
                g_online.side_joined = TRUE;
                break;
            }
        }
    }
    if (room->tournament[0] != 0 && !g_online.tournament_begun) {
        PortTournamentSetup setup;

        if (!port_RoomTournamentParse(room->tournament, &setup)) {
            port_SessionFail("--room-tournament: its words are not a "
                             "tournament");
            port_Quit(PORT_EXIT_FAILED);
        }
        if (port_RoomTournamentBegin(&setup, why, sizeof(why))) {
            g_online.tournament_begun = TRUE;
        } else if (!port_TextsEqual(why, g_online.tournament_why)) {
            port_OnlineKeepWord(
                g_online.tournament_why, sizeof(g_online.tournament_why), why);
            port_Log("room: the tournament waits: %s\n", why);
        }
    }
    if (room->until_champion) {
        PortRoomTournamentView tournament;

        port_RoomTournamentGet(&tournament);
        if (tournament.on && tournament.finished) {
            port_Log("room: the room's tournament is over; this program "
                     "leaves and ends\n");
            port_RoomLeave();
            port_Quit(PORT_EXIT_OK);
        }
    }
    if ((room->matches != 0 && view.last_number >= (s32)room->matches &&
         (view.match.number == 0 ||
          (view.match.a != view.you && view.match.b != view.you) ||
          view.match.number > (s32)room->matches)) ||
        (room->side != 0 && g_online.side_played)) {
        port_Log("room: the room has had what this program waited for (%d "
                 "results); it leaves and ends\n",
                 view.last_number);
        port_RoomLeave();
        port_Quit(PORT_EXIT_OK);
    }
    if (port_RoomNext(&next) != PORT_ROOM_NEXT_NONE &&
        (room->matches == 0 || next.match.number <= (s32)room->matches ||
         next.side)) {
        if (next.side) {
            g_online.side_played = TRUE;
        }
        g_online.next = next;
        g_online.room_asked = TRUE;
        return;
    }
    /* nothing to do in the room: the program's own game, which nothing
     * paces with no window, is let run no faster than this */
    port_RoomWait(ROOM_IDLE_MS);
}

/* The room's match asked for, played or watched here. */
static void port_OnlineRoomRun(void) {
    PortOnlineAsk ask;
    PortTournamentForm form;
    const PortOnlineRoom *room = &g_online.room;

    g_online.room_asked = FALSE;
    if (g_online.next.what == PORT_ROOM_NEXT_WATCH && !g_online.ranked) {
        port_OnlineWatchRun();
        return;
    }
    port_OnlineMatchAsk(
        &ask, &g_online.next, room->on ? room->lobby : g_online.ask.lobby,
        room->on ? room->player : g_online.ask.player,
        room->on ? room->bind : g_online.ask.bind,
        room->on ? room->delay : g_online.ask.delay,
        room->on ? room->paths : g_online.ask.paths);
    g_online.ask = ask;
    g_online.room_match = TRUE;
    if (g_online.ranked) {
        /* the ladder's rule held by the host's welcome, and the player's
         * own put back after */
        port_KeepMatch();
        port_HoldMatch(port_RankedRule());
    }
    g_online.tournament =
        !g_online.ranked && port_RoomTournamentMatch(&g_online.next, &form);
    if (g_online.tournament) {
        /* a room's tournament's match: its rule held, and its two entrants
         * in the host's welcome (port_tournament.h) */
        port_KeepMatch();
        port_HoldMatch(port_RoomTournamentRule());
        port_SetSessionTournament(
            g_online.next.role == PORT_SESSION_HOST ? &form : NULL);
    }
    port_OnlineRun();
    if (g_online.ranked) {
        port_RestoreMatch();
        g_online.ranked = FALSE;
    }
    if (g_online.tournament) {
        port_RestoreMatch();
        g_online.tournament = FALSE;
    }
    if (room->on && room->quit_left && room->leave_match != 0 &&
        (s32)room->leave_match == g_online.next.match.number) {
        port_Log("room: this program left its match: it leaves the room "
                 "and ends\n");
        port_RoomLeave();
        port_Quit(PORT_EXIT_OK);
    }
}

/* A boundary of the program's own play (port_online.h): --session-at's
 * count, and a session asked for begun. */
void port_OnlineBoundary(void) {
    if (g_online.boot == NULL) {
        return;
    }
    if (!g_online.room_asked && !g_online.asked) {
        port_OnlineRoomTurn();
    }
    if (g_online.room_asked) {
        port_OnlineRoomRun();
        return;
    }
    if (g_online.times != 0 && !g_online.asked) {
        g_online.steps++;
        if (g_online.steps >= g_online.after) {
            g_online.asked = 1;
        }
    }
    if (!g_online.asked) {
        return;
    }
    g_online.asked = 0;
    port_OnlineRun();
}
