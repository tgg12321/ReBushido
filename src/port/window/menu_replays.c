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

/* A fighter as the row has it: its name, and whether the CPU played it
 * (the weapons are on the line under). */
static void port_ReplayFighter(
    const PortReplayInfo *info, s32 side, char *out, u32 room) {
    snprintf(out, room, "%s%s", port_NameRoster(info->roster[side]),
             info->is_cpu[side] ? ", CPU" : "");
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
    snprintf(label, label_room, "%s  vs  %s", first, second);
    if (!info->playable) {
        snprintf(value, value_room, "cannot be played");
        return;
    }
    snprintf(value, value_room, "%u:%02u", seconds / SECONDS_PER_MINUTE,
             seconds % SECONDS_PER_MINUTE);
}

void port_ReplaysDetail(s32 item, char *out, u32 room) {
    const PortReplayInfo *info;

    if (item < 0 || item >= g_row_count) {
        out[0] = 0;
        return;
    }
    info = &g_rows[item];
    snprintf(out, room, "%s and %s     %.16s     %s (%s%s)",
             port_NameWeapon(info->weapon[0]), port_NameWeapon(info->weapon[1]),
             info->date, port_NameStage(info->stage),
             info->online != 0 ? "online " : "", port_NameMode(info->mode));
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
    return "Enter / Cross: watch it.   Square / Delete: make a film of it "
           "(films/)";
}

/* ---- a replay as a film (sdl_export.c) ---- */

const char * const g_port_replays_film_settings[2] = {
    "film_size", "film_picture"};

s32 port_ReplaysExport(s32 item, char *note, u32 room) {
    char why[PORT_REPLAY_WHY_BYTES];

    if (item < 0 || item >= g_row_count) {
        return FALSE;
    }
    if (!g_rows[item].playable) {
        snprintf(note, room, "No film: %s", g_rows[item].why);
        return FALSE;
    }
    if (!port_ExportBegin(
            g_rows[item].path, g_rows[item].name, why, sizeof(why))) {
        snprintf(note, room, "No film: %s", why);
        return FALSE;
    }
    return TRUE;
}

s32 port_ReplaysFilmShown(void) {
    PortExportView view;

    port_ExportGetView(&view);
    return view.state != PORT_EXPORT_IDLE;
}

void port_ReplaysFilmDescribe(
    char *label, u32 label_room, char *value, u32 value_room) {
    PortExportView view;

    port_ExportGetView(&view);
    value[0] = 0;
    switch (view.state) {
    case PORT_EXPORT_RUNNING:
        snprintf(label, label_room, "Making a film of %.40s", view.name);
        snprintf(value, value_room, "%u%%   %u:%02u", view.permille / 10,
                 view.seconds / SECONDS_PER_MINUTE,
                 view.seconds % SECONDS_PER_MINUTE);
        break;
    case PORT_EXPORT_DONE:
        snprintf(label, label_room, "Film made: %s", view.words);
        break;
    case PORT_EXPORT_FAILED:
        snprintf(label, label_room, "No film: %s", view.words);
        break;
    default:
        snprintf(label, label_room, "The film was stopped");
        break;
    }
}

const char *port_ReplaysFilmHelp(void) {
    PortExportView view;

    port_ExportGetView(&view);
    if (view.state == PORT_EXPORT_RUNNING) {
        return "Made by another run of the program, the game here as it "
               "was. Enter / Cross: stop it";
    }
    if (view.state == PORT_EXPORT_DONE) {
        return "In films/ beside the program: Windows, browsers and Discord "
               "play an .mp4 as it is.  Enter / Cross: put this line away";
    }
    return "Enter / Cross: put this line away";
}

void port_ReplaysFilmAccept(void) {
    PortExportView view;

    port_ExportGetView(&view);
    if (view.state == PORT_EXPORT_RUNNING) {
        port_ExportStop();
    } else {
        port_ExportForget();
    }
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
