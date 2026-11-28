/* The sound remastered (include/port_remix.h; docs/PORT.md, "The sound
 * remastered"; ROADMAP 32): a second mix of the chip's voices, for the
 * player's ears and --wav alone, beside the chip's own (src/port/spu.c),
 * which stays to the bit what it was.
 *
 * Three parts:
 *
 *   - the resampler. The chip plays a waveform at its pitch by a table
 *     of four weights (its "Gaussian"), which smooths away much of the
 *     top of the sound and lets through images of it above. Here a voice
 *     keeps its last 64 samples, under its envelope, and each sample is
 *     the sum of 32 to 64 of them under a sinc windowed by Kaiser's
 *     window (beta 8): flat within 0.002 dB to 0.35 of the rate, and
 *     all else 75 dB and more down. Played faster than recorded, the
 *     sinc is narrowed to the rate it is played at (seven bands), so
 *     what would fold back is taken out first. It is 32 of the
 *     waveform's samples behind the chip, under a millisecond at the
 *     game's rates;
 *   - the reverb: a network of eight delay lines fed back through a
 *     Householder matrix (I - J/4: lossless, and exact in whole numbers),
 *     each line damped at the top and its gain set for the room's decay,
 *     after four all-pass filters that thicken the first echoes and a
 *     pre-delay. Its room is the stage's (g_port_remix_stage_rooms) and
 *     every voice is heard in it; out of a fight the room is none and
 *     the chip's own reverb is heard, as in the original;
 *   - the head, with headphones: a voice's left and right volumes say
 *     where it is, from the left (-90 degrees) to the right; the ear
 *     away from it hears it later (Woodworth's delay round a head 8.75
 *     cm across), a little softer, and with its highs shaded by the head
 *     (Brown and Duda's model of a spherical head, one pole and one zero
 *     an ear, made level for a sound straight ahead so that a voice in
 *     the middle is as it was). The disc's music is two speakers at 30
 *     degrees either side. No measured head is used: every number is the
 *     formula's, and there is no data of anyone else's to license.
 *
 * Then a limiter, 64 samples ahead, where the chip cuts its sum at 16
 * bits.
 *
 * Whole numbers throughout, as the chip is: no floating point, so no
 * denormal and no NaN, and the 32-bit and 64-bit programs mix the same.
 * The tables are made once, by series (the sine, the exponential and
 * the Bessel function the window needs), as the setting is turned on.
 * The reverb's arithmetic rounds a product towards zero, so that its
 * tail dies to nothing and never sits at a last step. Nothing here is
 * the game's state (config/port_state.txt): nothing of the game reads
 * it. But where a rollback puts the game's state back (netplay, the
 * self-test), the mix's running state is put back with it
 * (port_RemixKeep, port_RemixPutBack), so that the steps run again are
 * mixed from where it was then, and the reverb hears each sound once. */
#include "common.h"
#include "port_host.h"
#include "port_remix.h"
#include "port_spu.h"
#include "bb2.h"

/* ---- arithmetic for the tables, by series ---- */

#define Q30 30
#define ONE_Q30 ((s64)1 << Q30)
/* pi, of 2 to the 24th: what a number of half turns of 2 to the 30th is
 * multiplied by for its radians, staying within 64 bits */
#define PI_Q24 52707179LL
#define LN2_Q30 744261118LL
/* The bits of the long division's numbers. */
#define WIDE_BITS 64
/* 1 / ((2n)(2n + 1)), of 2 to the 30th: the sine's series, a term from
 * the one before */
#define SINE_TERMS 8
static const s32 g_port_remix_sine_steps[SINE_TERMS + 1] = {
    0,       178956971, 53687091, 25565282, 14913081,
    9761289, 6882960,   5113056,  3947580,
};
/* 1 / n, of 2 to the 30th: the exponential's series */
#define FALL_TERMS 12
static const s32 g_port_remix_inverse[FALL_TERMS + 1] = {
    0,         1073741824, 536870912, 357913941, 268435456,
    214748365, 178956971,  153391689, 134217728, 119304647,
    107374182, 97612893,   89478485,
};

/* `numerator` / `divisor`, towards zero, by long division: the 32-bit
 * program has no library's division of 64 bits. For the tables only. */
static s64 port_RemixDivide(s64 numerator, s64 divisor) {
    u64 top = (u64)(numerator < 0 ? -numerator : numerator);
    u64 under = (u64)(divisor < 0 ? -divisor : divisor);
    u64 quotient = 0;
    u64 rest = 0;
    s32 bit;

    for (bit = WIDE_BITS - 1; bit >= 0; bit--) {
        rest = (rest << 1) | ((top >> bit) & 1);
        if (rest >= under) {
            rest -= under;
            quotient |= (u64)1 << bit;
        }
    }
    return ((numerator < 0) != (divisor < 0)) ? -(s64)quotient : (s64)quotient;
}

/* The square root of `value`, rounded down. */
static u64 port_RemixRoot(u64 value) {
    u64 root = 0;
    u64 bit = (u64)1 << 62;

    while (bit > value) {
        bit >>= 2;
    }
    while (bit != 0) {
        if (value >= root + bit) {
            value -= root + bit;
            root = (root >> 1) + bit;
        } else {
            root >>= 1;
        }
        bit >>= 2;
    }
    return root;
}

/* sin(pi * turns), `turns` half turns of 2 to the 30th; of 2 to the
 * 30th. */
static s64 port_RemixSine(s64 turns) {
    s32 negative = 0;
    s64 x;
    s64 square;
    s64 term;
    s64 sum;
    s32 order;

    if (turns < 0) {
        turns = -turns;
        negative = 1;
    }
    turns &= 2 * ONE_Q30 - 1;
    if (turns >= ONE_Q30) {
        turns -= ONE_Q30;
        negative ^= 1;
    }
    if (turns > ONE_Q30 / 2) {
        turns = ONE_Q30 - turns;
    }
    x = (turns * PI_Q24) >> 24;
    square = (x * x) >> Q30;
    term = x;
    sum = x;
    for (order = 1; order <= SINE_TERMS; order++) {
        term =
            -(((term * square) >> Q30) * g_port_remix_sine_steps[order] >> Q30);
        sum += term;
    }
    return negative ? -sum : sum;
}

/* e to the -`x`, `x` from 0 to 1 of 2 to the 30th; of 2 to the 30th. */
static s64 port_RemixFall(s64 x) {
    s64 term = ONE_Q30;
    s64 sum = ONE_Q30;
    s32 order;

    for (order = 1; order <= FALL_TERMS; order++) {
        term = -(((term * x) >> Q30) * g_port_remix_inverse[order] >> Q30);
        sum += term;
    }
    return sum;
}

/* 2 to the -`k`, `k` of 2 to the 16th; of 2 to the 30th. */
static s64 port_RemixHalves(u32 k) {
    u32 whole = k >> 16;
    s64 part = ((s64)(k & 0xFFFF) << 14) * LN2_Q30 >> Q30;

    if (whole >= WIDE_BITS - 2) {
        return 0;
    }
    return port_RemixFall(part) >> whole;
}

/* The Bessel function I0 of `z`, both of 2 to the 16th. */
static s32 port_RemixBessel(s32 z) {
    s32 half = z / 2;
    s32 quarter_square = (s32)(((s64)half * half) >> 16);
    s32 term = 1 << 16;
    s32 sum = term;
    s32 k;

    /* until a term is too small to count (the window's beta needs
     * about 30) */
    for (k = 1; k < 64 && term != 0; k++) {
        term = (s32)(((s64)term * quarter_square) >> 16) / (k * k);
        sum += term;
    }
    return sum;
}

/* ---- the resampler ---- */

/* A voice's last samples, kept twice over so that any 64 in a row are
 * one run of memory. */
#define RING 64
#define RING_MASK (RING - 1)
/* The kernel's middle is this many samples behind the newest. */
#define KERNEL_DELAY 32
#define PHASE_SHIFT 4 /* of the chip's 12 bits of fraction, the low 4 */
#define PHASE_PART_MASK 0xF
#define COEF_SHIFT 14
/* Kaiser's beta, of 2 to the 16th: 8, whose side lobes are 80 dB down */
#define KAISER_BETA (8 << 16)

/* A band: the fastest step it serves, its taps, and how much faster than
 * recorded it plays, in eighths (its sinc is narrowed by that). */
static const struct {
    u16 step_most;
    u8 taps;
    u8 eighths;
} g_port_remix_bands[PORT_REMIX_BANDS] = {
    {0x1000, 32, 8},  {0x1400, 40, 10}, {0x1800, 48, 12}, {0x2000, 64, 16},
    {0x2800, 64, 20}, {0x3000, 64, 24}, {0x4000, 64, 32},
};

#define KERNEL_COEFS ((PORT_REMIX_PHASES + 1) * (32 + 40 + 48 + 64 * 4))
#define TAPS_MOST 64
/* the step of a voice as recorded, in eighths */
#define EIGHTHS_ONE 8
/* the kernel's places between two samples: 2 to the 8th */
#define PHASE_BITS 8

typedef struct {
    s16 ring[RING * 2];
    u32 head;  /* samples pushed */
    u32 quiet; /* of them, the last that were silent, up to RING */
} PortRemixVoice;

/* ---- the reverb ---- */

#define LINES 8
#define LINE_SIZE 8192
#define LINE_MASK (LINE_SIZE - 1)
#define PASSES 4
#define PASS_SIZE 1024
#define PASS_MASK (PASS_SIZE - 1)
#define PRE_SIZE 4096
#define PRE_MASK (PRE_SIZE - 1)
/* The reverb works at 2 to the 8th of a sample's step. */
#define REVERB_SHIFT 8
#define GAIN_SHIFT 15
/* a room's size, in hundredths */
#define SIZE_ONE 100
#define MILLISECONDS_ONE 1000
/* 1 - 2 pi 20 / 44100, of 32768: the high pass at 20 Hz */
#define DC_POLE 32675
/* The lines' and the all-passes' lengths in a room of size 100, in
 * samples at 44,100 a second: no two with a common factor. */
static const u16 g_port_remix_line_lengths[LINES] = {
    1009, 1171, 1361, 1553, 1747, 1949, 2129, 2357,
};
static const u16 g_port_remix_pass_lengths[PASSES] = {142, 107, 379, 277};
/* 3 log2(10) / 44.1, of 2 to the 16th: a line's length in samples times
 * this over the decay in milliseconds is the halvings it loses in a trip
 * round, for 60 dB in the decay */
#define DECAY_HALVINGS 14810

/* A room. `decay` is the time to fall 60 dB at low frequencies, in
 * milliseconds; `size` the lines' lengths, in hundredths of the table's;
 * `pre` the pre-delay in milliseconds; `damp` how much of the top each
 * trip round a line loses (a one-pole low pass, of 32768); `diffusion`
 * the all-passes' gain, of 32768; `wet` how loud the reverb is heard, of
 * 32768. */
typedef struct {
    const char *name;
    u16 decay;
    u8 size;
    u8 pre;
    u16 damp;
    u16 diffusion;
    u16 wet;
} PortRemixRoom;

static const PortRemixRoom g_port_remix_rooms[PORT_REMIX_ROOMS] = {
    /* name          decay size pre  damp  diffusion wet */
    {"none", 0, 100, 0, 0, 0, 0},
    {"open", 450, 170, 30, 18000, 16000, 8000},
    {"field", 550, 150, 24, 17000, 17000, 8800},
    {"forest", 750, 110, 12, 20000, 22000, 12000},
    {"bamboo", 600, 70, 6, 11000, 24000, 11900},
    {"courtyard", 950, 100, 10, 13000, 21000, 15100},
    {"valley", 1700, 200, 45, 16000, 15000, 14200},
    {"coast", 500, 170, 35, 19000, 16000, 8000},
    {"rooftop", 550, 180, 40, 15000, 15000, 9900},
    {"underpass", 1300, 90, 8, 10000, 20000, 16700},
    {"yard", 950, 140, 20, 11500, 18000, 15700},
    {"dojo", 850, 60, 5, 14500, 23000, 16200},
    {"hall", 1600, 100, 12, 13000, 23000, 21900},
    {"theatre", 1400, 110, 15, 14500, 23000, 20900},
    {"ship", 1100, 55, 4, 6500, 20000, 16000},
    {"station", 2000, 140, 18, 10000, 23000, 23700},
    {"garage", 1800, 120, 10, 8000, 21000, 22400},
    {"dam", 2600, 200, 40, 10000, 18000, 21500},
    {"tunnel", 2300, 90, 6, 8000, 20000, 22200},
    {"cave", 3200, 150, 14, 13000, 24500, 29400},
};

/* Each stage's room (enum StageIndex), chosen by what the stage is: the
 * original gave a reverb to the halls and rooms (Meikyokan's dojo,
 * Shainto's, the team battles', the practice stages', the parking lot)
 * and the cavern, and none outside; here the outside has its own too.
 * docs/PORT.md, "The sound remastered", has the table. */
static const u8 g_port_remix_stage_rooms[38] = {
    PORT_REMIX_ROOM_HALL,      /* Main Tatara Shrine */
    PORT_REMIX_ROOM_DOJO,      /* Meikyokan Dojo */
    PORT_REMIX_ROOM_HALL,      /* Shainto HQ */
    PORT_REMIX_ROOM_COURTYARD, /* Tatara Shrine */
    PORT_REMIX_ROOM_FIELD,     /* Bus Stop */
    PORT_REMIX_ROOM_VALLEY,    /* Lost Bridge */
    PORT_REMIX_ROOM_FOREST,    /* Tatara Shrine Path */
    PORT_REMIX_ROOM_BAMBOO,    /* Bamboo Thicket */
    PORT_REMIX_ROOM_COURTYARD, /* Outside the Meikyokan */
    PORT_REMIX_ROOM_YARD,      /* Future Dump Site */
    PORT_REMIX_ROOM_VALLEY,    /* Snake Cliff */
    PORT_REMIX_ROOM_COAST,     /* Kimigaura Coast */
    PORT_REMIX_ROOM_FIELD,     /* Orange Tree Fields */
    PORT_REMIX_ROOM_ROOFTOP,   /* Castle Rooftop */
    PORT_REMIX_ROOM_UNDERPASS, /* Under Lost Bridge */
    PORT_REMIX_ROOM_FIELD,     /* Terraced Fields */
    PORT_REMIX_ROOM_UNDERPASS, /* Under Railway */
    PORT_REMIX_ROOM_DAM,       /* Water Tower Dam */
    PORT_REMIX_ROOM_YARD,      /* Lumberyard */
    PORT_REMIX_ROOM_COAST,     /* Lighthouse */
    PORT_REMIX_ROOM_THEATRE,   /* Kabuki Stage */
    PORT_REMIX_ROOM_SHIP,      /* Abandoned Ship */
    PORT_REMIX_ROOM_COAST,     /* Ferry Dock */
    PORT_REMIX_ROOM_COURTYARD, /* Shainto Gate */
    PORT_REMIX_ROOM_CAVE,      /* Purple Cavern */
    PORT_REMIX_ROOM_STATION,   /* Abandoned Station */
    PORT_REMIX_ROOM_OPEN,      /* Railway */
    PORT_REMIX_ROOM_TUNNEL,    /* Abandoned Tunnel */
    PORT_REMIX_ROOM_YARD,      /* Abandoned Shipyard */
    PORT_REMIX_ROOM_ROOFTOP,   /* Building Rooftop */
    PORT_REMIX_ROOM_GARAGE,    /* Building Parking Lot */
    PORT_REMIX_ROOM_OPEN,      /* Shainto Cemetery */
    PORT_REMIX_ROOM_HALL,      /* team battle 1 */
    PORT_REMIX_ROOM_HALL,      /* team battle 2 */
    PORT_REMIX_ROOM_HALL,      /* chanbara's hall */
    PORT_REMIX_ROOM_DOJO,      /* practice 1 */
    PORT_REMIX_ROOM_DOJO,      /* practice 2 */
    PORT_REMIX_ROOM_OPEN,      /* stage 37 */
};

typedef struct {
    s32 lines[LINES][LINE_SIZE];
    s32 passes[PASSES][PASS_SIZE];
    s32 pre[PRE_SIZE];
    s32 lowpass[LINES];
    u32 length[LINES];
    u32 pass_length[PASSES];
    u32 pre_length;
    s32 gain[LINES];
    s32 damp;
    s32 diffusion;
    s32 unblocked; /* the high pass's last sample in, and out */
    s32 blocked;
    u32 at;
    u32 room;      /* the room the lines are set for */
    u32 tail_left; /* samples the tail is run on, the room left */
} PortRemixReverb;

/* ---- the head ---- */

#define PLACE_RING 64
#define PLACE_MASK (PLACE_RING - 1)
#define PLACE_STEPS 64 /* the table's places from the middle to a side */
#define PLACE_ONE 4096 /* a pan of all the way, and an ear's gain of 1 */
#define PLACE_SHIFT 12
/* The far ear's delay moves to a new place by this, of 256ths of a
 * sample, at each sample, so that it slides and does not click. */
#define PLACE_SLIDE 8
/* The source a disc's side is: a speaker this far round, in the table's
 * steps (30 degrees of 90), and how loud, of 32768, so that music in the
 * middle is as loud at each ear as it was. */
#define SPEAKER_STEP 21
#define LOW_SHIFT 4 /* the head's low pass keeps 4 bits under a sample */
/* the far ear's delay is in 256ths of a sample */
#define PLACE_PART_BITS 8
#define PLACE_PART_ONE (1 << PLACE_PART_BITS)
#define DISC_LEFT 24
#define DISC_RIGHT 25

typedef struct {
    s32 itd;  /* the far ear's delay, 256ths of a sample */
    s32 near; /* each ear's highs, of PLACE_ONE */
    s32 far;
    s32 far_gain; /* the far ear's level, of 32768 */
} PortRemixHead;

typedef struct {
    s32 ring[PLACE_RING];
    u32 at;
    s32 low[2]; /* each ear's low pass, LOW_SHIFT bits under a sample */
    s32 volume[2];
    s32 gain;  /* the source's level, of 32768, from its volumes */
    s32 side;  /* the ear away from it: 0 the left, 1 the right */
    s32 step;  /* its place in the table */
    s32 delay; /* the far ear's delay now, 256ths of a sample */
    u32 quiet; /* samples in a row it was given nothing */
} PortRemixPlace;

/* ---- the limiter ---- */

#define LOOK_AHEAD 64
#define LOOK_MASK (LOOK_AHEAD - 1)
#define LIMIT_ONE 32768
#define SAMPLE_MOST 0x7FFF
#define SAMPLE_LEAST (-0x8000)
#define LIMIT_MOST 32767
/* how fast the level comes back: 1/2048 of the way a sample, about 50 ms
 * to two thirds */
#define LIMIT_RISE 11

/* ---- all of it ---- */

static struct {
    s32 made;
    s32 is_on;
    s32 headphones;
    s32 room;
    s32 low_pole;     /* the head's low pass, of 32768 */
    s32 speaker_gain; /* a speaker's level, of 32768 */
    s32 dry[2];
    s32 send[2];
    PortRemixCounts counts;
    /* the limiter: the samples waiting, what each needs, the level */
    s32 waiting[LOOK_AHEAD][2];
    s32 needs[LOOK_AHEAD];
    u32 look_at;
    s32 least; /* the least of `needs`, and when it was written */
    u32 least_at;
    s32 level;
    PortRemixVoice voices[PORT_SPU_VOICES];
    PortRemixPlace places[PORT_REMIX_SOURCES];
    PortRemixHead head[PLACE_STEPS + 1];
    u32 band_start[PORT_REMIX_BANDS];
    /* the two large parts, in memory the system gives (port_HostAllocate):
     * as variables they would stand among the data before the game's
     * state, in the Windows program, and push it out of the space its
     * ordering tables can name (docs/PORT.md, "Where the program's
     * variables are") */
    s16 *kernels; /* KERNEL_COEFS of them */
    PortRemixReverb *reverb;
} g_remix;

s32 g_port_remix_live;
s16 g_port_remix_out[PORT_REMIX_MOST_FRAMES * 2];

/* ---- the tables ---- */

/* Kaiser's window at `x` (-1 to 1, of 2 to the 30th), of 2 to the 30th. */
static s64 port_RemixWindow(s64 x, s32 bessel_beta) {
    s64 square = (x * x) >> Q30;
    u64 under = (u64)(ONE_Q30 - square) << Q30;
    s64 root = (s64)port_RemixRoot(under);
    s32 z = (s32)(((s64)KAISER_BETA * root) >> Q30);

    return port_RemixDivide((s64)port_RemixBessel(z) << Q30, bessel_beta);
}

/* Band `band`'s kernel: for each of PORT_REMIX_PHASES + 1 places between
 * two samples, its taps, which add up to PORT_REMIX_COEF_ONE. */
static void port_RemixMakeBand(u32 band, s32 bessel_beta, s16 *out) {
    s32 taps = g_port_remix_bands[band].taps;
    s32 half = taps / 2;
    /* 1 / half, and how narrow the sinc is: 8 / eighths */
    s64 per_half = port_RemixDivide(ONE_Q30, half);
    s64 narrow = port_RemixDivide(
        EIGHTHS_ONE * ONE_Q30, g_port_remix_bands[band].eighths);
    s64 row[TAPS_MOST];
    s32 phase;
    s32 k;

    for (phase = 0; phase <= PORT_REMIX_PHASES; phase++) {
        s64 sum = 0;
        s32 total = 0;
        s32 middle = phase < PORT_REMIX_PHASES / 2 ? half - 1 : half;

        for (k = 0; k < taps; k++) {
            /* how far tap k is from the place, in samples */
            s64 apart = ((s64)(k - half + 1) << Q30) -
                        ((s64)phase << (Q30 - PHASE_BITS));
            s64 u = ((apart >> PHASE_BITS) * narrow) >> (Q30 - PHASE_BITS);
            s64 sinc = ONE_Q30;

            if (u != 0) {
                sinc = port_RemixDivide(
                    port_RemixSine(u) << Q30, (u * PI_Q24) >> 24);
            }
            row[k] = (sinc * port_RemixWindow(
                                 (apart * per_half) >> Q30, bessel_beta)) >>
                     Q30;
            sum += row[k];
        }
        for (k = 0; k < taps; k++) {
            s64 scaled = row[k] * PORT_REMIX_COEF_ONE;
            s32 coef = (s32)port_RemixDivide(
                scaled + (scaled < 0 ? -sum / 2 : sum / 2), sum);

            out[phase * taps + k] = (s16)coef;
            total += coef;
        }
        /* what rounding left over goes to the tap nearest the place */
        out[phase * taps + middle] =
            (s16)(out[phase * taps + middle] + PORT_REMIX_COEF_ONE - total);
    }
}

/* 1.05 and 0.95, of 2 to the 30th */
#define ALPHA_BASE 1127428915LL
#define ALPHA_SWING 1020054733LL
/* the angle's 1.2, in tenths */
#define ALPHA_TIMES_TENTHS 12
#define TENTHS_ONE 10

/* An ear's shading, as Brown and Duda have it, for an angle of `turns`
 * half turns between the ear and the sound: 1.05 + 0.95 cos(1.2 angle),
 * of 2 to the 30th. */
static s64 port_RemixAlpha(s64 turns) {
    /* 1.2 times the angle, and a quarter turn on for the cosine */
    s64 cosine = port_RemixSine(
        port_RemixDivide(turns * ALPHA_TIMES_TENTHS, TENTHS_ONE) + ONE_Q30 / 2);

    return ALPHA_BASE + ((ALPHA_SWING * cosine) >> Q30);
}

/* The head's table: for each place from the middle to a side, the far
 * ear's delay, both ears' highs and the far ear's level. */
static void port_RemixMakeHead(void) {
    /* each ear's shading over its shading at 90 degrees, where a sound
     * straight ahead is */
    s64 side_alpha;
    s32 step;

    side_alpha = port_RemixAlpha(ONE_Q30 / 2);
    for (step = 0; step <= PLACE_STEPS; step++) {
        PortRemixHead *head = &g_remix.head[step];
        /* the angle, in half turns: up to a half (90 degrees) */
        s64 angle = ((s64)step << Q30) >> 7;
        s64 sine = port_RemixSine(angle);
        s64 radians = (angle * PI_Q24) >> 24;
        s64 near_alpha = port_RemixAlpha(ONE_Q30 / 2 - angle);
        s64 far_alpha = port_RemixAlpha(ONE_Q30 / 2 + angle);

        /* Woodworth: (radius / speed of sound) (angle + sin angle), in
         * samples: 0.0875 / 343 * 44100 = 11.25, 2880 256ths */
        head->itd = (s32)(((radians + sine) * 2880) >> Q30);
        head->near = (s32)port_RemixDivide(near_alpha * PLACE_ONE, side_alpha);
        head->far = (s32)port_RemixDivide(far_alpha * PLACE_ONE, side_alpha);
        if (head->near > 2 * PLACE_ONE) {
            head->near = 2 * PLACE_ONE;
        }
        /* 3 dB down at the side, at all frequencies */
        head->far_gain = 32768 - (s32)((sine * 9830) >> Q30);
    }
    /* the low pass at twice c / a: 1 - e^(-7840 / 44100) */
    g_remix.low_pole =
        (s32)((ONE_Q30 -
               port_RemixFall(port_RemixDivide(ONE_Q30 * 7840, 44100))) >>
              15);
    /* a speaker's level, so that music in the middle is as loud at each
     * ear as it was: 1 / (1 + the far ear's level) */
    g_remix.speaker_gain = (s32)port_RemixDivide(
        (s64)32768 * 32768, 32768 + g_remix.head[SPEAKER_STEP].far_gain);
}

/* The tables, made once (port_remix.h): 0 if the system had no memory
 * for them. */
s32 port_RemixPrepare(void) {
    s32 bessel_beta;
    u32 band;
    u32 start = 0;

    if (g_remix.made) {
        return 1;
    }
    if (g_remix.kernels == NULL) {
        g_remix.kernels = port_HostAllocate(KERNEL_COEFS * sizeof(s16));
        g_remix.reverb = port_HostAllocate(sizeof(PortRemixReverb));
    }
    if (g_remix.kernels == NULL || g_remix.reverb == NULL) {
        return 0;
    }
    bessel_beta = port_RemixBessel(KAISER_BETA);
    for (band = 0; band < PORT_REMIX_BANDS; band++) {
        g_remix.band_start[band] = start;
        port_RemixMakeBand(band, bessel_beta, g_remix.kernels + start);
        start += (PORT_REMIX_PHASES + 1) * g_port_remix_bands[band].taps;
    }
    port_RemixMakeHead();
    g_remix.reverb->room = PORT_REMIX_ROOM_NONE;
    for (band = 0; band < LOOK_AHEAD; band++) {
        g_remix.needs[band] = LIMIT_ONE;
    }
    g_remix.level = LIMIT_ONE;
    g_remix.least = LIMIT_ONE;
    g_remix.made = 1;
    return 1;
}

/* ---- the settings ---- */

/* Turned on, the mix starts from silence: what its copies of the voices,
 * its head and its limiter held from the last time it was on is long
 * gone by. */
void port_SetSoundRemastered(s32 is_on) {
    if (is_on && !g_remix.is_on) {
        u8 *bytes;
        u32 i;

        if (!port_RemixPrepare()) {
            /* no memory for it: the chip's own sound */
            return;
        }
        bytes = (u8 *)g_remix.voices;
        for (i = 0; i < sizeof(g_remix.voices); i++) {
            bytes[i] = 0;
        }
        port_RemixPlaceReset();
        for (i = 0; i < LOOK_AHEAD; i++) {
            g_remix.waiting[i][0] = 0;
            g_remix.waiting[i][1] = 0;
        }
    }
    g_remix.is_on = is_on != 0;
}

s32 port_IsSoundRemastered(void) { return g_remix.is_on; }

void port_SetSoundHeadphones(s32 is_on) { g_remix.headphones = is_on != 0; }

s32 port_IsSoundHeadphones(void) { return g_remix.headphones; }

const PortRemixCounts *port_GetRemixCounts(void) { return &g_remix.counts; }

const char *port_RemixRoomName(u32 room) {
    return room < PORT_REMIX_ROOMS ? g_port_remix_rooms[room].name : "?";
}

u32 port_RemixStageRoom(s32 stage) {
    if (stage < 0 || stage >= (s32)sizeof(g_port_remix_stage_rooms)) {
        return PORT_REMIX_ROOM_NONE;
    }
    return g_port_remix_stage_rooms[stage];
}

u32 port_RemixRoomDecay(u32 room) { return g_port_remix_rooms[room].decay; }

u32 port_RemixRoomWet(u32 room) { return g_port_remix_rooms[room].wet; }

/* ---- the resampler ---- */

static const s16 *port_RemixFindKernel(u32 step, u32 phase, u32 *taps) {
    u32 band = 0;

    while (band + 1 < PORT_REMIX_BANDS &&
           step > g_port_remix_bands[band].step_most) {
        band++;
    }
    *taps = g_port_remix_bands[band].taps;
    return g_remix.kernels + g_remix.band_start[band] + phase * *taps;
}

const s16 *port_RemixKernel(u32 step, u32 phase, u32 *taps) {
    port_RemixPrepare();
    return port_RemixFindKernel(step, phase, taps);
}

/* A sample into the voice's copy (port_remix.h). */
void port_RemixPush(u32 voice, s32 sample) {
    PortRemixVoice *it = &g_remix.voices[voice];
    u32 at = it->head & RING_MASK;

    it->ring[at] = (s16)sample;
    it->ring[at + RING] = (s16)sample;
    it->head++;
    if (sample != 0) {
        it->quiet = 0;
    } else if (it->quiet < RING) {
        it->quiet++;
    }
}

/* The voice's sample at its place now: between the two rows of the
 * kernel either side of it, by the low bits of the fraction. */
static s32 port_RemixResample(const PortRemixVoice *it, u32 counter, u32 step) {
    u32 taps;
    const s16 *row = port_RemixFindKernel(step, counter >> PHASE_SHIFT, &taps);
    const s16 *next = row + taps;
    const s16 *samples =
        it->ring + ((it->head - KERNEL_DELAY - taps / 2) & RING_MASK);
    s32 here = 0;
    s32 there = 0;
    u32 k;

    for (k = 0; k < taps; k++) {
        here += row[k] * samples[k];
        there += next[k] * samples[k];
    }
    here += ((there - here) >> PHASE_SHIFT) * (s32)(counter & PHASE_PART_MASK);
    return (here + (1 << (COEF_SHIFT - 1))) >> COEF_SHIFT;
}

/* ---- the head ---- */

/* A product of 32768ths, rounded towards zero. */
static s32 port_RemixScale(s32 value, s32 gain) {
    s64 product = (s64)value * gain;

    return (s32)(product >= 0 ? product >> GAIN_SHIFT
                              : -((-product) >> GAIN_SHIFT));
}

/* Where a source is, from its volumes: its level, the ear away from it,
 * its place in the table. */
static void port_RemixPlaceBy(PortRemixPlace *place, const s32 *volume) {
    s32 left = volume[0] < 0 ? -volume[0] : volume[0];
    s32 right = volume[1] < 0 ? -volume[1] : volume[1];
    u32 square = ((u32)left * (u32)left + (u32)right * (u32)right) / 2;
    s32 pan;

    place->volume[0] = volume[0];
    place->volume[1] = volume[1];
    place->gain = (s32)port_RemixRoot(square);
    if (volume[0] + volume[1] < 0) {
        place->gain = -place->gain;
    }
    pan = left + right == 0 ? 0 : (right - left) * PLACE_STEPS / (left + right);
    place->side = pan > 0 ? 0 : 1;
    place->step = pan < 0 ? -pan : pan;
}

/* One sample of `sample` (at its level already) at the source's place:
 * the two ears into `ears`. */
static void port_RemixHear(PortRemixPlace *place, s32 sample, s32 *ears) {
    const PortRemixHead *head = &g_remix.head[place->step];
    s32 target = head->itd;
    s32 whole;
    s32 part;
    s32 later;
    s32 side;

    place->ring[place->at & PLACE_MASK] = sample;
    place->at++;
    if (place->delay < target) {
        place->delay += place->delay + PLACE_SLIDE < target
                            ? PLACE_SLIDE
                            : target - place->delay;
    } else if (place->delay > target) {
        place->delay -= place->delay - PLACE_SLIDE > target
                            ? PLACE_SLIDE
                            : place->delay - target;
    }
    whole = place->delay >> PLACE_PART_BITS;
    part = place->delay & (PLACE_PART_ONE - 1);
    later = (place->ring[(place->at - 1 - (u32)whole) & PLACE_MASK] *
                 (PLACE_PART_ONE - part) +
             place->ring[(place->at - 2 - (u32)whole) & PLACE_MASK] * part) >>
            PLACE_PART_BITS;
    later = port_RemixScale(later, head->far_gain);
    for (side = 0; side < 2; side++) {
        s32 heard = side == place->side ? later : sample;
        s32 alpha = side == place->side ? head->far : head->near;
        s32 low;

        place->low[side] += port_RemixScale(
            (heard << LOW_SHIFT) - place->low[side], g_remix.low_pole);
        low = place->low[side] >> LOW_SHIFT;
        ears[side] = low + (s32)(((s64)(heard - low) * alpha) >> PLACE_SHIFT);
    }
}

void port_RemixPlaceReset(void) {
    u8 *bytes = (u8 *)g_remix.places;
    u32 i;

    port_RemixPrepare();
    for (i = 0; i < sizeof(g_remix.places); i++) {
        bytes[i] = 0;
    }
}

void port_RemixPlaceRun(u32 source, s32 sample, const s32 *volume, s32 *ears) {
    PortRemixPlace *place = &g_remix.places[source];

    if (volume[0] != place->volume[0] || volume[1] != place->volume[1]) {
        port_RemixPlaceBy(place, volume);
    }
    port_RemixHear(place, port_RemixScale(sample, place->gain), ears);
}

s32 port_RemixMostDelay(void) {
    port_RemixPrepare();
    return g_remix.head[PLACE_STEPS].itd;
}

/* ---- the reverb ---- */

/* The room's lines, set: their lengths, gains and damping. A room of
 * other lengths starts from silence. */
static void port_RemixSetRoom(PortRemixReverb *reverb, u32 room) {
    const PortRemixRoom *it = &g_port_remix_rooms[room];
    u32 i;

    reverb->room = room;
    if (room == PORT_REMIX_ROOM_NONE) {
        return;
    }
    for (i = 0; i < LINES; i++) {
        u32 length = g_port_remix_line_lengths[i] * it->size / SIZE_ONE;
        u32 halvings = length * DECAY_HALVINGS / it->decay;

        reverb->length[i] = length;
        reverb->gain[i] = (s32)(port_RemixHalves(halvings) >> 15);
    }
    for (i = 0; i < PASSES; i++) {
        reverb->pass_length[i] =
            g_port_remix_pass_lengths[i] * it->size / SIZE_ONE;
    }
    reverb->pre_length = (u32)it->pre * PORT_SPU_RATE / MILLISECONDS_ONE + 1;
    reverb->damp = it->damp;
    reverb->diffusion = it->diffusion;
}

static void port_RemixClearReverb(PortRemixReverb *reverb) {
    u8 *bytes = (u8 *)reverb;
    u32 i;

    for (i = 0; i < sizeof(*reverb); i++) {
        bytes[i] = 0;
    }
}

/* One sample of the reverb: `in` a left and a right, `out` the same in
 * 256ths of a sample (REVERB_SHIFT), before the room's wet level. */
static void port_RemixRunReverb(
    PortRemixReverb *reverb, s32 in_left, s32 in_right, s32 *out) {
    s32 heard[LINES];
    s32 fed[LINES];
    s32 sum = 0;
    s32 quarter;
    s32 x = (in_left + in_right) * (1 << (REVERB_SHIFT - 1));
    u32 at = reverb->at;
    u32 i;

    /* what goes in loses its steady part first (a high pass at 20 Hz),
     * which the lines would otherwise add up */
    reverb->blocked =
        x - reverb->unblocked + port_RemixScale(reverb->blocked, DC_POLE);
    reverb->unblocked = x;
    x = reverb->blocked;
    reverb->pre[at & PRE_MASK] = x;
    x = reverb->pre[(at - reverb->pre_length) & PRE_MASK];
    for (i = 0; i < PASSES; i++) {
        s32 *pass = reverb->passes[i];
        s32 delayed = pass[(at - reverb->pass_length[i]) & PASS_MASK];
        s32 into = x - port_RemixScale(delayed, reverb->diffusion);

        pass[at & PASS_MASK] = into;
        x = delayed + port_RemixScale(into, reverb->diffusion);
    }
    x /= 2;
    for (i = 0; i < LINES; i++) {
        heard[i] = reverb->lines[i][(at - reverb->length[i]) & LINE_MASK];
        reverb->lowpass[i] =
            heard[i] +
            port_RemixScale(reverb->lowpass[i] - heard[i], reverb->damp);
        fed[i] = port_RemixScale(reverb->lowpass[i], reverb->gain[i]);
        sum += fed[i];
    }
    /* Householder's reflection, I - 2/8 of all ones: each line gets its
     * own back less a quarter of all of them */
    quarter = sum / 4;
    for (i = 0; i < LINES; i++) {
        reverb->lines[i][at & LINE_MASK] =
            fed[i] - quarter + ((i & 1) ? -x : x);
    }
    reverb->at = at + 1;
    /* the two sides, each half the lines less the others, in two
     * patterns that share nothing */
    out[0] = 0;
    out[1] = 0;
    for (i = 0; i < LINES; i++) {
        out[0] += (i & 1) ? -heard[i] : heard[i];
        out[1] += (i & 2) ? -heard[i] : heard[i];
    }
    out[0] /= 4;
    out[1] /= 4;
}

void port_RemixReverbReset(u32 room) {
    port_RemixPrepare();
    port_RemixClearReverb(g_remix.reverb);
    port_RemixSetRoom(g_remix.reverb, room);
}

void port_RemixReverbRun(s32 in_left, s32 in_right, s32 *out) {
    port_RemixRunReverb(g_remix.reverb, in_left, in_right, out);
}

/* ---- the mix ---- */

void port_RemixBegin(s32 stage) {
    u32 room = port_RemixStageRoom(stage);
    PortRemixReverb *reverb = g_remix.reverb;

    port_RemixPrepare();
    if ((s32)room != g_remix.room) {
        g_remix.room = (s32)room;
        g_remix.counts.rooms++;
    }
    g_remix.counts.room = room;
    if (room == PORT_REMIX_ROOM_NONE) {
        return;
    }
    if (room != reverb->room) {
        /* a new room: the old one's tail, if any, is cut where a fight's
         * loading has made it silent already */
        port_RemixClearReverb(reverb);
        port_RemixSetRoom(reverb, room);
    }
    /* six times the decay, for the tail once the room is left */
    reverb->tail_left =
        (u32)g_port_remix_rooms[room].decay * 6 * (PORT_SPU_RATE / 1000);
}

/* A voice's part of the next sample (port_remix.h): resampled, unless
 * it plays noise or has been silent for a kernel's length; into the sum
 * the reverb hears, and as it is or at its place round the head into the
 * sum heard. */
void port_RemixVoice(u32 voice, u32 counter, u32 step, s32 noise,
                     s32 noise_sample, const s32 *volume) {
    PortRemixVoice *it = &g_remix.voices[voice];
    PortRemixPlace *place = &g_remix.places[voice];
    s32 sample;
    s32 side;

    if (volume[0] == 0 && volume[1] == 0) {
        sample = 0;
    } else if (noise) {
        sample = noise_sample;
    } else if (it->quiet >= RING) {
        sample = 0;
    } else {
        sample = port_RemixResample(it, counter, step);
    }
    if (sample == 0 && (!g_remix.headphones || place->quiet >= PLACE_RING)) {
        return;
    }
    for (side = 0; side < 2; side++) {
        s32 heard = (sample * volume[side]) >> 15;

        g_remix.send[side] += heard;
        if (!g_remix.headphones) {
            g_remix.dry[side] += heard;
        }
    }
    if (g_remix.headphones) {
        s32 ears[2];

        port_RemixPlaceRun(voice, sample, volume, ears);
        g_remix.dry[0] += ears[0];
        g_remix.dry[1] += ears[1];
        if (sample != 0) {
            place->quiet = 0;
        } else if (++place->quiet >= PLACE_RING) {
            place->low[0] = 0;
            place->low[1] = 0;
        }
    }
}

/* A side of the disc's sound as a speaker in front, 30 degrees to its
 * side. */
static void port_RemixSpeaker(u32 source, s32 side, s32 sample, s32 *dry) {
    PortRemixPlace *place = &g_remix.places[source];
    s32 ears[2];

    place->step = SPEAKER_STEP;
    place->side = side ^ 1;
    port_RemixHear(place, port_RemixScale(sample, g_remix.speaker_gain), ears);
    dry[0] += ears[0];
    dry[1] += ears[1];
}

static s32 port_RemixClamp(s32 value) {
    if (value > SAMPLE_MOST) {
        return SAMPLE_MOST;
    }
    if (value < SAMPLE_LEAST) {
        return SAMPLE_LEAST;
    }
    return value;
}

/* The limiter: a left and a right in, the pair of LOOK_AHEAD samples ago
 * out, at a level that keeps every sample within 16 bits. Each sample
 * says how far down the level must be for it; the level falls to the
 * least of what the samples waiting say, an eighth of the way at each
 * sample, so that it is there when the loudest comes out, and rises
 * again over about 50 ms. Where the original's sum is cut at 16 bits,
 * here it is turned down for a moment. */
static void port_RemixLimit(s32 left, s32 right, s32 *out) {
    s32 loudest = left < 0 ? -left : left;
    s32 at = (s32)(g_remix.look_at & LOOK_MASK);
    s32 need;
    s32 least;

    if ((right < 0 ? -right : right) > loudest) {
        loudest = right < 0 ? -right : right;
    }
    out[0] = port_RemixScale(g_remix.waiting[at][0], g_remix.level);
    out[1] = port_RemixScale(g_remix.waiting[at][1], g_remix.level);
    g_remix.waiting[at][0] = left;
    g_remix.waiting[at][1] = right;
    need =
        loudest > LIMIT_MOST ? (LIMIT_MOST << GAIN_SHIFT) / loudest : LIMIT_ONE;
    g_remix.needs[at] = need;
    /* the least of the needs waiting: the new one if it is less, or, when
     * the least has just come out, the least of all of them again */
    if (need <= g_remix.least) {
        g_remix.least = need;
        g_remix.least_at = g_remix.look_at;
    } else if (g_remix.look_at - g_remix.least_at >= LOOK_AHEAD) {
        u32 i;

        g_remix.least = LIMIT_ONE;
        g_remix.least_at = g_remix.look_at - (LOOK_AHEAD - 1);
        for (i = 0; i < LOOK_AHEAD; i++) {
            u32 when = g_remix.look_at - (u32)((at - (s32)i) & LOOK_MASK);

            /* of equal ones the newest, which stays the longest */
            if (g_remix.needs[i] < g_remix.least ||
                (g_remix.needs[i] == g_remix.least &&
                 (s32)(when - g_remix.least_at) >= 0)) {
                g_remix.least = g_remix.needs[i];
                g_remix.least_at = when;
            }
        }
    }
    least = g_remix.least;
    g_remix.look_at++;
    if (g_remix.level > least) {
        s32 fall = (g_remix.level - least) >> 3;

        g_remix.level -= fall > 0 ? fall : g_remix.level - least;
        g_remix.counts.limited++;
    } else {
        g_remix.level += ((LIMIT_ONE - g_remix.level) >> LIMIT_RISE) + 1;
        if (g_remix.level > least) {
            g_remix.level = least;
        }
    }
}

/* The next sample (port_remix.h): the disc's sound, the room's reverb
 * (or the chip's out of a fight), the limiter, the main volume. */
void port_RemixFrame(const s32 *cd, const s32 *chip_wet, const s32 *main_volume,
                     s32 unmuted, s16 *out) {
    PortRemixReverb *reverb = g_remix.reverb;
    s32 wet[2] = {0, 0};
    s32 mixed[2];
    s32 side;

    if (g_remix.headphones) {
        PortRemixPlace *disc = &g_remix.places[DISC_LEFT];

        if (cd[0] != 0 || cd[1] != 0) {
            disc->quiet = 0;
        } else if (disc->quiet < PLACE_RING) {
            disc->quiet++;
        }
        if (disc->quiet < PLACE_RING) {
            port_RemixSpeaker(DISC_LEFT, 0, cd[0], g_remix.dry);
            port_RemixSpeaker(DISC_RIGHT, 1, cd[1], g_remix.dry);
        }
    } else {
        g_remix.dry[0] += cd[0];
        g_remix.dry[1] += cd[1];
    }
    if (reverb->room != PORT_REMIX_ROOM_NONE && reverb->tail_left > 0) {
        s32 in[2];

        if (g_remix.room == PORT_REMIX_ROOM_NONE) {
            /* the room left: its tail dies, nothing more goes in */
            in[0] = 0;
            in[1] = 0;
            reverb->tail_left--;
        } else {
            in[0] = port_RemixClamp(g_remix.send[0]);
            in[1] = port_RemixClamp(g_remix.send[1]);
        }
        port_RemixRunReverb(reverb, in[0], in[1], wet);
        wet[0] = port_RemixScale(wet[0], g_port_remix_rooms[reverb->room].wet) /
                 (1 << REVERB_SHIFT);
        wet[1] = port_RemixScale(wet[1], g_port_remix_rooms[reverb->room].wet) /
                 (1 << REVERB_SHIFT);
    }
    if (g_remix.room == PORT_REMIX_ROOM_NONE) {
        /* out of a fight, the chip's own reverb, as the game sets it */
        wet[0] += chip_wet[0];
        wet[1] += chip_wet[1];
    }
    port_RemixLimit(g_remix.dry[0] + wet[0], g_remix.dry[1] + wet[1], mixed);
    for (side = 0; side < 2; side++) {
        s32 sample = mixed[side];

        if (sample != port_RemixClamp(sample)) {
            g_remix.counts.clipped++;
        }
        sample = (port_RemixClamp(sample) * main_volume[side]) >> 15;
        out[side] = (s16)(unmuted ? port_RemixClamp(sample) : 0);
        g_remix.dry[side] = 0;
        g_remix.send[side] = 0;
    }
    g_remix.counts.samples++;
}

/* ---- kept for a rollback ---- */

/* What a sample changes of the mix, but its counts: the room, the sums,
 * the limiter, the voices' copies, the head's rings and the reverb. */
typedef struct {
    u32 kept; /* the setting was on when it was kept */
    s32 room;
    s32 dry[2];
    s32 send[2];
    s32 waiting[LOOK_AHEAD][2];
    s32 needs[LOOK_AHEAD];
    u32 look_at;
    s32 least;
    u32 least_at;
    s32 level;
    PortRemixVoice voices[PORT_SPU_VOICES];
    PortRemixPlace places[PORT_REMIX_SOURCES];
    PortRemixReverb reverb;
} PortRemixKept;

/* each in memory the system gives, on its first keeping (a reverb's
 * lines are a quarter of a megabyte) */
static PortRemixKept *g_remix_kept[PORT_REMIX_KEEPS];

/* (every part is of whole words) */
static void port_RemixCopy(void *to, const void *from, u32 bytes) {
    u32 *out = to;
    const u32 *in = from;
    u32 i;

    for (i = 0; i < bytes / sizeof(u32); i++) {
        out[i] = in[i];
    }
}

void port_RemixKeep(u32 place) {
    PortRemixKept *kept;

    if (place >= PORT_REMIX_KEEPS) {
        return;
    }
    if (!g_remix.is_on) {
        if (g_remix_kept[place] != NULL) {
            g_remix_kept[place]->kept = FALSE;
        }
        return;
    }
    if (g_remix_kept[place] == NULL) {
        g_remix_kept[place] = port_HostAllocate(sizeof(PortRemixKept));
        if (g_remix_kept[place] == NULL) {
            return;
        }
    }
    kept = g_remix_kept[place];
    kept->kept = TRUE;
    kept->room = g_remix.room;
    kept->look_at = g_remix.look_at;
    kept->least = g_remix.least;
    kept->least_at = g_remix.least_at;
    kept->level = g_remix.level;
    port_RemixCopy(kept->dry, g_remix.dry, sizeof(kept->dry));
    port_RemixCopy(kept->send, g_remix.send, sizeof(kept->send));
    port_RemixCopy(kept->waiting, g_remix.waiting, sizeof(kept->waiting));
    port_RemixCopy(kept->needs, g_remix.needs, sizeof(kept->needs));
    port_RemixCopy(kept->voices, g_remix.voices, sizeof(kept->voices));
    port_RemixCopy(kept->places, g_remix.places, sizeof(kept->places));
    port_RemixCopy(&kept->reverb, g_remix.reverb, sizeof(kept->reverb));
}

void port_RemixPutBack(u32 place) {
    const PortRemixKept *kept;

    if (place >= PORT_REMIX_KEEPS || g_remix_kept[place] == NULL ||
        !g_remix_kept[place]->kept || !g_remix.is_on) {
        return;
    }
    kept = g_remix_kept[place];
    g_remix.room = kept->room;
    g_remix.look_at = kept->look_at;
    g_remix.least = kept->least;
    g_remix.least_at = kept->least_at;
    g_remix.level = kept->level;
    port_RemixCopy(g_remix.dry, kept->dry, sizeof(kept->dry));
    port_RemixCopy(g_remix.send, kept->send, sizeof(kept->send));
    port_RemixCopy(g_remix.waiting, kept->waiting, sizeof(kept->waiting));
    port_RemixCopy(g_remix.needs, kept->needs, sizeof(kept->needs));
    port_RemixCopy(g_remix.voices, kept->voices, sizeof(kept->voices));
    port_RemixCopy(g_remix.places, kept->places, sizeof(kept->places));
    port_RemixCopy(g_remix.reverb, &kept->reverb, sizeof(kept->reverb));
}
