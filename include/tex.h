#ifndef TEX_H
#define TEX_H

/* Textures in video memory (tex.c, tex_scroll.c): loading the TIM images of
 * a file, the black and white option, tinting a colour table, textures
 * that scroll, and moving a loaded mesh's textures to another place
 * (tex_Shift, in render.c). */

#include "common.h"
#include <psxsdk/libgpu.h>

/* ---- places in video memory ----
 * A primitive finds its texture through two words. The texture-page word has
 * the page's column in its low four bits and its row in the fifth: pages are
 * TPAGE_WIDTH pixels apart across video memory and TPAGE_HEIGHT down. The
 * colour-table word has x / 16 in its low six bits and y in the nine above;
 * its top bit is not part of the place. */
#define TPAGE_WIDTH 64
#define TPAGE_HEIGHT 256
#define TPAGE_COLUMN_MASK 0xF
#define TPAGE_ROW_SHIFT 4
#define TPAGE_PLACE_MASK 0x1F /* the column and the row */
#define CLUT_X_MASK 0x3F
#define CLUT_Y_SHIFT 6
#define CLUT_Y_MASK 0x1FF
#define CLUT_TOP_BIT 0x8000
/* A 15-bit pixel: five bits each of red (the lowest), green and blue, and a
 * top bit that says how the pixel blends. */
#define PIXEL_BITS 16
#define PIXEL_PART_MASK 0x1F
#define PIXEL_GREEN_SHIFT 5
#define PIXEL_BLUE_SHIFT 10
#define PIXEL_TOP_BIT 0x8000
/* A pixel's green and blue where they lie in it. */
#define PIXEL_GREEN_MASK (PIXEL_PART_MASK << PIXEL_GREEN_SHIFT)
#define PIXEL_BLUE_MASK (PIXEL_PART_MASK << PIXEL_BLUE_SHIFT)
/* The colours of a 4-bit texture's colour table: one row of video memory. */
#define CLUT_COLORS 16
/* Video memory is 1024 pixels by this many. */
#define VRAM_HEIGHT 512

/* A TIM picture in a file of its own: after the file's header each of its
 * two blocks, the colour table and the pixels, has a header. A picture
 * with no table has its pixels at TIM_PIXELS_NO_CLUT; one with a table has
 * it at TIM_CLUT and its pixels after the table's colours. */
#define TIM_HEADER_SIZE 8
#define TIM_BLOCK_HEADER_SIZE 12
#define TIM_CLUT (TIM_HEADER_SIZE + TIM_BLOCK_HEADER_SIZE)
#define TIM_PIXELS_NO_CLUT (TIM_HEADER_SIZE + TIM_BLOCK_HEADER_SIZE)
#define TIM_PIXELS(colors)                                                     \
    (TIM_CLUT + (colors) * sizeof(u16) + TIM_BLOCK_HEADER_SIZE)

/* ---- loading ----
 * A file's textures are groups of TIM images: a table of offsets to the
 * groups; each group a count and that many entries of an offset to the TIM
 * and where its picture and its colour table go in video memory. */

/* Loads group `group_index` of the texture file at `cursor` where its entries
 * say. */
extern void tex_LoadGroup(u32 *cursor, s32 group_index);
/* The same, moved by (x_offset, y_offset). */
extern void tex_LoadGroupAt(
    u32 *cursor, s32 group_index, s16 x_offset, s16 y_offset);
/* The same, the pictures moved by one offset and the colour tables by
 * another. */
extern void tex_LoadGroupAtSplit(
    u32 *cursor, s32 group_index, s16 image_x_offset, s16 image_y_offset,
    s16 clut_x_offset, s16 clut_y_offset);
/* A stage's textures: the colour tables left of x 640 go one row lower,
 * leaving the row above for the fog to work in (stage.h). */
extern void tex_LoadStageGroup(u32 *cursor, s16 group_index);
/* tex_LoadTim(tim, x, y, clut_x, clut_y), tex_LoadTimClut(tim, x, y) and
 * tex_ReadTim load or describe one TIM. They have no prototypes: they are
 * defined in the old style with s16 parameters and called with ints. */

/* ---- black and white ----
 * With the option on (g_scene_settings.grayscale), colour tables are made
 * gray as they are loaded. */
extern s32 tex_IsGrayscale(void);

/* ---- tinting ---- */

/* How a colour table is tinted (tex.c's tex_TintClut). */
enum TexTintMode {
    TEX_TINT_SCALE = 0, /* each part of a colour by its multiplier */
    TEX_TINT_GRAY = 1,  /* the colour's brightness by the multipliers */
};

/* Tints `width` colours of video memory at (x, y) where they are, each
 * part by its multiplier (ONE leaves it as it is). */
extern void tex_TintClutInPlace(
    s32 x, s32 y, s32 width, s32 red, s32 green, s32 blue);

/* ---- scrolling textures ----
 * A texture that flows (two stages have one: under the lost bridge it is
 * the water of the stream) is a rectangle of video memory copied each frame to
 * where the stage's meshes look for it, cut in two at a line that moves on
 * every frame. The texture comes in three sizes, each half the one before, and
 * all three are copied. */

/* TexScroll.ctl, and a row of g_tex_scroll_presets. */
enum TexScrollControl {
    TEX_SCROLL_SRC_X = 0,
    TEX_SCROLL_SRC_Y = 1,
    TEX_SCROLL_WIDTH = 2,
    TEX_SCROLL_HEIGHT = 3,
    TEX_SCROLL_DEST_X = 4,
    TEX_SCROLL_DEST_Y = 5,
    TEX_SCROLL_SPEED = 6, /* lines a frame */
    TEX_SCROLL_CONTROLS = 7,
};

/* A scrolling texture. */
typedef struct {
    s32 phase; /* how far the texture has scrolled, in lines */
    /* the copies: for each frame's buffer, two for each of the sizes */
    DR_MOVE move[2][6];
    /* enum TexScrollControl */
    s16 ctl[TEX_SCROLL_CONTROLS];
} TexScroll; /* 0x134 bytes */

extern u16 g_tex_scroll_presets[2][TEX_SCROLL_CONTROLS];

/* Sets scroll `scroll_index` up from preset `scroll_preset_index`. There is
 * room for one. */
extern void tex_InitScroll(s32 scroll_preset_index, s32 scroll_index);
/* Queues this frame's copies and moves the scroll on. */
extern void tex_QueueScroll(s32 scroll_index);

/* ---- moving a mesh's textures (tex_Shift) ----
 * A mesh's primitives name their texture page and colour table by position
 * in video memory. When a file's textures are loaded somewhere else than
 * they were made for, every textured primitive is shifted to match: `du`
 * and `dv` move the texture page (in pixels: TPAGE_WIDTH and TPAGE_HEIGHT
 * to the page), `dcx` and `dcy` the colour table (render.c). */
extern void tex_ShiftBlock(s16 block, s16 du, s16 dv, s16 dcx, s32 dcy);
extern void tex_ShiftBlockPages(
    s16 block, s16 du_pages, s16 dv_pages, s16 dcx_pages, s16 dcy_pages);
/* One mesh of a block. No prototype: the callers pass six ints. */
extern void tex_ShiftMesh();

#endif /* TEX_H */
