/* The port's sound chip: made-up waveforms through its decoder, pitch,
 * envelope, keys, mixer and reverb. */
/* The native port's sound chip (src/port/spu.c) on waveforms and
 * registers this test makes up: nothing of the game's. What it prints
 * is worked out a second time by tests/port/spu.py, from the rules as
 * they are written down and not from the chip's code; the cases small
 * enough to do by hand are checked here. The reverb is given the
 * settings of Sony's own modes (libspu's table, which is numbers). */
#include "common.h"
#include "src/port/spu.c"
/* what the chip hands the remastered mix, which it calls */
#include "src/port/remix.c"
#include "src/main/psxsdk/libspu/s_rmp.c"
#include "runtime/runtime.h"

static u16 g_made_regs[PORT_SPU_REG_COUNT];

#define MADE_WAVE 0x1000 /* where the made-up waveforms go */
/* An envelope that is full at once and stays: the fastest attack, which
 * is at the top at the third sample; the slowest decay, which waits
 * sixteen samples for its first step and before that finds the level
 * under a sustain level of everything; a sustain that rises, at the
 * slowest rate. */
#define MADE_HOLD_LOW 0x00FF
#define MADE_HOLD_HIGH 0x1FC0
#define MADE_HELD 0x7FFF
#define MADE_CONTROL (PORT_SPU_CONTROL_ON | PORT_SPU_CONTROL_UNMUTED)

static s16 g_made_out[2 * 4096];

static u16 *made_Voice(u32 voice) {
    return g_made_regs + voice * PORT_SPU_VOICE_REGS;
}

/* A block at `addr`: its shift and filter, its flags, and every sample
 * the four bits `nibble`. */
static void made_Block(u32 addr, u32 header, u32 flags, u32 nibble) {
    u8 *block = port_GetSpuRam() + addr;
    u32 i;

    block[0] = (u8)header;
    block[1] = (u8)flags;
    for (i = 2; i < 16; i++) {
        block[i] = (u8)(nibble | (nibble << 4));
    }
}

static void made_Reset(void) {
    u32 i;

    for (i = 0; i < PORT_SPU_REG_COUNT; i++) {
        g_made_regs[i] = 0;
    }
    port_AttachSpu(g_made_regs);
    g_made_regs[PORT_SPU_CONTROL] = MADE_CONTROL;
    g_made_regs[PORT_SPU_MAIN_VOLUME] = 0x3FFF;
    g_made_regs[PORT_SPU_MAIN_VOLUME + 1] = 0x3FFF;
}

static void made_KeyOn(u32 voice) {
    g_made_regs[PORT_SPU_KEY_ON + voice / 16] = (u16)(1U << (voice % 16));
}

static void made_Run(u32 frames) {
    while (frames > 0) {
        u32 now = frames > 4096 ? 4096 : frames;

        port_RunSpu(NULL, NULL, g_made_out, NULL, now);
        frames -= now;
    }
}

/* ---- the decoder ---- */

static void test_Decoder(void) {
    static const u8 data[14] = {0x10, 0x32, 0x54, 0x76, 0x98, 0xBA, 0xDC,
                                0xFE, 0x7F, 0x80, 0x17, 0xE9, 0x3C, 0xA5};
    u8 block[16];
    s16 samples[28];
    s32 old;
    s32 older;
    u32 header;
    u32 i;

    /* by hand: no filter, no shift: four bits at the top of sixteen */
    for (i = 0; i < 14; i++) {
        block[2 + i] = data[i];
    }
    block[0] = 0x00;
    block[1] = 0;
    old = older = 0;
    port_DecodeSpuBlock(block, &old, &older, samples);
    CHECK_EQ(samples[0], 0x0000);
    CHECK_EQ(samples[1], 0x1000);
    CHECK_EQ(samples[7], 0x7000);
    CHECK_EQ(samples[8], -0x8000);
    CHECK_EQ(samples[15], -0x1000);
    /* a shift of 12 leaves the four bits themselves; 13 is taken for 9 */
    block[0] = 0x0C;
    old = older = 0;
    port_DecodeSpuBlock(block, &old, &older, samples);
    CHECK_EQ(samples[1], 1);
    CHECK_EQ(samples[15], -1);
    block[0] = 0x0D;
    old = older = 0;
    port_DecodeSpuBlock(block, &old, &older, samples);
    CHECK_EQ(samples[1], 8);
    /* the second filter adds 60/64 of the sample before, rounded: ones
     * make 1, 2, 3, 4 */
    for (i = 2; i < 16; i++) {
        block[i] = 0x11;
    }
    block[0] = 0x1C;
    old = older = 0;
    port_DecodeSpuBlock(block, &old, &older, samples);
    CHECK_EQ(samples[0], 1);
    CHECK_EQ(samples[1], 2);
    CHECK_EQ(samples[2], 3);
    CHECK_EQ(samples[3], 4);
    CHECK_EQ(old, samples[27]);
    CHECK_EQ(older, samples[26]);
    /* a sample past 16 bits is held at the limit: sevens at the top,
     * each with all of the one before */
    for (i = 2; i < 16; i++) {
        block[i] = 0x77;
    }
    block[0] = 0x10;
    old = older = 0;
    port_DecodeSpuBlock(block, &old, &older, samples);
    CHECK_EQ(samples[0], 0x7000);
    CHECK_EQ(samples[1], 0x7FFF);
    /* every filter and several shifts, for the Python side: two blocks
     * in a row, the second starting from the first's last samples */
    for (i = 0; i < 14; i++) {
        block[2 + i] = data[i];
    }
    for (header = 0; header < 0x80; header += 0x0B) {
        u32 pass;

        old = older = 0;
        for (pass = 0; pass < 2; pass++) {
            block[0] = (u8)(header + pass);
            port_DecodeSpuBlock(block, &old, &older, samples);
            test_Print("adpcm %d", block[0]);
            for (i = 0; i < 28; i++) {
                test_Print(" %d", samples[i]);
            }
            test_Print("\n");
        }
    }
    test_Print("adpcm_data");
    for (i = 0; i < 14; i++) {
        test_Print(" %d", data[i]);
    }
    test_Print("\n");
}

/* ---- the interpolation ---- */

static void test_Interpolation(void) {
    static const s16 cases[5][4] = {
        {0, 0, 0, 0x7FFF},
        {0x7FFF, 0x7FFF, 0x7FFF, 0x7FFF},
        {-0x8000, -0x8000, -0x8000, -0x8000},
        {1000, -2000, 30000, -32768},
        {-123, 4567, -8901, 23456},
    };
    u32 i;
    u32 index;

    test_Print("gauss");
    for (i = 0; i < 512; i++) {
        test_Print(" %d", g_port_spu_gauss[i]);
    }
    test_Print("\n");
    /* by hand: the newest sample alone, at the start of its way in: its
     * weight is the table's first, -1 in 32768 */
    CHECK_EQ(port_InterpolateSpu(cases[0], 0), -1);
    /* the same sample four times is itself, less the table's shortfall
     * from one and what four roundings lose */
    CHECK(port_InterpolateSpu(cases[1], 0) >= 0x7FFF - 0x90);
    CHECK(port_InterpolateSpu(cases[1], 0) <= 0x7FFF);
    for (i = 0; i < 5; i++) {
        for (index = 0; index < 256; index += 51) {
            test_Print("interp %d %d %d\n", i, index,
                       port_InterpolateSpu(cases[i], index));
        }
    }
}

/* ---- the envelope ---- */

/* A level from `start`, stepped `samples` times at `rate`: where it is
 * every `every` samples. */
static void test_PrintLevel(
    s32 start, u32 rate, s32 down, s32 exponential, u32 samples, u32 every) {
    s32 level = start;
    u32 wait = 0;
    u32 i;

    test_Print("level %d %d %d %d %d %d", start, rate, down != 0,
               exponential != 0, samples, every);
    for (i = 1; i <= samples; i++) {
        port_StepSpuLevel(&level, &wait, rate, down, exponential);
        if (i % every == 0) {
            test_Print(" %d", level);
        }
    }
    test_Print("\n");
}

static void test_Envelope(void) {
    s32 level = 0;
    u32 wait = 0;
    u32 i;
    u16 *voice;

    /* by hand. The fastest rise: 7 at a shift of 11 a sample: 14336,
     * 28672, and held at the top */
    port_StepSpuLevel(&level, &wait, 0, 0, 0);
    CHECK_EQ(level, 14336);
    port_StepSpuLevel(&level, &wait, 0, 0, 0);
    CHECK_EQ(level, 28672);
    port_StepSpuLevel(&level, &wait, 0, 0, 0);
    CHECK_EQ(level, 0x7FFF);
    /* a shift of 12: a step of 7 every second sample; 4681 sevens are
     * 32767, so the top is reached at the 9362nd sample */
    level = 0;
    wait = 0;
    for (i = 0; i < 9360; i++) {
        port_StepSpuLevel(&level, &wait, 12 << 2, 0, 0);
    }
    CHECK_EQ(level, 32760);
    port_StepSpuLevel(&level, &wait, 12 << 2, 0, 0);
    CHECK_EQ(level, 32760);
    port_StepSpuLevel(&level, &wait, 12 << 2, 0, 0);
    CHECK_EQ(level, 32767);
    /* the step's two bits: 7, 6, 5, 4 going up, at a shift of 11 */
    for (i = 0; i < 4; i++) {
        level = 0;
        wait = 0;
        port_StepSpuLevel(&level, &wait, (11 << 2) | i, 0, 0);
        CHECK_EQ(level, 7 - i);
    }
    /* and -8, -7, -6, -5 going down, held at nothing */
    for (i = 0; i < 4; i++) {
        level = 100;
        wait = 0;
        port_StepSpuLevel(&level, &wait, (11 << 2) | i, 1, 0);
        CHECK_EQ(level, 100 - 8 + i);
    }
    level = 5;
    wait = 0;
    port_StepSpuLevel(&level, &wait, 11 << 2, 1, 0);
    CHECK_EQ(level, 0);
    /* an exponential fall takes the level's share of the step: from
     * half, a step of -8 at a shift of 3 (256 times 8) is -1024 */
    level = 0x4000;
    wait = 0;
    port_StepSpuLevel(&level, &wait, 3 << 2, 1, 1);
    CHECK_EQ(level, 0x4000 - 1024);
    /* an exponential rise above 0x6000 waits four times as long */
    level = 0x6001;
    wait = 0;
    for (i = 0; i < 3; i++) {
        port_StepSpuLevel(&level, &wait, 11 << 2, 0, 1);
    }
    CHECK_EQ(level, 0x6001);
    port_StepSpuLevel(&level, &wait, 11 << 2, 0, 1);
    CHECK_EQ(level, 0x6001 + 7);

    /* for the Python side: every kind, at several rates */
    test_PrintLevel(0, 0x00, 0, 0, 8, 1);
    test_PrintLevel(0, 0x2B, 0, 0, 4000, 250);
    test_PrintLevel(0, 0x33, 0, 0, 40000, 2500);
    test_PrintLevel(0, 0x1D, 0, 1, 2000, 100);
    test_PrintLevel(0, 0x2E, 0, 1, 40000, 2000);
    test_PrintLevel(0x7FFF, 0x10, 1, 1, 3000, 150);
    test_PrintLevel(0x7FFF, 0x34, 1, 1, 60000, 3000);
    test_PrintLevel(0x7FFF, 0x29, 1, 0, 6000, 300);
    test_PrintLevel(0x7FFF, 0x3F, 1, 0, 80000, 4000);
    test_PrintLevel(0x5000, 0x7F, 0, 0, 5000, 1000);

    /* a whole envelope on a voice: attack, decay to the sustain level,
     * the sustain's fall, and the release from the key off */
    made_Reset();
    made_Block(MADE_WAVE, 0x00, FLAG_END | FLAG_REPEAT | FLAG_LOOP_START, 0x7);
    voice = made_Voice(3);
    voice[PORT_SPU_VOICE_PITCH] = 0x1000;
    voice[PORT_SPU_VOICE_START] = MADE_WAVE / 8;
    voice[PORT_SPU_VOICE_ADSR_LOW] = 0x2C6A;  /* attack 0x2C, decay 6, to 11 */
    voice[PORT_SPU_VOICE_ADSR_HIGH] = 0xD1CA; /* sustain exp down 0x47 */
    made_KeyOn(3);
    test_Print("adsr %d %d", voice[PORT_SPU_VOICE_ADSR_LOW],
               voice[PORT_SPU_VOICE_ADSR_HIGH]);
    for (i = 0; i < 60; i++) {
        if (i == 40) {
            g_made_regs[PORT_SPU_KEY_OFF] = 1 << 3;
        }
        made_Run(200);
        test_Print(" %d", voice[PORT_SPU_VOICE_ENVELOPE]);
    }
    test_Print("\n");
    CHECK_EQ(voice[PORT_SPU_VOICE_ENVELOPE], 0);
    CHECK_EQ(g_port_spu.voices[3].phase, PHASE_OFF);
}

/* ---- keys, a waveform's flags, the ended bits ---- */

static void test_Keys(void) {
    u16 *voice;
    const PortSpuCounts *counts = port_GetSpuCounts();

    made_Reset();
    /* three blocks: the loop starts at the second; the third is the
     * end and goes back. Then one that ends and does not go back. */
    made_Block(MADE_WAVE, 0x00, 0, 0x1);
    made_Block(MADE_WAVE + 16, 0x00, FLAG_LOOP_START, 0x2);
    made_Block(MADE_WAVE + 32, 0x00, FLAG_END | FLAG_REPEAT, 0x3);
    made_Block(MADE_WAVE + 64, 0x00, 0, 0x4);
    made_Block(MADE_WAVE + 80, 0x00, FLAG_END, 0x5);
    voice = made_Voice(0);
    voice[PORT_SPU_VOICE_PITCH] = 0x1000;
    voice[PORT_SPU_VOICE_START] = MADE_WAVE / 8;
    voice[PORT_SPU_VOICE_ADSR_LOW] = MADE_HOLD_LOW;
    voice[PORT_SPU_VOICE_ADSR_HIGH] = MADE_HOLD_HIGH;
    voice[PORT_SPU_VOICE_VOLUME_LEFT] = 0x3FFF;

    /* nothing sounds before the key */
    made_Run(100);
    CHECK_EQ(voice[PORT_SPU_VOICE_ENVELOPE], 0);
    CHECK_EQ(g_made_out[0], 0);
    made_KeyOn(0);
    port_TakeSpuKeys();
    /* the key is taken, and the register cleared; the loop starts at
     * the waveform's start until a block says otherwise */
    CHECK_EQ(g_made_regs[PORT_SPU_KEY_ON], 0);
    CHECK_EQ(counts->key_ons, 1);
    CHECK_EQ(voice[PORT_SPU_VOICE_LOOP], MADE_WAVE / 8);
    /* at 0x1000 a sample of the waveform is a sample out: 28 a block.
     * The key played the first; 27 more are the first block's rest. */
    made_Run(27);
    CHECK_EQ(voice[PORT_SPU_VOICE_LOOP], MADE_WAVE / 8);
    CHECK_EQ(voice[PORT_SPU_VOICE_ENVELOPE], MADE_HELD);
    made_Run(1);
    CHECK_EQ(voice[PORT_SPU_VOICE_LOOP], (MADE_WAVE + 16) / 8);
    /* the end is told when the last block has been played, not before */
    made_Run(55);
    CHECK_EQ(g_made_regs[PORT_SPU_ENDED], 0);
    made_Run(1);
    CHECK_EQ(g_made_regs[PORT_SPU_ENDED], 1);
    CHECK_EQ(g_port_spu.voices[0].addr, MADE_WAVE + 32);
    /* it repeats: still sounding, a loop later */
    made_Run(500);
    CHECK_EQ(voice[PORT_SPU_VOICE_ENVELOPE], MADE_HELD);
    CHECK(g_made_out[2 * 499] != 0);
    CHECK_EQ(counts->ends, 0);
    /* the key off: the release, to nothing, and silence */
    voice[PORT_SPU_VOICE_ADSR_HIGH] = MADE_HOLD_HIGH | 0x0C;
    g_made_regs[PORT_SPU_KEY_OFF] = 1;
    made_Run(1);
    CHECK_EQ(g_port_spu.voices[0].phase, PHASE_RELEASE);
    CHECK_EQ(g_made_regs[PORT_SPU_KEY_OFF], 0);
    /* -8 every second sample: 4095 steps at most */
    made_Run(2 * 4096);
    CHECK_EQ(voice[PORT_SPU_VOICE_ENVELOPE], 0);
    CHECK_EQ(g_port_spu.voices[0].phase, PHASE_OFF);
    CHECK_EQ(g_made_out[2 * 4095], 0);
    /* a key off for a voice that is off already changes nothing */
    g_made_regs[PORT_SPU_KEY_OFF] = 1;
    made_Run(1);
    CHECK_EQ(g_port_spu.voices[0].phase, PHASE_OFF);

    /* a key on clears the voice's ended bit; a waveform that ends with
     * no repeat falls silent at its end and says so */
    voice[PORT_SPU_VOICE_START] = (MADE_WAVE + 64) / 8;
    made_KeyOn(0);
    made_Run(1);
    CHECK_EQ(g_made_regs[PORT_SPU_ENDED], 0);
    made_Run(54);
    CHECK_EQ(voice[PORT_SPU_VOICE_ENVELOPE], MADE_HELD);
    CHECK_EQ(g_made_regs[PORT_SPU_ENDED], 0);
    made_Run(1);
    CHECK_EQ(voice[PORT_SPU_VOICE_ENVELOPE], 0);
    CHECK_EQ(g_made_regs[PORT_SPU_ENDED], 1);
    CHECK_EQ(counts->ends, 1);
    /* off and on in one go: the voice starts */
    g_made_regs[PORT_SPU_KEY_OFF] = 1;
    made_KeyOn(0);
    made_Run(10);
    CHECK_EQ(voice[PORT_SPU_VOICE_ENVELOPE], MADE_HELD);
    /* voices past the sixteenth are in the second halfword */
    made_Voice(20)[PORT_SPU_VOICE_START] = MADE_WAVE / 8;
    made_Voice(20)[PORT_SPU_VOICE_PITCH] = 0x1000;
    made_Voice(20)[PORT_SPU_VOICE_ADSR_LOW] = MADE_HOLD_LOW;
    made_KeyOn(20);
    made_Run(120);
    CHECK_EQ(g_made_regs[PORT_SPU_ENDED + 1], 1 << 4);
    CHECK(made_Voice(20)[PORT_SPU_VOICE_ENVELOPE] != 0);
}

/* ---- pitch ---- */

static void test_Pitch(void) {
    u16 *voice;

    made_Reset();
    made_Block(MADE_WAVE, 0x00, FLAG_END | FLAG_REPEAT | FLAG_LOOP_START, 0x1);
    voice = made_Voice(0);
    voice[PORT_SPU_VOICE_START] = MADE_WAVE / 8;
    voice[PORT_SPU_VOICE_ADSR_LOW] = MADE_HOLD_LOW;
    /* half speed: 56 samples out a block, so the end is told at the
     * 56th; the key itself played the block's first */
    voice[PORT_SPU_VOICE_PITCH] = 0x0800;
    made_KeyOn(0);
    made_Run(54);
    CHECK_EQ(g_made_regs[PORT_SPU_ENDED], 0);
    made_Run(2);
    CHECK_EQ(g_made_regs[PORT_SPU_ENDED], 1);
    /* a pitch above 0x3FFF is 0x4000: four samples a sample, seven
     * samples out a block */
    voice[PORT_SPU_VOICE_PITCH] = 0xFFFF;
    made_KeyOn(0);
    made_Run(6);
    CHECK_EQ(g_made_regs[PORT_SPU_ENDED], 0);
    made_Run(1);
    CHECK_EQ(g_made_regs[PORT_SPU_ENDED], 1);
}

/* ---- the mixer ---- */

static void test_Mixer(void) {
    u32 i;

    /* six voices at the top of the scale and full volume are more than
     * 16 bits: the sum is held at 32767, and the main volume, which is
     * 32766 parts in 32768, leaves 32767 * 32766 / 32768: 32765 */
    made_Reset();
    made_Block(MADE_WAVE, 0x00, FLAG_END | FLAG_REPEAT | FLAG_LOOP_START, 0x7);
    made_Block(
        MADE_WAVE + 16, 0x00, FLAG_END | FLAG_REPEAT | FLAG_LOOP_START, 0x8);
    for (i = 0; i < 6; i++) {
        u16 *voice = made_Voice(i);

        voice[PORT_SPU_VOICE_PITCH] = 0x1000;
        voice[PORT_SPU_VOICE_START] = MADE_WAVE / 8;
        voice[PORT_SPU_VOICE_ADSR_LOW] = MADE_HOLD_LOW;
        voice[PORT_SPU_VOICE_ADSR_HIGH] = MADE_HOLD_HIGH;
        voice[PORT_SPU_VOICE_VOLUME_LEFT] = 0x3FFF;
        voice[PORT_SPU_VOICE_VOLUME_RIGHT] = 0x0400;
    }
    g_made_regs[PORT_SPU_KEY_ON] = 0x3F;
    made_Run(64);
    CHECK_EQ(g_made_out[2 * 63], 32765);
    /* the right side's volume is a sixteenth, and one voice is 0x7000
     * less the interpolation's shortfall from one (128 in 32768): six
     * are under the limit, within a hundredth of six sixteenths of
     * 0x7000 */
    CHECK(g_made_out[2 * 63 + 1] > 6 * 0x7000 / 16 - 108);
    CHECK(g_made_out[2 * 63 + 1] <= 6 * 0x7000 / 16);
    CHECK(port_GetSpuCounts()->clipped > 0);
    /* and the other way: -32768 held, times 32766 / 32768: -32766 */
    for (i = 0; i < 6; i++) {
        made_Voice(i)[PORT_SPU_VOICE_START] = (MADE_WAVE + 16) / 8;
    }
    g_made_regs[PORT_SPU_KEY_ON] = 0x3F;
    made_Run(64);
    CHECK_EQ(g_made_out[2 * 63], -32766);
    /* the chip keeps what voices 1 and 3 play, under their envelopes
     * and before their volumes, after the disc's two buffers: here both
     * are at the bottom of the scale, and voice 2 has no such buffer */
    {
        const u8 *ram = port_GetSpuRam();
        u32 at = (g_port_spu.capture + CAPTURE_BYTES - 2) % CAPTURE_BYTES;

        CHECK((s16)(ram[0x800 + at] | (ram[0x801 + at] << 8)) < -0x7F00);
        CHECK((s16)(ram[0xC00 + at] | (ram[0xC01 + at] << 8)) < -0x7F00);
        CHECK_EQ(g_port_spu.capture, 2 * 128);
    }
    /* a volume with the top bit of its fifteen set is below nothing,
     * and turns the sound over: 0x4000 is all of it, the other way */
    made_Voice(0)[PORT_SPU_VOICE_VOLUME_LEFT] = 0x4000;
    for (i = 1; i < 6; i++) {
        made_Voice(i)[PORT_SPU_VOICE_VOLUME_LEFT] = 0;
    }
    made_Run(64);
    CHECK(g_made_out[2 * 63] > 0x7FFF - 400);
    /* muted, the chip is silent; off, too */
    g_made_regs[PORT_SPU_CONTROL] = PORT_SPU_CONTROL_ON;
    made_Run(8);
    CHECK_EQ(g_made_out[2 * 7 + 1], 0);
    g_made_regs[PORT_SPU_CONTROL] = 0;
    made_Run(8);
    CHECK_EQ(g_made_out[2 * 7 + 1], 0);

    /* the disc's sound: only with its bit, at its own volume */
    made_Reset();
    {
        static s16 cd[2 * 16];

        for (i = 0; i < 16; i++) {
            cd[2 * i] = 20000;
            cd[2 * i + 1] = -20000;
        }
        g_made_regs[PORT_SPU_CD_VOLUME] = 0x4000;
        g_made_regs[PORT_SPU_CD_VOLUME + 1] = 0x7FFF;
        port_RunSpu(cd, NULL, g_made_out, NULL, 16);
        CHECK_EQ(g_made_out[2 * 15], 0);
        g_made_regs[PORT_SPU_CONTROL] |= PORT_SPU_CONTROL_CD;
        port_RunSpu(cd, NULL, g_made_out, NULL, 16);
        /* 20000 at half is 10000; at the main volume, 9999 */
        CHECK_EQ(g_made_out[2 * 15], 9999);
        /* -20000 at 32767/32768 is -20000 (rounded down); at the main
         * volume -19998.8, rounded down */
        CHECK_EQ(g_made_out[2 * 15 + 1], -19999);
        /* the chip keeps the disc's last samples as they came in at the
         * start of sound memory, left and then right, 512 of each, a
         * sample at every sample it makes, heard or not: 32 so far */
        CHECK_EQ(
            (s16)(port_GetSpuRam()[0] | (port_GetSpuRam()[1] << 8)), 20000);
        CHECK_EQ(
            (s16)(port_GetSpuRam()[62] | (port_GetSpuRam()[63] << 8)), 20000);
        CHECK_EQ(port_GetSpuRam()[64] | port_GetSpuRam()[65], 0);
        CHECK_EQ(
            (s16)(port_GetSpuRam()[0x400] | (port_GetSpuRam()[0x401] << 8)),
            -20000);
    }
}

/* ---- the reverb ---- */

/* One of Sony's modes, by its settings: a click through the disc's
 * input, and how loud what comes back is, half second by half
 * second. */
static void test_ReverbMode(u32 mode) {
    static s16 cd[2 * 4096];
    const u16 *settings = &_spu_rev_param[mode].dAPF1;
    u32 size = (0x10000 - (u32)_spu_rev_startaddr[mode]) * 8;
    u32 first = 0;
    u32 frame = 0;
    u32 half;
    u32 i;

    made_Reset();
    for (i = 0; i < 32; i++) {
        g_made_regs[PORT_SPU_REVERB_REGS + i] = settings[i];
        /* every place the mode names is inside its own memory */
        if (i >= REVERB_mLSAME && i < REVERB_vLIN) {
            CHECK((u32)settings[i] * 8 < size);
        }
    }
    CHECK((u32)settings[0] * 8 < size);
    CHECK((u32)settings[1] * 8 < size);
    /* the size Sony's table gives the mode is inside that memory too */
    CHECK(_spu_rev_startaddr[mode] + _spu_rev_workareasize[mode] <= 0x10000);
    CHECK(_spu_rev_workareasize[mode] > 0);
    g_made_regs[PORT_SPU_REVERB_BASE] = (u16)_spu_rev_startaddr[mode];
    g_made_regs[PORT_SPU_REVERB_VOLUME] = 0x7FFF;
    g_made_regs[PORT_SPU_REVERB_VOLUME + 1] = 0x7FFF;
    g_made_regs[PORT_SPU_CD_VOLUME] = 0x7FFF;
    g_made_regs[PORT_SPU_CD_VOLUME + 1] = 0x7FFF;
    g_made_regs[PORT_SPU_CONTROL] =
        MADE_CONTROL | PORT_SPU_CONTROL_REVERB | PORT_SPU_CONTROL_CD |
        PORT_SPU_CONTROL_CD_REVERB;
    test_Print("reverb %d", mode);
    for (half = 0; half < 12; half++) {
        s32 peak = 0;
        u32 left = PORT_SPU_RATE / 2;

        while (left > 0) {
            u32 now = left > 4096 ? 4096 : left;

            for (i = 0; i < now; i++) {
                /* the click: four samples, left only */
                cd[2 * i] = (frame + i < 4) ? 0x6000 : 0;
                cd[2 * i + 1] = 0;
            }
            port_RunSpu(cd, NULL, g_made_out, NULL, now);
            for (i = 0; i < now; i++) {
                s32 l = g_made_out[2 * i];
                s32 r = g_made_out[2 * i + 1];

                if (frame + i < 8) {
                    continue; /* the click itself */
                }
                l = l < 0 ? -l : l;
                r = r < 0 ? -r : r;
                if (l > peak) {
                    peak = l;
                }
                if (r > peak) {
                    peak = r;
                }
                if (first == 0 && (l | r) != 0) {
                    first = frame + i;
                }
            }
            frame += now;
            left -= now;
        }
        test_Print(" %d", peak);
    }
    test_Print(" first %d\n", first);
    /* nothing it was given was ever more than 16 bits hold */
    CHECK_EQ(port_GetSpuCounts()->clipped, 0);
}

static void test_Reverb(void) {
    u32 mode;
    u32 i;
    s32 any = 0;

    for (mode = 1; mode < 10; mode++) {
        test_ReverbMode(mode);
    }
    /* with its bit off the reverb writes nothing: its memory stays
     * empty, and nothing comes back */
    made_Reset();
    for (i = 0; i < 32; i++) {
        g_made_regs[PORT_SPU_REVERB_REGS + i] = (&_spu_rev_param[2].dAPF1)[i];
    }
    g_made_regs[PORT_SPU_REVERB_BASE] = (u16)_spu_rev_startaddr[2];
    g_made_regs[PORT_SPU_REVERB_VOLUME] = 0x7FFF;
    g_made_regs[PORT_SPU_CD_VOLUME] = 0x7FFF;
    g_made_regs[PORT_SPU_CONTROL] =
        MADE_CONTROL | PORT_SPU_CONTROL_CD | PORT_SPU_CONTROL_CD_REVERB;
    {
        static s16 cd[2 * 4096];

        cd[0] = 0x6000;
        port_RunSpu(cd, NULL, g_made_out, NULL, 4096);
        cd[0] = 0;
        port_RunSpu(cd, NULL, g_made_out, NULL, 4096);
    }
    for (i = (u32)_spu_rev_startaddr[2] * 8; i < PORT_SPU_RAM_BYTES; i++) {
        any |= port_GetSpuRam()[i];
    }
    CHECK_EQ(any, 0);
    CHECK_EQ(g_made_out[2 * 4000], 0);
}

void test_Main(void) {
    test_Decoder();
    test_Interpolation();
    test_Envelope();
    test_Keys();
    test_Pitch();
    test_Mixer();
    test_Reverb();
}
