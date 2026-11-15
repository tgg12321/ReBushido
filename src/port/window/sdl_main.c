/* The native port with a window (docs/PORT.md, "The window"): where the
 * program starts in that build. It takes the window's own options off
 * the command line, reads the settings file, finds the disc image if
 * nothing names one, says how the pads are pressed, hangs the window on
 * the port (sdl_window.c) and then is the port's program as it is
 * without a window (src/port/main.c):
 *
 *   rebushido [--image FILE] [--config FILE] [--fullscreen]
 *             [--scale fit|integer|stretch] [--filter nearest|linear]
 *             [--no-vsync] [--no-sound] [--no-fps] [--no-log]
 *             [--log FILE]
 *             [the options of the build without a window]
 *
 * and for the tests, which show and sound nothing:
 *
 *             [--fast] [--inject keys|pads] [--tap-frames FOLDER]
 *             [--tap-every N] [--tap-audio FILE] [--fault-at N]
 *             [--fault-in-sound N] */
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
#define IMAGE_SUM_BYTES 0x100000
#define CUE_BYTES_MAX 2048

static const char g_port_window_usage[] =
    "rebushido: ReBushido's native port, in a window (docs/PORT.md)\n"
    "\n"
    "  rebushido [--image FILE] [--config FILE] [--fullscreen]\n"
    "            [--scale fit|integer|stretch] [--filter nearest|linear]\n"
    "            [--no-vsync] [--no-sound] [--no-fps] [--no-log]\n"
    "            [--log FILE]\n"
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
    "  --no-fps       no counter in the corner (F3 shows and hides it)\n"
    "  --no-log       keep no log of the session\n"
    "  --log FILE     the session's log; without it a new file in logs/\n"
    "                 beside the program, of which the newest twenty are\n"
    "                 kept\n"
    "\n"
    "In the window: Escape, or closing it, ends the program. F11 or\n"
    "Alt+Enter fills the screen and comes back. F3 shows and hides the\n"
    "counter in the corner: pictures shown a second, the game's own\n"
    "frames a second, and the speed against the console's.\n"
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
    "--spu), and for the tests, which show and sound nothing: --fast\n"
    "(never wait), --inject keys|pads (a plan's presses as key events,\n"
    "or as two controllers SDL makes up), --tap-frames FOLDER and\n"
    "--tap-every N (each Nth picture as SDL is given it), --tap-audio FILE\n"
    "(the samples as they are queued), --fault-at N and --fault-in-sound N\n"
    "(touch memory that is not there at vertical blank N, on the game's\n"
    "stack or on the sound card's thread: what a crash leaves in the\n"
    "log).\n";

static void port_Complain(const char *problem, const char *word) {
    fprintf(stderr, "rebushido: %s%s\nrebushido: --help says what it takes\n",
            problem, word);
    exit(PORT_EXIT_USAGE);
}

/* ---- the settings file ---- */

static const char *g_port_settings_path;

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

/* For the log: a file of the image by name, its size, and a sum of its
 * first megabyte, which tells one image from another without reading
 * 700 MB as the program starts. (The port itself checks every sector
 * it reads for its mark and its own number: src/port/disc.c.) */
static void port_NoteImageFile(const char *path) {
    static u8 bytes[IMAGE_SUM_BYTES];
    FILE *file = fopen(path, "rb");
    long long size = 0;
    size_t got;

    if (file == NULL) {
        port_Note("image: %s (cannot be opened)", path);
        return;
    }
    got = fread(bytes, 1, sizeof(bytes), file);
#if defined(_WIN32)
    if (_fseeki64(file, 0, SEEK_END) == 0) {
        size = _ftelli64(file);
    }
#else
    if (fseeko(file, 0, SEEK_END) == 0) {
        size = ftello(file);
    }
#endif
    fclose(file);
    port_Note("image: %s; %lld bytes; CRC-32 of its first %u bytes %08X", path,
              size, (unsigned)got, (unsigned)port_Crc32(0, bytes, (u32)got));
}

/* The image named, and, where that is a cue sheet, the file of sectors
 * its first FILE line names, beside it. */
static void port_NoteImage(const char *path) {
    char sectors[PATH_BYTES_MAX];
    char text[CUE_BYTES_MAX + 1];
    const char *name;
    const char *end;
    const char *cut = path;
    const char *at;
    FILE *file;
    size_t got;

    port_NoteImageFile(path);
    if (!port_EndsWith(path, ".cue") || (file = fopen(path, "rb")) == NULL) {
        return;
    }
    got = fread(text, 1, CUE_BYTES_MAX, file);
    fclose(file);
    text[got] = 0;
    name = strstr(text, "FILE \"");
    end = name != NULL ? strchr(name + 6, '"') : NULL;
    if (end == NULL) {
        return;
    }
    name += 6;
    /* the cue sheet's folder, then the name */
    for (at = path; *at != 0; at++) {
        if (*at == '/' || *at == '\\') {
            cut = at + 1;
        }
    }
    snprintf(sectors, sizeof(sectors), "%.*s%.*s", (int)(cut - path), path,
             (int)(end - name), name);
    port_NoteImageFile(sectors);
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
    s32 no_fps = 0;
    s32 no_log = 0;
    const char *log = NULL;
    const char *image = NULL;
    s32 code;
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
        } else if (strcmp(word, "--no-fps") == 0) {
            no_fps = 1;
        } else if (strcmp(word, "--no-log") == 0) {
            no_log = 1;
        } else if (strcmp(word, "--log") == 0 && has_value) {
            log = argv[++i];
        } else if (strcmp(word, "--fault-at") == 0 && has_value) {
            options->fault_at = atoi(argv[++i]);
        } else if (strcmp(word, "--fault-in-sound") == 0 && has_value) {
            options->fault_in_sound = atoi(argv[++i]);
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
                image = has_value ? argv[i + 1] : NULL;
            }
            if (count == ARGUMENTS_MAX) {
                port_Complain("too many words on the command line", "");
            }
            passed[count++] = argv[i];
        }
    }
    if (!no_log) {
        port_OpenLog(log, beside);
    }
    port_LoadSettings(config, beside);
    if (no_fps) {
        g_port_settings.fps = 0;
    }
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
        image = port_FindImage(beside);
        passed[count++] = image_option;
        passed[count++] = (char *)image;
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
        port_NoteImage(image);
    }

    port_Say("ReBushido in a window. Escape ends it; F11 or Alt+Enter fills "
             "the screen; F3 is the counter in the corner.");
    port_Say("memory cards: card1.mcd and card2.mcd beside the program "
             "unless --card1, --card2 or --no-cards says otherwise.");
    port_Say("settings: %s", g_port_settings_path != NULL
                                 ? g_port_settings_path
                                 : "the ones built in (--help shows them)");
    port_DescribeControls();
    port_StartWindow();
    /* the game never comes back; what does is a start that failed */
    code = port_Main(count, passed);
    port_EndSession(code);
    return code;
}
