/* The staff roll, drawn from STAFF.BIN: the credits scroll up the screen a
 * line of pixels a frame, and near the end a last card appears, stays and
 * fades. The screen that shows it (staff_Init, staff_Update) is with the
 * fight's other screens. */
#include "common.h"
#include "bb2.h"

/* Defined further down. */
static s32 g_staff_pool_frame;

/* The file in the disc's data group. */
#define STAFF_FILE CDROM_DATA_STAFF_BIN

/* The sheet of the card the roll ends on: one red character, "the end". */
#define STAFF_END_CARD 10
/* The frames at which the card appears, starts to fade and the roll is
 * over; the fade takes STAFF_FADE frames. */
#define STAFF_CARD_FRAME 2730
#define STAFF_FADE_FRAME 2820
#define STAFF_END_FRAME 2880
#define STAFF_FADE 15
/* The card's tint before it fades: the texture as it is. */
#define STAFF_TINT_PLAIN 0x80
/* The entry of the display's ordering table everything is drawn in. */
#define STAFF_LAYER 2

/* This file's variables, in the order the original has them. */
static s32 g_staff_frame; /* frames the roll has run: how far it has scrolled */
static PrimPool *g_staff_pools;
static StaffFile *g_staff_file;
/* cleared with the frame count when the roll loads; nothing reads it */
static s32 g_staff_unused_load_word;

/* Where the file's image began, which is free once it is in video memory. */
static s32 staff_GetFreeRam(StaffFile *file) {
    return file->head.free_ram_start;
}

/* The pool of frame `frame` (0 or 1). */
static PrimPool *staff_GetPrimPool(s32 frame) { return &g_staff_pools[frame]; }

/* Draws a frame: the end card once its time has come, then every sheet of
 * the roll, `g_staff_frame` pixels up from where it starts. */
static void staff_DrawRoll(PrimCursors *prims) {
    SpriteDraw draw;
    SpriteSheetSlot *sheets;
    s32 i;
    s32 no_abr;

    no_abr = 0;
    draw.ot_index = STAFF_LAYER;
    draw.has_color = 0;
    draw.semi = 0;
    draw.x = 0;
    draw.header = PTR(g_staff_file->sheets[STAFF_END_CARD]);
    draw.y = 0;
    draw.table = draw.header->cells;
    sheets = g_staff_file->sheets;
    if (g_staff_frame >= STAFF_CARD_FRAME) {
        if (g_staff_frame >= STAFF_FADE_FRAME) {
            s16 level;
            draw.has_color = 1;
            /* from the plain tint (the shift is times that) down to black */
            level = STAFF_TINT_PLAIN -
                    (((g_staff_frame - STAFF_FADE_FRAME) << 7) / STAFF_FADE);
            if (level < 0) {
                level = 0;
            }
            draw.col_r = draw.col_g = draw.col_b = level;
        }
        draw.sprt_out = prims->sprt;
        prims->sprt = sprite_Draw(&draw);
        SetDrawMode(prims->mode, 1, 0, sprite_GetTPage(draw.header, no_abr), 0);
        AddPrim(g_disp_ot + draw.ot_index, prims->mode);
        prims->mode++;
    }
    draw.has_color = 0;
    /* Every sheet but the last, which is the card. */
    for (i = 0; PTR(sheets[i + 1]) != (SpriteSheet *)ADDR(-1); i++) {
        draw.header = PTR(sheets[i]);
        draw.table = draw.header->cells;
        draw.y = -g_staff_frame;
        draw.sprt_out = prims->sprt;
        prims->sprt = sprite_Draw(&draw);
        SetDrawMode(prims->mode, 1, 0, sprite_GetTPage(draw.header, no_abr), 0);
        AddPrim(g_disp_ot + draw.ot_index, prims->mode);
        prims->mode++;
    }
}

/* Loads STAFF.BIN to `memory` (the two frames' pool records come first, the
 * pools after the file) and starts the roll from its top, over black.
 * Returns 1. */
s32 staff_LoadRoll(s32 memory) {
    StaffFile *file;

    ClearOTagR(g_disp_ot, DISP_OT_SIZE);
    file = (StaffFile *)ADDR(memory + 2 * sizeof(PrimPool));
    g_staff_pools = (PrimPool *)ADDR(memory);
    g_staff_file = file;
    sprite_LoadFile(STAFF_FILE, &file->head);
    sprite_InitPools(staff_GetFreeRam(file), g_staff_pools);
    g_staff_pool_frame = 0;
    g_staff_frame = 0;
    g_staff_unused_load_word = 0;
    disp_SetBackground(1, 0, 0, 0);
    return 1;
}

/* A frame of the roll: starts the frame's pool, draws the roll and moves it
 * on a line. Returns 1 when the roll is over. */
s32 staff_UpdateRoll(void) {
    PrimCursors prims;
    PrimPool *pool;
    g_staff_pool_frame++;
    pool = staff_GetPrimPool(g_staff_pool_frame & 1);
    prims.ft4 = pool->ft4;
    prims.g4 = PTR(pool->g4);
    prims.sprt = pool->sprt;
    prims.tile = PTR(pool->tile);
    prims.mode = PTR(pool->mode);
    prims.area = PTR(pool->area);
    prims.offset = PTR(pool->offset);
    staff_DrawRoll(&prims);
    g_staff_frame++;
    return g_staff_frame >= STAFF_END_FRAME;
}

/* Frames drawn; the low bit picks the pool. */
static s32 g_staff_pool_frame = 0;
