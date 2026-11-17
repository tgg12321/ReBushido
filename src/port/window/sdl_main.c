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
 *             [--menu-shots FOLDER] */
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
#include "port_mods.h"
#include "port_remaster.h"
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
    "  rebushido [--image FILE] [--config FILE]\n"
    "            [--fullscreen | --windowed]\n"
    "            [--scale fit|integer|stretch] [--filter nearest|linear]\n"
    "            [--no-vsync] [--no-sound] [--no-fps] [--no-log]\n"
    "            [--log FILE] [--graphics original|remastered]\n"
    "            [--resolution N] [--draw-distance original|far|max]\n"
    "            [--antialiasing off|2|4] [--aspect W:H]\n"
    "            [--frame-rate original|display|N]\n"
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
    "  --antialiasing N    the scene drawn N times over each way and\n"
    "                 averaged down: off, 2 or 4\n"
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
    "                 the fighters' own bodies. It needs the\n"
    "                 remastered picture\n"
    "  --frame-rate W  original: one picture a vertical blank, as the\n"
    "                 console; display: as many pictures a second as the\n"
    "                 screen shows; or a number of them. The game steps\n"
    "                 at the console's own rate whatever this says: a\n"
    "                 picture between two of its frames is drawn from\n"
    "                 frames it has worked out already. It needs the\n"
    "                 remastered picture\n"
    "\n"
    "In the window: F1 opens the settings menu (Select does on the\n"
    "options page, the top menu and the pause, and a controller's guide\n"
    "button anywhere), which changes and writes the settings file.\n"
    "Escape, or closing the window, ends the program. F11 or\n"
    "Alt+Enter fills the screen and comes back. F2 changes between the\n"
    "original picture and the remastered one. F3 shows and hides the\n"
    "counter in the corner: pictures shown a second, the game's own\n"
    "frames a second, and the speed against the console's. F12 keeps a\n"
    "picture of the window as a PNG in screenshots/ beside the program,\n"
    "and the session's log says where it went.\n"
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
    "--tap-every N (each Nth picture as SDL is given it), --tap-window\n"
    "FOLDER (each Nth window as a player would see it, where there is a\n"
    "graphics card), --toggle-at N (what F2 does, at every Nth blank),\n"
    "--shot-at N[,P] (what F12 does, at blank N, keeping the Pth\n"
    "picture of it: 0 is the game's own frame and -1 the last shown),\n"
    "--window WxH (a window, not the full screen, that size as it opens), --tap-audio FILE\n"
    "(the samples as they are queued), --fault-at N and --fault-in-sound N\n"
    "(touch memory that is not there at vertical blank N, on the game's\n"
    "stack or on the sound card's thread: what a crash leaves in the\n"
    "log), --menu-at N[,N...] (what F1 does, at those blanks),\n"
    "--menu-keys WORDS (presses in the settings menu, one a picture: up,\n"
    "down, left, right, accept, back, close, clear, key:NAME, pad:NAME,\n"
    "shot) and --menu-shots FOLDER (where shot keeps the window).\n";

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
    static char *passed[ARGUMENTS_MAX + 8];
    static char mods_option[] = "--mods";
    static char mods_off[] = "off";
    static char image_option[] = "--image";
    static char frames_option[] = "--frames";
    static char frames_endless[] = "2000000000";
    PortWindowOptions *options = &g_port_window_options;
    const char *config = NULL;
    char *base = SDL_GetBasePath();
    const char *beside = base != NULL ? base : "";
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
    s32 stage_mesh = -1;
    s32 shadows = -1;
    PortRemasterView view;
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
                port_Complain("--shadows takes original, soft, cast, cast:N "
                              "or 1 to 100, not ",
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
        } else if (strcmp(word, "--menu-at") == 0 && has_value) {
            options->menu_at = argv[++i];
        } else if (strcmp(word, "--menu-keys") == 0 && has_value) {
            options->menu_keys = argv[++i];
        } else if (strcmp(word, "--menu-shots") == 0 && has_value) {
            options->menu_shots = argv[++i];
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
             "the screen; F2 changes picture; F3 is the counter in the "
             "corner; F12 keeps a picture of the window in "
             "screenshots/.");
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
    if (g_port_settings.shadows & PORT_SHADOWS_CAST) {
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
