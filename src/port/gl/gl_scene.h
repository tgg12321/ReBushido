#ifndef GL_SCENE_H
#define GL_SCENE_H

/* The stage's textures replaced by whole pages (docs/MODS.md, "The
 * scene's textures by page"): a mod's picture of a texture page of the
 * stage, seen through one of its colour tables, stands in for every face
 * of the stage that reads that page through that table -- the ground's
 * mesh (gl_stage.c) and the walls, the props and whatever else of the
 * stage the renderer draws as primitives (gl_draw.c). A stage's textures
 * are then a few files, a page and a table each, and not hundreds of
 * pieces. A fighter's pages are replaced the same way, keyed by what his
 * model's file loaded (docs/MODS.md, "A fighter's pages"), a partner's of
 * two on two in the second bank of video memory as well.
 *
 * The page's key is of the stage's own texels in it and the table as the
 * stage's file loaded it (port_mods.h, port_TexturePageKey), so it is the
 * same in every run. The pictures are kept on the card as the layers of
 * one array, so a frame's batches are not broken by them: which layer a
 * page and a table have is a texel of a table of its own that the
 * shaders look up. Where the game changes the colours a face is drawn in
 * -- the fog's rows, a tint -- the picture's colours are changed by as
 * much (PORT_GL_SCENE_SHADER).
 *
 * With no mod's textures and nothing to write out nothing here is done,
 * and no shader takes the path: the picture is what it was. */

#include "common.h"
#include "port_remaster.h"

/* The texture units the three tables are bound to while the scene is
 * drawn: the units before them are the mirror of video memory, the
 * ground's slots, a 2D replacement and the fighters' models'. */
#define PORT_GL_SCENE_LOOKUP_UNIT 6
#define PORT_GL_SCENE_PAGES_UNIT 7
#define PORT_GL_SCENE_BASES_UNIT 8

/* Looks up the GL functions it uses and makes its tables: NULL, or why it
 * cannot. */
extern const char *port_GlSceneStart(void *(*getproc)(const char *name));
/* Off with the console's own picture held to (`--gl-exact`). */
extern void port_GlSceneSetOn(s32 on);
/* Once a frame, before any of it is drawn: the stage's textures read
 * again where the game loaded a stage, and the pages whose texels were
 * written over looked up again. */
extern void port_GlSceneNewFrame(void);
/* Whether a mod's page stands in for this primitive's texture: a face of
 * the stage or of a fighter, in perspective, through no texture window. */
extern s32 port_GlSceneFind(const PortRemasterPrim *prim);
/* The game wrote this rectangle of video memory. */
extern void port_GlSceneWroteVram(s32 x, s32 y, s32 w, s32 h);
/* ... or lines `y` to `y + h - 1` of two on two's second bank, which
 * changed since the last frame (port_gpu.h). */
extern void port_GlSceneWroteBank(s32 y, s32 h);
/* Binds the three tables to their units for a draw and says whether any
 * page has a picture: the shaders' `u_scene_on`. */
extern s32 port_GlSceneBind(void);
/* The coarsest level of the pictures' mipmaps a shader may read, for
 * `u_scene_levels`. */
extern float port_GlSceneLevels(void);

/* The shaders' part of it, put into the fragment shaders of the ground's
 * mesh and of the renderer. The shader that takes it has `vram(x, y)`
 * before it and defines `scene_now(index)`, the colour of a table's index
 * as this fragment is to draw it now (0 to 31 a part), after it.
 *
 * `scene_layer` is which layer of the pictures a page and a table have,
 * or -1. `scene_color` reads the picture at a texture point (with half a
 * texel added, as the vertices have it), held inside `box`, the
 * rectangle of the face's own texels, so that nothing of a texture
 * packed beside it in the page is smoothed in; and changes its colours as
 * the game has changed the table's since the picture was made: each part
 * is the picture's times what the colours about that point are now over
 * what they were (one more than each, so that a black stays a black and a
 * colour of the fog over a black still comes). Its alpha below a half is
 * a texel of nothing. */
#define PORT_GL_SCENE_SHADER                                                      \
    "uniform usampler2D u_scene_lookup;\n"                                        \
    "uniform sampler2DArray u_scene_pages;\n"                                     \
    "uniform usampler2D u_scene_bases;\n"                                         \
    "uniform int u_scene_on;\n"                                                   \
    "uniform float u_scene_levels;\n"                                             \
    "vec3 scene_now(uint index);\n"                                               \
    "int scene_layer(ivec4 page, uint bits) {\n"                                  \
    "    if (u_scene_on == 0) { return -1; }\n"                                   \
    "    int column = (((page.x >> 6) & 15) + ((page.y >> 8) & 1) * 16) * 64\n"   \
    "               + ((page.z >> 4) & 63);\n"                                    \
    "    int row = page.w >= 1024 ? 512 + ((page.w - 1024) & 255)\n"              \
    "                             : (page.w & 511);\n"                            \
    "    uint entry = texelFetch(u_scene_lookup, ivec2(column, row), 0).r;\n"     \
    "    int layer = int(entry & 4095u) - 1;\n"                                   \
    "    if (layer < 0 || ((entry >> 12) & 3u) != bits) { return -1; }\n"         \
    "    return layer;\n"                                                         \
    "}\n"                                                                         \
    "uint scene_index(ivec4 page, uint bits, int u, int v) {\n"                   \
    "    int y = page.y + (v & 255);\n"                                           \
    "    u &= 255;\n"                                                             \
    "    if (bits == 0u) {\n"                                                     \
    "        uint word = vram(page.x + (u >> 2), y);\n"                           \
    "        return (word >> ((uint(u) & 3u) * 4u)) & 15u;\n"                     \
    "    }\n"                                                                     \
    "    uint word = vram(page.x + (u >> 1), y);\n"                               \
    "    return (word >> ((uint(u) & 1u) * 8u)) & 255u;\n"                        \
    "}\n"                                                                         \
    "vec3 scene_channels(uint texel) {\n"                                         \
    "    return vec3(float(texel & 31u), float((texel >> 5) & 31u),\n"            \
    "                float((texel >> 10) & 31u));\n"                              \
    "}\n"                                                                         \
    "vec4 scene_color(int layer, ivec4 page, uint bits, vec2 uv,\n"               \
    "                 vec2 across, vec2 down, vec4 box) {\n"                      \
    "    float side = float(textureSize(u_scene_pages, 0).x) / 256.0;\n"          \
    "    float reach = min(length(across), length(down)) * side;\n"               \
    "    float level = clamp(log2(max(reach, 1e-6)), 0.0, u_scene_levels);\n"     \
    "    float half_texel = 0.5 * exp2(level) / side;\n"                          \
    "    vec2 middle = 0.5 * (box.xy + box.zw);\n"                                \
    "    vec2 low = min(box.xy + vec2(half_texel), middle);\n"                    \
    "    vec2 high = max(box.zw - vec2(half_texel), middle);\n"                   \
    "    vec2 at = clamp(uv, low, high);\n"                                       \
    "    vec4 got = textureGrad(u_scene_pages, vec3(at / 256.0, float(layer)),\n" \
    "                           across / 256.0, down / 256.0);\n"                 \
    "    if (got.a < 0.5) { return vec4(0.0); }\n"                                \
    "    vec3 shown = got.rgb / got.a * 31.0;\n"                                  \
    "    if (bits > 1u) { return vec4(shown, 1.0); }\n"                           \
    "    vec2 corner = at - vec2(0.5);\n"                                         \
    "    ivec2 first = ivec2(floor(corner));\n"                                   \
    "    vec2 part = corner - vec2(first);\n"                                     \
    "    vec3 was = vec3(0.0);\n"                                                 \
    "    vec3 now = vec3(0.0);\n"                                                 \
    "    float weight = 0.0;\n"                                                   \
    "    for (int j = 0; j < 2; j++) {\n"                                         \
    "        for (int i = 0; i < 2; i++) {\n"                                     \
    "            uint index = scene_index(page, bits, first.x + i,\n"             \
    "                                     first.y + j);\n"                        \
    "            uint base = texelFetch(u_scene_bases,\n"                         \
    "                                   ivec2(int(index), layer), 0).r;\n"        \
    "            if (base == 0u) { continue; }\n"                                 \
    "            float how = (i == 0 ? 1.0 - part.x : part.x)\n"                  \
    "                      * (j == 0 ? 1.0 - part.y : part.y) + 1e-4;\n"          \
    "            was += scene_channels(base) * how;\n"                            \
    "            now += scene_now(index) * how;\n"                                \
    "            weight += how;\n"                                                \
    "        }\n"                                                                 \
    "    }\n"                                                                     \
    "    if (weight > 0.0) {\n"                                                   \
    "        shown = (shown + vec3(1.0)) * (now / weight + vec3(1.0))\n"          \
    "              / (was / weight + vec3(1.0)) - vec3(1.0);\n"                   \
    "    }\n"                                                                     \
    "    return vec4(clamp(shown, vec3(0.0), vec3(31.0)), 1.0);\n"                \
    "}\n"

#endif /* GL_SCENE_H */
