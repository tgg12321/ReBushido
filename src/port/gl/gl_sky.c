/* A sky behind the stages that are open to it, in the remastered picture
 * (gl_sky.h; docs/PORT.md, "The sky").
 *
 * One triangle over the whole frame, painted where the game's own fill
 * of the colour of the distance was. Each pixel works out the direction
 * it looks in from the view the stage's ground is drawn with -- the
 * coprocessor's rotation, its projection distance and its screen
 * offsets, carried out of the renderer (port_RemasterNoteStageView) --
 * and the sky is a function of that direction alone: a gradient, stars
 * on the faces of a cube about the eye, a moon where the stage's light
 * comes from, and a few thin clouds. Or, where a mod gives the stage a
 * picture of its own sky, that picture, wrapped round the eye from left
 * to right and from the top down (an equirectangular picture).
 *
 * Nothing here changes a value the game computes. */
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>

#include "common.h"
#include "gl.h"
#include "gl_sky.h"
#include "gl_stage.h"
#include "port_mods.h"
#include "port_png.h"
#include "port_remaster.h"

/* the game's own header, for the shape of a light setup */
#include "light.h"

/* ---- GL, as much of it as this file uses ---- */

typedef unsigned int GLenum;
typedef unsigned char GLboolean;
typedef int GLint;
typedef unsigned int GLuint;
typedef int GLsizei;
typedef float GLfloat;
typedef char GLchar;

#define GL_FALSE 0
#define GL_TRIANGLES 0x0004
#define GL_TEXTURE_2D 0x0DE1
#define GL_TEXTURE0 0x84C0
#define GL_TEXTURE_MIN_FILTER 0x2801
#define GL_TEXTURE_MAG_FILTER 0x2800
#define GL_TEXTURE_WRAP_S 0x2802
#define GL_TEXTURE_WRAP_T 0x2803
#define GL_LINEAR 0x2601
#define GL_LINEAR_MIPMAP_LINEAR 0x2703
#define GL_REPEAT 0x2901
#define GL_CLAMP_TO_EDGE 0x812F
#define GL_RGBA 0x1908
#define GL_RGBA8 0x8058
#define GL_UNSIGNED_BYTE 0x1401
#define GL_UNPACK_ALIGNMENT 0x0CF5
#define GL_FRAMEBUFFER 0x8D40
#define GL_DEPTH_TEST 0x0B71
#define GL_BLEND 0x0BE2
#define GL_SCISSOR_TEST 0x0C11
#define GL_VERTEX_SHADER 0x8B31
#define GL_FRAGMENT_SHADER 0x8B30
#define GL_COMPILE_STATUS 0x8B81
#define GL_LINK_STATUS 0x8B82
#define GL_TEXTURE_CUBE_MAP 0x8513
#define GL_TEXTURE_CUBE_MAP_POSITIVE_X 0x8515
#define GL_TEXTURE_CUBE_MAP_SEAMLESS 0x884F
#define GL_COLOR_ATTACHMENT0 0x8CE0
#define GL_R16F 0x822D
#define GL_RED 0x1903
#define GL_FLOAT 0x1406

#define PORT_GL_SKY_FUNCTIONS                                                  \
    GL(void, glViewport, (GLint, GLint, GLsizei, GLsizei))                     \
    GL(void, glScissor, (GLint, GLint, GLsizei, GLsizei))                      \
    GL(void, glEnable, (GLenum))                                               \
    GL(void, glDisable, (GLenum))                                              \
    GL(void, glBindTexture, (GLenum, GLuint))                                  \
    GL(void, glActiveTexture, (GLenum))                                        \
    GL(void, glGenTextures, (GLsizei, GLuint *))                               \
    GL(void, glTexImage2D,                                                     \
       (GLenum, GLint, GLint, GLsizei, GLsizei, GLint, GLenum, GLenum,         \
        const void *))                                                         \
    GL(void, glTexParameteri, (GLenum, GLenum, GLint))                         \
    GL(void, glGenerateMipmap, (GLenum))                                       \
    GL(void, glPixelStorei, (GLenum, GLint))                                   \
    GL(void, glBindFramebuffer, (GLenum, GLuint))                              \
    GL(void, glGenFramebuffers, (GLsizei, GLuint *))                           \
    GL(void, glDeleteFramebuffers, (GLsizei, const GLuint *))                  \
    GL(void, glFramebufferTexture2D, (GLenum, GLenum, GLenum, GLuint, GLint))  \
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
    GL(void, glGetProgramInfoLog, (GLuint, GLsizei, GLsizei *, GLchar *))      \
    GL(void, glUseProgram, (GLuint))                                           \
    GL(GLint, glGetUniformLocation, (GLuint, const GLchar *))                  \
    GL(void, glUniform1i, (GLint, GLint))                                      \
    GL(void, glUniform1f, (GLint, GLfloat))                                    \
    GL(void, glUniform2f, (GLint, GLfloat, GLfloat))                           \
    GL(void, glUniform3f, (GLint, GLfloat, GLfloat, GLfloat))                  \
    GL(void, glUniformMatrix3fv, (GLint, GLsizei, GLboolean, const GLfloat *)) \
    GL(void, glDrawArrays, (GLenum, GLint, GLsizei))

#define GL(result, name, args) static result(*name) args;
PORT_GL_SKY_FUNCTIONS
#undef GL

/* ---- the stages ---- */

/* The game's 38 stages (STAGE_COUNT). */
#define SKY_STAGES 38
/* A colour's channel at its brightest, and one of the coprocessor's
 * fractions (1.12) */
#define CHANNEL_MAX 255.0f
#define GTE_ONE 4096.0f
/* The clouds' cube about the eye: texels a side of each of its faces
 * (port_GlSkyBakeClouds) */
#define CLOUD_SIDE 1024
#define CUBE_FACES 6
/* A rotation's nine numbers */
#define MATRIX_ELEMENTS 9
/* How long a shader's complaint and a mod's file's key may be */
#define LOG_SIZE 512
#define KEY_SIZE 16
#define PROBLEM_SIZE 96

/* Which stages are open to the sky: looked at, stage by stage, over the
 * prover's tour of each (tools/port_stage_prove.py; docs/PORT.md, "The
 * sky"). A stage that is a hall or a cave keeps the game's fill: a star
 * in a gap between two of its walls would be a hole, not a sky. */
static const u8 g_sky_open[SKY_STAGES] = {
    0, /* the main Tatara shrine: a hall */
    0, /* the Meikyokan dojo: a hall */
    1, /* Shainto HQ */
    1, /* the Tatara shrine */
    1, /* the bus stop */
    1, /* the lost bridge */
    1, /* the Tatara shrine path */
    1, /* the bamboo thicket */
    1, /* outside the Meikyokan */
    1, /* the future dump site */
    1, /* the snake cliff */
    1, /* Kimigaura coast */
    1, /* the orange tree fields */
    0, /* the castle rooftop: a sky and a moon of its own (stage_hooks.c) */
    1, /* under the lost bridge */
    1, /* the terraced fields */
    1, /* under the railway */
    1, /* the water tower dam */
    1, /* the lumberyard */
    1, /* the lighthouse */
    1, /* the kabuki stage */
    1, /* the abandoned ship */
    1, /* the ferry dock */
    1, /* the Shainto gate */
    0, /* the purple cavern: a cave */
    1, /* the abandoned station */
    1, /* the railway */
    1, /* the abandoned tunnel: the open ground before its mouth */
    1, /* the abandoned shipyard */
    1, /* the building's rooftop */
    0, /* the building's parking lot: a garage */
    1, /* the Shainto cemetery */
    0, /* the first team battle court: a hall */
    1, /* the second team battle court: a yard */
    0, /* chanbara's hall */
    0, /* practice 1: a dojo */
    0, /* practice 2: a dojo */
    0, /* stage 37: a small model of a castle town, seen from afar */
};

/* ---- the shaders ---- */

static const char g_sky_vertex_text[] =
    "#version 330 core\n"
    /* one triangle that covers the whole of the target */
    "void main() {\n"
    "    vec2 at = vec2(float((gl_VertexID & 1) * 4) - 1.0,\n"
    "                   float((gl_VertexID & 2) * 2) - 1.0);\n"
    "    gl_Position = vec4(at, 0.0, 1.0);\n"
    "}\n";

/* The noise the clouds are made of, in both of the shaders below. */
#define SKY_NOISE                                                                \
    "float hash(vec3 p) {\n"                                                     \
    "    p = fract(p * 0.3183099 + vec3(0.71, 0.113, 0.419));\n"                 \
    "    p *= 17.0;\n"                                                           \
    "    return fract(p.x * p.y * p.z * (p.x + p.y + p.z));\n"                   \
    "}\n"                                                                        \
    "float noise(vec3 p) {\n"                                                    \
    "    vec3 i = floor(p);\n"                                                   \
    "    vec3 f = fract(p);\n"                                                   \
    "    f = f * f * (3.0 - 2.0 * f);\n"                                         \
    "    return mix(mix(mix(hash(i), hash(i + vec3(1, 0, 0)), f.x),\n"           \
    "                   mix(hash(i + vec3(0, 1, 0)), hash(i + vec3(1, 1, 0)),\n" \
    "                       f.x), f.y),\n"                                       \
    "               mix(mix(hash(i + vec3(0, 0, 1)), hash(i + vec3(1, 0, 1)),\n" \
    "                       f.x),\n"                                             \
    "                   mix(hash(i + vec3(0, 1, 1)), hash(i + vec3(1, 1, 1)),\n" \
    "                       f.x), f.y), f.z);\n"                                 \
    "}\n"                                                                        \
    "float clouds(vec3 p) {\n"                                                   \
    "    float sum = 0.0;\n"                                                     \
    "    float part = 0.5;\n"                                                    \
    "    for (int i = 0; i < 5; i++) {\n"                                        \
    "        sum += part * noise(p);\n"                                          \
    "        p = p * 2.03 + vec3(1.7, 9.2, 4.1);\n"                              \
    "        part *= 0.5;\n"                                                     \
    "    }\n"                                                                    \
    "    return sum;\n"                                                          \
    "}\n"                                                                        \
    "float cloud_cover(vec3 dir) {\n"                                            \
    "    float up = -dir.y;\n"                                                   \
    "    vec3 sheet = vec3(dir.x, 0.0, dir.z) / max(up + 0.12, 0.05);\n"         \
    "    return clouds(sheet * 1.6 + vec3(3.0, 0.0, 1.0));\n"                    \
    "}\n"

static const char g_sky_fragment_text[] =
    "#version 330 core\n"
    /* the view's rotation as the card wants it: a direction in the view
     * into the world's */
    "uniform mat3 u_turn;\n"
    "uniform vec2 u_middle;\n" /* the screen's middle, video memory's px */
    "uniform float u_h;\n"     /* the projection's distance */
    "uniform vec2 u_scale;\n"  /* the buffer's pixels to one of video's */
    "uniform vec3 u_far;\n"    /* the colour of the distance */
    "uniform vec3 u_ambient;\n"
    "uniform vec3 u_moon;\n"
    "uniform vec3 u_moon_color;\n"
    "uniform int u_has_picture;\n"
    "uniform sampler2D u_picture;\n"
    /* the clouds' noise, worked out once (port_GlSkyBakeClouds) */
    "uniform samplerCube u_clouds;\n"
    "layout(location = 0) out vec4 o_color;\n"
    "const float PI = 3.14159265;\n" SKY_NOISE
    /* The stars of one layer: the direction onto the face of a cube about
     * the eye, the face cut into `cells` by `cells` cells, and a star in
     * a cell where the cell's number says so, as wide as a pixel. */
    "float stars(vec3 dir, float cells, float share, float pixel) {\n"
    "    vec3 a = abs(dir);\n"
    "    vec2 uv;\n"
    "    float face;\n"
    "    if (a.x >= a.y && a.x >= a.z) {\n"
    "        uv = dir.yz / a.x; face = dir.x > 0.0 ? 0.0 : 1.0;\n"
    "    } else if (a.y >= a.z) {\n"
    "        uv = dir.xz / a.y; face = dir.y > 0.0 ? 2.0 : 3.0;\n"
    "    } else {\n"
    "        uv = dir.xy / a.z; face = dir.z > 0.0 ? 4.0 : 5.0;\n"
    "    }\n"
    "    vec2 g = (uv * 0.5 + 0.5) * cells;\n"
    "    vec2 cell = floor(g);\n"
    "    float pick = hash(vec3(cell, face + cells));\n"
    "    if (pick > share) { return 0.0; }\n"
    "    vec2 at = cell + 0.25 + 0.5 * vec2(hash(vec3(cell, face + 7.0)),\n"
    "                                       hash(vec3(cell, face + 13.0)));\n"
    /* the distance on the face, in the angle it is from the eye */
    "    float apart = length(g - at) * 2.0 / cells;\n"
    "    float bright = 0.25 + 0.75 * pow(hash(vec3(cell, face + 29.0)), 3.0);\n"
    "    return bright * (1.0 - smoothstep(0.35 * pixel, 1.1 * pixel, apart));\n"
    "}\n"
    "void main() {\n"
    "    vec2 screen = (gl_FragCoord.xy - vec2(0.5)) / u_scale;\n"
    /* the console's frame is half as tall as it is wide: a line of it
     * is two of the view's units */
    "    vec3 look = vec3((screen.x - u_middle.x) / u_h,\n"
    "                     2.0 * (screen.y - u_middle.y) / u_h, 1.0);\n"
    "    vec3 dir = normalize(u_turn * look);\n"
    /* the world's y runs down */
    "    float up = -dir.y;\n"
    "    vec3 color;\n"
    "    if (u_has_picture != 0) {\n"
    "        vec2 at = vec2(atan(dir.x, dir.z) / (2.0 * PI) + 0.5,\n"
    "                       acos(clamp(up, -1.0, 1.0)) / PI);\n"
    "        color = texture(u_picture, at).rgb;\n"
    "        o_color = vec4(color, 1.0);\n"
    "        return;\n"
    "    }\n"
    /* how wide a pixel of the buffer is, as an angle */
    "    float pixel = 1.0 / (u_h * u_scale.x);\n"
    /* The moon: the way the stage's light comes from, seen from above
     * whichever way its vector is meant, and put low in the sky -- a
     * fight's camera looks along the ground, and sees little more than
     * a fifth of the way up. */
    "    vec3 moon = length(u_moon) > 1.0 ? normalize(u_moon)\n"
    "                                     : normalize(vec3(0.5, -0.6, 0.6));\n"
    "    vec2 flat_way = length(moon.xz) > 1e-4 ? normalize(moon.xz)\n"
    "                                           : vec2(0.0, 1.0);\n"
    "    float moon_up = clamp(abs(moon.y), 0.2, 0.36);\n"
    "    moon = vec3(flat_way.x * sqrt(1.0 - moon_up * moon_up), -moon_up,\n"
    "                flat_way.y * sqrt(1.0 - moon_up * moon_up));\n"
    "    float high = clamp(up, 0.0, 1.0);\n"
    /* The gradient: the colour of the distance at the horizon itself,
     * which is what the stage's fog runs into; a glow of the stage's own
     * ambient light just above it; a deep night higher up. */
    "    vec3 glow = u_far + u_ambient * 0.30;\n"
    "    vec3 top = u_far * 0.5 + u_ambient * 0.06;\n"
    "    color = mix(glow, top, smoothstep(0.0, 0.45, high));\n"
    /* and the colour of the distance again a little below the horizon,
     * where a stage that stands high (a bridge, a rooftop) shows the
     * empty dark under it */
    "    color = mix(u_far, color, smoothstep(-0.03, 0.01, up));\n"
    /* the moon, where the stage's light comes from, and its halo */
    "    float moon_cos = dot(dir, moon);\n"
    "    float moon_apart = acos(clamp(moon_cos, -1.0, 1.0));\n"
    "    float radius = 0.045;\n"
    "    float halo = 0.22 * exp(-moon_apart * 9.0)\n"
    "               + 0.18 * exp(-moon_apart * 30.0);\n"
    "    color += u_moon_color * halo * smoothstep(-0.02, 0.08, high);\n"
    /* clouds: thin, low and lit from the side the moon is on */
    "    float cover = texture(u_clouds, dir).r;\n"
    "    float cloud = smoothstep(0.52, 0.78, cover)\n"
    "                * smoothstep(0.0, 0.12, high)\n"
    "                * (1.0 - smoothstep(0.45, 0.9, high));\n"
    "    vec3 cloud_color = u_far + u_ambient * 0.16\n"
    "                     + u_moon_color * (0.05 + 0.25 * exp(-moon_apart * 4.0));\n"
    /* the stars, two layers, fewer near the horizon and none behind a
     * cloud or the moon */
    "    float star = stars(dir, 90.0, 0.05, pixel) * 1.3\n"
    "               + stars(dir, 240.0, 0.06, pixel) * 0.7;\n"
    "    star *= smoothstep(0.01, 0.12, high) * (1.0 - cloud);\n"
    "    star *= smoothstep(radius * 1.5, radius * 4.0, moon_apart);\n"
    "    color += vec3(0.80, 0.85, 1.0) * star;\n"
    "    color = mix(color, cloud_color, cloud * 0.75);\n"
    "    float disc = 1.0 - smoothstep(radius - pixel, radius + pixel,\n"
    "                                  moon_apart);\n"
    "    if (disc > 0.0) {\n"
    /* its seas: a noise over the disc, fixed to it */
    "        vec3 side = normalize(cross(moon, vec3(0.0, 1.0, 0.0)));\n"
    "        vec3 lift = cross(side, moon);\n"
    "        vec2 on = vec2(dot(dir, side), dot(dir, lift)) / radius;\n"
    "        float sea = noise(vec3(on * 2.2, 5.0)) * 0.6\n"
    "                  + noise(vec3(on * 5.0, 9.0)) * 0.4;\n"
    "        vec3 face = u_moon_color * (1.05 - 0.35 * smoothstep(0.45,\n"
    "                                                    0.75, sea));\n"
    "        color = mix(color, face, disc * (1.0 - cloud * 0.7));\n"
    "    }\n"
    "    o_color = vec4(clamp(color, 0.0, 1.0), 1.0);\n"
    "}\n";

/* The clouds' noise, five octaves of it, is the dearest part of a sky's
 * pixel and a function of the direction alone: it is worked out once,
 * into a cube about the eye whose six faces are drawn one by one, and
 * the sky reads it there (port_GlSkyBakeClouds). A texel of a face is
 * the direction through its middle, by the card's own rule for which
 * face and texel a direction reads. */
static const char g_sky_bake_text[] =
    "#version 330 core\n"
    "uniform int u_face;\n"
    "uniform float u_side;\n"
    "layout(location = 0) out vec4 o_cover;\n" SKY_NOISE "void main() {\n"
    "    vec2 st = gl_FragCoord.xy / u_side * 2.0 - vec2(1.0);\n"
    "    float sc = st.x;\n"
    "    float tc = st.y;\n"
    "    vec3 dir;\n"
    "    if (u_face == 0) { dir = vec3(1.0, -tc, -sc); }\n"
    "    else if (u_face == 1) { dir = vec3(-1.0, -tc, sc); }\n"
    "    else if (u_face == 2) { dir = vec3(sc, 1.0, tc); }\n"
    "    else if (u_face == 3) { dir = vec3(sc, -1.0, -tc); }\n"
    "    else if (u_face == 4) { dir = vec3(sc, -tc, 1.0); }\n"
    "    else { dir = vec3(-sc, -tc, -1.0); }\n"
    "    o_cover = vec4(cloud_cover(normalize(dir)), 0.0, 0.0, 1.0);\n"
    "}\n";

/* ---- the state ---- */

static struct {
    s32 is_started;
    s32 kind;
    GLuint program;
    GLuint array;
    GLint at_turn;
    GLint at_middle;
    GLint at_h;
    GLint at_scale;
    GLint at_far;
    GLint at_ambient;
    GLint at_moon;
    GLint at_moon_color;
    GLint at_has_picture;
    GLint at_picture;
    GLint at_clouds;
    /* the clouds' cube (port_GlSkyBakeClouds) and what makes it */
    GLuint bake_program;
    GLint bake_at_face;
    GLint bake_at_side;
    GLuint clouds;
    /* the stage's frames counted at the last clear: a clear after which
     * the renderer drew the stage's tiles is a fight's frame */
    s32 last_frames;
    s32 is_stage_frame;
    /* a mod's picture of the sky, for the stage it was looked for */
    s32 picture_stage;
    GLuint picture;
    /* what the log has been told */
    s32 said_stage;
    s32 has_news;
    char news[160];
    u32 drawn;
} g_sky;

/* ---- starting ---- */

static GLuint port_GlSkyShader(GLenum kind, const char *text) {
    GLuint shader = glCreateShader(kind);
    GLint ok = 0;

    glShaderSource(shader, 1, &text, NULL);
    glCompileShader(shader);
    glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[LOG_SIZE];

        log[0] = 0;
        glGetShaderInfoLog(shader, sizeof(log), NULL, log);
        snprintf(
            g_sky.news, sizeof(g_sky.news), "the sky's shader: %.120s", log);
        return 0;
    }
    return shader;
}

static GLuint port_GlSkyLink(GLuint vertex, GLuint fragment) {
    GLuint program = glCreateProgram();
    GLint ok = 0;

    glAttachShader(program, vertex);
    glAttachShader(program, fragment);
    glLinkProgram(program);
    glGetProgramiv(program, GL_LINK_STATUS, &ok);
    return ok ? program : 0;
}

/* The functions, the shader and its uniforms (gl_sky.h). */
const char *port_GlSkyStart(void *(*getproc)(const char *name)) {
    GLuint vertex;
    GLuint fragment;
    GLuint bake;

#define GL(result, name, args)                                                 \
    name = (result(*) args)getproc(#name);                                     \
    if (name == NULL) {                                                        \
        return "OpenGL has no " #name;                                         \
    }
    PORT_GL_SKY_FUNCTIONS
#undef GL
    vertex = port_GlSkyShader(GL_VERTEX_SHADER, g_sky_vertex_text);
    fragment = port_GlSkyShader(GL_FRAGMENT_SHADER, g_sky_fragment_text);
    bake = port_GlSkyShader(GL_FRAGMENT_SHADER, g_sky_bake_text);
    if (vertex == 0 || fragment == 0 || bake == 0) {
        return g_sky.news;
    }
    g_sky.program = port_GlSkyLink(vertex, fragment);
    g_sky.bake_program = port_GlSkyLink(vertex, bake);
    if (g_sky.program == 0 || g_sky.bake_program == 0) {
        return "the sky's shaders do not link";
    }
    g_sky.bake_at_face = glGetUniformLocation(g_sky.bake_program, "u_face");
    g_sky.bake_at_side = glGetUniformLocation(g_sky.bake_program, "u_side");
    g_sky.clouds = 0;
    g_sky.at_turn = glGetUniformLocation(g_sky.program, "u_turn");
    g_sky.at_middle = glGetUniformLocation(g_sky.program, "u_middle");
    g_sky.at_h = glGetUniformLocation(g_sky.program, "u_h");
    g_sky.at_scale = glGetUniformLocation(g_sky.program, "u_scale");
    g_sky.at_far = glGetUniformLocation(g_sky.program, "u_far");
    g_sky.at_ambient = glGetUniformLocation(g_sky.program, "u_ambient");
    g_sky.at_moon = glGetUniformLocation(g_sky.program, "u_moon");
    g_sky.at_moon_color = glGetUniformLocation(g_sky.program, "u_moon_color");
    g_sky.at_has_picture = glGetUniformLocation(g_sky.program, "u_has_picture");
    g_sky.at_picture = glGetUniformLocation(g_sky.program, "u_picture");
    g_sky.at_clouds = glGetUniformLocation(g_sky.program, "u_clouds");
    /* the triangle is made of its vertices' numbers alone, but a core
     * context draws nothing with no vertex array bound */
    glGenVertexArrays(1, &g_sky.array);
    g_sky.picture_stage = -1;
    g_sky.picture = 0;
    g_sky.said_stage = -1;
    g_sky.is_started = TRUE;
    return NULL;
}

void port_GlSkySetKind(s32 kind) { g_sky.kind = kind; }

u32 port_GlSkyDrawn(void) { return g_sky.drawn; }

const char *port_GlSkyNews(void) {
    if (!g_sky.has_news) {
        return NULL;
    }
    g_sky.has_news = FALSE;
    return g_sky.news;
}

/* ---- a mod's picture of the sky ---- */

/* The picture a mod gives this stage's sky (`skies/stage_NN.png`, or
 * `skies/default.png` for every stage it has none for), on the card, or
 * 0. Looked for once a stage. */
static GLuint port_GlSkyPicture(s32 stage) {
    char key[KEY_SIZE];
    const char *path;
    PortPngInfo info;
    char problem[PROBLEM_SIZE];
    u32 size;
    u8 *file = NULL;
    u8 *work = NULL;
    u8 *rgba = NULL;
    GLuint texture = 0;

    if (stage == g_sky.picture_stage) {
        return g_sky.picture;
    }
    g_sky.picture_stage = stage;
    g_sky.picture = 0;
    snprintf(key, sizeof(key), "stage_%02d", (int)stage);
    path = port_ModFind(PORT_MOD_SKIES, key);
    if (path == NULL) {
        path = port_ModFind(PORT_MOD_SKIES, "default");
    }
    if (path == NULL) {
        return 0;
    }
    problem[0] = 0;
    size = port_ModFileSize(path);
    file = size > 0 ? malloc(size) : NULL;
    if (file != NULL && port_ModReadFile(path, file, size) == size &&
        port_PngMeasure(file, size, &info, problem, sizeof(problem))) {
        work = malloc(info.work_bytes);
        rgba = malloc((size_t)info.w * (size_t)info.h * 4);
        if (work != NULL && rgba != NULL &&
            port_PngDecode(file, size, work, rgba, problem, sizeof(problem))) {
            glActiveTexture(GL_TEXTURE0);
            glGenTextures(1, &texture);
            glBindTexture(GL_TEXTURE_2D, texture);
            glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, info.w, info.h, 0, GL_RGBA,
                         GL_UNSIGNED_BYTE, rgba);
            glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
            glGenerateMipmap(GL_TEXTURE_2D);
            glTexParameteri(
                GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            /* round the eye from left to right, and not over the top */
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        }
    }
    free(rgba);
    free(work);
    free(file);
    g_sky.picture = texture;
    snprintf(g_sky.news, sizeof(g_sky.news),
             texture != 0 ? "sky: stage %d is the mod's picture %s"
                          : "sky: stage %d: the mod's picture %s is not used",
             (int)stage, path);
    g_sky.has_news = TRUE;
    return texture;
}

/* ---- the clouds ---- */

/* The clouds' cube, made the first time a sky is painted: six faces of
 * CLOUD_SIDE texels a side, each texel the clouds' noise of the
 * direction through it, read smoothly between texels (a texel is about
 * a tenth of a degree, two pixels of a 2560-wide picture: the noise's
 * finest octave is still a few texels across where the clouds are). It
 * does not change with the stage: the clouds' colours are the stage's,
 * and worked out at each pixel. 0 where it cannot be made. */
static GLuint port_GlSkyBakeClouds(u32 framebuffer) {
    GLuint texture = 0;
    GLuint target = 0;
    s32 face;

    glGenTextures(1, &texture);
    glBindTexture(GL_TEXTURE_CUBE_MAP, texture);
    for (face = 0; face < CUBE_FACES; face++) {
        glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + face, 0, GL_R16F,
                     CLOUD_SIDE, CLOUD_SIDE, 0, GL_RED, GL_FLOAT, NULL);
    }
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    /* a face's edge read with its neighbour's, so that no seam shows */
    glEnable(GL_TEXTURE_CUBE_MAP_SEAMLESS);
    glGenFramebuffers(1, &target);
    glBindFramebuffer(GL_FRAMEBUFFER, target);
    glUseProgram(g_sky.bake_program);
    glBindVertexArray(g_sky.array);
    glUniform1f(g_sky.bake_at_side, (GLfloat)CLOUD_SIDE);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_BLEND);
    glDisable(GL_SCISSOR_TEST);
    glViewport(0, 0, CLOUD_SIDE, CLOUD_SIDE);
    for (face = 0; face < CUBE_FACES; face++) {
        glFramebufferTexture2D(
            GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
            GL_TEXTURE_CUBE_MAP_POSITIVE_X + face, texture, 0);
        glUniform1i(g_sky.bake_at_face, face);
        glDrawArrays(GL_TRIANGLES, 0, 3);
    }
    glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
    glDeleteFramebuffers(1, &target);
    return texture;
}

/* ---- painting it ---- */

/* The stage's light, as the sky is coloured by it: the scenery's ambient
 * colour, and the sun's colour and direction, which is where the moon
 * goes (the shader puts it above the horizon). */
static void port_GlSkyLight(
    GLfloat *ambient, GLfloat *moon, GLfloat *moon_color) {
    const LightSetup *lights = (const LightSetup *)port_RemasterTileLights();
    s32 i;

    for (i = 0; i < 3; i++) {
        ambient[i] = (GLfloat)lights->back[i] / CHANNEL_MAX;
        /* the sun's colour, 1.12, made pale: a moon's */
        moon_color[i] =
            (GLfloat)(0.55 + 0.35 * (double)lights->cmat.m[i][0] / 4096.0);
        moon[i] = (GLfloat)lights->lmat.m[0][i];
    }
}

/* The frame's own clear has just been drawn: the sky over it, where the
 * frame is a fight's on a stage open to the sky (gl_sky.h). */
void port_GlSkyCleared(s32 x, s32 y, s32 w, s32 h, u32 color) {
    const PortStageView *view;
    PortGlPicture picture;
    s32 is_between = FALSE;
    s32 stage;
    GLfloat turn[MATRIX_ELEMENTS];
    GLfloat ambient[3];
    GLfloat moon[3];
    GLfloat moon_color[3];
    GLuint own;
    s32 left;
    s32 right;
    s32 i;

    view = port_GlStageViewNow(&is_between);
    if (!is_between) {
        /* A clear after which the renderer has drawn the stage's tiles
         * is a frame of a fight, the stage in it (the frame's table is
         * built before its clear: main.c). A picture between two frames
         * is of the frame on show, which was decided at its clear. */
        g_sky.is_stage_frame = view->frames != g_sky.last_frames;
        g_sky.last_frames = view->frames;
    }
    if (!g_sky.is_started || g_sky.kind == PORT_SKY_ORIGINAL ||
        !g_sky.is_stage_frame || view->h <= 0) {
        return;
    }
    stage = port_RemasterStageNumber();
    if (stage < 0 || stage >= SKY_STAGES) {
        return;
    }
    if (stage != g_sky.said_stage) {
        g_sky.said_stage = stage;
        snprintf(g_sky.news, sizeof(g_sky.news),
                 g_sky_open[stage] ? "sky: stage %d is open: the night sky"
                                   : "sky: stage %d is closed: the game's "
                                     "colour of the distance",
                 (int)stage);
        g_sky.has_news = TRUE;
    }
    if (!g_sky_open[stage]) {
        return;
    }
    own = port_GlSkyPicture(stage);
    port_GlGetPicture(&picture);
    if (picture.framebuffer == 0 && picture.color_texture == 0) {
        return;
    }
    /* a direction in the view into the world's: the rotation's
     * transpose, which in the card's order of a column after a column is
     * the matrix's own order */
    for (i = 0; i < MATRIX_ELEMENTS; i++) {
        turn[i] = (GLfloat)view->matrix[i] / GTE_ONE;
    }
    port_GlSkyLight(ambient, moon, moon_color);
    if (g_sky.clouds == 0 && own == 0) {
        g_sky.clouds = port_GlSkyBakeClouds(picture.framebuffer);
    }
    glBindFramebuffer(GL_FRAMEBUFFER, picture.framebuffer);
    glUseProgram(g_sky.program);
    glBindVertexArray(g_sky.array);
    glUniformMatrix3fv(g_sky.at_turn, 1, GL_FALSE, turn);
    /* the screen's middle as the coprocessor has it, with the drawing
     * offset of the frame's buffer and a wider picture's band */
    glUniform2f(g_sky.at_middle,
                (GLfloat)view->ofx / (GLfloat)PORT_REMASTER_ONE +
                    (GLfloat)(x + picture.margin),
                (GLfloat)view->ofy / (GLfloat)PORT_REMASTER_ONE + (GLfloat)y);
    glUniform1f(g_sky.at_h, (GLfloat)view->h);
    glUniform2f(
        g_sky.at_scale, (GLfloat)picture.draw_x, (GLfloat)picture.draw_y);
    glUniform3f(g_sky.at_far, (GLfloat)((color >> 16) & 0xFF) / CHANNEL_MAX,
                (GLfloat)((color >> 8) & 0xFF) / CHANNEL_MAX,
                (GLfloat)(color & 0xFF) / CHANNEL_MAX);
    glUniform3f(g_sky.at_ambient, ambient[0], ambient[1], ambient[2]);
    glUniform3f(g_sky.at_moon, moon[0], moon[1], moon[2]);
    glUniform3f(
        g_sky.at_moon_color, moon_color[0], moon_color[1], moon_color[2]);
    glUniform1i(g_sky.at_has_picture, own != 0);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, own);
    glUniform1i(g_sky.at_picture, 0);
    glActiveTexture(GL_TEXTURE0 + 1);
    glBindTexture(GL_TEXTURE_CUBE_MAP, g_sky.clouds);
    glUniform1i(g_sky.at_clouds, 1);
    glActiveTexture(GL_TEXTURE0);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_BLEND);
    glEnable(GL_SCISSOR_TEST);
    /* the frame's rectangle and a wider picture's two bands */
    left = (picture.margin > 0 && picture.display_w > 0)
               ? picture.display_x
               : x + picture.margin;
    right = (picture.margin > 0 && picture.display_w > 0)
                ? picture.display_x + picture.display_w + 2 * picture.margin
                : x + picture.margin + w;
    glScissor(left * picture.draw_x, y * picture.draw_y,
              (right - left) * picture.draw_x, h * picture.draw_y);
    glViewport(0, 0, picture.target_w, picture.target_h);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    port_GlForgetState();
    g_sky.drawn++;
}
