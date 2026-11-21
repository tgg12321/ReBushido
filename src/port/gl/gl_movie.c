/* The movie made better in the window (gl_movie.h; docs/PORT.md, "The
 * movie made better").
 *
 * The passes `larger` and `sharper` are AMD FidelityFX Super Resolution
 * 1's EASU and RCAS (ffx_fsr1.h), written again here in GLSL 3.30 from
 * AMD's own, which is under this licence:
 *
 *   Copyright (c) 2021 Advanced Micro Devices, Inc. All rights reserved.
 *
 *   Permission is hereby granted, free of charge, to any person
 *   obtaining a copy of this software and associated documentation files
 *   (the "Software"), to deal in the Software without restriction,
 *   including without limitation the rights to use, copy, modify, merge,
 *   publish, distribute, sublicense, and/or sell copies of the Software,
 *   and to permit persons to whom the Software is furnished to do so,
 *   subject to the following conditions:
 *
 *   The above copyright notice and this permission notice shall be
 *   included in all copies or substantial portions of the Software.
 *
 *   THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 *   EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 *   MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 *   NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 *   BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 *   ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 *   CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 *   SOFTWARE.
 *
 * The three passes before them are the project's own. */
#include <stddef.h>
#include <stdio.h>

#include "common.h"
#include "gl_movie.h"

/* ---- GL, as much of it as this file uses ---- */

typedef unsigned int GLenum;
typedef unsigned char GLboolean;
typedef int GLint;
typedef unsigned int GLuint;
typedef int GLsizei;
typedef float GLfloat;
typedef char GLchar;

#define GL_TRIANGLES 0x0004
#define GL_TEXTURE_2D 0x0DE1
#define GL_UNSIGNED_BYTE 0x1401
#define GL_FLOAT 0x1406
#define GL_RGBA 0x1908
#define GL_BGRA 0x80E1
#define GL_RGBA8 0x8058
#define GL_RGBA16F 0x881A
#define GL_NEAREST 0x2600
#define GL_LINEAR 0x2601
#define GL_TEXTURE_MAG_FILTER 0x2800
#define GL_TEXTURE_MIN_FILTER 0x2801
#define GL_TEXTURE_WRAP_S 0x2802
#define GL_TEXTURE_WRAP_T 0x2803
#define GL_CLAMP_TO_EDGE 0x812F
#define GL_TEXTURE0 0x84C0
#define GL_FRAMEBUFFER 0x8D40
#define GL_DRAW_FRAMEBUFFER_BINDING 0x8CA6
#define GL_COLOR_ATTACHMENT0 0x8CE0
#define GL_FRAMEBUFFER_COMPLETE 0x8CD5
#define GL_UNPACK_ALIGNMENT 0x0CF5
#define GL_ACTIVE_TEXTURE 0x84E0
#define GL_TEXTURE_BINDING_2D 0x8069
#define GL_CURRENT_PROGRAM 0x8B8D
#define GL_VERTEX_ARRAY_BINDING 0x85B5
#define GL_COMPILE_STATUS 0x8B81
#define GL_LINK_STATUS 0x8B82
#define GL_VERTEX_SHADER 0x8B31
#define GL_FRAGMENT_SHADER 0x8B30

#define PORT_GL_MOVIE_FUNCTIONS                                                \
    GL(void, glViewport, (GLint, GLint, GLsizei, GLsizei))                     \
    GL(void, glGetIntegerv, (GLenum, GLint *))                                 \
    GL(void, glBindTexture, (GLenum, GLuint))                                  \
    GL(void, glActiveTexture, (GLenum))                                        \
    GL(void, glGenTextures, (GLsizei, GLuint *))                               \
    GL(void, glTexImage2D,                                                     \
       (GLenum, GLint, GLint, GLsizei, GLsizei, GLint, GLenum, GLenum,         \
        const void *))                                                         \
    GL(void, glTexSubImage2D,                                                  \
       (GLenum, GLint, GLint, GLint, GLsizei, GLsizei, GLenum, GLenum,         \
        const void *))                                                         \
    GL(void, glTexParameteri, (GLenum, GLenum, GLint))                         \
    GL(void, glPixelStorei, (GLenum, GLint))                                   \
    GL(void, glBindFramebuffer, (GLenum, GLuint))                              \
    GL(void, glGenFramebuffers, (GLsizei, GLuint *))                           \
    GL(void, glFramebufferTexture2D, (GLenum, GLenum, GLenum, GLuint, GLint))  \
    GL(GLenum, glCheckFramebufferStatus, (GLenum))                             \
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
    GL(void, glUniform2i, (GLint, GLint, GLint))                               \
    GL(void, glDrawArrays, (GLenum, GLint, GLsizei))

#define GL(result, name, args) static result(*name) args;
PORT_GL_MOVIE_FUNCTIONS
#undef GL

/* ---- how strong ---- */

/* RCAS's sharpening, 1 its strongest; AMD gives it in stops down from
 * that, 2 to the minus stops: light over the console's own frames (0.6
 * stops), lighter still over a mod's (1.2), which a learned upscale has
 * already made sharp. */
#define SHARPEN_OWN 0.66f
#define SHARPEN_MOD 0.435f
/* The scale a frame is taken to have where none is known: the opening's
 * commonest. */
#define SCALE_UNKNOWN 3
/* The movie a mod may replace: the game has one (docs/PORT.md, "The
 * movie"). */
#define MOVIE_KEY "opening"
#define SIDE_MAX 8192

/* ---- the shaders ---- */

/* One triangle over the whole target; each pass works from
 * gl_FragCoord. */
static const char g_movie_vertex_text[] =
    "#version 330 core\n"
    "void main() {\n"
    "    vec2 at = vec2(float((gl_VertexID << 1) & 2), float(gl_VertexID & "
    "2));\n"
    "    gl_Position = vec4(at * 2.0 - 1.0, 0.0, 1.0);\n"
    "}\n";

/* What the three passes at the console's size share: a pixel of the
 * input by its place from the top, clamped to the picture, in steps of
 * 0 to 255; its brightness; and the chip's colour: red, green and blue of
 * brightness (Y) and the two differences (Cb, Cr), as src/port/mdec.c
 * has it. */
#define MOVIE_COMMON_SHADER                                                    \
    "#version 330 core\n"                                                      \
    "uniform sampler2D u_in;\n"                                                \
    "uniform ivec2 u_size;\n"                                                  \
    "uniform float u_scale;\n"                                                 \
    "out vec4 o_color;\n"                                                      \
    "vec3 at(ivec2 p) {\n"                                                     \
    "    return texelFetch(u_in, clamp(p, ivec2(0), u_size - 1), 0).rgb\n"     \
    "        * 255.0;\n"                                                       \
    "}\n"                                                                      \
    "float luma(vec3 c) { return dot(c, vec3(0.299, 0.587, 0.114)); }\n"       \
    "const mat3 YCC_TO_RGB = mat3(1.0, 1.0, 1.0, 0.0, -0.3437, 1.772,\n"       \
    "                             1.402, -0.7143, 0.0);\n"

/* colour: Cb and Cr are one value for each cell of 2 by 2 pixels, which
 * the chip lays over the four flat. They are taken back out of the four
 * (the step to red, green and blue is linear, so the mean of the four
 * pixels' differences is the cell's, but where a channel was clipped),
 * and each pixel takes them from the four cells nearest it, weighted by
 * how near their centres are: 9/16, 3/16, 3/16 and 1/16. Its brightness
 * stays its own. */
static const char g_movie_colour_text[] = MOVIE_COMMON_SHADER
    "vec2 cell(ivec2 c, mat3 to_ycc) {\n"
    "    c = clamp(c, ivec2(0), u_size / 2 - 1);\n"
    "    ivec2 p = c * 2;\n"
    "    vec3 sum = at(p) + at(p + ivec2(1, 0)) + at(p + ivec2(0, 1))\n"
    "             + at(p + ivec2(1, 1));\n"
    "    return (to_ycc * (sum * 0.25)).yz;\n"
    "}\n"
    "void main() {\n"
    "    mat3 to_ycc = inverse(YCC_TO_RGB);\n"
    "    ivec2 p = ivec2(gl_FragCoord.xy);\n"
    "    ivec2 c = p / 2;\n"
    "    ivec2 side = ivec2((p.x & 1) == 0 ? -1 : 1, (p.y & 1) == 0 ? -1 : 1);\n"
    "    vec2 chroma = 0.5625 * cell(c, to_ycc)\n"
    "                + 0.1875 * cell(c + ivec2(side.x, 0), to_ycc)\n"
    "                + 0.1875 * cell(c + ivec2(0, side.y), to_ycc)\n"
    "                + 0.0625 * cell(c + side, to_ycc);\n"
    "    float y = (to_ycc * at(p)).x;\n"
    "    vec3 rgb = YCC_TO_RGB * vec3(y, chroma);\n"
    "    o_color = vec4(clamp(rgb, 0.0, 255.0) / 255.0, 1.0);\n"
    "}\n";

/* blocks: across each edge between two 8 by 8 blocks, p1 p0 | q0 q1,
 * where the step from p0 to q0 is less than `alpha` and each side is
 * flat to within `beta` (so that the step is the quantisation's, not an
 * edge of the picture), the two pixels beside it are moved towards each
 * other by a quarter of the step and a little of the slope outside it,
 * held to `tc`, and the next two half as much towards the ramp between.
 * Each pixel adds what the edge across x and the edge across y give it.
 * All three grow with the frame's quantisation scale. */
static const char g_movie_blocks_text[] = MOVIE_COMMON_SHADER
    "vec3 edge(ivec2 p, ivec2 d, int k, int size) {\n"
    "    int r = k & 7;\n"
    "    int b;\n"
    "    if (r == 6 || r == 7) { b = k - r + 8; }\n"
    "    else if (r == 0 || r == 1) { b = k - r; }\n"
    "    else { return vec3(0.0); }\n"
    "    if (b <= 0 || b >= size) { return vec3(0.0); }\n"
    "    ivec2 q = p - d * (k - b);\n"
    "    vec3 p2 = at(q - 3 * d);\n"
    "    vec3 p1 = at(q - 2 * d);\n"
    "    vec3 p0 = at(q - d);\n"
    "    vec3 q0 = at(q);\n"
    "    vec3 q1 = at(q + d);\n"
    "    vec3 q2 = at(q + 2 * d);\n"
    "    float alpha = 6.0 + 3.0 * u_scale;\n"
    "    float beta = 2.0 + 0.8 * u_scale;\n"
    "    float tc = 2.0 + 1.2 * u_scale;\n"
    "    if (abs(luma(q0) - luma(p0)) >= alpha ||\n"
    "        abs(luma(p1) - luma(p0)) >= beta ||\n"
    "        abs(luma(q1) - luma(q0)) >= beta) { return vec3(0.0); }\n"
    "    vec3 delta = clamp(((q0 - p0) * 4.0 + (p1 - q1)) / 8.0, -tc, tc);\n"
    "    vec3 mid = (p0 + q0) * 0.5;\n"
    "    if (r == 7) { return delta; }\n"
    "    if (r == 0) { return -delta; }\n"
    "    if (r == 6) { return clamp((p2 + mid) * 0.5 - p1, -tc * 0.5, tc * "
    "0.5); }\n"
    "    return clamp((q2 + mid) * 0.5 - q1, -tc * 0.5, tc * 0.5);\n"
    "}\n"
    "void main() {\n"
    "    ivec2 p = ivec2(gl_FragCoord.xy);\n"
    "    vec3 c = at(p) + edge(p, ivec2(1, 0), p.x, u_size.x)\n"
    "                   + edge(p, ivec2(0, 1), p.y, u_size.y);\n"
    "    o_color = vec4(clamp(c, 0.0, 255.0) / 255.0, 1.0);\n"
    "}\n";

/* ringing: each pixel the mean of the nine about it, each weighted by its
 * place (1, a half beside, 0.3 at a corner) and by how near its
 * brightness is to the middle one's, so a ripple a few steps high is
 * smoothed and an edge, many steps high, is not. */
static const char g_movie_ringing_text[] = MOVIE_COMMON_SHADER
    "void main() {\n"
    "    ivec2 p = ivec2(gl_FragCoord.xy);\n"
    "    vec3 middle = at(p);\n"
    "    float l = luma(middle);\n"
    "    float sigma = 2.0 + 0.9 * u_scale;\n"
    "    vec3 sum = vec3(0.0);\n"
    "    float weights = 0.0;\n"
    "    for (int dy = -1; dy <= 1; dy++) {\n"
    "        for (int dx = -1; dx <= 1; dx++) {\n"
    "            vec3 n = at(p + ivec2(dx, dy));\n"
    "            float place = (dx == 0 && dy == 0) ? 1.0\n"
    "                        : (dx == 0 || dy == 0) ? 0.5 : 0.3;\n"
    "            float apart = (luma(n) - l) / sigma;\n"
    "            float w = place * exp(-apart * apart);\n"
    "            sum += n * w;\n"
    "            weights += w;\n"
    "        }\n"
    "    }\n"
    "    o_color = vec4(sum / weights / 255.0, 1.0);\n"
    "}\n";

/* larger: FSR 1's EASU. For a pixel of the output, the 12 texels about
 * the point it falls on (b c / e f g h / i j k l / n o); the direction
 * and length of an edge there from the brightness of the four nearest
 * quads, mixed by where the point is; a kernel near Lanczos-2, stretched
 * along the edge and narrowed across it, over the 12; and the answer held
 * between the least and the most of the four nearest, so that it never
 * rings. */
static const char g_movie_larger_text[] =
    "#version 330 core\n"
    "uniform sampler2D u_in;\n"
    "uniform ivec2 u_size;\n" /* the input's */
    "uniform ivec2 u_out;\n"  /* the output's */
    "out vec4 o_color;\n"
    "vec3 at(ivec2 p) {\n"
    "    return texelFetch(u_in, clamp(p, ivec2(0), u_size - 1), 0).rgb;\n"
    "}\n"
    "float luma(vec3 c) { return c.b * 0.5 + (c.r * 0.5 + c.g); }\n"
    "void set(inout vec2 dir, inout float len, vec2 pp, float w,\n"
    "         float la, float lb, float lc, float ld, float le) {\n"
    "    float dc = ld - lc;\n"
    "    float cb = lc - lb;\n"
    "    float len_x = max(abs(dc), abs(cb));\n"
    "    len_x = len_x > 0.0 ? 1.0 / len_x : 0.0;\n"
    "    float dir_x = ld - lb;\n"
    "    dir.x += dir_x * w;\n"
    "    len_x = clamp(abs(dir_x) * len_x, 0.0, 1.0);\n"
    "    len_x *= len_x;\n"
    "    len += len_x * w;\n"
    "    float ec = le - lc;\n"
    "    float ca = lc - la;\n"
    "    float len_y = max(abs(ec), abs(ca));\n"
    "    len_y = len_y > 0.0 ? 1.0 / len_y : 0.0;\n"
    "    float dir_y = le - la;\n"
    "    dir.y += dir_y * w;\n"
    "    len_y = clamp(abs(dir_y) * len_y, 0.0, 1.0);\n"
    "    len_y *= len_y;\n"
    "    len += len_y * w;\n"
    "}\n"
    "void tap(inout vec3 sum, inout float weights, vec2 off, vec2 dir,\n"
    "         vec2 len, float lob, float clp, vec3 c) {\n"
    "    vec2 v = vec2(off.x * dir.x + off.y * dir.y,\n"
    "                  off.x * -dir.y + off.y * dir.x) * len;\n"
    "    float d2 = min(v.x * v.x + v.y * v.y, clp);\n"
    "    float wb = 2.0 / 5.0 * d2 - 1.0;\n"
    "    float wa = lob * d2 - 1.0;\n"
    "    wb *= wb;\n"
    "    wa *= wa;\n"
    "    wb = 25.0 / 16.0 * wb - (25.0 / 16.0 - 1.0);\n"
    "    float w = wb * wa;\n"
    "    sum += c * w;\n"
    "    weights += w;\n"
    "}\n"
    "void main() {\n"
    /* the output's pixel from the top, and where it falls in the input;
     * written with the top in the last row, as the window has it */
    "    vec2 o = vec2(gl_FragCoord.x, float(u_out.y) - gl_FragCoord.y);\n"
    "    vec2 src = o * vec2(u_size) / vec2(u_out) - 0.5;\n"
    "    vec2 fp = floor(src);\n"
    "    vec2 pp = src - fp;\n"
    "    ivec2 f0 = ivec2(fp);\n"
    "    vec3 b = at(f0 + ivec2(0, -1));\n"
    "    vec3 c = at(f0 + ivec2(1, -1));\n"
    "    vec3 e = at(f0 + ivec2(-1, 0));\n"
    "    vec3 f = at(f0);\n"
    "    vec3 g = at(f0 + ivec2(1, 0));\n"
    "    vec3 h = at(f0 + ivec2(2, 0));\n"
    "    vec3 i = at(f0 + ivec2(-1, 1));\n"
    "    vec3 j = at(f0 + ivec2(0, 1));\n"
    "    vec3 k = at(f0 + ivec2(1, 1));\n"
    "    vec3 l = at(f0 + ivec2(2, 1));\n"
    "    vec3 n = at(f0 + ivec2(0, 2));\n"
    "    vec3 oo = at(f0 + ivec2(1, 2));\n"
    "    float bl = luma(b), cl = luma(c), el = luma(e), fl = luma(f);\n"
    "    float gl = luma(g), hl = luma(h), il = luma(i), jl = luma(j);\n"
    "    float kl = luma(k), ll = luma(l), nl = luma(n), ol = luma(oo);\n"
    "    vec2 dir = vec2(0.0);\n"
    "    float len = 0.0;\n"
    "    set(dir, len, pp, (1.0 - pp.x) * (1.0 - pp.y), bl, el, fl, gl, jl);\n"
    "    set(dir, len, pp, pp.x * (1.0 - pp.y), cl, fl, gl, hl, kl);\n"
    "    set(dir, len, pp, (1.0 - pp.x) * pp.y, fl, il, jl, kl, nl);\n"
    "    set(dir, len, pp, pp.x * pp.y, gl, jl, kl, ll, ol);\n"
    "    vec2 dir2 = dir * dir;\n"
    "    float dir_r = dir2.x + dir2.y;\n"
    "    bool zero = dir_r < 1.0 / 32768.0;\n"
    "    dir_r = zero ? 1.0 : inversesqrt(dir_r);\n"
    "    dir.x = zero ? 1.0 : dir.x;\n"
    "    dir *= dir_r;\n"
    "    len = len * 0.5;\n"
    "    len *= len;\n"
    "    float stretch = (dir.x * dir.x + dir.y * dir.y)\n"
    "                  / max(abs(dir.x), abs(dir.y));\n"
    "    vec2 len2 = vec2(1.0 + (stretch - 1.0) * len, 1.0 - 0.5 * len);\n"
    "    float lob = 0.5 + ((1.0 / 4.0 - 0.04) - 0.5) * len;\n"
    "    float clp = 1.0 / lob;\n"
    "    vec3 sum = vec3(0.0);\n"
    "    float weights = 0.0;\n"
    "    tap(sum, weights, vec2(0.0, -1.0) - pp, dir, len2, lob, clp, b);\n"
    "    tap(sum, weights, vec2(1.0, -1.0) - pp, dir, len2, lob, clp, c);\n"
    "    tap(sum, weights, vec2(-1.0, 1.0) - pp, dir, len2, lob, clp, i);\n"
    "    tap(sum, weights, vec2(0.0, 1.0) - pp, dir, len2, lob, clp, j);\n"
    "    tap(sum, weights, vec2(0.0, 0.0) - pp, dir, len2, lob, clp, f);\n"
    "    tap(sum, weights, vec2(-1.0, 0.0) - pp, dir, len2, lob, clp, e);\n"
    "    tap(sum, weights, vec2(1.0, 1.0) - pp, dir, len2, lob, clp, k);\n"
    "    tap(sum, weights, vec2(2.0, 1.0) - pp, dir, len2, lob, clp, l);\n"
    "    tap(sum, weights, vec2(2.0, 0.0) - pp, dir, len2, lob, clp, h);\n"
    "    tap(sum, weights, vec2(1.0, 0.0) - pp, dir, len2, lob, clp, g);\n"
    "    tap(sum, weights, vec2(1.0, 2.0) - pp, dir, len2, lob, clp, oo);\n"
    "    tap(sum, weights, vec2(0.0, 2.0) - pp, dir, len2, lob, clp, n);\n"
    "    vec3 least = min(min(f, g), min(j, k));\n"
    "    vec3 most = max(max(f, g), max(j, k));\n"
    "    o_color = vec4(min(most, max(least, sum / weights)), 1.0);\n"
    "}\n";

/* sharper: FSR 1's RCAS, over the larger picture into the window. Each
 * pixel less a part of its four neighbours, the part as large as it may
 * be without taking the pixel past the least or the most of them (no
 * ringing, no clipping), at most 3/16, times `u_sharp`. */
static const char g_movie_sharper_text[] =
    "#version 330 core\n"
    "uniform sampler2D u_in;\n"
    "uniform ivec2 u_size;\n"
    "uniform ivec2 u_place;\n" /* the rectangle's corner, from the bottom */
    "uniform float u_sharp;\n"
    "out vec4 o_color;\n"
    "vec3 at(ivec2 p) {\n"
    "    return texelFetch(u_in, clamp(p, ivec2(0), u_size - 1), 0).rgb;\n"
    "}\n"
    "float luma(vec3 c) { return c.b * 0.5 + (c.r * 0.5 + c.g); }\n"
    "void main() {\n"
    /* larger wrote the picture's top into its last row, so its rows run
     * up as the window's do */
    "    ivec2 p = ivec2(gl_FragCoord.xy) - u_place;\n"
    "    vec3 b = at(p + ivec2(0, -1));\n"
    "    vec3 d = at(p + ivec2(-1, 0));\n"
    "    vec3 e = at(p);\n"
    "    vec3 f = at(p + ivec2(1, 0));\n"
    "    vec3 h = at(p + ivec2(0, 1));\n"
    "    vec3 least = min(min(b, d), min(f, h));\n"
    "    vec3 most = max(max(b, d), max(f, h));\n"
    "    vec3 hit_least = least / max(4.0 * most, vec3(1e-5));\n"
    "    vec3 hit_most = (1.0 - most) / min(4.0 * least - 4.0, vec3(-1e-5));\n"
    "    vec3 lobes = max(-hit_least, hit_most);\n"
    "    float lobe = max(-(0.25 - 1.0 / 16.0),\n"
    "                     min(max(lobes.r, max(lobes.g, lobes.b)), 0.0))\n"
    "               * u_sharp;\n"
    /* less where the five are noise: their brightness far from what the
     * four make of the middle, against the spread of the five */
    "    float bl = luma(b), dl = luma(d), el = luma(e), fl = luma(f);\n"
    "    float hl = luma(h);\n"
    "    float noise = 0.25 * (bl + dl + fl + hl) - el;\n"
    "    float spread = max(max(max(bl, dl), max(el, fl)), hl)\n"
    "                 - min(min(min(bl, dl), min(el, fl)), hl);\n"
    "    noise = clamp(abs(noise) / max(spread, 1e-5), 0.0, 1.0);\n"
    "    lobe *= -0.5 * noise + 1.0;\n"
    "    vec3 c = (lobe * (b + d + f + h) + e) / (4.0 * lobe + 1.0);\n"
    "    o_color = vec4(clamp(c, 0.0, 1.0), 1.0);\n"
    "}\n";

/* ---- the passes ---- */

typedef struct {
    GLuint program;
    GLint at_in;
    GLint at_size;
    GLint at_scale;
    GLint at_out;
    GLint at_place;
    GLint at_sharp;
} MoviePass;

/* A texture the passes draw into, with its framebuffer. */
typedef struct {
    GLuint texture;
    GLuint framebuffer;
    s32 w;
    s32 h;
} MovieTarget;

enum {
    PASS_COLOUR,
    PASS_BLOCKS,
    PASS_RINGING,
    PASS_LARGER,
    PASS_SHARPER,
    PASSES
};

static struct {
    s32 is_ready;
    char problem[300];
    MoviePass pass[PASSES];
    GLuint array;
    /* the software GPU's picture, and a mod's frame */
    GLuint own;
    s32 own_w;
    s32 own_h;
    GLuint mod;
    s32 mod_w;
    s32 mod_h;
    u32 mod_number; /* the frame in `mod`, 0 none */
    /* colour, blocks and ringing at the console's size, one after
     * another between two; larger at the window's */
    MovieTarget small[2];
    MovieTarget large;
} g_movie;

static GLuint port_GlMovieShader(GLenum kind, const char *text) {
    GLuint shader = glCreateShader(kind);
    GLint ok = 0;

    glShaderSource(shader, 1, &text, NULL);
    glCompileShader(shader);
    glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[400];

        log[0] = 0;
        glGetShaderInfoLog(shader, sizeof(log), NULL, log);
        snprintf(g_movie.problem, sizeof(g_movie.problem),
                 "the movie's shader: %.200s", log);
        return 0;
    }
    return shader;
}

static s32 port_GlMovieMake(MoviePass *pass, const char *fragment_text) {
    GLuint vertex = port_GlMovieShader(GL_VERTEX_SHADER, g_movie_vertex_text);
    GLuint fragment = port_GlMovieShader(GL_FRAGMENT_SHADER, fragment_text);
    GLint ok = 0;

    if (vertex == 0 || fragment == 0) {
        return FALSE;
    }
    pass->program = glCreateProgram();
    glAttachShader(pass->program, vertex);
    glAttachShader(pass->program, fragment);
    glLinkProgram(pass->program);
    glGetProgramiv(pass->program, GL_LINK_STATUS, &ok);
    if (!ok) {
        snprintf(g_movie.problem, sizeof(g_movie.problem),
                 "the movie's shaders do not link");
        return FALSE;
    }
    pass->at_in = glGetUniformLocation(pass->program, "u_in");
    pass->at_size = glGetUniformLocation(pass->program, "u_size");
    pass->at_scale = glGetUniformLocation(pass->program, "u_scale");
    pass->at_out = glGetUniformLocation(pass->program, "u_out");
    pass->at_place = glGetUniformLocation(pass->program, "u_place");
    pass->at_sharp = glGetUniformLocation(pass->program, "u_sharp");
    return TRUE;
}

const char *port_GlMovieStart(void *(*getproc)(const char *name)) {
#define GL(result, name, args)                                                 \
    name = (result(*) args)getproc(#name);                                     \
    if (name == NULL) {                                                        \
        snprintf(                                                              \
            g_movie.problem, sizeof(g_movie.problem), "OpenGL has no " #name); \
        return g_movie.problem;                                                \
    }
    PORT_GL_MOVIE_FUNCTIONS
#undef GL
    if (!port_GlMovieMake(&g_movie.pass[PASS_COLOUR], g_movie_colour_text) ||
        !port_GlMovieMake(&g_movie.pass[PASS_BLOCKS], g_movie_blocks_text) ||
        !port_GlMovieMake(&g_movie.pass[PASS_RINGING], g_movie_ringing_text) ||
        !port_GlMovieMake(&g_movie.pass[PASS_LARGER], g_movie_larger_text) ||
        !port_GlMovieMake(&g_movie.pass[PASS_SHARPER], g_movie_sharper_text)) {
        return g_movie.problem;
    }
    glGenVertexArrays(1, &g_movie.array);
    glGenTextures(1, &g_movie.own);
    glGenTextures(1, &g_movie.mod);
    g_movie.is_ready = TRUE;
    return NULL;
}

/* A texture of this size and kind, read texel by texel. */
static void port_GlMovieTexture(
    GLuint texture, s32 w, s32 h, GLint inside, GLenum format, GLenum type) {
    glBindTexture(GL_TEXTURE_2D, texture);
    glTexImage2D(GL_TEXTURE_2D, 0, inside, w, h, 0, format, type, NULL);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
}

/* `target` at this size, made again where it is not. */
static s32 port_GlMovieTarget(MovieTarget *target, s32 w, s32 h) {
    if (target->w == w && target->h == h) {
        return TRUE;
    }
    if (target->texture == 0) {
        glGenTextures(1, &target->texture);
        glGenFramebuffers(1, &target->framebuffer);
    }
    port_GlMovieTexture(target->texture, w, h, GL_RGBA16F, GL_RGBA, GL_FLOAT);
    glBindFramebuffer(GL_FRAMEBUFFER, target->framebuffer);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D,
                           target->texture, 0);
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
        target->w = 0;
        target->h = 0;
        return FALSE;
    }
    target->w = w;
    target->h = h;
    return TRUE;
}

/* One pass from `texture` (`w` by `h`) into the target bound. */
static void port_GlMovieRun(
    const MoviePass *pass, GLuint texture, s32 w, s32 h, float scale) {
    glUseProgram(pass->program);
    glBindTexture(GL_TEXTURE_2D, texture);
    glUniform1i(pass->at_in, 0);
    glUniform2i(pass->at_size, w, h);
    glUniform1f(pass->at_scale, scale);
    glDrawArrays(GL_TRIANGLES, 0, 3);
}

/* The software GPU's picture made clean at its own size: the texture the
 * last pass wrote, or 0. */
static GLuint port_GlMovieClean(
    const u32 *pixels, s32 w, s32 h, u32 scale, GLint window) {
    float strength = (float)(scale != 0 ? scale : SCALE_UNKNOWN);
    s32 i;

    if (w != g_movie.own_w || h != g_movie.own_h) {
        port_GlMovieTexture(
            g_movie.own, w, h, GL_RGBA8, GL_BGRA, GL_UNSIGNED_BYTE);
        g_movie.own_w = w;
        g_movie.own_h = h;
    }
    glBindTexture(GL_TEXTURE_2D, g_movie.own);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    glTexSubImage2D(
        GL_TEXTURE_2D, 0, 0, 0, w, h, GL_BGRA, GL_UNSIGNED_BYTE, pixels);
    for (i = 0; i < 2; i++) {
        if (!port_GlMovieTarget(&g_movie.small[i], w, h)) {
            glBindFramebuffer(GL_FRAMEBUFFER, (GLuint)window);
            return 0;
        }
    }
    glViewport(0, 0, w, h);
    glBindFramebuffer(GL_FRAMEBUFFER, g_movie.small[0].framebuffer);
    port_GlMovieRun(&g_movie.pass[PASS_COLOUR], g_movie.own, w, h, strength);
    glBindFramebuffer(GL_FRAMEBUFFER, g_movie.small[1].framebuffer);
    port_GlMovieRun(
        &g_movie.pass[PASS_BLOCKS], g_movie.small[0].texture, w, h, strength);
    glBindFramebuffer(GL_FRAMEBUFFER, g_movie.small[0].framebuffer);
    port_GlMovieRun(
        &g_movie.pass[PASS_RINGING], g_movie.small[1].texture, w, h, strength);
    return g_movie.small[0].texture;
}

void port_GlMoviePrepare(u32 number) {
    const u8 *pixels;
    s32 w = 0;
    s32 h = 0;
    GLint unit = 0;
    GLint bound = 0;

    if (!g_movie.is_ready || number == 0 || !port_GlMoviePackOpen(MOVIE_KEY)) {
        return;
    }
    pixels = port_GlMoviePackFrame(number, &w, &h);
    if (pixels == NULL || number == g_movie.mod_number) {
        return;
    }
    glGetIntegerv(GL_ACTIVE_TEXTURE, &unit);
    glActiveTexture(GL_TEXTURE0);
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &bound);
    if (w != g_movie.mod_w || h != g_movie.mod_h) {
        port_GlMovieTexture(
            g_movie.mod, w, h, GL_RGBA8, GL_RGBA, GL_UNSIGNED_BYTE);
        g_movie.mod_w = w;
        g_movie.mod_h = h;
    }
    glBindTexture(GL_TEXTURE_2D, g_movie.mod);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    glTexSubImage2D(
        GL_TEXTURE_2D, 0, 0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
    g_movie.mod_number = number;
    glBindTexture(GL_TEXTURE_2D, (GLuint)bound);
    glActiveTexture((GLenum)unit);
}

s32 port_GlMovieDraw(
    const u32 *pixels, s32 pixels_w, s32 pixels_h, s32 window_w, s32 window_h,
    s32 x, s32 y, s32 w, s32 h, u32 number, u32 scale) {
    const MoviePass *larger = &g_movie.pass[PASS_LARGER];
    const MoviePass *sharper = &g_movie.pass[PASS_SHARPER];
    GLint window = 0;
    GLint program = 0;
    GLint array = 0;
    GLint unit = 0;
    GLint bound = 0;
    GLuint source;
    s32 source_w;
    s32 source_h;
    float sharp;
    s32 shown = FALSE;

    if (!g_movie.is_ready || pixels_w <= 0 || pixels_h <= 0 || w <= 0 ||
        h <= 0 || w > SIDE_MAX || h > SIDE_MAX) {
        return FALSE;
    }
    /* what the drawing path has bound, given back at the end */
    glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &window);
    glGetIntegerv(GL_CURRENT_PROGRAM, &program);
    glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &array);
    glGetIntegerv(GL_ACTIVE_TEXTURE, &unit);
    glActiveTexture(GL_TEXTURE0);
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &bound);
    glBindVertexArray(g_movie.array);

    if (number != 0 && number == g_movie.mod_number) {
        source = g_movie.mod;
        source_w = g_movie.mod_w;
        source_h = g_movie.mod_h;
        sharp = SHARPEN_MOD;
    } else {
        source = port_GlMovieClean(pixels, pixels_w, pixels_h, scale, window);
        source_w = pixels_w;
        source_h = pixels_h;
        sharp = SHARPEN_OWN;
    }
    if (source != 0 && port_GlMovieTarget(&g_movie.large, w, h)) {
        glBindFramebuffer(GL_FRAMEBUFFER, g_movie.large.framebuffer);
        glViewport(0, 0, w, h);
        glUseProgram(larger->program);
        glBindTexture(GL_TEXTURE_2D, source);
        glUniform1i(larger->at_in, 0);
        glUniform2i(larger->at_size, source_w, source_h);
        glUniform2i(larger->at_out, w, h);
        glDrawArrays(GL_TRIANGLES, 0, 3);

        glBindFramebuffer(GL_FRAMEBUFFER, (GLuint)window);
        glViewport(x, window_h - y - h, w, h);
        glUseProgram(sharper->program);
        glBindTexture(GL_TEXTURE_2D, g_movie.large.texture);
        glUniform1i(sharper->at_in, 0);
        glUniform2i(sharper->at_size, w, h);
        glUniform2i(sharper->at_place, x, window_h - y - h);
        glUniform1f(sharper->at_sharp, sharp);
        glDrawArrays(GL_TRIANGLES, 0, 3);
        shown = TRUE;
    }
    glBindFramebuffer(GL_FRAMEBUFFER, (GLuint)window);
    glViewport(0, 0, window_w, window_h);
    glBindTexture(GL_TEXTURE_2D, (GLuint)bound);
    glActiveTexture((GLenum)unit);
    glBindVertexArray((GLuint)array);
    glUseProgram((GLuint)program);
    return shown;
}
