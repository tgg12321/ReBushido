/* The colour conversions of math.c against Python's colorsys. */
#include "src/main/math.c"
#include "runtime/runtime.h"

/* A stand-in for tex.c's: whether the game is in black and white, which
 * math_HsvToRgb asks. The test says. */
static s32 g_test_grayscale;

s32 tex_IsGrayscale(void) { return g_test_grayscale; }

/* Prints, for a grid of hues, saturations and values, the colour
 * math_HsvToRgb makes and what math_RgbToHsv reads back from it; color.py
 * compares both with colorsys. Here: in black and white a colour comes out
 * gray. */
void test_Main(void) {
    s32 hsv[3];
    s32 rgb[3];
    s32 back[3];

    for (hsv[0] = 0; hsv[0] <= ONE; hsv[0] += ONE / 48) {
        for (hsv[1] = 0; hsv[1] <= ONE; hsv[1] += ONE / 8) {
            for (hsv[2] = 0; hsv[2] <= ONE; hsv[2] += ONE / 8) {
                math_HsvToRgb(hsv, rgb);
                math_RgbToHsv(rgb, back);
                test_Print("%d %d %d  %d %d %d  %d %d %d\n", hsv[0], hsv[1],
                           hsv[2], rgb[0], rgb[1], rgb[2], back[0], back[1],
                           back[2]);
            }
        }
    }

    g_test_grayscale = 1;
    hsv[0] = ONE / 3;
    hsv[1] = ONE;
    hsv[2] = ONE / 2;
    math_HsvToRgb(hsv, rgb);
    CHECK_EQ(rgb[0], ONE / 2);
    CHECK_EQ(rgb[1], ONE / 2);
    CHECK_EQ(rgb[2], ONE / 2);
}
