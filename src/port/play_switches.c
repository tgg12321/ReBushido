/* The run's gameplay switches as it asked for them (port_play.h,
 * port_top_menu.h): the window's unlock_all and stage_select_everywhere,
 * or --unlock-all and --stage-select; its menu, bugfixes and the game's
 * three options, or --menu. Outside the state: the game's step takes them
 * in when it next asks (src/port/play.c, src/port/top_menu.c), and nothing
 * a rollback or run-ahead loads puts them back. In a session the host's
 * hold. */
#include "common.h"
#include "bb2.h"

static struct {
    s32 unlock_all;
    s32 stage_pages;
    s32 top_menu;
    s32 bugfixes;
    u32 game_options; /* the save's SAVE_FLAG_... bits */
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

void port_SetTopMenuSwitch(s32 on) {
    if (!g_port_play_switches.held) {
        g_port_play_switches.top_menu = on != 0;
    }
}

void port_SetBugfixesSwitch(s32 on) {
    if (!g_port_play_switches.held) {
        g_port_play_switches.bugfixes = on != 0;
    }
}

void port_SetGameOptions(u32 flags) {
    if (!g_port_play_switches.held) {
        g_port_play_switches.game_options = flags;
    }
}

s32 port_GetTopMenuSwitch(void) { return g_port_play_switches.top_menu; }

s32 port_GetBugfixesSwitch(void) { return g_port_play_switches.bugfixes; }

u32 port_GetGameOptions(void) { return g_port_play_switches.game_options; }

u32 port_PlaySwitchBits(void) {
    return (g_port_play_switches.unlock_all ? PORT_PLAY_BIT_UNLOCK_ALL : 0) |
           (g_port_play_switches.stage_pages ? PORT_PLAY_BIT_STAGE_PAGES : 0) |
           (g_port_play_switches.top_menu ? PORT_PLAY_BIT_TOP_MENU : 0) |
           (g_port_play_switches.bugfixes ? PORT_PLAY_BIT_BUGFIXES : 0) |
           ((g_port_play_switches.game_options & PORT_PLAY_OPTION_MASK)
            << PORT_PLAY_OPTION_SHIFT);
}

void port_HoldPlaySwitches(u32 bits) {
    g_port_play_switches.held = FALSE;
    port_SetUnlockAll((bits & PORT_PLAY_BIT_UNLOCK_ALL) != 0);
    port_SetStageSelectEverywhere((bits & PORT_PLAY_BIT_STAGE_PAGES) != 0);
    port_SetTopMenuSwitch((bits & PORT_PLAY_BIT_TOP_MENU) != 0);
    port_SetBugfixesSwitch((bits & PORT_PLAY_BIT_BUGFIXES) != 0);
    port_SetGameOptions(
        (bits >> PORT_PLAY_OPTION_SHIFT) & PORT_PLAY_OPTION_MASK);
    g_port_play_switches.held = TRUE;
}
