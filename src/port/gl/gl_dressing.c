/* A stage's dressing in the remastered picture (gl_dressing.h; docs/PORT.md,
 * "The dressing").
 *
 * Three steps. When the ground's mesh is built its triangles are kept here
 * (port_GlDressingNoteGround), with a copy of video memory to read their
 * textures from. The first time the stage's dressing is drawn it is grown:
 * every block of the stage's files is turned into things -- tufts, bushes,
 * reeds, flowers at places looked up on the ground under them; water
 * quads; meshes read from the mods and set in place -- and sent to the card
 * once. Each picture then draws them: the foliage by instancing, its
 * blades built and bent in the vertex shader from the wind, the fighters
 * and their slashes; the meshes; the water; the particles, stateless where
 * they fall or rise over an area, and from a ring of the frame's events
 * where they are a step's dust.
 *
 * Nothing here is the game's state, nor changes a value the game
 * computes. */
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "common.h"
#include "gl.h"
#include "gl_bench.h"
#include "gl_dressing.h"
#include "gl_lights.h"
#include "gl_post.h"
#include "gl_see.h"
#include "gl_sun.h"
#include "gl_time.h"
#include "port_dressing.h"
#include "port_fighter.h"
#include "port_gpu.h"
#include "port_lights.h"
#include "port_mods.h"
#include "port_remaster.h"
#include "port_stage_choice.h"

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
typedef ptrdiff_t GLintptr;

#define GL_FALSE 0
#define GL_TRUE 1
#define GL_TRIANGLES 0x0004
#define GL_FLOAT 0x1406
#define GL_UNSIGNED_BYTE 0x1401
#define GL_ARRAY_BUFFER 0x8892
#define GL_STATIC_DRAW 0x88E4
#define GL_DYNAMIC_DRAW 0x88E8
#define GL_VERTEX_SHADER 0x8B31
#define GL_FRAGMENT_SHADER 0x8B30
#define GL_COMPILE_STATUS 0x8B81
#define GL_LINK_STATUS 0x8B82
#define GL_DEPTH_TEST 0x0B71
#define GL_BLEND 0x0BE2
#define GL_CULL_FACE 0x0B44
#define GL_LESS 0x0201
#define GL_SRC_ALPHA 0x0302
#define GL_ONE_MINUS_SRC_ALPHA 0x0303
#define GL_ONE 1
#define GL_ZERO 0
#define GL_FUNC_ADD 0x8006

#define PORT_GL_DRESSING_FUNCTIONS                                             \
    GL(void, glEnable, (GLenum))                                               \
    GL(void, glDisable, (GLenum))                                              \
    GL(void, glDepthFunc, (GLenum))                                            \
    GL(void, glDepthMask, (GLboolean))                                         \
    GL(void, glBlendFuncSeparate, (GLenum, GLenum, GLenum, GLenum))            \
    GL(void, glBlendEquation, (GLenum))                                        \
    GL(void, glColorMaski,                                                     \
       (GLuint, GLboolean, GLboolean, GLboolean, GLboolean))                   \
    GL(void, glGenBuffers, (GLsizei, GLuint *))                                \
    GL(void, glBindBuffer, (GLenum, GLuint))                                   \
    GL(void, glBufferData, (GLenum, GLsizeiptr, const void *, GLenum))         \
    GL(void, glBufferSubData, (GLenum, GLintptr, GLsizeiptr, const void *))    \
    GL(void, glGenVertexArrays, (GLsizei, GLuint *))                           \
    GL(void, glBindVertexArray, (GLuint))                                      \
    GL(void, glVertexAttribPointer,                                            \
       (GLuint, GLint, GLenum, GLboolean, GLsizei, const void *))              \
    GL(void, glVertexAttribDivisor, (GLuint, GLuint))                          \
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
    GL(void, glUniform1iv, (GLint, GLsizei, const GLint *))                    \
    GL(void, glUniform3fv, (GLint, GLsizei, const GLfloat *))                  \
    GL(void, glUniform4fv, (GLint, GLsizei, const GLfloat *))                  \
    GL(void, glUniformMatrix3fv, (GLint, GLsizei, GLboolean, const GLfloat *)) \
    GL(void, glUniformMatrix4fv, (GLint, GLsizei, GLboolean, const GLfloat *)) \
    GL(void, glDrawArrays, (GLenum, GLint, GLsizei))                           \
    GL(void, glDrawArraysInstanced, (GLenum, GLint, GLsizei, GLsizei))

#define GL(result, name, args) static result(*name) args;
PORT_GL_DRESSING_FUNCTIONS
#undef GL

/* ---- the numbers ---- */

/* The world's y runs down; the ground's normals are 4096 long. */
#define NORMAL_ONE 4096.0f
/* A face of the ground looks up when its normal's y is below this, of one. */
#define FACES_UP (-0.5f)
/* The lookup's squares over the ground, in world units, and the most of
 * them a side. */
#define BIN_SIZE 512.0f
#define BINS_MOST 160
/* Video memory's size, and a texture page's colour tables. */
#define VRAM_W 1024
#define VRAM_H 512
#define TEXTURES_MOST 128
/* The most things of every block of a stage together, and of the meshes'
 * vertices. */
#define THINGS_MOST 100000
#define MESH_VERTICES_MOST 200000
/* How many tries a place is looked for, a thing (most are kept where the
 * ground is right). */
#define TRIES_EACH 4
/* The vertices one instance of each kind of foliage is drawn with: blades
 * of two or three bends and a tip, a bloom's two crossed quads, a bush's
 * leaves as cards. */
#define GRASS_BLADES 7
#define REEDS_BLADES 3
#define FLOWER_BLADES 3
#define BUSH_CARDS 36
#define BLADE_VERTICES(bends) ((bends) * 6 + 3)
#define GRASS_VERTICES (GRASS_BLADES * BLADE_VERTICES(2))
#define REEDS_VERTICES (REEDS_BLADES * BLADE_VERTICES(3))
#define FLOWER_VERTICES (FLOWER_BLADES * BLADE_VERTICES(2) + 12)
#define BUSH_VERTICES (BUSH_CARDS * 6)
/* How far from the camera, in world units, the foliage begins to thin:
 * past it fewer things and fewer blades to a tuft. */
#define FOLIAGE_THIN 9000.0f
/* How far a tuft's blades reach from its root, about. */
#define TUFT_REACH 60.0f
/* What pushes the foliage, at most: the fighters, where they were a few
 * frames ago (so the grass rises again behind them), and the slashes. */
#define PUSHERS_MOST 16
#define PUSH_HISTORY 3
#define PUSH_HISTORY_FRAMES 8
#define SLASHES_MOST 4
/* The dust's ring, and the ripples'. */
#define DUST_MOST 512
#define RIPPLES_MOST 8
/* The game steps sixty times a second. */
#define FRAMES_A_SECOND 60.0f
/* The flames the embers rise from (port_lights.h). */
#define FLAMES_MOST PORT_LIGHTS_FLAMES
/* A colour's channel, and a byte of light that is one (the coprocessor's
 * 128) */
#define CHANNEL_MOST 255.0f
#define PI_F 3.14159265f
#define DEGREES_HALF 180.0f
#define LOG_SIZE 2048
#define NEWS_SIZE 256
/* How far below the fighters' lowest bone his feet are, and how far about
 * his axis he pushes the grass, of his cylinder (gl_see.h). */
#define FEET_REACH 0.55f
/* How near the ground a fighter's or a weapon's height must be to push. */
#define PUSH_HEIGHT 900.0f
/* A fighter who moves this far in a frame kicks up dust and ripples. */
#define STEP_SPEED 9.0f
/* A weapon whose hand moves this far in a frame is a slash. */
#define SLASH_SPEED 45.0f
/* the night's sky, for the water at the stage's own time (gl_time.c's) */
#define NIGHT_HORIZON_R 0.05f
#define NIGHT_HORIZON_G 0.07f
#define NIGHT_HORIZON_B 0.13f
#define NIGHT_ZENITH_R 0.005f
#define NIGHT_ZENITH_G 0.01f
#define NIGHT_ZENITH_B 0.03f

/* ---- what is kept of the ground ---- */

/* A triangle of the ground's mesh that looks up: where it is, its texture
 * points and where it reads them, and whether it is a piece's. */
typedef struct {
    float p[3][3];
    float uv[3][2];
    s16 page[2];
    s16 clut[2];
    u8 bits;
    u8 is_piece;
    s16 piece;
    float normal_y; /* of one, the face's */
} DressTri;

/* gl_stage.c's StageVertex, as far as it is read here (the mesh's own
 * layout: gl_stage.c keeps it, and port_GlDressingNoteGround is given its
 * size). */
typedef struct {
    float x;
    float y;
    float z;
    float u;
    float v;
    u8 color[4];
    s16 page[4];
    u8 info[4];
    u16 tile;
    u16 reflect;
    s16 normal[4];
} DressStageVertex;

/* A thing of the foliage, as the card has it: where its root is and how it
 * is turned; how tall and wide, its seed, and how much it sways; its
 * colour. */
typedef struct {
    float root[4];
    float shape[4];
    u8 color[4];
} DressThing;

/* A vertex of a mesh or of the water. */
typedef struct {
    float place[3];
    float normal[3];
    u8 color[4];
} DressVertex;

/* A particle of the dust: where and when it was kicked up, which way, its
 * size and colour. */
typedef struct {
    float at[4];  /* x y z, the clock it began at */
    float way[4]; /* x y z a second, its size */
    u8 color[4];  /* and its life, in tenths of a second */
} DressDust;

/* What a block of the stage's files grew into. */
typedef struct {
    s32 block; /* its place in the set */
    s32 kind;
    s32 first;      /* its first thing, or mesh or water vertex */
    s32 count;      /* things or vertices */
    s32 is_mesh;    /* its vertices are the meshes' buffer's */
    float ground_y; /* the mean height of the ground it grew on */
} DressRun;

typedef struct {
    GLint view;
    GLint camera;
    GLint h;
    GLint screen;
    GLint target;
    GLint scale;
    GLint near_plane;
    GLint depth;
    GLint depth_shift;
    GLint fog_levels;
    GLint fog_slot;
    GLint fog_color;
    GLint light;
    GLint color;
    GLint back;
    GLint eye;
    GLint tone_on;
    GLint clock;
    GLint wind;
    GLint right;
    GLint down;
    PortGlSunAt sun;
    PortGlLightsAt lights;
    /* the foliage's own */
    GLint kind;
    GLint sway;
    GLint bloom;
    GLint push;
    GLint push_way;
    GLint push_count;
    GLint thin;
    /* the particles' */
    GLint area;
    GLint size;
    GLint tint;
    GLint ground;
    GLint top;
    GLint flames;
    GLint flame_count;
    /* the water's */
    GLint water;
    GLint clarity;
    GLint waves;
    GLint sky_low;
    GLint sky_high;
    GLint ripples;
    GLint ripple_count;
} DressAt;

static struct {
    s32 is_started;
    s32 is_on;
    char problem[NEWS_SIZE];
    s32 has_news;
    char news[NEWS_SIZE];
    /* the programs */
    GLuint foliage;
    GLuint mesh;
    GLuint water;
    GLuint particles;
    GLuint caster;
    GLint caster_at_sun;
    DressAt foliage_at;
    DressAt mesh_at;
    DressAt water_at;
    DressAt particles_at;
    GLuint thing_buffer;
    GLuint thing_array;
    GLuint mesh_buffer;
    GLuint mesh_array;
    GLuint dust_buffer;
    GLuint dust_array;
    GLuint empty_array;
    /* the ground, as the mesh was last built */
    DressTri *tris;
    s32 tri_count;
    s32 ground_serial;
    u16 *vram;
    PortStageTexture textures[TEXTURES_MOST];
    s32 texture_count;
    float lo[2];
    s32 bins_x;
    s32 bins_z;
    s32 *bin_first; /* bins_x * bins_z + 1 */
    s32 *bin_list;
    /* what is grown from it */
    s32 grown_serial;
    s32 grown_stage;
    const PortDressSet *grown_set;
    s32 run_count;
    DressRun runs[PORT_DRESSING_MOST];
    DressThing *things;
    s32 thing_count;
    DressVertex *mesh_vertices;
    s32 mesh_vertex_count;
    u32 hidden[2];
    float wind[4];
    /* the frame's */
    float pushers[PUSHERS_MOST][4];
    float push_ways[PUSHERS_MOST][4];
    s32 pusher_count;
    PortSeeFighter fighters[PUSH_HISTORY][PORT_SEE_FIGHTERS];
    s32 fighter_counts[PUSH_HISTORY];
    u32 frames;
    float weapon_at[PORT_FIGHTER_WEAPONS][3];
    s32 weapon_count;
    s32 slash_count;
    DressDust dust[DUST_MOST];
    s32 dust_next;
    s32 dust_used;
    float ripples[RIPPLES_MOST][4];
    s32 ripple_next;
    u32 seed;
    /* what the last picture drew */
    u32 drawn_things;
    u32 triangles;
    u32 draws;
} g_dress;

/* ---- arithmetic (the window's build has no mathematics library) ---- */

static float port_DressRoot(float value) {
    float root = value > 1.0f ? value : 1.0f;
    s32 i;

    if (value <= 0.0f) {
        return 0.0f;
    }
    for (i = 0; i < 40; i++) {
        float next = 0.5f * (root + value / root);

        if (next >= root) {
            break;
        }
        root = next;
    }
    return root;
}

/* sin of `angle` in radians, to a few parts in a million */
static float port_DressSin(float angle) {
    float x2;
    s32 turns = (s32)(angle / (2.0f * PI_F));

    angle -= (float)turns * 2.0f * PI_F;
    if (angle > PI_F) {
        angle -= 2.0f * PI_F;
    } else if (angle < -PI_F) {
        angle += 2.0f * PI_F;
    }
    if (angle > PI_F / 2.0f) {
        angle = PI_F - angle;
    } else if (angle < -PI_F / 2.0f) {
        angle = -PI_F - angle;
    }
    x2 = angle * angle;
    return angle *
           (1.0f - x2 / 6.0f *
                       (1.0f - x2 / 20.0f *
                                   (1.0f - x2 / 42.0f * (1.0f - x2 / 72.0f))));
}

static float port_DressCos(float angle) {
    return port_DressSin(angle + PI_F / 2.0f);
}

/* A number of 0 to 1 from the run's own sequence (the same in every run:
 * a stage grows the same each time it is loaded). */
static float port_DressRandom(u32 *state) {
    u32 x = *state;

    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    *state = x;
    return (float)(x >> 8) / (float)(1 << 24);
}

static u32 port_DressSeedOf(const char *name, s32 stage, s32 kind) {
    u32 hash = 2166136261u ^ (u32)(stage * 131 + kind);

    while (*name != 0) {
        hash = (hash ^ (u8)*name++) * 16777619u;
    }
    return hash == 0 ? 1 : hash;
}

/* ---- the shaders ---- */

/* What every program of the dressing has: the ground's projection and
 * depth (gl_stage.c's), its slots and fog, and the stage's light. */
#define DRESS_COMMON                                                             \
    "uniform mat3 u_view;\n"                                                     \
    "uniform vec3 u_camera;\n"                                                   \
    "uniform float u_h;\n"                                                       \
    "uniform vec2 u_screen;\n"                                                   \
    "uniform vec2 u_target;\n"                                                   \
    "uniform vec2 u_scale;\n"                                                    \
    "uniform float u_near;\n"                                                    \
    "uniform float u_clock;\n" /* seconds */                                     \
    "uniform vec4 u_wind;\n"   /* its way x and z, strength, gusts */            \
    "uniform vec3 u_right;\n"  /* the camera's axes in the world */              \
    "uniform vec3 u_down;\n"                                                     \
    "uint dress_hash(uint x) {\n"                                                \
    "    x ^= x >> 16; x *= 0x7feb352du; x ^= x >> 15; x *= 0x846ca68bu;\n"      \
    "    x ^= x >> 16; return x;\n"                                              \
    "}\n"                                                                        \
    "float dress_random(uint seed, uint k) {\n"                                  \
    "    return float(dress_hash(seed * 747796405u + k * 2891336453u + 1u)\n"    \
    "                 >> 8) / 16777216.0;\n"                                     \
    "}\n" /* the coprocessor's projection, as gl_stage.c writes it */            \
    "vec4 dress_project(vec3 p, out float depth) {\n"                            \
    "    vec3 v = u_view * (p - u_camera);\n"                                    \
    "    vec2 at = 2.0 * u_scale * u_h * v.xy\n"                                 \
    "            + (2.0 * u_scale * u_screen + vec2(1.0)) * v.z;\n"              \
    "    depth = v.z;\n"                                                         \
    "    return vec4(at / u_target - v.z, v.z - u_near, v.z);\n"                 \
    "}\n" /* the wind at a place: how far it leans a thing's top, of its         \
           * height, its way in x and z */                                       \
    "vec2 dress_wind(vec3 root, float seed) {\n"                                 \
    "    vec2 way = u_wind.xy;\n"                                                \
    "    float along = dot(root.xz, way);\n"                                     \
    "    float sway = 0.55 * sin(u_clock * 1.9 + along * 0.0011 + seed * 6.3)\n" \
    "               + 0.25 * sin(u_clock * 3.3 + along * 0.0023 + seed * 2.1)\n" \
    "               + 0.20 * sin(u_clock * 5.1 + root.x * 0.004 + seed);\n"      \
    "    float gust = sin(u_clock * 0.47 - along * 0.00031)\n"                   \
    "               * sin(u_clock * 0.29 - along * 0.00019 + 1.3);\n"            \
    "    gust = max(gust, 0.0) * u_wind.w * 1.6;\n"                              \
    "    vec2 cross_way = vec2(-way.y, way.x);\n"                                \
    "    float flutter = 0.15 * sin(u_clock * 4.7 + seed * 11.0);\n"             \
    "    return u_wind.z * (way * (0.25 + 0.30 * sway + gust)\n"                 \
    "                       + cross_way * flutter);\n"                           \
    "}\n"

/* The fragments' side: the ground's slots and fog, and the stage's light
 * with the sun's maps and the frame's lights, as the mesh's shader lights
 * the ground (gl_stage.c). */
#define DRESS_FRAGMENT_COMMON                                                        \
    "uniform int u_depth_shift;\n"                                                   \
    "uniform float u_depth;\n"                                                       \
    "uniform int u_fog_levels;\n"                                                    \
    "uniform int u_fog_slot[32];\n"                                                  \
    "uniform vec4 u_fog_color;\n" /* rgb 0 to 1, and how much at the most */         \
    "uniform mat3 u_light;\n"                                                        \
    "uniform mat3 u_color;\n"                                                        \
    "uniform vec3 u_back;\n"                                                         \
    "uniform vec3 u_eye;\n" PORT_GL_SUN_SHADER                                       \
        PORT_GL_LIGHTS_SHADER PORT_GL_TONE_SHADER                                    \
    "int slot_scaled(int scaled) {\n"                                                \
    "    int octave = scaled >> 11;\n"                                               \
    "    int fraction = (scaled & 2047) - 4096;\n"                                   \
    "    return (fraction >> (octave & 31)) & 4095;\n"                               \
    "}\n"                                                                            \
    "int slot_of(int depth) { return slot_scaled(depth >> u_depth_shift); }\n"       \
    "int fog_level(int slot) {\n"                                                    \
    "    int level = 0;\n"                                                           \
    "    for (int i = 0; i < 32; i++) {\n"                                           \
    "        if (i < u_fog_levels && slot >= u_fog_slot[i]) { level = i + 1; }\n"    \
    "    }\n"                                                                        \
    "    return level;\n"                                                            \
    "}\n" /* how far into the fog's levels a depth is, as the mesh's time fog        \
           */                                                                        \
    "float dress_fog(float depth) {\n"                                               \
    "    if (u_fog_levels <= 0) { return 0.0; }\n"                                   \
    "    int slot = slot_of(int(clamp(depth, 0.0, 65535.0)));\n"                     \
    "    int level = fog_level(slot);\n"                                             \
    "    int low = (level > 0) ? u_fog_slot[level - 1] : 0;\n"                       \
    "    int high = (level < u_fog_levels) ? u_fog_slot[level] : low;\n"             \
    "    float part = (high > low)\n"                                                \
    "               ? clamp(float(slot - low) / float(high - low), 0.0, 1.0)\n"      \
    "               : 1.0;\n"                                                        \
    "    return clamp((float(level) + part - 0.5) / float(u_fog_levels),\n"          \
    "                 0.0, 1.0) * u_fog_color.a;\n"                                  \
    "}\n" /* the light at a place, in the game's bytes (128 one), its normal         \
           * of length one: the stage's lights, less the sun where its maps          \
           * hide it, and the frame's lights; `base` the stage's alone */            \
    "vec3 dress_light(vec3 place, vec3 n, out vec3 base) {\n"                        \
    "    vec3 ir = max(u_light * n, vec3(0.0)) * 4096.0;\n"                          \
    "    vec3 color = clamp(u_back + (u_color * ir) / 16.0, 0.0, 255.0);\n"          \
    "    if (u_sun_on != 0) {\n"                                                     \
    "        float seen = sun_visible(place, n);\n"                                  \
    "        if (seen < 1.0) {\n"                                                    \
    "            vec3 to_sun = vec3(u_light[0][0], u_light[1][0], u_light[2][0]);\n" \
    "            float turn = max(dot(to_sun, n), 0.0);\n"                           \
    "            color = sun_shade(color, u_color[0] * turn * 256.0, seen);\n"       \
    "        }\n"                                                                    \
    "    }\n"                                                                        \
    "    base = color;\n"                                                            \
    "    if (u_lights_count != 0) { color += lights_add(place, n); }\n"              \
    "    return color;\n"                                                            \
    "}\n" /* a colour of 0 to 1 lit, in bytes: the texel times the light over        \
           * sixteen, as the mesh (a texel being five bits) */                       \
    "vec3 dress_shade(vec3 albedo, vec3 base, vec3 lit) {\n"                         \
    "    vec3 texel = albedo * 31.0;\n"                                              \
    "    return tone_map(texel * base / 16.0, texel * lit / 16.0);\n"                \
    "}\n"

/* The foliage: a thing's blades, bloom or leaves built from its vertex's
 * number, bent by the wind and pushed by what pushes. */
static const char g_dress_foliage_vertex[] =
    "#version 330 core\n"
    "layout(location = 0) in vec4 a_root;\n"  /* x y z, its turn */
    "layout(location = 1) in vec4 a_shape;\n" /* height, width, seed, sway */
    "layout(location = 2) in vec4 a_color;\n" DRESS_COMMON
    "uniform int u_kind;\n"
    "uniform float u_sway;\n"
    "uniform vec4 u_push[16];\n"     /* x y z, how far */
    "uniform vec4 u_push_way[16];\n" /* x z of a slash's way, strength, 1 for a
                                        way */
    "uniform int u_push_count;\n"
    "uniform float u_thin;\n" /* how far off the foliage begins to thin */
    "out vec3 v_place;\n"
    "out vec3 v_normal;\n"
    "out vec3 v_albedo;\n"
    "out float v_depth;\n"
    "out float v_shade;\n" /* the darker near the root */
    "out vec3 v_card;\n"   /* a leaf's or a bloom's place in its card, and 1 */
    "const vec3 UP = vec3(0.0, -1.0, 0.0);\n"
    /* how far what pushes moves a thing's top here, of its height */
    "vec2 dress_push(vec3 root, float height) {\n"
    "    vec2 sum = vec2(0.0);\n"
    "    for (int i = 0; i < u_push_count; i++) {\n"
    "        vec4 p = u_push[i];\n"
    "        vec2 d = root.xz - p.xz;\n"
    "        float apart = length(d);\n"
    "        if (apart >= p.w || abs(root.y - p.y) > 900.0) { continue; }\n"
    "        float k = 1.0 - apart / p.w;\n"
    "        k = k * k * (3.0 - 2.0 * k) * u_push_way[i].z;\n"
    "        vec2 way = u_push_way[i].w > 0.5 ? u_push_way[i].xy\n"
    "                 : (apart > 1.0 ? d / apart : vec2(1.0, 0.0));\n"
    "        sum += way * k * 1.3;\n"
    "    }\n"
    "    return sum;\n"
    "}\n"
    /* a point of a blade: `t` of the way up, `side` -1 to 1 across */
    "vec3 dress_blade(vec3 base, vec2 lean, float height, float width,\n"
    "                 vec2 across, float t, float side, out vec3 n) {\n"
    "    float most = 0.92;\n"
    "    float bend = length(lean);\n"
    "    if (bend > most) { lean *= most / bend; bend = most; }\n"
    "    float rise = sqrt(1.0 - bend * bend);\n"
    "    vec2 out_at = lean * height * t * t;\n"
    "    float w = width * (1.0 - 0.85 * pow(t, 1.4));\n"
    "    vec3 p = base + vec3(out_at.x + across.x * side * w, -rise * height * t,\n"
    "                         out_at.y + across.y * side * w);\n"
    /* the blade's face: across it, and along it as it bends */
    "    vec3 along = normalize(vec3(2.0 * lean.x * t, -rise, 2.0 * lean.y * t));\n"
    "    n = normalize(cross(vec3(across.x, 0.0, across.y), along));\n"
    "    return p;\n"
    "}\n"
    "void main() {\n"
    "    uint seed = uint(a_shape.z);\n"
    "    vec3 root = a_root.xyz;\n"
    "    float height = a_shape.x;\n"
    "    float width = a_shape.y;\n"
    "    int id = gl_VertexID;\n"
    /* fewer of them farther off, and fewer blades to a tuft: what is far
     * is a few pixels, and the most of the vertices */
    "    float far_off = distance(root, u_camera);\n"
    "    if (dress_random(seed, 99u)\n"
    "        > clamp(1.7 - far_off / u_thin, 0.15, 1.0)) {\n"
    "        gl_Position = vec4(2.0, 2.0, 2.0, 1.0);\n"
    "        return;\n"
    "    }\n"
    "    if (u_kind == 0 && id >= (far_off > 1.4 * u_thin ? 3 : far_off > 0.8 * u_thin ? 5 : 7)\n"
    "                                * 15) {\n"
    "        gl_Position = vec4(2.0, 2.0, 2.0, 1.0);\n"
    "        return;\n"
    "    }\n"
    "    vec2 thing_lean = dress_wind(root, a_shape.z * 0.001) * u_sway * a_shape.w\n"
    "                    + dress_push(root, height);\n"
    "    vec3 p;\n"
    "    vec3 n;\n"
    "    v_card = vec3(0.0);\n"
    "    v_albedo = a_color.rgb;\n"
    "    v_shade = 1.0;\n"
    "    if (u_kind == 1) {\n"
    /* a bush: leaves as cards over half an egg */
    "        int card = id / 6;\n"
    "        int corner = id % 6;\n"
    "        uint s = seed + uint(card) * 7919u;\n"
    "        float phi = dress_random(s, 1u) * 6.2831853;\n"
    "        float up = mix(-0.15, 1.0, sqrt(dress_random(s, 2u)));\n"
    "        float around = sqrt(max(1.0 - up * up, 0.0));\n"
    "        vec3 radii = vec3(height * 0.62 * width, height * 0.52,\n"
    "                          height * 0.62 * width);\n"
    "        vec3 middle = root + vec3(0.0, -height * 0.46, 0.0);\n"
    "        vec3 dir = vec3(cos(phi + a_root.w) * around, -up,\n"
    "                        sin(phi + a_root.w) * around);\n"
    "        vec3 at = middle + dir * radii * mix(0.75, 1.0,\n"
    "                                             dress_random(s, 3u));\n"
    "        n = normalize(dir / max(radii, vec3(1.0)));\n"
    "        vec3 t1 = normalize(cross(n, abs(n.y) > 0.9 ? vec3(1.0, 0.0, 0.0) : UP));\n"
    "        vec3 t2 = cross(n, t1);\n"
    "        float spin = dress_random(s, 4u) * 6.2831853;\n"
    "        vec3 a1 = cos(spin) * t1 + sin(spin) * t2;\n"
    "        vec3 a2 = -sin(spin) * t1 + cos(spin) * t2;\n"
    "        const vec2 quad[6] = vec2[6](vec2(-1.0, -1.0), vec2(1.0, -1.0),\n"
    "            vec2(1.0, 1.0), vec2(-1.0, -1.0), vec2(1.0, 1.0), vec2(-1.0, 1.0));\n"
    "        vec2 q = quad[corner];\n"
    "        float size = height * 0.20 * mix(0.8, 1.2, dress_random(s, 5u));\n"
    "        float rise = clamp((root.y - at.y) / height, 0.0, 1.0);\n"
    "        vec2 sway = thing_lean * 0.25 * rise * height;\n"
    "        p = at + a1 * q.x * size * 0.5 + a2 * q.y * size\n"
    "          + vec3(sway.x, 0.0, sway.y);\n"
    "        v_card = vec3(q, 1.0);\n"
    "        v_shade = mix(0.55, 1.0, rise);\n"
    "        float tone = mix(0.8, 1.15, dress_random(s, 6u));\n"
    "        v_albedo = a_color.rgb * tone;\n"
    "    } else {\n"
    "        int bends = u_kind == 2 ? 3 : 2;\n"
    "        int each = bends * 6 + 3;\n"
    "        int blades = u_kind == 2 ? 3 : u_kind == 3 ? 3 : 7;\n"
    "        int blade = id / each;\n"
    "        int local = id % each;\n"
    "        bool bloom = u_kind == 3 && blade >= blades;\n"
    "        if (bloom) { blade = 0; }\n"
    "        uint s = seed + uint(blade) * 104729u;\n"
    "        float spread = u_kind == 2 ? 55.0 : 48.0;\n"
    "        float a = dress_random(s, 1u) * 6.2831853;\n"
    "        float r = sqrt(dress_random(s, 2u)) * spread * width;\n"
    "        vec3 base = root + vec3(cos(a) * r, 0.0, sin(a) * r);\n"
    "        float turn = a_root.w + dress_random(s, 3u) * 6.2831853;\n"
    "        vec2 across = vec2(cos(turn), sin(turn));\n"
    "        float h = height * mix(0.6, 1.0, dress_random(s, 4u));\n"
    "        float bw = (u_kind == 2 ? 9.0 : u_kind == 3 ? 5.0 : 15.0) * width;\n"
    "        vec2 own_lean = vec2(cos(a), sin(a)) * mix(0.08, 0.35,\n"
    "                                                   dress_random(s, 5u));\n"
    "        vec2 lean = own_lean + thing_lean;\n"
    "        if (bloom) {\n"
    /* the flower's bloom: two crossed quads at the tip of its first blade */
    "            vec3 tip_n;\n"
    "            vec3 tip = dress_blade(base, lean, h, bw, across, 1.0, 0.0, tip_n);\n"
    "            int corner = (id - blades * each) % 6;\n"
    "            int which = (id - blades * each) / 6;\n"
    "            const vec2 quad[6] = vec2[6](vec2(-1.0, -1.0), vec2(1.0, -1.0),\n"
    "                vec2(1.0, 1.0), vec2(-1.0, -1.0), vec2(1.0, 1.0), vec2(-1.0, 1.0));\n"
    "            vec2 q = quad[corner];\n"
    "            vec3 a1 = which == 0 ? vec3(across.x, 0.0, across.y)\n"
    "                                 : vec3(-across.y, 0.0, across.x);\n"
    "            float size = 20.0 + 14.0 * width;\n"
    "            p = tip + a1 * q.x * size + UP * (q.y * 0.6 + 0.4) * size;\n"
    "            n = normalize(UP * 0.6 + normalize(u_camera - tip) * 0.4);\n"
    "            v_card = vec3(q, 2.0);\n"
    "            v_albedo = vec3(-1.0);\n"
    "        } else {\n"
    "            int tri = local / 3;\n"
    "            int corner = local % 3;\n"
    "            float t;\n"
    "            float side;\n"
    "            if (tri < bends * 2) {\n"
    "                int step = tri / 2;\n"
    "                bool second = (tri & 1) == 1;\n"
    "                int k = second ? (corner == 0 ? 1 : corner == 1 ? 3 : 2)\n"
    "                               : (corner == 0 ? 0 : corner == 1 ? 2 : 1);\n"
    "                t = float(step + (k >> 1)) / float(bends + 1);\n"
    "                side = (k & 1) == 0 ? -1.0 : 1.0;\n"
    "            } else {\n"
    "                t = corner == 2 ? 1.0 : float(bends) / float(bends + 1);\n"
    "                side = corner == 0 ? -1.0 : corner == 1 ? 1.0 : 0.0;\n"
    "            }\n"
    "            float w = bw;\n"
    /* a reed's head: brown and wider, below its tip */
    "            if (u_kind == 2 && blade == 0 && t > 0.55 && t < 0.95) {\n"
    "                w *= 2.6;\n"
    "                v_albedo = vec3(0.36, 0.24, 0.14);\n"
    "            }\n"
    "            p = dress_blade(base, lean, h, w, across, t, side, n);\n"
    /* lit nearer the way the ground faces than the way the blade does, as
     * a meadow is lit */
    "            n = normalize(mix(n, UP, 0.6));\n"
    "            v_shade = mix(0.62, 1.08, t);\n"
    "            float tone = mix(0.82, 1.12, dress_random(s, 6u));\n"
    "            if (v_albedo.r >= 0.0) { v_albedo *= tone; }\n"
    "        }\n"
    "    }\n"
    "    v_place = p;\n"
    "    v_normal = n;\n"
    "    gl_Position = dress_project(p, v_depth);\n"
    "}\n";

/* The meshes: their vertices as they are, in the world. */
static const char g_dress_mesh_vertex[] =
    "#version 330 core\n"
    "layout(location = 0) in vec3 a_place;\n"
    "layout(location = 1) in vec3 a_normal;\n"
    "layout(location = 2) in vec4 a_color;\n" DRESS_COMMON "out vec3 v_place;\n"
    "out vec3 v_normal;\n"
    "out vec3 v_albedo;\n"
    "out float v_depth;\n"
    "out float v_shade;\n"
    "out vec3 v_card;\n"
    "void main() {\n"
    "    v_place = a_place;\n"
    "    v_normal = a_normal;\n"
    "    v_albedo = a_color.rgb;\n"
    "    v_shade = 1.0;\n"
    "    v_card = vec3(0.0);\n"
    "    gl_Position = dress_project(a_place, v_depth);\n"
    "}\n";

/* The foliage's and the meshes' fragments: lit as the ground, into the
 * colour buffer, the ground's slots and its depth. */
static const char g_dress_solid_fragment[] =
    "#version 430 core\n" DRESS_FRAGMENT_COMMON "uniform vec3 u_bloom;\n"
    "in vec3 v_place;\n"
    "in vec3 v_normal;\n"
    "in vec3 v_albedo;\n"
    "in float v_depth;\n"
    "in float v_shade;\n"
    "in vec3 v_card;\n"
    "layout(location = 0) out vec4 o_color;\n"
    "layout(location = 1) out float o_slot;\n"
    "layout(location = 2) out float o_deep;\n"
    "void main() {\n"
    "    vec3 albedo = v_albedo;\n"
    "    if (v_card.z > 0.5) {\n"
    "        vec2 q = v_card.xy;\n"
    "        if (v_card.z > 1.5) {\n"
    /* a bloom: five petals about a middle */
    "            float r = length(q);\n"
    "            float a = atan(q.y, q.x);\n"
    "            float petal = 0.55 + 0.45 * abs(cos(a * 2.5));\n"
    "            if (r > petal) { discard; }\n"
    "            albedo = r < 0.22 ? vec3(0.95, 0.82, 0.30) : u_bloom;\n"
    "        } else {\n"
    /* a leaf: pointed at both ends */
    "            float w = 0.55 * (1.0 - q.y * q.y);\n"
    "            if (abs(q.x) > w) { discard; }\n"
    "            albedo *= 0.85 + 0.15 * (1.0 - abs(q.x) / max(w, 0.01));\n"
    "        }\n"
    "    }\n"
    "    vec3 n = normalize(v_normal);\n"
    "    if (dot(n, u_eye - v_place) < 0.0 && v_card.z < 0.5) { n = -n; }\n"
    "    vec3 base;\n"
    "    vec3 lit = dress_light(v_place, n, base);\n"
    "    vec3 color = dress_shade(albedo * v_shade, base, lit);\n"
    "    color = mix(color, u_fog_color.rgb * 255.0, dress_fog(v_depth));\n"
    "    o_color = vec4(color / 255.0, 1.0);\n"
    "    gl_FragDepth = clamp(v_depth / u_depth, 0.0, 1.0);\n"
    "    o_slot = float(slot_of(int(clamp(v_depth, 0.0, 65535.0))));\n"
    "    o_deep = gl_FragDepth * 32768.0;\n"
    "}\n";

/* The water: a level quad of each of its rectangles. */
static const char g_dress_water_vertex[] =
    "#version 330 core\n"
    "layout(location = 0) in vec3 a_place;\n" DRESS_COMMON "out vec3 v_place;\n"
    "out float v_depth;\n"
    "void main() {\n"
    "    v_place = a_place;\n"
    "    gl_Position = dress_project(a_place, v_depth);\n"
    "}\n";

static const char g_dress_water_fragment[] =
    "#version 430 core\n" DRESS_FRAGMENT_COMMON "uniform float u_clock;\n"
    "uniform vec4 u_wind;\n"
    "uniform vec3 u_water;\n" /* its own colour, 0 to 1 */
    "uniform float u_clarity;\n"
    "uniform float u_waves;\n"
    "uniform vec3 u_sky_low;\n" /* the sky at the horizon and overhead */
    "uniform vec3 u_sky_high;\n"
    "uniform vec4 u_ripples[8];\n" /* x z, the clock it began at, strength */
    "uniform int u_ripple_count;\n"
    "in vec3 v_place;\n"
    "in float v_depth;\n"
    "layout(location = 0) out vec4 o_color;\n"
    /* the water's slope at a place: waves along the wind and across it,
     * and the rings where a fighter waded */
    "vec2 slope(vec2 at) {\n"
    "    vec2 s = vec2(0.0);\n"
    "    vec2 way = normalize(u_wind.xy + vec2(1e-4));\n"
    "    vec2 dirs[4] = vec2[4](way, vec2(-way.y, way.x),\n"
    "                           normalize(way + vec2(0.6, -0.3)),\n"
    "                           normalize(vec2(-0.4, 0.9) - way * 0.5));\n"
    "    float sizes[4] = float[4](900.0, 610.0, 330.0, 170.0);\n"
    "    for (int i = 0; i < 4; i++) {\n"
    "        float k = 6.2831853 / sizes[i];\n"
    "        float speed = sqrt(9.8 * 1000.0 / k) * 0.6;\n"
    "        float phase = k * (dot(dirs[i], at) - speed * u_clock);\n"
    "        float amp = sizes[i] * 0.018 * u_waves;\n"
    "        s += dirs[i] * cos(phase) * amp * k;\n"
    "    }\n"
    "    for (int i = 0; i < u_ripple_count; i++) {\n"
    "        vec4 r = u_ripples[i];\n"
    "        float age = u_clock - r.z;\n"
    "        if (age < 0.0 || age > 3.0) { continue; }\n"
    "        vec2 d = at - r.xy;\n"
    "        float apart = length(d) + 1e-3;\n"
    "        float ring = age * 420.0;\n"
    "        float off = apart - ring;\n"
    "        float fall = exp(-age * 1.4) * exp(-off * off / 9000.0) * r.w;\n"
    "        s += d / apart * cos(off * 0.06) * fall * 0.35;\n"
    "    }\n"
    "    return s;\n"
    "}\n"
    "void main() {\n"
    "    vec2 s = slope(v_place.xz);\n"
    /* up is -y */
    "    vec3 n = normalize(vec3(-s.x, -1.0, -s.y));\n"
    "    vec3 from_eye = normalize(v_place - u_eye);\n"
    "    vec3 r = reflect(from_eye, n);\n"
    "    float up = clamp(-r.y, 0.0, 1.0);\n"
    "    vec3 sky = mix(u_sky_low, u_sky_high, pow(up, 0.55)) * 255.0;\n"
    "    vec3 base;\n"
    "    vec3 lit = dress_light(v_place, vec3(0.0, -1.0, 0.0), base);\n"
    "    vec3 deep = dress_shade(u_water, base, lit);\n"
    "    float facing = clamp(dot(-from_eye, n), 0.0, 1.0);\n"
    "    float fresnel = 0.02 + 0.98 * pow(1.0 - facing, 5.0);\n"
    "    vec3 to_sun = normalize(vec3(u_light[0][0], u_light[1][0],\n"
    "                                 u_light[2][0]) + vec3(1e-5));\n"
    "    float seen = u_sun_on != 0 ? sun_visible(v_place, vec3(0.0, -1.0, 0.0))\n"
    "                               : 1.0;\n"
    "    vec3 sun = u_color[0] * 256.0;\n"
    "    float glint = pow(max(dot(r, to_sun), 0.0), 220.0) * 3.0\n"
    "                + pow(max(dot(r, to_sun), 0.0), 24.0) * 0.12;\n"
    "    vec3 color = mix(deep, sky, fresnel * 0.82) + sun * glint * seen;\n"
    "    if (u_lights_count != 0) {\n"
    "        color += lights_add(v_place, n) * (0.25 + fresnel);\n"
    "    }\n"
    "    color = mix(color, u_fog_color.rgb * 255.0, dress_fog(v_depth) * 0.55);\n"
    "    float alpha = clamp(mix(1.0 - u_clarity, 1.0, fresnel) + glint, 0.0, 1.0);\n"
    "    o_color = vec4(clamp(color, 0.0, 255.0) / 255.0, alpha);\n"
    "    gl_FragDepth = clamp(v_depth / u_depth, 0.0, 1.0);\n"
    "}\n";

/* The particles: quads facing the camera. Over an area each is worked out
 * from its number and the clock alone; the dust from its own event. */
static const char g_dress_particles_vertex[] =
    "#version 330 core\n"
    "layout(location = 0) in vec4 a_at;\n"  /* the dust's: x y z, begun */
    "layout(location = 1) in vec4 a_way;\n" /* a second's move, its size */
    "layout(location = 2) in vec4 a_color;\n" DRESS_COMMON
    "uniform int u_kind;\n"
    "uniform vec4 u_area;\n" /* x0 z0 x1 z1 */
    "uniform float u_size;\n"
    "uniform vec3 u_tint;\n"
    "uniform float u_ground;\n"
    "uniform float u_top;\n"
    "uniform vec4 u_flames[5];\n"
    "uniform int u_flame_count;\n"
    "out vec3 v_place;\n"
    "out float v_depth;\n"
    "out vec2 v_corner;\n"
    "out vec4 v_color;\n"
    "out float v_glow;\n"
    "void main() {\n"
    "    const vec2 quad[6] = vec2[6](vec2(-1.0, -1.0), vec2(1.0, -1.0),\n"
    "        vec2(1.0, 1.0), vec2(-1.0, -1.0), vec2(1.0, 1.0), vec2(-1.0, 1.0));\n"
    "    vec2 q = quad[gl_VertexID % 6];\n"
    "    uint s = uint(gl_InstanceID) * 2654435761u + uint(u_kind) * 97u;\n"
    "    vec3 at;\n"
    "    float size = u_size * mix(0.7, 1.3, dress_random(s, 9u));\n"
    "    float alpha = 1.0;\n"
    "    float spin = 0.0;\n"
    "    v_glow = 0.0;\n"
    "    vec3 tint = u_tint;\n"
    "    vec2 drift = u_wind.xy * u_wind.z;\n"
    "    if (u_kind == 0) {\n"
    /* dust: from its event, slowing, rising a little, fading */
    "        float age = u_clock - a_at.w;\n"
    "        float life = a_color.a * 25.5;\n"
    "        if (age < 0.0 || age > life) { gl_Position = vec4(2.0, 2.0, 2.0, 1.0); return; }\n"
    "        float slow = (1.0 - exp(-age * 2.2)) / 2.2;\n"
    "        at = a_at.xyz + a_way.xyz * slow + vec3(drift.x, 0.0, drift.y) * age * 60.0;\n"
    "        size = a_way.w * (1.0 + age * 1.6);\n"
    "        alpha = 0.55 * (1.0 - age / life) * smoothstep(0.0, 0.12, age);\n"
    "        tint = a_color.rgb;\n"
    "    } else if (u_kind == 2) {\n"
    /* embers: rising from a flame, wandering, cooling */
    "        int flame = int(dress_random(s, 1u) * float(max(u_flame_count, 1)));\n"
    "        vec4 f = u_flames[min(flame, 4)];\n"
    "        float life = mix(1.4, 3.2, dress_random(s, 2u));\n"
    "        float age = fract(u_clock / life + dress_random(s, 3u)) * life;\n"
    "        float wob = dress_random(s, 4u) * 6.2831853;\n"
    "        at = f.xyz + vec3(sin(u_clock * 2.3 + wob) * 60.0 * age\n"
    "                          + (dress_random(s, 5u) - 0.5) * 160.0,\n"
    "                          -age * mix(260.0, 520.0, dress_random(s, 6u)),\n"
    "                          cos(u_clock * 1.9 + wob) * 60.0 * age\n"
    "                          + (dress_random(s, 7u) - 0.5) * 160.0)\n"
    "           + vec3(drift.x, 0.0, drift.y) * age * 120.0;\n"
    "        alpha = (1.0 - age / life);\n"
    "        v_glow = 1.0;\n"
    "        tint = mix(u_tint, vec3(1.0, 0.25, 0.05), age / life);\n"
    "        if (u_flame_count == 0) { alpha = 0.0; }\n"
    "    } else {\n"
    /* over an area: falling (leaves, snow, petals) or hovering (fireflies) */
    "        vec2 at_xz = mix(u_area.xy, u_area.zw,\n"
    "                         vec2(dress_random(s, 1u), dress_random(s, 2u)));\n"
    "        float wob = dress_random(s, 4u) * 6.2831853;\n"
    "        if (u_kind == 5) {\n"
    "            float y = u_ground - mix(80.0, u_top, dress_random(s, 3u));\n"
    "            at = vec3(at_xz.x + sin(u_clock * 0.37 + wob) * 260.0,\n"
    "                      y + sin(u_clock * 0.71 + wob * 1.7) * 90.0,\n"
    "                      at_xz.y + cos(u_clock * 0.29 + wob) * 260.0);\n"
    "            float blink = sin(u_clock * mix(0.9, 1.7, dress_random(s, 5u)) + wob);\n"
    "            alpha = smoothstep(0.2, 0.9, blink);\n"
    "            v_glow = 1.0;\n"
    "        } else {\n"
    "            float fall = u_kind == 3 ? 160.0 : u_kind == 1 ? 210.0 : 170.0;\n"
    "            fall *= mix(0.7, 1.3, dress_random(s, 5u));\n"
    "            float life = u_top / fall;\n"
    "            float age = fract(u_clock / life + dress_random(s, 3u)) * life;\n"
    "            float sway = u_kind == 3 ? 40.0 : 140.0;\n"
    "            at = vec3(at_xz.x + sin(u_clock * 1.3 + wob) * sway,\n"
    "                      u_ground - u_top + age * fall,\n"
    "                      at_xz.y + cos(u_clock * 1.1 + wob) * sway)\n"
    "               + vec3(drift.x, 0.0, drift.y) * age * 90.0;\n"
    "            alpha = smoothstep(0.0, 0.4, age) * smoothstep(life, life - 0.6, age);\n"
    "            spin = u_clock * mix(1.5, 4.0, dress_random(s, 6u)) + wob;\n"
    "            tint *= mix(0.8, 1.15, dress_random(s, 7u));\n"
    "        }\n"
    "    }\n"
    "    vec2 turned = vec2(cos(spin) * q.x - sin(spin) * q.y,\n"
    "                       sin(spin) * q.x + cos(spin) * q.y);\n"
    "    float squash = (u_kind == 1 || u_kind == 4) ? 0.55 : 1.0;\n"
    "    vec3 p = at + u_right * turned.x * size + u_down * turned.y * size * squash;\n"
    "    v_place = p;\n"
    "    v_corner = q;\n"
    "    v_color = vec4(tint, alpha);\n"
    "    gl_Position = dress_project(p, v_depth);\n"
    "}\n";

static const char g_dress_particles_fragment[] =
    "#version 430 core\n" DRESS_FRAGMENT_COMMON "uniform int u_kind;\n"
    "in vec3 v_place;\n"
    "in float v_depth;\n"
    "in vec2 v_corner;\n"
    "in vec4 v_color;\n"
    "in float v_glow;\n"
    "layout(location = 0) out vec4 o_color;\n"
    "void main() {\n"
    "    float r = length(v_corner);\n"
    "    float shape;\n"
    "    if (u_kind == 1 || u_kind == 4) {\n"
    "        float w = 0.6 * (1.0 - v_corner.y * v_corner.y);\n"
    "        shape = abs(v_corner.x) < w ? 1.0 : 0.0;\n"
    "    } else if (u_kind == 0) {\n"
    "        shape = (1.0 - smoothstep(0.2, 1.0, r));\n"
    "    } else {\n"
    "        shape = 1.0 - smoothstep(0.45, 1.0, r);\n"
    "    }\n"
    "    float alpha = v_color.a * shape;\n"
    "    if (alpha < 0.01) { discard; }\n"
    "    vec3 color;\n"
    "    if (v_glow > 0.5) {\n"
    "        color = v_color.rgb * 255.0 * (1.0 + (1.0 - r));\n"
    "    } else {\n"
    "        vec3 base;\n"
    "        vec3 lit = dress_light(v_place, normalize(u_eye - v_place), base);\n"
    "        color = dress_shade(v_color.rgb, base, max(lit, base));\n"
    "    }\n"
    "    color = mix(color, u_fog_color.rgb * 255.0, dress_fog(v_depth));\n"
    "    o_color = vec4(clamp(color, 0.0, 255.0) / 255.0, alpha);\n"
    "}\n";

/* The meshes as the sun or a light sees them: their depth alone. */
static const char g_dress_caster_vertex[] =
    "#version 330 core\n"
    "layout(location = 0) in vec3 a_place;\n"
    "uniform mat4 u_sun;\n"
    "void main() {\n"
    "    gl_Position = u_sun * vec4(a_place, 1.0);\n"
    "}\n";

static const char g_dress_caster_fragment[] =
    "#version 330 core\n"
    "void main() {\n"
    "}\n";

/* ---- making the programs ---- */

static void port_DressSay(const char *what, const char *more) {
    snprintf(g_dress.problem, sizeof(g_dress.problem), "%s%s", what,
             more != NULL ? more : "");
}

static GLuint port_DressShader(GLenum kind, const char *text) {
    GLuint shader = glCreateShader(kind);
    GLint ok = GL_FALSE;

    glShaderSource(shader, 1, &text, NULL);
    glCompileShader(shader);
    glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        static char log[LOG_SIZE];

        glGetShaderInfoLog(shader, (GLsizei)sizeof(log), NULL, log);
        port_DressSay("a dressing shader does not compile: ", log);
        return 0;
    }
    return shader;
}

static GLuint port_DressProgram(
    const char *vertex_text, const char *fragment_text) {
    GLuint vertex = port_DressShader(GL_VERTEX_SHADER, vertex_text);
    GLuint fragment = port_DressShader(GL_FRAGMENT_SHADER, fragment_text);
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
        static char log[LOG_SIZE];

        glGetProgramInfoLog(program, (GLsizei)sizeof(log), NULL, log);
        port_DressSay("a dressing program does not link: ", log);
        return 0;
    }
    return program;
}

static void port_DressLocate(GLuint program, DressAt *at) {
    at->view = glGetUniformLocation(program, "u_view");
    at->camera = glGetUniformLocation(program, "u_camera");
    at->h = glGetUniformLocation(program, "u_h");
    at->screen = glGetUniformLocation(program, "u_screen");
    at->target = glGetUniformLocation(program, "u_target");
    at->scale = glGetUniformLocation(program, "u_scale");
    at->near_plane = glGetUniformLocation(program, "u_near");
    at->depth = glGetUniformLocation(program, "u_depth");
    at->depth_shift = glGetUniformLocation(program, "u_depth_shift");
    at->fog_levels = glGetUniformLocation(program, "u_fog_levels");
    at->fog_slot = glGetUniformLocation(program, "u_fog_slot");
    at->fog_color = glGetUniformLocation(program, "u_fog_color");
    at->light = glGetUniformLocation(program, "u_light");
    at->color = glGetUniformLocation(program, "u_color");
    at->back = glGetUniformLocation(program, "u_back");
    at->eye = glGetUniformLocation(program, "u_eye");
    at->tone_on = glGetUniformLocation(program, "u_tone_on");
    at->clock = glGetUniformLocation(program, "u_clock");
    at->wind = glGetUniformLocation(program, "u_wind");
    at->right = glGetUniformLocation(program, "u_right");
    at->down = glGetUniformLocation(program, "u_down");
    at->kind = glGetUniformLocation(program, "u_kind");
    at->sway = glGetUniformLocation(program, "u_sway");
    at->bloom = glGetUniformLocation(program, "u_bloom");
    at->push = glGetUniformLocation(program, "u_push");
    at->push_way = glGetUniformLocation(program, "u_push_way");
    at->push_count = glGetUniformLocation(program, "u_push_count");
    at->thin = glGetUniformLocation(program, "u_thin");
    at->area = glGetUniformLocation(program, "u_area");
    at->size = glGetUniformLocation(program, "u_size");
    at->tint = glGetUniformLocation(program, "u_tint");
    at->ground = glGetUniformLocation(program, "u_ground");
    at->top = glGetUniformLocation(program, "u_top");
    at->flames = glGetUniformLocation(program, "u_flames");
    at->flame_count = glGetUniformLocation(program, "u_flame_count");
    at->water = glGetUniformLocation(program, "u_water");
    at->clarity = glGetUniformLocation(program, "u_clarity");
    at->waves = glGetUniformLocation(program, "u_waves");
    at->sky_low = glGetUniformLocation(program, "u_sky_low");
    at->sky_high = glGetUniformLocation(program, "u_sky_high");
    at->ripples = glGetUniformLocation(program, "u_ripples");
    at->ripple_count = glGetUniformLocation(program, "u_ripple_count");
    port_GlSunLocate(program, &at->sun);
    port_GlLightsLocate(program, &at->lights);
}

const char *port_GlDressingStart(void *(*getproc)(const char *name)) {
    if (g_dress.is_started) {
        return NULL;
    }
#define GL(result, name, args)                                                 \
    name = (result(*) args)getproc(#name);                                     \
    if (name == NULL) {                                                        \
        port_DressSay("no ", #name);                                           \
        return g_dress.problem;                                                \
    }
    PORT_GL_DRESSING_FUNCTIONS
#undef GL
    g_dress.foliage =
        port_DressProgram(g_dress_foliage_vertex, g_dress_solid_fragment);
    g_dress.mesh =
        port_DressProgram(g_dress_mesh_vertex, g_dress_solid_fragment);
    g_dress.water =
        port_DressProgram(g_dress_water_vertex, g_dress_water_fragment);
    g_dress.particles =
        port_DressProgram(g_dress_particles_vertex, g_dress_particles_fragment);
    g_dress.caster =
        port_DressProgram(g_dress_caster_vertex, g_dress_caster_fragment);
    if (g_dress.foliage == 0 || g_dress.mesh == 0 || g_dress.water == 0 ||
        g_dress.particles == 0 || g_dress.caster == 0) {
        return g_dress.problem;
    }
    g_dress.caster_at_sun = glGetUniformLocation(g_dress.caster, "u_sun");
    port_DressLocate(g_dress.foliage, &g_dress.foliage_at);
    port_DressLocate(g_dress.mesh, &g_dress.mesh_at);
    port_DressLocate(g_dress.water, &g_dress.water_at);
    port_DressLocate(g_dress.particles, &g_dress.particles_at);
    glGenBuffers(1, &g_dress.thing_buffer);
    glGenBuffers(1, &g_dress.mesh_buffer);
    glGenBuffers(1, &g_dress.dust_buffer);
    glGenVertexArrays(1, &g_dress.thing_array);
    glGenVertexArrays(1, &g_dress.mesh_array);
    glGenVertexArrays(1, &g_dress.dust_array);
    glGenVertexArrays(1, &g_dress.empty_array);
    {
        const DressVertex *one = NULL;
        const DressDust *dust = NULL;

        glBindVertexArray(g_dress.mesh_array);
        glBindBuffer(GL_ARRAY_BUFFER, g_dress.mesh_buffer);
        glVertexAttribPointer(
            0, 3, GL_FLOAT, GL_FALSE, (GLsizei)sizeof(*one), one->place);
        glVertexAttribPointer(
            1, 3, GL_FLOAT, GL_FALSE, (GLsizei)sizeof(*one), one->normal);
        glVertexAttribPointer(
            2, 4, GL_UNSIGNED_BYTE, GL_TRUE, (GLsizei)sizeof(*one), one->color);
        glEnableVertexAttribArray(0);
        glEnableVertexAttribArray(1);
        glEnableVertexAttribArray(2);
        glBindVertexArray(g_dress.dust_array);
        glBindBuffer(GL_ARRAY_BUFFER, g_dress.dust_buffer);
        glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)sizeof(g_dress.dust), NULL,
                     GL_DYNAMIC_DRAW);
        glVertexAttribPointer(
            0, 4, GL_FLOAT, GL_FALSE, (GLsizei)sizeof(*dust), dust->at);
        glVertexAttribPointer(
            1, 4, GL_FLOAT, GL_FALSE, (GLsizei)sizeof(*dust), dust->way);
        glVertexAttribPointer(2, 4, GL_UNSIGNED_BYTE, GL_TRUE,
                              (GLsizei)sizeof(*dust), dust->color);
        glVertexAttribDivisor(0, 1);
        glVertexAttribDivisor(1, 1);
        glVertexAttribDivisor(2, 1);
        glEnableVertexAttribArray(0);
        glEnableVertexAttribArray(1);
        glEnableVertexAttribArray(2);
        glBindVertexArray(g_dress.thing_array);
        glBindBuffer(GL_ARRAY_BUFFER, g_dress.thing_buffer);
        glVertexAttribDivisor(0, 1);
        glVertexAttribDivisor(1, 1);
        glVertexAttribDivisor(2, 1);
        glEnableVertexAttribArray(0);
        glEnableVertexAttribArray(1);
        glEnableVertexAttribArray(2);
        glBindVertexArray(0);
        glBindBuffer(GL_ARRAY_BUFFER, 0);
    }
    g_dress.grown_serial = -1;
    g_dress.seed = 0x9E3779B9u;
    g_dress.is_started = TRUE;
    return NULL;
}

void port_GlDressingSetOn(s32 on) { g_dress.is_on = on != 0; }

s32 port_GlDressingIsOn(void) { return g_dress.is_on; }

u32 port_GlDressingThings(void) { return g_dress.drawn_things; }

u32 port_GlDressingTriangles(void) { return g_dress.triangles; }

u32 port_GlDressingDraws(void) { return g_dress.draws; }

const char *port_GlDressingNews(void) {
    if (!g_dress.has_news) {
        return NULL;
    }
    g_dress.has_news = FALSE;
    return g_dress.news;
}

/* ---- the ground ---- */

/* Which row a face's colour table is really at: the row it names is the
 * one the fog writes over each frame; the table was loaded where its
 * texture says (port_lights.py, read_mesh). */
static s32 port_DressClutRow(s32 x, s32 y) {
    s32 i;

    for (i = 0; i < g_dress.texture_count; i++) {
        const PortStageTexture *t = &g_dress.textures[i];
        s32 w = t->clut_w > 1 ? t->clut_w : 1;
        s32 h = t->clut_h > 1 ? t->clut_h : 1;

        if (t->clut_x >= 0 && x >= t->clut_x && x < t->clut_x + w &&
            y >= t->named_y && y < t->named_y + h) {
            return t->clut_y + y - t->named_y;
        }
    }
    return y;
}

void port_GlDressingNoteGround(
    const void *vertices, s32 stride, s32 count, s32 pieces_from) {
    const u8 *bytes = (const u8 *)vertices;
    const u16 *vram = g_port_gl_vram != NULL ? g_port_gl_vram : g_port_vram;
    s32 kept = 0;
    s32 i;
    s32 b;
    float hi[2];

    g_dress.ground_serial++;
    g_dress.tri_count = 0;
    free(g_dress.tris);
    free(g_dress.bin_first);
    free(g_dress.bin_list);
    g_dress.tris = NULL;
    g_dress.bin_first = NULL;
    g_dress.bin_list = NULL;
    if (vertices == NULL || count < 3 ||
        stride < (s32)sizeof(DressStageVertex)) {
        return;
    }
    g_dress.tris = (DressTri *)malloc((size_t)(count / 3) * sizeof(DressTri));
    if (g_dress.vram == NULL) {
        g_dress.vram = (u16 *)malloc(VRAM_W * VRAM_H * sizeof(u16));
    }
    if (g_dress.tris == NULL || g_dress.vram == NULL) {
        return;
    }
    memcpy(g_dress.vram, vram, VRAM_W * VRAM_H * sizeof(u16));
    {
        u32 version = 0;
        s32 textures = 0;
        const PortStageTexture *texture =
            port_RemasterStageTextures(&textures, &version);

        if (textures > TEXTURES_MOST) {
            textures = TEXTURES_MOST;
        }
        g_dress.texture_count = textures;
        if (textures > 0) {
            memcpy(g_dress.textures, texture,
                   (size_t)textures * sizeof(PortStageTexture));
        }
    }
    for (i = 0; i + 2 < count; i += 3) {
        const DressStageVertex *v[3];
        DressTri *t = &g_dress.tris[kept];
        float up;
        s32 k;

        for (k = 0; k < 3; k++) {
            v[k] = (const DressStageVertex *)(bytes + (size_t)(i + k) * stride);
        }
        up = ((float)v[0]->normal[1] + (float)v[1]->normal[1] +
              (float)v[2]->normal[1]) /
             (3.0f * NORMAL_ONE);
        if (up > FACES_UP) {
            continue;
        }
        for (k = 0; k < 3; k++) {
            t->p[k][0] = v[k]->x;
            t->p[k][1] = v[k]->y;
            t->p[k][2] = v[k]->z;
            t->uv[k][0] = v[k]->u;
            t->uv[k][1] = v[k]->v;
        }
        t->page[0] = v[0]->page[0];
        t->page[1] = v[0]->page[1];
        t->clut[0] = v[0]->page[2];
        t->clut[1] = (s16)port_DressClutRow(v[0]->page[2], v[0]->page[3]);
        t->bits = v[0]->info[0];
        t->is_piece = i >= pieces_from;
        t->piece = (s16)(t->is_piece ? (s32)v[0]->tile - 512 : -1);
        t->normal_y = up;
        if (kept == 0) {
            g_dress.lo[0] = hi[0] = t->p[0][0];
            g_dress.lo[1] = hi[1] = t->p[0][2];
        }
        for (k = 0; k < 3; k++) {
            if (t->p[k][0] < g_dress.lo[0]) {
                g_dress.lo[0] = t->p[k][0];
            }
            if (t->p[k][0] > hi[0]) {
                hi[0] = t->p[k][0];
            }
            if (t->p[k][2] < g_dress.lo[1]) {
                g_dress.lo[1] = t->p[k][2];
            }
            if (t->p[k][2] > hi[1]) {
                hi[1] = t->p[k][2];
            }
        }
        kept++;
    }
    g_dress.tri_count = kept;
    if (kept == 0) {
        return;
    }
    /* the triangles sorted into squares over the ground, a triangle into
     * every square its box touches */
    g_dress.bins_x = (s32)((hi[0] - g_dress.lo[0]) / BIN_SIZE) + 1;
    g_dress.bins_z = (s32)((hi[1] - g_dress.lo[1]) / BIN_SIZE) + 1;
    if (g_dress.bins_x > BINS_MOST) {
        g_dress.bins_x = BINS_MOST;
    }
    if (g_dress.bins_z > BINS_MOST) {
        g_dress.bins_z = BINS_MOST;
    }
    g_dress.bin_first = (s32 *)calloc(
        (size_t)(g_dress.bins_x * g_dress.bins_z + 1), sizeof(s32));
    if (g_dress.bin_first == NULL) {
        g_dress.tri_count = 0;
        return;
    }
    for (b = 0; b < 2; b++) {
        s32 filled = 0;

        for (i = 0; i < kept; i++) {
            const DressTri *t = &g_dress.tris[i];
            float x0 = t->p[0][0];
            float x1 = x0;
            float z0 = t->p[0][2];
            float z1 = z0;
            s32 bx0;
            s32 bx1;
            s32 bz0;
            s32 bz1;
            s32 bx;
            s32 bz;
            s32 k;

            for (k = 1; k < 3; k++) {
                x0 = t->p[k][0] < x0 ? t->p[k][0] : x0;
                x1 = t->p[k][0] > x1 ? t->p[k][0] : x1;
                z0 = t->p[k][2] < z0 ? t->p[k][2] : z0;
                z1 = t->p[k][2] > z1 ? t->p[k][2] : z1;
            }
            bx0 = (s32)((x0 - g_dress.lo[0]) / BIN_SIZE);
            bx1 = (s32)((x1 - g_dress.lo[0]) / BIN_SIZE);
            bz0 = (s32)((z0 - g_dress.lo[1]) / BIN_SIZE);
            bz1 = (s32)((z1 - g_dress.lo[1]) / BIN_SIZE);
            bx1 = bx1 >= g_dress.bins_x ? g_dress.bins_x - 1 : bx1;
            bz1 = bz1 >= g_dress.bins_z ? g_dress.bins_z - 1 : bz1;
            for (bz = bz0; bz <= bz1; bz++) {
                for (bx = bx0; bx <= bx1; bx++) {
                    s32 at = bz * g_dress.bins_x + bx;

                    if (b == 0) {
                        g_dress.bin_first[at + 1]++;
                    } else {
                        g_dress.bin_list[g_dress.bin_first[at]++] = i;
                    }
                }
            }
            filled++;
        }
        if (b == 0) {
            s32 total;

            for (i = 0; i < g_dress.bins_x * g_dress.bins_z; i++) {
                g_dress.bin_first[i + 1] += g_dress.bin_first[i];
            }
            total = g_dress.bin_first[g_dress.bins_x * g_dress.bins_z];
            g_dress.bin_list = (s32 *)malloc((size_t)(total + 1) * sizeof(s32));
            if (g_dress.bin_list == NULL) {
                g_dress.tri_count = 0;
                return;
            }
        }
        (void)filled;
    }
    /* the second pass moved each square's first on to its end: back */
    for (i = g_dress.bins_x * g_dress.bins_z; i > 0; i--) {
        g_dress.bin_first[i] = g_dress.bin_first[i - 1];
    }
    g_dress.bin_first[0] = 0;
}

/* The colour a triangle's texture has at a point of it (weights of its
 * corners): FALSE where the texel is clear. */
static s32 port_DressTexel(const DressTri *t, const float *weight, float *rgb) {
    const u16 *vram = g_dress.vram;
    s32 u = (s32)(weight[0] * t->uv[0][0] + weight[1] * t->uv[1][0] +
                  weight[2] * t->uv[2][0]) &
            0xFF;
    s32 v = (s32)(weight[0] * t->uv[0][1] + weight[1] * t->uv[1][1] +
                  weight[2] * t->uv[2][1]) &
            0xFF;
    s32 row = ((t->page[1] + v) & (VRAM_H - 1)) * VRAM_W;
    s32 clut = (t->clut[1] & (VRAM_H - 1)) * VRAM_W;
    u16 color;

    if (t->bits == 0) {
        s32 index = (vram[row + ((t->page[0] + u / 4) & (VRAM_W - 1))] >>
                     ((u % 4) * 4)) &
                    0xF;
        color = vram[clut + ((t->clut[0] + index) & (VRAM_W - 1))];
    } else if (t->bits == 1) {
        s32 index = (vram[row + ((t->page[0] + u / 2) & (VRAM_W - 1))] >>
                     ((u % 2) * 8)) &
                    0xFF;
        color = vram[clut + ((t->clut[0] + index) & (VRAM_W - 1))];
    } else {
        color = vram[row + ((t->page[0] + u) & (VRAM_W - 1))];
    }
    if (color == 0) {
        return FALSE;
    }
    rgb[0] = (float)(color & 31) / 31.0f;
    rgb[1] = (float)((color >> 5) & 31) / 31.0f;
    rgb[2] = (float)((color >> 10) & 31) / 31.0f;
    return TRUE;
}

/* The ground at (x, z): the highest face that looks up, where it is, how
 * level it lies, its texture's colour there, and whether it is a piece's.
 * FALSE where there is none. */
static s32 port_DressGroundAt(
    float x, float z, float *y, float *level, float *rgb, s32 *is_piece) {
    s32 bx = (s32)((x - g_dress.lo[0]) / BIN_SIZE);
    s32 bz = (s32)((z - g_dress.lo[1]) / BIN_SIZE);
    const DressTri *best = NULL;
    float best_weight[3] = {0.0f, 0.0f, 0.0f};
    float best_y = 0.0f;
    s32 at;
    s32 i;

    if (g_dress.tri_count == 0 || bx < 0 || bz < 0 || bx >= g_dress.bins_x ||
        bz >= g_dress.bins_z) {
        return FALSE;
    }
    at = bz * g_dress.bins_x + bx;
    for (i = g_dress.bin_first[at]; i < g_dress.bin_first[at + 1]; i++) {
        const DressTri *t = &g_dress.tris[g_dress.bin_list[i]];
        float ax = t->p[0][0];
        float az = t->p[0][2];
        float e1x = t->p[1][0] - ax;
        float e1z = t->p[1][2] - az;
        float e2x = t->p[2][0] - ax;
        float e2z = t->p[2][2] - az;
        float area = e1x * e2z - e1z * e2x;
        float w1;
        float w2;
        float w0;
        float py;

        if (area < 1.0f && area > -1.0f) {
            continue;
        }
        w1 = ((x - ax) * e2z - (z - az) * e2x) / area;
        w2 = (e1x * (z - az) - e1z * (x - ax)) / area;
        w0 = 1.0f - w1 - w2;
        if (w0 < 0.0f || w1 < 0.0f || w2 < 0.0f) {
            continue;
        }
        py = w0 * t->p[0][1] + w1 * t->p[1][1] + w2 * t->p[2][1];
        if (best == NULL || py < best_y) {
            best = t;
            best_y = py;
            best_weight[0] = w0;
            best_weight[1] = w1;
            best_weight[2] = w2;
        }
    }
    if (best == NULL) {
        return FALSE;
    }
    *y = best_y;
    *level = -best->normal_y;
    *is_piece = best->is_piece;
    if (!port_DressTexel(best, best_weight, rgb)) {
        rgb[0] = rgb[1] = rgb[2] = -1.0f;
    }
    return TRUE;
}

/* Whether a colour of the ground is of the kind a block grows on
 * (PORT_DRESS_ON_...). */
static s32 port_DressGroundIs(const float *rgb, s32 on) {
    float high = rgb[0];
    float low = rgb[0];
    float saturation;
    float hue = 0.0f;
    s32 k;

    if (rgb[0] < 0.0f) {
        return FALSE;
    }
    if (on == PORT_DRESS_ON_ANY) {
        return TRUE;
    }
    for (k = 1; k < 3; k++) {
        high = rgb[k] > high ? rgb[k] : high;
        low = rgb[k] < low ? rgb[k] : low;
    }
    saturation = high > 0.0f ? (high - low) / high : 0.0f;
    if (high > low) {
        if (high == rgb[0]) {
            hue = 60.0f * (rgb[1] - rgb[2]) / (high - low);
        } else if (high == rgb[1]) {
            hue = 60.0f * (2.0f + (rgb[2] - rgb[0]) / (high - low));
        } else {
            hue = 60.0f * (4.0f + (rgb[0] - rgb[1]) / (high - low));
        }
        if (hue < 0.0f) {
            hue += 360.0f;
        }
    }
    switch (on) {
    case PORT_DRESS_ON_GREEN:
        return saturation > 0.12f && hue >= 62.0f && hue <= 190.0f &&
               high > 0.08f;
    case PORT_DRESS_ON_EARTH:
        return saturation > 0.12f && (hue < 55.0f || hue > 340.0f) &&
               high < 0.75f;
    case PORT_DRESS_ON_SAND:
        return saturation <= 0.45f && hue >= 15.0f && hue <= 60.0f &&
               high >= 0.45f;
    case PORT_DRESS_ON_STONE:
        return saturation < 0.15f && high >= 0.15f && high < 0.8f;
    case PORT_DRESS_ON_SNOW:
        return saturation < 0.18f && high >= 0.7f;
    case PORT_DRESS_ON_BLUE:
        return saturation > 0.2f && hue > 190.0f && hue <= 260.0f;
    default:
        return TRUE;
    }
}

/* ---- growing ---- */

static s32 port_DressInside(const s32 (*list)[4], s32 count, float x, float z) {
    s32 i;

    for (i = 0; i < count; i++) {
        if (x >= (float)list[i][0] && x <= (float)list[i][2] &&
            z >= (float)list[i][1] && z <= (float)list[i][3]) {
            return TRUE;
        }
    }
    return FALSE;
}

/* A place of a block's areas, each as likely as its size makes it. */
static void port_DressPlace(
    const PortDressDef *def, u32 *state, float *x, float *z) {
    float total = 0.0f;
    float pick;
    s32 i;

    for (i = 0; i < def->area_count; i++) {
        total += (float)(def->area[i][2] - def->area[i][0]) *
                 (float)(def->area[i][3] - def->area[i][1]);
    }
    pick = port_DressRandom(state) * total;
    for (i = 0; i + 1 < def->area_count; i++) {
        float size = (float)(def->area[i][2] - def->area[i][0]) *
                     (float)(def->area[i][3] - def->area[i][1]);

        if (pick < size) {
            break;
        }
        pick -= size;
    }
    *x = (float)def->area[i][0] +
         port_DressRandom(state) * (float)(def->area[i][2] - def->area[i][0]);
    *z = (float)def->area[i][1] +
         port_DressRandom(state) * (float)(def->area[i][3] - def->area[i][1]);
}

static u8 port_DressByte(float value) {
    value *= CHANNEL_MOST;
    return (u8)(value < 0.0f           ? 0.0f
                : value > CHANNEL_MOST ? CHANNEL_MOST
                                       : value);
}

/* Whether the ground a tuft's width about (x, z) is of the kind `on` and
 * at about its height. */
static s32 port_DressAroundIs(float x, float z, float y, s32 on) {
    static const float about[4][2] = {
        {TUFT_REACH, 0.0f},
        {-TUFT_REACH, 0.0f},
        {0.0f, TUFT_REACH},
        {0.0f, -TUFT_REACH}};
    s32 k;
    s32 misses = 0;

    for (k = 0; k < 4; k++) {
        float there_y;
        float level;
        float rgb[3];
        s32 is_piece;

        if (!port_DressGroundAt(x + about[k][0], z + about[k][1], &there_y,
                                &level, rgb, &is_piece) ||
            is_piece || !port_DressGroundIs(rgb, on) ||
            there_y < y - TUFT_REACH || there_y > y + TUFT_REACH) {
            misses++;
        }
    }
    /* one side of four may be another ground: a texture's own speckle */
    return misses <= 1;
}

/* The foliage of a block: places on the ground of its kind. */
static void port_DressGrowFoliage(
    const PortDressDef *def, DressRun *run, s32 stage) {
    u32 state = port_DressSeedOf(def->name, stage, def->kind);
    s32 tries = def->count * TRIES_EACH;
    double ground_sum = 0.0;
    s32 t;

    run->first = g_dress.thing_count;
    run->count = 0;
    if (def->area_count == 0) {
        return;
    }
    for (t = 0; t < tries && run->count < def->count &&
                g_dress.thing_count < THINGS_MOST;
         t++) {
        float x;
        float z;
        float y;
        float level;
        float rgb[3];
        s32 is_piece;
        DressThing *thing;
        float shade;
        s32 k;

        port_DressPlace(def, &state, &x, &z);
        if (port_DressInside(def->avoid, def->avoid_count, x, z) ||
            !port_DressGroundAt(x, z, &y, &level, rgb, &is_piece) || is_piece ||
            level * PORT_DRESSING_ONE < (float)def->level ||
            !port_DressGroundIs(rgb, def->on)) {
            continue;
        }
        if (def->has_heights &&
            (y < (float)def->heights[0] || y > (float)def->heights[1])) {
            continue;
        }
        /* a tuft's blades spread about its root: the ground there must be
         * of its kind too, or it grows over a path's stones */
        if (def->kind != PORT_DRESS_BUSH && def->on != PORT_DRESS_ON_ANY &&
            !port_DressAroundIs(x, z, y, def->on)) {
            continue;
        }
        thing = &g_dress.things[g_dress.thing_count++];
        thing->root[0] = x;
        thing->root[1] = y;
        thing->root[2] = z;
        thing->root[3] = port_DressRandom(&state) * 2.0f * PI_F;
        thing->shape[0] =
            (float)def->height[0] +
            port_DressRandom(&state) * (float)(def->height[1] - def->height[0]);
        thing->shape[1] = (float)def->width / PORT_DRESSING_ONE;
        thing->shape[2] = (float)(u32)(port_DressRandom(&state) * 16777215.0f);
        thing->shape[3] = 0.7f + 0.6f * port_DressRandom(&state);
        /* its colour: the block's, or the ground's under it made a little
         * richer, as the grass is to the earth it grows from */
        shade = 0.9f + 0.2f * port_DressRandom(&state);
        for (k = 0; k < 3; k++) {
            float c = def->has_color ? (float)def->color[k] / CHANNEL_MOST
                                     : rgb[k] * 1.1f;

            c *= shade * (float)def->tint[k] / PORT_DRESSING_ONE;
            thing->color[k] = port_DressByte(c);
        }
        thing->color[3] = 255;
        ground_sum += y;
        run->count++;
    }
    run->ground_y = run->count > 0 ? (float)(ground_sum / run->count) : 0.0f;
}

/* The water of a block: a level quad of each of its rectangles. */
static void port_DressGrowWater(const PortDressDef *def, DressRun *run) {
    s32 i;

    run->first = g_dress.mesh_vertex_count;
    run->count = 0;
    run->is_mesh = TRUE;
    if (def->on != PORT_DRESS_ON_ANY) {
        /* laid on the ground's own faces of its colour in its areas: a
         * stream's bed, whatever its shape, a little above it */
        double sum = 0.0;

        for (i = 0; i < g_dress.tri_count &&
                    g_dress.mesh_vertex_count + 3 <= MESH_VERTICES_MOST;
             i++) {
            const DressTri *t = &g_dress.tris[i];
            float weight[3] = {1.0f / 3.0f, 1.0f / 3.0f, 1.0f / 3.0f};
            float middle[3];
            float rgb[3];
            s32 k;

            for (k = 0; k < 3; k++) {
                middle[k] = (t->p[0][k] + t->p[1][k] + t->p[2][k]) / 3.0f;
            }
            if (t->is_piece || -t->normal_y * PORT_DRESSING_ONE < def->level ||
                !port_DressInside(
                    def->area, def->area_count, middle[0], middle[2]) ||
                (def->has_heights && (middle[1] < (float)def->heights[0] ||
                                      middle[1] > (float)def->heights[1])) ||
                !port_DressTexel(t, weight, rgb) ||
                !port_DressGroundIs(rgb, def->on)) {
                continue;
            }
            for (k = 0; k < 3; k++) {
                DressVertex *v =
                    &g_dress.mesh_vertices[g_dress.mesh_vertex_count++];

                v->place[0] = t->p[k][0];
                v->place[1] = t->p[k][1] - (float)def->at[1];
                v->place[2] = t->p[k][2];
                v->normal[0] = 0.0f;
                v->normal[1] = -1.0f;
                v->normal[2] = 0.0f;
                memset(v->color, 255, sizeof(v->color));
            }
            sum += middle[1];
            run->count += 3;
        }
        run->ground_y = run->count > 0 ? (float)(sum / (run->count / 3)) : 0.0f;
        return;
    }
    for (i = 0; i < def->area_count &&
                g_dress.mesh_vertex_count + 6 <= MESH_VERTICES_MOST;
         i++) {
        static const s32 corners[6][2] = {
            {0, 1}, {2, 1}, {2, 3}, {0, 1}, {2, 3}, {0, 3}};
        s32 k;

        for (k = 0; k < 6; k++) {
            DressVertex *v =
                &g_dress.mesh_vertices[g_dress.mesh_vertex_count++];

            v->place[0] = (float)def->area[i][corners[k][0]];
            v->place[1] = (float)def->at[1];
            v->place[2] = (float)def->area[i][corners[k][1]];
            v->normal[0] = 0.0f;
            v->normal[1] = -1.0f;
            v->normal[2] = 0.0f;
            memset(v->color, 255, sizeof(v->color));
        }
        run->count += 6;
    }
    run->ground_y = (float)def->at[1];
}

/* The ground's height under a block's areas, for its particles: the mean of
 * a few places. */
static void port_DressGrowParticles(
    const PortDressDef *def, DressRun *run, s32 stage) {
    u32 state = port_DressSeedOf(def->name, stage, def->kind);
    double sum = 0.0;
    s32 found = 0;
    s32 t;

    run->first = 0;
    run->count = def->count;
    run->ground_y = (float)def->at[1];
    for (t = 0; t < 64 && def->area_count > 0; t++) {
        float x;
        float z;
        float y;
        float level;
        float rgb[3];
        s32 is_piece;

        port_DressPlace(def, &state, &x, &z);
        if (port_DressGroundAt(x, z, &y, &level, rgb, &is_piece)) {
            sum += y;
            found++;
        }
    }
    if (found > 0) {
        run->ground_y = (float)(sum / found);
    }
}

/* ---- a mesh of the mods' (Wavefront OBJ) ---- */

/* The most of a mesh file's own points and normals, and its size. */
#define OBJ_POINTS_MOST 30000
#define OBJ_BYTES_MOST (4 << 20)

static float port_DressNumber(const char **text) {
    const char *at = *text;
    float sign = 1.0f;
    float value = 0.0f;
    float scale = 1.0f;
    s32 exponent = 0;
    s32 exponent_sign = 1;

    while (*at == ' ' || *at == '\t') {
        at++;
    }
    if (*at == '-' || *at == '+') {
        sign = *at == '-' ? -1.0f : 1.0f;
        at++;
    }
    while (*at >= '0' && *at <= '9') {
        value = value * 10.0f + (float)(*at++ - '0');
    }
    if (*at == '.') {
        at++;
        while (*at >= '0' && *at <= '9') {
            scale /= 10.0f;
            value += (float)(*at++ - '0') * scale;
        }
    }
    if (*at == 'e' || *at == 'E') {
        at++;
        if (*at == '-' || *at == '+') {
            exponent_sign = *at == '-' ? -1 : 1;
            at++;
        }
        while (*at >= '0' && *at <= '9') {
            exponent = exponent * 10 + (*at++ - '0');
        }
        while (exponent-- > 0) {
            value = exponent_sign > 0 ? value * 10.0f : value / 10.0f;
        }
    }
    *text = at;
    return sign * value;
}

/* A mesh file's triangles into the meshes' vertices: its points (with
 * their colours where it gives them, `v x y z r g b`), its faces (a
 * polygon made a fan), in its own units, the world's y down. The box of
 * its points. How many vertices; 0 where it could not be read. */
static s32 port_DressReadObj(const char *path, float *lo, float *hi) {
    u32 size = port_ModFileSize(path);
    char *text;
    float(*points)[6];
    s32 point_count = 0;
    s32 first = g_dress.mesh_vertex_count;
    const char *at;
    s32 k;

    if (size == 0 || size > OBJ_BYTES_MOST) {
        return 0;
    }
    text = (char *)malloc(size + 1);
    points = (float(*)[6])malloc(OBJ_POINTS_MOST * sizeof(*points));
    if (text == NULL || points == NULL) {
        free(text);
        free(points);
        return 0;
    }
    size = port_ModReadFile(path, text, size);
    text[size] = 0;
    for (k = 0; k < 3; k++) {
        lo[k] = 1e30f;
        hi[k] = -1e30f;
    }
    for (at = text; *at != 0;) {
        const char *line = at;

        while (*at != 0 && *at != '\n') {
            at++;
        }
        if (line[0] == 'v' && line[1] == ' ' && point_count < OBJ_POINTS_MOST) {
            const char *p = line + 2;
            float *point = points[point_count++];

            for (k = 0; k < 6; k++) {
                point[k] = k < 3 ? port_DressNumber(&p) : -1.0f;
            }
            while (*p == ' ' || *p == '\t') {
                p++;
            }
            if (*p != '\n' && *p != '\r' && *p != 0) {
                for (k = 3; k < 6; k++) {
                    point[k] = port_DressNumber(&p);
                }
            }
            for (k = 0; k < 3; k++) {
                lo[k] = point[k] < lo[k] ? point[k] : lo[k];
                hi[k] = point[k] > hi[k] ? point[k] : hi[k];
            }
        } else if (line[0] == 'f' && line[1] == ' ') {
            const char *p = line + 2;
            s32 corner[16];
            s32 corners = 0;

            while (corners < 16) {
                s32 number;

                while (*p == ' ' || *p == '\t') {
                    p++;
                }
                if (*p == '\n' || *p == '\r' || *p == 0 || p >= at) {
                    break;
                }
                number = (s32)port_DressNumber(&p);
                while (*p != ' ' && *p != '\t' && *p != '\n' && *p != '\r' &&
                       *p != 0) {
                    p++; /* a texture's or a normal's number: not read */
                }
                corner[corners++] =
                    number < 0 ? point_count + number : number - 1;
            }
            for (k = 1; k + 1 < corners; k++) {
                s32 three[3];
                float e1[3];
                float e2[3];
                float n[3];
                float length;
                s32 c;
                s32 j;

                three[0] = corner[0];
                three[1] = corner[k];
                three[2] = corner[k + 1];
                if (g_dress.mesh_vertex_count + 3 > MESH_VERTICES_MOST) {
                    break;
                }
                for (c = 0; c < 3; c++) {
                    if (three[c] < 0 || three[c] >= point_count) {
                        three[0] = -1;
                    }
                }
                if (three[0] < 0) {
                    continue;
                }
                for (j = 0; j < 3; j++) {
                    e1[j] = points[three[1]][j] - points[three[0]][j];
                    e2[j] = points[three[2]][j] - points[three[0]][j];
                }
                n[0] = e1[1] * e2[2] - e1[2] * e2[1];
                n[1] = e1[2] * e2[0] - e1[0] * e2[2];
                n[2] = e1[0] * e2[1] - e1[1] * e2[0];
                length =
                    port_DressRoot(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]);
                for (c = 0; c < 3; c++) {
                    DressVertex *v =
                        &g_dress.mesh_vertices[g_dress.mesh_vertex_count++];
                    const float *point = points[three[c]];

                    for (j = 0; j < 3; j++) {
                        v->place[j] = point[j];
                        v->normal[j] = length > 0.0f ? n[j] / length : 0.0f;
                    }
                    for (j = 0; j < 3; j++) {
                        v->color[j] = point[3] < 0.0f
                                          ? 200
                                          : port_DressByte(point[3 + j]);
                    }
                    v->color[3] = 255;
                }
            }
        }
        if (*at == '\n') {
            at++;
        }
    }
    free(text);
    free(points);
    return g_dress.mesh_vertex_count - first;
}

/* A piece's box in the world, from the ground's faces of it that look up
 * and every other: FALSE where the mesh has none of it. */
static s32 port_DressPieceBox(s32 piece, float *lo, float *hi) {
    s32 found = FALSE;
    s32 i;
    s32 k;
    s32 c;

    for (i = 0; i < g_dress.tri_count; i++) {
        const DressTri *t = &g_dress.tris[i];

        if (!t->is_piece || t->piece != piece) {
            continue;
        }
        for (c = 0; c < 3; c++) {
            for (k = 0; k < 3; k++) {
                if (!found || t->p[c][k] < lo[k]) {
                    lo[k] = t->p[c][k];
                }
                if (!found || t->p[c][k] > hi[k]) {
                    hi[k] = t->p[c][k];
                }
            }
            found = TRUE;
        }
    }
    return found;
}

/* A mesh block: its file read, turned, scaled and set in place -- at `at`,
 * or fitted into a piece's box where it stands in for the piece. */
static void port_DressGrowMesh(const PortDressDef *def, DressRun *run) {
    const char *path = port_ModFind(PORT_MOD_DRESSING, def->file);
    float lo[3];
    float hi[3];
    float turn = (float)def->turn * PI_F / DEGREES_HALF;
    float c = port_DressCos(turn);
    float s = port_DressSin(turn);
    float scale[3];
    float offset[3];
    float turned_lo[3];
    float turned_hi[3];
    s32 count;
    s32 i;
    s32 k;

    run->first = g_dress.mesh_vertex_count;
    run->count = 0;
    run->is_mesh = TRUE;
    if (path == NULL) {
        snprintf(g_dress.news, sizeof(g_dress.news),
                 "dressing: mesh %s: no file %s.obj in the mods' dressing/",
                 def->name, def->file);
        g_dress.has_news = TRUE;
        return;
    }
    count = port_DressReadObj(path, lo, hi);
    if (count == 0) {
        return;
    }
    /* turned about the upright through its own middle */
    for (k = 0; k < 3; k++) {
        turned_lo[k] = 1e30f;
        turned_hi[k] = -1e30f;
    }
    for (i = run->first; i < run->first + count; i++) {
        DressVertex *v = &g_dress.mesh_vertices[i];
        float x = v->place[0];
        float z = v->place[2];
        float nx = v->normal[0];
        float nz = v->normal[2];

        v->place[0] = c * x - s * z;
        v->place[2] = s * x + c * z;
        v->normal[0] = c * nx - s * nz;
        v->normal[2] = s * nx + c * nz;
        for (k = 0; k < 3; k++) {
            turned_lo[k] =
                v->place[k] < turned_lo[k] ? v->place[k] : turned_lo[k];
            turned_hi[k] =
                v->place[k] > turned_hi[k] ? v->place[k] : turned_hi[k];
        }
    }
    for (k = 0; k < 3; k++) {
        scale[k] = (float)def->scale / PORT_DRESSING_ONE;
        offset[k] = (float)def->at[k];
    }
    if (def->sit) {
        /* on the ground under it: its own y is up from there */
        float y;
        float level;
        float rgb[3];
        s32 is_piece;

        if (port_DressGroundAt((float)def->at[0], (float)def->at[2], &y, &level,
                               rgb, &is_piece)) {
            offset[1] = y + (float)def->at[1];
        }
    }
    if (def->piece >= 0) {
        float box_lo[3];
        float box_hi[3];
        float worst = 0.0f;

        if (!port_DressPieceBox(def->piece, box_lo, box_hi)) {
            snprintf(g_dress.news, sizeof(g_dress.news),
                     "dressing: mesh %s: the stage's mesh has no piece %d",
                     def->name, def->piece);
            g_dress.has_news = TRUE;
            g_dress.mesh_vertex_count = run->first;
            return;
        }
        /* fitted into the piece's box: it keeps the piece's outline, and a
         * mesh whose proportions are not the box's is refused */
        for (k = 0; k < 3; k++) {
            float own = turned_hi[k] - turned_lo[k];
            float box = box_hi[k] - box_lo[k];

            scale[k] = own > 1e-3f ? box / own : 1.0f;
        }
        for (k = 0; k < 3; k++) {
            float ratio =
                scale[k] /
                (scale[(k + 1) % 3] > 1e-6f ? scale[(k + 1) % 3] : 1e-6f);
            float off = ratio > 1.0f ? ratio - 1.0f : 1.0f / ratio - 1.0f;

            worst = off > worst ? off : worst;
        }
        if (worst > 0.25f) {
            snprintf(g_dress.news, sizeof(g_dress.news),
                     "dressing: mesh %s refused: its proportions are not "
                     "piece %d's (%d%% off)",
                     def->name, def->piece, (s32)(worst * 100.0f));
            g_dress.has_news = TRUE;
            g_dress.mesh_vertex_count = run->first;
            return;
        }
        for (k = 0; k < 3; k++) {
            offset[k] = box_lo[k] - turned_lo[k] * scale[k];
        }
        g_dress.hidden[def->piece >> 5] |= 1u << (def->piece & 31);
    }
    for (i = run->first; i < run->first + count; i++) {
        DressVertex *v = &g_dress.mesh_vertices[i];
        float n[3];
        float length;

        for (k = 0; k < 3; k++) {
            v->place[k] = v->place[k] * scale[k] + offset[k];
            n[k] = v->normal[k] / scale[k];
        }
        length = port_DressRoot(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]);
        for (k = 0; k < 3; k++) {
            v->normal[k] = length > 0.0f ? n[k] / length : 0.0f;
        }
        if (def->has_color) {
            for (k = 0; k < 3; k++) {
                v->color[k] = def->color[k];
            }
        }
        for (k = 0; k < 3; k++) {
            float tinted = (float)v->color[k] * (float)def->tint[k] /
                           PORT_DRESSING_ONE / CHANNEL_MOST;

            v->color[k] = port_DressByte(tinted);
        }
    }
    run->count = count;
    run->ground_y = offset[1];
}

/* Everything of the stage's set made into things and sent to the card. */
static void port_DressGrow(const PortDressSet *set, s32 stage) {
    s32 i;
    s32 counts[PORT_DRESS_KINDS];

    g_dress.grown_serial = g_dress.ground_serial;
    g_dress.grown_stage = stage;
    g_dress.grown_set = set;
    g_dress.run_count = 0;
    g_dress.thing_count = 0;
    g_dress.mesh_vertex_count = 0;
    g_dress.hidden[0] = g_dress.hidden[1] = 0;
    g_dress.wind[0] = 0.87f;
    g_dress.wind[1] = 0.5f;
    g_dress.wind[2] = 0.5f;
    g_dress.wind[3] = 0.33f;
    memset(counts, 0, sizeof(counts));
    if (g_dress.things == NULL) {
        g_dress.things = (DressThing *)malloc(THINGS_MOST * sizeof(DressThing));
    }
    if (g_dress.mesh_vertices == NULL) {
        g_dress.mesh_vertices =
            (DressVertex *)malloc(MESH_VERTICES_MOST * sizeof(DressVertex));
    }
    if (set == NULL || g_dress.things == NULL ||
        g_dress.mesh_vertices == NULL) {
        return;
    }
    for (i = 0; i < set->count; i++) {
        const PortDressDef *def = &set->block[i];
        DressRun *run = &g_dress.runs[g_dress.run_count];

        memset(run, 0, sizeof(*run));
        run->block = i;
        run->kind = def->kind;
        switch (def->kind) {
        case PORT_DRESS_GRASS:
        case PORT_DRESS_BUSH:
        case PORT_DRESS_REEDS:
        case PORT_DRESS_FLOWERS:
            port_DressGrowFoliage(def, run, stage);
            break;
        case PORT_DRESS_WATER:
            port_DressGrowWater(def, run);
            break;
        case PORT_DRESS_PARTICLES:
            port_DressGrowParticles(def, run, stage);
            break;
        case PORT_DRESS_MESH:
            port_DressGrowMesh(def, run);
            break;
        case PORT_DRESS_WIND: {
            float way = (float)def->direction * PI_F / DEGREES_HALF;

            g_dress.wind[0] = port_DressCos(way);
            g_dress.wind[1] = port_DressSin(way);
            g_dress.wind[2] = (float)def->strength / PORT_DRESSING_ONE;
            g_dress.wind[3] = (float)def->gusts / PORT_DRESSING_ONE;
            continue;
        }
        default:
            continue;
        }
        counts[def->kind] += run->count;
        g_dress.run_count++;
    }
    glBindBuffer(GL_ARRAY_BUFFER, g_dress.thing_buffer);
    glBufferData(
        GL_ARRAY_BUFFER,
        (GLsizeiptr)((size_t)(g_dress.thing_count > 0 ? g_dress.thing_count
                                                      : 1) *
                     sizeof(DressThing)),
        g_dress.things, GL_STATIC_DRAW);
    glBindBuffer(GL_ARRAY_BUFFER, g_dress.mesh_buffer);
    glBufferData(GL_ARRAY_BUFFER,
                 (GLsizeiptr)((size_t)(g_dress.mesh_vertex_count > 0
                                           ? g_dress.mesh_vertex_count
                                           : 1) *
                              sizeof(DressVertex)),
                 g_dress.mesh_vertices, GL_STATIC_DRAW);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    snprintf(
        g_dress.news, sizeof(g_dress.news),
        "dressing: stage %d grown from %d blocks: %d tufts of grass, %d "
        "bushes, %d reeds, %d flowers, %d vertices of water and meshes, "
        "%d particles",
        stage, set->count, counts[PORT_DRESS_GRASS], counts[PORT_DRESS_BUSH],
        counts[PORT_DRESS_REEDS], counts[PORT_DRESS_FLOWERS],
        g_dress.mesh_vertex_count, counts[PORT_DRESS_PARTICLES]);
    g_dress.has_news = TRUE;
}

/* The stage's set, grown if it has not been for this ground: NULL where
 * nothing is to be drawn. */
static const PortDressSet *port_DressNow(void) {
    s32 stage = port_RemasterStageNumber();
    const PortDressSet *set;

    if (!g_dress.is_started || !g_dress.is_on || g_dress.tri_count == 0) {
        return NULL;
    }
    set = port_DressingOfStage(stage);
    if (set == NULL || set->count == 0) {
        return NULL;
    }
    if (g_dress.grown_serial != g_dress.ground_serial ||
        g_dress.grown_stage != stage || g_dress.grown_set != set) {
        port_DressGrow(set, stage);
    }
    return set;
}

void port_GlDressingHiddenPieces(u32 *words, s32 count) {
    s32 i;

    for (i = 0; i < count; i++) {
        words[i] = 0;
    }
    if (port_DressNow() == NULL) {
        return;
    }
    for (i = 0; i < count && i < 2; i++) {
        words[i] = g_dress.hidden[i];
    }
}

/* ---- the frame: what pushes, the dust, the ripples ---- */

static void port_DressAddDust(
    float x, float y, float z, const float *way, const u8 *rgb, float clock) {
    DressDust *d = &g_dress.dust[g_dress.dust_next];

    d->at[0] = x;
    d->at[1] = y;
    d->at[2] = z;
    d->at[3] = clock;
    d->way[0] = way[0];
    d->way[1] = way[1];
    d->way[2] = way[2];
    d->way[3] = 30.0f + 30.0f * port_DressRandom(&g_dress.seed);
    d->color[0] = rgb[0];
    d->color[1] = rgb[1];
    d->color[2] = rgb[2];
    d->color[3] =
        (u8)(12 + 10 * port_DressRandom(&g_dress.seed)); /* 1.2 to 2.2 s */
    g_dress.dust_next = (g_dress.dust_next + 1) % DUST_MOST;
    if (g_dress.dust_used < DUST_MOST) {
        g_dress.dust_used++;
    }
}

/* The particles block of the stage that is dust, or NULL. */
static const PortDressDef *port_DressDust(const PortDressSet *set) {
    s32 i;

    for (i = 0; i < set->count; i++) {
        if (set->block[i].kind == PORT_DRESS_PARTICLES &&
            set->block[i].subkind == PORT_PARTICLES_DUST) {
            return &set->block[i];
        }
    }
    return NULL;
}

/* Whether a place is in the water, and its height. */
static s32 port_DressInWater(
    const PortDressSet *set, float x, float y, float z, float *water_y) {
    s32 i;

    for (i = 0; i < set->count; i++) {
        const PortDressDef *def = &set->block[i];

        if (def->kind == PORT_DRESS_WATER &&
            port_DressInside(def->area, def->area_count, x, z) &&
            y > (float)def->at[1] - 300.0f && y < (float)def->at[1] + 900.0f) {
            *water_y = (float)def->at[1];
            return TRUE;
        }
    }
    return FALSE;
}

static float port_DressClock(void) {
    return port_GlLightsClock() / FRAMES_A_SECOND;
}

void port_GlDressingFrame(void) {
    const PortDressSet *set = port_DressNow();
    PortSeeFighter now[PORT_SEE_FIGHTERS];
    s32 count;
    s32 i;
    s32 h;
    float clock = port_DressClock();
    const PortDressDef *dust;

    g_dress.pusher_count = 0;
    g_dress.slash_count = 0;
    if (set == NULL) {
        g_dress.fighter_counts[0] = 0;
        return;
    }
    g_dress.frames++;
    dust = port_DressDust(set);
    count = port_GlSeeFightersAlways(now);
    /* each fighter: where he is, and where he was (the grass rises again
     * behind him); his step's dust and ripples, by how far he moved */
    for (i = 0; i < count; i++) {
        float dx = 0.0f;
        float dz = 0.0f;
        float speed = 0.0f;
        float feet = now[i].bottom;
        float water_y;

        if (i < g_dress.fighter_counts[0]) {
            dx = now[i].x - g_dress.fighters[0][i].x;
            dz = now[i].z - g_dress.fighters[0][i].z;
            speed = port_DressRoot(dx * dx + dz * dz);
        }
        if (speed > 400.0f) {
            speed = 0.0f; /* a cut: the fighters placed again */
        }
        if (dust != NULL && speed > STEP_SPEED &&
            port_DressRandom(&g_dress.seed) < 0.12f + speed * 0.01f) {
            float rgb[3];
            float y;
            float level;
            s32 is_piece;
            u8 color[3];
            float way[3];
            s32 k;
            s32 puffs = 2 + (s32)(speed / 15.0f);

            if (port_DressGroundAt(
                    now[i].x, now[i].z, &y, &level, rgb, &is_piece) &&
                port_DressGroundIs(rgb, dust->on) &&
                (dust->area_count == 0 ||
                 port_DressInside(
                     dust->area, dust->area_count, now[i].x, now[i].z))) {
                for (k = 0; k < 3; k++) {
                    float c = dust->has_color
                                  ? (float)dust->color[k] / CHANNEL_MOST
                                  : (rgb[0] < 0.0f ? 0.5f : rgb[k] * 1.15f);

                    color[k] = port_DressByte(c);
                }
                for (k = 0; k < puffs && k < 6; k++) {
                    float a = port_DressRandom(&g_dress.seed) * 2.0f * PI_F;
                    float out =
                        60.0f + 120.0f * port_DressRandom(&g_dress.seed);

                    way[0] = port_DressCos(a) * out - dx * 6.0f;
                    way[1] = -40.0f - 90.0f * port_DressRandom(&g_dress.seed);
                    way[2] = port_DressSin(a) * out - dz * 6.0f;
                    port_DressAddDust(
                        now[i].x + port_DressCos(a) * 120.0f, y - 20.0f,
                        now[i].z + port_DressSin(a) * 120.0f, way, color,
                        clock);
                }
            }
        }
        if (port_DressInWater(set, now[i].x, feet, now[i].z, &water_y) &&
            (speed > STEP_SPEED ? g_dress.frames % 9 == 0
                                : g_dress.frames % 50 == 0)) {
            float *r = g_dress.ripples[g_dress.ripple_next];

            r[0] = now[i].x;
            r[1] = now[i].z;
            r[2] = clock;
            r[3] = speed > STEP_SPEED ? 1.0f : 0.4f;
            g_dress.ripple_next = (g_dress.ripple_next + 1) % RIPPLES_MOST;
        }
    }
    /* history: every few frames the oldest goes */
    if (g_dress.frames % PUSH_HISTORY_FRAMES == 0) {
        for (h = PUSH_HISTORY - 1; h > 0; h--) {
            memcpy(g_dress.fighters[h], g_dress.fighters[h - 1],
                   sizeof(g_dress.fighters[h]));
            g_dress.fighter_counts[h] = g_dress.fighter_counts[h - 1];
        }
    }
    memcpy(g_dress.fighters[0], now, sizeof(now));
    g_dress.fighter_counts[0] = count;
    for (h = 0; h < PUSH_HISTORY; h++) {
        for (i = 0; i < g_dress.fighter_counts[h] &&
                    g_dress.pusher_count < PUSHERS_MOST - SLASHES_MOST;
             i++) {
            const PortSeeFighter *f = &g_dress.fighters[h][i];
            float *p = g_dress.pushers[g_dress.pusher_count];
            float *w = g_dress.push_ways[g_dress.pusher_count];

            p[0] = f->x;
            p[1] = f->bottom;
            p[2] = f->z;
            p[3] = f->radius * FEET_REACH * (1.0f + 0.25f * (float)h);
            w[0] = 0.0f;
            w[1] = 0.0f;
            w[2] = 1.0f - 0.3f * (float)h;
            w[3] = 0.0f;
            g_dress.pusher_count++;
        }
    }
    /* the slashes: a weapon's hand that moves fast near the ground sweeps
     * the grass its way */
    {
        s32 weapons = 0;
        const PortFighterWeapon *weapon = port_GetFighterWeapons(&weapons);
        u32 serial = port_FighterSerial();
        s32 n = 0;

        for (i = 0; weapon != NULL && i < weapons && i < PORT_FIGHTER_WEAPONS;
             i++) {
            float at[3];
            float way[3];
            float speed;
            float lowest = -1e30f;
            s32 f;
            s32 k;

            if (serial - weapon[i].serial > 1) {
                continue;
            }
            for (k = 0; k < 3; k++) {
                at[k] = (float)weapon[i].world.t[k];
                way[k] = i < g_dress.weapon_count
                             ? at[k] - g_dress.weapon_at[i][k]
                             : 0.0f;
                g_dress.weapon_at[i][k] = at[k];
            }
            speed = port_DressRoot(way[0] * way[0] + way[2] * way[2]);
            for (f = 0; f < count; f++) {
                lowest = now[f].bottom > lowest ? now[f].bottom : lowest;
            }
            if (speed < SLASH_SPEED || speed > 2000.0f ||
                at[1] < lowest - PUSH_HEIGHT - 400.0f ||
                g_dress.pusher_count >= PUSHERS_MOST) {
                continue;
            }
            {
                float *p = g_dress.pushers[g_dress.pusher_count];
                float *w = g_dress.push_ways[g_dress.pusher_count];

                p[0] = at[0] + way[0] * 6.0f;
                p[1] = lowest;
                p[2] = at[2] + way[2] * 6.0f;
                p[3] = 900.0f;
                w[0] = way[0] / speed;
                w[1] = way[2] / speed;
                w[2] = speed > 120.0f ? 1.0f : speed / 120.0f;
                w[3] = 1.0f;
                g_dress.pusher_count++;
                n++;
            }
        }
        g_dress.weapon_count =
            weapons < PORT_FIGHTER_WEAPONS ? weapons : PORT_FIGHTER_WEAPONS;
        g_dress.slash_count = n;
    }
}

/* ---- drawing ---- */

/* Whether a block is shown at the time of day chosen (its `times`). */
static s32 port_DressShownNow(const PortDressDef *def) {
    return def->times == 0 || port_GlTimeLightGain(def->times) > 0.0f;
}

static void port_DressUniforms(
    const DressAt *at, const PortGlDressingView *view) {
    const void *setup = port_GlTimeTileLights();
    const PortGlTime *look = port_GlTime();
    GLfloat light[9];
    GLfloat color[9];
    GLfloat back[3];
    GLfloat fog[4];

    glUniformMatrix3fv(at->view, 1, GL_FALSE, view->view);
    glUniform3f(at->camera, view->camera[0], view->camera[1], view->camera[2]);
    glUniform3f(at->eye, view->camera[0], view->camera[1], view->camera[2]);
    glUniform1f(at->h, view->h);
    glUniform2f(at->screen, view->screen[0], view->screen[1]);
    glUniform2f(at->target, view->target[0], view->target[1]);
    glUniform2f(at->scale, view->scale[0], view->scale[1]);
    glUniform1f(at->near_plane, view->near_plane);
    glUniform1f(at->depth, view->depth);
    glUniform1i(at->depth_shift, view->depth_shift);
    glUniform1i(at->fog_levels, view->fog_levels);
    glUniform1iv(at->fog_slot, 32, view->fog_slot);
    /* the fog: the time's, or the game's own towards its black distance */
    fog[0] = look->on ? look->fog[0] : 0.0f;
    fog[1] = look->on ? look->fog[1] : 0.0f;
    fog[2] = look->on ? look->fog[2] : 0.0f;
    fog[3] = look->on ? look->fog_amount : 1.0f;
    glUniform4fv(at->fog_color, 1, fog);
    if (setup != NULL) {
        port_GlTimeSetupFloats(setup, light, color, back);
        glUniformMatrix3fv(at->light, 1, GL_TRUE, light);
        glUniformMatrix3fv(at->color, 1, GL_TRUE, color);
        glUniform3f(at->back, back[0], back[1], back[2]);
    }
    glUniform1i(at->tone_on, port_GlPostToneIsOn());
    glUniform1f(at->clock, port_DressClock());
    glUniform4f(at->wind, g_dress.wind[0], g_dress.wind[1], g_dress.wind[2],
                g_dress.wind[3]);
    glUniform3f(
        at->right, view->rotation[0], view->rotation[1], view->rotation[2]);
    glUniform3f(
        at->down, view->rotation[3], view->rotation[4], view->rotation[5]);
    port_GlSunBind(&at->sun);
    port_GlLightsBind(&at->lights);
}

static void port_DressThingFormat(s32 first) {
    const DressThing *one = (const DressThing *)NULL + first;

    glBindVertexArray(g_dress.thing_array);
    glBindBuffer(GL_ARRAY_BUFFER, g_dress.thing_buffer);
    glVertexAttribPointer(
        0, 4, GL_FLOAT, GL_FALSE, (GLsizei)sizeof(DressThing), one->root);
    glVertexAttribPointer(
        1, 4, GL_FLOAT, GL_FALSE, (GLsizei)sizeof(DressThing), one->shape);
    glVertexAttribPointer(2, 4, GL_UNSIGNED_BYTE, GL_TRUE,
                          (GLsizei)sizeof(DressThing), one->color);
}

void port_GlDressingDrawSolid(const PortGlDressingView *view) {
    const PortDressSet *set = port_DressNow();
    s32 i;

    g_dress.drawn_things = 0;
    g_dress.triangles = 0;
    g_dress.draws = 0;
    if (set == NULL) {
        return;
    }
    PORT_GPU_BEGIN(PORT_GPU_DRESS);
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LESS);
    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);
    glDisable(GL_CULL_FACE);
    /* the foliage */
    glUseProgram(g_dress.foliage);
    port_DressUniforms(&g_dress.foliage_at, view);
    glUniform4fv(
        g_dress.foliage_at.push, g_dress.pusher_count, g_dress.pushers[0]);
    glUniform4fv(g_dress.foliage_at.push_way, g_dress.pusher_count,
                 g_dress.push_ways[0]);
    glUniform1i(g_dress.foliage_at.push_count, g_dress.pusher_count);
    glUniform1f(g_dress.foliage_at.thin, FOLIAGE_THIN);
    for (i = 0; i < g_dress.run_count; i++) {
        const DressRun *run = &g_dress.runs[i];
        const PortDressDef *def = &set->block[run->block];
        s32 kind;
        s32 vertices;

        if (run->is_mesh || run->count == 0 || !port_DressShownNow(def)) {
            continue;
        }
        switch (run->kind) {
        case PORT_DRESS_GRASS:
            kind = 0;
            vertices = GRASS_VERTICES;
            break;
        case PORT_DRESS_BUSH:
            kind = 1;
            vertices = BUSH_VERTICES;
            break;
        case PORT_DRESS_REEDS:
            kind = 2;
            vertices = REEDS_VERTICES;
            break;
        case PORT_DRESS_FLOWERS:
            kind = 3;
            vertices = FLOWER_VERTICES;
            break;
        default:
            continue;
        }
        port_DressThingFormat(run->first);
        glUniform1i(g_dress.foliage_at.kind, kind);
        glUniform1f(
            g_dress.foliage_at.sway, (float)def->sway / PORT_DRESSING_ONE);
        glUniform3f(
            g_dress.foliage_at.bloom,
            def->has_color ? (float)def->color[0] / CHANNEL_MOST : 0.92f,
            def->has_color ? (float)def->color[1] / CHANNEL_MOST : 0.62f,
            def->has_color ? (float)def->color[2] / CHANNEL_MOST : 0.78f);
        glDrawArraysInstanced(GL_TRIANGLES, 0, vertices, run->count);
        g_dress.drawn_things += (u32)run->count;
        g_dress.triangles += (u32)(run->count * vertices / 3);
        g_dress.draws++;
    }
    /* the meshes */
    glUseProgram(g_dress.mesh);
    port_DressUniforms(&g_dress.mesh_at, view);
    glBindVertexArray(g_dress.mesh_array);
    for (i = 0; i < g_dress.run_count; i++) {
        const DressRun *run = &g_dress.runs[i];
        const PortDressDef *def = &set->block[run->block];

        if (run->kind != PORT_DRESS_MESH || run->count == 0 ||
            !port_DressShownNow(def)) {
            continue;
        }
        glDrawArrays(GL_TRIANGLES, run->first, run->count);
        g_dress.drawn_things++;
        g_dress.triangles += (u32)(run->count / 3);
        g_dress.draws++;
    }
    glBindVertexArray(0);
    port_GlCountDraw(g_dress.triangles * 3, g_dress.draws);
    PORT_GPU_END();
}

u32 port_GlDressingDrawSunCasters(const float *matrix) {
    const PortDressSet *set = port_DressNow();
    u32 triangles = 0;
    s32 i;

    if (set == NULL || g_dress.mesh_vertex_count == 0) {
        return 0;
    }
    glUseProgram(g_dress.caster);
    glUniformMatrix4fv(g_dress.caster_at_sun, 1, GL_FALSE, matrix);
    glBindVertexArray(g_dress.mesh_array);
    for (i = 0; i < g_dress.run_count; i++) {
        const DressRun *run = &g_dress.runs[i];

        if (run->kind != PORT_DRESS_MESH || run->count == 0 ||
            !port_DressShownNow(&set->block[run->block])) {
            continue;
        }
        glDrawArrays(GL_TRIANGLES, run->first, run->count);
        triangles += (u32)(run->count / 3);
    }
    glBindVertexArray(0);
    return triangles;
}

void port_GlDressingDrawMixed(const PortGlDressingView *view) {
    const PortDressSet *set = port_DressNow();
    const PortGlTime *look;
    s32 i;

    if (set == NULL) {
        return;
    }
    look = port_GlTime();
    PORT_GPU_BEGIN(PORT_GPU_DRESS);
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LESS);
    glDepthMask(GL_FALSE);
    glEnable(GL_BLEND);
    glBlendEquation(GL_FUNC_ADD);
    glDisable(GL_CULL_FACE);
    /* the water: mixed over what is under it, its alpha left as the ground
     * wrote it (gl_post.h) */
    glUseProgram(g_dress.water);
    port_DressUniforms(&g_dress.water_at, view);
    glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ZERO, GL_ONE);
    if (look->on) {
        glUniform3f(g_dress.water_at.sky_low, look->horizon[0],
                    look->horizon[1], look->horizon[2]);
        glUniform3f(g_dress.water_at.sky_high, look->zenith[0], look->zenith[1],
                    look->zenith[2]);
    } else {
        glUniform3f(g_dress.water_at.sky_low, NIGHT_HORIZON_R, NIGHT_HORIZON_G,
                    NIGHT_HORIZON_B);
        glUniform3f(g_dress.water_at.sky_high, NIGHT_ZENITH_R, NIGHT_ZENITH_G,
                    NIGHT_ZENITH_B);
    }
    glUniform4fv(g_dress.water_at.ripples, RIPPLES_MOST, g_dress.ripples[0]);
    glUniform1i(g_dress.water_at.ripple_count, RIPPLES_MOST);
    glBindVertexArray(g_dress.mesh_array);
    for (i = 0; i < g_dress.run_count; i++) {
        const DressRun *run = &g_dress.runs[i];
        const PortDressDef *def = &set->block[run->block];

        if (run->kind != PORT_DRESS_WATER || run->count == 0 ||
            !port_DressShownNow(def)) {
            continue;
        }
        glUniform3f(
            g_dress.water_at.water,
            def->has_color ? (float)def->color[0] / CHANNEL_MOST : 0.10f,
            def->has_color ? (float)def->color[1] / CHANNEL_MOST : 0.22f,
            def->has_color ? (float)def->color[2] / CHANNEL_MOST : 0.28f);
        glUniform1f(
            g_dress.water_at.clarity, (float)def->clarity / PORT_DRESSING_ONE);
        glUniform1f(
            g_dress.water_at.waves, (float)def->waves / PORT_DRESSING_ONE);
        glDrawArrays(GL_TRIANGLES, run->first, run->count);
        g_dress.triangles += (u32)(run->count / 3);
        g_dress.draws++;
    }
    /* the particles */
    glUseProgram(g_dress.particles);
    port_DressUniforms(&g_dress.particles_at, view);
    {
        const PortLightsGame *game = port_LightsGame();
        GLfloat flames[4 * FLAMES_MOST];
        s32 flame_count = 0;

        memset(flames, 0, sizeof(flames));
        if (game != NULL && game->flame_stage == port_RemasterStageNumber()) {
            for (i = 0; i < game->flame_count && i < FLAMES_MOST; i++) {
                flames[4 * i] = (float)game->flame[i].at[0];
                flames[4 * i + 1] = (float)game->flame[i].at[1] - 200.0f;
                flames[4 * i + 2] = (float)game->flame[i].at[2];
                flame_count++;
            }
        }
        glUniform4fv(g_dress.particles_at.flames, FLAMES_MOST, flames);
        glUniform1i(g_dress.particles_at.flame_count, flame_count);
    }
    for (i = 0; i < g_dress.run_count; i++) {
        const DressRun *run = &g_dress.runs[i];
        const PortDressDef *def = &set->block[run->block];
        s32 sub = def->subkind;
        s32 is_glow =
            sub == PORT_PARTICLES_EMBERS || sub == PORT_PARTICLES_FIREFLIES;

        if (run->kind != PORT_DRESS_PARTICLES || !port_DressShownNow(def)) {
            continue;
        }
        if (is_glow) {
            glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE, GL_ZERO, GL_ONE);
        } else {
            glBlendFuncSeparate(
                GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ZERO, GL_ONE);
        }
        glUniform1i(g_dress.particles_at.kind, sub);
        glUniform1f(g_dress.particles_at.size, (float)def->height[0]);
        glUniform3f(
            g_dress.particles_at.tint,
            def->has_color ? (float)def->color[0] / CHANNEL_MOST : 0.8f,
            def->has_color ? (float)def->color[1] / CHANNEL_MOST : 0.7f,
            def->has_color ? (float)def->color[2] / CHANNEL_MOST : 0.6f);
        glUniform1f(g_dress.particles_at.ground, run->ground_y);
        glUniform1f(g_dress.particles_at.top, (float)def->top);
        if (sub == PORT_PARTICLES_DUST) {
            if (g_dress.dust_used == 0) {
                continue;
            }
            glBindVertexArray(g_dress.dust_array);
            glBindBuffer(GL_ARRAY_BUFFER, g_dress.dust_buffer);
            glBufferSubData(
                GL_ARRAY_BUFFER, 0,
                (GLsizeiptr)((size_t)g_dress.dust_used * sizeof(DressDust)),
                g_dress.dust);
            glDrawArraysInstanced(GL_TRIANGLES, 0, 6, g_dress.dust_used);
            g_dress.triangles += (u32)(g_dress.dust_used * 2);
        } else {
            if (def->area_count == 0 && sub != PORT_PARTICLES_EMBERS) {
                continue;
            }
            glUniform4f(g_dress.particles_at.area,
                        def->area_count > 0 ? (float)def->area[0][0] : 0.0f,
                        def->area_count > 0 ? (float)def->area[0][1] : 0.0f,
                        def->area_count > 0 ? (float)def->area[0][2] : 0.0f,
                        def->area_count > 0 ? (float)def->area[0][3] : 0.0f);
            glBindVertexArray(g_dress.empty_array);
            glDrawArraysInstanced(GL_TRIANGLES, 0, 6, run->count);
            g_dress.triangles += (u32)(run->count * 2);
        }
        g_dress.draws++;
    }
    glBindVertexArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glDisable(GL_BLEND);
    glDisable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);
    PORT_GPU_END();
}
