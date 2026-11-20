/* The native port's window: its lettering, in one typeface at any size
 * (menu.h; docs/PORT.md, "The port's lettering"). The typeface is Shippori
 * Mincho in three weights (src/port/fonts/, the SIL Open Font Licence),
 * built into the program by tools/port_sdl_build.py; its letters are drawn
 * from their outlines by stb_truetype (public domain) at the size asked
 * for, so text is as crisp in a window of 4K as in one of 960 by 720. The
 * settings menu, the netplay line, the stage select's page and the
 * counter draw through it, and the same faces letter the mods the port
 * ships (tools/port_lettering.py). Only the build with a window has this
 * file. */
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

/* The letters drawn: the printable ones of ASCII and of Latin-1 (text is
 * UTF-8); any other is a question mark. */
#define ASCII_FIRST 32
#define ASCII_LAST 126
#define LATIN_FIRST 160
#define LATIN_LAST 255
#define ASCII_COUNT (ASCII_LAST - ASCII_FIRST + 1)
#define LETTERS (ASCII_COUNT + LATIN_LAST - LATIN_FIRST + 1)
/* sizes kept made at once: the menu's text, its headings, a title, the
 * netplay line, the counter ... */
#define SIZES_KEPT 8
#define CHANNEL_MAX 255
#define ALPHA_SHIFT 24
#define RED_SHIFT 16
#define GREEN_SHIFT 8
#define UTF8_TWO 0xC0
#define UTF8_MORE 0x80
#define UTF8_BITS 0x3F
#define UTF8_TWO_BITS 0x1F
#define UTF8_SHIFT 6

typedef struct {
    u8 *coverage; /* w by h, 0 to 255 */
    s32 w;
    s32 h;
    s32 x; /* from the pen to the bitmap's corner */
    s32 y; /* ... from the baseline */
    float advance;
    s32 is_made;
} PortGlyph;

/* one face at one size, its letters made as they are first drawn */
typedef struct {
    s32 face;
    s32 size; /* 0: not in use */
    float scale;
    s32 ascent;
    u32 used; /* when it was last asked for */
    PortGlyph glyphs[LETTERS];
} PortFontSize;

static struct {
    s32 is_started;
    stbtt_fontinfo info[PORT_FACES];
    PortFontSize sizes[SIZES_KEPT];
    u32 clock;
    /* the face and size of port_FontSetSize */
    s32 size;
} g_font;

s32 port_FontStart(void) {
    const unsigned char *files[PORT_FACES];
    s32 i;

    if (g_font.is_started) {
        return TRUE;
    }
    files[PORT_FACE_TEXT] = g_port_font_text;
    files[PORT_FACE_BOLD] = g_port_font_bold;
    files[PORT_FACE_TITLE] = g_port_font_title;
    for (i = 0; i < PORT_FACES; i++) {
        if (!stbtt_InitFont(&g_font.info[i], files[i],
                            stbtt_GetFontOffsetForIndex(files[i], 0))) {
            return FALSE;
        }
    }
    g_font.is_started = TRUE;
    return TRUE;
}

static void port_FontForget(PortFontSize *kept) {
    s32 i;

    for (i = 0; i < LETTERS; i++) {
        stbtt_FreeBitmap(kept->glyphs[i].coverage, NULL);
        kept->glyphs[i].coverage = NULL;
        kept->glyphs[i].is_made = FALSE;
    }
    kept->size = 0;
}

/* The face at the size, made ready (its letters made later); NULL if the
 * typeface could not be read. */
static PortFontSize *port_FontSize(s32 face, s32 pixels) {
    PortFontSize *oldest = &g_font.sizes[0];
    s32 ascent = 0;
    s32 descent = 0;
    s32 gap = 0;
    s32 i;

    if (!g_font.is_started || pixels <= 0) {
        return NULL;
    }
    if (face < 0 || face >= PORT_FACES) {
        face = PORT_FACE_TEXT;
    }
    g_font.clock++;
    for (i = 0; i < SIZES_KEPT; i++) {
        PortFontSize *kept = &g_font.sizes[i];

        if (kept->size == pixels && kept->face == face) {
            kept->used = g_font.clock;
            return kept;
        }
        if (kept->used < oldest->used) {
            oldest = kept;
        }
    }
    port_FontForget(oldest);
    oldest->face = face;
    oldest->size = pixels;
    oldest->used = g_font.clock;
    oldest->scale =
        stbtt_ScaleForPixelHeight(&g_font.info[face], (float)pixels);
    stbtt_GetFontVMetrics(&g_font.info[face], &ascent, &descent, &gap);
    oldest->ascent = (s32)(ascent * oldest->scale + 0.5f);
    return oldest;
}

/* The next letter of UTF-8 text, and the text moved past it. */
static s32 port_FontNextLetter(const char **text) {
    const u8 *at = (const u8 *)*text;
    s32 letter = at[0];

    if (letter >= UTF8_TWO && (at[1] & UTF8_TWO) == UTF8_MORE) {
        letter = ((letter & UTF8_TWO_BITS) << UTF8_SHIFT) | (at[1] & UTF8_BITS);
        *text += 2;
    } else {
        /* a longer sequence: its bytes each a letter not drawn */
        *text += 1;
    }
    return letter;
}

static s32 port_FontLetterIndex(s32 letter) {
    if (letter >= ASCII_FIRST && letter <= ASCII_LAST) {
        return letter - ASCII_FIRST;
    }
    if (letter >= LATIN_FIRST && letter <= LATIN_LAST) {
        return ASCII_COUNT + letter - LATIN_FIRST;
    }
    return '?' - ASCII_FIRST;
}

static s32 port_FontIndexLetter(s32 index) {
    return index < ASCII_COUNT ? ASCII_FIRST + index
                               : LATIN_FIRST + index - ASCII_COUNT;
}

static const PortGlyph *port_FontGlyph(PortFontSize *kept, s32 index) {
    PortGlyph *glyph = &kept->glyphs[index];
    const stbtt_fontinfo *info = &g_font.info[kept->face];
    s32 letter = port_FontIndexLetter(index);
    s32 advance = 0;
    s32 bearing = 0;

    if (!glyph->is_made) {
        glyph->coverage = stbtt_GetCodepointBitmap(
            info, kept->scale, kept->scale, letter, &glyph->w, &glyph->h,
            &glyph->x, &glyph->y);
        stbtt_GetCodepointHMetrics(info, letter, &advance, &bearing);
        glyph->advance = advance * kept->scale;
        glyph->is_made = TRUE;
    }
    return glyph;
}

/* ---- the text ---- */

s32 port_TextAscent(const PortTextStyle *style) {
    PortFontSize *kept = port_FontSize(style->face, style->size);

    return kept == NULL ? 0 : kept->ascent;
}

s32 port_TextMeasure(const char *text, const PortTextStyle *style) {
    PortFontSize *kept = port_FontSize(style->face, style->size);
    float pen = 0;
    s32 before = -1;

    if (kept == NULL) {
        return 0;
    }
    while (*text != 0) {
        s32 letter = port_FontNextLetter(&text);
        s32 index = port_FontLetterIndex(letter);

        if (before >= 0) {
            pen += kept->scale * stbtt_GetCodepointKernAdvance(
                                     &g_font.info[kept->face], before,
                                     port_FontIndexLetter(index));
        }
        pen += port_FontGlyph(kept, index)->advance;
        before = port_FontIndexLetter(index);
    }
    return (s32)(pen + 0.5f) + 2 * style->outline_px +
           (style->shadow_px > 0 ? style->shadow_px : 0);
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

/* How much of the glyph covers (i, j) of its bitmap, its shape grown by
 * `grow` pixels all round (an edge): the most within that round. */
static u32 port_FontCover(const PortGlyph *glyph, s32 i, s32 j, s32 grow) {
    u32 most = 0;
    s32 di;
    s32 dj;

    if (grow == 0) {
        return i < 0 || j < 0 || i >= glyph->w || j >= glyph->h
                   ? 0
                   : glyph->coverage[j * glyph->w + i];
    }
    for (dj = -grow; dj <= grow; dj++) {
        s32 y = j + dj;

        if (y < 0 || y >= glyph->h) {
            continue;
        }
        for (di = -grow; di <= grow; di++) {
            s32 x = i + di;
            u32 here;

            if (x < 0 || x >= glyph->w ||
                di * di + dj * dj > grow * grow + grow) {
                continue;
            }
            here = glyph->coverage[y * glyph->w + x];
            if (here > most) {
                most = here;
            }
        }
    }
    return most;
}

/* One pass of the text: its letters grown by `grow`, moved by (dx, dy),
 * in `color`. */
static void port_TextPass(
    u32 *pixels, s32 w, s32 h, s32 x, s32 baseline, const char *text,
    PortFontSize *kept, s32 grow, u32 color) {
    float pen = (float)x;
    s32 before = -1;

    while (*text != 0) {
        s32 letter = port_FontNextLetter(&text);
        s32 index = port_FontLetterIndex(letter);
        const PortGlyph *glyph;
        s32 left;
        s32 i;
        s32 j;

        if (before >= 0) {
            pen += kept->scale * stbtt_GetCodepointKernAdvance(
                                     &g_font.info[kept->face], before,
                                     port_FontIndexLetter(index));
        }
        glyph = port_FontGlyph(kept, index);
        left = (s32)(pen + 0.5f) + glyph->x;
        for (j = -grow; j < glyph->h + grow; j++) {
            s32 py = baseline + glyph->y + j;

            if (py < 0 || py >= h) {
                continue;
            }
            for (i = -grow; i < glyph->w + grow; i++) {
                s32 px = left + i;
                u32 amount;

                if (px < 0 || px >= w) {
                    continue;
                }
                amount = port_FontCover(glyph, i, j, grow);
                if (amount != 0) {
                    pixels[py * w + px] =
                        port_FontMix(pixels[py * w + px], color, amount);
                }
            }
        }
        pen += glyph->advance;
        before = port_FontIndexLetter(index);
    }
}

void port_TextDraw(u32 *pixels, s32 w, s32 h, s32 x, s32 baseline,
                   const char *text, const PortTextStyle *style) {
    PortFontSize *kept = port_FontSize(style->face, style->size);
    s32 grow = style->outline_px > 0 ? style->outline_px : 0;

    if (kept == NULL) {
        return;
    }
    /* the letters begin `grow` in, so that x is the edge's left */
    x += grow;
    if (style->shadow_px > 0 && (style->shadow >> ALPHA_SHIFT) != 0) {
        port_TextPass(
            pixels, w, h, x + style->shadow_px, baseline + style->shadow_px,
            text, kept, grow, style->shadow);
    }
    if (grow > 0 && (style->outline >> ALPHA_SHIFT) != 0) {
        port_TextPass(
            pixels, w, h, x, baseline, text, kept, grow, style->outline);
    }
    port_TextPass(pixels, w, h, x, baseline, text, kept, 0, style->color);
}

/* ---- the plain text of one face and size ---- */

void port_FontSetSize(s32 pixels) { g_font.size = pixels; }

static PortTextStyle port_FontPlain(u32 color) {
    PortTextStyle style;

    memset(&style, 0, sizeof(style));
    style.face = PORT_FACE_TEXT;
    style.size = g_font.size;
    style.color = color;
    return style;
}

s32 port_FontAscent(void) {
    PortTextStyle style = port_FontPlain(0);

    return port_TextAscent(&style);
}

s32 port_FontMeasure(const char *text) {
    PortTextStyle style = port_FontPlain(0);

    return port_TextMeasure(text, &style);
}

void port_FontDraw(u32 *pixels, s32 w, s32 h, s32 x, s32 baseline,
                   const char *text, u32 color) {
    PortTextStyle style = port_FontPlain(color);

    port_TextDraw(pixels, w, h, x, baseline, text, &style);
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
