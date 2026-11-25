/* The native port's window: what the session's log says of the game
 * (window.h; docs/PORT.md, "The window"). The window's SDL files cannot
 * see the game's variables (they are compiled with a C library's
 * headers, and the game's names are the game's object's own), so this
 * file, which is compiled as the port's own are, reads them out for
 * those files and has the names the log gives things, and what the
 * window lays over ReBushido's top menu (port_ReadTopMenu). It only
 * reads, but for the settings menu's Bug fixes (port_SetBugfixes). */
#include "common.h"
#include "port_replay.h"
#include "window.h"
#include "bb2.h"

#define NAME_COUNT(names) ((s32)(sizeof(names) / sizeof((names)[0])))

/* The main menu's page (src/main/menu_screen.c, enum MenuPage), which no
 * header has: the settings menu opens on Select at the top page. */
extern u8 g_menu_page;
#define MENU_PAGE_TOP 1
#define MENU_PAGE_SELECT 3

/* enum Screen (sys.h). */
static const char * const g_port_screen_names[] = {
    "fight (loading)",
    "fight",
    "debug menu (set-up)",
    "debug menu",
    "replay (set-up)",
    "replay",
    "cutscene (set-up)",
    "cutscene",
    "menu (set-up)",
    "menu",
    "staff roll (set-up)",
    "staff roll",
    "after fight (set-up)",
    "after fight",
    "unused",
    "movie",
    "winner (set-up)",
    "winner",
    "ending (set-up)",
    "ending",
    "team result (set-up)",
    "team result",
    "stage card (set-up)",
    "stage card",
    "after match (set-up)",
    "after match",
    "save (set-up)",
    "save",
    "kaishaku (set-up)",
    "kaishaku",
    "best times (set-up)",
    "best times",
    "kill count (set-up)",
    "kill count",
};

/* enum FightMode (fight.h). */
static const char * const g_port_mode_names[] = {
    "story", "vs CPU", "practice", "chanbara", "vs", "team", "network",
};

/* The roster (fighter.h). */
static const char * const g_port_roster_names[] = {
    "Kannuki (story)",
    "Tatsumi (story)",
    "Mikado (story)",
    "Kannuki",
    "Tatsumi",
    "Mikado",
    "Utsusemi",
    "Red Shadow",
    "Tsubame",
    "Sazanka",
    "Suminagashi",
    "Nightstalker",
    "Matsumushi",
    "the vanisher",
    "sidearm 1",
    "Gengoro (story)",
    "Kaun (story)",
    "Jo (story)",
    "Gengoro",
    "Kaun",
    "Jo",
    "Hongou",
    "Katze",
    "Highwayman",
    "Chihiro",
    "Utamaru",
    "Tony Umeda",
    "Isohachi",
    "the armored one",
    "sidearm 2",
    "last 1",
    "the victim",
    "last 2",
};

/* enum WeaponId (weapon.h). */
static const char * const g_port_weapon_names[] = {
    "katana",   "long sword", "nodachi",  "broadsword",
    "naginata", "yari",       "long gun", "short gun",
};

/* enum StageIndex (stage.h). */
static const char * const g_port_stage_names[] = {
    "Main Tatara Shrine",   "Meikyokan Dojo",     "Shainto HQ",
    "Tatara Shrine",        "Bus Stop",           "Lost Bridge",
    "Tatara Shrine Path",   "Bamboo Thicket",     "Outside the Meikyokan",
    "Future Dump Site",     "Snake Cliff",        "Kimigaura Coast",
    "Orange Tree Fields",   "Castle Rooftop",     "Under Lost Bridge",
    "Terraced Fields",      "Under Railway",      "Water Tower Dam",
    "Lumberyard",           "Lighthouse",         "Kabuki Stage",
    "Abandoned Ship",       "Ferry Dock",         "Shainto Gate",
    "Purple Cavern",        "Abandoned Station",  "Railway",
    "Abandoned Tunnel",     "Abandoned Shipyard", "Building Rooftop",
    "Building Parking Lot", "Shainto Cemetery",   "team battle 1",
    "team battle 2",        "chanbara's hall",    "practice 1",
    "practice 2",
};

static const char *port_NameOf(
    const char * const *names, s32 count, s32 index) {
    return index >= 0 && index < count ? names[index] : "?";
}

const char *port_NameScreen(s32 screen) {
    return port_NameOf(
        g_port_screen_names, NAME_COUNT(g_port_screen_names), screen);
}

const char *port_NameMode(s32 mode) {
    return port_NameOf(g_port_mode_names, NAME_COUNT(g_port_mode_names), mode);
}

const char *port_NameRoster(s32 roster) {
    return port_NameOf(
        g_port_roster_names, NAME_COUNT(g_port_roster_names), roster);
}

const char *port_NameWeapon(s32 weapon) {
    return port_NameOf(
        g_port_weapon_names, NAME_COUNT(g_port_weapon_names), weapon);
}

const char *port_NameStage(s32 stage) {
    return port_NameOf(
        g_port_stage_names, NAME_COUNT(g_port_stage_names), stage);
}

/* `ReBushido VERSION (COMMIT)`, as the options page has it. */
const char *port_GetVersionText(void) { return g_rb_version; }

/* The game as it is now (window.h). */
void port_ReadGameState(PortGameState *state) {
    s32 i;

    state->screen = g_sys_screen;
    state->in_fight = g_sys_screen == SCREEN_FIGHT_UPDATE;
    state->frames = g_disp_frame_count;
    state->mode = g_fight_mode;
    state->stage = g_fight_stage;
    state->winner = g_fight_winner;
    state->defeat_frames = g_fight_defeat_frames;
    for (i = 0; i < 2; i++) {
        state->roster[i] = g_fighters[i].roster_id;
        state->weapon[i] = g_fighters[i].weapon_id;
        state->is_cpu[i] = g_fighters[i].is_cpu;
    }
    state->is_top_menu =
        g_sys_screen == SCREEN_MENU_UPDATE && g_menu_page == MENU_PAGE_TOP;
#if RB_OPTIONS_PAGE
    /* (the port has no quick-boot build, whose options page is none) */
    state->is_options_page = g_rb_page_state == RB_PAGE_UP;
#else
    state->is_options_page = FALSE;
#endif
    state->is_paused = port_RemasterIsPaused();
    state->is_cutscene = g_sys_screen == SCREEN_CUTSCENE_UPDATE;
    state->is_top_menu_ours = g_port_top_menu.is_on;
    state->top_page = g_port_top_menu.page;
    state->top_cursor = g_port_top_menu.cursor[g_port_top_menu.page];
    state->menu_asks = g_port_top_menu.asks;
    state->menu_ask_page = g_port_top_menu.ask_page;
}

void port_ReadTopMenu(PortTopView *view) {
    const PortTopRow *rows = g_port_top_rows[g_port_top_menu.page];
    /* read from the state the step left, never by calling the game's
     * code, which is the step's alone while it runs */
    u32 rows_on = g_port_top_menu.rows_on;
    s32 i;

    view->is_up = g_port_top_menu.is_on && g_sys_screen == SCREEN_MENU_UPDATE &&
                  g_menu_page == MENU_PAGE_TOP && !g_port_top_menu.covered;
    view->page = g_port_top_menu.page;
    view->cursor = g_port_top_menu.cursor[g_port_top_menu.page];
    for (i = 0; i < PORT_TOP_ROWS; i++) {
        view->names[i] = rows[i].name;
        view->is_on[i] =
            rows[i].kind != PORT_TOP_MODE || ((rows_on >> rows[i].row) & 1);
    }
    view->caption[0] = rows[view->cursor].caption[0];
    view->caption[1] = rows[view->cursor].caption[1];
}

void port_ReadPanelRows(PortPanelRowsView *view) {
    /* drawn in this frame or the one before: the pause's loop and the
     * menu after a fight draw them every frame they are up */
    view->is_up = g_port_top_menu.is_on &&
                  (u32)g_disp_frame_count - g_port_top_menu.panel_frame <= 2;
    view->cursor = g_port_top_menu.panel_cursor;
    view->y[0] = g_port_top_menu.panel_y[0];
    view->y[1] = g_port_top_menu.panel_y[1];
}

void port_ReadPageLines(PortPageLinesView *view) {
    s32 i;

    for (i = 0; i < PORT_PAGE_LINES; i++) {
        view->is_up[i] =
            g_port_top_menu.is_on &&
            (u32)g_disp_frame_count - g_port_top_menu.line_frame[i] <= 2;
        view->place[i][0] = g_port_top_menu.line_place[i][0];
        view->place[i][1] = g_port_top_menu.line_place[i][1];
        view->place[i][2] = g_port_top_menu.line_place[i][2];
        view->is_lit[i] = g_port_top_menu.line_lit[i];
    }
}

s32 port_IsOnStagePages(void) {
    return g_port_stage_choice.on_stages &&
           g_sys_screen == SCREEN_MENU_UPDATE &&
           g_menu_page == MENU_PAGE_SELECT;
}

s32 port_MatchShown(void) {
    const PortMatch *match = &g_port_match;

    if (g_sys_screen == SCREEN_MENU_UPDATE && g_menu_page == MENU_PAGE_SELECT) {
        if (match->rule_page) {
            /* the rule's page, before the grid */
            return PORT_MATCH_SHOWN_RULE;
        }
        /* a Crew match's later places being picked, or two on two's
         * partners (or, the rule chosen before the grid, every place) */
        return (match->page_rule == PORT_MATCH_CREW ||
                match->page_teams != PORT_TEAMS_ONE) &&
                       match->crew_size != 0 &&
                       (match->crew_picked > 0 || match->rule_first) &&
                       match->crew_picked < match->crew_size
                   ? PORT_MATCH_SHOWN_PICK
                   : PORT_MATCH_SHOWN_NONE;
    }
    if (match->rule == PORT_MATCH_SINGLE || match->mode != g_fight_mode) {
        return PORT_MATCH_SHOWN_NONE;
    }
    if (match->card != PORT_MATCH_CARD_NONE &&
        (g_sys_screen == SCREEN_AFTER_FIGHT_UPDATE ||
         g_sys_screen == SCREEN_AFTER_MATCH_UPDATE)) {
        return PORT_MATCH_SHOWN_CARD;
    }
    return g_sys_screen == SCREEN_FIGHT_UPDATE ? PORT_MATCH_SHOWN_HUD
                                               : PORT_MATCH_SHOWN_NONE;
}

s32 port_MatchCrewMarks(s32 row, s32 col) {
    s32 fighter;
    s32 marks = 0;
    s32 side;

    if (row < 0 || row >= SELECT_GRID_ROWS || col < 0 ||
        col >= SELECT_GRID_COLS) {
        return 0;
    }
    fighter = g_select_grid[row][col].fighter;
    for (side = 0; side < g_port_match.crew_sides && side < 2; side++) {
        if (port_MatchCrewHas(side, fighter)) {
            marks |= 1 << side;
        }
    }
    return marks;
}

const char *port_MatchCrewName(s32 fighter) {
    return fighter >= 0 && fighter <= CHARACTER_COUNT
               ? port_NameRoster(g_fighter_character_rosters[fighter])
               : "?";
}

s32 port_ReadScreen(void) {
    if (g_sys_screen == SCREEN_FIGHT_UPDATE) {
        return 0;
    }
    return g_disp_loading_screen == LOADING_SCREEN_STAGE_CARD ||
                   g_disp_loading_screen == LOADING_SCREEN_TEAM_SCORES
               ? g_disp_loading_screen
               : 0;
}

s32 port_LoadKind(void) {
    if (g_port_load_wait <= 0) {
        return PORT_LOAD_NONE;
    }
    return port_ReadScreen() != 0 ? PORT_LOAD_READ : PORT_LOAD_PICTURE;
}

/* A page of the window's over ReBushido's top menu (port_top_menu.h):
 * written at a blank's turn, where the game is still, as the Bug fixes
 * below. */
void port_SetTopMenuCovered(s32 on) { g_port_top_menu.covered = (u8)(on != 0); }

/* The game's own Bug fixes setting, for the settings menu (menu.h). The
 * one thing here that writes the game: the menu sets it while the game is
 * stopped at a frame's boundary, as the options page at boot sets it
 * before the first. */
s32 port_GetBugfixes(void) { return g_rb_settings.bugfixes; }

void port_SetBugfixes(s32 on) {
    if (g_rb_settings.bugfixes != (u8)(on != 0)) {
        /* no input made the change: a fight being recorded cannot be
         * played back the same */
        port_ReplayAbandon("Bug fixes was changed in the settings menu");
    }
    g_rb_settings.bugfixes = (u8)(on != 0);
}
