/* The sound remastered (src/port/remix.c) on signals this test makes up:
 * the resampler on tones across the band at the game's pitches and
 * faster, beside the chip's own table; the reverb's rooms on a click and
 * on noise; the head on a click from either side and the middle; and the
 * chip run with the remastered mix beside it, which must leave the chip
 * as it was to the bit. What it prints is measured by tests/port/remix.py
 * (powers, decays, delays), which holds it to what each part is for.
 * With the argument `bench` it times the mixer instead and checks
 * nothing. */
#include "common.h"
#include "src/port/spu.c"
#include "src/port/remix.c"
#include "src/main/psxsdk/libspu/s_rmp.c"
#include "runtime/runtime.h"

#define TONE_N 4096
#define TONE_WARM 256
#define TONE_AMPLITUDE 16000.0

/* A sine's sample, `phase` half turns of 2 to the 30th. */
static s32 made_Sine(s64 phase, s32 amplitude) {
    s64 value = port_RemixSine(phase);

    return (
        s32)((value * amplitude + (value >= 0 ? ONE_Q30 / 2 : -ONE_Q30 / 2)) >>
             Q30);
}

static double made_Cos(s64 phase) {
    return (double)port_RemixSine(phase + ONE_Q30 / 2) / (double)ONE_Q30;
}

static double made_Sin(s64 phase) {
    return (double)port_RemixSine(phase) / (double)ONE_Q30;
}

/* A power as a whole number of parts in a thousand million, within what
 * a word holds. */
static s32 made_Parts(double ratio) {
    double parts = ratio * 1e9;

    if (parts > 2e9) {
        return 2000000000;
    }
    return (s32)(parts + 0.5);
}

/* A voice of the resampler, or the chip's four samples, played at `step`
 * on a sine that goes `half_turns` (of 2 to the 30th) a sample of its
 * own; TONE_N samples of it after a warming into `out`. */
static void made_Play(u32 step, s64 half_turns, s32 chip, s32 *out) {
    PortRemixVoice *voice = &g_remix.voices[0];
    s16 recent[4] = {0, 0, 0, 0};
    s64 phase = 0;
    u32 counter = 0;
    u32 n;
    u32 i;

    for (i = 0; i < sizeof(*voice); i++) {
        ((u8 *)voice)[i] = 0;
    }
    for (n = 0; n < TONE_WARM + TONE_N; n++) {
        counter += step;
        while (counter >= COUNTER_ONE) {
            s32 sample = made_Sine(phase, (s32)TONE_AMPLITUDE);

            counter -= COUNTER_ONE;
            port_RemixPush(0, sample);
            recent[0] = recent[1];
            recent[1] = recent[2];
            recent[2] = recent[3];
            recent[3] = (s16)sample;
            phase += half_turns;
        }
        if (n >= TONE_WARM) {
            out[n - TONE_WARM] =
                chip ? port_InterpolateSpu(
                           recent, (counter >> COUNTER_INDEX_SHIFT) &
                                       COUNTER_INDEX_MASK)
                     : port_RemixResample(voice, counter, step);
        }
    }
}

/* A tone of `k` cycles in TONE_N samples heard at `step`: what is left of
 * it, and what else is heard (images, what folds back, the kernel's
 * error), both as powers over the tone's own. */
static void test_Tone(u32 step, u32 k, s32 chip) {
    static s32 out[TONE_N];
    /* the waveform's own frequency, in half turns a sample: the tone's at
     * the voice's rate, slowed by the step */
    s64 half_turns = port_RemixDivide((s64)k << 31, step);
    double along = 0;
    double across = 0;
    double total = 0;
    double tone;
    u32 n;

    made_Play(step, half_turns, chip, out);
    for (n = 0; n < TONE_N; n++) {
        s64 phase = (s64)((k * n) % TONE_N) << 19;

        along += out[n] * made_Cos(phase);
        across += out[n] * made_Sin(phase);
        total += (double)out[n] * out[n];
    }
    tone = 2 * (along * along + across * across) / ((double)TONE_N * TONE_N);
    total /= TONE_N;
    test_Print(
        "tone %x %u %s %d %d\n", step, k, chip ? "chip" : "sinc",
        made_Parts(tone / (TONE_AMPLITUDE * TONE_AMPLITUDE / 2)),
        made_Parts((total - tone) / (TONE_AMPLITUDE * TONE_AMPLITUDE / 2)));
}

/* A tone of `thousandths` of the waveform's rate played faster than
 * recorded, so fast that it is above what the output's rate holds: what
 * comes out of it, all of it folded back, as a power over the tone's. */
static void test_Fold(u32 step, u32 thousandths, s32 chip) {
    static s32 out[TONE_N];
    s64 half_turns = port_RemixDivide((s64)thousandths * 2 * ONE_Q30, 1000);
    double total = 0;
    u32 n;

    made_Play(step, half_turns, chip, out);
    for (n = 0; n < TONE_N; n++) {
        total += (double)out[n] * out[n];
    }
    total /= TONE_N;
    test_Print("fold %x %u %s %d\n", step, thousandths, chip ? "chip" : "sinc",
               made_Parts(total / (TONE_AMPLITUDE * TONE_AMPLITUDE / 2)));
}

static void test_Kernels(void) {
    u32 step;
    u32 phase;
    u32 bad = 0;

    port_RemixPrepare();
    /* every row of every band adds up to one, and its middle is its
     * largest near the place it is for */
    for (step = 0x100; step <= 0x4000; step += 0x100) {
        for (phase = 0; phase <= PORT_REMIX_PHASES; phase++) {
            u32 taps;
            const s16 *row = port_RemixKernel(step, phase, &taps);
            s32 sum = 0;
            u32 k;

            for (k = 0; k < taps; k++) {
                sum += row[k];
            }
            bad += sum != PORT_REMIX_COEF_ONE;
        }
    }
    CHECK_EQ(bad, 0);
    {
        u32 taps;
        const s16 *row = port_RemixKernel(0x1000, 0, &taps);
        u32 k;

        /* at a sample's own place, the sinc at the waveform's rate is that
         * sample alone */
        CHECK_EQ(taps, 32);
        CHECK_EQ(row[15], PORT_REMIX_COEF_ONE);
        for (k = 0; k < taps; k++) {
            if (k != 15) {
                CHECK_EQ(row[k], 0);
            }
        }
    }
}

static void test_Resampler(void) {
    static const u16 steps[] = {
        0x0659, 0x0800, 0x1000, 0x1555, 0x2000, 0x2AAA, 0x4000};
    static const u16 tones[] = {41,   123,  205,  410,  614,  819,  1024,
                                1229, 1434, 1638, 1720, 1802, 1884, 1966};
    u32 s;
    u32 t;

    for (s = 0; s < sizeof(steps) / sizeof(steps[0]); s++) {
        for (t = 0; t < sizeof(tones) / sizeof(tones[0]); t++) {
            /* a tone of the output's that the waveform can hold */
            if ((u32)tones[t] * COUNTER_ONE / steps[s] < TONE_N / 2 ||
                steps[s] >= COUNTER_ONE) {
                test_Tone(steps[s], tones[t], 0);
                test_Tone(steps[s], tones[t], 1);
            }
        }
    }
    /* the waveform's tones that a voice played faster puts above what
     * the output holds */
    for (s = 3; s < sizeof(steps) / sizeof(steps[0]); s++) {
        u32 lowest = 500U * COUNTER_ONE / steps[s] * 115 / 100;
        u32 f;

        for (f = lowest; f <= 490; f += (490 - lowest) / 4 + 1) {
            test_Fold(steps[s], f, 0);
            test_Fold(steps[s], f, 1);
        }
    }
}

/* ---- the reverb ---- */

#define BLOCK 441 /* 10 ms */

static void test_Room(u32 room) {
    u32 length = port_RemixRoomDecay(room) * 6 * 441 / 10 + 44100;
    u32 blocks = 0;
    u32 last_heard = 0;
    double energy = 0;
    double whole = 0;
    /* the low part of it: four one-pole low passes at about 200 Hz, one
     * after the other */
    double low[2][4] = {{0, 0, 0, 0}, {0, 0, 0, 0}};
    double low_energy = 0;
    static s32 lows[2000];
    s32 peak = 0;
    u32 n;
    u32 i;

    port_RemixReverbReset(room);
    test_Print("room %u %s decay %u wet %u", room, port_RemixRoomName(room),
               port_RemixRoomDecay(room), port_RemixRoomWet(room));
    for (i = 0; i < LINES; i++) {
        test_Print(
            " line %u %d", g_remix.reverb.length[i], g_remix.reverb.gain[i]);
    }
    test_Print("\n");
    test_Print("energy %u", room);
    for (n = 0; n < length; n++) {
        s32 out[2];
        s32 in = n == 0 ? 16384 : 0;

        port_RemixReverbRun(in, in, out);
        /* in samples: the reverb gives 256ths */
        energy += ((double)out[0] * out[0] + (double)out[1] * out[1]) / 65536.0;
        for (i = 0; i < 2; i++) {
            u32 pole;
            double x = out[i] / 256.0;

            for (pole = 0; pole < 4; pole++) {
                low[i][pole] += (x - low[i][pole]) * 0.028;
                x = low[i][pole];
            }
            low_energy += x * x;
        }
        if (out[0] != 0 || out[1] != 0) {
            last_heard = n;
        }
        if ((out[0] < 0 ? -out[0] : out[0]) / 256 > peak) {
            peak = (out[0] < 0 ? -out[0] : out[0]) / 256;
        }
        if ((n + 1) % BLOCK == 0) {
            whole += energy;
            /* in 64 of a sample's square */
            test_Print(
                " %d", energy / 64 > 2e9 ? 2000000000 : (s32)(energy / 64));
            energy = 0;
            if (blocks < 2000) {
                lows[blocks] = low_energy > 2e9 ? 2000000000 : (s32)low_energy;
            }
            low_energy = 0;
            blocks++;
        }
    }
    test_Print("\n");
    test_Print("low %u", room);
    for (n = 0; n < blocks && n < 2000; n++) {
        test_Print(" %d", lows[n]);
    }
    test_Print("\n");
    /* the direct sound's energy is two samples of 16384: the reverb's
     * over it, before the wet level */
    test_Print("tail %u peak %d silent_after %u length %u whole %d\n", room,
               peak, last_heard + 1, length,
               made_Parts(whole / (2.0 * 16384 * 16384) / 1000));
}

/* Noise at full scale for ten seconds, then silence: nothing grows past
 * what the lines can hold, and all of it dies to nothing. */
static void test_RoomNoise(u32 room) {
    u32 seed = 12345;
    s32 peak = 0;
    s32 line_peak = 0;
    u32 silent = 0;
    u32 n;

    port_RemixReverbReset(room);
    for (n = 0; n < 441000 + 1764000; n++) {
        s32 out[2];
        s32 left = 0;
        s32 right = 0;
        u32 i;

        if (n < 441000) {
            seed = seed * 1103515245U + 12345U;
            left = (s32)(seed >> 16) - 32768;
            seed = seed * 1103515245U + 12345U;
            right = (s32)(seed >> 16) - 32768;
        }
        port_RemixReverbRun(left, right, out);
        out[0] /= 256;
        out[1] /= 256;
        if ((out[0] < 0 ? -out[0] : out[0]) > peak) {
            peak = out[0] < 0 ? -out[0] : out[0];
        }
        if ((out[1] < 0 ? -out[1] : out[1]) > peak) {
            peak = out[1] < 0 ? -out[1] : out[1];
        }
        for (i = 0; i < LINES; i++) {
            s32 v =
                g_remix.reverb.lines[i][(g_remix.reverb.at - 1) & LINE_MASK];

            if ((v < 0 ? -v : v) > line_peak) {
                line_peak = v < 0 ? -v : v;
            }
        }
        if (out[0] != 0 || out[1] != 0) {
            silent = n + 1;
        }
    }
    test_Print("noise %u peak %d line_peak %d silent_after %d\n", room, peak,
               line_peak, (s32)silent - 441000);
}

static void test_Reverb(void) {
    u32 room;

    for (room = 1; room < PORT_REMIX_ROOMS; room++) {
        test_Room(room);
    }
    test_RoomNoise(PORT_REMIX_ROOM_CAVE);
    test_RoomNoise(PORT_REMIX_ROOM_SHIP);
    test_RoomNoise(PORT_REMIX_ROOM_DAM);
    for (room = 0; room < 38; room++) {
        test_Print("stage %u %s\n", room,
                   port_RemixRoomName(port_RemixStageRoom((s32)room)));
    }
    CHECK_EQ(port_RemixStageRoom(-1), PORT_REMIX_ROOM_NONE);
    CHECK_EQ(port_RemixStageRoom(38), PORT_REMIX_ROOM_NONE);
}

/* ---- the head ---- */

/* A click from a source of these volumes, after it has been still long
 * enough for the far ear's delay to settle: 256 samples of each ear. */
static void test_Place(const char *name, s32 left, s32 right, s32 disc) {
    s32 volume[2];
    s32 ears[2][256];
    u32 n;

    volume[0] = left;
    volume[1] = right;
    port_RemixPlaceReset();
    for (n = 0; n < 4000 + 256; n++) {
        s32 sample = n == 4000 ? 16384 : 0;
        s32 got[2];

        if (disc) {
            got[0] = 0;
            got[1] = 0;
            port_RemixSpeaker(DISC_LEFT, 0, sample, got);
        } else {
            port_RemixPlaceRun(0, sample, volume, got);
        }
        if (n >= 4000) {
            ears[0][n - 4000] = got[0];
            ears[1][n - 4000] = got[1];
        }
    }
    for (n = 0; n < 2; n++) {
        u32 i;

        test_Print("ear %s %s", name, n == 0 ? "left" : "right");
        for (i = 0; i < 256; i++) {
            test_Print(" %d", ears[n][i]);
        }
        test_Print("\n");
    }
}

static void test_Head(void) {
    test_Print("head most_delay %d pole %d speaker %d\n", port_RemixMostDelay(),
               g_remix.low_pole, g_remix.speaker_gain);
    test_Place("hard_left", 32766, 0, 0);
    test_Place("hard_right", 0, 32766, 0);
    test_Place("middle", 23170, 23170, 0);
    test_Place("leftish", 30000, 12000, 0);
    test_Place("disc_left", 0, 0, 1);
}

/* ---- the chip, with the mix beside it ---- */

static u16 g_made_regs[PORT_SPU_REG_COUNT];
static u8 g_made_ram[PORT_SPU_RAM_BYTES];
static u16 g_made_after[PORT_SPU_REG_COUNT];

#define MADE_WAVE 0x2000
#define MADE_BLOCKS 64

/* Sound memory with a waveform of noise that loops, the reverb's mode 3,
 * and `voices` voices playing it at as many pitches. */
static void made_Chip(u32 voices) {
    const u16 *settings = &_spu_rev_param[3].dAPF1;
    u32 seed = 99;
    u32 i;

    for (i = 0; i < PORT_SPU_REG_COUNT; i++) {
        g_made_regs[i] = 0;
    }
    port_AttachSpu(g_made_regs);
    for (i = 0; i < MADE_BLOCKS * 16; i++) {
        u8 *ram = port_GetSpuRam() + MADE_WAVE;

        seed = seed * 1103515245U + 12345U;
        ram[i] = (u8)(seed >> 24);
        if (i % 16 == 0) {
            ram[i] = 0x24; /* a shift of 4, the second filter */
        } else if (i % 16 == 1) {
            ram[i] =
                i == 1 ? 0x04 : (i == (MADE_BLOCKS - 1) * 16 + 1 ? 0x03 : 0);
        }
    }
    for (i = 0; i < 32; i++) {
        g_made_regs[PORT_SPU_REVERB_REGS + i] = settings[i];
    }
    g_made_regs[PORT_SPU_REVERB_BASE] = (u16)_spu_rev_startaddr[3];
    g_made_regs[PORT_SPU_REVERB_VOLUME] = 0x3000;
    g_made_regs[PORT_SPU_REVERB_VOLUME + 1] = 0x3000;
    g_made_regs[PORT_SPU_REVERB_VOICES] = 0xFFFF;
    g_made_regs[PORT_SPU_REVERB_VOICES + 1] = 0xFF;
    g_made_regs[PORT_SPU_MAIN_VOLUME] = 0x3FFF;
    g_made_regs[PORT_SPU_MAIN_VOLUME + 1] = 0x3FFF;
    g_made_regs[PORT_SPU_CD_VOLUME] = 0x4000;
    g_made_regs[PORT_SPU_CD_VOLUME + 1] = 0x4000;
    g_made_regs[PORT_SPU_CONTROL] =
        PORT_SPU_CONTROL_ON | PORT_SPU_CONTROL_UNMUTED |
        PORT_SPU_CONTROL_REVERB | PORT_SPU_CONTROL_CD;
    for (i = 0; i < voices; i++) {
        u16 *regs = g_made_regs + i * PORT_SPU_VOICE_REGS;

        regs[PORT_SPU_VOICE_VOLUME_LEFT] = (u16)(0x0400 + i * 0x40);
        regs[PORT_SPU_VOICE_VOLUME_RIGHT] = (u16)(0x0C00 - i * 0x40);
        regs[PORT_SPU_VOICE_PITCH] = (u16)(0x0400 + i * 0x2A0);
        regs[PORT_SPU_VOICE_START] = MADE_WAVE / 8;
        regs[PORT_SPU_VOICE_ADSR_LOW] = 0x00FF;
        regs[PORT_SPU_VOICE_ADSR_HIGH] = 0x1FC0;
    }
    g_made_regs[PORT_SPU_KEY_ON] =
        (u16)(voices >= 16 ? 0xFFFF : (1U << voices) - 1);
    g_made_regs[PORT_SPU_KEY_ON + 1] =
        (u16)(voices > 16 ? (1U << (voices - 16)) - 1 : 0);
}

static void made_Cd(s16 *cd, u32 frames, u32 from) {
    u32 i;

    for (i = 0; i < frames; i++) {
        cd[2 * i] =
            (s16)made_Sine((s64)((from + i) % 100) * (2 * ONE_Q30 / 100), 9000);
        cd[2 * i + 1] =
            (s16)made_Sine((s64)((from + i) % 147) * (2 * ONE_Q30 / 147), 7000);
    }
}

/* The chip run twice, with the remastered mix and without, the same
 * registers, waveforms and disc: its samples, its memory and its
 * registers the same to the bit; and the mix's samples are not the
 * chip's. */
static void test_Chip(void) {
    static s16 cd[2 * 735];
    static s16 alone[2 * 735 * 120];
    static s16 beside[2 * 735 * 120];
    static s16 remastered[2 * 735];
    u32 frames = 735;
    u32 blank;
    u32 differ = 0;
    u32 heard = 0;
    u32 i;

    made_Chip(24);
    for (blank = 0; blank < 120; blank++) {
        made_Cd(cd, frames, blank * frames);
        port_RunSpu(cd, NULL, alone + blank * frames * 2, NULL, frames);
    }
    test_Copy(g_made_ram, port_GetSpuRam(), PORT_SPU_RAM_BYTES);
    test_Copy(g_made_after, g_made_regs, sizeof(g_made_regs));

    made_Chip(24);
    port_SetSoundRemastered(1);
    port_SetSoundHeadphones(1);
    for (blank = 0; blank < 120; blank++) {
        made_Cd(cd, frames, blank * frames);
        port_RemixBegin(blank < 60 ? 24 : -1);
        port_RunSpuRemastered(
            cd, NULL, beside + blank * frames * 2, NULL, remastered, frames);
        for (i = 0; i < frames * 2; i++) {
            differ += remastered[i] != beside[blank * frames * 2 + i];
            heard += remastered[i] != 0;
        }
    }
    port_SetSoundRemastered(0);
    port_SetSoundHeadphones(0);
    CHECK(test_BytesEqual(alone, beside, sizeof(alone)));
    CHECK(test_BytesEqual(g_made_ram, port_GetSpuRam(), PORT_SPU_RAM_BYTES));
    CHECK(test_BytesEqual(g_made_after, g_made_regs, sizeof(g_made_regs)));
    CHECK(differ > frames * 100);
    CHECK(heard > frames * 100);
    CHECK_EQ(g_port_remix_live, 0);
    test_Print("chip the_same %d differ %u heard %u\n",
               test_BytesEqual(alone, beside, sizeof(alone)), differ, heard);
}

/* ---- the mixer's time ---- */

/* 24 voices sounding for ten seconds, 735 samples a blank: the chip
 * alone, then with the remastered mix beside it (speakers, then
 * headphones), in a room. */
static void test_Bench(void) {
    static s16 cd[2 * 735];
    static s16 out[2 * 735];
    static s16 remastered[2 * 735];
    static const char * const names[3] = {
        "original", "remastered", "remastered+headphones"};
    u32 kind;
    u32 start;

    g_remix.made = 0;
    start = test_Microseconds();
    port_RemixPrepare();
    test_Print("bench tables %u us\n", test_Microseconds() - start);
    for (kind = 0; kind < 3; kind++) {
        u32 blank;
        u32 took;

        made_Chip(24);
        port_SetSoundHeadphones(kind == 2);
        start = test_Microseconds();
        for (blank = 0; blank < 600; blank++) {
            made_Cd(cd, 735, blank * 735);
            if (kind == 0) {
                port_RunSpu(cd, NULL, out, NULL, 735);
            } else {
                port_RemixBegin(24); /* the Purple Cavern */
                port_RunSpuRemastered(cd, NULL, out, NULL, remastered, 735);
            }
        }
        took = test_Microseconds() - start;
        test_Print(
            "bench %s: %u us for 10 s of sound, %u us a blank, %u.%02u%% of "
            "a processor\n",
            names[kind], took, took / 600, took / 100000, took / 1000 % 100);
    }
}

void test_Main(void) {
    if (test_Arg(0) != NULL && test_StringsEqual(test_Arg(0), "bench")) {
        test_Bench();
        return;
    }
    test_Kernels();
    test_Resampler();
    test_Reverb();
    test_Head();
    test_Chip();
}
