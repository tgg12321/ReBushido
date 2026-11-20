/* The fight's own screens (fight.h): setting a fight up, a frame of it, the
 * moment after the last blow and the menu that follows; with what the mode
 * adds to each frame and what it does when a fighter has fallen. The sound
 * and camera functions that sit among them are under banners. */
#define INCLUDE_ASM_USE_MACRO_INC 1
#include "common.h"
#include "include_asm.h"
#include <psxsdk/inline_o.h>
#include <psxsdk/gtemac.h>
#include "bb2.h"

FightConfig g_fight_config;

/* The lens of the side view and of the first-person view, in degrees. */
#define CAMERA_FOV_SIDE 45
#define CAMERA_FOV_FIRST_PERSON 80

/* Room for what is kept of the stage's sound file and of a mode's. */
#define SND_STAGE_VAB_ROOM 0x2518
#define SND_JUDGE_VAB_ROOM 0xE80
#define SND_CHANBARA_VAB_ROOM 0x1B18

/* Where the stage banks and the mode's banks (chanbara or team) are kept. */
s32 g_snd_stage_vab_buffer[SND_STAGE_VAB_ROOM / 4];
s32 g_snd_mode_vab_buffer[SND_CHANBARA_VAB_ROOM / 4];

/* The music of the modes that have their own: CD tracks. */
enum {
    MUSIC_CHANBARA_CLAN_1 = 8,
    MUSIC_CHANBARA_CLAN_2 = 9,
    MUSIC_PRACTICE_ROUND = 10,
    MUSIC_PRACTICE = 11,
};

/* Chanbara: the opponents that are the first of the stage's second, third
 * and fourth areas. */
enum {
    CHANBARA_AREA_2_FIRST = 31,
    CHANBARA_AREA_3_FIRST = 51,
    CHANBARA_AREA_4_FIRST = 81,
};

/* Team battle: the judge's timing, in frames. */
enum {
    TEAM_TIME_UP_CALL = 45, /* "time up", then the second call, ... */
    TEAM_TIME_UP_FRAMES = 80,
    TEAM_BREAK_DOWN = 60, /* a round with a fighter down stops sooner */
    TEAM_BREAK_CALL = 70,
    TEAM_BREAK_FRAMES = 130,
    TEAM_OUT_FRAMES = 60,
    TEAM_WARNING_FRAMES = 60,
    TEAM_WARNINGS_FOR_POINT = 2,
    /* a point: the call, and the end; a point by fouls runs through the
     * same timer from where that one ends, with a second call */
    TEAM_IPPON_CALL = 30,
    TEAM_IPPON_FRAMES = 90,
    TEAM_IPPON_FOULS_CALL = 105,
    TEAM_IPPON_FOULS_FRAMES = 180,
    TEAM_MATCH_CLAN_CALL = 20,
    TEAM_MATCH_DRAW_FRAMES = 80,
    TEAM_MATCH_WON_FRAMES = 100,
    TEAM_NOBODY = 2, /* g_team_penalized: nobody's fault */
};

/* The most wins the display has room for. */
#define FIGHT_WINS_MAX 99

/* The story's stage card stays up this many passes of two fields (half a
 * second) after the route has been marked on it. */
#define STAGE_CARD_HOLD_PASSES 15

/* What the debug menu starts with. */
void fight_InitConfig(void) {
    g_fight_config.cpu_levels[1] = FIGHT_CPU_LEVEL_DEFAULT;
    g_fight_config.cpu_levels[0] = FIGHT_CPU_LEVEL_DEFAULT;
    g_fight_config.roster[0] = ROSTER_TATSUMI_STORY;
    g_fight_config.roster[1] = ROSTER_KAUN_STORY;
    g_fight_config.stage = STAGE_ORANGE_TREE_FIELDS;
    g_fight_config.weapon[0] = WEAPON_ID_KATANA;
    g_fight_config.weapon[1] = WEAPON_ID_KATANA;
    g_fight_config.is_cpu[1] = 0;
    g_fight_config.is_cpu[0] = 0;
    g_fight_config.mode = FIGHT_MODE_VS;
    g_fight_config.first_person = 0;
    g_fight_config.every_hit_kills = 0;
}

/* ---- one opponent after another: the story's henchmen and chanbara ---- */

/* Lets the first fighter out of a paired move its opponent did not live
 * through. */
static void fight_ReleasePlayer(void) {
    u16 kind = g_fighters[0].move_kind;
    if (kind == MOVE_KIND_PAIRED_ED || kind == MOVE_KIND_PAIRED) {
        motion_ResetStance(0);
        {
            MoveScript *stance_move = motion_GetDefaultStanceMove(0);
            g_fighters[0].move_is_common = 0;
            fighter_StartMove(0, stance_move, 0);
        }
    }
}

/* The next opponent comes on, beside the first fighter (in chanbara's new
 * area, at its start position), and the fight goes on as a new one: the
 * replay starts again and what the last one left lying is cleared away. */
static void fight_BringOnOpponent(void) {
    MoveScript *stance_move;
    fight_ReleasePlayer();
    if (g_fight_mode == FIGHT_MODE_CHANBARA && g_chanbara_area_changed) {
        fighter_Init(1, g_fight_config.is_cpu[1], g_fight_config.roster[1],
                     g_fight_config.weapon[1], 0);
    } else {
        fighter_Init(1, g_fight_config.is_cpu[1], g_fight_config.roster[1],
                     g_fight_config.weapon[1], 1);
    }
    cpu_InitFighters();
    motion_ResetStance(1);
    stance_move = motion_GetDefaultStanceMove(1);
    g_fighters[1].move_is_common = 0;
    fighter_StartMove(1, stance_move, 0);
    g_fight_defeat_frames = 0;
    g_fight_winner = FIGHT_NOBODY;
    object_RemoveStuck();
    object_RemoveDroppedWeapons();
    camera_InitSideView(&g_fighters[0], &g_fighters[1]);
    replay_Reset();
    motion_AttachFighter(1);
}

/* Chanbara's next area: stands the first fighter at its start position
 * there, still. */
static void fight_PlacePlayerAtStart(void) {
    Fighter *fighter = g_fighters;
    VECTOR other_start;
    s32 x, y, z;

    fight_GetStartPosition(0, &fighter->base_pos.x, g_fight_start_pair);
    fight_GetStartPosition(1, &other_start.vx, g_fight_start_pair);
    fighter->root_offset.x = 0;
    x = fighter->base_pos.x;
    y = fighter->base_pos.y;
    z = fighter->base_pos.z;
    fighter->root_offset.y = -FIGHTER_WAIST_HEIGHT;
    fighter->root_offset.z = 0;
    fighter->world_pos.x = x;
    fighter->world_pos.y = y - FIGHTER_WAIST_HEIGHT;
    fighter->world_pos.z = z;
    fighter->foot_pos.vx = x;
    fighter->foot_pos.vy = y;
    fighter->foot_pos.vz = z;
    fighter->prev_foot_pos = fighter->foot_pos;
    fighter->prev_root_offset = fighter->root_offset;
    fighter->velocity.vx = 0;
    fighter->velocity.vy = 0;
    fighter->velocity.vz = 0;
    fighter->prev_velocity = fighter->velocity;
    /* where the opponent's body would be: worked out and not used */
    setVector(&other_start, other_start.vx,
              other_start.vy - FIGHTER_WAIST_HEIGHT, other_start.vz);
    fighter->blade_motion[0].vx = 0;
    fighter->blade_motion[0].vy = 0;
    fighter->blade_motion[0].vz = 0;
    fighter->blade_motion[1].vx = 0;
    fighter->blade_motion[1].vy = 0;
    fighter->blade_motion[1].vz = 0;
    fighter->push.vx = 0;
    fighter->push.vy = 0;
    fighter->push.vz = 0;
    fighter->unk_144 = 0;
    fighter->turn_left = 0;
    fighter->run_drag = 0;
    fighter->use_root_heading = 0;
    fighter->run_speed = 0;
    fighter->ground_y = fighter->foot_pos.vy;
    cloth_RequestSnap(0);
}

/* The story's henchmen shout as they come on, from where they stand; not
 * in the first chapter of the second clan's characters. */
static void story_HenchmanShout(void) {
    s16 *character = &g_fighters[0].character;
    s32 sound;
    if (g_fighter_clans[*character] != 0) {
        if (g_story_chapter == 1) {
            return;
        }
    }
    if (g_fight_mode != FIGHT_MODE_STORY) {
        return;
    }
    if (g_story_opponent_named != 0) {
        return;
    }
    sound = SND_FIGHTER_COUNT + SND_FIGHTER_CHALLENGE;
    if (g_story_henchmen_left != g_story_henchmen_count) {
        if (rand() & 1) {
            sound = SND_FIGHTER_COUNT + SND_FIGHTER_SHOUT_A;
        } else {
            sound = SND_FIGHTER_COUNT + SND_FIGHTER_SHOUT_B;
        }
    }
    /* FAKE: &g_fighters[1].world_pos.x */
    snd_PlayAt(sound, (s32 *)((u8 *)character + 0x536));
}

/* ---- when a fighter has fallen ---- */

/* What follows a defeat, by the mode (see fight_UpdateDefeat): the replay,
 * unless the mode has something of its own. */
static inline void fight_AfterDefeat(void) {
    u8 old_start_pair;
    MoveScript *stance_move;

    switch (g_fight_mode) {
    case FIGHT_MODE_STORY:
        if (g_fighters[0].is_defeated) {
            break;
        }
        if (g_story_henchmen_left != 0) {
            if (--g_story_henchmen_left == 0) {
                /* the last of the band */
                snd_StopAllVoices();
                if (g_story_first_henchmen != 0) {
                    g_story_first_henchmen = 0;
                    break;
                }
                if (g_story_interlude != 0) {
                    story_SetOpponent(
                        g_story_interlude + STORY_INTERLUDE_OPPONENT);
                    if (g_story_player_roster != STORY_NO_ROSTER) {
                        story_PlayInterludeScene(
                            INTERLUDE_GUEST_MEETS_OPPONENT);
                    } else {
                        story_PlayInterludeScene(
                            INTERLUDE_PLAYER_MEETS_OPPONENT);
                    }
                } else {
                    story_RunScript();
                }
            } else {
                fight_BringOnOpponent();
                story_HenchmanShout();
            }
            return;
        }
        if (g_story_victim == 0) {
            break;
        }
        story_PlayVictimScene(VICTIM_SCENE_STRUCK);
        return;
    case FIGHT_MODE_CHANBARA:
        if (g_fighters[0].is_defeated) {
            break;
        }
        if (fight_NextChanbaraOpponent() == 0) {
            return;
        }
        g_sys_skip_frames = 1;
        disp_Reset();
        model_Load(1, g_chanbara_model, 0);
        old_start_pair = g_fight_start_pair;
        switch (g_chanbara_count) {
        case CHANBARA_AREA_2_FIRST:
            g_fight_start_pair = 1;
            resource_SelectStageArea(1);
            break;
        case CHANBARA_AREA_3_FIRST:
            g_fight_start_pair = 2;
            resource_SelectStageArea(2);
            break;
        case CHANBARA_AREA_4_FIRST:
            g_fight_start_pair = 3;
            resource_SelectStageArea(3);
            break;
        }
        if ((g_chanbara_area_changed =
                 (old_start_pair != g_fight_start_pair))) {
            fight_PlacePlayerAtStart();
            stance_move = motion_GetStanceMove(0);
            g_fighters[0].move_is_common = 0;
            fighter_StartMove(0, stance_move, 0);
        }
        fight_BringOnOpponent();
        if (g_chanbara_kind < CHANBARA_LAST_KIND) {
            model_TintOpponent(
                g_chanbara_tint.r, g_chanbara_tint.g, g_chanbara_tint.b);
        }
        return;
    case FIGHT_MODE_PRACTICE: {
        s16 first_fell;
        u8 *wins;
        /* a win for the one left standing, unless both fell */
        if ((first_fell = g_fighters[0].is_defeated) == 0 ||
            !g_fighters[1].is_defeated) {
            /* the second count is reached from the first (see fight.h) */
            wins = &g_fight_wins_p1;
            wins[first_fell != 0]++;
        }
    }
        g_fight_restart = 1;
        g_fight_restart_pair =
            fight_FindNearestStartPair(&g_fighters[g_fight_winner].world_pos);
        g_sys_screen = SCREEN_FIGHT_INIT;
        return;
    case FIGHT_MODE_VS:
    case FIGHT_MODE_NETWORK: {
        s16 first_fell;
        u8 *wins;
        if ((first_fell = g_fighters[0].is_defeated) == 0 ||
            !g_fighters[1].is_defeated) {
            /* the second count is reached from the first (see fight.h) */
            wins = &g_fight_wins_p1;
            wins[first_fell != 0]++;
        }
    } break;
    case FIGHT_MODE_TEAM:
        return;
    }
    g_sys_screen = SCREEN_REPLAY_INIT;
}

/* Once a frame of the fight. Two seconds after a fighter has fallen the
 * mode decides what follows: most go to the replay, counting the win; the
 * story's henchmen and chanbara's opponents are followed by the next, with
 * the fight running on; practice starts again where the fighters are. A
 * team battle looks after itself (fight_UpdateMode). The story also plays
 * the scene of the victim spared from here. */
void fight_UpdateDefeat(void) {
    if (g_fighters[0].is_defeated || g_fighters[1].is_defeated) {
        g_fight_defeat_frames++;
    } else if (g_fight_mode == FIGHT_MODE_STORY && g_story_victim != 0 &&
               g_fighters[0].surface_class == 1) {
        /* the player has stepped off the floor the victim kneels on, onto
         * the ground round it (SURFACE_FIRM_2, the only kind of the second
         * class there) */
        story_PlayVictimScene(VICTIM_SCENE_SPARED);
    }
    if (g_fight_defeat_frames > FIGHT_DEFEAT_FRAMES) {
        fight_AfterDefeat();
    }
    if (g_fight_wins_p1 > FIGHT_WINS_MAX) {
        g_fight_wins_p1 = FIGHT_WINS_MAX;
    }
    if (g_fight_wins_p2 > FIGHT_WINS_MAX) {
        g_fight_wins_p2 = FIGHT_WINS_MAX;
    }
}

/* ---- what the mode adds to a frame ---- */

/* Chanbara's time as the display has it. */
void fight_GetChanbaraTime(ClockTime *time) {
    s32 frames = g_chanbara_time;

    if (frames >= CHANBARA_TIME_MAX) {
        /* 99'59"99, the last the display can show */
        time->time_minutes = 99;
        time->time_seconds = SECONDS_PER_MINUTE - 1;
        time->time_hundredths = HUNDREDTHS_PER_SECOND - 1;
        return;
    }
    {
        s32 minutes = frames / FRAMES_PER_MINUTE;
        s32 seconds = frames / FRAMES_PER_SECOND - minutes * SECONDS_PER_MINUTE;
        time->time_seconds = seconds;
        {
            s32 hundredths = (g_chanbara_time % FRAMES_PER_SECOND) *
                             HUNDREDTHS_PER_SECOND / FRAMES_PER_SECOND;
            time->time_minutes = minutes;
            time->time_hundredths = hundredths;
        }
    }
}

/* Once a frame of the fight: the mode's part of the display (the number of
 * the opponent, chanbara's clock and count, the wins, a team battle's
 * points and clock), chanbara's clock itself, and all of a team battle's
 * rules.
 *
 * In a team battle the judge's calls each run on a timer, tested in the
 * order below; only when none is running is the round looked at. A kill is
 * a point against the one who fell. A fighter who steps over the court's
 * line gets a warning if it walked, ran or jumped there (two warnings are a
 * point to the other side), and the round starts again. Two down at once, or
 * one lying on the ground, stop the round. When a side has the points it
 * needs or the clock runs out, the match is recorded and the next one set
 * up. */
static void fight_UpdateMode(void) {
    ClockTime time;

    if (g_fight_mode == FIGHT_MODE_VSCPU) {
        g_disp_prim_cursor +=
            hud_DrawOpponentCount(g_vscpu_faced, (s32)g_disp_prim_cursor, 1) /
            4;
    } else if (g_fight_mode == FIGHT_MODE_CHANBARA) {
        /* the clock stops when either falls, but not for an opponent that
         * is not the last */
        if (!g_fighters[0].is_defeated &&
            (!g_fighters[1].is_defeated ||
             g_chanbara_count != CHANBARA_OPPONENTS)) {
            g_chanbara_time++;
            if (g_chanbara_time > CHANBARA_TIME_MAX) {
                g_chanbara_time = CHANBARA_TIME_MAX;
            }
        }
        fight_GetChanbaraTime(&time);
        g_disp_prim_cursor +=
            hud_DrawTimeAndCount(
                &time, g_chanbara_count, (s32)g_disp_prim_cursor, 1) /
            4;
    } else if (
        (g_fight_mode == FIGHT_MODE_PRACTICE && g_fight_practice_round == 1) ||
        g_fight_mode == FIGHT_MODE_VS) {
        g_disp_prim_cursor +=
            hud_DrawWinCounts(
                g_fight_wins_p1, g_fight_wins_p2, (s32)g_disp_prim_cursor, 1) /
            4;
    } else if (g_fight_mode == FIGHT_MODE_TEAM) {
        /* how long the result's calls take; then the frames left on the
         * clock */
        s32 frames;

        if (g_team_time_up_timer != 0) {
            if (g_team_time_up_timer == TEAM_TIME_UP_CALL) {
                snd_Play(SND_JUDGE_MATCH_OVER, SND_VOLUME_MAX, SND_VOLUME_MAX);
            }
            if (++g_team_time_up_timer == TEAM_TIME_UP_FRAMES) {
                g_team_time_up_timer = 0;
                fight_RecordTeamMatch();
                g_team_result_timer = 1;
            }
        } else if (g_team_break_timer != 0) {
            if (++g_team_break_timer == TEAM_BREAK_CALL) {
                snd_Play(SND_JUDGE_BREAK, SND_VOLUME_MAX, SND_VOLUME_MAX);
            } else if (g_team_break_timer == TEAM_BREAK_FRAMES) {
                g_team_break_timer = 0;
                g_fight_restart = 1;
                g_sys_screen = SCREEN_FIGHT_INIT;
            }
        } else if (g_team_out_timer != 0) {
            g_disp_prim_cursor +=
                hud_DrawCaption(
                    HUD_CAPTION_OUT_OF_BOUNDS, (s32)g_disp_prim_cursor, 1) /
                4;
            if (++g_team_out_timer == TEAM_OUT_FRAMES) {
                g_team_out_timer = 0;
                if (g_team_penalized != TEAM_NOBODY) {
                    snd_Play(SND_JUDGE_WARNING, SND_VOLUME_MAX, SND_VOLUME_MAX);
                    (&g_team_warnings)[g_team_penalized]++;
                    g_team_warning_timer = 1;
                } else {
                    g_fight_restart = 1;
                    g_sys_screen = SCREEN_FIGHT_INIT;
                }
            }
        } else if (g_team_warning_timer != 0) {
            if (++g_team_warning_timer == TEAM_WARNING_FRAMES) {
                g_team_warning_timer = 0;
                if ((&g_team_warnings)[g_team_penalized] ==
                    TEAM_WARNINGS_FOR_POINT) {
                    u8 *points = &g_team_points;

                    points[g_team_penalized == 0]++;
                    (&g_team_warnings)[g_team_penalized] = 0;
                    g_team_ippon_timer = TEAM_IPPON_FRAMES;
                } else {
                    g_fight_restart = 1;
                    g_sys_screen = SCREEN_FIGHT_INIT;
                }
            }
        } else if (g_team_ippon_timer != 0) {
            g_disp_prim_cursor +=
                hud_DrawCaption(HUD_CAPTION_IPPON, (s32)g_disp_prim_cursor, 1) /
                4;
            if (++g_team_ippon_timer == TEAM_IPPON_CALL) {
                snd_Play(SND_JUDGE_IPPON, SND_VOLUME_MAX, SND_VOLUME_MAX);
            }
            if (g_team_ippon_timer == TEAM_IPPON_FOULS_CALL) {
                snd_Play(SND_JUDGE_IPPON_BY_WARNINGS, SND_VOLUME_MAX,
                         SND_VOLUME_MAX);
            }
            if (g_team_ippon_timer == TEAM_IPPON_FRAMES ||
                g_team_ippon_timer == TEAM_IPPON_FOULS_FRAMES) {
                g_team_ippon_timer = 0;
                if ((&g_team_points)[g_team_penalized == 0] ==
                    g_team_points_to_win) {
                    fight_RecordTeamMatch();
                    g_team_result_timer = 1;
                } else {
                    g_fight_restart = 1;
                    g_sys_screen = SCREEN_FIGHT_INIT;
                }
            }
        } else if (g_team_begin_countdown != 0) {
            if (--g_team_begin_countdown == 0) {
                snd_Play(SND_JUDGE_BEGIN, SND_VOLUME_MAX, SND_VOLUME_MAX);
            }
        } else if (g_team_result_timer != 0) {
            if (g_team_results[g_team_match - 1] == TEAM_RESULT_DRAW) {
                if (g_team_result_timer == 1) {
                    snd_Play(SND_JUDGE_DRAW, SND_VOLUME_MAX, SND_VOLUME_MAX);
                }
                frames = TEAM_MATCH_DRAW_FRAMES;
            } else {
                if (g_team_result_timer == 1) {
                    snd_Play(SND_JUDGE_WINNER, SND_VOLUME_MAX, SND_VOLUME_MAX);
                }
                if (g_team_result_timer == TEAM_MATCH_CLAN_CALL) {
                    snd_Play(
                        g_fighter_clans
                                [g_fighters[g_team_results[g_team_match - 1]]
                                     .character]
                            ? SND_JUDGE_CLAN_2
                            : SND_JUDGE_CLAN_1,
                        SND_VOLUME_MAX, SND_VOLUME_MAX);
                }
                frames = TEAM_MATCH_WON_FRAMES;
            }
            if (++g_team_result_timer == frames) {
                g_team_result_timer = 0;
                fight_NextTeamMatch();
            }
        } else if (g_fighters[0].is_defeated && g_fighters[1].is_defeated) {
            g_team_break_timer = 1;
        } else if (g_fighters[1].is_defeated) {
            g_team_penalized = 1;
            g_team_ippon_timer = 1;
            (&g_team_points)[g_team_penalized ^ 1]++;
        } else if (g_fighters[0].is_defeated) {
            g_team_penalized = 0;
            g_team_ippon_timer = 1;
            (&g_team_points)[g_team_penalized ^ 1]++;
        } else if (g_fighters->move_kind == MOVE_KIND_DOWN ||
                   g_fighters[1].move_kind == MOVE_KIND_DOWN) {
            g_team_break_timer = TEAM_BREAK_DOWN;
        } else if (g_fighters[0].surface == SURFACE_FIRM_2) {
            u16 kind;

            snd_Play(SND_JUDGE_OUT, SND_VOLUME_MAX, SND_VOLUME_MAX);
            g_team_out_timer = 1;
            kind = g_fighters[0].move_kind;
            if (kind == MOVE_KIND_RUN || kind == MOVE_KIND_ARUN ||
                kind == MOVE_KIND_RUN_ST || kind == MOVE_KIND_WALK ||
                kind == MOVE_KIND_STEP || kind == MOVE_KIND_LJUMP) {
                g_team_penalized = 0;
            } else {
                g_team_penalized = TEAM_NOBODY;
            }
        } else if (g_fighters[1].surface == SURFACE_FIRM_2) {
            u16 kind;

            snd_Play(SND_JUDGE_OUT, SND_VOLUME_MAX, SND_VOLUME_MAX);
            g_team_out_timer = 1;
            kind = g_fighters[1].move_kind;
            if (kind == MOVE_KIND_RUN || kind == MOVE_KIND_ARUN ||
                kind == MOVE_KIND_RUN_ST || kind == MOVE_KIND_WALK ||
                kind == MOVE_KIND_STEP || kind == MOVE_KIND_LJUMP) {
                g_team_penalized = 1;
            } else {
                g_team_penalized = TEAM_NOBODY;
            }
        } else if (g_team_time_limit != 0) {
            g_team_time_frames++;
            if (g_team_time_frames >= g_team_time_limit * FRAMES_PER_SECOND) {
                snd_Play(SND_JUDGE_TIME_UP, SND_VOLUME_MAX, SND_VOLUME_MAX);
                g_team_time_up_timer = 1;
            }
        }
        if (g_team_time_limit != 0) {
            frames = g_team_time_limit * FRAMES_PER_SECOND - g_team_time_frames;
            time.time_seconds = frames / FRAMES_PER_SECOND;
            time.time_hundredths = frames % FRAMES_PER_SECOND *
                                   HUNDREDTHS_PER_SECOND / FRAMES_PER_SECOND;
        }
        /* four bits each: the first side's points, the second's, then the
         * warnings of each */
        g_disp_prim_cursor +=
            hud_DrawPointsAndTime(
                &time,
                g_team_points | (g_team_warnings << 8) |
                    (g_team_points_p2 << 4) | (g_team_warnings_p2 << 12),
                (s32)g_disp_prim_cursor, 1) /
            4;
    }
}

/* ---- setting a fight up ---- */

/* The stage that is in memory goes with the two fighters' classes: these
 * are the classes it was loaded for. */
static s8 g_fight_loaded_classes[2] = {-1, -1};

/* Loads the stage and the sound file that goes with it (the stage's bank
 * and the bank of each fighter's class), unless they are what is in memory
 * already. What is kept of the sound file is moved out of the staging area. */
void fight_LoadStage(void) {
    s32 load_addr = SYS_STAGING_ADDR;
    s32 kept_size;
    s32 *dest;

    disp_Reset();

    if (
#if RB_PORT
        /* another arena, or none, over the same host (port_stage_choice.h) */
        port_StageChoiceAtLoad(g_fight_mode) ||
#endif
        g_fight_stage != g_fight_loaded_stage ||
        g_fighter_roster_classes[g_fight_config.roster[0]] !=
            g_fight_loaded_classes[0] ||
        g_fighter_roster_classes[g_fight_config.roster[1]] !=
            g_fight_loaded_classes[1]) {
        motion_DropChanbaraPack();
        stage_Load(g_fight_stage, load_addr);
#if RB_PORT
        port_StageChoiceLoaded();
#endif
        fight_ResetObstacles();
        snd_CloseStageVabs();

        kept_size = snd_LoadStageVabs(
#if RB_PORT
            /* a mod's arena may be heard as another stage (port_arenas.h) */
            load_addr, port_ArenaSoundStage(g_fight_stage),
#else
            load_addr, g_fight_stage,
#endif
            g_fighter_roster_classes[g_fight_config.roster[0]],
            g_fighter_roster_classes[g_fight_config.roster[1]]);

        g_fight_loaded_stage = g_fight_stage;
        g_fight_loaded_classes[0] =
            g_fighter_roster_classes[g_fight_config.roster[0]];
        g_fight_loaded_classes[1] =
            g_fighter_roster_classes[g_fight_config.roster[1]];

        if (kept_size > SND_STAGE_VAB_ROOM) {
            sys_Panic();
        }

        dest = g_snd_stage_vab_buffer;
        memcpy((u8 *)dest, (u8 *)ADDR(load_addr), kept_size);
        snd_RebaseStageVabs((s32)dest - load_addr);
    }
}

/* ---- Sound: the fight's banks, music and reverb (see snd.h) ---- */

/* Team mode: loads the judge's bank and keeps its header in
 * g_snd_mode_vab_buffer. */
static void snd_InstallJudgeVab(void) {
    s32 load_addr = SYS_STAGING_ADDR;
    s32 kept_size;
    s32 *dest;

    disp_Reset();
    motion_DropChanbaraPack();
    snd_CloseJudgeVab();
    kept_size = snd_LoadJudgeVab(load_addr);
    if (kept_size > SND_JUDGE_VAB_ROOM) {
        sys_Panic();
    }
    dest = g_snd_mode_vab_buffer;
    memcpy((u8 *)dest, (u8 *)ADDR(load_addr), kept_size);
    snd_RebaseJudgeVab((s32)dest - load_addr);
}

/* Chanbara mode: the same for its two banks. */
static void snd_InstallChanbaraVabs(void) {
    s32 load_addr = SYS_STAGING_ADDR;
    s32 kept_size;
    s32 *dest;

    disp_Reset();
    motion_DropChanbaraPack();
    snd_CloseChanbaraVabs();
    kept_size = snd_LoadChanbaraVabs(load_addr);
    if (kept_size > SND_CHANBARA_VAB_ROOM) {
        sys_Panic();
    }
    dest = g_snd_mode_vab_buffer;
    memcpy((u8 *)dest, (u8 *)ADDR(load_addr), kept_size);
    snd_RebaseChanbaraVabs((s32)dest - load_addr);
}

/* Silences the fight and closes the banks only its mode uses. */
void snd_EndFight(void) {
    snd_Reset();
    snd_StopReverb();
    if (g_fight_mode == FIGHT_MODE_TEAM) {
        snd_CloseJudgeVab();
    }
    if (g_fight_mode == FIGHT_MODE_CHANBARA) {
        snd_CloseChanbaraVabs();
    }
}

/* Starts the fight's music, looping, in the modes that have it: the stage's
 * track in versus, and a track of their own in chanbara (by the player's
 * clan) and in the two kinds of practice. */
static void snd_StartFightMusic(void) {
#if RB_PORT
    s32 piece;
#endif

    snd_EnableCdAudio();
#if RB_PORT
    /* the music chosen on the stage pages, or the window's fight_music
     * (port_fight_music.h): the stage's piece in every mode, another
     * piece, none, or what the game itself starts */
    piece = port_FightMusicStart(g_fight_mode, g_fight_stage, snd_IsMusicOff());
    if (piece != PORT_FIGHT_MUSIC_GAME) {
        if (piece == PORT_FIGHT_MUSIC_STAFF_ROLL) {
            cdrom_StartAudio(
                cdrom_GetFileIndex(CDROM_GROUP_AUDIO, CDROM_AUDIO_BB2_M2),
                PORT_FIGHT_MUSIC_STAFF_ROLL_CHANNEL);
        } else if (piece != PORT_FIGHT_MUSIC_QUIET) {
            cdrom_StartMusic(piece);
        }
        cdrom_SetAudioLoop(1);
        return;
    }
#endif
    if (snd_IsMusicOff()) {
        return;
    }
    switch (g_fight_mode) {
    case FIGHT_MODE_STORY:
    case FIGHT_MODE_VSCPU:
    default:
        break;
    case FIGHT_MODE_VS:
#if RB_PORT
        /* a mod's arena's own track (port_arenas.h) */
        cdrom_StartMusic(port_ArenaMusic(
            g_fight_stage, g_snd_stage_music_tracks[g_fight_stage]));
#else
        cdrom_StartMusic(g_snd_stage_music_tracks[g_fight_stage]);
#endif
        break;
    case FIGHT_MODE_CHANBARA:
        if (g_fighter_clans[g_fighters[0].character] != 0) {
            cdrom_StartMusic(MUSIC_CHANBARA_CLAN_2);
        } else {
            cdrom_StartMusic(MUSIC_CHANBARA_CLAN_1);
        }
        break;
    case FIGHT_MODE_PRACTICE:
        if (g_fight_practice_round != 0) {
            cdrom_StartMusic(MUSIC_PRACTICE_ROUND);
        } else {
            cdrom_StartMusic(MUSIC_PRACTICE);
        }
        break;
    case FIGHT_MODE_TEAM:
        break;
    }
    cdrom_SetAudioLoop(1);
}

/* Ambient sounds play when there is no music over the fight: always in the
 * modes without music, and in practice, chanbara and versus when the music is
 * turned off. */
s32 snd_AmbienceEnabled(void) {
    s32 fight_mode = g_fight_mode;

#if RB_PORT
    /* a fight whose music the port chose: the ambient sounds where no
     * music plays, as versus has them with the music off */
    if (port_FightMusicAmbience() >= 0) {
        return port_FightMusicAmbience();
    }
#endif
    if (fight_mode >= FIGHT_MODE_TEAM) {
        return 1;
    }
    if (fight_mode >= FIGHT_MODE_PRACTICE) {
        return snd_IsMusicOff();
    }
    return 1;
}

/* Chanbara's opponents come as a pack of models (ndata.h), read into the
 * room of the second fighter's motion file. */
static void motion_LoadChanbaraPack(void) {
    ndata_LoadPack(
        g_fighter_clans[g_fighters[0].character] < 1, SYS_STAGING_ADDR);
    g_motion_loaded_sets[1] = MOTION_SET_PACK;
}

/* ---- the fight's screens ---- */

/* The story's stage card is the loading screen of the chapter's first
 * fight or cutscene: keeps it up until the whole route has been marked on
 * it and half a second more, then clears the screen. */
void story_FinishStageCard(void) {
    s32 i;

    if (g_disp_loading_screen != LOADING_SCREEN_STAGE_CARD) {
        return;
    }
#if RB_PORT
    /* the card's own time on the screen, which the window may hurry once
     * it has been up a while (docs/PORT.md, "Quick loading") */
    g_port_load_wait++;
#endif
    link_BeginWait();
    if (g_story_route_drawn <= g_story_chapter) {
        do {
            link_ExchangeIdle();
            disp_DrawLoadingScreen();
            VSync(2);
        } while (g_story_route_drawn <= g_story_chapter);
        for (i = 0; i < STAGE_CARD_HOLD_PASSES; i++) {
            link_ExchangeIdle();
            disp_DrawLoadingScreen();
            VSync(2);
        }
    }
    link_EndWait();
#if RB_PORT
    g_port_load_wait--;
#endif
    disp_Blank();
    disp_Show();
}

/* SCREEN_FIGHT_INIT: sets up the two fighters from g_fight_config and loads
 * what the fight needs: their models (the fighter looked through also as
 * the tiny model), their weapons and sub-weapons with the sounds they make,
 * the stage, the mode's own sounds and models, the motion files. A fight
 * that starts again (g_fight_restart) keeps what is loaded. Then the
 * fighters are put into their first move: their stance, or, at the start
 * of practice and of a team battle's match, their entrance. */
void fight_Init(void) {
    s32 i;
    s32 load_addr;

    snd_Reset();
    if (g_disp_loading_screen != LOADING_SCREEN_STAGE_CARD) {
        disp_Blank();
        disp_Show();
    }
    disp_DrawLoadingScreen();
    disp_Reset();
    stage_UnfogModel();
    object_ClearAll();
    object_ClearDirt();
    stage_SetFirstPerson(g_fight_first_person);
    SetGeomScreen(math_FovToScreenDist(
        g_fight_first_person ? CAMERA_FOV_FIRST_PERSON : CAMERA_FOV_SIDE));
    for (i = 0; i < 2; i++) {
        if (g_fight_mode != FIGHT_MODE_STORY) {
            model_RequestTextures(0, 0);
        }
        fighter_Init(i, g_fight_config.is_cpu[i], g_fight_config.roster[i],
                     g_fight_config.weapon[i], 0);
        if (g_fight_first_person && g_fight_viewer == i) {
            stage_FogModel(i == 0);
        }
    }
    cloth_RequestSnap(0);
    cloth_RequestSnap(1);
    if (!g_fight_restart) {
        resource_ClearWeaponRequests();
        load_addr = SYS_STAGING_ADDR;
        motion_DropChanbaraPack();
        for (i = 0; i < 2; i++) {
            if (g_fight_first_person && g_fight_viewer == i) {
                model_Load(i, g_fighter_roster_models[g_fight_config.roster[i]],
                           load_addr);
                model_LoadTiny(i);
            } else if (g_fight_mode == FIGHT_MODE_CHANBARA && i == 1) {
                model_Load(1, g_chanbara_model, 0);
            } else {
                model_Load(i, g_fighter_roster_models[g_fight_config.roster[i]],
                           load_addr);
            }
            resource_RequestWeapon(g_fighters[i].weapon_model);
            /* chanbara's opponents share one set of weapon sounds; the
             * weapons of practice and of a team battle have their own */
            if (g_fight_mode != FIGHT_MODE_CHANBARA || i != 1) {
                if ((g_fight_mode == FIGHT_MODE_PRACTICE &&
                     g_fight_practice_round == 0) ||
                    g_fight_mode == FIGHT_MODE_TEAM) {
                    resource_SetWeaponSounds(
                        i,
                        g_resource_weapon_sound_kinds_alt
                            [g_fighters[i].character][g_fighters[i].weapon_id]);
                } else {
                    resource_SetWeaponSounds(
                        i,
                        g_resource_weapon_sound_kinds[g_fighters[i].character]
                                                     [g_fighters[i].weapon_id]);
                }
            }
            if (g_fighters[i].sub_weapon_kind != OBJECT_NONE) {
                resource_RequestWeapon(
                    g_object_weapon_models[g_fighters[i].sub_weapon_kind]);
                if (g_fighters[i].sub_weapon_kind == OBJECT_FROG) {
                    resource_RequestWeapon(g_object_weapon_models[OBJECT_FROG] +
                                           WEAPON_MODEL_FROG_IN_AIR);
                }
            }
        }
        resource_LoadWeapons(load_addr);
        model_SetOutline(0, 0);
        model_SetOutline(1, 0);
        /* the opponents that come in colours */
        if (g_fight_mode == FIGHT_MODE_STORY && g_story_opponent_named == 0) {
            model_TintOpponent(g_story_henchman_tint.r, g_story_henchman_tint.g,
                               g_story_henchman_tint.b);
        } else if (g_fight_mode == FIGHT_MODE_CHANBARA) {
            model_TintOpponent(
                g_chanbara_tint.r, g_chanbara_tint.g, g_chanbara_tint.b);
        } else if (g_fight_mode == FIGHT_MODE_PRACTICE) {
            u8 *tint = g_fight_practice_tints
                [g_fighter_clans[g_fighters[0].character]];
            if (g_fight_practice_round == 0) {
                model_TintOpponent(tint[0], tint[1], tint[2]);
            }
        }
        fight_LoadStage();
        if (g_fight_mode == FIGHT_MODE_TEAM) {
            snd_InstallJudgeVab();
        }
        if (g_fight_mode == FIGHT_MODE_CHANBARA) {
            snd_InstallChanbaraVabs();
            motion_LoadChanbaraPack();
        }
        motion_LoadFighterFiles(
            g_fighter_roster_characters[g_fight_config.roster[0]],
            g_fight_config.weapon[0],
            g_fighter_roster_characters[g_fight_config.roster[1]],
            g_fight_config.weapon[1]);
    } else if (g_fight_mode == FIGHT_MODE_TEAM) {
        /* a round that starts again is called at once */
        g_team_begin_countdown = 1;
    }
    motion_OpenStreams();
    motion_AttachFighter(0);
    motion_AttachFighter(1);
    cpu_InitFighters();
    if (g_fight_mode == FIGHT_MODE_PRACTICE ||
        g_fight_mode == FIGHT_MODE_TEAM) {
        if (g_fight_restart) {
            motion_ResetStance(0);
            {
                MoveScript *move = motion_GetDefaultStanceMove(0);
                g_fighters[0].move_is_common = 0;
                fighter_StartMove(0, move, 0);
            }
            if (g_fight_mode == FIGHT_MODE_PRACTICE &&
                g_fight_practice_round == 0) {
                /* the partner stays in the stance it was in */
                MoveScript *move = motion_GetStanceMove(1);
                g_fighters[1].move_is_common = 0;
                fighter_StartMove(1, move, 0);
            } else {
                MoveScript *move;
                motion_ResetStance(1);
                move = motion_GetDefaultStanceMove(1);
                g_fighters[1].move_is_common = 0;
                fighter_StartMove(1, move, 0);
            }
        } else {
            motion_ResetStance(0);
            motion_ResetStance(1);
            {
                MoveScript *move = motion_GetEntranceMove(0);
                g_fighters[0].move_is_common = 1;
                fighter_StartMove(0, move, 1);
            }
            {
                MoveScript *move = motion_GetEntranceMove(1);
                g_fighters[1].move_is_common = 1;
                fighter_StartMove(1, move, 1);
            }
        }
    } else {
        motion_ResetStance(0);
        motion_ResetStance(1);
        {
            MoveScript *move = motion_GetDefaultStanceMove(0);
            g_fighters[0].move_is_common = 0;
            fighter_StartMove(0, move, 0);
        }
        {
            MoveScript *move = motion_GetDefaultStanceMove(1);
            g_fighters[1].move_is_common = 0;
            fighter_StartMove(1, move, 0);
        }
    }
    g_fight_defeat_frames = 0;
    g_fight_winner = FIGHT_NOBODY;
    camera_InitSideView(&g_fighters[0], &g_fighters[1]);
    if (g_fight_first_person) {
        camera_InitFirstPerson(&g_fighters[0], &g_fighters[1]);
    }
    replay_Reset();
    eff_Stop();
    story_FinishStageCard();
    g_disp_loading_screen = LOADING_SCREEN_NONE;
    g_disp_loading_noise_burst = 0;
    eff_Init();
    g_fight_restart = 0;
    g_sys_screen = SCREEN_FIGHT_UPDATE;
    story_HenchmanShout();
    snd_StartFightMusic();
    snd_ResetStageSounds();
#if RB_PORT
    snd_SetStageReverb(port_ArenaSoundStage(g_fight_stage));
#else
    snd_SetStageReverb(g_fight_stage);
#endif
    if (g_fight_mode == FIGHT_MODE_NETWORK) {
        rng_SetSeed(g_fight_rng_seed);
    }
}

/* ---- the camera (camera.c, camera.h): what chooses the frame's view and
 * begins the scene from it ---- */

/* Begins a frame of the fight's scene. With the first-person setting on the
 * view is the first-person one, except while the two fighters are locked
 * together in one move, which is shown from the side; if a fighter is
 * defeated in that move the side view stays. The fighter looked through is
 * drawn as outlines, and the first-person view has a wider lens. In the
 * fight itself the player's zoom is added. */
static void camera_BeginFightScene(void) {
    s32 unused[2];    /* nothing uses it */
    CameraView local; /* the view with the player's zoom added */
    CameraView *view;

    if (g_fight_first_person) {
        if (g_camera_mode == CAMERA_SIDE) {
            if (g_fighters[0].is_defeated || g_fighters[1].is_defeated) {
                g_camera_mode = CAMERA_SIDE_HELD;
            }
        }
        if (g_camera_mode != CAMERA_SIDE_HELD) {
            if (g_fighters[0].move_kind == MOVE_KIND_PAIRED ||
                g_fighters[1].move_kind == MOVE_KIND_PAIRED) {
                view = &g_camera_main;
                g_camera_mode = CAMERA_SIDE;
            } else {
                view = &g_camera_second;
                g_camera_mode = CAMERA_FIRST_PERSON;
            }
        } else {
            view = &g_camera_main;
        }
        stage_SetFirstPerson(g_camera_mode == CAMERA_FIRST_PERSON);

        SetGeomScreen(math_FovToScreenDist(
            g_camera_mode == CAMERA_FIRST_PERSON ? CAMERA_FOV_FIRST_PERSON
                                                 : CAMERA_FOV_SIDE));

        if (g_camera_mode == CAMERA_FIRST_PERSON) {
            model_SetOutline(g_fight_viewer, 1);
            model_SetOutline(g_fight_viewer == 0, 0);
        } else {
            model_SetOutline(0, 0);
            model_SetOutline(1, 0);
        }
    } else {
        view = &g_camera_main;
    }

    if (g_sys_screen == SCREEN_FIGHT_UPDATE) {
        local.focus.x = view->focus.x + g_camera_offset.focus.x;
        local.focus.y = view->focus.y + g_camera_offset.focus.y;
        local.focus.z = view->focus.z + g_camera_offset.focus.z;
        local.angles.vx = view->angles.vx + g_camera_offset.angles.vx;
        local.angles.vy = view->angles.vy + g_camera_offset.angles.vy;
        local.angles.vz = view->angles.vz + g_camera_offset.angles.vz;
        local.eye_distance = view->eye_distance + g_camera_offset.eye_distance;
    } else {
        local = *view;
    }

    scene_Begin(&local.focus, &local.angles, local.eye_distance);
    camera_CalcEye(&local, &view->eye.x);
    eff_Update(&local.angles.vx, &view->eye.x);
    stage_OpenCellFully(view->open_cells[0]);
    stage_OpenCellFully(view->open_cells[1]);
    g_camera_current = view;
}

/* Begins a frame of the replay's scene: from the second view for the middle
 * third of the replay, from the main one before and after. */
void camera_BeginReplayScene(s32 progress) {
    s32 unused[2];    /* nothing uses it */
    CameraView local; /* the view with the player's zoom added */
    CameraView *view;

    view = &g_camera_second;
    if (progress < ONE / 3 || progress > 2 * ONE / 3) {
        view = &g_camera_main;
    }

    local.focus.x = view->focus.x + g_camera_offset.focus.x;
    local.focus.y = view->focus.y + g_camera_offset.focus.y;
    local.focus.z = view->focus.z + g_camera_offset.focus.z;
    local.angles.vx = view->angles.vx + g_camera_offset.angles.vx;
    local.angles.vy = view->angles.vy + g_camera_offset.angles.vy;
    local.angles.vz = view->angles.vz + g_camera_offset.angles.vz;
    local.eye_distance = view->eye_distance + g_camera_offset.eye_distance;

    scene_Begin(&local.focus, &local.angles, local.eye_distance);
    camera_CalcEye(&local, &view->eye.x);
    eff_Update(&local.angles.vx, &view->eye.x);
    g_camera_current = view;
}

/* In the first-person view the fighter looked through is shown as the tiny
 * model, turned by how far the view lags behind a sharp turn, with its
 * weapon if it is holding it. */
static void camera_QueueTinyModel(void) {
    s32 viewer_index = g_fight_viewer;
    Fighter *viewer = &g_fighters[viewer_index];
    s32 weapon_id;
    if (viewer->frame_flags & FIGHTER_DRAW_WEAPON) {
        weapon_id = viewer->weapon_id;
    } else {
        weapon_id = -1;
    }
    {
        u32 sub_weapon_out = viewer->frame_flags & FIGHTER_DRAW_SUB_WEAPON;
        model_QueueTiny(
            g_camera_second.turn_lag, weapon_id, sub_weapon_out > 0);
    }
}

/* ---- the fight's screens, continued ---- */

/* SCREEN_FIGHT_UPDATE: a frame of the fight. The camera follows the
 * fighters, each fighter is played from its pad, the two are tested against
 * each other, and the mode has its say. */
void fight_Update(void) {
    PadState pad;

    g_fight_squashed_view = scene_GetSquashedView();
    camera_UpdateSideView(
        &g_fighters[0].camera_target, &g_fighters[1].camera_target,
        &g_fighters[0], &g_fighters[1]);
    if (g_fight_first_person) {
        camera_UpdateFirstPerson(&g_fighters[g_fight_viewer]);
    }
    camera_BeginFightScene();
    replay_BeginFrame();
    fight_UpdateDistance();
    fighter_ReadPad(0, &pad);
    fighter_Update(0, &pad);
    fighter_ReadPad(1, &pad);
    fighter_Update(1, &pad);
    fight_UpdateFighterPair();
    object_Update();
    object_UpdateDirt();
    replay_AdvanceFrame();
    if (g_fight_first_person && g_camera_mode == CAMERA_FIRST_PERSON) {
        camera_QueueTinyModel();
    } else {
        stage_OpenCellsTo(g_fighters[0].world_pos.x, g_fighters[0].world_pos.z);
        stage_OpenCellsTo(g_fighters[1].world_pos.x, g_fighters[1].world_pos.z);
    }
    /* the debug view leaves the stage out */
    scene_End((g_fight_debug_view ^ 1) != 0);
    fight_UpdateMode();
    snd_UpdateStageSounds();
    fight_UpdateDefeat();
}

/* SCREEN_AFTER_FIGHT_INIT. The cursor starts on "again", or under it when a
 * rematch is not on offer. */
void fight_AfterFightInit(void) {
    u8 rematch_offered;
    model_SetOutline(0, 0);
    model_SetOutline(1, 0);
    eff_Stop();
    rematch_offered = g_fight_rematch_offered;
    g_fighters[1].blending = 0;
    g_fighters[0].blending = 0;
    g_sys_screen_frames = 0;
    g_fight_choice_timer = 0;
    g_sys_screen = SCREEN_AFTER_FIGHT_UPDATE;
    g_fight_first_choice = rematch_offered < 1;
    g_fight_choice = rematch_offered < 1;
}

/* SCREEN_AFTER_FIGHT_UPDATE: the fight runs on with no input, the camera
 * circling the fighter that lost, under the choice of again, character
 * select or main menu. A choice takes effect two seconds after it is made;
 * "again" starts the fight where the fighters are, and in chanbara costs
 * half a minute on the clock. */
void fight_AfterFightUpdate(void) {
    PadState pad;
    ClockTime time;

    g_sys_screen_frames++;
    g_fight_squashed_view = scene_GetSquashedView();
    /* round the loser, a turn in 512 frames */
    camera_LookAtFighter(
        &g_fighters[g_fight_winner == 0], g_sys_screen_frames << 3);
    camera_BeginFightScene();
    replay_BeginFrame();
    fight_UpdateDistance();
    pad_ClearState(&pad);
    fighter_Update(0, &pad);
    fighter_Update(1, &pad);
    fight_UpdateFighterPair();
    object_Update();
    object_UpdateDirt();
    replay_AdvanceFrame();
    scene_End(1);
    snd_UpdateStageSounds();
    if (g_fight_mode == FIGHT_MODE_CHANBARA) {
        fight_GetChanbaraTime(&time);
        g_disp_prim_cursor +=
            hud_DrawTimeAndCount(
                &time, g_chanbara_count, (s32)g_disp_prim_cursor, 1) /
            4;
    }
    if (g_fight_choice_timer == 0) {
        g_disp_prim_cursor +=
            hud_DrawPanel(HUD_PANEL_AFTER_FIGHT, g_fight_choice,
                          (s32)g_disp_prim_cursor, 0) /
            4;
        if ((g_pad_state.pressed & PAD_ANY(PADLup))) {
            snd_Play(MENU_SOUND_CURSOR, SND_VOLUME_MAX, SND_VOLUME_MAX);
            if (g_fight_choice != g_fight_first_choice) {
                g_fight_choice--;
            } else {
                g_fight_choice = FIGHT_CHOICE_TOP_MENU;
            }
        } else if ((g_pad_state.pressed & PAD_ANY(PADLdown))) {
            snd_Play(MENU_SOUND_CURSOR, SND_VOLUME_MAX, SND_VOLUME_MAX);
            if (g_fight_choice == FIGHT_CHOICE_TOP_MENU) {
                g_fight_choice = g_fight_first_choice;
            } else {
                g_fight_choice++;
            }
        }
        if ((g_pad_state.pressed & PAD_ANY(PADRdown))) {
            snd_Play(MENU_SOUND_CONFIRM, SND_VOLUME_MAX, SND_VOLUME_MAX);
            /* the other two are acted on at once */
            g_fight_choice_timer = (g_fight_choice == FIGHT_CHOICE_REMATCH)
                                       ? 1
                                       : FIGHT_CHOICE_FRAMES;
            if (g_fight_choice != FIGHT_CHOICE_REMATCH) {
                return;
            }
            g_fighters[g_fight_winner == 0].face = MODEL_FACE_NORMAL;
            if (g_fight_mode == FIGHT_MODE_CHANBARA) {
                g_chanbara_time += CHANBARA_CONTINUE_PENALTY;
                if (g_chanbara_time > CHANBARA_TIME_MAX) {
                    g_chanbara_time = CHANBARA_TIME_MAX;
                }
            }
        }
        return;
    }
    if (g_fight_choice == FIGHT_CHOICE_REMATCH) {
        g_disp_prim_cursor +=
            hud_DrawCaption(HUD_CAPTION_REMATCH, (s32)g_disp_prim_cursor, 1) /
            4;
    }
    g_fight_choice_timer++;
    if (g_fight_choice_timer < FIGHT_CHOICE_FRAMES) {
        return;
    }
    if (g_fight_choice == FIGHT_CHOICE_REMATCH) {
        g_fight_restart = 1;
        g_fight_continues++;
        g_fight_restart_pair =
            fight_FindNearestStartPair(&g_fighters[g_fight_winner].world_pos);
        if (g_fighters[1].is_cpu) {
            cpu_FadeAttacks(1);
        }
        g_sys_screen = SCREEN_FIGHT_INIT;
        return;
    }
    if (g_fight_choice == FIGHT_CHOICE_SUB_MENU) {
        cdrom_StopAndWait();
        snd_EndFight();
        g_menu_reopen_mode = 1;
        g_sys_screen = SCREEN_MENU_INIT;
        return;
    }
    if (g_fight_choice == FIGHT_CHOICE_TOP_MENU) {
        cdrom_StopAndWait();
        snd_EndFight();
        g_sys_screen = SCREEN_MENU_INIT;
    }
}

/* How long the winner is shown: three and a half seconds. */
#define WINNER_VIEW_FRAMES 105

/* SCREEN_WINNER_VIEW_INIT: puts the winner into its win pose, unless it is
 * in the air or down itself. */
void fight_WinnerViewInit(void) {
    s8 winner_index = g_fight_winner;
    Fighter *winner = &g_fighters[winner_index];
    u16 kind = winner->move_kind;

    if (kind != MOVE_KIND_DROP && !winner->airborne &&
        kind != MOVE_KIND_MJUMP && kind != MOVE_KIND_LJUMP &&
        !winner->is_defeated) {
        motion_ResetStance(g_fight_winner);
        {
            MoveScript *win_move =
                motion_GetWinMove(g_fight_winner, winner->character);
            s32 index = g_fight_winner;
            winner->move_is_common = 1;
            fighter_StartMove(index, win_move, 1);
        }
        winner->off_arm_ok = 1;
    }

    eff_Stop();
    g_sys_screen_frames = 0;
    g_sys_screen = SCREEN_WINNER_VIEW_UPDATE;
}

/* SCREEN_WINNER_VIEW_UPDATE: the fight runs on with no input for three and a
 * half seconds (or until cross is pressed) while the camera swings round to
 * the winner; then the mode decides what follows: the menu after a fight,
 * or, for a win against the CPU, the next opponent. */
void fight_WinnerViewUpdate(void) {
    PadState pad;
    s16 next_screen;

    g_sys_screen_frames++;
    g_fight_squashed_view = scene_GetSquashedView();
    /* how far through the view is, of ONE */
    camera_SwingToFighter(&g_fighters[g_fight_winner],
                          (g_sys_screen_frames << 12) / WINNER_VIEW_FRAMES);
    camera_BeginFightScene();
    replay_BeginFrame();
    fight_UpdateDistance();
    pad_ClearState(&pad);
    fighter_Update(0, &pad);
    fighter_Update(1, &pad);
    fight_UpdateFighterPair();
    object_Update();
    object_UpdateDirt();
    replay_AdvanceFrame();
    scene_End(1);
    snd_UpdateStageSounds();

    /* a winner that has since fallen too is not shown */
    if (g_fighters[g_fight_winner].is_defeated &&
        g_fight_mode == FIGHT_MODE_VSCPU) {
        g_sys_screen_frames = WINNER_VIEW_FRAMES;
    }

    if (g_sys_screen_frames >= WINNER_VIEW_FRAMES ||
        (g_pad_state.pressed & PAD_ANY(PADRdown))) {
        switch (g_fight_mode) {
        case FIGHT_MODE_VS:
            next_screen = SCREEN_AFTER_FIGHT_INIT;
            break;
        case FIGHT_MODE_VSCPU:
            if (g_fight_winner == 0) {
#if RB_PORT
                /* the stage's piece the port's fight_music plays here,
                 * where the game plays none, ends with the fight, as
                 * versus ends its own before what follows is loaded
                 * (port_fight_music.h) */
                cdrom_StopAndWait();
#endif
                snd_EndFight();
                g_disp_loading_screen = LOADING_SCREEN_NEXT_OPPONENT;
                fight_NextVsCpuOpponent();
                return;
            }
            next_screen = SCREEN_AFTER_FIGHT_INIT;
            break;
        case FIGHT_MODE_NETWORK:
            next_screen = SCREEN_AFTER_FIGHT_INIT;
            break;
        default:
            snd_EndFight();
            next_screen = SCREEN_DEBUG_MENU_INIT;
            break;
        }
        g_sys_screen = next_screen;
    }
}
