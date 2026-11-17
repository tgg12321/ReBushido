/* The native port's weapons as data (port_weapons.h; docs/MODS.md,
 * "Weapons and motions"): the mods' `weapons/NAME.txt` read and checked as
 * the program starts, the run's `--weapon`s, and the answers to what the
 * game asks of a fighter's weapon. With no mod's weapon every answer is
 * the game's own value, given back as it came.
 *
 * Nothing here is the game's state (config/port_state.txt): the weapons
 * are read once and only read after, and which one a fighter has is
 * worked out from the game's state each time it is asked. */
#include "common.h"
#include "port_host.h"
#include "port_mods.h"
#include "port_weapons.h"
#include "bb2.h"

#define TEXT_END 0
#define LINE_VALUE_SIZE 128
#define PROBLEM_SIZE 160
/* a weapon's file is not larger than this */
#define FILE_BYTES_MAX 0x4000
/* a point's coordinates, from the hand: the yari's tip is 1,930 out */
#define POINT_MAX 8192
/* the CPU's numbers (cpu_data.c): a reach is kept in units of 2 and a
 * range in units of 8, signed */
#define CPU_REACH_UNIT 2
#define CPU_REACH_MAX (255 * CPU_REACH_UNIT)
#define CPU_RANGE_UNIT 8
#define CPU_RANGE_MIN (-128 * CPU_RANGE_UNIT)
#define CPU_RANGE_MAX (127 * CPU_RANGE_UNIT)
/* the stances a fight can begin in: the first, second or third */
#define STANCE_LAST 2
/* a factor's fraction: so many digits after the point at most */
#define FRACTION_DIGITS_MAX 4
#define DECIMAL 10
#define FNV_OFFSET 0xCBF29CE484222325ULL
#define FNV_PRIME 0x100000001B3ULL
#define CLANS 2

static const char * const g_port_weapon_id_names[WEAPON_ID_COUNT] = {
    "katana",   "long_sword", "nodachi",  "broadsword",
    "naginata", "yari",       "long_gun", "short_gun"};

static struct {
    s32 count;
    PortWeapon weapons[PORT_WEAPONS_MAX];
    /* the one that stands in for each of the game's weapons, or NULL */
    const PortWeapon *replacing[WEAPON_ID_COUNT];
    /* `--weapon`: what each side was told, and the weapon it is */
    char chosen_key[PORT_WEAPON_SIDES][PORT_WEAPON_KEY_SIZE];
    const PortWeapon *chosen[PORT_WEAPON_SIDES];
} g_port_weapons;

/* ---- text ---- */

static void port_WeaponSay(char *out, u32 room, const char *format, ...) {
    PortArgs args;

    if (out == NULL || room == 0) {
        return;
    }
    PORT_ARGS_START(args, format);
    port_FormatText(out, room, format, args);
    PORT_ARGS_END(args);
}

static char port_WeaponLower(char character) {
    return character >= 'A' && character <= 'Z' ? (char)(character - 'A' + 'a')
                                                : character;
}

static s32 port_WeaponIsSpace(char character) {
    return character == ' ' || character == '\t' || character == '\r' ||
           character == '\n';
}

/* Whether the `length` characters at `text` are `word`, letters of
 * either case, `-` the same as `_`. */
static s32 port_WeaponWordIs(const char *text, u32 length, const char *word) {
    u32 i;

    for (i = 0; i < length; i++) {
        char character = port_WeaponLower(text[i]);

        if (character == '-') {
            character = '_';
        }
        if (word[i] == TEXT_END || character != word[i]) {
            return FALSE;
        }
    }
    return word[length] == TEXT_END;
}

static void port_WeaponCopy(char *out, u32 room, const char *text, u32 length) {
    u32 i;

    for (i = 0; i < length && i + 1 < room; i++) {
        out[i] = text[i];
    }
    out[i] = TEXT_END;
}

s32 port_WeaponIdOf(const char *name, u32 length) {
    s32 id;

    for (id = 0; id < WEAPON_ID_COUNT; id++) {
        if (port_WeaponWordIs(name, length, g_port_weapon_id_names[id])) {
            return id;
        }
    }
    return -1;
}

const char *port_WeaponIdName(s32 weapon_id) {
    return weapon_id >= 0 && weapon_id < WEAPON_ID_COUNT
               ? g_port_weapon_id_names[weapon_id]
               : "none";
}

/* The whole numbers of a value, separated by spaces or commas, into
 * `numbers` (`room` of them): how many, or -1 for something that is not
 * one, or for more than there is room for. */
static s32 port_WeaponNumbers(
    const char *value, u32 length, s32 *numbers, s32 room) {
    char word[LINE_VALUE_SIZE];
    s32 count = 0;
    u32 at = 0;

    while (at < length) {
        u32 start;

        while (at < length &&
               (port_WeaponIsSpace(value[at]) || value[at] == ',')) {
            at++;
        }
        if (at == length) {
            break;
        }
        start = at;
        while (at < length && !port_WeaponIsSpace(value[at]) &&
               value[at] != ',') {
            at++;
        }
        if (count >= room || at - start >= sizeof(word)) {
            return -1;
        }
        port_WeaponCopy(word, sizeof(word), value + start, at - start);
        if (!port_ParseNumber(word, &numbers[count])) {
            return -1;
        }
        count++;
    }
    return count;
}

/* A factor such as `1`, `1.25` or `0.875`, as so many
 * PORT_WEAPON_SCALE_ONEs (rounded down): TRUE, or FALSE for anything
 * else. */
static s32 port_WeaponFactor(const char *value, u32 length, s32 *factor) {
    u32 whole = 0;
    u32 fraction = 0;
    u32 scale = 1;
    u32 digits = 0;
    u32 at = 0;

    if (length == 0) {
        return FALSE;
    }
    while (at < length && value[at] >= '0' && value[at] <= '9') {
        whole = whole * DECIMAL + (u32)(value[at] - '0');
        if (whole > PORT_WEAPON_SCALE_MAX / PORT_WEAPON_SCALE_ONE) {
            return FALSE;
        }
        at++;
        digits++;
    }
    if (at < length && value[at] == '.') {
        at++;
        while (at < length && value[at] >= '0' && value[at] <= '9') {
            if (scale >= 10000) {
                return FALSE;
            }
            fraction = fraction * DECIMAL + (u32)(value[at] - '0');
            scale *= DECIMAL;
            at++;
            digits++;
        }
    }
    if (at != length || digits == 0) {
        return FALSE;
    }
    *factor = (s32)(whole * PORT_WEAPON_SCALE_ONE +
                    fraction * PORT_WEAPON_SCALE_ONE / scale);
    return TRUE;
}

/* ---- a weapon's file ---- */

/* the names a file may say, each once */
enum {
    SAID_FORMAT = 1 << 0,
    SAID_NAME = 1 << 1,
    SAID_BASE = 1 << 2,
    SAID_REPLACES = 1 << 3,
    SAID_POINTS = 1 << 4,
    SAID_SPEED = 1 << 5,
    SAID_POWER = 1 << 6,
    SAID_MODELS = 1 << 7,
    SAID_PRACTICE = 1 << 8,
    SAID_STANCE = 1 << 9,
    SAID_CPU_REACH = 1 << 10,
    SAID_CPU_RANGE = 1 << 11
};

/* Whether `model` is one a weapon in a hand can be: one of the models the
 * game gives a weapon of the same sort as `base`, blade or gun (in team
 * battles and practice, `practice`). Every other model is a sub-weapon's,
 * a scabbard's or the story's, which the game loads otherwise. */
static s32 port_WeaponModelFits(s32 base, s32 practice, s32 model) {
    s32 weapon;
    s32 clan;

    for (weapon = 0; weapon < WEAPON_ID_COUNT; weapon++) {
        if (WEAPON_IS_GUN(weapon) != WEAPON_IS_GUN(base)) {
            continue;
        }
        if (practice) {
            if (g_fighter_practice_weapon_models[weapon] == model) {
                return TRUE;
            }
            continue;
        }
        for (clan = 0; clan < CLANS; clan++) {
            if (g_fighter_clan_weapon_models[weapon][clan] == model) {
                return TRUE;
            }
        }
    }
    return FALSE;
}

/* One line's name and value into the weapon: NULL, or why it cannot be. */
static const char *port_WeaponTakeLine(PortWeapon *weapon, u32 *said,
                                       const char *name, u32 name_length,
                                       const char *value, u32 value_length) {
    s32 numbers[PORT_WEAPON_POINTS_MAX * 3 + 1];
    u32 bit;
    s32 count;
    s32 i;

    if (port_WeaponWordIs(name, name_length, "format")) {
        bit = SAID_FORMAT;
    } else if (port_WeaponWordIs(name, name_length, "name")) {
        bit = SAID_NAME;
    } else if (port_WeaponWordIs(name, name_length, "base")) {
        bit = SAID_BASE;
    } else if (port_WeaponWordIs(name, name_length, "replaces")) {
        bit = SAID_REPLACES;
    } else if (port_WeaponWordIs(name, name_length, "points")) {
        bit = SAID_POINTS;
    } else if (port_WeaponWordIs(name, name_length, "speed")) {
        bit = SAID_SPEED;
    } else if (port_WeaponWordIs(name, name_length, "power")) {
        bit = SAID_POWER;
    } else if (port_WeaponWordIs(name, name_length, "models")) {
        bit = SAID_MODELS;
    } else if (port_WeaponWordIs(name, name_length, "practice_model")) {
        bit = SAID_PRACTICE;
    } else if (port_WeaponWordIs(name, name_length, "default_stance")) {
        bit = SAID_STANCE;
    } else if (port_WeaponWordIs(name, name_length, "cpu_reach")) {
        bit = SAID_CPU_REACH;
    } else if (port_WeaponWordIs(name, name_length, "cpu_range")) {
        bit = SAID_CPU_RANGE;
    } else if (port_WeaponWordIs(name, name_length, "author") ||
               port_WeaponWordIs(name, name_length, "about")) {
        return NULL;
    } else {
        return "a name a weapon's file does not have";
    }
    if (*said & bit) {
        return "said twice";
    }
    *said |= bit;
    count = port_WeaponNumbers(value, value_length, numbers,
                               sizeof(numbers) / sizeof(numbers[0]));
    switch (bit) {
    case SAID_FORMAT:
        if (count != 1 || numbers[0] < 1) {
            return "format takes the format's number, 1";
        }
        if (numbers[0] > PORT_WEAPON_FORMAT) {
            return "a later format than this program reads (1): a later "
                   "ReBushido is needed";
        }
        weapon->format = numbers[0];
        break;
    case SAID_NAME:
        port_WeaponCopy(weapon->name, sizeof(weapon->name), value,
                        value_length);
        break;
    case SAID_BASE:
        weapon->base = port_WeaponIdOf(value, value_length);
        if (weapon->base < 0) {
            return "base is one of katana, long_sword, nodachi, broadsword, "
                   "naginata, yari, long_gun, short_gun";
        }
        break;
    case SAID_REPLACES:
        if (port_WeaponWordIs(value, value_length, "yes")) {
            weapon->replaces = TRUE;
        } else if (port_WeaponWordIs(value, value_length, "no")) {
            weapon->replaces = FALSE;
        } else {
            return "replaces is yes or no";
        }
        break;
    case SAID_POINTS:
        if (count < 6 || count % 3 != 0) {
            return "points takes x y z of each point, from the tip to the "
                   "hand";
        }
        for (i = 0; i < count; i++) {
            if (numbers[i] < -POINT_MAX || numbers[i] > POINT_MAX) {
                return "a point's x, y and z are from -8192 to 8192";
            }
        }
        weapon->point_count = count / 3;
        for (i = 0; i < weapon->point_count; i++) {
            weapon->points[i].vx = (s16)numbers[i * 3];
            weapon->points[i].vy = (s16)numbers[i * 3 + 1];
            weapon->points[i].vz = (s16)numbers[i * 3 + 2];
            weapon->points[i].pad = 0;
        }
        break;
    case SAID_SPEED:
    case SAID_POWER:
        if (!port_WeaponFactor(value, value_length, &i) ||
            i < PORT_WEAPON_SCALE_MIN || i > PORT_WEAPON_SCALE_MAX) {
            return "speed and power are factors such as 1.25, from 0.0625 "
                   "to 4";
        }
        if (bit == SAID_SPEED) {
            weapon->speed = i;
        } else {
            weapon->power = i;
        }
        break;
    case SAID_MODELS:
        if (count != CLANS) {
            return "models takes two models' numbers, the Narukagami's and "
                   "the Shainto's";
        }
        weapon->models[0] = numbers[0];
        weapon->models[1] = numbers[1];
        break;
    case SAID_PRACTICE:
        if (count != 1) {
            return "practice_model takes a model's number";
        }
        weapon->practice_model = numbers[0];
        break;
    case SAID_STANCE:
        if (count != 1 || numbers[0] < 0 || numbers[0] > STANCE_LAST) {
            return "default_stance is 0, 1 or 2";
        }
        weapon->default_stance = numbers[0];
        break;
    case SAID_CPU_REACH:
        if (count != 1 || numbers[0] < 0 || numbers[0] > CPU_REACH_MAX ||
            numbers[0] % CPU_REACH_UNIT != 0) {
            return "cpu_reach is an even number from 0 to 510";
        }
        weapon->cpu_reach = numbers[0];
        break;
    default:
        if (count != 1 || numbers[0] < CPU_RANGE_MIN ||
            numbers[0] > CPU_RANGE_MAX || numbers[0] % CPU_RANGE_UNIT != 0) {
            return "cpu_range is a number of eights from -1024 to 1016";
        }
        weapon->cpu_range = numbers[0];
        break;
    }
    return NULL;
}

/* What the whole file said, checked together: NULL, or why it cannot be
 * used. */
static const char *port_WeaponCheck(const PortWeapon *weapon, u32 said) {
    if (!(said & SAID_FORMAT)) {
        return "no `format = 1`";
    }
    if (!(said & SAID_BASE)) {
        return "no `base =`: the weapon whose model and motions it has";
    }
    if (weapon->point_count != 0 &&
        weapon->point_count != g_fighter_weapon_point_counts[weapon->base]) {
        return g_fighter_weapon_point_counts[weapon->base] == 2
                   ? "points: this base's weapon has two points, the tip "
                     "and the hand's end"
                   : "points: this base's weapon has three points, the "
                     "tip, where the blade meets the shaft and the butt";
    }
    if (weapon->models[0] != PORT_WEAPON_UNSET &&
        (!port_WeaponModelFits(weapon->base, FALSE, weapon->models[0]) ||
         !port_WeaponModelFits(weapon->base, FALSE, weapon->models[1]))) {
        return WEAPON_IS_GUN(weapon->base)
                   ? "models: a gun's are 46 and 47"
                   : "models: a blade's are 0 to 9 (two a weapon)";
    }
    if (weapon->practice_model != PORT_WEAPON_UNSET &&
        !port_WeaponModelFits(weapon->base, TRUE, weapon->practice_model)) {
        return "practice_model: one of 51 to 56";
    }
    return NULL;
}

s32 port_WeaponParse(const char *text, u32 length, PortWeapon *weapon,
                     char *problem, u32 room) {
    u32 said = 0;
    s32 line = 0;
    u32 at = 0;
    const char *why;

    if (problem != NULL && room > 0) {
        problem[0] = TEXT_END;
    }
    weapon->name[0] = TEXT_END;
    weapon->format = 0;
    weapon->base = -1;
    weapon->replaces = FALSE;
    weapon->point_count = 0;
    weapon->speed = PORT_WEAPON_SCALE_ONE;
    weapon->power = PORT_WEAPON_SCALE_ONE;
    weapon->models[0] = PORT_WEAPON_UNSET;
    weapon->models[1] = PORT_WEAPON_UNSET;
    weapon->practice_model = PORT_WEAPON_UNSET;
    weapon->default_stance = PORT_WEAPON_UNSET;
    weapon->cpu_reach = PORT_WEAPON_UNSET;
    weapon->cpu_range = PORT_WEAPON_CPU_UNSET;
    while (at < length) {
        u32 start = at;
        u32 end;
        u32 equals;
        u32 name_end;
        u32 value_start;

        line++;
        while (at < length && text[at] != '\n') {
            at++;
        }
        end = at;
        at++;
        /* a comment runs to the line's end */
        for (equals = start; equals < end; equals++) {
            if (text[equals] == '#') {
                end = equals;
                break;
            }
        }
        while (start < end && port_WeaponIsSpace(text[start])) {
            start++;
        }
        while (end > start && port_WeaponIsSpace(text[end - 1])) {
            end--;
        }
        if (start == end) {
            continue;
        }
        for (equals = start; equals < end && text[equals] != '='; equals++) {
        }
        name_end = equals;
        while (name_end > start && port_WeaponIsSpace(text[name_end - 1])) {
            name_end--;
        }
        value_start = equals + 1;
        while (value_start < end && port_WeaponIsSpace(text[value_start])) {
            value_start++;
        }
        if (equals == end || name_end == start) {
            why = "not NAME = VALUE";
        } else {
            why = port_WeaponTakeLine(
                weapon, &said, text + start, name_end - start,
                text + value_start, value_start < end ? end - value_start : 0);
        }
        if (why != NULL) {
            port_WeaponSay(problem, room, "line %d: %s", line, why);
            return FALSE;
        }
    }
    why = port_WeaponCheck(weapon, said);
    if (why != NULL) {
        port_WeaponSay(problem, room, "%s", why);
        return FALSE;
    }
    if (weapon->name[0] == TEXT_END) {
        port_WeaponCopy(weapon->name, sizeof(weapon->name), weapon->key,
                        port_TextLength(weapon->key));
    }
    return TRUE;
}

/* ---- the run's weapons ---- */

s32 port_WeaponsChoose(const char *side_and_key) {
    s32 side;
    u32 length;

    if ((side_and_key[0] != '1' && side_and_key[0] != '2') ||
        side_and_key[1] != '=' || side_and_key[2] == TEXT_END) {
        return FALSE;
    }
    side = side_and_key[0] - '1';
    length = port_TextLength(side_and_key + 2);
    if (length >= PORT_WEAPON_KEY_SIZE) {
        return FALSE;
    }
    port_WeaponCopy(g_port_weapons.chosen_key[side],
                    sizeof(g_port_weapons.chosen_key[side]), side_and_key + 2,
                    length);
    for (length = 0; g_port_weapons.chosen_key[side][length] != TEXT_END;
         length++) {
        g_port_weapons.chosen_key[side][length] =
            port_WeaponLower(g_port_weapons.chosen_key[side][length]);
    }
    return TRUE;
}

static void port_WeaponsRead(const char *key, const char *path) {
    static char text[FILE_BYTES_MAX];
    char problem[PROBLEM_SIZE];
    PortWeapon *weapon;
    u32 size = port_ModFileSize(path);
    u32 got;

    if (g_port_weapons.count >= PORT_WEAPONS_MAX) {
        port_Log("weapons: %s (%s): more than %d weapons; left out\n", key,
                 path, PORT_WEAPONS_MAX);
        return;
    }
    if (size > sizeof(text)) {
        port_Log("weapons: %s (%s): %u bytes, more than a weapon's file may "
                 "be (%u); left out\n",
                 key, path, size, (u32)sizeof(text));
        return;
    }
    got = port_ModReadFile(path, text, sizeof(text));
    weapon = &g_port_weapons.weapons[g_port_weapons.count];
    port_WeaponCopy(
        weapon->key, sizeof(weapon->key), key, port_TextLength(key));
    if (got != size) {
        port_Log("weapons: %s (%s) cannot be read; left out\n", key, path);
        return;
    }
    if (!port_WeaponParse(text, got, weapon, problem, sizeof(problem))) {
        port_Log("weapons: %s (%s): %s; left out\n", key, path, problem);
        return;
    }
    g_port_weapons.count++;
}

void port_WeaponsStart(void) {
    const char *key;
    const char *path;
    s32 side;
    s32 i;

    for (i = 0; port_ModFileOf(PORT_MOD_WEAPONS, i, &key, &path); i++) {
        port_WeaponsRead(key, path);
    }
    for (i = 0; i < g_port_weapons.count; i++) {
        const PortWeapon *weapon = &g_port_weapons.weapons[i];

        port_Log("weapons: %s (\"%s\"): %s %s, %d points, speed %d/4096, "
                 "power %d/4096\n",
                 weapon->key, weapon->name,
                 weapon->replaces ? "stands in for the" : "a new slot on the",
                 port_WeaponIdName(weapon->base),
                 weapon->point_count != 0
                     ? weapon->point_count
                     : g_fighter_weapon_point_counts[weapon->base],
                 weapon->speed, weapon->power);
        if (weapon->replaces) {
            if (g_port_weapons.replacing[weapon->base] != NULL) {
                port_Log("weapons: %s stands in for the %s over %s, which "
                         "comes before it\n",
                         weapon->key, port_WeaponIdName(weapon->base),
                         g_port_weapons.replacing[weapon->base]->key);
            }
            g_port_weapons.replacing[weapon->base] = weapon;
        }
    }
    for (side = 0; side < PORT_WEAPON_SIDES; side++) {
        if (g_port_weapons.chosen_key[side][0] == TEXT_END) {
            continue;
        }
        for (i = 0; i < g_port_weapons.count; i++) {
            if (port_TextsEqual(g_port_weapons.weapons[i].key,
                                g_port_weapons.chosen_key[side])) {
                g_port_weapons.chosen[side] = &g_port_weapons.weapons[i];
            }
        }
        if (g_port_weapons.chosen[side] == NULL) {
            port_Log("weapons: --weapon %d=%s: no mod has that weapon; the "
                     "fighter has the game's\n",
                     side + 1, g_port_weapons.chosen_key[side]);
        } else {
            port_Log("weapons: the fighter in place %d fights with %s when "
                     "his weapon is the %s\n",
                     side + 1, g_port_weapons.chosen[side]->key,
                     port_WeaponIdName(g_port_weapons.chosen[side]->base));
        }
    }
}

s32 port_WeaponsCount(void) { return g_port_weapons.count; }

const PortWeapon *port_WeaponAt(s32 index) {
    return index >= 0 && index < g_port_weapons.count
               ? &g_port_weapons.weapons[index]
               : NULL;
}

u64 port_WeaponsSessionHash(void) {
    u64 hash = port_ModsGameplayHash();
    s32 side;

    for (side = 0; side < PORT_WEAPON_SIDES; side++) {
        const char *key = g_port_weapons.chosen[side] != NULL
                              ? g_port_weapons.chosen[side]->key
                              : "";
        u64 one = FNV_OFFSET;

        if (*key == TEXT_END) {
            continue;
        }
        one = (one ^ (u64)(side + 1)) * FNV_PRIME;
        for (; *key != TEXT_END; key++) {
            one = (one ^ (u8)*key) * FNV_PRIME;
        }
        hash += one;
    }
    return hash;
}

/* ---- what the game asks ---- */

const PortWeapon *port_WeaponOf(s32 index, s32 weapon_id) {
    const PortWeapon *chosen;

    if (g_port_weapons.count == 0 || weapon_id < 0 ||
        weapon_id >= WEAPON_ID_COUNT) {
        return NULL;
    }
    chosen = index >= 0 && index < PORT_WEAPON_SIDES
                 ? g_port_weapons.chosen[index]
                 : NULL;
    if (chosen != NULL && chosen->base == weapon_id) {
        return chosen;
    }
    return g_port_weapons.replacing[weapon_id];
}

SVECTOR *port_WeaponPoints(s32 index, s32 weapon_id, SVECTOR *own) {
    const PortWeapon *weapon = port_WeaponOf(index, weapon_id);

    return weapon != NULL && weapon->point_count != 0
               ? (SVECTOR *)weapon->points
               : own;
}

/* A value of the game's times a factor of PORT_WEAPON_SCALE_ONE. */
static s32 port_WeaponScale(s32 own, s32 factor) {
    return factor == PORT_WEAPON_SCALE_ONE
               ? own
               : (s32)(((s64)own * factor) / PORT_WEAPON_SCALE_ONE);
}

s32 port_WeaponSpeed(s32 index, s32 weapon_id, s32 own) {
    const PortWeapon *weapon = port_WeaponOf(index, weapon_id);

    return weapon != NULL ? port_WeaponScale(own, weapon->speed) : own;
}

s32 port_WeaponPower(s32 index, s32 weapon_id, s32 own) {
    const PortWeapon *weapon = port_WeaponOf(index, weapon_id);

    return weapon != NULL ? port_WeaponScale(own, weapon->power) : own;
}

s32 port_WeaponModel(s32 index, s32 weapon_id, s32 own) {
    const PortWeapon *weapon = port_WeaponOf(index, weapon_id);
    s32 clan;

    if (weapon == NULL) {
        return own;
    }
    /* the model the game chose tells which of its own it was: a clan's,
     * team battles' and practice's, or the story's (which stays) */
    for (clan = 0; clan < CLANS; clan++) {
        if (own == g_fighter_clan_weapon_models[weapon_id][clan] &&
            weapon->models[clan] != PORT_WEAPON_UNSET) {
            return weapon->models[clan];
        }
    }
    if (own == g_fighter_practice_weapon_models[weapon_id] &&
        weapon->practice_model != PORT_WEAPON_UNSET) {
        return weapon->practice_model;
    }
    return own;
}

s32 port_WeaponDefaultStance(s32 index, s32 weapon_id, s32 own) {
    const PortWeapon *weapon = port_WeaponOf(index, weapon_id);

    return weapon != NULL && weapon->default_stance != PORT_WEAPON_UNSET
               ? weapon->default_stance
               : own;
}

s32 port_WeaponCpuReach(s32 index, s32 weapon_id, s32 own) {
    const PortWeapon *weapon = port_WeaponOf(index, weapon_id);

    return weapon != NULL && weapon->cpu_reach != PORT_WEAPON_UNSET
               ? weapon->cpu_reach
               : own;
}

s32 port_WeaponCpuRange(s32 index, s32 weapon_id, s32 own) {
    const PortWeapon *weapon = port_WeaponOf(index, weapon_id);

    return weapon != NULL && weapon->cpu_range != PORT_WEAPON_CPU_UNSET
               ? weapon->cpu_range
               : own;
}
