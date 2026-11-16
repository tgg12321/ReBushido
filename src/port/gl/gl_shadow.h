#ifndef PORT_GL_SHADOW_H
#define PORT_GL_SHADOW_H

/* The fighters' shadows in the remastered picture (docs/PORT.md,
 * "Remastered", "The shadows").
 *
 * The game's shadow is the caster's own shape: the model's shadow mesh
 * flattened onto the ground by a shear worked out from the stage's sun
 * (light_ProjectShadow) and drawn as opaque black polygons, which hide
 * the ground under them and darken each other where they overlap.
 *
 * With `shadows soft` the picture does not draw those polygons at all.
 * Their silhouette goes into a mask of the picture's own resolution --
 * coverage, not colour, so that a face over a face is the same
 * darkness as one face alone -- and at the frame's end that mask
 * darkens what it falls on by a factor, which leaves the ground's
 * texture showing through.
 *
 * **It never darkens a fighter, a weapon or anything else standing in
 * it**, and not by comparing depths: the same mask carries, at every
 * pixel, whether something that is not the stage's ground painted that
 * pixel in this frame. Those pixels are left exactly as the picture
 * drew them. The renderer knows which primitives those are because the
 * side channel says so (port_remaster.h: a shadow's faces, and the
 * stage's own tiles), so the test is what drew the pixel and not how
 * far away it is -- a fighter's foot and the ground it stands on are at
 * the same depth, which is why depth cannot tell them apart and why the
 * first try at this painted the darkness over the caster's feet.
 *
 * Nothing here runs unless `shadows` asks for it: with `shadows
 * original` the game's own black polygons are drawn, and the picture is
 * what it was to the pixel. */

#include "common.h"
#include "port_remaster.h"

/* What `shadows soft` means, in hundredths, is PORT_SHADOWS_SOFT
 * (port_remaster.h): how much of what the shadow falls on is taken away
 * under the middle of it.
 *
 * How soft the edge is, in pixels of the picture -- not of video memory
 * and not of the colour buffer, so the softness is the same however
 * large the window is and whatever the smoothing. */
#define PORT_SHADOW_PENUMBRA 3

/* Loads the functions and makes the shaders; the mask itself is made
 * when a frame first needs it. Called where the backend starts
 * (gl_draw.c). Returns why it could not, or NULL. */
extern const char *port_GlShadowStart(void *(*getproc)(const char *name));

/* How much the shadows darken what they fall on, in hundredths: 0 for
 * the game's own black silhouettes, which is what `shadows original`
 * means. */
extern void port_GlShadowSetStrength(s32 strength);
extern s32 port_GlShadowStrength(void);
/* Whether the soft shadows are what this frame draws: the strength, and
 * a backend that could make the mask. */
extern s32 port_GlShadowIsSoft(void);

/* A frame's primitives are about to come (gl_draw.c, the backend's
 * `begin`). */
extern void port_GlShadowNewFrame(void);

/* Whether this primitive is a face of a shadow and must not be drawn
 * into the picture: it is kept for the mask instead. */
extern s32 port_GlShadowTakes(const PortRemasterPrim *prim);

/* Whether the picture has to keep this primitive's pixels out of the
 * darkening: everything of a frame but the stage's ground that the
 * shadows fall on. The vertices of such a batch are kept as they are
 * drawn (gl_draw.c, port_GlKeepOccluders) and painted into the mask at
 * the frame's end. */
extern s32 port_GlShadowCollects(void);
extern s32 port_GlShadowIsGround(const PortRemasterPrim *prim);

/* Video memory was written into the picture (a fill, a copy, a load).
 * One that comes after the shadows covers them, so the mask gives those
 * pixels up. */
extern void port_GlShadowWroteVram(s32 x, s32 y, s32 w, s32 h);

/* The mask and the darkening, before the picture is averaged down
 * (gl_draw.c, port_GlFinishFrame). Nothing happens twice in a frame and
 * nothing happens in a frame that had no shadow in it. */
extern void port_GlShadowFinish(void);

/* The mask drawn into the picture in the place of the darkening, for a
 * run that measures what was darkened and what was kept out of it: red
 * is the silhouettes' coverage and green is everything of the frame that
 * is not the stage's ground, which is never darkened. Only the prover
 * asks for it (--gl-shadow-mask). */
extern void port_GlShadowShowMask(s32 on);

/* Why the soft shadows could not be drawn, or NULL: the card has no
 * OpenGL 3.3, or there was no memory for the mask. */
extern const char *port_GlShadowProblem(void);

/* The last frame's faces in the mask, the triangles the run had no room
 * for, and the largest piece of the colour buffer any frame's darkening
 * was laid over. */
extern s32 port_GlShadowFaces(void);
/* Faces of a shadow the run took out of the picture, and the corners
 * they came to. */
extern u32 port_GlShadowTaken(void);
extern s32 port_GlShadowOccluders(void);
extern u32 port_GlShadowPoints(void);
extern s32 port_GlShadowLost(void);
extern u32 port_GlShadowArea(void);

#endif /* PORT_GL_SHADOW_H */
