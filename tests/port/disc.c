/* The port's disc: a made-up image through its reader and its libcd. */
/* The native port reads the game from the player's disc image
 * (src/port/disc.c) and answers the game's disc code as the drive does
 * (src/port/cd.c). Both are run here on an image this test makes up in
 * memory, forty sectors of numbers that say where they are: nothing of
 * the game's. The files the reader opens are this test's own stand-ins
 * for the system's (port_host.h). */
#include "common.h"
#include <psxsdk/libcd.h>
#include "port_movie.h"

/* The movie's stream (src/port/stream.c), which the drive hands a
 * sector to while a movie plays: no movie plays here, so the drive
 * works as it does for a file's sectors (tests/port/mdec.c runs the
 * stream itself). */
s32 port_StreamArmed(void) { return 0; }

void port_StartDiscStream(s32 sector, s32 double_speed) {
    (void)sector;
    (void)double_speed;
}

void port_StopDiscStream(void) {}

s32 port_PumpStream(void) { return 0; }

void (*CdDataCallback(void (*func)()))() {
    (void)func;
    return NULL;
}

#include "src/port/disc.c"
#include "src/port/cd.c"
#include "src/port/xa.c"
#include "src/port/text.c"
#include "src/port/stub.c"
#include "runtime/runtime.h"

/* ---- the made-up image ---- */

#define MADE_SECTORS 40
/* Sectors of the image that are not plain data. */
#define MADE_FORM2 20     /* a sector of sound: form 2 */
#define MADE_NO_SYNC 30   /* its sync is broken */
#define MADE_ELSEWHERE 31 /* it says it is another sector */
#define MADE_MODE1 32     /* a mode 1 sector */

static u8 *g_made_image;
static const char g_made_cue[] =
    "FILE \"disc.bin\" BINARY\n  TRACK 01 MODE2/2352\n    INDEX 01 00:00:00\n";
static const char g_made_cue_no_file[] = "TRACK 01 MODE2/2352\n";

/* The byte of the image's data at `offset` of `sector`: every one says
 * which sector and where in it. */
static u8 made_Byte(u32 sector, u32 offset) {
    return (u8)(sector * 37 + offset * 11 + (offset >> 8));
}

static void made_Build(void) {
    u32 sector;
    u32 i;

    g_made_image = test_Alloc(MADE_SECTORS * PORT_DISC_SECTOR_BYTES);
    for (sector = 0; sector < MADE_SECTORS; sector++) {
        u8 *raw = g_made_image + sector * PORT_DISC_SECTOR_BYTES;

        for (i = 1; i < 11; i++) {
            raw[i] = 0xFF;
        }
        port_DiscSectorToPosition(sector, raw + PORT_DISC_SYNC_BYTES);
        raw[PORT_DISC_SYNC_BYTES + 3] = 2;
        /* the subheader, twice: file, channel, submode, coding */
        raw[16] = raw[20] = 1;
        raw[18] = raw[22] = 0x08;
        for (i = 0; i < PORT_DISC_SECTOR_BYTES - PORT_DISC_DATA_OFFSET; i++) {
            raw[PORT_DISC_DATA_OFFSET + i] = made_Byte(sector, i);
        }
    }
    g_made_image[MADE_FORM2 * PORT_DISC_SECTOR_BYTES + 18] = 0x64;
    g_made_image[MADE_NO_SYNC * PORT_DISC_SECTOR_BYTES + 5] = 0x00;
    port_DiscSectorToPosition(
        7, g_made_image + MADE_ELSEWHERE * PORT_DISC_SECTOR_BYTES +
               PORT_DISC_SYNC_BYTES);
    g_made_image[MADE_MODE1 * PORT_DISC_SECTOR_BYTES + 15] = 1;
}

/* ---- the system, for the reader: five files that are memory ---- */

static struct {
    const char *path;
    const u8 *bytes;
    u32 size;
} g_made_files[5];

static void made_AddFiles(void) {
    g_made_files[0].path = "made/disc.bin";
    g_made_files[0].bytes = g_made_image;
    g_made_files[0].size = MADE_SECTORS * PORT_DISC_SECTOR_BYTES;
    g_made_files[1].path = "made/disc.cue";
    g_made_files[1].bytes = (const u8 *)g_made_cue;
    g_made_files[1].size = sizeof(g_made_cue) - 1;
    /* an image that ends in the middle of a sector */
    g_made_files[2].path = "made/cut.bin";
    g_made_files[2].bytes = g_made_image;
    g_made_files[2].size = 3 * PORT_DISC_SECTOR_BYTES + 100;
    g_made_files[3].path = "made/nofile.cue";
    g_made_files[3].bytes = (const u8 *)g_made_cue_no_file;
    g_made_files[3].size = sizeof(g_made_cue_no_file) - 1;
    /* a cue sheet beside no image: it names disc.bin in another folder */
    g_made_files[4].path = "other/disc.cue";
    g_made_files[4].bytes = (const u8 *)g_made_cue;
    g_made_files[4].size = sizeof(g_made_cue) - 1;
}

s32 port_HostOpen(const char *path) {
    s32 i;

    for (i = 0; i < 5; i++) {
        if (test_StringsEqual(path, g_made_files[i].path)) {
            return i;
        }
    }
    return -1;
}

void port_HostClose(s32 file) {}

u32 port_HostFileSize(s32 file) { return g_made_files[file].size; }

u32 port_HostReadAt(s32 file, u32 offset, void *dest, u32 count) {
    u32 size = g_made_files[file].size;

    if (offset >= size) {
        return 0;
    }
    if (count > size - offset) {
        count = size - offset;
    }
    test_Copy(dest, g_made_files[file].bytes + offset, count);
    return count;
}

/* What text.c and cd.c want besides. */
void port_HostWrite(s32 stream, const char *bytes, u32 count) {}

void port_Quit(s32 code) {
    test_Count(0, __FILE__, __LINE__, "the port stopped itself (%d)", code);
    for (;;) {
    }
}

/* The sound's clock, which the drive goes by while it plays (sound.c):
 * here the test moves it. */
static u32 g_made_samples;

u32 port_GetSampleCount(void) { return g_made_samples; }

/* ---- the reader ---- */

static void test_Positions(void) {
    static const struct {
        u32 sector;
        u8 minute, second, frame;
    } known[] = {
        {0, 0x00, 0x02, 0x00},
        {1, 0x00, 0x02, 0x01},
        {74, 0x00, 0x02, 0x74},
        {75, 0x00, 0x03, 0x00},
        {4349, 0x00, 0x59, 0x74},
        {4350, 0x01, 0x00, 0x00},
        {33394, 0x07, 0x27, 0x19}, /* cdrom.c's own example */
        {333000 - 150, 0x74, 0x00, 0x00},
    };

    u8 position[4];
    u32 i;

    for (i = 0; i < sizeof(known) / sizeof(known[0]); i++) {
        position[3] = 0x5A;
        port_DiscSectorToPosition(known[i].sector, position);
        CHECK_EQ(position[0], known[i].minute);
        CHECK_EQ(position[1], known[i].second);
        CHECK_EQ(position[2], known[i].frame);
        CHECK_EQ(position[3], 0x5A); /* three bytes, no more */
        CHECK_EQ(port_DiscPositionToSector(position), known[i].sector);
    }
    for (i = 0; i < 20000; i += 7) {
        port_DiscSectorToPosition(i, position);
        CHECK_EQ(port_DiscPositionToSector(position), i);
    }
    /* not two decimal digits; a 60th second, a 75th frame; before the
     * disc's first sector */
    position[0] = 0x00, position[1] = 0x0A, position[2] = 0x00;
    CHECK_EQ(port_DiscPositionToSector(position), -1);
    position[0] = 0x00, position[1] = 0x60, position[2] = 0x00;
    CHECK_EQ(port_DiscPositionToSector(position), -1);
    position[0] = 0x00, position[1] = 0x02, position[2] = 0x75;
    CHECK_EQ(port_DiscPositionToSector(position), -1);
    position[0] = 0x00, position[1] = 0x01, position[2] = 0x74;
    CHECK_EQ(port_DiscPositionToSector(position), -1);
}

static void test_Opening(void) {
    PortDisc disc;

    CHECK(port_OpenDisc(&disc, "made/disc.bin") == NULL);
    CHECK_EQ(disc.sector_count, MADE_SECTORS);
    port_CloseDisc(&disc);
    CHECK_EQ(disc.sector_count, 0);
    /* by its cue sheet, which names the image beside it; in capitals too */
    CHECK(port_OpenDisc(&disc, "made/disc.cue") == NULL);
    CHECK_EQ(disc.sector_count, MADE_SECTORS);
    CHECK_EQ(disc.file, 0);
    CHECK(port_OpenDisc(&disc, "made/nothere.bin") != NULL);
    CHECK_EQ(disc.file, -1);
    CHECK(port_OpenDisc(&disc, "made/nothere.cue") != NULL);
    CHECK(port_OpenDisc(&disc, "made/cut.bin") != NULL);
    CHECK_EQ(disc.sector_count, 0);
    CHECK(port_OpenDisc(&disc, "made/nofile.cue") != NULL);
    CHECK(port_OpenDisc(&disc, "other/disc.cue") != NULL);
    CHECK(port_TextEndsWith("A/B.CUE", ".cue"));
    CHECK(!port_TextEndsWith("cue", ".cue"));
}

static void test_Sectors(void) {
    static u8 raw[PORT_DISC_SECTOR_BYTES + 4];
    PortDisc disc;
    u32 sector;
    u32 i;
    s32 wrong;

    CHECK(port_OpenDisc(&disc, "made/disc.cue") == NULL);
    for (sector = 0; sector < MADE_SECTORS; sector++) {
        s32 want = PORT_DISC_OK;

        if (sector == MADE_NO_SYNC || sector == MADE_ELSEWHERE) {
            want = PORT_DISC_NOT_A_SECTOR;
        }
        raw[PORT_DISC_SECTOR_BYTES] = 0xA5;
        CHECK_EQ(port_ReadDiscSector(&disc, sector, raw), want);
        CHECK_EQ(raw[PORT_DISC_SECTOR_BYTES], 0xA5); /* 2352 bytes, no more */
        if (want != PORT_DISC_OK) {
            continue;
        }
        wrong = 0;
        for (i = 0; i < PORT_DISC_SECTOR_BYTES - PORT_DISC_DATA_OFFSET; i++) {
            wrong += raw[PORT_DISC_DATA_OFFSET + i] != made_Byte(sector, i);
        }
        CHECK_EQ(wrong, 0);
        CHECK_EQ(port_DiscPositionToSector(raw + PORT_DISC_SYNC_BYTES), sector);
    }
    /* past the end: the first sector the image has not, and far beyond */
    CHECK_EQ(
        port_ReadDiscSector(&disc, MADE_SECTORS, raw), PORT_DISC_OUT_OF_RANGE);
    CHECK_EQ(
        port_ReadDiscSector(&disc, 0xFFFFFFFF, raw), PORT_DISC_OUT_OF_RANGE);
    CHECK_EQ(
        port_ReadDiscSector(&disc, 0x80000000, raw), PORT_DISC_OUT_OF_RANGE);
    port_CloseDisc(&disc);
    CHECK_EQ(port_ReadDiscSector(&disc, 0, raw), PORT_DISC_OUT_OF_RANGE);
}

static void test_Data(void) {
    static u8 data[6 * PORT_DISC_DATA_BYTES + 4];
    PortDisc disc;
    u32 done;
    u32 i;
    s32 wrong = 0;

    CHECK(port_OpenDisc(&disc, "made/disc.bin") == NULL);
    /* five sectors, one after another, and nothing past them */
    data[5 * PORT_DISC_DATA_BYTES] = 0xA5;
    done = 99;
    CHECK_EQ(port_ReadDiscData(&disc, 3, 5, data, &done), PORT_DISC_OK);
    CHECK_EQ(done, 5);
    for (i = 0; i < 5 * PORT_DISC_DATA_BYTES; i++) {
        wrong += data[i] != made_Byte(3 + i / PORT_DISC_DATA_BYTES,
                                      i % PORT_DISC_DATA_BYTES);
    }
    CHECK_EQ(wrong, 0);
    CHECK_EQ(data[5 * PORT_DISC_DATA_BYTES], 0xA5);
    /* none at all */
    CHECK_EQ(port_ReadDiscData(&disc, 3, 0, data, &done), PORT_DISC_OK);
    CHECK_EQ(done, 0);
    /* into a sector of sound: the two before it are read */
    CHECK_EQ(port_ReadDiscData(&disc, MADE_FORM2 - 2, 4, data, &done),
             PORT_DISC_NOT_DATA);
    CHECK_EQ(done, 2);
    CHECK_EQ(data[PORT_DISC_DATA_BYTES], made_Byte(MADE_FORM2 - 1, 0));
    /* a mode 1 sector is not the game's kind of data either */
    CHECK_EQ(port_ReadDiscData(&disc, MADE_MODE1, 1, data, &done),
             PORT_DISC_NOT_DATA);
    /* off the end: the three there are, are read */
    CHECK_EQ(port_ReadDiscData(&disc, MADE_SECTORS - 3, 5, data, &done),
             PORT_DISC_OUT_OF_RANGE);
    CHECK_EQ(done, 3);
    CHECK_EQ(port_ReadDiscData(&disc, MADE_NO_SYNC - 1, 3, data, NULL),
             PORT_DISC_NOT_A_SECTOR);
    port_CloseDisc(&disc);
}

/* ---- the drive, as the game's disc code uses it (cdrom.c) ---- */

static s32 g_take_left;   /* sectors still wanted; negative if it failed */
static s32 g_take_next;   /* the sector expected */
static u8 *g_take_cursor; /* where it goes */
static s32 g_take_calls;

/* What cdrom_ReadyCallback does with each sector that arrives: its own
 * position first, twelve bytes, and then its data. */
static void test_TakeSector(u8 intr, u8 *result) {
    s32 header[4];

    g_take_calls++;
    if (intr == CdlDataReady) {
        CdGetSector(header, 3);
        if (CdPosToInt((CdlLOC *)header) != g_take_next) {
            g_take_left = -2;
        } else {
            CdGetSector(g_take_cursor, PORT_DISC_DATA_BYTES / 4);
            g_take_cursor += PORT_DISC_DATA_BYTES;
            g_take_next++;
            if (--g_take_left != 0) {
                return;
            }
        }
    } else {
        g_take_left = -1;
    }
    CdReadyCallback(NULL);
    CdControlF(CdlPause, NULL);
}

/* A read of `count` sectors from `sector`, the way cdrom_Update asks for
 * one. Returns what was left when the drive had nothing more to say. */
static s32 test_Read(s32 sector, s32 count, u8 *dest) {
    u8 mode[4];
    u8 result[8];
    CdlLOC loc;
    s32 polls;

    mode[0] = CdlModeSpeed | CdlModeSize1;
    CdControlF(CdlSetmode, mode);
    CHECK_EQ(CdSync(1, result), CdlComplete);
    CdIntToPos(sector, &loc);
    CHECK_EQ(CdPosToInt(&loc), sector);
    CdControl(CdlSetloc, (u8 *)&loc, NULL);
    CHECK_EQ(CdSync(1, result), CdlComplete);
    g_take_left = count;
    g_take_next = sector;
    g_take_cursor = dest;
    g_take_calls = 0;
    CdReadyCallback(test_TakeSector);
    CdControlF(CdlReadN, (u8 *)&loc);
    /* nothing arrives until the drive is next asked */
    CHECK_EQ(g_take_calls, 0);
    for (polls = 0; polls < 4; polls++) {
        CHECK_EQ(CdSync(1, result), CdlComplete);
    }
    CHECK(!(result[0] & CdlStatRead)); /* the callback paused it */
    return g_take_left;
}

static void test_Drive(void) {
    static u8 data[4 * PORT_DISC_DATA_BYTES + 4];
    PortDisc disc;
    u8 result[8];
    CdlLOC loc;
    u32 i;
    s32 wrong = 0;

    CHECK(port_OpenDisc(&disc, "made/disc.bin") == NULL);
    port_InsertDisc(&disc);
    CHECK_EQ(CdInit(), 1);
    CHECK_EQ(CdSync(1, result), CdlComplete);
    CHECK_EQ(result[0], CdlStatStandby);

    /* a read of four sectors: four calls, the data in its place */
    data[4 * PORT_DISC_DATA_BYTES] = 0xA5;
    CHECK_EQ(test_Read(8, 4, data), 0);
    CHECK_EQ(g_take_calls, 4);
    for (i = 0; i < 4 * PORT_DISC_DATA_BYTES; i++) {
        wrong += data[i] != made_Byte(8 + i / PORT_DISC_DATA_BYTES,
                                      i % PORT_DISC_DATA_BYTES);
    }
    CHECK_EQ(wrong, 0);
    CHECK_EQ(data[4 * PORT_DISC_DATA_BYTES], 0xA5);
    /* off the end of the disc: the two sectors there are, then an error */
    CHECK_EQ(test_Read(MADE_SECTORS - 2, 4, data), -1);
    CHECK_EQ(g_take_calls, 3);
    /* a sector that says it is another one: an error, not the data */
    CHECK_EQ(test_Read(MADE_ELSEWHERE, 1, data), -1);
    /* and the drive reads again after it */
    CHECK_EQ(test_Read(0, 1, data), 0);
    CHECK_EQ(data[5], made_Byte(0, 5));

    /* Sony's own reader, which the game waits on: data alone, 2048 bytes
     * a sector, from where the drive was last sent */
    CdIntToPos(12, &loc);
    CdControl(CdlSetloc, (u8 *)&loc, NULL);
    CHECK_EQ(CdRead(2, (u32 *)data, CdlModeSpeed), 1);
    CHECK_EQ(CdReadSync(1, NULL), 0);
    wrong = 0;
    for (i = 0; i < 2 * PORT_DISC_DATA_BYTES; i++) {
        wrong += data[i] != made_Byte(12 + i / PORT_DISC_DATA_BYTES,
                                      i % PORT_DISC_DATA_BYTES);
    }
    CHECK_EQ(wrong, 0);

    /* sound playing: the drive's place moves with the sound's clock,
     * 150 sectors a second at double speed (44,100 samples), and stops
     * where it is paused */
    result[0] = CdlModeSpeed | CdlModeRT | CdlModeSF;
    CdControlF(CdlSetmode, result);
    CdIntToPos(1000, &loc);
    CdControlF(CdlSetloc, (u8 *)&loc);
    CdControlF(CdlReadS, NULL);
    g_made_samples += 44100;
    CdControlF(CdlGetlocP, NULL);
    CHECK_EQ(CdSync(1, result), CdlComplete);
    CHECK_EQ(CdPosToInt((CdlLOC *)&result[5]), 1150);
    /* a thirtieth of a second: five sectors */
    g_made_samples += 1470;
    CdControlF(CdlPause, NULL);
    g_made_samples += 441000;
    CdControlF(CdlGetlocP, NULL);
    CdSync(1, result);
    CHECK_EQ(CdPosToInt((CdlLOC *)&result[5]), 1155);
    /* an hour on, at single speed: 75 sectors a second */
    result[0] = CdlModeRT;
    CdControlF(CdlSetmode, result);
    CdIntToPos(0, &loc);
    CdControlF(CdlSetloc, (u8 *)&loc);
    CdControlF(CdlReadS, NULL);
    g_made_samples += 3600U * 44100U;
    CdControlF(CdlGetlocP, NULL);
    CdSync(1, result);
    CHECK_EQ(CdPosToInt((CdlLOC *)&result[5]), 3600 * 75);
    CdControlF(CdlPause, NULL);

    /* the sectors that pass under the drive while it plays go to the
     * decoder if they are sound of the file and channel chosen: the
     * image has one sector of sound, MADE_FORM2, of file 1, channel 0 */
    {
        CdlFILTER filter;
        u32 before = port_GetXaSectorCount();

        result[0] = CdlModeSpeed | CdlModeRT | CdlModeSF;
        CdControlF(CdlSetmode, result);
        filter.file = 1;
        filter.chan = 0;
        CdControlB(CdlSetfilter, (u8 *)&filter, NULL);
        CdIntToPos(MADE_FORM2 - 2, &loc);
        CdControlF(CdlSetloc, (u8 *)&loc);
        CdControlF(CdlReadS, NULL);
        /* 294 samples are a sector at double speed: two sectors pass,
         * neither of them sound */
        port_PlayDisc(2 * 294);
        CHECK_EQ(port_GetXaSectorCount(), before);
        g_made_samples += 2 * 294;
        /* the next is the sector of sound, and is taken once */
        port_PlayDisc(294);
        CHECK_EQ(port_GetXaSectorCount(), before + 1);
        g_made_samples += 294;
        port_PlayDisc(294);
        CHECK_EQ(port_GetXaSectorCount(), before + 1);
        /* its 4032 samples of one channel wait to be played */
        CHECK_EQ(g_port_xa.queue_count, 18 * 8 * 28);
        /* a new stream forgets them; with another channel chosen the
         * sector is passed over */
        filter.chan = 5;
        CdControlB(CdlSetfilter, (u8 *)&filter, NULL);
        CdIntToPos(MADE_FORM2, &loc);
        CdControlF(CdlSetloc, (u8 *)&loc);
        CdControlF(CdlReadS, NULL);
        CHECK_EQ(g_port_xa.queue_count, 0);
        port_PlayDisc(3 * 294);
        CHECK_EQ(port_GetXaSectorCount(), before + 1);
        /* and with no choosing asked for, it is played whatever it is */
        result[0] = CdlModeSpeed | CdlModeRT;
        CdControlF(CdlSetmode, result);
        CdControlF(CdlSetloc, (u8 *)&loc);
        CdControlF(CdlReadS, NULL);
        port_PlayDisc(294);
        CHECK_EQ(port_GetXaSectorCount(), before + 2);
        /* past the end of the image nothing is played, and nothing
         * stops */
        CdIntToPos(MADE_SECTORS - 1, &loc);
        CdControlF(CdlSetloc, (u8 *)&loc);
        CdControlF(CdlReadS, NULL);
        port_PlayDisc(10 * 294);
        CHECK_EQ(port_GetXaSectorCount(), before + 2);
        CdControlF(CdlPause, NULL);
    }
    port_InsertDisc(NULL);
}

void test_Main(void) {
    made_Build();
    made_AddFiles();
    test_Positions();
    test_Opening();
    test_Sectors();
    test_Data();
    test_Drive();
}
