/* The lights of a frame beside the stage's own (gl_lights.h; docs/PORT.md,
 * "The lighting overhaul"): the list as it is filled, the snapshot the
 * picture takes of it, the grid it is sorted into, the shadows of the
 * strongest few, and what the shaders are given. Nothing here is the
 * game's state or changes a value the game computes. */
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#include "common.h"
#include "gl.h"
#include "gl_bench.h"
#include "gl_lights.h"
#include "gl_see.h"
#include "gl_shadow.h"
#include "gl_stage.h"
#include "gl_tween.h"

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
typedef ptrdiff_t GLintptr;

#define GL_FALSE 0
#define GL_TRUE 1
#define GL_NONE 0
#define GL_TEXTURE0 0x84C0
#define GL_TEXTURE_2D_ARRAY 0x8C1A
#define GL_DEPTH_COMPONENT 0x1902
#define GL_DEPTH_COMPONENT32F 0x8CAC
#define GL_FLOAT 0x1406
#define GL_TEXTURE_MIN_FILTER 0x2801
#define GL_TEXTURE_MAG_FILTER 0x2800
#define GL_TEXTURE_WRAP_S 0x2802
#define GL_TEXTURE_WRAP_T 0x2803
#define GL_LINEAR 0x2601
#define GL_CLAMP_TO_EDGE 0x812F
#define GL_NEAREST_L 0x2600
#define GL_TEXTURE_COMPARE_MODE 0x884C
#define GL_TEXTURE_COMPARE_FUNC 0x884D
#define GL_COMPARE_REF_TO_TEXTURE 0x884E
#define GL_LEQUAL 0x0203
#define GL_LESS 0x0201
#define GL_FRAMEBUFFER 0x8D40
#define GL_DEPTH_ATTACHMENT 0x8D00
#define GL_FRAMEBUFFER_COMPLETE 0x8CD5
#define GL_DEPTH_BUFFER_BIT 0x00000100
#define GL_DEPTH_TEST 0x0B71
#define GL_SCISSOR_TEST 0x0C11
#define GL_BLEND 0x0BE2
#define GL_CULL_FACE 0x0B44
#define GL_POLYGON_OFFSET_FILL 0x8037
#define GL_SHADER_STORAGE_BUFFER 0x90D2
#define GL_DYNAMIC_DRAW 0x88E8

#define PORT_GL_LIGHTS_FUNCTIONS                                               \
    GL(void, glGenTextures, (GLsizei, GLuint *))                               \
    GL(void, glBindTexture, (GLenum, GLuint))                                  \
    GL(void, glActiveTexture, (GLenum))                                        \
    GL(void, glTexImage3D,                                                     \
       (GLenum, GLint, GLint, GLsizei, GLsizei, GLsizei, GLint, GLenum,        \
        GLenum, const void *))                                                 \
    GL(void, glTexParameteri, (GLenum, GLenum, GLint))                         \
    GL(void, glGenFramebuffers, (GLsizei, GLuint *))                           \
    GL(void, glBindFramebuffer, (GLenum, GLuint))                              \
    GL(void, glFramebufferTextureLayer,                                        \
       (GLenum, GLenum, GLuint, GLint, GLint))                                 \
    GL(GLenum, glCheckFramebufferStatus, (GLenum))                             \
    GL(void, glDrawBuffer, (GLenum))                                           \
    GL(void, glReadBuffer, (GLenum))                                           \
    GL(void, glViewport, (GLint, GLint, GLsizei, GLsizei))                     \
    GL(void, glEnable, (GLenum))                                               \
    GL(void, glDisable, (GLenum))                                              \
    GL(void, glClear, (GLbitfield))                                            \
    GL(void, glClearDepth, (GLclampd))                                         \
    GL(void, glDepthMask, (GLboolean))                                         \
    GL(void, glDepthFunc, (GLenum))                                            \
    GL(void, glPolygonOffset, (GLfloat, GLfloat))                              \
    GL(void, glColorMask, (GLboolean, GLboolean, GLboolean, GLboolean))        \
    GL(void, glGenBuffers, (GLsizei, GLuint *))                                \
    GL(void, glBindBuffer, (GLenum, GLuint))                                   \
    GL(void, glBufferData, (GLenum, GLsizeiptr, const void *, GLenum))         \
    GL(void, glBindBufferBase, (GLenum, GLuint, GLuint))                       \
    GL(GLint, glGetUniformLocation, (GLuint, const GLchar *))                  \
    GL(void, glUniform1i, (GLint, GLint))                                      \
    GL(void, glCopyImageSubData,                                               \
       (GLuint, GLenum, GLint, GLint, GLint, GLint, GLuint, GLenum, GLint,     \
        GLint, GLint, GLint, GLsizei, GLsizei, GLsizei))

#define GL(result, name, args) static result(*name) args;
PORT_GL_LIGHTS_FUNCTIONS
#undef GL

/* The side of a light's shadow map, in texels. */
#define LIGHT_MAP_SIZE 512
/* Six faces a light. */
#define LIGHT_FACES 6
#define LIGHT_LAYERS (PORT_GL_LIGHTS_SHADOWED * LIGHT_FACES)
/* The nearest a light's shadow map sees, in world units. */
#define LIGHT_NEAR 8.0f
/* Of the grid's cells, how many lights one may name at most in all. */
#define LIST_MAX (PORT_GL_LIGHTS_CELLS * PORT_GL_LIGHTS_CELLS * 32)
/* A byte of light for an intensity of 1 (gl_lights.h). */
#define BYTES_ONE 128.0f
#define PI_F 3.14159265f

/* A light as the shaders read it (PortLight, std430: four vec4s). */
typedef struct {
    float place[4];
    float color[4];
    float aim[4];
    float extra[4];
} CardLight;

static struct {
    /* the producer's side */
    PortGlLight building[PORT_GL_LIGHTS_MAX];
    s32 building_count;
    /* handed over (End), under the lock */
    volatile s32 lock;
    PortGlLight handed[PORT_GL_LIGHTS_MAX];
    s32 handed_count;
    u32 handed_serial;
    /* the picture's (Take) */
    PortGlLight now[PORT_GL_LIGHTS_MAX];
    s32 now_count;
    PortGlLight before[PORT_GL_LIGHTS_MAX];
    s32 before_count;
    u32 taken_serial;
    u32 frames; /* frames taken, the flicker's clock */
    /* the renderer's */
    s32 is_started;
    s32 is_on;
    s32 made_up;
    s32 phase;
    s32 is_prepared;
    GLuint light_buffer;
    GLuint grid_buffer;
    GLuint list_buffer;
    GLuint face_buffer;
    GLuint map;
    GLuint framebuffer[LIGHT_LAYERS];
    /* The stage's own depth from each shadowed light, kept: the stage
     * stands still, so a light that has not moved has the same, and only
     * the fighters are drawn over a copy of it each picture. What it was
     * drawn for: the light's place and reach, and the stage's mesh. */
    GLuint stage_map;
    GLuint stage_framebuffer[LIGHT_LAYERS];
    float kept_place[PORT_GL_LIGHTS_SHADOWED][4];
    s32 kept_mesh[PORT_GL_LIGHTS_SHADOWED];
    u32 stage_redrawn;
    CardLight card[PORT_GL_LIGHTS_MAX];
    s32 card_count;
    s32 most; /* the most lights a picture of the run had */
    s32 shadowed;
    float face[LIGHT_LAYERS][16];
    float grid_head[4];
    u32 cell[PORT_GL_LIGHTS_CELLS * PORT_GL_LIGHTS_CELLS * 2];
    u32 list[LIST_MAX];
} g_lights;

/* ---- the producer's side ---- */

static void port_LightsLock(void) {
    while (__atomic_test_and_set(&g_lights.lock, __ATOMIC_ACQUIRE)) {
    }
}

static void port_LightsUnlock(void) {
    __atomic_clear(&g_lights.lock, __ATOMIC_RELEASE);
}

void port_GlLightsBegin(void) { g_lights.building_count = 0; }

s32 port_GlLightsAdd(const PortGlLight *light) {
    s32 at = g_lights.building_count;

    if (light == NULL || at >= PORT_GL_LIGHTS_MAX || !(light->radius > 0.0f) ||
        !(light->intensity > 0.0f) ||
        (light->kind != PORT_GL_LIGHT_POINT &&
         light->kind != PORT_GL_LIGHT_SPOT)) {
        return -1;
    }
    g_lights.building[at] = *light;
    g_lights.building_count = at + 1;
    return at;
}

void port_GlLightsEnd(void) {
    port_LightsLock();
    memcpy(g_lights.handed, g_lights.building,
           (size_t)g_lights.building_count * sizeof(PortGlLight));
    g_lights.handed_count = g_lights.building_count;
    g_lights.handed_serial++;
    port_LightsUnlock();
}

/* ---- the picture's side ---- */

void port_GlLightsTake(void) {
    port_LightsLock();
    if (g_lights.handed_serial != g_lights.taken_serial) {
        memcpy(g_lights.before, g_lights.now,
               (size_t)g_lights.now_count * sizeof(PortGlLight));
        g_lights.before_count = g_lights.now_count;
        memcpy(g_lights.now, g_lights.handed,
               (size_t)g_lights.handed_count * sizeof(PortGlLight));
        g_lights.now_count = g_lights.handed_count;
        g_lights.taken_serial = g_lights.handed_serial;
    }
    port_LightsUnlock();
    g_lights.frames++;
}

const PortGlLight *port_GlLightsNow(s32 *count) {
    *count = g_lights.now_count;
    return g_lights.now;
}

const PortGlLight *port_GlLightsBefore(s32 *count) {
    *count = g_lights.before_count;
    return g_lights.before;
}

const char *port_GlLightsStart(void *(*getproc)(const char *name)) {
    s32 i;

#define GL(result, name, args)                                                 \
    name = (result(*) args)getproc(#name);                                     \
    if (name == NULL) {                                                        \
        return "no " #name;                                                    \
    }
    PORT_GL_LIGHTS_FUNCTIONS
#undef GL
    glGenBuffers(1, &g_lights.light_buffer);
    glGenBuffers(1, &g_lights.grid_buffer);
    glGenBuffers(1, &g_lights.list_buffer);
    glGenBuffers(1, &g_lights.face_buffer);
    glGenTextures(1, &g_lights.map);
    glBindTexture(GL_TEXTURE_2D_ARRAY, g_lights.map);
    glTexImage3D(
        GL_TEXTURE_2D_ARRAY, 0, (GLint)GL_DEPTH_COMPONENT32F, LIGHT_MAP_SIZE,
        LIGHT_MAP_SIZE, LIGHT_LAYERS, 0, GL_DEPTH_COMPONENT, GL_FLOAT, NULL);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_COMPARE_MODE,
                    GL_COMPARE_REF_TO_TEXTURE);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_COMPARE_FUNC, GL_LEQUAL);
    glBindTexture(GL_TEXTURE_2D_ARRAY, 0);
    glGenTextures(1, &g_lights.stage_map);
    glBindTexture(GL_TEXTURE_2D_ARRAY, g_lights.stage_map);
    glTexImage3D(
        GL_TEXTURE_2D_ARRAY, 0, (GLint)GL_DEPTH_COMPONENT32F, LIGHT_MAP_SIZE,
        LIGHT_MAP_SIZE, LIGHT_LAYERS, 0, GL_DEPTH_COMPONENT, GL_FLOAT, NULL);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MIN_FILTER, GL_NEAREST_L);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAG_FILTER, GL_NEAREST_L);
    glBindTexture(GL_TEXTURE_2D_ARRAY, 0);
    glGenFramebuffers(LIGHT_LAYERS, g_lights.stage_framebuffer);
    for (i = 0; i < LIGHT_LAYERS; i++) {
        glBindFramebuffer(GL_FRAMEBUFFER, g_lights.stage_framebuffer[i]);
        glFramebufferTextureLayer(
            GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, g_lights.stage_map, 0, i);
        glDrawBuffer(GL_NONE);
        glReadBuffer(GL_NONE);
    }
    for (i = 0; i < PORT_GL_LIGHTS_SHADOWED; i++) {
        g_lights.kept_mesh[i] = -1;
    }
    glGenFramebuffers(LIGHT_LAYERS, g_lights.framebuffer);
    for (i = 0; i < LIGHT_LAYERS; i++) {
        glBindFramebuffer(GL_FRAMEBUFFER, g_lights.framebuffer[i]);
        glFramebufferTextureLayer(
            GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, g_lights.map, 0, i);
        glDrawBuffer(GL_NONE);
        glReadBuffer(GL_NONE);
        if (glCheckFramebufferStatus(GL_FRAMEBUFFER) !=
            GL_FRAMEBUFFER_COMPLETE) {
            glBindFramebuffer(GL_FRAMEBUFFER, 0);
            return "the lights' shadow maps cannot be drawn into";
        }
    }
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    g_lights.phase = -1;
    g_lights.is_started = TRUE;
    return NULL;
}

void port_GlLightsSetOn(s32 on) { g_lights.is_on = on != 0; }

s32 port_GlLightsIsOn(void) { return g_lights.is_on && g_lights.is_started; }

void port_GlLightsSetMadeUp(s32 count) {
    g_lights.made_up = count < 0                    ? 0
                       : count > PORT_GL_LIGHTS_MAX ? PORT_GL_LIGHTS_MAX
                                                    : count;
}

void port_GlLightsSetPhase(s32 phase) {
    g_lights.phase = phase < 0 ? -1 : phase;
}

void port_GlLightsNewPicture(void) { g_lights.is_prepared = FALSE; }

const void *port_GlLightsCard(s32 *count) {
    *count = g_lights.is_started && port_GlLightsIsOn() && g_lights.is_prepared
                 ? g_lights.card_count
                 : 0;
    return g_lights.card;
}

s32 port_GlLightsShown(void) { return g_lights.most; }

s32 port_GlLightsShadowed(void) { return g_lights.shadowed; }

/* ---- the arithmetic ---- */

static float port_LightsRoot(float value) {
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

static float port_LightsFloor(float value) {
    float whole = (float)(s32)value;

    return whole > value ? whole - 1.0f : whole;
}

/* The cosine of `degrees`, by its series (the window's C library is not
 * asked for its arithmetic: include/ has a math.h of the game's own). */
static float port_LightsCos(float degrees) {
    float x = degrees * PI_F / 180.0f;
    float term = 1.0f;
    float sum = 1.0f;
    s32 i;

    if (x < 0.0f) {
        x = -x;
    }
    if (x > PI_F) {
        x = PI_F;
    }
    for (i = 1; i < 12; i++) {
        term *= -x * x / (float)((2 * i - 1) * (2 * i));
        sum += term;
    }
    return sum;
}

/* A smooth number from 0 to 1 that moves as a flame does, the same for a
 * light's `id` and the frame's clock in every run: a few waves of prime
 * periods, mixed. */
static float port_LightsFlicker(u32 id, float clock) {
    float sum = 0.0f;
    static const float period[3] = {7.0f, 13.0f, 29.0f};
    s32 k;

    for (k = 0; k < 3; k++) {
        u32 seed = (id + 1u) * 0x9E3779B1U + (u32)k * 0x85EBCA6BU;
        float shift = (float)(seed >> 8) / 16777216.0f;
        float t = clock / period[k] + shift;
        float f = t - port_LightsFloor(t);
        /* a triangle wave, smoothed: 0 to 1 and back */
        float tri = f < 0.5f ? 2.0f * f : 2.0f - 2.0f * f;

        sum += tri * tri * (3.0f - 2.0f * tri);
    }
    return sum / 3.0f;
}

/* The lights made up for the benchmark: a ring about the fighters, at
 * about their height above the ground, each its own colour, a quarter of
 * them spots aimed down, a quarter flickering; everything measured in the
 * fighters' own height, which is the one scale every stage shares. */
static s32 port_LightsMadeUp(PortGlLight *out, const PortStageView *view) {
    PortSeeFighter fighters[PORT_SEE_FIGHTERS];
    s32 n = port_GlSeeFightersAlways(fighters);
    float centre[3] = {0.0f, 0.0f, 0.0f};
    float tall = 0.0f;
    s32 i;

    (void)view;
    if (n <= 0) {
        return 0;
    }
    {
        s32 good = 0;

        for (i = 0; i < n; i++) {
            float h = fighters[i].bottom - fighters[i].top;

            /* a slot whose bones are not a body standing on a stage this
             * frame (a fighter's view not yet made) is passed over */
            if (!(h > 0.0f && h < 100000.0f) || fighters[i].x > 1.0e6f ||
                fighters[i].x < -1.0e6f || fighters[i].z > 1.0e6f ||
                fighters[i].z < -1.0e6f) {
                continue;
            }
            centre[0] += fighters[i].x;
            centre[2] += fighters[i].z;
            centre[1] += fighters[i].bottom;
            tall += h;
            good++;
        }
        if (good == 0) {
            return 0;
        }
        centre[0] /= (float)good;
        centre[1] /= (float)good;
        centre[2] /= (float)good;
        tall /= (float)good;
    }
    if (tall <= 0.0f) {
        return 0;
    }
    /* placed once a stage, where the fighters first stand, and then left
     * where they are, as a stage's own lamps are */
    {
        static s32 placed_stage = -1;
        static float placed[4];
        s32 stage = port_RemasterStageNumber();

        if (stage != placed_stage) {
            placed_stage = stage;
            placed[0] = centre[0];
            placed[1] = centre[1];
            placed[2] = centre[2];
            placed[3] = tall;
        }
        centre[0] = placed[0];
        centre[1] = placed[1];
        centre[2] = placed[2];
        tall = placed[3];
    }
    for (i = 0; i < g_lights.made_up; i++) {
        PortGlLight *l = &out[i];
        float turn = 360.0f * (float)i / (float)g_lights.made_up;
        float ring = tall * (0.4f + 0.25f * (float)(i % 5));

        memset(l, 0, sizeof(*l));
        l->position[0] = centre[0] + ring * port_LightsCos(turn);
        /* the world's y runs down: above the feet is less */
        l->position[1] = centre[1] - tall * (0.6f + 0.2f * (float)(i % 3));
        l->position[2] = centre[2] + ring * port_LightsCos(turn - 90.0f);
        l->colour[0] = 0.6f + 0.4f * (float)(i % 3 == 0);
        l->colour[1] = 0.4f + 0.4f * (float)(i % 3 == 1);
        l->colour[2] = 0.3f + 0.6f * (float)(i % 3 == 2);
        l->intensity = 0.9f;
        l->radius = tall * 1.2f;
        l->kind = (i % 4 == 3) ? PORT_GL_LIGHT_SPOT : PORT_GL_LIGHT_POINT;
        l->direction[1] = 1.0f;
        l->cone_inner = 25.0f;
        l->cone_outer = 40.0f;
        l->flicker = (i % 4 == 0) ? 0.5f : 0.0f;
        l->casts_shadow = i < PORT_GL_LIGHTS_SHADOWED;
        l->id = (u32)(i + 1);
    }
    return g_lights.made_up;
}

/* A face of a light's shadow: looking from `at` along an axis, a quarter
 * turn wide, from LIGHT_NEAR to `far` (a column after a column). */
static void port_LightsFace(float *m, const float *at, s32 face, float far) {
    static const float look[LIGHT_FACES][3] = {
        {1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}};
    static const float up[LIGHT_FACES][3] = {
        {0, 1, 0}, {0, 1, 0}, {0, 0, 1}, {0, 0, 1}, {0, 1, 0}, {0, 1, 0}};
    const float *f = look[face];
    const float *u = up[face];
    float r[3];
    float a = (far + LIGHT_NEAR) / (far - LIGHT_NEAR);
    float b = -2.0f * far * LIGHT_NEAR / (far - LIGHT_NEAR);
    float tx;
    float ty;
    float tz;
    s32 k;

    /* right = up x forward */
    r[0] = u[1] * f[2] - u[2] * f[1];
    r[1] = u[2] * f[0] - u[0] * f[2];
    r[2] = u[0] * f[1] - u[1] * f[0];
    tx = -(r[0] * at[0] + r[1] * at[1] + r[2] * at[2]);
    ty = -(u[0] * at[0] + u[1] * at[1] + u[2] * at[2]);
    tz = -(f[0] * at[0] + f[1] * at[1] + f[2] * at[2]);
    /* clip = (x, y, a z + b, z) of the view (x right, y up, z ahead) */
    for (k = 0; k < 3; k++) {
        m[k * 4 + 0] = r[k];
        m[k * 4 + 1] = u[k];
        m[k * 4 + 2] = a * f[k];
        m[k * 4 + 3] = f[k];
    }
    m[12] = tx;
    m[13] = ty;
    m[14] = a * tz + b;
    m[15] = tz;
}

/* Which lights are this picture's: the frame's list, each moved between
 * its place in the frame on show and in the frame after where the picture
 * is between them, its flicker worked out; made up ones where there are
 * none and the benchmark asks. */
static s32 port_LightsOfPicture(PortGlLight *out, const PortStageView *view) {
    const PortGlLight *now = g_lights.now;
    s32 count = g_lights.now_count;
    float t = g_lights.phase >= 0
                  ? (float)g_lights.phase / (float)PORT_TWEEN_ONE
                  : 1.0f;
    float clock = (float)g_lights.frames + (g_lights.phase >= 0 ? t : 0.0f);
    s32 i;
    s32 k;

    if (count == 0 && g_lights.made_up > 0) {
        count = port_LightsMadeUp(out, view);
    } else {
        memcpy(out, now, (size_t)count * sizeof(*out));
    }
    for (i = 0; i < count; i++) {
        PortGlLight *l = &out[i];

        if (g_lights.phase >= 0 && l->id != 0) {
            /* the picture is between the list before and this one: from
             * where it was to where it is */
            for (k = 0; k < g_lights.before_count; k++) {
                const PortGlLight *b = &g_lights.before[k];

                if (b->id == l->id) {
                    s32 n;

                    for (n = 0; n < 3; n++) {
                        l->position[n] = b->position[n] +
                                         (l->position[n] - b->position[n]) * t;
                    }
                    l->intensity =
                        b->intensity + (l->intensity - b->intensity) * t;
                    break;
                }
            }
        }
        if (l->flicker > 0.0f) {
            float f = l->flicker > 1.0f ? 1.0f : l->flicker;

            l->intensity *= 1.0f - f * port_LightsFlicker(l->id, clock);
        }
    }
    return count;
}

/* ---- once a picture ---- */

void port_GlLightsPrepare(void) {
    PortGlLight light[PORT_GL_LIGHTS_MAX];
    s32 is_between = FALSE;
    const PortStageView *view;
    PortGlPicture picture;
    float origin_x;
    float origin_z;
    float cell;
    float strongest[PORT_GL_LIGHTS_SHADOWED];
    s32 shadow_of[PORT_GL_LIGHTS_SHADOWED];
    u32 used = 0;
    s32 count;
    s32 i;
    s32 x;
    s32 z;

    if (g_lights.is_prepared || !port_GlLightsIsOn()) {
        return;
    }
    g_lights.is_prepared = TRUE;
    g_lights.card_count = 0;
    g_lights.shadowed = 0;
    view = port_GlStageViewNow(&is_between);
    if (view == NULL || view->frames == 0) {
        return;
    }
    count = port_LightsOfPicture(light, view);
    if (count == 0) {
        return;
    }
    PORT_GPU_BEGIN(PORT_GPU_LIGHTS);
    /* The grid: over the square that holds every light's reach, seen from
     * above, so that no light lies outside it whatever the camera looks
     * at (a fight's camera stands thousands of units behind the
     * fighters, and a grid about it left the far half of the stage
     * unlit). Its cells are at least PORT_GL_LIGHTS_CELL, in whole
     * steps of it, its corner on whole cells: which cell a light is
     * binned into changes only how many a pixel loops over, never what
     * it is lit by. */
    {
        float low[2];
        float high[2];
        float side;

        low[0] = low[1] = 1e30f;
        high[0] = high[1] = -1e30f;
        for (i = 0; i < count; i++) {
            const PortGlLight *l = &light[i];

            for (x = 0; x < 2; x++) {
                float at = l->position[x * 2];

                low[x] = at - l->radius < low[x] ? at - l->radius : low[x];
                high[x] = at + l->radius > high[x] ? at + l->radius : high[x];
            }
        }
        side = high[0] - low[0] > high[1] - low[1] ? high[0] - low[0]
                                                   : high[1] - low[1];
        cell = PORT_GL_LIGHTS_CELL *
               (1.0f + port_LightsFloor(side / (PORT_GL_LIGHTS_CELLS - 1) /
                                        PORT_GL_LIGHTS_CELL));
        origin_x = port_LightsFloor(low[0] / cell) * cell;
        origin_z = port_LightsFloor(low[1] / cell) * cell;
    }
    /* the strongest few that ask cast shadows: the brightest at the
     * camera's distance */
    for (i = 0; i < PORT_GL_LIGHTS_SHADOWED; i++) {
        strongest[i] = 0.0f;
        shadow_of[i] = -1;
    }
    for (i = 0; i < count; i++) {
        const PortGlLight *l = &light[i];
        float dx = l->position[0] - (float)view->camera[0];
        float dy = l->position[1] - (float)view->camera[1];
        float dz = l->position[2] - (float)view->camera[2];
        float weight = l->intensity * l->radius * l->radius /
                       (1.0f + dx * dx + dy * dy + dz * dz);
        s32 k;

        if (!l->casts_shadow) {
            continue;
        }
        for (k = 0; k < PORT_GL_LIGHTS_SHADOWED; k++) {
            if (weight > strongest[k]) {
                s32 m;

                for (m = PORT_GL_LIGHTS_SHADOWED - 1; m > k; m--) {
                    strongest[m] = strongest[m - 1];
                    shadow_of[m] = shadow_of[m - 1];
                }
                strongest[k] = weight;
                shadow_of[k] = i;
                break;
            }
        }
    }
    for (i = 0; i < count; i++) {
        const PortGlLight *l = &light[i];
        CardLight *c = &g_lights.card[i];
        float length = port_LightsRoot(l->direction[0] * l->direction[0] +
                                       l->direction[1] * l->direction[1] +
                                       l->direction[2] * l->direction[2]);
        float scale = BYTES_ONE * l->intensity;
        s32 k;

        c->place[0] = l->position[0];
        c->place[1] = l->position[1];
        c->place[2] = l->position[2];
        c->place[3] = l->radius;
        for (k = 0; k < 3; k++) {
            float colour = l->colour[k] < 0.0f ? 0.0f : l->colour[k];

            c->color[k] = colour * scale;
            c->aim[k] = length > 0.0f ? l->direction[k] / length : 0.0f;
        }
        c->color[3] = l->kind == PORT_GL_LIGHT_SPOT ? 1.0f : 0.0f;
        c->aim[3] = port_LightsCos(l->cone_inner);
        c->extra[0] = port_LightsCos(
            l->cone_outer > l->cone_inner ? l->cone_outer
                                          : l->cone_inner + 1.0f);
        c->extra[1] = -1.0f;
        c->extra[2] = 0.0f;
        c->extra[3] = 0.0f;
        for (k = 0; k < PORT_GL_LIGHTS_SHADOWED; k++) {
            if (shadow_of[k] == i) {
                c->extra[1] = (float)(g_lights.shadowed * LIGHT_FACES);
                shadow_of[k] = -2 - g_lights.shadowed;
                g_lights.shadowed++;
                break;
            }
        }
    }
    g_lights.card_count = count;
    if (count > g_lights.most) {
        g_lights.most = count;
    }
    /* each cell: the lights whose reach overlaps it, seen from above */
    for (z = 0; z < PORT_GL_LIGHTS_CELLS; z++) {
        for (x = 0; x < PORT_GL_LIGHTS_CELLS; x++) {
            u32 at = (u32)(z * PORT_GL_LIGHTS_CELLS + x) * 2u;
            float x0 = origin_x + (float)x * cell;
            float z0 = origin_z + (float)z * cell;

            g_lights.cell[at] = used;
            g_lights.cell[at + 1] = 0;
            for (i = 0; i < count && used < LIST_MAX; i++) {
                const CardLight *c = &g_lights.card[i];
                float nx = c->place[0] < x0 ? x0
                           : c->place[0] > x0 + cell
                               ? x0 + cell
                               : c->place[0];
                float nz = c->place[2] < z0 ? z0
                           : c->place[2] > z0 + cell
                               ? z0 + cell
                               : c->place[2];
                float dx = c->place[0] - nx;
                float dz = c->place[2] - nz;

                if (dx * dx + dz * dz < c->place[3] * c->place[3]) {
                    g_lights.list[used++] = (u32)i;
                    g_lights.cell[at + 1]++;
                }
            }
        }
    }
    g_lights.grid_head[0] = origin_x;
    g_lights.grid_head[1] = origin_z;
    g_lights.grid_head[2] = cell;
    g_lights.grid_head[3] = (float)PORT_GL_LIGHTS_CELLS;
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, g_lights.light_buffer);
    glBufferData(GL_SHADER_STORAGE_BUFFER,
                 (GLsizeiptr)((size_t)count * sizeof(CardLight)), g_lights.card,
                 GL_DYNAMIC_DRAW);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, g_lights.grid_buffer);
    {
        static u32 grid[4 + PORT_GL_LIGHTS_CELLS * PORT_GL_LIGHTS_CELLS * 2];

        memcpy(grid, g_lights.grid_head, sizeof(g_lights.grid_head));
        memcpy(grid + 4, g_lights.cell, sizeof(g_lights.cell));
        glBufferData(GL_SHADER_STORAGE_BUFFER, (GLsizeiptr)sizeof(grid), grid,
                     GL_DYNAMIC_DRAW);
    }
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, g_lights.list_buffer);
    glBufferData(GL_SHADER_STORAGE_BUFFER,
                 (GLsizeiptr)((used > 0 ? used : 1) * sizeof(u32)),
                 g_lights.list, GL_DYNAMIC_DRAW);
    /* the shadows: six faces of each, the stage and the fighters drawn
     * into each as the cast shadows' programs draw them for the sun */
    port_GlGetPicture(&picture);
    if (g_lights.shadowed > 0 && picture.target_w > 0) {
        s32 slot = 0;

        glDisable(GL_SCISSOR_TEST);
        glDisable(GL_BLEND);
        glDisable(GL_CULL_FACE);
        glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
        glEnable(GL_DEPTH_TEST);
        glDepthFunc(GL_LESS);
        glDepthMask(GL_TRUE);
        glClearDepth(1.0);
        glEnable(GL_POLYGON_OFFSET_FILL);
        glPolygonOffset(1.5f, 4.0f);
        glViewport(0, 0, LIGHT_MAP_SIZE, LIGHT_MAP_SIZE);
        for (i = 0; i < count; i++) {
            const CardLight *c = &g_lights.card[i];
            s32 face;

            if (c->extra[1] < 0.0f) {
                continue;
            }
            slot = (s32)c->extra[1];
            {
                s32 which = slot / LIGHT_FACES;
                s32 mesh =
                    port_GlStageVertices() * 64 + port_RemasterStageNumber();
                s32 is_kept = g_lights.kept_mesh[which] == mesh &&
                              memcmp(g_lights.kept_place[which], c->place,
                                     sizeof(c->place)) == 0;

                for (face = 0; face < LIGHT_FACES; face++) {
                    float *m = g_lights.face[slot + face];

                    port_LightsFace(m, c->place, face, c->place[3]);
                    if (!is_kept) {
                        glBindFramebuffer(
                            GL_FRAMEBUFFER,
                            g_lights.stage_framebuffer[slot + face]);
                        glClear(GL_DEPTH_BUFFER_BIT);
                        port_GlStageDrawSunCasters(m);
                    }
                }
                if (!is_kept) {
                    memcpy(
                        g_lights.kept_place[which], c->place, sizeof(c->place));
                    g_lights.kept_mesh[which] = mesh;
                    g_lights.stage_redrawn++;
                }
                /* the stage's depth, then the fighters over it */
                glCopyImageSubData(
                    g_lights.stage_map, GL_TEXTURE_2D_ARRAY, 0, 0, 0, slot,
                    g_lights.map, GL_TEXTURE_2D_ARRAY, 0, 0, 0, slot,
                    LIGHT_MAP_SIZE, LIGHT_MAP_SIZE, LIGHT_FACES);
                for (face = 0; face < LIGHT_FACES; face++) {
                    glBindFramebuffer(
                        GL_FRAMEBUFFER, g_lights.framebuffer[slot + face]);
                    port_GlShadowDrawSunCasters(g_lights.face[slot + face]);
                }
            }
        }
        glDisable(GL_POLYGON_OFFSET_FILL);
        glDisable(GL_DEPTH_TEST);
        glDepthMask(GL_FALSE);
        glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
        glBindFramebuffer(GL_FRAMEBUFFER, picture.framebuffer);
        glViewport(0, 0, picture.target_w, picture.target_h);
        glEnable(GL_SCISSOR_TEST);
        port_GlForgetState();
    }
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, g_lights.face_buffer);
    glBufferData(GL_SHADER_STORAGE_BUFFER, (GLsizeiptr)sizeof(g_lights.face),
                 g_lights.face, GL_DYNAMIC_DRAW);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
    PORT_GPU_END();
}

void port_GlLightsLocate(u32 program, PortGlLightsAt *at) {
    if (glGetUniformLocation == NULL) {
        memset(at, 0xFF, sizeof(*at));
        return;
    }
    at->count = glGetUniformLocation(program, "u_lights_count");
    at->map = glGetUniformLocation(program, "u_lights_map");
    at->grid = -1;
}

void port_GlLightsBind(const PortGlLightsAt *at) {
    /* the map's sampler on its own unit whether the lights are on or not
     * (port_GlSunBind) */
    glUniform1i(at->map, PORT_GL_LIGHTS_MAP_UNIT);
    if (!g_lights.is_started) {
        glUniform1i(at->count, 0);
        return;
    }
    if (!g_lights.is_prepared || g_lights.card_count == 0 ||
        !port_GlLightsIsOn()) {
        glUniform1i(at->count, 0);
        return;
    }
    glUniform1i(at->count, getenv("RB_LIGHTS_DEBUG") != NULL
                               ? -g_lights.card_count
                               : g_lights.card_count);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, PORT_GL_LIGHTS_BINDING,
                     g_lights.light_buffer);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, PORT_GL_LIGHTS_GRID_BINDING,
                     g_lights.grid_buffer);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, PORT_GL_LIGHTS_LIST_BINDING,
                     g_lights.list_buffer);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 7, g_lights.face_buffer);
    glActiveTexture(GL_TEXTURE0 + PORT_GL_LIGHTS_MAP_UNIT);
    glBindTexture(GL_TEXTURE_2D_ARRAY, g_lights.map);
    glActiveTexture(GL_TEXTURE0);
    glUniform1i(at->map, PORT_GL_LIGHTS_MAP_UNIT);
}
