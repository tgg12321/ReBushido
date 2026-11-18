/* The software GPU's primitives painted beside the game (port_paint.h;
 * docs/PORT.md, "The software GPU beside the game").
 *
 * The words of the primitives and the settings go into a ring that each
 * of the painters reads in order, every one of them all of it: painter
 * `k` of `n` paints only the lines of video memory of the `k`th of `n`
 * bands of each 256 (port_raster.h), so a pixel is painted by one
 * painter alone, by every primitive in the order they came, and the
 * pixels are the same as where one thread paints them all.
 *
 * The game's thread counts the words it has given (`given`) and each
 * painter those it has painted (`done`); each square of video memory, 64
 * pixels by 8 lines, keeps the count at the end of the last primitive given
 * that may write it and the last that may read it. To read a square, the
 * game's thread waits until every painter has passed its writes; to write
 * one, its reads as well. A primitive that reads what one not yet painted
 * writes (a texture drawn into) waits the same way before it is given,
 * since its texels may be another painter's lines.
 *
 * A painter's lines are a band of each 256 lines one after another, so
 * that a small primitive is one painter's alone and the others pass it
 * by before they work anything of it out.
 *
 * Nothing here is the game's state (config/port_state.txt): what the
 * state puts back of the GPU, its settings, is given to the painters
 * again (port_PaintResync). Where no thread was started (the tests, the
 * provers, every program but the window), each word is painted as it is
 * given by one painter of all the lines, and all of this is two tests of
 * a flag. */
#include "common.h"
#include "port_gpu.h"
#include "port_host.h"
#include "port_paint.h"
#include "port_raster.h"
#include "bb2.h"

/* The ring: a fight frame is some 60,000 words, so a painter is never a
 * frame behind for want of room. */
#define RING_WORDS (1U << 20)
#define RING_MASK (RING_WORDS - 1)
/* The squares: 64 pixels across and 8 lines down, 16 across and 64 down:
 * low enough that the colour tables under a screen are not its lines. */
#define SQUARE_SHIFT 6
#define SQUARE_LINES_SHIFT 3
#define SQUARES_ACROSS (PORT_VRAM_WIDTH >> SQUARE_SHIFT)
#define SQUARES_DOWN (PORT_VRAM_HEIGHT >> SQUARE_LINES_SHIFT)
#define SQUARES (SQUARES_ACROSS * SQUARES_DOWN)
/* A wait spins this many times before it gives up the processor, and a
 * painter, with nothing to paint, this many before it sleeps. */
#define SPINS_BEFORE_YIELD 4000
#define SPINS_BEFORE_SLEEP 2000
/* The squares' counts are taken afresh at least this often, in words,
 * so that none is so old that it reads as one not yet given. */
#define SQUARES_FRESH (1U << 28)
/* What one painter's counters take, so that two painters never share a
 * line of the processor's cache. */
#define CACHE_LINE 64
/* A word no command begins with in the ring (a GP0 no-op's code, which
 * is never given): the next word is how many words after it the first
 * painter does alone (port_PaintGiveAlone). */
#define PAINT_ALONE 0x00A10E00U

/* What waited for the painters (port_PaintReport). */
enum {
    PAINT_WAIT_READ,
    PAINT_WAIT_WRITE,
    PAINT_WAIT_ALL,
    PAINT_WAIT_ROOM,
    PAINT_WAIT_TEXTURE,
    PAINT_WAIT_KINDS
};

/* One painter: its settings, what it has painted, and its sleep. */
typedef struct {
    PortGpu gpu;
    u32 done; /* words painted */
    s32 is_sleeping;
    s32 is_held;
    u32 painting; /* its ticks at work */
    u8 pad[CACHE_LINE];
} PortPainter;

typedef struct {
    s32 is_threaded;
    s32 count; /* painters */
    PortPaintHost host;
    u32 given;   /* words given (the game's thread) */
    u32 written; /* ... as the painters read it */
    s32 hold;    /* the game's thread asks the painters to hold still */
    s32 stop;
    s32 is_skipping; /* commands were not given since the settings */
    /* the words done alone: painters arrived at them, and the place in
     * the ring of the last the first painter has done */
    s32 arrived;
    u32 alone_done;
    u32 fresh; /* `given` when the squares were last taken afresh */
    u32 square_write[SQUARES];
    u32 square_read[SQUARES];
    /* ... and written by a primitive, which only its own painter's lines
     * are: a copy or a load done alone keeps its place among all of them */
    u32 square_drawn[SQUARES];
    /* the waits that found something still to paint, by what waited, and
     * their time in the host's ticks */
    u32 waits[PAINT_WAIT_KINDS];
    u32 waited[PAINT_WAIT_KINDS];
} PortPaintThreads;

static PortPaintThreads g_paint = {.count = 1};
static PortPainter g_painters[PORT_PAINTERS_MAX];
/* the ring, from the system's memory: the program's own variables have no
 * room for it beside the game's link space */
static u32 *g_paint_ring;

static u32 port_PaintLoad(const u32 *at) {
    return __atomic_load_n(at, __ATOMIC_ACQUIRE);
}

static void port_PaintPublish(u32 *at, u32 value) {
    __atomic_store_n(at, value, __ATOMIC_RELEASE);
}

static s32 port_PaintFlag(const s32 *at) {
    return __atomic_load_n(at, __ATOMIC_SEQ_CST);
}

static void port_PaintSetFlag(s32 *at, s32 value) {
    __atomic_store_n(at, value, __ATOMIC_SEQ_CST);
}

static void port_PaintSpin(void) {
#if defined(__i386__) || defined(__x86_64__)
    __builtin_ia32_pause();
#endif
}

s32 port_PaintIsThreaded(void) { return g_paint.is_threaded; }

/* The words every painter has painted. */
static u32 port_PaintDone(void) {
    u32 least = port_PaintLoad(&g_painters[0].done);
    s32 i;

    for (i = 1; i < g_paint.count; i++) {
        u32 one = port_PaintLoad(&g_painters[i].done);

        if ((s32)(one - least) < 0) {
            least = one;
        }
    }
    return least;
}

/* ---- the squares ---- */

/* The squares a rectangle covers, which goes round video memory's edges
 * as the GPU's addresses do: the first across and down, and how many. */
typedef struct {
    s32 x;
    s32 y;
    s32 across;
    s32 down;
} PortSquares;

static s32 port_PaintSquares(s32 x, s32 y, s32 w, s32 h, PortSquares *out) {
    if (w <= 0 || h <= 0) {
        return 0;
    }
    x &= PORT_VRAM_WIDTH - 1;
    y &= PORT_VRAM_HEIGHT - 1;
    out->x = x >> SQUARE_SHIFT;
    out->y = y >> SQUARE_LINES_SHIFT;
    out->across = w >= PORT_VRAM_WIDTH
                      ? SQUARES_ACROSS
                      : ((x + w - 1) >> SQUARE_SHIFT) - out->x + 1;
    out->down = h >= PORT_VRAM_HEIGHT
                    ? SQUARES_DOWN
                    : ((y + h - 1) >> SQUARE_LINES_SHIFT) - out->y + 1;
    if (out->across > SQUARES_ACROSS) {
        out->across = SQUARES_ACROSS;
    }
    if (out->down > SQUARES_DOWN) {
        out->down = SQUARES_DOWN;
    }
    return 1;
}

/* `value` set in each square of the rectangle. */
static void port_PaintMark(u32 *squares, const PortPaintRect *rect,
                           u32 value) {
    PortSquares at;
    s32 i;
    s32 j;

    if (!port_PaintSquares(rect->x, rect->y, rect->w, rect->h, &at)) {
        return;
    }
    for (j = 0; j < at.down; j++) {
        u32 *row = squares + ((at.y + j) % SQUARES_DOWN) * SQUARES_ACROSS;

        for (i = 0; i < at.across; i++) {
            row[(at.x + i) % SQUARES_ACROSS] = value;
        }
    }
}

/* The latest count of `need` and the squares of the rectangle. */
static u32 port_PaintNeeds(const u32 *squares, s32 x, s32 y, s32 w, s32 h,
                           u32 need) {
    PortSquares at;
    s32 i;
    s32 j;

    if (!port_PaintSquares(x, y, w, h, &at)) {
        return need;
    }
    for (j = 0; j < at.down; j++) {
        const u32 *row =
            squares + ((at.y + j) % SQUARES_DOWN) * SQUARES_ACROSS;

        for (i = 0; i < at.across; i++) {
            u32 one = row[(at.x + i) % SQUARES_ACROSS];

            if ((s32)(one - need) > 0) {
                need = one;
            }
        }
    }
    return need;
}

/* ---- waiting ---- */

/* Until every painter has painted `need` words. Painters held still are
 * read as they stand: nothing is given them while they are held. */
static void port_PaintWaitFor(u32 need, s32 kind) {
    s32 spins = 0;
    u32 start;

    if (g_paint.hold || (s32)(need - port_PaintDone()) <= 0) {
        return;
    }
    start = port_HostTicks();
    g_paint.waits[kind]++;
    while ((s32)(need - port_PaintDone()) > 0) {
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
    port_PaintWaitFor(port_PaintNeeds(g_paint.square_write, x, y, w, h,
                                      g_paint.given - RING_WORDS),
                      PAINT_WAIT_READ);
}

void port_PaintSyncWrite(s32 x, s32 y, s32 w, s32 h) {
    u32 need;

    if (!g_paint.is_threaded) {
        return;
    }
    need = port_PaintNeeds(g_paint.square_write, x, y, w, h,
                           g_paint.given - RING_WORDS);
    need = port_PaintNeeds(g_paint.square_read, x, y, w, h, need);
    port_PaintWaitFor(need, PAINT_WAIT_WRITE);
}

s32 port_PaintMayGiveWrites(void) { return g_paint.is_threaded; }

s32 port_PaintPainters(void) { return g_paint.count; }

s32 port_PaintIsReadPending(const PortPaintRect *rect) {
    u32 need;

    if (!g_paint.is_threaded) {
        return FALSE;
    }
    need = port_PaintNeeds(g_paint.square_read, rect->x, rect->y, rect->w,
                           rect->h, g_paint.given - RING_WORDS);
    return (s32)(need - port_PaintDone()) > 0;
}

void port_PaintGiveAlone(s32 count, const PortPaintRect *write,
                         const PortPaintRect *read,
                         const PortPaintRect *table) {
    u32 words[2];
    PortPaintRect none;
    u32 end;

    words[0] = PAINT_ALONE;
    words[1] = (u32)count;
    none.w = 0;
    none.h = 0;
    port_PaintSubmit(words, 2, &none, &none, &none);
    /* its places are done with only once all its words are */
    end = g_paint.given + (u32)count;
    port_PaintMark(g_paint.square_write, write, end);
    port_PaintMark(g_paint.square_read, read, end);
    port_PaintMark(g_paint.square_read, table, end);
}

s32 port_PaintIsInWay(s32 x, s32 y, s32 w, s32 h) {
    u32 need;

    if (!g_paint.is_threaded) {
        return FALSE;
    }
    need = port_PaintNeeds(g_paint.square_write, x, y, w, h,
                           g_paint.given - RING_WORDS);
    need = port_PaintNeeds(g_paint.square_read, x, y, w, h, need);
    return (s32)(need - port_PaintDone()) > 0;
}

void port_PaintSyncAll(void) {
    if (!g_paint.is_threaded) {
        return;
    }
    port_PaintWaitFor(g_paint.given, PAINT_WAIT_ALL);
}

void port_PaintResync(void) {
    s32 i;

    port_PaintSyncAll();
    for (i = 0; i < g_paint.count; i++) {
        port_PaintTakeSettings(&g_painters[i].gpu);
    }
}

void port_PaintHold(void) {
    s32 spins = 0;
    s32 i;

    if (!g_paint.is_threaded) {
        return;
    }
    port_PaintSetFlag(&g_paint.hold, 1);
    /* each held, or with nothing to paint, which it then cannot be given */
    for (i = 0; i < g_paint.count; i++) {
        PortPainter *painter = &g_painters[i];

        while (!port_PaintFlag(&painter->is_held) &&
               port_PaintLoad(&painter->done) != g_paint.given) {
            if (++spins < SPINS_BEFORE_YIELD) {
                port_PaintSpin();
            } else {
                g_paint.host.yield();
            }
        }
    }
}

void port_PaintLetGo(void) {
    if (!g_paint.is_threaded) {
        return;
    }
    port_PaintSetFlag(&g_paint.hold, 0);
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
            port_PaintGp0(&g_painters[0].gpu, words[i]);
        }
        return;
    }
    if (g_paint.given - g_paint.fresh > SQUARES_FRESH) {
        /* every square's count made one that is painted */
        port_PaintSyncAll();
        for (i = 0; i < SQUARES; i++) {
            g_paint.square_write[i] = g_paint.given;
            g_paint.square_read[i] = g_paint.given;
            g_paint.square_drawn[i] = g_paint.given;
        }
        g_paint.fresh = g_paint.given;
    }
    /* a texture another painter may still be drawing into */
    if (g_paint.count > 1 && (read->w > 0 || table->w > 0)) {
        u32 need = port_PaintNeeds(g_paint.square_drawn, read->x, read->y,
                                   read->w, read->h, g_paint.given - RING_WORDS);

        need = port_PaintNeeds(g_paint.square_drawn, table->x, table->y,
                               table->w, table->h, need);
        port_PaintWaitFor(need, PAINT_WAIT_TEXTURE);
    }
    /* room in the ring */
    port_PaintWaitFor(g_paint.given + (u32)count - RING_WORDS, PAINT_WAIT_ROOM);
    for (i = 0; i < count; i++) {
        g_paint_ring[(g_paint.given + (u32)i) & RING_MASK] = words[i];
    }
    g_paint.given += (u32)count;
    port_PaintMark(g_paint.square_write, write, g_paint.given);
    port_PaintMark(g_paint.square_drawn, write, g_paint.given);
    port_PaintMark(g_paint.square_read, read, g_paint.given);
    port_PaintMark(g_paint.square_read, table, g_paint.given);
    port_PaintPublish(&g_paint.written, g_paint.given);
    for (i = 0; i < g_paint.count; i++) {
        PortPainter *painter = &g_painters[i];

        if (port_PaintFlag(&painter->is_sleeping) &&
            __atomic_exchange_n(&painter->is_sleeping, 0, __ATOMIC_SEQ_CST)) {
            g_paint.host.wake(i);
        }
    }
}

/* ---- the painters' threads ---- */

s32 port_PaintStart(const PortPaintHost *host, s32 count) {
    s32 i;

    if (g_paint.is_threaded || count < 1) {
        return 0;
    }
    if (count > PORT_PAINTERS_MAX) {
        count = PORT_PAINTERS_MAX;
    }
    /* a power of two: a line's band is the top bits of its place in its
     * 256 */
    while (count & (count - 1)) {
        count &= count - 1;
    }
    if (g_paint_ring == NULL) {
        g_paint_ring = port_HostAllocate(RING_WORDS * sizeof(u32));
        if (g_paint_ring == NULL) {
            return 0;
        }
    }
    g_paint.host = *host;
    g_paint.count = count;
    g_paint.written = g_paint.given;
    g_paint.fresh = g_paint.given;
    g_paint.stop = 0;
    g_paint.hold = 0;
    for (i = 0; i < SQUARES; i++) {
        g_paint.square_write[i] = g_paint.given;
        g_paint.square_read[i] = g_paint.given;
        g_paint.square_drawn[i] = g_paint.given;
    }
    for (i = 0; i < count; i++) {
        PortPainter *painter = &g_painters[i];

        painter->done = g_paint.given;
        painter->is_sleeping = 0;
        painter->is_held = 0;
        painter->gpu.band = i;
        painter->gpu.band_bits = 0;
        while ((1 << painter->gpu.band_bits) < count) {
            painter->gpu.band_bits++;
        }
        port_PaintTakeSettings(&painter->gpu);
    }
    port_PaintSetFlag(&g_paint.is_threaded, 1);
    return count;
}

/* What waited for the painters, and how long, in the log. */
static void port_PaintReport(void) {
    static const char *const names[PAINT_WAIT_KINDS] = {
        "a read", "a write", "all", "room", "a texture drawn into"};
    s32 i;

    for (i = 0; i < PAINT_WAIT_KINDS; i++) {
        u32 us = g_paint.waited[i] / PORT_HOST_TICKS_PER_MICROSECOND;

        port_Log("rebushido: painters: %u waits for %s, %u.%03u ms in all\n",
                 g_paint.waits[i], names[i], us / 1000U, us % 1000U);
    }
    for (i = 0; i < g_paint.count; i++) {
        port_Log("rebushido: painter %d: %u ms painting\n", i,
                 g_painters[i].painting / PORT_HOST_TICKS_PER_MICROSECOND /
                     1000U);
    }
}

void port_PaintStop(void) {
    s32 i;

    if (!g_paint.is_threaded) {
        return;
    }
    port_PaintSyncAll();
    port_PaintReport();
    port_PaintSetFlag(&g_paint.stop, 1);
    for (i = 0; i < g_paint.count; i++) {
        port_PaintSetFlag(&g_painters[i].is_sleeping, 0);
        g_paint.host.wake(i);
    }
    port_PaintSetFlag(&g_paint.is_threaded, 0);
    /* one painter of all the lines from here on */
    g_paint.count = 1;
    g_painters[0].gpu.band = 0;
    g_painters[0].gpu.band_bits = 0;
}

/* Whether there is a reason to wake: words, a hold or the end. */
static s32 port_PaintHasNews(u32 at) {
    return port_PaintLoad(&g_paint.written) != at ||
           port_PaintFlag(&g_paint.hold) || port_PaintFlag(&g_paint.stop);
}

/* The ring's word at `at`, once it is there. */
static u32 port_PaintWord(u32 at) {
    s32 spins = 0;

    while (port_PaintLoad(&g_paint.written) - at - 1 >= RING_WORDS) {
        if (++spins < SPINS_BEFORE_YIELD) {
            port_PaintSpin();
        } else {
            g_paint.host.yield();
        }
    }
    return g_paint_ring[at & RING_MASK];
}

/* Words done alone (port_PaintGiveAlone), from `at`, which is their
 * marker: the first painter waits for the others to arrive and does
 * them, of all lines; the others arrive and wait for it. The place after
 * them. */
static u32 port_PaintAlone(PortPainter *painter, s32 index, u32 at) {
    u32 count = port_PaintWord(at + 1);
    u32 end = at + 2 + count;
    s32 spins = 0;

    if (index != 0) {
        __atomic_add_fetch(&g_paint.arrived, 1, __ATOMIC_SEQ_CST);
        while (port_PaintLoad(&g_paint.alone_done) != end) {
            if (++spins < SPINS_BEFORE_YIELD) {
                port_PaintSpin();
            } else {
                g_paint.host.yield();
            }
        }
        return end;
    }
    while (port_PaintFlag(&g_paint.arrived) != g_paint.count - 1) {
        if (++spins < SPINS_BEFORE_YIELD) {
            port_PaintSpin();
        } else {
            g_paint.host.yield();
        }
    }
    port_PaintSetFlag(&g_paint.arrived, 0);
    {
        s32 band_bits = painter->gpu.band_bits;
        u32 i;

        painter->gpu.band_bits = 0;
        for (i = at + 2; i != end; i++) {
            port_PaintGp0(&painter->gpu, port_PaintWord(i));
        }
        painter->gpu.band_bits = band_bits;
    }
    port_PaintPublish(&g_paint.alone_done, end);
    return end;
}

void port_PaintRun(s32 index) {
    PortPainter *painter = &g_painters[index];
    u32 at = port_PaintLoad(&painter->done);

    for (;;) {
        u32 written;
        u32 start;

        if (port_PaintFlag(&g_paint.hold)) {
            s32 spins = 0;

            port_PaintSetFlag(&painter->is_held, 1);
            while (port_PaintFlag(&g_paint.hold) &&
                   !port_PaintFlag(&g_paint.stop)) {
                if (++spins < SPINS_BEFORE_YIELD) {
                    port_PaintSpin();
                } else {
                    g_paint.host.yield();
                }
            }
            port_PaintSetFlag(&painter->is_held, 0);
        }
        written = port_PaintLoad(&g_paint.written);
        if (at == written) {
            s32 spins;

            if (port_PaintFlag(&g_paint.stop)) {
                return;
            }
            for (spins = 0;
                 spins < SPINS_BEFORE_SLEEP && !port_PaintHasNews(at);
                 spins++) {
                port_PaintSpin();
            }
            if (port_PaintHasNews(at)) {
                continue;
            }
            port_PaintSetFlag(&painter->is_sleeping, 1);
            if (port_PaintHasNews(at)) {
                port_PaintSetFlag(&painter->is_sleeping, 0);
                continue;
            }
            g_paint.host.sleep(index);
            continue;
        }
        start = port_HostTicks();
        while (at != written) {
            u32 word = g_paint_ring[at & RING_MASK];

            if (word == PAINT_ALONE && painter->gpu.count == 0 &&
                !painter->gpu.is_in_line && !painter->gpu.load.is_active) {
                at = port_PaintAlone(painter, index, at);
                port_PaintPublish(&painter->done, at);
                written = port_PaintLoad(&g_paint.written);
                continue;
            }
            port_PaintGp0(&painter->gpu, word);
            at++;
            port_PaintPublish(&painter->done, at);
            /* held between two commands */
            if (painter->gpu.count == 0 &&
                __atomic_load_n(&g_paint.hold, __ATOMIC_RELAXED)) {
                break;
            }
        }
        painter->painting += port_HostTicks() - start;
    }
}
