#ifndef MENU_H
#define MENU_H

/* The main menu: the screen of the main loop (menu_screen.c), its pages
 * (menu_pages.c: the top menu, the options, the memory card menu, the key
 * configuration, the first pages of training and chanbara, and the message
 * box the memory-card screens write in), the debug menu (fight_start.c) and
 * the settings the menus hand to the fight. Character select is select.h. */

#include "common.h"
#include "sprite.h"
#include "hud.h"

/* ---- the settings ---- */

/* What one side takes into one round: the fighter (an index into the
 * fighters' data, as character select counts them) and the weapon. */
typedef struct {
    u8 fighter;
    u8 weapon;
} RoundPick;

/* One best time as the menus show it, and who set it. */
typedef struct {
    u8 minutes;
    u8 seconds;
    u8 hundredths;
    u8 character_index;
} BestTime;

/* Everything the menus decide and the fight reads: who fights with what in
 * each round, the mode and stage, the match rules, the button mappings, the
 * options and the best times. There is one (g_game_settings); the lasting
 * parts go to the memory card (SaveRecord).
 * menu_mode is the top menu's row; match_count_index the round count - 3;
 * win_target_index the points that win a match - 1; team_clan_bits each
 * side's clan in team battle, a bit a side. practice_round is which of training
 * mode's two kinds was picked: "Practice Round", against a chosen opponent, and
 * not "Practice", against the ninja. A button mapping is one byte per action, a
 * bit per button, the first player's in the low four bits: the set being
 * edited, the defaults and the set in use. pad14_21 and pad20_4 are the bits
 * left over, which no code reads. */
typedef struct {
    RoundPick match_picks[2][5];
    u32 menu_mode : 4;
    u32 stage_index : 6;
    u32 match_count_index : 2;
    u32 time_limit_index : 2;
    u32 win_target_index : 1;
    u32 team_clan_bits : 2;
    u32 practice_round : 1;
    u32 toggle_option_bits : 3;
    u32 pad14_21 : 3;
    u8 button_mappings[3];
    u8 default_button_mappings[3];
    u8 committed_button_mappings[3];
    u32 grayscale : 1;    /* the "Monitor" option: black and white */
    u32 easy_cpu : 1;     /* the "Difficulty" option */
    u32 music_off : 1;    /* the "Music" option */
    u32 first_person : 1; /* the top menu's view: "POV", not "Side View" */
    u32 pad20_4 : 4;
    BestTime time_records[3];
    u8 fight_time[3];
    u8 fight_character_index;
} GameSettings;

/* The actions a button can be given, and the buttons, in the order of a
 * mapping's bits. */
#define MENU_ACTIONS 3
#define MENU_BUTTONS 4

extern GameSettings g_game_settings;
extern GameSettings *menu_GetSettings(void);

/* ---- the screen (menu_screen.c) ---- */

/* Set before going to SCREEN_MENU_INIT to come back not at the top of the
 * main menu but at the current mode's own page (character select for most
 * modes): "back" from a fight, as against "quit". */
extern u8 g_menu_reopen_mode;

/* The screens (enum Screen) of the main menu, of the save, and of the
 * debug menu (fight_modes.c, fight_start.c). */
extern void menu_Init(void);
extern void menu_Update(void);
extern void menu_SaveInit(void);
extern void menu_SaveUpdate(void);
extern void menu_DebugInit(void);
extern void menu_DebugUpdate(void);

/* ---- the pages' resource file ---- */

/* The sheets of chanbara's page: the table of best times, then the title
 * and the two answers. */
typedef struct {
    BestTimesSheets best_times;
    SpriteSheetSlot choice_sheets[3];
} MenuChanbaraSheets;

/* MOD.BIN, the main menu's resource file. After the head come twelve lists
 * (menu_RelocateFile), all of sprite sheets but `chanbara`:
 * - white_page_sheets: the modes' and pages' names in English on one page;
 * - row_sheets: the eight rows of the top menu, in Japanese;
 * - top_sheets: the top menu's furniture (enum MenuTopSheet, menu_pages.c);
 * - message_sheets: two sheets a message (the second -1 where one will do),
 *   by enum MenuMessage;
 * - mode_picture_sheets: for each row of the top menu a picture drawn as
 *   quads, then eight more drawn as sprites over it;
 * - option_sheets: the options page: its title, the six rows' names, then
 *   the two values of each of the three options;
 * - card_menu_sheets: the memory card menu: its title and three rows;
 * - key_config_sheets: the key configuration page (enum KeyConfigSheet);
 * - frame_sheets: three sheets every page has behind it;
 * - practice_sheets: training mode's page: its title, three rows and the
 *   two kinds' descriptions;
 * - mode_caption_sheets: each top-menu row's name in English.
 * key_config_rects are the coloured bars of the key configuration page,
 * as they are for the first player (the frame round a cursor's row is drawn
 * from the row's bar); message_sizes how big a box each message needs. */
typedef struct {
    SpriteFileHead head;
    SpriteListSlot white_page_sheets;
    SpriteListSlot row_sheets;
    SpriteListSlot top_sheets;
    SpriteListSlot message_sheets;
    SpriteListSlot mode_picture_sheets;
    SpriteListSlot option_sheets;
    SpriteListSlot card_menu_sheets;
    SpriteListSlot key_config_sheets;
    SpriteListSlot frame_sheets;
    SpriteListSlot practice_sheets;
    PTR32(MenuChanbaraSheets *) chanbara;
    SpriteListSlot mode_caption_sheets;
    PTR32(ColorRect *) key_config_rects;
    PTR32(ScreenSize *) message_sizes;
} MenuFile;

/* What the pages' draw functions are handed each frame: the file and where
 * the next primitive of each kind goes. It is a MenuDrawCtx (select.h) one
 * word further on; the first word is not used. */
typedef struct {
    s32 unused;
    MenuFile *file;
    PrimCursors prims;
} MenuCtx;

/* ---- the pages (menu_pages.c) ---- */

/* Which rows of the top menu can be chosen: a bit for each mode (enum
 * FightMode) and bit MENU_ROW_OPTIONS for the options. */
extern void menu_SetEnabledRows(u32 modes);
/* Loads MOD.BIN and its sounds to `memory` and sets the top menu up. The
 * pages edit `settings`. */
extern s32 menu_Load(s32 memory, GameSettings *settings);

/* A page's update draws the page and reads the pad (both pads' buttons, the
 * second pad's in the high half); what it returns is the page's own, or,
 * for the pages that are only chosen on or left, one of these. */
enum MenuPageResult {
    PAGE_CANCELED = -1, /* the page was left */
    PAGE_RUNNING = 0,
    PAGE_CONFIRMED = 1, /* what was to be chosen on it has been */
};

/* The top menu. Returns the mode of the row chosen (enum FightMode;
 * MENU_ROW_OPTIONS for the options), or one of these. */
#define MENU_TOP_RUNNING (-1)
#define MENU_TOP_CANCELED (-2)
#define MENU_ROW_OPTIONS 7
extern s32 menu_UpdateTop(s32 held, u32 pressed);
#if RB_PORT
/* The mode of the row the top menu's cursor is on (enum FightMode, or
 * MENU_ROW_OPTIONS). */
extern s32 menu_GetTopRowMode(void);
/* The main menu's page (menu_screen.c, enum MenuPage). */
extern u8 g_menu_page;
#endif

/* What menu_UpdateOptions returns. */
enum OptionMenuResult {
    OPTION_MENU_RESULT_NONE = 0,
    OPTION_MENU_RESULT_EXIT = 1,
    OPTION_MENU_RESULT_MEMCARD = 2,
    OPTION_MENU_RESULT_KEY_CONFIG = 3,
};

extern s32 menu_UpdateOptions(s32 held, u32 pressed);

/* What menu_UpdateCardMenu returns. */
enum MemcardMenuResult {
    MEMCARD_MENU_RESULT_NONE = 0,
    MEMCARD_MENU_RESULT_BACK = 1,
    MEMCARD_MENU_RESULT_LOAD = 2,
    MEMCARD_MENU_RESULT_SAVE = 3,
};

extern s32 menu_UpdateCardMenu(s32 held, s32 pressed);

/* The key configuration: PAGE_CONFIRMED when "OK" is chosen. */
extern void menu_ResetKeyConfig(void);
extern s32 menu_UpdateKeyConfig(s32 held, s32 pressed);
/* Training mode's page and chanbara's: PAGE_CONFIRMED to go on, PAGE_CANCELED
 * to go back. */
extern s32 menu_UpdatePractice(s32 held, s32 pressed);
extern s32 menu_ResetChanbara(void);
extern s32 menu_UpdateChanbara(s32 held, s32 pressed);
/* A page of the menu's text on white that nothing shows; PAGE_CONFIRMED when
 * the cross button is pressed. */
extern s32 menu_UpdateWhitePage(s32 held, s32 pressed);

/* The messages of the message box. */
enum MenuMessage {
    MSG_NONE = -1,
    MSG_CHECKING = 0,     /* "Checking Memory Card. Please wait..." */
    MSG_NO_SAVE_FILE = 1, /* "No Bushido Blade 2 data found on Memory Card" */
    MSG_LOADING = 2,
    MSG_LOAD_DONE = 3,
    MSG_LOAD_FAILED = 4,
    MSG_CARD_FULL = 5,     /* "Not enough empty block space to save" */
    MSG_ASK_OVERWRITE = 6, /* "Overwrite data? Previous data will be erased" */
    MSG_SAVING = 7,
    MSG_SAVE_DONE = 8,
    MSG_SAVE_FAILED = 9,
    MSG_NO_CARD = 10,    /* "Memory Card has been removed" */
    MSG_BAD_DATA = 11,   /* "This data is destroyed. Cannot load" */
    MSG_ASK_FORMAT = 12, /* "Memory Card is unformatted. Format?" */
    MSG_FORMATTING = 13,
    MSG_FORMAT_DONE = 14, /* not shown */
    MSG_FORMAT_FAILED = 15,
    MSG_ASK_SAVE_COPY = 16, /* the same words as the next; not shown */
    MSG_ASK_SAVE = 17,      /* "Save current data on Memory Card?" */
    MSG_ANSWERS = 18,       /* the answers under a question */
    MSG_ASK_LOAD = 19,
    MSG_UNFORMATTED = 20, /* "Memory Card is unformatted" */
};

/* Draws a message in a box in the middle of the screen. `answer` is -1, or
 * for a question the answer the cursor is on: 0 yes, 1 no. */
extern void menu_DrawMessageBox(s32 message, s32 answer);

/* What the other 2D screens draw as the main menu does. */
/* A page's see-through panel across the screen. */
#define MENU_PANEL_Y 48
#define MENU_PANEL_HEIGHT 176
#define MENU_PANEL_WIDE_X 63
#define MENU_PANEL_WIDE_WIDTH 514
/* The backdrop: a column down the screen between these two, and a slanted
 * piece each side of it: left of the column what is under a line that
 * rises to the column's top, right of it what is over a line that falls
 * from the column's foot. */
#define MENU_BACKDROP_COLUMN_LEFT 290
#define MENU_BACKDROP_COLUMN_RIGHT 350
#define MENU_BACKDROP_SLANT_LEFT_Y 185
#define MENU_BACKDROP_SLANT_RIGHT_Y 54
/* The height of the rule under a page's title. */
#define MENU_TITLE_RULE_Y 37

/* The line under a page's title: `rule` is its x, y, width and height. */
extern void menu_DrawTitleRule(MenuCtx *ctx, u16 *rule, s32 ot_index);
/* The colours of a page's panel and backdrop, which the option "Monitor"
 * turns gray. */
extern void menu_SetPanelColor(TILE *tile);
extern void menu_SetBackdropColor(POLY_F4 *quad);
/* Moves a cursor a frame: see menu_pages.c. What comes back says what a
 * finished nudge means, in the low byte for left and right and in the third
 * byte for up and down. */
#define NUDGE_PLUS 1 /* right, or down */
#define NUDGE_MINUS 2
#define NUDGE_DOWN_SHIFT 16
#define NUDGE_ACROSS(events) ((events) & 0xFF)
#define NUDGE_DOWN(events) ((events) >> NUDGE_DOWN_SHIFT)
extern s32 menu_StepNudge(u32 *pressed, s32 player, s16 *nudge, s16 *steps);
/* The table of best times, from `sheets`; row `rank` blinks (-1: none, and
 * no fourth row for the time just set). */
extern void menu_DrawBestTimes(
    s32 *sprt, s32 *mode, BestTimesSheets *sheets, s32 ot_index, s32 rank);

/* The picture shown while two consoles connect over the link cable. */
extern s32 menu_LoadLinkPicture(void);
extern s32 menu_ClearLinkPicture(void);

/* ---- the pages as the screen runs them (menu_run.c) ---- */

/* For each row of the top menu as the settings keep it: the mode it starts,
 * and 1 if the mode's character select is for two pads. */
extern u8 g_menu_row_modes[8][2];

extern s32 menu_Open(s32 memory);
/* enum MenuPageResult: confirmed when a row is chosen (menu_GetChosenMode
 * says which mode it stands for), canceled when the menu is left. */
extern s32 menu_RunTop(s32 held, s32 pressed);
extern s32 menu_GetChosenMode(void);

/* The options and the pages under it. Returns 0 while they are up. */
enum MenuOptionsResult {
    MENU_OPTIONS_LEFT = -1,
    MENU_OPTIONS_BACK = 1, /* never returned */
    MENU_OPTIONS_LOAD = 2, /* "Load" was chosen in the memory card menu */
    MENU_OPTIONS_SAVE = 3,
};

extern s32 menu_RunOptions(s32 held, s32 pressed);
/* After a load or a save: back to the memory card menu. */
extern void menu_ReturnToCardMenu(void);
extern s32 menu_RunPractice(s32 held, s32 pressed);
extern void menu_OpenChanbara(void);
extern s32 menu_RunChanbara(s32 held, s32 pressed);
extern void menu_ShowLinkPicture(void);
extern void menu_HideLinkPicture(void);

/* ---- the debug menu (fight_start.c) ---- */

/* The "M" row's number, 0 to 3. Nothing else reads it. */
extern u8 g_menu_debug_m_option;

#endif /* MENU_H */
