#ifndef PORT_GL_FIGHTER_CARD_H
#define PORT_GL_FIGHTER_CARD_H

/* The HD fighters' work done on the card (docs/PORT.md, "The HD fighters
 * on the card"): a model's body posed on the bones of a frame, the grid
 * of its vertices that a cloth's places look about them in, and each
 * smooth cloth (gl_cloth.h) made through the game's points and kept out
 * of that body, all by compute shaders, once a frame and pose. Nothing
 * of it comes back to the processor: the cloth is drawn and cast from
 * the buffer it was made into, and the shadows read the body's lowest
 * point where they need it from the card's own memory.
 *
 * The sums are the processor's of before (gl_cloth.c's history): the
 * same grid of 64-unit cubes and the same order of each cube's vertices
 * (the vertices' own), in single precision; each place is its own work,
 * so a run gives the same buffers every time. */

#include "common.h"
#include "gl_cloth.h"

/* Makes the compute shaders. NULL, or why the card cannot do it. */
extern const char *port_GlFighterCardStart(void *(*getproc)(const char *));

/* Each of these leaves no program in use: the caller binds its own again
 * (asking GL which was in use would wait on the driver's thread). */

/* A body posed: the model's vertex buffer (PortFighterVertex, as the
 * file has it) and how many, each bone's world matrix in the world's
 * units (a column after a column, as u_world has it) and each joint's
 * turn (the bone's rotation times its inverse bind matrix, row after
 * row), posed on the card under `key` in the slot's own room. Posed
 * again only where the key is new. The pose's number, or -1. */
extern s32 port_GlFighterCardPose(
    s32 slot, u64 key, u32 vertex_buffer, s32 vertex_count,
    const float (*world)[16], const float (*turn)[9]);
/* ... and several at once (all the fighters of a frame): each step of
 * every new pose dispatched before the barrier after it, so that the
 * card makes them side by side. Each ask's `pose` is set (-1: none);
 * at most PORT_GL_CARD_ASKS are made in one batch. */
#define PORT_GL_CARD_ASKS 24

typedef struct {
    s32 slot;
    u64 key;
    u32 vertex_buffer;
    s32 vertex_count;
    const float (*world)[16];
    const float (*turn)[9];
    /* the model's joints' bands and the pose's dual quaternions, where the
     * joints are blended (gl_joints.h); band_buffer 0 where not */
    u32 band_buffer;
    const float (*dual)[4];
    s32 pose;
} PortGlCardPoseAsk;

extern void port_GlFighterCardPoses(PortGlCardPoseAsk *asks, s32 count);
/* How many copies of each fighter the benchmark's stress draws, each
 * with poses of his own: room in each slot for as many more. */
extern void port_GlFighterCardKeep(s32 copies);
/* The buffer holding a pose's lowest point, the greatest y of its
 * vertices, at word PORT_GL_CARD_FEET_WORD as an ordered integer
 * (port_GlFighterCardFeet in GLSL, below), or 0. */
extern u32 port_GlFighterCardFeetBuffer(s32 pose);
#define PORT_GL_CARD_FEET_WORD 8193
/* GLSL that reads that word back as the lowest point, truncated to the
 * whole unit as the processor's ground was. */
#define PORT_GL_CARD_FEET_GLSL                                                 \
    "float card_feet(int key) {\n"                                             \
    "    int bits = key >= 0 ? key : key ^ 0x7FFFFFFF;\n"                      \
    "    return trunc(intBitsToFloat(bits));\n"                                \
    "}\n"

/* A cloth's surface made ready for the card: its sums, quads and
 * vertices sent once. NULL where there is no memory. */
typedef struct PortGlCardCloth PortGlCardCloth;
extern PortGlCardCloth *port_GlFighterCardCloth(const PortGlCloth *surface);

/* Cloths made through the game's points, each `points` (its point_count
 * of them, three numbers each, in the world's units) into `out`
 * (port_GlClothVertices PortGlClothVertex), kept out of pose `pose` by
 * `gap` where `is_held` (each point first, then each place of the
 * surface). Where `is_counting`, a place kept out is looked at again for
 * the count of those still inside (port_GlFighterCardCounts). The
 * fighter's cloths are made together, each step for all of them at
 * once: a step's barrier waits on the card. */
typedef struct {
    PortGlCardCloth *cloth;
    const float *points;
    u32 out;
} PortGlCardHold;

extern void port_GlFighterCardHold(
    const PortGlCardHold *holds, s32 count, s32 pose, double gap, s32 is_held,
    s32 is_counting);

/* ... and the cloths of several bodies at once, each step of every one
 * before the barrier after it. */
typedef struct {
    const PortGlCardHold *holds;
    s32 count;
    s32 pose;
} PortGlCardHoldSet;

extern void port_GlFighterCardHolds(const PortGlCardHoldSet *sets, s32 count,
                                    double gap, s32 is_held, s32 is_counting);
/* While the game loads: the slot's poses given room for the model, and
 * every program run once on its bind pose, with `holds` (the model's
 * cloths, through points of nought, into buffers of the caller's): a
 * first dispatch, and a buffer's first room, cost the fight's first
 * frame some tens of milliseconds where they were not done before. */
extern void port_GlFighterCardWarm(
    s32 slot, u32 vertex_buffer, s32 vertex_count, const PortGlCardHold *holds,
    s32 count);
/* A buffer of `bytes` on the card for such a cloth, or 0. */
extern u32 port_GlFighterCardBuffer(s32 bytes);
/* The places of the cloths kept out that the game's cloth had inside the
 * body, and those still inside as drawn (only where counted), since the
 * run began: read back from the card, so once at a run's end. */
extern void port_GlFighterCardCounts(u32 *inside, u32 *left_inside);

#endif /* PORT_GL_FIGHTER_CARD_H */
