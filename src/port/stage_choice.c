/* The kinds of stage the players choose on the stage pages (ROADMAP 16;
 * port_stage_choice.h; docs/PORT.md, "The stage select's kinds of
 * stage"). A state file: what is here is saved, put back and hashed with
 * the game's own variables, so the choice is rolled back with the stage
 * the game keeps and is the same on both machines of a session.
 *
 * On a stage page Square switches the stage under the cursor between
 * Classic and Remastered Classic, and Select opens the arenas' page over
 * it: up and down choose an arena, Cross fights on it (on the stage it
 * stands in for, which the player is never shown), Triangle or Select
 * goes back to the stages. Nothing here draws: the window lays the
 * switch's state and the arenas' page over the picture from what this
 * file keeps (src/port/window/stage_page.c), so video memory is the
 * console's and the play tests that hold the port's picture to the
 * emulator's see the screen they always did. */
#include "common.h"
#include "bb2.h"

PortStageChoice g_port_stage_choice;

/* the rows of the arenas' page shown at once */
#define PAGE_ROWS 5

/* The modes whose fights come from a stage page: versus and link. */
static s32 port_StageModeChooses(s32 mode) {
    return mode == FIGHT_MODE_VS || mode == FIGHT_MODE_NETWORK;
}

/* Made the first time the game asks, and the default kind taken in when
 * the window's setting has changed it (port_SetStageKindDefault): every
 * stage starts again as the new default. */
static PortStageChoice *port_StageChoice(void) {
    PortStageChoice *choice = &g_port_stage_choice;
    s32 kind = port_GetStageKindDefault();
    s32 i;

    if (!choice->is_started) {
        choice->is_started = TRUE;
        choice->default_kind = (u8)(kind ^ 1); /* reset below */
        choice->fight_kind = (u8)kind;
        choice->arena = (s8)port_ArenaAsked();
        choice->loaded_arena = PORT_STAGE_NO_ARENA;
    }
    if (choice->default_kind != kind) {
        choice->default_kind = (u8)kind;
        for (i = 0; i < STAGE_COUNT; i++) {
            choice->kinds[i] = (u8)kind;
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

s32 port_StageArena(void) {
    return g_port_stage_choice.is_started ? g_port_stage_choice.arena
                                          : port_ArenaAsked();
}

/* ---- a stage page ---- */

static void port_StagePageMove(PortStageChoice *choice, s32 step) {
    s32 count = port_ArenaCount();
    s32 row = choice->page_cursor + step;

    if (row < 0) {
        row = count - 1;
    } else if (row >= count) {
        row = 0;
    }
    choice->page_cursor = (u8)row;
    if (row < choice->page_top) {
        choice->page_top = (u8)row;
    } else if (row >= choice->page_top + PAGE_ROWS) {
        choice->page_top = (u8)(row - PAGE_ROWS + 1);
    }
}

/* A frame of a stage page (port_stage_choice.h): Square and Select on the
 * grid, or the arenas' page, which has every press while it is up. */
s32 port_StagePageStep(s32 pressed, s32 *stage, s32 is_open) {
    PortStageChoice *choice = port_StageChoice();
    const PortArena *arena;

    choice->on_stages = TRUE;
    choice->cursor_kind = *stage >= 0 && *stage < STAGE_COUNT
                              ? choice->kinds[*stage]
                              : choice->default_kind;
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
        if ((pressed & PAD_ANY(PADselect)) && port_ArenaCount() > 0) {
            snd_Play(MENU_SOUND_CONFIRM, SND_VOLUME_MAX, SND_VOLUME_MAX);
            choice->page_open = TRUE;
            choice->page_cursor =
                (u8)(choice->arena >= 0 && choice->arena < port_ArenaCount()
                         ? choice->arena
                         : 0);
            choice->page_top = 0;
            port_StagePageMove(choice, 0);
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
    if (pressed & PAD_ANY(PADLdown)) {
        snd_Play(MENU_SOUND_CURSOR, SND_VOLUME_MAX, SND_VOLUME_MAX);
        port_StagePageMove(choice, 1);
    } else if (pressed & PAD_ANY(PADLup)) {
        snd_Play(MENU_SOUND_CURSOR, SND_VOLUME_MAX, SND_VOLUME_MAX);
        port_StagePageMove(choice, -1);
    }
    arena = port_ArenaAt(choice->page_cursor);
    if ((pressed & PAD_ANY(PADRdown)) && arena != NULL) {
        snd_Play(MENU_SOUND_CONFIRM, SND_VOLUME_MAX, SND_VOLUME_MAX);
        choice->arena = (s8)choice->page_cursor;
        /* an arena brings its own dressing: the remastered picture's */
        choice->fight_kind = PORT_STAGE_REMASTERED;
        choice->page_open = FALSE;
        *stage = arena->host;
        return PORT_STAGE_PAGE_ARENA;
    }
    return PORT_STAGE_PAGE_TAKEN;
}

void port_StageChosen(s32 stage) {
    PortStageChoice *choice = port_StageChoice();

    choice->fight_kind = stage >= 0 && stage < STAGE_COUNT
                             ? choice->kinds[stage]
                             : choice->default_kind;
    choice->arena = (s8)port_ArenaAsked();
    choice->page_open = FALSE;
}

void port_StageChoiceReset(void) {
    PortStageChoice *choice = port_StageChoice();

    choice->fight_kind = choice->default_kind;
    choice->arena = (s8)port_ArenaAsked();
    choice->page_open = FALSE;
}

void port_StageLeftPages(void) { g_port_stage_choice.on_stages = FALSE; }

s32 port_StageChoiceAtLoad(s32 mode) {
    PortStageChoice *choice = port_StageChoice();

    choice->on_stages = FALSE;
    choice->page_open = FALSE;
    if (!port_StageModeChooses(mode)) {
        choice->fight_kind = choice->default_kind;
        choice->arena = (s8)port_ArenaAsked();
    }
    return choice->arena != choice->loaded_arena;
}

void port_StageChoiceLoaded(void) {
    g_port_stage_choice.loaded_arena = g_port_stage_choice.arena;
}
