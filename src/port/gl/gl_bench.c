/* The card's own time, pass by pass, and the benchmark's stress
 * (gl_bench.h; docs/PORT.md, "Measuring on the card").
 *
 * A blank's passes are a run of marks, each a timestamp of the card's
 * and the pass that runs from it (or none). The marks of a blank are kept
 * in one of RING slots until the card has passed the last of them; then
 * the time from each mark to the next is added to its pass. Nothing here
 * changes what is drawn: a timestamp is a command the card notes the
 * time at and does nothing else for. */
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#include "common.h"
#include "gl.h"
#include "gl_bench.h"

/* ---- GL, as much of it as this file uses ---- */

typedef unsigned int GLenum;
typedef int GLint;
typedef unsigned int GLuint;
typedef int GLsizei;
typedef unsigned long long GLuint64;

#define GL_TIMESTAMP 0x8E28
#define GL_QUERY_RESULT 0x8866
#define GL_QUERY_RESULT_AVAILABLE 0x8867

#define PORT_GL_BENCH_FUNCTIONS                                                \
    GL(void, glGenQueries, (GLsizei, GLuint *))                                \
    GL(void, glQueryCounter, (GLuint, GLenum))                                 \
    GL(void, glGetQueryObjectiv, (GLuint, GLenum, GLint *))                    \
    GL(void, glGetQueryObjectui64v, (GLuint, GLenum, GLuint64 *))              \
    GL(void, glFlush, (void))

#define GL(result, name, args) static result(*name) args;
PORT_GL_BENCH_FUNCTIONS
#undef GL

/* Blanks the card may be behind before one is not kept: a swap holds
 * the program to two or three. */
#define RING 8
/* A blank's marks: a pass is two, and a fight's blank with frames run
 * ahead and pictures between frames comes to some hundreds. */
#define MARKS_MAX 2048
/* Passes inside passes. */
#define DEPTH_MAX 8
/* The pass of a mark after which nothing runs. */
#define IDLE 0xFF

enum { SLOT_FREE, SLOT_OPEN, SLOT_CLOSED };

typedef struct {
    s32 state;
    u32 blank;
    s32 count;
    s32 is_lost;
    GLuint query[MARKS_MAX];
    u8 pass[MARKS_MAX];
} BenchSlot;

/* A blank read, waiting to be taken. */
typedef struct {
    u32 blank;
    u32 nanoseconds[PORT_GPU_PASSES];
} BenchDone;

static struct {
    s32 is_able;
    s32 has_queries;
    BenchSlot *slots;
    s32 open;   /* the slot of the blank being drawn */
    s32 oldest; /* the oldest slot not yet read */
    u8 stack[DEPTH_MAX];
    s32 depth;
    BenchDone done[RING];
    s32 done_first;
    s32 done_count;
    u32 lost;
    u32 timed;
    s32 flushes;
    s32 fighter_copies;
    s32 stage_copies;
} g_bench;

s32 g_port_gpu_on;
s32 g_port_gpu_base = PORT_GPU_SCENE;

static const char * const g_bench_names[PORT_GPU_PASSES] = {
    "scene",   "vram",  "stage",   "fighters", "shadow", "sky",
    "resolve", "tween", "present", "cloth",    "skin",   "flayer",
    "flay",    "smask", "sdark",   "sun",
};

const char *port_GlBenchName(s32 pass) {
    return pass >= 0 && pass < PORT_GPU_PASSES ? g_bench_names[pass] : "?";
}

const char *port_GlBenchStart(void *(*getproc)(const char *name)) {
#define GL(result, name, args)                                                 \
    name = (result(*) args)getproc(#name);                                     \
    if (name == NULL) {                                                        \
        return "OpenGL has no " #name;                                         \
    }
    PORT_GL_BENCH_FUNCTIONS
#undef GL
    g_bench.is_able = TRUE;
    return NULL;
}

/* The slots and their queries, at the first time taken. */
static s32 port_GlBenchMake(void) {
    s32 i;

    if (g_bench.slots != NULL) {
        return TRUE;
    }
    g_bench.slots = (BenchSlot *)calloc(RING, sizeof(BenchSlot));
    if (g_bench.slots == NULL) {
        return FALSE;
    }
    for (i = 0; i < RING; i++) {
        glGenQueries(MARKS_MAX, g_bench.slots[i].query);
    }
    g_bench.slots[0].state = SLOT_OPEN;
    return TRUE;
}

void port_GlBenchTime(s32 on) {
    if (on && (!g_bench.is_able || !port_GlBenchMake())) {
        on = FALSE;
    }
    g_port_gpu_on = on;
    g_bench.depth = 0;
    g_bench.flushes = getenv("RB_GPU_FLUSH") != NULL;
}

static void port_GlBenchMark(s32 pass) {
    BenchSlot *slot = &g_bench.slots[g_bench.open];

    if (slot->state != SLOT_OPEN) {
        return;
    }
    if (slot->count == MARKS_MAX) {
        slot->is_lost = TRUE;
        return;
    }
    glQueryCounter(slot->query[slot->count], GL_TIMESTAMP);
    slot->pass[slot->count] = (u8)pass;
    slot->count++;
}

void port_GlBenchBegin(s32 pass) {
    if (g_bench.depth < DEPTH_MAX) {
        g_bench.stack[g_bench.depth] = (u8)pass;
    }
    g_bench.depth++;
    port_GlBenchMark(pass);
}

void port_GlBenchEnd(void) {
    if (g_bench.depth == 0) {
        return;
    }
    g_bench.depth--;
    port_GlBenchMark(g_bench.depth > 0 && g_bench.depth <= DEPTH_MAX
                         ? g_bench.stack[g_bench.depth - 1]
                         : IDLE);
    if (g_bench.flushes) {
        glFlush();
    }
}

/* The slot's marks into time per pass, where the card has passed the
 * last of them: FALSE where it has not yet. */
static s32 port_GlBenchRead(BenchSlot *slot) {
    BenchDone *done;
    GLuint64 before = 0;
    GLint is_there = 0;
    s32 i;

    if (slot->count > 0) {
        glGetQueryObjectiv(
            slot->query[slot->count - 1], GL_QUERY_RESULT_AVAILABLE, &is_there);
        if (!is_there) {
            return FALSE;
        }
    }
    if (slot->is_lost || g_bench.done_count == RING) {
        g_bench.lost++;
        return TRUE;
    }
    done = &g_bench.done[(g_bench.done_first + g_bench.done_count) % RING];
    memset(done, 0, sizeof(*done));
    done->blank = slot->blank;
    for (i = 0; i < slot->count; i++) {
        GLuint64 now = 0;

        glGetQueryObjectui64v(slot->query[i], GL_QUERY_RESULT, &now);
        if (i > 0 && slot->pass[i - 1] < PORT_GPU_PASSES && now > before) {
            done->nanoseconds[slot->pass[i - 1]] += (u32)(now - before);
        }
        before = now;
    }
    g_bench.done_count++;
    g_bench.timed++;
    return TRUE;
}

void port_GlBenchBlank(u32 blank) {
    BenchSlot *slot;
    s32 pass;

    if (!g_port_gpu_on) {
        return;
    }
    slot = &g_bench.slots[g_bench.open];
    /* a pass still running is cut at the blank, and goes on in the next */
    pass = g_bench.depth > 0 && g_bench.depth <= DEPTH_MAX
               ? g_bench.stack[g_bench.depth - 1]
               : IDLE;
    if (pass != IDLE) {
        port_GlBenchMark(IDLE);
    }
    slot->blank = blank;
    slot->state = SLOT_CLOSED;
    g_bench.open = (g_bench.open + 1) % RING;
    /* the card is a whole ring behind: the oldest is let go unread */
    if (g_bench.slots[g_bench.open].state == SLOT_CLOSED) {
        g_bench.lost++;
        g_bench.oldest = (g_bench.open + 1) % RING;
    }
    slot = &g_bench.slots[g_bench.open];
    slot->state = SLOT_OPEN;
    slot->count = 0;
    slot->is_lost = FALSE;
    if (pass != IDLE) {
        port_GlBenchMark(pass);
    }
    /* what the card has finished, oldest first */
    while (g_bench.oldest != g_bench.open &&
           g_bench.slots[g_bench.oldest].state == SLOT_CLOSED &&
           port_GlBenchRead(&g_bench.slots[g_bench.oldest])) {
        g_bench.slots[g_bench.oldest].state = SLOT_FREE;
        g_bench.oldest = (g_bench.oldest + 1) % RING;
    }
}

s32 port_GlBenchTake(u32 *blank, u32 *nanoseconds) {
    const BenchDone *done;

    if (g_bench.done_count == 0) {
        return FALSE;
    }
    done = &g_bench.done[g_bench.done_first];
    *blank = done->blank;
    memcpy(nanoseconds, done->nanoseconds, sizeof(done->nanoseconds));
    g_bench.done_first = (g_bench.done_first + 1) % RING;
    g_bench.done_count--;
    return TRUE;
}

u32 port_GlBenchLost(void) { return g_bench.lost; }

u32 port_GlBenchTimed(void) { return g_bench.timed; }

/* ---- the stress ---- */

static s32 port_GlBenchNumber(const char *text, s32 *out) {
    s32 value = 0;
    s32 digits = 0;

    while (*text >= '0' && *text <= '9' && digits < 4) {
        value = value * 10 + (*text - '0');
        text++;
        digits++;
    }
    if (digits == 0 || (*text != 0 && *text != ',') || value < 1) {
        return FALSE;
    }
    *out = value;
    return TRUE;
}

s32 port_GlBenchStress(const char *text) {
    while (*text != 0) {
        s32 value = 0;

        if (strncmp(text, "fighters=", 9) == 0 &&
            port_GlBenchNumber(text + 9, &value)) {
            /* two fighters on the stage: each drawn half as many times */
            g_bench.fighter_copies = (value + 1) / 2;
            text += 9;
        } else if (strncmp(text, "stage=", 6) == 0 &&
                   port_GlBenchNumber(text + 6, &value)) {
            g_bench.stage_copies = value;
            text += 6;
        } else {
            return FALSE;
        }
        while (*text != 0 && *text != ',') {
            text++;
        }
        if (*text == ',') {
            text++;
        }
    }
    return TRUE;
}

s32 port_GlBenchFighterCopies(void) {
    return g_bench.fighter_copies > 1 ? g_bench.fighter_copies : 1;
}

s32 port_GlBenchStageCopies(void) {
    return g_bench.stage_copies > 1 ? g_bench.stage_copies : 1;
}
