/* The stage's sun casting shadows from maps of its own (gl_sun.h;
 * docs/PORT.md, "The lighting overhaul"). Nothing here changes a value
 * the game computes. */
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#include "common.h"
#include "gl.h"
#include "gl_bench.h"
#include "gl_shadow.h"
#include "gl_stage.h"
#include "gl_sun.h"
#include "gl_dressing.h"
#include "gl_time.h"
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
typedef double GLclampd;
typedef char GLchar;

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
#define GL_NEAREST 0x2600
#define GL_CLAMP_TO_EDGE 0x812F
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
#define GL_DEPTH_CLAMP 0x864F

#define PORT_GL_SUN_FUNCTIONS                                                  \
    GL(void, glGenTextures, (GLsizei, GLuint *))                               \
    GL(void, glBindTexture, (GLenum, GLuint))                                  \
    GL(void, glActiveTexture, (GLenum))                                        \
    GL(void, glTexImage3D,                                                     \
       (GLenum, GLint, GLint, GLsizei, GLsizei, GLsizei, GLint, GLenum,        \
        GLenum, const void *))                                                 \
    GL(void, glTexParameteri, (GLenum, GLenum, GLint))                         \
    GL(void, glGenSamplers, (GLsizei, GLuint *))                               \
    GL(void, glSamplerParameteri, (GLuint, GLenum, GLint))                     \
    GL(void, glBindSampler, (GLuint, GLuint))                                  \
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
    GL(GLint, glGetUniformLocation, (GLuint, const GLchar *))                  \
    GL(void, glUniform1i, (GLint, GLint))                                      \
    GL(void, glUniform1f, (GLint, GLfloat))                                    \
    GL(void, glUniform3f, (GLint, GLfloat, GLfloat, GLfloat))                  \
    GL(void, glUniform4f, (GLint, GLfloat, GLfloat, GLfloat, GLfloat))         \
    GL(void, glUniform2f, (GLint, GLfloat, GLfloat))                           \
    GL(void, glUniformMatrix3fv, (GLint, GLsizei, GLboolean, const GLfloat *)) \
    GL(void, glUniformMatrix4fv, (GLint, GLsizei, GLboolean, const GLfloat *))

#define GL(result, name, args) static result(*name) args;
PORT_GL_SUN_FUNCTIONS
#undef GL

/* ---- the cascades ---- */

/* Where each of the nested maps' slices of the view begins and ends, in
 * the world's units from the camera along its view, the nearest the
 * fight's: a fight is seen from 500 to 1,500 units. Past the last, to the
 * end of the stage however far it is drawn, the last map holds the whole
 * stage (port_SunWhole). */
#define SUN_SLICES (PORT_GL_SUN_CASCADES - 1)
/* The nearer maps the dressing's foliage casts into: the nearest alone,
 * the fight's 16 to 1,600 units. The second (to 5,000) cost the card half
 * a millisecond a picture at 2,560 by 1,440 on the dressed bench's stage 8
 * (the sun's pass 1.12 ms against 0.60) and changed no byte of a fight's
 * pictures on stages 8 and 19: past it a tuft's shadow is a texel or two */
#define SUN_FOLIAGE_CASCADES 1
static const float g_sun_split[SUN_SLICES + 1] = {
    16.0f, 1600.0f, 5000.0f, 16000.0f};
/* What the map of the whole stage holds where there is no mesh to say
 * how big the stage is: a slice on to the far corner of the largest
 * stage's grid (32 cells of 2,000 units, 90,510 across its diagonal). */
#define SUN_WHOLE_FAR 92000.0f
/* The texels the map of the whole stage leaves about the stage's box. */
#define SUN_WHOLE_MARGIN 4.0f
/* How far towards the sun each map reaches past its own slice, so that a
 * tall wall or a roof outside the slice still casts into it. */
#define SUN_BEHIND 6000.0f
/* The step the maps' depths move by. */
#define SUN_DEPTH_STEP 256.0f
/* The game's stages. */
#define PORT_SUN_STAGES 38
/* The penumbra: how wide it grows for each unit between a caster and the
 * surface its shadow falls on (a sun of about two and a half degrees,
 * softer than the real one's half a degree, as the game's soft light
 * wants). */
#define SUN_SOFT 0.04f
/* The slope-scaled bias of a caster's depth, as glPolygonOffset has it. */
#define SUN_OFFSET_FACTOR 2.0f
#define SUN_OFFSET_UNITS 4.0f

static struct {
    s32 is_started;
    s32 is_on;
    s32 is_ready; /* the maps are this picture's */
    s32 is_tried; /* ... or were tried for this picture and could not be */
    GLuint map;
    GLuint compare;
    GLuint raw;
    GLuint framebuffer[PORT_GL_SUN_CASCADES];
    float travel[3];
    GLfloat matrix[PORT_GL_SUN_CASCADES][16];
    float texel[PORT_GL_SUN_CASCADES];
    float depth[PORT_GL_SUN_CASCADES];
    u32 pictures;
    u32 triangles;
    /* the view the ground was last drawn with, a pixel and its depth
     * turned back into the world's place by it (port_GlSunNoteView) */
    s32 has_view;
    GLfloat unview[9];
    float eye[3];
    float h;
    float middle[2];
    u32 stamp; /* moved on with the maps and the view */
} g_sun;

const char *port_GlSunStart(void *(*getproc)(const char *name)) {
    s32 i;

#define GL(result, name, args)                                                 \
    name = (result(*) args)getproc(#name);                                     \
    if (name == NULL) {                                                        \
        return "no " #name;                                                    \
    }
    PORT_GL_SUN_FUNCTIONS
#undef GL
    glGenTextures(1, &g_sun.map);
    glBindTexture(GL_TEXTURE_2D_ARRAY, g_sun.map);
    glTexImage3D(GL_TEXTURE_2D_ARRAY, 0, (GLint)GL_DEPTH_COMPONENT32F,
                 PORT_GL_SUN_SIZE, PORT_GL_SUN_SIZE, PORT_GL_SUN_CASCADES, 0,
                 GL_DEPTH_COMPONENT, GL_FLOAT, NULL);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindTexture(GL_TEXTURE_2D_ARRAY, 0);
    /* the same texels read two ways: compared, each read four of them
     * mixed (the card's own percentage-closer filter), and as depths */
    glGenSamplers(1, &g_sun.compare);
    glSamplerParameteri(g_sun.compare, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glSamplerParameteri(g_sun.compare, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glSamplerParameteri(g_sun.compare, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glSamplerParameteri(g_sun.compare, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glSamplerParameteri(
        g_sun.compare, GL_TEXTURE_COMPARE_MODE, GL_COMPARE_REF_TO_TEXTURE);
    glSamplerParameteri(g_sun.compare, GL_TEXTURE_COMPARE_FUNC, GL_LEQUAL);
    glGenSamplers(1, &g_sun.raw);
    glSamplerParameteri(g_sun.raw, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glSamplerParameteri(g_sun.raw, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glSamplerParameteri(g_sun.raw, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glSamplerParameteri(g_sun.raw, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glGenFramebuffers(PORT_GL_SUN_CASCADES, g_sun.framebuffer);
    for (i = 0; i < PORT_GL_SUN_CASCADES; i++) {
        glBindFramebuffer(GL_FRAMEBUFFER, g_sun.framebuffer[i]);
        glFramebufferTextureLayer(
            GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, g_sun.map, 0, i);
        glDrawBuffer(GL_NONE);
        glReadBuffer(GL_NONE);
        if (glCheckFramebufferStatus(GL_FRAMEBUFFER) !=
            GL_FRAMEBUFFER_COMPLETE) {
            glBindFramebuffer(GL_FRAMEBUFFER, 0);
            return "the sun's map cannot be drawn into";
        }
    }
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    g_sun.is_started = TRUE;
    return NULL;
}

void port_GlSunSetOn(s32 on) { g_sun.is_on = on != 0; }

s32 port_GlSunIsOn(void) { return g_sun.is_on && g_sun.is_started; }

void port_GlSunNewPicture(void) {
    g_sun.is_ready = FALSE;
    g_sun.is_tried = FALSE;
}

s32 port_GlSunIsReady(void) { return g_sun.is_ready; }

s32 port_GlSunTravel(float *travel) {
    if (!g_sun.is_ready) {
        return FALSE;
    }
    travel[0] = g_sun.travel[0];
    travel[1] = g_sun.travel[1];
    travel[2] = g_sun.travel[2];
    return TRUE;
}

u32 port_GlSunPictures(void) { return g_sun.pictures; }

u32 port_GlSunTriangles(void) { return g_sun.triangles; }

u32 port_GlSunStamp(void) { return g_sun.stamp; }

/* ---- the arithmetic ---- */

static float port_SunRoot(float value) {
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

static float port_SunFloor(float value) {
    float whole = (float)(s32)value;

    return whole > value ? whole - 1.0f : whole;
}

static void port_SunNormalise(float *v) {
    float length = port_SunRoot(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);

    if (length > 0.0f) {
        v[0] /= length;
        v[1] /= length;
        v[2] /= length;
    }
}

static void port_SunCross(const float *a, const float *b, float *out) {
    out[0] = a[1] * b[2] - a[2] * b[1];
    out[1] = a[2] * b[0] - a[0] * b[2];
    out[2] = a[0] * b[1] - a[1] * b[0];
}

static float port_SunDot(const float *a, const float *b) {
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}

/* The sun's direction of travel: the game slides a point at height `above`
 * over the ground (y running down, so above is less than nought) by
 * `above` times each slope (light_ProjectShadow), so the light goes down
 * one unit for every minus slope across. */
static s32 port_SunDirection(float *travel) {
    const PortGlTime *look = port_GlTime();
    s32 shear_x;
    s32 shear_z;

    /* a time of day of the port's own has a sun of its own (gl_time.h),
     * which the maps are drawn along instead of the game's: the cascades
     * and the whole stage's map are fitted to it again each picture */
    if (look->on && look->to_sun[1] < -0.01f) {
        travel[0] = -look->to_sun[0];
        travel[1] = -look->to_sun[1];
        travel[2] = -look->to_sun[2];
        port_SunNormalise(travel);
        return TRUE;
    }
    port_RemasterShadowShear(&shear_x, &shear_z);
    travel[0] = -(float)shear_x / 4096.0f;
    travel[1] = 1.0f;
    travel[2] = -(float)shear_z / 4096.0f;
    port_SunNormalise(travel);
    return TRUE;
}

/* Map `i`: the slice of the view from g_sun_split[i] to [i + 1] held in a
 * sphere, the sphere seen along the sun, its middle moved to a whole texel
 * of the map so that the map's texels stay where they are on the ground as
 * the camera moves. */
static void port_SunCascade(s32 i, const float *eye, const float *forward,
                            float spread, const float *right, const float *up) {
    float near = i < SUN_SLICES ? g_sun_split[i] : g_sun_split[SUN_SLICES];
    float far = i < SUN_SLICES ? g_sun_split[i + 1] : SUN_WHOLE_FAR;
    /* the sphere's middle on the view's axis, where the near and the far
     * corners of the slice are as far from it */
    float k2 = spread * spread;
    float along = 0.5f * (far + near) * (1.0f + k2);
    float radius;
    float middle[3];
    float texel;
    float mx;
    float my;
    float mz;
    float lo;
    float hi;
    GLfloat *m = g_sun.matrix[i];
    s32 k;

    if (along > far) {
        along = far;
    }
    radius = port_SunRoot((far - along) * (far - along) + k2 * far * far);
    /* a whole number of units, so that the size never wobbles */
    radius = port_SunFloor(radius + 1.0f);
    for (k = 0; k < 3; k++) {
        middle[k] = eye[k] + forward[k] * along;
    }
    texel = 2.0f * radius / (float)PORT_GL_SUN_SIZE;
    mx = port_SunFloor(port_SunDot(right, middle) / texel) * texel;
    my = port_SunDot(up, middle);
    my = port_SunFloor(my / texel) * texel;
    /* the depths too are held still: moved by whole steps, so that a
     * caster's depth in the map, and what a surface's own is compared
     * with, do not creep as the camera does */
    mz = port_SunFloor(port_SunDot(g_sun.travel, middle) / SUN_DEPTH_STEP) *
         SUN_DEPTH_STEP;
    lo = mz - radius - SUN_BEHIND;
    hi = mz + radius;
    /* x = (right . p - mx) / radius, y likewise, z from -1 at lo to 1 at
     * hi; a column after a column */
    for (k = 0; k < 3; k++) {
        m[k * 4 + 0] = right[k] / radius;
        m[k * 4 + 1] = up[k] / radius;
        m[k * 4 + 2] = 2.0f * g_sun.travel[k] / (hi - lo);
        m[k * 4 + 3] = 0.0f;
    }
    m[12] = -mx / radius;
    m[13] = -my / radius;
    m[14] = -2.0f * lo / (hi - lo) - 1.0f;
    m[15] = 1.0f;
    g_sun.texel[i] = texel;
    g_sun.depth[i] = hi - lo;
}

/* The last map: the whole of the stage's box seen along the sun, the
 * same at every picture while the stage and its sun stand (so nothing of
 * it crawls, and nothing needs moving by whole texels), for everything
 * past the nested maps' slices however far the stage is drawn. Its two
 * sides are scaled each to the box's own, which a stage's ground,
 * longer one way than the other, gives texels the narrower for. */
static s32 port_SunWhole(s32 i, const float *right, const float *up) {
    float lo[3];
    float hi[3];
    float least[3];
    float most[3];
    float half_x;
    float half_y;
    float texel;
    GLfloat *m = g_sun.matrix[i];
    s32 c;
    s32 k;

    if (!port_GlStageBox(lo, hi)) {
        return FALSE;
    }
    for (c = 0; c < 8; c++) {
        float corner[3];
        float along[3];

        corner[0] = (c & 1) ? hi[0] : lo[0];
        corner[1] = (c & 2) ? hi[1] : lo[1];
        corner[2] = (c & 4) ? hi[2] : lo[2];
        along[0] = port_SunDot(right, corner);
        along[1] = port_SunDot(up, corner);
        along[2] = port_SunDot(g_sun.travel, corner);
        for (k = 0; k < 3; k++) {
            if (c == 0 || along[k] < least[k]) {
                least[k] = along[k];
            }
            if (c == 0 || along[k] > most[k]) {
                most[k] = along[k];
            }
        }
    }
    half_x = 0.5f * (most[0] - least[0]);
    half_y = 0.5f * (most[1] - least[1]);
    half_x += SUN_WHOLE_MARGIN * 2.0f * half_x / (float)PORT_GL_SUN_SIZE + 1.0f;
    half_y += SUN_WHOLE_MARGIN * 2.0f * half_y / (float)PORT_GL_SUN_SIZE + 1.0f;
    /* the depths a little past the box's, as the slices' are, so that a
     * fighter's sword over the highest roof is not held to its end */
    least[2] -= SUN_DEPTH_STEP;
    most[2] += SUN_DEPTH_STEP;
    for (k = 0; k < 3; k++) {
        m[k * 4 + 0] = right[k] / half_x;
        m[k * 4 + 1] = up[k] / half_y;
        m[k * 4 + 2] = 2.0f * g_sun.travel[k] / (most[2] - least[2]);
        m[k * 4 + 3] = 0.0f;
    }
    m[12] = -0.5f * (most[0] + least[0]) / half_x;
    m[13] = -0.5f * (most[1] + least[1]) / half_y;
    m[14] = -2.0f * least[2] / (most[2] - least[2]) - 1.0f;
    m[15] = 1.0f;
    texel =
        2.0f * (half_x > half_y ? half_x : half_y) / (float)PORT_GL_SUN_SIZE;
    g_sun.texel[i] = texel;
    g_sun.depth[i] = most[2] - least[2];
    return TRUE;
}

/* Whether a stage's own ground and pieces cast the sun's shadows: not on a
 * stage under a roof, whose ceiling would put its whole floor in shadow
 * where the game lit it (docs/PORT.md, "The lighting overhaul"). Found by
 * every stage's picture with the sun's maps and with `shadows cast`, the
 * tour at blank 3,200: the lower half's mean lightness fell to 0.40, 0.86,
 * 0.70 and 0.32 of cast's on these four, whose pictures showed a ceiling's
 * shadow over the floor; everywhere else it is a wall's, a pillar's or a
 * terrace's, and stays (docs/PORT.md, "The lighting overhaul"). */
static const u8 g_sun_stage_casts[PORT_SUN_STAGES] = {
    /* 0: the main Tatara shrine, under its roof */
    0, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    /* 20: the kabuki stage, under its theatre's */
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    /* 30: the building's parking lot; 34: the chanbara hall */
    0, 1, 1, 1, 0, 1, 1, 1};

static s32 port_SunStageCasts(void) {
    s32 stage = port_RemasterStageNumber();

    return stage < 0 || stage >= PORT_SUN_STAGES || g_sun_stage_casts[stage];
}

/* The card's state the maps are drawn in, and the picture's put back
 * after them. */
static void port_SunMapsBegin(void) {
    glDisable(GL_SCISSOR_TEST);
    glDisable(GL_BLEND);
    glDisable(GL_CULL_FACE);
    glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LESS);
    glDepthMask(GL_TRUE);
    glClearDepth(1.0);
    /* a caster outside the map's depth is held to its ends, not cut */
    glEnable(GL_DEPTH_CLAMP);
    glEnable(GL_POLYGON_OFFSET_FILL);
    glPolygonOffset(SUN_OFFSET_FACTOR, SUN_OFFSET_UNITS);
    glViewport(0, 0, PORT_GL_SUN_SIZE, PORT_GL_SUN_SIZE);
}

static void port_SunMapsEnd(const PortGlPicture *picture) {
    glDisable(GL_POLYGON_OFFSET_FILL);
    glDisable(GL_DEPTH_CLAMP);
    glDisable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glBindFramebuffer(GL_FRAMEBUFFER, picture->framebuffer);
    glViewport(0, 0, picture->target_w, picture->target_h);
    glEnable(GL_SCISSOR_TEST);
    port_GlForgetState();
}

void port_GlSunWarm(void) {
    static s32 is_warm;
    PortGlPicture picture;
    s32 i;

    if (is_warm || !port_GlSunIsOn()) {
        return;
    }
    port_GlGetPicture(&picture);
    if (picture.target_w <= 0) {
        return;
    }
    is_warm = TRUE;
    port_SunMapsBegin();
    for (i = 0; i < PORT_GL_SUN_CASCADES; i++) {
        glBindFramebuffer(GL_FRAMEBUFFER, g_sun.framebuffer[i]);
        glClear(GL_DEPTH_BUFFER_BIT);
        port_GlShadowWarmSunCasters();
        port_GlDressingWarmCasters();
    }
    port_SunMapsEnd(&picture);
}

s32 port_GlSunPrepare(const PortStageView *view) {
    PortGlPicture picture;
    float eye[3];
    float forward[3];
    float right[3];
    float up[3];
    float spread;
    float half_w;
    float axis[3] = {0.0f, 0.0f, 1.0f};
    s32 i;

    if (g_sun.is_ready || g_sun.is_tried) {
        return g_sun.is_ready;
    }
    g_sun.is_tried = TRUE;
    if (!port_GlSunIsOn() || view == NULL || view->h <= 0) {
        return FALSE;
    }
    port_GlGetPicture(&picture);
    if (picture.target_w <= 0 || picture.display_w <= 0) {
        return FALSE;
    }
    PORT_GPU_BEGIN(PORT_GPU_SUN);
    port_SunDirection(g_sun.travel);
    for (i = 0; i < 3; i++) {
        eye[i] = (float)view->camera[i];
        /* the view's third row: its depth */
        forward[i] = (float)view->matrix[6 + i];
    }
    port_SunNormalise(forward);
    /* How wide the view is at a unit's depth, corner to corner: the
     * screen's half width and a wider picture's band over the projection's
     * distance, and its half height, which the projection takes at half
     * scale (the console's frame is half as tall as it is wide). */
    half_w = (float)picture.display_w * 0.5f + (float)picture.margin;
    spread = port_SunRoot(half_w * half_w + 240.0f * 240.0f) / (float)view->h;
    /* the map's own two axes across the sun */
    if (g_sun.travel[2] > 0.9f || g_sun.travel[2] < -0.9f) {
        axis[0] = 1.0f;
        axis[2] = 0.0f;
    }
    port_SunCross(axis, g_sun.travel, right);
    port_SunNormalise(right);
    port_SunCross(g_sun.travel, right, up);
    port_SunNormalise(up);
    g_sun.triangles = 0;
    port_SunMapsBegin();
    for (i = 0; i < PORT_GL_SUN_CASCADES; i++) {
        if (i < SUN_SLICES || !port_SunWhole(i, right, up)) {
            port_SunCascade(i, eye, forward, spread, right, up);
        }
        glBindFramebuffer(GL_FRAMEBUFFER, g_sun.framebuffer[i]);
        glClear(GL_DEPTH_BUFFER_BIT);
        if (port_SunStageCasts()) {
            g_sun.triangles += port_GlStageDrawSunCasters(g_sun.matrix[i]);
            g_sun.triangles += port_GlDressingDrawSunCasters(g_sun.matrix[i]);
            if (i < SUN_FOLIAGE_CASCADES) {
                g_sun.triangles +=
                    port_GlDressingDrawFoliageCasters(g_sun.matrix[i], eye);
            }
        }
        g_sun.triangles += port_GlShadowDrawSunCasters(g_sun.matrix[i]);
    }
    port_SunMapsEnd(&picture);
    PORT_GPU_END();
    g_sun.is_ready = TRUE;
    g_sun.stamp++;
    g_sun.pictures++;
    return TRUE;
}

/* ---- the readers ---- */

void port_GlSunNoteView(const float *rotation, const float *camera, float h,
                        float middle_x, float middle_y) {
    /* the view in the world's units, its second row halved as the
     * projection halves it (gl_post.c, POST_COMMON) */
    float m[9];
    float det;
    s32 i;

    for (i = 0; i < 9; i++) {
        m[i] = rotation[i] / 4096.0f * (i / 3 == 1 ? 0.5f : 1.0f);
    }
    det = m[0] * (m[4] * m[8] - m[5] * m[7]) -
          m[1] * (m[3] * m[8] - m[5] * m[6]) +
          m[2] * (m[3] * m[7] - m[4] * m[6]);
    if (h <= 0.0f || det == 0.0f) {
        g_sun.has_view = FALSE;
        return;
    }
    /* its inverse, a column after a column: the cofactors over the
     * determinant, transposed */
    g_sun.unview[0] = (m[4] * m[8] - m[5] * m[7]) / det;
    g_sun.unview[3] = (m[2] * m[7] - m[1] * m[8]) / det;
    g_sun.unview[6] = (m[1] * m[5] - m[2] * m[4]) / det;
    g_sun.unview[1] = (m[5] * m[6] - m[3] * m[8]) / det;
    g_sun.unview[4] = (m[0] * m[8] - m[2] * m[6]) / det;
    g_sun.unview[7] = (m[2] * m[3] - m[0] * m[5]) / det;
    g_sun.unview[2] = (m[3] * m[7] - m[4] * m[6]) / det;
    g_sun.unview[5] = (m[1] * m[6] - m[0] * m[7]) / det;
    g_sun.unview[8] = (m[0] * m[4] - m[1] * m[3]) / det;
    for (i = 0; i < 3; i++) {
        g_sun.eye[i] = camera[i];
    }
    g_sun.h = h;
    g_sun.middle[0] = middle_x;
    g_sun.middle[1] = middle_y;
    g_sun.has_view = TRUE;
    g_sun.stamp++;
}

void port_GlSunLocateScreen(u32 program, PortGlSunScreenAt *at) {
    if (glGetUniformLocation == NULL) {
        memset(at, 0xFF, sizeof(*at));
        return;
    }
    port_GlSunLocate(program, &at->sun);
    at->unview = glGetUniformLocation(program, "u_sun_unview");
    at->eye = glGetUniformLocation(program, "u_sun_eye");
    at->h = glGetUniformLocation(program, "u_sun_h");
    at->middle = glGetUniformLocation(program, "u_sun_middle");
    at->dark = glGetUniformLocation(program, "u_sun_dark");
    at->time.on = glGetUniformLocation(program, "u_time_on");
    at->time.own_light = glGetUniformLocation(program, "u_time_own_light");
    at->time.own_color = glGetUniformLocation(program, "u_time_own_color");
    at->time.own_back = glGetUniformLocation(program, "u_time_own_back");
    at->time.light = glGetUniformLocation(program, "u_time_light");
    at->time.color = glGetUniformLocation(program, "u_time_color");
    at->time.back = glGetUniformLocation(program, "u_time_back");
    at->time.floor = glGetUniformLocation(program, "u_time_floor");
}

/* The time of day's lights and the stage's own, for `time_relight`
 * (gl_time.h). */
static void port_SunBindTime(const PortGlTimeAt *at) {
    const void *own = port_RemasterTileLights();
    GLfloat light[9];
    GLfloat color[9];
    GLfloat back[3];
    const PortGlTime *look = port_GlTime();
    s32 on = own != NULL && look->on;
    s32 i;

    glUniform1i(at->on, on);
    for (i = 0; i < 3; i++) {
        float f = look->floor[i];

        back[i] = on ? (f > 2.0f ? 2.0f : f) : 1.0f;
    }
    glUniform3f(at->floor, back[0], back[1], back[2]);
    if (!on) {
        return;
    }
    port_GlTimeSetupFloats(own, light, color, back);
    glUniformMatrix3fv(at->own_light, 1, GL_TRUE, light);
    glUniformMatrix3fv(at->own_color, 1, GL_TRUE, color);
    glUniform3f(at->own_back, back[0], back[1], back[2]);
    port_GlTimeSetupFloats(port_GlTimeTileLights(), light, color, back);
    glUniformMatrix3fv(at->light, 1, GL_TRUE, light);
    glUniformMatrix3fv(at->color, 1, GL_TRUE, color);
    glUniform3f(at->back, back[0], back[1], back[2]);
}

void port_GlSunBindScreen(const PortGlSunScreenAt *at) {
    if (!g_sun.is_started) {
        return;
    }
    port_SunBindTime(&at->time);
    if (!g_sun.has_view) {
        /* no place in the world to be had: nothing is lit by it */
        glUniform1i(at->sun.on, 0);
        glUniform1f(at->h, 0.0f);
        return;
    }
    port_GlSunBind(&at->sun);
    glUniformMatrix3fv(at->unview, 1, GL_FALSE, g_sun.unview);
    glUniform3f(at->eye, g_sun.eye[0], g_sun.eye[1], g_sun.eye[2]);
    glUniform1f(at->h, g_sun.h);
    glUniform2f(at->middle, g_sun.middle[0], g_sun.middle[1]);
    glUniform1f(at->dark, (GLfloat)port_GlShadowStrength() / 100.0f);
}

/* How many times its own share of the light a shadow takes from the
 * stage's sunlit floor, so that it takes the `shadows` strength of all of
 * it (sun_shade, gl_sun.h): the stage's tiles' lights on a level floor --
 * the sun's turn on it and each other light's, through the colour matrix,
 * onto the colour of the dark, as the coprocessor's NCS has it -- and the
 * strength over the sun's share of that. At least 1 (the sun's own share,
 * where the sun alone is already more than the strength), at most
 * SUN_GAIN_MOST (a sun low over the floor, or none). */
#define SUN_GAIN_MOST 4.0f

static float port_SunGain(void) {
    const LightSetup *lights = (const LightSetup *)port_GlTimeTileLights();
    float strength = (float)port_GlShadowStrength() / 100.0f;
    float lit[3];
    float sun[3];
    float of_lit;
    float of_sun;
    float gain;
    s32 i;
    s32 c;

    if (lights == NULL) {
        return 1.0f;
    }
    for (c = 0; c < 3; c++) {
        lit[c] = (float)lights->back[c];
        sun[c] = 0.0f;
    }
    for (i = 0; i < LIGHT_SETUP_LIGHTS; i++) {
        /* a level floor's normal is up, which is minus y */
        float turn = -(float)lights->lmat.m[i][1] / 4096.0f;

        if (turn <= 0.0f) {
            continue;
        }
        for (c = 0; c < 3; c++) {
            float add = (float)lights->cmat.m[c][i] * turn / 16.0f;

            lit[c] += add;
            if (i == LIGHT_SUN) {
                sun[c] += add;
            }
        }
    }
    for (c = 0; c < 3; c++) {
        lit[c] = lit[c] > 255.0f ? 255.0f : lit[c];
    }
    of_lit = 0.299f * lit[0] + 0.587f * lit[1] + 0.114f * lit[2];
    of_sun = 0.299f * sun[0] + 0.587f * sun[1] + 0.114f * sun[2];
    if (of_sun <= 0.0f || of_lit <= 0.0f) {
        return SUN_GAIN_MOST;
    }
    gain = strength * of_lit / of_sun;
    return gain < 1.0f ? 1.0f : gain > SUN_GAIN_MOST ? SUN_GAIN_MOST : gain;
}

void port_GlSunLocate(u32 program, PortGlSunAt *at) {
    if (glGetUniformLocation == NULL) {
        memset(at, 0xFF, sizeof(*at));
        return;
    }
    at->on = glGetUniformLocation(program, "u_sun_on");
    at->matrix = glGetUniformLocation(program, "u_sun_matrix");
    at->texel = glGetUniformLocation(program, "u_sun_texel");
    at->depth = glGetUniformLocation(program, "u_sun_depth");
    at->travel = glGetUniformLocation(program, "u_sun_travel");
    at->map = glGetUniformLocation(program, "u_sun_map");
    at->raw = glGetUniformLocation(program, "u_sun_raw");
    at->soft = glGetUniformLocation(program, "u_sun_soft");
    at->gain = glGetUniformLocation(program, "u_sun_gain");
}

void port_GlSunBind(const PortGlSunAt *at) {
    /* the maps' samplers on their own units whether the maps are on or
     * not: left on unit 0 beside the drawing's own texture, a sampler of
     * another kind makes the card refuse every draw of the program (Mesa
     * does), and the fighters' models were not drawn with `shadows` other
     * than `map` */
    glUniform1i(at->map, PORT_GL_SUN_UNIT);
    glUniform1i(at->raw, PORT_GL_SUN_RAW_UNIT);
    if (!g_sun.is_started) {
        glUniform1i(at->on, 0);
        return;
    }
    if (!g_sun.is_ready) {
        glUniform1i(at->on, 0);
        return;
    }
    glUniform1i(at->on, 1);
    glUniformMatrix4fv(
        at->matrix, PORT_GL_SUN_CASCADES, GL_FALSE, g_sun.matrix[0]);
    glUniform4f(at->texel, g_sun.texel[0], g_sun.texel[1], g_sun.texel[2],
                g_sun.texel[3]);
    glUniform4f(at->depth, g_sun.depth[0], g_sun.depth[1], g_sun.depth[2],
                g_sun.depth[3]);
    glUniform3f(at->travel, g_sun.travel[0], g_sun.travel[1], g_sun.travel[2]);
    /* the moon's softer, a low sun's a little (gl_time.h) */
    glUniform1f(at->soft, SUN_SOFT * port_GlTime()->soft);
    glUniform1f(
        at->gain, getenv("RB_SUN_DEBUG") != NULL ? -1.0f : port_SunGain());
    glActiveTexture(GL_TEXTURE0 + PORT_GL_SUN_UNIT);
    glBindTexture(GL_TEXTURE_2D_ARRAY, g_sun.map);
    glBindSampler(PORT_GL_SUN_UNIT, g_sun.compare);
    glActiveTexture(GL_TEXTURE0 + PORT_GL_SUN_RAW_UNIT);
    glBindTexture(GL_TEXTURE_2D_ARRAY, g_sun.map);
    glBindSampler(PORT_GL_SUN_RAW_UNIT, g_sun.raw);
    glActiveTexture(GL_TEXTURE0);
    glUniform1i(at->map, PORT_GL_SUN_UNIT);
    glUniform1i(at->raw, PORT_GL_SUN_RAW_UNIT);
}
