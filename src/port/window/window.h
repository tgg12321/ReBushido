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
/* The same for a picture of `picture_w` by `picture_h` square pixels,
 * whose shape need not be four by three: the remastered picture's, which
 * is as wide as the view asks for and as large as the window already, so
 * it has no whole-multiple choice (PORT_SCALE_INTEGER is `fit` for it). */
extern void port_FitShape(s32 window_w, s32 window_h, s32 mode, s32 picture_w,
                          s32 picture_h, PortRect *out);
/* How many times the console's lines the remastered picture is drawn at,
 * when it is drawn `scale` times across: enough that a pixel of it is
 * square. The console's picture fills four by three in every mode, so a
 * 640-wide one has lines twice as tall as a pixel is wide (twice the
 * scale) and a 320-wide one has square pixels already (the scale
 * itself). */
extern s32 port_TallFor(s32 scale, s32 picture_w, s32 picture_h);
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
/* The newest `frames` pairs written and not yet played taken back, as if
 * they had never been written, to be written again otherwise (netplay's
 * rollback: sdl_net.c). Returns how many were: never more than wait. */
extern u32 port_TakeBackRing(PortRing *ring, u32 frames);

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
/* A hold (port_PaceHold) moves each blank on at most this much: a
 * quarter of a blank's time. */
#define PORT_PACE_HOLD_STEP 184

typedef struct {
    s32 is_started;
    u32 base;          /* the PC's clock when the game's time was 0 */
    u32 lost;          /* times time was let go */
    u32 lost_recent;   /* ... since `window_start` */
    u32 window_start;  /* a blank's number */
    s32 nudged;        /* samples the blanks have been moved by, in all */
    s32 queue_average; /* the queue's error, smoothed, in 256ths */
    u32 held;          /* samples of a hold still to be taken */
    u32 late;          /* how late the last blank was, in samples */
    s32 lost_by;       /* how late the blank was when time was last let
                        * go, in samples (negative: early) */
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
/* The blanks from here on are due `samples` later in all, taken
 * PORT_PACE_HOLD_STEP a blank at most: netplay's time sync, where this
 * program is ahead of the other (src/port/netplay.c). */
extern void port_PaceHold(PortPace *pace, u32 samples);
/* The game's time `due` is now: the blanks a quick load hurried through
 * are not waited for afterwards, nor counted as time the PC lost; a
 * hold still to be taken is kept. */
extern void port_PaceRebase(PortPace *pace, u32 now, u32 due);

/* ---- when a picture is shown, and of what moment (pace.c) ---- */

/* The display's refreshes, as the swaps that waited for them say: a
 * swap with vsync on comes back at a refresh, so the refreshes after it
 * are that moment and whole periods on. */
typedef struct {
    u32 period;  /* samples' time between two refreshes; 0: not known */
    u32 last;    /* when the last swap came back */
    s32 is_seen; /* whether a swap has come back yet */
} PortRefresh;

/* A swap came back at `now`. */
extern void port_NoteSwap(PortRefresh *refresh, u32 now);
/* When a picture begun `now` and drawn in `cost` would be shown: the
 * first refresh that comes after it is drawn; `now + cost` where the
 * refreshes are not known. */
extern u32 port_NextRefresh(const PortRefresh *refresh, u32 now, u32 cost);
/* How far a picture shown at `shown_at` is between two frames, from 0 to
 * `one`: the frame on show went up at `frame_at`, the next goes up
 * `period` later, and every picture is shown `lag` after the moment it
 * is a picture of. Out of that range it is the nearer end. */
extern s32 port_PacePhase(
    u32 shown_at, u32 frame_at, u32 lag, u32 period, s32 one);
/* `average` moved an eighth of the way to `sample`, both samples' time:
 * how the window keeps what a picture takes to draw and how late it is
 * shown, steady from one blank to the next. */
extern u32 port_PaceAverage(u32 average, u32 sample);

/* ---- keys and controllers to the pads (keymap.c) ---- */

/* What a pad's button is pressed by. */
enum {
    PORT_SOURCE_KEY = 1,    /* a key: both pads' are on the one keyboard */
    PORT_SOURCE_BUTTON = 2, /* a button of the pad's own controller */
    PORT_SOURCE_AXIS = 3,   /* a stick or a trigger pushed one way */
};

#define PORT_BINDINGS_MAX 256
#define PORT_PADS 2
/* The pads the window binds keys and controllers to: the game's two, and
 * the third and fourth that two on two's partners are played from
 * (port_duo.h). */
#define PORT_WINDOW_PADS 4
/* An axis's furthest. */
#define PORT_AXIS_MAX 32767
/* How far a stick is pushed before it counts, in parts of a hundred. */
#define PORT_DEADZONE_DEFAULT 40

typedef struct {
    u8 source;     /* PORT_SOURCE_... */
    u8 pad;        /* 0 to 3 (PORT_WINDOW_PADS) */
    s8 direction;  /* an axis's: 1 or -1 */
    u8 is_default; /* from the layout built in, not from a file */
    u8 is_down;
    s32 code;   /* the key's, button's or axis's number */
    u32 button; /* one of Sony's PAD... masks */
} PortBinding;

/* How many times the console's resolution the remastered picture is drawn
 * at; 0 for as many as the window's own size asks for
 * (port_RemasterScaleFor). */
#define PORT_RESOLUTION_WINDOW 0

/* How many pictures a second the window draws (docs/PORT.md,
 * "Remastered", "More pictures a second"). The game steps at the
 * console's own rate whatever this says, and computes exactly what it
 * always did: the pictures in between are drawn from the frames it has
 * already worked out. */
#define PORT_FRAME_RATE_ORIGINAL 0   /* one picture a blank, as the console */
#define PORT_FRAME_RATE_DISPLAY (-1) /* as many as the display shows */
/* The most pictures a blank may be drawn as, whatever the rate asks
 * for: a guard, so that a display that does not wait cannot hold the
 * game up by drawing for ever. */
#define PORT_PICTURES_A_BLANK_MAX 8
/* A rate a setting may ask for. */
#define PORT_FRAME_RATE_MAX 1000
/* Frames run ahead at most (step.c's PORT_AHEAD_MAX), and with nothing
 * said: the game puts a frame on the display two of its frames after it
 * reads the pads, and two frames ahead show no frame that was guessed;
 * but two cost a menu's blank more than half its time on the owner's PC
 * with every picture setting on, so one is the default (docs/PORT.md,
 * "The HD cloth and run-ahead"). */
#define PORT_RUN_AHEAD_MAX 2
#define PORT_RUN_AHEAD_DEFAULT 1

/* Playing online: `A.B.C.D:PORT` with room to spare, the port a host
 * waits at with nothing said, and the frames of delay. */
#define PORT_NET_ADDRESS_BYTES 48
/* The lobby server built in: the owner's (tools/relay/README.md). */
#define PORT_NET_LOBBY_DEFAULT "135.181.108.74:7845"
#define PORT_NET_ROOM_BYTES 25
#define PORT_NET_PLAYER_BYTES 17

/* net_role's places */
enum {
    PORT_NET_ROLE_HOST = 0,
    PORT_NET_ROLE_JOIN = 1,
    PORT_NET_ROLE_ROOM = 2,
    PORT_NET_ROLE_ROOMS = 3,
};

#define PORT_NET_PORT_DEFAULT 7845
#define PORT_NET_PORT_MAX 65535
#define PORT_NET_DELAY_DEFAULT 2
#define PORT_NET_DELAY_MAX 8

/* The most a stick's dead zone may be, in parts of a hundred. */
#define PORT_DEADZONE_MAX 99
/* the most replays kept, and megabytes they take (settings_list.h) */
#define PORT_REPLAYS_KEEP_MAX 100000
#define PORT_REPLAYS_MEGABYTES_MAX 1000000

/* ---- the settings (keymap.c, settings_list.h) ---- */

/* A letter a name the lobby shows may have (net_room, net_player). */
extern s32 port_IsNameLetter(char c);

/* Every setting of rebushido.cfg is an entry of settings_list.h, which
 * gives its member here, its line of the built-in text, how it is read
 * and how the settings menu shows it; a new one is one entry there. */

/* How a change of a setting takes effect. */
enum {
    PORT_TAKES_NOW = 0,     /* at once, while the game is paused */
    PORT_TAKES_RESTART = 1, /* the next time the program starts */
};

/* The most values a setting is read from by name, or steps through in
 * the menu. */
#define PORT_CHOICES_MAX 8

/* The settings menu's groups of settings, in its order. */
enum {
    PORT_MENU_NONE = 0, /* not in the menu */
    PORT_MENU_GAME = 1,
    PORT_MENU_WINDOW = 2,
    PORT_MENU_REMASTER = 3,
    PORT_MENU_CONTROLS = 4,
    PORT_MENU_MODS = 5,
    PORT_MENU_TRAINING = 6,
    PORT_MENU_TOURNAMENT = 7,
};

#define PORT_CHOICE_GROUPS 7

/* A number as the text of it, for a setting built in as a constant. */
#define PORT_QUOTE(number) PORT_QUOTE_TEXT(number)
#define PORT_QUOTE_TEXT(number) #number

typedef struct {
    PortBinding bindings[PORT_BINDINGS_MAX];
    s32 count;
    s32 layout;       /* which of g_port_layouts the built-in ones are */
    s32 own_controls; /* the pN lines read from a file */
    /* the picture's shape (`aspect`): 4 by 3, 16 by 9 */
    s32 aspect_w;
    s32 aspect_h;
    /* the host to join (`net_address`), as it was written */
    char net_address[PORT_NET_ADDRESS_BYTES];
    /* the lobby (`net_lobby`), the room's name and the player's
     * (`net_room`, `net_player`), and the address a host by address
     * punches to (`net_punch_to`): each as written, "" for none */
    char net_lobby[PORT_NET_ADDRESS_BYTES];
    char net_room[PORT_NET_ROOM_BYTES];
    char net_player[PORT_NET_PLAYER_BYTES];
    char net_punch_to[PORT_NET_ADDRESS_BYTES];
    /* the rest, each the member of an entry of settings_list.h */
#define PORT_SETTING(member, ...) s32 member;
#define PORT_SETTING_CUSTOM(...)
#define PORT_SETTINGS_HEADING(text)
#include "settings_list.h"
#undef PORT_SETTING
#undef PORT_SETTING_CUSTOM
#undef PORT_SETTINGS_HEADING
} PortSettings;

/* The number of the key, button or axis of this name, as the window's
 * library names them; a negative number if it knows none, and
 * PORT_NAME_RESERVED for a key that is the window's own and no pad's
 * (Escape: pause and back). */
typedef s32 (*PortNameLookup)(s32 source, const char *name);
#define PORT_NAME_RESERVED (-2)
/* A line of a settings file that says nothing the port knows. */
typedef void (*PortComplaint)(s32 line, const char *why, const char *text);

/* How a setting's value is read. */
enum {
    PORT_READ_SWITCH = 0, /* off or on: 0 or 1 */
    PORT_READ_WORDS = 1,  /* one of `words`: its place in the list */
    PORT_READ_NUMBER = 2, /* `least` to `most`, or one of `words`: the
                           * number of `values` in its place */
    PORT_READ_BY = 3,     /* by `reader` */
};

/* A setting's own reader: TRUE, or FALSE with `why` the reason, which
 * left NULL is the setting's `is`. */
typedef s32 (*PortSettingReader)(PortSettings *settings, const char *value,
                                 PortNameLookup lookup, const char **why);

/* An entry of settings_list.h, which says what each field is for. One
 * with no name is a heading of the built-in text. */
typedef struct {
    s32 at; /* where its member is in PortSettings; -1 for none */
    const char *name;
    const char *value;
    const char *note;
    s32 read; /* PORT_READ_... */
    const char *words[PORT_CHOICES_MAX];
    s32 values[PORT_CHOICES_MAX];
    s32 least;
    s32 most;
    PortSettingReader reader;
    const char *is;
    s32 menu; /* PORT_MENU_... */
    s32 rank;
    s32 takes; /* PORT_TAKES_... */
    const char *label;
    const char *help;
    const char *choices[PORT_CHOICES_MAX];
    const char *shown[PORT_CHOICES_MAX];
} PortSetting;

extern const PortSetting g_port_setting_list[];
extern const s32 g_port_setting_count;

/* The most bytes the built-in text may take, with its 0. */
#define PORT_DEFAULT_SETTINGS_BYTES 16384

/* The settings built in, as the text of a settings file (--help prints
 * it), made from settings_list.h: each setting's line and its comment,
 * the comment beginning at a column of its own. Its line
 * `controls = original` brings the bindings of that layout. */
extern const char *port_GetDefaultSettings(void);

/* The layouts of the keys and the controllers' buttons that a settings
 * file starts from (`controls = NAME`), each as a settings file with
 * nothing but pN lines in it. The first is the one built in. */
typedef struct {
    const char *name;
    const char *text;
} PortLayout;

#define PORT_LAYOUTS 1
extern const PortLayout g_port_layouts[PORT_LAYOUTS];
/* The settings before any file: the layout built in. */
extern void port_SetDefaultSettings(
    PortSettings *settings, PortNameLookup lookup);
/* A settings file's text, which is cut up in place (docs/PORT.md,
 * "The window"). A line that binds a pad's button takes the built-in
 * keys, or the built-in controller buttons, of that button away first,
 * and takes its own key or button away from whatever that pressed as
 * built in. Returns the lines it could not use, each told to `complain`
 * if that is not NULL. */
extern s32 port_ReadSettings(PortSettings *settings, char *text,
                             PortNameLookup lookup, PortComplaint complain);
/* Sony's mask for a button's name in a settings file (`cross`, `l1`,
 * `up`, `start`...), letters of either case; 0 if it is none. */
extern u32 port_FindPadButton(const char *name);
/* The masks of several buttons' names joined by `+` (`l1+r1`), or of
 * one; 0 if any of them is none. */
extern u32 port_FindPadButtons(const char *names);
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

/* ---- the settings file written (settext.c) ---- */

/* The settings menu (menu.c) changes rebushido.cfg a line at a time, and
 * keeps everything else of it as the player wrote it: other lines, the
 * comments, blank lines, the order. `text` is the file's text with a 0
 * after it, in `room` bytes; each returns FALSE, the text as it was, if
 * the change does not fit.
 *
 * The setting `name` takes `value`: the last line that sets it (the one
 * that counts, since a later line overrides an earlier) has its value
 * changed in place, with the comment after it kept where it was; with
 * no such line, `name = value` is added at the end. */
extern s32 port_SetSettingText(
    char *text, u32 room, const char *name, const char *value);
/* A pad's button bound anew: every line of exactly this name
 * (`p1.cross`) that binds a key (`keys` 1) or a controller's button or
 * axis (0), and any `none` of it, is taken out, and `name = value` added
 * at the end (`key Space`, `pad a`, `none`). A NULL value only takes
 * them out, which gives that button back the layout's own. */
extern s32 port_SetBindingText(
    char *text, u32 room, const char *name, s32 keys, const char *value);
/* Every pN line of a pad (0 or 1), or of both (-1), taken out: its
 * buttons are the layout's own again. */
extern void port_ClearBindingsText(char *text, s32 pad);

/* ---- the settings menu's choices (choices.c) ---- */

/* A setting of rebushido.cfg as the settings menu shows it: its name in
 * the file, what the menu calls it, and the values it can step through,
 * each as the file writes it and as the menu shows it. Made from the
 * entries of settings_list.h that have a group of the menu, in the
 * order of their groups and ranks. */
typedef struct {
    const char *name;  /* in the file */
    const char *label; /* in the menu */
    const char *help;  /* a line about it */
    s32 takes;         /* PORT_TAKES_... */
    s32 count;
    s32 built_in; /* the value built in, of these; -1 for none */
    const char *words[PORT_CHOICES_MAX]; /* in the file */
    const char *shown[PORT_CHOICES_MAX]; /* in the menu */
} PortChoice;

/* The groups the menu shows them in, each from its first choice. */
typedef struct {
    const char *title;
    const char *help; /* a line about it, for the menu's first page */
    s32 first;
    s32 count;
} PortChoiceGroup;

/* The menu's settings, numbered from 0 in its order: one of them (NULL
 * outside), how many there are, and one of the PORT_CHOICE_GROUPS groups
 * (NULL outside), which take every setting once, in order. */
extern const PortChoice *port_FindChoice(s32 choice);
extern s32 port_CountChoices(void);
extern const PortChoiceGroup *port_FindChoiceGroup(s32 group);
/* Which of a choice's values the settings hold now: its number, or -1
 * for a value that is none of them (a file's own `shadows = 30`). */
extern s32 port_GetChoice(const PortSettings *settings, s32 choice);
/* The settings with that value, read as a file's line would be: TRUE, or
 * FALSE if the line is refused. */
extern s32 port_SetChoice(PortSettings *settings, s32 choice, s32 value);
/* The next value of a choice from `value` (`step` 1 or -1, going round);
 * a value that is none of them steps to the first or the last. */
extern s32 port_StepChoice(s32 choice, s32 value, s32 step);

/* ---- the game, for the log (gamestate.c) ---- */

/* What the log follows of the game: read from the game's own variables,
 * never written. */
typedef struct {
    s32 screen;   /* enum Screen */
    s32 in_fight; /* the screen is the fight itself */
    s32 frames;   /* frames shown since power on */
    s32 mode;     /* enum FightMode */
    s32 stage;
    s32 winner;        /* 0, 1, or -1 for nobody */
    s32 defeat_frames; /* frames since a fighter was cut down */
    s32 roster[2];
    s32 weapon[2];
    s32 is_cpu[2];
    /* where the settings menu may be opened with Select (menu.c): */
    s32 is_top_menu;     /* the main menu's top page */
    s32 is_options_page; /* ReBushido's options page at boot */
    s32 is_paused;       /* the fight's pause menu */
    s32 is_cutscene;     /* a story's cutscene, which Cross skips */
    /* ReBushido's top menu (port_top_menu.h): on, its page and cursor,
     * and the settings menu it asked for: a count that only goes
     * forward, and the page (PORT_TOP_ASK_...) */
    s32 is_top_menu_ours;
    s32 top_page;
    s32 top_cursor;
    s32 menu_asks;
    s32 menu_ask_page;
} PortGameState;

extern void port_ReadGameState(PortGameState *state);
/* TRUE while the select screen is on one of its stage pages
 * (port_stage_choice.h), where the window lays the kinds of stage over
 * the picture (stage_page.c). */
extern s32 port_IsOnStagePages(void);
/* What the window lays over the stage pages, for a picture whose 4:3
 * game area is `area_w` by `area_h` pixels of the window: its pixels
 * (0xAARRGGBB, `box->w` by `box->h`) and where they go in the area;
 * FALSE when nothing is to be shown. Drawn again only when what it
 * shows has changed. */
extern s32 port_StagePagePixels(
    s32 area_w, s32 area_h, const u32 **pixels, PortRect *box);

/* What of the match rules is over the picture (include/port_match.h):
 * nothing, the lives or the score over a fight of a match, the card
 * after one, or the crews so far while a Crew match's later places are
 * picked on the select screen (match_page.c). */
enum {
    PORT_MATCH_SHOWN_NONE = 0,
    PORT_MATCH_SHOWN_HUD = 1,
    PORT_MATCH_SHOWN_CARD = 2,
    PORT_MATCH_SHOWN_PICK = 3,
    PORT_MATCH_SHOWN_RULE = 4, /* the rule's page before the first select */
};

extern s32 port_MatchShown(void);
/* A Crew match's place of a side: the fighter's name as the window shows
 * it (match_page.c reads the crews; the game's tables are here). */
extern const char *port_MatchCrewName(s32 fighter);
/* The crews that have the character at the select grid's place (row,
 * col): a bit a side (port_MatchCrewHas). */
extern s32 port_MatchCrewMarks(s32 row, s32 col);
/* What the window lays over the picture for them, as
 * port_StagePagePixels does for the stage pages. */
extern s32 port_MatchPixels(
    s32 area_w, s32 area_h, const u32 **pixels, PortRect *box);
/* Two on two over its fight (include/port_duo.h; match_page.c): the band
 * of the teams, who stands; and a fighter's tag over his head, the one
 * port_DuoTagChoose chose (0 to 3), with the players who target him. */
extern s32 port_DuoBandPixels(
    s32 area_w, s32 area_h, const u32 **pixels, PortRect *box);
extern void port_DuoTagChoose(s32 who);
extern s32 port_DuoTagPixels(
    s32 area_w, s32 area_h, const u32 **pixels, PortRect *box);
/* A side's line under the weapon select's row, the side
 * port_WeaponPickChoose chose: the mod's weapon its cursor shows, where
 * the weapon has any (port_weapons.h; match_page.c). */
extern void port_WeaponPickChoose(s32 side);
/* Training's input display over its fight, a column a pad, its line of
 * what training does and the hit areas (include/port_training.h;
 * training_page.c): the one port_TrainingCanvasChoose chose (0 and 1 the
 * pads' columns, 2 the line, 3 the hit areas). */
#define PORT_TRAINING_CANVASES 4
#define PORT_TRAINING_CANVAS_STATUS 2
#define PORT_TRAINING_CANVAS_AREAS 3
extern void port_TrainingCanvasChoose(s32 which);
extern s32 port_TrainingPixels(
    s32 area_w, s32 area_h, const u32 **pixels, PortRect *box);
extern s32 port_WeaponPickPixels(
    s32 area_w, s32 area_h, const u32 **pixels, PortRect *box);
/* "Single fight", "Stock N", "First to N" or "Crew N" into `out`. */
extern void port_MatchRuleName(char *out, s32 room, s32 rule, s32 count);
/* ReBushido's top menu as the window lays it over the game's page
 * (top_page.c), read from the game (port_top_menu.h): whether the page is
 * up, which of its two pages and rows, each row's name and whether it can
 * be chosen, and the caption the window draws for the cursor's row (NULL
 * where the game draws its own). */
#define PORT_TOP_VIEW_ROWS 6 /* PORT_TOP_ROWS */

typedef struct {
    s32 is_up;
    s32 page;
    s32 cursor;
    const char *names[PORT_TOP_VIEW_ROWS];
    s32 is_on[PORT_TOP_VIEW_ROWS];
    const char *caption[2];
} PortTopView;

extern void port_ReadTopMenu(PortTopView *view);

/* The pause's and the after-fight menu's rows that ReBushido's menus name
 * anew (port_top_menu.h): whether they are on the picture, the row under
 * the cursor (the game's: 1 and 2 are these two) and each row's y in the
 * game's screen. */
typedef struct {
    s32 is_up;
    s32 cursor;
    s32 y[2];
} PortPanelRowsView;

extern void port_ReadPanelRows(PortPanelRowsView *view);
/* The training page's and the group battle page's line the same way
 * (PORT_TOP_LINE_...): whether each is on the picture, its place in the
 * game's screen (left, top, width) and whether it is lit. */
#define PORT_PAGE_LINES 2 /* PORT_TOP_LINES */
#define PORT_PAGE_LINE_TRAINING 0
#define PORT_PAGE_LINE_GROUP 1

typedef struct {
    s32 is_up[PORT_PAGE_LINES];
    s32 place[PORT_PAGE_LINES][3];
    s32 is_lit[PORT_PAGE_LINES];
} PortPageLinesView;

extern void port_ReadPageLines(PortPageLinesView *view);
/* What the window lays over them (top_page.c), as port_TopPagePixels. */
extern s32 port_PageLinesPixels(
    s32 area_w, s32 area_h, const u32 **pixels, PortRect *box);
/* What the window lays over them (top_page.c), as port_TopPagePixels. */
extern s32 port_PanelRowsPixels(
    s32 area_w, s32 area_h, const u32 **pixels, PortRect *box);
/* A page of the window's (the settings menu, the online pages) is up over
 * ReBushido's top menu, or is no longer: the game draws its backdrop and
 * panel alone under it (port_top_menu.h). The one other thing the window
 * writes of the game's, at a blank's turn. */
extern void port_SetTopMenuCovered(s32 on);
/* The same for ReBushido's top menu (top_page.c): the rows' names and the
 * captions the game has none for, laid over the game's own page while it
 * is up (port_top_menu.h). */
extern s32 port_TopPagePixels(
    s32 area_w, s32 area_h, const u32 **pixels, PortRect *box);
/* Whether the game is waiting for the disc now (g_port_load_wait), and
 * what it shows meanwhile: PORT_LOAD_NONE, PORT_LOAD_PICTURE where its
 * loading picture, a black screen or what was on it last is up, which
 * the window may hurry through (quick_loading), or PORT_LOAD_READ where
 * the loading screen is one the player reads (port_ReadScreen), which
 * is hurried only once it has been up a while. */
#define PORT_LOAD_NONE 0
#define PORT_LOAD_PICTURE 1
#define PORT_LOAD_READ 2
extern s32 port_LoadKind(void);
/* The loading screen, where it is one the player reads: story mode's
 * card of the next stage and its route, or the team battle's scores
 * between two matches (enum LoadingScreen); 0 for any other, and in a
 * fight. */
extern s32 port_ReadScreen(void);
/* Whether the game's screen is the fight (SCREEN_FIGHT_UPDATE): a
 * replay's film begins with it (sdl_export.c). */
extern s32 port_IsFightScreen(void);
/* The game's own Bug fixes setting (g_rb_settings.bugfixes), read, and
 * set by the settings menu while the game is stopped. */
extern s32 port_GetBugfixes(void);
extern void port_SetBugfixes(s32 on);
/* Names for the log; "?" for a number that has none. */
extern const char *port_NameScreen(s32 screen);
extern const char *port_NameMode(s32 mode);
extern const char *port_NameRoster(s32 roster);
/* ... a character's (enum CharacterId), by its roster's name */
extern const char *port_NameCharacter(s32 character);
extern const char *port_NameWeapon(s32 weapon);
extern const char *port_NameStage(s32 stage);
/* `ReBushido VERSION (COMMIT)`. */
extern const char *port_GetVersionText(void);

/* ---- the session's log (logname.c) ---- */

/* `rebushido-YYYYMMDD-HHMMSS.log` and the 0 after it. */
#define PORT_LOG_NAME_BYTES 30
/* How many sessions' logs are kept. */
#define PORT_LOGS_KEPT 20

/* A log's name for a date and a time of day, into PORT_LOG_NAME_BYTES. */
extern void port_FormatLogName(
    s32 year, s32 month, s32 day, s32 hour, s32 minute, s32 second, char *out);
/* Whether a file's name is a log's: exactly that form. */
extern s32 port_IsLogName(const char *name);
/* Of `count` names of a folder's files, which are logs that `keep` or
 * more logs are newer than: 1 in `is_old` for each. Returns how many. A
 * name that is not a log's is never one. */
extern s32 port_FindOldLogs(
    const char * const *names, s32 count, s32 keep, u8 *is_old);

/* What the program prints comes in pieces of any size; the log wants
 * whole lines, each written and flushed as it ends, so that a crash
 * loses nothing that was said before it. */
#define PORT_TEE_LINE_MAX 512

typedef struct {
    char line[PORT_TEE_LINE_MAX];
    u32 used;
} PortTee;

/* A whole line, without its end; `length` bytes, no 0 after them. */
typedef void (*PortLineSink)(const char *line, u32 length);
extern void port_TeeBytes(
    PortTee *tee, const char *bytes, u32 count, PortLineSink sink);
extern void port_FlushTee(PortTee *tee, PortLineSink sink);

/* zlib's sum of 32 bits; `crc` is 0 at the start, then the sum so far. */
extern u32 port_Crc32(u32 crc, const u8 *bytes, u32 count);

/* ---- the counter in the corner (fps.c) ---- */

/* A rate is worked out over this much of the clock: half a second. */
#define PORT_FPS_WINDOW 22050

typedef struct {
    s32 is_started;
    u32 window_start; /* the clock, in samples */
    u32 count;        /* things since then */
    u32 tenths;       /* the last rate, in tenths a second */
} PortFpsMeter;

/* `events` more of what is counted happened, at `now` on the clock. */
extern void port_CountFps(PortFpsMeter *meter, u32 now, u32 events);

/* The counter's text, `59.8/30.0 100%`: the pictures shown a second,
 * the game's own frames a second (it works a frame out every second
 * vertical blank in a fight, every blank in the main menu), and the
 * pictures shown as parts in a hundred of the console's 59.83. */
#define PORT_FPS_TEXT_BYTES 32
/* `fps = times`: the counter with the processor's and the card's time
 * per part of a frame under it (sdl_window.c, port_DrawCounter) */
#define PORT_FPS_TIMES 2
/* `vsync = adaptive`: the display waited for, but a picture that missed
 * its refresh is shown at once, torn, rather than a refresh late
 * (sdl_window.c, port_SetSwapInterval) */
#define PORT_VSYNC_ADAPTIVE 2
extern void port_FormatFps(
    char *out, u32 shown_tenths, u32 game_tenths, u32 blank_tenths);

/* The digits' shapes: 5 by 7, a column between two. */
#define PORT_GLYPH_W 5
#define PORT_GLYPH_H 7
/* A letter's seven rows, the top first, five bits each, the left
 * highest; NULL for one that has no shape (the digits, `/`, `%`, `.`
 * and `-` have). */
extern const u8 *port_FindGlyph(char letter);
/* How many pixels wide a text is. */
extern s32 port_MeasureText(const char *text);
extern void port_DrawText(
    u32 *pixels, s32 w, s32 h, s32 x, s32 y, const char *text, u32 color);

enum {
    PORT_CORNER_TOP_LEFT = 0,
    PORT_CORNER_TOP_RIGHT = 1,
    PORT_CORNER_BOTTOM_LEFT = 2,
    PORT_CORNER_BOTTOM_RIGHT = 3,
};

/* Where a box of the counter's own pixels goes in the window, given
 * where the picture is: in the picture's corner, each of its pixels a
 * whole number of the window's. */
extern void port_PlaceCounter(
    const PortRect *picture, s32 box_w, s32 box_h, s32 corner, PortRect *out);

/* ---- the processors with the larger cache (cpus.c) ---- */

/* What the system says of its processors' third-level caches, and which
 * of them the step's thread is to prefer (cpu_prefer; docs/PORT.md,
 * "The processors with the larger cache"). The system's own reading is
 * cpu_prefer.c's; what is made of it is here, held to made-up tables by
 * tests/port/window.c. */
#define PORT_CPUS_MAX 256
#define PORT_CACHES_MAX 32
#define PORT_CPU_LIST_BYTES 128
/* cpu_prefer's words */
#define PORT_CPU_PREFER_OFF 0
#define PORT_CPU_PREFER_AUTO 1
/* updates' words and update_channel's (update.c) */
#define PORT_UPDATES_ASK 0
#define PORT_UPDATES_AUTO 1
#define PORT_UPDATES_OFF 2
#define PORT_UPDATE_CHANNEL_RELEASE 0
#define PORT_UPDATE_CHANNEL_BETA 1

/* logical processors by number, a bit each (a Windows processor group's
 * processor n is number 64 * group + n) */
typedef struct {
    u32 words[PORT_CPUS_MAX / 32];
} PortCpuSet;

typedef struct {
    /* every logical processor the system has */
    PortCpuSet all;
    /* the third-level caches: each one's size in KB and the processors
     * that share it */
    s32 caches;
    u32 kb[PORT_CACHES_MAX];
    PortCpuSet on[PORT_CACHES_MAX];
    /* how many kinds of core (efficiency classes, or Linux's cpu_core and
     * cpu_atom): more than one is a hybrid processor */
    s32 kinds;
    /* something could not be read */
    s32 is_unread;
} PortCacheTable;

enum {
    PORT_CPUS_UNREAD = 0, /* nothing done: the table is not whole */
    PORT_CPUS_ONE_CACHE,  /* one L3 for all */
    PORT_CPUS_ONE_SIZE,   /* L3s of one size, or none clearly larger */
    PORT_CPUS_HYBRID,     /* cores of more than one kind: the system's */
    PORT_CPUS_PREFER      /* the larger cache's processors preferred */
};

/* The larger cache is preferred where it is this much larger than any
 * other, in tenths: 1.5 times. */
#define PORT_CPUS_LARGER_TENTHS 15

extern void port_AddCpu(PortCpuSet *set, s32 cpu);
extern s32 port_HasCpu(const PortCpuSet *set, s32 cpu);
extern s32 port_CountCpus(const PortCpuSet *set);
/* What is to be done with a table, and in `prefer` the processors
 * preferred where it is PORT_CPUS_PREFER (else nothing in it). */
extern s32 port_ChooseCpus(const PortCacheTable *table, PortCpuSet *prefer);
/* Linux's list of processors, `0-7,16-23` and an end of line, into a
 * set; FALSE for one that cannot be read. */
extern s32 port_ReadCpuList(const char *text, PortCpuSet *set);
/* Linux's size of a cache, `32768K` (or `96M`), in KB; 0 for one that
 * cannot be read. */
extern u32 port_ReadCacheSize(const char *text);
/* A set as Linux writes it, `0-7,16-23`, into PORT_CPU_LIST_BYTES (cut
 * short with `...` where it is longer). */
extern void port_FormatCpuList(const PortCpuSet *set, char *out);

#endif /* PORT_WINDOW_H */
