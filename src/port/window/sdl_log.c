/* The native port's window: the session's log (docs/PORT.md, "The
 * window"). Every session of the build with a window writes one,
 * `logs/rebushido-YYYYMMDD-HHMMSS.log` beside the program, and the newest
 * twenty are kept. Everything the port prints, to either of its outputs,
 * goes into it as well, a line at a time, each line written out as it
 * ends: so what was said before a crash is in the file. Beside that the
 * window's files leave notes in it that the console does not get: what
 * the session runs on, where the game is, what the disc reads, a line
 * every ten seconds.
 *
 * If the program's folder cannot be written to, the log goes to the
 * folder SDL names for a program's own files, and the console is told
 * where. `--no-log` leaves it out; `--log FILE` names the file. */
#include <SDL.h>
#include <dirent.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

#if defined(_WIN32)
#include <direct.h>
#endif

#include "port_host.h"
#include "sdl_window.h"

#define PATH_BYTES_MAX 1024
#define LINE_BYTES_MAX 1024
#define NAMES_MAX 512
#define LOG_FOLDER "logs"
#define MILLISECONDS 1000
#define EPOCH_YEAR 1900

static FILE *g_port_log;
static char g_port_log_path[PATH_BYTES_MAX];
/* The two outputs' lines are put together apart. */
static PortTee g_port_log_tees[2];

/* A line into the file, after the seconds since the session began, and
 * out of the library's hands at once. */
static void port_WriteLogLine(const char *line, u32 length) {
    Uint32 now = SDL_GetTicks();

    if (g_port_log == NULL) {
        return;
    }
    fprintf(g_port_log, "%6u.%03u  ", (unsigned)(now / MILLISECONDS),
            (unsigned)(now % MILLISECONDS));
    fwrite(line, 1, length, g_port_log);
    fputc('\n', g_port_log);
    fflush(g_port_log);
}

/* What the port prints (port_host.h). */
static void port_TeeToLog(s32 stream, const char *bytes, u32 count) {
    port_TeeBytes(&g_port_log_tees[stream == PORT_HOST_OUTPUT ? 0 : 1], bytes,
                  count, port_WriteLogLine);
}

/* To the error output, and so to the log: `rebushido: ...`. */
void port_Say(const char *format, ...) {
    char line[LINE_BYTES_MAX];
    va_list args;
    int length;

    va_start(args, format);
    length = snprintf(line, sizeof(line), "rebushido: ");
    length += vsnprintf(
        line + length, sizeof(line) - (size_t)length - 1, format, args);
    va_end(args);
    if (length > (int)sizeof(line) - 2) {
        length = (int)sizeof(line) - 2;
    }
    line[length++] = '\n';
    port_HostWrite(PORT_HOST_ERRORS, line, (u32)length);
}

/* To the log alone. */
void port_Note(const char *format, ...) {
    char line[LINE_BYTES_MAX];
    va_list args;
    int length;

    if (g_port_log == NULL) {
        return;
    }
    va_start(args, format);
    length = vsnprintf(line, sizeof(line), format, args);
    va_end(args);
    if (length > (int)sizeof(line) - 1) {
        length = (int)sizeof(line) - 1;
    }
    port_WriteLogLine(line, length > 0 ? (u32)length : 0);
}

const char *port_GetLogPath(void) {
    return g_port_log != NULL ? g_port_log_path : NULL;
}

static void port_MakeFolder(const char *path) {
#if defined(_WIN32)
    _mkdir(path);
#else
    mkdir(path, 0755);
#endif
}

/* Deletes the logs of a folder that twenty are newer than. */
static void port_DropOldLogs(const char *folder) {
    static char *names[NAMES_MAX];
    static u8 is_old[NAMES_MAX];
    char path[PATH_BYTES_MAX];
    DIR *listing = opendir(folder);
    struct dirent *entry;
    s32 count = 0;
    s32 i;

    if (listing == NULL) {
        return;
    }
    while (count < NAMES_MAX && (entry = readdir(listing)) != NULL) {
        if (port_IsLogName(entry->d_name)) {
            names[count] = SDL_strdup(entry->d_name);
            if (names[count] != NULL) {
                count++;
            }
        }
    }
    closedir(listing);
    port_FindOldLogs(
        (const char * const *)names, count, PORT_LOGS_KEPT, is_old);
    for (i = 0; i < count; i++) {
        if (is_old[i]) {
            snprintf(path, sizeof(path), "%s/%s", folder, names[i]);
            remove(path);
        }
        SDL_free(names[i]);
    }
}

/* A new log in `folder`/logs/, named for now: 1 if it could be made. */
static s32 port_OpenLogIn(const char *folder) {
    char name[PORT_LOG_NAME_BYTES];
    char logs[PATH_BYTES_MAX];
    time_t now = time(NULL);
    const struct tm *when = localtime(&now);

    if (when == NULL) {
        return 0;
    }
    port_FormatLogName(
        when->tm_year + EPOCH_YEAR, when->tm_mon + 1, when->tm_mday,
        when->tm_hour, when->tm_min, when->tm_sec, name);
    snprintf(logs, sizeof(logs), "%s%s", folder, LOG_FOLDER);
    port_MakeFolder(logs);
    snprintf(g_port_log_path, sizeof(g_port_log_path), "%s/%s", logs, name);
    g_port_log = fopen(g_port_log_path, "w");
    if (g_port_log == NULL) {
        return 0;
    }
    port_DropOldLogs(logs);
    return 1;
}

/* The session's log: the file named, or a new one beside the program,
 * or failing that in the folder SDL gives a program for its own files.
 * From here on everything printed goes into it. */
void port_OpenLog(const char *named, const char *beside) {
    SDL_version compiled;
    SDL_version running;

    if (named != NULL) {
        snprintf(g_port_log_path, sizeof(g_port_log_path), "%s", named);
        g_port_log = fopen(g_port_log_path, "w");
    } else if (!port_OpenLogIn(beside)) {
        char *own = SDL_GetPrefPath("ReBushido", "ReBushido");

        if (own != NULL) {
            port_OpenLogIn(own);
            SDL_free(own);
        }
    }
    if (g_port_log == NULL) {
        fprintf(stderr, "rebushido: no log of this session: none could be "
                        "made beside the program or in your own folder\n");
        return;
    }
    g_port_host_tee = port_TeeToLog;
    SDL_VERSION(&compiled);
    SDL_GetVersion(&running);
    port_Note("the log of a session of ReBushido's native port");
    port_Note("version: %s", port_GetVersionText());
    port_Note("build: %s, %d-bit",
#if defined(_WIN32)
              "Windows",
#else
              "Linux",
#endif
              (int)(sizeof(void *) * 8));
    port_Note("SDL: built for %d.%d.%d, running %d.%d.%d", compiled.major,
              compiled.minor, compiled.patch, running.major, running.minor,
              running.patch);
    port_Say("this session's log: %s", g_port_log_path);
}

/* The last of a line that never ended, and the file shut. */
void port_CloseLog(void) {
    if (g_port_log == NULL) {
        return;
    }
    g_port_host_tee = NULL;
    port_FlushTee(&g_port_log_tees[0], port_WriteLogLine);
    port_FlushTee(&g_port_log_tees[1], port_WriteLogLine);
    fclose(g_port_log);
    g_port_log = NULL;
}
