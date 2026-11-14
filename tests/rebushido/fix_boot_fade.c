/* The boot image's fade (main.c, RB_FIX_BOOT_FADE): the pixels it walks. */
#include "src/main/main.c"
#include "src/rebushido/settings.c"
#include "runtime/runtime.h"

/* A small picture: 8 by 4 pixels, and ten more after it that are not the
 * picture's. */
#define TEST_WIDTH 8
#define TEST_HEIGHT 4
#define TEST_PIXELS (TEST_WIDTH * TEST_HEIGHT)
#define TEST_AFTER 10
#define TEST_WHITE 0x7FFF

/* What showing the picture touches of the machine, stood in for. */
static s32 g_test_loads;

void SetDispMask(s32 mask) {}

DISPENV *SetDefDispEnv(DISPENV *env, s32 x, s32 y, s32 w, s32 h) { return env; }

DISPENV *PutDispEnv(DISPENV *env) { return env; }

s32 DrawSync(s32 mode) { return 0; }

s32 VSync(s32 mode) { return 0; }

void cdrom_WaitIdle(void) {}

s32 cdrom_GetFileIndex(s32 group, s32 file) { return 0; }

s32 cdrom_StartRead(s32 file_index, s32 dest) { return 0; }

s32 LoadImage(RECT *rect, u32 *p) {
    g_test_loads++;
    return 0;
}

/* Shows a white picture and returns its pixels as the fade left them. */
static u16 *test_Fade(s32 bugfixes) {
    u8 *file = test_Alloc(TIM_PIXELS_NO_CLUT + (TEST_PIXELS + TEST_AFTER) * 2);
    u16 *pixels = (u16 *)(file + TIM_PIXELS_NO_CLUT);
    s32 i;

    for (i = 0; i < TEST_PIXELS + TEST_AFTER; i++) {
        pixels[i] = TEST_WHITE;
    }
    g_disp_boot_image_rect.w = TEST_WIDTH;
    g_disp_boot_image_rect.h = TEST_HEIGHT;
    g_rb_settings.bugfixes = bugfixes;
    g_test_loads = 0;
    disp_ShowBootImage(file, NULL, 0);
    /* shown once, and once more for each frame of the fade */
    CHECK_EQ(g_test_loads, 1 + BOOT_IMAGE_FRAMES - BOOT_IMAGE_FADE_START);
    return pixels;
}

void test_Main(void) {
    u16 *pixels;
    s32 i;

    CHECK_EQ(RB_FIX_BOOT_FADE, 1);
    /* the header's size, which the original adds twice, is ten pixels */
    CHECK_EQ(TIM_PIXELS_NO_CLUT, TEST_AFTER * 2);

    /* The original: the first ten pixels are never faded, the rest are
     * black at the end, and so are the ten after the picture. */
    pixels = test_Fade(FALSE);
    for (i = 0; i < TEST_AFTER; i++) {
        CHECK_EQ(pixels[i], TEST_WHITE);
    }
    for (i = TEST_AFTER; i < TEST_PIXELS + TEST_AFTER; i++) {
        CHECK_EQ(pixels[i], 0);
    }

    /* Fixed: the picture's pixels, all of them and no others. */
    pixels = test_Fade(TRUE);
    for (i = 0; i < TEST_PIXELS; i++) {
        CHECK_EQ(pixels[i], 0);
    }
    for (i = TEST_PIXELS; i < TEST_PIXELS + TEST_AFTER; i++) {
        CHECK_EQ(pixels[i], TEST_WHITE);
    }
}
