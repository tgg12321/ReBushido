/* What the modes keep between fights (fight.h): the order of opponents of a
 * run against the CPU, chanbara's hundred opponents and its best times, a
 * team battle's matches. The debug menu's first screen sits ahead of
 * them. */
#define INCLUDE_ASM_USE_MACRO_INC 1
#include "common.h"
#include "include_asm.h"
#include <psxsdk/inline_o.h>
#include <psxsdk/gtemac.h>
#include "bb2.h"

u8 g_team_scores[8][2];
u8 g_team_rosters[5][2];
u8 g_team_weapons[5][2];
u8 g_vscpu_order[24];

/* ---- the debug menu (fight_start.c) ---- */

/* SCREEN_DEBUG_MENU_INIT: the debug menu needs nothing loaded. */
void menu_DebugInit(void) {
    disp_Reset();
    g_sys_screen_frames = 0;
    g_sys_screen = SCREEN_DEBUG_MENU_UPDATE;
}

/* ---- VSCPU: one opponent after another ---- */

/* The characters met in any order. */
#define VSCPU_ANY_ORDER 0x3EF3DF
/* Where in the run the gunners come, if they have been won. */
#define VSCPU_GUNNER_PLACE 10

/* Makes the run's order: the unlocked characters other than the player's
 * own, shuffled; then the gunners that have been won, put in at the
 * eleventh place (in either order when there are both); and last, if it has
 * been won, the last character of the player's clan. */
void fight_BuildVsCpuOrder(void) {
    s32 gunner_bits[2];
    s32 count;
    s32 i;
    s32 first;
    u8 *slot;
    s32 one;

    {
        s32 mask =
            ~(1 << g_fighter_roster_characters[g_fight_config.roster[0]]) &
            VSCPU_ANY_ORDER;
        s32 bits;
        count = 0;
        i = 0;
        bits = SAVE_UNLOCKS() & mask;
        one = 1;
        slot = g_vscpu_order;
        do {
            if (bits & (one << i)) {
                *slot = i;
                slot++;
                count++;
            }
            i++;
        } while (i < CHARACTER_COUNT);
    }

    for (i = 0; i < 4 * CHARACTER_COUNT; i++) {
        first = rand() % count;
        {
            s32 second = rand() % count;
            s32 swapped = g_vscpu_order[first];
            g_vscpu_order[first] = g_vscpu_order[second];
            g_vscpu_order[second] = swapped;
        }
    }

    if (SAVE_UNLOCKS() &
        (UNLOCK_BIT(CHARACTER_TSUBAME) | UNLOCK_BIT(CHARACTER_KATZE))) {
        s32 gunner_1 = SAVE_UNLOCKS() & UNLOCK_BIT(CHARACTER_TSUBAME);
        s32 gunner_2 = SAVE_UNLOCKS() & UNLOCK_BIT(CHARACTER_KATZE);
        gunner_bits[0] = gunner_1;
        gunner_bits[1] = gunner_2;
        if (gunner_1 == 0) {
            gunner_bits[0] = gunner_2;
            gunner_bits[1] = 0;
        } else if (gunner_2 != 0) {
            if (rand() & 1) {
                s32 later = gunner_bits[1];
                s32 sooner = gunner_bits[0];
                gunner_bits[0] = later;
                gunner_bits[1] = sooner;
            }
        }
        if (count > VSCPU_GUNNER_PLACE) {
            i = count;
            do {
                g_vscpu_order[i] = g_vscpu_order[i - 1];
                i--;
            } while (i > VSCPU_GUNNER_PLACE);
            {
                s32 gunner = CHARACTER_KATZE;
                if (gunner_bits[0] & UNLOCK_BIT(CHARACTER_TSUBAME)) {
                    gunner = CHARACTER_TSUBAME;
                }
                g_vscpu_order[VSCPU_GUNNER_PLACE] = gunner;
            }
        } else {
            s32 gunner = CHARACTER_KATZE;
            if (gunner_bits[0] & UNLOCK_BIT(CHARACTER_TSUBAME)) {
                gunner = CHARACTER_TSUBAME;
            }
            g_vscpu_order[count] = gunner;
        }
        count++;
        if (gunner_bits[1] != 0) {
            /* the other one goes to the end */
            u8 *end = &g_vscpu_order[count];
            count++;
            {
                s32 gunner = CHARACTER_KATZE;
                if (gunner_bits[1] & UNLOCK_BIT(CHARACTER_TSUBAME)) {
                    gunner = CHARACTER_TSUBAME;
                }
                *end = gunner;
            }
        }
    }
    {
        u8 clan = g_fighter_clans
            [g_fighter_roster_characters[g_fight_config.roster[0]]];
        if (clan != 0) {
            if (SAVE_UNLOCKS() & UNLOCK_BIT(CHARACTER_LAST_2)) {
                g_vscpu_order[count] = CHARACTER_LAST_2;
                count++;
            }
        } else {
            if (SAVE_UNLOCKS() & UNLOCK_BIT(CHARACTER_LAST_1)) {
                g_vscpu_order[count] = CHARACTER_LAST_1;
                count++;
            }
        }
    }
    g_vscpu_count = count;
    g_vscpu_faced = 0;
    g_vscpu_weapon_cursor = 0;
}

/* Sets up the fight against the run's next opponent, on that character's
 * stage: the gunners with their guns, the others with the next weapon of
 * the list (a spear for the second clan where the list has the naginata).
 * When there is none left the run is over: with an ending if it met all
 * twenty, otherwise with the count of those it beat. */
void fight_NextVsCpuOpponent(void) {
    u8 faced = g_vscpu_faced;
    u8 count = g_vscpu_count;

    if (faced == count) {
        g_disp_loading_screen = LOADING_SCREEN_NONE;
        g_disp_loading_noise_burst = 0;
        if (faced == VSCPU_FULL_RUN) {
            u8 clan = g_fighter_clans[g_fighters[0].character];
            s32 ending = ENDING_VSCPU;
            if (clan != 0) {
                ending = ENDING_VSCPU + 1;
            }
            g_fight_ending = ending;
            g_sys_screen = SCREEN_ENDING_INIT;
        } else {
            g_sys_screen = SCREEN_KILL_COUNT_INIT;
        }
    } else {
        u8 character;
        g_fight_story_wounds_p2 = 0;
        g_vscpu_faced = faced + 1;
        character = g_vscpu_order[faced];
        g_fight_config.roster[1] = g_fighter_character_rosters[character];
        if (character == CHARACTER_TSUBAME) {
            g_fight_config.weapon[1] = WEAPON_ID_LONG_GUN;
        } else if (character == CHARACTER_KATZE) {
            g_fight_config.weapon[1] = WEAPON_ID_SHORT_GUN;
        } else {
            u8 weapon_cursor = g_vscpu_weapon_cursor;
            s8 weapon;
            g_vscpu_weapon_cursor = weapon_cursor + 1;
            weapon = g_vscpu_weapons[weapon_cursor];
            g_fight_config.weapon[1] = weapon;
            if (weapon == WEAPON_ID_NAGINATA) {
                u8 clan = g_fighter_clans[character];
                if (clan != 0) {
                    g_fight_config.weapon[1] = WEAPON_ID_YARI;
                }
            }
        }
        g_fight_config.cpu_levels[1] = FIGHT_CPU_LEVEL_DEFAULT;
        {
            s16 stage = g_vscpu_stages[character];
#if RB_PORT
            /* the stage the stage pages chose, for every opponent of the
             * run (port_play.h) */
            if (port_PlayStageChosen(g_fight_mode)) {
                stage = g_fight_config.stage;
            }
#endif
            g_sys_screen = SCREEN_FIGHT_INIT;
            g_fight_stage = stage;
        }
    }
}

/* ---- chanbara: a hundred opponents against the clock ---- */

/* Puts the run's time among the three best times of the save data, if it
 * belongs there, with who did it and with what. */
static void fight_InsertBestTime(void) {
    /* FAKE: g_saved_record.times, not through a pointer */
    SaveRecord *record = &g_saved_record;
    s32 rank = SAVE_TIME_COUNT;
    s32 above;
    s32 i;

    while (1) {
        above = rank - 1;
        if (record->times[above].time_frames < g_chanbara_time) {
            break;
        }
        rank = above;
        if (rank <= 0) {
            break;
        }
    }
    g_chanbara_rank = rank;
    if (rank < SAVE_TIME_COUNT) {
        for (i = SAVE_TIME_COUNT - 1; i > rank; i--) {
            record->times[i] = record->times[i - 1];
        }
        record->times[rank].holder_character_index = g_fighters[0].character;
        record->times[rank].holder_weapon_id = g_fighters[0].weapon_id;
        record->times[rank].time_frames = g_chanbara_time;
    }
}

/* Makes the next of the hundred the opponent (its kind, model, colour and
 * roster entry) and returns 1. After the last the run is over: it wins the
 * player's clan its gunner if it took fifteen minutes or less and its last
 * character if it needed no continue, the time goes among the best times,
 * and the replay follows; returns 0. */
s32 fight_NextChanbaraOpponent(void) {
    u8 count;
    s32 index;

    count = g_chanbara_count;
    index = count & 0xFF;
    if (index == CHANBARA_OPPONENTS) {
        g_chanbara_fast_unlock = 0;
        g_chanbara_clean_unlock = 0;

        if (g_chanbara_time <= CHANBARA_FAST_TIME) {
            s32 *unlocks;
            s32 old;
            s32 bit;

            bit = UNLOCK_BIT(CHARACTER_TSUBAME);
            if (g_fighter_clans
                    [g_fighter_roster_characters[g_fight_config.roster[0]]] !=
                0) {
                bit = UNLOCK_BIT(CHARACTER_KATZE);
            }
            unlocks = &g_saved_record.unlock_mask;
            old = *unlocks;
            g_chanbara_fast_unlock = (old & bit) == 0;
            *unlocks = old | bit;
        }

        if (g_fight_continues == 0) {
            s32 *unlocks;
            s32 old;
            s32 bit;

            bit = UNLOCK_BIT(CHARACTER_LAST_1);
            if (g_fighter_clans
                    [g_fighter_roster_characters[g_fight_config.roster[0]]] !=
                0) {
                bit = UNLOCK_BIT(CHARACTER_LAST_2);
            }
            unlocks = &g_saved_record.unlock_mask;
            old = *unlocks;
            g_chanbara_clean_unlock = (old & bit) == 0;
            *unlocks = old | bit;
        }

        fight_InsertBestTime();
        g_sys_screen = SCREEN_REPLAY_INIT;
        return 0;
    } else {
        u8 *opponent = g_chanbara_opponents[index];
        u8(*models)[CHANBARA_KINDS] = g_chanbara_models;
        u8(*rosters)[CHANBARA_KINDS] = g_chanbara_rosters;
        s32 kind;
        s32 clan_row;

        g_chanbara_count = count + 1;
        g_fight_story_wounds_p2 = 0;
        kind = opponent[0];
        g_chanbara_kind = kind;
        clan_row =
            g_fighter_clans
                [g_fighter_roster_characters[g_fight_config.roster[0]]] == 0;
        kind &= 0xFF;
        g_chanbara_model = models[clan_row][kind];
        g_chanbara_tint.r = opponent[1];
        g_chanbara_tint.g = opponent[2];
        g_chanbara_tint.b = opponent[3];
        g_fight_config.roster[1] = rosters[clan_row][kind];
        return 1;
    }
}

/* What follows the table of best times: the ending of a character the run
 * has just won; otherwise the save screen if the time is on the table, and
 * the menu if not. */
void fight_LeaveBestTimes(void) {
    s32 ending;
    if (g_chanbara_fast_unlock) {
        ending = ENDING_CHANBARA_FAST;
        if (g_fighter_clans[g_fighters[0].character] != 0) {
            ending = ENDING_CHANBARA_FAST + 1;
        }
        g_fight_ending = ending;
        g_sys_screen = SCREEN_ENDING_INIT;
        return;
    }
    if (g_chanbara_clean_unlock) {
        ending = ENDING_CHANBARA_CLEAN;
        if (g_fighter_clans[g_fighters[0].character] != 0) {
            ending = ENDING_CHANBARA_CLEAN + 1;
        }
        g_fight_ending = ending;
        g_sys_screen = SCREEN_ENDING_INIT;
        return;
    }
    if (g_chanbara_rank < SAVE_TIME_COUNT) {
        g_sys_screen = SCREEN_SAVE_INIT;
    } else {
        g_sys_screen = SCREEN_MENU_INIT;
    }
}

/* ---- the team battle's matches ---- */

/* The match is over: says who won it. The side with the points it needed,
 * or with more points; on equal points the side with fewer warnings, which is
 * given a point for it; otherwise a draw. The points are kept for the
 * table. */
void fight_RecordTeamMatch(void) {
    u8 points_1, points_2;

    points_1 = g_team_points;
    if (points_1 == g_team_points_to_win) {
        g_team_results[g_team_match] = TEAM_RESULT_SIDE_1;
    } else {
        points_2 = g_team_points_p2;
        if (points_2 == g_team_points_to_win) {
            g_team_results[g_team_match] = TEAM_RESULT_SIDE_2;
        } else if (points_2 < points_1) {
            g_team_results[g_team_match] = TEAM_RESULT_SIDE_1;
        } else if (points_1 < points_2) {
            g_team_results[g_team_match] = TEAM_RESULT_SIDE_2;
        } else {
            if (g_team_warnings < g_team_warnings_p2) {
                g_team_points = points_1 + 1;
                g_team_results[g_team_match] = TEAM_RESULT_SIDE_1;
            } else if (g_team_warnings_p2 < g_team_warnings) {
                g_team_points_p2 = points_2 + 1;
                g_team_results[g_team_match] = TEAM_RESULT_SIDE_2;
            } else {
                g_team_results[g_team_match] = TEAM_RESULT_DRAW;
            }
        }
    }
    g_team_scores[g_team_match][0] = g_team_points;
    g_team_scores[g_team_match][1] = g_team_points_p2;
    g_team_match++;
}

/* Packs the points of the matches so far for the table that is the
 * loading screen between matches: two bits a side, 3 for a match not yet
 * fought. */
static void fight_PackTeamScores(void) {
    s32 shift = 0;
    s32 i = 0;
    s32 packed = 0;
    u8 count;
    s32 fought;
    u8 *score;

    g_disp_loading_screen = LOADING_SCREEN_TEAM_SCORES;
    count = g_team_match_count;
    if (i < count) {
        fought = g_team_match;
        do {
            s32 is_fought;
            s32 side;
            score = g_team_scores[i];
            is_fought = (i < fought);
            for (side = 0; side < 2; side++) {
                if (is_fought) {
                    packed |= score[side] << shift;
                } else {
                    packed |= 3 << shift;
                }
                shift += 2;
            }
            i++;
            count = g_team_match_count;
        } while (i < count);
    }

    g_team_packed_scores = packed;
}

/* Sets up the battle's next match: the two teams' fighters of the next
 * place, no points, the clock at nothing. After the last match the result
 * is shown. */
void fight_NextTeamMatch(void) {
    fight_PackTeamScores();
    if (g_team_match == g_team_match_count) {
        g_disp_loading_screen = LOADING_SCREEN_NONE;
        g_disp_loading_noise_burst = 0;
        g_sys_screen = SCREEN_TEAM_RESULT_INIT;
    } else {
        s32 side;
        g_team_time_frames = 0;
        g_team_points_p2 = 0;
        g_team_points = 0;
        g_team_warnings_p2 = 0;
        g_team_warnings = 0;
        g_team_break_timer = 0;
        g_team_begin_countdown = TEAM_BEGIN_FRAMES;
        g_team_time_up_timer = 0;
        g_team_out_timer = 0;
        g_team_result_timer = 0;
        g_team_warning_timer = 0;
        g_team_ippon_timer = 0;
        for (side = 0; side < 2; side++) {
            g_fight_config.roster[side] = g_team_rosters[g_team_match][side];
            g_fight_config.weapon[side] = g_team_weapons[g_team_match][side];
        }
        disp_Blank();
        cdrom_StopAndWait();
        g_sys_screen = SCREEN_FIGHT_INIT;
    }
}
