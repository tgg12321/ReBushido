/* The lighting overhaul's passes after the scene: reflections on the
 * screen, ambient occlusion and the lights' bloom (gl_post.h; docs/PORT.md,
 * "The lighting overhaul", c and d).
 *
 * They read the depth of the ground's mesh and the fighters' layer in the
 * world's units, one a pixel of the buffer (gl.h, PortGlPicture's
 * `ground_depth`), and the view the drawing on show was drawn with (the
 * game draws one frame while it shows the last, in another part of video
 * memory, and each part keeps its own depth). Once a picture, over the
 * picture averaged down to the window's own pixels, before it is shown or
 * read back:
 *
 *   light     at a third of the size, one pass with two outputs: the occlusion
 *             of each pixel of ground, and its reflection -- a ray along
 *             the view reflected in its face, walked across the picture
 *             through the depth until it passes behind something; where
 *             it meets nothing, the sky the picture shows in that
 *             direction, or nothing
 *   smooth    both smoothed, three by three, where the depth agrees
 *   bloom     each light of the frame's list a soft glow at a quarter of
 *             the size where the scene does not hide it, spread wider
 *             at an eighth and a sixteenth
 *   lay       the lot laid over the picture into a buffer of the
 *             picture's size, which then becomes the picture
 *
 * Nothing here changes a value the game computes, and nothing runs with
 * the console's picture (`Graphics: Original`, the prover's exact one). */
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "common.h"
#include "gl.h"
#include "gl_bench.h"
#include "gl_lights.h"
#include "gl_post.h"
#include "gl_shadow.h"
#include "gl_time.h"
#include "port_gpu.h"
#include "port_remaster.h"

/* the game's own header, for the shape of a light setup */
#include "light.h"

/* ---- GL, as much of it as this file uses ---- */

typedef unsigned int GLenum;
typedef unsigned char GLboolean;
typedef unsigned int GLbitfield;
typedef int GLint;
typedef unsigned int GLuint;
typedef int GLsizei;
typedef float GLfloat;
typedef char GLchar;

#define GL_FALSE 0
#define GL_TRUE 1
#define GL_TRIANGLES 0x0004
#define GL_TEXTURE_2D 0x0DE1
#define GL_TEXTURE0 0x84C0
#define GL_TEXTURE_MIN_FILTER 0x2801
#define GL_TEXTURE_MAG_FILTER 0x2800
#define GL_TEXTURE_WRAP_S 0x2802
#define GL_TEXTURE_WRAP_T 0x2803
#define GL_NEAREST 0x2600
#define GL_LINEAR 0x2601
#define GL_CLAMP_TO_EDGE 0x812F
#define GL_RGBA 0x1908
#define GL_RGBA16F 0x881A
#define GL_RGBA8 0x8058
#define GL_SHADER_STORAGE_BUFFER 0x90D2
#define GL_STREAM_DRAW 0x88E0
#define GL_R8 0x8229
#define GL_R32F 0x822E
#define GL_RED 0x1903
#define GL_FLOAT 0x1406
#define GL_UNSIGNED_BYTE 0x1401
#define GL_FRAMEBUFFER 0x8D40
#define GL_FRAMEBUFFER_COMPLETE 0x8CD5
#define GL_COLOR_ATTACHMENT0 0x8CE0
#define GL_COLOR_ATTACHMENT1 0x8CE1
#define GL_COLOR 0x1800
#define GL_DEPTH_TEST 0x0B71
#define GL_COMPUTE_SHADER 0x91B9
#define GL_NONE 0
#define GL_DEPTH_COMPONENT 0x1902
#define GL_DEPTH_COMPONENT32F 0x8CAC
#define GL_DEPTH_ATTACHMENT 0x8D00
#define GL_DEPTH_BUFFER_BIT 0x00000100
#define GL_LESS 0x0201
#define GL_R32UI 0x8236
#define GL_RED_INTEGER 0x8D94
#define GL_UNSIGNED_INT 0x1405
#define GL_READ_WRITE 0x88BA
#define GL_TEXTURE_FETCH_BARRIER_BIT 0x00000008
#define GL_R16F 0x822D
#define GL_BLEND 0x0BE2
#define GL_SCISSOR_TEST 0x0C11
#define GL_CULL_FACE 0x0B44
#define GL_ONE 1
#define GL_FUNC_ADD 0x8006
#define GL_VERTEX_SHADER 0x8B31
#define GL_FRAGMENT_SHADER 0x8B30
#define GL_COMPILE_STATUS 0x8B81
#define GL_LINK_STATUS 0x8B82

#define PORT_GL_POST_FUNCTIONS                                                 \
    GL(void, glViewport, (GLint, GLint, GLsizei, GLsizei))                     \
    GL(void, glEnable, (GLenum))                                               \
    GL(void, glDisable, (GLenum))                                              \
    GL(void, glBindTexture, (GLenum, GLuint))                                  \
    GL(void, glActiveTexture, (GLenum))                                        \
    GL(void, glGenTextures, (GLsizei, GLuint *))                               \
    GL(void, glTexImage2D,                                                     \
       (GLenum, GLint, GLint, GLsizei, GLsizei, GLint, GLenum, GLenum,         \
        const void *))                                                         \
    GL(void, glTexParameteri, (GLenum, GLenum, GLint))                         \
    GL(void, glBindFramebuffer, (GLenum, GLuint))                              \
    GL(void, glGenFramebuffers, (GLsizei, GLuint *))                           \
    GL(void, glFramebufferTexture2D, (GLenum, GLenum, GLenum, GLuint, GLint))  \
    GL(GLenum, glCheckFramebufferStatus, (GLenum))                             \
    GL(void, glDrawBuffers, (GLsizei, const GLenum *))                         \
    GL(void, glClearBufferfv, (GLenum, GLint, const GLfloat *))                \
    GL(void, glBlendFunc, (GLenum, GLenum))                                    \
    GL(void, glBlendEquation, (GLenum))                                        \
    GL(void, glGenVertexArrays, (GLsizei, GLuint *))                           \
    GL(void, glBindVertexArray, (GLuint))                                      \
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
    GL(void, glUseProgram, (GLuint))                                           \
    GL(GLint, glGetUniformLocation, (GLuint, const GLchar *))                  \
    GL(void, glUniform1i, (GLint, GLint))                                      \
    GL(void, glUniform1f, (GLint, GLfloat))                                    \
    GL(void, glUniform2f, (GLint, GLfloat, GLfloat))                           \
    GL(void, glUniform3f, (GLint, GLfloat, GLfloat, GLfloat))                  \
    GL(void, glUniformMatrix3fv, (GLint, GLsizei, GLboolean, const GLfloat *)) \
    GL(void, glDrawArrays, (GLenum, GLint, GLsizei))                           \
    GL(void, glDrawArraysInstanced, (GLenum, GLint, GLsizei, GLsizei))         \
    GL(void, glGenBuffers, (GLsizei, GLuint *))                                \
    GL(void, glBindBuffer, (GLenum, GLuint))                                   \
    GL(void, glBufferData, (GLenum, ptrdiff_t, const void *, GLenum))          \
    GL(void, glBindBufferBase, (GLenum, GLuint, GLuint))                       \
    GL(void, glDispatchCompute, (GLuint, GLuint, GLuint))                      \
    GL(void, glMemoryBarrier, (GLbitfield))                                    \
    GL(void, glBindImageTexture,                                               \
       (GLuint, GLuint, GLint, GLboolean, GLint, GLenum, GLenum))              \
    GL(void, glClearTexImage, (GLuint, GLint, GLenum, GLenum, const void *))   \
    GL(void, glClear, (GLbitfield))                                            \
    GL(void, glClearDepth, (double))                                           \
    GL(void, glDepthFunc, (GLenum))                                            \
    GL(void, glDepthMask, (GLboolean))                                         \
    GL(void, glReadBuffer, (GLenum))

#define GL(result, name, args) static result(*name) args;
PORT_GL_POST_FUNCTIONS
#undef GL

/* ---- the stages' floors ---- */

#define POST_STAGES 38

/* How much each stage's level ground reflects (0 none), and how rough it
 * is (the reflection's spread, 0 a mirror to 1). Looked at stage by stage
 * on the prover's tour (docs/PORT.md, "c. Reflections"): the polished
 * boards of a hall or a dojo, a stage's lacquered or glazed floor, the
 * glazed roof tiles of the castle, wet concrete at night and a garage's
 * sealed floor reflect; earth, grass, sand, gravel, rough stone and
 * weathered planks do not. Only a face that lies level (within about 20
 * degrees) reflects at all, so a hall's walls and a roof's slopes keep
 * their own colour. */
typedef struct {
    u8 percent; /* how much it reflects, in hundredths */
    u8 rough;   /* how rough, in hundredths */
} PostFloor;

static const PostFloor g_post_floors[POST_STAGES] = {
    {50, 30}, /* 0: the main Tatara shrine: a polished hall */
    {45, 35}, /* 1: the Meikyokan dojo: its boards */
    {22, 50}, /* 2: Shainto HQ: an open deck of boards */
    {40, 35}, /* 3: the Tatara shrine: its lacquered boards */
    {0, 0},   /* 4: the bus stop */
    {0, 0},   /* 5: the lost bridge: weathered planks */
    {0, 0},   /* 6: the Tatara shrine path */
    {0, 0},   /* 7: the bamboo thicket */
    {0, 0},   /* 8: outside the Meikyokan */
    {0, 0},   /* 9: the future dump site */
    {0, 0},   /* 10: the snake cliff */
    {0, 0},   /* 11: Kimigaura coast: sand */
    {0, 0},   /* 12: the orange tree fields */
    {35, 25}, /* 13: the castle rooftop: glazed tiles under the moon */
    {0, 0},   /* 14: under the lost bridge */
    {0, 0},   /* 15: the terraced fields */
    {0, 0},   /* 16: under the railway */
    {20, 45}, /* 17: the water tower dam: its painted deck */
    {0, 0},   /* 18: the lumberyard */
    {0, 0},   /* 19: the lighthouse */
    {45, 30}, /* 20: the kabuki stage: its lacquered stage */
    {0, 0},   /* 21: the abandoned ship */
    {20, 45}, /* 22: the ferry dock: wet concrete under the lamp */
    {0, 0},   /* 23: the Shainto gate */
    {0, 0},   /* 24: the purple cavern */
    {0, 0},   /* 25: the abandoned station */
    {0, 0},   /* 26: the railway */
    {0, 0},   /* 27: the abandoned tunnel */
    {0, 0},   /* 28: the abandoned shipyard */
    {20, 45}, /* 29: the building's rooftop: wet at night */
    {35, 30}, /* 30: the building's parking lot: a sealed floor */
    {0, 0},   /* 31: the Shainto cemetery */
    {45, 30}, /* 32: the first team battle court: a hall's boards */
    {0, 0},   /* 33: the second team battle court: a yard */
    {45, 30}, /* 34: chanbara's hall */
    {45, 30}, /* 35: practice 1: a dojo */
    {45, 30}, /* 36: practice 2: a dojo */
    {0, 0},   /* 37: a model of a castle town */
};

/* How rough the dressing's water is, mirrored (port_GlPostRough). */
#define POST_WATER_ROUGH 0.45f

/* How level a face has to be to reflect: its normal's upward part. */
#define POST_LEVEL 0.94f

float port_GlPostReflectance(s32 stage, const float *normal) {
    float length;
    float up;

    if (stage < 0 || stage >= POST_STAGES ||
        g_post_floors[stage].percent == 0) {
        return 0.0f;
    }
    length =
        normal[0] * normal[0] + normal[1] * normal[1] + normal[2] * normal[2];
    if (length <= 0.0f) {
        return 0.0f;
    }
    /* the mesh draws both sides of the ground, so the normal's sign is
     * not trusted either way: level is level */
    up = normal[1] * normal[1] / length;
    if (up < POST_LEVEL * POST_LEVEL) {
        return 0.0f;
    }
    return (float)g_post_floors[stage].percent / 100.0f;
}

/* ---- the floor's plane ---- */

/* The heights the reflecting faces of the stage's mesh lie at, each with
 * the area at it: the one with the most is the floor the reflections
 * mirror the scene in (`plane` below). A face is counted if it lies level
 * to a thousandth; its height is rounded to POST_PLANE_STEP. */
#define POST_PLANE_HEIGHTS 64
#define POST_PLANE_STEP 4.0f

static struct {
    s32 count;
    float height[POST_PLANE_HEIGHTS];
    float area[POST_PLANE_HEIGHTS];
    s32 is_set;
    float plane;
    /* the plane is the dressing's water's (port_GlPostWaterPlane), and how
     * much of it there is */
    s32 is_water;
    float water_size;
} g_post_plane;

/* A square root by Newton's steps (the window's C library is not asked
 * for its arithmetic, as in gl_lights.c). */
static float port_GlPostRoot(float s) {
    float r = s > 1.0f ? s : 1.0f;
    s32 k;

    if (s <= 0.0f) {
        return 0.0f;
    }
    /* from s itself, halving until it is near: enough for any face */
    for (k = 0; k < 48; k++) {
        r = 0.5f * (r + s / r);
    }
    return r;
}

void port_GlPostFloorsBegin(void) {
    memset(&g_post_plane, 0, sizeof(g_post_plane));
}

void port_GlPostFloorAdd(const float *a, const float *b, const float *c) {
    float u[3];
    float v[3];
    float n[3];
    float area;
    float height;
    s32 i;

    for (i = 0; i < 3; i++) {
        u[i] = b[i] - a[i];
        v[i] = c[i] - a[i];
    }
    n[0] = u[1] * v[2] - u[2] * v[1];
    n[1] = u[2] * v[0] - u[0] * v[2];
    n[2] = u[0] * v[1] - u[1] * v[0];
    area = n[0] * n[0] + n[1] * n[1] + n[2] * n[2];
    if (area <= 0.0f || n[1] * n[1] < 0.998f * area) {
        return;
    }
    height = (a[1] + b[1] + c[1]) / 3.0f;
    height =
        POST_PLANE_STEP *
        (float)(s32)(height / POST_PLANE_STEP + (height < 0.0f ? -0.5f : 0.5f));
    for (i = 0; i < g_post_plane.count; i++) {
        if (g_post_plane.height[i] == height) {
            break;
        }
    }
    if (i == g_post_plane.count) {
        if (i == POST_PLANE_HEIGHTS) {
            return;
        }
        g_post_plane.height[i] = height;
        g_post_plane.count++;
    }
    /* the cross product's length is twice the face's area */
    g_post_plane.area[i] += port_GlPostRoot(area);
}

void port_GlPostFloorsEnd(void) {
    s32 best = -1;
    s32 i;

    for (i = 0; i < g_post_plane.count; i++) {
        if (best < 0 || g_post_plane.area[i] > g_post_plane.area[best]) {
            best = i;
        }
    }
    g_post_plane.is_set = best >= 0;
    g_post_plane.plane = best >= 0 ? g_post_plane.height[best] : 0.0f;
}

void port_GlPostWaterPlane(float height, float size) {
    /* a stage whose floor reflects keeps its floor's plane; of the water,
     * the most of it */
    if (g_post_plane.is_set &&
        (!g_post_plane.is_water || size <= g_post_plane.water_size)) {
        return;
    }
    g_post_plane.is_set = TRUE;
    g_post_plane.is_water = TRUE;
    g_post_plane.water_size = size;
    g_post_plane.plane = height;
}

/* ---- the shaders ---- */

/* The passes' own size: a texel of the depth, the occlusion and the
 * reflections they read and write is this many of the picture's pixels
 * each way. At half the size they cost the card about twice as long, for
 * detail the smoothing takes away again. */
#define POST_SHRINK 3
#define POST_SHRINK_TEXT "3.0"

/* one triangle over the whole of the target */
static const char g_post_vertex_text[] =
    "#version 430 core\n"
    "void main() {\n"
    "    vec2 at = vec2(float((gl_VertexID & 1) * 4) - 1.0,\n"
    "                   float((gl_VertexID & 2) * 2) - 1.0);\n"
    "    gl_Position = vec4(at, 0.0, 1.0);\n"
    "}\n";

/* The depth at a third of the picture's size, from the buffer's (gl.h,
 * `ground_depth`): the one the passes below read. */
static const char g_post_half_text[] =
    "#version 430 core\n"
    "const float SHRINK = " POST_SHRINK_TEXT ";\n"
    "uniform sampler2D u_ground;\n"
    "uniform vec2 u_from;\n" /* the picture's corner in the buffer */
    "uniform float u_sample;\n"
    "layout(location = 0) out vec4 o_depth;\n"
    "void main() {\n"
    "    vec2 f = (floor(gl_FragCoord.xy) + 0.5) * SHRINK;\n"
    "    ivec2 at = ivec2(u_from + (f + 0.5) * u_sample);\n"
    "    o_depth = vec4(texelFetch(u_ground,\n"
    "        clamp(at, ivec2(0), textureSize(u_ground, 0) - 1), 0).r);\n"
    "}\n";

/* What every pass knows of the picture: its colours at the full size
 * (`u_full`), the kept depth, and the view the ground was drawn with. A
 * pixel of the picture `f` (its middle at a half) is a point of video
 * memory's picture, `screen_of`, which the coprocessor's projection put
 * there from a point of the view (x right, y down, z ahead, the world's
 * units):
 *
 *   screen = middle + h * (x, y / 2) / z
 *
 * (the second row of the view is halved: the console's frame is half as
 * tall as it is wide), so a pixel and its depth give its point back. The
 * picture's pixel is `u_sample` of the buffer's each way, and the picture
 * begins at `u_from` of the buffer's depth. */
#define POST_COMMON                                                                 \
    "const float SHRINK = " POST_SHRINK_TEXT ";\n"                                  \
    "uniform sampler2D u_depth;\n"                                                  \
    "uniform sampler2D u_picture;\n"                                                \
    "uniform vec2 u_full;\n"                                                        \
    "uniform float u_range;\n"                                                      \
    "uniform float u_h;\n"                                                          \
    "uniform vec2 u_middle;\n"                                                      \
    "uniform vec2 u_corner;\n"                                                      \
    "uniform vec2 u_scale;\n"                                                       \
    "uniform float u_sample;\n"                                                     \
    "uniform vec2 u_from;\n"                                                        \
    "vec2 screen_of(vec2 f) {\n"                                                    \
    "    return u_corner + (f * u_sample - 0.5) / u_scale;\n"                       \
    "}\n"                                                                           \
    "vec2 picture_of(vec3 v) {\n"                                                   \
    "    vec2 s = u_middle + vec2(v.x, 0.5 * v.y) * u_h / v.z;\n"                   \
    "    return ((s - u_corner) * u_scale + 0.5) / u_sample;\n"                     \
    "}\n" /* the depth is at a third of the picture's size: a texel                 \
           * of it is the depth at the middle of its pixels */                      \
    "ivec2 half_of(vec2 f) {\n"                                                     \
    "    return clamp(ivec2(f / SHRINK), ivec2(0), textureSize(u_depth, 0) - 1);\n" \
    "}\n"                                                                           \
    "float depth_at(vec2 f) {\n"                                                    \
    "    return texelFetch(u_depth, half_of(f), 0).r;\n"                            \
    "}\n"                                                                           \
    "vec3 view_of(vec2 f, float z) {\n"                                             \
    "    vec2 s = screen_of(f) - u_middle;\n"                                       \
    "    return vec3(s.x * z / u_h, 2.0 * s.y * z / u_h, z);\n"                     \
    "}\n"                                                                           \
    "vec3 view_at(vec2 f) {\n"                                                      \
    "    ivec2 at = half_of(f);\n"                                                  \
    "    return view_of((vec2(at) + 0.5) * SHRINK,\n"                               \
    "                   texelFetch(u_depth, at, 0).r);\n"                           \
    "}\n" /* the face's normal from the depth about the pixel, each way from        \
           * the side whose depth runs on more evenly, so that an edge does         \
           * not bend it */                                                         \
    "vec3 normal_at(vec2 f, vec3 p) {\n"                                            \
    "    float step = SHRINK;\n"                                                    \
    "    vec3 r = view_at(f + vec2(step, 0.0)) - p;\n"                              \
    "    vec3 l = p - view_at(f - vec2(step, 0.0));\n"                              \
    "    vec3 d = view_at(f + vec2(0.0, step)) - p;\n"                              \
    "    vec3 u = p - view_at(f - vec2(0.0, step));\n"                              \
    "    vec3 across = abs(r.z) < abs(l.z) ? r : l;\n"                              \
    "    vec3 down = abs(d.z) < abs(u.z) ? d : u;\n"                                \
    "    vec3 n = normalize(cross(down, across));\n"                                \
    "    return dot(n, p) > 0.0 ? -n : n;\n"                                        \
    "}\n" /* what painted the pixel last (gl_post.h): the ground's                  \
           * reflectance, or -1 where it was not the ground */                      \
    "float ground_of(float a) {\n"                                                  \
    "    if (a < 0.575 || a >= 0.9) { return -1.0; }\n"                             \
    "    return clamp((a - 0.6) / 0.25, 0.0, 1.0);\n"                               \
    "}\n" /* how much a floor of `shine` reflects seen at `facing` (the cosine      \
           * of the view to its normal): Schlick's Fresnel for a varnish, and       \
           * a floor seen from above still showing half of it */                    \
    "float shine_amount(float shine, float facing) {\n"                             \
    "    float fresnel = 0.04 + 0.96 * pow(1.0 - clamp(facing, 0.0, 1.0), 5.0);\n"  \
    "    return shine * (0.6 + 0.4 * fresnel);\n"                                   \
    "}\n"                                                                           \
    "float noise_at(vec2 f) {\n"                                                    \
    "    return fract(52.9829189\n"                                                 \
    "                 * fract(dot(f, vec2(0.06711056, 0.00583715))));\n"            \
    "}\n"

/* The world, for the passes that mirror in the floor's plane: the depth
 * of the buffer's own pixels (`full_depth`, not the third's), the view
 * into the world and back, and the plane. */
#define POST_WORLD                                                             \
    "uniform sampler2D u_ground;\n" /* the depth, the buffer's own */          \
    "uniform mat3 u_turn;\n"        /* the world into the view */              \
    "uniform mat3 u_back;\n"        /* the view into the world */              \
    "uniform vec3 u_eye;\n"                                                    \
    "uniform float u_plane;\n" /* the floor's height (y runs down) */          \
    "float full_depth(vec2 f) {\n"                                             \
    "    ivec2 at = ivec2(u_from + (floor(f) + 0.5) * u_sample);\n"            \
    "    return texelFetch(u_ground,\n"                                        \
    "        clamp(at, ivec2(0), textureSize(u_ground, 0) - 1), 0).r;\n"       \
    "}\n"                                                                      \
    "vec3 world_of(vec3 v) { return u_back * v + u_eye; }\n"                   \
    "vec3 mirrored(vec3 w) { return vec3(w.x, 2.0 * u_plane - w.y, w.z); }\n"

/* At half the size, for each pixel of ground: the occlusion (McGuire's
 * alchemy: the points about it in a disc of the world's `u_radius`, each
 * counting by how far it stands above the pixel's face over its
 * distance), 1 where nothing is hidden; and the reflection, rgb the
 * reflected colour already times how much of it shows, a how much (the
 * picture under it is kept by one less that). */
static const char g_post_light_text[] =
    "#version 430 core\n" POST_COMMON "uniform float u_radius;\n"
    "uniform float u_strength;\n"
    "uniform float u_reach;\n"
    "uniform vec3 u_up;\n" /* the world's up in the view, as a normal */
    "uniform int u_debug;\n"
    "uniform int u_ao_on;\n"
    "uniform int u_reflect_on;\n" POST_WORLD "uniform sampler2D u_mirror;\n"
    "uniform int u_planar;\n"
    "layout(location = 0) out vec4 o_ao;\n"
    "layout(location = 1) out vec4 o_reflect;\n"
    /* how much of a reflection at `f` is believed: none at the picture's
     * edges, where what it would show is off the picture, and the top's
     * band wider, where the HUD is */
    "float edge_of(vec2 f) {\n"
    "    vec2 u = f / u_full;\n"
    "    float side = smoothstep(0.0, 0.08, u.x)\n"
    "               * smoothstep(0.0, 0.08, 1.0 - u.x);\n"
    "    float tall = smoothstep(0.0, 0.06, 1.0 - u.y)\n"
    "               * smoothstep(0.08, 0.2, u.y);\n"
    "    return side * tall;\n"
    "}\n"
    "float occlusion(vec2 f, vec3 p, vec3 n) {\n"
    "    float across = u_radius * u_h / p.z * u_scale.x / u_sample;\n"
    "    if (across < 3.0 || p.z > 16000.0) { return 1.0; }\n"
    "    float turn = noise_at(gl_FragCoord.xy) * 6.2831853;\n"
    "    float sum = 0.0;\n"
    "    const int many = 6;\n"
    "    for (int i = 0; i < many; i++) {\n"
    "        float part = (float(i) + 0.5) / float(many);\n"
    "        float angle = part * 25.1327412 + turn;\n"
    "        vec2 at = f + vec2(cos(angle), sin(angle)) * across * part;\n"
    "        if (any(lessThan(at, vec2(0.0)))\n"
    "            || any(greaterThanEqual(at, u_full))) { continue; }\n"
    "        vec3 v = view_at(at) - p;\n"
    "        float vv = dot(v, v);\n"
    "        float near = max(1.0 - vv / (u_radius * u_radius * 4.0), 0.0);\n"
    "        sum += max(dot(v, n) - 0.01 * p.z, 0.0) * u_radius\n"
    "             / (vv + 0.01 * u_radius * u_radius) * near;\n"
    "    }\n"
    "    float ao = clamp(1.0 - u_strength * sum / float(many), 0.0, 1.0);\n"
    /* nothing far off: the occlusion is of what stands about the fight */
    "    return mix(ao, 1.0, smoothstep(8000.0, 16000.0, p.z));\n"
    "}\n"
    "vec4 reflection(vec2 f, vec3 p, vec3 n, float shine) {\n"
    "    vec3 eye = normalize(p);\n"
    "    vec3 r = reflect(eye, n);\n"
    "    if (dot(r, n) <= 0.0) { return vec4(0.0); }\n"
    "    float amount = shine_amount(shine, dot(-eye, n));\n"
    "    float reach = u_reach;\n"
    "    if (r.z < 0.0) { reach = min(reach, (p.z - 24.0) / -r.z); }\n"
    "    vec3 e = p + r * reach;\n"
    "    vec2 to = picture_of(e);\n"
    /* a step of about twelve pixels of the picture, at most 12 */
    "    float steps = clamp(length(to - f) / 12.0, 6.0, 12.0);\n"
    "    float jitter = noise_at(gl_FragCoord.xy);\n"
    "    float last = 0.0;\n"
    "    float found = -1.0;\n"
    "    vec2 sky = vec2(-1.0);\n"
    "    for (float i = 1.0; i <= 12.0; i += 1.0) {\n"
    "        if (i > steps) { break; }\n"
    "        float t = (i - 1.0 + jitter) / steps;\n"
    "        vec2 at = mix(f, to, t);\n"
    "        if (any(lessThan(at, vec2(0.0)))\n"
    "            || any(greaterThanEqual(at, u_full))) { break; }\n"
    /* the ray's depth there, as the picture interpolates it: one over
     * the depth runs evenly across the screen */
    "        float ray = 1.0 / mix(1.0 / p.z, 1.0 / e.z, t);\n"
    "        float scene = depth_at(at);\n"
    "        if (scene >= u_range * 0.999) { sky = at; last = t; continue; }\n"
    "        float thick = 40.0 + 0.03 * ray;\n"
    "        if (ray > scene + 2.0 && ray - scene < thick) {\n"
    "            float low = last;\n"
    "            float high = t;\n"
    "            for (int k = 0; k < 4; k++) {\n"
    "                float mid = 0.5 * (low + high);\n"
    "                float rm = 1.0 / mix(1.0 / p.z, 1.0 / e.z, mid);\n"
    "                if (rm > depth_at(mix(f, to, mid))) { high = mid; }\n"
    "                else { low = mid; }\n"
    "            }\n"
    "            found = high;\n"
    "            break;\n"
    "        }\n"
    "        last = t;\n"
    "    }\n"
    "    vec3 seen = vec3(0.0);\n"
    "    float sure = 0.0;\n"
    "    if (found >= 0.0) {\n"
    "        vec2 at = mix(f, to, found);\n"
    "        seen = texture(u_picture, at / u_full).rgb;\n"
    "        sure = edge_of(at) * (1.0 - smoothstep(0.6, 1.0, found));\n"
    "    } else {\n"
    /* Nothing met: the sky the picture shows in the reflected direction,
     * where it shows any -- the sky is the same seen from anywhere, so
     * its pixel in that direction is what the floor reflects -- or the
     * last of the open distance the ray passed over; else nothing */
    "        if (r.z > 0.02) {\n"
    "            vec2 at = picture_of(r * 1.0e6);\n"
    "            if (all(greaterThanEqual(at, vec2(0.0)))\n"
    "                && all(lessThan(at, u_full))\n"
    "                && depth_at(at) >= u_range * 0.999) {\n"
    "                seen = texture(u_picture, at / u_full).rgb;\n"
    "                sure = edge_of(at);\n"
    "            }\n"
    "        }\n"
    "        if (sure <= 0.0 && sky.x >= 0.0) {\n"
    "            seen = texture(u_picture, sky / u_full).rgb;\n"
    "            sure = 0.6 * edge_of(sky);\n"
    "        }\n"
    "    }\n"
    "    float a = amount * sure;\n"
    "    return vec4(seen * a, a);\n"
    "}\n"
    "void main() {\n"
    "    vec2 f = (floor(gl_FragCoord.xy) + 0.5) * SHRINK;\n"
    "    float shine = ground_of(texelFetch(u_picture, ivec2(f), 0).a);\n"
    "    float z = depth_at(f);\n"
    "    o_ao = vec4(1.0);\n"
    "    o_reflect = vec4(0.0);\n"
    "    if (shine < 0.0 || z >= u_range * 0.999) { return; }\n"
    "    vec3 p = view_of(floor(f) + 0.5, z);\n"
    "    vec3 n = normal_at(f, p);\n"
    "    if (u_ao_on != 0) { o_ao = vec4(occlusion(f, p, n), 0.0, 0.0, 1.0); }\n"
    /* A reflecting face lies level (gl_post.h), so its normal is the
     * world's up, not the one worked out from the depth. That one was
     * measured (RB_POST_DEBUG=normal) off by three to ten degrees on the
     * Meikyokan's floor, most near the camera, tile by tile (the mesh
     * pushes each tile's depth back by its own share so that two in one
     * plane sort as on the console, gl_stage.c) and in bands (the
     * depth's half floats keep a 2048th of it), read over three pixels
     * at a third of the size: a reflected ray turned twice that, which
     * bent the reflections into streaks. */
    "    vec3 up = u_up * (dot(u_up, p) > 0.0 ? -1.0 : 1.0);\n"
    "    if (u_debug == 4) {\n"
    "        float apart = degrees(acos(clamp(dot(n, up), -1.0, 1.0)));\n"
    "        o_reflect = vec4(apart / 10.0, apart / 30.0, 0.0, 0.0);\n"
    "        return;\n"
    "    }\n"
    /* where the floor's plane is mirrored and the mirror has the whole of
     * the pixel, the ray is not walked: what it found would not show */
    "    if (u_planar != 0 && texture(u_mirror, f / u_full).a >= 0.999) {\n"
    "        vec3 w = world_of(p);\n"
    "        if (abs(w.y - u_plane) <= 6.0 + 0.004 * z) { return; }\n"
    "    }\n"
    "    if (u_reflect_on != 0 && shine > 0.004) {\n"
    "        o_reflect = reflection(f, p, up, shine);\n"
    "    }\n"
    "}\n";

/* The scene mirrored in the floor's plane, at half the picture's size
 * (POST_MIRROR_SHRINK), by one pass of a compute shader over the
 * picture's texels at that size. Each texel whose pixel has a depth of its
 * own (the ground's mesh and its pieces, the fighters' layer, by the
 * alpha's code; the fighters the game draws itself, by the shadows'
 * casters) and stands above the plane is taken into the world, mirrored
 * in the plane, and put where the camera sees the mirrored point; at each
 * texel of the mirror the nearest that lands there wins, by an atomic
 * least of one word: its depth on a scale of logarithms in the top 14
 * bits, how much of it is the mirror's (`band`, below) in the next 3, its
 * colour in 5 bits a channel in the low 15. So what the floor shows is
 * exactly what stands over it, by the picture's own geometry: as sharp as
 * the picture at half its size, and never bent. (Two passes, the nearest
 * depth and then the nearest's colour in a buffer of its own, cost the
 * card twice as long.) */
#define POST_MIRROR_SHRINK 2
#define POST_MIRROR_SHRINK_TEXT "2.0"
/* the near end of the fighters' depth buffer, and of the mirror's scale of
 * depths, in the world's units */
#define POST_FIGHTER_NEAR 16.0f
#define POST_FIGHTER_NEAR_TEXT "16.0"

/* The mirror's word, made and read. */
#define POST_MIRROR_WORD                                                         \
    "const float STEP = " POST_MIRROR_SHRINK_TEXT ";\n"                          \
    "const float NEAR = " POST_FIGHTER_NEAR_TEXT ";\n"                           \
    "uint word_of(float z, float band, vec3 c) {\n"                              \
    "    float d = clamp(log2(z / NEAR) / log2(u_range / NEAR), 0.0, 1.0);\n"    \
    "    uvec3 q = uvec3(clamp(c, 0.0, 1.0) * 31.0 + 0.5);\n"                    \
    "    uint b = uint(clamp(band, 0.0, 1.0) * 7.0 + 0.5);\n"                    \
    "    return (uint(d * 16383.0 + 0.5) << 18) | (b << 15)\n"                   \
    "         | (q.r << 10) | (q.g << 5) | q.b;\n"                               \
    "}\n"                                                                        \
    "float depth_of_word(uint w) {\n"                                            \
    "    return NEAR * exp2(float(w >> 18) / 16383.0 * log2(u_range / NEAR));\n" \
    "}\n"                                                                        \
    "vec4 color_of_word(uint w) {\n"                                             \
    "    float b = float((w >> 15) & 7u) / 7.0;\n"                               \
    "    vec3 c = vec3(float((w >> 10) & 31u), float((w >> 5) & 31u),\n"         \
    "                  float(w & 31u)) / 31.0;\n"                                \
    "    return vec4(c, 1.0) * b;\n"                                             \
    "}\n"

static const char g_post_mirror_text[] =
    "#version 430 core\n"
    "layout(local_size_x = 8, local_size_y = 8) in;\n" POST_COMMON
        POST_WORLD POST_MIRROR_WORD
    "layout(r32ui, binding = 0) uniform uimage2D u_nearest;\n"
    /* the depth of the fighters drawn from the game's own primitives, a
     * depth buffer over the part of video memory from `u_fighter_corner`,
     * `u_fighter_scale` of its texels a pixel of video memory
     * (port_GlPostFighterDepth): its near and far are POST_FIGHTER_NEAR
     * and the depth's range */
    "uniform sampler2D u_fighters;\n"
    "uniform int u_fighters_on;\n"
    "uniform vec2 u_fighter_corner;\n"
    "uniform vec2 u_fighter_scale;\n"
    "float fighter_depth(vec2 f) {\n"
    "    ivec2 at = ivec2(floor((screen_of(f) - u_fighter_corner) * u_fighter_scale));\n"
    "    if (any(lessThan(at, ivec2(0)))\n"
    "        || any(greaterThanEqual(at, textureSize(u_fighters, 0)))) {\n"
    "        return u_range;\n"
    "    }\n"
    "    float d = texelFetch(u_fighters, at, 0).r;\n"
    "    if (d >= 1.0) { return u_range; }\n"
    "    return 2.0 * NEAR * u_range / (u_range + NEAR - (2.0 * d - 1.0) * (u_range - NEAR));\n"
    "}\n"
    "void main() {\n"
    "    ivec2 size = imageSize(u_nearest);\n"
    "    ivec2 cell = ivec2(gl_GlobalInvocationID.xy);\n"
    "    ivec2 px = ivec2((vec2(cell) + 0.5) * STEP);\n"
    "    if (any(greaterThanEqual(vec2(px), u_full))) { return; }\n"
    "    vec4 c = texelFetch(u_picture, px, 0);\n"
    /* only the ground's and the fighters' layer's pixels have a depth of
     * their own: what else painted a pixel last is not at the depth
     * there -- but for the fighters the game draws from its own
     * primitives, which the shadows' casters give a depth of their own,
     * where they are nearer than the ground */
    "    vec2 f = vec2(px) + 0.5;\n"
    "    bool coded = c.a >= 0.575 && c.a <= 0.975;\n"
    "    if (!coded && (u_fighters_on == 0 || c.a < 0.975)) { return; }\n"
    /* the HUD mixed over the ground keeps the ground's code (gl_post.h):
     * nothing of the top's band, where it is, is mirrored, and below it
     * the mirror comes in gradually; and gradually from the picture's
     * sides, past which the mirror has nothing, so that where it ends is
     * not a hard edge across the floor */
    "    vec2 u = f / u_full;\n"
    "    float band = smoothstep(0.16, 0.22, u.y)\n"
    "               * smoothstep(0.0, 0.08, u.x) * smoothstep(0.0, 0.08, 1.0 - u.x);\n"
    "    if (band <= 0.07) { return; }\n"
    "    float z = full_depth(f);\n"
    "    if (!coded) {\n"
    "        float fighter = fighter_depth(f);\n"
    "        if (fighter >= z - 4.0) { return; }\n"
    "        z = fighter;\n"
    "    }\n"
    "    if (z <= 16.0 || z >= u_range * 0.999) { return; }\n"
    "    vec3 w = world_of(view_of(f, z));\n"
    /* the floor itself, within what its depth is sure of (the mesh's
     * half floats and the push that sorts its tiles), mirrors nothing */
    "    if (u_plane - w.y < 6.0 + 0.004 * z) { return; }\n"
    "    vec3 v = u_turn * (mirrored(w) - u_eye);\n"
    "    if (v.z < 32.0) { return; }\n"
    "    ivec2 to = ivec2(floor(picture_of(v) / STEP));\n"
    "    if (any(lessThan(to, ivec2(0))) || any(greaterThanEqual(to, size))) { return; }\n"
    "    imageAtomicMin(u_nearest, to, word_of(v.z, band, c.rgb));\n"
    "}\n";

/* The mirror's words read into its colours, premultiplied by how much of
 * the texel is the mirror's, and how far each mirrored point stands from
 * the floor where it is seen (where the ray from the eye crosses the
 * plane: the floor's roughness blurs a reflection the more the farther
 * what it shows stands from it), times the same. A texel the mirror has
 * nothing for, with at least three of its eight neighbours that have
 * something, takes their mean (where the angle stretches what is
 * mirrored, the texels it lands on are a little apart). */
static const char g_post_fill_text[] =
    "#version 430 core\n" POST_COMMON POST_WORLD POST_MIRROR_WORD
    "uniform usampler2D u_nearest;\n"
    "layout(location = 0) out vec4 o_color;\n"
    "layout(location = 1) out vec4 o_far;\n"
    "float far_of(ivec2 at, uint w) {\n"
    "    vec3 d = world_of(view_of((vec2(at) + 0.5) * STEP, depth_of_word(w))) - u_eye;\n"
    "    float t = clamp((u_plane - u_eye.y) / d.y, 0.0, 1.0);\n"
    "    return length(d) * (1.0 - t);\n"
    "}\n"
    "void main() {\n"
    "    ivec2 size = textureSize(u_nearest, 0);\n"
    "    ivec2 at = ivec2(gl_FragCoord.xy);\n"
    "    uint w = texelFetch(u_nearest, at, 0).r;\n"
    "    vec4 c = vec4(0.0);\n"
    "    float far = 0.0;\n"
    "    if (w != 0xFFFFFFFFu) {\n"
    "        c = color_of_word(w);\n"
    "        far = far_of(at, w) * c.a;\n"
    "    } else {\n"
    "        vec4 sum = vec4(0.0);\n"
    "        float far_sum = 0.0;\n"
    "        float count = 0.0;\n"
    "        for (int j = -1; j <= 1; j++) {\n"
    "            for (int i = -1; i <= 1; i++) {\n"
    "                ivec2 near = clamp(at + ivec2(i, j), ivec2(0), size - 1);\n"
    "                uint n = texelFetch(u_nearest, near, 0).r;\n"
    "                if (n != 0xFFFFFFFFu) {\n"
    "                    vec4 s = color_of_word(n);\n"
    "                    sum += s;\n"
    "                    far_sum += far_of(near, n) * s.a;\n"
    "                    count += 1.0;\n"
    "                }\n"
    "            }\n"
    "        }\n"
    "        if (count >= 3.0) { c = sum / count; far = far_sum / count; }\n"
    "    }\n"
    "    o_color = c;\n"
    "    o_far = vec4(far);\n"
    "}\n";

/* The mirrored scene blurred as the floor's roughness blurs what stands
 * far from it, at a quarter of the picture's size: a disc of nine reads
 * of the half size's, `u_soft` of its texels wide, the empty ones counting
 * as nothing (the colours are premultiplied by how much of the texel is
 * the mirror's: its alpha). */
static const char g_post_soft_text[] =
    "#version 430 core\n"
    "uniform sampler2D u_source;\n"
    "uniform float u_soft;\n"
    "uniform vec2 u_size;\n" /* the target's */
    "layout(location = 0) out vec4 o_color;\n"
    "void main() {\n"
    "    vec2 source = vec2(textureSize(u_source, 0));\n"
    "    vec2 uv = gl_FragCoord.xy / u_size;\n"
    "    vec4 sum = texture(u_source, uv) * 2.0;\n"
    "    float weight = 2.0;\n"
    "    for (int i = 0; i < 8; i++) {\n"
    "        float part = (float(i) + 0.5) / 8.0;\n"
    "        float angle = float(i) * 2.39996323;\n"
    "        vec2 at = vec2(cos(angle), sin(angle)) * sqrt(part) * u_soft;\n"
    "        float w = 1.0 - 0.5 * part;\n"
    "        sum += texture(u_source, uv + at / source) * w;\n"
    "        weight += w;\n"
    "    }\n"
    "    o_color = sum / weight;\n"
    "}\n";

/* Both of half the size smoothed in one pass, three by three texels,
 * each neighbour by how near its depth is to the pixel's: nothing of a
 * fighter is smoothed into the ground behind him. The reflections spread
 * by the floor's roughness, the occlusion by a texel. */
static const char g_post_smooth_text[] =
    "#version 430 core\n" POST_COMMON "uniform sampler2D u_ao;\n"
    "uniform sampler2D u_reflect;\n"
    "uniform float u_spread;\n"
    "layout(location = 0) out vec4 o_ao;\n"
    "layout(location = 1) out vec4 o_reflect;\n"
    "void main() {\n"
    "    ivec2 size = textureSize(u_ao, 0);\n"
    "    ivec2 at = ivec2(gl_FragCoord.xy);\n"
    "    float z = texelFetch(u_depth, at, 0).r;\n"
    "    float ao = 0.0;\n"
    "    float ao_weight = 0.0;\n"
    "    vec4 shine = vec4(0.0);\n"
    "    float shine_weight = 0.0;\n"
    "    for (int j = -1; j <= 1; j++) {\n"
    "        for (int i = -1; i <= 1; i++) {\n"
    "            float w = (i == 0 ? 2.0 : 1.0) * (j == 0 ? 2.0 : 1.0);\n"
    "            ivec2 near = clamp(at + ivec2(i, j), ivec2(0), size - 1);\n"
    "            ivec2 far = clamp(at + ivec2(vec2(i, j) * u_spread),\n"
    "                              ivec2(0), size - 1);\n"
    "            if (abs(texelFetch(u_depth, near, 0).r - z) < 0.04 * z + 8.0) {\n"
    "                ao += texelFetch(u_ao, near, 0).r * w;\n"
    "                ao_weight += w;\n"
    "            }\n"
    "            if (abs(texelFetch(u_depth, far, 0).r - z) < 0.04 * z + 8.0) {\n"
    "                shine += texelFetch(u_reflect, far, 0) * w;\n"
    "                shine_weight += w;\n"
    "            }\n"
    "        }\n"
    "    }\n"
    "    o_ao = vec4(ao / max(ao_weight, 1e-4), 0.0, 0.0, 1.0);\n"
    "    o_reflect = shine / max(shine_weight, 1e-4);\n"
    "}\n";

/* Each light of the frame's list a glow at a quarter of the size: a
 * quad about the point the light is at, gone where the scene stands in
 * front of it: the whole quad where all about its middle is nearer, and
 * each pixel of it where what that pixel shows is nearer, so that the
 * halo of a torch behind a fighter is not laid over him where it reaches
 * past his edge. The list is the card's own (gl_lights.h, binding 4). */
static const char g_post_glow_vertex_text[] =
    "#version 430 core\n" POST_COMMON
    "struct PortLight { vec4 place; vec4 color; vec4 aim; vec4 extra; };\n"
    "layout(std430, binding = 4) readonly buffer PortLights {\n"
    "    PortLight port_light[];\n"
    "};\n"
    "uniform mat3 u_turn;\n" /* the world into the view */
    "uniform vec3 u_eye;\n"
    "uniform float u_gain;\n"
    "out vec2 v_corner;\n"
    "out vec3 v_color;\n"
    "out vec2 v_at;\n"   /* where the corner is, in the picture's pixels */
    "out float v_far;\n" /* nearer than this hides the light */
    "void main() {\n"
    "    int corner = gl_VertexID % 6;\n"
    "    vec2 c = vec2((corner == 1 || corner == 2 || corner == 4) ? 1.0 : -1.0,\n"
    "                  (corner == 2 || corner == 4 || corner == 5) ? 1.0 : -1.0);\n"
    "    PortLight l = port_light[gl_InstanceID];\n"
    "    vec3 v = u_turn * (l.place.xyz - u_eye);\n"
    "    v_corner = c;\n"
    "    v_color = vec3(0.0);\n"
    "    gl_Position = vec4(2.0, 2.0, 2.0, 1.0);\n"
    "    if (v.z < 64.0) { return; }\n"
    /* how large: a share of the light's reach, more for a brighter one */
    "    float bright = max(l.color.r, max(l.color.g, l.color.b)) / 128.0;\n"
    "    float size = clamp(l.place.w * 0.035, 24.0, 320.0)\n"
    "               * clamp(sqrt(bright), 0.5, 1.6);\n"
    "    vec2 middle = picture_of(v);\n"
    "    float across = size * u_h / v.z * u_scale.x / u_sample;\n"
    "    if (across < 1.0) { return; }\n"
    /* hidden by the scene: the depth about its middle, nearer than it */
    "    float seen = 0.0;\n"
    "    for (int i = 0; i < 4; i++) {\n"
    "        vec2 off = vec2((i & 1) != 0 ? 0.35 : -0.35,\n"
    "                        (i & 2) != 0 ? 0.35 : -0.35);\n"
    "        vec2 at = middle + off * across;\n"
    "        if (any(lessThan(at, vec2(0.0)))\n"
    "            || any(greaterThanEqual(at, u_full))) {\n"
    "            seen += 0.25; continue;\n"
    "        }\n"
    "        seen += depth_at(at) + size * 0.5 >= v.z ? 0.25 : 0.0;\n"
    "    }\n"
    "    if (seen <= 0.0) { return; }\n"
    "    v_color = l.color.rgb / 128.0 * u_gain * seen;\n"
    "    vec2 at = middle + c * across * 3.0;\n"
    "    v_at = at;\n"
    "    v_far = v.z - size * 0.5;\n"
    "    gl_Position = vec4(at / u_full * 2.0 - vec2(1.0), 0.0, 1.0);\n"
    "}\n";

static const char g_post_glow_fragment_text[] =
    "#version 430 core\n" POST_COMMON "in vec2 v_corner;\n"
    "in vec3 v_color;\n"
    "in vec2 v_at;\n"
    "in float v_far;\n"
    "layout(location = 0) out vec4 o_color;\n"
    "void main() {\n"
    /* what this pixel shows stands in front of the light */
    "    if (depth_at(v_at) < v_far) { discard; }\n"
    "    float r = length(v_corner) * 3.0;\n"
    /* a bright middle and a wide soft fall */
    "    float glow = exp(-r * r * 2.0) + 0.25 * exp(-r * r * 0.35);\n"
    "    o_color = vec4(v_color * glow, 1.0);\n"
    "}\n";

/* The glow made smaller (`u_down` 1: four of the larger read as one,
 * smoothed) or brought back up (0: the smaller read smoothly) into the
 * target bound, which adds what it draws on the way up. */
static const char g_post_scale_text[] =
    "#version 430 core\n"
    "uniform sampler2D u_source;\n"
    "uniform int u_down;\n"
    "layout(location = 0) out vec4 o_color;\n"
    "void main() {\n"
    "    vec2 size = vec2(textureSize(u_source, 0));\n"
    "    vec2 here = gl_FragCoord.xy;\n"
    "    vec2 at = u_down != 0 ? here * 2.0 / size : here * 0.5 / size;\n"
    "    vec2 one = u_down != 0 ? 1.0 / size : 0.5 / size;\n"
    "    o_color = 0.25 * (texture(u_source, at + vec2(-one.x, -one.y))\n"
    "                    + texture(u_source, at + vec2(one.x, -one.y))\n"
    "                    + texture(u_source, at + vec2(-one.x, one.y))\n"
    "                    + texture(u_source, at + vec2(one.x, one.y)));\n"
    "}\n";

/* The lot over the picture, into a buffer of its own: the occlusion
 * darkening the ground's
 * light from all about, the reflections over it, and the glow added.
 * The alpha is the picture's own. */
static const char g_post_lay_text[] =
    "#version 430 core\n" POST_COMMON "uniform sampler2D u_ao;\n"
    "uniform sampler2D u_reflect;\n"
    "uniform sampler2D u_glow;\n"
    "uniform int u_ao_on;\n"
    "uniform int u_reflect_on;\n"
    "uniform int u_glow_on;\n"
    "uniform float u_ambient;\n"
    "uniform int u_debug;\n" POST_WORLD "uniform sampler2D u_mirror;\n"
    "uniform sampler2D u_mirror_soft;\n"
    "uniform sampler2D u_mirror_far;\n"
    "uniform int u_planar;\n"
    "uniform float u_rough;\n"
    "uniform float u_ripple;\n"
    "layout(location = 0) out vec4 o_color;\n"
    /* The mirror's reflection of a pixel of the floor's plane, rgb times
     * how much shows and a how much, and `cover` how much of the pixel
     * the mirror has anything for. Off the plane (a step, a raised stage,
     * a sloping face) nothing: the reflection on the screen has those. */
    "vec4 planar(vec2 f, float shine, vec2 grain, out float cover) {\n"
    "    cover = 0.0;\n"
    /* on the plane: the point of the third's texel, its own middle and
     * depth (one read of a small buffer, not the picture's own) */
    "    vec3 p = view_at(f);\n"
    "    if (p.z >= u_range * 0.999) { return vec4(0.0); }\n"
    "    vec3 w = world_of(p);\n"
    "    if (abs(w.y - u_plane) > 6.0 + 0.004 * p.z) { return vec4(0.0); }\n"
    /* the floor is not glass: the slope of its own grain in the picture
     * (`grain`) turns its normal a little, and the mirror is read that
     * much aside */
    "    vec2 uv = (f + grain * u_ripple) / u_full;\n"
    "    vec4 sharp = texture(u_mirror, uv);\n"
    "    vec4 soft = texture(u_mirror_soft, uv);\n"
    "    if (sharp.a + soft.a <= 0.0) { return vec4(0.0); }\n"
    /* the rougher the floor and the farther what it shows stands from
     * it, the blurrier: a fighter's feet are sharp in it, his head soft;
     * and past the mirror's edges only the blurred */
    "    float far = texture(u_mirror_far, uv).r / max(sharp.a, 1e-3);\n"
    "    float blur = clamp(far * u_rough / 400.0, 0.0, 1.0);\n"
    "    vec4 m = mix(sharp, soft, max(blur, 1.0 - sharp.a));\n"
    "    float amount = shine_amount(shine, abs(normalize(w - u_eye).y));\n"
    "    cover = clamp(m.a, 0.0, 1.0);\n"
    "    return vec4(m.rgb, m.a) * amount;\n"
    "}\n"
    "void main() {\n"
    "    vec2 f = gl_FragCoord.xy;\n"
    "    vec4 c = texelFetch(u_picture, ivec2(f), 0);\n"
    "    vec2 uv = f / u_full;\n"
    "    float shine = ground_of(c.a);\n"
    /* the slope of the picture's brightness, from the pixels beside it in
     * its quad, taken where every pixel of the quad still runs */
    "    float l = dot(c.rgb, vec3(0.3, 0.59, 0.11));\n"
    "    vec2 grain = 2.0 * vec2(dFdx(l), dFdy(l));\n"
    "    if (shine >= 0.0) {\n"
    "        if (u_ao_on != 0) {\n"
    "            float ao = texture(u_ao, uv).r;\n"
    "            c.rgb *= mix(1.0, ao, u_ambient);\n"
    "            if (u_debug == 1) { c.rgb = vec3(ao); }\n"
    "        }\n"
    "        if (u_reflect_on != 0) {\n"
    "            vec4 r = texture(u_reflect, uv);\n"
    "            float cover = 0.0;\n"
    "            vec4 m = u_planar != 0 ? planar(f, shine, grain, cover) : vec4(0.0);\n"
    /* the mirror where it has the scene, the reflection on the screen
     * where it has none (what the mirror cannot have: the sky, the
     * distance, what has no depth) */
    "            r *= 1.0 - cover;\n"
    "            c.rgb = c.rgb * (1.0 - r.a - m.a) + r.rgb + m.rgb;\n"
    "            if (u_debug == 2 || u_debug == 4) { c.rgb = r.rgb + m.rgb + vec3(0.0, 0.0, r.a * 0.3); }\n"
    "            if (u_debug == 7) { c.rgb = vec3(cover, r.a * 2.0, m.a * 2.0); }\n"
    "        }\n"
    "    }\n"
    "    if (u_glow_on != 0) {\n"
    "        c.rgb += texture(u_glow, uv).rgb;\n"
    "    }\n"
    "    if (u_debug == 3) {\n"
    "        float z = depth_at(f);\n"
    "        c.rgb = vec3(fract(z / 1000.0), z / u_range, max(shine, 0.0));\n"
    "    }\n"
    "    if (u_debug == 5 && c.a > 0.575 && c.a < 0.975) {\n"
    "        float dy = world_of(view_of(f, full_depth(f))).y - u_plane;\n"
    "        c.rgb = vec3(clamp(0.5 + dy / 40.0, 0.0, 1.0),\n"
    "                     clamp(-dy / 400.0, 0.0, 1.0), float(u_planar));\n"
    "    }\n"
    "    if (u_debug == 6 && u_planar != 0) {\n"
    "        vec4 m = texture(u_mirror, uv);\n"
    "        c.rgb = m.rgb + vec3(0.0, 0.0, 0.25) * (1.0 - m.a);\n"
    "    }\n"
    "    o_color = vec4(clamp(c.rgb, 0.0, 1.0), c.a);\n"
    "}\n";

/* ---- the state ---- */

/* the world's units of the occlusion's reach, how dark it is, and the
 * reach of a reflected ray */
#define AO_RADIUS 110.0f
#define AO_STRENGTH 2.6f
#define REFLECT_REACH 6000.0f
/* how bright a light's glow is, of its own colour */
#define GLOW_GAIN 0.55f
/* the glow's levels: a quarter of the size, an eighth, a sixteenth */
#define GLOW_LEVELS 3

typedef struct {
    GLuint program;
    GLint at_depth;
    GLint at_picture;
    GLint at_full;
    GLint at_range;
    GLint at_h;
    GLint at_middle;
    GLint at_corner;
    GLint at_scale;
    GLint at_sample;
    GLint at_from;
} PostProgram;

typedef struct {
    GLuint texture;
    GLuint framebuffer;
    s32 w;
    s32 h;
} PostTarget;

/* Where a program keeps what POST_WORLD declares. */
typedef struct {
    GLint ground;
    GLint turn;
    GLint back;
    GLint eye;
    GLint plane;
} PostWorld;

/* A drawing of the ground: its view, and the line of video memory its
 * drawing area begins at. */
typedef struct {
    s32 is_set;
    float rotation[9];
    float camera[3];
    float h;
    float middle[2];
    s32 top;
    /* the lights it was lit by, as the card had them (gl_lights.h) */
    s32 lights;
    u8 light[PORT_GL_LIGHTS_MAX * PORT_GL_LIGHTS_CARD_SIZE];
    /* the depth of the fighters the game draws from its own primitives
     * (port_GlPostFighterDepth): which of the two buffers, whether it was
     * drawn, and the part of video memory it covers (its corner and its
     * size, in video memory's pixels) */
    s32 fighter_slot;
    s32 has_fighters;
    float fighter_corner[2];
    float fighter_size[2];
} PostView;

static struct {
    s32 is_started;
    char problem[200];
    s32 is_reflect;
    s32 is_ao;
    s32 is_glow;
    s32 is_tone;
    s32 debug;
    GLuint array;
    GLuint half_program;
    GLint half_at_ground;
    GLint half_at_from;
    GLint half_at_sample;
    PostTarget half;
    PostProgram light;
    GLint light_at_radius;
    GLint light_at_strength;
    GLint light_at_reach;
    GLint light_at_ao_on;
    GLint light_at_reflect_on;
    GLint light_at_up;
    GLint light_at_debug;
    GLint light_at_mirror;
    GLint light_at_planar;
    PostWorld light_world;
    /* the scene mirrored in the floor's plane, and blurred */
    PostProgram mirror;
    GLint mirror_at_fighters;
    GLint mirror_at_fighters_on;
    GLint mirror_at_fighter_corner;
    GLint mirror_at_fighter_scale;
    PostWorld mirror_world;
    PostProgram fill;
    PostWorld fill_world;
    GLint fill_at_nearest;
    GLuint soft_program;
    GLint soft_at_source;
    GLint soft_at_soft;
    GLint soft_at_size;
    PostProgram smooth;
    GLint smooth_at_ao;
    GLint smooth_at_reflect;
    GLint smooth_at_spread;
    PostProgram glow;
    GLint glow_at_turn;
    GLint glow_at_eye;
    GLint glow_at_gain;
    GLuint scale_program;
    GLint scale_at_source;
    GLint scale_at_down;
    PostProgram lay;
    GLint lay_at_ao;
    GLint lay_at_reflect;
    GLint lay_at_glow;
    GLint lay_at_ao_on;
    GLint lay_at_reflect_on;
    GLint lay_at_glow_on;
    GLint lay_at_ambient;
    GLint lay_at_debug;
    PostWorld lay_world;
    GLint lay_at_mirror;
    GLint lay_at_mirror_soft;
    GLint lay_at_mirror_far;
    GLint lay_at_planar;
    GLint lay_at_rough;
    GLint lay_at_ripple;
    /* the targets, at the size they were last made: the occlusion and
     * the reflections twice over (for the smoothing), each pair drawn
     * through one framebuffer */
    s32 w;
    s32 h;
    PostTarget ao_one;
    PostTarget reflect_one;
    PostTarget ao_two;
    PostTarget reflect_two;
    GLuint pair_one;
    GLuint pair_two;
    PostTarget glow_level[GLOW_LEVELS];
    /* the mirror, at half the size: the nearest's word at each texel;
     * its colours and how far each stands from the floor, holes filled,
     * drawn together; and its colours blurred, at a quarter */
    PostTarget mirror_nearest;
    PostTarget mirror_color;
    PostTarget mirror_far;
    PostTarget mirror_soft;
    GLuint mirror_framebuffer;
    /* the fighters' depth, one for each of the two drawings
     * (PostView.fighter_slot), the buffer's own size */
    GLuint fighter_depth[2];
    GLuint fighter_framebuffer[2];
    s32 fighter_w[2];
    s32 fighter_h[2];
    /* what the passes lay the picture into, the picture's size (it and
     * the picture change places after: port_GlPostRun) */
    PostTarget out;
    /* the drawing on show's lights, sent again for the glow */
    GLuint light_buffer;
    /* The view the passes read the depth with: the one the picture on
     * show was drawn with (port_GlPostRun chooses it) */
    float rotation[9];
    float camera[3];
    float h_of;
    float middle[2];
    /* its turn and the turn's inverse, row after row, and the world's
     * up in the view (port_GlPostTurns); whether the floor's plane is
     * mirrored in this picture */
    float turn[9];
    float back[9];
    float up[3];
    s32 is_planar;
    /* The views of the ground's last two drawings (port_GlPostNoteView):
     * the one being drawn, and the one before it. The game draws one frame
     * while it shows the last, in another part of video memory, so the
     * picture shown may be either. */
    PostView live;
    PostView kept;
    /* the view `live` was when the ground's mesh began to be drawn again
     * (port_GlPostBeforeMesh), until the new one is noted */
    PostView was;
    /* the ground's mesh drawn since the frame began (port_GlPostNewFrame) */
    s32 is_noted;
    /* the depth the passes read (gl.h) */
    GLuint depth;
    u32 pictures;
} g_post;

/* ---- starting ---- */

static GLuint port_GlPostShader(GLenum kind, const char *text) {
    GLuint shader = glCreateShader(kind);
    GLint ok = 0;

    glShaderSource(shader, 1, &text, NULL);
    glCompileShader(shader);
    glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[400];

        log[0] = 0;
        glGetShaderInfoLog(shader, sizeof(log), NULL, log);
        snprintf(g_post.problem, sizeof(g_post.problem),
                 "the lighting's passes' shader: %.150s", log);
        return 0;
    }
    return shader;
}

/* A program of a vertex and a fragment shader, or, with no vertex
 * shader's text, of `fragment_text` as a compute shader. */
static GLuint port_GlPostLink(
    const char *vertex_text, const char *fragment_text) {
    GLuint vertex = vertex_text != NULL
                        ? port_GlPostShader(GL_VERTEX_SHADER, vertex_text)
                        : 1;
    GLuint fragment = port_GlPostShader(
        vertex_text != NULL ? GL_FRAGMENT_SHADER : GL_COMPUTE_SHADER,
        fragment_text);
    GLuint program;
    GLint ok = 0;

    if (vertex == 0 || fragment == 0) {
        return 0;
    }
    program = glCreateProgram();
    if (vertex_text != NULL) {
        glAttachShader(program, vertex);
    }
    glAttachShader(program, fragment);
    glLinkProgram(program);
    glGetProgramiv(program, GL_LINK_STATUS, &ok);
    if (!ok) {
        snprintf(g_post.problem, sizeof(g_post.problem),
                 "the lighting's passes' shaders do not link");
        return 0;
    }
    return program;
}

static s32 port_GlPostMake(
    PostProgram *one, const char *vertex_text, const char *fragment_text) {
    one->program = port_GlPostLink(vertex_text, fragment_text);
    if (one->program == 0) {
        return FALSE;
    }
    one->at_depth = glGetUniformLocation(one->program, "u_depth");
    one->at_picture = glGetUniformLocation(one->program, "u_picture");
    one->at_full = glGetUniformLocation(one->program, "u_full");
    one->at_range = glGetUniformLocation(one->program, "u_range");
    one->at_h = glGetUniformLocation(one->program, "u_h");
    one->at_middle = glGetUniformLocation(one->program, "u_middle");
    one->at_corner = glGetUniformLocation(one->program, "u_corner");
    one->at_scale = glGetUniformLocation(one->program, "u_scale");
    one->at_sample = glGetUniformLocation(one->program, "u_sample");
    one->at_from = glGetUniformLocation(one->program, "u_from");
    return TRUE;
}

static void port_GlPostWorldAt(PostWorld *world, GLuint program) {
    world->ground = glGetUniformLocation(program, "u_ground");
    world->turn = glGetUniformLocation(program, "u_turn");
    world->back = glGetUniformLocation(program, "u_back");
    world->eye = glGetUniformLocation(program, "u_eye");
    world->plane = glGetUniformLocation(program, "u_plane");
}

const char *port_GlPostStart(void *(*getproc)(const char *name)) {
    const char *debug = getenv("RB_POST_DEBUG");

#define GL(result, name, args)                                                 \
    name = (result(*) args)getproc(#name);                                     \
    if (name == NULL) {                                                        \
        snprintf(                                                              \
            g_post.problem, sizeof(g_post.problem), "OpenGL has no " #name);   \
        return g_post.problem;                                                 \
    }
    PORT_GL_POST_FUNCTIONS
#undef GL
    g_post.scale_program =
        port_GlPostLink(g_post_vertex_text, g_post_scale_text);
    g_post.half_program = port_GlPostLink(g_post_vertex_text, g_post_half_text);
    g_post.soft_program = port_GlPostLink(g_post_vertex_text, g_post_soft_text);
    if (g_post.scale_program == 0 || g_post.half_program == 0 ||
        g_post.soft_program == 0 ||
        !port_GlPostMake(
            &g_post.light, g_post_vertex_text, g_post_light_text) ||
        !port_GlPostMake(
            &g_post.smooth, g_post_vertex_text, g_post_smooth_text) ||
        !port_GlPostMake(
            &g_post.glow, g_post_glow_vertex_text, g_post_glow_fragment_text) ||
        !port_GlPostMake(&g_post.lay, g_post_vertex_text, g_post_lay_text) ||
        !port_GlPostMake(&g_post.mirror, NULL, g_post_mirror_text) ||
        !port_GlPostMake(&g_post.fill, g_post_vertex_text, g_post_fill_text)) {
        return g_post.problem;
    }
#define AT(member, program, name)                                              \
    g_post.member = glGetUniformLocation(program, name)
    AT(half_at_ground, g_post.half_program, "u_ground");
    AT(half_at_from, g_post.half_program, "u_from");
    AT(half_at_sample, g_post.half_program, "u_sample");
    AT(light_at_radius, g_post.light.program, "u_radius");
    AT(light_at_strength, g_post.light.program, "u_strength");
    AT(light_at_reach, g_post.light.program, "u_reach");
    AT(light_at_ao_on, g_post.light.program, "u_ao_on");
    AT(light_at_reflect_on, g_post.light.program, "u_reflect_on");
    AT(smooth_at_ao, g_post.smooth.program, "u_ao");
    AT(smooth_at_reflect, g_post.smooth.program, "u_reflect");
    AT(smooth_at_spread, g_post.smooth.program, "u_spread");
    AT(glow_at_turn, g_post.glow.program, "u_turn");
    AT(glow_at_eye, g_post.glow.program, "u_eye");
    AT(glow_at_gain, g_post.glow.program, "u_gain");
    AT(scale_at_source, g_post.scale_program, "u_source");
    AT(scale_at_down, g_post.scale_program, "u_down");
    AT(lay_at_ao, g_post.lay.program, "u_ao");
    AT(lay_at_reflect, g_post.lay.program, "u_reflect");
    AT(lay_at_glow, g_post.lay.program, "u_glow");
    AT(lay_at_ao_on, g_post.lay.program, "u_ao_on");
    AT(lay_at_reflect_on, g_post.lay.program, "u_reflect_on");
    AT(lay_at_glow_on, g_post.lay.program, "u_glow_on");
    AT(lay_at_ambient, g_post.lay.program, "u_ambient");
    AT(lay_at_debug, g_post.lay.program, "u_debug");
    AT(light_at_up, g_post.light.program, "u_up");
    AT(light_at_debug, g_post.light.program, "u_debug");
    AT(light_at_mirror, g_post.light.program, "u_mirror");
    AT(light_at_planar, g_post.light.program, "u_planar");
    AT(mirror_at_fighters, g_post.mirror.program, "u_fighters");
    AT(mirror_at_fighters_on, g_post.mirror.program, "u_fighters_on");
    AT(mirror_at_fighter_corner, g_post.mirror.program, "u_fighter_corner");
    AT(mirror_at_fighter_scale, g_post.mirror.program, "u_fighter_scale");
    AT(fill_at_nearest, g_post.fill.program, "u_nearest");
    AT(soft_at_size, g_post.soft_program, "u_size");
    AT(soft_at_source, g_post.soft_program, "u_source");
    AT(soft_at_soft, g_post.soft_program, "u_soft");
    AT(lay_at_mirror, g_post.lay.program, "u_mirror");
    AT(lay_at_mirror_soft, g_post.lay.program, "u_mirror_soft");
    AT(lay_at_mirror_far, g_post.lay.program, "u_mirror_far");
    AT(lay_at_planar, g_post.lay.program, "u_planar");
    AT(lay_at_rough, g_post.lay.program, "u_rough");
    AT(lay_at_ripple, g_post.lay.program, "u_ripple");
#undef AT
    port_GlPostWorldAt(&g_post.mirror_world, g_post.mirror.program);
    port_GlPostWorldAt(&g_post.fill_world, g_post.fill.program);
    port_GlPostWorldAt(&g_post.lay_world, g_post.lay.program);
    port_GlPostWorldAt(&g_post.light_world, g_post.light.program);
    glGenVertexArrays(1, &g_post.array);
    /* RB_POST_DEBUG=ao, =reflect or =depth: the occlusion, the
     * reflections alone, or the depth (red its thousands, green all of
     * it) and the reflectance (blue), in the place of the picture: how
     * they were looked at. And for the mirror: =normal, on the ground,
     * how far the normal worked out from the depth turns from the
     * world's up (red all of it at ten degrees, green at thirty);
     * =height, on the ground and the fighters, the height above
     * the floor's plane (red half at the plane, 20 units a quarter;
     * green 400 units above), blue where the plane is mirrored; =mirror
     * the mirror itself, blue where it has nothing; =cover how much of
     * each pixel the mirror covers (red), and the reflection on the
     * screen's (green) and the mirror's (blue) shares */
    g_post.debug =
        debug == NULL                   ? 0
        : strcmp(debug, "ao") == 0      ? 1
        : strcmp(debug, "reflect") == 0 ? 2
        : strcmp(debug, "depth") == 0   ? 3
        : strcmp(debug, "normal") == 0  ? 4
        : strcmp(debug, "height") == 0  ? 5
        : strcmp(debug, "mirror") == 0  ? 6
        : strcmp(debug, "cover") == 0   ? 7
                                        : 0;
    g_post.is_started = TRUE;
    return NULL;
}

void port_GlPostSetOn(s32 reflections, s32 occlusion, s32 bloom, s32 tonemap) {
    g_post.is_reflect = reflections != 0;
    g_post.is_ao = occlusion != 0;
    g_post.is_glow = bloom != 0;
    g_post.is_tone = tonemap != 0;
}

s32 port_GlPostToneIsOn(void) { return g_post.is_tone; }

s32 port_GlPostNeedsDepth(void) {
    return g_post.is_started &&
           (g_post.is_reflect || g_post.is_ao || g_post.is_glow);
}

const char *port_GlPostProblem(void) {
    return g_post.problem[0] != 0 ? g_post.problem : NULL;
}

u32 port_GlPostPictures(void) { return g_post.pictures; }

/* ---- the targets ---- */

static s32 port_GlPostTarget(PostTarget *target, s32 w, s32 h, GLenum format,
                             GLenum layout, GLenum type, GLenum filter) {
    if (target->texture == 0) {
        glGenTextures(1, &target->texture);
        glGenFramebuffers(1, &target->framebuffer);
    }
    glBindTexture(GL_TEXTURE_2D, target->texture);
    glTexImage2D(GL_TEXTURE_2D, 0, (GLint)format, w, h, 0, layout, type, NULL);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, (GLint)filter);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, (GLint)filter);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindFramebuffer(GL_FRAMEBUFFER, target->framebuffer);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D,
                           target->texture, 0);
    target->w = w;
    target->h = h;
    return glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
}

/* A framebuffer drawing into the occlusion and the reflections at once. */
static s32 port_GlPostPair(
    GLuint *framebuffer, const PostTarget *ao, const PostTarget *shine) {
    static const GLenum both[2] = {GL_COLOR_ATTACHMENT0, GL_COLOR_ATTACHMENT1};

    if (*framebuffer == 0) {
        glGenFramebuffers(1, framebuffer);
    }
    glBindFramebuffer(GL_FRAMEBUFFER, *framebuffer);
    glFramebufferTexture2D(
        GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, ao->texture, 0);
    glFramebufferTexture2D(
        GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT1, GL_TEXTURE_2D, shine->texture, 0);
    glDrawBuffers(2, both);
    return glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
}

/* The mirror's targets, at half the picture's size: the words the compute
 * shader writes, their colours and distances with the holes filled (drawn
 * into at once), and the colours blurred at a quarter. */
static s32 port_GlPostMirrorTargets(s32 w, s32 h) {
    static const GLenum both[2] = {GL_COLOR_ATTACHMENT0, GL_COLOR_ATTACHMENT1};
    s32 mirror_w = (w + POST_MIRROR_SHRINK - 1) / POST_MIRROR_SHRINK;
    s32 mirror_h = (h + POST_MIRROR_SHRINK - 1) / POST_MIRROR_SHRINK;
    s32 ok = TRUE;

    ok = ok &&
         port_GlPostTarget(&g_post.mirror_nearest, mirror_w, mirror_h, GL_R32UI,
                           GL_RED_INTEGER, GL_UNSIGNED_INT, GL_NEAREST);
    ok = ok &&
         port_GlPostTarget(&g_post.mirror_color, mirror_w, mirror_h, GL_RGBA8,
                           GL_RGBA, GL_UNSIGNED_BYTE, GL_LINEAR);
    ok = ok && port_GlPostTarget(&g_post.mirror_far, mirror_w, mirror_h,
                                 GL_R16F, GL_RED, GL_FLOAT, GL_LINEAR);
    ok = ok && port_GlPostTarget(
                   &g_post.mirror_soft, (mirror_w + 1) / 2, (mirror_h + 1) / 2,
                   GL_RGBA8, GL_RGBA, GL_UNSIGNED_BYTE, GL_LINEAR);
    if (!ok) {
        return FALSE;
    }
    if (g_post.mirror_framebuffer == 0) {
        glGenFramebuffers(1, &g_post.mirror_framebuffer);
    }
    glBindFramebuffer(GL_FRAMEBUFFER, g_post.mirror_framebuffer);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D,
                           g_post.mirror_color.texture, 0);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT1, GL_TEXTURE_2D,
                           g_post.mirror_far.texture, 0);
    glDrawBuffers(2, both);
    return glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
}

static s32 port_GlPostTargets(s32 w, s32 h) {
    s32 half_w = (w + POST_SHRINK - 1) / POST_SHRINK;
    s32 half_h = (h + POST_SHRINK - 1) / POST_SHRINK;
    s32 ok = TRUE;
    s32 i;

    if (w == g_post.w && h == g_post.h) {
        return TRUE;
    }
    ok = ok && port_GlPostTarget(&g_post.half, half_w, half_h, GL_R32F, GL_RED,
                                 GL_FLOAT, GL_NEAREST);
    ok = ok && port_GlPostTarget(&g_post.ao_one, half_w, half_h, GL_R8, GL_RED,
                                 GL_UNSIGNED_BYTE, GL_LINEAR);
    ok = ok && port_GlPostTarget(&g_post.ao_two, half_w, half_h, GL_R8, GL_RED,
                                 GL_UNSIGNED_BYTE, GL_LINEAR);
    ok = ok && port_GlPostTarget(&g_post.reflect_one, half_w, half_h,
                                 GL_RGBA16F, GL_RGBA, GL_FLOAT, GL_LINEAR);
    ok = ok && port_GlPostTarget(&g_post.reflect_two, half_w, half_h,
                                 GL_RGBA16F, GL_RGBA, GL_FLOAT, GL_LINEAR);
    ok = ok &&
         port_GlPostPair(&g_post.pair_one, &g_post.ao_one, &g_post.reflect_one);
    ok = ok &&
         port_GlPostPair(&g_post.pair_two, &g_post.ao_two, &g_post.reflect_two);
    for (i = 0; i < GLOW_LEVELS; i++) {
        s32 shift = i + 2;

        ok = ok && port_GlPostTarget(
                       &g_post.glow_level[i], (w + (1 << shift) - 1) >> shift,
                       (h + (1 << shift) - 1) >> shift, GL_RGBA16F, GL_RGBA,
                       GL_FLOAT, GL_LINEAR);
    }
    ok = ok && port_GlPostTarget(&g_post.out, w, h, GL_RGBA8, GL_RGBA,
                                 GL_UNSIGNED_BYTE, GL_NEAREST);
    ok = ok && port_GlPostMirrorTargets(w, h);
    if (!ok) {
        snprintf(g_post.problem, sizeof(g_post.problem),
                 "the lighting's passes' buffers cannot be drawn into");
        g_post.w = 0;
        g_post.h = 0;
        return FALSE;
    }
    g_post.w = w;
    g_post.h = h;
    return TRUE;
}

static void port_GlPostDraw(void) {
    glBindVertexArray(g_post.array);
    glDrawArrays(GL_TRIANGLES, 0, 3);
}

/* ---- the drawings of the ground ---- */

void port_GlPostNoteView(const float *rotation, const float *camera, float h,
                         float middle_x, float middle_y, s32 top) {
    PostView *view = &g_post.live;

    if (g_post.was.is_set && g_post.was.top != top) {
        g_post.kept = g_post.was;
    }
    g_post.was.is_set = FALSE;
    /* the fighters' depth of the drawing kept stays its own */
    view->fighter_slot = 1 - g_post.kept.fighter_slot;
    memcpy(view->rotation, rotation, sizeof(view->rotation));
    memcpy(view->camera, camera, sizeof(view->camera));
    view->h = h;
    view->middle[0] = middle_x;
    view->middle[1] = middle_y;
    view->top = top;
    view->is_set = h > 0.0f;
    g_post.is_noted = TRUE;
    {
        s32 count = 0;
        const void *card = port_GlLightsCard(&count);

        view->lights = count > PORT_GL_LIGHTS_MAX ? PORT_GL_LIGHTS_MAX : count;
        if (view->lights > 0) {
            memcpy(view->light, card,
                   (size_t)view->lights * PORT_GL_LIGHTS_CARD_SIZE);
        }
    }
}

static s32 port_GlPostIsOn(void) {
    return g_post.is_started &&
           (g_post.is_reflect || g_post.is_ao || g_post.is_glow);
}

/* Whether `view`'s drawing is the picture on show. */
static s32 port_GlPostShows(
    const PostView *view, const PortGlPicture *picture) {
    s32 apart = picture->display_y - view->top;

    return view->is_set && apart * 2 > -picture->display_h &&
           apart * 2 < picture->display_h;
}

void port_GlPostBeforeMesh(const PortGlPicture *picture) {
    (void)picture;
    /* Which view the drawing before becomes is known when this one's is
     * noted: a drawing of the other part of video memory keeps it as the
     * one before (the game draws one frame while it shows the last), and
     * a drawing over the same part replaces it. A picture between two
     * frames (gl_tween.h) draws the frame on show again, over its own
     * part: kept as the one before, it pushed out the view of the part the
     * game's own picture shows next, and that picture -- the one shown
     * where no picture between two frames can be drawn, at a cut -- had
     * none of these passes. */
    g_post.was = g_post.live;
    g_post.live.is_set = FALSE;
    g_post.live.has_fighters = FALSE;
}

void port_GlPostFighterDepth(void) {
    PostView *view = &g_post.live;
    PortGlPicture picture;
    GLfloat matrix[16];
    float turn[9];
    float row[4][3];
    float constant[4];
    float wide;
    float tall;
    float near_to_far;
    float a;
    float b;
    s32 slot = view->fighter_slot & 1;
    s32 w;
    s32 h;
    s32 i;
    s32 k;

    view->has_fighters = FALSE;
    if (!g_post.is_started || !g_post.is_reflect || !view->is_set) {
        return;
    }
    port_GlGetPicture(&picture);
    if (picture.display_w <= 0 || picture.display_h <= 0) {
        return;
    }
    /* the drawing's part of video memory, the picture's band in, at half
     * the buffer's size: the mirror is at half the picture's */
    wide = (float)(picture.display_w + 2 * picture.margin);
    tall = (float)picture.display_h;
    w = (s32)(wide * (float)picture.draw_x + 1.0f) / 2;
    h = (s32)(tall * (float)picture.draw_y + 1.0f) / 2;
    if (w <= 0 || h <= 0) {
        return;
    }
    if (g_post.fighter_framebuffer[slot] == 0) {
        glGenTextures(1, &g_post.fighter_depth[slot]);
        glGenFramebuffers(1, &g_post.fighter_framebuffer[slot]);
    }
    if (g_post.fighter_w[slot] != w || g_post.fighter_h[slot] != h) {
        glBindTexture(GL_TEXTURE_2D, g_post.fighter_depth[slot]);
        glTexImage2D(GL_TEXTURE_2D, 0, (GLint)GL_DEPTH_COMPONENT32F, w, h, 0,
                     GL_DEPTH_COMPONENT, GL_FLOAT, NULL);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glBindFramebuffer(GL_FRAMEBUFFER, g_post.fighter_framebuffer[slot]);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT,
                               GL_TEXTURE_2D, g_post.fighter_depth[slot], 0);
        {
            static const GLenum none = GL_NONE;

            glDrawBuffers(1, &none);
        }
        glReadBuffer(GL_NONE);
        g_post.fighter_w[slot] = w;
        g_post.fighter_h[slot] = h;
    }
    view->fighter_corner[0] = (float)picture.display_x;
    view->fighter_corner[1] = (float)view->top;
    view->fighter_size[0] = wide;
    view->fighter_size[1] = tall;
    /* The world into the depth buffer's clip space: the coprocessor's
     * projection of the drawing's own view (screen = middle + h * (x, y /
     * 2) / z, in video memory's pixels), each row linear in the point of
     * the view and so in the world's, with an ordinary perspective depth
     * from POST_FIGHTER_NEAR to the depth's range. */
    for (i = 0; i < 9; i++) {
        turn[i] = view->rotation[i] / 4096.0f;
    }
    near_to_far = PORT_GL_DEPTH_RANGE - POST_FIGHTER_NEAR;
    a = (PORT_GL_DEPTH_RANGE + POST_FIGHTER_NEAR) / near_to_far;
    b = -2.0f * PORT_GL_DEPTH_RANGE * POST_FIGHTER_NEAR / near_to_far;
    for (k = 0; k < 3; k++) {
        float x = turn[k];
        float y = turn[3 + k];
        float z = turn[6 + k];

        row[0][k] = 2.0f / wide *
                        ((view->middle[0] - view->fighter_corner[0]) * z +
                         view->h * x) -
                    z;
        row[1][k] = 2.0f / tall *
                        ((view->middle[1] - view->fighter_corner[1]) * z +
                         0.5f * view->h * y) -
                    z;
        row[2][k] = a * z;
        row[3][k] = z;
    }
    for (i = 0; i < 4; i++) {
        constant[i] =
            -(row[i][0] * view->camera[0] + row[i][1] * view->camera[1] +
              row[i][2] * view->camera[2]);
    }
    constant[2] += b;
    /* a column after a column */
    for (i = 0; i < 4; i++) {
        for (k = 0; k < 3; k++) {
            matrix[k * 4 + i] = row[i][k];
        }
        matrix[12 + i] = constant[i];
    }
    glBindFramebuffer(GL_FRAMEBUFFER, g_post.fighter_framebuffer[slot]);
    glViewport(0, 0, w, h);
    glDisable(GL_SCISSOR_TEST);
    glDisable(GL_BLEND);
    glDisable(GL_CULL_FACE);
    glDepthMask(GL_TRUE);
    glClearDepth(1.0);
    glClear(GL_DEPTH_BUFFER_BIT);
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LESS);
    view->has_fighters = port_GlShadowDrawSunCasters(matrix) > 0;
    glDisable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);
    glBindFramebuffer(GL_FRAMEBUFFER, picture.framebuffer);
    port_GlForgetState();
}

void port_GlPostNewFrame(void) {
    /* the game draws a frame while it shows the last, the two halves of
     * video memory in turn: a frame with no ground painted over the
     * older of the two drawings kept, then over the newer */
    if (!g_post.is_noted) {
        if (g_post.kept.is_set) {
            g_post.kept.is_set = FALSE;
        } else {
            g_post.live.is_set = FALSE;
        }
    }
    g_post.is_noted = FALSE;
}

s32 port_GlPostWanted(const PortGlPicture *picture) {
    return port_GlPostIsOn() && picture->display_w > 0 &&
           (port_GlPostShows(&g_post.live, picture) ||
            port_GlPostShows(&g_post.kept, picture));
}

/* ---- the passes ---- */

typedef struct {
    s32 w;
    s32 h;
    float corner[2];
    float scale[2];
    float sample;
    float from[2];
} PostFrame;

static void port_GlPostCommon(
    const PostProgram *one, const PostFrame *frame, GLuint picture_texture) {
    glUseProgram(one->program);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, g_post.half.texture);
    glUniform1i(one->at_depth, 0);
    glActiveTexture(GL_TEXTURE0 + 1);
    glBindTexture(GL_TEXTURE_2D, picture_texture);
    glUniform1i(one->at_picture, 1);
    glUniform2f(one->at_full, (GLfloat)frame->w, (GLfloat)frame->h);
    glUniform1f(one->at_range, PORT_GL_DEPTH_RANGE);
    glUniform1f(one->at_h, g_post.h_of);
    glUniform2f(one->at_middle, g_post.middle[0], g_post.middle[1]);
    glUniform2f(one->at_corner, frame->corner[0], frame->corner[1]);
    glUniform2f(one->at_scale, frame->scale[0], frame->scale[1]);
    glUniform1f(one->at_sample, frame->sample);
    glUniform2f(one->at_from, frame->from[0], frame->from[1]);
}

/* How much of the stage's light on its level ground is not the sun's: the
 * occlusion darkens that share alone (the light from all about), the
 * sun's being a direct light the occlusion has no say over. */
static float port_GlPostAmbient(void) {
    const LightSetup *lights = (const LightSetup *)port_GlTimeTileLights();
    float back;
    float sun;
    float turn;
    s32 i;

    if (lights == NULL) {
        return 0.6f;
    }
    back = (float)lights->back[0] + (float)lights->back[1] +
           (float)lights->back[2];
    /* the first light's turn on a face looking up (y runs down) */
    turn = -(float)lights->lmat.m[0][1] / 4096.0f;
    if (turn < 0.0f) {
        turn = 0.0f;
    }
    sun = 0.0f;
    for (i = 0; i < 3; i++) {
        sun += (float)lights->cmat.m[i][0] * turn / 16.0f;
    }
    if (back + sun <= 0.0f) {
        return 0.6f;
    }
    back = back / (back + sun);
    return back < 0.35f ? 0.35f : back > 1.0f ? 1.0f : back;
}

/* The view's turn (the world into the view) and its inverse, row after
 * row, and the world's up in the view as a normal; FALSE where the turn
 * has no inverse. */
static s32 port_GlPostTurns(void) {
    const float *t = g_post.turn;
    float *b = g_post.back;
    float det;
    float length;
    s32 i;

    for (i = 0; i < 9; i++) {
        g_post.turn[i] = g_post.rotation[i] / 4096.0f;
    }
    b[0] = t[4] * t[8] - t[5] * t[7];
    b[1] = t[2] * t[7] - t[1] * t[8];
    b[2] = t[1] * t[5] - t[2] * t[4];
    b[3] = t[5] * t[6] - t[3] * t[8];
    b[4] = t[0] * t[8] - t[2] * t[6];
    b[5] = t[2] * t[3] - t[0] * t[5];
    b[6] = t[3] * t[7] - t[4] * t[6];
    b[7] = t[1] * t[6] - t[0] * t[7];
    b[8] = t[0] * t[4] - t[1] * t[3];
    det = t[0] * b[0] + t[1] * b[3] + t[2] * b[6];
    if (det > -1e-6f && det < 1e-6f) {
        return FALSE;
    }
    for (i = 0; i < 9; i++) {
        b[i] /= det;
    }
    /* a normal goes into the view by the inverse's transpose: (0, -1, 0)
     * takes the inverse's second row */
    length = port_GlPostRoot(b[3] * b[3] + b[4] * b[4] + b[5] * b[5]);
    if (length <= 0.0f) {
        return FALSE;
    }
    for (i = 0; i < 3; i++) {
        g_post.up[i] = -b[3 + i] / length;
    }
    return TRUE;
}

static void port_GlPostWorld(const PostWorld *world) {
    glActiveTexture(GL_TEXTURE0 + 5);
    glBindTexture(GL_TEXTURE_2D, g_post.depth);
    glActiveTexture(GL_TEXTURE0);
    glUniform1i(world->ground, 5);
    glUniformMatrix3fv(world->turn, 1, GL_TRUE, g_post.turn);
    glUniformMatrix3fv(world->back, 1, GL_TRUE, g_post.back);
    glUniform3f(
        world->eye, g_post.camera[0], g_post.camera[1], g_post.camera[2]);
    glUniform1f(world->plane, g_post_plane.plane);
}

/* The scene mirrored in the floor's plane, at half the size, its holes
 * filled, and blurred by the floor's roughness. */
static void port_GlPostMirror(const PostFrame *frame, GLuint picture_texture,
                              const PostView *view, float rough) {
    static const GLuint farthest = 0xFFFFFFFFu;
    GLuint groups_x = (GLuint)(g_post.mirror_nearest.w + 7) / 8;
    GLuint groups_y = (GLuint)(g_post.mirror_nearest.h + 7) / 8;

    PORT_GPU_BEGIN(PORT_GPU_SSR);
    glClearTexImage(g_post.mirror_nearest.texture, 0, GL_RED_INTEGER,
                    GL_UNSIGNED_INT, &farthest);
    port_GlPostCommon(&g_post.mirror, frame, picture_texture);
    port_GlPostWorld(&g_post.mirror_world);
    glUniform1i(g_post.mirror_at_fighters_on, view->has_fighters);
    glActiveTexture(GL_TEXTURE0 + 7);
    glBindTexture(GL_TEXTURE_2D, g_post.fighter_depth[view->fighter_slot]);
    glActiveTexture(GL_TEXTURE0);
    glUniform1i(g_post.mirror_at_fighters, 7);
    glUniform2f(g_post.mirror_at_fighter_corner, view->fighter_corner[0],
                view->fighter_corner[1]);
    glUniform2f(
        g_post.mirror_at_fighter_scale,
        (GLfloat)g_post.fighter_w[view->fighter_slot] /
            (view->fighter_size[0] > 0.0f ? view->fighter_size[0] : 1.0f),
        (GLfloat)g_post.fighter_h[view->fighter_slot] /
            (view->fighter_size[1] > 0.0f ? view->fighter_size[1] : 1.0f));
    glBindImageTexture(0, g_post.mirror_nearest.texture, 0, GL_FALSE, 0,
                       GL_READ_WRITE, GL_R32UI);
    glDispatchCompute(groups_x, groups_y, 1);
    glMemoryBarrier(GL_TEXTURE_FETCH_BARRIER_BIT);
    /* read into colours and distances, its holes filled */
    port_GlPostCommon(&g_post.fill, frame, picture_texture);
    port_GlPostWorld(&g_post.fill_world);
    glActiveTexture(GL_TEXTURE0 + 2);
    glBindTexture(GL_TEXTURE_2D, g_post.mirror_nearest.texture);
    glActiveTexture(GL_TEXTURE0);
    glUniform1i(g_post.fill_at_nearest, 2);
    glBindFramebuffer(GL_FRAMEBUFFER, g_post.mirror_framebuffer);
    glViewport(0, 0, g_post.mirror_color.w, g_post.mirror_color.h);
    port_GlPostDraw();
    /* blurred at a quarter of the size, as wide as the floor is rough */
    glUseProgram(g_post.soft_program);
    glActiveTexture(GL_TEXTURE0 + 2);
    glBindTexture(GL_TEXTURE_2D, g_post.mirror_color.texture);
    glUniform1i(g_post.soft_at_source, 2);
    glUniform1f(g_post.soft_at_soft, 1.5f + 8.0f * rough);
    glUniform2f(g_post.soft_at_size, (GLfloat)g_post.mirror_soft.w,
                (GLfloat)g_post.mirror_soft.h);
    glBindFramebuffer(GL_FRAMEBUFFER, g_post.mirror_soft.framebuffer);
    glViewport(0, 0, g_post.mirror_soft.w, g_post.mirror_soft.h);
    port_GlPostDraw();
    PORT_GPU_END();
}

/* The occlusion and the reflections of the ground, at a third of the size. */
/* How rough the stage's floor is, 0 a mirror to 1. */
static float port_GlPostRough(void) {
    s32 stage = port_RemasterStageNumber();

    /* water mirrored is broken by its waves: the grain of its own
     * picture turns the mirror aside, and what stands farther from it is
     * blurred, as on a floor this rough */
    if (g_post_plane.is_water) {
        return POST_WATER_ROUGH;
    }
    return stage >= 0 && stage < POST_STAGES
               ? (float)g_post_floors[stage].rough / 100.0f
               : 0.5f;
}

static void port_GlPostLight(const PostFrame *frame, GLuint picture_texture) {
    float rough = port_GlPostRough();

    PORT_GPU_BEGIN(g_post.is_reflect ? PORT_GPU_SSR : PORT_GPU_AO);
    port_GlPostCommon(&g_post.light, frame, picture_texture);
    glUniform1f(g_post.light_at_radius, AO_RADIUS);
    glUniform1f(g_post.light_at_strength, AO_STRENGTH);
    glUniform1f(g_post.light_at_reach, REFLECT_REACH);
    glUniform1i(g_post.light_at_ao_on, g_post.is_ao);
    glUniform1i(g_post.light_at_reflect_on, g_post.is_reflect);
    glUniform3f(g_post.light_at_up, g_post.up[0], g_post.up[1], g_post.up[2]);
    glUniform1i(g_post.light_at_debug, g_post.debug);
    glUniform1i(g_post.light_at_planar, g_post.is_planar);
    if (g_post.is_planar) {
        port_GlPostWorld(&g_post.light_world);
        glActiveTexture(GL_TEXTURE0 + 6);
        glBindTexture(GL_TEXTURE_2D, g_post.mirror_color.texture);
        glActiveTexture(GL_TEXTURE0);
        glUniform1i(g_post.light_at_mirror, 6);
    }
    glBindFramebuffer(GL_FRAMEBUFFER, g_post.pair_one);
    glViewport(0, 0, g_post.ao_one.w, g_post.ao_one.h);
    port_GlPostDraw();
    PORT_GPU_END();
    /* smoothed into the second pair, which the picture is laid from */
    PORT_GPU_BEGIN(PORT_GPU_AO);
    port_GlPostCommon(&g_post.smooth, frame, picture_texture);
    glUniform1i(g_post.smooth_at_ao, 2);
    glUniform1i(g_post.smooth_at_reflect, 3);
    glUniform1f(g_post.smooth_at_spread, 1.0f + 3.0f * rough);
    glActiveTexture(GL_TEXTURE0 + 2);
    glBindTexture(GL_TEXTURE_2D, g_post.ao_one.texture);
    glActiveTexture(GL_TEXTURE0 + 3);
    glBindTexture(GL_TEXTURE_2D, g_post.reflect_one.texture);
    glBindFramebuffer(GL_FRAMEBUFFER, g_post.pair_two);
    port_GlPostDraw();
    PORT_GPU_END();
}

/* The lights' glow at a quarter of the size, spread wider below it, or 0
 * where the frame has no lights. */
static s32 port_GlPostGlow(
    const PostFrame *frame, GLuint picture_texture, const PostView *view) {
    static const GLfloat nothing[4] = {0.0f, 0.0f, 0.0f, 0.0f};
    GLfloat turn[9];
    s32 lights = view->lights;
    s32 i;

    if (lights <= 0) {
        return 0;
    }
    if (g_post.light_buffer == 0) {
        glGenBuffers(1, &g_post.light_buffer);
    }
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, g_post.light_buffer);
    glBufferData(
        GL_SHADER_STORAGE_BUFFER, (ptrdiff_t)lights * PORT_GL_LIGHTS_CARD_SIZE,
        view->light, GL_STREAM_DRAW);
    glBindBufferBase(
        GL_SHADER_STORAGE_BUFFER, PORT_GL_LIGHTS_BINDING, g_post.light_buffer);
    PORT_GPU_BEGIN(PORT_GPU_BLOOM);
    glBindFramebuffer(GL_FRAMEBUFFER, g_post.glow_level[0].framebuffer);
    glViewport(0, 0, g_post.glow_level[0].w, g_post.glow_level[0].h);
    glClearBufferfv(GL_COLOR, 0, nothing);
    port_GlPostCommon(&g_post.glow, frame, picture_texture);
    /* the glow's quads are placed in the full picture's pixels and land
     * in a quarter of them: the viewport does that */
    for (i = 0; i < 9; i++) {
        turn[i] = g_post.rotation[i] / 4096.0f;
    }
    glUniformMatrix3fv(g_post.glow_at_turn, 1, GL_TRUE, turn);
    glUniform3f(g_post.glow_at_eye, g_post.camera[0], g_post.camera[1],
                g_post.camera[2]);
    /* brighter as the time of day darkens (gl_time.h) */
    glUniform1f(g_post.glow_at_gain, GLOW_GAIN * port_GlTime()->bloom);
    glEnable(GL_BLEND);
    glBlendEquation(GL_FUNC_ADD);
    glBlendFunc(GL_ONE, GL_ONE);
    glBindVertexArray(g_post.array);
    glDrawArraysInstanced(GL_TRIANGLES, 0, 6, lights);
    glDisable(GL_BLEND);
    /* smaller, and back up added into the larger */
    glUseProgram(g_post.scale_program);
    glUniform1i(g_post.scale_at_source, 2);
    glActiveTexture(GL_TEXTURE0 + 2);
    glUniform1i(g_post.scale_at_down, 1);
    for (i = 1; i < GLOW_LEVELS; i++) {
        glBindTexture(GL_TEXTURE_2D, g_post.glow_level[i - 1].texture);
        glBindFramebuffer(GL_FRAMEBUFFER, g_post.glow_level[i].framebuffer);
        glViewport(0, 0, g_post.glow_level[i].w, g_post.glow_level[i].h);
        port_GlPostDraw();
    }
    glUniform1i(g_post.scale_at_down, 0);
    glEnable(GL_BLEND);
    glBlendFunc(GL_ONE, GL_ONE);
    for (i = GLOW_LEVELS - 1; i > 0; i--) {
        glBindTexture(GL_TEXTURE_2D, g_post.glow_level[i].texture);
        glBindFramebuffer(GL_FRAMEBUFFER, g_post.glow_level[i - 1].framebuffer);
        glViewport(
            0, 0, g_post.glow_level[i - 1].w, g_post.glow_level[i - 1].h);
        port_GlPostDraw();
    }
    glDisable(GL_BLEND);
    PORT_GPU_END();
    return lights;
}

void port_GlPostRun(const PortGlPicture *picture, u32 *picture_framebuffer,
                    u32 *picture_texture, s32 w, s32 h, s32 sample) {
    GLuint picture_texture_in = *picture_texture;
    const PostView *view;
    PostFrame frame;
    s32 lights = 0;
    s32 i;

    if (!port_GlPostWanted(picture) || w <= 0 || h <= 0 ||
        !port_GlPostTargets(w, h)) {
        return;
    }
    glDisable(GL_SCISSOR_TEST);
    glDisable(GL_BLEND);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    /* the view of the drawing on show: the one being drawn, or the one
     * before it */
    view =
        port_GlPostShows(&g_post.live, picture) ? &g_post.live : &g_post.kept;
    g_post.depth = picture->ground_depth;
    memcpy(g_post.rotation, view->rotation, sizeof(g_post.rotation));
    memcpy(g_post.camera, view->camera, sizeof(g_post.camera));
    g_post.h_of = view->h;
    g_post.middle[0] = view->middle[0];
    g_post.middle[1] = view->middle[1];
    frame.w = w;
    frame.h = h;
    frame.corner[0] = (float)picture->display_x;
    frame.corner[1] = (float)picture->display_y;
    frame.scale[0] = (float)picture->draw_x;
    frame.scale[1] = (float)picture->draw_y;
    frame.sample = (float)(sample < 1 ? 1 : sample);
    frame.from[0] = (float)(picture->display_x * picture->draw_x);
    frame.from[1] = (float)(picture->display_y * picture->draw_y);

    /* the depth at a third of the size */
    PORT_GPU_BEGIN(PORT_GPU_POST);
    glUseProgram(g_post.half_program);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, g_post.depth);
    glUniform1i(g_post.half_at_ground, 0);
    glUniform2f(g_post.half_at_from, frame.from[0], frame.from[1]);
    glUniform1f(g_post.half_at_sample, frame.sample);
    glBindFramebuffer(GL_FRAMEBUFFER, g_post.half.framebuffer);
    glViewport(0, 0, g_post.half.w, g_post.half.h);
    port_GlPostDraw();
    PORT_GPU_END();
    /* the floor's plane mirrored, where the stage has one that reflects
     * and the camera is above it */
    g_post.is_planar =
        g_post.is_reflect && port_GlPostTurns() && g_post_plane.is_set &&
        g_post.camera[1] < g_post_plane.plane;
    if (!g_post.is_planar) {
        g_post.up[0] = 0.0f;
        g_post.up[1] = -1.0f;
        g_post.up[2] = 0.0f;
    }
    if (g_post.is_planar) {
        port_GlPostMirror(&frame, picture_texture_in, view, port_GlPostRough());
    }
    if (g_post.is_ao || g_post.is_reflect) {
        port_GlPostLight(&frame, picture_texture_in);
    }
    if (g_post.is_glow) {
        lights = port_GlPostGlow(&frame, picture_texture_in, view);
    }

    /* the lot over the picture, into the buffer it is laid into */
    PORT_GPU_BEGIN(PORT_GPU_POST);
    port_GlPostCommon(&g_post.lay, &frame, picture_texture_in);
    glActiveTexture(GL_TEXTURE0 + 2);
    glBindTexture(GL_TEXTURE_2D, g_post.ao_two.texture);
    glActiveTexture(GL_TEXTURE0 + 3);
    glBindTexture(GL_TEXTURE_2D, g_post.reflect_two.texture);
    glActiveTexture(GL_TEXTURE0 + 4);
    glBindTexture(GL_TEXTURE_2D, g_post.glow_level[0].texture);
    glActiveTexture(GL_TEXTURE0);
    glUniform1i(g_post.lay_at_ao, 2);
    glUniform1i(g_post.lay_at_reflect, 3);
    glUniform1i(g_post.lay_at_glow, 4);
    glUniform1i(g_post.lay_at_ao_on, g_post.is_ao);
    glUniform1i(g_post.lay_at_reflect_on, g_post.is_reflect);
    glUniform1i(g_post.lay_at_glow_on, lights > 0);
    glUniform1f(g_post.lay_at_ambient, port_GlPostAmbient());
    glUniform1i(g_post.lay_at_debug, g_post.debug);
    glUniform1i(g_post.lay_at_planar, g_post.is_planar);
    if (g_post.is_planar) {
        float rough = port_GlPostRough();

        port_GlPostWorld(&g_post.lay_world);
        glActiveTexture(GL_TEXTURE0 + 6);
        glBindTexture(GL_TEXTURE_2D, g_post.mirror_color.texture);
        glActiveTexture(GL_TEXTURE0 + 7);
        glBindTexture(GL_TEXTURE_2D, g_post.mirror_soft.texture);
        glActiveTexture(GL_TEXTURE0 + 8);
        glBindTexture(GL_TEXTURE_2D, g_post.mirror_far.texture);
        glActiveTexture(GL_TEXTURE0);
        glUniform1i(g_post.lay_at_mirror, 6);
        glUniform1i(g_post.lay_at_mirror_soft, 7);
        glUniform1i(g_post.lay_at_mirror_far, 8);
        glUniform1f(g_post.lay_at_rough, rough);
        glUniform1f(g_post.lay_at_ripple, 24.0f * rough);
    }
    glBindFramebuffer(GL_FRAMEBUFFER, g_post.out.framebuffer);
    glViewport(0, 0, w, h);
    port_GlPostDraw();
    PORT_GPU_END();
    /* the picture is now the one just laid, and the one it was laid from
     * is the next picture's to lay into: no copy, and no barrier (which
     * held Mesa's software renderer for the whole of its drawing) */
    {
        GLuint texture = *picture_texture;
        GLuint framebuffer = *picture_framebuffer;

        *picture_texture = g_post.out.texture;
        *picture_framebuffer = g_post.out.framebuffer;
        g_post.out.texture = texture;
        g_post.out.framebuffer = framebuffer;
    }
    for (i = 0; i < 9; i++) {
        glActiveTexture(GL_TEXTURE0 + i);
        glBindTexture(GL_TEXTURE_2D, 0);
    }
    glActiveTexture(GL_TEXTURE0);
    port_GlForgetState();
    g_post.pictures++;
}
