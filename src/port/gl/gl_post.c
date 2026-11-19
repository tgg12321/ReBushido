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
 *   light     at half the size, one pass with two outputs: the occlusion
 *             of each pixel of ground, and its reflection -- a ray along
 *             the view reflected in its face, walked across the picture
 *             through the depth until it passes behind something; where
 *             it meets nothing, the sky the picture shows in that
 *             direction, or nothing
 *   smooth    both smoothed, three by three, where the depth agrees
 *   bloom     each light of the frame's list a soft glow at a quarter of
 *             the size where the scene does not hide it, spread wider
 *             at an eighth and a sixteenth
 *   lay       the lot laid over the picture, in its own place
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
    GL(void, glTextureBarrier, (void))                                         \
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
    GL(void, glDrawArraysInstanced, (GLenum, GLint, GLsizei, GLsizei))

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

/* ---- the shaders ---- */

/* one triangle over the whole of the target */
static const char g_post_vertex_text[] =
    "#version 430 core\n"
    "void main() {\n"
    "    vec2 at = vec2(float((gl_VertexID & 1) * 4) - 1.0,\n"
    "                   float((gl_VertexID & 2) * 2) - 1.0);\n"
    "    gl_Position = vec4(at, 0.0, 1.0);\n"
    "}\n";

/* The depth at half the picture's size, from the buffer's (gl.h,
 * `ground_depth`): the one the passes below read. */
static const char g_post_half_text[] =
    "#version 430 core\n"
    "uniform sampler2D u_ground;\n"
    "uniform vec2 u_from;\n" /* the picture's corner in the buffer */
    "uniform float u_sample;\n"
    "layout(location = 0) out vec4 o_depth;\n"
    "void main() {\n"
    "    vec2 f = floor(gl_FragCoord.xy) * 2.0 + vec2(1.0);\n"
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
#define POST_COMMON                                                              \
    "uniform sampler2D u_depth;\n"                                               \
    "uniform sampler2D u_picture;\n"                                             \
    "uniform vec2 u_full;\n"                                                     \
    "uniform float u_range;\n"                                                   \
    "uniform float u_h;\n"                                                       \
    "uniform vec2 u_middle;\n"                                                   \
    "uniform vec2 u_corner;\n"                                                   \
    "uniform vec2 u_scale;\n"                                                    \
    "uniform float u_sample;\n"                                                  \
    "uniform vec2 u_from;\n"                                                     \
    "vec2 screen_of(vec2 f) {\n"                                                 \
    "    return u_corner + (f * u_sample - 0.5) / u_scale;\n"                    \
    "}\n"                                                                        \
    "vec2 picture_of(vec3 v) {\n"                                                \
    "    vec2 s = u_middle + vec2(v.x, 0.5 * v.y) * u_h / v.z;\n"                \
    "    return ((s - u_corner) * u_scale + 0.5) / u_sample;\n"                  \
    "}\n" /* the depth is at half the picture's size: a texel of it is the       \
           * depth at the middle of its two by two pixels */                     \
    "ivec2 half_of(vec2 f) {\n"                                                  \
    "    return clamp(ivec2(f * 0.5), ivec2(0), textureSize(u_depth, 0) - 1);\n" \
    "}\n"                                                                        \
    "float depth_at(vec2 f) {\n"                                                 \
    "    return texelFetch(u_depth, half_of(f), 0).r;\n"                         \
    "}\n"                                                                        \
    "vec3 view_of(vec2 f, float z) {\n"                                          \
    "    vec2 s = screen_of(f) - u_middle;\n"                                    \
    "    return vec3(s.x * z / u_h, 2.0 * s.y * z / u_h, z);\n"                  \
    "}\n"                                                                        \
    "vec3 view_at(vec2 f) {\n"                                                   \
    "    ivec2 at = half_of(f);\n"                                               \
    "    return view_of(vec2(at) * 2.0 + vec2(1.0),\n"                           \
    "                   texelFetch(u_depth, at, 0).r);\n"                        \
    "}\n" /* the face's normal from the depth about the pixel, each way from     \
           * the side whose depth runs on more evenly, so that an edge does                      \
           * not bend it */                                                                           \
    "vec3 normal_at(vec2 f, vec3 p) {\n"                                         \
    "    float step = 2.0;\n"                                                    \
    "    vec3 r = view_at(f + vec2(step, 0.0)) - p;\n"                           \
    "    vec3 l = p - view_at(f - vec2(step, 0.0));\n"                           \
    "    vec3 d = view_at(f + vec2(0.0, step)) - p;\n"                           \
    "    vec3 u = p - view_at(f - vec2(0.0, step));\n"                           \
    "    vec3 across = abs(r.z) < abs(l.z) ? r : l;\n"                           \
    "    vec3 down = abs(d.z) < abs(u.z) ? d : u;\n"                             \
    "    vec3 n = normalize(cross(down, across));\n"                             \
    "    return dot(n, p) > 0.0 ? -n : n;\n"                                     \
    "}\n" /* what painted the pixel last (gl_post.h): the ground's               \
           * reflectance, or -1 where it was not the ground */                                            \
    "float ground_of(float a) {\n"                                               \
    "    if (a < 0.575 || a >= 0.9) { return -1.0; }\n"                          \
    "    return clamp((a - 0.6) / 0.25, 0.0, 1.0);\n"                            \
    "}\n"                                                                        \
    "float noise_at(vec2 f) {\n"                                                 \
    "    return fract(52.9829189\n"                                              \
    "                 * fract(dot(f, vec2(0.06711056, 0.00583715))));\n"         \
    "}\n"

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
    "uniform int u_ao_on;\n"
    "uniform int u_reflect_on;\n"
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
    "    float facing = clamp(dot(-eye, n), 0.0, 1.0);\n"
    /* Schlick's Fresnel for a varnish, and a floor seen from above
     * still showing some of it */
    "    float fresnel = 0.04 + 0.96 * pow(1.0 - facing, 5.0);\n"
    "    float amount = shine * (0.65 + 0.35 * fresnel);\n"
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
    "    vec2 f = floor(gl_FragCoord.xy) * 2.0 + vec2(1.0);\n"
    "    float shine = ground_of(texelFetch(u_picture, ivec2(f), 0).a);\n"
    "    float z = depth_at(f);\n"
    "    o_ao = vec4(1.0);\n"
    "    o_reflect = vec4(0.0);\n"
    "    if (shine < 0.0 || z >= u_range * 0.999) { return; }\n"
    "    vec3 p = view_of(floor(f) + 0.5, z);\n"
    "    vec3 n = normal_at(f, p);\n"
    "    if (u_ao_on != 0) { o_ao = vec4(occlusion(f, p, n), 0.0, 0.0, 1.0); }\n"
    "    if (u_reflect_on != 0 && shine > 0.004) {\n"
    "        o_reflect = reflection(f, p, n, shine);\n"
    "    }\n"
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
 * front of it. The list is the card's own (gl_lights.h, binding 4). */
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
    "    gl_Position = vec4(at / u_full * 2.0 - vec2(1.0), 0.0, 1.0);\n"
    "}\n";

static const char g_post_glow_fragment_text[] =
    "#version 430 core\n"
    "in vec2 v_corner;\n"
    "in vec3 v_color;\n"
    "layout(location = 0) out vec4 o_color;\n"
    "void main() {\n"
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

/* The lot over the picture, in its own place (each pixel reads its own
 * texel alone, after a barrier): the occlusion darkening the ground's
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
    "uniform int u_debug;\n"
    "layout(location = 0) out vec4 o_color;\n"
    "void main() {\n"
    "    vec2 f = gl_FragCoord.xy;\n"
    "    vec4 c = texelFetch(u_picture, ivec2(f), 0);\n"
    "    vec2 uv = f / u_full;\n"
    "    float shine = ground_of(c.a);\n"
    "    if (shine >= 0.0) {\n"
    "        if (u_ao_on != 0) {\n"
    "            float ao = texture(u_ao, uv).r;\n"
    "            c.rgb *= mix(1.0, ao, u_ambient);\n"
    "            if (u_debug == 1) { c.rgb = vec3(ao); }\n"
    "        }\n"
    "        if (u_reflect_on != 0) {\n"
    "            vec4 r = texture(u_reflect, uv);\n"
    "            c.rgb = c.rgb * (1.0 - r.a) + r.rgb;\n"
    "            if (u_debug == 2) { c.rgb = r.rgb + vec3(0.0, 0.0, r.a * 0.3); }\n"
    "        }\n"
    "    }\n"
    "    if (u_glow_on != 0) {\n"
    "        c.rgb += texture(u_glow, uv).rgb;\n"
    "    }\n"
    "    if (u_debug == 3) {\n"
    "        float z = depth_at(f);\n"
    "        c.rgb = vec3(fract(z / 1000.0), z / u_range, max(shine, 0.0));\n"
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

/* A drawing of the ground: its view, and the line of video memory its
 * drawing area begins at. */
typedef struct {
    s32 is_set;
    float rotation[9];
    float camera[3];
    float h;
    float middle[2];
    s32 top;
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
    /* The view the passes read the depth with: the one the picture on
     * show was drawn with (port_GlPostRun chooses it) */
    float rotation[9];
    float camera[3];
    float h_of;
    float middle[2];
    /* The views of the ground's last two drawings (port_GlPostNoteView):
     * the one being drawn, and the one before it. The game draws one frame
     * while it shows the last, in another part of video memory, so the
     * picture shown may be either. */
    PostView live;
    PostView kept;
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

static GLuint port_GlPostLink(
    const char *vertex_text, const char *fragment_text) {
    GLuint vertex = port_GlPostShader(GL_VERTEX_SHADER, vertex_text);
    GLuint fragment = port_GlPostShader(GL_FRAGMENT_SHADER, fragment_text);
    GLuint program;
    GLint ok = 0;

    if (vertex == 0 || fragment == 0) {
        return 0;
    }
    program = glCreateProgram();
    glAttachShader(program, vertex);
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
    if (g_post.scale_program == 0 || g_post.half_program == 0 ||
        !port_GlPostMake(
            &g_post.light, g_post_vertex_text, g_post_light_text) ||
        !port_GlPostMake(
            &g_post.smooth, g_post_vertex_text, g_post_smooth_text) ||
        !port_GlPostMake(
            &g_post.glow, g_post_glow_vertex_text, g_post_glow_fragment_text) ||
        !port_GlPostMake(&g_post.lay, g_post_vertex_text, g_post_lay_text)) {
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
#undef AT
    glGenVertexArrays(1, &g_post.array);
    /* RB_POST_DEBUG=ao, =reflect or =depth: the occlusion, the
     * reflections alone, or the depth (red its thousands, green all of
     * it) and the reflectance (blue), in the place of the picture: how
     * they were looked at */
    g_post.debug =
        debug == NULL                   ? 0
        : strcmp(debug, "ao") == 0      ? 1
        : strcmp(debug, "reflect") == 0 ? 2
        : strcmp(debug, "depth") == 0   ? 3
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

static s32 port_GlPostTargets(s32 w, s32 h) {
    s32 half_w = (w + 1) / 2;
    s32 half_h = (h + 1) / 2;
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

    memcpy(view->rotation, rotation, sizeof(view->rotation));
    memcpy(view->camera, camera, sizeof(view->camera));
    view->h = h;
    view->middle[0] = middle_x;
    view->middle[1] = middle_y;
    view->top = top;
    view->is_set = h > 0.0f;
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
    if (g_post.live.is_set) {
        g_post.kept = g_post.live;
    }
    g_post.live.is_set = FALSE;
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
    const LightSetup *lights = (const LightSetup *)port_RemasterTileLights();
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

/* The occlusion and the reflections of the ground, at half the size. */
static void port_GlPostLight(const PostFrame *frame, GLuint picture_texture) {
    s32 stage = port_RemasterStageNumber();
    float rough = stage >= 0 && stage < POST_STAGES
                      ? (float)g_post_floors[stage].rough / 100.0f
                      : 0.5f;

    PORT_GPU_BEGIN(g_post.is_reflect ? PORT_GPU_SSR : PORT_GPU_AO);
    port_GlPostCommon(&g_post.light, frame, picture_texture);
    glUniform1f(g_post.light_at_radius, AO_RADIUS);
    glUniform1f(g_post.light_at_strength, AO_STRENGTH);
    glUniform1f(g_post.light_at_reach, REFLECT_REACH);
    glUniform1i(g_post.light_at_ao_on, g_post.is_ao);
    glUniform1i(g_post.light_at_reflect_on, g_post.is_reflect);
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
static s32 port_GlPostGlow(const PostFrame *frame, GLuint picture_texture) {
    static const GLfloat nothing[4] = {0.0f, 0.0f, 0.0f, 0.0f};
    GLfloat turn[9];
    s32 lights = port_GlLightsBindLast();
    s32 i;

    if (lights <= 0) {
        return 0;
    }
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
    glUniform1f(g_post.glow_at_gain, GLOW_GAIN);
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

void port_GlPostRun(const PortGlPicture *picture, u32 picture_framebuffer,
                    u32 picture_texture, s32 w, s32 h, s32 sample) {
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

    /* the depth at half the size */
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
    if (g_post.is_ao || g_post.is_reflect) {
        port_GlPostLight(&frame, picture_texture);
    }
    if (g_post.is_glow) {
        lights = port_GlPostGlow(&frame, picture_texture);
    }

    /* the lot over the picture, in its own place: each pixel reads its
     * own texel alone, which a barrier makes safe */
    PORT_GPU_BEGIN(PORT_GPU_POST);
    port_GlPostCommon(&g_post.lay, &frame, picture_texture);
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
    glBindFramebuffer(GL_FRAMEBUFFER, picture_framebuffer);
    glViewport(0, 0, w, h);
    glTextureBarrier();
    port_GlPostDraw();
    glTextureBarrier();
    PORT_GPU_END();
    for (i = 0; i < 5; i++) {
        glActiveTexture(GL_TEXTURE0 + i);
        glBindTexture(GL_TEXTURE_2D, 0);
    }
    glActiveTexture(GL_TEXTURE0);
    port_GlForgetState();
    g_post.pictures++;
}
