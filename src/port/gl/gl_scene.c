/* The stage's textures replaced by whole pages, for the remastered
 * picture (gl_scene.h; docs/MODS.md, "The scene's textures by page").
 *
 * When the game loads a stage, tex_LoadStageGroup says which TIMs it
 * loaded and where (port_remaster.h). That says which of video memory's
 * pixels are the stage's own, and which pages and colour tables its faces
 * can name: each TIM's page, with the table it brought. Each of those is
 * looked up as soon as the stage is drawn -- its key, then a mod's
 * picture of that key -- so that the pictures are read while the stage
 * loads and not in the middle of a fight. A page and a table the faces
 * name that no TIM brought is looked up the first time a face is drawn
 * with it, the same way.
 *
 * The pictures are the layers of one array on the card, all of one size,
 * with their mipmaps made here. Which layer a page and a table have is
 * one texel of the lookup table, at the page's number times 64 and the
 * table's column across and the table's row down, which is where both
 * shaders find it; and the colours the picture was made with, the table
 * as the stage loaded it, are a row of the table of bases, by layer.
 *
 * Two on two's partners of characters of their own have their textures
 * in the second bank of video memory (port_gpu.h), and their faces name
 * their pages and tables PORT_VRAM_BANK_NAMED_Y further down: their pages
 * are looked up the same way, keyed by the bank's pixels where the bank
 * has them (port_GlSceneVramOf), their tables' entries in rows of the
 * lookup table below the console's 512.
 *
 * Nothing here is in the build without a window, and nothing here changes
 * a value the game computes. */
#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "common.h"
#include "gl_load.h"
#include "gl_scene.h"
#include "port_gpu.h"
#include "port_host.h"
#include "port_mods.h"
#include "port_png.h"
#include "port_remaster.h"
#include "port_stage_choice.h"

/* ---- GL, as much of it as this file uses ---- */

typedef unsigned int GLenum;
typedef int GLint;
typedef unsigned int GLuint;
typedef int GLsizei;
typedef float GLfloat;

#define GL_NO_ERROR 0
#define GL_TEXTURE_2D 0x0DE1
#define GL_TEXTURE_2D_ARRAY 0x8C1A
#define GL_TEXTURE0 0x84C0
#define GL_TEXTURE_MIN_FILTER 0x2801
#define GL_TEXTURE_MAG_FILTER 0x2800
#define GL_TEXTURE_WRAP_S 0x2802
#define GL_TEXTURE_WRAP_T 0x2803
#define GL_TEXTURE_BASE_LEVEL 0x813C
#define GL_TEXTURE_MAX_LEVEL 0x813D
#define GL_TEXTURE_MAX_ANISOTROPY 0x84FE
#define GL_MAX_TEXTURE_MAX_ANISOTROPY 0x84FF
#define GL_NEAREST 0x2600
#define GL_LINEAR 0x2601
#define GL_LINEAR_MIPMAP_LINEAR 0x2703
#define GL_CLAMP_TO_EDGE 0x812F
#define GL_RGBA 0x1908
#define GL_RGBA8 0x8058
#define GL_R16UI 0x8234
#define GL_RED_INTEGER 0x8D94
#define GL_UNSIGNED_BYTE 0x1401
#define GL_UNSIGNED_SHORT 0x1403
#define GL_UNPACK_ALIGNMENT 0x0CF5
#define GL_UNPACK_ROW_LENGTH 0x0CF2

#define PORT_GL_SCENE_FUNCTIONS                                                \
    GL(void, glGenTextures, (GLsizei, GLuint *))                               \
    GL(void, glDeleteTextures, (GLsizei, const GLuint *))                      \
    GL(void, glBindTexture, (GLenum, GLuint))                                  \
    GL(void, glActiveTexture, (GLenum))                                        \
    GL(void, glTexParameteri, (GLenum, GLenum, GLint))                         \
    GL(void, glTexParameterf, (GLenum, GLenum, GLfloat))                       \
    GL(void, glTexImage2D,                                                     \
       (GLenum, GLint, GLint, GLsizei, GLsizei, GLint, GLenum, GLenum,         \
        const void *))                                                         \
    GL(void, glTexSubImage2D,                                                  \
       (GLenum, GLint, GLint, GLint, GLsizei, GLsizei, GLenum, GLenum,         \
        const void *))                                                         \
    GL(void, glTexImage3D,                                                     \
       (GLenum, GLint, GLint, GLsizei, GLsizei, GLsizei, GLint, GLenum,        \
        GLenum, const void *))                                                 \
    GL(void, glTexSubImage3D,                                                  \
       (GLenum, GLint, GLint, GLint, GLint, GLsizei, GLsizei, GLsizei, GLenum, \
        GLenum, const void *))                                                 \
    GL(void, glPixelStorei, (GLenum, GLint))                                   \
    GL(void, glGetFloatv, (GLenum, GLfloat *))                                 \
    GL(GLenum, glGetError, (void))

#define GL(result, name, args) static result(*name) args;
PORT_GL_SCENE_FUNCTIONS
#undef GL

/* ---- what is kept ---- */

/* The lookup table: a column for each of the 32 pages' 64 columns of
 * colour tables, and a row for each row of video memory, then one for each
 * row of the second bank (port_GlSceneRow). An entry is the
 * layer and one, in its low twelve bits; the depth of the texels it was
 * found for, in the next two; whether the table is a fighter's in the
 * next; and the top bit once it has been looked up, with a layer or
 * without. */
#define PAGES 32
#define PAGES_ACROSS 16
#define PAGE_X_STEP 64
#define PAGE_Y_STEP 256
#define TABLE_COLUMNS 64
#define TABLE_STEP 16
#define LOOKUP_W (PAGES * TABLE_COLUMNS)
#define LOOKUP_H (PORT_VRAM_HEIGHT + PORT_VRAM_BANK_H)
#define ENTRY_LAYER_MASK 0x0FFF
#define ENTRY_BITS_SHIFT 12
#define ENTRY_SEEN 0x8000
/* the table is a fighter's, not the stage's (no shader reads it) */
#define ENTRY_FIGHTER 0x4000

/* The most pictures a stage and its fighters may have. A stage's pages and
 * tables come to some twenty or thirty, a fighter's to as many again, and
 * two on two has four. */
#define LAYERS_MAX 192
/* ... and how many more the array has room for than the stage's TIMs
 * asked for, for a page found later */
#define LAYERS_SPARE 4
/* What has been looked up, so that a write over its page or its table can
 * have it looked up again. */
#define LOOKED_MAX 1024
/* the colours of a table, at the most */
#define COLORS_MAX 256
#define COLORS_4 16
#define RGBA 4
#define BYTE_MAX 255
#define FIVE_MAX 31
#define RED_SHIFT 0
#define GREEN_SHIFT 5
#define BLUE_SHIFT 10
#define TEXT_SIZE 1024
/* the pictures' mipmaps go down to a quarter of the game's own texel:
 * smaller, a level would smooth a texture into its neighbours in the
 * page, which the rectangle a face is held to cannot keep out */
#define LEVELS_BELOW_TEXEL 2
#define ANISOTROPY_MAX 16.0f
/* a PNG's signature and its header's chunk, and where the width is in it */
#define PNG_HEAD 24
#define PNG_WIDTH_AT 16
/* the largest picture of a page: eight times it, 64 MB with its levels */
#define PAGE_PICTURE_MAX 2048
/* the stage's textures not yet read into the tables */
#define VERSION_NONE 0xFFFFFFFFU
/* the folder of the dump the pages are written into */
#define DUMP_PAGES "pages"

/* what a layer holds */
enum { LAYER_READING = 0, LAYER_READY, LAYER_FAILED };

/* Whose textures a page's are: the stage's, or a fighter's model's, by its
 * slot (port_remaster.h). Each has its own pixels of video memory, and a
 * page's key is of its owner's texels alone. */
enum {
    OWNER_STAGE = 0,
    OWNER_FIGHTER_1,
    OWNER_FIGHTER_2,
    /* two on two's partners of their own, in the second bank: the lists
     * PORT_PARTNER_TEXTURES(0) and (1) */
    OWNER_PARTNER_1,
    OWNER_PARTNER_2,
    OWNERS
};
#define OWNER_IS_BANK(owner) ((owner) >= OWNER_PARTNER_1)

typedef struct {
    s16 page_x;
    s16 page_y;
    s16 clut_x; /* the table the faces name */
    s16 clut_y;
    s16 base_x; /* ... and where its colours are as the stage loaded it */
    s16 base_y;
    s16 bits;
    s16 owner;      /* OWNER_... whose table it is */
    s16 is_bank;    /* of the second bank: the coordinates above are of
                       the console's part it stands in for */
    s16 is_stale;   /* written over since: looked up again next frame */
    s16 is_waiting; /* its picture is being read (gl_load.h) */
    u64 key;
    const char *path; /* a mod's picture of it, or NULL */
} Looked;

static struct {
    s32 is_started;
    s32 is_on;
    s32 has_tables;
    GLuint lookup;
    GLuint pages;
    GLuint bases;
    u16 *lookup_table; /* what the card's lookup table holds */
    u8 *own[OWNERS];   /* which pixels of video memory are each owner's */
    u32 owned_pages;   /* a bit for each page that has any */
    u32 owned_bank_pages; /* ... and of the second bank's */
    /* video memory with the second bank where it has pixels, as a bank
     * face reads it (port_GlSceneVramOf), and whether it is this frame's */
    u16 *bank_view;
    s32 is_bank_view_fresh;
    u32 version;       /* the stage's textures the tables were made for */
    s32 side;          /* the pictures' size, each way */
    s32 levels;        /* ... and how many levels each has */
    s32 capacity;      /* layers the array has room for */
    s32 layer_count;
    u64 layer_keys[LAYERS_MAX];
    s8 layer_state[LAYERS_MAX]; /* LAYER_... */
    /* one more each time the layers are forgotten: a picture read for
     * one before is not put in */
    u32 generation;
    s32 is_full_said; /* the stage's pages filled the array, said once */
    /* a page with a picture found no room since the stage's pages were
     * read; and the fewest layers the array is made with when they are
     * read again (0 until then) */
    s32 is_short;
    s32 layers_least;
    u16 *base_rows;   /* the bases, LAYERS_MAX rows of COLORS_MAX */
    Looked *looked;
    s32 looked_count;
    s32 has_stale;
    GLfloat anisotropy;
    u32 loaded;
    u32 dumped;
} g_scene;

/* ---- the log ---- */

static void port_GlSceneSay(const char *format, ...) {
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

const char *port_GlSceneStart(void *(*getproc)(const char *name)) {
#define GL(result, name, args)                                                 \
    name = (result(*) args)getproc(#name);                                     \
    if (name == NULL) {                                                        \
        return "no " #name;                                                    \
    }
    PORT_GL_SCENE_FUNCTIONS
#undef GL
    /* a context of its own: what was on the card before is not this
     * one's, so the tables are made again when they are first wanted */
    g_scene.has_tables = FALSE;
    g_scene.lookup = 0;
    g_scene.pages = 0;
    g_scene.bases = 0;
    g_scene.capacity = 0;
    g_scene.layer_count = 0;
    g_scene.generation++;
    /* the stage's textures read again into the new tables */
    g_scene.version = VERSION_NONE;
    g_scene.is_started = TRUE;
    return NULL;
}

void port_GlSceneSetOn(s32 on) { g_scene.is_on = on != 0; }

/* Whether anything is to be looked up: a mod has textures, or the game's
 * are to be written out. */
static s32 port_GlSceneIsWanted(void) {
    return g_scene.is_started && g_scene.is_on &&
           (port_ModsCount(PORT_MOD_TEXTURES) > 0 ||
            port_ModsGetDumpFolder() != NULL);
}

/* The lookup table and the bases, made the first time they are wanted. */
static s32 port_GlSceneMakeTables(void) {
    s32 i;

    if (g_scene.has_tables) {
        return g_scene.lookup != 0;
    }
    g_scene.has_tables = TRUE;
    if (g_scene.lookup_table == NULL) {
        g_scene.lookup_table =
            calloc((size_t)LOOKUP_W * LOOKUP_H, sizeof(*g_scene.lookup_table));
        for (i = 0; i < OWNERS; i++) {
            g_scene.own[i] = calloc((size_t)PORT_VRAM_PIXELS, 1);
        }
        g_scene.bank_view =
            malloc((size_t)PORT_VRAM_PIXELS * sizeof(*g_scene.bank_view));
        g_scene.base_rows =
            calloc((size_t)LAYERS_MAX * COLORS_MAX, sizeof(*g_scene.base_rows));
        g_scene.looked = calloc(LOOKED_MAX, sizeof(*g_scene.looked));
    }
    for (i = 0; i < OWNERS; i++) {
        if (g_scene.own[i] == NULL) {
            break;
        }
    }
    if (g_scene.lookup_table == NULL || i < OWNERS ||
        g_scene.bank_view == NULL || g_scene.base_rows == NULL ||
        g_scene.looked == NULL) {
        port_GlSceneSay("mods: no memory for the stage's pages");
        return FALSE;
    }
    memset(g_scene.lookup_table, 0,
           (size_t)LOOKUP_W * LOOKUP_H * sizeof(*g_scene.lookup_table));
    glActiveTexture(GL_TEXTURE0 + PORT_GL_SCENE_LOOKUP_UNIT);
    glGenTextures(1, &g_scene.lookup);
    glBindTexture(GL_TEXTURE_2D, g_scene.lookup);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 2);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_R16UI, LOOKUP_W, LOOKUP_H, 0,
                 GL_RED_INTEGER, GL_UNSIGNED_SHORT, g_scene.lookup_table);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glGenTextures(1, &g_scene.bases);
    glBindTexture(GL_TEXTURE_2D, g_scene.bases);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_R16UI, COLORS_MAX, LAYERS_MAX, 0,
                 GL_RED_INTEGER, GL_UNSIGNED_SHORT, g_scene.base_rows);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glPixelStorei(GL_UNPACK_ALIGNMENT, RGBA);
    glActiveTexture(GL_TEXTURE0);
    /* how far the card filters a texture seen at a slant, where it can */
    g_scene.anisotropy = 0.0f;
    while (glGetError() != GL_NO_ERROR) {
    }
    glGetFloatv(GL_MAX_TEXTURE_MAX_ANISOTROPY, &g_scene.anisotropy);
    if (glGetError() != GL_NO_ERROR) {
        g_scene.anisotropy = 0.0f;
    }
    if (g_scene.anisotropy > ANISOTROPY_MAX) {
        g_scene.anisotropy = ANISOTROPY_MAX;
    }
    return TRUE;
}

/* ---- the lookup table ---- */

static s32 port_GlSceneColumn(s32 page_x, s32 page_y, s32 clut_x) {
    s32 page = ((page_x / PAGE_X_STEP) % PAGES_ACROSS) +
               ((page_y / PAGE_Y_STEP) & 1) * PAGES_ACROSS;

    return page * TABLE_COLUMNS + ((clut_x / TABLE_STEP) % TABLE_COLUMNS);
}

/* The row of the lookup table of a table the faces name at row `clut_y`:
 * its own within the console's lines, and below them, from row 512, one
 * of the second bank's (named PORT_VRAM_BANK_NAMED_Y further down); -1
 * for none. The shader works it out as this does (gl_scene.h). */
static s32 port_GlSceneRow(s32 clut_y) {
    if (clut_y >= PORT_VRAM_BANK_NAMED_Y) {
        clut_y -= PORT_VRAM_BANK_NAMED_Y;
        return clut_y < PORT_VRAM_BANK_H ? PORT_VRAM_HEIGHT + clut_y : -1;
    }
    return clut_y >= 0 && clut_y < PORT_VRAM_HEIGHT ? clut_y : -1;
}

/* Video memory as a face of the bank, or of the console's, reads it: the
 * console's with the second bank's pixels where it has them, made once a
 * frame it is wanted (the bank's own pixels are the game's memory,
 * port_VramBank, which the card's copy follows each frame). */
static const u16 *port_GlSceneVramOf(s32 is_bank) {
    const u16 *bank = port_VramBank();
    s32 row;

    if (!is_bank || bank == NULL) {
        return g_port_gl_vram;
    }
    if (!g_scene.is_bank_view_fresh) {
        memcpy(g_scene.bank_view, g_port_gl_vram,
               (size_t)PORT_VRAM_PIXELS * sizeof(*g_scene.bank_view));
        for (row = 0; row < PORT_VRAM_BANK_H; row++) {
            memcpy(&g_scene.bank_view[row * PORT_VRAM_WIDTH + PORT_VRAM_BANK_X],
                   &bank[row * PORT_VRAM_BANK_W],
                   PORT_VRAM_BANK_W * sizeof(*bank));
        }
        g_scene.is_bank_view_fresh = TRUE;
    }
    return g_scene.bank_view;
}

static void port_GlSceneSetEntry(s32 column, s32 row, u16 entry) {
    u16 *at = &g_scene.lookup_table[row * LOOKUP_W + column];

    if (*at == entry) {
        return;
    }
    *at = entry;
    glActiveTexture(GL_TEXTURE0 + PORT_GL_SCENE_LOOKUP_UNIT);
    glBindTexture(GL_TEXTURE_2D, g_scene.lookup);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 2);
    glTexSubImage2D(GL_TEXTURE_2D, 0, column, row, 1, 1, GL_RED_INTEGER,
                    GL_UNSIGNED_SHORT, at);
    glPixelStorei(GL_UNPACK_ALIGNMENT, RGBA);
    glActiveTexture(GL_TEXTURE0);
}

/* ---- the pictures ---- */

/* The array of pictures, with room for `layers` of `side` texels each
 * way: made again, empty, where it has not the room. */
static s32 port_GlSceneMakeArray(s32 side, s32 layers) {
    s32 level;
    s32 size;

    /* never more than the tables have rows for (the stage's pages read
     * again asked for as many as it found) */
    if (layers > LAYERS_MAX) {
        layers = LAYERS_MAX;
    }
    if (g_scene.pages != 0 && side == g_scene.side &&
        layers <= g_scene.capacity) {
        return TRUE;
    }
    if (g_scene.pages != 0) {
        glDeleteTextures(1, &g_scene.pages);
        g_scene.pages = 0;
    }
    g_scene.generation++;
    g_scene.side = side;
    g_scene.capacity = layers;
    /* down to a quarter of the game's texel: a picture of four times the
     * page is 1024, 512, 256, 128 and 64 */
    g_scene.levels = 1;
    for (size = side;
         size > PORT_MOD_PAGE_SIDE >> LEVELS_BELOW_TEXEL && size > 1;
         size >>= 1) {
        g_scene.levels++;
    }
    glActiveTexture(GL_TEXTURE0 + PORT_GL_SCENE_PAGES_UNIT);
    glGenTextures(1, &g_scene.pages);
    glBindTexture(GL_TEXTURE_2D_ARRAY, g_scene.pages);
    for (level = 0, size = side; level < g_scene.levels; level++, size >>= 1) {
        glTexImage3D(GL_TEXTURE_2D_ARRAY, level, GL_RGBA8, size, size, layers,
                     0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    }
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_BASE_LEVEL, 0);
    glTexParameteri(
        GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAX_LEVEL, g_scene.levels - 1);
    glTexParameteri(
        GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    if (g_scene.anisotropy > 1.0f) {
        glTexParameterf(
            GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAX_ANISOTROPY, g_scene.anisotropy);
    }
    glActiveTexture(GL_TEXTURE0);
    return glGetError() == GL_NO_ERROR;
}

/* A picture of `w` by `h` made `side` each way, by the mean of what each
 * new pixel covers, read between the old ones. */
static void port_GlSceneResize(const u8 *from, s32 w, s32 h, u8 *to, s32 side) {
    s32 x;
    s32 y;
    s32 c;

    for (y = 0; y < side; y++) {
        float fy = ((float)y + 0.5f) * (float)h / (float)side - 0.5f;
        s32 y0 = fy < 0.0f ? 0 : (s32)fy;
        s32 y1 = y0 + 1 < h ? y0 + 1 : h - 1;
        float ty = fy < 0.0f ? 0.0f : fy - (float)y0;

        for (x = 0; x < side; x++) {
            float fx = ((float)x + 0.5f) * (float)w / (float)side - 0.5f;
            s32 x0 = fx < 0.0f ? 0 : (s32)fx;
            s32 x1 = x0 + 1 < w ? x0 + 1 : w - 1;
            float tx = fx < 0.0f ? 0.0f : fx - (float)x0;

            for (c = 0; c < RGBA; c++) {
                float top =
                    (float)from[(y0 * w + x0) * RGBA + c] * (1.0f - tx) +
                    (float)from[(y0 * w + x1) * RGBA + c] * tx;
                float bottom =
                    (float)from[(y1 * w + x0) * RGBA + c] * (1.0f - tx) +
                    (float)from[(y1 * w + x1) * RGBA + c] * tx;

                to[(y * side + x) * RGBA + c] =
                    (u8)(top * (1.0f - ty) + bottom * ty + 0.5f);
            }
        }
    }
}

/* The level below: each pixel the mean of the four it stands for. The
 * colours are already multiplied by their alpha, so a clear pixel takes
 * nothing of its colour into its neighbours. */
static void port_GlSceneHalve(const u8 *from, s32 side, u8 *to) {
    s32 half = side / 2;
    s32 x;
    s32 y;
    s32 c;

    for (y = 0; y < half; y++) {
        for (x = 0; x < half; x++) {
            for (c = 0; c < RGBA; c++) {
                const u8 *at = from + ((2 * y) * side + 2 * x) * RGBA + c;
                u32 sum = (u32)at[0] + at[RGBA] + at[side * RGBA] +
                          at[side * RGBA + RGBA];

                to[(y * half + x) * RGBA + c] = (u8)((sum + 2) / 4);
            }
        }
    }
}

/* On the thread that reads it (gl_load.h): the picture made the array's
 * size, and every level of it below, one after another. */
static s32 port_GlScenePrepare(PortGlLoad *load) {
    s32 side = load->side;
    size_t bytes = 0;
    u8 *levels;
    u8 *now;
    s32 level;
    s32 size;

    for (level = 0, size = side; level < load->levels; level++, size >>= 1) {
        bytes += (size_t)size * (size_t)size * RGBA;
    }
    levels = malloc(bytes);
    if (levels == NULL) {
        snprintf(load->problem, sizeof(load->problem), "no memory for it");
        return FALSE;
    }
    if (load->w == side && load->h == side) {
        memcpy(levels, load->pixels, (size_t)side * side * RGBA);
    } else {
        port_GlSceneResize(load->pixels, load->w, load->h, levels, side);
    }
    for (level = 0, now = levels, size = side; level + 1 < load->levels;
         level++, size >>= 1) {
        u8 *next = now + (size_t)size * size * RGBA;

        port_GlSceneHalve(now, size, next);
        now = next;
    }
    free(load->pixels);
    load->pixels = levels;
    return TRUE;
}

static void port_GlSceneEnter(Looked *one);

/* A page's picture read, put into its layer, every level of it; and each
 * page and table that waited for it given it. Nothing where the layers
 * have been forgotten since it was asked for. */
static void port_GlSceneLoaded(PortGlLoad *load) {
    const char *name = load->owner != NULL ? (const char *)load->owner : "?";
    s32 layer = load->tag;
    s32 level;
    s32 side;
    s32 i;

    if (load->generation != g_scene.generation || g_scene.pages == 0 ||
        layer >= g_scene.layer_count ||
        g_scene.layer_keys[layer] != load->key ||
        g_scene.layer_state[layer] != LAYER_READING) {
        free(load->owner);
        load->owner = NULL;
        return;
    }
    if (load->is_loaded) {
        const u8 *now = load->pixels;

        glActiveTexture(GL_TEXTURE0 + PORT_GL_SCENE_PAGES_UNIT);
        glBindTexture(GL_TEXTURE_2D_ARRAY, g_scene.pages);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        for (level = 0, side = g_scene.side; level < g_scene.levels; level++,
            side >>= 1) {
            glTexSubImage3D(GL_TEXTURE_2D_ARRAY, level, 0, 0, layer, side, side,
                            1, GL_RGBA, GL_UNSIGNED_BYTE, now);
            now += (size_t)side * side * RGBA;
        }
        glPixelStorei(GL_UNPACK_ALIGNMENT, RGBA);
        glActiveTexture(GL_TEXTURE0);
        g_scene.loaded++;
        g_scene.layer_state[layer] = LAYER_READY;
        port_GlSceneSay("mods: texture %s is %s (%dx%d)", name, load->path,
                        load->w, load->h);
    } else {
        g_scene.layer_state[layer] = LAYER_FAILED;
        port_GlSceneSay("mods: texture %s: %s is not used: %s", name,
                        load->path, load->problem);
    }
    free(load->owner);
    load->owner = NULL;
    for (i = 0; i < g_scene.looked_count; i++) {
        Looked *one = &g_scene.looked[i];

        if (one->is_waiting && one->key == load->key) {
            port_GlSceneEnter(one);
        }
    }
}

/* The mod's picture at `path` asked for, for layer `layer`: read beside
 * the game, or at once where it cannot be (gl_load.h). */
static void port_GlSceneLoad(const char *path, const char *name, s32 layer) {
    PortGlLoad load;
    size_t length = strlen(name) + 1;

    memset(&load, 0, sizeof(load));
    load.path = path;
    load.prepare = port_GlScenePrepare;
    load.done = port_GlSceneLoaded;
    load.key = g_scene.layer_keys[layer];
    load.tag = layer;
    load.side = g_scene.side;
    load.levels = g_scene.levels;
    load.generation = g_scene.generation;
    load.owner = malloc(length);
    if (load.owner == NULL) {
        g_scene.layer_state[layer] = LAYER_FAILED;
        return;
    }
    memcpy(load.owner, name, length);
    if (!port_GlLoadAsk(&load)) {
        free(load.owner);
        g_scene.layer_state[layer] = LAYER_FAILED;
    }
}

/* How large the array's pictures are: the first picture's own width, made
 * a power of two from the page's 256 so that every level halves, which
 * every other picture is made to. 0 where it is no PNG. */
static s32 port_GlSceneSideOf(const char *path) {
    u8 head[PNG_HEAD];
    u32 got = port_ModReadFile(path, head, sizeof(head));
    u32 w;
    s32 side;

    if (got < PNG_HEAD) {
        return 0;
    }
    /* the header's first chunk is its size: the width four bytes, the
     * high first */
    w = ((u32)head[PNG_WIDTH_AT] << 24) | ((u32)head[PNG_WIDTH_AT + 1] << 16) |
        ((u32)head[PNG_WIDTH_AT + 2] << 8) | (u32)head[PNG_WIDTH_AT + 3];
    for (side = PORT_MOD_PAGE_SIDE; (u32)side < w && side < PAGE_PICTURE_MAX;
         side <<= 1) {
    }
    return side;
}

/* ---- writing a page out ---- */

/* An owner's textures as the game loaded them, and how many. */
static const PortStageTexture *port_GlSceneTextures(s32 owner, s32 *count) {
    u32 version;

    if (owner == OWNER_STAGE) {
        return port_RemasterStageTextures(count, &version);
    }
    /* the partners' lists follow the slots' (PORT_PARTNER_TEXTURES) */
    return port_RemasterFighterTextures(owner - OWNER_FIGHTER_1, count);
}

static u8 port_GlSceneByte(u16 pixel, s32 shift) {
    return (u8)(((pixel >> shift) & FIVE_MAX) * BYTE_MAX / FIVE_MAX);
}

/* The page as the stage's colours draw it, as a PNG named by its key in
 * the dump's folder of pages, and beside it which of its rectangles are
 * its owner's own textures, the stage's or a fighter's
 * (tools/port_stage_hd.py reads both). A texel that is not its owner's
 * own, and one of nothing, is clear. */
static void port_GlSceneDump(
    const PortTexturePage *page, s32 owner, const char *name) {
    const char *folder = port_ModsGetDumpFolder();
    const u16 *vram = port_GlSceneVramOf(OWNER_IS_BANK(owner));
    const PortStageTexture *textures;
    char path[TEXT_SIZE];
    s32 count;
    u8 *rgba;
    u8 *png;
    u32 size;
    FILE *file;
    s32 u;
    s32 v;
    s32 i;

    if (folder == NULL) {
        return;
    }
    snprintf(path, sizeof(path), "%s/%s", folder, DUMP_PAGES);
    port_HostMakeFolder(path);
    snprintf(path, sizeof(path), "%s/%s/%s.png", folder, DUMP_PAGES, name);
    file = fopen(path, "rb");
    if (file != NULL) {
        fclose(file);
        return;
    }
    rgba = malloc((size_t)PORT_MOD_PAGE_SIDE * PORT_MOD_PAGE_SIDE * RGBA);
    png = malloc(port_PngEncodedSize(PORT_MOD_PAGE_SIDE, PORT_MOD_PAGE_SIDE));
    if (rgba == NULL || png == NULL) {
        free(rgba);
        free(png);
        return;
    }
    for (v = 0; v < PORT_MOD_PAGE_SIDE; v++) {
        for (u = 0; u < PORT_MOD_PAGE_SIDE; u++) {
            PortTextureRegion region;
            u8 *out =
                rgba + ((size_t)v * PORT_MOD_PAGE_SIDE + (size_t)u) * RGBA;
            s32 x = page->page_x + port_TexturePagePixel(page, u);
            u16 texel = 0;

            if (page->own[(page->page_y + v) * PORT_VRAM_WIDTH + x] != 0) {
                region.bits = page->bits;
                region.page_x = page->page_x;
                region.page_y = page->page_y;
                region.clut_x = page->clut_x;
                region.clut_y = page->clut_y;
                region.u = 0;
                region.v = 0;
                region.w = PORT_MOD_PAGE_SIDE;
                region.h = PORT_MOD_PAGE_SIDE;
                region.and_u = PORT_MOD_PAGE_SIDE - 1;
                region.or_u = 0;
                region.and_v = PORT_MOD_PAGE_SIDE - 1;
                region.or_v = 0;
                texel = port_TextureTexel(vram, &region, u, v);
            }
            out[0] = port_GlSceneByte(texel, RED_SHIFT);
            out[1] = port_GlSceneByte(texel, GREEN_SHIFT);
            out[2] = port_GlSceneByte(texel, BLUE_SHIFT);
            out[3] = texel != 0 ? BYTE_MAX : 0;
        }
    }
    size = port_PngEncode(rgba, PORT_MOD_PAGE_SIDE, PORT_MOD_PAGE_SIDE, png);
    file = fopen(path, "wb");
    if (file != NULL) {
        if (fwrite(png, 1, size, file) == size) {
            g_scene.dumped++;
            port_GlSceneSay(
                "mods: wrote %s (page %d %d, table %d %d)", path, page->page_x,
                page->page_y, page->clut_x, page->clut_y);
        }
        fclose(file);
    }
    free(rgba);
    free(png);
    /* its owner's own textures in it, in texels from its corner */
    snprintf(path, sizeof(path), "%s/%s/%s.txt", folder, DUMP_PAGES, name);
    file = fopen(path, "w");
    if (file == NULL) {
        return;
    }
    fprintf(file, "page %d %d bits %d table %d %d\n", page->page_x,
            page->page_y, page->bits, page->clut_x, page->clut_y);
    /* a partner of two on two is fighter 3 (the first slot's) or 4, his
     * pixels the second bank's */
    if (owner == OWNER_STAGE) {
        fprintf(file, "owner stage\n");
    } else {
        fprintf(file, "owner fighter %d%s\n", owner - OWNER_FIGHTER_1 + 1,
                OWNER_IS_BANK(owner) ? " bank" : "");
    }
    textures = port_GlSceneTextures(owner, &count);
    for (i = 0; i < count; i++) {
        const PortStageTexture *one = &textures[i];
        s32 per = port_TexturePagePixel(page, PORT_MOD_PAGE_SIDE);
        s32 left = one->x - page->page_x;
        s32 top = one->y - page->page_y;

        if (left >= per || top >= PORT_MOD_PAGE_SIDE || left + one->w <= 0 ||
            top + one->h <= 0) {
            continue;
        }
        /* in texels: a pixel of 16 bits is four of 4 bits, two of 8 */
        fprintf(file, "texture %d %d %d %d table %d %d\n",
                left * PORT_MOD_PAGE_SIDE / per, top,
                one->w * PORT_MOD_PAGE_SIDE / per, one->h, one->clut_x,
                one->clut_y);
    }
    fclose(file);
}

/* ---- looking a page up ---- */

/* Whose table the faces name at (x, y) is, and where its colours are as
 * its owner loaded them: the stage's tables left of the display's width
 * are loaded a row below where the faces name them, the row the fog
 * writes over, and a fogged fighter's are the plain copy below the fog's
 * block (port_remaster.h). FALSE for a table of nobody's. */
static s32 port_GlSceneBaseOf(
    s32 x, s32 y, s32 is_bank, s32 *base_x, s32 *base_y, s32 *owner) {
    const PortStageTexture *textures;
    s32 count;
    s32 who;
    s32 i;

    for (who = 0; who < OWNERS; who++) {
        if (OWNER_IS_BANK(who) != (is_bank != 0)) {
            continue;
        }
        textures = port_GlSceneTextures(who, &count);
        for (i = 0; i < count; i++) {
            const PortStageTexture *one = &textures[i];

            if (one->clut_x < 0 || x < one->clut_x ||
                x >= one->clut_x + one->clut_w || y < one->named_y ||
                y >= one->named_y + one->clut_h) {
                continue;
            }
            *base_x = x;
            *base_y = y + (one->clut_y - one->named_y);
            *owner = who;
            return TRUE;
        }
    }
    return FALSE;
}

/* The key of a page and its table, its name, and a mod's picture of it,
 * or the page written out where there is none and that was asked for. */
static void port_GlSceneKeyOf(Looked *one, char *name) {
    PortTexturePage page;

    page.bits = one->bits;
    page.page_x = one->page_x;
    page.page_y = one->page_y;
    page.clut_x = one->base_x;
    page.clut_y = one->base_y;
    page.own = g_scene.own[one->owner];
    one->key = port_TexturePageKey(port_GlSceneVramOf(one->is_bank), &page);
    port_TexturePageKeyName(one->key, &page, name, PORT_MOD_KEY_SIZE);
    one->path = port_ModFind(PORT_MOD_TEXTURES, name);
    if (one->path == NULL) {
        port_GlSceneDump(&page, one->owner, name);
    }
}

/* The layer of the picture of this key: one already loaded, or a new
 * one; -1 where it cannot be read or there is no room. */
static s32 port_GlSceneLayerOf(u64 key, const char *path, const char *name) {
    s32 i;

    for (i = 0; i < g_scene.layer_count; i++) {
        if (g_scene.layer_keys[i] == key) {
            return g_scene.layer_state[i] == LAYER_FAILED ? -1 : i;
        }
    }
    if (g_scene.layer_count >= g_scene.capacity) {
        g_scene.is_short = TRUE;
        /* said once a stage: a page the game writes again and again (a
         * stage's water) is a new key each time */
        if (!g_scene.is_full_said) {
            g_scene.is_full_said = TRUE;
            port_GlSceneSay("mods: texture %s: no room for more than %d "
                            "pages of one stage (said once a stage)",
                            name, g_scene.capacity);
        }
        return -1;
    }
    i = g_scene.layer_count++;
    g_scene.layer_keys[i] = key;
    g_scene.layer_state[i] = LAYER_READING;
    port_GlSceneLoad(path, name, i);
    if (g_scene.layer_state[i] == LAYER_FAILED &&
        i == g_scene.layer_count - 1) {
        /* read at once and of no use: the layer is the next one's */
        g_scene.layer_count--;
        return -1;
    }
    return g_scene.layer_state[i] == LAYER_FAILED ? -1 : i;
}

/* The entry of the lookup table for a page and a table whose key has been
 * worked out: its picture's layer, the picture loaded the first time, and
 * the colours it was made with as its row of the bases. */
static void port_GlSceneEnter(Looked *one) {
    PortTexturePage page;
    char name[PORT_MOD_KEY_SIZE];
    s32 column = port_GlSceneColumn(one->page_x, one->page_y, one->clut_x);
    s32 colors = one->bits == PORT_MOD_TEXELS_4 ? COLORS_4 : COLORS_MAX;
    const u16 *vram = port_GlSceneVramOf(one->is_bank);
    s32 layer = -1;
    s32 i;

    page.bits = one->bits;
    port_TexturePageKeyName(one->key, &page, name, sizeof(name));
    if (one->path != NULL && g_scene.pages != 0) {
        layer = port_GlSceneLayerOf(one->key, one->path, name);
    }
    /* a picture still being read: the game's own page until it is in
     * (port_GlSceneLoaded enters it again) */
    one->is_waiting = layer >= 0 && g_scene.layer_state[layer] == LAYER_READING;
    if (one->is_waiting) {
        layer = -1;
    }
    if (layer >= 0) {
        u16 *row = &g_scene.base_rows[layer * COLORS_MAX];

        for (i = 0; i < colors; i++) {
            row[i] = vram[one->base_y * PORT_VRAM_WIDTH +
                          ((one->base_x + i) & (PORT_VRAM_WIDTH - 1))];
        }
        glActiveTexture(GL_TEXTURE0 + PORT_GL_SCENE_BASES_UNIT);
        glBindTexture(GL_TEXTURE_2D, g_scene.bases);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 2);
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, layer, COLORS_MAX, 1,
                        GL_RED_INTEGER, GL_UNSIGNED_SHORT, row);
        glPixelStorei(GL_UNPACK_ALIGNMENT, RGBA);
        glActiveTexture(GL_TEXTURE0);
    }
    port_GlSceneSetEntry(
        column,
        port_GlSceneRow(one->clut_y +
                        (one->is_bank ? PORT_VRAM_BANK_NAMED_Y : 0)),
        (u16)(ENTRY_SEEN | (one->owner != OWNER_STAGE ? ENTRY_FIGHTER : 0U) |
              (layer >= 0
                   ? (u32)(layer + 1) | ((u32)one->bits << ENTRY_BITS_SHIFT)
                   : 0U)));
}

/* The array made, where there is none, for the first picture found: room
 * for `layers` of its size. */
static void port_GlSceneWantArray(const char *path, s32 layers) {
    s32 side = g_scene.pages != 0 ? g_scene.side : port_GlSceneSideOf(path);

    if (side == 0) {
        return;
    }
    if (layers > LAYERS_MAX) {
        layers = LAYERS_MAX;
    }
    if (!port_GlSceneMakeArray(side, layers)) {
        port_GlSceneSay("mods: the card has no room for %d pictures of %d by "
                        "%d",
                        layers, side, side);
    }
}

/* A page and a table looked up again, alone: one the faces named that no
 * TIM of the stage brought, or one written over. */
static void port_GlSceneLookUp(Looked *one) {
    char name[PORT_MOD_KEY_SIZE];

    one->is_stale = FALSE;
    port_GlSceneKeyOf(one, name);
    if (one->path != NULL && g_scene.pages == 0) {
        port_GlSceneWantArray(one->path, LAYERS_SPARE);
    }
    port_GlSceneEnter(one);
}

/* A page and a table the faces name, kept and, where `is_now` says,
 * looked up at once: FALSE where it is no page of the stage's. A page and
 * table of the second bank are named PORT_VRAM_BANK_NAMED_Y further
 * down. */
static s32 port_GlSceneAdd(
    s32 page_x, s32 page_y, s32 clut_x, s32 clut_y, s32 bits, s32 is_now) {
    PortTexturePage page;
    Looked *one;
    s32 base_x;
    s32 base_y;
    s32 owner;
    s32 column;
    s32 row = port_GlSceneRow(clut_y);
    s32 is_bank = clut_y >= PORT_VRAM_BANK_NAMED_Y;
    u32 owned = is_bank ? g_scene.owned_bank_pages : g_scene.owned_pages;

    if (is_bank) {
        if (page_y < PORT_VRAM_BANK_NAMED_Y) {
            return FALSE;
        }
        page_y -= PORT_VRAM_BANK_NAMED_Y;
        clut_y -= PORT_VRAM_BANK_NAMED_Y;
    }
    page.bits = bits;
    page.page_x = page_x;
    page.page_y = page_y;
    page.clut_x = 0;
    page.clut_y = 0;
    page.own = NULL;
    column = port_GlSceneColumn(page_x, page_y, clut_x);
    if (row < 0 ||
        (g_scene.lookup_table[row * LOOKUP_W + column] & ENTRY_SEEN) != 0) {
        return FALSE;
    }
    /* seen, whatever comes of it: a table that is not the stage's is not
     * asked about again */
    g_scene.lookup_table[row * LOOKUP_W + column] = ENTRY_SEEN;
    if (bits == PORT_MOD_TEXELS_16 || !port_TexturePageFits(&page) ||
        (owned & (1U << (column / TABLE_COLUMNS))) == 0 ||
        !port_GlSceneBaseOf(
            clut_x, clut_y, is_bank, &base_x, &base_y, &owner) ||
        g_scene.looked_count >= LOOKED_MAX) {
        return FALSE;
    }
    one = &g_scene.looked[g_scene.looked_count++];
    one->page_x = (s16)page_x;
    one->page_y = (s16)page_y;
    one->clut_x = (s16)clut_x;
    one->clut_y = (s16)clut_y;
    one->base_x = (s16)base_x;
    one->base_y = (s16)base_y;
    one->bits = (s16)bits;
    one->owner = (s16)owner;
    one->is_bank = (s16)is_bank;
    one->is_stale = FALSE;
    one->is_waiting = FALSE;
    one->key = 0;
    one->path = NULL;
    if (is_now) {
        port_GlSceneLookUp(one);
    }
    return TRUE;
}

/* Which pixels of video memory are an owner's own, and which pages have
 * any of anyone's. */
static void port_GlSceneOwn(s32 owner) {
    const PortStageTexture *textures;
    u8 *own = g_scene.own[owner];
    s32 count;
    s32 i;

    u32 *owned = OWNER_IS_BANK(owner) ? &g_scene.owned_bank_pages
                                      : &g_scene.owned_pages;

    textures = port_GlSceneTextures(owner, &count);
    memset(own, 0, (size_t)PORT_VRAM_PIXELS);
    for (i = 0; i < count; i++) {
        const PortStageTexture *one = &textures[i];
        s32 x;
        s32 y;

        for (y = one->y; y < one->y + one->h && y < PORT_VRAM_HEIGHT; y++) {
            for (x = one->x; x < one->x + one->w && x < PORT_VRAM_WIDTH; x++) {
                if (x < 0 || y < 0) {
                    continue;
                }
                own[y * PORT_VRAM_WIDTH + x] = 1;
                *owned |=
                    1U << (((x / PAGE_X_STEP) % PAGES_ACROSS) +
                           ((y / PAGE_Y_STEP) & 1) * PAGES_ACROSS);
            }
        }
    }
}

/* Every page and table one TIM of the stage brings: each page its picture
 * lies in (one wider than a page goes on into the next, as a fighter's
 * do), with each table of its colour table's block (a block of 4-bit
 * tables may hold several side by side, and rows of them). */
static void port_GlSceneAddTim(const PortStageTexture *one, s32 owner) {
    s32 colors = one->bits == PORT_MOD_TEXELS_4 ? COLORS_4 : COLORS_MAX;
    s32 across = one->clut_w / colors;
    s32 named = OWNER_IS_BANK(owner) ? PORT_VRAM_BANK_NAMED_Y : 0;
    /* a page's width in pixels: 64 of 4-bit texels, 128 of 8-bit */
    s32 page_w = one->bits == PORT_MOD_TEXELS_4 ? PAGE_X_STEP : 2 * PAGE_X_STEP;
    s32 page_x;
    s32 page_y;
    s32 row;
    s32 i;

    if (one->clut_x < 0 || one->bits == PORT_MOD_TEXELS_16) {
        return;
    }
    if (across < 1) {
        across = 1;
    }
    for (page_y = one->y - one->y % PAGE_Y_STEP; page_y < one->y + one->h;
         page_y += PAGE_Y_STEP) {
        for (page_x = one->x - one->x % PAGE_X_STEP; page_x < one->x + one->w;
             page_x += page_w) {
            for (row = 0; row < one->clut_h; row++) {
                for (i = 0; i < across; i++) {
                    port_GlSceneAdd(page_x, page_y + named,
                                    one->clut_x + i * colors,
                                    one->named_y + row + named, one->bits,
                                    FALSE);
                }
            }
        }
    }
}

/* The stage's and the fighters' textures as the game has just loaded
 * them: which pixels are whose, and every page and table their TIMs bring
 * looked up, the pictures of all of them read now. */
static void port_GlSceneReadStage(void) {
    char name[PORT_MOD_KEY_SIZE];
    const PortStageTexture *textures;
    const char *first = NULL;
    s32 count;
    s32 pictures = 0;
    s32 owner;
    s32 i;
    s32 j;

    (void)port_RemasterStageTextures(&count, &g_scene.version);
    memset(g_scene.lookup_table, 0,
           (size_t)LOOKUP_W * LOOKUP_H * sizeof(*g_scene.lookup_table));
    glActiveTexture(GL_TEXTURE0 + PORT_GL_SCENE_LOOKUP_UNIT);
    glBindTexture(GL_TEXTURE_2D, g_scene.lookup);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 2);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, LOOKUP_W, LOOKUP_H, GL_RED_INTEGER,
                    GL_UNSIGNED_SHORT, g_scene.lookup_table);
    glPixelStorei(GL_UNPACK_ALIGNMENT, RGBA);
    glActiveTexture(GL_TEXTURE0);
    g_scene.looked_count = 0;
    g_scene.layer_count = 0;
    g_scene.generation++;
    g_scene.is_full_said = FALSE;
    g_scene.is_short = FALSE;
    g_scene.has_stale = FALSE;
    g_scene.owned_pages = 0;
    g_scene.owned_bank_pages = 0;
    g_scene.is_bank_view_fresh = FALSE;
    for (owner = 0; owner < OWNERS; owner++) {
        port_GlSceneOwn(owner);
    }
    for (owner = 0; owner < OWNERS; owner++) {
        textures = port_GlSceneTextures(owner, &count);
        /* a partner's only while the fight has the bank */
        if (OWNER_IS_BANK(owner) && port_VramBank() == NULL) {
            continue;
        }
        for (i = 0; i < count; i++) {
            port_GlSceneAddTim(&textures[i], owner);
        }
    }
    /* the keys first, to know how many pictures there are; then the array
     * as large as that and a few more, and the pictures into it */
    for (i = 0; i < g_scene.looked_count; i++) {
        Looked *one = &g_scene.looked[i];

        port_GlSceneKeyOf(one, name);
        if (one->path == NULL) {
            continue;
        }
        for (j = 0; j < i; j++) {
            if (g_scene.looked[j].path != NULL &&
                g_scene.looked[j].key == one->key) {
                break;
            }
        }
        if (j == i) {
            pictures++;
            first = first != NULL ? first : one->path;
        }
    }
    if (pictures + LAYERS_SPARE < g_scene.layers_least) {
        pictures = g_scene.layers_least - LAYERS_SPARE;
    }
    if (first != NULL &&
        (g_scene.pages == 0 || g_scene.capacity < pictures + LAYERS_SPARE)) {
        if (g_scene.pages != 0) {
            /* made again, larger, at the size it has */
            port_GlSceneMakeArray(g_scene.side, pictures + LAYERS_SPARE);
        } else {
            port_GlSceneWantArray(first, pictures + LAYERS_SPARE);
        }
    }
    for (i = 0; i < g_scene.looked_count; i++) {
        port_GlSceneEnter(&g_scene.looked[i]);
    }
    if (g_scene.looked_count > 0) {
        port_GlSceneSay("mods: the stage's pages: %d looked up, %d with a "
                        "picture",
                        g_scene.looked_count, g_scene.layer_count);
    }
}

void port_GlSceneNewFrame(void) {
    const PortStageTexture *textures;
    u32 version;
    s32 count;
    s32 i;

    if (!port_GlSceneIsWanted() || !port_GlSceneMakeTables()) {
        return;
    }
    /* the bank as this frame has it (port_GlSceneVramOf) */
    g_scene.is_bank_view_fresh = FALSE;
    textures = port_RemasterStageTextures(&count, &version);
    (void)textures;
    if (version != g_scene.version) {
        g_scene.layers_least = 0;
        port_GlSceneReadStage();
        return;
    }
    /* Pages with pictures found after the stage's were read, more than
     * the array had room for: a fighter's face as it changes (his eyes
     * shut, his mouth open) is a new key of the page it is copied into,
     * and of each table of it, and four fighters bring many. The pages
     * are read again, the array twice as large, as many times as it
     * takes up to LAYERS_MAX. */
    if (g_scene.is_short && g_scene.capacity < LAYERS_MAX) {
        g_scene.layers_least = 2 * g_scene.capacity;
        port_GlSceneReadStage();
        return;
    }
    if (!g_scene.has_stale) {
        return;
    }
    g_scene.has_stale = FALSE;
    for (i = 0; i < g_scene.looked_count; i++) {
        if (g_scene.looked[i].is_stale) {
            port_GlSceneLookUp(&g_scene.looked[i]);
        }
    }
}

s32 port_GlSceneFind(const PortRemasterPrim *prim) {
    s32 column;
    s32 row;
    u16 entry;

    /* port_StageIsDressed(): a stage fought Classic is drawn from the
     * game's own textures, not a mod's pages (stages-hd); a fighter's
     * pages are his whatever the stage is */
    if (!prim->is_fighter && !port_StageIsDressed()) {
        return FALSE;
    }
    if (!prim->is_textured || !prim->has_depth ||
        prim->texel_bits == PORT_MOD_TEXELS_16 || g_scene.lookup == 0 ||
        !port_GlSceneIsWanted() ||
        prim->window_and_u != PORT_MOD_PAGE_SIDE - 1 ||
        prim->window_and_v != PORT_MOD_PAGE_SIDE - 1 ||
        prim->window_or_u != 0 || prim->window_or_v != 0 ||
        (row = port_GlSceneRow(prim->clut_y)) < 0) {
        return FALSE;
    }
    column = port_GlSceneColumn(prim->page_x, prim->page_y, prim->clut_x);
    entry = g_scene.lookup_table[row * LOOKUP_W + column];
    if ((entry & ENTRY_SEEN) == 0) {
        port_GlSceneAdd(prim->page_x, prim->page_y, prim->clut_x, prim->clut_y,
                        prim->texel_bits, TRUE);
        entry = g_scene.lookup_table[row * LOOKUP_W + column];
    }
    /* a fighter's face by a fighter's table, the stage's by the stage's */
    return (entry & ENTRY_LAYER_MASK) != 0 &&
           (s32)((entry >> ENTRY_BITS_SHIFT) & 3) == prim->texel_bits &&
           ((entry & ENTRY_FIGHTER) != 0) == (prim->is_fighter != 0);
}

/* Whether two rectangles of video memory meet. */
static s32 port_GlSceneMeets(
    s32 x, s32 y, s32 w, s32 h, s32 x2, s32 y2, s32 w2, s32 h2) {
    return x < x2 + w2 && x2 < x + w && y < y2 + h2 && y2 < y + h;
}

/* Whether the rectangle holds any pixel that is an owner's own: a page's
 * key is made of those alone (port_TexturePageKey, `own`), so a write of
 * none of them (the fog's colour tables, a line at a time every frame,
 * which lie inside the pages' rectangles) cannot change it. Looking the
 * pages up again for those writes was a sixth of the processor's time in
 * a fight on the owner's PC (2026-11-18). */
static s32 port_GlSceneWritesOwn(s32 owner, s32 x, s32 y, s32 w, s32 h) {
    s32 right = x + w < PORT_VRAM_WIDTH ? x + w : PORT_VRAM_WIDTH;
    s32 bottom = y + h < PORT_VRAM_HEIGHT ? y + h : PORT_VRAM_HEIGHT;
    s32 i;
    s32 j;

    if (x < 0 || y < 0 || w <= 0 || h <= 0) {
        return TRUE;
    }
    for (j = y; j < bottom; j++) {
        const u8 *row = g_scene.own[owner] + j * PORT_VRAM_WIDTH;

        for (i = x; i < right; i++) {
            if (row[i] != 0) {
                return TRUE;
            }
        }
    }
    return FALSE;
}

/* A write of the console's video memory (is_bank FALSE) or of the second
 * bank's, at the console's coordinates of what it stands in for: the
 * pages and tables of that memory it reaches looked up again. */
static void port_GlSceneWrote(s32 x, s32 y, s32 w, s32 h, s32 is_bank) {
    s32 writes_own[OWNERS]; /* -1: not yet asked */
    s32 i;

    if (g_scene.looked == NULL) {
        return;
    }
    for (i = 0; i < OWNERS; i++) {
        writes_own[i] = -1;
    }
    for (i = 0; i < g_scene.looked_count; i++) {
        Looked *one = &g_scene.looked[i];
        PortTexturePage page;

        page.bits = one->bits;
        if (one->is_stale || (one->is_bank != 0) != (is_bank != 0)) {
            continue;
        }
        if ((port_GlSceneMeets(x, y, w, h, one->page_x, one->page_y,
                               port_TexturePagePixel(&page, PORT_MOD_PAGE_SIDE),
                               PORT_MOD_PAGE_SIDE) &&
             (writes_own[one->owner] >= 0
                  ? writes_own[one->owner]
                  : (writes_own[one->owner] =
                         port_GlSceneWritesOwn(one->owner, x, y, w, h)))) ||
            port_GlSceneMeets(
                x, y, w, h, one->base_x, one->base_y,
                one->bits == PORT_MOD_TEXELS_4 ? COLORS_4 : COLORS_MAX, 1)) {
            one->is_stale = TRUE;
            g_scene.has_stale = TRUE;
        }
    }
}

void port_GlSceneWroteVram(s32 x, s32 y, s32 w, s32 h) {
    port_GlSceneWrote(x, y, w, h, FALSE);
}

void port_GlSceneWroteBank(s32 y, s32 h) {
    if (h > 0) {
        port_GlSceneWrote(PORT_VRAM_BANK_X, y, PORT_VRAM_BANK_W, h, TRUE);
    }
}

s32 port_GlSceneBind(void) {
    if (g_scene.lookup == 0 || g_scene.pages == 0 || g_scene.layer_count == 0 ||
        !port_GlSceneIsWanted()) {
        return FALSE;
    }
    glActiveTexture(GL_TEXTURE0 + PORT_GL_SCENE_LOOKUP_UNIT);
    glBindTexture(GL_TEXTURE_2D, g_scene.lookup);
    glActiveTexture(GL_TEXTURE0 + PORT_GL_SCENE_PAGES_UNIT);
    glBindTexture(GL_TEXTURE_2D_ARRAY, g_scene.pages);
    glActiveTexture(GL_TEXTURE0 + PORT_GL_SCENE_BASES_UNIT);
    glBindTexture(GL_TEXTURE_2D, g_scene.bases);
    glActiveTexture(GL_TEXTURE0);
    return TRUE;
}

float port_GlSceneLevels(void) {
    return g_scene.levels > 0 ? (float)(g_scene.levels - 1) : 0.0f;
}
