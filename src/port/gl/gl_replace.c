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
#include "gl_load.h"
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
/* a picture of a whole screen, which is one to be seen wherever it is
 * written: wider than a page of 16-bit texels and as tall as the screen */
#define SCREEN_PICTURE_W 320
#define SCREEN_PICTURE_H 240
#define RGBA 4
#define BYTE_MAX 255
#define FIVE_MAX 31
/* a texel's top bit: it mixes where the primitive is half-transparent */
#define MIX_BIT 0x8000
#define RED_SHIFT 0
#define GREEN_SHIFT 5
#define BLUE_SHIFT 10
#define TEXT_SIZE 1024
/* how many fades the log names */
#define FADES_SAID_MAX 64
#define HALF_SHIFT 32
/* the texture window that leaves every texture point of a picture as it
 * is: all of video memory's */
#define WINDOW_ALL_X (PORT_VRAM_WIDTH - 1)
#define WINDOW_ALL_Y (PORT_VRAM_HEIGHT - 1)

enum {
    SLOT_EMPTY = 0, /* no key */
    SLOT_NONE,      /* the key has no replacement */
    SLOT_LOADED,    /* ... has one, on the card */
    SLOT_FAILED,    /* ... has one that cannot be read */
    SLOT_LOADING    /* ... has one being read beside the game (gl_load.h) */
};

typedef struct {
    u64 key;
    u32 texture;
    s32 state;
    s32 mix;  /* PORT_GL_MIX_... */
    u32 tint; /* a fade's (port_TextureFadeOf); PORT_MOD_TINT_SAME */
    /* the fades' generation when a key with no replacement was last
     * looked for among them */
    u32 generation;
} ReplaceSlot;

/* The fades (docs/MODS.md, "Fades"). A texture the game fades through
 * its colour table is a new key at each step, and a picture it fades by
 * writing it again is too. So each texture that has a replacement is
 * kept here with what it was drawn from when it was found -- the colour
 * table of a texture, the pixels of a picture -- and a key with no
 * replacement of its own whose texels are a kept one's, with its colours
 * that one's times a number for each part, is drawn from that one's
 * picture times those numbers. */
#define FADES_MAX 1024
#define PICTURE_FADES_MAX 8
#define TABLE_COLORS_MAX 256
#define TABLE_COLORS_4 16

typedef struct {
    u64 texel_key;
    u64 key; /* the replacement's own: its texture is 0 while it is read */
    s32 bits;
    u32 texture;
    s32 mix;
    s32 colors;
    u16 table[TABLE_COLORS_MAX];
} FadeTexture;

/* a picture is held to one kept of its size, wherever either was
 * written: the game writes a fade's steps into whichever of its two
 * pictures is being drawn */
typedef struct {
    s32 w;
    s32 h;
    u64 key;
    u32 texture;
    u16 *pixels;
} FadePicture;

static struct {
    s32 is_started;
    s32 has_slots;
    ReplaceSlot *slots;
    s32 used;
    s32 is_full;
    u32 loaded;
    u32 dumped;
    FadeTexture *fades;
    s32 fade_count;
    FadePicture pictures[PICTURE_FADES_MAX];
    s32 picture_next;
    /* one more each time a texture or a picture is kept for the fades:
     * a key that had no replacement is then looked for again */
    u32 generation;
    u32 faded;
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

/* ---- the fades ---- */

static void port_ReplaceForgetFades(void) {
    s32 i;

    g_replace.fade_count = 0;
    for (i = 0; i < PICTURE_FADES_MAX; i++) {
        free(g_replace.pictures[i].pixels);
        g_replace.pictures[i].pixels = NULL;
        g_replace.pictures[i].texture = 0;
    }
    g_replace.picture_next = 0;
    g_replace.generation++;
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
    /* a context of its own: the textures given to the one before are not
     * this one's, so every key is looked up again (a dump already written
     * is not written twice: its file is there) */
    if (g_replace.slots != NULL) {
        memset(g_replace.slots, 0, SLOTS * sizeof(*g_replace.slots));
        g_replace.used = 0;
        g_replace.is_full = FALSE;
    }
    port_ReplaceForgetFades();
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
            u16 texel = port_TextureTexel(g_port_gl_vram, region, u, v);
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
            /* where it was: the pieces of one line of text sit side by
             * side in one page, which is how a mod finds them */
            port_ReplaceSay("mods: wrote %s (page %d %d, at %d %d, table %d "
                            "%d)",
                            path, region->page_x, region->page_y, region->u,
                            region->v, region->clut_x, region->clut_y);
        }
        fclose(file);
    }
    free(rgba);
    free(png);
}

/* ---- reading a replacement ---- */

static ReplaceSlot *port_ReplaceSlotOf(u64 key);
static void port_ReplaceKeptLoaded(u64 key, u32 texture);

/* A mod's picture read (gl_load.h) given to the card: the texture of the
 * slot of its key, where the slot is still waiting for it (a new context
 * forgets every key). Its colours are kept multiplied by its alpha, so
 * that smoothing a clear texel into the next does not darken it; the
 * shader divides them out again. */
static void port_ReplaceLoaded(PortGlLoad *load) {
    ReplaceSlot *slot =
        g_replace.slots != NULL ? port_ReplaceSlotOf(load->key) : NULL;
    const char *name = load->owner != NULL ? (const char *)load->owner : "?";
    GLuint texture = 0;

    if (slot != NULL && slot->state == SLOT_LOADING && slot->key == load->key) {
        if (load->is_loaded) {
            glActiveTexture(GL_TEXTURE0 + PORT_GL_REPLACE_UNIT);
            glGenTextures(1, &texture);
            glBindTexture(GL_TEXTURE_2D, texture);
            glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, load->w, load->h, 0,
                         GL_RGBA, GL_UNSIGNED_BYTE, load->pixels);
            glPixelStorei(GL_UNPACK_ALIGNMENT, RGBA);
            glGenerateMipmap(GL_TEXTURE_2D);
            glTexParameteri(
                GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
            glActiveTexture(GL_TEXTURE0);
            g_replace.loaded++;
            port_ReplaceSay("mods: texture %s is %s (%dx%d)", name, load->path,
                            load->w, load->h);
        } else {
            port_ReplaceSay("mods: texture %s: %s is not used: %s", name,
                            load->path, load->problem);
        }
        slot->texture = texture;
        slot->state = texture != 0 ? SLOT_LOADED : SLOT_FAILED;
        if (texture != 0) {
            port_ReplaceKeptLoaded(load->key, texture);
        }
    }
    free(load->owner);
    load->owner = NULL;
}

/* The mod's picture at `path` asked for, for the slot: read beside the
 * game, the slot drawing the game's own texture until it is on the card,
 * or at once where it cannot be (gl_load.h). */
static void port_ReplaceLoad(
    ReplaceSlot *slot, const char *path, const char *name) {
    PortGlLoad load;
    size_t length = strlen(name) + 1;

    memset(&load, 0, sizeof(load));
    load.path = path;
    load.done = port_ReplaceLoaded;
    load.key = slot->key;
    load.owner = malloc(length);
    slot->state = SLOT_LOADING;
    slot->texture = 0;
    if (load.owner == NULL) {
        slot->state = SLOT_FAILED;
        return;
    }
    memcpy(load.owner, name, length);
    if (!port_GlLoadAsk(&load)) {
        free(load.owner);
        slot->state = SLOT_FAILED;
    }
}

/* ---- fading ---- */

/* How many colours the region's table has: 0 for 16-bit texels. */
static s32 port_ReplaceColors(const PortTextureRegion *region) {
    return region->bits == PORT_MOD_TEXELS_4   ? TABLE_COLORS_4
           : region->bits == PORT_MOD_TEXELS_8 ? TABLE_COLORS_MAX
                                               : 0;
}

/* The region's colour table as video memory has it now. */
static void port_ReplaceTable(const PortTextureRegion *region, u16 *table) {
    s32 colors = port_ReplaceColors(region);
    s32 i;

    for (i = 0; i < colors; i++) {
        table[i] =
            g_port_gl_vram[(region->clut_y & WINDOW_ALL_Y) * PORT_VRAM_WIDTH +
                        ((region->clut_x + i) & WINDOW_ALL_X)];
    }
}

/* The picture's pixels as video memory has them now, into `out`, their
 * top bits left out: a picture is shown as it is, and the boot logo's
 * fade sets them in some steps and not in others. */
static void port_ReplacePixels(const PortTextureRegion *region, u16 *out) {
    s32 i;
    s32 j;

    for (j = 0; j < region->h; j++) {
        const u16 *row = g_port_gl_vram + (region->page_y + j) * PORT_VRAM_WIDTH +
                         region->page_x;

        for (i = 0; i < region->w; i++) {
            out[j * region->w + i] = row[i] & ~MIX_BIT;
        }
    }
}

/* A replacement found for the region's key, kept for the fades with what
 * it was drawn from. */
static void port_ReplaceKeepForFades(
    const PortTextureRegion *region, s32 is_picture, const ReplaceSlot *slot) {
    if (is_picture) {
        FadePicture *kept = &g_replace.pictures[g_replace.picture_next];
        u16 *pixels =
            malloc((size_t)region->w * (size_t)region->h * sizeof(*pixels));

        if (pixels == NULL) {
            return;
        }
        port_ReplacePixels(region, pixels);
        free(kept->pixels);
        kept->w = region->w;
        kept->h = region->h;
        kept->key = slot->key;
        kept->texture = slot->texture;
        kept->pixels = pixels;
        g_replace.picture_next =
            (g_replace.picture_next + 1) % PICTURE_FADES_MAX;
    } else {
        FadeTexture *kept;

        if (port_ReplaceColors(region) == 0) {
            return;
        }
        if (g_replace.fades == NULL) {
            g_replace.fades = malloc(FADES_MAX * sizeof(*g_replace.fades));
        }
        if (g_replace.fades == NULL || g_replace.fade_count >= FADES_MAX) {
            return;
        }
        kept = &g_replace.fades[g_replace.fade_count++];
        kept->texel_key = port_TextureTexelKey(g_port_gl_vram, region);
        kept->bits = region->bits;
        kept->key = slot->key;
        kept->texture = slot->texture;
        kept->mix = slot->mix;
        kept->colors = port_ReplaceColors(region);
        port_ReplaceTable(region, kept->table);
    }
    g_replace.generation++;
}

/* The texture of a replacement read beside the game, given to what was
 * kept of it for the fades while it was read; and every key with no
 * replacement of its own looked for among the fades again. */
static void port_ReplaceKeptLoaded(u64 key, u32 texture) {
    s32 i;

    for (i = 0; i < PICTURE_FADES_MAX; i++) {
        if (g_replace.pictures[i].pixels != NULL &&
            g_replace.pictures[i].texture == 0 &&
            g_replace.pictures[i].key == key) {
            g_replace.pictures[i].texture = texture;
        }
    }
    for (i = 0; i < g_replace.fade_count; i++) {
        if (g_replace.fades[i].texture == 0 && g_replace.fades[i].key == key) {
            g_replace.fades[i].texture = texture;
        }
    }
    g_replace.generation++;
}

/* A picture kept for the fades that the region, a picture with no
 * replacement of its own, is a fade of: its texture, or 0. */
static u32 port_ReplacePictureFade(const PortTextureRegion *region, u32 *tint) {
    u16 *now = NULL;
    u32 texture = 0;
    s32 i;

    for (i = 0; i < PICTURE_FADES_MAX && texture == 0; i++) {
        FadePicture *kept = &g_replace.pictures[i];

        if (kept->pixels == NULL || kept->texture == 0 ||
            kept->w != region->w || kept->h != region->h) {
            continue;
        }
        if (now == NULL) {
            now = malloc((size_t)region->w * (size_t)region->h * sizeof(*now));
            if (now == NULL) {
                return 0;
            }
            port_ReplacePixels(region, now);
        }
        if (port_TextureFadeOf(
                kept->pixels, now, (u32)(region->w * region->h), tint)) {
            texture = kept->texture;
        }
    }
    free(now);
    return texture;
}

/* The same for a texture seen through a colour table: the kept one of
 * the same texels whose table this one's is a fade of, or NULL. */
static const FadeTexture *port_ReplaceTextureFade(
    const PortTextureRegion *region, u32 *tint) {
    u16 table[TABLE_COLORS_MAX];
    u64 texel_key;
    s32 i;

    if (port_ReplaceColors(region) == 0 || g_replace.fade_count == 0) {
        return NULL;
    }
    texel_key = port_TextureTexelKey(g_port_gl_vram, region);
    port_ReplaceTable(region, table);
    for (i = 0; i < g_replace.fade_count; i++) {
        const FadeTexture *kept = &g_replace.fades[i];

        if (kept->texture != 0 && kept->texel_key == texel_key &&
            kept->bits == region->bits &&
            port_TextureFadeOf(kept->table, table, (u32)kept->colors, tint)) {
            return kept;
        }
    }
    return NULL;
}

/* Whether the region, which has no replacement of its own, is a fade of
 * one kept: then the slot is that one's picture with the fade's tint. */
static s32 port_ReplaceFadeOf(const PortTextureRegion *region, s32 is_picture,
                              ReplaceSlot *slot, const char *name) {
    u32 tint = PORT_MOD_TINT_SAME;

    slot->generation = g_replace.generation;
    if (is_picture) {
        u32 texture = port_ReplacePictureFade(region, &tint);

        if (texture == 0) {
            return FALSE;
        }
        slot->texture = texture;
        slot->mix = PORT_GL_MIX_TEXEL;
    } else {
        const FadeTexture *kept = port_ReplaceTextureFade(region, &tint);

        if (kept == NULL) {
            return FALSE;
        }
        slot->texture = kept->texture;
        slot->mix = kept->mix;
    }
    slot->state = SLOT_LOADED;
    slot->tint = tint;
    g_replace.faded++;
    if (g_replace.faded <= FADES_SAID_MAX) {
        port_ReplaceSay("mods: texture %s is a fade (%d, %d, %d of %d)", name,
                        (s32)(tint & BYTE_MAX),
                        (s32)((tint >> PORT_MOD_TINT_SHIFT) & BYTE_MAX),
                        (s32)((tint >> (2 * PORT_MOD_TINT_SHIFT)) & BYTE_MAX),
                        PORT_MOD_TINT_ONE);
    }
    return TRUE;
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

/* Whether the game's texels of the region mix with what is under them
 * where the primitive is half-transparent: all of the texels that are
 * drawn, none of them, or some and not others. The console decides it
 * texel by texel, by each one's top bit; a mod's picture draws where the
 * game's texture drew nothing as well (an English word over the space
 * between two Japanese characters), and there the console's texel has no
 * say. Where every texel the game drew says the same, the whole picture
 * says it (gl_replace.h). */
static s32 port_ReplaceMixOf(const PortTextureRegion *region) {
    s32 mixing = 0;
    s32 solid = 0;
    s32 u;
    s32 v;

    for (v = 0; v < region->h; v++) {
        for (u = 0; u < region->w; u++) {
            u16 texel = port_TextureTexel(g_port_gl_vram, region, u, v);

            if (texel == 0) {
                continue;
            }
            if (texel & MIX_BIT) {
                mixing++;
            } else {
                solid++;
            }
        }
    }
    if (mixing > 0 && solid == 0) {
        return PORT_GL_MIX_ALL;
    }
    if (solid > 0 && mixing == 0) {
        return PORT_GL_MIX_NONE;
    }
    return PORT_GL_MIX_TEXEL;
}

/* What became of the region's key, worked out the first time it is
 * seen: its texture, or 0, and in `mix` whether it mixes. A key with no
 * replacement is written out then, where `may_dump` says. */
static u32 port_ReplaceOf(const PortTextureRegion *region, s32 is_picture,
                          s32 may_dump, s32 *mix, u32 *tint) {
    u64 key = port_TextureKey(g_port_gl_vram, region);
    ReplaceSlot *slot = port_ReplaceSlotOf(key);
    char name[PORT_MOD_KEY_SIZE];
    const char *path;

    if (slot == NULL) {
        return 0;
    }
    if (slot->state == SLOT_NONE && slot->generation != g_replace.generation) {
        /* a replacement has been kept since: a fade of it, perhaps */
        port_TextureKeyName(key, region, name, sizeof(name));
        port_ReplaceFadeOf(region, is_picture, slot, name);
    }
    if (slot->state != SLOT_EMPTY) {
        *mix = slot->mix;
        *tint = slot->tint;
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
    slot->tint = PORT_MOD_TINT_SAME;
    if (path == NULL) {
        slot->state = SLOT_NONE;
        if (port_ReplaceFadeOf(region, is_picture, slot, name)) {
            *mix = slot->mix;
            *tint = slot->tint;
            return slot->texture;
        }
        if (may_dump) {
            port_ReplaceDump(region, name, is_picture);
        }
        return 0;
    }
    slot->mix = is_picture ? PORT_GL_MIX_TEXEL : port_ReplaceMixOf(region);
    port_ReplaceLoad(slot, path, name);
    if (slot->state != SLOT_FAILED) {
        port_ReplaceKeepForFades(region, is_picture, slot);
    }
    *mix = slot->mix;
    *tint = slot->tint;
    return slot->state == SLOT_LOADED ? slot->texture : 0;
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
    out->texture = port_ReplaceOf(&region, FALSE, TRUE, &out->mix, &out->tint);
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
    const u16 *top = g_port_gl_vram + y * PORT_VRAM_WIDTH + x;
    const u16 *row = top + PORT_VRAM_WIDTH;
    u16 first = top[0];
    s32 i;
    s32 j;

    /* the first line pixel by pixel, and every other line against it
     * whole: a frame's clear is a fill of 640 by 240, looked at every
     * frame and every picture between frames */
    for (i = 0; i < w; i++) {
        if (top[i] != first) {
            return FALSE;
        }
    }
    for (j = 1; j < h; j++, row += PORT_VRAM_WIDTH) {
        if (memcmp(row, top, (size_t)w * sizeof(u16)) != 0) {
            return FALSE;
        }
    }
    return TRUE;
}

u32 port_GlReplaceFindPicture(s32 x, s32 y, s32 w, s32 h, s32 display_x,
                              s32 display_w, s32 is_dark, u32 *tint) {
    PortTextureRegion region;
    s32 is_shown;
    s32 is_whole;
    s32 mix = PORT_GL_MIX_TEXEL;

    *tint = PORT_MOD_TINT_SAME;

    if (w < PICTURE_SIDE_MIN || h < PICTURE_SIDE_MIN ||
        x + w > PORT_VRAM_WIDTH || y + h > PORT_VRAM_HEIGHT ||
        (w == PORT_VRAM_WIDTH && h == PORT_VRAM_HEIGHT) ||
        !port_ReplaceIsOn() || port_ReplaceIsPlain(x, y, w, h)) {
        return 0;
    }
    /* a picture where the display is: one the game copied in to be
     * seen, and not a texture page it loaded to draw from. While nothing
     * is shown, a picture of a whole screen is one too: the movie player
     * loads its title picture beside the display with the display off.
     * While the display is the movie's, in the 24-bit mode, nothing else
     * is looked up: each of the movie's frames is new, and they would
     * fill the table of keys seen before the top menu is reached. */
    is_whole = w >= SCREEN_PICTURE_W && h >= SCREEN_PICTURE_H;
    if (display_w < 0 && !is_whole) {
        return 0;
    }
    is_shown =
        (display_w > 0 && x >= display_x && x + w <= display_x + display_w) ||
        (is_dark && is_whole);
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
    return port_ReplaceOf(&region, TRUE, is_shown, &mix, tint);
}
