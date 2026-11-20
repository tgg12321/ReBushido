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
 *     go, Escape is the game's pause or its way back on player one's
 *     pad (port_EscapeButton), and the window's close ends the program;
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
 * window"). Where the PC gives an OpenGL 4.5 core context, the window is
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
#include "gl_bench.h"
#include "gl_fighter.h"
#include "gl_light_list.h"
#include "gl_load.h"
#include "gl_pause.h"
#include "gl_record.h"
#include "gl_sky.h"
#include "gl_stage.h"
#include "gl_tween.h"
#include "menu.h"
#include "port_gpu.h"
#include "port_host.h"
#include "port_movie.h"
#include "port_music.h"
#include "port_online.h"
#include "port_paint.h"
#include "port_profile.h"
#include "port_remaster.h"
#include "port_spu.h"
#include "port_play.h"
#include "port_stage_choice.h"
#include "port_fight_music.h"
#include "port_match.h"
#include "port_top_menu.h"
#include "shared.h"
#include "portmap.h"
#include "sdl_window.h"
#include "update.h"

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
/* The sound card is asked for this many samples at a time (12 ms), and
 * the queue before it is kept this long (81 ms) of the room it has
 * (docs/PORT.md, "Sound that does not run out"). The samples of a blank
 * are made at its end, so the queue holds what the card plays while the
 * next blank is worked out (a blank, 737), what it may take in one gulp,
 * and what is left for a picture or a step that runs late: 53 ms. The
 * queue and the card's gulp together are what the queue and the gulp
 * were before (58 + 23 ms), less one gulp's worth of waiting in the
 * card's own buffer. */
#define AUDIO_DEVICE_FRAMES 512
#define AUDIO_QUEUE_FRAMES 3584
#define AUDIO_RING_FRAMES 8192
/* Where the sound runs out all the same, a PC whose hitches are longer:
 * the queue is made this much longer each time, up to the most (109 ms),
 * and the log says why the first few times. */
#define AUDIO_QUEUE_STEP 256
#define AUDIO_QUEUE_MOST 4800
#define UNDERRUNS_TOLD 20
#define LOST_NEAR 8
/* A frame of the game is a blank in a menu and two in a fight; one that
 * the blanks' moments say took more than this many is a jump of them. */
#define FRAME_PERIOD_MOST 4
/* Netplay's rollback takes back what the queue holds of the blanks it
 * runs again, but this much (23 ms) is left to play while it runs; the
 * pieces of sound of the last blanks are remembered. */
#define RERUN_KEEP_FRAMES 1024
#define QUEUED_HISTORY 128
#define RERUN_FRAMES 8192
/* A blank's samples, 44,100 a second at the console's 59.83 blanks. */
#define BLANK_SAMPLES 737
/* The last of a wait is not slept, for the system wakes a sleeper late:
 * this many samples' time (3 ms) is watched out on the clock. */
#define WAIT_AWAKE 132
#define MILLISECONDS 1000
#define MICROSECONDS 1000000
#define PATH_BYTES_MAX 1024
#define BYTES_PER_PIXEL 4
#define TAP_MAGIC 0x52464252U /* "RBFR", the low byte first */
/* The OpenGL the remastered picture asks for: 4.5 core (compute shaders,
 * direct state access, persistent mapped buffers). */
#define GL_WANTED_MAJOR 4
#define GL_WANTED_MINOR 5
/* The counter in the corner: its own few pixels, a dark box under white
 * digits, drawn over the picture SDL has been handed already; with
 * `fps = times`, three lines more under it (port_DrawCounter). */
#define COUNTER_W 300
#define COUNTER_LINE_H (PORT_GLYPH_H + COUNTER_PAD)
#define COUNTER_LINES 4
#define COUNTER_H (PORT_GLYPH_H + 2 * COUNTER_PAD)
#define COUNTER_H_MAX (COUNTER_H + (COUNTER_LINES - 1) * COUNTER_LINE_H)
#define COUNTER_TEXT_BYTES 56
/* The times are the means of the blanks in which the game drew a frame,
 * over this many blanks (half a second), and the parts each line names. */
#define TIMES_BLANKS 30
#define TIMES_NAMED 4
#define COUNTER_PAD 2
#define COUNTER_BOX 0xB0000000U
#define COUNTER_INK 0xFFFFFFFFU
/* The picture's lines a unit of the counter's box is a pixel of
 * (port_PlaceCounter), and the typeface's size for capitals as tall as
 * the digits' rows: Shippori Mincho's capitals are 737 of its 1448 units
 * from the top of the tallest letter to the bottom of the deepest. */
#define COUNTER_PICTURE_LINES 240
#define COUNTER_SIZE_PERCENT 196
#define COUNTER_PERCENT 100
/* The player's own pictures of the window (F12): the folder they go in,
 * beside the program, and how the files are named. */
#define SHOTS_FOLDER "screenshots"
#define SHOT_EPOCH_YEAR 1900
#define SHOT_NUMBERS 1000
/* The log's line every ten seconds, and its count of the sound running
 * out every minute, in vertical blanks. */
#define BEAT_BLANKS 600
#define MINUTE_BLANKS 3600
/* quick_loading: how long a loading screen the player reads (story
 * mode's card of the next stage, the team battle's scores) is up at the
 * console's pace before its load is hurried, in blanks: two seconds */
#define READ_BLANKS 120
/* How long a hurried load may go without reading the disc before it is
 * paced again (g_port_load_stalled), in seconds. */
#define STALL_SECONDS 3
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
/* What was queued at each of the last blanks, for netplay's rollback
 * (port_SoundRerunBegins): a ring of the blank and the pairs. */
static u32 g_port_queued_blank[QUEUED_HISTORY];
static u32 g_port_queued_frames[QUEUED_HISTORY];
static u32 g_port_queued_count;
/* A rollback's sound: what it makes, the newest RERUN_FRAMES pairs of it
 * kept round a ring; how many it took back from the queue, which is how
 * many of the newest it puts there; and the account. */
static s16 g_port_rerun_samples[RERUN_FRAMES * 2];
static u32 g_port_rerun_made;
static u32 g_port_rerun_room;
static s32 g_port_is_rerunning;
static u32 g_port_rerun_taken;
static u32 g_port_rerun_heard;
static u32 g_port_rerun_lost;
/* The pixels laid over a picture where SDL's renderer draws
 * (port_LayPixels). */
static SDL_Texture *g_port_lay_texture;
static s32 g_port_lay_w;
static s32 g_port_lay_h;

static PortPace g_port_pace;
static Uint64 g_port_clock_start;
/* --profile: where each blank's time went and when each picture was
 * shown (port_profile.h), and the phase of the picture being drawn: -1
 * for the game's own */
static FILE *g_port_profile_file;
static s32 g_port_profile_phase = -1;
static u32 g_port_blanks_shown;
static s32 g_port_show_fps;
static SDL_Texture *g_port_counter;
static u32 g_port_counter_pixels[COUNTER_W * COUNTER_H_MAX];

/* The times under the counter (`fps = times`): each part's microseconds
 * summed over the blanks in which the game drew a frame, the processor's
 * and the card's, as they are being counted and as last shown; and, by
 * the blank's low bits, which blanks drew a frame, for the card's times,
 * which come a few blanks late. */
typedef struct {
    u32 cpu_blanks;
    u32 cpu[PORT_PROFILE_PARTS];
    u32 cpu_most;
    u32 gpu_blanks;
    u32 gpu[PORT_GPU_PASSES];
    u32 gpu_most;
} PortTimes;

static PortTimes g_port_times;
static PortTimes g_port_times_shown;
static u32 g_port_times_count;
static u8 g_port_frame_blanks[64];
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
/* When the pictures are shown (pace.c, "when a picture is shown"): the
 * display's refreshes as the swaps say; the moment the last blank was
 * due and the moment its pictures were done; how long after its frame's
 * moment a frame's first picture is on the screen (`lag`); and, kept at
 * their recent peaks, what a picture between two frames takes to draw
 * and how long after a blank's pictures the next blank's first is ready
 * to be shown (the game's step and that picture). */
static PortRefresh g_port_refresh;
static u32 g_port_last_due;
static u32 g_port_left_at;
static u32 g_port_lag;
static s32 g_port_has_lag;
static s32 g_port_wants_lag;
static u32 g_port_draw_cost;
static u32 g_port_ready_after;
/* The step's refresh (docs/PORT.md, "The step's refresh"): on unless
 * RB_STEP_REFRESH=off; the refresh the picture shown before the step was
 * handed over for, where the swap after it is still to come (0: none);
 * and how many steps had one, and how many of those the step's own first
 * picture would have made in time after all. */
static s32 g_port_step_refresh_on = TRUE;
static u32 g_port_queued_for;
/* What a held step and its first picture take, from the boundary's read
 * of the input, on average (a peak held too many steps to it that would
 * have been in time); the step begun and not yet shown,
 * when it began, and what the picture shown before it took. */
static u32 g_port_step_ready;
static s32 g_port_step_pending;
static u32 g_port_step_began;
static u32 g_port_step_spent;
static u32 g_port_step_refreshes;
static u32 g_port_step_refresh_early;
/* A step run beside the window's thread (docs/PORT.md, "The step on a
 * thread of its own"): when it ended, and what its record and first
 * picture take after that, on average. */
static u32 g_port_step_done_at;
static s32 g_port_step_done;
static u32 g_port_after_ready;
/* The clock's samples waited for since the last picture was handed to
 * the display, for --profile's `p` line. */
static u32 g_port_waited;
/* ... and all the clock's samples waited for, never reset (the input read
 * late: what a frame's first picture took, less its waits). */
static u32 g_port_waited_all;
/* When the picture about to be handed over began to be drawn, in
 * microseconds as the `p` lines (docs/PORT.md, "Input lag"). */
static Uint64 g_port_picture_began;
static u32 g_port_pictures;
static u32 g_port_pictures_between;
/* Blanks that showed nothing for being more than a refresh late
 * (port_ShowPictures), in all, and whether the last one did. */
static u32 g_port_pictures_unshown;
static s32 g_port_unshown;
/* The game as the log last saw it, and what the log has counted. */
static PortGameState g_port_seen;
static s32 g_port_has_seen;
static u32 g_port_lost_noted;
static u32 g_port_underruns_noted;
/* The sound's running out as each blank saw it (port_WatchSound): the
 * count, how many were told of one by one, and the queue's length after
 * the last blank's samples. */
static u32 g_port_underruns_seen;
static u32 g_port_underruns_told;
static u32 g_port_queue_seen;
/* ... and the last blank at which time was let go: the sound running out
 * within LOST_NEAR blanks of it is that, which no queue holds. */
static u32 g_port_lost_counted;
static u32 g_port_lost_blank;
/* What the last blank's time went to, by part, in microseconds: what the
 * log says where time is let go or the sound runs out. */
static u32 g_port_last_ticks[PORT_PROFILE_PARTS];
/* A report (F9): asked for, and its folder while its picture is kept. */
static s32 g_port_wants_report;
static char g_port_report_folder[PATH_BYTES_MAX];
static u32 g_port_read_start;
/* The load under way (port_LoadKind): its first blank, how many blanks
 * it has taken, how many of them were hurried (quick_loading) and the
 * clock as it began; and in the whole run, the loads and their blanks. */
static u32 g_port_load_first;
static u32 g_port_load_blanks;
static u32 g_port_load_hurried;
static u32 g_port_load_began;
static u32 g_port_hurry_shown;
static u32 g_port_loads;
static u32 g_port_loads_hurried;
static u32 g_port_load_blanks_all;
static u32 g_port_load_ms_all;
/* The loading screen the player reads that is up (port_ReadScreen), and
 * how many of a load's blanks it has been shown for at the console's
 * pace. */
static s32 g_port_read_screen;
static u32 g_port_read_blanks;
/* A load hurried through that reads nothing is not hurried any more: the
 * clock at its last read of the disc (or its last blank at the console's
 * pace), and whether it has gone STALL_SECONDS since and is paced to its
 * end. A load that never ends is a hang, whose blanks hurried would race
 * thousands a second and its sound with them. */
static u32 g_port_load_read_at;
static s32 g_port_load_stalled;

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
/* The button of player one's pad that Escape holds while it is down,
 * chosen as it went down (port_EscapeButton); 0 while it is up. */
static u32 g_port_escape_held;
/* --inject: a plan's presses, made as the PC's input at the next read of
 * the events at a blank (port_ReadInput), as they always were, and never
 * at the read just before the game takes its input */
#define INJECT_WAITING_MAX 64

typedef struct {
    s32 pad;
    u32 button;
    s32 down;
} PortInjectWaiting;

static PortInjectWaiting g_port_inject_waiting[INJECT_WAITING_MAX];
static s32 g_port_inject_waiting_count;
/* Run-ahead (step.c; docs/PORT.md, "Input lag"): the display the frames
 * run ahead left, which is what is shown while the step itself runs; its
 * pixels are in g_port_frame. */
static PortDisplay g_port_ahead_display;
/* The settings menu's own texture, where SDL's renderer draws. */
static SDL_Texture *g_port_menu_texture;
static s32 g_port_menu_texture_w;
static s32 g_port_menu_texture_h;

static void port_SwapPicture(void);
static void port_NoteStepRefresh(u32 now, u32 shown_for);
static void port_LateRead(void);
static void port_NoteLateDrawn(void);
static void port_NoteLateShown(void);
static void port_ReadInput(u32 blank);
static s32 port_OnStepper(void);
static SDL_Thread *g_port_stepper;
static s32 port_RunsBeside(void);
static void port_FlushRecord(void);
static s32 port_WatchSound(u32 blank, s32 is_hurried);
static void port_MakeReport(void);
#if !defined(_WIN32)
/* The window's thread's own processor time, in the host's ticks: what
 * --profile counts by where RB_PROFILE_CPU says (port_profile.h). */
static u32 port_ThreadTicks(void) {
    struct timespec now;

    clock_gettime(CLOCK_THREAD_CPUTIME_ID, &now);
    return (u32)now.tv_sec * 10000000U + (u32)now.tv_nsec / 100U;
}
#else
/* the same, from the host (host_windows.c) */
extern u32 port_HostThreadTicks(void);
#define port_ThreadTicks port_HostThreadTicks
#endif
static void port_ShownDisplay(PortDisplay *display);
static void port_CountTimes(void);

/* ---- names ---- */

s32 port_LookUpName(s32 source, const char *name) {
    switch (source) {
    case PORT_SOURCE_KEY: {
        SDL_Scancode code = SDL_GetScancodeFromName(name);

        if (code == SDL_SCANCODE_ESCAPE) {
            /* the window's own: pause and back (port_EscapeButton) */
            return PORT_NAME_RESERVED;
        }
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

/* The PC's clock in microseconds since the window opened: --profile's
 * lines. */
static Uint64 port_Micros(void) {
    return (SDL_GetPerformanceCounter() - g_port_clock_start) * MICROSECONDS /
           SDL_GetPerformanceFrequency();
}

/* Waits `samples` of the clock: most of it asleep, the end awake. */
static void port_WaitFor(u32 samples) {
    u32 began = port_ReadClock();
    u32 until = began + samples;
    s32 was = port_ProfileSwitch(PORT_PROFILE_WAIT);

    for (;;) {
        s32 left = (s32)(until - port_ReadClock());

        if (left <= 0) {
            g_port_waited += port_ReadClock() - began;
            g_port_waited_all += port_ReadClock() - began;
            port_ProfileSwitch(was);
            return;
        }
        if (left > WAIT_AWAKE) {
            SDL_Delay(
                (Uint32)(left - WAIT_AWAKE) * MILLISECONDS / PORT_SPU_RATE);
        }
    }
}

/* --display-rate: the next blank of the display made up for a test,
 * which a swap with vsync on waits for. */
static void port_WaitForMadeUpBlank(void) {
    Uint64 frequency = SDL_GetPerformanceFrequency();
    Uint64 period = frequency / (Uint64)g_port_window_options.display_rate;
    Uint64 since = SDL_GetPerformanceCounter() - g_port_clock_start;
    Uint64 next = (since / period + 1) * period;

    /* a picture handed over for a refresh without waiting for it
     * (port_HandPicture) is shown at that refresh: this one at the next */
    if (g_port_queued_for != 0) {
        Uint64 queued =
            (Uint64)g_port_queued_for * frequency / (Uint64)PORT_SPU_RATE;

        if (next < queued + period / 2) {
            next = (queued + period / 2) / period * period + period;
        }
    }

    for (;;) {
        Uint64 now = SDL_GetPerformanceCounter() - g_port_clock_start;
        Uint64 left_ms;

        if (now >= next) {
            return;
        }
        left_ms = (next - now) * MILLISECONDS / frequency;
        if (left_ms > 2) {
            SDL_Delay((Uint32)(left_ms - 2));
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

/* Whether quick_loading is on for this run (sdl_window.h). */
static s32 port_QuickLoading(void) {
    switch (g_port_window_options.quick_loading) {
    case PORT_QUICK_OFF:
        return FALSE;
    case PORT_QUICK_ON:
        return TRUE;
    default:
        return g_port_settings.quick_loading;
    }
}

/* Whether this blank is one of a load's that is hurried through: behind
 * a loading picture, at once; behind a screen the player reads, once it
 * has been up for READ_BLANKS at the console's pace. */
static s32 port_HurriesBlank(void) {
    s32 kind;

    if (!port_QuickLoading() || g_port_load_stalled) {
        return FALSE;
    }
    kind = port_LoadKind();
    return kind == PORT_LOAD_PICTURE ||
           (kind == PORT_LOAD_READ && g_port_read_blanks >= READ_BLANKS);
}

/* A blank's samples, as they are made (port_spu.h). */
static void port_QueueSound(const s16 *samples, u32 frames) {
    u32 slot = g_port_queued_count % QUEUED_HISTORY;

    if (g_port_audio_tap != NULL) {
        fwrite(samples, 2 * sizeof(s16), frames, g_port_audio_tap);
    }
    g_port_queued_blank[slot] = port_GetBlankCount();
    g_port_queued_frames[slot] = frames;
    g_port_queued_count++;
    if (g_port_audio != 0) {
        SDL_LockAudioDevice(g_port_audio);
        /* A hurried blank's sound goes to the card only while the queue
         * is short of what it should hold: the card is not given seconds
         * of a load's sound in a moment, nor left with nothing to play
         * as the load ends. A load's sound is the loading picture's
         * silence, or the end of a sound that was playing as it began. */
        if (!port_HurriesBlank() || g_port_ring.count < g_port_ring.target) {
            port_WriteRing(&g_port_ring, samples, frames);
        }
        SDL_UnlockAudioDevice(g_port_audio);
    }
}

/* ---- the sound of netplay's rollback (sdl_window.h) ---- */

/* The last `blanks` blanks are about to be made again on a corrected
 * course: what the queue still holds of them, all but RERUN_KEEP_FRAMES
 * of the queue, is taken back, and as many of the newest corrected pairs
 * go in its place when the rollback ends (port_SoundRerunEnds). What the
 * card has played of them already stays played: nothing is heard twice,
 * and the queue is as long as it was. (The corrected course may take
 * other blanks to come to the same frame, a load more or less; its
 * newest pairs are still the ones for now.) */
void port_SoundRerunBegins(u32 blanks) {
    u32 now = port_GetBlankCount();
    u32 made = 0;
    u32 taken = 0;
    u32 i;

    for (i = 0; i < QUEUED_HISTORY && i < g_port_queued_count; i++) {
        u32 slot = (g_port_queued_count - 1 - i) % QUEUED_HISTORY;

        if (now - g_port_queued_blank[slot] >= blanks) {
            break;
        }
        made += g_port_queued_frames[slot];
    }
    if (g_port_audio != 0) {
        SDL_LockAudioDevice(g_port_audio);
        if (g_port_ring.count > RERUN_KEEP_FRAMES) {
            u32 room = g_port_ring.count - RERUN_KEEP_FRAMES;

            taken = port_TakeBackRing(&g_port_ring, made < room ? made : room);
        }
        SDL_UnlockAudioDevice(g_port_audio);
    }
    g_port_rerun_room = taken;
    g_port_rerun_made = 0;
    g_port_rerun_taken += taken;
    g_port_rerun_lost += made - taken;
    g_port_is_rerunning = TRUE;
}

/* A rollback's pairs (port_spu.h), kept until it ends. */
void port_SoundRerun(const s16 *samples, u32 frames) {
    u32 i;

    if (!g_port_is_rerunning) {
        return;
    }
    for (i = 0; i < frames; i++) {
        u32 at = (g_port_rerun_made + i) % RERUN_FRAMES;

        g_port_rerun_samples[at * 2] = samples[i * 2];
        g_port_rerun_samples[at * 2 + 1] = samples[i * 2 + 1];
    }
    g_port_rerun_made += frames;
}

/* The rollback has ended: its newest pairs, as many as were taken back,
 * into the queue. */
void port_SoundRerunEnds(void) {
    u32 count = g_port_rerun_room;
    u32 first;
    u32 i;

    if (!g_port_is_rerunning) {
        return;
    }
    g_port_is_rerunning = FALSE;
    if (count > g_port_rerun_made) {
        count = g_port_rerun_made;
    }
    if (count > RERUN_FRAMES) {
        count = RERUN_FRAMES;
    }
    if (count == 0 || g_port_audio == 0) {
        return;
    }
    first = g_port_rerun_made - count;
    g_port_rerun_heard += count;
    SDL_LockAudioDevice(g_port_audio);
    for (i = 0; i < count; i++) {
        port_WriteRing(
            &g_port_ring,
            &g_port_rerun_samples[((first + i) % RERUN_FRAMES) * 2], 1);
    }
    SDL_UnlockAudioDevice(g_port_audio);
}

/* The blanks from here on are later by `blanks` (sdl_window.h). */
void port_HoldPace(u32 blanks) {
    port_PaceHold(&g_port_pace, blanks * BLANK_SAMPLES);
}

static void port_OpenAudio(void) {
    SDL_AudioSpec want;
    SDL_AudioSpec have;

    /* a window never shown is a test's or a tool's: it paces by SDL's
     * dummy device and never sounds on the PC's own (the owner,
     * 2026-11-19, heard the agents' hidden runs) */
    if (g_port_window_options.is_hidden) {
        SDL_SetHintWithPriority(
            SDL_HINT_AUDIODRIVER, "dummy", SDL_HINT_OVERRIDE);
    }
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
    options->is_stage_props = g_port_settings.stage_props_mesh;
    options->is_fighter_models = g_port_settings.fighter_models;
    /* the scene held against the ground's own depth, which is part of
     * drawing the ground as one mesh and has no setting of its own
     * (gl.h) */
    options->is_stage_depth = TRUE;
    /* the fighters' shadows as a darkening of the ground, or the game's
     * own black polygons (src/port/gl/gl_shadow.h) */
    options->shadows = g_port_settings.shadows;
    options->is_smooth_light = g_port_settings.smooth_light;
    options->is_lights = g_port_settings.lights;
    /* the lighting's finish (gl_post.h) */
    options->is_reflections = g_port_settings.reflections;
    options->is_ambient_occlusion = g_port_settings.ambient_occlusion;
    options->is_bloom = g_port_settings.bloom;
    options->is_tonemap = g_port_settings.tonemap;
    options->effects = g_port_settings.effects;
    /* the sky and the walls seen through (gl_sky.h, gl_see.h) */
    options->sky = g_port_settings.sky;
    options->see_through = g_port_settings.see_through;
    /* the lights beside the stage's own, which are no option of the
     * backend's: told to their list here (gl_light_list.h) */
    port_GlLightListSetStageOn(g_port_settings.stage_lights);
    port_GlLightListSetEffectsOn(g_port_settings.effect_lights);
}

/* ---- SDL's threads, lent to the reading of a mod's pictures (gl_load.h) */

typedef struct {
    s32 (*body)(void *arg);
    void *arg;
} PortThreadStart;

static int SDLCALL port_ThreadBody(void *data) {
    PortThreadStart start = *(PortThreadStart *)data;

    free(data);
    return start.body(start.arg);
}

static s32 port_ThreadStart(s32 (*body)(void *arg), void *arg) {
    PortThreadStart *start = malloc(sizeof(*start));
    SDL_Thread *thread;

    if (start == NULL) {
        return FALSE;
    }
    start->body = body;
    start->arg = arg;
    thread = SDL_CreateThread(port_ThreadBody, "rebushido-read", start);
    if (thread == NULL) {
        free(start);
        return FALSE;
    }
    /* below the game's own thread, which a picture read late does not
     * hold up */
    SDL_DetachThread(thread);
    return TRUE;
}

static void *port_ThreadMakeLock(void) { return SDL_CreateMutex(); }

static void port_ThreadLock(void *lock) { SDL_LockMutex(lock); }

static void port_ThreadUnlock(void *lock) { SDL_UnlockMutex(lock); }

static void *port_ThreadMakeSignal(void) { return SDL_CreateSemaphore(0); }

static void port_ThreadWait(void *signal) { SDL_SemWait(signal); }

static void port_ThreadPost(void *signal) { SDL_SemPost(signal); }

/* Threads to read a mod's pictures on, beside the game: a quarter of the
 * processors, two at the least and six at the most. Once. */
static void port_LendThreads(void) {
    static const PortGlThreads threads = {
        port_ThreadStart,  port_ThreadMakeLock,   port_ThreadLock,
        port_ThreadUnlock, port_ThreadMakeSignal, port_ThreadWait,
        port_ThreadPost};
    static s32 is_lent;
    s32 count = SDL_GetCPUCount() / 4;

    if (is_lent) {
        return;
    }
    is_lent = TRUE;
    count = count < 2 ? 2 : count > 6 ? 6 : count;
    port_GlLoadSetThreads(&threads, count);
    if (port_GlLoadIsBeside()) {
        port_Note("graphics: a mod's pictures are read on %d threads beside "
                  "the game",
                  count);
    }
}

/* The window's context and the remastered renderer on it: NULL, or why
 * there is none. The window must have been made with SDL_WINDOW_OPENGL.
 * Nothing of this changes what the game computes: with no card the port
 * is what it was. */
/* The display waited for as the settings say: with `vsync = adaptive`
 * the driver's late swap tear (SDL's -1), where it has one; where it has
 * none, waited for as `on` does, and the log says so. */
static void port_SetSwapInterval(void) {
    if (g_port_window_options.display_rate > 0) {
        /* --display-rate: the display made up is the one waited for
         * (port_WaitForMadeUpBlank), not the PC's own, which a hidden
         * window on a real card is held to as well */
        SDL_GL_SetSwapInterval(0);
        return;
    }
    if (g_port_vsync == PORT_VSYNC_ADAPTIVE &&
        SDL_GL_SetSwapInterval(-1) == 0) {
        return;
    }
    if (g_port_vsync == PORT_VSYNC_ADAPTIVE) {
        port_Note("video: the driver has no adaptive vsync (%s): waited for "
                  "as with vsync on",
                  SDL_GetError());
    }
    SDL_GL_SetSwapInterval(g_port_vsync != 0 ? 1 : 0);
}

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
    port_SetSwapInterval();
    g_port_gl_scale = port_WantedScale();
    port_GlOptionsFor(g_port_gl_scale, &options);
    g_port_gl_tall = options.tall;
    port_LendThreads();
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

#if defined(_WIN32)
/* A hidden window's program keeps the clock's fine waits: since Windows
 * 11 a program none of whose windows can be seen is held to the system's
 * coarse timer (15.6 ms) whatever it asked for, so a wait of a millisecond
 * or two in a paced run on the card (--hidden) slept a refresh and more
 * (docs/PORT.md, "The step's refresh"). The call is Windows 11's own
 * (SetProcessInformation, ProcessPowerThrottling,
 * PROCESS_POWER_THROTTLING_IGNORE_TIMER_RESOLUTION); an older system has
 * none and needs none. */
#define POWER_THROTTLING_CLASS 4
#define POWER_THROTTLING_VERSION 1
#define POWER_THROTTLING_IGNORE_TIMER_RESOLUTION 0x4

static void port_KeepFineTimer(void) {
    typedef struct {
        u32 version;
        u32 control;
        u32 state;
    } Throttling;

    typedef int (*SetInformation)(
        void *process, int what, void *data, u32 size);
    void *kernel = SDL_LoadObject("kernel32.dll");
    SetInformation set;
    Throttling throttling;

    if (kernel == NULL) {
        return;
    }
    set = (SetInformation)SDL_LoadFunction(kernel, "SetProcessInformation");
    if (set == NULL) {
        return;
    }
    throttling.version = POWER_THROTTLING_VERSION;
    throttling.control = POWER_THROTTLING_IGNORE_TIMER_RESOLUTION;
    throttling.state = 0;
    /* the current process: the handle (HANDLE)-1 */
    if (!set((void *)(intptr_t)-1, POWER_THROTTLING_CLASS, &throttling,
             sizeof(throttling))) {
        port_Note("video: a hidden window's fine timer is not kept");
    }
}
#endif

/* The window itself, with a context for the graphics card or without
 * one. */
static void port_MakeWindow(s32 wants_gl) {
    Uint32 flags = SDL_WINDOW_RESIZABLE;

    if (g_port_window_options.is_hidden) {
        flags |= SDL_WINDOW_HIDDEN;
#if defined(_WIN32)
        port_KeepFineTimer();
#endif
    } else if (g_port_settings.is_fullscreen) {
        flags |= SDL_WINDOW_FULLSCREEN_DESKTOP;
    }
    if (wants_gl) {
        flags |= SDL_WINDOW_OPENGL;
        /* the remastered picture's least (docs/PORT.md, "Measuring on the
         * card"): a PC without it shows the console's own picture */
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, GL_WANTED_MAJOR);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, GL_WANTED_MINOR);
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
    g_port_is_fullscreen =
        g_port_settings.is_fullscreen && !g_port_window_options.is_hidden;
    if (g_port_is_fullscreen) {
        SDL_ShowCursor(SDL_DISABLE);
    }
    g_port_vsync = g_port_window_options.is_fast ? 0 : g_port_settings.vsync;
    if (SDL_GetCurrentDisplayMode(
            SDL_GetWindowDisplayIndex(g_port_window), &mode) == 0) {
        g_port_display_rate = mode.refresh_rate;
    }
    if (g_port_window_options.display_rate > 0) {
        g_port_display_rate = g_port_window_options.display_rate;
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
                     "nothing). It needs OpenGL %d.%d.",
                     GL_WANTED_MAJOR, GL_WANTED_MINOR);
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
    if (g_port_window_options.bench_stress != NULL) {
        if (!port_GlBenchStress(g_port_window_options.bench_stress)) {
            port_Say("--bench-stress: %s is not fighters=N,stage=M",
                     g_port_window_options.bench_stress);
            port_Quit(PORT_EXIT_FAILED);
        }
        port_Note("bench: each fighter drawn %d times, the ground %d times",
                  port_GlBenchFighterCopies(), port_GlBenchStageCopies());
    }
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
    if (g_port_window_options.profile != NULL) {
        u32 ticks[PORT_PROFILE_PARTS];
        s32 i;

        g_port_profile_file = fopen(g_port_window_options.profile, "w");
        if (g_port_profile_file == NULL) {
            port_Say(
                "--profile: %s cannot be made", g_port_window_options.profile);
            port_Quit(PORT_EXIT_FAILED);
        }
        fprintf(g_port_profile_file, "parts");
        for (i = 0; i < PORT_PROFILE_PARTS; i++) {
            fprintf(g_port_profile_file, " %s", port_ProfileName(i));
        }
        fputc('\n', g_port_profile_file);
        /* the card's passes, a `g` line for each blank it has finished */
        fprintf(g_port_profile_file, "gpu_parts");
        for (i = 0; i < PORT_GPU_PASSES; i++) {
            fprintf(g_port_profile_file, " %s", port_GlBenchName(i));
        }
        fputc('\n', g_port_profile_file);
        g_port_profile_on = 1;
        if (g_port_has_gl) {
            port_GlBenchTime(TRUE);
        }
        if (getenv("RB_PROFILE_CPU") != NULL) {
            g_port_profile_clock = port_ThreadTicks;
        }
        /* what was counted before is no blank's */
        port_ProfileTake(ticks);
    }
    if (g_port_window_options.sample != NULL) {
        port_SampleStart(g_port_window_options.sample);
    }
    /* `fps = times` counts the parts' time as --profile does */
    port_CountTimes();
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

    port_ShownDisplay(&display);
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
 * card does not draw, so that the software GPU's picture is the one there
 * is of them. The title picture it holds after the movie, in the 16-bit
 * mode, is the card's where a mod has a picture of it (the art mod's,
 * made larger from the disc): it is drawn from that, and so is its fade. */
static s32 port_ShowsRemastered(void) {
    PortDisplay display;

    if (!g_port_has_gl || !port_RemasterIsOn()) {
        return FALSE;
    }
    port_ShownDisplay(&display);
    if (port_MovieIsPlaying()) {
        return !display.is_rgb24 && port_GlShowsReplacedPicture();
    }
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

/* What a report says of the program, the PC and the session, into
 * `out` (port_MakeReport). */
static void port_DescribeForReport(char *out, u32 room) {
    SDL_version linked;
    SDL_DisplayMode mode;
    s32 window_w = 0;
    s32 window_h = 0;
    u32 used = 0;
    u32 i;
    const PortSettings *set = &g_port_settings;

#define REPORT_LINE(...)                                                       \
    do {                                                                       \
        if (used < room) {                                                     \
            s32 n = snprintf(out + used, room - used, __VA_ARGS__);            \
            used += n > 0 ? (u32)n : 0;                                        \
        }                                                                      \
    } while (0)

    SDL_GetVersion(&linked);
    port_WindowSize(&window_w, &window_h);
    REPORT_LINE("%s\n", port_GetVersionText());
    REPORT_LINE("system: %s, %d processors, %d MB; SDL %d.%d.%d\n",
                SDL_GetPlatform(), SDL_GetCPUCount(), SDL_GetSystemRAM(),
                linked.major, linked.minor, linked.patch);
    REPORT_LINE("video: driver %s", SDL_GetCurrentVideoDriver());
    if (g_port_has_gl) {
        REPORT_LINE("; card %s, %s; OpenGL %s", port_GlDescribe(PORT_GL_VENDOR),
                    port_GlDescribe(PORT_GL_RENDERER),
                    port_GlDescribe(PORT_GL_VERSION));
    }
    REPORT_LINE("\n");
    if (g_port_window != NULL &&
        SDL_GetCurrentDisplayMode(
            SDL_GetWindowDisplayIndex(g_port_window), &mode) == 0) {
        REPORT_LINE("display: %d by %d at %d a second\n", mode.w, mode.h,
                    mode.refresh_rate);
    }
    REPORT_LINE("window: %d by %d%s%s; vsync %s, the display's rate taken as "
                "%d; refresh measured every %u samples\n",
                window_w, window_h, g_port_is_fullscreen ? ", full screen" : "",
                g_port_window_options.is_hidden ? ", hidden" : "",
                g_port_vsync == PORT_VSYNC_ADAPTIVE ? "adaptive"
                : g_port_vsync != 0                 ? "on"
                                                    : "off",
                g_port_display_rate, g_port_refresh.period);
    REPORT_LINE("sound: %s; the queue %u long (%u at the last blank), ran out "
                "%u times, %u pairs thrown away\n",
                g_port_audio != 0 ? SDL_GetCurrentAudioDriver() : "none",
                g_port_ring.target, g_port_queue_seen, g_port_ring.underruns,
                g_port_ring.dropped);
    REPORT_LINE("pace: time let go %u times; the blanks moved %d samples in "
                "all to keep the queue\n",
                g_port_pace.lost, g_port_pace.nudged);
    REPORT_LINE("pictures: %u shown, %u of them between frames; %u blanks, "
                "%u of them a refresh late and not shown; now %u.%u a second "
                "and %u.%u of the game's frames\n",
                g_port_pictures, g_port_pictures_between, g_port_blanks_shown,
                g_port_pictures_unshown, g_port_shown_meter.tenths / 10,
                g_port_shown_meter.tenths % 10, g_port_game_meter.tenths / 10,
                g_port_game_meter.tenths % 10);
    REPORT_LINE(
        "settings: graphics %s, frame_rate %d, run_ahead %d, "
        "low_latency %d, late_input %d, resolution %d, antialiasing %d, "
        "aspect %d:%d, "
        "fighter_models %d, shadows %d, smooth_light %d, effects %d, "
        "sky %d, see_through %d, stage_mesh %d, quick_loading %d\n",
        port_RemasterIsOn() ? "remastered" : "original", set->frame_rate,
        set->run_ahead, set->low_latency, set->late_input, set->resolution,
        set->supersample, set->aspect_w, set->aspect_h, set->fighter_models,
        set->shadows, set->smooth_light, set->effects, set->sky,
        set->see_through, set->stage_mesh, set->quick_loading);
    REPORT_LINE("the last blank's time, microseconds:");
    for (i = 0; i < PORT_PROFILE_PARTS; i++) {
        REPORT_LINE(" %s %u", port_ProfileName((s32)i), g_port_last_ticks[i]);
    }
    REPORT_LINE("\n");
#undef REPORT_LINE
}

/* ---- a picture written beside the game ----
 *
 * F12's and F9's picture of the window is encoded and written on a
 * thread of its own: on the thread that draws, a PNG of 2560 by 1440
 * held a blank up 60 to 70 ms (tools/port_stalls.py; docs/PORT.md,
 * "Stalls"). The pixels are copied for it; one picture is encoded at a
 * time (the PNG writer of picture.c keeps its state in one place); and
 * the program waits for every one before it ends, so that a test finds
 * its file. Where the window's pictures are written as they are shown
 * (--tap-window, --tap-frames), which use the same writer on this
 * thread, it is written here as before. */

#define PNG_THREADS_MAX 8

typedef struct {
    char path[PATH_BYTES_MAX];
    u32 *pixels;
    s32 w;
    s32 h;
} PortPngJob;

static SDL_Thread *g_port_png_threads[PNG_THREADS_MAX];
static SDL_mutex *g_port_png_lock;
static SDL_atomic_t g_port_png_failed;

static s32 port_WritePngFile(
    const char *path, const u32 *pixels, s32 w, s32 h) {
    s32 file = port_HostCreate(path);
    s32 written = file >= 0 && port_WritePixelsPng(file, pixels, w, h);

    if (file >= 0) {
        port_HostClose(file);
    }
    return written;
}

static int SDLCALL port_PngThread(void *data) {
    PortPngJob *job = data;

    SDL_LockMutex(g_port_png_lock);
    if (!port_WritePngFile(job->path, job->pixels, job->w, job->h)) {
        SDL_AtomicIncRef(&g_port_png_failed);
    }
    SDL_UnlockMutex(g_port_png_lock);
    free(job->pixels);
    free(job);
    return 0;
}

/* Every picture still being written, waited for. */
static void port_WaitForPngs(void) {
    s32 i;

    for (i = 0; i < PNG_THREADS_MAX; i++) {
        if (g_port_png_threads[i] != NULL) {
            SDL_WaitThread(g_port_png_threads[i], NULL);
            g_port_png_threads[i] = NULL;
        }
    }
    if (SDL_AtomicGet(&g_port_png_failed) > 0) {
        port_Say("F12: %d pictures could not be written",
                 SDL_AtomicGet(&g_port_png_failed));
        SDL_AtomicSet(&g_port_png_failed, 0);
    }
}

/* The window's pixels kept by port_KeepShot written to `path`: on a
 * thread of their own where it can be, here where it cannot. FALSE if
 * written here and it could not be. */
static s32 port_WriteShotPng(const char *path, s32 w, s32 h) {
    size_t bytes = (size_t)w * (size_t)h * sizeof(u32);
    PortPngJob *job;
    s32 slot;

    if (g_port_window_options.tap_window != NULL ||
        g_port_window_options.tap_frames != NULL) {
        return port_WritePngFile(path, g_port_window_pixels, w, h);
    }
    if (g_port_png_lock == NULL) {
        g_port_png_lock = SDL_CreateMutex();
    }
    for (slot = 0; slot < PNG_THREADS_MAX; slot++) {
        if (g_port_png_threads[slot] == NULL) {
            break;
        }
    }
    if (slot == PNG_THREADS_MAX) {
        port_WaitForPngs();
        slot = 0;
    }
    job = malloc(sizeof(*job));
    if (g_port_png_lock == NULL || job == NULL ||
        (job->pixels = malloc(bytes)) == NULL) {
        free(job);
        return port_WritePngFile(path, g_port_window_pixels, w, h);
    }
    snprintf(job->path, sizeof(job->path), "%s", path);
    memcpy(job->pixels, g_port_window_pixels, bytes);
    job->w = w;
    job->h = h;
    g_port_png_threads[slot] =
        SDL_CreateThread(port_PngThread, "rebushido-png", job);
    if (g_port_png_threads[slot] == NULL) {
        free(job->pixels);
        free(job);
        return port_WritePngFile(path, g_port_window_pixels, w, h);
    }
    return TRUE;
}

/* A report of a problem (F9): its folder beside the log, the picture the
 * window was showing, and everything else (sdl_report.c). */
static void port_MakeReport(void) {
    static char about[4096];
    char path[PATH_BYTES_MAX];
    s32 picture = FALSE;

    if (!port_ReportFolder(
            g_port_report_folder, sizeof(g_port_report_folder))) {
        port_Say("F9: no folder can be made for the report");
        return;
    }
    if (g_port_has_shot) {
        g_port_has_shot = FALSE;
        snprintf(path, sizeof(path), "%s/picture.png", g_port_report_folder);
        picture = port_WriteShotPng(path, g_port_shot_w, g_port_shot_h);
    }
    /* the line first, so that the log the report keeps has it */
    port_Say("F9: a report of the last seconds%s: %s",
             picture ? ", with a picture of the window" : "",
             g_port_report_folder);
    port_DescribeForReport(about, sizeof(about));
    port_WriteReport(g_port_report_folder, about);
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
    s32 written;

    g_port_wants_shot = FALSE;
    g_port_shot_picture = -1;
    if (g_port_wants_report) {
        /* F9: the picture is the report's, and the rest follows it */
        g_port_wants_report = FALSE;
        SDL_free(base);
        port_MakeReport();
        return;
    }
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
    written = port_WriteShotPng(path, window_w, window_h);
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
    s32 is_tapped;

    port_ShownDisplay(&display);
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
    /* the software GPU's picture, where a test takes it; and where it is
     * shown, below -- not where the remastered picture is, which has no
     * use for it */
    is_tapped = may_tap && g_port_window_options.tap_frames != NULL &&
                blank % (u32)g_port_window_options.tap_every == 0;
    /* where the frames were run ahead, g_port_frame holds what they left
     * (port_AheadDone) */
    if (is_tapped) {
        if (!g_port_unseen) {
            port_PaintSyncAll();
            port_ConvertDisplay(g_port_vram, display.x, display.y, w, h,
                                display.is_rgb24, g_port_frame);
        }
        port_TapFrame(blank, w, h, display.is_on);
    }
    if (!display.is_on) {
        return;
    }
    if (g_port_has_gl && port_ShowsRemastered() &&
        port_ShowRemastered(window_w, window_h)) {
        return;
    }
    if (!is_tapped && !g_port_unseen) {
        port_PaintSyncAll();
        port_ConvertDisplay(g_port_vram, display.x, display.y, w, h,
                            display.is_rgb24, g_port_frame);
    }
    if (g_port_has_gl) {
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

/* ---- the times under the counter (`fps = times`) ---- */

/* The parts' times are counted always: for --profile, for the counter's
 * times, and for a report (F9, sdl_report.c), which keeps the last ten
 * seconds of them; a switch is two reads of the clock. Turned on, the
 * time counted before is no blank's. */
static void port_CountTimes(void) {
    s32 on = TRUE;
    u32 ticks[PORT_PROFILE_PARTS];

    if (on && !g_port_profile_on) {
        g_port_profile_on = 1;
        port_ProfileTake(ticks);
        SDL_zero(g_port_times);
        g_port_times_count = 0;
    } else if (!on) {
        g_port_profile_on = 0;
    }
    if (g_port_has_gl) {
        port_GlBenchTime(on);
    }
}

/* The counter: off, on, or with the times under it (F3 steps through
 * them). */
static void port_SetCounter(s32 how) {
    g_port_show_fps = how;
    if (g_port_is_open) {
        port_CountTimes();
    }
}

/* Whether part `part` of the processor's is its own work: not a wait for
 * the clock or for the display. */
static s32 port_IsWork(s32 part) {
    return part != PORT_PROFILE_WAIT && part != PORT_PROFILE_SWAP;
}

/* A blank's times into the sums: the processor's in microseconds, at
 * the blank; the card's in nanoseconds, a few blanks later. */
static void port_AddCpuTimes(const u32 *microseconds) {
    u32 sum = 0;
    s32 i;

    for (i = 0; i < PORT_PROFILE_PARTS; i++) {
        g_port_times.cpu[i] += microseconds[i];
        sum += port_IsWork(i) ? microseconds[i] : 0;
    }
    g_port_times.cpu_blanks++;
    if (sum > g_port_times.cpu_most) {
        g_port_times.cpu_most = sum;
    }
}

static void port_AddGpuTimes(const u32 *nanoseconds) {
    u32 sum = 0;
    s32 i;

    for (i = 0; i < PORT_GPU_PASSES; i++) {
        g_port_times.gpu[i] += nanoseconds[i] / 1000U;
        sum += nanoseconds[i] / 1000U;
    }
    g_port_times.gpu_blanks++;
    if (sum > g_port_times.gpu_most) {
        g_port_times.gpu_most = sum;
    }
}

/* `NAME 1.2 NAME 0.8 ...`: the TIMES_NAMED largest of `count` sums of
 * microseconds over `blanks`, as milliseconds a blank. */
static void port_FormatLargest(
    char *out, size_t room, const char *head, const u32 *sums, s32 count,
    u32 blanks, const char *(*name)(s32), s32 cpu) {
    s32 used[PORT_PROFILE_PARTS + PORT_GPU_PASSES];
    size_t at;
    s32 named;
    s32 i;

    SDL_zeroa(used);
    at = (size_t)SDL_snprintf(out, room, "%s", head);
    for (named = 0; named < TIMES_NAMED && blanks > 0; named++) {
        s32 best = -1;
        char word[16];
        s32 k;

        for (i = 0; i < count; i++) {
            if (!used[i] && (!cpu || port_IsWork(i)) &&
                (best < 0 || sums[i] > sums[best])) {
                best = i;
            }
        }
        if (best < 0 || sums[best] == 0 || at >= room) {
            break;
        }
        used[best] = TRUE;
        SDL_strlcpy(word, name(best), sizeof(word));
        for (k = 0; word[k] != 0; k++) {
            word[k] = (char)SDL_toupper((unsigned char)word[k]);
        }
        at += (size_t)SDL_snprintf(out + at, room - at, " %s %.1f", word,
                                   (double)sums[best] / blanks / 1000.0);
    }
}

/* The last blank's time for the log: all of it, the processor's work,
 * and its four largest parts, in milliseconds. */
static void port_LargestParts(char *out, size_t room) {
    u32 all = 0;
    u32 work = 0;
    size_t at;
    s32 i;

    for (i = 0; i < PORT_PROFILE_PARTS; i++) {
        all += g_port_last_ticks[i];
        work += port_IsWork(i) ? g_port_last_ticks[i] : 0;
    }
    at = (size_t)SDL_snprintf(
        out, room, "%.1f ms, %.1f of it work:", all / 1000.0, work / 1000.0);
    if (at < room) {
        port_FormatLargest(out + at, room - at, "", g_port_last_ticks,
                           PORT_PROFILE_PARTS, 1, port_ProfileName, TRUE);
    }
}

/* The lines under the counter, from the last half second's sums. */
static s32 port_FormatTimes(char lines[][COUNTER_TEXT_BYTES]) {
    const PortTimes *t = &g_port_times_shown;
    u32 cpu = 0;
    u32 gpu = 0;
    s32 i;

    for (i = 0; i < PORT_PROFILE_PARTS; i++) {
        cpu += port_IsWork(i) ? t->cpu[i] : 0;
    }
    for (i = 0; i < PORT_GPU_PASSES; i++) {
        gpu += t->gpu[i];
    }
    if (t->gpu_blanks > 0) {
        SDL_snprintf(lines[0], COUNTER_TEXT_BYTES,
                     "CPU %.1f MOST %.1f GPU %.1f MOST %.1f",
                     t->cpu_blanks ? (double)cpu / t->cpu_blanks / 1000.0 : 0.0,
                     t->cpu_most / 1000.0, (double)gpu / t->gpu_blanks / 1000.0,
                     t->gpu_most / 1000.0);
    } else {
        SDL_snprintf(lines[0], COUNTER_TEXT_BYTES, "CPU %.1f MOST %.1f",
                     t->cpu_blanks ? (double)cpu / t->cpu_blanks / 1000.0 : 0.0,
                     t->cpu_most / 1000.0);
    }
    port_FormatLargest(
        lines[1], COUNTER_TEXT_BYTES, "C", t->cpu, PORT_PROFILE_PARTS,
        t->cpu_blanks, port_ProfileName, TRUE);
    if (t->gpu_blanks == 0) {
        return 2;
    }
    port_FormatLargest(lines[2], COUNTER_TEXT_BYTES, "G", t->gpu,
                       PORT_GPU_PASSES, t->gpu_blanks, port_GlBenchName, FALSE);
    return 3;
}

/* --profile's lines of a blank, and the times' sums: the parts' time in
 * microseconds (the `b` line; the part of the game's that the blank
 * began with is the step of the frame before, and `is_new_frame` says
 * the game drew a frame: gl_tween.h), and each blank the card has
 * finished since, a few blanks late, its passes' (the `g` line). */
static void port_NoteProfile(u32 blank, s32 is_new_frame) {
    u32 ticks[PORT_PROFILE_PARTS];
    u32 gpu[PORT_GPU_PASSES];
    u32 timed;
    s32 i;

    /* a sample from now is the next blank's */
    g_port_sample_blank = blank + 1;
    if (!g_port_profile_on) {
        return;
    }
    port_ProfileTake(ticks);
    for (i = 0; i < PORT_PROFILE_PARTS; i++) {
        ticks[i] /= PORT_HOST_TICKS_PER_MICROSECOND;
    }
    g_port_frame_blanks[blank % sizeof(g_port_frame_blanks)] = (u8)is_new_frame;
    port_ReportNoteBlank(blank, is_new_frame, ticks);
    memcpy(g_port_last_ticks, ticks, sizeof(g_port_last_ticks));
    if (g_port_profile_file != NULL) {
        fprintf(g_port_profile_file, "b %u %d", blank, is_new_frame);
        for (i = 0; i < PORT_PROFILE_PARTS; i++) {
            fprintf(g_port_profile_file, " %u", ticks[i]);
        }
        fputc('\n', g_port_profile_file);
    }
    if (is_new_frame) {
        port_AddCpuTimes(ticks);
    }
    if (g_port_stepper != NULL) {
        /* the step's thread's own parts, where it ran beside this one
         * (docs/PORT.md, "The step on a thread of its own") */
        u32 step[PORT_PROFILE_PARTS];
        u32 any = 0;

        port_ProfileTakeStep(step);
        for (i = 0; i < PORT_PROFILE_PARTS; i++) {
            step[i] /= PORT_HOST_TICKS_PER_MICROSECOND;
            any |= i != PORT_PROFILE_WAIT ? step[i] : 0;
        }
        if (any != 0 && g_port_profile_file != NULL) {
            fprintf(g_port_profile_file, "s %u", blank);
            for (i = 0; i < PORT_PROFILE_PARTS; i++) {
                fprintf(g_port_profile_file, " %u", step[i]);
            }
            fputc('\n', g_port_profile_file);
        }
    }
    if (g_port_has_gl && g_port_gpu_on) {
        port_GlBenchBlank(blank);
        while (port_GlBenchTake(&timed, gpu)) {
            port_ReportNoteCard(timed, gpu);
            if (g_port_profile_file != NULL) {
                fprintf(g_port_profile_file, "g %u", timed);
                for (i = 0; i < PORT_GPU_PASSES; i++) {
                    fprintf(g_port_profile_file, " %u", gpu[i] / 1000U);
                }
                fputc('\n', g_port_profile_file);
            }
            if (g_port_frame_blanks[timed % sizeof(g_port_frame_blanks)]) {
                port_AddGpuTimes(gpu);
            }
        }
    }
    if (++g_port_times_count >= TIMES_BLANKS) {
        g_port_times_shown = g_port_times;
        SDL_zero(g_port_times);
        g_port_times_count = 0;
    }
}

/* What is laid over the game's 4:3 area, the kinds of stage over the
 * stage pages (stage_page.c) and the match rules' strip and cards
 * (match_page.c): where the area is in the picture, and the pixels laid
 * there, with the card or with SDL, each through a texture of its own. */
typedef s32 (*PortOverlayPixels)(
    s32 area_w, s32 area_h, const u32 **pixels, PortRect *box);

typedef struct {
    SDL_Texture *texture;
    s32 w;
    s32 h;
} PortOverlay;

static PortOverlay g_port_stage_page;
static PortOverlay g_port_match_page;

static void port_DrawOverlay(PortOverlay *overlay, PortOverlayPixels make) {
    const u32 *pixels;
    PortRect picture;
    PortRect box;
    SDL_Rect where;
    s32 area_w;
    s32 area_h;
    s32 window_w = 0;
    s32 window_h = 0;

    port_WindowSize(&window_w, &window_h);
    port_PicturePlace(window_w, window_h, &picture);
    area_h = picture.h;
    area_w = picture.h * 4 / 3;
    if (area_w > picture.w) {
        area_w = picture.w;
        area_h = picture.w * 3 / 4;
    }
    if (!make(area_w, area_h, &pixels, &box)) {
        return;
    }
    where.x = picture.x + (picture.w - area_w) / 2 + box.x;
    where.y = picture.y + (picture.h - area_h) / 2 + box.y;
    where.w = box.w;
    where.h = box.h;
    if (g_port_has_gl) {
        port_GlShowPixels(pixels, box.w, box.h, window_w, window_h, where.x,
                          where.y, where.w, where.h, FALSE, TRUE);
        return;
    }
    if (overlay->texture == NULL || overlay->w != box.w ||
        overlay->h != box.h) {
        if (overlay->texture != NULL) {
            SDL_DestroyTexture(overlay->texture);
        }
        overlay->texture =
            SDL_CreateTexture(g_port_renderer, SDL_PIXELFORMAT_ARGB8888,
                              SDL_TEXTUREACCESS_STREAMING, box.w, box.h);
        if (overlay->texture == NULL) {
            return;
        }
        SDL_SetTextureBlendMode(overlay->texture, SDL_BLENDMODE_BLEND);
        overlay->w = box.w;
        overlay->h = box.h;
    }
    SDL_UpdateTexture(overlay->texture, NULL, pixels, box.w * BYTES_PER_PIXEL);
    SDL_RenderCopy(g_port_renderer, overlay->texture, NULL, &where);
}

static void port_DrawStagePage(void) {
    port_DrawOverlay(&g_port_stage_page, port_StagePagePixels);
}

static void port_DrawMatchPage(void) {
    port_DrawOverlay(&g_port_match_page, port_MatchPixels);
}

/* The counter in the port's typeface (font.c), at the window's own
 * resolution, where there is a card: its box the size the digits' box
 * would be (port_PlaceCounter), each of its units `times` pixels, the
 * capitals as tall as the digits' seven rows. FALSE where the typeface
 * cannot be read, and the digits' own shapes are drawn instead. */
static s32 port_DrawCounterLettered(
    char lines[][COUNTER_TEXT_BYTES], s32 count) {
    static u32 *g_pixels;
    static s32 g_room;
    static s32 g_font_ok = -1;
    PortTextStyle style;
    PortRect picture;
    PortRect place;
    s32 window_w = 0;
    s32 window_h = 0;
    s32 times;
    s32 text_w = 0;
    s32 box_h;
    s32 i;

    if (g_font_ok < 0) {
        g_font_ok = port_FontStart();
    }
    if (!g_font_ok) {
        return FALSE;
    }
    port_WindowSize(&window_w, &window_h);
    port_PicturePlace(window_w, window_h, &picture);
    times = picture.h / COUNTER_PICTURE_LINES;
    if (times < 1) {
        times = 1;
    }
    memset(&style, 0, sizeof(style));
    style.face = PORT_FACE_BOLD;
    style.size = PORT_GLYPH_H * times * COUNTER_SIZE_PERCENT / COUNTER_PERCENT;
    style.color = COUNTER_INK;
    for (i = 0; i < count; i++) {
        s32 w = port_TextMeasure(lines[i], &style) + 2 * COUNTER_PAD * times;

        text_w = w > text_w ? w : text_w;
    }
    /* whole units, so that the box is placed as the digits' is */
    text_w = (text_w + times - 1) / times * times;
    box_h = (COUNTER_H + (count - 1) * COUNTER_LINE_H) * times;
    if (text_w * box_h > g_room) {
        u32 *grown =
            SDL_realloc(g_pixels, (size_t)text_w * box_h * BYTES_PER_PIXEL);

        if (grown == NULL) {
            return FALSE;
        }
        g_pixels = grown;
        g_room = text_w * box_h;
    }
    for (i = 0; i < text_w * box_h; i++) {
        g_pixels[i] = COUNTER_BOX;
    }
    for (i = 0; i < count; i++) {
        port_TextDraw(g_pixels, text_w, box_h, COUNTER_PAD * times,
                      (COUNTER_PAD + i * COUNTER_LINE_H + PORT_GLYPH_H) * times,
                      lines[i], &style);
    }
    port_PlaceCounter(&picture, text_w / times, box_h / times,
                      g_port_settings.fps_corner, &place);
    port_GlShowPixels(g_pixels, text_w, box_h, window_w, window_h, place.x,
                      place.y, place.w, place.h, FALSE, TRUE);
    return TRUE;
}

/* ReBushido's top menu over the game's page (top_page.c), the same way. */
static SDL_Texture *g_port_top_page;
static s32 g_port_top_page_w;
static s32 g_port_top_page_h;

static void port_DrawTopPage(void) {
    const u32 *pixels;
    PortRect picture;
    PortRect box;
    SDL_Rect where;
    s32 area_w;
    s32 area_h;
    s32 window_w = 0;
    s32 window_h = 0;

    port_WindowSize(&window_w, &window_h);
    port_PicturePlace(window_w, window_h, &picture);
    area_h = picture.h;
    area_w = picture.h * 4 / 3;
    if (area_w > picture.w) {
        area_w = picture.w;
        area_h = picture.w * 3 / 4;
    }
    if (!port_TopPagePixels(area_w, area_h, &pixels, &box)) {
        return;
    }
    where.x = picture.x + (picture.w - area_w) / 2 + box.x;
    where.y = picture.y + (picture.h - area_h) / 2 + box.y;
    where.w = box.w;
    where.h = box.h;
    if (g_port_has_gl) {
        port_GlShowPixels(pixels, box.w, box.h, window_w, window_h, where.x,
                          where.y, where.w, where.h, FALSE, TRUE);
        return;
    }
    if (g_port_top_page == NULL || g_port_top_page_w != box.w ||
        g_port_top_page_h != box.h) {
        if (g_port_top_page != NULL) {
            SDL_DestroyTexture(g_port_top_page);
        }
        g_port_top_page =
            SDL_CreateTexture(g_port_renderer, SDL_PIXELFORMAT_ARGB8888,
                              SDL_TEXTUREACCESS_STREAMING, box.w, box.h);
        if (g_port_top_page == NULL) {
            return;
        }
        SDL_SetTextureBlendMode(g_port_top_page, SDL_BLENDMODE_BLEND);
        g_port_top_page_w = box.w;
        g_port_top_page_h = box.h;
    }
    SDL_UpdateTexture(g_port_top_page, NULL, pixels, box.w * BYTES_PER_PIXEL);
    SDL_RenderCopy(g_port_renderer, g_port_top_page, NULL, &where);
}

static void port_DrawCounter(void) {
    char lines[COUNTER_LINES][COUNTER_TEXT_BYTES];
    PortRect picture;
    PortRect place;
    SDL_Rect from;
    SDL_Rect where;
    s32 count = 1;
    s32 text_w = 0;
    s32 box_h;
    s32 stride;
    s32 i;
    s32 window_w = 0;
    s32 window_h = 0;

    /* what is laid over the picture's corner is laid over it with the
     * stage pages' kinds of stage (stage_page.c) */
    port_DrawStagePage();
    port_DrawMatchPage();
    port_DrawTopPage();
    if (!g_port_show_fps) {
        return;
    }
    if (!g_port_has_gl && g_port_counter == NULL) {
        g_port_counter = SDL_CreateTexture(
            g_port_renderer, SDL_PIXELFORMAT_ARGB8888,
            SDL_TEXTUREACCESS_STREAMING, COUNTER_W, COUNTER_H_MAX);
        if (g_port_counter == NULL) {
            port_Say("no counter: %s", SDL_GetError());
            g_port_show_fps = 0;
            return;
        }
        SDL_SetTextureBlendMode(g_port_counter, SDL_BLENDMODE_BLEND);
        SDL_SetTextureScaleMode(g_port_counter, SDL_ScaleModeNearest);
    }
    port_FormatFps(lines[0], g_port_shown_meter.tenths,
                   g_port_game_meter.tenths, g_port_blank_meter.tenths);
    if (g_port_show_fps == PORT_FPS_TIMES) {
        count += port_FormatTimes(&lines[1]);
    }
    if (g_port_has_gl && port_DrawCounterLettered(lines, count)) {
        return;
    }
    for (i = 0; i < count; i++) {
        s32 w = port_MeasureText(lines[i]) + 2 * COUNTER_PAD;

        text_w = w > text_w ? w : text_w;
    }
    if (text_w > COUNTER_W) {
        text_w = COUNTER_W;
    }
    box_h = COUNTER_H + (count - 1) * COUNTER_LINE_H;
    /* with the card, the box is the text's own width, so that the pixels
     * given to it are the ones shown and no more */
    stride = g_port_has_gl ? text_w : COUNTER_W;
    for (i = 0; i < stride * box_h; i++) {
        g_port_counter_pixels[i] = i % stride < text_w ? COUNTER_BOX : 0;
    }
    for (i = 0; i < count; i++) {
        port_DrawText(g_port_counter_pixels, stride, box_h, COUNTER_PAD,
                      COUNTER_PAD + i * COUNTER_LINE_H, lines[i], COUNTER_INK);
    }
    port_WindowSize(&window_w, &window_h);
    if (g_port_has_gl) {
        port_PicturePlace(window_w, window_h, &picture);
        port_PlaceCounter(
            &picture, text_w, box_h, g_port_settings.fps_corner, &place);
        port_GlShowPixels(
            g_port_counter_pixels, text_w, box_h, window_w, window_h, place.x,
            place.y, place.w, place.h, FALSE, TRUE);
        return;
    }
    SDL_UpdateTexture(g_port_counter, NULL, g_port_counter_pixels,
                      COUNTER_W * BYTES_PER_PIXEL);
    port_FitPicture(window_w, window_h, g_port_settings.scale, &picture);
    port_PlaceCounter(
        &picture, text_w, box_h, g_port_settings.fps_corner, &place);
    from.x = 0;
    from.y = 0;
    from.w = text_w;
    from.h = box_h;
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
    /* an update is never downloaded during a fight (update.c) */
    port_UpdateWatch(now.in_fight);
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
    if (g_port_has_seen && now.is_paused != g_port_seen.is_paused) {
        port_Note("game: the pause menu %s; blank %u, frame %d",
                  now.is_paused ? "is up" : "is closed", blank, now.frames);
    }
    if (g_port_has_seen && now.is_top_menu != g_port_seen.is_top_menu) {
        port_Note("game: the top menu %s; blank %u, frame %d",
                  now.is_top_menu ? "is up" : "is left", blank, now.frames);
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
        char parts[COUNTER_TEXT_BYTES * 2];

        port_LargestParts(parts, sizeof(parts));
        port_Note(
            "pace: blank %u was %d ms %s, more than the tenth of a "
            "second the game catches up, and the time was let go (%u "
            "times so far); the blank before took %s",
            blank - 1,
            (g_port_pace.lost_by < 0 ? -g_port_pace.lost_by
                                     : g_port_pace.lost_by) *
                MILLISECONDS / PORT_SPU_RATE,
            g_port_pace.lost_by < 0 ? "early (the clock went back)" : "late",
            g_port_pace.lost, parts);
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
    if (kind != 2) {
        g_port_load_read_at = port_ReadClock();
    }
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

    if (g_port_window_options.is_hidden) {
        return 0; /* a run nobody watches: no dialog to wait on */
    }
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
    /* the router's port given back (portmap.c): the program ends by the
     * system's own exit, which runs no atexit, so here; a crash's lease
     * runs out by itself */
    port_UnmapPort();
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

/* The state of another moment put back (sdl_window.h). */
void port_WindowStateReplaced(void) {
    if (g_port_tween_on) {
        port_TweenForget();
        g_port_frame_period = 0;
    }
    if (g_port_has_gl && port_RemasterIsOn()) {
        port_GlRefresh();
    }
    port_MenuFollowAsks();
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

/* Pixels laid over what has been drawn of the picture so far, at the
 * window's own resolution (sdl_window.h). */
void port_LayPixels(const u32 *pixels, s32 w, s32 h, s32 x, s32 y) {
    s32 window_w = 0;
    s32 window_h = 0;
    SDL_Rect where;

    if (w <= 0 || h <= 0) {
        return;
    }
    port_WindowSize(&window_w, &window_h);
    if (g_port_has_gl) {
        port_GlShowPixels(
            pixels, w, h, window_w, window_h, x, y, w, h, FALSE, TRUE);
        return;
    }
    if (g_port_renderer == NULL) {
        return;
    }
    if (g_port_lay_texture == NULL || g_port_lay_w != w || g_port_lay_h != h) {
        if (g_port_lay_texture != NULL) {
            SDL_DestroyTexture(g_port_lay_texture);
        }
        g_port_lay_texture =
            SDL_CreateTexture(g_port_renderer, SDL_PIXELFORMAT_ARGB8888,
                              SDL_TEXTUREACCESS_STREAMING, w, h);
        g_port_lay_w = w;
        g_port_lay_h = h;
        if (g_port_lay_texture == NULL) {
            return;
        }
        SDL_SetTextureBlendMode(g_port_lay_texture, SDL_BLENDMODE_BLEND);
    }
    SDL_UpdateTexture(g_port_lay_texture, NULL, pixels, w * BYTES_PER_PIXEL);
    where.x = x;
    where.y = y;
    where.w = w;
    where.h = h;
    SDL_RenderCopy(g_port_renderer, g_port_lay_texture, NULL, &where);
}

/* The window's events read between two blanks, while the program waits
 * on the network (sdl_window.h). */
void port_PollWindow(void) { port_ReadInput(port_GetBlankCount()); }

/* The game's last picture shown again, with the counter and netplay's
 * line over it: the window while it waits on the network. */
void port_ShowStill(void) {
    port_OpenWindow();
    port_DrawPicture(port_GetBlankCount(), FALSE);
    port_DrawCounter();
    port_DrawNetLine();
    port_SwapPicture();
}

/* What the keys and controllers hold on a pad, through the layout, and
 * Escape's button on player one's. */
static u32 port_PadHeld(s32 pad) {
    return port_GetHeldButtons(&g_port_settings, pad) |
           (pad == 0 ? g_port_escape_held : 0);
}

void port_HoldBackPads(void) {
    s32 pad;

    for (pad = 0; pad < PORT_PADS; pad++) {
        g_port_held_back[pad] = port_PadHeld(pad);
    }
}

/* Escape is no key a player binds (port_LookUpName): it is the game's
 * own way back from where it is, player one's button of it held while
 * Escape is (docs/PORT.md, "The window", Escape). In the fight, Start,
 * which opens the pause menu (main.c); in the pause menu, Triangle,
 * which closes it and goes on with the fight; in a story's cutscene,
 * Cross, which skips it (story.c); everywhere else Triangle, the button
 * every menu and screen of the game backs out with (menu_pages.c,
 * select.c, memcard_save.c), and which does nothing where there is
 * nothing to go back to (the title, the options page at boot, a load).
 * The button is the one for where the game is as Escape goes down, so
 * that the pause it opens is not closed by the same press. */
static u32 port_EscapeButton(void) {
    PortGameState now;

    port_ReadGameState(&now);
    if (now.in_fight && !now.is_paused) {
        return port_FindPadButton("start");
    }
    if (now.is_cutscene) {
        return port_FindPadButton("cross");
    }
    return port_FindPadButton("triangle");
}

void port_TakeEscape(s32 down) {
    g_port_escape_held = down ? port_EscapeButton() : 0;
    if (down) {
        port_Note("Escape: %s of player one; blank %u",
                  port_NamePadButton(g_port_escape_held), port_GetBlankCount());
    }
}

/* Whether `blank` is one of a test's list ("1500,4000"). */
static s32 port_IsListedBlank(const char *at, u32 after, u32 upto) {
    while (at != NULL && *at != 0) {
        u32 listed = (u32)atoi(at);

        if (listed > after && listed <= upto) {
            return TRUE;
        }
        at = strchr(at, ',');
        if (at != NULL) {
            at++;
        }
    }
    return FALSE;
}

/* --escape-at: Escape pressed at those blanks and let go
 * ESCAPE_TEST_BLANKS later, as SDL's events of the key, which are read
 * as a player's are. */
#define ESCAPE_TEST_BLANKS 4

static void port_PressEscapeForTest(u32 blank) {
    /* the blanks since the last one read here: with the step on a thread
     * of its own the window's thread reads only a frame's boundary, so a
     * listed blank inside a step is pressed at the next boundary */
    static u32 s_last;
    const char *at = g_port_window_options.escape_at;
    u32 last = s_last;
    s32 down;
    SDL_Event event;

    s_last = blank;
    if (at == NULL || blank <= last) {
        return;
    }
    down = port_IsListedBlank(at, last, blank);
    if (!down &&
        (blank < ESCAPE_TEST_BLANKS ||
         !port_IsListedBlank(
             at, last < ESCAPE_TEST_BLANKS ? 0 : last - ESCAPE_TEST_BLANKS,
             blank - ESCAPE_TEST_BLANKS))) {
        return;
    }
    SDL_zero(event);
    event.type = down ? SDL_KEYDOWN : SDL_KEYUP;
    event.key.state = down ? SDL_PRESSED : SDL_RELEASED;
    event.key.keysym.scancode = SDL_SCANCODE_ESCAPE;
    event.key.keysym.sym = SDLK_ESCAPE;
    if (SDL_PushEvent(&event) != 1) {
        port_Say("--escape-at: SDL took no event: %s", SDL_GetError());
        port_Quit(PORT_EXIT_FAILED);
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

void port_WindowGameSwitches(void) {
    port_SetBugfixesSwitch(g_port_settings.bugfixes);
    /* the save's flags: SAVE_FLAG_GRAYSCALE, _EASY_CPU, _MUSIC_OFF */
    port_SetGameOptions((g_port_settings.monitor != 0 ? 1U : 0U) |
                        (g_port_settings.difficulty != 0 ? 2U : 0U) |
                        (g_port_settings.music == 0 ? 4U : 0U));
}

void port_WindowApplyChoice(s32 choice) {
    const char *name = port_FindChoice(choice)->name;

    if (strcmp(name, "fullscreen") == 0) {
        port_SetFullscreen(g_port_settings.is_fullscreen);
    } else if (strcmp(name, "fps") == 0) {
        port_SetCounter(g_port_settings.fps);
    } else if (strcmp(name, "vsync") == 0) {
        g_port_vsync =
            g_port_window_options.is_fast ? 0 : g_port_settings.vsync;
        if (g_port_has_gl) {
            port_SetSwapInterval();
        } else {
            SDL_RenderSetVSync(g_port_renderer, g_port_vsync != 0);
        }
    } else if (strcmp(name, "filter") == 0 && !g_port_has_gl) {
        /* SDL's renderer takes the filter as a texture is made */
        SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY,
                    g_port_settings.is_linear ? "1" : "0");
        g_port_texture_w = 0;
    } else if (
        (strcmp(name, "stage_mesh") == 0 ||
         strcmp(name, "stage_props_mesh") == 0 ||
         strcmp(name, "shadows") == 0 || strcmp(name, "smooth_light") == 0 ||
         strcmp(name, "lights") == 0 || strcmp(name, "effects") == 0 ||
         strcmp(name, "sky") == 0 || strcmp(name, "see_through") == 0 ||
         strcmp(name, "stage_lights") == 0 ||
         strcmp(name, "effect_lights") == 0 ||
         strcmp(name, "reflections") == 0 ||
         strcmp(name, "ambient_occlusion") == 0 || strcmp(name, "bloom") == 0 ||
         strcmp(name, "tonemap") == 0 || strcmp(name, "fighter_models") == 0) &&
        g_port_has_gl) {
        PortGlOptions options;

        port_GlOptionsFor(g_port_gl_scale, &options);
        port_GlSetOptions(&options);
    } else if (strcmp(name, "music_mods") == 0) {
        port_MusicSetOn(g_port_settings.music_mods);
    } else if (strcmp(name, "run_ahead") == 0) {
        port_SetRunAhead(g_port_settings.run_ahead);
    } else if (strcmp(name, "stage_kind") == 0) {
        port_SetStageKindDefault(g_port_settings.stage_kind);
    } else if (strcmp(name, "time_of_day") == 0) {
        port_SetStageTimeDefault(g_port_settings.time_of_day);
    } else if (strcmp(name, "unlock_all") == 0) {
        port_SetUnlockAll(g_port_settings.unlock_all);
    } else if (strcmp(name, "stage_select_everywhere") == 0) {
        port_SetStageSelectEverywhere(g_port_settings.stage_select_everywhere);
    } else if (strcmp(name, "fight_music") == 0) {
        port_SetFightMusic(g_port_settings.fight_music);
    } else if (
        strcmp(name, "match_rule") == 0 || strcmp(name, "match_count") == 0) {
        port_SetMatchDefault(
            g_port_settings.match_rule, g_port_settings.match_count);
    } else if (strcmp(name, "bugfixes") == 0) {
        /* the game's own, at once, as the options page at boot set it */
        port_WindowGameSwitches();
        port_SetBugfixes(g_port_settings.bugfixes);
    } else if (
        strcmp(name, "replays") == 0 || strcmp(name, "replays_keep") == 0 ||
        strcmp(name, "replays_megabytes") == 0) {
        port_ApplyReplaySettings();
    } else if (strcmp(name, "difficulty") == 0 || strcmp(name, "music") == 0 ||
               strcmp(name, "monitor") == 0) {
        port_WindowGameSwitches();
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
        u32 held = port_PadHeld(pad);
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
    if (key->keysym.scancode == SDL_SCANCODE_ESCAPE) {
        /* the window's own: pause and back, never the end of the program */
        g_port_key_events++;
        port_TakeEscape(down);
        return;
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
            /* off, on, and on with the times under it */
            port_SetCounter((g_port_show_fps + 1) % (PORT_FPS_TIMES + 1));
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
    if (key->keysym.scancode == SDL_SCANCODE_F9) {
        /* the window's own: a report of a problem, its picture taken as
         * F12's is (port_WriteShot) */
        if (down) {
            g_port_wants_report = TRUE;
            g_port_wants_shot = TRUE;
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
static void port_MakePress(s32 pad, u32 button, s32 down) {
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
}

/* ... kept until the events are next read at a blank: a press of a
 * plan's frame is made as the frame begins, after the blank and before
 * the game takes its input, where the window reads the events again
 * (port_ReadInputLate) and would have it a frame sooner than the plan
 * says. */
static s32 port_InjectPress(s32 pad, u32 button, s32 down) {
    PortInjectWaiting *waiting;

    if (g_port_inject_waiting_count >= INJECT_WAITING_MAX) {
        port_Say(
            "--inject: more than %d presses at one blank", INJECT_WAITING_MAX);
        port_Quit(PORT_EXIT_FAILED);
    }
    waiting = &g_port_inject_waiting[g_port_inject_waiting_count++];
    waiting->pad = pad;
    waiting->button = button;
    waiting->down = down;
    return 1;
}

/* The events SDL has, read: the keys and controllers' state, and the
 * window's own keys. */
static void port_ReadEvents(void) {
    SDL_Event event;
    s32 pad;

    while (SDL_PollEvent(&event)) {
        if (port_ReplayWindowEvent(&event)) {
            /* a playback's key or button (sdl_replay.c) */
            continue;
        }
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
}

/* A vertical blank, before the pads are filled (port_platform.h). */
static void port_ReadInput(u32 blank) {
    s32 was = port_ProfileSwitch(PORT_PROFILE_WINDOW);
    s32 i;

    (void)blank;
    if (port_OnStepper()) {
        /* a step beside the window's thread: the events are that thread's
         * to read, and were read at the frame's boundary; the pads are
         * what the keys and controllers held then (docs/PORT.md, "The step
         * on a thread of its own") */
        port_SendPads();
        port_ProfileSwitch(was);
        return;
    }
    port_OpenWindow();
    for (i = 0; i < g_port_inject_waiting_count; i++) {
        const PortInjectWaiting *waiting = &g_port_inject_waiting[i];

        port_MakePress(waiting->pad, waiting->button, waiting->down);
    }
    g_port_inject_waiting_count = 0;
    port_PressEscapeForTest(blank);
    port_ReadEvents();
    port_SendPads();
    port_ProfileSwitch(was);
}

/* Just before the game takes its input, at a frame's boundary
 * (port_platform.h): the events read again, and what the keys and
 * controllers hold now put into the input the pads' driver read at the
 * blank, the plan's buttons as they were then. A press is taken up to a
 * blank sooner: in a fight the driver's read was a whole blank before
 * the game takes it, the window's wait for the blank's time between
 * them (docs/PORT.md, "Input lag"). */
static void port_ReadInputLate(void) {
    s32 was;

    if (!g_port_is_open) {
        return;
    }
    was = port_ProfileSwitch(PORT_PROFILE_WINDOW);
    /* with the step on a thread of its own, the boundary is where the
     * window's thread reads the keys at all */
    port_PressEscapeForTest(port_GetBlankCount());
    port_ReadEvents();
    port_SendPads();
    port_ReadDevicesNow();
    if (g_port_profile_file != NULL) {
        /* when the game took its input, in microseconds as the `p` lines:
         * a press's lag to the picture that shows it (tools/port_bench.py
         * --paced) */
        fprintf(
            g_port_profile_file, "k %llu\n", (unsigned long long)port_Micros());
    }
    /* in a load, no frame is run ahead across it (step.c) */
    g_port_ahead_held = port_LoadKind() != PORT_LOAD_NONE;
    if (g_port_has_gl && g_port_tween_on && port_GetRunAhead() > 0 &&
        port_TweenTakeMirrorBehind()) {
        /* A picture between frames that could not keep what it changed of
         * the card's mirror of video memory put back what video memory
         * holds at its blank, which, with frames run ahead, is a frame or
         * two behind the frames the card draws; the step after them tells
         * the card nothing. So the mirror is sent video memory as it is
         * now, before the frames are run ahead and their writes go over
         * it. (Every picture did, before the mirror was put back by what
         * the picture changed of it: the whole of video memory sent at
         * every boundary.) The stage's pages are not looked up again for
         * it (gl.h, port_GlRestoreVram): the frames run ahead tell the
         * card of each page they write. */
        port_ProfileSwitch(PORT_PROFILE_RASTER);
        port_PaintSyncAll();
        port_ProfileSwitch(PORT_PROFILE_CARD);
        port_GlRestoreVram(0, 0, PORT_VRAM_WIDTH, PORT_VRAM_HEIGHT);
    }
    port_ProfileSwitch(was);
}

/* ---- the frames run ahead (step.c) ---- */

/* A blank of a frame run ahead: the card's frame is ended there as at
 * any blank, so that each frame the game draws ahead is one of the
 * card's and of the pictures between frames, and nothing is shown. */
static void port_AheadBlankTurn(u32 blank) {
    (void)blank;
    /* no frame is run ahead into a load: its blanks are the step's own,
     * hurried through or paced (quick_loading), and stop the frames
     * ahead where it begins */
    if (port_LoadKind() != PORT_LOAD_NONE) {
        port_GiveUpAhead();
        return;
    }
    /* a frame the card does not draw has nothing to end
     * (g_port_ahead_kept) */
    if (g_port_has_gl && !g_port_ahead_kept) {
        s32 was = port_ProfileSwitch(PORT_PROFILE_FINISH);

        port_RemasterShow();
        port_GlFinishOrRecord();
        port_ProfileSwitch(was);
    }
}

/* The frames ahead are done, their state still in place: the display
 * they left is kept, with its pixels, and the card's frame is ended with
 * it, so that while the step itself runs the window shows what the
 * display will hold that many frames on. */
static void port_AheadDone(void) {
    PortDisplay *display = &g_port_ahead_display;
    s32 w;
    s32 h;

    port_GetDisplay(display);
    if (g_port_has_gl) {
        s32 was = port_ProfileSwitch(PORT_PROFILE_FINISH);

        port_RemasterShow();
        port_GlFinishOrRecord();
        port_ProfileSwitch(was);
    }
    w = display->w < PORT_PICTURE_WIDTH_MAX
            ? display->w
            : PORT_PICTURE_WIDTH_MAX;
    h = display->h < PORT_PICTURE_HEIGHT_MAX
            ? display->h
            : PORT_PICTURE_HEIGHT_MAX;
    /* the software GPU's picture of them, which only the original
     * picture shows: not painted where it is not (g_port_ahead_unpainted) */
    if (display->is_on && w > 0 && h > 0 && !g_port_ahead_unpainted) {
        s32 was = port_ProfileSwitch(PORT_PROFILE_PICTURE);

        port_PaintSyncAll();
        port_ConvertDisplay(g_port_vram, display->x, display->y, w, h,
                            display->is_rgb24, g_port_frame);
        port_ProfileSwitch(was);
    }
    /* frames run ahead from a state about to be put back, beside the
     * window: their record is drawn first (port_FlushRecord) */
    if (g_port_ahead && g_port_has_gl && port_GlIsRecording()) {
        port_FlushRecord();
    }
}

/* The sound's samples at each blank of a step whose window's turns are
 * held (step.c), for the pace when its turn comes: the step has mixed the
 * sound of all its blanks by then. */
#define HELD_SAMPLES_MAX 8
static u32 g_port_held_blank[HELD_SAMPLES_MAX];
static u32 g_port_held_samples[HELD_SAMPLES_MAX];
static s32 g_port_held_noted;

/* A blank of the step that is the first frame ahead (step.c): the card's
 * frame is ended there, as at a frame ahead's blank, and its turn is
 * given after the step. Not where a load begins: its blanks are shown as
 * they come. */
static s32 port_HeldBlankTurn(u32 blank) {
    if (port_LoadKind() != PORT_LOAD_NONE) {
        return FALSE;
    }
    g_port_held_blank[g_port_held_noted % HELD_SAMPLES_MAX] = blank;
    g_port_held_samples[g_port_held_noted % HELD_SAMPLES_MAX] =
        port_GetSampleCount();
    g_port_held_noted++;
    if (g_port_has_gl && !g_port_ahead_kept) {
        s32 was = port_ProfileSwitch(PORT_PROFILE_FINISH);

        port_RemasterShow();
        port_GlFinishOrRecord();
        port_ProfileSwitch(was);
    }
    return TRUE;
}

/* The sound's samples at `blank`: as they were at it where its turn was
 * held, and how many have been mixed since (0 where it was not). */
static u32 port_BlankSamples(u32 blank, u32 *since) {
    u32 now = port_GetSampleCount();
    s32 i;

    *since = 0;
    for (i = 0; i < HELD_SAMPLES_MAX && i < g_port_held_noted; i++) {
        if (g_port_held_blank[i] == blank) {
            *since = now - g_port_held_samples[i];
            g_port_held_blank[i] = 0;
            return g_port_held_samples[i];
        }
    }
    return now;
}

/* The display the window shows: the frames run ahead left it, where they
 * were run before this step, and otherwise it is the console's own. */
static void port_ShownDisplay(PortDisplay *display) {
    if (g_port_unseen) {
        *display = g_port_ahead_display;
    } else {
        port_GetDisplay(display);
    }
}

/* ---- the pictures of one blank ---- */

/* The picture that is ready, shown; or, where `for_refresh` is not 0,
 * handed over for that refresh and not waited for (the step's refresh,
 * below): the display shows it then while the program goes on, and the
 * next picture is shown at the refresh after it. */
static void port_HandPicture(u32 for_refresh) {
    s32 was = port_ProfileSwitch(PORT_PROFILE_SWAP);
    Uint64 handed = port_Micros();
    Uint64 drawn = 0;
    Uint64 swapped;

    if (g_port_has_gl && g_port_vsync && g_port_settings.low_latency &&
        for_refresh == 0) {
        /* `low_latency`: the card's work for it done before it is handed
         * over, and when, which is what the press's way to the screen
         * waits for (port_LateRead). A display shows a picture at the
         * first refresh after the card has drawn it, and so does the
         * made-up one. */
        port_GlWaitForCard();
        drawn = port_Micros();
        port_NoteLateDrawn();
    }
    if (g_port_window_options.display_rate > 0 && g_port_vsync &&
        for_refresh == 0) {
        port_WaitForMadeUpBlank();
    }
    if (g_port_has_gl) {
        if (for_refresh == 0 || g_port_window_options.display_rate == 0) {
            SDL_GL_SwapWindow(g_port_window);
        }
        /* the driver's first time of a thread ended, on the loading
         * screen (warm.c) */
        port_WarmDriver();
        if (g_port_vsync && g_port_settings.low_latency && for_refresh == 0) {
            /* ... and back only once the swap is made, so that the next
             * picture is begun from when this one went up and none waits
             * in the driver's queue behind another (docs/PORT.md, "Input
             * lag") */
            port_GlWaitForCard();
        }
    } else {
        SDL_RenderPresent(g_port_renderer);
    }
    port_ProfileSwitch(was);
    if (g_port_vsync) {
        port_NoteSwap(
            &g_port_refresh, for_refresh != 0 ? for_refresh : port_ReadClock());
    }
    if (for_refresh == 0) {
        port_NoteLateShown();
    }
    /* when the swap came back, in microseconds, and the picture's phase:
     * for --profile, and for a report; a picture not waited for, when it
     * is shown */
    swapped = for_refresh != 0
                  ? (Uint64)for_refresh * MICROSECONDS / PORT_SPU_RATE
                  : port_Micros();
    port_ReportNoteSwap(swapped, g_port_profile_phase);
    if (g_port_profile_file != NULL) {
        /* then 1 for a picture not waited for, the microseconds waited
         * for the clock since the picture before, when the picture
         * began to be drawn, was handed over and had been drawn by the
         * card (0: not waited for) (tools/port_bench.py: the press to the
         * picture, part by part) */
        fprintf(g_port_profile_file, "p %llu %d %d %u %llu %llu %llu\n",
                (unsigned long long)swapped, g_port_profile_phase,
                for_refresh != 0 ? 1 : 0,
                (u32)((u64)g_port_waited * MICROSECONDS / PORT_SPU_RATE),
                (unsigned long long)(g_port_picture_began != 0
                                         ? g_port_picture_began
                                         : handed),
                (unsigned long long)handed, (unsigned long long)drawn);
    }
    g_port_picture_began = 0;
    g_port_waited = 0;
    g_port_queued_for = for_refresh;
    g_port_pictures++;
    port_CountFps(&g_port_shown_meter, port_ReadClock(), 1);
}

static void port_SwapPicture(void) { port_HandPicture(0); }

/* A picture between the frame on show and the frame after it, at the
 * phase the clock has reached, ready to be shown. FALSE where there is
 * none: the frames are unlike, or nothing has been written down yet, and
 * what the window holds is then the game's own picture.
 *
 * The phase is where the moment the picture will be on the screen lies
 * between the moment the frame on show went up and the moment the frame
 * after it will (port_PacePhase): 0 is the frame on show, and the last
 * phase is the frame the game has already worked out and is about to put
 * up. So nothing is guessed ahead. The moments are the blanks' own, as
 * the pace keeps them, and not when the game happened to finish a step;
 * and the moment of a picture is the refresh it will be shown at, not
 * the moment it was begun (pace.c). */
static s32 port_DrawTween(s32 slot) {
    s32 window_w = 0;
    s32 window_h = 0;
    s32 phase;
    s32 was;
    u32 now;
    u32 shown_at;

    if (!g_port_tween_on || !port_ShowsRemastered() ||
        port_RemasterIsHolding() || g_port_frame_period == 0 ||
        !port_TweenCanDraw()) {
        return FALSE;
    }
    now = port_ReadClock();
    if (g_port_vsync) {
        shown_at = port_NextRefresh(&g_port_refresh, now, g_port_draw_cost);
    } else if (slot > 0 && (s32)(g_port_next_picture - now) > 0) {
        /* the clock holds the rate: it is shown when its slot comes */
        shown_at = g_port_next_picture;
    } else {
        shown_at = now + g_port_draw_cost;
    }
    phase = port_PacePhase(shown_at, g_port_frame_at, g_port_lag,
                           g_port_frame_period, PORT_TWEEN_ONE);
    g_port_picture_began = port_Micros();
    was = port_ProfileSwitch(PORT_PROFILE_TWEEN);
    g_port_gpu_base = PORT_GPU_TWEEN;
    if (!port_TweenDraw(phase)) {
        g_port_gpu_base = PORT_GPU_SCENE;
        port_ProfileSwitch(was);
        return FALSE;
    }
    g_port_gpu_base = PORT_GPU_SCENE;
    port_WindowSize(&window_w, &window_h);
    port_GlClearWindow(window_w, window_h);
    port_ShowRemastered(window_w, window_h);
    port_ProfileSwitch(was);
    /* what a picture takes to draw, at its recent peak */
    now = port_ReadClock() - now;
    g_port_draw_cost =
        now > g_port_draw_cost ? now : port_PaceAverage(g_port_draw_cost, now);
    g_port_profile_phase = phase;
    g_port_pictures_between++;
    return TRUE;
}

/* Whether another picture is drawn in this blank, after `pictures` of
 * them, the last shown just now. With the display waited for and its
 * refreshes known: where its next refresh comes before the next blank's
 * first picture can be ready -- the blank's own moment and then the
 * game's step and that picture's drawing -- the display would show the
 * last picture twice, so one is drawn for it here. Otherwise while more
 * than half a slot of the blank is left. */
static s32 port_WantsAnotherPicture(u32 due, s32 slot, s32 pictures) {
    u32 now = port_ReadClock();

    if (slot == 0 || pictures >= PORT_PICTURES_A_BLANK_MAX) {
        return FALSE;
    }
    if (port_WantsLaterShot(pictures - 1)) {
        return TRUE;
    }
    if (g_port_vsync && g_port_refresh.period != 0) {
        u32 next = port_NextRefresh(&g_port_refresh, now, g_port_draw_cost);
        /* never more than a refresh past the blank's own moment, and
         * none once that moment has passed: a PC that cannot keep up
         * catches up rather than drawing more */
        u32 reach = g_port_ready_after < g_port_refresh.period
                        ? g_port_ready_after
                        : g_port_refresh.period;

        return (s32)(due - now) > 0 && (s32)(next - due) < (s32)reach;
    }
    return (s32)(due - now) > slot / 2;
}

/* The pictures of this blank, shown when each is due; `due` is the
 * clock's reading at which the blank itself is.
 *
 * With `frame_rate original` there is one picture, the game's own, and
 * it is shown at the blank's own moment, which is what the window always
 * did. Otherwise a picture is shown every slot of the rate asked for, or
 * at each of the display's refreshes where it is waited for, and each is
 * a picture of the moment it is shown at -- the first as well, which is
 * drawn again between two frames where it can be (port_DrawTween). With
 * the display waited for, the swap is the wait, and the pace takes up
 * what a swap ran over (pace.c). */
static void port_ShowPictures(u32 due) {
    s32 rate = port_PictureRate();
    s32 slot = rate > 0 ? PORT_SPU_RATE / rate : 0;
    s32 pictures = 0;
    s32 left;
    u32 ready;

    g_port_refresh.period = g_port_vsync && g_port_display_rate > 0
                                ? (u32)(PORT_SPU_RATE / g_port_display_rate)
                                : 0;
    /* A blank more than a refresh late shows nothing, where the one before
     * showed something: its swap would wait for the display's next
     * refresh and make the game later still, and the display shows the
     * picture before for that refresh either way. So with the display
     * waited for, a step that ran long costs a refresh of the picture and
     * not the game's pace, which is what `vsync = off` was for. */
    if (g_port_refresh.period != 0 && !g_port_wants_shot &&
        !g_port_window_options.is_fast && !g_port_unshown &&
        g_port_pace.late > g_port_refresh.period) {
        g_port_unshown = TRUE;
        g_port_pictures_unshown++;
        if (g_port_profile_file != NULL) {
            /* a blank too late to show, and by how much, in microseconds
             * (tools/port_bench.py) */
            fprintf(
                g_port_profile_file, "u %llu %u\n",
                (unsigned long long)port_Micros(),
                (u32)((u64)g_port_pace.late * MICROSECONDS / PORT_SPU_RATE));
        }
        g_port_wants_lag = FALSE;
        g_port_last_due = due;
        g_port_left_at = port_ReadClock();
        return;
    }
    g_port_unshown = FALSE;
    /* The first picture: the game's own frame as the blank began, and,
     * where pictures between frames are drawn, the picture of the moment
     * it will be shown at instead -- unless a test asked for the game's
     * own (--shot-at N,0). */
    if (slot != 0 && !(g_port_wants_shot && g_port_shot_picture == 0) &&
        port_DrawTween(slot)) {
        port_DrawCounter();
    }
    /* how long after the last blank's pictures this one's first is ready:
     * the game's step and that picture, at its recent peak */
    port_NoteStepRefresh(port_ReadClock(), g_port_queued_for);
    ready = port_ReadClock() - g_port_left_at;
    g_port_ready_after = ready > g_port_ready_after
                             ? ready
                             : port_PaceAverage(g_port_ready_after, ready);
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
        if (pictures == 0 && g_port_wants_lag) {
            /* how long after its frame's moment a frame's first picture
             * is on the screen: what every picture's phase is counted
             * back by (port_PacePhase) */
            u32 lag = port_ReadClock() - g_port_frame_at;

            g_port_lag =
                g_port_has_lag ? port_PaceAverage(g_port_lag, lag) : lag;
            g_port_has_lag = TRUE;
            g_port_wants_lag = FALSE;
        }
        pictures++;
        if (!port_WantsAnotherPicture(due, slot, pictures)) {
            break;
        }
        if (!port_DrawTween(slot)) {
            break;
        }
        port_DrawCounter();
        port_DrawNetLine();
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
    g_port_last_due = due;
    g_port_left_at = port_ReadClock();
}

/* ---- the step's refresh (docs/PORT.md, "The step's refresh") ---- */

/* What is kept to spare before a refresh, in the clock's samples (a
 * millisecond). */
#define STEP_REFRESH_SPARE (PORT_SPU_RATE / 1000)

/* A step whose window's turns are held begins (step.c), its input read:
 * where the display's next refresh comes before the step and its first
 * picture can be ready (the time they take, on average), that
 * refresh would show the picture before it again. A picture between the
 * two frames written down already is drawn for it now, and handed over
 * without waiting for the card: the step runs while the display shows
 * it, and the step's own first picture is shown at the refresh after,
 * which is the one it would have made anyway. The input was read as
 * before, so nothing is later. Only with the display waited for and its
 * refreshes known; never for a picture a test or the player asked for. */
static void port_StepRefresh(void) {
    u32 now;
    u32 next;

    port_LateRead();
    now = port_ReadClock();
    g_port_step_pending = TRUE;
    g_port_step_began = now;
    g_port_step_spent = 0;
    /* a step run beside the window's thread has its pictures drawn while
     * it runs (port_BesideStep) */
    if (port_RunsBeside()) {
        return;
    }
    if (!g_port_step_refresh_on || !g_port_is_open || !g_port_vsync ||
        g_port_refresh.period == 0 || !g_port_refresh.is_seen ||
        g_port_window_options.is_fast || g_port_wants_shot ||
        port_PictureRate() == 0 || g_port_step_ready == 0) {
        return;
    }
    next = port_NextRefresh(&g_port_refresh, now, g_port_draw_cost);
    /* a refresh is seen a little after it comes, at the swap's return:
     * the step's picture is held to it with that much to spare */
    if ((s32)(now + g_port_step_ready + STEP_REFRESH_SPARE - next) <= 0) {
        return;
    }
    if (!port_DrawTween(1)) {
        return;
    }
    port_DrawCounter();
    port_HandPicture(next);
    g_port_step_spent = port_ReadClock() - now;
    g_port_step_refreshes++;
}

/* The held step's first picture is drawn, at `now`: what the step and it
 * took, less the picture shown before it, into their average; and whether
 * that picture was needed after all (the log's count). */
static void port_NoteStepRefresh(u32 now, u32 shown_for) {
    u32 took;

    if (!g_port_step_pending) {
        return;
    }
    g_port_step_pending = FALSE;
    if (g_port_step_done) {
        u32 after = now - g_port_step_done_at;

        g_port_step_done = FALSE;
        g_port_after_ready = g_port_after_ready == 0
                                 ? after
                                 : port_PaceAverage(g_port_after_ready, after);
    }
    took = now - g_port_step_began - g_port_step_spent;
    g_port_step_ready =
        g_port_step_ready == 0 ? took
                               : port_PaceAverage(g_port_step_ready, took);
    if (shown_for != 0 &&
        (s32)(g_port_step_began + took - shown_for) + STEP_REFRESH_SPARE < 0) {
        g_port_step_refresh_early++;
    }
}

/* ---- the step on a thread of its own (docs/PORT.md, "The step on a
 * thread of its own") ---- */

/* A step whose blanks the window holds (step.c) runs on a thread of its
 * own while the window's thread draws the pictures the display's
 * refreshes ask for meanwhile. The step calls no OpenGL: what it hands
 * the card is written down (gl_record.h) and drawn here once it has
 * ended, before its blanks' turns. Its input was read at the boundary,
 * on this thread, as ever. A step that comes to what only this thread
 * may do (a blank's turn given while it runs: a load begins) is moved
 * here, and goes on where it was (port_StepToHost); a step whose frames
 * ahead put the state back has its record drawn before they do
 * (port_FlushRecord). RB_STEP_THREAD=off runs every step on the
 * window's thread, as before. */
enum {
    STEPPER_RUNNING,
    STEPPER_DONE,  /* the step and its frames ahead have run */
    STEPPER_MOVED, /* ... or it is to go on on the window's thread */
    STEPPER_FLUSH  /* it waits for its record to be drawn */
};

static SDL_Thread *g_port_stepper;
static SDL_threadID g_port_stepper_id;
static SDL_sem *g_port_stepper_go;
static SDL_sem *g_port_stepper_back;
static SDL_sem *g_port_stepper_resume;
static SDL_atomic_t g_port_stepper_state;
static SDL_atomic_t g_port_stepper_stop;
static s32 g_port_stepper_moving;
static s32 g_port_stepper_on = TRUE;
/* the step being run beside the window's thread now */
static s32 g_port_beside;
/* the latest refresh a picture beside the step was handed over for */
static u32 g_port_beside_until;
static s32 g_port_has_beside_until;
/* The record drawn as it is written (RB_STEP_STREAM=off: once the step
 * has ended), when the frame's list began, and how long it takes to the
 * step's end, on average. */
static s32 g_port_stream_on = TRUE;
static u32 g_port_list_from;
static u32 g_port_list_left;
static u32 g_port_steps_beside;
static u32 g_port_steps_moved;
static u32 g_port_pictures_beside;
/* a picture the input's late read handed over for a refresh in its wait
 * (port_LateRead) */
static s32 g_port_late_beside;
/* When the step's thread took the step up and when it said it was done,
 * in microseconds as the `p` lines: what the hand-overs cost (`e`). */
static Uint64 g_port_stepper_woke;
static Uint64 g_port_stepper_ended;
/* what the step's thread was told of the processors it asked for
 * (PORT_CPU_ASKED...) */
static s32 g_port_stepper_cpus;

static s32 port_OnStepper(void) {
    return g_port_stepper != NULL && SDL_ThreadID() == g_port_stepper_id;
}

static s32 port_ProfileThreadOf(void) { return port_OnStepper(); }

/* The game's functions begun off its stack by a thread other than the
 * step's while the step runs on it (g_port_foreign_call_hook), and the
 * first of them: none is to be. Such a call runs the game's code beside
 * the game, on memory the step is changing and run-ahead puts back
 * (docs/PORT.md, "The window's thread and the game's state"): the
 * window's thread asking the stage's number of resource_GetStage, as
 * the mesh decided which primitives it draws, did, and that stopped
 * the owner's fight on 2026-11-18. Said once in the log as it is first
 * seen, and their count as the session ends; tools/port_bench.py fails a
 * run whose log says so. */
static SDL_atomic_t g_port_foreign_calls;
static SDL_atomic_t g_port_foreign_first;
static s32 g_port_foreign_said;

static void port_OnForeignCall(u32 function) {
    if (port_OnStepper() ||
        SDL_AtomicGet(&g_port_stepper_state) != STEPPER_RUNNING ||
        !g_port_beside) {
        return;
    }
    if (SDL_AtomicIncRef(&g_port_foreign_calls) == 0) {
        SDL_AtomicSet(&g_port_foreign_first, (int)function);
    }
}

/* The first such call said, on the window's thread. */
static void port_SayForeignCalls(void) {
    u32 function;
    u32 offset = 0;
    const char *name;

    if (g_port_foreign_said || SDL_AtomicGet(&g_port_foreign_calls) == 0) {
        return;
    }
    g_port_foreign_said = TRUE;
    function = (u32)SDL_AtomicGet(&g_port_foreign_first);
    name = port_FindFunction(function, &offset);
    port_Note("rebushido: a function of the game was begun beside the "
              "step, off its thread: %s (0x%08X); the game's code is the "
              "step's alone while it runs",
              name != NULL ? name : "?", function);
}

/* The step's thread: a step each time it is given one, entered here, and
 * the frames ahead after it, unless it went to the window's thread. */
static int SDLCALL port_StepperThread(void *unused) {
    (void)unused;
    g_port_stepper_id = SDL_ThreadID();
    /* the larger cache's processors, where the processor has one
     * (cpu_prefer; docs/PORT.md, "The processors with the larger
     * cache"); the window's thread waits for this and says it */
    g_port_stepper_cpus = port_CpuPreferHere();
    SDL_SemPost(g_port_stepper_back);
    port_ProfileSwitch(PORT_PROFILE_WAIT);
    for (;;) {
        SDL_SemWait(g_port_stepper_go);
        if (SDL_AtomicGet(&g_port_stepper_stop)) {
            return 0;
        }
        g_port_stepper_woke = port_Micros();
        port_ProfileSwitch(PORT_PROFILE_GAME);
        g_port_stepper_moving = FALSE;
        port_HostEnterGame();
        if (!g_port_stepper_moving) {
            /* what the frames ahead after it draw waits for the step's
             * thread to be still (gl_record.h) */
            port_GlRecordHold();
            port_AfterStepAhead();
        }
        port_ProfileSwitch(PORT_PROFILE_WAIT);
        g_port_stepper_ended = port_Micros();
        SDL_AtomicSet(&g_port_stepper_state,
                      g_port_stepper_moving ? STEPPER_MOVED : STEPPER_DONE);
        SDL_SemPost(g_port_stepper_back);
    }
}

/* In the step, on its thread: what follows is the window's thread's
 * (g_port_step_to_host). The step leaves its thread where it is, and the
 * window's enters it there; on any other thread, nothing. */
static void port_StepToHost(void) {
    if (!port_OnStepper()) {
        return;
    }
    g_port_stepper_moving = TRUE;
    port_HostLeaveGame();
    /* on the window's thread from here */
}

/* In the step, on its thread: the record so far drawn now, by the
 * window's thread, before the step goes on (the state about to be put
 * back is what the backend reads of the game's memory). */
static void port_FlushRecord(void) {
    if (!port_OnStepper()) {
        return;
    }
    SDL_AtomicSet(&g_port_stepper_state, STEPPER_FLUSH);
    SDL_SemPost(g_port_stepper_back);
    SDL_SemWait(g_port_stepper_resume);
}

/* The program ends from inside the step: on the window's thread. */
static void port_CloseWindow(s32 code);

static void port_CloseFromStep(s32 code) {
    port_StepToHost();
    port_CloseWindow(code);
}

/* Whether this held step runs beside the window's thread. */
static s32 port_RunsBeside(void) {
    return g_port_stepper != NULL && g_port_has_gl && g_port_is_open;
}

static void port_DrawRecord(void) {
    s32 was = port_ProfileSwitch(PORT_PROFILE_CARD);

    port_GlDrawPending();
    port_ProfileSwitch(was);
}

/* Waits for the step's thread to say something, `samples` of the clock at
 * the most: TRUE where it did. Most of the wait asleep, the end awake. */
static s32 port_WaitForStepper(u32 samples) {
    u32 until = port_ReadClock() + samples;

    for (;;) {
        s32 left = (s32)(until - port_ReadClock());

        if (left <= 0) {
            return SDL_SemTryWait(g_port_stepper_back) == 0;
        }
        if (left > WAIT_AWAKE) {
            Uint32 ms =
                (Uint32)(left - WAIT_AWAKE) * MILLISECONDS / PORT_SPU_RATE;

            if (SDL_SemWaitTimeout(g_port_stepper_back, ms > 0 ? ms : 1) == 0) {
                return TRUE;
            }
        } else if (SDL_SemTryWait(g_port_stepper_back) == 0) {
            return TRUE;
        }
    }
}

/* Whether pictures are drawn beside the step: as for the step's refresh,
 * with the display waited for and its refreshes known, never with
 * --fast, and never for a picture a test or the player asked for. */
static s32 port_MayDrawBeside(void) {
    return g_port_step_refresh_on && g_port_vsync &&
           g_port_refresh.period != 0 && g_port_refresh.is_seen &&
           !g_port_window_options.is_fast && !g_port_wants_shot &&
           port_PictureRate() != 0 && g_port_step_ready != 0;
}

/* The step's thread has something to say (port_WaitForStepper): its state
 * other than running, and then any other word it posted drained. */
static s32 port_StepperSays(void) {
    s32 state = SDL_AtomicGet(&g_port_stepper_state);

    if (state != STEPPER_RUNNING) {
        while (SDL_SemTryWait(g_port_stepper_back) == 0) {
        }
    }
    return state;
}

/* A picture between the two frames written down already, for the refresh
 * `next`, handed over and not waited for. */
static void port_DrawBeside(u32 next) {
    if (port_DrawTween(1)) {
        port_DrawCounter();
        port_HandPicture(next);
        g_port_pictures_beside++;
    }
    g_port_beside_until = next;
    g_port_has_beside_until = TRUE;
    port_ProfileSwitch(PORT_PROFILE_WAIT);
}

/* While the step runs. For each refresh of the display that comes while
 * the step still runs with no more time before it than its record and
 * its first picture take after it (on average, and a millisecond to
 * spare), a picture between the two frames written down already, handed
 * over for that refresh and not waited for. Nothing is guessed ahead: a
 * step that ends in time for a refresh has its own picture shown at it.
 * Once the step's frame's list has begun, its record is drawn as it is
 * written (port_GlDrawSoFar), so that little of it is left once the step
 * has ended; no picture can be drawn meanwhile, so the next refresh is
 * given one first where the list, the rest of the record and the step's
 * first picture would miss it. Returns what the step's thread said. */
static s32 port_BesideStep(void) {
    s32 was = port_ProfileSwitch(PORT_PROFILE_WAIT);
    s32 is_drawing = FALSE;
    s32 state;

    for (;;) {
        u32 now;
        u32 next;
        u32 after;
        s32 left;

        state = port_StepperSays();
        if (state != STEPPER_RUNNING) {
            break;
        }
        now = port_ReadClock();
        next = port_NextRefresh(&g_port_refresh, now, g_port_draw_cost);
        /* what the step's own first picture needs once the step has
         * ended: the rest of its record drawn, and the picture */
        after = g_port_after_ready > g_port_draw_cost ? g_port_after_ready
                                                      : g_port_draw_cost;
        if (!is_drawing && g_port_stream_on && port_GlRecordHasDraws()) {
            if (port_MayDrawBeside() &&
                !(g_port_has_beside_until &&
                  (s32)(next - g_port_beside_until) <= 0) &&
                (s32)(next - now - g_port_list_left - after -
                      STEP_REFRESH_SPARE) <= 0) {
                port_DrawBeside(next);
            }
            is_drawing = TRUE;
            g_port_list_from = port_ReadClock();
        }
        if (is_drawing) {
            u32 made;

            port_ProfileSwitch(PORT_PROFILE_CARD);
            made = port_GlDrawSoFar();
            port_ProfileSwitch(PORT_PROFILE_WAIT);
            if (made == 0) {
                SDL_SemWaitTimeout(g_port_stepper_back, 1);
            }
            continue;
        }
        if (!port_MayDrawBeside()) {
            SDL_SemWait(g_port_stepper_back);
            continue;
        }
        if (g_port_has_beside_until && (s32)(next - g_port_beside_until) <= 0) {
            /* that one has its picture: until a picture begun would make
             * the one after it */
            left = (s32)(g_port_beside_until - g_port_draw_cost - now) + 1;
        } else {
            left = (s32)(next - after - STEP_REFRESH_SPARE - now);
            if (left <= 0) {
                port_DrawBeside(next);
                continue;
            }
        }
        (void)port_WaitForStepper(left > 0 ? (u32)left : 0);
    }
    if (is_drawing) {
        u32 took = port_ReadClock() - g_port_list_from;

        g_port_list_left =
            g_port_list_left == 0 ? took
                                  : port_PaceAverage(g_port_list_left, took);
    }
    port_ProfileSwitch(was);
    return state;
}

/* The step's thread's word that the frame's list has begun, which ends the
 * window's thread's wait (port_BesideStep). */
static void port_WakeWindow(void) { SDL_SemPost(g_port_stepper_back); }

/* A held step (g_port_step_runner): on the step's thread, its drawing
 * written down, the window's thread drawing pictures meanwhile; then its
 * record drawn here. */
static void port_RunStep(void) {
    s32 state;

    if (!port_RunsBeside()) {
        port_HostEnterGame();
        return;
    }
    g_port_beside = TRUE;
    /* a refresh the input's late read gave a picture to has it */
    g_port_has_beside_until = g_port_late_beside;
    g_port_late_beside = FALSE;
    port_GlStepRecords(TRUE);
    SDL_AtomicSet(&g_port_stepper_state, STEPPER_RUNNING);
    SDL_SemPost(g_port_stepper_go);
    for (;;) {
        state = port_BesideStep();
        if (state != STEPPER_FLUSH) {
            break;
        }
        port_GlStepRecords(FALSE);
        port_DrawRecord();
        port_GlStepRecords(TRUE);
        SDL_AtomicSet(&g_port_stepper_state, STEPPER_RUNNING);
        SDL_SemPost(g_port_stepper_resume);
    }
    g_port_step_done_at = port_ReadClock();
    g_port_step_done = TRUE;
    if (g_port_profile_file != NULL) {
        /* the step's thread done, and then its record drawn: parts of a
         * press's way to the picture (tools/port_bench.py --paced) */
        fprintf(g_port_profile_file, "e %llu %llu %llu\n",
                (unsigned long long)port_Micros(),
                (unsigned long long)g_port_stepper_woke,
                (unsigned long long)g_port_stepper_ended);
    }
    port_GlStepRecords(FALSE);
    port_DrawRecord();
    if (g_port_profile_file != NULL) {
        fprintf(
            g_port_profile_file, "r %llu\n", (unsigned long long)port_Micros());
    }
    g_port_beside = FALSE;
    g_port_steps_beside++;
    port_SayForeignCalls();
    if (state == STEPPER_MOVED) {
        /* the rest of the step here, as any step's */
        g_port_steps_moved++;
        port_HostEnterGame();
    }
}

static void port_StartStepper(void) {
    const char *setting = SDL_getenv("RB_STEP_THREAD");

    g_port_stepper_on = setting == NULL || strcmp(setting, "off") != 0;
    setting = SDL_getenv("RB_STEP_STREAM");
    g_port_stream_on = setting == NULL || strcmp(setting, "off") != 0;
    /* a press made as the PC's own (--inject) is read at a blank, on the
     * window's thread */
    if (!g_port_stepper_on ||
        g_port_window_options.inject != PORT_INJECT_NONE) {
        return;
    }
    g_port_stepper_go = SDL_CreateSemaphore(0);
    g_port_stepper_back = SDL_CreateSemaphore(0);
    g_port_stepper_resume = SDL_CreateSemaphore(0);
    if (g_port_stepper_go == NULL || g_port_stepper_back == NULL ||
        g_port_stepper_resume == NULL) {
        return;
    }
    g_port_stepper =
        SDL_CreateThread(port_StepperThread, "rebushido-step", NULL);
    if (g_port_stepper == NULL) {
        return;
    }
    /* the thread's id and processors set, by itself */
    SDL_SemWait(g_port_stepper_back);
    port_CpuPreferSay(g_port_stepper_cpus, "step");
    g_port_profile_thread = port_ProfileThreadOf;
    g_port_step_runner = port_RunStep;
    g_port_step_to_host = port_StepToHost;
    g_port_foreign_call_hook = port_OnForeignCall;
    g_port_quit_hook = port_CloseFromStep;
    port_GlRecordWake(port_WakeWindow);
}

static void port_StopStepper(void) {
    if (g_port_stepper == NULL || port_OnStepper() || g_port_beside) {
        return;
    }
    g_port_step_runner = NULL;
    g_port_step_to_host = NULL;
    g_port_foreign_call_hook = NULL;
    SDL_AtomicSet(&g_port_stepper_stop, 1);
    SDL_SemPost(g_port_stepper_go);
    SDL_WaitThread(g_port_stepper, NULL);
    g_port_stepper = NULL;
    g_port_profile_thread = NULL;
}

/* ---- the input read late (docs/PORT.md, "Input lag") ----
 *
 * A held step's first picture goes up at the first refresh after the
 * step, its record, the picture and the card's drawing of it are done;
 * whatever is left between then and that refresh was time the press had
 * already waited. So where the display is waited for, the boundary waits
 * that much first and the keys and controllers are read again just
 * before the step begins (the "frame delay" of other programs): the
 * picture goes up at the same refresh, and a press made meanwhile is in
 * it. What the read to the card's drawing takes is learnt from the last
 * LATE_TOOK steps: the refresh aimed at is the one their median makes,
 * and the wait leaves the step their 90th centile (LATE_RANK) and a spare
 * is kept beyond it that a picture late for its refresh makes larger
 * and each in time a little smaller, so that about one in a hundred of
 * the frames' first pictures is late. A refresh that falls in the wait
 * before the picture's is given a picture between the two frames
 * written down already, as one inside the step would be. The game takes
 * its input at the boundary as ever, and what it computes is the same:
 * only the PC's own presses are read later. Only for a step on the
 * step's thread, with the display waited for (`vsync`), its refreshes
 * known and `low_latency`, whose wait for the card says when a picture
 * was drawn; never with --fast, --inject or for a picture a test or the
 * player asked for. `late_input = off` reads at the boundary only. */
#define LATE_TOOK 32
#define LATE_RANK 3
/* The spare, in sixteenths of a sample: at first a millisecond, never
 * under half of one or over four; a late picture adds a millisecond, one
 * in time takes off a hundredth of that. */
#define LATE_SPARE_ONE 16
#define LATE_SPARE_FIRST (PORT_SPU_RATE / 1000 * LATE_SPARE_ONE)
#define LATE_SPARE_LEAST (PORT_SPU_RATE / 2000 * LATE_SPARE_ONE)
#define LATE_SPARE_MOST (PORT_SPU_RATE / 250 * LATE_SPARE_ONE)
#define LATE_SPARE_MISS (PORT_SPU_RATE / 1000 * LATE_SPARE_ONE)
#define LATE_SPARE_HIT (LATE_SPARE_MISS / 100)

static u32 g_port_late_took[LATE_TOOK];
static u32 g_port_late_count;
static u32 g_port_late_spare = LATE_SPARE_FIRST;
/* the next picture waited for is the held step's first: when the input
 * was read for it, and the clock's waits by then */
static s32 g_port_late_first;
static u32 g_port_late_read_at;
static u32 g_port_late_waited_at;
/* the refresh a read late was timed for, and whether one was */
static u32 g_port_late_target;
static s32 g_port_late_timed;
/* for the log: reads made late, the time waited, the first pictures late
 * for their refresh */
static u32 g_port_late_reads;
static u64 g_port_late_waited;
static u32 g_port_late_missed;

static s32 port_MayReadLate(void) {
    return g_port_settings.late_input && port_RunsBeside() && g_port_vsync &&
           g_port_settings.low_latency && g_port_refresh.period != 0 &&
           g_port_refresh.is_seen && !g_port_window_options.is_fast &&
           !g_port_wants_shot &&
           g_port_window_options.inject == PORT_INJECT_NONE;
}

/* What the read to a first picture drawn by the card takes: `*usual`
 * the median of the last steps', the return its 90th centile and the
 * spare; 0 until enough steps have said. */
static u32 port_LateBudget(u32 *usual) {
    u32 took[LATE_TOOK];
    u32 count = g_port_late_count < LATE_TOOK ? g_port_late_count : LATE_TOOK;
    u32 i;
    u32 j;

    if (count < LATE_TOOK / 2) {
        return 0;
    }
    memcpy(took, g_port_late_took, count * sizeof(took[0]));
    /* largest first, by as many passes of a selection as the median
     * needs */
    for (i = 0; i <= count / 2; i++) {
        for (j = i + 1; j < count; j++) {
            if (took[j] > took[i]) {
                u32 swap = took[i];

                took[i] = took[j];
                took[j] = swap;
            }
        }
    }
    *usual = took[count / 2];
    return took[LATE_RANK] + g_port_late_spare / LATE_SPARE_ONE;
}

/* A held step begins, its input read at the boundary (port_StepRefresh):
 * the wait, where there is time to spare before the refresh its first
 * picture will make, and the read again. That refresh is the one the
 * step makes as it usually does without a wait (its median): never a
 * later one, so that no picture of the frame comes later than it would
 * have; and the wait is what leaves the step its 90th centile and the
 * spare before it. */
static void port_LateRead(void) {
    s32 was;
    u32 now;
    u32 budget;
    u32 usual = 0;
    u32 earliest;
    u32 earliest_blank;
    u32 target;
    u32 start;
    s32 wait;

    g_port_late_first = FALSE;
    g_port_late_timed = FALSE;
    g_port_late_beside = FALSE;
    if (!port_MayReadLate()) {
        return;
    }
    now = port_ReadClock();
    g_port_late_first = TRUE;
    g_port_late_read_at = now;
    g_port_late_waited_at = g_port_waited_all;
    budget = port_LateBudget(&usual);
    if (budget == 0) {
        return;
    }
    /* With `frame_rate original` the first picture is shown no sooner
     * than its blank's own moment (port_ShowPictures), the blank after
     * the one shown last. The refreshes are known by the swaps' return, a
     * little after each: where one falls within a millisecond of that
     * moment, which refresh the picture makes is not known, and nothing
     * is waited for (a wait for the later one would push the picture past
     * the earlier, and every blank after it a refresh late). */
    if (g_port_pace.late > g_port_refresh.period / 2) {
        /* the game is behind its pace: it catches up, and nothing is
         * waited for */
        return;
    }
    /* (the moment the last blank was due, which a late one was shown
     * after) */
    earliest_blank = g_port_last_due - g_port_pace.late + BLANK_SAMPLES;
    earliest = now;
    if (port_PictureRate() == 0) {
        u32 near = port_NextRefresh(
            &g_port_refresh, earliest_blank - STEP_REFRESH_SPARE, 0);

        if ((s32)(near - earliest_blank) < STEP_REFRESH_SPARE) {
            return;
        }
        earliest = earliest_blank;
    }
    if ((s32)(now + usual - earliest) > 0) {
        earliest = now + usual;
    }
    target = port_NextRefresh(&g_port_refresh, earliest, 0);
    start = target - budget;
    /* and the step's first blank has its turn less than a refresh after
     * its own moment, the millisecond spare: a blank later than that
     * shows nothing (port_ShowPictures) */
    if ((s32)(start + budget -
              (earliest_blank + g_port_refresh.period - STEP_REFRESH_SPARE)) >
        0) {
        start = earliest_blank + g_port_refresh.period - STEP_REFRESH_SPARE -
                budget;
    }
    wait = (s32)(start - now);
    if (wait <= 0 || wait > BLANK_SAMPLES) {
        return;
    }
    was = port_ProfileSwitch(PORT_PROFILE_WINDOW);
    if (port_PictureRate() != 0 && port_MayDrawBeside()) {
        /* a refresh that comes in the wait: a picture between the two
         * frames written down already, handed over for it (one that comes
         * later, in the step, is the step's to give one, port_BesideStep) */
        u32 next = port_NextRefresh(&g_port_refresh, now, g_port_draw_cost);

        if ((s32)(start - next) > 0 && next != g_port_queued_for) {
            port_DrawBeside(next);
            g_port_late_beside = TRUE;
        }
    }
    wait = (s32)(start - port_ReadClock());
    if (wait > 0) {
        port_WaitFor((u32)wait);
    }
    /* the blank's pictures ended at the wait's end: what the next blank's
     * first picture takes after them is the step's, as before */
    g_port_left_at = port_ReadClock();
    port_ProfileSwitch(PORT_PROFILE_WINDOW);
    port_ReadEvents();
    port_SendPads();
    port_ReadDevicesNow();
    g_port_late_read_at = port_ReadClock();
    g_port_late_waited_at = g_port_waited_all;
    g_port_late_target = target;
    g_port_late_timed = TRUE;
    g_port_late_reads++;
    g_port_late_waited += g_port_late_read_at - now;
    if (g_port_profile_file != NULL) {
        /* the read made late, as the `k` line (tools/port_bench.py) */
        fprintf(
            g_port_profile_file, "l %llu\n", (unsigned long long)port_Micros());
    }
    port_ProfileSwitch(was);
}

/* The card has drawn a picture about to be handed over: where it is a
 * held step's first, what it took from the read, less the clock's waits
 * between (with `frame_rate original`, for the blank's own moment). */
static void port_NoteLateDrawn(void) {
    if (!g_port_late_first) {
        return;
    }
    g_port_late_first = FALSE;
    g_port_late_took[g_port_late_count++ % LATE_TOOK] =
        port_ReadClock() - g_port_late_read_at -
        (g_port_waited_all - g_port_late_waited_at);
}

/* ... and its swap has come back: where the read was timed for a
 * refresh, whether it made it, and the spare kept by that. */
static void port_NoteLateShown(void) {
    if (!g_port_late_timed) {
        return;
    }
    g_port_late_timed = FALSE;
    if ((s32)(port_ReadClock() - g_port_late_target) >
        (s32)(g_port_refresh.period / 2)) {
        g_port_late_missed++;
        g_port_late_spare += LATE_SPARE_MISS;
        if (g_port_late_spare > LATE_SPARE_MOST) {
            g_port_late_spare = LATE_SPARE_MOST;
        }
    } else if (g_port_late_spare > LATE_SPARE_LEAST + LATE_SPARE_HIT) {
        g_port_late_spare -= LATE_SPARE_HIT;
    }
}

/* ---- quick loading (docs/PORT.md, "Quick loading") ---- */

/* A hurried blank's picture is shown at most this often, in the clock's
 * samples: the loading picture's noise still moves and the window still
 * answers, but the display is not waited for at every blank. */
#define HURRY_PICTURE_SAMPLES (PORT_SPU_RATE / 30)

/* The log's account of the loads: each blank of one counted, and a line
 * as it ends. Where it was hurried through, the blanks after it are due
 * from now on (port_PaceRebase): none of the game's time it took is
 * waited for afterwards. */
static void port_FollowLoad(u32 blank, s32 kind, s32 is_hurried) {
    u32 now = port_ReadClock();
    u32 ms;

    if (kind != PORT_LOAD_NONE) {
        if (g_port_load_blanks == 0) {
            g_port_load_first = blank;
            g_port_load_began = now;
            g_port_hurry_shown = now;
        }
        g_port_load_blanks++;
        g_port_load_hurried += is_hurried ? 1 : 0;
        return;
    }
    g_port_load_stalled = FALSE;
    if (g_port_load_blanks == 0) {
        return;
    }
    ms = (u32)((u64)(now - g_port_load_began) * MILLISECONDS / PORT_SPU_RATE);
    port_Note("load: %u blanks from blank %u, %u of them hurried through; "
              "%u ms",
              g_port_load_blanks, g_port_load_first, g_port_load_hurried, ms);
    if (g_port_load_hurried != 0) {
        port_PaceRebase(&g_port_pace, now, port_GetSampleCount());
        g_port_last_due = now;
        g_port_left_at = now;
        g_port_loads_hurried++;
    }
    g_port_loads++;
    g_port_load_blanks_all += g_port_load_blanks;
    g_port_load_ms_all += ms;
    g_port_load_blanks = 0;
    g_port_load_hurried = 0;
}

/* Whether a load hurried through has read nothing of the disc for
 * STALL_SECONDS (g_port_load_stalled): from then to its end it is paced,
 * and the log says so once. */
static s32 port_LoadStalls(u32 blank) {
    u32 now = port_ReadClock();

    if (g_port_load_blanks == 0 || g_port_load_hurried == 0) {
        /* its first blank, or its first hurried one */
        g_port_load_read_at = now;
        return FALSE;
    }
    if (now - g_port_load_read_at < (u32)STALL_SECONDS * PORT_SPU_RATE) {
        return FALSE;
    }
    g_port_load_stalled = TRUE;
    port_Note("load: WARNING: %u blanks from blank %u hurried through and "
              "nothing read of the disc for %d seconds: the game waits for "
              "something that does not come; the load is paced from blank "
              "%u, no longer hurried (quick_loading)",
              g_port_load_blanks, g_port_load_first, STALL_SECONDS, blank);
    return TRUE;
}

/* A blank of a load, hurried through: the game's work for it goes on as
 * ever (the card is given the frame, the log follows), but nothing waits
 * for the PC's clock, and a picture is shown only now and then. */
static void port_HurryBlank(u32 blank) {
    /* where the frames were run ahead, the card's frame was ended as they
     * ended (port_AheadDone) */
    if (g_port_has_gl && !g_port_unseen) {
        port_ProfileSwitch(PORT_PROFILE_FINISH);
        port_PaintSyncAll();
        port_RemasterShow();
        port_GlFinishFrame();
        port_ProfileSwitch(PORT_PROFILE_WINDOW);
    }
    port_WatchGame(blank);
    (void)port_WatchSound(blank, TRUE);
    if ((s32)(port_ReadClock() - g_port_hurry_shown) >= HURRY_PICTURE_SAMPLES) {
        port_ProfileSwitch(PORT_PROFILE_PICTURE);
        port_DrawPicture(blank, TRUE);
        port_ProfileSwitch(PORT_PROFILE_WINDOW);
        port_DrawCounter();
        port_DrawNetLine();
        port_SwapPicture();
        g_port_hurry_shown = port_ReadClock();
    }
    g_port_blanks_shown++;
    port_NoteProfile(blank, FALSE);
    port_ProfileSwitch(PORT_PROFILE_GAME);
}

/* A vertical blank's end (port_platform.h): its pictures, when they are
 * due. */
/* The sound card's side as this blank finds it, and how far the queue is
 * from its length, for the pace. Where the sound ran out since the last
 * blank, the log says what the blank before took and how far behind the
 * game was, and the queue is made longer, unless that was time let go
 * (a stall longer than any queue) or a load hurried through. */
static s32 port_WatchSound(u32 blank, s32 is_hurried) {
    u32 underruns;
    u32 count;
    u32 target;

    if (g_port_audio == 0) {
        return 0;
    }
    SDL_LockAudioDevice(g_port_audio);
    underruns = g_port_ring.underruns;
    count = g_port_ring.count;
    target = g_port_ring.target;
    SDL_UnlockAudioDevice(g_port_audio);
    if (g_port_pace.lost != g_port_lost_counted) {
        g_port_lost_counted = g_port_pace.lost;
        g_port_lost_blank = blank;
    }
    if (underruns != g_port_underruns_seen) {
        s32 grow = !is_hurried && blank - g_port_lost_blank > LOST_NEAR &&
                   target < AUDIO_QUEUE_MOST;

        if (g_port_underruns_told < UNDERRUNS_TOLD) {
            char parts[COUNTER_TEXT_BYTES * 2];

            port_LargestParts(parts, sizeof(parts));
            port_Note("sound: ran out before blank %u: %u samples were queued "
                      "after the blank before, which took %s%s",
                      blank, g_port_queue_seen, parts,
                      g_port_underruns_told + 1 == UNDERRUNS_TOLD
                          ? " (no more of these lines: the count each minute)"
                          : "");
            g_port_underruns_told++;
        }
        if (grow) {
            target += AUDIO_QUEUE_STEP;
            SDL_LockAudioDevice(g_port_audio);
            g_port_ring.target = target;
            SDL_UnlockAudioDevice(g_port_audio);
            port_Note("sound: the queue is %u long from here (%u ms)", target,
                      target * MILLISECONDS / PORT_SPU_RATE);
        }
        g_port_underruns_seen = underruns;
    }
    g_port_queue_seen = count;
    return (s32)count - (s32)target;
}

static void port_ShowBlank(u32 blank) {
    u32 due;
    u32 frames = g_port_tween_frames;
    s32 load;
    s32 is_hurried;

    port_ProfileSwitch(PORT_PROFILE_WINDOW);
    port_OpenWindow();
    load = port_LoadKind();
    if (port_ReadScreen() != g_port_read_screen) {
        g_port_read_screen = port_ReadScreen();
        g_port_read_blanks = 0;
    }
    is_hurried = port_HurriesBlank();
    if (is_hurried && port_LoadStalls(blank)) {
        is_hurried = FALSE;
    }
    if (load == PORT_LOAD_READ && !is_hurried) {
        g_port_read_blanks++;
    }
    port_FollowLoad(blank, load, is_hurried);
    /* what the frames ahead paint into the software GPU's video memory is
     * shown only where the original picture is, or where a test takes
     * it */
    g_port_ahead_unpainted = g_port_has_gl && port_ShowsRemastered() &&
                             g_port_window_options.tap_frames == NULL;
    if (load != PORT_LOAD_NONE && g_port_has_gl) {
        /* the fighters' own models read while the game loads, not at the
         * fight's first frame */
        port_ProfileSwitch(PORT_PROFILE_CARD);
        port_GlFighterPreload();
        port_ProfileSwitch(PORT_PROFILE_WINDOW);
    }
    if (is_hurried) {
        port_HurryBlank(blank);
        return;
    }
    if (g_port_has_gl) {
        /* the frame the card drew is finished before anything is shown,
         * where the software GPU's picture is complete too
         * (port_remaster.h) */
        u32 started = SDL_GetTicks();

        const char *news;

        port_ProfileSwitch(PORT_PROFILE_FINISH);
        /* where the frames were run ahead, the card's frame was ended as
         * they ended, with the display they left (port_AheadDone) */
        if (!g_port_unseen) {
            /* the frame's primitives painted (port_paint.h): what the
             * frame's end reads of video memory is all of it */
            port_PaintSyncAll();
            port_RemasterShow();
            port_GlFinishFrame();
        }
        port_ProfileSwitch(PORT_PROFILE_WINDOW);
        port_GlNoteFrameTime((SDL_GetTicks() - started) * MILLISECONDS);
        /* which path the stage's ground takes (gl_stage.h) */
        news = port_GlStageNews();
        if (news != NULL) {
            port_Say("%s", news);
        }
        /* and whether the stage has a sky (gl_sky.h) */
        news = port_GlSkyNews();
        if (news != NULL) {
            port_Say("%s", news);
        }
    }
    if (g_port_tween_on && port_TweenGeneration() != g_port_tween_frames) {
        /* the game has drawn a frame: the frame that was on show is the
         * one before it, and a picture's phase is counted from the
         * moment of the blank its step began at -- the blank the pace
         * held to, so that a step that took longer or shorter moves no
         * picture (pace.c) */
        if (g_port_tween_frames != 0) {
            u32 period = g_port_last_due - g_port_frame_at;

            /* Where the time was let go or a load hurried through between
             * the two, the blanks' moments jumped: the frame lasts what
             * the frame before lasted, not the jump, so that the pictures
             * after a hitch go on at the game's pace rather than crawl
             * through one frame (docs/PORT.md, "After a hitch"). */
            if (period <= FRAME_PERIOD_MOST * BLANK_SAMPLES ||
                g_port_frame_period == 0) {
                g_port_frame_period =
                    period <= FRAME_PERIOD_MOST * BLANK_SAMPLES
                        ? period
                        : 2 * BLANK_SAMPLES;
            }
        }
        g_port_frame_at = g_port_last_due;
        g_port_tween_frames = port_TweenGeneration();
        g_port_wants_lag = TRUE;
    }
    port_ProfileSwitch(PORT_PROFILE_PICTURE);
    g_port_picture_began = port_Micros();
    port_DrawPicture(blank, TRUE);
    port_ProfileSwitch(PORT_PROFILE_WINDOW);
    g_port_profile_phase = -1;
    port_WatchGame(blank);
    port_DrawCounter();
    port_DrawNetLine();
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
    if (g_port_window_options.report_at > 0 &&
        blank == (u32)g_port_window_options.report_at) {
        /* for a test: what F9 does */
        g_port_wants_report = TRUE;
        g_port_wants_shot = TRUE;
    }
    if (g_port_window_options.stall_ms > 0 &&
        blank >= (u32)g_port_window_options.stall_at &&
        (blank - (u32)g_port_window_options.stall_at) %
                (u32)g_port_window_options.stall_every ==
            0) {
        /* for a test: a hitch, as a driver's or the system's would be */
        SDL_Delay((Uint32)g_port_window_options.stall_ms);
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
        /* a held blank's (step.c): the samples as they were at it, and
         * the queue without what the step mixed after it */
        u32 since;
        u32 samples = port_BlankSamples(blank, &since);
        s32 queue_error = port_WatchSound(blank, is_hurried);

        if (g_port_audio != 0) {
            queue_error -= (s32)since;
        }
        due += port_PaceBlank(&g_port_pace, blank, due, samples, queue_error);
    }
    port_ShowPictures(due);
    g_port_blanks_shown++;
    /* the settings menu, where it was asked for (sdl_menu.c) */
    port_MaybeRunMenu(blank);
    /* Only a display whose rate is not known can be what holds the game
     * back here: one that says it is slower than the game is not waited
     * for from the start (port_OpenWindow), and one as fast as the game
     * or faster never holds a blank up by more than a refresh. Time let
     * go there is the PC's (a load, a hitch, a picture too dear to draw),
     * and giving up the wait for it only tore every picture after it in
     * two: the owner's "large artifacting" (docs/PORT.md, "Where the time
     * goes"). */
    if (g_port_vsync && g_port_display_rate == 0 &&
        port_PaceIsHeldBack(&g_port_pace)) {
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
    port_NoteProfile(blank, frames != g_port_tween_frames);
    port_ProfileSwitch(PORT_PROFILE_GAME);
}

/* The program ends (port_platform.h): what the window did, SDL's leave,
 * and the log's last lines. */
static void port_StopPainters(void);
static void port_StopPairing(void);

static void port_StopStepper(void);

static void port_CloseWindow(s32 code) {
    port_StopStepper();
    if (g_port_steps_beside != 0) {
        const PortGlRecordCounts *records = port_GlRecordCounts();

        port_Note(
            "the step beside the window: %u steps on a thread of their "
            "own, %u of them moved to the window's thread, %u pictures "
            "drawn beside them; %u records of %u calls, %u of them drawn "
            "while the step ran, %u views and %u side channels kept, %u KB "
            "a record at the most, %u lost",
            g_port_steps_beside, g_port_steps_moved, g_port_pictures_beside,
            records->records, records->calls, records->streamed,
            records->views_kept, records->sides_kept,
            records->most_bytes / 1024U, records->lost);
    }
    port_SayForeignCalls();
    if (SDL_AtomicGet(&g_port_foreign_calls) != 0) {
        port_Note("the step beside the window: %d functions of the game "
                  "begun beside it, off its thread",
                  SDL_AtomicGet(&g_port_foreign_calls));
    }
    if (g_port_step_refreshes != 0) {
        port_Note("the step's refresh: %u steps showed a picture for the "
                  "refresh they would have missed, %u of them in time for "
                  "their own",
                  g_port_step_refreshes, g_port_step_refresh_early);
    }
    if (g_port_late_reads != 0) {
        /* in hundredths of a millisecond */
        u32 waited = (u32)(g_port_late_waited * 100000U / PORT_SPU_RATE /
                           g_port_late_reads);
        u32 spare = (u32)((u64)g_port_late_spare / LATE_SPARE_ONE * 100000U /
                          PORT_SPU_RATE);

        port_Note("late_input: %u steps read the keys again later, by "
                  "%u.%02u ms on average; %u of their first pictures late "
                  "for their refresh; %u.%02u ms kept to spare at the end",
                  g_port_late_reads, waited / 100U, waited % 100U,
                  g_port_late_missed, spare / 100U, spare % 100U);
    }
    port_StopPainters();
    port_StopPairing();
    if (g_port_audio_tap != NULL) {
        fclose(g_port_audio_tap);
        g_port_audio_tap = NULL;
    }
    port_WaitForPngs();
    port_SampleEnd();
    if (g_port_profile_file != NULL) {
        fclose(g_port_profile_file);
        g_port_profile_file = NULL;
        g_port_profile_on = 0;
    }
    if (g_port_has_gl && port_GlBenchTimed() + port_GlBenchLost() > 0) {
        port_Say("profile: the card's time taken for %u blanks, %u not kept",
                 port_GlBenchTimed(), port_GlBenchLost());
    }
    if (!g_port_is_open) {
        port_EndSession(code);
        return;
    }
    if (g_port_blanks_shown != 0) {
        u32 samples = port_ReadClock();

        port_Say("window: %u blanks shown in %u.%03u seconds; time let go %u "
                 "times, blanks moved %d samples in all; %u blanks a refresh "
                 "late showed nothing; the sound ran out %u times, its queue "
                 "%u long",
                 g_port_blanks_shown, samples / PORT_SPU_RATE,
                 samples % PORT_SPU_RATE * MILLISECONDS / PORT_SPU_RATE,
                 g_port_pace.lost, g_port_pace.nudged, g_port_pictures_unshown,
                 g_port_ring.underruns, g_port_ring.target);
    }
    if (g_port_loads != 0) {
        port_Say("loads: %u, of %u blanks in %u.%03u seconds; %u hurried "
                 "through (quick_loading %s)",
                 g_port_loads, g_port_load_blanks_all,
                 g_port_load_ms_all / MILLISECONDS,
                 g_port_load_ms_all % MILLISECONDS, g_port_loads_hurried,
                 port_QuickLoading() ? "on" : "off");
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
    if (g_port_rerun_taken + g_port_rerun_lost != 0) {
        port_Say("sound: netplay's rollbacks took back %u samples not yet "
                 "played and queued %u corrected ones in their place; %u had "
                 "been played already and stood",
                 g_port_rerun_taken, g_port_rerun_heard, g_port_rerun_lost);
    }
    if (g_port_has_gl) {
        port_Say(
            "graphics: the %s picture at the end, %u changes of picture "
            "(F2); the scene %d times the console's resolution, %u "
            "vertices and %u draws in the last frame",
            port_RemasterIsOn() ? "remastered" : "original", g_port_toggles,
            g_port_gl_scale, port_GlGetVertexCount(), port_GlGetDrawCount());
        if (port_GlFighterCounts()->loaded != 0) {
            const PortGlFighterCounts *fighters = port_GlFighterCounts();

            port_Say("graphics: the fighters' models: %u drawings tinted, %u "
                     "colour tables sent; the HD cloth kept out of the body "
                     "%u times and read again %u times",
                     fighters->tinted, fighters->palettes_sent,
                     fighters->cloth_holds, fighters->cloth_reused);
        }
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

/* ---- the software GPU's painters (port_paint.h) ---- */

/* Painters unless RB_PAINTERS says otherwise: a fight frame's primitives
 * in about a quarter of the time one takes. */
#define PAINTERS 4

static SDL_sem *g_port_paint_sem[PORT_PAINTERS_MAX];
static SDL_Thread *g_port_paint_thread[PORT_PAINTERS_MAX];
static s32 g_port_painters;

static void port_PainterSleep(s32 painter) {
    SDL_SemWait(g_port_paint_sem[painter]);
}

static void port_PainterWake(s32 painter) {
    SDL_SemPost(g_port_paint_sem[painter]);
}

static void port_PainterYield(void) { SDL_Delay(0); }

static int SDLCALL port_PainterThread(void *index) {
    /* the painters are on the game's way: ahead of the rest of the PC */
    SDL_SetThreadPriority(SDL_THREAD_PRIORITY_HIGH);
    /* ... and beside the step's thread, under the same cache
     * (cpu_prefer; docs/PORT.md, "The processors with the larger
     * cache") */
    port_CpuPreferHere();
    port_PaintRun((s32)(intptr_t)index);
    return 0;
}

/* The primitives painted on threads of their own: RB_PAINTERS of them
 * (0 or `off`: the game's thread paints them, to compare). */
static void port_StartPainters(void) {
    static const PortPaintHost host = {
        port_PainterSleep, port_PainterWake, port_PainterYield};
    const char *setting = SDL_getenv("RB_PAINTERS");
    s32 count = PAINTERS;
    s32 i;

    if (setting != NULL) {
        count = strcmp(setting, "off") == 0 ? 0 : atoi(setting);
    }
    /* RB_PAINT_LINES: the painters' groups of lines, a power of two
     * (port_paint.h) */
    if (SDL_getenv("RB_PAINT_LINES") != NULL) {
        s32 lines = atoi(SDL_getenv("RB_PAINT_LINES"));
        s32 shift = 0;

        while (shift < 8 && (2 << shift) <= lines) {
            shift++;
        }
        g_port_paint_band_shift = lines > 0 ? shift : -1;
    }
    if (count <= 0) {
        port_Note("the software GPU paints on the game's thread "
                  "(RB_PAINTERS=%s)",
                  setting);
        return;
    }
    for (i = 0; i < count && i < PORT_PAINTERS_MAX; i++) {
        g_port_paint_sem[i] = SDL_CreateSemaphore(0);
        if (g_port_paint_sem[i] == NULL) {
            return;
        }
    }
    count = port_PaintStart(&host, count);
    for (i = 0; i < count; i++) {
        char name[16];

        snprintf(name, sizeof(name), "painter %d", (int)i);
        g_port_paint_thread[i] =
            SDL_CreateThread(port_PainterThread, name, (void *)(intptr_t)i);
        if (g_port_paint_thread[i] == NULL) {
            /* the painters that are, and none from here on */
            port_Note("a painter has no thread: %s", SDL_GetError());
            g_port_painters = i;
            port_StopPainters();
            return;
        }
    }
    g_port_painters = count;
    port_Note("the software GPU paints on %d threads", (int)count);
}

static void port_StopPainters(void) {
    s32 i;

    if (!port_PaintIsThreaded()) {
        return;
    }
    port_PaintStop();
    for (i = 0; i < g_port_painters; i++) {
        SDL_WaitThread(g_port_paint_thread[i], NULL);
        g_port_paint_thread[i] = NULL;
    }
    g_port_painters = 0;
}

/* ---- the pairing of two frames, beside the game (gl_tween.h) ---- */

static SDL_sem *g_port_pair_go;
static SDL_sem *g_port_pair_done;
static SDL_Thread *g_port_pair_thread;
static SDL_atomic_t g_port_pair_stop;

static void port_PairGive(void) { SDL_SemPost(g_port_pair_go); }

static void port_PairWait(void) { SDL_SemWait(g_port_pair_done); }

static int SDLCALL port_PairThread(void *unused) {
    (void)unused;
    SDL_SetThreadPriority(SDL_THREAD_PRIORITY_HIGH);
    for (;;) {
        SDL_SemWait(g_port_pair_go);
        if (SDL_AtomicGet(&g_port_pair_stop)) {
            return 0;
        }
        port_TweenPairWork();
        SDL_SemPost(g_port_pair_done);
    }
}

/* Unless RB_PAIRING=game says the first picture works it out. */
static void port_StartPairing(void) {
    const char *setting = SDL_getenv("RB_PAIRING");

    if (setting != NULL && strcmp(setting, "game") == 0) {
        return;
    }
    g_port_pair_go = SDL_CreateSemaphore(0);
    g_port_pair_done = SDL_CreateSemaphore(0);
    if (g_port_pair_go == NULL || g_port_pair_done == NULL) {
        return;
    }
    g_port_pair_thread = SDL_CreateThread(port_PairThread, "pairing", NULL);
    if (g_port_pair_thread != NULL) {
        port_TweenSetWorker(port_PairGive, port_PairWait);
    }
}

static void port_StopPairing(void) {
    if (g_port_pair_thread == NULL) {
        return;
    }
    /* no pairing is under way once the pictures between frames stop */
    port_TweenStop();
    port_TweenSetWorker(NULL, NULL);
    SDL_AtomicSet(&g_port_pair_stop, 1);
    SDL_SemPost(g_port_pair_go);
    SDL_WaitThread(g_port_pair_thread, NULL);
    g_port_pair_thread = NULL;
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
    g_port_boundary_hook = port_ReadInputLate;
    g_port_ahead_blank_hook = port_AheadBlankTurn;
    g_port_ahead_done_hook = port_AheadDone;
    /* the step itself as the first frame ahead (step.c); RB_AHEAD=old
     * runs every frame ahead from a saved state, as before */
    if (SDL_getenv("RB_AHEAD") == NULL ||
        strcmp(SDL_getenv("RB_AHEAD"), "old") != 0) {
        g_port_held_blank_hook = port_HeldBlankTurn;
    }
    /* the step's refresh; RB_STEP_REFRESH=off: the refresh shows the
     * picture before it again, as before */
    g_port_step_refresh_on = SDL_getenv("RB_STEP_REFRESH") == NULL ||
                             strcmp(SDL_getenv("RB_STEP_REFRESH"), "off") != 0;
    g_port_held_begin_hook = port_StepRefresh;
    g_port_ahead_sync = SDL_getenv("RB_DONE_SYNC") == NULL;
    g_port_ahead_quiet = SDL_getenv("RB_AHEAD_SOUND") == NULL ||
                         strcmp(SDL_getenv("RB_AHEAD_SOUND"), "mix") != 0;
    port_SetRunAhead(g_port_settings.run_ahead);
    port_StartNetWindow();
    /* sessions begin and end in this program: the state of the game's
     * start is kept as it begins (port_online.h) */
    port_OnlineWanted();
    port_StartOnlinePages();
    port_CpuPreferStart(g_port_settings.cpu_prefer);
    port_StartPainters();
    port_StartPairing();
    port_StartStepper();
}
