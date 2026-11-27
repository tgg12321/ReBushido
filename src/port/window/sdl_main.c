/* The native port with a window (docs/PORT.md, "The window"): where the
 * program starts in that build. It takes the window's own options off
 * the command line, reads the settings file, finds the disc image if
 * nothing names one, says how the pads are pressed, hangs the window on
 * the port (sdl_window.c) and then is the port's program as it is
 * without a window (src/port/main.c):
 *
 *   rebushido [--image FILE] [--config FILE] [--fullscreen | --windowed]
 *             [--scale fit|integer|stretch] [--filter nearest|linear]
 *             [--no-vsync] [--no-sound] [--no-fps] [--no-log]
 *             [--log FILE]
 *             [--graphics original|remastered] [--resolution N]
 *             [--draw-distance original|far|max] [--antialiasing off|2|4]
 *             [--aspect W:H] [--frame-rate original|display|N]
 *             [the options of the build without a window]
 *
 * and for the tests, which show and sound nothing:
 *
 *             [--fast] [--inject keys|pads] [--tap-frames FOLDER]
 *             [--tap-window FOLDER] [--tap-every N] [--tap-audio FILE]
 *             [--toggle-at N] [--shot-at N] [--fault-at N]
 *             [--fault-in-sound N] [--menu-at N[,N...]] [--menu-keys WORDS]
 *             [--menu-shots FOLDER] [--escape-at N[,N...]] [--profile FILE]
 * [--sample FILE]
 *             [--display-rate N]
 *             [--bench-stress fighters=N,stage=M] */
#define SDL_MAIN_HANDLED
#include <SDL.h>
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
#include <windows.h>

#include <commdlg.h>
#include <process.h>
#else
#include <unistd.h>
#endif

#include "port_host.h"
#include "film.h"
#include "port_mods.h"
#include "port_net.h"
#include "port_online.h"
#include "portmap.h"
#include "port_music.h"
#include "port_remaster.h"
#include "port_play.h"
#include "port_stage_choice.h"
#include "port_fight_music.h"
#include "port_match.h"
#include "port_ranked.h"
#include "port_duo.h"
#include "port_cpu.h"
#include "port_top_menu.h"
#include "shared.h"
#include "sdl_window.h"
#include "update.h"

#define PATH_BYTES_MAX 1024
#define ARGUMENTS_MAX 64
#define SETTINGS_FILE "rebushido.cfg"
/* the ranked identity, beside the settings (docs/PLAYING.md, "Ranked") */
#define IDENTITY_FILE "rebushido-identity.key"
#define SETTINGS_BYTES_MAX 0x10000
#define IMAGE_SUM_BYTES 0x100000
#define CUE_BYTES_MAX 2048

static const char g_port_window_usage[] =
    "rebushido: ReBushido's native port, in a window (docs/PORT.md)\n"
    "\n"
    "  rebushido [--image FILE] [--config FILE]\n"
    "            [--fullscreen | --windowed]\n"
    "            [--scale fit|integer|stretch] [--filter nearest|linear]\n"
    "            [--no-vsync] [--no-sound] [--no-fps] [--no-log]\n"
    "            [--log FILE] [--graphics original|remastered]\n"
    "            [--resolution N] [--draw-distance original|far|max]\n"
    "            [--antialiasing off|2|4] [--aspect W:H]\n"
    "            [--frame-rate original|display|N] [--quick-loading on|off]\n"
    "\n"
    "  --image FILE   your own image of the game's disc: the .cue, or the\n"
    "                 .bin of raw sectors it names. Without it the first\n"
    "                 .cue, or else .bin, beside the program is taken\n"
    "  --config FILE  the settings file; rebushido.cfg beside the program\n"
    "                 if nothing says, and the settings built in if there\n"
    "                 is none\n"
    "  --fullscreen   start filling the screen, which is how it starts\n"
    "                 with nothing said\n"
    "  --windowed     start in a window instead\n"
    "  --scale MODE   fit: four by three, as large as the window holds;\n"
    "                 integer: 320 by 240 a whole number of times;\n"
    "                 stretch: all of the window\n"
    "  --filter KIND  nearest: each pixel a block; linear: smoothed\n"
    "  --no-vsync     do not wait for the display\n"
    "  --no-sound     open no sound card\n"
    "  --no-fps       no counter in the corner (F3 shows and hides it)\n"
    "  --no-log       keep no log of the session\n"
    "  --log FILE     the session's log; without it a new file in logs/\n"
    "                 beside the program, of which the newest twenty are\n"
    "                 kept\n"
    "  --graphics W   original: the console's own picture, pixel for\n"
    "                 pixel; remastered: the scene drawn again by the\n"
    "                 graphics card at the window's resolution. Without\n"
    "                 it the options page at boot says which, and F2\n"
    "                 changes picture while the game runs\n"
    "  --resolution N how many times the console's resolution the scene\n"
    "                 is drawn at; 0, the default, is the window's own\n"
    "                 size\n"
    "  --draw-distance W   original: the console's disc of stage cells;\n"
    "                 far: the whole wedge the camera looks down; max:\n"
    "                 every cell of the stage, with the fog pushed out to\n"
    "                 match\n"
    "  --antialiasing N    the scene's edges multisampled, N times N\n"
    "                 samples a pixel (8 at most): off, 2 or 4\n"
    "  --aspect W:H   the shape of the picture: 4:3 as the console, 16:9\n"
    "                 for a wider view of the same scene\n"
    "  --stage-mesh W on, the default: the stage's ground drawn from one\n"
    "                 static mesh built when the stage loads, instead of\n"
    "                 from the game's own tile primitives; off is the\n"
    "                 tiles. It needs the remastered picture\n"
    "  --shadows W    original, the default: the game's own black\n"
    "                 shadows; soft: the ground under a shadow darkened\n"
    "                 instead, and never the fighter standing in it; or\n"
    "                 a number, how much of the ground it takes, in\n"
    "                 hundredths; cast (cast:N): the same, cast from\n"
    "                 the fighters' own bodies; map: the sun's own\n"
    "                 shadow maps, on the ground, walls, steps and the\n"
    "                 fighters. It needs the remastered picture\n"
    "  --frame-rate W  original: one picture a vertical blank, as the\n"
    "                 console; display: as many pictures a second as the\n"
    "                 screen shows; or a number of them. The game steps\n"
    "                 at the console's own rate whatever this says: a\n"
    "                 picture between two of its frames is drawn from\n"
    "                 frames it has worked out already. It needs the\n"
    "                 remastered picture\n"
    "  --quick-loading W  on, the default: the waits for the disc behind\n"
    "                 a loading picture are hurried through; off: each\n"
    "                 blank of them at the console's pace, as a run with\n"
    "                 --plan or --frames has it unless this says on. The\n"
    "                 game computes the same either way\n"
    "\n"
    "In the window: F1 opens the settings menu (Select does on the\n"
    "options page, the main menu and the pause, and a controller's guide\n"
    "button anywhere), which changes and writes the settings file.\n"
    "Escape is the game's Start in a fight, which pauses it, and its\n"
    "way back everywhere else (Triangle; Cross skips a cutscene). It\n"
    "never ends the program: closing the window does, or Quit in the\n"
    "settings menu. F11 or Alt+Enter fills the screen and comes back.\n"
    "F2 changes between the original picture and the remastered one. F3\n"
    "shows and hides the\n"
    "counter in the corner: pictures shown a second, the game's own\n"
    "frames a second, and the speed against the console's. F12 keeps a\n"
    "picture of the window as a PNG in screenshots/ beside the program,\n"
    "and the session's log says where it went. F9 keeps a report of a\n"
    "problem: the picture, the last ten seconds' times, the log, the\n"
    "settings and what the PC is, in a folder report-... beside the log.\n"
    "Online, F5 resyncs a game fallen out of step with the other's.\n"
    "--fight STAGE FIGHTER FIGHTER starts straight in a fight (below).\n"
    "\n"
    "A replay as a film: --replay FILE --export OUT.mp4 plays it with no\n"
    "window shown, as fast as the PC goes, and writes the fight as a film\n"
    "with its sound: an .mp4 (H.264 and AAC, by Windows' own encoders) or\n"
    "an .avi (Motion JPEG and the sound as it is, on any system). It is\n"
    "1920 by 1080 unless --window WxH says, the picture as --graphics or\n"
    "the settings say (docs/PLAYING.md, \"Replays\").\n"
    "\n"
    "Online: the settings menu's Play online page, or --host ADDRESS:PORT\n"
    "(0.0.0.0:7845 to be found from other PCs) and --join ADDRESS:PORT\n"
    "on the other PC, with --net-delay N on the host's (docs/PLAYING.md,\n"
    "\"Playing online\").\n"
    "\n"
    "The memory cards are card1.mcd and card2.mcd beside the program,\n"
    "card images of 128 KB that an emulator or a card tool opens; one\n"
    "that is not there is made, formatted and empty. --card1 FILE,\n"
    "--card2 FILE and --no-cards say otherwise.\n"
    "\n"
    "The settings file, with everything as it is when there is none. The\n"
    "first controller plugged in is player one's, the second player\n"
    "two's; a line `p1.cross = key Space` takes the built-in key of that\n"
    "button away, and Space from whatever it pressed as built in; more\n"
    "lines add more keys, `p1.cross = none` leaves it with nothing. A\n"
    "key's name is SDL's (Up, Space, Return, Left Shift,\n"
    "Keypad 4), a controller's is SDL's too (a, b, x, y, back, start,\n"
    "leftshoulder, dpup; +leftx, -lefty, +righttrigger for a stick or a\n"
    "trigger pushed one way).\n"
    "\n";

static const char g_port_window_usage_end[] =
    "\n"
    "The options of the build without a window work here too (--frames,\n"
    "--trace, --plan, --dump, --out, --shots, --every, --vram, --wav,\n"
    "--spu), --prepare-only (make the mods of the disc that prepare/\n"
    "beside the program makes, as the first start does, and end:\n"
    "docs/MODS.md, \"Made from your disc\"), and for the tests, which show\n"
    "and sound nothing: --fast\n"
    "(never wait), --inject keys|pads (a plan's presses as key events,\n"
    "or as two controllers SDL makes up), --tap-frames FOLDER and\n"
    "--tap-every N (each Nth picture as SDL is given it), --tap-window\n"
    "FOLDER (each Nth window as a player would see it, where there is a\n"
    "graphics card), --toggle-at N (what F2 does, at every Nth blank),\n"
    "--shot-at N[,P] (what F12 does, at blank N, keeping the Pth\n"
    "picture of it: 0 is the game's own frame and -1 the last shown),\n"
    "--window WxH (a window, not the full screen, that size as it opens), --tap-audio FILE\n"
    "(the samples as they are queued), --film-read FILM FOLDER --film-at\n"
    "N[,N...] (a film read back: those frames, as --tap-window's files,\n"
    "and its sound), --fault-at N and --fault-in-sound N\n"
    "(touch memory that is not there at vertical blank N, on the game's\n"
    "stack or on the sound card's thread: what a crash leaves in the\n"
    "log), --menu-at N[,N...] (what F1 does, at those blanks),\n"
    "--menu-keys WORDS (presses in the settings menu, one a picture: up,\n"
    "down, left, right, accept, back, close, clear, key:NAME, pad:NAME,\n"
    "shot, wait:N), --menu-shots FOLDER (where shot keeps the window),\n"
    "--online-at N (the online pages opened at blank N, as Online Play\n"
    "opens them), --online-keys WORDS (presses on them: up, down, left,\n"
    "right, accept, back, delete, shift, done, type:TEXT, shot, wait:N,\n"
    "until:lobby|waiting|cards),\n"
    "--escape-at N[,N...] (Escape pressed at those blanks),\n"
    "--run-ahead 0|1|2 (frames run ahead, whatever the settings say;\n"
    "with --fast or a picture taken for a test it is 0 unless this says),\n"
    "--profile FILE (where each blank's time went, and when each picture\n"
    "was shown: tools/port_profile.py reads it), --sample FILE (the\n"
    "window's thread sampled a thousand times a second, for\n"
    "tools/port_stalls.py), --display-rate N (a\n"
    "display made up of N blanks a second, which a swap waits for) and\n"
    "--bench-stress fighters=N,stage=M (the HD fighters drawn as if there\n"
    "were N, the ground M times over: tools/port_bench.py), --report-at N\n"
    "(what F9 does, at blank N) and --stall MS[,EVERY[,FROM]] (the window\n"
    "stops MS milliseconds at every EVERYth blank from FROM: a hitch made\n"
    "on purpose, for the pace and the sound). A trial build (-dev) takes\n"
    "--update-url URL and --update-key HEX: its updater asks a test's\n"
    "server, with a test's key (tools/port_update_test.py; docs/PORT.md,\n"
    "\"Updates\").\n";

static void port_Complain(const char *problem, const char *word) {
    fprintf(stderr, "rebushido: %s%s\nrebushido: --help says what it takes\n",
            problem, word);
    exit(PORT_EXIT_USAGE);
}

/* ---- the settings file ---- */

static const char *g_port_settings_path;
/* where the settings menu writes them: the file read, or the one beside
 * the program that was not there */
static const char *g_port_settings_written;

const char *port_GetSettingsPath(void) { return g_port_settings_written; }

static void port_ComplainOfLine(s32 line, const char *why, const char *text) {
    port_Say("%s line %d is not used (%s): %s", g_port_settings_path, line, why,
             text);
}

/* A whole text file with a 0 after it, or NULL. */
static char *port_ReadSettingsFile(const char *path) {
    FILE *file = fopen(path, "rb");
    char *text;
    size_t got;

    if (file == NULL) {
        return NULL;
    }
    text = malloc(SETTINGS_BYTES_MAX + 1);
    if (text == NULL) {
        fclose(file);
        return NULL;
    }
    got = fread(text, 1, SETTINGS_BYTES_MAX, file);
    fclose(file);
    text[got] = 0;
    return text;
}

/* The settings built in, then the file's: the one named, or the one
 * beside the program if it is there. */
static void port_LoadSettings(const char *named, const char *beside) {
    static char path[PATH_BYTES_MAX];
    char *text;

    port_SetDefaultSettings(&g_port_settings, port_LookUpName);
    if (named != NULL) {
        g_port_settings_path = named;
    } else {
        snprintf(path, sizeof(path), "%s%s", beside, SETTINGS_FILE);
        g_port_settings_path = path;
    }
    g_port_settings_written = g_port_settings_path;
    text = port_ReadSettingsFile(g_port_settings_path);
    if (text == NULL) {
        if (named != NULL) {
            port_Complain("the settings file cannot be read: ", named);
        }
        g_port_settings_path = NULL;
        return;
    }
    port_ReadSettings(
        &g_port_settings, text, port_LookUpName, port_ComplainOfLine);
    free(text);
}

/* ---- the disc image ---- */

static s32 port_EndsWith(const char *name, const char *ending) {
    size_t length = strlen(name);
    size_t ending_length = strlen(ending);

    return length >= ending_length &&
           SDL_strcasecmp(name + length - ending_length, ending) == 0;
}

/* The first file of a folder, by name, that ends so: 1 and its path. */
static s32 port_FindFileEnding(
    const char *folder, const char *ending, char *path, size_t room) {
    char best[PATH_BYTES_MAX];
    DIR *listing = opendir(folder[0] != 0 ? folder : ".");
    struct dirent *entry;

    best[0] = 0;
    if (listing == NULL) {
        return 0;
    }
    while ((entry = readdir(listing)) != NULL) {
        if (port_EndsWith(entry->d_name, ending) &&
            strlen(entry->d_name) < sizeof(best) &&
            (best[0] == 0 || strcmp(entry->d_name, best) < 0)) {
            strcpy(best, entry->d_name);
        }
    }
    closedir(listing);
    if (best[0] == 0) {
        return 0;
    }
    snprintf(path, room, "%s%s", folder, best);
    return 1;
}

#if defined(_WIN32)
/* Windows' own dialog for choosing a file. Never opened where SDL has
 * been told to show nothing (the tests). NOT TRIED: it was written and
 * read, and no dialog has been opened. */
static s32 port_AskForImage(char *path, size_t room) {
    OPENFILENAMEA ask;
    const char *driver = getenv("SDL_VIDEODRIVER");

    if (driver != NULL &&
        (strcmp(driver, "dummy") == 0 || strcmp(driver, "offscreen") == 0)) {
        return 0;
    }
    memset(&ask, 0, sizeof(ask));
    path[0] = 0;
    ask.lStructSize = sizeof(ask);
    ask.lpstrFilter = "Disc images (*.cue, *.bin)\0*.cue;*.bin\0"
                      "All files\0*.*\0";
    ask.lpstrFile = path;
    ask.nMaxFile = (DWORD)room;
    ask.lpstrTitle = "ReBushido: your image of the Bushido Blade 2 disc";
    ask.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR |
                OFN_HIDEREADONLY;
    return GetOpenFileNameA(&ask) != 0;
}
#else
static s32 port_AskForImage(char *path, size_t room) {
    (void)path;
    (void)room;
    return 0;
}
#endif

/* The image when nothing names one: a .cue beside the program, a .bin
 * beside it, or the player is asked. Ends the program if there is
 * none. */
static const char *port_FindImage(const char *beside) {
    static char path[PATH_BYTES_MAX];

    if (port_FindFileEnding(beside, ".cue", path, sizeof(path)) ||
        port_FindFileEnding(beside, ".bin", path, sizeof(path))) {
        port_Say("the disc image beside the program: %s", path);
        return path;
    }
    if (port_AskForImage(path, sizeof(path))) {
        return path;
    }
    port_Say("no disc image. The game is read from your own image of the");
    port_Say("Bushido Blade 2 disc (SLUS-00663), which is not part of this "
             "program.");
    port_Say("Put its .cue and .bin beside the program (%s),",
             beside[0] != 0 ? beside : "this folder");
    port_Say("or name it: rebushido --image \"Bushido Blade 2 (USA).cue\"");
    port_EndSession(PORT_EXIT_USAGE);
    exit(PORT_EXIT_USAGE);
}

/* A file of the image's size and the CRC-32 of its first megabyte, which
 * tells one image from another without reading 700 MB as the program
 * starts. (The port itself checks every sector it reads for its mark and
 * its own number: src/port/disc.c.) FALSE if it cannot be opened. */
static s32 port_SumImageFile(
    const char *path, long long *size, u32 *sum, u32 *summed) {
    static u8 bytes[IMAGE_SUM_BYTES];
    FILE *file = fopen(path, "rb");
    size_t got;

    *size = 0;
    if (file == NULL) {
        return FALSE;
    }
    got = fread(bytes, 1, sizeof(bytes), file);
#if defined(_WIN32)
    if (_fseeki64(file, 0, SEEK_END) == 0) {
        *size = _ftelli64(file);
    }
#else
    if (fseeko(file, 0, SEEK_END) == 0) {
        *size = ftello(file);
    }
#endif
    fclose(file);
    *sum = port_Crc32(0, bytes, (u32)got);
    *summed = (u32)got;
    return TRUE;
}

/* For the log: a file of the image by name, its size and its sum. */
static void port_NoteImageFile(const char *path) {
    long long size;
    u32 sum;
    u32 summed;

    if (!port_SumImageFile(path, &size, &sum, &summed)) {
        port_Note("image: %s (cannot be opened)", path);
        return;
    }
    port_Note("image: %s; %lld bytes; CRC-32 of its first %u bytes %08X", path,
              size, (unsigned)summed, (unsigned)sum);
}

/* Where the image is a cue sheet, the file of sectors its first FILE line
 * names, beside it, into `sectors`: FALSE if there is none. */
static s32 port_SectorsOfCue(const char *path, char *sectors, size_t room) {
    char text[CUE_BYTES_MAX + 1];
    const char *name;
    const char *end;
    const char *cut = path;
    const char *at;
    FILE *file;
    size_t got;

    if (!port_EndsWith(path, ".cue") || (file = fopen(path, "rb")) == NULL) {
        return FALSE;
    }
    got = fread(text, 1, CUE_BYTES_MAX, file);
    fclose(file);
    text[got] = 0;
    name = strstr(text, "FILE \"");
    end = name != NULL ? strchr(name + 6, '"') : NULL;
    if (end == NULL) {
        return FALSE;
    }
    name += 6;
    /* the cue sheet's folder, then the name */
    for (at = path; *at != 0; at++) {
        if (*at == '/' || *at == '\\') {
            cut = at + 1;
        }
    }
    snprintf(sectors, room, "%.*s%.*s", (int)(cut - path), path,
             (int)(end - name), name);
    return TRUE;
}

/* The image named, and, where that is a cue sheet, its file of sectors. */
static void port_NoteImage(const char *path) {
    char sectors[PATH_BYTES_MAX];

    port_NoteImageFile(path);
    if (port_SectorsOfCue(path, sectors, sizeof(sectors))) {
        port_NoteImageFile(sectors);
    }
}

/* What the mods made from the disc are made for (prepare.c): the
 * program's version, without its commit, and the disc's sectors by their
 * size and sum. */
static void port_DiscModsKey(const char *image, char *key, size_t room) {
    char sectors[PATH_BYTES_MAX];
    const char *version = port_GetVersionText();
    const char *commit = strstr(version, " (");
    long long size = 0;
    u32 sum = 0;
    u32 summed;

    if (!port_SectorsOfCue(image, sectors, sizeof(sectors))) {
        snprintf(sectors, sizeof(sectors), "%s", image);
    }
    port_SumImageFile(sectors, &size, &sum, &summed);
    snprintf(key, room, "%.*s disc %lld-%08X",
             commit != NULL ? (int)(commit - version) : (int)strlen(version),
             version, size, (unsigned)sum);
}

/* ---- starting again, online ---- */

/* The command line as the program was started, for starting it again. */
static s32 g_port_main_argc;
static char **g_port_main_argv;
static char *g_port_main_path;
static const char *g_port_image_path;

const char *port_GetProgramPath(void) { return g_port_main_path; }

const char *port_GetImagePath(void) { return g_port_image_path; }

const char *port_GetCommandWord(const char *option) {
    s32 i;

    for (i = 1; i + 1 < g_port_main_argc; i++) {
        if (strcmp(g_port_main_argv[i], option) == 0) {
            return g_port_main_argv[i + 1];
        }
    }
    return NULL;
}

/* What a start as a session leaves out of the command line it was
 * started with: the network's options, which it gives anew, and the
 * settings menu's for a test, which brought it here. Each with how many
 * words follow it. */
static const struct {
    const char *option;
    s32 values;
} g_port_online_drops[] = {
    {"--host", 1},           {"--join", 1},          {"--net-bind", 1},
    {"--net-peer", 1},       {"--netplay", 1},       {"--net-delay", 1},
    {"--link-side", 1},      {"--rollback-test", 1}, {"--rollback-every", 1},
    {"--rollback-quiet", 0}, {"--menu-at", 1},       {"--menu-keys", 1},
    {"--menu-shots", 1},     {"--escape-at", 1},     {"--lobby", 1},
    {"--online-at", 1},      {"--online-keys", 1},   {"--relay", 1},
    {"--room", 1},           {"--player", 1},        {"--room-password", 1},
    {"--room-key", 1},       {"--punch-to", 1},      {"--net-paths", 1},
    {"--mapped", 1},         {"--players", 1},
};

#if defined(_WIN32)
/* A word of a command line as Windows' C library reads it back: in
 * double quotes, with a quote inside taken as it is. Into `out`. */
static void port_QuoteWord(const char *word, char *out, size_t room) {
    size_t used = 0;

    if (room < 3) {
        out[0] = 0;
        return;
    }
    out[used++] = '"';
    for (; *word != 0 && used + 3 < room; word++) {
        if (*word == '"') {
            out[used++] = '\\';
        }
        out[used++] = *word;
    }
    out[used++] = '"';
    out[used] = 0;
}
#endif

/* The port mapping's options: the tests' fake router, or the real one
 * (sdl_window.h). */
static PortMapOptions g_port_map_options;
static s32 g_port_map_off;    /* --portmap off */
static s32 g_port_map_forced; /* --portmap on, whatever the settings say */

const void *port_WindowPortMapOptions(void) { return &g_port_map_options; }

/* An address no one on the internet reaches: unknown (0), private
 * (10/8, 172.16/12, 192.168/16) or the providers' shared space
 * (100.64/10). 127/8 is the tests' fake router's, and taken. */
static s32 port_IsBehindAnother(u32 address) {
    return address == 0 || (address >> 24) == 10 || (address >> 20) == 0xAC1 ||
           (address >> 16) == 0xC0A8 || (address >> 22) == (0x64400000U >> 22);
}

/* A host's port opened in its router before the session begins (portmap.c;
 * docs/NETPLAY.md, section 14), where the settings and the command line
 * let it and the host is found through every network: what it found is
 * said where the host waits, and the port is the host's first way for a
 * lobby's guest. It is closed again as the program ends. */
static void port_MapBind(const char *text) {
    static char said[PORT_MAP_WHY_BYTES + 96];
    PortNetAddress bind;
    PortMapResult map;

    if (g_port_map_off ||
        (!g_port_settings.net_portmap && !g_port_map_forced)) {
        return;
    }
    /* (a host on this PC alone has no router to ask, but a test's fake
     * one, which the options name) */
    if (text == NULL || !port_ParseNetAddress(text, &bind) || bind.port == 0 ||
        bind.is_v6 ||
        ((bind.address >> 24) == 127 && g_port_map_options.ssdp_at == NULL &&
         g_port_map_options.gateway_at == NULL)) {
        return;
    }
    port_Say(
        "online: asking the router to open UDP port %u%s%s%s%s", bind.port,
        g_port_map_options.ssdp_at != NULL ? " (UPnP's search to " : "",
        g_port_map_options.ssdp_at != NULL ? g_port_map_options.ssdp_at : "",
        g_port_map_options.gateway_at != NULL ? ", the gateway " : "",
        g_port_map_options.gateway_at != NULL ? g_port_map_options.gateway_at
                                              : "");
    if (port_MapPort(bind.port, &g_port_map_options, &map) &&
        port_IsBehindAnother(map.external_address)) {
        /* the router opened it, but it is not the internet's: another
         * router (or the provider's NAT) stands before it */
        snprintf(
            said, sizeof(said),
            "%s opened your router's port, but your router is behind "
            "another (it is at %u.%u.%u.%u): a lobby room finds the "
            "way instead",
            map.method, map.external_address >> 24,
            (map.external_address >> 16) & 0xFF,
            (map.external_address >> 8) & 0xFF, map.external_address & 0xFF);
    } else if (map.ok) {
        PortNetAddress outside;

        port_ClearNetAddress(&outside);
        outside.address = map.external_address;
        outside.port = map.external_port;
        port_SetRelayMapped(&outside);
        snprintf(said, sizeof(said),
                 "%s opened your router's port: the other player joins "
                 "%u.%u.%u.%u:%u",
                 map.method, map.external_address >> 24,
                 (map.external_address >> 16) & 0xFF,
                 (map.external_address >> 8) & 0xFF,
                 map.external_address & 0xFF, map.external_port);
    } else {
        snprintf(said, sizeof(said), "%s", map.why);
    }
    port_Say("online: %s", said);
    port_SetSessionMapped(said);
}

/* ... for a session the command line starts (--host). */
static void port_MapHostPort(s32 count, char **words) {
    s32 i;

    for (i = 1; i + 1 < count; i++) {
        if (strcmp(words[i], "--host") == 0) {
            port_MapBind(words[i + 1]);
            return;
        }
    }
}

/* ... for a session begun in the program (port_online.h), which asked
 * for it: the settings say whether, as at the start. */
static void port_MapOnlineHost(const char *bind) { port_MapBind(bind); }

/* `text` into `out` (PORT_ONLINE_TEXT_BYTES or more), "" for NULL. */
static void port_AskWord(char *out, size_t room, const char *text) {
    snprintf(out, room, "%s", text != NULL ? text : "");
}

/* The session as the online pages ask it, begun in this program at the
 * game's next boundary (port_online.h): TRUE, or FALSE with `problem`
 * where it cannot be. */
static s32 port_StartOnlineHere(
    const PortOnlineStart *start, const char **problem) {
    PortOnlineAsk ask;
    s32 role = start->role;

    *problem = NULL;
    if (!port_OnlineCanBegin()) {
        return FALSE;
    }
    memset(&ask, 0, sizeof(ask));
    ask.role = role == PORT_NET_ROLE_HOST || role == PORT_NET_ROLE_ROOM
                   ? PORT_SESSION_HOST
                   : PORT_SESSION_JOIN;
    ask.delay = (u32)start->delay;
    ask.portmap = start->portmap;
    ask.players = start->players;
    ask.pcs = start->pcs;
    port_AskWord(ask.player, sizeof(ask.player), start->player);
    port_AskWord(ask.lobby, sizeof(ask.lobby), start->lobby);
    port_AskWord(ask.key, sizeof(ask.key), start->key);
    port_AskWord(ask.password, sizeof(ask.password), start->password);
    if (ask.role == PORT_SESSION_HOST) {
        port_AskWord(ask.at, sizeof(ask.at), start->bind);
        port_AskWord(ask.room, sizeof(ask.room), start->room);
        port_AskWord(ask.punch_to, sizeof(ask.punch_to), start->punch_to);
    } else {
        port_AskWord(
            ask.at, sizeof(ask.at),
            role == PORT_NET_ROLE_ROOMS ? start->code : start->address);
        port_AskWord(ask.bind, sizeof(ask.bind), start->bind);
    }
    *problem = port_OnlineAskFor(&ask);
    if (*problem == NULL) {
        port_Say("online: a session as the %s%s%s; it begins at the game's "
                 "next boundary, in this program (no start again)",
                 ask.role == PORT_SESSION_HOST ? "host" : "guest",
                 ask.lobby[0] != 0 ? " by way of the lobby at " : " at ",
                 ask.lobby[0] != 0 ? ask.lobby : ask.at);
    }
    return TRUE;
}

/* A session asked for (sdl_window.h): begun in this program where it can
 * be (port_StartOnlineHere). Where it cannot, the fallback: the program
 * again, as a session's: the same command line but the network's options,
 * with the ones `start` says. The window and the sound card are let go
 * first. On Linux the program becomes the new one; on Windows the new one
 * is started (its process's number said, for a test to wait on) and this
 * one ends. Returns only if it could not be done. */
const char *port_StartOnline(const PortOnlineStart *start) {
    static char *words[ARGUMENTS_MAX + 32];
    static char delay_text[16];
    static char opt_host[] = "--host";
    static char opt_join[] = "--join";
    static char opt_delay[] = "--net-delay";
    static char opt_bind[] = "--net-bind";
    static char opt_lobby[] = "--lobby";
    static char opt_room[] = "--room";
    static char opt_player[] = "--player";
    static char opt_key[] = "--room-key";
    static char opt_punch[] = "--punch-to";
    static char opt_portmap[] = "--portmap";
    static char opt_players[] = "--players";
    static char word_four[] = "4";
    static char word_couch[] = "2x2";
    static char word_off[] = "off";
    static char strings[8][PATH_BYTES_MAX];
    s32 role = start->role;
    s32 is_host = role == PORT_NET_ROLE_HOST || role == PORT_NET_ROLE_ROOM;
    s32 count = 0;
    s32 kept = 0;
    s32 i;
    const char *problem;

    if (port_StartOnlineHere(start, &problem)) {
        return problem;
    }
    port_Say("online: this program cannot begin a session as it runs (no "
             "state of the game's start was kept): it starts again as the "
             "session's instead");
    if (g_port_main_path == NULL) {
        return "The program cannot be started again.";
    }
    words[count++] = g_port_main_path;
    for (i = 1; i < g_port_main_argc && count < ARGUMENTS_MAX; i++) {
        size_t drop;
        s32 skip = -1;

        for (drop = 0; drop < sizeof(g_port_online_drops) /
                                  sizeof(g_port_online_drops[0]);
             drop++) {
            if (strcmp(g_port_main_argv[i], g_port_online_drops[drop].option) ==
                0) {
                skip = g_port_online_drops[drop].values;
            }
        }
        if (skip >= 0) {
            i += skip;
            continue;
        }
        words[count++] = g_port_main_argv[i];
    }
#define KEEP(text)                                                             \
    (snprintf(strings[kept], PATH_BYTES_MAX, "%s", (text)), strings[kept++])
    snprintf(delay_text, sizeof(delay_text), "%d", start->delay);
    if (start->lobby != NULL) {
        words[count++] = opt_lobby;
        words[count++] = KEEP(start->lobby);
        if (start->player != NULL && start->player[0] != 0) {
            words[count++] = opt_player;
            words[count++] = KEEP(start->player);
        }
        if (start->key != NULL) {
            words[count++] = opt_key;
            words[count++] = KEEP(start->key);
        }
    }
    if (is_host) {
        words[count++] = opt_host;
        words[count++] = KEEP(start->bind);
        words[count++] = opt_delay;
        words[count++] = delay_text;
        if (role == PORT_NET_ROLE_ROOM) {
            words[count++] = opt_room;
            words[count++] = KEEP(start->room);
        }
        if (start->punch_to != NULL) {
            words[count++] = opt_punch;
            words[count++] = KEEP(start->punch_to);
        }
        if (!start->portmap) {
            words[count++] = opt_portmap;
            words[count++] = word_off;
        }
        if (start->players == PORT_RELAY_FOUR) {
            words[count++] = opt_players;
            words[count++] =
                start->pcs == PORT_RELAY_FOUR ? word_four : word_couch;
        }
    } else {
        words[count++] = opt_join;
        words[count++] =
            KEEP(role == PORT_NET_ROLE_ROOMS ? start->code : start->address);
        words[count++] = opt_bind;
        words[count++] = KEEP(start->bind);
    }
#undef KEEP
    words[count] = NULL;
    port_Say("online: the program starts again as the %s of a session%s%s",
             is_host ? "host" : "guest",
             start->lobby != NULL ? " by way of the lobby at " : " at ",
             start->lobby != NULL ? start->lobby
             : is_host            ? start->bind
                                  : start->address);
#if defined(_WIN32)
    {
        static char quoted[ARGUMENTS_MAX + 32][PATH_BYTES_MAX];
        static char *quoted_words[ARGUMENTS_MAX + 32];
        intptr_t process;

        for (i = 0; i < count; i++) {
            port_QuoteWord(words[i], quoted[i], sizeof(quoted[i]));
            quoted_words[i] = quoted[i];
        }
        quoted_words[count] = NULL;
        process = _spawnv(
            _P_NOWAIT, g_port_main_path, (const char * const *)quoted_words);
        if (process == -1) {
            port_Say("online: the program cannot be started again");
            return "The program cannot be started again.";
        }
        port_Say("online: started again as process %lu",
                 (unsigned long)GetProcessId((HANDLE)process));
        port_Quit(PORT_EXIT_OK);
    }
#else
    port_EndSession(PORT_EXIT_OK);
    SDL_Quit();
    execv(g_port_main_path, words);
    fprintf(stderr, "rebushido: online: %s cannot be started again\n",
            g_port_main_path);
    exit(PORT_EXIT_FAILED);
#endif
}

/* ---- the program ---- */

/* The program's own path, which the port reads its names beside
 * (rebushido.sym, or rebushido.exe.sym): as it was started, or on
 * Windows as the system has it, whatever was typed. */
static char *port_OwnPath(char *typed) {
#if defined(_WIN32)
    static char path[PATH_BYTES_MAX];
    DWORD length = GetModuleFileNameA(NULL, path, sizeof(path));

    if (length != 0 && length < sizeof(path)) {
        return path;
    }
#endif
    return typed;
}

static s32 port_ChooseWord(
    const char *option, const char *word, const char *first, const char *second,
    const char *third) {
    if (strcmp(word, first) == 0) {
        return 0;
    }
    if (strcmp(word, second) == 0) {
        return 1;
    }
    if (third != NULL && strcmp(word, third) == 0) {
        return 2;
    }
    port_Complain(option, word);
    return 0;
}

int main(int argc, char **argv) {
    static char *passed[ARGUMENTS_MAX + 8];
    static char mods_option[] = "--mods";
    static char mods_off[] = "off";
    static char image_option[] = "--image";
    static char frames_option[] = "--frames";
    static char frames_endless[] = "2000000000";
    PortWindowOptions *options = &g_port_window_options;
    const char *config = NULL;
    char *base;
    const char *beside;
    s32 count = 0;
    s32 has_image = 0;
    s32 has_end = 0;
    s32 has_mods = 0;    /* the command line says where they are */
    s32 fullscreen = -1; /* -1: as the settings say */
    s32 no_vsync = 0;
    s32 scale = -1;
    s32 filter = -1;
    s32 no_fps = 0;
    s32 no_log = 0;
    const char *log = NULL;
    const char *image = NULL;
    /* the remastered picture's, where the command line says: -1 for what
     * the settings file or the layout built in says */
    s32 graphics = -1;
    s32 resolution = -1;
    s32 distance = -1;
    s32 supersample = -1;
    s32 aspect_w = -1;
    s32 aspect_h = -1;
    s32 frame_rate = PORT_FRAME_RATE_ORIGINAL;
    s32 has_frame_rate = 0;
    s32 run_ahead = -1;
    s32 stage_mesh = -1;
    s32 shadows = -1;
    PortRemasterView view;
    s32 is_session = 0; /* started into a session of online play */
    /* a replay's film made (--export), or one read back for a test */
    const char *export_path = NULL;
    const char *film_read = NULL;
    const char *film_folder = NULL;
    const char *film_at = NULL;
    s32 code;
    s32 i;

    /* the step that puts a downloaded update in place, which the program
     * starts as a copy of itself and which is nothing else (update.h) */
    code = port_UpdateApplyMain();
    if (code >= 0) {
        return code;
    }
    /* the console's memory at its own addresses, before SDL, the card's
     * driver or anything of the C library's can take one of them
     * (port_host.h) */
    port_MapFixedMemory();
    base = SDL_GetBasePath();
    beside = base != NULL ? base : "";
    SDL_SetMainReady();
    options->tap_every = 1;
    g_port_main_argc = argc;
    g_port_main_argv = argv;
    if (argc > 0) {
        passed[count++] = port_OwnPath(argv[0]);
        g_port_main_path = passed[0];
    }
    for (i = 1; i < argc; i++) {
        const char *word = argv[i];
        s32 has_value = i + 1 < argc;

        if (strcmp(word, "--help") == 0 || strcmp(word, "-h") == 0) {
            fputs(g_port_window_usage, stdout);
            fputs(port_GetDefaultSettings(), stdout);
            /* the lines the layout built in brings */
            fputs("\n", stdout);
            fputs(g_port_layouts[0].text, stdout);
            fputs(g_port_window_usage_end, stdout);
            return PORT_EXIT_OK;
        } else if (strcmp(word, "--fullscreen") == 0) {
            fullscreen = 1;
        } else if (strcmp(word, "--windowed") == 0) {
            fullscreen = 0;
        } else if (strcmp(word, "--no-vsync") == 0) {
            no_vsync = 1;
        } else if (strcmp(word, "--no-sound") == 0) {
            options->no_sound = 1;
        } else if (strcmp(word, "--hidden") == 0) {
            options->is_hidden = 1;
        } else if (strcmp(word, "--prepare-only") == 0) {
            options->prepare_only = 1;
        } else if (strcmp(word, "--update-url") == 0 && has_value) {
            options->update_url = argv[++i];
        } else if (strcmp(word, "--update-key") == 0 && has_value) {
            options->update_key = argv[++i];
        } else if (strcmp(word, "--update-started") == 0) {
            options->update_started = 1;
        } else if (strcmp(word, "--update-rolled-back") == 0 && has_value) {
            options->update_rolled_back = argv[++i];
        } else if (strcmp(word, "--no-fps") == 0) {
            no_fps = 1;
        } else if (strcmp(word, "--no-log") == 0) {
            no_log = 1;
        } else if (strcmp(word, "--log") == 0 && has_value) {
            log = argv[++i];
        } else if (strcmp(word, "--toggle-at") == 0 && has_value) {
            options->toggle_at = atoi(argv[++i]);
        } else if (strcmp(word, "--shot-at") == 0 && has_value) {
            const char *at = argv[++i];
            const char *comma = strchr(at, ',');

            options->shot_at = atoi(at);
            options->shot_picture = comma != NULL ? atoi(comma + 1) : -1;
        } else if (strcmp(word, "--window") == 0 && has_value) {
            const char *size = argv[++i];
            const char *by = strchr(size, 'x');

            options->window_w = atoi(size);
            options->window_h = by != NULL ? atoi(by + 1) : 0;
            if (options->window_w <= 0 || options->window_h <= 0) {
                port_Complain(
                    "--window takes a size like 1280x720, not ", size);
            }
            /* a window of a size asked for is a window: a test's
             * picture is that size whatever the settings say */
            fullscreen = 0;
        } else if (strcmp(word, "--report-at") == 0 && has_value) {
            options->report_at = atoi(argv[++i]);
        } else if (strcmp(word, "--stall") == 0 && has_value) {
            /* MS[,EVERY[,FROM]]: a stall of MS at every EVERYth blank
             * from blank FROM (600 and 1200 where not said) */
            const char *how = argv[++i];

            options->stall_ms = atoi(how);
            options->stall_every = 600;
            options->stall_at = 1200;
            if (strchr(how, ',') != NULL) {
                options->stall_every = atoi(strchr(how, ',') + 1);
                how = strchr(how, ',') + 1;
                if (strchr(how, ',') != NULL) {
                    options->stall_at = atoi(strchr(how, ',') + 1);
                }
            }
            if (options->stall_ms <= 0 || options->stall_every <= 0) {
                port_Complain("--stall takes MS[,EVERY[,FROM]], not ", argv[i]);
            }
        } else if (strcmp(word, "--fault-at") == 0 && has_value) {
            options->fault_at = atoi(argv[++i]);
        } else if (strcmp(word, "--fault-in-sound") == 0 && has_value) {
            options->fault_in_sound = atoi(argv[++i]);
        } else if (strcmp(word, "--export") == 0 && has_value) {
            export_path = argv[++i];
        } else if (strcmp(word, "--film-read") == 0 && i + 2 < argc) {
            film_read = argv[++i];
            film_folder = argv[++i];
        } else if (strcmp(word, "--film-at") == 0 && has_value) {
            film_at = argv[++i];
        } else if (strcmp(word, "--fast") == 0) {
            options->is_fast = 1;
        } else if (strcmp(word, "--quick-loading") == 0 && has_value) {
            options->quick_loading =
                PORT_QUICK_OFF +
                port_ChooseWord("--quick-loading takes off or on, not ",
                                argv[++i], "off", "on", NULL);
        } else if (strcmp(word, "--portmap") == 0 && has_value) {
            g_port_map_off = port_ChooseWord("--portmap takes on or off, not ",
                                             argv[++i], "on", "off", NULL);
            g_port_map_forced = !g_port_map_off;
        } else if (strcmp(word, "--portmap-ssdp") == 0 && has_value) {
            g_port_map_options.ssdp_at = argv[++i];
        } else if (strcmp(word, "--portmap-gateway") == 0 && has_value) {
            g_port_map_options.gateway_at = argv[++i];
        } else if (strcmp(word, "--config") == 0 && has_value) {
            config = argv[++i];
        } else if (strcmp(word, "--scale") == 0 && has_value) {
            scale = port_ChooseWord("--scale takes fit, integer or stretch, "
                                    "not ",
                                    argv[++i], "fit", "integer", "stretch");
        } else if (strcmp(word, "--filter") == 0 && has_value) {
            filter = port_ChooseWord("--filter takes nearest or linear, not ",
                                     argv[++i], "nearest", "linear", NULL);
        } else if (strcmp(word, "--inject") == 0 && has_value) {
            options->inject =
                1 + port_ChooseWord("--inject takes keys or pads, not ",
                                    argv[++i], "keys", "pads", NULL);
        } else if (strcmp(word, "--graphics") == 0 && has_value) {
            graphics =
                port_ChooseWord("--graphics takes original or remastered, not ",
                                argv[++i], "original", "remastered", NULL);
        } else if (strcmp(word, "--resolution") == 0 && has_value) {
            resolution = atoi(argv[++i]);
            if (resolution < 0 || resolution > PORT_REMASTER_SCALE_MAX) {
                port_Complain("--resolution takes 0 (the window's own size) "
                              "or a number up to 8, not ",
                              argv[i]);
            }
        } else if (strcmp(word, "--draw-distance") == 0 && has_value) {
            distance = port_ReadRemasterDistance(argv[++i]);
            if (distance < 0) {
                port_Complain(
                    "--draw-distance takes original, far or max, not ",
                    argv[i]);
            }
        } else if (strcmp(word, "--antialiasing") == 0 && has_value) {
            const char *how = argv[++i];

            supersample = strcmp(how, "off") == 0 ? 1 : atoi(how);
            if (supersample < 1 || supersample > PORT_SUPERSAMPLE_MAX) {
                port_Complain("--antialiasing takes off, 2 or 4, not ", how);
            }
        } else if (strcmp(word, "--aspect") == 0 && has_value) {
            const char *shape = argv[++i];
            const char *colon = strchr(shape, ':');

            if (colon == NULL || atoi(shape) <= 0 || atoi(colon + 1) <= 0) {
                port_Complain(
                    "--aspect takes W:H, as 4:3 or 16:9, not ", shape);
            }
            aspect_w = atoi(shape);
            aspect_h = atoi(colon + 1);
        } else if (strcmp(word, "--stage-mesh") == 0 && has_value) {
            const char *how = argv[++i];

            if (strcmp(how, "on") == 0) {
                stage_mesh = 1;
            } else if (strcmp(how, "off") == 0) {
                stage_mesh = 0;
            } else {
                port_Complain("--stage-mesh takes on or off, not ", how);
            }
        } else if (strcmp(word, "--shadows") == 0 && has_value) {
            const char *how = argv[++i];

            shadows = port_ReadRemasterShadows(how);
            if (shadows < 0) {
                port_Complain("--shadows takes original, soft, cast, cast:N, "
                              "map or 1 to 100, not ",
                              how);
            }
        } else if (strcmp(word, "--frame-rate") == 0 && has_value) {
            const char *how = argv[++i];

            if (strcmp(how, "original") == 0) {
                frame_rate = PORT_FRAME_RATE_ORIGINAL;
            } else if (strcmp(how, "display") == 0) {
                frame_rate = PORT_FRAME_RATE_DISPLAY;
            } else {
                frame_rate = atoi(how);
                if (frame_rate < 1 || frame_rate > PORT_FRAME_RATE_MAX) {
                    port_Complain("--frame-rate takes original, display or a "
                                  "number of pictures a second, not ",
                                  how);
                }
            }
            has_frame_rate = 1;
        } else if (strcmp(word, "--run-ahead") == 0 && has_value) {
            const char *how = argv[++i];

            if (how[0] < '0' || how[0] > '0' + PORT_RUN_AHEAD_MAX ||
                how[1] != 0) {
                port_Complain("--run-ahead takes 0, 1 or 2, not ", how);
            }
            run_ahead = how[0] - '0';
        } else if (strcmp(word, "--tap-frames") == 0 && has_value) {
            options->tap_frames = argv[++i];
        } else if (strcmp(word, "--tap-window") == 0 && has_value) {
            options->tap_window = argv[++i];
        } else if (strcmp(word, "--tap-every") == 0 && has_value) {
            options->tap_every = atoi(argv[++i]);
            if (options->tap_every <= 0) {
                port_Complain("--tap-every takes a number, not ", argv[i]);
            }
        } else if (strcmp(word, "--tap-audio") == 0 && has_value) {
            options->tap_audio = argv[++i];
        } else if (strcmp(word, "--profile") == 0 && has_value) {
            options->profile = argv[++i];
        } else if (strcmp(word, "--sample") == 0 && has_value) {
            options->sample = argv[++i];
        } else if (strcmp(word, "--bench-stress") == 0 && has_value) {
            options->bench_stress = argv[++i];
        } else if (strcmp(word, "--display-rate") == 0 && has_value) {
            options->display_rate = atoi(argv[++i]);
            if (options->display_rate <= 0) {
                port_Complain("--display-rate takes a number, not ", argv[i]);
            }
        } else if (strcmp(word, "--menu-at") == 0 && has_value) {
            options->menu_at = argv[++i];
        } else if (strcmp(word, "--menu-keys") == 0 && has_value) {
            options->menu_keys = argv[++i];
        } else if (strcmp(word, "--menu-shots") == 0 && has_value) {
            options->menu_shots = argv[++i];
        } else if (strcmp(word, "--online-at") == 0 && has_value) {
            options->online_at = atoi(argv[++i]);
        } else if (strcmp(word, "--online-keys") == 0 && has_value) {
            options->online_keys = argv[++i];
        } else if (strcmp(word, "--escape-at") == 0 && has_value) {
            options->escape_at = argv[++i];
        } else {
            /* the port's own, which it reads itself */
            if (strcmp(word, "--image") == 0) {
                has_image = 1;
                image = has_value ? argv[i + 1] : NULL;
            }
            if (strcmp(word, "--mods") == 0) {
                has_mods = 1;
            }
            if (strcmp(word, "--frames") == 0 || strcmp(word, "--plan") == 0) {
                has_end = 1;
            }
            if (strcmp(word, "--host") == 0 || strcmp(word, "--join") == 0 ||
                strcmp(word, "--lobby") == 0) {
                is_session = 1;
            }
            if (count == ARGUMENTS_MAX) {
                port_Complain("too many words on the command line", "");
            }
            passed[count++] = argv[i];
        }
    }
    if (film_read != NULL) {
        /* a film read back, for a test (film.h), and nothing else */
        char why[PORT_FILM_WHY_BYTES];

        if (!port_FilmRead(film_read, film_folder,
                           film_at != NULL ? film_at : "0", why, sizeof(why))) {
            fprintf(stderr, "rebushido: --film-read: %s\n", why);
            return PORT_EXIT_FAILED;
        }
        return PORT_EXIT_OK;
    }
    if (export_path != NULL) {
        /* a replay's film (sdl_export.c): a window never shown, of the
         * film's size, as fast as the PC goes, with the game's own
         * picture a blank and nothing over it */
        char why[PORT_FILM_WHY_BYTES];

        if (port_GetCommandWord("--replay") == NULL) {
            port_Complain("--export makes a film of a replay: --replay FILE "
                          "says which",
                          "");
        }
        if (options->window_w == 0) {
            options->window_w = PORT_FILM_WIDTH;
            options->window_h = PORT_FILM_HEIGHT;
        }
        if (!port_ExportSetUp(export_path, options->window_w, options->window_h,
                              why, sizeof(why))) {
            port_Complain("--export: ", why);
        }
        options->is_fast = 1;
        options->is_hidden = 1;
        no_fps = 1;
        no_vsync = 1;
        fullscreen = 0;
        run_ahead = 0;
        frame_rate = PORT_FRAME_RATE_ORIGINAL;
        has_frame_rate = 1;
    }
    if (!no_log) {
        port_OpenLog(log, beside);
    }
    port_LoadSettings(config, beside);
    {
        /* the ranked identity beside the settings file, as the cards are
         * beside the program (--identity names another) */
        static char identity[PATH_BYTES_MAX];
        const char *settings = g_port_settings_written;
        const char *cut = settings;
        const char *at;

        for (at = settings; *at != 0; at++) {
            if (*at == '/' || *at == '\\') {
                cut = at + 1;
            }
        }
        snprintf(identity, sizeof(identity), "%.*s%s", (int)(cut - settings),
                 settings, IDENTITY_FILE);
        port_RankedSetIdentity(identity);
    }
    /* the updater (update.h): never in a session of online play or a
     * test's run, unless the test names its server; a downloaded update
     * put in place before anything else is done */
    if ((options->update_url != NULL || options->update_key != NULL) &&
        strstr(port_GetVersionText(), "-dev") == NULL) {
        port_Complain("--update-url and --update-key are a trial build's, "
                      "for its tests: not a release's",
                      "");
    }
    port_UpdateSetUp(
        beside, options->update_url, options->update_key,
        !is_session && !options->prepare_only && export_path == NULL &&
            (!has_end || options->update_url != NULL));
    if (options->update_rolled_back != NULL) {
        port_UpdateRolledBack(options->update_rolled_back);
    }
    if (!options->update_started && port_UpdateAtStart()) {
        port_EndSession(PORT_EXIT_OK);
        return PORT_EXIT_OK;
    }
    if (no_fps) {
        g_port_settings.fps = 0;
    }
    if (fullscreen >= 0) {
        g_port_settings.is_fullscreen = fullscreen;
    }
    if (no_vsync) {
        g_port_settings.vsync = 0;
    }
    if (scale >= 0) {
        g_port_settings.scale = scale;
    }
    if (filter >= 0) {
        g_port_settings.is_linear = filter;
    }
    /* the remastered picture: the command line over the settings file,
     * and then the view a run keeps, which decides the game's primitive
     * buffers before the game starts (port_remaster.h) */
    if (graphics >= 0) {
        g_port_settings.graphics =
            graphics == 0 ? PORT_GRAPHICS_ORIGINAL : PORT_GRAPHICS_REMASTERED;
    }
    if (resolution >= 0) {
        g_port_settings.resolution = resolution;
    }
    if (distance >= 0) {
        g_port_settings.distance = distance;
    }
    if (supersample >= 0) {
        g_port_settings.supersample = supersample;
    }
    if (has_frame_rate) {
        g_port_settings.frame_rate = frame_rate;
    }
    if (run_ahead >= 0) {
        g_port_settings.run_ahead = run_ahead;
    } else if (options->is_fast || options->tap_frames != NULL ||
               options->tap_window != NULL || options->shot_at > 0) {
        /* a test that holds the window's pictures to the blanks they
         * were drawn at, unless it asks for frames run ahead */
        g_port_settings.run_ahead = 0;
    }
    if (stage_mesh >= 0) {
        g_port_settings.stage_mesh = stage_mesh;
    }
    if (shadows >= 0) {
        g_port_settings.shadows = shadows;
    }
    if (aspect_w > 0 && aspect_h > 0) {
        g_port_settings.aspect_w = aspect_w;
        g_port_settings.aspect_h = aspect_h;
    }
    port_SetGraphics(g_port_settings.graphics);
    view.distance = g_port_settings.distance;
    view.supersample = g_port_settings.supersample;
    view.aspect_w = g_port_settings.aspect_w;
    view.aspect_h = g_port_settings.aspect_h;
    port_RemasterSetView(&view);
    port_RemasterGetView(&view);
    g_port_settings.distance = view.distance;
    g_port_settings.supersample = view.supersample;
    g_port_settings.aspect_w = view.aspect_w;
    g_port_settings.aspect_h = view.aspect_h;
    if (!has_image) {
        image = port_FindImage(beside);
        passed[count++] = image_option;
        passed[count++] = (char *)image;
    }
    if (!has_end) {
        /* A player's session ends when the player ends it. The program
         * without a window stops by itself after a minute of blanks
         * unless it is told how many (src/port/plan.c), which is right
         * for a test and ended every game played here at sixty seconds;
         * this many blanks is a year. */
        passed[count++] = frames_option;
        passed[count++] = frames_endless;
    } else if (options->quick_loading == PORT_QUICK_SETTINGS) {
        /* a test's run keeps the console's pace through the loads, as
         * every test's picture and sound were taken with it */
        options->quick_loading = PORT_QUICK_OFF;
    }
    if (!has_mods && !g_port_settings.mods) {
        /* the settings file turned them off (docs/MODS.md) */
        passed[count++] = mods_option;
        passed[count++] = mods_off;
    }
    if (!g_port_settings.english) {
        /* the English top menu the program ships is a mod of its own,
         * mods/english/, which the settings leave out (docs/MODS.md) */
        port_ModsLeaveOut("english");
    }
    if (!g_port_settings.ui) {
        /* and the menus' lettering drawn again, mods/ui/ */
        port_ModsLeaveOut("ui");
    }
    /* a mod's music, or the game's own (docs/MODS.md, "Music"); the
     * command line's --music-mods comes after and has the last word */
    port_MusicSetOn(g_port_settings.music_mods);
    /* the kind of stage the stages start as (port_stage_choice.h) */
    port_SetStageKindDefault(g_port_settings.stage_kind);
    /* ... and the time of day they start at */
    port_SetStageTimeDefault(g_port_settings.time_of_day);
    /* the two gameplay switches (port_play.h); --unlock-all and
     * --stage-select on the command line turn them on */
    port_SetUnlockAll(g_port_settings.unlock_all);
    port_SetStageSelectEverywhere(g_port_settings.stage_select_everywhere);
    /* a fight's music (port_fight_music.h); --fight-music has the last
     * word */
    port_SetFightMusic(g_port_settings.fight_music);
    /* the match rule (port_match.h); --match has the last word */
    port_SetMatchDefault(
        g_port_settings.match_rule, g_port_settings.match_count);
    /* two on two (port_duo.h); --teams has the last word */
    port_SetTeamsDefault(g_port_settings.teams);
    /* training's dummy, slot and slow motion (port_training.h); the
     * command line's --training-... have the last word */
    port_WindowTrainingSwitches();
    /* ReBushido's top menu (port_top_menu.h), in a player's run: a test's
     * (--plan, --frames) presses through the original's menus, and has
     * them unless its --menu, which comes after, says otherwise */
    port_SetTopMenuSwitch(!has_end && g_port_settings.top_menu == 0);
    /* every fight kept in replays/ beside the program, in a player's run;
     * a test's keeps none unless its --replays, which comes after, says
     * where (docs/PLAYING.md, "Replays") */
    port_StartReplayWindow(beside, has_end || export_path != NULL);
    port_WindowGameSwitches();
    /* the CPU (port_cpu.h): the game's own in a test's run, unless its
     * --cpu, which comes after, says otherwise */
    if (has_end) {
        port_SetCpuLevels(PORT_CPU_ORIGINAL, PORT_CPU_ORIGINAL);
    }
    passed[count] = NULL;
    {
        char line[2 * PATH_BYTES_MAX];
        size_t used = 0;

        line[0] = 0;
        for (i = 1; i < argc && used < sizeof(line); i++) {
            used += (size_t)snprintf(
                line + used, sizeof(line) - used, " %s", argv[i]);
        }
        port_Note("command line:%s", line);
    }
    if (image != NULL) {
        g_port_image_path = image;
        port_NoteImage(image);
    }
    if (options->update_started) {
        /* started by the step that installed an update: it has started,
         * its settings and the disc read, so it is kept */
        port_UpdateSayStarted();
    }
    /* the mods made from the player's disc, the first time it is played
     * (prepare.c): in a player's session with the mods beside the
     * program, or for --prepare-only, which makes them and ends */
    if (image != NULL &&
        (options->prepare_only ||
         (!has_end && !has_mods && !options->is_fast && g_port_settings.mods &&
          g_port_settings.disc_mods))) {
        char key[PATH_BYTES_MAX];
        s32 closed = 0;
        s32 failed;

        port_DiscModsKey(image, key, sizeof(key));
        port_Note("prepare: the disc's mods are made for \"%s\"", key);
        failed = port_PrepareMods(beside, image, key, options->is_hidden,
                                  options->tap_window, &closed);
        if (options->prepare_only || closed) {
            code = options->prepare_only && failed ? PORT_EXIT_FAILED
                                                   : PORT_EXIT_OK;
            port_EndSession(code);
            return code;
        }
    }

    port_Say("ReBushido in a window. Escape pauses and goes back; closing "
             "the window ends it; F11 or Alt+Enter fills the screen; F2 "
             "changes picture; F3 is the counter in the corner; F12 keeps a "
             "picture of the window in screenshots/.");
    port_Say("graphics: %s to start with; the remastered picture is drawn "
             "%s, draw distance %s, antialiasing %d, aspect %d:%d.",
             g_port_settings.graphics == PORT_GRAPHICS_ORIGINAL ? "original"
             : g_port_settings.graphics == PORT_GRAPHICS_REMASTERED
                 ? "remastered"
                 : "whichever the options page says",
             g_port_settings.resolution == PORT_RESOLUTION_WINDOW
                 ? "at the window's own size"
                 : "at the resolution asked for",
             port_NameRemasterDistance(view.distance), view.supersample,
             view.aspect_w, view.aspect_h);
    port_Say("stage_mesh: the stage's ground is drawn %s.",
             g_port_settings.stage_mesh
                 ? "from one static mesh of its own"
                 : "from the game's own tile primitives");
    if (g_port_settings.shadows & PORT_SHADOWS_MAP) {
        port_Say("shadows: the sun's own shadow maps, on the ground, the "
                 "stage's pieces and the fighters' models.");
    } else if (g_port_settings.shadows & PORT_SHADOWS_CAST) {
        port_Say("shadows: cast from the fighters' own bodies, taking %d "
                 "hundredths of the ground they fall on.",
                 g_port_settings.shadows & PORT_SHADOWS_STRENGTH_MASK);
    } else if (g_port_settings.shadows > 0) {
        port_Say("shadows: soft, taking %d hundredths of the ground they "
                 "fall on.",
                 g_port_settings.shadows);
    } else {
        port_Say("shadows: the game's own black silhouettes.");
    }
    port_Say("smooth_light: the game's own lights worked out %s.",
             g_port_settings.smooth_light
                 ? "at each pixel of the ground and the fighters' models"
                 : "once a face and once a vertex, as the game does");
    port_Say("effects: %s.", port_NameRemasterEffects(g_port_settings.effects));
    port_Say("memory cards: card1.mcd and card2.mcd beside the program "
             "unless --card1, --card2 or --no-cards says otherwise.");
    port_Say("settings: %s", g_port_settings_path != NULL
                                 ? g_port_settings_path
                                 : "the ones built in (--help shows them)");
    port_DescribeControls();
    port_StartWindow();
    /* whether there is a newer release, in the background */
    port_UpdateBegin();
    port_MapHostPort(count, passed);
    /* a session begun in the program asks the router as it begins, and
     * gives the port back as it ends (port_online.h) */
    g_port_online_map_hook = port_MapOnlineHost;
    g_port_online_unmap_hook = port_UnmapPort;
    /* the game never comes back; what does is a start that failed */
    code = port_Main(count, passed);
    port_EndSession(code);
    return code;
}
