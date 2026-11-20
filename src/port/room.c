/* A room of several at the lobby (port_room.h; docs/NETPLAY.md, section
 * 16): the member's side of it. Outside the state, like the lobby's
 * client (relay.c): nothing here is the game's.
 *
 * A member opens a room or enters one by its code (a password proved as
 * a two-player room's is: the salt and a challenge from the server, an
 * HMAC back), and from then on says every second that it is still there.
 * Each word to the server is answered with the room as the server has it:
 * its members, the rotation's match now and the matches beside it, each
 * match's ticket to its two players alone, and the last result. What the
 * member asks (the rotation, a match beside, a match's result) is said
 * again with each word until the room shows it taken. Its own socket,
 * apart from a session's, carries its matches' feeds and what it watches
 * too (watch.c). */
#include "common.h"
#include "port_host.h"
#include "port_net.h"
#include "port_room.h"
#include "bb2.h"

#define ROOM_HELLO_BYTES 160
#define ROSTER_BYTES 472
#define TOKEN_BYTES 16
#define CODE_FIELD 8
#define NAME_FIELD 24
#define PLAYER_FIELD 16
#define SETUP_FIELD 20
#define SALT_FIELD 16
#define SECRET_FIELD 32
#define MEMBER_BYTES 24
#define SIDE_BYTES 32
#define WORD_BYTES 4
#define HALF_BYTES 2
#define BITS_PER_BYTE 8
/* how often a member says it is there, how often an open or an enter is
 * said again until answered, and for how long */
#define STAY_EVERY_MS 1000
#define ASK_EVERY_MS 250
#define ASK_GIVE_UP_MS 6000
#define POLL_MS 5
/* the matches done here, the newest of them */
#define DONE_KEPT 16
#define WHY_BYTES 400
/* the salt of a room's password: a linear congruential step (Numerical
 * Recipes'), from the clock */
#define SALT_MULTIPLY 1664525U
#define SALT_ADD 1013904223U
#define SALT_BYTE_SHIFT 24
#define SALT_SEED 0x7F4A7C15U
/* a round trip smoothed over about this many */
#define RTT_SMOOTH 4

enum {
    KIND_ROOM = PORT_NET_KIND_RELAY + 9,
    KIND_ROSTER,
    KIND_FEED,
    KIND_FEED_ACK,
    KIND_WATCH,
    KIND_SCENE
};

/* ROOM's actions (its detail). */
enum {
    ACTION_OPEN = 1,
    ACTION_ENTER,
    ACTION_STAY,
    ACTION_LEAVE,
    ACTION_RULE,
    ACTION_RESULT,
    ACTION_SIDE_OPEN,
    ACTION_SIDE_JOIN,
    ACTION_SIDE_DONE
};

/* ROSTER's status (its detail): the lobby's STATE's errors (relay.c). */
enum {
    STATUS_OK = 1,
    STATUS_PASSWORD = 2,
    STATUS_NO_ROOM = 0x81,
    STATUS_ROOM_FULL = 0x82,
    STATUS_SERVER_FULL = 0x83,
    STATUS_VERSION = 0x84,
    STATUS_WRONG_PASSWORD = 0x85,
    STATUS_RATE_LIMITED = 0x86,
    STATUS_INCOMPATIBLE = 0x88,
};

#define RELAY_VERSION 2
#define ROOM_FLAG_PLAYS 0x01

/* ROOM's fields. */
enum {
    HELLO_CODE = PORT_NET_HEADER_BYTES,
    HELLO_NAME = HELLO_CODE + CODE_FIELD,
    HELLO_PLAYER = HELLO_NAME + NAME_FIELD,
    HELLO_SETUP = HELLO_PLAYER + PLAYER_FIELD,
    HELLO_SALT = HELLO_SETUP + SETUP_FIELD,
    HELLO_SECRET = HELLO_SALT + SALT_FIELD,
    HELLO_TOKEN = HELLO_SECRET + SECRET_FIELD,
    HELLO_FLAGS = HELLO_TOKEN + TOKEN_BYTES,
    HELLO_RULE = HELLO_FLAGS + 1,
    HELLO_ARG = HELLO_RULE + 1,
    HELLO_STAMP = HELLO_ARG + 2,
    HELLO_RTT = HELLO_STAMP + WORD_BYTES,
    HELLO_NUMBER = HELLO_RTT + WORD_BYTES,
};

/* ROSTER's fields. */
enum {
    ROSTER_CODE = PORT_NET_HEADER_BYTES,
    ROSTER_NAME = ROSTER_CODE + CODE_FIELD,
    ROSTER_TOKEN = ROSTER_NAME + NAME_FIELD,
    ROSTER_SALT = ROSTER_TOKEN + TOKEN_BYTES,
    ROSTER_CHALLENGE = ROSTER_SALT + SALT_FIELD,
    ROSTER_YOU = ROSTER_CHALLENGE + SALT_FIELD,
    ROSTER_HOW_MANY,
    ROSTER_RULE,
    ROSTER_HOST,
    ROSTER_SETUP,
    ROSTER_MATCH = ROSTER_SETUP + SETUP_FIELD,
    ROSTER_MATCH_CODE = ROSTER_MATCH + 2 * WORD_BYTES,
    ROSTER_MATCH_TICKET = ROSTER_MATCH_CODE + CODE_FIELD,
    ROSTER_LAST = ROSTER_MATCH_TICKET + PORT_RELAY_TICKET_BYTES,
    ROSTER_MEMBERS = ROSTER_LAST + 2 * WORD_BYTES,
    ROSTER_SIDES = ROSTER_MEMBERS + PORT_ROOM_MEMBERS * MEMBER_BYTES,
};

static struct {
    s32 is_on;
    s32 socket;
    PortNetAddress server;
    char code[PORT_ROOM_CODE_BYTES];
    u8 token[TOKEN_BYTES];
    char player[PORT_RELAY_PLAYER_BYTES];
    s32 plays;
    s32 watching;
    u32 stay_at;
    u32 heard_at;
    u32 rtt;
    PortRoomView view;
    s32 done[DONE_KEPT];
    s32 done_count;
    /* what is asked, said again until the room shows it */
    s32 rule_asked; /* -1: none */
    s32 side_open;
    s32 side_join; /* the side's place, or -1 */
    s32 result_number;
    s32 result_winner;
} g_room = {.socket = -1, .rule_asked = -1, .side_join = -1};

/* ---- the datagrams ---- */

static void port_RoomPutText(u8 *out, u32 room, const char *text) {
    u32 i;

    for (i = 0; i < room; i++) {
        out[i] = 0;
    }
    for (i = 0; text != NULL && text[i] != 0 && i < room; i++) {
        out[i] = (u8)(text[i] >= ' ' && text[i] <= '~' ? text[i] : '?');
    }
}

static void port_RoomGetText(char *out, const u8 *in, u32 room) {
    u32 i;

    for (i = 0; i < room && in[i] != 0; i++) {
        out[i] = (char)(in[i] >= ' ' && in[i] <= '~' ? in[i] : '?');
    }
    out[i] = 0;
}

/* A ROOM: `action`, with this member's words; `secret` a password's
 * verifier (an open) or proof (an enter), or NULL. */
static void port_RoomSay(s32 action, s32 arg, u32 number, const char *name,
                         const u8 *salt, const u8 *secret) {
    u8 out[ROOM_HELLO_BYTES];
    char setup[PORT_SETUP_CODE_BYTES];
    u32 now = port_NetClock();
    u32 i;

    if (g_room.socket < 0) {
        return;
    }
    for (i = 0; i < sizeof(out); i++) {
        out[i] = 0;
    }
    port_NetHeader(out, KIND_ROOM, action, RELAY_VERSION);
    port_RoomPutText(out + HELLO_CODE, CODE_FIELD, g_room.code);
    port_RoomPutText(out + HELLO_NAME, NAME_FIELD, name);
    port_RoomPutText(out + HELLO_PLAYER, PLAYER_FIELD, g_room.player);
    port_SessionSetupCode(setup);
    port_RoomPutText(out + HELLO_SETUP, SETUP_FIELD, setup);
    for (i = 0; salt != NULL && i < SALT_FIELD; i++) {
        out[HELLO_SALT + i] = salt[i];
    }
    for (i = 0; secret != NULL && i < PORT_RELAY_KEY_BYTES; i++) {
        out[HELLO_SECRET + i] = secret[i];
    }
    for (i = 0; i < TOKEN_BYTES; i++) {
        out[HELLO_TOKEN + i] = g_room.token[i];
    }
    out[HELLO_FLAGS] = (u8)(g_room.plays ? ROOM_FLAG_PLAYS : 0);
    out[HELLO_RULE] = (u8)(g_room.rule_asked >= 0 ? g_room.rule_asked : 0);
    out[HELLO_ARG] = (u8)arg;
    port_NetPutWord(out + HELLO_STAMP, now);
    out[HELLO_RTT] = (u8)g_room.rtt;
    out[HELLO_RTT + 1] = (u8)(g_room.rtt >> BITS_PER_BYTE);
    port_NetPutWord(out + HELLO_NUMBER, number);
    port_HostUdpSend(g_room.socket, &g_room.server, out, sizeof(out));
    g_room.stay_at = now;
}

/* A match of ROSTER's: its number, its players, its code and ticket. */
static void port_RoomTakeMatch(
    PortRoomMatch *out, const u8 *head, const u8 *code, const u8 *ticket) {
    u32 i;

    out->has_ticket = 0;
    for (i = 0; i < PORT_RELAY_TICKET_BYTES; i++) {
        out->ticket[i] = ticket[i];
        out->has_ticket |= ticket[i] != 0;
    }
    port_RoomGetText(out->code, code, CODE_FIELD - 2);
    out->a = head[0];
    out->b = head[1];
}

/* A ROSTER from the server: the room as it is now. */
static void port_RoomTake(const u8 *in) {
    PortRoomView *view = &g_room.view;
    u32 stamp = port_NetGetWord(in + HALF_BYTES * 2);
    u32 trip = port_NetClock() - stamp;
    s32 i;

    if (stamp != 0 && trip < ASK_GIVE_UP_MS) {
        g_room.rtt = g_room.rtt == 0
                         ? trip
                         : (g_room.rtt * (RTT_SMOOTH - 1) + trip) / RTT_SMOOTH;
    }
    g_room.heard_at = port_NetClock();
    port_RoomGetText(view->code, in + ROSTER_CODE, CODE_FIELD - 2);
    port_RoomGetText(view->name, in + ROSTER_NAME, NAME_FIELD);
    port_RoomGetText(view->setup, in + ROSTER_SETUP, SETUP_FIELD);
    for (i = 0; i < TOKEN_BYTES; i++) {
        g_room.token[i] = in[ROSTER_TOKEN + i];
    }
    for (i = 0; i < CODE_FIELD - 2 && view->code[i] != 0; i++) {
        g_room.code[i] = view->code[i];
    }
    g_room.code[i] = 0;
    view->in_room = TRUE;
    view->you = in[ROSTER_YOU];
    view->count = in[ROSTER_HOW_MANY] < PORT_ROOM_MEMBERS ? in[ROSTER_HOW_MANY]
                                                          : PORT_ROOM_MEMBERS;
    view->rule = in[ROSTER_RULE];
    view->host = in[ROSTER_HOST];
    view->plays = g_room.plays;
    view->watching = g_room.watching;
    view->rtt = g_room.rtt;
    view->match.number = (s32)port_NetGetWord(in + ROSTER_MATCH);
    port_RoomTakeMatch(&view->match, in + ROSTER_MATCH + WORD_BYTES,
                       in + ROSTER_MATCH_CODE, in + ROSTER_MATCH_TICKET);
    if (view->match.a == PORT_ROOM_NOBODY) {
        view->match.number = 0;
    }
    view->last_number = (s32)port_NetGetWord(in + ROSTER_LAST);
    view->last_a = in[ROSTER_LAST + WORD_BYTES];
    view->last_b = in[ROSTER_LAST + WORD_BYTES + 1];
    view->last_winner = in[ROSTER_LAST + WORD_BYTES + 2];
    for (i = 0; i < view->count; i++) {
        const u8 *at = in + ROSTER_MEMBERS + i * MEMBER_BYTES;
        PortRoomMember *member = &view->members[i];

        member->id = at[0];
        member->flags = at[1];
        member->wins = at[2];
        member->rtt =
            (u32)(at[WORD_BYTES] | (at[WORD_BYTES + 1] << BITS_PER_BYTE));
        port_RoomGetText(member->player, at + 2 * WORD_BYTES, PLAYER_FIELD);
    }
    view->side_count = 0;
    for (i = 0; i < PORT_ROOM_SIDES; i++) {
        const u8 *at = in + ROSTER_SIDES + i * SIDE_BYTES;
        PortRoomMatch *side = &view->sides[view->side_count];

        if (at[0] == PORT_ROOM_NOBODY) {
            continue;
        }
        port_RoomTakeMatch(
            side, at, at + 2 * WORD_BYTES, at + 2 * WORD_BYTES + CODE_FIELD);
        side->number = (s32)port_NetGetWord(at + WORD_BYTES);
        view->side_count++;
    }
    /* what was asked and is now taken */
    if (g_room.rule_asked >= 0 && view->rule == g_room.rule_asked) {
        g_room.rule_asked = -1;
    }
    for (i = 0; i < view->side_count; i++) {
        if (view->sides[i].a == view->you) {
            g_room.side_open = FALSE;
        }
        if (view->sides[i].b == view->you) {
            g_room.side_join = -1;
        }
    }
    if (g_room.result_number != 0 &&
        view->match.number != g_room.result_number) {
        s32 still = FALSE;

        for (i = 0; i < view->side_count; i++) {
            still |= view->sides[i].number == g_room.result_number;
        }
        if (!still) {
            g_room.result_number = 0;
        }
    }
}

/* Everything the room's socket has, waiting up to `wait_ms` for the
 * first: the server's alone, each to its reader. */
static void port_RoomReceive(u32 wait_ms) {
    u8 in[PORT_NET_DATAGRAM_MAX];
    PortNetAddress from;
    s32 got;

    if (g_room.socket < 0) {
        return;
    }
    for (got =
             port_HostUdpReceive(g_room.socket, in, sizeof(in), &from, wait_ms);
         got > 0;
         got = port_HostUdpReceive(g_room.socket, in, sizeof(in), &from, 0)) {
        s32 detail;
        u32 number;
        s32 kind = port_NetReadHeader(in, got, &detail, &number);

        if (!port_NetAddressesEqual(&from, &g_room.server)) {
            continue;
        }
        if (kind == KIND_ROSTER && got == ROSTER_BYTES && detail == STATUS_OK) {
            port_RoomTake(in);
        } else if (kind == KIND_ROSTER && got == ROSTER_BYTES &&
                   detail == STATUS_NO_ROOM && g_room.is_on) {
            port_Log("room: the lobby has no room %s for this member now\n",
                     g_room.code);
            g_room.view.in_room = FALSE;
        } else if (kind == KIND_FEED_ACK || kind == KIND_SCENE) {
            port_WatchTake(in, got);
        }
    }
}

/* ---- opening and entering ---- */

static void port_RoomText(char *out, u32 room, const char *format, ...) {
    PortArgs args;

    PORT_ARGS_START(args, format);
    port_FormatText(out, room, format, args);
    PORT_ARGS_END(args);
}

/* The server's refusal in a player's words. */
static void port_RoomRefusal(s32 status, const u8 *in, char *why, u32 room) {
    char theirs[SETUP_FIELD + 1];

    switch (status) {
    case STATUS_NO_ROOM:
        port_RoomText(
            why, room, "There is no room %s at the lobby: it may have closed.",
            g_room.code);
        break;
    case STATUS_ROOM_FULL:
        port_RoomText(
            why, room, "Room %s has eight members already.", g_room.code);
        break;
    case STATUS_SERVER_FULL:
        port_RoomText(
            why, room, "The lobby is full just now: try again in a while.");
        break;
    case STATUS_VERSION:
        port_RoomText(why, room,
                      "The lobby server is of another version: this release "
                      "cannot use it.");
        break;
    case STATUS_WRONG_PASSWORD:
        port_RoomText(why, room, "The password is not room %s's.", g_room.code);
        break;
    case STATUS_RATE_LIMITED:
        port_RoomText(why, room,
                      "The lobby has had too many tries from your address: "
                      "wait a minute and try again.");
        break;
    case STATUS_INCOMPATIBLE:
        port_RoomGetText(theirs, in + ROSTER_SETUP, SETUP_FIELD);
        port_RoomText(why, room,
                      "Room %s's game is not compatible with yours: its setup "
                      "code is %s.",
                      g_room.code, theirs);
        break;
    default:
        port_RoomText(
            why, room, "The lobby could not take this request (%d).", status);
        break;
    }
}

/* The room's socket and words, before an open or an enter: 0 where the
 * lobby's address is not one. */
static s32 port_RoomStart(
    const char *lobby, const char *player, s32 plays, char *why, u32 room) {
    port_RoomLeave();
    if (!port_ParseNetAddress(lobby, &g_room.server) ||
        g_room.server.port == 0) {
        port_RoomText(why, room,
                      "The lobby server's address is not one (as "
                      "203.0.113.7:7845).");
        return 0;
    }
    g_room.socket = port_RelayOpenSocket(&g_room.server);
    if (g_room.socket < 0) {
        port_RoomText(why, room, "This PC's network could not be used.");
        return 0;
    }
    port_RoomPutText((u8 *)g_room.player, PORT_RELAY_PLAYER_BYTES - 1, player);
    g_room.player[PORT_RELAY_PLAYER_BYTES - 1] = 0;
    g_room.plays = plays;
    g_room.watching = TRUE;
    g_room.rtt = 0;
    return 1;
}

/* An open or an enter said until the server answers: 1 and the room, or
 * 0 with why. A locked room's challenge is answered with `key`'s proof
 * (an enter), made of the password and the salt it gives. */
static s32 port_RoomAsk(
    s32 action, const char *name, const u8 *salt, const u8 *verifier,
    const char *password, char *why, u32 room) {
    u8 in[PORT_NET_DATAGRAM_MAX];
    u8 proof[PORT_RELAY_KEY_BYTES];
    s32 has_proof = FALSE;
    u32 start = port_NetClock();
    u32 asked_at = 0;

    for (;;) {
        PortNetAddress from;
        u32 now = port_NetClock();
        s32 got;

        if (now - start > ASK_GIVE_UP_MS) {
            port_RoomText(why, room,
                          "The lobby server does not answer. Is its address "
                          "right, and does your network let UDP through?");
            return 0;
        }
        if (now - asked_at >= ASK_EVERY_MS) {
            port_RoomSay(action, 0, 0, name, salt,
                         action == ACTION_OPEN ? verifier
                         : has_proof           ? proof
                                               : NULL);
            asked_at = now;
        }
        got =
            port_HostUdpReceive(g_room.socket, in, sizeof(in), &from, POLL_MS);
        if (got == ROSTER_BYTES && in[0] == 'R' && in[1] == 'B' &&
            in[2] == KIND_ROSTER &&
            port_NetAddressesEqual(&from, &g_room.server)) {
            s32 status = in[3];

            if (status == STATUS_OK) {
                port_RoomTake(in);
                g_room.is_on = TRUE;
                return 1;
            }
            if (status == STATUS_PASSWORD && action == ACTION_ENTER) {
                u8 key[PORT_RELAY_KEY_BYTES];

                if (password == NULL || password[0] == 0) {
                    port_RoomText(
                        why, room, "Room %s is locked: it needs its password.",
                        g_room.code);
                    return 0;
                }
                port_RelayRoomKey(in + ROSTER_SALT, password, key);
                port_RelayHmac(key, in + ROSTER_CHALLENGE, SALT_FIELD, proof);
                has_proof = TRUE;
                asked_at = 0;
                continue;
            }
            port_RoomRefusal(status, in, why, room);
            return 0;
        }
    }
}

/* A room of several opened at the lobby (port_room.h): its password's
 * salt made of the clock and its verifier, as a two-player room's. */
s32 port_RoomOpen(
    const char *lobby, const char *name, const char *player,
    const char *password, s32 rule, s32 plays, char *why, u32 room) {
    u8 salt[SALT_FIELD];
    u8 verifier[PORT_RELAY_KEY_BYTES];
    s32 locked = password != NULL && password[0] != 0;
    s32 ok;

    if (!port_RoomStart(lobby, player, plays, why, room)) {
        return 0;
    }
    if (locked) {
        u32 seed = port_HostTicks() ^ port_NetClock() ^ SALT_SEED;
        u32 i;

        for (i = 0; i < SALT_FIELD; i++) {
            seed = seed * SALT_MULTIPLY + SALT_ADD;
            salt[i] = (u8)(seed >> SALT_BYTE_SHIFT);
        }
        port_RelayRoomKey(salt, password, verifier);
    }
    g_room.code[0] = 0;
    g_room.rule_asked = rule;
    ok = port_RoomAsk(ACTION_OPEN, name, locked ? salt : NULL,
                      locked ? verifier : NULL, NULL, why, room);
    g_room.rule_asked = -1;
    if (!ok) {
        port_RoomLeave();
        return 0;
    }
    port_Log(
        "room: the room's code is %s (a room of several, %s)\n", g_room.code,
        rule == PORT_ROOM_RULE_FIXED    ? "a fixed turn"
        : rule == PORT_ROOM_RULE_WINNER ? "the winner stays"
                                        : "no rotation");
    return 1;
}

/* A room of several entered by its code (port_room.h). */
s32 port_RoomEnter(const char *lobby, const char *code, const char *player,
                   const char *password, s32 plays, char *why, u32 room) {
    u32 i;

    if (!port_RoomStart(lobby, player, plays, why, room)) {
        return 0;
    }
    for (i = 0; code[i] != 0 && i + 1 < sizeof(g_room.code); i++) {
        g_room.code[i] = code[i] >= 'a' && code[i] <= 'z'
                             ? (char)(code[i] - 'a' + 'A')
                             : code[i];
    }
    g_room.code[i] = 0;
    if (!port_RoomAsk(ACTION_ENTER, "", NULL, NULL, password, why, room)) {
        port_RoomLeave();
        return 0;
    }
    port_Log("room: in room %s (%s), member %d of %d\n", g_room.code,
             g_room.view.name, g_room.view.you, g_room.view.count);
    return 1;
}

/* ---- in the room ---- */

void port_RoomLeave(void) {
    u32 i;

    if (g_room.is_on) {
        for (i = 0; i < 3; i++) {
            port_RoomSay(ACTION_LEAVE, 0, 0, "", NULL, NULL);
        }
        port_Log("room: left room %s\n", g_room.code);
    }
    port_WatchStop();
    if (g_room.socket >= 0) {
        port_HostUdpClose(g_room.socket);
    }
    for (i = 0; i < sizeof(g_room); i++) {
        ((u8 *)&g_room)[i] = 0;
    }
    g_room.socket = -1;
    g_room.rule_asked = -1;
    g_room.side_join = -1;
}

s32 port_RoomIsOn(void) { return g_room.is_on; }

/* The room's turn (port_room.h): the server's answers read, and every
 * STAY_EVERY_MS what is asked and not yet taken, or that this member is
 * still there. */
void port_RoomTick(void) {
    u32 now;

    if (!g_room.is_on) {
        return;
    }
    port_RoomReceive(0);
    now = port_NetClock();
    if (now - g_room.stay_at < STAY_EVERY_MS) {
        return;
    }
    /* what is asked and not yet taken, else that this member is here */
    if (g_room.result_number != 0) {
        port_RoomSay(ACTION_RESULT, g_room.result_winner,
                     (u32)g_room.result_number, "", NULL, NULL);
    } else if (g_room.rule_asked >= 0) {
        port_RoomSay(ACTION_RULE, 0, 0, "", NULL, NULL);
    } else if (g_room.side_open) {
        port_RoomSay(ACTION_SIDE_OPEN, 0, 0, "", NULL, NULL);
    } else if (g_room.side_join >= 0) {
        port_RoomSay(ACTION_SIDE_JOIN, g_room.side_join, 0, "", NULL, NULL);
    } else {
        port_RoomSay(ACTION_STAY, 0, 0, "", NULL, NULL);
    }
}

void port_RoomGet(PortRoomView *out) {
    *out = g_room.view;
    out->in_room = g_room.is_on && g_room.view.in_room;
    out->plays = g_room.plays;
    out->watching = g_room.watching;
}

const char *port_RoomPlayer(s32 id) {
    s32 i;

    for (i = 0; i < g_room.view.count; i++) {
        if (g_room.view.members[i].id == id) {
            return g_room.view.members[i].player;
        }
    }
    return "";
}

/* An ask said now (and again with the next words until it is taken). */
static void port_RoomAskNow(void) { g_room.stay_at -= STAY_EVERY_MS; }

void port_RoomSetRule(s32 rule) {
    g_room.rule_asked = rule;
    port_RoomAskNow();
}

void port_RoomSetPlays(s32 plays) {
    g_room.plays = plays;
    port_RoomAskNow();
}

void port_RoomSetWatching(s32 on) { g_room.watching = on; }

void port_RoomSideOpen(void) {
    g_room.side_open = TRUE;
    port_RoomAskNow();
}

void port_RoomSideJoin(s32 side) {
    g_room.side_join = side;
    port_RoomAskNow();
}

void port_RoomResult(s32 number, s32 winner) {
    port_Log("room: match %d over: %s\n", number,
             winner == PORT_ROOM_WON_FIRST    ? "its first player won"
             : winner == PORT_ROOM_WON_SECOND ? "its second player won"
                                              : "nobody won");
    g_room.result_number = number;
    g_room.result_winner = winner;
    port_RoomAskNow();
    port_RoomTick();
}

static s32 port_RoomIsDone(s32 number) {
    s32 i;

    for (i = 0; i < g_room.done_count && i < DONE_KEPT; i++) {
        if (g_room.done[i] == number) {
            return TRUE;
        }
    }
    return FALSE;
}

void port_RoomDone(s32 number) {
    g_room.done[g_room.done_count++ % DONE_KEPT] = number;
}

/* A match `next` asks of this member. */
static void port_RoomNextOf(
    PortRoomNext *out, s32 what, const PortRoomMatch *match, s32 side) {
    u32 i;

    out->what = what;
    out->side = side;
    out->match = *match;
    out->role =
        match->a == g_room.view.you ? PORT_SESSION_HOST : PORT_SESSION_JOIN;
    for (i = 0; i < PORT_RELAY_PLAYER_BYTES; i++) {
        out->first[i] = port_RoomPlayer(match->a)[i];
        out->second[i] = port_RoomPlayer(match->b)[i];
        if (out->first[i] == 0 && out->second[i] == 0) {
            break;
        }
    }
    out->first[PORT_RELAY_PLAYER_BYTES - 1] = 0;
    out->second[PORT_RELAY_PLAYER_BYTES - 1] = 0;
}

/* What this member is to do now (port_room.h): a match of its own
 * first, the rotation's or one beside it, then the rotation's to
 * watch. */
s32 port_RoomNext(PortRoomNext *out) {
    const PortRoomView *view = &g_room.view;
    const PortRoomMatch *match = &view->match;
    s32 you = view->you;
    s32 i;

    out->what = PORT_ROOM_NEXT_NONE;
    if (!g_room.is_on || !view->in_room) {
        return PORT_ROOM_NEXT_NONE;
    }
    if (match->number != 0 && (match->a == you || match->b == you) &&
        match->has_ticket && !port_RoomIsDone(match->number)) {
        port_RoomNextOf(out, PORT_ROOM_NEXT_PLAY, match, FALSE);
        return out->what;
    }
    for (i = 0; i < view->side_count; i++) {
        const PortRoomMatch *side = &view->sides[i];

        if (side->b != PORT_ROOM_NOBODY && (side->a == you || side->b == you) &&
            side->has_ticket && !port_RoomIsDone(side->number)) {
            port_RoomNextOf(out, PORT_ROOM_NEXT_PLAY, side, TRUE);
            return out->what;
        }
    }
    if (g_room.watching && match->number != 0 && match->a != you &&
        match->b != you && !port_RoomIsDone(match->number)) {
        port_RoomNextOf(out, PORT_ROOM_NEXT_WATCH, match, FALSE);
        return out->what;
    }
    return PORT_ROOM_NEXT_NONE;
}

void port_RoomSend(const void *bytes, u32 count) {
    if (g_room.socket >= 0) {
        port_HostUdpSend(g_room.socket, &g_room.server, bytes, count);
    }
}

s32 port_RoomCode(char *code, u8 *token) {
    u32 i;

    for (i = 0; i < CODE_FIELD; i++) {
        code[i] = i < sizeof(g_room.code) ? g_room.code[i] : 0;
    }
    for (i = 0; i < TOKEN_BYTES; i++) {
        token[i] = g_room.token[i];
    }
    return g_room.is_on;
}

/* Waits up to `wait_ms` for the server's next words (watch.c's waits). */
void port_RoomWait(u32 wait_ms) {
    if (!g_room.is_on) {
        return;
    }
    port_RoomReceive(wait_ms);
}
