/* The native port's window: the stage select's kinds of stage laid over
 * the game's picture (window.h; docs/PLAYING.md, "Kinds of stage";
 * include/port_stage_choice.h). On a stage page it says the kind of the
 * stage under the cursor, Classic or Remastered Classic, and what Square
 * and Select do; with the arenas' page up it is that page: the arenas by
 * their names, the one under the cursor's picture (its file's preview,
 * or a card of its name where it has none), and what Cross and Triangle
 * do. It is drawn at the window's own resolution in the settings menu's
 * font, the same over the remastered picture and over the console's
 * (F2), and none of it is in the game's video memory: what the game has
 * on the screen is the console's, and the play tests that hold the
 * port's picture to the emulator's see that. Everything it shows it
 * reads of the game's state (src/port/stage_choice.c), which the game's
 * step alone writes. */
#include <stdlib.h>
#include <string.h>

#include "menu.h"
#include "port_arenas.h"
#include "port_fight_music.h"
#include "port_png.h"
#include "port_stage_choice.h"

/* The game's screen, whose places the page's are given in: 640 by 240,
 * shown 4:3, so that a unit across is half a unit down. */
#define GAME_W 640
#define GAME_H 240

/* The colours, 0xAARRGGBB, the settings menu's. */
#define COLOR_PANEL 0xFF0E121EU
#define COLOR_EDGE 0xFF6A5530U
#define COLOR_GOLD 0xFFE6C877U
#define COLOR_TEXT 0xFFEDEDF2U
#define COLOR_DIM 0xFF8E96AAU
#define COLOR_BAR 0xFF26304AU
#define COLOR_CARD 0xFF1A1E2AU

/* The strip under the stage pages' scroll, and the page over them, in
 * the game's places. */
#define STRIP_X 40
#define STRIP_Y 204
#define STRIP_W 560
#define STRIP_H 36
#define PAGE_X 40
#define PAGE_Y 62
#define PAGE_W 560
#define PAGE_H 176

/* The page's rows: so many shown, their place, their pitch. */
#define ROWS 5
#define ROW_X 64
#define ROW_FIRST 108
#define ROW_PITCH 20
#define ROW_BAR_X 54
#define ROW_BAR_W 256
#define ROW_BAR_ABOVE 14
#define ROW_BAR_H 19
/* The picture's frame: 4:3 as the screen shows it. */
#define PICTURE_X 322
#define PICTURE_Y 94
#define PICTURE_W 256
#define PICTURE_H 96

/* The letters' sizes, in the game's lines. */
#define SIZE_TITLE 15
#define SIZE_KIND 12
#define SIZE_ROW 11
#define SIZE_HELP 9

typedef struct {
    s32 tried;
    s32 w;
    s32 h;
    u8 *rgba;
} Preview;

static struct {
    /* what the pixels were drawn for */
    s32 drawn;
    s32 area_w;
    s32 area_h;
    s32 open;
    s32 cursor;
    s32 top;
    s32 kind;
    s32 count;
    s32 music; /* the row of music, and its rule's (port_fight_music.h) */
    s32 music_rule;
    /* the pixels, `box` of the game's area */
    u32 *pixels;
    u32 room;
    PortRect box;
    Preview previews[PORT_ARENAS_MAX];
    s32 font_ok;
} g_page;

/* ---- places ---- */

static s32 port_PageX(s32 x) {
    return x * g_page.area_w / GAME_W - g_page.box.x;
}

static s32 port_PageY(s32 y) {
    return y * g_page.area_h / GAME_H - g_page.box.y;
}

static s32 port_PageSize(s32 lines) {
    s32 size = lines * g_page.area_h / GAME_H;

    return size < 6 ? 6 : size;
}

static void port_PageText(s32 x, s32 y, s32 lines, const char *text, u32 color,
                          s32 is_centred, s32 is_right) {
    s32 px;

    port_FontSetSize(port_PageSize(lines));
    px = port_PageX(x);
    if (is_centred) {
        px -= port_FontMeasure(text) / 2;
    } else if (is_right) {
        px -= port_FontMeasure(text);
    }
    port_FontDraw(g_page.pixels, g_page.box.w, g_page.box.h, px, port_PageY(y),
                  text, color);
}

static void port_PageRect(s32 x, s32 y, s32 w, s32 h, u32 color) {
    s32 x0 = port_PageX(x);
    s32 y0 = port_PageY(y);

    port_FillRect(g_page.pixels, g_page.box.w, g_page.box.h, x0, y0,
                  port_PageX(x + w) - x0, port_PageY(y + h) - y0, color);
}

/* A frame a pixel of the window wide, or two on a large one. */
static void port_PageFrame(s32 x, s32 y, s32 w, s32 h, u32 color) {
    s32 x0 = port_PageX(x);
    s32 y0 = port_PageY(y);
    s32 x1 = port_PageX(x + w);
    s32 y1 = port_PageY(y + h);
    s32 line = g_page.area_h >= 960 ? 2 : 1;

    port_FillRect(g_page.pixels, g_page.box.w, g_page.box.h, x0, y0, x1 - x0,
                  line, color);
    port_FillRect(g_page.pixels, g_page.box.w, g_page.box.h, x0, y1 - line,
                  x1 - x0, line, color);
    port_FillRect(g_page.pixels, g_page.box.w, g_page.box.h, x0, y0, line,
                  y1 - y0, color);
    port_FillRect(g_page.pixels, g_page.box.w, g_page.box.h, x1 - line, y0,
                  line, y1 - y0, color);
}

/* ---- an arena's picture ---- */

static const Preview *port_PagePreview(s32 index) {
    Preview *preview = &g_page.previews[index];
    const PortArena *arena = port_ArenaAt(index);
    PortPngInfo info;
    char problem[96];
    u8 *work;

    if (preview->tried || arena == NULL) {
        return preview->rgba != NULL ? preview : NULL;
    }
    preview->tried = TRUE;
    if (arena->preview_bytes == 0 || arena->file == NULL) {
        return NULL;
    }
    if (!port_PngMeasure(arena->file + arena->preview_at, arena->preview_bytes,
                         &info, problem, sizeof(problem))) {
        return NULL;
    }
    work = malloc(info.work_bytes);
    preview->rgba = malloc((size_t)info.w * info.h * 4);
    if (work == NULL || preview->rgba == NULL ||
        !port_PngDecode(arena->file + arena->preview_at, arena->preview_bytes,
                        work, preview->rgba, problem, sizeof(problem))) {
        free(preview->rgba);
        preview->rgba = NULL;
    }
    free(work);
    preview->w = info.w;
    preview->h = info.h;
    return preview->rgba != NULL ? preview : NULL;
}

/* The picture into its frame, made that size by a mean of the pixels
 * each covers (it is made smaller) or the nearest (larger). */
static void port_PagePicture(
    const Preview *preview, s32 x, s32 y, s32 w, s32 h) {
    s32 x0 = port_PageX(x);
    s32 y0 = port_PageY(y);
    s32 pw = port_PageX(x + w) - x0;
    s32 ph = port_PageY(y + h) - y0;
    s32 i;
    s32 j;

    for (j = 0; j < ph; j++) {
        s32 sy0 = j * preview->h / ph;
        s32 sy1 = (j + 1) * preview->h / ph;

        if (y0 + j < 0 || y0 + j >= g_page.box.h) {
            continue;
        }
        sy1 = sy1 > sy0 ? sy1 : sy0 + 1;
        for (i = 0; i < pw; i++) {
            s32 sx0 = i * preview->w / pw;
            s32 sx1 = (i + 1) * preview->w / pw;
            u32 sum[3] = {0, 0, 0};
            u32 n = 0;
            s32 sx;
            s32 sy;

            if (x0 + i < 0 || x0 + i >= g_page.box.w) {
                continue;
            }
            sx1 = sx1 > sx0 ? sx1 : sx0 + 1;
            for (sy = sy0; sy < sy1; sy++) {
                for (sx = sx0; sx < sx1; sx++) {
                    const u8 *p =
                        preview->rgba + ((size_t)sy * preview->w + sx) * 4;

                    sum[0] += p[0];
                    sum[1] += p[1];
                    sum[2] += p[2];
                    n++;
                }
            }
            g_page.pixels[(y0 + j) * g_page.box.w + x0 + i] =
                0xFF000000U | (sum[0] / n) << 16 | (sum[1] / n) << 8 |
                (sum[2] / n);
        }
    }
}

/* ---- the two looks ---- */

static void port_PageStrip(const PortStageChoice *choice, s32 count) {
    port_PageText(
        64, 222, SIZE_KIND,
        choice->cursor_kind == PORT_STAGE_CLASSIC ? "Classic"
                                                  : "Remastered Classic",
        choice->cursor_kind == PORT_STAGE_CLASSIC ? COLOR_TEXT : COLOR_GOLD,
        FALSE, FALSE);
    port_PageText(64, 235, SIZE_HELP, "Square: Classic / Remastered", COLOR_DIM,
                  FALSE, FALSE);
    if (count > 0) {
        port_PageText(576, 222, SIZE_HELP, "Select: custom arenas", COLOR_DIM,
                      FALSE, TRUE);
    }
}

/* The row of music, in the strip's middle between the kind's lines and
 * the arenas' (port_fight_music.h): the piece the fight starts with (a
 * mod's by its key), made smaller where it is wider than the gap, and
 * what L1 and R1 do. */
#define MUSIC_X 334
#define MUSIC_W 136
#define SIZE_MUSIC_LEAST 6

static void port_PageMusic(void) {
    s32 row = g_port_fight_music.row;
    const char *name = port_FightMusicRowName(row);
    s32 room = port_PageX(MUSIC_X + MUSIC_W / 2) - port_PageX(MUSIC_X);
    s32 lines = SIZE_ROW;

    port_PageText(MUSIC_X, 213, SIZE_HELP, "Music", COLOR_DIM, TRUE, FALSE);
    port_FontSetSize(port_PageSize(lines));
    while (lines > SIZE_MUSIC_LEAST && port_FontMeasure(name) > 2 * room) {
        lines--;
        port_FontSetSize(port_PageSize(lines));
    }
    port_PageText(
        MUSIC_X, 225, lines, name,
        row == PORT_FIGHT_MUSIC_ROW_OWN ? COLOR_TEXT : COLOR_GOLD, TRUE, FALSE);
    port_PageText(
        MUSIC_X, 235, SIZE_HELP, "L1 / R1: music", COLOR_DIM, TRUE, FALSE);
}

static void port_PageArenas(const PortStageChoice *choice, s32 count) {
    const PortArena *chosen = port_ArenaAt(choice->page_cursor);
    const Preview *preview;
    s32 i;

    port_PageRect(PAGE_X, PAGE_Y, PAGE_W, PAGE_H, COLOR_PANEL);
    port_PageFrame(PAGE_X, PAGE_Y, PAGE_W, PAGE_H, COLOR_EDGE);
    port_PageText(
        GAME_W / 2, 80, SIZE_TITLE, "Custom Arenas", COLOR_GOLD, TRUE, FALSE);
    port_PageRect(
        ROW_BAR_X, 86, PAGE_W - 2 * (ROW_BAR_X - PAGE_X), 1, COLOR_EDGE);
    for (i = 0; i < ROWS && choice->page_top + i < count; i++) {
        const PortArena *arena = port_ArenaAt(choice->page_top + i);
        s32 y = ROW_FIRST + i * ROW_PITCH;
        s32 is_cursor = choice->page_top + i == choice->page_cursor;

        if (is_cursor) {
            port_PageRect(
                ROW_BAR_X, y - ROW_BAR_ABOVE, ROW_BAR_W, ROW_BAR_H, COLOR_BAR);
            port_PageFrame(
                ROW_BAR_X, y - ROW_BAR_ABOVE, ROW_BAR_W, ROW_BAR_H, COLOR_GOLD);
        }
        port_PageText(ROW_X, y, SIZE_ROW, arena->name,
                      is_cursor ? COLOR_TEXT : COLOR_DIM, FALSE, FALSE);
    }
    if (choice->page_top > 0) {
        port_PageText(ROW_BAR_X + ROW_BAR_W / 2, ROW_FIRST - ROW_PITCH + 4,
                      SIZE_HELP, "more above", COLOR_DIM, TRUE, FALSE);
    }
    if (choice->page_top + ROWS < count) {
        port_PageText(ROW_BAR_X + ROW_BAR_W / 2, ROW_FIRST + ROWS * ROW_PITCH,
                      SIZE_HELP, "more below", COLOR_DIM, TRUE, FALSE);
    }
    preview = port_PagePreview(choice->page_cursor);
    if (preview != NULL) {
        port_PagePicture(preview, PICTURE_X, PICTURE_Y, PICTURE_W, PICTURE_H);
    } else {
        /* a card of its name where it brings no picture */
        port_PageRect(PICTURE_X, PICTURE_Y, PICTURE_W, PICTURE_H, COLOR_CARD);
        if (chosen != NULL) {
            port_PageText(
                PICTURE_X + PICTURE_W / 2, PICTURE_Y + PICTURE_H / 2 + 5,
                SIZE_TITLE, chosen->name, COLOR_GOLD, TRUE, FALSE);
        }
    }
    port_PageFrame(PICTURE_X, PICTURE_Y, PICTURE_W, PICTURE_H, COLOR_EDGE);
    if (chosen != NULL) {
        port_PageText(PICTURE_X + PICTURE_W / 2, PICTURE_Y + PICTURE_H + 14,
                      SIZE_ROW, chosen->name, COLOR_GOLD, TRUE, FALSE);
    }
    port_PageText(GAME_W / 2, 232, SIZE_HELP,
                  "Cross: fight here      Triangle or Select: back", COLOR_DIM,
                  TRUE, FALSE);
}

/* ---- what the window asks ---- */

s32 port_StagePagePixels(
    s32 area_w, s32 area_h, const u32 **pixels, PortRect *box) {
    const PortStageChoice *choice = &g_port_stage_choice;
    s32 count = port_ArenaCount();
    s32 open = choice->page_open && count > 0;
    u32 need;

    if (!port_IsOnStagePages() || area_w <= 0 || area_h <= 0) {
        return FALSE;
    }
    if (!g_page.font_ok) {
        g_page.font_ok = port_FontStart();
        if (!g_page.font_ok) {
            return FALSE;
        }
    }
    if (!g_page.drawn || g_page.area_w != area_w || g_page.area_h != area_h ||
        g_page.open != open || g_page.cursor != choice->page_cursor ||
        g_page.top != choice->page_top || g_page.kind != choice->cursor_kind ||
        g_page.count != count || g_page.music != g_port_fight_music.row ||
        g_page.music_rule != port_GetFightMusicRule()) {
        g_page.music = g_port_fight_music.row;
        g_page.music_rule = port_GetFightMusicRule();
        g_page.area_w = area_w;
        g_page.area_h = area_h;
        g_page.open = open;
        g_page.cursor = choice->page_cursor;
        g_page.top = choice->page_top;
        g_page.kind = choice->cursor_kind;
        g_page.count = count;
        g_page.box.x = (open ? PAGE_X : STRIP_X) * area_w / GAME_W;
        g_page.box.y = (open ? PAGE_Y : STRIP_Y) * area_h / GAME_H;
        g_page.box.w =
            (open ? PAGE_X + PAGE_W : STRIP_X + STRIP_W) * area_w / GAME_W -
            g_page.box.x;
        g_page.box.h =
            (open ? PAGE_Y + PAGE_H : STRIP_Y + STRIP_H) * area_h / GAME_H -
            g_page.box.y;
        need = (u32)(g_page.box.w * g_page.box.h);
        if (need > g_page.room) {
            free(g_page.pixels);
            g_page.pixels = malloc((size_t)need * sizeof(u32));
            g_page.room = g_page.pixels != NULL ? need : 0;
        }
        if (g_page.pixels == NULL || need == 0) {
            g_page.drawn = FALSE;
            return FALSE;
        }
        memset(g_page.pixels, 0, (size_t)need * sizeof(u32));
        if (open) {
            port_PageArenas(choice, count);
        } else {
            port_PageStrip(choice, count);
            port_PageMusic();
        }
        g_page.drawn = TRUE;
    }
    *pixels = g_page.pixels;
    *box = g_page.box;
    return TRUE;
}
