/* The fighters' shadows in the remastered picture (gl_shadow.h;
 * docs/PORT.md, "Remastered", "The shadows").
 *
 * The game paints a shadow as opaque black polygons: the caster's mesh
 * flattened onto the ground. Here those polygons are not painted at
 * all. Their silhouette is drawn into a mask of the colour buffer's own
 * resolution -- coverage and nothing else, so that a face over a face
 * is one darkness and not two -- and at the frame's end the mask
 * darkens what it falls on by a factor.
 *
 * The same mask carries, in its alpha, whether something that is not
 * the stage's ground painted that pixel: those pixels are not touched.
 * So a fighter standing in its own shadow is exactly as lit as it is
 * with the setting off, and the darkening lies on the ground alone. */
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#include "common.h"
#include "gl.h"
#include "gl_fighter.h"
#include "gl_fighter_card.h"
#include "gl_shadow.h"
#include "gl_stage.h"
#include "gl_tween.h"
#include "port_fighter.h"
#include "port_gpu.h"
#include "port_profile.h"
#include "port_remaster.h"

/* the game's own header, for the size of a tile's record: which
 * primitives are the stage's ground (gl_stage.c does the same) */
#include "stage.h"

/* ---- GL, as much of it as this file uses ---- */

typedef unsigned int GLenum;
typedef unsigned char GLboolean;
typedef unsigned int GLbitfield;
typedef int GLint;
typedef unsigned int GLuint;
typedef int GLsizei;
typedef float GLfloat;
typedef char GLchar;
typedef ptrdiff_t GLsizeiptr;

#define GL_FALSE 0
#define GL_TRUE 1
#define GL_TRIANGLES 0x0004
#define GL_FLOAT 0x1406
#define GL_RGBA 0x1908
#define GL_RGBA8 0x8058
#define GL_UNSIGNED_BYTE 0x1401
#define GL_TEXTURE_2D 0x0DE1
#define GL_TEXTURE0 0x84C0
#define GL_TEXTURE1 0x84C1
#define GL_TEXTURE_MIN_FILTER 0x2801
#define GL_TEXTURE_MAG_FILTER 0x2800
#define GL_TEXTURE_WRAP_S 0x2802
#define GL_TEXTURE_WRAP_T 0x2803
#define GL_NEAREST 0x2600
#define GL_LINEAR 0x2601
#define GL_CLAMP_TO_EDGE 0x812F
#define GL_FRAMEBUFFER 0x8D40
#define GL_COLOR_ATTACHMENT0 0x8CE0
#define GL_FRAMEBUFFER_COMPLETE 0x8CD5
#define GL_COLOR_BUFFER_BIT 0x00004000
#define GL_ARRAY_BUFFER 0x8892
#define GL_STREAM_DRAW 0x88E0
#define GL_VERTEX_SHADER 0x8B31
#define GL_FRAGMENT_SHADER 0x8B30
#define GL_COMPILE_STATUS 0x8B81
#define GL_LINK_STATUS 0x8B82
#define GL_BLEND 0x0BE2
#define GL_SCISSOR_TEST 0x0C11
#define GL_ZERO 0
#define GL_SRC_COLOR 0x0300
#define GL_FUNC_ADD 0x8006
#define GL_UNSIGNED_INT 0x1405
#define GL_SHADER_STORAGE_BUFFER 0x90D2

#define PORT_GL_SHADOW_FUNCTIONS                                               \
    GL(void, glViewport, (GLint, GLint, GLsizei, GLsizei))                     \
    GL(void, glScissor, (GLint, GLint, GLsizei, GLsizei))                      \
    GL(void, glEnable, (GLenum))                                               \
    GL(void, glDisable, (GLenum))                                              \
    GL(void, glClearColor, (GLfloat, GLfloat, GLfloat, GLfloat))               \
    GL(void, glClear, (GLbitfield))                                            \
    GL(void, glColorMask, (GLboolean, GLboolean, GLboolean, GLboolean))        \
    GL(void, glBlendFunc, (GLenum, GLenum))                                    \
    GL(void, glBlendEquation, (GLenum))                                        \
    GL(void, glGenTextures, (GLsizei, GLuint *))                               \
    GL(void, glBindTexture, (GLenum, GLuint))                                  \
    GL(void, glActiveTexture, (GLenum))                                        \
    GL(void, glTexParameteri, (GLenum, GLenum, GLint))                         \
    GL(void, glTexImage2D,                                                     \
       (GLenum, GLint, GLint, GLsizei, GLsizei, GLint, GLenum, GLenum,         \
        const void *))                                                         \
    GL(void, glGenFramebuffers, (GLsizei, GLuint *))                           \
    GL(void, glBindFramebuffer, (GLenum, GLuint))                              \
    GL(void, glFramebufferTexture2D, (GLenum, GLenum, GLenum, GLuint, GLint))  \
    GL(GLenum, glCheckFramebufferStatus, (GLenum))                             \
    GL(void, glGenBuffers, (GLsizei, GLuint *))                                \
    GL(void, glBindBuffer, (GLenum, GLuint))                                   \
    GL(void, glBufferData, (GLenum, GLsizeiptr, const void *, GLenum))         \
    GL(void, glGenVertexArrays, (GLsizei, GLuint *))                           \
    GL(void, glBindVertexArray, (GLuint))                                      \
    GL(void, glVertexAttribPointer,                                            \
       (GLuint, GLint, GLenum, GLboolean, GLsizei, const void *))              \
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
    GL(void, glUniform4f, (GLint, GLfloat, GLfloat, GLfloat, GLfloat))         \
    GL(void, glUniform3f, (GLint, GLfloat, GLfloat, GLfloat))                  \
    GL(void, glUniformMatrix3fv, (GLint, GLsizei, GLboolean, const GLfloat *)) \
    GL(void, glReadPixels,                                                     \
       (GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, void *))               \
    GL(void, glDrawArrays, (GLenum, GLint, GLsizei))                           \
    GL(void, glDrawElements, (GLenum, GLsizei, GLenum, const void *))         \
    GL(void, glUniform1iv, (GLint, GLsizei, const GLint *))                    \
    GL(void, glUniformMatrix4fv, (GLint, GLsizei, GLboolean, const GLfloat *)) \
    GL(void, glBindBufferBase, (GLenum, GLuint, GLuint))

#ifndef GL_GEOMETRY_SHADER
#define GL_GEOMETRY_SHADER 0x8DD9
#endif

#define GL(result, name, args) static result(*name) args;
PORT_GL_SHADOW_FUNCTIONS
#undef GL

/* ---- what a frame may hold ---- */

/* The silhouette's triangles. The game's shadow meshes of two fighters,
 * their weapons and the props that cast one come to a few dozen faces (a
 * whole fight's worst was measured at 105 primitives, docs/PORT.md); the
 * fighters' own faces, cast, to a few thousand. */
#define TRIANGLES_MAX 16384
#define POINTS_MAX (3 * TRIANGLES_MAX)
/* The things drawn from models of their own that a frame casts: the
 * fighters and the weapons (gl_fighter.h). Their triangles never come to
 * the processor: the card poses and casts them (port_GlShadowDrawModels). */
#define MODEL_CASTS (PORT_FIGHTER_SLOTS + PORT_FIGHTER_WEAPONS)
/* Writes of video memory into the picture after the shadows, which cover
 * them: a frame has none in a fight. More than this are taken together
 * as the one rectangle holding them all. */
#define COVERS_MAX 8

/* A corner of the silhouette, as the picture places it. */
typedef struct {
    GLfloat x;
    GLfloat y;
    GLfloat w;
} ShadowPoint;

typedef struct {
    s32 left;
    s32 top;
    s32 right; /* the last pixel, not one past it */
    s32 bottom;
} ShadowBox;

/* `shadows cast`: a triangle of a fighter's face flattened onto the
 * ground, its corners in the world's own units, and which face of which
 * mesh it is, by which a frame's triangle is found again in the next. */
typedef struct {
    float p[3][3];
    PortAddress group;
    s32 face;
    s32 part; /* the first or the second half of a quad */
} CastTriangle;

/* A thing drawn from a model of its own as a frame cast it: what the
 * card casts it from, the ground it falls on (or its own lowest point,
 * which the card has), and the sun's slope. */
typedef struct {
    s32 caster;
    PortGlFighterCast cast;
    float ground_y;
    s32 is_on_feet;
    float x_shear;
    float z_shear;
} ModelCast;

/* One frame's: the game's own faces as triangles, the things cast from
 * models, and the view its picture was drawn with. */
typedef struct {
    s32 is_valid;
    s32 count;
    PortStageView view;
    CastTriangle *triangle;
    s32 model_count;
    s32 model_triangles;
    ModelCast model[MODEL_CASTS];
} CastFrame;

/* How near the camera a corner of a cast shadow may come, in the world's
 * units, before its triangle is left out: a shadow lies on the ground in
 * front of the camera, and this keeps the divide away from 0. */
#define CAST_NEAR 32.0
/* How far the pictures between two frames may cast outside the box of
 * the two frames' own silhouettes, in the console's pixels. */
#define CAST_BOX_GROW 8

static struct {
    s32 is_started;
    s32 strength; /* hundredths; 0 is the game's own black shadows */
    s32 is_cast;  /* cast from the fighters' own faces */
    s32 phase;    /* of a picture between two frames, or -1 */
    char problem[256];
    /* the mask: red the shadows' coverage, alpha what is not the ground */
    GLuint mask;
    GLuint framebuffer;
    s32 mask_w;
    s32 mask_h;
    s32 has_mask;
    GLuint silhouette; /* the program that draws the silhouettes */
    GLuint dark;       /* ... and the one that darkens the picture */
    GLuint buffer;
    GLuint array;
    GLuint quad_buffer;
    GLuint quad_array;
    GLint at_target;
    GLint at_scale;
    GLint dark_at_target;
    GLint dark_at_rect;
    GLint dark_at_mask;
    GLint dark_at_radius;
    GLint dark_at_strength;
    GLint dark_at_show;
    s32 is_shown; /* the mask drawn in the picture's place */
    ShadowPoint *point;
    s32 count;
    s32 lost; /* triangles a frame had no room for */
    /* this frame */
    s32 has_any;
    ShadowBox box;  /* the silhouettes, in the colour buffer's pixels */
    ShadowBox clip; /* ... and the drawing area they belong to */
    s32 cover_count;
    ShadowBox cover[COVERS_MAX];
    s32 offset_x; /* the drawing offset of the picture's primitives */
    s32 offset_y;
    /* the last two frames cast, the newer of them, and the older; -1 for
     * none */
    CastFrame cast[2];
    s32 newest;
    s32 older;
    /* The cast triangles as the card projects them (port_GlShadowSendPair):
     * a vertex is the frame on show's corner and the frame after it's,
     * in the world's units, sent once a pair of frames and not once a
     * picture; what the frames were (each frame's number, `gathered`), and
     * the box the pair's silhouettes lie in, at either end. */
    GLuint cast_program;
    GLuint cast_buffer;
    GLuint cast_array;
    GLint cast_at_t;
    GLint cast_at_rotation;
    GLint cast_at_camera;
    GLint cast_at_project;
    GLint cast_at_target;
    GLint cast_at_scale;
    GLint cast_at_kind;
    GLint cast_at_world_one;
    GLint cast_at_world_two;
    GLint cast_at_flat_one;
    GLint cast_at_flat_two;
    /* the frames the models are cast from in this picture */
    const CastFrame *card_one;
    const CastFrame *card_two;
    u32 gathered[2];
    u32 sent_one;
    u32 sent_two;
    s32 sent_count;
    ShadowBox sent_box;
    float *staged;
    /* this picture's: whether the card projects it, and how */
    s32 is_on_card;
    GLfloat card_t;
    GLfloat card_rotation[9];
    GLfloat card_camera[3];
    GLfloat card_project[4];
    s32 cast_faces;
    s32 cast_orphans;
    u32 cast_model_triangles; /* cast from the models, the run's */
    /* frames a model's shadow was laid at its feet, the game casting none
     * of its body (port_GlShadowCast), the run's */
    u32 cast_feet_grounds;
    /* faces of the stage's pieces taken for floor (port_GlShadowIsFloor),
     * the run's */
    u32 floor_faces;
    s32 cast_triangles;
    u32 cast_fallbacks;
    u32 cast_unpaired;
    /* what the last frame did, and the run */
    s32 shown_occluders;
    s32 shown_faces;
    s32 run_lost;
    u32 widest_area;
} g_shadow;

/* ---- the shaders ---- */

/* The silhouette: the same transform as the picture's own vertex shader,
 * so that the mask's coverage is the pixels the picture would have
 * painted black, to the sample. */
static const char g_shadow_vertex_text[] =
    "#version 330 core\n"
    "layout(location = 0) in vec3 a_place;\n" /* x, y in video memory's
                                               * pixels; z is w */
    "uniform vec2 u_target;\n"
    "uniform vec2 u_scale;\n"
    "void main() {\n"
    "    vec2 at = a_place.xy * u_scale + vec2(0.5);\n"
    "    vec2 ndc = at / u_target * 2.0 - vec2(1.0);\n"
    "    float w = a_place.z;\n"
    "    gl_Position = vec4(ndc.x * w, ndc.y * w, 0.0, w);\n"
    "}\n";

/* The cast triangles as the card places them: each corner `u_t` of the
 * way from the frame on show's place to the frame after it's, seen through
 * the picture's rotation and projected as port_GlShadowProject projects
 * a point (u_project: H, the two offsets, and the nearest a point may
 * be); the geometry shader leaves out a triangle with a corner nearer
 * than that, as port_GlShadowCast did. */
static const char g_shadow_cast_vertex_text[] =
    "#version 430 core\n"
    /* the game's own faces: each corner of the two frames, slid onto the
     * ground already */
    "layout(location = 0) in vec3 a_one;\n"
    "layout(location = 1) in vec3 a_two;\n"
    /* a model's vertex: its terms on the bones, as the fighters' shader
     * places it (gl_fighter.c) */
    "layout(location = 5) in vec4 a_term[8];\n"
    "layout(location = 13) in uvec4 a_term_joint;\n"
    "layout(location = 14) in uvec4 a_term_joint1;\n"
    /* a smooth cloth's vertices as the card kept them out of the body
     * (PortGlClothVertex), and the bodies' lowest points */
    "layout(std430, binding = 0) readonly buffer ClothOne { float c_one[]; };\n"
    "layout(std430, binding = 1) readonly buffer ClothTwo { float c_two[]; };\n"
    "layout(std430, binding = 2) readonly buffer FeetOne { int f_one[]; };\n"
    "layout(std430, binding = 3) readonly buffer FeetTwo { int f_two[]; };\n"
    /* 0: the faces; 1: a model; 2: a smooth cloth */
    "uniform int u_kind;\n"
    "uniform mat4 u_world_one[21];\n"
    "uniform mat4 u_world_two[21];\n"
    /* the ground, the sun's two slopes, and whether the ground is the
     * body's lowest point */
    "uniform vec4 u_flat_one;\n"
    "uniform vec4 u_flat_two;\n"
    "uniform float u_t;\n"
    "uniform mat3 u_rotation;\n"
    "uniform vec3 u_camera;\n"
    "uniform vec4 u_project;\n"
    "uniform vec2 u_target;\n"
    "uniform vec2 u_scale;\n"
    "out float v_ok;\n"
    PORT_GL_CARD_FEET_GLSL
    /* light_ProjectShadow's arithmetic: x and z move by the height above
     * the ground times the sun's slope, and y becomes the ground's */
    "float ground_one() {\n"
    "    return u_flat_one.w > 0.5 ? card_feet(f_one[8193]) : u_flat_one.x;\n"
    "}\n"
    "float ground_two() {\n"
    "    return u_flat_two.w > 0.5 ? card_feet(f_two[8193]) : u_flat_two.x;\n"
    "}\n"
    "vec3 flat_of(vec3 p, vec4 flat_by, float ground) {\n"
    "    float above = p.y - ground;\n"
    "    return vec3(p.x + above * flat_by.y, ground, p.z + above * flat_by.z);\n"
    "}\n"
    "vec3 skin_one() {\n"
    "    return (u_world_one[a_term_joint.x] * a_term[0]\n"
    "          + u_world_one[a_term_joint.y] * a_term[1]\n"
    "          + u_world_one[a_term_joint.z] * a_term[2]\n"
    "          + u_world_one[a_term_joint.w] * a_term[3]\n"
    "          + u_world_one[a_term_joint1.x] * a_term[4]\n"
    "          + u_world_one[a_term_joint1.y] * a_term[5]\n"
    "          + u_world_one[a_term_joint1.z] * a_term[6]\n"
    "          + u_world_one[a_term_joint1.w] * a_term[7]).xyz;\n"
    "}\n"
    "vec3 skin_two() {\n"
    "    return (u_world_two[a_term_joint.x] * a_term[0]\n"
    "          + u_world_two[a_term_joint.y] * a_term[1]\n"
    "          + u_world_two[a_term_joint.z] * a_term[2]\n"
    "          + u_world_two[a_term_joint.w] * a_term[3]\n"
    "          + u_world_two[a_term_joint1.x] * a_term[4]\n"
    "          + u_world_two[a_term_joint1.y] * a_term[5]\n"
    "          + u_world_two[a_term_joint1.z] * a_term[6]\n"
    "          + u_world_two[a_term_joint1.w] * a_term[7]).xyz;\n"
    "}\n"
    "void main() {\n"
    "    vec3 one = a_one;\n"
    "    vec3 two = a_two;\n"
    "    if (u_kind == 1) {\n"
    "        one = flat_of(skin_one(), u_flat_one, ground_one());\n"
    "        two = u_t > 0.0 ? flat_of(skin_two(), u_flat_two, ground_two())\n"
    "                        : one;\n"
    "    } else if (u_kind == 2) {\n"
    "        int b = gl_VertexID * 8;\n"
    "        one = flat_of(vec3(c_one[b], c_one[b + 1], c_one[b + 2]),\n"
    "                      u_flat_one, ground_one());\n"
    "        two = u_t > 0.0\n"
    "            ? flat_of(vec3(c_two[b], c_two[b + 1], c_two[b + 2]),\n"
    "                      u_flat_two, ground_two())\n"
    "            : one;\n"
    "    }\n"
    "    vec3 m = one + (two - one) * u_t;\n"
    "    vec3 v = u_rotation * (m - u_camera);\n"
    "    v_ok = v.z >= u_project.w ? 1.0 : 0.0;\n"
    "    float z = max(v.z, 1.0);\n"
    "    vec2 p = vec2(u_project.y + u_project.x * v.x / z,\n"
    "                  u_project.z + u_project.x * 0.5 * v.y / z);\n"
    "    vec2 at = p * u_scale + vec2(0.5);\n"
    "    gl_Position = vec4(at / u_target * 2.0 - vec2(1.0), 0.0, 1.0);\n"
    "}\n";

static const char g_shadow_cast_geometry_text[] =
    "#version 430 core\n"
    "layout(triangles) in;\n"
    "layout(triangle_strip, max_vertices = 3) out;\n"
    "in float v_ok[];\n"
    "void main() {\n"
    "    if (v_ok[0] < 0.5 || v_ok[1] < 0.5 || v_ok[2] < 0.5) {\n"
    "        return;\n"
    "    }\n"
    "    for (int i = 0; i < 3; i++) {\n"
    "        gl_Position = gl_in[i].gl_Position;\n"
    "        EmitVertex();\n"
    "    }\n"
    "    EndPrimitive();\n"
    "}\n";

/* Coverage, and nothing else: a face over a face writes the same 1, so
 * where two shadows overlap the darkness is one shadow's. The alpha is
 * left clear here and is the occluders' to set. */
static const char g_shadow_fragment_text[] =
    "#version 330 core\n"
    "out vec4 o_mask;\n"
    "void main() { o_mask = vec4(1.0, 0.0, 0.0, 0.0); }\n";

/* A rectangle of the colour buffer, in its own pixels. */
static const char g_shadow_quad_text[] =
    "#version 330 core\n"
    "layout(location = 0) in vec2 a_corner;\n" /* 0 or 1, each way */
    "uniform vec4 u_rect;\n"
    "uniform vec2 u_target;\n"
    "void main() {\n"
    "    vec2 at = u_rect.xy + a_corner * u_rect.zw;\n"
    "    vec2 ndc = at / u_target * 2.0 - vec2(1.0);\n"
    "    gl_Position = vec4(ndc, 0.0, 1.0);\n"
    "}\n";

/* The darkening. The pixel's own alpha says whether the picture painted
 * something there that is not the stage's ground -- a fighter, a weapon,
 * an effect, the HUD -- and such a pixel is left exactly as it was. The
 * coverage is read as nine points of a tent spread over the penumbra,
 * each of them the average of four of the mask's own pixels (the mask is
 * filtered), which is a soft edge of `u_radius` pixels and a middle that
 * is flat. */
static const char g_shadow_dark_text[] =
    "#version 330 core\n"
    "uniform sampler2D u_mask;\n"
    "uniform float u_radius;\n"
    "uniform float u_strength;\n"
    /* the mask itself in the place of the picture, for a run that
     * measures what was darkened and what was kept out of it: red the
     * silhouettes' coverage, green what is not the stage's ground */
    "uniform int u_show;\n"
    "out vec4 o_color;\n"
    "void main() {\n"
    "    ivec2 at = ivec2(floor(gl_FragCoord.xy));\n"
    "    vec4 here = texelFetch(u_mask, at, 0);\n"
    "    if (u_show != 0) { o_color = vec4(here.r, here.a, 0.0, 1.0); return; }\n"
    "    if (here.a > 0.5) { discard; }\n"
    "    vec2 size = vec2(textureSize(u_mask, 0));\n"
    "    float spread = u_radius * 0.7;\n"
    "    float sum = 0.0;\n"
    "    float weight = 0.0;\n"
    "    for (int j = -1; j <= 1; j++) {\n"
    "        for (int i = -1; i <= 1; i++) {\n"
    "            float how = (i == 0 ? 2.0 : 1.0) * (j == 0 ? 2.0 : 1.0);\n"
    "            vec2 point = gl_FragCoord.xy\n"
    "                       + vec2(float(i), float(j)) * spread;\n"
    "            sum += how * texture(u_mask, point / size).r;\n"
    "            weight += how;\n"
    "        }\n"
    "    }\n"
    "    float cover = sum / weight;\n"
    "    o_color = vec4(vec3(1.0 - u_strength * cover), 1.0);\n"
    "}\n";

/* ---- starting ---- */

static void port_GlShadowSay(const char *what, const char *more) {
    u32 at = 0;
    u32 i;

    for (i = 0; what[i] != 0 && at + 1 < sizeof(g_shadow.problem); i++) {
        g_shadow.problem[at++] = what[i];
    }
    for (i = 0;
         more != NULL && more[i] != 0 && at + 1 < sizeof(g_shadow.problem);
         i++) {
        g_shadow.problem[at++] = more[i];
    }
    g_shadow.problem[at] = 0;
}

static GLuint port_GlShadowShader(GLenum kind, const char *text) {
    GLuint shader = glCreateShader(kind);
    GLint ok = GL_FALSE;

    glShaderSource(shader, 1, &text, NULL);
    glCompileShader(shader);
    glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        static char log[2048];

        glGetShaderInfoLog(shader, (GLsizei)sizeof(log), NULL, log);
        port_GlShadowSay("a shadow shader does not compile: ", log);
        return 0;
    }
    return shader;
}

static GLuint port_GlShadowProgram(const char *vertex, const char *fragment) {
    GLuint program;
    GLuint one = port_GlShadowShader(GL_VERTEX_SHADER, vertex);
    GLuint two = port_GlShadowShader(GL_FRAGMENT_SHADER, fragment);
    GLint ok = GL_FALSE;

    if (one == 0 || two == 0) {
        return 0;
    }
    program = glCreateProgram();
    glAttachShader(program, one);
    glAttachShader(program, two);
    glLinkProgram(program);
    glGetProgramiv(program, GL_LINK_STATUS, &ok);
    if (!ok) {
        static char log[2048];

        glGetProgramInfoLog(program, (GLsizei)sizeof(log), NULL, log);
        port_GlShadowSay("the shadow shaders do not link: ", log);
        return 0;
    }
    return program;
}

/* The same, with a geometry shader between the two. */
static GLuint port_GlShadowProgram3(
    const char *vertex, const char *geometry, const char *fragment) {
    GLuint program;
    GLuint one = port_GlShadowShader(GL_VERTEX_SHADER, vertex);
    GLuint two = port_GlShadowShader(GL_GEOMETRY_SHADER, geometry);
    GLuint three = port_GlShadowShader(GL_FRAGMENT_SHADER, fragment);
    GLint ok = GL_FALSE;

    if (one == 0 || two == 0 || three == 0) {
        return 0;
    }
    program = glCreateProgram();
    glAttachShader(program, one);
    glAttachShader(program, two);
    glAttachShader(program, three);
    glLinkProgram(program);
    glGetProgramiv(program, GL_LINK_STATUS, &ok);
    if (!ok) {
        static char log[2048];

        glGetProgramInfoLog(program, (GLsizei)sizeof(log), NULL, log);
        port_GlShadowSay("the cast shadows' shaders do not link: ", log);
        return 0;
    }
    return program;
}

const char *port_GlShadowStart(void *(*getproc)(const char *name)) {
    static const GLfloat corners[] = {
        0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f, 1.0f, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f};
    s32 i;

    if (g_shadow.is_started) {
        return NULL;
    }
    g_shadow.problem[0] = 0;
#define GL(result, name, args)                                                 \
    name = (result(*) args)getproc(#name);                                     \
    if (name == NULL) {                                                        \
        port_GlShadowSay("OpenGL has no ", #name);                             \
        return g_shadow.problem;                                               \
    }
    PORT_GL_SHADOW_FUNCTIONS
#undef GL
    if (g_shadow.point == NULL) {
        g_shadow.point = malloc(POINTS_MAX * sizeof(*g_shadow.point));
        if (g_shadow.point == NULL) {
            port_GlShadowSay("there is no memory for the shadows", NULL);
            return g_shadow.problem;
        }
    }
    for (i = 0; i < 2; i++) {
        if (g_shadow.cast[i].triangle == NULL) {
            g_shadow.cast[i].triangle =
                malloc(TRIANGLES_MAX * sizeof(CastTriangle));
            if (g_shadow.cast[i].triangle == NULL) {
                port_GlShadowSay("there is no memory for the shadows", NULL);
                return g_shadow.problem;
            }
        }
        g_shadow.cast[i].is_valid = FALSE;
        g_shadow.cast[i].count = 0;
    }
    g_shadow.newest = -1;
    g_shadow.older = -1;
    g_shadow.phase = -1;
    g_shadow.silhouette =
        port_GlShadowProgram(g_shadow_vertex_text, g_shadow_fragment_text);
    if (g_shadow.silhouette == 0) {
        return g_shadow.problem;
    }
    g_shadow.dark =
        port_GlShadowProgram(g_shadow_quad_text, g_shadow_dark_text);
    if (g_shadow.dark == 0) {
        return g_shadow.problem;
    }
    g_shadow.cast_program = port_GlShadowProgram3(
        g_shadow_cast_vertex_text, g_shadow_cast_geometry_text,
        g_shadow_fragment_text);
    if (g_shadow.cast_program == 0) {
        return g_shadow.problem;
    }
    g_shadow.cast_at_t = glGetUniformLocation(g_shadow.cast_program, "u_t");
    g_shadow.cast_at_rotation =
        glGetUniformLocation(g_shadow.cast_program, "u_rotation");
    g_shadow.cast_at_camera =
        glGetUniformLocation(g_shadow.cast_program, "u_camera");
    g_shadow.cast_at_project =
        glGetUniformLocation(g_shadow.cast_program, "u_project");
    g_shadow.cast_at_target =
        glGetUniformLocation(g_shadow.cast_program, "u_target");
    g_shadow.cast_at_scale =
        glGetUniformLocation(g_shadow.cast_program, "u_scale");
    g_shadow.cast_at_kind =
        glGetUniformLocation(g_shadow.cast_program, "u_kind");
    g_shadow.cast_at_world_one =
        glGetUniformLocation(g_shadow.cast_program, "u_world_one");
    g_shadow.cast_at_world_two =
        glGetUniformLocation(g_shadow.cast_program, "u_world_two");
    g_shadow.cast_at_flat_one =
        glGetUniformLocation(g_shadow.cast_program, "u_flat_one");
    g_shadow.cast_at_flat_two =
        glGetUniformLocation(g_shadow.cast_program, "u_flat_two");
    g_shadow.staged = malloc(TRIANGLES_MAX * 18 * sizeof(float));
    if (g_shadow.staged == NULL) {
        port_GlShadowSay("there is no memory for the shadows", NULL);
        return g_shadow.problem;
    }
    glGenBuffers(1, &g_shadow.cast_buffer);
    glGenVertexArrays(1, &g_shadow.cast_array);
    glBindVertexArray(g_shadow.cast_array);
    glBindBuffer(GL_ARRAY_BUFFER, g_shadow.cast_buffer);
    glVertexAttribPointer(
        0, 3, GL_FLOAT, GL_FALSE, (GLsizei)(6 * sizeof(float)), NULL);
    glVertexAttribPointer(
        1, 3, GL_FLOAT, GL_FALSE, (GLsizei)(6 * sizeof(float)),
        (const void *)(3 * sizeof(float)));
    glEnableVertexAttribArray(0);
    glEnableVertexAttribArray(1);
    g_shadow.sent_one = 0;
    g_shadow.sent_two = 0;
    g_shadow.at_target = glGetUniformLocation(g_shadow.silhouette, "u_target");
    g_shadow.at_scale = glGetUniformLocation(g_shadow.silhouette, "u_scale");
    g_shadow.dark_at_target = glGetUniformLocation(g_shadow.dark, "u_target");
    g_shadow.dark_at_rect = glGetUniformLocation(g_shadow.dark, "u_rect");
    g_shadow.dark_at_mask = glGetUniformLocation(g_shadow.dark, "u_mask");
    g_shadow.dark_at_radius = glGetUniformLocation(g_shadow.dark, "u_radius");
    g_shadow.dark_at_strength =
        glGetUniformLocation(g_shadow.dark, "u_strength");
    g_shadow.dark_at_show = glGetUniformLocation(g_shadow.dark, "u_show");
    glGenTextures(1, &g_shadow.mask);
    glGenFramebuffers(1, &g_shadow.framebuffer);
    glGenBuffers(1, &g_shadow.buffer);
    glGenVertexArrays(1, &g_shadow.array);
    glBindVertexArray(g_shadow.array);
    glBindBuffer(GL_ARRAY_BUFFER, g_shadow.buffer);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 0, NULL);
    glEnableVertexAttribArray(0);
    glGenBuffers(1, &g_shadow.quad_buffer);
    glGenVertexArrays(1, &g_shadow.quad_array);
    glBindVertexArray(g_shadow.quad_array);
    glBindBuffer(GL_ARRAY_BUFFER, g_shadow.quad_buffer);
    glBufferData(
        GL_ARRAY_BUFFER, (GLsizeiptr)sizeof(corners), corners, GL_STREAM_DRAW);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, NULL);
    glEnableVertexAttribArray(0);
    g_shadow.is_started = TRUE;
    return NULL;
}

static s32 port_GlShadowMakeMask(const PortGlPicture *picture);

void port_GlShadowWarm(void) {
    static const float corners[18];
    static GLuint pixel;
    static GLuint framebuffer;

    if (!g_shadow.is_started || pixel != 0) {
        return;
    }
    glGenTextures(1, &pixel);
    glBindTexture(GL_TEXTURE_2D, pixel);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 1, 1, 0, GL_RGBA,
                 GL_UNSIGNED_BYTE, NULL);
    glGenFramebuffers(1, &framebuffer);
    glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
    glFramebufferTexture2D(
        GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, pixel, 0);
    glViewport(0, 0, 1, 1);
    glDisable(GL_BLEND);
    glDisable(GL_SCISSOR_TEST);
    glUseProgram(g_shadow.cast_program);
    glBindVertexArray(g_shadow.cast_array);
    glBindBuffer(GL_ARRAY_BUFFER, g_shadow.cast_buffer);
    glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)sizeof(corners), corners,
                 GL_STREAM_DRAW);
    /* one triangle of the game's faces, every corner at nought and so
     * left out by the near plane; the models' kinds are branches of the
     * same program */
    glUniform1i(g_shadow.cast_at_kind, 0);
    glUniform1f(g_shadow.cast_at_t, 0.0f);
    glUniform4f(g_shadow.cast_at_project, 1.0f, 0.0f, 0.0f, 1.0f);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    /* ... and the darkening, of a pixel */
    glUseProgram(g_shadow.dark);
    glBindVertexArray(g_shadow.quad_array);
    glUniform2f(g_shadow.dark_at_target, 1.0f, 1.0f);
    glUniform4f(g_shadow.dark_at_rect, 0.0f, 0.0f, 1.0f, 1.0f);
    glDrawArrays(GL_TRIANGLES, 0, 6);
    glBindVertexArray(0);
    /* the mask, at the picture's size */
    {
        PortGlPicture picture;

        port_GlGetPicture(&picture);
        if (picture.target_w > 0 && picture.target_h > 0) {
            port_GlShadowMakeMask(&picture);
        }
    }
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    g_shadow.sent_one = 0;
    g_shadow.sent_two = 0;
    port_GlForgetState();
}

/* The mask, as large as the colour buffer and in the same pixels, so
 * that nothing here has a coordinate of its own. It is asked for only
 * when a frame first has a shadow in it and `shadows soft` is on. */
static s32 port_GlShadowMakeMask(const PortGlPicture *picture) {
    if (g_shadow.has_mask && picture->target_w == g_shadow.mask_w &&
        picture->target_h == g_shadow.mask_h) {
        return TRUE;
    }
    g_shadow.has_mask = FALSE;
    glBindTexture(GL_TEXTURE_2D, g_shadow.mask);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, picture->target_w,
                 picture->target_h, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    /* the coverage is read as points between its pixels (the darkening's
     * shader); the alpha is read at the pixel itself, by texelFetch,
     * which no filter touches */
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindFramebuffer(GL_FRAMEBUFFER, g_shadow.framebuffer);
    glFramebufferTexture2D(
        GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, g_shadow.mask, 0);
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
        port_GlShadowSay("the shadows' mask cannot be drawn into", NULL);
        glBindFramebuffer(GL_FRAMEBUFFER, picture->framebuffer);
        return FALSE;
    }
    glBindFramebuffer(GL_FRAMEBUFFER, picture->framebuffer);
    g_shadow.mask_w = picture->target_w;
    g_shadow.mask_h = picture->target_h;
    g_shadow.has_mask = TRUE;
    return TRUE;
}

/* ---- the setting ---- */

void port_GlShadowSetStrength(s32 setting) {
    s32 strength = setting < 0 ? 0 : (setting & PORT_SHADOWS_STRENGTH_MASK);

    g_shadow.strength = strength > 100 ? 100 : strength;
    g_shadow.is_cast = setting > 0 && (setting & PORT_SHADOWS_CAST) != 0;
}

s32 port_GlShadowStrength(void) { return g_shadow.strength; }

s32 port_GlShadowIsCast(void) {
    return port_GlShadowIsSoft() && g_shadow.is_cast;
}

void port_GlShadowSetPhase(s32 phase) {
    g_shadow.phase = phase < 0 ? -1 : phase;
}

s32 port_GlShadowCastFaces(void) { return g_shadow.cast_faces; }

s32 port_GlShadowCastOrphans(void) { return g_shadow.cast_orphans; }

s32 port_GlShadowCastTriangles(void) { return g_shadow.cast_triangles; }

u32 port_GlShadowCastFallbacks(void) { return g_shadow.cast_fallbacks; }

u32 port_GlShadowCastUnpaired(void) { return g_shadow.cast_unpaired; }

u32 port_GlShadowCastFromModels(void) { return g_shadow.cast_model_triangles; }

u32 port_GlShadowCastFeet(void) { return g_shadow.cast_feet_grounds; }

u32 port_GlShadowFloorFaces(void) { return g_shadow.floor_faces; }

s32 port_GlShadowIsSoft(void) {
    return g_shadow.is_started && g_shadow.strength > 0;
}

s32 port_GlShadowCollects(void) { return port_GlShadowIsSoft(); }

void port_GlShadowShowMask(s32 on) { g_shadow.is_shown = on != 0; }

const char *port_GlShadowProblem(void) {
    return g_shadow.problem[0] == 0 ? NULL : g_shadow.problem;
}

s32 port_GlShadowFaces(void) { return g_shadow.shown_faces; }

s32 port_GlShadowLost(void) { return g_shadow.run_lost; }

s32 port_GlShadowOccluders(void) { return g_shadow.shown_occluders; }

u32 port_GlShadowArea(void) { return g_shadow.widest_area; }

/* ---- a frame ---- */

static void port_GlShadowEmptyBox(ShadowBox *box) {
    box->left = 0;
    box->top = 0;
    box->right = -1;
    box->bottom = -1;
}

static s32 port_GlShadowBoxIsEmpty(const ShadowBox *box) {
    return box->right < box->left || box->bottom < box->top;
}

static void port_GlShadowAddToBox(ShadowBox *box, s32 x, s32 y) {
    if (port_GlShadowBoxIsEmpty(box)) {
        box->left = box->right = x;
        box->top = box->bottom = y;
        return;
    }
    box->left = x < box->left ? x : box->left;
    box->right = x > box->right ? x : box->right;
    box->top = y < box->top ? y : box->top;
    box->bottom = y > box->bottom ? y : box->bottom;
}

static void port_GlShadowHoldBox(ShadowBox *box, const ShadowBox *inside) {
    box->left = box->left > inside->left ? box->left : inside->left;
    box->top = box->top > inside->top ? box->top : inside->top;
    box->right = box->right < inside->right ? box->right : inside->right;
    box->bottom = box->bottom < inside->bottom ? box->bottom : inside->bottom;
}

static ShadowBox port_GlShadowGrownBox(const ShadowBox *box, s32 by) {
    ShadowBox out = *box;

    out.left -= by;
    out.top -= by;
    out.right += by;
    out.bottom += by;
    return out;
}

static void port_GlShadowLay(void);

/* Nothing of a picture's shadows is kept: what comes now belongs to the
 * next one. */
static void port_GlShadowForget(void) {
    /* the vertices the picture kept for the mask are the picture's own
     * (gl_draw.c) */
    port_GlForgetOccluders();
    g_shadow.count = 0;
    g_shadow.lost = 0;
    g_shadow.has_any = FALSE;
    g_shadow.is_on_card = FALSE;
    g_shadow.cover_count = 0;
    port_GlShadowEmptyBox(&g_shadow.box);
    port_GlShadowEmptyBox(&g_shadow.clip);
}

/* A frame's primitives are about to come -- and the frame before it may
 * not have been laid down yet. The game clears the next frame's ordering
 * table **before** the picture of this one has been read (gl_tween.c
 * says the same of the camera it keeps), so this is where a frame's
 * shadows are laid over the picture it built: everything of that frame
 * has been drawn, and nothing of the next one has. */
void port_GlShadowNewFrame(void) {
    s32 was = port_ProfileSwitch(PORT_PROFILE_SHADOW);

    port_GlShadowLay();
    port_GlShadowForget();
    port_ProfileSwitch(was);
}

/* Whether this primitive is a piece of the stage's ground that the
 * shadows fall on, which is therefore not one of the things that keep
 * the darkening off a pixel: a tile of the stage, by the thing the
 * renderer said it was drawing (port_remaster.h).
 *
 * The ground is never one of them, wherever it comes in the painting.
 * The console paints a tile whose own depth is nearer than the shadow's
 * over the shadow, so parts of the game's own black silhouette are
 * hidden by the very ground they fall on -- a fifth of it in the frames
 * measured here. A shadow that is drawn as a darkening of the ground
 * should not have those holes in it, so the darkening is laid on the
 * whole silhouette. What the ground does not do, the fighters and
 * everything else in the scene do. */
/* How near to level a face of the stage's pieces is a floor the shadows
 * fall on: the cosine of the angle between its normal and the vertical,
 * 30 degrees. */
#define FLOOR_COS 0.866

/* Whether a face of the stage drawn as a piece of the scene and not as a
 * tile -- a deck, a bridge's boards, a stage's floor the game builds as a
 * model -- lies level, and so is floor a fighter may stand on and his
 * shadow falls on. Its corners are taken back from the screen into the
 * camera's space by the view the stage was drawn with (the projection is
 * the coprocessor's: screen = h * v / z + the middle), and from there
 * into the world by the inverse of the view's rotation, whose second row
 * the renderer halves and the inverse undoes; the world's vertical is y.
 * The scale the renderer drew a thing at scales all three of a corner's
 * numbers alike and leaves the direction of a normal as it is. */
static s32 port_GlShadowIsFloor(const PortRemasterPrim *prim) {
    s32 is_between = FALSE;
    const PortStageView *view = port_GlStageViewNow(&is_between);
    double m[3][3];
    double inv[3][3];
    double world[3][3];
    double det;
    double a[3];
    double b[3];
    double n[3];
    double length;
    s32 i;
    s32 j;

    if (view == NULL || view->h <= 0 || prim->corner_count < 3 ||
        !prim->has_depth) {
        return FALSE;
    }
    for (i = 0; i < 9; i++) {
        m[i / 3][i % 3] = (double)view->matrix[i] / 4096.0;
    }
    det = m[0][0] * (m[1][1] * m[2][2] - m[1][2] * m[2][1]) -
          m[0][1] * (m[1][0] * m[2][2] - m[1][2] * m[2][0]) +
          m[0][2] * (m[1][0] * m[2][1] - m[1][1] * m[2][0]);
    if (det > -1e-9 && det < 1e-9) {
        return FALSE;
    }
    inv[0][0] = (m[1][1] * m[2][2] - m[1][2] * m[2][1]) / det;
    inv[0][1] = (m[0][2] * m[2][1] - m[0][1] * m[2][2]) / det;
    inv[0][2] = (m[0][1] * m[1][2] - m[0][2] * m[1][1]) / det;
    inv[1][0] = (m[1][2] * m[2][0] - m[1][0] * m[2][2]) / det;
    inv[1][1] = (m[0][0] * m[2][2] - m[0][2] * m[2][0]) / det;
    inv[1][2] = (m[0][2] * m[1][0] - m[0][0] * m[1][2]) / det;
    inv[2][0] = (m[1][0] * m[2][1] - m[1][1] * m[2][0]) / det;
    inv[2][1] = (m[0][1] * m[2][0] - m[0][0] * m[2][1]) / det;
    inv[2][2] = (m[0][0] * m[1][1] - m[0][1] * m[1][0]) / det;
    for (i = 0; i < 3; i++) {
        const PortRemasterCorner *c = &prim->corner[i];
        double z = (double)c->depth;
        double v[3];

        if (c->depth <= 0) {
            return FALSE;
        }
        v[0] = ((double)(c->x - (prim->offset_x << 16)) - (double)view->ofx) /
               65536.0 * z / (double)view->h;
        v[1] = ((double)(c->y - (prim->offset_y << 16)) - (double)view->ofy) /
               65536.0 * z / (double)view->h;
        v[2] = z;
        for (j = 0; j < 3; j++) {
            world[i][j] =
                inv[j][0] * v[0] + inv[j][1] * v[1] + inv[j][2] * v[2];
        }
    }
    for (j = 0; j < 3; j++) {
        a[j] = world[1][j] - world[0][j];
        b[j] = world[2][j] - world[0][j];
    }
    n[0] = a[1] * b[2] - a[2] * b[1];
    n[1] = a[2] * b[0] - a[0] * b[2];
    n[2] = a[0] * b[1] - a[1] * b[0];
    length = n[0] * n[0] + n[1] * n[1] + n[2] * n[2];
    /* level: the vertical part of the normal is most of it, whichever
     * way the face is wound */
    return length > 0.0 && n[1] * n[1] >= FLOOR_COS * FLOOR_COS * length;
}

s32 port_GlShadowIsGround(const PortRemasterPrim *prim) {
    s32 count = 0;
    PortAddress tiles;

    if (!port_GlShadowIsSoft() || prim->id.object == 0) {
        return FALSE;
    }
    tiles = port_RemasterStageTiles(&count);
    if (tiles != 0 && count > 0 && prim->id.object >= tiles &&
        prim->id.object <
            tiles + (PortAddress)((u32)count * sizeof(StageTile))) {
        return TRUE;
    }
    /* a level face of the stage's own pieces, which a fighter may stand
     * on (a deck, a bridge): the shadows fall on it as on the ground */
    if (!prim->is_fighter && !prim->is_shadow && !prim->is_effect &&
        !prim->is_half && port_GlShadowIsFloor(prim)) {
        g_shadow.floor_faces++;
        return TRUE;
    }
    return FALSE;
}

/* The first primitive of a picture that has a shadow in it: the drawing
 * area it belongs to and its drawing offset. */
static void port_GlShadowBeginPicture(const PortRemasterPrim *prim) {
    s32 x;
    s32 y;
    s32 w;
    s32 h;

    /* the drawing area the game gave this frame: the darkening must not
     * reach outside it, where the picture of the frame before it is
     * waiting to be shown */
    port_GlScissorFor(prim, &x, &y, &w, &h);
    g_shadow.clip.left = x;
    g_shadow.clip.top = y;
    g_shadow.clip.right = x + w - 1;
    g_shadow.clip.bottom = y + h - 1;
    g_shadow.has_any = TRUE;
    g_shadow.offset_x = prim->offset_x;
    g_shadow.offset_y = prim->offset_y;
}

s32 port_GlShadowTakes(const PortRemasterPrim *prim) {
    PortGlPoint point[6];
    PortGlPicture picture;
    s32 many;
    s32 i;

    if (!port_GlShadowIsSoft()) {
        return FALSE;
    }
    if (!prim->is_shadow) {
        /* a fighter's own face says that the picture has someone in it
         * to cast a shadow from, where the game's shadow mesh may be off
         * the screen entirely */
        if (g_shadow.is_cast && !g_shadow.has_any && prim->is_fighter &&
            prim->has_place) {
            port_GlShadowBeginPicture(prim);
        }
        return FALSE;
    }
    /* From here on the primitive is not drawn, whether or not there is
     * room for it in the mask: a face the mask lost is a face of the
     * silhouette missing, which is a hole in a soft shadow, and not a
     * black polygon in the middle of one. */
    many = port_GlPlacePoints(prim, point);
    port_GlGetPicture(&picture);
    if (!g_shadow.has_any) {
        port_GlShadowBeginPicture(prim);
    }
    if (g_shadow.count + many > POINTS_MAX) {
        g_shadow.lost += many / 3;
        return TRUE;
    }
    for (i = 0; i < many; i++) {
        ShadowPoint *to = &g_shadow.point[g_shadow.count++];

        to->x = point[i].x;
        to->y = point[i].y;
        to->w = point[i].w;
        port_GlShadowAddToBox(&g_shadow.box, (s32)(point[i].x * picture.draw_x),
                              (s32)(point[i].y * picture.draw_y));
    }
    return TRUE;
}

void port_GlShadowWroteVram(s32 x, s32 y, s32 w, s32 h) {
    PortGlPicture picture;
    ShadowBox box;

    /* Only a write that comes after the shadows have been painted hides
     * them. The one before them is the frame's own clear, which is what
     * the shadows are painted onto. */
    if (!port_GlShadowIsSoft() || !g_shadow.has_any || w <= 0 || h <= 0) {
        return;
    }
    port_GlGetPicture(&picture);
    box.left = (x + picture.margin) * picture.draw_x;
    box.top = y * picture.draw_y;
    box.right = box.left + w * picture.draw_x - 1;
    box.bottom = box.top + h * picture.draw_y - 1;
    /* Only what lands in the picture the shadows are in. A frame writes
     * video memory a hundred times over and nearly all of it is
     * elsewhere: the stage's fog is 31 rows of colour tables copied
     * every frame, at the far end of video memory (stage_QueueFog). */
    port_GlShadowHoldBox(&box, &g_shadow.clip);
    if (port_GlShadowBoxIsEmpty(&box)) {
        return;
    }
    if (g_shadow.cover_count < COVERS_MAX) {
        g_shadow.cover[g_shadow.cover_count++] = box;
        return;
    }
    /* more than the frame was given room for: the one rectangle holding
     * them all, which gives up a little more of the shadows than it has
     * to and never darkens what one of them covered */
    port_GlShadowAddToBox(&g_shadow.cover[0], box.left, box.top);
    port_GlShadowAddToBox(&g_shadow.cover[0], box.right, box.bottom);
}

/* The penumbra in the colour buffer's own pixels: a few pixels of the
 * picture, which is what the player sees, times the samples a picture
 * pixel is made of. */
static s32 port_GlShadowRadius(void) {
    PortGlOptions options;
    s32 sample;

    port_GlGetOptions(&options);
    sample = options.sample < 1 ? 1 : options.sample;
    return PORT_SHADOW_PENUMBRA * sample;
}

static void port_GlShadowScissorBox(const ShadowBox *box) {
    glScissor(box->left, box->top, box->right - box->left + 1,
              box->bottom - box->top + 1);
}

/* The same thing of the frame after it, or NULL: the same caster drawn
 * from the same model. */
static const ModelCast *port_GlShadowModelAfter(const ModelCast *one) {
    const CastFrame *two = g_shadow.card_two;
    s32 i;

    for (i = 0; two != NULL && i < two->model_count; i++) {
        if (two->model[i].caster == one->caster &&
            two->model[i].cast.array == one->cast.array) {
            return &two->model[i];
        }
    }
    return NULL;
}

static void port_GlShadowFlat(GLint at, const ModelCast *model) {
    glUniform4f(at, model->ground_y, model->x_shear, model->z_shear,
                model->is_on_feet ? 1.0f : 0.0f);
}

/* The things drawn from models of their own, cast on the card from their
 * own vertices: posed on each frame's bones by their terms (and the
 * smooth cloths from the buffers the card kept them out of the body
 * into), slid onto each frame's ground and mixed between the two as the
 * game's faces are; the cast program already in use. */
static void port_GlShadowDrawModels(void) {
    const CastFrame *one = g_shadow.card_one;
    s32 i;
    s32 k;

    if (one == NULL || one->model_count == 0) {
        return;
    }
    for (i = 0; i < one->model_count; i++) {
        const ModelCast *a = &one->model[i];
        const ModelCast *b = a;

        if (g_shadow.card_two != NULL) {
            b = port_GlShadowModelAfter(a);
            if (b == NULL) {
                g_shadow.cast_unpaired++;
                b = a;
            }
        }
        glUniform1i(g_shadow.cast_at_kind, 1);
        glUniformMatrix4fv(g_shadow.cast_at_world_one, PORT_FIGHTER_BONES,
                           GL_FALSE, a->cast.world[0]);
        glUniformMatrix4fv(g_shadow.cast_at_world_two, PORT_FIGHTER_BONES,
                           GL_FALSE, b->cast.world[0]);
        port_GlShadowFlat(g_shadow.cast_at_flat_one, a);
        port_GlShadowFlat(g_shadow.cast_at_flat_two, b);
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 2, a->cast.feet);
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 3, b->cast.feet);
        glBindVertexArray(a->cast.array);
        glDrawElements(GL_TRIANGLES, a->cast.indices, GL_UNSIGNED_INT, NULL);
        port_GlCountDraw((u32)a->cast.indices, 1);
        glUniform1i(g_shadow.cast_at_kind, 2);
        for (k = 0; k < a->cast.cloth_count; k++) {
            u32 after = a->cast.cloth_held[k];
            s32 n;

            for (n = 0; b != a && n < b->cast.cloth_count; n++) {
                if (b->cast.cloth_mesh[n] == a->cast.cloth_mesh[k] &&
                    b->cast.cloth_indices[n] == a->cast.cloth_indices[k]) {
                    after = b->cast.cloth_held[n];
                    break;
                }
            }
            glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, a->cast.cloth_held[k]);
            glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, after);
            glBindVertexArray(a->cast.cloth_array[k]);
            glDrawElements(
                GL_TRIANGLES, a->cast.cloth_indices[k], GL_UNSIGNED_INT, NULL);
            port_GlCountDraw((u32)a->cast.cloth_indices[k], 1);
        }
    }
    glBindVertexArray(0);
}

/* The silhouettes into the mask's red. */
static void port_GlShadowDrawSilhouettes(const PortGlPicture *picture) {
    if (g_shadow.is_on_card) {
        glUseProgram(g_shadow.cast_program);
        glBindVertexArray(g_shadow.cast_array);
        glUniform1f(g_shadow.cast_at_t, g_shadow.card_t);
        glUniformMatrix3fv(
            g_shadow.cast_at_rotation, 1, GL_TRUE, g_shadow.card_rotation);
        glUniform3f(g_shadow.cast_at_camera, g_shadow.card_camera[0],
                    g_shadow.card_camera[1], g_shadow.card_camera[2]);
        glUniform4f(g_shadow.cast_at_project, g_shadow.card_project[0],
                    g_shadow.card_project[1], g_shadow.card_project[2],
                    g_shadow.card_project[3]);
        glUniform2f(g_shadow.cast_at_target, (GLfloat)picture->target_w,
                    (GLfloat)picture->target_h);
        glUniform2f(g_shadow.cast_at_scale, (GLfloat)picture->draw_x,
                    (GLfloat)picture->draw_y);
        glDisable(GL_BLEND);
        glUniform1i(g_shadow.cast_at_kind, 0);
        if (g_shadow.sent_count > 0) {
            glDrawArrays(GL_TRIANGLES, 0, 3 * g_shadow.sent_count);
            port_GlCountDraw((u32)(3 * g_shadow.sent_count), 1);
        }
        port_GlShadowDrawModels();
        return;
    }
    glUseProgram(g_shadow.silhouette);
    glBindVertexArray(g_shadow.array);
    glBindBuffer(GL_ARRAY_BUFFER, g_shadow.buffer);
    glBufferData(GL_ARRAY_BUFFER,
                 (GLsizeiptr)((u32)g_shadow.count * sizeof(*g_shadow.point)),
                 g_shadow.point, GL_STREAM_DRAW);
    glUniform2f(g_shadow.at_target, (GLfloat)picture->target_w,
                (GLfloat)picture->target_h);
    glUniform2f(
        g_shadow.at_scale, (GLfloat)picture->draw_x, (GLfloat)picture->draw_y);
    glDisable(GL_BLEND);
    glDrawArrays(GL_TRIANGLES, 0, g_shadow.count);
    port_GlCountDraw((u32)g_shadow.count, 1);
}

/* What a write of video memory covered: the mask gives those pixels up,
 * coverage and all, and marks them as not the ground. */
static void port_GlShadowDrawCovers(const ShadowBox *inside) {
    s32 i;

    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    for (i = 0; i < g_shadow.cover_count; i++) {
        ShadowBox box = g_shadow.cover[i];

        port_GlShadowHoldBox(&box, inside);
        if (port_GlShadowBoxIsEmpty(&box)) {
            continue;
        }
        port_GlShadowScissorBox(&box);
        glClear(GL_COLOR_BUFFER_BIT);
    }
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
}

/* A picture drawn between two of the game's frames is begun and finished
 * in one call (gl_tween.c), so that one is laid down here. */
void port_GlShadowFinish(void) { port_GlShadowLay(); }

/* The mask of the picture whose shadows have been gathered, and the
 * darkening over it: the caller has flushed what the picture still owed
 * and holds the gathering. */
static void port_GlShadowPaint(void) {
    PortGlPicture picture;
    ShadowBox target;
    ShadowBox cleared;
    ShadowBox painted;
    s32 radius = port_GlShadowRadius();

    port_GlGetPicture(&picture);
    if (picture.target_w <= 0 || g_shadow.count == 0 ||
        !port_GlShadowMakeMask(&picture)) {
        return;
    }
    target.left = 0;
    target.top = 0;
    target.right = picture.target_w - 1;
    target.bottom = picture.target_h - 1;
    /* Three boxes: the darkening is laid over the silhouettes grown by
     * the penumbra, its shader reads the mask a penumbra further out
     * again, and the mask is cleared that far out so that nothing of the
     * frame before is read. */
    painted = port_GlShadowGrownBox(&g_shadow.box, radius);
    cleared = port_GlShadowGrownBox(&g_shadow.box, 2 * radius + 1);
    port_GlShadowHoldBox(&painted, &g_shadow.clip);
    port_GlShadowHoldBox(&painted, &target);
    port_GlShadowHoldBox(&cleared, &target);
    if (port_GlShadowBoxIsEmpty(&painted) ||
        port_GlShadowBoxIsEmpty(&cleared)) {
        return;
    }

    /* the mask */
    glBindFramebuffer(GL_FRAMEBUFFER, g_shadow.framebuffer);
    glViewport(0, 0, picture.target_w, picture.target_h);
    glEnable(GL_SCISSOR_TEST);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glDisable(GL_BLEND);
    glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
    port_GlShadowScissorBox(&cleared);
    glClear(GL_COLOR_BUFFER_BIT);
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    port_GlShadowDrawSilhouettes(&picture);
    port_GlShadowDrawCovers(&cleared);
    /* Everything the frame painted that is not the stage's ground, into
     * the mask's alpha alone and through the picture's own shader, so
     * that a texel the picture threw away leaves the ground showing and
     * the shadow on it (gl_draw.c). */
    port_GlShadowScissorBox(&cleared);
    g_shadow.shown_occluders = port_GlOccluderCount();
    port_GlDrawOccluders(g_shadow.framebuffer);
    /* ... and the fighters drawn from models of their own, whose faces
     * the picture did not draw */
    port_GlShadowScissorBox(&cleared);
    port_GlFighterDrawOccluders(g_shadow.framebuffer);
    /* ... and the stage's pieces the mesh drew (gl_stage.h) */
    port_GlShadowScissorBox(&cleared);
    port_GlStageDrawOccluders(g_shadow.framebuffer);

    /* the darkening, into the picture */
    glBindFramebuffer(GL_FRAMEBUFFER, picture.framebuffer);
    glViewport(0, 0, picture.target_w, picture.target_h);
    glEnable(GL_SCISSOR_TEST);
    port_GlShadowScissorBox(&painted);
    glUseProgram(g_shadow.dark);
    glBindVertexArray(g_shadow.quad_array);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, g_shadow.mask);
    glUniform1i(g_shadow.dark_at_mask, 0);
    glUniform2f(g_shadow.dark_at_target, (GLfloat)picture.target_w,
                (GLfloat)picture.target_h);
    glUniform4f(
        g_shadow.dark_at_rect, (GLfloat)painted.left, (GLfloat)painted.top,
        (GLfloat)(painted.right - painted.left + 1),
        (GLfloat)(painted.bottom - painted.top + 1));
    glUniform1f(g_shadow.dark_at_radius, (GLfloat)radius);
    glUniform1f(g_shadow.dark_at_strength, (GLfloat)g_shadow.strength / 100.0f);
    glUniform1i(g_shadow.dark_at_show, g_shadow.is_shown);
    /* what is there times what this draws: the ground keeps its texture
     * and is never covered */
    if (g_shadow.is_shown) {
        /* the mask itself, in the place of the picture */
        glDisable(GL_BLEND);
    } else {
        glEnable(GL_BLEND);
        glBlendEquation(GL_FUNC_ADD);
        glBlendFunc(GL_ZERO, GL_SRC_COLOR);
    }
    glDrawArrays(GL_TRIANGLES, 0, 6);
    glDisable(GL_BLEND);
    port_GlCountDraw(6, 1);
    {
        u32 area = (u32)((painted.right - painted.left + 1) *
                         (painted.bottom - painted.top + 1));

        if (area > g_shadow.widest_area) {
            g_shadow.widest_area = area;
        }
    }
    /* the renderer's own batching knows nothing of what was just done */
    port_GlForgetState();
}

/* ---- `shadows cast` ---- */

/* A view's rotation as numbers, 1.12 made 1, the second row whole: the
 * view is loaded with it halved, which the caller undoes and does. */
static void port_GlShadowRotation(const PortStageView *view, double *r) {
    s32 i;

    for (i = 0; i < 9; i++) {
        r[i] = (double)view->matrix[i] / 4096.0;
    }
}

/* The inverse of a rotation of three by three, row after row: FALSE if it
 * has none. The view's rotation is a rotation, but one of 1.12 numbers,
 * so the transpose is not quite its inverse, and the true one takes a
 * point back exactly where the projection took it from. */
static s32 port_GlShadowInvert(const double *m, double *out) {
    double det =
        m[0] * (m[4] * m[8] - m[5] * m[7]) -
        m[1] * (m[3] * m[8] - m[5] * m[6]) + m[2] * (m[3] * m[7] - m[4] * m[6]);
    double one;

    if (det > -1e-9 && det < 1e-9) {
        return FALSE;
    }
    one = 1.0 / det;
    out[0] = (m[4] * m[8] - m[5] * m[7]) * one;
    out[1] = (m[2] * m[7] - m[1] * m[8]) * one;
    out[2] = (m[1] * m[5] - m[2] * m[4]) * one;
    out[3] = (m[5] * m[6] - m[3] * m[8]) * one;
    out[4] = (m[0] * m[8] - m[2] * m[6]) * one;
    out[5] = (m[2] * m[3] - m[0] * m[5]) * one;
    out[6] = (m[3] * m[7] - m[4] * m[6]) * one;
    out[7] = (m[1] * m[6] - m[0] * m[7]) * one;
    out[8] = (m[0] * m[4] - m[1] * m[3]) * one;
    return TRUE;
}

/* The faces of the frame whose picture is being laid down, back into the
 * world and slid onto the ground (port_remaster.h,
 * port_RemasterCasterFaces). A corner's place before the projection is
 * the view's rotation times its offset from the camera, in the scale the
 * renderer drew the thing at and with the second row halved; so the
 * world's point is the camera plus the inverse rotation times that,
 * scaled back. Then it is light_ProjectShadow's arithmetic, in floating
 * point and on every corner: x and z move by the height above the
 * caster's ground times the sun's slope, and y becomes the ground's. */
static void port_GlShadowGather(CastFrame *frame) {
    const PortShadowCaster *face;
    /* the things drawn from models of their own: the ground each casts
     * onto, by the first of its faces, or no ground yet */
    s32 model_ground[PORT_FIGHTER_SLOTS + PORT_FIGHTER_WEAPONS];
    s32 has_ground[PORT_FIGHTER_SLOTS + PORT_FIGHTER_WEAPONS];
    /* ... and whether any face of it was drawn this frame, with a ground or
     * not */
    s32 has_faces[PORT_FIGHTER_SLOTS];
    s32 faces = 0;
    double r[9];
    double back[9];
    double x_shear;
    double z_shear;
    s32 shear_x;
    s32 shear_z;
    s32 i;

    frame->count = 0;
    frame->model_count = 0;
    frame->model_triangles = 0;
    frame->view = *port_GetRemasterStageView();
    frame->is_valid = FALSE;
    {
        static u32 gathered;

        /* never 0, which is no frame sent */
        if (++gathered == 0) {
            gathered = 1;
        }
        g_shadow.gathered[frame == &g_shadow.cast[0] ? 0 : 1] = gathered;
    }
    g_shadow.cast_faces = 0;
    g_shadow.cast_orphans = 0;
    face = port_RemasterCasterFaces(&faces);
    /* no stage drawn this frame, and so no view to take the faces back
     * with */
    if (faces <= 0 || frame->view.frames <= 0 || frame->view.h <= 0) {
        return;
    }
    port_GlShadowRotation(&frame->view, r);
    if (!port_GlShadowInvert(r, back)) {
        return;
    }
    port_RemasterShadowShear(&shear_x, &shear_z);
    x_shear = (double)shear_x / 4096.0;
    z_shear = (double)shear_z / 4096.0;
    g_shadow.cast_faces = faces;
    memset(has_ground, 0, sizeof(has_ground));
    memset(has_faces, 0, sizeof(has_faces));
    for (i = 0; i < faces; i++, face++) {
        static const s32 corner[2][3] = {{0, 1, 2}, {1, 3, 2}};
        float flat[PORT_REMASTER_CORNERS][3];
        s32 caster = port_GlFighterCaster(face->object);
        s32 ground_y;
        s32 halves;
        s32 j;

        if (caster >= 0 && caster < PORT_FIGHTER_SLOTS) {
            has_faces[caster] = TRUE;
        }
        if (!port_RemasterCasterGround(face->object, &ground_y)) {
            g_shadow.cast_orphans++;
            continue;
        }
        if (caster >= 0) {
            /* cast from the model instead, after the faces */
            if (!has_ground[caster]) {
                has_ground[caster] = TRUE;
                model_ground[caster] = ground_y;
            }
            continue;
        }
        for (j = 0; j < face->count; j++) {
            double scale = (double)(1 << (face->depth_shift & 31)) /
                           (double)(1 << PORT_REMASTER_VIEW_FRACTION);
            double v[3];
            double world[3];
            double above;
            s32 k;

            v[0] = (double)face->view[j][0] * scale;
            v[1] = (double)face->view[j][1] * scale * 2.0;
            v[2] = (double)face->view[j][2] * scale;
            for (k = 0; k < 3; k++) {
                world[k] = (double)frame->view.camera[k] + back[k * 3] * v[0] +
                           back[k * 3 + 1] * v[1] + back[k * 3 + 2] * v[2];
            }
            above = world[1] - (double)ground_y;
            flat[j][0] = (float)(world[0] + above * x_shear);
            flat[j][1] = (float)ground_y;
            flat[j][2] = (float)(world[2] + above * z_shear);
        }
        halves = face->count == 4 ? 2 : 1;
        for (j = 0; j < halves; j++) {
            CastTriangle *to;
            s32 k;

            if (frame->count >= TRIANGLES_MAX) {
                g_shadow.lost++;
                break;
            }
            to = &frame->triangle[frame->count++];
            for (k = 0; k < 3; k++) {
                to->p[k][0] = flat[corner[j][k]][0];
                to->p[k][1] = flat[corner[j][k]][1];
                to->p[k][2] = flat[corner[j][k]][2];
            }
            to->group = face->group;
            to->face = face->face;
            to->part = j;
        }
    }
    for (i = 0; i < MODEL_CASTS; i++) {
        /* A fighter drawn from a model whose body the game casts no
         * shadow of this frame: on some floors the game's shadow nodes of
         * a body are not drawn at all -- the raised floor of the Tatara
         * shrine, where a fighter standing in line with the shrine had
         * his sword's shadow and no other. His shadow falls on the floor
         * his soles are on: the lowest point of the model as the frame
         * posed it, which the card finds (gl_fighter_card.h). */
        s32 is_on_feet =
            i < PORT_FIGHTER_SLOTS && !has_ground[i] && has_faces[i];
        ModelCast *model;

        if (!has_ground[i] && !is_on_feet) {
            continue;
        }
        model = &frame->model[frame->model_count];
        if (!port_GlFighterCastOf(i, is_on_feet, &model->cast)) {
            continue;
        }
        if (is_on_feet && model->cast.feet == 0) {
            continue;
        }
        if (is_on_feet) {
            g_shadow.cast_feet_grounds++;
        }
        model->caster = i;
        model->is_on_feet = is_on_feet;
        model->ground_y = is_on_feet ? 0.0f : (float)model_ground[i];
        model->x_shear = (float)x_shear;
        model->z_shear = (float)z_shear;
        frame->model_count++;
        frame->model_triangles += model->cast.triangles;
        g_shadow.cast_model_triangles += (u32)model->cast.triangles;
    }
    frame->is_valid = frame->count > 0 || frame->model_count > 0;
}

/* A point of the world on the picture: the view's rotation and the
 * projection, as the coprocessor has them, and the picture's drawing
 * offset and margin. FALSE for a point too near the camera or behind
 * it. */
static s32 port_GlShadowProject(const PortStageView *view, const double *r,
                                const float *p, s32 margin, ShadowPoint *out) {
    double d[3];
    double v[3];
    s32 k;

    for (k = 0; k < 3; k++) {
        d[k] = (double)p[k] - (double)view->camera[k];
    }
    for (k = 0; k < 3; k++) {
        v[k] = r[k * 3] * d[0] + r[k * 3 + 1] * d[1] + r[k * 3 + 2] * d[2];
    }
    if (v[2] < CAST_NEAR) {
        return FALSE;
    }
    out->x = (GLfloat)((double)view->ofx / PORT_REMASTER_ONE +
                       (double)view->h * v[0] / v[2] +
                       (double)(g_shadow.offset_x + margin));
    out->y = (GLfloat)((double)view->ofy / PORT_REMASTER_ONE +
                       (double)view->h * 0.5 * v[1] / v[2] +
                       (double)g_shadow.offset_y);
    out->w = 1.0f;
    return TRUE;
}

/* Whether two frames' triangles are the same triangle of the same face. */
static s32 port_GlShadowSameTriangle(
    const CastTriangle *one, const CastTriangle *two) {
    return one->group == two->group && one->face == two->face &&
           one->part == two->part;
}

/* The box of the pair's silhouettes: the corners of the box in the world
 * that holds a frame's triangles, as the frame's view places them, grown by
 * a little for the pictures between two frames, where the view is mixed;
 * the whole drawing area where a corner is too near the camera. It only
 * bounds what the mask is cleared and darkened over, so it may be larger
 * than the silhouettes, and it is worked out once a pair of frames. */
static void port_GlShadowPairBox(
    const CastFrame *frame, const PortGlPicture *picture, ShadowBox *box) {
    float lo[3];
    float hi[3];
    double r[9];
    s32 i;
    s32 k;
    s32 n;

    if (frame->count <= 0 && frame->model_count <= 0) {
        return;
    }
    for (n = 0; n < 3; n++) {
        lo[n] = 1e30f;
        hi[n] = -1e30f;
    }
    for (i = 0; i < frame->model_count; i++) {
        const ModelCast *model = &frame->model[i];
        s32 corner;

        if (!model->cast.is_boxed) {
            *box = g_shadow.clip;
            return;
        }
        /* each corner of the model's box slid onto its ground, which is
         * anywhere in the box where it is the body's lowest point */
        for (corner = 0; corner < 16; corner++) {
            const float *at = model->cast.lo;
            float p[3];
            float ground = model->ground_y;
            float above;

            for (n = 0; n < 3; n++) {
                p[n] = (corner >> n) & 1 ? model->cast.hi[n] : at[n];
            }
            if (model->is_on_feet) {
                ground = corner & 8 ? model->cast.hi[1] : model->cast.lo[1];
            }
            above = p[1] - ground;
            p[0] += above * model->x_shear;
            p[1] = ground;
            p[2] += above * model->z_shear;
            for (n = 0; n < 3; n++) {
                lo[n] = p[n] < lo[n] ? p[n] : lo[n];
                hi[n] = p[n] > hi[n] ? p[n] : hi[n];
            }
        }
    }
    for (i = 0; i < frame->count; i++) {
        for (k = 0; k < 3; k++) {
            for (n = 0; n < 3; n++) {
                float v = frame->triangle[i].p[k][n];

                lo[n] = v < lo[n] ? v : lo[n];
                hi[n] = v > hi[n] ? v : hi[n];
            }
        }
    }
    port_GlShadowRotation(&frame->view, r);
    for (i = 0; i < 8; i++) {
        float corner[3];
        ShadowPoint at;

        corner[0] = (i & 1) ? hi[0] : lo[0];
        corner[1] = (i & 2) ? hi[1] : lo[1];
        corner[2] = (i & 4) ? hi[2] : lo[2];
        if (!port_GlShadowProject(
                &frame->view, r, corner, picture->margin, &at)) {
            *box = g_shadow.clip;
            return;
        }
        port_GlShadowAddToBox(box, (s32)(at.x * (GLfloat)picture->draw_x),
                              (s32)(at.y * (GLfloat)picture->draw_y));
    }
}

/* The pair of frames the pictures are cast from, sent to the card once:
 * each triangle of the frame on show with the same triangle of the frame
 * after it, or itself where that has none (port_GlShadowSameTriangle). */
static void port_GlShadowSendPair(
    const CastFrame *one, const CastFrame *two, const PortGlPicture *picture) {
    u32 one_number = g_shadow.gathered[one == &g_shadow.cast[0] ? 0 : 1];
    u32 two_number =
        two == NULL ? 0 : g_shadow.gathered[two == &g_shadow.cast[0] ? 0 : 1];
    float *out = g_shadow.staged;
    s32 i;
    s32 k;
    s32 n;

    if (one_number == g_shadow.sent_one && two_number == g_shadow.sent_two) {
        return;
    }
    for (i = 0; i < one->count; i++) {
        const CastTriangle *a = &one->triangle[i];
        const CastTriangle *b = a;

        if (two != NULL) {
            b = i < two->count ? &two->triangle[i] : NULL;
            if (b == NULL || !port_GlShadowSameTriangle(a, b)) {
                g_shadow.cast_unpaired++;
                b = a;
            }
        }
        for (k = 0; k < 3; k++) {
            for (n = 0; n < 3; n++) {
                *out++ = a->p[k][n];
            }
            for (n = 0; n < 3; n++) {
                *out++ = b->p[k][n];
            }
        }
    }
    glBindBuffer(GL_ARRAY_BUFFER, g_shadow.cast_buffer);
    glBufferData(
        GL_ARRAY_BUFFER, (GLsizeiptr)((size_t)one->count * 18 * sizeof(float)),
        g_shadow.staged, GL_STREAM_DRAW);
    port_GlShadowEmptyBox(&g_shadow.sent_box);
    port_GlShadowPairBox(one, picture, &g_shadow.sent_box);
    if (two != NULL) {
        port_GlShadowPairBox(two, picture, &g_shadow.sent_box);
    }
    if (!port_GlShadowBoxIsEmpty(&g_shadow.sent_box)) {
        s32 grow = picture->draw_x * CAST_BOX_GROW;

        g_shadow.sent_box.left -= grow;
        g_shadow.sent_box.top -= grow;
        g_shadow.sent_box.right += grow;
        g_shadow.sent_box.bottom += grow;
    }
    g_shadow.sent_count = one->count;
    g_shadow.sent_one = one_number;
    g_shadow.sent_two = two_number;
}

/* The silhouette of the picture being laid down, cast from the fighters'
 * faces, in the place of the game's shadow mesh. A picture of the game's
 * own frame gathers that frame's faces; a picture between two frames
 * moves each triangle of the frame on show the way it went by the frame
 * after, and the camera with it (gl_tween.c). Where there is nothing to
 * cast from, the game's own silhouette, already gathered, stays. */
static void port_GlShadowCast(void) {
    const CastFrame *one;
    const CastFrame *two = NULL;
    PortStageView view;
    PortGlPicture picture;
    double r[9];
    s32 i;

    if (g_shadow.phase < 0) {
        s32 next = g_shadow.newest == 0 ? 1 : 0;

        port_GlShadowGather(&g_shadow.cast[next]);
        /* the card may have posed a body or made a cloth, with programs
         * and buffers of its own (gl_fighter_card.h) */
        port_GlForgetState();
        g_shadow.older = g_shadow.newest;
        g_shadow.newest = next;
        one = &g_shadow.cast[next];
    } else {
        if (g_shadow.older < 0 || g_shadow.newest < 0) {
            g_shadow.cast_fallbacks++;
            return;
        }
        one = &g_shadow.cast[g_shadow.older];
        two = &g_shadow.cast[g_shadow.newest];
        if (!two->is_valid) {
            two = NULL;
        }
    }
    if (!one->is_valid) {
        g_shadow.cast_fallbacks++;
        return;
    }
    view = one->view;
    if (two != NULL) {
        port_GlStageMixView(
            &view, &one->view, &two->view, g_shadow.phase, PORT_TWEEN_ONE);
    }
    port_GlShadowRotation(&view, r);
    port_GlGetPicture(&picture);
    port_GlShadowSendPair(one, two, &picture);
    g_shadow.count = 3 * (g_shadow.sent_count + one->model_triangles);
    g_shadow.box = g_shadow.sent_box;
    g_shadow.is_on_card = g_shadow.count > 0;
    g_shadow.card_one = one;
    g_shadow.card_two = two;
    g_shadow.card_t =
        two != NULL ? (GLfloat)g_shadow.phase / (GLfloat)PORT_TWEEN_ONE : 0.0f;
    for (i = 0; i < 9; i++) {
        g_shadow.card_rotation[i] = (GLfloat)r[i];
    }
    for (i = 0; i < 3; i++) {
        g_shadow.card_camera[i] = (GLfloat)view.camera[i];
    }
    g_shadow.card_project[0] = (GLfloat)view.h;
    g_shadow.card_project[1] =
        (GLfloat)((double)view.ofx / PORT_REMASTER_ONE +
                  (double)(g_shadow.offset_x + picture.margin));
    g_shadow.card_project[2] = (GLfloat)((double)view.ofy / PORT_REMASTER_ONE +
                                         (double)g_shadow.offset_y);
    g_shadow.card_project[3] = (GLfloat)CAST_NEAR;
    g_shadow.cast_triangles = g_shadow.count / 3;
}

/* The shadows gathered so far laid over the picture they belong to, and
 * forgotten: the next primitive to come is the next picture's. */
static void port_GlShadowLay(void) {
    if (!port_GlShadowIsSoft() || !g_shadow.has_any) {
        return;
    }
    /* everything of the picture is painted before the shadows darken it,
     * and the batch of it that is still waiting is part of that */
    port_GlFlushBatch();
    if (g_shadow.is_cast) {
        port_GlShadowCast();
    }
    g_shadow.shown_faces = g_shadow.count / 3;
    g_shadow.run_lost += g_shadow.lost;
    port_GlShadowPaint();
    port_GlShadowForget();
}
