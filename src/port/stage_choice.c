/* The kinds of stage the players choose on the stage pages (ROADMAP 16;
 * port_stage_choice.h; docs/PORT.md, "The stage select's kinds of
 * stage"). A state file: what is here is saved, put back and hashed with
 * the game's own variables, so the choice is rolled back with the stage
 * the game keeps and is the same on both machines of a session.
 *
 * On a stage page Square switches the stage under the cursor between
 * Classic and Remastered Classic, L1 and R1 turn its time of day (its
 * own, dawn, day, dusk, night), and Select opens the arenas' page in
 * its place, a page of the stage select's own shape: six arenas on the
 * scroll, three rows of two, the d-pad among them as on the stages' grid
 * (past the page's side to the next page, as the grid's arrows go to the
 * next map), L1 and R1 a page back or on, Cross fights on the one under
 * the cursor (on the stage it stands in for, which the player is never
 * shown), Triangle or Select goes back to the stages; each press with the
 * game's own sound. Nothing here draws: the window lays the panel and the
 * arenas' names and pictures over the picture from what this file keeps
 * (src/port/window/stage_page.c), and the game's page leaves out its map
 * and the stages' names while the arenas' page is up
 * (port_StagePageCovers), so with it down video memory is the console's
 * and the play tests that hold the port's picture to the emulator's see
 * the screen they always did. */
#include "common.h"
#include "bb2.h"

PortStageChoice g_port_stage_choice;

/* The arenas' page is a page of the stage select's own shape: six places
 * on the scroll, three rows of two, an arena's place on its page being
 * row * 2 + column as a stage's is (port_stage_choice.h). */
#define PAGE_PLACES PORT_STAGE_PAGE_PLACES
#define PAGE_COLS 2
#define PAGE_ROWS (PAGE_PLACES / PAGE_COLS)

/* The modes whose fights come from a stage page: versus and link, and
 * versus the CPU and training when their select screen went on to the
 * stage pages (port_play.h). */
static s32 port_StageModeChooses(s32 mode) {
    return mode == FIGHT_MODE_VS || mode == FIGHT_MODE_NETWORK ||
           port_PlayStageChosen(mode);
}

/* Made the first time the game asks, and the default kind taken in when
 * the window's setting has changed it (port_SetStageKindDefault): every
 * stage starts again as the new default. */
static PortStageChoice *port_StageChoice(void) {
    PortStageChoice *choice = &g_port_stage_choice;
    s32 kind = port_GetStageKindDefault();
    s32 time = port_GetStageTimeDefault();
    s32 i;

    if (!choice->is_started) {
        choice->is_started = TRUE;
        choice->default_kind = (u8)(kind ^ 1); /* reset below */
        choice->fight_kind = (u8)kind;
        choice->arena = (s8)port_ArenaAsked();
        choice->loaded_arena = PORT_STAGE_NO_ARENA;
        choice->default_time = (u8)(time + 1); /* reset below */
        choice->fight_time = (u8)time;
    }
    if (choice->default_kind != kind) {
        choice->default_kind = (u8)kind;
        for (i = 0; i < STAGE_COUNT; i++) {
            choice->kinds[i] = (u8)kind;
        }
    }
    /* the stage pages start every stage at its own time, whatever the
     * setting (the owner, 2026-11-19); `time_of_day` is the time of the
     * modes with no stage page (fight_time above) */
    if (choice->default_time != time) {
        choice->default_time = (u8)time;
        for (i = 0; i < STAGE_COUNT; i++) {
            choice->times[i] = (u8)PORT_TIME_OWN;
        }
    }
    return choice;
}

s32 port_StageIsDressed(void) {
    const PortStageChoice *choice = &g_port_stage_choice;

    if (!choice->is_started) {
        return port_ArenaAsked() != PORT_STAGE_NO_ARENA ||
               port_GetStageKindDefault() == PORT_STAGE_REMASTERED;
    }
    return choice->arena != PORT_STAGE_NO_ARENA ||
           choice->fight_kind == PORT_STAGE_REMASTERED;
}

/* The time the picture lights the stage at (port_stage_choice.h): the
 * fight's, or photo mode's while it turns one. */
s32 port_StageTime(void) {
    const PortStageChoice *choice = &g_port_stage_choice;
    s32 time =
        choice->is_started ? choice->fight_time : port_GetStageTimeDefault();

    /* photo mode's time, for the picture alone (port_theatre.h): only the
     * picture asks this */
    if (port_TheatreTime() >= 0) {
        time = port_TheatreTime();
    }
    if (!port_StageIsDressed() || time >= PORT_TIME_COUNT) {
        return PORT_TIME_OWN;
    }
    return time;
}

s32 port_StageArena(void) {
    return g_port_stage_choice.is_started ? g_port_stage_choice.arena
                                          : port_ArenaAsked();
}

/* ---- a stage page ---- */

s32 port_StagePageCovers(void) {
    return g_port_stage_choice.page_open && port_ArenaCount() > 0;
}

/* The arena at `place` of page `page`, or the last where the page has
 * none there; the page's first is where it is shown from. */
static void port_StagePagePut(PortStageChoice *choice, s32 page, s32 place) {
    s32 count = port_ArenaCount();
    s32 at = page * PAGE_PLACES + place;

    choice->page_top = (u8)(page * PAGE_PLACES);
    choice->page_cursor = (u8)(at < count ? at : count - 1);
}

/* L1 and R1, or Left and Right past the page's edge: the page one back or
 * on (round from the last to the first and back), the cursor on the same
 * place of it or the page's last. */
static void port_StagePageTurn(PortStageChoice *choice, s32 way, s32 place) {
    s32 count = port_ArenaCount();
    s32 pages = (count + PAGE_PLACES - 1) / PAGE_PLACES;
    s32 page = (choice->page_cursor / PAGE_PLACES + way + pages) % pages;

    port_StagePagePut(choice, page, place);
}

/* Up and down: the row above or below in the same column, round the
 * page's three, past any the page has no arena in. */
static void port_StagePageRow(PortStageChoice *choice, s32 way) {
    s32 count = port_ArenaCount();
    s32 page = choice->page_cursor / PAGE_PLACES;
    s32 place = choice->page_cursor % PAGE_PLACES;
    s32 col = place % PAGE_COLS;
    s32 row = place / PAGE_COLS;
    s32 i;

    for (i = 0; i < PAGE_ROWS - 1; i++) {
        row = (row + way + PAGE_ROWS) % PAGE_ROWS;
        if (page * PAGE_PLACES + row * PAGE_COLS + col < count) {
            port_StagePagePut(choice, page, row * PAGE_COLS + col);
            return;
        }
    }
}

/* Left and right: the other column, as the stages' grid goes; past the
 * page's edge to the next page or the one before where there are more
 * than one, as the grid's arrows go to the next map. */
static void port_StagePageColumn(PortStageChoice *choice, s32 way) {
    s32 count = port_ArenaCount();
    s32 page = choice->page_cursor / PAGE_PLACES;
    s32 place = choice->page_cursor % PAGE_PLACES;
    s32 col = place % PAGE_COLS;
    s32 row = place / PAGE_COLS;
    s32 to = col + way;

    if (count > PAGE_PLACES && (to < 0 || to >= PAGE_COLS)) {
        port_StagePageTurn(choice, way, row * PAGE_COLS + (to < 0 ? 1 : 0));
        return;
    }
    to = (to + PAGE_COLS) % PAGE_COLS;
    if (page * PAGE_PLACES + row * PAGE_COLS + to < count) {
        port_StagePagePut(choice, page, row * PAGE_COLS + to);
    }
}

/* A frame of a stage page (port_stage_choice.h): Square and Select on the
 * grid, or the arenas' page, which has every press while it is up. */
s32 port_StagePageStep(s32 pressed, s32 *stage, s32 is_open) {
    PortStageChoice *choice = port_StageChoice();
    const PortArena *arena;

    choice->on_stages = TRUE;
    choice->cursor_stage =
        *stage >= 0 && *stage < STAGE_COUNT ? (u8)*stage : PORT_STAGE_NO_CURSOR;
    choice->cursor_kind = *stage >= 0 && *stage < STAGE_COUNT
                              ? choice->kinds[*stage]
                              : choice->default_kind;
    choice->cursor_time = *stage >= 0 && *stage < STAGE_COUNT
                              ? choice->times[*stage]
                              : choice->default_time;
    if (!choice->page_open) {
        if (!is_open) {
            return PORT_STAGE_PAGE_GRID;
        }
        if ((pressed & PAD_ANY(PADRleft)) && *stage >= 0 &&
            *stage < STAGE_COUNT) {
            snd_Play(MENU_SOUND_CURSOR, SND_VOLUME_MAX, SND_VOLUME_MAX);
            choice->kinds[*stage] ^= 1;
            choice->cursor_kind = choice->kinds[*stage];
            return PORT_STAGE_PAGE_TAKEN;
        }
        /* L1 and R1: the stage's time of day, back and on */
        if ((pressed & PAD_ANY(PADL1 | PADR1)) && *stage >= 0 &&
            *stage < STAGE_COUNT) {
            s32 step = (pressed & PAD_ANY(PADR1)) ? 1 : PORT_TIME_COUNT - 1;

            snd_Play(MENU_SOUND_CURSOR, SND_VOLUME_MAX, SND_VOLUME_MAX);
            choice->times[*stage] =
                (u8)((choice->times[*stage] + step) % PORT_TIME_COUNT);
            choice->cursor_time = choice->times[*stage];
            return PORT_STAGE_PAGE_TAKEN;
        }
        if ((pressed & PAD_ANY(PADselect)) && port_ArenaCount() > 0) {
            snd_Play(MENU_SOUND_CONFIRM, SND_VOLUME_MAX, SND_VOLUME_MAX);
            choice->page_open = TRUE;
            {
                s32 at = choice->arena >= 0 && choice->arena < port_ArenaCount()
                             ? choice->arena
                             : 0;

                port_StagePagePut(choice, at / PAGE_PLACES, at % PAGE_PLACES);
            }
            return PORT_STAGE_PAGE_TAKEN;
        }
        return PORT_STAGE_PAGE_GRID;
    }
    /* the arenas' page: it has every press while it is up */
    if (!is_open) {
        return PORT_STAGE_PAGE_TAKEN;
    }
    if (pressed & PAD_ANY(PADRup | PADselect)) {
        snd_Play(MENU_SOUND_CANCEL, SND_VOLUME_MAX, SND_VOLUME_MAX);
        choice->page_open = FALSE;
        return PORT_STAGE_PAGE_TAKEN;
    }
    if (pressed & PAD_ANY(PADLdown | PADLup)) {
        snd_Play(MENU_SOUND_CURSOR, SND_VOLUME_MAX, SND_VOLUME_MAX);
        port_StagePageRow(choice, (pressed & PAD_ANY(PADLdown)) ? 1 : -1);
    } else if (pressed & PAD_ANY(PADLright | PADLleft)) {
        snd_Play(MENU_SOUND_CURSOR, SND_VOLUME_MAX, SND_VOLUME_MAX);
        port_StagePageColumn(choice, (pressed & PAD_ANY(PADLright)) ? 1 : -1);
    } else if (
        (pressed & PAD_ANY(PADL1 | PADR1)) && port_ArenaCount() > PAGE_PLACES) {
        snd_Play(MENU_SOUND_CURSOR, SND_VOLUME_MAX, SND_VOLUME_MAX);
        port_StagePageTurn(choice, (pressed & PAD_ANY(PADR1)) ? 1 : -1,
                           choice->page_cursor % PAGE_PLACES);
    }
    arena = port_ArenaAt(choice->page_cursor);
    if ((pressed & PAD_ANY(PADRdown)) && arena != NULL) {
        snd_Play(MENU_SOUND_CONFIRM, SND_VOLUME_MAX, SND_VOLUME_MAX);
        choice->arena = (s8)choice->page_cursor;
        /* an arena brings its own dressing: the remastered picture's */
        choice->fight_kind = PORT_STAGE_REMASTERED;
        choice->fight_time = choice->default_time;
        choice->page_open = FALSE;
        *stage = arena->host;
        return PORT_STAGE_PAGE_ARENA;
    }
    return PORT_STAGE_PAGE_TAKEN;
}

/* The grid's Cross (port_stage_choice.h): the fight takes the stage's kind
 * and time of day as the page left them. */
void port_StageChosen(s32 stage) {
    PortStageChoice *choice = port_StageChoice();

    choice->fight_kind = stage >= 0 && stage < STAGE_COUNT
                             ? choice->kinds[stage]
                             : choice->default_kind;
    choice->fight_time = stage >= 0 && stage < STAGE_COUNT
                             ? choice->times[stage]
                             : choice->default_time;
    choice->arena = (s8)port_ArenaAsked();
    choice->page_open = FALSE;
}

void port_StageChoiceReset(void) {
    PortStageChoice *choice = port_StageChoice();

    choice->fight_kind = choice->default_kind;
    choice->fight_time = choice->default_time;
    choice->arena = (s8)port_ArenaAsked();
    choice->page_open = FALSE;
}

void port_StageLeftPages(void) { g_port_stage_choice.on_stages = FALSE; }

/* The fight's stage loads (port_stage_choice.h): a mode with no stage page
 * takes the default kind and time and the run's arena. */
s32 port_StageChoiceAtLoad(s32 mode) {
    PortStageChoice *choice = port_StageChoice();

    choice->on_stages = FALSE;
    choice->page_open = FALSE;
    if (!port_StageModeChooses(mode)) {
        choice->fight_kind = choice->default_kind;
        choice->fight_time = choice->default_time;
        choice->arena = (s8)port_ArenaAsked();
    }
    return choice->arena != choice->loaded_arena;
}

void port_StageChoiceLoaded(void) {
    g_port_stage_choice.loaded_arena = g_port_stage_choice.arena;
}
