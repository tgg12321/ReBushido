#ifndef PORT_WINDOW_H
#define PORT_WINDOW_H

/* The native port's window (docs/PORT.md, "The window"): the part of it
 * that asks nothing of SDL or of a C library, which is the arithmetic.
 * The four files it declares (scale.c, ring.c, pace.c, keymap.c) are
 * compiled as the port's own files are, and the host tests run them on
 * made-up numbers (tests/port/window.c). The files that talk to SDL
 * (sdl_main.c, sdl_window.c) and to the C library (host_posix.c) are only
 * in the build with a window, `make port-sdl`.
 *
 * Nothing here holds a pointer in a whole number or cares how wide a
 * pointer is. */

#include "common.h"

/* ---- where the picture goes in the window (scale.c) ---- */

/* The console's picture fills a screen of four by three whatever its
 * mode, 320 or 640 pixels across. */
#define PORT_PICTURE_ASPECT_W 4
#define PORT_PICTURE_ASPECT_H 3
/* The picture a whole number of times, counted in these. */
#define PORT_PICTURE_UNIT_W 320
#define PORT_PICTURE_UNIT_H 240
/* The most pixels a displayed rectangle has, each way. */
#define PORT_PICTURE_WIDTH_MAX 640
#define PORT_PICTURE_HEIGHT_MAX 512

enum {
    PORT_SCALE_FIT = 0,     /* the largest four by three that fits */
    PORT_SCALE_INTEGER = 1, /* the largest whole multiple of 320 by 240 */
    PORT_SCALE_STRETCH = 2, /* all of the window, whatever its shape */
};

typedef struct {
    s32 x;
    s32 y;
    s32 w;
    s32 h;
} PortRect;

/* The rectangle of a window `window_w` by `window_h` that the picture is
 * drawn into, in the middle of it. A window with no size gives a
 * rectangle with none. */
extern void port_FitPicture(
    s32 window_w, s32 window_h, s32 mode, PortRect *out);
/* A pixel of video memory as 0xFFRRGGBB: each channel's five bits
 * stretched to eight, as the port's PNGs have them (picture.c). */
extern u32 port_ConvertPixel(u16 pixel);
/* The displayed rectangle of video memory (`vram`: 1024 by 512 pixels) as
 * such pixels, a row after a row, into `out`: `w` by `h` of them from
 * (`x`, `y`), going round at video memory's edges; `is_rgb24` for the
 * mode of three bytes a pixel. */
extern void port_ConvertDisplay(
    const u16 *vram, s32 x, s32 y, s32 w, s32 h, s32 is_rgb24, u32 *out);

/* ---- the sound on its way to the sound card (ring.c) ---- */

/* How long a fade is, in samples: the way down when the sound runs out
 * and the way up when it comes back. */
#define PORT_RING_FADE 128

/* Samples waiting to be played, a left and a right each, in memory the
 * caller gives. The game's side writes a blank's worth at a time and the
 * sound card's side reads what it is asked for; the caller sees to it
 * that the two do not run at once. */
typedef struct {
    s16 *samples;   /* `capacity` pairs */
    u32 capacity;   /* in pairs */
    u32 start;      /* the next pair to read */
    u32 count;      /* pairs waiting */
    u32 target;     /* play begins, and begins again, with this many */
    s32 is_waiting; /* nothing is played until `target` are waiting */
    s32 fade;       /* 0 silent to PORT_RING_FADE full */
    s16 last[2];    /* the last pair played, for the way down */
    u32 underruns;  /* times the sound ran out */
    u32 dropped;    /* pairs thrown away because the ring was full */
} PortRing;

extern void port_InitRing(
    PortRing *ring, s16 *samples, u32 capacity, u32 target);
/* `frames` more pairs. If there is no room the oldest waiting are thrown
 * away for them: the sound stays this close behind the game. */
extern void port_WriteRing(PortRing *ring, const s16 *samples, u32 frames);
/* The next `frames` pairs for the sound card, always all of them: where
 * the sound has run out, the last pair fading to silence and then
 * silence, until `target` pairs are waiting again, which then fade in. */
extern void port_ReadRing(PortRing *ring, s16 *out, u32 frames);

/* ---- the PC's clock and the game's (pace.c) ---- */

/* Times are counted in the sound's samples, 44,100 a second, in 32 bits
 * that go round: the game's time is the samples it has mixed
 * (port_GetSampleCount), which is its vertical blanks at the console's
 * rate (sound.c), and the PC's clock is read in the same unit. */

/* The game is this far behind the PC's clock, or this far ahead: the
 * PC stopped it for a while (a window dragged, a debugger), and the
 * time lost is let go. A tenth of a second. */
#define PORT_PACE_LOST 4410
/* The most a blank is moved to keep the sound's queue at its length:
 * eight samples in a blank's 737, a part in a hundred, which is ten
 * times what a sound card's clock is ever off by. */
#define PORT_PACE_NUDGE_MAX 8
/* The queue may be this far from its length before anything is moved. */
#define PORT_PACE_SLACK 128
/* A step for each this many samples the queue is off by. */
#define PORT_PACE_NUDGE_SHIFT 6
/* The display holds the game back: this many times time was let go
 * within this many blanks, and waiting for the display is given up. */
#define PORT_PACE_LOST_TIMES 3
#define PORT_PACE_LOST_WINDOW 1800

typedef struct {
    s32 is_started;
    u32 base;         /* the PC's clock when the game's time was 0 */
    u32 lost;         /* times time was let go */
    u32 lost_recent;  /* ... since `window_start` */
    u32 window_start; /* a blank's number */
    s32 nudged;       /* samples the blanks have been moved by, in all */
    s32 queue_average; /* the queue's error, smoothed, in 256ths */
} PortPace;

/* A blank is about to be shown. `now` is the PC's clock, `due` the
 * game's time at this blank, `queue_error` how many more samples wait
 * for the sound card than should (negative: fewer; 0 with no sound
 * card). Returns how long to wait before showing it, in samples' time;
 * 0 if it is late already. */
extern u32 port_PaceBlank(
    PortPace *pace, u32 blank, u32 now, u32 due, s32 queue_error);
/* Whether the display has held the game back so often lately that
 * waiting for it should stop (PORT_PACE_LOST_TIMES). */
extern s32 port_PaceIsHeldBack(const PortPace *pace);

/* ---- keys and controllers to the pads (keymap.c) ---- */

/* What a pad's button is pressed by. */
enum {
    PORT_SOURCE_KEY = 1, /* a key: both pads' are on the one keyboard */
    PORT_SOURCE_BUTTON = 2, /* a button of the pad's own controller */
    PORT_SOURCE_AXIS = 3,   /* a stick or a trigger pushed one way */
};

#define PORT_BINDINGS_MAX 160
#define PORT_PADS 2
/* An axis's furthest. */
#define PORT_AXIS_MAX 32767
/* How far a stick is pushed before it counts, in parts of a hundred. */
#define PORT_DEADZONE_DEFAULT 40

typedef struct {
    u8 source;     /* PORT_SOURCE_... */
    u8 pad;        /* 0 or 1 */
    s8 direction;  /* an axis's: 1 or -1 */
    u8 is_default; /* from the layout built in, not from a file */
    u8 is_down;
    s32 code;   /* the key's, button's or axis's number */
    u32 button; /* one of Sony's PAD... masks */
} PortBinding;

typedef struct {
    PortBinding bindings[PORT_BINDINGS_MAX];
    s32 count;
    s32 scale;         /* PORT_SCALE_... */
    s32 is_fullscreen; /* at the start */
    s32 is_linear;     /* the picture smoothed as it is scaled */
    s32 vsync;         /* wait for the display */
    s32 deadzone;      /* parts of a hundred */
} PortSettings;

/* The number of the key, button or axis of this name, as the window's
 * library names them; a negative number if it knows none. */
typedef s32 (*PortNameLookup)(s32 source, const char *name);
/* A line of a settings file that says nothing the port knows. */
typedef void (*PortComplaint)(s32 line, const char *why, const char *text);

/* The layout built in, as the text of a settings file. */
extern const char g_port_default_settings[];
/* The settings before any file: the layout built in. */
extern void port_SetDefaultSettings(PortSettings *settings,
                                    PortNameLookup lookup);
/* A settings file's text, which is cut up in place (docs/PORT.md,
 * "The settings file"). A line that binds a pad's button takes the
 * built-in keys, or the built-in controller buttons, of that button
 * away first. Returns the lines it could not use, each told to
 * `complain` if that is not NULL. */
extern s32 port_ReadSettings(PortSettings *settings, char *text,
                             PortNameLookup lookup, PortComplaint complain);
/* Sony's mask for a button's name in a settings file (`cross`, `l1`,
 * `up`, `start`...), letters of either case; 0 if it is none. */
extern u32 port_FindPadButton(const char *name);
/* The name of the button with this mask; NULL if there is none. */
extern const char *port_NamePadButton(u32 button);
/* A key went down or came up (`value` 1 or 0), or the button `code` of
 * pad `pad`'s controller did, or its axis `code` moved to `value`
 * (-32768 to 32767). `pad` is not looked at for a key. */
extern void port_ApplyInput(
    PortSettings *settings, s32 source, s32 pad, s32 code, s32 value);
/* Everything a pad's controller held is let go: it was unplugged. */
extern void port_ReleaseController(PortSettings *settings, s32 pad);
/* The buttons held on a pad now, as Sony's masks. */
extern u32 port_GetHeldButtons(const PortSettings *settings, s32 pad);
/* The first key (`keys` 1) or the first button or axis of a controller
 * (0) that presses this button of a pad; NULL if nothing does. */
extern const PortBinding *port_FindBinding(
    const PortSettings *settings, s32 keys, s32 pad, u32 button);

#endif /* PORT_WINDOW_H */
