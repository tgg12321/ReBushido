/* The steps between the main menu's screen and its pages: menu_screen.c
 * decides which page is up and calls one of these each frame; these call
 * the page (menu_pages.c, select.c, select_team.c) and keep the little that
 * has to last from one page to the next. */
#include "common.h"
#include "bb2.h"

/* Which page of the options is up (g_menu_options_step). The two in the
 * middle have the values of the memory card menu's results. */
enum MenuOptionsStep {
    OPTIONS_STEP_MENU = 0,
    OPTIONS_STEP_CARD_MENU = 1,
    OPTIONS_STEP_LOAD = 2, /* menu_screen.c is running the load */
    OPTIONS_STEP_SAVE = 3,
    OPTIONS_STEP_KEY_CONFIG_OPEN = 4,
    OPTIONS_STEP_KEY_CONFIG = 5,
};

/* This file's variables, in the order the original has them. */
/* 1: the mode picked is for two pads */
static s32 g_select_pending_extra_players;
static s32 g_menu_options_step;
static s32 g_select_unlocks; /* the fighters character select offers */

/* Loads the main menu (menu_pages.c) to `memory`, over a black screen. */
s32 menu_Open(s32 memory) {
    menu_Load(memory, &g_game_settings);
    disp_SetBackground(1, 0, 0, 0);
    g_menu_options_step = OPTIONS_STEP_MENU;
    return 1;
}

/* A frame of the white page. Nothing calls it. */
static s32 menu_RunWhitePage(void) {
    /* called with no arguments, as the original has it */
    if (((s32(*)())menu_UpdateWhitePage)() == PAGE_CONFIRMED) {
        g_menu_options_step = OPTIONS_STEP_MENU;
        return PAGE_CONFIRMED;
    }
    return PAGE_RUNNING;
}

/* A frame of the top menu. Returns PAGE_CONFIRMED when a row is chosen (it
 * is kept in the settings), PAGE_CANCELED when the menu is left. */
s32 menu_RunTop(s32 held, s32 pressed) {
    s32 result;
    s32 row_mode;

    result = PAGE_RUNNING;
    row_mode = menu_UpdateTop(held, pressed);
    if (row_mode >= 0) {
        result = PAGE_CONFIRMED;
        g_menu_options_step = OPTIONS_STEP_MENU;
        g_game_settings.menu_mode = row_mode;
    } else if (row_mode == MENU_TOP_CANCELED) {
        result = PAGE_CANCELED;
    }
    return result;
}

/* The mode the chosen row stands for. Also notes whether the mode's
 * character select is for two pads. */
s32 menu_GetChosenMode(void) {
    g_menu_options_step = OPTIONS_STEP_MENU;
    g_select_pending_extra_players =
        g_menu_row_modes[g_game_settings.menu_mode][1];
    return g_menu_row_modes[g_game_settings.menu_mode][0];
}

/* The unlock mask's bits of the Narukagami's ten named characters
 * (KANNUKI to MATSUMUSHI) and of the Shainto's (GENGORO to ISOHACHI). */
#define NARUKAGAMI_BITS 0x3FF
#define SHAINTO_BITS (0x3FF << CHARACTER_GENGORO)

/* Says which fighters character select is to offer: `unlock_mask` has a bit
 * for each character, and the screen wants a bit for each of its own
 * twenty-two (enum SelectFighter), which leave the characters without a
 * name out: the Narukagami's ten, the Shainto's ten and the two crests,
 * closed up. */
void select_SetUnlocks(s32 unlock_mask) {
    g_select_unlocks =
        (unlock_mask & NARUKAGAMI_BITS) +
        ((u32)(unlock_mask & SHAINTO_BITS) >>
         (CHARACTER_GENGORO - SELECT_GENGORO)) +
        ((u32)(unlock_mask & UNLOCK_BIT(CHARACTER_LAST_1)) >>
         (CHARACTER_LAST_1 - SELECT_LAST_1)) +
        ((u32)(unlock_mask & UNLOCK_BIT(CHARACTER_LAST_2)) >>
         (CHARACTER_LAST_2 - SELECT_LAST_2));
}

/* Loads character select (select.c) to `memory`, over a black screen. */
s32 select_Open(s32 memory) {
    select_Load(memory, g_select_pending_extra_players, &g_game_settings,
                g_select_unlocks);
    disp_SetBackground(1, 0, 0, 0);
    return 1;
}

/* A frame of character select. When it ends, either way, the menu's sounds
 * are let go. */
s32 select_Run(s32 held, s32 pressed) {
    s32 result = select_Update(held, pressed);
    if (result) {
        snd_CloseMenuVab();
    }
    return result;
}

/* A frame of training mode's page. */
s32 menu_RunPractice(s32 held, s32 pressed) {
    g_menu_options_step = OPTIONS_STEP_MENU;
    return menu_UpdatePractice(held, pressed);
}

/* Opens chanbara's page. */
void menu_OpenChanbara(void) {
    g_menu_options_step = OPTIONS_STEP_MENU;
    disp_SetBackground(1, 0, 0, 0);
    menu_ResetChanbara();
}

/* A frame of chanbara's page. */
s32 menu_RunChanbara(s32 held, s32 pressed) {
    return menu_UpdateChanbara(held, pressed);
}

/* Loads team battle's character select to `memory`, over a black screen. */
s32 select_OpenTeam(s32 memory) {
    select_LoadTeam(memory, &g_game_settings, g_select_unlocks);
    disp_SetBackground(1, 0, 0, 0);
    return 1;
}

/* A frame of it. */
s32 select_RunTeam(s32 held, s32 pressed) {
    return select_UpdateTeam(held, pressed);
}

/* The picture that is up while the link cable connects (link_Connect). */
void menu_ShowLinkPicture(void) { menu_LoadLinkPicture(); }

void menu_HideLinkPicture(void) { menu_ClearLinkPicture(); }

/* After a load or a save from the options: back to the memory card menu. */
void menu_ReturnToCardMenu(void) {
    g_menu_options_step = OPTIONS_STEP_CARD_MENU;
}

/* A frame of the options and the pages under them: the memory card menu and
 * the key configuration. Returns MENU_OPTIONS_LEFT when the options are
 * left, MENU_OPTIONS_LOAD or MENU_OPTIONS_SAVE while a load or a save has
 * been asked for (menu_screen.c runs those, then calls
 * menu_ReturnToCardMenu), 0 otherwise. The key configuration works on a
 * copy of the mapping, kept only when the page is left by its last row. */
s32 menu_RunOptions(s32 held, s32 pressed) {
    s32 result;
    s32 page_result;

    switch (g_menu_options_step) {
    case OPTIONS_STEP_MENU:
        result = 0;
        page_result = menu_UpdateOptions(held, pressed);
        switch (page_result) {
        case OPTION_MENU_RESULT_EXIT:
            result = MENU_OPTIONS_LEFT;
            break;
        case OPTION_MENU_RESULT_MEMCARD:
            g_menu_options_step = OPTIONS_STEP_CARD_MENU;
            break;
        case OPTION_MENU_RESULT_KEY_CONFIG:
            g_game_settings.button_mappings[0] =
                g_game_settings.committed_button_mappings[0];
            g_game_settings.button_mappings[1] =
                g_game_settings.committed_button_mappings[1];
            g_game_settings.button_mappings[2] =
                g_game_settings.committed_button_mappings[2];
            g_menu_options_step = OPTIONS_STEP_KEY_CONFIG_OPEN;
            break;
        }
        break;
    case OPTIONS_STEP_CARD_MENU:
        result = 0;
        page_result = menu_UpdateCardMenu(held, pressed);
        if (page_result == MEMCARD_MENU_RESULT_BACK) {
            g_menu_options_step = OPTIONS_STEP_MENU;
        } else if (page_result == MEMCARD_MENU_RESULT_LOAD) {
            /* OPTIONS_STEP_LOAD */
            g_menu_options_step = page_result;
        } else if (page_result == MEMCARD_MENU_RESULT_SAVE) {
            /* OPTIONS_STEP_SAVE */
            g_menu_options_step = page_result;
        }
        break;
    case OPTIONS_STEP_LOAD:
        result = MENU_OPTIONS_LOAD;
        break;
    case OPTIONS_STEP_SAVE:
        result = MENU_OPTIONS_SAVE;
        break;
    case OPTIONS_STEP_KEY_CONFIG_OPEN:
        menu_ResetKeyConfig();
        g_menu_options_step++;
        /* fall through */
    case OPTIONS_STEP_KEY_CONFIG:
        result = 0;
        page_result = menu_UpdateKeyConfig(held, pressed);
        if (page_result == PAGE_CONFIRMED) {
            g_menu_options_step = OPTIONS_STEP_MENU;
            g_game_settings.committed_button_mappings[0] =
                g_game_settings.button_mappings[0];
            g_game_settings.committed_button_mappings[1] =
                g_game_settings.button_mappings[1];
            g_game_settings.committed_button_mappings[2] =
                g_game_settings.button_mappings[2];
        } else if (page_result == PAGE_CANCELED) {
            g_menu_options_step = OPTIONS_STEP_MENU;
        }
        break;
    }
    return result;
}

/* The settings the menus edit. */
GameSettings *menu_GetSettings(void) { return &g_game_settings; }
