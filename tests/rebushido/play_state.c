/* Where the play tests read the fighters and the fight's settings
 * (tests/play/state.toml). */
#include "common.h"
#include "bb2.h"
#include "runtime/runtime.h"

/* Prints where a member is in its structure, for play_state.py to set
 * against the offsets written in tests/play/state.toml. */
#define WHERE(type, member)                                                    \
    test_Print(#type "." #member " %d\n", (int)__builtin_offsetof(type, member))

void test_Main(void) {
    WHERE(Fighter, is_cpu);
    WHERE(Fighter, character);
    WHERE(Fighter, roster_id);
    WHERE(Fighter, weapon_id);
    WHERE(Fighter, is_defeated);
    WHERE(Fighter, foot_pos.vx);
    WHERE(Fighter, foot_pos.vy);
    WHERE(Fighter, foot_pos.vz);
    WHERE(Fighter, off_arm_ok);
    WHERE(Fighter, leg_wounds);
    WHERE(Fighter, arm_wounds);
    WHERE(Fighter, body_wounds);
    WHERE(FightConfig, roster[0]);
    WHERE(FightConfig, roster[1]);
    WHERE(FightConfig, weapon[0]);
    WHERE(FightConfig, weapon[1]);
    WHERE(FightConfig, is_cpu[1]);
    WHERE(FightConfig, stage);
    WHERE(FightConfig, mode);
    /* the second fighter is one Fighter further */
    test_Print("Fighter %d\n", (int)sizeof(Fighter));
    CHECK_EQ(sizeof(g_fighters) / sizeof(g_fighters[0]), 2);
}
