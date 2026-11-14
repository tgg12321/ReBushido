/* The narration, drawn from NAR.BIN: what story mode opens with, before
 * character select. Lines of text fade in and out one after another, and
 * over them the pictures go through their scenes (enum NarrationScene): a
 * picture across the screen; a smaller one on the left, joined by one on
 * the right; then a second picture across the screen, whose pixels take
 * the first one's place in video memory. Everything is timed in frames by
 * tables in the file. The cross button skips it. */
#include "common.h"
#include "bb2.h"

/* Defined further down. */
static RECT g_narration_picture_rect;

#define NARRATION_FILE CDROM_DATA_NAR_BIN /* of the disc's data group */

/* Frames a picture or a line takes to fade in or out. */
#define NARRATION_FADE 60
/* How bright a line of text gets while it fades, and once it is there. */
#define NARRATION_TEXT_FADE_LEVEL 112
#define NARRATION_TEXT_LEVEL 114

/* Where things are on the screen. */
#define NARRATION_PICTURE_Y 30
#define NARRATION_STRIP_WIDTH 128
#define NARRATION_LEFT_X 43
#define NARRATION_RIGHT_X 342

/* The ordering table's entries: the text is in front of the pictures. */
#define NARRATION_LAYER_TEXT 1
#define NARRATION_LAYER_PICTURE 2

/* A strip of the second picture is a TIM with a table of 256 colours; its
 * pixels begin here, and it is this wide in video memory. */
#define NARRATION_TIM_PIXELS TIM_PIXELS(256)
#define NARRATION_STRIP_TEXELS 64

/* This file's variables, in the order the original has them. */
static s32 g_narration_frame; /* frames the narration has run */
static PrimPool *g_narration_pools;
static NarrationFile *g_narration_file;
/* frames drawn; the low bit picks the pool */
static s32 g_narration_pool_frame;
static s32 g_narration_scene;

s32 narration_RelocateFile(NarrationFile *file) {
    sprite_RelocateList(file, file->picture_1_sheets);
    sprite_RelocateList(file, file->picture_2_sheets);
    sprite_RelocateList(file, file->left_sheets);
    sprite_RelocateList(file, file->right_sheets);
    sprite_RelocateList(file, file->line_sheets);
    return file->head.free_ram_start;
}

static PrimPool *narration_GetPrimPool(s32 frame) {
    return &g_narration_pools[frame];
}

/* Draws a frame: the scene's picture, faded by where the frame falls
 * between the scene's start and end, then every line of text that is up.
 * The scene moves on here too, when its picture has gone and the next one's
 * time has come. */
static void narration_Draw(MenuDrawCtx *ctx) {
    SpriteDraw draw;
    RECT rect;
    s16 *starts;
    s16 *ends;
    SpriteCell *cells;
    s32 abr;
    SpriteSheet **sheets;
    s32 i;
    s32 x;
    s32 level;
    NarrationLine *line;
    s32 *pixels;

    draw.ot_index = NARRATION_LAYER_PICTURE;
    draw.y = NARRATION_PICTURE_Y;
    draw.semi = 0;
    starts = g_narration_file->scene_starts;
    cells = g_narration_file->strip_cells;
    ends = g_narration_file->scene_ends;
    abr = SPRITE_ABR_ADD; /* pictures and text are added to what is under */
    if (g_narration_frame < starts[g_narration_scene] + NARRATION_FADE &&
        g_narration_frame >= starts[g_narration_scene]) {
        level = ((g_narration_frame - starts[g_narration_scene]) << 7) /
                NARRATION_FADE;
        draw.has_color = 1;
        draw.col_r = draw.col_g = draw.col_b = level;
    } else if (g_narration_frame >= ends[g_narration_scene] &&
               g_narration_frame < ends[g_narration_scene] + NARRATION_FADE) {
        level =
            ((NARRATION_FADE - (g_narration_frame - ends[g_narration_scene]))
             << 7) /
            NARRATION_FADE;
        draw.has_color = 1;
        draw.col_r = draw.col_g = draw.col_b = level;
    } else {
        draw.has_color = 0;
    }

    if (g_narration_frame < ends[NARRATION_END] + NARRATION_FADE) {
        switch (g_narration_scene) {
        case NARRATION_PICTURE_1:
            if (g_narration_frame + 1 >=
                ends[g_narration_scene] + NARRATION_FADE) {
                if (g_narration_frame + 1 >= starts[g_narration_scene + 1]) {
                    g_narration_scene++;
                }
            } else if (g_narration_frame >= starts[g_narration_scene]) {
                sheets = g_narration_file->picture_1_sheets;
                for (i = 0; i < NARRATION_STRIPS; sheets++, i++) {
                    draw.header = *sheets;
                    draw.table = cells;
                    draw.x = i * NARRATION_STRIP_WIDTH;
                    draw.sprt_out = ctx->prims.sprt;
                    ctx->prims.sprt = sprite_Draw(&draw);
                    SetDrawMode(ctx->prims.mode, 1, 0,
                                sprite_GetTPage(draw.header, abr), 0);
                    AddPrim(g_disp_ot + draw.ot_index, ctx->prims.mode);
                    ctx->prims.mode++;
                }
            }
            break;
        case NARRATION_BOTH:
            if (g_narration_frame + 1 >=
                ends[g_narration_scene] + NARRATION_FADE) {
                if (g_narration_frame + 1 >= starts[g_narration_scene + 1]) {
                    g_narration_scene++;
                }
                break;
            }
            sheets = g_narration_file->right_sheets;
            for (i = 0, x = NARRATION_RIGHT_X; i < 2;
                 x += NARRATION_STRIP_WIDTH, sheets++, i++) {
                draw.header = *sheets;
                draw.x = x;
                draw.table = cells;
                draw.sprt_out = ctx->prims.sprt;
                ctx->prims.sprt = sprite_Draw(&draw);
                SetDrawMode(ctx->prims.mode, 1, 0,
                            sprite_GetTPage(draw.header, abr), 0);
                AddPrim(g_disp_ot + draw.ot_index, ctx->prims.mode);
                ctx->prims.mode++;
            }
            /* fall through */
        case NARRATION_LEFT:
            if (g_narration_frame + 1 >= starts[g_narration_scene + 1] &&
                g_narration_scene == NARRATION_LEFT) {
                g_narration_scene = NARRATION_BOTH;
            }
            /* While the right picture comes in the left one stays lit. */
            if (g_narration_frame < ends[NARRATION_BOTH] &&
                g_narration_frame >= starts[NARRATION_BOTH]) {
                draw.has_color = 0;
            }
            sheets = g_narration_file->left_sheets;
            for (i = 0, x = NARRATION_LEFT_X; i < 2; x += NARRATION_STRIP_WIDTH,
                sheets++, i++) {
                draw.header = *sheets;
                draw.x = x;
                draw.table = cells;
                draw.sprt_out = ctx->prims.sprt;
                ctx->prims.sprt = sprite_Draw(&draw);
                SetDrawMode(ctx->prims.mode, 1, 0,
                            sprite_GetTPage(draw.header, abr), 0);
                AddPrim(g_disp_ot + draw.ot_index, ctx->prims.mode);
                ctx->prims.mode++;
            }
            break;
        case NARRATION_SWAP:
            rect = g_narration_picture_rect;
            for (i = 0; i < NARRATION_STRIPS; i++) {
                /* FAKE: g_narration_file->picture_2_pixels[i], by address */
                pixels = &g_narration_file->picture_2_pixels[i];
                LoadImage(&rect, (u32 *)(*pixels + NARRATION_TIM_PIXELS));
                DrawSync(0);
                rect.x += NARRATION_STRIP_TEXELS;
            }
            g_narration_scene++;
            break;
        case NARRATION_PICTURE_2:
            if (g_narration_frame + 1 <
                ends[g_narration_scene] + NARRATION_FADE) {
                sheets = g_narration_file->picture_2_sheets;
                for (i = 0; i < NARRATION_STRIPS; sheets++, i++) {
                    draw.header = *sheets;
                    draw.table = cells;
                    draw.x = i * NARRATION_STRIP_WIDTH;
                    draw.sprt_out = ctx->prims.sprt;
                    ctx->prims.sprt = sprite_Draw(&draw);
                    SetDrawMode(ctx->prims.mode, 1, 0,
                                sprite_GetTPage(draw.header, abr), 0);
                    AddPrim(g_disp_ot + draw.ot_index, ctx->prims.mode);
                    ctx->prims.mode++;
                }
            }
            break;
        }
    }

    draw.ot_index = NARRATION_LAYER_TEXT;
    draw.y = 0;
    draw.x = 0;
    sheets = g_narration_file->line_sheets;
    for (i = 0; i < NARRATION_LINES; i++) {
        /* FAKE: &g_narration_file->lines[i], summed as integers */
        line = (NarrationLine *)(i * 4 + (s32)g_narration_file->lines);
        if (g_narration_frame < line->off + NARRATION_FADE &&
            g_narration_frame >= line->on) {
            if (g_narration_frame < line->on + NARRATION_FADE) {
                draw.has_color = 1;
                level = ((g_narration_frame - line->on) *
                         NARRATION_TEXT_FADE_LEVEL) /
                        NARRATION_FADE;
                draw.semi = 1;
                draw.col_r = draw.col_g = draw.col_b = level;
            } else if (g_narration_frame >= line->off &&
                       g_narration_frame < line->off + NARRATION_FADE) {
                draw.has_color = 1;
                level = ((line->off + NARRATION_FADE - g_narration_frame) *
                         NARRATION_TEXT_FADE_LEVEL) /
                        NARRATION_FADE;
                draw.semi = 1;
                draw.col_r = draw.col_g = draw.col_b = level;
            } else {
                draw.has_color = 1;
                draw.semi = 1;
                draw.col_r = draw.col_g = draw.col_b = NARRATION_TEXT_LEVEL;
            }
            draw.header = sheets[i];
            draw.table = draw.header->cells;
            draw.sprt_out = ctx->prims.sprt;
            ctx->prims.sprt = sprite_Draw(&draw);
            SetDrawMode(
                ctx->prims.mode, 1, 0, sprite_GetTPage(draw.header, abr), 0);
            AddPrim(g_disp_ot + draw.ot_index, ctx->prims.mode);
            ctx->prims.mode++;
        }
    }
}

/* Reads NAR.BIN in behind the two frames' primitive pools at `memory`, and
 * starts the narration at its first picture. */
s32 narration_Load(s32 memory) {
    NarrationFile *file;
    s32 free_ram;

    ClearOTagR(g_disp_ot, DISP_OT_SIZE);
    file = (NarrationFile *)(memory + 2 * sizeof(PrimPool));
    g_narration_pools = (PrimPool *)memory;
    g_narration_file = file;
    sprite_LoadFile(NARRATION_FILE, &file->head);
    free_ram = narration_RelocateFile(file);
    sprite_InitPools(free_ram, g_narration_pools);
    g_narration_pool_frame = 0;
    g_narration_frame = 0;
    g_narration_scene = NARRATION_PICTURE_1;
    return 1;
}

/* A frame of the narration: draws it into this frame's pool and counts the
 * frame. Returns 1 when the last scene is over, or at once if the cross
 * button is pressed. */
s32 narration_Update(s32 pressed) {
    MenuDrawCtx ctx;
    PrimPool *pool;
    g_narration_pool_frame++;
    pool = narration_GetPrimPool(g_narration_pool_frame & 1);
    ctx.prims.ft4 = pool->ft4;
    ctx.prims.g4 = pool->g4;
    ctx.prims.sprt = pool->sprt;
    ctx.prims.tile = pool->tile;
    ctx.prims.mode = pool->mode;
    ctx.prims.area = pool->area;
    ctx.prims.offset = pool->offset;
    if (g_narration_frame > 0 && (pressed & PADRdown)) {
        return 1;
    }
    narration_Draw(&ctx);
    g_narration_frame++;
    return g_narration_frame >= g_narration_file->scene_ends[NARRATION_END];
}

/* Where a strip of the second picture goes in video memory: the first
 * strip's place in the screen's texture page. */
static RECT g_narration_picture_rect = {
    VRAM_PAGE_X, 0, NARRATION_STRIP_TEXELS, 184};
