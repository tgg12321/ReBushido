/* The native port's window: ReBushido's top menu laid over the game's own
 * page (window.h; include/port_top_menu.h; docs/PLAYING.md, "The main
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
#include "update.h"

/* The game's screen, whose places the page's are given in: 640 by 240,
 * shown 4:3, so that a unit across is half a unit down. */
#define GAME_W 640
#define GAME_H 240

/* What the page lays its pixels over, in the game's places: the title's
 * small line, the rows on the left and the caption on the right. */
#define BOX_X 60
#define BOX_Y 40
#define BOX_W 522
#define BOX_H 188

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
/* the updater's notice (update.c, port_UpdateNotice): two lines under
 * the caption, in its line's size */
#define NOTICE_FIRST_Y 196
#define NOTICE_SECOND_Y 208
#define NOTICE_BYTES 128
/* The title's small line under its big one, "MAIN MENU" where the game's
 * says "TOP MENU": its capitals spaced over the same span as the game's
 * (the first's middle and the last's), on its baseline, in its teal with
 * a dark edge, SIZE_SMALL lines high. */
#define SMALL_TEXT "MAIN MENU"
#define SMALL_FIRST_X 82
#define SMALL_LAST_X 264
#define SMALL_Y 52
#define SIZE_SMALL 8
#define COLOR_SMALL 0xFF297B94U
#define COLOR_SMALL_EDGE 0xFF081820U
/* The pause's and the after-fight menu's two rows (port_top_menu.h): the
 * screen's middle, where they are centred; how far below the y of the
 * game's sheet their baseline is; their height, and the most of the
 * panel's width they may take; the lit row's white, the others' laid
 * half over the panel, as the game's semi-transparent rows are, and the
 * shadow under them (tools/port_ui.py, PAUSE). */
#define PANEL_MIDDLE_X 320
#define PANEL_BASELINE 10
#define SIZE_PANEL_ROW 13
#define PANEL_ROW_MOST 236
#define COLOR_PANEL_ROW 0xFFFFFFFFU
#define COLOR_PANEL_ROW_DIM 0x80FFFFFFU
#define COLOR_PANEL_SHADOW 0xFF181818U

/* The colours, 0xAARRGGBB: a row the cursor is on, one that can be
 * chosen (the game's gold), one that cannot, the caption's white, and
 * the shadow under every letter. */
#define COLOR_CURSOR 0xFFFFEC9CU
#define COLOR_ROW 0xFFD8B860U
#define COLOR_ROW_OFF 0xFF606060U
#define COLOR_CAPTION 0xFFF2F2F2U
#define COLOR_SHADOW 0xC0101838U
#define COLOR_NOTICE 0xFFFFD27AU

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
    s32 notice_generation;
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
 * that draws letters here, in the port's typeface (font.c, port_TextDraw)
 * with a shadow down and to the right, as the game's lettering has: a
 * row's name in the bold face, the caption's name in the title's, its
 * line in the text's. */
static void port_TopPageText(
    s32 x, s32 y, s32 lines, const char *text, u32 color, s32 align) {
    PortTextStyle style;
    s32 size = lines * g_top.area_h / GAME_H;
    s32 px;

    if (size < 6) {
        size = 6;
    }
    memset(&style, 0, sizeof(style));
    style.face = lines == SIZE_NAME   ? PORT_FACE_TITLE
                 : lines == SIZE_LINE ? PORT_FACE_TEXT
                                      : PORT_FACE_BOLD;
    style.size = size;
    style.color = color;
    style.shadow = COLOR_SHADOW;
    style.shadow_px = size / 14 > 1 ? size / 14 : 1;
    px = port_TopX(x);
    if (align == ALIGN_CENTRE) {
        px -= port_TextMeasure(text, &style) / 2;
    } else if (align == ALIGN_RIGHT) {
        px -= port_TextMeasure(text, &style);
    }
    port_TextDraw(
        g_top.pixels, g_top.box.w, g_top.box.h, px, port_TopY(y), text, &style);
}

/* ---- the page ---- */

/* The title's small line, a capital at a time over the game's span. */
static void port_TopPageSmall(void) {
    PortTextStyle style;
    s32 count = (s32)strlen(SMALL_TEXT);
    s32 size = SIZE_SMALL * g_top.area_h / GAME_H;
    s32 i;

    memset(&style, 0, sizeof(style));
    style.face = PORT_FACE_BOLD;
    style.size = size < 6 ? 6 : size;
    style.color = COLOR_SMALL;
    style.outline = COLOR_SMALL_EDGE;
    style.outline_px = style.size / 12 > 1 ? style.size / 12 : 1;
    for (i = 0; i < count; i++) {
        char letter[2];
        s32 x = SMALL_FIRST_X + (SMALL_LAST_X - SMALL_FIRST_X) * i /
                                    (count > 1 ? count - 1 : 1);

        letter[0] = SMALL_TEXT[i];
        letter[1] = 0;
        if (letter[0] != ' ') {
            port_TextDraw(g_top.pixels, g_top.box.w, g_top.box.h,
                          port_TopX(x) - port_TextMeasure(letter, &style) / 2,
                          port_TopY(SMALL_Y), letter, &style);
        }
    }
}

static void port_TopPageDraw(const PortTopView *view) {
    char first[NOTICE_BYTES];
    char second[NOTICE_BYTES];
    s32 generation;
    s32 i;

    port_TopPageSmall();
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
    if (port_UpdateNotice(first, second, sizeof(first), &generation)) {
        port_TopPageText(CAPTION_X, NOTICE_FIRST_Y, SIZE_LINE, first,
                         COLOR_NOTICE, ALIGN_CENTRE);
        port_TopPageText(CAPTION_X, NOTICE_SECOND_Y, SIZE_LINE, second,
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
    char first[NOTICE_BYTES];
    char second[NOTICE_BYTES];
    s32 notice_generation;
    u32 need;

    port_ReadTopMenu(&view);
    port_UpdateNotice(first, second, sizeof(first), &notice_generation);
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
        port_TopViewsDiffer(&g_top.view, &view) ||
        g_top.notice_generation != notice_generation) {
        g_top.notice_generation = notice_generation;
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

/* ---- the pause's and the after-fight menu's renamed rows ---- */

static struct {
    s32 drawn;
    s32 area_w;
    s32 area_h;
    PortPanelRowsView view;
    u32 *pixels;
    u32 room;
    PortRect box;
} g_rows;

/* A row of the panel in the game's lettering's look, centred, as wide as
 * the panel lets it be. */
static void port_PanelRowText(s32 y, const char *text, s32 is_lit) {
    PortTextStyle style;
    s32 size = SIZE_PANEL_ROW * g_rows.area_h / GAME_H;
    s32 most = PANEL_ROW_MOST * g_rows.area_w / GAME_W;
    s32 width;

    memset(&style, 0, sizeof(style));
    style.face = PORT_FACE_BOLD;
    style.size = size < 6 ? 6 : size;
    style.shadow = COLOR_PANEL_SHADOW;
    style.shadow_px = style.size / 12 > 1 ? style.size / 12 : 1;
    width = port_TextMeasure(text, &style);
    if (width > most && width > 0) {
        style.size = style.size * most / width;
        width = port_TextMeasure(text, &style);
    }
    if (!is_lit) {
        /* the game's semi-transparent row: letters and shadow half over */
        style.shadow = (COLOR_PANEL_SHADOW & 0x00FFFFFFU) | 0x80000000U;
    }
    style.color = is_lit ? COLOR_PANEL_ROW : COLOR_PANEL_ROW_DIM;
    port_TextDraw(
        g_rows.pixels, g_rows.box.w, g_rows.box.h,
        PANEL_MIDDLE_X * g_rows.area_w / GAME_W - g_rows.box.x - width / 2,
        y * g_rows.area_h / GAME_H - g_rows.box.y, text, &style);
}

s32 port_PanelRowsPixels(
    s32 area_w, s32 area_h, const u32 **pixels, PortRect *box) {
    PortPanelRowsView view;
    u32 need;

    port_ReadPanelRows(&view);
    if (!view.is_up || area_w <= 0 || area_h <= 0) {
        return FALSE;
    }
    if (!g_top.font_ok) {
        g_top.font_ok = port_FontStart();
        if (!g_top.font_ok) {
            return FALSE;
        }
    }
    if (!g_rows.drawn || g_rows.area_w != area_w || g_rows.area_h != area_h ||
        memcmp(&g_rows.view, &view, sizeof(view)) != 0) {
        s32 top = view.y[0] < view.y[1] ? view.y[0] : view.y[1];
        s32 bottom = view.y[0] < view.y[1] ? view.y[1] : view.y[0];

        g_rows.area_w = area_w;
        g_rows.area_h = area_h;
        g_rows.view = view;
        g_rows.box.x =
            (PANEL_MIDDLE_X - PANEL_ROW_MOST / 2 - 8) * area_w / GAME_W;
        g_rows.box.w = (PANEL_ROW_MOST + 16) * area_w / GAME_W;
        g_rows.box.y = (top - 2) * area_h / GAME_H;
        g_rows.box.h = (bottom - top + SIZE_PANEL_ROW + 6) * area_h / GAME_H;
        if (g_rows.box.y < 0 || g_rows.box.y + g_rows.box.h > area_h) {
            return FALSE;
        }
        need = (u32)(g_rows.box.w * g_rows.box.h);
        if (need > g_rows.room) {
            free(g_rows.pixels);
            g_rows.pixels = malloc((size_t)need * sizeof(u32));
            g_rows.room = g_rows.pixels != NULL ? need : 0;
        }
        if (g_rows.pixels == NULL || need == 0) {
            g_rows.drawn = FALSE;
            return FALSE;
        }
        memset(g_rows.pixels, 0, (size_t)need * sizeof(u32));
        port_PanelRowText(view.y[0] + PANEL_BASELINE,
                          "Return to Character Select", view.cursor == 1);
        port_PanelRowText(view.y[1] + PANEL_BASELINE, "Return to Main Menu",
                          view.cursor == 2);
        g_rows.drawn = TRUE;
    }
    *pixels = g_rows.pixels;
    *box = g_rows.box;
    return TRUE;
}
