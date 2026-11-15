/* The native port's pictures (port_gpu.h; docs/PORT.md, "The picture"):
 * the rectangle of video memory that is on show, written to a file as
 * its 16-bit pixels or as a PNG, and video memory whole.
 *
 * The PNG is the simplest there is: 8 bits each of red, green and blue,
 * no filter, and the pixels stored, not packed, which the format allows
 * (a zlib stream of "stored" blocks). It is written through one small
 * buffer, a piece at a time, since the port has no memory to give
 * back. */
#include "common.h"
#include "port_gpu.h"
#include "port_host.h"
#include "bb2.h"

#define BUFFER_BYTES 0x4000
#define BYTE_MASK 0xFF
#define BITS_PER_BYTE 8
/* A pixel's three channels, five bits each. */
#define CHANNEL_MAX 31
#define CHANNEL_BITS 5
#define CHANNELS 3
#define BYTE_MAX 255
/* The 24-bit mode keeps three bytes in a word and a half. */
#define RGB24_BYTES 3
/* A picture this much wider than high is shown with each line twice: the
 * game's 640 by 240 fills a screen of four by three. */
#define WIDE_RATIO 2

/* PNG. */
#define PNG_SIGNATURE_BYTES 8
#define PNG_HEADER_BYTES 13
#define PNG_DEPTH 8
#define PNG_COLOR_RGB 2
#define PNG_TAG_BYTES 4
#define CRC_POLYNOMIAL 0xEDB88320U
#define CRC_START 0xFFFFFFFFU
#define CRC_ENTRIES 256
/* zlib: the two bytes that begin a stream, the most a stored block
 * holds, the five bytes before one, the sum after the last. */
#define ZLIB_METHOD 0x78
#define ZLIB_CHECK 0x01
#define ZLIB_HEADER_BYTES 2
#define ZLIB_BLOCK_MAX 0xFFFF
#define ZLIB_BLOCK_HEAD_BYTES 5
#define ZLIB_SUM_BYTES 4
#define ADLER_MODULUS 65521U
#define ADLER_SHIFT 16

/* A file being written, through the buffer; `failed` once a write has
 * not gone through. */
typedef struct {
    s32 file;
    s32 failed;
    u32 used;
    u32 crc;       /* of the chunk being written */
    u32 adler_low; /* of the pixels' bytes */
    u32 adler_high;
    u32 block_left; /* bytes still to come in the stored block */
    u32 data_left;  /* ... and in the picture */
    u8 bytes[BUFFER_BYTES];
} PortPictureFile;

static PortPictureFile g_port_picture;
static u32 g_port_crc_table[CRC_ENTRIES];

static void port_FlushPicture(void) {
    PortPictureFile *out = &g_port_picture;

    if (out->used != 0 && !out->failed &&
        !port_HostAppend(out->file, out->bytes, out->used)) {
        out->failed = 1;
    }
    out->used = 0;
}

static void port_PutByte(u32 byte) {
    PortPictureFile *out = &g_port_picture;

    if (out->used == BUFFER_BYTES) {
        port_FlushPicture();
    }
    out->bytes[out->used++] = (u8)byte;
    out->crc = g_port_crc_table[(out->crc ^ byte) & BYTE_MASK] ^
               (out->crc >> BITS_PER_BYTE);
}

/* Four bytes, the highest first, as PNG has its numbers. */
static void port_PutWord(u32 word) {
    s32 shift;

    for (shift = 3 * BITS_PER_BYTE; shift >= 0; shift -= BITS_PER_BYTE) {
        port_PutByte((word >> shift) & BYTE_MASK);
    }
}

/* A file begins: nothing in the buffer yet, and the table of the sum's
 * 256 steps made the first time. */
static void port_BeginPicture(s32 file) {
    PortPictureFile *out = &g_port_picture;
    u32 i;
    s32 bit;

    out->file = file;
    out->failed = 0;
    out->used = 0;
    if (g_port_crc_table[1] == 0) {
        for (i = 0; i < CRC_ENTRIES; i++) {
            u32 value = i;

            for (bit = 0; bit < BITS_PER_BYTE; bit++) {
                value =
                    (value & 1) ? (value >> 1) ^ CRC_POLYNOMIAL : value >> 1;
            }
            g_port_crc_table[i] = value;
        }
    }
}

/* A chunk begins: its length and its four letters. */
static void port_BeginChunk(u32 length, const char *tag) {
    s32 i;

    port_PutWord(length);
    g_port_picture.crc = CRC_START;
    for (i = 0; i < PNG_TAG_BYTES; i++) {
        port_PutByte((u8)tag[i]);
    }
}

static void port_EndChunk(void) { port_PutWord(~g_port_picture.crc); }

/* A byte of the picture's data: into the stored block it falls in, a
 * new block begun where the last is full. */
static void port_PutDataByte(u32 byte) {
    PortPictureFile *out = &g_port_picture;

    if (out->block_left == 0) {
        u32 size =
            out->data_left < ZLIB_BLOCK_MAX ? out->data_left : ZLIB_BLOCK_MAX;

        /* whether it is the last block; its size, and the size inverted */
        port_PutByte(size == out->data_left);
        port_PutByte(size & BYTE_MASK);
        port_PutByte(size >> BITS_PER_BYTE);
        port_PutByte(~size & BYTE_MASK);
        port_PutByte((~size >> BITS_PER_BYTE) & BYTE_MASK);
        out->block_left = size;
    }
    port_PutByte(byte);
    out->block_left--;
    out->data_left--;
    out->adler_low = (out->adler_low + byte) % ADLER_MODULUS;
    out->adler_high = (out->adler_high + out->adler_low) % ADLER_MODULUS;
}

/* A channel of five bits as a byte. */
static u32 port_ByteOfChannel(u32 channel) {
    return (channel & CHANNEL_MAX) * BYTE_MAX / CHANNEL_MAX;
}

/* The pixel at (x, y) of the display as three bytes. */
static void port_ReadDisplayPixel(
    const PortDisplay *display, s32 x, s32 y, u8 *rgb) {
    s32 line = (display->y + y) & (PORT_VRAM_HEIGHT - 1);
    s32 i;

    if (display->is_rgb24) {
        const u8 *row = (const u8 *)&g_port_vram[line * PORT_VRAM_WIDTH];
        u32 at =
            (u32)(display->x * 2 + x * RGB24_BYTES) % (PORT_VRAM_WIDTH * 2);

        for (i = 0; i < CHANNELS; i++) {
            rgb[i] = row[(at + (u32)i) % (PORT_VRAM_WIDTH * 2)];
        }
    } else {
        u16 pixel = g_port_vram[line * PORT_VRAM_WIDTH +
                                ((display->x + x) & (PORT_VRAM_WIDTH - 1))];

        for (i = 0; i < CHANNELS; i++) {
            rgb[i] = (u8)port_ByteOfChannel((u32)pixel >> (i * CHANNEL_BITS));
        }
    }
}

/* The signature, the header, the pixels as one zlib stream of stored
 * blocks, and the end (port_gpu.h). */
s32 port_WriteDisplayPng(s32 file) {
    PortPictureFile *out = &g_port_picture;
    static const u8 signature[PNG_SIGNATURE_BYTES] = {
        0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n', /* every PNG's */
    };
    PortDisplay display;
    s32 repeat;
    u32 data_bytes;
    u32 blocks;
    s32 x;
    s32 y;
    s32 i;
    u8 rgb[CHANNELS];

    port_GetDisplay(&display);
    repeat = display.w >= WIDE_RATIO * display.h ? 2 : 1;
    /* each line is a byte that says "no filter" and its pixels */
    data_bytes = (u32)(display.h * repeat) * (u32)(1 + display.w * CHANNELS);
    blocks = (data_bytes + ZLIB_BLOCK_MAX - 1) / ZLIB_BLOCK_MAX;
    port_BeginPicture(file);
    for (i = 0; i < PNG_SIGNATURE_BYTES; i++) {
        port_PutByte(signature[i]);
    }
    port_BeginChunk(PNG_HEADER_BYTES, "IHDR");
    port_PutWord((u32)display.w);
    port_PutWord((u32)(display.h * repeat));
    port_PutByte(PNG_DEPTH);
    port_PutByte(PNG_COLOR_RGB);
    port_PutByte(0); /* the one way of packing, no filter, no interlace */
    port_PutByte(0);
    port_PutByte(0);
    port_EndChunk();
    port_BeginChunk(ZLIB_HEADER_BYTES + blocks * ZLIB_BLOCK_HEAD_BYTES +
                        data_bytes + ZLIB_SUM_BYTES,
                    "IDAT");
    port_PutByte(ZLIB_METHOD);
    port_PutByte(ZLIB_CHECK);
    out->adler_low = 1;
    out->adler_high = 0;
    out->block_left = 0;
    out->data_left = data_bytes;
    for (y = 0; y < display.h * repeat; y++) {
        port_PutDataByte(0);
        for (x = 0; x < display.w; x++) {
            port_ReadDisplayPixel(&display, x, y / repeat, rgb);
            for (i = 0; i < CHANNELS; i++) {
                port_PutDataByte(rgb[i]);
            }
        }
    }
    port_PutWord((out->adler_high << ADLER_SHIFT) | out->adler_low);
    port_EndChunk();
    port_BeginChunk(0, "IEND");
    port_EndChunk();
    port_FlushPicture();
    return !out->failed;
}

/* A row of the display after a row, as video memory has them
 * (port_gpu.h). */
s32 port_WriteDisplayPixels(s32 file) {
    PortDisplay display;
    s32 y;

    port_GetDisplay(&display);
    for (y = 0; y < display.h; y++) {
        s32 line = (display.y + y) & (PORT_VRAM_HEIGHT - 1);
        s32 x = display.x & (PORT_VRAM_WIDTH - 1);
        s32 w = display.w;

        /* a display that runs off video memory's right edge goes on at
         * its left */
        if (x + w > PORT_VRAM_WIDTH) {
            u32 first = (u32)(PORT_VRAM_WIDTH - x);

            if (!port_HostAppend(file, &g_port_vram[line * PORT_VRAM_WIDTH + x],
                                 first * sizeof(u16))) {
                return 0;
            }
            w -= (s32)first;
            x = 0;
        }
        if (!port_HostAppend(file, &g_port_vram[line * PORT_VRAM_WIDTH + x],
                             (u32)w * sizeof(u16))) {
            return 0;
        }
    }
    return 1;
}

s32 port_WriteVram(s32 file) {
    return port_HostAppend(file, g_port_vram, PORT_VRAM_BYTES);
}
