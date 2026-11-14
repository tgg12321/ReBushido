/* A splash when twelve are alive (eff.c, RB_FIX_SPLASH_POOL). */
#include "src/main/eff.c"
#include "src/rebushido/settings.c"
#include "runtime/runtime.h"

#define TEST_ALL_LIVE ((1 << SPLASH_COUNT) - 1)

static s32 g_test_position[3];

/* Asks for a splash at (n, n + 1, n + 2). */
static void test_Spawn(s32 n) {
    g_test_position[0] = n;
    g_test_position[1] = n + 1;
    g_test_position[2] = n + 2;
    g_eff_position = g_test_position;
    CHECK_EQ(eff_SpawnSplash(), 1);
}

void test_Main(void) {
    s32 bugfixes;
    s32 i;

    CHECK_EQ(RB_FIX_SPLASH_POOL, 1);

    /* With a place free the fix changes nothing: the first free place is
     * taken, whatever the setting. */
    for (bugfixes = 0; bugfixes < 2; bugfixes++) {
        g_rb_settings.bugfixes = bugfixes;
        g_eff_splash_live = 0;
        for (i = 0; i < SPLASH_COUNT; i++) {
            test_Spawn(100 * i);
            CHECK_EQ(g_eff_splash_live, (2 << i) - 1);
            CHECK_EQ(g_eff_splash_positions[i].x, 100 * i);
            CHECK_EQ(g_eff_splash_positions[i].z, 100 * i + 2);
            CHECK_EQ(g_eff_splash_ages[i], 0);
        }
        /* one in the middle is over, and the next takes its place */
        g_eff_splash_live &= ~(1 << 5);
        g_eff_splash_ages[5] = 16;
        test_Spawn(7000);
        CHECK_EQ(g_eff_splash_live, TEST_ALL_LIVE);
        CHECK_EQ(g_eff_splash_positions[5].x, 7000);
        CHECK_EQ(g_eff_splash_ages[5], 0);
    }

    /* All twelve alive, with the fix: the splash is not made, and none of
     * the twelve is touched. (The original writes a thirteenth after the
     * tables' ends, which a test cannot ask for.) */
    g_rb_settings.bugfixes = TRUE;
    for (i = 0; i < SPLASH_COUNT; i++) {
        g_eff_splash_ages[i] = i + 1;
    }
    test_Spawn(9000);
    CHECK_EQ(g_eff_splash_live, TEST_ALL_LIVE);
    CHECK_EQ(g_eff_stage[EFF_SPLASH], 1);
    for (i = 0; i < SPLASH_COUNT; i++) {
        CHECK(g_eff_splash_positions[i].x != 9000);
        CHECK_EQ(g_eff_splash_ages[i], i + 1);
    }
}
