/* The stages' light files, read, layered, noted and shipped */
/* (src/port/lights.c; docs/MODS.md, "Lights"): a file read and every way
 * it can be wrong, two mods' files of one stage layered in their order,
 * the game's flames and flashes as noted, and every file the program
 * ships read as the program reads it. */
/* The mods are a made-up tree in memory; the shipped files are read from
 * the checkout, their names given by tests/port/lights.py, which holds
 * what this prints of them to each stage's bounds. */
#include "common.h"
#include "port_host.h"
#include "port_lights.h"
#include "port_mods.h"
#include "src/port/text.c"
#include "src/port/mods.c"
#include "src/port/lights.c"
#include "runtime/runtime.h"

#define PROBLEM_SIZE 160

/* ---- the game, as far as the lights read it ---- */

static s32 g_test_stage = 7;

s32 resource_GetStage(void) { return g_test_stage; }

/* A custom arena's light file (include/port_arenas.h), or NULL for none. */
static const char *g_test_arena;

const char *port_ArenaLightsKey(void) { return g_test_arena; }

/* ---- the host, in memory ---- */

typedef struct {
    const char *path;
    const char *text; /* NULL for a folder */
} MadeEntry;

static const MadeEntry g_made_tree[] = {
    {"mods", NULL},
    {"mods/base", NULL},
    {"mods/base/mod.txt", "name = Base\norder = -50\n"},
    {"mods/base/lights", NULL},
    {"mods/base/lights/stage_07.txt",
     "format = 1\n"
     "[light torch]\n"
     "at = 100 -900 2000\n"
     "color = #FF8833   # a flame's\n"
     "radius = 2500\n"
     "flicker = 0.4\n"
     "[light lamp]\n"
     "at = -100 -1200 0\n"
     "[light every_flame]\n"
     "on = flames\n"},
    {"mods/base/lights/stage_08.txt", "format = 1\n[light a]\n[light b]\n"},
    {"mods/later", NULL},
    {"mods/later/mod.txt", "name = Later\norder = 10\n"},
    {"mods/later/lights", NULL},
    {"mods/later/lights/night", NULL},
    {"mods/later/lights/night/STAGE_07.txt",
     "# over the base mod's\n"
     "[light torch]\n"
     "color = 40 80 255\n"
     "remove lamp\n"
     "[light blade]\n"
     "on = weapon 1\n"
     "kind = spot\n"
     "direction = 0 0 1\n"
     "cone = 10 25\n"
     "intensity = 2.25\n"
     "shadow = yes\n"
     "nonsense here\n"},
    {"mods/later/lights/stage_08.txt", "clear\n[light c]\n"},
};
#define MADE_ENTRIES ((s32)(sizeof(g_made_tree) / sizeof(g_made_tree[0])))
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

/* ---- one file ---- */

static s32 test_Parse(const char *text, PortLightSet *set, char *problem) {
    return port_LightsParse(
        text, port_TextLength(text), set, problem, PROBLEM_SIZE);
}

/* A file one of whose lines is wrong, and the first words of why. */
static void test_Wrong(const char *text, const char *why) {
    PortLightSet set;
    char problem[PROBLEM_SIZE];
    u32 length = port_TextLength(why);

    set.count = 0;
    CHECK_EQ(test_Parse(text, &set, problem), 1);
    if (!test_BytesEqual(problem, why, length)) {
        test_Print(
            "FAIL: \"%s\" says \"%s\", not \"%s...\"\n", text, problem, why);
        CHECK(FALSE);
    }
}

static void test_File(void) {
    PortLightSet set;
    char problem[PROBLEM_SIZE];
    const PortLightDef *light;

    set.count = 0;
    CHECK_EQ(
        test_Parse(
            "# a stage's lights\n"
            "format = 1\n"
            "\n"
            "[light Lantern_1]   # by the gate\n"
            "  kind = spot\r\n"
            "  at = 1500 -1200.75 -3000\n"
            "  on = fighter 2 bone 7\n"
            "  colour = #ffC080\n"
            "  intensity = 1.5\n"
            "  radius = 4000\n"
            "  direction = 0 1 -1\n"
            "  cone = 20 40\n"
            "  flicker = 0.25\n"
            "  shadow = yes\n"
            "[fire]\n"
            "on = flame 2\n"
            "color = 255 128 0\n",
            &set, problem),
        0);
    CHECK_EQ(set.count, 2);
    light = &set.light[0];
    CHECK(test_StringsEqual(light->name, "lantern_1"));
    CHECK_EQ(light->kind, PORT_LIGHT_SPOT);
    CHECK_EQ(light->at[0], 1500);
    CHECK_EQ(light->at[1], -1200);
    CHECK_EQ(light->at[2], -3000);
    CHECK_EQ(light->on, PORT_LIGHT_ON_FIGHTER);
    CHECK_EQ(light->on_index, 1);
    CHECK_EQ(light->on_bone, 7);
    CHECK_EQ(light->color[0], 255);
    CHECK_EQ(light->color[1], 0xC0);
    CHECK_EQ(light->color[2], 0x80);
    CHECK_EQ(light->intensity, 1500);
    CHECK_EQ(light->radius, 4000);
    CHECK_EQ(light->direction[1], 1);
    CHECK_EQ(light->direction[2], -1);
    CHECK_EQ(light->cone_inner, 20);
    CHECK_EQ(light->cone_outer, 40);
    CHECK_EQ(light->flicker, 250);
    CHECK_EQ(light->shadow, TRUE);
    light = &set.light[1];
    CHECK(test_StringsEqual(light->name, "fire"));
    CHECK_EQ(light->on, PORT_LIGHT_ON_FLAME);
    CHECK_EQ(light->on_index, 2);
    CHECK_EQ(light->color[1], 128);
    /* what a light is with nothing said */
    CHECK_EQ(light->kind, PORT_LIGHT_POINT);
    CHECK_EQ(light->intensity, PORT_LIGHT_ONE);
    CHECK_EQ(light->radius, 3000);
    CHECK_EQ(light->on_bone, -1);
    CHECK_EQ(light->shadow, FALSE);
    CHECK_EQ(light->flicker, 0);

    /* the same name again changes it; remove and clear */
    CHECK_EQ(test_Parse("[light FIRE]\nradius = 9000\nremove lantern_1\n", &set,
                        problem),
             0);
    CHECK_EQ(set.count, 1);
    CHECK_EQ(set.light[0].radius, 9000);
    CHECK_EQ(set.light[0].color[1], 128);
    CHECK_EQ(test_Parse("clear\n", &set, problem), 0);
    CHECK_EQ(set.count, 0);

    test_Wrong("format = 2\n", "line 1: `format = 1`");
    test_Wrong("radius = 5\n", "line 1: a light's line before");
    test_Wrong("[light a]\nbrightness = 2\n", "line 2: not a word a light has");
    test_Wrong("[light a]\ncolor = #12345\n", "line 2: `color` is");
    test_Wrong("[light a]\ncolor = 1 2 300\n", "line 2: `color` is");
    test_Wrong("[light a]\nat = 1 2\n", "line 2: `at` is three numbers");
    test_Wrong("[light a]\nat = 1 2 3 4\n", "line 2: `at` is three numbers");
    test_Wrong("[light a]\nat = 1 x 3\n", "line 2: `at` is three numbers");
    test_Wrong("[light a]\nradius = 0\n", "line 2: `radius`");
    test_Wrong("[light a]\nintensity = -1\n", "line 2: `intensity`");
    test_Wrong("[light a]\nflicker = 1.5\n", "line 2: `flicker`");
    test_Wrong("[light a]\ncone = 50 40\n", "line 2: `cone`");
    test_Wrong("[light a]\ndirection = 0 0 0\n", "line 2: `direction`");
    test_Wrong("[light a]\nkind = area\n", "line 2: `kind`");
    test_Wrong("[light a]\nshadow = maybe\n", "line 2: `shadow`");
    test_Wrong("[light a]\non = fighter 3\n", "line 2: a fighter is 1 or 2");
    test_Wrong("[light a]\non = weapon\n", "line 2: `on` names a thing");
    test_Wrong("[light a]\non = piece 2 bone 1\n", "line 2: only a fighter");
    test_Wrong("[light a]\non = moon\n", "line 2: `on` is");
    test_Wrong("[light a]\non = fighter 1 bone 21\n", "line 2: a bone is");
    test_Wrong("[light two words]\n", "line 1: a light begins");
    test_Wrong("[light a\n", "line 1: a light begins");
    test_Wrong("remove nobody\n", "line 1: no light of that name");
    test_Wrong("clear everything\n", "line 1: `clear` alone");
    test_Wrong("lights everywhere\n", "line 1: not `name = value`");
    /* a light whose block could not begin: its lines are not counted
     * again */
    test_Wrong("[light]\nradius = 4\nat = 1 2 3\n", "line 1: a light begins");
    {
        static char many[64 * 16 + 32];
        u32 at = 0;
        s32 i;

        for (i = 0; i < PORT_LIGHTS_MOST + 1; i++) {
            many[at++] = '[';
            many[at++] = 'l';
            many[at++] = (char)('a' + i / 26);
            many[at++] = (char)('a' + i % 26);
            many[at++] = ']';
            many[at++] = '\n';
        }
        many[at] = 0;
        test_Wrong(many, "line 65: more lights than a stage holds");
    }
}

/* ---- two mods' files of a stage ---- */

static void test_Layers(void) {
    const PortLightSet *set;
    const char *path;

    port_ModsSetFolder("mods");
    port_ModsStart("ignored");
    CHECK_EQ(port_ModsCount(PORT_MOD_LIGHTS), 2);
    /* the last one's file is the one port_ModFind gives; every one's in
     * the mods' order is port_ModFindEach's */
    path = port_ModFind(PORT_MOD_LIGHTS, "stage_07");
    CHECK(path != NULL &&
          test_StringsEqual(path, "mods/later/lights/night/STAGE_07.txt"));
    path = port_ModFindEach(PORT_MOD_LIGHTS, "stage_07", 0);
    CHECK(path != NULL &&
          test_StringsEqual(path, "mods/base/lights/stage_07.txt"));
    path = port_ModFindEach(PORT_MOD_LIGHTS, "stage_07", 1);
    CHECK(path != NULL &&
          test_StringsEqual(path, "mods/later/lights/night/STAGE_07.txt"));
    CHECK(port_ModFindEach(PORT_MOD_LIGHTS, "stage_07", 2) == NULL);
    CHECK(port_ModFindEach(PORT_MOD_LIGHTS, "stage_09", 0) == NULL);
    CHECK(port_ModFindEach(PORT_MOD_SKIES, "stage_07", 0) == NULL);
    /* the overridden is kept out of its kind's files */
    {
        const char *key;
        s32 place = 0;

        while (port_ModFileOf(PORT_MOD_LIGHTS, place, &key, &path)) {
            place++;
        }
        CHECK_EQ(place, 2);
    }

    CHECK(port_LightsOfStage(-1) == NULL);
    set = port_LightsOfStage(7);
    CHECK(set != NULL);
    /* the base's torch, lamp and flames; the later one's torch made
     * blue, the lamp removed, a blade's light added; its last line not
     * used, and nothing else lost */
    CHECK_EQ(set->count, 3);
    CHECK(test_StringsEqual(set->light[0].name, "torch"));
    CHECK_EQ(set->light[0].at[1], -900);
    CHECK_EQ(set->light[0].color[0], 40);
    CHECK_EQ(set->light[0].color[2], 255);
    CHECK_EQ(set->light[0].flicker, 400);
    CHECK_EQ(set->light[0].radius, 2500);
    CHECK(test_StringsEqual(set->light[1].name, "every_flame"));
    CHECK_EQ(set->light[1].on, PORT_LIGHT_ON_FLAME);
    CHECK_EQ(set->light[1].on_index, -1);
    CHECK(test_StringsEqual(set->light[2].name, "blade"));
    CHECK_EQ(set->light[2].on, PORT_LIGHT_ON_WEAPON);
    CHECK_EQ(set->light[2].on_index, 1);
    CHECK_EQ(set->light[2].kind, PORT_LIGHT_SPOT);
    CHECK_EQ(set->light[2].intensity, 2250);
    CHECK_EQ(set->light[2].shadow, TRUE);
    /* asked again, the same set */
    CHECK(port_LightsOfStage(7) == set);
    set = port_LightsOfStage(8);
    CHECK_EQ(set->count, 1);
    CHECK(test_StringsEqual(set->light[0].name, "c"));
    set = port_LightsOfStage(9);
    CHECK_EQ(set->count, 0);
    /* an arena standing in for stage 7 names its own file, and the
     * stage's own again when it is gone */
    g_test_arena = "STAGE_08";
    set = port_LightsOfStage(7);
    CHECK_EQ(set->count, 1);
    CHECK(test_StringsEqual(set->light[0].name, "c"));
    g_test_arena = NULL;
    set = port_LightsOfStage(7);
    CHECK_EQ(set->count, 3);
}

/* ---- what the game noted ---- */

static void test_Notes(void) {
    /* the game's Flame: x times two and its bit, y times eight and the
     * kind */
    static const s32 flames[] = {
        -5199, -1649 * 8 + 1, 12900, 1001, 800 * 8 + 3, -40, 0, 0, 0};
    const PortLightsGame *game = port_LightsGame();
    s32 at[3] = {10, -20, 30};
    u32 event;

    g_test_stage = 6;
    port_LightsNoteFlames(flames);
    CHECK_EQ(game->flame_stage, 6);
    CHECK_EQ(game->flame_count, 2);
    CHECK_EQ(game->flame[0].at[0], -2599); /* as eff_DrawFlames divides */
    CHECK_EQ(game->flame[0].at[1], -1648);
    CHECK_EQ(game->flame[0].at[2], 12900);
    CHECK_EQ(game->flame[0].kind, 1);
    CHECK_EQ(game->flame[1].at[0], 500);
    CHECK_EQ(game->flame[1].at[1], 800);
    CHECK_EQ(game->flame[1].kind, 3);

    port_LightsNoteEffects();
    CHECK_EQ(game->serial, 1);
    port_LightsNoteGlow(3, at);
    port_LightsNoteGlow(3, at); /* drawn twice: one glow */
    port_LightsNoteGlow(6, at);
    CHECK_EQ(game->glow_count, 2);
    CHECK_EQ(game->glow[1].flash, 6);
    CHECK_EQ(game->glow[1].at[1], -20);
    port_LightsNoteFlash(at, 0xFFFFEF, 9);
    event = game->flash_event;
    CHECK_EQ(game->flash_on, TRUE);
    CHECK_EQ(game->flash_color, 0xFFFFEF);
    port_LightsNoteEffects();
    CHECK_EQ(game->glow_count, 0);
    port_LightsNoteFlash(at, 0xFFFFEF, 8); /* the same flash, older */
    CHECK_EQ(game->flash_event, event);
    port_LightsNoteFlash(at, 0xFF0060, 9); /* a new one at once */
    CHECK_EQ(game->flash_event, event + 1);
    port_LightsNoteFlash(at, 0xFF0060, 0);
    CHECK_EQ(game->flash_on, FALSE);
}

/* ---- the files the program ships ---- */

/* Each file named: read as the program reads it, every line used, and
 * each light printed for tests/port/lights.py: `light FILE NAME ON INDEX
 * X Y Z RADIUS`. */
static void test_Shipped(void) {
    s32 i;

    for (i = 0; test_Arg(i) != NULL; i++) {
        PortLightSet set;
        char problem[PROBLEM_SIZE];
        unsigned size = 0;
        const char *text = (const char *)test_ReadFile(test_Arg(i), &size);
        s32 problems;
        s32 j;

        CHECK(text != NULL);
        if (text == NULL) {
            continue;
        }
        set.count = 0;
        problems = port_LightsParse(text, size, &set, problem, PROBLEM_SIZE);
        if (problems != 0) {
            test_Print("FAIL: %s: %s\n", test_Arg(i), problem);
        }
        CHECK_EQ(problems, 0);
        CHECK(set.count > 0);
        for (j = 0; j < set.count; j++) {
            const PortLightDef *light = &set.light[j];

            test_Print("light %s %s %d %d %d %d %d %d\n", test_Arg(i),
                       light->name, light->on, light->on_index, light->at[0],
                       light->at[1], light->at[2], light->radius);
        }
    }
}

void test_Main(void) {
    test_File();
    test_Layers();
    test_Notes();
    test_Shipped();
}
