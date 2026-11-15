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
 * PC's threads fall. */
#include <SDL.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "port_gpu.h"
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
/* The log's line every ten seconds, and its count of the sound running
 * out every minute, in vertical blanks. */
#define BEAT_BLANKS 600
#define MINUTE_BLANKS 3600

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
/* The buttons the port's pads have been told are down. */
static u32 g_port_sent[PORT_PADS];
/* For the end's account: a plan's presses made as the PC's input, the
 * key and controller events read, and the changes told to the pads. */
static u32 g_port_injected;
static u32 g_port_key_events;
static u32 g_port_controller_events;
static u32 g_port_pad_changes;

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

                if (binding->pad != pad || binding->button != button) {
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

/* What the picture runs on, for the log. */
static void port_NoteVideo(void) {
    SDL_RendererInfo info;
    SDL_DisplayMode mode;
    int w = 0;
    int h = 0;

    port_Note("video: driver %s", SDL_GetCurrentVideoDriver());
    if (SDL_GetRendererInfo(g_port_renderer, &info) == 0) {
        port_Note("video: renderer %s%s", info.name,
                  (info.flags & SDL_RENDERER_ACCELERATED) ? "" : " (software)");
    }
    if (SDL_GetCurrentDisplayMode(
            SDL_GetWindowDisplayIndex(g_port_window), &mode) == 0) {
        port_Note("video: display %d by %d, %d pictures a second", mode.w,
                  mode.h, mode.refresh_rate);
    }
    SDL_GetRendererOutputSize(g_port_renderer, &w, &h);
    port_Note("video: window %d by %d%s; vsync %s; scale %d, filter %s", w, h,
              g_port_is_fullscreen ? ", the whole screen" : "",
              g_port_vsync ? "on" : "off", g_port_settings.scale,
              g_port_settings.is_linear ? "linear" : "nearest");
}

/* The window, the sound card and the controllers, at the first blank:
 * the port has its memory and its disc by then. */
static void port_OpenWindow(void) {
    SDL_DisplayMode mode;
    Uint32 flags = SDL_WINDOW_RESIZABLE;

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
    if (g_port_settings.is_fullscreen) {
        flags |= SDL_WINDOW_FULLSCREEN_DESKTOP;
    }
    g_port_window = SDL_CreateWindow(
        WINDOW_TITLE, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        WINDOW_WIDTH, WINDOW_HEIGHT, flags);
    if (g_port_window == NULL) {
        port_Say("no window: %s", SDL_GetError());
        port_Quit(PORT_EXIT_FAILED);
    }
    g_port_is_fullscreen = g_port_settings.is_fullscreen;
    if (g_port_is_fullscreen) {
        SDL_ShowCursor(SDL_DISABLE);
    }
    g_port_vsync = g_port_settings.vsync && !g_port_window_options.is_fast;
    if (g_port_vsync &&
        SDL_GetCurrentDisplayMode(
            SDL_GetWindowDisplayIndex(g_port_window), &mode) == 0 &&
        mode.refresh_rate != 0 && mode.refresh_rate < REFRESH_MIN) {
        port_Say("the display shows %d pictures a second, fewer than the "
                 "game's 59.83: it is not waited for (vsync off)",
                 mode.refresh_rate);
        g_port_vsync = 0;
    }
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
    port_NoteVideo();
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

/* The displayed rectangle of video memory into the window: black where
 * the display is off or has no size. */
static void port_DrawPicture(u32 blank) {
    PortDisplay display;
    PortRect place;
    SDL_Rect where;
    s32 w;
    s32 h;
    int window_w = 0;
    int window_h = 0;

    port_GetDisplay(&display);
    w = display.w < PORT_PICTURE_WIDTH_MAX ? display.w : PORT_PICTURE_WIDTH_MAX;
    h = display.h < PORT_PICTURE_HEIGHT_MAX
            ? display.h
            : PORT_PICTURE_HEIGHT_MAX;
    SDL_SetRenderDrawColor(g_port_renderer, 0, 0, 0, SDL_ALPHA_OPAQUE);
    SDL_RenderClear(g_port_renderer);
    if (w <= 0 || h <= 0) {
        return;
    }
    port_ConvertDisplay(g_port_vram, display.x, display.y, w, h,
                        display.is_rgb24, g_port_frame);
    if (g_port_window_options.tap_frames != NULL &&
        blank % (u32)g_port_window_options.tap_every == 0) {
        port_TapFrame(blank, w, h, display.is_on);
    }
    if (!display.is_on) {
        return;
    }
    port_SizeTexture(w, h);
    SDL_UpdateTexture(g_port_texture, NULL, g_port_frame, w * BYTES_PER_PIXEL);
    SDL_GetRendererOutputSize(g_port_renderer, &window_w, &window_h);
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
static void port_DrawCounter(void) {
    char text[PORT_FPS_TEXT_BYTES];
    PortRect picture;
    PortRect place;
    SDL_Rect from;
    SDL_Rect where;
    s32 text_w;
    s32 i;
    int window_w = 0;
    int window_h = 0;

    if (!g_port_show_fps) {
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
    port_FormatFps(text, g_port_shown_meter.tenths, g_port_game_meter.tenths);
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
    SDL_GetRendererOutputSize(g_port_renderer, &window_w, &window_h);
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
    port_CountFps(&g_port_shown_meter, clock, 1);
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

/* ---- the pads ---- */

/* Tells the port's pads what has changed since they were last told. */
static void port_SendPads(void) {
    s32 pad;
    u32 bit;

    for (pad = 0; pad < PORT_PADS; pad++) {
        u32 held = port_GetHeldButtons(&g_port_settings, pad);
        u32 changed = held ^ g_port_sent[pad];

        for (bit = 1; changed != 0; bit <<= 1) {
            if (changed & bit) {
                port_SetPadButton(pad, bit, (held & bit) != 0);
                g_port_pad_changes++;
                changed &= ~bit;
            }
        }
        g_port_sent[pad] = held;
    }
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
    if (key->keysym.scancode == SDL_SCANCODE_F3) {
        if (down) {
            g_port_show_fps = !g_port_show_fps;
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

/* A vertical blank's end (port_platform.h): its picture, when it is
 * due. */
static void port_ShowBlank(u32 blank) {
    port_OpenWindow();
    port_DrawPicture(blank);
    port_WatchGame(blank);
    port_DrawCounter();
    if (g_port_window_options.fault_at > 0 &&
        blank == (u32)g_port_window_options.fault_at) {
        port_FaultOnPurpose();
    }
    if (!g_port_window_options.is_fast) {
        s32 queue_error = 0;

        if (g_port_audio != 0) {
            SDL_LockAudioDevice(g_port_audio);
            queue_error = (s32)g_port_ring.count - (s32)g_port_ring.target;
            SDL_UnlockAudioDevice(g_port_audio);
        }
        port_WaitFor(port_PaceBlank(&g_port_pace, blank, port_ReadClock(),
                                    port_GetSampleCount(), queue_error));
    }
    SDL_RenderPresent(g_port_renderer);
    g_port_blanks_shown++;
    if (g_port_vsync && port_PaceIsHeldBack(&g_port_pace)) {
        port_Say("the display holds the game back: it is no longer waited "
                 "for (vsync off); blank %u",
                 blank);
        g_port_vsync = 0;
        SDL_RenderSetVSync(g_port_renderer, 0);
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
    if (g_port_audio != 0) {
        SDL_CloseAudioDevice(g_port_audio);
        g_port_audio = 0;
        port_Say("sound: ran out %u times, %u samples thrown away",
                 g_port_ring.underruns, g_port_ring.dropped);
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
    g_port_blank_hook = port_ShowBlank;
    g_port_sound_hook = port_QueueSound;
    g_port_quit_hook = port_CloseWindow;
    g_port_crash_hook = port_AfterCrash;
    g_port_disc_hook = port_NoteDisc;
    if (g_port_window_options.inject != PORT_INJECT_NONE) {
        g_port_press_hook = port_InjectPress;
    }
}
