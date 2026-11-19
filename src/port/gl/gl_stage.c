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
#include <string.h>

#include "common.h"
#include "gl.h"
#include "gl_bench.h"
#include "gl_light.h"
#include "gl_light_list.h"
#include "gl_scene.h"
#include "gl_see.h"
#include "gl_stage.h"
#include "gl_sun.h"
#include "gl_lights.h"
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
#define GL_LEQUAL 0x0203
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
    GL(void, glColorMask, (GLboolean, GLboolean, GLboolean, GLboolean))        \
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
    GL(void, glUniform1fv, (GLint, GLsizei, const GLfloat *))                  \
    GL(void, glUniform4fv, (GLint, GLsizei, const GLfloat *))                  \
    GL(void, glUniform1uiv, (GLint, GLsizei, const GLuint *))                  \
    GL(void, glUniformMatrix3fv, (GLint, GLsizei, GLboolean, const GLfloat *)) \
    GL(void, glUniformMatrix4fv, (GLint, GLsizei, GLboolean, const GLfloat *)) \
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
/* a piece's face lit at each corner, the colour run across it */
#define FLAG_SHADED 8

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
/* A piece's vertices name it by its number after the most tiles there
 * can be (StageVertex.tile), and the shader by that (`a_tile - 512`). */
#define PIECE_BASE TILES_MAX
/* What a piece's face is (StageVertex.info[3]): a piece's at all; the
 * back of a face drawn from both sides, drawn only from behind; left
 * out nearer than the cutoff; and three corners, not four. */
#define PIECE_FACE 1
#define PIECE_BACK 2
#define PIECE_CUTOFF 4
#define PIECE_THREE 8

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
    /* The normal it is lit from with `smooth_light` (gl_light.h): the
     * mean of the faces' about this corner that do not turn by more than
     * a crease from its own, 4096 long; and nought. */
    s16 normal[4];
    /* the rectangle of texels the face reads, its texture points' least
     * and most: a mod's picture of the page is read inside it, so that
     * nothing of a texture packed beside it is smoothed in (gl_scene.h) */
    u8 box[4];
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
} StageVertex; /* 104 bytes */

static struct {
    s32 is_started;
    s32 is_on;
    s32 is_every; /* on for every stage, whatever g_stage_proved says */
    char problem[256];
    /* which stage the last line of news was about, and the line itself
     * (port_GlStageNews): -1 before there has been one */
    s32 said_stage;
    s32 has_news;
    char news[256];
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
    GLint at_smooth_light;
    GLint at_light;
    GLint at_color;
    GLint at_back;
    GLint at_scene_lookup;
    GLint at_scene_pages;
    GLint at_scene_bases;
    GLint at_scene_on;
    GLint at_scene_levels;
    GLint at_eye;
    GLint at_see_count;
    GLint at_see_axis;
    GLint at_see_radius;
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
    /* the stage's pieces in the mesh (gl_stage.h, `stage_props_mesh`):
     * the setting, and every stage whatever g_stage_props_proved says */
    s32 is_props_on;
    s32 is_props_every;
    s32 is_props_hidden; /* left out of the picture altogether: a mask */
    GLint at_pieces;
    GLint at_far;
    GLint at_occluding;
    GLint at_samples;
    GLint at_slots;
    s32 piece_first; /* the first of the mesh's vertices that are a piece's */
    /* the view and the paint the mesh was last drawn with, which the
     * pieces' marks in the shadows' mask are drawn with */
    s32 has_drawn;
    PortStageView drawn_view;
    PortRemasterPrim drawn_paint;
    PortAddress pieces; /* the first piece, which the rest follow */
    /* the last piece's node: an object past it is no piece, and is not
     * looked for among them (port_GlStagePieceOf, asked of every
     * primitive the backend is given, every picture) */
    PortAddress pieces_last;
    const PortStagePiece *piece_list; /* as they were taken down */
    s32 piece_count;
    /* which pieces the mesh holds: one whose mesh it could not read all
     * of is left to the game's own primitives, whole */
    u32 piece_in[PORT_STAGE_PIECE_WORDS];
    s32 piece_faces;
    s32 pieces_held;
    s32 pieces_missed;
    /* the sun's maps (gl_sun.h): what the mesh casts into them, and where
     * the mesh's own program reads them */
    GLuint sun_program;
    GLint sun_at_matrix;
    GLint sun_at_pieces;
    GLint sun_at_window;
    GLint sun_at_vram;
    PortGlSunAt sun;
    PortGlLightsAt lights;
} g_stage;

/* The view last mixed between two frames (port_GlStageMixView), kept
 * unrounded for the card. A view keeps the camera in whole units of the
 * world and its rotation in the console's 1.12 numbers, and a camera that
 * moves less than a unit a frame along one axis steps that axis all at
 * once at one phase while the others move smoothly: the mixed camera
 * leaves the line between the two frames' and what stands close to it --
 * the cave's railings -- jumps a pixel and back between two frames that
 * agree where it is (tools/port_fps_prove.py, the order of the pictures;
 * docs/PORT.md, "More pictures a second"). */
static struct {
    s16 matrix[PORT_GTE_MATRIX_ELEMENTS_PORT]; /* the rounded view it is of */
    s32 camera[3];
    GLfloat exact_matrix[PORT_GTE_MATRIX_ELEMENTS_PORT];
    GLfloat exact_camera[3];
    s32 is_set;
} g_stage_mixed;

/* Whether `view` is the view between two frames being drawn now, whose
 * unrounded rotation and camera are kept. */
static s32 port_GlStageIsMixed(const PortStageView *view) {
    return g_stage.has_own_view && g_stage_mixed.is_set &&
           memcmp(view->matrix, g_stage_mixed.matrix,
                  sizeof(g_stage_mixed.matrix)) == 0 &&
           memcmp(view->camera, g_stage_mixed.camera,
                  sizeof(g_stage_mixed.camera)) == 0;
}

/* The camera of a view, as the card is given it. */
static void port_GlStageEye(const PortStageView *view, GLint at) {
    if (port_GlStageIsMixed(view)) {
        glUniform3f(
            at, g_stage_mixed.exact_camera[0], g_stage_mixed.exact_camera[1],
            g_stage_mixed.exact_camera[2]);
    } else {
        glUniform3f(at, (GLfloat)view->camera[0], (GLfloat)view->camera[1],
                    (GLfloat)view->camera[2]);
    }
}

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
    "layout(location = 10) in uvec4 a_box;\n"
    "layout(location = 11) in vec3 a_normal;\n"
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
    /* which of the stage's pieces are drawn from the mesh this frame,
     * and what a depth in the tiles' scale is times to be the world's */
    "uniform uint u_pieces[2];\n"
    /* the tiles the game is drawing opened up (the fragment shader's
     * first test, before; now the face's own) */
    "uniform uint u_open[16];\n"
    "uniform float u_far;\n"
    "out vec2 v_texture;\n"
    "flat out float v_push;\n"
    "out float v_depth;\n"
    "flat out int v_cull;\n"
    /* a tile's face has one colour: there is nothing to interpolate */
    "flat out vec3 v_color;\n"
    /* ... and a piece's face lit at each corner has three or four, which
     * the chip runs across it (a Gouraud-shaded primitive) */
    "out vec3 v_shade;\n"
    "flat out uvec4 v_flags;\n"
    "flat out ivec4 v_page;\n"
    "flat out uvec4 v_info;\n"
    "flat out uint v_tile;\n"
    "flat out float v_slot;\n"
    "out vec3 v_smooth_normal;\n"
    "flat out vec4 v_box;\n"
    /* where the pixel is in the world, and which way its face looks, for
     * the walls seen through (gl_see.h) */
    "out vec3 v_place;\n"
    "flat out vec3 v_normal;\n"
    /* the ordering-table slot a depth is sorted at
     * (render_OtIndexShifted) */
    "int slot_scaled(int scaled) {\n"
    "    int octave = scaled >> 11;\n"
    "    int fraction = (scaled & 2047) - 4096;\n"
    "    return (fraction >> (octave & 31)) & 4095;\n"
    "}\n"
    "int slot_of(int depth) {\n"
    "    return slot_scaled(depth >> u_depth_shift);\n"
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
    /* A piece of the stage that is not ground: a node's mesh, whose
     * faces the renderer tests otherwise (render_EmitFT4 and the rest).
     * Its depth is the plain average of its corners', each held at 0 as
     * the coprocessor holds a depth behind the eye, and a face with none
     * is left out; its ordering-table slot is that depth's with the
     * node's scale undone (render_OtIndex: a node's depth goes in
     * shifted by three less its scale, so by three in all); only the
     * faces drawn through the packed table and the unlit ones are left
     * out nearer than the cutoff; and a face is drawn from its front
     * alone, or, for the other side of a face drawn from both, from its
     * back. */
    "    if ((a_info.w & 1u) != 0u) {\n"
    "        bool three = (a_info.w & 8u) != 0u;\n"
    "        float sort = three\n"
    "            ? (max(c0.z, 0.0) + max(c1.z, 0.0) + max(c2.z, 0.0)) / 3.0\n"
    "            : (max(c0.z, 0.0) + max(c1.z, 0.0) + max(c2.z, 0.0)\n"
    "               + max(c3.z, 0.0)) / 4.0;\n"
    "        int slot = slot_scaled(\n"
    "            int(clamp(sort * u_far, 0.0, 16777215.0)) >> 3);\n"
    "        uint piece = a_tile - 512u;\n"
    "        bool shown = piece < 64u\n"
    "            && (u_pieces[piece >> 5u] & (1u << (piece & 31u))) != 0u;\n"
    "        bool seen = ((a_info.w & 2u) != 0u) ? !front : front;\n"
    "        bool near = (a_info.w & 4u) != 0u && slot < u_cutoff;\n"
    "        v_cull = (!shown || !seen || near || sort <= 0.0) ? 1 : 0;\n"
    "        v_slot = float(slot);\n"
    "    }\n"
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
    /* A piece's face is pushed back by a 256th of the depth the
     * console sorted it at, as the ground's is by its farthest
     * corner's: where it lies over the ground or over another piece in
     * one plane, the face the console painted later wins, as it did
     * there. */
    "    if ((a_info.w & 1u) != 0u) {\n"
    "        float sum = c0.z + c1.z + c2.z;\n"
    "        float sort = ((a_info.w & 8u) != 0u) ? sum / 3.0\n"
    "                                             : (sum + c3.z) / 4.0;\n"
    "        v_push = max(sort, 0.0) / 256.0;\n"
    "    }\n"
    "    v_color = vec3(a_color.xyz);\n"
    "    v_shade = vec3(a_color.xyz);\n"
    "    v_flags = a_color;\n"
    "    v_page = a_page;\n"
    "    v_info = a_info;\n"
    "    v_tile = a_tile;\n"
    "    v_smooth_normal = a_normal;\n"
    /* to the far edges of the last texels */
    "    v_box = vec4(vec2(a_box.xy), vec2(a_box.zw) + vec2(1.0));\n"
    "    v_place = a_place;\n"
    "    v_normal = cross(a_corner1 - a_corner0, a_corner2 - a_corner0);\n"
    /* A face left out is left out here, before the card rasterises it,
     * and not pixel by pixel: everything the test asks is the face's own
     * (its corners, its tile, its flags, the same at each of its
     * vertices), so all three vertices of each of its triangles are put
     * at the same point outside the picture and the triangle is clipped
     * whole. A face that mixes with what is behind it is not the mesh's:
     * a tile the game is drawing opened up so that a fighter behind it
     * can be seen, or one whose own code says it mixes; the mesh is
     * drawn where the farthest tile of the frame was, which is not where
     * such a face belongs in the painting, so it is left to the game's
     * own primitive (port_GlStageIsTile). And a face whose whole is
     * nearer than the cutoff is the ground between the camera and the
     * fight, which the renderer leaves out too. */
    "    bool open = a_tile < 512u\n"
    "        && (u_open[a_tile >> 5u] & (1u << (a_tile & 31u))) != 0u;\n"
    "    if (open || (a_color.w & 4u) != 0u || v_cull != 0) {\n"
    "        gl_Position = vec4(2.0, 2.0, 2.0, 1.0);\n"
    "    }\n"
    "}\n";

static const char g_stage_fragment_text[] =
    "#version 430 core\n"
    /* the picture's samples a pixel (gl.h, PortGlPicture.samples) */
    "uniform int u_samples;\n" PORT_GL_VRAM_SHADER
    "uniform uvec4 u_window;\n" /* the texture window, and or or */
    "uniform int u_fog_levels;\n"
    "uniform int u_fog_slot[32];\n"
    "uniform ivec2 u_fog_clut[32];\n"
    "uniform ivec4 u_fog_rect;\n" /* the row the textures read */
    "uniform int u_depth_shift;\n"
    "uniform float u_depth;\n" /* what a depth is divided by */
    /* `smooth_light` (gl_light.h): the tiles' lights, as the fighters'
     * are given theirs (gl_fighter.c) */
    "uniform int u_smooth_light;\n"
    "uniform mat3 u_light;\n"
    "uniform mat3 u_color;\n"
    "uniform vec3 u_back;\n"
    "in vec3 v_smooth_normal;\n"
    "in vec2 v_texture;\n"
    "in float v_depth;\n"
    "flat in float v_push;\n"
    "flat in int v_cull;\n"
    "flat in vec3 v_color;\n"
    "in vec3 v_shade;\n"
    "flat in uvec4 v_flags;\n"
    "flat in ivec4 v_page;\n"
    "flat in uvec4 v_info;\n"
    "flat in uint v_tile;\n"
    "flat in float v_slot;\n"
    "flat in vec4 v_box;\n"
    "in vec3 v_place;\n"
    "flat in vec3 v_normal;\n"
    /* the walls seen through (gl_see.h): where the camera is in the
     * world, and each fighter's cylinder -- its axis's x, z, top and
     * bottom (the world's y runs down) and how far about it */
    "uniform vec3 u_eye;\n"
    "uniform int u_see_count;\n"
    "uniform vec4 u_see_axis[3];\n"
    "uniform float u_see_radius[3];\n"
    "layout(location = 0) out vec4 o_color;\n"
    /* a slot, a whole number below 65536, kept in floating point
     * (gl_draw.c, port_GlGroundSlotsEnd) */
    "layout(location = 1) out float o_slot;\n"
    /* the pieces' marks in the shadows' mask: a piece's pixel where the
     * picture kept it, which is where the ground's slots hold its own */
    "uniform int u_occluding;\n"
    "uniform usampler2D u_slots;\n"
    /* a mod's picture of a whole page (gl_scene.h) */
    PORT_GL_SCENE_SHADER
        /* the sun's maps (gl_sun.h) */
        PORT_GL_SUN_SHADER
    /* the lights of a frame (gl_lights.h) */
    PORT_GL_LIGHTS_SHADER
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
    /* How much of this pixel's face is taken away so that a fighter
     * behind it can be seen: the most of any fighter's. Each fighter is
     * an upright cylinder from his head down to his feet. The sight line
     * from the camera through the pixel counts where, seen from above,
     * it passes his axis within the cylinder's reach (softly towards its
     * edge), where at that point it is between his head and his feet --
     * a face the line meets below his feet is the ground under him, not
     * a wall in front of him -- and where that point is farther than the
     * pixel by more than a step. A face that lies flat and is seen from
     * above is the ground someone may stand on, and is never thinned. */
    "float see_through() {\n"
    "    if (u_see_count == 0) { return 0.0; }\n"
    "    vec3 normal = normalize(v_normal);\n"
    "    if (abs(normal.y) > 0.7 && u_eye.y < v_place.y) { return 0.0; }\n"
    "    vec3 sight = v_place - u_eye;\n"
    "    float reach = length(sight);\n"
    "    if (reach < 1.0) { return 0.0; }\n"
    "    sight /= reach;\n"
    "    float level = dot(sight.xz, sight.xz);\n"
    "    if (level < 1e-4) { return 0.0; }\n"
    "    float most = 0.0;\n"
    "    for (int i = 0; i < 3; i++) {\n"
    "        if (i >= u_see_count) { break; }\n"
    "        vec2 axis = u_see_axis[i].xy;\n"
    "        float t = dot(axis - u_eye.xz, sight.xz) / level;\n"
    "        float apart = length(u_eye.xz + t * sight.xz - axis);\n"
    "        float height = u_eye.y + t * sight.y;\n"
    "        float r = u_see_radius[i];\n"
    "        float inside = 1.0 - smoothstep(0.45 * r, r, apart);\n"
    /* the world's y runs down: the head is the smaller */
    "        float upright = smoothstep(u_see_axis[i].z - 240.0,\n"
    "                                   u_see_axis[i].z, height)\n"
    "                      * (1.0 - smoothstep(u_see_axis[i].w - 80.0,\n"
    "                                          u_see_axis[i].w + 20.0,\n"
    "                                          height));\n"
    "        float behind = smoothstep(80.0, 320.0, t - reach);\n"
    "        most = max(most, inside * upright * behind);\n"
    "    }\n"
    "    return most;\n"
    "}\n"
    /* an ordered dither, four by four pixels of the buffer: a face
     * thinned by `part` keeps that share of its pixels out */
    "float dither() {\n"
    "    ivec2 at = ivec2(gl_FragCoord.xy) & 3;\n"
    "    int order[16] = int[16](0, 8, 2, 10, 12, 4, 14, 6,\n"
    "                            3, 11, 1, 9, 15, 7, 13, 5);\n"
    "    return (float(order[at.y * 4 + at.x]) + 0.5) / 16.0;\n"
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
    /* the colour of index `index` of the face's table as this fragment
     * draws it, the fog's two levels mixed as above: what a mod's picture
     * of the page is made to follow (gl_scene.h) */
    "vec3 scene_now(uint index) {\n"
    "    vec3 now = channels(vram(g_clut.x + int(index), g_clut.y));\n"
    "    if (g_fog_mix >= 1.0) { return now; }\n"
    "    return mix(channels(vram(g_clut_near.x + int(index),\n"
    "                             g_clut_near.y)), now, g_fog_mix);\n"
    "}\n"
    "void main() {\n"
    /* Every sample the face covers, unless it is thinned below: a shader
     * that writes the mask anywhere must write it everywhere, or the
     * mask is undefined (and on a real card none of the face is drawn). */
    "    gl_SampleMask[0] = -1;\n"
    /* a face left out was left out by the vertices (above) */
    /* a wall between the camera and a fighter, thinned to at most
     * four parts in five */
    "    float thinned = 0.8 * see_through();\n"
    /* Multisampled, the share is how many of the pixel's samples are
     * kept, the dither choosing between one count and the next: a fade,
     * as the supersampled picture's averaging made of it. The shadows'
     * mask has one sample a pixel and keeps the dither. */
    "    if (u_samples > 1 && u_occluding == 0) {\n"
    "        if (thinned > 0.0) {\n"
    "            int kept = int(floor((1.0 - thinned) * float(u_samples)\n"
    "                                 + dither()));\n"
    "            if (kept <= 0) { discard; }\n"
    "            if (kept < u_samples) {\n"
    "                gl_SampleMask[0] = (1 << kept) - 1;\n"
    "            }\n"
    "        }\n"
    "    } else if (dither() < thinned) { discard; }\n"
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
    /* a piece's face lit at each corner: the colour run across it */
    "    vec3 color = ((v_flags.w & 8u) != 0u) ? v_shade : v_color;\n"
    /* the face's NCS again at this pixel, from the normal that runs
     * across it (render_EmitTile lights a face once, with the record's
     * own normal) */
    "    float length_of = length(v_smooth_normal);\n"
    "    if (u_smooth_light != 0 && length_of > 0.25) {\n"
    "        vec3 ir = max(u_light * (v_smooth_normal / length_of), vec3(0.0))\n"
    "                * 4096.0;\n"
    "        color = clamp(u_back + (u_color * ir) / 16.0, 0.0, 255.0);\n"
    "    }\n"
    /* `shadows map` (gl_sun.h): where the sun's map says the sun does not
     * reach, its own share of the light -- the first light of the setup,
     * its turn on the normal through its column of the colour matrix --
     * is taken away, and the rest of the stage's light is left */
    "    if (u_sun_on != 0 && length_of > 0.25) {\n"
    "        vec3 n = v_smooth_normal / length_of;\n"
    "        float seen = sun_visible(v_place, n);\n"
    "        if (seen < 1.0) {\n"
    "            vec3 to_sun = vec3(u_light[0][0], u_light[1][0], u_light[2][0]);\n"
    "            float turn = max(dot(to_sun, n), 0.0);\n"
    "            color = sun_shade(color, u_color[0] * turn * 256.0, seen);\n"
    "        }\n"
    "    }\n"
    /* the lights of a frame beside the stage's own (gl_lights.h), added to
     * its light as the coprocessor adds its lights */
    "    if (u_lights_count != 0 && length_of > 0.25) {\n"
    "        color += lights_add(v_place, v_smooth_normal / length_of);\n"
    "    }\n"
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
    /* a mod's picture of the page, mipmapped, read inside the face's
     * own texels, its colours following the fog's */
    "    int layer = scene_layer(v_page, v_info.x);\n"
    "    if (layer >= 0) {\n"
    "        vec4 got = scene_color(layer, v_page, v_info.x, v_texture,\n"
    "                               wide_across, wide_down, v_box);\n"
    "        if (got.a < 0.5) { discard; }\n"
    "        shown = got.rgb;\n"
    "    } else if (steps > 1) {\n"
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
    "    if (u_occluding != 0) {\n"
    "        uint kept = texelFetch(u_slots, ivec2(gl_FragCoord.xy), 0).r;\n"
    "        if ((v_info.w & 1u) == 0u || kept != uint(v_slot)) { discard; }\n"
    /* a face that lies level, within 30 degrees, is floor a fighter may
     * stand on, and the shadows fall on it as on the ground
     * (gl_shadow.c, port_GlShadowIsFloor) */
    "        vec3 level = normalize(v_normal);\n"
    "        if (abs(level.y) >= 0.866) { discard; }\n"
    "    }\n"
    /* The depth is kept as it is and not as the card's own divide
     * leaves it: a ground seen almost edge on reaches to the far end of
     * the stage, and a depth that falls off as one over the distance has
     * nothing left out there to tell two faces apart by. */
    "    gl_FragDepth = clamp((v_depth + v_push) / u_depth, 0.0, 1.0);\n"
    "    o_slot = v_slot;\n"
    "}\n";

/* The mesh as the sun sees it (gl_sun.h): into one of the sun's depth
 * maps, every face the picture could show -- the ground, and the pieces
 * the frame queued -- from both sides, a face that mixes with what is
 * behind it left out as the picture leaves it to the game. A texel of
 * nothing casts nothing, so a fence's cut-out texture casts its own
 * shape. */
static const char g_stage_sun_vertex_text[] =
    "#version 330 core\n"
    "layout(location = 0) in vec3 a_place;\n"
    "layout(location = 1) in vec2 a_texture;\n"
    "layout(location = 2) in uvec4 a_color;\n"
    "layout(location = 3) in ivec4 a_page;\n"
    "layout(location = 4) in uvec4 a_info;\n"
    "layout(location = 5) in uint a_tile;\n"
    "uniform mat4 u_sun;\n"
    "uniform uint u_pieces[2];\n"
    "out vec2 v_texture;\n"
    "flat out ivec4 v_page;\n"
    "flat out uvec4 v_info;\n"
    "flat out uint v_flags;\n"
    "void main() {\n"
    "    bool hidden = (a_color.w & 4u) != 0u;\n"
    "    if ((a_info.w & 1u) != 0u) {\n"
    "        uint piece = a_tile - 512u;\n"
    "        hidden = hidden || piece >= 64u\n"
    "            || (u_pieces[piece >> 5u] & (1u << (piece & 31u))) == 0u;\n"
    "    }\n"
    "    gl_Position = hidden ? vec4(2.0, 2.0, 2.0, 1.0)\n"
    "                         : u_sun * vec4(a_place, 1.0);\n"
    "    v_texture = a_texture;\n"
    "    v_page = a_page;\n"
    "    v_info = a_info;\n"
    "    v_flags = a_color.w;\n"
    "}\n";

static const char g_stage_sun_fragment_text[] =
    "#version 330 core\n" PORT_GL_VRAM_SHADER "uniform uvec4 u_window;\n"
    "in vec2 v_texture;\n"
    "flat in ivec4 v_page;\n"
    "flat in uvec4 v_info;\n"
    "flat in uint v_flags;\n"
    "void main() {\n"
    "    if ((v_flags & 1u) == 0u) { return; }\n"
    "    int u = (int(floor(v_texture.x)) & int(u_window.x)) | int(u_window.y);\n"
    "    int v = (int(floor(v_texture.y)) & int(u_window.z)) | int(u_window.w);\n"
    "    int y = v_page.y + v;\n"
    "    uint texel;\n"
    "    if (v_info.x == 0u) {\n"
    "        uint word = vram(v_page.x + (u >> 2), y);\n"
    "        uint index = (word >> ((uint(u) & 3u) * 4u)) & 15u;\n"
    "        texel = vram(v_page.z + int(index), v_page.w);\n"
    "    } else if (v_info.x == 1u) {\n"
    "        uint word = vram(v_page.x + (u >> 1), y);\n"
    "        uint index = (word >> ((uint(u) & 1u) * 8u)) & 255u;\n"
    "        texel = vram(v_page.z + int(index), v_page.w);\n"
    "    } else {\n"
    "        texel = vram(v_page.x + u, y);\n"
    "    }\n"
    "    if (texel == 0u) { discard; }\n"
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
    glVertexAttribIPointer(
        10, 4, GL_UNSIGNED_BYTE, (GLsizei)sizeof(*one), one->box);
    glVertexAttribPointer(
        11, 3, GL_SHORT, GL_FALSE, (GLsizei)sizeof(*one), one->normal);
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
    glEnableVertexAttribArray(10);
    /* the normal `smooth_light` and the sun's maps light from: without it
     * every vertex read nought, and neither ever reached the ground */
    glEnableVertexAttribArray(11);
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
    AT(at_smooth_light, "u_smooth_light");
    AT(at_light, "u_light");
    AT(at_color, "u_color");
    AT(at_back, "u_back");
    AT(at_scene_lookup, "u_scene_lookup");
    AT(at_scene_pages, "u_scene_pages");
    AT(at_scene_bases, "u_scene_bases");
    AT(at_scene_on, "u_scene_on");
    AT(at_scene_levels, "u_scene_levels");
    AT(at_eye, "u_eye");
    AT(at_see_count, "u_see_count");
    AT(at_see_axis, "u_see_axis");
    AT(at_see_radius, "u_see_radius");
    AT(at_pieces, "u_pieces");
    AT(at_far, "u_far");
    AT(at_occluding, "u_occluding");
    AT(at_samples, "u_samples");
    AT(at_slots, "u_slots");
#undef AT
    port_GlSunLocate(g_stage.program, &g_stage.sun);
    port_GlLightsLocate(g_stage.program, &g_stage.lights);
    vertex = port_GlStageShader(GL_VERTEX_SHADER, g_stage_sun_vertex_text);
    fragment =
        port_GlStageShader(GL_FRAGMENT_SHADER, g_stage_sun_fragment_text);
    if (vertex == 0 || fragment == 0) {
        return g_stage.problem;
    }
    g_stage.sun_program = glCreateProgram();
    glAttachShader(g_stage.sun_program, vertex);
    glAttachShader(g_stage.sun_program, fragment);
    glLinkProgram(g_stage.sun_program);
    glGetProgramiv(g_stage.sun_program, GL_LINK_STATUS, &ok);
    if (!ok) {
        static char log[2048];

        glGetProgramInfoLog(
            g_stage.sun_program, (GLsizei)sizeof(log), NULL, log);
        port_GlStageSay("the sun's shaders do not link: ", log);
        return g_stage.problem;
    }
    g_stage.sun_at_matrix = glGetUniformLocation(g_stage.sun_program, "u_sun");
    g_stage.sun_at_pieces =
        glGetUniformLocation(g_stage.sun_program, "u_pieces");
    g_stage.sun_at_window =
        glGetUniformLocation(g_stage.sun_program, "u_window");
    g_stage.sun_at_vram = glGetUniformLocation(g_stage.sun_program, "u_vram");
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

/* The stages the pieces are measured good on as meshes, stage by stage:
 * the prover's own verdict (tools/port_stage_prove.py --props; docs/PORT.md,
 * "The stage's pieces as meshes"). A 0 here draws that stage's pieces from
 * the game's own primitives whatever the setting says. */
static const u8 g_stage_props_proved[STAGE_COUNT] = {
    1, 1, 1, 1, 1, 1, 1, 1, /* 0 to 7 */
    1, 1, 1, 1, 1, 1, 1, 1, /* 8 to 15 */
    1, 1, 1, 1, 1, 1, 1, 1, /* 16 to 23 */
    1, 1, 1, 1, 1, 1, 1, 1, /* 24 to 31 */
    1, 1, 1, 1, 1, 1,       /* the modes' and the practice's own */
};

void port_GlStageSetPropsOn(s32 on) {
    g_stage.is_props_on = on != 0;
    g_stage.is_props_every = on > 1;
    g_stage.is_props_hidden = on == PORT_GL_PROPS_HIDE;
    g_stage.said_stage = -1;
}

/* Whether this stage's pieces are drawn from the mesh: the ground's mesh
 * first, then the setting and the measurements. */
static s32 port_GlStagePropsAreOn(s32 stage) {
    if (!g_stage.is_props_on || stage < 0 || stage >= STAGE_COUNT) {
        return FALSE;
    }
    return g_stage.is_props_every || g_stage_props_proved[stage];
}

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
    } else if (port_GlStageIsOnQuietly(stage)) {
        snprintf(g_stage.news, sizeof(g_stage.news),
                 "stage %d: the ground is drawn from %s; what stands on it "
                 "from %s.",
                 stage, how,
                 !g_stage.is_props_on
                     ? "the game's own primitives: stage_props_mesh is off"
                 : port_GlStagePropsAreOn(stage)
                     ? "the mesh as well"
                     : "the game's own primitives: the pieces are not "
                       "measured good on this stage");
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
static void port_GlStageLightBy(
    const LightSetup *lights, const void *matrix, const s16 *normal, u8 *rgb) {
    PortGte saved = g_port_gte;
    u32 color;

    port_GteLoadControl(PORT_GTE_LIGHT, matrix, PORT_GTE_MATRIX_WORDS);
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

static void port_GlStageLight(const s16 *normal, u8 *rgb) {
    const LightSetup *lights = (const LightSetup *)port_RemasterTileLights();

    port_GlStageLightBy(lights, &lights->lmat, normal, rgb);
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
    s16 normal[3]; /* the record's, which the face is lit from */
    u8 box[4];     /* its texture points' least and most */
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
    /* the face's own until the mesh is whole (port_GlStageBuild) */
    to->normal[0] = face->normal[0];
    to->normal[1] = face->normal[1];
    to->normal[2] = face->normal[2];
    to->normal[3] = 0;
    for (i = 0; i < 4; i++) {
        to->box[i] = face->box[i];
    }
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
    face.normal[0] = normal[0];
    face.normal[1] = normal[1];
    face.normal[2] = normal[2];
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
    /* the texels the whole face reads, which every piece of it is held
     * to (gl_scene.h) */
    face.box[0] = face.box[1] = 0xFF;
    face.box[2] = face.box[3] = 0;
    for (i = 0; i < count; i++) {
        u8 u = (u8)(corner[i].uv & 0xFF);
        u8 v = (u8)((corner[i].uv >> 8) & 0xFF);

        face.box[0] = u < face.box[0] ? u : face.box[0];
        face.box[1] = v < face.box[1] ? v : face.box[1];
        face.box[2] = u > face.box[2] ? u : face.box[2];
        face.box[3] = v > face.box[3] ? v : face.box[3];
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

/* ---- the stage's pieces ---- */

/* Everything of the stage that stands on the ground and is not a tile --
 * the railings, the fences, the walls, the posts, the roofs -- is a piece
 * (StageNode, stage.h): a node of its own with a mesh, a place and three
 * angles, which the game queues from the cells the camera sees and the
 * renderer projects again every frame (render_DrawQueue). Its faces are
 * built into the mesh beside the ground's, in the world, so that they are
 * drawn by the same camera: still where the camera is still, and in a
 * picture between two frames moved by the camera alone. A piece's mesh is
 * read as render_DrawQueue reads it -- parts, each its vertices and its
 * groups of records, a group's type naming the routine that draws it --
 * and each record as that routine reads it. */

/* A mesh's part: how many vertices (or threes of them), and above that
 * the form they are kept in (render_DrawPart). */
#define PART_COUNT_MASK 0xFFFF
#define PART_FORMAT_SHIFT 16
#define PART_PREBUILT 0 /* threes of SVECTORs */
#define PART_COMPACT 1  /* three halfwords each */
#define PART_PACKED 4   /* the same, drawn through the packed table */
#define SVECTOR_SIZE 8
/* Nothing a piece has comes near these; they bound a walk through memory
 * that is not a mesh. */
#define PIECE_PARTS_MAX 64
#define PIECE_POINTS_MAX 256

/* The routines of g_render_handlers and g_render_packed_handlers a
 * piece's group may name (render.c), and their records' sizes
 * (render_asm.c, EMIT_..._SIZE). */
#define TYPE_FT3 4
#define TYPE_UNLIT_FT3 5
#define TYPE_TWO_SIDED_FT3 6
#define TYPE_FT4 12
#define TYPE_UNLIT_FT4 13
#define TYPE_TWO_SIDED_FT4 14
#define TYPE_GT3 20
#define TYPE_TWO_SIDED_GT3 22
#define TYPE_GT4 28
#define TYPE_TWO_SIDED_GT4 30
#define SIZE_FT3 0x18
#define SIZE_UNLIT_FT3 0x10
#define SIZE_FT4 0x18
#define SIZE_UNLIT_FT4 0x14
#define SIZE_GT3 0x24
#define SIZE_GT4 0x2C
/* where a record keeps its corners and its normals */
#define AT_CORNERS3 0x0C
#define AT_CORNERS4 0x0E
#define AT_GT4_CORNERS 0x10
#define AT_FT3_NORMAL 0x10 /* x, y, z */
#define AT_FT4_NORMAL_Z 0x12
#define AT_FT4_NORMAL_XY 0x14
#define AT_GT3_NORMAL_XY0 0x10
#define AT_GT3_NORMAL_Z01 0x14 /* the first's z, then the second's */
#define AT_GT3_NORMAL_XY1 0x18
#define AT_GT3_NORMAL_XY2 0x1C
#define AT_GT3_NORMAL_Z2 0x20
#define AT_GT4_NORMAL_XY_FACE 0x14
#define AT_GT4_NORMAL_Z_FACE_0 0x18 /* the face's z, then the first's */
#define AT_GT4_NORMAL_XY0 0x1C
#define AT_GT4_NORMAL_XY1 0x20
#define AT_GT4_NORMAL_Z12 0x24
#define AT_GT4_NORMAL_XY2 0x28
/* the code byte of a record's first word, which the coprocessor's colour
 * carries into the primitive (its RGBC); and the unlit routines' own
 * (RENDER_UNLIT_FT3, RENDER_UNLIT_FT4: the texture as it is, opaque) */
#define RECORD_CODE_SHIFT 24
#define CODE_UNLIT 0x25
/* the colour of an unlit face, which the chip does not use */
#define UNLIT_GRAY 0x80

/* One face of a piece, as the routine that draws it would make it. */
typedef struct {
    s32 count; /* 3 or 4 */
    GLfloat place[4][3];
    u32 uv[4];
    u8 rgb[4][3];
    u32 tpage;
    u32 clut;
    u32 code;
    u8 shaded; /* a colour at each corner */
    u8 how;    /* PIECE_... */
    /* each corner's normal in the world, for `smooth_light` (gl_light.h),
     * the one it is lit from */
    s16 normal[4][3];
} PieceFace;

/* The piece being read: where its points are in the world, and how it
 * is lit. */
typedef struct {
    s32 index;
    const PortStagePiece *piece;
    GLfloat point[PIECE_POINTS_MAX][3];
    s32 points;
} PieceRead;

/* A normal of three halfwords, each turned bit for bit where the face is
 * seen from behind (RENDER_FLIP), as the renderer turns them. */
static void port_GlStagePieceNormal(s16 *normal, u32 xy, u32 z, u32 flip) {
    xy ^= flip;
    normal[0] = (s16)(xy & 0xFFFF);
    normal[1] = (s16)(xy >> 16);
    normal[2] = (s16)((z ^ flip) & 0xFFFF);
}

/* A normal of the piece's lit as the renderer lights it, and the same
 * normal into the world, which the tiles' lights are in, for
 * `smooth_light` (gl_light.h): turned by the piece's own matrix for a
 * rotated one, as the renderer turns the lights the other way
 * (render_LoadTurnedLights). */
static void port_GlStagePieceLight(
    const PieceRead *read, const s16 *normal, u8 *rgb, s16 *world) {
    const PortStagePiece *piece = read->piece;
    s32 row;

    port_GlStageLightBy(
        (const LightSetup *)piece->lights, piece->light_matrix, normal, rgb);
    for (row = 0; row < 3; row++) {
        if (piece->kind == NODE_KIND_ROTATED) {
            world[row] =
                (s16)(((s32)piece->rotation[row * 3] * normal[0] +
                       (s32)piece->rotation[row * 3 + 1] * normal[1] +
                       (s32)piece->rotation[row * 3 + 2] * normal[2]) >>
                      12);
        } else {
            world[row] = normal[row];
        }
    }
}

/* A piece's face into the mesh: the chip's one or two triangles. */
static void port_GlStagePieceFace(
    const PieceRead *read, const PieceFace *face) {
    static const s32 order[6] = {0, 1, 2, 1, 2, 3};
    StageVertex *to;
    s32 corners = face->count == 4 ? 6 : 3;
    u32 tpage = face->tpage;
    u32 clut = face->clut;
    u8 flags;
    u8 box[4];
    s32 i;
    s32 j;

    /* A face that mixes with what is behind it is left to the game's own
     * primitive, at its own place in the painting (port_GlStageIsTile:
     * is_half). */
    if (face->code & CODE_HALF) {
        return;
    }
    flags = (u8)(FLAG_TEXTURED | ((face->code & CODE_RAW) ? FLAG_RAW : 0) |
                 (face->shaded ? FLAG_SHADED : 0));
    box[0] = box[1] = 0xFF;
    box[2] = box[3] = 0;
    for (i = 0; i < face->count; i++) {
        u8 u = (u8)(face->uv[i] & 0xFF);
        u8 v = (u8)((face->uv[i] >> 8) & 0xFF);

        box[0] = u < box[0] ? u : box[0];
        box[1] = v < box[1] ? v : box[1];
        box[2] = u > box[2] ? u : box[2];
        box[3] = v > box[3] ? v : box[3];
    }
    to = port_GlStageRoom(corners);
    if (to == NULL) {
        return;
    }
    for (i = 0; i < corners; i++) {
        s32 k = order[i];
        StageVertex *one = &to[i];

        one->x = face->place[k][0];
        one->y = face->place[k][1];
        one->z = face->place[k][2];
        one->u = (GLfloat)(face->uv[k] & 0xFF) + 0.5f;
        one->v = (GLfloat)((face->uv[k] >> 8) & 0xFF) + 0.5f;
        one->color[0] = face->rgb[k][0];
        one->color[1] = face->rgb[k][1];
        one->color[2] = face->rgb[k][2];
        one->color[3] = flags;
        one->page[0] = (s16)((tpage & MODE_PAGE_X_MASK) * MODE_PAGE_X_STEP);
        one->page[1] = (s16)((tpage & MODE_PAGE_Y_BIT) ? MODE_PAGE_Y_STEP : 0);
        one->page[2] = (s16)((clut & CLUT_X_MASK) * CLUT_X_STEP);
        one->page[3] = (s16)((clut >> CLUT_Y_SHIFT) & (PORT_VRAM_HEIGHT - 1));
        one->info[0] = (u8)((tpage >> MODE_DEPTH_SHIFT) & MODE_DEPTH_MASK);
        one->info[1] = (u8)((tpage >> MODE_BLEND_SHIFT) & MODE_BLEND_MASK);
        one->info[2] = 0;
        one->info[3] =
            (u8)(PIECE_FACE | face->how | (face->count == 3 ? PIECE_THREE : 0));
        one->tile = (u16)(PIECE_BASE + read->index);
        one->pad = 0;
        one->normal[0] = face->normal[k][0];
        one->normal[1] = face->normal[k][1];
        one->normal[2] = face->normal[k][2];
        one->normal[3] = 0;
        for (j = 0; j < 4; j++) {
            one->box[j] = box[j];
        }
        for (j = 0; j < 4; j++) {
            const GLfloat *corner = face->place[j < face->count ? j : 2];

            one->corner[j][0] = corner[0];
            one->corner[j][1] = corner[1];
            one->corner[j][2] = corner[2];
        }
    }
    g_stage.piece_faces++;
}

/* The corners of a record, by the byte of each among the part's points;
 * false where one names a point the part has not. */
static s32 port_GlStagePieceCorners(
    const PieceRead *read, PieceFace *face, const u8 *corners, s32 count) {
    s32 i;

    face->count = count;
    for (i = 0; i < count; i++) {
        s32 index = corners[i];

        if (index >= read->points) {
            return FALSE;
        }
        face->place[i][0] = read->point[index][0];
        face->place[i][1] = read->point[index][1];
        face->place[i][2] = read->point[index][2];
    }
    return TRUE;
}

/* What every record begins with: the texture page, the colour table and
 * the texture points (EMIT_UV0_CLUT, EMIT_UV1_TPAGE). */
static void port_GlStagePieceTexture(
    PieceFace *face, const u8 *record, s32 count) {
    s32 i;

    face->tpage = STAGE_HALF(record, RECORD_TPAGE);
    face->clut = STAGE_HALF(record, RECORD_CLUT);
    for (i = 0; i < count; i++) {
        face->uv[i] = STAGE_HALF(record, RECORD_UV0 + 2 * i);
    }
    face->shaded = FALSE;
    /* none until a corner is lit: an unlit face keeps the game's own */
    for (i = 0; i < 4; i++) {
        face->normal[i][0] = face->normal[i][1] = face->normal[i][2] = 0;
    }
}

static void port_GlStagePieceFlat(PieceFace *face, const u8 *rgb) {
    s32 i;

    for (i = 0; i < 4; i++) {
        face->rgb[i][0] = rgb[0];
        face->rgb[i][1] = rgb[1];
        face->rgb[i][2] = rgb[2];
        face->normal[i][0] = face->normal[0][0];
        face->normal[i][1] = face->normal[0][1];
        face->normal[i][2] = face->normal[0][2];
    }
}

/* One record of a group, both sides of it where its routine draws both.
 * `group_code` is the code of the group's first record, which some
 * routines take for all of them. False where the type is not one a piece
 * is read for. */
static s32 port_GlStagePieceRecord(
    const PieceRead *read, s32 type, s32 is_packed, const u8 *record,
    u32 group_code) {
    PieceFace face;
    s16 normal[3];
    u8 rgb[3];
    u32 own_code = STAGE_WORD(record, RECORD_TPAGE) >> RECORD_CODE_SHIFT;
    s32 sides;
    s32 side;

    switch (type) {
    case TYPE_FT3:
    case TYPE_TWO_SIDED_FT3:
        sides = type == TYPE_TWO_SIDED_FT3 ? 2 : 1;
        for (side = 0; side < sides; side++) {
            u32 flip = side ? 0xFFFFFFFFU : 0;

            port_GlStagePieceTexture(&face, record, 3);
            if (!port_GlStagePieceCorners(
                    read, &face, record + AT_CORNERS3, 3)) {
                return FALSE;
            }
            port_GlStagePieceNormal(
                normal, STAGE_WORD(record, AT_FT3_NORMAL),
                STAGE_WORD(record, AT_FT3_NORMAL + 4), flip);
            port_GlStagePieceLight(read, normal, rgb, face.normal[0]);
            port_GlStagePieceFlat(&face, rgb);
            /* the packed routine takes the group's first colour and code
             * (render_EmitPackedFT3) and leaves out what is nearer than
             * the cutoff */
            face.code = (is_packed && type == TYPE_FT3) ? group_code : own_code;
            face.how =
                (u8)((side ? PIECE_BACK : 0) |
                     ((is_packed && type == TYPE_FT3) ? PIECE_CUTOFF : 0));
            port_GlStagePieceFace(read, &face);
        }
        return TRUE;

    case TYPE_FT4:
    case TYPE_TWO_SIDED_FT4:
        sides = type == TYPE_TWO_SIDED_FT4 ? 2 : 1;
        for (side = 0; side < sides; side++) {
            u32 flip = side ? 0xFFFFFFFFU : 0;

            port_GlStagePieceTexture(&face, record, 4);
            if (!port_GlStagePieceCorners(
                    read, &face, record + AT_CORNERS4, 4)) {
                return FALSE;
            }
            port_GlStagePieceNormal(
                normal, STAGE_WORD(record, AT_FT4_NORMAL_XY),
                STAGE_HALF(record, AT_FT4_NORMAL_Z), flip);
            port_GlStagePieceLight(read, normal, rgb, face.normal[0]);
            port_GlStagePieceFlat(&face, rgb);
            face.code = own_code;
            face.how =
                (u8)((side ? PIECE_BACK : 0) |
                     ((is_packed && type == TYPE_FT4) ? PIECE_CUTOFF : 0));
            port_GlStagePieceFace(read, &face);
        }
        return TRUE;

    case TYPE_UNLIT_FT3:
    case TYPE_UNLIT_FT4: {
        s32 count = type == TYPE_UNLIT_FT3 ? 3 : 4;

        port_GlStagePieceTexture(&face, record, count);
        if (!port_GlStagePieceCorners(
                read, &face, record + (count == 3 ? AT_CORNERS3 : AT_CORNERS4),
                count)) {
            return FALSE;
        }
        rgb[0] = rgb[1] = rgb[2] = UNLIT_GRAY;
        port_GlStagePieceFlat(&face, rgb);
        face.code = CODE_UNLIT;
        face.how = PIECE_CUTOFF;
        port_GlStagePieceFace(read, &face);
        return TRUE;
    }

    case TYPE_GT3:
    case TYPE_TWO_SIDED_GT3:
        sides = type == TYPE_TWO_SIDED_GT3 ? 2 : 1;
        for (side = 0; side < sides; side++) {
            u32 flip = side ? 0xFFFFFFFFU : 0;
            u32 z01 = STAGE_WORD(record, AT_GT3_NORMAL_Z01);

            port_GlStagePieceTexture(&face, record, 3);
            if (!port_GlStagePieceCorners(
                    read, &face, record + AT_CORNERS3, 3)) {
                return FALSE;
            }
            port_GlStagePieceNormal(
                normal, STAGE_WORD(record, AT_GT3_NORMAL_XY0), z01, flip);
            port_GlStagePieceLight(read, normal, face.rgb[0], face.normal[0]);
            port_GlStagePieceNormal(
                normal, STAGE_WORD(record, AT_GT3_NORMAL_XY1), z01 >> 16, flip);
            port_GlStagePieceLight(read, normal, face.rgb[1], face.normal[1]);
            port_GlStagePieceNormal(
                normal, STAGE_WORD(record, AT_GT3_NORMAL_XY2),
                STAGE_WORD(record, AT_GT3_NORMAL_Z2), flip);
            port_GlStagePieceLight(read, normal, face.rgb[2], face.normal[2]);
            face.shaded = TRUE;
            /* the colour and code are the group's first record's
             * (render_EmitGT3) */
            face.code = group_code;
            face.how = (u8)(side ? PIECE_BACK : 0);
            port_GlStagePieceFace(read, &face);
        }
        return TRUE;

    case TYPE_GT4:
    case TYPE_TWO_SIDED_GT4:
        sides = type == TYPE_TWO_SIDED_GT4 ? 2 : 1;
        for (side = 0; side < sides; side++) {
            u32 flip = side ? 0xFFFFFFFFU : 0;
            u32 z_face_0 = STAGE_WORD(record, AT_GT4_NORMAL_Z_FACE_0);
            u32 z12 = STAGE_WORD(record, AT_GT4_NORMAL_Z12);

            port_GlStagePieceTexture(&face, record, 4);
            if (!port_GlStagePieceCorners(
                    read, &face, record + AT_GT4_CORNERS, 4)) {
                return FALSE;
            }
            /* the first corner is lit by the face's own normal, the other
             * three by theirs; seen from behind, the renderer turns the
             * second and fourth corners' x and y not at all
             * (render_EmitBothSidesGT4), and neither does this */
            port_GlStagePieceNormal(
                normal, STAGE_WORD(record, AT_GT4_NORMAL_XY_FACE), z_face_0,
                flip);
            port_GlStagePieceLight(read, normal, face.rgb[0], face.normal[0]);
            port_GlStagePieceNormal(
                normal, STAGE_WORD(record, AT_GT4_NORMAL_XY0),
                (z_face_0 ^ flip) >> 16, 0);
            port_GlStagePieceLight(read, normal, face.rgb[1], face.normal[1]);
            port_GlStagePieceNormal(
                normal, STAGE_WORD(record, AT_GT4_NORMAL_XY1), z12, flip);
            port_GlStagePieceLight(read, normal, face.rgb[2], face.normal[2]);
            port_GlStagePieceNormal(
                normal, STAGE_WORD(record, AT_GT4_NORMAL_XY2),
                (z12 ^ flip) >> 16, 0);
            port_GlStagePieceLight(read, normal, face.rgb[3], face.normal[3]);
            face.shaded = TRUE;
            face.code = group_code;
            face.how = (u8)(side ? PIECE_BACK : 0);
            port_GlStagePieceFace(read, &face);
        }
        return TRUE;

    default:
        return FALSE;
    }
}

/* How long a record of a group of this type is, 0 for a type a piece is
 * not read for. */
static s32 port_GlStagePieceRecordSize(s32 type) {
    switch (type) {
    case TYPE_FT3:
    case TYPE_TWO_SIDED_FT3:
        return SIZE_FT3;
    case TYPE_UNLIT_FT3:
        return SIZE_UNLIT_FT3;
    case TYPE_FT4:
    case TYPE_TWO_SIDED_FT4:
        return SIZE_FT4;
    case TYPE_UNLIT_FT4:
        return SIZE_UNLIT_FT4;
    case TYPE_GT3:
    case TYPE_TWO_SIDED_GT3:
        return SIZE_GT3;
    case TYPE_GT4:
    case TYPE_TWO_SIDED_GT4:
        return SIZE_GT4;
    default:
        return 0;
    }
}

/* A part's points, in the world: the piece's matrix and place for a
 * rotated piece, its place alone for one the renderer draws unrotated
 * (render_SetupNode). */
static void port_GlStagePiecePoint(PieceRead *read, s32 at, const s16 *vertex) {
    const PortStagePiece *piece = read->piece;
    GLfloat x = (GLfloat)vertex[0];
    GLfloat y = (GLfloat)vertex[1];
    GLfloat z = (GLfloat)vertex[2];
    s32 row;

    for (row = 0; row < 3; row++) {
        if (piece->kind == NODE_KIND_ROTATED) {
            read->point[at][row] =
                ((GLfloat)piece->rotation[row * 3] * x +
                 (GLfloat)piece->rotation[row * 3 + 1] * y +
                 (GLfloat)piece->rotation[row * 3 + 2] * z) /
                    4096.0f +
                (GLfloat)piece->place[row];
        } else {
            read->point[at][row] = (row == 0 ? x : (row == 1 ? y : z)) +
                                   (GLfloat)piece->place[row];
        }
    }
}

/* One piece into the mesh. False, with nothing of it kept, where its mesh
 * holds anything this does not read as the renderer would. */
static s32 port_GlStagePiece(s32 index, const PortStagePiece *piece) {
    static PieceRead read;
    const u8 *walk = (const u8 *)piece->mesh;
    s32 first = g_stage.built;
    s32 faces = g_stage.piece_faces;
    s32 parts;

    if (walk == NULL || (piece->kind != NODE_KIND_ROTATED &&
                         piece->kind != NODE_KIND_UNROTATED)) {
        return FALSE;
    }
    read.index = index;
    read.piece = piece;
    for (parts = 0; parts < PIECE_PARTS_MAX; parts++) {
        u32 word = STAGE_WORD(walk, 0);
        s32 count = (s32)(word & PART_COUNT_MASK);
        s32 format = (s32)(word >> PART_FORMAT_SHIFT);
        s32 i;

        walk += sizeof(u32);
        if (count == 0) {
            return TRUE;
        }
        if (format == PART_PREBUILT) {
            read.points = count * 3;
            if (read.points > PIECE_POINTS_MAX) {
                break;
            }
            for (i = 0; i < read.points; i++) {
                port_GlStagePiecePoint(&read, i, (const s16 *)walk);
                walk += SVECTOR_SIZE;
            }
        } else if (format == PART_COMPACT || format == PART_PACKED) {
            read.points = count;
            if (read.points > PIECE_POINTS_MAX) {
                break;
            }
            for (i = 0; i < read.points; i++) {
                port_GlStagePiecePoint(&read, i, (const s16 *)walk);
                walk += PACKED_VERTEX_SIZE;
            }
            /* moved on two bytes if that leaves it on a whole word
             * (render_ProjectPackedVertices) */
            walk += (PortAddress)walk & (sizeof(u32) - 1);
        } else {
            break;
        }
        /* the groups (render_DrawGroups): a count, a type and the
         * records, until a count of none */
        for (;;) {
            s32 records = STAGE_HALF(walk, 0);
            s32 type;
            s32 size;
            u32 group_code;

            walk += sizeof(u16);
            if (records == 0) {
                break;
            }
            type = STAGE_HALF(walk, 0);
            walk += sizeof(u16);
            size = port_GlStagePieceRecordSize(type);
            if (size == 0) {
                goto missed;
            }
            group_code = STAGE_WORD(walk, RECORD_TPAGE) >> RECORD_CODE_SHIFT;
            for (i = 0; i < records; i++) {
                if (!port_GlStagePieceRecord(
                        &read, type, format == PART_PACKED, walk, group_code)) {
                    goto missed;
                }
                walk += size;
            }
        }
        /* another part follows where the halfword after is not none
         * (render_DrawQueue) */
        walk += sizeof(u16);
        if (STAGE_HALF(walk, -(s32)sizeof(u16)) == 0) {
            return TRUE;
        }
    }
missed:
    g_stage.built = first;
    g_stage.piece_faces = faces;
    return FALSE;
}

/* Every piece of the stage into the mesh, after the ground. */
static void port_GlStagePieces(void) {
    s32 count = 0;
    const PortStagePiece *pieces = port_RemasterStagePieces(&count);
    s32 i;

    g_stage.pieces = 0;
    g_stage.pieces_last = 0;
    g_stage.piece_count = 0;
    g_stage.piece_faces = 0;
    g_stage.pieces_held = 0;
    g_stage.pieces_missed = 0;
    for (i = 0; i < PORT_STAGE_PIECE_WORDS; i++) {
        g_stage.piece_in[i] = 0;
    }
    if (count <= 0) {
        return;
    }
    for (i = 0; i < count && i < PORT_STAGE_PIECES_MAX; i++) {
        if (port_GlStagePiece(i, &pieces[i])) {
            g_stage.piece_in[i >> 5] |= 1U << (i & 31);
            g_stage.pieces_held++;
        } else {
            g_stage.pieces_missed++;
        }
    }
    g_stage.pieces = pieces[0].node;
    g_stage.pieces_last = pieces[0].node;
    for (i = 1; i < count; i++) {
        if (pieces[i].node > g_stage.pieces_last) {
            g_stage.pieces_last = pieces[i].node;
        }
    }
    g_stage.piece_list = pieces;
    g_stage.piece_count = count;
}

/* Which piece of the stage this object is, or -1. */
static s32 port_GlStagePieceOf(PortAddress object) {
    s32 i;

    /* by the addresses the pieces were taken down at, and not by the
     * size of a StageNode: this file's build lays the game's structures
     * out with pointers of its own size, the game's with PTR32 slots */
    if (g_stage.piece_count == 0 || object < g_stage.pieces ||
        object > g_stage.pieces_last) {
        return -1;
    }
    for (i = 0; i < g_stage.piece_count; i++) {
        if (g_stage.piece_list[i].node == object) {
            return i;
        }
    }
    return -1;
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
    /* each corner's normal the mean of the faces' about it, for
     * `smooth_light` (gl_light.h); a stage the memory cannot be had for
     * keeps the faces' own, which light it as the game does */
    if (g_stage.built > 0) {
        port_GlLightSmoothNormals(
            &g_stage.building[0].x,
            (s32)(sizeof(StageVertex) / sizeof(GLfloat)),
            g_stage.building[0].normal,
            (s32)(sizeof(StageVertex) / sizeof(s16)), g_stage.built);
    }
    g_stage.piece_first = g_stage.built;
    /* and what stands on it: built whatever the setting says, which
     * decides frame by frame whether they are drawn from it */
    port_GlStagePieces();
    /* for tools/port_lights.py, with --dump-textures alone */
    port_GlLightListDumpMesh(g_stage.building, (s32)sizeof(StageVertex),
                             g_stage.built, g_stage.piece_first);
    if (g_stage.piece_count > 0) {
        /* what the mesh made of the pieces, for the run's log */
        snprintf(g_stage.news, sizeof(g_stage.news),
                 "the stage's mesh holds %d of its %d pieces, %d faces of "
                 "them; %d are left to the game's own primitives.",
                 g_stage.pieces_held, g_stage.piece_count, g_stage.piece_faces,
                 g_stage.pieces_missed);
        g_stage.has_news = TRUE;
    }
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

/* The tiles' lights for `smooth_light`, as the coprocessor has them
 * (port_GlStageLight): the light matrix and the colour matrix made 1,
 * and the colour of the dark in bytes. */
static void port_GlStageLights(void) {
    const LightSetup *lights = (const LightSetup *)port_RemasterTileLights();
    GLfloat light[9];
    GLfloat color[9];
    s32 i;

    glUniform1i(g_stage.at_smooth_light,
                port_GlLightIsSmooth() && lights != NULL ? 1 : 0);
    if (lights == NULL) {
        return;
    }
    for (i = 0; i < 9; i++) {
        light[i] = (GLfloat)lights->lmat.m[i / 3][i % 3] / 4096.0f;
        color[i] = (GLfloat)lights->cmat.m[i / 3][i % 3] / 4096.0f;
    }
    /* the numbers are a row after a row */
    glUniformMatrix3fv(g_stage.at_light, 1, GL_TRUE, light);
    glUniformMatrix3fv(g_stage.at_color, 1, GL_TRUE, color);
    glUniform3f(g_stage.at_back, (GLfloat)lights->back[0],
                (GLfloat)lights->back[1], (GLfloat)lights->back[2]);
}

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
    s32 is_mixed = port_GlStageIsMixed(view);
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
                (is_mixed ? g_stage_mixed.exact_matrix[row * 3 + column]
                          : (GLfloat)view->matrix[row * 3 + column]) *
                scale;
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

/* The fighters the walls are thinned for (gl_see.h), as the shader has
 * them. */
static void port_GlStageSeeThrough(const PortStageView *view) {
    PortSeeFighter fighters[PORT_SEE_FIGHTERS];
    GLfloat axis[4 * PORT_SEE_FIGHTERS];
    GLfloat radius[PORT_SEE_FIGHTERS];
    s32 count = port_GlSeeFighters(fighters);
    s32 i;

    for (i = 0; i < PORT_SEE_FIGHTERS; i++) {
        axis[4 * i] = i < count ? fighters[i].x : 0.0f;
        axis[4 * i + 1] = i < count ? fighters[i].z : 0.0f;
        axis[4 * i + 2] = i < count ? fighters[i].top : 0.0f;
        axis[4 * i + 3] = i < count ? fighters[i].bottom : 0.0f;
        radius[i] = i < count ? fighters[i].radius : 0.0f;
    }
    port_GlStageEye(view, g_stage.at_eye);
    glUniform1i(g_stage.at_see_count, count);
    glUniform4fv(g_stage.at_see_axis, PORT_SEE_FIGHTERS, axis);
    glUniform1fv(g_stage.at_see_radius, PORT_SEE_FIGHTERS, radius);
}

/* The program and everything it is told, for a picture drawn with this
 * view: the mesh itself, and the pieces' marks in the shadows' mask. */
static void port_GlStageUniforms(
    const PortStageView *view, const PortGlPicture *picture,
    const PortRemasterPrim *paint) {
    GLfloat matrix[9];
    GLint slot[32];
    GLint clut[2 * (PORT_STAGE_FOG_LEVELS + 1)];
    GLuint open[TILE_WORDS];
    GLuint pieces[PORT_STAGE_PIECE_WORDS];
    s32 scale_x = picture->draw_x;
    s32 scale_y = picture->draw_y;
    s32 i;
    /* Which tiles the game drew opened up in the frame this view is of,
     * so that a fighter behind one can be seen: their faces mix with
     * what is behind them and are the game's own primitives, not the
     * mesh's (render_EmitTile, TILE_BLEND). They are the view's and not
     * the tiles' own flags, which by the time a picture between two
     * frames is drawn are already the next frame's: read there, a tile
     * the next frame opens was left out of the mesh while the frame on
     * show drew it whole, and no primitive of it was drawn either. */
    for (i = 0; i < TILE_WORDS; i++) {
        open[i] = (i < PORT_STAGE_TILE_WORDS) ? view->open[i] : 0;
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
    glBindTexture(PORT_GL_VRAM_TARGET, picture->vram_texture);
    glUniform1i(g_stage.at_vram, 0);
    glUniformMatrix3fv(g_stage.at_view, 1, GL_FALSE, matrix);
    port_GlStageEye(view, g_stage.at_camera);
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
    port_GlStageLights();
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
    /* which pieces the mesh draws: those the frame queued and left as
     * they were, that the mesh holds, on a stage they are drawn from it
     * on (port_GlStageIsTile leaves out their primitives by the same) */
    for (i = 0; i < PORT_STAGE_PIECE_WORDS; i++) {
        pieces[i] = port_GlStagePropsAreOn(port_RemasterStageNumber()) &&
                            !g_stage.is_props_hidden
                        ? view->pieces[i] & g_stage.piece_in[i]
                        : 0;
    }
    glUniform1uiv(g_stage.at_pieces, PORT_STAGE_PIECE_WORDS, pieces);
    glUniform1f(g_stage.at_far, (GLfloat)(1 << (view->far_shift & 31)));
    /* a mod's pictures of the stage's pages (gl_scene.h) */
    glUniform1i(g_stage.at_scene_lookup, PORT_GL_SCENE_LOOKUP_UNIT);
    glUniform1i(g_stage.at_scene_pages, PORT_GL_SCENE_PAGES_UNIT);
    glUniform1i(g_stage.at_scene_bases, PORT_GL_SCENE_BASES_UNIT);
    glUniform1i(g_stage.at_scene_on, port_GlSceneBind() ? 1 : 0);
    glUniform1f(g_stage.at_scene_levels, port_GlSceneLevels());
    port_GlStageSeeThrough(view);
    glUniform1i(g_stage.at_occluding, 0);
    glUniform1i(g_stage.at_samples, picture->samples);
    glUniform1i(g_stage.at_slots, 1);
}

static void port_GlStageDrawMesh(
    const PortGlPicture *picture, const PortRemasterPrim *paint) {
    const PortStageView *view =
        g_stage.has_own_view ? &g_stage.own_view : port_GetRemasterStageView();
    s32 scale_x = picture->draw_x;
    s32 scale_y = picture->draw_y;

    if (g_stage.vertex_count == 0 || !port_GlStageTarget(picture)) {
        return;
    }
    /* what the pieces' marks in the shadows' mask are drawn with, at the
     * picture's end (port_GlStageDrawOccluders), and the sun's maps */
    g_stage.drawn_view = *view;
    g_stage.drawn_paint = *paint;
    g_stage.has_drawn = TRUE;
    /* the sun's maps, drawn before the mesh reads them (gl_sun.h) */
    if (port_GlSunIsOn()) {
        port_GlSunPrepare(view);
        port_GlStageTarget(picture);
    }
    if (port_GlLightsIsOn()) {
        port_GlLightsPrepare();
        port_GlStageTarget(picture);
    }
    port_GlStageUniforms(view, picture, paint);
    port_GlSunBind(&g_stage.sun);
    port_GlLightsBind(&g_stage.lights);

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
    /* --bench-stress: the ground again, over itself, as a larger stage's
     * vertices and pixels (gl_bench.h); the picture is the same */
    if (port_GlBenchStageCopies() > 1) {
        s32 copy;

        glDepthFunc(GL_LEQUAL);
        for (copy = 1; copy < port_GlBenchStageCopies(); copy++) {
            glDrawArrays(GL_TRIANGLES, 0, g_stage.vertex_count);
        }
        glDepthFunc(GL_LESS);
    }
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

void port_GlStageSunPrepare(void) {
    s32 is_between = FALSE;
    const PortStageView *view = port_GlStageViewNow(&is_between);

    if (!port_GlStageIsOn() || !g_stage.has_drawn ||
        g_stage.vertex_count == 0 || view->frames == 0 ||
        g_stage.version != view->version) {
        return;
    }
    if (port_GlSunIsOn()) {
        port_GlSunPrepare(view);
    }
    port_GlLightsPrepare();
}

u32 port_GlStageDrawSunCasters(const float *matrix) {
    PortGlPicture picture;
    GLuint pieces[PORT_STAGE_PIECE_WORDS];
    const PortStageView *view;
    s32 i;

    if (!g_stage.is_started || g_stage.vertex_count == 0 ||
        !g_stage.has_drawn) {
        return 0;
    }
    port_GlGetPicture(&picture);
    view = g_stage.has_own_view ? &g_stage.own_view : &g_stage.drawn_view;
    for (i = 0; i < PORT_STAGE_PIECE_WORDS; i++) {
        pieces[i] = port_GlStagePropsAreOn(port_RemasterStageNumber()) &&
                            !g_stage.is_props_hidden
                        ? view->pieces[i] & g_stage.piece_in[i]
                        : 0;
    }
    glUseProgram(g_stage.sun_program);
    glBindVertexArray(g_stage.array);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(PORT_GL_VRAM_TARGET, picture.vram_texture);
    glUniform1i(g_stage.sun_at_vram, 0);
    glUniformMatrix4fv(g_stage.sun_at_matrix, 1, GL_FALSE, matrix);
    glUniform1uiv(g_stage.sun_at_pieces, PORT_STAGE_PIECE_WORDS, pieces);
    glUniform4ui(
        g_stage.sun_at_window, (GLuint)g_stage.drawn_paint.window_and_u,
        (GLuint)g_stage.drawn_paint.window_or_u,
        (GLuint)g_stage.drawn_paint.window_and_v,
        (GLuint)g_stage.drawn_paint.window_or_v);
    glDrawArrays(GL_TRIANGLES, 0, g_stage.vertex_count);
    port_GlCountDraw((u32)g_stage.vertex_count, 1);
    glBindVertexArray(0);
    return (u32)g_stage.vertex_count / 3;
}

void port_GlStageDrawOccluders(u32 framebuffer) {
    PortGlPicture picture;

    if (!g_stage.is_drawn || !g_stage.has_drawn ||
        g_stage.vertex_count <= g_stage.piece_first) {
        return;
    }
    port_GlGetPicture(&picture);
    if (picture.target_w <= 0) {
        return;
    }
    port_GlStageUniforms(&g_stage.drawn_view, &picture, &g_stage.drawn_paint);
    glUniform1i(g_stage.at_occluding, 1);
    glActiveTexture(GL_TEXTURE0 + 1);
    glBindTexture(GL_TEXTURE_2D, picture.slot_texture);
    glActiveTexture(GL_TEXTURE0);
    glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
    glViewport(0, 0, picture.target_w, picture.target_h);
    /* the alpha alone, as the game's own faces mark it (gl_draw.c,
     * port_GlDrawOccluders); the caller's scissor stands */
    glDisable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);
    glDisable(GL_BLEND);
    glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_TRUE);
    glDrawArrays(GL_TRIANGLES, g_stage.piece_first,
                 g_stage.vertex_count - g_stage.piece_first);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glUniform1i(g_stage.at_occluding, 0);
    port_GlCountDraw((u32)(g_stage.vertex_count - g_stage.piece_first), 1);
    glBindFramebuffer(GL_FRAMEBUFFER, picture.framebuffer);
    port_GlForgetState();
}

/* Whether this primitive is a drawing of one of the stage's tiles, the
 * mesh being on and built of this stage. */
static s32 port_GlStageIsTile(const PortRemasterPrim *prim, s32 may_build) {
    const PortStageView *view;
    s32 piece;

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
    if (prim->id.object >= g_stage.tiles &&
        prim->id.object <
            g_stage.tiles +
                (PortAddress)((u32)g_stage.tile_count * sizeof(StageTile))) {
        return TRUE;
    }
    /* A piece's, where the mesh holds the piece and draws it this frame:
     * the frame the mesh is drawn with, a picture's own between two frames
     * where there is one, says which pieces it queued and left as they
     * were taken down (PortStageView.pieces). */
    piece = port_GlStagePieceOf(prim->id.object);
    if (piece < 0 || !port_GlStagePropsAreOn(port_RemasterStageNumber())) {
        return FALSE;
    }
    if (g_stage.has_own_view) {
        view = &g_stage.own_view;
    }
    return (view->pieces[piece >> 5] & g_stage.piece_in[piece >> 5] &
            (1U << (piece & 31))) != 0;
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

const PortStageView *port_GlStageViewNow(s32 *is_between) {
    *is_between = g_stage.has_own_view;
    return g_stage.has_own_view ? &g_stage.own_view
                                : port_GetRemasterStageView();
}

void port_GlStageMixView(PortStageView *out, const PortStageView *one,
                         const PortStageView *two, s32 phase, s32 whole) {
    s32 i;

    *out = *one;
    g_stage_mixed.is_set = FALSE;
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
    /* and unrounded, for the card */
    for (i = 0; i < PORT_GTE_MATRIX_ELEMENTS_PORT; i++) {
        g_stage_mixed.matrix[i] = out->matrix[i];
        g_stage_mixed.exact_matrix[i] =
            (GLfloat)((double)one->matrix[i] +
                      (double)(two->matrix[i] - one->matrix[i]) *
                          (double)phase / (double)whole);
    }
    for (i = 0; i < 3; i++) {
        g_stage_mixed.camera[i] = out->camera[i];
        g_stage_mixed.exact_camera[i] =
            (GLfloat)((double)one->camera[i] +
                      (double)(two->camera[i] - one->camera[i]) *
                          (double)phase / (double)whole);
    }
    g_stage_mixed.is_set = TRUE;
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
        PORT_GPU_BEGIN(PORT_GPU_STAGE);
        port_GlStageDrawMesh(&picture, prim);
        PORT_GPU_END();
        port_GlForgetState();
    }
    return TRUE;
}
