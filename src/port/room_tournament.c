/* A tournament in a room of several (port_room.h; docs/NETPLAY.md,
 * section 22; docs/PORT.md, "A tournament in a room"). Outside the state,
 * as the room's client is: the bracket is the room's host's, and each of
 * its matches a session of two whose state carries only that match
 * (port_tournament.h's PortTournamentForm).
 *
 * Nothing of the lobby is new: it is version 4 as it was. The bracket goes
 * by what a room already carries.
 *
 * - **The host's board.** The room's host opens a match beside the room's
 *   that nobody joins (SIDE_OPEN) and sends as its feed's settings, the 64
 *   bytes a spectator is given of a match, the tournament as it stands
 *   (BOARD_...: its setup, each entrant a member's id or the CPU, the
 *   winners of the matches decided in their order, the walkovers, and the
 *   two members who are to play the next match). Every member asks the
 *   server for it (WATCH) as for any match's settings, about once a
 *   second, and takes the newest; the server keeps the settings as they
 *   were last sent. A match beside the room's that nobody joins is let go
 *   after two minutes: the host opens another.
 * - **The rotation follows the bracket.** The host sets the room's
 *   rotation to a fixed turn, and each member says it plays (the ROOM's
 *   flag that puts it in the rotation) only while the board names it for
 *   the next match: so the server's first two free players are always the
 *   board's two, and the server makes the match, its code and its ticket,
 *   as for any rotation. A player's word that he plays is held back once
 *   he is in a match of the room (the board then names a later one), so
 *   that the two who just played are never put together again before the
 *   host has told the next pair. Where the host is one of the two, it
 *   lets its board go first (a member in a match beside the room's is not
 *   free), once the other shows he plays.
 * - **The result** is the room's: the players tell it as of any match,
 *   and the host reads it in the room's last result, by the two who
 *   played. A member who leaves in the middle of his match loses it, as
 *   in any room's match; one who has left is out, each later match of his
 *   a walkover for the other. A match not met (a draw, the meeting failed)
 *   is played again.
 * - **The CPU** is an entrant too: its match is played by two members,
 *   the CPU's side carried by the room's host where it is not the other
 *   side's player, else by another member, both CPUs by the host and
 *   another. The session's host's welcome says which side is whose; each
 *   machine computes the CPU (it is the game's own). A match of two CPUs
 *   with no other member to carry it is decided by the tournament's dice.
 * - The host's leave ends it: the board was its. */
#include "common.h"
#include "port_host.h"
#include "port_net.h"
#include "port_room.h"
#include "bb2.h"

#define CODE_FIELD 8
#define TOKEN_BYTES 16
#define WORD_BYTES 4
#define HALF_BYTES 2
#define BITS_PER_BYTE 8
#define BYTE_MASK 0xFF
#define NO_START 0xFFFFFFFFU
#define WHY_BYTES 400
#define DATE_BYTES 24
/* an entrant's name as shown, "CPU N" among them */
#define NAME_BYTES (PORT_TOURNAMENT_NAME_BYTES + 8)

enum {
    KIND_FEED = PORT_NET_KIND_RELAY + 11,
    KIND_FEED_ACK,
    KIND_WATCH,
    KIND_SCENE
};

#define PART_SETTINGS 1
#define FEED_CODE PORT_NET_HEADER_BYTES
#define FEED_TICKET (FEED_CODE + CODE_FIELD)
#define FEED_BODY (FEED_TICKET + PORT_RELAY_TICKET_BYTES)
#define WATCH_BYTES 192
#define WATCH_ROOM PORT_NET_HEADER_BYTES
#define WATCH_TOKEN (WATCH_ROOM + CODE_FIELD)
#define WATCH_MATCH (WATCH_TOKEN + TOKEN_BYTES)
#define WATCH_START (WATCH_MATCH + CODE_FIELD)
#define WATCH_WANTS (WATCH_START + WORD_BYTES)
#define WATCH_FLAGS (WATCH_WANTS + WORD_BYTES)
#define SCENE_BODY (PORT_NET_HEADER_BYTES + CODE_FIELD)

/* The board, the 64 bytes of its feed's settings. */
enum {
    BOARD_MARK = 0,                            /* "RBTN" */
    BOARD_NUMBER = BOARD_MARK + WORD_BYTES,    /* the host's count of them */
    BOARD_VERSION = BOARD_NUMBER + HALF_BYTES, /* changed by every change */
    BOARD_HOST = BOARD_VERSION + HALF_BYTES,
    BOARD_FLAGS,
    BOARD_COUNT,
    BOARD_DOUBLE,
    BOARD_RULE,
    BOARD_RULE_COUNT,
    BOARD_RESULTS,                         /* matches decided */
    BOARD_SEED,                            /* the dice */
    BOARD_SEEDS = BOARD_SEED + WORD_BYTES, /* the entrant of each seed */
    BOARD_ENTRANTS = BOARD_SEEDS + PORT_BRACKET_MOST, /* three bytes each */
    BOARD_WINNERS = BOARD_ENTRANTS + 3 * PORT_BRACKET_MOST, /* a bit each */
    BOARD_WALKOVERS = BOARD_WINNERS + HALF_BYTES,           /* ... */
    BOARD_PAIR = BOARD_WALKOVERS + HALF_BYTES,              /* two members */
    BOARD_AFTER = BOARD_PAIR + 2, /* the room's match the pair comes after */
    BOARD_GONE = BOARD_AFTER + WORD_BYTES, /* entrants who left, a bit each */
    BOARD_BYTES = BOARD_GONE + 1
};

#define BOARD_MARK_WORD 0x4E544252U /* "RBTN" */
#define BOARD_FLAG_DONE 0x01
/* an entrant of the board: a member's id, or the CPU at a level */
#define BOARD_CPU 0x80
#define BOARD_LEVEL_MASK 0x7F

/* How often the host sends its board, asks for its match beside the
 * room's, and how long a board of a tournament over is kept up; how often
 * a member asks for the board. */
#define FEED_EVERY_MS 500
#define OPEN_EVERY_MS 2000
#define DONE_KEPT_MS 20000
#define ASK_EVERY_MS 700
/* the dice of each match's draws, from the tournament's */
#define DICE_STEP 0x9E3779B9U

static struct {
    s32 on;
    s32 done;
    s32 is_host;
    s32 host;
    u32 number;
    u32 version;
    PortTournamentSetup setup;
    u8 member[PORT_BRACKET_MOST];
    u8 seeds[PORT_BRACKET_MOST];
    u8 winners[PORT_BRACKET_MATCHES];
    s32 results;
    u32 walkovers; /* by the order of the results */
    u32 gone;
    s32 pair[2];
    u32 after;
    PortBracket bracket;
    char date[DATE_BYTES];
    /* the host's: its board's match beside the room's (its code and
     * ticket, its number), when it last sent and asked */
    s32 has_board;
    PortRoomMatch board;
    u32 fed_at;
    u32 opened_at;
    s32 closing; /* its board let go, this side's number */
    u32 done_at;
    s32 rule_set;
    s32 rule_kept;
    s32 ready; /* every member has read the board: pairs may be named */
    /* a member's: the newest board's version had, the room's last match
     * it was in, its own word of playing before, and when it asked */
    u32 had_number;
    u32 had_version;
    u32 seen_match;
    s32 plays_kept;
    s32 plays_held;
    u32 asked_at;
    char asked_code[PORT_ROOM_CODE_BYTES];
    s32 recorded;
    s32 said;
} g_rt;

static void port_RtText(char *out, u32 room, const char *format, ...) {
    PortArgs args;

    PORT_ARGS_START(args, format);
    port_FormatText(out, room, format, args);
    PORT_ARGS_END(args);
}

static void port_RtCopy(char *out, u32 room, const char *text) {
    u32 i;

    for (i = 0; text != NULL && text[i] != 0 && i + 1 < room; i++) {
        out[i] = text[i];
    }
    out[i] = 0;
}

static s32 port_RtIsMember(s32 entrant) {
    return entrant >= 0 && entrant < PORT_BRACKET_MOST &&
           g_rt.member[entrant] != PORT_ROOM_NOBODY;
}

static s32 port_RtInRoom(const PortRoomView *view, s32 id) {
    s32 i;

    for (i = 0; i < view->count; i++) {
        if (view->members[i].id == id) {
            return TRUE;
        }
    }
    return FALSE;
}

static s32 port_RtFlags(const PortRoomView *view, s32 id) {
    s32 i;

    for (i = 0; i < view->count; i++) {
        if (view->members[i].id == id) {
            return view->members[i].flags;
        }
    }
    return 0;
}

/* The bracket made again from the seeds and every result in its order:
 * the same on every member that has the same board. */
static void port_RtRebuild(void) {
    s32 i;

    port_BracketBuild(
        &g_rt.bracket, g_rt.setup.count, g_rt.setup.is_double, g_rt.seeds);
    for (i = 0; i < g_rt.results; i++) {
        s32 next = port_BracketNext(&g_rt.bracket);

        if (next < 0) {
            break;
        }
        port_BracketReport(&g_rt.bracket, next, g_rt.winners[i]);
    }
    g_rt.done = port_BracketIsDone(&g_rt.bracket);
}

/* An entrant's name as shown: its member's, or "CPU N". */
static void port_RtName(s32 entrant, char *out, u32 room) {
    if (entrant < 0 || entrant >= g_rt.setup.count) {
        port_RtCopy(out, room, "nobody");
        return;
    }
    port_TournamentName(&g_rt.setup.entrant[entrant], entrant, out, room);
}

/* The members' names kept as the roster gives them, for those who leave. */
static void port_RtKeepNames(void) {
    s32 i;

    for (i = 0; i < g_rt.setup.count; i++) {
        const char *name;

        if (!port_RtIsMember(i)) {
            continue;
        }
        name = port_RoomPlayer(g_rt.member[i]);
        if (name[0] != 0) {
            port_RtCopy(g_rt.setup.entrant[i].name,
                        sizeof(g_rt.setup.entrant[i].name), name);
        }
    }
}

/* ---- the board ---- */

static void port_RtPutBoard(u8 *out) {
    s32 i;

    for (i = 0; i < PORT_SESSION_SETTINGS_BYTES; i++) {
        out[i] = 0;
    }
    port_NetPutWord(out + BOARD_MARK, BOARD_MARK_WORD);
    out[BOARD_NUMBER] = (u8)g_rt.number;
    out[BOARD_NUMBER + 1] = (u8)(g_rt.number >> BITS_PER_BYTE);
    out[BOARD_VERSION] = (u8)g_rt.version;
    out[BOARD_VERSION + 1] = (u8)(g_rt.version >> BITS_PER_BYTE);
    out[BOARD_HOST] = (u8)g_rt.host;
    out[BOARD_FLAGS] = (u8)(g_rt.done ? BOARD_FLAG_DONE : 0);
    out[BOARD_COUNT] = g_rt.setup.count;
    out[BOARD_DOUBLE] = g_rt.setup.is_double;
    out[BOARD_RULE] = g_rt.setup.rule;
    out[BOARD_RULE_COUNT] = g_rt.setup.rule_count;
    out[BOARD_RESULTS] = (u8)g_rt.results;
    port_NetPutWord(out + BOARD_SEED, g_rt.setup.seed);
    for (i = 0; i < PORT_BRACKET_MOST; i++) {
        const PortTournamentEntrant *entrant = &g_rt.setup.entrant[i];
        u8 *at = out + BOARD_ENTRANTS + 3 * i;

        out[BOARD_SEEDS + i] = g_rt.seeds[i];
        at[0] = port_RtIsMember(i)
                    ? g_rt.member[i]
                    : (u8)(BOARD_CPU | (entrant->cpu & BOARD_LEVEL_MASK));
        at[1] = entrant->fighter;
        at[2] = entrant->weapon;
    }
    for (i = 0; i < g_rt.results && i < PORT_BRACKET_MATCHES; i++) {
        if (g_rt.winners[i]) {
            out[BOARD_WINNERS + i / BITS_PER_BYTE] |=
                (u8)(1 << (i % BITS_PER_BYTE));
        }
        if ((g_rt.walkovers >> i) & 1) {
            out[BOARD_WALKOVERS + i / BITS_PER_BYTE] |=
                (u8)(1 << (i % BITS_PER_BYTE));
        }
    }
    out[BOARD_PAIR] = (u8)g_rt.pair[0];
    out[BOARD_PAIR + 1] = (u8)g_rt.pair[1];
    port_NetPutWord(out + BOARD_AFTER, g_rt.after);
    out[BOARD_GONE] = (u8)g_rt.gone;
}

/* A member takes the host's board: the tournament as it stands. */
static void port_RtTakeBoard(const u8 *in) {
    u32 number =
        (u32)(in[BOARD_NUMBER] | in[BOARD_NUMBER + 1] << BITS_PER_BYTE);
    u32 version =
        (u32)(in[BOARD_VERSION] | in[BOARD_VERSION + 1] << BITS_PER_BYTE);
    s32 fresh;
    s32 i;

    if (port_NetGetWord(in + BOARD_MARK) != BOARD_MARK_WORD ||
        in[BOARD_COUNT] < PORT_BRACKET_LEAST ||
        in[BOARD_COUNT] > PORT_BRACKET_MOST ||
        in[BOARD_RESULTS] > PORT_BRACKET_MATCHES) {
        return;
    }
    fresh = !g_rt.on || number != g_rt.had_number;
    if (!fresh && version == g_rt.had_version) {
        return;
    }
    if (fresh) {
        PortRoomView view;
        PortDate date;

        port_RoomGet(&view);
        g_rt.plays_kept = view.plays;
        g_rt.plays_held = FALSE;
        g_rt.seen_match = 0;
        g_rt.recorded = FALSE;
        g_rt.said = 0;
        port_HostDate(&date);
        port_RtText(g_rt.date, sizeof(g_rt.date),
                    "%04d-%02d-%02dT%02d:%02d:%02d", date.year, date.month,
                    date.day, date.hour, date.minute, date.second);
        for (i = 0; i < (s32)sizeof(g_rt.setup); i++) {
            ((u8 *)&g_rt.setup)[i] = 0;
        }
    }
    g_rt.on = TRUE;
    g_rt.is_host = FALSE;
    g_rt.had_number = number;
    g_rt.had_version = version;
    g_rt.number = number;
    g_rt.version = version;
    g_rt.host = in[BOARD_HOST];
    g_rt.setup.count = in[BOARD_COUNT];
    g_rt.setup.is_double = in[BOARD_DOUBLE];
    g_rt.setup.seeding = PORT_TOURNAMENT_SEED_ORDER;
    g_rt.setup.rule = in[BOARD_RULE];
    g_rt.setup.rule_count = in[BOARD_RULE_COUNT];
    g_rt.setup.seed = port_NetGetWord(in + BOARD_SEED);
    g_rt.results = in[BOARD_RESULTS];
    for (i = 0; i < PORT_BRACKET_MOST; i++) {
        const u8 *at = in + BOARD_ENTRANTS + 3 * i;
        PortTournamentEntrant *entrant = &g_rt.setup.entrant[i];

        g_rt.seeds[i] = in[BOARD_SEEDS + i];
        if (at[0] & BOARD_CPU) {
            g_rt.member[i] = PORT_ROOM_NOBODY;
            entrant->pad = PORT_TOURNAMENT_CPU;
            entrant->cpu = (u8)(at[0] & BOARD_LEVEL_MASK);
            entrant->name[0] = 0;
        } else {
            g_rt.member[i] = at[0];
            entrant->pad = PORT_ROOM_TOURNAMENT_MEMBER;
        }
        entrant->fighter = at[1];
        entrant->weapon = at[2];
    }
    g_rt.walkovers = 0;
    for (i = 0; i < g_rt.results; i++) {
        g_rt.winners[i] = (u8)((in[BOARD_WINNERS + i / BITS_PER_BYTE] >>
                                (i % BITS_PER_BYTE)) &
                               1);
        g_rt.walkovers |=
            (u32)((in[BOARD_WALKOVERS + i / BITS_PER_BYTE] >>
                   (i % BITS_PER_BYTE)) &
                  1)
            << i;
    }
    g_rt.pair[0] = in[BOARD_PAIR];
    g_rt.pair[1] = in[BOARD_PAIR + 1];
    g_rt.after = port_NetGetWord(in + BOARD_AFTER);
    g_rt.gone = in[BOARD_GONE];
    port_RtKeepNames();
    port_RtRebuild();
    /* (ended by its host before its champion) */
    g_rt.done |= (in[BOARD_FLAGS] & BOARD_FLAG_DONE) != 0;
    if (fresh) {
        port_Log("room tournament: the host's board: %d entrants, %s "
                 "elimination, %s %d\n",
                 g_rt.setup.count, g_rt.setup.is_double ? "double" : "single",
                 g_rt.setup.rule == PORT_MATCH_STOCK ? "stock" : "first_to",
                 g_rt.setup.rule_count);
    }
}

/* A SCENE or FEED_ACK of the room's socket (room.c): TRUE where it is the
 * board's, and taken. */
s32 port_RoomTournamentTake(const u8 *in, s32 bytes) {
    s32 detail;
    u32 number;
    s32 kind = port_NetReadHeader(in, bytes, &detail, &number);
    const char *code = g_rt.is_host ? g_rt.board.code : g_rt.asked_code;
    s32 i;

    if ((kind != KIND_SCENE && kind != KIND_FEED_ACK) || code[0] == 0 ||
        bytes < SCENE_BODY) {
        return FALSE;
    }
    for (i = 0; i < CODE_FIELD; i++) {
        char letter = i < PORT_ROOM_CODE_BYTES ? code[i] : 0;

        if ((char)in[PORT_NET_HEADER_BYTES + i] != letter) {
            return FALSE;
        }
        if (letter == 0) {
            break;
        }
    }
    if (kind == KIND_SCENE && detail == PART_SETTINGS && !g_rt.is_host &&
        bytes >= SCENE_BODY + PORT_SESSION_SETTINGS_BYTES) {
        port_RtTakeBoard(in + SCENE_BODY);
    }
    return TRUE;
}

/* ---- the host's ---- */

/* The board sent to its match beside the room's: the server keeps it as
 * that match's settings. */
static void port_RtFeed(void) {
    u8 out[FEED_BODY + PORT_SESSION_SETTINGS_BYTES];
    u32 i;

    port_NetHeader(out, KIND_FEED, PART_SETTINGS, 0);
    for (i = 0; i < CODE_FIELD; i++) {
        out[FEED_CODE + i] = 0;
    }
    for (i = 0; i < PORT_ROOM_CODE_BYTES && g_rt.board.code[i] != 0; i++) {
        out[FEED_CODE + i] = (u8)g_rt.board.code[i];
    }
    for (i = 0; i < PORT_RELAY_TICKET_BYTES; i++) {
        out[FEED_TICKET + i] = g_rt.board.ticket[i];
    }
    port_RtPutBoard(out + FEED_BODY);
    port_RoomSend(out, sizeof(out));
    g_rt.fed_at = port_NetClock();
}

/* The board changed: sent at once. */
static void port_RtChanged(void) {
    g_rt.version++;
    g_rt.fed_at = port_NetClock() - FEED_EVERY_MS;
}

/* A result into the bracket: `slot` of the next match won it. */
static void port_RtReport(s32 slot, s32 walkover) {
    s32 next = port_BracketNext(&g_rt.bracket);
    char winner[NAME_BYTES];
    char loser[NAME_BYTES];
    const PortBracketMatch *match;

    if (next < 0 || g_rt.results >= PORT_BRACKET_MATCHES) {
        return;
    }
    match = &g_rt.bracket.match[next];
    port_RtName(match->who[slot], winner, sizeof(winner));
    port_RtName(match->who[1 - slot], loser, sizeof(loser));
    if (walkover) {
        g_rt.walkovers |= 1U << g_rt.results;
    }
    g_rt.winners[g_rt.results++] = (u8)slot;
    port_BracketReport(&g_rt.bracket, next, slot);
    g_rt.done = port_BracketIsDone(&g_rt.bracket);
    g_rt.pair[0] = g_rt.pair[1] = PORT_ROOM_NOBODY;
    port_Log("room tournament: match %d (%s, %d): %s beat %s%s\n", g_rt.results,
             port_BracketRoundName(&g_rt.bracket, next), next, winner, loser,
             walkover ? " (a walkover: the other has left)" : "");
    port_RtChanged();
}

/* A member free to carry a CPU's side, not `but`: the host first. */
static s32 port_RtCarrier(const PortRoomView *view, s32 but) {
    s32 i;

    if (view->you != but) {
        return view->you;
    }
    for (i = 0; i < view->count; i++) {
        if (view->members[i].id != but) {
            return view->members[i].id;
        }
    }
    return PORT_ROOM_NOBODY;
}

/* The next match's two members, the walkovers of those who left and the
 * matches of two CPUs nobody can carry decided on the way. */
static void port_RtChoose(const PortRoomView *view) {
    while (!g_rt.done && g_rt.pair[0] == PORT_ROOM_NOBODY) {
        s32 next = port_BracketNext(&g_rt.bracket);
        const PortBracketMatch *match;
        s32 human[2];
        s32 gone[2];
        s32 slot;

        if (next < 0) {
            g_rt.done = TRUE;
            port_RtChanged();
            break;
        }
        match = &g_rt.bracket.match[next];
        for (slot = 0; slot < 2; slot++) {
            human[slot] = port_RtIsMember(match->who[slot]);
            gone[slot] = human[slot] && ((g_rt.gone >> match->who[slot]) & 1);
        }
        if (gone[0] || gone[1]) {
            port_RtReport(gone[0] ? 1 : 0, TRUE);
            continue;
        }
        if (human[0] && human[1]) {
            g_rt.pair[0] = g_rt.member[match->who[0]];
            g_rt.pair[1] = g_rt.member[match->who[1]];
        } else if (human[0] || human[1]) {
            s32 player = g_rt.member[match->who[human[0] ? 0 : 1]];

            g_rt.pair[0] = player;
            g_rt.pair[1] = port_RtCarrier(view, player);
        } else {
            g_rt.pair[0] = view->you;
            g_rt.pair[1] = port_RtCarrier(view, view->you);
        }
        if (g_rt.pair[1] == PORT_ROOM_NOBODY) {
            /* two CPUs and nobody to carry the other: the dice */
            g_rt.pair[0] = PORT_ROOM_NOBODY;
            port_Log("room tournament: nobody to play the CPUs' match with: "
                     "the dice decide it\n");
            port_RtReport((s32)((g_rt.setup.seed ^ (u32)next) & 1), FALSE);
            continue;
        }
        g_rt.after = (u32)view->last_number;
        port_RtChanged();
        {
            char first[NAME_BYTES];
            char second[NAME_BYTES];

            port_RtName(match->who[0], first, sizeof(first));
            port_RtName(match->who[1], second, sizeof(second));
            port_Log(
                "room tournament: next, %s: %s vs %s, played by %s "
                "and %s\n",
                port_BracketRoundName(&g_rt.bracket, next), first, second,
                port_RoomPlayer(g_rt.pair[0]), port_RoomPlayer(g_rt.pair[1]));
        }
    }
}

/* The slot of the next match that member `id` played, of the room's match
 * whose first player was `first`. */
static s32 port_RtSlotOf(s32 id, s32 first) {
    s32 next = port_BracketNext(&g_rt.bracket);
    const PortBracketMatch *match;
    s32 slot;

    if (next < 0) {
        return 0;
    }
    match = &g_rt.bracket.match[next];
    for (slot = 0; slot < 2; slot++) {
        if (port_RtIsMember(match->who[slot]) &&
            g_rt.member[match->who[slot]] == id) {
            return slot;
        }
    }
    for (slot = 0; slot < 2; slot++) {
        if (port_RtIsMember(match->who[slot])) {
            /* one CPU: the member who is not the player carried it */
            return 1 - slot;
        }
    }
    /* two CPUs: the room's match's first carried the first */
    return id == first ? 0 : 1;
}

/* The host's turn: who left, the room's result read, the next pair, the
 * rotation, and its board up (or let go where it is to play). */
static void port_RtHostTurn(const PortRoomView *view) {
    u32 now = port_NetClock();
    s32 in_pair;
    s32 wanted;
    s32 board_up = FALSE;
    s32 i;

    for (i = 0; i < g_rt.setup.count; i++) {
        if (port_RtIsMember(i) && !((g_rt.gone >> i) & 1) &&
            !port_RtInRoom(view, g_rt.member[i])) {
            char name[NAME_BYTES];

            port_RtName(i, name, sizeof(name));
            g_rt.gone |= 1U << i;
            port_Log("room tournament: %s has left the room: his matches "
                     "to come are walkovers\n",
                     name);
            port_RtChanged();
        }
    }
    if (g_rt.pair[0] != PORT_ROOM_NOBODY &&
        (u32)view->last_number > g_rt.after &&
        ((view->last_a == g_rt.pair[0] && view->last_b == g_rt.pair[1]) ||
         (view->last_a == g_rt.pair[1] && view->last_b == g_rt.pair[0]))) {
        s32 won = view->last_winner == PORT_ROOM_WON_FIRST ? view->last_a
                  : view->last_winner == PORT_ROOM_WON_SECOND
                      ? view->last_b
                      : -1;
        s32 next = port_BracketNext(&g_rt.bracket);
        s32 carried =
            next >= 0 && (!port_RtIsMember(g_rt.bracket.match[next].who[0]) ||
                          !port_RtIsMember(g_rt.bracket.match[next].who[1]));
        s32 carrier_left = carried && (!port_RtInRoom(view, view->last_a) ||
                                       !port_RtInRoom(view, view->last_b));

        g_rt.after = (u32)view->last_number;
        if (won < 0 || carrier_left) {
            /* not met, a draw, or the one who carried a CPU left: again */
            port_Log("room tournament: room match %d decided nothing of the "
                     "tournament: it is played again\n",
                     view->last_number);
            g_rt.pair[0] = g_rt.pair[1] = PORT_ROOM_NOBODY;
            port_RtChanged();
        } else {
            port_RtReport(port_RtSlotOf(won, view->last_a), FALSE);
        }
    }
    if (!g_rt.ready) {
        /* the first pair named once every member has read the board, which
         * names nobody yet: until then a member's word that it plays is
         * its own from before (a player comes into a room playing) */
        g_rt.ready = TRUE;
        for (i = 0; i < view->count; i++) {
            if (view->members[i].flags & PORT_ROOM_PLAYS) {
                g_rt.ready = FALSE;
            }
        }
    }
    if (g_rt.ready) {
        port_RtChoose(view);
    }
    in_pair = view->you == g_rt.pair[0] || view->you == g_rt.pair[1];
    /* the rotation a fixed turn, once nobody but the pair says it plays */
    if (!g_rt.rule_set) {
        s32 stray = FALSE;

        for (i = 0; i < view->count; i++) {
            s32 id = view->members[i].id;

            if ((view->members[i].flags & PORT_ROOM_PLAYS) && id != view->you &&
                id != g_rt.pair[0] && id != g_rt.pair[1]) {
                stray = TRUE;
            }
        }
        if (!stray && view->match.number == 0) {
            port_RoomSetRule(PORT_ROOM_RULE_FIXED);
            g_rt.rule_set = TRUE;
        }
    }
    /* the board: up, but where this program is to play now */
    for (i = 0; i < view->side_count; i++) {
        const PortRoomMatch *side = &view->sides[i];

        if (side->a == view->you && side->b == PORT_ROOM_NOBODY &&
            side->has_ticket && side->number != g_rt.closing) {
            if (!g_rt.has_board || side->number != g_rt.board.number) {
                g_rt.board = *side;
                g_rt.has_board = TRUE;
                g_rt.fed_at = now - FEED_EVERY_MS;
            }
            board_up = TRUE;
        }
    }
    if (!board_up) {
        g_rt.has_board = FALSE;
    }
    if (g_rt.done && g_rt.done_at == 0) {
        g_rt.done_at = now | 1;
        port_RoomSetRule(g_rt.rule_kept);
    }
    {
        s32 other = g_rt.pair[0] == view->you ? g_rt.pair[1] : g_rt.pair[0];

        /* this program plays the next: its board let go once the other
         * has read it (he says he plays), so that the server pairs them;
         * and a tournament over has its board up a while, then none */
        wanted = !(in_pair && !g_rt.done &&
                   (port_RtFlags(view, other) & PORT_ROOM_PLAYS)) &&
                 !(g_rt.done && now - g_rt.done_at >= DONE_KEPT_MS);
    }
    if (!wanted) {
        if (g_rt.has_board) {
            g_rt.closing = g_rt.board.number;
            g_rt.has_board = FALSE;
            port_RoomSideDone(g_rt.board.number);
        }
    } else if (!board_up && now - g_rt.opened_at >= OPEN_EVERY_MS) {
        s32 busy = view->match.a == view->you || view->match.b == view->you;

        for (i = 0; i < view->side_count; i++) {
            busy |=
                view->sides[i].a == view->you || view->sides[i].b == view->you;
        }
        if (!busy && view->side_count < PORT_ROOM_SIDES) {
            port_RoomSideOpen();
            g_rt.opened_at = now;
        }
    }
    if (g_rt.has_board && now - g_rt.fed_at >= FEED_EVERY_MS) {
        port_RtFeed();
    }
}

/* ---- every member's ---- */

/* The tournament over: said, and kept in the record, once. */
static void port_RtOver(void) {
    PortStatsTournament line;
    PortDate date;
    const PortBracket *bracket = &g_rt.bracket;
    char name[NAME_BYTES];
    s32 i;
    u32 byte;

    if (g_rt.recorded) {
        return;
    }
    g_rt.recorded = TRUE;
    port_RtName(bracket->champion, name, sizeof(name));
    port_Log("room tournament: over after %d matches; the champion: %s\n",
             g_rt.results, name);
    for (byte = 0; byte < sizeof(line); byte++) {
        ((u8 *)&line)[byte] = 0;
    }
    port_HostDate(&date);
    line.year = (u16)date.year;
    line.month = (u8)date.month;
    line.day = (u8)date.day;
    line.hour = (u8)date.hour;
    line.minute = (u8)date.minute;
    line.second = (u8)date.second;
    port_RtCopy(line.begun, sizeof(line.begun), g_rt.date);
    line.count = g_rt.setup.count;
    line.is_double = g_rt.setup.is_double;
    line.seeding = PORT_TOURNAMENT_SEED_ORDER;
    line.rule = g_rt.setup.rule;
    line.rule_count = g_rt.setup.rule_count;
    line.champion = bracket->champion;
    for (i = 0; i < g_rt.setup.count; i++) {
        s32 seed;

        port_RtName(i, line.entrant[i].name, sizeof(line.entrant[i].name));
        line.entrant[i].cpu =
            (s8)(port_RtIsMember(i) ? PORT_STATS_PERSON
                                    : g_rt.setup.entrant[i].cpu);
        line.entrant[i].place = (u8)port_BracketPlace(bracket, i);
        line.entrant[i].losses = (u8)port_BracketLosses(bracket, i);
        for (seed = 0; seed < bracket->count; seed++) {
            if (bracket->seeds[seed] == i) {
                line.entrant[i].seed = (u8)(seed + 1);
            }
        }
        port_Log("room tournament: place %d: %s (%d lost)\n",
                 line.entrant[i].place, line.entrant[i].name,
                 line.entrant[i].losses);
    }
    for (i = 0; i < bracket->match_count; i++) {
        const PortBracketMatch *match = &bracket->match[i];

        if (match->played && match->order >= 1 &&
            match->order <= PORT_STATS_TOURNAMENT_MATCHES) {
            PortStatsTournamentMatch *out = &line.match[match->order - 1];

            out->part = match->part;
            out->round = match->round;
            out->first = match->who[0];
            out->second = match->who[1];
            out->winner = match->who[match->winner];
            line.played++;
        }
    }
    port_StatsAddTournament(&line);
}

/* A member's turn: the room's match it is in noted, its word that it
 * plays the board's, and the board asked for. */
static void port_RtMemberTurn(const PortRoomView *view) {
    u32 now = port_NetClock();
    s32 i;

    if (view->match.number != 0 &&
        (view->match.a == view->you || view->match.b == view->you) &&
        (u32)view->match.number > g_rt.seen_match) {
        g_rt.seen_match = (u32)view->match.number;
    }
    if (g_rt.on && !g_rt.done && !g_rt.is_host && view->host != g_rt.host) {
        /* the host may leave as soon as the last match is decided, before
         * this program read its last board (a loaded machine): the room's
         * last result, where it is the match the board named, is taken
         * here as the host took it */
        if (g_rt.pair[0] != PORT_ROOM_NOBODY &&
            (u32)view->last_number > g_rt.after &&
            ((view->last_a == g_rt.pair[0] && view->last_b == g_rt.pair[1]) ||
             (view->last_a == g_rt.pair[1] && view->last_b == g_rt.pair[0])) &&
            (view->last_winner == PORT_ROOM_WON_FIRST ||
             view->last_winner == PORT_ROOM_WON_SECOND)) {
            s32 won = view->last_winner == PORT_ROOM_WON_FIRST ? view->last_a
                                                               : view->last_b;

            g_rt.after = (u32)view->last_number;
            port_RtReport(port_RtSlotOf(won, view->last_a), FALSE);
        }
        if (!g_rt.done) {
            port_Log("room tournament: its host has left the room: the "
                     "tournament is over, unfinished\n");
            g_rt.on = FALSE;
        }
    }
    if (g_rt.on && !g_rt.done) {
        s32 want = (view->you == g_rt.pair[0] || view->you == g_rt.pair[1]) &&
                   g_rt.after >= g_rt.seen_match &&
                   (u32)view->last_number <= g_rt.after;

        if (want != view->plays) {
            port_RoomSetPlays(want);
        }
        g_rt.plays_held = TRUE;
    } else if (g_rt.plays_held) {
        port_RoomSetPlays(g_rt.plays_kept);
        g_rt.plays_held = FALSE;
    }
    if (g_rt.on && g_rt.done) {
        port_RtOver();
    }
    while (g_rt.on && g_rt.said < g_rt.results) {
        g_rt.said++;
        if (!g_rt.is_host) {
            port_Log("room tournament: %d of its matches decided\n", g_rt.said);
        }
    }
    if (g_rt.is_host) {
        return;
    }
    /* the board: the host's match beside the room's that nobody joins */
    for (i = 0; i < view->side_count; i++) {
        const PortRoomMatch *side = &view->sides[i];
        u8 out[WATCH_BYTES];
        char code[CODE_FIELD];
        u8 token[TOKEN_BYTES];
        u32 at;

        if (side->a != view->host || side->b != PORT_ROOM_NOBODY ||
            now - g_rt.asked_at < ASK_EVERY_MS || !port_RoomCode(code, token)) {
            continue;
        }
        g_rt.asked_at = now;
        port_RtCopy(g_rt.asked_code, sizeof(g_rt.asked_code), side->code);
        for (at = 0; at < sizeof(out); at++) {
            out[at] = 0;
        }
        port_NetHeader(out, KIND_WATCH, 0, 0);
        for (at = 0; at < CODE_FIELD; at++) {
            out[WATCH_ROOM + at] = (u8)code[at];
            out[WATCH_MATCH + at] =
                at < PORT_ROOM_CODE_BYTES ? (u8)side->code[at] : 0;
        }
        for (at = 0; at < TOKEN_BYTES; at++) {
            out[WATCH_TOKEN + at] = token[at];
        }
        port_NetPutWord(out + WATCH_START, NO_START);
        port_NetPutWord(out + WATCH_WANTS, NO_START);
        port_RoomSend(out, sizeof(out));
        break;
    }
}

/* Each turn of the room's (room.c). */
void port_RoomTournamentTick(void) {
    PortRoomView view;

    port_RoomGet(&view);
    if (!view.in_room) {
        return;
    }
    if (g_rt.on && g_rt.is_host) {
        if (view.host != view.you) {
            port_Log("room tournament: this program is no longer the room's "
                     "host: the tournament ends\n");
            g_rt.on = FALSE;
        } else {
            port_RtHostTurn(&view);
        }
    }
    port_RtMemberTurn(&view);
}

void port_RoomTournamentForget(void) {
    u32 i;

    for (i = 0; i < sizeof(g_rt); i++) {
        ((u8 *)&g_rt)[i] = 0;
    }
}

/* ---- the host's begin and end ---- */

/* --room-tournament's words: port_TournamentParse's, where `@Name` is a
 * member of the room. */
s32 port_RoomTournamentParse(const char *spec, PortTournamentSetup *out) {
    char words[WHY_BYTES];
    u32 at = 0;
    s32 i;

    for (i = 0; spec[i] != 0 && at + 4 < sizeof(words); i++) {
        if (spec[i] == '@' && (i == 0 || spec[i - 1] == ',')) {
            /* a member: as the PC's first pad, its name after */
            words[at++] = 'p';
            words[at++] = '1';
            words[at++] = '/';
            continue;
        }
        words[at++] = spec[i];
    }
    words[at] = 0;
    if (!port_TournamentParse(words, out)) {
        return FALSE;
    }
    for (i = 0; i < out->count; i++) {
        if (out->entrant[i].pad < PORT_TOURNAMENT_PADS) {
            if (out->entrant[i].name[0] == 0) {
                return FALSE;
            }
            out->entrant[i].pad = PORT_ROOM_TOURNAMENT_MEMBER;
        }
    }
    return TRUE;
}

/* The member of the room named `name` (letters alike in either case). */
static s32 port_RtMemberNamed(const PortRoomView *view, const char *name) {
    s32 i;

    for (i = 0; i < view->count; i++) {
        const char *theirs = view->members[i].player;
        u32 at;

        for (at = 0; theirs[at] != 0 && name[at] != 0; at++) {
            char mine = theirs[at] >= 'a' && theirs[at] <= 'z'
                            ? (char)(theirs[at] - 'a' + 'A')
                            : theirs[at];
            char asked = name[at] >= 'a' && name[at] <= 'z'
                             ? (char)(name[at] - 'a' + 'A')
                             : name[at];

            if (mine != asked) {
                break;
            }
        }
        if (theirs[at] == 0 && name[at] == 0) {
            return view->members[i].id;
        }
    }
    return PORT_ROOM_NOBODY;
}

/* The room's host begins a tournament (port_room.h): its members found by
 * name, the seeds drawn, nobody in the rotation until the board says who. */
s32 port_RoomTournamentBegin(
    const PortTournamentSetup *setup, char *why, u32 room) {
    PortRoomView view;
    PortDate date;
    s32 i;
    s32 j;

    port_RoomGet(&view);
    if (!view.in_room || view.host != view.you) {
        port_RtText(why, room, "Only the room's host can begin a tournament.");
        return 0;
    }
    if (g_rt.on && !g_rt.done) {
        port_RtText(why, room, "A tournament is under way in the room.");
        return 0;
    }
    if (view.match.number != 0) {
        port_RtText(
            why, room, "The room's match must end before a tournament begins.");
        return 0;
    }
    if (setup->count < PORT_BRACKET_LEAST || setup->count > PORT_BRACKET_MOST) {
        port_RtText(why, room, "A tournament has two to eight entrants.");
        return 0;
    }
    for (i = 0; i < (s32)sizeof(g_rt.member); i++) {
        g_rt.member[i] = PORT_ROOM_NOBODY;
    }
    for (i = 0; i < setup->count; i++) {
        if (setup->entrant[i].pad != PORT_ROOM_TOURNAMENT_MEMBER) {
            continue;
        }
        g_rt.member[i] = (u8)port_RtMemberNamed(&view, setup->entrant[i].name);
        if (g_rt.member[i] == PORT_ROOM_NOBODY) {
            port_RtText(
                why, room, "%s is not in the room.", setup->entrant[i].name);
            return 0;
        }
        for (j = 0; j < i; j++) {
            if (g_rt.member[j] == g_rt.member[i]) {
                port_RtText(why, room, "%s is an entrant twice.",
                            setup->entrant[i].name);
                return 0;
            }
        }
    }
    g_rt.on = TRUE;
    g_rt.done = FALSE;
    g_rt.is_host = TRUE;
    g_rt.host = view.you;
    g_rt.number++;
    g_rt.version = 0;
    g_rt.setup = *setup;
    g_rt.setup.seeding = PORT_TOURNAMENT_SEED_ORDER;
    port_BracketShuffle(g_rt.seeds, setup->count, setup->seed);
    if (setup->seeding == PORT_TOURNAMENT_SEED_ORDER) {
        for (i = 0; i < setup->count; i++) {
            g_rt.seeds[i] = (u8)i;
        }
    }
    g_rt.results = 0;
    g_rt.walkovers = 0;
    g_rt.gone = 0;
    g_rt.pair[0] = g_rt.pair[1] = PORT_ROOM_NOBODY;
    g_rt.after = (u32)view.last_number;
    g_rt.seen_match = 0;
    g_rt.has_board = FALSE;
    g_rt.closing = 0;
    g_rt.done_at = 0;
    g_rt.opened_at = 0;
    g_rt.rule_kept = view.rule;
    g_rt.rule_set = FALSE;
    g_rt.ready = FALSE;
    g_rt.plays_kept = view.plays;
    g_rt.plays_held = FALSE;
    g_rt.recorded = FALSE;
    g_rt.said = 0;
    port_HostDate(&date);
    port_RtText(
        g_rt.date, sizeof(g_rt.date), "%04d-%02d-%02dT%02d:%02d:%02d",
        date.year, date.month, date.day, date.hour, date.minute, date.second);
    port_RtKeepNames();
    port_RtRebuild();
    /* nobody in the rotation until the board says who */
    port_RoomSetRule(PORT_ROOM_RULE_OFF);
    port_Log("room tournament: begun by this program, the room's host: %d "
             "entrants, %s elimination, %s %d, seeds:",
             setup->count, setup->is_double ? "double" : "single",
             setup->rule == PORT_MATCH_STOCK ? "stock" : "first_to",
             setup->rule_count);
    for (i = 0; i < setup->count; i++) {
        char name[NAME_BYTES];

        port_RtName(g_rt.seeds[i], name, sizeof(name));
        port_Log(" %d. %s", i + 1, name);
    }
    port_Log("\n");
    port_RtChanged();
    return 1;
}

void port_RoomTournamentEnd(void) {
    if (!g_rt.on || !g_rt.is_host) {
        return;
    }
    port_Log("room tournament: ended by its host\n");
    g_rt.done = TRUE;
    port_RtChanged();
}

/* The tournament as this member has it (port_room.h). */
void port_RoomTournamentGet(PortRoomTournamentView *out) {
    u32 i;

    for (i = 0; i < sizeof(*out); i++) {
        ((u8 *)out)[i] = 0;
    }
    out->on = g_rt.on;
    out->done = g_rt.done;
    out->is_host = g_rt.is_host;
    out->host = g_rt.host;
    out->number = g_rt.number;
    out->setup = g_rt.setup;
    for (i = 0; i < PORT_BRACKET_MOST; i++) {
        out->member[i] = g_rt.member[i];
    }
    out->gone = g_rt.gone;
    out->walkovers = g_rt.walkovers;
    out->results = g_rt.results;
    out->bracket = g_rt.bracket;
    out->next = g_rt.done ? -1 : port_BracketNext(&g_rt.bracket);
    out->pair[0] = g_rt.pair[0];
    out->pair[1] = g_rt.pair[1];
    out->finished =
        g_rt.done &&
        (!g_rt.is_host ||
         (g_rt.done_at != 0 && port_NetClock() - g_rt.done_at >= DONE_KEPT_MS));
}

/* ---- a match of it, as a session (online.c) ---- */

#define MATCH_COUNT_SHIFT 8

u32 port_RoomTournamentRule(void) {
    return port_MatchBitsTeams(
        (u32)g_rt.setup.rule |
            (u32)(g_rt.setup.rule_count > 0 ? g_rt.setup.rule_count : 1)
                << MATCH_COUNT_SHIFT,
        PORT_TEAMS_ONE);
}

/* The room's match `next` is the tournament's next (port_room.h): its
 * form, the session's host's entrant first, each a pad or the CPU. */
s32 port_RoomTournamentMatch(
    const PortRoomNext *next, PortTournamentForm *form) {
    const PortRoomMatch *match = &next->match;
    s32 index = port_BracketNext(&g_rt.bracket);
    const PortBracketMatch *bracket_match;
    s32 slot[2];
    s32 k;
    u32 i;

    for (i = 0; i < sizeof(*form); i++) {
        ((u8 *)form)[i] = 0;
    }
    if (!g_rt.on || g_rt.done || next->side || index < 0 ||
        next->players != 2 ||
        !((match->a == g_rt.pair[0] && match->b == g_rt.pair[1]) ||
          (match->a == g_rt.pair[1] && match->b == g_rt.pair[0]))) {
        return FALSE;
    }
    if ((u32)match->number > g_rt.seen_match) {
        g_rt.seen_match = (u32)match->number;
    }
    bracket_match = &g_rt.bracket.match[index];
    slot[0] = port_RtSlotOf(match->a, match->a);
    slot[1] = port_RtSlotOf(match->b, match->a);
    if (slot[1] == slot[0]) {
        slot[1] = 1 - slot[0];
    }
    form->on = TRUE;
    form->rule = g_rt.setup.rule;
    form->rule_count = g_rt.setup.rule_count > 0 ? g_rt.setup.rule_count : 1;
    for (k = 0; k < 2; k++) {
        s32 entrant = bracket_match->who[slot[k]];
        const PortTournamentEntrant *of;

        if (entrant >= PORT_BRACKET_MOST) {
            return FALSE;
        }
        of = &g_rt.setup.entrant[entrant];

        form->pad[k] = port_RtIsMember(entrant) ? (u8)k : PORT_TOURNAMENT_CPU;
        form->cpu[k] = of->cpu;
        form->fighter[k] =
            port_RtIsMember(entrant) ? PORT_TOURNAMENT_RANDOM : of->fighter;
        form->weapon[k] =
            port_RtIsMember(entrant) ? PORT_TOURNAMENT_RANDOM : of->weapon;
    }
    form->dice =
        g_rt.setup.seed + DICE_STEP * (u32)(index + 1) + (u32)match->number;
    {
        char first[NAME_BYTES];
        char second[NAME_BYTES];

        port_RtName(bracket_match->who[slot[0]], first, sizeof(first));
        port_RtName(bracket_match->who[slot[1]], second, sizeof(second));
        port_Log("room tournament: room match %d is its %s, %s vs %s\n",
                 match->number, port_BracketRoundName(&g_rt.bracket, index),
                 first, second);
    }
    return TRUE;
}

const char *port_RoomTournamentDate(void) { return g_rt.on ? g_rt.date : ""; }
