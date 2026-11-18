/* The remastered renderer on a graphics card (gl.h; docs/PORT.md,
 * "Remastered"): OpenGL 3.3 core, no window and no display of its own.
 *
 * The colour buffer is all of video memory at `scale` times its
 * resolution. A primitive is drawn where the GTE really projected it,
 * with sixteen bits of fraction, and its texture in perspective by the
 * depth of each corner; its texels are read from a mirror of the
 * software GPU's video memory, which is sent to the card again
 * whenever a fill, a copy or a load changes it, and those three are
 * drawn into the colour buffer by stretching that mirror, so that
 * everything the game makes by copying pixels about (the loading
 * screens, the portraits, the fades, the movie) is there.
 *
 * GL's names are declared here and looked up at the start: nothing is
 * vendored, and the build needs no loader and no GL headers but the
 * host's own.
 *
 * Nothing here is in the build without a window: the port and its
 * checks do not have it. */
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "common.h"
#include "gl.h"
#include "gl_fighter.h"
#include "gl_replace.h"
#include "gl_scene.h"
#include "gl_see.h"
#include "gl_shadow.h"
#include "gl_sky.h"
#include "gl_stage.h"
#include "port_gpu.h"
#include "port_mods.h"
#include "port_profile.h"
#include "port_remaster.h"

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
#define GL_NO_ERROR 0
#define GL_TRIANGLES 0x0004
#define GL_LINES 0x0001
#define GL_UNSIGNED_BYTE 0x1401
#define GL_SHORT 0x1402
#define GL_UNSIGNED_SHORT 0x1403
#define GL_FLOAT 0x1406
#define GL_RGBA 0x1908
#define GL_RGBA8 0x8058
#define GL_R16UI 0x8234
#define GL_COLOR 0x1800
#define GL_RED_INTEGER 0x8D94
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
#define GL_COLOR_ATTACHMENT1 0x8CE1
#define GL_DEPTH_ATTACHMENT 0x8D00
#define GL_RENDERBUFFER 0x8D41
#define GL_DEPTH_COMPONENT24 0x81A6
#define GL_DEPTH_TEST 0x0B71
#define GL_LESS 0x0201
#define GL_FRAMEBUFFER_COMPLETE 0x8CD5
#define GL_COLOR_BUFFER_BIT 0x00004000
#define GL_ARRAY_BUFFER 0x8892
#define GL_STREAM_DRAW 0x88E0
#define GL_VERTEX_SHADER 0x8B31
#define GL_FRAGMENT_SHADER 0x8B30
#define GL_GEOMETRY_SHADER 0x8DD9
#define GL_COMPILE_STATUS 0x8B81
#define GL_LINK_STATUS 0x8B82
#define GL_BLEND 0x0BE2
#define GL_SCISSOR_TEST 0x0C11
#define GL_ZERO 0
#define GL_ONE 1
#define GL_SRC_ALPHA 0x0302
#define GL_ONE_MINUS_SRC_ALPHA 0x0303
#define GL_FUNC_ADD 0x8006
#define GL_FUNC_REVERSE_SUBTRACT 0x800B
#define GL_UNPACK_ROW_LENGTH 0x0CF2
#define GL_UNPACK_ALIGNMENT 0x0CF5
#define GL_PACK_ALIGNMENT 0x0D05
#define GL_VERSION 0x1F02
#define GL_RENDERER 0x1F01
#define GL_VENDOR 0x1F00
/* The window's own framebuffer, and the order the host's pixels are in. */
#define GL_WINDOW_FRAMEBUFFER 0
#define GL_BGRA 0x80E1

/* Every GL function this file calls: the name, and the type of the
 * pointer it is kept in. */
#define PORT_GL_FUNCTIONS                                                      \
    GL(const char *, glGetString, (GLenum))                                    \
    GL(GLenum, glGetError, (void))                                             \
    GL(void, glViewport, (GLint, GLint, GLsizei, GLsizei))                     \
    GL(void, glScissor, (GLint, GLint, GLsizei, GLsizei))                      \
    GL(void, glEnable, (GLenum))                                               \
    GL(void, glDisable, (GLenum))                                              \
    GL(void, glClearColor, (GLfloat, GLfloat, GLfloat, GLfloat))               \
    GL(void, glClear, (GLbitfield))                                            \
    GL(void, glColorMask, (GLboolean, GLboolean, GLboolean, GLboolean))        \
    GL(void, glFlush, (void))                                                  \
    GL(void, glBlendFunc, (GLenum, GLenum))                                    \
    GL(void, glBlendEquation, (GLenum))                                        \
    GL(void, glPixelStorei, (GLenum, GLint))                                   \
    GL(void, glReadPixels,                                                     \
       (GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, void *))               \
    GL(void, glGenTextures, (GLsizei, GLuint *))                               \
    GL(void, glBindTexture, (GLenum, GLuint))                                  \
    GL(void, glActiveTexture, (GLenum))                                        \
    GL(void, glTexParameteri, (GLenum, GLenum, GLint))                         \
    GL(void, glTexImage2D,                                                     \
       (GLenum, GLint, GLint, GLsizei, GLsizei, GLint, GLenum, GLenum,         \
        const void *))                                                         \
    GL(void, glTexSubImage2D,                                                  \
       (GLenum, GLint, GLint, GLint, GLsizei, GLsizei, GLenum, GLenum,         \
        const void *))                                                         \
    GL(void, glGenFramebuffers, (GLsizei, GLuint *))                           \
    GL(void, glBindFramebuffer, (GLenum, GLuint))                              \
    GL(void, glFramebufferTexture2D, (GLenum, GLenum, GLenum, GLuint, GLint))  \
    GL(void, glFramebufferRenderbuffer, (GLenum, GLenum, GLenum, GLuint))      \
    GL(void, glGenRenderbuffers, (GLsizei, GLuint *))                          \
    GL(void, glBindRenderbuffer, (GLenum, GLuint))                             \
    GL(void, glRenderbufferStorage, (GLenum, GLenum, GLsizei, GLsizei))        \
    GL(GLenum, glCheckFramebufferStatus, (GLenum))                             \
    GL(void, glDepthFunc, (GLenum))                                            \
    GL(void, glDepthMask, (GLboolean))                                         \
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
    GL(void, glUniform3f, (GLint, GLfloat, GLfloat, GLfloat))                  \
    GL(void, glUniform2f, (GLint, GLfloat, GLfloat))                           \
    GL(void, glUniform4f, (GLint, GLfloat, GLfloat, GLfloat, GLfloat))         \
    GL(void, glDrawArrays, (GLenum, GLint, GLsizei))                           \
    GL(void, glDrawBuffers, (GLsizei, const GLenum *))                         \
    GL(void, glClearBufferuiv, (GLenum, GLint, const GLuint *))

#define GL(result, name, args) static result(*name) args;
PORT_GL_FUNCTIONS
#undef GL

/* ---- the picture ---- */

/* A batch's vertices: a frame of the fight has about ten thousand. */
#define VERTICES_MAX 65536
/* Rectangles of video memory written since the card last drew, kept to
 * be stretched into the colour buffer together (port_GlDrawBlits). */
#define BLITS_MAX 32
/* a part of a fade's tint (port_mods.h) */
#define TINT_PART_MASK 0xFF
/* The longest run of pixels sent to the card in one go. */
#define UPLOAD_PIXELS_MAX (PORT_VRAM_PIXELS)

/* The bits of a vertex's `flags` byte. */
/* The most a primitive may span, as the chip has it. */
#define SPAN_X_MAX 1023
#define SPAN_Y_MAX 511
/* ... and the most one drawn where the projection really put it may
 * (port_GlIsDrawn): video memory's own width and height, which is wider
 * than any picture and far short of the tens of thousands of pixels a
 * projection that came apart puts a corner at. */
#define SPAN_TRUE_X_MAX (2 * PORT_VRAM_WIDTH)
#define SPAN_TRUE_Y_MAX (2 * PORT_VRAM_HEIGHT)

/* A pixel of video memory: five bits a channel, red lowest. */
#define VRAM_CHANNEL_MASK 31
#define VRAM_RED_SHIFT 0
#define VRAM_GREEN_SHIFT 5
#define VRAM_BLUE_SHIFT 10

#define FLAG_TEXTURED 1
#define FLAG_RAW 2
#define FLAG_HALF 4
#define FLAG_DITHERED 8
/* The primitive is flat on the screen, as the chip drew it: the 2D
 * layer, and anything of the scene the side channel has no depth for.
 * Such a one is sampled once for each pixel of video memory and not once
 * for each pixel of the picture (the shader's `snap`). */
#define FLAG_FLAT 16
/* Every corner has a depth: it is a polygon of the scene, drawn in
 * perspective, and its texture is read over the whole area of a fragment
 * where that is more than a texel (the shader's `steps`). */
#define FLAG_DEPTH 32
/* The ground drawn as one mesh hides it by the ordering table's slots and
 * not by depth (port_GlIsOverGround). */
#define FLAG_OVER_GROUND 64
/* A mod's picture of its whole texture page stands in for its texture
 * (gl_scene.h). */
#define FLAG_PAGE 128
/* Not in the byte: a cloth held against the depth of the fighters drawn
 * from models (port_GlFighterHolds), carried in the second bit of the
 * vertex's `info[3]` (INFO_HELD). */
#define FLAG_HELD 256
/* The bits of a vertex's `info[3]`: the chip's rule for which pixels a
 * flat polygon covers, and a cloth held against the models. */
#define INFO_FLAT_RULE 1
#define INFO_HELD 2
/* The unit the fighters' layer's depth is read from. */
#define FIGHTER_DEPTH_UNIT 9
/* How much nearer than it is a cloth is taken to be, against the models,
 * in the world's units: where it is fastened, at the belt, the cloth and
 * the model are the same surface to within the model's fit. */
#define FIGHTER_CLOTH_BIAS 8.0f

/* A corner's `w`: the depth the coprocessor worked out, in these units.
 * Any number in proportion to it will do -- it is the perspective divide
 * and nothing else -- and this keeps a fight's within a few hundred. The
 * shader is told it, so that it can take the depth back out of a
 * fragment's own `w` for the test against the ground's mesh
 * (gl_stage.h). */
#define PORT_GL_DEPTH_UNIT 1024.0f

/* Which fragments a draw keeps, where a half-transparent primitive's
 * texels do not all mix (the mix that subtracts needs two draws). */
#define PASS_ALL 0
#define PASS_MIXED 1
#define PASS_PLAIN 2

typedef struct {
    GLfloat x;
    GLfloat y;
    GLfloat w;
    GLfloat u;
    GLfloat v;
    u8 color[4]; /* red, green, blue, the flags */
    s16 page[4]; /* the page's corner, the colour table's first pixel */
    u8 info[4];  /* the texel's bits, the mix, nothing, nothing */
    u8 window[4];
    /* the ordering-table slot the renderer linked it at, or -1
     * (port_remaster.h, `ot_slot`) */
    GLfloat slot;
} PortGlVertex;

/* What a run of vertices must share: everything the fixed blending and
 * the scissor cannot be told per vertex. */
typedef struct {
    s32 is_half;
    s32 blend;
    s32 clip_left;
    s32 clip_top;
    s32 clip_right;
    s32 clip_bottom;
    /* the scene of a wider picture is drawn across the whole of it, and
     * the console's drawing area does not reach that far */
    s32 is_wide;
    /* A piece of the stage's ground, which the fighters' shadows fall
     * on (gl_shadow.h). It is in the batch's state and not in a vertex
     * so that a batch is all ground or none of it: what is not ground is
     * kept aside as it is drawn, for the mask that keeps the darkening
     * off everything standing in a shadow. */
    s32 is_ground;
    /* The ground's mesh was drawn this frame before these primitives
     * came, so they are held against it (port_GlFlush). It is the
     * batch's own and not asked when the batch is drawn: the last batch
     * of a frame is drawn at the vertical blank, after the game has
     * begun the next frame, and asked then it was held against nothing
     * at all. */
    s32 is_held;
    /* A mod's replacement of the batch's texture (gl_replace.h), 0 for
     * none, and the texture points of the page it stands for. */
    u32 replace_texture;
    s32 replace_u;
    s32 replace_v;
    s32 replace_w;
    s32 replace_h;
    s32 replace_mix;  /* PORT_GL_MIX_... (gl_replace.h) */
    u32 replace_tint; /* a fade's (gl_replace.h) */
} PortGlState;

/* A rectangle of video memory written, to be stretched into the colour
 * buffer, and a mod's picture of it (0 for none) with a fade's tint. */
typedef struct {
    PortDirtyRect rect;
    u32 replacement;
    u32 tint;
} PortGlBlit;

static struct {
    s32 is_started;
    PortGlOptions options;
    GLuint program;
    GLuint blit_program;
    GLuint vram_texture;
    GLuint color_texture;
    GLuint framebuffer;
    /* The depth buffer beside the colour buffer: the ground's mesh
     * writes it and the scene's primitives are tested against it
     * (gl_stage.h). It is made and sized here, with the colour buffer,
     * so that the framebuffer is always complete whether or not the mesh
     * is drawn. */
    GLuint depth_buffer;
    /* ... and the ordering-table slot of the face of the ground each
     * pixel shows, which the mesh writes beside its colour and a
     * fighter is held against (port_GlGroundSlotsBegin) */
    GLuint slot_texture;
    GLuint buffer;
    GLuint array;
    GLuint blit_buffer;
    GLuint blit_array;
    GLuint resolve_program;
    GLuint picture_texture;
    GLuint picture_framebuffer;
    /* what shows the picture in a window, and the texture the host's own
     * pixels are given to the card in (port_GlShowPixels) */
    GLuint show_program;
    GLuint host_texture;
    s32 host_w;
    s32 host_h;
    s32 scale_x; /* a pixel of video memory is this many of the picture */
    s32 scale_y;
    s32 sample; /* ... and this many of those each way are averaged */
    s32 draw_x; /* scale_x times sample: the colour buffer's own scale */
    s32 draw_y;
    s32 target_w;
    s32 target_h;
    s32 picture_w; /* what the resolve pass last made */
    s32 picture_h;
    /* How many pixels of video memory wide the picture is on each side
     * of the display's own rectangle (port_RemasterMargin): 0 at the
     * console's shape. Everything the game draws is moved right by it,
     * so that the console's 640 pixels sit in the middle and the scene,
     * whose corners the side channel has wherever the projection put
     * them, fills the width. */
    s32 margin;
    GLint at_target;
    GLint at_scale;
    GLint at_vram;
    GLint at_affine;
    GLint at_exact;
    GLint at_like_emulator;
    GLint at_bilinear;
    GLint at_minify;
    GLint at_pass;
    GLint at_w_scale;
    GLint at_depth_range;
    GLint at_depth_bias;
    GLint at_ground_slots;
    GLint at_fighter_depth;
    GLint at_fighter_rect;
    GLint at_fighter_bias;
    GLint at_ground_order;
    GLint blit_at_target;
    GLint blit_at_rect;
    GLint blit_at_scale;
    GLint blit_at_vram;
    GLint blit_at_shift;
    GLint at_replace;
    GLint at_replaced;
    GLint at_replace_rect;
    GLint at_replace_mix;
    GLint at_replace_tint;
    GLint at_scene_lookup;
    GLint at_scene_pages;
    GLint at_scene_bases;
    GLint at_scene_on;
    GLint at_scene_levels;
    GLint blit_at_tint;
    GLint blit_at_replace;
    GLint blit_at_replaced;
    GLint resolve_at_color;
    GLint resolve_at_sample;
    GLint resolve_at_at;
    GLint show_at_picture;
    GLint show_at_dest;
    GLint show_at_window;
    GLint show_at_source;
    GLint show_at_texture;
    GLint show_at_over;
    /* VERTICES_MAX of them, asked for as the backend starts: two and a
     * half megabytes, which is more than the window's program may keep
     * in its own variables (tools/port_sdl_build.py: they all have to
     * fit in four megabytes, so that their low 24 bits are no other
     * memory's). Nothing of the game's is ever in memory from malloc;
     * this is the backend's own. */
    PortGlVertex *vertices;
    s32 count;
    /* The vertices of this frame that are not the stage's ground, kept
     * as they are drawn: at the frame's end they are drawn again into
     * the shadows' mask, which is how the darkening is kept off
     * everything standing in a shadow (gl_shadow.h). Asked for only
     * where `shadows soft` is on. */
    PortGlVertex *occluders;
    s32 occluder_count;
    s32 occluders_lost;
    PortGlState state;
    s32 has_state;
    /* The mirror of video memory the card reads texels from, and its
     * copy here: each rectangle is copied in as video memory held it
     * when it was written, and what is sent to the card is read from
     * the copy, so that a rectangle sent later is still the one that was
     * written and not what the software GPU has drawn over it since
     * (port_GlMarkMirror). `mirror` is what the card has not been sent. */
    u16 *staged;
    PortDirtyRect mirror;
    /* what is to be stretched into the colour buffer before the card
     * next draws anything else (port_GlDrawBlits) */
    PortGlBlit blit[BLITS_MAX];
    s32 blit_count;
    s32 display_x;
    s32 display_y;
    s32 display_w;
    s32 display_h;
    s32 display_is_on;
    s32 display_is_rgb24; /* the movie's mode */
    u32 frame_time;
    u32 vertex_count;
    u32 draw_count;
    u32 upload_count;
    u32 dropped;
    u32 outside; /* vertices outside the console's own 640 pixels */
    u32 wide;    /* runs of primitives drawn across the whole width */
    u32 shown_outside;
    u32 shown_wide;
    u32 shown_dropped;
    u32 shown_vertices;
    u32 shown_draws;
    u32 shown_uploads;
    /* the colour the game last cleared its picture to, as the picture
     * holds it: 0 until it has cleared one (gl.h) */
    u32 background;
    char problem[256];
} g_gl;

/* ---- the shaders ---- */

static const char g_gl_vertex_text[] =
    "#version 330 core\n"
    "layout(location = 0) in vec3 a_place;\n" /* x, y in video memory's
                                               * pixels; z is w */
    "layout(location = 1) in vec2 a_texture;\n"
    "layout(location = 2) in uvec4 a_color;\n" /* red, green, blue, flags */
    "layout(location = 3) in ivec4 a_page;\n"
    "layout(location = 4) in uvec4 a_info;\n"
    "layout(location = 5) in uvec4 a_window;\n"
    "layout(location = 6) in float a_slot;\n"
    "uniform vec2 u_target;\n"
    "uniform vec2 u_scale;\n"
    "flat out float g_slot;\n"
    /* the texture in perspective, and as the console had it */
    "out vec2 g_texture;\n"
    "noperspective out vec2 g_texture_flat;\n"
    /* a colour changes evenly across the screen, as the chip's does */
    "noperspective out vec3 g_color;\n"
    "flat out uvec4 g_flags;\n"
    "flat out ivec4 g_page;\n"
    "flat out uvec4 g_info;\n"
    "flat out uvec4 g_window;\n"
    /* the corner in video memory's pixels, for the chip's own rule
     * about which pixels a flat polygon covers (the geometry shader) */
    "noperspective out vec2 g_place;\n"
    "void main() {\n"
    /* a pixel of video memory is `scale` pixels of the buffer, and the
     * chip's sample point is the pixel's own whole coordinate: so the
     * middle of buffer pixel (i, j) is video memory's (i / scale,
     * j / scale) */
    "    vec2 at = a_place.xy * u_scale + vec2(0.5);\n"
    "    vec2 ndc = at / u_target * 2.0 - vec2(1.0);\n"
    "    float w = a_place.z;\n"
    /* video memory's lines run the same way as the buffer's, not the
     * other way: mirroring them would turn the rule that says which
     * pixels an edge belongs to upside down, and move every primitive
     * a line. The picture is read back from the top line on. */
    "    gl_Position = vec4(ndc.x * w, ndc.y * w, 0.0, w);\n"
    "    g_texture = a_texture;\n"
    "    g_texture_flat = a_texture;\n"
    "    g_color = vec3(a_color.xyz);\n"
    "    g_flags = a_color;\n"
    "    g_page = a_page;\n"
    "    g_info = a_info;\n"
    "    g_window = a_window;\n"
    "    g_slot = a_slot;\n"
    "    g_place = a_place.xy;\n"
    "}\n";

/* A flat polygon -- the 2D layer's, drawn by whole pixels of video
 * memory -- is the chip's own picture at a larger scale, and that means
 * its edges too: the chip decides which of its pixels a triangle covers
 * by its own rule (src/port/raster.c, port_DrawTriangle), once for each
 * pixel of video memory, and a block of the picture is then all of that
 * triangle or none of it. The card's own rule, applied at the picture's
 * resolution, cuts a block that a slanting edge crosses in two, and two
 * triangles of a quad whose texture does not run evenly across the
 * diagonal leave a seam of broken blocks between them (`--gl-blocks`).
 *
 * So such a triangle is drawn as the rectangle about it, and the fragment
 * shader keeps a block only where the chip's rule puts the block's own
 * pixel inside the triangle, and gives it the colour and the texture
 * point the chip's own arithmetic gives that pixel (worked out here once
 * for the triangle, in whole numbers, as raster.c works them out). A
 * texture stepped back from the picture's pixel by derivatives, which is
 * what a rectangle still has, came out a texel off along the diagonal of
 * a quad, where the two triangles' steps are not the same.
 * Everything else passes through as it came.
 *
 * (The inputs are read with constant indices only: Mesa's software
 * driver read `g_color[i]` with `i` a variable wrongly.) */
static const char g_gl_geometry_text[] =
    "#version 330 core\n"
    "layout(triangles) in;\n"
    "layout(triangle_strip, max_vertices = 4) out;\n"
    "uniform vec2 u_target;\n"
    "uniform vec2 u_scale;\n"
    /* the software GPU draws as the emulator does (port_gpu.h) */
    "uniform int u_like_emulator;\n"
    "flat in float g_slot[];\n"
    "in vec2 g_texture[];\n"
    "noperspective in vec2 g_texture_flat[];\n"
    "noperspective in vec3 g_color[];\n"
    "flat in uvec4 g_flags[];\n"
    "flat in ivec4 g_page[];\n"
    "flat in uvec4 g_info[];\n"
    "flat in uvec4 g_window[];\n"
    "noperspective in vec2 g_place[];\n"
    "flat out float v_slot;\n"
    "out vec2 v_texture;\n"
    "noperspective out vec2 v_texture_flat;\n"
    "noperspective out vec3 v_color;\n"
    "flat out uvec4 v_flags;\n"
    "flat out ivec4 v_page;\n"
    "flat out uvec4 v_info;\n"
    "flat out uvec4 v_window;\n"
    /* the triangle's three corners in whole pixels of video memory, and
     * whether the chip's rule is applied at all (the fragment shader) */
    "flat out ivec4 v_corners;\n"
    "flat out ivec4 v_corner;\n"
    /* the rectangle of texels a triangle of the scene reads, its texture
     * points' least and most, half a texel out to the edges of those
     * texels: a mod's picture of its page is read inside it (gl_scene.h) */
    "flat out vec4 v_box;\n"
    /* ... and the chip's own arithmetic for the five values a pixel of it
     * has -- red, green, blue, u, v -- as src/port/raster.c works it out
     * once for the triangle: each value's change for a pixel across and
     * for a pixel down, in twelve bits of fraction, and where it starts,
     * at the corner furthest left (which `v_corner.w` says) with half a
     * step added. The fifth value's three are in `v_last`. */
    "flat out ivec4 v_across;\n"
    "flat out ivec4 v_down;\n"
    "flat out ivec4 v_start;\n"
    "flat out ivec4 v_last;\n"
    "void same(int i) {\n"
    "    v_slot = g_slot[i];\n"
    "    v_flags = g_flags[i];\n"
    "    v_page = g_page[i];\n"
    "    v_info = g_info[i];\n"
    "    v_window = g_window[i];\n"
    "}\n"
    /* a corner's five values: the vertex's texture point has half a texel
     * added (port_GlPutVertex) */
    "void values_of(vec3 color, vec2 point, out int value[5]) {\n"
    "    ivec3 c = ivec3(round(color));\n"
    "    ivec2 t = ivec2(round(point - vec2(0.5))) & 255;\n"
    "    value = int[5](c.r, c.g, c.b, t.x, t.y);\n"
    "}\n"
    /* a whole number divided as C divides one, toward nothing */
    "int divide(int a, int b) {\n"
    "    int q = abs(a) / abs(b);\n"
    "    return ((a < 0) != (b < 0)) ? -q : q;\n"
    "}\n"
    "void main() {\n"
    "    if ((g_info[0].w & 1u) == 0u) {\n"
    "        vec2 least = min(min(g_texture[0], g_texture[1]), g_texture[2]);\n"
    "        vec2 most = max(max(g_texture[0], g_texture[1]), g_texture[2]);\n"
    "        for (int i = 0; i < 3; i++) {\n"
    "            same(i);\n"
    "            v_box = vec4(least - vec2(0.5), most + vec2(0.5));\n"
    "            v_texture = g_texture[i];\n"
    "            v_texture_flat = g_texture_flat[i];\n"
    "            v_color = g_color[i];\n"
    "            v_corners = ivec4(0);\n"
    "            v_corner = ivec4(0);\n"
    "            v_across = ivec4(0);\n"
    "            v_down = ivec4(0);\n"
    "            v_start = ivec4(0);\n"
    "            v_last = ivec4(0);\n"
    "            gl_Position = gl_in[i].gl_Position;\n"
    "            EmitVertex();\n"
    "        }\n"
    "        EndPrimitive();\n"
    "        return;\n"
    "    }\n"
    "    vec2 p0 = g_place[0];\n"
    "    vec2 p1 = g_place[1];\n"
    "    vec2 p2 = g_place[2];\n"
    "    float area = (p1.x - p0.x) * (p2.y - p0.y)\n"
    "               - (p2.x - p0.x) * (p1.y - p0.y);\n"
    "    ivec2 c0 = ivec2(round(p0));\n"
    "    ivec2 c1 = ivec2(round(p1));\n"
    "    ivec2 c2 = ivec2(round(p2));\n"
    "    int whole = (c1.x - c0.x) * (c2.y - c0.y) - (c2.x - c0.x) * (c1.y - c0.y);\n"
    /* of two as far left, the later */
    "    int origin = 0;\n"
    "    if (c1.x <= c0.x) {\n"
    "        origin = c2.x <= c1.x ? 2 : 1;\n"
    "    } else if (c2.x < c0.x) {\n"
    "        origin = 2;\n"
    "    }\n"
    "    int first[5];\n"
    "    int second[5];\n"
    "    int third[5];\n"
    "    values_of(g_color[0], g_texture_flat[0], first);\n"
    "    values_of(g_color[1], g_texture_flat[1], second);\n"
    "    values_of(g_color[2], g_texture_flat[2], third);\n"
    "    int across[5];\n"
    "    int down[5];\n"
    "    int start[5];\n"
    "    for (int k = 0; k < 5; k++) {\n"
    "        int to_second = second[k] - first[k];\n"
    "        int to_third = third[k] - first[k];\n"
    "        int from = origin == 0 ? first[k]\n"
    "                 : origin == 1 ? second[k] : third[k];\n"
    "        int cross_x = to_second * (c2.y - c0.y) - to_third * (c1.y - c0.y);\n"
    "        int cross_y = (c1.x - c0.x) * to_third - (c2.x - c0.x) * to_second;\n"
    "        across[k] = divide(int(uint(cross_x) << 12u), whole);\n"
    "        down[k] = divide(int(uint(cross_y) << 12u), whole);\n"
    "        start[k] = int((uint(from) << 12u) + 2048u);\n"
    /* as the emulator: from the first corner, a half added and the
     * whole number taken (port_InterpolateWhole) */
    "        if (u_like_emulator != 0) {\n"
    "            across[k] = cross_x;\n"
    "            down[k] = cross_y;\n"
    "            start[k] = first[k];\n"
    "        }\n"
    "    }\n"
    "    if (u_like_emulator != 0) { origin = 0; }\n"
    /* a pixel either way beyond the corners: every block the chip's rule
     * may keep is inside it */
    "    vec2 low = floor(min(min(p0, p1), p2)) - vec2(1.0);\n"
    "    vec2 high = ceil(max(max(p0, p1), p2)) + vec2(1.0);\n"
    "    for (int i = 0; i < 4; i++) {\n"
    "        vec2 q = vec2((i & 1) != 0 ? high.x : low.x,\n"
    "                      (i & 2) != 0 ? high.y : low.y);\n"
    "        float b1 = ((q.x - p0.x) * (p2.y - p0.y)\n"
    "                  - (p2.x - p0.x) * (q.y - p0.y)) / area;\n"
    "        float b2 = ((p1.x - p0.x) * (q.y - p0.y)\n"
    "                  - (q.x - p0.x) * (p1.y - p0.y)) / area;\n"
    "        float b0 = 1.0 - b1 - b2;\n"
    "        same(0);\n"
    "        v_box = vec4(0.0);\n"
    "        v_texture = b0 * g_texture[0] + b1 * g_texture[1]\n"
    "                  + b2 * g_texture[2];\n"
    "        v_texture_flat = b0 * g_texture_flat[0]\n"
    "                       + b1 * g_texture_flat[1]\n"
    "                       + b2 * g_texture_flat[2];\n"
    "        v_color = b0 * g_color[0] + b1 * g_color[1] + b2 * g_color[2];\n"
    "        v_corners = ivec4(c0, c1);\n"
    "        v_corner = ivec4(c2, u_like_emulator != 0 ? 2 : 1, origin);\n"
    "        v_across = ivec4(across[0], across[1], across[2], across[3]);\n"
    "        v_down = ivec4(down[0], down[1], down[2], down[3]);\n"
    "        v_start = ivec4(start[0], start[1], start[2], start[3]);\n"
    "        v_last = ivec4(across[4], down[4], start[4], whole);\n"
    /* as the vertex shader places a corner; a flat polygon's w is 1 */
    "        vec2 at = q * u_scale + vec2(0.5);\n"
    "        gl_Position = vec4(at / u_target * 2.0 - vec2(1.0), 0.0, 1.0);\n"
    "        EmitVertex();\n"
    "    }\n"
    "    EndPrimitive();\n"
    "}\n";

static const char g_gl_fragment_text[] =
    "#version 330 core\n"
    "uniform usampler2D u_vram;\n"
    "uniform vec2 u_target;\n"
    "uniform vec2 u_scale;\n"
    "uniform int u_affine;\n" /* the texture flat, as the console */
    "uniform int u_exact;\n"  /* five bits a channel, with the dither */
    "uniform int u_bilinear;\n"
    "uniform int u_minify;\n" /* the texels of a fragment averaged */
    "uniform int u_pass;\n"
    /* a mod's picture in the place of the texture (gl_replace.h), and the
     * texture points of the page it stands for: corner, then size */
    "uniform sampler2D u_replace;\n"
    "uniform int u_replaced;\n"
    "uniform vec4 u_replace_rect;\n"
    /* which of its pixels mix: the game's texel under each (0), all (1)
     * or none (2) */
    "uniform int u_replace_mix;\n"
    /* its colours times this: the tint of a fade (gl_replace.h) */
    "uniform vec3 u_replace_tint;\n"
    /* the depth test against the ground's mesh: what a corner's own
     * depth was divided by to make its w, what a depth in the world's
     * units is divided by to make a value for the depth buffer, and how
     * much nearer than it is a primitive of the scene is taken to be */
    "uniform float u_w_scale;\n"
    "uniform float u_depth_range;\n"
    "uniform float u_depth_bias;\n"
    /* the slot of the face of the ground each pixel shows, and whether
     * the mesh wrote it this frame (port_GlIsOverGround) */
    "uniform usampler2D u_ground_slots;\n"
    "uniform int u_ground_order;\n"
    /* the depth of the fighters drawn from models, and where their layer
     * lies (port_GlFighterLayerDepth) */
    "uniform sampler2D u_fighter_depth;\n"
    "uniform vec4 u_fighter_rect;\n"
    "uniform float u_fighter_bias;\n"
    "flat in float v_slot;\n"
    "in vec2 v_texture;\n"
    "noperspective in vec2 v_texture_flat;\n"
    "noperspective in vec3 v_color;\n"
    "flat in uvec4 v_flags;\n"
    "flat in ivec4 v_page;\n"
    "flat in uvec4 v_info;\n"
    "flat in uvec4 v_window;\n"
    "flat in ivec4 v_corners;\n"
    "flat in ivec4 v_corner;\n"
    "flat in ivec4 v_across;\n"
    "flat in ivec4 v_down;\n"
    "flat in ivec4 v_start;\n"
    "flat in ivec4 v_last;\n"
    "flat in vec4 v_box;\n"
    "out vec4 o_color;\n"
    /* Whether the chip draws pixel `p` of a flat triangle (the geometry
     * shader): as src/port/raster.c draws one, line by line from the
     * highest corner to the line above the lowest, and on each line from
     * the edge on the left to the pixel before the edge on the right.
     * A pixel is past an edge where it is at or after the place the edge
     * crosses its line, and it is drawn where it is past one of the two
     * edges beside its line and not the other. Whole numbers, as the
     * chip's are. */
    "bool chip_covers(ivec2 p) {\n"
    "    ivec2 top = v_corners.xy;\n"
    "    ivec2 middle = v_corners.zw;\n"
    "    ivec2 bottom = v_corner.xy;\n"
    "    ivec2 swap;\n"
    "    if (middle.y < top.y) { swap = top; top = middle; middle = swap; }\n"
    "    if (bottom.y < middle.y) {\n"
    "        swap = middle; middle = bottom; bottom = swap;\n"
    "        if (middle.y < top.y) { swap = top; top = middle; middle = swap; }\n"
    "    }\n"
    "    if (p.y < top.y || p.y >= bottom.y) { return false; }\n"
    "    ivec2 from = p.y < middle.y ? top : middle;\n"
    "    ivec2 to = p.y < middle.y ? middle : bottom;\n"
    "    int long_down = bottom.y - top.y;\n"
    "    int short_down = to.y - from.y;\n"
    "    bool past_long = p.x * long_down\n"
    "        >= top.x * long_down + (p.y - top.y) * (bottom.x - top.x);\n"
    "    bool past_short = p.x * short_down\n"
    "        >= from.x * short_down + (p.y - from.y) * (to.x - from.x);\n"
    "    return past_long != past_short;\n"
    "}\n"
    /* The colour and the texture point the chip gives pixel `p` of a flat
     * triangle -- red, green, blue, u, v -- from its steps across and
     * down, which the geometry shader worked out as the chip does: the
     * whole number of where the value starts and the steps to the pixel,
     * in twelve bits of fraction, wrapping as the chip's does. */
    "void chip_values(ivec2 p, out int value[5]) {\n"
    "    ivec2 origin = v_corner.w == 0 ? v_corners.xy\n"
    "                 : v_corner.w == 1 ? v_corners.zw : v_corner.xy;\n"
    /* as the emulator: each value from the first corner by its changes
     * times the triangle's area, a half added and rounded down
     * (port_InterpolateWhole) */
    "    if (v_corner.z == 2) {\n"
    "        ivec2 d = p - v_corners.xy;\n"
    "        int area = v_last.w;\n"
    "        for (int k = 0; k < 5; k++) {\n"
    "            int start = k < 4 ? v_start[k] : v_last.z;\n"
    "            int change = (k < 4 ? v_across[k] : v_last.x) * d.x\n"
    "                       + (k < 4 ? v_down[k] : v_last.y) * d.y;\n"
    "            int top = 2 * (start * area + change) + area;\n"
    "            int bottom = 2 * area;\n"
    "            if (bottom < 0) { top = -top; bottom = -bottom; }\n"
    "            int q = top >= 0 ? top / bottom\n"
    "                             : -((bottom - 1 - top) / bottom);\n"
    "            value[k] = q & 255;\n"
    "        }\n"
    "        return;\n"
    "    }\n"
    "    uvec2 walk = uvec2(p - origin);\n"
    "    for (int k = 0; k < 4; k++) {\n"
    "        uint sum = uint(v_start[k]) + uint(v_across[k]) * walk.x\n"
    "                 + uint(v_down[k]) * walk.y;\n"
    "        value[k] = int(sum >> 12u) & 255;\n"
    "    }\n"
    "    uint sum = uint(v_last.z) + uint(v_last.x) * walk.x\n"
    "             + uint(v_last.y) * walk.y;\n"
    "    value[4] = int(sum >> 12u) & 255;\n"
    "}\n"
    /* the chip's pattern, by the pixel's place in a four by four square
     * of video memory */
    "const int dither[16] = int[16](-4, 0, -3, 1, 2, -2, 3, -1,\n"
    "                               -3, 1, -4, 0, 3, -1, 2, -2);\n"
    "uint vram(int x, int y) {\n"
    "    return texelFetch(u_vram, ivec2(x & 1023, y & 511), 0).r;\n"
    "}\n"
    /* the texel at (u, v) of this primitive's page, through the texture
     * window and its colour table */
    "uint texel_at(int u, int v) {\n"
    "    u = (u & int(v_window.x)) | int(v_window.y);\n"
    "    v = (v & int(v_window.z)) | int(v_window.w);\n"
    "    int y = v_page.y + v;\n"
    "    if (v_info.x == 0u) {\n"
    "        uint word = vram(v_page.x + (u >> 2), y);\n"
    "        uint index = (word >> ((uint(u) & 3u) * 4u)) & 15u;\n"
    "        return vram(v_page.z + int(index), v_page.w);\n"
    "    }\n"
    "    if (v_info.x == 1u) {\n"
    "        uint word = vram(v_page.x + (u >> 1), y);\n"
    "        uint index = (word >> ((uint(u) & 1u) * 8u)) & 255u;\n"
    "        return vram(v_page.z + int(index), v_page.w);\n"
    "    }\n"
    "    return vram(v_page.x + u, y);\n"
    "}\n"
    "vec3 channels(uint texel) {\n"
    "    return vec3(float(texel & 31u), float((texel >> 5) & 31u),\n"
    "                float((texel >> 10) & 31u));\n"
    "}\n"
    /* a mod's picture of a whole page (gl_scene.h), whose colours follow
     * the table the primitive names as it is now: the fog's row its
     * depth was drawn at, a tint */
    PORT_GL_SCENE_SHADER "vec3 scene_now(uint index) {\n"
    "    return channels(vram(v_page.z + int(index), v_page.w));\n"
    "}\n"
    "void main() {\n"
    /* A fighter's own is hidden by the ground as the console hid him:
     * where the face of the ground this pixel shows was linked at a
     * nearer slot of the ordering table than he was, and so painted over
     * him (port_GlIsOverGround). */
    "    if (u_ground_order != 0 && (v_flags.w & 64u) != 0u\n"
    "        && v_slot >= 0.0\n"
    "        && texelFetch(u_ground_slots, ivec2(gl_FragCoord.xy), 0).r\n"
    "           < uint(v_slot)) {\n"
    "        discard;\n"
    "    }\n"
    /* A cloth of a fighter drawn from a model is hidden where the models
     * are nearer than it (port_GlFighterHolds): the layer they were drawn
     * into kept their depth, in the world's own units, as this does. */
    "    if ((v_info.w & 2u) != 0u) {\n"
    "        ivec2 at = ivec2(gl_FragCoord.xy - u_fighter_rect.xy);\n"
    "        if (all(greaterThanEqual(at, ivec2(0)))\n"
    "            && all(lessThan(at, ivec2(u_fighter_rect.zw)))) {\n"
    "            float body = texelFetch(u_fighter_depth, at, 0).r;\n"
    "            float deep = (u_w_scale / gl_FragCoord.w)\n"
    "                       * exp2(float(v_info.z));\n"
    "            if (deep - u_fighter_bias > body) { discard; }\n"
    "        }\n"
    "    }\n"
    "    vec2 uv = (u_affine != 0) ? v_texture_flat : v_texture;\n"
    /* the texture point as it runs across the primitive, before it is
     * stepped back to the corner of a block: what a mod's picture, which
     * has texels of its own between the console's, is read at */
    "    vec2 smooth_uv = uv;\n"
    "    vec3 color = v_color;\n"
    "    bool mixes = (v_flags.w & 4u) != 0u;\n"
    "    ivec2 pixel = ivec2(floor(gl_FragCoord.xy / u_scale));\n"
    /* A flat primitive is the chip's own picture at a larger scale, and
     * the chip settled its texel and its colour once for each pixel of
     * video memory: so the point they are read at is that pixel's own
     * corner, where the chip's arithmetic began, and not this pixel's
     * middle part of the way across it. Both vary evenly across the
     * primitive (`noperspective`), so a derivative is their exact step
     * and stepping back by the place in the block is exact as well.
     * Without this the right part of every block of a texture is the
     * next texel's, which at eight times the resolution is half the
     * block: the menus come apart (docs/PORT.md, "Remastered").
     *
     * The four derivatives are taken where every fragment takes them,
     * not inside the test: a derivative asked for where the fragments of
     * a block of four do not all ask is undefined, and the block at the
     * edge of a flat primitive holds fragments of another. */
    "    vec2 uv_across = dFdx(v_texture_flat);\n"
    "    vec2 uv_down = dFdy(v_texture_flat);\n"
    "    vec3 color_across = dFdx(v_color);\n"
    "    vec3 color_down = dFdy(v_color);\n"
    /* How far across the texture this fragment reaches, each way: the
     * texture point in perspective, stepped as the screen steps. In the
     * distance it is several texels, and reading one of them is the
     * noise that crawls as the camera turns ("the floor's shimmer"). */
    "    vec2 wide_across = dFdx(v_texture);\n"
    "    vec2 wide_down = dFdy(v_texture);\n"
    /* a flat triangle's block the chip would not draw (the geometry
     * shader), now that every derivative is taken */
    "    if (v_corner.z != 0 && !chip_covers(pixel)) {\n"
    "        discard;\n"
    "    }\n"
    /* So this many points of it are read, spread over that reach: one
     * where a fragment covers a texel or less, which is every fragment
     * of anything near, and up to four each way where it covers more.
     * Only a polygon of the scene, drawn in perspective, is read this
     * way: the 2D layer is the console's own pixels, block for block,
     * and the pass that averages the samples down leaves it so. */
    "    int steps = 1;\n"
    "    if (u_minify != 0 && (v_flags.w & 32u) != 0u) {\n"
    "        float span = max(max(abs(wide_across.x), abs(wide_across.y)),\n"
    "                         max(abs(wide_down.x), abs(wide_down.y)));\n"
    "        steps = int(clamp(ceil(span), 1.0, 4.0));\n"
    "    }\n"
    "    if ((v_flags.w & 16u) != 0u) {\n"
    "        vec2 back = mod(floor(gl_FragCoord.xy), u_scale);\n"
    "        uv = v_texture_flat - back.x * uv_across - back.y * uv_down;\n"
    "        color -= back.x * color_across + back.y * color_down;\n"
    "    }\n"
    /* ... and a flat triangle's colour and texture point are the chip's
     * own for its pixel, worked out as the chip works them out */
    "    if (v_corner.z != 0) {\n"
    "        int value[5];\n"
    "        chip_values(pixel, value);\n"
    "        color = vec3(float(value[0]), float(value[1]), float(value[2]));\n"
    "        uv = vec2(float(value[3]), float(value[4])) + vec2(0.5);\n"
    "    }\n"
    /* the chip works in whole numbers: a colour that comes out of the
     * interpolation as 203.99999 is the 204 it means */
    "    if (u_exact != 0) { color = floor(color + vec3(0.5)); }\n"
    "    float shade = 1.0;\n"
    "    if ((v_flags.w & 1u) != 0u) {\n"
    "        uint texel = 0u;\n"
    "        vec3 texel_color = vec3(0.0);\n"
    "        float weight = 0.0;\n"
    "        int layer = (v_flags.w & 128u) != 0u\n"
    "                  ? scene_layer(v_page, v_info.x) : -1;\n"
    "        if (layer >= 0) {\n"
    /* The mod's picture of the page, smoothed and mipmapped, read inside
     * the triangle's own texels; whether it mixes is still the game's
     * texel's to say */
    "            vec4 got = scene_color(layer, v_page, v_info.x, uv,\n"
    "                                   wide_across, wide_down, v_box);\n"
    "            if (got.a < 0.5) { discard; }\n"
    "            texel_color = got.rgb;\n"
    "            texel = texel_at(int(floor(uv.x)), int(floor(uv.y)));\n"
    "            weight = 1.0;\n"
    "        } else if (u_replaced != 0) {\n"
    /* The mod's picture, smoothed. A texture point names the texel it
     * is in, half a texel in from the corner the chip begins at, and a
     * pixel of the picture is a block of `scale` of video memory's: so
     * the point less that half, and half a pixel of the block on, is
     * where in the region this pixel's middle is. Its alpha says what is
     * clear, its colours were kept multiplied by it, and its texel of
     * thirty-one to a channel is the console's own. Whether it mixes is
     * still the console's texel's to say. */
    "            vec2 step_across = (u_affine != 0) ? uv_across : wide_across;\n"
    "            vec2 step_down = (u_affine != 0) ? uv_down : wide_down;\n"
    "            vec2 at = (smooth_uv - vec2(0.5) + vec2(0.5) / u_scale\n"
    "                       - u_replace_rect.xy) / u_replace_rect.zw;\n"
    "            vec4 got = textureGrad(u_replace, at,\n"
    "                                   step_across / u_replace_rect.zw,\n"
    "                                   step_down / u_replace_rect.zw);\n"
    "            if (got.a < 0.5) { discard; }\n"
    "            texel_color = got.rgb / got.a * 31.0 * u_replace_tint;\n"
    "            texel = texel_at(int(floor(uv.x)), int(floor(uv.y)));\n"
    "            if (u_replace_mix == 1) { texel = 32768u; }\n"
    "            if (u_replace_mix == 2) { texel = 1u; }\n"
    "            weight = 1.0;\n"
    "        } else if (u_bilinear != 0) {\n"
    /* four texels about the point, each one that is nothing left out,
     * so that a page's edge does not bleed */
    "            vec2 at = uv - vec2(0.5);\n"
    "            vec2 part = fract(at);\n"
    "            ivec2 first = ivec2(floor(at));\n"
    "            float best = -1.0;\n"
    "            for (int j = 0; j < 2; j++) {\n"
    "                for (int i = 0; i < 2; i++) {\n"
    "                    uint one = texel_at(first.x + i, first.y + j);\n"
    "                    float how = (i == 0 ? 1.0 - part.x : part.x)\n"
    "                              * (j == 0 ? 1.0 - part.y : part.y);\n"
    "                    if (one != 0u) {\n"
    "                        texel_color += channels(one) * how;\n"
    "                        weight += how;\n"
    "                    }\n"
    "                    if (how > best) { best = how; texel = one; }\n"
    "                }\n"
    "            }\n"
    "            if (weight < 0.004) { discard; }\n"
    "            texel_color /= weight;\n"
    "        } else if (steps > 1) {\n"
    /* The texels this fragment covers, averaged: `steps` by `steps`
     * points spread evenly over the area the texture point steps across
     * and down, each read through this primitive's own texture window
     * and colour table, so that nothing is read outside the page the
     * chip would have read in. A texel that is nothing is left out of
     * the average as it is of the four about a point, and the fragment
     * is dropped only where every one of them is nothing. */
    "            vec3 sum = vec3(0.0);\n"
    "            uint any = 0u;\n"
    "            float part = 1.0 / float(steps);\n"
    "            int i;\n"
    "            int j;\n"
    "            texel = texel_at(int(floor(uv.x)), int(floor(uv.y)));\n"
    "            for (j = 0; j < steps; j++) {\n"
    "                for (i = 0; i < steps; i++) {\n"
    "                    vec2 off = (vec2(float(i), float(j)) + vec2(0.5))\n"
    "                             * part - vec2(0.5);\n"
    "                    vec2 at = uv + off.x * wide_across\n"
    "                            + off.y * wide_down;\n"
    "                    uint one = texel_at(int(floor(at.x)),\n"
    "                                        int(floor(at.y)));\n"
    "                    if (one != 0u) {\n"
    "                        sum += channels(one);\n"
    "                        weight += 1.0;\n"
    "                        any = one;\n"
    "                    }\n"
    "                }\n"
    "            }\n"
    "            if (weight < 0.5) { discard; }\n"
    "            if (texel == 0u) { texel = any; }\n"
    "            texel_color = sum / weight;\n"
    "        } else {\n"
    "            texel = texel_at(int(floor(uv.x)), int(floor(uv.y)));\n"
    "            if (texel == 0u) { discard; }\n"
    "            texel_color = channels(texel);\n"
    "            weight = 1.0;\n"
    "        }\n"
    /* only the texels whose top bit is set mix with what is there */
    "        mixes = mixes && (texel & 32768u) != 0u;\n"
    "        if ((v_flags.w & 2u) != 0u) {\n"
    "            color = texel_color * 255.0 / 31.0;\n"
    "        } else {\n"
    /* a texel times the colour, 128 being one: the chip drops what is
     * left over, so nothing is rounded up here */
    "            color = texel_color * color / 16.0;\n"
    "            if (u_exact != 0) { color = floor(color); }\n"
    "        }\n"
    "    }\n"
    "    if (u_exact != 0) {\n"
    "        float step = ((v_flags.w & 8u) != 0u)\n"
    "                   ? float(dither[(pixel.y & 3) * 4 + (pixel.x & 3)])\n"
    "                   : 0.0;\n"
    "        ivec3 five = ivec3(clamp(floor(color) + step,\n"
    "                                vec3(0.0), vec3(255.0))) >> 3;\n"
    "        color = vec3(five * 255 / 31);\n"
    "    } else {\n"
    "        color = clamp(color, vec3(0.0), vec3(255.0));\n"
    "    }\n"
    /* the alpha says how much of what is there to keep: the four mixes
     * are made of it and the blending the draw was set up with */
    "    if (mixes) {\n"
    "        if (v_info.y == 0u) { shade = 0.5; }\n"
    "        else if (v_info.y == 3u) { color *= 0.25; shade = 0.0; }\n"
    "        else { shade = 0.0; }\n"
    "    }\n"
    "    if (u_pass == 1 && !mixes) { discard; }\n"
    "    if (u_pass == 2 && mixes) { discard; }\n"
    "    o_color = vec4(color / 255.0, shade);\n"
    /* How deep this fragment is, for the test against the ground's own
     * mesh (gl_stage.h). The coprocessor's depth for a corner is the
     * primitive's own w, and the card interpolates one over w evenly
     * across the screen, so taking it back from gl_FragCoord is the
     * depth in perspective, which is what a straight line through the
     * world has. The scale the renderer drew the thing at is taken off
     * (`v_info.z`), leaving a depth in the world's own units, and the
     * offset keeps what lies on the ground from being eaten by it.
     *
     * A primitive the side channel has no depth for -- the 2D layer, the
     * fonts, the menus, a polygon of the scene whose corners were not
     * written down -- is put at the very front: it was drawn where the
     * console drew it and has nothing to be behind. */
    "    if ((v_flags.w & 96u) == 32u) {\n"
    "        float deep = (u_w_scale / gl_FragCoord.w) * exp2(float(v_info.z));\n"
    "        gl_FragDepth = clamp((deep - u_depth_bias) / u_depth_range,\n"
    "                             0.0, 1.0);\n"
    "    } else {\n"
    "        gl_FragDepth = 0.0;\n"
    "    }\n"
    "}\n";

/* The pass that stretches a rectangle of the mirror of video memory
 * into the colour buffer: what a fill, a copy or a load left there. */
static const char g_gl_blit_vertex_text[] =
    "#version 330 core\n"
    "layout(location = 0) in vec2 a_corner;\n" /* 0 or 1, each way */
    "uniform vec4 u_rect;\n" /* left, top, width, height, in video memory */
    "uniform vec2 u_target;\n"
    "uniform vec2 u_scale;\n"
    /* how far right of its own place in video memory it is drawn: the
     * margin of a wider picture. What is read is not moved with it. */
    "uniform float u_shift;\n"
    "out vec2 v_at;\n"
    "void main() {\n"
    "    vec2 at = u_rect.xy + a_corner * u_rect.zw;\n"
    "    vec2 place = (at + vec2(u_shift, 0.0)) * u_scale;\n"
    "    vec2 ndc = place / u_target * 2.0 - vec2(1.0);\n"
    "    gl_Position = vec4(ndc.x, ndc.y, 0.0, 1.0);\n"
    "    v_at = at;\n"
    "}\n";

static const char g_gl_blit_fragment_text[] =
    "#version 330 core\n"
    "uniform usampler2D u_vram;\n"
    /* a mod's picture in the place of the rectangle (gl_replace.h) */
    "uniform sampler2D u_replace;\n"
    "uniform int u_replaced;\n"
    "uniform vec3 u_tint;\n" /* the tint of its fade */
    "uniform vec4 u_rect;\n"
    "in vec2 v_at;\n"
    "out vec4 o_color;\n"
    "void main() {\n"
    "    if (u_replaced != 0) {\n"
    "        vec4 got = texture(u_replace, (v_at - u_rect.xy) / u_rect.zw);\n"
    "        o_color = vec4(got.rgb / max(got.a, 1.0 / 255.0) * u_tint, 1.0);\n"
    "        return;\n"
    "    }\n"
    "    ivec2 at = ivec2(floor(v_at));\n"
    "    uint pixel = texelFetch(u_vram, ivec2(at.x & 1023, at.y & 511), 0).r;\n"
    "    ivec3 five = ivec3(int(pixel & 31u), int((pixel >> 5) & 31u),\n"
    "                       int((pixel >> 10) & 31u));\n"
    "    o_color = vec4(vec3(five * 255 / 31) / 255.0, 1.0);\n"
    "}\n";

/* The pass that averages the colour buffer down into the picture: a
 * block of `sample` by `sample` of it becomes one pixel. This is what
 * smooths the scene's edges, and it leaves the 2D layer as crisp as it
 * was: a pixel of video memory is a block of `scale * sample` pixels of
 * one colour, and every block averaged lies inside one of those. With
 * `sample` 1 the pass is not run at all and the picture is read from the
 * colour buffer itself, so the proof at scale 1 is of the same pixels it
 * always was. */
static const char g_gl_resolve_vertex_text[] =
    "#version 330 core\n"
    "layout(location = 0) in vec2 a_corner;\n" /* 0 or 1, each way */
    "void main() {\n"
    "    gl_Position = vec4(a_corner * 2.0 - vec2(1.0), 0.0, 1.0);\n"
    "}\n";

static const char g_gl_resolve_fragment_text[] =
    "#version 330 core\n"
    "uniform sampler2D u_color;\n"
    "uniform int u_sample;\n"
    "uniform vec2 u_at;\n" /* the picture's corner in the colour buffer */
    "out vec4 o_color;\n"
    "void main() {\n"
    "    ivec2 first = ivec2(u_at) + ivec2(floor(gl_FragCoord.xy)) * u_sample;\n"
    /* Whole numbers, so that a block of one colour comes out as that
     * colour whatever the number of samples: a division in floating
     * point by anything but a power of two moves some values a step, and
     * the 2D layer must not move at all. */
    "    int many = u_sample * u_sample;\n"
    "    ivec3 sum = ivec3(0);\n"
    "    for (int j = 0; j < u_sample; j++) {\n"
    "        for (int i = 0; i < u_sample; i++) {\n"
    "            vec3 one = texelFetch(u_color, first + ivec2(i, j), 0).rgb;\n"
    "            sum += ivec3(one * 255.0 + vec3(0.5));\n"
    "        }\n"
    "    }\n"
    "    o_color = vec4(vec3((sum + ivec3(many / 2)) / many) / 255.0, 1.0);\n"
    "}\n";

/* The pass that puts a picture into the window: a rectangle of a texture
 * into a rectangle of the window's own framebuffer, which is the only
 * thing drawn there (src/port/window/sdl_window.c). Both rectangles are
 * counted in pixels from the top left, as the picture's own lines run.
 * `u_over` keeps the texture's alpha, for the counter in the corner. */
static const char g_gl_show_vertex_text[] =
    "#version 330 core\n"
    "layout(location = 0) in vec2 a_corner;\n" /* 0 or 1, each way */
    "uniform vec4 u_dest;\n"                   /* in the window's pixels */
    "uniform vec2 u_window;\n"
    "uniform vec4 u_source;\n" /* in the texture's texels */
    "uniform vec2 u_texture;\n"
    "out vec2 v_at;\n"
    "void main() {\n"
    "    vec2 at = u_dest.xy + a_corner * u_dest.zw;\n"
    /* the window's lines run down from the top, the framebuffer's up */
    "    gl_Position = vec4(at.x / u_window.x * 2.0 - 1.0,\n"
    "                       1.0 - at.y / u_window.y * 2.0, 0.0, 1.0);\n"
    "    v_at = (u_source.xy + a_corner * u_source.zw) / u_texture;\n"
    "}\n";

static const char g_gl_show_fragment_text[] =
    "#version 330 core\n"
    "uniform sampler2D u_picture;\n"
    "uniform int u_over;\n"
    "in vec2 v_at;\n"
    "out vec4 o_color;\n"
    "void main() {\n"
    "    vec4 texel = texture(u_picture, v_at);\n"
    "    o_color = vec4(texel.rgb, u_over != 0 ? texel.a : 1.0);\n"
    "}\n";

/* ---- starting ---- */

static void port_GlSay(const char *what, const char *more) {
    u32 at = 0;
    u32 i;

    for (i = 0; what[i] != 0 && at + 1 < sizeof(g_gl.problem); i++) {
        g_gl.problem[at++] = what[i];
    }
    for (i = 0; more != NULL && more[i] != 0 && at + 1 < sizeof(g_gl.problem);
         i++) {
        g_gl.problem[at++] = more[i];
    }
    g_gl.problem[at] = 0;
}

static s32 port_GlFindFunctions(void *(*getproc)(const char *name)) {
#define GL(result, name, args)                                                 \
    name = (result(*) args)getproc(#name);                                     \
    if (name == NULL) {                                                        \
        port_GlSay("OpenGL has no ", #name);                                   \
        return FALSE;                                                          \
    }
    PORT_GL_FUNCTIONS
#undef GL
    return TRUE;
}

static GLuint port_GlMakeShader(GLenum kind, const char *text) {
    GLuint shader = glCreateShader(kind);
    GLint ok = GL_FALSE;

    glShaderSource(shader, 1, &text, NULL);
    glCompileShader(shader);
    glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        static char log[2048];

        glGetShaderInfoLog(shader, (GLsizei)sizeof(log), NULL, log);
        port_GlSay("a shader does not compile: ", log);
        return 0;
    }
    return shader;
}

/* A program of a vertex shader, a geometry shader where `geometry` is not
 * NULL, and a fragment shader. */
static GLuint port_GlMakeProgramWith(
    const char *vertex, const char *geometry, const char *fragment) {
    GLuint program;
    GLuint one = port_GlMakeShader(GL_VERTEX_SHADER, vertex);
    GLuint two = port_GlMakeShader(GL_FRAGMENT_SHADER, fragment);
    GLuint three =
        geometry != NULL ? port_GlMakeShader(GL_GEOMETRY_SHADER, geometry) : 0;
    GLint ok = GL_FALSE;

    if (one == 0 || two == 0 || (geometry != NULL && three == 0)) {
        return 0;
    }
    program = glCreateProgram();
    glAttachShader(program, one);
    glAttachShader(program, two);
    if (three != 0) {
        glAttachShader(program, three);
    }
    glLinkProgram(program);
    glGetProgramiv(program, GL_LINK_STATUS, &ok);
    if (!ok) {
        static char log[2048];

        glGetProgramInfoLog(program, (GLsizei)sizeof(log), NULL, log);
        port_GlSay("the shaders do not link: ", log);
        return 0;
    }
    return program;
}

static GLuint port_GlMakeProgram(const char *vertex, const char *fragment) {
    return port_GlMakeProgramWith(vertex, NULL, fragment);
}

static void port_GlSetUpVertexFormat(void) {
    const PortGlVertex *one = NULL;

    glBindVertexArray(g_gl.array);
    glBindBuffer(GL_ARRAY_BUFFER, g_gl.buffer);
    glVertexAttribPointer(
        0, 3, GL_FLOAT, GL_FALSE, (GLsizei)sizeof(*one), &one->x);
    glVertexAttribPointer(
        1, 2, GL_FLOAT, GL_FALSE, (GLsizei)sizeof(*one), &one->u);
    glVertexAttribIPointer(
        2, 4, GL_UNSIGNED_BYTE, (GLsizei)sizeof(*one), one->color);
    glVertexAttribIPointer(3, 4, GL_SHORT, (GLsizei)sizeof(*one), one->page);
    glVertexAttribIPointer(
        4, 4, GL_UNSIGNED_BYTE, (GLsizei)sizeof(*one), one->info);
    glVertexAttribIPointer(
        5, 4, GL_UNSIGNED_BYTE, (GLsizei)sizeof(*one), one->window);
    glVertexAttribPointer(
        6, 1, GL_FLOAT, GL_FALSE, (GLsizei)sizeof(*one), &one->slot);
    glEnableVertexAttribArray(0);
    glEnableVertexAttribArray(1);
    glEnableVertexAttribArray(2);
    glEnableVertexAttribArray(3);
    glEnableVertexAttribArray(4);
    glEnableVertexAttribArray(5);
    glEnableVertexAttribArray(6);
}

/* The colour buffer: all of video memory at the scale. */
/* The depth buffer beside the colour buffer, attached only where the
 * ground's mesh will write it (gl_stage.h). It is not attached
 * otherwise, and not because it costs anything: Mesa's software driver
 * takes another path through its rasteriser when a framebuffer has a
 * depth attachment at all, and a few hundred pixels of a fight picture
 * then land on the other side of a half-texel. The picture at scale 1
 * is held to the software GPU's pixel for pixel (`--gl-exact`,
 * docs/PORT.md, "How it is proved, with nothing shown"), so a run that
 * draws no mesh must have no depth buffer either. */
static void port_GlAttachDepth(void) {
    s32 wanted = g_gl.options.is_stage_mesh && !g_gl.options.is_exact;

    glBindFramebuffer(GL_FRAMEBUFFER, g_gl.framebuffer);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT,
                              GL_RENDERBUFFER, wanted ? g_gl.depth_buffer : 0);
}

/* The ground's mesh writes, beside its colour, the ordering-table slot of
 * the face each pixel shows (gl_stage.c): the buffer it goes in is
 * attached as a second colour buffer for that draw alone, so that the
 * scene's primitives can read it after and nothing else ever writes it.
 * Where the mesh draws nothing it is farther than any slot. */
void port_GlGroundSlotsBegin(void) {
    static const GLenum both[2] = {GL_COLOR_ATTACHMENT0, GL_COLOR_ATTACHMENT1};
    static const GLuint far[4] = {0xFFFF, 0, 0, 0};

    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT1, GL_TEXTURE_2D,
                           g_gl.slot_texture, 0);
    glDrawBuffers(2, both);
    glClearBufferuiv(GL_COLOR, 1, far);
}

void port_GlGroundSlotsEnd(void) {
    static const GLenum one[1] = {GL_COLOR_ATTACHMENT0};

    glDrawBuffers(1, one);
    glFramebufferTexture2D(
        GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT1, GL_TEXTURE_2D, 0, 0);
}

static s32 port_GlMakeTarget(void) {
    g_gl.draw_x = g_gl.scale_x * g_gl.sample;
    g_gl.draw_y = g_gl.scale_y * g_gl.sample;
    g_gl.target_w = PORT_VRAM_WIDTH * g_gl.draw_x;
    g_gl.target_h = PORT_VRAM_HEIGHT * g_gl.draw_y;
    glBindTexture(GL_TEXTURE_2D, g_gl.color_texture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, g_gl.target_w, g_gl.target_h, 0,
                 GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindRenderbuffer(GL_RENDERBUFFER, g_gl.depth_buffer);
    glRenderbufferStorage(
        GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, g_gl.target_w, g_gl.target_h);
    glBindTexture(GL_TEXTURE_2D, g_gl.slot_texture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_R16UI, g_gl.target_w, g_gl.target_h, 0,
                 GL_RED_INTEGER, GL_UNSIGNED_SHORT, NULL);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glBindFramebuffer(GL_FRAMEBUFFER, g_gl.framebuffer);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D,
                           g_gl.color_texture, 0);
    port_GlAttachDepth();
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
        port_GlSay("the colour buffer cannot be drawn into", NULL);
        return FALSE;
    }
    glViewport(0, 0, g_gl.target_w, g_gl.target_h);
    return TRUE;
}

/* ---- the mirror of video memory ---- */

static void port_GlUploadMirror(void) {
    s32 w;
    s32 h;

    if (g_gl.mirror.is_empty) {
        return;
    }
    w = g_gl.mirror.right - g_gl.mirror.left + 1;
    h = g_gl.mirror.bottom - g_gl.mirror.top + 1;
    glBindTexture(GL_TEXTURE_2D, g_gl.vram_texture);
    glPixelStorei(GL_UNPACK_ROW_LENGTH, PORT_VRAM_WIDTH);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 2);
    glTexSubImage2D(
        GL_TEXTURE_2D, 0, g_gl.mirror.left, g_gl.mirror.top, w, h,
        GL_RED_INTEGER, GL_UNSIGNED_SHORT,
        &g_gl.staged[g_gl.mirror.top * PORT_VRAM_WIDTH + g_gl.mirror.left]);
    glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
    g_gl.upload_count += (u32)(w * h);
    port_ClearDirtyRect(&g_gl.mirror);
}

/* A rectangle of video memory, as it is now, to be sent to the card: it
 * is copied into the backend's own copy at once, and sent with the rest
 * the next time the card reads it (port_GlUploadMirror). A rectangle
 * that goes round an edge is taken as the whole of that way, as
 * port_AddDirtyRect takes it. */
static void port_GlMarkMirror(s32 x, s32 y, s32 w, s32 h) {
    PortDirtyRect rect;
    s32 row;

    if (w <= 0 || h <= 0) {
        return;
    }
    port_ClearDirtyRect(&rect);
    port_AddDirtyRect(&rect, x, y, w, h);
    for (row = rect.top; row <= rect.bottom; row++) {
        memcpy(&g_gl.staged[row * PORT_VRAM_WIDTH + rect.left],
               &g_port_vram[row * PORT_VRAM_WIDTH + rect.left],
               (size_t)(rect.right - rect.left + 1) * sizeof(u16));
    }
    port_AddDirtyRect(&g_gl.mirror, x, y, w, h);
}

/* ---- the batches ---- */

/* The picture's margin for the shape asked for and the display's
 * rectangle as it is (port_remaster.h). */
static void port_GlFindMargin(void) {
    g_gl.margin = port_RemasterMargin(
        g_gl.display_w, g_gl.options.aspect_w, g_gl.options.aspect_h);
}

/* The scissor a batch of this state is drawn through, in the colour
 * buffer's own pixels. */
static void port_GlScissorOf(
    const PortGlState *state, s32 *x, s32 *y, s32 *w, s32 *h) {
    *y = state->clip_top * g_gl.draw_y;
    *h = (state->clip_bottom - state->clip_top + 1) * g_gl.draw_y;
    if (state->is_wide) {
        /* the scene, across the whole of a wider picture */
        *x = g_gl.display_x * g_gl.draw_x;
        *w = (g_gl.display_w + 2 * g_gl.margin) * g_gl.draw_x;
        return;
    }
    *x = (state->clip_left + g_gl.margin) * g_gl.draw_x;
    *w = (state->clip_right - state->clip_left + 1) * g_gl.draw_x;
}

static void port_GlSetState(const PortGlState *state) {
    s32 x;
    s32 y;
    s32 w;
    s32 h;

    if (state->is_half) {
        glEnable(GL_BLEND);
        switch (state->blend) {
        case PORT_REMASTER_BLEND_HALF:
            glBlendEquation(GL_FUNC_ADD);
            glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
            break;
        case PORT_REMASTER_BLEND_SUBTRACT:
            glBlendEquation(GL_FUNC_REVERSE_SUBTRACT);
            glBlendFunc(GL_ONE, GL_ONE);
            break;
        default: /* added, and a quarter added: the shader has scaled it */
            glBlendEquation(GL_FUNC_ADD);
            glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
            break;
        }
    } else {
        glDisable(GL_BLEND);
    }
    port_GlScissorOf(state, &x, &y, &w, &h);
    glScissor(x, y, w, h);
}

/* What a draw of the picture's own program needs, whichever buffer it
 * draws into. */
/* What a cloth of a fighter drawn from a model is held against: the
 * depth of the layer the models were drawn into in this picture
 * (port_GlFighterHolds). With no layer laid the rectangle is empty and
 * nothing is held. */
static void port_GlSetFighterDepth(void) {
    s32 rect[4] = {0, 0, 0, 0};
    u32 texture = (u32)port_GlFighterLayerDepth(rect);

    glActiveTexture(GL_TEXTURE0 + FIGHTER_DEPTH_UNIT);
    glBindTexture(GL_TEXTURE_2D, texture);
    glActiveTexture(GL_TEXTURE0);
    glUniform1i(g_gl.at_fighter_depth, FIGHTER_DEPTH_UNIT);
    glUniform4f(g_gl.at_fighter_rect, (GLfloat)rect[0], (GLfloat)rect[1],
                (GLfloat)(texture != 0 ? rect[2] : 0),
                (GLfloat)(texture != 0 ? rect[3] : 0));
    glUniform1f(g_gl.at_fighter_bias, FIGHTER_CLOTH_BIAS);
}

static void port_GlSetUniforms(void) {
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, g_gl.vram_texture);
    glUniform1i(g_gl.at_vram, 0);
    glUniform2f(g_gl.at_target, (GLfloat)g_gl.target_w, (GLfloat)g_gl.target_h);
    glUniform2f(g_gl.at_scale, (GLfloat)g_gl.draw_x, (GLfloat)g_gl.draw_y);
    glUniform1i(g_gl.at_affine, g_gl.options.is_exact ? 1 : 0);
    glUniform1i(g_gl.at_exact, g_gl.options.is_exact ? 1 : 0);
    glUniform1i(g_gl.at_like_emulator, port_GpuIsLikeEmulator() ? 1 : 0);
    glUniform1i(
        g_gl.at_bilinear, (g_gl.options.is_bilinear && !g_gl.options.is_exact));
    /* the texels of a fragment averaged where it covers more than one of
     * them, which the console's own picture does not do */
    glUniform1i(g_gl.at_minify, g_gl.options.is_exact ? 0 : 1);
    /* what the test against the ground's own depth needs: the unit a
     * corner's depth is in, the range a depth in the world's units is
     * kept over, and the offset that keeps what lies on the ground from
     * being eaten by it (gl_stage.h) */
    glUniform1f(g_gl.at_w_scale, PORT_GL_DEPTH_UNIT);
    glUniform1f(g_gl.at_depth_range, PORT_GL_DEPTH_RANGE);
    glUniform1f(g_gl.at_depth_bias, PORT_GL_DEPTH_BIAS);
    /* held against the slots of the ground only where port_GlFlush says
     * the mesh wrote them into the buffer being drawn into */
    glUniform1i(g_gl.at_ground_order, 0);
    glUniform1i(g_gl.at_ground_slots, 1);
    port_GlSetFighterDepth();
    glUniform1i(g_gl.at_replaced, 0);
    glUniform1i(g_gl.at_replace, PORT_GL_REPLACE_UNIT);
    /* a mod's pictures of the stage's pages (gl_scene.h) */
    glUniform1i(g_gl.at_scene_lookup, PORT_GL_SCENE_LOOKUP_UNIT);
    glUniform1i(g_gl.at_scene_pages, PORT_GL_SCENE_PAGES_UNIT);
    glUniform1i(g_gl.at_scene_bases, PORT_GL_SCENE_BASES_UNIT);
    glUniform1i(
        g_gl.at_scene_on, !g_gl.options.is_exact && port_GlSceneBind() ? 1 : 0);
    glUniform1f(g_gl.at_scene_levels, port_GlSceneLevels());
}

/* A fade's tint (port_mods.h, PORT_MOD_TINT_ONE a part for as it was)
 * into a uniform of three. */
static void port_GlSetTint(GLint at, u32 tint) {
    glUniform3f(
        at, (GLfloat)(tint & TINT_PART_MASK) / (GLfloat)PORT_MOD_TINT_ONE,
        (GLfloat)((tint >> PORT_MOD_TINT_SHIFT) & TINT_PART_MASK) /
            (GLfloat)PORT_MOD_TINT_ONE,
        (GLfloat)((tint >> (2 * PORT_MOD_TINT_SHIFT)) & TINT_PART_MASK) /
            (GLfloat)PORT_MOD_TINT_ONE);
}

/* The batch's replacement texture, where it has one (gl_replace.h). */
static void port_GlSetReplacement(const PortGlState *state) {
    if (state->replace_texture == 0) {
        return;
    }
    glActiveTexture(GL_TEXTURE0 + PORT_GL_REPLACE_UNIT);
    glBindTexture(GL_TEXTURE_2D, state->replace_texture);
    glActiveTexture(GL_TEXTURE0);
    glUniform1i(g_gl.at_replaced, 1);
    glUniform1i(g_gl.at_replace_mix, state->replace_mix);
    port_GlSetTint(g_gl.at_replace_tint, state->replace_tint);
    glUniform4f(g_gl.at_replace_rect, (GLfloat)state->replace_u,
                (GLfloat)state->replace_v, (GLfloat)state->replace_w,
                (GLfloat)state->replace_h);
}

/* The batch's vertices kept for the shadows' mask, where they are not
 * the stage's ground and are not half-transparent: a half-transparent
 * primitive lets what is behind it through, so the shadow under it
 * shows through as well (gl_shadow.h). */
static void port_GlKeepOccluders(void) {
    s32 room;
    s32 many;

    if (!port_GlShadowCollects() || g_gl.occluders == NULL ||
        g_gl.state.is_ground || g_gl.state.is_half) {
        return;
    }
    room = VERTICES_MAX - g_gl.occluder_count;
    many = g_gl.count < room ? g_gl.count : room;
    memcpy(&g_gl.occluders[g_gl.occluder_count], g_gl.vertices,
           (size_t)many * sizeof(*g_gl.occluders));
    g_gl.occluder_count += many;
    g_gl.occluders_lost += g_gl.count - many;
}

static void port_GlDrawBlits(void);

/* The rectangles of video memory written, then the batch of primitives
 * that came after them. */
static void port_GlFlush(void) {
    s32 was;

    port_GlDrawBlits();
    if (g_gl.count == 0) {
        return;
    }
    was = port_ProfileSwitch(PORT_PROFILE_CARD);
    port_GlKeepOccluders();
    port_GlUploadMirror();
    glUseProgram(g_gl.program);
    glBindVertexArray(g_gl.array);
    glBindBuffer(GL_ARRAY_BUFFER, g_gl.buffer);
    glBufferData(
        GL_ARRAY_BUFFER, (GLsizeiptr)((u32)g_gl.count * sizeof(*g_gl.vertices)),
        g_gl.vertices, GL_STREAM_DRAW);
    port_GlSetUniforms();
    port_GlSetReplacement(&g_gl.state);
    /* The scene against the ground (gl_stage.h). The ground's mesh has
     * drawn its own depth into the buffer this frame, so a primitive of
     * the scene that stands behind a piece of ground is left out: what
     * the painter's order alone could not do, the mesh being painted
     * where the farthest of the frame's tiles was. Nothing writes the
     * buffer but the mesh, so the order the scene's own primitives are
     * painted in is untouched, and the test is off entirely in a frame
     * the mesh did not draw -- the buffer is then the frame before's. */
    if (g_gl.state.is_held) {
        glDepthFunc(GL_LESS);
        glDepthMask(GL_FALSE);
        glEnable(GL_DEPTH_TEST);
        /* a fighter is held by the slots and not by the depth
         * (port_GlIsOverGround) */
        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, g_gl.slot_texture);
        glActiveTexture(GL_TEXTURE0);
        glUniform1i(g_gl.at_ground_order, 1);
    } else {
        glDisable(GL_DEPTH_TEST);
    }
    port_GlSetState(&g_gl.state);
    if (g_gl.state.is_wide) {
        g_gl.wide++;
    }
    if (g_gl.state.is_half &&
        g_gl.state.blend == PORT_REMASTER_BLEND_SUBTRACT) {
        /* what is there, less this: only the texels that mix are taken
         * away, and the others are drawn as they are */
        glUniform1i(g_gl.at_pass, PASS_MIXED);
        glDrawArrays(GL_TRIANGLES, 0, g_gl.count);
        glDisable(GL_BLEND);
        glUniform1i(g_gl.at_pass, PASS_PLAIN);
        glDrawArrays(GL_TRIANGLES, 0, g_gl.count);
        g_gl.draw_count += 2;
    } else {
        glUniform1i(g_gl.at_pass, PASS_ALL);
        glDrawArrays(GL_TRIANGLES, 0, g_gl.count);
        g_gl.draw_count++;
    }
    /* the test is this batch's alone: what the backend draws that is not
     * a primitive -- a rectangle of video memory, the pass that averages
     * the picture down, the window itself -- has no depth of its own and
     * is never held against the ground's */
    glDisable(GL_DEPTH_TEST);
    g_gl.vertex_count += (u32)g_gl.count;
    g_gl.count = 0;
    port_ProfileSwitch(was);
}

static s32 port_GlStateIsSame(const PortGlState *a, const PortGlState *b) {
    return a->is_half == b->is_half && a->blend == b->blend &&
           a->clip_left == b->clip_left && a->clip_top == b->clip_top &&
           a->clip_right == b->clip_right && a->clip_bottom == b->clip_bottom &&
           a->is_wide == b->is_wide && a->is_ground == b->is_ground &&
           a->is_held == b->is_held &&
           a->replace_texture == b->replace_texture &&
           a->replace_u == b->replace_u && a->replace_v == b->replace_v &&
           a->replace_w == b->replace_w && a->replace_h == b->replace_h &&
           a->replace_mix == b->replace_mix &&
           a->replace_tint == b->replace_tint;
}

static void port_GlWant(const PortGlState *state, s32 vertices) {
    if (g_gl.has_state && !port_GlStateIsSame(&g_gl.state, state)) {
        port_GlFlush();
    }
    if (g_gl.count + vertices > VERTICES_MAX) {
        port_GlFlush();
    }
    g_gl.state = *state;
    g_gl.has_state = TRUE;
}

/* ---- a primitive ---- */

static GLfloat port_GlPlace(s32 fixed) {
    return (GLfloat)fixed / (GLfloat)PORT_REMASTER_ONE;
}

static void port_GlPutVertex(
    const PortRemasterPrim *prim, s32 corner, GLfloat w, s32 flags) {
    const PortRemasterCorner *from = &prim->corner[corner];
    PortGlVertex *to = &g_gl.vertices[g_gl.count++];
    /* the margin puts the console's own 640 pixels in the middle of a
     * wider picture; a group of the fight's HUD that the game anchored to
     * an edge is drawn the margin further out again (port_remaster.h) */
    GLfloat at = (GLfloat)(g_gl.margin + prim->anchor * g_gl.margin);

    if (prim->has_place && !g_gl.options.is_exact) {
        to->x = port_GlPlace(from->x) + at;
        to->y = port_GlPlace(from->y);
    } else {
        to->x = (GLfloat)from->ix + at;
        to->y = (GLfloat)from->iy;
    }
    if (g_gl.display_w > 0 &&
        (to->x < (GLfloat)g_gl.margin ||
         to->x > (GLfloat)(g_gl.margin + g_gl.display_w))) {
        g_gl.outside++;
    }
    to->w = w;
    /* A texel's middle. The chip counts a texture point across a
     * polygon from its leftmost corner and starts half a step in, and
     * takes the whole number of what it has; adding half a texel here
     * is what matches it. (Measured: without it, the picture at scale
     * 1 differs from the software GPU's in twelve times as many
     * pixels.) */
    to->u = (GLfloat)from->u + 0.5f;
    to->v = (GLfloat)from->v + 0.5f;
    to->color[0] = (u8)from->r;
    to->color[1] = (u8)from->g;
    to->color[2] = (u8)from->b;
    to->color[3] = (u8)flags;
    to->page[0] = (s16)prim->page_x;
    to->page[1] = (s16)prim->page_y;
    to->page[2] = (s16)prim->clut_x;
    to->page[3] = (s16)prim->clut_y;
    to->info[0] = (u8)prim->texel_bits;
    to->info[1] = (u8)prim->blend;
    /* the scale the renderer drew it at, which the depth test against
     * the ground's own mesh puts back (port_remaster.h) */
    to->info[2] = (u8)prim->depth_shift;
    /* a flat polygon covers the pixels the chip's own rule gives it (the
     * geometry shader), and a cloth is held against the models */
    to->info[3] =
        (u8)(((flags & FLAG_FLAT) != 0 &&
                      (prim->kind == PORT_REMASTER_TRIANGLE ||
                       prim->kind == PORT_REMASTER_QUAD)
                  ? INFO_FLAT_RULE
                  : 0) |
             ((flags & FLAG_HELD) != 0 ? INFO_HELD : 0));
    to->window[0] = (u8)prim->window_and_u;
    to->window[1] = (u8)prim->window_or_u;
    to->window[2] = (u8)prim->window_and_v;
    to->window[3] = (u8)prim->window_or_v;
    to->slot = (GLfloat)prim->ot_slot;
}

/* The w of a corner: any positive number in proportion to how far the
 * point is, which is what a perspective-correct texture needs. Without
 * a depth every corner has 1, and the texture is flat across the
 * primitive as the console's is. The shader takes the depth back out of
 * it for the test against the ground's own mesh, so what it is divided
 * by is a number the shader is told (`u_w_scale`, PORT_GL_DEPTH_UNIT). */
static GLfloat port_GlW(const PortRemasterPrim *prim, s32 corner) {
    if (!prim->has_depth) {
        return 1.0f;
    }
    return (GLfloat)prim->corner[corner].depth / PORT_GL_DEPTH_UNIT;
}

/* Whether this primitive is drawn where the projection really put it,
 * with every corner in front of the camera: then its corners are the
 * geometry itself and not the eleven bits the chip held them in, and the
 * chip's rule about how far a triangle may span is not the rule to apply
 * to them (port_GlIsDrawn). */
static s32 port_GlHasTruePlace(const PortRemasterPrim *prim) {
    return prim->has_place && prim->has_depth && !g_gl.options.is_exact;
}

/* The chip draws no triangle that spans more than 1023 pixels across
 * or 511 down, and none of no area (src/port/raster.c): a corner the
 * projection put off the screen is held to eleven bits, so a polygon
 * that is partly behind the camera comes out spanning the whole range,
 * and the game counts on its not being drawn. The hardware path has to
 * leave out the same ones, by the same whole-pixel corners.
 *
 * A primitive whose every corner is where the projection put it and in
 * front of the camera is measured by those places instead, and allowed
 * the wider span of SPAN_TRUE_X_MAX: such a polygon is an honest
 * perspective triangle however far off the side its corners are, and
 * holding it to the chip's eleven bits is what cut the ground off at the
 * console's own edges in the bands of a wider picture (docs/PORT.md, "A
 * wider picture"). What is still left out by the wider span is a
 * projection that came apart -- a corner all but on the camera's own
 * plane, whose place runs to thousands of pixels -- which is what the
 * chip's rule was protecting the picture from. */
static s32 port_GlIsDrawn(const PortRemasterPrim *prim, s32 a, s32 b, s32 c) {
    s32 corner[3];
    s32 x[3];
    s32 y[3];
    s32 is_true = port_GlHasTruePlace(prim);
    s32 span_x = is_true ? SPAN_TRUE_X_MAX : SPAN_X_MAX;
    s32 span_y = is_true ? SPAN_TRUE_Y_MAX : SPAN_Y_MAX;
    s32 left;
    s32 right;
    s32 top;
    s32 bottom;
    s32 area;
    s32 i;

    corner[0] = a;
    corner[1] = b;
    corner[2] = c;
    for (i = 0; i < 3; i++) {
        const PortRemasterCorner *one = &prim->corner[corner[i]];

        x[i] = is_true ? (one->x >> PORT_REMASTER_FRACTION) : one->ix;
        y[i] = is_true ? (one->y >> PORT_REMASTER_FRACTION) : one->iy;
    }
    left = right = x[0];
    top = bottom = y[0];
    for (i = 1; i < 3; i++) {
        left = x[i] < left ? x[i] : left;
        right = x[i] > right ? x[i] : right;
        top = y[i] < top ? y[i] : top;
        bottom = y[i] > bottom ? y[i] : bottom;
    }
    if (right - left > span_x || bottom - top > span_y) {
        return FALSE;
    }
    if (is_true) {
        /* **No area is no area of its own places**, not of the whole
         * pixels the chip held its corners at. A sliver of a polygon --
         * a sword's blade turned nearly edge on, the side of a sandal --
         * whose corners round to one line of whole pixels has an area all
         * the same where the projection put it, and the card draws it
         * there. Dropped by its whole pixels it came and went from one
         * picture to the next as its corners crossed a pixel, and a
         * picture between two frames held it at the frame on show's
         * place while the faces about it moved (gl_tween.c, `held`): a
         * fighter's sword stood still for a frame and jumped. */
        double ax = (double)prim->corner[b].x - (double)prim->corner[a].x;
        double ay = (double)prim->corner[b].y - (double)prim->corner[a].y;
        double bx = (double)prim->corner[c].x - (double)prim->corner[a].x;
        double by = (double)prim->corner[c].y - (double)prim->corner[a].y;

        return ax * by - bx * ay != 0.0;
    }
    area = (x[1] - x[0]) * (y[2] - y[0]) - (x[2] - x[0]) * (y[1] - y[0]);
    return area != 0;
}

/* Whether every triangle of a primitive is one the chip would draw: a
 * caller that moves a primitive's corners asks before it does, so that a
 * picture between two frames never loses one (gl.h). */
s32 port_GlWouldDraw(const PortRemasterPrim *prim) {
    switch (prim->kind) {
    case PORT_REMASTER_TRIANGLE:
        return port_GlIsDrawn(prim, 0, 1, 2);
    case PORT_REMASTER_QUAD:
        return port_GlIsDrawn(prim, 0, 1, 2) && port_GlIsDrawn(prim, 1, 2, 3);
    default:
        return TRUE;
    }
}

static void port_GlTriangle(
    const PortRemasterPrim *prim, s32 a, s32 b, s32 c, s32 flags) {
    if (!port_GlIsDrawn(prim, a, b, c)) {
        g_gl.dropped++;
        return;
    }
    port_GlPutVertex(prim, a, port_GlW(prim, a), flags);
    port_GlPutVertex(prim, b, port_GlW(prim, b), flags);
    port_GlPutVertex(prim, c, port_GlW(prim, c), flags);
}

/* A rectangle: four corners of its own, flat on the screen, a texel a
 * pixel. */
static void port_GlRectangle(const PortRemasterPrim *prim, s32 flags) {
    PortRemasterPrim quad = *prim;
    s32 i;

    for (i = 1; i < 4; i++) {
        quad.corner[i] = prim->corner[0];
    }
    /* A plain rectangle that covers the console's whole width is a fade
     * or a page of one colour, and has to cover a wider picture too, or
     * the scene shows at the sides of it. A textured one is a picture of
     * the console's own shape and stays where it is, four by three in
     * the middle. */
    if (g_gl.margin > 0 && !prim->is_textured &&
        prim->rect_w >= g_gl.display_w && g_gl.display_w > 0) {
        quad.corner[0].ix -= g_gl.margin;
        quad.rect_w = prim->rect_w + 2 * g_gl.margin;
        for (i = 1; i < 4; i++) {
            quad.corner[i] = quad.corner[0];
        }
    }
    quad.corner[1].ix += quad.rect_w;
    quad.corner[1].u += quad.rect_w;
    quad.corner[2].iy += quad.rect_h;
    quad.corner[2].v += quad.rect_h;
    quad.corner[3].ix += quad.rect_w;
    quad.corner[3].iy += quad.rect_h;
    quad.corner[3].u += quad.rect_w;
    quad.corner[3].v += quad.rect_h;
    for (i = 0; i < 4; i++) {
        quad.corner[i].x = quad.corner[i].ix << PORT_REMASTER_FRACTION;
        quad.corner[i].y = quad.corner[i].iy << PORT_REMASTER_FRACTION;
    }
    quad.has_depth = FALSE;
    port_GlTriangle(&quad, 0, 1, 2, flags);
    port_GlTriangle(&quad, 1, 2, 3, flags);
}

/* What a batch of this primitive must share: its mix, the drawing area
 * and whether it is drawn across the whole of a wider picture. */
static void port_GlStateOf(const PortRemasterPrim *prim, PortGlState *state) {
    state->is_half = prim->is_half;
    state->blend = prim->blend;
    state->clip_left = prim->clip_left;
    state->clip_top = prim->clip_top;
    state->clip_right = prim->clip_right;
    state->clip_bottom = prim->clip_bottom;
    /* The whole width of a wider picture, where the game was not
     * restricting the drawing area to part of the screen (the pause
     * menu, the replay's panels): the scene, which is a primitive the
     * projection gave a depth to, and a group of the fight's HUD the game
     * anchored to an edge, which is drawn out in a band and would be cut
     * off at the console's own edge (port_remaster.h). Every other flat
     * primitive keeps the console's drawing area and stays four by three
     * in the middle. */
    state->is_wide = g_gl.margin > 0 &&
                     (prim->has_depth || prim->anchor != PORT_ANCHOR_MIDDLE) &&
                     prim->clip_left <= g_gl.display_x &&
                     prim->clip_right >= g_gl.display_x + g_gl.display_w - 1;
    /* the ground the shadows fall on, which the mask they are kept in
     * does not take as something standing in them (gl_shadow.h) */
    state->is_ground = port_GlShadowIsGround(prim);
    state->is_held =
        port_GlStageSaveFrame() && g_gl.options.is_stage_depth ? TRUE : FALSE;
    state->replace_texture = 0;
    state->replace_u = 0;
    state->replace_v = 0;
    state->replace_w = 0;
    state->replace_h = 0;
    state->replace_mix = 0;
    state->replace_tint = PORT_MOD_TINT_SAME;
}

/* A mod's picture of the primitive's texture, where there is one
 * (gl_replace.h): never in the console's own picture. */
static void port_GlReplacementOf(
    const PortRemasterPrim *prim, PortGlState *state) {
    PortGlReplacement found;

    if (g_gl.options.is_exact || !port_GlReplaceFind(prim, &found)) {
        return;
    }
    state->replace_texture = found.texture;
    state->replace_u = found.u;
    state->replace_v = found.v;
    state->replace_w = found.w;
    state->replace_h = found.h;
    state->replace_mix = found.mix;
    state->replace_tint = found.tint;
}

/* Whether this primitive is held against the ground by the console's own
 * rule and not by the depth of each pixel (docs/PORT.md, "Depth: the
 * scene is held against the ground"). The console hid nothing by depth:
 * it painted every tile at the ordering-table slot of its farthest
 * corner, so a terrace's face in front of a fighter that runs back past
 * him was painted before him and he was drawn over it, and a face whose
 * farthest corner is nearer than he is was painted over him. Held to the
 * depth of each pixel he was hidden by the first as well. So a fighter's
 * own -- his body, his cloth, a weapon -- is hidden where the face of
 * the ground the pixel shows has a nearer slot than his own (the
 * shader, `u_ground_slots`); an effect, which comes with no node and no
 * slot, is never hidden. The stage's own pieces keep the depth of each
 * pixel, which is what hides a fence behind a terrace, and so does a
 * shadow, which lies on the ground and has its depth to within the
 * offset. */
static s32 port_GlIsOverGround(const PortRemasterPrim *prim) {
    if (prim->is_shadow) {
        return FALSE;
    }
    return prim->is_fighter || prim->id.object == 0;
}

static void port_GlDraw(const PortRemasterPrim *prim) {
    PortGlState state;
    s32 flags = 0;

    if (!g_gl.is_started) {
        return;
    }
    /* the stage's ground is drawn from a mesh of its own, and the
     * primitives the game made of its tiles are left out (gl_stage.h) */
    if (port_GlStageTakes(prim)) {
        return;
    }
    /* a face of a fighter's shadow is not painted black: it goes into
     * the mask that darkens the ground at the frame's end
     * (gl_shadow.h) */
    if (port_GlShadowTakes(prim)) {
        return;
    }
    /* a fighter drawn from a model of his own: his primitives are left
     * out, and the model is drawn at the first of them (gl_fighter.h) */
    if (port_GlFighterTakes(prim)) {
        return;
    }
    /* ... and his cloth is held against the model's depth */
    if (port_GlFighterHolds() && prim->has_depth) {
        flags |= FLAG_HELD;
    }
    if (prim->is_textured) {
        flags |= FLAG_TEXTURED;
    }
    if (prim->is_raw) {
        flags |= FLAG_RAW;
    }
    if (prim->is_half) {
        flags |= FLAG_HALF;
    }
    if (prim->is_dithered) {
        flags |= FLAG_DITHERED;
    }
    if (prim->has_depth) {
        flags |= FLAG_DEPTH;
    }
    if (port_GlIsOverGround(prim)) {
        flags |= FLAG_OVER_GROUND;
    }
    /* drawn where the chip put it, by whole pixels of video memory: so
     * its texel and its colour are read as the chip read them, once a
     * pixel of video memory (the shader). A primitive the projection
     * gave a place to is not on those whole pixels and has no block to
     * read at. */
    if (!prim->has_place || prim->kind == PORT_REMASTER_RECT) {
        flags |= FLAG_FLAT;
    }
    port_GlStateOf(prim, &state);
    port_GlReplacementOf(prim, &state);
    /* a face of the stage whose page a mod has a picture of: no state of
     * the batch's, so that the stage's faces stay one batch (gl_scene.h) */
    if (!g_gl.options.is_exact && state.replace_texture == 0 &&
        port_GlSceneFind(prim)) {
        flags |= FLAG_PAGE;
    }
    if (state.clip_right < state.clip_left ||
        state.clip_bottom < state.clip_top) {
        return;
    }
    port_GlWant(&state, 6);
    switch (prim->kind) {
    case PORT_REMASTER_TRIANGLE:
        port_GlTriangle(prim, 0, 1, 2, flags);
        break;
    case PORT_REMASTER_QUAD:
        /* as the chip: the first three corners and the last three */
        port_GlTriangle(prim, 0, 1, 2, flags);
        port_GlTriangle(prim, 1, 2, 3, flags);
        break;
    case PORT_REMASTER_RECT:
        port_GlRectangle(prim, flags);
        break;
    default:
        break;
    }
}

/* ---- what is not a primitive ---- */

/* A pixel of video memory as the colour buffer holds it: the five bits a
 * channel widened exactly as the two shaders widen them. */
static s32 port_GlFive(u16 pixel, s32 shift) {
    return ((pixel >> shift) & VRAM_CHANNEL_MASK) * 255 / VRAM_CHANNEL_MASK;
}

static GLfloat port_GlChannel(u16 pixel, s32 shift) {
    return (GLfloat)port_GlFive(pixel, shift) / 255.0f;
}

/* The one colour the rectangle `rect` of video memory is, or FALSE where
 * it is not one colour: its four corners and its middle, which is a
 * frame's own clear or a fade told from a picture copied in at the cost
 * of five pixels. */
static s32 port_GlOneColor(const PortDirtyRect *rect, u16 *out) {
    s32 at_x[5];
    s32 at_y[5];
    u16 one = g_port_vram[rect->top * PORT_VRAM_WIDTH + rect->left];
    s32 i;

    at_x[0] = rect->left;
    at_y[0] = rect->top;
    at_x[1] = rect->right;
    at_y[1] = rect->top;
    at_x[2] = rect->left;
    at_y[2] = rect->bottom;
    at_x[3] = rect->right;
    at_y[3] = rect->bottom;
    at_x[4] = (rect->left + rect->right) / 2;
    at_y[4] = (rect->top + rect->bottom) / 2;
    for (i = 1; i < 5; i++) {
        if (g_port_vram[at_y[i] * PORT_VRAM_WIDTH + at_x[i]] != one) {
            return FALSE;
        }
    }
    *out = one;
    return TRUE;
}

/* The bands beside the console's own 640 pixels, over the lines of video
 * memory the rectangle `rect` covers, cleared.
 *
 * The game clears its drawing area at the start of each frame and knows
 * nothing of the bands, so what the scene last drew into them would
 * stay. It has to happen here, where that clear does, and not at the
 * top of a frame: the game draws its frame after the picture of the
 * frame before it has been read (DrawOTag comes past the vertical blank
 * in its loop), so a clear at the top of a frame would wipe the bands of
 * the picture about to be read.
 *
 * They are cleared to **the colour the game just cleared its own picture
 * to**, where that was one colour: a fight's frame begins with a fill of
 * the stage's colour of the distance (light.c, light_SetFarColor), which
 * is the sky of every stage but the few whose is black, and a band
 * cleared to black instead left two black bars down the sides of a wider
 * picture wherever no ground covered them (docs/PORT.md, "A wider
 * picture"). What the game wrote and is not one colour -- a loading
 * picture, a portrait -- is the console's own 640 pixels and no business
 * of the bands, which stay black as they were. */
static void port_GlClearBands(
    const PortDirtyRect *rect, s32 is_one_color, u16 color) {
    s32 left = g_gl.display_x * g_gl.draw_x;
    s32 band = g_gl.margin * g_gl.draw_x;
    s32 right = (g_gl.display_x + g_gl.margin + g_gl.display_w) * g_gl.draw_x;
    s32 top = rect->top * g_gl.draw_y;
    s32 height = (rect->bottom - rect->top + 1) * g_gl.draw_y;

    if (g_gl.margin <= 0 || g_gl.display_w <= 0) {
        return;
    }
    if (is_one_color) {
        glClearColor(port_GlChannel(color, VRAM_RED_SHIFT),
                     port_GlChannel(color, VRAM_GREEN_SHIFT),
                     port_GlChannel(color, VRAM_BLUE_SHIFT), 1.0f);
    }
    glDisable(GL_BLEND);
    glEnable(GL_SCISSOR_TEST);
    glScissor(left, top, band, height);
    glClear(GL_COLOR_BUFFER_BIT);
    glScissor(right, top, band, height);
    glClear(GL_COLOR_BUFFER_BIT);
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    g_gl.has_state = FALSE;
}

/* One rectangle of video memory stretched into the colour buffer, from
 * the mirror or from a mod's picture of it. */
static void port_GlBlit(const PortGlBlit *blit) {
    const PortDirtyRect *rect = &blit->rect;

    if (blit->replacement != 0) {
        glActiveTexture(GL_TEXTURE0 + PORT_GL_REPLACE_UNIT);
        glBindTexture(GL_TEXTURE_2D, blit->replacement);
        glActiveTexture(GL_TEXTURE0);
    }
    glUniform1i(g_gl.blit_at_replaced, blit->replacement != 0);
    port_GlSetTint(g_gl.blit_at_tint, blit->tint);
    glUniform4f(g_gl.blit_at_rect, (GLfloat)rect->left, (GLfloat)rect->top,
                (GLfloat)(rect->right - rect->left + 1),
                (GLfloat)(rect->bottom - rect->top + 1));
    glDrawArrays(GL_TRIANGLES, 0, 6);
    g_gl.draw_count++;
}

/* The rectangles of video memory written since the card last drew,
 * stretched into the colour buffer in the order they were written, with
 * the mirror sent once for all of them. Nothing was drawn between them,
 * so this is the picture each in its turn would have made: where a later
 * one covers an earlier, the later is what is left, and the mirror holds
 * each as it was written (port_GlMarkMirror). A frame writes video memory
 * fifty times and more, mostly the fog's colour tables a line at a time
 * (stage_QueueFog), and a send and a draw for each kept the card waiting
 * on itself. */
static void port_GlDrawBlits(void) {
    s32 was;
    s32 i;

    if (g_gl.blit_count == 0) {
        return;
    }
    was = port_ProfileSwitch(PORT_PROFILE_CARD);
    port_GlUploadMirror();
    glUseProgram(g_gl.blit_program);
    glBindVertexArray(g_gl.blit_array);
    glUniform1i(g_gl.blit_at_replace, PORT_GL_REPLACE_UNIT);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, g_gl.vram_texture);
    glUniform1i(g_gl.blit_at_vram, 0);
    glUniform2f(
        g_gl.blit_at_target, (GLfloat)g_gl.target_w, (GLfloat)g_gl.target_h);
    glUniform2f(g_gl.blit_at_scale, (GLfloat)g_gl.draw_x, (GLfloat)g_gl.draw_y);
    glUniform1f(g_gl.blit_at_shift, (GLfloat)g_gl.margin);
    glDisable(GL_BLEND);
    if (g_gl.margin > 0 && g_gl.display_w > 0) {
        /* What the game wrote into video memory belongs to the
         * console's own 640 pixels, wherever in video memory it is: the
         * bands beside them are the scene's alone, and a rectangle of
         * texture memory stretched into them would show its texels
         * (docs/PORT.md, "Remastered", "A wider picture"). */
        glEnable(GL_SCISSOR_TEST);
        glScissor((g_gl.display_x + g_gl.margin) * g_gl.draw_x, 0,
                  g_gl.display_w * g_gl.draw_x, g_gl.target_h);
    } else {
        glDisable(GL_SCISSOR_TEST);
    }
    for (i = 0; i < g_gl.blit_count; i++) {
        port_GlBlit(&g_gl.blit[i]);
    }
    glEnable(GL_SCISSOR_TEST);
    g_gl.blit_count = 0;
    g_gl.has_state = FALSE;
    port_ProfileSwitch(was);
}

/* Whether rectangle `a` holds the whole of `b`. */
static s32 port_GlRectHolds(const PortDirtyRect *a, const PortDirtyRect *b) {
    return a->left <= b->left && a->top <= b->top && a->right >= b->right &&
           a->bottom >= b->bottom;
}

/* A rectangle of video memory the software GPU wrote: it is sent to the
 * card and stretched into the colour buffer, in its turn among the
 * primitives (port_GlDrawBlits). */
static void port_GlWroteVram(s32 x, s32 y, s32 w, s32 h) {
    PortGlBlit blit;
    PortDirtyRect *rect = &blit.rect;
    s32 was;
    s32 kept;
    s32 i;

    if (!g_gl.is_started || w <= 0 || h <= 0) {
        return;
    }
    if (g_gl.count != 0) {
        port_GlFlush();
    }
    was = port_ProfileSwitch(PORT_PROFILE_CARD);
    /* pixels of the picture the game has put there itself: where they
     * fall over a shadow already painted, that shadow is covered
     * (gl_shadow.h) */
    port_GlShadowWroteVram(x, y, w, h);
    /* a page of the stage written over is looked up again (gl_scene.h) */
    port_GlSceneWroteVram(x, y, w, h);
    port_ClearDirtyRect(rect);
    port_AddDirtyRect(rect, x, y, w, h);
    port_GlMarkMirror(x, y, w, h);
    /* a picture a mod has a picture of (gl_replace.h) */
    blit.tint = PORT_MOD_TINT_SAME;
    blit.replacement =
        g_gl.options.is_exact
            ? 0
            : port_GlReplaceFindPicture(
                  rect->left, rect->top, rect->right - rect->left + 1,
                  rect->bottom - rect->top + 1, g_gl.display_x,
                  g_gl.display_is_rgb24 ? 0 : g_gl.display_w, &blit.tint);
    /* one written before that this one covers is left out: this one is
     * drawn over the whole of it */
    kept = 0;
    for (i = 0; i < g_gl.blit_count; i++) {
        if (!port_GlRectHolds(rect, &g_gl.blit[i].rect)) {
            g_gl.blit[kept++] = g_gl.blit[i];
        }
    }
    g_gl.blit_count = kept;
    if (g_gl.blit_count == BLITS_MAX) {
        port_GlDrawBlits();
    }
    g_gl.blit[g_gl.blit_count++] = blit;
    g_gl.has_state = FALSE;
    /* a fill or a copy as wide as the console's picture is the frame's
     * own clear: the bands go with it, and where it is one colour that
     * colour is the picture's background, which is what shows through a
     * hole in the scene (gl.h, port_GlBackground). The bands are outside
     * the console's own 640 pixels, where no rectangle of video memory
     * is stretched, so they are cleared now. */
    if (rect->right - rect->left + 1 >= g_gl.display_w) {
        u16 color = 0;
        s32 is_one_color = port_GlOneColor(rect, &color);

        if (is_one_color) {
            g_gl.background =
                0xFF000000U | ((u32)port_GlFive(color, VRAM_RED_SHIFT) << 16) |
                ((u32)port_GlFive(color, VRAM_GREEN_SHIFT) << 8) |
                (u32)port_GlFive(color, VRAM_BLUE_SHIFT);
        }
        port_GlClearBands(rect, is_one_color, color);
        /* the frame's own clear: where it is a fight's on a stage open
         * to the sky, the sky goes over it (gl_sky.h), and so over the
         * clear itself, which is drawn first */
        if (is_one_color && !g_gl.options.is_exact) {
            port_GlDrawBlits();
            port_GlSkyCleared(
                rect->left, rect->top, rect->right - rect->left + 1,
                rect->bottom - rect->top + 1, g_gl.background);
        }
    }
    port_ProfileSwitch(was);
}

static void port_GlBegin(void) {
    /* The frame that has just been built, before anything of this one is
     * drawn: its shadows darken the ground of the picture it made, and
     * it has to be here, because the game clears the next frame's
     * ordering table before the picture of this one has been read
     * (gl_shadow.h). What it uses is the vertices kept below, so it goes
     * first. */
    port_GlShadowNewFrame();
    g_gl.has_state = FALSE;
    port_GlForgetOccluders();
    port_GlStageNewFrame();
    port_GlFighterNewFrame();
    port_GlSceneNewFrame();
}

/* ---- what the stage's own mesh asks of the picture (gl.h) ---- */

void port_GlGetPicture(PortGlPicture *picture) {
    picture->framebuffer = g_gl.framebuffer;
    picture->color_texture = g_gl.color_texture;
    picture->vram_texture = g_gl.vram_texture;
    picture->depth_buffer = g_gl.depth_buffer;
    picture->slot_texture = g_gl.slot_texture;
    picture->target_w = g_gl.target_w;
    picture->target_h = g_gl.target_h;
    picture->draw_x = g_gl.draw_x;
    picture->draw_y = g_gl.draw_y;
    picture->margin = g_gl.margin;
    picture->display_x = g_gl.display_x;
    picture->display_y = g_gl.display_y;
    picture->display_w = g_gl.display_w;
    picture->display_h = g_gl.display_h;
}

void port_GlFlushBatch(void) {
    port_GlFlush();
    port_GlUploadMirror();
}

/* ---- what the fighters' shadows ask of the picture (gl.h) ---- */

s32 port_GlPlacePoints(const PortRemasterPrim *prim, PortGlPoint *out) {
    static const s32 corner[2][3] = {{0, 1, 2}, {1, 2, 3}};
    s32 triangles = prim->kind == PORT_REMASTER_QUAD ? 2 : 1;
    s32 many = 0;
    s32 i;
    s32 j;

    if (prim->kind != PORT_REMASTER_TRIANGLE &&
        prim->kind != PORT_REMASTER_QUAD) {
        return 0;
    }
    for (i = 0; i < triangles; i++) {
        if (!port_GlIsDrawn(prim, corner[i][0], corner[i][1], corner[i][2])) {
            continue;
        }
        for (j = 0; j < 3; j++) {
            const PortRemasterCorner *from = &prim->corner[corner[i][j]];
            GLfloat at = (GLfloat)(g_gl.margin + prim->anchor * g_gl.margin);

            if (prim->has_place && !g_gl.options.is_exact) {
                out[many].x = port_GlPlace(from->x) + at;
                out[many].y = port_GlPlace(from->y);
            } else {
                out[many].x = (GLfloat)from->ix + at;
                out[many].y = (GLfloat)from->iy;
            }
            out[many].w = port_GlW(prim, corner[i][j]);
            many++;
        }
    }
    return many;
}

void port_GlScissorFor(
    const PortRemasterPrim *prim, s32 *x, s32 *y, s32 *w, s32 *h) {
    PortGlState state;

    port_GlStateOf(prim, &state);
    port_GlScissorOf(&state, x, y, w, h);
}

/* How many vertices of this frame are kept for the shadows' mask, and
 * how many the frame had no room for. */
s32 port_GlOccluderCount(void) { return g_gl.occluder_count; }

void port_GlForgetOccluders(void) {
    g_gl.occluder_count = 0;
    g_gl.occluders_lost = 0;
}

s32 port_GlOccluderLost(void) { return g_gl.occluders_lost; }

void port_GlDrawOccluders(u32 framebuffer) {
    if (!g_gl.is_started || g_gl.occluders == NULL ||
        g_gl.occluder_count == 0) {
        return;
    }
    port_GlUploadMirror();
    glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
    glUseProgram(g_gl.program);
    glBindVertexArray(g_gl.array);
    glBindBuffer(GL_ARRAY_BUFFER, g_gl.buffer);
    glBufferData(
        GL_ARRAY_BUFFER,
        (GLsizeiptr)((u32)g_gl.occluder_count * sizeof(*g_gl.occluders)),
        g_gl.occluders, GL_STREAM_DRAW);
    port_GlSetUniforms();
    glUniform1i(g_gl.at_pass, PASS_ALL);
    /* the alpha alone: what the fragment's colour is does not matter,
     * only that the picture kept it. Every one of these primitives is
     * opaque, so the shader's own alpha is 1 (gl_shadow.h). */
    glDisable(GL_BLEND);
    glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_TRUE);
    glDrawArrays(GL_TRIANGLES, 0, g_gl.occluder_count);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    g_gl.vertex_count += (u32)g_gl.occluder_count;
    g_gl.draw_count++;
    glBindFramebuffer(GL_FRAMEBUFFER, g_gl.framebuffer);
    /* the batch's own vertices are no longer in the buffer */
    g_gl.has_state = FALSE;
}

void port_GlForgetState(void) { g_gl.has_state = FALSE; }

void port_GlCountDraw(u32 vertices, u32 draws) {
    g_gl.vertex_count += vertices;
    g_gl.draw_count += draws;
}

static void port_GlEnd(s32 x, s32 y, s32 w, s32 h, s32 is_on, s32 is_rgb24) {
    port_GlFlush();
    g_gl.display_x = x;
    g_gl.display_y = y;
    g_gl.display_w = w;
    g_gl.display_h = h;
    port_GlFindMargin();
    /* Not done: the 24-bit mode, which only the movie uses, is read
     * from the software GPU's own video memory. */
    g_gl.display_is_on = is_on && !is_rgb24;
    g_gl.display_is_rgb24 = is_rgb24;
    /* what the frame on show cost: a blank at which the game drew
     * nothing leaves the frame before it standing (port_remaster.h) */
    if (g_gl.vertex_count != 0 || g_gl.upload_count != 0) {
        g_gl.shown_dropped = g_gl.dropped;
        g_gl.dropped = 0;
        g_gl.shown_outside = g_gl.outside;
        g_gl.shown_wide = g_gl.wide;
        g_gl.outside = 0;
        g_gl.wide = 0;
        g_gl.shown_vertices = g_gl.vertex_count;
        g_gl.shown_draws = g_gl.draw_count;
        g_gl.shown_uploads = g_gl.upload_count;
        g_gl.vertex_count = 0;
        g_gl.draw_count = 0;
        g_gl.upload_count = 0;
    }
}

static const PortRemasterBackend g_gl_backend = {
    port_GlBegin, port_GlDraw, port_GlWroteVram, port_GlEnd};

const PortRemasterBackend *port_GlBackend(void) { return &g_gl_backend; }

void port_GlUploadVram(s32 x, s32 y, s32 w, s32 h) {
    if (!g_gl.is_started || w <= 0 || h <= 0) {
        return;
    }
    /* what was written before is drawn from what it was */
    port_GlDrawBlits();
    port_GlMarkMirror(x, y, w, h);
    port_GlUploadMirror();
    port_GlSceneWroteVram(x, y, w, h);
}

void port_GlSaveCounts(u32 *four) {
    four[0] = g_gl.vertex_count;
    four[1] = g_gl.draw_count;
    four[2] = g_gl.upload_count;
    four[3] = g_gl.dropped;
}

void port_GlLoadCounts(const u32 *four) {
    g_gl.vertex_count = four[0];
    g_gl.draw_count = four[1];
    g_gl.upload_count = four[2];
    g_gl.dropped = four[3];
}

u32 port_GlDroppedNow(void) { return g_gl.dropped; }

s32 port_GlPictureMargin(void) { return g_gl.margin; }

u32 port_GlBackground(void) { return g_gl.background; }

/* ---- what the host asks for ---- */

/* The two scales a PortGlOptions asks for, each held to what there
 * is. */
static s32 port_GlSampleOf(const PortGlOptions *options) {
    s32 sample = options->sample < 1 ? 1 : options->sample;

    return sample > PORT_SUPERSAMPLE_MAX ? PORT_SUPERSAMPLE_MAX : sample;
}

static void port_GlScalesOf(
    const PortGlOptions *options, s32 *across, s32 *down) {
    *across = options->scale;
    *down = options->tall > 0 ? options->tall : options->scale;
    if (*across < 1) {
        *across = 1;
    }
    if (*across > PORT_REMASTER_SCALE_MAX) {
        *across = PORT_REMASTER_SCALE_MAX;
    }
    if (*down < 1) {
        *down = 1;
    }
    if (*down > PORT_REMASTER_SCALE_MAX) {
        *down = PORT_REMASTER_SCALE_MAX;
    }
}

void port_GlSetOptions(const PortGlOptions *options) {
    s32 across;
    s32 down;
    s32 sample = port_GlSampleOf(options);

    port_GlScalesOf(options, &across, &down);
    g_gl.options = *options;
    /* the comparison with the software GPU's own picture wants the
     * tiles as the game emitted them, and the shadows the game's own
     * black polygons */
    port_GlStageSetOn(options->is_exact ? 0 : options->is_stage_mesh);
    port_GlStageSetPropsOn(options->is_exact ? 0 : options->is_stage_props);
    port_GlShadowSetStrength(options->is_exact ? 0 : options->shadows);
    port_GlFighterSetOn(options->is_exact ? 0 : options->is_fighter_models);
    port_GlSceneSetOn(!options->is_exact);
    port_GlSkySetKind(options->is_exact ? 0 : options->sky);
    port_GlSeeSetOn(options->is_exact ? 0 : options->see_through);
    if (g_gl.is_started) {
        /* the setting may have turned the mesh on or off, and the depth
         * buffer is attached only for it */
        port_GlAttachDepth();
    }
    if (g_gl.is_started && (across != g_gl.scale_x || down != g_gl.scale_y ||
                            sample != g_gl.sample)) {
        g_gl.scale_x = across;
        g_gl.scale_y = down;
        g_gl.sample = sample;
        g_gl.blit_count = 0;
        port_GlMakeTarget();
        port_GlWroteVram(0, 0, PORT_VRAM_WIDTH, PORT_VRAM_HEIGHT);
        return;
    }
    g_gl.scale_x = across;
    g_gl.scale_y = down;
    g_gl.sample = sample;
    g_gl.draw_x = g_gl.scale_x * sample;
    g_gl.draw_y = g_gl.scale_y * sample;
    port_GlFindMargin();
}

void port_GlGetOptions(PortGlOptions *options) { *options = g_gl.options; }

s32 port_GlIsStarted(void) { return g_gl.is_started; }

const char *port_GlStart(
    void *(*getproc)(const char *name), const PortGlOptions *options) {
    static const GLfloat corners[] = {
        0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f, 1.0f, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f};

    g_gl.problem[0] = 0;
    if (!port_GlFindFunctions(getproc)) {
        return g_gl.problem;
    }
    if (g_gl.vertices == NULL) {
        g_gl.vertices = malloc(VERTICES_MAX * sizeof(*g_gl.vertices));
        if (g_gl.vertices == NULL) {
            port_GlSay("there is no memory for the vertices of a frame", NULL);
            return g_gl.problem;
        }
    }
    if (g_gl.staged == NULL) {
        g_gl.staged = calloc(PORT_VRAM_PIXELS, sizeof(*g_gl.staged));
        if (g_gl.staged == NULL) {
            port_GlSay("there is no memory for the copy of video memory", NULL);
            return g_gl.problem;
        }
    }
    if (g_gl.occluders == NULL) {
        /* what keeps the shadows off the fighters: as many vertices
         * again, which a frame of a fight uses a tenth of (gl_shadow.h).
         * The shadows go on without it. */
        g_gl.occluders = malloc(VERTICES_MAX * sizeof(*g_gl.occluders));
    }
    g_gl.options = *options;
    port_GlScalesOf(options, &g_gl.scale_x, &g_gl.scale_y);
    g_gl.sample = port_GlSampleOf(options);
    g_gl.program = port_GlMakeProgramWith(
        g_gl_vertex_text, g_gl_geometry_text, g_gl_fragment_text);
    if (g_gl.program == 0) {
        return g_gl.problem;
    }
    g_gl.blit_program =
        port_GlMakeProgram(g_gl_blit_vertex_text, g_gl_blit_fragment_text);
    if (g_gl.blit_program == 0) {
        return g_gl.problem;
    }
    g_gl.at_target = glGetUniformLocation(g_gl.program, "u_target");
    g_gl.at_scale = glGetUniformLocation(g_gl.program, "u_scale");
    g_gl.at_vram = glGetUniformLocation(g_gl.program, "u_vram");
    g_gl.at_affine = glGetUniformLocation(g_gl.program, "u_affine");
    g_gl.at_exact = glGetUniformLocation(g_gl.program, "u_exact");
    g_gl.at_like_emulator =
        glGetUniformLocation(g_gl.program, "u_like_emulator");
    g_gl.at_bilinear = glGetUniformLocation(g_gl.program, "u_bilinear");
    g_gl.at_minify = glGetUniformLocation(g_gl.program, "u_minify");
    g_gl.at_w_scale = glGetUniformLocation(g_gl.program, "u_w_scale");
    g_gl.at_depth_range = glGetUniformLocation(g_gl.program, "u_depth_range");
    g_gl.at_depth_bias = glGetUniformLocation(g_gl.program, "u_depth_bias");
    g_gl.at_ground_slots = glGetUniformLocation(g_gl.program, "u_ground_slots");
    g_gl.at_fighter_depth =
        glGetUniformLocation(g_gl.program, "u_fighter_depth");
    g_gl.at_fighter_rect = glGetUniformLocation(g_gl.program, "u_fighter_rect");
    g_gl.at_fighter_bias = glGetUniformLocation(g_gl.program, "u_fighter_bias");
    g_gl.at_ground_order = glGetUniformLocation(g_gl.program, "u_ground_order");
    g_gl.at_pass = glGetUniformLocation(g_gl.program, "u_pass");
    g_gl.blit_at_target = glGetUniformLocation(g_gl.blit_program, "u_target");
    g_gl.blit_at_rect = glGetUniformLocation(g_gl.blit_program, "u_rect");
    g_gl.blit_at_scale = glGetUniformLocation(g_gl.blit_program, "u_scale");
    g_gl.blit_at_vram = glGetUniformLocation(g_gl.blit_program, "u_vram");
    g_gl.blit_at_shift = glGetUniformLocation(g_gl.blit_program, "u_shift");
    g_gl.at_replace = glGetUniformLocation(g_gl.program, "u_replace");
    g_gl.at_replaced = glGetUniformLocation(g_gl.program, "u_replaced");
    g_gl.at_replace_rect = glGetUniformLocation(g_gl.program, "u_replace_rect");
    g_gl.at_replace_mix = glGetUniformLocation(g_gl.program, "u_replace_mix");
    g_gl.at_replace_tint = glGetUniformLocation(g_gl.program, "u_replace_tint");
    g_gl.at_scene_lookup = glGetUniformLocation(g_gl.program, "u_scene_lookup");
    g_gl.at_scene_pages = glGetUniformLocation(g_gl.program, "u_scene_pages");
    g_gl.at_scene_bases = glGetUniformLocation(g_gl.program, "u_scene_bases");
    g_gl.at_scene_on = glGetUniformLocation(g_gl.program, "u_scene_on");
    g_gl.at_scene_levels = glGetUniformLocation(g_gl.program, "u_scene_levels");
    g_gl.blit_at_tint = glGetUniformLocation(g_gl.blit_program, "u_tint");
    g_gl.blit_at_replace = glGetUniformLocation(g_gl.blit_program, "u_replace");
    g_gl.blit_at_replaced =
        glGetUniformLocation(g_gl.blit_program, "u_replaced");
    g_gl.resolve_program = port_GlMakeProgram(
        g_gl_resolve_vertex_text, g_gl_resolve_fragment_text);
    if (g_gl.resolve_program == 0) {
        return g_gl.problem;
    }
    g_gl.resolve_at_color =
        glGetUniformLocation(g_gl.resolve_program, "u_color");
    g_gl.resolve_at_sample =
        glGetUniformLocation(g_gl.resolve_program, "u_sample");
    g_gl.resolve_at_at = glGetUniformLocation(g_gl.resolve_program, "u_at");
    g_gl.show_program =
        port_GlMakeProgram(g_gl_show_vertex_text, g_gl_show_fragment_text);
    if (g_gl.show_program == 0) {
        return g_gl.problem;
    }
    g_gl.show_at_picture = glGetUniformLocation(g_gl.show_program, "u_picture");
    g_gl.show_at_dest = glGetUniformLocation(g_gl.show_program, "u_dest");
    g_gl.show_at_window = glGetUniformLocation(g_gl.show_program, "u_window");
    g_gl.show_at_source = glGetUniformLocation(g_gl.show_program, "u_source");
    g_gl.show_at_texture = glGetUniformLocation(g_gl.show_program, "u_texture");
    g_gl.show_at_over = glGetUniformLocation(g_gl.show_program, "u_over");
    glGenTextures(1, &g_gl.host_texture);

    glGenTextures(1, &g_gl.vram_texture);
    glBindTexture(GL_TEXTURE_2D, g_gl.vram_texture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_R16UI, PORT_VRAM_WIDTH, PORT_VRAM_HEIGHT,
                 0, GL_RED_INTEGER, GL_UNSIGNED_SHORT, NULL);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    glGenTextures(1, &g_gl.color_texture);
    glGenFramebuffers(1, &g_gl.framebuffer);
    glGenRenderbuffers(1, &g_gl.depth_buffer);
    glGenTextures(1, &g_gl.slot_texture);
    glGenTextures(1, &g_gl.picture_texture);
    glGenFramebuffers(1, &g_gl.picture_framebuffer);
    if (!port_GlMakeTarget()) {
        return g_gl.problem;
    }
    glGenBuffers(1, &g_gl.buffer);
    glGenVertexArrays(1, &g_gl.array);
    port_GlSetUpVertexFormat();
    glGenBuffers(1, &g_gl.blit_buffer);
    glGenVertexArrays(1, &g_gl.blit_array);
    glBindVertexArray(g_gl.blit_array);
    glBindBuffer(GL_ARRAY_BUFFER, g_gl.blit_buffer);
    glBufferData(
        GL_ARRAY_BUFFER, (GLsizeiptr)sizeof(corners), corners, GL_STREAM_DRAW);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, NULL);
    glEnableVertexAttribArray(0);

    /* The stage's ground as one mesh, which draws into the same colour
     * buffer through a framebuffer of its own (gl_stage.h). A card that
     * cannot have it keeps the remastered picture and draws the game's
     * own tiles: this is not a reason to give the picture up. */
    {
        const char *why = port_GlStageStart(getproc);

        if (why != NULL) {
            /* kept for whoever asks; the picture goes on without it */
            port_GlSay("the stage's ground is drawn from the game's own "
                       "tiles: ",
                       why);
        }
    }
    /* The fighters' shadows as a soft darkening of the ground, which
     * draws into the colour buffer through a mask of its own
     * (gl_shadow.h). A card that cannot have it draws the game's own
     * black silhouettes, as `shadows original` does. */
    {
        const char *why = port_GlShadowStart(getproc);

        if (why != NULL) {
            port_GlSay("the shadows are the game's own black polygons: ", why);
        }
    }
    /* A mod's textures (gl_replace.h). A card that cannot have them
     * draws the game's own. */
    {
        const char *why = port_GlReplaceStart(getproc);

        if (why != NULL) {
            port_GlSay("the mods' textures are not drawn: ", why);
        }
    }
    /* ... and their pictures of the stage's pages (gl_scene.h) */
    {
        const char *why = port_GlSceneStart(getproc);

        if (why != NULL) {
            port_GlSay("the mods' pages of the stage are not drawn: ", why);
        }
    }
    port_GlSceneSetOn(!options->is_exact);
    /* the comparison with the software GPU's own picture wants the
     * tiles as the game emitted them, and the shadows as it drew them */
    port_GlStageSetOn(options->is_exact ? 0 : options->is_stage_mesh);
    port_GlStageSetPropsOn(options->is_exact ? 0 : options->is_stage_props);
    port_GlShadowSetStrength(options->is_exact ? 0 : options->shadows);
    /* the fighters' own models (gl_fighter.h): a card that cannot have
     * them draws the game's own fighters */
    {
        const char *why = port_GlFighterStart(getproc);

        if (why != NULL) {
            port_GlSay("the fighters are the game's own primitives: ", why);
        }
    }
    port_GlFighterSetOn(options->is_exact ? 0 : options->is_fighter_models);
    /* the sky (gl_sky.h): a card that cannot have it shows the game's
     * own colour of the distance */
    {
        const char *why = port_GlSkyStart(getproc);

        if (why != NULL) {
            port_GlSay("the sky is the game's colour of the distance: ", why);
        }
    }
    port_GlSkySetKind(options->is_exact ? 0 : options->sky);
    /* the walls seen through (gl_see.h), which the ground's mesh draws */
    port_GlSeeSetOn(options->is_exact ? 0 : options->see_through);

    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    glEnable(GL_SCISSOR_TEST);
    glScissor(0, 0, g_gl.target_w, g_gl.target_h);
    g_gl.is_started = TRUE;
    /* video memory as it is now, all of it */
    port_ClearDirtyRect(&g_gl.mirror);
    g_gl.blit_count = 0;
    port_GlWroteVram(0, 0, PORT_VRAM_WIDTH, PORT_VRAM_HEIGHT);
    port_RemasterSetBackend(&g_gl_backend);
    return NULL;
}

/* The display's rectangle of the colour buffer averaged down into the
 * picture, where more than one sample a pixel is drawn. Leaves the
 * picture's framebuffer bound, which is what is read back. */
static s32 port_GlResolve(void) {
    s32 w = (g_gl.display_w + 2 * g_gl.margin) * g_gl.scale_x;
    s32 h = g_gl.display_h * g_gl.scale_y;

    if (w <= 0 || h <= 0) {
        return FALSE;
    }
    if (w != g_gl.picture_w || h != g_gl.picture_h) {
        glBindTexture(GL_TEXTURE_2D, g_gl.picture_texture);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA,
                     GL_UNSIGNED_BYTE, NULL);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glBindFramebuffer(GL_FRAMEBUFFER, g_gl.picture_framebuffer);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                               GL_TEXTURE_2D, g_gl.picture_texture, 0);
        if (glCheckFramebufferStatus(GL_FRAMEBUFFER) !=
            GL_FRAMEBUFFER_COMPLETE) {
            port_GlSay("the picture cannot be drawn into", NULL);
            return FALSE;
        }
        g_gl.picture_w = w;
        g_gl.picture_h = h;
    }
    glBindFramebuffer(GL_FRAMEBUFFER, g_gl.picture_framebuffer);
    glViewport(0, 0, w, h);
    glDisable(GL_BLEND);
    glDisable(GL_SCISSOR_TEST);
    glUseProgram(g_gl.resolve_program);
    glBindVertexArray(g_gl.blit_array);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, g_gl.color_texture);
    glUniform1i(g_gl.resolve_at_color, 0);
    glUniform1i(g_gl.resolve_at_sample, g_gl.sample);
    glUniform2f(g_gl.resolve_at_at, (GLfloat)(g_gl.display_x * g_gl.draw_x),
                (GLfloat)(g_gl.display_y * g_gl.draw_y));
    glDrawArrays(GL_TRIANGLES, 0, 6);
    g_gl.draw_count++;
    /* what is drawn next is the scene again, into the colour buffer */
    glBindFramebuffer(GL_FRAMEBUFFER, g_gl.framebuffer);
    glViewport(0, 0, g_gl.target_w, g_gl.target_h);
    glEnable(GL_SCISSOR_TEST);
    g_gl.has_state = FALSE;
    return TRUE;
}

void port_GlFinishFrame(void) {
    if (!g_gl.is_started) {
        return;
    }
    port_GlFlush();
    /* the fighters' shadows: the mask, and the ground under them
     * darkened. It is before the picture is averaged down, so the soft
     * edge is smoothed with everything else (gl_shadow.h). */
    port_GlShadowFinish();
    if (g_gl.sample > 1) {
        port_GlResolve();
    }
    /* Handed to the card, and not waited for: the card draws while the
     * program goes on, and what reads the picture back (the prover, a
     * picture of the window) or shows it (the swap) waits for it then.
     * Waiting here (glFinish) held the program up for the whole of the
     * card's drawing at every picture. */
    glFlush();
}

s32 port_GlReadPicture(u32 *out, u32 room, s32 *out_w, s32 *out_h) {
    s32 w = (g_gl.display_w + 2 * g_gl.margin) * g_gl.scale_x;
    s32 h = g_gl.display_h * g_gl.scale_y;
    s32 at;

    *out_w = w;
    *out_h = h;
    if (!g_gl.is_started || !g_gl.display_is_on || w <= 0 || h <= 0 ||
        (u32)(w * h) > room) {
        return FALSE;
    }
    /* the buffer's lines run as video memory's do, so the first line
     * read is the picture's first */
    if (g_gl.sample > 1) {
        /* the pass that averaged the samples down drew the picture
         * alone, so it is read from its own corner */
        glBindFramebuffer(GL_FRAMEBUFFER, g_gl.picture_framebuffer);
        glReadPixels(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, out);
        glBindFramebuffer(GL_FRAMEBUFFER, g_gl.framebuffer);
    } else {
        glBindFramebuffer(GL_FRAMEBUFFER, g_gl.framebuffer);
        glReadPixels(
            g_gl.display_x * g_gl.scale_x, g_gl.display_y * g_gl.scale_y, w, h,
            GL_RGBA, GL_UNSIGNED_BYTE, out);
    }
    /* the pixels come as red, green, blue, alpha; the port's are
     * 0xFFRRGGBB */
    for (at = 0; at < h * w; at++) {
        u32 pixel = out[at];

        out[at] = 0xFF000000U | ((pixel & 0xFF) << 16) | (pixel & 0xFF00) |
                  ((pixel >> 16) & 0xFF);
    }
    return TRUE;
}

/* ---- into a window (gl.h) ---- */

void port_GlRefresh(void) {
    if (!g_gl.is_started) {
        return;
    }
    port_GlWroteVram(0, 0, PORT_VRAM_WIDTH, PORT_VRAM_HEIGHT);
}

const char *port_GlDescribe(s32 which) {
    const char *text;

    if (!g_gl.is_started) {
        return "?";
    }
    switch (which) {
    case PORT_GL_VENDOR:
        text = glGetString(GL_VENDOR);
        break;
    case PORT_GL_RENDERER:
        text = glGetString(GL_RENDERER);
        break;
    default:
        text = glGetString(GL_VERSION);
        break;
    }
    return text != NULL ? text : "?";
}

/* The window's framebuffer, ready to be drawn into: nothing of the game's
 * drawing state holds while it is bound. */
static void port_GlTakeWindow(s32 window_w, s32 window_h) {
    glBindFramebuffer(GL_FRAMEBUFFER, GL_WINDOW_FRAMEBUFFER);
    glViewport(0, 0, window_w, window_h);
    glDisable(GL_SCISSOR_TEST);
    glDisable(GL_BLEND);
}

/* The colour buffer again, as the game's drawing left it. */
static void port_GlLeaveWindow(void) {
    glBindFramebuffer(GL_FRAMEBUFFER, g_gl.framebuffer);
    glViewport(0, 0, g_gl.target_w, g_gl.target_h);
    glEnable(GL_SCISSOR_TEST);
    g_gl.has_state = FALSE;
}

/* A rectangle of a texture into a rectangle of the window. */
static void port_GlPresent(
    GLuint texture, s32 texture_w, s32 texture_h, s32 source_x, s32 source_y,
    s32 source_w, s32 source_h, s32 window_w, s32 window_h, s32 x, s32 y, s32 w,
    s32 h, s32 is_linear, s32 is_over) {
    GLint filter = is_linear ? GL_LINEAR : GL_NEAREST;

    glUseProgram(g_gl.show_program);
    glBindVertexArray(g_gl.blit_array);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, filter);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, filter);
    glUniform1i(g_gl.show_at_picture, 0);
    glUniform4f(
        g_gl.show_at_dest, (GLfloat)x, (GLfloat)y, (GLfloat)w, (GLfloat)h);
    glUniform2f(g_gl.show_at_window, (GLfloat)window_w, (GLfloat)window_h);
    glUniform4f(g_gl.show_at_source, (GLfloat)source_x, (GLfloat)source_y,
                (GLfloat)source_w, (GLfloat)source_h);
    glUniform2f(g_gl.show_at_texture, (GLfloat)texture_w, (GLfloat)texture_h);
    glUniform1i(g_gl.show_at_over, is_over ? 1 : 0);
    if (is_over) {
        glEnable(GL_BLEND);
        glBlendEquation(GL_FUNC_ADD);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    }
    glDrawArrays(GL_TRIANGLES, 0, 6);
    if (is_over) {
        glDisable(GL_BLEND);
    }
    /* the filters are the drawing path's own again: it reads texels as
     * blocks, whatever the picture is shown with */
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
}

void port_GlClearWindow(s32 window_w, s32 window_h) {
    if (!g_gl.is_started) {
        return;
    }
    port_GlTakeWindow(window_w, window_h);
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    port_GlLeaveWindow();
}

s32 port_GlShowPicture(
    s32 window_w, s32 window_h, s32 x, s32 y, s32 w, s32 h, s32 is_linear) {
    s32 picture_w = (g_gl.display_w + 2 * g_gl.margin) * g_gl.scale_x;
    s32 picture_h = g_gl.display_h * g_gl.scale_y;

    if (!g_gl.is_started || !g_gl.display_is_on || picture_w <= 0 ||
        picture_h <= 0 || w <= 0 || h <= 0) {
        return FALSE;
    }
    /* The scene is drawn at as many times the console's resolution as the
     * window asks for, rounded up, so the picture is almost always larger
     * than the rectangle it is shown in. Taking one pixel of it for each
     * pixel of the window there would throw the others away and leave the
     * distance crawling as the camera turns, whatever the `filter`
     * setting says about a pixel being a block: that setting is about the
     * console's own picture, whose pixels are blocks. A picture made
     * smaller is smoothed (docs/PORT.md, "Remastered"). */
    if (w < picture_w || h < picture_h) {
        is_linear = TRUE;
    }
    port_GlTakeWindow(window_w, window_h);
    if (g_gl.sample > 1) {
        /* the pass that averaged the samples down drew the picture alone */
        port_GlPresent(g_gl.picture_texture, g_gl.picture_w, g_gl.picture_h, 0,
                       0, picture_w, picture_h, window_w, window_h, x, y, w, h,
                       is_linear, FALSE);
    } else {
        port_GlPresent(g_gl.color_texture, g_gl.target_w, g_gl.target_h,
                       g_gl.display_x * g_gl.scale_x,
                       g_gl.display_y * g_gl.scale_y, picture_w, picture_h,
                       window_w, window_h, x, y, w, h, is_linear, FALSE);
    }
    port_GlLeaveWindow();
    return TRUE;
}

s32 port_GlShowPixels(
    const u32 *pixels, s32 pixels_w, s32 pixels_h, s32 window_w, s32 window_h,
    s32 x, s32 y, s32 w, s32 h, s32 is_linear, s32 is_over) {
    if (!g_gl.is_started || pixels_w <= 0 || pixels_h <= 0 || w <= 0 ||
        h <= 0) {
        return FALSE;
    }
    glBindTexture(GL_TEXTURE_2D, g_gl.host_texture);
    if (pixels_w != g_gl.host_w || pixels_h != g_gl.host_h) {
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, pixels_w, pixels_h, 0, GL_BGRA,
                     GL_UNSIGNED_BYTE, NULL);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        g_gl.host_w = pixels_w;
        g_gl.host_h = pixels_h;
    }
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, pixels_w, pixels_h, GL_BGRA,
                    GL_UNSIGNED_BYTE, pixels);
    port_GlTakeWindow(window_w, window_h);
    port_GlPresent(
        g_gl.host_texture, pixels_w, pixels_h, 0, 0, pixels_w, pixels_h,
        window_w, window_h, x, y, w, h, is_linear, is_over);
    port_GlLeaveWindow();
    return TRUE;
}

s32 port_GlReadWindow(u32 *out, u32 room, s32 window_w, s32 window_h) {
    s32 line;
    s32 at;

    if (!g_gl.is_started || window_w <= 0 || window_h <= 0 ||
        (u32)(window_w * window_h) > room) {
        return FALSE;
    }
    glBindFramebuffer(GL_FRAMEBUFFER, GL_WINDOW_FRAMEBUFFER);
    glReadPixels(0, 0, window_w, window_h, GL_RGBA, GL_UNSIGNED_BYTE, out);
    glBindFramebuffer(GL_FRAMEBUFFER, g_gl.framebuffer);
    /* the window's lines come up from the bottom, and the pixels as red,
     * green, blue, alpha; the port's are 0xFFRRGGBB from the top */
    for (line = 0; line < window_h / 2; line++) {
        s32 other = window_h - 1 - line;

        for (at = 0; at < window_w; at++) {
            u32 one = out[line * window_w + at];

            out[line * window_w + at] = out[other * window_w + at];
            out[other * window_w + at] = one;
        }
    }
    for (at = 0; at < window_w * window_h; at++) {
        u32 pixel = out[at];

        out[at] = 0xFF000000U | ((pixel & 0xFF) << 16) | (pixel & 0xFF00) |
                  ((pixel >> 16) & 0xFF);
    }
    return TRUE;
}

void port_GlNoteFrameTime(u32 microseconds) { g_gl.frame_time = microseconds; }

u32 port_GlGetFrameTime(void) { return g_gl.frame_time; }

u32 port_GlGetVertexCount(void) { return g_gl.shown_vertices; }

u32 port_GlGetDrawCount(void) { return g_gl.shown_draws; }

u32 port_GlGetUploadCount(void) { return g_gl.shown_uploads; }

u32 port_GlGetDroppedCount(void) { return g_gl.shown_dropped; }

u32 port_GlGetOutsideCount(void) { return g_gl.shown_outside; }

u32 port_GlGetWideCount(void) { return g_gl.shown_wide; }
