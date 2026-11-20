#ifndef PORT_TOP_MENU_H
#define PORT_TOP_MENU_H

/* ReBushido's top menu on the native port (docs/PLAYING.md, "The top
 * menu"; docs/PORT.md, "ReBushido's top menu"): the window's `menu =
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
    u32 session_left;
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
