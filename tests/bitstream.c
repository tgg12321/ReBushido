/* The bit reader and its tracks (bitstream.c) against a reader in Python. */
#include "src/main/bitstream.c"
#include "runtime/runtime.h"

#define STREAM_WORDS 400
static u32 g_test_stream[STREAM_WORDS];

/* How many bits each read asks for, in turn: every width the game uses, 1
 * to 16, and a few wider. */
#define WIDTH_COUNT 20

static s32 test_Width(s32 read) {
    s32 width = read % WIDTH_COUNT + 1;

    return (width > 16) ? width + 4 : width;
}

#define READ_COUNT 600
#define TRACK_FRAMES 60

/* Makes a stream of scrambled words and prints it; reads it with
 * bitstream_ReadBits in fields of many widths and prints each; then reads
 * the same words as a track, printing its six values after every frame.
 * bitstream.py reads the same bits its own way. */
void test_Main(void) {
    BitStream stream;
    u32 scramble = 0x12345678;
    s32 i, j;
    s16 *values;

    for (i = 0; i < STREAM_WORDS; i++) {
        scramble = scramble * 1664525 + 1013904223;
        g_test_stream[i] = scramble ^ (scramble >> 15);
        test_Print("word %u\n", g_test_stream[i]);
    }

    stream.next = g_test_stream;
    stream.word = 0;
    stream.avail = 0;
    for (i = 0; i < READ_COUNT; i++) {
        test_Print("bits %d %u\n", test_Width(i),
                   bitstream_ReadBits(&stream, test_Width(i)));
    }

    bitstream_OpenTrack((s32)g_test_stream, 1);
    for (i = 0; i < TRACK_FRAMES; i++) {
        values = bitstream_StepTrack(1);
        test_Print("track");
        for (j = 0; j < TRACK_VALUES; j++) {
            test_Print(" %d", values[j]);
        }
        test_Print("\n");
    }
    /* the other track has not moved */
    bitstream_OpenTrack((s32)g_test_stream, 0);
    CHECK(g_bitstream_tracks[0].bits.next == g_test_stream);
    CHECK(g_bitstream_tracks[1].bits.next > g_test_stream);
}
