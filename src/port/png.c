/* PNG pictures for the native port's mods (port_png.h; docs/MODS.md): a
 * reader of any PNG that is not interlaced, with its own inflate, and a
 * writer of the plainest PNG there is, eight bits a channel and an alpha,
 * its rows stored and not packed.
 *
 * The inflate is the format as RFC 1951 writes it down, done the plain
 * way: a code is read a bit at a time and looked up by how many codes
 * each length has. That is slower than a table but small, and a texture
 * is read once. Nothing here asks for memory: the caller gives the work
 * area port_PngMeasure says, and the pixels' room. */
#include "common.h"
#include "port_png.h"
#include "bb2.h"

/* ---- the file ---- */

#define SIGNATURE_BYTES 8
#define CHUNK_HEAD_BYTES 8 /* the length, then the four letters */
#define CHUNK_SUM_BYTES 4
#define HEADER_BYTES 13 /* IHDR's data */
#define BITS_PER_BYTE 8
#define BYTE_MASK 0xFF
#define BYTE_MAX 255
#define RGBA 4

/* IHDR's colour types, and what a pixel of each holds */
enum {
    COLOR_GRAY = 0,
    COLOR_RGB = 2,
    COLOR_PALETTE = 3,
    COLOR_GRAY_ALPHA = 4,
    COLOR_RGB_ALPHA = 6
};

/* the five ways a row is filtered */
enum {
    FILTER_NONE = 0,
    FILTER_SUB = 1,
    FILTER_UP = 2,
    FILTER_AVERAGE = 3,
    FILTER_PAETH = 4
};

#define PALETTE_ENTRIES 256
#define DEPTH_WIDE 16 /* a sample of two bytes, the high one first */

static const u8 g_png_signature[SIGNATURE_BYTES] = {
    0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n', /* every PNG's */
};

/* What the header and the chunks before the data say. */
typedef struct {
    s32 w;
    s32 h;
    s32 depth;
    s32 color;
    s32 channels;
    u32 row_bytes;  /* a row's bytes, its filter's byte left out */
    u32 data_bytes; /* the IDAT chunks' bytes, all of them */
    u32 rows_bytes; /* the rows inflated: h times (1 + row_bytes) */
    s32 has_key;    /* a colour of grey or RGB that is transparent */
    u32 key[3];     /* ... that colour, at the samples' own depth */
    s32 palette_count;
    u8 palette[PALETTE_ENTRIES][RGBA];
} PngFile;

static void png_Say(char *problem, u32 room, const char *why) {
    u32 i;

    if (problem == NULL || room == 0) {
        return;
    }
    for (i = 0; i + 1 < room && why[i] != 0; i++) {
        problem[i] = why[i];
    }
    problem[i] = 0;
}

static u32 png_Big32(const u8 *at) {
    return ((u32)at[0] << (3 * BITS_PER_BYTE)) |
           ((u32)at[1] << (2 * BITS_PER_BYTE)) | ((u32)at[2] << BITS_PER_BYTE) |
           (u32)at[3];
}

static s32 png_IsChunk(const u8 *at, const char *name) {
    return at[0] == (u8)name[0] && at[1] == (u8)name[1] &&
           at[2] == (u8)name[2] && at[3] == (u8)name[3];
}

/* How many samples a pixel of this colour type has; 0 for one that is
 * not a PNG's. */
static s32 png_ChannelsOf(s32 color) {
    switch (color) {
    case COLOR_GRAY:
    case COLOR_PALETTE:
        return 1;
    case COLOR_GRAY_ALPHA:
        return 2;
    case COLOR_RGB:
        return 3;
    case COLOR_RGB_ALPHA:
        return RGBA;
    default:
        return 0;
    }
}

/* Whether the format allows this depth for this colour type: grey any
 * of 1, 2, 4, 8 and 16 bits, a palette's index up to 8, and every other
 * type 8 or 16. */
static s32 png_DepthIsAllowed(s32 color, s32 depth) {
    s32 is_narrow = depth == 1 || depth == 2 || depth == 4; /* bits */

    if (color == COLOR_GRAY) {
        return is_narrow || depth == BITS_PER_BYTE || depth == DEPTH_WIDE;
    }
    if (color == COLOR_PALETTE) {
        return is_narrow || depth == BITS_PER_BYTE;
    }
    return depth == BITS_PER_BYTE || depth == DEPTH_WIDE;
}

/* The file's chunks walked once: the header, the palette, what is
 * transparent and how many bytes of data there are. */
static s32 png_Read(
    const u8 *file, u32 size, PngFile *png, char *problem, u32 room) {
    u32 at = SIGNATURE_BYTES;
    s32 has_header = FALSE;
    s32 has_end = FALSE;
    u32 bits;
    s32 i;

    png->data_bytes = 0;
    png->has_key = FALSE;
    png->palette_count = 0;
    for (i = 0; i < PALETTE_ENTRIES; i++) {
        png->palette[i][0] = 0;
        png->palette[i][1] = 0;
        png->palette[i][2] = 0;
        png->palette[i][3] = BYTE_MAX;
    }
    if (size < SIGNATURE_BYTES) {
        png_Say(problem, room, "too short to be a PNG");
        return FALSE;
    }
    for (i = 0; i < SIGNATURE_BYTES; i++) {
        if (file[i] != g_png_signature[i]) {
            png_Say(problem, room, "not a PNG");
            return FALSE;
        }
    }
    while (!has_end) {
        u32 length;
        const u8 *data;

        if (size - at < CHUNK_HEAD_BYTES + CHUNK_SUM_BYTES) {
            png_Say(problem, room, "it ends before its IEND");
            return FALSE;
        }
        length = png_Big32(file + at);
        if (length > size - at - CHUNK_HEAD_BYTES - CHUNK_SUM_BYTES) {
            png_Say(problem, room, "a chunk runs past the file's end");
            return FALSE;
        }
        data = file + at + CHUNK_HEAD_BYTES;
        if (png_IsChunk(file + at + 4, "IHDR")) {
            if (length != HEADER_BYTES) {
                png_Say(problem, room, "its header is not 13 bytes");
                return FALSE;
            }
            png->w = (s32)png_Big32(data);
            png->h = (s32)png_Big32(data + 4);
            png->depth = data[8]; /* bits a sample */
            png->color = data[9]; /* the colour type */
            /* the one compression and the one filter method; no
             * interlace */
            if (data[10] != 0 || data[11] != 0) {
                png_Say(problem, room, "a method no PNG has");
                return FALSE;
            }
            if (data[12] != 0) { /* interlaced */
                png_Say(problem, room,
                        "it is interlaced: save it without (Adam7 is not "
                        "read)");
                return FALSE;
            }
            has_header = TRUE;
        } else if (png_IsChunk(file + at + 4, "PLTE")) {
            png->palette_count = (s32)(length / 3);
            if (png->palette_count > PALETTE_ENTRIES) {
                png->palette_count = PALETTE_ENTRIES;
            }
            for (i = 0; i < png->palette_count; i++) {
                png->palette[i][0] = data[i * 3];
                png->palette[i][1] = data[i * 3 + 1];
                png->palette[i][2] = data[i * 3 + 2];
            }
        } else if (png_IsChunk(file + at + 4, "tRNS") && has_header) {
            if (png->color == COLOR_PALETTE) {
                for (i = 0; i < (s32)length && i < PALETTE_ENTRIES; i++) {
                    png->palette[i][3] = data[i];
                }
            } else if (png->color == COLOR_GRAY && length >= 2) {
                png->has_key = TRUE;
                png->key[0] = ((u32)data[0] << BITS_PER_BYTE) | data[1];
            } else if (png->color == COLOR_RGB && length >= 6) { /* 3 of 16 */
                png->has_key = TRUE;
                for (i = 0; i < 3; i++) {
                    png->key[i] =
                        ((u32)data[i * 2] << BITS_PER_BYTE) | data[i * 2 + 1];
                }
            }
        } else if (png_IsChunk(file + at + 4, "IDAT")) {
            png->data_bytes += length;
        } else if (png_IsChunk(file + at + 4, "IEND")) {
            has_end = TRUE;
        }
        at += CHUNK_HEAD_BYTES + length + CHUNK_SUM_BYTES;
    }
    if (!has_header) {
        png_Say(problem, room, "it has no header");
        return FALSE;
    }
    png->channels = png_ChannelsOf(png->color);
    if (png->channels == 0 || !png_DepthIsAllowed(png->color, png->depth)) {
        png_Say(problem, room, "a colour type or depth no PNG has");
        return FALSE;
    }
    if (png->w <= 0 || png->h <= 0 || png->w > PORT_PNG_SIDE_MAX ||
        png->h > PORT_PNG_SIDE_MAX) {
        png_Say(problem, room, "no pixels, or more than 8192 each way");
        return FALSE;
    }
    if (png->data_bytes == 0) {
        png_Say(problem, room, "it has no IDAT");
        return FALSE;
    }
    bits = (u32)png->w * (u32)(png->channels * png->depth);
    png->row_bytes = (bits + BITS_PER_BYTE - 1) / BITS_PER_BYTE;
    png->rows_bytes = (u32)png->h * (1 + png->row_bytes);
    return TRUE;
}

s32 port_PngMeasure(
    const u8 *file, u32 size, PortPngInfo *info, char *problem, u32 room) {
    PngFile png;

    if (!png_Read(file, size, &png, problem, room)) {
        return FALSE;
    }
    info->w = png.w;
    info->h = png.h;
    info->work_bytes = png.data_bytes + png.rows_bytes;
    return TRUE;
}

/* ---- inflate (RFC 1950, 1951) ---- */

#define CODE_BITS_MAX 15
#define LITERALS_MAX 288 /* the literal and length alphabet's size */
#define DISTANCES_MAX 30
#define LENGTH_CODES 29
#define CODE_LENGTH_CODES 19
#define END_OF_BLOCK 256
#define FIRST_LENGTH 257
/* the fixed codes' lengths, by where the literals' runs begin */
#define FIXED_NINE_FROM 144
#define FIXED_SEVEN_FROM 256
#define FIXED_EIGHT_AGAIN_FROM 280
#define FIXED_DISTANCE_BITS 5
/* the zlib header: deflate's method, and the bit that says a dictionary
 * is wanted, which a PNG never has */
#define ZLIB_METHOD_MASK 0x0F
#define ZLIB_DEFLATE 8
#define ZLIB_DICTIONARY 0x20
#define ZLIB_CHECK 31

enum { BLOCK_STORED = 0, BLOCK_FIXED = 1, BLOCK_DYNAMIC = 2 };

/* the code lengths' code: the order its lengths come in, and what its
 * last three symbols repeat */
enum { REPEAT_LAST = 16, REPEAT_ZERO = 17, REPEAT_ZERO_LONG = 18 };

static const u8 g_png_length_order[CODE_LENGTH_CODES] = {
    16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15};

/* what a length code and a distance code begin at, and how many bits
 * more each reads */
static const u16 g_png_length_base[LENGTH_CODES] = {
    3,  4,  5,  6,  7,  8,  9,  10, 11,  13,  15,  17,  19,  23, 27,
    31, 35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258};
static const u8 g_png_length_extra[LENGTH_CODES] = {
    0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2,
    2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0};
static const u16 g_png_distance_base[DISTANCES_MAX] = {
    1,    2,    3,    4,    5,    7,    9,    13,    17,    25,
    33,   49,   65,   97,   129,  193,  257,  385,   513,   769,
    1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577};
static const u8 g_png_distance_extra[DISTANCES_MAX] = {
    0, 0, 0, 0, 1, 1, 2, 2,  3,  3,  4,  4,  5,  5,  6,
    6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13};

typedef struct {
    const u8 *in;
    u32 in_size;
    u32 in_at;
    u32 bits;
    s32 bit_count;
    u8 *out;
    u32 out_size;
    u32 out_at;
    s32 failed;
} PngInflate;

/* A code: how many codes there are of each length, and the symbols in
 * the order of their codes. */
typedef struct {
    s16 counts[CODE_BITS_MAX + 1];
    s16 symbols[LITERALS_MAX];
} PngCode;

/* `need` bits of the stream, the first of them lowest. */
static u32 png_Bits(PngInflate *z, s32 need) {
    u32 value = z->bits;

    while (z->bit_count < need) {
        if (z->in_at >= z->in_size) {
            z->failed = TRUE;
            return 0;
        }
        value |= (u32)z->in[z->in_at++] << z->bit_count;
        z->bit_count += BITS_PER_BYTE;
    }
    z->bits = value >> need;
    z->bit_count -= need;
    return value & ((1U << need) - 1);
}

/* The code whose lengths are these: FALSE where more codes are asked of a
 * length than there are. A code with room to spare is allowed, as zlib
 * allows a distance code of one symbol. */
static s32 png_MakeCode(PngCode *code, const u8 *lengths, s32 count) {
    s16 offsets[CODE_BITS_MAX + 1];
    s32 left = 1;
    s32 i;

    for (i = 0; i <= CODE_BITS_MAX; i++) {
        code->counts[i] = 0;
    }
    for (i = 0; i < count; i++) {
        code->counts[lengths[i]]++;
    }
    for (i = 1; i <= CODE_BITS_MAX; i++) {
        left = left * 2 - code->counts[i];
        if (left < 0) {
            return FALSE;
        }
    }
    offsets[1] = 0;
    for (i = 1; i < CODE_BITS_MAX; i++) {
        offsets[i + 1] = (s16)(offsets[i] + code->counts[i]);
    }
    for (i = 0; i < count; i++) {
        if (lengths[i] != 0) {
            code->symbols[offsets[lengths[i]]++] = (s16)i;
        }
    }
    return TRUE;
}

/* The next symbol of `code`, or -1. Codes are written from their highest
 * bit, so the bits are taken one at a time; the codes of each length
 * follow those of the length before. */
static s32 png_Decode(PngInflate *z, const PngCode *code) {
    s32 value = 0;
    s32 first = 0;
    s32 index = 0;
    s32 length;

    for (length = 1; length <= CODE_BITS_MAX; length++) {
        s32 count = code->counts[length];

        value |= (s32)png_Bits(z, 1);
        if (value - first < count) {
            return code->symbols[index + value - first];
        }
        index += count;
        first = (first + count) * 2;
        value *= 2;
    }
    z->failed = TRUE;
    return -1;
}

/* A block's symbols, to its end. */
static s32 png_Symbols(
    PngInflate *z, const PngCode *literals, const PngCode *distances) {
    for (;;) {
        s32 symbol = png_Decode(z, literals);

        if (z->failed || symbol < 0) {
            return FALSE;
        }
        if (symbol < END_OF_BLOCK) {
            if (z->out_at >= z->out_size) {
                return FALSE;
            }
            z->out[z->out_at++] = (u8)symbol;
        } else if (symbol == END_OF_BLOCK) {
            return TRUE;
        } else {
            u32 length;
            u32 distance;
            s32 which = symbol - FIRST_LENGTH;

            if (which >= LENGTH_CODES) {
                return FALSE;
            }
            length = g_png_length_base[which] +
                     png_Bits(z, g_png_length_extra[which]);
            which = png_Decode(z, distances);
            if (z->failed || which < 0 || which >= DISTANCES_MAX) {
                return FALSE;
            }
            distance = g_png_distance_base[which] +
                       png_Bits(z, g_png_distance_extra[which]);
            if (z->failed || distance > z->out_at ||
                length > z->out_size - z->out_at) {
                return FALSE;
            }
            while (length-- > 0) {
                z->out[z->out_at] = z->out[z->out_at - distance];
                z->out_at++;
            }
        }
    }
}

/* A stored block: its length and that negated, then its bytes as they
 * are. */
static s32 png_Stored(PngInflate *z) {
    u32 length;
    u32 check;

    /* to the byte's boundary */
    z->bits = 0;
    z->bit_count = 0;
    if (z->in_size - z->in_at < 4) {
        return FALSE;
    }
    length = z->in[z->in_at] | ((u32)z->in[z->in_at + 1] << BITS_PER_BYTE);
    check = z->in[z->in_at + 2] | ((u32)z->in[z->in_at + 3] << BITS_PER_BYTE);
    z->in_at += 4;
    if ((length ^ check) != 0xFFFF || length > z->in_size - z->in_at ||
        length > z->out_size - z->out_at) {
        return FALSE;
    }
    while (length-- > 0) {
        z->out[z->out_at++] = z->in[z->in_at++];
    }
    return TRUE;
}

/* A block of the fixed codes RFC 1951 writes down. */
static s32 png_Fixed(PngInflate *z) {
    u8 lengths[LITERALS_MAX];
    PngCode literals;
    PngCode distances;
    s32 i;

    for (i = 0; i < LITERALS_MAX; i++) {
        /* RFC 1951, 3.2.6: bits of each literal's and length's code */
        lengths[i] = i < FIXED_NINE_FROM          ? 8  /* bits */
                     : i < FIXED_SEVEN_FROM       ? 9  /* bits */
                     : i < FIXED_EIGHT_AGAIN_FROM ? 7  /* bits */
                                                  : 8; /* bits */
    }
    png_MakeCode(&literals, lengths, LITERALS_MAX);
    for (i = 0; i < DISTANCES_MAX; i++) {
        lengths[i] = FIXED_DISTANCE_BITS;
    }
    png_MakeCode(&distances, lengths, DISTANCES_MAX);
    return png_Symbols(z, &literals, &distances);
}

/* A block that brings its codes: the lengths of a code for the codes'
 * lengths, those lengths run-length coded, then the symbols. */
static s32 png_Dynamic(PngInflate *z) {
    u8 lengths[LITERALS_MAX + DISTANCES_MAX];
    PngCode literals;
    PngCode distances;
    /* RFC 1951, 3.2.7: how many of each code's lengths follow, in 5, 5
     * and 4 bits */
    s32 literal_count = (s32)png_Bits(z, 5) + FIRST_LENGTH; /* HLIT */
    s32 distance_count = (s32)png_Bits(z, 5) + 1;           /* HDIST */
    s32 length_count = (s32)png_Bits(z, 4) + 4;             /* HCLEN */
    s32 at = 0;
    s32 i;

    if (z->failed || literal_count > LITERALS_MAX - 2 ||
        distance_count > DISTANCES_MAX) {
        return FALSE;
    }
    for (i = 0; i < CODE_LENGTH_CODES; i++) {
        lengths[g_png_length_order[i]] =
            i < length_count ? (u8)png_Bits(z, 3) : 0;
    }
    if (!png_MakeCode(&literals, lengths, CODE_LENGTH_CODES)) {
        return FALSE;
    }
    while (at < literal_count + distance_count) {
        s32 symbol = png_Decode(z, &literals);
        s32 repeat;
        u8 length = 0;

        if (z->failed || symbol < 0) {
            return FALSE;
        }
        if (symbol < REPEAT_LAST) {
            lengths[at++] = (u8)symbol;
            continue;
        }
        if (symbol == REPEAT_LAST) {
            if (at == 0) {
                return FALSE;
            }
            length = lengths[at - 1];
            repeat = 3 + (s32)png_Bits(z, 2);
        } else if (symbol == REPEAT_ZERO) {
            repeat = 3 + (s32)png_Bits(z, 3);
        } else {
            repeat = 11 + (s32)png_Bits(z, 7); /* 11 to 138 zeroes */
        }
        if (at + repeat > literal_count + distance_count) {
            return FALSE;
        }
        while (repeat-- > 0) {
            lengths[at++] = length;
        }
    }
    if (lengths[END_OF_BLOCK] == 0 ||
        !png_MakeCode(&literals, lengths, literal_count) ||
        !png_MakeCode(&distances, lengths + literal_count, distance_count)) {
        return FALSE;
    }
    return png_Symbols(z, &literals, &distances);
}

/* A zlib stream into `out`: TRUE where it held exactly `out_size`
 * bytes. */
static s32 png_Inflate(const u8 *in, u32 in_size, u8 *out, u32 out_size) {
    PngInflate z;
    s32 is_last = FALSE;

    if (in_size < 2 || (in[0] & ZLIB_METHOD_MASK) != ZLIB_DEFLATE ||
        (in[1] & ZLIB_DICTIONARY) != 0 ||
        (((u32)in[0] << BITS_PER_BYTE) | in[1]) % ZLIB_CHECK != 0) {
        return FALSE;
    }
    z.in = in;
    z.in_size = in_size;
    z.in_at = 2;
    z.bits = 0;
    z.bit_count = 0;
    z.out = out;
    z.out_size = out_size;
    z.out_at = 0;
    z.failed = FALSE;
    while (!is_last) {
        s32 kind;
        s32 ok;

        is_last = (s32)png_Bits(&z, 1);
        kind = (s32)png_Bits(&z, 2);
        if (kind == BLOCK_STORED) {
            ok = png_Stored(&z);
        } else if (kind == BLOCK_FIXED) {
            ok = png_Fixed(&z);
        } else if (kind == BLOCK_DYNAMIC) {
            ok = png_Dynamic(&z);
        } else {
            ok = FALSE;
        }
        if (!ok || z.failed) {
            return FALSE;
        }
    }
    return z.out_at == out_size;
}

/* ---- the rows ---- */

static u8 png_Paeth(u8 left, u8 up, u8 up_left) {
    s32 guess = (s32)left + up - up_left;
    s32 to_left = guess > left ? guess - left : left - guess;
    s32 to_up = guess > up ? guess - up : up - guess;
    s32 to_up_left = guess > up_left ? guess - up_left : up_left - guess;

    if (to_left <= to_up && to_left <= to_up_left) {
        return left;
    }
    return to_up <= to_up_left ? up : up_left;
}

/* Each row's filter undone, in place, from the top. */
static s32 png_Unfilter(const PngFile *png, u8 *rows) {
    u32 stride = 1 + png->row_bytes;
    /* the byte a filter looks back to: a pixel's bytes before, and at
     * least one */
    u32 back = (u32)(png->channels * png->depth) / BITS_PER_BYTE;
    s32 y;

    if (back == 0) {
        back = 1;
    }
    for (y = 0; y < png->h; y++) {
        u8 *line = rows + (u32)y * stride + 1;
        const u8 *prior = y > 0 ? line - stride : NULL;
        s32 filter = line[-1];
        u32 i;

        for (i = 0; i < png->row_bytes; i++) {
            u8 left = i >= back ? line[i - back] : 0;
            u8 up = prior != NULL ? prior[i] : 0;
            u8 up_left = prior != NULL && i >= back ? prior[i - back] : 0;

            switch (filter) {
            case FILTER_NONE:
                break;
            case FILTER_SUB:
                line[i] = (u8)(line[i] + left);
                break;
            case FILTER_UP:
                line[i] = (u8)(line[i] + up);
                break;
            case FILTER_AVERAGE:
                line[i] = (u8)(line[i] + ((u32)left + up) / 2);
                break;
            case FILTER_PAETH:
                line[i] = (u8)(line[i] + png_Paeth(left, up, up_left));
                break;
            default:
                return FALSE;
            }
        }
    }
    return TRUE;
}

/* Sample number `index` of a row, at the file's depth. */
static u32 png_Sample(const PngFile *png, const u8 *line, u32 index) {
    u32 bit;

    if (png->depth == DEPTH_WIDE) {
        return ((u32)line[index * 2] << BITS_PER_BYTE) | line[index * 2 + 1];
    }
    if (png->depth == BITS_PER_BYTE) {
        return line[index];
    }
    /* several to a byte, the first highest */
    bit = index * (u32)png->depth;
    return ((u32)line[bit / BITS_PER_BYTE] >>
            (BITS_PER_BYTE - png->depth - (s32)(bit % BITS_PER_BYTE))) &
           ((1U << png->depth) - 1);
}

/* A sample as a byte: the high one of two, or one of fewer bits spread
 * over the byte's range. */
static u8 png_ByteOf(const PngFile *png, u32 sample) {
    if (png->depth == DEPTH_WIDE) {
        return (u8)(sample >> BITS_PER_BYTE);
    }
    return (u8)(sample * BYTE_MAX / ((1U << png->depth) - 1));
}

/* The unfiltered rows as four bytes a pixel: a palette's entries, grey
 * spread to three channels, a colour that is transparent made clear. */
static void png_Pixels(const PngFile *png, const u8 *rows, u8 *rgba) {
    u32 stride = 1 + png->row_bytes;
    s32 x;
    s32 y;

    for (y = 0; y < png->h; y++) {
        const u8 *line = rows + (u32)y * stride + 1;

        for (x = 0; x < png->w; x++) {
            u8 *out = rgba + ((u32)y * (u32)png->w + (u32)x) * RGBA;
            u32 first = (u32)x * (u32)png->channels;
            u32 sample[RGBA];
            s32 i;

            for (i = 0; i < png->channels; i++) {
                sample[i] = png_Sample(png, line, first + (u32)i);
            }
            switch (png->color) {
            case COLOR_PALETTE:
                for (i = 0; i < RGBA; i++) {
                    out[i] = png->palette[sample[0] & BYTE_MASK][i];
                }
                break;
            case COLOR_GRAY:
            case COLOR_GRAY_ALPHA:
                out[0] = png_ByteOf(png, sample[0]);
                out[1] = out[0];
                out[2] = out[0];
                out[3] =
                    png->color == COLOR_GRAY_ALPHA ? png_ByteOf(png, sample[1])
                    : png->has_key && sample[0] == png->key[0]
                        ? 0
                        : BYTE_MAX;
                break;
            default:
                out[0] = png_ByteOf(png, sample[0]);
                out[1] = png_ByteOf(png, sample[1]);
                out[2] = png_ByteOf(png, sample[2]);
                out[3] =
                    png->color == COLOR_RGB_ALPHA ? png_ByteOf(png, sample[3])
                    : png->has_key && sample[0] == png->key[0] &&
                            sample[1] == png->key[1] && sample[2] == png->key[2]
                        ? 0
                        : BYTE_MAX;
                break;
            }
        }
    }
}

/* The chunks' data gathered, inflated, unfiltered and made pixels
 * (port_png.h). */
s32 port_PngDecode(
    const u8 *file, u32 size, u8 *work, u8 *rgba, char *problem, u32 room) {
    PngFile png;
    u8 *rows;
    u32 at = SIGNATURE_BYTES;
    u32 gathered = 0;

    if (!png_Read(file, size, &png, problem, room)) {
        return FALSE;
    }
    /* the IDAT chunks' data, end to end: one zlib stream */
    for (;;) {
        u32 length = png_Big32(file + at);

        if (png_IsChunk(file + at + 4, "IDAT")) {
            u32 i;

            for (i = 0; i < length; i++) {
                work[gathered + i] = file[at + CHUNK_HEAD_BYTES + i];
            }
            gathered += length;
        } else if (png_IsChunk(file + at + 4, "IEND")) {
            break;
        }
        at += CHUNK_HEAD_BYTES + length + CHUNK_SUM_BYTES;
    }
    rows = work + png.data_bytes;
    if (!png_Inflate(work, png.data_bytes, rows, png.rows_bytes)) {
        png_Say(problem, room, "its data are damaged or cut short");
        return FALSE;
    }
    if (!png_Unfilter(&png, rows)) {
        png_Say(problem, room, "a row has a filter no PNG has");
        return FALSE;
    }
    png_Pixels(&png, rows, rgba);
    return TRUE;
}

/* ---- writing ---- */

#define COLOR_RGBA_DEPTH 8
#define STORED_BLOCK_MAX 0xFFFF /* the most bytes a stored block holds */
#define STORED_HEAD_BYTES 5     /* its kind, its length and that negated */
#define ZLIB_HEAD_BYTES 2
#define ZLIB_SUM_BYTES 4
#define ZLIB_HEAD_METHOD 0x78 /* deflate, a window of 32 KB */
#define ZLIB_HEAD_CHECK 0x01  /* makes the two bytes a multiple of 31 */
#define CRC_POLYNOMIAL 0xEDB88320U
#define ADLER_MODULUS 65521
#define HALF_SHIFT 16

typedef struct {
    u8 *out;
    u32 at;
    u32 crc_from; /* where the chunk's sum begins: its four letters */
} PngWriter;

static void png_Put(PngWriter *writer, u32 byte) {
    writer->out[writer->at++] = (u8)byte;
}

static void png_Put32(PngWriter *writer, u32 word) {
    png_Put(writer, word >> (3 * BITS_PER_BYTE));
    png_Put(writer, (word >> (2 * BITS_PER_BYTE)) & BYTE_MASK);
    png_Put(writer, (word >> BITS_PER_BYTE) & BYTE_MASK);
    png_Put(writer, word & BYTE_MASK);
}

static void png_Begin(PngWriter *writer, u32 length, const char *name) {
    s32 i;

    png_Put32(writer, length);
    writer->crc_from = writer->at;
    for (i = 0; i < 4; i++) {
        png_Put(writer, (u8)name[i]);
    }
}

/* The chunk's sum, a bit at a time: a dump is written once. */
static void png_End(PngWriter *writer) {
    u32 crc = 0xFFFFFFFF;
    u32 i;

    for (i = writer->crc_from; i < writer->at; i++) {
        s32 bit;

        crc ^= writer->out[i];
        for (bit = 0; bit < BITS_PER_BYTE; bit++) {
            crc = (crc >> 1) ^ (CRC_POLYNOMIAL & (0U - (crc & 1)));
        }
    }
    png_Put32(writer, ~crc);
}

/* The rows' bytes, each a filter's byte of none and the pixels. */
static u32 png_RowsBytes(s32 w, s32 h) { return (u32)h * (1 + (u32)w * RGBA); }

static u32 png_StreamBytes(s32 w, s32 h) {
    u32 data = png_RowsBytes(w, h);
    u32 blocks = (data + STORED_BLOCK_MAX - 1) / STORED_BLOCK_MAX;

    return ZLIB_HEAD_BYTES + blocks * STORED_HEAD_BYTES + data + ZLIB_SUM_BYTES;
}

u32 port_PngEncodedSize(s32 w, s32 h) {
    return SIGNATURE_BYTES +
           (CHUNK_HEAD_BYTES + HEADER_BYTES + CHUNK_SUM_BYTES) +
           (CHUNK_HEAD_BYTES + png_StreamBytes(w, h) + CHUNK_SUM_BYTES) +
           (CHUNK_HEAD_BYTES + CHUNK_SUM_BYTES);
}

/* The signature, the header, the rows in stored blocks with their sum,
 * and the end (port_png.h). */
u32 port_PngEncode(const u8 *rgba, s32 w, s32 h, u8 *out) {
    PngWriter writer;
    u32 data = png_RowsBytes(w, h);
    u32 row = 1 + (u32)w * RGBA;
    u32 low = 1; /* the Adler-32 sum's two halves */
    u32 high = 0;
    u32 left = 0;
    u32 i;

    writer.out = out;
    writer.at = 0;
    for (i = 0; i < SIGNATURE_BYTES; i++) {
        png_Put(&writer, g_png_signature[i]);
    }
    png_Begin(&writer, HEADER_BYTES, "IHDR");
    png_Put32(&writer, (u32)w);
    png_Put32(&writer, (u32)h);
    png_Put(&writer, COLOR_RGBA_DEPTH);
    png_Put(&writer, COLOR_RGB_ALPHA);
    png_Put(&writer, 0); /* the one compression, filter and no interlace */
    png_Put(&writer, 0);
    png_Put(&writer, 0);
    png_End(&writer);
    png_Begin(&writer, png_StreamBytes(w, h), "IDAT");
    png_Put(&writer, ZLIB_HEAD_METHOD);
    png_Put(&writer, ZLIB_HEAD_CHECK);
    for (i = 0; i < data; i++) {
        u32 in_row = i % row;
        u32 byte;

        if (left == 0) {
            u32 rest = data - i;

            left = rest < STORED_BLOCK_MAX ? rest : STORED_BLOCK_MAX;
            png_Put(&writer, rest <= STORED_BLOCK_MAX ? 1 : 0); /* last? */
            png_Put(&writer, left & BYTE_MASK);
            png_Put(&writer, left >> BITS_PER_BYTE);
            png_Put(&writer, ~left & BYTE_MASK);
            png_Put(&writer, (~left >> BITS_PER_BYTE) & BYTE_MASK);
        }
        byte = in_row == 0 ? FILTER_NONE
                           : rgba[(i / row) * (u32)w * RGBA + (in_row - 1)];
        png_Put(&writer, byte);
        low = (low + byte) % ADLER_MODULUS;
        high = (high + low) % ADLER_MODULUS;
        left--;
    }
    png_Put32(&writer, (high << HALF_SHIFT) | low);
    png_End(&writer);
    png_Begin(&writer, 0, "IEND");
    png_End(&writer);
    return writer.at;
}
