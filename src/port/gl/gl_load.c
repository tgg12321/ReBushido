/* A mod's pictures read and decoded beside the game (gl_load.h). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "common.h"
#include "gl_load.h"
#include "port_png.h"

#define RGBA 4
#define BYTE_MAX 255
/* the most threads read on */
#define THREADS_MAX 8

static struct {
    PortGlThreads threads;
    s32 is_beside;
    void *lock;
    void *asked_signal; /* one for each picture asked for */
    void *done_signal;  /* one for each picture read */
    PortGlLoad *asked;  /* the oldest first */
    PortGlLoad *asked_last;
    PortGlLoad *done;
    PortGlLoad *done_last;
    s32 pending; /* asked for and not yet given back */
} g_load;

/* ---- reading one ---- */

/* The whole file at `path`, and its size; NULL where it cannot be read.
 * The C library's own, which any thread may use (the host's files are
 * a table the game's thread keeps). */
static u8 *port_GlLoadFile(const char *path, u32 *size) {
    FILE *file = fopen(path, "rb");
    long length;
    u8 *bytes;

    if (file == NULL) {
        return NULL;
    }
    if (fseek(file, 0, SEEK_END) != 0 || (length = ftell(file)) <= 0 ||
        fseek(file, 0, SEEK_SET) != 0) {
        fclose(file);
        return NULL;
    }
    bytes = malloc((size_t)length);
    if (bytes != NULL &&
        fread(bytes, 1, (size_t)length, file) != (size_t)length) {
        free(bytes);
        bytes = NULL;
    }
    fclose(file);
    *size = (u32)length;
    return bytes;
}

/* The picture read, decoded, its colours multiplied by its alpha, and
 * made what was asked: on whichever thread it is. */
static void port_GlLoadRead(PortGlLoad *load) {
    PortPngInfo info;
    u32 size = 0;
    u8 *file = port_GlLoadFile(load->path, &size);
    u8 *work = NULL;
    u8 *rgba = NULL;
    u32 i;

    load->is_loaded = FALSE;
    load->pixels = NULL;
    if (file == NULL) {
        snprintf(load->problem, sizeof(load->problem), "it cannot be read");
    } else if (port_PngMeasure(
                   file, size, &info, load->problem, sizeof(load->problem))) {
        work = malloc(info.work_bytes);
        rgba = malloc((size_t)info.w * (size_t)info.h * RGBA);
        if (work == NULL || rgba == NULL) {
            snprintf(load->problem, sizeof(load->problem), "no memory for it");
        } else if (port_PngDecode(file, size, work, rgba, load->problem,
                                  sizeof(load->problem))) {
            /* kept multiplied by their alpha, so that smoothing a clear
             * texel into the next does not darken it; the shaders divide
             * them out again */
            for (i = 0; i < (u32)info.w * (u32)info.h * RGBA; i += RGBA) {
                rgba[i] = (u8)(rgba[i] * rgba[i + 3] / BYTE_MAX);
                rgba[i + 1] = (u8)(rgba[i + 1] * rgba[i + 3] / BYTE_MAX);
                rgba[i + 2] = (u8)(rgba[i + 2] * rgba[i + 3] / BYTE_MAX);
            }
            load->w = info.w;
            load->h = info.h;
            load->pixels = rgba;
            rgba = NULL;
            load->is_loaded =
                load->prepare != NULL ? load->prepare(load) : TRUE;
        }
    }
    free(file);
    free(work);
    free(rgba);
}

/* ---- the threads ---- */

static s32 port_GlLoadThread(void *arg) {
    (void)arg;
    for (;;) {
        PortGlLoad *load;

        g_load.threads.wait(g_load.asked_signal);
        g_load.threads.lock(g_load.lock);
        load = g_load.asked;
        if (load != NULL) {
            g_load.asked = load->next;
            if (g_load.asked == NULL) {
                g_load.asked_last = NULL;
            }
        }
        g_load.threads.unlock(g_load.lock);
        if (load == NULL) {
            continue;
        }
        port_GlLoadRead(load);
        load->next = NULL;
        g_load.threads.lock(g_load.lock);
        if (g_load.done_last != NULL) {
            g_load.done_last->next = load;
        } else {
            g_load.done = load;
        }
        g_load.done_last = load;
        g_load.threads.unlock(g_load.lock);
        g_load.threads.post(g_load.done_signal);
    }
    return 0;
}

void port_GlLoadSetThreads(const PortGlThreads *threads, s32 count) {
    const char *at_once = getenv("RB_TEXTURES_AT_ONCE");
    s32 started = 0;
    s32 i;

    if (threads == NULL || count <= 0 || g_load.lock != NULL) {
        return;
    }
    g_load.threads = *threads;
    if (at_once != NULL && at_once[0] != 0 && at_once[0] != '0') {
        return;
    }
    g_load.lock = threads->make_lock();
    g_load.asked_signal = threads->make_signal();
    g_load.done_signal = threads->make_signal();
    if (g_load.lock == NULL || g_load.asked_signal == NULL ||
        g_load.done_signal == NULL) {
        return;
    }
    if (count > THREADS_MAX) {
        count = THREADS_MAX;
    }
    /* beside the game from the first thread that starts: the rest only
     * read faster */
    for (i = 0; i < count; i++) {
        if (threads->start(port_GlLoadThread, NULL)) {
            started++;
        }
    }
    g_load.is_beside = started > 0;
}

s32 port_GlLoadIsBeside(void) { return g_load.is_beside; }

/* ---- asking and collecting ---- */

s32 port_GlLoadAsk(const PortGlLoad *asked) {
    PortGlLoad *load = malloc(sizeof(*load));

    if (load == NULL) {
        return FALSE;
    }
    *load = *asked;
    load->is_loaded = FALSE;
    load->pixels = NULL;
    load->problem[0] = 0;
    load->next = NULL;
    if (!g_load.is_beside) {
        port_GlLoadRead(load);
        load->done(load);
        free(load->pixels);
        free(load);
        return TRUE;
    }
    g_load.threads.lock(g_load.lock);
    if (g_load.asked_last != NULL) {
        g_load.asked_last->next = load;
    } else {
        g_load.asked = load;
    }
    g_load.asked_last = load;
    g_load.pending++;
    g_load.threads.unlock(g_load.lock);
    g_load.threads.post(g_load.asked_signal);
    return TRUE;
}

void port_GlLoadCollect(u32 bytes) {
    u32 sent = 0;

    if (!g_load.is_beside) {
        return;
    }
    while (sent < bytes || sent == 0) {
        PortGlLoad *load;

        g_load.threads.lock(g_load.lock);
        load = g_load.done;
        if (load != NULL) {
            g_load.done = load->next;
            if (g_load.done == NULL) {
                g_load.done_last = NULL;
            }
            g_load.pending--;
        }
        g_load.threads.unlock(g_load.lock);
        if (load == NULL) {
            return;
        }
        load->done(load);
        sent += load->is_loaded ? (u32)load->w * (u32)load->h * RGBA : 1;
        free(load->pixels);
        free(load);
    }
}

void port_GlLoadFinish(void) {
    if (!g_load.is_beside) {
        return;
    }
    for (;;) {
        s32 reading;

        port_GlLoadCollect(0xFFFFFFFFU);
        g_load.threads.lock(g_load.lock);
        reading = g_load.pending;
        g_load.threads.unlock(g_load.lock);
        if (reading == 0) {
            return;
        }
        /* a picture's end, or one counted before and collected since:
         * either way, look again */
        g_load.threads.wait(g_load.done_signal);
    }
}

s32 port_GlLoadPending(void) {
    s32 reading;

    if (!g_load.is_beside) {
        return 0;
    }
    g_load.threads.lock(g_load.lock);
    reading = g_load.pending;
    g_load.threads.unlock(g_load.lock);
    return reading;
}
