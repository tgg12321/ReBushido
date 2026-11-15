#ifndef SPRITE_H
#define SPRITE_H

/* Sprite sheets (select.c). Everything flat on the screen (the menus, the
 * counters over a fight, the result tables) is drawn from sheets: a piece of
 * texture cut into cells, each cell with its place on the screen. */

#include "common.h"
#include <psxsdk/libgpu.h>

/* A position on the 640x240 screen. */
typedef struct {
    s16 x;
    s16 y;
} ScreenPoint;

/* A width and height in pixels. */
typedef struct {
    s16 w;
    s16 h;
} ScreenSize;

/* One cell of a sheet: a rectangle of texture and where it goes. x and y are
 * its offset from the place the sheet is drawn at, u and v its offset in the
 * texture from the sheet's origin, w and h its size. */
typedef struct {
    s16 x;
    s16 y;
    u8 u;
    u8 v;
    u8 w;
    u8 h;
} SpriteCell;

/* A sheet: the texture page and colour table its cells use, the origin in
 * the page that the cells' u and v are measured from, and how many cells
 * there are. tp0 and tp1 are the bits of the texture page, cx and cy where
 * the colour table is in video memory. In a resource file the cells follow
 * the sheet; a file may put two or three sheets ahead of one set of cells
 * (the plain look, then a highlight for each player). */
typedef struct {
    u8 tp0;
    u8 tp1;
    u8 count;
    /* 0 in the program's sheets but the best times' four, which have
     * 0xFF; no code reads it */
    u8 unk3;
    u16 cx;
    u16 cy;
    u16 ubase;
    u16 vbase;
    SpriteCell cells[0];
} SpriteSheet;

/* A sheet's address as a resource file keeps it: an entry of one of its
 * lists of sheets, four bytes (PTR32, common.h). */
typedef PTR32(SpriteSheet *) SpriteSheetSlot;
/* ... and the address of such a list, as a file's head keeps it. */
typedef PTR32(SpriteSheetSlot *) SpriteListSlot;

/* A request to draw a sheet. The primitives are built where sprt_out or
 * ft4_out points (which of the two depends on the function) and linked into
 * entry ot_index of the display's ordering table. */
typedef struct {
    SpriteSheet *header;
    SpriteCell *table; /* the sheet's cells */
    s32 sprt_out;      /* where the next SPRT goes */
    s32 ft4_out;       /* where the next POLY_FT4 goes */
    s32 semi;          /* nonzero: semi-transparent */
    u32 ot_index;
    s32 x; /* where on the screen */
    s32 y;
    s32 scale_x; /* 0x100 is life size; the quads only */
    s32 scale_y;
    u8 has_color; /* nonzero: tinted with the colour below */
    u8 col_r;
    u8 col_g;
    u8 col_b;
} SpriteDraw;

/* SpriteDraw.scale_x and scale_y at life size. */
#define SPRITE_SCALE_ONE 0x100

/* sprite_GetTPage's `abr_bits`: the GPU's blending mode in its place in a
 * texture page's word. 0 is half of each; these are for a sheet that is
 * added to what is under it, taken off it, and added at a quarter. */
#define SPRITE_ABR_ADD 0x20
#define SPRITE_ABR_SUBTRACT 0x40
#define SPRITE_ABR_ADD_QUARTER 0x60
/* A sheet's texture page as the GPU wants it: tp0 without the bits for the
 * blending mode and the colour depth, and tp1 for the depth. */
#define SPRITE_TPAGE(sheet) (((sheet)->tp0 & 0xFE1F) + ((sheet)->tp1 << 7))

/* The brightness of something that glows under a cursor (the menus, the
 * two select screens): it swings with `sine`, an rsin of the screen's
 * frame count, `depth` either side of `center`. The sum is a byte's, so
 * the centre is taken off as what is left of 256. */
#define SPRITE_GLOW(sine, depth, center)                                       \
    ((((sine) * (depth)) >> 12) - (0x100 - (center)))

/* How sprite_DrawScaled draws: mirrored, or with one of the GPU's blending
 * modes for semi-transparent primitives. */
enum SpriteQuadMode {
    SPRITE_QUAD_NORMAL = 0,
    SPRITE_QUAD_MIRROR_X = 1,
    SPRITE_QUAD_MIRROR_Y = 2,
    SPRITE_QUAD_MIRROR_XY = 3,
    SPRITE_QUAD_ABR_1 = 4,
    SPRITE_QUAD_ABR_2 = 5,
    SPRITE_QUAD_ABR_3 = 6,
};

/* Draws a sheet's cells as sprites, leaving out the ones off the screen.
 * Returns where the next sprite would go. A sprite carries no texture page:
 * the caller adds a DR_MODE with the sheet's (sprite_GetTPage) behind them. */
extern s32 sprite_Draw(SpriteDraw *draw);
/* Draws the cells as textured quads, scaled about the first cell's corner.
 * Returns, and leaves in ft4_out, where the next quad would go. */
extern s32 sprite_DrawScaled(SpriteDraw *draw, s32 mode);
/* A sheet's texture page, with `abr_bits` (a blending mode, already in
 * place) added. */
extern s32 sprite_GetTPage(SpriteSheet *sheet, s32 abr_bits);

/* ---- a 2D screen's resource file and primitive memory ---- */

/* The start of every 2D-screen resource file (MOD.BIN, SEL*.BIN, D_SEL.BIN,
 * NAR.BIN, STAFF.BIN). A file begins with a list of file-relative offsets
 * ending in -1, which sprite_RelocateFile turns into addresses; the first
 * five mean the same in every file. `sounds` is the screen's sound bank
 * (empty in D_SEL, NAR and STAFF). free_ram_start is where the image data
 * begins: once the image is in video memory the screen reuses that memory
 * for its primitives. `image` is the screen's texture page. The two strips
 * are the 640x32 band at the bottom of video memory, which holds the colour
 * tables, in colour and in grayscale. What follows the head is each file's
 * own, mostly lists of sheets. */
typedef struct {
    PTR32(struct SoundVab *) sounds;
    s32 free_ram_start;
    s32 image;
    s32 bottom_strip;
    s32 grayscale_bottom_strip;
} SpriteFileHead;

/* The primitive memory of a 2D screen for one of the two frames in flight:
 * where the region kept for each kind of primitive starts. Tiles and sprites
 * are in the opposite order to PrimCursors. */
typedef struct {
    s32 ft4;
    PTR32(void *) f4;
    PTR32(void *) g4;
    PTR32(void *) tile;
    s32 sprt;
    PTR32(void *) mode;
    PTR32(void *) area;
    PTR32(void *) offset;
    PTR32(void *) tail_region;
    /* two more words, which sprite_InitPools leaves alone and no code
     * reads */
    s32 unk_24;
    s32 unk_28;
} PrimPool;

/* How many bytes of a pool each kind of primitive gets, and how many
 * primitives that is. */
#define PRIM_POOL_FT4_SIZE 40000  /* 1000 textured quads */
#define PRIM_POOL_F4_SIZE 24000   /* 1000 plain quads */
#define PRIM_POOL_G4_SIZE 1080    /* 30 shaded quads */
#define PRIM_POOL_TILE_SIZE 1600  /* 100 tiles */
#define PRIM_POOL_SPRT_SIZE 7000  /* 350 sprites */
#define PRIM_POOL_MODE_SIZE 600   /* 50 drawing modes */
#define PRIM_POOL_AREA_SIZE 120   /* 10 drawing areas */
#define PRIM_POOL_OFFSET_SIZE 120 /* 10 drawing offsets */
#define PRIM_POOL_TAIL_SIZE 272
/* from the sprites to the end of a pool */
#define PRIM_POOL_REST_SIZE                                                    \
    (PRIM_POOL_SPRT_SIZE + PRIM_POOL_MODE_SIZE + PRIM_POOL_AREA_SIZE +         \
     PRIM_POOL_OFFSET_SIZE + PRIM_POOL_TAIL_SIZE)

/* Where the next primitive of each kind goes while a 2D screen draws its
 * frame. Filled from the frame's PrimPool; the draw functions build at a
 * cursor and advance it. */
typedef struct {
    s32 ft4;
    POLY_F4 *f4;
    POLY_G4 *g4;
    s32 sprt;
    TILE *tile;
    DR_MODE *mode;
    DR_AREA *area;
    DR_OFFSET *offset;
} PrimCursors;

/* A coloured rectangle, as the resource files list them (the key
 * configuration page's bars, the team select's rows). */
typedef struct {
    s16 x, y, w, h;
    u8 r, g, b, pad;
} ColorRect;

/* Where a screen's pictures go in video memory (1024 x 512). The display's
 * two buffers are its left 640 columns, down to row 480; under them is the
 * band of colour tables. Right of them is the screen's texture page, and
 * under that a strip not so wide whose pixels follow the page's in the
 * file (two bytes a pixel). */
#define VRAM_PAGE_X 640
#define VRAM_PAGE_WIDTH 384
#define VRAM_PAGE_HEIGHT 476
#define VRAM_PAGE_FOOT_WIDTH 368
#define VRAM_PAGE_FOOT_HEIGHT 36
#define VRAM_PAGE_BYTES (VRAM_PAGE_WIDTH * VRAM_PAGE_HEIGHT * 2)
#define VRAM_COLORS_Y 480
#define VRAM_COLORS_HEIGHT 32

/* A screen copies a strip one pixel high from its colour tables to here
 * when it loads (what it is for there is not known). */
#define SPRITE_STRIP_COPY_X 960
#define SPRITE_STRIP_COPY_Y 510

/* Turns a file's leading offsets (up to a -1) into addresses. */
extern void sprite_RelocateFile(s32 *file);
/* Turns one of a file's lists of offsets (up to a 0; -1 entries are left)
 * into addresses. */
extern void sprite_RelocateList(void *file, void *list);
/* Lays the two frames' pools out from `memory` on. Returns the first byte
 * past them. */
extern s32 sprite_InitPools(s32 memory, PrimPool *pools);
/* Reads resource file `data_file` of the disc's data group to `file`,
 * relocates its head and loads its image and bottom strip into video
 * memory. */
extern void sprite_LoadFile(s32 data_file, SpriteFileHead *file);
/* Loads the file's bottom strip again, in colour or in grayscale as the
 * option now says. */
extern void sprite_LoadBottomStrip(SpriteFileHead *file);

#endif /* SPRITE_H */
