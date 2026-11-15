/* The PlayStation's picture decompressor in C (docs/PORT.md, "The
 * movie"): the MDEC, and Sony's libpress on top of it.
 *
 * The console's movie player (movovl/) is compiled into the port as it
 * is, and it reaches the chip only through libpress's DecDCT... calls.
 * Sony's libpress module is one file that holds both those calls and the
 * driver that writes the chip's registers and starts its two DMA
 * channels, so the whole module is the port's, by the rule that Sony's
 * code is compiled in where it is plain C on memory and replaced where
 * it talks to the console (docs/PORT.md, "Sony's libraries in the
 * port"). The calls here do what Sony's do; under them is the chip.
 *
 * What the chip does, and what a movie asks of it:
 *
 *   - DecDCTvlc expands a frame's bitstream into run-level pairs. That
 *     half is software on the console too (libpress's own assembly), and
 *     it is written again here from the format;
 *   - DecDCTin gives the chip those pairs. The chip makes an 8 by 8
 *     block of coefficients of each, multiplies them by the quantisation
 *     table and the frame's scale, runs the inverse cosine transform,
 *     and turns six blocks (Cr, Cb and four of luminance) into a
 *     macroblock of 16 by 16 pixels;
 *   - DecDCTout takes those pixels out, as many words as it is asked
 *     for, and runs the callback the player hung on the transfer's end.
 *     On the console that is the DMA's interrupt; here the transfer is
 *     done as it is asked for, as the port does the sound chip's
 *     (sound.c), and the callback runs at once.
 *
 * The movie's pixels go to video memory from there, a strip at a time
 * (movovl/src/movie.c).
 *
 * The bitstream (docs/formats/STR.md): 16-bit little-endian words read
 * from the top bit down. A frame begins with four halfwords: the length
 * of the run-level data in words, 0x3800, the quantisation scale and the
 * version. Then, for each block: ten bits of the lowest coefficient, and
 * then variable-length codes, each a run of zero coefficients and the
 * next one's size, until the code that ends the block. The code table is
 * the one MPEG-1 has for the coefficients of a block, and the port's
 * copy of it is held to the table in the original library
 * (tests/port/mdec.py reads it out of movovl/asm/data/
 * libpress_vlc.data.s). Version 3, which codes the lowest coefficient as
 * a difference, is not written: the game's one movie is version 2. */
#include "common.h"
#include "port_movie.h"
#include "bb2.h"

/* The chip works in macroblocks of 16 by 16 pixels: six blocks of 8 by
 * 8 coefficients, the two of colour at half the width and height. */
#define BLOCK_SIDE 8
#define BLOCK_VALUES (BLOCK_SIDE * BLOCK_SIDE)
#define MACROBLOCK_SIDE 16
#define MACROBLOCK_PIXELS (MACROBLOCK_SIDE * MACROBLOCK_SIDE)
#define LUMA_BLOCKS 4
#define BYTES_24BIT 3
#define BYTES_15BIT 2
#define MACROBLOCK_BYTES_MAX (MACROBLOCK_PIXELS * BYTES_24BIT)

/* A run-level halfword: six bits of run under ten of value. The first of
 * a block has the quantisation scale where the run is. */
#define RL_RUN_SHIFT 10
#define RL_RUN_MASK 0x3F
#define RL_VALUE_MASK 0x3FF
#define RL_VALUE_SIGN 0x200
#define RL_END_OF_BLOCK 0xFE00
/* What the code table gives for the escape code, which no run and value
 * can be: the six bits of run and ten of value follow it in the
 * bitstream. */
#define RL_ESCAPE 0x7C1F

/* A coefficient is held to ten bits with a sign. */
#define COEFFICIENT_MIN (-0x400)
#define COEFFICIENT_MAX 0x3FF
/* A coefficient after the lowest is its value times its place in the
 * quantisation table and the frame's scale, with half of eight added and
 * eight taken out. The lowest is its value times the table's first entry
 * alone. */
#define RL_DEQUANT_SHIFT 3
#define RL_DEQUANT_HALF 4
/* The transform's fixed point: the cosine table is brought down by eight
 * and each sum by 8192, with half of that added first. */
#define IDCT_SCALE_SHIFT 3
#define IDCT_SUM_SHIFT 13
#define IDCT_SUM_HALF 0x0FFF

/* The command word DecDCTvlc writes in front of the run-level data:
 * "decode macroblocks", with the length in words. The two bits DecDCTin
 * sets are the depth (clear for 24-bit colour) and the bit to put in the
 * top of each 15-bit pixel. */
#define MDEC_DECODE_COMMAND 0x30000000U
#define MDEC_DEPTH_15BIT 0x08000000U
#define MDEC_SET_MASK_BIT 0x02000000U
#define MDEC_SIZE_MASK 0xFFFFU

/* The bitstream's header: these halfwords in front of a frame. */
#define FRAME_HEADER_HALFWORDS 4
#define FRAME_MAGIC 0x3800
#define FRAME_VERSION_2 2

/* The longest code of the table, and the window a code is looked up in:
 * every code of more than 13 bits begins with eight zeroes, so the
 * first table answers for the next 13 bits and the second for the nine
 * after those eight. */
#define CODE_BITS_MAX 17
#define SHORT_INDEX_BITS 13
#define SHORT_INDEX_COUNT (1 << SHORT_INDEX_BITS)
#define LONG_PREFIX_BITS 8
#define LONG_INDEX_BITS 9
#define LONG_INDEX_COUNT (1 << LONG_INDEX_BITS)

/* The bits an escape's run and value take after the code. */
#define ESCAPE_BITS 16
/* The bits of the lowest coefficient of a block, version 2. */
#define DC_BITS 10
/* A halfword of the bitstream, and the place of its top bit, which is
 * the one read first. */
#define HALFWORD_BITS 16
#define HALFWORD_TOP_BIT 15
/* The top bit of a 15-bit pixel, which the chip sets or clears as the
 * command word says. */
#define PIXEL_MASK_BIT 0x8000

/* ---- the chip's own tables ---- */

/* The quantisation tables the chip is loaded with at a reset, in the
 * order the coefficients are read in: the same for luminance and for
 * colour. These are the bytes of Sony's libpress (the two halves of its
 * mdec_iq, less the command word that loads them). */
static const u8 g_port_mdec_default_quant[BLOCK_VALUES] = {
    2,  16, 16, 19, 16, 19, 22, 22, 22, 22, 22, 22, 26, 24, 26, 27,
    27, 27, 26, 26, 26, 26, 27, 27, 27, 29, 29, 29, 34, 34, 34, 29,
    29, 29, 27, 27, 29, 29, 32, 32, 34, 34, 37, 38, 37, 35, 35, 34,
    35, 38, 38, 40, 40, 40, 48, 48, 46, 46, 56, 56, 58, 69, 69, 83,
};

/* The cosine table the chip is loaded with, row by row: entry [u][x] is
 * the cosine of (2x+1)u pi/16 in 32768ths, with the first row brought
 * down by the square root of two. These are the halfwords of Sony's
 * mdec_coef. */
static const s16 g_port_mdec_default_scale[BLOCK_VALUES] = {
    23170,  23170,  23170,  23170,  23170,  23170,  23170,  23170,
    32138,  27245,  18204,  6392,   -6393,  -18205, -27246, -32139,
    30273,  12539,  -12540, -30274, -30274, -12540, 12539,  30273,
    27245,  -6393,  -32139, -18205, 18204,  32138,  6392,   -27246,
    23170,  -23171, -23171, 23170,  23170,  -23171, -23171, 23170,
    18204,  -32139, 6392,   27245,  -27246, -6393,  32138,  -18205,
    12539,  -30274, 30273,  -12540, -12540, 30273,  -30274, 12539,
    6392,   -18205, 27245,  -32139, 32138,  -27246, 18204,  -6393,
};

/* Where the nth coefficient read belongs in the block: the zigzag, out
 * from the corner. */
static const u8 g_port_mdec_zigzag[BLOCK_VALUES] = {
    0,  1,  8,  16, 9,  2,  3,  10, 17, 24, 32, 25, 18, 11, 4,  5,
    12, 19, 26, 33, 40, 48, 41, 34, 27, 20, 13, 6,  7,  14, 21, 28,
    35, 42, 49, 56, 57, 50, 43, 36, 29, 22, 15, 23, 30, 37, 44, 51,
    58, 59, 52, 45, 38, 31, 39, 46, 53, 60, 61, 54, 47, 55, 62, 63,
};

/* ---- the code table ---- */

/* One code of the bitstream: its bits without the sign that ends it, how
 * many bits it is with that sign, and what it means. The same bits with
 * the last bit set are the same run and the value with its sign turned.
 * This is MPEG-1's table for the coefficients of a block; every row is
 * held to the table of the original library by tests/port/mdec.py. */
typedef struct {
    u16 bits;
    u8 length;
    u8 run;
    s16 level;
} PortMdecCode;

static const PortMdecCode g_port_mdec_codes[] = {
    {0x0003, 3, 0, 1},    {0x0003, 4, 1, 1},    {0x0004, 5, 0, 2},
    {0x0005, 5, 2, 1},    {0x0005, 6, 0, 3},    {0x0006, 6, 4, 1},
    {0x0007, 6, 3, 1},    {0x0004, 7, 7, 1},    {0x0005, 7, 6, 1},
    {0x0006, 7, 1, 2},    {0x0007, 7, 5, 1},    {0x0004, 8, 2, 2},
    {0x0005, 8, 9, 1},    {0x0006, 8, 0, 4},    {0x0007, 8, 8, 1},
    {0x0020, 9, 13, 1},   {0x0021, 9, 0, 6},    {0x0022, 9, 12, 1},
    {0x0023, 9, 11, 1},   {0x0024, 9, 3, 2},    {0x0025, 9, 1, 3},
    {0x0026, 9, 0, 5},    {0x0027, 9, 10, 1},   {0x0008, 11, 16, 1},
    {0x0009, 11, 5, 2},   {0x000A, 11, 0, 7},   {0x000B, 11, 2, 3},
    {0x000C, 11, 1, 4},   {0x000D, 11, 15, 1},  {0x000E, 11, 14, 1},
    {0x000F, 11, 4, 2},   {0x0010, 13, 0, 11},  {0x0011, 13, 8, 2},
    {0x0012, 13, 4, 3},   {0x0013, 13, 0, 10},  {0x0014, 13, 2, 4},
    {0x0015, 13, 7, 2},   {0x0016, 13, 21, 1},  {0x0017, 13, 20, 1},
    {0x0018, 13, 0, 9},   {0x0019, 13, 19, 1},  {0x001A, 13, 18, 1},
    {0x001B, 13, 1, 5},   {0x001C, 13, 3, 3},   {0x001D, 13, 0, 8},
    {0x001E, 13, 6, 2},   {0x001F, 13, 17, 1},  {0x0010, 14, 10, 2},
    {0x0011, 14, 9, 2},   {0x0012, 14, 5, 3},   {0x0013, 14, 3, 4},
    {0x0014, 14, 2, 5},   {0x0015, 14, 1, 7},   {0x0016, 14, 1, 6},
    {0x0017, 14, 0, 15},  {0x0018, 14, 0, 14},  {0x0019, 14, 0, 13},
    {0x001A, 14, 0, 12},  {0x001B, 14, 26, 1},  {0x001C, 14, 25, 1},
    {0x001D, 14, 24, 1},  {0x001E, 14, 23, 1},  {0x001F, 14, 22, 1},
    {0x0010, 15, 0, 31},  {0x0011, 15, 0, 30},  {0x0012, 15, 0, 29},
    {0x0013, 15, 0, 28},  {0x0014, 15, 0, 27},  {0x0015, 15, 0, 26},
    {0x0016, 15, 0, 25},  {0x0017, 15, 0, 24},  {0x0018, 15, 0, 23},
    {0x0019, 15, 0, 22},  {0x001A, 15, 0, 21},  {0x001B, 15, 0, 20},
    {0x001C, 15, 0, 19},  {0x001D, 15, 0, 18},  {0x001E, 15, 0, 17},
    {0x001F, 15, 0, 16},  {0x0010, 16, 0, 40},  {0x0011, 16, 0, 39},
    {0x0012, 16, 0, 38},  {0x0013, 16, 0, 37},  {0x0014, 16, 0, 36},
    {0x0015, 16, 0, 35},  {0x0016, 16, 0, 34},  {0x0017, 16, 0, 33},
    {0x0018, 16, 0, 32},  {0x0019, 16, 1, 14},  {0x001A, 16, 1, 13},
    {0x001B, 16, 1, 12},  {0x001C, 16, 1, 11},  {0x001D, 16, 1, 10},
    {0x001E, 16, 1, 9},   {0x001F, 16, 1, 8},   {0x0010, 17, 1, 18},
    {0x0011, 17, 1, 17},  {0x0012, 17, 1, 16},  {0x0013, 17, 1, 15},
    {0x0014, 17, 6, 3},   {0x0015, 17, 16, 2},  {0x0016, 17, 15, 2},
    {0x0017, 17, 14, 2},  {0x0018, 17, 13, 2},  {0x0019, 17, 12, 2},
    {0x001A, 17, 11, 2},  {0x001B, 17, 31, 1},  {0x001C, 17, 30, 1},
    {0x001D, 17, 29, 1},  {0x001E, 17, 28, 1},  {0x001F, 17, 27, 1},
};
#define CODE_COUNT                                                             \
    ((s32)(sizeof(g_port_mdec_codes) / sizeof(g_port_mdec_codes[0])))

/* The code that ends a block, and the escape, which the table above does
 * not hold because neither has a sign. */
#define EOB_BITS 0x2
#define EOB_LENGTH 2
#define ESCAPE_CODE_BITS 0x1
#define ESCAPE_LENGTH 6

/* What a window of bits comes to: the halfword it means and how many
 * bits it takes, 0 if the bits name no code. */
typedef struct {
    u16 value;
    u8 length;
    u8 pad;
} PortMdecLookup;

static PortMdecLookup g_port_mdec_short[SHORT_INDEX_COUNT];
static PortMdecLookup g_port_mdec_long[LONG_INDEX_COUNT];
static s32 g_port_mdec_tables_built;

/* ---- the chip ---- */

typedef struct {
    u8 quant_luma[BLOCK_VALUES];
    u8 quant_chroma[BLOCK_VALUES];
    s16 scale[BLOCK_VALUES];
    /* the cosine table brought down by eight, as the transform takes it */
    s32 scale_eighth[BLOCK_VALUES];
    /* the run-level data being decoded, and how much is left */
    const u16 *codes;
    u32 codes_left;
    s32 depth_24bit;
    s32 set_mask_bit;
    /* the macroblock being handed out */
    u8 pixels[MACROBLOCK_BYTES_MAX];
    u32 pixels_bytes;
    u32 pixels_at;
    void (*out_callback)(void);
    void (*in_callback)(void);
    /* what a run says at its end */
    u32 frames;
    u32 macroblocks;
    u32 short_frames; /* the chip was asked for more than it had */
    /* a transfer out asked for while the callback was running */
    s32 running;
    s32 waiting;
    u32 *waiting_at;
    s32 waiting_words;
} PortMdec;

static PortMdec g_port_mdec;

static s32 port_MdecClamp(s32 value, s32 low, s32 high) {
    if (value < low) {
        return low;
    }
    if (value > high) {
        return high;
    }
    return value;
}

/* A run-level halfword's value, ten bits with a sign. */
static s32 port_MdecValue(u16 code) {
    s32 value = code & RL_VALUE_MASK;

    return (value & RL_VALUE_SIGN) ? value - (RL_VALUE_SIGN * 2) : value;
}

/* The chip as it is after a reset (port_movie.h): its two quantisation
 * tables and its cosine table as Sony's library loads them, 24-bit
 * colour, and nothing being decoded. */
void port_ResetMdec(void) {
    u32 i;

    for (i = 0; i < BLOCK_VALUES; i++) {
        g_port_mdec.quant_luma[i] = g_port_mdec_default_quant[i];
        g_port_mdec.quant_chroma[i] = g_port_mdec_default_quant[i];
        g_port_mdec.scale[i] = g_port_mdec_default_scale[i];
        g_port_mdec.scale_eighth[i] =
            g_port_mdec_default_scale[i] / (1 << IDCT_SCALE_SHIFT);
    }
    g_port_mdec.codes = NULL;
    g_port_mdec.codes_left = 0;
    g_port_mdec.depth_24bit = 1;
    g_port_mdec.set_mask_bit = 0;
    g_port_mdec.pixels_bytes = 0;
    g_port_mdec.pixels_at = 0;
    g_port_mdec.waiting = 0;
}

/* ---- the bitstream ---- */

/* `count` bits of `data` from bit `at`: the halfwords are little-endian
 * and each is read from its top bit down (port_movie.h). */
u32 port_ReadMdecBits(const u8 *data, u32 at, u32 count) {
    u32 out = 0;
    u32 i;

    for (i = 0; i < count; i++) {
        u32 bit = at + i;
        u32 byte = (bit / HALFWORD_BITS) * 2;
        u32 word = (u32)data[byte] | ((u32)data[byte + 1] << 8);
        u32 place = HALFWORD_TOP_BIT - bit % HALFWORD_BITS;

        out = (out << 1) | ((word >> place) & 1);
    }
    return out;
}

/* Fills both tables from the list of codes: every window of bits that
 * begins with a code answers with that code. */
void port_BuildMdecTables(void) {
    s32 i;

    if (g_port_mdec_tables_built) {
        return;
    }
    g_port_mdec_tables_built = 1;
    for (i = 0; i < CODE_COUNT + 2; i++) {
        u32 bits;
        u32 length;
        u16 value;
        u32 sign;

        if (i == CODE_COUNT) {
            bits = EOB_BITS;
            length = EOB_LENGTH;
            value = RL_END_OF_BLOCK;
        } else if (i == CODE_COUNT + 1) {
            bits = ESCAPE_CODE_BITS;
            length = ESCAPE_LENGTH;
            value = RL_ESCAPE;
        } else {
            bits = 0;
            length = 0;
            value = 0;
        }
        for (sign = 0; sign < 2; sign++) {
            u32 code;
            u32 bit_count;
            u16 answer;
            u32 step;
            u32 first;
            u32 fill;

            if (i < CODE_COUNT) {
                const PortMdecCode *row = &g_port_mdec_codes[i];
                s32 level = sign ? -row->level : row->level;

                code = ((u32)row->bits << 1) | sign;
                bit_count = row->length;
                answer = (u16)(((u32)row->run << RL_RUN_SHIFT) |
                               ((u32)level & RL_VALUE_MASK));
            } else {
                if (sign) {
                    break; /* neither of the two has a sign */
                }
                code = bits;
                bit_count = length;
                answer = value;
            }
            if (bit_count <= SHORT_INDEX_BITS) {
                step = 1U << (SHORT_INDEX_BITS - bit_count);
                first = code << (SHORT_INDEX_BITS - bit_count);
                for (fill = 0; fill < step; fill++) {
                    g_port_mdec_short[first + fill].value = answer;
                    g_port_mdec_short[first + fill].length = (u8)bit_count;
                }
                continue;
            }
            /* a long code: eight zeroes, then the nine bits the second
             * table answers for */
            bit_count -= LONG_PREFIX_BITS;
            code &= (1U << bit_count) - 1;
            step = 1U << (LONG_INDEX_BITS - bit_count);
            first = code << (LONG_INDEX_BITS - bit_count);
            for (fill = 0; fill < step; fill++) {
                g_port_mdec_long[first + fill].value = answer;
                g_port_mdec_long[first + fill].length = (u8)bit_count;
            }
        }
    }
}

/* The next code at bit `at` (port_movie.h). */
u32 port_NextMdecCode(const u8 *data, u32 at, u16 *value) {
    u32 head = port_ReadMdecBits(data, at, SHORT_INDEX_BITS);
    const PortMdecLookup *found;

    port_BuildMdecTables();
    if (head >> (SHORT_INDEX_BITS - LONG_PREFIX_BITS) != 0) {
        found = &g_port_mdec_short[head];
        *value = found->value;
        return found->length;
    }
    found = &g_port_mdec_long[port_ReadMdecBits(data, at, CODE_BITS_MAX) &
                              (LONG_INDEX_COUNT - 1)];
    *value = found->value;
    if (found->length == 0) {
        return 0;
    }
    return found->length + LONG_PREFIX_BITS;
}

/* ---- the bitstream to run-level pairs ---- */

/* Sony's: expands the frame at `bs` into `buf`, whose first word is the
 * command the chip is given with it (the length in words). Returns 0. */
s32 DecDCTvlc(u32 *bs, u32 *buf) {
    const u8 *data = (const u8 *)bs;
    const u16 *header = (const u16 *)bs;
    u16 *out = (u16 *)(buf + 1);
    u32 words = header[0];
    u32 scale = header[2];
    u32 version = header[3];
    u32 room = words * 2;
    u32 at = FRAME_HEADER_HALFWORDS * HALFWORD_BITS;
    u32 count = 0;

    port_BuildMdecTables();
    buf[0] = MDEC_DECODE_COMMAND | (words & MDEC_SIZE_MASK);
    if (header[1] != FRAME_MAGIC || version != FRAME_VERSION_2) {
        port_Log("rebushido: this movie's frames are version %u, which the "
                 "port does not read (version 2 is the game's)\n",
                 version);
        return 0;
    }
    while (count + 1 < room) {
        /* the lowest coefficient, and the scale the chip is to use */
        u32 began = count;
        u32 dc = port_ReadMdecBits(data, at, DC_BITS);
        s32 ended = 0;

        at += DC_BITS;
        out[count++] = (u16)(((scale & RL_RUN_MASK) << RL_RUN_SHIFT) |
                             (dc & RL_VALUE_MASK));
        while (count < room) {
            u16 value;
            u32 used = port_NextMdecCode(data, at, &value);

            if (used == 0) {
                /* the frame's last block is behind: what is left is the
                 * bits the encoder filled the last sector out with. The
                 * bitstream says no more than that, and the block just
                 * begun goes back */
                count = began;
                ended = 1;
                break;
            }
            at += used;
            if (value == RL_ESCAPE) {
                value = (u16)port_ReadMdecBits(data, at, ESCAPE_BITS);
                at += ESCAPE_BITS;
            }
            out[count++] = value;
            if (value == RL_END_OF_BLOCK) {
                break;
            }
        }
        if (ended) {
            break;
        }
    }
    /* the frame's declared length is rounded up to the 32 words the chip
     * is given at a time: what is over ends no block */
    while (count < room) {
        out[count++] = RL_END_OF_BLOCK;
    }
    return 0;
}

/* ---- run-level pairs to a block of coefficients ---- */

/* The chip's inverse cosine transform, twice over: each pass multiplies
 * the rows by the cosine table and leaves the answer turned about its
 * diagonal, so the second pass takes the columns. A row of nothing gives
 * a column of nothing, which is where most of the work is saved. */
static void port_MdecIdctPass(const s32 *src, s32 *dst) {
    const s32 *scale = g_port_mdec.scale_eighth;
    s32 row;

    for (row = 0; row < BLOCK_SIDE; row++) {
        const s32 *line = &src[row * BLOCK_SIDE];
        s32 column;
        s32 any = 0;
        s32 z;

        for (z = 0; z < BLOCK_SIDE; z++) {
            any |= line[z];
        }
        if (any == 0) {
            for (column = 0; column < BLOCK_SIDE; column++) {
                dst[column * BLOCK_SIDE + row] = 0;
            }
            continue;
        }
        for (column = 0; column < BLOCK_SIDE; column++) {
            s32 total = 0;

            for (z = 0; z < BLOCK_SIDE; z++) {
                total += line[z] * scale[z * BLOCK_SIDE + column];
            }
            dst[column * BLOCK_SIDE + row] =
                (total + IDCT_SUM_HALF) >> IDCT_SUM_SHIFT;
        }
    }
}

/* One block (port_movie.h). */
u32 port_MdecBlock(const u16 *codes, u32 count, s32 chroma, s32 *out) {
    const u8 *quant =
        chroma ? g_port_mdec.quant_chroma : g_port_mdec.quant_luma;
    s32 work[BLOCK_VALUES];
    s32 scratch[BLOCK_VALUES];
    u32 at = 0;
    u32 scale;
    s32 value;
    u32 k;
    s32 i;

    for (i = 0; i < BLOCK_VALUES; i++) {
        work[i] = 0;
    }
    /* the chip passes over halfwords that end a block until it finds one
     * that begins one: that is what the padding at the end of a frame's
     * run-level data is */
    while (at < count && codes[at] == RL_END_OF_BLOCK) {
        at++;
    }
    if (at >= count) {
        for (i = 0; i < BLOCK_VALUES; i++) {
            out[i] = 0;
        }
        return at;
    }
    scale = (codes[at] >> RL_RUN_SHIFT) & RL_RUN_MASK;
    value = port_MdecValue(codes[at]) * (s32)quant[0];
    k = 0;
    for (;;) {
        if (scale == 0) {
            /* with no scale the chip takes the value as it is, and in
             * the order the coefficients came, not the zigzag */
            value = port_MdecValue(codes[at]) * 2;
        }
        value = port_MdecClamp(value, COEFFICIENT_MIN, COEFFICIENT_MAX);
        work[scale != 0 ? g_port_mdec_zigzag[k] : k] = value;
        at++;
        if (at >= count) {
            break;
        }
        if (codes[at] == RL_END_OF_BLOCK) {
            at++;
            break;
        }
        k += ((codes[at] >> RL_RUN_SHIFT) & RL_RUN_MASK) + 1;
        if (k >= BLOCK_VALUES) {
            /* more coefficients than a block holds: the rest are passed
             * over, up to the halfword that ends the block */
            while (at < count && codes[at] != RL_END_OF_BLOCK) {
                at++;
            }
            if (at < count) {
                at++;
            }
            break;
        }
        value = (port_MdecValue(codes[at]) * (s32)quant[k] * (s32)scale +
                 RL_DEQUANT_HALF) >>
                RL_DEQUANT_SHIFT;
    }
    port_MdecIdctPass(work, scratch);
    port_MdecIdctPass(scratch, out);
    return at;
}

/* ---- colour ---- */

/* The chip's constants for the step from brightness and the two colour
 * differences to red, green and blue, in 16384ths. What the chip's own
 * fixed point is, and which way it rounds, is not published: these are
 * the factors every description of it gives (1.402, 0.3437, 0.7143,
 * 1.772), and the port keeps 14 bits of fraction (docs/PORT.md, "The
 * movie": what this costs against the emulator). */
#define COLOR_ONE_SHIFT 14
#define COLOR_R_CR 22970
#define COLOR_G_CB 5631
#define COLOR_G_CR 11703
#define COLOR_B_CB 29032
#define LEVEL_MIN (-128)
#define LEVEL_MAX 127
#define LEVEL_OFFSET 128

/* Six blocks to a macroblock of pixels (port_movie.h). */
void port_MdecMacroblockRgb(
    const s32 *cr, const s32 *cb, const s32 **luma, u8 *rgb) {
    s32 quarter;

    for (quarter = 0; quarter < LUMA_BLOCKS; quarter++) {
        s32 left = BLOCK_SIDE * (quarter & 1);
        s32 top = BLOCK_SIDE * (quarter >> 1);
        s32 y;

        for (y = 0; y < BLOCK_SIDE; y++) {
            s32 x;

            for (x = 0; x < BLOCK_SIDE; x++) {
                s32 at = ((x + left) / 2) + ((y + top) / 2) * BLOCK_SIDE;
                s32 red = port_MdecClamp(cr[at], LEVEL_MIN, LEVEL_MAX);
                s32 blue = port_MdecClamp(cb[at], LEVEL_MIN, LEVEL_MAX);
                s32 level = port_MdecClamp(
                    luma[quarter][y * BLOCK_SIDE + x], LEVEL_MIN, LEVEL_MAX);
                s32 r = level + ((COLOR_R_CR * red) >> COLOR_ONE_SHIFT);
                s32 g = level - ((COLOR_G_CB * blue) >> COLOR_ONE_SHIFT) -
                        ((COLOR_G_CR * red) >> COLOR_ONE_SHIFT);
                s32 b = level + ((COLOR_B_CB * blue) >> COLOR_ONE_SHIFT);
                u8 *pixel = &rgb[((y + top) * MACROBLOCK_SIDE + x + left) *
                                 BYTES_24BIT];

                pixel[0] =
                    (u8)(port_MdecClamp(r, LEVEL_MIN, LEVEL_MAX) + LEVEL_OFFSET);
                pixel[1] =
                    (u8)(port_MdecClamp(g, LEVEL_MIN, LEVEL_MAX) + LEVEL_OFFSET);
                pixel[2] =
                    (u8)(port_MdecClamp(b, LEVEL_MIN, LEVEL_MAX) + LEVEL_OFFSET);
            }
        }
    }
}

/* ---- the chip's output ---- */

/* The six blocks of the next macroblock, as pixels of the depth asked
 * for, into g_port_mdec.pixels. */
static void port_MdecNextMacroblock(void) {
    s32 blocks[2 + LUMA_BLOCKS][BLOCK_VALUES];
    const s32 *luma[LUMA_BLOCKS];
    u8 rgb[MACROBLOCK_PIXELS * BYTES_24BIT];
    s32 i;

    for (i = 0; i < 2 + LUMA_BLOCKS; i++) {
        u32 used =
            port_MdecBlock(g_port_mdec.codes, g_port_mdec.codes_left, i < 2,
                           blocks[i]);

        g_port_mdec.codes += used;
        g_port_mdec.codes_left -= used;
    }
    for (i = 0; i < LUMA_BLOCKS; i++) {
        luma[i] = blocks[2 + i];
    }
    port_MdecMacroblockRgb(blocks[0], blocks[1], luma, rgb);
    g_port_mdec.macroblocks++;
    g_port_mdec.pixels_at = 0;
    if (g_port_mdec.depth_24bit) {
        for (i = 0; i < MACROBLOCK_PIXELS * BYTES_24BIT; i++) {
            g_port_mdec.pixels[i] = rgb[i];
        }
        g_port_mdec.pixels_bytes = MACROBLOCK_PIXELS * BYTES_24BIT;
        return;
    }
    for (i = 0; i < MACROBLOCK_PIXELS; i++) {
        u32 pixel = ((u32)(rgb[i * BYTES_24BIT] >> 3)) |
                    ((u32)(rgb[i * BYTES_24BIT + 1] >> 3) << 5) |
                    ((u32)(rgb[i * BYTES_24BIT + 2] >> 3) << 10);

        if (g_port_mdec.set_mask_bit) {
            pixel |= PIXEL_MASK_BIT;
        }
        g_port_mdec.pixels[i * BYTES_15BIT] = (u8)pixel;
        g_port_mdec.pixels[i * BYTES_15BIT + 1] = (u8)(pixel >> 8);
    }
    g_port_mdec.pixels_bytes = MACROBLOCK_PIXELS * BYTES_15BIT;
}

/* `words` words of decoded picture to `at`. */
static void port_MdecFill(u32 *at, s32 words) {
    u8 *out = (u8 *)at;
    u32 bytes = (u32)words * 4;
    u32 done;

    for (done = 0; done < bytes; done++) {
        if (g_port_mdec.pixels_at >= g_port_mdec.pixels_bytes) {
            if (g_port_mdec.codes_left == 0) {
                /* the chip was asked for more than it had */
                if (done == 0) {
                    g_port_mdec.short_frames++;
                }
                out[done] = 0;
                continue;
            }
            port_MdecNextMacroblock();
        }
        out[done] = g_port_mdec.pixels[g_port_mdec.pixels_at++];
    }
}

/* ---- Sony's libpress ---- */

void DecDCTReset(s32 mode) {
    if (mode == 0) {
        ResetCallback();
    }
    port_ResetMdec();
}

/* The length, in words, of the run-level data DecDCTvlc made. */
s32 DecDCTBufSize(u32 *bs) { return *(u16 *)bs; }

/* Bit 0 of `mode` asks for 24-bit colour, bit 1 for the mask bit to be
 * set in 15-bit pixels: Sony's writes both into the command word, and
 * the chip reads them there. */
void DecDCTin(u32 *buf, s32 mode) {
    if (mode & 1) {
        *buf &= ~MDEC_DEPTH_15BIT;
    } else {
        *buf |= MDEC_DEPTH_15BIT;
    }
    if (mode & 2) {
        *buf |= MDEC_SET_MASK_BIT;
    } else {
        *buf &= ~MDEC_SET_MASK_BIT;
    }
    g_port_mdec.depth_24bit = (*buf & MDEC_DEPTH_15BIT) == 0;
    g_port_mdec.set_mask_bit = (*buf & MDEC_SET_MASK_BIT) != 0;
    g_port_mdec.codes = (const u16 *)(buf + 1);
    g_port_mdec.codes_left = (*buf & MDEC_SIZE_MASK) * 2;
    g_port_mdec.frames++;
    g_port_mdec.pixels_bytes = 0;
    g_port_mdec.pixels_at = 0;
    if (g_port_mdec.in_callback != NULL) {
        g_port_mdec.in_callback();
    }
}

/* On the console the transfer out is a DMA and its end is an interrupt;
 * here it is done as it is asked for and the callback runs at once. A
 * callback that asks for the next piece is answered by this loop, not by
 * calling into it again. */
void DecDCTout(u32 *buf, s32 size) {
    u32 *at = buf;
    s32 words = size;

    if (g_port_mdec.running) {
        g_port_mdec.waiting = 1;
        g_port_mdec.waiting_at = buf;
        g_port_mdec.waiting_words = size;
        return;
    }
    g_port_mdec.running = 1;
    for (;;) {
        port_MdecFill(at, words);
        if (g_port_mdec.out_callback == NULL) {
            break;
        }
        g_port_mdec.waiting = 0;
        g_port_mdec.out_callback();
        if (!g_port_mdec.waiting) {
            break;
        }
        at = g_port_mdec.waiting_at;
        words = g_port_mdec.waiting_words;
    }
    g_port_mdec.running = 0;
}

/* The chip is never busy here. */
s32 DecDCTinSync(s32 mode) {
    (void)mode;
    return 0;
}

s32 DecDCToutSync(s32 mode) {
    (void)mode;
    return 0;
}

void DecDCTinCallback(void (*func)(void)) { g_port_mdec.in_callback = func; }

/* What a run says of the chip at its end (port_movie.h). */
void port_PrintMdecSummary(void) {
    port_Print("movie decoded %u frames %u macroblocks, %u short\n",
               g_port_mdec.frames, g_port_mdec.macroblocks,
               g_port_mdec.short_frames);
}

void DecDCToutCallback(void (*func)(void)) { g_port_mdec.out_callback = func; }
