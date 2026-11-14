/* The decoder of motion streams (motion.h): the fighters' recorded motion,
 * packed by the bit.
 *
 * A stream begins with a pose, 63 values of 12 bits, and 63 codes of two
 * bits that say how each of those values is packed from then on. A table of
 * three-byte entries follows, 0x70 bytes in: for every eighth frame, where
 * its bits begin, counted from the table. The frames are in groups of eight.
 * The first of a group is whole: a bit for each value, and the value's 12
 * bits if it is not the opening pose's. The seven after it give, for each
 * value, how much its rate of change has changed since the frame before, in
 * one of three packings (code 0: the value never changes). Every frame also
 * has the root's three values, 16 bits each or a single bit for zero.
 *
 * So a frame is decoded from the frame before it, or else from the start of
 * its group. Each decoder keeps the last few frames it decoded and where the
 * stream stood after them: the usual request, the frame after the last one,
 * costs one frame's bits. */
#include "common.h"
#include "bb2.h"

/* Defined further down. */
static s32 g_motion_codes_in_work;

MotionDecoder g_motion_decoders[3];

/* How each value is packed in the frames after a group's first. */
enum {
    CODE_FIXED = 0, /* it never changes: nothing in the stream */
    CODE_SMALL = 1, /* a count of zero bits, then that many bits less one */
    CODE_RARE = 2,  /* a bit for "no change", or else 12 bits */
    CODE_WIDE = 3,  /* a bit for "a change follows"; a sign and a size */
};

/* Where the frames' bits begin: the group table is this far into the stream,
 * and its entries count from the table. */
#define GROUP_TABLE_OFFSET 0x70
#define GROUP_TABLE_BITS (GROUP_TABLE_OFFSET * 8)
#define GROUP_FRAMES 8

/* The stream is read a word of 32 bits at a time; a value is 12 bits (an
 * angle: 4096 to the turn). */
#define VALUE_BITS 12
#define VALUE_MASK ((1 << VALUE_BITS) - 1)
#define VALUE_SIGN (1 << (VALUE_BITS - 1))
/* The first of the two triples that are signed: the 17th of the bones'
 * (after the root's three values and the waist's three); the other is the
 * next but one. */
#define SIGNED_TRIPLE (6 + 16 * 3)

/* CODE_SMALL: the change is at most 12 bits long, its low bit the sign.
 * CODE_WIDE: four bits first, the change's three low bits and its sign; the
 * rest of it is at most 8 bits long. */
#define SMALL_MAX_BITS 12
#define WIDE_LOW_BITS 3
#define WIDE_LOW_MASK ((1 << WIDE_LOW_BITS) - 1)
#define WIDE_SIGN (1 << WIDE_LOW_BITS)
#define WIDE_MAX_BITS 8

/* The work area, in halfwords: the pose being built, the rate each value is
 * changing at, the codes. */
#define WORK_RATES MOTION_FRAME_VALUES
#define WORK_CODES (MOTION_FRAME_VALUES * 2)
/* the first of the 12-bit values, after the root's three */
#define PACKED 3

/* The reader: `word` holds the unread bits at its top, `bits` says how many
 * there are, `next` is the next word of the stream. GETBITS reads `width` bits
 * into `dst`; GETBITS_PRE also sets the bits of `pre` in it. */
#define GETBITS_PRE(dst, width, pre)                                           \
    {                                                                          \
        u32 top = (pre);                                                       \
        if (bits < (width)) {                                                  \
            s32 need = (width) - bits;                                         \
            u32 hi = word >> (BITSTREAM_WORD_BITS - bits);                     \
            s32 left = BITSTREAM_WORD_BITS - need;                             \
            word = *next++;                                                    \
            dst = top | ((hi << need) | (word >> left));                       \
            word <<= need;                                                     \
            bits = left;                                                       \
        } else {                                                               \
            dst = top | (word >> (BITSTREAM_WORD_BITS - (width)));             \
            word <<= (width);                                                  \
            bits -= (width);                                                   \
        }                                                                      \
    }

#define GETBITS(dst, width)                                                    \
    if (bits < (width)) {                                                      \
        s32 need = (width) - bits;                                             \
        u32 hi = word >> (BITSTREAM_WORD_BITS - bits);                         \
        s32 left = BITSTREAM_WORD_BITS - need;                                 \
        word = *next++;                                                        \
        dst = (hi << need) | (word >> left);                                   \
        word <<= need;                                                         \
        bits = left;                                                           \
    } else {                                                                   \
        dst = word >> (BITSTREAM_WORD_BITS - (width));                         \
        word <<= (width);                                                      \
        bits -= (width);                                                       \
    }

/* One of the root's values: a bit, and 16 bits if it is set. */
#define GET_ROOT(dst)                                                          \
    GETBITS(field, 1);                                                         \
    if (field) {                                                               \
        GETBITS(field, 16);                                                    \
    }                                                                          \
    dst = field;

/* Counts zero bits up to the first set bit, which it takes, or up to
 * `most`. */
#define COUNT_ZEROS(count, most)                                               \
    count = 0;                                                                 \
    do {                                                                       \
        u32 bit;                                                               \
                                                                               \
        if (bits == 0) {                                                       \
            word = *next++;                                                    \
            bits = BITSTREAM_WORD_BITS;                                        \
        }                                                                      \
        bit = word >> (BITSTREAM_WORD_BITS - 1);                               \
        word <<= 1;                                                            \
        bits--;                                                                \
        if (bit) {                                                             \
            break;                                                             \
        }                                                                      \
        count++;                                                               \
    } while (count < (most));

/* A number packed so that small ones are short: `count` zero bits say how
 * many bits follow a leading one (none for 0 and 1); `most` zeros, that the
 * number is `most` bits long. */
#define GET_SIZED(dst, count, most)                                            \
    COUNT_ZEROS(count, most);                                                  \
    if (count == (most)) {                                                     \
        GETBITS_PRE(dst, (most) - 1, 1 << ((most) - 1));                       \
    } else if (count >= 2) {                                                   \
        count = count - 1;                                                     \
        GETBITS_PRE(dst, count, 1 << count);                                   \
    } else {                                                                   \
        dst = count;                                                           \
    }

/* Copies a frame's worth of halfwords, a word at a time. */
#define COPY_FRAME(d, s)                                                       \
    {                                                                          \
        u32 *from = (u32 *)(s);                                                \
        u32 *to = (u32 *)(d);                                                  \
        u32 k;                                                                 \
        for (k = 0; k < sizeof(MotionFrame) / 4; k++) {                        \
            *to++ = *from++;                                                   \
        }                                                                      \
    }

/* Gives decoder `decoder_index` the stream at `next` and reads its head:
 * the opening pose and the codes. Nothing is kept of any stream before. */
void motion_OpenStream(s32 decoder_index, u32 *next) {
    MotionDecoder *decoder;
    s32 i;
    u32 word;
    s32 bits;

    bits = BITSTREAM_WORD_BITS;
    decoder = &g_motion_decoders[decoder_index];
    decoder->stream = next;
    word = *next++;
    for (i = 0; i < MOTION_PACKED_VALUES; i++) {
        GETBITS(decoder->pose[i + PACKED], VALUE_BITS);
    }
    for (i = 0; i < MOTION_PACKED_VALUES; i++) {
        GETBITS(decoder->code[i + PACKED], 2);
    }
    for (i = 0; i < MOTION_DECODER_CACHES; i++) {
        decoder->cache[i].frame = MOTION_NO_FRAME;
    }
    decoder->turn = 0;
}

/* The decoder's four kept frames are used in turn. Of the one whose turn it
 * is and the one before: if either is the frame asked for, that is the
 * answer; if either is the frame before it, decoding goes on from there;
 * otherwise it starts from the head of the frame's group. The frame is kept
 * in the one whose turn it is, unless it is the last of its group, which
 * nothing can go on from. */
void motion_DecodeFrame(
    s32 decoder_index, s32 frame_index, MotionFrame *out, u16 *work) {
    MotionDecoder *decoder;
    u8 *group_table;
    MotionDecoderCache *cache;
    MotionDecoderCache *prev;
    u32 *next;
    u32 word;
    s32 bits;
    s32 index_in_group;
    s32 group_index;
    s32 old_turn;
    s32 i;
    s32 value_index;
    s32 step; /* how many frames past the group's first */
    s32 offset;
    s32 shift;
    u16 *value;
    u16 code;
    u32 field;
    s16 raw; /* a value as its 12 bits */

    index_in_group = frame_index & (GROUP_FRAMES - 1);
    group_index = frame_index >> 3; /* GROUP_FRAMES to a group */
    decoder = &g_motion_decoders[decoder_index];
    group_table = (u8 *)decoder->stream + GROUP_TABLE_OFFSET;
    if (!g_motion_codes_in_work) {
        COPY_FRAME(work + WORK_CODES, decoder->code);
    }
    old_turn = decoder->turn;
    decoder->turn = old_turn + 1;
    cache = &decoder->cache[(old_turn + 1) & (MOTION_DECODER_CACHES - 1)];
    if (cache->frame == frame_index) {
        COPY_FRAME(out, cache->pose);
        decoder->turn -= 1;
        return;
    }
    prev = &decoder->cache[old_turn & (MOTION_DECODER_CACHES - 1)];
    if (prev->frame == frame_index) {
        COPY_FRAME(out, prev->pose);
        decoder->turn -= 1;
        return;
    }
    if (cache->frame != frame_index - 1) {
        if (prev->frame == frame_index - 1) {
            cache = prev;
        } else {
            /* the first frame of the group: every value that is not the
             * opening pose's is there in full */
            offset = ((group_table[group_index * 3] << 16) |
                      (group_table[group_index * 3 + 1] << 8) |
                      group_table[group_index * 3 + 2]) +
                     GROUP_TABLE_BITS;
            /* the word the bits begin in, and how far into it */
            next = decoder->stream + (offset >> 5);
            shift = offset & (BITSTREAM_WORD_BITS - 1);
            bits = BITSTREAM_WORD_BITS - shift;
            word = *next++ << shift;
            COPY_FRAME(work, decoder->pose);
            GET_ROOT(work[0]);
            GET_ROOT(work[1]);
            GET_ROOT(work[2]);
            for (i = 0; i < MOTION_PACKED_VALUES; i++) {
                GETBITS(field, 1);
                if (field) {
                    GETBITS(work[i + PACKED], VALUE_BITS);
                }
            }
            step = 0;
            goto decode;
        }
    }
    /* go on from the frame before */
    next = cache->next;
    bits = cache->bits;
    word = cache->word;
    COPY_FRAME(work, cache->pose);
    if (index_in_group >= 2) {
        COPY_FRAME(work + WORK_RATES, cache->rate);
    }
    step = index_in_group - 1;
decode:
    for (; step < index_in_group; step++) {
        GET_ROOT(work[0]);
        GET_ROOT(work[1]);
        GET_ROOT(work[2]);
        for (value_index = 0; value_index < MOTION_PACKED_VALUES;
             value_index++) {
            /* how much the value's rate of change has changed (and, before
             * that, the bit that says whether it has) */
            s16 change;

            code = work[value_index + WORK_CODES + PACKED];
            if (code == CODE_FIXED) {
                continue;
            }
            switch (code) {
            case CODE_SMALL: {
                s32 count;

                GET_SIZED(change, count, SMALL_MAX_BITS);
                /* the low bit is the sign */
                change = (change & 1) ? -(change / 2) - 1 : change / 2;
                break;
            }
            case CODE_RARE: {
                GETBITS(change, 1);
                if (change) {
                    change = 0;
                } else {
                    GETBITS(change, VALUE_BITS);
                }
                break;
            }
            case CODE_WIDE: {
                GETBITS(change, 1);
                if (change) {
                    s32 count;

                    /* three low bits and a sign, then the rest of the
                     * size */
                    GETBITS(field, WIDE_LOW_BITS + 1);
                    GET_SIZED(change, count, WIDE_MAX_BITS);
                    change =
                        ((change << WIDE_LOW_BITS) | (field & WIDE_LOW_MASK)) +
                        1;
                    if (field & WIDE_SIGN) {
                        change = -change;
                    }
                }
                break;
            }
            }
            if (step == 0) {
                /* the frame before was whole: it had no rate */
                work[value_index + WORK_RATES + PACKED] = change;
                work[value_index + PACKED] += change;
            } else {
                work[value_index + PACKED] +=
                    work[value_index + WORK_RATES + PACKED] + change;
                work[value_index + WORK_RATES + PACKED] += change;
            }
        }
    }
    /* two triples are signed: give their 12 bits a sign (`step` counts the
     * two) */
    value = &work[SIGNED_TRIPLE];
    for (step = 0; step < 2; step++) {
        for (i = 0; i < 3; i++) {
            raw = *value;
            *value =
                (raw & VALUE_SIGN) ? (raw | ~VALUE_MASK) : (raw & VALUE_MASK);
            value++;
        }
        value += 3;
    }
    COPY_FRAME(out, work);
    if (index_in_group < GROUP_FRAMES - 1) {
        cache->frame = frame_index;
        COPY_FRAME(cache->pose, work);
        COPY_FRAME(cache->rate, work + WORK_RATES);
        cache->next = next;
        cache->bits = bits;
        cache->word = word;
    }
}

/* Prints a frame's values, three to a line (a debug aid; nothing calls
 * it). */
static void motion_PrintFrame(u16 *values) {
    s32 i;
    u16 *third; /* the third value of the line */

    i = 0;
    third = values + 2;
    do {
        i++;
        font_Printf("%04x %04x %04x\n", values[0], third[-1], third[0]);
        third += 3;
        values += 3;
    } while (i < MOTION_FRAME_VALUES / 3);
}

/* Set if the caller's work area already holds the decoder's codes. Nothing
 * sets it. */
static s32 g_motion_codes_in_work = 0;

/* A text no code prints; it comes after the file's other string. */
static const char g_str_sound_id[] = "SOUND ID:%d\n";
