/* The settings' file (save.c): its checksum, its header, the settings back
 * whole, and every kind of file that must not be read. */
#include "src/rebushido/settings.c"
#include "src/rebushido/save.c"
#include "runtime/runtime.h"

/* The settings at the start of the file's second half. */
#define TEST_DATA RB_SAVE_HEADER_SIZE
/* Bytes after the file, which packing must leave alone. */
#define TEST_GUARD 16

static u8 g_test_image[RB_SAVE_FILE_SIZE + TEST_GUARD];

/* Writes the checksum of a file whose settings the test has changed. */
static void test_Seal(u8 *image) {
    u32 checksum = rb_Checksum(image + TEST_DATA, RB_SAVE_AT_CHECKSUM);
    s32 i;

    for (i = 0; i < 4; i++) {
        image[TEST_DATA + RB_SAVE_AT_CHECKSUM + i] = checksum >> (i * 8);
    }
}

/* A file with Bugfixes on and the remastered picture. */
static void test_PackChosen(void) {
    g_rb_settings.bugfixes = TRUE;
    g_rb_settings.graphics = RB_GRAPHICS_ENHANCED;
    rb_PackSave(g_test_image);
    rb_ResetSettings();
}

/* The file must not be read, and must leave the settings as they were. */
static void test_Refused(const char *why) {
    g_rb_settings.bugfixes = TRUE;
    g_rb_settings.graphics = RB_GRAPHICS_ORIGINAL;
    test_Count(rb_UnpackSave(g_test_image) == FALSE, __FILE__, __LINE__,
               "%s is refused", why);
    test_Count(g_rb_settings.bugfixes == TRUE &&
                   g_rb_settings.graphics == RB_GRAPHICS_ORIGINAL,
               __FILE__, __LINE__, "%s leaves the settings alone", why);
    rb_ResetSettings();
}

void test_Main(void) {
    s32 i;

    CHECK_EQ(REBUSHIDO, 1);
    CHECK_EQ(RB_OPTIONS_PAGE, 1);

    /* the CRC-32 everybody knows the answer of; save.py has the rest */
    CHECK(rb_Checksum((const u8 *)"123456789", 9) == 0xCBF43926);
    CHECK(rb_Checksum((const u8 *)"", 0) == 0);

    /* the file's name: a card's names are twenty bytes, and the game looks
     * for its own save by how a name begins */
    CHECK(sizeof(g_rb_save_file_name) <= 20);
    CHECK(!test_BytesEqual(g_rb_save_file_name, "BASLUS-00663BUSHIDO2",
                           sizeof(g_rb_save_file_name) - 1));
    CHECK(test_BytesEqual(g_rb_save_file_name, "BASLUS-00663", 12));

    /* the settings as the game starts, packed: for save.py */
    for (i = 0; i < TEST_GUARD; i++) {
        g_test_image[RB_SAVE_FILE_SIZE + i] = 0xA5;
    }
    rb_PackSave(g_test_image);
    test_Print("default ");
    for (i = 0; i < RB_SAVE_FILE_SIZE; i++) {
        test_Print("%02x", g_test_image[i]);
    }
    test_Print("\n");
    for (i = 0; i < TEST_GUARD; i++) {
        CHECK_EQ(g_test_image[RB_SAVE_FILE_SIZE + i], 0xA5);
    }
    CHECK_EQ(rb_UnpackSave(g_test_image), TRUE);
    CHECK_EQ(g_rb_settings.bugfixes, FALSE);
    CHECK_EQ(g_rb_settings.graphics, RB_GRAPHICS_ORIGINAL);

    /* chosen settings come back, each in its own place */
    test_PackChosen();
    test_Print("chosen ");
    for (i = 0; i < RB_SAVE_FILE_SIZE; i++) {
        test_Print("%02x", g_test_image[i]);
    }
    test_Print("\n");
    CHECK_EQ(g_test_image[TEST_DATA + RB_SAVE_AT_VALUES + 0], TRUE);
    CHECK_EQ(
        g_test_image[TEST_DATA + RB_SAVE_AT_VALUES + 1], RB_GRAPHICS_ENHANCED);
    CHECK_EQ(rb_UnpackSave(g_test_image), TRUE);
    CHECK_EQ(g_rb_settings.bugfixes, TRUE);
    CHECK_EQ(g_rb_settings.graphics, RB_GRAPHICS_ENHANCED);

    g_rb_settings.bugfixes = FALSE;
    g_rb_settings.graphics = RB_GRAPHICS_ENHANCED;
    rb_PackSave(g_test_image);
    rb_ResetSettings();
    CHECK_EQ(rb_UnpackSave(g_test_image), TRUE);
    CHECK_EQ(g_rb_settings.bugfixes, FALSE);
    CHECK_EQ(g_rb_settings.graphics, RB_GRAPHICS_ENHANCED);

    /* a spoiled value, a spoiled checksum, other letters, another
     * version (its checksum good), an empty file */
    test_PackChosen();
    g_test_image[TEST_DATA + RB_SAVE_AT_VALUES + 1] ^= 1;
    test_Refused("a spoiled value");

    test_PackChosen();
    g_test_image[TEST_DATA + RB_SAVE_AT_CHECKSUM + 2] ^= 0x40;
    test_Refused("a spoiled checksum");

    test_PackChosen();
    g_test_image[TEST_DATA + RB_SAVE_AT_MAGIC + 3] = 'X';
    test_Seal(g_test_image);
    test_Refused("a file with other letters");

    test_PackChosen();
    g_test_image[TEST_DATA + RB_SAVE_AT_VERSION] = RB_SAVE_VERSION + 1;
    test_Seal(g_test_image);
    test_Refused("a later version");

    test_PackChosen();
    g_test_image[TEST_DATA + RB_SAVE_AT_VERSION] = 0;
    test_Seal(g_test_image);
    test_Refused("version 0");

    for (i = 0; i < RB_SAVE_FILE_SIZE; i++) {
        g_test_image[i] = 0;
    }
    test_Refused("a file of zeroes");
    for (i = 0; i < RB_SAVE_FILE_SIZE; i++) {
        g_test_image[i] = 0xFF;
    }
    test_Refused("a file of ones");

    /* a file from before a setting was there: that one is as the
     * original, whatever its byte says */
    test_PackChosen();
    g_test_image[TEST_DATA + RB_SAVE_AT_COUNT] = 1;
    test_Seal(g_test_image);
    CHECK_EQ(rb_UnpackSave(g_test_image), TRUE);
    CHECK_EQ(g_rb_settings.bugfixes, TRUE);
    CHECK_EQ(g_rb_settings.graphics, RB_GRAPHICS_ORIGINAL);
    rb_ResetSettings();

    /* a file with settings this version does not have: the ones it has
     * are read */
    test_PackChosen();
    g_test_image[TEST_DATA + RB_SAVE_AT_COUNT] = RB_SAVE_SLOT_COUNT + 3;
    g_test_image[TEST_DATA + RB_SAVE_AT_VALUES + RB_SAVE_SLOT_COUNT] = 9;
    test_Seal(g_test_image);
    CHECK_EQ(rb_UnpackSave(g_test_image), TRUE);
    CHECK_EQ(g_rb_settings.bugfixes, TRUE);
    CHECK_EQ(g_rb_settings.graphics, RB_GRAPHICS_ENHANCED);
    rb_ResetSettings();

    /* a value this version has no choice for is as the original */
    test_PackChosen();
    g_test_image[TEST_DATA + RB_SAVE_AT_VALUES + 1] = 2;
    test_Seal(g_test_image);
    CHECK_EQ(rb_UnpackSave(g_test_image), TRUE);
    CHECK_EQ(g_rb_settings.bugfixes, TRUE);
    CHECK_EQ(g_rb_settings.graphics, RB_GRAPHICS_ORIGINAL);
    rb_ResetSettings();

    /* the file has room for every place a setting can take */
    CHECK(RB_SAVE_SLOT_COUNT <= RB_SAVE_VALUES);
    CHECK(RB_SAVE_HEADER_SIZE + RB_SAVE_AT_CHECKSUM + 4 <= RB_SAVE_FILE_SIZE);
}
