#ifndef PORT_PAINT_H
#define PORT_PAINT_H

/* The software GPU's painting beside the game (src/port/paint.c;
 * docs/PORT.md, "The software GPU on a thread of its own").
 *
 * src/port/raster.c takes every word written to GP0 on the game's
 * thread: the settings, the copies into, out of and within video memory,
 * and the fill are done there as they come. The primitives (polygons,
 * rectangles, lines), which are nearly all of its time, are painted from
 * the same words by a painter that keeps its own copy of the settings:
 * on the game's thread at once, or, where the host has started a thread
 * for it (the window), on that thread, in the order they came, while the
 * game goes on.
 *
 * Nothing on the game's thread may then read or write video memory that
 * a primitive not yet painted writes or reads. Each primitive marks the
 * squares of video memory it may write (its drawing area) and read (its
 * texture page and colour table); a read of video memory waits for the
 * primitives that write it, a write for those that write or read it
 * (port_PaintSyncRead, port_PaintSyncWrite). What reads all of it -- the
 * state saved, put back or hashed, the console's own picture -- waits for
 * them all (port_PaintSyncAll). A picture between frames, which writes
 * video memory and puts it back, holds the painter still while it does
 * (port_PaintHold). The pixels are the same, and in the same order, as
 * when the game's thread paints them itself.
 *
 * The painter's settings are not the game's state: src/port/state.c
 * puts the GPU's settings back, and they are given to the painter again
 * (port_PaintResync). */

#include "common.h"

/* ---- the painter's side, for src/port/raster.c ---- */

/* Words of one primitive or setting to be painted; `write_*` and `read_*`
 * are the rectangles of video memory it may write and read (w or h 0:
 * none; a second read for a colour table). */
typedef struct {
    s32 x;
    s32 y;
    s32 w;
    s32 h;
} PortPaintRect;

extern void port_PaintSubmit(const u32 *words, s32 count,
                             const PortPaintRect *write,
                             const PortPaintRect *read,
                             const PortPaintRect *table);
/* Commands not painted (a step run again, a frame ahead): the painter is
 * given none of them, settings included ... */
extern void port_PaintSkipped(void);
/* ... and, before the first that is painted again, takes the settings
 * the GPU has then. */
extern void port_PaintWillPaint(void);

/* ---- what waits for the painter ---- */

extern void port_PaintSyncRead(s32 x, s32 y, s32 w, s32 h);
extern void port_PaintSyncWrite(s32 x, s32 y, s32 w, s32 h);
extern void port_PaintSyncAll(void);
/* Whether a write of video memory may be given to the painters, to be
 * done in its turn: only where there is one, since a painter reads what
 * every other's lines hold (a colour table), and only it would keep the
 * order of a write and the primitives that read the place before. */
extern s32 port_PaintMayGiveWrites(void);
/* Whether a write of the rectangle would wait for the painters. */
extern s32 port_PaintIsInWay(s32 x, s32 y, s32 w, s32 h);
/* After the state is put back: everything painted, and the settings the
 * painter keeps taken from the GPU's again. */
extern void port_PaintResync(void);
/* The painter held still between two primitives, and let go. */
extern void port_PaintHold(void);
extern void port_PaintLetGo(void);

/* ---- the host's thread ---- */

/* What the host gives: a way for the painter to sleep with nothing to
 * paint and to be woken, and to give up the processor while waiting. */
typedef struct {
    void (*sleep)(s32 painter); /* until woken; may return sooner */
    void (*wake)(s32 painter);
    void (*yield)(void);
} PortPaintHost;

/* The most painters, each a thread. */
#define PORT_PAINTERS_MAX 8

/* `count` painters' threads are about to run port_PaintRun: from here on,
 * the primitives go to them. Returns how many there are to be (a power
 * of two, at most PORT_PAINTERS_MAX), 0 where there is no memory for
 * their ring. */
extern s32 port_PaintStart(const PortPaintHost *host, s32 count);
/* Painter `index`'s thread: paints until port_PaintStop. */
extern void port_PaintRun(s32 index);
/* Everything painted, the threads told to end (they return from
 * port_PaintRun), and the game's thread paints again. */
extern void port_PaintStop(void);
/* Whether the primitives are painted on a thread of their own. */
extern s32 port_PaintIsThreaded(void);

#endif /* PORT_PAINT_H */
