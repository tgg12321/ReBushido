#ifndef GL_BENCH_H
#define GL_BENCH_H

/* The card's own time, pass by pass, and the benchmark's stress (docs/
 * PORT.md, "Measuring on the card").
 *
 * Each pass of the remastered picture is bracketed by a pair of the
 * card's timestamps (GL_TIMESTAMP, glQueryCounter): one where its first
 * command is handed over, one after its last. What lies between two
 * passes, while the processor works out what to draw next, belongs to
 * none of them, so a blank's sum is the card's busy time and not its
 * wall time. A pass the processor feeds slowly from inside still counts
 * the card's waiting for it (a fighter's cloth worked out between two of
 * its draws): a pass's time is never less than its true cost.
 *
 * The answers are read a few blanks later, when the card has them, from
 * a ring of blanks: the program never waits for the card to be read.
 * Off (no --profile and the counter not showing times), a pass's
 * bracket is one test of a flag. */

#include "common.h"

typedef enum {
    PORT_GPU_SCENE,    /* the game's primitives, batched (gl_draw.c) */
    PORT_GPU_VRAM,     /* video memory sent, and its rectangles stretched */
    PORT_GPU_STAGE,    /* the ground as one mesh (gl_stage.c) */
    PORT_GPU_FIGHTERS, /* the HD fighters, their cloth, their layer laid */
    PORT_GPU_SHADOW,   /* the shadows' mask and the ground darkened */
    PORT_GPU_SKY,      /* the sky over the frame's clear (gl_sky.c) */
    PORT_GPU_RESOLVE,  /* the samples averaged down */
    PORT_GPU_TWEEN,    /* a picture between frames: the game's primitives
                        * drawn again (gl_tween.c) */
    PORT_GPU_PRESENT,  /* the picture into the window, the counter */
    PORT_GPU_CLOTH,    /* the HD bodies posed and the cloth made, by compute
                        * (gl_fighter_card.c) */
    PORT_GPU_PASSES
} PortGpuPass;

/* Whether the card's time is being taken. */
extern s32 g_port_gpu_on;
/* The pass the game's primitives are counted to: the scene's, or the
 * pictures between frames' while one is drawn. */
extern s32 g_port_gpu_base;

#define PORT_GPU_BEGIN(pass)                                                   \
    do {                                                                       \
        if (g_port_gpu_on) {                                                   \
            port_GlBenchBegin(pass);                                           \
        }                                                                      \
    } while (0)
#define PORT_GPU_END()                                                         \
    do {                                                                       \
        if (g_port_gpu_on) {                                                   \
            port_GlBenchEnd();                                                 \
        }                                                                      \
    } while (0)

/* The card's timer looked up: NULL, or why there is none (the rest of
 * the picture does not need it). */
extern const char *port_GlBenchStart(void *(*getproc)(const char *name));
/* Taken from now, or no longer. */
extern void port_GlBenchTime(s32 on);
/* A pass begins, inside whatever pass is running; and the latest ends. */
extern void port_GlBenchBegin(s32 pass);
extern void port_GlBenchEnd(void);
/* The blank's passes are all handed over: the blank is closed, and the
 * blanks the card has finished are read. */
extern void port_GlBenchBlank(u32 blank);
/* The oldest blank read and not yet taken: its number, and each pass's
 * time in nanoseconds (PORT_GPU_PASSES of them). FALSE where there is
 * none. */
extern s32 port_GlBenchTake(u32 *blank, u32 *nanoseconds);
/* A pass's short name, for a report. */
extern const char *port_GlBenchName(s32 pass);
/* Blanks whose times were not kept (the ring full, or more passes than a
 * blank holds), and blanks timed. */
extern u32 port_GlBenchLost(void);
extern u32 port_GlBenchTimed(void);

/* ---- the benchmark's stress (--bench-stress) ---- */

/* `fighters=N,stage=M`: the HD fighters drawn as if there were N of
 * them (each of the two drawn N/2 times, rounded up, the copies beside
 * him on the screen), and the ground's mesh drawn M times over. Nothing
 * of the game is touched: it is more work for the card and for the
 * processor that feeds it, for the benchmark (tools/port_bench.py).
 * FALSE where the text is not understood. */
extern s32 port_GlBenchStress(const char *text);
/* How many times each fighter, and the ground, is drawn: 1 unstressed. */
extern s32 port_GlBenchFighterCopies(void);
extern s32 port_GlBenchStageCopies(void);

#endif /* GL_BENCH_H */
