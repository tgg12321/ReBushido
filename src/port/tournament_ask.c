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
#define DATE_BYTES PORT_TOURNAMENT_DATE_BYTES
#define DECIMAL 10
/* the kept tournament's file (port_SetTournamentFile): its first line,
 * the room for its path and for its text, and a test's frames into the
 * fight of the match it ends in (--tournament-quit-in) */
#define KEPT_HEAD "rebushido-tournament"
#define KEPT_VERSION 1
#define KEPT_PATH_BYTES 1024
#define KEPT_TEXT_BYTES 4096
#define KEPT_LINE_BYTES 160
#define QUIT_IN_FRAMES 300
#define HEX_LOW 0xF
#define HEX_BITS 4
#define BYTE_MOST 0xFF
/* a name's byte as %XX: three bytes, and the one after them */
#define ESCAPE_BYTES 3
#define ESCAPE_ROOM 4

/* An entrant's line's numbers, before its name. */
enum {
    KEPT_PAD,
    KEPT_CPU,
    KEPT_FIGHTER,
    KEPT_WEAPON,
    KEPT_LAST_FIGHTER,
    KEPT_LAST_WEAPON,
    KEPT_FIELDS
};

/* The file's keys, in the order of g_kept_keys. */
enum {
    KEY_COUNT,
    KEY_DOUBLE,
    KEY_SEEDING,
    KEY_RULE,
    KEY_RULE_COUNT,
    KEY_SEED,
    KEY_DICE,
    KEY_JUDGED,
    KEY_RESULT,
    KEY_ENTRANT,
    KEY_DATE,
    KEYS
};

static const char * const g_kept_keys[KEYS] = {
    "count=", "double=", "seeding=", "rule=",    "rule_count=", "seed=",
    "dice=",  "judged=", "result=",  "entrant=", "date=",
};

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
    /* a test's: inputs at the main menu before the next match, and the
     * program ended when it is over; the input the main menu came up at
     * (the game's inputs, not boundaries: a rollback that runs frames
     * again brings the match no sooner), and whether it has been asked */
    s32 auto_after;
    s32 auto_quit;
    u32 top_from;
    s32 top_seen;
    s32 auto_asked;
    /* the tournament the log and the record have followed: its number,
     * the matches said, whether its line is written */
    u16 number;
    u8 said;
    s32 recorded;
    char date[DATE_BYTES];
    /* the tournament kept between runs: its file, the setup of the last
     * resume asked, and the tournament and matches it was last written
     * for (so it is written once a match) */
    char kept_path[KEPT_PATH_BYTES];
    PortTournamentKept kept;
    u16 kept_number;
    u8 kept_decided;
    s32 kept_written;
    /* a test's: the program ended in the fight of the match after
     * `quit_in` decided (one more than it, 0 for none), the frames of it
     * seen; and the kept one resumed at the first boundary */
    s32 quit_in;
    s32 quit_frames;
    s32 resume_at_start;
    /* the match whose sides the log has said (its tournament's number and
     * the matches before it), 0 none */
    u32 said_begun;
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

const PortTournamentKept *port_TournamentAskedKept(void) {
    return &g_port_tournament_ask.kept;
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

void port_SetTournamentQuitIn(s32 decided) {
    g_port_tournament_ask.quit_in = decided + 1;
}

void port_SetTournamentResumeAtStart(void) {
    g_port_tournament_ask.resume_at_start = TRUE;
}

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
 * or `order` (the seeding), `first_to:N`, `stock:N` or `crew:N` (`one`:
 * one fight), `seed:N`, and two to eight entrants' words. */
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
        } else if (port_TextsEqual(key, "crew")) {
            /* a crew of 2 to PORT_TOURNAMENT_CREW_MOST, three if not said */
            out->rule = PORT_MATCH_CREW;
            out->rule_count =
                (u8)(number == 0                      ? 3
                     : number < PORT_MATCH_CREW_LEAST ? PORT_MATCH_CREW_LEAST
                     : number > PORT_TOURNAMENT_CREW_MOST
                         ? PORT_TOURNAMENT_CREW_MOST
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
                 "random or order, one, first_to:N, stock:N or crew:N, "
                 "seed:N, and "
                 "two to eight of pN or cpu:LEVEL[:FIGHTER[:WEAPON]], each "
                 "perhaps /Name\n",
                 spec);
        return FALSE;
    }
    port_AskTournamentStart(&setup);
    return TRUE;
}

/* ---- the tournament kept between runs (port_tournament.h) ---- */

void port_SetTournamentFile(const char *path) {
    port_AskText(g_port_tournament_ask.kept_path,
                 sizeof(g_port_tournament_ask.kept_path), "%s",
                 path != NULL ? path : "");
}

/* A name as the file has it: every byte not a letter, a digit, `-` or `_`
 * as %XX. */
static void port_KeptName(const char *name, char *out, u32 room) {
    static const char g_hex[] = "0123456789ABCDEF";
    u32 length = 0;

    for (; *name != 0 && length + ESCAPE_ROOM < room; name++) {
        u8 letter = (u8)*name;

        if ((letter >= 'a' && letter <= 'z') ||
            (letter >= 'A' && letter <= 'Z') ||
            (letter >= '0' && letter <= '9') || letter == '-' ||
            letter == '_') {
            out[length++] = (char)letter;
        } else {
            out[length++] = '%';
            out[length++] = g_hex[letter >> HEX_BITS];
            out[length++] = g_hex[letter & HEX_LOW];
        }
    }
    out[length] = 0;
}

/* The tournament as it is, written whole into PATH.part and renamed over
 * PATH, as the record is: the file is the last one or this one. */
static void port_TournamentKeep(void) {
    static char text[KEPT_TEXT_BYTES];
    const PortTournament *tournament = &g_port_tournament;
    const PortTournamentSetup *setup = &tournament->setup;
    const PortBracket *bracket = &tournament->bracket;
    char part[KEPT_PATH_BYTES + sizeof(".part")];
    u32 at;
    s32 order;
    s32 i;
    s32 file;
    s32 ok;

    at = port_AskText(
        text, sizeof(text),
        KEPT_HEAD " %d\ndate=%s\ncount=%d\ndouble=%d\n"
                  "seeding=%d\nrule=%d\nrule_count=%d\n"
                  "seed=%u\ndice=%u\njudged=%d\n",
        KEPT_VERSION, g_port_tournament_ask.date, setup->count,
        setup->is_double, setup->seeding, setup->rule, setup->rule_count,
        setup->seed, tournament->dice_kept, tournament->judged);
    for (i = 0; i < setup->count && at < sizeof(text) - KEPT_LINE_BYTES; i++) {
        const PortTournamentEntrant *entrant = &setup->entrant[i];
        char name[3 * PORT_TOURNAMENT_NAME_BYTES + 1];

        port_KeptName(entrant->name, name, sizeof(name));
        at += port_AskText(
            text + at, sizeof(text) - at, "entrant=%d:%d:%d:%d:%d:%d:%s\n",
            entrant->pad, entrant->cpu, entrant->fighter, entrant->weapon,
            tournament->last_fighter[i], tournament->last_weapon[i], name);
    }
    for (order = 1; order <= bracket->played; order++) {
        for (i = 0; i < bracket->match_count; i++) {
            const PortBracketMatch *match = &bracket->match[i];

            if (match->played && match->order == order &&
                at < sizeof(text) - KEPT_LINE_BYTES) {
                at += port_AskText(text + at, sizeof(text) - at,
                                   "result=%d:%d\n", i, match->winner);
            }
        }
    }
    port_AskText(
        part, sizeof(part), "%s.part", g_port_tournament_ask.kept_path);
    file = port_HostCreate(part);
    if (file < 0) {
        port_Log("tournament: %s cannot be written\n", part);
        return;
    }
    ok = port_HostAppend(file, text, at);
    port_HostClose(file);
    if (!ok || !port_HostRename(part, g_port_tournament_ask.kept_path)) {
        port_Log("tournament: %s cannot be written\n",
                 g_port_tournament_ask.kept_path);
        port_HostRemove(part);
        return;
    }
    g_port_tournament_ask.kept_written = TRUE;
    port_Log("tournament: kept in %s after %d matches\n",
             g_port_tournament_ask.kept_path, tournament->decided);
}

/* The kept tournament over or left: its file taken away. */
static void port_TournamentUnkeep(void) {
    if (g_port_tournament_ask.kept_path[0] != 0 &&
        g_port_tournament_ask.kept_written) {
        port_HostRemove(g_port_tournament_ask.kept_path);
        port_Log(
            "tournament: %s taken away\n", g_port_tournament_ask.kept_path);
    }
    g_port_tournament_ask.kept_written = FALSE;
}

/* A number of the file's: digits, from `at`; where they end. */
static const char *port_KeptNumber(const char *at, u32 *value) {
    *value = 0;
    while (*at >= '0' && *at <= '9') {
        *value = *value * DECIMAL + (u32)(*at - '0');
        at++;
    }
    return at;
}

/* A digit of %XX: its value, or -1. */
static s32 port_KeptHex(char digit) {
    return digit >= '0' && digit <= '9'   ? digit - '0'
           : digit >= 'A' && digit <= 'F' ? digit - 'A' + DECIMAL
           : digit >= 'a' && digit <= 'f' ? digit - 'a' + DECIMAL
                                          : -1;
}

/* An entrant's line: pad:cpu:fighter:weapon:last fighter:last weapon:name. */
static s32 port_KeptEntrant(const char *at, PortTournamentKept *kept) {
    PortTournamentEntrant *entrant;
    u32 field[KEPT_FIELDS];
    u32 length = 0;
    s32 i;

    if (kept->setup.count >= PORT_BRACKET_MOST) {
        return FALSE;
    }
    for (i = 0; i < KEPT_FIELDS; i++) {
        at = port_KeptNumber(at, &field[i]);
        if (*at != ':' || field[i] > BYTE_MOST) {
            return FALSE;
        }
        at++;
    }
    entrant = &kept->setup.entrant[kept->setup.count];
    entrant->pad = (u8)field[KEPT_PAD];
    entrant->cpu = (u8)field[KEPT_CPU];
    entrant->fighter = (u8)field[KEPT_FIGHTER];
    entrant->weapon = (u8)field[KEPT_WEAPON];
    if ((entrant->pad >= PORT_TOURNAMENT_PADS &&
         entrant->pad != PORT_TOURNAMENT_CPU) ||
        entrant->cpu > PORT_CPU_LEVELS) {
        return FALSE;
    }
    kept->last_fighter[kept->setup.count] = (u8)field[KEPT_LAST_FIGHTER];
    kept->last_weapon[kept->setup.count] = (u8)field[KEPT_LAST_WEAPON];
    while (*at != 0 && *at != '\n' && length + 1 < sizeof(entrant->name)) {
        if (*at == '%' && port_KeptHex(at[1]) >= 0 &&
            port_KeptHex(at[2]) >= 0) {
            entrant->name[length++] =
                (char)(port_KeptHex(at[1]) << HEX_BITS | port_KeptHex(at[2]));
            at += ESCAPE_BYTES;
        } else {
            entrant->name[length++] = *at++;
        }
    }
    entrant->name[length] = 0;
    kept->setup.count++;
    return TRUE;
}

/* One line of the file into `kept`: FALSE where it is not one. */
static s32 port_KeptLine(const char *line, PortTournamentKept *kept) {
    s32 key;
    u32 value;
    u32 slot;
    const char *at = line;

    for (key = 0; key < KEYS; key++) {
        const char *word = g_kept_keys[key];

        for (at = line; *word != 0 && *at == *word; word++, at++) {
        }
        if (*word == 0) {
            break;
        }
    }
    if (key == KEYS) {
        return FALSE;
    }
    if (key == KEY_ENTRANT) {
        return port_KeptEntrant(at, kept);
    }
    if (key == KEY_DATE) {
        u32 length = 0;

        while (*at != 0 && *at != '\n' && length + 1 < sizeof(kept->date)) {
            kept->date[length++] = *at++;
        }
        kept->date[length] = 0;
        return TRUE;
    }
    at = port_KeptNumber(at, &value);
    if (key == KEY_RESULT) {
        if (*at != ':' || kept->results >= PORT_BRACKET_MATCHES ||
            value >= PORT_BRACKET_MATCHES) {
            return FALSE;
        }
        at = port_KeptNumber(at + 1, &slot);
        if (slot > 1) {
            return FALSE;
        }
        kept->result[kept->results++] = (u8)(value << 1 | slot);
    } else if (key == KEY_COUNT) {
        /* (the entrants' lines count them) */
    } else if (key == KEY_DOUBLE) {
        kept->setup.is_double = (u8)(value != 0);
    } else if (key == KEY_SEEDING) {
        kept->setup.seeding = (u8)(value != 0);
    } else if (key == KEY_RULE) {
        kept->setup.rule = (u8)value;
    } else if (key == KEY_RULE_COUNT) {
        kept->setup.rule_count = (u8)value;
    } else if (key == KEY_SEED) {
        kept->setup.seed = value;
    } else if (key == KEY_DICE) {
        kept->dice = value;
    } else {
        kept->judged = (u16)value;
    }
    return *at == 0 || *at == '\n';
}

/* The kept file read (port_tournament.h): its first line this version's,
 * each line one of its keys, two entrants at least and a rule a
 * tournament has. */
s32 port_TournamentKeptRead(PortTournamentKept *kept) {
    static char text[KEPT_TEXT_BYTES];
    const char *path = g_port_tournament_ask.kept_path;
    char head[WORD_BYTES];
    const char *line;
    u32 size;
    u32 byte;
    s32 file;

    for (byte = 0; byte < sizeof(*kept); byte++) {
        ((u8 *)kept)[byte] = 0;
    }
    if (path[0] == 0) {
        return FALSE;
    }
    file = port_HostOpen(path);
    if (file < 0) {
        return FALSE;
    }
    size = port_HostFileSize(file);
    if (size >= sizeof(text)) {
        size = sizeof(text) - 1;
    }
    size = port_HostReadAt(file, 0, text, size);
    port_HostClose(file);
    text[size] = 0;
    port_AskText(head, sizeof(head), KEPT_HEAD " %d\n", KEPT_VERSION);
    for (byte = 0; head[byte] != 0 && text[byte] == head[byte]; byte++) {
    }
    if (head[byte] != 0) {
        return FALSE;
    }
    for (line = text + byte; *line != 0;) {
        if (*line != '\n' && !port_KeptLine(line, kept)) {
            port_Log(
                "tournament: %s: a line not understood; not resumed\n", path);
            return FALSE;
        }
        while (*line != 0 && *line != '\n') {
            line++;
        }
        if (*line == '\n') {
            line++;
        }
    }
    return kept->setup.count >= PORT_BRACKET_LEAST &&
           (kept->setup.rule == PORT_MATCH_FIRST_TO ||
            kept->setup.rule == PORT_MATCH_STOCK ||
            kept->setup.rule == PORT_MATCH_CREW);
}

s32 port_AskTournamentResume(void) {
    if (!port_TournamentKeptRead(&g_port_tournament_ask.kept)) {
        port_Log("tournament: no tournament kept to resume (%s)\n",
                 g_port_tournament_ask.kept_path[0] != 0
                     ? g_port_tournament_ask.kept_path
                     : "no file");
        return FALSE;
    }
    port_TournamentAsk(PORT_TOURNAMENT_ASK_RESUME);
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

/* A fighter and weapon as --tournament's words have them. */
static void port_TournamentPickText(
    s32 fighter, s32 weapon, char *out, u32 room) {
    const char *who = "?";
    s32 i;

    for (i = 0; i < COUNT_OF(g_port_tournament_choosable); i++) {
        if (g_port_tournament_choosable[i] == fighter) {
            who = g_port_tournament_fighter_words[i];
        }
    }
    port_AskText(
        out, room, "%s/%s", who,
        weapon >= 0 && weapon < COUNT_OF(g_port_tournament_weapon_words)
            ? g_port_tournament_weapon_words[weapon]
            : "?");
}

/* A match begun, in its fight: who plays each side with what (a crew's
 * every place). */
static void port_TournamentSaySides(void) {
    const PortTournament *tournament = &g_port_tournament;
    const PortMatch *match = &g_port_match;
    s32 side;

    port_Log("tournament: match %d's fight:", tournament->decided + 1);
    for (side = 0; side < 2; side++) {
        char name[PORT_STATS_NAME_BYTES];
        char pick[WORD_BYTES];
        s32 places = match->rule == PORT_MATCH_CREW ? match->count : 1;
        s32 place;

        port_TournamentNameOf(
            tournament->side_entrant[side], name, sizeof(name));
        port_Log("%s %s (", side == 0 ? "" : " against", name);
        for (place = 0; place < places && place < PORT_MATCH_CREW_MOST;
             place++) {
            if (match->rule == PORT_MATCH_CREW) {
                port_TournamentPickText(
                    match->crew[side][place].fighter,
                    match->crew[side][place].weapon, pick, sizeof(pick));
            } else {
                port_TournamentPickText(
                    tournament->fighter[side], tournament->weapon[side], pick,
                    sizeof(pick));
            }
            port_Log("%s%s", place == 0 ? "" : " ", pick);
        }
        port_Log(")");
    }
    port_Log("\n");
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
    if (tournament->resumed && g_port_tournament_ask.kept.date[0] != 0) {
        /* resumed: the date it was begun at, as its fights have it */
        port_AskText(
            g_port_tournament_ask.date, sizeof(g_port_tournament_ask.date),
            "%s", g_port_tournament_ask.kept.date);
    }
    g_port_tournament_ask.number = tournament->number;
    g_port_tournament_ask.said = 0;
    g_port_tournament_ask.recorded = FALSE;
    g_port_tournament_ask.top_seen = FALSE;
    g_port_tournament_ask.auto_asked = FALSE;
    port_Log("tournament: %s, %d entrants, %s elimination, %s %d, seeds:",
             tournament->resumed ? "resumed" : "begun", tournament->setup.count,
             tournament->setup.is_double ? "double" : "single",
             tournament->setup.rule == PORT_MATCH_STOCK  ? "stock"
             : tournament->setup.rule == PORT_MATCH_CREW ? "crew"
                                                         : "first_to",
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

    if (g_port_tournament_ask.resume_at_start) {
        /* a test's: the kept one, as Resume on the page begins it */
        g_port_tournament_ask.resume_at_start = FALSE;
        port_AskTournamentResume();
    }
    if (g_port_tournament_ask.was_on && !port_TournamentIsOn()) {
        port_Log("tournament: %s\n", g_port_tournament_ask.recorded
                                         ? "over, and put away"
                                         : "left, not finished: nothing kept");
        port_TournamentUnkeep();
    }
    g_port_tournament_ask.was_on = port_TournamentIsOn();
    if (!port_TournamentIsOn()) {
        return;
    }
    if (tournament->number != g_port_tournament_ask.number) {
        port_TournamentBegun();
    }
    port_TournamentSayMatches();
    /* kept for the next run, each time a match is decided (never a room's
     * match, which is the room's) */
    if (!tournament->online && g_port_tournament_ask.kept_path[0] != 0 &&
        tournament->phase != PORT_TOURNAMENT_DONE &&
        (tournament->number != g_port_tournament_ask.kept_number ||
         tournament->decided != g_port_tournament_ask.kept_decided ||
         !g_port_tournament_ask.kept_written)) {
        g_port_tournament_ask.kept_number = tournament->number;
        g_port_tournament_ask.kept_decided = tournament->decided;
        port_TournamentKeep();
    }
    /* the sides of a match said as its first fight is up */
    if (tournament->phase != PORT_TOURNAMENT_PLAYING) {
        g_port_tournament_ask.said_begun = 0;
    } else if (tournament->began && g_sys_screen == SCREEN_FIGHT_UPDATE &&
               g_port_tournament_ask.said_begun == 0) {
        g_port_tournament_ask.said_begun =
            (u32)tournament->number << 8 | (u32)(tournament->decided + 1);
        port_TournamentSaySides();
    }
    /* a test's: the program ended in the middle of a match's fight */
    if (g_port_tournament_ask.quit_in > 0 &&
        tournament->decided + 1 == g_port_tournament_ask.quit_in &&
        tournament->phase == PORT_TOURNAMENT_PLAYING &&
        g_sys_screen == SCREEN_FIGHT_UPDATE &&
        ++g_port_tournament_ask.quit_frames >= QUIT_IN_FRAMES) {
        port_Log("tournament: --tournament-quit-in: the program ends in "
                 "match %d's fight\n",
                 tournament->decided + 1);
        port_Quit(PORT_EXIT_OK);
    }
    if (tournament->phase == PORT_TOURNAMENT_DONE &&
        !g_port_tournament_ask.recorded) {
        char name[PORT_STATS_NAME_BYTES];

        g_port_tournament_ask.recorded = TRUE;
        port_TournamentNameOf(tournament->bracket.champion, name, sizeof(name));
        port_Log("tournament: over after %d matches; the champion: %s\n",
                 tournament->bracket.played, name);
        port_TournamentRecord();
        port_TournamentUnkeep();
    }
    /* a test's own play: the next match, a while after the bracket is up */
    if (g_port_tournament_ask.auto_after > 0 && at_top &&
        (tournament->phase == PORT_TOURNAMENT_BETWEEN ||
         tournament->phase == PORT_TOURNAMENT_DONE)) {
        if (!g_port_tournament_ask.top_seen) {
            g_port_tournament_ask.top_seen = TRUE;
            g_port_tournament_ask.top_from = g_port_input_count;
        }
        if (!g_port_tournament_ask.auto_asked &&
            g_port_input_count - g_port_tournament_ask.top_from >=
                (u32)g_port_tournament_ask.auto_after) {
            g_port_tournament_ask.auto_asked = TRUE;
            if (tournament->phase == PORT_TOURNAMENT_BETWEEN) {
                port_AskTournamentPlay();
            } else if (g_port_tournament_ask.auto_quit) {
                port_Log("tournament: --tournament-quit: the program ends\n");
                port_Quit(PORT_EXIT_OK);
            }
        }
    } else if (!at_top) {
        g_port_tournament_ask.top_seen = FALSE;
        g_port_tournament_ask.auto_asked = FALSE;
    }
}
