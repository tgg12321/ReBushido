#ifndef PORT_SDL_WINDOW_H
#define PORT_SDL_WINDOW_H

/* The native port's window, between its two SDL files (docs/PORT.md,
 * "The window"): sdl_main.c reads the command line and the settings,
 * sdl_window.c is the window, the sound card and the controllers. */

#include "window.h"

/* A plan's presses are made as the PC's own input (--inject). */
enum {
    PORT_INJECT_NONE = 0,
    PORT_INJECT_KEYS = 1, /* key events */
    PORT_INJECT_PADS = 2, /* two controllers SDL makes up */
};

/* What only the command line says. The last five are for the tests. */
typedef struct {
    s32 no_sound;           /* open no sound card */
    s32 is_fast;            /* never wait: as fast as the PC goes */
    s32 inject;             /* PORT_INJECT_... */
    const char *tap_frames; /* a folder for the pictures as they are shown */
    /* ... and for the window's own pixels, which only the build with a
     * graphics card has (docs/PORT.md, "Remastered in the window") */
    const char *tap_window;
    s32 tap_every;         /* ... every this many blanks */
    const char *tap_audio; /* a file for the samples as they are queued */
    s32 toggle_at;         /* change picture at every this many blanks */
    s32 shot_at;           /* what F12 does, at this blank */
    /* ... of which picture of it: 0 is the game's own frame, and -1 the
     * last one shown, which is a picture between two frames where more
     * a second than the game's are drawn */
    s32 shot_picture;
    /* the window's size as it opens, where a run asks for one: for a
     * test of a size a player's screen has and this machine has not */
    s32 window_w;
    s32 window_h;
    s32 fault_at;       /* touch memory that is not there at this blank */
    s32 fault_in_sound; /* ... on the sound card's thread, from it on */
    /* the settings menu (menu.h), for a test: opened as F1 opens it, at
     * these blanks ("1500,4000"); told these words, one a picture, as presses
     * (port_MenuWord; `shot` keeps the window's picture as a PNG in
     * `menu_shots`); closed when they run out */
    const char *menu_at;
    const char *menu_keys;
    const char *menu_shots;
} PortWindowOptions;

extern PortSettings g_port_settings;
extern PortWindowOptions g_port_window_options;

/* SDL's number for a key's name, or a controller's button's or axis's
 * (window.h, PortNameLookup). */
extern s32 port_LookUpName(s32 source, const char *name);
/* The pads' keys and buttons as they are now, to the error output. */
extern void port_DescribeControls(void);
/* Hangs the window on the port (port_platform.h, "the window"): it
 * opens at the first vertical blank. */
extern void port_StartWindow(void);
/* The settings file the settings menu writes (sdl_main.c): the one read,
 * or rebushido.cfg beside the program where none was. */
extern const char *port_GetSettingsPath(void);

/* ---- the settings menu in the window (sdl_menu.c) ---- */

union SDL_Event;

/* The menu is asked for (`why`, for the log): it opens at the end of the
 * first blank where the game has shown a frame since. */
extern void port_AskForMenu(const char *why);
/* At a blank's end: the menu, where it was asked for, from the moment it
 * opens to the moment it closes, with the game stopped. */
extern void port_MaybeRunMenu(u32 blank);
/* A pad's buttons `held` now, of which `sent` were held before: where
 * Select opens the menu (the screens that do nothing with it), the
 * buttons to hold back from the game until let go, and in `out` what the
 * game is given. */
extern u32 port_MenuTakesPad(u32 held, u32 sent, u32 *out);

/* ---- what sdl_window.c does for the menu ---- */

extern void port_GetWindowSize(s32 *w, s32 *h);
/* The game's picture as it stopped, darkened, the menu's `pixels` over it
 * at `place`, and shown; for a test, the window as a PNG at `shot` first,
 * where that is not NULL. */
extern void port_ShowMenu(
    u32 blank, const u32 *pixels, const PortRect *place, const char *shot);
/* Whatever the pads hold now is not given to the game until let go. */
extern void port_HoldBackPads(void);
/* A controller plugged in or pulled out while the menu is open. */
extern void port_TakeControllerEvent(const union SDL_Event *event);
/* The pad of a controller by SDL's number for it; -1 for none. */
extern s32 port_PadOfController(s32 id);
extern void port_ToggleFullscreen(void);

/* ---- the session's log (sdl_log.c) ---- */

/* A line `rebushido: ...` to the error output, and so to the log. */
extern void port_Say(const char *format, ...)
    __attribute__((format(printf, 1, 2)));
/* A line to the log alone. */
extern void port_Note(const char *format, ...)
    __attribute__((format(printf, 1, 2)));
/* Begins the log: the file `named`, or a new one in logs/ of the folder
 * `beside` (with its last separator). */
extern void port_OpenLog(const char *named, const char *beside);
extern void port_CloseLog(void);
/* The log's path, or NULL if the session has none. */
extern const char *port_GetLogPath(void);
/* A folder, if it is not there already: the log's own, and the one the
 * player's pictures of the window go in (F12). */
extern void port_MakeFolder(const char *path);
/* The session's last lines, with the code the program ends with; where
 * it ends because something went wrong, the player is told where the
 * log is. Then the log is shut. */
extern void port_EndSession(s32 code);

#endif /* PORT_SDL_WINDOW_H */
