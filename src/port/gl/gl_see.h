#ifndef PORT_GL_SEE_H
#define PORT_GL_SEE_H

/* The stage's walls faded where they stand between the camera and a
 * fighter, in the remastered picture (docs/PORT.md, "Seeing the fighters
 * through the stage").
 *
 * The game has a way of its own for this: a tile of the stage that can
 * hide a fighter is drawn half-transparent while it is in the way
 * (STAGE_TILE_OPEN, stage_Draw), but only the tiles the stage's data
 * marks, and only by whole cells. `see_through on` does it for every
 * face of the ground's mesh (gl_stage.c): each fighter is an upright
 * cylinder about his bones, from his head to his feet, and a face whose
 * pixel lies on a line from the camera to him, nearer than he is, is left
 * out by a dither that thins towards the cylinder's edge -- a soft hole
 * through the wall, which the picture's smoothing (`antialiasing`) turns
 * into a fade. Never the floor: a face
 * that lies flat and is seen from above is the ground someone stands on,
 * and is never thinned.
 *
 * Nothing here changes a value the game computes. */

#include "common.h"

/* The most fighters held: the game's three model slots. */
#define PORT_SEE_FIGHTERS 3

/* A fighter's cylinder in the world's own units: the axis from (x,
 * top, z) down to (x, bottom, z) -- the world's y runs down -- and how
 * far about it. */
typedef struct {
    float x;
    float z;
    float top;
    float bottom;
    float radius;
} PortSeeFighter;

/* On or off (the `see_through` setting). */
extern void port_GlSeeSetOn(s32 on);
extern s32 port_GlSeeIsOn(void);

/* The fighters of the frame being drawn, at most PORT_SEE_FIGHTERS:
 * how many. 0 when it is off or there are none. */
extern s32 port_GlSeeFighters(PortSeeFighter *out);
/* ... the same whether the walls are seen through or not (gl_lights.c) */
extern s32 port_GlSeeFightersAlways(PortSeeFighter *out);

#endif /* PORT_GL_SEE_H */
