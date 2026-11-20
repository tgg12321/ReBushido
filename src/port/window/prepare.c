/* The mods made from the player's own disc (docs/MODS.md, "Made from your
 * disc"): the remastered art, the stages' pages made larger and the
 * fighters with their geometry fixed are the game's own pictures and
 * models made anew, so no package may carry them; the package carries
 * what makes them instead, in prepare/ beside the program
 * (tools/port_prepare_bundle.py): a Python of its own with numpy and
 * Pillow, and the developer tools that make them, tools/port_prepare.py
 * first. The first time the program starts with a disc, or when the disc
 * or the program's version is another, this runs that tool and shows how
 * far it has come; the tool runs this program itself, with no window
 * shown, to dump the textures, and writes each mod into mods/ whole, with
 * made-from-disc.txt saying what it was made for. Escape (or a
 * controller's B) skips it: what was done is kept, and the next start
 * goes on from there. `disc_mods = off` never runs it.
 *
 * Only the build for Windows does this; anywhere else, and where there is
 * no prepare/ beside the program (every build of the repository's own),
 * it does nothing. Only the build with a window has this file. */
#include <SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
#include <windows.h>
#endif

#include "menu.h"
#include "port_gpu.h"
#include "port_host.h"
#include "sdl_window.h"

#define PREPARE_W 960
#define PREPARE_H 540
#define PATH_MAX_BYTES 1024
#define LINE_BYTES 512
#define COMMAND_BYTES 8192
#define MARKER "made-from-disc.txt"
#define MARKER_KEY "key = "
/* how often the screen is drawn again while the tool works, in ms */
#define FRAME_MS 33
#define PERMILLE 1000
#define BACKGROUND 0xFF0E0E12u
#define TITLE_COLOR 0xFFF2EEE6u
#define TEXT_COLOR 0xFFC8C2B8u
#define DIM_COLOR 0xFF8A857Cu
#define BAR_BACK 0xFF2A2A30u
#define BAR_FRONT 0xFFB8963Cu

/* the mods the tool makes, in the order it makes them */
static const char * const g_prepare_mods[] = {"art", "stages-hd", "fighters"};
#define PREPARE_MODS (s32)(sizeof(g_prepare_mods) / sizeof(g_prepare_mods[0]))

/* Whether mods/NAME needs making for `key`: not if its made-from-disc.txt
 * says this key, nor if it is a mod of the player's own (a mod.txt and no
 * made-from-disc.txt), which is never written over. */
static s32 port_PrepareNeeded(
    const char *beside, const char *name, const char *key) {
    char path[PATH_MAX_BYTES];
    char line[LINE_BYTES];
    FILE *file;
    s32 is_made = 0;

    snprintf(path, sizeof(path), "%smods/%s/%s", beside, name, MARKER);
    file = fopen(path, "rb");
    if (file != NULL) {
        while (fgets(line, sizeof(line), file) != NULL) {
            size_t length = strlen(line);

            while (length > 0 &&
                   (line[length - 1] == '\n' || line[length - 1] == '\r')) {
                line[--length] = 0;
            }
            if (strncmp(line, MARKER_KEY, strlen(MARKER_KEY)) == 0 &&
                strcmp(line + strlen(MARKER_KEY), key) == 0) {
                is_made = 1;
            }
        }
        fclose(file);
        return !is_made;
    }
    snprintf(path, sizeof(path), "%smods/%s/mod.txt", beside, name);
    file = fopen(path, "rb");
    if (file != NULL) {
        fclose(file);
        return 0; /* the player's own */
    }
    return 1;
}

#if defined(_WIN32)

/* What the tool has said, for the screen. */
typedef struct {
    s32 step;                 /* the mod being made, from 1 */
    s32 steps;                /* of how many */
    s32 permille;             /* of all of it */
    char title[LINE_BYTES];   /* the mod's name */
    char text[LINE_BYTES];    /* what it is doing */
    char pending[LINE_BYTES]; /* a line read only in part */
    size_t pending_used;
    s32 made;
    s32 failed;
} PortPrepareState;

/* UTF-8 (or, failing that, the PC's own code page: the command line's)
 * as Windows' wide characters, into `out`. */
static void port_PrepareWide(const char *text, wchar_t *out, s32 room) {
    if (MultiByteToWideChar(
            CP_UTF8, MB_ERR_INVALID_CHARS, text, -1, out, room) == 0) {
        MultiByteToWideChar(CP_ACP, 0, text, -1, out, room);
    }
}

/* One line of the tool's: `step I N TITLE`, `progress PERMILLE TEXT`,
 * `made NAME`, `failed NAME WHY`; every one but the progress goes into
 * the log. */
static void port_PrepareLine(PortPrepareState *state, char *line) {
    s32 permille;
    s32 step;
    s32 steps;
    s32 used = 0;

    if (sscanf(line, "progress %d %n", &permille, &used) == 1 && used > 0) {
        state->permille = permille;
        snprintf(state->text, sizeof(state->text), "%s", line + used);
        return;
    }
    port_Note("prepare: %s", line);
    if (sscanf(line, "step %d %d %n", &step, &steps, &used) == 2 && used > 0) {
        state->step = step;
        state->steps = steps;
        snprintf(state->title, sizeof(state->title), "%s", line + used);
    } else if (strncmp(line, "made ", 5) == 0) {
        state->made++;
    } else if (strncmp(line, "failed ", 7) == 0) {
        state->failed++;
    }
}

/* What has come down the pipe, cut into lines. */
static void port_PrepareRead(PortPrepareState *state, HANDLE pipe) {
    char bytes[LINE_BYTES];
    DWORD waiting = 0;
    DWORD got = 0;
    DWORD i;

    while (PeekNamedPipe(pipe, NULL, 0, NULL, &waiting, NULL) && waiting > 0) {
        if (!ReadFile(pipe, bytes,
                      waiting < sizeof(bytes) ? waiting : sizeof(bytes), &got,
                      NULL) ||
            got == 0) {
            return;
        }
        for (i = 0; i < got; i++) {
            char c = bytes[i];

            if (c == '\n') {
                state->pending[state->pending_used] = 0;
                port_PrepareLine(state, state->pending);
                state->pending_used = 0;
            } else if (
                c != '\r' && state->pending_used + 1 < sizeof(state->pending)) {
                state->pending[state->pending_used++] = c;
            }
        }
    }
}

static void port_PrepareText(u32 *pixels, s32 x, s32 baseline, const char *text,
                             s32 face, s32 size, u32 color) {
    PortTextStyle style;

    memset(&style, 0, sizeof(style));
    style.face = face;
    style.size = size;
    style.color = color;
    port_TextDraw(pixels, PREPARE_W, PREPARE_H, x, baseline, text, &style);
}

/* The screen: the title with the mod's number, what the tool is doing,
 * the bar, and how to skip. */
static void port_PreparePaint(
    u32 *pixels, const PortPrepareState *state, u32 seconds) {
    char line[LINE_BYTES];
    s32 bar_w = PREPARE_W - 160;
    s32 done = state->permille * bar_w / PERMILLE;
    s32 i;

    for (i = 0; i < PREPARE_W * PREPARE_H; i++) {
        pixels[i] = BACKGROUND;
    }
    snprintf(line, sizeof(line),
             "Preparing the remastered art from your disc... %d/%d",
             state->step > 0 ? state->step : 1,
             state->steps > 0 ? state->steps : 1);
    port_PrepareText(pixels, 80, 150, line, PORT_FACE_TITLE, 34, TITLE_COLOR);
    port_PrepareText(
        pixels, 80, 205, state->title, PORT_FACE_BOLD, 26, TEXT_COLOR);
    port_PrepareText(
        pixels, 80, 245, state->text, PORT_FACE_TEXT, 22, DIM_COLOR);
    port_FillRect(pixels, PREPARE_W, PREPARE_H, 80, 280, bar_w, 14, BAR_BACK);
    port_FillRect(pixels, PREPARE_W, PREPARE_H, 80, 280, done, 14, BAR_FRONT);
    snprintf(line, sizeof(line), "%d%%   %u:%02u", state->permille / 10,
             seconds / 60, seconds % 60);
    port_PrepareText(pixels, 80, 330, line, PORT_FACE_TEXT, 22, TEXT_COLOR);
    port_PrepareText(pixels, 80, 410,
                     "Made once, from your own disc, in a few minutes: the "
                     "menus' art, the stages'",
                     PORT_FACE_TEXT, 20, DIM_COLOR);
    port_PrepareText(pixels, 80, 438,
                     "textures and the fighters, which no download may "
                     "carry. Escape (or B) skips it;",
                     PORT_FACE_TEXT, 20, DIM_COLOR);
    port_PrepareText(
        pixels, 80, 466, "it goes on from where it was at the next start.",
        PORT_FACE_TEXT, 20, DIM_COLOR);
}

static void port_PrepareShow(
    SDL_Renderer *renderer, SDL_Texture *texture, const u32 *pixels) {
    SDL_UpdateTexture(texture, NULL, pixels, PREPARE_W * (s32)sizeof(u32));
    SDL_RenderClear(renderer);
    SDL_RenderCopy(renderer, texture, NULL, NULL);
    SDL_RenderPresent(renderer);
}

/* For a test (--tap-window FOLDER): the screen as it is when a mod is
 * begun, prepare-N.png in `shots`. */
static void port_PrepareShot(const char *shots, s32 step, const u32 *pixels) {
    char path[PATH_MAX_BYTES];
    s32 file;

    snprintf(path, sizeof(path), "%s/prepare-%d.png", shots, step);
    file = port_HostCreate(path);
    if (file < 0 || !port_WritePixelsPng(file, pixels, PREPARE_W, PREPARE_H)) {
        port_Note("prepare: %s cannot be written", path);
    }
    if (file >= 0) {
        port_HostClose(file);
    }
}

/* Whether the player asked to skip (Escape, a controller's B, Back or
 * Start), or closed the window (*closed). */
static s32 port_PrepareSkipped(s32 *closed) {
    SDL_Event event;
    s32 skipped = 0;

    while (SDL_PollEvent(&event)) {
        if (event.type == SDL_QUIT) {
            *closed = 1;
            skipped = 1;
        } else if (
            event.type == SDL_KEYDOWN && event.key.keysym.sym == SDLK_ESCAPE) {
            skipped = 1;
        } else if (event.type == SDL_CONTROLLERDEVICEADDED) {
            SDL_GameControllerOpen(event.cdevice.which);
        } else if (event.type == SDL_CONTROLLERBUTTONDOWN &&
                   (event.cbutton.button == SDL_CONTROLLER_BUTTON_B ||
                    event.cbutton.button == SDL_CONTROLLER_BUTTON_BACK ||
                    event.cbutton.button == SDL_CONTROLLER_BUTTON_START)) {
            skipped = 1;
        }
    }
    return skipped;
}

/* The tool started in a job of its own, which ends with it everything it
 * started (the program's runs that dump), whether it is skipped or this
 * program ends: its output down `*pipe`. */
static s32 port_PrepareStart(const char *command, HANDLE *job,
                             PROCESS_INFORMATION *process, HANDLE *pipe) {
    static wchar_t wide[COMMAND_BYTES];
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits;
    SECURITY_ATTRIBUTES inherit;
    STARTUPINFOW start;
    HANDLE write_end;

    memset(&inherit, 0, sizeof(inherit));
    inherit.nLength = sizeof(inherit);
    inherit.bInheritHandle = TRUE;
    if (!CreatePipe(pipe, &write_end, &inherit, 0)) {
        return 0;
    }
    SetHandleInformation(*pipe, HANDLE_FLAG_INHERIT, 0);
    *job = CreateJobObjectW(NULL, NULL);
    memset(&limits, 0, sizeof(limits));
    limits.BasicLimitInformation.LimitFlags =
        JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    if (*job != NULL) {
        SetInformationJobObject(
            *job, JobObjectExtendedLimitInformation, &limits, sizeof(limits));
    }
    memset(&start, 0, sizeof(start));
    start.cb = sizeof(start);
    start.dwFlags = STARTF_USESTDHANDLES;
    start.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
    start.hStdOutput = write_end;
    start.hStdError = write_end;
    port_PrepareWide(command, wide, COMMAND_BYTES);
    if (!CreateProcessW(
            NULL, wide, NULL, NULL, TRUE, CREATE_NO_WINDOW | CREATE_SUSPENDED,
            NULL, NULL, &start, process)) {
        port_Note("prepare: the tool does not start (error %lu)",
                  (unsigned long)GetLastError());
        CloseHandle(write_end);
        CloseHandle(*pipe);
        if (*job != NULL) {
            CloseHandle(*job);
        }
        return 0;
    }
    if (*job != NULL) {
        AssignProcessToJobObject(*job, process->hProcess);
    }
    ResumeThread(process->hThread);
    CloseHandle(write_end);
    return 1;
}

s32 port_PrepareMods(const char *beside, const char *image, const char *key,
                     s32 is_hidden, const char *shots, s32 *closed) {
    static char command[COMMAND_BYTES];
    static u32 pixels[PREPARE_W * PREPARE_H];
    char python[PATH_MAX_BYTES];
    char tool[PATH_MAX_BYTES];
    char program[PATH_MAX_BYTES];
    char only[LINE_BYTES];
    PortPrepareState state;
    PROCESS_INFORMATION process;
    SDL_Window *window = NULL;
    SDL_Renderer *renderer = NULL;
    SDL_Texture *texture = NULL;
    HANDLE job = NULL;
    HANDLE pipe = NULL;
    DWORD code = 1;
    u32 began;
    s32 skipped = 0;
    s32 shot_step = 0;
    s32 i;
    FILE *file;

    *closed = 0;
    snprintf(python, sizeof(python), "%sprepare\\python\\python.exe", beside);
    snprintf(tool, sizeof(tool), "%sprepare\\tools\\port_prepare.py", beside);
    file = fopen(tool, "rb");
    if (file == NULL) {
        return 0; /* no prepare/ beside the program: nothing to make */
    }
    fclose(file);
    only[0] = 0;
    for (i = 0; i < PREPARE_MODS; i++) {
        if (port_PrepareNeeded(beside, g_prepare_mods[i], key)) {
            snprintf(only + strlen(only), sizeof(only) - strlen(only), "%s%s",
                     only[0] != 0 ? "," : "", g_prepare_mods[i]);
        }
    }
    if (only[0] == 0) {
        return 0;
    }
    if (GetModuleFileNameA(NULL, program, sizeof(program)) == 0) {
        return 1;
    }
    snprintf(
        command, sizeof(command),
        "\"%s\" \"%s\" --image \"%s\" --program \"%s\" --mods "
        "\"%smods\" --work \"%sprepare-work\" --plans \"%sprepare\\plans\" "
        "--key \"%s\" --only %s",
        python, tool, image, program, beside, beside, beside, key, only);
    port_Say("prepare: making %s from your disc (prepare/; docs/MODS.md, "
             "\"Made from your disc\")",
             only);
    port_Note("prepare: %s", command);
    memset(&state, 0, sizeof(state));
    snprintf(state.text, sizeof(state.text), "begun");
    if (!port_PrepareStart(command, &job, &process, &pipe)) {
        return 1;
    }
    if (SDL_InitSubSystem(SDL_INIT_VIDEO | SDL_INIT_GAMECONTROLLER) == 0) {
        window = SDL_CreateWindow(
            "ReBushido", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
            PREPARE_W, PREPARE_H, is_hidden ? SDL_WINDOW_HIDDEN : 0);
    }
    if (window != NULL) {
        renderer = SDL_CreateRenderer(window, -1, 0);
        if (renderer == NULL) {
            renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_SOFTWARE);
        }
    }
    if (renderer != NULL) {
        texture = SDL_CreateTexture(
            renderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING,
            PREPARE_W, PREPARE_H);
    }
    port_FontStart();
    began = SDL_GetTicks();
    for (;;) {
        DWORD waited = WaitForSingleObject(process.hProcess, FRAME_MS);

        port_PrepareRead(&state, pipe);
        if (waited == WAIT_OBJECT_0) {
            break;
        }
        if (window != NULL && port_PrepareSkipped(closed)) {
            skipped = 1;
            break;
        }
        port_PreparePaint(pixels, &state, (SDL_GetTicks() - began) / 1000);
        if (texture != NULL) {
            port_PrepareShow(renderer, texture, pixels);
        }
        if (shots != NULL && state.step != shot_step) {
            port_PrepareShot(shots, state.step, pixels);
            shot_step = state.step;
        }
    }
    if (skipped) {
        /* the tool and the runs it started: what was finished is kept */
        if (job != NULL) {
            TerminateJobObject(job, 1);
        } else {
            TerminateProcess(process.hProcess, 1);
        }
        WaitForSingleObject(process.hProcess, INFINITE);
        port_Say("prepare: skipped after %u seconds; the next start goes "
                 "on from there",
                 (SDL_GetTicks() - began) / 1000);
    } else {
        GetExitCodeProcess(process.hProcess, &code);
        port_PrepareRead(&state, pipe);
        port_Say("prepare: %d made, %d failed, in %u seconds (the tool "
                 "ended %lu)",
                 state.made, state.failed, (SDL_GetTicks() - began) / 1000,
                 (unsigned long)code);
    }
    CloseHandle(process.hProcess);
    CloseHandle(process.hThread);
    CloseHandle(pipe);
    if (job != NULL) {
        CloseHandle(job);
    }
    if (texture != NULL) {
        SDL_DestroyTexture(texture);
    }
    if (renderer != NULL) {
        SDL_DestroyRenderer(renderer);
    }
    if (window != NULL) {
        SDL_DestroyWindow(window);
    }
    SDL_QuitSubSystem(SDL_INIT_VIDEO | SDL_INIT_GAMECONTROLLER);
    return skipped || code != 0;
}

#else

s32 port_PrepareMods(const char *beside, const char *image, const char *key,
                     s32 is_hidden, const char *shots, s32 *closed) {
    (void)beside;
    (void)image;
    (void)key;
    (void)is_hidden;
    (void)shots;
    (void)port_PrepareNeeded;
    (void)g_prepare_mods;
    *closed = 0;
    return 0;
}

#endif
