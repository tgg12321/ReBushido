/* The native port's record of fights: the file and the counting
 * (port_stats.h; docs/PORT.md, "Stats").
 *
 * The file is text, a line each: the first `rebushido-stats VERSION`, then
 * a line `fight` for each fight, oldest first, of words `key=value`, and
 * after them a line `tournament` for each local tournament over (version
 * 2; port_tournament.h), oldest first, of words the same way. A
 * key this version does not know is passed over, a key the line does not
 * have is the value built in, so a later version may add keys without a
 * new number. A name (the opponent's, the replay's) has every byte that
 * is not a letter of ASCII, and `%` and `=`, written as %XX. The file is
 * written whole after each fight, into PATH.part and renamed over PATH, so
 * that it is the old one or the new one, never a part of either. A file
 * whose first line is not this format's, of a newer version, or with a
 * line that is neither a fight nor a tournament, is renamed aside
 * (PATH.broken-DATE) and kept; what was read from it is kept in the new
 * one. A file of version 1 is read as it is (it has no tournaments).
 *
 * Outside the state (config/port_state.txt): nothing here is the game's,
 * and the game never reads it. */
#include "common.h"
#include "port_host.h"
#include "bb2.h"

#define HEAD_WORD "rebushido-stats"
#define FIGHT_WORD "fight"
#define TOURNAMENT_WORD "tournament"
#define LINE_BYTES 768
/* a tournament's line: its words, eight entrants and fifteen matches */
#define TOURNAMENT_LINE_BYTES 2048
#define TOURNAMENTS_FIRST_ROOM 16
/* an entrant's value, who:place:losses:seed:name, and a match's,
 * part:round:first:second:winner: their fields, and the numbers among
 * them */
#define ENTRANT_FIELDS 5
#define ENTRANT_NUMBERS 3
#define MATCH_FIELDS 5
#define MATCH_NUMBERS 4
#define FIRST_ROOM 64
#define WORD_BYTES 64
#define HEX_DIGIT_BITS 4
#define HEX_LOW 0xF
#define HEX_LETTERS_FROM 10
/* room for the file's first line, and for what a name is given beside */
#define HEAD_ROOM 64
#define ASIDE_WORD_BYTES 32

/* the date's numbers: year, month, day, hour, minute, second */
enum {
    DATE_YEAR,
    DATE_MONTH,
    DATE_DAY,
    DATE_HOUR,
    DATE_MINUTE,
    DATE_SECOND,
    DATE_PARTS
};

/* The CPUs' words (port_cpu.h's levels), a person first. */
static const char * const g_cpu_words[] = {
    "person", "original", "novice", "adept", "veteran", "master"};
#define CPU_WORDS 6
static const char * const g_mode_words[] = {
    "story", "vscpu", "practice", "slash", "vs", "group", "link"};
#define MODE_WORDS 7
static const char * const g_online_words[] = {
    "local", "online", "room", "ranked"};
#define ONLINE_WORDS 4
static const char * const g_rule_words[] = {
    "single", "stock", "first_to", "crew", "group"};
#define RULE_WORDS 5
static const char * const g_result_words[] = {"won", "lost", "draw", "going"};
#define RESULT_WORDS 4
static const char * const g_how_words[PORT_STATS_HOWS] = {
    "kill", "surrender", "points", "time"};
static const char * const g_blow_words[PORT_STATS_BLOWS] = {
    "none",     "strike", "running", "leaping", "climbing",
    "off_hand", "thrown", "shot",    "paired",  "other"};
static const char * const g_place_words[PORT_STATS_PLACES] = {
    "none", "head", "chest", "hip", "sword_arm", "off_arm", "legs"};
static const char * const g_by_mode_words[PORT_STATS_MODES] = {
    "Story",      "Vs. CPU", "Slash",  "Versus",     "Group Battle",
    "Link cable", "Online",  "Ranked", "Two on two", "Tournament"};
/* a tournament's words: the bracket's parts (port_bracket.h), the
 * seeding, and who plays an entrant */
static const char * const g_part_words[] = {
    "winners", "losers", "final", "reset"};
#define PART_WORDS 4
static const char * const g_seeding_words[] = {"random", "order"};
#define SEEDING_WORDS 2
static const char * const g_pad_words[] = {"pad1", "pad2", "pad3", "pad4"};
#define PAD_WORDS 4

static struct {
    char path[PORT_STATS_PATH_BYTES];
    s32 on;
    PortStatsFight *fights;
    s32 count;
    s32 room;
    u32 last_match;
    u32 generation;
    PortStatsTournament *tournaments;
    s32 tournament_count;
    s32 tournament_room;
    /* the text written, kept to be written again */
    char *text;
    u32 text_room;
} g_store;

/* ---- small things ---- */

static void port_StoreCopy(char *to, const char *from, u32 room) {
    u32 i;

    for (i = 0; from != NULL && from[i] != 0 && i + 1 < room; i++) {
        to[i] = from[i];
    }
    to[i] = 0;
}

static u32 port_StoreText(char *out, u32 room, const char *format, ...) {
    PortArgs args;
    u32 length;

    PORT_ARGS_START(args, format);
    length = port_FormatText(out, room, format, args);
    PORT_ARGS_END(args);
    return length;
}

static const char *port_WordOf(
    const char * const *words, s32 count, s32 value) {
    return value >= 0 && value < count ? words[value] : words[0];
}

/* Whether the `length` bytes at `text` are `word`. */
static s32 port_IsWord(const char *text, u32 length, const char *word) {
    u32 j;

    for (j = 0; j < length && word[j] == text[j]; j++) {
    }
    return j == length && word[j] == 0;
}

/* Which of `words` the `length` bytes at `text` are, or -1. */
static s32 port_WordNumber(
    const char * const *words, s32 count, const char *text, u32 length) {
    s32 i;

    for (i = 0; i < count; i++) {
        if (port_IsWord(text, length, words[i])) {
            return i;
        }
    }
    return -1;
}

const char *port_StatsHowWord(s32 how) {
    return port_WordOf(g_how_words, PORT_STATS_HOWS, how);
}

const char *port_StatsBlowWord(s32 blow) {
    return port_WordOf(g_blow_words, PORT_STATS_BLOWS, blow);
}

const char *port_StatsPlaceWord(s32 place) {
    return port_WordOf(g_place_words, PORT_STATS_PLACES, place);
}

const char *port_StatsModeWord(s32 by_mode) {
    return port_WordOf(g_by_mode_words, PORT_STATS_MODES, by_mode);
}

/* A name, its bytes not plain letters as %XX. */
static u32 port_PutName(char *out, u32 room, const char *name) {
    static const char hex[] = "0123456789ABCDEF";
    u32 at = 0;
    u32 i;

    for (i = 0; name[i] != 0; i++) {
        u8 byte = (u8)name[i];

        if (byte > ' ' && byte <= '~' && byte != '%' && byte != '=') {
            if (at + 1 >= room) {
                break;
            }
            out[at++] = (char)byte;
        } else {
            if (at + 3 >= room) {
                break;
            }
            out[at++] = '%';
            out[at++] = hex[byte >> HEX_DIGIT_BITS];
            out[at++] = hex[byte & HEX_LOW];
        }
    }
    if (room > 0) {
        out[at] = 0;
    }
    return at;
}

/* A hex digit's value, or -1. */
static s32 port_HexValue(char digit) {
    if (digit >= '0' && digit <= '9') {
        return digit - '0';
    }
    if (digit >= 'A' && digit <= 'F') {
        return digit - 'A' + HEX_LETTERS_FROM;
    }
    if (digit >= 'a' && digit <= 'f') {
        return digit - 'a' + HEX_LETTERS_FROM;
    }
    return -1;
}

/* A name as port_PutName wrote it, its %XX made bytes again. */
static void port_GetName(char *out, u32 room, const char *text, u32 length) {
    u32 at = 0;
    u32 i;

    for (i = 0; i < length && at + 1 < room; i++) {
        if (text[i] == '%' && i + 2 < length &&
            port_HexValue(text[i + 1]) >= 0 &&
            port_HexValue(text[i + 2]) >= 0) {
            out[at++] = (char)((port_HexValue(text[i + 1]) << HEX_DIGIT_BITS) |
                               port_HexValue(text[i + 2]));
            i += 2;
        } else {
            out[at++] = text[i];
        }
    }
    out[at] = 0;
}

/* `count` numbers of `text` split by ':' (the rest 0): FALSE where one
 * is not a number. */
static s32 port_GetNumbers(const char *text, u32 length, s32 *out, s32 count) {
    char word[WORD_BYTES];
    s32 number;
    u32 at = 0;

    for (number = 0; number < count; number++) {
        u32 used = 0;

        out[number] = 0;
        if (at >= length) {
            continue;
        }
        while (at < length && text[at] != ':' && used + 1 < sizeof(word)) {
            word[used++] = text[at++];
        }
        word[used] = 0;
        at++;
        if (used > 0 && !port_ParseNumber(word, &out[number])) {
            return FALSE;
        }
    }
    return TRUE;
}

/* ---- a fight as a line ---- */

static u32 port_PutKill(char *out, u32 room, const PortStatsKill *kill) {
    return port_StoreText(
        out, room, "%s:%s:%d:%d", port_StatsBlowWord(kill->blow),
        port_StatsPlaceWord(kill->place), kill->behind, kill->down);
}

/* The fight's line (port_stats.h). */
u32 port_StatsFormat(const PortStatsFight *fight, char *out, u32 room) {
    char given[WORD_BYTES];
    char taken[WORD_BYTES];
    char opponent[PORT_STATS_NAME_BYTES * 3 + 1];
    char replay[PORT_STATS_REPLAY_BYTES * 3 + 1];
    char tournament[PORT_STATS_DATE_BYTES * 3 + 1];
    u32 length;

    port_PutName(tournament, sizeof(tournament), fight->tournament);
    port_PutKill(given, sizeof(given), &fight->given);
    port_PutKill(taken, sizeof(taken), &fight->taken);
    port_PutName(opponent, sizeof(opponent), fight->opponent);
    port_PutName(replay, sizeof(replay), fight->replay);
    length = port_StoreText(
        out, room,
        FIGHT_WORD
        " date=%04d-%02d-%02dT%02d:%02d:%02d mode=%s online=%s "
        "teams=%d cpu=%s side=%d rule=%s count=%d match=%u "
        "match_fight=%d match_result=%s stage=%d me=%d:%d "
        "them=%d:%d result=%s how=%s kills=%d:%d given=%s taken=%s "
        "wounds_given=%d:%d:%d:%d wounds_taken=%d:%d:%d:%d "
        "frames=%u opponent=%s replay=%s tournament=%s\n",
        fight->year, fight->month, fight->day, fight->hour, fight->minute,
        fight->second, port_WordOf(g_mode_words, MODE_WORDS, fight->mode),
        port_WordOf(g_online_words, ONLINE_WORDS, fight->online), fight->teams,
        port_WordOf(g_cpu_words, CPU_WORDS, fight->cpu + 1), fight->side,
        port_WordOf(g_rule_words, RULE_WORDS, fight->rule), fight->count,
        fight->match, fight->match_fight,
        port_WordOf(g_result_words, RESULT_WORDS, fight->match_result),
        fight->stage, fight->fighter[0], fight->weapon[0], fight->fighter[1],
        fight->weapon[1],
        port_WordOf(g_result_words, RESULT_WORDS, fight->result),
        port_StatsHowWord(fight->how), fight->kills_given, fight->kills_taken,
        given, taken, fight->wounds_given[0], fight->wounds_given[1],
        fight->wounds_given[2], fight->wounds_given[3], fight->wounds_taken[0],
        fight->wounds_taken[1], fight->wounds_taken[2], fight->wounds_taken[3],
        fight->frames, opponent, replay, tournament);
    return length + 1 < room ? length : 0;
}

/* A kill's words: blow:place:behind:down. */
static s32 port_GetKill(const char *text, u32 length, PortStatsKill *kill) {
    u32 at = 0;
    u32 from;
    s32 numbers[2];
    s32 word;

    for (from = at; at < length && text[at] != ':'; at++) {
    }
    word =
        port_WordNumber(g_blow_words, PORT_STATS_BLOWS, text + from, at - from);
    if (word < 0) {
        return FALSE;
    }
    kill->blow = (u8)word;
    for (from = ++at; at < length && text[at] != ':'; at++) {
    }
    word = port_WordNumber(
        g_place_words, PORT_STATS_PLACES, text + from, at - from);
    if (word < 0 && at > from) {
        return FALSE;
    }
    kill->place = (u8)(word < 0 ? 0 : word);
    at++;
    if (!port_GetNumbers(text + (at < length ? at : length),
                         at < length ? length - at : 0, numbers, 2)) {
        return FALSE;
    }
    kill->behind = (u8)(numbers[0] != 0);
    kill->down = (u8)(numbers[1] != 0);
    return TRUE;
}

/* One key's value into the fight: FALSE where the value is not one. */
static s32 port_GetValue(PortStatsFight *fight, const char *key, u32 key_length,
                         const char *value, u32 length) {
    s32 numbers[DATE_PARTS];
    s32 word;
    s32 i;

#define KEY_IS(name) port_IsWord(key, key_length, name)
    if (KEY_IS("date")) {
        /* 2026-11-27T14:03:22 */
        char digits[WORD_BYTES];
        u32 used = 0;
        u32 at;

        for (at = 0; at < length && used + 1 < sizeof(digits); at++) {
            digits[used++] =
                value[at] >= '0' && value[at] <= '9' ? value[at] : ':';
        }
        digits[used] = 0;
        if (!port_GetNumbers(digits, used, numbers, DATE_PARTS)) {
            return FALSE;
        }
        fight->year = (u16)numbers[DATE_YEAR];
        fight->month = (u8)numbers[DATE_MONTH];
        fight->day = (u8)numbers[DATE_DAY];
        fight->hour = (u8)numbers[DATE_HOUR];
        fight->minute = (u8)numbers[DATE_MINUTE];
        fight->second = (u8)numbers[DATE_SECOND];
        return TRUE;
    }
    if (KEY_IS("mode")) {
        word = port_WordNumber(g_mode_words, MODE_WORDS, value, length);
        fight->mode = (u8)word;
        return word >= 0;
    }
    if (KEY_IS("online")) {
        word = port_WordNumber(g_online_words, ONLINE_WORDS, value, length);
        fight->online = (u8)word;
        return word >= 0;
    }
    if (KEY_IS("cpu")) {
        word = port_WordNumber(g_cpu_words, CPU_WORDS, value, length);
        fight->cpu = (s8)(word - 1);
        return word >= 0;
    }
    if (KEY_IS("rule")) {
        word = port_WordNumber(g_rule_words, RULE_WORDS, value, length);
        fight->rule = (u8)word;
        return word >= 0;
    }
    if (KEY_IS("match_result") || KEY_IS("result")) {
        word = port_WordNumber(g_result_words, RESULT_WORDS, value, length);
        if (word < 0) {
            return FALSE;
        }
        if (key_length == sizeof("result") - 1) {
            fight->result = (u8)word;
            return word < PORT_STATS_RESULTS;
        }
        fight->match_result = (u8)word;
        return TRUE;
    }
    if (KEY_IS("how")) {
        word = port_WordNumber(g_how_words, PORT_STATS_HOWS, value, length);
        fight->how = (u8)word;
        return word >= 0;
    }
    if (KEY_IS("given")) {
        return port_GetKill(value, length, &fight->given);
    }
    if (KEY_IS("taken")) {
        return port_GetKill(value, length, &fight->taken);
    }
    if (KEY_IS("opponent")) {
        port_GetName(fight->opponent, sizeof(fight->opponent), value, length);
        return TRUE;
    }
    if (KEY_IS("replay")) {
        port_GetName(fight->replay, sizeof(fight->replay), value, length);
        return TRUE;
    }
    if (KEY_IS("tournament")) {
        port_GetName(
            fight->tournament, sizeof(fight->tournament), value, length);
        return TRUE;
    }
    if (!KEY_IS("teams") && !KEY_IS("side") && !KEY_IS("count") &&
        !KEY_IS("match") && !KEY_IS("match_fight") && !KEY_IS("stage") &&
        !KEY_IS("me") && !KEY_IS("them") && !KEY_IS("kills") &&
        !KEY_IS("wounds_given") && !KEY_IS("wounds_taken") &&
        !KEY_IS("frames")) {
        /* a later version's key, passed over */
        return TRUE;
    }
    if (!port_GetNumbers(value, length, numbers, 4)) {
        return FALSE;
    }
    if (KEY_IS("teams")) {
        fight->teams = (u8)numbers[0];
    } else if (KEY_IS("side")) {
        fight->side = (u8)(numbers[0] != 0);
    } else if (KEY_IS("count")) {
        fight->count = (u8)numbers[0];
    } else if (KEY_IS("match")) {
        fight->match = (u32)numbers[0];
    } else if (KEY_IS("match_fight")) {
        fight->match_fight = (u8)numbers[0];
    } else if (KEY_IS("stage")) {
        fight->stage = (s16)numbers[0];
    } else if (KEY_IS("me") || KEY_IS("them")) {
        i = key[0] == 'm' ? 0 : 1;
        fight->fighter[i] = (s16)numbers[0];
        fight->weapon[i] = (s16)numbers[1];
    } else if (KEY_IS("kills")) {
        fight->kills_given = (u8)numbers[0];
        fight->kills_taken = (u8)numbers[1];
    } else if (KEY_IS("wounds_given") || KEY_IS("wounds_taken")) {
        u8 *wounds = key[sizeof("wounds_") - 1] == 'g' ? fight->wounds_given
                                                       : fight->wounds_taken;

        for (i = 0; i < PORT_STATS_WOUNDS; i++) {
            wounds[i] = (u8)numbers[i];
        }
    } else if (KEY_IS("frames")) {
        fight->frames = (u32)numbers[0];
    }
    return TRUE;
#undef KEY_IS
}

/* A line read into a fight (port_stats.h): every key's value, or FALSE
 * where the line is not a fight's. */
s32 port_StatsParse(const char *line, u32 length, PortStatsFight *out) {
    u32 at = sizeof(FIGHT_WORD) - 1;
    u32 i;

    for (i = 0; i < sizeof(*out); i++) {
        ((u8 *)out)[i] = 0;
    }
    out->teams = 1;
    out->cpu = PORT_STATS_PERSON;
    out->match_result = PORT_STATS_GOING;
    if (length < at || !port_IsWord(line, at, FIGHT_WORD) ||
        (length > at && line[at] != ' ')) {
        return FALSE;
    }
    while (at < length) {
        u32 key;
        u32 equals;
        u32 end;

        while (at < length && line[at] == ' ') {
            at++;
        }
        if (at >= length) {
            break;
        }
        key = at;
        while (at < length && line[at] != ' ' && line[at] != '=') {
            at++;
        }
        if (at >= length || line[at] != '=') {
            return FALSE;
        }
        equals = at;
        while (at < length && line[at] != ' ') {
            at++;
        }
        end = at;
        if (!port_GetValue(out, line + key, equals - key, line + equals + 1,
                           end - equals - 1)) {
            return FALSE;
        }
    }
    return TRUE;
}

/* ---- a tournament as a line ---- */

/* A date's words (2026-11-27T14:03:22) of the six numbers. */
static u32 port_PutDate(char *out, u32 room, u16 year, const u8 *parts) {
    return port_StoreText(out, room, "%04d-%02d-%02dT%02d:%02d:%02d", year,
                          parts[0], parts[1], parts[2], parts[3], parts[4]);
}

/* The tournament's line (port_stats.h): its words, then `eN` for each
 * entrant (who:place:losses:seed:name) and `mN` for each match played
 * (part:round:first:second:winner, the entrants from 1). */
u32 port_StatsFormatTournament(
    const PortStatsTournament *tournament, char *out, u32 room) {
    u8 parts[DATE_PARTS - 1];
    char date[WORD_BYTES];
    char begun[PORT_STATS_DATE_BYTES * 3 + 1];
    u32 at;
    s32 i;

    parts[0] = tournament->month;
    parts[1] = tournament->day;
    parts[2] = tournament->hour;
    parts[3] = tournament->minute;
    parts[4] = tournament->second;
    port_PutDate(date, sizeof(date), tournament->year, parts);
    port_PutName(begun, sizeof(begun), tournament->begun);
    at = port_StoreText(
        out, room,
        TOURNAMENT_WORD " date=%s begun=%s elimination=%s seeding=%s rule=%s "
                        "count=%d entrants=%d champion=%d played=%d",
        date, begun, tournament->is_double ? "double" : "single",
        port_WordOf(g_seeding_words, SEEDING_WORDS, tournament->seeding),
        port_WordOf(g_rule_words, RULE_WORDS, tournament->rule),
        tournament->rule_count, tournament->count, tournament->champion + 1,
        tournament->played);
    for (i = 0; i < tournament->count && i < PORT_STATS_TOURNAMENT_ENTRANTS;
         i++) {
        const PortStatsTournamentEntrant *entrant = &tournament->entrant[i];
        char name[PORT_STATS_NAME_BYTES * 3 + 1];

        port_PutName(name, sizeof(name), entrant->name);
        if (at + 1 >= room) {
            return 0;
        }
        at += port_StoreText(
            out + at, room - at, " e%d=%s:%d:%d:%d:%s", i + 1,
            entrant->cpu == PORT_STATS_PERSON
                ? port_WordOf(g_pad_words, PAD_WORDS, entrant->pad)
                : port_WordOf(g_cpu_words, CPU_WORDS, entrant->cpu + 1),
            entrant->place, entrant->losses, entrant->seed, name);
    }
    for (i = 0; i < tournament->played && i < PORT_STATS_TOURNAMENT_MATCHES;
         i++) {
        const PortStatsTournamentMatch *match = &tournament->match[i];

        if (at + 1 >= room) {
            return 0;
        }
        at += port_StoreText(
            out + at, room - at, " m%d=%s:%d:%d:%d:%d", i + 1,
            port_WordOf(g_part_words, PART_WORDS, match->part), match->round,
            match->first + 1, match->second + 1, match->winner + 1);
    }
    if (at + 2 >= room) {
        return 0;
    }
    out[at++] = '\n';
    out[at] = 0;
    return at;
}

/* The `length` bytes at `text` split at ':' into `fields` (at most
 * `most`; the last keeps the rest): how many there were. */
static s32 port_SplitFields(
    const char *text, u32 length, const char **fields, u32 *lengths, s32 most) {
    s32 count = 0;
    u32 at = 0;

    while (count < most) {
        u32 from = at;

        while (at < length && (text[at] != ':' || count == most - 1)) {
            at++;
        }
        fields[count] = text + from;
        lengths[count] = at - from;
        count++;
        if (at >= length) {
            break;
        }
        at++;
    }
    return count;
}

/* A number of `length` bytes: FALSE where it is not one. */
static s32 port_GetNumber(const char *text, u32 length, s32 *out) {
    char word[WORD_BYTES];
    u32 i;

    if (length == 0 || length >= sizeof(word)) {
        return FALSE;
    }
    for (i = 0; i < length; i++) {
        word[i] = text[i];
    }
    word[length] = 0;
    return port_ParseNumber(word, out);
}

/* `eN` and `mN`: the N, from 1 to `most`, or -1. */
static s32 port_KeyIndex(
    const char *key, u32 key_length, char letter, s32 most) {
    s32 index;

    if (key_length < 2 || key[0] != letter ||
        !port_GetNumber(key + 1, key_length - 1, &index) || index < 1 ||
        index > most) {
        return -1;
    }
    return index - 1;
}

/* An entrant's value: who:place:losses:seed:name. */
static s32 port_GetEntrant(
    PortStatsTournamentEntrant *entrant, const char *value, u32 length) {
    const char *fields[ENTRANT_FIELDS];
    u32 lengths[ENTRANT_FIELDS];
    s32 numbers[ENTRANT_NUMBERS];
    s32 word;
    s32 i;

    if (port_SplitFields(value, length, fields, lengths, ENTRANT_FIELDS) !=
        ENTRANT_FIELDS) {
        return FALSE;
    }
    word = port_WordNumber(g_pad_words, PAD_WORDS, fields[0], lengths[0]);
    if (word >= 0) {
        entrant->cpu = PORT_STATS_PERSON;
        entrant->pad = (u8)word;
    } else {
        word = port_WordNumber(g_cpu_words, CPU_WORDS, fields[0], lengths[0]);
        if (word < 1) {
            return FALSE;
        }
        entrant->cpu = (s8)(word - 1);
        entrant->pad = 0xFF;
    }
    for (i = 0; i < ENTRANT_NUMBERS; i++) {
        if (!port_GetNumber(fields[i + 1], lengths[i + 1], &numbers[i])) {
            return FALSE;
        }
    }
    entrant->place = (u8)numbers[0];
    entrant->losses = (u8)numbers[1];
    entrant->seed = (u8)numbers[2];
    port_GetName(entrant->name, sizeof(entrant->name),
                 fields[ENTRANT_FIELDS - 1], lengths[ENTRANT_FIELDS - 1]);
    return TRUE;
}

/* A match's value: part:round:first:second:winner. */
static s32 port_GetMatch(
    PortStatsTournamentMatch *match, const char *value, u32 length) {
    const char *fields[MATCH_FIELDS];
    u32 lengths[MATCH_FIELDS];
    s32 numbers[MATCH_NUMBERS];
    s32 word;
    s32 i;

    if (port_SplitFields(value, length, fields, lengths, MATCH_FIELDS) !=
        MATCH_FIELDS) {
        return FALSE;
    }
    word = port_WordNumber(g_part_words, PART_WORDS, fields[0], lengths[0]);
    if (word < 0) {
        return FALSE;
    }
    for (i = 0; i < MATCH_NUMBERS; i++) {
        if (!port_GetNumber(fields[i + 1], lengths[i + 1], &numbers[i])) {
            return FALSE;
        }
    }
    match->part = (u8)word;
    match->round = (u8)numbers[0];
    match->first = (u8)(numbers[1] - 1);
    match->second = (u8)(numbers[2] - 1);
    match->winner = (u8)(numbers[3] - 1);
    return TRUE;
}

/* One key's value into the tournament: FALSE where it is not one. */
static s32 port_GetTournamentValue(
    PortStatsTournament *tournament, const char *key, u32 key_length,
    const char *value, u32 length) {
    s32 index;
    s32 number;
    s32 word;

#define KEY_IS(name) port_IsWord(key, key_length, name)
    if (KEY_IS("date")) {
        PortStatsFight fight;

        if (!port_GetValue(&fight, key, key_length, value, length)) {
            return FALSE;
        }
        tournament->year = fight.year;
        tournament->month = fight.month;
        tournament->day = fight.day;
        tournament->hour = fight.hour;
        tournament->minute = fight.minute;
        tournament->second = fight.second;
        return TRUE;
    }
    if (KEY_IS("begun")) {
        port_GetName(
            tournament->begun, sizeof(tournament->begun), value, length);
        return TRUE;
    }
    if (KEY_IS("elimination")) {
        tournament->is_double = port_IsWord(value, length, "double");
        return tournament->is_double || port_IsWord(value, length, "single");
    }
    if (KEY_IS("seeding")) {
        word = port_WordNumber(g_seeding_words, SEEDING_WORDS, value, length);
        tournament->seeding = (u8)word;
        return word >= 0;
    }
    if (KEY_IS("rule")) {
        word = port_WordNumber(g_rule_words, RULE_WORDS, value, length);
        tournament->rule = (u8)word;
        return word >= 0;
    }
    index = port_KeyIndex(key, key_length, 'e', PORT_STATS_TOURNAMENT_ENTRANTS);
    if (index >= 0) {
        return port_GetEntrant(&tournament->entrant[index], value, length);
    }
    index = port_KeyIndex(key, key_length, 'm', PORT_STATS_TOURNAMENT_MATCHES);
    if (index >= 0) {
        return port_GetMatch(&tournament->match[index], value, length);
    }
    if (!KEY_IS("count") && !KEY_IS("entrants") && !KEY_IS("champion") &&
        !KEY_IS("played")) {
        /* a later version's key, passed over */
        return TRUE;
    }
    if (!port_GetNumber(value, length, &number) || number < 0) {
        return FALSE;
    }
    if (KEY_IS("count")) {
        tournament->rule_count = (u8)number;
    } else if (KEY_IS("entrants")) {
        tournament->count = (u8)number;
        return number <= PORT_STATS_TOURNAMENT_ENTRANTS;
    } else if (KEY_IS("champion")) {
        tournament->champion = (u8)(number - 1);
    } else {
        tournament->played = (u8)number;
        return number <= PORT_STATS_TOURNAMENT_MATCHES;
    }
    return TRUE;
#undef KEY_IS
}

/* A line read into a tournament (port_stats.h): FALSE where it is not
 * one. */
s32 port_StatsParseTournament(
    const char *line, u32 length, PortStatsTournament *out) {
    u32 at = sizeof(TOURNAMENT_WORD) - 1;
    u32 i;

    for (i = 0; i < sizeof(*out); i++) {
        ((u8 *)out)[i] = 0;
    }
    if (length < at || !port_IsWord(line, at, TOURNAMENT_WORD) ||
        (length > at && line[at] != ' ')) {
        return FALSE;
    }
    while (at < length) {
        u32 key;
        u32 equals;

        while (at < length && line[at] == ' ') {
            at++;
        }
        if (at >= length) {
            break;
        }
        key = at;
        while (at < length && line[at] != ' ' && line[at] != '=') {
            at++;
        }
        if (at >= length || line[at] != '=') {
            return FALSE;
        }
        equals = at;
        while (at < length && line[at] != ' ') {
            at++;
        }
        if (!port_GetTournamentValue(out, line + key, equals - key,
                                     line + equals + 1, at - equals - 1)) {
            return FALSE;
        }
    }
    return out->count >= 2;
}

/* ---- the fights in memory ---- */

static s32 port_StoreRoom(s32 count) {
    PortStatsFight *more;
    s32 room;
    s32 i;

    if (count <= g_store.room) {
        return TRUE;
    }
    room = g_store.room == 0 ? FIRST_ROOM : g_store.room * 2;
    while (room < count) {
        room *= 2;
    }
    more = port_HostAllocate((u32)room * sizeof(PortStatsFight));
    if (more == NULL) {
        return FALSE;
    }
    for (i = 0; i < g_store.count; i++) {
        more[i] = g_store.fights[i];
    }
    /* (the old array is the system's still: never given back, and a
     * page reading it at the moment reads what it had) */
    g_store.fights = more;
    g_store.room = room;
    return TRUE;
}

/* A fight kept in memory, the last; its match's number noted. */
static void port_StoreKeep(const PortStatsFight *fight) {
    if (!port_StoreRoom(g_store.count + 1)) {
        return;
    }
    g_store.fights[g_store.count] = *fight;
    if (fight->match > g_store.last_match) {
        g_store.last_match = fight->match;
    }
    /* the fight before the count that shows it */
    __asm__ volatile("" ::: "memory");
    g_store.count++;
    g_store.generation++;
}

s32 port_StatsCount(void) { return g_store.count; }

const PortStatsFight *port_StatsFightAt(s32 index) {
    return index >= 0 && index < g_store.count ? &g_store.fights[index] : NULL;
}

u32 port_StatsGeneration(void) { return g_store.generation; }

const char *port_GetStatsFile(void) { return g_store.on ? g_store.path : ""; }

/* A tournament kept in memory, the last. */
static void port_StoreKeepTournament(const PortStatsTournament *tournament) {
    if (g_store.tournament_count >= g_store.tournament_room) {
        s32 room = g_store.tournament_room == 0 ? TOURNAMENTS_FIRST_ROOM
                                                : g_store.tournament_room * 2;
        PortStatsTournament *more =
            port_HostAllocate((u32)room * sizeof(PortStatsTournament));
        s32 i;

        if (more == NULL) {
            return;
        }
        for (i = 0; i < g_store.tournament_count; i++) {
            more[i] = g_store.tournaments[i];
        }
        g_store.tournaments = more;
        g_store.tournament_room = room;
    }
    g_store.tournaments[g_store.tournament_count] = *tournament;
    __asm__ volatile("" ::: "memory");
    g_store.tournament_count++;
    g_store.generation++;
}

s32 port_StatsTournamentCount(void) { return g_store.tournament_count; }

const PortStatsTournament *port_StatsTournamentAt(s32 index) {
    return index >= 0 && index < g_store.tournament_count
               ? &g_store.tournaments[index]
               : NULL;
}

/* ---- the file ---- */

static s32 port_StoreWrite(void) {
    char part[PORT_STATS_PATH_BYTES + sizeof(".part")];
    u32 need = HEAD_ROOM + (u32)g_store.count * LINE_BYTES +
               (u32)g_store.tournament_count * TOURNAMENT_LINE_BYTES;
    u32 at;
    s32 file;
    s32 ok;
    s32 i;

    if (!g_store.on) {
        return FALSE;
    }
    if (need > g_store.text_room) {
        u32 room = need * 2;
        char *text = port_HostAllocate(room);

        if (text == NULL) {
            port_Log("stats: no memory to write %s\n", g_store.path);
            return FALSE;
        }
        g_store.text = text;
        g_store.text_room = room;
    }
    at = port_StoreText(
        g_store.text, g_store.text_room, HEAD_WORD " %d\n", PORT_STATS_VERSION);
    for (i = 0; i < g_store.count; i++) {
        at += port_StatsFormat(
            &g_store.fights[i], g_store.text + at, g_store.text_room - at);
    }
    for (i = 0; i < g_store.tournament_count; i++) {
        at += port_StatsFormatTournament(
            &g_store.tournaments[i], g_store.text + at, g_store.text_room - at);
    }
    port_StoreText(part, sizeof(part), "%s.part", g_store.path);
    file = port_HostCreate(part);
    if (file < 0) {
        port_Log("stats: %s cannot be written\n", part);
        return FALSE;
    }
    ok = port_HostAppend(file, g_store.text, at);
    port_HostClose(file);
    if (!ok || !port_HostRename(part, g_store.path)) {
        port_Log("stats: %s cannot be written\n", g_store.path);
        port_HostRemove(part);
        return FALSE;
    }
    return TRUE;
}

/* The file renamed aside, as it was: PATH.broken-YYYYMMDD-HHMMSS. */
static void port_StoreKeepAside(const char *why) {
    char aside[PORT_STATS_PATH_BYTES + ASIDE_WORD_BYTES];
    PortDate date;

    port_HostDate(&date);
    port_StoreText(aside, sizeof(aside), "%s.broken-%04d%02d%02d-%02d%02d%02d",
                   g_store.path, date.year, date.month, date.day, date.hour,
                   date.minute, date.second);
    if (port_HostRename(g_store.path, aside)) {
        port_Log("stats: %s %s: kept as %s, and a new record begun\n",
                 g_store.path, why, aside);
    } else {
        /* not to be written over: no record is kept this run */
        port_Log("stats: %s %s, and cannot be kept aside: no record is "
                 "kept\n",
                 g_store.path, why);
        g_store.on = FALSE;
    }
}

/* The file read, its fights kept; a file that is not a record of this
 * version's, or has a line that is not a fight, put aside. */
static void port_StoreRead(void) {
    s32 file = port_HostOpen(g_store.path);
    u32 size;
    char *text;
    u32 at;
    u32 line;
    s32 version = 0;
    s32 broken = FALSE;

    if (file < 0) {
        return;
    }
    size = port_HostFileSize(file);
    text = size > 0 ? port_HostAllocate(size + 1) : NULL;
    if (size == 0 || text == NULL ||
        port_HostReadAt(file, 0, text, size) != size) {
        port_HostClose(file);
        if (size != 0) {
            port_StoreKeepAside("cannot be read");
        }
        return;
    }
    port_HostClose(file);
    text[size] = 0;
    for (at = 0, line = 0; at < size; line++) {
        u32 end = at;
        u32 length;

        while (end < size && text[end] != '\n') {
            end++;
        }
        length = end - at;
        if (length > 0 && text[at + length - 1] == '\r') {
            length--;
        }
        if (line == 0) {
            u32 head = sizeof(HEAD_WORD) - 1;
            char number[WORD_BYTES];

            port_StoreCopy(number, "", sizeof(number));
            if (length > head + 1 && length - head - 1 < sizeof(number) &&
                port_IsWord(text + at, head, HEAD_WORD) &&
                text[at + head] == ' ') {
                u32 i;

                for (i = 0; i < length - head - 1; i++) {
                    number[i] = text[at + head + 1 + i];
                }
                number[i] = 0;
            }
            if (!port_ParseNumber(number, &version) || version < 1) {
                port_StoreKeepAside("is not a record of fights");
                return;
            }
            if (version > PORT_STATS_VERSION) {
                port_StoreKeepAside("is of a later version");
                return;
            }
        } else if (length > 0) {
            PortStatsFight fight;

            PortStatsTournament tournament;

            if (port_StatsParse(text + at, length, &fight)) {
                port_StoreKeep(&fight);
            } else if (
                port_StatsParseTournament(text + at, length, &tournament)) {
                port_StoreKeepTournament(&tournament);
            } else {
                broken = TRUE;
            }
        }
        at = end + 1;
    }
    if (broken) {
        port_StoreKeepAside("has lines that are not fights nor "
                            "tournaments");
        if (g_store.on) {
            port_StoreWrite();
        }
    }
    port_Log("stats: %s: %d fights, %d tournaments\n", g_store.path,
             g_store.count, g_store.tournament_count);
}

/* The record's file (port_stats.h): read now. */
void port_SetStatsFile(const char *path) {
    g_store.count = 0;
    g_store.tournament_count = 0;
    g_store.last_match = 0;
    g_store.generation++;
    g_store.on = path != NULL && path[0] != 0;
    if (!g_store.on) {
        g_store.path[0] = 0;
        return;
    }
    port_StoreCopy(g_store.path, path, sizeof(g_store.path));
    port_StoreRead();
}

/* A fight added, its match numbered, and the file written (port_stats.h). */
s32 port_StatsAdd(PortStatsFight *fight) {
    if (fight->match == 0) {
        const PortStatsFight *last = port_StatsFightAt(g_store.count - 1);

        fight->match =
            fight->match_fight > 1 && last != NULL &&
                    last->match_fight + 1 == fight->match_fight &&
                    last->rule == fight->rule && last->mode == fight->mode
                ? last->match
                : g_store.last_match + 1;
    }
    port_StoreKeep(fight);
    return port_StoreWrite();
}

/* A tournament added and the file written (port_stats.h). */
s32 port_StatsAddTournament(const PortStatsTournament *tournament) {
    port_StoreKeepTournament(tournament);
    return port_StoreWrite();
}

void port_StatsSetReplay(s32 index, const char *name) {
    if (index < 0 || index >= g_store.count) {
        return;
    }
    port_StoreCopy(g_store.fights[index].replay, name,
                   sizeof(g_store.fights[index].replay));
    g_store.generation++;
    port_StoreWrite();
}

/* ---- the counting ---- */

s32 port_StatsModeOf(const PortStatsFight *fight) {
    if (fight->online == PORT_STATS_RANKED) {
        return PORT_STATS_BY_RANKED;
    }
    if (fight->online != PORT_STATS_LOCAL) {
        return PORT_STATS_BY_ONLINE;
    }
    switch (fight->mode) {
    case FIGHT_MODE_STORY:
        return PORT_STATS_BY_STORY;
    case FIGHT_MODE_VSCPU:
        return PORT_STATS_BY_VSCPU;
    case FIGHT_MODE_CHANBARA:
        return PORT_STATS_BY_SLASH;
    case FIGHT_MODE_TEAM:
        return PORT_STATS_BY_GROUP;
    case FIGHT_MODE_NETWORK:
        return PORT_STATS_BY_LINK;
    default:
        return PORT_STATS_BY_VERSUS;
    }
}

static void port_Score(PortStatsScore *score, s32 result) {
    score->fights++;
    if (result == PORT_STATS_WON) {
        score->won++;
    } else if (result == PORT_STATS_LOST) {
        score->lost++;
    } else {
        score->drawn++;
    }
}

static void port_ScoreKey(PortStatsScore *scores, s32 key, s32 result) {
    if (key >= 0 && key < PORT_STATS_KEYS) {
        port_Score(&scores[key], result);
    }
}

static s32 port_SameName(const char *first, const char *second) {
    u32 i;

    for (i = 0; first[i] != 0 && first[i] == second[i]; i++) {
    }
    return first[i] == second[i];
}

/* The fight counted against its opponent: an online one by name, the
 * CPU by its level, a person beside the player as one. */
static void port_ScoreOpponent(
    PortStatsTally *tally, const PortStatsFight *fight) {
    s32 online = fight->online != PORT_STATS_LOCAL;
    s32 i;

    for (i = 0; i < tally->opponent_count; i++) {
        PortStatsOpponent *of = &tally->opponents[i];

        if ((of->online != 0) == online &&
            (online ? port_SameName(of->name, fight->opponent)
                    : of->cpu == fight->cpu)) {
            port_Score(&of->score, fight->result);
            return;
        }
    }
    if (tally->opponent_count < PORT_STATS_OPPONENTS) {
        PortStatsOpponent *of = &tally->opponents[tally->opponent_count++];

        port_StoreCopy(
            of->name, online ? fight->opponent : "", sizeof(of->name));
        of->cpu = online ? (s8)PORT_STATS_PERSON : fight->cpu;
        of->online = (u8)online;
        port_Score(&of->score, fight->result);
    }
}

/* Every fight of the record counted (port_stats.h). */
void port_StatsTally(PortStatsTally *out) {
    u32 run = 0;
    s32 i;
    u32 j;

    for (j = 0; j < sizeof(*out); j++) {
        ((u8 *)out)[j] = 0;
    }
    for (i = 0; i < g_store.count; i++) {
        const PortStatsFight *fight = &g_store.fights[i];
        s32 w;

        port_Score(&out->fights, fight->result);
        if (fight->match_result < PORT_STATS_RESULTS) {
            port_Score(&out->matches, fight->match_result);
        }
        port_ScoreKey(out->by_fighter, fight->fighter[0], fight->result);
        port_ScoreKey(out->by_weapon, fight->weapon[0], fight->result);
        port_ScoreKey(out->by_stage, fight->stage, fight->result);
        port_ScoreKey(out->by_against, fight->fighter[1], fight->result);
        port_Score(&out->by_mode[port_StatsModeOf(fight)], fight->result);
        if (fight->tournament[0] != 0) {
            port_Score(&out->by_mode[PORT_STATS_BY_TOURNAMENT], fight->result);
        }
        if (fight->teams == 2) {
            port_Score(&out->by_mode[PORT_STATS_BY_TWO_ON_TWO], fight->result);
        }
        if (fight->how < PORT_STATS_HOWS) {
            port_Score(&out->by_how[fight->how], fight->result);
        }
        port_ScoreOpponent(out, fight);
        out->kills_given += fight->kills_given;
        out->kills_taken += fight->kills_taken;
        if (fight->given.blow != PORT_STATS_BLOW_NONE) {
            out->given_by_blow[fight->given.blow % PORT_STATS_BLOWS]++;
            out->given_by_place[fight->given.place % PORT_STATS_PLACES]++;
            out->given_behind += fight->given.behind;
        }
        if (fight->taken.blow != PORT_STATS_BLOW_NONE) {
            out->taken_by_blow[fight->taken.blow % PORT_STATS_BLOWS]++;
            out->taken_by_place[fight->taken.place % PORT_STATS_PLACES]++;
            out->taken_behind += fight->taken.behind;
        }
        for (w = 0; w < PORT_STATS_WOUNDS; w++) {
            out->wounds_given[w] += fight->wounds_given[w];
            out->wounds_taken[w] += fight->wounds_taken[w];
        }
        run = fight->result == PORT_STATS_WON ? run + 1 : 0;
        if (run > out->longest_run) {
            out->longest_run = run;
        }
        out->frames += fight->frames;
    }
    out->run_now = run;
}
