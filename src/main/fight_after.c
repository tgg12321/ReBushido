/* The screens that follow a fight or a mode's run (fight.h): the staff
 * roll and the opening movie, the endings, a team battle's result,
 * chanbara's best times, the menu after a match, the count of opponents
 * beaten, and the kaishaku. */
#define INCLUDE_ASM_USE_MACRO_INC 1
#include "common.h"
#include "include_asm.h"
#include <psxsdk/inline_o.h>
#include <psxsdk/gtemac.h>
#include "bb2.h"

/* Defined further down. */
static u8 g_ending_cutscenes[ENDING_COUNT];

/* The staff roll's music: an audio file and the channel of it. */
#define STAFF_MUSIC_FILE CDROM_AUDIO_BB2_M2
#define STAFF_MUSIC_CHANNEL 4

/* How long the screens that only show something stay up, in frames. */
#define RESULT_SCREEN_FRAMES 240
#define KILL_COUNT_FRAMES 150

/* The opening movie: its file among the movies, its last frame, and the
 * frame from which the title picture is shown over it. */
#define OPENING_MOVIE_FILE CDROM_MOVIE_OPENING
#define OPENING_MOVIE_END_FRAME 3320
#define OPENING_MOVIE_STILL_FRAME 2817

/* The team battle's result: the frames at which the judge names the
 * winning side (or calls the draw) and then says the battle is won. */
#define TEAM_BATTLE_CALL_FRAME 30
#define TEAM_BATTLE_WON_FRAME 67

/* The slowly turning view of the stage that the menu after a match and the
 * count of opponents beaten are shown over: from this far, at a point this
 * high above the stage's middle, tilted a little, a turn in 1024 frames. */
#define TURNING_VIEW_HEIGHT 3000
#define TURNING_VIEW_DISTANCE 10000
#define TURNING_VIEW_PITCH 0x20
#define TURNING_VIEW_SPEED_SHIFT 2

/* The kaishaku is seen through a lens of this many degrees. */
#define KAISHAKU_FOV 45

/* ---- the staff roll (staff.h) and the movie ---- */

/* SCREEN_STAFF_ROLL_INIT: drops the fight's data, loads the staff roll and
 * starts its music. */
void staff_Init(void) {
    disp_Reset();
    disp_Blank();
    motion_ForgetFiles();
    model_Free(0);
    model_Free(1);
    eff_ClearInitFlag();
    snd_CloseAllVabs();
    staff_LoadRoll(SYS_LOAD_ADDR);
    snd_EnableCdAudio();
    {
        s32 file_index =
            cdrom_GetFileIndex(CDROM_GROUP_AUDIO, STAFF_MUSIC_FILE);
        cdrom_StartAudio(file_index, STAFF_MUSIC_CHANNEL);
    }
    cdrom_WaitAudioStarted();
    g_sys_screen = SCREEN_STAFF_ROLL_UPDATE;
    disp_Show();
}

/* SCREEN_STAFF_ROLL_UPDATE: runs the staff roll to its end or until cross is
 * pressed. Then the characters the story won as guests are added to the
 * save data, with the fact that this character's story has been finished
 * (kept for the first three characters of each clan), and saved if any of
 * it is new. */
void staff_Update(void) {
    s32 changed = 0;
    s32 over;
    s32 old; /* the characters unlocked so far */
    s32 bit;

    over = staff_UpdateRoll() != 0;
    if (g_pad_state.pressed & PADRdown) {
        over = 1;
    }
    if (over) {
        cdrom_StopAndWait();
        old = g_saved_record.unlock_mask;
        if ((old | g_story_unlocks) != old) {
            g_saved_record.unlock_mask = old | g_story_unlocks;
            changed = 1;
        }
        /* the three characters each clan starts the game with */
        if (g_fighters[0].character == CHARACTER_KANNUKI ||
            g_fighters[0].character == CHARACTER_TATSUMI ||
            g_fighters[0].character == CHARACTER_MIKADO ||
            g_fighters[0].character == CHARACTER_GENGORO ||
            g_fighters[0].character == CHARACTER_KAUN ||
            g_fighters[0].character == CHARACTER_JO) {
            bit = g_fighters[0].character;
            /* bits 0 to 2 for the first clan's, 3 to 5 for the second's */
            if (bit >= CHARACTER_GENGORO) {
                bit -= CHARACTER_GENGORO - 3;
            }
            if (!((g_saved_record.staff_roll_character_mask >> bit) & 1)) {
                g_saved_record.staff_roll_character_mask |= 1 << bit;
                changed = 1;
            }
        }
        if (changed) {
            g_sys_screen = SCREEN_SAVE_INIT;
        } else {
            g_sys_screen = SCREEN_MENU_INIT;
        }
    }
}

/* SCREEN_MOVIE: plays the opening movie and the title still, starts the
 * engine again and goes to the main menu. */
void sys_MovieScreen(void) {
    s32 arena_base;
    disp_Reset();
    motion_ForgetFiles();
    model_Free(0);
    model_Free(1);
    eff_ClearInitFlag();
    arena_base = (s32)arena_GetBase();
    sys_ExecMovieOverlay(arena_base, SYS_MOVIE_WORK_ADDR, OPENING_MOVIE_FILE,
                         OPENING_MOVIE_END_FRAME, OPENING_MOVIE_STILL_FRAME);
    sys_InitEngineState();
    g_sys_screen = SCREEN_MENU_INIT;
}

/* ---- the endings ---- */

/* SCREEN_ENDING_INIT: sets up ending g_fight_ending: its picture, the
 * fighter it shows, its cutscene and its voice. The endings for a
 * character that chanbara has won show that character; a team battle's
 * shows the winning side's fighter of the last match; the others the
 * player's. */
void cutscene_EndingInit(void) {
    s32 picture;
    s8 *voice;
    disp_Blank();
    disp_Reset();
    motion_ForgetFiles();
    if (g_fight_ending == ENDING_TEAM || g_fight_ending == ENDING_TEAM + 1) {
        eff_ClearInitFlag();
    }
    if (g_fight_ending == ENDING_CHANBARA_FAST) {
        g_fight_config.roster[0] = ROSTER_TSUBAME;
        g_fight_config.weapon[0] = WEAPON_ID_LONG_GUN;
        fight_LoadFighter(0);
        fight_LoadWeaponOf(0);
    } else if (g_fight_ending == ENDING_CHANBARA_FAST + 1) {
        g_fight_config.roster[0] = ROSTER_KATZE;
        g_fight_config.weapon[0] = WEAPON_ID_SHORT_GUN;
        fight_LoadFighter(0);
        fight_LoadWeaponOf(0);
    } else if (g_fight_ending == ENDING_CHANBARA_CLEAN) {
        g_fight_config.roster[0] = ROSTER_LAST_1;
        g_fight_config.weapon[0] = WEAPON_ID_KATANA;
        fight_LoadFighter(0);
        fight_LoadWeaponOf(0);
    } else if (g_fight_ending == ENDING_CHANBARA_CLEAN + 1) {
        g_fight_config.roster[0] = ROSTER_LAST_2;
        g_fight_config.weapon[0] = WEAPON_ID_KATANA;
        fight_LoadFighter(0);
        fight_LoadWeaponOf(0);
    }
    /* the last two endings share a picture */
    if (g_fight_ending != ENDING_CHANBARA_CLEAN + 1) {
        picture = g_fight_ending;
    } else {
        picture = ENDING_CHANBARA_CLEAN;
    }
    hud_LoadEndingPicture(picture, (u8 *)SYS_LOAD_ADDR);
    if ((g_fight_ending == ENDING_TEAM &&
         g_fighter_clans[g_fighters[0].character] != 0) ||
        (g_fight_ending == ENDING_TEAM + 1 &&
         g_fighter_clans[g_fighters[0].character] == 0)) {
        /* the second side won */
        g_fight_config.roster[0] = g_fight_config.roster[1];
        g_fight_config.weapon[0] = g_fight_config.weapon[1];
        fight_LoadFighter(0);
        fight_LoadWeaponOf(0);
    }
    cutscene_StartFromSet(
        CUTSCENE_SET_SHARED, g_ending_cutscenes[g_fight_ending], 0,
        g_fighters[0].weapon_model, -1, -1, -1, SYS_LOAD_ADDR);
    model_SetOutline(0, 0);
    model_SetOutline(1, 0);
    eff_Stop();
    voice = g_ending_voices[g_fight_ending];
    if (voice[0] >= 0) {
        snd_EnableCdAudio();
        cdrom_StartAudio(
            cdrom_GetFileIndex(CDROM_GROUP_AUDIO, voice[0]), (u8)voice[1]);
        cdrom_WaitAudioStarted();
    }
    g_sys_screen_frames = 0;
    g_sys_screen = SCREEN_ENDING_UPDATE;
    disp_Show();
}

/* SCREEN_ENDING_UPDATE: plays the ending while its picture wipes in, to its
 * end or until cross is pressed. Then: after the story's, the staff roll;
 * after a team battle's, the menu that follows a match; after a character
 * won in chanbara, the save screen, but first the ending of the other
 * character if the run won both. */
void cutscene_EndingUpdate(void) {
    g_sys_screen_frames++;
    /* the size is in bytes and the cursor counts words: it moves on four
     * times as far as the picture takes, where the other screens divide by
     * 4 (the original's behaviour) */
#if RB_FIX_PRIM_CURSOR
    if (g_rb_settings.bugfixes) {
        g_disp_prim_cursor +=
            hud_DrawEndingPicture((s32)g_disp_prim_cursor, 1) / 4;
    } else
#endif
    {
        g_disp_prim_cursor += hud_DrawEndingPicture((s32)g_disp_prim_cursor, 1);
    }
    if (cutscene_RunFrame() == 0 || (g_pad_state.pressed & PAD_ANY(PADRdown))) {
        if ((g_fight_ending == ENDING_CHANBARA_FAST ||
             g_fight_ending == ENDING_CHANBARA_FAST + 1) &&
            g_chanbara_clean_unlock) {
            g_fight_ending = g_fighter_clans[g_fighters[0].character] != 0
                                 ? ENDING_CHANBARA_CLEAN + 1
                                 : ENDING_CHANBARA_CLEAN;
            g_sys_screen = SCREEN_ENDING_INIT;
        } else {
            if ((g_fight_ending == ENDING_TEAM ||
                 g_fight_ending == ENDING_TEAM + 1)) {
                g_sys_screen = SCREEN_AFTER_MATCH_INIT;
            } else if ((g_fight_ending == ENDING_CHANBARA_FAST ||
                        g_fight_ending == ENDING_CHANBARA_FAST + 1)) {
                g_sys_screen = SCREEN_SAVE_INIT;
            } else if ((g_fight_ending == ENDING_CHANBARA_CLEAN ||
                        g_fight_ending == ENDING_CHANBARA_CLEAN + 1)) {
                g_sys_screen = SCREEN_SAVE_INIT;
            } else if (g_fight_ending == ENDING_STORY ||
                       g_fight_ending == ENDING_STORY + 1) {
                g_sys_screen = SCREEN_STAFF_ROLL_INIT;
            } else {
                g_sys_screen = SCREEN_MENU_INIT;
            }
        }
    }
    if (g_sys_screen != SCREEN_ENDING_UPDATE) {
        cdrom_StopAndWait();
        if (g_sys_screen != SCREEN_ENDING_INIT) {
            cutscene_End();
        }
    }
}

/* ---- the team battle's result ---- */

/* SCREEN_TEAM_RESULT_INIT: works out which team won the battle: by matches
 * won, then by points; otherwise it is a draw. */
void fight_TeamResultInit(void) {
    s32 counts[3];
    s32 i;
    s32 side;

    counts[TEAM_RESULT_DRAW] = 0;
    counts[TEAM_RESULT_SIDE_2] = 0;
    counts[TEAM_RESULT_SIDE_1] = 0;
    for (i = 0; i < g_team_match_count; i++) {
        counts[g_team_results[i]]++;
    }
    if (counts[0] != counts[1]) {
        g_team_winner = counts[0] < counts[1];
    } else {
        counts[1] = 0;
        counts[0] = 0;
        for (i = 0; i < g_team_match_count; i++) {
            for (side = 0; side < 2; side++) {
                counts[side] += g_team_scores[i][side];
            }
        }
        if (counts[0] != counts[1]) {
            g_team_winner = counts[0] < counts[1];
        } else {
            g_team_winner = TEAM_RESULT_DRAW;
        }
    }
    disp_SetBackground(1, 0, 0, 0);
    g_sys_screen_frames = 0;
    g_sys_screen = SCREEN_TEAM_RESULT_UPDATE;
}

/* SCREEN_TEAM_RESULT_UPDATE: shows the table of matches and plays the
 * judge's call, for eight seconds or until cross is pressed; then the
 * winning side's ending, or after a draw the menu that follows a match. */
void fight_TeamResultUpdate(void) {
    g_sys_screen_frames++;
    if (g_team_winner == TEAM_RESULT_DRAW) {
        if (g_sys_screen_frames == TEAM_BATTLE_CALL_FRAME) {
            snd_Play(SND_JUDGE_DRAW, SND_VOLUME_MAX, SND_VOLUME_MAX);
        }
    } else {
        if (g_sys_screen_frames == TEAM_BATTLE_CALL_FRAME) {
            snd_Play(g_fighter_clans[g_fighters[g_team_winner].character] != 0
                         ? SND_JUDGE_CLAN_2
                         : SND_JUDGE_CLAN_1,
                     SND_VOLUME_MAX, SND_VOLUME_MAX);
        }
        if (g_sys_screen_frames == TEAM_BATTLE_WON_FRAME) {
            snd_Play(SND_JUDGE_BATTLE_WON, SND_VOLUME_MAX, SND_VOLUME_MAX);
        }
    }
    g_disp_prim_cursor +=
        hud_DrawTeamScores(g_team_packed_scores, (s32)g_disp_prim_cursor, 1) /
        4;
    if ((g_pad_state.pressed & PAD_ANY(PADRdown)) ||
        g_sys_screen_frames > RESULT_SCREEN_FRAMES) {
        if (g_team_winner == TEAM_RESULT_DRAW) {
            g_sys_screen = SCREEN_AFTER_MATCH_INIT;
        } else {
            cdrom_StopAndWait();
            g_fight_ending =
                g_fighter_clans[g_fighters[g_team_winner].character] != 0
                    ? ENDING_TEAM + 1
                    : ENDING_TEAM;
            g_sys_screen = SCREEN_ENDING_INIT;
        }
    }
}

/* ---- chanbara's best times ---- */

/* SCREEN_BEST_TIMES_INIT: puts the three best times and this run's time into
 * the menus' settings, where the table is drawn from. */
void fight_BestTimesInit(void) {
    ClockTime run_time;
    GameSettings *settings;
    s32 i;
    TimeRec *best;

    settings = menu_GetSettings();
    cdrom_StopAndWait();
    disp_Blank();
    hud_ResetBestTimes();
    for (i = 0; i < SAVE_TIME_COUNT; i++) {
        best = &g_saved_record.times[i];
        settings->time_records[i].minutes =
            best->time_frames / FRAMES_PER_MINUTE;
        settings->time_records[i].seconds =
            best->time_frames / FRAMES_PER_SECOND % SECONDS_PER_MINUTE;
        settings->time_records[i].hundredths =
            best->time_frames % FRAMES_PER_SECOND * HUNDREDTHS_PER_SECOND /
            FRAMES_PER_SECOND;
        settings->time_records[i].character_index =
            best->holder_character_index;
    }
    fight_GetChanbaraTime(&run_time);
    settings->fight_time[0] = run_time.time_minutes;
    settings->fight_time[1] = run_time.time_seconds;
    settings->fight_time[2] = run_time.time_hundredths;
    settings->fight_character_index = g_fighters[0].character;
    disp_SetBackground(1, 0, 0, 0);
    g_sys_screen_frames = 0;
    g_sys_screen = SCREEN_BEST_TIMES_UPDATE;
    disp_Show();
}

/* SCREEN_BEST_TIMES_UPDATE: shows the table for eight seconds or until cross
 * is pressed. */
void fight_BestTimesUpdate(void) {
    g_sys_screen_frames++;
    g_disp_prim_cursor +=
        hud_DrawBestTimes((s32)g_disp_prim_cursor, 1, g_chanbara_rank) / 4;
    if ((g_pad_state.pressed & PAD_ANY(PADRdown)) ||
        g_sys_screen_frames > RESULT_SCREEN_FRAMES) {
        fight_LeaveBestTimes();
    }
}

/* ---- over a slowly turning view of the stage ---- */

/* SCREEN_AFTER_MATCH_INIT. */
void fight_AfterMatchInit(void) {
    disp_Blank();
    g_fight_choice = FIGHT_CHOICE_REMATCH;
    g_fight_choice_timer = 0;
    g_sys_screen_frames = 0;
    g_sys_screen = SCREEN_AFTER_MATCH_UPDATE;
    disp_Show();
}

/* SCREEN_AFTER_MATCH_UPDATE: the choice of again, character select or main
 * menu over a slowly turning view of the stage. A choice takes effect two
 * seconds after it is made; "again" starts a team battle over, and any
 * other fight where the fighters are. */
void fight_AfterMatchUpdate(void) {
    stage_SetFarView(0);
    g_camera_main.focus.x = 0;
    g_camera_main.focus.y = -TURNING_VIEW_HEIGHT;
    g_camera_main.focus.z = 0;
    g_camera_main.angles.vx = TURNING_VIEW_PITCH;
    g_camera_main.angles.vz = 0;
    g_camera_main.eye_distance = TURNING_VIEW_DISTANCE;
    g_camera_main.angles.vy = g_disp_frame_count << TURNING_VIEW_SPEED_SHIFT;
    scene_Begin(
        &g_camera_main.focus, &g_camera_main.angles, TURNING_VIEW_DISTANCE);
    scene_End(1);

#if RB_PORT
    /* a match goes on, or is decided, with a card in place of the menu
     * (port_match.h) */
    if (port_MatchAfterFight(g_pad_state.pressed)) {
        return;
    }
#endif
    if (g_fight_choice_timer == 0) {
        g_disp_prim_cursor +=
            hud_DrawPanel(HUD_PANEL_AFTER_FIGHT, g_fight_choice,
                          (s32)g_disp_prim_cursor, 0) /
            4;

        if ((g_pad_state.pressed & PAD_ANY(PADLup))) {
            snd_Play(MENU_SOUND_CURSOR, SND_VOLUME_MAX, SND_VOLUME_MAX);
            if (g_fight_choice != FIGHT_CHOICE_REMATCH) {
                g_fight_choice--;
            } else {
                g_fight_choice = FIGHT_CHOICE_TOP_MENU;
            }
        } else if ((g_pad_state.pressed & PAD_ANY(PADLdown))) {
            snd_Play(MENU_SOUND_CURSOR, SND_VOLUME_MAX, SND_VOLUME_MAX);
            if (g_fight_choice == FIGHT_CHOICE_TOP_MENU) {
                g_fight_choice = FIGHT_CHOICE_REMATCH;
            } else {
                g_fight_choice++;
            }
        }

        if ((g_pad_state.pressed & PAD_ANY(PADRdown))) {
            snd_Play(MENU_SOUND_CONFIRM, SND_VOLUME_MAX, SND_VOLUME_MAX);
            g_fight_choice_timer = (g_fight_choice == FIGHT_CHOICE_REMATCH)
                                       ? 1
                                       : FIGHT_CHOICE_FRAMES;
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

    cdrom_StopAndWait();
    if (g_fight_choice == FIGHT_CHOICE_REMATCH) {
        if (g_fight_mode == FIGHT_MODE_TEAM) {
            disp_Reset();
            motion_ForgetFiles();
            g_team_match = 0;
            fight_NextTeamMatch();
            return;
        }
        g_fight_restart = 1;
#if RB_PORT
        /* a Crew match begun again loads its crews' first (port_match.h) */
        if (port_MatchRematchLoads()) {
            g_fight_restart = 0;
        }
#endif
        g_fight_continues++;
        g_fight_restart_pair =
            fight_FindNearestStartPair(&g_fighters[g_fight_winner].world_pos);
        g_sys_screen = SCREEN_FIGHT_INIT;
        return;
    }
    if (g_fight_choice == FIGHT_CHOICE_SUB_MENU) {
        snd_EndFight();
        g_menu_reopen_mode = 1;
        g_sys_screen = SCREEN_MENU_INIT;
        return;
    }
    if (g_fight_choice == FIGHT_CHOICE_TOP_MENU) {
        snd_EndFight();
        g_sys_screen = SCREEN_MENU_INIT;
    }
}

/* SCREEN_KILL_COUNT_INIT. */
void fight_KillCountInit(void) {
    disp_Blank();
    eff_Stop();
    g_sys_screen_frames = 0;
    g_sys_screen = SCREEN_KILL_COUNT_UPDATE;
    disp_Show();
}

/* SCREEN_KILL_COUNT_UPDATE: the number of opponents a run against the CPU
 * met, over a slowly turning view of the stage, for five seconds or until
 * cross is pressed; then the main menu. */
void fight_KillCountUpdate(void) {
    stage_SetFarView(0);
    g_camera_main.focus.x = 0;
    g_camera_main.focus.y = -TURNING_VIEW_HEIGHT;
    g_camera_main.focus.z = 0;
    g_camera_main.angles.vx = TURNING_VIEW_PITCH;
    g_camera_main.angles.vz = 0;
    g_camera_main.eye_distance = TURNING_VIEW_DISTANCE;
    g_camera_main.angles.vy = g_disp_frame_count << TURNING_VIEW_SPEED_SHIFT;
    scene_Begin(
        &g_camera_main.focus, &g_camera_main.angles, TURNING_VIEW_DISTANCE);
    scene_End(1);

    /* (not divided by 4, as in cutscene_EndingUpdate: the original's
     * behaviour) */
#if RB_FIX_PRIM_CURSOR
    if (g_rb_settings.bugfixes) {
        g_disp_prim_cursor +=
            hud_DrawKillCount(g_vscpu_count, (s32)g_disp_prim_cursor, 1) / 4;
    } else
#endif
    {
        g_disp_prim_cursor +=
            hud_DrawKillCount(g_vscpu_count, (s32)g_disp_prim_cursor, 1);
    }
    g_sys_screen_frames++;
    if (g_sys_screen_frames > KILL_COUNT_FRAMES ||
        (g_pad_state.pressed & PAD_ANY(PADRdown))) {
        cdrom_StopAndWait();
        snd_EndFight();
        g_sys_screen = SCREEN_MENU_INIT;
    }
}

/* ---- the kaishaku ---- */

/* The pair of start positions the kaishaku is played at. */
s32 g_kaishaku_start_pair;

/* The kaishaku's four recordings among the shared cutscenes: with a blade
 * and with a gun, each for either fighter as the winner. */
enum {
    KAISHAKU_CUTSCENE_BLADE = 6,
    KAISHAKU_CUTSCENE_GUN = 8,
};

/* SCREEN_KAISHAKU_INIT: a fighter has gone into the kaishaku move
 * (fight_UpdateFighterPair), which makes the other the winner. The rest is
 * a cutscene, played at the start position nearest the loser. */
void cutscene_KaishakuInit(void) {
    s32 cutscene;
    s8 winner;

    snd_EndFight();
    cdrom_StopAndWait();
    disp_Blank();
    disp_Reset();
    disp_SetBackground(1, 0, 0, 0);
    stage_UnfogModel();
    stage_SetFirstPerson(0);
    SetGeomScreen(math_FovToScreenDist(KAISHAKU_FOV));
    winner = g_fight_winner;
    {
        if (WEAPON_IS_GUN(g_fighters[winner].weapon_id)) {
            cutscene = KAISHAKU_CUTSCENE_GUN;
            if (winner != 0) {
                cutscene = KAISHAKU_CUTSCENE_GUN + 1;
            }
        } else {
            cutscene = KAISHAKU_CUTSCENE_BLADE;
            if (winner != 0) {
                cutscene = KAISHAKU_CUTSCENE_BLADE + 1;
            }
        }
    }
    {
        Vec3 *loser_pos = &g_fighters[0].world_pos;
        s32 start_pair;
        if (g_fight_winner == 0) {
            loser_pos = &g_fighters[1].world_pos;
        }
        start_pair = fight_FindNearestStartPair(loser_pos);
        g_kaishaku_start_pair = start_pair;
        cutscene_StartFromSet(
            CUTSCENE_SET_SHARED, cutscene, start_pair,
            g_fighters[0].weapon_model, g_fighters[1].weapon_model, -1, -1, 0);
    }
    model_SetOutline(0, 0);
    model_SetOutline(1, 0);
    eff_Stop();
    g_sys_screen_frames = 0;
    g_sys_screen = SCREEN_KAISHAKU_UPDATE;
    disp_Show();
}

/* SCREEN_KAISHAKU_UPDATE: plays the cutscene with its sounds (the winner's
 * voice, the weapon, the cut, the loser's cry, the fall) and its blood, to
 * its end or until cross is pressed; counts the win in versus; and goes to
 * the menu that follows a match. In the story, with a guest in, the
 * player's own fighter takes over from here, as when the guest falls. */
void cutscene_KaishakuUpdate(void) {
    s32 unused[4]; /* nothing uses it, nor the second */
    s32 blood_pos[3];
    s32 unused_2[2];
    s16 *view_angles;
    s32 *eye;
    s32 over = 0;
    s8 winner;
    s16 winner_character;

    snd_UpdateStageSounds();
    winner = g_fight_winner;
    winner_character = g_fighters[winner].character;
    if (g_sys_screen_frames == g_kaishaku_voice_frames[winner_character]) {
        snd_Play(SND_FIGHTER_COUNT * winner + SND_FIGHTER_KAISHAKU_VOICE,
                 SND_VOLUME_MAX, SND_VOLUME_MAX);
    }
    if (g_sys_screen_frames == g_kaishaku_cues.swing_frame) {
        snd_Play(SND_FIGHTER_COUNT * g_fight_winner + SND_FIGHTER_SHOT,
                 SND_VOLUME_MAX, SND_VOLUME_MAX);
    }
    if (g_sys_screen_frames == g_kaishaku_cues.cut_frame) {
        snd_Play(SND_FIGHTER_COUNT * g_fight_winner + SND_FIGHTER_KAISHAKU_CUT,
                 SND_VOLUME_MAX, SND_VOLUME_MAX);
    }
    if (g_sys_screen_frames == g_kaishaku_cues.cry_frame) {
        if (g_fight_winner == 0) {
            snd_Play(SND_FIGHTER_COUNT + SND_FIGHTER_DEATH_CRY, SND_VOLUME_MAX,
                     SND_VOLUME_MAX);
        } else {
            snd_Play(SND_FIGHTER_DEATH_CRY, SND_VOLUME_MAX, SND_VOLUME_MAX);
        }
    }
    if (g_sys_screen_frames == g_kaishaku_cues.fall_frame) {
        snd_Play(SND_STAGE_FIRST, SND_VOLUME_MAX, SND_VOLUME_MAX);
    }
    if (g_sys_screen_frames == g_kaishaku_cues.blood_frame) {
        fight_GetStartPosition(0, blood_pos, g_kaishaku_start_pair);
        blood_pos[0] += g_kaishaku_cues.blood_x;
        blood_pos[1] += g_kaishaku_cues.blood_y;
        blood_pos[2] += g_kaishaku_cues.blood_z;
        eff_StartBlood(blood_pos, &g_kaishaku_blood_dir.vx);
    }
    view_angles = cutscene_GetViewAngles();
    eye = node_GetCameraPosition();
    eff_Update(view_angles, eye);
    if (cutscene_RunFrame() == 0) {
        over = 1;
    }
    if (over || (g_pad_state.pressed & PAD_ANY(PADRdown))) {
        cutscene_End();
        if (g_fight_mode == FIGHT_MODE_VS ||
            g_fight_mode == FIGHT_MODE_NETWORK) {
            /* the second count is reached from the first (see fight.h) */
            (&g_fight_wins_p1)[g_fight_winner]++;
        }
#if RB_PORT
        /* the surrenderer's loss, counted into the match (port_match.h) */
        port_MatchFightOver(
            g_fight_mode, g_fight_winner != 0, g_fight_winner == 0);
#endif
        /* the cutscene read through the motion files' streams */
        motion_OpenStream(0, g_motion_common_sections.stream);
#if RB_PORT
        /* Only the slots that hold a motion file: when the two fighters
         * fight with the one set, or in chanbara, the other slot's stream
         * is the null it was at power on, and what the console reads
         * through it is its kernel's memory at address 0 (fighter.c,
         * motion_OpenStreams). Nothing then reads that decoder. */
        if (g_motion_loaded_sets[0] != 0) {
            motion_OpenStream(1, g_motion_fighter_sections[0].stream);
        }
        if (g_motion_loaded_sets[1] != 0 &&
            g_motion_loaded_sets[1] != MOTION_SET_PACK) {
            motion_OpenStream(2, g_motion_fighter_sections[1].stream);
        }
#else
        motion_OpenStream(1, g_motion_fighter_sections[0].stream);
        motion_OpenStream(2, g_motion_fighter_sections[1].stream);
#endif
        if (g_fight_mode == FIGHT_MODE_STORY &&
            g_story_player_roster != STORY_NO_ROSTER) {
            snd_EndFight();
            story_PutPlayerBack();
            fight_LoadFighter(0);
            fight_LoadWeapons();
            story_PlayInterludeScene(INTERLUDE_GUEST_FELL);
        } else {
            g_sys_screen = SCREEN_AFTER_MATCH_INIT;
#if RB_PORT
            /* training: both start again instead, as after a kill
             * (port_training.h) */
            port_TrainingRestart();
#endif
        }
    }
    g_sys_screen_frames++;
}

/* The cutscene of each ending, among the shared ones. */
/* clang-format off */
static u8 g_ending_cutscenes[ENDING_COUNT] = {
    0,  /* ENDING_STORY */
    1,  /* ENDING_STORY + 1 */
    2,  /* ENDING_VSCPU */
    3,  /* ENDING_VSCPU + 1 */
    4,  /* ENDING_TEAM */
    5,  /* ENDING_TEAM + 1 */
    11, /* ENDING_CHANBARA_FAST */
    10, /* ENDING_CHANBARA_FAST + 1 */
    12, /* ENDING_CHANBARA_CLEAN */
    13, /* ENDING_CHANBARA_CLEAN + 1 */
};
/* clang-format on */
