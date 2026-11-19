#ifndef PORT_GL_LIGHTS_H
#define PORT_GL_LIGHTS_H

/* The lights of a frame beside the stage's own, for the remastered picture
 * alone (docs/PORT.md, "The lighting overhaul").
 *
 * The stage's three directional lights and its colour of the dark
 * (light_SetStage) stay what every surface is lit by first: they are the
 * stage's mood. What this list adds is lights with a place -- a torch, a
 * lantern, the flash where two blades meet -- each lighting the ground,
 * the stage's pieces and the fighters' models at the pixel, the strongest
 * few that ask for it casting shadows of their own.
 *
 * Who fills it: a frame's lights are given between port_GlLightsBegin and
 * port_GlLightsEnd, once a game frame, by whatever knows them (a stage's
 * file of lights, the events of a fight: src/port/gl/gl_lights.c and the
 * mod's side). The list is the picture's and nothing else: it is not the
 * game's state, it is never saved or hashed (tools/port_build.py,
 * OUTSIDE_STATE), and nothing the game computes reads it. Begin, Add and
 * End may be called on the step's thread: End hands the frame's list to
 * the picture as the side channel's other snapshots are handed over (the
 * step's record, gl_record.h), so the frame drawn is lit by its own list.
 *
 * Every number is in the game's world: its units, its axes, the y axis
 * running down (a light above the ground has a smaller y than the
 * ground). */

#include "common.h"

/* The most lights one frame holds; an Add past it is refused. */
#define PORT_GL_LIGHTS_MAX 128

/* What kind of light. */
#define PORT_GL_LIGHT_POINT 0 /* every way from its place */
#define PORT_GL_LIGHT_SPOT 1  /* a cone from its place along `direction` */

typedef struct {
    /* where it is, in the world's units */
    float position[3];
    /* its colour, each of red, green and blue from 0 to 1, linear (not
     * the screen's gamma) */
    float colour[3];
    /* how bright: 1 lights a surface facing it, at its place, as brightly
     * as the stage's sun at full colour would; any number above 0 */
    float intensity;
    /* how far its light reaches, in the world's units: it falls off
     * smoothly to nothing there, and nothing farther is lit by it */
    float radius;
    /* PORT_GL_LIGHT_POINT or PORT_GL_LIGHT_SPOT */
    s32 kind;
    /* a spot's: which way it points (need not be of length one), and the
     * half-angles of its cone in degrees: full inside `cone_inner`, falling
     * off to nothing at `cone_outer` */
    float direction[3];
    float cone_inner;
    float cone_outer;
    /* how much of its intensity flickers, 0 (steady) to 1 (down to nothing
     * at times), as a flame does: the picture works the flicker out from
     * the light's `id` and the game's frame, so it is the same at any
     * frame rate and in every run */
    float flicker;
    /* whether it should cast shadows: a wish, granted to the strongest few
     * on the screen (PORT_GL_LIGHTS_SHADOWED) */
    s32 casts_shadow;
    /* the same number every frame for the same light, 0 for none: a
     * picture between two frames moves a light from its place in one to
     * its place in the next by it, and its flicker is seeded by it */
    u32 id;
} PortGlLight;

/* How many of a frame's lights cast shadows at most. */
#define PORT_GL_LIGHTS_SHADOWED 4

/* A frame's list, made anew: Begin empties it, each Add puts one light in
 * (its index, or -1 where the list is full or the light is no light: a
 * radius or intensity of 0 or less, an unknown kind) and End hands it to
 * the picture. A frame that calls none of them keeps the last list. */
extern void port_GlLightsBegin(void);
extern s32 port_GlLightsAdd(const PortGlLight *light);
extern void port_GlLightsEnd(void);

/* ---- the picture's side (gl_lights.c, the renderer) ---- */

/* The list the picture being drawn is lit by, and how many it holds. */
extern const PortGlLight *port_GlLightsNow(s32 *count);
/* The list handed over before it, which a picture between two frames moves
 * the lights from (`id` pairs them), and how many it holds. */
extern const PortGlLight *port_GlLightsBefore(s32 *count);

#endif /* PORT_GL_LIGHTS_H */
