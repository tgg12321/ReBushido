/* The native port's window: ReBushido's top menu laid over the game's own
 * page (window.h; include/port_top_menu.h; docs/PLAYING.md, "The top
 * menu"). The game draws the page as it draws its own -- the backdrop,
 * the title, the line under each row with the cursor's glowing, the
 * picture and caption of the mode under the cursor -- and this draws what
 * the game has no picture for: the names of the rows, each where the
 * game's own name would be (lit, dark where it cannot be chosen, bright
 * under the cursor), and for the three rows the game has no caption for
 * (Singleplayer, Online Play, Back) two lines of its own in the caption's
 * place. It is drawn at the window's own resolution, the same over the
 * remastered picture and the console's (F2), and none of it is in the
 * game's video memory. Everything it shows it reads of the game's state
 * (gamestate.c, port_ReadTopMenu).
 *
 * Its letters are drawn by port_TopPageText alone: the font and the look
 * of the page's text are that one function's. */
#include <stdlib.h>
#include <string.h>

#include "menu.h"

/* The game's screen, whose places the page's are given in: 640 by 240,
 * shown 4:3, so that a unit across is half a unit down. */
#define GAME_W 640
#define GAME_H 240

/* What the page lays its pixels over, in the game's places: the rows on
 * the left and the caption on the right. */
#define BOX_X 96
#define BOX_Y 56
#define BOX_W 486
#define BOX_H 172

/* The rows' names: right-aligned at ROW_RIGHT as the game's are, their
 * baselines on the game's rows (menu_pages.c, TOP_ROWS_Y and TOP_ROW_PITCH,
 * the last at TOP_LAST_ROW_Y), and SIZE_ROW lines high. */
#define ROW_RIGHT 236
#define ROW_FIRST 74
#define ROW_PITCH 24
#define ROW_LAST 218
#define SIZE_ROW 15
/* The caption: its name over the picture, and a line under it, both
 * centred on the panel, as the game's are. */
#define CAPTION_X 448
#define CAPTION_NAME_Y 76
#define CAPTION_LINE_Y 157
#define SIZE_NAME 14
#define SIZE_LINE 9

/* The colours, 0xAARRGGBB: a row the cursor is on, one that can be
 * chosen (the game's gold), one that cannot, the caption's white, and
 * the shadow under every letter. */
#define COLOR_CURSOR 0xFFFFEC9CU
#define COLOR_ROW 0xFFD8B860U
#define COLOR_ROW_OFF 0xFF606060U
#define COLOR_CAPTION 0xFFF2F2F2U
#define COLOR_SHADOW 0xC0101838U

enum {
    ALIGN_LEFT = 0,
    ALIGN_CENTRE = 1,
    ALIGN_RIGHT = 2,
};

static struct {
    /* what the pixels were drawn for */
    s32 drawn;
    s32 area_w;
    s32 area_h;
    PortTopView view;
    /* the pixels, `box` of the game's area */
    u32 *pixels;
    u32 room;
    PortRect box;
    s32 font_ok;
} g_top;

/* ---- places ---- */

static s32 port_TopX(s32 x) { return x * g_top.area_w / GAME_W - g_top.box.x; }

static s32 port_TopY(s32 y) { return y * g_top.area_h / GAME_H - g_top.box.y; }

/* ---- the text ---- */

/* A line of the page's text, `lines` of the game's lines high, its
 * baseline at the game's `y`, aligned on the game's `x`: the one function
 * that draws letters here, in the settings menu's font with a shadow down
 * and to the right, as the game's lettering has. It maps onto the font
 * work's port_TextDraw: a PortTextStyle of PORT_FACE_TEXT (a row's name,
 * the caption's line) or PORT_FACE_TITLE (the caption's name), `size`
 * pixels, `color`, and a shadow of `shadow` pixels in COLOR_SHADOW. */
static void port_TopPageText(
    s32 x, s32 y, s32 lines, const char *text, u32 color, s32 align) {
    s32 size = lines * g_top.area_h / GAME_H;
    s32 shadow;
    s32 px;

    if (size < 6) {
        size = 6;
    }
    shadow = size / 14 > 1 ? size / 14 : 1;
    port_FontSetSize(size);
    px = port_TopX(x);
    if (align == ALIGN_CENTRE) {
        px -= port_FontMeasure(text) / 2;
    } else if (align == ALIGN_RIGHT) {
        px -= port_FontMeasure(text);
    }
    port_FontDraw(g_top.pixels, g_top.box.w, g_top.box.h, px + shadow,
                  port_TopY(y) + shadow, text, COLOR_SHADOW);
    port_FontDraw(
        g_top.pixels, g_top.box.w, g_top.box.h, px, port_TopY(y), text, color);
}

/* ---- the page ---- */

static void port_TopPageDraw(const PortTopView *view) {
    s32 i;

    for (i = 0; i < PORT_TOP_VIEW_ROWS; i++) {
        u32 color = i == view->cursor ? COLOR_CURSOR
                    : view->is_on[i]  ? COLOR_ROW
                                      : COLOR_ROW_OFF;
        s32 y =
            i == PORT_TOP_VIEW_ROWS - 1 ? ROW_LAST : ROW_FIRST + i * ROW_PITCH;

        if (view->names[i] != NULL) {
            port_TopPageText(
                ROW_RIGHT, y, SIZE_ROW, view->names[i], color, ALIGN_RIGHT);
        }
    }
    if (view->caption[0] != NULL) {
        port_TopPageText(CAPTION_X, CAPTION_NAME_Y, SIZE_NAME, view->caption[0],
                         COLOR_CAPTION, ALIGN_CENTRE);
    }
    if (view->caption[1] != NULL) {
        port_TopPageText(CAPTION_X, CAPTION_LINE_Y, SIZE_LINE, view->caption[1],
                         COLOR_CAPTION, ALIGN_CENTRE);
    }
}

/* ---- what the window asks ---- */

static s32 port_TopViewsDiffer(const PortTopView *a, const PortTopView *b) {
    s32 i;

    if (a->page != b->page || a->cursor != b->cursor ||
        a->caption[0] != b->caption[0] || a->caption[1] != b->caption[1]) {
        return TRUE;
    }
    for (i = 0; i < PORT_TOP_VIEW_ROWS; i++) {
        if (a->names[i] != b->names[i] || a->is_on[i] != b->is_on[i]) {
            return TRUE;
        }
    }
    return FALSE;
}

s32 port_TopPagePixels(
    s32 area_w, s32 area_h, const u32 **pixels, PortRect *box) {
    PortTopView view;
    u32 need;

    port_ReadTopMenu(&view);
    if (!view.is_up || area_w <= 0 || area_h <= 0) {
        return FALSE;
    }
    if (!g_top.font_ok) {
        g_top.font_ok = port_FontStart();
        if (!g_top.font_ok) {
            return FALSE;
        }
    }
    if (!g_top.drawn || g_top.area_w != area_w || g_top.area_h != area_h ||
        port_TopViewsDiffer(&g_top.view, &view)) {
        g_top.area_w = area_w;
        g_top.area_h = area_h;
        g_top.view = view;
        g_top.box.x = BOX_X * area_w / GAME_W;
        g_top.box.y = BOX_Y * area_h / GAME_H;
        g_top.box.w = (BOX_X + BOX_W) * area_w / GAME_W - g_top.box.x;
        g_top.box.h = (BOX_Y + BOX_H) * area_h / GAME_H - g_top.box.y;
        need = (u32)(g_top.box.w * g_top.box.h);
        if (need > g_top.room) {
            free(g_top.pixels);
            g_top.pixels = malloc((size_t)need * sizeof(u32));
            g_top.room = g_top.pixels != NULL ? need : 0;
        }
        if (g_top.pixels == NULL || need == 0) {
            g_top.drawn = FALSE;
            return FALSE;
        }
        memset(g_top.pixels, 0, (size_t)need * sizeof(u32));
        port_TopPageDraw(&view);
        g_top.drawn = TRUE;
    }
    *pixels = g_top.pixels;
    *box = g_top.box;
    return TRUE;
}
