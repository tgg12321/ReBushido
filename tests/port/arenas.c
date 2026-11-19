/* The port's custom arenas (src/port/arenas.c; docs/MODS.md, "Custom
 * arenas"): an arena's file read and every way it can be wrong, its
 * picture for the stage select, the mods' arenas with one in play over
 * its host, the answers the game is given where it reads its stage
 * tables, for the host and for every other stage and with no arena at
 * all, the ground read in place of the host's and changed from one arena
 * to another and to none, and the hash the session compares. */
/* Everything is made up here: the mods are files in memory, the ground a
 * header of six sections and nothing in them, and the game's tables the
 * game's shapes with numbers of the test's own. */
#include "common.h"
#include "port_arenas.h"
#include "port_host.h"
#include "port_mods.h"
#include "src/port/text.c"
#include "src/port/arenas.c"
#include "runtime/runtime.h"

/* ---- the game's tables ---- */

StageHooks g_stage_hooks[STAGE_COUNT];
s16 g_fight_start_positions[STAGE_COUNT][FIGHT_START_PAIRS][2]
                           [FIGHT_START_POINT_SIZE];

static void test_Nothing(void) {}

/* ---- the host and the mods, in memory ---- */

#define GROUND_BYTES 32
#define FILE_ROOM 2048

typedef struct {
    const char *key;
    const char *text;
    u8 bytes[FILE_ROOM];
    u32 size;
} MadeArena;

static MadeArena g_made[3];
static s32 g_made_count;
static s32 g_ground_stage = -1;
/* the arena in play, as the game's state has it (port_stage_choice.h) */
static s32 g_in_play = PORT_STAGE_NO_ARENA;
static s32 g_started;

s32 port_ModFileOf(s32 kind, s32 place, const char **key, const char **path) {
    if (kind != PORT_MOD_ARENAS || place < 0 || place >= g_made_count) {
        return FALSE;
    }
    *key = g_made[place].key;
    *path = g_made[place].key;
    return TRUE;
}

static MadeArena *test_Made(const char *path) {
    s32 i;

    for (i = 0; i < g_made_count; i++) {
        if (port_TextsEqual(g_made[i].key, path)) {
            return &g_made[i];
        }
    }
    return NULL;
}

u32 port_ModFileSize(const char *path) {
    MadeArena *made = test_Made(path);

    return made != NULL ? made->size : 0;
}

u32 port_ModReadFile(const char *path, void *dest, u32 room) {
    MadeArena *made = test_Made(path);

    if (made == NULL || room < made->size) {
        return 0;
    }
    test_Copy(dest, made->bytes, made->size);
    return made->size;
}

/* every host's ground: 87 sectors from sector 1000 */
#define HOST_SECTOR 1000
#define HOST_ROOM 178176

s32 port_FindArenaRoom(s32 stage, u32 *sector, u32 *length) {
    g_ground_stage = stage;
    *sector = HOST_SECTOR;
    *length = HOST_ROOM;
    return TRUE;
}

s32 port_StageArena(void) { return g_started ? g_in_play : port_ArenaAsked(); }

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

/* An arena's file: `text`, a line saying the ground's length, then a
 * ground of six sections. */
static void test_Make(MadeArena *made, const char *key, const char *text) {
    u32 length = port_TextLength(text);
    u32 i;

    made->key = key;
    made->text = text;
    test_Copy(made->bytes, text, length);
    /* GROUND_BYTES */
    test_Copy(made->bytes + length, "ground = 32\n", 12);
    length += 12;
    for (i = 0; i < GROUND_BYTES; i++) {
        made->bytes[length + i] = 0;
    }
    made->bytes[length] = 6; /* the header's count of sections */
    made->size = length + GROUND_BYTES;
}

static s32 test_Parse(const char *text, PortArena *arena, char *problem) {
    MadeArena made;

    test_Make(&made, "test", text);
    test_Copy(arena->key, "test", 5);
    return port_ArenaParse(made.bytes, made.size, arena, problem, 160);
}

static void test_Wrong(const char *text, const char *why) {
    PortArena arena;
    char problem[160];

    CHECK(!test_Parse(text, &arena, problem));
    if (!port_TextEndsWith(problem, why)) {
        test_Print("said \"%s\", not \"...%s\"\n", problem, why);
        CHECK(FALSE);
    }
}

#define GOOD                                                                   \
    "RBARENA 1\n"                                                              \
    "# a comment, and a blank line\n"                                          \
    "\n"                                                                       \
    "name = Stone courtyard\n"                                                 \
    "stands_in_for = 21\n"                                                     \
    "start1 = -2000 0 0, 2000 0 0\n"                                           \
    "start3 = 0 -450 6000 0 -450 7200   # on the dais\n"                       \
    "light = 0xE8DCC4 0x3A3A48 0xD8CCB4 0x403E50 0x0D800A7D 0x101418\n"        \
    "fog = off\n"                                                              \
    "fog_span = 32000\n"                                                       \
    "music = 2\n"                                                              \
    "sounds = 0\n"                                                             \
    "lights = courtyard-noon\n"                                                \
    "chain = ring 6 37 -500 -500 500 -500 500 500 -500 500\n"                  \
    "chain = open 3 50 -8000 4000 8000 4000\n"

static void test_File(void) {
    PortArena arena;
    char problem[160];

    CHECK(test_Parse(GOOD, &arena, problem));
    CHECK(test_StringsEqual(arena.name, "Stone courtyard"));
    CHECK(test_StringsEqual(arena.lights, "courtyard-noon"));
    CHECK_EQ(arena.format, 1);
    CHECK_EQ(arena.host, 21);
    CHECK_EQ(arena.starts[0], -2000);
    CHECK_EQ(arena.starts[3], 2000);
    /* a pair not given is the first again */
    CHECK_EQ(arena.starts[6], -2000);
    CHECK_EQ(arena.starts[12 + 1], -450);
    CHECK_EQ(arena.starts[12 + 5], 7200);
    CHECK_EQ(arena.starts[18 + 3], 2000);
    CHECK(arena.has_light);
    CHECK_EQ(arena.light[4], 0x0D800A7D);
    CHECK_EQ(arena.fog, 0);
    CHECK_EQ(arena.fog_span, 32000);
    CHECK_EQ(arena.fog_farthest, PORT_ARENA_UNSET);
    CHECK_EQ(arena.music, 2);
    CHECK_EQ(arena.sounds, 0);
    CHECK_EQ(arena.chain_count, 2);
    CHECK_EQ(arena.chains[0].flags, 0);
    CHECK_EQ(arena.chains[0].corners, 4);
    CHECK_EQ(arena.chains[0].at[2][1], 500);
    CHECK_EQ(arena.chains[1].flags, NAV_CHAIN_OPEN);
    CHECK_EQ(arena.chains[1].margin, 50);
    CHECK_EQ(arena.ground_bytes, GROUND_BYTES);

    /* the least a file can be: a host of its own, the rest the host's */
    CHECK(test_Parse("RBARENA 1\nstart1 = 0 0 0 4000 0 0\n", &arena, problem));
    CHECK_EQ(arena.host, PORT_ARENA_HOST_DEFAULT);
    CHECK(test_StringsEqual(arena.name, "test"));
    CHECK(!arena.has_light);
    CHECK_EQ(arena.chain_count, PORT_ARENA_UNSET);

    test_Wrong(
        "RBARENA\nstart1 = 0 0 0 4000 0 0\n", "no format on its first line");
    test_Wrong(
        "RBARENA 3\nstart1 = 0 0 0 4000 0 0\n", "a later ReBushido is needed");
    /* a picture of its own: so many bytes after its line, and a newline */
    CHECK(test_Parse("RBARENA 2\npreview = 4\nPNG?\nstart1 = 0 0 0 4000 0 0\n",
                     &arena, problem));
    CHECK_EQ(arena.preview_bytes, 4);
    CHECK_EQ(arena.preview_at, 22);
    CHECK_EQ(arena.starts[3], 4000);
    test_Wrong("RBARENA 2\nstart1 = 0 0 0 4000 0 0\npreview = 900000\n",
               "preview: is the number of bytes of the PNG that follows "
               "the line");
    test_Wrong("RBX 1\n", "not an arena's file (it begins with RBARENA)");
    test_Wrong("RBARENA 1\nname = x\n", "no start1: where the fighters start");
    test_Wrong(
        "RBARENA 1\nstart1 = 0 0 0 4000 0\n", "start1 is two points of x y z");
    test_Wrong("RBARENA 1\nstart1 = 0 0 0 40000 0 0\n",
               "a point is outside the grid (-32000 to 32000)");
    /* a stage with a hook, areas, props or code of its own, or none of
     * the select screen's */
    test_Wrong(
        "RBARENA 1\nstands_in_for = 4\nstart1 = 0 0 0 4000 0 0\n", "25 to 31)");
    test_Wrong("RBARENA 1\nstands_in_for = 2\n", "25 to 31)");
    test_Wrong("RBARENA 1\nstands_in_for = 34\n", "25 to 31)");
    test_Wrong("RBARENA 1\nstands_in_for = 32\n", "25 to 31)");
    test_Wrong("RBARENA 1\nfog = sometimes\n", "fog is on or off");
    test_Wrong(
        "RBARENA 1\nmusic = 9\n", "music is a track of the stages', 0 to 4");
    test_Wrong("RBARENA 1\nsounds = 40\n", "sounds is a stage, 0 to 37");
    test_Wrong("RBARENA 1\nlight = 1 2 3\n", "the distance's colour");
    test_Wrong("RBARENA 1\nchain = loop 1 1 0 0 1 1\n",
               "chain begins with ring or open");
    test_Wrong(
        "RBARENA 1\nchain = ring 1 1 0 0\n", "then two to 16 corners of x z");
    test_Wrong("RBARENA 1\ncolour = blue\n",
               "colour is not a name this program knows");
    test_Wrong("RBARENA 1\nname\n", "line 2: no `=`");
}

/* ---- the run ---- */

static void test_Answers(void) {
    s16 starts_row[PORT_ARENA_START_HALVES];
    s32 game_light[PORT_ARENA_LIGHT_WORDS] = {1, 2, 3, 4, 5, 6};
    NavSet game_nav = {0};
    s16 *table;
    s32 i;

    /* with no arena in play every answer is the game's */
    CHECK(port_ArenaInPlay() == NULL);
    CHECK(port_ArenaStartPositions(g_fight_start_positions) ==
          (void *)g_fight_start_positions);
    CHECK(port_ArenaLightPreset(21, game_light) == game_light);
    CHECK_EQ(port_ArenaFogOn(21, 1), 1);
    CHECK_EQ(port_ArenaFogSpan(21, 20585), 20585);
    CHECK_EQ(port_ArenaFogFarthest(21, 0), 0);
    CHECK_EQ(port_ArenaMusic(21, 0), 0);
    CHECK_EQ(port_ArenaSoundStage(21), 21);
    CHECK(port_ArenaNavSet(21, &game_nav) == (void *)&game_nav);
    CHECK(port_ArenaLightsKey() == NULL);
    CHECK(port_ArenaSessionHash() == 0);

    /* the mods: two good arenas and a broken one, the first asked for */
    for (i = 0; i < STAGE_COUNT * PORT_ARENA_START_HALVES; i++) {
        ((s16 *)g_fight_start_positions)[i] = (s16)i;
    }
    test_Make(&g_made[0], "courtyard", GOOD);
    test_Make(&g_made[1], "broken", "RBARENA 1\nfog = maybe\n");
    test_Make(&g_made[2], "plain", "RBARENA 1\nstart1 = 0 0 0 4000 0 0\n");
    g_made_count = 3;
    CHECK(port_ArenaAsk("CourtYard"));
    port_ArenasStart();
    CHECK_EQ(port_ArenaCount(), 2);
    CHECK(port_ArenaAt(1) != NULL &&
          test_StringsEqual(port_ArenaAt(1)->key, "plain"));
    CHECK(port_ArenaAt(2) == NULL);
    CHECK(port_ArenaInPlay() == port_ArenaAt(0));
    CHECK_EQ(port_ArenaAsked(), 0);
    CHECK_EQ(g_ground_stage, 25); /* the last one's host, `plain`'s */
    CHECK_EQ(port_ArenaAt(0)->ground_sector, HOST_SECTOR);
    CHECK_EQ(port_ArenaAt(0)->ground_sectors, HOST_ROOM / 2048);
    {
        /* its ground in place of its host's: the header's six, then the
         * zeros past its 32 bytes; another sector is the disc's */
        u8 data[2048];

        CHECK(port_ArenaGroundSector(HOST_SECTOR, data));
        CHECK_EQ(data[0], 6);
        CHECK_EQ(data[GROUND_BYTES], 0);
        CHECK(port_ArenaGroundSector(HOST_SECTOR + 86, data));
        CHECK(!port_ArenaGroundSector(HOST_SECTOR + 87, data));
        CHECK(!port_ArenaGroundSector(HOST_SECTOR - 1, data));
    }

    /* the host's row of the start table is the arena's, every other
     * stage's the game's */
    table = (s16 *)port_ArenaStartPositions(g_fight_start_positions);
    CHECK(table != (s16 *)g_fight_start_positions);
    CHECK_EQ(table[21 * PORT_ARENA_START_HALVES], -2000);
    CHECK_EQ(table[21 * PORT_ARENA_START_HALVES + 14], 6000);
    CHECK_EQ(table[20 * PORT_ARENA_START_HALVES + 5],
             20 * PORT_ARENA_START_HALVES + 5);
    CHECK_EQ(table[22 * PORT_ARENA_START_HALVES], 22 * PORT_ARENA_START_HALVES);
    for (i = 0; i < PORT_ARENA_START_HALVES; i++) {
        starts_row[i] = table[21 * PORT_ARENA_START_HALVES + i];
    }
    CHECK_EQ(starts_row[3], 2000);

    CHECK(port_ArenaLightPreset(21, game_light) != game_light);
    CHECK_EQ(port_ArenaLightPreset(21, game_light)[0], 0xE8DCC4);
    CHECK(port_ArenaLightPreset(20, game_light) == game_light);
    CHECK_EQ(port_ArenaFogOn(21, 1), 0);
    CHECK_EQ(port_ArenaFogOn(20, 1), 1);
    CHECK_EQ(port_ArenaFogSpan(21, 20585), 32000);
    /* not given: the host's own */
    CHECK_EQ(port_ArenaFogFarthest(21, 1), 1);
    CHECK_EQ(port_ArenaMusic(21, 0), 2);
    CHECK_EQ(port_ArenaMusic(25, 3), 3);
    CHECK_EQ(port_ArenaSoundStage(21), 0);
    CHECK_EQ(port_ArenaSoundStage(9), 9);
    {
        NavSet *nav = (NavSet *)port_ArenaNavSet(21, &game_nav);

        CHECK(nav != &game_nav);
        CHECK_EQ(nav->nchains, 2);
        CHECK_EQ(nav->chains[0].nvtx, 4);
        CHECK_EQ(nav->chains[0].kind, 6);
        CHECK_EQ(nav->chains[0].vtx[1][0], 500);
        CHECK_EQ(nav->chains[1].flags, NAV_CHAIN_OPEN);
        CHECK(port_ArenaNavSet(20, &game_nav) == (void *)&game_nav);
    }
    CHECK(test_StringsEqual(port_ArenaLightsKey(), "courtyard-noon"));
    /* the session's hash: every arena offered, by bytes and key, and the
     * one asked for */
    CHECK(port_ArenaSessionHash() != 0);
    CHECK(port_ArenaSessionHash() != port_ArenaAt(1)->hash);

    /* the stage select puts the other in play: its tables, its ground,
     * made again for it */
    g_started = TRUE;
    g_in_play = 1;
    CHECK(port_ArenaInPlay() == port_ArenaAt(1));
    CHECK_EQ(port_ArenaFogOn(21, 1), 1); /* not its host */
    CHECK_EQ(port_ArenaFogOn(25, 1), 1); /* its host, and it says nothing */
    table = (s16 *)port_ArenaStartPositions(g_fight_start_positions);
    CHECK_EQ(table[25 * PORT_ARENA_START_HALVES + 3], 4000);
    CHECK_EQ(table[21 * PORT_ARENA_START_HALVES], 21 * PORT_ARENA_START_HALVES);
    CHECK(port_ArenaNavSet(25, &game_nav) == (void *)&game_nav);
    CHECK(port_ArenaLightsKey() == NULL);
    /* and none: the disc's again, everywhere */
    g_in_play = PORT_STAGE_NO_ARENA;
    CHECK(port_ArenaInPlay() == NULL);
    CHECK(port_ArenaStartPositions(g_fight_start_positions) ==
          (void *)g_fight_start_positions);
    {
        u8 data[2048];

        CHECK(!port_ArenaGroundSector(HOST_SECTOR, data));
    }
    /* back to the first: its tables made again */
    g_in_play = 0;
    table = (s16 *)port_ArenaStartPositions(g_fight_start_positions);
    CHECK_EQ(table[21 * PORT_ARENA_START_HALVES], -2000);
    CHECK_EQ(table[25 * PORT_ARENA_START_HALVES + 3],
             25 * PORT_ARENA_START_HALVES + 3);
}

void test_Main(void) {
    s32 i;

    for (i = 0; i < STAGE_COUNT; i++) {
        g_stage_hooks[i].init = NULL;
        g_stage_hooks[i].update = NULL;
    }
    g_stage_hooks[STAGE_BUS_STOP].init = test_Nothing;
    test_File();
    test_Answers();
}
