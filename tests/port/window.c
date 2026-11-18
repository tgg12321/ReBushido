/* The port's window, without SDL: the picture's place, the sound's queue, the
 * clock, the keys, the log's names and lines, the counter. */
/* The arithmetic of the native port's window (src/port/window/; docs/
 * PORT.md, "The window"), held to cases worked out by hand. Nothing here
 * opens a window or a sound card: the four files ask nothing of SDL or
 * of a C library, and the files that do are not built by any check. */
#include "common.h"
#include "src/port/window/scale.c"
#include "src/port/window/ring.c"
#include "src/port/window/pace.c"
#include "src/port/window/keymap.c"
#include "src/port/window/logname.c"
#include "src/port/window/fps.c"
#include "runtime/runtime.h"

/* ---- where the picture goes ---- */

static void test_CheckPlace(
    s32 window_w, s32 window_h, s32 mode, s32 x, s32 y, s32 w, s32 h) {
    PortRect place;

    port_FitPicture(window_w, window_h, mode, &place);
    CHECK_EQ(place.x, x);
    CHECK_EQ(place.y, y);
    CHECK_EQ(place.w, w);
    CHECK_EQ(place.h, h);
}

static void test_Place(void) {
    /* a window of the picture's shape is filled */
    test_CheckPlace(960, 720, PORT_SCALE_FIT, 0, 0, 960, 720);
    test_CheckPlace(320, 240, PORT_SCALE_FIT, 0, 0, 320, 240);
    /* a wide screen: bars left and right */
    test_CheckPlace(1920, 1080, PORT_SCALE_FIT, 240, 0, 1440, 1080);
    /* a square one: bars above and below */
    test_CheckPlace(1000, 1000, PORT_SCALE_FIT, 0, 125, 1000, 750);
    /* a sliver still has a picture */
    test_CheckPlace(1, 500, PORT_SCALE_FIT, 0, 249, 1, 1);
    test_CheckPlace(500, 1, PORT_SCALE_FIT, 249, 0, 1, 1);
    /* whole multiples of 320 by 240 */
    test_CheckPlace(1920, 1080, PORT_SCALE_INTEGER, 320, 60, 1280, 960);
    test_CheckPlace(960, 720, PORT_SCALE_INTEGER, 0, 0, 960, 720);
    test_CheckPlace(959, 720, PORT_SCALE_INTEGER, 159, 120, 640, 480);
    test_CheckPlace(640, 2000, PORT_SCALE_INTEGER, 0, 760, 640, 480);
    /* too small for the picture once: fitted */
    test_CheckPlace(300, 200, PORT_SCALE_INTEGER, 17, 0, 266, 200);
    /* all of the window */
    test_CheckPlace(1920, 1080, PORT_SCALE_STRETCH, 0, 0, 1920, 1080);
    test_CheckPlace(7, 900, PORT_SCALE_STRETCH, 0, 0, 7, 900);
    /* no window, no picture */
    test_CheckPlace(0, 0, PORT_SCALE_FIT, 0, 0, 0, 0);
    test_CheckPlace(640, 0, PORT_SCALE_INTEGER, 0, 0, 0, 0);
    test_CheckPlace(-5, 480, PORT_SCALE_STRETCH, 0, 0, 0, 0);
}

static void test_CheckShape(s32 window_w, s32 window_h, s32 mode, s32 picture_w,
                            s32 picture_h, s32 x, s32 y, s32 w, s32 h) {
    PortRect place;

    port_FitShape(window_w, window_h, mode, picture_w, picture_h, &place);
    CHECK_EQ(place.x, x);
    CHECK_EQ(place.y, y);
    CHECK_EQ(place.w, w);
    CHECK_EQ(place.h, h);
}

/* The remastered picture's place: its own shape, whatever that is
 * (window.h, port_FitShape). */
static void test_Shape(void) {
    /* four by three, as the console: the same as port_FitPicture's */
    test_CheckShape(1920, 1080, PORT_SCALE_FIT, 1280, 960, 240, 0, 1440, 1080);
    test_CheckShape(1000, 1000, PORT_SCALE_FIT, 640, 480, 0, 125, 1000, 750);
    /* 16 by 9: a wide screen is nearly filled (the picture is 1704 by
     * 960, a hair narrower than 16 by 9, so a pixel is left each side),
     * and a four-by-three window gets bars above and below */
    test_CheckShape(1920, 1080, PORT_SCALE_FIT, 1704, 960, 1, 0, 1917, 1080);
    test_CheckShape(960, 720, PORT_SCALE_FIT, 852, 480, 0, 90, 960, 540);
    /* a picture 16 by 10 in a window 16 by 9: bars left and right */
    test_CheckShape(1600, 900, PORT_SCALE_FIT, 1600, 1000, 80, 0, 1440, 900);
    /* the whole multiple has no meaning for it: `integer` is `fit` */
    test_CheckShape(960, 720, PORT_SCALE_INTEGER, 852, 480, 0, 90, 960, 540);
    /* all of the window */
    test_CheckShape(1920, 1080, PORT_SCALE_STRETCH, 852, 480, 0, 0, 1920, 1080);
    /* a sliver still has a picture */
    test_CheckShape(1, 500, PORT_SCALE_FIT, 852, 480, 0, 249, 1, 1);
    /* no window, or no picture, no place */
    test_CheckShape(0, 720, PORT_SCALE_FIT, 852, 480, 0, 0, 0, 0);
    test_CheckShape(960, 720, PORT_SCALE_FIT, 0, 480, 0, 0, 0, 0);
    test_CheckShape(960, 720, PORT_SCALE_FIT, 852, -1, 0, 0, 0, 0);
    /* how many times the console's lines a scale draws, so that the
     * remastered picture's pixels are square (port_TallFor) */
    CHECK_EQ(port_TallFor(1, 640, 240), 2);
    CHECK_EQ(port_TallFor(3, 640, 240), 6);
    CHECK_EQ(port_TallFor(1, 320, 240), 1);
    CHECK_EQ(port_TallFor(4, 320, 240), 4);
    /* 640 by 480 has square pixels of its own: the scale itself */
    CHECK_EQ(port_TallFor(2, 640, 480), 2);
    /* a 256-wide mode has no whole number: the nearest */
    CHECK_EQ(port_TallFor(5, 256, 240), 4);
    /* and nothing sensible is still something */
    CHECK_EQ(port_TallFor(0, 640, 240), 1);
    CHECK_EQ(port_TallFor(3, 0, 240), 3);
    CHECK_EQ(port_TallFor(3, 640, 0), 3);
}

static u16 g_test_vram[1024 * 512];

static void test_Pixels(void) {
    u32 out[8];
    u8 *bytes;

    /* five bits to eight: 0 to 0, 31 to 255, and between them by the
     * rule of the port's PNGs, n * 255 / 31 rounded down */
    CHECK_EQ(port_ConvertPixel(0x0000), 0xFF000000);
    CHECK_EQ(port_ConvertPixel(0x7FFF), 0xFFFFFFFF);
    CHECK_EQ(port_ConvertPixel(0x001F), 0xFFFF0000);
    CHECK_EQ(port_ConvertPixel(0x03E0), 0xFF00FF00);
    CHECK_EQ(port_ConvertPixel(0x7C00), 0xFF0000FF);
    CHECK_EQ(port_ConvertPixel(0x0010), 0xFF830000); /* 16: 131 */
    CHECK_EQ(port_ConvertPixel(0x0001), 0xFF080000); /* 1: 8 */
    CHECK_EQ(port_ConvertPixel(1 << 5 | 30 << 10), 0xFF0008F6);
    /* the mask bit is no colour */
    CHECK_EQ(port_ConvertPixel(0x8000), 0xFF000000);
    CHECK_EQ(port_ConvertPixel(0xFFFF), 0xFFFFFFFF);

    /* a rectangle that runs off the right edge and the bottom goes on
     * at the left and the top */
    g_test_vram[511 * 1024 + 1022] = 0x001F;
    g_test_vram[511 * 1024 + 1023] = 0x03E0;
    g_test_vram[511 * 1024 + 0] = 0x7C00;
    g_test_vram[511 * 1024 + 1] = 0x7FFF;
    g_test_vram[0 * 1024 + 1022] = 0x0001;
    g_test_vram[0 * 1024 + 1023] = 0x0010;
    g_test_vram[0 * 1024 + 0] = 0x8000;
    g_test_vram[0 * 1024 + 1] = 0x0421;
    port_ConvertDisplay(g_test_vram, 1022, 511, 4, 2, 0, out);
    CHECK_EQ(out[0], 0xFFFF0000);
    CHECK_EQ(out[1], 0xFF00FF00);
    CHECK_EQ(out[2], 0xFF0000FF);
    CHECK_EQ(out[3], 0xFFFFFFFF);
    CHECK_EQ(out[4], 0xFF080000);
    CHECK_EQ(out[5], 0xFF830000);
    CHECK_EQ(out[6], 0xFF000000);
    CHECK_EQ(out[7], 0xFF080808);

    /* three bytes a pixel, the red first, from the byte the display
     * starts at */
    bytes = (u8 *)&g_test_vram[7 * 1024];
    bytes[20] = 0x11;
    bytes[21] = 0x22;
    bytes[22] = 0x33;
    bytes[23] = 0x44;
    bytes[24] = 0x55;
    bytes[25] = 0x66;
    port_ConvertDisplay(g_test_vram, 10, 7, 2, 1, 1, out);
    CHECK_EQ(out[0], 0xFF112233);
    CHECK_EQ(out[1], 0xFF445566);
}

/* ---- the sound's queue ---- */

#define TEST_RING_FRAMES 1024
#define TEST_RING_TARGET 300
#define TEST_LEVEL 12800 /* 100 for each step of a fade */

static s16 g_test_ring_samples[TEST_RING_FRAMES * 2];
static s16 g_test_sound[4096 * 2];
static s16 g_test_heard[4096 * 2];

static void test_FillSound(s32 left, s32 right, u32 frames) {
    u32 i;

    for (i = 0; i < frames; i++) {
        g_test_sound[i * 2] = (s16)left;
        g_test_sound[i * 2 + 1] = (s16)right;
    }
}

/* The largest step between two pairs that follow each other in what was
 * heard, counting from `before`, the pair heard before them. */
static s32 test_LargestStep(s32 before, u32 frames) {
    s32 largest = 0;
    u32 i;

    for (i = 0; i < frames; i++) {
        s32 step = g_test_heard[i * 2] - before;

        if (step < 0) {
            step = -step;
        }
        if (step > largest) {
            largest = step;
        }
        before = g_test_heard[i * 2];
    }
    return largest;
}

static void test_Ring(void) {
    PortRing ring;
    u32 i;
    s32 wrong;

    port_InitRing(
        &ring, g_test_ring_samples, TEST_RING_FRAMES, TEST_RING_TARGET);
    /* nothing yet: silence, and that is no running out */
    port_ReadRing(&ring, g_test_heard, 50);
    CHECK_EQ(g_test_heard[0], 0);
    CHECK_EQ(g_test_heard[99], 0);
    CHECK_EQ(ring.underruns, 0);
    /* not enough yet: still silence, and nothing taken */
    test_FillSound(TEST_LEVEL, -TEST_LEVEL, 299);
    port_WriteRing(&ring, g_test_sound, 299);
    port_ReadRing(&ring, g_test_heard, 10);
    CHECK_EQ(g_test_heard[0], 0);
    CHECK_EQ(ring.count, 299);
    /* enough: it fades in over PORT_RING_FADE samples, then is itself */
    port_WriteRing(&ring, g_test_sound, 1);
    port_ReadRing(&ring, g_test_heard, 200);
    CHECK_EQ(g_test_heard[0], 100);
    CHECK_EQ(g_test_heard[1], -100);
    CHECK_EQ(g_test_heard[2], 200);
    CHECK_EQ(g_test_heard[126 * 2], 12700);
    CHECK_EQ(g_test_heard[127 * 2], TEST_LEVEL);
    CHECK_EQ(g_test_heard[199 * 2], TEST_LEVEL);
    CHECK_EQ(g_test_heard[199 * 2 + 1], -TEST_LEVEL);
    CHECK_EQ(ring.count, 100);
    /* it runs out: the last pair fades away, then silence, and no step
     * is larger than a fade's */
    port_ReadRing(&ring, g_test_heard, 400);
    CHECK_EQ(ring.underruns, 1);
    CHECK_EQ(g_test_heard[99 * 2], TEST_LEVEL);
    CHECK_EQ(g_test_heard[100 * 2], 12700);
    CHECK_EQ(g_test_heard[101 * 2 + 1], -12600);
    CHECK_EQ(g_test_heard[227 * 2], 0);
    CHECK_EQ(g_test_heard[399 * 2], 0);
    CHECK_EQ(test_LargestStep(TEST_LEVEL, 400), 100);
    /* a little comes: not played until there is enough again, so one
     * stall is one silence */
    port_WriteRing(&ring, g_test_sound, 100);
    port_ReadRing(&ring, g_test_heard, 100);
    CHECK_EQ(g_test_heard[0], 0);
    CHECK_EQ(g_test_heard[199], 0);
    CHECK_EQ(ring.count, 100);
    CHECK_EQ(ring.underruns, 1);
    port_WriteRing(&ring, g_test_sound, 200);
    port_ReadRing(&ring, g_test_heard, 300);
    CHECK_EQ(g_test_heard[0], 100);
    CHECK_EQ(g_test_heard[299 * 2], TEST_LEVEL);
    CHECK_EQ(test_LargestStep(0, 300), 100);
    CHECK_EQ(ring.count, 0);
    CHECK_EQ(ring.dropped, 0);

    /* what is written comes out in its order, round the ring's end many
     * times */
    port_InitRing(&ring, g_test_ring_samples, 16, 1);
    ring.fade = PORT_RING_FADE; /* as after the fade in */
    wrong = 0;
    for (i = 0; i < 400; i++) {
        s16 pair[2];
        s16 heard[4];

        pair[0] = (s16)(i * 3 + 1);
        pair[1] = (s16) - (s32)i;
        port_WriteRing(&ring, pair, 1);
        pair[0]++;
        port_WriteRing(&ring, pair, 1);
        port_ReadRing(&ring, heard, 2);
        wrong += heard[0] != (s16)(i * 3 + 1) || heard[1] != (s16) - (s32)i ||
                 heard[2] != (s16)(i * 3 + 2) || heard[3] != (s16) - (s32)i;
    }
    CHECK_EQ(wrong, 0);
    CHECK_EQ(ring.dropped, 0);
    CHECK_EQ(ring.underruns, 0);

    /* no room: the oldest go, the newest stay, in order */
    port_InitRing(&ring, g_test_ring_samples, 16, 1);
    for (i = 0; i < 20; i++) {
        g_test_sound[i * 2] = (s16)(1000 + i);
        g_test_sound[i * 2 + 1] = (s16)(2000 + i);
    }
    port_WriteRing(&ring, g_test_sound, 20);
    CHECK_EQ(ring.count, 16);
    CHECK_EQ(ring.dropped, 4);
    port_WriteRing(&ring, g_test_sound, 10);
    CHECK_EQ(ring.count, 16);
    CHECK_EQ(ring.dropped, 14);
    ring.fade = PORT_RING_FADE; /* as after the fade in */
    port_ReadRing(&ring, g_test_heard, 16);
    /* the last six of the first twenty, then the ten */
    CHECK_EQ(g_test_heard[0], 1014);
    CHECK_EQ(g_test_heard[1], 2014);
    CHECK_EQ(g_test_heard[5 * 2], 1019);
    CHECK_EQ(g_test_heard[6 * 2], 1000);
    CHECK_EQ(g_test_heard[15 * 2], 1009);
    CHECK_EQ(g_test_heard[15 * 2 + 1], 2009);
    CHECK_EQ(ring.count, 0);

    /* netplay's rollback takes back the newest pairs waiting, never more
     * than wait, and what is written next follows what is left */
    for (i = 0; i < 12; i++) {
        g_test_sound[i * 2] = (s16)(3000 + i);
        g_test_sound[i * 2 + 1] = (s16)(4000 + i);
    }
    port_WriteRing(&ring, g_test_sound, 12);
    CHECK_EQ(port_TakeBackRing(&ring, 5), 5);
    CHECK_EQ(ring.count, 7);
    g_test_sound[0] = 5000;
    port_WriteRing(&ring, g_test_sound, 1);
    port_ReadRing(&ring, g_test_heard, 8);
    CHECK_EQ(g_test_heard[6 * 2], 3006);
    CHECK_EQ(g_test_heard[7 * 2], 5000);
    CHECK_EQ(port_TakeBackRing(&ring, 3), 0);
    CHECK_EQ(ring.dropped, 14);
}

/* ---- the clock ---- */

/* A blank's samples, as sound.c counts them: 737 and 341/2500. */
#define TEST_BLANK_FRAMES 737
#define TEST_BLANK_FRACTION 341
#define TEST_BLANK_FRACTION_ONE 2500
#define TEST_CARD_FRAMES 1024

/* Many blanks with a sound card whose clock is `card_rate` parts in
 * 100,000 of the PC's, which takes its samples 1024 at a time. Returns
 * the times the sound ran out after it first began; `nudged_out` is how
 * far the blanks were moved in all. */
static u32 test_RunWithCard(s32 card_rate, u32 blanks, s32 *nudged_out) {
    static s16 ring_samples[8192 * 2];
    PortRing ring;
    PortPace pace;
    u32 now = 5000; /* the PC's clock, in samples */
    u32 due = 0;    /* the game's time */
    u32 fraction = 0;
    u32 card_clock = 0; /* the card's own clock, in samples */
    u32 card_part = 0;  /* ... and its 100,000ths */
    u32 card_taken = 0; /* samples the card has asked for */
    u32 blank;
    u32 start_underruns = 0;

    port_InitRing(&ring, ring_samples, 8192, 2560);
    pace.is_started = 0;
    test_FillSound(1000, 1000, 1024);
    for (blank = 1; blank <= blanks; blank++) {
        u32 frames = TEST_BLANK_FRAMES;
        u32 before = now;

        fraction += TEST_BLANK_FRACTION;
        if (fraction >= TEST_BLANK_FRACTION_ONE) {
            fraction -= TEST_BLANK_FRACTION_ONE;
            frames++;
        }
        due += frames;
        port_WriteRing(&ring, g_test_sound, frames);
        now += 40; /* what the PC took to work the blank out */
        now += port_PaceBlank(
            &pace, blank, now, due, (s32)ring.count - (s32)ring.target);
        /* what the card asked for up to now */
        card_part += (now - before) * (u32)card_rate;
        card_clock += card_part / 100000;
        card_part %= 100000;
        while (card_taken + TEST_CARD_FRAMES <= card_clock) {
            port_ReadRing(&ring, g_test_heard, TEST_CARD_FRAMES);
            card_taken += TEST_CARD_FRAMES;
        }
        if (blank == 100) {
            start_underruns = ring.underruns;
        }
    }
    *nudged_out = pace.nudged;
    CHECK_EQ(ring.dropped, 0);
    CHECK_EQ(pace.lost, 0);
    return ring.underruns - start_underruns;
}

/* The window's queue (sdl_window.c: 3584 pairs, the card asking for 512
 * at a time) through `blanks` blanks of a fight, in which a frame's
 * blank takes `heavy` samples' time to work out and the blank after it
 * 2 ms, and every 300th blank stalls `stall` more. The card asks for its
 * samples by the PC's clock, a gulp whenever a gulp's time has passed,
 * so it reads while the game works a blank out. Returns the times the
 * sound ran out after it first began; `late_out` the largest lateness
 * the pace saw, in samples. */
static u32 test_RunWithStalls(
    u32 target, u32 gulp, u32 heavy, u32 stall, u32 blanks, u32 *late_out) {
    static s16 ring_samples[8192 * 2];
    PortRing ring;
    PortPace pace;
    u32 now = 5000;
    u32 due = 0;
    u32 fraction = 0;
    u32 card_taken = 5000;
    u32 blank;
    u32 begun = 0;
    u32 late = 0;

    port_InitRing(&ring, ring_samples, 8192, target);
    pace.is_started = 0;
    test_FillSound(1000, 1000, 1024);
    for (blank = 1; blank <= blanks; blank++) {
        u32 frames = TEST_BLANK_FRAMES;
        u32 step = (blank & 1) ? heavy : 88;
        u32 until;

        if (blank % 300 == 0) {
            step += stall;
        }
        /* the step: the card goes on reading while the game works */
        until = now + step;
        while ((s32)(until - (card_taken + gulp)) >= 0) {
            port_ReadRing(&ring, g_test_heard, gulp);
            card_taken += gulp;
        }
        now = until;
        /* the blank's samples, at its end */
        fraction += TEST_BLANK_FRACTION;
        if (fraction >= TEST_BLANK_FRACTION_ONE) {
            fraction -= TEST_BLANK_FRACTION_ONE;
            frames++;
        }
        due += frames;
        port_WriteRing(&ring, g_test_sound, frames);
        until = now + port_PaceBlank(&pace, blank, now, due,
                                     (s32)ring.count - (s32)ring.target);
        late = pace.late > late ? pace.late : late;
        while ((s32)(until - (card_taken + gulp)) >= 0) {
            port_ReadRing(&ring, g_test_heard, gulp);
            card_taken += gulp;
        }
        now = until;
        if (blank == 60) {
            begun = ring.underruns;
        }
    }
    *late_out = late;
    CHECK_EQ(pace.lost, 0);
    return ring.underruns - begun;
}

static void test_Pace(void) {
    PortPace pace;
    u32 blank;
    u32 now;
    s32 nudged;

    /* the first blank is the start: no wait; then each is waited for
     * until the clock has gone as far as the game's time */
    pace.is_started = 0;
    CHECK_EQ(port_PaceBlank(&pace, 1, 1000000, 737, 0), 0);
    CHECK_EQ(port_PaceBlank(&pace, 2, 1000100, 737 + 737, 0), 637);
    CHECK_EQ(port_PaceBlank(&pace, 3, 1000737 + 737, 737 * 3, 0), 0);
    /* late, by less than a tenth of a second: no wait, and the time is
     * not let go: the next blank is still due by the start's clock */
    CHECK_EQ(port_PaceBlank(&pace, 4, 1000000 + 737 * 3 + 4000, 737 * 4, 0), 0);
    CHECK_EQ(pace.lost, 0);
    CHECK_EQ(port_PaceBlank(&pace, 5, 1000000 + 737 * 4 - 10, 737 * 5, 0), 10);
    /* later than that: the time is let go, and the clock starts from
     * here */
    CHECK_EQ(port_PaceBlank(&pace, 6, 2000000, 737 * 6, 0), 0);
    CHECK_EQ(pace.lost, 1);
    CHECK_EQ(port_PaceBlank(&pace, 7, 2000000 + 37, 737 * 7, 0), 700);
    /* a quick load: a hundred blanks of the game's time in no time of
     * the PC's, rebased, are neither waited for nor lost time */
    port_PaceRebase(&pace, 2000000 + 737 + 40, 737 * 107);
    CHECK_EQ(port_PaceBlank(&pace, 108, 2000000 + 737 + 40, 737 * 108, 0), 737);
    CHECK_EQ(pace.lost, 1);
    /* a clock that jumped back is let go as well */
    CHECK_EQ(port_PaceBlank(&pace, 8, 1500000, 737 * 8, 0), 0);
    CHECK_EQ(pace.lost, 2);
    CHECK(!port_PaceIsHeldBack(&pace));
    CHECK_EQ(port_PaceBlank(&pace, 9, 3000000, 737 * 9, 0), 0);
    CHECK_EQ(pace.lost, 3);
    /* three times within PORT_PACE_LOST_WINDOW blanks: held back */
    CHECK(port_PaceIsHeldBack(&pace));
    /* ... but not three times far apart */
    pace.is_started = 0;
    CHECK(!port_PaceIsHeldBack(&pace));
    now = 0;
    for (blank = 1; blank <= 3 * 2000 + 1; blank++) {
        if (blank % 2000 == 0) {
            now += 50000; /* a stall */
        }
        now += port_PaceBlank(&pace, blank, now, blank * 737, 0);
    }
    CHECK_EQ(pace.lost, 3);
    CHECK(!port_PaceIsHeldBack(&pace));

    /* the clock and the game's time go round at 32 bits together */
    pace.is_started = 0;
    CHECK_EQ(port_PaceBlank(&pace, 1, 0xFFFFFF00U, 0xFFFFFE00U, 0), 0);
    CHECK_EQ(port_PaceBlank(&pace, 2, 0xFFFFFF10U, 0x000000E1U, 0), 737 - 16);
    CHECK_EQ(port_PaceBlank(&pace, 3, 0x00000200U, 0x000003C2U, 0), 0x2C2);
    CHECK_EQ(pace.lost, 0);

    /* netplay's hold: nothing before the start; then each blank is later
     * by a quarter of a blank's time at most, until all of it is taken,
     * and no time is let go */
    pace.is_started = 0;
    port_PaceHold(&pace, 1000);
    CHECK_EQ(port_PaceBlank(&pace, 1, 737, 737, 0), 0);
    CHECK_EQ(pace.held, 0);
    port_PaceHold(&pace, 400);
    CHECK_EQ(
        port_PaceBlank(&pace, 2, 737 * 2, 737 * 2, 0), PORT_PACE_HOLD_STEP);
    CHECK_EQ(port_PaceBlank(&pace, 3, 737 * 3 + 184, 737 * 3, 0), 184);
    CHECK_EQ(port_PaceBlank(&pace, 4, 737 * 4 + 368, 737 * 4, 0), 32);
    CHECK_EQ(port_PaceBlank(&pace, 5, 737 * 5 + 400, 737 * 5, 0), 0);
    CHECK_EQ(pace.held, 0);
    CHECK_EQ(pace.lost, 0);

    /* a queue at its length, or within PORT_PACE_SLACK of it, moves
     * nothing */
    pace.is_started = 0;
    for (blank = 1; blank <= 500; blank++) {
        port_PaceBlank(&pace, blank, blank * 737, blank * 737,
                       (blank & 1) ? PORT_PACE_SLACK : -PORT_PACE_SLACK);
    }
    CHECK_EQ(pace.nudged, 0);
    /* a queue that stays too long makes each blank later, by no more
     * than PORT_PACE_NUDGE_MAX; too short, sooner */
    pace.is_started = 0;
    now = 0;
    for (blank = 1; blank <= 400; blank++) {
        u32 wait = port_PaceBlank(&pace, blank, now, blank * 737, 5000);

        if (blank > 300) {
            CHECK_EQ(wait, 737 + PORT_PACE_NUDGE_MAX);
        }
        now += wait;
    }
    CHECK(pace.nudged > 300 * PORT_PACE_NUDGE_MAX);
    CHECK(pace.nudged <= 400 * PORT_PACE_NUDGE_MAX);
    pace.is_started = 0;
    now = 0;
    for (blank = 1; blank <= 400; blank++) {
        u32 wait = port_PaceBlank(&pace, blank, now, blank * 737, -5000);

        if (blank > 300) {
            CHECK_EQ(wait, 737 - PORT_PACE_NUDGE_MAX);
        }
        now += wait;
    }
    CHECK(pace.nudged < -300 * PORT_PACE_NUDGE_MAX);

    /* a sound card that keeps the PC's time: nothing runs out, and the
     * blanks are hardly moved */
    CHECK_EQ(test_RunWithCard(100000, 20000, &nudged), 0);
    CHECK(nudged > -3000 && nudged < 3000);
    /* one whose clock is two parts in a thousand fast, and one as slow
     * (a real one is off by far less): the blanks follow it, about
     * 20000 * 737 * 0.002 = 29,480 samples in all, and nothing runs
     * out or is thrown away */
    CHECK_EQ(test_RunWithCard(100200, 20000, &nudged), 0);
    CHECK(nudged < -26000 && nudged > -33000);
    CHECK_EQ(test_RunWithCard(99800, 20000, &nudged), 0);
    CHECK(nudged > 26000 && nudged < 33000);

    /* the window's queue holds a fight whose frame's blank takes 20 ms
     * to work out, with a stall of 50 ms more every five seconds: nothing
     * runs out and no time is let go (docs/PORT.md, "Sound that does not
     * run out") */
    {
        u32 late = 0;

        CHECK_EQ(test_RunWithStalls(3584, 512, 882, 2205, 6000, &late), 0);
        /* (the stall, less the blank's own time it fell in) */
        CHECK(late > 2205 - 737);
        /* ... and with the step at a whole blank and more, 27 ms, which
         * the light blank after it makes up */
        CHECK_EQ(test_RunWithStalls(3584, 512, 1190, 2205, 6000, &late), 0);
        /* a stall of 70 ms is more than the queue holds: it runs out once
         * at each, but the last, at the run's last blank */
        CHECK_EQ(test_RunWithStalls(3584, 512, 882, 3087, 6000, &late),
                 6000 / 300 - 1);
        /* the queue as it was (2560, the card asking for 1024 at a time)
         * ran out at every stall of 50 ms */
        CHECK_EQ(test_RunWithStalls(2560, 1024, 882, 2205, 6000, &late),
                 6000 / 300 - 1);
    }
    /* how late the last blank was, for the window (sdl_window.c) */
    pace.is_started = 0;
    port_PaceBlank(&pace, 1, 1000, 737, 0);
    port_PaceBlank(&pace, 2, 1000 + 737 + 300, 737 * 2, 0);
    CHECK_EQ(pace.late, 300);
    port_PaceBlank(&pace, 3, 1000 + 737 * 2, 737 * 3, 0);
    CHECK_EQ(pace.late, 0);
}

/* When a picture is shown, and of what moment (pace.c). */
static void test_Refresh(void) {
    PortRefresh refresh = {0, 0, 0};
    u32 average;
    s32 i;

    /* refreshes not known: ready is when it is shown */
    CHECK_EQ(port_NextRefresh(&refresh, 1000, 50), 1050);
    /* a display of 85 a second: 518 samples a refresh, the last swap
     * came back at 10000 */
    refresh.period = 518;
    port_NoteSwap(&refresh, 10000);
    CHECK_EQ(port_NextRefresh(&refresh, 10000, 50), 10518);
    CHECK_EQ(port_NextRefresh(&refresh, 10400, 100), 10518);
    CHECK_EQ(port_NextRefresh(&refresh, 10400, 200), 11036);
    /* ready before the refresh the last swap waited for: the next */
    CHECK_EQ(port_NextRefresh(&refresh, 9900, 50), 10518);
    /* round the clock's 32 bits */
    port_NoteSwap(&refresh, 0xFFFFFF00U);
    CHECK_EQ(port_NextRefresh(&refresh, 0xFFFFFF00U, 300), 0x00000106U);

    /* the phase: a fight's two blanks, 1474 samples, from 5000 */
    CHECK_EQ(port_PacePhase(5000, 5000, 0, 1474, 4096), 0);
    CHECK_EQ(port_PacePhase(5737, 5000, 0, 1474, 4096), 2048);
    CHECK_EQ(port_PacePhase(6474, 5000, 0, 1474, 4096), 4096);
    /* shown before its frame's moment, or after the next: the ends */
    CHECK_EQ(port_PacePhase(4000, 5000, 0, 1474, 4096), 0);
    CHECK_EQ(port_PacePhase(9000, 5000, 0, 1474, 4096), 4096);
    /* every picture shown 300 after its moment */
    CHECK_EQ(port_PacePhase(6037, 5000, 300, 1474, 4096), 2048);
    /* no period known */
    CHECK_EQ(port_PacePhase(6000, 5000, 0, 0, 4096), 0);

    /* an average goes an eighth of the way, up or down */
    CHECK_EQ(port_PaceAverage(800, 1600), 900);
    CHECK_EQ(port_PaceAverage(800, 0), 700);
    average = 0;
    for (i = 0; i < 200; i++) {
        average = port_PaceAverage(average, 1000);
    }
    CHECK(average > 990 && average <= 1000);
}

/* ---- keys and controllers ---- */

typedef struct {
    s32 source;
    const char *name;
    s32 code;
} TestName;

/* Names as the window's library might have them, with numbers made up
 * here. */
static const TestName g_test_names[] = {
    {PORT_SOURCE_KEY, "Up", 82},
    {PORT_SOURCE_KEY, "Down", 81},
    {PORT_SOURCE_KEY, "Left", 80},
    {PORT_SOURCE_KEY, "Right", 79},
    {PORT_SOURCE_KEY, "Z", 29},
    {PORT_SOURCE_KEY, "X", 27},
    {PORT_SOURCE_KEY, "A", 4},
    {PORT_SOURCE_KEY, "S", 22},
    {PORT_SOURCE_KEY, "Q", 20},
    {PORT_SOURCE_KEY, "W", 26},
    {PORT_SOURCE_KEY, "1", 30},
    {PORT_SOURCE_KEY, "2", 31},
    {PORT_SOURCE_KEY, "Return", 40},
    /* the window's own, as sdl_window.c's port_LookUpName has it */
    {PORT_SOURCE_KEY, "Escape", PORT_NAME_RESERVED},
    {PORT_SOURCE_KEY, "Right Shift", 229},
    {PORT_SOURCE_KEY, "I", 12},
    {PORT_SOURCE_KEY, "K", 14},
    {PORT_SOURCE_KEY, "J", 13},
    {PORT_SOURCE_KEY, "L", 15},
    {PORT_SOURCE_KEY, "G", 10},
    {PORT_SOURCE_KEY, "H", 11},
    {PORT_SOURCE_KEY, "T", 23},
    {PORT_SOURCE_KEY, "Y", 28},
    {PORT_SOURCE_KEY, "R", 21},
    {PORT_SOURCE_KEY, "U", 24},
    {PORT_SOURCE_KEY, "5", 34},
    {PORT_SOURCE_KEY, "6", 35},
    {PORT_SOURCE_KEY, "P", 19},
    {PORT_SOURCE_KEY, "O", 18},
    {PORT_SOURCE_KEY, "Space", 44},
    {PORT_SOURCE_KEY, "Keypad 4", 92},
    {PORT_SOURCE_BUTTON, "a", 0},
    {PORT_SOURCE_BUTTON, "b", 1},
    {PORT_SOURCE_BUTTON, "x", 2},
    {PORT_SOURCE_BUTTON, "y", 3},
    {PORT_SOURCE_BUTTON, "back", 4},
    {PORT_SOURCE_BUTTON, "start", 6},
    {PORT_SOURCE_BUTTON, "leftshoulder", 9},
    {PORT_SOURCE_BUTTON, "rightshoulder", 10},
    {PORT_SOURCE_BUTTON, "dpup", 11},
    {PORT_SOURCE_BUTTON, "dpdown", 12},
    {PORT_SOURCE_BUTTON, "dpleft", 13},
    {PORT_SOURCE_BUTTON, "dpright", 14},
    {PORT_SOURCE_AXIS, "leftx", 0},
    {PORT_SOURCE_AXIS, "lefty", 1},
    {PORT_SOURCE_AXIS, "lefttrigger", 4},
    {PORT_SOURCE_AXIS, "righttrigger", 5},
};

static s32 test_LookUp(s32 source, const char *name) {
    u32 i;

    for (i = 0; i < sizeof(g_test_names) / sizeof(g_test_names[0]); i++) {
        if (g_test_names[i].source == source &&
            test_StringsEqual(g_test_names[i].name, name)) {
            return g_test_names[i].code;
        }
    }
    return -1;
}

/* A library that knows no controller at all. */
static s32 test_LookUpKeysOnly(s32 source, const char *name) {
    return source == PORT_SOURCE_KEY ? test_LookUp(source, name) : -1;
}

static s32 g_test_complaints;
static s32 g_test_complaint_lines[8];

static void test_Complain(s32 line, const char *why, const char *text) {
    (void)why;
    (void)text;
    if (g_test_complaints < 8) {
        g_test_complaint_lines[g_test_complaints] = line;
    }
    g_test_complaints++;
}

static PortSettings g_test_settings;

/* How many keys (1) or controller buttons and axes (0) press a button. */
static s32 test_CountBindings(s32 keys, s32 pad, u32 button) {
    s32 count = 0;
    s32 i;

    for (i = 0; i < g_test_settings.count; i++) {
        const PortBinding *binding = &g_test_settings.bindings[i];

        count += binding->pad == pad && binding->button == button &&
                 (binding->source == PORT_SOURCE_KEY) == keys;
    }
    return count;
}

static void test_ReadText(const char *text) {
    static char copy[2048];
    u32 i;

    for (i = 0; text[i] != 0; i++) {
        copy[i] = text[i];
    }
    copy[i] = 0;
    g_test_complaints = 0;
    port_ReadSettings(&g_test_settings, copy, test_LookUp, test_Complain);
}

static void test_Keys(void) {
    PortSettings *settings = &g_test_settings;
    const PortBinding *binding;

    /* the buttons' names are Sony's masks */
    CHECK_EQ(port_FindPadButton("cross"), PADRdown);
    CHECK_EQ(port_FindPadButton("Circle"), PADRright);
    CHECK_EQ(port_FindPadButton("SQUARE"), PADRleft);
    CHECK_EQ(port_FindPadButton("triangle"), PADRup);
    CHECK_EQ(port_FindPadButton("up"), PADLup);
    CHECK_EQ(port_FindPadButton("down"), PADLdown);
    CHECK_EQ(port_FindPadButton("left"), PADLleft);
    CHECK_EQ(port_FindPadButton("right"), PADLright);
    CHECK_EQ(port_FindPadButton("l1"), PADL1);
    CHECK_EQ(port_FindPadButton("l2"), PADL2);
    CHECK_EQ(port_FindPadButton("r1"), PADR1);
    CHECK_EQ(port_FindPadButton("r2"), PADR2);
    CHECK_EQ(port_FindPadButton("start"), PADstart);
    CHECK_EQ(port_FindPadButton("select"), PADselect);
    CHECK_EQ(port_FindPadButton("l3"), 0);
    CHECK_EQ(port_FindPadButton(""), 0);
    CHECK(test_StringsEqual(port_NamePadButton(PADstart), "start"));
    CHECK(port_NamePadButton(PADstart | PADselect) == NULL);

    /* the layout built in: every button of both pads has one key, and
     * one button of a controller, a direction a stick as well */
    port_SetDefaultSettings(settings, test_LookUp);
    CHECK_EQ(settings->count, 2 * (14 + 18));
    CHECK_EQ(test_CountBindings(1, 0, PADRdown), 1);
    CHECK_EQ(test_CountBindings(0, 0, PADRdown), 1);
    CHECK_EQ(test_CountBindings(1, 1, PADLup), 1);
    CHECK_EQ(test_CountBindings(0, 1, PADLup), 2);
    CHECK_EQ(settings->scale, PORT_SCALE_FIT);
    CHECK_EQ(settings->is_fullscreen, 1);
    CHECK_EQ(settings->is_linear, 0);
    CHECK_EQ(settings->vsync, 1);
    CHECK_EQ(settings->deadzone, PORT_DEADZONE_DEFAULT);
    binding = port_FindBinding(settings, 1, 0, PADRdown);
    CHECK(binding != NULL && binding->code == 29 && binding->is_default);
    binding = port_FindBinding(settings, 0, 1, PADstart);
    CHECK(binding != NULL && binding->source == PORT_SOURCE_BUTTON &&
          binding->code == 6);
    binding = port_FindBinding(settings, 0, 0, PADL2);
    CHECK(binding != NULL && binding->source == PORT_SOURCE_AXIS &&
          binding->code == 4 && binding->direction == 1);
    CHECK(port_FindBinding(settings, 1, 0, PADRdown | PADRup) == NULL);
    /* names the library does not know are left out, and the rest
     * stays */
    port_SetDefaultSettings(settings, test_LookUpKeysOnly);
    CHECK_EQ(settings->count, 2 * 14);

    /* a key held is the button held; let go, it is not */
    port_SetDefaultSettings(settings, test_LookUp);
    CHECK_EQ(port_GetHeldButtons(settings, 0), 0);
    port_ApplyInput(settings, PORT_SOURCE_KEY, 0, 29, 1);
    port_ApplyInput(settings, PORT_SOURCE_KEY, 0, 82, 1);
    CHECK_EQ(port_GetHeldButtons(settings, 0), PADRdown | PADLup);
    CHECK_EQ(port_GetHeldButtons(settings, 1), 0);
    /* the second pad's keys are on the same keyboard, whatever pad the
     * caller names */
    port_ApplyInput(settings, PORT_SOURCE_KEY, 0, 10, 1);
    CHECK_EQ(port_GetHeldButtons(settings, 1), PADRdown);
    port_ApplyInput(settings, PORT_SOURCE_KEY, 0, 29, 0);
    CHECK_EQ(port_GetHeldButtons(settings, 0), PADLup);
    /* a key that is nobody's does nothing */
    port_ApplyInput(settings, PORT_SOURCE_KEY, 0, 44, 1);
    CHECK_EQ(port_GetHeldButtons(settings, 0), PADLup);
    /* a controller's button is its own pad's only */
    port_ApplyInput(settings, PORT_SOURCE_BUTTON, 1, 1, 1);
    CHECK_EQ(port_GetHeldButtons(settings, 1), PADRdown | PADRright);
    CHECK_EQ(port_GetHeldButtons(settings, 0), PADLup);
    /* a key and a controller's button for one button: held while
     * either is */
    port_ApplyInput(settings, PORT_SOURCE_BUTTON, 0, 11, 1);
    port_ApplyInput(settings, PORT_SOURCE_KEY, 0, 82, 0);
    CHECK_EQ(port_GetHeldButtons(settings, 0), PADLup);
    port_ApplyInput(settings, PORT_SOURCE_BUTTON, 0, 11, 0);
    CHECK_EQ(port_GetHeldButtons(settings, 0), 0);
    /* a stick counts from the dead zone on: 40 parts in 100 of 32767
     * is 13106 */
    port_ApplyInput(settings, PORT_SOURCE_AXIS, 0, 0, 13105);
    CHECK_EQ(port_GetHeldButtons(settings, 0), 0);
    port_ApplyInput(settings, PORT_SOURCE_AXIS, 0, 0, 13106);
    CHECK_EQ(port_GetHeldButtons(settings, 0), PADLright);
    port_ApplyInput(settings, PORT_SOURCE_AXIS, 0, 0, -13106);
    CHECK_EQ(port_GetHeldButtons(settings, 0), PADLleft);
    port_ApplyInput(settings, PORT_SOURCE_AXIS, 0, 1, -32768);
    CHECK_EQ(port_GetHeldButtons(settings, 0), PADLleft | PADLup);
    port_ApplyInput(settings, PORT_SOURCE_AXIS, 0, 1, 32767);
    CHECK_EQ(port_GetHeldButtons(settings, 0), PADLleft | PADLdown);
    port_ApplyInput(settings, PORT_SOURCE_AXIS, 0, 5, 20000);
    CHECK_EQ(port_GetHeldButtons(settings, 0), PADLleft | PADLdown | PADR2);
    /* a trigger at rest, as an axis's lowest, presses nothing */
    port_ApplyInput(settings, PORT_SOURCE_AXIS, 0, 4, -32768);
    CHECK_EQ(port_GetHeldButtons(settings, 0), PADLleft | PADLdown | PADR2);
    /* pulled out: what the controller held is let go, the keys not */
    port_ApplyInput(settings, PORT_SOURCE_KEY, 0, 40, 1);
    port_ReleaseController(settings, 0);
    CHECK_EQ(port_GetHeldButtons(settings, 0), PADstart);
    CHECK_EQ(port_GetHeldButtons(settings, 1), PADRdown | PADRright);

    /* a file: a line for a button takes its built-in key away, and
     * leaves its built-in controller button; more lines add */
    port_SetDefaultSettings(settings, test_LookUp);
    test_ReadText(
        "# mine\n"
        "\n"
        "p1.cross = key Space   # the long one\n"
        "P1.Cross = KEY Keypad 4\n"
        "  p2.start=pad a\r\n"
        "p2.up = pad -lefty\n");
    CHECK_EQ(g_test_complaints, 0);
    CHECK_EQ(test_CountBindings(1, 0, PADRdown), 2);
    CHECK_EQ(test_CountBindings(0, 0, PADRdown), 1);
    port_ApplyInput(settings, PORT_SOURCE_KEY, 0, 29, 1); /* Z no longer */
    CHECK_EQ(port_GetHeldButtons(settings, 0), 0);
    port_ApplyInput(settings, PORT_SOURCE_KEY, 0, 44, 1);
    CHECK_EQ(port_GetHeldButtons(settings, 0), PADRdown);
    port_ApplyInput(settings, PORT_SOURCE_KEY, 0, 44, 0);
    port_ApplyInput(settings, PORT_SOURCE_KEY, 0, 92, 1);
    CHECK_EQ(port_GetHeldButtons(settings, 0), PADRdown);
    /* the second pad's Start is on A now, and Start does nothing; its
     * key is as it was. A is Start alone: it is Cross no longer, on
     * that pad, and still Cross on the first */
    CHECK_EQ(test_CountBindings(0, 1, PADstart), 1);
    CHECK_EQ(test_CountBindings(1, 1, PADstart), 1);
    CHECK_EQ(test_CountBindings(0, 1, PADRdown), 0);
    CHECK_EQ(test_CountBindings(1, 1, PADRdown), 1);
    port_ApplyInput(settings, PORT_SOURCE_BUTTON, 1, 6, 1);
    CHECK_EQ(port_GetHeldButtons(settings, 1), 0);
    port_ApplyInput(settings, PORT_SOURCE_BUTTON, 1, 0, 1);
    CHECK_EQ(port_GetHeldButtons(settings, 1), PADstart);
    port_ApplyInput(settings, PORT_SOURCE_BUTTON, 0, 0, 1);
    port_ApplyInput(settings, PORT_SOURCE_KEY, 0, 92, 0);
    CHECK_EQ(port_GetHeldButtons(settings, 0), PADRdown);
    port_ApplyInput(settings, PORT_SOURCE_BUTTON, 0, 0, 0);
    /* its Up is the stick alone */
    CHECK_EQ(test_CountBindings(0, 1, PADLup), 1);
    /* a key given a new button is taken from its built-in one, on
     * whichever pad that was; the same for a stick's direction, on its
     * own pad only; and what a file gave is never taken by a later
     * line */
    test_ReadText("p2.circle = key Up\n"
                  "p1.triangle = pad +leftx\n"
                  "p1.square = key Space\n");
    CHECK_EQ(g_test_complaints, 0);
    CHECK_EQ(test_CountBindings(1, 0, PADLup), 0);
    CHECK_EQ(test_CountBindings(1, 1, PADRright), 1);
    CHECK_EQ(
        test_CountBindings(0, 0, PADLright), 1); /* the pad, not the stick */
    CHECK_EQ(test_CountBindings(0, 1, PADLright), 2);
    CHECK_EQ(test_CountBindings(0, 0, PADRup), 1);
    CHECK_EQ(test_CountBindings(1, 0, PADRdown), 2); /* Space is both now */
    CHECK_EQ(test_CountBindings(1, 0, PADRleft), 1);
    /* `none`: nothing presses it */
    test_ReadText("p1.select = none\n");
    CHECK_EQ(g_test_complaints, 0);
    CHECK_EQ(test_CountBindings(1, 0, PADselect), 0);
    CHECK_EQ(test_CountBindings(0, 0, PADselect), 0);
    CHECK_EQ(test_CountBindings(1, 1, PADselect), 1);

    /* the window's own settings */
    test_ReadText("scale = integer\nfilter = linear\nfullscreen = on\n"
                  "vsync = off\ndeadzone = 25\n");
    CHECK_EQ(g_test_complaints, 0);
    CHECK_EQ(settings->scale, PORT_SCALE_INTEGER);
    CHECK_EQ(settings->is_linear, 1);
    CHECK_EQ(settings->is_fullscreen, 1);
    CHECK_EQ(settings->vsync, 0);
    CHECK_EQ(settings->deadzone, 25);
    test_ReadText("scale = STRETCH\nfilter = nearest\nfullscreen = off\n"
                  "vsync = on\n");
    CHECK_EQ(settings->scale, PORT_SCALE_STRETCH);
    CHECK_EQ(settings->is_linear, 0);
    CHECK_EQ(settings->is_fullscreen, 0);
    CHECK_EQ(settings->vsync, 1);

    /* lines that say nothing the port knows are counted and named, and
     * change nothing; the ones between them are used */
    port_SetDefaultSettings(settings, test_LookUp);
    test_ReadText(
        "p1.cross = key Nonesuch\n"  /* 1 */
        "p3.cross = key Z\n"         /* 2 */
        "p1.jump = key Z\n"          /* 3 */
        "p1.cross = mouse left\n"    /* 4 */
        "scale = huge\n"             /* 5 */
        "deadzone = 100\n"           /* 6 */
        "just words\n"               /* 7 */
        "p2.circle = key Space\n"    /* 8: used */
        "p1.cross = pad +nonesuch\n" /* 9 */
        "volume = 3\n");             /* 10 */
    CHECK_EQ(g_test_complaints, 9);
    CHECK_EQ(g_test_complaint_lines[0], 1);
    CHECK_EQ(g_test_complaint_lines[6], 7);
    CHECK_EQ(g_test_complaint_lines[7], 9);
    CHECK_EQ(settings->count, 2 * (14 + 18));
    CHECK_EQ(settings->scale, PORT_SCALE_FIT);
    CHECK_EQ(settings->deadzone, PORT_DEADZONE_DEFAULT);
    binding = port_FindBinding(settings, 1, 0, PADRdown);
    CHECK(binding != NULL && binding->code == 29);
    binding = port_FindBinding(settings, 1, 1, PADRright);
    CHECK(binding != NULL && binding->code == 44);

    /* Escape is no pad's (pause and back): a line that binds it is
     * complained of, and the button keeps its built-in key */
    port_SetDefaultSettings(settings, test_LookUp);
    test_ReadText("p1.start = key Escape\n");
    CHECK_EQ(g_test_complaints, 1);
    CHECK_EQ(settings->count, 2 * (14 + 18));
    binding = port_FindBinding(settings, 1, 0, PADstart);
    CHECK(binding != NULL && binding->code == 40);
}

/* ---- the remastered picture's settings ---- */

/* The five lines of a settings file that say which picture is shown and
 * how it is drawn (keymap.c; docs/PORT.md, "Remastered in the window").
 * The words of the draw distance are the renderer's own
 * (port_ReadRemasterDistance), which is why this test is linked with it
 * (window.py). */
static void test_Graphics(void) {
    port_SetDefaultSettings(&g_test_settings, test_LookUp);
    /* what the layout built in says: the remastered picture, the
     * window's size the resolution */
    CHECK_EQ(g_test_settings.graphics, PORT_GRAPHICS_REMASTERED);
    CHECK_EQ(g_test_settings.resolution, PORT_RESOLUTION_WINDOW);
    /* the whole stage, so that nothing of it comes and goes as the
     * camera turns */
    CHECK_EQ(g_test_settings.distance, PORT_DISTANCE_MAX);
    CHECK_EQ(g_test_settings.supersample, 2);
    /* the wider picture is what the window shows with nothing said */
    CHECK_EQ(g_test_settings.aspect_w, PORT_ASPECT_W_WIDE);
    CHECK_EQ(g_test_settings.aspect_h, PORT_ASPECT_H_WIDE);
    test_ReadText("graphics = remastered\nresolution = 4\n"
                  "draw_distance = max\nantialiasing = 4\naspect = 16:9\n");
    CHECK_EQ(g_test_complaints, 0);
    CHECK_EQ(g_test_settings.graphics, PORT_GRAPHICS_REMASTERED);
    CHECK_EQ(g_test_settings.resolution, 4);
    CHECK_EQ(g_test_settings.distance, PORT_DISTANCE_MAX);
    CHECK_EQ(g_test_settings.supersample, 4);
    CHECK_EQ(g_test_settings.aspect_w, 16);
    CHECK_EQ(g_test_settings.aspect_h, 9);
    /* letters of either case, `off` for no smoothing, `original` for the
     * console's own distance, and the window's own size again */
    test_ReadText("graphics = Original\nresolution = 0\n"
                  "draw_distance = Original\nantialiasing = OFF\n"
                  "aspect = 4:3\n");
    CHECK_EQ(g_test_complaints, 0);
    CHECK_EQ(g_test_settings.graphics, PORT_GRAPHICS_ORIGINAL);
    CHECK_EQ(g_test_settings.resolution, PORT_RESOLUTION_WINDOW);
    CHECK_EQ(g_test_settings.distance, PORT_DISTANCE_ORIGINAL);
    CHECK_EQ(g_test_settings.supersample, 1);
    CHECK_EQ(g_test_settings.aspect_w, 4);
    CHECK_EQ(g_test_settings.aspect_h, 3);
    /* `follow` is the options page again */
    test_ReadText("graphics = follow\n");
    CHECK_EQ(g_test_complaints, 0);
    CHECK_EQ(g_test_settings.graphics, PORT_GRAPHICS_FOLLOW);
    /* a shape narrower than the console's is the console's: the scene is
     * never cut to less than it showed */
    test_ReadText("aspect = 5:4\n");
    CHECK_EQ(g_test_complaints, 0);
    CHECK_EQ(g_test_settings.aspect_w, PORT_ASPECT_W_ORIGINAL);
    CHECK_EQ(g_test_settings.aspect_h, PORT_ASPECT_H_ORIGINAL);
    /* a shape in its own terms is kept as it is written: the renderer
     * reduces it (port_RemasterSetView) */
    test_ReadText("aspect = 32:18\n");
    CHECK_EQ(g_test_complaints, 0);
    CHECK_EQ(g_test_settings.aspect_w, 32);
    CHECK_EQ(g_test_settings.aspect_h, 18);
    /* and the lines it cannot use: each leaves the setting alone */
    test_ReadText("graphics = better\nresolution = 9\nresolution = -1\n"
                  "draw_distance = farther\nantialiasing = 5\n"
                  "antialiasing = some\naspect = 16\naspect = 16:\n"
                  "aspect = :9\naspect = 0:9\n");
    CHECK_EQ(g_test_complaints, 10);
    CHECK_EQ(g_test_settings.graphics, PORT_GRAPHICS_FOLLOW);
    CHECK_EQ(g_test_settings.resolution, PORT_RESOLUTION_WINDOW);
    CHECK_EQ(g_test_settings.distance, PORT_DISTANCE_ORIGINAL);
    CHECK_EQ(g_test_settings.supersample, 1);
    CHECK_EQ(g_test_settings.aspect_w, 32);
    CHECK_EQ(g_test_settings.aspect_h, 18);
}

/* ---- the session's log ---- */

#define TEST_LOGS 25

static s32 g_test_line_count;
static u32 g_test_line_lengths[8];
static char g_test_lines[8][16];

static void test_TakeLine(const char *line, u32 length) {
    u32 i;

    if (g_test_line_count < 8) {
        g_test_line_lengths[g_test_line_count] = length;
        for (i = 0; i < length && i < 15; i++) {
            g_test_lines[g_test_line_count][i] = line[i];
        }
        g_test_lines[g_test_line_count][i] = 0;
    }
    g_test_line_count++;
}

static void test_Log(void) {
    static char names[TEST_LOGS + 3][PORT_LOG_NAME_BYTES];
    static const char *listed[TEST_LOGS + 3];
    static u8 is_old[TEST_LOGS + 3];
    static char many[1100];
    PortTee tee;
    char name[PORT_LOG_NAME_BYTES + 4];
    s32 i;
    s32 old;

    /* a name for a moment, of one length, in order of time as text */
    port_FormatLogName(2026, 11, 15, 3, 7, 9, name);
    CHECK(test_StringsEqual(name, "rebushido-20261115-030709.log"));
    CHECK_EQ(name[PORT_LOG_NAME_BYTES - 1], 0);
    CHECK(port_IsLogName(name));
    CHECK(port_IsLogName("rebushido-19991231-235959.log"));
    CHECK(!port_IsLogName("rebushido-2026115-030709.log"));
    CHECK(!port_IsLogName("rebushido-20261115_030709.log"));
    CHECK(!port_IsLogName("rebushido-20261115-030709.txt"));
    CHECK(!port_IsLogName("rebushido-20261115-030709.log.bak"));
    CHECK(!port_IsLogName("xrebushido-20261115-030709.log"));
    CHECK(!port_IsLogName("rebushido-2026111a-030709.log"));
    CHECK(!port_IsLogName("rebushido.cfg"));
    CHECK(!port_IsLogName(""));

    /* 25 logs, in no order, among files that are not logs: the five
     * oldest go, the twenty newest stay, and nothing else is touched */
    for (i = 0; i < TEST_LOGS; i++) {
        /* seconds 0 to 24, shuffled by a step of seven */
        port_FormatLogName(2026, 11, 15, 12, 0, (i * 7) % TEST_LOGS, names[i]);
        listed[i] = names[i];
    }
    listed[TEST_LOGS] = "rebushido.cfg";
    listed[TEST_LOGS + 1] = "notes.txt";
    listed[TEST_LOGS + 2] = "rebushido-20200101-000000.log.old";
    old = port_FindOldLogs(listed, TEST_LOGS + 3, PORT_LOGS_KEPT, is_old);
    CHECK_EQ(old, 5);
    for (i = 0; i < TEST_LOGS; i++) {
        CHECK_EQ(is_old[i], (i * 7) % TEST_LOGS < 5);
    }
    CHECK_EQ(is_old[TEST_LOGS], 0);
    CHECK_EQ(is_old[TEST_LOGS + 1], 0);
    CHECK_EQ(is_old[TEST_LOGS + 2], 0);
    /* twenty or fewer: none goes */
    CHECK_EQ(port_FindOldLogs(listed, 20, PORT_LOGS_KEPT, is_old), 0);
    CHECK_EQ(port_FindOldLogs(listed, 0, PORT_LOGS_KEPT, is_old), 0);
    /* a year's last second is older than the next year's first */
    listed[0] = "rebushido-20260101-000000.log";
    listed[1] = "rebushido-20251231-235959.log";
    CHECK_EQ(port_FindOldLogs(listed, 2, 1, is_old), 1);
    CHECK_EQ(is_old[0], 0);
    CHECK_EQ(is_old[1], 1);

    /* what is printed in pieces comes out as whole lines, each when its
     * end comes; an empty line is a line; what has not ended waits */
    tee.used = 0;
    g_test_line_count = 0;
    port_TeeBytes(&tee, "ab", 2, test_TakeLine);
    CHECK_EQ(g_test_line_count, 0);
    port_TeeBytes(&tee, "c\nde\r\n\nf", 8, test_TakeLine);
    CHECK_EQ(g_test_line_count, 3);
    CHECK(test_StringsEqual(g_test_lines[0], "abc"));
    CHECK(test_StringsEqual(g_test_lines[1], "de"));
    CHECK_EQ(g_test_line_lengths[2], 0);
    port_FlushTee(&tee, test_TakeLine);
    CHECK_EQ(g_test_line_count, 4);
    CHECK(test_StringsEqual(g_test_lines[3], "f"));
    port_FlushTee(&tee, test_TakeLine);
    CHECK_EQ(g_test_line_count, 4);
    /* a line longer than the buffer comes in pieces, nothing lost */
    for (i = 0; i < 1100; i++) {
        many[i] = 'x';
    }
    g_test_line_count = 0;
    port_TeeBytes(&tee, many, 1100, test_TakeLine);
    port_TeeBytes(&tee, "\n", 1, test_TakeLine);
    CHECK_EQ(g_test_line_count, 3);
    CHECK_EQ(g_test_line_lengths[0], PORT_TEE_LINE_MAX);
    CHECK_EQ(g_test_line_lengths[1], PORT_TEE_LINE_MAX);
    CHECK_EQ(g_test_line_lengths[2], 1100 - 2 * PORT_TEE_LINE_MAX);

    /* the sum everyone's CRC-32 gives "123456789", whole or in pieces */
    CHECK_EQ(port_Crc32(0, (const u8 *)"123456789", 9), 0xCBF43926);
    CHECK_EQ(port_Crc32(
                 port_Crc32(0, (const u8 *)"1234", 4), (const u8 *)"56789", 5),
             0xCBF43926);
    CHECK_EQ(port_Crc32(0, (const u8 *)"", 0), 0);
    CHECK_EQ(port_Crc32(0, (const u8 *)"a", 1), 0xE8B7BE43);
}

/* ---- the counter in the corner ---- */

/* How many pixels of a buffer are `ink`. */
static s32 test_CountInk(const u32 *pixels, s32 count, u32 ink) {
    s32 found = 0;
    s32 i;

    for (i = 0; i < count; i++) {
        found += pixels[i] == ink;
    }
    return found;
}

static void test_Counter(void) {
    static const char shapes[] = "0123456789/%.-ABCDEFGHIJKLMNOPQRSTUVWXYZ";
    static u32 pixels[3 * 20 * 9];
    PortFpsMeter meter;
    PortRect picture;
    PortRect place;
    char text[PORT_FPS_TEXT_BYTES];
    u32 now;
    s32 i;
    s32 j;

    /* sixty a second: one every 735 samples of the clock. The rate is
     * worked out each half second, to a tenth */
    meter.is_started = 0;
    now = 1000;
    port_CountFps(&meter, now, 1);
    CHECK_EQ(meter.tenths, 0);
    for (i = 0; i < 29; i++) {
        now += 735;
        port_CountFps(&meter, now, 1);
    }
    CHECK_EQ(meter.tenths, 0); /* not half a second yet */
    now += 735;
    port_CountFps(&meter, now, 1);
    CHECK_EQ(meter.tenths, 600);
    /* the console's own blanks, 737 samples apart: 59.8 */
    for (i = 0; i < 30; i++) {
        now += 737;
        port_CountFps(&meter, now, 1);
    }
    CHECK_EQ(meter.tenths, 598);
    /* a frame of the game every second blank, counted at every blank:
     * thirty a second */
    for (i = 0; i < 30; i++) {
        now += 735;
        port_CountFps(&meter, now, (u32)(i & 1));
    }
    CHECK_EQ(meter.tenths, 300);
    /* several at once count as several */
    now += 22050;
    port_CountFps(&meter, now, 25);
    CHECK_EQ(meter.tenths, 500);
    /* nothing for half a second: none a second */
    now += 22050;
    port_CountFps(&meter, now, 0);
    CHECK_EQ(meter.tenths, 0);
    /* the clock going round at 32 bits */
    meter.is_started = 0;
    now = 0xFFFFF000U;
    port_CountFps(&meter, now, 1);
    for (i = 0; i < 30; i++) {
        now += 735;
        port_CountFps(&meter, now, 1);
    }
    CHECK(now < 0x10000);
    CHECK_EQ(meter.tenths, 600);

    /* the text: shown, the game's, and the speed against the console's
     * 59.826 */
    port_FormatFps(text, 598, 299, 598);
    CHECK(test_StringsEqual(text, "59.8/29.9 100%"));
    port_FormatFps(text, 600, 300, 600);
    CHECK(test_StringsEqual(text, "60.0/30.0 100%"));
    port_FormatFps(text, 0, 0, 0);
    CHECK(test_StringsEqual(text, "0.0/0.0 0%"));
    port_FormatFps(text, 299, 150, 299);
    CHECK(test_StringsEqual(text, "29.9/15.0 50%"));
    port_FormatFps(text, 1200, 600, 1200);
    CHECK(test_StringsEqual(text, "120.0/60.0 201%"));
    /* more pictures a second than the console's blanks: the speed is
     * still the blanks' (gl_tween.h) */
    port_FormatFps(text, 1440, 300, 598);
    CHECK(test_StringsEqual(text, "144.0/30.0 100%"));
    port_FormatFps(text, 1440, 300, 299);
    CHECK(test_StringsEqual(text, "144.0/30.0 50%"));
    port_FormatFps(text, 9999, 9999, 9999);
    CHECK(port_MeasureText(text) + 4 <= 112); /* the widest fits its box */

    /* every letter of the text has a shape of five by seven, no two
     * alike; a digit reaches the top row and the bottom */
    for (i = 0; shapes[i] != 0; i++) {
        const u8 *rows = port_FindGlyph(shapes[i]);

        CHECK(rows != NULL);
        for (j = 0; rows != NULL && j < PORT_GLYPH_H; j++) {
            CHECK(rows[j] < (1 << PORT_GLYPH_W));
        }
        if (rows != NULL && shapes[i] >= '0' && shapes[i] <= '9') {
            CHECK(rows[0] != 0 && rows[PORT_GLYPH_H - 1] != 0);
        }
        for (j = 0; rows != NULL && j < i; j++) {
            CHECK(!test_BytesEqual(
                rows, port_FindGlyph(shapes[j]), PORT_GLYPH_H));
        }
    }
    CHECK(port_FindGlyph(' ') == NULL);
    CHECK(port_FindGlyph('a') == NULL);
    /* the capitals, for the times under the counter (PORT_FPS_TIMES) */
    CHECK(port_FindGlyph('A') != NULL && port_FindGlyph('Z') != NULL);
    CHECK_EQ(port_MeasureText(""), 0);
    CHECK_EQ(port_MeasureText("0"), 5);
    CHECK_EQ(port_MeasureText("60/30"), 29);

    /* a 1 at (1, 1) of the middle one of three boxes of 20 by 9: its
     * ten pixels, where they belong, and nothing in the boxes beside */
    port_DrawText(pixels + 20 * 9, 20, 9, 1, 1, "1", 0xFFFFFFFF);
    CHECK_EQ(test_CountInk(pixels, 3 * 20 * 9, 0xFFFFFFFF), 10);
    CHECK_EQ(pixels[20 * 9 + 1 * 20 + 3], 0xFFFFFFFF); /* its top */
    CHECK_EQ(pixels[20 * 9 + 2 * 20 + 2], 0xFFFFFFFF); /* its flag */
    CHECK_EQ(pixels[20 * 9 + 7 * 20 + 2], 0xFFFFFFFF); /* its foot */
    CHECK_EQ(pixels[20 * 9 + 7 * 20 + 4], 0xFFFFFFFF);
    CHECK_EQ(pixels[20 * 9 + 7 * 20 + 5], 0);
    /* a space is a letter's room; what runs off the box is left out */
    for (i = 0; i < 3 * 20 * 9; i++) {
        pixels[i] = 0;
    }
    port_DrawText(pixels + 20 * 9, 20, 9, 1, 1, "1 1", 0xFF00FF00);
    CHECK_EQ(test_CountInk(pixels, 3 * 20 * 9, 0xFF00FF00), 20);
    CHECK_EQ(pixels[20 * 9 + 1 * 20 + 15], 0xFF00FF00);
    for (i = 0; i < 3 * 20 * 9; i++) {
        pixels[i] = 0;
    }
    port_DrawText(pixels + 20 * 9, 20, 9, -3, -2, "8", 0xFF00FF00);
    port_DrawText(pixels + 20 * 9, 20, 9, 17, 5, "8", 0xFF00FF00);
    port_DrawText(pixels + 20 * 9, 20, 9, 40, 40, "8", 0xFF00FF00);
    CHECK_EQ(test_CountInk(pixels, 20 * 9, 0xFF00FF00), 0);
    CHECK_EQ(test_CountInk(pixels + 2 * 20 * 9, 20 * 9, 0xFF00FF00), 0);
    CHECK(test_CountInk(pixels + 20 * 9, 20 * 9, 0xFF00FF00) > 0);

    /* its place: in the picture's corner, each of its pixels as many of
     * the window's as the picture's 240 lines are, whole */
    picture.x = 240;
    picture.y = 0;
    picture.w = 1440;
    picture.h = 1080;
    port_PlaceCounter(&picture, 64, 11, PORT_CORNER_TOP_LEFT, &place);
    CHECK_EQ(place.w, 256);
    CHECK_EQ(place.h, 44);
    CHECK_EQ(place.x, 248);
    CHECK_EQ(place.y, 8);
    port_PlaceCounter(&picture, 64, 11, PORT_CORNER_TOP_RIGHT, &place);
    CHECK_EQ(place.x, 240 + 1440 - 256 - 8);
    CHECK_EQ(place.y, 8);
    port_PlaceCounter(&picture, 64, 11, PORT_CORNER_BOTTOM_LEFT, &place);
    CHECK_EQ(place.x, 248);
    CHECK_EQ(place.y, 1080 - 44 - 8);
    port_PlaceCounter(&picture, 64, 11, PORT_CORNER_BOTTOM_RIGHT, &place);
    CHECK_EQ(place.x, 240 + 1440 - 256 - 8);
    CHECK_EQ(place.y, 1080 - 44 - 8);
    /* a picture smaller than the console's: once, never less */
    picture.x = 10;
    picture.y = 20;
    picture.w = 266;
    picture.h = 200;
    port_PlaceCounter(&picture, 64, 11, PORT_CORNER_TOP_LEFT, &place);
    CHECK_EQ(place.w, 64);
    CHECK_EQ(place.h, 11);
    CHECK_EQ(place.x, 12);
    CHECK_EQ(place.y, 22);

    /* its settings */
    port_SetDefaultSettings(&g_test_settings, test_LookUp);
    CHECK_EQ(g_test_settings.fps, 1);
    CHECK_EQ(g_test_settings.fps_corner, PORT_CORNER_TOP_LEFT);
    CHECK_EQ(g_test_settings.count, 2 * (14 + 18));
    test_ReadText("fps = off\nfps_corner = bottom-right\n");
    CHECK_EQ(g_test_complaints, 0);
    CHECK_EQ(g_test_settings.fps, 0);
    CHECK_EQ(g_test_settings.fps_corner, PORT_CORNER_BOTTOM_RIGHT);
    test_ReadText("fps = on\nfps_corner = Top-Right\nfps_corner = middle\n"
                  "fps = maybe\n");
    CHECK_EQ(g_test_complaints, 2);
    CHECK_EQ(g_test_settings.fps, 1);
    CHECK_EQ(g_test_settings.fps_corner, PORT_CORNER_TOP_RIGHT);
}

void test_Main(void) {
    test_Place();
    test_Shape();
    test_Graphics();
    test_Pixels();
    test_Ring();
    test_Pace();
    test_Refresh();
    test_Keys();
    test_Log();
    test_Counter();
}
