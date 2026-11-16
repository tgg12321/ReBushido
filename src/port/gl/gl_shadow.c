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
#include "gl_shadow.h"
#include "port_gpu.h"
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
    GL(void, glReadPixels,                                                     \
       (GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, void *))               \
    GL(void, glDrawArrays, (GLenum, GLint, GLsizei))

#define GL(result, name, args) static result(*name) args;
PORT_GL_SHADOW_FUNCTIONS
#undef GL

/* ---- what a frame may hold ---- */

/* The silhouette's triangles. Two fighters, their weapons and the props
 * that cast one come to a few dozen faces; a whole fight's worst was
 * measured at 105 primitives (docs/PORT.md). */
#define TRIANGLES_MAX 4096
#define POINTS_MAX (3 * TRIANGLES_MAX)
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

static struct {
    s32 is_started;
    s32 strength; /* hundredths; 0 is the game's own black shadows */
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
    ShadowBox box;   /* the silhouettes, in the colour buffer's pixels */
    ShadowBox clip;  /* ... and the drawing area they belong to */
    s32 cover_count;
    ShadowBox cover[COVERS_MAX];
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

const char *port_GlShadowStart(void *(*getproc)(const char *name)) {
    static const GLfloat corners[] = {
        0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f, 1.0f, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f};

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
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D,
                           g_shadow.mask, 0);
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

void port_GlShadowSetStrength(s32 strength) {
    g_shadow.strength = strength < 0 ? 0 : (strength > 100 ? 100 : strength);
}

s32 port_GlShadowStrength(void) { return g_shadow.strength; }

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
    port_GlShadowLay();
    port_GlShadowForget();
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
s32 port_GlShadowIsGround(const PortRemasterPrim *prim) {
    s32 count = 0;
    PortAddress tiles;

    if (!port_GlShadowIsSoft() || prim->id.object == 0) {
        return FALSE;
    }
    tiles = port_RemasterStageTiles(&count);
    if (tiles == 0 || count <= 0) {
        return FALSE;
    }
    return prim->id.object >= tiles &&
           prim->id.object <
               tiles + (PortAddress)((u32)count * sizeof(StageTile));
}

s32 port_GlShadowTakes(const PortRemasterPrim *prim) {
    PortGlPoint point[6];
    PortGlPicture picture;
    s32 many;
    s32 i;

    if (!port_GlShadowIsSoft() || !prim->is_shadow) {
        return FALSE;
    }
    /* From here on the primitive is not drawn, whether or not there is
     * room for it in the mask: a face the mask lost is a face of the
     * silhouette missing, which is a hole in a soft shadow, and not a
     * black polygon in the middle of one. */
    many = port_GlPlacePoints(prim, point);
    port_GlGetPicture(&picture);
    if (!g_shadow.has_any) {
        ShadowBox clip;
        s32 x;
        s32 y;
        s32 w;
        s32 h;

        /* the drawing area the game gave this frame: the darkening must
         * not reach outside it, where the picture of the frame before it
         * is waiting to be shown */
        port_GlScissorFor(prim, &x, &y, &w, &h);
        clip.left = x;
        clip.top = y;
        clip.right = x + w - 1;
        clip.bottom = y + h - 1;
        g_shadow.clip = clip;
        g_shadow.has_any = TRUE;
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

/* The silhouettes into the mask's red. */
static void port_GlShadowDrawSilhouettes(const PortGlPicture *picture) {
    glUseProgram(g_shadow.silhouette);
    glBindVertexArray(g_shadow.array);
    glBindBuffer(GL_ARRAY_BUFFER, g_shadow.buffer);
    glBufferData(GL_ARRAY_BUFFER,
                 (GLsizeiptr)((u32)g_shadow.count * sizeof(*g_shadow.point)),
                 g_shadow.point, GL_STREAM_DRAW);
    glUniform2f(g_shadow.at_target, (GLfloat)picture->target_w,
                (GLfloat)picture->target_h);
    glUniform2f(g_shadow.at_scale, (GLfloat)picture->draw_x,
                (GLfloat)picture->draw_y);
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
    glUniform4f(g_shadow.dark_at_rect, (GLfloat)painted.left,
                (GLfloat)painted.top,
                (GLfloat)(painted.right - painted.left + 1),
                (GLfloat)(painted.bottom - painted.top + 1));
    glUniform1f(g_shadow.dark_at_radius, (GLfloat)radius);
    glUniform1f(
        g_shadow.dark_at_strength, (GLfloat)g_shadow.strength / 100.0f);
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

/* The shadows gathered so far laid over the picture they belong to, and
 * forgotten: the next primitive to come is the next picture's. */
static void port_GlShadowLay(void) {
    if (!port_GlShadowIsSoft() || !g_shadow.has_any) {
        return;
    }
    /* everything of the picture is painted before the shadows darken it,
     * and the batch of it that is still waiting is part of that */
    port_GlFlushBatch();
    g_shadow.shown_faces = g_shadow.count / 3;
    g_shadow.run_lost += g_shadow.lost;
    port_GlShadowPaint();
    port_GlShadowForget();
}
