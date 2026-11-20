#ifndef GL_LOAD_H
#define GL_LOAD_H

/* A mod's pictures read and decoded away from the picture (docs/PORT.md,
 * "Where the time goes", "A mod's pictures, read beside the game"): a
 * PNG asked for is read from its file and decoded on a thread of its
 * own, and what it was asked for is given it on the thread that draws,
 * the next time that thread collects (port_GlLoadCollect), to send to
 * the card. Until then what asked draws what it would have drawn with
 * no mod: the game's own texture. A picture read at once is never more
 * than one picture late, so nothing the player sees waits on a file.
 *
 * Where the window has given no threads (the provers' build, which has
 * no window), or RB_TEXTURES_AT_ONCE is in the environment, a picture
 * is read and decoded where it is asked for and given at once, as it
 * always was: the same pictures, to the byte. */

#include "common.h"

/* What the window lends of its threads (SDL's). */
typedef struct {
    /* runs `body(arg)` on a new thread: FALSE where it cannot */
    s32 (*start)(s32 (*body)(void *arg), void *arg);
    void *(*make_lock)(void);
    void (*lock)(void *lock);
    void (*unlock)(void *lock);
    /* a count that `wait` takes one from, waiting for one where it is 0 */
    void *(*make_signal)(void);
    void (*wait)(void *signal);
    void (*post)(void *signal);
} PortGlThreads;

#define PORT_GL_LOAD_PROBLEM 160

typedef struct PortGlLoad PortGlLoad;

struct PortGlLoad {
    /* what is asked: the file, which lives as long as the mods do */
    const char *path;
    /* run on the reading thread after the PNG is decoded (the pixels'
     * colours already multiplied by their alpha), to make what `done`
     * sends: NULL to send the picture as it is. FALSE and a problem
     * where it cannot. */
    s32 (*prepare)(PortGlLoad *load);
    /* run on the drawing thread with what came of it */
    void (*done)(PortGlLoad *load);
    /* the asker's own, as it likes */
    void *owner;
    /* read where it is asked, and given before port_GlLoadAsk returns,
     * even where pictures are read beside the game: for what is drawn
     * over the picture on show, which nothing clears (gl_replace.c) */
    s32 at_once;
    u64 key;
    s32 tag;
    s32 side;
    s32 levels;
    u32 generation;
    /* what came of it */
    s32 is_loaded;
    s32 w;
    s32 h;
    u8 *pixels; /* freed after `done`: `prepare` may put others here */
    char problem[PORT_GL_LOAD_PROBLEM];
    /* the queue's */
    volatile s32 state;
    PortGlLoad *next;
};

/* The threads to read on, `count` of them (none where `threads` is
 * NULL). Given once, before the first picture is asked for. */
extern void port_GlLoadSetThreads(const PortGlThreads *threads, s32 count);
/* Whether a picture asked for is read beside the game (so is late). */
extern s32 port_GlLoadIsBeside(void);
/* A picture asked for: `load` filled in as far as `generation` and
 * copied; FALSE where there is no memory. Read at once, `done` is run
 * before this returns. */
extern s32 port_GlLoadAsk(const PortGlLoad *load);
/* What has been read since, given to whom asked: at most about `bytes`
 * of pixels, and at least one picture where any is ready. */
extern void port_GlLoadCollect(u32 bytes);
/* Every picture asked for, read and given: before the pictures' owners
 * forget what they asked (a new context). */
extern void port_GlLoadFinish(void);
/* How many pictures are being read now. */
extern s32 port_GlLoadPending(void);

#endif
