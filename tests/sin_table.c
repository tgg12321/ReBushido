/* The sine table (math_sin.c) against the real sine. */
#include "src/main/math_sin.c"
#include "runtime/runtime.h"

/* Prints the table, a line an angle; sin_table.py compares each entry with
 * sin(). Here: the table is the same wave whichever quarter it is read in. */
void test_Main(void) {
    s32 angle;

    for (angle = 0; angle < ANGLE_TURN; angle++) {
        test_Print("%d\n", g_math_sin_table[angle]);
    }
    for (angle = 0; angle < ANGLE_TURN / 2; angle++) {
        /* the second half turn is the first, negated */
        CHECK_EQ(
            g_math_sin_table[angle + ANGLE_TURN / 2], -g_math_sin_table[angle]);
    }
    for (angle = 0; angle <= ANGLE_TURN / 4; angle++) {
        /* the second quarter is the first, mirrored */
        CHECK_EQ(
            g_math_sin_table[ANGLE_TURN / 2 - angle], g_math_sin_table[angle]);
    }
}
