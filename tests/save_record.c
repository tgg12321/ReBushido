/* The save file's record, checksums, patches and header (memcard_save.c). */
#include "src/main/memcard_save.c"
#include "runtime/runtime.h"

/* A stand-in for Sony's libc: the header's title is copied with it. */
u8 *strcpy(u8 *dest, u8 *src) {
    u8 *to = dest;

    while ((*to++ = *src++) != 0) {
    }
    return dest;
}

/* A page of the PlayStation's memory, at its own address: where a save
 * file's patches are allowed to write. */
#define TEST_RAM_PAGE 0x80010000

static SaveRecord test_MakeRecord(void) {
    SaveRecord record;

    memset(&record, 0, sizeof(record));
    record.unlock_mask = SAVE_DEFAULT_UNLOCKS;
    record.staff_roll_character_mask = 0x15;
    record.times[0].holder_character_index = 3;
    record.times[0].holder_weapon_id = 2;
    record.times[0].time_frames = 5400;
    record.times[2].time_frames = 123456;
    record.button_config[1] = 7;
    record.flags = SAVE_FLAG_EASY_CPU;
    return record;
}

static s32 test_ByteSum(void *bytes, s32 count) {
    s32 sum = 0;
    u8 *byte = bytes;

    while (count-- > 0) {
        sum += *byte++;
    }
    return sum;
}

static void test_PrintHeader(const char *label, u8 *header) {
    s32 i;

    test_Print("%s ", label);
    for (i = 0; i < MEMCARD_HEADER_SIZE; i++) {
        test_Print("%02x", header[i]);
    }
    test_Print("\n");
}

/* Packs a record, and takes it apart again: whole, with a copy or all three
 * spoiled, when only asked whether it is good, and with patches. Then
 * prints the file header for three sets of unlocked fighters, for
 * save_record.py to read the title of. */
void test_Main(void) {
    SaveRecord record = test_MakeRecord();
    SaveData data;
    u8 header[MEMCARD_HEADER_SIZE];
    u16 *ram = test_AllocAt(TEST_RAM_PAGE, 0x1000);
    s32 i;

    /* the image is the card's smallest file: header and data, half each */
    CHECK_EQ(sizeof(SaveData), MEMCARD_FILE_SIZE - MEMCARD_HEADER_SIZE);

    /* packed: three copies, each with the sum of the record's bytes */
    g_saved_record = record;
    memset(&data, 0xEE, sizeof(data));
    memcard_PackSaveData((u8 *)&data);
    for (i = 0; i < SAVE_COPIES; i++) {
        CHECK(test_BytesEqual(&data.copies[i], &record, sizeof(record)));
        CHECK_EQ(data.checksums[i], test_ByteSum(&record, sizeof(record)));
    }
    for (i = 0; i < SAVE_PATCH_COUNT; i++) {
        CHECK(PTR(data.patch_addrs[i]) == NULL);
        CHECK_EQ(data.patch_values[i], 0);
    }
    CHECK_EQ(data.unused, 0);

    /* unpacked: the record comes back */
    memset(&g_saved_record, 0, sizeof(g_saved_record));
    CHECK_EQ(memcard_UnpackSaveData(&data), 1);
    CHECK(test_BytesEqual(&g_saved_record, &record, sizeof(record)));

    /* a spoiled first copy: the second is taken */
    data.copies[0].unlock_mask ^= 0x40;
    data.copies[1].button_config[0] = 9;
    data.checksums[1] += 9;
    memset(&g_saved_record, 0, sizeof(g_saved_record));
    CHECK_EQ(memcard_UnpackSaveData(&data), 1);
    CHECK_EQ(g_saved_record.button_config[0], 9);
    CHECK_EQ(g_saved_record.unlock_mask, SAVE_DEFAULT_UNLOCKS);

    /* all three spoiled: nothing is loaded */
    data.checksums[1]++;
    data.checksums[2]--;
    memset(&g_saved_record, 0, sizeof(g_saved_record));
    CHECK_EQ(memcard_UnpackSaveData(&data), 0);
    CHECK_EQ(g_saved_record.unlock_mask, 0);

    /* the boot's check only asks whether the save is good */
    g_saved_record = record;
    memcard_PackSaveData((u8 *)&data);
    memset(&g_saved_record, 0, sizeof(g_saved_record));
    g_memcard_verify_only = 1;
    CHECK_EQ(memcard_UnpackSaveData(&data), 1);
    CHECK_EQ(g_saved_record.unlock_mask, 0);
    g_memcard_verify_only = 0;

    /* patches: a halfword written where the file says, if that is in the
     * PlayStation's memory; one that is not is left out (this one points
     * at a variable of the test's, far below) */
    {
        u16 outside = 0x1111;

        SET_PTR(data.patch_addrs[0], &ram[4]);
        data.patch_values[0] = 0xBEEF;
        SET_PTR(data.patch_addrs[1], &outside);
        data.patch_values[1] = 0x2222;
        CHECK_EQ(memcard_UnpackSaveData(&data), 1);
        CHECK_EQ(ram[4], 0xBEEF);
        CHECK_EQ(outside, 0x1111);
        CHECK(test_BytesEqual(&g_saved_record, &record, sizeof(record)));

        /* a record flagged "patches only" is not loaded, its patches are */
        memset(&g_saved_record, 0, sizeof(g_saved_record));
        data.patch_values[0] = 0xCAFE;
        for (i = 0; i < SAVE_COPIES; i++) {
            data.copies[i].flags |= SAVE_FLAG_PATCHES_ONLY;
            data.checksums[i] += SAVE_FLAG_PATCHES_ONLY;
        }
        CHECK_EQ(memcard_UnpackSaveData(&data), 1);
        CHECK_EQ(ram[4], 0xCAFE);
        CHECK_EQ(g_saved_record.unlock_mask, 0);
    }

    /* the header, with the fighters a new game has, with every fighter,
     * and with none */
    g_saved_record.unlock_mask = SAVE_DEFAULT_UNLOCKS;
    memcard_BuildFileHeader(header);
    test_PrintHeader("default", header);
    g_saved_record.unlock_mask = (1 << TITLE_UNLOCK_BITS) - 1;
    memcard_BuildFileHeader(header);
    test_PrintHeader("all", header);
    g_saved_record.unlock_mask = 0;
    memcard_BuildFileHeader(header);
    test_PrintHeader("none", header);
}
