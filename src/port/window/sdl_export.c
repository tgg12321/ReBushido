/* A replay as a film (docs/PORT.md, "A replay as a film"; film.h): the
 * program that makes one, and the program that asks for one.
 *
 * The film is made by another run of this program, `--replay FILE
 * --export OUT`: a window never shown, of the film's size, that plays
 * the replay as fast as the PC goes and gives each blank's picture, as
 * the window would show it, and the blank's sound to the film. Every
 * blank of the fight is a frame, from the first whose screen is the
 * fight to the replay's end, so the film is the fight and not its load.
 * Its lines say how far it is: `film: frame F of N`, and at the end
 * where the film went or why there is none.
 *
 * The settings menu's Replays page asks for one (port_ExportBegin): that
 * other run is started with the player's own settings, disc and mods,
 * and the page follows its lines; the game that asked goes on as it was,
 * none of its state touched, and the film can be stopped (the run is
 * ended and its file taken away). Nothing here is the game's state.
 * Only the build with a window has this file. */
#include <SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
#include <windows.h>
#else
#include <fcntl.h>
#include <signal.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#if defined(__linux__)
#include <sys/prctl.h>
#endif
#endif

#include "film.h"
#include "port_replay.h"
#include "port_spu.h"
#include "shared.h"
#include "sdl_window.h"

#define PATH_BYTES 1024
#define LINE_BYTES 512
#define COMMAND_BYTES 8192
#define WORDS_MAX 40
#define PERMILLE 1000
/* the last of the sound kept before the film begins: a blank's head
 * (PORT_SPU_HEAD_FRAMES) and more */
#define TAIL_FRAMES 64
/* a line of how far it is, every so many frames */
#define SAY_EVERY 60
#define FILM_FOLDER "films"
/* how long a run that is stopped is given to end before its file is
 * taken away */
#define STOP_WAIT_MS 3000
#define BYTES_PER_PIXEL 4
#define MEGABYTE (1024L * 1024L)

/* ---- the run that makes the film ---- */

static struct {
    const char *path;
    s32 kind;
    s32 w;
    s32 h;
    PortFilm *film;
    s32 failed;
    char why[PORT_FILM_WHY_BYTES];
    u32 first_blank;
    u64 first_sample;
    u64 heard; /* samples the window was given since the program began */
    s16 tail[TAIL_FRAMES * PORT_FILM_CHANNELS];
    u32 *scaled; /* a picture of the window's size made the film's */
    u32 *last;   /* the last picture, for a blank that shows none */
    u32 started; /* SDL's ticks as the first frame was taken */
    s32 told_scale;
} g_film;

s32 port_ExportSetUp(const char *path, s32 w, s32 h, char *why, u32 room) {
    g_film.kind = port_FilmKindOf(path);
    if (g_film.kind == PORT_FILM_NONE) {
        snprintf(why, room, "--export takes a file ending .mp4 or .avi, not %s",
                 path);
        return FALSE;
    }
    if (!port_FilmCanMake(g_film.kind)) {
        snprintf(why, room,
                 "an .mp4 is made by Windows' Media Foundation, which is not "
                 "here: an .avi can be made");
        return FALSE;
    }
    if (w <= 0 || h <= 0 || (w & 1) != 0 || (h & 1) != 0) {
        snprintf(why, room,
                 "a film is an even number of pixels each way, not %dx%d", w,
                 h);
        return FALSE;
    }
    g_film.path = path;
    g_film.w = w;
    g_film.h = h;
    return TRUE;
}

s32 port_ExportIsOn(void) { return g_film.path != NULL; }

static void port_FilmFails(const char *why) {
    if (!g_film.failed) {
        g_film.failed = TRUE;
        snprintf(g_film.why, sizeof(g_film.why), "%s", why);
        port_Say("film: not made: %s", why);
    }
}

/* The film begins with this frame where the replay is at its fight. */
s32 port_ExportWants(void) {
    PortReplayStatus status;

    if (g_film.path == NULL || g_film.failed) {
        return FALSE;
    }
    if (g_film.film != NULL) {
        return TRUE;
    }
    port_GetReplayStatus(&status);
    return status.is_playing && port_IsFightScreen();
}

/* `pixels` the window's, `w` by `h`, as the film's size: by the nearest
 * pixel, where the system gave the window another size than asked */
static const u32 *port_FilmSized(const u32 *pixels, s32 w, s32 h) {
    s32 x;
    s32 y;

    if (w == g_film.w && h == g_film.h) {
        return pixels;
    }
    if (g_film.scaled == NULL) {
        g_film.scaled =
            malloc((size_t)g_film.w * (size_t)g_film.h * BYTES_PER_PIXEL);
        if (g_film.scaled == NULL) {
            return NULL;
        }
    }
    if (!g_film.told_scale) {
        g_film.told_scale = TRUE;
        port_Say("film: the window is %dx%d and not %dx%d: its pictures are "
                 "made the film's size",
                 w, h, g_film.w, g_film.h);
    }
    for (y = 0; y < g_film.h; y++) {
        const u32 *row = pixels + (size_t)(y * h / g_film.h) * (size_t)w;
        u32 *out = g_film.scaled + (size_t)y * (size_t)g_film.w;

        for (x = 0; x < g_film.w; x++) {
            out[x] = row[x * w / g_film.w];
        }
    }
    return g_film.scaled;
}

static void port_FilmBegin(u32 blank) {
    char why[PORT_FILM_WHY_BYTES];
    u32 head = PORT_SPU_HEAD_FRAMES;

    g_film.film = port_FilmOpen(
        g_film.path, g_film.kind, g_film.w, g_film.h, why, sizeof(why));
    if (g_film.film == NULL) {
        port_FilmFails(why);
        return;
    }
    g_film.last = calloc((size_t)g_film.w * (size_t)g_film.h, BYTES_PER_PIXEL);
    /* the blank's picture is shown from its moment, and the samples
     * made after the moment are the blank's head, made with it */
    g_film.first_blank = blank;
    g_film.first_sample = g_film.heard - head;
    g_film.started = SDL_GetTicks();
    port_Say("film: %s, %dx%d, begins at blank %u and sample %llu", g_film.path,
             g_film.w, g_film.h, blank,
             (unsigned long long)g_film.first_sample);
    if (!port_FilmSound(
            g_film.film,
            g_film.tail + (TAIL_FRAMES - head) * PORT_FILM_CHANNELS, head)) {
        port_FilmFails("its sound cannot be written");
    }
}

void port_ExportPicture(u32 blank, const u32 *pixels, s32 w, s32 h) {
    PortReplayStatus status;
    const u32 *picture;

    if (!port_ExportWants()) {
        return;
    }
    if (g_film.film == NULL) {
        port_FilmBegin(blank);
        if (g_film.film == NULL) {
            port_Quit(PORT_EXIT_FAILED);
            return;
        }
    }
    picture = pixels != NULL ? port_FilmSized(pixels, w, h) : g_film.last;
    if (picture == NULL || g_film.last == NULL) {
        port_FilmFails("no memory for its pictures");
        port_Quit(PORT_EXIT_FAILED);
        return;
    }
    if (picture != g_film.last) {
        memcpy(g_film.last, picture,
               (size_t)g_film.w * (size_t)g_film.h * BYTES_PER_PIXEL);
    }
    if (!port_FilmPicture(g_film.film, picture)) {
        port_FilmFails("a picture cannot be written (the disc is full, or an "
                       "AVI reached 2 GB)");
        port_Quit(PORT_EXIT_FAILED);
        return;
    }
    if (port_FilmPictures(g_film.film) % SAY_EVERY == 0) {
        port_GetReplayStatus(&status);
        port_Say("film: frame %u of %u", status.frame, status.frames);
    }
}

void port_ExportSound(const s16 *samples, u32 frames) {
    u32 i;

    g_film.heard += frames;
    if (g_film.path == NULL) {
        return;
    }
    if (g_film.film != NULL) {
        if (!g_film.failed && !port_FilmSound(g_film.film, samples, frames)) {
            port_FilmFails("its sound cannot be written");
        }
        return;
    }
    /* the last few kept, for the blank the film begins at */
    if (frames >= TAIL_FRAMES) {
        memcpy(g_film.tail,
               samples + (frames - TAIL_FRAMES) * PORT_FILM_CHANNELS,
               sizeof(g_film.tail));
        return;
    }
    memmove(g_film.tail, g_film.tail + frames * PORT_FILM_CHANNELS,
            (TAIL_FRAMES - frames) * PORT_FILM_CHANNELS * sizeof(s16));
    for (i = 0; i < frames * PORT_FILM_CHANNELS; i++) {
        g_film.tail[(TAIL_FRAMES - frames) * PORT_FILM_CHANNELS + i] =
            samples[i];
    }
}

void port_ExportReplayEnded(const char *trouble) {
    if (g_film.path != NULL && trouble != NULL) {
        port_FilmFails(trouble);
    }
}

s32 port_ExportEnd(s32 code) {
    char why[PORT_FILM_WHY_BYTES];
    u32 pictures;
    u64 samples;
    u64 wanted;
    FILE *file;
    long bytes = 0;

    if (g_film.path == NULL) {
        return code;
    }
    if (g_film.film == NULL) {
        port_FilmFails(code == PORT_EXIT_OK ? "the replay has no fight"
                                            : "the replay was not played");
        return PORT_EXIT_FAILED;
    }
    pictures = port_FilmPictures(g_film.film);
    /* the sound as long as the pictures: the last blank's is made at the
     * blank after it, which the replay does not reach */
    samples = port_FilmSamples(g_film.film);
    wanted = (u64)pictures * PORT_FILM_RATE_DENOMINATOR *
             PORT_FILM_SAMPLE_RATE / PORT_FILM_RATE_NUMERATOR;
    while (samples < wanted && !g_film.failed) {
        static const s16 g_silence[256 * PORT_FILM_CHANNELS];
        u32 more = wanted - samples < 256 ? (u32)(wanted - samples) : 256;

        port_FilmSound(g_film.film, g_silence, more);
        samples += more;
    }
    if (code != PORT_EXIT_OK) {
        port_FilmFails("the replay did not play to its end");
    }
    if (!port_FilmClose(g_film.film, !g_film.failed, why, sizeof(why))) {
        port_FilmFails(why);
    }
    g_film.film = NULL;
    if (g_film.failed) {
        return PORT_EXIT_FAILED;
    }
    file = fopen(g_film.path, "rb");
    if (file != NULL) {
        fseek(file, 0, SEEK_END);
        bytes = ftell(file);
        fclose(file);
    }
    port_Say("film: written %s, %u frames (%u.%02u seconds), %ld bytes, in "
             "%u.%u seconds",
             g_film.path, pictures,
             (u32)((u64)pictures * PORT_FILM_RATE_DENOMINATOR /
                   PORT_FILM_RATE_NUMERATOR),
             (u32)((u64)pictures * PORT_FILM_RATE_DENOMINATOR * 100 /
                   PORT_FILM_RATE_NUMERATOR % 100),
             bytes, (SDL_GetTicks() - g_film.started) / 1000,
             (SDL_GetTicks() - g_film.started) / 100 % 10);
    return code;
}

/* ---- the program that asks for one ---- */

static struct {
    s32 state;
    u32 permille;
    u32 generation;
    char name[PORT_REPLAY_NAME_BYTES];
    char out[PATH_BYTES];
    char words[LINE_BYTES];
    char line[LINE_BYTES];
    u32 line_bytes;
    char last[LINE_BYTES]; /* the run's last line, for a run that ends badly */
    u32 began;
#if defined(_WIN32)
    HANDLE process;
    HANDLE job;
    HANDLE pipe;
#else
    pid_t process;
    int pipe;
#endif
} g_asked;

static void port_ExportSet(s32 state, const char *words) {
    g_asked.state = state;
    g_asked.generation++;
    if (words != NULL) {
        snprintf(g_asked.words, sizeof(g_asked.words), "%s", words);
    }
}

void port_ExportGetView(PortExportView *view) {
    view->state = g_asked.state;
    view->permille = g_asked.permille;
    view->generation = g_asked.generation;
    view->seconds = g_asked.state == PORT_EXPORT_RUNNING
                        ? (SDL_GetTicks() - g_asked.began) / 1000
                        : 0;
    snprintf(view->name, sizeof(view->name), "%s", g_asked.name);
    snprintf(view->words, sizeof(view->words), "%s", g_asked.words);
}

/* The film's name in films/ beside the program: the replay's, as an
 * .mp4 where Windows can make one and an .avi elsewhere. */
static void port_ExportOut(const char *name, s32 kind) {
    char *base = SDL_GetBasePath();
    char stem[PORT_REPLAY_NAME_BYTES];
    char *dot;

    snprintf(stem, sizeof(stem), "%s", name);
    dot = strrchr(stem, '.');
    if (dot != NULL) {
        *dot = 0;
    }
    snprintf(g_asked.out, sizeof(g_asked.out), "%s%s", base != NULL ? base : "",
             FILM_FOLDER);
#if defined(_WIN32)
    CreateDirectoryA(g_asked.out, NULL);
#else
    mkdir(g_asked.out, 0755);
#endif
    snprintf(g_asked.out, sizeof(g_asked.out), "%s%s/%s.%s",
             base != NULL ? base : "", FILM_FOLDER, stem,
             kind == PORT_FILM_MP4 ? "mp4" : "avi");
    SDL_free(base);
}

/* A line of the run's: how far it is, or how it ended. */
static void port_ExportLine(const char *line) {
    const char *film = strstr(line, "film: ");
    unsigned frame = 0;
    unsigned frames = 0;

    if (line[0] != 0) {
        snprintf(g_asked.last, sizeof(g_asked.last), "%s", line);
    }
    if (film == NULL) {
        return;
    }
    film += 6;
    if (sscanf(film, "frame %u of %u", &frame, &frames) == 2 && frames != 0) {
        g_asked.permille = frame * PERMILLE / frames;
        g_asked.generation++;
    } else if (strncmp(film, "written ", 8) == 0) {
        g_asked.permille = PERMILLE;
        snprintf(g_asked.words, sizeof(g_asked.words), "%s", film + 8);
    } else if (strncmp(film, "not made: ", 10) == 0) {
        snprintf(g_asked.words, sizeof(g_asked.words), "%s", film + 10);
    }
}

static void port_ExportBytes(const char *bytes, u32 count) {
    u32 i;

    for (i = 0; i < count; i++) {
        if (bytes[i] == '\n' || bytes[i] == '\r') {
            g_asked.line[g_asked.line_bytes] = 0;
            port_ExportLine(g_asked.line);
            g_asked.line_bytes = 0;
        } else if (g_asked.line_bytes + 1 < sizeof(g_asked.line)) {
            g_asked.line[g_asked.line_bytes++] = bytes[i];
        }
    }
}

/* The run has ended with `code`: the film there, or why not. */
static void port_ExportEnded(s32 code) {
    if (g_asked.state != PORT_EXPORT_RUNNING) {
        return;
    }
    if (code == PORT_EXIT_OK && g_asked.permille == PERMILLE) {
        /* where it is, beside the program, and how large */
        const char *name = strstr(g_asked.out, FILM_FOLDER "/");
        FILE *file = fopen(g_asked.out, "rb");
        long bytes = 0;

        if (file != NULL) {
            fseek(file, 0, SEEK_END);
            bytes = ftell(file);
            fclose(file);
        }
        port_Say("film: %s", g_asked.words);
        snprintf(g_asked.words, sizeof(g_asked.words), "%s, %ld.%ld MB",
                 name != NULL ? name : g_asked.out, bytes / MEGABYTE,
                 bytes % MEGABYTE * 10 / MEGABYTE);
        port_ExportSet(PORT_EXPORT_DONE, NULL);
        return;
    }
    if (strstr(g_asked.last, "film: not made: ") == NULL) {
        snprintf(g_asked.words, sizeof(g_asked.words), "%s", g_asked.last);
    }
    port_Say("film: of %s, not made (exit %d): %s", g_asked.name, code,
             g_asked.words);
    remove(g_asked.out);
    port_ExportSet(PORT_EXPORT_FAILED, NULL);
}

#if defined(_WIN32)

/* A word of a command line as Windows' C library reads it back. */
static void port_ExportQuote(const char *word, char *out, u32 room) {
    u32 at = 0;
    u32 slashes = 0;

    if (room < 3) {
        return;
    }
    out[at++] = '"';
    for (; *word != 0 && at + 4 < room; word++) {
        if (*word == '\\') {
            slashes++;
        } else if (*word == '"') {
            while (slashes-- > 0 && at + 4 < room) {
                out[at++] = '\\';
            }
            slashes = 0;
            out[at++] = '\\';
        } else {
            slashes = 0;
        }
        out[at++] = *word;
    }
    while (slashes-- > 0 && at + 3 < room) {
        out[at++] = '\\';
    }
    out[at++] = '"';
    out[at] = 0;
}

static s32 port_ExportStart(char **words) {
    static char command[COMMAND_BYTES];
    static wchar_t wide[COMMAND_BYTES];
    char quoted[PATH_BYTES + 16];
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits;
    SECURITY_ATTRIBUTES inherit;
    STARTUPINFOW start;
    PROCESS_INFORMATION process;
    HANDLE write_end;
    u32 used = 0;
    s32 i;

    for (i = 0; words[i] != NULL; i++) {
        port_ExportQuote(words[i], quoted, sizeof(quoted));
        used += (u32)snprintf(command + used, sizeof(command) - used, "%s%s",
                              i == 0 ? "" : " ", quoted);
        if (used >= sizeof(command)) {
            return FALSE;
        }
    }
    if (MultiByteToWideChar(CP_ACP, 0, command, -1, wide, COMMAND_BYTES) == 0) {
        return FALSE;
    }
    memset(&inherit, 0, sizeof(inherit));
    inherit.nLength = sizeof(inherit);
    inherit.bInheritHandle = TRUE;
    if (!CreatePipe(&g_asked.pipe, &write_end, &inherit, 0)) {
        return FALSE;
    }
    SetHandleInformation(g_asked.pipe, HANDLE_FLAG_INHERIT, 0);
    /* in a job of its own, which ends it with this program */
    g_asked.job = CreateJobObjectW(NULL, NULL);
    memset(&limits, 0, sizeof(limits));
    limits.BasicLimitInformation.LimitFlags =
        JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    if (g_asked.job != NULL) {
        SetInformationJobObject(g_asked.job, JobObjectExtendedLimitInformation,
                                &limits, sizeof(limits));
    }
    memset(&start, 0, sizeof(start));
    start.cb = sizeof(start);
    start.dwFlags = STARTF_USESTDHANDLES;
    start.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
    start.hStdOutput = write_end;
    start.hStdError = write_end;
    if (!CreateProcessW(
            NULL, wide, NULL, NULL, TRUE, CREATE_NO_WINDOW | CREATE_SUSPENDED,
            NULL, NULL, &start, &process)) {
        port_Say("film: the run cannot be started (error %lu)",
                 (unsigned long)GetLastError());
        CloseHandle(write_end);
        CloseHandle(g_asked.pipe);
        if (g_asked.job != NULL) {
            CloseHandle(g_asked.job);
        }
        return FALSE;
    }
    if (g_asked.job != NULL) {
        AssignProcessToJobObject(g_asked.job, process.hProcess);
    }
    ResumeThread(process.hThread);
    CloseHandle(process.hThread);
    CloseHandle(write_end);
    g_asked.process = process.hProcess;
    return TRUE;
}

static void port_ExportRead(void) {
    char bytes[LINE_BYTES];
    DWORD waiting = 0;
    DWORD got = 0;

    while (PeekNamedPipe(g_asked.pipe, NULL, 0, NULL, &waiting, NULL) &&
           waiting > 0 &&
           ReadFile(g_asked.pipe, bytes,
                    waiting < sizeof(bytes) ? waiting : sizeof(bytes), &got,
                    NULL) &&
           got > 0) {
        port_ExportBytes(bytes, got);
    }
}

static void port_ExportClose(void) {
    CloseHandle(g_asked.process);
    CloseHandle(g_asked.pipe);
    if (g_asked.job != NULL) {
        CloseHandle(g_asked.job);
    }
    g_asked.process = NULL;
}

void port_ExportTick(void) {
    DWORD code = 0;

    if (g_asked.state != PORT_EXPORT_RUNNING || g_asked.process == NULL) {
        return;
    }
    port_ExportRead();
    if (WaitForSingleObject(g_asked.process, 0) != WAIT_OBJECT_0) {
        return;
    }
    port_ExportRead();
    GetExitCodeProcess(g_asked.process, &code);
    port_ExportClose();
    port_ExportEnded((s32)code);
}

static void port_ExportKill(void) {
    TerminateProcess(g_asked.process, PORT_EXIT_FAILED);
    WaitForSingleObject(g_asked.process, STOP_WAIT_MS);
    port_ExportClose();
}

#else

static s32 port_ExportStart(char **words) {
    int ends[2];
    pid_t child;

    if (pipe(ends) != 0) {
        return FALSE;
    }
    child = fork();
    if (child < 0) {
        close(ends[0]);
        close(ends[1]);
        return FALSE;
    }
    if (child == 0) {
        dup2(ends[1], STDOUT_FILENO);
        dup2(ends[1], STDERR_FILENO);
        close(ends[0]);
        close(ends[1]);
#if defined(__linux__)
        /* it ends with this program */
        prctl(PR_SET_PDEATHSIG, SIGTERM);
#endif
        execv(words[0], words);
        _exit(PORT_EXIT_FAILED);
    }
    close(ends[1]);
    fcntl(ends[0], F_SETFL, fcntl(ends[0], F_GETFL) | O_NONBLOCK);
    g_asked.process = child;
    g_asked.pipe = ends[0];
    return TRUE;
}

static void port_ExportRead(void) {
    char bytes[LINE_BYTES];
    ssize_t got;

    while ((got = read(g_asked.pipe, bytes, sizeof(bytes))) > 0) {
        port_ExportBytes(bytes, (u32)got);
    }
}

void port_ExportTick(void) {
    int status = 0;

    if (g_asked.state != PORT_EXPORT_RUNNING || g_asked.process <= 0) {
        return;
    }
    port_ExportRead();
    if (waitpid(g_asked.process, &status, WNOHANG) != g_asked.process) {
        return;
    }
    port_ExportRead();
    close(g_asked.pipe);
    g_asked.process = 0;
    port_ExportEnded(
        WIFEXITED(status) ? WEXITSTATUS(status) : PORT_EXIT_FAILED);
}

static void port_ExportKill(void) {
    int status = 0;
    u32 began = SDL_GetTicks();

    kill(g_asked.process, SIGTERM);
    while (waitpid(g_asked.process, &status, WNOHANG) == 0 &&
           SDL_GetTicks() - began < STOP_WAIT_MS) {
        SDL_Delay(10);
    }
    if (waitpid(g_asked.process, &status, WNOHANG) == 0) {
        kill(g_asked.process, SIGKILL);
        waitpid(g_asked.process, &status, 0);
    }
    close(g_asked.pipe);
    g_asked.process = 0;
}

#endif

s32 port_ExportBegin(
    const char *replay, const char *name, char *why, u32 room) {
    static char size[32];
    static char graphics_word[16];
    static const char * const g_sizes[] = {
        "1920x1080", "1280x720", "2560x1440", "3840x2160"};
    char *words[WORDS_MAX];
    const char *image = port_GetImagePath();
    /* the settings file this run was told of, or none: the one beside the
     * program, as this run read it */
    const char *settings = port_GetCommandWord("--config");
    s32 kind = port_FilmCanMake(PORT_FILM_MP4) ? PORT_FILM_MP4 : PORT_FILM_AVI;
    s32 count = 0;
    s32 i;

    if (g_asked.state == PORT_EXPORT_RUNNING) {
        snprintf(why, room, "a film is being made already");
        return FALSE;
    }
    if (port_GetProgramPath() == NULL || image == NULL) {
        snprintf(why, room, "this program cannot start another run of itself");
        return FALSE;
    }
    port_ExportOut(name, kind);
    i = g_port_settings.film_size;
    snprintf(
        size, sizeof(size), "%s",
        g_sizes[i >= 0 && i < (s32)(sizeof(g_sizes) / sizeof(g_sizes[0])) ? i
                                                                          : 0]);
    snprintf(graphics_word, sizeof(graphics_word), "%s",
             g_port_settings.film_picture == 0 ? "remastered" : "original");
    words[count++] = (char *)port_GetProgramPath();
    words[count++] = (char *)"--replay";
    words[count++] = (char *)replay;
    words[count++] = (char *)"--export";
    words[count++] = g_asked.out;
    words[count++] = (char *)"--window";
    words[count++] = size;
    words[count++] = (char *)"--graphics";
    words[count++] = graphics_word;
    words[count++] = (char *)"--image";
    words[count++] = (char *)image;
    if (settings != NULL) {
        words[count++] = (char *)"--config";
        words[count++] = (char *)settings;
    }
    /* the mods as this run has them (the replay holds their sums) */
    if (port_GetCommandWord("--mods") != NULL) {
        words[count++] = (char *)"--mods";
        words[count++] = (char *)port_GetCommandWord("--mods");
    }
    if (port_GetCommandWord("--music-mods") != NULL) {
        words[count++] = (char *)"--music-mods";
        words[count++] = (char *)port_GetCommandWord("--music-mods");
    }
    words[count++] = (char *)"--no-cards";
    words[count++] = (char *)"--no-log";
    words[count] = NULL;
    snprintf(g_asked.name, sizeof(g_asked.name), "%s", name);
    g_asked.permille = 0;
    g_asked.line_bytes = 0;
    g_asked.last[0] = 0;
    g_asked.words[0] = 0;
    if (!port_ExportStart(words)) {
        snprintf(why, room, "another run of this program cannot be started");
        return FALSE;
    }
    g_asked.began = SDL_GetTicks();
    port_Say("film: of %s, into %s (%s, %s)", name, g_asked.out, size,
             graphics_word);
    port_ExportSet(PORT_EXPORT_RUNNING, NULL);
    return TRUE;
}

void port_ExportStop(void) {
    if (g_asked.state != PORT_EXPORT_RUNNING) {
        return;
    }
    port_ExportKill();
    remove(g_asked.out);
    port_Say("film: of %s, stopped", g_asked.name);
    port_ExportSet(PORT_EXPORT_STOPPED, "stopped");
}

void port_ExportForget(void) {
    if (g_asked.state != PORT_EXPORT_RUNNING) {
        port_ExportSet(PORT_EXPORT_IDLE, "");
    }
}
