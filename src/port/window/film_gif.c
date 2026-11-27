/* A clip of the replay theatre as a GIF (film.h; docs/PORT.md, "The
 * replay theatre and photo mode"): the port's own writer, GIF89a with its
 * looping extension, no library of anyone else's.
 *
 * A GIF has no sound and its times are in hundredths of a second, so a
 * clip is the fight's own frames: every second blank of the film (a frame
 * of the fight is two blanks), each shown for its time, the hundredths
 * counted from the clip's start so that they never drift (3, 3, 4, 3,
 * ...: 33.43 ms a frame). It is made at most PORT_GIF_WIDTH across, the
 * film's pictures taken down by the mean of each block of pixels. Each
 * picture has its own palette of 256 colours, cut from its own colours
 * (the median cut, of 32,768 boxes of five bits a channel), with an
 * ordered dither of four by four, which stays still from picture to
 * picture where a scene does, and the pixels written by LZW as GIF has
 * it. After the first picture a pixel that stayed near what it was last
 * drawn from is left as it is (the transparent index, 255), which a clip
 * of a still camera is mostly (four seconds of a remastered fight: 1.9 MB,
 * against 15.4 with every pixel drawn again). Only the build with a
 * window has this file. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "film.h"

#define BINS 32768
#define COLORS 256
#define LZW_CODES 4096
#define LZW_MIN_BITS 8
#define BLOCK_BYTES 255
#define HUNDREDTHS 100
/* the index of a pixel left as the GIF shows it, after the first picture */
#define TRANSPARENT (COLORS - 1)
/* how near, each channel, a pixel must stay to what it was last drawn from
 * to be left as it is */
#define KEPT_NEAR 6

struct PortGif {
    FILE *file;
    s32 w; /* the film's */
    s32 h;
    s32 gw; /* the GIF's */
    s32 gh;
    u32 frames;
    u32 shown_hundredths;
    u32 *small;
    u32 *drawn_from; /* each pixel's picture as it was last drawn */
    u8 *kept;        /* ... left as it is in this picture */
    u8 *indexes;
    u32 *count;
    u8 *map;
    s32 failed;
    /* the LZW's codes: a prefix's code and a byte to the code that is
     * them, by an open hash */
    s32 *hash_code;
    u32 *hash_key;
    /* the bits being written into a block */
    u8 block[BLOCK_BYTES];
    s32 block_bytes;
    u32 bits;
    s32 bit_count;
};

static void port_GifByte(PortGif *gif, u32 value) {
    if (fputc((int)(value & 0xFF), gif->file) == EOF) {
        gif->failed = TRUE;
    }
}

static void port_GifHalf(PortGif *gif, u32 value) {
    port_GifByte(gif, value);
    port_GifByte(gif, value >> 8);
}

PortGif *port_GifOpen(const char *path, s32 w, s32 h, char *why, u32 room) {
    PortGif *gif = calloc(1, sizeof(PortGif));
    s32 gw = w < PORT_GIF_WIDTH ? w : PORT_GIF_WIDTH;
    s32 gh = (s32)((s64)h * gw / w);

    if (gif == NULL) {
        snprintf(why, room, "no memory for the GIF");
        return NULL;
    }
    gif->w = w;
    gif->h = h;
    gif->gw = gw;
    gif->gh = gh > 0 ? gh : 1;
    gif->small = malloc((size_t)gif->gw * (size_t)gif->gh * sizeof(u32));
    gif->indexes = malloc((size_t)gif->gw * (size_t)gif->gh);
    gif->drawn_from = malloc((size_t)gif->gw * (size_t)gif->gh * sizeof(u32));
    gif->kept = malloc((size_t)gif->gw * (size_t)gif->gh);
    gif->count = malloc(BINS * sizeof(u32));
    gif->map = malloc(BINS);
    gif->hash_code = malloc(LZW_CODES * 2 * sizeof(s32));
    gif->hash_key = malloc(LZW_CODES * 2 * sizeof(u32));
    gif->file = fopen(path, "wb");
    if (gif->small == NULL || gif->indexes == NULL || gif->count == NULL ||
        gif->drawn_from == NULL || gif->kept == NULL || gif->map == NULL ||
        gif->hash_code == NULL || gif->hash_key == NULL || gif->file == NULL) {
        snprintf(why, room, "%s cannot be written",
                 gif->file == NULL ? path : "the GIF (no memory)");
        port_GifClose(gif, FALSE);
        return NULL;
    }
    /* the header, the screen with no palette of its own, and the
     * application extension that loops it for ever */
    fwrite("GIF89a", 1, 6, gif->file);
    port_GifHalf(gif, (u32)gif->gw);
    port_GifHalf(gif, (u32)gif->gh);
    port_GifByte(gif, 0x70); /* no global palette, eight bits a colour */
    port_GifByte(gif, 0);
    port_GifByte(gif, 0);
    port_GifByte(gif, 0x21);
    port_GifByte(gif, 0xFF);
    port_GifByte(gif, 11);
    fwrite("NETSCAPE2.0", 1, 11, gif->file);
    port_GifByte(gif, 3);
    port_GifByte(gif, 1);
    port_GifHalf(gif, 0);
    port_GifByte(gif, 0);
    return gif;
}

/* The film's picture taken down to the GIF's size: each of its pixels the
 * mean of the film's block under it. */
static void port_GifShrink(PortGif *gif, const u32 *pixels) {
    s32 x;
    s32 y;

    for (y = 0; y < gif->gh; y++) {
        s32 y0 = y * gif->h / gif->gh;
        s32 y1 = (y + 1) * gif->h / gif->gh;

        if (y1 <= y0) {
            y1 = y0 + 1;
        }
        for (x = 0; x < gif->gw; x++) {
            s32 x0 = x * gif->w / gif->gw;
            s32 x1 = (x + 1) * gif->w / gif->gw;
            u32 r = 0;
            u32 g = 0;
            u32 b = 0;
            u32 n = 0;
            s32 i;
            s32 j;

            if (x1 <= x0) {
                x1 = x0 + 1;
            }
            for (j = y0; j < y1; j++) {
                const u32 *row = pixels + (size_t)j * (size_t)gif->w;

                for (i = x0; i < x1; i++) {
                    r += (row[i] >> 16) & 0xFF;
                    g += (row[i] >> 8) & 0xFF;
                    b += row[i] & 0xFF;
                    n++;
                }
            }
            gif->small[y * gif->gw + x] =
                ((r / n) << 16) | ((g / n) << 8) | (b / n);
        }
    }
}

/* The ordered dither's threshold at a pixel, -4 to 3: a fraction of the
 * five bits' step of eight. */
static s32 port_GifDither(s32 x, s32 y) {
    static const u8 g_bayer[4][4] = {
        {0, 8, 2, 10}, {12, 4, 14, 6}, {3, 11, 1, 9}, {15, 7, 13, 5}};

    return g_bayer[y & 3][x & 3] / 2 - 4;
}

static u32 port_GifBin(u32 pixel, s32 dither) {
    s32 c[3];
    s32 i;

    c[0] = (s32)((pixel >> 16) & 0xFF) + dither;
    c[1] = (s32)((pixel >> 8) & 0xFF) + dither;
    c[2] = (s32)(pixel & 0xFF) + dither;
    for (i = 0; i < 3; i++) {
        c[i] = c[i] < 0 ? 0 : c[i] > 255 ? 255 : c[i];
    }
    return ((u32)(c[0] >> 3) << 10) | ((u32)(c[1] >> 3) << 5) |
           (u32)(c[2] >> 3);
}

typedef struct {
    s32 lo[3];
    s32 hi[3];
    u32 pixels;
} GifBox;

/* The box's bounds drawn in to the bins it holds that are used, and how
 * many pixels they hold. */
static void port_GifShrinkBox(const PortGif *gif, GifBox *box) {
    s32 lo[3] = {31, 31, 31};
    s32 hi[3] = {0, 0, 0};
    s32 r;
    s32 g;
    s32 b;

    box->pixels = 0;
    for (r = box->lo[0]; r <= box->hi[0]; r++) {
        for (g = box->lo[1]; g <= box->hi[1]; g++) {
            for (b = box->lo[2]; b <= box->hi[2]; b++) {
                u32 n = gif->count[(r << 10) | (g << 5) | b];

                if (n == 0) {
                    continue;
                }
                box->pixels += n;
                lo[0] = r < lo[0] ? r : lo[0];
                lo[1] = g < lo[1] ? g : lo[1];
                lo[2] = b < lo[2] ? b : lo[2];
                hi[0] = r > hi[0] ? r : hi[0];
                hi[1] = g > hi[1] ? g : hi[1];
                hi[2] = b > hi[2] ? b : hi[2];
            }
        }
    }
    if (box->pixels > 0) {
        memcpy(box->lo, lo, sizeof(lo));
        memcpy(box->hi, hi, sizeof(hi));
    }
}

/* A box cut in two across its longest side at its median pixel: FALSE
 * where it is one bin. */
static s32 port_GifCut(const PortGif *gif, GifBox *box, GifBox *other) {
    s32 side = 0;
    s32 i;
    s32 at;
    u32 half = 0;

    for (i = 1; i < 3; i++) {
        if (box->hi[i] - box->lo[i] > box->hi[side] - box->lo[side]) {
            side = i;
        }
    }
    if (box->hi[side] == box->lo[side]) {
        return FALSE;
    }
    /* both ends of a shrunk box hold pixels: each half keeps one */
    for (at = box->lo[side]; at < box->hi[side] - 1; at++) {
        GifBox slice = *box;

        slice.lo[side] = at;
        slice.hi[side] = at;
        port_GifShrinkBox(gif, &slice);
        half += slice.pixels;
        if (half * 2 >= box->pixels) {
            break;
        }
    }
    *other = *box;
    box->hi[side] = at;
    other->lo[side] = at + 1;
    port_GifShrinkBox(gif, box);
    port_GifShrinkBox(gif, other);
    return TRUE;
}

/* The picture's palette, by the median cut, and each bin's colour of it
 * (the nearest of the palette's). */
static s32 port_GifPalette(PortGif *gif, u8 *palette, s32 colors) {
    static GifBox g_boxes[COLORS];
    s32 boxes = 1;
    s32 i;
    u32 bin;

    g_boxes[0].lo[0] = g_boxes[0].lo[1] = g_boxes[0].lo[2] = 0;
    g_boxes[0].hi[0] = g_boxes[0].hi[1] = g_boxes[0].hi[2] = 31;
    port_GifShrinkBox(gif, &g_boxes[0]);
    while (boxes < colors) {
        s32 widest = -1;
        u32 most = 0;

        /* the box of the most pixels that is more than one bin */
        for (i = 0; i < boxes; i++) {
            const GifBox *box = &g_boxes[i];
            s32 is_one = box->lo[0] == box->hi[0] && box->lo[1] == box->hi[1] &&
                         box->lo[2] == box->hi[2];

            if (!is_one && box->pixels > most) {
                most = box->pixels;
                widest = i;
            }
        }
        if (widest < 0 ||
            !port_GifCut(gif, &g_boxes[widest], &g_boxes[boxes])) {
            break;
        }
        boxes++;
    }
    for (i = 0; i < boxes; i++) {
        const GifBox *box = &g_boxes[i];
        u64 sum[3] = {0, 0, 0};
        s32 r;
        s32 g;
        s32 b;

        for (r = box->lo[0]; r <= box->hi[0]; r++) {
            for (g = box->lo[1]; g <= box->hi[1]; g++) {
                for (b = box->lo[2]; b <= box->hi[2]; b++) {
                    u32 n = gif->count[(r << 10) | (g << 5) | b];

                    sum[0] += (u64)n * (u64)(r * 8 + 4);
                    sum[1] += (u64)n * (u64)(g * 8 + 4);
                    sum[2] += (u64)n * (u64)(b * 8 + 4);
                }
            }
        }
        palette[i * 3 + 0] = (u8)(box->pixels > 0 ? sum[0] / box->pixels : 0);
        palette[i * 3 + 1] = (u8)(box->pixels > 0 ? sum[1] / box->pixels : 0);
        palette[i * 3 + 2] = (u8)(box->pixels > 0 ? sum[2] / box->pixels : 0);
    }
    for (i = boxes; i < COLORS; i++) {
        palette[i * 3 + 0] = palette[i * 3 + 1] = palette[i * 3 + 2] = 0;
    }
    /* each bin used: the nearest colour of the palette */
    for (bin = 0; bin < BINS; bin++) {
        s32 r = (s32)((bin >> 10) & 31) * 8 + 4;
        s32 g = (s32)((bin >> 5) & 31) * 8 + 4;
        s32 b = (s32)(bin & 31) * 8 + 4;
        s32 best = 0;
        s32 best_far = 0x7FFFFFFF;

        if (gif->count[bin] == 0) {
            continue;
        }
        for (i = 0; i < boxes; i++) {
            s32 dr = r - palette[i * 3 + 0];
            s32 dg = g - palette[i * 3 + 1];
            s32 db = b - palette[i * 3 + 2];
            s32 far = dr * dr * 3 + dg * dg * 4 + db * db * 2;

            if (far < best_far) {
                best_far = far;
                best = i;
            }
        }
        gif->map[bin] = (u8)best;
    }
    return boxes;
}

/* ---- the LZW ---- */

static void port_GifFlushBlock(PortGif *gif) {
    if (gif->block_bytes > 0) {
        port_GifByte(gif, (u32)gif->block_bytes);
        if (fwrite(gif->block, 1, (size_t)gif->block_bytes, gif->file) !=
            (size_t)gif->block_bytes) {
            gif->failed = TRUE;
        }
        gif->block_bytes = 0;
    }
}

static void port_GifCode(PortGif *gif, u32 code, s32 width) {
    gif->bits |= code << gif->bit_count;
    gif->bit_count += width;
    while (gif->bit_count >= 8) {
        gif->block[gif->block_bytes++] = (u8)gif->bits;
        gif->bits >>= 8;
        gif->bit_count -= 8;
        if (gif->block_bytes == BLOCK_BYTES) {
            port_GifFlushBlock(gif);
        }
    }
}

static void port_GifLzw(PortGif *gif, const u8 *indexes, u32 count) {
    u32 clear = 1U << LZW_MIN_BITS;
    u32 end = clear + 1;
    u32 next = clear + 2;
    s32 width = LZW_MIN_BITS + 1;
    u32 prefix;
    u32 i;

    port_GifByte(gif, LZW_MIN_BITS);
    gif->bits = 0;
    gif->bit_count = 0;
    gif->block_bytes = 0;
    for (i = 0; i < LZW_CODES * 2; i++) {
        gif->hash_code[i] = -1;
    }
    port_GifCode(gif, clear, width);
    prefix = indexes[0];
    for (i = 1; i < count; i++) {
        u32 key = (prefix << 8) | indexes[i];
        u32 slot = (key * 2654435761U) >> 19 & (LZW_CODES * 2 - 1);

        while (gif->hash_code[slot] >= 0 && gif->hash_key[slot] != key) {
            slot = (slot + 1) & (LZW_CODES * 2 - 1);
        }
        if (gif->hash_code[slot] >= 0) {
            prefix = (u32)gif->hash_code[slot];
            continue;
        }
        port_GifCode(gif, prefix, width);
        if (next < LZW_CODES) {
            gif->hash_code[slot] = (s32)next;
            gif->hash_key[slot] = key;
            if (next == (1U << width) && width < 12) {
                width++;
            }
            next++;
        } else {
            /* the table is full: begun again */
            u32 j;

            port_GifCode(gif, clear, width);
            for (j = 0; j < LZW_CODES * 2; j++) {
                gif->hash_code[j] = -1;
            }
            next = clear + 2;
            width = LZW_MIN_BITS + 1;
        }
        prefix = indexes[i];
    }
    port_GifCode(gif, prefix, width);
    port_GifCode(gif, end, width);
    if (gif->bit_count > 0) {
        gif->block[gif->block_bytes++] = (u8)gif->bits;
        gif->bits = 0;
        gif->bit_count = 0;
    }
    port_GifFlushBlock(gif);
    port_GifByte(gif, 0);
}

/* ---- a picture ---- */

/* Whether the pixel the GIF shows already is near enough to the picture's
 * to be left as it is: within KEPT_NEAR of each channel of the picture's
 * pixel it was last drawn from. */
static s32 port_GifKeeps(u32 was, u32 now) {
    s32 shift;

    for (shift = 0; shift <= 16; shift += 8) {
        s32 a = (s32)((was >> shift) & 0xFF);
        s32 b = (s32)((now >> shift) & 0xFF);

        if (a - b > KEPT_NEAR || b - a > KEPT_NEAR) {
            return FALSE;
        }
    }
    return TRUE;
}

s32 port_GifPicture(PortGif *gif, const u32 *pixels, u32 index) {
    u8 palette[COLORS * 3];
    u32 count = (u32)gif->gw * (u32)gif->gh;
    s32 is_first = gif->frames == 0;
    u32 shown;
    u32 delay;
    u32 i;

    if ((index & 1) != 0) {
        /* the second blank of the fight's frame: the same frame */
        return !gif->failed;
    }
    port_GifShrink(gif, pixels);
    /* after the first picture, a pixel near what it was drawn from last
     * is left as the GIF shows it (the transparent index): only what
     * moved is drawn again, which a still camera makes most of a clip */
    memset(gif->count, 0, BINS * sizeof(u32));
    for (i = 0; i < count; i++) {
        s32 x = (s32)(i % (u32)gif->gw);
        s32 y = (s32)(i / (u32)gif->gw);

        gif->kept[i] =
            !is_first && port_GifKeeps(gif->drawn_from[i], gif->small[i]);
        if (!gif->kept[i]) {
            gif->count[port_GifBin(gif->small[i], port_GifDither(x, y))]++;
        }
    }
    port_GifPalette(gif, palette, is_first ? COLORS : COLORS - 1);
    for (i = 0; i < count; i++) {
        s32 x = (s32)(i % (u32)gif->gw);
        s32 y = (s32)(i / (u32)gif->gw);

        if (gif->kept[i]) {
            gif->indexes[i] = TRANSPARENT;
            continue;
        }
        gif->indexes[i] =
            gif->map[port_GifBin(gif->small[i], port_GifDither(x, y))];
        gif->drawn_from[i] = gif->small[i];
    }
    /* its time: to the next frame's from the clip's start, in hundredths */
    gif->frames++;
    shown = (u32)((u64)gif->frames * 2 * PORT_FILM_RATE_DENOMINATOR *
                  HUNDREDTHS / PORT_FILM_RATE_NUMERATOR);
    delay = shown - gif->shown_hundredths;
    gif->shown_hundredths = shown;
    /* the graphic control extension: left in place, and after the first
     * the transparent index; the picture's place and palette */
    port_GifByte(gif, 0x21);
    port_GifByte(gif, 0xF9);
    port_GifByte(gif, 4);
    port_GifByte(gif, is_first ? 0x04 : 0x05);
    port_GifHalf(gif, delay);
    port_GifByte(gif, TRANSPARENT);
    port_GifByte(gif, 0);
    port_GifByte(gif, 0x2C);
    port_GifHalf(gif, 0);
    port_GifHalf(gif, 0);
    port_GifHalf(gif, (u32)gif->gw);
    port_GifHalf(gif, (u32)gif->gh);
    port_GifByte(gif, 0x87); /* its own palette of 256 */
    if (fwrite(palette, 1, sizeof(palette), gif->file) != sizeof(palette)) {
        gif->failed = TRUE;
    }
    port_GifLzw(gif, gif->indexes, count);
    return !gif->failed;
}

s32 port_GifClose(PortGif *gif, s32 keep) {
    s32 ok = !gif->failed;

    if (gif->file != NULL) {
        port_GifByte(gif, 0x3B);
        ok = ok && !gif->failed && fclose(gif->file) == 0;
    }
    free(gif->small);
    free(gif->drawn_from);
    free(gif->kept);
    free(gif->indexes);
    free(gif->count);
    free(gif->map);
    free(gif->hash_code);
    free(gif->hash_key);
    (void)keep;
    free(gif);
    return ok;
}
