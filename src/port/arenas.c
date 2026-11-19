/* The native port's custom arenas (port_arenas.h; docs/MODS.md, "Custom
 * arenas"): the mods' `arenas/NAME.arena` read and checked as the program
 * starts, the one the run asks for put in play over its host stage, and
 * the answers to what the game asks of its stage tables. With no arena in
 * play every answer is the game's own value, given back as it came.
 *
 * An arena's file is text and then its ground:
 *
 *   RBARENA 1                  the magic and the format
 *   name = Stone courtyard     lines of `name = value`, `#` a comment
 *   ...
 *   ground = 51234             the last line: so many bytes follow it,
 *   <the ground file>          from the byte after its newline
 *
 * Nothing here is the game's state (config/port_state.txt): the arenas
 * are read once and only read after. */
#include "common.h"
#include "port_discfiles.h"
#include "port_host.h"
#include "port_mods.h"
#include "bb2.h"

#define TEXT_END 0
#define WORD_SIZE 64
#define PROBLEM_SIZE 160
/* an arena's file is not larger than this: its text and a ground no
 * larger than the largest stage's room on the disc (stage 37's 96
 * sectors) */
#define FILE_BYTES_MAX (256 * 1024)
#define NUMBERS_MAX (2 * PORT_ARENA_CHAIN_CORNERS_MAX + 2)
#define FNV_OFFSET 0xCBF29CE484222325ULL
#define FNV_PRIME 0x100000001B3ULL
#define START_PAIRS 4
#define START_PAIR_HALVES 6
/* a colour of the light preset: 0xRRGGBB; the angles' word: two of 4096 */
#define COLOR_MAX 0xFFFFFF
/* the music the stages have (g_snd_stage_music_tracks: 0 to 4) */
#define MUSIC_TRACK_MAX 4
/* a ground file's header: its first word, how many sections, is six for
 * a stage with nothing of its own (tools/stage_ground.py) */
#define GROUND_SECTIONS_PLAIN 6
/* the grid's edge: a place or a corner is no farther from the middle */
#define GRID_REACH 32000
/* a fog's depth, in the world's units (g_stage_fog_spans) */
#define FOG_SPAN_MIN 1000
#define FOG_SPAN_MAX 64000
/* `startN`: the name's length, and where its number is */
#define START_NAME_LENGTH 6
#define START_NUMBER_AT 5
/* `RBARENA 1`: where the format's number begins on the first line */
#define MAGIC_LENGTH 7
#define DECIMAL 10
/* the light's word of angles, which is no colour */
#define LIGHT_ANGLES 4
/* a chain's line: ring or open, a kind, a margin, then the corners */
#define CHAIN_HEAD_WORDS 3
#define CHAIN_CORNERS_MIN 2
/* the ground's header, at the least: its count and seven offsets */
#define GROUND_HEADER_BYTES (8 * 4)
#define BYTE_BITS 8

static struct {
    s32 count;
    PortArena arenas[PORT_ARENAS_MAX];
    char asked[PORT_ARENA_KEY_SIZE]; /* the run's `--arena` */
    PortArena *in_play;
    u8 *file; /* the one in play's file, whole */
    /* what the game is handed in place of its tables */
    s16 starts[STAGE_COUNT * PORT_ARENA_START_HALVES];
    NavChain chains[PORT_ARENA_CHAINS_MAX];
    NavSet nav;
} g_port_arenas;

/* ---- text ---- */

static void port_ArenaSay(char *out, u32 room, const char *format, ...) {
    PortArgs args;

    if (out == NULL || room == 0) {
        return;
    }
    PORT_ARGS_START(args, format);
    port_FormatText(out, room, format, args);
    PORT_ARGS_END(args);
}

static s32 port_ArenaIsSpace(char character) {
    return character == ' ' || character == '\t' || character == '\r' ||
           character == ',';
}

static char port_ArenaLower(char character) {
    return character >= 'A' && character <= 'Z' ? (char)(character - 'A' + 'a')
                                                : character;
}

/* The words of `text` (`length` bytes) apart by spaces or commas, each
 * into `words` (WORD_SIZE bytes each, `room` of them): how many, or -1
 * for too many or one too long. */
static s32 port_ArenaWords(
    const char *text, u32 length, char (*words)[WORD_SIZE], s32 room) {
    s32 count = 0;
    u32 at = 0;

    while (at < length) {
        u32 size = 0;

        while (at < length && port_ArenaIsSpace(text[at])) {
            at++;
        }
        if (at == length) {
            break;
        }
        if (count == room) {
            return -1;
        }
        while (at < length && !port_ArenaIsSpace(text[at])) {
            if (size + 1 >= WORD_SIZE) {
                return -1;
            }
            words[count][size++] = text[at++];
        }
        words[count][size] = TEXT_END;
        count++;
    }
    return count;
}

/* Whether the name of a line (`length` bytes) is `name`, `-` as `_`. */
static s32 port_ArenaNameIs(const char *text, u32 length, const char *name) {
    u32 i;

    for (i = 0; i < length; i++) {
        char character = port_ArenaLower(text[i]);

        if (character == '-') {
            character = '_';
        }
        if (name[i] == TEXT_END || character != name[i]) {
            return FALSE;
        }
    }
    return name[length] == TEXT_END;
}

/* The numbers of a line's value into `numbers`: how many, or -1 for a
 * word that is not one. */
static s32 port_ArenaNumbers(
    const char *value, u32 length, s32 *numbers, s32 room) {
    char words[NUMBERS_MAX][WORD_SIZE];
    s32 count = port_ArenaWords(value, length, words, NUMBERS_MAX);
    s32 i;

    if (count < 0 || count > room) {
        return -1;
    }
    for (i = 0; i < count; i++) {
        if (!port_ParseNumber(words[i], &numbers[i])) {
            return -1;
        }
    }
    return count;
}

/* A stage an arena may stand in for: none with a hook of its own, areas,
 * props, or code that asks for it by name (stage.h, stage_data.c). */
static s32 port_ArenaHostIsPlain(s32 stage) {
    /* the modes' own stages (32 on) are not on the select screen, and the
     * music's table has none of them */
    if (stage < 0 || stage >= STAGE_TEAM_1) {
        return FALSE;
    }
    if (g_stage_hooks[stage].init != NULL ||
        g_stage_hooks[stage].update != NULL) {
        return FALSE;
    }
    return stage != STAGE_SHAINTO_HQ && stage != STAGE_TATARA_SHRINE &&
           stage != STAGE_CHANBARA;
}

/* ---- a file ---- */

/* One line, `name` = `value`, into the arena: NULL, or what is wrong. */
static const char *port_ArenaLine(
    PortArena *arena, const char *name, u32 name_length, const char *value,
    u32 value_length) {
    s32 numbers[NUMBERS_MAX];
    s32 count;
    s32 i;

#define NAME(word) port_ArenaNameIs(name, name_length, word)
    if (NAME("name") || NAME("lights")) {
        char *out = NAME("name") ? arena->name : arena->lights;
        u32 room = NAME("name") ? PORT_ARENA_NAME_SIZE : PORT_ARENA_KEY_SIZE;

        if (value_length + 1 > room) {
            return "too long";
        }
        for (i = 0; i < (s32)value_length; i++) {
            out[i] = value[i];
        }
        out[i] = TEXT_END;
        return NULL;
    }
    if (NAME("fog")) {
        if (port_ArenaNameIs(value, value_length, "on")) {
            arena->fog = 1;
        } else if (port_ArenaNameIs(value, value_length, "off")) {
            arena->fog = 0;
        } else {
            return "is on or off";
        }
        return NULL;
    }
    if (NAME("chain")) {
        char words[NUMBERS_MAX + 1][WORD_SIZE];
        PortArenaChain *chain;

        count = port_ArenaWords(value, value_length, words, NUMBERS_MAX + 1);
        if (count < CHAIN_HEAD_WORDS + 2 * CHAIN_CORNERS_MIN ||
            (count - CHAIN_HEAD_WORDS) % 2 != 0 ||
            (count - CHAIN_HEAD_WORDS) / 2 > PORT_ARENA_CHAIN_CORNERS_MAX) {
            return "is ring or open, a kind, a margin, then two to 16 "
                   "corners of x z";
        }
        if (arena->chain_count == PORT_ARENA_UNSET) {
            arena->chain_count = 0;
        }
        if (arena->chain_count == PORT_ARENA_CHAINS_MAX) {
            return "more chains than 8";
        }
        chain = &arena->chains[arena->chain_count];
        if (port_TextsEqual(words[0], "open")) {
            chain->flags = NAV_CHAIN_OPEN;
        } else if (port_TextsEqual(words[0], "ring")) {
            chain->flags = 0;
        } else {
            return "begins with ring or open";
        }
        for (i = 1; i < count; i++) {
            if (!port_ParseNumber(words[i], &numbers[i])) {
                return "has a word that is not a number";
            }
        }
        if (numbers[1] < 0 || numbers[1] > 0xFF || numbers[2] < 0 ||
            numbers[2] > 0xFF) {
            return "a kind and a margin are 0 to 255";
        }
        chain->kind = (u8)numbers[1];
        chain->margin = (u8)numbers[2];
        chain->corners = (u8)((count - CHAIN_HEAD_WORDS) / 2);
        for (i = 0; i < chain->corners; i++) {
            s32 x = numbers[CHAIN_HEAD_WORDS + 2 * i];
            s32 z = numbers[CHAIN_HEAD_WORDS + 1 + 2 * i];

            if (x < -GRID_REACH || x > GRID_REACH || z < -GRID_REACH ||
                z > GRID_REACH) {
                return "a corner is outside the grid (-32000 to 32000)";
            }
            chain->at[i][0] = (s16)x;
            chain->at[i][1] = (s16)z;
        }
        arena->chain_count++;
        return NULL;
    }
    if (!NAME("stands_in_for") && !NAME("light") && !NAME("format") &&
        !NAME("fog_span") && !NAME("fog_farthest") && !NAME("music") &&
        !NAME("sounds") &&
        !(name_length == START_NAME_LENGTH &&
          port_ArenaNameIs(name, START_NUMBER_AT, "start"))) {
        return "is not a name this program knows";
    }
    count = port_ArenaNumbers(value, value_length, numbers, NUMBERS_MAX);
    if (count < 0) {
        return "has a word that is not a number";
    }
    if (NAME("stands_in_for")) {
        if (count != 1 || !port_ArenaHostIsPlain(numbers[0])) {
            return "is a stage of the select screen with no hook, areas or "
                   "props of its own (0, 1, 6, 9, 10, 12, 15 to 17, 19 to "
                   "22, 25 to 31)";
        }
        arena->host = numbers[0];
        return NULL;
    }
    if (name_length == START_NAME_LENGTH &&
        port_ArenaNameIs(name, START_NUMBER_AT, "start") &&
        name[START_NUMBER_AT] >= '1' &&
        name[START_NUMBER_AT] <= '0' + START_PAIRS) {
        s32 pair = name[START_NUMBER_AT] - '1';

        if (count != START_PAIR_HALVES) {
            return "is two points of x y z";
        }
        for (i = 0; i < START_PAIR_HALVES; i++) {
            if (numbers[i] < -GRID_REACH || numbers[i] > GRID_REACH) {
                return "a point is outside the grid (-32000 to 32000)";
            }
            arena->starts[pair * START_PAIR_HALVES + i] = (s16)numbers[i];
        }
        arena->has_starts |= 1 << pair;
        return NULL;
    }
    if (NAME("light")) {
        if (count != PORT_ARENA_LIGHT_WORDS) {
            return "is six numbers: two colours of the fighters', two of "
                   "the scenery's, the sun's angles, the distance's colour";
        }
        for (i = 0; i < PORT_ARENA_LIGHT_WORDS; i++) {
            if (i != LIGHT_ANGLES &&
                (numbers[i] < 0 || numbers[i] > COLOR_MAX)) {
                return "a colour is 0 to 0xFFFFFF";
            }
            arena->light[i] = numbers[i];
        }
        arena->has_light = TRUE;
        return NULL;
    }
    if (count != 1) {
        return "is one number";
    }
    if (NAME("format")) {
        arena->format = numbers[0];
    } else if (NAME("fog_span")) {
        if (numbers[0] < FOG_SPAN_MIN || numbers[0] > FOG_SPAN_MAX) {
            return "is 1000 to 64000";
        }
        arena->fog_span = numbers[0];
    } else if (NAME("fog_farthest")) {
        if (numbers[0] != 0 && numbers[0] != 1) {
            return "is 0 or 1";
        }
        arena->fog_farthest = numbers[0];
    } else if (NAME("music")) {
        if (numbers[0] < 0 || numbers[0] > MUSIC_TRACK_MAX) {
            return "is a track of the stages', 0 to 4";
        }
        arena->music = numbers[0];
    } else if (NAME("sounds")) {
        if (numbers[0] < 0 || numbers[0] >= STAGE_COUNT) {
            return "is a stage, 0 to 37";
        }
        arena->sounds = numbers[0];
    } else {
        return "is not a name this program knows";
    }
    return NULL;
#undef NAME
}

/* An arena's file into `arena` (port_arenas.h): its first line, then
 * each line of `name = value` (port_ArenaLine) to the one that says how
 * long the ground is; then what a file must have, and the starts it did
 * not give made the first pair's. */
s32 port_ArenaParse(
    const u8 *data, u32 length, PortArena *arena, char *problem, u32 room) {
    const char *text = (const char *)data;
    u32 at = 0;
    s32 line = 0;
    s32 i;
    u64 hash = FNV_OFFSET;

    arena->name[0] = TEXT_END;
    arena->lights[0] = TEXT_END;
    arena->format = PORT_ARENA_UNSET;
    arena->host = PORT_ARENA_HOST_DEFAULT;
    arena->has_starts = 0;
    arena->has_light = FALSE;
    arena->fog = PORT_ARENA_UNSET;
    arena->fog_span = PORT_ARENA_UNSET;
    arena->fog_farthest = PORT_ARENA_UNSET;
    arena->music = PORT_ARENA_UNSET;
    arena->sounds = PORT_ARENA_UNSET;
    arena->chain_count = PORT_ARENA_UNSET;
    arena->ground_at = 0;
    arena->ground_bytes = 0;
    for (i = 0; i < (s32)length; i++) {
        hash = (hash ^ data[i]) * FNV_PRIME;
    }
    arena->hash = hash;

    for (i = 0; PORT_ARENA_MAGIC[i] != TEXT_END; i++) {
        if ((u32)i >= length || text[i] != PORT_ARENA_MAGIC[i]) {
            port_ArenaSay(problem, room,
                          "not an arena's file (it begins "
                          "with RBARENA)");
            return FALSE;
        }
    }
    while (at < length) {
        u32 start = at;
        u32 end;
        u32 equals;
        u32 name_end;
        u32 value_at;
        const char *wrong;

        while (at < length && text[at] != '\n') {
            at++;
        }
        end = at;
        at += at < length;
        line++;
        if (line == 1) {
            /* `RBARENA 1`: the format */
            s32 format = 0;

            for (i = (s32)start + MAGIC_LENGTH; i < (s32)end; i++) {
                if (text[i] >= '0' && text[i] <= '9') {
                    format = format * DECIMAL + text[i] - '0';
                }
            }
            arena->format = format;
            continue;
        }
        for (i = (s32)start; i < (s32)end && text[i] != '#'; i++) {
        }
        end = (u32)i;
        while (end > start && port_ArenaIsSpace(text[end - 1])) {
            end--;
        }
        while (start < end && port_ArenaIsSpace(text[start])) {
            start++;
        }
        if (start == end) {
            continue;
        }
        for (equals = start; equals < end && text[equals] != '='; equals++) {
        }
        if (equals == end) {
            port_ArenaSay(problem, room, "line %d: no `=`", line);
            return FALSE;
        }
        name_end = equals;
        while (name_end > start && port_ArenaIsSpace(text[name_end - 1])) {
            name_end--;
        }
        value_at = equals + 1;
        while (value_at < end && port_ArenaIsSpace(text[value_at])) {
            value_at++;
        }
        if (port_ArenaNameIs(text + start, name_end - start, "ground")) {
            s32 bytes;
            char word[WORD_SIZE];
            u32 size = end - value_at;

            if (size + 1 > WORD_SIZE) {
                size = WORD_SIZE - 1;
            }
            for (i = 0; i < (s32)size; i++) {
                word[i] = text[value_at + i];
            }
            word[size] = TEXT_END;
            if (!port_ParseNumber(word, &bytes) || bytes <= 0 ||
                (u32)bytes > length - at) {
                port_ArenaSay(problem, room,
                              "line %d: ground: is the number of bytes that "
                              "follow the line",
                              line);
                return FALSE;
            }
            arena->ground_at = at;
            arena->ground_bytes = (u32)bytes;
            break;
        }
        wrong = port_ArenaLine(arena, text + start, name_end - start,
                               text + value_at, end - value_at);
        if (wrong != NULL) {
            char name[WORD_SIZE];
            u32 size = name_end - start < WORD_SIZE - 1 ? name_end - start
                                                        : WORD_SIZE - 1;

            for (i = 0; i < (s32)size; i++) {
                name[i] = text[start + i];
            }
            name[size] = TEXT_END;
            port_ArenaSay(problem, room, "line %d: %s %s", line, name, wrong);
            return FALSE;
        }
    }
    if (arena->format < 1) {
        port_ArenaSay(problem, room, "no format on its first line");
        return FALSE;
    }
    if (arena->format > PORT_ARENA_FORMAT) {
        port_ArenaSay(
            problem, room, "its format is %d: a later ReBushido is needed",
            arena->format);
        return FALSE;
    }
    if (arena->ground_bytes < GROUND_HEADER_BYTES) {
        port_ArenaSay(problem, room,
                      "no ground (its last line is "
                      "`ground = BYTES`)");
        return FALSE;
    }
    {
        const u8 *ground = data + arena->ground_at;
        u32 sections = ground[0] | (ground[1] << BYTE_BITS) |
                       (ground[2] << (2 * BYTE_BITS)) |
                       ((u32)ground[3] << (3 * BYTE_BITS));

        if (sections != GROUND_SECTIONS_PLAIN) {
            port_ArenaSay(problem, room,
                          "its ground has %u sections, not six: an arena "
                          "has no areas or props",
                          sections);
            return FALSE;
        }
    }
    if (!(arena->has_starts & 1)) {
        port_ArenaSay(problem, room, "no start1: where the fighters start");
        return FALSE;
    }
    /* pairs not given are the first */
    for (i = 1; i < START_PAIRS; i++) {
        if (!(arena->has_starts & (1 << i))) {
            s32 j;

            for (j = 0; j < START_PAIR_HALVES; j++) {
                arena->starts[i * START_PAIR_HALVES + j] = arena->starts[j];
            }
        }
    }
    if (arena->name[0] == TEXT_END) {
        port_ArenaSay(arena->name, PORT_ARENA_NAME_SIZE, "%s", arena->key);
    }
    return TRUE;
}

/* ---- the mods' arenas ---- */

s32 port_ArenaAsk(const char *key) {
    u32 i;

    for (i = 0; key[i] != TEXT_END; i++) {
        if (i + 1 >= PORT_ARENA_KEY_SIZE) {
            return FALSE;
        }
        g_port_arenas.asked[i] = port_ArenaLower(key[i]);
    }
    g_port_arenas.asked[i] = TEXT_END;
    return TRUE;
}

/* What the game is handed while `arena` is in play. */
static void port_ArenaPutInPlay(PortArena *arena) {
    s16 *row;
    s32 i;

    for (i = 0; i < STAGE_COUNT * PORT_ARENA_START_HALVES; i++) {
        g_port_arenas.starts[i] = ((s16 *)g_fight_start_positions)[i];
    }
    row = &g_port_arenas.starts[arena->host * PORT_ARENA_START_HALVES];
    for (i = 0; i < PORT_ARENA_START_HALVES; i++) {
        row[i] = arena->starts[i];
    }
    if (arena->chain_count != PORT_ARENA_UNSET) {
        for (i = 0; i < arena->chain_count; i++) {
            g_port_arenas.chains[i].flags = arena->chains[i].flags;
            g_port_arenas.chains[i].kind = arena->chains[i].kind;
            g_port_arenas.chains[i].margin = arena->chains[i].margin;
            g_port_arenas.chains[i].nvtx = arena->chains[i].corners;
            g_port_arenas.chains[i].vtx = arena->chains[i].at;
        }
        g_port_arenas.nav.nchains = (u8)arena->chain_count;
        g_port_arenas.nav.chains = g_port_arenas.chains;
    }
    g_port_arenas.in_play = arena;
}

/* Every mod's arena read (port_arenas.h); the one --arena asked for, if
 * any, put over its host: its ground over the host's sectors, the tables
 * made. A name no mod has ends the run, as a mistake of its arguments. */
void port_ArenasStart(void) {
    const char *key;
    const char *path;
    s32 place;
    s32 i;

    for (place = 0; port_ModFileOf(PORT_MOD_ARENAS, place, &key, &path);
         place++) {
        PortArena *arena = &g_port_arenas.arenas[g_port_arenas.count];
        char problem[PROBLEM_SIZE];
        u32 size = port_ModFileSize(path);
        u8 *data;

        if (g_port_arenas.count == PORT_ARENAS_MAX) {
            port_Log(
                "arenas: more than %d; %s left out\n", PORT_ARENAS_MAX, path);
            continue;
        }
        if (size == 0 || size > FILE_BYTES_MAX) {
            port_Log(
                "arenas: %s (%s) is %u bytes: left out\n", key, path, size);
            continue;
        }
        data = port_HostAllocate(size);
        if (data == NULL || port_ModReadFile(path, data, size) != size) {
            port_Log("arenas: %s (%s) cannot be read: left out\n", key, path);
            continue;
        }
        port_ArenaSay(arena->key, PORT_ARENA_KEY_SIZE, "%s", key);
        if (!port_ArenaParse(data, size, arena, problem, sizeof(problem))) {
            port_Log("arenas: %s (%s): %s; left out\n", key, path, problem);
            continue;
        }
        g_port_arenas.count++;
        if (port_TextsEqual(arena->key, g_port_arenas.asked)) {
            g_port_arenas.file = data;
        }
    }
    if (g_port_arenas.asked[0] == TEXT_END) {
        return;
    }
    for (i = 0; i < g_port_arenas.count; i++) {
        PortArena *arena = &g_port_arenas.arenas[i];
        u32 room;

        if (!port_TextsEqual(arena->key, g_port_arenas.asked)) {
            continue;
        }
        if (!port_SetArenaGround(
                arena->host, g_port_arenas.file + arena->ground_at,
                arena->ground_bytes, &room)) {
            port_Log("arenas: %s's ground is %u bytes, where its host, stage "
                     "%d, has room for %u\n",
                     arena->key, arena->ground_bytes, arena->host, room);
            port_Quit(PORT_EXIT_USAGE);
        }
        port_ArenaPutInPlay(arena);
        port_Log("arenas: %s (\"%s\") in play for stage %d, its ground %u "
                 "bytes of the %u there is room for\n",
                 arena->key, arena->name, arena->host, arena->ground_bytes,
                 room);
        return;
    }
    port_Log("arenas: no mod has an arena %s (%d arenas)\n",
             g_port_arenas.asked, g_port_arenas.count);
    port_Quit(PORT_EXIT_USAGE);
}

s32 port_ArenaCount(void) { return g_port_arenas.count; }

const PortArena *port_ArenaAt(s32 index) {
    return index >= 0 && index < g_port_arenas.count
               ? &g_port_arenas.arenas[index]
               : NULL;
}

const PortArena *port_ArenaInPlay(void) { return g_port_arenas.in_play; }

const char *port_ArenaLightsKey(void) {
    const PortArena *arena = g_port_arenas.in_play;

    return arena != NULL && arena->lights[0] != TEXT_END ? arena->lights : NULL;
}

/* The arena in play for netplay's set-up (port_arenas.h): its file's
 * hash, and its key, so that two arenas of the same bytes under two
 * names are not the same. */
u64 port_ArenaSessionHash(void) {
    const PortArena *arena = g_port_arenas.in_play;
    const char *key;
    u64 hash;

    if (arena == NULL) {
        return 0;
    }
    hash = arena->hash;
    for (key = arena->key; *key != TEXT_END; key++) {
        hash = (hash ^ (u8)*key) * FNV_PRIME;
    }
    return hash;
}

/* ---- what the game asks ---- */

/* The arena in play if `stage` is its host, else NULL. */
static const PortArena *port_ArenaFor(s32 stage) {
    const PortArena *arena = g_port_arenas.in_play;

    return arena != NULL && arena->host == stage ? arena : NULL;
}

void *port_ArenaStartPositions(void *table) {
    return g_port_arenas.in_play != NULL ? (void *)g_port_arenas.starts : table;
}

s32 *port_ArenaLightPreset(s32 stage, s32 *preset) {
    const PortArena *arena = port_ArenaFor(stage);

    return arena != NULL && arena->has_light ? (s32 *)arena->light : preset;
}

s32 port_ArenaFogOn(s32 stage, s32 on) {
    const PortArena *arena = port_ArenaFor(stage);

    return arena != NULL && arena->fog != PORT_ARENA_UNSET ? arena->fog : on;
}

s32 port_ArenaFogFarthest(s32 stage, s32 farthest) {
    const PortArena *arena = port_ArenaFor(stage);

    return arena != NULL && arena->fog_farthest != PORT_ARENA_UNSET
               ? arena->fog_farthest
               : farthest;
}

s32 port_ArenaFogSpan(s32 stage, s32 span) {
    const PortArena *arena = port_ArenaFor(stage);

    return arena != NULL && arena->fog_span != PORT_ARENA_UNSET
               ? arena->fog_span
               : span;
}

s32 port_ArenaMusic(s32 stage, s32 track) {
    const PortArena *arena = port_ArenaFor(stage);

    return arena != NULL && arena->music != PORT_ARENA_UNSET
               ? arena->music
               : track;
}

s32 port_ArenaSoundStage(s32 stage) {
    const PortArena *arena = port_ArenaFor(stage);

    return arena != NULL && arena->sounds != PORT_ARENA_UNSET
               ? arena->sounds
               : stage;
}

void *port_ArenaNavSet(s32 stage, void *set) {
    const PortArena *arena = port_ArenaFor(stage);

    return arena != NULL && arena->chain_count != PORT_ARENA_UNSET
               ? (void *)&g_port_arenas.nav
               : set;
}
