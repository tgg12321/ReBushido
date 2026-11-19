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
#include "port_remaster.h"

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
    /* how bright: 1 lights a white surface facing it, close to it, to its
     * own colour (a texel shown as it is, which is what the game's light
     * of 128 does); the stage's sun at noon is about 1 too. Any number
     * above 0; above 1 is brighter than the texture's own colour */
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

/* Makes the buffers, the shadows' maps and their programs. Called where
 * the backend starts (gl_draw.c). Returns why it could not, or NULL. */
extern const char *port_GlLightsStart(void *(*getproc)(const char *name));
/* The `lights` setting: whether the list lights the picture at all. */
extern void port_GlLightsSetOn(s32 on);
extern s32 port_GlLightsIsOn(void);
/* A game frame's picture begins (gl_draw.c, the backend's `begin`): the
 * list handed over last is taken as the frame's, the one before it kept. */
extern void port_GlLightsTake(void);
/* The picture now drawn is `phase` of PORT_TWEEN_ONE between the frame on
 * show and the one after it, or -1 for the game's own frame (gl_shadow.c
 * tells it as it tells the shadows). */
extern void port_GlLightsSetPhase(s32 phase);
/* A picture begins: what was worked out for the last is forgotten. */
extern void port_GlLightsNewPicture(void);
/* This picture's lights placed, sorted into the grid and handed to the
 * card, and the shadows of the strongest few drawn, once a picture, before
 * anything reads them. Leaves the card's state forgotten and the picture's
 * framebuffer bound, as port_GlSunPrepare does. */
extern void port_GlLightsPrepare(void);
/* For the benchmark and the provers: `count` lights of the renderer's own
 * made up about the fight, the first PORT_GL_LIGHTS_SHADOWED casting,
 * in the place of a frame's empty list (gl_bench.h, `lights=N`). */
extern void port_GlLightsSetMadeUp(s32 count);

/* Where a program has the lights' uniforms, and the lights given to it. */
typedef struct {
    s32 count;
    s32 grid;
    s32 map;
} PortGlLightsAt;

extern void port_GlLightsLocate(u32 program, PortGlLightsAt *at);
extern void port_GlLightsBind(const PortGlLightsAt *at);

/* What the run had: the most lights a picture had, and those of the last
 * with shadows. */
extern s32 port_GlLightsShown(void);
extern s32 port_GlLightsShadowed(void);

/* The texture unit the lights' shadow maps are read from, and the
 * buffers' binding points (std430), clear of the cast shadows' 0 to 3. */
#define PORT_GL_LIGHTS_MAP_UNIT 13
#define PORT_GL_LIGHTS_BINDING 4
#define PORT_GL_LIGHTS_GRID_BINDING 5
#define PORT_GL_LIGHTS_LIST_BINDING 6
/* The grid the lights are sorted into, over the ground about the camera:
 * cells of this many world units a side, this many a side. */
#define PORT_GL_LIGHTS_CELL 256
#define PORT_GL_LIGHTS_CELLS 64

/* The shader's side: `lights_add(world, normal)`, the light the list adds
 * at a point of the world whose surface faces `normal` (of length one), in
 * the game's bytes of light (128 a texel at its own colour), to be added
 * to what the stage's own lights give there. Needs GLSL 4.3. */
#define PORT_GL_LIGHTS_SHADER                                                       \
    "struct PortLight {\n"                                                          \
    "    vec4 place;\n" /* xyz, and the radius */                                   \
    "    vec4 color;\n" /* rgb in bytes of light, and the kind */                   \
    "    vec4 aim;\n"   /* a spot's direction, and the cosine inside */             \
    "    vec4 extra;\n" /* the cosine outside, the shadow's first layer */          \
    "};\n"                                                                          \
    "layout(std430, binding = 4) readonly buffer PortLights {\n"                    \
    "    PortLight port_light[];\n"                                                 \
    "};\n"                                                                          \
    "layout(std430, binding = 5) readonly buffer PortLightGrid {\n"                 \
    "    vec4 grid_origin;\n" /* the grid's corner, x and z */                      \
    "    uint grid_cell[];\n" /* each cell's first, and how many */                 \
    "};\n"                                                                          \
    "layout(std430, binding = 6) readonly buffer PortLightList {\n"                 \
    "    uint grid_list[];\n"                                                       \
    "};\n"                                                                          \
    "layout(std430, binding = 7) readonly buffer PortLightFaces {\n"                \
    "    mat4 light_face[];\n"                                                      \
    "};\n"                                                                          \
    "uniform int u_lights_count;\n"                                                 \
    "uniform sampler2DArrayShadow u_lights_map;\n"                                  \
    "float light_shadow(int first, vec3 to_point, vec3 world, vec3 n) {\n"          \
    "    vec3 a = abs(to_point);\n"                                                 \
    "    int face = a.x >= a.y && a.x >= a.z ? (to_point.x > 0.0 ? 0 : 1)\n"        \
    "             : a.y >= a.z ? (to_point.y > 0.0 ? 2 : 3)\n"                      \
    "                          : (to_point.z > 0.0 ? 4 : 5);\n"                     \
    "    float far_of = max(a.x, max(a.y, a.z));\n"                                 \
    "    vec3 p = world + n * far_of * 0.006;\n"                                    \
    "    vec4 c = light_face[first + face] * vec4(p, 1.0);\n"                       \
    "    vec3 at = c.xyz / c.w * 0.5 + vec3(0.5);\n"                                \
    "    return texture(u_lights_map, vec4(at.xy, float(first + face), at.z));\n"   \
    "}\n"                                                                           \
    "vec3 lights_add(vec3 world, vec3 n) {\n"                                       \
    "    if (u_lights_count == 0) { return vec3(0.0); }\n"                          \
    "    ivec2 cell = ivec2(floor((world.xz - grid_origin.xy)\n"                    \
    "                             / grid_origin.z));\n"                             \
    "    int side = int(grid_origin.w);\n"                                          \
    "    if (any(lessThan(cell, ivec2(0))) || any(greaterThanEqual(cell,\n"         \
    "                                         ivec2(side)))) {\n"                   \
    "        return vec3(0.0);\n"                                                   \
    "    }\n"                                                                       \
    "    int at = (cell.y * side + cell.x) * 2;\n"                                  \
    "    uint first = grid_cell[at];\n"                                             \
    "    uint count = grid_cell[at + 1];\n" /* RB_LIGHTS_DEBUG: how many            \
                                               lights the pixel's cell names,       \
                                               in red */                            \
    "    if (u_lights_count < 0) { return vec3(float(count) * 60.0, 0.0, 0.0); }\n" \
    "    vec3 sum = vec3(0.0);\n"                                                   \
    "    for (uint k = 0u; k < count; k++) {\n"                                     \
    "        PortLight l = port_light[grid_list[first + k]];\n"                     \
    "        vec3 to_light = l.place.xyz - world;\n"                                \
    "        float d = length(to_light);\n"                                         \
    "        if (d >= l.place.w || d < 1e-3) { continue; }\n"                       \
    "        vec3 dir = to_light / d;\n"                                            \
    "        float turn = dot(n, dir);\n"                                           \
    "        if (turn <= 0.0) { continue; }\n"                                      \
    "        float near = d / l.place.w;\n"                                         \
    "        float fall = clamp(1.0 - near * near, 0.0, 1.0);\n"                    \
    "        fall *= fall;\n"                                                       \
    "        if (l.color.w > 0.5) {\n"                                              \
    "            fall *= smoothstep(l.extra.x, l.aim.w, dot(-dir, l.aim.xyz));\n"   \
    "        }\n"                                                                   \
    "        if (fall <= 0.0) { continue; }\n"                                      \
    "        if (l.extra.y >= 0.0 && fall * turn > 0.02) {\n"                       \
    "            fall *= light_shadow(int(l.extra.y), -to_light, world, n);\n"      \
    "        }\n"                                                                   \
    "        sum += l.color.rgb * (turn * fall);\n"                                 \
    "    }\n"                                                                       \
    "    return sum;\n"                                                             \
    "}\n"

#endif /* PORT_GL_LIGHTS_H */
