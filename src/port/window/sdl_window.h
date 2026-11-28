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
    s32 no_sound;  /* open no sound card */
    s32 is_fast;   /* never wait: as fast as the PC goes */
    s32 is_hidden; /* a window never shown: agents' runs on a real card */
    /* make the mods of the disc (prepare.c) and end, with no game */
    s32 prepare_only;
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
    /* the online pages (online_pages.c), for a test: opened as Online
     * Play opens them, at this blank; told these words (port_PagesWord:
     * presses, type:TEXT, shot, wait:N, until:lobby), the pictures kept in
     * `menu_shots` */
    s32 online_at;
    const char *online_keys;
    /* the replay theatre and photo mode (sdl_theatre.c): a test's words
     * (port_TheatreWord: presses, at:FRAME, wait:N, still:PATH, hash, ...),
     * and the view a run begins with (--theatre-view, the words
     * port_TheatreViewWords makes: a clip's film run is given its own),
     * and the free camera's path it follows (--theatre-path, the keys as
     * port_TheatrePathWords makes them) */
    const char *theatre_keys;
    const char *theatre_view;
    const char *theatre_path;
    /* Escape pressed at these blanks ("1500,4000") and let go a few after,
     * as SDL's events of the key (port_EscapeButton) */
    const char *escape_at;
    /* where each blank's time went and when each picture was shown
     * (port_profile.h; tools/port_profile.py) */
    const char *profile;
    /* the window's thread sampled into this file (sampler.c) */
    const char *sample;
    /* the benchmark's stress, `fighters=N,stage=M` (gl_bench.h) */
    const char *bench_stress;
    /* a display made up for a test, with no screen: this many blanks a
     * second, counted from the window's opening, and a swap with vsync
     * on waits for the next of them as a display's own would */
    s32 display_rate;
    /* quick_loading as the command line says: PORT_QUICK_... A run with a
     * plan or a count of blanks keeps the console's pace through its
     * loads unless --quick-loading says otherwise (docs/PORT.md, "Quick
     * loading") */
    s32 quick_loading;
    /* what F9 does, at this blank: a report (sdl_report.c) */
    s32 report_at;
    /* a stall made on purpose, for a test of the pace and the sound:
     * the window's thread sleeps `stall_ms` at every `stall_every`th
     * blank from `stall_at` on (0: none) */
    s32 stall_at;
    s32 stall_every;
    s32 stall_ms;
    /* the updater's (update.h): a test's server and key, which a trial
     * build alone takes; and the words the step that installs an update
     * gives the program it starts */
    const char *update_url;
    const char *update_key;
    s32 update_started;
    const char *update_rolled_back;
} PortWindowOptions;

enum {
    PORT_QUICK_SETTINGS = 0, /* as the settings say */
    PORT_QUICK_OFF = 1,
    PORT_QUICK_ON = 2,
};

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
/* At a blank's end: the menu, where it was asked for. Over ReBushido's
 * top menu and the fight's pause it opens beside the game, which goes on
 * running under it with no press given it, and has a turn here at every
 * blank; anywhere else it runs from the moment it opens to the moment it
 * closes, with the game stopped. The online pages likewise. */
extern void port_MaybeRunMenu(u32 blank);

/* ---- the window's pages over the game (sdl_menu.c; docs/PORT.md, "The
 * window's pages in the game's look") ---- */

/* Whether the game is where a page opens beside it: ReBushido's top menu,
 * or the fight's pause. */
extern s32 port_PageCanBeBeside(void);
/* Whether a page (the settings menu, the online pages) is open beside the
 * running game: the PC's input is the page's, and the game is given no
 * press. */
extern s32 port_PageIsBeside(void);
/* ReBushido's top menu covered by a page or not (port_top_menu.h). */
extern void port_PageCover(s32 on);
/* The page's sounds played or not (port_PageSound): where the game runs
 * beside it, or its sound goes on while it is stopped. */
extern void port_PageSoundsOn(s32 on);
/* An event of SDL's taken by a page beside the game: TRUE where it was
 * (every one but the window's own keys, F2, F3, F9 and F12). */
extern s32 port_PageTakesEvent(const union SDL_Event *event);
/* A page's turn or press: its pixels are drawn again at the next
 * picture (port_PagePixels). */
extern void port_PageChanged(void);
/* Whether the page open draws the game's backdrop itself (anywhere but
 * over the main menu, which draws its own): the window darkens the whole
 * picture under it. */
extern s32 port_PageHasOwnBackdrop(void);
/* What is laid over the game's 4:3 area for the page open, as
 * port_StagePagePixels: FALSE where none is. */
extern s32 port_PagePixels(
    s32 area_w, s32 area_h, const u32 **pixels, PortRect *box);
/* A test's picture of the window with the page on it: at the next blank
 * shown, where the page is beside the game; the path, once, to the window
 * as it shows it (sdl_window.c). */
extern void port_PageShot(const char *path);
extern const char *port_PageTakeShot(void);
/* The game's sound for a blank where it is stopped and a page asks that
 * its sound go on (port_RunStoppedSound): as many blanks as the PC's time
 * owes since the last, at most a few. */
extern void port_PageKeepSound(void);
/* A pad's buttons `held` now, of which `sent` were held before: where
 * Select opens the menu (the screens that do nothing with it), the
 * buttons to hold back from the game until let go, and in `out` what the
 * game is given. */
extern u32 port_MenuTakesPad(u32 held, u32 sent, u32 *out);

/* ---- what sdl_window.c does for the menu ---- */

extern void port_GetWindowSize(s32 *w, s32 *h);
/* The game's picture as it stopped, with the page open over it
 * (port_PagePixels), and shown; for a test, the window as a PNG at `shot`
 * first, where that is not NULL. */
extern void port_ShowPage(u32 blank, const char *shot);
/* The game's picture as it stopped, darkened, `pixels` over it at `place`,
 * and shown; for a test, the window as a PNG at `shot` first, where that
 * is not NULL: netplay's panel of a session from the command line
 * (sdl_net.c). */
extern void port_ShowMenu(
    u32 blank, const u32 *pixels, const PortRect *place, const char *shot);
/* Whatever the pads hold now is not given to the game until let go. */
extern void port_HoldBackPads(void);
/* Escape down or up: the game's pause, or its way back, on player one's
 * pad while it is held (sdl_window.c, port_EscapeButton). */
extern void port_TakeEscape(s32 down);
/* A controller plugged in or pulled out while the menu is open. */
extern void port_TakeControllerEvent(const union SDL_Event *event);
/* The pad of a controller by SDL's number for it; -1 for none. */
extern s32 port_PadOfController(s32 id);
extern void port_ToggleFullscreen(void);
/* The game's switches of the settings -- bugfixes and the game's three
 * options -- given to the run (port_top_menu.h): as it starts, and as the
 * settings menu changes one. */
extern void port_WindowGameSwitches(void);
/* The window's training_ settings, into the run's (port_training.h). */
extern void port_WindowTrainingSwitches(void);

/* ---- netplay in the window (sdl_net.c; docs/NETPLAY.md, section 13)
 * ---- */

/* Hangs the window's part of netplay on the port: its line, its waits,
 * its pacing and its sound in a rollback (port_net.h). */
extern void port_StartNetWindow(void);
/* Netplay's line over the picture: the ping, the rollback, the delay,
 * and what is being waited for. Where netplay is off, a replay's line
 * (port_ReplayWords), or nothing. */
extern void port_DrawNetLine(void);

/* A session asked for, by the online pages or the settings menu's Play
 * online page (sdl_main.c): it begins at the game's next boundary in this
 * program (port_online.h), or, where it cannot, the program starts again
 * as the session's. */
/* ---- replays (sdl_replay.c; port_replay.h) ---- */

/* Every fight kept in replays/ in `beside` (the program's folder), but
 * in a test's run, and the window's ways of a playback hung on the port. */
extern void port_StartReplayWindow(const char *beside, s32 is_test);
/* The settings' replays, kept or not and how many, given the port. */
extern void port_ApplyReplaySettings(void);
/* An event of SDL's taken by a playback (its keys and buttons, and every
 * other key but F1 to F12, which are the game's no longer): TRUE where it
 * was. */
extern s32 port_ReplayWindowEvent(const union SDL_Event *event);
/* The line's words while a replay plays, or for a moment after it, into
 * `out` ("" for none), and their colour. */
extern u32 port_ReplayWords(char *out, u32 room);

/* ---- the replay theatre and photo mode (sdl_theatre.c) ---- */

/* The run's start: the view --theatre-view asks for, a test's words. */
extern void port_StartTheatre(void);
/* Each frame's boundary, before the theatre's look (sdl_window.c's
 * boundary hook): whether looks are run, the free camera moved by what is
 * held, a test's words, and photo mode's wait while it is on. */
extern void port_TheatreBoundary(void);
/* While a replay's playback waits (paused, or slower than played): the
 * same, and the frame on show looked at again where the view changed. */
extern void port_TheatrePausedTick(void);
/* An event of the window's: TRUE where it was the theatre's (its keys,
 * the mouse and sticks of the free camera, F4 for photo mode). */
extern s32 port_TheatreWindowEvent(const union SDL_Event *event);
/* Whether the window's own lettering over the fight is left out with the
 * game's HUD. */
extern s32 port_TheatreHidesOverlays(void);
/* What the line says of the theatre while a replay plays, or of photo
 * mode, into `out` ("" for nothing); FALSE where the line is hidden. */
extern s32 port_TheatreWords(char *out, u32 room);
/* The theatre's still: the picture alone at the window's size, a PNG at
 * `path` or in screenshots/ (sdl_window.c); the file's path in
 * `written`. */
extern s32 port_WriteWindowStill(
    const char *path, char *written, u32 room, s32 *w, s32 *h);
/* For a test and the eye: the window as it is shown, the picture with
 * what is laid over it (the seek bar, the pages), a PNG at `path`. */
extern s32 port_WriteWindowShown(const char *path);
/* Where the game's 4:3 area is in the window, in its pixels: what the
 * pages and the seek bar are placed in. */
extern void port_GetLookArea(PortRect *area);
/* A point of the mouse's (SDL's, in the window's own units) in the
 * window's pixels. */
extern void port_MouseToPixels(s32 *x, s32 *y);
/* The theatre's seek bar over the picture, where it is shown
 * (sdl_theatre.c; port_DrawCounter). */
extern void port_TheatreDrawBar(void);

/* ---- a replay as a film (sdl_export.c; film.h) ---- */

/* The run that makes one (--replay FILE --export OUT): the film's file,
 * its kind by its ending, and its size. FALSE, with the reason, where it
 * cannot be made. */
extern s32 port_ExportSetUp(
    const char *path, s32 w, s32 h, char *why, u32 room);
extern s32 port_ExportIsOn(void);
/* Whether this blank is the film's: the replay at its fight. */
extern s32 port_ExportWants(void);
/* The blank's picture, `w` by `h` pixels 0xFFRRGGBB from the top, as the
 * window shows it; NULL for a blank that shows none (the last again). */
extern void port_ExportPicture(u32 blank, const u32 *pixels, s32 w, s32 h);
/* The samples the window is given, as they are made. */
extern void port_ExportSound(const s16 *samples, u32 frames);
/* The replay ended on a problem: the film is not made. */
extern void port_ExportReplayEnded(const char *trouble);
/* The program ends with `code`: the film finished, or taken away. The
 * code the program ends with: `code`, or PORT_EXIT_FAILED where the film
 * was not made. */
extern s32 port_ExportEnd(s32 code);

/* The program that asks for one (the settings menu's Replays page). */
enum {
    PORT_EXPORT_IDLE = 0,
    PORT_EXPORT_RUNNING = 1,
    PORT_EXPORT_DONE = 2,
    PORT_EXPORT_FAILED = 3,
    PORT_EXPORT_STOPPED = 4,
};

typedef struct {
    s32 state; /* PORT_EXPORT_... */
    u32 permille;
    u32 generation;  /* new at each change */
    u32 seconds;     /* since it began, while it runs */
    char name[96];   /* the replay's file */
    char words[512]; /* where it went, or why there is none */
} PortExportView;

/* A film of the replay at `replay` (its file's name `name`) begun by
 * another run of this program, into films/ beside it, with the size and
 * the picture the settings say (film_size, film_picture): FALSE, with
 * the reason, where it cannot be. */
extern s32 port_ExportBegin(
    const char *replay, const char *name, char *why, u32 room);

/* ... and a clip of the replay theatre's (sdl_theatre.c): its frames, the
 * theatre's view as --theatre-view words, and a GIF or a film */
typedef struct {
    u32 from;
    u32 to;
    const char *view;
    const char *path; /* the camera's keys (--theatre-path), or NULL */
    s32 is_gif;
} PortExportClip;

extern s32 port_ExportClip(const char *replay, const char *name,
                           const PortExportClip *clip, char *why, u32 room);
/* The run that makes one: the replay's frames it is of, `to` 0 for its
 * end (--export-from, --export-to). */
extern void port_ExportSetRange(u32 from, u32 to);
extern u32 port_ExportFrom(void);
/* ... stopped, its file taken away */
extern void port_ExportStop(void);
/* ... and the last one's words no longer shown */
extern void port_ExportForget(void);
/* Each blank, and each turn of a page: the run's lines read, its end
 * seen. */
extern void port_ExportTick(void);
extern void port_ExportGetView(PortExportView *view);

/* The program's own path, the disc image it runs, and the word after
 * `option` on its command line, or NULL (sdl_main.c). */
extern const char *port_GetProgramPath(void);
extern const char *port_GetImagePath(void);
extern const char *port_GetCommandWord(const char *option);

/* The settings menu's Play online page: the program starts again as a
 * session's (sdl_main.c), with the network's options of its command line
 * given anew. */
typedef struct {
    s32 role;            /* PORT_NET_ROLE_... (window.h) */
    const char *bind;    /* this program's socket, `ADDRESS:PORT` */
    const char *address; /* join: the host's */
    const char *lobby;   /* room, rooms: the lobby server's */
    const char *code;    /* rooms: the room's code */
    const char *room;    /* room: its name */
    const char *player;
    const char *key;      /* a locked room's SALT:KEY in hex, or NULL */
    const char *password; /* rooms by code: the password typed, or NULL */
    const char *punch_to; /* host: the other player's address, or NULL */
    s32 delay;
    s32 portmap; /* the host asks its router for its port */
    s32 players; /* host: 2, or 4 (docs/NETPLAY.md, section 18) */
    s32 pcs;     /* ... on 4 PCs, or two on each of 2 */
} PortOnlineStart;

/* NULL, or what is wrong, in a player's words. */
extern const char *port_StartOnline(const PortOnlineStart *start);

/* ---- the online pages (online_pages.c) ---- */

/* Hangs the pages on a session begun in the program (port_online.h). */
extern void port_StartOnlinePages(void);
/* Online Play: the pages open at the end of the next blank. */
extern void port_AskForOnlinePages(const char *why);
/* At a blank's end: the pages, where asked for; TRUE if they were up.
 * Beside the game, a turn of theirs. */
extern s32 port_MaybeRunOnlinePages(u32 blank);
/* Whether they are open, and beside the running game; an event of SDL's
 * given them there; and their page drawn in the game's look (menu.h,
 * PortLook). */
extern s32 port_OnlinePagesAreOpen(void);
extern s32 port_OnlinePagesAreBeside(void);
extern void port_OnlinePagesEvent(const union SDL_Event *event);
struct PortLook;
extern void port_OnlinePagesDraw(struct PortLook *look, s32 is_own);
/* The state of another moment was put back (a session begun or ended in
 * the program): nothing drawn between frames until two more, all of
 * video memory given to the card, the settings menu's count of the top
 * menu's asks followed (sdl_window.c, sdl_menu.c). */
extern void port_WindowStateReplaced(void);
extern void port_MenuFollowAsks(void);
/* The port mapping's options (the tests' fake router: --portmap-ssdp,
 * --portmap-gateway), and whether --portmap off said none. */
extern const void *port_WindowPortMapOptions(void);

/* ---- what sdl_window.c does for it ---- */

/* `pixels`, `w` by `h`, over what has been drawn of the picture so far,
 * at (`x`, `y`) of the window, blended by their alpha. */
extern void port_LayPixels(const u32 *pixels, s32 w, s32 h, s32 x, s32 y);
/* The window's events read now, between two blanks (closing the window
 * ends the program). */
extern void port_PollWindow(void);
/* The game's last picture shown again, with the counter and the line. */
extern void port_ShowStill(void);
/* The blanks from here on are later by `blanks` in all: time sync. */
extern void port_HoldPace(u32 blanks);
/* A rollback of `blanks` blanks begins: what the sound's queue holds of
 * them and has not played is taken back; the corrected sound of them
 * (port_SoundRerun) goes in its place; and it ends. */
extern void port_SoundRerunBegins(u32 blanks);
extern void port_SoundRerun(const s16 *samples, u32 frames);
extern void port_SoundRerunEnds(void);

/* ---- a report of a problem, F9 (sdl_report.c) ---- */

/* What the last seconds are kept of: a blank's processor time by part
 * (port_profile.h, in microseconds), the card's passes of a blank it has
 * finished (gl_bench.h, in nanoseconds), and a picture handed to the
 * display (when, in microseconds since the window opened, and its
 * phase between two frames, -1 for the game's own). */
/* --sample (sampler.c): started as the window opens, written as it
 * closes; the blank whose time is being counted, for the samples. */
extern void port_SampleStart(const char *path);
/* What the card's driver does once, done in the first pictures (warm.c):
 * called at every swap. */
extern void port_WarmDriver(void);

/* The processors with the larger cache (cpu_prefer.c): the system's
 * caches read and what is to be done said in the log, with
 * PORT_CPU_PREFER_OFF or _AUTO; then the calling thread asked to run on
 * the processors preferred, where something is to be asked, and what
 * was said to that put in the log (by the window's thread: the log is
 * its). */
enum { PORT_CPU_ASKED_NOTHING, PORT_CPU_ASKED, PORT_CPU_REFUSED };

extern void port_CpuPreferStart(s32 setting);
extern s32 port_CpuPreferHere(void);
extern void port_CpuPreferSay(s32 asked, const char *thread);
extern void port_SampleEnd(void);
extern volatile u32 g_port_sample_blank;
extern void port_ReportNoteBlank(u32 blank, s32 is_new_frame, const u32 *cpu);
extern void port_ReportNoteCard(u32 blank, const u32 *nanoseconds);
extern void port_ReportNoteSwap(unsigned long long microseconds, s32 phase);
/* A new report's folder, made, into `out`: 1, or 0 if it cannot be. */
extern s32 port_ReportFolder(char *out, u32 room);
/* The report's files but the picture, into `folder`: the last seconds'
 * times, the log, the settings file and `about`. */
extern void port_WriteReport(const char *folder, const char *about);

/* ---- the mods made from the player's disc (prepare.c) ---- */

/* Makes the mods of mods/ that prepare/ beside the program makes and that
 * are not yet made for `key` (the program's version and the disc's),
 * showing how far it has come in a window of its own (never shown if
 * `is_hidden`). `beside` is the program's folder with its last separator.
 * Nonzero if one was not made, skipped or failed; *closed if the player
 * closed the window. Nothing where there is no prepare/, or off Windows.
 * For a test, `shots` (NULL for none) is a folder the screen is written
 * into as each mod is begun. */
extern s32 port_PrepareMods(
    const char *beside, const char *image, const char *key, s32 is_hidden,
    const char *shots, s32 *closed);

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
