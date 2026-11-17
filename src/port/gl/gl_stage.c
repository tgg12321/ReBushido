/* The stage's ground as one static mesh, for the remastered picture
 * (gl_stage.h; docs/PORT.md, "Remastered", "The ground as one mesh").
 *
 * The mesh is built from the stage's own data when a stage is first
 * drawn: every tile of the 32 by 32 grid, every face of its mesh at the
 * finest detail the game ever cuts it to, with the texture page and
 * colour table the face names, the texture points it was given, and the
 * colour the coprocessor lights it with. It goes to the card as one
 * vertex buffer and is drawn with the camera's own transform, which the
 * side channel carries out of the renderer: the same rotation, the same
 * projection distance and the same screen offsets the game's own tiles
 * were projected with, so the mesh lands where they landed.
 *
 * The stage's fog is not a shader on the console: it is 31 rows of
 * colour tables, each blended further towards the fog's colour, copied
 * over the row the textures read in the middle of the frame. The mesh
 * cannot have one row for all of it, so it reads the row the fog would
 * have had for that pixel's own depth -- the levels are all still in
 * video memory, which the card reads texels from. That is the game's own
 * fog, to the colour, and per pixel instead of per face. The two rows a
 * depth lies between are read and mixed, so the fog runs on instead of
 * stepping where a band ends, which on a long floor was a straight
 * bright line across it.
 *
 * The mesh writes a depth of its own, in the world's own units, into the
 * buffer the renderer keeps beside the colour buffer, and the rest of
 * the scene is tested against it (gl_draw.c): a piece of ground between
 * the camera and a fighter veils him again, which the painter's order
 * alone could not do, the mesh being painted where the farthest of the
 * frame's tiles was. Nothing but the mesh writes that depth, so the
 * order the scene's own primitives are painted in is untouched.
 *
 * Nothing here is in the build without a window; nothing here changes a
 * value the game computes. */
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>

#include "common.h"
#include "gl.h"
#include "gl_stage.h"
#include "port_gpu.h"
#include "port_remaster.h"

/* the game's own headers, for the shapes of what the mesh is built from:
 * a tile, a light setup and the coprocessor. The names themselves come
 * through port_remaster.h, which this file's own kind of build can
 * reach (tools/port_sdl_build.py). */
#include "light.h"
#include "port_gte.h"
#include "stage.h"

/* ---- GL, as much of it as this file uses ---- */

typedef unsigned int GLenum;
typedef unsigned char GLboolean;
typedef unsigned int GLbitfield;
typedef int GLint;
typedef unsigned int GLuint;
typedef int GLsizei;
typedef float GLfloat;
typedef double GLclampd;
typedef char GLchar;
typedef ptrdiff_t GLsizeiptr;

#define GL_FALSE 0
#define GL_TRUE 1
#define GL_TRIANGLES 0x0004
#define GL_UNSIGNED_BYTE 0x1401
#define GL_SHORT 0x1402
#define GL_UNSIGNED_SHORT 0x1403
#define GL_FLOAT 0x1406
#define GL_TEXTURE_2D 0x0DE1
#define GL_TEXTURE0 0x84C0
#define GL_FRAMEBUFFER 0x8D40
#define GL_DEPTH_BUFFER_BIT 0x00000100
#define GL_DEPTH_TEST 0x0B71
#define GL_LESS 0x0201
#define GL_ARRAY_BUFFER 0x8892
#define GL_STATIC_DRAW 0x88E4
#define GL_VERTEX_SHADER 0x8B31
#define GL_FRAGMENT_SHADER 0x8B30
#define GL_COMPILE_STATUS 0x8B81
#define GL_LINK_STATUS 0x8B82
#define GL_BLEND 0x0BE2
#define GL_SCISSOR_TEST 0x0C11

#define PORT_GL_STAGE_FUNCTIONS                                                \
    GL(void, glViewport, (GLint, GLint, GLsizei, GLsizei))                     \
    GL(void, glScissor, (GLint, GLint, GLsizei, GLsizei))                      \
    GL(void, glEnable, (GLenum))                                               \
    GL(void, glDisable, (GLenum))                                              \
    GL(void, glCullFace, (GLenum))                                             \
    GL(void, glFrontFace, (GLenum))                                            \
    GL(void, glClear, (GLbitfield))                                            \
    GL(void, glClearDepth, (GLclampd))                                         \
    GL(void, glDepthFunc, (GLenum))                                            \
    GL(void, glDepthMask, (GLboolean))                                         \
    GL(void, glBindTexture, (GLenum, GLuint))                                  \
    GL(void, glActiveTexture, (GLenum))                                        \
    GL(void, glBindFramebuffer, (GLenum, GLuint))                              \
    GL(void, glGenBuffers, (GLsizei, GLuint *))                                \
    GL(void, glBindBuffer, (GLenum, GLuint))                                   \
    GL(void, glBufferData, (GLenum, GLsizeiptr, const void *, GLenum))         \
    GL(void, glGenVertexArrays, (GLsizei, GLuint *))                           \
    GL(void, glBindVertexArray, (GLuint))                                      \
    GL(void, glVertexAttribPointer,                                            \
       (GLuint, GLint, GLenum, GLboolean, GLsizei, const void *))              \
    GL(void, glVertexAttribIPointer,                                           \
       (GLuint, GLint, GLenum, GLsizei, const void *))                         \
    GL(void, glEnableVertexAttribArray, (GLuint))                              \
    GL(GLuint, glCreateShader, (GLenum))                                       \
    GL(void, glShaderSource,                                                   \
       (GLuint, GLsizei, const GLchar * const *, const GLint *))               \
    GL(void, glCompileShader, (GLuint))                                        \
    GL(void, glGetShaderiv, (GLuint, GLenum, GLint *))                         \
    GL(void, glGetShaderInfoLog, (GLuint, GLsizei, GLsizei *, GLchar *))       \
    GL(GLuint, glCreateProgram, (void))                                        \
    GL(void, glAttachShader, (GLuint, GLuint))                                 \
    GL(void, glLinkProgram, (GLuint))                                          \
    GL(void, glGetProgramiv, (GLuint, GLenum, GLint *))                        \
    GL(void, glGetProgramInfoLog, (GLuint, GLsizei, GLsizei *, GLchar *))      \
    GL(void, glUseProgram, (GLuint))                                           \
    GL(GLint, glGetUniformLocation, (GLuint, const GLchar *))                  \
    GL(void, glUniform1i, (GLint, GLint))                                      \
    GL(void, glUniform1f, (GLint, GLfloat))                                    \
    GL(void, glUniform2f, (GLint, GLfloat, GLfloat))                           \
    GL(void, glUniform3f, (GLint, GLfloat, GLfloat, GLfloat))                  \
    GL(void, glUniform4i, (GLint, GLint, GLint, GLint, GLint))                 \
    GL(void, glUniform4ui, (GLint, GLuint, GLuint, GLuint, GLuint))            \
    GL(void, glUniform1iv, (GLint, GLsizei, const GLint *))                    \
    GL(void, glUniform2iv, (GLint, GLsizei, const GLint *))                    \
    GL(void, glUniform1uiv, (GLint, GLsizei, const GLuint *))                  \
    GL(void, glUniformMatrix3fv, (GLint, GLsizei, GLboolean, const GLfloat *)) \
    GL(void, glDrawArrays, (GLenum, GLint, GLsizei))

#define GL(result, name, args) static result(*name) args;
PORT_GL_STAGE_FUNCTIONS
#undef GL

/* ---- what the game's own numbers are ---- */

/* A tile's record in a mesh, in bytes, and what is in it
 * (src/main/render_asm.c has the same under its own names: EMIT_...).
 * The first word is the texture page under the tile's own bits and the
 * primitive's code, and it is the word the renderer gives the
 * coprocessor as a colour, whose low bytes the lighting operation does
 * not read. */
#define RECORD_SIZE 0x18
#define RECORD_TPAGE 0x00
#define RECORD_BITS 0x02 /* a quad sorted farther, and how often to cut */
#define RECORD_CODE 0x03
#define RECORD_CLUT 0x04
#define RECORD_UV0 0x06
#define RECORD_TRI_CORNERS 0x0C
#define RECORD_TRI_NORMAL 0x10
#define RECORD_QUAD_CORNERS 0x0E
#define RECORD_QUAD_NORMAL_Z 0x12
#define RECORD_QUAD_NORMAL_XY 0x14

#define BITS_CUT_FIELD(bits) (((bits) >> 1) & 3)
/* The finest the game ever cuts a tile: the two cells nearest the camera
 * (g_stage_detail_pattern, STAGE_CELL_DETAIL). */
#define TILE_CUTS_FINEST 2
/* A group's routine is found by its type less its low three bits, and
 * the tiles have two: triangles and quads. */
#define GROUP_TYPE_SHIFT 3
#define GROUP_TRIANGLES 0
#define GROUP_QUADS 1
/* The light setup the tiles are lit by (TILE_LIGHT_SETUP). */
#define TILE_LIGHTS 4
/* A packed vertex is three halfwords. */
#define PACKED_VERTEX_SIZE 6
/* The high byte of a texture point. */
#define UV_V_MASK 0xFF00

/* The chip's own fields, as the decoder reads them
 * (src/port/remaster.c). */
#define MODE_PAGE_X_MASK 0xF
#define MODE_PAGE_X_STEP 64
#define MODE_PAGE_Y_BIT 0x10
#define MODE_PAGE_Y_STEP 256
#define MODE_BLEND_SHIFT 5
#define MODE_BLEND_MASK 3
#define MODE_DEPTH_SHIFT 7
#define MODE_DEPTH_MASK 3
#define CLUT_X_MASK 0x3F
#define CLUT_X_STEP 16
#define CLUT_Y_SHIFT 6
#define CODE_RAW 0x01
#define CODE_HALF 0x02

/* The flags of a vertex, as the renderer's own shader has them
 * (gl_draw.c). */
#define FLAG_TEXTURED 1
#define FLAG_RAW 2
#define FLAG_HALF 4

/* A game address as a pointer: the port keeps the game's memory below
 * 4 GB, so the low 32 bits are the whole of an address (docs/PORT.md,
 * "64 bits"). This is common.h's ADDR(), which is written for the files
 * compiled with the game's own switches; this one is not. */
#define STAGE_AT(value) ((PortAddress)(u32)(value))
#define STAGE_WORD(at, offset) (*(const u32 *)((const u8 *)(at) + (offset)))
#define STAGE_HALF(at, offset) (*(const u16 *)((const u8 *)(at) + (offset)))
#define STAGE_SHALF(at, offset) (*(const s16 *)((const u8 *)(at) + (offset)))
#define STAGE_BYTE(at, offset) (*(const u8 *)((const u8 *)(at) + (offset)))

/* How far the mesh is drawn into the depth buffer. The mesh's own depth
 * is in the scale the renderer drew the tiles at (`far_shift`), so what
 * it is divided by is the range of a depth in the world's own units,
 * scaled down by the same shift: the buffer then holds a depth in the
 * world's units, which is the one scale the rest of the scene can be
 * held against it in (gl.h, PORT_GL_DEPTH_RANGE). */
#define DEPTH_RANGE PORT_GL_DEPTH_RANGE
/* Nothing nearer than this is drawn: the near plane, in the same units.
 * The clip is at half of it (the plane is `z - near`, which the card
 * tests against `-w`). */
#define NEAR_PLANE 16.0f
/* How far back a tile's seam number pushes its faces, in world units a
 * number (port_GlStageSeams): sixteen steps of the depth buffer's, which
 * holds a 64th of a unit. */
#define SEAM_STEP 0.25f

/* The most tiles a stage has (STAGE_MAX_TILES), rounded up so that which
 * of them are drawn opened up fits in whole words. */
#define TILES_MAX 512
#define TILE_WORDS (TILES_MAX / 32)

/* ---- the mesh ---- */

/* One corner of a tile's face while it is being cut up, as the game
 * keeps it (TileCorner): where it is in the tile, and its texture
 * point. */
typedef struct {
    s32 x;
    s32 y;
    s32 z;
    u32 uv;
} StageCorner;

/* A vertex of the mesh. The texture window is one for the whole of it
 * and is a uniform. */
typedef struct {
    GLfloat x; /* where it is in the world */
    GLfloat y;
    GLfloat z;
    GLfloat u; /* its texture point, a texel's middle */
    GLfloat v;
    u8 color[4]; /* red, green, blue, the flags */
    s16 page[4]; /* the page's corner, the colour table's first pixel */
    u8 info[4];  /* the texel's bits, the mix, nothing, nothing */
    u16 tile;    /* which tile of the stage it belongs to */
    u16 pad;
    /* The corners of the face this vertex is of, in the world, the
     * fourth the third's again for a triangle. Every vertex of a face
     * carries all of them because the two tests the renderer makes
     * before it draws a tile's face are of the face and not of the
     * corner (render_EmitTile): it leaves out a face turned away from
     * the camera (render_FaceFront, the cross product of its first
     * three corners on the screen) and a face whose farthest corner is
     * nearer than the cutoff (RENDER_OT_CUTOFF), which is how the
     * ground between the camera and the fight is kept out of the way. */
    GLfloat corner[4][3];
} StageVertex; /* 92 bytes */

static struct {
    s32 is_started;
    s32 is_on;
    s32 is_every; /* on for every stage, whatever g_stage_proved says */
    char problem[256];
    /* which stage the last line of news was about, and the line itself
     * (port_GlStageNews): -1 before there has been one */
    s32 said_stage;
    s32 has_news;
    char news[128];
    GLuint program;
    GLuint buffer;
    GLuint array;
    GLint at_view;
    GLint at_camera;
    GLint at_h;
    GLint at_screen;
    GLint at_target;
    GLint at_scale;
    GLint at_near;
    GLint at_depth;
    GLint at_vram;
    GLint at_window;
    GLint at_open;
    GLint at_fog_levels;
    GLint at_fog_slot;
    GLint at_fog_clut;
    GLint at_fog_rect;
    GLint at_depth_shift;
    GLint at_cutoff;
    GLint at_unit;
    /* the mesh as it stands */
    s32 version; /* which stage's data it was built from */
    PortAddress tiles;
    s32 tile_count;
    s32 faces;
    s32 missed;
    s32 vertex_count;
    StageVertex *building;
    s32 built;
    s32 room;
    /* the view a picture between two of the game's frames draws with */
    s32 has_own_view;
    PortStageView own_view;
    /* this frame */
    s32 is_drawn;
    u32 drawn_vertices;
    u32 draws;
} g_stage;

/* ---- the shaders ---- */

static const char g_stage_vertex_text[] =
    "#version 330 core\n"
    "layout(location = 0) in vec3 a_place;\n" /* where it is in the world */
    "layout(location = 1) in vec2 a_texture;\n"
    "layout(location = 2) in uvec4 a_color;\n" /* red, green, blue, flags */
    "layout(location = 3) in ivec4 a_page;\n"
    "layout(location = 4) in uvec4 a_info;\n"
    "layout(location = 5) in uint a_tile;\n"
    /* the corners of the face this vertex is of */
    "layout(location = 6) in vec3 a_corner0;\n"
    "layout(location = 7) in vec3 a_corner1;\n"
    "layout(location = 8) in vec3 a_corner2;\n"
    "layout(location = 9) in vec3 a_corner3;\n"
    /* the view the renderer drew the stage's tiles with: its rotation
     * already scaled as the tiles' own projection scales it, where the
     * camera is, the distance to the projection plane, and where the
     * middle of the screen is (the drawing offset and a wider picture's
     * margin in it) */
    "uniform mat3 u_view;\n"
    "uniform vec3 u_camera;\n"
    "uniform float u_h;\n"
    "uniform vec2 u_screen;\n"
    "uniform vec2 u_target;\n"
    "uniform vec2 u_scale;\n"
    "uniform float u_near;\n"
    "uniform int u_depth_shift;\n"
    "uniform int u_cutoff;\n"
    "uniform float u_unit;\n" /* a seam's step, in the tiles' scale */
    "out vec2 v_texture;\n"
    "flat out float v_push;\n"
    "out float v_depth;\n"
    "flat out int v_cull;\n"
    /* a tile's face has one colour: there is nothing to interpolate */
    "flat out vec3 v_color;\n"
    "flat out uvec4 v_flags;\n"
    "flat out ivec4 v_page;\n"
    "flat out uvec4 v_info;\n"
    "flat out uint v_tile;\n"
    "flat out float v_slot;\n"
    /* the ordering-table slot a depth is sorted at
     * (render_OtIndexShifted) */
    "int slot_of(int depth) {\n"
    "    int scaled = depth >> u_depth_shift;\n"
    "    int octave = scaled >> 11;\n"
    "    int fraction = (scaled & 2047) - 4096;\n"
    "    return (fraction >> (octave & 31)) & 4095;\n"
    "}\n"
    "void main() {\n"
    "    vec3 v = u_view * (a_place - u_camera);\n"
    /* The two tests the renderer makes of a whole face before it draws
     * it (render_EmitTile). A face is turned away from the camera when
     * the cross product of its first three corners on the screen comes
     * out positive (render_FaceFront: MAC0 < 0 is front); the corners
     * divided by their own depths, that sign is the sign of the volume
     * the three make with the eye, which is worked out here and holds
     * whether or not the face crosses the plane of the eye. And the
     * depth the face is sorted at is its farthest corner's, which is
     * what says whether it is nearer than the cutoff. */
    "    vec3 c0 = u_view * (a_corner0 - u_camera);\n"
    "    vec3 c1 = u_view * (a_corner1 - u_camera);\n"
    "    vec3 c2 = u_view * (a_corner2 - u_camera);\n"
    "    vec3 c3 = u_view * (a_corner3 - u_camera);\n"
    "    float far = max(max(c0.z, c1.z), max(c2.z, c3.z));\n"
    "    bool front = dot(c0, cross(c1, c2)) < 0.0;\n"
    "    v_cull = (!front ||\n"
    "              slot_of(int(clamp(far, 0.0, 65535.0))) < u_cutoff)\n"
    "             ? 1 : 0;\n"
    /* the slot is also what a fighter is held against (gl_draw.c,
     * port_GlGroundSlotsBegin) */
    "    v_slot = float(slot_of(int(clamp(far, 0.0, 65535.0))));\n"
    /* The projection the coprocessor makes, written so that every
     * component is linear in the point: the card's own divide by w puts
     * the corner where the GTE put it, and a face that crosses the near
     * plane is cut where a straight line says.
     *
     *   screen = h * v.xy / v.z + u_screen
     *   buffer = screen * u_scale + 0.5
     *   clip   = (buffer / u_target * 2 - 1) * v.z
     */
    "    vec2 at = 2.0 * u_scale * u_h * v.xy\n"
    "            + (2.0 * u_scale * u_screen + vec2(1.0)) * v.z;\n"
    "    gl_Position = vec4(at / u_target - v.z, v.z - u_near, v.z);\n"
    "    v_texture = a_texture;\n"
    "    v_depth = v.z;\n"
    /* Where two tiles lie over each other (port_GlStageSeams) their
     * faces are the one plane, and the depth's rounding alone would say
     * which a pixel shows, flipping pixel by pixel as the camera moves.
     * The console painted the face sorted nearer -- the one whose
     * farthest corner is nearer -- over the other, so each face is
     * pushed back by a 256th of that corner's depth: the nearer face
     * wins the strip, as on the console, by a margin that grows with
     * the depth as the rounding does. Where the two farthest corners are
     * as far, the tile's seam number settles it, a quarter of a world
     * unit a number. */
    "    v_push = max(far, 0.0) / 256.0 + float(a_info.z) * u_unit;\n"
    "    v_color = vec3(a_color.xyz);\n"
    "    v_flags = a_color;\n"
    "    v_page = a_page;\n"
    "    v_info = a_info;\n"
    "    v_tile = a_tile;\n"
    "}\n";

static const char g_stage_fragment_text[] =
    "#version 330 core\n"
    "uniform usampler2D u_vram;\n"
    "uniform uvec4 u_window;\n" /* the texture window, and or or */
    "uniform uint u_open[16];\n"
    "uniform int u_fog_levels;\n"
    "uniform int u_fog_slot[32];\n"
    "uniform ivec2 u_fog_clut[32];\n"
    "uniform ivec4 u_fog_rect;\n" /* the row the textures read */
    "uniform int u_depth_shift;\n"
    "uniform float u_depth;\n" /* what a depth is divided by */
    "in vec2 v_texture;\n"
    "in float v_depth;\n"
    "flat in float v_push;\n"
    "flat in int v_cull;\n"
    "flat in vec3 v_color;\n"
    "flat in uvec4 v_flags;\n"
    "flat in ivec4 v_page;\n"
    "flat in uvec4 v_info;\n"
    "flat in uint v_tile;\n"
    "flat in float v_slot;\n"
    "layout(location = 0) out vec4 o_color;\n"
    "layout(location = 1) out uint o_slot;\n"
    "uint vram(int x, int y) {\n"
    "    return texelFetch(u_vram, ivec2(x & 1023, y & 511), 0).r;\n"
    "}\n"
    /* the ordering-table slot a depth is sorted at
     * (render_OtIndexShifted) */
    "int slot_of(int depth) {\n"
    "    int scaled = depth >> u_depth_shift;\n"
    "    int octave = scaled >> 11;\n"
    "    int fraction = (scaled & 2047) - 4096;\n"
    "    return (fraction >> (octave & 31)) & 4095;\n"
    "}\n"
    /* Which level of the stage's fog this depth is drawn under: the
     * copies are linked into the ordering table at even steps of depth
     * and the table is drawn from the far end, so every copy farther
     * than this depth has been made and the last of them was the
     * nearest. That is the first level whose slot is farther than this
     * one, which is the count of the levels nearer than it. */
    "int fog_level(int slot) {\n"
    "    int level = 0;\n"
    "    for (int step = 16; step > 0; step >>= 1) {\n"
    "        if (u_fog_slot[level + step - 1] <= slot) { level += step; }\n"
    "    }\n"
    "    return min(level, u_fog_levels);\n"
    "}\n"
    "ivec2 g_clut;\n"      /* the colours this fragment reads */
    "ivec2 g_clut_near;\n" /* ... and the level before it, for the fog */
    "float g_fog_mix;\n"   /* how far between the two this fragment is */
    "uint texel_index(int u, int v, out bool has_clut) {\n"
    "    u = (u & int(u_window.x)) | int(u_window.y);\n"
    "    v = (v & int(u_window.z)) | int(u_window.w);\n"
    "    int y = v_page.y + v;\n"
    "    has_clut = true;\n"
    "    if (v_info.x == 0u) {\n"
    "        uint word = vram(v_page.x + (u >> 2), y);\n"
    "        return (word >> ((uint(u) & 3u) * 4u)) & 15u;\n"
    "    }\n"
    "    if (v_info.x == 1u) {\n"
    "        uint word = vram(v_page.x + (u >> 1), y);\n"
    "        return (word >> ((uint(u) & 1u) * 8u)) & 255u;\n"
    "    }\n"
    "    has_clut = false;\n"
    "    return vram(v_page.x + u, y);\n"
    "}\n"
    "vec3 channels(uint texel) {\n"
    "    return vec3(float(texel & 31u), float((texel >> 5) & 31u),\n"
    "                float((texel >> 10) & 31u));\n"
    "}\n"
    "vec3 texel_color(int u, int v, out uint texel) {\n"
    "    bool has_clut;\n"
    "    uint index = texel_index(u, v, has_clut);\n"
    "    if (!has_clut) {\n"
    "        texel = index;\n"
    "        return channels(texel);\n"
    "    }\n"
    "    texel = vram(g_clut.x + int(index), g_clut.y);\n"
    "    if (g_fog_mix >= 1.0) { return channels(texel); }\n"
    "    uint before = vram(g_clut_near.x + int(index),\n"
    "                       g_clut_near.y);\n"
    "    return mix(channels(before), channels(texel), g_fog_mix);\n"
    "}\n"
    "void main() {\n"
    /* A face that mixes with what is behind it is not the mesh's: a
     * tile the game is drawing opened up so that a fighter behind it
     * can be seen, or one whose own code says it mixes. The mesh is
     * drawn where the farthest tile of the frame was, which is not
     * where such a face belongs in the painting, so it is left to the
     * game's own primitive (port_GlStageIsTile). */
    "    bool open = (u_open[v_tile >> 5u] & (1u << (v_tile & 31u))) != 0u;\n"
    "    if (open || (v_flags.w & 4u) != 0u) { discard; }\n"
    /* the whole face is nearer than the cutoff: the ground between the
     * camera and the fight, which the renderer leaves out too */
    "    if (v_cull != 0) { discard; }\n"
    "    int depth = int(clamp(v_depth, 0.0, 65535.0));\n"
    "    int slot = slot_of(depth);\n"
    "    g_clut = v_page.zw;\n"
    "    g_clut_near = g_clut;\n"
    "    g_fog_mix = 1.0;\n"
    "    if (u_fog_levels > 0 && g_clut.y == u_fog_rect.y\n"
    "        && g_clut.x >= u_fog_rect.x\n"
    "        && g_clut.x < u_fog_rect.x + u_fog_rect.z) {\n"
    "        int level = fog_level(slot);\n"
    "        int column = g_clut.x - u_fog_rect.x;\n"
    "        int low = (level > 0) ? u_fog_slot[level - 1] : 0;\n"
    "        int high = (level < u_fog_levels)\n"
    "                 ? u_fog_slot[level] : low;\n"
    "        float part = (high > low)\n"
    "                   ? clamp(float(slot - low)\n"
    "                           / float(high - low), 0.0, 1.0)\n"
    "                   : 1.0;\n"
    "        float place = clamp(float(level) + part - 0.5,\n"
    "                            0.0, float(u_fog_levels));\n"
    "        int first = int(floor(place));\n"
    "        int second = min(first + 1, u_fog_levels);\n"
    "        ivec2 near = u_fog_clut[first];\n"
    "        ivec2 far = u_fog_clut[second];\n"
    "        g_clut_near = ivec2(column + near.x, near.y);\n"
    "        g_clut = ivec2(column + far.x, far.y);\n"
    "        g_fog_mix = place - float(first);\n"
    "    }\n"
    "    vec3 color = v_color;\n"
    "    uint texel;\n"
    "    vec3 one_color = texel_color(int(floor(v_texture.x)),\n"
    "                                 int(floor(v_texture.y)), texel);\n"
    "    vec3 shown;\n"
    /* The texels this fragment covers, averaged, as the renderer does
     * it for a polygon of the scene (gl_draw.c, `steps`): the ground
     * runs away from the camera, so far off a fragment covers several
     * texels and reading one of them is the noise that crawls as the
     * camera turns. The mesh is drawn only when the picture is not
     * being held to the console's own pixels, so there is nothing to
     * ask: it always averages. */
    "    vec2 wide_across = dFdx(v_texture);\n"
    "    vec2 wide_down = dFdy(v_texture);\n"
    "    float span = max(max(abs(wide_across.x), abs(wide_across.y)),\n"
    "                     max(abs(wide_down.x), abs(wide_down.y)));\n"
    "    int steps = int(clamp(ceil(span), 1.0, 4.0));\n"
    "    if (steps > 1) {\n"
    "        vec3 sum = vec3(0.0);\n"
    "        float weight = 0.0;\n"
    "        uint any = 0u;\n"
    "        float part = 1.0 / float(steps);\n"
    "        for (int j = 0; j < steps; j++) {\n"
    "            for (int i = 0; i < steps; i++) {\n"
    "                vec2 off = (vec2(float(i), float(j)) + vec2(0.5))\n"
    "                         * part - vec2(0.5);\n"
    "                vec2 at = v_texture + off.x * wide_across\n"
    "                        + off.y * wide_down;\n"
    "                uint one;\n"
    "                vec3 got = texel_color(int(floor(at.x)),\n"
    "                                       int(floor(at.y)), one);\n"
    "                if (one != 0u) {\n"
    "                    sum += got;\n"
    "                    weight += 1.0;\n"
    "                    any = one;\n"
    "                }\n"
    "            }\n"
    "        }\n"
    "        if (weight < 0.5) { discard; }\n"
    "        if (texel == 0u) { texel = any; }\n"
    "        shown = sum / weight;\n"
    "    } else {\n"
    "        if (texel == 0u) { discard; }\n"
    "        shown = one_color;\n"
    "    }\n"
    "    if ((v_flags.w & 2u) != 0u) {\n"
    "        color = shown * 255.0 / 31.0;\n"
    "    } else {\n"
    /* a texel times the colour, 128 being one */
    "        color = shown * color / 16.0;\n"
    "    }\n"
    "    color = clamp(color, vec3(0.0), vec3(255.0));\n"
    "    o_color = vec4(color / 255.0, 1.0);\n"
    /* The depth is kept as it is and not as the card's own divide
     * leaves it: a ground seen almost edge on reaches to the far end of
     * the stage, and a depth that falls off as one over the distance has
     * nothing left out there to tell two faces apart by. */
    "    gl_FragDepth = clamp((v_depth + v_push) / u_depth, 0.0, 1.0);\n"
    "    o_slot = uint(v_slot);\n"
    "}\n";

/* ---- starting ---- */

static void port_GlStageSay(const char *what, const char *more) {
    u32 at = 0;
    u32 i;

    for (i = 0; what[i] != 0 && at + 1 < sizeof(g_stage.problem); i++) {
        g_stage.problem[at++] = what[i];
    }
    for (i = 0;
         more != NULL && more[i] != 0 && at + 1 < sizeof(g_stage.problem);
         i++) {
        g_stage.problem[at++] = more[i];
    }
    g_stage.problem[at] = 0;
}

static GLuint port_GlStageShader(GLenum kind, const char *text) {
    GLuint shader = glCreateShader(kind);
    GLint ok = GL_FALSE;

    glShaderSource(shader, 1, &text, NULL);
    glCompileShader(shader);
    glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        static char log[2048];

        glGetShaderInfoLog(shader, (GLsizei)sizeof(log), NULL, log);
        port_GlStageSay("a shader does not compile: ", log);
        return 0;
    }
    return shader;
}

static void port_GlStageFormat(void) {
    const StageVertex *one = NULL;

    glBindVertexArray(g_stage.array);
    glBindBuffer(GL_ARRAY_BUFFER, g_stage.buffer);
    glVertexAttribPointer(
        0, 3, GL_FLOAT, GL_FALSE, (GLsizei)sizeof(*one), &one->x);
    glVertexAttribPointer(
        1, 2, GL_FLOAT, GL_FALSE, (GLsizei)sizeof(*one), &one->u);
    glVertexAttribIPointer(
        2, 4, GL_UNSIGNED_BYTE, (GLsizei)sizeof(*one), one->color);
    glVertexAttribIPointer(3, 4, GL_SHORT, (GLsizei)sizeof(*one), one->page);
    glVertexAttribIPointer(
        4, 4, GL_UNSIGNED_BYTE, (GLsizei)sizeof(*one), one->info);
    glVertexAttribIPointer(
        5, 1, GL_UNSIGNED_SHORT, (GLsizei)sizeof(*one), &one->tile);
    glVertexAttribPointer(
        6, 3, GL_FLOAT, GL_FALSE, (GLsizei)sizeof(*one), one->corner[0]);
    glVertexAttribPointer(
        7, 3, GL_FLOAT, GL_FALSE, (GLsizei)sizeof(*one), one->corner[1]);
    glVertexAttribPointer(
        8, 3, GL_FLOAT, GL_FALSE, (GLsizei)sizeof(*one), one->corner[2]);
    glVertexAttribPointer(
        9, 3, GL_FLOAT, GL_FALSE, (GLsizei)sizeof(*one), one->corner[3]);
    glEnableVertexAttribArray(0);
    glEnableVertexAttribArray(1);
    glEnableVertexAttribArray(2);
    glEnableVertexAttribArray(3);
    glEnableVertexAttribArray(4);
    glEnableVertexAttribArray(5);
    glEnableVertexAttribArray(6);
    glEnableVertexAttribArray(7);
    glEnableVertexAttribArray(8);
    glEnableVertexAttribArray(9);
}

const char *port_GlStageStart(void *(*getproc)(const char *name)) {
    GLuint vertex;
    GLuint fragment;
    GLint ok = GL_FALSE;

    g_stage.problem[0] = 0;
#define GL(result, name, args)                                                 \
    name = (result(*) args)getproc(#name);                                     \
    if (name == NULL) {                                                        \
        port_GlStageSay("OpenGL has no ", #name);                              \
        return g_stage.problem;                                                \
    }
    PORT_GL_STAGE_FUNCTIONS
#undef GL
    vertex = port_GlStageShader(GL_VERTEX_SHADER, g_stage_vertex_text);
    fragment = port_GlStageShader(GL_FRAGMENT_SHADER, g_stage_fragment_text);
    if (vertex == 0 || fragment == 0) {
        return g_stage.problem;
    }
    g_stage.program = glCreateProgram();
    glAttachShader(g_stage.program, vertex);
    glAttachShader(g_stage.program, fragment);
    glLinkProgram(g_stage.program);
    glGetProgramiv(g_stage.program, GL_LINK_STATUS, &ok);
    if (!ok) {
        static char log[2048];

        glGetProgramInfoLog(g_stage.program, (GLsizei)sizeof(log), NULL, log);
        port_GlStageSay("the shaders do not link: ", log);
        return g_stage.problem;
    }
#define AT(member, name)                                                       \
    g_stage.member = glGetUniformLocation(g_stage.program, name)
    AT(at_view, "u_view");
    AT(at_camera, "u_camera");
    AT(at_h, "u_h");
    AT(at_screen, "u_screen");
    AT(at_target, "u_target");
    AT(at_scale, "u_scale");
    AT(at_near, "u_near");
    AT(at_depth, "u_depth");
    AT(at_vram, "u_vram");
    AT(at_window, "u_window");
    AT(at_open, "u_open");
    AT(at_fog_levels, "u_fog_levels");
    AT(at_fog_slot, "u_fog_slot");
    AT(at_fog_clut, "u_fog_clut");
    AT(at_fog_rect, "u_fog_rect");
    AT(at_depth_shift, "u_depth_shift");
    AT(at_cutoff, "u_cutoff");
    AT(at_unit, "u_unit");
#undef AT
    glGenBuffers(1, &g_stage.buffer);
    glGenVertexArrays(1, &g_stage.array);
    port_GlStageFormat();
    g_stage.said_stage = -1;
    g_stage.is_started = TRUE;
    return NULL;
}

void port_GlStageSetOn(s32 on) {
    g_stage.is_on = on != 0;
    g_stage.is_every = on > 1;
    /* the next stage drawn says which path it takes, the setting having
     * changed */
    g_stage.said_stage = -1;
}

/* The stages the mesh is measured good on, stage by stage: the prover's
 * own verdict, a number a stage (tools/port_stage_prove.py writes it
 * into `verdict.txt`; docs/PORT.md, "How it is proved"). Every stage the
 * game can load is measured, the 32 a fight can be set on and the six
 * the modes have of their own. A 0 here -- a stage whose ground the mesh
 * draws other than the game does -- draws the game's own tiles instead,
 * whatever the setting says, and the run's log says so. */
static const u8 g_stage_proved[STAGE_COUNT] = {
    1, 1, 1, 1, 1, 1, 1, 1, /* 0 to 7 */
    1, 1, 1, 1, 1, 1, 1, 1, /* 8 to 15 */
    1, 1, 1, 1, 1, 1, 1, 1, /* 16 to 23 */
    1, 1, 1, 1, 1, 1, 1, 1, /* 24 to 31 */
    1, 1, 1, 1, 1, 1,       /* the modes' and the practice's own */
};

/* Whether this stage's ground is the mesh's, with no word said about it:
 * the setting, a stage the mesh is measured good on, or every stage where
 * the setting says so. */
static s32 port_GlStageIsOnQuietly(s32 stage) {
    if (!g_stage.is_started || !g_stage.is_on) {
        return FALSE;
    }
    if (stage < 0 || stage >= STAGE_COUNT) {
        return FALSE;
    }
    return g_stage.is_every || g_stage_proved[stage];
}

/* Which path this stage's ground takes, for the run's log: said once a
 * stage, whenever the stage or the setting changes. */
static void port_GlStageTellPath(s32 stage) {
    const char *how;

    if (stage == g_stage.said_stage) {
        return;
    }
    g_stage.said_stage = stage;
    if (!g_stage.is_started || !g_stage.is_on) {
        how = "the game's own tile primitives: stage_mesh is off";
    } else if (stage < 0 || stage >= STAGE_COUNT) {
        how = "the game's own tile primitives: no stage is loaded";
    } else if (g_stage_proved[stage]) {
        how = "one static mesh of its own";
    } else if (g_stage.is_every) {
        how = "one static mesh of its own, the setting saying every stage";
    } else {
        how = "the game's own tile primitives: the mesh is not measured "
              "good on this stage";
    }
    if (stage < 0) {
        snprintf(g_stage.news, sizeof(g_stage.news),
                 "no stage: the ground is drawn from %s.", how);
    } else {
        snprintf(g_stage.news, sizeof(g_stage.news),
                 "stage %d: the ground is drawn from %s.", stage, how);
    }
    g_stage.has_news = TRUE;
}

const char *port_GlStageNews(void) {
    if (!g_stage.has_news) {
        return NULL;
    }
    g_stage.has_news = FALSE;
    return g_stage.news;
}

s32 port_GlStageIsOn(void) {
    s32 stage = port_RemasterStageNumber();

    port_GlStageTellPath(stage);
    return port_GlStageIsOnQuietly(stage);
}

s32 port_GlStageFaces(void) { return g_stage.faces; }

s32 port_GlStageVertices(void) { return g_stage.vertex_count; }

s32 port_GlStageTiles(void) { return g_stage.tile_count; }

s32 port_GlStageMissed(void) { return g_stage.missed; }

u32 port_GlStageDrawnVertices(void) { return g_stage.drawn_vertices; }

u32 port_GlStageDraws(void) { return g_stage.draws; }

s32 port_GlStageSaveFrame(void) { return g_stage.is_drawn; }

void port_GlStageLoadFrame(s32 state) { g_stage.is_drawn = state; }

void port_GlStageNewFrame(void) {
    g_stage.is_drawn = FALSE;
    g_stage.drawn_vertices = 0;
    g_stage.draws = 0;
}

/* ---- the colour a face is lit with ---- */

/* One normal lit by the tiles' light setup, exactly as the renderer
 * lights a tile's face (render_EmitTile: the light matrix, the light
 * colour matrix and the ambient colour into the coprocessor, then NCS).
 * The whole of the coprocessor is put back afterwards: this runs while
 * the game's frame is being drawn, and nothing the game computes may
 * change. */
static void port_GlStageLight(const s16 *normal, u8 *rgb) {
    const LightSetup *lights = (const LightSetup *)port_RemasterTileLights();
    PortGte saved = g_port_gte;
    u32 color;

    port_GteLoadControl(PORT_GTE_LIGHT, &lights->lmat, PORT_GTE_MATRIX_WORDS);
    port_GteLoadControl(
        PORT_GTE_LIGHT_COLOR, &lights->cmat, PORT_GTE_MATRIX_WORDS);
    /* the ambient colour, each byte times sixteen
     * (render_LoadBackColor) */
    port_GteWriteControl(PORT_GTE_RBK, (u32)lights->back[0] << 4);
    port_GteWriteControl(PORT_GTE_GBK, (u32)lights->back[1] << 4);
    port_GteWriteControl(PORT_GTE_BBK, (u32)lights->back[2] << 4);
    port_GteWriteData(
        PORT_GTE_VXY0, ((u32)normal[0] & 0xFFFF) | ((u32)normal[1] << 16));
    port_GteWriteData(PORT_GTE_VZ0, (u32)(s32)normal[2]);
    port_GteCommand(PORT_GTE_CMD_NCS);
    color = port_GteReadData(PORT_GTE_RGB2);
    g_port_gte = saved;
    rgb[0] = (u8)color;
    rgb[1] = (u8)(color >> 8);
    rgb[2] = (u8)(color >> 16);
}

/* ---- building the mesh ---- */

static StageVertex *port_GlStageRoom(s32 how_many) {
    if (g_stage.built + how_many > g_stage.room) {
        s32 room = g_stage.room == 0 ? 0x4000 : g_stage.room * 2;
        StageVertex *bigger;

        while (room < g_stage.built + how_many) {
            room *= 2;
        }
        bigger = realloc(g_stage.building, (size_t)room * sizeof(*bigger));
        if (bigger == NULL) {
            return NULL;
        }
        g_stage.building = bigger;
        g_stage.room = room;
    }
    g_stage.built += how_many;
    return &g_stage.building[g_stage.built - how_many];
}

/* What every vertex of one face shares. */
typedef struct {
    s32 tile;
    s32 tile_x;
    s32 tile_z;
    u8 color[4]; /* red, green, blue, the flags */
    s16 page[4];
    u8 info[4];
    /* the face's corners in the world, in the order the record has
     * them, the fourth the third's again for a triangle */
    GLfloat corner[4][3];
} StageFace;

/* The face's corners in the world, which every vertex of it carries. */
static void port_GlStageCorners(
    StageFace *face, const StageCorner *corner, s32 count) {
    s32 i;

    for (i = 0; i < 4; i++) {
        const StageCorner *one = &corner[i < count ? i : count - 1];

        face->corner[i][0] = (GLfloat)(face->tile_x + one->x);
        face->corner[i][1] = (GLfloat)one->y;
        face->corner[i][2] = (GLfloat)(face->tile_z + one->z);
    }
}

static void port_GlStagePutVertex(
    StageVertex *to, const StageFace *face, const StageCorner *corner) {
    s32 i;

    to->x = (GLfloat)(face->tile_x + corner->x);
    to->y = (GLfloat)corner->y;
    to->z = (GLfloat)(face->tile_z + corner->z);
    /* a texel's middle, as the renderer's own vertices have it
     * (port_GlPutVertex) */
    to->u = (GLfloat)(corner->uv & 0xFF) + 0.5f;
    to->v = (GLfloat)((corner->uv >> 8) & 0xFF) + 0.5f;
    for (i = 0; i < 4; i++) {
        to->color[i] = face->color[i];
        to->page[i] = face->page[i];
        to->info[i] = face->info[i];
    }
    to->tile = (u16)face->tile;
    to->pad = 0;
    for (i = 0; i < 4; i++) {
        s32 j;

        for (j = 0; j < 3; j++) {
            to->corner[i][j] = face->corner[i][j];
        }
    }
}

/* A triangle of the mesh, in the order the chip takes a polygon's
 * corners. */
static void port_GlStageTriangle(const StageFace *face, const StageCorner *a,
                                 const StageCorner *b, const StageCorner *c) {
    StageVertex *to = port_GlStageRoom(3);

    if (to == NULL) {
        return;
    }
    port_GlStagePutVertex(&to[0], face, a);
    port_GlStagePutVertex(&to[1], face, b);
    port_GlStagePutVertex(&to[2], face, c);
}

/* Whether a quad's four corners are in one plane. */
static s32 port_GlStageIsFlat(const StageCorner *corner) {
    s64 ax = corner[1].x - corner[0].x;
    s64 ay = corner[1].y - corner[0].y;
    s64 az = corner[1].z - corner[0].z;
    s64 bx = corner[2].x - corner[0].x;
    s64 by = corner[2].y - corner[0].y;
    s64 bz = corner[2].z - corner[0].z;
    s64 cx = corner[3].x - corner[0].x;
    s64 cy = corner[3].y - corner[0].y;
    s64 cz = corner[3].z - corner[0].z;

    return (ay * bz - az * by) * cx + (az * bx - ax * bz) * cy +
               (ax * by - ay * bx) * cz ==
           0;
}

static void port_GlStageQuad(StageFace *face, const StageCorner *corner) {
    /* the chip's two triangles: the first three corners and the last
     * three */
    port_GlStageTriangle(face, &corner[0], &corner[1], &corner[2]);
    port_GlStageTriangle(face, &corner[1], &corner[2], &corner[3]);
}

/* The middle of two corners, as the cutting makes it
 * (render_SplitMidpoint): the two bytes of the texture point are
 * averaged apart, and each is kept whole. */
static void port_GlStageMidpoint(
    const StageCorner *first, const StageCorner *second, StageCorner *middle) {
    u32 u = ((first->uv & 0xFF) + (second->uv & 0xFF)) >> 1;
    u32 v = ((first->uv & UV_V_MASK) + (second->uv & UV_V_MASK)) >> 1;

    middle->x = (s16)((first->x + second->x) >> 1);
    middle->y = (s16)((first->y + second->y) >> 1);
    middle->z = (s16)((first->z + second->z) >> 1);
    middle->uv = (u & 0xFF) | (v & UV_V_MASK);
}

/* A quad cut `cuts` times over, as the renderer cuts a tile near the
 * camera (render_SplitFT4): four smaller quads meeting at the middle of
 * the first and fourth corners. The shape of a quad whose corners are
 * not in one plane follows from how often it was cut, so the mesh is cut
 * as far as the game ever cuts it and nothing of the ground changes
 * shape as the camera moves. */
static void port_GlStageSplitQuad(
    StageFace *face, const StageCorner *corner, s32 cuts) {
    StageCorner next[4];

    if (cuts == 0) {
        port_GlStageQuad(face, corner);
        return;
    }
    next[0] = corner[0];
    port_GlStageMidpoint(&corner[0], &corner[1], &next[1]);
    port_GlStageMidpoint(&corner[0], &corner[2], &next[2]);
    port_GlStageMidpoint(&corner[0], &corner[3], &next[3]);
    port_GlStageSplitQuad(face, next, cuts - 1);
    next[0] = corner[1];
    port_GlStageMidpoint(&corner[1], &corner[3], &next[2]);
    port_GlStageSplitQuad(face, next, cuts - 1);
    next[0] = corner[3];
    port_GlStageMidpoint(&corner[2], &corner[3], &next[1]);
    port_GlStageSplitQuad(face, next, cuts - 1);
    next[0] = corner[2];
    port_GlStageMidpoint(&corner[0], &corner[2], &next[2]);
    port_GlStageSplitQuad(face, next, cuts - 1);
}

/* One record of a tile's mesh: a face, with its texture, the colour the
 * coprocessor lights it with, and its corners among the tile's
 * vertices. */
static void port_GlStageFace(s32 tile, const StageTile *at, const s16 *vertices,
                             const u8 *record, s32 quad) {
    StageFace face;
    StageCorner corner[4];
    u32 word = STAGE_WORD(record, RECORD_TPAGE);
    u32 tpage = word & 0xFFFF;
    u32 bits = (word >> 16) & 0xFF;
    u32 code = (word >> 24) & 0xFF;
    u32 clut = STAGE_HALF(record, RECORD_CLUT);
    s32 count = quad ? 4 : 3;
    s16 normal[3];
    s32 cut_field;
    s32 cuts;
    s32 i;

    if (quad) {
        normal[0] = STAGE_SHALF(record, RECORD_QUAD_NORMAL_XY);
        normal[1] = STAGE_SHALF(record, RECORD_QUAD_NORMAL_XY + 2);
        normal[2] = STAGE_SHALF(record, RECORD_QUAD_NORMAL_Z);
    } else {
        normal[0] = STAGE_SHALF(record, RECORD_TRI_NORMAL);
        normal[1] = STAGE_SHALF(record, RECORD_TRI_NORMAL + 2);
        normal[2] = STAGE_SHALF(record, RECORD_TRI_NORMAL + 4);
    }
    face.tile = tile;
    face.tile_x = at->x;
    face.tile_z = at->z;
    port_GlStageLight(normal, face.color);
    face.color[3] = (u8)(FLAG_TEXTURED | ((code & CODE_RAW) ? FLAG_RAW : 0) |
                         ((code & CODE_HALF) ? FLAG_HALF : 0));
    face.page[0] = (s16)((tpage & MODE_PAGE_X_MASK) * MODE_PAGE_X_STEP);
    face.page[1] = (s16)((tpage & MODE_PAGE_Y_BIT) ? MODE_PAGE_Y_STEP : 0);
    face.page[2] = (s16)((clut & CLUT_X_MASK) * CLUT_X_STEP);
    face.page[3] = (s16)((clut >> CLUT_Y_SHIFT) & (PORT_VRAM_HEIGHT - 1));
    face.info[0] = (u8)((tpage >> MODE_DEPTH_SHIFT) & MODE_DEPTH_MASK);
    face.info[1] = (u8)((tpage >> MODE_BLEND_SHIFT) & MODE_BLEND_MASK);
    face.info[2] = 0;
    face.info[3] = 0;

    for (i = 0; i < count; i++) {
        s32 index = quad ? STAGE_BYTE(record, RECORD_QUAD_CORNERS + i)
                         : STAGE_BYTE(record, RECORD_TRI_CORNERS + i);
        const s16 *vertex = &vertices[3 * index];

        corner[i].x = vertex[0];
        corner[i].y = vertex[1];
        corner[i].z = vertex[2];
        /* The texture points as the record has them. The renderer halves
         * them once or twice for a cell the camera is far from
         * (render_TileShrink), which is a coarser texture for a coarser
         * cut; with the renderer's own options and a cell cut as often
         * as this one is, it halves them not at all, so these are the
         * points the console itself uses on the ground nearest the
         * camera. */
        corner[i].uv = STAGE_HALF(record, RECORD_UV0 + 2 * i);
    }
    g_stage.faces++;
    /* The renderer makes its two tests of the face once, of the corners
     * the record names, and cuts afterwards: every piece a cut face
     * breaks into is drawn or left out with the whole (render_EmitTile).
     * So every vertex of every piece carries the record's own corners. */
    port_GlStageCorners(&face, corner, quad ? 4 : 3);
    if (!quad) {
        /* a triangle takes the same shape however often it is cut */
        port_GlStageTriangle(&face, &corner[0], &corner[1], &corner[2]);
        return;
    }
    cut_field = (s32)BITS_CUT_FIELD(bits);
    cuts = (cut_field != 0) ? TILE_CUTS_FINEST - (cut_field - 1) : 0;
    if (cuts < 0) {
        cuts = 0;
    }
    /* Cut as often as the renderer ever cuts it, which is what it does
     * for the two cells nearest the camera: the shape of a quad whose
     * corners are not in one plane follows from how often it was cut, so
     * cutting every face the same way is what stops the ground changing
     * shape as the camera moves. A quad whose corners are in one plane
     * takes the same shape however often it is cut, and is kept whole:
     * four corners instead of a hundred, and a texture the card runs
     * across the whole of it instead of a step at every piece's edge
     * where the cutting rounded a texture point. */
    if (cuts == 0 || port_GlStageIsFlat(corner)) {
        port_GlStageQuad(&face, corner);
        return;
    }
    port_GlStageSplitQuad(&face, corner, cuts);
}

/* One tile: its mesh is a count of packed vertices, the vertices, and
 * then groups of records, a group's type saying which routine the
 * renderer draws it with (render_DrawStageTiles). */
static void port_GlStageTile(s32 tile, const StageTile *at) {
    const u8 *mesh =
        (const u8 *)port_RemasterTileMesh(at->mesh_block, (s32)at->mesh);
    const s16 *vertices;
    const u8 *walk;
    s32 count;

    if (mesh == NULL) {
        return;
    }
    count = STAGE_HALF(mesh, 0);
    if (count == 0) {
        return;
    }
    vertices = (const s16 *)(mesh + sizeof(u32));
    walk = (const u8 *)vertices + ((count * PACKED_VERTEX_SIZE + 3) & ~3);
    for (;;) {
        s32 records = STAGE_HALF(walk, 0);
        s32 type = STAGE_HALF(walk, 2);
        s32 quad;
        s32 i;

        if (records == 0) {
            return;
        }
        walk += 2 * sizeof(u16);
        type >>= GROUP_TYPE_SHIFT;
        if (type != GROUP_TRIANGLES && type != GROUP_QUADS) {
            /* the renderer stops the game for one of these; there is no
             * telling how long the group is, so the tile is given up */
            g_stage.missed += records;
            return;
        }
        quad = type == GROUP_QUADS;
        for (i = 0; i < records; i++) {
            port_GlStageFace(tile, at, vertices, walk, quad);
            walk += RECORD_SIZE;
        }
    }
}

/* Which seam number each tile's faces are pushed back by (info[2]).
 * Every tile is a cell of 2,000 units, but its mesh is 2,040 across, so
 * it lies over its neighbours' by a strip 40 units wide on every side.
 * On the console the painter's order hid that; in one buffer with a
 * depth test the two tiles' faces in the strip are the same plane, and
 * which of them a pixel shows is left to the rounding of each one's
 * depth, which flips pixel by pixel as the camera moves. The shader
 * pushes each face back by a share of its farthest corner's depth, so
 * that the face the console sorted nearer wins the strip as it did
 * there; this number is for when the two are as far, a camera looking
 * straight along the seam. Every tile gets a small one, no two tiles
 * whose ground overlaps the same one (a greedy colouring of the tiles'
 * footprints), and the lower number wins. */
static void port_GlStageSeams(s32 count) {
    static s32 low_x[TILES_MAX];
    static s32 high_x[TILES_MAX];
    static s32 low_z[TILES_MAX];
    static s32 high_z[TILES_MAX];
    static u8 seam[TILES_MAX];
    s32 i;
    s32 j;

    for (i = 0; i < count; i++) {
        low_x[i] = low_z[i] = 0x7FFFFFFF;
        high_x[i] = high_z[i] = -0x7FFFFFFF;
    }
    for (i = 0; i < g_stage.built; i++) {
        const StageVertex *one = &g_stage.building[i];
        s32 x = (s32)one->x;
        s32 z = (s32)one->z;
        s32 tile = one->tile;

        if (x < low_x[tile]) {
            low_x[tile] = x;
        }
        if (x > high_x[tile]) {
            high_x[tile] = x;
        }
        if (z < low_z[tile]) {
            low_z[tile] = z;
        }
        if (z > high_z[tile]) {
            high_z[tile] = z;
        }
    }
    for (i = 0; i < count; i++) {
        u32 taken = 0;
        s32 number = 0;

        for (j = 0; j < i; j++) {
            if (low_x[i] < high_x[j] && low_x[j] < high_x[i] &&
                low_z[i] < high_z[j] && low_z[j] < high_z[i] && seam[j] < 32) {
                taken |= 1u << seam[j];
            }
        }
        while (number < 31 && (taken & (1u << number)) != 0) {
            number++;
        }
        seam[i] = (u8)number;
    }
    for (i = 0; i < g_stage.built; i++) {
        g_stage.building[i].info[2] = seam[g_stage.building[i].tile];
    }
}

/* The whole of the stage's ground into one vertex buffer. */
static void port_GlStageBuild(const PortStageView *view) {
    s32 count = 0;
    const StageTile *tiles = (const StageTile *)port_RemasterStageTiles(&count);
    s32 i;

    g_stage.version = view->version;
    g_stage.tiles = 0;
    g_stage.tile_count = 0;
    g_stage.faces = 0;
    g_stage.missed = 0;
    g_stage.vertex_count = 0;
    g_stage.built = 0;
    if (tiles == NULL || count <= 0 || count > TILES_MAX) {
        return;
    }
    for (i = 0; i < count; i++) {
        port_GlStageTile(i, &tiles[i]);
    }
    port_GlStageSeams(count);
    g_stage.vertex_count = g_stage.built;
    g_stage.tiles = (PortAddress)tiles;
    g_stage.tile_count = count;
    glBindBuffer(GL_ARRAY_BUFFER, g_stage.buffer);
    glBufferData(GL_ARRAY_BUFFER,
                 (GLsizeiptr)((size_t)g_stage.built * sizeof(StageVertex)),
                 g_stage.building, GL_STATIC_DRAW);
    /* the card has it now */
    free(g_stage.building);
    g_stage.building = NULL;
    g_stage.room = 0;
    g_stage.built = 0;
}

/* ---- drawing it ---- */

/* The renderer's own framebuffer is what the mesh draws through: the
 * colour buffer the primitives are painted into, with the depth buffer
 * the renderer makes beside it (gl.h). The mesh is the only thing that
 * writes that depth, and the scene is held against it afterwards
 * (gl_draw.c), so the two have to be the one buffer. */
static s32 port_GlStageTarget(const PortGlPicture *picture) {
    if (picture->framebuffer == 0 && picture->color_texture == 0) {
        return FALSE;
    }
    glBindFramebuffer(GL_FRAMEBUFFER, picture->framebuffer);
    return TRUE;
}

/* The view the renderer drew the tiles with, as the card wants it: the
 * rotation of 1.12 numbers scaled down as the tiles' own projection
 * scales it, its second row halved (the console's frame is half as tall
 * as it is wide). */
static void port_GlStageView(const PortStageView *view, GLfloat *matrix) {
    s32 row;
    s32 column;

    for (row = 0; row < 3; row++) {
        GLfloat scale =
            (GLfloat)(1.0 / 4096.0 / (double)(1 << (view->far_shift & 31)));

        if (row == 1) {
            scale *= 0.5f;
        }
        for (column = 0; column < 3; column++) {
            /* a mat3's memory is a column after a column */
            matrix[column * 3 + row] =
                (GLfloat)view->matrix[row * 3 + column] * scale;
        }
    }
}

/* Where each level of the fog keeps its colours: the rectangle the
 * textures read, and then one rectangle after another downwards, going
 * on from the top of the next column at the bottom of video memory
 * (stage_SetFogMoves). */
static void port_GlStageFogRows(const PortStageView *view, GLint *clut) {
    s32 x = view->fog_x;
    s32 y = view->fog_y;
    s32 i;

    for (i = 0; i < PORT_STAGE_FOG_LEVELS + 1; i++) {
        y += view->fog_h;
        if (y >= PORT_VRAM_HEIGHT) {
            y = view->fog_y;
            x += view->fog_w;
        }
        clut[2 * i] = x;
        clut[2 * i + 1] = y;
    }
}

static void port_GlStageDrawMesh(
    const PortGlPicture *picture, const PortRemasterPrim *paint) {
    const PortStageView *view =
        g_stage.has_own_view ? &g_stage.own_view : port_GetRemasterStageView();
    const StageTile *tiles = (const StageTile *)g_stage.tiles;
    GLfloat matrix[9];
    GLint slot[32];
    GLint clut[2 * (PORT_STAGE_FOG_LEVELS + 1)];
    GLuint open[TILE_WORDS];
    s32 scale_x = picture->draw_x;
    s32 scale_y = picture->draw_y;
    s32 i;

    if (g_stage.vertex_count == 0 || !port_GlStageTarget(picture)) {
        return;
    }
    /* Which tiles the game is drawing opened up this frame, so that a
     * fighter behind one can be seen: their faces mix with what is
     * behind them and are the game's own primitives, not the
     * mesh's (render_EmitTile, TILE_BLEND). */
    for (i = 0; i < TILE_WORDS; i++) {
        open[i] = 0;
    }
    for (i = 0; i < g_stage.tile_count; i++) {
        if (tiles[i].draw_flags & STAGE_TILE_OPEN) {
            open[i >> 5] |= 1U << (i & 31);
        }
    }
    for (i = 0; i < 32; i++) {
        /* past the last level: never nearer than any depth, so that the
         * search below counts it as farther than everything */
        slot[i] = (i < view->fog_levels) ? view->fog_slot[i] : 0x7FFFFFFF;
    }
    port_GlStageFogRows(view, clut);
    port_GlStageView(view, matrix);

    glUseProgram(g_stage.program);
    glBindVertexArray(g_stage.array);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, picture->vram_texture);
    glUniform1i(g_stage.at_vram, 0);
    glUniformMatrix3fv(g_stage.at_view, 1, GL_FALSE, matrix);
    glUniform3f(g_stage.at_camera, (GLfloat)view->camera[0],
                (GLfloat)view->camera[1], (GLfloat)view->camera[2]);
    glUniform1f(g_stage.at_h, (GLfloat)view->h);
    /* where the middle of the screen is: the coprocessor's own offset,
     * the drawing offset of the frame's buffer, and a wider picture's
     * band at the left */
    glUniform2f(g_stage.at_screen,
                (GLfloat)view->ofx / (GLfloat)PORT_REMASTER_ONE +
                    (GLfloat)(paint->offset_x + picture->margin),
                (GLfloat)view->ofy / (GLfloat)PORT_REMASTER_ONE +
                    (GLfloat)paint->offset_y);
    glUniform2f(g_stage.at_target, (GLfloat)picture->target_w,
                (GLfloat)picture->target_h);
    glUniform2f(g_stage.at_scale, (GLfloat)scale_x, (GLfloat)scale_y);
    glUniform1f(g_stage.at_near, NEAR_PLANE * 2.0f);
    /* the mesh's depths are the renderer's own scale for the tiles, so
     * what they are divided by is scaled with them: the buffer holds a
     * depth in the world's units either way */
    glUniform1f(
        g_stage.at_depth, DEPTH_RANGE / (GLfloat)(1 << (view->far_shift & 31)));
    glUniform1i(g_stage.at_depth_shift, view->depth_shift & 31);
    glUniform1i(g_stage.at_cutoff, view->ot_cutoff);
    /* a tile's seam number is a quarter of a world unit (SEAM_STEP) */
    glUniform1f(
        g_stage.at_unit, SEAM_STEP / (GLfloat)(1 << (view->far_shift & 31)));
    glUniform1i(g_stage.at_fog_levels, view->fog_levels);
    glUniform1iv(g_stage.at_fog_slot, 32, slot);
    glUniform2iv(g_stage.at_fog_clut, PORT_STAGE_FOG_LEVELS + 1, clut);
    glUniform4i(g_stage.at_fog_rect, view->fog_x, view->fog_y, view->fog_w,
                view->fog_h);
    glUniform4ui(g_stage.at_window, (GLuint)paint->window_and_u,
                 (GLuint)paint->window_or_u, (GLuint)paint->window_and_v,
                 (GLuint)paint->window_or_v);
    glUniform1uiv(g_stage.at_open, TILE_WORDS, open);

    /* The whole depth buffer is cleared, and not only the part the mesh
     * draws in: the scene is held against it everywhere (gl_draw.c), so
     * what the mesh does not reach must be the far end and not what the
     * frame before left there. A clear obeys the scissor, hence the
     * order here. */
    glDisable(GL_SCISSOR_TEST);
    glClearDepth(1.0);
    glDepthMask(GL_TRUE);
    glClear(GL_DEPTH_BUFFER_BIT);
    port_GlGroundSlotsBegin();
    /* The scene of a wider picture is drawn across the whole of it, as
     * the renderer draws every primitive the projection gave a depth to
     * (port_GlDraw, is_wide). */
    glEnable(GL_SCISSOR_TEST);
    if (picture->margin > 0 && picture->display_w > 0) {
        glScissor(picture->display_x * scale_x, paint->clip_top * scale_y,
                  (picture->display_w + 2 * picture->margin) * scale_x,
                  (paint->clip_bottom - paint->clip_top + 1) * scale_y);
    } else {
        glScissor((paint->clip_left + picture->margin) * scale_x,
                  paint->clip_top * scale_y,
                  (paint->clip_right - paint->clip_left + 1) * scale_x,
                  (paint->clip_bottom - paint->clip_top + 1) * scale_y);
    }
    glViewport(0, 0, picture->target_w, picture->target_h);
    /* The ground is drawn from both sides and the depth buffer settles
     * which face a pixel belongs to. The renderer leaves out a face the
     * camera sees from behind (render_FaceFront), but it asks that of a
     * quad's first three corners and draws both of its triangles, which
     * wind opposite ways: the card's own test is per triangle, and
     * telling it to leave out the back of a face took half of a stage's
     * walls away with it. What is drawn that the console did not is the
     * underside of a face with nothing in front of it, which is more of
     * the stage to see and not less. */
    glDepthFunc(GL_LESS);
    glDepthMask(GL_TRUE);
    glEnable(GL_DEPTH_TEST);
    /* Nothing of the mesh mixes with what is behind it, so there is no
     * blending and one draw. */
    glDisable(GL_BLEND);
    glDrawArrays(GL_TRIANGLES, 0, g_stage.vertex_count);
    port_GlGroundSlotsEnd();
    g_stage.draws++;
    g_stage.drawn_vertices += (u32)g_stage.vertex_count;
    /* The scene is tested against this depth and writes none of it
     * (gl_draw.c), so the writing is turned off here and the test is
     * the renderer's own to turn on. */
    glDisable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);
    glViewport(0, 0, picture->target_w, picture->target_h);
    port_GlCountDraw(g_stage.drawn_vertices, g_stage.draws);
}

/* Whether this primitive is a drawing of one of the stage's tiles, the
 * mesh being on and built of this stage. */
static s32 port_GlStageIsTile(const PortRemasterPrim *prim, s32 may_build) {
    const PortStageView *view;

    if (!port_GlStageIsOn() || prim->id.object == 0) {
        return FALSE;
    }
    /* A tile the game is drawing opened up, so that a fighter behind it
     * can be seen, mixes with what is behind it and is linked into the
     * ordering table at its own depth, which is nearer than the
     * fighter's: the mesh is drawn where the farthest of the frame's
     * tiles was, so such a tile drawn with it would be painted over by
     * the very fighter it is meant to veil. It stays the game's own
     * primitive, at its own place in the painting, and the mesh leaves
     * its faces out. */
    if (prim->is_half) {
        return FALSE;
    }
    view = port_GetRemasterStageView();
    if (view->frames == 0) {
        return FALSE;
    }
    if (may_build && g_stage.version != view->version) {
        /* a stage that is not the one the mesh was built of */
        port_GlStageBuild(view);
    }
    if (g_stage.tile_count == 0) {
        return FALSE;
    }
    return prim->id.object >= g_stage.tiles &&
           prim->id.object <
               g_stage.tiles +
                   (PortAddress)((u32)g_stage.tile_count * sizeof(StageTile));
}

s32 port_GlStageWouldTake(const PortRemasterPrim *prim) {
    return port_GlStageIsTile(prim, FALSE);
}

void port_GlStageSetView(const PortStageView *view) {
    g_stage.has_own_view = view != NULL;
    if (view != NULL) {
        g_stage.own_view = *view;
    }
}

void port_GlStageGetView(PortStageView *view) {
    *view = *port_GetRemasterStageView();
}

void port_GlStageMixView(PortStageView *out, const PortStageView *one,
                         const PortStageView *two, s32 phase, s32 whole) {
    s32 i;

    *out = *one;
    if (whole <= 0 || one->far_shift != two->far_shift ||
        one->depth_shift != two->depth_shift) {
        /* the renderer changed the scale what is far off is kept at
         * between the two frames: the one on show is what is drawn */
        return;
    }
    for (i = 0; i < PORT_GTE_MATRIX_ELEMENTS_PORT; i++) {
        out->matrix[i] =
            (s16)(one->matrix[i] +
                  ((two->matrix[i] - one->matrix[i]) * phase) / whole);
    }
    for (i = 0; i < 3; i++) {
        out->camera[i] =
            one->camera[i] +
            (s32)(((s64)(two->camera[i] - one->camera[i]) * phase) / whole);
    }
    out->h = one->h + ((two->h - one->h) * phase) / whole;
    out->ofx = one->ofx + (s32)(((s64)(two->ofx - one->ofx) * phase) / whole);
    out->ofy = one->ofy + (s32)(((s64)(two->ofy - one->ofy) * phase) / whole);
}

s32 port_GlStageTakes(const PortRemasterPrim *prim) {
    PortGlPicture picture;

    if (!port_GlStageIsTile(prim, TRUE)) {
        return FALSE;
    }
    if (!g_stage.is_drawn) {
        /* The mesh takes the tiles' place in the order the game paints
         * in: it is drawn where the first of them would have been, which
         * is the farthest, and everything of the scene that stands on
         * the ground is painted over it afterwards.
         *
         * What was painted before it is drawn first, and while the mesh
         * is still not drawn: those primitives are farther than any of
         * the ground and have nothing to be tested against -- the depth
         * buffer still holds the frame before's (gl_draw.c). */
        port_GlFlushBatch();
        g_stage.is_drawn = TRUE;
        port_GlGetPicture(&picture);
        port_GlStageDrawMesh(&picture, prim);
        port_GlForgetState();
    }
    return TRUE;
}
