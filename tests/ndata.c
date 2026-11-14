/* The NDATA archive's directory, readers and packs (ndata.c). */
#include "src/main/ndata.c"
#include "src/main/arena.c"
#include "runtime/runtime.h"
#include "stubs/sys.c"

/* ---- stand-ins ---- */

/* The CD (cdrom.c): a read is not done, only noted. */
static s32 g_test_read_count;
static s32 g_test_read_file, g_test_read_dest, g_test_read_sector,
    g_test_read_sectors;

s32 cdrom_StartReadAt(
    s32 file_index, s32 dest, s32 sector_offset, s32 sector_count) {
    g_test_read_count++;
    g_test_read_file = file_index;
    g_test_read_dest = dest;
    g_test_read_sector = sector_offset;
    g_test_read_sectors = sector_count;
    return 1;
}

void cdrom_WaitIdle(void) {}

/* What arena.c calls, and what ndata_ReadChecked calls when an entry does
 * not fit (which the test does not make happen: the game then waits for
 * ever). */
s32 DrawSync(s32 mode) { return 0; }

void resource_ForgetWeapons(void) {}

void sys_PollHost(void) {}

void printf(const char *format, ...) {}

/* ---- the test ---- */

#define NAME_COUNT (s32)(sizeof(g_ndata_file_names) / sizeof(char *))
/* The directory on the disc: 901 entries. */
#define INF_ENTRIES 901

static void test_PrintKind(const char *name, s32 id) {
    test_Print("kind %s %d %s\n", name, id, g_ndata_file_names[id]);
}

/* The number the game asks for a model by, given its entry: the entries
 * from NDATA_FIRST_SHARED_MODEL up are asked for by a number 27 higher
 * (the numbers between are second outfits, which share the first's
 * files). */
static s32 test_ModelNumber(s32 entry) {
    return (entry >= NDATA_FIRST_SHARED_MODEL) ? entry + NDATA_SHARED_MODEL_STEP
                                               : entry;
}

/* Whether the last read was of the archive, of this entry, to there. */
static s32 test_ReadWas(s32 id, s32 dest) {
    return g_test_read_file == FILE_NDATA_DAT && g_test_read_dest == dest &&
           g_test_read_sector == g_ndata_inf[id].start_sector &&
           g_test_read_sectors == g_ndata_inf[id].length_sectors;
}

/* A pack made by hand: five sections of 16, 32, 48, 64 and 80 bytes, each
 * filled with its number. */
static s32 g_test_pack[0x100];

static void test_MakePack(void) {
    s32 section;
    s32 offset = (PACK_SECTIONS + 2) * 4;

    g_test_pack[0] = PACK_SECTIONS;
    for (section = 0; section < PACK_SECTIONS; section++) {
        g_test_pack[1 + section] = offset;
        memset((u8 *)g_test_pack + offset, 0xA0 + section, 16 * (section + 1));
        offset += 16 * (section + 1);
    }
    g_test_pack[1 + PACK_SECTIONS] = offset;
}

/* With the disc (ndata.py passes the two chanbara packs and the seven
 * models that can come out of one, as files): each model the game would
 * take from a pack is the same bytes as the model's own entry. */
static void test_RealPacks(void) {
    s32 pack, model;
    u8 *dest = test_Alloc(0x80000);

    for (pack = 0; pack < 2; pack++) {
        u32 pack_size;
        u8 *pack_file = test_ReadFile(test_Arg(pack), &pack_size);

        CHECK(pack_file != NULL);
        for (model = 0; model < NDATA_PACKED_MODEL_COUNT; model++) {
            u32 size;
            u8 *file = test_ReadFile(test_Arg(2 + model), &size);
            s32 section = g_ndata_pack_sections[model];
            s32 reads;
            u32 length;

            CHECK(file != NULL);
            ndata_LoadPack(pack, (s32)pack_file);
            reads = g_test_read_count;
            memset(dest, 0, 0x80000);
            ndata_ReadModel(
                test_ModelNumber(NDATA_PACKED_MODELS + model), (s32)dest);
            if (g_test_read_count != reads) {
                /* not in a pack: read from the archive */
                test_Print("pack %d model %d read\n", pack, model);
                continue;
            }
            length = ((s32 *)pack_file)[2 + section] -
                     ((s32 *)pack_file)[1 + section];
            /* and the copy's count of sections, and how much nearer its
             * start its sixth word says the textures are than the model's
             * own file says */
            test_Print("pack %d model %d section %d bytes %u same %d "
                       "sections %d nearer %d\n",
                       pack, model, section, length,
                       length <= size && test_BytesEqual(dest, file, length),
                       ((s32 *)dest)[0], ((s32 *)file)[5] - ((s32 *)dest)[5]);
        }
    }
}

void test_Main(void) {
    s32 i;
    u8 dest[0x200];
    u8 *in_arena;

    /* the directory and the names, for ndata.py to compare with the file
     * map (docs/formats/ndata_filemap.csv) and the disc's NDATA.INF */
    for (i = 0; i < INF_ENTRIES; i++) {
        test_Print("entry %d %d %d %s\n", i, (u16)g_ndata_inf[i].start_sector,
                   (u16)g_ndata_inf[i].length_sectors,
                   (i < NAME_COUNT) ? g_ndata_file_names[i] : "-");
    }
    /* where each kind of entry starts */
    test_PrintKind("NDATA_MODELS", NDATA_MODELS);
    test_PrintKind("NDATA_PACKED_MODELS", NDATA_PACKED_MODELS);
    test_PrintKind("NDATA_CHANBARA_PACKS", NDATA_CHANBARA_PACKS);
    test_PrintKind("NDATA_STAGES", NDATA_STAGES);
    test_PrintKind("NDATA_TINY_MODELS", NDATA_TINY_MODELS);
    test_PrintKind("NDATA_TINY_MODELS_GRAY", NDATA_TINY_MODELS_GRAY);
    test_PrintKind("NDATA_WEAPONS", NDATA_WEAPONS);
    test_PrintKind("NDATA_WEAPONS_SET_1", NDATA_WEAPONS_SET_1);
    test_PrintKind("NDATA_MOTIONS", NDATA_MOTIONS);

    /* the readers ask the CD for the entry of their kind */
    ndata_ReadStage(5, (s32)dest);
    CHECK(test_ReadWas(NDATA_STAGES + 5, (s32)dest));
    ndata_ReadTinyModel(3, (s32)dest);
    CHECK(test_ReadWas(NDATA_TINY_MODELS + 3, (s32)dest));
    ndata_ReadWeapons(0, 7, (s32)dest);
    CHECK(test_ReadWas(NDATA_WEAPONS + 7, (s32)dest));
    ndata_ReadWeapons(1, 7, (s32)dest);
    CHECK(test_ReadWas(NDATA_WEAPONS_SET_1 + 7, (s32)dest));
    ndata_ReadModel(4, (s32)dest);
    CHECK(test_ReadWas(NDATA_MODELS + 4, (s32)dest));
    /* a model number from 0x1F up is the file of the model 27 below */
    ndata_ReadModel(0x1F, (s32)dest);
    CHECK(test_ReadWas(NDATA_MODELS + 4, (s32)dest));
    CHECK_EQ(g_test_read_count, 6);

    /* an entry bound for the arena is measured against the room left;
     * either way the size comes back in bytes */
    arena_Reset();
    in_arena = arena_GetBase();
    CHECK_EQ(ndata_ReadChecked(NDATA_STAGES, (s32)in_arena),
             g_ndata_inf[NDATA_STAGES].length_sectors * 0x800);
    CHECK(test_ReadWas(NDATA_STAGES, (s32)in_arena));
    CHECK_EQ(ndata_ReadChecked(NDATA_STAGES, (s32)dest),
             g_ndata_inf[NDATA_STAGES].length_sectors * 0x800);
    CHECK_EQ(ndata_GetArenaRoomAfter(NDATA_STAGES),
             ARENA_SIZE - g_ndata_inf[NDATA_STAGES].length_sectors * 0x800);

    /* a pack: the packed models come out of it and nothing is read */
    test_MakePack();
    CHECK_EQ(ndata_IsPackLoaded(), 0);
    ndata_LoadPack(1, (s32)g_test_pack);
    CHECK(test_ReadWas(NDATA_CHANBARA_PACKS + 1, (s32)g_test_pack));
    CHECK_EQ(ndata_IsPackLoaded(), 1);
    g_test_read_count = 0;
    for (i = 0; i < NDATA_PACKED_MODEL_COUNT; i++) {
        s32 section = g_ndata_pack_sections[i];
        s32 j;

        memset(dest, 0, sizeof(dest));
        ndata_ReadModel(test_ModelNumber(NDATA_PACKED_MODELS + i), (s32)dest);
        if (section == PACK_SECTION_NONE) {
            /* the one model that is in no pack is read from the archive */
            CHECK_EQ(g_test_read_count, 1);
            continue;
        }
        for (j = 0; j < 16 * (section + 1); j++) {
            CHECK_EQ(dest[j], 0xA0 + section);
        }
        CHECK_EQ(dest[j], 0);
    }
    /* a model that is not one of the seven is read from the archive */
    ndata_ReadModel(NDATA_PACKED_MODELS - 1, (s32)dest);
    CHECK_EQ(g_test_read_count, 2);
    /* a pack that does not begin with its count of sections is dropped */
    g_test_pack[0] = PACK_SECTIONS + 1;
    ndata_ReadModel(test_ModelNumber(NDATA_PACKED_MODELS + 1), (s32)dest);
    CHECK_EQ(g_test_read_count, 3);
    CHECK_EQ(ndata_IsPackLoaded(), 0);

    if (test_Arg(0)) {
        test_RealPacks();
    }
}
