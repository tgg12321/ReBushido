/* The disc's sound in the native port (docs/PORT.md, "Sound"): XA
 * streams, which are all the music and speech the game has. On the
 * console the drive itself plays them: a sector of sound that passes
 * under it, and is of the file and channel the game has chosen, is
 * decoded by the drive's own chip and goes to the sound chip as the
 * disc's sound, like a CD's. Here the port's drive (cd.c) hands such a
 * sector to port_FeedXaSector as it passes, and the mix reads the
 * result with port_ReadCdAudio.
 *
 * A sector holds 18 groups of 128 bytes. A group is eight blocks of 28
 * four-bit samples (or four of eight bits) with a shift and a filter
 * each, the samples of the blocks interleaved; in stereo the blocks are
 * left and right by turns. A sector says whether it is stereo, whether
 * its samples are 37,800 or 18,900 a second, and their size.
 *
 * The decoding is the format's, to the bit. The drive then brings
 * 37,800 samples a second to the 44,100 of the sound chip, seven out
 * for every six in, by a filter of 29 coefficients for each of the
 * seven places: those tables are not reproduced. Here each sample out
 * is a cubic through the four samples around its place, which passes
 * the same notes and differs only near the top of what can be heard.
 * Samples at 18,900 a second are each played twice first, as the drive
 * does. */
#include "common.h"
#include "port_disc.h"
#include "port_spu.h"
#include "bb2.h"

/* The subheader's bytes, from the sector's 16th: file, channel, submode
 * and coding. */
#define SUBHEADER (PORT_DISC_SYNC_BYTES + PORT_DISC_HEADER_BYTES)
#define SUBHEADER_SUBMODE 2
#define SUBHEADER_CODING 3
#define SECTOR_MODE (PORT_DISC_SYNC_BYTES + 3)
#define MODE_2 2
#define SUBMODE_AUDIO 0x04
#define CODING_STEREO 0x01
#define CODING_HALF_RATE 0x04
#define CODING_8_BIT 0x10

#define GROUPS 18
#define GROUP_BYTES 128
/* A group's sixteen bytes of headers: the blocks' own are from the
 * fourth, and the rest are copies. */
#define GROUP_HEADERS 4
#define GROUP_DATA 16
#define BLOCK_SAMPLES 28
#define BLOCKS_4_BIT 8
#define BLOCKS_8_BIT 4
#define HEADER_SHIFT_MASK 0x0F
#define HEADER_FILTER_SHIFT 4
#define HEADER_FILTER_MASK 0x03
/* Shifts that would leave nothing of a sample are taken for these. */
#define SHIFT_MAX_4_BIT 12
#define SHIFT_INVALID_4_BIT 9
#define SHIFT_MAX_8_BIT 8
#define SHIFT_INVALID_8_BIT 8
#define FILTER_SHIFT 6
#define FILTER_ROUND 32

#define SAMPLE_MAX 0x7FFF
#define SAMPLE_MIN (-0x8000)

/* The most a sector holds: one channel of four bits. */
#define SECTOR_SAMPLES_MAX (GROUPS * BLOCKS_4_BIT * BLOCK_SAMPLES)

/* What is waiting to be played, at 37,800 a second: room for a few
 * sectors, of which the drive sends one about every three blanks. */
#define QUEUE_FRAMES 0x8000
#define QUEUE_MASK (QUEUE_FRAMES - 1)

/* Seven samples out for six in. */
#define RESAMPLE_OUT 7
#define RESAMPLE_IN 6
/* The cubic's weights are in this many parts: twice the cube of
 * seven. */
#define CUBIC_ONE (2 * RESAMPLE_OUT * RESAMPLE_OUT * RESAMPLE_OUT)
#define NIBBLE_MASK 0xF

/* The drive's volumes are in 128ths. */
#define MIX_SHIFT 7
#define MIX_ALL 0x80

static const s32 g_port_xa_filters[4][2] = {
    {0, 0},
    {60, 0},
    {115, -52},
    {98, -55},
};

static struct {
    /* the decoder's last two samples, left and right */
    s32 old[2];
    s32 older[2];
    /* what is waiting: a left and a right each */
    s16 queue[QUEUE_FRAMES][2];
    u32 queue_read;
    u32 queue_count;
    /* the four samples around the place being played, and how many
     * sevenths past the second of them it is */
    s32 window[4][2];
    u32 phase;
    u8 mix[4];
    s32 muted;
    u32 sectors;
} g_port_xa = {.mix = {MIX_ALL, 0, MIX_ALL, 0}};

static s32 port_ClampXaSample(s32 value) {
    if (value > SAMPLE_MAX) {
        return SAMPLE_MAX;
    }
    if (value < SAMPLE_MIN) {
        return SAMPLE_MIN;
    }
    return value;
}

/* The 28 samples of block `block` of a group into `out`, every `stride`
 * places: given the channel's last two samples, which are left as the
 * block's last two. */
static void port_DecodeXaBlock(const u8 *group, u32 block, s32 wide, s32 *old,
                               s32 *older, s16 *out, u32 stride) {
    u32 header = group[GROUP_HEADERS + block];
    u32 shift = header & HEADER_SHIFT_MASK;
    u32 filter = (header >> HEADER_FILTER_SHIFT) & HEADER_FILTER_MASK;
    u32 i;

    if (wide) {
        if (shift > SHIFT_MAX_8_BIT) {
            shift = SHIFT_INVALID_8_BIT;
        }
    } else if (shift > SHIFT_MAX_4_BIT) {
        shift = SHIFT_INVALID_4_BIT;
    }
    for (i = 0; i < BLOCK_SAMPLES; i++) {
        s32 sample;

        if (wide) {
            sample = (s16)(group[GROUP_DATA + i * 4 + block] << 8) >> shift;
        } else {
            u32 byte = group[GROUP_DATA + i * 4 + block / 2];
            u32 nibble = (block & 1) ? byte >> 4 : byte & NIBBLE_MASK;

            sample = (s16)(nibble << 12) >> shift;
        }
        sample += (*old * g_port_xa_filters[filter][0] +
                   *older * g_port_xa_filters[filter][1] + FILTER_ROUND) >>
                  FILTER_SHIFT;
        sample = port_ClampXaSample(sample);
        out[i * stride] = (s16)sample;
        *older = *old;
        *old = sample;
    }
}

/* The samples of a sector of sound, `raw` its 2352 bytes, into
 * `samples`: left and right by turns if `*stereo`. Returns how many
 * there are, a left and a right counted as two; 0 if it is not a sector
 * of sound. `*half_rate` says they are 18,900 a second. */
static u32 port_DecodeXaSector(const u8 *raw, s32 *old, s32 *older,
                               s16 *samples, s32 *stereo, s32 *half_rate) {
    const u8 *subheader = raw + SUBHEADER;
    u32 coding = subheader[SUBHEADER_CODING];
    s32 wide = (coding & CODING_8_BIT) != 0;
    u32 blocks = wide ? BLOCKS_8_BIT : BLOCKS_4_BIT;
    u32 count = 0;
    u32 group;
    u32 block;

    if (raw[SECTOR_MODE] != MODE_2 ||
        !(subheader[SUBHEADER_SUBMODE] & PORT_DISC_SUBMODE_FORM2) ||
        !(subheader[SUBHEADER_SUBMODE] & SUBMODE_AUDIO)) {
        return 0;
    }
    *stereo = (coding & CODING_STEREO) != 0;
    *half_rate = (coding & CODING_HALF_RATE) != 0;
    for (group = 0; group < GROUPS; group++) {
        const u8 *bytes = raw + PORT_DISC_DATA_OFFSET + group * GROUP_BYTES;

        if (*stereo) {
            for (block = 0; block < blocks; block++) {
                u32 side = block & 1;

                port_DecodeXaBlock(
                    bytes, block, wide, &old[side], &older[side],
                    samples + count + (block / 2) * BLOCK_SAMPLES * 2 + side,
                    2);
            }
        } else {
            for (block = 0; block < blocks; block++) {
                port_DecodeXaBlock(bytes, block, wide, &old[0], &older[0],
                                   samples + count + block * BLOCK_SAMPLES, 1);
            }
        }
        count += blocks * BLOCK_SAMPLES;
    }
    return count;
}

/* A left and a right to the end of what is waiting. */
static void port_QueueXaFrame(s32 left, s32 right) {
    u32 at;

    if (g_port_xa.queue_count >= QUEUE_FRAMES) {
        /* more than the mix has taken: the oldest is lost */
        g_port_xa.queue_read = (g_port_xa.queue_read + 1) & QUEUE_MASK;
        g_port_xa.queue_count--;
    }
    at = (g_port_xa.queue_read + g_port_xa.queue_count) & QUEUE_MASK;
    g_port_xa.queue[at][0] = (s16)left;
    g_port_xa.queue[at][1] = (s16)right;
    g_port_xa.queue_count++;
}

/* A sector as the drive passes it (port_spu.h). */
u32 port_FeedXaSector(const u8 *raw) {
    static s16 samples[SECTOR_SAMPLES_MAX];
    s32 stereo = 0;
    s32 half_rate = 0;
    u32 count = port_DecodeXaSector(
        raw, g_port_xa.old, g_port_xa.older, samples, &stereo, &half_rate);
    u32 step = stereo ? 2 : 1;
    u32 i;

    if (count == 0) {
        return 0;
    }
    for (i = 0; i < count; i += step) {
        s32 left = samples[i];
        s32 right = stereo ? samples[i + 1] : left;

        port_QueueXaFrame(left, right);
        if (half_rate) {
            port_QueueXaFrame(left, right);
        }
    }
    g_port_xa.sectors++;
    return count;
}

/* A new stream (port_spu.h): nothing waiting, nothing remembered. */
void port_ResetCdAudio(void) {
    u32 i;

    for (i = 0; i < 2; i++) {
        g_port_xa.old[i] = 0;
        g_port_xa.older[i] = 0;
    }
    for (i = 0; i < 4; i++) {
        g_port_xa.window[i][0] = 0;
        g_port_xa.window[i][1] = 0;
    }
    g_port_xa.queue_read = 0;
    g_port_xa.queue_count = 0;
    g_port_xa.phase = 0;
}

void port_SetCdMix(
    u8 left_to_left, u8 left_to_right, u8 right_to_right, u8 right_to_left) {
    g_port_xa.mix[0] = left_to_left;
    g_port_xa.mix[1] = left_to_right;
    g_port_xa.mix[2] = right_to_right;
    g_port_xa.mix[3] = right_to_left;
}

void port_MuteCdAudio(s32 muted) { g_port_xa.muted = muted; }

u32 port_GetXaSectorCount(void) { return g_port_xa.sectors; }

/* The window goes on by one sample of what is waiting; silence if
 * nothing is. */
static void port_AdvanceXaWindow(void) {
    u32 i;

    for (i = 0; i < 3; i++) {
        g_port_xa.window[i][0] = g_port_xa.window[i + 1][0];
        g_port_xa.window[i][1] = g_port_xa.window[i + 1][1];
    }
    if (g_port_xa.queue_count == 0) {
        g_port_xa.window[3][0] = 0;
        g_port_xa.window[3][1] = 0;
        return;
    }
    g_port_xa.window[3][0] = g_port_xa.queue[g_port_xa.queue_read][0];
    g_port_xa.window[3][1] = g_port_xa.queue[g_port_xa.queue_read][1];
    g_port_xa.queue_read = (g_port_xa.queue_read + 1) & QUEUE_MASK;
    g_port_xa.queue_count--;
}

/* Where the disc's sound is copied before the drive's volumes, if not
 * NULL (port_spu.h): set once, as the program starts, for --dump-music. */
s16 *g_port_xa_unmixed;

/* The drive's volumes and its mute on `frames` samples, a left and a
 * right each (port_spu.h). */
void port_MixCdAudio(s16 *samples, u32 frames) {
    u32 frame;

    for (frame = 0; frame < frames; frame++) {
        s32 left = samples[frame * 2];
        s32 right = samples[frame * 2 + 1];

        if (g_port_xa.muted) {
            samples[frame * 2] = 0;
            samples[frame * 2 + 1] = 0;
        } else {
            samples[frame * 2] = (s16)port_ClampXaSample(
                (left * g_port_xa.mix[0] + right * g_port_xa.mix[3]) >>
                MIX_SHIFT);
            samples[frame * 2 + 1] = (s16)port_ClampXaSample(
                (right * g_port_xa.mix[2] + left * g_port_xa.mix[1]) >>
                MIX_SHIFT);
        }
    }
}

/* The next samples at the sound chip's rate (port_spu.h). */
void port_ReadCdAudio(s16 *out, u32 frames) {
    u32 frame;
    u32 side;

    for (frame = 0; frame < frames; frame++) {
        /* the cubic through the window's four samples (Catmull and
         * Rom's), at `phase` sevenths past its second: with t the
         * place between 0 and 1, the weights are (-t3 + 2t2 - t) / 2,
         * (3t3 - 5t2 + 2) / 2, (-3t3 + 4t2 + t) / 2 and (t3 - t2) / 2,
         * here in whole numbers, times CUBIC_ONE */
        s32 phase = (s32)g_port_xa.phase;
        s32 one = RESAMPLE_OUT;
        s32 square = phase * phase;
        s32 cube = square * phase;
        s32 weights[4];

        weights[0] = -cube + 2 * one * square - one * one * phase;
        weights[2] = -3 * cube + 4 * one * square + one * one * phase;
        weights[3] = cube - one * square;
        /* the four add up to one */
        weights[1] = CUBIC_ONE - weights[0] - weights[2] - weights[3];
        for (side = 0; side < 2; side++) {
            out[frame * 2 + side] = (s16)port_ClampXaSample(
                (g_port_xa.window[0][side] * weights[0] +
                 g_port_xa.window[1][side] * weights[1] +
                 g_port_xa.window[2][side] * weights[2] +
                 g_port_xa.window[3][side] * weights[3]) /
                CUBIC_ONE);
        }
        g_port_xa.phase += RESAMPLE_IN;
        while (g_port_xa.phase >= RESAMPLE_OUT) {
            g_port_xa.phase -= RESAMPLE_OUT;
            port_AdvanceXaWindow();
        }
    }
    if (g_port_xa_unmixed != NULL) {
        for (frame = 0; frame < frames * 2; frame++) {
            g_port_xa_unmixed[frame] = out[frame];
        }
    }
    port_MixCdAudio(out, frames);
}
