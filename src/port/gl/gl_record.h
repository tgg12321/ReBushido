#ifndef PORT_GL_RECORD_H
#define PORT_GL_RECORD_H

/* The step's drawing written down, and drawn by the window afterwards
 * (docs/PORT.md, "The step on a thread of its own").
 *
 * The side channel hands the backend the step's primitives and writes of
 * video memory as the step makes them (port_remaster.h), and the window
 * ends the card's frame at the step's blanks; all of that is OpenGL, and
 * a context is current on one thread. While a step records, none of it
 * reaches OpenGL: each call of the backend, each end of the card's frame
 * and each turn of the pause's holder is written down in order, with
 * everything the backend would read of the game at it -- the pixels of
 * each rectangle of video memory told of as they are then, the fighters'
 * views and the side channel's stage view, casters and sun as they are
 * then (port_FighterKeepViews, port_RemasterKeepSide), what the game's
 * memory says of each primitive's node (PortRemasterPrim). The window's
 * thread then draws the record, the calls made in the same order to the
 * same backend, which reads all of that from the record and nothing from
 * the game, as the game has gone on. */

#include "common.h"

/* From now on the backend's calls are written down (TRUE), or made as
 * they come again (FALSE). Called on the window's thread, with the step
 * still. What is written down at TRUE first is the views and the side
 * channel as they are, and the getters answer from those until the
 * record is drawn: a picture drawn while the step runs reads nothing the
 * step is writing. FALSE leaves the record to be drawn. */
extern void port_GlStepRecords(s32 on);
extern s32 port_GlIsRecording(void);

/* The card's frame ended (port_GlFinishFrame): written down where the
 * step records, and done now otherwise. */
extern void port_GlFinishOrRecord(void);

/* What is written down, drawn now and forgotten: on the thread with the
 * context, with the step still. */
extern void port_GlDrawPending(void);

/* While the step still writes: what it has written so far drawn now, on
 * the thread with the context, so that little is left to draw once it
 * has ended. Nothing is drawn past a hold (port_GlRecordHold), nor past
 * the stage's view of another version than was drawn last: the stage's
 * meshes are then built again from the game's memory, which only a
 * still step leaves alone; the rest waits for port_GlDrawPending. Once
 * something of a frame's list has been drawn, no picture may be drawn
 * until the record has been (the card's frame is under way). Returns how
 * many calls were made. */
extern u32 port_GlDrawSoFar(void);
/* On the step's thread: what is written down from here is drawn only once
 * the step is still (the frames ahead after the step, which put the
 * game's memory back). */
extern void port_GlRecordHold(void);
/* Whether the record has a primitive in it: the frame's list has begun.
 * `wake` is called, on the step's thread, as the first is written. */
extern s32 port_GlRecordHasDraws(void);
extern void port_GlRecordWake(void (*wake)(void));

/* What the records came to: how many, their calls and snapshots, the most
 * bytes one took, and the time drawing them took, in microseconds. */
typedef struct {
    u32 records;
    u32 calls;
    u32 views_kept;
    u32 sides_kept;
    u32 most_bytes;
    u32 lost;
    u32 streamed; /* calls drawn while the step ran (port_GlDrawSoFar) */
    u64 bytes;
} PortGlRecordCounts;

extern const PortGlRecordCounts *port_GlRecordCounts(void);

#endif /* PORT_GL_RECORD_H */
