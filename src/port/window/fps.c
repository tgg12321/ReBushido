/* The native port's window: the counter in its corner, as far as it is
 * arithmetic (window.h; docs/PORT.md, "The window"): how many a second,
 * the digits' shapes, the counter's text and its place. No SDL in it:
 * sdl_window.c draws what this makes over the picture it has already
 * handed SDL, never into the game's video memory. tests/port/window.c
 * runs it on made-up times. */
#include "common.h"
#include "window.h"

/* Samples a second: the clock's unit (window.h, "the PC's clock"). */
#define CLOCK_RATE 44100
#define TENTHS 10
#define PERCENT 100
/* The console's vertical blanks a second, in thousandths: 59.826. */
#define CONSOLE_RATE_THOUSANDTHS 59826
#define DECIMAL 10
/* The picture's lines, by which the counter's size is a whole number. */
#define PICTURE_LINES 240
#define MARGIN 2

/* A glyph's seven rows, the top first, five bits a row, the left
 * highest. */
typedef struct {
    char letter;
    u8 rows[PORT_GLYPH_H];
} PortGlyph;

static const PortGlyph g_port_glyphs[] = {
    {'0', {0x0E, 0x11, 0x13, 0x15, 0x19, 0x11, 0x0E}},
    {'1', {0x04, 0x0C, 0x04, 0x04, 0x04, 0x04, 0x0E}},
    {'2', {0x0E, 0x11, 0x01, 0x02, 0x04, 0x08, 0x1F}},
    {'3', {0x1E, 0x01, 0x01, 0x0E, 0x01, 0x01, 0x1E}},
    {'4', {0x02, 0x06, 0x0A, 0x12, 0x1F, 0x02, 0x02}},
    {'5', {0x1F, 0x10, 0x1E, 0x01, 0x01, 0x11, 0x0E}},
    {'6', {0x06, 0x08, 0x10, 0x1E, 0x11, 0x11, 0x0E}},
    {'7', {0x1F, 0x01, 0x02, 0x04, 0x08, 0x08, 0x08}},
    {'8', {0x0E, 0x11, 0x11, 0x0E, 0x11, 0x11, 0x0E}},
    {'9', {0x0E, 0x11, 0x11, 0x0F, 0x01, 0x02, 0x0C}},
    {'/', {0x01, 0x01, 0x02, 0x04, 0x08, 0x10, 0x10}},
    {'%', {0x19, 0x19, 0x02, 0x04, 0x08, 0x13, 0x13}},
    {'.', {0x00, 0x00, 0x00, 0x00, 0x00, 0x0C, 0x0C}},
    {'-', {0x00, 0x00, 0x00, 0x1F, 0x00, 0x00, 0x00}},
};

/* ---- how many a second ---- */

/* `events` more things happened, at `now`. The rate is worked out anew
 * each half second, over the time since it was last worked out. */
void port_CountFps(PortFpsMeter *meter, u32 now, u32 events) {
    u32 elapsed;

    if (!meter->is_started) {
        meter->is_started = 1;
        meter->window_start = now;
        meter->count = 0;
        meter->tenths = 0;
        return;
    }
    meter->count += events;
    elapsed = now - meter->window_start;
    if (elapsed >= PORT_FPS_WINDOW) {
        /* to the nearest tenth */
        meter->tenths =
            (meter->count * (CLOCK_RATE * TENTHS) + elapsed / 2) / elapsed;
        meter->window_start = now;
        meter->count = 0;
    }
}

/* ---- the text ---- */

static char *port_PutNumber(char *out, u32 value) {
    char digits[DECIMAL];
    s32 count = 0;

    do {
        digits[count++] = (char)('0' + value % DECIMAL);
        value /= DECIMAL;
    } while (value != 0 && count < DECIMAL);
    while (count > 0) {
        *out++ = digits[--count];
    }
    return out;
}

static char *port_PutTenths(char *out, u32 tenths) {
    out = port_PutNumber(out, tenths / TENTHS);
    *out++ = '.';
    *out++ = (char)('0' + tenths % TENTHS);
    return out;
}

/* `59.8/30.0 100%`: pictures shown a second, the game's own frames a
 * second, and the speed against the console's (window.h). */
void port_FormatFps(char *out, u32 shown_tenths, u32 game_tenths) {
    /* shown tenths are thousandths times 100; so a percent is this */
    u32 percent =
        (shown_tenths * PERCENT * PERCENT + CONSOLE_RATE_THOUSANDTHS / 2) /
        CONSOLE_RATE_THOUSANDTHS;

    out = port_PutTenths(out, shown_tenths);
    *out++ = '/';
    out = port_PutTenths(out, game_tenths);
    *out++ = ' ';
    out = port_PutNumber(out, percent);
    *out++ = '%';
    *out = 0;
}

/* ---- the digits ---- */

const u8 *port_FindGlyph(char letter) {
    u32 i;

    for (i = 0; i < sizeof(g_port_glyphs) / sizeof(g_port_glyphs[0]); i++) {
        if (g_port_glyphs[i].letter == letter) {
            return g_port_glyphs[i].rows;
        }
    }
    return NULL;
}

s32 port_MeasureText(const char *text) {
    s32 letters = 0;

    while (text[letters] != 0) {
        letters++;
    }
    return letters == 0 ? 0 : letters * (PORT_GLYPH_W + 1) - 1;
}

/* The text into pixels `w` by `h`, its top left at (`x`, `y`); what
 * falls outside is left out, and a letter with no shape is a space. */
void port_DrawText(
    u32 *pixels, s32 w, s32 h, s32 x, s32 y, const char *text, u32 color) {
    for (; *text != 0; text++, x += PORT_GLYPH_W + 1) {
        const u8 *rows = port_FindGlyph(*text);
        s32 row;
        s32 column;

        if (rows == NULL) {
            continue;
        }
        for (row = 0; row < PORT_GLYPH_H; row++) {
            for (column = 0; column < PORT_GLYPH_W; column++) {
                s32 at_x = x + column;
                s32 at_y = y + row;

                if ((rows[row] >> (PORT_GLYPH_W - 1 - column)) & 1 &&
                    at_x >= 0 && at_x < w && at_y >= 0 && at_y < h) {
                    pixels[at_y * w + at_x] = color;
                }
            }
        }
    }
}

/* ---- its place ---- */

/* A box `box_w` by `box_h` of the counter's own pixels, each a whole
 * number of the window's, in a corner of the picture's rectangle: as
 * many times as the picture's 240 lines are, and once at the least, so
 * it grows with the window and stays sharp (window.h). */
void port_PlaceCounter(
    const PortRect *picture, s32 box_w, s32 box_h, s32 corner, PortRect *out) {
    s32 times = picture->h / PICTURE_LINES;

    if (times < 1) {
        times = 1;
    }
    out->w = box_w * times;
    out->h = box_h * times;
    out->x =
        (corner == PORT_CORNER_TOP_RIGHT || corner == PORT_CORNER_BOTTOM_RIGHT)
            ? picture->x + picture->w - out->w - MARGIN * times
            : picture->x + MARGIN * times;
    out->y = (corner == PORT_CORNER_BOTTOM_LEFT ||
              corner == PORT_CORNER_BOTTOM_RIGHT)
                 ? picture->y + picture->h - out->h - MARGIN * times
                 : picture->y + MARGIN * times;
}
