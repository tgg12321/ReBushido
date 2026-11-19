#ifndef PORT_GL_SUN_H
#define PORT_GL_SUN_H

/* The stage's sun casting shadows from a map of its own, for the
 * remastered picture alone (`shadows map`; docs/PORT.md, "The lighting
 * overhaul").
 *
 * The game's shadow is the fighters flattened onto the ground along the
 * sun. Here the sun has a depth map instead -- three of them, each a
 * square of the world seen along the sun, nested about the camera
 * (cascades) -- into which everything with a place in the world is drawn
 * as the sun sees it: the stage's ground and pieces (gl_stage.c) and the
 * fighters, their weapons and cloths (gl_shadow.c, from what the cast
 * shadows are made of). What is lit from a mesh of its own -- the ground,
 * the pieces, the fighters' models -- then asks the map, at each pixel,
 * how much of the sun reaches it, and takes the sun's own share of the
 * game's lighting away where it does not: so a shadow is the stage's own
 * light without its sun, it falls on walls, steps and the fighters
 * themselves, and its edge is as soft as the distance from the thing that
 * casts it makes it (percentage-closer soft shadows).
 *
 * Each map's square keeps its size and moves by whole texels of its own,
 * so nothing of a shadow crawls while the camera turns or the fighters
 * stand still. The maps are drawn once a picture, before the first thing
 * that reads them, from the picture's own camera and the casters at its
 * own phase between two frames.
 *
 * Nothing here runs unless `shadows map` asks for it, and nothing here
 * changes a value the game computes. */

#include "common.h"
#include "port_remaster.h"

/* How many maps, nested, and how many texels a side each has. */
#define PORT_GL_SUN_CASCADES 3
#define PORT_GL_SUN_SIZE 2048

/* The texture units a reader binds the map to: compared (the shadow test
 * itself), and its plain depths (how far a caster is, for the softness). */
#define PORT_GL_SUN_UNIT 14
#define PORT_GL_SUN_RAW_UNIT 15

/* Makes the map and its programs. Called where the backend starts
 * (gl_draw.c). Returns why it could not, or NULL. */
extern const char *port_GlSunStart(void *(*getproc)(const char *name));

/* Whether the sun casts from a map (the `shadows` setting's `map`), and
 * whether this picture has one: the setting, a backend that could make it,
 * and a picture drawn with the stage's camera. */
extern void port_GlSunSetOn(s32 on);
extern s32 port_GlSunIsOn(void);

/* A picture begins: the maps drawn for the last one are forgotten. */
extern void port_GlSunNewPicture(void);

/* The maps drawn for this picture, if they have not been, from the view
 * the ground is drawn with (`view`), before anything reads them. Leaves
 * the card's state forgotten (port_GlForgetState) and the picture's own
 * framebuffer bound. TRUE where there are maps to read. */
extern s32 port_GlSunPrepare(const PortStageView *view);
extern s32 port_GlSunIsReady(void);

/* Where a program has the map's uniforms, and the map given to it. */
typedef struct {
    s32 on;
    s32 matrix;
    s32 texel;
    s32 depth;
    s32 travel;
    s32 map;
    s32 raw;
    s32 soft;
    s32 gain;
} PortGlSunAt;

extern void port_GlSunLocate(u32 program, PortGlSunAt *at);
/* The program in use is given the maps (or told there are none). */
extern void port_GlSunBind(const PortGlSunAt *at);

/* The sun's direction of travel in the world, of length one, as the maps
 * are drawn along it; FALSE where there is none this picture. */
extern s32 port_GlSunTravel(float *travel);

/* What the maps came to, for the run's log: pictures they were drawn for,
 * and the triangles drawn into them in the last. */
extern u32 port_GlSunPictures(void);
extern u32 port_GlSunTriangles(void);

/* The shader's side: `sun_visible(world, normal)`, the share of the sun
 * that reaches a point of the world whose surface faces `normal` (of
 * length one, or nought), 1 where the maps do not reach. Needs GLSL 4.0. */
#define PORT_GL_SUN_SHADER                                                                                                                    \
    "uniform int u_sun_on;\n"                                                                                                                 \
    "uniform mat4 u_sun_matrix[3];\n" /* a texel of each map, in the world's                                                                  \
                                         units */                                                                                             \
    "uniform vec3 u_sun_texel;\n" /* the depth each map holds, from 0 to 1, in                                                                \
                                     the world's units */                                                                                     \
    "uniform vec3 u_sun_depth;\n"                                                                                                             \
    "uniform vec3 u_sun_travel;\n" /* how wide a penumbra grows a unit of                                                                     \
                                      distance from its caster */                                                                             \
    "uniform float u_sun_soft;\n"  /* how many times the sun's own share a                                                                    \
                                      shadow takes away (sun_shade) */                                                                        \
    "uniform float u_sun_gain;\n"                                                                                                             \
    "uniform sampler2DArrayShadow u_sun_map;\n"                                                                                               \
    "uniform sampler2DArray u_sun_raw;\n"                                                                                                     \
    "const vec2 sun_disk[16] = vec2[16](\n"                                                                                                   \
    "    vec2(-0.94201624, -0.39906216), vec2(0.94558609, -0.76890725),\n"                                                                    \
    "    vec2(-0.09418410, -0.92938870), vec2(0.34495938, 0.29387760),\n"                                                                     \
    "    vec2(-0.91588581, 0.45771432), vec2(-0.81544232, -0.87912464),\n"                                                                    \
    "    vec2(-0.38277543, 0.27676845), vec2(0.97484398, 0.75648379),\n"                                                                      \
    "    vec2(0.44323325, -0.97511554), vec2(0.53742981, -0.47373420),\n"                                                                     \
    "    vec2(-0.26496911, -0.41893023), vec2(0.79197514, 0.19090188),\n"                                                                     \
    "    vec2(-0.24188840, 0.99706507), vec2(-0.81409955, 0.91437590),\n"                                                                     \
    "    vec2(0.19984126, 0.78641367), vec2(0.14383161, -0.14100790));\n" /* a                                                                \
                                                                           * turn of the disk for each pixel of the buffer, the same at every \
                                                                           * picture, so that a still scene's penumbra is still */            \
    "mat2 sun_turn() {\n"                                                                                                                     \
    "    float a = 6.2831853 * fract(52.9829189 * fract(dot(gl_FragCoord.xy,\n"                                                               \
    "                                vec2(0.06711056, 0.00583715))));\n"                                                                      \
    "    float c = cos(a);\n"                                                                                                                 \
    "    float s = sin(a);\n"                                                                                                                 \
    "    return mat2(c, s, -s, c);\n"                                                                                                         \
    "}\n"                                                                                                                                     \
    "float sun_filter(int i, vec3 at, float texel) {\n"                                                                                       \
    "    vec2 size = vec2(textureSize(u_sun_raw, 0).xy);\n"                                                                                   \
    "    mat2 turn = sun_turn();\n" /* the casters about the point, nearer the                                                                \
                                     * sun than it: how far they are from it                                                                  \
                                     * says how wide its penumbra is */                                                                       \
    "    float search = clamp(u_sun_soft * 600.0 / texel, 2.0, 24.0);\n"                                                                      \
    "    float sum = 0.0;\n"                                                                                                                  \
    "    float found = 0.0;\n"                                                                                                                \
    "    for (int k = 0; k < 6; k++) {\n"                                                                                                     \
    "        vec2 uv = at.xy + turn * sun_disk[k * 2 + 1] * search / size;\n"                                                                 \
    "        float d = texture(u_sun_raw, vec3(uv, float(i))).r;\n"                                                                           \
    "        if (d < at.z) { sum += d; found += 1.0; }\n"                                                                                     \
    "    }\n"                                                                                                                                 \
    "    if (found == 0.0) { return 1.0; }\n"                                                                                                 \
    "    float apart = (at.z - sum / found) * u_sun_depth[i];\n"                                                                              \
    "    float radius = clamp(apart * u_sun_soft / texel, 1.0, 16.0);\n"                                                                      \
    "    float lit = 0.0;\n"                                                                                                                  \
    "    for (int k = 0; k < 10; k++) {\n"                                                                                                    \
    "        vec2 uv = at.xy + turn * sun_disk[k] * radius / size;\n"                                                                         \
    "        lit += texture(u_sun_map, vec4(uv, float(i), at.z));\n"                                                                          \
    "    }\n"                                                                                                                                 \
    "    return lit / 10.0;\n"                                                                                                                \
    "}\n"                                                                                                                                     \
    "float sun_visible(vec3 world, vec3 normal) {\n"                                                                                          \
    "    if (u_sun_on == 0) { return 1.0; }\n" /* A face turned away from the                                                                 \
                                                * maps' sun is in its own                                                                     \
                                                * shadow by them, but the game                                                                \
                                                * may light it with a sun of                                                                  \
                                                * its own from that side (a                                                                   \
                                                * fighter's lights are his                                                                    \
                                                * own): it is left as the game                                                                \
                                                * lit it, and a face the sun                                                                  \
                                                * grazes is let in softly. */                                                                 \
    "    float facing = -dot(normal, u_sun_travel);\n"                                                                                        \
    "    if (facing <= 0.0) { return 1.0; }\n"                                                                                                \
    "    float let_in = smoothstep(0.0, 0.15, facing);\n"                                                                                     \
    "    vec3 n = normal;\n"                                                                                                                  \
    "    for (int i = 0; i < 3; i++) {\n"                                                                                                     \
    "        float texel = u_sun_texel[i];\n"                                                                                                 \
    "        vec3 p = world + n * texel * 1.5 - u_sun_travel * texel;\n"                                                                      \
    "        vec4 c = u_sun_matrix[i] * vec4(p, 1.0);\n"                                                                                      \
    "        vec3 at = c.xyz * 0.5 + vec3(0.5);\n"                                                                                            \
    "        if (all(greaterThan(at.xy, vec2(0.03)))\n"                                                                                       \
    "            && all(lessThan(at.xy, vec2(0.97))) && at.z < 1.0) {\n"                                                                      \
    "            return 1.0 - let_in * (1.0 - sun_filter(i, at, texel));\n"                                                                   \
    "        }\n"                                                                                                                             \
    "    }\n"                                                                                                                                 \
    "    return 1.0;\n"                                                                                                                       \
    "}\n" /* What a shadow takes from `lit` (bytes of light, the game's                                                                       \
           * scale), of which `sun` is the sun's own share, `seen` of the sun                                                                 \
           * reaching it: the sun's share, and of the rest of the light (the                                                                  \
           * sky, which a caster hides some of too) as much again as                                                                          \
           * `u_sun_gain` says -- worked out for each stage so that a shadow                                                                  \
           * on its sunlit floor takes away the `shadows` strength of the                                                                     \
           * light, as the cast shadows did; it scales with the sun's own                                                                     \
           * share, so a face the sun barely reaches loses little and one                                                                     \
           * turned away loses nothing. */                                                                                                    \
    "vec3 sun_shade(vec3 lit, vec3 sun, float seen) {\n"                                                                                      \
    "    if (u_sun_gain < 0.0) { return vec3(255.0 * seen); }\n"                                                                              \
    "    if (seen >= 1.0) { return lit; }\n"                                                                                                  \
    "    vec3 rest = max(lit - sun, vec3(0.0));\n"                                                                                            \
    "    float of_sun = dot(sun, vec3(0.299, 0.587, 0.114));\n"                                                                               \
    "    float of_rest = dot(rest, vec3(0.299, 0.587, 0.114));\n"                                                                             \
    "    float more = of_rest > 0.0\n"                                                                                                        \
    "        ? clamp((u_sun_gain - 1.0) * of_sun / of_rest, 0.0, 1.0) : 0.0;\n"                                                               \
    "    vec3 lost = sun + rest * more;\n"                                                                                                    \
    "    return max(lit - (1.0 - seen) * lost, vec3(0.0));\n"                                                                                 \
    "}\n"

#endif /* PORT_GL_SUN_H */
