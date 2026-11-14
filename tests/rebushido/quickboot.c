/* Quick boot (quickboot.c): asks for the fight the build chose and starts its
 * mode. */
/* What `make quickboot` gives the file, here with a fight of the test's. */
#define RB_QUICK_BOOT_BUILD 1
#define RB_QB_MODE FIGHT_MODE_VSCPU
#define RB_QB_P1 ROSTER_TSUBAME
#define RB_QB_P2 ROSTER_KATZE
#define RB_QB_WEAPON1 WEAPON_ID_LONG_GUN
#define RB_QB_WEAPON2 WEAPON_ID_SHORT_GUN
#define RB_QB_STAGE STAGE_SNAKE_CLIFF
#define RB_QB_CPU2 1
#include "src/rebushido/quickboot.c"
#include "runtime/runtime.h"

/* What quick boot touches of the game, stood in for. */
FightConfig g_fight_config;
s8 g_fight_loaded_stage;
static s32 g_test_vabs_closed;
static s32 g_test_modes_started;
/* what was asked for when the mode was started */
static FightConfig g_test_started_with;
static s32 g_test_stage_at_start;

void snd_CloseAllVabs(void) { g_test_vabs_closed++; }

void fight_StartMode(void) {
    g_test_modes_started++;
    g_test_started_with = g_fight_config;
    g_test_stage_at_start = g_fight_loaded_stage;
}

void test_Main(void) {
    CHECK_EQ(RB_QUICK_BOOT, 1);

    /* as fight_InitConfig leaves it, and a stage that looks loaded */
    g_fight_config.cpu_levels[0] = FIGHT_CPU_LEVEL_DEFAULT;
    g_fight_config.cpu_levels[1] = FIGHT_CPU_LEVEL_DEFAULT;
    g_fight_loaded_stage = 0;

    rb_QuickBoot();

    /* the mode is started once, after everything is asked for */
    CHECK_EQ(g_test_modes_started, 1);
    CHECK_EQ(g_test_vabs_closed, 1);
    CHECK_EQ(g_test_stage_at_start, -1);
    CHECK_EQ(g_test_started_with.mode, FIGHT_MODE_VSCPU);
    CHECK_EQ(g_test_started_with.roster[0], ROSTER_TSUBAME);
    CHECK_EQ(g_test_started_with.roster[1], ROSTER_KATZE);
    CHECK_EQ(g_test_started_with.weapon[0], WEAPON_ID_LONG_GUN);
    CHECK_EQ(g_test_started_with.weapon[1], WEAPON_ID_SHORT_GUN);
    CHECK_EQ(g_test_started_with.stage, STAGE_SNAKE_CLIFF);
    CHECK_EQ(g_test_started_with.is_cpu[0], 0);
    CHECK_EQ(g_test_started_with.is_cpu[1], 1);
    /* and what it does not choose is left alone */
    CHECK_EQ(g_test_started_with.cpu_levels[0], FIGHT_CPU_LEVEL_DEFAULT);
    CHECK_EQ(g_test_started_with.cpu_levels[1], FIGHT_CPU_LEVEL_DEFAULT);
    CHECK_EQ(g_test_started_with.first_person, 0);

    /* the build's choice, where a play test can read it */
    CHECK_EQ(sizeof(g_rb_quick_boot), 7);
    CHECK_EQ(g_rb_quick_boot[0], FIGHT_MODE_VSCPU);
    CHECK_EQ(g_rb_quick_boot[5], STAGE_SNAKE_CLIFF);
}
