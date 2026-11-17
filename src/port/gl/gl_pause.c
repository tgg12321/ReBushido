/* The fight's last frame under the pause menu in the remastered picture
 * (gl_pause.h). */
#include <stddef.h>

#include "common.h"
#include "gl.h"
#include "gl_pause.h"
#include "port_remaster.h"

/* ---- GL, as much of it as this file uses ---- */

typedef unsigned int GLenum;
typedef unsigned int GLbitfield;
typedef int GLint;
typedef unsigned int GLuint;
typedef int GLsizei;

#define GL_RGBA 0x1908
#define GL_RGBA8 0x8058
#define GL_UNSIGNED_BYTE 0x1401
#define GL_TEXTURE_2D 0x0DE1
#define GL_TEXTURE_MIN_FILTER 0x2801
#define GL_TEXTURE_MAG_FILTER 0x2800
#define GL_NEAREST 0x2600
#define GL_LINEAR 0x2601
#define GL_FRAMEBUFFER 0x8D40
#define GL_READ_FRAMEBUFFER 0x8CA8
#define GL_DRAW_FRAMEBUFFER 0x8CA9
#define GL_COLOR_ATTACHMENT0 0x8CE0
#define GL_FRAMEBUFFER_COMPLETE 0x8CD5
#define GL_COLOR_BUFFER_BIT 0x00004000
#define GL_SCISSOR_TEST 0x0C11

#define PORT_GL_PAUSE_FUNCTIONS                                                \
    GL(void, glEnable, (GLenum))                                               \
    GL(void, glDisable, (GLenum))                                              \
    GL(void, glGenTextures, (GLsizei, GLuint *))                               \
    GL(void, glBindTexture, (GLenum, GLuint))                                  \
    GL(void, glTexParameteri, (GLenum, GLenum, GLint))                         \
    GL(void, glTexImage2D,                                                     \
       (GLenum, GLint, GLint, GLsizei, GLsizei, GLint, GLenum, GLenum,         \
        const void *))                                                         \
    GL(void, glGenFramebuffers, (GLsizei, GLuint *))                           \
    GL(void, glBindFramebuffer, (GLenum, GLuint))                              \
    GL(void, glFramebufferTexture2D, (GLenum, GLenum, GLenum, GLuint, GLint))  \
    GL(GLenum, glCheckFramebufferStatus, (GLenum))                             \
    GL(void, glBlitFramebuffer,                                                \
       (GLint, GLint, GLint, GLint, GLint, GLint, GLint, GLint, GLbitfield,    \
        GLenum))

#define GL(result, name, args) static result(*name) args;
PORT_GL_PAUSE_FUNCTIONS
#undef GL

static struct {
    GLuint texture;
    GLuint framebuffer;
    /* the texture's size, in the colour buffer's pixels as they were
     * when it was made */
    s32 room_w;
    s32 room_h;
    /* the picture kept in it, from its corner: 0 by 0 while nothing is
     * kept */
    s32 kept_w;
    s32 kept_h;
} g_pause;

/* A frame's rectangle of the colour buffer as it is now, from its
 * corner: the console's own width and a wider picture's two bands. */
static void port_GlPauseSize(const PortGlPicture *picture, s32 *w, s32 *h) {
    *w = (picture->display_w + 2 * picture->margin) * picture->draw_x;
    *h = picture->display_h * picture->draw_y;
}

/* The frame at (`x`, `y`) of video memory into the texture. FALSE if
 * there is none. */
static s32 port_GlPauseKeep(const PortGlPicture *picture, s32 x, s32 y) {
    s32 w = 0;
    s32 h = 0;

    port_GlPauseSize(picture, &w, &h);
    if (w <= 0 || h <= 0) {
        return FALSE;
    }
    if (w > g_pause.room_w || h > g_pause.room_h) {
        glBindTexture(GL_TEXTURE_2D, g_pause.texture);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA,
                     GL_UNSIGNED_BYTE, NULL);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glBindFramebuffer(GL_FRAMEBUFFER, g_pause.framebuffer);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                               GL_TEXTURE_2D, g_pause.texture, 0);
        if (glCheckFramebufferStatus(GL_FRAMEBUFFER) !=
            GL_FRAMEBUFFER_COMPLETE) {
            glBindFramebuffer(GL_FRAMEBUFFER, picture->framebuffer);
            return FALSE;
        }
        g_pause.room_w = w;
        g_pause.room_h = h;
    }
    glBindFramebuffer(GL_READ_FRAMEBUFFER, picture->framebuffer);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, g_pause.framebuffer);
    glBlitFramebuffer(
        x * picture->draw_x, y * picture->draw_y, x * picture->draw_x + w,
        y * picture->draw_y + h, 0, 0, w, h, GL_COLOR_BUFFER_BIT, GL_NEAREST);
    g_pause.kept_w = w;
    g_pause.kept_h = h;
    return TRUE;
}

/* The decoder's holder (port_remaster.h): the fight's last frame kept
 * from (`from_x`, `from_y`) at the pause's first loop, and put at (`x`,
 * `y`) of video memory on each. */
static void port_GlPauseHold(s32 what, s32 from_x, s32 from_y, s32 x, s32 y) {
    PortGlPicture picture;
    s32 w = 0;
    s32 h = 0;
    s32 is_kept = TRUE;

    if (what == PORT_PAUSE_OFF) {
        g_pause.kept_w = 0;
        g_pause.kept_h = 0;
        return;
    }
    if (!port_GlIsStarted()) {
        return;
    }
    /* what the frame has drawn so far goes first: the picture goes over
     * the frame's own clear and under the panel */
    port_GlFlushBatch();
    port_GlGetPicture(&picture);
    glDisable(GL_SCISSOR_TEST);
    if (g_pause.kept_w == 0) {
        is_kept = port_GlPauseKeep(&picture, from_x, from_y);
    }
    port_GlPauseSize(&picture, &w, &h);
    if (is_kept && w > 0 && h > 0) {
        glBindFramebuffer(GL_READ_FRAMEBUFFER, g_pause.framebuffer);
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, picture.framebuffer);
        glBlitFramebuffer(
            0, 0, g_pause.kept_w, g_pause.kept_h, x * picture.draw_x,
            y * picture.draw_y, x * picture.draw_x + w, y * picture.draw_y + h,
            GL_COLOR_BUFFER_BIT,
            w == g_pause.kept_w && h == g_pause.kept_h ? GL_NEAREST
                                                       : GL_LINEAR);
    }
    glBindFramebuffer(GL_FRAMEBUFFER, picture.framebuffer);
    glEnable(GL_SCISSOR_TEST);
    port_GlForgetState();
}

const char *port_GlPauseStart(void *(*getproc)(const char *name)) {
#define GL(result, name, args)                                                 \
    name = (result(*) args)getproc(#name);                                     \
    if (name == NULL) {                                                        \
        return "OpenGL has no " #name;                                         \
    }
    PORT_GL_PAUSE_FUNCTIONS
#undef GL
    glGenTextures(1, &g_pause.texture);
    glGenFramebuffers(1, &g_pause.framebuffer);
    g_pause.room_w = 0;
    g_pause.room_h = 0;
    g_pause.kept_w = 0;
    g_pause.kept_h = 0;
    port_RemasterSetPauseHolder(port_GlPauseHold);
    return NULL;
}

void port_GlPauseStop(void) {
    port_RemasterSetPauseHolder(NULL);
    g_pause.kept_w = 0;
    g_pause.kept_h = 0;
}
