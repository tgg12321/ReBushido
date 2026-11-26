/* A sky behind the stages that are open to it, in the remastered picture
 * (gl_sky.h; docs/PORT.md, "The sky").
 *
 * Painted where the game's own fill of the colour of the distance was,
 * in three steps:
 *
 * - the air (port_GlSkyAir): the clear sky of every direction, worked out
 *   when the stage, its time or its light changes, into a small picture
 *   wrapped round the eye. By day, dawn and dusk it is the sunlight
 *   scattered by the air (the molecules' blue, the haze's glow about the
 *   sun, the ozone's blue overhead at twilight) on its way through a
 *   planet's atmosphere; by night, the stage's own gradient from its
 *   colour of the distance up into the dark.
 * - the clouds (g_sky_low_text): a picture of a few hundred lines,
 *   each pixel the air behind it and the clouds before it -- a layer of
 *   heaped clouds marched through, lit by the sun or the moon through
 *   themselves, by the sky above and below, and seen through the haze
 *   with their distance; a veil of thin high cloud above them; the
 *   glow about the sun or the moon; at night the Milky Way -- the whole
 *   of it drifting with the stage's wind; and below the horizon, ranges
 *   far off in the haze running into the colour of the distance. A
 *   quarter of its pixels is marched each picture, the rest are the last
 *   picture's where the same direction was (g_sky_mix_text). The clouds'
 *   noise is worked out once, into three textures (port_GlSkyMakeNoise).
 * - the frame (port_GlSkyCleared): one triangle over the frame, each
 *   pixel the clouds' picture there, with what must be sharp drawn over
 *   it at the frame's own resolution: the stars, the sun's disc, the
 *   moon's face; each as much as the clouds leave clear.
 *
 * Each pixel works out the direction it looks in from the view the
 * stage's ground is drawn with -- the coprocessor's rotation, its
 * projection distance and its screen offsets, carried out of the
 * renderer (port_RemasterNoteStageView) -- and the sky is a function of
 * that direction and the picture's clock alone, so it turns with the
 * camera and never moves as the camera moves. Or, where a mod gives the
 * stage a picture of its own sky, that picture, wrapped round the eye
 * from left to right and from the top down (an equirectangular picture).
 *
 * Nothing here changes a value the game computes. */
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "common.h"
#include "gl.h"
#include "gl_dressing.h"
#include "gl_lights.h"
#include "gl_sky.h"
#include "gl_time.h"
#include "gl_stage.h"
#include "port_mods.h"
#include "port_png.h"
#include "port_remaster.h"
#include "port_stage_choice.h"

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
#define GL_TEXTURE_3D 0x806F
#define GL_TEXTURE0 0x84C0
#define GL_TEXTURE_MIN_FILTER 0x2801
#define GL_TEXTURE_MAG_FILTER 0x2800
#define GL_TEXTURE_WRAP_S 0x2802
#define GL_TEXTURE_WRAP_T 0x2803
#define GL_TEXTURE_WRAP_R 0x8072
#define GL_LINEAR 0x2601
#define GL_LINEAR_MIPMAP_LINEAR 0x2703
#define GL_REPEAT 0x2901
#define GL_CLAMP_TO_EDGE 0x812F
#define GL_RGBA 0x1908
#define GL_RGBA8 0x8058
#define GL_RGBA16F 0x881A
#define GL_UNSIGNED_BYTE 0x1401
#define GL_HALF_FLOAT 0x140B
#define GL_UNPACK_ALIGNMENT 0x0CF5
#define GL_FRAMEBUFFER 0x8D40
#define GL_DEPTH_TEST 0x0B71
#define GL_BLEND 0x0BE2
#define GL_SCISSOR_TEST 0x0C11
#define GL_VERTEX_SHADER 0x8B31
#define GL_FRAGMENT_SHADER 0x8B30
#define GL_COMPILE_STATUS 0x8B81
#define GL_LINK_STATUS 0x8B82
#define GL_COLOR_ATTACHMENT0 0x8CE0

#define PORT_GL_SKY_FUNCTIONS                                                  \
    GL(void, glViewport, (GLint, GLint, GLsizei, GLsizei))                     \
    GL(void, glScissor, (GLint, GLint, GLsizei, GLsizei))                      \
    GL(void, glEnable, (GLenum))                                               \
    GL(void, glDisable, (GLenum))                                              \
    GL(void, glBindTexture, (GLenum, GLuint))                                  \
    GL(void, glActiveTexture, (GLenum))                                        \
    GL(void, glGenTextures, (GLsizei, GLuint *))                               \
    GL(void, glDeleteTextures, (GLsizei, const GLuint *))                      \
    GL(void, glTexImage2D,                                                     \
       (GLenum, GLint, GLint, GLsizei, GLsizei, GLint, GLenum, GLenum,         \
        const void *))                                                         \
    GL(void, glTexImage3D,                                                     \
       (GLenum, GLint, GLint, GLsizei, GLsizei, GLsizei, GLint, GLenum,        \
        GLenum, const void *))                                                 \
    GL(void, glTexParameteri, (GLenum, GLenum, GLint))                         \
    GL(void, glGenerateMipmap, (GLenum))                                       \
    GL(void, glPixelStorei, (GLenum, GLint))                                   \
    GL(void, glBindFramebuffer, (GLenum, GLuint))                              \
    GL(void, glGenFramebuffers, (GLsizei, GLuint *))                           \
    GL(void, glFramebufferTexture2D, (GLenum, GLenum, GLenum, GLuint, GLint))  \
    GL(void, glFramebufferTextureLayer,                                        \
       (GLenum, GLenum, GLuint, GLint, GLint))                                 \
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
    GL(void, glUniform3fv, (GLint, GLsizei, const GLfloat *))                  \
    GL(void, glUniform2fv, (GLint, GLsizei, const GLfloat *))                  \
    GL(void, glUniform4fv, (GLint, GLsizei, const GLfloat *))                  \
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
/* A rotation's nine numbers */
#define MATRIX_ELEMENTS 9
/* How long a shader's complaint and a mod's file's key may be */
#define LOG_SIZE 512
#define KEY_SIZE 16
#define PROBLEM_SIZE 96
/* The air's picture round the eye: texels from left to right (all the way
 * round) and from the nadir to the zenith (port_GlSkyAir) */
#define AIR_W 192
#define AIR_H 96
/* The clouds' noise (port_GlSkyMakeNoise): their shapes and the detail
 * that wears their edges, cubes of texels that tile; the weather over
 * them, a square that tiles */
#define SHAPE_SIDE 128
#define DETAIL_SIDE 32
#define WEATHER_SIDE 512
/* The clouds' picture (g_sky_low_text): about this many lines for the
 * frame's height, whatever the picture's own, read smoothly between its
 * pixels (the clouds are soft; what is sharp is drawn at the frame's) */
#define LOW_LINES 360
/* The clouds' pictures are made once, this size, while the game loads;
 * a frame uses as much of them as its own size needs (a frame wider than
 * they are is read at a coarser step) */
#define LOW_W_MOST 1600
#define LOW_H_MOST 480
/* how many steps a pixel of the clouds' picture takes through the layer */
#define CLOUD_STEPS 96
/* The game's frames a second, of the picture's clock (the clouds' drift) */
#define FRAMES_A_SECOND 60.0f
#define DEGREES_HALF 180.0f
#define PI_F 3.14159265f
/* a number of this file's written into a shader */
#define SKY_WORD(x) #x
#define SKY_NUMBER(x) SKY_WORD(x)

/* Which stages are open to the sky: looked at, stage by stage, over the
 * prover's tour of each (tools/port_stage_prove.py; docs/PORT.md, "The
 * sky"). A stage that is a hall or a cave keeps the game's fill: a star
 * in a gap between two of its walls would be a hole, not a sky. What lies
 * beyond an open one, below the horizon: land (SKY_LAND), ranges far off
 * in the haze, or the sea (SKY_SEA), the sky in it, a headland or an
 * island here and there. */
#define SKY_LAND 1
#define SKY_SEA 2
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
    2, /* Kimigaura coast: the sea beyond */
    1, /* the orange tree fields */
    0, /* the castle rooftop: a sky and a moon of its own (stage_hooks.c) */
    1, /* under the lost bridge */
    1, /* the terraced fields */
    1, /* under the railway */
    1, /* the water tower dam */
    1, /* the lumberyard */
    2, /* the lighthouse: the sea beyond */
    1, /* the kabuki stage */
    2, /* the abandoned ship: the sea beyond */
    2, /* the ferry dock: the sea beyond */
    1, /* the Shainto gate */
    0, /* the purple cavern: a cave */
    1, /* the abandoned station */
    1, /* the railway */
    1, /* the abandoned tunnel: the open ground before its mouth */
    2, /* the abandoned shipyard: the sea beyond */
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

s32 port_GlSkyIsOpen(s32 stage) {
    return stage >= 0 && stage < SKY_STAGES && g_sky_open[stage];
}

/* ---- each time's sky ---- */

/* What a time of day makes of the sky (judged on the pictures of
 * docs/PORT.md, "The sky"), beside its colours of gl_time.c. The first is
 * the stage's own time, the game's night. */
typedef struct {
    float cover;        /* how much of the sky the heaped clouds cover, 0-1 */
    float cirrus;       /* the veil of high cloud, 0-1 */
    float air_height;   /* degrees: the height of the sun the air's colour is
                           worked out for, lower than the sun's own at dawn
                           and dusk so that its light is the time's deep gold
                           (the sun's own colour in gl_time.c); 0: its own */
    float haze;         /* the haze in the air, times a clear day's */
    float power;        /* the sunlight in the air */
    float light;        /* the light on the clouds, times the sun's colour */
    float glow;         /* the glow about the sun or the moon */
    float milky;        /* the Milky Way */
    float horizon;      /* how much of the horizon is the colour of the
                           distance, which the ground's fog runs into */
    float shade;        /* the sky's light on the clouds, times its own */
    float deepen;       /* the sun's colour on the clouds made deeper: its
                           hue to this power */
    float clearing;     /* the clouds thinned about the sun or the moon */
    float shown;        /* degrees: the sun's or the moon's disc drawn at most
                           this high, on its own bearing (0: where it is) */
    float paint;        /* how much of the time's palette is laid over the
                           air's own colours, 0-1 */
    float palette;      /* which: 0 dawn's, 1 the day's, 2 dusk's */
    float boost;        /* the horizon under the sun lit up by the palette */
    float deepen_above; /* the sky darker upwards by the palette, 0-1 */
    float vivid;        /* the colours made more vivid: 1 as they are */
    float under;        /* the low sun's light under the clouds, times its
                           colour on them */
    float rays;         /* the rays of a low sun fanning out, 0-1 */
    float tops;         /* metres: the tallest clouds' tops */
    float far_haze;     /* metres: how far off the haze hides half of a
                           cloud */
    float thin_far;     /* how much the clouds thin far off, 0-1: a clear
                           band of sky over the horizon */
    float scattered;    /* the light scattered many times in the clouds,
                           which lights their sides: 1 a fair day's */
} SkyLook;

static const SkyLook g_sky_looks[PORT_TIME_COUNT] = {
    /* the stage's own: the game's night */
    {
        .cover = 0.42f,
        .cirrus = 0.25f,
        .air_height = 0.0f,
        .haze = 1.0f,
        .power = 0.0f,
        .light = 0.55f,
        .glow = 1.0f,
        .milky = 1.0f,
        .horizon = 0.55f,
        .shade = 1.0f,
        .deepen = 1.0f,
        .clearing = 0.8f,
        .shown = 5.0f,
        .paint = 0.0f,
        .palette = 0.0f,
        .boost = 0.0f,
        .deepen_above = 0.0f,
        .vivid = 1.1f,
        .under = 0.0f,
        .rays = 0.0f,
        .tops = 5200.0f,
        .far_haze = 76000.0f,
        .thin_far = 0.0f,
        .scattered = 1.0f,
    },
    /* dawn: the sun rising in pink and peach under a high veil */
    {
        .cover = 0.38f,
        .cirrus = 0.70f,
        .air_height = 6.0f,
        .haze = 0.4f,
        .power = 32.0f,
        .light = 4.0f,
        .glow = 1.0f,
        .milky = 0.0f,
        .horizon = 0.1f,
        .shade = 0.5f,
        .deepen = 2.0f,
        .clearing = 0.4f,
        .shown = 4.0f,
        .paint = 0.8f,
        .palette = 0.0f,
        .boost = 2.0f,
        .deepen_above = 0.5f,
        .vivid = 1.35f,
        .under = 1.0f,
        .rays = 0.5f,
        .tops = 6500.0f,
        .far_haze = 160000.0f,
        .thin_far = 0.0f,
        .scattered = 0.6f,
    },
    /* day: fair-weather heaps in a deep blue */
    {
        .cover = 0.38f,
        .cirrus = 0.35f,
        .air_height = 0.0f,
        .haze = 0.6f,
        .power = 20.0f,
        .light = 3.5f,
        .glow = 0.6f,
        .milky = 0.0f,
        .horizon = 0.05f,
        .shade = 1.0f,
        .deepen = 1.0f,
        .clearing = 0.0f,
        .shown = 0.0f,
        .paint = 0.85f,
        .palette = 1.0f,
        .boost = 0.0f,
        .deepen_above = 0.3f,
        .vivid = 1.3f,
        .under = 0.0f,
        .rays = 0.0f,
        .tops = 8000.0f,
        .far_haze = 60000.0f,
        .thin_far = 0.6f,
        .scattered = 1.3f,
    },
    /* dusk: the sun's last light under a high veil, the clouds aflame */
    {
        .cover = 0.48f,
        .cirrus = 0.85f,
        .air_height = 3.0f,
        .haze = 0.8f,
        .power = 30.0f,
        .light = 5.5f,
        .glow = 1.2f,
        .milky = 0.0f,
        .horizon = 0.1f,
        .shade = 0.5f,
        .deepen = 2.0f,
        .clearing = 0.6f,
        .shown = 3.5f,
        .paint = 0.85f,
        .palette = 2.0f,
        .boost = 3.0f,
        .deepen_above = 0.5f,
        .vivid = 1.3f,
        .under = 0.9f,
        .rays = 0.8f,
        .tops = 7000.0f,
        .far_haze = 140000.0f,
        .thin_far = 0.0f,
        .scattered = 0.5f,
    },
    /* night: broken clouds under the moon, the Milky Way */
    {
        .cover = 0.40f,
        .cirrus = 0.30f,
        .air_height = 0.0f,
        .haze = 1.0f,
        .power = 0.0f,
        .light = 0.65f,
        .glow = 1.0f,
        .milky = 1.0f,
        .horizon = 0.55f,
        .shade = 1.0f,
        .deepen = 1.0f,
        .clearing = 0.8f,
        .shown = 5.0f,
        .paint = 0.0f,
        .palette = 0.0f,
        .boost = 0.0f,
        .deepen_above = 0.0f,
        .vivid = 1.1f,
        .under = 0.0f,
        .rays = 0.0f,
        .tops = 5200.0f,
        .far_haze = 76000.0f,
        .thin_far = 0.0f,
        .scattered = 1.0f,
    },
};

/* The quarter of the clouds' picture each picture marches, by turns: the
 * corners of a square of four pixels (0 its lower left, 1 its lower
 * right, 2 its upper left, 3 its upper right) taken crosswise. */
static const u8 g_sky_quarters[4] = {0, 3, 1, 2};

/* What the rain makes of any time's sky: a ceiling of cloud. */
#define RAIN_COVER 0.96f
#define RAIN_DARK 0.45f

/* ---- the shaders ---- */

static const char g_sky_vertex_text[] =
    "#version 330 core\n"
    /* one triangle that covers the whole of the target */
    "void main() {\n"
    "    vec2 at = vec2(float((gl_VertexID & 1) * 4) - 1.0,\n"
    "                   float((gl_VertexID & 2) * 2) - 1.0);\n"
    "    gl_Position = vec4(at, 0.0, 1.0);\n"
    "}\n";

/* The direction a pixel of the frame looks in, of the view the ground is
 * drawn with; and a direction turned so that y is up (the world's y runs
 * down), in which the sky's shaders work. */
#define SKY_VIEW                                                                \
    "uniform mat3 u_turn;\n"                                                    \
    "uniform vec2 u_middle;\n" /* the screen's middle, video memory's px */     \
    "uniform float u_h;\n"     /* the projection's distance */                  \
    "uniform vec2 u_scale;\n"  /* the buffer's pixels to one of video's */      \
    "const float PI = 3.14159265;\n"                                            \
    "vec3 sky_dir(vec2 at) {\n"                                                 \
    "    vec2 screen = (at - vec2(0.5)) / u_scale;\n" /* the console's frame    \
                                                       * is half as tall as it  \
                                                       * is wide: a line of it  \
                                                       * is two of the view's   \
                                                       * units */               \
    "    vec3 look = vec3((screen.x - u_middle.x) / u_h,\n"                     \
    "                     2.0 * (screen.y - u_middle.y) / u_h, 1.0);\n"         \
    "    vec3 w = normalize(u_turn * look);\n"                                  \
    "    return vec3(w.x, -w.y, w.z);\n"                                        \
    "}\n" /* where a direction is in the air's picture: round the eye from      \
           * left to right, and from the nadir to the zenith, the texels        \
           * closer together near the horizon where the air changes most */     \
    "vec2 air_at(vec3 d) {\n"                                                   \
    "    float e = asin(clamp(d.y, -1.0, 1.0)) / (0.5 * PI);\n"                 \
    "    return vec2(atan(d.x, d.z) / (2.0 * PI) + 0.5,\n"                      \
    "                0.5 + 0.5 * sign(e) * sqrt(abs(e)));\n"                    \
    "}\n"                                                                       \
    "vec3 air_dir(vec2 uv) {\n"                                                 \
    "    float t = uv.y * 2.0 - 1.0;\n"                                         \
    "    float e = sign(t) * t * t * 0.5 * PI;\n"                               \
    "    float a = (uv.x - 0.5) * 2.0 * PI;\n"                                  \
    "    return vec3(cos(e) * sin(a), sin(e), cos(e) * cos(a));\n"              \
    "}\n" /* the light scattered by a drop or a crystal, by how far it turns    \
           * from its way (Henyey and Greenstein's) */                          \
    "float scatter(float mu, float g) {\n"                                      \
    "    float gg = g * g;\n"                                                   \
    "    return (1.0 - gg) / (4.0 * PI * pow(1.0 + gg - 2.0 * g * mu, 1.5));\n" \
    "}\n"

/* The clouds' noise, made once into three textures that tile
 * (port_GlSkyMakeNoise): a hash of whole numbers, so that it is the same
 * on every card; a noise of gradients (Perlin's) and one of cells
 * (Worley's), each repeating after `period` of its cells. */
static const char g_sky_noise_text[] =
    "#version 330 core\n"
    "uniform int u_what;\n" /* 0 the shapes, 1 the detail, 2 the weather */
    "uniform float u_side;\n"
    "uniform float u_layer;\n"
    "layout(location = 0) out vec4 o_noise;\n"
    "uvec3 stir(uvec3 v) {\n"
    "    v = v * 1664525u + 1013904223u;\n"
    "    v.x += v.y * v.z; v.y += v.z * v.x; v.z += v.x * v.y;\n"
    "    v ^= v >> 16u;\n"
    "    v.x += v.y * v.z; v.y += v.z * v.x; v.z += v.x * v.y;\n"
    "    return v;\n"
    "}\n"
    "vec3 random3(ivec3 cell, ivec3 period, int seed) {\n"
    "    uvec3 c = uvec3((cell + period * 4) % period);\n"
    "    uint s = uint(seed);\n"
    "    return vec3(stir(c + uvec3(s * 7919u, s * 104729u, s * 1299709u))\n"
    "                >> 8u) / 16777216.0;\n"
    "}\n"
    "float gradient(vec3 p, ivec3 period, int seed) {\n"
    "    ivec3 i = ivec3(floor(p));\n"
    "    vec3 f = p - floor(p);\n"
    "    vec3 u = f * f * f * (f * (f * 6.0 - 15.0) + 10.0);\n"
    "    float n[8];\n"
    "    for (int k = 0; k < 8; k++) {\n"
    "        ivec3 o = ivec3(k & 1, (k >> 1) & 1, (k >> 2) & 1);\n"
    "        vec3 g = random3(i + o, period, seed) * 2.0 - 1.0;\n"
    "        n[k] = dot(normalize(g + vec3(1e-4)), f - vec3(o));\n"
    "    }\n"
    "    return mix(mix(mix(n[0], n[1], u.x), mix(n[2], n[3], u.x), u.y),\n"
    "               mix(mix(n[4], n[5], u.x), mix(n[6], n[7], u.x), u.y),\n"
    "               u.z);\n"
    "}\n"
    /* 1 at a cell's point, falling away from it */
    "float cells(vec3 p, ivec3 period, int seed) {\n"
    "    ivec3 i = ivec3(floor(p));\n"
    "    vec3 f = p - floor(p);\n"
    "    float d = 1.0;\n"
    "    for (int z = -1; z <= 1; z++) {\n"
    "        for (int y = -1; y <= 1; y++) {\n"
    "            for (int x = -1; x <= 1; x++) {\n"
    "                ivec3 o = ivec3(x, y, z);\n"
    "                vec3 q = vec3(o) + random3(i + o, period, seed) - f;\n"
    "                d = min(d, dot(q, q));\n"
    "            }\n"
    "        }\n"
    "    }\n"
    "    return 1.0 - sqrt(d);\n"
    "}\n"
    "float gradients(vec3 p, ivec3 period, int seed, int octaves) {\n"
    "    float sum = 0.0;\n"
    "    float part = 0.5;\n"
    "    for (int i = 0; i < octaves; i++) {\n"
    "        sum += part * gradient(p, period, seed + i);\n"
    "        p *= 2.0;\n"
    "        period *= 2;\n"
    "        part *= 0.5;\n"
    "    }\n"
    "    return clamp(sum * 1.1 + 0.5, 0.0, 1.0);\n"
    "}\n"
    "float cellses(vec3 p, ivec3 period, int seed) {\n"
    "    return cells(p, period, seed) * 0.625\n"
    "         + cells(p * 2.0, period * 2, seed + 1) * 0.25\n"
    "         + cells(p * 4.0, period * 4, seed + 2) * 0.125;\n"
    "}\n"
    "void main() {\n"
    "    vec3 at = vec3(gl_FragCoord.xy, u_layer + 0.5) / u_side;\n"
    "    if (u_what == 0) {\n"
    /* the shapes: billows (Perlin's noise made lumpy by Worley's), and
     * three sizes of cells that carve them */
    "        float soft = gradients(at * 4.0, ivec3(4), 1, 5);\n"
    "        float lumps = cellses(at * 4.0, ivec3(4), 10);\n"
    "        float billow = lumps + soft * (1.0 - lumps);\n"
    "        o_noise = vec4(billow, cellses(at * 8.0, ivec3(8), 20),\n"
    "                       cellses(at * 16.0, ivec3(16), 30),\n"
    "                       cellses(at * 32.0, ivec3(32), 40));\n"
    "    } else if (u_what == 1) {\n"
    "        o_noise = vec4(cellses(at * 4.0, ivec3(4), 50),\n"
    "                       cellses(at * 8.0, ivec3(8), 60),\n"
    "                       cellses(at * 16.0, ivec3(16), 70), 1.0);\n"
    "    } else {\n"
    /* the weather: where the clouds gather, how tall they grow, and the
     * high veil's streaks and their breaking up */
    "        vec3 flat_at = vec3(at.xy, 0.37);\n"
    "        o_noise = vec4(gradients(flat_at * vec3(4.0, 4.0, 1.0),\n"
    "                                 ivec3(4, 4, 1), 80, 6),\n"
    "                       gradients(flat_at * vec3(3.0, 3.0, 1.0),\n"
    "                                 ivec3(3, 3, 1), 90, 4),\n"
    "                       gradients(flat_at * vec3(3.0, 14.0, 1.0),\n"
    "                                 ivec3(3, 14, 1), 100, 6),\n"
    "                       cellses(flat_at * vec3(12.0, 12.0, 1.0),\n"
    "                               ivec3(12, 12, 1), 110));\n"
    "    }\n"
    "}\n";

/* The air's picture (port_GlSkyAir). By day, dawn and dusk: the light of
 * the sun scattered once into the eye along each direction, through a
 * planet's air of molecules, haze and ozone, the sunlight at each step
 * dimmed by the air between it and the sun. By night: the stage's own
 * gradient, its colour of the distance at the horizon and a glow of its
 * ambient light just above, darkening upwards, and the moon's light in
 * the air on its side. */
static const char g_sky_air_text[] =
    "#version 330 core\n" SKY_VIEW "uniform int u_night;\n"
    "uniform vec3 u_sun;\n"     /* the sun's way, y up */
    "uniform vec3 u_sun_air;\n" /* the way the air's light is dimmed along */
    "uniform float u_haze;\n"
    "uniform float u_power;\n"
    "uniform vec3 u_low;\n"  /* the night's colours at the horizon */
    "uniform vec3 u_high;\n" /* and above */
    "uniform vec3 u_moon_light;\n"
    "uniform vec3 u_belt;\n"  /* the Belt of Venus, dawn's and dusk's */
    "uniform vec4 u_paint;\n" /* the time's palette: how much of it, which
                                 (0 dawn, 1 day, 2 dusk), and the glow it
                                 adds low on the sun's side */
    "layout(location = 0) out vec4 o_air;\n"
    "const float GROUND = 6360e3;\n"
    "const float TOP = 6420e3;\n"
    "const vec3 RAYLEIGH = vec3(5.8e-6, 13.5e-6, 33.1e-6);\n"
    "const float MIE = 21e-6;\n"
    "const vec3 OZONE = vec3(1.3e-6, 3.76e-6, 0.17e-6);\n"
    /* the air's three kinds at a height: molecules, haze, ozone */
    "vec3 thickness(float height) {\n"
    "    return vec3(exp(-height / 8000.0), exp(-height / 1200.0),\n"
    "                max(0.0, 1.0 - abs(height - 25000.0) / 15000.0));\n"
    "}\n"
    "float out_of(vec3 o, vec3 d, float r) {\n"
    "    float b = dot(o, d);\n"
    "    float c = dot(o, o) - r * r;\n"
    "    float h = b * b - c;\n"
    "    return h < 0.0 ? -1.0 : -b + sqrt(h);\n"
    "}\n"
    "float into(vec3 o, vec3 d, float r) {\n"
    "    float b = dot(o, d);\n"
    "    float c = dot(o, o) - r * r;\n"
    "    float h = b * b - c;\n"
    "    return h < 0.0 ? -1.0 : -b - sqrt(h);\n"
    "}\n"
    "vec3 extinction(vec3 depth) {\n"
    "    return RAYLEIGH * depth.x + MIE * 1.1 * u_haze * depth.y\n"
    "         + OZONE * depth.z;\n"
    "}\n"
    /* The time's palette: the hue of the sky at elevation `e` (radians)
     * on the sun's side (`toward` 1) and away from it (0). The air worked
     * out alone is pale; a painter's sunset has the gold and the flame at
     * the horizon under the sun, magenta and violet above them, the
     * Earth's shadow and the Belt of Venus opposite. Dawn's is the same
     * in peach and rose, the day's a blue deepening upwards. */
    "vec3 band(float e, vec3 a, vec3 b, vec3 c, vec3 d, vec3 z) {\n"
    "    vec3 h = mix(a, b, smoothstep(0.0, 0.07, e));\n"
    "    h = mix(h, c, smoothstep(0.05, 0.22, e));\n"
    "    h = mix(h, d, smoothstep(0.18, 0.55, e));\n"
    "    return mix(h, z, smoothstep(0.5, 1.2, e));\n"
    "}\n"
    "vec3 palette(float e, float toward, float which) {\n"
    "    vec3 sun_side;\n"
    "    vec3 away;\n"
    "    if (which < 0.5) {\n"
    "        sun_side = band(e, vec3(1.0, 0.70, 0.32), vec3(1.0, 0.46, 0.42),\n"
    "                        vec3(0.80, 0.44, 0.82), vec3(0.42, 0.48, 0.95),\n"
    "                        vec3(0.24, 0.40, 0.86));\n"
    "        away = band(e, vec3(0.46, 0.48, 0.80), vec3(1.0, 0.56, 0.72),\n"
    "                    vec3(0.62, 0.52, 0.86), vec3(0.40, 0.50, 0.90),\n"
    "                    vec3(0.24, 0.40, 0.86));\n"
    "    } else if (which < 1.5) {\n"
    "        sun_side = band(e, vec3(0.70, 0.82, 1.0), vec3(0.42, 0.62, 1.0),\n"
    "                        vec3(0.32, 0.54, 1.0), vec3(0.20, 0.40, 0.95),\n"
    "                        vec3(0.13, 0.30, 0.88));\n"
    "        away = sun_side;\n"
    "    } else {\n"
    "        sun_side = band(e, vec3(1.0, 0.46, 0.08), vec3(1.0, 0.30, 0.10),\n"
    "                        vec3(0.92, 0.26, 0.40), vec3(0.42, 0.26, 0.70),\n"
    "                        vec3(0.14, 0.18, 0.55));\n"
    "        away = band(e, vec3(0.40, 0.34, 0.62), vec3(0.95, 0.42, 0.55),\n"
    "                    vec3(0.55, 0.32, 0.68), vec3(0.28, 0.26, 0.66),\n"
    "                    vec3(0.14, 0.18, 0.55));\n"
    "    }\n"
    "    return mix(away, sun_side, toward);\n"
    "}\n"
    "void main() {\n"
    "    vec3 d = air_dir(gl_FragCoord.xy / vec2(" SKY_NUMBER(AIR_W) ".0, " SKY_NUMBER(
        AIR_H) ".0));\n"
               "    float e = max(d.y, 0.0);\n"
               "    if (u_night != 0) {\n"
               "        vec3 c = mix(u_low, u_high, smoothstep(0.0, 0.45, e));\n"
               "        c = pow(max(c, vec3(0.0)), vec3(2.2));\n"
               "        float mu = dot(d, u_sun);\n"
               "        c += u_moon_light * (0.75 * (1.0 + mu * mu) * 0.06\n"
               "                           + scatter(mu, 0.7) * 0.05)\n"
               "           * (0.4 + 0.6 * exp(-e * 3.0));\n"
               "        o_air = vec4(c, 1.0);\n"
               "        return;\n"
               "    }\n"
               /* below the horizon, the horizon's own: the ground's fog is laid
                * over it in the clouds' pass */
               "    d = normalize(vec3(d.x, max(d.y, 0.002), d.z));\n"
               "    vec3 eye = vec3(0.0, GROUND + 100.0, 0.0);\n"
               "    float far = out_of(eye, d, TOP);\n"
               "    const int STEPS = 24;\n"
               "    const int LIGHT_STEPS = 6;\n"
               "    float step_size = far / float(STEPS);\n"
               "    vec3 depth = vec3(0.0);\n"
               "    vec3 sum_r = vec3(0.0);\n"
               "    vec3 sum_m = vec3(0.0);\n"
               "    for (int i = 0; i < STEPS; i++) {\n"
               "        vec3 p = eye + d * step_size * (float(i) + 0.5);\n"
               "        float height = length(p) - GROUND;\n"
               "        vec3 here = thickness(height) * step_size;\n"
               "        depth += here * 0.5;\n"
               "        float ground = into(p, u_sun_air, GROUND);\n"
               "        if (ground < 0.0) {\n"
               "            float to_sun = out_of(p, u_sun_air, TOP);\n"
               "            float light_size = to_sun / float(LIGHT_STEPS);\n"
               "            vec3 light_depth = vec3(0.0);\n"
               "            for (int j = 0; j < LIGHT_STEPS; j++) {\n"
               "                vec3 q = p + u_sun_air * light_size * (float(j) + 0.5);\n"
               "                light_depth += thickness(length(q) - GROUND) * light_size;\n"
               "            }\n"
               "            vec3 seen = exp(-extinction(depth + light_depth));\n"
               "            sum_r += seen * here.x;\n"
               "            sum_m += seen * here.y;\n"
               "        }\n"
               "        depth += here * 0.5;\n"
               "    }\n"
               "    float mu = dot(d, u_sun);\n"
               "    vec3 c = u_power * (sum_r * RAYLEIGH * 0.0596831 * (1.0 + mu * mu)\n"
               "                      + sum_m * MIE * u_haze * scatter(mu, 0.76));\n"
               /* the Belt of Venus: the far side of a dawn or a dusk, a band of
                * pink over the horizon where the air still has the low sun's
                * light */
               "    float away = 0.5 - 0.5 * dot(normalize(d.xz + vec2(1e-5)),\n"
               "                                 normalize(u_sun.xz + vec2(1e-5)));\n"
               "    c += u_belt * away * smoothstep(0.0, 0.06, e)\n"
               "       * (1.0 - smoothstep(0.10, 0.32, e));\n"
               /* the time's palette over the air's own: its brightness kept,
                * its hue the painter's, the horizon under the sun lit up */
               "    if (u_paint.x > 0.0) {\n"
               "        const vec3 LUMA = vec3(0.2126, 0.7152, 0.0722);\n"
               "        float toward = pow(1.0 - away, 1.6);\n"
               "        vec3 hue = palette(e, toward, u_paint.y);\n"
               "        float bright = dot(c, LUMA)\n"
               "                     * (1.0 + u_paint.z * toward * toward * toward\n"
               "                                    * exp(-e * 9.0))\n"
               "                     * (1.0 - u_paint.w * smoothstep(0.02, 0.3, e));\n"
               "        c = mix(c, hue / dot(hue, LUMA) * bright, u_paint.x);\n"
               "    }\n"
               "    o_air = vec4(c, 1.0);\n"
               "}\n";

/* What lies before the sky, below the horizon and on it, which the
 * frame draws at its own resolution (g_sky_fragment_text) so that the
 * ranges' ridges are sharp, and the light and colour functions the
 * clouds share with it: the glow about the sun or the moon, at `apart`
 * radians from it; light made a colour of the screen, by a curve that
 * keeps a bright light's hue more than each channel's own would; the
 * screen's colour made more vivid by the look; the ranges far off round
 * the horizon, three of them one before another, the top of range `k`
 * (0 the farthest) at a bearing a noise of the bearing alone, their
 * ridges a noise folded at its middle so that it peaks; and the lands:
 * the ranges low on the horizon, each paler the farther it is, their
 * valleys in mist and their ridges caught by the light on its side;
 * beyond the stage's ground, the sea with the sky in it and the light's
 * path on it (a headland or an island here and there), or a plain in the
 * haze; the whole running into the colour of the distance where the
 * ground's fog meets it. */
#define SKY_LANDS                                                              \
    "uniform sampler2D u_air;\n"                                               \
    "uniform sampler2D u_weather;\n"                                           \
    "uniform vec3 u_light_way;\n"                                              \
    "uniform vec3 u_light;\n"                                                  \
    "uniform vec3 u_glow;\n"                                                   \
    "uniform vec3 u_far;\n"                                                    \
    "uniform float u_horizon;\n"                                               \
    "uniform vec4 u_look;\n"                                                   \
    "vec3 glow(float apart) {\n"                                               \
    "    return u_glow * (0.5 * exp(-apart * 7.0)\n"                           \
    "                     + 0.6 * exp(-apart * 22.0)\n"                        \
    "                     + 1.2 * exp(-apart * 70.0));\n"                      \
    "}\n"                                                                      \
    "vec3 screen_of(vec3 c) {\n"                                               \
    "    float most = max(max(c.r, c.g), max(c.b, 1e-5));\n"                   \
    "    c = mix(1.0 - exp(-c), c * (1.0 - exp(-most)) / most, 0.7);\n"        \
    "    return pow(c, vec3(1.0 / 2.2));\n"                                    \
    "}\n"                                                                      \
    "float range_top(vec2 bearing, float k) {\n"                               \
    "    float n = textureLod(u_weather, bearing * (0.22 + 0.08 * k)\n"        \
    "                         + vec2(0.31 + 0.17 * k, 0.62), 0.0).r;\n"        \
    "    float ridge = textureLod(u_weather, bearing * (1.5 + 0.5 * k)\n"      \
    "                             + vec2(0.13, 0.27 * k), 0.0).g;\n"           \
    "    ridge = 1.0 - abs(ridge * 2.0 - 1.0);\n"                              \
    "    float shape = smoothstep(0.28, 0.9, n) * (0.7 + 0.3 * ridge);\n"      \
    "    return mix(-0.012, 0.052 - 0.012 * k, shape);\n"                      \
    "}\n"                                                                      \
    "vec3 vivid(vec3 c) {\n"                                                   \
    "    float grey = dot(c, vec3(0.2126, 0.7152, 0.0722));\n"                 \
    "    return max(mix(vec3(grey), c, u_look.x), vec3(0.0));\n"               \
    "}\n"                                                                      \
    "vec4 lands(vec3 d, float e, float px, vec3 haze, vec3 shade,\n"           \
    "           vec3 rim) {\n"                                                 \
    "    vec2 bearing = normalize(d.xz + vec2(1e-5));\n"                       \
    "    vec2 light_flat = normalize(u_light_way.xz + vec2(1e-5));\n"          \
    "    float off = acos(clamp(dot(bearing, light_flat), -1.0, 1.0));\n"      \
    "    float below = max(-e, 0.0);\n"                                        \
    "    vec3 dark = mix(shade, u_far, 0.3);\n"                                \
    "    vec3 c = mix(haze, dark, 0.35 * smoothstep(0.0, 0.05, below));\n"     \
    "    if (u_look.w > 0.5) {\n"                                              \
    "        vec3 m = normalize(vec3(d.x, below + 0.035, d.z));\n"             \
    "        float m_apart = acos(clamp(dot(m, u_light_way), -1.0, 1.0));\n"   \
    "        vec3 sky = screen_of(textureLod(u_air, air_at(m), 0.0).rgb\n"     \
    "                             + glow(m_apart));\n"                         \
    "        float fresnel = 0.15 + 0.55 * exp(-below * 30.0);\n"              \
    "        c = mix(mix(u_far, shade, 0.25), sky, fresnel);\n"                \
    "        float wide = 0.02 + 0.15 * below;\n"                              \
    "        c += rim * 0.8 * exp(-off * off / (wide * wide))\n"               \
    "           * exp(-below * 5.0) * smoothstep(0.0, 0.004, below);\n"        \
    "    }\n"                                                                  \
    "    float cover = 0.0;\n"                                                 \
    "    for (int k = 0; k < 3; k++) {\n"                                      \
    "        float top = range_top(bearing, float(k));\n"                      \
    "        if (u_look.w > 0.5) {\n"                                          \
    "            float land_here = textureLod(u_weather,\n"                    \
    "                bearing * 0.07 + vec2(0.71, 0.19 + 0.2 * float(k)),\n"    \
    "                0.0).r;\n"                                                \
    "            top = mix(-0.03, top, smoothstep(0.52, 0.72, land_here));\n"  \
    "        }\n"                                                              \
    "        float on = 1.0 - smoothstep(top - px, top + px, e);\n"            \
    "        float far = 0.66 - 0.25 * float(k);\n"                            \
    "        vec3 land = mix(dark, haze, far);\n"                              \
    "        land = mix(land, haze, 0.35 * smoothstep(0.0, 0.02, top - e));\n" \
    "        land += rim * (1.0 - smoothstep(0.0, 4.0 * px, top - e))\n"       \
    "              * pow(max(cos(off), 0.0), 8.0) * (1.0 - far) * 0.8;\n"      \
    "        c = mix(c, land, on);\n"                                          \
    "        cover = max(cover, on);\n"                                        \
    "    }\n"                                                                  \
    "    if (u_look.w < 0.5) {\n"                                              \
    "        vec2 plain = d.xz / max(below, 0.004);\n"                         \
    "        float field = textureLod(u_weather,\n"                            \
    "            plain * 0.012 + vec2(0.4, 0.8), 0.0).a;\n"                    \
    "        c *= 1.0 + (field - 0.5) * 0.4\n"                                 \
    "                  * smoothstep(0.002, 0.03, below);\n"                    \
    "    }\n"                                                                  \
    "    c = vivid(c);\n"                                                      \
    "    c = mix(c, u_far, smoothstep(0.02, 0.16, below));\n"                  \
    "    return vec4(c, 1.0 - cover);\n"                                       \
    "}\n"                                                                      \
    "vec4 land_at(vec3 d, float px) {\n"                                       \
    "    vec3 level = normalize(vec3(d.x, 0.0, d.z) + vec3(1e-5));\n"          \
    "    level = normalize(vec3(level.x, 0.002, level.z));\n"                  \
    "    float aside = acos(clamp(dot(level, u_light_way), -1.0, 1.0));\n"     \
    "    vec3 haze = screen_of(textureLod(u_air, air_at(level), 0.0).rgb\n"    \
    "                             + glow(aside));\n"                           \
    "    haze = mix(haze, u_far, u_horizon * 0.5);\n"                          \
    "    vec3 shade = screen_of(textureLod(u_air, vec2(0.5, 1.0), 0.0).rgb)\n" \
    "               * 0.5;\n"                                                  \
    "    vec3 rim = screen_of(u_light * 0.3 + u_glow * 1.5)\n"                 \
    "             * (1.0 - smoothstep(0.15, 0.4, u_light_way.y));\n"           \
    "    return lands(d, d.y, px, haze, shade, rim);\n"                        \
    "}\n"

/* The clouds' picture (g_sky_low_text): each pixel the air behind and the
 * clouds before it, in light (not yet a colour of the screen), made a
 * colour of the screen at its end; its alpha how much of what is behind
 * the clouds shows through them (the stars, the sun, the moon). */
static const char g_sky_low_text[] =
    "#version 330 core\n" SKY_VIEW SKY_LANDS
    "uniform vec2 u_origin;\n" /* the frame's corner, the buffer's px */
    "uniform float u_low;\n"   /* the buffer's pixels to one of this */
    "uniform sampler3D u_shape;\n"
    "uniform sampler3D u_detail;\n"
    "uniform float u_cover;\n"
    "uniform float u_clearing;\n" /* the clouds thinned about the light */
    "uniform float u_cirrus;\n"
    "uniform float u_dark;\n"
    "uniform vec2 u_drift;\n" /* how far the clouds have moved, metres */
    "uniform float u_milky;\n"
    "uniform float u_shade;\n"
    "uniform int u_steps;\n"
    "uniform vec3 u_under;\n" /* the low sun's light under the clouds */
    "uniform vec3 u_layer;\n" /* metres: the tallest clouds' tops, how far
                                 off the haze hides half of one; and how
                                 much the far ones thin, 0-1 */
    /* each pixel here is the clouds' picture's pixel `u_spread` times
     * its place and `u_offset` on: every pixel (1), or one corner of each
     * square of four (2) */
    "uniform float u_spread;\n"
    "uniform float u_footprint;\n"
    "uniform vec2 u_offset;\n"
    "layout(location = 0) out vec4 o_sky;\n"
    "const float GROUND = 6360e3;\n"
    "const float EYE = 120.0;\n"
    "const float BASE = 1100.0;\n" /* the heaped clouds' layer, metres */
    "const float VEIL = 8500.0;\n" /* the high cloud's */
    "const float THICK = 0.026;\n" /* light lost a metre in a cloud */
    "float shell(vec3 d, float height) {\n"
    "    float o = GROUND + EYE;\n"
    "    float b = o * d.y;\n"
    "    float c = o * o - (GROUND + height) * (GROUND + height);\n"
    "    return -b + sqrt(b * b - c);\n"
    "}\n"
    /* the cover along the ray being marched */
    "float g_cover;\n"
    "float remap(float v, float a, float b, float c, float d) {\n"
    "    return c + (v - a) / (b - a) * (d - c);\n"
    "}\n"
    /* how thick the cloud is at a place (`h` its height in the layer,
     * 0 to 1), with the detail that wears its edges or without */
    "float cloud(vec3 p, float h, bool fine, float wide) {\n"
    "    if (h < 0.0 || h > 1.0) { return 0.0; }\n"
    "    vec3 q = p + vec3(u_drift.x, 0.0, u_drift.y);\n"
    "    vec4 w = textureLod(u_weather, q.xz / 90000.0, 0.0);\n"
    "    float cover = clamp(g_cover + (w.r - 0.5) * 1.3, 0.0, 1.0);\n"
    "    float top = mix(0.3, 1.0, w.g);\n"
    "    float shape = smoothstep(0.0, 0.08, h)\n"
    "                * (1.0 - smoothstep(top * 0.45, top, h));\n"
    "    vec4 s = textureLod(u_shape, q / 11000.0,\n"
    "                        max(0.0, log2(wide / 86.0)));\n"
    "    float carve = s.g * 0.625 + s.b * 0.25 + s.a * 0.125;\n"
    "    float base = remap(s.r, carve - 1.0, 1.0, 0.0, 1.0);\n"
    "    base = remap(base * shape, 1.0 - cover, 1.0, 0.0, 1.0) * cover;\n"
    "    if (base <= 0.0 || !fine) { return max(base, 0.0); }\n"
    "    vec3 t = textureLod(u_detail, q / 1700.0 + vec3(0.0, h * 0.4, 0.0),\n"
    "                        max(0.0, log2(wide / 53.0))).rgb;\n"
    "    float wear = t.r * 0.625 + t.g * 0.25 + t.b * 0.125;\n"
    "    wear = mix(1.0 - wear, wear, clamp(h * 3.0, 0.0, 1.0));\n"
    "    return max(remap(base, wear * 0.55, 1.0, 0.0, 1.0), 0.0);\n"
    "}\n"
    "float height_of(vec3 p) {\n"
    "    return (length(p + vec3(0.0, GROUND, 0.0)) - GROUND - BASE)\n"
    "         / (u_layer.x - BASE);\n"
    "}\n"
    "void main() {\n"
    "    vec2 cell = floor(gl_FragCoord.xy) * u_spread + u_offset;\n"
    "    vec2 at = u_origin + (cell + vec2(0.5)) * u_low;\n"
    "    vec3 d = sky_dir(at);\n"
    "    float e = d.y;\n"
    /* below the horizon, the lands (SKY_LANDS); the frame draws them
     * again at its own resolution about the horizon, where the ranges'
     * ridges are, and over the sky above it */
    "    float px = max(fwidth(e), 1e-4);\n"
    "    if (e < 0.0) {\n"
    "        o_sky = vec4(land_at(d, px).rgb, 0.0);\n"
    "        return;\n"
    "    }\n"
    "    vec3 air = textureLod(u_air, air_at(d), 0.0).rgb;\n"
    /* the sky's light on the clouds: from overhead on their tops, from
     * the horizon on their undersides */
    "    vec3 above = textureLod(u_air, vec2(0.5, 1.0), 0.0).rgb;\n"
    "    vec3 around = (textureLod(u_air, vec2(0.0, 0.62), 0.0).rgb\n"
    "                 + textureLod(u_air, vec2(0.25, 0.62), 0.0).rgb\n"
    "                 + textureLod(u_air, vec2(0.5, 0.62), 0.0).rgb\n"
    "                 + textureLod(u_air, vec2(0.75, 0.62), 0.0).rgb) * 0.25;\n"
    "    float mu = dot(d, u_light_way);\n"
    "    float apart = acos(clamp(mu, -1.0, 1.0));\n"
    /* the glow about the sun or the moon, and the rays of a low sun
     * through the gaps of clouds out of sight: the glow streaked round
     * it, and beams of it fanning out */
    "    vec3 sky = air + glow(apart);\n"
    "    if (u_look.z > 0.0) {\n"
    "        vec3 side = normalize(cross(u_light_way, vec3(0.0, 1.0, 0.0)));\n"
    "        vec3 lift = cross(side, u_light_way);\n"
    "        vec2 ring = normalize(vec2(dot(d, side), dot(d, lift)) + vec2(1e-6));\n"
    "        float ray = textureLod(u_weather, ring * 0.33 + vec2(0.5, 0.25)\n"
    "                                          + u_drift * 2e-7, 0.0).g;\n"
    "        ray = smoothstep(0.38, 0.72, ray);\n"
    "        sky += glow(apart) * u_look.z * (ray - 0.4) * 0.8\n"
    "             + u_glow * u_look.z * ray * 1.2 * exp(-apart * 2.2);\n"
    "    }\n"
    /* the Milky Way, a band across the night, its stars' haze cut by its
     * lanes of dust */
    "    if (u_milky > 0.0 && e > 0.0) {\n"
    "        vec3 plane = normalize(vec3(0.42, 0.58, -0.70));\n"
    "        float off = dot(d, plane);\n"
    "        vec4 n = textureLod(u_shape, d * 0.9 + vec3(0.3), 0.0);\n"
    "        vec4 m = textureLod(u_detail, d * 2.5, 0.0);\n"
    "        float band = exp(-off * off / 0.028) * (0.45 + 0.9 * n.r);\n"
    "        float lane = exp(-(off - 0.02 * (n.g - 0.5)) * (off - 0.02 * (n.g - 0.5))\n"
    "                         / 0.0012) * smoothstep(0.35, 0.75, m.r + n.b * 0.4);\n"
    "        vec3 core = normalize(vec3(-0.8, 0.25, 0.55));\n"
    "        float toward = 0.5 + 0.5 * dot(d, core);\n"
    "        vec3 hue = mix(vec3(0.70, 0.76, 1.0), vec3(1.0, 0.86, 0.70),\n"
    "                       toward * toward);\n"
    "        sky += hue * band * (1.0 - 0.8 * lane) * u_milky\n"
    "             * (0.006 + 0.012 * toward * toward)\n"
    "             * smoothstep(0.0, 0.3, e);\n"
    "    }\n"
    /* the high veil: streaks of ice far above, lit through */
    "    if (u_cirrus > 0.0 && e > 0.0) {\n"
    "        float t = shell(d, VEIL);\n"
    "        vec2 q = (d.xz * t + u_drift * 1.6) / 60000.0;\n"
    /* where the veil lies, in patches, and the streaks bent by the
     * high wind's eddies */
    "        vec4 big = textureLod(u_weather, q * 0.45 + vec2(0.21, 0.63), 0.0);\n"
    "        vec2 r = mat2(0.8, 0.6, -0.6, 0.8)\n"
    "               * (q + (big.gr - vec2(0.5)) * 0.45);\n"
    "        float streak = textureLod(u_weather, r, 0.0).b;\n"
    "        vec4 v = textureLod(u_weather, r * 3.7 + vec2(0.37, 0.11), 0.0);\n"
    "        float veil = smoothstep(0.40, 0.95, streak * 0.75 + v.b * 0.4)\n"
    "                   * smoothstep(0.30, 0.65, big.r)\n"
    "                   * (0.45 + 0.55 * v.a) * u_cirrus\n"
    "                   * exp(-t / 260000.0);\n"
    "        vec3 lit = u_light * (0.10 + 2.6 * scatter(mu, 0.6)) * 0.7\n"
    "                 + above * 0.55 + around * 0.25 + u_under * 1.5;\n"
    "        sky = mix(sky, lit, clamp(veil, 0.0, 1.0) * 0.75);\n"
    "    }\n"
    /* the heaped clouds, marched through from their base up */
    "    float clear = 1.0;\n"
    "    vec3 lit = vec3(0.0);\n"
    "    float seen_at = 0.0;\n"
    "    float seen_much = 0.0;\n"
    "    if (u_cover > 0.0 && e > -0.01) {\n"
    "        float t0 = shell(d, BASE);\n"
    "        float t1 = min(shell(d, u_layer.x), t0 + 120000.0);\n"
    /* a step through a cloud, longer the farther it is (where a pixel
     * covers more of it); three of them at once through the clear air
     * between, and back two where one meets a cloud */
    "        float fine = clamp(60.0 + t0 * 0.012, 60.0, 1400.0);\n"
    /* a pixel's own start along its ray, so that the steps' rings are
     * the picture's grain and not its bands (Jimenez's noise) */
    "        float start = fract(52.9829189 * fract(dot(cell + vec2(0.5),\n"
    "                            vec2(0.06711056, 0.00583715))));\n"
    "        float phase = 0.55 * scatter(mu, 0.72) + 0.12 * scatter(mu, 0.93)\n"
    "                    + 0.33 * scatter(mu, -0.2);\n"
    /* a gap in the clouds about the sun or the moon low in the sky, which
     * the clouds about it catch the light at */
    "        float cover_here = u_cover * (1.0 - u_clearing * exp(-apart * apart / 0.012));\n"
    /* a low sun lights the clouds' undersides, those on its side most */
    "        float toward = 0.5 + 0.5 * dot(normalize(d.xz + vec2(1e-5)),\n"
    "                                       normalize(u_light_way.xz + vec2(1e-5)));\n"
    "        float under = 0.2 + 0.8 * toward * toward * toward;\n"
    "        float t = t0 + fine * start;\n"
    "        bool inside = false;\n"
    "        int quiet = 0;\n"
    "        for (int i = 0; i < u_steps; i++) {\n"
    "            if (t > t1 || clear < 0.03) { break; }\n"
    "            vec3 p = d * t + vec3(0.0, EYE, 0.0);\n"
    "            float h = height_of(p);\n"
    "            float wide = t * u_footprint;\n"
    "            fine = clamp(60.0 + t * 0.012, 60.0, 1400.0);\n"
    /* the far clouds thinned, by the look: a clear band of sky over the
     * horizon under the heaps */
    "            g_cover = cover_here * (1.0 - u_layer.z * smoothstep(25000.0, 90000.0, t));\n"
    "            if (!inside) {\n"
    "                if (cloud(p, h, false, wide * 2.0) > 0.0) {\n"
    "                    inside = true;\n"
    "                    quiet = 0;\n"
    "                    t = max(t - 2.0 * fine, t0);\n"
    "                } else {\n"
    "                    t += 3.0 * fine;\n"
    "                }\n"
    "                continue;\n"
    "            }\n"
    "            float thick = cloud(p, h, true, wide);\n"
    "            if (thick > 0.002) {\n"
    "                quiet = 0;\n"
    /* the light's way through the cloud towards the sun or the moon */
    "                float through = 0.0;\n"
    "                float reach = 80.0;\n"
    "                vec3 q = p;\n"
    "                for (int j = 0; j < 4; j++) {\n"
    "                    q += u_light_way * reach;\n"
    "                    through += cloud(q, height_of(q), j < 2,\n"
    "                                     max(wide, reach * 0.5)) * reach;\n"
    "                    reach *= 2.6;\n"
    "                }\n"
    "                float od = THICK * 0.6 * through;\n"
    /* what reaches the place: the light straight through, its scattering
     * in the cloud (made of a few weaker and wider lobes), and its powder
     * (a cloud's edge that faces the light is darker than its depth) */
    "                float sun = phase * exp(-od) * 4.0 * PI\n"
    "                          * (1.0 - 0.6 * exp(-thick * 8.0 - od * 2.0))\n"
    "                          + (0.45 * exp(-od * 0.25)\n"
    "                          + 0.10 * exp(-od * 0.06)) * u_look.y;\n"
    "                vec3 sky_light = mix(around * 0.3 + above * 0.3, above * 1.0,\n"
    "                                     clamp(h * 1.4, 0.0, 1.0));\n"
    "                vec3 here = u_light * sun * (1.0 - u_dark)\n"
    "                          + sky_light * u_shade * (1.0 - 0.5 * u_dark)\n"
    "                          + u_under * under * (1.0 - h) * (1.0 - h)\n"
    "                            * exp(-thick * 1.5);\n"
    "                float kept = exp(-THICK * thick * fine);\n"
    "                lit += clear * here * (1.0 - kept);\n"
    "                seen_at += t * clear * (1.0 - kept);\n"
    "                seen_much += clear * (1.0 - kept);\n"
    "                clear *= kept;\n"
    "            } else if (++quiet > 5) {\n"
    "                inside = false;\n"
    "            }\n"
    "            t += fine;\n"
    "        }\n"
    /* the clouds far off seen through the haze, which is the air's
     * colour where they are */
    "        if (seen_much > 0.0) {\n"
    "            float far = seen_at / seen_much;\n"
    "            float haze = 1.0 - exp(-far * 0.69 / u_layer.y);\n"
    "            lit = mix(lit, air * (1.0 - clear), haze);\n"
    "        }\n"
    "    }\n"
    "    vec3 c = sky * clear + lit;\n"
    /* the light made a colour of the screen */
    "    c = vivid(screen_of(c));\n"
    /* a haze at the horizon, the colour of the distance, which the
     * ground's fog runs into */
    "    float haze = u_horizon * exp(-e * 30.0);\n"
    "    c = mix(c, u_far, haze);\n"
    "    float shows = clear * (1.0 - haze) * smoothstep(0.0, 0.02, e);\n"
    "    o_sky = vec4(c, shows);\n"
    "}\n";

/* The clouds' picture put together (port_GlSkyCleared): the quarter
 * marched this picture where a pixel is of it, and the others the last
 * picture's clouds where the same direction was -- exact for a sky, which
 * is a function of the direction alone; the clouds drift too little in
 * the three pictures since a pixel was marched to tell -- or, where that
 * direction was not in the last picture, the nearest pixel marched. */
static const char g_sky_mix_text[] =
    "#version 330 core\n" SKY_VIEW "uniform vec2 u_origin;\n"
    "uniform float u_low;\n"
    "uniform sampler2D u_march;\n"
    "uniform vec2 u_offset;\n"
    "uniform sampler2D u_history;\n"
    "uniform mat3 u_prev_turn;\n"
    "uniform vec2 u_prev_middle;\n"
    "uniform float u_prev_h;\n"
    "uniform vec2 u_prev_origin;\n"
    "uniform vec2 u_prev_span;\n"
    "uniform vec2 u_prev_fit;\n" /* how much of the texture it filled */
    "uniform int u_has_history;\n"
    "uniform vec2 u_march_most;\n" /* the last pixel marched */
    "layout(location = 0) out vec4 o_sky;\n"
    "void main() {\n"
    "    ivec2 cell = ivec2(gl_FragCoord.xy);\n"
    "    ivec2 corner = cell - ivec2(u_offset);\n"
    "    ivec2 marched = corner >> 1;\n"
    "    if ((corner.x & 1) == 0 && (corner.y & 1) == 0) {\n"
    "        o_sky = texelFetch(u_march, marched, 0);\n"
    "        return;\n"
    "    }\n"
    "    vec3 d = sky_dir(u_origin + (vec2(cell) + vec2(0.5)) * u_low);\n"
    "    vec3 look = transpose(u_prev_turn) * vec3(d.x, -d.y, d.z);\n"
    "    if (u_has_history != 0 && look.z > 0.01) {\n"
    "        vec2 screen = vec2(look.x / look.z * u_prev_h,\n"
    "                           0.5 * look.y / look.z * u_prev_h)\n"
    "                    + u_prev_middle;\n"
    "        vec2 back = (screen * u_scale + vec2(0.5) - u_prev_origin)\n"
    "                  / u_prev_span;\n"
    "        if (back.x > 0.0 && back.y > 0.0 && back.x < 1.0\n"
    "            && back.y < 1.0) {\n"
    "            o_sky = textureLod(u_history, back * u_prev_fit, 0.0);\n"
    "            return;\n"
    "        }\n"
    "    }\n"
    "    o_sky = texelFetch(u_march, clamp(marched, ivec2(0), ivec2(u_march_most)),\n"
    "                       0);\n"
    "}\n";

/* The frame (port_GlSkyCleared): the clouds' picture read smoothly, and
 * over it at the frame's own resolution what must be sharp, as much as
 * the clouds leave clear; or a mod's picture of the sky. */
static const char g_sky_fragment_text[] =
    "#version 330 core\n" SKY_VIEW SKY_LANDS "uniform int u_has_picture;\n"
    "uniform sampler2D u_picture;\n"
    "uniform sampler2D u_low_sky;\n"
    "uniform vec2 u_origin;\n"
    "uniform vec2 u_span;\n" /* the clouds' picture's size, the buffer's px */
    "uniform vec2 u_fit;\n"  /* how much of its texture it fills */
    "uniform vec2 u_fit_most;\n" /* the middle of its last texel */
    "uniform sampler3D u_shape;\n"
    "uniform sampler3D u_detail;\n"
    "uniform float u_stars;\n"
    "uniform vec3 u_sun;\n" /* the sun's way, y up, and its colour */
    "uniform vec3 u_sun_light;\n"
    "uniform int u_has_sun;\n"
    "uniform vec3 u_moon;\n"
    "uniform vec3 u_moon_light;\n"
    "uniform int u_has_moon;\n"
    "uniform float u_moon_size;\n"
    "uniform vec3 u_glint;\n" /* the light's glints on it */
    "uniform float u_time;\n" /* seconds of the picture's clock */
    "layout(location = 0) out vec4 o_color;\n"
    "float hash(vec3 p) {\n"
    "    p = fract(p * 0.3183099 + vec3(0.71, 0.113, 0.419));\n"
    "    p *= 17.0;\n"
    "    return fract(p.x * p.y * p.z * (p.x + p.y + p.z));\n"
    "}\n"
    /* The stars of one layer: the direction onto the face of a cube about
     * the eye, the face cut into `cells` by `cells` cells, and a star in
     * a cell where the cell's number says so, as wide as a pixel. */
    "vec3 stars(vec3 dir, float cells, float share, float pixel) {\n"
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
    "    if (pick > share) { return vec3(0.0); }\n"
    "    vec2 at = cell + 0.25 + 0.5 * vec2(hash(vec3(cell, face + 7.0)),\n"
    "                                       hash(vec3(cell, face + 13.0)));\n"
    /* the distance on the face, in the angle it is from the eye */
    "    float apart = length(g - at) * 2.0 / cells;\n"
    "    float bright = 0.2 + 0.8 * pow(hash(vec3(cell, face + 29.0)), 4.0);\n"
    "    float warm = hash(vec3(cell, face + 41.0));\n"
    "    vec3 hue = warm < 0.25 ? vec3(1.0, 0.82, 0.62)\n"
    "             : warm > 0.8 ? vec3(0.70, 0.80, 1.0) : vec3(0.92, 0.94, 1.0);\n"
    "    float wide = pixel * (0.8 + 0.9 * bright);\n"
    "    return hue * bright * (1.0 - smoothstep(0.3 * wide, 1.1 * wide, apart));\n"
    "}\n"
    "void main() {\n"
    "    vec3 d = sky_dir(gl_FragCoord.xy);\n"
    "    if (u_has_picture != 0) {\n"
    "        vec2 at = vec2(atan(d.x, d.z) / (2.0 * PI) + 0.5,\n"
    "                       acos(clamp(d.y, -1.0, 1.0)) / PI);\n"
    "        o_color = vec4(texture(u_picture, at).rgb, 1.0);\n"
    "        return;\n"
    "    }\n"
    "    vec2 low_at = min((gl_FragCoord.xy - u_origin) / u_span * u_fit,\n"
    "                      u_fit_most);\n"
    "    vec4 low = texture(u_low_sky, low_at);\n"
    "    vec3 color = low.rgb;\n"
    "    float clear = clamp(low.a, 0.0, 1.0);\n"
    /* how wide a pixel of the buffer is, as an angle */
    "    float pixel = 1.0 / (u_h * u_scale.x);\n"
    "    float high = clamp(d.y, 0.0, 1.0);\n"
    /* the stars, three layers, the faint ones thicker in the Milky Way,
     * fewer near the horizon, none behind a cloud */
    "    if (u_stars > 0.0 && clear > 0.0) {\n"
    "        vec3 plane = normalize(vec3(0.42, 0.58, -0.70));\n"
    "        float off = dot(d, plane);\n"
    "        float band = exp(-off * off / 0.03);\n"
    "        vec3 star = stars(d, 90.0, 0.06, pixel) * 1.4\n"
    "                  + stars(d, 220.0, 0.07, pixel) * 0.8\n"
    "                  + stars(d, 420.0, 0.05 + 0.25 * band, pixel) * 0.5;\n"
    "        color += star * u_stars * clear * smoothstep(0.01, 0.16, high)\n"
    "               * smoothstep(0.0, 1.0, 1.0 - max(max(color.r, color.g),\n"
    "                                                color.b) * 0.8);\n"
    "    }\n"
    /* the sun's disc, darker at its limb */
    "    if (u_has_sun != 0) {\n"
    "        float apart = acos(clamp(dot(d, u_sun), -1.0, 1.0));\n"
    "        float radius = 0.021;\n"
    "        float disc = 1.0 - smoothstep(radius - pixel, radius + pixel, apart);\n"
    /* its bloom: the air's brightest light about it, low in the sky */
    "        float out_of_disc = max(apart - radius, 0.0);\n"
    "        float low = 1.0 - smoothstep(0.1, 0.35, u_sun.y);\n"
    "        color += u_sun_light * clear * low\n"
    "               * (0.55 * exp(-out_of_disc * 60.0) + 0.18 * exp(-out_of_disc * 14.0));\n"
    "        if (disc > 0.0) {\n"
    "            float r = clamp(apart / radius, 0.0, 1.0);\n"
    "            float limb = 1.0 - 0.18 * (1.0 - sqrt(1.0 - r * r));\n"
    "            vec3 face = min(mix(u_sun_light, vec3(1.0, 0.97, 0.86), 0.5)\n"
    "                            * 1.6 * limb, vec3(1.0));\n"
    "            color = mix(color, max(color, face), disc * clear);\n"
    "        }\n"
    "    }\n"
    /* the moon: its seas and craters on a sphere fixed to it, lit a
     * little from one side so that its craters stand out, the dark side's
     * faint earthshine; behind the clouds as much as they cover it */
    "    if (u_has_moon != 0) {\n"
    "        float radius = u_moon_size;\n"
    "        float apart = acos(clamp(dot(d, u_moon), -1.0, 1.0));\n"
    /* its corona: the light of a bright moon in the air close about it */
    "        float out_of_disc = max(apart - radius, 0.0) / radius;\n"
    "        color += u_moon_light * clear * (0.16 * exp(-out_of_disc * 3.0)\n"
    "                                      + 0.05 * exp(-out_of_disc * 0.7))\n"
    "               * step(radius, apart);\n"
    "        if (apart < radius + 2.0 * pixel) {\n"
    "            vec3 side = normalize(cross(u_moon, vec3(0.0, 1.0, 0.0)));\n"
    "            vec3 lift = cross(side, u_moon);\n"
    "            vec2 on = vec2(dot(d, side), dot(d, lift)) / sin(radius);\n"
    "            float rr = dot(on, on);\n"
    "            float edge = 1.0 - smoothstep(1.0 - 2.0 * pixel / radius,\n"
    "                                          1.0, sqrt(rr));\n"
    "            vec3 n = vec3(on, sqrt(max(1.0 - rr, 0.0)));\n"
    "            vec3 at = n * 0.55 + vec3(0.5);\n"
    "            vec4 big = textureLod(u_shape, at * 0.55, 0.0);\n"
    "            vec4 pits = textureLod(u_detail, at * 1.3 + vec3(0.2), 0.0);\n"
    "            vec4 small = textureLod(u_detail, at * 3.7 + vec3(0.6), 0.0);\n"
    /* the seas: wide and dark, smooth */
    "            float sea = smoothstep(0.52, 0.68, big.r * 0.7 + big.g * 0.4);\n"
    /* the craters: a cell's point is a crater's middle, its floor
     * shadowed on the side towards the light, its rim bright */
    "            float crater = 0.0;\n"
    "            float rim = 0.0;\n"
    "            float c1 = pits.g;\n"
    "            float c2 = small.r;\n"
    "            crater += smoothstep(0.78, 0.9, c1) * 0.5\n"
    "                    + smoothstep(0.82, 0.92, c2) * 0.35;\n"
    "            rim += smoothstep(0.66, 0.74, c1) * (1.0 - smoothstep(0.74, 0.8, c1))\n"
    "                 + smoothstep(0.74, 0.8, c2) * (1.0 - smoothstep(0.8, 0.84, c2))\n"
    "                 * 0.7;\n"
    "            float albedo = mix(0.86, 0.50, sea) * (1.0 - 0.25 * crater)\n"
    "                         + 0.12 * rim * (1.0 - sea * 0.5)\n"
    "                         + 0.08 * (pits.b - 0.5);\n"
    "            vec3 light_way = normalize(vec3(-0.42, 0.22, 0.88));\n"
    "            float lit = dot(n, light_way);\n"
    "            float shade = smoothstep(-0.08, 0.22, lit) * (0.72 + 0.28 * lit);\n"
    "            float limb = 0.82 + 0.18 * n.z;\n"
    "            vec3 face = u_moon_light * albedo * limb\n"
    "                      * (shade * 1.3 + 0.04);\n"
    "            face *= mix(vec3(1.0), vec3(0.97, 0.98, 1.03), sea);\n"
    "            color = mix(color, min(face, vec3(1.0)),\n"
    "                        edge * clamp(clear * 1.15, 0.0, 1.0));\n"
    "        }\n"
    "    }\n"
    /* what lies before the sky, drawn over it about the horizon at the
     * frame's own resolution, so that the ranges' ridges are sharp: the
     * lands and the sea beyond the stage (SKY_LANDS); lower down, where
     * nothing is sharp, the clouds' picture has them */
    "    float px = max(fwidth(d.y), 1e-5);\n"
    "    if (d.y > (u_look.w > 0.5 ? -0.032 : -0.014) && d.y < 0.054) {\n"
    "        vec4 land = land_at(d, px);\n"
    "        color = mix(land.rgb, color, d.y < 0.0 ? 0.0 : land.a);\n"
    "    }\n"
    /* the sea beyond the stage: glints of the sun or the moon on its
     * waves, along the light's path, twinkling */
    "    if (u_look.w > 0.5 && d.y < 0.0) {\n"
    "        float below = -d.y;\n"
    "        vec2 bearing = normalize(d.xz + vec2(1e-5));\n"
    "        vec2 light_flat = normalize(u_sun.xz + vec2(1e-5));\n"
    "        float off = acos(clamp(dot(bearing, light_flat), -1.0, 1.0));\n"
    "        float wide = 0.012 + 0.1 * below;\n"
    "        float path = exp(-off * off / (wide * wide)) * exp(-below * 5.0)\n"
    "                   * smoothstep(0.0, 0.003, below)\n"
    "                   * (1.0 - smoothstep(0.02, 0.12, below));\n"
    "        vec2 cell = floor(gl_FragCoord.xy / vec2(2.0, 1.0));\n"
    "        float glint = hash(vec3(cell, floor(u_time * 7.0)));\n"
    "        color += u_glint * step(1.0 - 0.2 * path, glint) * path * 2.0;\n"
    "    }\n"
    "    o_color = vec4(clamp(color, 0.0, 1.0), 1.0);\n"
    "}\n";

/* ---- the state ---- */

/* The uniforms of the view, in each of the programs that has them. */
typedef struct {
    GLint turn;
    GLint middle;
    GLint h;
    GLint scale;
} SkyViewAt;

/* What the air's picture was worked out for: it is made again when any
 * of it changes. */
typedef struct {
    s32 night;
    GLfloat sun[3];
    GLfloat sun_air[3];
    GLfloat haze;
    GLfloat power;
    GLfloat low[3];
    GLfloat high[3];
    GLfloat moon_light[3];
    GLfloat belt[3];
    GLfloat paint[4];
} SkyAirKey;

static struct {
    s32 is_started;
    s32 kind;
    GLuint array;
    GLuint work; /* a framebuffer for the textures this file draws into */
    /* the frame's program */
    GLuint program;
    SkyViewAt view;
    GLint at_has_picture;
    GLint at_picture;
    GLint at_low_sky;
    GLint at_origin;
    GLint at_span;
    GLint at_fit;
    GLint at_fit_most;
    GLint at_shape;
    GLint at_detail;
    GLint at_stars;
    GLint at_sun;
    GLint at_sun_light;
    GLint at_has_sun;
    GLint at_moon;
    GLint at_moon_light;
    GLint at_has_moon;
    GLint at_moon_size;
    GLint at_air;
    GLint at_weather;
    GLint at_light_way;
    GLint at_light;
    GLint at_glow;
    GLint at_far;
    GLint at_horizon;
    GLint at_look;
    GLint at_glint;
    GLint at_time;
    /* the clouds' program and its picture */
    GLuint low_program;
    SkyViewAt low_view;
    GLint low_at_origin;
    GLint low_at_low;
    GLint low_at_air;
    GLint low_at_shape;
    GLint low_at_detail;
    GLint low_at_weather;
    GLint low_at_light_way;
    GLint low_at_light;
    GLint low_at_glow;
    GLint low_at_cover;
    GLint low_at_clearing;
    GLint low_at_cirrus;
    GLint low_at_dark;
    GLint low_at_drift;
    GLint low_at_far;
    GLint low_at_horizon;
    GLint low_at_milky;
    GLint low_at_shade;
    GLint low_at_steps;
    GLint low_at_spread;
    GLint low_at_footprint;
    GLint low_at_offset;
    GLint low_at_look;
    GLint low_at_under;
    GLint low_at_layer;
    /* the program that puts the clouds' picture together */
    GLuint mix_program;
    SkyViewAt mix_view;
    GLint mix_at_origin;
    GLint mix_at_low;
    GLint mix_at_march;
    GLint mix_at_offset;
    GLint mix_at_history;
    GLint mix_at_prev_turn;
    GLint mix_at_prev_middle;
    GLint mix_at_prev_h;
    GLint mix_at_prev_origin;
    GLint mix_at_prev_span;
    GLint mix_at_prev_fit;
    GLint mix_at_has_history;
    GLint mix_at_march_most;
    /* the quarter of it marched this picture */
    GLuint march;
    /* the clouds' pictures, this one's and the last one's, by turns */
    GLuint low[2];
    s32 low_now;
    /* what the last picture of the clouds was drawn with: its view, its
     * look, the stage's frame it was of, and which quarter it marched */
    s32 has_history;
    s32 history_frames;
    s32 phase;
    GLfloat prev_turn[MATRIX_ELEMENTS];
    GLfloat prev_middle[2];
    GLfloat prev_h;
    GLfloat prev_origin[2];
    GLfloat prev_span[2];
    GLfloat prev_fit[2];
    GLfloat prev_scale[2];
    /* the air's program and its picture */
    GLuint air_program;
    GLint air_at_night;
    GLint air_at_sun;
    GLint air_at_sun_air;
    GLint air_at_haze;
    GLint air_at_power;
    GLint air_at_low;
    GLint air_at_high;
    GLint air_at_moon_light;
    GLint air_at_belt;
    GLint air_at_paint;
    GLuint air;
    s32 has_air;
    SkyAirKey air_key;
    /* the noise's program and its textures */
    GLuint noise_program;
    GLint noise_at_what;
    GLint noise_at_side;
    GLint noise_at_layer;
    GLuint shape;
    GLuint detail;
    GLuint weather;
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

/* ---- arithmetic (the window's build has no mathematics library) ---- */

static float port_SkyRoot(float value) {
    float root;
    float last = 0.0f;
    s32 i;

    if (value <= 0.0f) {
        return 0.0f;
    }
    root = value > 1.0f ? value : 1.0f;
    for (i = 0; i < 64 && root != last; i++) {
        last = root;
        root = 0.5f * (root + value / root);
    }
    return root;
}

/* the sine of `a` radians, by its series after bringing it to -pi..pi */
static float port_SkySin(float a) {
    float term;
    float sum;
    s32 i;

    while (a > PI_F) {
        a -= 2.0f * PI_F;
    }
    while (a < -PI_F) {
        a += 2.0f * PI_F;
    }
    term = a;
    sum = a;
    for (i = 1; i < 10; i++) {
        term *= -a * a / (float)((2 * i) * (2 * i + 1));
        sum += term;
    }
    return sum;
}

/* `v` made of length one, or (0, 1, 0) where it has none */
static void port_SkyUnit(GLfloat *v) {
    float length = port_SkyRoot(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);

    if (length < 1e-6f) {
        v[0] = 0.0f;
        v[1] = 1.0f;
        v[2] = 0.0f;
        return;
    }
    v[0] /= length;
    v[1] /= length;
    v[2] /= length;
}

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

static void port_GlSkyViewAt(GLuint program, SkyViewAt *at) {
    at->turn = glGetUniformLocation(program, "u_turn");
    at->middle = glGetUniformLocation(program, "u_middle");
    at->h = glGetUniformLocation(program, "u_h");
    at->scale = glGetUniformLocation(program, "u_scale");
}

/* The functions, the shaders and their uniforms (gl_sky.h). */
const char *port_GlSkyStart(void *(*getproc)(const char *name)) {
    GLuint vertex;
    GLuint fragment;
    GLuint low;
    GLuint mix;
    GLuint air;
    GLuint noise;
    GLuint p;

#define GL(result, name, args)                                                 \
    name = (result(*) args)getproc(#name);                                     \
    if (name == NULL) {                                                        \
        return "OpenGL has no " #name;                                         \
    }
    PORT_GL_SKY_FUNCTIONS
#undef GL
    vertex = port_GlSkyShader(GL_VERTEX_SHADER, g_sky_vertex_text);
    fragment = port_GlSkyShader(GL_FRAGMENT_SHADER, g_sky_fragment_text);
    low = port_GlSkyShader(GL_FRAGMENT_SHADER, g_sky_low_text);
    mix = port_GlSkyShader(GL_FRAGMENT_SHADER, g_sky_mix_text);
    air = port_GlSkyShader(GL_FRAGMENT_SHADER, g_sky_air_text);
    noise = port_GlSkyShader(GL_FRAGMENT_SHADER, g_sky_noise_text);
    if (vertex == 0 || fragment == 0 || low == 0 || mix == 0 || air == 0 ||
        noise == 0) {
        return g_sky.news;
    }
    g_sky.program = port_GlSkyLink(vertex, fragment);
    g_sky.low_program = port_GlSkyLink(vertex, low);
    g_sky.mix_program = port_GlSkyLink(vertex, mix);
    g_sky.air_program = port_GlSkyLink(vertex, air);
    g_sky.noise_program = port_GlSkyLink(vertex, noise);
    if (g_sky.program == 0 || g_sky.low_program == 0 ||
        g_sky.mix_program == 0 || g_sky.air_program == 0 ||
        g_sky.noise_program == 0) {
        return "the sky's shaders do not link";
    }
    p = g_sky.program;
    port_GlSkyViewAt(p, &g_sky.view);
    g_sky.at_has_picture = glGetUniformLocation(p, "u_has_picture");
    g_sky.at_picture = glGetUniformLocation(p, "u_picture");
    g_sky.at_low_sky = glGetUniformLocation(p, "u_low_sky");
    g_sky.at_origin = glGetUniformLocation(p, "u_origin");
    g_sky.at_span = glGetUniformLocation(p, "u_span");
    g_sky.at_fit = glGetUniformLocation(p, "u_fit");
    g_sky.at_fit_most = glGetUniformLocation(p, "u_fit_most");
    g_sky.at_shape = glGetUniformLocation(p, "u_shape");
    g_sky.at_detail = glGetUniformLocation(p, "u_detail");
    g_sky.at_stars = glGetUniformLocation(p, "u_stars");
    g_sky.at_sun = glGetUniformLocation(p, "u_sun");
    g_sky.at_sun_light = glGetUniformLocation(p, "u_sun_light");
    g_sky.at_has_sun = glGetUniformLocation(p, "u_has_sun");
    g_sky.at_moon = glGetUniformLocation(p, "u_moon");
    g_sky.at_moon_light = glGetUniformLocation(p, "u_moon_light");
    g_sky.at_has_moon = glGetUniformLocation(p, "u_has_moon");
    g_sky.at_moon_size = glGetUniformLocation(p, "u_moon_size");
    g_sky.at_air = glGetUniformLocation(p, "u_air");
    g_sky.at_weather = glGetUniformLocation(p, "u_weather");
    g_sky.at_light_way = glGetUniformLocation(p, "u_light_way");
    g_sky.at_light = glGetUniformLocation(p, "u_light");
    g_sky.at_glow = glGetUniformLocation(p, "u_glow");
    g_sky.at_far = glGetUniformLocation(p, "u_far");
    g_sky.at_horizon = glGetUniformLocation(p, "u_horizon");
    g_sky.at_look = glGetUniformLocation(p, "u_look");
    g_sky.at_glint = glGetUniformLocation(p, "u_glint");
    g_sky.at_time = glGetUniformLocation(p, "u_time");
    p = g_sky.low_program;
    port_GlSkyViewAt(p, &g_sky.low_view);
    g_sky.low_at_origin = glGetUniformLocation(p, "u_origin");
    g_sky.low_at_low = glGetUniformLocation(p, "u_low");
    g_sky.low_at_air = glGetUniformLocation(p, "u_air");
    g_sky.low_at_shape = glGetUniformLocation(p, "u_shape");
    g_sky.low_at_detail = glGetUniformLocation(p, "u_detail");
    g_sky.low_at_weather = glGetUniformLocation(p, "u_weather");
    g_sky.low_at_light_way = glGetUniformLocation(p, "u_light_way");
    g_sky.low_at_light = glGetUniformLocation(p, "u_light");
    g_sky.low_at_glow = glGetUniformLocation(p, "u_glow");
    g_sky.low_at_cover = glGetUniformLocation(p, "u_cover");
    g_sky.low_at_clearing = glGetUniformLocation(p, "u_clearing");
    g_sky.low_at_cirrus = glGetUniformLocation(p, "u_cirrus");
    g_sky.low_at_dark = glGetUniformLocation(p, "u_dark");
    g_sky.low_at_drift = glGetUniformLocation(p, "u_drift");
    g_sky.low_at_far = glGetUniformLocation(p, "u_far");
    g_sky.low_at_horizon = glGetUniformLocation(p, "u_horizon");
    g_sky.low_at_milky = glGetUniformLocation(p, "u_milky");
    g_sky.low_at_shade = glGetUniformLocation(p, "u_shade");
    g_sky.low_at_steps = glGetUniformLocation(p, "u_steps");
    g_sky.low_at_spread = glGetUniformLocation(p, "u_spread");
    g_sky.low_at_footprint = glGetUniformLocation(p, "u_footprint");
    g_sky.low_at_offset = glGetUniformLocation(p, "u_offset");
    g_sky.low_at_look = glGetUniformLocation(p, "u_look");
    g_sky.low_at_under = glGetUniformLocation(p, "u_under");
    g_sky.low_at_layer = glGetUniformLocation(p, "u_layer");
    p = g_sky.mix_program;
    port_GlSkyViewAt(p, &g_sky.mix_view);
    g_sky.mix_at_origin = glGetUniformLocation(p, "u_origin");
    g_sky.mix_at_low = glGetUniformLocation(p, "u_low");
    g_sky.mix_at_march = glGetUniformLocation(p, "u_march");
    g_sky.mix_at_offset = glGetUniformLocation(p, "u_offset");
    g_sky.mix_at_history = glGetUniformLocation(p, "u_history");
    g_sky.mix_at_prev_turn = glGetUniformLocation(p, "u_prev_turn");
    g_sky.mix_at_prev_middle = glGetUniformLocation(p, "u_prev_middle");
    g_sky.mix_at_prev_h = glGetUniformLocation(p, "u_prev_h");
    g_sky.mix_at_prev_origin = glGetUniformLocation(p, "u_prev_origin");
    g_sky.mix_at_prev_span = glGetUniformLocation(p, "u_prev_span");
    g_sky.mix_at_prev_fit = glGetUniformLocation(p, "u_prev_fit");
    g_sky.mix_at_has_history = glGetUniformLocation(p, "u_has_history");
    g_sky.mix_at_march_most = glGetUniformLocation(p, "u_march_most");
    p = g_sky.air_program;
    g_sky.air_at_night = glGetUniformLocation(p, "u_night");
    g_sky.air_at_sun = glGetUniformLocation(p, "u_sun");
    g_sky.air_at_sun_air = glGetUniformLocation(p, "u_sun_air");
    g_sky.air_at_haze = glGetUniformLocation(p, "u_haze");
    g_sky.air_at_power = glGetUniformLocation(p, "u_power");
    g_sky.air_at_low = glGetUniformLocation(p, "u_low");
    g_sky.air_at_high = glGetUniformLocation(p, "u_high");
    g_sky.air_at_moon_light = glGetUniformLocation(p, "u_moon_light");
    g_sky.air_at_belt = glGetUniformLocation(p, "u_belt");
    g_sky.air_at_paint = glGetUniformLocation(p, "u_paint");
    p = g_sky.noise_program;
    g_sky.noise_at_what = glGetUniformLocation(p, "u_what");
    g_sky.noise_at_side = glGetUniformLocation(p, "u_side");
    g_sky.noise_at_layer = glGetUniformLocation(p, "u_layer");
    /* the triangle is made of its vertices' numbers alone, but a core
     * context draws nothing with no vertex array bound */
    glGenVertexArrays(1, &g_sky.array);
    glGenFramebuffers(1, &g_sky.work);
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

/* ---- the textures ---- */

/* A texture of `kind` (2D or 3D) the sky draws into, read smoothly; one
 * that repeats where `wrap`, else clamped at its edges. */
static GLuint port_GlSkyTexture(GLenum kind, s32 w, s32 h, s32 depth,
                                GLint format, GLenum type, GLint wrap) {
    GLuint texture = 0;

    glGenTextures(1, &texture);
    glBindTexture(kind, texture);
    if (kind == GL_TEXTURE_3D) {
        glTexImage3D(kind, 0, format, w, h, depth, 0, GL_RGBA, type, NULL);
        glTexParameteri(kind, GL_TEXTURE_WRAP_R, wrap);
    } else {
        glTexImage2D(kind, 0, format, w, h, 0, GL_RGBA, type, NULL);
    }
    glTexParameteri(kind, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(kind, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(kind, GL_TEXTURE_WRAP_S, wrap);
    glTexParameteri(kind, GL_TEXTURE_WRAP_T, wrap);
    return texture;
}

/* The noise's program drawn into a texture of `side` texels a side:
 * each layer of a cube, or the square. */
static void port_GlSkyDrawNoise(
    GLuint texture, s32 what, s32 side, s32 layers) {
    s32 layer;

    glUniform1i(g_sky.noise_at_what, what);
    glUniform1f(g_sky.noise_at_side, (GLfloat)side);
    glViewport(0, 0, side, side);
    for (layer = 0; layer < layers; layer++) {
        if (layers > 1) {
            glFramebufferTextureLayer(
                GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, texture, 0, layer);
        } else {
            glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                                   GL_TEXTURE_2D, texture, 0);
        }
        glUniform1f(g_sky.noise_at_layer, (GLfloat)layer);
        glDrawArrays(GL_TRIANGLES, 0, 3);
    }
}

/* The clouds' noise, made the first time a sky is painted (or while the
 * game loads, port_GlSkyWarm): the same for every stage and time, a
 * function of the texel alone. */
static void port_GlSkyMakeNoise(void) {
    if (g_sky.shape != 0) {
        return;
    }
    g_sky.shape =
        port_GlSkyTexture(GL_TEXTURE_3D, SHAPE_SIDE, SHAPE_SIDE, SHAPE_SIDE,
                          GL_RGBA8, GL_UNSIGNED_BYTE, GL_REPEAT);
    g_sky.detail =
        port_GlSkyTexture(GL_TEXTURE_3D, DETAIL_SIDE, DETAIL_SIDE, DETAIL_SIDE,
                          GL_RGBA8, GL_UNSIGNED_BYTE, GL_REPEAT);
    g_sky.weather = port_GlSkyTexture(GL_TEXTURE_2D, WEATHER_SIDE, WEATHER_SIDE,
                                      1, GL_RGBA8, GL_UNSIGNED_BYTE, GL_REPEAT);
    glBindFramebuffer(GL_FRAMEBUFFER, g_sky.work);
    glUseProgram(g_sky.noise_program);
    glBindVertexArray(g_sky.array);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_BLEND);
    glDisable(GL_SCISSOR_TEST);
    port_GlSkyDrawNoise(g_sky.shape, 0, SHAPE_SIDE, SHAPE_SIDE);
    port_GlSkyDrawNoise(g_sky.detail, 1, DETAIL_SIDE, DETAIL_SIDE);
    port_GlSkyDrawNoise(g_sky.weather, 2, WEATHER_SIDE, 1);
    /* the shapes and the detail smaller and smaller, for the clouds far
     * off, where a pixel covers several of their texels */
    glBindTexture(GL_TEXTURE_3D, g_sky.shape);
    glGenerateMipmap(GL_TEXTURE_3D);
    glTexParameteri(
        GL_TEXTURE_3D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glBindTexture(GL_TEXTURE_3D, g_sky.detail);
    glGenerateMipmap(GL_TEXTURE_3D);
    glTexParameteri(
        GL_TEXTURE_3D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
}

/* The clouds' two pictures, this one's and the last one's, and the
 * quarter marched: made once, while the game loads, at the most a frame
 * uses of them. */
static void port_GlSkyMakeLow(void) {
    s32 i;

    if (g_sky.low[0] != 0) {
        return;
    }
    for (i = 0; i < 2; i++) {
        g_sky.low[i] =
            port_GlSkyTexture(GL_TEXTURE_2D, LOW_W_MOST, LOW_H_MOST, 1,
                              GL_RGBA16F, GL_HALF_FLOAT, GL_CLAMP_TO_EDGE);
    }
    g_sky.march = port_GlSkyTexture(
        GL_TEXTURE_2D, (LOW_W_MOST + 1) / 2, (LOW_H_MOST + 1) / 2, 1,
        GL_RGBA16F, GL_HALF_FLOAT, GL_CLAMP_TO_EDGE);
    g_sky.has_history = FALSE;
}

/* The air's picture, made again where what it is worked out from has
 * changed since (a stage, a time, the stage's light). */
static void port_GlSkyAir(const SkyAirKey *key) {
    if (g_sky.air == 0) {
        GLuint texture =
            port_GlSkyTexture(GL_TEXTURE_2D, AIR_W, AIR_H, 1, GL_RGBA16F,
                              GL_HALF_FLOAT, GL_CLAMP_TO_EDGE);

        /* round the eye from left to right */
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
        g_sky.air = texture;
    }
    if (g_sky.has_air && memcmp(key, &g_sky.air_key, sizeof(*key)) == 0) {
        return;
    }
    g_sky.air_key = *key;
    g_sky.has_air = TRUE;
    g_sky.has_history = FALSE;
    glBindFramebuffer(GL_FRAMEBUFFER, g_sky.work);
    glFramebufferTexture2D(
        GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, g_sky.air, 0);
    glUseProgram(g_sky.air_program);
    glBindVertexArray(g_sky.array);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_BLEND);
    glDisable(GL_SCISSOR_TEST);
    glViewport(0, 0, AIR_W, AIR_H);
    glUniform1i(g_sky.air_at_night, key->night);
    glUniform3fv(g_sky.air_at_sun, 1, key->sun);
    glUniform3fv(g_sky.air_at_sun_air, 1, key->sun_air);
    glUniform1f(g_sky.air_at_haze, key->haze);
    glUniform1f(g_sky.air_at_power, key->power);
    glUniform3fv(g_sky.air_at_low, 1, key->low);
    glUniform3fv(g_sky.air_at_high, 1, key->high);
    glUniform3fv(g_sky.air_at_moon_light, 1, key->moon_light);
    glUniform3fv(g_sky.air_at_belt, 1, key->belt);
    glUniform4fv(g_sky.air_at_paint, 1, key->paint);
    glDrawArrays(GL_TRIANGLES, 0, 3);
}

/* Texture `texture` (of `kind`) on unit `unit`, which the sampler at
 * `at` of the program in use reads. */
static void port_GlSkyBind(GLenum kind, s32 unit, GLuint texture, GLint at) {
    glActiveTexture(GL_TEXTURE0 + unit);
    glBindTexture(kind, texture);
    glUniform1i(at, unit);
}

void port_GlSkyWarm(void) {
    PortGlPicture picture;
    SkyAirKey key;

    if (!g_sky.is_started || g_sky.shape != 0 ||
        (g_sky.kind == PORT_SKY_ORIGINAL && !port_GlTime()->on)) {
        return;
    }
    port_GlGetPicture(&picture);
    if (picture.framebuffer == 0) {
        return;
    }
    port_GlSkyMakeNoise();
    /* the air's program and the clouds' drawn once each, so that the
     * driver makes them ready now and not at the fight's first frame */
    memset(&key, 0, sizeof(key));
    key.sun[1] = 1.0f;
    key.sun_air[1] = 1.0f;
    key.haze = 1.0f;
    port_GlSkyAir(&key);
    g_sky.has_air = FALSE;
    port_GlSkyMakeLow();
    /* each with the textures it reads bound as a frame binds them: the
     * driver makes a program ready for the kinds of texture it is drawn
     * with */
    glFramebufferTexture2D(
        GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, g_sky.march, 0);
    glUseProgram(g_sky.low_program);
    glUniform1i(g_sky.low_at_steps, 1);
    port_GlSkyBind(GL_TEXTURE_2D, 0, g_sky.air, g_sky.low_at_air);
    port_GlSkyBind(GL_TEXTURE_3D, 1, g_sky.shape, g_sky.low_at_shape);
    port_GlSkyBind(GL_TEXTURE_3D, 2, g_sky.detail, g_sky.low_at_detail);
    port_GlSkyBind(GL_TEXTURE_2D, 3, g_sky.weather, g_sky.low_at_weather);
    glViewport(0, 0, 4, 4);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glFramebufferTexture2D(
        GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, g_sky.low[0], 0);
    glUseProgram(g_sky.mix_program);
    port_GlSkyBind(GL_TEXTURE_2D, 0, g_sky.march, g_sky.mix_at_march);
    port_GlSkyBind(GL_TEXTURE_2D, 4, g_sky.low[1], g_sky.mix_at_history);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    /* the frame's own, into the picture through a scissor of no pixel */
    glBindFramebuffer(GL_FRAMEBUFFER, picture.framebuffer);
    glUseProgram(g_sky.program);
    port_GlSkyBind(GL_TEXTURE_2D, 0, 0, g_sky.at_picture);
    port_GlSkyBind(GL_TEXTURE_2D, 1, g_sky.low[0], g_sky.at_low_sky);
    port_GlSkyBind(GL_TEXTURE_3D, 2, g_sky.shape, g_sky.at_shape);
    port_GlSkyBind(GL_TEXTURE_3D, 3, g_sky.detail, g_sky.at_detail);
    port_GlSkyBind(GL_TEXTURE_2D, 4, g_sky.air, g_sky.at_air);
    port_GlSkyBind(GL_TEXTURE_2D, 5, g_sky.weather, g_sky.at_weather);
    glEnable(GL_SCISSOR_TEST);
    glScissor(0, 0, 0, 0);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glDisable(GL_SCISSOR_TEST);
    glActiveTexture(GL_TEXTURE0);
    port_GlForgetState();
}

/* ---- painting it ---- */

/* Everything of the sky's look the frame is painted with: worked out of
 * the stage's light, its time and its weather. */
typedef struct {
    s32 night;
    GLfloat far[3];       /* the colour of the distance, of the screen */
    GLfloat light_way[3]; /* the sun's or the moon's way, y up */
    GLfloat light[3];     /* its light on the clouds */
    GLfloat glow[3];
    GLfloat sun_light[3]; /* the sun's disc's colour */
    GLfloat moon_light[3];
    GLfloat stars;
    s32 has_moon;
    GLfloat cover;
    GLfloat clearing;
    GLfloat cirrus;
    GLfloat dark;
    GLfloat horizon;
    GLfloat milky;
    GLfloat shade;
    GLfloat drift[2];
    GLfloat look[4]; /* the clouds' program's u_look */
    GLfloat under[3];
    GLfloat layer[3]; /* the tallest tops, the haze's half distance, how much
                         the far clouds thin */
    GLfloat glint[3]; /* the light's glints on the sea */
    GLfloat seconds;  /* the picture's clock */
    SkyAirKey air;
} SkyLookNow;

/* The look of the sky for the stage's light (its scenery setup, or the
 * time's), the time and the fill's colour (`color`, 0xFFRRGGBB). */
static void port_GlSkyLookNow(SkyLookNow *now, u32 color, s32 stage) {
    const PortGlTime *look = port_GlTime();
    const LightSetup *lights = (const LightSetup *)port_GlTimeTileLights();
    s32 time = look->on ? look->time : PORT_TIME_OWN;
    const SkyLook *at = &g_sky_looks[time];
    float wind[4];
    s32 is_raining = FALSE;
    float ambient[3];
    float most;
    float seconds;
    float speed;
    s32 i;

    memset(now, 0, sizeof(*now));
    now->night = !look->on || look->has_moon != 0.0f;
    for (i = 0; i < 3; i++) {
        ambient[i] = (float)lights->back[i] / CHANNEL_MAX;
        now->far[i] =
            look->on ? look->fog[i]
                     : (float)((color >> (16 - 8 * i)) & 0xFF) / CHANNEL_MAX;
    }
    /* the way of the light: the time's sun or moon, or the stage's own
     * light seen from above whichever way its vector is meant, put low
     * enough to be in a fight's view (its camera looks along the ground
     * and sees little more than a fifth of the way up) */
    if (look->on) {
        now->light_way[0] = look->to_sun[0];
        now->light_way[1] = -look->to_sun[1];
        now->light_way[2] = look->to_sun[2];
    } else {
        float flat[2];
        float up;
        float across;

        flat[0] = (float)lights->lmat.m[0][0];
        flat[1] = (float)lights->lmat.m[0][2];
        up = (float)lights->lmat.m[0][1];
        across = port_SkyRoot(flat[0] * flat[0] + flat[1] * flat[1]);
        if (across + (up < 0.0f ? -up : up) < 1.0f) {
            flat[0] = 0.5f;
            flat[1] = 0.6f;
            across = port_SkyRoot(0.61f);
        }
        if (across < 1e-4f) {
            flat[0] = 0.0f;
            flat[1] = 1.0f;
            across = 1.0f;
        }
        up = up < 0.0f ? -up : up;
        up = up / port_SkyRoot(across * across + up * up);
        up = up < 0.14f ? 0.14f : up > 0.22f ? 0.22f : up;
        now->light_way[0] = flat[0] / across * port_SkyRoot(1.0f - up * up);
        now->light_way[1] = up;
        now->light_way[2] = flat[1] / across * port_SkyRoot(1.0f - up * up);
    }
    port_SkyUnit(now->light_way);
    /* the disc drawn no higher than the look says, on its own bearing: a
     * fight's camera looks down at the fighters and sees the sky only a
     * few degrees up, and a sunset or a moonrise is a disc low over the
     * horizon; the ground keeps the light's own height */
    if (at->shown > 0.0f) {
        float most_up = port_SkySin(at->shown * PI_F / DEGREES_HALF);

        if (now->light_way[1] > most_up) {
            float across = port_SkyRoot(now->light_way[0] * now->light_way[0] +
                                        now->light_way[2] * now->light_way[2]);
            float keep = port_SkyRoot(1.0f - most_up * most_up);

            if (across > 1e-4f) {
                now->light_way[0] *= keep / across;
                now->light_way[2] *= keep / across;
                now->light_way[1] = most_up;
            }
        }
    }
    now->clearing = is_raining ? 0.0f : at->clearing;
    /* the weather: the stage's wind drifts the clouds, its rain closes the
     * sky (gl_dressing.h) */
    port_GlDressingWeather(wind, &is_raining);
    seconds = port_GlLightsClock() / FRAMES_A_SECOND;
    now->seconds = seconds;
    speed = 9.0f + 22.0f * wind[2];
    now->drift[0] = -wind[0] * speed * seconds;
    now->drift[1] = -wind[1] * speed * seconds;
    now->cover = is_raining ? RAIN_COVER : at->cover;
    now->cirrus = is_raining ? 0.0f : at->cirrus;
    now->dark = is_raining ? RAIN_DARK : 0.0f;
    now->horizon = at->horizon;
    now->milky = is_raining ? 0.0f : at->milky;
    now->shade = at->shade;
    now->layer[0] = at->tops;
    now->layer[1] = at->far_haze;
    now->layer[2] = at->thin_far;
    now->look[0] = at->vivid;
    now->look[1] = at->scattered;
    now->look[2] = is_raining ? 0.0f : at->rays;
    now->look[3] = g_sky_open[stage] == SKY_SEA ? 1.0f : 0.0f;
    now->stars = is_raining ? 0.0f : look->on ? look->stars : 1.0f;
    if (now->night) {
        /* the moon: pale, a little of the stage's light's hue in it */
        float moon[3] = {0.80f, 0.84f, 0.92f};

        if (!look->on) {
            for (i = 0; i < 3; i++) {
                moon[i] =
                    0.7f * moon[i] +
                    0.3f *
                        (0.55f + 0.35f * (float)lights->cmat.m[i][0] / GTE_ONE);
            }
        }
        for (i = 0; i < 3; i++) {
            now->moon_light[i] = moon[i] * 1.05f;
            now->glint[i] = is_raining ? 0.0f : moon[i] * 0.55f;
            now->light[i] = moon[i] * at->light * 0.18f;
            now->glow[i] = moon[i] * 0.09f * at->glow;
            /* the night's gradient: the colour of the distance at the
             * horizon, a glow of the stage's ambient light above it, a
             * deep night higher up */
            now->air.low[i] = now->far[i] + ambient[i] * 0.30f;
            now->air.high[i] = now->far[i] * 0.5f + ambient[i] * 0.06f;
            now->air.moon_light[i] = moon[i] * 0.02f;
            now->air.sun[i] = now->light_way[i];
            now->air.sun_air[i] = now->light_way[i];
        }
        now->air.night = TRUE;
        now->air.haze = 1.0f;
        /* the rain's ceiling hides it, but for its glow */
        now->has_moon = !is_raining;
        return;
    }
    /* dawn, day and dusk: the sun's colour (bytes where it meets a face)
     * made a light of at most one in each channel */
    most = look->sun[0] > look->sun[1] ? look->sun[0] : look->sun[1];
    most = most > look->sun[2] ? most : look->sun[2];
    most = most < 1.0f ? 1.0f : most;
    for (i = 0; i < 3; i++) {
        float hue = look->sun[i] / most;
        float deep = hue;

        /* hue to the power `deepen`, by halving and a root: 1, 1.5, 2 */
        if (at->deepen > 1.75f) {
            deep = hue * hue;
        } else if (at->deepen > 1.25f) {
            deep = hue * port_SkyRoot(hue);
        }
        now->sun_light[i] = hue;
        now->glint[i] = is_raining || now->light_way[1] > 0.35f ? 0.0f : hue;
        now->light[i] = deep * at->light;
        now->under[i] = is_raining ? 0.0f : deep * at->under;
        now->glow[i] = hue * hue * at->glow * 0.6f;
        now->air.sun[i] = now->light_way[i];
        now->air.sun_air[i] = now->light_way[i];
    }
    /* the air's light dimmed as at a lower sun: dawn's and dusk's gold */
    if (at->air_height > 0.0f) {
        float flat = port_SkyRoot(now->light_way[0] * now->light_way[0] +
                                  now->light_way[2] * now->light_way[2]);
        float up = port_SkySin(at->air_height * PI_F / DEGREES_HALF);
        float across = port_SkyRoot(1.0f - up * up);

        if (flat > 1e-4f) {
            now->air.sun_air[0] = now->light_way[0] / flat * across;
            now->air.sun_air[1] = up;
            now->air.sun_air[2] = now->light_way[2] / flat * across;
        }
    }
    now->air.haze = at->haze;
    now->air.power = at->power;
    now->air.paint[0] = is_raining ? 0.0f : at->paint;
    now->air.paint[1] = at->palette;
    now->air.paint[2] = at->boost;
    now->air.paint[3] = at->deepen_above;
    /* dawn's and dusk's Belt of Venus */
    if (at->air_height > 0.0f) {
        now->air.belt[0] = 0.10f;
        now->air.belt[1] = 0.045f;
        now->air.belt[2] = 0.06f;
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
    GLfloat middle[2];
    SkyLookNow now;
    const PortGlTime *look;
    GLuint own;
    s32 left;
    s32 right;
    s32 rect[4];
    s32 low_w;
    s32 low_h;
    s32 step;
    s32 quarter;
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
    /* a time of day of the port's own has its sky whatever `sky` says: a
     * day over a black fill is no day */
    if (!g_sky.is_started ||
        (g_sky.kind == PORT_SKY_ORIGINAL && !port_GlTime()->on) ||
        !g_sky.is_stage_frame || view->h <= 0) {
        return;
    }
    /* port_StageIsDressed(): a stage fought Classic keeps the game's
     * colour of the distance */
    if (!port_StageIsDressed()) {
        return;
    }
    stage = port_RemasterStageNumber();
    if (stage < 0 || stage >= SKY_STAGES) {
        return;
    }
    if (stage != g_sky.said_stage) {
        g_sky.said_stage = stage;
        snprintf(g_sky.news, sizeof(g_sky.news),
                 g_sky_open[stage] ? "sky: stage %d is open: the sky"
                                   : "sky: stage %d is closed: the game's "
                                     "colour of the distance",
                 (int)stage);
        g_sky.has_news = TRUE;
    }
    if (!g_sky_open[stage]) {
        return;
    }
    /* a mod's picture of the sky is the stage's own time's */
    look = port_GlTime();
    own = look->on ? 0 : port_GlSkyPicture(stage);
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
    /* the screen's middle as the coprocessor has it, with the drawing
     * offset of the frame's buffer and a wider picture's band */
    middle[0] = (GLfloat)view->ofx / (GLfloat)PORT_REMASTER_ONE +
                (GLfloat)(x + picture.margin);
    middle[1] = (GLfloat)view->ofy / (GLfloat)PORT_REMASTER_ONE + (GLfloat)y;
    /* the frame's rectangle and a wider picture's two bands, the buffer's
     * pixels */
    left = (picture.margin > 0 && picture.display_w > 0)
               ? picture.display_x
               : x + picture.margin;
    right = (picture.margin > 0 && picture.display_w > 0)
                ? picture.display_x + picture.display_w + 2 * picture.margin
                : x + picture.margin + w;
    rect[0] = left * picture.draw_x;
    rect[1] = y * picture.draw_y;
    rect[2] = (right - left) * picture.draw_x;
    rect[3] = h * picture.draw_y;
    step = (rect[3] + LOW_LINES / 2) / LOW_LINES;
    step = step < 1 ? 1 : step;
    if ((rect[2] + step - 1) / step > LOW_W_MOST) {
        step = (rect[2] + LOW_W_MOST - 1) / LOW_W_MOST;
    }
    if ((rect[3] + step - 1) / step > LOW_H_MOST) {
        step = (rect[3] + LOW_H_MOST - 1) / LOW_H_MOST;
    }
    low_w = (rect[2] + step - 1) / step;
    low_h = (rect[3] + step - 1) / step;
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_BLEND);
    if (own == 0) {
        port_GlSkyLookNow(&now, color, stage);
        port_GlSkyMakeNoise();
        port_GlSkyAir(&now.air);
        port_GlSkyMakeLow();
        /* the last picture's clouds are this one's but where the stage's
         * frames jumped (a fight begun again, a menu between) or the
         * buffer's scale changed; a picture between two frames is of the
         * frame on show, and with frames run ahead that is behind the
         * last one drawn */
        if (view->frames - g_sky.history_frames > 8 ||
            g_sky.history_frames - view->frames > 8 ||
            g_sky.prev_scale[0] != (GLfloat)picture.draw_x ||
            g_sky.prev_scale[1] != (GLfloat)picture.draw_y) {
            g_sky.has_history = FALSE;
        }
        g_sky.low_now ^= 1;
        g_sky.phase = (g_sky.phase + 1) & 3;
        quarter = g_sky_quarters[g_sky.phase];
        /* the clouds marched: a quarter of the picture's pixels, into a
         * picture of their own, so that the card's groups of pixels march
         * together */
        glBindFramebuffer(GL_FRAMEBUFFER, g_sky.work);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                               GL_TEXTURE_2D, g_sky.march, 0);
        glDisable(GL_SCISSOR_TEST);
        glViewport(0, 0, (low_w + 1) / 2, (low_h + 1) / 2);
        glUseProgram(g_sky.low_program);
        glBindVertexArray(g_sky.array);
        glUniformMatrix3fv(g_sky.low_view.turn, 1, GL_FALSE, turn);
        glUniform2f(g_sky.low_view.middle, middle[0], middle[1]);
        glUniform1f(g_sky.low_view.h, (GLfloat)view->h);
        glUniform2f(g_sky.low_view.scale, (GLfloat)picture.draw_x,
                    (GLfloat)picture.draw_y);
        glUniform2f(g_sky.low_at_origin, (GLfloat)rect[0], (GLfloat)rect[1]);
        glUniform1f(g_sky.low_at_low, (GLfloat)step);
        glUniform1f(g_sky.low_at_spread, 2.0f);
        /* how wide a marched pixel is, as an angle: the noise is read at
         * the size of its mipmap that fits it */
        glUniform1f(
            g_sky.low_at_footprint,
            (GLfloat)(step * 2) / ((GLfloat)view->h * (GLfloat)picture.draw_x));
        glUniform2f(g_sky.low_at_offset, (GLfloat)(quarter & 1),
                    (GLfloat)(quarter >> 1));
        glUniform3fv(g_sky.low_at_light_way, 1, now.light_way);
        glUniform3fv(g_sky.low_at_light, 1, now.light);
        glUniform3fv(g_sky.low_at_glow, 1, now.glow);
        glUniform1f(g_sky.low_at_cover, now.cover);
        glUniform1f(g_sky.low_at_clearing, now.clearing);
        glUniform1f(g_sky.low_at_cirrus, now.cirrus);
        glUniform1f(g_sky.low_at_dark, now.dark);
        glUniform2f(g_sky.low_at_drift, now.drift[0], now.drift[1]);
        glUniform3fv(g_sky.low_at_far, 1, now.far);
        glUniform1f(g_sky.low_at_horizon, now.horizon);
        glUniform1f(g_sky.low_at_milky, now.milky);
        glUniform1f(g_sky.low_at_shade, now.shade);
        glUniform1i(g_sky.low_at_steps, CLOUD_STEPS);
        glUniform4fv(g_sky.low_at_look, 1, now.look);
        glUniform3fv(g_sky.low_at_under, 1, now.under);
        glUniform3fv(g_sky.low_at_layer, 1, now.layer);
        port_GlSkyBind(GL_TEXTURE_2D, 0, g_sky.air, g_sky.low_at_air);
        port_GlSkyBind(GL_TEXTURE_3D, 1, g_sky.shape, g_sky.low_at_shape);
        port_GlSkyBind(GL_TEXTURE_3D, 2, g_sky.detail, g_sky.low_at_detail);
        port_GlSkyBind(GL_TEXTURE_2D, 3, g_sky.weather, g_sky.low_at_weather);
        glDrawArrays(GL_TRIANGLES, 0, 3);
        /* the clouds' picture put together: the quarter just marched, the
         * rest the last picture's, or the nearest marched where there is
         * none (the fight's first picture) */
        {
            glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                                   GL_TEXTURE_2D, g_sky.low[g_sky.low_now], 0);
            glViewport(0, 0, low_w, low_h);
            glUseProgram(g_sky.mix_program);
            glUniformMatrix3fv(g_sky.mix_view.turn, 1, GL_FALSE, turn);
            glUniform2f(g_sky.mix_view.middle, middle[0], middle[1]);
            glUniform1f(g_sky.mix_view.h, (GLfloat)view->h);
            glUniform2f(g_sky.mix_view.scale, (GLfloat)picture.draw_x,
                        (GLfloat)picture.draw_y);
            glUniform2f(
                g_sky.mix_at_origin, (GLfloat)rect[0], (GLfloat)rect[1]);
            glUniform1f(g_sky.mix_at_low, (GLfloat)step);
            glUniform2f(g_sky.mix_at_offset, (GLfloat)(quarter & 1),
                        (GLfloat)(quarter >> 1));
            glUniformMatrix3fv(
                g_sky.mix_at_prev_turn, 1, GL_FALSE, g_sky.prev_turn);
            glUniform2f(g_sky.mix_at_prev_middle, g_sky.prev_middle[0],
                        g_sky.prev_middle[1]);
            glUniform1f(g_sky.mix_at_prev_h, g_sky.prev_h);
            glUniform2f(g_sky.mix_at_prev_origin, g_sky.prev_origin[0],
                        g_sky.prev_origin[1]);
            glUniform2f(
                g_sky.mix_at_prev_span, g_sky.prev_span[0], g_sky.prev_span[1]);
            glUniform2f(
                g_sky.mix_at_prev_fit, g_sky.prev_fit[0], g_sky.prev_fit[1]);
            glUniform1i(g_sky.mix_at_has_history, g_sky.has_history);
            glUniform2f(g_sky.mix_at_march_most, (GLfloat)((low_w + 1) / 2 - 1),
                        (GLfloat)((low_h + 1) / 2 - 1));
            port_GlSkyBind(GL_TEXTURE_2D, 0, g_sky.march, g_sky.mix_at_march);
            port_GlSkyBind(GL_TEXTURE_2D, 4, g_sky.low[g_sky.low_now ^ 1],
                           g_sky.mix_at_history);
            glDrawArrays(GL_TRIANGLES, 0, 3);
        }
        /* this picture is the next one's last */
        memcpy(g_sky.prev_turn, turn, sizeof(turn));
        g_sky.prev_middle[0] = middle[0];
        g_sky.prev_middle[1] = middle[1];
        g_sky.prev_h = (GLfloat)view->h;
        g_sky.prev_origin[0] = (GLfloat)rect[0];
        g_sky.prev_origin[1] = (GLfloat)rect[1];
        g_sky.prev_span[0] = (GLfloat)(low_w * step);
        g_sky.prev_span[1] = (GLfloat)(low_h * step);
        g_sky.prev_fit[0] = (GLfloat)low_w / (GLfloat)LOW_W_MOST;
        g_sky.prev_fit[1] = (GLfloat)low_h / (GLfloat)LOW_H_MOST;
        g_sky.prev_scale[0] = (GLfloat)picture.draw_x;
        g_sky.prev_scale[1] = (GLfloat)picture.draw_y;
        g_sky.history_frames = view->frames;
        g_sky.has_history = TRUE;
    } else {
        memset(&now, 0, sizeof(now));
    }
    /* the frame */
    glBindFramebuffer(GL_FRAMEBUFFER, picture.framebuffer);
    glUseProgram(g_sky.program);
    glBindVertexArray(g_sky.array);
    glUniformMatrix3fv(g_sky.view.turn, 1, GL_FALSE, turn);
    glUniform2f(g_sky.view.middle, middle[0], middle[1]);
    glUniform1f(g_sky.view.h, (GLfloat)view->h);
    glUniform2f(
        g_sky.view.scale, (GLfloat)picture.draw_x, (GLfloat)picture.draw_y);
    glUniform2f(g_sky.at_origin, (GLfloat)rect[0], (GLfloat)rect[1]);
    glUniform2f(
        g_sky.at_span, (GLfloat)(low_w * step), (GLfloat)(low_h * step));
    glUniform2f(g_sky.at_fit, (GLfloat)low_w / (GLfloat)LOW_W_MOST,
                (GLfloat)low_h / (GLfloat)LOW_H_MOST);
    glUniform2f(
        g_sky.at_fit_most, ((GLfloat)low_w - 0.5f) / (GLfloat)LOW_W_MOST,
        ((GLfloat)low_h - 0.5f) / (GLfloat)LOW_H_MOST);
    glUniform1f(g_sky.at_stars, now.stars);
    glUniform3fv(g_sky.at_sun, 1, now.light_way);
    glUniform3fv(g_sky.at_sun_light, 1, now.sun_light);
    glUniform1i(g_sky.at_has_sun, own == 0 && !now.night && now.dark == 0.0f);
    glUniform3fv(g_sky.at_moon, 1, now.light_way);
    glUniform3fv(g_sky.at_moon_light, 1, now.moon_light);
    glUniform1i(g_sky.at_has_moon, own == 0 && now.has_moon);
    glUniform1f(g_sky.at_moon_size, 0.058f);
    /* what lies before the sky, which the frame draws (SKY_LANDS) */
    glUniform3fv(g_sky.at_light_way, 1, now.light_way);
    glUniform3fv(g_sky.at_light, 1, now.light);
    glUniform3fv(g_sky.at_glow, 1, now.glow);
    glUniform3fv(g_sky.at_far, 1, now.far);
    glUniform1f(g_sky.at_horizon, now.horizon);
    glUniform4fv(g_sky.at_look, 1, now.look);
    glUniform3fv(g_sky.at_glint, 1, now.glint);
    glUniform1f(g_sky.at_time, now.seconds);
    glUniform1i(g_sky.at_has_picture, own != 0);
    port_GlSkyBind(GL_TEXTURE_2D, 0, own, g_sky.at_picture);
    port_GlSkyBind(
        GL_TEXTURE_2D, 1, g_sky.low[g_sky.low_now], g_sky.at_low_sky);
    port_GlSkyBind(GL_TEXTURE_3D, 2, g_sky.shape, g_sky.at_shape);
    port_GlSkyBind(GL_TEXTURE_3D, 3, g_sky.detail, g_sky.at_detail);
    port_GlSkyBind(GL_TEXTURE_2D, 4, g_sky.air, g_sky.at_air);
    port_GlSkyBind(GL_TEXTURE_2D, 5, g_sky.weather, g_sky.at_weather);
    glActiveTexture(GL_TEXTURE0);
    glEnable(GL_SCISSOR_TEST);
    glScissor(rect[0], rect[1], rect[2], rect[3]);
    glViewport(0, 0, picture.target_w, picture.target_h);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    port_GlForgetState();
    g_sky.drawn++;
}
