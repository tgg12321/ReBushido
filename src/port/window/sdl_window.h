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
    s32 tap_every;          /* ... every this many blanks */
    const char *tap_audio;  /* a file for the samples as they are queued */
    s32 fault_at;           /* touch memory that is not there at this blank */
    s32 fault_in_sound;     /* ... on the sound card's thread, from it on */
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
/* The session's last lines, with the code the program ends with; where
 * it ends because something went wrong, the player is told where the
 * log is. Then the log is shut. */
extern void port_EndSession(s32 code);

#endif /* PORT_SDL_WINDOW_H */
