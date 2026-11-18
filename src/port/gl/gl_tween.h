#ifndef PORT_GL_TWEEN_H
#define PORT_GL_TWEEN_H

/* Pictures between the game's frames (docs/PORT.md, "Remastered", "More
 * pictures a second"): the frame on show drawn again with the geometry
 * of the frame after it mixed in, so that a 144 Hz screen shows 144
 * pictures of a game that still steps 30 times a second and computes
 * exactly what it always did.
 *
 * Nothing here runs the game and nothing here changes a value the game
 * computes. It sits between the decoder (port_remaster.h) and the
 * renderer that draws (gl.h): every primitive and every write of video
 * memory a frame makes is written down here as it passes, and a picture
 * between two frames is that list given to the renderer again with each
 * corner moved part of the way.
 *
 * Which frame is "on show": the game draws frame F into one half of
 * video memory and puts the other half, which holds frame F-1, on the
 * display. So at the blank where frame F's drawing has just been handed
 * over, what the screen shows is frame F-1, and frame F is already
 * known: a picture between the two is behind nothing and ahead of
 * nothing. There is no added latency (docs/PORT.md). With run-ahead
 * the frames come from the steps run ahead (step.c), as many frames
 * sooner, and this is the same. */

#include "common.h"
#include "port_remaster.h"

/* A phase is in 65,536ths: 0 is the frame on show, PORT_TWEEN_ONE is the
 * frame after it. */
#define PORT_TWEEN_FRACTION 16
#define PORT_TWEEN_ONE (1 << PORT_TWEEN_FRACTION)

/* What a run counts, for the prover and the session's log. */
typedef struct {
    u32 frames;     /* frames written down */
    u32 pictures;   /* pictures drawn between frames */
    u32 cuts;       /* pictures not drawn: the two frames are unlike */
    u32 overflowed; /* frames too large to be written down */
    /* of the last picture drawn */
    u32 prims;    /* primitives replayed */
    u32 mixed;    /* ... whose corners were moved part of the way */
    u32 unpaired; /* ... that the frame after it has not, or not near */
    u32 flat;     /* ... that have no depth: the 2D layer, the HUD */
    /* of the last pair of frames: how the partners were found */
    u32 scene;    /* primitives of the frame on show that can be moved */
    u32 weighed;  /* ... of those, the ones in the picture, which are
                   * what says whether the two frames are one scene */
    u32 jumped;   /* ... that are paired and a screen's width away: a
                   * camera that cut, where many of them are */
    u32 named;    /* by the face and the thing it is a drawing of */
    u32 searched; /* ... and by the nearest of the same signature */
    u32 clashes;  /* ... where the nearest was another's already */
    u32 moved;    /* ... by name, of a thing whose node is another one
                   * frame on (gl_tween.c, port_TweenPairMoved) */
    u32 held;     /* primitives not moved between their two places,
                   * because so moved they would not be drawn at all */
    /* how far the farthest corner moved between the two frames, in
     * whole pixels, over the last picture */
    u32 widest_step;
    /* corners of the last picture that do not lie between the two
     * frames' own places for them: none may, and the number is kept so
     * that a run can say so (docs/PORT.md) */
    u32 off_the_line;
    /* of the last pair of frames: faces one of the two frames has and
     * the other has not, moved all the same by their corners, which the
     * other frame has (docs/PORT.md, "More pictures a second"): a face
     * of the frame on show the frame after it turned away from, and a
     * face of the frame after it that the frame on show had turned away
     * from */
    u32 going;
    u32 coming;
} PortTweenCounts;

/* Takes the renderer that draws (port_GlBackend) and puts itself in
 * front of it. Returns NULL, or why it could not. */
extern const char *port_TweenStart(void);
/* Gives the renderer back and frees what was written down. */
extern void port_TweenStop(void);
extern s32 port_TweenIsOn(void);
/* Pairs two frames' primitives as this file did before they were named
 * -- by what they are painted out of and where they were, the nearest
 * of the same signature -- and lets a moved primitive fall outside the
 * chip's rule and go undrawn. Nothing but the prover asks for it, and
 * only to measure what naming buys (`--gl-guess`). */
extern void port_TweenGuessOnly(s32 on);

/* Everything written down is forgotten: nothing is drawn between frames
 * again until the game has drawn two more. The window calls it where the
 * picture changes (F2), since the frames written down before that are no
 * longer the ones on the display. */
extern void port_TweenForget(void);

/* Whether a picture between frames has put the card's mirror of video
 * memory back from video memory as it is since this was last asked: what
 * it does where what the picture changed of the mirror was too much to
 * keep (gl.h, port_GlMirrorHold). With frames run ahead that is a frame or
 * two behind the frames the card draws, and the window sends the mirror
 * video memory as it is before the frames are run ahead (sdl_window.c,
 * port_ReadInputLate). */
extern s32 port_TweenTakeMirrorBehind(void);

/* How many frames have been written down: the caller watches this to
 * see when the game has drawn a new one, and takes the time then. */
extern u32 port_TweenGeneration(void);

/* Whether a picture between the frame on show and the frame after it
 * can be drawn now. */
extern s32 port_TweenCanDraw(void);

/* The picture at `phase` into the renderer's colour buffer: the frame on
 * show drawn again, every corner that the frame after it has as well
 * moved `phase` of the way there. FALSE where it could not be drawn, in
 * which case nothing was touched and what the renderer holds is still
 * the game's own picture. */
extern s32 port_TweenDraw(s32 phase);

extern const PortTweenCounts *port_TweenGetCounts(void);

/* The pairing of two frames worked out on a thread of the host's
 * (docs/PORT.md, "The software GPU beside the game"): as soon as the
 * frame after is whole, while the game goes on, and not in the first
 * picture between the two. `give` has the host's thread run
 * port_TweenPairWork; `wait` returns once it has. Without them (NULL)
 * the first picture works it out. */
extern void port_TweenSetWorker(void (*give)(void), void (*wait)(void));
extern void port_TweenPairWork(void);

/* One point of the last picture, to be watched from phase to phase: the
 * first corner of the primitive whose corners moved farthest between the
 * two frames, where it was drawn, in 16.16 pixels with the drawing
 * offset taken off. The same primitive at every phase of the same pair
 * of frames, so a run can hold the point to the line between its two
 * ends. FALSE if the last picture moved nothing. */
extern s32 port_TweenTrack(s32 *key, s32 *x, s32 *y);

/* ---- the arithmetic, which the host tests run (tests/port/tween.c) ---- */

/* One corner of a primitive moved part of the way from `before` to
 * `after`, in view space and not on the screen: a point that comes
 * towards the camera moves on the screen as the projection would have
 * it and not in a straight line.
 *
 * The screen position of a point whose view-space position moves evenly
 * is, with wa = (1 - t) * za and wb = t * zb,
 *
 *     x = (wa * xa + wb * xb) / (wa + wb),
 *
 * where xa, xb are the two screen positions and za, zb the two depths:
 * the screen's own centre, which the projection added to both, falls out
 * of it. The depth itself moves evenly.
 *
 * `phase` is in PORT_TWEEN_ONE ths. The positions are 16.16 pixels with
 * the drawing offset taken off, which is what makes the two frames'
 * halves of video memory comparable. */
extern void port_TweenMixCorner(
    PortRemasterCorner *out, const PortRemasterCorner *before,
    const PortRemasterCorner *after, s32 phase);

/* How far the farthest corner is from its own place one frame on, in
 * whole pixels. */
extern s32 port_TweenStep(
    const PortRemasterPrim *before, const PortRemasterPrim *after);

/* The farthest a corner may move and still be the same corner one frame
 * on, where a primitive has no name and is looked for by its likeness
 * (port_TweenFind). */
#define PORT_TWEEN_STEP_MAX 128

/* What says that two frames are not one scene a frame apart but a cut
 * (port_TweenPairFrames). Of the primitives the picture holds: the share
 * of them, in 256ths of those that are paired, that the frame after may
 * fail to hold at all, and the share that it may hold a jump away; and
 * how far a jump is, in whole pixels of the console's screen, a third of
 * whose width in one frame of the game is a camera that cut and not one
 * that moved. */
#define PORT_TWEEN_CUT_SHARE 256
#define PORT_TWEEN_JUMP_SHARE 128
#define PORT_TWEEN_JUMP 213

#endif /* PORT_GL_TWEEN_H */
