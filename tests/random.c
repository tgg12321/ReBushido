/* The random numbers and the field of view (main.c), and Sony's rand. */
#include "src/main/main.c"
#include "src/main/math_sin.c"
#include "src/main/psxsdk/libc2/rand.c"
#include "runtime/runtime.h"

#define DRAWS 2000

/* What the rand() of the C standard's own example gives from a seed of 1:
 * Sony's is that one, to the number. */
static const s32 g_test_ansi_rand[10] = {
    16838, 5758, 10113, 17515, 31051, 5627, 23010, 7419, 16212, 4086,
};

/* Prints the game's random numbers from a few seeds, and the projection
 * distance for a spread of fields of view; random.py works both out its
 * own way. Here: Sony's rand() against the well-known sequence, and the
 * game's numbers again from the same seed. */
void test_Main(void) {
    static const s32 seeds[] = {0, 1, 12345, -1, 0x7FFFFFFF};
    s32 i, j;
    s32 first[8];

    for (i = 0; i < 5; i++) {
        rng_SetSeed(seeds[i]);
        for (j = 0; j < DRAWS; j++) {
            test_Print("rng %d %d\n", seeds[i], rng_Next());
        }
    }

    rng_SetSeed(777);
    for (i = 0; i < 8; i++) {
        first[i] = rng_Next();
    }
    rng_SetSeed(777);
    for (i = 0; i < 8; i++) {
        CHECK_EQ(rng_Next(), first[i]);
    }

    srand(1);
    for (i = 0; i < 10; i++) {
        CHECK_EQ(rand(), g_test_ansi_rand[i]);
    }

    for (i = 10; i <= 170; i += 5) {
        test_Print("fov %d %d\n", i, math_FovToScreenDist(i));
    }
}
