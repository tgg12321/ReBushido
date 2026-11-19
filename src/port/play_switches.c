/* The run's two gameplay switches as it asked for them (port_play.h):
 * the window's unlock_all and stage_select_everywhere, or --unlock-all and
 * --stage-select. Outside the state: the game's step takes them in when it
 * next asks (src/port/play.c), and nothing a rollback or run-ahead loads
 * puts them back. In a session the host's hold. */
#include "common.h"
#include "bb2.h"

static struct {
    s32 unlock_all;
    s32 stage_pages;
    s32 held;
} g_port_play_switches;

void port_SetUnlockAll(s32 on) {
    if (!g_port_play_switches.held) {
        g_port_play_switches.unlock_all = on != 0;
    }
}

void port_SetStageSelectEverywhere(s32 on) {
    if (!g_port_play_switches.held) {
        g_port_play_switches.stage_pages = on != 0;
    }
}

s32 port_GetUnlockAll(void) { return g_port_play_switches.unlock_all; }

s32 port_GetStageSelectEverywhere(void) {
    return g_port_play_switches.stage_pages;
}

u32 port_PlaySwitchBits(void) {
    return (g_port_play_switches.unlock_all ? PORT_PLAY_BIT_UNLOCK_ALL : 0) |
           (g_port_play_switches.stage_pages ? PORT_PLAY_BIT_STAGE_PAGES : 0);
}

void port_HoldPlaySwitches(u32 bits) {
    g_port_play_switches.held = FALSE;
    port_SetUnlockAll((bits & PORT_PLAY_BIT_UNLOCK_ALL) != 0);
    port_SetStageSelectEverywhere((bits & PORT_PLAY_BIT_STAGE_PAGES) != 0);
    g_port_play_switches.held = TRUE;
}
