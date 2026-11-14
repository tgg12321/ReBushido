/* The square root tables and math_TableSqrt (math.h) against real roots. */
#include "common.h"
#include <psxsdk/inline_o.h>
#include <psxsdk/gtemac.h>
#include "src/main/fight_data.c"
#include "src/main/math_asm.c"
#include "runtime/runtime.h"
#include "stubs/gte.c"

/* Prints the three tables and math_TableSqrt's root of a spread of numbers;
 * roots.py compares them with roots it works out itself. (math_Sqrt, which
 * reads the second table, is assembly and is not run.) */
void test_Main(void) {
    s32 i;
    u32 x;

    for (i = 0; i < MATH_SQRT_TABLE_SIZE; i++) {
        test_Print("sqrt8 %d %d\n", i, g_math_sqrt8_table[i]);
    }
    /* the assembly's two tables are halfwords, written as words */
    for (i = 0; i < 256; i++) {
        test_Print("sqrt512 %d %d\n", i, ((const u16 *)g_math_sqrt_table)[i]);
    }
    for (i = 0; i < 192; i++) {
        test_Print(
            "rsqrt %d %d\n", i + 64, ((const u16 *)g_math_rsqrt_table)[i]);
    }

    /* every number up to 0x20000, then steps of about one part in 200 up
     * to the largest positive number, then a few with the top bit set */
    for (x = 0; x < 0x20000; x++) {
        test_Print("root %u %d\n", x, math_TableSqrt(x));
    }
    for (; x < 0x7FFFFFFF - x / 200; x += x / 200) {
        test_Print("root %u %d\n", x, math_TableSqrt(x));
    }
    test_Print("root %u %d\n", 0x7FFFFFFF, math_TableSqrt(0x7FFFFFFF));
    test_Print("root %u %d\n", 0x80000000U, math_TableSqrt(0x80000000U));
    test_Print("root %u %d\n", 0xC0000000U, math_TableSqrt(0xC0000000U));
    test_Print("root %u %d\n", 0xFFFFFFFFU, math_TableSqrt(0xFFFFFFFFU));
}
