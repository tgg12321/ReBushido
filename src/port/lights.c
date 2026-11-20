/* Where the remastered picture's lights come from (port_lights.h;
 * docs/MODS.md, "Lights"): a stage's light files read and layered in the
 * mods' order, and what the game's effects say they lit up.
 *
 * Nothing here is the game's state (config/port_state.txt: outside). */
#include "common.h"
#include "port_host.h"
#include "port_mods.h"
#include "bb2.h"

/* the largest light file read */
#define FILE_BYTES_MAX 0x10000
#define WORD_SIZE 32
#define LINE_END '\n'
#define TEXT_END 0
#define TENS 10
#define HEX_DIGITS 16
#define COLOR_MOST 255
/* the largest whole part of a number read */
#define WHOLE_MOST 1000000
/* `#RRGGBB`: its hexadecimal digits, and its length with the `#` */
#define COLOR_DIGITS 6
#define COLOR_TEXT_LENGTH (COLOR_DIGITS + 1)
#define PROBLEM_SIZE 128
#define CONE_MOST 90
#define FIGHTERS 2

/* a light as a file starts it */
#define DEFAULT_RADIUS 3000
#define DEFAULT_CONE_INNER 30
#define DEFAULT_CONE_OUTER 45

/* the game's Flame (eff.h): x times two over its bit, y times eight over
 * its kind */
#define FLAME_X_SCALE 2
#define FLAME_Y_SCALE 8
#define FLAME_KIND_MASK 7

static struct {
    s32 has_stage;               /* whether a stage's set is kept */
    s32 stage;                   /* whose */
    char key[PORT_MOD_KEY_SIZE]; /* and the files' key it was read by */
    PortLightSet set;
    char text[FILE_BYTES_MAX];
    PortLightsGame game;
} g_port_lights;

/* ---- text ---- */

static void port_LightsSay(char *out, u32 room, const char *format, ...) {
    PortArgs args;

    if (out == NULL || room == 0) {
        return;
    }
    PORT_ARGS_START(args, format);
    port_FormatText(out, room, format, args);
    PORT_ARGS_END(args);
}

static s32 port_LightsIsSpace(char character) {
    return character == ' ' || character == '\t' || character == '\r';
}

static char port_LightsLower(char character) {
    return character >= 'A' && character <= 'Z' ? (char)(character - 'A' + 'a')
                                                : character;
}

/* Whether the `length` characters at `text` are `word`, of either case. */
static s32 port_LightsWordIs(const char *text, u32 length, const char *word) {
    u32 i;

    for (i = 0; i < length; i++) {
        if (word[i] == TEXT_END || port_LightsLower(text[i]) != word[i]) {
            return FALSE;
        }
    }
    return word[length] == TEXT_END;
}

/* The next word of `*text` (up to `end`), its length; *text moves past
 * it. 0 at the end. */
static u32 port_LightsWord(
    const char **text, const char *end, const char **word) {
    const char *at = *text;
    u32 length = 0;

    while (at < end && port_LightsIsSpace(*at)) {
        at++;
    }
    *word = at;
    while (at + length < end && !port_LightsIsSpace(at[length])) {
        length++;
    }
    *text = at + length;
    return length;
}

/* A number of the file, whole or with a fraction (`-1.25`), in
 * thousandths: TRUE and the number. */
static s32 port_LightsFraction(const char *text, u32 length, s32 *value) {
    u32 i = 0;
    s32 sign = 1;
    s32 whole = 0;
    s32 part = 0;
    s32 scale = PORT_LIGHT_ONE;
    s32 digits = 0;

    if (length > 0 && (text[0] == '-' || text[0] == '+')) {
        sign = text[0] == '-' ? -1 : 1;
        i++;
    }
    for (; i < length && text[i] >= '0' && text[i] <= '9'; i++) {
        if (whole > WHOLE_MOST) {
            return FALSE;
        }
        whole = whole * TENS + (text[i] - '0');
        digits++;
    }
    if (i < length && text[i] == '.') {
        for (i++; i < length && text[i] >= '0' && text[i] <= '9'; i++) {
            if (scale > 1) {
                scale /= TENS;
                part += (text[i] - '0') * scale;
            }
            digits++;
        }
    }
    if (i != length || digits == 0) {
        return FALSE;
    }
    *value = sign * (whole * PORT_LIGHT_ONE + part);
    return TRUE;
}

/* `count` whole numbers (a fraction is rounded towards nought). */
static s32 port_LightsWholes(
    const char *text, const char *end, s32 *out, s32 count) {
    const char *word;
    s32 i;

    for (i = 0; i < count; i++) {
        u32 length = port_LightsWord(&text, end, &word);
        s32 value;

        if (length == 0 || !port_LightsFraction(word, length, &value)) {
            return FALSE;
        }
        out[i] = value / PORT_LIGHT_ONE;
    }
    return port_LightsWord(&text, end, &word) == 0;
}

/* One fraction and nothing after it. */
static s32 port_LightsOneFraction(const char *text, const char *end, s32 *out) {
    const char *word;
    u32 length = port_LightsWord(&text, end, &word);

    if (length == 0 || !port_LightsFraction(word, length, out)) {
        return FALSE;
    }
    return port_LightsWord(&text, end, &word) == 0;
}

static s32 port_LightsHexDigit(char character) {
    character = port_LightsLower(character);
    if (character >= '0' && character <= '9') {
        return character - '0';
    }
    if (character >= 'a' && character <= 'f') {
        return character - 'a' + TENS;
    }
    return -1;
}

/* `#RRGGBB`, or three numbers of 0 to 255. */
static s32 port_LightsColor(const char *text, const char *end, u8 *rgb) {
    const char *word;
    const char *start = text;
    u32 length = port_LightsWord(&text, end, &word);
    s32 parts[3];
    s32 i;

    if (length == COLOR_TEXT_LENGTH && word[0] == '#') {
        for (i = 0; i < 3; i++) {
            s32 high = port_LightsHexDigit(word[1 + 2 * i]);
            s32 low = port_LightsHexDigit(word[2 + 2 * i]);

            if (high < 0 || low < 0) {
                return FALSE;
            }
            rgb[i] = (u8)(high * HEX_DIGITS + low);
        }
        return port_LightsWord(&text, end, &word) == 0;
    }
    if (!port_LightsWholes(start, end, parts, 3)) {
        return FALSE;
    }
    for (i = 0; i < 3; i++) {
        if (parts[i] < 0 || parts[i] > COLOR_MOST) {
            return FALSE;
        }
        rgb[i] = (u8)parts[i];
    }
    return TRUE;
}

/* `yes` or `on`, `no` or `off`, and nothing after it. */
static s32 port_LightsYesOrNo(const char *text, const char *end, s32 *out) {
    const char *word;
    u32 length = port_LightsWord(&text, end, &word);
    const char *rest;

    if (port_LightsWord(&text, end, &rest) != 0) {
        return FALSE;
    }
    if (port_LightsWordIs(word, length, "yes") ||
        port_LightsWordIs(word, length, "on")) {
        *out = TRUE;
        return TRUE;
    }
    if (port_LightsWordIs(word, length, "no") ||
        port_LightsWordIs(word, length, "off")) {
        *out = FALSE;
        return TRUE;
    }
    return FALSE;
}

/* Where a line's comment begins, or `end`: a `#` at the line's start or
 * after a space, unless it is a colour (`#RRGGBB` and then a space or the
 * end). */
static const char *port_LightsComment(const char *at, const char *end) {
    const char *hash;

    for (hash = at; hash < end; hash++) {
        s32 digits = 0;

        if (*hash != '#' || (hash > at && !port_LightsIsSpace(hash[-1]))) {
            continue;
        }
        while (digits < COLOR_DIGITS && hash + 1 + digits < end &&
               port_LightsHexDigit(hash[1 + digits]) >= 0) {
            digits++;
        }
        if (digits == COLOR_DIGITS &&
            (hash + COLOR_TEXT_LENGTH == end ||
             port_LightsIsSpace(hash[COLOR_TEXT_LENGTH]))) {
            continue; /* a colour */
        }
        return hash;
    }
    return end;
}

/* ---- a light file ---- */

void port_LightDefault(PortLightDef *light, const char *name, u32 length) {
    u32 i;
    u8 *bytes = (u8 *)light;

    for (i = 0; i < sizeof(*light); i++) {
        bytes[i] = 0;
    }
    for (i = 0; i < length && i + 1 < PORT_LIGHT_NAME_SIZE; i++) {
        light->name[i] = port_LightsLower(name[i]);
    }
    light->kind = PORT_LIGHT_POINT;
    light->on = PORT_LIGHT_ON_STAGE;
    light->on_bone = -1;
    light->color[0] = COLOR_MOST;
    light->color[1] = COLOR_MOST;
    light->color[2] = COLOR_MOST;
    light->intensity = PORT_LIGHT_ONE / 2; /* a lantern's, not the sun's */
    light->radius = DEFAULT_RADIUS;
    light->direction[1] = 1; /* down: the world's y runs down */
    light->cone_inner = DEFAULT_CONE_INNER;
    light->cone_outer = DEFAULT_CONE_OUTER;
}

/* The light of this name in the set, or -1. */
static s32 port_LightsNamed(
    const PortLightSet *set, const char *name, u32 length) {
    s32 i;

    for (i = 0; i < set->count; i++) {
        if (port_LightsWordIs(name, length, set->light[i].name)) {
            return i;
        }
    }
    return -1;
}

static void port_LightsForget(PortLightSet *set, s32 at) {
    s32 i;

    for (i = at; i + 1 < set->count; i++) {
        set->light[i] = set->light[i + 1];
    }
    set->count--;
}

/* `on = ...`: what the light is fastened to. NULL, or what is wrong. */
static const char *port_LightsOn(
    const char *text, const char *end, PortLightDef *light) {
    const char *word;
    u32 length = port_LightsWord(&text, end, &word);
    const char *number;
    u32 number_length;
    s32 value;

    if (port_LightsWordIs(word, length, "stage")) {
        light->on = PORT_LIGHT_ON_STAGE;
        light->on_index = 0;
        return port_LightsWord(&text, end, &word) == 0
                   ? NULL
                   : "`on = stage` alone";
    }
    if (port_LightsWordIs(word, length, "flames")) {
        light->on = PORT_LIGHT_ON_FLAME;
        light->on_index = -1;
        return port_LightsWord(&text, end, &word) == 0
                   ? NULL
                   : "`on = flames` alone";
    }
    if (port_LightsWordIs(word, length, "fighter")) {
        light->on = PORT_LIGHT_ON_FIGHTER;
    } else if (port_LightsWordIs(word, length, "weapon")) {
        light->on = PORT_LIGHT_ON_WEAPON;
    } else if (port_LightsWordIs(word, length, "piece")) {
        light->on = PORT_LIGHT_ON_PIECE;
    } else if (port_LightsWordIs(word, length, "flame")) {
        light->on = PORT_LIGHT_ON_FLAME;
    } else {
        return "`on` is stage, flames, fighter N, weapon N, piece N or flame N";
    }
    number_length = port_LightsWord(&text, end, &number);
    if (number_length == 0 ||
        !port_LightsFraction(number, number_length, &value) ||
        value % PORT_LIGHT_ONE != 0) {
        return "`on` names a thing and its number";
    }
    value /= PORT_LIGHT_ONE;
    /* a fighter is 1 or 2 in the file; the rest count from 0 */
    if (light->on == PORT_LIGHT_ON_FIGHTER) {
        if (value < 1 || value > FIGHTERS) {
            return "a fighter is 1 or 2";
        }
        value--;
    } else if (value < 0) {
        return "a number of `on` is 0 or more";
    }
    light->on_index = value;
    light->on_bone = -1;
    length = port_LightsWord(&text, end, &word);
    if (length == 0) {
        return NULL;
    }
    if (light->on != PORT_LIGHT_ON_FIGHTER ||
        !port_LightsWordIs(word, length, "bone")) {
        return "only a fighter has `bone N` after it";
    }
    number_length = port_LightsWord(&text, end, &number);
    if (number_length == 0 ||
        !port_LightsFraction(number, number_length, &value) ||
        value % PORT_LIGHT_ONE != 0 || value < 0 ||
        value >= 21 * PORT_LIGHT_ONE) {
        return "a bone is 0 to 20";
    }
    light->on_bone = value / PORT_LIGHT_ONE;
    return port_LightsWord(&text, end, &word) == 0
               ? NULL
               : "nothing after the bone";
}

/* `times`: one or more of dawn, day, dusk and night, or `always` (0). */
static const char *port_LightsTimes(
    const char *value, const char *end, u8 *times) {
    static const char * const names[4] = {"dawn", "day", "dusk", "night"};
    const char *word;
    u32 length;
    u32 i;
    u8 out = 0;
    s32 words = 0;

    while ((length = port_LightsWord(&value, end, &word)) != 0) {
        words++;
        if (port_LightsWordIs(word, length, "always")) {
            continue;
        }
        for (i = 0; i < 4; i++) {
            if (port_LightsWordIs(word, length, names[i])) {
                out |= (u8)(1 << i);
                break;
            }
        }
        if (i == 4) {
            return "`times` is dawn, day, dusk, night or always";
        }
    }
    if (words == 0) {
        return "`times` is dawn, day, dusk, night or always";
    }
    *times = out;
    return NULL;
}

/* `name = value` of a light: NULL, or what is wrong with it. */
static const char *port_LightsSet(
    PortLightDef *light, const char *name, u32 name_length, const char *value,
    const char *end) {
    s32 numbers[3];
    s32 fraction;
    const char *word;
    u32 length;

    if (port_LightsWordIs(name, name_length, "kind")) {
        length = port_LightsWord(&value, end, &word);
        if (port_LightsWordIs(word, length, "point")) {
            light->kind = PORT_LIGHT_POINT;
        } else if (port_LightsWordIs(word, length, "spot")) {
            light->kind = PORT_LIGHT_SPOT;
        } else {
            return "`kind` is point or spot";
        }
        return port_LightsWord(&value, end, &word) == 0
                   ? NULL
                   : "`kind` is one word";
    }
    if (port_LightsWordIs(name, name_length, "at")) {
        if (!port_LightsWholes(value, end, light->at, 3)) {
            return "`at` is three numbers: x y z";
        }
        return NULL;
    }
    if (port_LightsWordIs(name, name_length, "on")) {
        return port_LightsOn(value, end, light);
    }
    if (port_LightsWordIs(name, name_length, "color") ||
        port_LightsWordIs(name, name_length, "colour")) {
        return port_LightsColor(value, end, light->color)
                   ? NULL
                   : "`color` is #RRGGBB or three numbers of 0 to 255";
    }
    if (port_LightsWordIs(name, name_length, "intensity")) {
        if (!port_LightsOneFraction(value, end, &fraction) || fraction <= 0) {
            return "`intensity` is a number above 0";
        }
        light->intensity = fraction;
        return NULL;
    }
    if (port_LightsWordIs(name, name_length, "radius")) {
        if (!port_LightsWholes(value, end, numbers, 1) || numbers[0] <= 0) {
            return "`radius` is a number of world units above 0";
        }
        light->radius = numbers[0];
        return NULL;
    }
    if (port_LightsWordIs(name, name_length, "direction")) {
        if (!port_LightsWholes(value, end, numbers, 3) ||
            (numbers[0] == 0 && numbers[1] == 0 && numbers[2] == 0)) {
            return "`direction` is three numbers, not all 0";
        }
        light->direction[0] = numbers[0];
        light->direction[1] = numbers[1];
        light->direction[2] = numbers[2];
        return NULL;
    }
    if (port_LightsWordIs(name, name_length, "cone")) {
        if (!port_LightsWholes(value, end, numbers, 2) || numbers[0] < 0 ||
            numbers[0] > numbers[1] || numbers[1] > CONE_MOST ||
            numbers[1] == 0) {
            return "`cone` is two angles, the inner and the outer, 0 to 90";
        }
        light->cone_inner = numbers[0];
        light->cone_outer = numbers[1];
        return NULL;
    }
    if (port_LightsWordIs(name, name_length, "flicker")) {
        if (!port_LightsOneFraction(value, end, &fraction) || fraction < 0 ||
            fraction > PORT_LIGHT_ONE) {
            return "`flicker` is a number of 0 to 1";
        }
        light->flicker = fraction;
        return NULL;
    }
    if (port_LightsWordIs(name, name_length, "shadow")) {
        return port_LightsYesOrNo(value, end, &light->shadow)
                   ? NULL
                   : "`shadow` is yes or no";
    }
    if (port_LightsWordIs(name, name_length, "times")) {
        return port_LightsTimes(value, end, &light->times);
    }
    return "not a word a light has";
}

/* One file over the set, line by line (port_lights.h). */
s32 port_LightsParse(
    const char *text, u32 length, PortLightSet *set, char *problem, u32 room) {
    const char *at = text;
    const char *all_end = text + length;
    s32 problems = 0;
    s32 line = 0;
    s32 current = -1;        /* the light the lines are of */
    s32 is_skipping = FALSE; /* a light that could not be had */

    if (problem != NULL && room > 0) {
        problem[0] = TEXT_END;
    }
    while (at < all_end) {
        const char *end = at;
        const char *word;
        const char *why = NULL;
        u32 word_length;
        const char *rest;

        while (end < all_end && *end != LINE_END) {
            end++;
        }
        line++;
        rest = at;
        /* a comment ends the line */
        end = port_LightsComment(at, end);
        word_length = port_LightsWord(&rest, end, &word);
        if (word_length == 0) {
            while (end < all_end && *end != LINE_END) {
                end++;
            }
            at = end + 1;
            continue;
        }
        if (word[0] == '[') {
            /* `[light NAME]` or `[NAME]`: a light, new or of the same name */
            const char *close = word;
            const char *name;
            u32 name_length;

            while (close < end && *close != ']') {
                close++;
            }
            rest = word + 1;
            name_length = port_LightsWord(&rest, close, &name);
            if (name_length > 0 &&
                port_LightsWordIs(name, name_length, "light")) {
                name_length = port_LightsWord(&rest, close, &name);
            }
            {
                const char *more;

                if (close >= end || name_length == 0 ||
                    port_LightsWord(&rest, close, &more) != 0 ||
                    name_length + 1 > PORT_LIGHT_NAME_SIZE) {
                    why = "a light begins `[light NAME]`, the name one word "
                          "of at most 31 letters";
                    current = -1;
                    is_skipping = TRUE;
                } else {
                    current = port_LightsNamed(set, name, name_length);
                    is_skipping = FALSE;
                    if (current < 0 && set->count >= PORT_LIGHTS_MOST) {
                        why = "more lights than a stage holds (64)";
                        is_skipping = TRUE;
                    } else if (current < 0) {
                        current = set->count++;
                        port_LightDefault(
                            &set->light[current], name, name_length);
                    }
                }
            }
        } else if (port_LightsWordIs(word, word_length, "clear")) {
            if (port_LightsWord(&rest, end, &word) != 0) {
                why = "`clear` alone";
            } else {
                set->count = 0;
                current = -1;
                is_skipping = FALSE;
            }
        } else if (port_LightsWordIs(word, word_length, "remove")) {
            const char *name;
            u32 name_length = port_LightsWord(&rest, end, &name);
            const char *more;
            s32 which;

            if (name_length == 0 || port_LightsWord(&rest, end, &more) != 0) {
                why = "`remove NAME`";
            } else {
                which = port_LightsNamed(set, name, name_length);
                if (which < 0) {
                    why = "no light of that name to remove";
                } else {
                    port_LightsForget(set, which);
                    current = -1;
                    is_skipping = FALSE;
                }
            }
        } else {
            /* `name = value` */
            const char *equals = word;
            const char *value;
            u32 name_length;

            while (equals < end && *equals != '=') {
                equals++;
            }
            name_length = 0;
            while (word + name_length < equals &&
                   !port_LightsIsSpace(word[name_length])) {
                name_length++;
            }
            value = equals + 1;
            if (equals >= end) {
                why = "not `name = value`, `[light NAME]`, `clear` nor "
                      "`remove NAME`";
            } else if (port_LightsWordIs(word, name_length, "format")) {
                s32 format;

                if (!port_LightsWholes(value, end, &format, 1) ||
                    format != PORT_LIGHTS_FORMAT) {
                    why = "`format = 1`: this program reads no other";
                }
            } else if (is_skipping) {
                /* the light's own line said what is wrong */
            } else if (current < 0) {
                why = "a light's line before any `[light NAME]`";
            } else {
                why = port_LightsSet(
                    &set->light[current], word, name_length, value, end);
            }
        }
        if (why != NULL) {
            if (problems == 0) {
                port_LightsSay(problem, room, "line %d: %s", line, why);
            }
            problems++;
        }
        at = end;
        while (at < all_end && *at != LINE_END) {
            at++;
        }
        at++;
    }
    return problems;
}

/* ---- a stage's set ---- */

void port_LightsStageKey(s32 stage, char *key, u32 room) {
    port_LightsSay(key, room, "stage_%02d", stage);
}

/* The stage's set, or a custom arena's (port_lights.h). */
const PortLightSet *port_LightsOfStage(s32 stage) {
    char key[PORT_MOD_KEY_SIZE];
    char problem[PROBLEM_SIZE];
    const char *arena = port_ArenaLightsKey();
    const char *path;
    s32 place;
    u32 i;

    /* a stage fought Classic has none of its dressing: no lights of its
     * own (port_stage_choice.h) */
    if (stage < 0 || !port_StageIsDressed()) {
        return NULL;
    }
    if (arena != NULL) {
        for (i = 0; i + 1 < sizeof(key) && arena[i] != TEXT_END; i++) {
            key[i] = port_LightsLower(arena[i]);
        }
        key[i] = TEXT_END;
    } else {
        port_LightsStageKey(stage, key, sizeof(key));
    }
    if (g_port_lights.has_stage && stage == g_port_lights.stage &&
        port_TextsEqual(key, g_port_lights.key)) {
        return &g_port_lights.set;
    }
    g_port_lights.has_stage = TRUE;
    g_port_lights.stage = stage;
    for (i = 0; i < sizeof(key); i++) {
        g_port_lights.key[i] = key[i];
    }
    g_port_lights.set.count = 0;
    for (place = 0;
         (path = port_ModFindEach(PORT_MOD_LIGHTS, key, place)) != NULL;
         place++) {
        u32 size = port_ModFileSize(path);
        u32 got;
        s32 problems;

        if (size > FILE_BYTES_MAX) {
            port_Log("lights: %s is longer than %d bytes: left out\n", path,
                     FILE_BYTES_MAX);
            continue;
        }
        got = port_ModReadFile(path, g_port_lights.text, FILE_BYTES_MAX);
        problems = port_LightsParse(g_port_lights.text, got, &g_port_lights.set,
                                    problem, sizeof(problem));
        if (problems > 0) {
            port_Log(
                "lights: %s: %d lines not used; %s\n", path, problems, problem);
        }
    }
    if (place > 0) {
        port_Log("lights: stage %d has %d lights of %d files of %s\n", stage,
                 g_port_lights.set.count, place, key);
    }
    return &g_port_lights.set;
}

/* ---- what the game lights up ---- */

static void port_LightsCopyPlace(s32 *out, const void *position) {
    const s32 *from = (const s32 *)position;

    out[0] = from[0];
    out[1] = from[1];
    out[2] = from[2];
}

void port_LightsNoteEffects(void) {
    g_port_lights.game.serial++;
    g_port_lights.game.glow_count = 0;
}

/* The game's point light, and whether it is a new one. */
void port_LightsNoteFlash(const void *position, s32 color, s32 frames) {
    PortLightsGame *game = &g_port_lights.game;

    if (frames <= 0) {
        game->flash_on = FALSE;
        game->flash_frames = 0;
        return;
    }
    /* a new one: more frames left than the last had, or a new place */
    if (!game->flash_on || frames > game->flash_frames ||
        game->flash_at[0] != ((const s32 *)position)[0] ||
        game->flash_at[2] != ((const s32 *)position)[2]) {
        game->flash_event++;
    }
    game->flash_on = TRUE;
    port_LightsCopyPlace(game->flash_at, position);
    game->flash_color = color;
    game->flash_frames = frames;
}

/* A glowing quad, once a frame however often it is drawn. */
void port_LightsNoteGlow(s32 flash, const void *position) {
    PortLightsGame *game = &g_port_lights.game;
    s32 i;

    for (i = 0; i < game->glow_count; i++) {
        if (game->glow[i].flash == flash) {
            return; /* drawn twice: the same glow */
        }
    }
    if (game->glow_count >= PORT_LIGHTS_GLOWS) {
        return;
    }
    game->glow[game->glow_count].flash = flash;
    port_LightsCopyPlace(game->glow[game->glow_count].at, position);
    game->glow_count++;
}

/* The stage's flames as eff_DrawFlames reads them, told the log when
 * they change. */
void port_LightsNoteFlames(const void *flames) {
    const Flame *flame = (const Flame *)flames;
    PortLightsGame *game = &g_port_lights.game;
    s32 count = 0;
    s32 stage = resource_GetStage();

    while (
        count < PORT_LIGHTS_FLAMES && (flame[count].x_bits & FLAME_PRESENT)) {
        PortLightFlame *out = &game->flame[count];

        /* as eff_DrawFlames divides them */
        out->at[0] = flame[count].x_bits / FLAME_X_SCALE;
        out->at[1] = flame[count].y_bits / FLAME_Y_SCALE;
        out->at[2] = flame[count].z;
        out->kind = flame[count].y_bits & FLAME_KIND_MASK;
        count++;
    }
    if (stage != game->flame_stage || count != game->flame_count) {
        s32 i;

        for (i = 0; i < count; i++) {
            port_Log("lights: stage %d's flame %d, kind %d, at %d %d %d\n",
                     stage, i, game->flame[i].kind, game->flame[i].at[0],
                     game->flame[i].at[1], game->flame[i].at[2]);
        }
    }
    game->flame_stage = stage;
    game->flame_count = count;
}

const PortLightsGame *port_LightsGame(void) { return &g_port_lights.game; }
