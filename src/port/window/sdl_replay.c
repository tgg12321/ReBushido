/* The native port's replays in the window (port_replay.h; docs/PLAYING.md,
 * "Replays"): where they are kept and how many, the keys and the
 * controllers' buttons while one plays, the line that says where it is,
 * and the window kept alive while a playback waits (paused, or slower
 * than it was played). Only the build with a window has this file.
 *
 * While a replay plays, the game takes the recorded inputs and none of
 * the PC's: the keys and buttons below are the playback's, and the rest
 * (F1's settings menu, F2's picture, F12's picture taken) are the
 * window's as ever. */
#include <SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "port_replay.h"
#include "shared.h"
#include "sdl_window.h"

#define REPLAY_FOLDER "replays"
#define PATH_BYTES 1024
/* how long the writing of a fight's file may take at each boundary after
 * it, in microseconds */
#define SLICE_MICROSECONDS 2500
/* a blank, in milliseconds, for a playback slower than it was played */
#define BLANK_MS 16
/* how long the line says how a playback ended */
#define ENDED_SHOWN_MS 6000
#define FRAMES_PER_SECOND 30
#define SPEED_ONE 8
#define COLOR_TEXT 0xFFEDEDF2U
#define COLOR_TROUBLE 0xFFF0A848U

static struct {
    char folder[PATH_BYTES];
    u32 waited_at;
    char ended[PORT_REPLAY_WHY_BYTES];
    u32 ended_at;
    s32 ended_badly;
} g_replay;

/* The settings' replays, kept or not, and how many (port_WindowApplyChoice
 * and the start). */
void port_ApplyReplaySettings(void) {
    port_SetReplayRecording(g_port_settings.replays);
    port_SetReplayKeep((u32)g_port_settings.replays_keep,
                       (u32)g_port_settings.replays_megabytes);
}

/* While a playback waits: the window's events read, its last picture
 * shown again with the line, a blank's time passed. */
static void port_ReplayWait(void) {
    u32 now = SDL_GetTicks();

    port_PollWindow();
    port_ShowStill();
    if (now - g_replay.waited_at < BLANK_MS) {
        SDL_Delay(BLANK_MS - (now - g_replay.waited_at));
    }
    g_replay.waited_at = SDL_GetTicks();
}

static void port_ReplayEnded(const char *trouble) {
    port_ExportReplayEnded(trouble);
    g_replay.ended_at = SDL_GetTicks();
    g_replay.ended_badly = trouble != NULL;
    snprintf(g_replay.ended, sizeof(g_replay.ended), "%s",
             trouble != NULL ? trouble : "The replay is over");
    port_Note("replay: %s", g_replay.ended);
}

void port_StartReplayWindow(const char *beside, s32 is_test) {
    if (!is_test) {
        snprintf(g_replay.folder, sizeof(g_replay.folder), "%s%s", beside,
                 REPLAY_FOLDER);
        port_SetReplayFolder(g_replay.folder);
    }
    port_ApplyReplaySettings();
    port_SetReplaySlice(SLICE_MICROSECONDS);
    g_port_replay_wait_hook = port_ReplayWait;
    g_port_replay_end_hook = port_ReplayEnded;
}

/* ---- the keys while one plays ---- */

static s32 port_ReplayKey(SDL_Scancode code) {
    switch (code) {
    case SDL_SCANCODE_SPACE:
    case SDL_SCANCODE_P:
    case SDL_SCANCODE_RETURN:
        port_ReplayControl(PORT_REPLAY_PAUSE);
        return TRUE;
    case SDL_SCANCODE_RIGHT:
    case SDL_SCANCODE_PERIOD:
        port_ReplayControl(PORT_REPLAY_STEP);
        return TRUE;
    case SDL_SCANCODE_UP:
    case SDL_SCANCODE_EQUALS:
        port_ReplayControl(PORT_REPLAY_FASTER);
        return TRUE;
    case SDL_SCANCODE_DOWN:
    case SDL_SCANCODE_MINUS:
        port_ReplayControl(PORT_REPLAY_SLOWER);
        return TRUE;
    case SDL_SCANCODE_R:
    case SDL_SCANCODE_HOME:
        port_ReplayControl(PORT_REPLAY_RESTART);
        return TRUE;
    case SDL_SCANCODE_K:
    case SDL_SCANCODE_END:
        port_ReplayControl(PORT_REPLAY_KILL);
        return TRUE;
    case SDL_SCANCODE_ESCAPE:
    case SDL_SCANCODE_BACKSPACE:
        port_ReplayControl(PORT_REPLAY_STOP);
        return TRUE;
    default:
        return FALSE;
    }
}

static s32 port_ReplayButton(s32 button) {
    switch (button) {
    case SDL_CONTROLLER_BUTTON_A:
    case SDL_CONTROLLER_BUTTON_START:
        port_ReplayControl(PORT_REPLAY_PAUSE);
        return TRUE;
    case SDL_CONTROLLER_BUTTON_DPAD_RIGHT:
    case SDL_CONTROLLER_BUTTON_RIGHTSHOULDER:
        port_ReplayControl(PORT_REPLAY_STEP);
        return TRUE;
    case SDL_CONTROLLER_BUTTON_DPAD_UP:
        port_ReplayControl(PORT_REPLAY_FASTER);
        return TRUE;
    case SDL_CONTROLLER_BUTTON_DPAD_DOWN:
        port_ReplayControl(PORT_REPLAY_SLOWER);
        return TRUE;
    case SDL_CONTROLLER_BUTTON_X:
        port_ReplayControl(PORT_REPLAY_RESTART);
        return TRUE;
    case SDL_CONTROLLER_BUTTON_Y:
        port_ReplayControl(PORT_REPLAY_KILL);
        return TRUE;
    case SDL_CONTROLLER_BUTTON_B:
    case SDL_CONTROLLER_BUTTON_BACK:
        port_ReplayControl(PORT_REPLAY_STOP);
        return TRUE;
    default:
        return FALSE;
    }
}

s32 port_ReplayWindowEvent(const union SDL_Event *event) {
    if (!port_ReplayIsPlaying()) {
        return FALSE;
    }
    if (event->type == SDL_KEYDOWN || event->type == SDL_KEYUP) {
        SDL_Scancode code = event->key.keysym.scancode;

        if (code >= SDL_SCANCODE_F1 && code <= SDL_SCANCODE_F12) {
            return FALSE;
        }
        if (event->type == SDL_KEYDOWN && event->key.repeat == 0) {
            port_ReplayKey(code);
        }
        /* no key is the game's while a replay plays */
        return TRUE;
    }
    if (event->type == SDL_CONTROLLERBUTTONDOWN &&
        event->cbutton.button != SDL_CONTROLLER_BUTTON_GUIDE) {
        port_ReplayButton(event->cbutton.button);
        return TRUE;
    }
    return event->type == SDL_CONTROLLERBUTTONUP &&
           event->cbutton.button != SDL_CONTROLLER_BUTTON_GUIDE;
}

/* ---- the line ---- */

/* A film being made by another run (sdl_export.c), how far it is; or for
 * a moment after, where it went. FALSE where there is none to say. */
static s32 port_FilmWords(char *out, u32 room, u32 *color) {
    static u32 g_seen_generation;
    static u32 g_seen_at;
    PortExportView view;

    port_ExportGetView(&view);
    if (view.state == PORT_EXPORT_RUNNING) {
        snprintf(out, room,
                 "Making a film of the replay: %u%%   |   the settings "
                 "menu's Replays page stops it",
                 view.permille / 10);
        *color = COLOR_TEXT;
        return TRUE;
    }
    if (view.generation != g_seen_generation) {
        g_seen_generation = view.generation;
        g_seen_at = SDL_GetTicks();
    }
    if ((view.state == PORT_EXPORT_DONE || view.state == PORT_EXPORT_FAILED) &&
        SDL_GetTicks() - g_seen_at < ENDED_SHOWN_MS) {
        snprintf(out, room, "%s: %s",
                 view.state == PORT_EXPORT_DONE ? "Film made" : "No film",
                 view.words);
        *color = view.state == PORT_EXPORT_DONE ? COLOR_TEXT : COLOR_TROUBLE;
        return TRUE;
    }
    return FALSE;
}

u32 port_ReplayWords(char *out, u32 room) {
    PortReplayStatus status;
    char speed[32];
    u32 color;

    out[0] = 0;
    port_GetReplayStatus(&status);
    if (status.is_playing &&
        (g_port_window_options.is_hidden || port_ExportIsOn())) {
        /* a window never shown has no line over its picture: no one reads
         * it, and a film and a test take the picture alone */
        return COLOR_TEXT;
    }
    if (!status.is_playing && port_FilmWords(out, room, &color)) {
        return color;
    }
    if (!status.is_playing) {
        if (g_replay.ended[0] != 0 &&
            SDL_GetTicks() - g_replay.ended_at < ENDED_SHOWN_MS) {
            snprintf(out, room, "Replay: %s", g_replay.ended);
        }
        return g_replay.ended_badly ? COLOR_TROUBLE : COLOR_TEXT;
    }
    if (status.in_lead) {
        /* another build's: its run from power on, unseen, to the fight */
        snprintf(
            out, room,
            "Replay of another build: playing its session again from "
            "power on to the fight, %u%%   |   Esc back",
            status.lead_of > 0 ? status.lead_at * 100 / status.lead_of : 100);
        return COLOR_TEXT;
    }
    if (status.speed >= SPEED_ONE) {
        snprintf(speed, sizeof(speed), "x%d", status.speed / SPEED_ONE);
    } else {
        snprintf(speed, sizeof(speed), "x1/%d", SPEED_ONE / status.speed);
    }
    snprintf(out, room,
             "Replay   %u:%02u / %u:%02u   %s%s   |   Space pause   Right a "
             "frame   Up/Down speed   R start   K the kill   Esc back",
             status.frame / FRAMES_PER_SECOND / 60,
             status.frame / FRAMES_PER_SECOND % 60,
             status.frames / FRAMES_PER_SECOND / 60,
             status.frames / FRAMES_PER_SECOND % 60, speed,
             status.is_paused ? "   paused" : "");
    return COLOR_TEXT;
}
