/* The software GPU's primitives painted beside the game (port_paint.h;
 * docs/PORT.md, "The software GPU on a thread of its own").
 *
 * The words of the primitives and the settings go into a ring that one
 * thread, the painter, empties in order. The game's thread counts the
 * words it has given (`g_paint.given`) and the painter those it has
 * painted (`done`); each square of video memory, 64 by 64 pixels, keeps
 * the count at the end of the last primitive given that may write it and
 * the last that may read it. To read a square, the game's thread waits
 * until `done` has passed its writes; to write one, its reads as well.
 *
 * Nothing here is the game's state (config/port_state.txt): what the
 * state puts back of the GPU, its settings, is given to the painter
 * again (port_PaintResync). Where no thread was started (the tests, the
 * provers, every program but the window), each word is painted as it is
 * given, and all of this is two tests of a flag. */
#include "common.h"
#include "port_gpu.h"
#include "port_host.h"
#include "port_paint.h"
#include "port_raster.h"
#include "bb2.h"

/* The ring: a fight frame is some 60,000 words, so the painter is never
 * a frame behind for want of room. */
#define RING_WORDS (1U << 20)
#define RING_MASK (RING_WORDS - 1)
/* The squares: 16 across and 8 down. */
#define SQUARE_SHIFT 6
#define SQUARES_ACROSS (PORT_VRAM_WIDTH >> SQUARE_SHIFT)
#define SQUARES_DOWN (PORT_VRAM_HEIGHT >> SQUARE_SHIFT)
#define SQUARES (SQUARES_ACROSS * SQUARES_DOWN)
/* A wait spins this many times before it gives up the processor, and the
 * painter, with nothing to paint, this many before it sleeps. */
#define SPINS_BEFORE_YIELD 4000
#define SPINS_BEFORE_SLEEP 20000
/* The squares' counts are taken afresh at least this often, in words,
 * so that none is so old that it reads as one not yet given. */
#define SQUARES_FRESH (1U << 28)

PortGpu g_port_paint;

/* What waited for the painter (port_PaintReport). */
enum {
    PAINT_WAIT_READ,
    PAINT_WAIT_WRITE,
    PAINT_WAIT_ALL,
    PAINT_WAIT_ROOM,
    PAINT_WAIT_KINDS
};

typedef struct {
    s32 is_threaded;
    PortPaintHost host;
    u32 given; /* words given (the game's thread) */
    u32 written; /* ... as the painter reads it */
    u32 done;    /* words painted (the painter) */
    s32 is_sleeping;
    s32 hold;    /* the game's thread asks the painter to hold still */
    s32 is_held; /* ... and the painter does */
    s32 stop;
    s32 is_skipping; /* commands were not given since the settings */
    u32 fresh; /* `given` when the squares were last taken afresh */
    u32 square_write[SQUARES];
    u32 square_read[SQUARES];
    /* the waits that found something still to paint, by what waited, and
     * their time in the host's ticks */
    u32 waits[PAINT_WAIT_KINDS];
    u32 waited[PAINT_WAIT_KINDS];
    u32 painting; /* the painter's ticks at work */
    u32 held;     /* ... and held still */
} PortPaintThread;

static PortPaintThread g_paint;
/* the ring, from the system's memory: the program's own variables have no
 * room for it beside the game's link space */
static u32 *g_paint_ring;

static u32 port_PaintLoad(const u32 *at) {
    return __atomic_load_n(at, __ATOMIC_ACQUIRE);
}

static void port_PaintPublish(u32 *at, u32 value) {
    __atomic_store_n(at, value, __ATOMIC_RELEASE);
}

static void port_PaintSpin(void) {
#if defined(__i386__) || defined(__x86_64__)
    __builtin_ia32_pause();
#endif
}

s32 port_PaintIsThreaded(void) { return g_paint.is_threaded; }

/* ---- the squares ---- */

/* `value` set in each square of the rectangle, which goes round video
 * memory's edges as the GPU's addresses do. */
static void port_PaintMark(u32 *squares, s32 x, s32 y, s32 w, s32 h,
                           u32 value) {
    s32 first_x = (x & (PORT_VRAM_WIDTH - 1)) >> SQUARE_SHIFT;
    s32 first_y = (y & (PORT_VRAM_HEIGHT - 1)) >> SQUARE_SHIFT;
    s32 across;
    s32 down;
    s32 i;
    s32 j;

    if (w <= 0 || h <= 0) {
        return;
    }
    across = w >= PORT_VRAM_WIDTH ? SQUARES_ACROSS
                                  : (((x & (PORT_VRAM_WIDTH - 1)) + w - 1) >>
                                     SQUARE_SHIFT) -
                                        first_x + 1;
    down = h >= PORT_VRAM_HEIGHT ? SQUARES_DOWN
                                 : (((y & (PORT_VRAM_HEIGHT - 1)) + h - 1) >>
                                    SQUARE_SHIFT) -
                                       first_y + 1;
    if (across > SQUARES_ACROSS) {
        across = SQUARES_ACROSS;
    }
    if (down > SQUARES_DOWN) {
        down = SQUARES_DOWN;
    }
    for (j = 0; j < down; j++) {
        u32 *row = squares + ((first_y + j) % SQUARES_DOWN) * SQUARES_ACROSS;

        for (i = 0; i < across; i++) {
            row[(first_x + i) % SQUARES_ACROSS] = value;
        }
    }
}

/* The latest count the squares of the rectangle wait for, or `done` if
 * none. */
static u32 port_PaintNeeds(const u32 *squares, s32 x, s32 y, s32 w, s32 h,
                           u32 need) {
    s32 first_x = (x & (PORT_VRAM_WIDTH - 1)) >> SQUARE_SHIFT;
    s32 first_y = (y & (PORT_VRAM_HEIGHT - 1)) >> SQUARE_SHIFT;
    s32 across;
    s32 down;
    s32 i;
    s32 j;

    if (w <= 0 || h <= 0) {
        return need;
    }
    across = w >= PORT_VRAM_WIDTH ? SQUARES_ACROSS
                                  : (((x & (PORT_VRAM_WIDTH - 1)) + w - 1) >>
                                     SQUARE_SHIFT) -
                                        first_x + 1;
    down = h >= PORT_VRAM_HEIGHT ? SQUARES_DOWN
                                 : (((y & (PORT_VRAM_HEIGHT - 1)) + h - 1) >>
                                    SQUARE_SHIFT) -
                                       first_y + 1;
    if (across > SQUARES_ACROSS) {
        across = SQUARES_ACROSS;
    }
    if (down > SQUARES_DOWN) {
        down = SQUARES_DOWN;
    }
    for (j = 0; j < down; j++) {
        const u32 *row =
            squares + ((first_y + j) % SQUARES_DOWN) * SQUARES_ACROSS;

        for (i = 0; i < across; i++) {
            u32 one = row[(first_x + i) % SQUARES_ACROSS];

            if ((s32)(one - need) > 0) {
                need = one;
            }
        }
    }
    return need;
}

/* ---- waiting ---- */

/* Until the painter has painted `need` words. A painter held still is
 * read as it stands: nothing is given it while it is held. */
static void port_PaintWaitFor(u32 need, s32 kind) {
    s32 spins = 0;
    u32 start;

    if (g_paint.hold || (s32)(need - port_PaintLoad(&g_paint.done)) <= 0) {
        return;
    }
    start = port_HostTicks();
    g_paint.waits[kind]++;
    while ((s32)(need - port_PaintLoad(&g_paint.done)) > 0) {
        if (++spins < SPINS_BEFORE_YIELD) {
            port_PaintSpin();
        } else {
            g_paint.host.yield();
        }
    }
    g_paint.waited[kind] += port_HostTicks() - start;
}

void port_PaintSyncRead(s32 x, s32 y, s32 w, s32 h) {
    if (!g_paint.is_threaded) {
        return;
    }
    port_PaintWaitFor(
        port_PaintNeeds(g_paint.square_write, x, y, w, h, g_paint.done),
        PAINT_WAIT_READ);
}

void port_PaintSyncWrite(s32 x, s32 y, s32 w, s32 h) {
    u32 need;

    if (!g_paint.is_threaded) {
        return;
    }
    need = port_PaintNeeds(g_paint.square_write, x, y, w, h, g_paint.done);
    need = port_PaintNeeds(g_paint.square_read, x, y, w, h, need);
    port_PaintWaitFor(need, PAINT_WAIT_WRITE);
}

void port_PaintSyncAll(void) {
    if (!g_paint.is_threaded) {
        return;
    }
    port_PaintWaitFor(g_paint.given, PAINT_WAIT_ALL);
}

void port_PaintResync(void) {
    port_PaintSyncAll();
    port_PaintTakeSettings();
}

void port_PaintHold(void) {
    s32 spins = 0;

    if (!g_paint.is_threaded) {
        return;
    }
    __atomic_store_n(&g_paint.hold, 1, __ATOMIC_SEQ_CST);
    /* held, or with nothing to paint, which it then cannot be given */
    while (!__atomic_load_n(&g_paint.is_held, __ATOMIC_SEQ_CST) &&
           port_PaintLoad(&g_paint.done) != g_paint.given) {
        if (++spins < SPINS_BEFORE_YIELD) {
            port_PaintSpin();
        } else {
            g_paint.host.yield();
        }
    }
}

void port_PaintLetGo(void) {
    if (!g_paint.is_threaded) {
        return;
    }
    __atomic_store_n(&g_paint.hold, 0, __ATOMIC_SEQ_CST);
}

/* ---- giving ---- */

void port_PaintSkipped(void) { g_paint.is_skipping = 1; }

void port_PaintWillPaint(void) {
    if (g_paint.is_skipping) {
        g_paint.is_skipping = 0;
        port_PaintResync();
    }
}

void port_PaintSubmit(const u32 *words, s32 count, const PortPaintRect *write,
                      const PortPaintRect *read, const PortPaintRect *table) {
    s32 i;

    if (!g_paint.is_threaded) {
        for (i = 0; i < count; i++) {
            port_PaintGp0(words[i]);
        }
        return;
    }
    if (g_paint.given - g_paint.fresh > SQUARES_FRESH) {
        /* every square's count made one that is painted */
        port_PaintSyncAll();
        for (i = 0; i < SQUARES; i++) {
            g_paint.square_write[i] = g_paint.given;
            g_paint.square_read[i] = g_paint.given;
        }
        g_paint.fresh = g_paint.given;
    }
    /* room in the ring */
    port_PaintWaitFor(g_paint.given + (u32)count - RING_WORDS, PAINT_WAIT_ROOM);
    for (i = 0; i < count; i++) {
        g_paint_ring[(g_paint.given + (u32)i) & RING_MASK] = words[i];
    }
    g_paint.given += (u32)count;
    port_PaintMark(g_paint.square_write, write->x, write->y, write->w,
                   write->h, g_paint.given);
    port_PaintMark(g_paint.square_read, read->x, read->y, read->w, read->h,
                   g_paint.given);
    port_PaintMark(g_paint.square_read, table->x, table->y, table->w,
                   table->h, g_paint.given);
    port_PaintPublish(&g_paint.written, g_paint.given);
    if (__atomic_load_n(&g_paint.is_sleeping, __ATOMIC_SEQ_CST) &&
        __atomic_exchange_n(&g_paint.is_sleeping, 0, __ATOMIC_SEQ_CST)) {
        g_paint.host.wake();
    }
}

/* ---- the painter's thread ---- */

s32 port_PaintStart(const PortPaintHost *host) {
    s32 i;

    if (g_paint.is_threaded) {
        return 0;
    }
    if (g_paint_ring == NULL) {
        g_paint_ring = port_HostAllocate(RING_WORDS * sizeof(u32));
        if (g_paint_ring == NULL) {
            return 0;
        }
    }
    g_paint.host = *host;
    g_paint.done = g_paint.given;
    g_paint.written = g_paint.given;
    g_paint.fresh = g_paint.given;
    g_paint.stop = 0;
    g_paint.hold = 0;
    g_paint.is_held = 0;
    g_paint.is_sleeping = 0;
    for (i = 0; i < SQUARES; i++) {
        g_paint.square_write[i] = g_paint.given;
        g_paint.square_read[i] = g_paint.given;
    }
    __atomic_store_n(&g_paint.is_threaded, 1, __ATOMIC_SEQ_CST);
    return 1;
}

/* What waited for the painter, and how long, in the log. */
static void port_PaintReport(void) {
    static const char *const names[PAINT_WAIT_KINDS] = {"a read", "a write",
                                                         "all", "room"};
    s32 i;

    for (i = 0; i < PAINT_WAIT_KINDS; i++) {
        u32 us = g_paint.waited[i] / PORT_HOST_TICKS_PER_MICROSECOND;

        port_Log("rebushido: painter: %u waits for %s, %u.%03u ms in all\n",
                 g_paint.waits[i], names[i], us / 1000U, us % 1000U);
    }
    port_Log("rebushido: painter: %u ms painting, %u ms held\n",
             g_paint.painting / PORT_HOST_TICKS_PER_MICROSECOND / 1000U,
             g_paint.held / PORT_HOST_TICKS_PER_MICROSECOND / 1000U);
}

void port_PaintStop(void) {
    if (!g_paint.is_threaded) {
        return;
    }
    port_PaintReport();
    port_PaintSyncAll();
    __atomic_store_n(&g_paint.stop, 1, __ATOMIC_SEQ_CST);
    __atomic_store_n(&g_paint.is_sleeping, 0, __ATOMIC_SEQ_CST);
    g_paint.host.wake();
    __atomic_store_n(&g_paint.is_threaded, 0, __ATOMIC_SEQ_CST);
}

/* Whether there is a reason to wake: words, a hold or the end. */
static s32 port_PaintHasNews(u32 at) {
    return port_PaintLoad(&g_paint.written) != at ||
           __atomic_load_n(&g_paint.hold, __ATOMIC_SEQ_CST) ||
           __atomic_load_n(&g_paint.stop, __ATOMIC_SEQ_CST);
}

void port_PaintRun(void) {
    u32 at = port_PaintLoad(&g_paint.done);

    for (;;) {
        u32 written;
        u32 start;

        if (__atomic_load_n(&g_paint.hold, __ATOMIC_SEQ_CST)) {
            __atomic_store_n(&g_paint.is_held, 1, __ATOMIC_SEQ_CST);
            s32 spins = 0;
            u32 held_at = port_HostTicks();

            while (__atomic_load_n(&g_paint.hold, __ATOMIC_SEQ_CST) &&
                   !__atomic_load_n(&g_paint.stop, __ATOMIC_SEQ_CST)) {
                if (++spins < SPINS_BEFORE_YIELD) {
                    port_PaintSpin();
                } else {
                    g_paint.host.yield();
                }
            }
            g_paint.held += port_HostTicks() - held_at;
            __atomic_store_n(&g_paint.is_held, 0, __ATOMIC_SEQ_CST);
        }
        written = port_PaintLoad(&g_paint.written);
        if (at == written) {
            s32 spins;

            if (__atomic_load_n(&g_paint.stop, __ATOMIC_SEQ_CST)) {
                return;
            }
            for (spins = 0; spins < SPINS_BEFORE_SLEEP && !port_PaintHasNews(at);
                 spins++) {
                port_PaintSpin();
            }
            if (port_PaintHasNews(at)) {
                continue;
            }
            __atomic_store_n(&g_paint.is_sleeping, 1, __ATOMIC_SEQ_CST);
            if (port_PaintHasNews(at)) {
                __atomic_store_n(&g_paint.is_sleeping, 0, __ATOMIC_SEQ_CST);
                continue;
            }
            g_paint.host.sleep();
            continue;
        }
        start = port_HostTicks();
        while (at != written) {
            port_PaintGp0(g_paint_ring[at & RING_MASK]);
            at++;
            port_PaintPublish(&g_paint.done, at);
            /* held between two commands */
            if (g_port_paint.count == 0 &&
                __atomic_load_n(&g_paint.hold, __ATOMIC_RELAXED)) {
                break;
            }
        }
        g_paint.painting += port_HostTicks() - start;
    }
}
