/* A stage's dressing (port_dressing.h; docs/MODS.md, "Dressing"): a stage's
 * dressing files read and layered in the mods' order. What the picture
 * makes of them is src/port/gl/gl_dressing.c.
 *
 * Nothing here is the game's state (config/port_state.txt: outside). */
#include "common.h"
#include "port_dressing.h"
#include "port_host.h"
#include "port_mods.h"
#include "bb2.h"

/* the largest dressing file read */
#define FILE_BYTES_MAX 0x10000
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
#define TIMES 4
/* how far from the world's middle anything may be: the collision grid's
 * half (COLLISION_GRID_HALF) and some */
#define WORLD_MOST 40000
#define DEGREES_TURN 360
/* the tallest a block's things, and how high above the ground particles
 * begin, at most */
#define HEIGHT_MOST 20000
/* the last of the stage's pieces a mesh may stand in for (gl_stage.c's
 * sixty-four) */
#define PIECE_LAST 63

/* a block as a file starts it, by kind: its count and its heights */
#define GRASS_COUNT 4000
#define GRASS_LOW 110
#define GRASS_HIGH 240
#define BUSH_COUNT 40
#define BUSH_LOW 350
#define BUSH_HIGH 700
#define REEDS_COUNT 600
#define REEDS_LOW 500
#define REEDS_HIGH 900
#define PARTICLES_COUNT 300
#define PARTICLE_SIZE 24
#define PARTICLES_TOP 3000
/* how level the ground must be, of a normal 1000 long */
#define LEVEL_LEAST 800
/* water: what shows through it, its waves */
#define WATER_CLARITY 350

static struct {
    s32 has_stage; /* whether a stage's set is kept */
    s32 stage;     /* whose */
    PortDressSet set;
    char text[FILE_BYTES_MAX];
} g_port_dressing;

static const char * const g_dress_kind_names[PORT_DRESS_KINDS] = {
    "grass", "bush", "reeds", "flowers", "water", "particles", "mesh", "wind"};

static const char * const g_particles_kind_names[PORT_PARTICLES_KINDS] = {
    "dust", "leaves", "embers", "snow", "petals", "fireflies"};

static const char * const g_dress_on_names[PORT_DRESS_ON_KINDS] = {
    "any", "green", "earth", "sand", "stone", "snow", "blue"};

/* ---- text ---- */

static void port_DressingSay(char *out, u32 room, const char *format, ...) {
    PortArgs args;

    if (out == NULL || room == 0) {
        return;
    }
    PORT_ARGS_START(args, format);
    port_FormatText(out, room, format, args);
    PORT_ARGS_END(args);
}

static s32 port_DressingIsSpace(char character) {
    return character == ' ' || character == '\t' || character == '\r';
}

static char port_DressingLower(char character) {
    return character >= 'A' && character <= 'Z' ? (char)(character - 'A' + 'a')
                                                : character;
}

/* Whether the `length` characters at `text` are `word`, of either case. */
static s32 port_DressingWordIs(const char *text, u32 length, const char *word) {
    u32 i;

    for (i = 0; i < length; i++) {
        if (word[i] == TEXT_END || port_DressingLower(text[i]) != word[i]) {
            return FALSE;
        }
    }
    return word[length] == TEXT_END;
}

/* The next word of `*text` (up to `end`), its length; *text moves past
 * it. 0 at the end. */
static u32 port_DressingWord(
    const char **text, const char *end, const char **word) {
    const char *at = *text;
    u32 length = 0;

    while (at < end && port_DressingIsSpace(*at)) {
        at++;
    }
    *word = at;
    while (at + length < end && !port_DressingIsSpace(at[length])) {
        length++;
    }
    *text = at + length;
    return length;
}

/* A number of the file, whole or with a fraction (`-1.25`), in
 * thousandths: TRUE and the number. */
static s32 port_DressingFraction(const char *text, u32 length, s32 *value) {
    u32 i = 0;
    s32 sign = 1;
    s32 whole = 0;
    s32 part = 0;
    s32 scale = PORT_DRESSING_ONE;
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
    *value = sign * (whole * PORT_DRESSING_ONE + part);
    return TRUE;
}

/* `count` whole numbers (a fraction is rounded towards nought), and
 * nothing after them. */
static s32 port_DressingWholes(
    const char *text, const char *end, s32 *out, s32 count) {
    const char *word;
    s32 i;

    for (i = 0; i < count; i++) {
        u32 length = port_DressingWord(&text, end, &word);
        s32 value;

        if (length == 0 || !port_DressingFraction(word, length, &value)) {
            return FALSE;
        }
        out[i] = value / PORT_DRESSING_ONE;
    }
    return port_DressingWord(&text, end, &word) == 0;
}

/* One fraction, in thousandths, and nothing after it. */
static s32 port_DressingOneFraction(
    const char *text, const char *end, s32 *out) {
    const char *word;
    u32 length = port_DressingWord(&text, end, &word);

    if (length == 0 || !port_DressingFraction(word, length, out)) {
        return FALSE;
    }
    return port_DressingWord(&text, end, &word) == 0;
}

static s32 port_DressingHexDigit(char character) {
    character = port_DressingLower(character);
    if (character >= '0' && character <= '9') {
        return character - '0';
    }
    if (character >= 'a' && character <= 'f') {
        return character - 'a' + TENS;
    }
    return -1;
}

/* `#RRGGBB`, or three numbers of 0 to 255. */
static s32 port_DressingColor(const char *text, const char *end, u8 *rgb) {
    const char *word;
    const char *start = text;
    u32 length = port_DressingWord(&text, end, &word);
    s32 parts[3];
    s32 i;

    if (length == COLOR_TEXT_LENGTH && word[0] == '#') {
        for (i = 0; i < 3; i++) {
            s32 high = port_DressingHexDigit(word[1 + 2 * i]);
            s32 low = port_DressingHexDigit(word[2 + 2 * i]);

            if (high < 0 || low < 0) {
                return FALSE;
            }
            rgb[i] = (u8)(high * HEX_DIGITS + low);
        }
        return port_DressingWord(&text, end, &word) == 0;
    }
    if (!port_DressingWholes(start, end, parts, 3)) {
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
static s32 port_DressingYesOrNo(const char *text, const char *end, s32 *out) {
    const char *word;
    u32 length = port_DressingWord(&text, end, &word);
    const char *rest;

    if (port_DressingWord(&text, end, &rest) != 0) {
        return FALSE;
    }
    if (port_DressingWordIs(word, length, "yes") ||
        port_DressingWordIs(word, length, "on")) {
        *out = TRUE;
        return TRUE;
    }
    if (port_DressingWordIs(word, length, "no") ||
        port_DressingWordIs(word, length, "off")) {
        *out = FALSE;
        return TRUE;
    }
    return FALSE;
}

/* Where a line's comment begins, or `end`: a `#` at the line's start or
 * after a space, unless it is a colour (`#RRGGBB` and then a space or the
 * end). */
static const char *port_DressingComment(const char *at, const char *end) {
    const char *hash;

    for (hash = at; hash < end; hash++) {
        s32 digits = 0;

        if (*hash != '#' || (hash > at && !port_DressingIsSpace(hash[-1]))) {
            continue;
        }
        while (digits < COLOR_DIGITS && hash + 1 + digits < end &&
               port_DressingHexDigit(hash[1 + digits]) >= 0) {
            digits++;
        }
        if (digits == COLOR_DIGITS &&
            (hash + COLOR_TEXT_LENGTH == end ||
             port_DressingIsSpace(hash[COLOR_TEXT_LENGTH]))) {
            continue; /* a colour */
        }
        return hash;
    }
    return end;
}

/* The place of `word` among `names`, or -1. */
static s32 port_DressingWhich(
    const char *word, u32 length, const char * const *names, s32 count) {
    s32 i;

    for (i = 0; i < count; i++) {
        if (port_DressingWordIs(word, length, names[i])) {
            return i;
        }
    }
    return -1;
}

/* ---- a block ---- */

const char *port_DressKindName(s32 kind) {
    return kind >= 0 && kind < PORT_DRESS_KINDS ? g_dress_kind_names[kind]
                                                : "?";
}

const char *port_ParticlesKindName(s32 kind) {
    return kind >= 0 && kind < PORT_PARTICLES_KINDS
               ? g_particles_kind_names[kind]
               : "?";
}

/* A block as a file starts it: every number its kind's own (port_dressing.h). */
void port_DressDefault(
    PortDressDef *block, s32 kind, const char *name, u32 length) {
    u32 i;
    u8 *bytes = (u8 *)block;

    for (i = 0; i < sizeof(*block); i++) {
        bytes[i] = 0;
    }
    for (i = 0; i < length && i + 1 < PORT_DRESSING_NAME_SIZE; i++) {
        block->name[i] = port_DressingLower(name[i]);
    }
    block->kind = kind;
    block->width = PORT_DRESSING_ONE;
    block->tint[0] = PORT_DRESSING_ONE;
    block->tint[1] = PORT_DRESSING_ONE;
    block->tint[2] = PORT_DRESSING_ONE;
    block->level = LEVEL_LEAST;
    block->sway = PORT_DRESSING_ONE;
    block->scale = PORT_DRESSING_ONE;
    block->piece = -1;
    block->clarity = WATER_CLARITY;
    block->waves = PORT_DRESSING_ONE;
    block->top = PARTICLES_TOP;
    block->strength = PORT_DRESSING_ONE / 2;
    block->gusts = PORT_DRESSING_ONE / 3;
    switch (kind) {
    case PORT_DRESS_GRASS:
    case PORT_DRESS_FLOWERS:
        block->count = GRASS_COUNT;
        block->height[0] = GRASS_LOW;
        block->height[1] = GRASS_HIGH;
        break;
    case PORT_DRESS_BUSH:
        block->count = BUSH_COUNT;
        block->height[0] = BUSH_LOW;
        block->height[1] = BUSH_HIGH;
        block->solid = TRUE; /* a bush is something to walk round */
        break;
    case PORT_DRESS_REEDS:
        block->count = REEDS_COUNT;
        block->height[0] = REEDS_LOW;
        block->height[1] = REEDS_HIGH;
        break;
    case PORT_DRESS_PARTICLES:
        block->count = PARTICLES_COUNT;
        block->height[0] = PARTICLE_SIZE;
        block->height[1] = PARTICLE_SIZE;
        break;
    case PORT_DRESS_MESH:
        block->solid = TRUE;
        break;
    default:
        break;
    }
}

/* The block of this kind and name in the set, or -1. */
static s32 port_DressingNamed(
    const PortDressSet *set, s32 kind, const char *name, u32 length) {
    s32 i;

    for (i = 0; i < set->count; i++) {
        if ((kind < 0 || set->block[i].kind == kind) &&
            port_DressingWordIs(name, length, set->block[i].name)) {
            return i;
        }
    }
    return -1;
}

static void port_DressingForget(PortDressSet *set, s32 at) {
    s32 i;

    for (i = at; i + 1 < set->count; i++) {
        set->block[i] = set->block[i + 1];
    }
    set->count--;
}

/* `times = dawn dusk`, or `always`. */
static const char *port_DressingTimes(
    const char *value, const char *end, u8 *times) {
    static const char * const names[TIMES] = {"dawn", "day", "dusk", "night"};
    const char *word;
    u32 length;
    s32 which;
    u8 out = 0;
    s32 words = 0;

    while ((length = port_DressingWord(&value, end, &word)) != 0) {
        words++;
        if (port_DressingWordIs(word, length, "always")) {
            continue;
        }
        which = port_DressingWhich(word, length, names, TIMES);
        if (which < 0) {
            return "`times` is dawn, day, dusk, night or always";
        }
        out |= (u8)(1 << which);
    }
    if (words == 0) {
        return "`times` is dawn, day, dusk, night or always";
    }
    *times = out;
    return NULL;
}

/* A rectangle `X0 Z0 X1 Z1` onto a block's list of them. */
static const char *port_DressingRectangle(
    const char *value, const char *end, s32 (*list)[4], s32 *count) {
    s32 numbers[4];
    s32 i;

    if (!port_DressingWholes(value, end, numbers, 4) ||
        numbers[0] >= numbers[2] || numbers[1] >= numbers[3]) {
        return "a rectangle is four numbers, X0 Z0 X1 Z1, the least first";
    }
    for (i = 0; i < 4; i++) {
        if (numbers[i] < -WORLD_MOST || numbers[i] > WORLD_MOST) {
            return "a rectangle lies within 40,000 of the world's middle";
        }
    }
    if (*count >= PORT_DRESSING_AREAS) {
        return "more rectangles than a block holds (8)";
    }
    for (i = 0; i < 4; i++) {
        list[*count][i] = numbers[i];
    }
    (*count)++;
    return NULL;
}

/* A whole number of `least` to `most`, alone. */
static s32 port_DressingWholeIn(
    const char *value, const char *end, s32 *out, s32 least, s32 most) {
    s32 number;

    if (!port_DressingWholes(value, end, &number, 1) || number < least ||
        number > most) {
        return FALSE;
    }
    *out = number;
    return TRUE;
}

/* A fraction of `least` to `most` thousandths, alone. */
static s32 port_DressingFractionIn(
    const char *value, const char *end, s32 *out, s32 least, s32 most) {
    s32 number;

    if (!port_DressingOneFraction(value, end, &number) || number < least ||
        number > most) {
        return FALSE;
    }
    *out = number;
    return TRUE;
}

/* `name = value` of the lines every kind has a use for: NULL, what is
 * wrong, or `""` where the name is not one of them. */
static const char *port_DressingSetShared(
    PortDressDef *block, const char *name, u32 name_length, const char *value,
    const char *end) {
    s32 numbers[3];

    if (port_DressingWordIs(name, name_length, "area")) {
        return port_DressingRectangle(
            value, end, block->area, &block->area_count);
    }
    if (port_DressingWordIs(name, name_length, "avoid")) {
        return port_DressingRectangle(
            value, end, block->avoid, &block->avoid_count);
    }
    if (port_DressingWordIs(name, name_length, "count")) {
        return port_DressingWholeIn(
                   value, end, &block->count, 0, PORT_DRESSING_COUNT_MOST)
                   ? NULL
                   : "`count` is a number of 0 to 20,000";
    }
    if (port_DressingWordIs(name, name_length, "color") ||
        port_DressingWordIs(name, name_length, "colour")) {
        const char *word;
        const char *rest = value;
        u32 length = port_DressingWord(&rest, end, &word);

        if (port_DressingWordIs(word, length, "ground") &&
            port_DressingWord(&rest, end, &word) == 0) {
            block->has_color = FALSE;
            return NULL;
        }
        if (!port_DressingColor(value, end, block->color)) {
            return "`color` is #RRGGBB, three numbers of 0 to 255, or ground";
        }
        block->has_color = TRUE;
        return NULL;
    }
    if (port_DressingWordIs(name, name_length, "tint")) {
        u8 rgb[3];
        s32 i;

        if (!port_DressingColor(value, end, rgb)) {
            return "`tint` is #RRGGBB or three numbers of 0 to 255";
        }
        /* #808080 leaves a colour as it is, #FFFFFF doubles it */
        for (i = 0; i < 3; i++) {
            block->tint[i] = (s32)rgb[i] * PORT_DRESSING_ONE / (COLOR_MOST / 2);
        }
        return NULL;
    }
    if (port_DressingWordIs(name, name_length, "times")) {
        return port_DressingTimes(value, end, &block->times);
    }
    if (port_DressingWordIs(name, name_length, "at")) {
        if (!port_DressingWholes(value, end, numbers, 3)) {
            return "`at` is three numbers: x y z";
        }
        block->at[0] = numbers[0];
        block->at[1] = numbers[1];
        block->at[2] = numbers[2];
        return NULL;
    }
    if (port_DressingWordIs(name, name_length, "sit")) {
        return port_DressingYesOrNo(value, end, &block->sit)
                   ? NULL
                   : "`sit` is yes or no";
    }
    if (port_DressingWordIs(name, name_length, "solid")) {
        return port_DressingYesOrNo(value, end, &block->solid)
                   ? NULL
                   : "`solid` is yes or no";
    }
    return "";
}

/* `on = green` of a foliage block, `on = flames` of the embers. */
static const char *port_DressingOn(
    PortDressDef *block, const char *value, const char *end) {
    const char *word;
    const char *more;
    u32 length = port_DressingWord(&value, end, &word);
    s32 which;

    if (port_DressingWord(&value, end, &more) != 0) {
        return "`on` is one word";
    }
    if (block->kind == PORT_DRESS_PARTICLES) {
        if (port_DressingWordIs(word, length, "flames")) {
            block->on_flames = TRUE;
            return NULL;
        }
        if (port_DressingWordIs(word, length, "area")) {
            block->on_flames = FALSE;
            return NULL;
        }
    }
    which =
        port_DressingWhich(word, length, g_dress_on_names, PORT_DRESS_ON_KINDS);
    if (which < 0) {
        return "`on` is any, green, earth, sand, stone, snow or blue (or a "
               "particles block's flames or area)";
    }
    block->on = which;
    return NULL;
}

/* `name = value` of a block: NULL, or what is wrong with it. */
static const char *port_DressingSet(
    PortDressDef *block, const char *name, u32 name_length, const char *value,
    const char *end) {
    s32 numbers[2];
    const char *why =
        port_DressingSetShared(block, name, name_length, value, end);

    if (why == NULL || why[0] != TEXT_END) {
        return why;
    }
    if (port_DressingWordIs(name, name_length, "height") ||
        port_DressingWordIs(name, name_length, "size")) {
        const char *rest = value;
        const char *word;
        s32 words = 0;

        while (port_DressingWord(&rest, end, &word) != 0) {
            words++;
        }
        if (words == 1 &&
            port_DressingWholeIn(value, end, numbers, 1, HEIGHT_MOST)) {
            numbers[1] = numbers[0];
        } else if (words != 2 || !port_DressingWholes(value, end, numbers, 2) ||
                   numbers[0] < 1 || numbers[0] > numbers[1] ||
                   numbers[1] > HEIGHT_MOST) {
            return "`height` is one or two numbers of world units, the "
                   "least first, up to 20,000";
        }
        block->height[0] = numbers[0];
        block->height[1] = numbers[1];
        return NULL;
    }
    if (port_DressingWordIs(name, name_length, "width")) {
        return port_DressingFractionIn(
                   value, end, &block->width, 1, 10 * PORT_DRESSING_ONE)
                   ? NULL
                   : "`width` is a number above 0, up to 10";
    }
    if (port_DressingWordIs(name, name_length, "on")) {
        return port_DressingOn(block, value, end);
    }
    if (port_DressingWordIs(name, name_length, "level")) {
        return port_DressingFractionIn(
                   value, end, &block->level, 0, PORT_DRESSING_ONE)
                   ? NULL
                   : "`level` is a number of 0 to 1";
    }
    if (port_DressingWordIs(name, name_length, "heights")) {
        if (!port_DressingWholes(value, end, numbers, 2) ||
            numbers[0] > numbers[1]) {
            return "`heights` is two numbers, the least y first";
        }
        block->has_heights = TRUE;
        block->heights[0] = numbers[0];
        block->heights[1] = numbers[1];
        return NULL;
    }
    if (port_DressingWordIs(name, name_length, "sway")) {
        return port_DressingFractionIn(
                   value, end, &block->sway, 0, 4 * PORT_DRESSING_ONE)
                   ? NULL
                   : "`sway` is a number of 0 to 4";
    }
    if (port_DressingWordIs(name, name_length, "y")) {
        return port_DressingWholeIn(
                   value, end, &block->at[1], -WORLD_MOST, WORLD_MOST)
                   ? NULL
                   : "`y` is a height in world units (y runs down)";
    }
    if (port_DressingWordIs(name, name_length, "turn")) {
        return port_DressingWholeIn(
                   value, end, &block->turn, -DEGREES_TURN, DEGREES_TURN)
                   ? NULL
                   : "`turn` is degrees, -360 to 360";
    }
    if (port_DressingWordIs(name, name_length, "scale")) {
        return port_DressingFractionIn(
                   value, end, &block->scale, 1, 100 * PORT_DRESSING_ONE)
                   ? NULL
                   : "`scale` is a number above 0, up to 100";
    }
    if (port_DressingWordIs(name, name_length, "piece")) {
        return port_DressingWholeIn(value, end, &block->piece, -1, PIECE_LAST)
                   ? NULL
                   : "`piece` is the stage's piece, 0 to 63, or -1";
    }
    if (port_DressingWordIs(name, name_length, "file")) {
        const char *word;
        const char *more;
        u32 length = port_DressingWord(&value, end, &word);
        u32 i;

        if (length == 0 || length + 1 > PORT_DRESSING_NAME_SIZE ||
            port_DressingWord(&value, end, &more) != 0) {
            return "`file` is the mesh's key, one word of at most 31 letters";
        }
        for (i = 0; i < length; i++) {
            block->file[i] = port_DressingLower(word[i]);
        }
        block->file[length] = TEXT_END;
        return NULL;
    }
    if (port_DressingWordIs(name, name_length, "clarity")) {
        return port_DressingFractionIn(
                   value, end, &block->clarity, 0, PORT_DRESSING_ONE)
                   ? NULL
                   : "`clarity` is a number of 0 to 1";
    }
    if (port_DressingWordIs(name, name_length, "waves")) {
        return port_DressingFractionIn(
                   value, end, &block->waves, 0, 4 * PORT_DRESSING_ONE)
                   ? NULL
                   : "`waves` is a number of 0 to 4";
    }
    if (port_DressingWordIs(name, name_length, "top")) {
        return port_DressingWholeIn(value, end, &block->top, 0, HEIGHT_MOST)
                   ? NULL
                   : "`top` is world units above the ground, 0 to 20,000";
    }
    if (port_DressingWordIs(name, name_length, "kind")) {
        const char *word;
        const char *more;
        u32 length = port_DressingWord(&value, end, &word);
        s32 which = port_DressingWhich(
            word, length, g_particles_kind_names, PORT_PARTICLES_KINDS);

        if (block->kind != PORT_DRESS_PARTICLES || which < 0 ||
            port_DressingWord(&value, end, &more) != 0) {
            return "`kind` is a particles block's: dust, leaves, embers, "
                   "snow, petals or fireflies";
        }
        block->subkind = which;
        return NULL;
    }
    if (port_DressingWordIs(name, name_length, "strength")) {
        return port_DressingFractionIn(
                   value, end, &block->strength, 0, 4 * PORT_DRESSING_ONE)
                   ? NULL
                   : "`strength` is a number of 0 to 4";
    }
    if (port_DressingWordIs(name, name_length, "direction")) {
        return port_DressingWholeIn(
                   value, end, &block->direction, -DEGREES_TURN, DEGREES_TURN)
                   ? NULL
                   : "`direction` is degrees, -360 to 360";
    }
    if (port_DressingWordIs(name, name_length, "gusts")) {
        return port_DressingFractionIn(
                   value, end, &block->gusts, 0, PORT_DRESSING_ONE)
                   ? NULL
                   : "`gusts` is a number of 0 to 1";
    }
    return "not a word a dressing block has";
}

/* `[KIND NAME]`: the block the lines that follow are of, new or of the same
 * kind and name; -1 and why where it cannot be had. */
static s32 port_DressingOpen(
    PortDressSet *set, const char *word, const char *end, const char **why) {
    const char *close = word;
    const char *rest = word + 1;
    const char *kind_word;
    const char *name;
    const char *more;
    u32 kind_length;
    u32 name_length;
    s32 kind;
    s32 at;

    while (close < end && *close != ']') {
        close++;
    }
    kind_length = port_DressingWord(&rest, close, &kind_word);
    name_length = port_DressingWord(&rest, close, &name);
    kind = port_DressingWhich(
        kind_word, kind_length, g_dress_kind_names, PORT_DRESS_KINDS);
    if (close >= end || kind < 0 ||
        port_DressingWord(&rest, close, &more) != 0 ||
        name_length + 1 > PORT_DRESSING_NAME_SIZE) {
        *why = "a block begins `[KIND NAME]`: grass, bush, reeds, flowers, "
               "water, particles, mesh or wind, the name one word of at "
               "most 31 letters";
        return -1;
    }
    if (name_length == 0) {
        /* `[wind]`: the one wind of the stage */
        name = kind_word;
        name_length = kind_length;
    }
    at = port_DressingNamed(set, kind, name, name_length);
    if (at >= 0) {
        return at;
    }
    if (set->count >= PORT_DRESSING_MOST) {
        *why = "more blocks than a stage holds (48)";
        return -1;
    }
    at = set->count++;
    port_DressDefault(&set->block[at], kind, name, name_length);
    return at;
}

/* `remove NAME`: the first block of the name, whatever its kind. */
static const char *port_DressingRemove(
    PortDressSet *set, const char *rest, const char *end) {
    const char *name;
    const char *more;
    u32 name_length = port_DressingWord(&rest, end, &name);
    s32 which;

    if (name_length == 0 || port_DressingWord(&rest, end, &more) != 0) {
        return "`remove NAME`";
    }
    which = port_DressingNamed(set, -1, name, name_length);
    if (which < 0) {
        return "no block of that name to remove";
    }
    port_DressingForget(set, which);
    return NULL;
}

/* One file over the set, line by line (port_dressing.h). */
s32 port_DressingParse(
    const char *text, u32 length, PortDressSet *set, char *problem, u32 room) {
    const char *at = text;
    const char *all_end = text + length;
    s32 problems = 0;
    s32 line = 0;
    s32 current = -1;        /* the block the lines are of */
    s32 is_skipping = FALSE; /* a block that could not be had */

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
        end = port_DressingComment(at, end);
        word_length = port_DressingWord(&rest, end, &word);
        if (word_length == 0) {
            /* an empty line, or a comment's */
        } else if (word[0] == '[') {
            current = port_DressingOpen(set, word, end, &why);
            is_skipping = current < 0;
        } else if (port_DressingWordIs(word, word_length, "clear")) {
            if (port_DressingWord(&rest, end, &word) != 0) {
                why = "`clear` alone";
            } else {
                set->count = 0;
                current = -1;
                is_skipping = FALSE;
            }
        } else if (port_DressingWordIs(word, word_length, "remove")) {
            why = port_DressingRemove(set, rest, end);
            if (why == NULL) {
                current = -1;
                is_skipping = FALSE;
            }
        } else {
            /* `name = value` */
            const char *equals = word;
            u32 name_length = 0;

            while (equals < end && *equals != '=') {
                equals++;
            }
            while (word + name_length < equals &&
                   !port_DressingIsSpace(word[name_length])) {
                name_length++;
            }
            if (equals >= end) {
                why = "not `name = value`, `[KIND NAME]`, `clear` nor "
                      "`remove NAME`";
            } else if (port_DressingWordIs(word, name_length, "format")) {
                s32 format;

                if (!port_DressingWholes(equals + 1, end, &format, 1) ||
                    format != PORT_DRESSING_FORMAT) {
                    why = "`format = 1`: this program reads no other";
                }
            } else if (is_skipping) {
                /* the block's own line said what is wrong */
            } else if (current < 0) {
                why = "a block's line before any `[KIND NAME]`";
            } else {
                why = port_DressingSet(
                    &set->block[current], word, name_length, equals + 1, end);
            }
        }
        if (why != NULL) {
            if (problems == 0) {
                port_DressingSay(problem, room, "line %d: %s", line, why);
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

void port_DressingStageKey(s32 stage, char *key, u32 room) {
    port_DressingSay(key, room, "stage_%02d", stage);
}

/* The stage's set (port_dressing.h). */
const PortDressSet *port_DressingOfStage(s32 stage) {
    char key[PORT_MOD_KEY_SIZE];
    char problem[PROBLEM_SIZE];
    const char *path;
    s32 place;

    /* a stage fought Classic has none of its dressing, and an arena
     * brings its own scene (port_stage_choice.h) */
    if (stage < 0 || !port_StageIsDressed() || port_ArenaInPlay() != NULL) {
        return NULL;
    }
    if (g_port_dressing.has_stage && stage == g_port_dressing.stage) {
        return &g_port_dressing.set;
    }
    g_port_dressing.has_stage = TRUE;
    g_port_dressing.stage = stage;
    g_port_dressing.set.count = 0;
    port_DressingStageKey(stage, key, sizeof(key));
    for (place = 0;
         (path = port_ModFindEach(PORT_MOD_DRESSING, key, place)) != NULL;
         place++) {
        u32 size = port_ModFileSize(path);
        u32 got;
        s32 problems;

        if (size > FILE_BYTES_MAX) {
            port_Log("dressing: %s is longer than %d bytes: left out\n", path,
                     FILE_BYTES_MAX);
            continue;
        }
        got = port_ModReadFile(path, g_port_dressing.text, FILE_BYTES_MAX);
        problems = port_DressingParse(
            g_port_dressing.text, got, &g_port_dressing.set, problem,
            sizeof(problem));
        if (problems > 0) {
            port_Log("dressing: %s: %d lines not used; %s\n", path, problems,
                     problem);
        }
    }
    if (place > 0) {
        port_Log("dressing: stage %d has %d blocks of %d files of %s\n", stage,
                 g_port_dressing.set.count, place, key);
    }
    return &g_port_dressing.set;
}
