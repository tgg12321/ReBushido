/* ReBushido's top menu on the native port (port_top_menu.h): its rows,
 * where its cursor is, the boot without the options page and the game's
 * options taken from the run's settings. A state file: what is here is
 * saved, put back and hashed with the game's own variables. */
#include "common.h"
#include "bb2.h"

/* The game's rows of its top menu (menu_pages.c, g_menu_top), as the
 * screen has them. */
#define GAME_ROW_STORY 0
#define GAME_ROW_VSCPU 1
#define GAME_ROW_PRACTICE 2
#define GAME_ROW_VS 3
#define GAME_ROW_TEAM 4
#define GAME_ROW_LINK 5
#define GAME_ROW_CHANBARA 6
#define GAME_ROW_OPTIONS 7
/* the game's MENU_VIEW_SIDE */
#define SIDE 1

/* The three of the game's options the save keeps in its flags. */
#define GAME_OPTION_FLAGS                                                      \
    (SAVE_FLAG_GRAYSCALE | SAVE_FLAG_EASY_CPU | SAVE_FLAG_MUSIC_OFF)

const PortTopRow g_port_top_rows[PORT_TOP_PAGES][PORT_TOP_ROWS] = {
    {
        {"Singleplayer",
         {"Singleplayer", "Story, Vs. CPU, Slash and Group Battle"},
         PORT_TOP_SINGLE,
         GAME_ROW_STORY,
         SIDE},
        {"Versus", {NULL, NULL}, PORT_TOP_MODE, GAME_ROW_VS, SIDE},
        {"Training", {NULL, NULL}, PORT_TOP_MODE, GAME_ROW_PRACTICE, SIDE},
        {"Online Play",
         {"Online Play", "Host or join a game with another PC"},
         PORT_TOP_ONLINE,
         GAME_ROW_LINK,
         0},
        {"Options", {NULL, NULL}, PORT_TOP_OPTIONS, GAME_ROW_OPTIONS, 0},
    },
    {
        {"Story", {NULL, NULL}, PORT_TOP_MODE, GAME_ROW_STORY, SIDE},
        {"Vs. CPU", {NULL, NULL}, PORT_TOP_MODE, GAME_ROW_VSCPU, SIDE},
        {"Slash", {NULL, NULL}, PORT_TOP_MODE, GAME_ROW_CHANBARA, SIDE},
        {"Group Battle", {NULL, NULL}, PORT_TOP_MODE, GAME_ROW_TEAM, SIDE},
        {"Back", {"Back", "To the top menu"}, PORT_TOP_BACK, GAME_ROW_STORY, 0},
    },
};

PortTopMenu g_port_top_menu;

/* The switch taken in at boot; with it, the options page's settings set as
 * the page would have left them, and the page not shown. */
s32 port_TopMenuBoot(void) {
    g_port_top_menu.is_on = (u8)(port_GetTopMenuSwitch() != 0);
    g_port_top_menu.page = PORT_TOP_PAGE_TOP;
    if (!g_port_top_menu.is_on) {
        return FALSE;
    }
#if RB_OPTIONS_PAGE
    /* what the page would have set, and where it would have left the
     * game: Graphics is the window's own, so the game's stays the
     * retail picture */
    g_rb_settings.bugfixes = (u8)(port_GetBugfixesSwitch() != 0);
    g_rb_settings.graphics = RB_GRAPHICS_ORIGINAL;
    g_rb_page_state = RB_PAGE_DONE;
#endif
    return TRUE;
}

s32 port_TopMenuIsOn(void) { return g_port_top_menu.is_on; }

void port_TopMenuReset(void) { g_port_top_menu.page = PORT_TOP_PAGE_TOP; }

s32 port_TopMenuCursor(void) {
    return g_port_top_menu.cursor[g_port_top_menu.page];
}

const PortTopRow *port_TopMenuRow(void) {
    return &g_port_top_rows[g_port_top_menu.page][port_TopMenuCursor()];
}

s32 port_TopMenuRowIsOn(s32 row, u32 rows_on) {
    const PortTopRow *of = &g_port_top_rows[g_port_top_menu.page][row];

    return of->kind != PORT_TOP_MODE || ((rows_on >> of->row) & 1);
}

/* The cursor on to the next row that can be chosen, round at the ends. */
void port_TopMenuMove(s32 step, u32 rows_on) {
    s32 row = port_TopMenuCursor();
    s32 i;

    for (i = 0; i < PORT_TOP_ROWS; i++) {
        row = (row + step + PORT_TOP_ROWS) % PORT_TOP_ROWS;
        if (port_TopMenuRowIsOn(row, rows_on)) {
            g_port_top_menu.cursor[g_port_top_menu.page] = (u8)row;
            return;
        }
    }
}

void port_TopMenuOpenPage(s32 page) { g_port_top_menu.page = (u8)page; }

void port_TopMenuAsk(s32 page) {
    g_port_top_menu.asks++;
    g_port_top_menu.ask_page = (u8)page;
}

void port_TopMenuTakeOptions(void) {
    u32 flags = port_GetGameOptions() & GAME_OPTION_FLAGS;

    g_game_settings.grayscale = (flags & SAVE_FLAG_GRAYSCALE) != 0;
    g_game_settings.easy_cpu = (flags & SAVE_FLAG_EASY_CPU) != 0;
    g_game_settings.music_off = (flags & SAVE_FLAG_MUSIC_OFF) != 0;
    g_game_settings.first_person = 0;
    g_saved_record.flags =
        (u8)((g_saved_record.flags & ~GAME_OPTION_FLAGS) | flags);
}
