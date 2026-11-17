/* The port's disc by name: its directory, and a mod's stage files. */
/* The ISO 9660 directory of a made-up image is read (src/port/
 * discfiles.c): folders, a folder of two sectors, files found by path in
 * either case and by a sector inside them. Then a stage's files are found
 * by their keys (NDATA's through its own directory, NDATA.INF), written
 * out for --dump-stages, and a mod's files are put over the disc's
 * sectors: one that fits, one shorter (the rest reads as zeros), one too
 * long (left out), and a motion file of MOTION/ by its key; the sectors
 * none covers are left as they were.
 * Nothing here is the game's. */
#include "common.h"
#include "port_disc.h"
#include "port_discfiles.h"
#include "port_host.h"
#include "port_mods.h"
#include "src/port/text.c"
#include "src/port/discfiles.c"
#include "runtime/runtime.h"

#define MADE_SECTORS 200
#define SECTOR PORT_DISC_DATA_BYTES
/* where the made-up disc has things */
#define MADE_ROOT 20
#define MADE_LOADSE 21 /* two sectors: 21 and 22 */
#define MADE_NDATA 23
#define MADE_LOADSE1 24
#define MADE_U_PIC 25
#define MADE_MOTION 26
#define MADE_U123 192
#define MADE_U123_BYTES 3000
#define MADE_COMMON 195
#define MADE_INF 40
#define MADE_DAT 100
#define MADE_STAGE07 50
#define MADE_STAGE07_BYTES 5000
#define MADE_STG07 80
#define MADE_STG07_BYTES 19240
#define MADE_S07_START 10 /* in NDATA.DAT */
#define MADE_S07_SECTORS 3

s32 g_port_rerun;
s32 (*g_port_disc_override)(u32 sector, u8 *raw);

static u8 *g_made_disc;

static u8 made_Byte(u32 sector, u32 offset) {
    return (u8)(sector * 7 + offset * 13 + (offset >> 8));
}

s32 port_ReadDiscData(
    const PortDisc *disc, u32 sector, u32 count, u8 *dest, u32 *done) {
    (void)disc;
    if (sector + count > MADE_SECTORS) {
        return PORT_DISC_OUT_OF_RANGE;
    }
    test_Copy(dest, g_made_disc + sector * SECTOR, count * SECTOR);
    if (done != NULL) {
        *done = count;
    }
    return PORT_DISC_OK;
}

static void made_Word(u8 *at, u32 value) {
    at[0] = (u8)value;
    at[1] = (u8)(value >> 8);
    at[2] = (u8)(value >> 16);
    at[3] = (u8)(value >> 24);
}

/* A directory record at `at` of a folder's sector: its length. */
static u32 made_Record(
    u8 *at, const char *name, u32 sector, u32 size, s32 is_folder) {
    u32 length = port_TextLength(name);
    u32 bytes = 33 + length + ((length & 1) ? 0 : 1);
    u32 i;

    for (i = 0; i < bytes; i++) {
        at[i] = 0;
    }
    at[0] = (u8)bytes;
    made_Word(at + 2, sector);
    made_Word(at + 10, size);
    at[25] = is_folder ? 2 : 0;
    at[32] = (u8)length;
    for (i = 0; i < length; i++) {
        at[33 + i] = (u8)name[i];
    }
    return bytes;
}

/* A folder's own record and its parent's, whose names are a byte. */
static u32 made_Dots(u8 *at, u32 self, u32 parent) {
    static const char self_name[] = {0, 0};
    static const char parent_name[] = {1, 0};
    u32 used = made_Record(at, "x", self, SECTOR, TRUE);

    at[33] = (u8)self_name[0];
    used += made_Record(at + used, "x", parent, SECTOR, TRUE);
    at[used - 34 + 33] = (u8)parent_name[0];
    return used;
}

static void made_Disc(void) {
    u8 *at;
    u32 sector;
    u32 i;

    g_made_disc = test_Alloc(MADE_SECTORS * SECTOR);
    for (sector = 0; sector < MADE_SECTORS; sector++) {
        for (i = 0; i < SECTOR; i++) {
            g_made_disc[sector * SECTOR + i] = made_Byte(sector, i);
        }
    }
    /* the volume: its kind, its standard, and the root's record */
    for (sector = 16; sector <= MADE_MOTION; sector++) {
        for (i = 0; i < SECTOR; i++) {
            g_made_disc[sector * SECTOR + i] = 0;
        }
    }
    at = g_made_disc + 16 * SECTOR;
    at[0] = 1;
    test_Copy(at + 1, "CD001", 5);
    made_Record(at + 156, "x", MADE_ROOT, SECTOR, TRUE);
    at[156 + 33] = 0;
    /* the root */
    at = g_made_disc + MADE_ROOT * SECTOR;
    at += made_Dots(at, MADE_ROOT, MADE_ROOT);
    at += made_Record(at, "LOADSE", MADE_LOADSE, 2 * SECTOR, TRUE);
    at += made_Record(at, "LOADSE1", MADE_LOADSE1, SECTOR, TRUE);
    at += made_Record(at, "MOTION", MADE_MOTION, SECTOR, TRUE);
    at += made_Record(at, "NDATA", MADE_NDATA, SECTOR, TRUE);
    at += made_Record(at, "SYSTEM.CNF;1", 30, 68, FALSE);
    made_Record(at, "U_PIC", MADE_U_PIC, SECTOR, TRUE);
    /* LOADSE, over two sectors: the second's record after the first's
     * sector has room left */
    at = g_made_disc + MADE_LOADSE * SECTOR;
    at += made_Dots(at, MADE_LOADSE, MADE_ROOT);
    at += made_Record(at, "BB2_M1.XA;1", 60, 2 * SECTOR, FALSE);
    made_Record(at, "STAGE07.BIN;1", MADE_STAGE07, MADE_STAGE07_BYTES, FALSE);
    made_Record(g_made_disc + (MADE_LOADSE + 1) * SECTOR, "STAGE09.BIN;1", 65,
                SECTOR, FALSE);
    at = g_made_disc + MADE_LOADSE1 * SECTOR;
    at += made_Dots(at, MADE_LOADSE1, MADE_ROOT);
    made_Record(at, "STAGE29.BIN;1", 70, SECTOR, FALSE);
    at = g_made_disc + MADE_NDATA * SECTOR;
    at += made_Dots(at, MADE_NDATA, MADE_ROOT);
    at += made_Record(at, "NDATA.DAT;1", MADE_DAT, 90 * SECTOR, FALSE);
    made_Record(at, "NDATA.INF;1", MADE_INF, 3604, FALSE);
    /* two motion files: one a mod stands in for, one it does not */
    at = g_made_disc + MADE_MOTION * SECTOR;
    at += made_Dots(at, MADE_MOTION, MADE_ROOT);
    at += made_Record(at, "COMMON.BBM;1", MADE_COMMON, SECTOR, FALSE);
    made_Record(at, "U123.BBM;1", MADE_U123, MADE_U123_BYTES, FALSE);
    at = g_made_disc + MADE_U_PIC * SECTOR;
    at += made_Dots(at, MADE_U_PIC, MADE_ROOT);
    made_Record(at, "STG07.BIN;1", MADE_STG07, MADE_STG07_BYTES, FALSE);
    /* NDATA.INF: stage 7's file, and stage 29's left out of the build */
    at = g_made_disc + MADE_INF * SECTOR + (0x27 + 7) * 4;
    at[0] = MADE_S07_START;
    at[1] = 0;
    at[2] = MADE_S07_SECTORS;
    at[3] = 0;
    at = g_made_disc + MADE_INF * SECTOR + (0x27 + 29) * 4;
    at[0] = 0xFF;
    at[1] = 0xFF;
}

/* ---- the mods and the host, made up ---- */

#define MADE_FILES 12

typedef struct {
    char path[96];
    u8 *bytes;
    u32 size;
} MadeFile;

/* the mods' files, and what --dump-stages wrote */
static MadeFile g_made_files[MADE_FILES];
static s32 g_made_file_count;

static MadeFile *made_File(const char *path) {
    s32 i;

    for (i = 0; i < g_made_file_count; i++) {
        if (port_TextsEqual(g_made_files[i].path, path)) {
            return &g_made_files[i];
        }
    }
    return NULL;
}

/* A mod's file of `size` bytes of numbers that say they are the mod's. */
static void made_ModFile(const char *path, u32 size) {
    MadeFile *file = &g_made_files[g_made_file_count++];
    u32 i;

    test_Copy(file->path, path, port_TextLength(path) + 1);
    file->bytes = test_Alloc(size);
    file->size = size;
    for (i = 0; i < size; i++) {
        file->bytes[i] = (u8)(i * 3 + 1);
    }
}

const char *port_ModFind(s32 kind, const char *key) {
    if (kind == PORT_MOD_MOTIONS) {
        return port_TextsEqual(key, "u123") ? "mod/u123.bbm" : NULL;
    }
    if (kind != PORT_MOD_STAGES) {
        return NULL;
    }
    if (port_TextsEqual(key, "s07")) {
        return "mod/s07.dat";
    }
    if (port_TextsEqual(key, "stage07")) {
        return "mod/stage07.bin";
    }
    if (port_TextsEqual(key, "stg07")) {
        return "mod/stg07.bin";
    }
    return NULL;
}

u32 port_ModFileSize(const char *path) {
    MadeFile *file = made_File(path);

    return file != NULL ? file->size : 0;
}

u32 port_ModReadFile(const char *path, void *dest, u32 room) {
    MadeFile *file = made_File(path);
    u32 size;

    if (file == NULL) {
        return 0;
    }
    size = file->size < room ? file->size : room;
    test_Copy(dest, file->bytes, size);
    return size;
}

void *port_HostAllocate(u32 size) { return test_Alloc(size); }

s32 port_HostMakeFolder(const char *path) { return path != NULL; }

s32 port_HostCreate(const char *path) {
    MadeFile *file = &g_made_files[g_made_file_count];

    if (g_made_file_count >= MADE_FILES) {
        return -1;
    }
    test_Copy(file->path, path, port_TextLength(path) + 1);
    file->bytes = NULL;
    file->size = 0;
    return g_made_file_count++;
}

s32 port_HostAppend(s32 handle, const void *bytes, u32 count) {
    MadeFile *file = &g_made_files[handle];

    file->bytes = test_Alloc(count);
    test_Copy(file->bytes, bytes, count);
    file->size = count;
    return 1;
}

void port_HostClose(s32 file) { (void)file; }

/* what is said goes to the log; the test keeps it quiet */
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

/* ---- the tests ---- */

static void test_Directory(void) {
    PortDisc disc = {-1, MADE_SECTORS};
    const PortDiscFile *file;

    /* the root, six folders and nine files */
    CHECK_EQ(port_ReadDiscFiles(&disc), 16);
    CHECK(port_FindDiscFile("MOTION/U123.BBM") != NULL);
    file = port_FindDiscFile("loadse/stage07.bin");
    CHECK(file != NULL);
    if (file != NULL) {
        CHECK_EQ(file->sector, MADE_STAGE07);
        CHECK_EQ(file->size, MADE_STAGE07_BYTES);
        CHECK(port_TextsEqual(file->path + file->name_at, "STAGE07.BIN"));
    }
    /* the record in the folder's second sector */
    file = port_FindDiscFile("LOADSE/STAGE09.BIN");
    CHECK(file != NULL && file->sector == 65);
    CHECK(port_FindDiscFile("SYSTEM.CNF") != NULL);
    CHECK(port_FindDiscFile("LOADSE/STAGE07") == NULL);
    CHECK(port_FindDiscFile("NDATA/NDATA.DAT") != NULL);
    /* by a sector inside: 5000 bytes are three sectors */
    file = port_DiscFileAt(MADE_STAGE07 + 2);
    CHECK(file != NULL && file->sector == MADE_STAGE07);
    CHECK(port_DiscFileAt(MADE_STAGE07 + 3) == NULL);
    file = port_DiscFileAt(61);
    CHECK(file != NULL && port_TextsEqual(file->path, "LOADSE/BB2_M1.XA"));
    /* a folder's sectors are no file's */
    CHECK(port_DiscFileAt(MADE_ROOT) == NULL);
}

static void test_Keys(void) {
    char key[PORT_STAGE_KEY_SIZE];

    CHECK(port_StageFileKey(7, PORT_STAGE_FILE_GROUND, key, sizeof(key)));
    CHECK(port_TextsEqual(key, "s07"));
    CHECK(port_StageFileKey(29, PORT_STAGE_FILE_SOUND, key, sizeof(key)));
    CHECK(port_TextsEqual(key, "stage29"));
    CHECK(port_StageFileKey(31, PORT_STAGE_FILE_CARD, key, sizeof(key)));
    CHECK(port_TextsEqual(key, "stg31"));
    /* the modes' own stages have no card; there are 38 stages */
    CHECK(!port_StageFileKey(32, PORT_STAGE_FILE_CARD, key, sizeof(key)));
    CHECK(!port_StageFileKey(38, PORT_STAGE_FILE_GROUND, key, sizeof(key)));
    CHECK(!port_StageFileKey(-1, PORT_STAGE_FILE_SOUND, key, sizeof(key)));
}

/* Whether sector `sector`'s data, as the reader hands it out, is the
 * disc's own: `raw` as port_ReadDiscSector fills it. */
static s32 made_IsDisc(const u8 *raw, u32 sector) {
    return test_BytesEqual(
        raw + PORT_DISC_DATA_OFFSET, g_made_disc + sector * SECTOR, SECTOR);
}

/* Whether it is bytes `first` on of a mod's file of `size` bytes (and
 * zeros past its end). */
static s32 made_IsMod(const u8 *raw, u32 first, u32 size) {
    u32 i;

    for (i = 0; i < SECTOR; i++) {
        u8 want = first + i < size ? (u8)((first + i) * 3 + 1) : 0;

        if (raw[PORT_DISC_DATA_OFFSET + i] != want) {
            return FALSE;
        }
    }
    return TRUE;
}

static void test_StageMods(void) {
    PortDisc disc = {-1, MADE_SECTORS};
    static u8 raw[PORT_DISC_SECTOR_BYTES];
    MadeFile *dump;
    u32 sector;
    u32 i;

    made_ModFile("mod/s07.dat", 5000);
    made_ModFile("mod/stage07.bin", 7000);
    made_ModFile("mod/stg07.bin", 100);
    made_ModFile("mod/u123.bbm", 2500);
    port_SetStageDumpFolder("dump");
    port_StartStageMods(&disc);
    CHECK(g_port_disc_override == port_OverrideDiscSector);
    /* the dump: NDATA's stage in whole sectors, the others to the byte */
    dump = made_File("dump/s07.dat");
    CHECK(dump != NULL && dump->size == MADE_S07_SECTORS * SECTOR);
    if (dump != NULL) {
        for (i = 0; i < dump->size; i++) {
            if (dump->bytes[i] !=
                made_Byte(MADE_DAT + MADE_S07_START + i / SECTOR, i % SECTOR)) {
                break;
            }
        }
        CHECK_EQ(i, dump->size);
    }
    dump = made_File("dump/stage07.bin");
    CHECK(dump != NULL && dump->size == MADE_STAGE07_BYTES);
    dump = made_File("dump/stg07.bin");
    CHECK(dump != NULL && dump->size == MADE_STG07_BYTES);
    CHECK(made_File("dump/stage29.bin") != NULL);
    /* stage 29's ground was left out of the build: nothing to write */
    CHECK(made_File("dump/s29.dat") == NULL);
    /* the mod's s07 over NDATA.DAT's sectors of it, the last part zeros */
    for (sector = 0; sector < MADE_S07_SECTORS; sector++) {
        u32 at = MADE_DAT + MADE_S07_START + sector;

        test_Copy(
            raw + PORT_DISC_DATA_OFFSET, g_made_disc + at * SECTOR, SECTOR);
        CHECK(port_OverrideDiscSector(at, raw));
        CHECK(made_IsMod(raw, sector * SECTOR, 5000));
    }
    /* stg07: 100 bytes, then zeros to the end of the disc's file */
    test_Copy(
        raw + PORT_DISC_DATA_OFFSET, g_made_disc + MADE_STG07 * SECTOR, SECTOR);
    CHECK(port_OverrideDiscSector(MADE_STG07, raw));
    CHECK(made_IsMod(raw, 0, 100));
    CHECK(port_OverrideDiscSector(MADE_STG07 + 9, raw));
    CHECK(made_IsMod(raw, 9 * SECTOR, 100));
    /* the motion file U123.BBM: the mod's 2,500 bytes over its two
     * sectors, the rest zeros; COMMON.BBM, which no mod has, the disc's */
    for (sector = 0; sector < 2; sector++) {
        test_Copy(raw + PORT_DISC_DATA_OFFSET,
                  g_made_disc + (MADE_U123 + sector) * SECTOR, SECTOR);
        CHECK(port_OverrideDiscSector(MADE_U123 + sector, raw));
        CHECK(made_IsMod(raw, sector * SECTOR, 2500));
    }
    /* the sectors around them, and stage07 whose mod's file is too long,
     * are the disc's */
    for (sector = 0; sector < MADE_SECTORS; sector++) {
        s32 is_s07 = sector >= MADE_DAT + MADE_S07_START &&
                     sector < MADE_DAT + MADE_S07_START + MADE_S07_SECTORS;
        s32 is_stg07 = sector >= MADE_STG07 && sector < MADE_STG07 + 10;
        s32 is_u123 = sector >= MADE_U123 && sector < MADE_U123 + 2;

        if (is_s07 || is_stg07 || is_u123) {
            continue;
        }
        test_Copy(
            raw + PORT_DISC_DATA_OFFSET, g_made_disc + sector * SECTOR, SECTOR);
        CHECK(!port_OverrideDiscSector(sector, raw));
        CHECK(made_IsDisc(raw, sector));
    }
}

void test_Main(void) {
    made_Disc();
    test_Directory();
    test_Keys();
    test_StageMods();
}
