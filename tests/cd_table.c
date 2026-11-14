/* The CD file table and its position arithmetic (cdrom.c) against the disc. */
#include "src/main/cdrom.c"
#include "src/main/psxsdk/libcd/sys.c"
#include "runtime/runtime.h"

#define FILE_COUNT (s32)(sizeof(g_cdrom_files) / sizeof(g_cdrom_files[0]))

/* Prints every file of the table as the sector it starts at and its size,
 * for cd_table.py to compare with the disc's directory. Here, with Sony's
 * own conversions between a position and a sector (which are C): a
 * position goes to a sector and back, a read is set up for the right
 * sectors, and a file's number is found from its group. */
void test_Main(void) {
    s32 i;
    s32 u134; /* MOTION/U134.BBM, which starts at 00:59:00 */
    CdlLOC loc;
    u8 dest[4];

    for (i = 0; i < FILE_COUNT; i++) {
        test_Print("file %d %d %u\n", i, CdPosToInt(&g_cdrom_files[i].loc),
                   g_cdrom_files[i].size);
    }

    /* the table's own example: 07:27:19 is sector 33394 of the disc */
    loc.minute = 0x07;
    loc.second = 0x27;
    loc.sector = 0x19;
    CHECK_EQ(CdPosToInt(&loc), (7 * 60 + 27 - 2) * 75 + 19);
    CHECK_EQ(CdPosToInt(&loc), 33394);
    for (i = 0; i < 300000; i += 997) {
        CdIntToPos(i, &loc);
        CHECK_EQ(CdPosToInt(&loc), i);
    }

    /* a whole file: as many sectors as hold its bytes */
    g_cdrom.state = CDROM_STATE_IDLE;
    CHECK_EQ(cdrom_StartRead(1, (s32)dest), 1);
    CHECK_EQ(g_cdrom.read_sector_count, 2); /* NDATA.INF: 3604 bytes */
    CHECK_EQ(g_cdrom.read_dest, (s32)dest);
    CHECK_EQ(g_cdrom.state, CDROM_STATE_READ_SETMODE);
    /* nothing more is taken while that runs */
    CHECK_EQ(cdrom_StartRead(0, (s32)dest), 0);
    CHECK_EQ(cdrom_StartReadAt(0, (s32)dest, 10, 3), 0);
    CHECK_EQ(g_cdrom.file_index, 1);

    /* part of a file: some sectors of it, from so many sectors in. The
     * step from 00:59:74 to 01:00:00 is in the way of this one */
    u134 = cdrom_GetFileIndex(CDROM_GROUP_MOTION, MOTION_FILE_SETS + 3);
    g_cdrom.state = CDROM_STATE_IDLE;
    CHECK_EQ(cdrom_StartReadAt(u134, (s32)dest, 80, 3), 1);
    CHECK_EQ(CdPosToInt(&g_cdrom_files[u134].loc), (59 - 2) * 75);
    CHECK_EQ(CdPosToInt(&g_cdrom.file.loc), (59 - 2) * 75 + 80);
    CHECK_EQ(g_cdrom.file.loc.minute, 0x01);
    CHECK_EQ(g_cdrom.file.loc.second, 0x00);
    CHECK_EQ(g_cdrom.file.loc.sector, 0x05);
    CHECK_EQ(g_cdrom.read_sector_count, 3);
    /* the table's own entry is not moved */
    CHECK_EQ(g_cdrom_files[u134].loc.second, 0x59);

    CHECK_EQ(cdrom_GetFileIndex(CDROM_GROUP_NDATA, 1), 1);
    CHECK_EQ(cdrom_GetFileIndex(CDROM_GROUP_MOTION, MOTION_FILE_COMMON), 3);
    CHECK_EQ(cdrom_GetFileIndex(CDROM_GROUP_MOVIE, CDROM_MOVIE_TITLE),
             FILE_COUNT - 1);
    CHECK_EQ(cdrom_GetFileSize(1), 3604);
}
