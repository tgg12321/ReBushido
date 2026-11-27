/* What a local tournament keeps outside the game's state
 * (port_tournament.h): the window's asks (a tournament begun, the next
 * match, the tournament left), each a count and the input it is for,
 * which the game takes in with that input (tournament.c,
 * port_TournamentTakeInput); the log's lines and the record's line
 * written at the host's boundary when it is over; --tournament's words
 * and a test's own play (--tournament-auto). Outside the state, so that
 * nothing a rollback or run-ahead puts back asks twice or writes the
 * record twice. */
#include "common.h"
#include "port_host.h"
#include "bb2.h"

/* The main menu's top page (menu_screen.c, enum MenuPage). */
#define MENU_PAGE_TOP 1
#define WORD_BYTES 32
#define DATE_BYTES 24
#define DECIMAL 10

/* The fighters a CPU entrant may be given, and their names as
 * --tournament takes them (the select screen's, with blades). */
static const u8 g_port_tournament_choosable[] = {
    CHARACTER_KANNUKI,     CHARACTER_TATSUMI,      CHARACTER_MIKADO,
    CHARACTER_UTSUSEMI,    CHARACTER_RED_SHADOW,   CHARACTER_SAZANKA,
    CHARACTER_SUMINAGASHI, CHARACTER_NIGHTSTALKER, CHARACTER_MATSUMUSHI,
    CHARACTER_GENGORO,     CHARACTER_KAUN,         CHARACTER_JO,
    CHARACTER_HONGOU,      CHARACTER_HIGHWAYMAN,   CHARACTER_CHIHIRO,
    CHARACTER_UTAMARU,     CHARACTER_TONY_UMEDA,   CHARACTER_ISOHACHI,
};
static const char * const g_port_tournament_fighter_words[] = {
    "kannuki", "tatsumi",     "mikado",       "utsusemi",   "redshadow",
    "sazanka", "suminagashi", "nightstalker", "matsumushi", "gengoro",
    "kaun",    "jo",          "hongou",       "highwayman", "chihiro",
    "utamaru", "tonyumeda",   "isohachi",
};
static const char * const g_port_tournament_weapon_words[] = {
    "katana", "longsword", "nodachi", "broadsword", "naginata", "yari"};
static const char * const g_port_tournament_cpu_words[] = {
    "original", "novice", "adept", "veteran", "master"};
#define COUNT_OF(array) ((s32)(sizeof(array) / sizeof((array)[0])))

static struct {
    /* each kind's asks (PORT_TOURNAMENT_ASK_...): a count, and the
     * number of the input (g_port_input_count) the last is taken at */
    u16 asks[PORT_TOURNAMENT_ASKS];
    u32 ask_at[PORT_TOURNAMENT_ASKS];
    PortTournamentSetup setup;
    /* the tournament was on at the last boundary (its end said) */
    s32 was_on;
    /* a test's: boundaries at the main menu before the next match, and
     * the program ended when it is over */
    s32 auto_after;
    s32 auto_quit;
    s32 waited;
    /* the tournament the log and the record have followed: its number,
     * the matches said, whether its line is written */
    u16 number;
    u8 said;
    s32 recorded;
    char date[DATE_BYTES];
} g_port_tournament_ask;

static u32 port_AskText(char *out, u32 room, const char *format, ...) {
    PortArgs args;
    u32 length;

    PORT_ARGS_START(args, format);
    length = port_FormatText(out, room, format, args);
    PORT_ARGS_END(args);
    return length;
}

const u8 *port_TournamentFighters(s32 *count) {
    *count = COUNT_OF(g_port_tournament_choosable);
    return g_port_tournament_choosable;
}

/* An ask of `kind`, for the next input the game takes. */
static void port_TournamentAsk(s32 kind) {
    g_port_tournament_ask.ask_at[kind] = g_port_input_count;
    g_port_tournament_ask.asks[kind]++;
}

s32 port_TournamentAsked(s32 kind, u16 *count) {
    if (kind < 0 || kind >= PORT_TOURNAMENT_ASKS ||
        g_port_tournament_ask.asks[kind] == 0 ||
        g_port_input_count < g_port_tournament_ask.ask_at[kind]) {
        return FALSE;
    }
    *count = g_port_tournament_ask.asks[kind];
    return TRUE;
}

const PortTournamentSetup *port_TournamentAskedSetup(void) {
    return &g_port_tournament_ask.setup;
}

void port_AskTournamentStart(const PortTournamentSetup *setup) {
    g_port_tournament_ask.setup = *setup;
    port_TournamentAsk(PORT_TOURNAMENT_ASK_BEGIN);
}

void port_AskTournamentPlay(void) {
    port_TournamentAsk(PORT_TOURNAMENT_ASK_PLAY);
}

void port_AskTournamentLeave(void) {
    port_TournamentAsk(PORT_TOURNAMENT_ASK_LEAVE);
}

void port_SetTournamentAuto(s32 boundaries) {
    g_port_tournament_ask.auto_after = boundaries;
}

void port_SetTournamentQuit(void) { g_port_tournament_ask.auto_quit = TRUE; }

const char *port_TournamentDate(void) {
    return port_TournamentIsOn() ? g_port_tournament_ask.date : "";
}

void port_TournamentName(
    const PortTournamentEntrant *entrant, s32 index, char *out, u32 room) {
    if (entrant->name[0] != 0) {
        port_AskText(out, room, "%s", entrant->name);
    } else if (entrant->pad < PORT_TOURNAMENT_PADS) {
        port_AskText(out, room, "Player %d", entrant->pad + 1);
    } else {
        port_AskText(out, room, "CPU %d", index + 1);
    }
}

/* ---- --tournament's words ---- */

/* `text` up to `end` as a key: lower case, letters and digits alone. */
static void port_TournamentKey(const char *text, const char *end, char *out) {
    u32 length = 0;

    for (; text != end && *text != 0 && length + 1 < WORD_BYTES; text++) {
        char letter = *text;

        if (letter >= 'A' && letter <= 'Z') {
            letter = (char)(letter - 'A' + 'a');
        }
        if ((letter >= 'a' && letter <= 'z') ||
            (letter >= '0' && letter <= '9')) {
            out[length++] = letter;
        }
    }
    out[length] = 0;
}

/* Which of `words` the key is, or a number among them; -1. */
static s32 port_TournamentWord(
    const char *key, const char * const *words, s32 count) {
    s32 number = 0;
    s32 i;

    for (i = 0; i < count; i++) {
        if (port_TextsEqual(key, words[i])) {
            return i;
        }
    }
    for (i = 0; key[i] >= '0' && key[i] <= '9'; i++) {
        number = number * DECIMAL + (key[i] - '0');
    }
    return i > 0 && key[i] == 0 && number < count ? number : -1;
}

/* A field of an entrant's word (`cpu:4:kannuki:katana/Name`): its start
 * and end, from `at`; the next field's start after it. */
static const char *port_TournamentField(
    const char *at, const char *end, const char **field_end) {
    const char *stop = at;

    while (stop != end && *stop != ':' && *stop != '/') {
        stop++;
    }
    *field_end = stop;
    return stop != end && *stop == ':' ? stop + 1 : stop;
}

/* One entrant's word: `pN` (the PC's pad N, 1 to 4) or `cpu:LEVEL`,
 * `cpu:LEVEL:FIGHTER` or `cpu:LEVEL:FIGHTER:WEAPON`, each perhaps with
 * `/Name` after it (a `_` of the name is a space). */
static s32 port_TournamentEntrantWord(
    const char *word, const char *end, PortTournamentEntrant *out) {
    char key[WORD_BYTES];
    const char *field_end;
    const char *at;
    const char *slash = word;
    u32 length = 0;
    s32 value;

    out->name[0] = 0;
    out->pad = PORT_TOURNAMENT_CPU;
    out->cpu = PORT_CPU_ADEPT;
    out->fighter = PORT_TOURNAMENT_RANDOM;
    out->weapon = PORT_TOURNAMENT_RANDOM;
    while (slash != end && *slash != '/') {
        slash++;
    }
    if (slash != end) {
        for (at = slash + 1; at != end && length + 1 < sizeof(out->name);
             at++) {
            out->name[length++] = *at == '_' ? ' ' : *at;
        }
        out->name[length] = 0;
    }
    at = port_TournamentField(word, slash, &field_end);
    port_TournamentKey(word, field_end, key);
    if (key[0] == 'p' && key[1] >= '1' && key[1] <= '4' && key[2] == 0) {
        out->pad = (u8)(key[1] - '1');
        return at == slash;
    }
    if (!port_TextsEqual(key, "cpu")) {
        return FALSE;
    }
    if (at != slash) {
        const char *field = at;

        at = port_TournamentField(field, slash, &field_end);
        port_TournamentKey(field, field_end, key);
        value = port_TournamentWord(key, g_port_tournament_cpu_words,
                                    COUNT_OF(g_port_tournament_cpu_words));
        if (value < 0) {
            return FALSE;
        }
        out->cpu = (u8)value;
    }
    if (at != slash) {
        const char *field = at;

        at = port_TournamentField(field, slash, &field_end);
        port_TournamentKey(field, field_end, key);
        if (!port_TextsEqual(key, "random")) {
            value = port_TournamentWord(
                key, g_port_tournament_fighter_words,
                COUNT_OF(g_port_tournament_fighter_words));
            if (value < 0) {
                return FALSE;
            }
            out->fighter = g_port_tournament_choosable[value];
        }
    }
    if (at != slash) {
        const char *field = at;

        at = port_TournamentField(field, slash, &field_end);
        port_TournamentKey(field, field_end, key);
        if (!port_TextsEqual(key, "random")) {
            value = port_TournamentWord(
                key, g_port_tournament_weapon_words,
                COUNT_OF(g_port_tournament_weapon_words));
            if (value < 0) {
                return FALSE;
            }
            out->weapon = (u8)value;
        }
    }
    return at == slash;
}

/* The words of a setup, split by commas: `single` or `double`, `random`
 * or `order` (the seeding), `first_to:N` or `stock:N` (`one`: one
 * fight), `seed:N`, and two to eight entrants' words. */
s32 port_TournamentParse(const char *spec, PortTournamentSetup *out) {
    const char *at = spec;
    u32 i;

    for (i = 0; i < sizeof(*out); i++) {
        ((u8 *)out)[i] = 0;
    }
    out->rule = PORT_MATCH_FIRST_TO;
    out->rule_count = 1;
    out->seeding = PORT_TOURNAMENT_SEED_RANDOM;
    while (*at != 0) {
        const char *end = at;
        char key[WORD_BYTES];
        s32 number = 0;
        const char *colon;

        while (*end != 0 && *end != ',') {
            end++;
        }
        for (colon = at; colon != end && *colon != ':'; colon++) {
        }
        port_TournamentKey(at, colon, key);
        if (colon != end) {
            const char *digit;

            for (digit = colon + 1; digit != end; digit++) {
                if (*digit >= '0' && *digit <= '9') {
                    number = number * DECIMAL + (*digit - '0');
                }
            }
        }
        if (port_TextsEqual(key, "single") || port_TextsEqual(key, "double")) {
            out->is_double = key[0] == 'd';
        } else if (
            port_TextsEqual(key, "random") || port_TextsEqual(key, "order")) {
            out->seeding = key[0] == 'o' ? PORT_TOURNAMENT_SEED_ORDER
                                         : PORT_TOURNAMENT_SEED_RANDOM;
        } else if (
            port_TextsEqual(key, "firstto") || port_TextsEqual(key, "stock")) {
            out->rule = key[0] == 's' ? PORT_MATCH_STOCK : PORT_MATCH_FIRST_TO;
            out->rule_count =
                (u8)(number < 1                       ? 1
                     : number > PORT_MATCH_COUNT_MOST ? PORT_MATCH_COUNT_MOST
                                                      : number);
        } else if (port_TextsEqual(key, "one")) {
            out->rule = PORT_MATCH_FIRST_TO;
            out->rule_count = 1;
        } else if (port_TextsEqual(key, "seed")) {
            out->seed = (u32)number;
        } else {
            if (out->count >= PORT_BRACKET_MOST ||
                !port_TournamentEntrantWord(
                    at, end, &out->entrant[out->count])) {
                return FALSE;
            }
            out->count++;
        }
        at = *end == ',' ? end + 1 : end;
    }
    return out->count >= PORT_BRACKET_LEAST;
}

/* --tournament's words begun at the first boundary, or said wrong. */
s32 port_AskTournamentWords(const char *spec) {
    PortTournamentSetup setup;

    if (!port_TournamentParse(spec, &setup)) {
        port_Log("--tournament: %s is not a tournament: single or double, "
                 "random or order, one, first_to:N or stock:N, seed:N, and "
                 "two to eight of pN or cpu:LEVEL[:FIGHTER[:WEAPON]], each "
                 "perhaps /Name\n",
                 spec);
        return FALSE;
    }
    port_AskTournamentStart(&setup);
    return TRUE;
}

/* ---- the record and the log ---- */

static void port_TournamentNameOf(s32 entrant, char *out, u32 room) {
    const PortTournament *tournament = &g_port_tournament;

    if (entrant < 0 || entrant >= tournament->setup.count) {
        port_AskText(out, room, "nobody");
        return;
    }
    port_TournamentName(
        &tournament->setup.entrant[entrant], entrant, out, room);
}

/* The matches decided since the log last said, said. */
static void port_TournamentSayMatches(void) {
    const PortTournament *tournament = &g_port_tournament;
    const PortBracket *bracket = &tournament->bracket;

    while (g_port_tournament_ask.said < bracket->played) {
        u8 order = (u8)(g_port_tournament_ask.said + 1);
        s32 i;

        for (i = 0; i < bracket->match_count; i++) {
            const PortBracketMatch *match = &bracket->match[i];

            if (match->order == order) {
                char winner[PORT_STATS_NAME_BYTES];
                char loser[PORT_STATS_NAME_BYTES];

                port_TournamentNameOf(
                    match->who[match->winner], winner, sizeof(winner));
                port_TournamentNameOf(
                    match->who[1 - match->winner], loser, sizeof(loser));
                port_Log("tournament: match %d of %d (%s, %d): %s beat %s\n",
                         order, port_BracketMost(bracket),
                         port_BracketRoundName(bracket, i), i, winner, loser);
                if ((tournament->judged >> i) & 1) {
                    port_Log("tournament: match %d judged: three minutes and "
                             "neither fell\n",
                             order);
                }
            }
        }
        g_port_tournament_ask.said = order;
    }
}

/* The tournament over: its line in the record, and the places said. */
static void port_TournamentRecord(void) {
    const PortTournament *tournament = &g_port_tournament;
    const PortBracket *bracket = &tournament->bracket;
    PortStatsTournament line;
    PortDate date;
    s32 i;
    u32 byte;

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
    port_AskText(
        line.begun, sizeof(line.begun), "%s", g_port_tournament_ask.date);
    line.count = tournament->setup.count;
    line.is_double = tournament->setup.is_double;
    line.seeding = tournament->setup.seeding;
    line.rule = tournament->setup.rule;
    line.rule_count = tournament->setup.rule_count;
    line.champion = bracket->champion;
    for (i = 0; i < tournament->setup.count; i++) {
        const PortTournamentEntrant *entrant = &tournament->setup.entrant[i];
        s32 seed;

        port_TournamentNameOf(
            i, line.entrant[i].name, sizeof(line.entrant[i].name));
        line.entrant[i].cpu =
            (s8)(entrant->pad < PORT_TOURNAMENT_PADS ? PORT_STATS_PERSON
                                                     : entrant->cpu);
        line.entrant[i].pad = entrant->pad;
        line.entrant[i].place = (u8)port_BracketPlace(bracket, i);
        line.entrant[i].losses = (u8)port_BracketLosses(bracket, i);
        for (seed = 0; seed < bracket->count; seed++) {
            if (bracket->seeds[seed] == i) {
                line.entrant[i].seed = (u8)(seed + 1);
            }
        }
    }
    for (i = 0; i < bracket->match_count; i++) {
        const PortBracketMatch *match = &bracket->match[i];

        if (match->played && match->order >= 1 &&
            match->order <= PORT_BRACKET_MATCHES) {
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
    for (i = 0; i < tournament->setup.count; i++) {
        char name[PORT_STATS_NAME_BYTES];

        port_TournamentNameOf(i, name, sizeof(name));
        port_Log("tournament: place %d: %s (%d lost)\n",
                 port_BracketPlace(bracket, i), name,
                 port_BracketLosses(bracket, i));
    }
}

/* A tournament begun here: its date, the log. */
static void port_TournamentBegun(void) {
    const PortTournament *tournament = &g_port_tournament;
    PortDate date;
    s32 seed;

    port_HostDate(&date);
    port_AskText(g_port_tournament_ask.date, sizeof(g_port_tournament_ask.date),
                 "%04d-%02d-%02dT%02d:%02d:%02d", date.year, date.month,
                 date.day, date.hour, date.minute, date.second);
    g_port_tournament_ask.number = tournament->number;
    g_port_tournament_ask.said = 0;
    g_port_tournament_ask.recorded = FALSE;
    g_port_tournament_ask.waited = 0;
    port_Log("tournament: begun, %d entrants, %s elimination, %s %d, seeds:",
             tournament->setup.count,
             tournament->setup.is_double ? "double" : "single",
             tournament->setup.rule == PORT_MATCH_STOCK ? "stock" : "first_to",
             tournament->setup.rule_count);
    for (seed = 0; seed < tournament->bracket.count; seed++) {
        char name[PORT_STATS_NAME_BYTES];

        port_TournamentNameOf(
            tournament->bracket.seeds[seed], name, sizeof(name));
        port_Log(" %d. %s", seed + 1, name);
    }
    port_Log("\n");
}

/* Between two steps (port_tournament.h): a tournament begun or ended
 * said, the matches said, the record written once, a test's own play
 * asked. */
void port_TournamentBoundary(void) {
    PortTournament *tournament = &g_port_tournament;
    s32 at_top =
        g_sys_screen == SCREEN_MENU_UPDATE && g_menu_page == MENU_PAGE_TOP;

    if (g_port_tournament_ask.was_on && !port_TournamentIsOn()) {
        port_Log("tournament: %s\n", g_port_tournament_ask.recorded
                                         ? "over, and put away"
                                         : "left, not finished: nothing kept");
    }
    g_port_tournament_ask.was_on = port_TournamentIsOn();
    if (!port_TournamentIsOn()) {
        return;
    }
    if (tournament->number != g_port_tournament_ask.number) {
        port_TournamentBegun();
    }
    port_TournamentSayMatches();
    if (tournament->phase == PORT_TOURNAMENT_DONE &&
        !g_port_tournament_ask.recorded) {
        char name[PORT_STATS_NAME_BYTES];

        g_port_tournament_ask.recorded = TRUE;
        port_TournamentNameOf(tournament->bracket.champion, name, sizeof(name));
        port_Log("tournament: over after %d matches; the champion: %s\n",
                 tournament->bracket.played, name);
        port_TournamentRecord();
    }
    /* a test's own play: the next match, a while after the bracket is up */
    if (g_port_tournament_ask.auto_after > 0 && at_top &&
        (tournament->phase == PORT_TOURNAMENT_BETWEEN ||
         tournament->phase == PORT_TOURNAMENT_DONE)) {
        if (++g_port_tournament_ask.waited >=
            g_port_tournament_ask.auto_after) {
            g_port_tournament_ask.waited = 0;
            if (tournament->phase == PORT_TOURNAMENT_BETWEEN) {
                port_AskTournamentPlay();
            } else if (g_port_tournament_ask.auto_quit) {
                port_Log("tournament: --tournament-quit: the program ends\n");
                port_Quit(PORT_EXIT_OK);
            }
        }
    } else if (!at_top) {
        g_port_tournament_ask.waited = 0;
    }
}
