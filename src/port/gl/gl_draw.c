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
#include "port_gpu.h"
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
    GL(void, glFinish, (void))                                                 \
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
    GL(GLenum, glCheckFramebufferStatus, (GLenum))                             \
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
    GL(void, glUniform2f, (GLint, GLfloat, GLfloat))                           \
    GL(void, glUniform4f, (GLint, GLfloat, GLfloat, GLfloat, GLfloat))         \
    GL(void, glDrawArrays, (GLenum, GLint, GLsizei))

#define GL(result, name, args) static result(*name) args;
PORT_GL_FUNCTIONS
#undef GL

/* ---- the picture ---- */

/* A batch's vertices: a frame of the fight has about ten thousand. */
#define VERTICES_MAX 65536
/* The longest run of pixels sent to the card in one go. */
#define UPLOAD_PIXELS_MAX (PORT_VRAM_PIXELS)

/* The bits of a vertex's `flags` byte. */
/* The most a primitive may span, as the chip has it. */
#define SPAN_X_MAX 1023
#define SPAN_Y_MAX 511

#define FLAG_TEXTURED 1
#define FLAG_RAW 2
#define FLAG_HALF 4
#define FLAG_DITHERED 8

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
} PortGlState;

static struct {
    s32 is_started;
    PortGlOptions options;
    GLuint program;
    GLuint blit_program;
    GLuint vram_texture;
    GLuint color_texture;
    GLuint framebuffer;
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
    GLint at_bilinear;
    GLint at_pass;
    GLint blit_at_target;
    GLint blit_at_rect;
    GLint blit_at_scale;
    GLint blit_at_vram;
    GLint blit_at_shift;
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
    PortGlState state;
    s32 has_state;
    PortDirtyRect mirror;
    s32 display_x;
    s32 display_y;
    s32 display_w;
    s32 display_h;
    s32 display_is_on;
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
    "uniform vec2 u_target;\n"
    "uniform vec2 u_scale;\n"
    /* the texture in perspective, and as the console had it */
    "out vec2 v_texture;\n"
    "noperspective out vec2 v_texture_flat;\n"
    /* a colour changes evenly across the screen, as the chip's does */
    "noperspective out vec3 v_color;\n"
    "flat out uvec4 v_flags;\n"
    "flat out ivec4 v_page;\n"
    "flat out uvec4 v_info;\n"
    "flat out uvec4 v_window;\n"
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
    "    v_texture = a_texture;\n"
    "    v_texture_flat = a_texture;\n"
    "    v_color = vec3(a_color.xyz);\n"
    "    v_flags = a_color;\n"
    "    v_page = a_page;\n"
    "    v_info = a_info;\n"
    "    v_window = a_window;\n"
    "}\n";

static const char g_gl_fragment_text[] =
    "#version 330 core\n"
    "uniform usampler2D u_vram;\n"
    "uniform vec2 u_target;\n"
    "uniform vec2 u_scale;\n"
    "uniform int u_affine;\n" /* the texture flat, as the console */
    "uniform int u_exact;\n"  /* five bits a channel, with the dither */
    "uniform int u_bilinear;\n"
    "uniform int u_pass;\n"
    "in vec2 v_texture;\n"
    "noperspective in vec2 v_texture_flat;\n"
    "noperspective in vec3 v_color;\n"
    "flat in uvec4 v_flags;\n"
    "flat in ivec4 v_page;\n"
    "flat in uvec4 v_info;\n"
    "flat in uvec4 v_window;\n"
    "out vec4 o_color;\n"
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
    "void main() {\n"
    "    vec2 uv = (u_affine != 0) ? v_texture_flat : v_texture;\n"
    /* the chip works in whole numbers: a colour that comes out of the
     * interpolation as 203.99999 is the 204 it means */
    "    vec3 color = (u_exact != 0) ? floor(v_color + vec3(0.5)) : v_color;\n"
    "    bool mixes = (v_flags.w & 4u) != 0u;\n"
    "    ivec2 pixel = ivec2(floor(gl_FragCoord.xy / u_scale));\n"
    "    float shade = 1.0;\n"
    "    if ((v_flags.w & 1u) != 0u) {\n"
    "        uint texel = 0u;\n"
    "        vec3 texel_color = vec3(0.0);\n"
    "        float weight = 0.0;\n"
    "        if (u_bilinear != 0) {\n"
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
    "in vec2 v_at;\n"
    "out vec4 o_color;\n"
    "void main() {\n"
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

static GLuint port_GlMakeProgram(const char *vertex, const char *fragment) {
    GLuint program;
    GLuint one = port_GlMakeShader(GL_VERTEX_SHADER, vertex);
    GLuint two = port_GlMakeShader(GL_FRAGMENT_SHADER, fragment);
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
        port_GlSay("the shaders do not link: ", log);
        return 0;
    }
    return program;
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
    glEnableVertexAttribArray(0);
    glEnableVertexAttribArray(1);
    glEnableVertexAttribArray(2);
    glEnableVertexAttribArray(3);
    glEnableVertexAttribArray(4);
    glEnableVertexAttribArray(5);
}

/* The colour buffer: all of video memory at the scale. */
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
    glBindFramebuffer(GL_FRAMEBUFFER, g_gl.framebuffer);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D,
                           g_gl.color_texture, 0);
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
        &g_port_vram[g_gl.mirror.top * PORT_VRAM_WIDTH + g_gl.mirror.left]);
    glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
    g_gl.upload_count += (u32)(w * h);
    port_ClearDirtyRect(&g_gl.mirror);
}

/* ---- the batches ---- */

/* The picture's margin for the shape asked for and the display's
 * rectangle as it is (port_remaster.h). */
static void port_GlFindMargin(void) {
    g_gl.margin = port_RemasterMargin(
        g_gl.display_w, g_gl.options.aspect_w, g_gl.options.aspect_h);
}

static void port_GlSetState(const PortGlState *state) {
    s32 top;

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
    top = state->clip_top * g_gl.draw_y;
    if (state->is_wide) {
        /* the scene, across the whole of a wider picture */
        glScissor((g_gl.display_x) * g_gl.draw_x, top,
                  (g_gl.display_w + 2 * g_gl.margin) * g_gl.draw_x,
                  (state->clip_bottom - state->clip_top + 1) * g_gl.draw_y);
        return;
    }
    glScissor((state->clip_left + g_gl.margin) * g_gl.draw_x, top,
              (state->clip_right - state->clip_left + 1) * g_gl.draw_x,
              (state->clip_bottom - state->clip_top + 1) * g_gl.draw_y);
}

static void port_GlFlush(void) {
    if (g_gl.count == 0) {
        return;
    }
    port_GlUploadMirror();
    glUseProgram(g_gl.program);
    glBindVertexArray(g_gl.array);
    glBindBuffer(GL_ARRAY_BUFFER, g_gl.buffer);
    glBufferData(
        GL_ARRAY_BUFFER, (GLsizeiptr)((u32)g_gl.count * sizeof(*g_gl.vertices)),
        g_gl.vertices, GL_STREAM_DRAW);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, g_gl.vram_texture);
    glUniform1i(g_gl.at_vram, 0);
    glUniform2f(g_gl.at_target, (GLfloat)g_gl.target_w, (GLfloat)g_gl.target_h);
    glUniform2f(g_gl.at_scale, (GLfloat)g_gl.draw_x, (GLfloat)g_gl.draw_y);
    glUniform1i(g_gl.at_affine, g_gl.options.is_exact ? 1 : 0);
    glUniform1i(g_gl.at_exact, g_gl.options.is_exact ? 1 : 0);
    glUniform1i(
        g_gl.at_bilinear, (g_gl.options.is_bilinear && !g_gl.options.is_exact));
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
    g_gl.vertex_count += (u32)g_gl.count;
    g_gl.count = 0;
}

static s32 port_GlStateIsSame(const PortGlState *a, const PortGlState *b) {
    return a->is_half == b->is_half && a->blend == b->blend &&
           a->clip_left == b->clip_left && a->clip_top == b->clip_top &&
           a->clip_right == b->clip_right && a->clip_bottom == b->clip_bottom &&
           a->is_wide == b->is_wide;
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

    if (prim->has_depth && !g_gl.options.is_exact) {
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
    to->info[2] = 0;
    to->info[3] = 0;
    to->window[0] = (u8)prim->window_and_u;
    to->window[1] = (u8)prim->window_or_u;
    to->window[2] = (u8)prim->window_and_v;
    to->window[3] = (u8)prim->window_or_v;
}

/* The w of a corner: any positive number in proportion to how far the
 * point is, which is what a perspective-correct texture needs. Without
 * a depth every corner has 1, and the texture is flat across the
 * primitive as the console's is. */
static GLfloat port_GlW(const PortRemasterPrim *prim, s32 corner) {
    if (!prim->has_depth) {
        return 1.0f;
    }
    return (GLfloat)prim->corner[corner].depth / 1024.0f;
}

/* The chip draws no triangle that spans more than 1023 pixels across
 * or 511 down, and none of no area (src/port/raster.c): a corner the
 * projection put off the screen is held to eleven bits, so a polygon
 * that is partly behind the camera comes out spanning the whole range,
 * and the game counts on its not being drawn. The hardware path has to
 * leave out the same ones, by the same whole-pixel corners. */
static s32 port_GlIsDrawn(const PortRemasterPrim *prim, s32 a, s32 b, s32 c) {
    s32 left = prim->corner[a].ix;
    s32 right = left;
    s32 top = prim->corner[a].iy;
    s32 bottom = top;
    s32 area;
    s32 i;
    s32 corner[3];

    corner[0] = a;
    corner[1] = b;
    corner[2] = c;
    for (i = 1; i < 3; i++) {
        s32 x = prim->corner[corner[i]].ix;
        s32 y = prim->corner[corner[i]].iy;

        left = x < left ? x : left;
        right = x > right ? x : right;
        top = y < top ? y : top;
        bottom = y > bottom ? y : bottom;
    }
    if (right - left > SPAN_X_MAX || bottom - top > SPAN_Y_MAX) {
        return FALSE;
    }
    area = (prim->corner[b].ix - prim->corner[a].ix) *
               (prim->corner[c].iy - prim->corner[a].iy) -
           (prim->corner[c].ix - prim->corner[a].ix) *
               (prim->corner[b].iy - prim->corner[a].iy);
    return area != 0;
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

static void port_GlDraw(const PortRemasterPrim *prim) {
    PortGlState state;
    s32 flags = 0;

    if (!g_gl.is_started) {
        return;
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
    state.is_half = prim->is_half;
    state.blend = prim->blend;
    state.clip_left = prim->clip_left;
    state.clip_top = prim->clip_top;
    state.clip_right = prim->clip_right;
    state.clip_bottom = prim->clip_bottom;
    /* The whole width of a wider picture, where the game was not
     * restricting the drawing area to part of the screen (the pause
     * menu, the replay's panels): the scene, which is a primitive the
     * projection gave a depth to, and a group of the fight's HUD the game
     * anchored to an edge, which is drawn out in a band and would be cut
     * off at the console's own edge (port_remaster.h). Every other flat
     * primitive keeps the console's drawing area and stays four by three
     * in the middle. */
    state.is_wide = g_gl.margin > 0 &&
                    (prim->has_depth || prim->anchor != PORT_ANCHOR_MIDDLE) &&
                    prim->clip_left <= g_gl.display_x &&
                    prim->clip_right >= g_gl.display_x + g_gl.display_w - 1;
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

/* The bands beside the console's own 640 pixels, over the lines `top`
 * to `top + height` of video memory, cleared to black.
 *
 * The game clears its drawing area at the start of each frame and knows
 * nothing of the bands, so what the scene last drew into them would
 * stay. It has to happen here, where that clear does, and not at the
 * top of a frame: the game draws its frame after the picture of the
 * frame before it has been read (DrawOTag comes past the vertical blank
 * in its loop), so a clear at the top of a frame would wipe the bands of
 * the picture about to be read. Black is what the game clears to. */
static void port_GlClearBands(s32 top, s32 height) {
    s32 left = g_gl.display_x * g_gl.draw_x;
    s32 band = g_gl.margin * g_gl.draw_x;
    s32 right = (g_gl.display_x + g_gl.margin + g_gl.display_w) * g_gl.draw_x;

    if (g_gl.margin <= 0 || g_gl.display_w <= 0) {
        return;
    }
    glDisable(GL_BLEND);
    glEnable(GL_SCISSOR_TEST);
    glScissor(left, top * g_gl.draw_y, band, height * g_gl.draw_y);
    glClear(GL_COLOR_BUFFER_BIT);
    glScissor(right, top * g_gl.draw_y, band, height * g_gl.draw_y);
    glClear(GL_COLOR_BUFFER_BIT);
    g_gl.has_state = FALSE;
}

/* A rectangle of video memory the software GPU wrote: it is sent to the
 * card and stretched into the colour buffer, in its turn among the
 * primitives. */
static void port_GlWroteVram(s32 x, s32 y, s32 w, s32 h) {
    PortDirtyRect rect;

    if (!g_gl.is_started || w <= 0 || h <= 0) {
        return;
    }
    port_GlFlush();
    port_ClearDirtyRect(&rect);
    port_AddDirtyRect(&rect, x, y, w, h);
    port_AddDirtyRect(&g_gl.mirror, x, y, w, h);
    port_GlUploadMirror();
    glUseProgram(g_gl.blit_program);
    glBindVertexArray(g_gl.blit_array);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, g_gl.vram_texture);
    glUniform1i(g_gl.blit_at_vram, 0);
    glUniform2f(
        g_gl.blit_at_target, (GLfloat)g_gl.target_w, (GLfloat)g_gl.target_h);
    glUniform2f(g_gl.blit_at_scale, (GLfloat)g_gl.draw_x, (GLfloat)g_gl.draw_y);
    glUniform4f(g_gl.blit_at_rect, (GLfloat)rect.left, (GLfloat)rect.top,
                (GLfloat)(rect.right - rect.left + 1),
                (GLfloat)(rect.bottom - rect.top + 1));
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
    glDrawArrays(GL_TRIANGLES, 0, 6);
    glEnable(GL_SCISSOR_TEST);
    g_gl.draw_count++;
    g_gl.has_state = FALSE;
    /* a fill or a copy as wide as the console's picture is the frame's
     * own clear: the bands go with it */
    if (rect.right - rect.left + 1 >= g_gl.display_w) {
        port_GlClearBands(rect.top, rect.bottom - rect.top + 1);
    }
}

static void port_GlBegin(void) { g_gl.has_state = FALSE; }

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
    port_AddDirtyRect(&g_gl.mirror, x, y, w, h);
    port_GlUploadMirror();
}

void port_GlSaveCounts(u32 *three) {
    three[0] = g_gl.vertex_count;
    three[1] = g_gl.draw_count;
    three[2] = g_gl.upload_count;
}

void port_GlLoadCounts(const u32 *three) {
    g_gl.vertex_count = three[0];
    g_gl.draw_count = three[1];
    g_gl.upload_count = three[2];
}

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
    if (g_gl.is_started && (across != g_gl.scale_x || down != g_gl.scale_y ||
                            sample != g_gl.sample)) {
        g_gl.scale_x = across;
        g_gl.scale_y = down;
        g_gl.sample = sample;
        port_GlMakeTarget();
        port_ClearDirtyRect(&g_gl.mirror);
        port_AddDirtyRect(
            &g_gl.mirror, 0, 0, PORT_VRAM_WIDTH, PORT_VRAM_HEIGHT);
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
    g_gl.options = *options;
    port_GlScalesOf(options, &g_gl.scale_x, &g_gl.scale_y);
    g_gl.sample = port_GlSampleOf(options);
    g_gl.program = port_GlMakeProgram(g_gl_vertex_text, g_gl_fragment_text);
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
    g_gl.at_bilinear = glGetUniformLocation(g_gl.program, "u_bilinear");
    g_gl.at_pass = glGetUniformLocation(g_gl.program, "u_pass");
    g_gl.blit_at_target = glGetUniformLocation(g_gl.blit_program, "u_target");
    g_gl.blit_at_rect = glGetUniformLocation(g_gl.blit_program, "u_rect");
    g_gl.blit_at_scale = glGetUniformLocation(g_gl.blit_program, "u_scale");
    g_gl.blit_at_vram = glGetUniformLocation(g_gl.blit_program, "u_vram");
    g_gl.blit_at_shift = glGetUniformLocation(g_gl.blit_program, "u_shift");
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

    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    glEnable(GL_SCISSOR_TEST);
    glScissor(0, 0, g_gl.target_w, g_gl.target_h);
    g_gl.is_started = TRUE;
    /* video memory as it is now, all of it */
    port_ClearDirtyRect(&g_gl.mirror);
    port_AddDirtyRect(&g_gl.mirror, 0, 0, PORT_VRAM_WIDTH, PORT_VRAM_HEIGHT);
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
    if (g_gl.sample > 1) {
        port_GlResolve();
    }
    glFinish();
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
    port_ClearDirtyRect(&g_gl.mirror);
    port_AddDirtyRect(&g_gl.mirror, 0, 0, PORT_VRAM_WIDTH, PORT_VRAM_HEIGHT);
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
