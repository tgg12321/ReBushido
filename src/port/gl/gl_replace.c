/* Replacement textures in the remastered picture (gl_replace.h;
 * docs/MODS.md): the key of what a primitive reads, the mod's picture of
 * that key read and given to the card once, and the game's own texture
 * written out where a player asked for them. */
#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "common.h"
#include "gl_replace.h"
#include "port_gpu.h"
#include "port_host.h"
#include "port_mods.h"
#include "port_png.h"
#include "port_remaster.h"

/* ---- GL, as much of it as this file uses ---- */

typedef unsigned int GLenum;
typedef int GLint;
typedef unsigned int GLuint;
typedef int GLsizei;

#define GL_TEXTURE_2D 0x0DE1
#define GL_TEXTURE0 0x84C0
#define GL_TEXTURE_MIN_FILTER 0x2801
#define GL_TEXTURE_MAG_FILTER 0x2800
#define GL_TEXTURE_WRAP_S 0x2802
#define GL_TEXTURE_WRAP_T 0x2803
#define GL_LINEAR 0x2601
#define GL_LINEAR_MIPMAP_LINEAR 0x2703
#define GL_CLAMP_TO_EDGE 0x812F
#define GL_RGBA 0x1908
#define GL_RGBA8 0x8058
#define GL_UNSIGNED_BYTE 0x1401
#define GL_UNPACK_ALIGNMENT 0x0CF5

#define PORT_GL_REPLACE_FUNCTIONS                                              \
    GL(void, glGenTextures, (GLsizei, GLuint *))                               \
    GL(void, glBindTexture, (GLenum, GLuint))                                  \
    GL(void, glActiveTexture, (GLenum))                                        \
    GL(void, glTexParameteri, (GLenum, GLenum, GLint))                         \
    GL(void, glTexImage2D,                                                     \
       (GLenum, GLint, GLint, GLsizei, GLsizei, GLint, GLenum, GLenum,         \
        const void *))                                                         \
    GL(void, glGenerateMipmap, (GLenum))                                       \
    GL(void, glPixelStorei, (GLenum, GLint))

#define GL(result, name, args) static result(*name) args;
PORT_GL_REPLACE_FUNCTIONS
#undef GL

/* ---- what is kept ---- */

/* Every key seen, with what became of it: a power of two of them, which
 * is far more than the game has textures. */
#define SLOTS 0x10000
#define SLOT_MASK (SLOTS - 1)
/* the largest region looked up: a page's whole width and height */
#define REGION_SIDE_MAX 256
/* the smallest picture the game writes into video memory that is looked
 * up: a smaller one is a colour table or a piece of a font */
#define PICTURE_SIDE_MIN 16
#define RGBA 4
#define BYTE_MAX 255
#define FIVE_MAX 31
#define RED_SHIFT 0
#define GREEN_SHIFT 5
#define BLUE_SHIFT 10
#define TEXT_SIZE 1024
#define HALF_SHIFT 32
/* the texture window that leaves every texture point of a picture as it
 * is: all of video memory's */
#define WINDOW_ALL_X (PORT_VRAM_WIDTH - 1)
#define WINDOW_ALL_Y (PORT_VRAM_HEIGHT - 1)

enum {
    SLOT_EMPTY = 0, /* no key */
    SLOT_NONE,      /* the key has no replacement */
    SLOT_LOADED,    /* ... has one, on the card */
    SLOT_FAILED     /* ... has one that cannot be read */
};

typedef struct {
    u64 key;
    u32 texture;
    s32 state;
} ReplaceSlot;

static struct {
    s32 is_started;
    s32 has_slots;
    ReplaceSlot *slots;
    s32 used;
    s32 is_full;
    u32 loaded;
    u32 dumped;
} g_replace;

/* ---- the log ---- */

/* A line of the run's log (the window's session log takes it as well). */
static void port_ReplaceSay(const char *format, ...) {
    char line[TEXT_SIZE];
    va_list args;
    s32 length;

    va_start(args, format);
    length = vsnprintf(line, sizeof(line) - 1, format, args);
    va_end(args);
    if (length < 0) {
        return;
    }
    if (length > (s32)sizeof(line) - 2) {
        length = (s32)sizeof(line) - 2;
    }
    line[length++] = '\n';
    port_HostWrite(PORT_HOST_ERRORS, line, (u32)length);
}

/* ---- starting ---- */

const char *port_GlReplaceStart(void *(*getproc)(const char *name)) {
#define GL(result, name, args)                                                 \
    name = (result(*) args)getproc(#name);                                     \
    if (name == NULL) {                                                        \
        return "no " #name;                                                    \
    }
    PORT_GL_REPLACE_FUNCTIONS
#undef GL
    g_replace.is_started = TRUE;
    return NULL;
}

/* Whether anything is to be looked up: a mod has textures, or the game's
 * are to be written out. The mods are read after the picture starts, so
 * this is asked each time; the table is made the first time it says
 * yes. */
static s32 port_ReplaceIsOn(void) {
    if (!g_replace.is_started) {
        return FALSE;
    }
    if (port_ModsCount(PORT_MOD_TEXTURES) == 0 &&
        port_ModsGetDumpFolder() == NULL) {
        return FALSE;
    }
    if (!g_replace.has_slots) {
        g_replace.has_slots = TRUE;
        g_replace.slots = calloc(SLOTS, sizeof(*g_replace.slots));
        if (g_replace.slots == NULL) {
            port_ReplaceSay("mods: no memory for the textures' table");
        }
    }
    return g_replace.slots != NULL;
}

/* ---- writing a texture out ---- */

static u8 port_ReplaceByte(u16 pixel, s32 shift) {
    return (u8)(((pixel >> shift) & FIVE_MAX) * BYTE_MAX / FIVE_MAX);
}

/* The region as the GPU would draw it, as a PNG named by its key. A
 * texel of nothing is clear; a picture the game wrote into video memory
 * has no such texel, and is opaque all over. */
static void port_ReplaceDump(
    const PortTextureRegion *region, const char *name, s32 is_picture) {
    const char *folder = port_ModsGetDumpFolder();
    char path[TEXT_SIZE];
    u8 *rgba;
    u8 *png;
    u32 size;
    FILE *file;
    s32 u;
    s32 v;

    if (folder == NULL) {
        return;
    }
    snprintf(path, sizeof(path), "%s/%s.png", folder, name);
    file = fopen(path, "rb");
    if (file != NULL) {
        /* written by a run before this one */
        fclose(file);
        return;
    }
    rgba = malloc((size_t)region->w * (size_t)region->h * RGBA);
    png = malloc(port_PngEncodedSize(region->w, region->h));
    if (rgba == NULL || png == NULL) {
        free(rgba);
        free(png);
        return;
    }
    for (v = 0; v < region->h; v++) {
        for (u = 0; u < region->w; u++) {
            u16 texel = port_TextureTexel(g_port_vram, region, u, v);
            u8 *out = rgba + ((size_t)v * (size_t)region->w + (size_t)u) * RGBA;

            out[0] = port_ReplaceByte(texel, RED_SHIFT);
            out[1] = port_ReplaceByte(texel, GREEN_SHIFT);
            out[2] = port_ReplaceByte(texel, BLUE_SHIFT);
            out[3] = is_picture || texel != 0 ? BYTE_MAX : 0;
        }
    }
    size = port_PngEncode(rgba, region->w, region->h, png);
    file = fopen(path, "wb");
    if (file != NULL) {
        if (fwrite(png, 1, size, file) == size) {
            g_replace.dumped++;
            port_ReplaceSay("mods: wrote %s", path);
        }
        fclose(file);
    }
    free(rgba);
    free(png);
}

/* ---- reading a replacement ---- */

/* The mod's picture at `path` on the card: its texture, or 0. Its colours
 * are kept multiplied by its alpha, so that smoothing a clear texel into
 * the next does not darken it; the shader divides them out again. */
static u32 port_ReplaceLoad(const char *path, const char *name) {
    char problem[TEXT_SIZE / 4];
    PortPngInfo info;
    u32 size = port_ModFileSize(path);
    u8 *file = size > 0 ? malloc(size) : NULL;
    u8 *work = NULL;
    u8 *rgba = NULL;
    GLuint texture = 0;
    u32 i;

    problem[0] = 0;
    if (file == NULL || port_ModReadFile(path, file, size) != size) {
        snprintf(problem, sizeof(problem), "it cannot be read");
    } else if (port_PngMeasure(file, size, &info, problem, sizeof(problem))) {
        work = malloc(info.work_bytes);
        rgba = malloc((size_t)info.w * (size_t)info.h * RGBA);
        if (work == NULL || rgba == NULL) {
            snprintf(problem, sizeof(problem), "no memory for it");
        } else if (
            port_PngDecode(file, size, work, rgba, problem, sizeof(problem))) {
            for (i = 0; i < (u32)info.w * (u32)info.h * RGBA; i += RGBA) {
                rgba[i] = (u8)(rgba[i] * rgba[i + 3] / BYTE_MAX);
                rgba[i + 1] = (u8)(rgba[i + 1] * rgba[i + 3] / BYTE_MAX);
                rgba[i + 2] = (u8)(rgba[i + 2] * rgba[i + 3] / BYTE_MAX);
            }
            glActiveTexture(GL_TEXTURE0 + PORT_GL_REPLACE_UNIT);
            glGenTextures(1, &texture);
            glBindTexture(GL_TEXTURE_2D, texture);
            glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, info.w, info.h, 0, GL_RGBA,
                         GL_UNSIGNED_BYTE, rgba);
            glPixelStorei(GL_UNPACK_ALIGNMENT, RGBA);
            glGenerateMipmap(GL_TEXTURE_2D);
            glTexParameteri(
                GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
            glActiveTexture(GL_TEXTURE0);
            g_replace.loaded++;
            port_ReplaceSay(
                "mods: texture %s is %s (%dx%d)", name, path, info.w, info.h);
        }
    }
    if (texture == 0) {
        port_ReplaceSay(
            "mods: texture %s: %s is not used: %s", name, path, problem);
    }
    free(file);
    free(work);
    free(rgba);
    return texture;
}

/* ---- the keys ---- */

/* The slot of a key: its own, or the empty one it goes in; NULL when the
 * table is full and the key is not in it. */
static ReplaceSlot *port_ReplaceSlotOf(u64 key) {
    u32 at = (u32)(key ^ (key >> HALF_SHIFT)) & SLOT_MASK;
    s32 tries;

    for (tries = 0; tries < SLOTS; tries++) {
        ReplaceSlot *slot = &g_replace.slots[at];

        if (slot->state == SLOT_EMPTY || slot->key == key) {
            return slot;
        }
        at = (at + 1) & SLOT_MASK;
    }
    return NULL;
}

/* What became of the region's key, worked out the first time it is
 * seen: its texture, or 0. A key with no replacement is written out
 * then, where `may_dump` says. */
static u32 port_ReplaceOf(
    const PortTextureRegion *region, s32 is_picture, s32 may_dump) {
    u64 key = port_TextureKey(g_port_vram, region);
    ReplaceSlot *slot = port_ReplaceSlotOf(key);
    char name[PORT_MOD_KEY_SIZE];
    const char *path;

    if (slot == NULL) {
        return 0;
    }
    if (slot->state != SLOT_EMPTY) {
        return slot->state == SLOT_LOADED ? slot->texture : 0;
    }
    /* seen for the first time; a table nearly full keeps what it has */
    if (g_replace.used >= SLOTS / 2) {
        if (!g_replace.is_full) {
            g_replace.is_full = TRUE;
            port_ReplaceSay("mods: more than %d textures seen; no more are "
                            "looked up",
                            SLOTS / 2);
        }
        return 0;
    }
    g_replace.used++;
    slot->key = key;
    port_TextureKeyName(key, region, name, sizeof(name));
    path = port_ModFind(PORT_MOD_TEXTURES, name);
    if (path == NULL) {
        slot->state = SLOT_NONE;
        if (may_dump) {
            port_ReplaceDump(region, name, is_picture);
        }
        return 0;
    }
    slot->texture = port_ReplaceLoad(path, name);
    slot->state = slot->texture != 0 ? SLOT_LOADED : SLOT_FAILED;
    return slot->texture;
}

/* The texels a primitive reads: a sprite's rectangle, or the rectangle
 * of a polygon's texture points, its right and bottom edges not read. */
static s32 port_ReplaceRegionOf(
    const PortRemasterPrim *prim, PortTextureRegion *region) {
    s32 i;

    region->bits = prim->texel_bits;
    region->page_x = prim->page_x;
    region->page_y = prim->page_y;
    region->clut_x = prim->clut_x;
    region->clut_y = prim->clut_y;
    region->and_u = prim->window_and_u;
    region->or_u = prim->window_or_u;
    region->and_v = prim->window_and_v;
    region->or_v = prim->window_or_v;
    if (prim->kind == PORT_REMASTER_RECT) {
        region->u = prim->corner[0].u;
        region->v = prim->corner[0].v;
        region->w = prim->rect_w;
        region->h = prim->rect_h;
    } else {
        s32 right = prim->corner[0].u;
        s32 bottom = prim->corner[0].v;

        region->u = right;
        region->v = bottom;
        for (i = 1; i < prim->corner_count; i++) {
            const PortRemasterCorner *corner = &prim->corner[i];

            region->u = corner->u < region->u ? corner->u : region->u;
            region->v = corner->v < region->v ? corner->v : region->v;
            right = corner->u > right ? corner->u : right;
            bottom = corner->v > bottom ? corner->v : bottom;
        }
        region->w = right > region->u ? right - region->u : 1;
        region->h = bottom > region->v ? bottom - region->v : 1;
    }
    return region->w > 0 && region->h > 0 && region->w <= REGION_SIDE_MAX &&
           region->h <= REGION_SIDE_MAX;
}

s32 port_GlReplaceFind(const PortRemasterPrim *prim, PortGlReplacement *out) {
    PortTextureRegion region;

    /* The 2D layer. A face of the scene -- one the renderer named, or one
     * the projection placed -- is left to video memory: it reads a piece
     * of its page that is not a texture of its own (docs/MODS.md, "What
     * is not replaced"), and the scene drawn again under the pause has
     * lost both (port_RemasterIsRedrawing). */
    if (!prim->is_textured || prim->has_depth || prim->has_place ||
        prim->id.object != 0 || port_RemasterIsRedrawing() ||
        !port_ReplaceIsOn() || !port_ReplaceRegionOf(prim, &region)) {
        return FALSE;
    }
    out->texture = port_ReplaceOf(&region, FALSE, TRUE);
    if (out->texture == 0) {
        return FALSE;
    }
    out->u = region.u;
    out->v = region.v;
    out->w = region.w;
    out->h = region.h;
    return TRUE;
}

/* Whether the rectangle is all one colour: a fill, which is no picture.
 * Every pixel is looked at (a few would not do: a line of text on black
 * is black at its corners and its middle), and the first that differs
 * ends it, so a picture costs a few pixels and a fill no more than the
 * key it would otherwise have had worked out. */
static s32 port_ReplaceIsPlain(s32 x, s32 y, s32 w, s32 h) {
    const u16 *row = g_port_vram + y * PORT_VRAM_WIDTH + x;
    u16 first = row[0];
    s32 i;
    s32 j;

    for (j = 0; j < h; j++, row += PORT_VRAM_WIDTH) {
        for (i = 0; i < w; i++) {
            if (row[i] != first) {
                return FALSE;
            }
        }
    }
    return TRUE;
}

u32 port_GlReplaceFindPicture(
    s32 x, s32 y, s32 w, s32 h, s32 display_x, s32 display_w) {
    PortTextureRegion region;
    s32 is_shown;

    if (w < PICTURE_SIDE_MIN || h < PICTURE_SIDE_MIN ||
        x + w > PORT_VRAM_WIDTH || y + h > PORT_VRAM_HEIGHT ||
        (w == PORT_VRAM_WIDTH && h == PORT_VRAM_HEIGHT) ||
        !port_ReplaceIsOn() || port_ReplaceIsPlain(x, y, w, h)) {
        return 0;
    }
    /* a picture where the display is: one the game copied in to be
     * seen, and not a texture page it loaded to draw from */
    is_shown =
        display_w > 0 && x >= display_x && x + w <= display_x + display_w;
    region.bits = PORT_MOD_TEXELS_16;
    region.page_x = x;
    region.page_y = y;
    region.clut_x = 0;
    region.clut_y = 0;
    region.u = 0;
    region.v = 0;
    region.w = w;
    region.h = h;
    region.and_u = WINDOW_ALL_X;
    region.or_u = 0;
    region.and_v = WINDOW_ALL_Y;
    region.or_v = 0;
    /* a page of texels the game loaded to draw from is looked up, but
     * not written out: as one picture it is noise */
    if (!is_shown && port_ModsCount(PORT_MOD_TEXTURES) == 0) {
        return 0;
    }
    return port_ReplaceOf(&region, TRUE, is_shown);
}
