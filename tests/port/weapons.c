/* The port's weapons as data (src/port/weapons.c; docs/MODS.md, "Weapons
 * and motions"): a weapon's file read and every way it can be wrong, a
 * folder of mods with a weapon that stands in for one of the game's, a new
 * slot chosen by --weapon and one that is left out, the answers the game
 * is given with and without them, and the hash the session compares. */
/* Everything is made up here: the mods are a tree in memory, and the
 * game's tables the weapons are checked against are the game's values,
 * written again (src/main/fight_data.c). */
#include "common.h"
#include "port_host.h"
#include "port_mods.h"
#include "port_weapons.h"
#include "src/port/text.c"
#include "src/port/mods.c"
#include "src/port/weapons.c"
#include "src/port/weapon_pick.c"
#include "runtime/runtime.h"

/* ---- the game's tables ---- */

u8 g_fighter_weapon_point_counts[WEAPON_ID_COUNT] = {2, 2, 2, 2, 3, 3, 2, 2};
u8 g_fighter_clan_weapon_models[WEAPON_ID_COUNT][2] = {
    {0, 1}, {2, 3}, {4, 5}, {6, 7}, {8, 9}, {8, 9}, {46, 47}, {46, 47}};
u8 g_fighter_practice_weapon_models[WEAPON_ID_COUNT] = {
    51, 52, 53, 54, 55, 56, 51, 51};

/* ---- the host, in memory ---- */

typedef struct {
    const char *path;
    const char *text; /* NULL for a folder */
} MadeEntry;

static const MadeEntry g_made_tree[] = {
    {"mods", NULL},
    {"mods/blades", NULL},
    {"mods/blades/weapons", NULL},
    {"mods/blades/weapons/Katana-Long.txt",
     "# a katana that reaches further\n"
     "format = 1\n"
     "name = Long katana\n"
     "base = katana\n"
     "points = 0 -60 1250,  0 0 100\n"
     "speed = 0.875   # a little slower\n"
     "power = 1.25\n"
     "models = 2 3\n"
     "default_stance = 2\n"
     "cpu_reach = 300\n"},
    {"mods/blades/weapons/heavy-yari.txt",
     "format = 1\r\n"
     "base = Yari\r\n"
     "replaces = yes\r\n"
     "power = 2\r\n"
     "cpu_range = -16\r\n"},
    {"mods/blades/weapons/broken.txt", "format = 1\nbase = naginata\n"
                                       "points = 0 0 100 0 0 50\n"},
    {"mods/blades/textures", NULL},
    {"mods/blades/textures/x.png", "not counted"},
    {"mods/blades/motions", NULL},
    {"mods/blades/motions/u123.bbm", "MW motion"},
    /* a weapon with a model and motions of its own (format 2), and one
     * whose motions are larger than the game's room for them */
    {"mods/blades/weapons/kanabo.txt",
     "format = 2\n"
     "name = Kanabo\n"
     "base = katana\n"
     "model = Kanabo\n"
     "motions = kanabo\n"},
    {"mods/blades/weapons/huge.txt", "format = 2\nbase = yari\n"
                                     "motions = huge\n"},
    {"mods/blades/motions/kanabo.bbm",
     "kanabo's motions: a file of 147 bytes, more than a motion file's head "
     "and less than the disc's largest set's file, which is 300 bytes here."
     "........"},
    {"mods/blades/motions/huge.bbm",
     "0123456789012345678901234567890123456789012345678901234567890123456789"
     "0123456789012345678901234567890123456789012345678901234567890123456789"
     "0123456789012345678901234567890123456789012345678901234567890123456789"
     "0123456789012345678901234567890123456789012345678901234567890123456789"
     "0123456789012345678901234567890123456789012345678901234567890123456789"},
    {"mods/blades/models", NULL},
    {"mods/blades/models/kanabo.glb", "a club's model"},
};
#define MADE_ENTRIES ((s32)(sizeof(g_made_tree) / sizeof(g_made_tree[0])))

/* ---- the disc's motion files, and the select screen's ---- */

static const PortDiscFile g_made_disc[] = {
    {"MOTION/", 0, 100, 2048},           {"MOTION/WIN.DAT", 7, 101, 9000},
    {"MOTION/COMMON.BBM", 7, 110, 5000}, {"MOTION/U123.BBM", 7, 120, 200},
    {"MOTION/U124.BBM", 7, 130, 300},    {"STAGE/S07.DAT", 6, 140, 900},
};

const PortDiscFile *port_DiscFileNumber(u32 index) {
    return index < sizeof(g_made_disc) / sizeof(g_made_disc[0])
               ? &g_made_disc[index]
               : NULL;
}

s32 g_disp_frame_count;
static s32 g_made_sounds;

s32 snd_Play(s32 sound, s32 right, s32 left) {
    (void)sound;
    (void)right;
    (void)left;
    g_made_sounds++;
    return 0;
}

#define MADE_FILES_MAX 8

static s32 g_made_open[MADE_FILES_MAX];

static s32 made_Find(const char *path) {
    s32 i;

    for (i = 0; i < MADE_ENTRIES; i++) {
        if (test_StringsEqual(g_made_tree[i].path, path)) {
            return i;
        }
    }
    return -1;
}

s32 port_HostOpen(const char *path) {
    s32 entry = made_Find(path);
    s32 i;

    if (entry < 0 || g_made_tree[entry].text == NULL) {
        return -1;
    }
    for (i = 0; i < MADE_FILES_MAX; i++) {
        if (g_made_open[i] == 0) {
            g_made_open[i] = entry + 1;
            return i;
        }
    }
    return -1;
}

void port_HostClose(s32 file) { g_made_open[file] = 0; }

u32 port_HostFileSize(s32 file) {
    return port_TextLength(g_made_tree[g_made_open[file] - 1].text);
}

u32 port_HostReadAt(s32 file, u32 offset, void *dest, u32 count) {
    const char *text = g_made_tree[g_made_open[file] - 1].text;
    u32 size = port_TextLength(text);
    u32 i;

    for (i = 0; i < count && offset + i < size; i++) {
        ((char *)dest)[i] = text[offset + i];
    }
    return i;
}

s32 port_HostListFolder(
    const char *path,
    void (*each)(void *context, const char *name, s32 is_folder),
    void *context) {
    u32 length = port_TextLength(path);
    s32 entry = made_Find(path);
    s32 i;

    if (entry < 0 || g_made_tree[entry].text != NULL) {
        return 0;
    }
    for (i = 0; i < MADE_ENTRIES; i++) {
        const char *other = g_made_tree[i].path;
        u32 j;

        if (port_TextLength(other) <= length + 1 || other[length] != '/') {
            continue;
        }
        for (j = 0; j < length && other[j] == path[j]; j++) {
        }
        if (j < length) {
            continue;
        }
        for (j = length + 1; other[j] != 0 && other[j] != '/'; j++) {
        }
        if (other[j] == 0) {
            each(context, other + length + 1, g_made_tree[i].text == NULL);
        }
    }
    return 1;
}

s32 port_HostMakeFolder(const char *path) { return path != NULL; }

void *port_HostAllocate(u32 size) { return test_Alloc(size); }

void port_HostWrite(s32 stream, const char *bytes, u32 count) {
    (void)stream;
    (void)bytes;
    (void)count;
}

void port_Quit(s32 code) {
    test_Print("quit %d\n", code);
    for (;;) {
    }
}

/* ---- a weapon's file ---- */

/* The file `text`: whether it is a weapon, and the problem said. */
static s32 test_Parse(
    const char *text, PortWeapon *weapon, char *problem, u32 room) {
    test_Copy(weapon->key, "test", 5);
    return port_WeaponParse(text, port_TextLength(text), weapon, problem, room);
}

/* A file that is not a weapon, and the first words of why. */
static void test_Wrong(const char *text, const char *why) {
    PortWeapon weapon;
    char problem[PROBLEM_SIZE];
    u32 length = port_TextLength(why);

    CHECK(!test_Parse(text, &weapon, problem, sizeof(problem)));
    if (!test_BytesEqual(problem, why, length)) {
        test_Print(
            "FAIL: \"%s\" says \"%s\", not \"%s...\"\n", text, problem, why);
        CHECK(FALSE);
    }
}

static void test_File(void) {
    PortWeapon weapon;
    char problem[PROBLEM_SIZE];
    s32 factor;

    CHECK(test_Parse(g_made_tree[3].text, &weapon, problem, sizeof(problem)));
    CHECK(test_StringsEqual(weapon.name, "Long katana"));
    CHECK_EQ(weapon.format, 1);
    CHECK_EQ(weapon.base, WEAPON_ID_KATANA);
    CHECK_EQ(weapon.replaces, FALSE);
    CHECK_EQ(weapon.point_count, 2);
    CHECK_EQ(weapon.points[0].vy, -60);
    CHECK_EQ(weapon.points[0].vz, 1250);
    CHECK_EQ(weapon.points[1].vz, 100);
    CHECK_EQ(weapon.speed, 3584);
    CHECK_EQ(weapon.power, 5120);
    CHECK_EQ(weapon.models[0], 2);
    CHECK_EQ(weapon.models[1], 3);
    CHECK_EQ(weapon.practice_model, PORT_WEAPON_UNSET);
    CHECK_EQ(weapon.default_stance, 2);
    CHECK_EQ(weapon.cpu_reach, 300);
    CHECK_EQ(weapon.cpu_range, PORT_WEAPON_CPU_UNSET);
    /* the least of a file: the base's numbers for the rest, its key for a
     * name */
    CHECK(test_Parse(
        "format=1\nbase=short-gun", &weapon, problem, sizeof(problem)));
    CHECK_EQ(weapon.base, WEAPON_ID_SHORT_GUN);
    CHECK_EQ(weapon.point_count, 0);
    CHECK_EQ(weapon.speed, PORT_WEAPON_SCALE_ONE);
    CHECK(test_StringsEqual(weapon.name, "test"));
    CHECK(test_Parse("format = 1\nbase = naginata\n"
                     "points = 0 0 1600  0 0 900  0 0 -800\n"
                     "practice_model = 55\nmodels = 9 8\n",
                     &weapon, problem, sizeof(problem)));
    CHECK_EQ(weapon.point_count, 3);
    CHECK_EQ(weapon.points[2].vz, -800);
    CHECK_EQ(weapon.practice_model, 55);
    /* factors */
    CHECK(port_WeaponFactor("1", 1, &factor) && factor == 4096);
    CHECK(port_WeaponFactor("0.0625", 6, &factor) && factor == 256);
    CHECK(port_WeaponFactor("4.", 2, &factor) && factor == 16384);
    CHECK(!port_WeaponFactor("1.2.3", 5, &factor));
    CHECK(!port_WeaponFactor(".", 1, &factor));
    CHECK(!port_WeaponFactor("-1", 2, &factor));
    CHECK(!port_WeaponFactor("1.00001", 7, &factor));
    /* and every way a file can be wrong, with its line */
    test_Wrong("format = 1\nbase = katana\nlength = 3\n",
               "line 3: a name a weapon's file does not have");
    test_Wrong(
        "format = 1\nbase = katana\nbase = yari\n", "line 3: said twice");
    test_Wrong("format = 1\nkatana\n", "line 2: not NAME = VALUE");
    test_Wrong("base = katana\n", "no `format = 1`");
    test_Wrong("format = 3\nbase = katana\n", "line 1: a later format");
    test_Wrong("format = 1\nbase = katana\nmodel = club\n",
               "model and motions are format 2's");
    test_Wrong("format = 2\nbase = katana\nmodel = a/club\n",
               "line 3: model takes the name");
    test_Wrong("format = 2\nbase = katana\nmotions = club.bbm\n",
               "line 3: motions takes the name");
    test_Wrong("format = 2\nbase = katana\nmotions =\n",
               "line 3: motions takes the name");
    test_Wrong("format = x\n", "line 1: format takes");
    test_Wrong("format = 1\n", "no `base =`");
    test_Wrong("format = 1\nbase = club\n", "line 2: base is one of");
    test_Wrong("format = 1\nbase = katana\nreplaces = maybe\n",
               "line 3: replaces is yes or no");
    test_Wrong("format = 1\nbase = katana\npoints = 0 0 1000 0 0\n",
               "line 3: points takes x y z");
    test_Wrong("format = 1\nbase = katana\npoints = 0 0 9000 0 0 100\n",
               "line 3: a point's x");
    test_Wrong("format = 1\nbase = katana\npoints = 0 0 9 0 0 1 0 0 -9\n",
               "points: this base's weapon has two points");
    test_Wrong("format = 1\nbase = yari\npoints = 0 0 1000 0 0 100\n",
               "points: this base's weapon has three points");
    test_Wrong("format = 1\nbase = katana\nspeed = 5\n",
               "line 3: speed and power are factors");
    test_Wrong("format = 1\nbase = katana\npower = 0.01\n",
               "line 3: speed and power are factors");
    test_Wrong(
        "format = 1\nbase = katana\nmodels = 1\n", "line 3: models takes two");
    test_Wrong("format = 1\nbase = katana\nmodels = 0 10\n",
               "models: a blade's are 0 to 9");
    test_Wrong("format = 1\nbase = long_gun\nmodels = 0 1\n",
               "models: a gun's are 46 and 47");
    test_Wrong("format = 1\nbase = katana\npractice_model = 50\n",
               "practice_model: one of 51 to 56");
    test_Wrong("format = 1\nbase = katana\ndefault_stance = 3\n",
               "line 3: default_stance is 0, 1 or 2");
    test_Wrong("format = 1\nbase = katana\ncpu_reach = 301\n",
               "line 3: cpu_reach is an even number");
    test_Wrong("format = 1\nbase = katana\ncpu_range = 12\n",
               "line 3: cpu_range is a number of eights");
}

/* ---- the run's weapons ---- */

static SVECTOR g_own_points[2] = {{0, -60, 950, 0}, {0, 0, 100, 0}};

/* With no mod's weapon every answer is the game's own. */
static void test_None(void) {
    s32 index;
    s32 weapon;

    for (index = 0; index < 2; index++) {
        for (weapon = 0; weapon < WEAPON_ID_COUNT; weapon++) {
            CHECK(port_WeaponOf(index, weapon) == NULL);
            CHECK(
                port_WeaponPoints(index, weapon, g_own_points) == g_own_points);
            CHECK_EQ(port_WeaponSpeed(index, weapon, 0x1234), 0x1234);
            CHECK_EQ(port_WeaponPower(index, weapon, -7), -7);
            CHECK_EQ(port_WeaponModel(index, weapon, 50), 50);
            CHECK_EQ(port_WeaponDefaultStance(index, weapon, 1), 1);
            CHECK_EQ(port_WeaponCpuReach(index, weapon, 418), 418);
            CHECK_EQ(port_WeaponCpuRange(index, weapon, -88), -88);
        }
    }
    CHECK_EQ(port_WeaponsSessionHash(), 0);
}

/* FNV-1a of 64 bits over a kind, a key and a file's bytes, written again
 * from port_mods.h's description. */
static u64 test_FileHash(s32 kind, const char *key, const char *text) {
    u64 hash = (0xCBF29CE484222325ULL ^ (u64)kind) * 0x100000001B3ULL;

    for (; *key != 0; key++) {
        hash = (hash ^ (u8)*key) * 0x100000001B3ULL;
    }
    for (; *text != 0; text++) {
        hash = (hash ^ (u8)*text) * 0x100000001B3ULL;
    }
    return hash;
}

/* The weapon of the run's that has this key, or NULL. */
static const PortWeapon *test_Weapon(const char *key) {
    s32 i;

    for (i = 0; i < port_WeaponsCount(); i++) {
        if (test_StringsEqual(port_WeaponAt(i)->key, key)) {
            return port_WeaponAt(i);
        }
    }
    return NULL;
}

/* A made-up file's text, by its path. */
static const char *test_Text(const char *path) {
    return g_made_tree[made_Find(path)].text;
}

static void test_Run(void) {
    const PortWeapon *long_katana;
    const PortWeapon *yari;
    SVECTOR *points;
    u64 mods;

    CHECK(!port_WeaponsChoose("3=katana-long"));
    CHECK(!port_WeaponsChoose("1:katana-long"));
    CHECK(!port_WeaponsChoose("1="));
    CHECK(port_WeaponsChoose("2=nothing"));
    CHECK(port_WeaponsChoose("1=KATANA-LONG"));
    port_ModsSetFolder("mods");
    port_ModsStart("ignored");
    CHECK_EQ(port_ModsCount(PORT_MOD_WEAPONS), 5);
    CHECK_EQ(port_ModsCount(PORT_MOD_MOTIONS), 3);
    port_WeaponsStart();
    /* broken.txt is left out: a naginata has three points; and huge.txt:
     * its motions are larger than the disc's largest set's file */
    CHECK_EQ(port_WeaponsCount(), 3);
    long_katana = test_Weapon("katana-long");
    yari = test_Weapon("heavy-yari");
    CHECK(test_Weapon("huge") == NULL);
    CHECK(long_katana != NULL && yari != NULL && test_Weapon("kanabo"));
    CHECK(test_StringsEqual(long_katana->key, "katana-long"));
    CHECK(test_StringsEqual(yari->name, "heavy-yari"));
    /* the new slot is the first fighter's, and only with a katana */
    CHECK(port_WeaponOf(0, WEAPON_ID_KATANA) == long_katana);
    CHECK(port_WeaponOf(1, WEAPON_ID_KATANA) == NULL);
    CHECK(port_WeaponOf(0, WEAPON_ID_LONG_SWORD) == NULL);
    /* the yari that stands in for the game's is everybody's */
    CHECK(port_WeaponOf(0, WEAPON_ID_YARI) == yari);
    CHECK(port_WeaponOf(1, WEAPON_ID_YARI) == yari);
    CHECK(port_WeaponOf(1, WEAPON_ID_COUNT) == NULL);
    points = port_WeaponPoints(0, WEAPON_ID_KATANA, g_own_points);
    CHECK(points != g_own_points && points[0].vz == 1250);
    CHECK(port_WeaponPoints(1, WEAPON_ID_KATANA, g_own_points) == g_own_points);
    /* the yari gives no points of its own */
    CHECK(port_WeaponPoints(1, WEAPON_ID_YARI, g_own_points) == g_own_points);
    CHECK_EQ(port_WeaponSpeed(0, WEAPON_ID_KATANA, 0x1000), 0xE00);
    CHECK_EQ(port_WeaponSpeed(1, WEAPON_ID_KATANA, 0x1000), 0x1000);
    CHECK_EQ(port_WeaponPower(0, WEAPON_ID_KATANA, 0x1000), 0x1400);
    CHECK_EQ(port_WeaponPower(1, WEAPON_ID_YARI, 0xC00), 0x1800);
    /* the models by which of the game's it was: the clans' change, the
     * practice one and the story's stay */
    CHECK_EQ(port_WeaponModel(0, WEAPON_ID_KATANA, 0), 2);
    CHECK_EQ(port_WeaponModel(0, WEAPON_ID_KATANA, 1), 3);
    CHECK_EQ(port_WeaponModel(0, WEAPON_ID_KATANA, 51), 51);
    CHECK_EQ(port_WeaponModel(0, WEAPON_ID_KATANA, 50), 50);
    CHECK_EQ(port_WeaponModel(1, WEAPON_ID_YARI, 8), 8);
    CHECK_EQ(port_WeaponDefaultStance(0, WEAPON_ID_KATANA, 0), 2);
    CHECK_EQ(port_WeaponDefaultStance(1, WEAPON_ID_YARI, 1), 1);
    CHECK_EQ(port_WeaponCpuReach(0, WEAPON_ID_KATANA, 0), 300);
    CHECK_EQ(port_WeaponCpuRange(0, WEAPON_ID_KATANA, 0), 0);
    CHECK_EQ(port_WeaponCpuRange(1, WEAPON_ID_YARI, 936), -16);
    CHECK_EQ(port_WeaponCpuReach(1, WEAPON_ID_YARI, 36), 36);
    /* the session's hash: the weapons' and the motion's files, not the
     * texture, and the one --weapon that found its weapon */
    mods = test_FileHash(PORT_MOD_WEAPONS, "katana-long", g_made_tree[3].text) +
           test_FileHash(PORT_MOD_WEAPONS, "heavy-yari", g_made_tree[4].text) +
           test_FileHash(PORT_MOD_WEAPONS, "broken", g_made_tree[5].text) +
           test_FileHash(PORT_MOD_MOTIONS, "u123", g_made_tree[9].text) +
           test_FileHash(PORT_MOD_WEAPONS, "kanabo",
                         test_Text("mods/blades/weapons/kanabo.txt")) +
           test_FileHash(PORT_MOD_WEAPONS, "huge",
                         test_Text("mods/blades/weapons/huge.txt")) +
           test_FileHash(PORT_MOD_MOTIONS, "kanabo",
                         test_Text("mods/blades/motions/kanabo.bbm")) +
           test_FileHash(PORT_MOD_MOTIONS, "huge",
                         test_Text("mods/blades/motions/huge.bbm"));
    CHECK(port_ModsGameplayHash() == mods);
    /* and the session's, the kanabo's own model in it too */
    mods += test_FileHash(
        PORT_MOD_MODELS, "kanabo", test_Text("mods/blades/models/kanabo.glb"));
    CHECK(port_WeaponsSessionHash() != mods);
    g_port_weapons.chosen[0] = NULL;
    CHECK(port_WeaponsSessionHash() == mods);
    /* the long katana was --weapon's; the kanabo is no one's until it is
     * picked */
    CHECK(port_WeaponOf(0, WEAPON_ID_KATANA) == NULL);
}

/* The weapon select: the katana's two new slots, gone round with L1 and
 * R1 (the game's own among them), picked with Cross, and what each
 * fighter then fights with; the kanabo's own motion set, its file read
 * into the game's room, and its base's rules kept. */
static void test_Pick(void) {
    const PortWeapon *kanabo = test_Weapon("kanabo");
    const PortWeapon *first = port_WeaponVariant(WEAPON_ID_KATANA, 1);
    const PortWeapon *second = port_WeaponVariant(WEAPON_ID_KATANA, 2);
    static u8 room[512];
    const char *shown;
    s32 count;
    s32 set;

    CHECK_EQ(port_WeaponVariantCount(WEAPON_ID_KATANA), 2);
    CHECK_EQ(port_WeaponVariantCount(WEAPON_ID_YARI), 0); /* it replaces */
    CHECK_EQ(port_WeaponVariantCount(WEAPON_ID_NODACHI), 0);
    CHECK(first != NULL && second != NULL && first != second);
    CHECK(first == kanabo || second == kanabo);
    CHECK(port_WeaponVariant(WEAPON_ID_KATANA, 3) == NULL);
    CHECK(port_WeaponVariant(WEAPON_ID_KATANA, 0) == NULL);
    port_WeaponPickReset();
    g_made_sounds = 0;
    g_disp_frame_count = 100;
    /* nothing pressed: the game's own, and nothing picked */
    CHECK(!port_WeaponPickStep(0, WEAPON_ID_KATANA, FALSE, FALSE));
    CHECK(port_WeaponPickShown(0, &count) == NULL);
    CHECK_EQ(count, 2);
    /* R1 twice: the second variant; L1 three times: round to it again */
    CHECK(port_WeaponPickStep(0, WEAPON_ID_KATANA, FALSE, TRUE));
    CHECK(port_WeaponPickStep(0, WEAPON_ID_KATANA, FALSE, TRUE));
    CHECK_EQ(g_port_weapon_pick.variant[0], 2);
    CHECK(port_WeaponPickStep(0, WEAPON_ID_KATANA, TRUE, FALSE));
    CHECK(port_WeaponPickStep(0, WEAPON_ID_KATANA, TRUE, FALSE));
    CHECK(port_WeaponPickStep(0, WEAPON_ID_KATANA, TRUE, FALSE));
    CHECK_EQ(g_port_weapon_pick.variant[0], 2);
    CHECK_EQ(g_made_sounds, 5);
    shown = port_WeaponPickShown(0, &count);
    CHECK(shown != NULL && test_StringsEqual(shown, second->name));
    /* a weapon with none: nothing to go round, and the cursor's move
     * takes the variant back to the game's own */
    CHECK(!port_WeaponPickStep(0, WEAPON_ID_NODACHI, FALSE, TRUE));
    CHECK_EQ(g_port_weapon_pick.variant[0], 0);
    CHECK(port_WeaponPickShown(0, &count) == NULL && count == 0);
    /* the kanabo picked by the first side; the second takes the katana */
    port_WeaponPickStep(0, WEAPON_ID_KATANA, FALSE, FALSE);
    while (port_WeaponVariant(
               WEAPON_ID_KATANA, g_port_weapon_pick.variant[0]) != kanabo) {
        port_WeaponPickStep(0, WEAPON_ID_KATANA, FALSE, TRUE);
    }
    port_WeaponPickTake(0, WEAPON_ID_KATANA);
    port_WeaponPickStep(1, WEAPON_ID_KATANA, FALSE, FALSE);
    port_WeaponPickTake(1, WEAPON_ID_KATANA);
    CHECK(port_WeaponOf(0, WEAPON_ID_KATANA) == kanabo);
    CHECK(port_WeaponOf(1, WEAPON_ID_KATANA) == NULL);
    /* the pick is the katana's alone */
    CHECK(port_WeaponOf(0, WEAPON_ID_LONG_SWORD) == NULL);
    shown = port_WeaponPickShown(0, &count);
    CHECK(shown != NULL && test_StringsEqual(shown, "Kanabo"));
    /* the row is gone three frames on */
    g_disp_frame_count += 3;
    CHECK(port_WeaponPickShown(0, &count) == NULL && count == 0);
    /* its own motion set: class 8 with the base's stance kinds */
    set = port_WeaponMotionSet(0, WEAPON_ID_KATANA, 0x0124);
    CHECK_EQ(set, 0x8124);
    CHECK_EQ(port_WeaponMotionSet(1, WEAPON_ID_KATANA, 0x0124), 0x0124);
    CHECK_EQ(port_WeaponMotionClass(set), WEAPON_ID_KATANA);
    CHECK_EQ(port_WeaponMotionClass(0x3123), 3);
    CHECK_EQ(port_WeaponMotionClass(0x9123), 9); /* no weapon's */
    CHECK(!port_WeaponReadMotions(0x0124, room));
    CHECK(port_WeaponReadMotions(set, room));
    CHECK(test_BytesEqual(
        room, test_Text("mods/blades/motions/kanabo.bbm"),
        port_TextLength(test_Text("mods/blades/motions/kanabo.bbm"))));
    /* choosing again: nothing picked while the side chooses */
    port_WeaponPickStep(0, WEAPON_ID_KATANA, FALSE, FALSE);
    CHECK(port_WeaponOf(0, WEAPON_ID_KATANA) == NULL);
    /* and the select screen loaded again: nothing at all */
    port_WeaponPickTake(0, WEAPON_ID_KATANA);
    CHECK(port_WeaponOf(0, WEAPON_ID_KATANA) == kanabo);
    port_WeaponPickReset();
    CHECK(port_WeaponOf(0, WEAPON_ID_KATANA) == NULL);
}

void test_Main(void) {
    test_File();
    test_None();
    test_Run();
    test_Pick();
}
