#ifndef PORT_GL_SKY_H
#define PORT_GL_SKY_H

/* A sky behind the stages that are open to it, in the remastered picture
 * (docs/PORT.md, "The sky").
 *
 * The console has no sky: a fight's frame begins with a fill of the
 * stage's colour of the distance (light.c, light_SetFarColor), which is
 * black or nearly on every stage, and whatever the stage's geometry does
 * not cover is that colour. Where the stage is open -- a field, a coast,
 * a rooftop -- that is a flat black above the horizon.
 *
 * `sky night` paints, in the place of that fill and only there, a night
 * sky of the port's own: a gradient that is the colour of the distance
 * at the horizon, so that the stage's own fog runs into it with no seam,
 * and darkens upwards towards the stage's own ambient colour; stars; and
 * a moon where the stage's light comes from. It is worked out per pixel
 * from the direction the pixel looks in, with the same view the stage's
 * ground is drawn with, so it turns with the camera, also in a picture
 * between two frames, and it is infinitely far: it never moves as the
 * camera moves. A mod may give a stage a picture of its own sky instead
 * (`skies/`, docs/MODS.md).
 *
 * It is painted where the fill was, before anything of the frame, so
 * every primitive of the scene is painted over it as over the fill.
 * Nothing of the stage's own geometry changes; a stage that is closed (a
 * hall, a cave) has none. Nothing here changes a value the game
 * computes. */

#include "common.h"
#include "gl.h"

enum {
    PORT_SKY_ORIGINAL, /* the game's own colour of the distance */
    PORT_SKY_NIGHT     /* the port's own night sky */
};

/* Makes the shader. Called where the backend starts, on its context.
 * Returns why it could not, or NULL; then the sky is the game's fill. */
extern const char *port_GlSkyStart(void *(*getproc)(const char *name));

/* PORT_SKY_ORIGINAL or PORT_SKY_NIGHT. */
extern void port_GlSkySetKind(s32 kind);

/* The game has just filled the rectangle (`x`, `y`, `w`, `h`) of video
 * memory, the whole width of its picture, with one colour (`color`, as
 * 0xFFRRGGBB): the frame's own clear. Where it is a fight's frame on a
 * stage that is open to the sky, the sky is painted over the fill, in
 * the picture's own rectangle and its bands. */
extern void port_GlSkyCleared(s32 x, s32 y, s32 w, s32 h, u32 color);

/* Which stages have a sky, as the run's log says it: the name of the
 * stage and why, once for each stage the run reaches. NULL while there is
 * nothing new to say. */
extern const char *port_GlSkyNews(void);

/* How many pictures the sky was painted in, for the prover. */
extern u32 port_GlSkyDrawn(void);

#endif /* PORT_GL_SKY_H */
