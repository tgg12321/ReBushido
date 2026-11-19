#ifndef PORT_GL_POST_H
#define PORT_GL_POST_H

/* The lighting overhaul's finish, after the scene and before the picture
 * is shown (docs/PORT.md, "The lighting overhaul", c and d): reflections
 * on the screen, ambient occlusion, the lights' bloom, and the tonemap the
 * lit shaders end in. The remastered picture alone: none of it runs with
 * `Graphics: Original` or the prover's exact picture, and nothing of it is
 * the game's state.
 *
 * **What a pixel is.** The colour buffer is all of video memory at the
 * picture's scale, eight bits a channel, and the game's own primitives
 * are painted into it in the console's order with the console's four
 * mixes; its alpha is read by nothing else. So the alpha says what
 * painted a pixel last (PORT_GL_POST_ALPHA_...): the ground's mesh writes
 * a code that carries how much its face reflects, the fighters' layer a
 * code of its own, and everything else -- the game's opaque primitives,
 * the clears, the sky, video memory written -- one. A primitive that
 * mixes with what is there leaves the alpha as it was (its blending takes
 * the larger of the two), so smoke over the floor keeps the floor's code.
 * The passes below read it from the picture once it is averaged down: a
 * reflection and the occlusion fall only on the ground's own pixels, and
 * a reflection's ray meets only the ground and the fighters, never the
 * HUD painted over them.
 *
 * **What depth there is.** The ground's mesh and the fighters' layer write
 * the depth buffer, in the world's units over PORT_GL_DEPTH_RANGE (gl.h);
 * it is taken down to the picture's size (a blit, the first sample of
 * several) and read with the view the mesh was drawn with. */

#include "common.h"
#include "gl.h"

/* The alpha codes (see above), in 255ths. The ground's mesh writes
 * GROUND + its reflectance (0 to 1) times GROUND_SPAN. */
#define PORT_GL_POST_ALPHA_GROUND 0.6
#define PORT_GL_POST_ALPHA_GROUND_SPAN 0.25
#define PORT_GL_POST_ALPHA_FIGHTER 0.95

/* The shader's side of the tonemap (`tonemap`), for the lit shaders:
 * `tone_map(base, total)` takes a pixel's colour as the game's own lights
 * make it (`base`) and with the lights of a frame added (`total`), both in
 * bytes (255 white, the game's light already in), and gives the colour to
 * write. Where nothing is added it is `base` clamped, to the byte: the
 * stage's own look is untouched. What the added light takes above a knee
 * -- the stage's own colour there, or three quarters of white, the higher
 * -- is rolled off towards white instead of clipped flat, so a torch's
 * pool or the flash of a clash keeps its hue and its gradient. With
 * `u_tone_on` 0, the game's clamp. */
#define PORT_GL_TONE_SHADER                                                    \
    "uniform int u_tone_on;\n"                                                 \
    "vec3 tone_map(vec3 base, vec3 total) {\n"                                 \
    "    if (u_tone_on == 0) { return clamp(total, 0.0, 255.0); }\n"           \
    "    vec3 knee = max(vec3(191.0), min(base, vec3(255.0)));\n"              \
    "    vec3 room = max(vec3(255.0) - knee, vec3(1e-3));\n"                   \
    "    vec3 over = max(total - knee, vec3(0.0));\n"                          \
    "    vec3 rolled = knee + room * (vec3(1.0) - exp(-over / room));\n"       \
    "    return clamp(mix(total, rolled, step(knee, total)), 0.0, 255.0);\n"   \
    "}\n"

/* Loads the functions and makes the shaders. Called where the backend
 * starts (gl_draw.c). Returns why it could not, or NULL; without it the
 * picture is what it was. */
extern const char *port_GlPostStart(void *(*getproc)(const char *name));

/* The settings (window `reflections`, `ambient_occlusion`, `bloom`,
 * `tonemap`; gl.h's options), each 0 or 1. */
extern void port_GlPostSetOn(
    s32 reflections, s32 occlusion, s32 bloom, s32 tonemap);
extern s32 port_GlPostToneIsOn(void);
/* Whether the passes will read the depth: the ground's mesh then writes it
 * beside its slots (gl_draw.c, port_GlGroundSlotsBegin). */
extern s32 port_GlPostNeedsDepth(void);

/* How much a face of the ground's mesh of `stage` reflects, 0 to 1, from
 * its normal (the world's, any length; y runs down): only what lies level
 * reflects, by the stage's own kind of floor (gl_post.c, the table). */
extern float port_GlPostReflectance(s32 stage, const float *normal);

/* The ground's mesh was drawn with this view (gl_stage.c): the rotation
 * (1.12, a row after a row), the camera, the projection's distance, the
 * screen's middle in video memory's pixels (offsets and a wider picture's
 * band in), and the line of video memory the drawing area begins at. */
extern void port_GlPostNoteView(
    const float *rotation, const float *camera, float h, float middle_x,
    float middle_y, s32 top);
/* The mesh is about to clear the depth buffer and draw again: the last
 * drawing's depth is kept, for when its picture is the one shown. */
extern void port_GlPostBeforeMesh(const PortGlPicture *picture);

/* Whether the passes will run on the picture on show: a setting on, and
 * a drawing of the ground's mesh that is that picture. The picture is
 * then averaged down into a buffer of its own even with one sample a
 * pixel (gl_draw.c, port_GlIsResolved). */
extern s32 port_GlPostWanted(const PortGlPicture *picture);

/* The passes, over the picture averaged down (`picture_texture`, `w` by
 * `h`, through `picture_framebuffer`), reading the scene's depth from
 * `picture->framebuffer`. Leaves the picture's framebuffer bound and the
 * card's state forgotten. */
extern void port_GlPostRun(
    const PortGlPicture *picture, u32 picture_framebuffer, u32 picture_texture,
    s32 w, s32 h, s32 sample);

/* Why the passes could not be made, or NULL. */
extern const char *port_GlPostProblem(void);
/* Pictures the passes ran on, over the run. */
extern u32 port_GlPostPictures(void);

#endif /* PORT_GL_POST_H */
