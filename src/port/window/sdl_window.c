/* The native port's window, sound card and controllers, on SDL 2
 * (docs/PORT.md, "The window"). Only the build with a window has this
 * file (tools/port_sdl_build.py).
 *
 * It hangs on the port at four places (port_platform.h, port_spu.h),
 * and changes nothing the game computes:
 *
 *   - at a vertical blank, before the pads' packets are filled, SDL's
 *     events are read: keys and controllers become the pads' buttons
 *     (keymap.c), a controller plugged in or pulled out is taken or let
 *     go, Escape or the window's close ends the program;
 *   - as a blank's samples are made they go into a queue (ring.c), which
 *     the sound card empties from a thread of SDL's;
 *   - at a blank's end the displayed rectangle of the port's video
 *     memory is shown, scaled (scale.c), when the PC's clock says the
 *     blank is due (pace.c);
 *   - as the program ends, the sound card and the window are closed.
 *
 * The game runs on a stack of the port's own and SDL is called from it:
 * one thread, so what the game sees of a key is the same however the
 * PC's threads fall.
 *
 * The picture is shown one of two ways (docs/PORT.md, "Remastered in the
 * window"). Where the PC gives an OpenGL 3.3 core context, the window is
 * made with one and everything is drawn through it: the remastered
 * picture, which the card itself drew (src/port/gl/), or the software
 * GPU's own pixels as a texture, and the counter over either. Where it
 * does not, the window is made again without one and SDL's renderer
 * shows the software GPU's picture, as it did before there was a card
 * path at all; the remastered picture is then not there and F2 says so. */
#include <SDL.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "gl.h"
#include "gl_pause.h"
#include "gl_stage.h"
#include "gl_tween.h"
#include "menu.h"
#include "port_gpu.h"
#include "port_host.h"
#include "port_movie.h"
#include "port_remaster.h"
#include "port_spu.h"
#include "shared.h"
#include "sdl_window.h"

#define WINDOW_TITLE "ReBushido"
/* The window as it opens: the picture three times. */
#define WINDOW_WIDTH (3 * PORT_PICTURE_UNIT_W)
#define WINDOW_HEIGHT (3 * PORT_PICTURE_UNIT_H)
/* A display slower than this cannot show every blank: it is not waited
 * for. */
#define REFRESH_MIN 59
/* Pictures a second to draw where the display does not say its rate and
 * the settings ask for the display's. */
#define REFRESH_UNKNOWN 60
/* The sound card is asked for this many samples at a time (23 ms), and
 * the queue before it is kept this long (58 ms) of the room it has. */
#define AUDIO_DEVICE_FRAMES 1024
#define AUDIO_QUEUE_FRAMES 2560
#define AUDIO_RING_FRAMES 8192
/* The last of a wait is not slept, for the system wakes a sleeper late:
 * this many samples' time (3 ms) is watched out on the clock. */
#define WAIT_AWAKE 132
#define MILLISECONDS 1000
#define PATH_BYTES_MAX 1024
#define BYTES_PER_PIXEL 4
#define TAP_MAGIC 0x52464252U /* "RBFR", the low byte first */
/* The counter in the corner: its own few pixels, a dark box under white
 * digits, drawn over the picture SDL has been handed already. */
#define COUNTER_W 112
#define COUNTER_H (PORT_GLYPH_H + 2 * COUNTER_PAD)
#define COUNTER_PAD 2
#define COUNTER_BOX 0xB0000000U
#define COUNTER_INK 0xFFFFFFFFU
/* The player's own pictures of the window (F12): the folder they go in,
 * beside the program, and how the files are named. */
#define SHOTS_FOLDER "screenshots"
#define SHOT_EPOCH_YEAR 1900
#define SHOT_NUMBERS 1000
/* The log's line every ten seconds, and its count of the sound running
 * out every minute, in vertical blanks. */
#define BEAT_BLANKS 600
#define MINUTE_BLANKS 3600
/* The window under the settings menu, darkened by this much black. */
#define MENU_DIM 0xA0000000U
#define ALPHA_SHIFT 24

PortSettings g_port_settings;
PortWindowOptions g_port_window_options;

static SDL_Window *g_port_window;
static SDL_Renderer *g_port_renderer;
static SDL_Texture *g_port_texture;
static s32 g_port_texture_w;
static s32 g_port_texture_h;
static s32 g_port_is_open;
static s32 g_port_is_fullscreen;
static s32 g_port_vsync;
/* The picture as it is given to SDL: 0xFFRRGGBB a pixel. */
static u32 g_port_frame[PORT_PICTURE_WIDTH_MAX * PORT_PICTURE_HEIGHT_MAX];

/* The graphics card: the window's own context, and the scale the
 * remastered renderer is drawing at (port_GlSetOptions), which follows
 * the window's size unless a setting fixes it. */
static SDL_GLContext g_port_gl_context;
static s32 g_port_has_gl;
static s32 g_port_gl_scale;
static s32 g_port_gl_tall;
/* How many times the picture was changed (F2), for the end's account. */
static u32 g_port_toggles;
/* The window's own pixels, where a test asks for them (--tap-window) or
 * the player does (F12), and how many pictures F12 has taken. */
static u32 *g_port_window_pixels;
static u32 g_port_window_room;
static s32 g_port_wants_shot;
static u32 g_port_shots_taken;
/* Which picture of the blank to keep (0 is the game's own frame, and
 * -1 the last one shown), and the pixels kept. */
static s32 g_port_shot_picture = -1;
static s32 g_port_has_shot;
static s32 g_port_shot_w;
static s32 g_port_shot_h;
static s32 g_port_shot_between;

static SDL_AudioDeviceID g_port_audio;
static PortRing g_port_ring;
static s16 g_port_ring_samples[AUDIO_RING_FRAMES * 2];
static FILE *g_port_audio_tap;

static PortPace g_port_pace;
static Uint64 g_port_clock_start;
static u32 g_port_blanks_shown;
static s32 g_port_show_fps;
static SDL_Texture *g_port_counter;
static u32 g_port_counter_pixels[COUNTER_W * COUNTER_H];
static PortFpsMeter g_port_shown_meter;
static PortFpsMeter g_port_game_meter;
static PortFpsMeter g_port_blank_meter;

/* More pictures a second than the game has frames (gl_tween.h;
 * docs/PORT.md, "Remastered", "More pictures a second"): the display's
 * own rate, whether the renderer that draws between frames is there, and
 * where the frame on show began, by which a picture's phase between it
 * and the frame after it is worked out. */
static s32 g_port_display_rate;
static s32 g_port_tween_on;
static u32 g_port_tween_frames;
static u32 g_port_frame_at;
static u32 g_port_frame_period;
static u32 g_port_next_picture;
static u32 g_port_pictures;
static u32 g_port_pictures_between;
/* The game as the log last saw it, and what the log has counted. */
static PortGameState g_port_seen;
static s32 g_port_has_seen;
static u32 g_port_lost_noted;
static u32 g_port_underruns_noted;
static u32 g_port_read_start;

/* The controller of each pad, and SDL's number for it while it is
 * plugged in. */
static SDL_GameController *g_port_controllers[PORT_PADS];
static SDL_JoystickID g_port_controller_ids[PORT_PADS];
/* The buttons the keys and controllers held on each pad when they were
 * last read, which is what the port's pads are given (g_port_device_hook). */
static u32 g_port_sent[PORT_PADS];
/* For the end's account: a plan's presses made as the PC's input, the
 * key and controller events read, and the changes told to the pads. */
static u32 g_port_injected;
static u32 g_port_key_events;
static u32 g_port_controller_events;
static u32 g_port_pad_changes;
/* What the pads are not given until it is let go: what was held as the
 * settings menu closed, and the Select that opened it (sdl_menu.c). */
static u32 g_port_held_back[PORT_PADS];
/* The settings menu's own texture, where SDL's renderer draws. */
static SDL_Texture *g_port_menu_texture;
static s32 g_port_menu_texture_w;
static s32 g_port_menu_texture_h;

static void port_SwapPicture(void);

/* ---- names ---- */

s32 port_LookUpName(s32 source, const char *name) {
    switch (source) {
    case PORT_SOURCE_KEY: {
        SDL_Scancode code = SDL_GetScancodeFromName(name);

        return code == SDL_SCANCODE_UNKNOWN ? -1 : (s32)code;
    }
    case PORT_SOURCE_BUTTON:
        return (s32)SDL_GameControllerGetButtonFromString(name);
    case PORT_SOURCE_AXIS:
        return (s32)SDL_GameControllerGetAxisFromString(name);
    default:
        return -1;
    }
}

/* A line for each pad's button with what presses it. */
void port_DescribeControls(void) {
    static const char * const order[] = {
        "up",       "down", "left", "right", "cross", "circle", "square",
        "triangle", "l1",   "r1",   "l2",    "r2",    "start",  "select",
    };
    char line[PATH_BYTES_MAX];
    s32 pad;
    size_t which;
    s32 i;

    for (pad = 0; pad < PORT_PADS; pad++) {
        port_Say("player %d:", pad + 1);
        for (which = 0; which < sizeof(order) / sizeof(order[0]); which++) {
            u32 button = port_FindPadButton(order[which]);
            size_t used = 0;

            line[0] = 0;
            for (i = 0; i < g_port_settings.count; i++) {
                const PortBinding *binding = &g_port_settings.bindings[i];
                const char *name = "?";
                const char *kind = "key ";
                const char *sign = "";

                /* one that presses several buttons at once is said under
                 * each of them */
                if (binding->pad != pad || (binding->button & button) == 0) {
                    continue;
                }
                if (binding->source == PORT_SOURCE_KEY) {
                    name = SDL_GetScancodeName((SDL_Scancode)binding->code);
                } else if (binding->source == PORT_SOURCE_BUTTON) {
                    kind = "pad ";
                    name = SDL_GameControllerGetStringForButton(
                        (SDL_GameControllerButton)binding->code);
                } else {
                    kind = "pad ";
                    sign = binding->direction > 0 ? "+" : "-";
                    name = SDL_GameControllerGetStringForAxis(
                        (SDL_GameControllerAxis)binding->code);
                }
                if (used < sizeof(line)) {
                    used += (size_t)snprintf(
                        line + used, sizeof(line) - used, "%s%s%s%s",
                        used != 0 ? ", " : "", kind, sign,
                        name != NULL ? name : "?");
                }
            }
            port_Say("  %-9s %s", order[which], line[0] != 0 ? line : "-");
        }
    }
}

/* ---- the clock ---- */

/* The PC's clock in the sound's samples since the window opened, going
 * round at 32 bits (window.h). */
static u32 port_ReadClock(void) {
    Uint64 elapsed = SDL_GetPerformanceCounter() - g_port_clock_start;
    Uint64 frequency = SDL_GetPerformanceFrequency();
    Uint64 whole = elapsed / frequency;
    Uint64 part = elapsed % frequency;

    return (u32)(whole * PORT_SPU_RATE + part * PORT_SPU_RATE / frequency);
}

/* Waits `samples` of the clock: most of it asleep, the end awake. */
static void port_WaitFor(u32 samples) {
    u32 until = port_ReadClock() + samples;

    for (;;) {
        s32 left = (s32)(until - port_ReadClock());

        if (left <= 0) {
            return;
        }
        if (left > WAIT_AWAKE) {
            SDL_Delay(
                (Uint32)(left - WAIT_AWAKE) * MILLISECONDS / PORT_SPU_RATE);
        }
    }
}

/* ---- sound ---- */

/* For the tests of the fault's report (--fault-at, --fault-in-sound):
 * memory that is not there is written. */
static void port_FaultOnPurpose(void) {
    static volatile s32 * volatile nowhere = (volatile s32 *)0x44;

    *nowhere = 1;
}

/* SDL's thread asks for samples: it holds the device's lock, and the
 * game's side takes the same lock to write. */
static void SDLCALL port_FillAudio(void *unused, Uint8 *stream, int bytes) {
    (void)unused;
    if (g_port_window_options.fault_in_sound > 0 &&
        port_GetBlankCount() >= (u32)g_port_window_options.fault_in_sound) {
        port_FaultOnPurpose();
    }
    port_ReadRing(
        &g_port_ring, (s16 *)(void *)stream, (u32)bytes / (2 * sizeof(s16)));
}

/* A blank's samples, as they are made (port_spu.h). */
static void port_QueueSound(const s16 *samples, u32 frames) {
    if (g_port_audio_tap != NULL) {
        fwrite(samples, 2 * sizeof(s16), frames, g_port_audio_tap);
    }
    if (g_port_audio != 0) {
        SDL_LockAudioDevice(g_port_audio);
        port_WriteRing(&g_port_ring, samples, frames);
        SDL_UnlockAudioDevice(g_port_audio);
    }
}

static void port_OpenAudio(void) {
    SDL_AudioSpec want;
    SDL_AudioSpec have;

    if (SDL_InitSubSystem(SDL_INIT_AUDIO) != 0) {
        port_Say("no sound: %s", SDL_GetError());
        return;
    }
    SDL_zero(want);
    want.freq = PORT_SPU_RATE;
    want.format = AUDIO_S16SYS;
    want.channels = 2;
    want.samples = AUDIO_DEVICE_FRAMES;
    want.callback = port_FillAudio;
    port_InitRing(&g_port_ring, g_port_ring_samples, AUDIO_RING_FRAMES,
                  AUDIO_QUEUE_FRAMES);
    /* nothing may be changed: SDL converts if the card wants another
     * rate or form */
    g_port_audio = SDL_OpenAudioDevice(NULL, 0, &want, &have, 0);
    if (g_port_audio == 0) {
        port_Say("no sound: %s", SDL_GetError());
        return;
    }
    port_Note("sound: driver %s, device %s; %d a second, %d channels, %d "
              "samples at a time; the queue is kept %d long",
              SDL_GetCurrentAudioDriver(),
              SDL_GetNumAudioDevices(0) > 0 ? SDL_GetAudioDeviceName(0, 0)
                                            : "the system's own choice",
              have.freq, have.channels, have.samples, AUDIO_QUEUE_FRAMES);
    SDL_PauseAudioDevice(g_port_audio, 0);
}

/* ---- controllers ---- */

static s32 port_FindController(SDL_JoystickID id) {
    s32 pad;

    for (pad = 0; pad < PORT_PADS; pad++) {
        if (g_port_controllers[pad] != NULL &&
            g_port_controller_ids[pad] == id) {
            return pad;
        }
    }
    return -1;
}

/* A controller was plugged in: it is the first pad's that has none. */
static void port_AddController(int device) {
    SDL_GameController *controller;
    SDL_JoystickID id;
    s32 pad;

    if (!SDL_IsGameController(device)) {
        return;
    }
    if (g_port_window_options.inject == PORT_INJECT_PADS &&
        !SDL_JoystickIsVirtual(device)) {
        /* a test presses its own controllers: no real one may join */
        return;
    }
    id = SDL_JoystickGetDeviceInstanceID(device);
    if (port_FindController(id) >= 0) {
        return;
    }
    for (pad = 0; pad < PORT_PADS; pad++) {
        if (g_port_controllers[pad] == NULL) {
            break;
        }
    }
    if (pad == PORT_PADS) {
        port_Say("a third controller is not used: %s",
                 SDL_GameControllerNameForIndex(device));
        return;
    }
    controller = SDL_GameControllerOpen(device);
    if (controller == NULL) {
        port_Say("a controller cannot be opened: %s", SDL_GetError());
        return;
    }
    g_port_controllers[pad] = controller;
    g_port_controller_ids[pad] = id;
    port_Say("player %d's controller: %s", pad + 1,
             SDL_GameControllerName(controller));
}

/* A controller was pulled out: what it held is let go. */
static void port_RemoveController(SDL_JoystickID id) {
    s32 pad = port_FindController(id);

    if (pad < 0) {
        return;
    }
    SDL_GameControllerClose(g_port_controllers[pad]);
    g_port_controllers[pad] = NULL;
    port_ReleaseController(&g_port_settings, pad);
    port_Say("player %d's controller was unplugged", pad + 1);
}

/* Two controllers of SDL's own making, for a test's presses. A trigger
 * at rest is at its axis's lowest. */
static void port_AttachTestControllers(void) {
    s32 pad;

    for (pad = 0; pad < PORT_PADS; pad++) {
        int device = SDL_JoystickAttachVirtual(
            SDL_JOYSTICK_TYPE_GAMECONTROLLER, SDL_CONTROLLER_AXIS_MAX,
            SDL_CONTROLLER_BUTTON_MAX, 0);

        if (device < 0) {
            port_Say(
                "--inject pads: SDL makes no controller: %s", SDL_GetError());
            port_Quit(PORT_EXIT_FAILED);
        }
        port_AddController(device);
        if (g_port_controllers[pad] != NULL) {
            SDL_Joystick *joystick =
                SDL_GameControllerGetJoystick(g_port_controllers[pad]);

            SDL_JoystickSetVirtualAxis(
                joystick, SDL_CONTROLLER_AXIS_TRIGGERLEFT,
                SDL_JOYSTICK_AXIS_MIN);
            SDL_JoystickSetVirtualAxis(
                joystick, SDL_CONTROLLER_AXIS_TRIGGERRIGHT,
                SDL_JOYSTICK_AXIS_MIN);
        }
    }
}

/* ---- the window ---- */

static void port_SetFullscreen(s32 on) {
    g_port_is_fullscreen = on;
    SDL_SetWindowFullscreen(
        g_port_window, on ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0);
    SDL_ShowCursor(on ? SDL_DISABLE : SDL_ENABLE);
}

/* The window's size in pixels, whichever way it is drawn into. */
static void port_WindowSize(s32 *w, s32 *h) {
    int across = 0;
    int down = 0;

    if (g_port_has_gl) {
        SDL_GL_GetDrawableSize(g_port_window, &across, &down);
    } else if (g_port_renderer != NULL) {
        SDL_GetRendererOutputSize(g_port_renderer, &across, &down);
    }
    *w = across;
    *h = down;
}

/* What the picture runs on, for the log. */
static void port_NoteVideo(void) {
    SDL_RendererInfo info;
    SDL_DisplayMode mode;
    s32 w = 0;
    s32 h = 0;

    port_Note("video: driver %s", SDL_GetCurrentVideoDriver());
    if (g_port_has_gl) {
        port_Note("video: OpenGL %s", port_GlDescribe(PORT_GL_VERSION));
        port_Note("video: card %s, %s", port_GlDescribe(PORT_GL_VENDOR),
                  port_GlDescribe(PORT_GL_RENDERER));
    } else if (SDL_GetRendererInfo(g_port_renderer, &info) == 0) {
        port_Note("video: renderer %s%s", info.name,
                  (info.flags & SDL_RENDERER_ACCELERATED) ? "" : " (software)");
    }
    if (SDL_GetCurrentDisplayMode(
            SDL_GetWindowDisplayIndex(g_port_window), &mode) == 0) {
        port_Note("video: display %d by %d, %d pictures a second", mode.w,
                  mode.h, mode.refresh_rate);
    }
    port_WindowSize(&w, &h);
    port_Note("video: window %d by %d%s; vsync %s; scale %d, filter %s", w, h,
              g_port_is_fullscreen ? ", the whole screen" : "",
              g_port_vsync ? "on" : "off", g_port_settings.scale,
              g_port_settings.is_linear ? "linear" : "nearest");
}

/* ---- the graphics card ---- */

/* The scale the remastered renderer draws the scene at: the setting's, or
 * as many times the console's resolution as the window's own size asks
 * for (port_RemasterScaleFor). */
static s32 port_WantedScale(void) {
    PortDisplay display;
    s32 window_w = 0;
    s32 window_h = 0;
    s32 margin;

    if (g_port_settings.resolution != PORT_RESOLUTION_WINDOW) {
        return g_port_settings.resolution;
    }
    port_WindowSize(&window_w, &window_h);
    port_GetDisplay(&display);
    margin = port_RemasterMargin(
        display.w, g_port_settings.aspect_w, g_port_settings.aspect_h);
    return port_RemasterScaleFor(
        window_w, window_h, display.w + 2 * margin, display.h);
}

/* The renderer's options for a scale: how many times the console's lines
 * the picture is drawn at follows the display's own mode, so that its
 * pixels come out square (port_TallFor). */
static void port_GlOptionsFor(s32 scale, PortGlOptions *options) {
    PortRemasterView view;
    PortDisplay display;

    port_RemasterGetView(&view);
    port_GetDisplay(&display);
    SDL_zerop(options);
    options->scale = scale;
    options->tall = port_TallFor(scale, display.w, display.h);
    options->sample = view.supersample;
    options->aspect_w = view.aspect_w;
    options->aspect_h = view.aspect_h;
    options->is_bilinear = FALSE;
    options->is_exact = FALSE;
    options->is_stage_mesh = g_port_settings.stage_mesh;
    options->is_fighter_models = g_port_settings.fighter_models;
    /* the scene held against the ground's own depth, which is part of
     * drawing the ground as one mesh and has no setting of its own
     * (gl.h) */
    options->is_stage_depth = TRUE;
    /* the fighters' shadows as a darkening of the ground, or the game's
     * own black polygons (src/port/gl/gl_shadow.h) */
    options->shadows = g_port_settings.shadows;
}

/* The window's context and the remastered renderer on it: NULL, or why
 * there is none. The window must have been made with SDL_WINDOW_OPENGL.
 * Nothing of this changes what the game computes: with no card the port
 * is what it was. */
static const char *port_StartGl(void) {
    PortGlOptions options;
    const char *problem;

    g_port_gl_context = SDL_GL_CreateContext(g_port_window);
    if (g_port_gl_context == NULL) {
        return SDL_GetError();
    }
    if (SDL_GL_MakeCurrent(g_port_window, g_port_gl_context) != 0) {
        return SDL_GetError();
    }
    SDL_GL_SetSwapInterval(g_port_vsync ? 1 : 0);
    g_port_gl_scale = port_WantedScale();
    port_GlOptionsFor(g_port_gl_scale, &options);
    g_port_gl_tall = options.tall;
    problem = port_GlStart(SDL_GL_GetProcAddress, &options);
    if (problem != NULL) {
        return problem;
    }
    g_port_has_gl = TRUE;
    problem = port_GlPauseStart(SDL_GL_GetProcAddress);
    if (problem != NULL) {
        port_Say("graphics: %s, so the pause draws the fight as the console "
                 "does",
                 problem);
    }
    return NULL;
}

/* The context and the window let go, so that the window can be made
 * again without a context (port_OpenWindow). */
static void port_DropGl(void) {
    port_GlPauseStop();
    if (g_port_gl_context != NULL) {
        SDL_GL_DeleteContext(g_port_gl_context);
        g_port_gl_context = NULL;
    }
    g_port_has_gl = FALSE;
}

/* Which picture is shown and what the remastered one is set to, for the
 * log. */
static void port_NoteGraphics(void) {
    PortRemasterView view;

    port_RemasterGetView(&view);
    port_Note("graphics: %s%s", port_RemasterIsOn() ? "remastered" : "original",
              g_port_has_gl ? ""
                            : " (no graphics card: the remastered picture is "
                              "not there and F2 does nothing)");
    if (g_port_has_gl) {
        port_Note("graphics: the scene %d times the console's resolution "
                  "(%s), draw distance %s, antialiasing %d, aspect %d:%d",
                  g_port_gl_scale,
                  g_port_settings.resolution == PORT_RESOLUTION_WINDOW
                      ? "the window's own size"
                      : "as the settings fix it",
                  port_NameRemasterDistance(view.distance), view.supersample,
                  view.aspect_w, view.aspect_h);
    }
}

/* ---- more pictures a second (gl_tween.h) ---- */

/* The renderer that draws a frame again between two of the game's, where
 * the settings ask for more pictures a second than the game has frames.
 * It needs the card path; with `frame_rate original` nothing of it is
 * started and the window shows one picture a vertical blank, exactly as
 * it did. */
static void port_StartTween(void) {
    const char *problem;

    if (g_port_settings.frame_rate == PORT_FRAME_RATE_ORIGINAL) {
        return;
    }
    if (!g_port_has_gl) {
        port_Say("frame_rate: there is no graphics card, so one picture a "
                 "vertical blank is what is shown (the console's rate)");
        return;
    }
    problem = port_TweenStart();
    if (problem != NULL) {
        port_Say("frame_rate: %s; one picture a vertical blank", problem);
        return;
    }
    g_port_tween_on = TRUE;
    if (g_port_settings.frame_rate == PORT_FRAME_RATE_DISPLAY) {
        port_Note("frame_rate: the display's own rate (%d a second as it "
                  "says, %d where it does not); the game steps at the "
                  "console's 59.83 blanks a second as before",
                  g_port_display_rate, REFRESH_UNKNOWN);
    } else {
        port_Note("frame_rate: %d pictures a second; the game steps at the "
                  "console's 59.83 blanks a second as before",
                  g_port_settings.frame_rate);
    }
}

/* How many pictures a second the window is to draw; 0 for one a vertical
 * blank, which is the console's own rate. */
static s32 port_PictureRate(void) {
    if (!g_port_tween_on || !port_RemasterIsOn()) {
        return 0;
    }
    if (g_port_settings.frame_rate == PORT_FRAME_RATE_DISPLAY) {
        return g_port_display_rate > 0 ? g_port_display_rate : REFRESH_UNKNOWN;
    }
    return g_port_settings.frame_rate;
}

/* The window itself, with a context for the graphics card or without
 * one. */
static void port_MakeWindow(s32 wants_gl) {
    Uint32 flags = SDL_WINDOW_RESIZABLE;

    if (g_port_settings.is_fullscreen) {
        flags |= SDL_WINDOW_FULLSCREEN_DESKTOP;
    }
    if (wants_gl) {
        flags |= SDL_WINDOW_OPENGL;
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
        SDL_GL_SetAttribute(
            SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
        SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
        SDL_GL_SetAttribute(SDL_GL_RED_SIZE, 8);
        SDL_GL_SetAttribute(SDL_GL_GREEN_SIZE, 8);
        SDL_GL_SetAttribute(SDL_GL_BLUE_SIZE, 8);
        SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 0);
    }
    g_port_window = SDL_CreateWindow(
        WINDOW_TITLE, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        g_port_window_options.window_w > 0 ? g_port_window_options.window_w
                                           : WINDOW_WIDTH,
        g_port_window_options.window_h > 0 ? g_port_window_options.window_h
                                           : WINDOW_HEIGHT,
        flags);
}

/* The window, the sound card and the controllers, at the first blank:
 * the port has its memory and its disc by then. */
static void port_OpenWindow(void) {
    SDL_DisplayMode mode;

    if (g_port_is_open) {
        return;
    }
    g_port_is_open = 1;
    g_port_show_fps = g_port_settings.fps;
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMECONTROLLER) != 0) {
        port_Say("SDL does not start: %s", SDL_GetError());
        port_Quit(PORT_EXIT_FAILED);
    }
    SDL_SetHint(
        SDL_HINT_RENDER_SCALE_QUALITY, g_port_settings.is_linear ? "1" : "0");
    port_MakeWindow(TRUE);
    if (g_port_window == NULL) {
        port_Note("video: no window with OpenGL: %s", SDL_GetError());
        port_MakeWindow(FALSE);
    }
    if (g_port_window == NULL) {
        port_Say("no window: %s", SDL_GetError());
        port_Quit(PORT_EXIT_FAILED);
    }
    g_port_is_fullscreen = g_port_settings.is_fullscreen;
    if (g_port_is_fullscreen) {
        SDL_ShowCursor(SDL_DISABLE);
    }
    g_port_vsync = g_port_settings.vsync && !g_port_window_options.is_fast;
    if (SDL_GetCurrentDisplayMode(
            SDL_GetWindowDisplayIndex(g_port_window), &mode) == 0) {
        g_port_display_rate = mode.refresh_rate;
    }
    if (g_port_vsync && g_port_display_rate != 0 &&
        g_port_display_rate < REFRESH_MIN) {
        port_Say("the display shows %d pictures a second, fewer than the "
                 "game's 59.83: it is not waited for (vsync off)",
                 g_port_display_rate);
        g_port_vsync = 0;
    }
    if ((SDL_GetWindowFlags(g_port_window) & SDL_WINDOW_OPENGL) != 0) {
        const char *problem = port_StartGl();

        if (problem != NULL) {
            /* no card path: the window is made again without a context,
             * since a window made for OpenGL is no use to SDL's renderer */
            port_Say("the remastered picture is not there: %s", problem);
            port_Say("the console's own picture is what is shown (F2 does "
                     "nothing). It needs OpenGL 3.3.");
            port_DropGl();
            SDL_DestroyWindow(g_port_window);
            port_MakeWindow(FALSE);
            if (g_port_window == NULL) {
                port_Say("no window: %s", SDL_GetError());
                port_Quit(PORT_EXIT_FAILED);
            }
        }
    }
    if (!g_port_has_gl) {
        g_port_renderer = SDL_CreateRenderer(
            g_port_window, -1, g_port_vsync ? SDL_RENDERER_PRESENTVSYNC : 0);
        if (g_port_renderer == NULL) {
            g_port_renderer =
                SDL_CreateRenderer(g_port_window, -1, SDL_RENDERER_SOFTWARE);
        }
        if (g_port_renderer == NULL) {
            port_Say("the window cannot be drawn into: %s", SDL_GetError());
            port_Quit(PORT_EXIT_FAILED);
        }
    }
    port_NoteVideo();
    port_NoteGraphics();
    port_StartTween();
    if (!g_port_window_options.no_sound) {
        port_OpenAudio();
    } else {
        port_Note("sound: none asked for (--no-sound)");
    }
    if (g_port_window_options.inject == PORT_INJECT_PADS) {
        port_AttachTestControllers();
    }
    g_port_clock_start = SDL_GetPerformanceCounter();
}

/* The texture the picture is given to SDL in, made again when the
 * game's mode changes the picture's size. */
static void port_SizeTexture(s32 w, s32 h) {
    if (g_port_texture != NULL && w == g_port_texture_w &&
        h == g_port_texture_h) {
        return;
    }
    if (g_port_texture != NULL) {
        SDL_DestroyTexture(g_port_texture);
    }
    g_port_texture =
        SDL_CreateTexture(g_port_renderer, SDL_PIXELFORMAT_ARGB8888,
                          SDL_TEXTUREACCESS_STREAMING, w, h);
    if (g_port_texture == NULL) {
        port_Say("no texture of %d by %d: %s", w, h, SDL_GetError());
        port_Quit(PORT_EXIT_FAILED);
    }
    g_port_texture_w = w;
    g_port_texture_h = h;
}

/* For a test: the picture as SDL is given it, to a file. Four words
 * (the mark, the width, the height, whether the display is on) and the
 * pixels. */
static void port_TapFrame(u32 blank, s32 w, s32 h, s32 is_on) {
    char path[PATH_BYTES_MAX];
    u32 head[4];
    FILE *file;

    snprintf(path, sizeof(path), "%s/blank-%06u.raw",
             g_port_window_options.tap_frames, blank);
    file = fopen(path, "wb");
    if (file == NULL) {
        port_Say("--tap-frames: %s cannot be made", path);
        port_Quit(PORT_EXIT_FAILED);
    }
    head[0] = TAP_MAGIC;
    head[1] = (u32)w;
    head[2] = (u32)h;
    head[3] = (u32)is_on;
    fwrite(head, sizeof(head), 1, file);
    fwrite(g_port_frame, sizeof(u32), (size_t)w * (size_t)h, file);
    fclose(file);
}

/* The shape the remastered picture is shown in: the console's four by
 * three in every mode, widened by the margin the view asks for. It is a
 * shape and not a count of pixels, because how many pixels the renderer
 * draws is the scale's business and the shape must not follow it. */
static void port_RemasterShape(s32 *across, s32 *down) {
    PortDisplay display;
    PortRemasterView view;
    s32 margin;

    port_GetDisplay(&display);
    port_RemasterGetView(&view);
    margin = port_RemasterMargin(display.w, view.aspect_w, view.aspect_h);
    *across = PORT_PICTURE_ASPECT_W * (display.w + 2 * margin);
    *down = PORT_PICTURE_ASPECT_H * display.w;
}

/* The remastered picture, which the card drew, into the window: as large
 * as its own shape allows, in the middle. It is as wide as the view asks
 * for, so that shape and not always the console's four by three is what
 * it is fitted by. FALSE if the card has no picture to show. */
static s32 port_ShowRemastered(s32 window_w, s32 window_h) {
    PortGlOptions options;
    PortRect place;
    s32 across = 0;
    s32 down = 0;
    s32 wanted = port_WantedScale();

    port_GlOptionsFor(wanted, &options);
    if (wanted != g_port_gl_scale || options.tall != g_port_gl_tall) {
        /* the window was resized, or the game changed its display mode,
         * which changes how tall a line of its picture is */
        g_port_gl_scale = wanted;
        g_port_gl_tall = options.tall;
        port_GlSetOptions(&options);
        port_Note("graphics: the scene is now %d times the console's "
                  "resolution and %d times its lines (the window is %d by "
                  "%d)",
                  wanted, options.tall, window_w, window_h);
    }
    port_RemasterShape(&across, &down);
    port_FitShape(
        window_w, window_h, g_port_settings.scale, across, down, &place);
    return port_GlShowPicture(window_w, window_h, place.x, place.y, place.w,
                              place.h, g_port_settings.is_linear);
}

/* Whether the window shows the remastered picture of this frame: not
 * while the movie player runs, whose frames are in the 24-bit mode the
 * card does not draw and whose title picture is put in video memory
 * outside the game's display, so that the software GPU's picture is the
 * one there is of them. */
static s32 port_ShowsRemastered(void) {
    PortDisplay display;

    if (!g_port_has_gl || !port_RemasterIsOn() || port_MovieIsPlaying()) {
        return FALSE;
    }
    port_GetDisplay(&display);
    return !display.is_rgb24;
}

/* ---- a picture of the window, for the player (F12) ---- */

/* The window's own pixels into `g_port_window_pixels`, 0xFFRRGGBB each,
 * a row after a row from the top: the card's framebuffer where there is
 * one, and SDL's renderer where there is not. FALSE if they cannot be
 * read or there is no room for them. */
static s32 port_ReadWindowPixels(s32 window_w, s32 window_h) {
    if (window_w <= 0 || window_h <= 0) {
        return FALSE;
    }
    if (g_port_window_room < (u32)(window_w * window_h)) {
        free(g_port_window_pixels);
        g_port_window_room = (u32)(window_w * window_h);
        g_port_window_pixels = malloc((size_t)g_port_window_room * sizeof(u32));
        if (g_port_window_pixels == NULL) {
            g_port_window_room = 0;
            return FALSE;
        }
    }
    if (g_port_has_gl) {
        return port_GlReadWindow(
            g_port_window_pixels, g_port_window_room, window_w, window_h);
    }
    return SDL_RenderReadPixels(
               g_port_renderer, NULL, SDL_PIXELFORMAT_ARGB8888,
               g_port_window_pixels, window_w * BYTES_PER_PIXEL) == 0;
}

/* For a test: the window's own pixels, as a player would see them, to a
 * file of the same form as port_TapFrame's. Only the build with a
 * graphics card has them; nothing of the game's reads them back. */
static void port_TapWindow(u32 blank) {
    char path[PATH_BYTES_MAX];
    u32 head[4];
    s32 window_w = 0;
    s32 window_h = 0;
    FILE *file;

    if (!g_port_has_gl) {
        return;
    }
    port_WindowSize(&window_w, &window_h);
    if (!port_ReadWindowPixels(window_w, window_h)) {
        return;
    }
    snprintf(path, sizeof(path), "%s/window-%06u.raw",
             g_port_window_options.tap_window, blank);
    file = fopen(path, "wb");
    if (file == NULL) {
        port_Say("--tap-window: %s cannot be made", path);
        port_Quit(PORT_EXIT_FAILED);
    }
    head[0] = TAP_MAGIC;
    head[1] = (u32)window_w;
    head[2] = (u32)window_h;
    head[3] = (u32)port_RemasterIsOn();
    fwrite(head, sizeof(head), 1, file);
    fwrite(g_port_window_pixels, sizeof(u32),
           (size_t)window_w * (size_t)window_h, file);
    fclose(file);
}

/* Whether a picture after this one is still wanted for a shot: a test
 * that asks for the Nth picture of a blank gets it even on a machine
 * with no time in the blank to draw it, which is every machine without
 * a graphics card. */
static s32 port_WantsLaterShot(s32 which) {
    return g_port_wants_shot && g_port_shot_picture > which;
}

/* A picture is drawn and about to be shown: where it is the one asked
 * for, the window's own pixels are kept for port_WriteShot. `which` is
 * which picture of this blank it is, counting the game's own frame as
 * 0; the pictures between frames follow it (gl_tween.h). With nothing
 * asked for in particular it is the last picture of the blank that is
 * kept, which is the one that stays on the screen -- and which, at more
 * pictures a second than the game has frames, is one drawn between two
 * of them. That is what a player pressing F12 is looking at, and what
 * the first key could never catch. */
static void port_KeepShot(s32 which) {
    s32 window_w = 0;
    s32 window_h = 0;

    if (!g_port_wants_shot) {
        return;
    }
    if (g_port_shot_picture >= 0 && which != g_port_shot_picture) {
        return;
    }
    port_WindowSize(&window_w, &window_h);
    g_port_has_shot = port_ReadWindowPixels(window_w, window_h);
    g_port_shot_w = window_w;
    g_port_shot_h = window_h;
    g_port_shot_between = which;
}

/* The window as the player sees it, into
 * `screenshots/shot-YYYYMMDD-HHMMSS-NNN.png` beside the program, and a
 * line of the log saying where it went (docs/PLAYING.md). */
static void port_WriteShot(void) {
    char path[PATH_BYTES_MAX];
    char folder[PATH_BYTES_MAX];
    char *base = SDL_GetBasePath();
    const char *beside = base != NULL ? base : "";
    time_t now = time(NULL);
    const struct tm *when = localtime(&now);
    s32 window_w = g_port_shot_w;
    s32 window_h = g_port_shot_h;
    s32 file;
    s32 written;

    g_port_wants_shot = FALSE;
    g_port_shot_picture = -1;
    if (!g_port_has_shot || when == NULL) {
        port_Say("F12: the window's pixels cannot be read");
        SDL_free(base);
        return;
    }
    g_port_has_shot = FALSE;
    snprintf(folder, sizeof(folder), "%s%s", beside, SHOTS_FOLDER);
    port_MakeFolder(folder);
    snprintf(path, sizeof(path), "%s/shot-%04d%02d%02d-%02d%02d%02d-%03u.png",
             folder, when->tm_year + SHOT_EPOCH_YEAR, when->tm_mon + 1,
             when->tm_mday, when->tm_hour, when->tm_min, when->tm_sec,
             g_port_shots_taken % SHOT_NUMBERS);
    SDL_free(base);
    file = port_HostCreate(path);
    written = file >= 0 && port_WritePixelsPng(
                               file, g_port_window_pixels, window_w, window_h);
    if (file >= 0) {
        port_HostClose(file);
    }
    g_port_shots_taken++;
    if (!written) {
        port_Say("F12: the picture %s cannot be written", path);
        return;
    }
    port_Say(
        "F12: a picture of the window, %d by %d, %s: %s", window_w, window_h,
        g_port_shot_between == 0 ? "the game's own frame"
                                 : "a picture between two frames",
        path);
}

/* The displayed rectangle of video memory into the window: black where
 * the display is off or has no size. */
static void port_DrawPicture(u32 blank, s32 may_tap) {
    PortDisplay display;
    PortRect place;
    SDL_Rect where;
    s32 w;
    s32 h;
    s32 window_w = 0;
    s32 window_h = 0;

    port_GetDisplay(&display);
    w = display.w < PORT_PICTURE_WIDTH_MAX ? display.w : PORT_PICTURE_WIDTH_MAX;
    h = display.h < PORT_PICTURE_HEIGHT_MAX
            ? display.h
            : PORT_PICTURE_HEIGHT_MAX;
    port_WindowSize(&window_w, &window_h);
    if (g_port_has_gl) {
        port_GlClearWindow(window_w, window_h);
    } else {
        SDL_SetRenderDrawColor(g_port_renderer, 0, 0, 0, SDL_ALPHA_OPAQUE);
        SDL_RenderClear(g_port_renderer);
    }
    if (w <= 0 || h <= 0) {
        return;
    }
    /* the software GPU's picture is made whichever is shown: it is what a
     * test takes of the picture, and what the window draws where the
     * remastered picture is off */
    port_ConvertDisplay(g_port_vram, display.x, display.y, w, h,
                        display.is_rgb24, g_port_frame);
    if (may_tap && g_port_window_options.tap_frames != NULL &&
        blank % (u32)g_port_window_options.tap_every == 0) {
        port_TapFrame(blank, w, h, display.is_on);
    }
    if (!display.is_on) {
        return;
    }
    if (g_port_has_gl) {
        if (port_ShowsRemastered() && port_ShowRemastered(window_w, window_h)) {
            return;
        }
        /* a movie is video and is smoothed when it is scaled, as the
         * remastered picture is, whatever the filter says of the game's
         * blocks */
        port_FitPicture(window_w, window_h, g_port_settings.scale, &place);
        port_GlShowPixels(g_port_frame, w, h, window_w, window_h, place.x,
                          place.y, place.w, place.h,
                          g_port_settings.is_linear ||
                              (port_RemasterIsOn() && port_MovieIsPlaying()),
                          FALSE);
        return;
    }
    port_SizeTexture(w, h);
    SDL_UpdateTexture(g_port_texture, NULL, g_port_frame, w * BYTES_PER_PIXEL);
    port_FitPicture(window_w, window_h, g_port_settings.scale, &place);
    where.x = place.x;
    where.y = place.y;
    where.w = place.w;
    where.h = place.h;
    SDL_RenderCopy(g_port_renderer, g_port_texture, NULL, &where);
}

/* The counter in the corner: pictures shown a second, the game's own
 * frames a second, and the speed against the console's. Its own
 * texture, each of its pixels a whole number of the window's, over the
 * picture: nothing of it is in the game's video memory or in what a
 * test takes of the picture. */
/* Where the picture is in the window, whichever of the two is shown: the
 * counter sits in a corner of it. */
static void port_PicturePlace(s32 window_w, s32 window_h, PortRect *out) {
    s32 across = 0;
    s32 down = 0;

    if (!port_ShowsRemastered()) {
        port_FitPicture(window_w, window_h, g_port_settings.scale, out);
        return;
    }
    port_RemasterShape(&across, &down);
    port_FitShape(window_w, window_h, g_port_settings.scale, across, down, out);
}

static void port_DrawCounter(void) {
    char text[PORT_FPS_TEXT_BYTES];
    PortRect picture;
    PortRect place;
    SDL_Rect from;
    SDL_Rect where;
    s32 text_w;
    s32 i;
    s32 window_w = 0;
    s32 window_h = 0;

    if (!g_port_show_fps) {
        return;
    }
    if (g_port_has_gl) {
        port_FormatFps(text, g_port_shown_meter.tenths,
                       g_port_game_meter.tenths, g_port_blank_meter.tenths);
        text_w = port_MeasureText(text) + 2 * COUNTER_PAD;
        if (text_w > COUNTER_W) {
            text_w = COUNTER_W;
        }
        /* the box is the text's own width here, so that the pixels given
         * to the card are the ones shown and no more */
        for (i = 0; i < text_w * COUNTER_H; i++) {
            g_port_counter_pixels[i] = COUNTER_BOX;
        }
        port_DrawText(g_port_counter_pixels, text_w, COUNTER_H, COUNTER_PAD,
                      COUNTER_PAD, text, COUNTER_INK);
        port_WindowSize(&window_w, &window_h);
        port_PicturePlace(window_w, window_h, &picture);
        port_PlaceCounter(
            &picture, text_w, COUNTER_H, g_port_settings.fps_corner, &place);
        port_GlShowPixels(
            g_port_counter_pixels, text_w, COUNTER_H, window_w, window_h,
            place.x, place.y, place.w, place.h, FALSE, TRUE);
        return;
    }
    if (g_port_counter == NULL) {
        g_port_counter = SDL_CreateTexture(
            g_port_renderer, SDL_PIXELFORMAT_ARGB8888,
            SDL_TEXTUREACCESS_STREAMING, COUNTER_W, COUNTER_H);
        if (g_port_counter == NULL) {
            port_Say("no counter: %s", SDL_GetError());
            g_port_show_fps = 0;
            return;
        }
        SDL_SetTextureBlendMode(g_port_counter, SDL_BLENDMODE_BLEND);
        SDL_SetTextureScaleMode(g_port_counter, SDL_ScaleModeNearest);
    }
    port_FormatFps(text, g_port_shown_meter.tenths, g_port_game_meter.tenths,
                   g_port_blank_meter.tenths);
    text_w = port_MeasureText(text) + 2 * COUNTER_PAD;
    if (text_w > COUNTER_W) {
        text_w = COUNTER_W;
    }
    for (i = 0; i < COUNTER_W * COUNTER_H; i++) {
        g_port_counter_pixels[i] = i % COUNTER_W < text_w ? COUNTER_BOX : 0;
    }
    port_DrawText(g_port_counter_pixels, COUNTER_W, COUNTER_H, COUNTER_PAD,
                  COUNTER_PAD, text, COUNTER_INK);
    SDL_UpdateTexture(g_port_counter, NULL, g_port_counter_pixels,
                      COUNTER_W * BYTES_PER_PIXEL);
    port_WindowSize(&window_w, &window_h);
    port_FitPicture(window_w, window_h, g_port_settings.scale, &picture);
    port_PlaceCounter(
        &picture, text_w, COUNTER_H, g_port_settings.fps_corner, &place);
    from.x = 0;
    from.y = 0;
    from.w = text_w;
    from.h = COUNTER_H;
    where.x = place.x;
    where.y = place.y;
    where.w = place.w;
    where.h = place.h;
    SDL_RenderCopy(g_port_renderer, g_port_counter, &from, &where);
}

/* ---- the log's notes of the game ---- */

/* Where the game is, when that changes; a fight's beginning and end;
 * a line every ten seconds; the sound running out, by the minute. It
 * reads the game and writes the log, and nothing else. */
static void port_WatchGame(u32 blank) {
    PortGameState now;
    u32 clock = port_ReadClock();
    s32 frames;

    port_ReadGameState(&now);
    frames = g_port_has_seen ? now.frames - g_port_seen.frames : 0;
    port_CountFps(&g_port_shown_meter, clock, 0);
    port_CountFps(&g_port_blank_meter, clock, 1);
    port_CountFps(&g_port_game_meter, clock, frames > 0 ? (u32)frames : 0);
    if (!g_port_has_seen || now.screen != g_port_seen.screen) {
        port_Note("game: screen %d, %s%s; blank %u, frame %d", now.screen,
                  port_NameScreen(now.screen),
                  g_port_has_seen ? ""
                                  : " (as the program starts: the "
                                    "options page comes first)",
                  blank, now.frames);
    }
    if (now.in_fight && (!g_port_has_seen || !g_port_seen.in_fight)) {
        port_Note(
            "game: a fight begins: mode %s, on %s (stage %d); %s%s with "
            "the %s against %s%s with the %s",
            port_NameMode(now.mode), port_NameStage(now.stage), now.stage,
            port_NameRoster(now.roster[0]), now.is_cpu[0] ? " (CPU)" : "",
            port_NameWeapon(now.weapon[0]), port_NameRoster(now.roster[1]),
            now.is_cpu[1] ? " (CPU)" : "", port_NameWeapon(now.weapon[1]));
    }
    if (now.in_fight && g_port_has_seen && now.defeat_frames != 0 &&
        g_port_seen.defeat_frames == 0) {
        port_Note("game: the fight is over: %s; blank %u, frame %d",
                  now.winner == 0   ? "player one is left standing"
                  : now.winner == 1 ? "player two is left standing"
                                    : "nobody is left standing",
                  blank, now.frames);
    }
    if (g_port_pace.lost != g_port_lost_noted) {
        port_Note("pace: the program was held up for more than a tenth of a "
                  "second, and the time was let go (%u times so far); blank "
                  "%u",
                  g_port_pace.lost, blank);
        g_port_lost_noted = g_port_pace.lost;
    }
    if (blank % BEAT_BLANKS == 0) {
        port_Note(
            "beat: blank %u, frame %d, screen %s; %u.%u shown and %u.%u "
            "of the game's frames a second; %u samples queued",
            blank, now.frames, port_NameScreen(now.screen),
            g_port_shown_meter.tenths / 10, g_port_shown_meter.tenths % 10,
            g_port_game_meter.tenths / 10, g_port_game_meter.tenths % 10,
            g_port_audio != 0 ? g_port_ring.count : 0);
    }
    if (blank % MINUTE_BLANKS == 0 &&
        g_port_ring.underruns != g_port_underruns_noted) {
        port_Note("sound: ran out %u times in the last minute (%u in all)",
                  g_port_ring.underruns - g_port_underruns_noted,
                  g_port_ring.underruns);
        g_port_underruns_noted = g_port_ring.underruns;
    }
    g_port_seen = now;
    g_port_has_seen = 1;
}

/* What the disc's drive reads (port_platform.h). */
static void port_NoteDisc(s32 kind, u32 sector) {
    if (kind == 1) {
        g_port_read_start = sector;
    } else if (kind == 2) {
        port_Note("disc: a stream of sound from sector %u; blank %u", sector,
                  port_GetBlankCount());
    } else if (sector != g_port_read_start) {
        port_Note("disc: %u sectors read from sector %u; blank %u",
                  sector - g_port_read_start, g_port_read_start,
                  port_GetBlankCount());
    }
}

/* Whether a box may be put before the player: never where SDL was told
 * to show nothing (the tests), and only on Windows, where a program
 * started by a double click has a console that closes with it. */
static s32 port_MayShowBox(void) {
#if defined(_WIN32)
    const char *driver = getenv("SDL_VIDEODRIVER");

    return driver == NULL ||
           (strcmp(driver, "dummy") != 0 && strcmp(driver, "offscreen") != 0);
#else
    return 0;
#endif
}

/* The player is told the game stopped, and where the log is. NOT TRIED:
 * no box has been opened. */
static void port_ShowStopBox(const char *what) {
    char text[2 * PATH_BYTES_MAX];
    const char *log = port_GetLogPath();

    if (!port_MayShowBox()) {
        return;
    }
    snprintf(text, sizeof(text), "%s\n\n%s%s", what,
             log != NULL ? "What happened is written in:\n"
                         : "This session kept no log.",
             log != NULL ? log : "");
    SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, WINDOW_TITLE, text, NULL);
}

/* After a fault's report (port_platform.h): the last functions the game
 * began, where the game was, and the log shut, so that all of it is in
 * the file whatever happens next. */
static void port_AfterCrash(void) {
    char log[PATH_BYTES_MAX];
    const char *path = port_GetLogPath();
    PortGameState now;
    u32 i;

    port_Say("the last functions the game began, the newest first:");
    for (i = 0; i < PORT_RECENT_CALLS && i < g_port_recent_call_count; i++) {
        u32 offset = 0;
        u32 address = g_port_recent_calls[(g_port_recent_call_count - 1 - i) %
                                          PORT_RECENT_CALLS];
        const char *name = port_FindFunction(address, &offset);

        port_Say("  %s (0x%08X)", name != NULL ? name : "?", address);
    }
    port_ReadGameState(&now);
    port_Say("the game: screen %s, mode %s, stage %s; %s with the %s, %s "
             "with the %s",
             port_NameScreen(now.screen), port_NameMode(now.mode),
             port_NameStage(now.stage), port_NameRoster(now.roster[0]),
             port_NameWeapon(now.weapon[0]), port_NameRoster(now.roster[1]),
             port_NameWeapon(now.weapon[1]));
    snprintf(log, sizeof(log), "%s", path != NULL ? path : "");
    port_Note("session: ended by a fault after %u.%03u seconds",
              (unsigned)(SDL_GetTicks() / MILLISECONDS),
              (unsigned)(SDL_GetTicks() % MILLISECONDS));
    port_CloseLog();
    if (log[0] != 0) {
        fprintf(stderr, "rebushido: this session's log: %s\n", log);
    }
    if (port_MayShowBox()) {
        char text[2 * PATH_BYTES_MAX];

        snprintf(text, sizeof(text),
                 "ReBushido stopped: the game's code failed.\n\n%s%s",
                 log[0] != 0 ? "What happened is written in:\n"
                             : "This session kept no log.",
                 log);
        SDL_ShowSimpleMessageBox(
            SDL_MESSAGEBOX_ERROR, WINDOW_TITLE, text, NULL);
    }
}

/* The session's end (sdl_window.h). */
void port_EndSession(s32 code) {
    port_Note(
        "session: ended %s (exit code %d) after %u.%03u seconds",
        code == PORT_EXIT_OK ? "as asked" : "because something stopped it",
        code, (unsigned)(SDL_GetTicks() / MILLISECONDS),
        (unsigned)(SDL_GetTicks() % MILLISECONDS));
    if (code != PORT_EXIT_OK) {
        port_ShowStopBox("ReBushido stopped.");
    }
    port_CloseLog();
}

/* Original and Remastered, one after the other (port_remaster.h): F2,
 * or the test's own `--toggle-at`. `why` names which for the log. */
static void port_ChangePicture(const char *why) {
    port_ToggleGraphics();
    g_port_toggles++;
    if (g_port_tween_on) {
        /* the frames written down are no longer the ones the display
         * holds: nothing is drawn between frames until the game has
         * drawn two more (gl_tween.h) */
        port_TweenForget();
        g_port_frame_period = 0;
    }
    if (!g_port_has_gl) {
        port_Say("%s: there is no graphics card here, so the remastered "
                 "picture cannot be shown",
                 why);
        return;
    }
    if (port_RemasterIsOn()) {
        /* what the game wrote into video memory while the console's
         * picture was shown was never sent to the card: all of it goes
         * now, so that the first remastered frame has its textures and
         * its 2D layer */
        port_GlRefresh();
    }
    port_Say("%s: the %s picture; blank %u", why,
             port_RemasterIsOn() ? "remastered" : "original",
             port_GetBlankCount());
    port_NoteGraphics();
}

/* ---- for the settings menu (sdl_menu.c, menu.h) ---- */

void port_GetWindowSize(s32 *w, s32 *h) { port_WindowSize(w, h); }

/* The window's pixels as a PNG at `path`, for a test, as F12 reads them:
 * before they are shown. */
static s32 port_WriteWindowPng(const char *path) {
    s32 window_w = 0;
    s32 window_h = 0;
    s32 file;
    s32 written;

    port_WindowSize(&window_w, &window_h);
    if (!port_ReadWindowPixels(window_w, window_h)) {
        return FALSE;
    }
    file = port_HostCreate(path);
    if (file < 0) {
        return FALSE;
    }
    written =
        port_WritePixelsPng(file, g_port_window_pixels, window_w, window_h);
    port_HostClose(file);
    return written;
}

void port_ShowMenu(
    u32 blank, const u32 *pixels, const PortRect *place, const char *shot) {
    static const u32 g_dim = MENU_DIM;
    s32 window_w = 0;
    s32 window_h = 0;
    SDL_Rect where;

    port_WindowSize(&window_w, &window_h);
    port_DrawPicture(blank, FALSE);
    if (g_port_has_gl) {
        port_GlShowPixels(&g_dim, 1, 1, window_w, window_h, 0, 0, window_w,
                          window_h, FALSE, TRUE);
        port_GlShowPixels(pixels, place->w, place->h, window_w, window_h,
                          place->x, place->y, place->w, place->h, FALSE, TRUE);
    } else {
        SDL_SetRenderDrawBlendMode(g_port_renderer, SDL_BLENDMODE_BLEND);
        SDL_SetRenderDrawColor(
            g_port_renderer, 0, 0, 0, MENU_DIM >> ALPHA_SHIFT);
        SDL_RenderFillRect(g_port_renderer, NULL);
        if (g_port_menu_texture == NULL || g_port_menu_texture_w != place->w ||
            g_port_menu_texture_h != place->h) {
            if (g_port_menu_texture != NULL) {
                SDL_DestroyTexture(g_port_menu_texture);
            }
            g_port_menu_texture = SDL_CreateTexture(
                g_port_renderer, SDL_PIXELFORMAT_ARGB8888,
                SDL_TEXTUREACCESS_STREAMING, place->w, place->h);
            g_port_menu_texture_w = place->w;
            g_port_menu_texture_h = place->h;
            if (g_port_menu_texture == NULL) {
                return;
            }
            SDL_SetTextureBlendMode(g_port_menu_texture, SDL_BLENDMODE_BLEND);
        }
        SDL_UpdateTexture(
            g_port_menu_texture, NULL, pixels, place->w * BYTES_PER_PIXEL);
        where.x = place->x;
        where.y = place->y;
        where.w = place->w;
        where.h = place->h;
        SDL_RenderCopy(g_port_renderer, g_port_menu_texture, NULL, &where);
    }
    if (shot != NULL && !port_WriteWindowPng(shot)) {
        port_Say("--menu-shots: %s cannot be written", shot);
    }
    port_SwapPicture();
}

void port_HoldBackPads(void) {
    s32 pad;

    for (pad = 0; pad < PORT_PADS; pad++) {
        g_port_held_back[pad] = port_GetHeldButtons(&g_port_settings, pad);
    }
}

void port_TakeControllerEvent(const SDL_Event *event) {
    if (event->type == SDL_CONTROLLERDEVICEADDED) {
        port_AddController(event->cdevice.which);
    } else if (event->type == SDL_CONTROLLERDEVICEREMOVED) {
        port_RemoveController(event->cdevice.which);
    }
}

s32 port_PadOfController(s32 id) {
    return port_FindController((SDL_JoystickID)id);
}

void port_ToggleFullscreen(void) { port_SetFullscreen(!g_port_is_fullscreen); }

s32 port_WindowIsRemastered(void) { return port_RemasterIsOn(); }

void port_WindowSetRemastered(s32 on) {
    if ((port_RemasterIsOn() != 0) != (on != 0)) {
        port_ChangePicture("the settings menu");
    }
}

void port_WindowApplyChoice(s32 choice) {
    const char *name = g_port_choices[choice].name;

    if (strcmp(name, "fullscreen") == 0) {
        port_SetFullscreen(g_port_settings.is_fullscreen);
    } else if (strcmp(name, "fps") == 0) {
        g_port_show_fps = g_port_settings.fps;
    } else if (strcmp(name, "vsync") == 0) {
        g_port_vsync = g_port_settings.vsync && !g_port_window_options.is_fast;
        if (g_port_has_gl) {
            SDL_GL_SetSwapInterval(g_port_vsync ? 1 : 0);
        } else {
            SDL_RenderSetVSync(g_port_renderer, g_port_vsync);
        }
    } else if (strcmp(name, "filter") == 0 && !g_port_has_gl) {
        /* SDL's renderer takes the filter as a texture is made */
        SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY,
                    g_port_settings.is_linear ? "1" : "0");
        g_port_texture_w = 0;
    } else if (
        (strcmp(name, "stage_mesh") == 0 || strcmp(name, "shadows") == 0) &&
        g_port_has_gl) {
        PortGlOptions options;

        port_GlOptionsFor(g_port_gl_scale, &options);
        port_GlSetOptions(&options);
    }
    /* the picture's size and filter on the card, the resolution, the
     * counter's corner and the dead zone are read each time they are
     * used */
}

/* ---- the pads ---- */

/* What the keys and controllers hold on each pad, through the layout,
 * now that their events have been read; the changes counted. */
static void port_SendPads(void) {
    s32 pad;
    u32 bit;

    for (pad = 0; pad < PORT_PADS; pad++) {
        u32 held = port_GetHeldButtons(&g_port_settings, pad);
        u32 changed;

        /* the settings menu's (sdl_menu.c): what is held back until it
         * is let go, and Select where it opens the menu */
        g_port_held_back[pad] &= held;
        held &= ~g_port_held_back[pad];
        g_port_held_back[pad] |=
            port_MenuTakesPad(held, g_port_sent[pad], &held);
        changed = held ^ g_port_sent[pad];

        for (bit = 1; changed != 0; bit <<= 1) {
            if (changed & bit) {
                g_port_pad_changes++;
                changed &= ~bit;
            }
        }
        g_port_sent[pad] = held;
    }
}

/* The port's pads ask what the PC holds on one of them (port_platform.h):
 * what it held when the events were last read, at a vertical blank. */
static u32 port_DeviceButtons(s32 pad) {
    return pad >= 0 && pad < PORT_PADS ? g_port_sent[pad] : 0;
}

static void port_TakeKey(const SDL_KeyboardEvent *key) {
    s32 down = key->type == SDL_KEYDOWN;

    if (down && key->repeat != 0) {
        return;
    }
    if (down && key->keysym.scancode == SDL_SCANCODE_ESCAPE) {
        port_Quit(PORT_EXIT_OK);
    }
    if (down && (key->keysym.scancode == SDL_SCANCODE_F11 ||
                 (key->keysym.scancode == SDL_SCANCODE_RETURN &&
                  (key->keysym.mod & KMOD_ALT) != 0))) {
        /* the window's own, and no button of a pad */
        port_SetFullscreen(!g_port_is_fullscreen);
        return;
    }
    if (key->keysym.scancode == SDL_SCANCODE_F2) {
        /* the window's own, and no button of a pad */
        if (down) {
            port_ChangePicture("F2");
        }
        return;
    }
    if (key->keysym.scancode == SDL_SCANCODE_F3) {
        if (down) {
            g_port_show_fps = !g_port_show_fps;
        }
        return;
    }
    if (key->keysym.scancode == SDL_SCANCODE_F1) {
        /* the window's own: the settings menu */
        if (down) {
            port_AskForMenu("F1");
        }
        return;
    }
    if (key->keysym.scancode == SDL_SCANCODE_F12) {
        /* the window's own, and no button of a pad: the picture itself is
         * taken at the end of the blank, where everything of it has been
         * drawn and nothing shown yet */
        if (down) {
            g_port_wants_shot = TRUE;
        }
        return;
    }
    g_port_key_events++;
    port_ApplyInput(
        &g_port_settings, PORT_SOURCE_KEY, 0, (s32)key->keysym.scancode, down);
}

/* A plan's press, made as the PC's own input (port_platform.h): a key
 * event, or a button or an axis of a controller of SDL's making. The
 * events are read at the same blank, before the pads are filled. */
static s32 port_InjectPress(s32 pad, u32 button, s32 down) {
    s32 keys = g_port_window_options.inject == PORT_INJECT_KEYS;
    const PortBinding *binding =
        port_FindBinding(&g_port_settings, keys, pad, button);

    port_OpenWindow();
    if (binding == NULL) {
        port_Say("--inject: nothing presses %s of player %d",
                 port_NamePadButton(button), pad + 1);
        port_Quit(PORT_EXIT_FAILED);
    }
    if (keys) {
        SDL_Event event;

        SDL_zero(event);
        event.type = down ? SDL_KEYDOWN : SDL_KEYUP;
        event.key.state = down ? SDL_PRESSED : SDL_RELEASED;
        event.key.keysym.scancode = (SDL_Scancode)binding->code;
        event.key.keysym.sym =
            SDL_GetKeyFromScancode((SDL_Scancode)binding->code);
        if (SDL_PushEvent(&event) != 1) {
            port_Say("--inject keys: SDL took no event: %s", SDL_GetError());
            port_Quit(PORT_EXIT_FAILED);
        }
    } else {
        SDL_Joystick *joystick;
        int result;

        if (g_port_controllers[pad] == NULL) {
            port_Say("--inject pads: player %d has no controller", pad + 1);
            port_Quit(PORT_EXIT_FAILED);
        }
        joystick = SDL_GameControllerGetJoystick(g_port_controllers[pad]);
        if (binding->source == PORT_SOURCE_BUTTON) {
            result = SDL_JoystickSetVirtualButton(
                joystick, binding->code, down ? SDL_PRESSED : SDL_RELEASED);
        } else {
            s32 is_trigger = binding->code == SDL_CONTROLLER_AXIS_TRIGGERLEFT ||
                             binding->code == SDL_CONTROLLER_AXIS_TRIGGERRIGHT;
            Sint16 rest = is_trigger ? SDL_JOYSTICK_AXIS_MIN : 0;
            Sint16 pushed = binding->direction > 0 ? SDL_JOYSTICK_AXIS_MAX
                                                   : SDL_JOYSTICK_AXIS_MIN;

            result = SDL_JoystickSetVirtualAxis(
                joystick, binding->code, down ? pushed : rest);
        }
        if (result != 0) {
            port_Say("--inject pads: SDL took no press: %s", SDL_GetError());
            port_Quit(PORT_EXIT_FAILED);
        }
    }
    g_port_injected++;
    return 1;
}

/* A vertical blank, before the pads are filled (port_platform.h). */
static void port_ReadInput(u32 blank) {
    SDL_Event event;
    s32 pad;

    (void)blank;
    port_OpenWindow();
    while (SDL_PollEvent(&event)) {
        switch (event.type) {
        case SDL_QUIT:
            port_Quit(PORT_EXIT_OK);
        case SDL_KEYDOWN:
        case SDL_KEYUP:
            port_TakeKey(&event.key);
            break;
        case SDL_CONTROLLERDEVICEADDED:
            port_AddController(event.cdevice.which);
            break;
        case SDL_CONTROLLERDEVICEREMOVED:
            port_RemoveController(event.cdevice.which);
            break;
        case SDL_CONTROLLERBUTTONDOWN:
        case SDL_CONTROLLERBUTTONUP:
            pad = port_FindController(event.cbutton.which);
            if (event.cbutton.button == SDL_CONTROLLER_BUTTON_GUIDE &&
                event.cbutton.state == SDL_PRESSED) {
                /* bound to no button of a pad: the settings menu */
                port_AskForMenu("the guide button");
            }
            if (pad >= 0) {
                g_port_controller_events++;
                port_ApplyInput(
                    &g_port_settings, PORT_SOURCE_BUTTON, pad,
                    event.cbutton.button, event.cbutton.state == SDL_PRESSED);
            }
            break;
        case SDL_CONTROLLERAXISMOTION:
            pad = port_FindController(event.caxis.which);
            if (pad >= 0) {
                g_port_controller_events++;
                port_ApplyInput(&g_port_settings, PORT_SOURCE_AXIS, pad,
                                event.caxis.axis, event.caxis.value);
            }
            break;
        default:
            break;
        }
    }
    port_SendPads();
}

/* ---- the pictures of one blank ---- */

/* The picture that is ready, shown. */
static void port_SwapPicture(void) {
    if (g_port_has_gl) {
        SDL_GL_SwapWindow(g_port_window);
    } else {
        SDL_RenderPresent(g_port_renderer);
    }
    g_port_pictures++;
    port_CountFps(&g_port_shown_meter, port_ReadClock(), 1);
}

/* A picture between the frame on show and the frame after it, at the
 * phase the clock has reached, ready to be shown. FALSE where there is
 * none: the frames are unlike, or nothing has been written down yet, and
 * what the window holds is then the game's own picture.
 *
 * The phase is where the clock is between the moment the frame on show
 * went up and the moment the frame after it will: 0 is the picture the
 * console itself would show, and the last phase is the frame the game
 * has already worked out and is about to put up. So nothing is guessed
 * ahead and nothing is held back — the picture is never later than the
 * console's own (docs/PORT.md). */
static s32 port_DrawTween(void) {
    s32 window_w = 0;
    s32 window_h = 0;
    s32 phase;

    if (!g_port_tween_on || !port_ShowsRemastered() ||
        port_RemasterIsHolding() || g_port_frame_period == 0 ||
        !port_TweenCanDraw()) {
        return FALSE;
    }
    phase = (s32)(((u64)(port_ReadClock() - g_port_frame_at) * PORT_TWEEN_ONE) /
                  g_port_frame_period);
    if (phase < 0) {
        phase = 0;
    }
    if (phase > PORT_TWEEN_ONE) {
        phase = PORT_TWEEN_ONE;
    }
    if (!port_TweenDraw(phase)) {
        return FALSE;
    }
    port_WindowSize(&window_w, &window_h);
    port_GlClearWindow(window_w, window_h);
    port_ShowRemastered(window_w, window_h);
    g_port_pictures_between++;
    return TRUE;
}

/* The pictures of this blank, the first of them the game's own, shown
 * when each is due; `due` is the clock's reading at which the blank
 * itself is.
 *
 * With `frame_rate original` there is one picture and it is shown at the
 * blank's own moment, which is what the window always did. Otherwise a
 * picture is shown every slot of the rate asked for, and the blank ends
 * when less than half a slot of it is left: with the display waited for,
 * the swap is the wait, and the pace takes up what a swap ran over
 * (pace.c). */
static void port_ShowPictures(u32 due) {
    s32 rate = port_PictureRate();
    s32 slot = rate > 0 ? PORT_SPU_RATE / rate : 0;
    s32 pictures = 0;
    s32 left;

    for (;;) {
        if (slot == 0) {
            left = (s32)(due - port_ReadClock());
            if (left > 0) {
                port_WaitFor((u32)left);
            }
        } else if (!g_port_vsync) {
            /* no display to wait for: the clock holds the rate */
            left = (s32)(g_port_next_picture - port_ReadClock());
            if (left > 0 && left <= slot) {
                port_WaitFor((u32)left);
            }
        }
        /* the picture about to be shown, where the player or a test
         * asked for a picture of the window */
        port_KeepShot(pictures);
        port_SwapPicture();
        g_port_next_picture = port_ReadClock() + (u32)slot;
        pictures++;
        left = (s32)(due - port_ReadClock());
        if (slot == 0 || pictures >= PORT_PICTURES_A_BLANK_MAX ||
            (left <= slot / 2 && !port_WantsLaterShot(pictures - 1))) {
            break;
        }
        if (!port_DrawTween()) {
            break;
        }
        port_DrawCounter();
    }
    if (g_port_wants_shot) {
        port_WriteShot();
    }
    /* The blank is not over until its own moment, however many pictures
     * were shown and whether or not any lay between two frames: the game
     * keeps the console's time (pace.c). */
    left = (s32)(due - port_ReadClock());
    if (left > 0) {
        port_WaitFor((u32)left);
    }
}

/* A vertical blank's end (port_platform.h): its pictures, when they are
 * due. */
static void port_ShowBlank(u32 blank) {
    u32 due;

    port_OpenWindow();
    if (g_port_has_gl) {
        /* the frame the card drew is finished before anything is shown,
         * where the software GPU's picture is complete too
         * (port_remaster.h) */
        u32 started = SDL_GetTicks();

        const char *news;

        port_RemasterShow();
        port_GlFinishFrame();
        port_GlNoteFrameTime((SDL_GetTicks() - started) * MILLISECONDS);
        /* which path the stage's ground takes (gl_stage.h) */
        news = port_GlStageNews();
        if (news != NULL) {
            port_Say("%s", news);
        }
    }
    if (g_port_tween_on && port_TweenGeneration() != g_port_tween_frames) {
        /* the game has drawn a frame: from this moment the frame that
         * was on show is the one before it, and a picture's phase is
         * counted from here */
        u32 now = port_ReadClock();

        if (g_port_tween_frames != 0) {
            g_port_frame_period = now - g_port_frame_at;
        }
        g_port_frame_at = now;
        g_port_tween_frames = port_TweenGeneration();
    }
    port_DrawPicture(blank, TRUE);
    port_WatchGame(blank);
    port_DrawCounter();
    if (g_port_window_options.tap_window != NULL &&
        blank % (u32)g_port_window_options.tap_every == 0) {
        port_TapWindow(blank);
    }
    if (g_port_window_options.shot_at > 0 &&
        blank == (u32)g_port_window_options.shot_at) {
        /* for a test, which cannot press a key that is no pad's button:
         * what F12 does, at one blank, and at the picture of that blank
         * the test asked for */
        g_port_wants_shot = TRUE;
        g_port_shot_picture = g_port_window_options.shot_picture;
    }
    if (g_port_window_options.toggle_at > 0 &&
        blank % (u32)g_port_window_options.toggle_at == 0) {
        /* for a test, which cannot press a key that is no pad's button:
         * what F2 does, at every Nth blank */
        port_ChangePicture("--toggle-at");
    }
    if (g_port_window_options.fault_at > 0 &&
        blank == (u32)g_port_window_options.fault_at) {
        port_FaultOnPurpose();
    }
    due = port_ReadClock();
    if (!g_port_window_options.is_fast) {
        s32 queue_error = 0;

        if (g_port_audio != 0) {
            SDL_LockAudioDevice(g_port_audio);
            queue_error = (s32)g_port_ring.count - (s32)g_port_ring.target;
            SDL_UnlockAudioDevice(g_port_audio);
        }
        due += port_PaceBlank(
            &g_port_pace, blank, due, port_GetSampleCount(), queue_error);
    }
    port_ShowPictures(due);
    g_port_blanks_shown++;
    /* the settings menu, where it was asked for (sdl_menu.c) */
    port_MaybeRunMenu(blank);
    if (g_port_vsync && port_PaceIsHeldBack(&g_port_pace)) {
        port_Say("the display holds the game back: it is no longer waited "
                 "for (vsync off); blank %u",
                 blank);
        g_port_vsync = 0;
        if (g_port_has_gl) {
            SDL_GL_SetSwapInterval(0);
        } else {
            SDL_RenderSetVSync(g_port_renderer, 0);
        }
    }
}

/* The program ends (port_platform.h): what the window did, SDL's leave,
 * and the log's last lines. */
static void port_CloseWindow(s32 code) {
    if (g_port_audio_tap != NULL) {
        fclose(g_port_audio_tap);
        g_port_audio_tap = NULL;
    }
    if (!g_port_is_open) {
        port_EndSession(code);
        return;
    }
    if (g_port_blanks_shown != 0) {
        u32 samples = port_ReadClock();

        port_Say("window: %u blanks shown in %u.%03u seconds; time let go %u "
                 "times, blanks moved %d samples in all",
                 g_port_blanks_shown, samples / PORT_SPU_RATE,
                 samples % PORT_SPU_RATE * MILLISECONDS / PORT_SPU_RATE,
                 g_port_pace.lost, g_port_pace.nudged);
    }
    port_Say("input: %u key events and %u controller events read, %u changes "
             "told to the pads; %u presses of a plan made as the PC's input",
             g_port_key_events, g_port_controller_events, g_port_pad_changes,
             g_port_injected);
    if (g_port_shots_taken != 0) {
        port_Say("pictures: %u of the window kept in %s (F12)",
                 g_port_shots_taken, SHOTS_FOLDER);
    }
    if (g_port_audio != 0) {
        SDL_CloseAudioDevice(g_port_audio);
        g_port_audio = 0;
        port_Say("sound: ran out %u times, %u samples thrown away",
                 g_port_ring.underruns, g_port_ring.dropped);
    }
    if (g_port_has_gl) {
        port_Say(
            "graphics: the %s picture at the end, %u changes of picture "
            "(F2); the scene %d times the console's resolution, %u "
            "vertices and %u draws in the last frame",
            port_RemasterIsOn() ? "remastered" : "original", g_port_toggles,
            g_port_gl_scale, port_GlGetVertexCount(), port_GlGetDrawCount());
        if (g_port_tween_on) {
            const PortTweenCounts *counts = port_TweenGetCounts();

            port_Say("frame_rate: %u pictures shown, %u of them between the "
                     "game's frames; %u frames written down, %u pictures not "
                     "drawn because the two frames were unlike, %u frames too "
                     "large to write down",
                     g_port_pictures, g_port_pictures_between, counts->frames,
                     counts->cuts, counts->overflowed);
            port_TweenStop();
        }
        port_DropGl();
    }
    port_EndSession(code);
    SDL_Quit();
    g_port_is_open = 0;
}

void port_StartWindow(void) {
    if (g_port_window_options.tap_audio != NULL) {
        g_port_audio_tap = fopen(g_port_window_options.tap_audio, "wb");
        if (g_port_audio_tap == NULL) {
            port_Say("--tap-audio: %s cannot be made",
                     g_port_window_options.tap_audio);
            exit(PORT_EXIT_FAILED);
        }
    }
    g_port_input_hook = port_ReadInput;
    g_port_device_hook = port_DeviceButtons;
    g_port_blank_hook = port_ShowBlank;
    g_port_sound_hook = port_QueueSound;
    g_port_quit_hook = port_CloseWindow;
    g_port_crash_hook = port_AfterCrash;
    g_port_disc_hook = port_NoteDisc;
    if (g_port_window_options.inject != PORT_INJECT_NONE) {
        g_port_press_hook = port_InjectPress;
    }
}
