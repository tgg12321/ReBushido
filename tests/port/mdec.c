/* The port's picture decompressor and the disc's stream, on made-up data. */
/* The movie in the native port (docs/PORT.md, "The movie"): the
 * bitstream decoder and the chip (src/port/mdec.c), and the streaming
 * library's ring with the port's handler under it (src/port/stream.c and
 * Sony's own modules). Nothing of the game's disc is read: the bits, the
 * coefficients and the sectors are all made up here, and the answers are
 * worked out by hand or a second time in Python (mdec.py). */
/* The overlay's sdk.h is shut out by its guard, as src/port/stream.c
 * shuts it out: it declares printf otherwise than Sony's libc.h does,
 * and the game's headers are in this program too. */
#define SDK_H
#include "common.h"
#include <psxsdk/libcd.h>
#include <psxsdk/libetc.h>
#include "port_disc.h"
#include "port_movie.h"

/* ---- what the two files stand on ---- */

void port_Log(const char *format, ...) { (void)format; }

void port_Print(const char *format, ...) { (void)format; }

s32 ResetCallback(void) { return 0; }

#include "src/port/mdec.c"

/* The drive, the clock and the disc's sound, for the stream: this test's
 * own, so that no sector and no blank is the real ones'. */
static u8 *g_made_sectors;
static u32 g_made_sector_count;
static u32 g_made_lines;
static u32 g_made_waits;
static u32 g_made_sound;

s32 port_DriveReadSector(s32 sector, u8 *raw) {
    u32 i;

    if (sector < 0 || (u32)sector >= g_made_sector_count) {
        return 0;
    }
    for (i = 0; i < PORT_DISC_SECTOR_BYTES; i++) {
        raw[i] = g_made_sectors[(u32)sector * PORT_DISC_SECTOR_BYTES + i];
    }
    return 1;
}

u32 port_FeedXaSector(const u8 *raw) {
    /* this test's sectors of sound are the ones whose data begins 0xFF */
    if (raw[PORT_DISC_DATA_OFFSET] == 0xFF) {
        g_made_sound++;
        return 1;
    }
    return 0;
}

u32 port_GetLineCount(void) { return g_made_lines; }

u32 port_GetBlankCount(void) { return g_made_lines / PORT_LINES_PER_BLANK; }

void port_WaitOneLine(void) {
    g_made_lines++;
    g_made_waits++;
}

void port_DiscSectorToPosition(u32 sector, u8 *position) {
    position[0] = (u8)(sector >> 16);
    position[1] = (u8)(sector >> 8);
    position[2] = (u8)sector;
}

CdlCB CdReadyCallback(CdlCB func) {
    (void)func;
    return NULL;
}

CdlLOC *CdIntToPos(s32 sector, CdlLOC *loc) {
    (void)sector;
    return loc;
}

s32 CdPosToInt(CdlLOC *loc) {
    (void)loc;
    return 0;
}

/* CdRead2's variable, which says whether sectors come whole. */
s32 StMode = 1;

/* Sony's own streaming modules, as the port compiles them, and
 * StGetNext under the name the port's wrapper calls it by. */
#define StGetNext port_StGetNextRing
#include "movovl/sdk/libcd/c_002.c"
#include "movovl/sdk/libcd/c_004.c"
#include "movovl/sdk/libcd/c_005.c"
#include "movovl/sdk/libcd/c_007.c"
#include "movovl/sdk/libcd/c_008.c"
#include "movovl/sdk/libcd/c_009.c"
#include "movovl/sdk/libcd/c_010.c"
#include "movovl/sdk/libcd/cdrom.c"
#undef StGetNext

#include "src/port/stream.c"

/* Which frame each display buffer holds (src/port/movie_shown.c), with
 * nothing of the display or the files under it: the dump is not run. */
#include "port_gpu.h"
#include "port_host.h"

static s32 g_made_playing;

s32 port_MovieIsPlaying(void) { return g_made_playing; }

void port_GetDisplay(PortDisplay *display) {
    display->x = 0;
    display->y = 0;
    display->w = 0;
    display->h = 0;
    display->is_on = 0;
    display->is_rgb24 = 0;
}

s32 port_WriteDisplayPng(s32 file) { return file >= 0; }

s32 port_HostCreate(const char *path) {
    (void)path;
    return -1;
}

void port_HostClose(s32 file) { (void)file; }

void port_Fail(const char *format, ...) { (void)format; }

#include "src/port/movie_shown.c"
#include "runtime/runtime.h"

/* ---- a bitstream made by hand ---- */

#define MADE_BITS_BYTES 8192

static u8 g_made_bits[MADE_BITS_BYTES];
static u32 g_made_bit_at;

static void made_ClearBits(void) {
    u32 i;

    for (i = 0; i < MADE_BITS_BYTES; i++) {
        g_made_bits[i] = 0;
    }
    g_made_bit_at = 0;
}

/* `count` bits of `value`, the highest first, in the order the chip
 * reads them: 16-bit little-endian words from the top bit down. */
static void made_PutBits(u32 value, u32 count) {
    u32 i;

    for (i = 0; i < count; i++) {
        u32 at = g_made_bit_at + i;
        u32 place = 15 - (at & 15);
        u32 byte = (at >> 4) * 2 + (place >= 8 ? 1 : 0);

        if ((value >> (count - 1 - i)) & 1) {
            g_made_bits[byte] |= (u8)(1 << (place & 7));
        }
    }
    g_made_bit_at += count;
}

/* ---- the bit reader ---- */

static void test_Bits(void) {
    static const u8 bytes[4] = {0x34, 0x12, 0xCD, 0xAB};

    /* the first halfword is 0x1234, read from its top bit */
    CHECK_EQ(port_ReadMdecBits(bytes, 0, 4), 0x1);
    CHECK_EQ(port_ReadMdecBits(bytes, 4, 4), 0x2);
    CHECK_EQ(port_ReadMdecBits(bytes, 8, 8), 0x34);
    CHECK_EQ(port_ReadMdecBits(bytes, 0, 16), 0x1234);
    /* and across the two halfwords */
    CHECK_EQ(port_ReadMdecBits(bytes, 16, 16), 0xABCD);
    CHECK_EQ(port_ReadMdecBits(bytes, 12, 8), 0x4A);
    CHECK_EQ(port_ReadMdecBits(bytes, 0, 1), 0);
    CHECK_EQ(port_ReadMdecBits(bytes, 3, 1), 1);
}

/* ---- the chip's own tables ---- */

/* The quantisation table and the cosine table the chip starts with,
 * printed for the Python side, which takes them out of the words the
 * original library loads the chip with. */
static void test_Tables(void) {
    s32 i;

    test_Print("quant");
    for (i = 0; i < 64; i++) {
        test_Print(" %u", g_port_mdec.quant_luma[i]);
        CHECK_EQ(g_port_mdec.quant_chroma[i], g_port_mdec.quant_luma[i]);
    }
    test_Print("\n");
    test_Print("scale");
    for (i = 0; i < 64; i++) {
        test_Print(" %d", g_port_mdec.scale[i]);
    }
    test_Print("\n");
}

/* ---- the code table ---- */

/* Every code of the table, read back out of a bitstream that holds it:
 * its length and what it means. The rows are printed for the Python
 * side, which works the whole table out again from the table of the
 * original library. */
static void test_Codes(void) {
    s32 i;
    u32 sign;

    port_BuildMdecTables();
    for (i = 0; i < CODE_COUNT; i++) {
        const PortMdecCode *row = &g_port_mdec_codes[i];

        for (sign = 0; sign < 2; sign++) {
            u32 code = ((u32)row->bits << 1) | sign;
            s32 level = sign ? -row->level : row->level;
            u16 value = 0;
            u32 used;

            made_ClearBits();
            made_PutBits(code, row->length);
            /* ones after it, so that nothing of the code is guessed
             * from the bits that follow */
            made_PutBits(0xFFFFFFFF, 32 - row->length);
            used = port_NextMdecCode(g_made_bits, 0, &value);
            CHECK_EQ(used, row->length);
            CHECK_EQ((value >> RL_RUN_SHIFT) & RL_RUN_MASK, row->run);
            CHECK_EQ(port_MdecValue(value), level);
            test_Print(
                "code %u %u %u %d\n", code, row->length, row->run, level);
        }
    }
    /* the two codes that have no sign */
    made_ClearBits();
    made_PutBits(EOB_BITS, EOB_LENGTH);
    made_PutBits(0xFFFFFFFF, 30);
    {
        u16 value = 0;

        CHECK_EQ(port_NextMdecCode(g_made_bits, 0, &value), EOB_LENGTH);
        CHECK_EQ(value, RL_END_OF_BLOCK);
        test_Print("code %u %u end\n", (u32)EOB_BITS, (u32)EOB_LENGTH);
    }
    made_ClearBits();
    made_PutBits(ESCAPE_CODE_BITS, ESCAPE_LENGTH);
    made_PutBits(0, 26);
    {
        u16 value = 0;

        CHECK_EQ(port_NextMdecCode(g_made_bits, 0, &value), ESCAPE_LENGTH);
        CHECK_EQ(value, RL_ESCAPE);
        test_Print(
            "code %u %u escape\n", (u32)ESCAPE_CODE_BITS, (u32)ESCAPE_LENGTH);
    }
    /* twelve zeroes name no code */
    made_ClearBits();
    made_PutBits(0, 32);
    {
        u16 value = 0;

        CHECK_EQ(port_NextMdecCode(g_made_bits, 0, &value), 0);
    }
}

/* ---- a frame's bitstream to run-level pairs ---- */

#define MADE_FRAME_WORDS 32
#define MADE_FRAME_HALFWORDS (MADE_FRAME_WORDS * 2)

static u32 g_made_frame[1 + MADE_FRAME_WORDS];

/* The header of a frame of `words` words, then `blocks` blocks: each a
 * lowest coefficient of `dc`, one coefficient of `run` and `level` by
 * the escape code, and the end. */
static void made_Frame(
    u32 words, u32 scale, s32 blocks, s32 dc, u32 run, s32 level) {
    s32 block;

    made_ClearBits();
    /* the four halfwords of the header, each read from its top bit */
    made_PutBits(words, 16);
    made_PutBits(FRAME_MAGIC, 16);
    made_PutBits(scale, 16);
    made_PutBits(FRAME_VERSION_2, 16);
    for (block = 0; block < blocks; block++) {
        made_PutBits((u32)dc & 0x3FF, DC_BITS);
        made_PutBits(ESCAPE_CODE_BITS, ESCAPE_LENGTH);
        made_PutBits(
            (run << RL_RUN_SHIFT) | ((u32)level & RL_VALUE_MASK), ESCAPE_BITS);
        made_PutBits(EOB_BITS, EOB_LENGTH);
    }
}

static void test_Vlc(void) {
    const u16 *out = (const u16 *)&g_made_frame[1];
    u32 i;

    made_Frame(MADE_FRAME_WORDS, 5, 3, -7, 4, -9);
    DecDCTvlc((u32 *)g_made_bits, g_made_frame);
    /* the command word: decode macroblocks, with the length in words */
    CHECK_EQ(g_made_frame[0] >> 28, 3);
    CHECK_EQ(g_made_frame[0] & 0xFFFF, MADE_FRAME_WORDS);
    for (i = 0; i < 3; i++) {
        CHECK_EQ((out[i * 3] >> RL_RUN_SHIFT) & RL_RUN_MASK, 5);
        CHECK_EQ(port_MdecValue(out[i * 3]), -7);
        CHECK_EQ((out[i * 3 + 1] >> RL_RUN_SHIFT) & RL_RUN_MASK, 4);
        CHECK_EQ(port_MdecValue(out[i * 3 + 1]), -9);
        CHECK_EQ(out[i * 3 + 2], RL_END_OF_BLOCK);
    }
    /* what is over, where the bits ran out, ends no block */
    for (i = 9; i < MADE_FRAME_HALFWORDS; i++) {
        CHECK_EQ(out[i], RL_END_OF_BLOCK);
    }
}

/* ---- a block of coefficients ---- */

/* A block of nothing but its lowest coefficient comes out flat, and
 * every one of its 64 values is the same number worked out here. */
static void test_FlatBlock(void) {
    u16 codes[2];
    s32 out[64];
    s32 dc;

    for (dc = -500; dc <= 500; dc += 100) {
        s32 first;
        s32 want;
        s32 i;

        codes[0] = (u16)((1 << RL_RUN_SHIFT) | ((u32)dc & RL_VALUE_MASK));
        codes[1] = RL_END_OF_BLOCK;
        CHECK_EQ(port_MdecBlock(codes, 2, 0, out), 2);
        /* the chip's two passes on one coefficient: times the first
         * entry of the quantisation table, held to ten bits, then twice
         * through the table's first row, 23170/8 each */
        first = dc * 2;
        if (first > COEFFICIENT_MAX) {
            first = COEFFICIENT_MAX;
        }
        if (first < COEFFICIENT_MIN) {
            first = COEFFICIENT_MIN;
        }
        want = (first * (23170 / 8) + IDCT_SUM_HALF) >> IDCT_SUM_SHIFT;
        want = (want * (23170 / 8) + IDCT_SUM_HALF) >> IDCT_SUM_SHIFT;
        for (i = 0; i < 64; i++) {
            CHECK_EQ(out[i], want);
        }
    }
}

/* Blocks with coefficients in several places, printed for the Python
 * side, which works the same arithmetic out a second time and also
 * compares it with a real inverse cosine transform. */
static void test_Blocks(void) {
    static const struct {
        u32 scale;
        s32 chroma;
        s32 dc;
        u32 run[3];
        s32 level[3];
    } cases[] = {
        {1, 0, 100, {0, 0, 0}, {40, -30, 20}},
        {4, 1, -200, {1, 7, 20}, {-60, 15, 9}},
        {12, 0, 0, {0, 3, 9}, {120, -100, 64}},
        {31, 1, 511, {2, 2, 2}, {-511, 511, -1}},
        {0, 0, 50, {0, 1, 2}, {7, -7, 3}},
    };

    u32 which;

    for (which = 0; which < sizeof(cases) / sizeof(cases[0]); which++) {
        u16 codes[8];
        s32 out[64];
        u32 count = 0;
        u32 i;

        codes[count++] = (u16)((cases[which].scale << RL_RUN_SHIFT) |
                               ((u32)cases[which].dc & RL_VALUE_MASK));
        for (i = 0; i < 3; i++) {
            codes[count++] =
                (u16)((cases[which].run[i] << RL_RUN_SHIFT) |
                      ((u32)cases[which].level[i] & RL_VALUE_MASK));
        }
        codes[count++] = RL_END_OF_BLOCK;
        CHECK_EQ(port_MdecBlock(codes, count, cases[which].chroma, out), count);
        test_Print(
            "block %u %d %d %u %d %u %d %u %d", cases[which].scale,
            cases[which].chroma, cases[which].dc, cases[which].run[0],
            cases[which].level[0], cases[which].run[1], cases[which].level[1],
            cases[which].run[2], cases[which].level[2]);
        for (i = 0; i < 64; i++) {
            test_Print(" %d", out[i]);
        }
        test_Print("\n");
    }
}

/* ---- colour ---- */

static void test_Color(void) {
    s32 cr[64];
    s32 cb[64];
    s32 luma[4][64];
    const s32 *blocks[4];
    u8 rgb[16 * 16 * 3];
    s32 i;
    s32 j;

    for (i = 0; i < 4; i++) {
        blocks[i] = luma[i];
    }
    /* grey: no colour difference, so red, green and blue are the
     * brightness and nothing else */
    for (i = 0; i < 64; i++) {
        cr[i] = 0;
        cb[i] = 0;
        for (j = 0; j < 4; j++) {
            luma[j][i] = 20;
        }
    }
    port_MdecMacroblockRgb(cr, cb, blocks, rgb);
    for (i = 0; i < 16 * 16; i++) {
        CHECK_EQ(rgb[i * 3], 148);
        CHECK_EQ(rgb[i * 3 + 1], 148);
        CHECK_EQ(rgb[i * 3 + 2], 148);
    }
    /* the colour differences are each read at half the width and
     * height: the four pixels of a corner share one value. The second
     * block of luminance is the top right of the macroblock, so its
     * first pixel is at x 8 */
    for (i = 0; i < 64; i++) {
        cr[i] = (i % 8) * 16 - 64;
        cb[i] = (i / 8) * 16 - 64;
        for (j = 0; j < 4; j++) {
            luma[j][i] = 0;
        }
    }
    port_MdecMacroblockRgb(cr, cb, blocks, rgb);
    for (i = 0; i < 16; i++) {
        for (j = 0; j < 16; j++) {
            const u8 *pixel = &rgb[(i * 16 + j) * 3];
            s32 red = (j / 2) * 16 - 64;
            s32 blue = (i / 2) * 16 - 64;
            s32 want_r = (COLOR_R_CR * red) >> COLOR_ONE_SHIFT;
            s32 want_g = -((COLOR_G_CB * blue) >> COLOR_ONE_SHIFT) -
                         ((COLOR_G_CR * red) >> COLOR_ONE_SHIFT);
            s32 want_b = (COLOR_B_CB * blue) >> COLOR_ONE_SHIFT;

            CHECK_EQ(pixel[0], port_MdecClamp(want_r, -128, 127) + 128);
            CHECK_EQ(pixel[1], port_MdecClamp(want_g, -128, 127) + 128);
            CHECK_EQ(pixel[2], port_MdecClamp(want_b, -128, 127) + 128);
            if (i == 0 && j < 4) {
                test_Print("color %d %d %d %d %d %d\n", 0, blue, red, pixel[0],
                           pixel[1], pixel[2]);
            }
        }
    }
}

/* ---- the chip, from a frame to pixels ---- */

static void test_Chip(void) {
    static u32 picture[16 * 16 * 3 / 4];
    const u8 *bytes = (const u8 *)picture;
    const u16 *halfwords = (const u16 *)picture;
    s32 want;
    u32 i;

    /* six blocks of one coefficient each: no colour, one brightness.
     * The whole macroblock is then one grey. */
    made_Frame(MADE_FRAME_WORDS, 1, 6, 0, 0, 0);
    DecDCTvlc((u32 *)g_made_bits, g_made_frame);
    /* the blocks are Cr, Cb and four of luminance: give the last four a
     * brightness by hand */
    {
        u16 *codes = (u16 *)&g_made_frame[1];

        for (i = 2; i < 6; i++) {
            codes[i * 3] = (u16)((1 << RL_RUN_SHIFT) | (200 & RL_VALUE_MASK));
        }
    }
    DecDCTReset(1);
    DecDCTin(g_made_frame, 1); /* 24-bit colour */
    DecDCTout(picture, 16 * 16 * 3 / 4);
    want = (200 * 2 * (23170 / 8) + IDCT_SUM_HALF) >> IDCT_SUM_SHIFT;
    want = (want * (23170 / 8) + IDCT_SUM_HALF) >> IDCT_SUM_SHIFT;
    want = port_MdecClamp(want, -128, 127) + 128;
    for (i = 0; i < 16 * 16; i++) {
        CHECK_EQ(bytes[i * 3], want);
        CHECK_EQ(bytes[i * 3 + 1], want);
        CHECK_EQ(bytes[i * 3 + 2], want);
    }
    /* the same in 15-bit colour, where a pixel is a halfword and the
     * mask bit is set or not as the mode says */
    DecDCTin(g_made_frame, 0);
    DecDCTout(picture, 16 * 16 * 2 / 4);
    for (i = 0; i < 16 * 16; i++) {
        u32 want15 = (u32)(want >> 3) * (1 | (1 << 5) | (1 << 10));

        CHECK_EQ(halfwords[i], want15);
    }
    DecDCTin(g_made_frame, 2);
    DecDCTout(picture, 16 * 16 * 2 / 4);
    for (i = 0; i < 16 * 16; i++) {
        CHECK(halfwords[i] & 0x8000);
    }
}

/* ---- the stream's ring ---- */

#define RING_SLOTS 16
#define MADE_STREAM_SECTORS 40
#define SECTOR_BODY 2016
#define FRAME_CHUNKS 3

static u32 g_made_ring[RING_SLOTS * (32 + SECTOR_BODY) / 4];

/* A sector of the stream: chunk `chunk` of `chunks` of frame `frame`,
 * its body filled with a number of its own. A `sound` sector is one the
 * drive plays itself. */
static void made_Sector(u32 at, u32 frame, u32 chunk, u32 chunks, s32 sound) {
    u8 *raw = &g_made_sectors[at * PORT_DISC_SECTOR_BYTES];
    u8 *data = &raw[PORT_DISC_DATA_OFFSET];
    u16 *head = (u16 *)data;
    u32 i;

    for (i = 0; i < PORT_DISC_SECTOR_BYTES; i++) {
        raw[i] = 0;
    }
    if (sound) {
        data[0] = 0xFF;
        return;
    }
    head[0] = STREAM_SECTOR_ID;
    head[1] = 0x8001;
    head[2] = (u16)chunk;
    head[3] = (u16)chunks;
    head[4] = (u16)frame;
    head[5] = 0;
    head[8] = 320;
    head[9] = 240;
    for (i = 0; i < SECTOR_BODY; i++) {
        data[32 + i] = (u8)(frame * 17 + chunk * 3 + i);
    }
}

/* The frame the ring hands out next, or NULL. */
static u32 *made_NextFrame(u32 **header) {
    u32 *addr = NULL;

    if (StGetNext(&addr, header) != 0) {
        return NULL;
    }
    return addr;
}

/* What the streaming library hangs on the drive: the port takes a read
 * that follows for a stream. */
static void made_DataReady(void) {}

static void test_Stream(void) {
    u32 *header = NULL;
    u32 *addr;
    u32 i;

    g_made_sectors = test_Alloc(MADE_STREAM_SECTORS * PORT_DISC_SECTOR_BYTES);
    g_made_sector_count = MADE_STREAM_SECTORS;
    /* three whole frames, with a sector of sound between the first two,
     * then a frame whose second chunk is missing, then a whole one */
    made_Sector(0, 1, 0, FRAME_CHUNKS, 0);
    made_Sector(1, 1, 1, FRAME_CHUNKS, 0);
    made_Sector(2, 1, 2, FRAME_CHUNKS, 0);
    made_Sector(3, 0, 0, 0, 1);
    made_Sector(4, 2, 0, FRAME_CHUNKS, 0);
    made_Sector(5, 2, 1, FRAME_CHUNKS, 0);
    made_Sector(6, 2, 2, FRAME_CHUNKS, 0);
    made_Sector(7, 3, 0, FRAME_CHUNKS, 0);
    made_Sector(8, 3, 2, FRAME_CHUNKS, 0); /* the middle chunk is lost */
    made_Sector(9, 4, 0, FRAME_CHUNKS, 0);
    made_Sector(10, 4, 1, FRAME_CHUNKS, 0);
    made_Sector(11, 4, 2, FRAME_CHUNKS, 0);

    StSetRing(g_made_ring, RING_SLOTS);
    StSetStream(1, 1, 0xFFFFFFFF, NULL, NULL);
    CdDataCallback(made_DataReady);
    CHECK(port_StreamArmed());
    g_made_lines = 0;
    port_StartDiscStream(0, 1);

    /* no sector is due before the drive has turned */
    CHECK_EQ(port_PumpStream(), 1);
    CHECK(made_NextFrame(&header) == NULL);
    /* a sector takes 20,979/200 scan lines: give the drive the time of
     * thirteen and the twelve have all passed */
    g_made_lines = 13 * SECTORS_PER_LINE_BOTTOM / SECTORS_PER_LINE_TOP;
    port_PumpStream();
    CHECK_EQ(g_made_sound, 1);

    /* frame 1, whole */
    addr = made_NextFrame(&header);
    CHECK(addr != NULL);
    if (addr != NULL) {
        CHECK_EQ(((u16 *)header)[4], 1);
        CHECK_EQ(((u16 *)header)[3], FRAME_CHUNKS);
        for (i = 0; i < 8; i++) {
            CHECK_EQ(((const u8 *)addr)[i], (u8)(1 * 17 + 0 * 3 + i));
        }
        /* the chunks are one after another in the ring */
        CHECK_EQ(((const u8 *)addr)[SECTOR_BODY], (u8)(1 * 17 + 1 * 3));
        CHECK_EQ(((const u8 *)addr)[2 * SECTOR_BODY], (u8)(1 * 17 + 2 * 3));
        CHECK_EQ(StFreeRing(addr), 0);
    }
    /* frame 2, whole */
    addr = made_NextFrame(&header);
    CHECK(addr != NULL);
    if (addr != NULL) {
        CHECK_EQ(((u16 *)header)[4], 2);
        CHECK_EQ(StFreeRing(addr), 0);
    }
    /* frame 3 lost a chunk, so it is never handed out: the next whole
     * one is frame 4 */
    addr = made_NextFrame(&header);
    CHECK(addr != NULL);
    if (addr != NULL) {
        CHECK_EQ(((u16 *)header)[4], 4);
        CHECK_EQ(StFreeRing(addr), 0);
    }
    CHECK(made_NextFrame(&header) == NULL);

    /* the wait moves the clock on a scan line each time it finds
     * nothing */
    g_made_waits = 0;
    CHECK_EQ(StGetNext(&addr, &header), 1);
    CHECK_EQ(g_made_waits, 1);

    /* the drive turns at 150 sectors a second, which is this many scan
     * lines each: the port's clock is 44,100 samples a second and
     * 20,979/200 lines a sector (stream.c) */
    g_made_lines = 0;
    port_StartDiscStream(100, 1);
    CHECK_EQ(port_StreamSectorsDue(), 100);
    g_made_lines = 104; /* 20,979/200 is 104.895: not yet one sector */
    CHECK_EQ(port_StreamSectorsDue(), 100);
    g_made_lines = 105;
    CHECK_EQ(port_StreamSectorsDue(), 101);
    g_made_lines = SECTORS_PER_LINE_BOTTOM; /* 200 sectors exactly */
    CHECK_EQ(port_StreamSectorsDue(), 300);
    /* at single speed it is half as fast */
    g_made_lines = 0;
    port_StartDiscStream(0, 0);
    g_made_lines = SECTORS_PER_LINE_BOTTOM;
    CHECK_EQ(port_StreamSectorsDue(), 100);

    /* a drive that is reset drops the stream */
    port_StopDiscStream();
    CHECK_EQ(port_PumpStream(), 0);
}

/* The frame on show followed through the player's three steps, as
 * movovl/src/movie.c takes them: frame N pulled and expanded, the frame
 * before decoded, the one before that copied to a display buffer. */
static void test_Shown(void) {
    static u32 vlc[2][4];
    /* each the size of a whole picture, which a strip lies inside */
    static u8 picture[2][MOVIE_PICTURE_BYTES];
    u32 frame;

    port_MovieForgetShown();
    CHECK_EQ(port_MovieShownFrame(0).number, 0);
    for (frame = 1; frame <= 6; frame++) {
        u32 *expanded = vlc[frame & 1];

        /* frame N into run-level buffer N & 1, with scale N + 10 */
        port_MovieNotePulled(frame);
        port_MovieNoteExpanded(expanded, frame + 10);
        if (frame >= 2) {
            /* frame N - 1 decoded into picture (N - 1) & 1, in two slices */
            port_MovieNoteDecoding(vlc[(frame - 1) & 1]);
            port_MovieNoteDecodedInto(picture[(frame - 1) & 1]);
            port_MovieNoteDecodedInto(picture[(frame - 1) & 1] + 1000);
        }
        if (frame >= 3) {
            /* frame N - 2 copied, a strip from its middle, into buffer
             * (N & 1): y 0 or 240 */
            port_MovieNoteCopied((frame & 1) * 240, picture[frame & 1] + 5000);
            CHECK_EQ(
                port_MovieShownFrame((s32)(frame & 1) * 240).number, frame - 2);
            CHECK_EQ(
                port_MovieShownFrame((s32)(frame & 1) * 240).scale, frame + 8);
        }
    }
    /* both buffers, each its own frame */
    CHECK_EQ(port_MovieShownFrame(0).number, 4);
    CHECK_EQ(port_MovieShownFrame(300).number, 3);
    /* a copy of pixels that are no picture's changes nothing */
    port_MovieNoteCopied(0, vlc);
    CHECK_EQ(port_MovieShownFrame(0).number, 4);
    port_MovieForgetShown();
    CHECK_EQ(port_MovieShownFrame(0).number, 0);
}

void test_Main(void) {
    port_ResetMdec(); /* the chip as it is after a reset: its two tables */
    test_Bits();
    test_Tables();
    test_Codes();
    test_Vlc();
    test_FlatBlock();
    test_Blocks();
    test_Color();
    test_Chip();
    test_Stream();
    test_Shown();
}
