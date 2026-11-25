/* The native port's window: the stage select's kinds of stage laid over
 * the game's picture (window.h; docs/PLAYING.md, "Kinds of stage";
 * include/port_stage_choice.h). On a stage page it is a panel under the
 * scroll, a column for each choice the page makes -- the kind of the
 * stage under the cursor (Classic or Remastered), its time of day, the
 * fight's music, and the custom arenas where there are any -- each its
 * value over the button that changes it (Square, L1 and R1, L2 and R2,
 * Select). With the arenas' page up it is a page of the stage select's
 * own: the game's page goes on drawing its title, its frame and its
 * scroll and leaves out its map and the stages' names
 * (port_StagePageCovers), and here the arenas are where those were --
 * the one under the cursor's picture in the map's frame (its file's
 * preview, or a card of its name where it has none), six names on the
 * scroll in three rows of two as the stages' are, dark and the cursor's
 * white with a dark edge, the page under them where the map's number
 * is -- and the panel under the scroll says what Cross, Triangle or
 * Select, and L1 and R1 do. It is drawn at the window's own resolution in
 * the port's typeface, the same over the remastered picture and over the
 * console's (F2), and none of it is in the game's video memory. Everything
 * it shows it reads of the game's state (src/port/stage_choice.c,
 * src/port/fight_music.c), which the game's step alone writes; the
 * presses' sounds are the game's own, played by that step. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "menu.h"
#include "port_arenas.h"
#include "port_fight_music.h"
#include "port_match.h"
#include "port_png.h"
#include "port_stage_choice.h"

/* The game's screen, whose places the page's are given in: 640 by 240,
 * shown 4:3, so that a unit across is half a unit down. */
#define GAME_W 640
#define GAME_H 240

/* The colours, 0xAARRGGBB, the settings menu's; the panel's lets a little
 * of the game's picture through. */
#define COLOR_STRIP 0xE00E121EU
#define COLOR_EDGE 0xFF6A5530U
#define COLOR_GOLD 0xFFE6C877U
#define COLOR_TEXT 0xFFEDEDF2U
#define COLOR_DIM 0xFF8E96AAU
#define COLOR_CARD 0xFF1A1E2AU
#define COLOR_CAP 0xFF2C3346U
#define COLOR_CAP_EDGE 0xFF56607CU
/* the scroll's lettering, measured from the stage pages: the names in a
 * dark ink, the cursor's white with a dark edge */
#define COLOR_INK 0xFF212119U
#define COLOR_INK_EDGE 0xFF16160EU
/* the pad's own colours for its symbols */
#define COLOR_SQUARE 0xFFE59AC6U
#define COLOR_CROSS 0xFF8FB8EAU
#define COLOR_TRIANGLE 0xFF5FCDA8U
#define COLOR_CIRCLE 0xFFE8737AU

/* The panel under the stage pages' scroll, whose foot is at 207, and the
 * arenas' page, from the map's frame to the panel's foot, in the game's
 * places. */
#define STRIP_X 40
#define STRIP_Y 209
#define STRIP_W 560
#define STRIP_H 31
#define PAGE_X 40
#define PAGE_Y 40
#define PAGE_W 560
#define PAGE_H 200

/* The panel's columns: across the panel less this much at each end, a
 * value's baseline, the buttons' baseline, and the room a column leaves
 * at its sides. */
#define STRIP_INSET 6
#define VALUE_BASE 222
#define KEYS_BASE 235
#define COLUMN_PAD 8

/* The stage pages' map, whose frame the arena's picture fills: the
 * game's window for it (select.c, STAGES_WINDOW_...), 352 by 74 of its
 * places, shown about 2.4 to 1. */
#define MAP_X 144
#define MAP_Y 40
#define MAP_W 352
#define MAP_H 74
/* The names on the scroll: the middles of its two columns, the first
 * row's baseline and their pitch, the room a name has across, and the
 * baseline of the map's number under them, all measured from the stage
 * pages' own. */
#define NAME_LEFT 211
#define NAME_RIGHT 427
#define NAME_FIRST 140
#define NAME_PITCH 20
#define NAME_ROOM 196
#define NAME_PAGE_BASE 196
/* the scroll's arrows to the page before and the next, where there is
 * more than one: their points, how far back they reach, their top and
 * foot, and their colour, the stage pages' */
#define ARROW_LEFT 88
#define ARROW_RIGHT 552
#define ARROW_DEPTH 8
#define ARROW_TOP 159
#define ARROW_FOOT 168
#define COLOR_ARROW 0xFF6B635AU
#define PAGE_PLACES PORT_STAGE_PAGE_PLACES
#define PAGE_COLS 2

/* The letters' sizes, in the game's lines: one for each kind of text.
 * A value too wide for its column is made smaller, down to the least, and
 * a name too wide for its place on the scroll likewise. */
#define SIZE_TITLE 15
#define SIZE_VALUE 11
#define SIZE_VALUE_LEAST 7
#define SIZE_NAME 11
#define SIZE_NAME_LEAST 7
#define SIZE_LABEL 8
#define SIZE_CAP 6

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
    s32 time;
    s32 count;
    s32 music; /* the row of music, and its rule's (port_fight_music.h) */
    s32 music_rule;
    s32 stage; /* the stage under the cursor, for its own piece */
    s32 match; /* the match rule's column (port_PageMatchKey) */
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

static PortTextStyle port_PageStyle(s32 face, s32 lines, u32 color) {
    PortTextStyle style;

    memset(&style, 0, sizeof(style));
    style.face = face;
    style.size = port_PageSize(lines);
    style.color = color;
    return style;
}

/* Text at `x` and `baseline`, pixels of the page. */
static void port_PageTextAt(
    s32 x, s32 baseline, const char *text, const PortTextStyle *style) {
    port_TextDraw(
        g_page.pixels, g_page.box.w, g_page.box.h, x, baseline, text, style);
}

static void port_PageText(s32 x, s32 y, s32 lines, const char *text, u32 color,
                          s32 is_centred, s32 is_right) {
    PortTextStyle style = port_PageStyle(PORT_FACE_TEXT, lines, color);
    s32 px = port_PageX(x);

    if (is_centred) {
        px -= port_TextMeasure(text, &style) / 2;
    } else if (is_right) {
        px -= port_TextMeasure(text, &style);
    }
    port_PageTextAt(px, port_PageY(y), text, &style);
}

/* A rectangle in pixels of the page. */
static void port_PageFill(s32 x, s32 y, s32 w, s32 h, u32 color) {
    port_FillRect(g_page.pixels, g_page.box.w, g_page.box.h, x, y, w, h, color);
}

static void port_PageRect(s32 x, s32 y, s32 w, s32 h, u32 color) {
    s32 x0 = port_PageX(x);
    s32 y0 = port_PageY(y);

    port_PageFill(
        x0, y0, port_PageX(x + w) - x0, port_PageY(y + h) - y0, color);
}

/* A line a pixel of the window wide, or two on a large one. */
static s32 port_PageLine(void) { return g_page.area_h >= 960 ? 2 : 1; }

/* ---- the buttons ---- */

/* A square root good to far less than a pixel, for the symbols' edges
 * (nothing of the window's links libm). */
static float port_PageRoot(float value) {
    float root = value > 1.0f ? value : 1.0f;
    s32 i;

    if (value <= 0.0f) {
        return 0.0f;
    }
    for (i = 0; i < 16; i++) {
        root = 0.5f * (root + value / root);
    }
    return root;
}

/* How far (px, py) is from the segment (ax, ay)-(bx, by). */
static float port_PageToSegment(
    float px, float py, float ax, float ay, float bx, float by) {
    float dx = bx - ax;
    float dy = by - ay;
    float t = ((px - ax) * dx + (py - ay) * dy) / (dx * dx + dy * dy);
    float ex;
    float ey;

    t = t < 0.0f ? 0.0f : t > 1.0f ? 1.0f : t;
    ex = px - (ax + t * dx);
    ey = py - (ay + t * dy);
    return port_PageRoot(ex * ex + ey * ey);
}

enum { SYMBOL_SQUARE, SYMBOL_CROSS, SYMBOL_TRIANGLE, SYMBOL_CIRCLE, SYMBOLS };

/* How far a pixel's middle (x, y), from the symbol's middle, is from the
 * symbol's stroke, `r` pixels out. */
static float port_PageSymbolDistance(s32 symbol, float x, float y, float r) {
    float d;
    float d1;
    float d2;

    if (symbol == SYMBOL_SQUARE) {
        float ax = x < 0.0f ? -x : x;
        float ay = y < 0.0f ? -y : y;

        d = (ax > ay ? ax : ay) - r * 0.8f;
        return d < 0.0f ? -d : d;
    }
    if (symbol == SYMBOL_CIRCLE) {
        d = port_PageRoot(x * x + y * y) - r * 0.85f;
        return d < 0.0f ? -d : d;
    }
    if (symbol == SYMBOL_CROSS) {
        r *= 0.8f;
        d = port_PageToSegment(x, y, -r, -r, r, r);
        d1 = port_PageToSegment(x, y, -r, r, r, -r);
        return d1 < d ? d1 : d;
    }
    /* a triangle on its base, its middle a little low */
    d = port_PageToSegment(x, y, 0.0f, -r, r, r * 0.72f);
    d1 = port_PageToSegment(x, y, r, r * 0.72f, -r, r * 0.72f);
    d2 = port_PageToSegment(x, y, -r, r * 0.72f, 0.0f, -r);
    d = d1 < d ? d1 : d;
    return d2 < d ? d2 : d;
}

/* One of the pad's symbols round (cx, cy), `r` pixels out, its stroke
 * `stroke` pixels wide and its edges smoothed. */
static void port_PageSymbol(
    s32 symbol, s32 cx, s32 cy, s32 r, float stroke, u32 color) {
    s32 i;
    s32 j;

    for (j = cy - r - 2; j <= cy + r + 2; j++) {
        for (i = cx - r - 2; i <= cx + r + 2; i++) {
            float d = port_PageSymbolDistance(symbol, (float)(i - cx) + 0.5f,
                                              (float)(j - cy) + 0.5f, (float)r);
            float cover = stroke * 0.5f + 0.5f - d;
            u32 alpha;

            if (cover <= 0.0f) {
                continue;
            }
            alpha = (u32)((cover > 1.0f ? 1.0f : cover) * (float)(color >> 24));
            port_PageFill(i, j, 1, 1, (alpha << 24) | (color & 0xFFFFFFU));
        }
    }
}

/* A button's cap at `x`, the line's baseline at `baseline` (pixels of the
 * page): a dark key with the button's name on it, or the pad's symbol.
 * `name` is the name, or "#square", "#cross", "#triangle", "#circle".
 * Drawn if
 * `draw`; its width in pixels either way. */
static s32 port_PageCap(s32 x, s32 baseline, const char *name, s32 draw) {
    static const u32 colors[SYMBOLS] = {
        COLOR_SQUARE, COLOR_CROSS, COLOR_TRIANGLE, COLOR_CIRCLE};
    PortTextStyle label = port_PageStyle(PORT_FACE_TEXT, SIZE_LABEL, 0);
    PortTextStyle style = port_PageStyle(PORT_FACE_BOLD, SIZE_CAP, COLOR_TEXT);
    s32 ascent = port_TextAscent(&label);
    s32 pad = port_PageSize(SIZE_LABEL) / 5 + 1;
    s32 h = ascent + 2 * pad;
    s32 top = baseline - ascent - pad;
    s32 line = port_PageLine();
    s32 symbol = -1;
    s32 w;

    if (strcmp(name, "#square") == 0) {
        symbol = SYMBOL_SQUARE;
    } else if (strcmp(name, "#cross") == 0) {
        symbol = SYMBOL_CROSS;
    } else if (strcmp(name, "#triangle") == 0) {
        symbol = SYMBOL_TRIANGLE;
    } else if (strcmp(name, "#circle") == 0) {
        symbol = SYMBOL_CIRCLE;
    }
    w = symbol >= 0 ? h : port_TextMeasure(name, &style) + 2 * pad + 2 * line;
    if (!draw) {
        return w;
    }
    /* the key, its corners cut by a line's width */
    port_PageFill(x + line, top, w - 2 * line, h, COLOR_CAP_EDGE);
    port_PageFill(x, top + line, w, h - 2 * line, COLOR_CAP_EDGE);
    port_PageFill(x + line, top + line, w - 2 * line, h - 2 * line, COLOR_CAP);
    if (symbol >= 0) {
        float stroke = (float)h / 9.0f;

        port_PageSymbol(symbol, x + w / 2, top + h / 2, h * 3 / 10,
                        stroke < 1.2f ? 1.2f : stroke, colors[symbol]);
    } else {
        port_PageTextAt(x + (w - port_TextMeasure(name, &style)) / 2,
                        top + (h + port_TextAscent(&style)) / 2, name, &style);
    }
    return w;
}

/* What buttons do: their caps (`between`, a word or NULL, between them),
 * then the words; from `x` (pixels), drawn if `draw`. Its width. */
static s32 port_PageKeys(
    s32 x, s32 baseline, const char * const *caps, s32 count,
    const char *between, const char *words, s32 draw) {
    PortTextStyle style = port_PageStyle(PORT_FACE_TEXT, SIZE_LABEL, COLOR_DIM);
    s32 gap = port_PageSize(SIZE_LABEL) / 3;
    s32 at = x;
    s32 i;

    for (i = 0; i < count; i++) {
        if (i > 0 && between != NULL) {
            if (draw) {
                port_PageTextAt(at, baseline, between, &style);
            }
            at += port_TextMeasure(between, &style) + gap;
        } else if (i > 0) {
            at -= gap / 2;
        }
        at += port_PageCap(at, baseline, caps[i], draw) + gap;
    }
    if (draw) {
        port_PageTextAt(at, baseline, words, &style);
    }
    return at + port_TextMeasure(words, &style) - x;
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

/* The picture into its frame, filling it: the middle of the picture as
 * wide as the frame is shown (its top and bottom, or its sides, left
 * out), made that size by a mean of the pixels each covers (it is made
 * smaller) or the nearest (larger). The frame is in the game's places,
 * shown 4:3, so a place down is two across. */
static void port_PagePicture(
    const Preview *preview, s32 x, s32 y, s32 w, s32 h) {
    s32 x0 = port_PageX(x);
    s32 y0 = port_PageY(y);
    s32 pw = port_PageX(x + w) - x0;
    s32 ph = port_PageY(y + h) - y0;
    /* the part of the picture shown: as wide as the frame */
    s32 cw = preview->w;
    s32 ch = preview->h;
    s32 cx;
    s32 cy;
    s32 i;
    s32 j;

    if (pw <= 0 || ph <= 0) {
        return;
    }
    if ((s64)preview->w * ph > (s64)preview->h * pw) {
        cw = (s32)((s64)preview->h * pw / ph);
    } else {
        ch = (s32)((s64)preview->w * ph / pw);
    }
    cw = cw < 1 ? 1 : cw;
    ch = ch < 1 ? 1 : ch;
    cx = (preview->w - cw) / 2;
    cy = (preview->h - ch) / 2;
    for (j = 0; j < ph; j++) {
        s32 sy0 = cy + j * ch / ph;
        s32 sy1 = cy + (j + 1) * ch / ph;

        if (y0 + j < 0 || y0 + j >= g_page.box.h) {
            continue;
        }
        sy1 = sy1 > sy0 ? sy1 : sy0 + 1;
        for (i = 0; i < pw; i++) {
            s32 sx0 = cx + i * cw / pw;
            s32 sx1 = cx + (i + 1) * cw / pw;
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

/* The times of day as the panel names them (PORT_TIME_...). */
static const char * const g_page_times[PORT_TIME_COUNT] = {
    "Stage's own", "Dawn", "Day", "Dusk", "Night"};

/* A column of the panel: its value over its buttons and their words. */
typedef struct {
    const char *value;
    u32 color;
    const char *caps[2];
    s32 cap_count;
    const char *between;
    const char *words;
} Column;

/* A column's value at `x`, made smaller where it is wider than `room`
 * pixels. */
static void port_PageValue(s32 x, const char *text, u32 color, s32 room) {
    s32 lines = SIZE_VALUE;
    PortTextStyle style = port_PageStyle(PORT_FACE_TEXT, lines, color);

    while (lines > SIZE_VALUE_LEAST && port_TextMeasure(text, &style) > room) {
        lines--;
        style = port_PageStyle(PORT_FACE_TEXT, lines, color);
    }
    port_PageTextAt(x, port_PageY(VALUE_BASE), text, &style);
}

/* The panel under the scroll, its `n` columns side by side. */
static void port_PageColumns(const Column *columns, s32 n) {
    s32 left = port_PageX(STRIP_X + STRIP_INSET);
    s32 right = port_PageX(STRIP_X + STRIP_W - STRIP_INSET);
    s32 pad = port_PageX(STRIP_X + COLUMN_PAD) - port_PageX(STRIP_X);
    s32 i;

    port_PageRect(STRIP_X, STRIP_Y, STRIP_W, STRIP_H, COLOR_STRIP);
    port_PageFill(port_PageX(STRIP_X), port_PageY(STRIP_Y),
                  port_PageX(STRIP_X + STRIP_W) - port_PageX(STRIP_X),
                  port_PageLine(), COLOR_EDGE);
    for (i = 0; i < n; i++) {
        s32 x0 = left + (right - left) * i / n;
        s32 x1 = left + (right - left) * (i + 1) / n;

        if (i > 0) {
            port_PageFill(
                x0, port_PageY(STRIP_Y + 5), port_PageLine(),
                port_PageY(STRIP_Y + STRIP_H - 4) - port_PageY(STRIP_Y + 5),
                COLOR_EDGE);
        }
        port_PageValue(
            x0 + pad, columns[i].value, columns[i].color, x1 - x0 - 2 * pad);
        port_PageKeys(
            x0 + pad, port_PageY(KEYS_BASE), columns[i].caps,
            columns[i].cap_count, columns[i].between, columns[i].words, TRUE);
    }
}

static void port_PageStrip(const PortStageChoice *choice, s32 count) {
    static char arenas[24];
    static char match[24];
    Column columns[5];
    s32 n = 0;
    s32 row = g_port_fight_music.row;

    memset(columns, 0, sizeof(columns));
    /* the kind */
    columns[n].value =
        choice->cursor_kind == PORT_STAGE_CLASSIC ? "Classic" : "Remastered";
    columns[n].color =
        choice->cursor_kind == PORT_STAGE_CLASSIC ? COLOR_TEXT : COLOR_GOLD;
    columns[n].caps[0] = "#square";
    columns[n].cap_count = 1;
    columns[n].words = "Style";
    n++;
    /* the time of day, which only a dressed stage's picture shows */
    if (choice->cursor_kind == PORT_STAGE_CLASSIC) {
        columns[n].value = "The game's light";
        columns[n].color = COLOR_DIM;
    } else {
        columns[n].value = g_page_times[choice->cursor_time < PORT_TIME_COUNT
                                            ? choice->cursor_time
                                            : PORT_TIME_OWN];
        columns[n].color =
            choice->cursor_time == PORT_TIME_OWN ? COLOR_TEXT : COLOR_GOLD;
    }
    columns[n].caps[0] = "L1";
    columns[n].caps[1] = "R1";
    columns[n].cap_count = 2;
    columns[n].words = "Time";
    n++;
    /* the fight's music (port_fight_music.h): a mod's piece by its key */
    columns[n].value = port_FightMusicRowName(row);
    columns[n].color =
        row == PORT_FIGHT_MUSIC_ROW_OWN ? COLOR_TEXT : COLOR_GOLD;
    /* the stage's own piece of a mod's (port_MusicStagePiece) */
    if (row == PORT_FIGHT_MUSIC_ROW_OWN &&
        port_MusicStagePiece(choice->cursor_stage) != NULL) {
        columns[n].value = "Stage's own: mod";
        columns[n].color = COLOR_GOLD;
    }
    columns[n].caps[0] = "L2";
    columns[n].caps[1] = "R2";
    columns[n].cap_count = 2;
    columns[n].words = "Music";
    n++;
    /* the match rule (port_match.h), where the mode plays matches */
    if (g_port_match.page_on) {
        port_MatchRuleName(match, sizeof(match), g_port_match.page_rule,
                           g_port_match.page_rule == PORT_MATCH_CREW
                               ? g_port_match.page_crew
                               : g_port_match.page_count);
        columns[n].value = match;
        columns[n].color = g_port_match.page_rule == PORT_MATCH_SINGLE
                               ? COLOR_TEXT
                               : COLOR_GOLD;
        columns[n].caps[0] = "#circle";
        columns[n].caps[1] = "START";
        columns[n].cap_count = 2;
        columns[n].words = "Match";
        n++;
    }
    if (count > 0) {
        snprintf(arenas, sizeof(arenas), "%d custom", (int)count);
        columns[n].value = arenas;
        columns[n].color = COLOR_TEXT;
        columns[n].caps[0] = "SELECT";
        columns[n].cap_count = 1;
        columns[n].words = "Arenas";
        n++;
    }
    port_PageColumns(columns, n);
}

/* A name on the scroll, centred on the game's `x` at the baseline `y`, in
 * the stage pages' lettering: dark ink, or white with a dark edge under
 * the cursor; made smaller where it is wider than its place. */
static void port_PageName(s32 x, s32 y, const char *name, s32 is_cursor) {
    s32 lines = SIZE_NAME;
    s32 room = port_PageX(x + NAME_ROOM / 2) - port_PageX(x - NAME_ROOM / 2);
    PortTextStyle style;

    for (;;) {
        style = port_PageStyle(
            PORT_FACE_BOLD, lines, is_cursor ? 0xFFFFFFFFU : COLOR_INK);
        if (is_cursor) {
            style.outline = COLOR_INK_EDGE;
            style.outline_px = port_PageSize(lines) / 16 + 1;
        }
        if (lines <= SIZE_NAME_LEAST ||
            port_TextMeasure(name, &style) <= room) {
            break;
        }
        lines--;
    }
    port_PageTextAt(port_PageX(x) - port_TextMeasure(name, &style) / 2,
                    port_PageY(y), name, &style);
}

/* An arrow on the scroll, its point at the game's `tip` and pointing
 * away from `back`, as the stage pages' arrows to another map are. */
static void port_PageArrow(s32 tip, s32 back) {
    s32 x0 = port_PageX(tip < back ? tip : back);
    s32 x1 = port_PageX(tip < back ? back : tip);
    s32 y0 = port_PageY(ARROW_TOP);
    s32 y1 = port_PageY(ARROW_FOOT);
    s32 half = (y1 - y0) / 2;
    s32 j;

    for (j = 0; j <= y1 - y0; j++) {
        /* how far from the point this row reaches, out of the depth */
        s32 off = j <= half ? j : y1 - y0 - j;
        s32 w = half > 0 ? (x1 - x0) * off / half : x1 - x0;

        if (tip < back) {
            port_PageFill(x1 - w, y0 + j, w, 1, COLOR_ARROW);
        } else {
            port_PageFill(x0, y0 + j, w, 1, COLOR_ARROW);
        }
    }
}

static void port_PageArenas(const PortStageChoice *choice, s32 count) {
    static char where[32];
    static char pages[24];
    const PortArena *chosen = port_ArenaAt(choice->page_cursor);
    const Preview *preview;
    s32 page = choice->page_cursor / PAGE_PLACES;
    s32 page_count = (count + PAGE_PLACES - 1) / PAGE_PLACES;
    Column columns[3];
    s32 n = 0;
    s32 i;

    /* the one under the cursor in the map's frame */
    preview = port_PagePreview(choice->page_cursor);
    if (preview != NULL) {
        port_PagePicture(preview, MAP_X, MAP_Y, MAP_W, MAP_H);
    } else {
        /* a card of its name where it brings no picture */
        port_PageRect(MAP_X, MAP_Y, MAP_W, MAP_H, COLOR_CARD);
        if (chosen != NULL) {
            port_PageText(MAP_X + MAP_W / 2, MAP_Y + MAP_H / 2 + 5, SIZE_TITLE,
                          chosen->name, COLOR_GOLD, TRUE, FALSE);
        }
    }
    /* the page's arenas on the scroll, as the stages' names are */
    for (i = 0; i < PAGE_PLACES; i++) {
        s32 at = page * PAGE_PLACES + i;
        const PortArena *arena = port_ArenaAt(at);

        if (at >= count || arena == NULL) {
            break;
        }
        port_PageName(i % PAGE_COLS == 0 ? NAME_LEFT : NAME_RIGHT,
                      NAME_FIRST + (i / PAGE_COLS) * NAME_PITCH, arena->name,
                      at == choice->page_cursor);
    }
    /* where the map's number is: which page of them; and the arrows */
    if (page_count > 1) {
        port_PageArrow(ARROW_LEFT, ARROW_LEFT + ARROW_DEPTH);
        port_PageArrow(ARROW_RIGHT, ARROW_RIGHT - ARROW_DEPTH);
        snprintf(where, sizeof(where), "( Arenas %d )", (int)page + 1);
    } else {
        snprintf(where, sizeof(where), "( Arenas )");
    }
    port_PageName((NAME_LEFT + NAME_RIGHT) / 2, NAME_PAGE_BASE, where, FALSE);

    /* the panel: what the buttons do */
    memset(columns, 0, sizeof(columns));
    columns[n].value = chosen != NULL ? chosen->name : "";
    columns[n].color = COLOR_GOLD;
    columns[n].caps[0] = "#cross";
    columns[n].cap_count = 1;
    columns[n].words = "Fight here";
    n++;
    if (page_count > 1) {
        snprintf(pages, sizeof(pages), "Page %d of %d", (int)page + 1,
                 (int)page_count);
        columns[n].value = pages;
        columns[n].color = COLOR_TEXT;
        columns[n].caps[0] = "L1";
        columns[n].caps[1] = "R1";
        columns[n].cap_count = 2;
        columns[n].words = "Pages";
        n++;
    }
    columns[n].value = "The stages";
    columns[n].color = COLOR_TEXT;
    columns[n].caps[0] = "#triangle";
    columns[n].caps[1] = "SELECT";
    columns[n].cap_count = 2;
    columns[n].between = "or";
    columns[n].words = "Back";
    n++;
    port_PageColumns(columns, n);
}

/* ---- what the window asks ---- */

/* What the panel's match column shows, as one number. */
static s32 port_PageMatchKey(void) {
    return g_port_match.page_on << 16 | g_port_match.page_rule << 8 |
           g_port_match.page_count | g_port_match.page_crew << 4;
}

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
        g_page.music_rule != port_GetFightMusicRule() ||
        g_page.time != choice->cursor_time ||
        g_page.stage != choice->cursor_stage ||
        g_page.match != port_PageMatchKey()) {
        g_page.match = port_PageMatchKey();
        g_page.music = g_port_fight_music.row;
        g_page.music_rule = port_GetFightMusicRule();
        g_page.area_w = area_w;
        g_page.area_h = area_h;
        g_page.open = open;
        g_page.cursor = choice->page_cursor;
        g_page.top = choice->page_top;
        g_page.kind = choice->cursor_kind;
        g_page.time = choice->cursor_time;
        g_page.stage = choice->cursor_stage;
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
        }
        g_page.drawn = TRUE;
    }
    *pixels = g_page.pixels;
    *box = g_page.box;
    return TRUE;
}
