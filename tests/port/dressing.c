/* The stages' dressing files, read, layered and shipped */
/* (src/port/dressing.c; docs/MODS.md, "Dressing"): a file read and the
 * ways it can be wrong, two mods' files of one stage layered in their
 * order, none of it on a Classic stage or with an arena, and every file
 * the program ships read as the program reads it. */
/* The mods are a made-up tree in memory; the shipped files are read from
 * the checkout, their names given by tests/port/dressing.py, which holds
 * what this prints of them to tools/port_dressing.py's reading and to the
 * stages' collision grids. */
#include "common.h"
#include "port_arenas.h"
#include "port_dressing.h"
#include "port_host.h"
#include "port_mods.h"
#include "src/port/text.c"
#include "src/port/mods.c"
#include "src/port/dressing.c"
#include "runtime/runtime.h"

#define PROBLEM_SIZE 160

/* ---- the game, as far as the dressing reads it ---- */

s32 resource_GetStage(void) { return 8; }

/* The fight's kind of stage (include/port_stage_choice.h): Classic has
 * none of a stage's dressing. */
static s32 g_test_classic;

s32 port_StageIsDressed(void) { return !g_test_classic; }

/* A custom arena in play (include/port_arenas.h): none, or this one. */
static const PortArena *g_test_arena;

const PortArena *port_ArenaInPlay(void) { return g_test_arena; }

/* ---- the host, in memory ---- */

typedef struct {
    const char *path;
    const char *text; /* NULL for a folder */
} MadeEntry;

static const MadeEntry g_made_tree[] = {
    {"mods", NULL},
    {"mods/base", NULL},
    {"mods/base/mod.txt", "name = Base\norder = -50\n"},
    {"mods/base/dressing", NULL},
    {"mods/base/dressing/stage_08.txt",
     "format = 1\n"
     "[grass lawn]\n"
     "area = -1000 -1000 1000 1000\n"
     "on = green\n"
     "[bush hedge]\n"
     "area = 5000 5000 6000 6000\n"
     "[water pond]\n"
     "area = 0 0 100 100\n"
     "y = 250\n"},
    {"mods/base/dressing/rock.obj", "v 0 0 0\nv 1 0 0\nv 0 1 0\nf 1 2 3\n"},
    {"mods/later", NULL},
    {"mods/later/mod.txt", "name = Later\norder = 10\n"},
    {"mods/later/dressing", NULL},
    {"mods/later/dressing/STAGE_08.txt",
     "# over the base mod's\n"
     "[grass lawn]\n"
     "count = 12\n"
     "remove hedge\n"
     "[particles snowfall]\n"
     "kind = snow\n"
     "area = 0 0 10 10\n"
     "nonsense here\n"},
    {"mods/later/dressing/stage_09.txt", "clear\n[wind]\nstrength = 1\n"},
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

static s32 test_Parse(const char *text, PortDressSet *set, char *problem) {
    return port_DressingParse(
        text, port_TextLength(text), set, problem, PROBLEM_SIZE);
}

/* A file one of whose lines is wrong, and the first words of why. */
static void test_Wrong(const char *text, const char *why) {
    PortDressSet set;
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
    static PortDressSet set;
    char problem[PROBLEM_SIZE];
    const PortDressDef *block;

    set.count = 0;
    CHECK_EQ(
        test_Parse(
            "# a stage's dressing\n"
            "format = 1\n"
            "\n"
            "[wind]\n"
            "strength = 0.75\n"
            "direction = -30\n"
            "gusts = 0.5\n"
            "[Grass Meadow]   # by the gate\n"
            "  area = -2000 -1000.5 3000 4000\n"
            "  area = 5000 5000 6000 6000\r\n"
            "  avoid = 0 0 100 100\n"
            "  on = Green\n"
            "  count = 1500\n"
            "  height = 80 160\n"
            "  width = 1.25\n"
            "  colour = #40A030\n"
            "  tint = #808080\n"
            "  level = 0.9\n"
            "  heights = -500 200\n"
            "  sway = 1.5\n"
            "  times = Dusk night\n"
            "[bush hedge]\n"
            "area = 0 0 10 10\n"
            "solid = no\n"
            "[water stream]\n"
            "area = 0 0 10 10\n"
            "y = 1200\n"
            "color = ground\n"
            "clarity = 0.4\n"
            "waves = 2\n"
            "[particles embers]\n"
            "kind = embers\n"
            "on = flames\n"
            "size = 12\n"
            "top = 1500\n"
            "[mesh lantern]\n"
            "file = Stone_Lantern\n"
            "at = 100 -50 200\n"
            "turn = 45\n"
            "scale = 2.5\n"
            "piece = 7\n",
            &set, problem),
        0);
    CHECK_EQ(set.count, 6);
    block = &set.block[0];
    CHECK_EQ(block->kind, PORT_DRESS_WIND);
    CHECK(test_StringsEqual(block->name, "wind"));
    CHECK_EQ(block->strength, 750);
    CHECK_EQ(block->direction, -30);
    CHECK_EQ(block->gusts, 500);
    block = &set.block[1];
    CHECK(test_StringsEqual(block->name, "meadow"));
    CHECK_EQ(block->kind, PORT_DRESS_GRASS);
    CHECK_EQ(block->area_count, 2);
    CHECK_EQ(block->area[0][1], -1000);
    CHECK_EQ(block->area[1][3], 6000);
    CHECK_EQ(block->avoid_count, 1);
    CHECK_EQ(block->on, PORT_DRESS_ON_GREEN);
    CHECK_EQ(block->count, 1500);
    CHECK_EQ(block->height[0], 80);
    CHECK_EQ(block->height[1], 160);
    CHECK_EQ(block->width, 1250);
    CHECK_EQ(block->has_color, TRUE);
    CHECK_EQ(block->color[1], 0xA0);
    CHECK_EQ(block->tint[0], 1007);
    CHECK_EQ(block->level, 900);
    CHECK_EQ(block->has_heights, TRUE);
    CHECK_EQ(block->heights[0], -500);
    CHECK_EQ(block->sway, 1500);
    CHECK_EQ(block->times, 12);
    CHECK_EQ(block->solid, FALSE);
    block = &set.block[2];
    CHECK_EQ(block->kind, PORT_DRESS_BUSH);
    CHECK_EQ(block->solid, FALSE);
    block = &set.block[3];
    CHECK_EQ(block->kind, PORT_DRESS_WATER);
    CHECK_EQ(block->at[1], 1200);
    CHECK_EQ(block->has_color, FALSE);
    CHECK_EQ(block->clarity, 400);
    CHECK_EQ(block->waves, 2000);
    block = &set.block[4];
    CHECK_EQ(block->subkind, PORT_PARTICLES_EMBERS);
    CHECK_EQ(block->on_flames, TRUE);
    CHECK_EQ(block->height[0], 12);
    CHECK_EQ(block->top, 1500);
    block = &set.block[5];
    CHECK_EQ(block->kind, PORT_DRESS_MESH);
    CHECK(test_StringsEqual(block->file, "stone_lantern"));
    CHECK_EQ(block->at[2], 200);
    CHECK_EQ(block->turn, 45);
    CHECK_EQ(block->scale, 2500);
    CHECK_EQ(block->piece, 7);
    CHECK_EQ(block->solid, TRUE);

    /* a block of a name already given changes it; `[grass x]` and
     * `[bush x]` are two */
    CHECK_EQ(
        test_Parse("[grass meadow]\ncount = 9\n[bush meadow]\n", &set, problem),
        0);
    CHECK_EQ(set.count, 7);
    CHECK_EQ(set.block[1].count, 9);
    CHECK_EQ(set.block[1].area_count, 2);
    CHECK_EQ(set.block[6].kind, PORT_DRESS_BUSH);
    CHECK_EQ(test_Parse("remove meadow\nclear\n[grass a]\n", &set, problem), 0);
    CHECK_EQ(set.count, 1);

    test_Wrong("format = 2\n", "line 1: `format = 1`");
    test_Wrong("count = 3\n", "line 1: a block's line before");
    test_Wrong("[tree oak]\n", "line 1: a block begins `[KIND NAME]`");
    test_Wrong("[grass a]\narea = 10 0 0 10\n", "line 2: a rectangle is four");
    test_Wrong("[grass a]\narea = 0 0 50000 10\n", "line 2: a rectangle lies");
    test_Wrong("[grass a]\ncount = 30000\n", "line 2: `count` is a number");
    test_Wrong("[grass a]\nheight = 200 100\n", "line 2: `height` is one");
    test_Wrong("[grass a]\non = moss\n", "line 2: `on` is any");
    test_Wrong("[grass a]\ncolor = #12345\n", "line 2: `color` is");
    test_Wrong("[grass a]\ntimes = noon\n", "line 2: `times` is");
    test_Wrong("[grass a]\nkind = snow\n", "line 2: `kind` is a particles");
    test_Wrong("[mesh a]\npiece = 64\n", "line 2: `piece` is");
    test_Wrong("[water a]\nclarity = 2\n", "line 2: `clarity` is");
    test_Wrong("[grass a]\nleaves = 3\n", "line 2: not a word");
    test_Wrong("[grass a]\nnothing\n", "line 2: not `name = value`");
    test_Wrong("remove nobody\n", "line 1: no block of that name");
    {
        static char many[PORT_DRESSING_MOST * 16 + 64];
        u32 at = 0;
        s32 i;

        for (i = 0; i <= PORT_DRESSING_MOST; i++) {
            many[at++] = '[';
            many[at++] = 'g';
            many[at++] = 'r';
            many[at++] = 'a';
            many[at++] = 's';
            many[at++] = 's';
            many[at++] = ' ';
            many[at++] = (char)('a' + i / 26);
            many[at++] = (char)('a' + i % 26);
            many[at++] = ']';
            many[at++] = '\n';
        }
        many[at] = 0;
        test_Wrong(many, "line 49: more blocks than a stage holds");
    }
}

/* The second pass's words: the weather, the water's, the shafts'. */
static void test_Weather(void) {
    static PortDressSet set;
    char problem[PROBLEM_SIZE];
    const PortDressDef *block;

    set.count = 0;
    CHECK_EQ(
        test_Parse(
            "format = 1\n"
            "[particles rain]\n"
            "kind = rain\n"
            "area = -100 -100 100 100\n"
            "glow = 1.5\n"
            "[water puddles]\n"
            "puddles = 0.4\n"
            "rain = 2\n"
            "foam = 0\n"
            "reflect = 0.9\n"
            "flow = 80 420\n"
            "shallow = #3E8C8A\n"
            "[shafts door]\n"
            "light = door_moon Door_Day\n"
            "size = 3800 1900\n",
            &set, problem),
        0);
    CHECK_EQ(set.count, 3);
    block = &set.block[0];
    CHECK_EQ(block->subkind, PORT_PARTICLES_RAIN);
    CHECK_EQ(block->glow, 1500);
    block = &set.block[1];
    CHECK_EQ(block->puddles, 400);
    CHECK_EQ(block->rain, 2000);
    CHECK_EQ(block->foam, 0);
    CHECK_EQ(block->reflect, 900);
    CHECK_EQ(block->flow[0], 80);
    CHECK_EQ(block->flow[1], 420);
    CHECK_EQ(block->has_shallow, TRUE);
    CHECK_EQ(block->shallow[0], 0x3E);
    block = &set.block[2];
    CHECK_EQ(block->kind, PORT_DRESS_SHAFTS);
    CHECK_EQ(block->light_count, 2);
    CHECK(test_BytesEqual(block->light[1], "door_day", 9));
    /* an opening is as wide as it is, whichever is the more */
    CHECK_EQ(block->height[0], 3800);
    CHECK_EQ(block->height[1], 1900);
    CHECK_EQ(block->count, 120);
    CHECK_EQ(set.block[1].reflect, 900);
    test_Wrong("[water a]\npuddles = 2\n", "line 2: `puddles` is");
    test_Wrong("[water a]\nflow = 80\n", "line 2: `flow` is two");
    test_Wrong("[water a]\nflow = 80 9000\n", "line 2: `flow` is two");
    test_Wrong("[shafts a]\nlight = a b c d e\n", "line 2: `light` is up");
    test_Wrong("[particles a]\nkind = hail\n", "line 2: `kind` is a");
    test_Wrong("[grass a]\nheight = 200 100\n", "line 2: `height` is one");
}

static void test_Layers(void) {
    const PortDressSet *set;
    static const PortArena arena;

    port_ModsSetFolder("mods");
    port_ModsStart("ignored");
    /* the stages' files and the mesh, the later stage_08 over the base's */
    CHECK_EQ(port_ModsCount(PORT_MOD_DRESSING), 3);
    CHECK(port_ModFind(PORT_MOD_DRESSING, "rock") != NULL);
    CHECK(port_DressingOfStage(-1) == NULL);
    g_test_classic = TRUE;
    CHECK(port_DressingOfStage(8) == NULL);
    g_test_classic = FALSE;
    g_test_arena = &arena;
    CHECK(port_DressingOfStage(8) == NULL);
    g_test_arena = NULL;
    set = port_DressingOfStage(8);
    CHECK(set != NULL);
    /* the base's lawn, hedge and pond; the later one's lawn made fewer,
     * the hedge removed, snow added, its last line not used */
    CHECK_EQ(set->count, 3);
    CHECK(test_StringsEqual(set->block[0].name, "lawn"));
    CHECK_EQ(set->block[0].count, 12);
    CHECK_EQ(set->block[0].on, PORT_DRESS_ON_GREEN);
    CHECK(test_StringsEqual(set->block[1].name, "pond"));
    CHECK_EQ(set->block[1].at[1], 250);
    CHECK(test_StringsEqual(set->block[2].name, "snowfall"));
    CHECK_EQ(set->block[2].subkind, PORT_PARTICLES_SNOW);
    CHECK(port_DressingOfStage(8) == set);
    set = port_DressingOfStage(9);
    CHECK_EQ(set->count, 1);
    CHECK_EQ(set->block[0].kind, PORT_DRESS_WIND);
    CHECK_EQ(set->block[0].strength, 1000);
    set = port_DressingOfStage(10);
    CHECK_EQ(set->count, 0);
}

static void test_Shipped(void) {
    s32 i;

    for (i = 0; test_Arg(i) != NULL; i++) {
        static PortDressSet set;
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
        problems = port_DressingParse(text, size, &set, problem, PROBLEM_SIZE);
        if (problems != 0) {
            test_Print("FAIL: %s: %s\n", test_Arg(i), problem);
        }
        CHECK_EQ(problems, 0);
        CHECK(set.count > 0);
        for (j = 0; j < set.count; j++) {
            const PortDressDef *block = &set.block[j];

            test_Print("block %s %s %s %d %d %d\n", test_Arg(i),
                       port_DressKindName(block->kind), block->name,
                       block->count, block->solid, block->area_count);
        }
    }
}

void test_Main(void) {
    test_File();
    test_Weather();
    test_Layers();
    test_Shipped();
}
