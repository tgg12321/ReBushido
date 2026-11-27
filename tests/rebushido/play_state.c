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
    WHERE(Fighter, frames_alive);
    WHERE(Fighter, is_defeated);
    WHERE(Fighter, foot_pos.vx);
    WHERE(Fighter, foot_pos.vy);
    WHERE(Fighter, foot_pos.vz);
    WHERE(Fighter, off_arm_ok);
    WHERE(Fighter, leg_wounds);
    WHERE(Fighter, arm_wounds);
    WHERE(Fighter, body_wounds);
    WHERE(Fighter, vanish_count);
    WHERE(FightConfig, roster[0]);
    WHERE(FightConfig, roster[1]);
    WHERE(FightConfig, weapon[0]);
    WHERE(FightConfig, weapon[1]);
    WHERE(FightConfig, is_cpu[1]);
    WHERE(FightConfig, stage);
    WHERE(FightConfig, mode);
    WHERE(RbSettings, bugfixes);
    WHERE(RbSettings, graphics);
    WHERE(PortStageChoice, fight_kind);
    WHERE(PortStageChoice, arena);
    WHERE(PortStageChoice, page_open);
    WHERE(PortStageChoice, on_stages);
    WHERE(PortStageChoice, cursor_kind);
    WHERE(PortStageChoice, fight_time);
    WHERE(PortStageChoice, cursor_time);
    WHERE(PortStageChoice, page_cursor);
    WHERE(PortStageChoice, page_top);
    WHERE(PortPlay, unlock_all);
    WHERE(PortPlay, stage_pages);
    WHERE(PortPlay, page_used);
    WHERE(PortPlay, unlocks);
    WHERE(PortFightMusic, row);
    WHERE(PortFightMusic, heard);
    WHERE(PortFightMusic, track);
    WHERE(PortMatch, page_rule);
    WHERE(PortMatch, page_count);
    WHERE(PortMatch, rule);
    WHERE(PortMatch, count);
    WHERE(PortMatch, lives[0]);
    WHERE(PortMatch, lives[1]);
    WHERE(PortMatch, wins[0]);
    WHERE(PortMatch, wins[1]);
    WHERE(PortMatch, fights);
    WHERE(PortMatch, last);
    WHERE(PortMatch, over);
    WHERE(PortMatch, winner);
    WHERE(PortMatch, card);
    WHERE(PortMatch, page_crew);
    WHERE(PortMatch, crew_size);
    WHERE(PortMatch, crew_picked);
    WHERE(PortMatch, crew_sides);
    WHERE(PortMatch, crew[0][0].fighter);
    WHERE(PortMatch, crew[0][1].fighter);
    WHERE(PortMatch, crew[0][2].fighter);
    WHERE(PortMatch, crew[1][0].fighter);
    WHERE(PortMatch, crew[1][1].fighter);
    WHERE(PortMatch, crew[1][2].fighter);
    WHERE(PortMatch, rule_page);
    WHERE(PortMatch, rule_row);
    WHERE(PortMatch, rule_first);
    WHERE(PortMatch, crew_most);
    WHERE(PortMatch, wounds_kept[0]);
    WHERE(PortMatch, wounds_kept[1]);
    WHERE(PortMatch, wounds[0][0]);
    WHERE(PortMatch, wounds[0][1]);
    WHERE(PortMatch, wounds[0][2]);
    WHERE(PortMatch, wounds[0][3]);
    WHERE(PortMatch, wounds[1][0]);
    WHERE(PortMatch, wounds[1][1]);
    WHERE(PortMatch, wounds[1][2]);
    WHERE(PortMatch, wounds[1][3]);
    WHERE(PortMatch, to_top);
    WHERE(PortMatch, page_teams);
    WHERE(PortMatch, teams);
    WHERE(PortTraining, is_on);
    WHERE(PortTraining, recording);
    WHERE(PortTraining, playing);
    WHERE(PortTraining, frozen);
    WHERE(PortTraining, slow);
    WHERE(PortTraining, holding);
    WHERE(PortTraining, dummy);
    WHERE(PortTraining, record_at);
    WHERE(PortTraining, play_at);
    WHERE(PortTraining, played_slot);
    WHERE(PortTraining, frames);
    WHERE(PortTraining, restarts);
    WHERE(PortTraining, holds);
    WHERE(PortTraining, shown[0][0].buttons);
    WHERE(PortTraining, shown[1][0].buttons);
    /* the hit tests' scratch, which first_frame_sweep.toml reads by
     * address: a sweep's far end as it is and as it was */
    CHECK_EQ(SCRATCHPAD, 0x1F800000);
    CHECK_EQ(__builtin_offsetof(
                 FightScratch, tail.weapon_hit.point_slots.work_points[1]),
             0x2C4);
    CHECK_EQ(__builtin_offsetof(
                 FightScratch, tail.weapon_hit.point_slots.work_points[3]),
             0x2DC);
    /* the second fighter is one Fighter further */
    test_Print("Fighter %d\n", (int)sizeof(Fighter));
    CHECK_EQ(sizeof(g_fighters) / sizeof(g_fighters[0]), 2);
}
