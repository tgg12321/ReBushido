/* A picture as a baseline JPEG (film.h): the frames of a film in an AVI,
 * where there is no H.264 encoder (docs/PORT.md, "A replay as a film").
 * JFIF's colours, the chroma at half the size each way (4:2:0), the
 * tables of the standard's Annex K scaled as the IJG's library scales
 * them, and its floating-point DCT (Arai, Agui and Nakajima's). Written
 * for the port; nothing of a library's. Only the build with a window has
 * this file. */
#include <string.h>

#include "film.h"

#define BLOCK 64
#define MCU 16
#define COMPONENTS 3
#define QUALITY_LEAST 1
#define QUALITY_MOST 100
#define QUALITY_HALF 50
#define LEVEL_SHIFT 128.0f
/* what a header takes at the most, beside the scan */
#define HEADER_BYTES 700

/* the natural place of each coefficient in the zigzag's order */
static const u8 g_natural[BLOCK] = {
    0,  1,  8,  16, 9,  2,  3,  10, 17, 24, 32, 25, 18, 11, 4,  5,
    12, 19, 26, 33, 40, 48, 41, 34, 27, 20, 13, 6,  7,  14, 21, 28,
    35, 42, 49, 56, 57, 50, 43, 36, 29, 22, 15, 23, 30, 37, 44, 51,
    58, 59, 52, 45, 38, 31, 39, 46, 53, 60, 61, 54, 47, 55, 62, 63,
};

/* Annex K's tables, in the natural order: the luminance's and the
 * chrominance's */
static const u8 g_luma_table[BLOCK] = {
    16, 11, 10, 16, 24,  40,  51,  61,  12, 12, 14, 19, 26,  58,  60,  55,
    14, 13, 16, 24, 40,  57,  69,  56,  14, 17, 22, 29, 51,  87,  80,  62,
    18, 22, 37, 56, 68,  109, 103, 77,  24, 35, 55, 64, 81,  104, 113, 92,
    49, 64, 78, 87, 103, 121, 120, 101, 72, 92, 95, 98, 112, 100, 103, 99,
};
static const u8 g_chroma_table[BLOCK] = {
    17, 18, 24, 47, 99, 99, 99, 99, 18, 21, 26, 66, 99, 99, 99, 99,
    24, 26, 56, 99, 99, 99, 99, 99, 47, 66, 99, 99, 99, 99, 99, 99,
    99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99,
    99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99,
};

/* Annex K's Huffman tables: how many codes of each length, then the
 * values */
static const u8 g_dc_luma_bits[16] = {
    0, 1, 5, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0};
static const u8 g_dc_chroma_bits[16] = {
    0, 3, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0};
static const u8 g_dc_values[12] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11};
static const u8 g_ac_luma_bits[16] = {
    0, 2, 1, 3, 3, 2, 4, 3, 5, 5, 4, 4, 0, 0, 1, 0x7D};
static const u8 g_ac_luma_values[162] = {
    0x01, 0x02, 0x03, 0x00, 0x04, 0x11, 0x05, 0x12, 0x21, 0x31, 0x41, 0x06,
    0x13, 0x51, 0x61, 0x07, 0x22, 0x71, 0x14, 0x32, 0x81, 0x91, 0xA1, 0x08,
    0x23, 0x42, 0xB1, 0xC1, 0x15, 0x52, 0xD1, 0xF0, 0x24, 0x33, 0x62, 0x72,
    0x82, 0x09, 0x0A, 0x16, 0x17, 0x18, 0x19, 0x1A, 0x25, 0x26, 0x27, 0x28,
    0x29, 0x2A, 0x34, 0x35, 0x36, 0x37, 0x38, 0x39, 0x3A, 0x43, 0x44, 0x45,
    0x46, 0x47, 0x48, 0x49, 0x4A, 0x53, 0x54, 0x55, 0x56, 0x57, 0x58, 0x59,
    0x5A, 0x63, 0x64, 0x65, 0x66, 0x67, 0x68, 0x69, 0x6A, 0x73, 0x74, 0x75,
    0x76, 0x77, 0x78, 0x79, 0x7A, 0x83, 0x84, 0x85, 0x86, 0x87, 0x88, 0x89,
    0x8A, 0x92, 0x93, 0x94, 0x95, 0x96, 0x97, 0x98, 0x99, 0x9A, 0xA2, 0xA3,
    0xA4, 0xA5, 0xA6, 0xA7, 0xA8, 0xA9, 0xAA, 0xB2, 0xB3, 0xB4, 0xB5, 0xB6,
    0xB7, 0xB8, 0xB9, 0xBA, 0xC2, 0xC3, 0xC4, 0xC5, 0xC6, 0xC7, 0xC8, 0xC9,
    0xCA, 0xD2, 0xD3, 0xD4, 0xD5, 0xD6, 0xD7, 0xD8, 0xD9, 0xDA, 0xE1, 0xE2,
    0xE3, 0xE4, 0xE5, 0xE6, 0xE7, 0xE8, 0xE9, 0xEA, 0xF1, 0xF2, 0xF3, 0xF4,
    0xF5, 0xF6, 0xF7, 0xF8, 0xF9, 0xFA,
};
static const u8 g_ac_chroma_bits[16] = {
    0, 2, 1, 2, 4, 4, 3, 4, 7, 5, 4, 4, 0, 1, 2, 0x77};
static const u8 g_ac_chroma_values[162] = {
    0x00, 0x01, 0x02, 0x03, 0x11, 0x04, 0x05, 0x21, 0x31, 0x06, 0x12, 0x41,
    0x51, 0x07, 0x61, 0x71, 0x13, 0x22, 0x32, 0x81, 0x08, 0x14, 0x42, 0x91,
    0xA1, 0xB1, 0xC1, 0x09, 0x23, 0x33, 0x52, 0xF0, 0x15, 0x62, 0x72, 0xD1,
    0x0A, 0x16, 0x24, 0x34, 0xE1, 0x25, 0xF1, 0x17, 0x18, 0x19, 0x1A, 0x26,
    0x27, 0x28, 0x29, 0x2A, 0x35, 0x36, 0x37, 0x38, 0x39, 0x3A, 0x43, 0x44,
    0x45, 0x46, 0x47, 0x48, 0x49, 0x4A, 0x53, 0x54, 0x55, 0x56, 0x57, 0x58,
    0x59, 0x5A, 0x63, 0x64, 0x65, 0x66, 0x67, 0x68, 0x69, 0x6A, 0x73, 0x74,
    0x75, 0x76, 0x77, 0x78, 0x79, 0x7A, 0x82, 0x83, 0x84, 0x85, 0x86, 0x87,
    0x88, 0x89, 0x8A, 0x92, 0x93, 0x94, 0x95, 0x96, 0x97, 0x98, 0x99, 0x9A,
    0xA2, 0xA3, 0xA4, 0xA5, 0xA6, 0xA7, 0xA8, 0xA9, 0xAA, 0xB2, 0xB3, 0xB4,
    0xB5, 0xB6, 0xB7, 0xB8, 0xB9, 0xBA, 0xC2, 0xC3, 0xC4, 0xC5, 0xC6, 0xC7,
    0xC8, 0xC9, 0xCA, 0xD2, 0xD3, 0xD4, 0xD5, 0xD6, 0xD7, 0xD8, 0xD9, 0xDA,
    0xE2, 0xE3, 0xE4, 0xE5, 0xE6, 0xE7, 0xE8, 0xE9, 0xEA, 0xF2, 0xF3, 0xF4,
    0xF5, 0xF6, 0xF7, 0xF8, 0xF9, 0xFA,
};

/* the DCT's scale of each row and column (jfdctflt's) */
static const float g_aan[8] = {1.0f, 1.387039845f, 1.306562965f, 1.175875602f,
                               1.0f, 0.785694958f, 0.541196100f, 0.275899379f};

typedef struct {
    u16 code[256];
    u8 size[256];
} HuffTable;

typedef struct {
    u8 *out;
    u32 at;
    u32 room;
    u32 bits;
    s32 count;
    s32 full;
} BitWriter;

static struct {
    s32 quality; /* the tables' quality, 0 before they are made */
    u8 tables[2][BLOCK];
    float divisors[2][BLOCK];
    HuffTable dc[2];
    HuffTable ac[2];
} g_jpeg;

static void port_JpegCodes(HuffTable *table, const u8 *bits, const u8 *values) {
    u32 code = 0;
    s32 k = 0;
    s32 length;
    s32 i;

    memset(table, 0, sizeof(*table));
    for (length = 1; length <= 16; length++) {
        for (i = 0; i < bits[length - 1]; i++) {
            table->code[values[k]] = (u16)code;
            table->size[values[k]] = (u8)length;
            code++;
            k++;
        }
        code <<= 1;
    }
}

static void port_JpegTables(s32 quality) {
    s32 scale;
    s32 t;
    s32 i;

    if (quality < QUALITY_LEAST) {
        quality = QUALITY_LEAST;
    }
    if (quality > QUALITY_MOST) {
        quality = QUALITY_MOST;
    }
    if (g_jpeg.quality == quality) {
        return;
    }
    g_jpeg.quality = quality;
    scale = quality < QUALITY_HALF ? 5000 / quality : 200 - quality * 2;
    for (t = 0; t < 2; t++) {
        const u8 *base = t == 0 ? g_luma_table : g_chroma_table;

        for (i = 0; i < BLOCK; i++) {
            s32 value = (base[i] * scale + 50) / 100;

            value = value < 1 ? 1 : value > 255 ? 255 : value;
            g_jpeg.tables[t][i] = (u8)value;
            g_jpeg.divisors[t][i] =
                1.0f / ((float)value * g_aan[i / 8] * g_aan[i % 8] * 8.0f);
        }
    }
    port_JpegCodes(&g_jpeg.dc[0], g_dc_luma_bits, g_dc_values);
    port_JpegCodes(&g_jpeg.dc[1], g_dc_chroma_bits, g_dc_values);
    port_JpegCodes(&g_jpeg.ac[0], g_ac_luma_bits, g_ac_luma_values);
    port_JpegCodes(&g_jpeg.ac[1], g_ac_chroma_bits, g_ac_chroma_values);
}

/* ---- writing ---- */

static void port_PutByte(BitWriter *w, u8 byte) {
    if (w->at >= w->room) {
        w->full = TRUE;
        return;
    }
    w->out[w->at++] = byte;
}

static void port_PutBytes(BitWriter *w, const u8 *bytes, u32 count) {
    u32 i;

    for (i = 0; i < count; i++) {
        port_PutByte(w, bytes[i]);
    }
}

static void port_PutBig16(BitWriter *w, u32 value) {
    port_PutByte(w, (u8)(value >> 8));
    port_PutByte(w, (u8)value);
}

/* `size` bits of `code`, the first the highest; a 0xFF of the scan has a
 * 0 after it */
static void port_PutBits(BitWriter *w, u32 code, s32 size) {
    w->bits = (w->bits << size) | (code & ((1U << size) - 1));
    w->count += size;
    while (w->count >= 8) {
        u8 byte = (u8)(w->bits >> (w->count - 8));

        port_PutByte(w, byte);
        if (byte == 0xFF) {
            port_PutByte(w, 0);
        }
        w->count -= 8;
    }
}

static void port_PutHuffTable(
    BitWriter *w, s32 kind_and_id, const u8 *bits, const u8 *values) {
    s32 count = 0;
    s32 i;

    for (i = 0; i < 16; i++) {
        count += bits[i];
    }
    port_PutByte(w, 0xFF);
    port_PutByte(w, 0xC4);
    port_PutBig16(w, (u32)(2 + 1 + 16 + count));
    port_PutByte(w, (u8)kind_and_id);
    port_PutBytes(w, bits, 16);
    port_PutBytes(w, values, (u32)count);
}

static void port_PutHeader(BitWriter *w, s32 width, s32 height) {
    static const u8 g_jfif[] = {
        0xFF, 0xD8, 0xFF, 0xE0, 0, 16, 'J', 'F', 'I', 'F',
        0,    1,    1,    0,    0, 1,  0,   1,   0,   0};
    s32 t;
    s32 i;

    port_PutBytes(w, g_jfif, sizeof(g_jfif));
    for (t = 0; t < 2; t++) {
        port_PutByte(w, 0xFF);
        port_PutByte(w, 0xDB);
        port_PutBig16(w, 2 + 1 + BLOCK);
        port_PutByte(w, (u8)t);
        for (i = 0; i < BLOCK; i++) {
            port_PutByte(w, g_jpeg.tables[t][g_natural[i]]);
        }
    }
    /* the frame: 8 bits, its size, Y at 2 by 2 and the chroma at 1 */
    port_PutByte(w, 0xFF);
    port_PutByte(w, 0xC0);
    port_PutBig16(w, 2 + 6 + COMPONENTS * 3);
    port_PutByte(w, 8);
    port_PutBig16(w, (u32)height);
    port_PutBig16(w, (u32)width);
    port_PutByte(w, COMPONENTS);
    for (i = 0; i < COMPONENTS; i++) {
        port_PutByte(w, (u8)(i + 1));
        port_PutByte(w, i == 0 ? 0x22 : 0x11);
        port_PutByte(w, i == 0 ? 0 : 1);
    }
    port_PutHuffTable(w, 0x00, g_dc_luma_bits, g_dc_values);
    port_PutHuffTable(w, 0x10, g_ac_luma_bits, g_ac_luma_values);
    port_PutHuffTable(w, 0x01, g_dc_chroma_bits, g_dc_values);
    port_PutHuffTable(w, 0x11, g_ac_chroma_bits, g_ac_chroma_values);
    /* the scan: all three, Y with the first tables */
    port_PutByte(w, 0xFF);
    port_PutByte(w, 0xDA);
    port_PutBig16(w, 2 + 1 + COMPONENTS * 2 + 3);
    port_PutByte(w, COMPONENTS);
    for (i = 0; i < COMPONENTS; i++) {
        port_PutByte(w, (u8)(i + 1));
        port_PutByte(w, i == 0 ? 0x00 : 0x11);
    }
    port_PutByte(w, 0);
    port_PutByte(w, 63);
    port_PutByte(w, 0);
}

/* ---- a block ---- */

/* jfdctflt's one dimension, along `stride` */
static void port_Dct8(float *d, s32 stride) {
    float tmp0 = d[0] + d[7 * stride];
    float tmp7 = d[0] - d[7 * stride];
    float tmp1 = d[stride] + d[6 * stride];
    float tmp6 = d[stride] - d[6 * stride];
    float tmp2 = d[2 * stride] + d[5 * stride];
    float tmp5 = d[2 * stride] - d[5 * stride];
    float tmp3 = d[3 * stride] + d[4 * stride];
    float tmp4 = d[3 * stride] - d[4 * stride];
    float tmp10 = tmp0 + tmp3;
    float tmp13 = tmp0 - tmp3;
    float tmp11 = tmp1 + tmp2;
    float tmp12 = tmp1 - tmp2;
    float z1;
    float z2;
    float z3;
    float z4;
    float z5;
    float z11;
    float z13;

    d[0] = tmp10 + tmp11;
    d[4 * stride] = tmp10 - tmp11;
    z1 = (tmp12 + tmp13) * 0.707106781f;
    d[2 * stride] = tmp13 + z1;
    d[6 * stride] = tmp13 - z1;
    tmp10 = tmp4 + tmp5;
    tmp11 = tmp5 + tmp6;
    tmp12 = tmp6 + tmp7;
    z5 = (tmp10 - tmp12) * 0.382683433f;
    z2 = 0.541196100f * tmp10 + z5;
    z4 = 1.306562965f * tmp12 + z5;
    z3 = tmp11 * 0.707106781f;
    z11 = tmp7 + z3;
    z13 = tmp7 - z3;
    d[5 * stride] = z13 + z2;
    d[3 * stride] = z13 - z2;
    d[stride] = z11 + z4;
    d[7 * stride] = z11 - z4;
}

/* the number of bits `value` takes, and its bits as the standard writes
 * them */
static s32 port_Category(s32 value, u32 *bits) {
    u32 magnitude = (u32)(value < 0 ? -value : value);
    s32 size = 0;

    while (magnitude != 0) {
        size++;
        magnitude >>= 1;
    }
    *bits = (u32)(value < 0 ? value - 1 : value);
    return size;
}

/* A block of 64 samples, level-shifted, transformed, quantised and
 * written; `*dc` the last DC of its component. */
static void port_JpegBlock(BitWriter *w, float *block, s32 table, s32 *dc) {
    s32 coefficients[BLOCK];
    const HuffTable *dc_table = &g_jpeg.dc[table];
    const HuffTable *ac_table = &g_jpeg.ac[table];
    s32 run = 0;
    s32 size;
    u32 bits;
    s32 i;

    for (i = 0; i < 8; i++) {
        port_Dct8(block + i * 8, 1);
    }
    for (i = 0; i < 8; i++) {
        port_Dct8(block + i, 8);
    }
    for (i = 0; i < BLOCK; i++) {
        float value = block[i] * g_jpeg.divisors[table][i];

        coefficients[i] =
            value >= 0.0f ? (s32)(value + 0.5f) : -(s32)(0.5f - value);
    }
    size = port_Category(coefficients[0] - *dc, &bits);
    *dc = coefficients[0];
    port_PutBits(w, dc_table->code[size], dc_table->size[size]);
    if (size != 0) {
        port_PutBits(w, bits, size);
    }
    for (i = 1; i < BLOCK; i++) {
        s32 value = coefficients[g_natural[i]];

        if (value == 0) {
            run++;
            continue;
        }
        while (run > 15) {
            port_PutBits(w, ac_table->code[0xF0], ac_table->size[0xF0]);
            run -= 16;
        }
        size = port_Category(value, &bits);
        port_PutBits(w, ac_table->code[(run << 4) + size],
                     ac_table->size[(run << 4) + size]);
        port_PutBits(w, bits, size);
        run = 0;
    }
    if (run > 0) {
        port_PutBits(w, ac_table->code[0x00], ac_table->size[0x00]);
    }
}

u32 port_JpegEncode(
    const u32 *pixels, s32 width, s32 height, s32 quality, u8 *out, u32 room) {
    BitWriter w;
    s32 dc[COMPONENTS] = {0, 0, 0};
    s32 mx;
    s32 my;

    if (width <= 0 || height <= 0 || room < HEADER_BYTES) {
        return 0;
    }
    port_JpegTables(quality);
    memset(&w, 0, sizeof(w));
    w.out = out;
    w.room = room;
    port_PutHeader(&w, width, height);
    for (my = 0; my < height; my += MCU) {
        for (mx = 0; mx < width; mx += MCU) {
            float luma[4][BLOCK];
            float cb[BLOCK];
            float cr[BLOCK];
            s32 x;
            s32 y;
            s32 b;

            memset(cb, 0, sizeof(cb));
            memset(cr, 0, sizeof(cr));
            for (y = 0; y < MCU; y++) {
                /* past the picture's edge its last row and column again */
                s32 row = my + y < height ? my + y : height - 1;
                const u32 *line = pixels + (size_t)row * (size_t)width;

                for (x = 0; x < MCU; x++) {
                    u32 pixel = line[mx + x < width ? mx + x : width - 1];
                    float r = (float)((pixel >> 16) & 0xFF);
                    float g = (float)((pixel >> 8) & 0xFF);
                    float bl = (float)(pixel & 0xFF);
                    s32 at = (y >> 1) * 8 + (x >> 1);

                    luma[(y >> 3) * 2 + (x >> 3)][(y & 7) * 8 + (x & 7)] =
                        0.299f * r + 0.587f * g + 0.114f * bl - LEVEL_SHIFT;
                    cb[at] +=
                        0.25f * (-0.168736f * r - 0.331264f * g + 0.5f * bl);
                    cr[at] +=
                        0.25f * (0.5f * r - 0.418688f * g - 0.081312f * bl);
                }
            }
            for (b = 0; b < 4; b++) {
                port_JpegBlock(&w, luma[b], 0, &dc[0]);
            }
            port_JpegBlock(&w, cb, 1, &dc[1]);
            port_JpegBlock(&w, cr, 1, &dc[2]);
        }
    }
    /* the last byte's rest filled with ones, and the end */
    if (w.count > 0) {
        port_PutBits(&w, 0x7F, 8 - w.count);
    }
    port_PutByte(&w, 0xFF);
    port_PutByte(&w, 0xD9);
    return w.full ? 0 : w.at;
}
