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
 * cards. */
#include "common.h"
#include "port_disc.h"
#include "port_host.h"
#include "port_net.h"
#include "port_online.h"
#include "port_paint.h"
#include "bb2.h"

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

s32 port_OnlineIsBusy(void) { return g_online.busy || g_online.asked; }

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
            port_SetRelay(&lobby, "", paths);
        }
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
    /* a session begun here ends where both go back to the top menu */
    port_SetSessionRules(
        ask->role == PORT_SESSION_HOST ? PORT_SESSION_RULE_TOP_MENU_LEAVES : 0);
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
        return "The two games parted (a desync): the log has the frame. "
               "Please send it to the project.";
    default:
        return "You both went back to the top menu: the session is over.";
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
    if (g_port_online_hook != NULL) {
        g_port_online_hook(PORT_ONLINE_BEGINS, NULL);
    }
    end = port_RunNetplay();
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

/* A boundary of the program's own play (port_online.h): --session-at's
 * count, and a session asked for begun. */
void port_OnlineBoundary(void) {
    if (g_online.boot == NULL) {
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
