/* The main menu as a screen of the main loop: which of its pages is up (the
 * top menu, options, character select, the memory-card prompts...), moving
 * between them, and leaving for a fight. The pages themselves are in
 * menu_pages.c, character select in select.c, team battle's in
 * select_team.c and the narration in narration.c; this file only runs
 * them, through the steps in menu_run.c.
 *
 * Also here: the options and the fight's settings on their way between the
 * menus' working record (GameSettings), the save data and the fight; and the
 * save screen. */
#include "common.h"
#include "bb2.h"

/* Defined further down. */
static u8 g_menu_close_sound_vabs;
static u8 g_menu_boot_card_check;

/* The pages of the main menu screen (g_menu_page). */
enum MenuPage {
    MENU_PAGE_TOP = 1,
    MENU_PAGE_OPTIONS = 2,
    MENU_PAGE_SELECT = 3,    /* character select */
    MENU_PAGE_NARRATION = 4, /* story mode's opening text */
    MENU_PAGE_PRACTICE = 5,  /* training mode: which kind of practice */
    MENU_PAGE_CHANBARA = 6,  /* chanbara: the best times, and a question */
    /* at boot: look for a save, ask whether to load it, load it */
    MENU_PAGE_CARD_CHECK = 7,
    MENU_PAGE_CARD_ASK_LOAD = 8,
    MENU_PAGE_CARD_LOAD = 9,
    MENU_PAGE_TEAM_SELECT = 10, /* team battle's character select */
    /* the top menu's music fading out before the chosen mode, or the movie */
    MENU_PAGE_FADE_TO_MODE = 11,
    MENU_PAGE_FADE_TO_MOVIE = 12,
    /* load and save from the options page */
    MENU_PAGE_OPTIONS_LOAD = 13,
    MENU_PAGE_OPTIONS_SAVE = 14,
};

/* The rows of the top menu that can be chosen (menu_SetEnabledRows): slash
 * mode is shut until the staff roll has been seen with all six of the
 * story's fighters. */
#define MENU_ROWS_ALL 0xFF
#define MENU_ROWS_NO_SLASH (MENU_ROWS_ALL & ~(1 << FIGHT_MODE_CHANBARA))
#define STORY_FIGHTERS_ALL 0x3F

/* The fighters on offer in character select, as a mask over the unlocked
 * ones: in the one-player modes, which leave out the two gunners (TSUBAME
 * and KATZE); in team battle and over the link cable; and in versus mode,
 * which adds the two that a chanbara run without a continue wins. The
 * game says as much when a run wins them: "Now Available in Vs Mode, Group
 * Battle Mode and Link Mode", and "Now Available in Vs Mode". */
#define SELECT_MASK_ALL 0x003FF3FF /* the twenty with a name */
#define SELECT_MASK_SOLO                                                       \
    (SELECT_MASK_ALL &                                                         \
     ~(UNLOCK_BIT(CHARACTER_TSUBAME) | UNLOCK_BIT(CHARACTER_KATZE)))
#define SELECT_MASK_VS                                                         \
    (SELECT_MASK_ALL | UNLOCK_BIT(CHARACTER_LAST_1) |                          \
     UNLOCK_BIT(CHARACTER_LAST_2))

/* The fourteen bits of the unlock mask that two consoles tell each other
 * over the link cable: every named character but the three each clan
 * starts with (UTSUSEMI to MATSUMUSHI and HONGOU to ISOHACHI). */
#define LOCKABLE_FIGHTERS 0x3F83F8

/* The top menu left alone for this long starts the movie again. */
#define MENU_IDLE_FRAMES 3241
/* Frames the music takes to fade when leaving the top menu, and the wait. */
#define MENU_FADE_FRAMES 30
#define MENU_FADE_WAIT 60

/* Music tracks. */
#define MENU_MUSIC_TOP 7
#define MENU_MUSIC_PAGES 1
#define MENU_MUSIC_NARRATION 0

/* The blue at the top of the memory-card prompts' backdrop. */
#define BACKDROP_BLUE 0x80

u8 g_menu_page;

/* Writes the options page's choices into the save data. */
static void menu_StoreOptions(void) {
    GameSettings *settings;
    s32 i;

    settings = menu_GetSettings();
    g_saved_record.flags &=
        ~(SAVE_FLAG_GRAYSCALE | SAVE_FLAG_EASY_CPU | SAVE_FLAG_MUSIC_OFF);
    g_saved_record.flags |= settings->grayscale ? SAVE_FLAG_GRAYSCALE : 0;
    g_saved_record.flags |= settings->easy_cpu ? SAVE_FLAG_EASY_CPU : 0;
    g_saved_record.flags |= settings->music_off ? SAVE_FLAG_MUSIC_OFF : 0;
    for (i = 0; i < MENU_ACTIONS; i++) {
        g_saved_record.button_config[i] = settings->button_mappings[i];
    }
}

/* Leaves the menus for a fight: copies what was chosen (fighters, weapons,
 * stage, view, and the mode's own settings) into g_fight_config and the
 * fight's variables, and starts the mode. */
static void menu_StartFight(void) {
    GameSettings *settings;
    s32 i;
    u8 *pick;
    /* FAKE: the two constants in locals; read them as the constants */
    s32 five;
    s32 twenty;
    s8 mode;
    u8 weapon_id;

    settings = menu_GetSettings();
    i = 0;
    five = ROSTER_MIKADO; /* the last of the Narukagami's three */
    twenty = ROSTER_JO;   /* and of the Shainto's */
    /* each fighter's first pick: match_picks[i][0] */
    pick = (u8 *)settings;

    do {
        s8 roster_index = g_fighter_character_rosters[pick[0]];
        g_fight_config.roster[i] = roster_index;
        /* Story mode takes the three fighters each clan starts with as
         * their other roster entries, three before them. */
        if ((roster_index >= ROSTER_KANNUKI &&
             roster_index <= ROSTER_TATSUMI) ||
            roster_index == five ||
            (roster_index >= ROSTER_GENGORO && roster_index <= ROSTER_KAUN) ||
            roster_index == twenty) {
            if (g_fight_config.mode == FIGHT_MODE_STORY) {
                g_fight_config.roster[i] =
                    g_fight_config.roster[i] -
                    (ROSTER_KANNUKI - ROSTER_KANNUKI_STORY);
            }
        }
        weapon_id = pick[1];
        pick += sizeof(settings->match_picks[0]);
        g_fight_config.is_cpu[i] = 0;
        g_fight_config.weapon[i] = weapon_id;
        i++;
    } while (i < 2);

    g_fight_config.stage = settings->stage_index;
    g_fight_config.first_person = settings->first_person;
    g_fight_viewer = 0;
    mode = g_fight_config.mode;

    if (mode == FIGHT_MODE_PRACTICE) {
        g_fight_practice_round = settings->practice_round;
        g_fight_practice_options = settings->toggle_option_bits;
    } else if (mode == FIGHT_MODE_TEAM) {
        s32 win_target;

        g_team_match_count = settings->match_count_index + 3;
        g_team_time_limit = g_team_time_limits[settings->time_limit_index];
        win_target = 1;
        if (settings->win_target_index) {
            win_target = 2;
        }
        g_team_points_to_win = win_target;
        pick = g_team_weapons[0];
        g_team_clan_bits = settings->team_clan_bits;
        /* Both teams' five picks: who fights in each match, with what. */
        {
            s32 match = 0;
            u8 *weapon_id_row = pick;
            u8 *roster_index_row = g_team_rosters[0];
            do {
                s32 side = 0;
                u8 *weapon_id_dst = weapon_id_row;
                u8 *roster_index_dst = roster_index_row;
                s32 offset = match << 1;
                do {
                    u8 *match_pick = (u8 *)settings + offset;
                    *roster_index_dst =
                        g_fighter_character_rosters[match_pick[0]];
                    offset += sizeof(settings->match_picks[0]);
                    side++;
                    *weapon_id_dst = match_pick[1];
                    roster_index_dst++;
                    weapon_id_dst++;
                } while (side < 2);
                weapon_id_row += 2;
                match++;
                roster_index_row += 2;
            } while (match < TEAM_MAX_MATCHES);
        }
    }

    fight_StartMode();
}

/* A time in frames as the menus show it. */
static inline u8 frames_to_minutes(s32 frames) {
    return frames / (60 * FRAMES_PER_SECOND); /* 60 seconds a minute */
}

static inline u8 frames_to_seconds(s32 frames) {
    return (frames / FRAMES_PER_SECOND) % 60; /* seconds past the minute */
}

static inline u8 frames_to_hundredths(s32 frames) {
    /* the frames past the second, as hundredths of one */
    return (frames % FRAMES_PER_SECOND) * 100 / FRAMES_PER_SECOND;
}

/* Sets the menus' working record from the save data: the options, the
 * button configuration and the three best times. */
static void menu_FetchOptions(void) {
    GameSettings *settings;
    u8 *src;
    TimeRec *times;
    s32 i;

    settings = menu_GetSettings();
    i = 0;
    /* the three SAVE_FLAG_... bits, a bit each */
    settings->grayscale = g_saved_record.flags & 1;
    settings->easy_cpu = (g_saved_record.flags >> 1) & 1;
    settings->music_off = (g_saved_record.flags >> 2) & 1;
    src = g_saved_record.button_config;
    for (; i < MENU_ACTIONS; i++) {
        settings->button_mappings[i] = *src;
        settings->committed_button_mappings[i] = *src;
        src++;
    }
    times = g_saved_record.times;
#if RB_PORT
    /* ReBushido's top menu: the options are the window's settings */
    if (port_TopMenuIsOn()) {
        port_TopMenuTakeOptions();
    }
#endif
    for (i = 0; i < BEST_TIMES_ROWS; i++) {
        settings->time_records[i].minutes =
            frames_to_minutes(times[i].time_frames);
        settings->time_records[i].seconds =
            frames_to_seconds(times[i].time_frames);
        settings->time_records[i].hundredths =
            frames_to_hundredths(times[i].time_frames);
        settings->time_records[i].character_index =
            times[i].holder_character_index;
    }
}

/* SCREEN_UNUSED's handler. */
void sys_UnusedScreen(void) {}

/* Back to the top menu with the options as saved. */
static void menu_ResetToTop(void) {
    s32 enabled_rows;

    g_menu_page = MENU_PAGE_TOP;
    menu_FetchOptions();
    if (SAVE_STAFF_ROLLS() == STORY_FIGHTERS_ALL) {
        enabled_rows = MENU_ROWS_ALL;
    } else {
        enabled_rows = MENU_ROWS_NO_SLASH;
    }
    menu_SetEnabledRows(enabled_rows);
}

/* SCREEN_MENU_INIT: drops everything the fight had loaded and loads the
 * menu. */
void menu_Init(void) {
    disp_Reset();
    disp_Blank();
    if (!g_menu_reopen_mode) {
        link_RequestClose();
#if RB_PORT
        /* ReBushido's top menu comes up on its top page, and training
         * is over (port_training.h) */
        port_TopMenuReset();
        port_TrainingChoose(FALSE);
#endif
    }
    motion_ForgetFiles();
    model_Free(0);
    model_Free(1);
    eff_ClearInitFlag();
    if (g_menu_close_sound_vabs) {
        snd_CloseAllVabs();
        g_fight_loaded_stage = -1;
    }
    g_menu_close_sound_vabs = 1;
    menu_ResetToTop();
    menu_Open(SYS_LOAD_ADDR);
    g_sys_screen_frames = 0;
    g_sys_screen = SCREEN_MENU_UPDATE;
    disp_Show();
}

/* The backdrop of the memory-card prompts: the whole screen, blue at the top
 * fading to black at the bottom, behind everything. */
static void menu_DrawBackdrop(void) {
    POLY_G4 *backdrop;

    backdrop = (POLY_G4 *)g_disp_prim_cursor;
    SetPolyG4(backdrop);
    setXY4(
        backdrop, 0, 0, DISP_WIDTH, 0, 0, DISP_HEIGHT, DISP_WIDTH, DISP_HEIGHT);
    setRGB0(backdrop, 0, 0, BACKDROP_BLUE);
    setRGB1(backdrop, 0, 0, BACKDROP_BLUE);
    setRGB2(backdrop, 0, 0, 0);
    setRGB3(backdrop, 0, 0, 0);
    AddPrim(g_disp_ot + (DISP_OT_SIZE - 1), backdrop);
    backdrop += 1;
    g_disp_prim_cursor = (u32 *)backdrop;
}

/* The music of the pages under the top menu. */
static void menu_StartMusic(void) {
    snd_EnableCdAudio();
    cdrom_StartMusic(MENU_MUSIC_PAGES);
    cdrom_SetAudioLoop(1);
}

/* Opens the first page of the mode (or of the options) picked on the top
 * menu. Coming back from a fight (g_menu_reopen_mode) every mode with a
 * character select opens straight on it. */
static void menu_EnterMode(s32 mode) {
    s32 link_result;

    switch (mode) {
    case FIGHT_MODE_STORY:
        if (!g_menu_reopen_mode) {
            narration_Load(SYS_LOAD_ADDR);
            snd_EnableCdAudio();
            cdrom_StartMusic(MENU_MUSIC_NARRATION);
            g_menu_page = MENU_PAGE_NARRATION;
            return;
        }
        select_Open(SYS_LOAD_ADDR);
        g_menu_page = MENU_PAGE_SELECT;
        menu_StartMusic();
        return;

    case FIGHT_MODE_PRACTICE:
        g_menu_page = MENU_PAGE_PRACTICE;
        menu_StartMusic();
        return;

    case FIGHT_MODE_CHANBARA:
        if (!g_menu_reopen_mode) {
            menu_OpenChanbara();
            g_menu_page = MENU_PAGE_CHANBARA;
        } else {
            select_Open(SYS_LOAD_ADDR);
            g_menu_page = MENU_PAGE_SELECT;
            menu_StartMusic();
        }
        /* a second time after a fight: the original's behaviour */
#if RB_FIX_CHANBARA_MUSIC
        if (g_rb_settings.bugfixes && g_menu_reopen_mode) {
            /* it has just been started */
        } else
#endif
        {
            menu_StartMusic();
        }
        return;

    case FIGHT_MODE_NETWORK:
        if (g_menu_reopen_mode) {
            select_Open(SYS_LOAD_ADDR);
            g_menu_page = MENU_PAGE_SELECT;
            menu_StartMusic();
            return;
        }

        /* Both consoles offer what either has unlocked. */
        link_result = link_Connect();
        if (link_result == 1) {
            select_SetUnlocks((SAVE_UNLOCKS() | g_link_peer_unlock_mask |
                               SAVE_DEFAULT_UNLOCKS) &
                              SELECT_MASK_ALL);
            select_Open(SYS_LOAD_ADDR);
            g_menu_page = MENU_PAGE_SELECT;
            menu_StartMusic();
            return;
        }

        if (link_result == -1) {
            snd_Play(MENU_SOUND_CANCEL, SND_VOLUME_MAX, SND_VOLUME_MAX);
            g_sys_screen = SCREEN_MENU_UPDATE;
            g_sys_screen_frames = 0;
            g_menu_page = MENU_PAGE_TOP;
        }
        return;

    case FIGHT_MODE_VSCPU:
        select_SetUnlocks(SAVE_UNLOCKS() & SELECT_MASK_SOLO);
        select_Open(SYS_LOAD_ADDR);
        g_menu_page = MENU_PAGE_SELECT;
        menu_StartMusic();
        return;

    case FIGHT_MODE_TEAM:
        select_SetUnlocks(SAVE_UNLOCKS() & SELECT_MASK_ALL);
        select_OpenTeam(SYS_LOAD_ADDR);
        g_menu_page = MENU_PAGE_TEAM_SELECT;
        menu_StartMusic();
        return;

    case FIGHT_MODE_VS:
        select_SetUnlocks(SAVE_UNLOCKS() & SELECT_MASK_VS);
        select_Open(SYS_LOAD_ADDR);
        g_menu_page = MENU_PAGE_SELECT;
        menu_StartMusic();
        return;

    case MENU_ROW_OPTIONS:
        g_menu_page = MENU_PAGE_OPTIONS;
        menu_StartMusic();
        return;
    }
}

/* The ways out of the pages that lead to a fight. */

/* Character select is confirmed. */
static inline void menu_LeaveForFight(void) {
    cdrom_StopAndWait();
    g_sys_screen = SCREEN_FIGHT_INIT;
    menu_StartFight();
}

/* Character select is cancelled: load the menu again, keeping the fight's
 * sound banks. */
static inline void menu_Reload(void) {
    cdrom_StopAndWait();
    g_sys_screen = SCREEN_MENU_INIT;
    g_menu_close_sound_vabs = 0;
}

/* A one-player mode's first page is confirmed: on to character select. */
static inline void menu_OpenSoloSelect(void) {
    cdrom_StopAndWait();
    select_SetUnlocks(SAVE_UNLOCKS() & SELECT_MASK_SOLO);
    select_Open(SYS_LOAD_ADDR);
    g_menu_page = MENU_PAGE_SELECT;
    menu_StartMusic();
}

static inline void menu_BackToTop(void) {
    cdrom_StopAndWait();
    g_menu_page = MENU_PAGE_TOP;
    g_sys_screen_frames = 0;
}

#if RB_PORT
/* The top menu's rows (g_menu_row_modes). */
#define MENU_TOP_ROW_MODES 8

/* A local tournament's next match from the top menu (port_tournament.h):
 * versus mode's select screen where a player is in it, or with two CPUs
 * the fight itself, their fighters, weapons and stage as the select
 * screen would have left them. TRUE where it began. */
static s32 menu_PortTournamentStep(void) {
    GameSettings *settings = menu_GetSettings();
    s32 next = port_TournamentTopMenu();
    u8 fighters[2];
    u8 weapons[2];
    u8 stage;
    s32 side;
    s32 row;

    if (next == PORT_TOURNAMENT_STAY) {
        return FALSE;
    }
    /* the top menu's Versus row as if it were chosen (menu_RunTop): its
     * row in the settings, the second pad's side of the select screen */
    for (row = 0; row < MENU_TOP_ROW_MODES; row++) {
        if (g_menu_row_modes[row][0] == FIGHT_MODE_VS) {
            settings->menu_mode = row;
        }
    }
    g_fight_config.mode = menu_GetChosenMode();
    switch (next) {
    case PORT_TOURNAMENT_TO_SELECT:
        cdrom_StopAndWait();
        menu_EnterMode(g_fight_config.mode);
        return TRUE;
    case PORT_TOURNAMENT_TO_FIGHT:
        port_TournamentFightPicks(fighters, weapons, &stage);
        for (side = 0; side < 2; side++) {
            settings->match_picks[side][0].fighter = fighters[side];
            settings->match_picks[side][0].weapon = weapons[side];
        }
        settings->stage_index = stage;
        /* as a select screen begins: no arena, the kinds' default, no
         * mod's weapon, the music the window's (select.c, select_Load) */
        port_StageChoiceReset();
        port_PlayReset();
        port_WeaponPickReset();
        port_FightMusicReset();
        menu_LeaveForFight();
        return TRUE;
    default:
        return FALSE;
    }
}
#endif

/* SCREEN_MENU_UPDATE: one frame of whichever page is up. */
void menu_Update(void) {
    s32 result;

    g_sys_frame_period = 1;
    rand();
    /* The first time the top menu comes up, look for a save to load. */
    if (g_menu_page == MENU_PAGE_TOP && g_menu_boot_card_check) {
        g_menu_boot_card_check = 0;
        g_menu_page = MENU_PAGE_CARD_CHECK;
    }
    if (g_menu_reopen_mode) {
        menu_EnterMode(g_fight_mode);
        g_menu_reopen_mode = 0;
    }
    switch (g_menu_page) {
    case MENU_PAGE_CARD_CHECK:
        menu_DrawBackdrop();
        result = memcard_BootCheckScreen();
        if (result == 1) { /* there is a save */
            g_menu_page = MENU_PAGE_CARD_ASK_LOAD;
#if RB_PORT
            /* ReBushido's top menu loads it without asking: the port's
             * cards are files kept beside it */
            if (port_TopMenuIsOn()) {
                g_menu_page = MENU_PAGE_CARD_LOAD;
            }
#endif
            break;
        }
        if (result == -1) { /* there is none */
            g_menu_page = MENU_PAGE_TOP;
        }
        break;
    case MENU_PAGE_CARD_ASK_LOAD:
        menu_DrawBackdrop();
        result = memcard_BootAskLoadScreen();
        if (result == PAGE_CONFIRMED) {
            g_menu_page = MENU_PAGE_CARD_LOAD;
            break;
        }
        if (result == PAGE_CANCELED) {
            g_menu_page = MENU_PAGE_TOP;
        }
        break;
    case MENU_PAGE_CARD_LOAD:
        menu_DrawBackdrop();
        if (memcard_LoadScreen()) {
            menu_ResetToTop();
        }
        break;
    case MENU_PAGE_TOP:
        if (g_sys_screen_frames == 0) {
            snd_EnableCdAudio();
            cdrom_StartMusic(MENU_MUSIC_TOP);
            cdrom_SetAudioLoop(1);
        }
        /* Count the frames nothing is held; too many and the movie plays. */
        g_sys_screen_frames++;
        if (g_pad_state.held) {
            g_sys_screen_frames = 1;
        }
#if RB_PORT
        /* a page of the window's over ReBushido's top menu is the
         * player's, however long nothing is pressed (port_top_menu.h) */
        if (port_TopMenuIsCovered()) {
            g_sys_screen_frames = 1;
        }
#endif
        if (g_sys_screen_frames >= MENU_IDLE_FRAMES) {
            g_sys_screen = SCREEN_MOVIE;
        }
#if RB_PORT
        if (menu_PortTournamentStep()) {
            break;
        }
#endif
        result = menu_RunTop(g_pad_state.held, g_pad_state.pressed);
        if (result == PAGE_RUNNING) {
            break;
        }
        if (result == PAGE_CONFIRMED) {
            cdrom_FadeMix(MENU_FADE_FRAMES, 0, 0, 0, 0);
            g_menu_page = MENU_PAGE_FADE_TO_MODE;
        } else if (result == PAGE_CANCELED) {
            cdrom_FadeMix(MENU_FADE_FRAMES, 0, 0, 0, 0);
            g_menu_page = MENU_PAGE_FADE_TO_MOVIE;
        } else {
            break;
        }
        g_sys_screen_frames = MENU_FADE_WAIT;
        break;
    case MENU_PAGE_FADE_TO_MODE:
        if (--g_sys_screen_frames == 0) {
            cdrom_StopAndWait();
            g_fight_config.mode = menu_GetChosenMode();
            menu_EnterMode(g_fight_config.mode);
        }
        break;
    case MENU_PAGE_FADE_TO_MOVIE:
        if (--g_sys_screen_frames == 0) {
            cdrom_StopAndWait();
            g_sys_screen = SCREEN_MOVIE;
        }
        break;
    case MENU_PAGE_OPTIONS:
        switch (menu_RunOptions(g_pad_state.held, g_pad_state.pressed)) {
        case MENU_OPTIONS_LEFT:
        case MENU_OPTIONS_BACK:
            cdrom_StopAndWait();
            g_menu_page = MENU_PAGE_TOP;
            menu_StoreOptions();
            g_sys_screen_frames = 0;
            if (SAVE_STAFF_ROLLS() == STORY_FIGHTERS_ALL) {
                menu_SetEnabledRows(MENU_ROWS_ALL);
            } else {
                menu_SetEnabledRows(MENU_ROWS_NO_SLASH);
            }
            break;
        case PAGE_RUNNING:
            break;
        case MENU_OPTIONS_LOAD:
            menu_StoreOptions();
            g_menu_page = MENU_PAGE_OPTIONS_LOAD;
            break;
        case MENU_OPTIONS_SAVE:
            menu_StoreOptions();
            g_menu_page = MENU_PAGE_OPTIONS_SAVE;
            break;
        }
        break;
    case MENU_PAGE_OPTIONS_LOAD:
        /* the memory card menu stays up behind the messages */
        menu_UpdateCardMenu(0, 0);
        if (memcard_LoadScreen()) {
            menu_ReturnToCardMenu();
            menu_FetchOptions();
            g_menu_page = MENU_PAGE_OPTIONS;
        }
        break;
    case MENU_PAGE_OPTIONS_SAVE:
        menu_UpdateCardMenu(0, 0);
        if (memcard_SaveScreen()) {
            menu_ReturnToCardMenu();
            g_menu_page = MENU_PAGE_OPTIONS;
        }
        break;

    case MENU_PAGE_SELECT:
        result = select_Run(g_pad_state.held, g_pad_state.pressed);
        if (result == PAGE_RUNNING) {
            break;
        }
        if (result == PAGE_CONFIRMED) {
            menu_LeaveForFight();
        } else if (result == PAGE_CANCELED) {
            menu_Reload();
        }
        break;
    case MENU_PAGE_NARRATION:
        result = narration_Update(g_pad_state.pressed);
        if (result == PAGE_RUNNING) {
            break;
        }
        if (result == PAGE_CONFIRMED) {
            menu_OpenSoloSelect();
        } else if (result == PAGE_CANCELED) {
            menu_Reload();
        }
        break;
    case MENU_PAGE_PRACTICE:
        result = menu_RunPractice(g_pad_state.held, g_pad_state.pressed);
        if (result == PAGE_RUNNING) {
            break;
        }
        if (result == PAGE_CONFIRMED) {
            menu_OpenSoloSelect();
        } else if (result == PAGE_CANCELED) {
            menu_BackToTop();
        }
        break;
    case MENU_PAGE_CHANBARA:
        result = menu_RunChanbara(g_pad_state.held, g_pad_state.pressed);
        if (result == PAGE_RUNNING) {
            break;
        }
        if (result == PAGE_CONFIRMED) {
            menu_OpenSoloSelect();
        } else if (result == PAGE_CANCELED) {
            menu_BackToTop();
        }
        break;
    case MENU_PAGE_TEAM_SELECT:
        result = select_RunTeam(g_pad_state.held, g_pad_state.pressed);
        if (result == PAGE_RUNNING) {
            break;
        }
        if (result == PAGE_CONFIRMED) {
            menu_LeaveForFight();
        } else if (result == PAGE_CANCELED) {
            menu_Reload();
        }
        break;
    }
    if (g_sys_screen != SCREEN_MENU_UPDATE) {
        g_sys_frame_period = 2;
    }
}

/* SCREEN_SAVE_INIT: as menu_Init, for the save prompts alone. */
void menu_SaveInit(void) {
    disp_Reset();
    disp_Blank();
    motion_ForgetFiles();
    model_Free(0);
    model_Free(1);
    eff_ClearInitFlag();
    snd_CloseAllVabs();
    menu_Open(SYS_LOAD_ADDR);
    g_sys_screen = SCREEN_SAVE_UPDATE;
    disp_Show();
}

/* SCREEN_SAVE_UPDATE: the save prompts, then the main menu. */
void menu_SaveUpdate(void) {
    g_sys_frame_period = 1;
    menu_DrawBackdrop();
    if (memcard_SaveScreen()) {
        g_sys_screen = SCREEN_MENU_INIT;
        g_sys_frame_period = 2;
    }
}

/* Gathers the lockable fighters' bits of an unlock mask into the low bits of
 * a word. */
s32 link_PackUnlockMask(s32 unlock_mask) {
    s32 result = 0;
    s32 i = 0;
    s32 j = 0;
    s32 mask = LOCKABLE_FIGHTERS;
    s32 one = 1;
    do {
        if ((mask >> i) & 1) {
            if (unlock_mask & (one << i)) {
                result |= one << j;
            }
            j++;
        }
        i++;
    } while (i < CHARACTER_COUNT);
    return result;
}

/* Spreads them out again. */
s32 link_UnpackUnlockMask(s32 packed) {
    s32 result = 0;
    s32 i = 0;
    s32 j = 0;
    s32 mask = LOCKABLE_FIGHTERS;
    s32 one = 1;
    do {
        if ((mask >> i) & 1) {
            if (packed & (one << j)) {
                result |= one << i;
            }
            j++;
        }
        i++;
    } while (i < CHARACTER_COUNT);
    return result;
}

/* Cleared for one reload of the menu to keep the fight's sound banks open
 * (backing out of character select). */
static u8 g_menu_close_sound_vabs = 1;
/* The memory card is looked at once, the first time the top menu comes up. */
static u8 g_menu_boot_card_check = 1;
u8 g_menu_reopen_mode = 0;
/* A format, "%s", and a pointer to it; nothing reads them. (Guess) left
 * from a line of debug text. */
char g_menu_unused_format[] = "%s";
static char *g_menu_unused_format_ptr = g_menu_unused_format;
