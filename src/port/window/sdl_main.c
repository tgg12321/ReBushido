/* The native port with a window (docs/PORT.md, "The window"): where the
 * program starts in that build. It takes the window's own options off
 * the command line, reads the settings file, finds the disc image if
 * nothing names one, says how the pads are pressed, hangs the window on
 * the port (sdl_window.c) and then is the port's program as it is
 * without a window (src/port/main.c):
 *
 *   rebushido [--image FILE] [--config FILE] [--fullscreen]
 *             [--scale fit|integer|stretch] [--filter nearest|linear]
 *             [--no-vsync] [--no-sound]
 *             [the options of the build without a window]
 *
 * and for the tests, which show and sound nothing:
 *
 *             [--fast] [--inject keys|pads] [--tap-frames FOLDER]
 *             [--tap-every N] [--tap-audio FILE] */
#define SDL_MAIN_HANDLED
#include <SDL.h>
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
#include <windows.h>

#include <commdlg.h>
#endif

#include "port_host.h"
#include "shared.h"
#include "sdl_window.h"

#define PATH_BYTES_MAX 1024
#define ARGUMENTS_MAX 64
#define SETTINGS_FILE "rebushido.cfg"
#define SETTINGS_BYTES_MAX 0x10000

static const char g_port_window_usage[] =
    "rebushido: ReBushido's native port, in a window (docs/PORT.md)\n"
    "\n"
    "  rebushido [--image FILE] [--config FILE] [--fullscreen]\n"
    "            [--scale fit|integer|stretch] [--filter nearest|linear]\n"
    "            [--no-vsync] [--no-sound]\n"
    "\n"
    "  --image FILE   your own image of the game's disc: the .cue, or the\n"
    "                 .bin of raw sectors it names. Without it the first\n"
    "                 .cue, or else .bin, beside the program is taken\n"
    "  --config FILE  the settings file; rebushido.cfg beside the program\n"
    "                 if nothing says, and the settings built in if there\n"
    "                 is none\n"
    "  --fullscreen   start filling the screen\n"
    "  --scale MODE   fit: four by three, as large as the window holds;\n"
    "                 integer: 320 by 240 a whole number of times;\n"
    "                 stretch: all of the window\n"
    "  --filter KIND  nearest: each pixel a block; linear: smoothed\n"
    "  --no-vsync     do not wait for the display\n"
    "  --no-sound     open no sound card\n"
    "\n"
    "In the window: Escape, or closing it, ends the program. F11 or\n"
    "Alt+Enter fills the screen and comes back.\n"
    "\n"
    "There is no memory card yet: the game finds none, so settings and\n"
    "what a player has unlocked are not kept from one run to the next.\n"
    "\n"
    "The settings file, with everything as it is when there is none. The\n"
    "first controller plugged in is player one's, the second player\n"
    "two's; a line `p1.cross = key Space` takes the built-in key of that\n"
    "button away, more lines add more keys, `p1.cross = none` leaves it\n"
    "with nothing. A key's name is SDL's (Up, Space, Return, Left Shift,\n"
    "Keypad 4), a controller's is SDL's too (a, b, x, y, back, start,\n"
    "leftshoulder, dpup; +leftx, -lefty, +righttrigger for a stick or a\n"
    "trigger pushed one way).\n"
    "\n";

static const char g_port_window_usage_end[] =
    "\n"
    "The options of the build without a window work here too (--frames,\n"
    "--trace, --plan, --dump, --out, --shots, --every, --vram, --wav,\n"
    "--spu), and for the tests, which show and sound nothing: --fast\n"
    "(never wait), --inject keys|pads (a plan's presses as key events,\n"
    "or as two controllers SDL makes up), --tap-frames FOLDER and\n"
    "--tap-every N (each Nth picture as SDL is given it), --tap-audio FILE\n"
    "(the samples as they are queued).\n";

static void port_Complain(const char *problem, const char *word) {
    fprintf(stderr, "rebushido: %s%s\nrebushido: --help says what it takes\n",
            problem, word);
    exit(PORT_EXIT_USAGE);
}

/* ---- the settings file ---- */

static const char *g_port_settings_path;

static void port_ComplainOfLine(s32 line, const char *why, const char *text) {
    fprintf(stderr, "rebushido: %s line %d is not used (%s): %s\n",
            g_port_settings_path, line, why, text);
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
        fprintf(
            stderr, "rebushido: the disc image beside the program: %s\n", path);
        return path;
    }
    if (port_AskForImage(path, sizeof(path))) {
        return path;
    }
    fprintf(stderr,
            "rebushido: no disc image. The game is read from your own "
            "image of the\n"
            "rebushido: Bushido Blade 2 disc (SLUS-00663), which is not part "
            "of this program.\n"
            "rebushido: Put its .cue and .bin beside the program (%s),\n"
            "rebushido: or name it: rebushido --image \"Bushido Blade 2 "
            "(USA).cue\"\n",
            beside[0] != 0 ? beside : "this folder");
    exit(PORT_EXIT_USAGE);
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
    static char *passed[ARGUMENTS_MAX + 4];
    static char image_option[] = "--image";
    PortWindowOptions *options = &g_port_window_options;
    const char *config = NULL;
    char *base = SDL_GetBasePath();
    const char *beside = base != NULL ? base : "";
    s32 count = 0;
    s32 has_image = 0;
    s32 fullscreen = 0;
    s32 no_vsync = 0;
    s32 scale = -1;
    s32 filter = -1;
    s32 i;

    SDL_SetMainReady();
    options->tap_every = 1;
    if (argc > 0) {
        passed[count++] = port_OwnPath(argv[0]);
    }
    for (i = 1; i < argc; i++) {
        const char *word = argv[i];
        s32 has_value = i + 1 < argc;

        if (strcmp(word, "--help") == 0 || strcmp(word, "-h") == 0) {
            fputs(g_port_window_usage, stdout);
            fputs(g_port_default_settings, stdout);
            fputs(g_port_window_usage_end, stdout);
            return PORT_EXIT_OK;
        } else if (strcmp(word, "--fullscreen") == 0) {
            fullscreen = 1;
        } else if (strcmp(word, "--no-vsync") == 0) {
            no_vsync = 1;
        } else if (strcmp(word, "--no-sound") == 0) {
            options->no_sound = 1;
        } else if (strcmp(word, "--fast") == 0) {
            options->is_fast = 1;
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
        } else if (strcmp(word, "--tap-frames") == 0 && has_value) {
            options->tap_frames = argv[++i];
        } else if (strcmp(word, "--tap-every") == 0 && has_value) {
            options->tap_every = atoi(argv[++i]);
            if (options->tap_every <= 0) {
                port_Complain("--tap-every takes a number, not ", argv[i]);
            }
        } else if (strcmp(word, "--tap-audio") == 0 && has_value) {
            options->tap_audio = argv[++i];
        } else {
            /* the port's own, which it reads itself */
            if (strcmp(word, "--image") == 0) {
                has_image = 1;
            }
            if (count == ARGUMENTS_MAX) {
                port_Complain("too many words on the command line", "");
            }
            passed[count++] = argv[i];
        }
    }
    port_LoadSettings(config, beside);
    if (fullscreen) {
        g_port_settings.is_fullscreen = 1;
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
    if (!has_image) {
        passed[count++] = image_option;
        passed[count++] = (char *)port_FindImage(beside);
    }
    passed[count] = NULL;

    fprintf(stderr,
            "rebushido: ReBushido in a window. Escape ends it; F11 or "
            "Alt+Enter fills the screen.\n"
            "rebushido: no memory card yet: settings and unlocks are not "
            "kept.\n"
            "rebushido: settings: %s\n",
            g_port_settings_path != NULL ? g_port_settings_path
                                         : "the ones built in (--help shows "
                                           "them)");
    port_DescribeControls();
    port_StartWindow();
    return port_Main(count, passed);
}
