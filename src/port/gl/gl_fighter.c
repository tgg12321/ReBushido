/* The fighters drawn from models of their own, on the game's bones
 * (gl_fighter.h; docs/PORT.md, "The fighters' own models").
 *
 * Each model is a glTF file skinned on the skeleton every model shares
 * (gl_fighter_file.h). Its vertices are in the game's own units and axes
 * at the bind pose, and a joint's matrix is the game's world matrix of
 * the bone times the joint's inverse bind matrix: so the card skins the
 * mesh on exactly the bones the game posed and placed, the camera is the
 * one the game projected its own fighter with, and the lights are the
 * fighter's own, lit as the coprocessor lights a normal (NCS: the light
 * matrix, the colour matrix and the colour of the dark). The texture is
 * read as the chip reads a texel, five bits a channel times the colour,
 * 128 being one.
 *
 * Nothing here is in the build without a window; nothing here changes a
 * value the game computes. */
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "common.h"
#include "gl.h"
#include "gl_cloth.h"
#include "gl_fighter.h"
#include "gl_fighter_file.h"
#include "gl_light.h"
#include "gl_stage.h"
#include "port_fighter.h"
#include "port_gpu.h"
#include "port_mods.h"
#include "port_remaster.h"

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
#define GL_UNSIGNED_INT 0x1405
#define GL_FLOAT 0x1406
#define GL_RGBA 0x1908
#define GL_RGBA8 0x8058
#define GL_RED 0x1903
#define GL_R32F 0x822E
#define GL_TEXTURE_2D 0x0DE1
#define GL_TEXTURE0 0x84C0
#define GL_TEXTURE_MIN_FILTER 0x2801
#define GL_TEXTURE_MAG_FILTER 0x2800
#define GL_TEXTURE_WRAP_S 0x2802
#define GL_TEXTURE_WRAP_T 0x2803
#define GL_NEAREST 0x2600
#define GL_LINEAR 0x2601
#define GL_LINEAR_MIPMAP_LINEAR 0x2703
#define GL_CLAMP_TO_EDGE 0x812F
#define GL_FRAMEBUFFER 0x8D40
#define GL_COLOR_ATTACHMENT0 0x8CE0
#define GL_COLOR_ATTACHMENT1 0x8CE1
#define GL_DEPTH_ATTACHMENT 0x8D00
#define GL_RENDERBUFFER 0x8D41
#define GL_DEPTH_COMPONENT24 0x81A6
#define GL_FRAMEBUFFER_COMPLETE 0x8CD5
#define GL_COLOR 0x1800
#define GL_DEPTH_BUFFER_BIT 0x00000100
#define GL_DEPTH_TEST 0x0B71
#define GL_LESS 0x0201
#define GL_ALWAYS 0x0207
#define GL_CULL_FACE 0x0B44
#define GL_BACK 0x0405
#define GL_CW 0x0900
#define GL_CCW 0x0901
#define GL_ARRAY_BUFFER 0x8892
#define GL_ELEMENT_ARRAY_BUFFER 0x8893
#define GL_STATIC_DRAW 0x88E4
#define GL_STREAM_DRAW 0x88E0
#define GL_VERTEX_SHADER 0x8B31
#define GL_FRAGMENT_SHADER 0x8B30
#define GL_COMPILE_STATUS 0x8B81
#define GL_LINK_STATUS 0x8B82
#define GL_BLEND 0x0BE2
#define GL_SCISSOR_TEST 0x0C11
#define GL_POLYGON_OFFSET_FILL 0x8037

#define PORT_GL_FIGHTER_FUNCTIONS                                              \
    GL(void, glViewport, (GLint, GLint, GLsizei, GLsizei))                     \
    GL(void, glScissor, (GLint, GLint, GLsizei, GLsizei))                      \
    GL(void, glEnable, (GLenum))                                               \
    GL(void, glDisable, (GLenum))                                              \
    GL(void, glCullFace, (GLenum))                                             \
    GL(void, glFrontFace, (GLenum))                                            \
    GL(void, glClear, (GLbitfield))                                            \
    GL(void, glClearDepth, (GLclampd))                                         \
    GL(void, glClearBufferfv, (GLenum, GLint, const GLfloat *))                \
    GL(void, glDepthFunc, (GLenum))                                            \
    GL(void, glDepthMask, (GLboolean))                                         \
    GL(void, glPolygonOffset, (GLfloat, GLfloat))                              \
    GL(void, glDrawBuffers, (GLsizei, const GLenum *))                         \
    GL(void, glColorMask, (GLboolean, GLboolean, GLboolean, GLboolean))        \
    GL(void, glGenerateMipmap, (GLenum))                                       \
    GL(void, glGenTextures, (GLsizei, GLuint *))                               \
    GL(void, glBindTexture, (GLenum, GLuint))                                  \
    GL(void, glActiveTexture, (GLenum))                                        \
    GL(void, glTexImage2D,                                                     \
       (GLenum, GLint, GLint, GLsizei, GLsizei, GLint, GLenum, GLenum,         \
        const void *))                                                         \
    GL(void, glTexParameteri, (GLenum, GLenum, GLint))                         \
    GL(void, glGenFramebuffers, (GLsizei, GLuint *))                           \
    GL(void, glBindFramebuffer, (GLenum, GLuint))                              \
    GL(void, glFramebufferTexture2D, (GLenum, GLenum, GLenum, GLuint, GLint))  \
    GL(void, glFramebufferRenderbuffer, (GLenum, GLenum, GLenum, GLuint))      \
    GL(GLenum, glCheckFramebufferStatus, (GLenum))                             \
    GL(void, glGenRenderbuffers, (GLsizei, GLuint *))                          \
    GL(void, glBindRenderbuffer, (GLenum, GLuint))                             \
    GL(void, glRenderbufferStorage, (GLenum, GLenum, GLsizei, GLsizei))        \
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
    GL(void, glUniform4f, (GLint, GLfloat, GLfloat, GLfloat, GLfloat))         \
    GL(void, glUniformMatrix3fv, (GLint, GLsizei, GLboolean, const GLfloat *)) \
    GL(void, glUniformMatrix4fv, (GLint, GLsizei, GLboolean, const GLfloat *)) \
    GL(void, glDrawArrays, (GLenum, GLint, GLsizei))                           \
    GL(void, glDrawElements, (GLenum, GLsizei, GLenum, const void *))

#define GL(result, name, args) static result(*name) args;
PORT_GL_FIGHTER_FUNCTIONS
#undef GL

/* ---- what is kept ---- */

/* The models a fight can have (Model.model_id, g_model_scales). */
#define MODELS_MAX 80
/* The model files' names by NDATA id (docs/formats/NDATA.md, DATA0). */
#define MODEL_FILES 37
/* An asset of the third set (Slash Mode's and the last opponents', ids 58
 * to 63) is read from NDATA id 27 below its own (ndata_ReadModel). */
#define MODEL_FIRST_REMAPPED 31
#define MODEL_REMAP 27
/* The weapon models (enum WeaponModel), whose files are `W00` to `W57`. */
#define WEAPON_FILES 58
/* GL's float texture, for what the game has done to a colour table */
#define GL_RGBA32F 0x8814
/* How much nearer the game sorts a cloth's face than it is
 * (render_SidedDepth), in the coprocessor's depth at the scale the
 * fighter is drawn at: so many times two to that scale in the world's. */
#define CLOTH_NEAR 80
/* How far outside the model's body its cloth is kept, in the world's
 * units (a fighter stands some 1,800 tall, the model's vertices some 26
 * apart). */
#define CLOTH_GAP 6.0
/* The planes the layer's depth is kept between, in the world's units. */
#define NEAR_PLANE 1.0f
#define FAR_PLANE 262144.0f

static const char * const g_model_files[MODEL_FILES] = {
    "M000", "M001", "M002", "M100", "M101", "M102", "M103", "M104",
    "M105", "M106", "M107", "M108", "M109", "M110", "M111", "M003",
    "M004", "M005", "M200", "M201", "M202", "M203", "M204", "M205",
    "M206", "M207", "M208", "M209", "M210", "M211", "M301", "M302",
    "M303", "M304", "M305", "M300", "M306",
};

enum { MODEL_UNTRIED, MODEL_LOADED, MODEL_MISSING };

/* A cloth of a model drawn as a smooth surface through the game's points
 * (gl_cloth.h): which of the model file's meshes it is, its surface, and
 * what the card draws it from, made again each picture. */
typedef struct {
    s32 mesh;
    PortGlCloth *surface;
    GLuint array;
    GLuint buffer;
    GLuint elements;
} FighterCloth;

typedef struct {
    s32 state;
    GLuint array;
    GLuint buffer;
    GLuint elements;
    s32 part_count;
    PortFighterPart part[PORT_FIGHTER_FILE_PARTS];
    GLuint texture[PORT_FIGHTER_FILE_PARTS];
    float inverse_bind[PORT_FIGHTER_FILE_JOINTS][16];
    s32 triangles;
    /* the vertices and the indices kept, for the shadows cast from them */
    s32 vertex_count;
    PortFighterVertex *vertices;
    u32 *indices;
    /* rb_palette: the picture of entries, and the tables as the file
     * has them (clut_count 0: none) */
    GLuint index_texture;
    s32 clut_count;
    PortFighterClut *clut;
    /* rb_cloth: the cloths drawn smooth, and the texture they are laid
     * from, the atlas of the body's first part */
    s32 cloth_count;
    FighterCloth cloth[PORT_FIGHTER_FILE_CLOTHS];
    GLuint cloth_texture;
} FighterModel;

/* Where the game has put a drawn thing's colour tables: the primitives
 * of a frame name the tables they read, and the model's file says where
 * its own were when it was made. The difference is the slot's (the second
 * fighter's tables are two pages to the right), the fog's or the weapons'
 * place, and it is worked out once from a frame's primitives and kept
 * while the same model is drawn there. */
#define SEEN_CLUTS 8

typedef struct {
    s32 model; /* the model the sightings and the shift are of, or -1 */
    s32 count;
    s32 x[SEEN_CLUTS];
    s32 y[SEEN_CLUTS];
    s32 is_known;
    s32 dx;
    s32 dy;
} ClutPlace;

/* What the models are drawn into before they are laid over the picture:
 * the fighters' layer, and the tiny model's, which the game draws after
 * the scene into a corner of the screen (model_tiny.c). */
enum { LAYER_FIGHTERS, LAYER_TINY, LAYERS };

typedef struct {
    GLuint framebuffer;
    GLuint color;
    GLuint depth_value;
    GLuint depth;
    s32 w;
    s32 h;
} FighterLayer;

/* The tiny models' files, `T00` to `T26` (g_model_tiny_ids). */
#define TINY_FILES 27

/* The things drawn into the layer whose tables are followed: the fighters'
 * slots, and the weapon models after them. */
#define PLACES (PORT_FIGHTER_SLOTS + WEAPON_FILES)

static struct {
    s32 is_started;
    s32 is_on;
    char folder[512];
    GLuint program;
    GLuint composite_program;
    GLuint quad_array;
    GLuint quad_buffer;
    FighterLayer layer[LAYERS];
    GLint at_joint;
    GLint at_world;
    GLint at_index;
    GLint at_palette;
    GLint at_paletted;
    GLint at_view;
    GLint at_h;
    GLint at_screen;
    GLint at_scale;
    GLint at_origin;
    GLint at_size;
    GLint at_light;
    GLint at_color;
    GLint at_back;
    GLint at_smooth_light;
    GLint at_texture;
    GLint at_cloth;
    GLint at_cloth_near;
    GLint c_at_layer;
    GLint c_at_layer_depth;
    GLint c_at_ground_slots;
    GLint c_at_ground_order;
    GLint c_at_origin;
    GLint c_at_rect;
    GLint c_at_target;
    GLint c_at_depth_range;
    /* the fighters of this frame are drawn: the rest of their
     * primitives are only left out */
    s32 is_drawn;
    /* the layer of this picture is laid, and its depth is what a cloth is
     * held against (port_GlFighterHolds); and what the shadows' mask
     * marks the fighters by at the picture's end
     * (port_GlFighterDrawOccluders). Not `is_drawn`, which a picture
     * between two frames puts back before it is finished: the mask of
     * every such picture had no fighter in it, and the shadow darkened
     * his feet, a flicker over them on a screen. */
    s32 is_laid;
    /* the primitive port_GlFighterTakes was last given is a cloth of a
     * fighter drawn from a model of his own */
    s32 holds;
    /* where the layer was laid over the picture */
    s32 laid[4];
    /* a picture between two frames is being drawn, `phase` of the way */
    s32 tween_phase;
    /* the views of the last two frames the fighters were drawn in: the
     * frame on show and the frame after it, for the pictures between */
    PortFighterView before[PORT_FIGHTER_SLOTS];
    PortFighterView latest[PORT_FIGHTER_SLOTS];
    /* ... and the weapons of the same two frames */
    PortFighterWeapon before_weapon[PORT_FIGHTER_WEAPONS];
    PortFighterWeapon latest_weapon[PORT_FIGHTER_WEAPONS];
    s32 before_weapons;
    s32 latest_weapons;
    u32 latest_serial;
    /* where each drawn thing's colour tables are, and a texture of what
     * the game has done to them */
    ClutPlace place[PLACES];
    GLuint palette_texture[PLACES];
    float palette[256 * PORT_FIGHTER_FILE_CLUTS * 4];
    s32 cull;
    /* the cloths drawn smooth where a file has them (RB_HD_CLOTH=0: the
     * game's own faces, as before) */
    s32 is_cloth_smooth;
    /* ... and kept out of the body (RB_HD_CLOTH_HOLD=0: as the game moved
     * them, for measuring) */
    s32 is_cloth_held;
    /* --gl-fighter-pose: the next frame's poses and primitives */
    char dump_folder[512];
    s32 dump_armed;
    FILE *dump;
    FighterModel model[MODELS_MAX];
    FighterModel weapon_model[WEAPON_FILES];
    FighterModel tiny_model[TINY_FILES];
    /* the tiny model of the frame on show and the frame after it, and
     * whether it is drawn in this picture */
    PortFighterView tiny_before[PORT_FIGHTER_SLOTS];
    PortFighterView tiny_latest[PORT_FIGHTER_SLOTS];
    s32 is_tiny_drawn;
    PortGlFighterCounts counts;
    char problem[512];
} g_fighter;

/* ---- the shaders ---- */

static const char g_fighter_vertex_text[] =
    "#version 330 core\n"
    "layout(location = 0) in vec3 a_place;\n"
    "layout(location = 1) in vec3 a_normal;\n"
    "layout(location = 2) in vec2 a_uv;\n"
    "layout(location = 3) in vec4 a_weight;\n"
    "layout(location = 4) in uvec4 a_joint;\n"
    /* the vertex as the game builds it: each bone's world matrix times a
     * vector and a share of its place (gl_fighter_file.h) */
    "layout(location = 5) in vec4 a_term[8];\n"
    "layout(location = 13) in uvec4 a_term_joint;\n"
    "layout(location = 14) in uvec4 a_term_joint1;\n"
    /* each bone's world matrix less the camera's place, times its
     * inverse bind matrix: for the normals */
    "uniform mat4 u_joint[21];\n"
    /* ... and without it, for the terms */
    "uniform mat4 u_world[21];\n"
    /* the view's rotation, 1.12 made 1, its second row halved (the
     * console's frame is half as tall as it is wide) */
    "uniform mat3 u_view;\n"
    "uniform float u_h;\n"
    "uniform vec2 u_screen;\n" /* the screen's middle, in video memory */
    "uniform vec2 u_scale;\n"  /* a pixel of video memory, of the buffer */
    "uniform vec2 u_origin;\n" /* the layer's corner in the buffer */
    "uniform vec2 u_size;\n"
    "uniform mat3 u_light;\n" /* the coprocessor's light matrix, made 1 */
    "uniform mat3 u_color;\n" /* ... and its colour matrix */
    "uniform vec3 u_back;\n"  /* the colour of the dark, 0 to 255 */
    /* a cloth (gl_cloth.h): its places are in the world already, less the
     * camera's, its normals the surface's own, and it is seen from both
     * sides, the side that looks away lit as the game lights such a face
     * of a cloth, by the normal turned round */
    "uniform int u_cloth;\n"
    "out vec2 v_uv;\n"
    "out vec3 v_color;\n"
    "out vec3 v_color_back;\n"
    /* the normal the colour was lit from, for `smooth_light`; nought
     * where there is none */
    "out vec3 v_normal;\n"
    "out float v_depth;\n"
    /* NCS: each light's turn on the normal, none below nought, through
     * the colour matrix onto the colour of the dark; a byte of colour is
     * that over sixteen, held to 255 */
    "vec3 lit(vec3 normal) {\n"
    "    float length_of = length(normal);\n"
    "    if (length_of < 0.5) {\n"
    "        return vec3(128.0);\n"
    "    }\n"
    "    vec3 ir = max(u_light * (normal / length_of), vec3(0.0)) * 4096.0;\n"
    "    return clamp(u_back + (u_color * ir) / 16.0, 0.0, 255.0);\n"
    "}\n"
    "void main() {\n"
    "    vec3 world;\n"
    "    vec3 normal;\n"
    "    if (u_cloth != 0) {\n"
    "        world = a_place;\n"
    "        normal = a_normal;\n"
    "    } else {\n"
    "        mat4 skin = a_weight.x * u_joint[a_joint.x]\n"
    "                  + a_weight.y * u_joint[a_joint.y]\n"
    "                  + a_weight.z * u_joint[a_joint.z]\n"
    "                  + a_weight.w * u_joint[a_joint.w];\n"
    "        world = (u_world[a_term_joint.x] * a_term[0]\n"
    "              + u_world[a_term_joint.y] * a_term[1]\n"
    "              + u_world[a_term_joint.z] * a_term[2]\n"
    "              + u_world[a_term_joint.w] * a_term[3]\n"
    "              + u_world[a_term_joint1.x] * a_term[4]\n"
    "              + u_world[a_term_joint1.y] * a_term[5]\n"
    "              + u_world[a_term_joint1.z] * a_term[6]\n"
    "              + u_world[a_term_joint1.w] * a_term[7]).xyz;\n"
    "        normal = mat3(skin) * a_normal;\n"
    "    }\n"
    "    vec3 v = u_view * world;\n"
    /* the projection the coprocessor makes, as the stage's mesh has it
     * (gl_stage.c), every component linear in the point */
    "    vec2 at = (u_h * v.xy + u_screen * v.z) * u_scale\n"
    "            + vec2(0.5) * v.z - u_origin * v.z;\n"
    "    vec2 ndc = at / u_size * 2.0 - vec2(v.z);\n"
    "    float n = "
    "1.0"
    ";\n"
    "    float f = "
    "262144.0"
    ";\n"
    "    gl_Position = vec4(ndc, (v.z * (f + n) - 2.0 * f * n) / (f - n),\n"
    "                       v.z);\n"
    "    v_color = lit(normal);\n"
    "    v_color_back = u_cloth != 0 ? lit(-normal) : v_color;\n"
    "    v_normal = length(normal) < 0.5 ? vec3(0.0) : normalize(normal);\n"
    "    v_uv = a_uv;\n"
    "    v_depth = v.z;\n"
    "}\n";

static const char g_fighter_fragment_text[] =
    "#version 330 core\n"
    "uniform sampler2D u_texture;\n"
    /* the entry of a colour table each texel is, and what the game has
     * done to the tables since the file was made (rb_palette) */
    "uniform sampler2D u_index;\n"
    "uniform sampler2D u_palette;\n"
    "uniform int u_paletted;\n"
    "uniform int u_cloth;\n"
    "uniform float u_cloth_near;\n"
    /* `smooth_light`: the same lights worked out at the pixel, from the
     * normal the corners' colours were lit from (docs/PORT.md,
     * "Lighting") */
    "uniform int u_smooth_light;\n"
    "uniform mat3 u_light;\n"
    "uniform mat3 u_color;\n"
    "uniform vec3 u_back;\n"
    "in vec2 v_uv;\n"
    "in vec3 v_color;\n"
    "in vec3 v_color_back;\n"
    "in vec3 v_normal;\n"
    "in float v_depth;\n"
    "layout(location = 0) out vec4 o_color;\n"
    "layout(location = 1) out float o_depth;\n"
    "void main() {\n"
    "    vec4 texel = texture(u_texture, v_uv);\n"
    "    if (u_paletted != 0) {\n"
    "        vec4 entry = texture(u_index, v_uv);\n"
    "        if (entry.a > 0.5) {\n"
    "            vec4 change = texelFetch(u_palette,\n"
    "                ivec2(int(entry.r * 255.0 + 0.5), int(entry.g * 255.0 + 0.5)), 0);\n"
    "            texel.rgb = clamp(texel.rgb + change.rgb, 0.0, 1.0);\n"
    /* an entry the game has made nothing is not drawn */
    "            if (change.a < 0.0) { discard; }\n"
    "        }\n"
    "    }\n"
    /* a texel of nothing is not drawn, as the chip leaves it */
    "    if (texel.a < 0.5) { discard; }\n"
    /* the chip's five bits a channel times the colour, 128 being one; a
     * cloth's face seen from behind is lit from behind (the front being
     * the side its normal is on, gl_cloth.h) */
    "    bool is_back = u_cloth != 0 && !gl_FrontFacing;\n"
    "    vec3 lit = is_back ? v_color_back : v_color;\n"
    "    vec3 normal = is_back ? -v_normal : v_normal;\n"
    "    float length_of = length(normal);\n"
    "    if (u_smooth_light != 0 && length_of > 0.25) {\n"
    "        vec3 ir = max(u_light * (normal / length_of), vec3(0.0))\n"
    "                * 4096.0;\n"
    "        lit = clamp(u_back + (u_color * ir) / 16.0, 0.0, 255.0);\n"
    "    }\n"
    "    vec3 c = texel.rgb * 31.0 * lit / 16.0;\n"
    "    o_color = vec4(clamp(c, 0.0, 255.0) / 255.0, 1.0);\n"
    /* the depth the layer is held to the scene by: a cloth's nearer by
     * what the game sorts its faces nearer by (render_SidedDepth), so
     * that a hem on the ground is not cut by the ground nor painted over
     * by the shadow under it, as the game's own is not */
    "    o_depth = v_depth - (u_cloth != 0 ? u_cloth_near : 0.0);\n"
    "}\n";

/* The layer laid over the picture. */
static const char g_composite_vertex_text[] =
    "#version 330 core\n"
    "layout(location = 0) in vec2 a_corner;\n"
    "uniform vec4 u_rect;\n" /* in the buffer's pixels */
    "uniform vec2 u_target;\n"
    "void main() {\n"
    "    vec2 at = u_rect.xy + a_corner * u_rect.zw;\n"
    "    gl_Position = vec4(at / u_target * 2.0 - vec2(1.0), 0.0, 1.0);\n"
    "}\n";

static const char g_composite_fragment_text[] =
    "#version 330 core\n"
    "uniform sampler2D u_layer;\n"
    "uniform sampler2D u_layer_depth;\n"
    "uniform usampler2D u_ground_slots;\n"
    "uniform int u_ground_order;\n"
    "uniform vec2 u_origin;\n"
    "uniform float u_depth_range;\n"
    "out vec4 o_color;\n"
    /* the ordering-table slot a depth is sorted at (render_OtIndexShifted):
     * the coprocessor has a fighter's depth scaled down by the scale he is
     * drawn at, and the renderer shifts it by three less that scale into
     * the table (render_DrawSkinned, render_DrawPart), so a depth in the
     * world's units goes in shifted by three whatever the scale */
    "int slot_of(int depth) {\n"
    "    int scaled = depth >> 3;\n"
    "    int octave = scaled >> 11;\n"
    "    int fraction = (scaled & 2047) - 4096;\n"
    "    return (fraction >> (octave & 31)) & 4095;\n"
    "}\n"
    "void main() {\n"
    "    ivec2 at = ivec2(gl_FragCoord.xy - u_origin);\n"
    "    vec4 c = texelFetch(u_layer, at, 0);\n"
    "    if (c.a < 0.5) { discard; }\n"
    "    float deep = texelFetch(u_layer_depth, at, 0).r;\n"
    /* held against the ground's mesh by the slots, as the game's own
     * primitives of a fighter are (gl_draw.c, port_GlIsOverGround) */
    "    if (u_ground_order != 0 &&\n"
    "        texelFetch(u_ground_slots, ivec2(gl_FragCoord.xy), 0).r\n"
    "            < uint(slot_of(int(clamp(deep, 0.0, 1048575.0))))) {\n"
    "        discard;\n"
    "    }\n"
    "    o_color = vec4(c.rgb, 1.0);\n"
    /* what the scene paints after this is held against the fighter */
    "    gl_FragDepth = clamp(deep / u_depth_range, 0.0, 1.0);\n"
    "}\n";

/* ---- starting ---- */

static void port_GlFighterSay(const char *what, const char *more) {
    snprintf(g_fighter.problem, sizeof(g_fighter.problem), "%s%s", what,
             more != NULL ? more : "");
}

static GLuint port_GlFighterShader(GLenum kind, const char *text) {
    GLuint shader = glCreateShader(kind);
    GLint ok = GL_FALSE;

    glShaderSource(shader, 1, &text, NULL);
    glCompileShader(shader);
    glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        static char log[2048];

        glGetShaderInfoLog(shader, (GLsizei)sizeof(log), NULL, log);
        port_GlFighterSay("a shader does not compile: ", log);
        return 0;
    }
    return shader;
}

static GLuint port_GlFighterProgram(
    const char *vertex_text, const char *fragment_text) {
    GLuint vertex = port_GlFighterShader(GL_VERTEX_SHADER, vertex_text);
    GLuint fragment = port_GlFighterShader(GL_FRAGMENT_SHADER, fragment_text);
    GLuint program;
    GLint ok = GL_FALSE;

    if (vertex == 0 || fragment == 0) {
        return 0;
    }
    program = glCreateProgram();
    glAttachShader(program, vertex);
    glAttachShader(program, fragment);
    glLinkProgram(program);
    glGetProgramiv(program, GL_LINK_STATUS, &ok);
    if (!ok) {
        static char log[2048];

        glGetProgramInfoLog(program, (GLsizei)sizeof(log), NULL, log);
        port_GlFighterSay("the shaders do not link: ", log);
        return 0;
    }
    return program;
}

const char *port_GlFighterStart(void *(*getproc)(const char *name)) {
    static const GLfloat corners[12] = {0, 0, 1, 0, 0, 1, 1, 0, 1, 1, 0, 1};
    const char *cull = getenv("RB_FIGHTER_CULL");
    const char *smooth_cloth = getenv("RB_HD_CLOTH");
    const char *held_cloth = getenv("RB_HD_CLOTH_HOLD");

    g_fighter.problem[0] = 0;
#define GL(result, name, args)                                                 \
    name = (result(*) args)getproc(#name);                                     \
    if (name == NULL) {                                                        \
        port_GlFighterSay("OpenGL has no ", #name);                            \
        return g_fighter.problem;                                              \
    }
    PORT_GL_FIGHTER_FUNCTIONS
#undef GL
    g_fighter.program =
        port_GlFighterProgram(g_fighter_vertex_text, g_fighter_fragment_text);
    g_fighter.composite_program = port_GlFighterProgram(
        g_composite_vertex_text, g_composite_fragment_text);
    if (g_fighter.program == 0 || g_fighter.composite_program == 0) {
        return g_fighter.problem;
    }
#define AT(member, name)                                                       \
    g_fighter.member = glGetUniformLocation(g_fighter.program, name)
    AT(at_joint, "u_joint");
    AT(at_world, "u_world");
    AT(at_index, "u_index");
    AT(at_palette, "u_palette");
    AT(at_paletted, "u_paletted");
    AT(at_view, "u_view");
    AT(at_h, "u_h");
    AT(at_screen, "u_screen");
    AT(at_scale, "u_scale");
    AT(at_origin, "u_origin");
    AT(at_size, "u_size");
    AT(at_light, "u_light");
    AT(at_color, "u_color");
    AT(at_back, "u_back");
    AT(at_smooth_light, "u_smooth_light");
    AT(at_texture, "u_texture");
    AT(at_cloth, "u_cloth");
    AT(at_cloth_near, "u_cloth_near");
#undef AT
#define AT(member, name)                                                       \
    g_fighter.member = glGetUniformLocation(g_fighter.composite_program, name)
    AT(c_at_layer, "u_layer");
    AT(c_at_layer_depth, "u_layer_depth");
    AT(c_at_ground_slots, "u_ground_slots");
    AT(c_at_ground_order, "u_ground_order");
    AT(c_at_origin, "u_origin");
    AT(c_at_rect, "u_rect");
    AT(c_at_target, "u_target");
    AT(c_at_depth_range, "u_depth_range");
#undef AT
    glGenVertexArrays(1, &g_fighter.quad_array);
    glGenBuffers(1, &g_fighter.quad_buffer);
    glBindVertexArray(g_fighter.quad_array);
    glBindBuffer(GL_ARRAY_BUFFER, g_fighter.quad_buffer);
    glBufferData(
        GL_ARRAY_BUFFER, (GLsizeiptr)sizeof(corners), corners, GL_STATIC_DRAW);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(GLfloat), NULL);
    glEnableVertexAttribArray(0);
    glBindVertexArray(0);
    {
        s32 i;

        for (i = 0; i < LAYERS; i++) {
            FighterLayer *layer = &g_fighter.layer[i];

            glGenFramebuffers(1, &layer->framebuffer);
            glGenTextures(1, &layer->color);
            glGenTextures(1, &layer->depth_value);
            glGenRenderbuffers(1, &layer->depth);
        }
    }
    glGenTextures(PLACES, g_fighter.palette_texture);
    {
        s32 i;

        for (i = 0; i < PLACES; i++) {
            g_fighter.place[i].model = -1;
        }
        /* the tiny model has the first of its views alone */
        for (i = 0; i < PORT_FIGHTER_SLOTS; i++) {
            g_fighter.tiny_before[i].model_id = -1;
            g_fighter.tiny_latest[i].model_id = -1;
        }
    }
    /* which way round a face is its front, as the console's (the
     * renderer's own test: render_FaceFront) */
    g_fighter.cull = cull != NULL ? atoi(cull) : 1;
    g_fighter.is_cloth_smooth = smooth_cloth == NULL || atoi(smooth_cloth) != 0;
    g_fighter.is_cloth_held = held_cloth == NULL || atoi(held_cloth) != 0;
    g_fighter.tween_phase = -1;
    g_fighter.is_started = TRUE;
    return NULL;
}

void port_GlFighterSetOn(s32 on) { g_fighter.is_on = on != 0; }

s32 port_GlFighterIsOn(void) { return g_fighter.is_on && g_fighter.is_started; }

void port_GlFighterSetFolder(const char *folder) {
    s32 i;

    if (folder == NULL) {
        /* the mods' own (port_ModFind) */
        folder = "";
    }
    snprintf(g_fighter.folder, sizeof(g_fighter.folder), "%s", folder);
    /* the folder changed: every model is looked for again */
    for (i = 0; i < MODELS_MAX; i++) {
        if (g_fighter.model[i].state == MODEL_MISSING) {
            g_fighter.model[i].state = MODEL_UNTRIED;
        }
    }
    for (i = 0; i < WEAPON_FILES; i++) {
        if (g_fighter.weapon_model[i].state == MODEL_MISSING) {
            g_fighter.weapon_model[i].state = MODEL_UNTRIED;
        }
    }
    for (i = 0; i < TINY_FILES; i++) {
        if (g_fighter.tiny_model[i].state == MODEL_MISSING) {
            g_fighter.tiny_model[i].state = MODEL_UNTRIED;
        }
    }
}

const PortGlFighterCounts *port_GlFighterCounts(void) {
    return &g_fighter.counts;
}

const char *port_GlFighterProblem(void) {
    return g_fighter.problem[0] != 0 ? g_fighter.problem : NULL;
}

/* ---- the models ---- */

static const char *port_GlFighterFileName(s32 model_id) {
    s32 file = model_id >= MODEL_FIRST_REMAPPED + MODEL_REMAP
                   ? model_id - MODEL_REMAP
                   : model_id;

    if (file < 0 || file >= MODEL_FILES) {
        return NULL;
    }
    return g_model_files[file];
}

/* A picture for the card. `is_smooth`: a picture drawn finer than the
 * console's (larger than the colour tables' picture of the same layout,
 * tools/fighter_hd.py), read between its texels and from smaller copies
 * of itself where it is drawn small; otherwise the console's own texels,
 * which are blocks. */
static GLuint port_GlFighterTexture(
    const PortFighterTexture *t, s32 is_smooth) {
    GLuint texture = 0;

    glGenTextures(1, &texture);
    glBindTexture(GL_TEXTURE_2D, texture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, t->w, t->h, 0, GL_RGBA,
                 GL_UNSIGNED_BYTE, t->rgba);
    if (is_smooth) {
        glGenerateMipmap(GL_TEXTURE_2D);
    }
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER,
                    is_smooth ? GL_LINEAR_MIPMAP_LINEAR : GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER,
                    is_smooth ? GL_LINEAR : GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    return texture;
}

/* A cloth of the file's as a smooth surface, and what the card draws it
 * from: the places and normals are written again each picture. */
static void port_GlFighterMakeCloth(
    FighterModel *model, const PortFighterFileCloth *from) {
    const PortGlClothVertex *one = NULL;
    FighterCloth *cloth = &model->cloth[model->cloth_count];
    PortGlCloth *surface;

    if (!g_fighter.is_cloth_smooth ||
        from->point_count > PORT_FIGHTER_CLOTH_POINTS) {
        return;
    }
    surface = port_GlClothMake(
        from->point_count, from->face_count, from->corners,
        (const s32(*)[4])from->face, (const float(*)[4][2])from->uv);
    if (surface == NULL) {
        return;
    }
    cloth->mesh = from->mesh;
    cloth->surface = surface;
    glGenVertexArrays(1, &cloth->array);
    glGenBuffers(1, &cloth->buffer);
    glGenBuffers(1, &cloth->elements);
    glBindVertexArray(cloth->array);
    glBindBuffer(GL_ARRAY_BUFFER, cloth->buffer);
    glBufferData(
        GL_ARRAY_BUFFER,
        (GLsizeiptr)((size_t)port_GlClothVertices(surface) * sizeof(*one)),
        NULL, GL_STREAM_DRAW);
    glVertexAttribPointer(
        0, 3, GL_FLOAT, GL_FALSE, (GLsizei)sizeof(*one), one->place);
    glVertexAttribPointer(
        1, 3, GL_FLOAT, GL_FALSE, (GLsizei)sizeof(*one), one->normal);
    glVertexAttribPointer(
        2, 2, GL_FLOAT, GL_FALSE, (GLsizei)sizeof(*one), one->uv);
    glEnableVertexAttribArray(0);
    glEnableVertexAttribArray(1);
    glEnableVertexAttribArray(2);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, cloth->elements);
    glBufferData(
        GL_ELEMENT_ARRAY_BUFFER,
        (GLsizeiptr)((size_t)port_GlClothTriangles(surface) * 3 * sizeof(u32)),
        port_GlClothIndices(surface), GL_STATIC_DRAW);
    glBindVertexArray(0);
    model->cloth_count++;
}

/* A model's file, `name`.glb, read and made ready for the card the first
 * time it is asked for. */
static FighterModel *port_GlFighterLoad(FighterModel *model, const char *name) {
    PortFighterFile file;
    const PortFighterVertex *one = NULL;
    const char *why;
    char path[700];
    s32 i;

    if (model->state == MODEL_LOADED) {
        return model;
    }
    if (model->state == MODEL_MISSING) {
        return NULL;
    }
    model->state = MODEL_MISSING;
    if (g_fighter.folder[0] != 0) {
        snprintf(path, sizeof(path), "%s/%s.glb", g_fighter.folder, name);
    } else {
        /* a mod's models/ folder: the key is the file's name in small
         * letters (docs/MODS.md) */
        char key[8];
        const char *found;

        for (i = 0; name[i] != 0 && i < (s32)sizeof(key) - 1; i++) {
            key[i] = (char)(name[i] >= 'A' && name[i] <= 'Z' ? name[i] + 32
                                                             : name[i]);
        }
        key[i] = 0;
        found = port_ModFind(PORT_MOD_MODELS, key);
        if (found == NULL) {
            return NULL;
        }
        snprintf(path, sizeof(path), "%s", found);
    }
    why = port_FighterFileRead(path, &file);
    if (why != NULL) {
        if (strcmp(why, "no such file") != 0) {
            g_fighter.counts.failed++;
            snprintf(g_fighter.problem, sizeof(g_fighter.problem), "%s: %s",
                     path, why);
        }
        return NULL;
    }
    glGenVertexArrays(1, &model->array);
    glGenBuffers(1, &model->buffer);
    glGenBuffers(1, &model->elements);
    glBindVertexArray(model->array);
    glBindBuffer(GL_ARRAY_BUFFER, model->buffer);
    glBufferData(
        GL_ARRAY_BUFFER, (GLsizeiptr)((size_t)file.vertex_count * sizeof(*one)),
        file.vertices, GL_STATIC_DRAW);
    glVertexAttribPointer(
        0, 3, GL_FLOAT, GL_FALSE, (GLsizei)sizeof(*one), one->place);
    glVertexAttribPointer(
        1, 3, GL_FLOAT, GL_FALSE, (GLsizei)sizeof(*one), one->normal);
    glVertexAttribPointer(
        2, 2, GL_FLOAT, GL_FALSE, (GLsizei)sizeof(*one), one->uv);
    glVertexAttribPointer(
        3, 4, GL_FLOAT, GL_FALSE, (GLsizei)sizeof(*one), one->weight);
    glVertexAttribIPointer(
        4, 4, GL_UNSIGNED_BYTE, (GLsizei)sizeof(*one), one->joint);
    for (i = 0; i < PORT_FIGHTER_TERMS; i++) {
        glVertexAttribPointer((GLuint)(5 + i), 4, GL_FLOAT, GL_FALSE,
                              (GLsizei)sizeof(*one), one->term[i]);
    }
    glVertexAttribIPointer(
        13, 4, GL_UNSIGNED_BYTE, (GLsizei)sizeof(*one), one->term_joint);
    glVertexAttribIPointer(
        14, 4, GL_UNSIGNED_BYTE, (GLsizei)sizeof(*one), one->term_joint + 4);
    for (i = 0; i < 15; i++) {
        glEnableVertexAttribArray((GLuint)i);
    }
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, model->elements);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER,
                 (GLsizeiptr)((size_t)file.index_count * sizeof(u32)),
                 file.indices, GL_STATIC_DRAW);
    glBindVertexArray(0);
    model->part_count = file.part_count;
    for (i = 0; i < file.part_count; i++) {
        model->part[i] = file.part[i];
    }
    for (i = 0; i < file.texture_count; i++) {
        model->texture[i] = port_GlFighterTexture(
            &file.texture[i],
            file.index.w > 0 && file.texture[i].w > file.index.w);
    }
    for (i = 0; i < PORT_FIGHTER_FILE_JOINTS; i++) {
        if (file.has_joint[i]) {
            memcpy(model->inverse_bind[i], file.inverse_bind[i],
                   sizeof(model->inverse_bind[i]));
        } else {
            s32 k;

            /* a bone the file has no joint for moves nothing of it */
            for (k = 0; k < 16; k++) {
                model->inverse_bind[i][k] = (k % 5 == 0) ? 1.0f : 0.0f;
            }
        }
    }
    model->triangles = file.index_count / 3;
    /* kept: the shadows are cast from them (port_GlFighterCastTriangles) */
    model->vertex_count = file.vertex_count;
    model->vertices = file.vertices;
    model->indices = file.indices;
    file.vertices = NULL;
    file.indices = NULL;
    model->clut_count = 0;
    if (file.clut_count > 0 && file.index.w > 0) {
        model->clut = malloc((size_t)file.clut_count * sizeof(*model->clut));
        if (model->clut != NULL) {
            memcpy(model->clut, file.clut,
                   (size_t)file.clut_count * sizeof(*model->clut));
            model->clut_count = file.clut_count;
            /* an entry is a whole number: never smoothed */
            model->index_texture = port_GlFighterTexture(&file.index, FALSE);
        }
    }
    model->cloth_count = 0;
    if (file.part_count > 0 && file.part[0].texture >= 0) {
        model->cloth_texture = model->texture[file.part[0].texture];
        for (i = 0; i < file.cloth_count; i++) {
            port_GlFighterMakeCloth(model, &file.cloth[i]);
        }
    }
    port_FighterFileFree(&file);
    model->state = MODEL_LOADED;
    g_fighter.counts.loaded++;
    return model;
}

static FighterModel *port_GlFighterModel(s32 model_id) {
    const char *name = port_GlFighterFileName(model_id);

    if (model_id < 0 || model_id >= MODELS_MAX || name == NULL) {
        return NULL;
    }
    return port_GlFighterLoad(&g_fighter.model[model_id], name);
}

/* A weapon model's (enum WeaponModel): `W00` to `W57`. */
static FighterModel *port_GlFighterWeaponModel(s32 weapon) {
    char name[8];

    if (weapon < 0 || weapon >= WEAPON_FILES) {
        return NULL;
    }
    snprintf(name, sizeof(name), "W%02d", weapon);
    return port_GlFighterLoad(&g_fighter.weapon_model[weapon], name);
}

/* A tiny model's (g_model_tiny_ids): `T00` to `T26`. */
static FighterModel *port_GlFighterTinyModel(s32 tiny) {
    char name[8];

    if (tiny < 0 || tiny >= TINY_FILES) {
        return NULL;
    }
    snprintf(name, sizeof(name), "T%02d", tiny);
    return port_GlFighterLoad(&g_fighter.tiny_model[tiny], name);
}

void port_GlFighterPreload(void) {
    s32 i;

    if (!g_fighter.is_on || !g_fighter.is_started) {
        return;
    }
    for (i = 0; i < PORT_FIGHTER_SLOTS; i++) {
        s32 model_id = port_FighterLoadedModel(i);

        if (model_id >= 0) {
            port_GlFighterModel(model_id);
            /* and its first-person view's figure, a small file */
            port_GlFighterTinyModel(port_FighterLoadedTiny(i));
        }
    }
    for (i = 0; i < WEAPON_FILES; i++) {
        s32 weapon = port_FighterLoadedWeapon(i);

        if (weapon < 0) {
            break;
        }
        port_GlFighterWeaponModel(weapon);
    }
}

/* ---- a frame ---- */

void port_GlFighterNewFrame(void) {
    g_fighter.is_drawn = FALSE;
    g_fighter.is_laid = FALSE;
    g_fighter.is_tiny_drawn = FALSE;
}

/* The slot whose model the primitive is a drawing of, by where its node
 * is, or -1. */
static s32 port_GlFighterSlotOf(
    const PortRemasterPrim *prim, const PortFighterView *views) {
    s32 slot;

    for (slot = 0; slot < PORT_FIGHTER_SLOTS; slot++) {
        const PortFighterView *view = &views[slot];

        if (view->model_id >= 0 && prim->id.object >= view->nodes_from &&
            prim->id.object < view->nodes_to) {
            return slot;
        }
    }
    return -1;
}

/* A square root, by Newton's steps: the program is linked with no maths
 * library. */
static double port_GlFighterRoot(double x) {
    double r = x > 1.0 ? x : 1.0;
    s32 i;

    if (x <= 0.0) {
        return 0.0;
    }
    for (i = 0; i < 40; i++) {
        r = 0.5 * (r + x / r);
    }
    return r;
}

/* A matrix's rotation part between two, made a rotation again. */
static void port_GlFighterMixRotation(
    double *out, const s16 *a, const s16 *b, double t) {
    double row[3][3];
    s32 i;
    s32 j;

    for (i = 0; i < 3; i++) {
        for (j = 0; j < 3; j++) {
            row[i][j] = ((1.0 - t) * a[i * 3 + j] + t * b[i * 3 + j]) / 4096.0;
        }
    }
    /* Gram-Schmidt on the columns: the bones are turned, never stretched */
    for (j = 0; j < 3; j++) {
        double length;
        s32 k;

        for (k = 0; k < j; k++) {
            double dot = 0.0;

            for (i = 0; i < 3; i++) {
                dot += row[i][j] * row[i][k];
            }
            for (i = 0; i < 3; i++) {
                row[i][j] -= dot * row[i][k];
            }
        }
        length =
            port_GlFighterRoot(row[0][j] * row[0][j] + row[1][j] * row[1][j] +
                               row[2][j] * row[2][j]);
        if (length < 1e-9) {
            length = 1.0;
        }
        for (i = 0; i < 3; i++) {
            row[i][j] /= length;
        }
    }
    for (i = 0; i < 9; i++) {
        out[i] = row[i / 3][i % 3];
    }
}

/* The view a fighter is drawn with: the frame's own, or between the
 * frame on show and the one after it. Rotations as numbers, 1 being 1. */
typedef struct {
    double bone_rotation[PORT_FIGHTER_BONES][9];
    double bone_place[PORT_FIGHTER_BONES][3];
    double view[9];
    double camera[3];
    double h;
    double ofx;
    double ofy;
    /* the lights: their matrix and colours, and the colour of the dark */
    const s16 *light;
    const s16 *color;
    const s32 *back;
} FighterPose;

static void port_GlFighterPoseOf(FighterPose *pose, const PortFighterView *a,
                                 const PortFighterView *b, double t) {
    s32 i;
    s32 k;

    if (b == NULL || b->model_id != a->model_id) {
        b = a;
        t = 0.0;
    }
    for (i = 0; i < PORT_FIGHTER_BONES; i++) {
        port_GlFighterMixRotation(
            pose->bone_rotation[i], a->bone[i].m, b->bone[i].m, t);
        for (k = 0; k < 3; k++) {
            pose->bone_place[i][k] =
                (1.0 - t) * a->bone[i].t[k] + t * b->bone[i].t[k];
        }
    }
    /* the view as the stage's mesh mixes it (port_GlStageMixView): each
     * number of the rotation evenly */
    for (i = 0; i < 9; i++) {
        pose->view[i] = ((1.0 - t) * a->view[i] + t * b->view[i]) / 4096.0;
    }
    for (k = 0; k < 3; k++) {
        pose->camera[k] = (1.0 - t) * a->camera[k] + t * b->camera[k];
    }
    pose->h = (1.0 - t) * a->h + t * b->h;
    pose->ofx = ((1.0 - t) * a->ofx + t * b->ofx) / 65536.0;
    pose->ofy = ((1.0 - t) * a->ofy + t * b->ofy) / 65536.0;
    pose->light = t < 0.5 ? a->light : b->light;
    pose->color = t < 0.5 ? a->color : b->color;
    pose->back = t < 0.5 ? a->back : b->back;
}

/* A layer the models are drawn into: at least `w` by `h`. */
static s32 port_GlFighterLayer(FighterLayer *layer, s32 w, s32 h) {
    static const GLenum both[2] = {GL_COLOR_ATTACHMENT0, GL_COLOR_ATTACHMENT1};

    if (w <= layer->w && h <= layer->h) {
        glBindFramebuffer(GL_FRAMEBUFFER, layer->framebuffer);
        glDrawBuffers(2, both);
        return TRUE;
    }
    if (w < layer->w) {
        w = layer->w;
    }
    if (h < layer->h) {
        h = layer->h;
    }
    glBindTexture(GL_TEXTURE_2D, layer->color);
    glTexImage2D(
        GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glBindTexture(GL_TEXTURE_2D, layer->depth_value);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_R32F, w, h, 0, GL_RED, GL_FLOAT, NULL);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glBindRenderbuffer(GL_RENDERBUFFER, layer->depth);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, w, h);
    glBindFramebuffer(GL_FRAMEBUFFER, layer->framebuffer);
    glFramebufferTexture2D(
        GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, layer->color, 0);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT1, GL_TEXTURE_2D,
                           layer->depth_value, 0);
    glFramebufferRenderbuffer(
        GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, layer->depth);
    glDrawBuffers(2, both);
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
        port_GlFighterSay("the fighters' layer cannot be drawn into", NULL);
        layer->w = 0;
        layer->h = 0;
        return FALSE;
    }
    layer->w = w;
    layer->h = h;
    return TRUE;
}

static void port_GlFighterUpload3(
    GLint at, const double *m, double scale_row1) {
    GLfloat rows[9];
    s32 i;

    for (i = 0; i < 9; i++) {
        rows[i] = (GLfloat)(m[i] * (i / 3 == 1 ? scale_row1 : 1.0));
    }
    /* the numbers are a row after a row */
    glUniformMatrix3fv(at, 1, GL_TRUE, rows);
}

static void port_GlFighterUploadLights(const FighterPose *pose) {
    GLfloat light[9];
    GLfloat color[9];
    s32 i;

    for (i = 0; i < 9; i++) {
        light[i] = (GLfloat)pose->light[i] / 4096.0f;
        color[i] = (GLfloat)pose->color[i] / 4096.0f;
    }
    glUniformMatrix3fv(g_fighter.at_light, 1, GL_TRUE, light);
    glUniformMatrix3fv(g_fighter.at_color, 1, GL_TRUE, color);
    glUniform3f(g_fighter.at_back, (GLfloat)pose->back[0],
                (GLfloat)pose->back[1], (GLfloat)pose->back[2]);
    glUniform1i(g_fighter.at_smooth_light, port_GlLightIsSmooth() ? 1 : 0);
}

/* A primitive of a drawn thing read a colour table at (x, y): kept, as
 * long as the model drawn there is the same, until the place of its
 * tables is known. */
static void port_GlFighterSawClut(s32 place, s32 model_id, s32 x, s32 y) {
    ClutPlace *p = &g_fighter.place[place];
    s32 i;

    if (p->model != model_id) {
        memset(p, 0, sizeof(*p));
        p->model = model_id;
    }
    if (p->is_known || p->count >= SEEN_CLUTS) {
        return;
    }
    for (i = 0; i < p->count; i++) {
        if (p->x[i] == x && p->y[i] == y) {
            return;
        }
    }
    p->x[p->count] = x;
    p->y[p->count] = y;
    p->count++;
}

/* Whether every table seen, moved back by (dx, dy), is one of the file's. */
static s32 port_GlFighterShiftFits(
    const ClutPlace *p, const FighterModel *model, s32 dx, s32 dy) {
    s32 i;
    s32 k;

    for (i = 0; i < p->count; i++) {
        for (k = 0; k < model->clut_count; k++) {
            if (model->clut[k].x == p->x[i] - dx &&
                model->clut[k].y == p->y[i] - dy) {
                break;
            }
        }
        if (k == model->clut_count) {
            return FALSE;
        }
    }
    return TRUE;
}

/* Where the place's tables are, from what its primitives read: the one
 * shift that takes every table seen to one of the file's. Left unknown
 * while more than one would. */
static void port_GlFighterFindPlace(s32 place, const FighterModel *model) {
    ClutPlace *p = &g_fighter.place[place];
    s32 found = 0;
    s32 dx = 0;
    s32 dy = 0;
    s32 k;

    if (p->is_known || p->count == 0 || model == NULL ||
        model->clut_count == 0) {
        return;
    }
    for (k = 0; k < model->clut_count; k++) {
        s32 x = p->x[0] - model->clut[k].x;
        s32 y = p->y[0] - model->clut[k].y;

        if (port_GlFighterShiftFits(p, model, x, y) &&
            (found == 0 || x != dx || y != dy)) {
            found++;
            dx = x;
            dy = y;
        }
    }
    if (found == 1) {
        p->is_known = TRUE;
        p->dx = dx;
        p->dy = dy;
    }
}

/* What the game has done to the model's colour tables, into the place's
 * texture: each entry's colour now less its colour in the file, a
 * channel of five bits made 0 to 1, and alpha -1 where the game has made
 * an entry nothing. FALSE where nothing has changed, or where the
 * tables have not been found. */
static s32 port_GlFighterPalette(const FighterModel *model, s32 place) {
    const ClutPlace *p = &g_fighter.place[place];
    float *out = g_fighter.palette;
    s32 changed = FALSE;
    s32 c;
    s32 k;

    if (model->clut_count == 0 || !p->is_known) {
        return FALSE;
    }
    for (c = 0; c < model->clut_count; c++) {
        const PortFighterClut *clut = &model->clut[c];
        const u16 *row =
            &g_port_vram[((clut->y + p->dy) & (PORT_VRAM_HEIGHT - 1)) *
                         PORT_VRAM_WIDTH];

        for (k = 0; k < 256; k++) {
            float *at = out + (c * 256 + k) * 4;
            u16 now;
            u16 was;
            s32 i;

            if (k >= clut->entries) {
                at[0] = at[1] = at[2] = at[3] = 0.0f;
                continue;
            }
            now = row[(clut->x + p->dx + k) & (PORT_VRAM_WIDTH - 1)];
            was = clut->color[k];
            for (i = 0; i < 3; i++) {
                at[i] = (float)((s32)((now >> (5 * i)) & 31) -
                                (s32)((was >> (5 * i)) & 31)) /
                        31.0f;
            }
            at[3] = now == 0 && was != 0 ? -1.0f : 0.0f;
            if ((now & 0x7FFF) != (was & 0x7FFF) || (now == 0) != (was == 0)) {
                changed = TRUE;
            }
        }
    }
    if (changed) {
        glActiveTexture(GL_TEXTURE0 + 5);
        glBindTexture(GL_TEXTURE_2D, g_fighter.palette_texture[place]);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA32F, 256, model->clut_count, 0,
                     GL_RGBA, GL_FLOAT, out);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glActiveTexture(GL_TEXTURE0 + 4);
        glBindTexture(GL_TEXTURE_2D, model->index_texture);
        glActiveTexture(GL_TEXTURE0);
        g_fighter.counts.tinted++;
    }
    return changed;
}

/* One fighter, or one weapon, into the layer: `place` is whose colour
 * tables it reads (ClutPlace). */
static void port_GlFighterDrawOne(
    const FighterModel *model, const FighterPose *pose, s32 place) {
    GLfloat joints[PORT_FIGHTER_FILE_JOINTS * 16];
    GLfloat worlds[PORT_FIGHTER_FILE_JOINTS * 16];
    s32 j;
    s32 i;

    for (j = 0; j < PORT_FIGHTER_FILE_JOINTS; j++) {
        /* the bone's world matrix, its place from the camera's, times the
         * inverse bind matrix: a column after a column */
        double world[16];
        const float *ibm = model->inverse_bind[j];
        s32 r;
        s32 c;

        for (r = 0; r < 3; r++) {
            for (c = 0; c < 3; c++) {
                world[c * 4 + r] = pose->bone_rotation[j][r * 3 + c];
            }
            world[12 + r] = pose->bone_place[j][r] - pose->camera[r];
            world[r * 4 + 3] = 0.0;
        }
        world[15] = 1.0;
        for (c = 0; c < 16; c++) {
            worlds[j * 16 + c] = (GLfloat)world[c];
        }
        for (c = 0; c < 4; c++) {
            for (r = 0; r < 4; r++) {
                double sum = 0.0;
                s32 k;

                for (k = 0; k < 4; k++) {
                    sum += world[k * 4 + r] * (double)ibm[c * 4 + k];
                }
                joints[j * 16 + c * 4 + r] = (GLfloat)sum;
            }
        }
    }
    glUniformMatrix4fv(
        g_fighter.at_joint, PORT_FIGHTER_FILE_JOINTS, GL_FALSE, joints);
    glUniformMatrix4fv(
        g_fighter.at_world, PORT_FIGHTER_FILE_JOINTS, GL_FALSE, worlds);
    glUniform1i(g_fighter.at_paletted, port_GlFighterPalette(model, place));
    port_GlFighterUpload3(g_fighter.at_view, pose->view, 0.5);
    glUniform1f(g_fighter.at_h, (GLfloat)pose->h);
    port_GlFighterUploadLights(pose);
    glBindVertexArray(model->array);
    for (i = 0; i < model->part_count; i++) {
        const PortFighterPart *part = &model->part[i];

        if (part->texture < 0) {
            continue;
        }
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, model->texture[part->texture]);
        glDrawElements(GL_TRIANGLES, part->count, GL_UNSIGNED_INT,
                       (const void *)(size_t)((u32)part->first * sizeof(u32)));
    }
    glBindVertexArray(0);
    g_fighter.counts.triangles += (u32)model->triangles;
}

/* ---- the cloths (gl_cloth.h) ---- */

/* The model's smooth cloth that a cloth the frame drew is, or NULL: the
 * same mesh of the model's file, of as many points. */
static const FighterCloth *port_GlFighterClothFor(
    const FighterModel *model, const PortFighterCloth *drawn) {
    s32 i;

    for (i = 0; i < model->cloth_count; i++) {
        const FighterCloth *cloth = &model->cloth[i];

        if (cloth->mesh == drawn->mesh &&
            port_GlClothPoints(cloth->surface) == drawn->count) {
            return cloth;
        }
    }
    return NULL;
}

/* Which of the view's cloths the node is, if it is drawn smooth: its
 * number in the view, or -1. */
static s32 port_GlFighterClothOfNode(
    const FighterModel *model, const PortFighterView *view, PortAddress node) {
    s32 k;

    if (model == NULL) {
        return -1;
    }
    for (k = 0; k < view->cloth_count; k++) {
        if (view->cloth[k].node == node) {
            return port_GlFighterClothFor(model, &view->cloth[k]) != NULL
                       ? k
                       : -1;
        }
    }
    return -1;
}

/* The model's body as `pose` poses it, less the camera's place: what its
 * cloths are kept out of (gl_cloth.h). Each vertex is where the card puts
 * it (the sum of its terms) and its normal turned as the card turns it
 * (the joints' rotations, blended). Kept for the slot until it is asked
 * for again. NULL where the cloths are not kept out. */
static const PortGlClothBody *port_GlFighterBody(
    const FighterModel *model, const FighterPose *pose, s32 slot) {
    static PortGlClothBody *bodies[PORT_FIGHTER_SLOTS];
    static float *places;
    static float *normals;
    static s32 room;
    double turn[PORT_FIGHTER_FILE_JOINTS][9];
    s32 i;
    s32 j;
    s32 k;

    if (model->vertices == NULL || slot < 0 || slot >= PORT_FIGHTER_SLOTS) {
        return NULL;
    }
    if (bodies[slot] == NULL) {
        bodies[slot] = port_GlClothBodyMake();
    }
    if (room < model->vertex_count) {
        float *more_places =
            realloc(places, (size_t)model->vertex_count * 3 * sizeof(float));
        float *more_normals =
            more_places == NULL
                ? NULL
                : realloc(
                      normals, (size_t)model->vertex_count * 3 * sizeof(float));

        if (more_places != NULL) {
            places = more_places;
        }
        if (more_normals == NULL) {
            return NULL;
        }
        normals = more_normals;
        room = model->vertex_count;
    }
    if (bodies[slot] == NULL) {
        return NULL;
    }
    /* each joint's turn: the bone's rotation times the inverse bind
     * matrix's */
    for (j = 0; j < PORT_FIGHTER_FILE_JOINTS; j++) {
        const float *ibm = model->inverse_bind[j];
        s32 r;
        s32 c;

        for (r = 0; r < 3; r++) {
            for (c = 0; c < 3; c++) {
                double sum = 0.0;

                for (k = 0; k < 3; k++) {
                    sum += pose->bone_rotation[j][r * 3 + k] *
                           (double)ibm[c * 4 + k];
                }
                turn[j][r * 3 + c] = sum;
            }
        }
    }
    for (i = 0; i < model->vertex_count; i++) {
        const PortFighterVertex *v = &model->vertices[i];
        double place[3] = {0.0, 0.0, 0.0};
        double normal[3] = {0.0, 0.0, 0.0};

        for (j = 0; j < PORT_FIGHTER_TERMS; j++) {
            const double *r = pose->bone_rotation[v->term_joint[j]];
            const double *t = pose->bone_place[v->term_joint[j]];
            const float *term = v->term[j];

            if (term[0] == 0.0f && term[1] == 0.0f && term[2] == 0.0f &&
                term[3] == 0.0f) {
                continue;
            }
            for (k = 0; k < 3; k++) {
                place[k] += r[k * 3] * term[0] + r[k * 3 + 1] * term[1] +
                            r[k * 3 + 2] * term[2] + t[k] * term[3];
            }
        }
        for (j = 0; j < 4; j++) {
            const double *r = turn[v->joint[j] % PORT_FIGHTER_FILE_JOINTS];

            if (v->weight[j] == 0.0f) {
                continue;
            }
            for (k = 0; k < 3; k++) {
                normal[k] +=
                    v->weight[j] *
                    (r[k * 3] * v->normal[0] + r[k * 3 + 1] * v->normal[1] +
                     r[k * 3 + 2] * v->normal[2]);
            }
        }
        for (k = 0; k < 3; k++) {
            places[i * 3 + k] = (float)(place[k] - pose->camera[k]);
            normals[i * 3 + k] = (float)normal[k];
        }
    }
    if (!port_GlClothBodySet(
            bodies[slot], places, normals, model->vertex_count)) {
        return NULL;
    }
    return bodies[slot];
}

/* A place of a cloth kept out of the body (port_GlClothHold), and
 * counted: whether the game's cloth had it inside the body, and whether
 * it is still inside once kept out. */
static void port_GlFighterHoldPlace(void *context, double *place) {
    const PortGlClothBody *body = context;
    double where[3] = {place[0], place[1], place[2]};

    g_fighter.counts.cloth_places++;
    if (port_GlClothBodyHold(body, where, CLOTH_GAP)) {
        g_fighter.counts.cloth_inside++;
    }
    if (g_fighter.is_cloth_held) {
        place[0] = where[0];
        place[1] = where[1];
        place[2] = where[2];
    }
    where[0] = place[0];
    where[1] = place[1];
    where[2] = place[2];
    if (port_GlClothBodyHold(body, where, 0.0)) {
        g_fighter.counts.cloth_left_inside++;
    }
}

/* A cloth's surface through its points as the frame drew them, or `t` of
 * the way to where the frame after it drew them (the same mesh, in
 * `after`), less `camera`, kept out of `body` where there is one: the
 * vertices to draw, in room kept here. */
static const PortGlClothVertex *port_GlFighterPoseCloth(
    const FighterCloth *cloth, const PortFighterCloth *drawn,
    const PortFighterView *after, double t, const double *camera,
    const PortGlClothBody *body) {
    static PortGlClothVertex *room;
    static s32 room_count;
    const PortFighterCloth *next = drawn;
    double points[PORT_FIGHTER_CLOTH_POINTS * 3];
    s32 count = port_GlClothVertices(cloth->surface);
    s32 i;
    s32 k;

    if (room_count < count) {
        PortGlClothVertex *more = realloc(room, (size_t)count * sizeof(*room));

        if (more == NULL) {
            return NULL;
        }
        room = more;
        room_count = count;
    }
    for (i = 0; after != NULL && i < after->cloth_count; i++) {
        if (after->cloth[i].mesh == drawn->mesh &&
            after->cloth[i].count == drawn->count) {
            next = &after->cloth[i];
            break;
        }
    }
    if (next == drawn) {
        t = 0.0;
    }
    /* a point is in 128ths of the world's unit (ClothPoint) */
    for (i = 0; i < drawn->count; i++) {
        for (k = 0; k < 3; k++) {
            points[i * 3 + k] =
                ((1.0 - t) * drawn->point[i][k] + t * next->point[i][k]) /
                    128.0 -
                camera[k];
        }
        /* the points the surface is made through are kept out of the
         * body first, and then every place of the surface */
        if (body != NULL && g_fighter.is_cloth_held) {
            port_GlClothBodyHold(body, &points[i * 3], CLOTH_GAP);
        }
    }
    port_GlClothPose(
        cloth->surface, points, body != NULL ? port_GlFighterHoldPlace : NULL,
        (void *)body, room);
    return room;
}

/* The slot's cloths that are drawn smooth, into the layer after his body,
 * with the body's view, lights and colour tables; from both sides. */
static void port_GlFighterDrawCloths(
    const FighterModel *model, const PortFighterView *view,
    const PortFighterView *after, double t, const FighterPose *pose, s32 slot) {
    const PortGlClothBody *body = NULL;
    s32 drawn = 0;
    s32 k;

    if (after != NULL && after->model_id != view->model_id) {
        after = NULL;
    }
    for (k = 0; k < view->cloth_count; k++) {
        const FighterCloth *cloth =
            port_GlFighterClothFor(model, &view->cloth[k]);
        const PortGlClothVertex *vertices;

        if (cloth == NULL) {
            continue;
        }
        if (body == NULL) {
            body = port_GlFighterBody(model, pose, slot);
        }
        vertices = port_GlFighterPoseCloth(
            cloth, &view->cloth[k], after, t, pose->camera, body);
        if (vertices == NULL) {
            continue;
        }
        if (drawn++ == 0) {
            glUniform1i(g_fighter.at_cloth, 1);
            glUniform1f(g_fighter.at_cloth_near,
                        (GLfloat)(CLOTH_NEAR << (view->scale_shift & 3)));
            glDisable(GL_CULL_FACE);
            /* kept out of the body by CLOTH_GAP, and taken a little
             * nearer in the depth buffer as well, so that where it lies
             * close over the body the two never flicker in turn */
            glEnable(GL_POLYGON_OFFSET_FILL);
            glPolygonOffset(-1.0f, -4.0f);
            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, model->cloth_texture);
        }
        glBindVertexArray(cloth->array);
        glBindBuffer(GL_ARRAY_BUFFER, cloth->buffer);
        glBufferData(GL_ARRAY_BUFFER,
                     (GLsizeiptr)((size_t)port_GlClothVertices(cloth->surface) *
                                  sizeof(*vertices)),
                     vertices, GL_STREAM_DRAW);
        glDrawElements(GL_TRIANGLES, port_GlClothTriangles(cloth->surface) * 3,
                       GL_UNSIGNED_INT, NULL);
        g_fighter.counts.triangles +=
            (u32)port_GlClothTriangles(cloth->surface);
        g_fighter.counts.cloths++;
    }
    if (drawn > 0) {
        glBindVertexArray(0);
        glUniform1i(g_fighter.at_cloth, 0);
        glDisable(GL_POLYGON_OFFSET_FILL);
        if (g_fighter.cull != 0) {
            glEnable(GL_CULL_FACE);
        }
    }
}

/* A weapon's place in the frame after the one on show: the weapon of the
 * same model drawn there in the same turn, or another of the same model. */
static const PortFighterWeapon *port_GlFighterWeaponAfter(
    const PortFighterWeapon *weapon, s32 index, const PortFighterWeapon *after,
    s32 count) {
    s32 i;

    if (after == NULL) {
        return NULL;
    }
    if (index < count && after[index].weapon == weapon->weapon) {
        return &after[index];
    }
    for (i = 0; i < count; i++) {
        if (after[i].weapon == weapon->weapon) {
            return &after[i];
        }
    }
    return NULL;
}

/* A weapon's pose: every bone on the weapon's world matrix (its file's
 * vertices are on bone 0, rb_weapon), between two frames as the bones of
 * a fighter are; the view is the frame's, `frame`. */
static void port_GlFighterWeaponPose(
    FighterPose *pose, const FighterPose *frame, const PortFighterWeapon *a,
    const PortFighterWeapon *b, double t) {
    double rotation[9];
    s32 i;
    s32 k;

    *pose = *frame;
    if (b == NULL) {
        b = a;
        t = 0.0;
    }
    port_GlFighterMixRotation(rotation, a->world.m, b->world.m, t);
    for (i = 0; i < PORT_FIGHTER_BONES; i++) {
        memcpy(pose->bone_rotation[i], rotation, sizeof(rotation));
        for (k = 0; k < 3; k++) {
            pose->bone_place[i][k] =
                (1.0 - t) * a->world.t[k] + t * b->world.t[k];
        }
    }
    pose->light = t < 0.5 ? a->light : b->light;
    pose->color = t < 0.5 ? a->color : b->color;
    pose->back = t < 0.5 ? a->back : b->back;
}

/* Every fighter and weapon of the frame that has a model of its own into
 * the layer, and the layer over the picture, in place of the primitive
 * `paint`. */
static void port_GlFighterDrawAll(
    const PortRemasterPrim *paint, s32 which, const PortFighterView *views,
    const PortFighterView *after, double t, const PortFighterWeapon *weapons,
    s32 weapon_count, const PortFighterWeapon *after_weapons,
    s32 after_weapon_count) {
    FighterLayer *layer = &g_fighter.layer[which];
    s32 is_tiny = which == LAYER_TINY;
    static const GLfloat clear_color[4] = {0.0f, 0.0f, 0.0f, 0.0f};
    static const GLfloat clear_depth[4] = {FAR_PLANE, 0.0f, 0.0f, 0.0f};
    PortGlPicture picture;
    PortGlOptions options;
    FighterPose pose;
    FighterPose frame;
    s32 has_frame = FALSE;
    s32 i;
    s32 x;
    s32 y;
    s32 w;
    s32 h;
    s32 slot;
    s32 drawn = 0;

    port_GlGetPicture(&picture);
    port_GlGetOptions(&options);
    /* the layer covers the drawing area, or the whole width of a wider
     * picture, as the scene is drawn across it (gl_draw.c) */
    y = paint->clip_top * picture.draw_y;
    h = (paint->clip_bottom - paint->clip_top + 1) * picture.draw_y;
    if (picture.margin > 0 && picture.display_w > 0) {
        x = picture.display_x * picture.draw_x;
        w = (picture.display_w + 2 * picture.margin) * picture.draw_x;
    } else {
        x = (paint->clip_left + picture.margin) * picture.draw_x;
        w = (paint->clip_right - paint->clip_left + 1) * picture.draw_x;
    }
    if (w <= 0 || h <= 0 || !port_GlFighterLayer(layer, w, h)) {
        return;
    }
    glDisable(GL_SCISSOR_TEST);
    glDisable(GL_BLEND);
    glViewport(0, 0, w, h);
    glClearBufferfv(GL_COLOR, 0, clear_color);
    glClearBufferfv(GL_COLOR, 1, clear_depth);
    glClearDepth(1.0);
    glDepthMask(GL_TRUE);
    glClear(GL_DEPTH_BUFFER_BIT);
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LESS);
    if (g_fighter.cull != 0) {
        glEnable(GL_CULL_FACE);
        glCullFace(GL_BACK);
        glFrontFace(g_fighter.cull == 1 ? GL_CW : GL_CCW);
    }
    glUseProgram(g_fighter.program);
    glUniform1i(g_fighter.at_texture, 0);
    glUniform2f(
        g_fighter.at_scale, (GLfloat)picture.draw_x, (GLfloat)picture.draw_y);
    glUniform2f(g_fighter.at_origin, (GLfloat)x, (GLfloat)y);
    glUniform2f(g_fighter.at_size, (GLfloat)w, (GLfloat)h);
    glUniform1i(g_fighter.at_index, 4);
    glUniform1i(g_fighter.at_palette, 5);
    for (slot = 0; slot < PORT_FIGHTER_SLOTS; slot++) {
        const PortFighterView *view = &views[slot];
        const FighterModel *model;

        if (view->model_id < 0) {
            continue;
        }
        port_GlFighterPoseOf(
            &pose, view, after != NULL ? &after[slot] : NULL, t);
        if (!has_frame) {
            /* the view the weapons are drawn with */
            frame = pose;
            has_frame = TRUE;
        }
        model = is_tiny ? port_GlFighterTinyModel(view->model_id)
                        : port_GlFighterModel(view->model_id);
        if (model == NULL) {
            continue;
        }
        glUniform2f(g_fighter.at_screen,
                    (GLfloat)(pose.ofx + paint->offset_x + picture.margin),
                    (GLfloat)(pose.ofy + paint->offset_y));
        glUseProgram(g_fighter.program);
        port_GlFighterDrawOne(model, &pose, slot);
        port_GlFighterDrawCloths(
            model, view, after != NULL ? &after[slot] : NULL, t, &pose, slot);
        drawn++;
    }
    for (i = 0; i < weapon_count && has_frame; i++) {
        const PortFighterWeapon *weapon = &weapons[i];
        const FighterModel *model = port_GlFighterWeaponModel(weapon->weapon);

        if (model == NULL) {
            continue;
        }
        port_GlFighterWeaponPose(
            &pose, &frame, weapon,
            port_GlFighterWeaponAfter(
                weapon, i, after_weapons, after_weapon_count),
            t);
        glUniform2f(g_fighter.at_screen,
                    (GLfloat)(pose.ofx + paint->offset_x + picture.margin),
                    (GLfloat)(pose.ofy + paint->offset_y));
        port_GlFighterDrawOne(
            model, &pose, PORT_FIGHTER_SLOTS + weapon->weapon);
        drawn++;
    }
    glDisable(GL_CULL_FACE);

    /* the layer over the picture */
    glBindFramebuffer(GL_FRAMEBUFFER, picture.framebuffer);
    {
        static const GLenum one[1] = {GL_COLOR_ATTACHMENT0};

        glDrawBuffers(1, one);
    }
    glViewport(0, 0, picture.target_w, picture.target_h);
    glEnable(GL_SCISSOR_TEST);
    glScissor(x, y, w, h);
    glUseProgram(g_fighter.composite_program);
    glActiveTexture(GL_TEXTURE0 + 2);
    glBindTexture(GL_TEXTURE_2D, layer->color);
    glActiveTexture(GL_TEXTURE0 + 3);
    glBindTexture(GL_TEXTURE_2D, layer->depth_value);
    glActiveTexture(GL_TEXTURE0 + 1);
    glBindTexture(GL_TEXTURE_2D, picture.slot_texture);
    glActiveTexture(GL_TEXTURE0);
    glUniform1i(g_fighter.c_at_layer, 2);
    glUniform1i(g_fighter.c_at_layer_depth, 3);
    glUniform1i(g_fighter.c_at_ground_slots, 1);
    /* the tiny model is drawn after the scene, over everything */
    glUniform1i(
        g_fighter.c_at_ground_order,
        !is_tiny && port_GlStageSaveFrame() && options.is_stage_depth ? 1 : 0);
    glUniform2f(g_fighter.c_at_origin, (GLfloat)x, (GLfloat)y);
    glUniform4f(
        g_fighter.c_at_rect, (GLfloat)x, (GLfloat)y, (GLfloat)w, (GLfloat)h);
    glUniform2f(g_fighter.c_at_target, (GLfloat)picture.target_w,
                (GLfloat)picture.target_h);
    glUniform1f(g_fighter.c_at_depth_range, PORT_GL_DEPTH_RANGE);
    /* kept for the shadows' mask (port_GlFighterDrawOccluders) */
    if (!is_tiny) {
        g_fighter.laid[0] = x;
        g_fighter.laid[1] = y;
        g_fighter.laid[2] = w;
        g_fighter.laid[3] = h;
    }
    /* the depth is written whatever is there: the test is only how GL
     * lets a fragment write it */
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_ALWAYS);
    glDepthMask(GL_TRUE);
    glBindVertexArray(g_fighter.quad_array);
    glDrawArrays(GL_TRIANGLES, 0, 6);
    glBindVertexArray(0);
    glDepthMask(GL_FALSE);
    glDepthFunc(GL_LESS);
    glDisable(GL_DEPTH_TEST);
    port_GlForgetState();
    if (is_tiny) {
        return;
    }
    g_fighter.is_laid = TRUE;
    if (drawn > 0) {
        g_fighter.counts.frames++;
    }
}

static s32 port_GlFighterWritePosesNow(
    const char *folder, const PortFighterView *views);

/* The poses of the frame that begins, and the game's own projected corners
 * of each of its fighters' primitives (--gl-fighter-pose): what the
 * exporter's reading of a model is held to. A frame's worth is written,
 * from the first primitive of the frame after the one asked at. */
static void port_GlFighterDumpFrame(void) {
    char path[700];

    if (g_fighter.dump != NULL) {
        fclose(g_fighter.dump);
        g_fighter.dump = NULL;
    }
    if (!g_fighter.dump_armed) {
        return;
    }
    g_fighter.dump_armed = FALSE;
    port_GlFighterWritePosesNow(g_fighter.dump_folder, g_fighter.latest);
    snprintf(path, sizeof(path), "%s/prims.txt", g_fighter.dump_folder);
    g_fighter.dump = fopen(path, "w");
}

static void port_GlFighterDumpPrim(
    const PortRemasterPrim *prim, const PortFighterView *view, s32 slot) {
    s32 i;

    fprintf(g_fighter.dump, "%d %ld %d %d %d", slot,
            (long)(prim->id.group - view->body_mesh), prim->id.face,
            prim->id.piece, prim->corner_count);
    for (i = 0; i < prim->corner_count; i++) {
        fprintf(g_fighter.dump, " %.3f %.3f %d",
                (double)(prim->corner[i].x - prim->offset_x * 65536) / 65536.0,
                (double)(prim->corner[i].y - prim->offset_y * 65536) / 65536.0,
                prim->corner[i].depth);
    }
    fprintf(g_fighter.dump, "\n");
}

/* The weapon of the frame the primitive is a drawing of, by its node, or
 * -1. A weapon's node is made afresh in each frame's buffer
 * (weapon_Queue), so its address names it within the frame. */
static s32 port_GlFighterWeaponIndexOf(
    const PortRemasterPrim *prim, const PortFighterWeapon *weapons, s32 count) {
    s32 i;

    for (i = 0; i < count; i++) {
        if (prim->id.object == weapons[i].node) {
            return i;
        }
    }
    return -1;
}

/* A primitive of the tiny model's body (port_GetFighterTinyView) where a
 * file of it is drawn instead: left out, and the model drawn into a layer
 * of its own at the first of them, held to nothing of the scene, as the
 * game draws the figure after it. Its weapon is the game's. */
static s32 port_GlFighterTakesTiny(const PortRemasterPrim *prim) {
    const PortFighterView *views =
        g_fighter.tween_phase >= 0 ? g_fighter.tiny_before
                                   : g_fighter.tiny_latest;
    const PortFighterView *view = &views[0];

    if (view->model_id < 0 || prim->id.object < view->nodes_from ||
        prim->id.object >= view->nodes_to ||
        !port_FighterIsTinyBody(prim->id.object) ||
        port_GlFighterTinyModel(view->model_id) == NULL) {
        return FALSE;
    }
    if (!g_fighter.is_tiny_drawn) {
        g_fighter.is_tiny_drawn = TRUE;
        port_GlFlushBatch();
        port_GlFighterDrawAll(
            prim, LAYER_TINY, views,
            g_fighter.tween_phase >= 0 ? g_fighter.tiny_latest : NULL,
            g_fighter.tween_phase >= 0
                ? (double)g_fighter.tween_phase / (double)(1 << 16)
                : 0.0,
            NULL, 0, NULL, 0);
    }
    g_fighter.counts.left_out++;
    return TRUE;
}

s32 port_GlFighterTakes(const PortRemasterPrim *prim) {
    const PortFighterView *views;
    const PortFighterView *after = NULL;
    const PortFighterWeapon *weapons;
    const PortFighterWeapon *after_weapons = NULL;
    s32 weapon_count;
    s32 after_weapon_count = 0;
    const FighterModel *model = NULL;
    s32 place = 0;
    s32 is_cloth;
    s32 slot;
    s32 s;

    g_fighter.holds = FALSE;
    if (!g_fighter.is_on || !g_fighter.is_started || prim->id.object == 0 ||
        prim->is_shadow) {
        return FALSE;
    }
    if (g_fighter.tween_phase >= 0) {
        /* a picture between the frame on show and the one after it: the
         * primitives are the frame on show's */
        views = g_fighter.before;
        after = g_fighter.latest;
        weapons = g_fighter.before_weapon;
        weapon_count = g_fighter.before_weapons;
        after_weapons = g_fighter.latest_weapon;
        after_weapon_count = g_fighter.latest_weapons;
    } else {
        views = g_fighter.latest;
        weapons = g_fighter.latest_weapon;
        if (g_fighter.latest_serial != port_FighterSerial()) {
            /* the first primitive of a frame the game has built: its
             * views become the latest, and the latest the one before */
            const PortFighterWeapon *built;

            memcpy(
                g_fighter.before, g_fighter.latest, sizeof(g_fighter.before));
            memcpy(g_fighter.before_weapon, g_fighter.latest_weapon,
                   sizeof(g_fighter.before_weapon));
            g_fighter.before_weapons = g_fighter.latest_weapons;
            for (s = 0; s < PORT_FIGHTER_SLOTS; s++) {
                g_fighter.latest[s] = *port_GetFighterView(s);
            }
            built = port_GetFighterWeapons(&g_fighter.latest_weapons);
            memcpy(g_fighter.latest_weapon, built,
                   (size_t)g_fighter.latest_weapons * sizeof(*built));
            memcpy(g_fighter.tiny_before, g_fighter.tiny_latest,
                   sizeof(g_fighter.tiny_before));
            g_fighter.tiny_latest[0] = *port_GetFighterTinyView();
            g_fighter.latest_serial = port_FighterSerial();
            port_GlFighterDumpFrame();
        }
        weapon_count = g_fighter.latest_weapons;
    }
    if (port_GlFighterTakesTiny(prim)) {
        return TRUE;
    }
    slot = port_GlFighterSlotOf(prim, views);
    is_cloth = slot >= 0 && port_FighterIsCloth(prim->id.object);
    if (is_cloth) {
        /* A cloth whose model's file has it is drawn smooth in the layer
         * (gl_cloth.h) and its faces left out. Any other is the game's
         * own (port_fighter.h), held against the layer's depth where its
         * fighter is drawn from a model. So the layer is drawn before the
         * first cloth as well: a cloth the table painted before the
         * models would otherwise be painted over by them wherever they
         * lie, in front of it or not (the long robes: 646 of 16,384 cloth
         * faces in port_game's fight of ROSTER_VICTIM against
         * ROSTER_LAST_2). */
        model = port_GlFighterModel(views[slot].model_id);
        if (model == NULL) {
            return FALSE;
        }
        g_fighter.holds =
            port_GlFighterClothOfNode(model, &views[slot], prim->id.object) < 0;
    }
    if (!g_fighter.holds) {
        if (slot >= 0 && !is_cloth && g_fighter.dump != NULL) {
            port_GlFighterDumpPrim(prim, &views[slot], slot);
        }
        if (slot >= 0) {
            model = port_GlFighterModel(views[slot].model_id);
            place = slot;
            s = views[slot].model_id;
        } else {
            s32 i = port_GlFighterWeaponIndexOf(prim, weapons, weapon_count);

            if (i >= 0) {
                model = port_GlFighterWeaponModel(weapons[i].weapon);
                place = PORT_FIGHTER_SLOTS + weapons[i].weapon;
                s = weapons[i].weapon;
            }
        }
        if (model == NULL) {
            return FALSE;
        }
        if (prim->is_textured && prim->texel_bits != PORT_REMASTER_DEPTH_15) {
            port_GlFighterSawClut(place, s, prim->clut_x, prim->clut_y);
            port_GlFighterFindPlace(place, model);
        }
    }
    if (!g_fighter.is_drawn) {
        /* drawn where the first of their primitives would have been:
         * what was painted before is painted first */
        g_fighter.is_drawn = TRUE;
        port_GlFlushBatch();
        port_GlFighterDrawAll(
            prim, LAYER_FIGHTERS, views, after,
            g_fighter.tween_phase >= 0
                ? (double)g_fighter.tween_phase / (double)(1 << 16)
                : 0.0,
            weapons, weapon_count, after_weapons, after_weapon_count);
    }
    if (g_fighter.holds) {
        /* the cloth itself is painted by the game's own path */
        return FALSE;
    }
    g_fighter.counts.left_out++;
    return TRUE;
}

s32 port_GlFighterHolds(void) { return g_fighter.holds && g_fighter.is_laid; }

s32 port_GlFighterLayerDepth(s32 rect[4]) {
    if (!g_fighter.is_laid || g_fighter.laid[2] <= 0) {
        return 0;
    }
    rect[0] = g_fighter.laid[0];
    rect[1] = g_fighter.laid[1];
    rect[2] = g_fighter.laid[2];
    rect[3] = g_fighter.laid[3];
    return (s32)g_fighter.layer[LAYER_FIGHTERS].depth_value;
}

/* ---- the shadows cast from the models (gl_fighter.h) ---- */

void port_GlFighterDrawOccluders(u32 framebuffer) {
    PortGlPicture picture;

    if (!g_fighter.is_laid || g_fighter.laid[2] <= 0) {
        return;
    }
    port_GlGetPicture(&picture);
    glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
    glViewport(0, 0, picture.target_w, picture.target_h);
    glUseProgram(g_fighter.composite_program);
    glActiveTexture(GL_TEXTURE0 + 2);
    glBindTexture(GL_TEXTURE_2D, g_fighter.layer[LAYER_FIGHTERS].color);
    glActiveTexture(GL_TEXTURE0 + 3);
    glBindTexture(GL_TEXTURE_2D, g_fighter.layer[LAYER_FIGHTERS].depth_value);
    glActiveTexture(GL_TEXTURE0 + 1);
    glBindTexture(GL_TEXTURE_2D, picture.slot_texture);
    glActiveTexture(GL_TEXTURE0);
    glUniform1i(g_fighter.c_at_layer, 2);
    glUniform1i(g_fighter.c_at_layer_depth, 3);
    glUniform1i(g_fighter.c_at_ground_slots, 1);
    /* Every pixel of the fighter, the ground's slots not asked: where his
     * soles stand on the ground the slot of the face of the ground and his
     * own are a step apart, and which wins changes from one picture to the
     * next as he breathes. Darkened there, that is a flicker over his
     * feet; kept out of the darkening, it is a sliver of ground the size
     * of a sole's edge left as lit as the sole. */
    glUniform1i(g_fighter.c_at_ground_order, 0);
    glUniform2f(g_fighter.c_at_origin, (GLfloat)g_fighter.laid[0],
                (GLfloat)g_fighter.laid[1]);
    glUniform4f(g_fighter.c_at_rect, (GLfloat)g_fighter.laid[0],
                (GLfloat)g_fighter.laid[1], (GLfloat)g_fighter.laid[2],
                (GLfloat)g_fighter.laid[3]);
    glUniform2f(g_fighter.c_at_target, (GLfloat)picture.target_w,
                (GLfloat)picture.target_h);
    glUniform1f(g_fighter.c_at_depth_range, PORT_GL_DEPTH_RANGE);
    /* the alpha alone, as the game's own faces mark it (gl_draw.c,
     * port_GlDrawOccluders) */
    glDisable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);
    glDisable(GL_BLEND);
    glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_TRUE);
    glBindVertexArray(g_fighter.quad_array);
    glDrawArrays(GL_TRIANGLES, 0, 6);
    glBindVertexArray(0);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glBindFramebuffer(GL_FRAMEBUFFER, picture.framebuffer);
    port_GlForgetState();
}

s32 port_GlFighterCaster(PortAddress object) {
    s32 slot;
    s32 i;

    if (!port_GlFighterIsOn() || object == 0) {
        return -1;
    }
    for (slot = 0; slot < PORT_FIGHTER_SLOTS; slot++) {
        const PortFighterView *view = &g_fighter.latest[slot];

        if (view->model_id >= 0 && object >= view->nodes_from &&
            object < view->nodes_to) {
            const FighterModel *model = port_GlFighterModel(view->model_id);

            /* a cloth drawn smooth casts with the body, after his
             * triangles (port_GlFighterCastTriangles); one that is the
             * game's own casts its own faces */
            if (port_FighterIsCloth(object) &&
                port_GlFighterClothOfNode(model, view, object) < 0) {
                return -1;
            }
            return model != NULL ? slot : -1;
        }
    }
    for (i = 0; i < g_fighter.latest_weapons; i++) {
        if (g_fighter.latest_weapon[i].node == object) {
            return port_GlFighterWeaponModel(
                       g_fighter.latest_weapon[i].weapon) != NULL
                       ? PORT_FIGHTER_SLOTS + i
                       : -1;
        }
    }
    return -1;
}

/* A fighter's smooth cloths as the frame drew them, each triangle in the
 * world's units given to `emit`, numbered on from `first`. How many. */
static s32 port_GlFighterCastCloths(
    const FighterModel *model, const PortFighterView *view, s32 slot, s32 first,
    PortGlFighterEmit emit, void *context) {
    const PortGlClothBody *body = NULL;
    FighterPose pose;
    s32 count = 0;
    s32 k;

    /* in the world's own units: no camera taken off */
    port_GlFighterPoseOf(&pose, view, NULL, 0.0);
    pose.camera[0] = 0.0;
    pose.camera[1] = 0.0;
    pose.camera[2] = 0.0;

    for (k = 0; k < view->cloth_count; k++) {
        const FighterCloth *cloth =
            port_GlFighterClothFor(model, &view->cloth[k]);
        const PortGlClothVertex *vertices;
        const u32 *index;
        s32 i;

        if (cloth == NULL) {
            continue;
        }
        if (body == NULL) {
            body = port_GlFighterBody(model, &pose, slot);
        }
        vertices = port_GlFighterPoseCloth(
            cloth, &view->cloth[k], NULL, 0.0, pose.camera, body);
        if (vertices == NULL) {
            continue;
        }
        index = port_GlClothIndices(cloth->surface);
        for (i = 0; i < port_GlClothTriangles(cloth->surface); i++) {
            float p[3][3];
            s32 c;

            for (c = 0; c < 3; c++) {
                memcpy(
                    p[c], vertices[index[i * 3 + c]].place, 3 * sizeof(float));
            }
            emit(context, p, first + count++);
        }
    }
    return count;
}

s32 port_GlFighterCastTriangles(
    s32 caster, PortGlFighterEmit emit, void *context) {
    static float *places;
    static s32 room;
    const FighterModel *model;
    FighterPose pose;
    s32 i;

    if (caster < PORT_FIGHTER_SLOTS) {
        const PortFighterView *view = &g_fighter.latest[caster];

        model = port_GlFighterModel(view->model_id);
        if (model == NULL) {
            return 0;
        }
        port_GlFighterPoseOf(&pose, view, NULL, 0.0);
    } else {
        const PortFighterWeapon *weapon =
            &g_fighter.latest_weapon[caster - PORT_FIGHTER_SLOTS];
        double rotation[9];

        model = port_GlFighterWeaponModel(weapon->weapon);
        if (model == NULL) {
            return 0;
        }
        port_GlFighterMixRotation(
            rotation, weapon->world.m, weapon->world.m, 0.0);
        for (i = 0; i < PORT_FIGHTER_BONES; i++) {
            s32 k;

            memcpy(pose.bone_rotation[i], rotation, sizeof(rotation));
            for (k = 0; k < 3; k++) {
                pose.bone_place[i][k] = (double)weapon->world.t[k];
            }
        }
    }
    if (model->vertices == NULL) {
        return 0;
    }
    if (room < model->vertex_count) {
        float *more =
            realloc(places, (size_t)model->vertex_count * 3 * sizeof(float));

        if (more == NULL) {
            return 0;
        }
        places = more;
        room = model->vertex_count;
    }
    /* each vertex as the card places it: the sum of its terms */
    for (i = 0; i < model->vertex_count; i++) {
        const PortFighterVertex *v = &model->vertices[i];
        double p[3] = {0.0, 0.0, 0.0};
        s32 j;
        s32 k;

        for (j = 0; j < PORT_FIGHTER_TERMS; j++) {
            const double *r = pose.bone_rotation[v->term_joint[j]];
            const double *t = pose.bone_place[v->term_joint[j]];
            const float *term = v->term[j];

            if (term[0] == 0.0f && term[1] == 0.0f && term[2] == 0.0f &&
                term[3] == 0.0f) {
                continue;
            }
            for (k = 0; k < 3; k++) {
                p[k] += r[k * 3] * term[0] + r[k * 3 + 1] * term[1] +
                        r[k * 3 + 2] * term[2] + t[k] * term[3];
            }
        }
        for (k = 0; k < 3; k++) {
            places[i * 3 + k] = (float)p[k];
        }
    }
    for (i = 0; i < model->triangles; i++) {
        float p[3][3];
        s32 c;

        for (c = 0; c < 3; c++) {
            memcpy(p[c], places + model->indices[i * 3 + c] * 3,
                   3 * sizeof(float));
        }
        emit(context, p, i);
    }
    if (caster < PORT_FIGHTER_SLOTS) {
        return model->triangles + port_GlFighterCastCloths(
                                      model, &g_fighter.latest[caster], caster,
                                      model->triangles, emit, context);
    }
    return model->triangles;
}

void port_GlFighterTween(s32 phase) {
    g_fighter.tween_phase = phase;
    g_fighter.is_drawn = FALSE;
    g_fighter.is_tiny_drawn = FALSE;
    /* a new picture has no layer yet; the end of one keeps its layer
     * until the batch is drawn (port_GlFinishFrame) */
    if (phase >= 0) {
        g_fighter.is_laid = FALSE;
    }
}

/* ---- the poses, for the exporter ---- */

static s32 port_GlFighterWritePosesNow(
    const char *folder, const PortFighterView *views) {
    s32 written = 0;
    s32 slot;

    for (slot = 0; slot < PORT_FIGHTER_SLOTS; slot++) {
        const PortFighterView *view = &views[slot];
        const char *name;
        char path[700];
        FILE *f;
        s32 i;
        s32 k;

        if (view == NULL || view->model_id < 0) {
            continue;
        }
        name = port_GlFighterFileName(view->model_id);
        if (name == NULL) {
            continue;
        }
        snprintf(path, sizeof(path), "%s/%s.pose.json", folder, name);
        f = fopen(path, "w");
        if (f == NULL) {
            continue;
        }
        fprintf(f, "{\"model_id\": %d, \"rotation\": [", view->model_id);
        for (i = 0; i < PORT_FIGHTER_BONES; i++) {
            fprintf(f, "%s[", i ? ", " : "");
            for (k = 0; k < 9; k++) {
                fprintf(f, "%s%d", k ? ", " : "", view->bone[i].m[k]);
            }
            fprintf(f, "]");
        }
        fprintf(f, "], \"translation\": [");
        for (i = 0; i < PORT_FIGHTER_BONES; i++) {
            fprintf(f, "%s[%d, %d, %d]", i ? ", " : "", view->bone[i].t[0],
                    view->bone[i].t[1], view->bone[i].t[2]);
        }
        fprintf(f, "], \"view\": [");
        for (k = 0; k < 9; k++) {
            fprintf(f, "%s%d", k ? ", " : "", view->view[k]);
        }
        fprintf(f,
                "], \"camera\": [%d, %d, %d], \"h\": %d, \"ofx\": %d, "
                "\"ofy\": %d, \"scale_shift\": %d}\n",
                view->camera[0], view->camera[1], view->camera[2], view->h,
                view->ofx, view->ofy, view->scale_shift);
        fclose(f);
        written++;
    }
    return written;
}

s32 port_GlFighterWritePoses(const char *folder) {
    snprintf(
        g_fighter.dump_folder, sizeof(g_fighter.dump_folder), "%s", folder);
    g_fighter.dump_armed = TRUE;
    return 0;
}
