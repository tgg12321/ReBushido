#ifndef PORT_GL_TIME_H
#define PORT_GL_TIME_H

/* The time of day the remastered picture lights a stage at (ROADMAP 20;
 * docs/PORT.md, "The time of day"; port_StageTime in
 * include/port_stage_choice.h).
 *
 * The game lights every one of its 38 stages at night, each by a setup of
 * its own (light.c, light_SetStage): a sun, an ambient colour and a colour
 * of the distance. With the stage's own time chosen nothing here changes
 * anything. With dawn, day, dusk or night, the picture is given another
 * setup in its place, worked out from the stage's own: the sun turned to
 * the time's height and bearing (low and long at dawn and dusk, from the
 * stage's own bearing turned back or on), its colour and the ambient the
 * time's, scaled so that the stage's own floor keeps its place in the
 * time's range of light; and with it the fog's colour and amount, the sky,
 * the softness of the sun's shadows, the bloom and the stage's own lights.
 * Night is its own look: a weak, cool, soft moon over a dark ambient, the
 * stage's lights brightened to carry the scene.
 *
 * Every reader of the stage's light in the picture asks here and not the
 * game (port_GlTimeTileLights): the ground's mesh and its sun's maps, the
 * sky, the occlusion. Nothing here is the game's state or changes a value
 * the game computes. */

#include "common.h"

typedef struct {
    s32 on;          /* a time other than the stage's own, on a dressed stage */
    s32 time;        /* PORT_TIME_... */
    s32 is_open;     /* the stage is open to the sky (gl_sky.c) */
    float to_sun[3]; /* towards the sun or the moon, of length one, y down */
    float sun[3];    /* its colour, bytes of light where it meets a face */
    float back[3];   /* the colour of the dark, bytes */
    float fog[3];    /* the fog's colour, 0 to 1, as the screen shows it */
    float fog_amount; /* how much of it the farthest fog level is; 0: the
                         game's own fog */
    float horizon[3]; /* the sky, 0 to 1 */
    float zenith[3];
    float stars;    /* 0 to 1 */
    float has_moon; /* a moon's disc, or the sun's */
    float soft;     /* the sun's penumbra, times the day's */
    float bloom;    /* the lights' glow, times its own */
    float floor[3]; /* the time's light on a level floor over the
                       stage's own, each channel */
} PortGlTime;

/* The look of the stage in play at the time chosen (worked out again when
 * either changes). Its `on` is 0 at the stage's own time. */
extern const PortGlTime *port_GlTime(void);

/* The light setup (a LightSetup, light.h) the picture lights the stage's
 * tiles and pieces with: the game's own, or the time's. */
extern const void *port_GlTimeTileLights(void);

/* How a light of a stage's file shines at the time chosen: 0 where its
 * `times` (PORT_LIGHT_TIME_... bits, 0 always) leaves the time out, else
 * how much brighter than its file says (night's the most). The stage's own
 * time is night: the game's stages are all night. Safe on any thread. */
extern float port_GlTimeLightGain(u32 times);

/* The shader's side of the game's own polygons of a fighter (gl_draw.c):
 * `time_relight(color, normal)`, the colour the coprocessor lit the face
 * with times how much the time's light differs from the stage's own at
 * that normal, each channel. */
#define PORT_GL_TIME_SHADER                                                    \
    "uniform int u_time_on;\n"                                                 \
    "uniform mat3 u_time_own_light;\n"                                         \
    "uniform mat3 u_time_own_color;\n"                                         \
    "uniform vec3 u_time_own_back;\n"                                          \
    "uniform mat3 u_time_light;\n"                                             \
    "uniform mat3 u_time_color;\n"                                             \
    "uniform vec3 u_time_back;\n"                                              \
    "uniform vec3 u_time_floor;\n"                                             \
    "vec3 time_lit(mat3 l, mat3 c, vec3 b, vec3 n) {\n"                        \
    "    return b + c * max(l * n, vec3(0.0)) * 256.0;\n"                      \
    "}\n"                                                                      \
    "vec3 time_relight(vec3 color, vec3 n) {\n"                                \
    "    if (u_time_on == 0) { return color; }\n"                              \
    "    vec3 own = time_lit(u_time_own_light, u_time_own_color,\n"            \
    "                        u_time_own_back, n);\n"                           \
    "    vec3 now = time_lit(u_time_light, u_time_color, u_time_back, n);\n"   \
    "    return color * clamp(now / max(own, vec3(24.0)), 0.0, 2.0);\n"        \
    "}\n"

typedef struct {
    s32 on;
    s32 own_light;
    s32 own_color;
    s32 own_back;
    s32 light;
    s32 color;
    s32 back;
    s32 floor;
} PortGlTimeAt;

/* (Located and given with the sun's screen uniforms: gl_sun.h,
 * port_GlSunLocateScreen and port_GlSunBindScreen.) */

/* A setup's matrices as a shader takes them: the light matrix and the
 * colour matrix made 1, a row after a row, and the colour of the dark in
 * bytes. */
extern void port_GlTimeSetupFloats(
    const void *setup, float *light, float *color, float *back);

#endif /* PORT_GL_TIME_H */
