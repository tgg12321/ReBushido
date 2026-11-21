#ifndef PORT_TOP_MENU_H
#define PORT_TOP_MENU_H

/* ReBushido's top menu on the native port (docs/PLAYING.md, "The main
 * menu"; docs/PORT.md, "ReBushido's main menu"): the window's `menu =
 * rebushido`, which a player's program has unless the settings say
 * `original`. With it:
 *
 * - the options page of ReBushido's disc (rb_options.h) is not shown at
 *   boot: its settings are the window's (`bugfixes`), and the game goes
 *   straight to its opening;
 * - the game's top menu has five rows in place of its eight: Singleplayer,
 *   a page of its own with Story, Vs. CPU, Slash and Group Battle;
 *   Versus; Training; Online Play, the settings menu's Play online page;
 *   and Options, the settings menu, in place of the game's own options
 *   page and its memory card and key configuration;
 * - the view is always the side view: no row offers the first person's
 *   (POV), and no fight is played in it;
 * - the game's options (Difficulty, Music, Monitor) are the window's
 *   settings, written into the game's own record each frame of the menu;
 * - a save on the memory card is loaded at boot without asking.
 *
 * The game draws the page as its own (the panel, the title, the lines
 * under the rows, the picture and the caption of the mode under the
 * cursor); the rows' names, and the captions of the three rows the game
 * has no words for, are laid over the picture by the window
 * (src/port/window/top_page.c). Where the switch is off -- the program
 * without a window, every run of a play test's plan, `menu = original`
 * -- the game is the original's, the options page included.
 *
 * What the run asks for is kept outside the state (play_switches.c); the
 * game takes it in at boot into g_port_top_menu, which is the game's
 * state (src/port/top_menu.c), as are the page and the cursor. In a
 * session of netplay the host's switches hold. The PlayStation builds
 * never see this header. */

#include "common.h"

/* The two pages, and what a row of one does. */
enum {
    PORT_TOP_PAGE_TOP = 0,
    PORT_TOP_PAGE_SINGLE = 1, /* Singleplayer */
    PORT_TOP_PAGES = 2,
};

enum {
    PORT_TOP_MODE = 0,    /* a mode: the game's own row `row` is chosen */
    PORT_TOP_SINGLE = 1,  /* on to the Singleplayer page */
    PORT_TOP_BACK = 2,    /* back to the top page */
    PORT_TOP_ONLINE = 3,  /* the window's online pages */
    PORT_TOP_OPTIONS = 4, /* the settings menu */
};

/* Rows a page: four in the places of the game's first four, and the last
 * in the place of the game's last (its options). */
#define PORT_TOP_ROWS 5
#define PORT_TOP_LAST_ROW 4

/* A row: its name, what it does, the game's row whose picture and caption
 * it shows (and which a mode chooses), the views drawn beside it (the
 * game's MENU_VIEW_SIDE, or none), and, for a row the game has no caption
 * for, the two lines the window shows in place of one. */
typedef struct {
    const char *name;
    const char *caption[2];
    u8 kind;
    u8 row;
    u8 views;
} PortTopRow;

extern const PortTopRow g_port_top_rows[PORT_TOP_PAGES][PORT_TOP_ROWS];

/* The game's lines of other pages that ReBushido's menus name anew. */
enum {
    PORT_TOP_LINE_TRAINING = 0, /* the training page's Return to Top Menu */
    PORT_TOP_LINE_GROUP = 1,    /* the group battle page's */
    PORT_TOP_LINES = 2,
};

/* What the settings menu is asked to open on. */
enum {
    PORT_TOP_ASK_OPTIONS = 0,
    PORT_TOP_ASK_ONLINE = 1,
};

typedef struct {
    u8 is_on; /* the run's switch, as the game took it in at boot */
    u8 page;
    u8 cursor[PORT_TOP_PAGES];
    /* the settings menu asked for: a count that only goes forward, which
     * the window follows, and the page it is to open on */
    u8 asks;
    u8 ask_page;
    /* the game's rows that can be chosen as the step last drew the page,
     * for the window to read */
    u8 rows_on;
    /* a session's (session.c, port_SessionInput): the game has been at
     * the select screen, and the frame and one where it came back to its
     * top menu after that, which ends a session whose rule says so (0:
     * not yet). In the state, so that a rollback takes it back. */
    u8 session_away;
    /* a room's match (PORT_SESSION_RULE_ONE_MATCH): who won it, 1 or 2
     * for the first or second pad, 3 for nobody; 0 while it goes on */
    u8 session_result;
    /* a page of the window's (the settings menu, the online pages) is up
     * over the menu: the game draws its backdrop and wide panel alone,
     * takes no press and does not count the time to its movie
     * (port_TopMenuSetCovered) */
    u8 covered;
    u32 session_left;
    /* The pause's and the menu after a fight's two last rows, which
     * ReBushido's menus name "Return to Character Select" and "Return to
     * Main Menu" where the game's lettering says "Return to Sub Menu" and
     * "Return to Top Menu": the game leaves them out (hud.c) and the
     * window letters them (top_page.c). The frame they were last drawn
     * in (g_disp_frame_count), the panel's kind (HUD_PANEL_...), the row
     * under the cursor and the screen's y of the top of each row. */
    u32 panel_frame;
    u8 panel_kind;
    u8 panel_cursor;
    s16 panel_y[2];
    /* ... and the training page's and the group battle page's "Return to
     * Top Menu", which they name "Return to Main Menu" the same way
     * (PORT_TOP_LINE_...): the frame each was last drawn in, its place
     * (the left, the top and the width of the game's sheet) and whether
     * it is lit. */
    u32 line_frame[PORT_TOP_LINES];
    s16 line_place[PORT_TOP_LINES][3];
    u8 line_lit[PORT_TOP_LINES];
} PortTopMenu;

extern PortTopMenu g_port_top_menu;

/* ---- what the game calls (under RB_PORT) ---- */

/* At boot, before the options page would come up: the switch taken in.
 * TRUE if the page is not to be shown, its settings set from the run's
 * (main.c). */
extern s32 port_TopMenuBoot(void);
extern s32 port_TopMenuIsOn(void);
/* The main menu loads, not back from a fight to a mode's page: the top
 * page, its cursor where it was. */
extern void port_TopMenuReset(void);
/* The row under the cursor. */
extern const PortTopRow *port_TopMenuRow(void);
extern s32 port_TopMenuCursor(void);
/* The cursor a row on (`step` 1 or -1), round at the ends, over the rows
 * whose game row has its bit in `rows_on` (the game's g_menu_top). */
extern void port_TopMenuMove(s32 step, u32 rows_on);
/* Whether a row of the page is lit: one that can be chosen. */
extern s32 port_TopMenuRowIsOn(s32 row, u32 rows_on);
/* The page changed: `page`, its cursor where it was. */
extern void port_TopMenuOpenPage(s32 page);
/* The settings menu asked for (PORT_TOP_ASK_...). */
extern void port_TopMenuAsk(s32 page);
/* The game's options as the run's settings have them, into the menus'
 * record and the save's flags (menu_screen.c, menu_pages.c). */
extern void port_TopMenuTakeOptions(void);
/* Whether a page of the window's is up over the menu (`covered`). */
extern s32 port_TopMenuIsCovered(void);
/* A row of the pause's or the after-fight menu's that the window letters
 * (`row` 0, back to character select, or 1, to the main menu), drawn at
 * the screen's `y` this frame. */
extern void port_TopMenuNotePanelRow(s32 row, s32 y, s32 panel, s32 cursor);
/* A line of another page's (PORT_TOP_LINE_...) left out for the window to
 * letter: its sheet, drawn at (`x`, `y`) this frame, lit or not. */
extern void port_TopMenuNoteLine(
    s32 line, const void *sheet, s32 x, s32 y, s32 is_lit);

/* ---- the game's menu sounds, for the window's pages ---- */

/* The three sounds the game's menus make (menu_pages.c): the cursor
 * moved, a row taken, a way back. The window's pages (the settings
 * menu, the online pages) ask for them as the game's menus would play
 * them. */
enum {
    PORT_TOP_SOUND_CURSOR = 0,
    PORT_TOP_SOUND_CONFIRM = 1,
    PORT_TOP_SOUND_CANCEL = 2,
    PORT_TOP_SOUNDS = 3,
};

/* One played, as the game's menus play it (snd_Play: it sounds at the
 * frame's flush of the queue). */
extern void port_TopMenuPlaySound(s32 sound);
/* The sounds the window asked for since the game last took its input:
 * asked outside the state (play_switches.c), and played by the game as
 * it takes its next input (pad.c, port_TakeInput), so that a replay
 * keeps them with that input (port_replay.h). -1 when there are none
 * left. */
extern void port_AskMenuSound(s32 sound);
extern s32 port_TakeMenuSound(void);

/* ---- the run's switches, outside the state (play_switches.c) ---- */

/* The window's `menu`, `bugfixes` and the game's three options (the
 * save's SAVE_FLAG_... bits); `--menu` on the command line. */
extern void port_SetTopMenuSwitch(s32 on);
extern s32 port_GetTopMenuSwitch(void);
extern void port_SetBugfixesSwitch(s32 on);
extern s32 port_GetBugfixesSwitch(void);
extern void port_SetGameOptions(u32 flags);
extern u32 port_GetGameOptions(void);

#endif /* PORT_TOP_MENU_H */
