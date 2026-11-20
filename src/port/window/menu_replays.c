/* The settings menu's Replays page (menu.h; docs/PLAYING.md, "Replays"):
 * the replays of replays/ beside the program, the newest first, each
 * with what it was (the date, the mode, the fighters and their weapons,
 * the stage, how long), and Enter or Cross to play one. A replay this
 * program cannot play says why instead (another build, another disc,
 * other gameplay mods). Only the build with a window has this file. */
#include <stdio.h>
#include <string.h>

#include "menu.h"
#include "port_replay.h"
#include "sdl_window.h"

#define REPLAY_ROWS 60
#define FRAMES_PER_SECOND 30
#define SECONDS_PER_MINUTE 60

static PortReplayInfo g_rows[REPLAY_ROWS];
static s32 g_row_count;
static char g_help[PORT_REPLAY_WHY_BYTES + 64];

s32 port_ReplaysItems(void) {
    g_row_count = port_ListReplays(g_rows, REPLAY_ROWS);
    return g_row_count;
}

/* A fighter as the row has it: its name, and its weapon where it is not
 * its own (every one of the roster has one). */
static void port_ReplayFighter(
    const PortReplayInfo *info, s32 side, char *out, u32 room) {
    snprintf(out, room, "%s%s (%s)", port_NameRoster(info->roster[side]),
             info->is_cpu[side] ? ", CPU" : "",
             port_NameWeapon(info->weapon[side]));
}

void port_ReplaysDescribe(
    s32 item, char *label, u32 label_room, char *value, u32 value_room) {
    const PortReplayInfo *info;
    char first[PORT_REPLAY_NAME_BYTES];
    char second[PORT_REPLAY_NAME_BYTES];
    u32 seconds;

    if (item < 0 || item >= g_row_count) {
        snprintf(label, label_room, "No replays yet");
        value[0] = 0;
        return;
    }
    info = &g_rows[item];
    port_ReplayFighter(info, 0, first, sizeof(first));
    port_ReplayFighter(info, 1, second, sizeof(second));
    seconds = info->frames / FRAMES_PER_SECOND;
    snprintf(
        label, label_room, "%.16s   %s  vs  %s", info->date, first, second);
    if (!info->playable) {
        snprintf(value, value_room, "cannot be played");
        return;
    }
    snprintf(value, value_room, "%s%s, %s, %u:%02u",
             info->online != 0 ? "online " : "", port_NameMode(info->mode),
             port_NameStage(info->stage), seconds / SECONDS_PER_MINUTE,
             seconds % SECONDS_PER_MINUTE);
}

const char *port_ReplaysHelp(s32 item) {
    if (item < 0 || item >= g_row_count) {
        return "Every fight is kept here as it ends (Keep replays, on the "
               "game's page)";
    }
    if (!g_rows[item].playable) {
        snprintf(g_help, sizeof(g_help), "Not by this program: %s",
                 g_rows[item].why);
        return g_help;
    }
    return "Enter / Cross: watch it. Then Space pauses, Right a frame, Up "
           "and Down the speed, R from the start, K to the kill, Escape back";
}

s32 port_ReplaysAccept(s32 item, char *note, u32 room) {
    char why[PORT_REPLAY_WHY_BYTES];

    if (item < 0 || item >= g_row_count) {
        return FALSE;
    }
    if (!port_PlayReplay(g_rows[item].path, FALSE, FALSE, why, sizeof(why))) {
        snprintf(note, room, "Not played: %s", why);
        return FALSE;
    }
    port_Note("replay: %s, from the settings menu", g_rows[item].name);
    return TRUE;
}
