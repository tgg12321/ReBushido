/* Scrolling textures (tex.h): a rectangle of video memory copied each frame
 * to where the stage's meshes look for it, starting a little further down
 * every frame, so that the texture seems to flow. Compiled with small
 * data. */
#include "common.h"
#include "bb2.h"

static TexScroll g_tex_scrolls[1];

#define TEX_SCROLL_SIZES 3 /* the texture, and it at a half and a quarter */

/* Sets scroll `scroll_index` up from a preset, starting at its first line.
 * There is room for one scroll. */
void tex_InitScroll(s32 scroll_preset_index, s32 scroll_index) {
    s32 i;
    u16 *src;
    s16 *dst;
    TexScroll *scroll;
    if (scroll_index > 0) {
        sys_Trap();
    }
    scroll = &g_tex_scrolls[scroll_index];
    scroll->phase = 0;
    src = g_tex_scroll_presets[scroll_preset_index];
    dst = scroll->ctl;
    for (i = 0; i < TEX_SCROLL_CONTROLS; i++) {
        *dst++ = *src++;
    }
}

/* Queues the copies for this frame, behind everything else in the scene's
 * ordering table so that they are done before anything is drawn: for each
 * size, the part of the source below the line `phase` to the top of the
 * destination and the part above it underneath. Then moves the line on. */
void tex_QueueScroll(s32 scroll_index) {
    RECT rect;
    s16 *scroll_controls;
    s32 x;
    s32 y;
    s32 source_x_offset;
    s32 source_y_offset;
    s32 dx;
    s32 dy;
    s32 dest_x_offset;
    s32 dest_y_offset;
    s32 w;
    s32 scroll_height;
    s32 i;
    TexScroll *scroll_record;
    DR_MOVE *move;
    s32 scroll_phase;

    scroll_record = &g_tex_scrolls[scroll_index];
    scroll_phase = scroll_record->phase;
    scroll_controls = scroll_record->ctl;
    move = scroll_record->move[g_disp_frame_count & 1];
    /* a place in video memory is its texture page's corner and an offset in
     * the page; only the offset is halved from one size to the next */
    x = scroll_controls[TEX_SCROLL_SRC_X] & ~(TPAGE_WIDTH - 1);
    y = scroll_controls[TEX_SCROLL_SRC_Y] & ~(TPAGE_HEIGHT - 1);
    source_x_offset = scroll_controls[TEX_SCROLL_SRC_X] % TPAGE_WIDTH;
    source_y_offset = scroll_controls[TEX_SCROLL_SRC_Y] % TPAGE_HEIGHT;
    w = scroll_controls[TEX_SCROLL_WIDTH];
    scroll_height = scroll_controls[TEX_SCROLL_HEIGHT];
    dx = scroll_controls[TEX_SCROLL_DEST_X] & ~(TPAGE_WIDTH - 1);
    dy = scroll_controls[TEX_SCROLL_DEST_Y] & ~(TPAGE_HEIGHT - 1);
    dest_x_offset = scroll_controls[TEX_SCROLL_DEST_X] % TPAGE_WIDTH;
    dest_y_offset = scroll_controls[TEX_SCROLL_DEST_Y] % TPAGE_HEIGHT;
    for (i = 0; i < TEX_SCROLL_SIZES; i++) {
        s32 dest_x = dx + dest_x_offset;
        s32 dest_y = dy + dest_y_offset;
        s32 sy = y + source_y_offset;
        s32 h1 = scroll_height - scroll_phase;
        /* FAKE: rect.h = scroll_phase below, with no h2 */
        s16 h2 = scroll_phase;
        rect.x = x + source_x_offset;
        rect.y = sy;
        rect.w = w;
        rect.h = h1;
        SetDrawMove(move, &rect, dest_x, dest_y + scroll_phase);
        addPrim(&g_render_ot[RENDER_OT_FARTHEST], move);
        move++;
        rect.y = sy + h1;
        rect.h = h2;
        SetDrawMove(move, &rect, dest_x, dest_y);
        addPrim(&g_render_ot[RENDER_OT_FARTHEST], move);
        move++;
        source_x_offset >>= 1;
        source_y_offset >>= 1;
        dest_x_offset >>= 1;
        dest_y_offset >>= 1;
        w >>= 1;
        scroll_height >>= 1;
        scroll_phase >>= 1;
    }
    scroll_record->phase += scroll_controls[TEX_SCROLL_SPEED];
    if (scroll_record->phase >= scroll_controls[TEX_SCROLL_HEIGHT]) {
        scroll_record->phase %= scroll_controls[TEX_SCROLL_HEIGHT];
    }
}
