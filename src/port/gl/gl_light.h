#ifndef PORT_GL_LIGHT_H
#define PORT_GL_LIGHT_H

/* The game's own lighting, worked out at each pixel, for the remastered
 * picture (`smooth_light`; docs/PORT.md, "Lighting").
 *
 * The game lights with the coprocessor: three directional lights and a
 * colour of the dark a light setup (LightSetup, src/main/scene.c; one a
 * stage, `light_SetStage`), each normal lit by NCS -- each light's turn
 * on the normal, none below nought, through the colour matrix onto the
 * colour of the dark -- and the colour put in the primitive. A tile of
 * the ground has one normal a face, so the ground is lit face by face;
 * a fighter's model has one a vertex, and the colour runs evenly
 * between them.
 *
 * With `smooth_light on` nothing of that changes but where it is worked
 * out: the same lights, the same colours, the same NCS, at every pixel,
 * from a normal that runs smoothly across the surface. The ground's
 * mesh gives each of its vertices the mean of the normals of the faces
 * about it that turn less than a crease from its own face, so a slope
 * is shaded as a slope and a wall still meets the ground at an edge; a
 * fighter's model takes its normal at the pixel from its vertices'.
 * Where a surface is flat the colour is the colour the game gave it, to
 * the coprocessor's rounding, so the stage keeps its palette and its
 * mood.
 *
 * Nothing here is in the build without a window; nothing here changes
 * a value the game computes. */

#include "common.h"

/* Whether the lighting is worked out at each pixel. */
extern void port_GlLightSetSmooth(s32 on);
extern s32 port_GlLightIsSmooth(void);

/* The crease, as the cosine of the angle between two faces' normals
 * beyond which they do not share a normal at a corner: 45 degrees. */
#define PORT_LIGHT_CREASE_COS 0.7071f

/* The normals of a mesh of `count` vertices, three to a triangle, made
 * smooth: `place` is each vertex's x, y and z, `stride` floats apart;
 * `normal` is each vertex's face normal, three shorts `normal_stride`
 * shorts apart, any length, and on return the vertex's own, of length
 * 4096. A vertex whose normal is nought is left so. Vertices are the
 * same corner where their places are the same to the unit. FALSE where
 * there was no memory for it, and the normals are then the faces'. */
extern s32 port_GlLightSmoothNormals(
    const float *place, s32 stride, s16 *normal, s32 normal_stride, s32 count);

#endif
