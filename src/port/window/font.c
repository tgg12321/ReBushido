/* The native port's window: text in a font of its own, at any size, for
 * the settings menu (menu.h; docs/PLAYING.md, "The settings menu"). The
 * font is Marcellus (src/port/fonts/, the SIL Open Font Licence), built
 * into the program by tools/port_sdl_build.py; its letters are drawn by
 * stb_truetype (public domain), at the size the window asks for, so the
 * menu is as crisp in a window of 4K as in one of 960 by 720. Only the
 * build with a window has this file. */
#include <stdlib.h>
#include <string.h>

/* The compiler's own arithmetic for stb_truetype: include/math.h is the
 * game's, which the window's files see first. */
#define STBTT_ifloor(x) ((int)__builtin_floor(x))
#define STBTT_iceil(x) ((int)__builtin_ceil(x))
#define STBTT_sqrt(x) __builtin_sqrt(x)
#define STBTT_pow(x, y) __builtin_pow(x, y)
#define STBTT_fmod(x, y) __builtin_fmod(x, y)
#define STBTT_cos(x) __builtin_cos(x)
#define STBTT_acos(x) __builtin_acos(x)
#define STBTT_fabs(x) __builtin_fabs(x)

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#pragma GCC diagnostic ignored "-Wsign-compare"
#pragma GCC diagnostic ignored "-Wunused-parameter"
#pragma GCC diagnostic ignored "-Wimplicit-fallthrough"
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"
#define STB_TRUETYPE_IMPLEMENTATION
#define STBTT_STATIC
#include "stb_truetype.h"
#pragma GCC diagnostic pop

#include "menu.h"

/* The letters a menu has: the printable ones of ASCII. */
#define FIRST_LETTER 32
#define LAST_LETTER 126
#define LETTERS (LAST_LETTER - FIRST_LETTER + 1)
#define CHANNEL_MAX 255
#define ALPHA_SHIFT 24
#define RED_SHIFT 16
#define GREEN_SHIFT 8

typedef struct {
    u8 *coverage; /* w by h, 0 to 255 */
    s32 w;
    s32 h;
    s32 x; /* from the pen to the bitmap's corner */
    s32 y; /* ... from the baseline */
    s32 advance;
} PortGlyph;

static struct {
    s32 is_started;
    stbtt_fontinfo info;
    s32 size; /* the pixels from the top of the tallest letter to the
                 bottom of the deepest, at which the letters are made */
    float scale;
    s32 ascent;
    PortGlyph glyphs[LETTERS];
} g_font;

s32 port_FontStart(void) {
    if (g_font.is_started) {
        return TRUE;
    }
    if (!stbtt_InitFont(&g_font.info, g_port_menu_font,
                        stbtt_GetFontOffsetForIndex(g_port_menu_font, 0))) {
        return FALSE;
    }
    g_font.is_started = TRUE;
    return TRUE;
}

static void port_FontForget(void) {
    s32 i;

    for (i = 0; i < LETTERS; i++) {
        stbtt_FreeBitmap(g_font.glyphs[i].coverage, NULL);
        g_font.glyphs[i].coverage = NULL;
    }
}

void port_FontSetSize(s32 pixels) {
    s32 ascent = 0;
    s32 descent = 0;
    s32 gap = 0;
    s32 i;

    if (!g_font.is_started || pixels == g_font.size || pixels <= 0) {
        return;
    }
    port_FontForget();
    g_font.size = pixels;
    g_font.scale = stbtt_ScaleForPixelHeight(&g_font.info, (float)pixels);
    stbtt_GetFontVMetrics(&g_font.info, &ascent, &descent, &gap);
    g_font.ascent = (s32)(ascent * g_font.scale + 0.5f);
    for (i = 0; i < LETTERS; i++) {
        PortGlyph *glyph = &g_font.glyphs[i];
        s32 advance = 0;
        s32 bearing = 0;

        glyph->coverage = stbtt_GetCodepointBitmap(
            &g_font.info, g_font.scale, g_font.scale, FIRST_LETTER + i,
            &glyph->w, &glyph->h, &glyph->x, &glyph->y);
        stbtt_GetCodepointHMetrics(
            &g_font.info, FIRST_LETTER + i, &advance, &bearing);
        glyph->advance = (s32)(advance * g_font.scale + 0.5f);
    }
}

s32 port_FontAscent(void) { return g_font.ascent; }

static const PortGlyph *port_FontGlyph(char letter) {
    s32 at = (u8)letter;

    if (at < FIRST_LETTER || at > LAST_LETTER) {
        at = '?';
    }
    return &g_font.glyphs[at - FIRST_LETTER];
}

s32 port_FontMeasure(const char *text) {
    s32 width = 0;

    if (!g_font.is_started || g_font.size == 0) {
        return 0;
    }
    for (; *text != 0; text++) {
        width += port_FontGlyph(*text)->advance;
    }
    return width;
}

/* `color` (0xAARRGGBB) laid over a pixel `amount` (0 to 255) of the way. */
static u32 port_FontMix(u32 under, u32 color, u32 amount) {
    u32 alpha = (color >> ALPHA_SHIFT) * amount / CHANNEL_MAX;
    u32 keep = CHANNEL_MAX - alpha;
    u32 under_alpha = under >> ALPHA_SHIFT;
    u32 out_alpha = alpha + under_alpha * keep / CHANNEL_MAX;
    u32 out = out_alpha << ALPHA_SHIFT;
    s32 shift;

    if (out_alpha == 0) {
        return 0;
    }
    for (shift = 0; shift <= RED_SHIFT; shift += GREEN_SHIFT) {
        u32 top = (color >> shift) & CHANNEL_MAX;
        u32 bottom = (under >> shift) & CHANNEL_MAX;
        u32 mixed = (top * alpha + bottom * under_alpha * keep / CHANNEL_MAX) /
                    out_alpha;

        out |= (mixed > CHANNEL_MAX ? CHANNEL_MAX : mixed) << shift;
    }
    return out;
}

void port_FontDraw(u32 *pixels, s32 w, s32 h, s32 x, s32 baseline,
                   const char *text, u32 color) {
    if (!g_font.is_started || g_font.size == 0) {
        return;
    }
    for (; *text != 0; text++) {
        const PortGlyph *glyph = port_FontGlyph(*text);
        s32 i;
        s32 j;

        for (j = 0; j < glyph->h; j++) {
            s32 py = baseline + glyph->y + j;

            if (py < 0 || py >= h) {
                continue;
            }
            for (i = 0; i < glyph->w; i++) {
                s32 px = x + glyph->x + i;
                u32 amount = glyph->coverage[j * glyph->w + i];

                if (px < 0 || px >= w || amount == 0) {
                    continue;
                }
                pixels[py * w + px] =
                    port_FontMix(pixels[py * w + px], color, amount);
            }
        }
        x += glyph->advance;
    }
}

void port_FillRect(
    u32 *pixels, s32 w, s32 h, s32 x, s32 y, s32 rw, s32 rh, u32 color) {
    s32 i;
    s32 j;

    for (j = y < 0 ? 0 : y; j < y + rh && j < h; j++) {
        for (i = x < 0 ? 0 : x; i < x + rw && i < w; i++) {
            pixels[j * w + i] =
                port_FontMix(pixels[j * w + i], color, CHANNEL_MAX);
        }
    }
}
