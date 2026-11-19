/* The port's two gameplay switches as the game has taken them in, and the
 * stage pages of the one-player modes (port_play.h; docs/PORT.md, "Every
 * stage in every mode, and every unlock"). A state file: what is here is
 * saved, put back and hashed with the game's own variables. */
#include "common.h"
#include "bb2.h"

PortPlay g_port_play;

/* The run's switches taken in: at a frame of the game, whenever the game
 * asks. */
static PortPlay *port_Play(void) {
    PortPlay *play = &g_port_play;

    play->unlock_all = (u8)(port_GetUnlockAll() != 0);
    play->stage_pages = (u8)(port_GetStageSelectEverywhere() != 0);
    play->is_started = TRUE;
    return play;
}

s32 port_PlayUnlocks(s32 saved) {
    PortPlay *play = port_Play();

    play->unlocks = play->unlock_all ? saved | PORT_PLAY_ALL_UNLOCKS : saved;
    return play->unlocks;
}

s32 port_PlayStaffRolls(s32 saved) {
    return port_Play()->unlock_all ? PORT_PLAY_ALL_STAFF_ROLLS : saved;
}

/* Versus the CPU and training with stage_select_everywhere; their fights
 * are then on the stage chosen there until the select screen loads again. */
s32 port_PlayGoesToStages(s32 mode) {
    PortPlay *play = port_Play();

    if ((mode == FIGHT_MODE_VSCPU || mode == FIGHT_MODE_PRACTICE) &&
        play->stage_pages) {
        play->page_used = TRUE;
        return TRUE;
    }
    return FALSE;
}

void port_PlayReset(void) { g_port_play.page_used = FALSE; }

s32 port_PlayStageChosen(s32 mode) {
    return g_port_play.page_used &&
           (mode == FIGHT_MODE_VSCPU || mode == FIGHT_MODE_PRACTICE);
}
