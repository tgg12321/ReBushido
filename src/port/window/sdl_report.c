/* The native port's window: a report of a problem, made by one key (F9;
 * docs/PLAYING.md, "The keys"; docs/PORT.md, "A report (F9)").
 *
 * All the while the window keeps the last seconds of where the time went
 * (the processor's parts of every blank, the card's passes, when each
 * picture was handed to the display) in rings of its own, which cost a
 * few stores a blank. The key makes a folder beside the session's log,
 * `logs/report-YYYYMMDD-HHMMSS/`, and puts in it:
 *
 *   picture.png   the window as the player saw it when the key went down
 *   profile.txt   those seconds, in the form --profile writes
 *                 (tools/port_profile.py reads it)
 *   session.log   the session's log up to the key, its last part where it
 *                 is long
 *   rebushido.cfg the settings file the session read
 *   about.txt     the build, the system, the card and its driver, the
 *                 sound card, the display, and what the window runs with
 *
 * so that a report from the owner's PC says where the time goes on it. */
#include <SDL.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "gl.h"
#include "gl_bench.h"
#include "port_profile.h"
#include "sdl_window.h"

#define PATH_BYTES_MAX 1024
#define EPOCH_YEAR 1900
/* What is kept: ten seconds of blanks, and of pictures at up to 200 a
 * second. */
#define REPORT_BLANKS 600
#define REPORT_PICTURES 2048
/* The log's last part, where it is longer. */
#define LOG_TAIL_BYTES (256 * 1024)
#define COPY_BYTES 65536

typedef struct {
    u32 blank;
    s32 is_new_frame;
    u32 cpu[PORT_PROFILE_PARTS]; /* microseconds */
} PortReportBlank;

typedef struct {
    u32 blank;
    u32 gpu[PORT_GPU_PASSES]; /* microseconds */
} PortReportCard;

typedef struct {
    unsigned long long at; /* microseconds since the window opened */
    s32 phase;
} PortReportPicture;

static PortReportBlank g_port_report_blanks[REPORT_BLANKS];
static u32 g_port_report_blank_count;
static PortReportCard g_port_report_cards[REPORT_BLANKS];
static u32 g_port_report_card_count;
static PortReportPicture g_port_report_pictures[REPORT_PICTURES];
static u32 g_port_report_picture_count;
static u32 g_port_reports;

void port_ReportNoteBlank(u32 blank, s32 is_new_frame, const u32 *cpu) {
    PortReportBlank *at =
        &g_port_report_blanks[g_port_report_blank_count++ % REPORT_BLANKS];

    at->blank = blank;
    at->is_new_frame = is_new_frame;
    memcpy(at->cpu, cpu, sizeof(at->cpu));
}

void port_ReportNoteCard(u32 blank, const u32 *nanoseconds) {
    PortReportCard *at =
        &g_port_report_cards[g_port_report_card_count++ % REPORT_BLANKS];
    s32 i;

    at->blank = blank;
    for (i = 0; i < PORT_GPU_PASSES; i++) {
        at->gpu[i] = nanoseconds[i] / 1000U;
    }
}

void port_ReportNoteSwap(unsigned long long microseconds, s32 phase) {
    PortReportPicture *at =
        &g_port_report_pictures[g_port_report_picture_count++ %
                                REPORT_PICTURES];

    at->at = microseconds;
    at->phase = phase;
}

/* The rings, oldest first, as --profile writes them: `b` lines, `g` lines
 * and `p` lines in the order they were kept. */
static void port_WriteReportProfile(FILE *out) {
    u32 first;
    u32 n;
    s32 i;

    fprintf(out, "parts");
    for (i = 0; i < PORT_PROFILE_PARTS; i++) {
        fprintf(out, " %s", port_ProfileName(i));
    }
    fprintf(out, "\ngpu_parts");
    for (i = 0; i < PORT_GPU_PASSES; i++) {
        fprintf(out, " %s", port_GlBenchName(i));
    }
    fputc('\n', out);
    first = g_port_report_blank_count > REPORT_BLANKS
                ? g_port_report_blank_count - REPORT_BLANKS
                : 0;
    for (n = first; n < g_port_report_blank_count; n++) {
        const PortReportBlank *b = &g_port_report_blanks[n % REPORT_BLANKS];

        fprintf(out, "b %u %d", b->blank, b->is_new_frame);
        for (i = 0; i < PORT_PROFILE_PARTS; i++) {
            fprintf(out, " %u", b->cpu[i]);
        }
        fputc('\n', out);
    }
    first = g_port_report_card_count > REPORT_BLANKS
                ? g_port_report_card_count - REPORT_BLANKS
                : 0;
    for (n = first; n < g_port_report_card_count; n++) {
        const PortReportCard *g = &g_port_report_cards[n % REPORT_BLANKS];

        fprintf(out, "g %u", g->blank);
        for (i = 0; i < PORT_GPU_PASSES; i++) {
            fprintf(out, " %u", g->gpu[i]);
        }
        fputc('\n', out);
    }
    first = g_port_report_picture_count > REPORT_PICTURES
                ? g_port_report_picture_count - REPORT_PICTURES
                : 0;
    for (n = first; n < g_port_report_picture_count; n++) {
        const PortReportPicture *p =
            &g_port_report_pictures[n % REPORT_PICTURES];

        fprintf(out, "p %llu %d\n", (unsigned long long)p->at, p->phase);
    }
}

/* A file copied, from `skip` bytes before its end (all of it where it is
 * shorter): 1 if it could be. */
static s32 port_CopyFile(const char *from, const char *to, long tail) {
    static char bytes[COPY_BYTES];
    FILE *in = fopen(from, "rb");
    FILE *out;
    size_t got;

    if (in == NULL) {
        return 0;
    }
    out = fopen(to, "wb");
    if (out == NULL) {
        fclose(in);
        return 0;
    }
    if (tail > 0 && fseek(in, 0, SEEK_END) == 0 && ftell(in) > tail) {
        fseek(in, -tail, SEEK_END);
        fputs("... (the log's start is in the session's own file)\n", out);
    } else {
        fseek(in, 0, SEEK_SET);
    }
    while ((got = fread(bytes, 1, sizeof(bytes), in)) > 0) {
        fwrite(bytes, 1, got, out);
    }
    fclose(in);
    fclose(out);
    return 1;
}

/* The folder a report goes in, made: beside the session's log, or in
 * logs/ beside the program. */
s32 port_ReportFolder(char *out, u32 room) {
    char logs[PATH_BYTES_MAX];
    const char *log = port_GetLogPath();
    time_t now = time(NULL);
    const struct tm *when = localtime(&now);

    if (when == NULL) {
        return 0;
    }
    if (log != NULL) {
        const char *slash = strrchr(log, '/');
        const char *back = strrchr(log, '\\');

        if (back != NULL && (slash == NULL || back > slash)) {
            slash = back;
        }
        snprintf(logs, sizeof(logs), "%.*s",
                 slash != NULL ? (int)(slash - log) : 0, log);
    } else {
        char *base = SDL_GetBasePath();

        snprintf(logs, sizeof(logs), "%slogs", base != NULL ? base : "");
        SDL_free(base);
        port_MakeFolder(logs);
    }
    snprintf(out, room, "%s/report-%04d%02d%02d-%02d%02d%02d-%u", logs,
             when->tm_year + EPOCH_YEAR, when->tm_mon + 1, when->tm_mday,
             when->tm_hour, when->tm_min, when->tm_sec, g_port_reports++);
    port_MakeFolder(out);
    return 1;
}

/* Everything but the picture, into `folder`; `about` is what the window
 * knows of itself (port_DescribeForReport). */
void port_WriteReport(const char *folder, const char *about) {
    char path[PATH_BYTES_MAX];
    const char *log = port_GetLogPath();
    const char *settings = port_GetSettingsPath();
    FILE *out;

    snprintf(path, sizeof(path), "%s/profile.txt", folder);
    out = fopen(path, "w");
    if (out != NULL) {
        port_WriteReportProfile(out);
        fclose(out);
    }
    snprintf(path, sizeof(path), "%s/about.txt", folder);
    out = fopen(path, "w");
    if (out != NULL) {
        fputs(about, out);
        fclose(out);
    }
    if (settings != NULL) {
        snprintf(path, sizeof(path), "%s/rebushido.cfg", folder);
        port_CopyFile(settings, path, 0);
    }
    if (log != NULL) {
        snprintf(path, sizeof(path), "%s/session.log", folder);
        port_CopyFile(log, path, LOG_TAIL_BYTES);
    }
}
