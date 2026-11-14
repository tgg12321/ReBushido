/* Reading bits (motion.h): a reader of a stream of bits, and on top of it
 * the tracks, six values that a stream changes a little every frame. A
 * cutscene's camera is recorded as one. */
#include "common.h"
#include "bb2.h"

/* Defined further down. */
static s32 bitstream_ReadBits(BitStream *stream, s32 bit_count);

static BitTrack g_bitstream_tracks[2];

#define TRACK_VALUES 6
/* A value's change is ahead of it as its width in bits, in four bits; a
 * width of 0 stands for sixteen. */
#define TRACK_WIDTH_BITS 4
#define TRACK_FULL_WIDTH 16

/* Starts a track at the head of the stream at `stream_address`: nothing read
 * yet, and its six values 0 (they are cleared last first). */
void bitstream_OpenTrack(s32 stream_address, s32 track_index) {
    BitTrack *track = &g_bitstream_tracks[track_index];

    track->bits.next = (u32 *)stream_address;
    track->bits.word = 0;
    track->bits.avail = 0;
    track->values[TRACK_VALUES - 1] = 0;
    track->values[4] = 0;
    track->values[3] = 0;
    track->values[2] = 0;
    track->values[1] = 0;
    track->values[0] = 0;
}

/* A frame of a track is, for each value, the width of its change and then
 * the change, signed. */
s16 *bitstream_StepTrack(s32 track_index) {
    s32 i;
    BitTrack *track = &g_bitstream_tracks[track_index];

    for (i = 0; i < TRACK_VALUES; i++) {
        s32 nbits;
        s16 delta;

        nbits = bitstream_ReadBits(&track->bits, TRACK_WIDTH_BITS);
        if (nbits == 0) {
            nbits = TRACK_FULL_WIDTH;
        }
        delta = bitstream_ReadBits(&track->bits, nbits);
        /* its top bit is its sign */
        if ((delta >> (nbits - 1)) & 1) {
            delta |= 0xFFFF << (nbits - 1);
        }
        track->values[i] += delta;
    }
    return track->values;
}

/* The next `bit_count` bits of the stream, most significant first. A new
 * word is taken when the one being read has fewer than that left. */
static s32 bitstream_ReadBits(BitStream *stream, s32 bit_count) {
    s32 avail = stream->avail;
    u32 bits;

    if (avail < bit_count) {
        u32 old_mask, new_mask;
        s32 shift;
        u32 *next;
        u32 word;
        u32 low;

        /* what is left of the old word, then the top of the new one */
        bit_count -= avail;
        old_mask = 1 << avail;
        old_mask -= 1;
        new_mask = (1 << bit_count) - 1;
        bits = stream->word & old_mask;
        next = stream->next;
        stream->next = next + 1;
        shift = BITSTREAM_WORD_BITS - bit_count;
        word = *next;
        stream->avail = shift;
        stream->word = word;
        low = (word >> shift) & new_mask;
        bits = (bits << bit_count) | low;
    } else {
        stream->avail = avail - bit_count;
        bits = (stream->word >> (avail - bit_count)) & ((1 << bit_count) - 1);
    }
    return bits;
}
