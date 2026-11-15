/* The PlayStation's sound chip in the native port (docs/PORT.md,
 * "Sound"): 24 voices that play compressed waveforms out of 512 KB of
 * sound memory, each at its own pitch, under its own envelope and
 * volume; a reverb; the disc's sound mixed in; a left and a right of 16
 * bits, 44,100 times a second.
 *
 * The program does not call this to make a sound. Sony's libraries
 * write the chip's registers, as on the console, and the chip reads
 * them when it makes its next samples (port_RunSpu). The registers are
 * plain memory, so the one thing a write does on the console that it
 * cannot do here, starting or stopping a voice, is done when the chip
 * next looks (port_TakeSpuKeys).
 *
 * Written from what is publicly known of the hardware; no emulator's
 * code. What is certain and what is not:
 *
 *   - the waveform's decoding, its five filters and three flags, the
 *     pitch counter and the envelope's arithmetic are the hardware's as
 *     it is documented, to the bit;
 *   - the table of the four-point interpolation is the chip's own, 512
 *     numbers; no formula is known that gives it (a Gaussian comes
 *     within 320 of its 23,000 at best), so it is written out;
 *   - the reverb is the documented network of two reflections, four
 *     combs and two all-pass filters at half the rate. The chip filters
 *     the sound on its way down to that rate and up again with 39
 *     coefficients that are not reproduced: here two samples are
 *     averaged on the way down and the result is drawn as a line on
 *     the way up;
 *   - a volume that sweeps is stepped as an envelope is, which is right
 *     for a rising one and a guess for the rest; the game sets none;
 *   - the few samples the chip takes to start a voice after its key and
 *     the interrupt at an address are not done: the game uses neither;
 *   - a waveform whose block names a filter above the fifth is played
 *     with none.
 */
#include "common.h"
#include "port_spu.h"
#include "bb2.h"

#define SAMPLE_MAX 0x7FFF
#define SAMPLE_MIN (-0x8000)
/* Volumes and the envelope are fractions of this. */
#define VOLUME_SHIFT 15
#define LEVEL_MAX 0x7FFF

/* ---- a waveform's blocks ---- */

/* Sixteen bytes: a shift and a filter, the flags, and 28 samples of four
 * bits. */
#define BLOCK_BYTES 16
#define BLOCK_SAMPLES 28
#define BLOCK_SHIFT_MASK 0x0F
#define BLOCK_FILTER_SHIFT 4
#define BLOCK_FILTER_MASK 0x07
#define BLOCK_FLAGS 1
#define BLOCK_DATA 2
/* The flags: this block is the waveform's last (the voice goes to the
 * loop's start after it); and then it goes on playing, where without
 * this it falls silent; the loop starts at this block. */
#define FLAG_END 0x01
#define FLAG_REPEAT 0x02
#define FLAG_LOOP_START 0x04
/* A shift above this would leave nothing of a sample; the chip then
 * shifts by 9. */
#define SHIFT_MAX 12
#define SHIFT_INVALID 9
#define FILTER_COUNT 5
/* The filters are in 64ths; half of one is added before the division. */
#define FILTER_SHIFT 6
#define FILTER_ROUND 32

/* How much of the sample before and of the one before that each filter
 * adds. */
static const s32 g_port_spu_filters[FILTER_COUNT][2] = {
    {0, 0}, {60, 0}, {115, -52}, {98, -55}, {122, -60},
};

/* ---- pitch ---- */

/* The pitch counter: 12 bits of fraction, of which the top eight choose
 * the interpolation's coefficients. 0x1000 a sample is the waveform as
 * recorded. */
#define COUNTER_ONE 0x1000
#define COUNTER_INDEX_SHIFT 4
#define COUNTER_INDEX_MASK 0xFF
/* Where in the table the four samples' weights are for the first place
 * between two samples: the oldest's and the second's run down from
 * these, the third's and the newest's up. */
#define GAUSS_OLDEST 0xFF
#define GAUSS_SECOND 0x1FF
#define GAUSS_THIRD 0x100
#define GAUSS_NEWEST 0
#define PITCH_MAX 0x3FFF
#define PITCH_LIMIT 0x4000
#define MODULATION_CENTER 0x8000

/* The chip's interpolation table: the weight of each of four samples
 * for 256 places between two of them. Out of its ROM, as documented;
 * tests/port/spu.py holds it to what such a table must be (it rises,
 * smoothly, and the four weights of a place add up to one, within one
 * part in 32,000). */
static const s16 g_port_spu_gauss[512] = {
    -0x0001, -0x0001, -0x0001, -0x0001, -0x0001, -0x0001, -0x0001, -0x0001,
    -0x0001, -0x0001, -0x0001, -0x0001, -0x0001, -0x0001, -0x0001, -0x0001,
    0x0000,  0x0000,  0x0000,  0x0000,  0x0000,  0x0000,  0x0000,  0x0001,
    0x0001,  0x0001,  0x0001,  0x0002,  0x0002,  0x0002,  0x0003,  0x0003,
    0x0003,  0x0004,  0x0004,  0x0005,  0x0005,  0x0006,  0x0007,  0x0007,
    0x0008,  0x0009,  0x0009,  0x000A,  0x000B,  0x000C,  0x000D,  0x000E,
    0x000F,  0x0010,  0x0011,  0x0012,  0x0013,  0x0015,  0x0016,  0x0018,
    0x0019,  0x001B,  0x001C,  0x001E,  0x0020,  0x0021,  0x0023,  0x0025,
    0x0027,  0x0029,  0x002C,  0x002E,  0x0030,  0x0033,  0x0035,  0x0038,
    0x003A,  0x003D,  0x0040,  0x0043,  0x0046,  0x0049,  0x004D,  0x0050,
    0x0054,  0x0057,  0x005B,  0x005F,  0x0063,  0x0067,  0x006B,  0x006F,
    0x0074,  0x0078,  0x007D,  0x0082,  0x0087,  0x008C,  0x0091,  0x0096,
    0x009C,  0x00A1,  0x00A7,  0x00AD,  0x00B3,  0x00BA,  0x00C0,  0x00C7,
    0x00CD,  0x00D4,  0x00DB,  0x00E3,  0x00EA,  0x00F2,  0x00FA,  0x0101,
    0x010A,  0x0112,  0x011B,  0x0123,  0x012C,  0x0135,  0x013F,  0x0148,
    0x0152,  0x015C,  0x0166,  0x0171,  0x017B,  0x0186,  0x0191,  0x019C,
    0x01A8,  0x01B4,  0x01C0,  0x01CC,  0x01D9,  0x01E5,  0x01F2,  0x0200,
    0x020D,  0x021B,  0x0229,  0x0237,  0x0246,  0x0255,  0x0264,  0x0273,
    0x0283,  0x0293,  0x02A3,  0x02B4,  0x02C4,  0x02D6,  0x02E7,  0x02F9,
    0x030B,  0x031D,  0x0330,  0x0343,  0x0356,  0x036A,  0x037E,  0x0392,
    0x03A7,  0x03BC,  0x03D1,  0x03E7,  0x03FC,  0x0413,  0x042A,  0x0441,
    0x0458,  0x0470,  0x0488,  0x04A0,  0x04B9,  0x04D2,  0x04EC,  0x0506,
    0x0520,  0x053B,  0x0556,  0x0572,  0x058E,  0x05AA,  0x05C7,  0x05E4,
    0x0601,  0x061F,  0x063E,  0x065C,  0x067C,  0x069B,  0x06BB,  0x06DC,
    0x06FD,  0x071E,  0x0740,  0x0762,  0x0784,  0x07A7,  0x07CB,  0x07EF,
    0x0813,  0x0838,  0x085D,  0x0883,  0x08A9,  0x08D0,  0x08F7,  0x091E,
    0x0946,  0x096F,  0x0998,  0x09C1,  0x09EB,  0x0A16,  0x0A40,  0x0A6C,
    0x0A98,  0x0AC4,  0x0AF1,  0x0B1E,  0x0B4C,  0x0B7A,  0x0BA9,  0x0BD8,
    0x0C07,  0x0C38,  0x0C68,  0x0C99,  0x0CCB,  0x0CFD,  0x0D30,  0x0D63,
    0x0D97,  0x0DCB,  0x0E00,  0x0E35,  0x0E6B,  0x0EA1,  0x0ED7,  0x0F0F,
    0x0F46,  0x0F7F,  0x0FB7,  0x0FF1,  0x102A,  0x1065,  0x109F,  0x10DB,
    0x1116,  0x1153,  0x118F,  0x11CD,  0x120B,  0x1249,  0x1288,  0x12C7,
    0x1307,  0x1347,  0x1388,  0x13C9,  0x140B,  0x144D,  0x1490,  0x14D4,
    0x1517,  0x155C,  0x15A0,  0x15E6,  0x162C,  0x1672,  0x16B9,  0x1700,
    0x1747,  0x1790,  0x17D8,  0x1821,  0x186B,  0x18B5,  0x1900,  0x194B,
    0x1996,  0x19E2,  0x1A2E,  0x1A7B,  0x1AC8,  0x1B16,  0x1B64,  0x1BB3,
    0x1C02,  0x1C51,  0x1CA1,  0x1CF1,  0x1D42,  0x1D93,  0x1DE5,  0x1E37,
    0x1E89,  0x1EDC,  0x1F2F,  0x1F82,  0x1FD6,  0x202A,  0x207F,  0x20D4,
    0x2129,  0x217F,  0x21D5,  0x222C,  0x2282,  0x22DA,  0x2331,  0x2389,
    0x23E1,  0x2439,  0x2492,  0x24EB,  0x2545,  0x259E,  0x25F8,  0x2653,
    0x26AD,  0x2708,  0x2763,  0x27BE,  0x281A,  0x2876,  0x28D2,  0x292E,
    0x298B,  0x29E7,  0x2A44,  0x2AA1,  0x2AFF,  0x2B5C,  0x2BBA,  0x2C18,
    0x2C76,  0x2CD4,  0x2D33,  0x2D91,  0x2DF0,  0x2E4F,  0x2EAE,  0x2F0D,
    0x2F6C,  0x2FCC,  0x302B,  0x308B,  0x30EA,  0x314A,  0x31AA,  0x3209,
    0x3269,  0x32C9,  0x3329,  0x3389,  0x33E9,  0x3449,  0x34A9,  0x3509,
    0x3569,  0x35C9,  0x3629,  0x3689,  0x36E8,  0x3748,  0x37A8,  0x3807,
    0x3867,  0x38C6,  0x3926,  0x3985,  0x39E4,  0x3A43,  0x3AA2,  0x3B00,
    0x3B5F,  0x3BBD,  0x3C1B,  0x3C79,  0x3CD7,  0x3D35,  0x3D92,  0x3DEF,
    0x3E4C,  0x3EA9,  0x3F05,  0x3F62,  0x3FBD,  0x4019,  0x4074,  0x40D0,
    0x412A,  0x4185,  0x41DF,  0x4239,  0x4292,  0x42EB,  0x4344,  0x439C,
    0x43F4,  0x444C,  0x44A3,  0x44FA,  0x4550,  0x45A6,  0x45FC,  0x4651,
    0x46A6,  0x46FA,  0x474E,  0x47A1,  0x47F4,  0x4846,  0x4898,  0x48E9,
    0x493A,  0x498A,  0x49D9,  0x4A29,  0x4A77,  0x4AC5,  0x4B13,  0x4B5F,
    0x4BAC,  0x4BF7,  0x4C42,  0x4C8D,  0x4CD7,  0x4D20,  0x4D68,  0x4DB0,
    0x4DF7,  0x4E3E,  0x4E84,  0x4EC9,  0x4F0E,  0x4F52,  0x4F95,  0x4FD7,
    0x5019,  0x505A,  0x509A,  0x50DA,  0x5118,  0x5156,  0x5194,  0x51D0,
    0x520C,  0x5247,  0x5281,  0x52BA,  0x52F3,  0x532A,  0x5361,  0x5397,
    0x53CC,  0x5401,  0x5434,  0x5467,  0x5499,  0x54CA,  0x54FA,  0x5529,
    0x5558,  0x5585,  0x55B2,  0x55DE,  0x5609,  0x5632,  0x565B,  0x5684,
    0x56AB,  0x56D1,  0x56F6,  0x571B,  0x573E,  0x5761,  0x5782,  0x57A3,
    0x57C3,  0x57E2,  0x57FF,  0x581C,  0x5838,  0x5853,  0x586D,  0x5886,
    0x589E,  0x58B5,  0x58CB,  0x58E0,  0x58F4,  0x5907,  0x5919,  0x592A,
    0x593A,  0x5949,  0x5958,  0x5965,  0x5971,  0x597C,  0x5986,  0x598F,
    0x5997,  0x599E,  0x59A4,  0x59A9,  0x59AD,  0x59B0,  0x59B2,  0x59B3,
};

/* ---- the envelope ---- */

/* A voice's envelope goes up to full (attack), down to the sustain level
 * (decay), on from there as the sustain's rate says, and at the key off
 * down to nothing (release). */
enum {
    PHASE_OFF = 0,
    PHASE_ATTACK = 1,
    PHASE_DECAY = 2,
    PHASE_SUSTAIN = 3,
    PHASE_RELEASE = 4,
};

/* A rate is seven bits: a shift of five and a step of two. */
#define RATE_SHIFT_SHIFT 2
#define RATE_STEP_MASK 3
#define RATE_MASK 0x7F
/* The step: 7 less the two bits going up, 8 less them going down. */
#define STEP_UP 7
#define STEP_DOWN (-8)
/* Below this shift the step is doubled for each one less; above it the
 * step waits twice as many samples for each one more. */
#define RATE_SHIFT_UNIT 11
/* A level that rises exponentially rises four times slower above
 * this. */
#define LEVEL_SLOW 0x6000
#define SLOW_FACTOR 4

/* The low envelope register: the attack's mode and rate, the decay's
 * shift, the sustain level in 16ths. */
#define ADSR_ATTACK_EXPONENTIAL 0x8000
#define ADSR_ATTACK_RATE_SHIFT 8
#define ADSR_DECAY_SHIFT 4
#define ADSR_DECAY_MASK 0xF
#define ADSR_SUSTAIN_LEVEL_MASK 0xF
#define SUSTAIN_LEVEL_UNIT 0x800
/* The high one: the sustain's mode, direction and rate, the release's
 * mode and shift. */
#define ADSR_SUSTAIN_EXPONENTIAL 0x8000
#define ADSR_SUSTAIN_DOWN 0x4000
#define ADSR_SUSTAIN_RATE_SHIFT 6
#define ADSR_RELEASE_EXPONENTIAL 0x0020
#define ADSR_RELEASE_MASK 0x1F

/* A volume register: a fixed volume of 15 bits, or with this bit a
 * sweep, whose bits are an envelope's. */
#define VOLUME_SWEEP 0x8000
#define SWEEP_EXPONENTIAL 0x4000
#define SWEEP_DOWN 0x2000
#define SWEEP_NEGATIVE 0x1000

/* ---- noise ---- */

#define NOISE_STEP_BASE 4
#define NOISE_STEP_MASK 3
#define NOISE_SHIFT_SHIFT 2
#define NOISE_RELOAD 0x20000
#define NOISE_CLOCK_MASK 0x3F

/* ---- the reverb ---- */

/* Its registers, by halfword from PORT_SPU_REVERB_REGS on: names as
 * they are written about. d and m are places in the reverb's memory, in
 * units of 8 bytes; v are volumes. */
enum {
    REVERB_dAPF1,
    REVERB_dAPF2,
    REVERB_vIIR,
    REVERB_vCOMB1,
    REVERB_vCOMB2,
    REVERB_vCOMB3,
    REVERB_vCOMB4,
    REVERB_vWALL,
    REVERB_vAPF1,
    REVERB_vAPF2,
    REVERB_mLSAME,
    REVERB_mRSAME,
    REVERB_mLCOMB1,
    REVERB_mRCOMB1,
    REVERB_mLCOMB2,
    REVERB_mRCOMB2,
    REVERB_dLSAME,
    REVERB_dRSAME,
    REVERB_mLDIFF,
    REVERB_mRDIFF,
    REVERB_mLCOMB3,
    REVERB_mRCOMB3,
    REVERB_mLCOMB4,
    REVERB_mRCOMB4,
    REVERB_dLDIFF,
    REVERB_dRDIFF,
    REVERB_mLAPF1,
    REVERB_mRAPF1,
    REVERB_mLAPF2,
    REVERB_mRAPF2,
    REVERB_vLIN,
    REVERB_vRIN,
};

#define ADDRESS_UNIT 8
/* The start of sound memory is the chip's own: four buffers of 512
 * samples that it goes round writing, a sample each at every sample it
 * makes: the disc's left and right as they come in, and what voices 1
 * and 3 play, under their envelopes. */
#define CAPTURE_BYTES 0x400
#define CAPTURE_CD_LEFT 0x000
#define CAPTURE_CD_RIGHT 0x400
#define CAPTURE_VOICE_1 0x800
#define CAPTURE_VOICE_3 0xC00
/* A register of a bit a voice is two halfwords: sixteen voices in the
 * first. */
#define VOICES_PER_HALFWORD 16
#define RAM_MASK (PORT_SPU_RAM_BYTES - 1)
#define REVERB_SAMPLE_BYTES 2

typedef struct {
    u32 addr;        /* the next block's place in sound memory */
    u8 flags;        /* the flags of the block being played */
    u8 phase;        /* of the envelope */
    u8 sample_index; /* the next of the block's samples to play */
    s16 block[BLOCK_SAMPLES];
    s32 old; /* the last sample decoded, and the one before */
    s32 older;
    s16 recent[4];  /* the last four played, the newest last */
    u32 counter;    /* the pitch counter's fraction */
    s32 level;      /* the envelope: 0 to LEVEL_MAX */
    u32 level_wait; /* samples since it last stepped */
    s32 volume[2];  /* a volume that sweeps, as it is now */
    u32 volume_wait[2];
    s32 out; /* its last sample under its envelope: what
              * modulates the next voice's pitch */
} PortSpuVoice;

static struct {
    u16 *regs;
    PortSpuVoice voices[PORT_SPU_VOICES];
    s32 main_volume[2];
    u32 main_volume_wait[2];
    s32 noise_timer;
    u16 noise_level;
    /* the reverb: where in its memory it is; the sample waiting for its
     * pair; its last two answers */
    u32 reverb_addr;
    s32 reverb_odd;
    s32 reverb_held[2];
    s32 reverb_before[2];
    s32 reverb_now[2];
    u32 capture; /* where in its four buffers the chip writes next */
    PortSpuCounts counts;
    u8 ram[PORT_SPU_RAM_BYTES];
} g_port_spu;

void (*g_port_spu_key_on_hook)(s32 voice, u32 addr, u32 pitch);

static s32 port_ClampSample(s32 value) {
    if (value > SAMPLE_MAX) {
        return SAMPLE_MAX;
    }
    if (value < SAMPLE_MIN) {
        return SAMPLE_MIN;
    }
    return value;
}

/* ---- decoding ---- */

/* The 28 samples of the block at `block`, given the two samples before
 * them, which are left as the block's last two. */
static void port_DecodeSpuBlock(
    const u8 *block, s32 *old, s32 *older, s16 *samples) {
    u32 shift = block[0] & BLOCK_SHIFT_MASK;
    u32 filter = (block[0] >> BLOCK_FILTER_SHIFT) & BLOCK_FILTER_MASK;
    u32 i;

    if (shift > SHIFT_MAX) {
        shift = SHIFT_INVALID;
    }
    if (filter >= FILTER_COUNT) {
        filter = 0;
    }
    for (i = 0; i < BLOCK_SAMPLES; i++) {
        u32 byte = block[BLOCK_DATA + i / 2];
        /* the low four bits first; they are a signed number, put at
         * the top of 16 bits and shifted down */
        s32 nibble = (i & 1) ? (s32)(byte >> 4) : (s32)(byte & 0xF);
        s32 sample = (s16)(nibble << 12) >> shift;

        sample += (*old * g_port_spu_filters[filter][0] +
                   *older * g_port_spu_filters[filter][1] + FILTER_ROUND) >>
                  FILTER_SHIFT;
        sample = port_ClampSample(sample);
        samples[i] = (s16)sample;
        *older = *old;
        *old = sample;
    }
}

/* The sample between the last four, `index` 256ths of the way from the
 * third to the newest. */
static s32 port_InterpolateSpu(const s16 *recent, u32 index) {
    return ((g_port_spu_gauss[GAUSS_OLDEST - index] * recent[0]) >>
            VOLUME_SHIFT) +
           ((g_port_spu_gauss[GAUSS_SECOND - index] * recent[1]) >>
            VOLUME_SHIFT) +
           ((g_port_spu_gauss[GAUSS_THIRD + index] * recent[2]) >>
            VOLUME_SHIFT) +
           ((g_port_spu_gauss[GAUSS_NEWEST + index] * recent[3]) >>
            VOLUME_SHIFT);
}

/* The voice's next block. If the one it has just played was a
 * waveform's last, it goes back to the loop's start, and says it has
 * ended; without the flag to repeat it falls silent as well. */
static void port_FetchSpuBlock(PortSpuVoice *voice, u32 index) {
    u16 *regs = g_port_spu.regs + index * PORT_SPU_VOICE_REGS;

    if (voice->flags & FLAG_END) {
        g_port_spu.regs[PORT_SPU_ENDED + index / VOICES_PER_HALFWORD] |=
            (u16)(1U << (index % VOICES_PER_HALFWORD));
        voice->addr = (u32)regs[PORT_SPU_VOICE_LOOP] * ADDRESS_UNIT;
        if (!(voice->flags & FLAG_REPEAT)) {
            voice->level = 0;
            voice->phase = PHASE_OFF;
            g_port_spu.counts.ends++;
        }
    }
    voice->addr &= RAM_MASK & ~(u32)(BLOCK_BYTES - 1);
    voice->flags = g_port_spu.ram[voice->addr + BLOCK_FLAGS];
    if (voice->flags & FLAG_LOOP_START) {
        regs[PORT_SPU_VOICE_LOOP] = (u16)(voice->addr / ADDRESS_UNIT);
    }
    port_DecodeSpuBlock(
        g_port_spu.ram + voice->addr, &voice->old, &voice->older, voice->block);
    voice->addr += BLOCK_BYTES;
    voice->sample_index = 0;
}

/* The voice goes on by one of its waveform's samples. */
static void port_AdvanceSpuVoice(PortSpuVoice *voice, u32 index) {
    if (voice->sample_index >= BLOCK_SAMPLES) {
        port_FetchSpuBlock(voice, index);
    }
    voice->recent[0] = voice->recent[1];
    voice->recent[1] = voice->recent[2];
    voice->recent[2] = voice->recent[3];
    voice->recent[3] = voice->block[voice->sample_index++];
}

/* ---- the envelope ---- */

/* One sample of a level that rises or falls at `rate`: every so many
 * samples it takes a step, within 0 and LEVEL_MAX. Exponential, a fall's
 * step is in proportion to the level, and a rise waits four times as
 * long above LEVEL_SLOW. */
static void port_StepSpuLevel(
    s32 *level, u32 *wait, u32 rate, s32 down, s32 exponential) {
    u32 shift = rate >> RATE_SHIFT_SHIFT;
    s32 step = down ? STEP_DOWN + (s32)(rate & RATE_STEP_MASK)
                    : STEP_UP - (s32)(rate & RATE_STEP_MASK);
    u32 cycles = 1;

    if (shift < RATE_SHIFT_UNIT) {
        step *= 1 << (RATE_SHIFT_UNIT - shift);
    } else {
        cycles = 1U << (shift - RATE_SHIFT_UNIT);
    }
    if (exponential) {
        if (down) {
            step = (step * *level) >> VOLUME_SHIFT;
        } else if (*level > LEVEL_SLOW) {
            cycles *= SLOW_FACTOR;
        }
    }
    if (++*wait < cycles) {
        return;
    }
    *wait = 0;
    *level += step;
    if (*level > LEVEL_MAX) {
        *level = LEVEL_MAX;
    }
    if (*level < 0) {
        *level = 0;
    }
}

/* One sample of a voice's envelope, by its two registers. */
static void port_StepSpuEnvelope(PortSpuVoice *voice, u32 low, u32 high) {
    u8 phase = voice->phase;

    switch (phase) {
    case PHASE_ATTACK:
        port_StepSpuLevel(&voice->level, &voice->level_wait,
                          (low >> ADSR_ATTACK_RATE_SHIFT) & RATE_MASK, 0,
                          low & ADSR_ATTACK_EXPONENTIAL);
        if (voice->level >= LEVEL_MAX) {
            voice->phase = PHASE_DECAY;
        }
        break;
    case PHASE_DECAY:
        port_StepSpuLevel(
            &voice->level, &voice->level_wait,
            ((low >> ADSR_DECAY_SHIFT) & ADSR_DECAY_MASK) << RATE_SHIFT_SHIFT,
            1, 1);
        if (voice->level <=
            (s32)((low & ADSR_SUSTAIN_LEVEL_MASK) + 1) * SUSTAIN_LEVEL_UNIT) {
            voice->phase = PHASE_SUSTAIN;
        }
        break;
    case PHASE_SUSTAIN:
        port_StepSpuLevel(
            &voice->level, &voice->level_wait,
            (high >> ADSR_SUSTAIN_RATE_SHIFT) & RATE_MASK,
            high & ADSR_SUSTAIN_DOWN, high & ADSR_SUSTAIN_EXPONENTIAL);
        break;
    case PHASE_RELEASE:
        port_StepSpuLevel(&voice->level, &voice->level_wait,
                          (high & ADSR_RELEASE_MASK) << RATE_SHIFT_SHIFT, 1,
                          high & ADSR_RELEASE_EXPONENTIAL);
        if (voice->level == 0) {
            voice->phase = PHASE_OFF;
        }
        break;
    default:
        break;
    }
    if (voice->phase != phase) {
        voice->level_wait = 0;
    }
}

/* A volume register's volume for this sample: what it says, twice (15
 * bits to 16), or where its sweep has got to. */
static s32 port_StepSpuVolume(u32 reg, s32 *current, u32 *wait) {
    if (!(reg & VOLUME_SWEEP)) {
        *current = (s16)(reg << 1);
        *wait = 0;
        return *current;
    }
    g_port_spu.counts.swept++;
    if (*current < 0) {
        *current = -*current;
    }
    port_StepSpuLevel(current, wait, reg & RATE_MASK, reg & SWEEP_DOWN,
                      reg & SWEEP_EXPONENTIAL);
    return (reg & SWEEP_NEGATIVE) ? -*current : *current;
}

/* ---- keys ---- */

static u32 port_ReadSpuVoiceBits(u32 reg) {
    return g_port_spu.regs[reg] |
           ((u32)(g_port_spu.regs[reg + 1] & 0xFF) << 16);
}

/* A voice's key on: it starts its waveform from the start, with no
 * memory of the last one, its envelope at nothing and rising, and is no
 * longer one that has ended. */
static void port_KeyOnSpuVoice(u32 index) {
    PortSpuVoice *voice = &g_port_spu.voices[index];
    u16 *regs = g_port_spu.regs + index * PORT_SPU_VOICE_REGS;
    u32 i;

    voice->addr = (u32)regs[PORT_SPU_VOICE_START] * ADDRESS_UNIT;
    regs[PORT_SPU_VOICE_LOOP] = regs[PORT_SPU_VOICE_START];
    voice->flags = 0;
    voice->sample_index = BLOCK_SAMPLES;
    voice->old = 0;
    voice->older = 0;
    for (i = 0; i < 4; i++) {
        voice->recent[i] = 0;
    }
    voice->counter = 0;
    voice->level = 0;
    voice->level_wait = 0;
    voice->phase = PHASE_ATTACK;
    regs[PORT_SPU_VOICE_ENVELOPE] = 0;
    g_port_spu.regs[PORT_SPU_ENDED + index / VOICES_PER_HALFWORD] &=
        (u16) ~(1U << (index % VOICES_PER_HALFWORD));
    g_port_spu.counts.key_ons++;
    if (g_port_spu_key_on_hook != NULL) {
        g_port_spu_key_on_hook(
            (s32)index, voice->addr, regs[PORT_SPU_VOICE_PITCH]);
    }
    port_AdvanceSpuVoice(voice, index);
}

/* The keys written since the chip last looked (port_spu.h): off first,
 * so that a voice keyed off and on in one go starts. */
void port_TakeSpuKeys(void) {
    u32 on;
    u32 off;
    u32 index;

    if (g_port_spu.regs == NULL) {
        return;
    }
    off = port_ReadSpuVoiceBits(PORT_SPU_KEY_OFF);
    on = port_ReadSpuVoiceBits(PORT_SPU_KEY_ON);
    if ((on | off) == 0) {
        return;
    }
    g_port_spu.regs[PORT_SPU_KEY_OFF] = 0;
    g_port_spu.regs[PORT_SPU_KEY_OFF + 1] = 0;
    g_port_spu.regs[PORT_SPU_KEY_ON] = 0;
    g_port_spu.regs[PORT_SPU_KEY_ON + 1] = 0;
    for (index = 0; index < PORT_SPU_VOICES; index++) {
        PortSpuVoice *voice = &g_port_spu.voices[index];

        if ((off >> index) & 1) {
            if (voice->phase != PHASE_OFF && voice->phase != PHASE_RELEASE) {
                voice->phase = PHASE_RELEASE;
                voice->level_wait = 0;
            }
            g_port_spu.counts.key_offs++;
        }
        if ((on >> index) & 1) {
            port_KeyOnSpuVoice(index);
        }
    }
}

/* ---- a voice's sample ---- */

/* One sample of voice `index`, left and right. */
static void port_RunSpuVoice(u32 index, s32 *out) {
    PortSpuVoice *voice = &g_port_spu.voices[index];
    u16 *regs = g_port_spu.regs + index * PORT_SPU_VOICE_REGS;
    u32 bit = 1U << index;
    u32 step = regs[PORT_SPU_VOICE_PITCH];
    s32 sample;
    u32 side;

    if (index > 0 && (port_ReadSpuVoiceBits(PORT_SPU_PITCH_MODULATION) & bit)) {
        /* the voice before this one moves its pitch */
        s32 factor = g_port_spu.voices[index - 1].out + MODULATION_CENTER;

        step = (u32)(((s32)(s16)step * factor) >> VOLUME_SHIFT) & 0xFFFF;
        g_port_spu.counts.modulated++;
    }
    if (step > PITCH_MAX) {
        step = PITCH_LIMIT;
    }
    if (port_ReadSpuVoiceBits(PORT_SPU_NOISE) & bit) {
        sample = (s16)g_port_spu.noise_level;
        g_port_spu.counts.noise_samples++;
    } else {
        sample = port_InterpolateSpu(
            voice->recent,
            (voice->counter >> COUNTER_INDEX_SHIFT) & COUNTER_INDEX_MASK);
    }
    voice->out = (sample * voice->level) >> VOLUME_SHIFT;
    for (side = 0; side < 2; side++) {
        s32 volume = port_StepSpuVolume(
            regs[PORT_SPU_VOICE_VOLUME_LEFT + side], &voice->volume[side],
            &voice->volume_wait[side]);

        out[side] = (voice->out * volume) >> VOLUME_SHIFT;
    }
    port_StepSpuEnvelope(
        voice, regs[PORT_SPU_VOICE_ADSR_LOW], regs[PORT_SPU_VOICE_ADSR_HIGH]);
    voice->counter += step;
    while (voice->counter >= COUNTER_ONE) {
        voice->counter -= COUNTER_ONE;
        port_AdvanceSpuVoice(voice, index);
    }
    /* after the advance: a waveform's end silences the voice there */
    regs[PORT_SPU_VOICE_ENVELOPE] = (u16)voice->level;
}

/* One sample of the noise all noise voices share: a shift register,
 * clocked by six bits of the control register. */
static void port_RunSpuNoise(u32 control) {
    u32 clock = (control >> PORT_SPU_CONTROL_NOISE_SHIFT) & NOISE_CLOCK_MASK;
    s32 reload = NOISE_RELOAD >> (clock >> NOISE_SHIFT_SHIFT);
    u32 level = g_port_spu.noise_level;

    g_port_spu.noise_timer -= NOISE_STEP_BASE + (s32)(clock & NOISE_STEP_MASK);
    if (g_port_spu.noise_timer < 0) {
        u32 parity = ((level >> 15) ^ (level >> 12) ^ (level >> 11) ^
                      (level >> 10) ^ 1) &
                     1;

        g_port_spu.noise_level = (u16)((level << 1) | parity);
        g_port_spu.noise_timer += reload;
        if (g_port_spu.noise_timer < 0) {
            g_port_spu.noise_timer += reload;
        }
    }
}

/* ---- the reverb ---- */

static s32 port_ReadReverbVolume(u32 reg) {
    return (s16)g_port_spu.regs[PORT_SPU_REVERB_REGS + reg];
}

/* The place in sound memory of the reverb's `reg`, less `back` bytes:
 * that far on from where the reverb is now, in its own memory, which
 * goes round from the reverb's base to the end of sound memory. */
static u32 port_FindReverbPlace(u32 reg, s32 back) {
    u32 base = (u32)g_port_spu.regs[PORT_SPU_REVERB_BASE] * ADDRESS_UNIT;
    s32 size = (s32)(PORT_SPU_RAM_BYTES - base);
    s32 offset =
        (s32)(g_port_spu.reverb_addr - base) +
        (s32)g_port_spu.regs[PORT_SPU_REVERB_REGS + reg] * ADDRESS_UNIT - back;

    offset %= size;
    if (offset < 0) {
        offset += size;
    }
    return (base + (u32)offset) & RAM_MASK & ~1U;
}

static s32 port_ReadReverb(u32 reg, s32 back) {
    u32 place = port_FindReverbPlace(reg, back);

    return (s16)(g_port_spu.ram[place] | (g_port_spu.ram[place + 1] << 8));
}

static void port_WriteReverb(u32 reg, s32 value, u32 control) {
    u32 place = port_FindReverbPlace(reg, 0);

    if (control & PORT_SPU_CONTROL_REVERB) {
        g_port_spu.ram[place] = (u8)value;
        g_port_spu.ram[place + 1] = (u8)(value >> 8);
    }
}

/* A wall's reflection: what was heard at `from` comes back off the wall,
 * with the new sound, into `to`, smoothed by what was there. */
static void port_ReflectReverb(u32 to, u32 from, s32 in, u32 control) {
    s32 before = port_ReadReverb(to, REVERB_SAMPLE_BYTES);
    s32 value =
        in +
        ((port_ReadReverb(from, 0) * port_ReadReverbVolume(REVERB_vWALL)) >>
         VOLUME_SHIFT) -
        before;

    value = port_ClampSample(value);
    value =
        ((value * port_ReadReverbVolume(REVERB_vIIR)) >> VOLUME_SHIFT) + before;
    port_WriteReverb(to, port_ClampSample(value), control);
}

/* An all-pass filter: `value` through the delay line at `at`, `delay`
 * long, with `volume` of it fed forward and back. */
static s32 port_PassReverb(
    s32 value, u32 at, u32 delay, u32 volume_reg, u32 control) {
    s32 back =
        (s32)g_port_spu.regs[PORT_SPU_REVERB_REGS + delay] * ADDRESS_UNIT;
    s32 volume = port_ReadReverbVolume(volume_reg);
    s32 delayed = port_ReadReverb(at, back);

    value = port_ClampSample(value - ((volume * delayed) >> VOLUME_SHIFT));
    port_WriteReverb(at, value, control);
    return port_ClampSample(((value * volume) >> VOLUME_SHIFT) + delayed);
}

/* One step of the reverb, which is every other sample: `in` goes into
 * its memory, and what its memory holds from long enough ago comes out
 * into `wet`, before the reverb's own volume. */
static void port_StepSpuReverb(const s32 *in, s32 *wet, u32 control) {
    static const u8 same[2] = {REVERB_mLSAME, REVERB_mRSAME};
    static const u8 same_from[2] = {REVERB_dLSAME, REVERB_dRSAME};
    static const u8 diff[2] = {REVERB_mLDIFF, REVERB_mRDIFF};
    /* the other side's */
    static const u8 diff_from[2] = {REVERB_dRDIFF, REVERB_dLDIFF};
    static const u8 combs[2][4] = {
        {REVERB_mLCOMB1, REVERB_mLCOMB2, REVERB_mLCOMB3, REVERB_mLCOMB4},
        {REVERB_mRCOMB1, REVERB_mRCOMB2, REVERB_mRCOMB3, REVERB_mRCOMB4},
    };
    static const u8 pass_1[2] = {REVERB_mLAPF1, REVERB_mRAPF1};
    static const u8 pass_2[2] = {REVERB_mLAPF2, REVERB_mRAPF2};
    u32 base = (u32)g_port_spu.regs[PORT_SPU_REVERB_BASE] * ADDRESS_UNIT;
    u32 side;
    u32 i;

    if (g_port_spu.reverb_addr < base) {
        g_port_spu.reverb_addr = base;
    }
    for (side = 0; side < 2; side++) {
        s32 heard = (in[side] * port_ReadReverbVolume(REVERB_vLIN + side)) >>
                    VOLUME_SHIFT;
        s32 value = 0;

        port_ReflectReverb(same[side], same_from[side], heard, control);
        port_ReflectReverb(diff[side], diff_from[side], heard, control);
        for (i = 0; i < 4; i++) {
            value += port_ReadReverbVolume(REVERB_vCOMB1 + i) *
                     port_ReadReverb(combs[side][i], 0);
        }
        value = port_ClampSample(value >> VOLUME_SHIFT);
        value = port_PassReverb(
            value, pass_1[side], REVERB_dAPF1, REVERB_vAPF1, control);
        wet[side] = port_PassReverb(
            value, pass_2[side], REVERB_dAPF2, REVERB_vAPF2, control);
    }
    g_port_spu.reverb_addr += REVERB_SAMPLE_BYTES;
    if (g_port_spu.reverb_addr >= PORT_SPU_RAM_BYTES) {
        g_port_spu.reverb_addr = base;
    }
    g_port_spu.counts.reverb_samples++;
}

/* One sample of the reverb's part of the sum: `in` is what the voices
 * and the disc send it. It works at half the chip's rate (see the top
 * of the file for what that leaves out). */
static void port_RunSpuReverb(const s32 *in, s32 *out, u32 control) {
    s32 volume[2];
    u32 side;

    volume[0] = (s16)g_port_spu.regs[PORT_SPU_REVERB_VOLUME];
    volume[1] = (s16)g_port_spu.regs[PORT_SPU_REVERB_VOLUME + 1];
    if (!(control & PORT_SPU_CONTROL_REVERB) && volume[0] == 0 &&
        volume[1] == 0) {
        /* it neither hears nor is heard */
        out[0] = 0;
        out[1] = 0;
        return;
    }
    if (!g_port_spu.reverb_odd) {
        g_port_spu.reverb_odd = 1;
        g_port_spu.reverb_held[0] = in[0];
        g_port_spu.reverb_held[1] = in[1];
        for (side = 0; side < 2; side++) {
            out[side] = (((g_port_spu.reverb_before[side] +
                           g_port_spu.reverb_now[side]) /
                          2) *
                         volume[side]) >>
                        VOLUME_SHIFT;
        }
        return;
    }
    g_port_spu.reverb_odd = 0;
    g_port_spu.reverb_held[0] = (g_port_spu.reverb_held[0] + in[0]) / 2;
    g_port_spu.reverb_held[1] = (g_port_spu.reverb_held[1] + in[1]) / 2;
    for (side = 0; side < 2; side++) {
        out[side] =
            (g_port_spu.reverb_now[side] * volume[side]) >> VOLUME_SHIFT;
        g_port_spu.reverb_before[side] = g_port_spu.reverb_now[side];
    }
    port_StepSpuReverb(g_port_spu.reverb_held, g_port_spu.reverb_now, control);
}

/* ---- the chip ---- */

/* A sample into one of the chip's four buffers at the start of sound
 * memory. */
static void port_CaptureSpuSample(u32 buffer, s32 sample) {
    g_port_spu.ram[buffer + g_port_spu.capture] = (u8)sample;
    g_port_spu.ram[buffer + g_port_spu.capture + 1] = (u8)(sample >> 8);
}

void port_AttachSpu(u16 *regs) {
    u8 *bytes = (u8 *)&g_port_spu;
    u32 i;

    for (i = 0; i < sizeof(g_port_spu); i++) {
        bytes[i] = 0;
    }
    g_port_spu.regs = regs;
    g_port_spu.noise_level = 1;
}

u8 *port_GetSpuRam(void) { return g_port_spu.ram; }

const PortSpuCounts *port_GetSpuCounts(void) { return &g_port_spu.counts; }

/* The next samples (port_spu.h): each voice's, added up; the disc's;
 * the reverb's; all of it at the main volume, within 16 bits. */
void port_RunSpu(const s16 *cd, s16 *out, u32 frames) {
    u16 *regs = g_port_spu.regs;
    u32 frame;
    u32 index;
    u32 side;

    port_TakeSpuKeys();
    for (frame = 0; frame < frames; frame++) {
        u32 control = regs != NULL ? regs[PORT_SPU_CONTROL] : 0;
        s32 sum[2] = {0, 0};
        s32 heard[2] = {0, 0};
        s32 wet[2];
        u32 reverb_voices;

        g_port_spu.counts.samples++;
        if (!(control & PORT_SPU_CONTROL_ON)) {
            out[frame * 2] = 0;
            out[frame * 2 + 1] = 0;
            continue;
        }
        port_RunSpuNoise(control);
        reverb_voices = port_ReadSpuVoiceBits(PORT_SPU_REVERB_VOICES);
        for (index = 0; index < PORT_SPU_VOICES; index++) {
            s32 voice[2];

            port_RunSpuVoice(index, voice);
            sum[0] += voice[0];
            sum[1] += voice[1];
            if ((reverb_voices >> index) & 1) {
                heard[0] += voice[0];
                heard[1] += voice[1];
            }
        }
        if (cd != NULL && (control & PORT_SPU_CONTROL_CD)) {
            for (side = 0; side < 2; side++) {
                s32 sample = (cd[frame * 2 + side] *
                              (s16)regs[PORT_SPU_CD_VOLUME + side]) >>
                             VOLUME_SHIFT;

                sum[side] += sample;
                if (control & PORT_SPU_CONTROL_CD_REVERB) {
                    heard[side] += sample;
                }
            }
        }
        port_CaptureSpuSample(CAPTURE_CD_LEFT, cd != NULL ? cd[frame * 2] : 0);
        port_CaptureSpuSample(
            CAPTURE_CD_RIGHT, cd != NULL ? cd[frame * 2 + 1] : 0);
        port_CaptureSpuSample(CAPTURE_VOICE_1, g_port_spu.voices[1].out);
        port_CaptureSpuSample(CAPTURE_VOICE_3, g_port_spu.voices[3].out);
        g_port_spu.capture = (g_port_spu.capture + 2) & (CAPTURE_BYTES - 1);
        heard[0] = port_ClampSample(heard[0]);
        heard[1] = port_ClampSample(heard[1]);
        port_RunSpuReverb(heard, wet, control);
        for (side = 0; side < 2; side++) {
            s32 volume = port_StepSpuVolume(regs[PORT_SPU_MAIN_VOLUME + side],
                                            &g_port_spu.main_volume[side],
                                            &g_port_spu.main_volume_wait[side]);
            s32 sample = port_ClampSample(sum[side]) + wet[side];

            if (sum[side] > SAMPLE_MAX || sum[side] < SAMPLE_MIN ||
                sample > SAMPLE_MAX || sample < SAMPLE_MIN) {
                g_port_spu.counts.clipped++;
            }
            sample = (port_ClampSample(sample) * volume) >> VOLUME_SHIFT;
            if (!(control & PORT_SPU_CONTROL_UNMUTED)) {
                sample = 0;
            }
            out[frame * 2 + side] = (s16)port_ClampSample(sample);
        }
    }
}
