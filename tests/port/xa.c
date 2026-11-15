/* The port's disc sound: made-up XA sectors through its decoder, its queue and
 * its change of rate. */
/* The native port plays the disc's XA sound itself (src/port/xa.c).
 * Here it is given sectors this test makes up, numbers and not sound,
 * nothing of the game's: what it decodes of them is worked out a second
 * time by tests/port/xa.py from the format as it is written down, and
 * the cases small enough to do by hand are checked here. */
#include "common.h"
#include "src/port/xa.c"
#include "runtime/runtime.h"

#define MADE_AUDIO 0x64 /* a submode: real time, form 2, sound */

static u8 g_made_sector[PORT_DISC_SECTOR_BYTES];
static s16 g_made_samples[SECTOR_SAMPLES_MAX];
static s16 g_made_out[2 * 4096];

/* A sector of sound with this coding byte: every group's blocks get
 * the headers `headers` in turn, and its data are numbers that say
 * where they are. */
static void made_Sector(u32 coding, const u8 *headers, u32 header_count) {
    u32 group;
    u32 i;

    for (i = 0; i < PORT_DISC_SECTOR_BYTES; i++) {
        g_made_sector[i] = 0;
    }
    g_made_sector[SECTOR_MODE] = MODE_2;
    g_made_sector[SUBHEADER] = 1;
    g_made_sector[SUBHEADER + SUBHEADER_SUBMODE] = MADE_AUDIO;
    g_made_sector[SUBHEADER + SUBHEADER_CODING] = (u8)coding;
    for (group = 0; group < GROUPS; group++) {
        u8 *bytes = g_made_sector + PORT_DISC_DATA_OFFSET + group * GROUP_BYTES;

        for (i = 0; i < 8; i++) {
            bytes[GROUP_HEADERS + i] = headers[(group * 8 + i) % header_count];
        }
        for (i = GROUP_DATA; i < GROUP_BYTES; i++) {
            bytes[i] = (u8)(group * 29 + i * 7 + (i >> 3));
        }
    }
}

static void test_PrintSector(const char *name, u32 coding) {
    static const u8 headers[] = {
        0x00, 0x1C, 0x29, 0x35, 0x0D, 0x18, 0x27, 0x3A, 0x04, 0x16, 0x2F};
    s32 old[2] = {0, 0};
    s32 older[2] = {0, 0};
    s32 stereo = -1;
    s32 half_rate = -1;
    u32 count;
    u32 i;

    made_Sector(coding, headers, sizeof(headers));
    count = port_DecodeXaSector(
        g_made_sector, old, older, g_made_samples, &stereo, &half_rate);
    test_Print(
        "sector %s %d %d %d %d\n", name, coding, count, stereo, half_rate);
    test_Print("bytes");
    for (i = 0; i < GROUPS * GROUP_BYTES; i++) {
        test_Print(" %d", g_made_sector[PORT_DISC_DATA_OFFSET + i]);
    }
    test_Print("\nsamples");
    for (i = 0; i < count; i++) {
        test_Print(" %d", g_made_samples[i]);
    }
    test_Print("\n");
}

static void test_Decoder(void) {
    static const u8 plain[] = {0x0C};
    static const u8 wide[] = {0x08};
    s32 old[2] = {0, 0};
    s32 older[2] = {0, 0};
    s32 stereo;
    s32 half_rate;
    u8 *group = g_made_sector + PORT_DISC_DATA_OFFSET;
    u32 count;

    /* by hand: four bits, mono, no filter, a shift of 12: each sample
     * is its four bits. The first four bytes of a group's data are the
     * first sample of its eight blocks, the low four bits first; a
     * block's 28 samples are one after another in what is decoded. */
    made_Sector(0x00, plain, 1);
    group[GROUP_DATA + 0] = 0x21;
    group[GROUP_DATA + 1] = 0x43;
    group[GROUP_DATA + 2] = 0x65;
    group[GROUP_DATA + 3] = 0xF7;
    group[GROUP_DATA + 4] = 0x8A; /* the blocks' second samples */
    count = port_DecodeXaSector(
        g_made_sector, old, older, g_made_samples, &stereo, &half_rate);
    CHECK_EQ(count, 18 * 8 * 28);
    CHECK_EQ(stereo, 0);
    CHECK_EQ(half_rate, 0);
    CHECK_EQ(g_made_samples[0 * 28], 1);
    CHECK_EQ(g_made_samples[1 * 28], 2);
    CHECK_EQ(g_made_samples[2 * 28], 3);
    CHECK_EQ(g_made_samples[3 * 28], 4);
    CHECK_EQ(g_made_samples[6 * 28], 7);
    CHECK_EQ(g_made_samples[7 * 28], -1);
    CHECK_EQ(g_made_samples[0 * 28 + 1], -6);
    CHECK_EQ(g_made_samples[1 * 28 + 1], -8);
    /* stereo: the blocks are left and right by turns, so the first
     * frame is blocks 0 and 1, and the 29th is blocks 2 and 3 */
    made_Sector(0x01, plain, 1);
    group[GROUP_DATA + 0] = 0x21;
    group[GROUP_DATA + 1] = 0x43;
    old[0] = old[1] = older[0] = older[1] = 0;
    count = port_DecodeXaSector(
        g_made_sector, old, older, g_made_samples, &stereo, &half_rate);
    CHECK_EQ(count, 18 * 8 * 28);
    CHECK_EQ(stereo, 1);
    CHECK_EQ(g_made_samples[0], 1);
    CHECK_EQ(g_made_samples[1], 2);
    CHECK_EQ(g_made_samples[2 * 28], 3);
    CHECK_EQ(g_made_samples[2 * 28 + 1], 4);
    /* eight bits, half rate: four blocks a group, a byte a sample, at
     * the top of sixteen bits and shifted down by 8: itself, signed */
    made_Sector(0x14, wide, 1);
    group[GROUP_DATA + 0] = 0x7F;
    group[GROUP_DATA + 1] = 0x80;
    group[GROUP_DATA + 2] = 0x01;
    group[GROUP_DATA + 3] = 0xFF;
    old[0] = older[0] = 0;
    count = port_DecodeXaSector(
        g_made_sector, old, older, g_made_samples, &stereo, &half_rate);
    CHECK_EQ(count, 18 * 4 * 28);
    CHECK_EQ(half_rate, 1);
    CHECK_EQ(g_made_samples[0 * 28], 127);
    CHECK_EQ(g_made_samples[1 * 28], -128);
    CHECK_EQ(g_made_samples[2 * 28], 1);
    CHECK_EQ(g_made_samples[3 * 28], -1);
    /* what is not a sector of sound holds none: a sector of data, a
     * sector of a movie's picture, a mode 1 sector */
    made_Sector(0x01, plain, 1);
    g_made_sector[SUBHEADER + SUBHEADER_SUBMODE] = 0x08;
    CHECK_EQ(port_DecodeXaSector(g_made_sector, old, older, g_made_samples,
                                 &stereo, &half_rate),
             0);
    g_made_sector[SUBHEADER + SUBHEADER_SUBMODE] = 0x62;
    CHECK_EQ(port_FeedXaSector(g_made_sector), 0);
    g_made_sector[SUBHEADER + SUBHEADER_SUBMODE] = MADE_AUDIO;
    g_made_sector[SECTOR_MODE] = 1;
    CHECK_EQ(port_FeedXaSector(g_made_sector), 0);
    CHECK_EQ(port_GetXaSectorCount(), 0);

    /* for the Python side: every filter and many shifts, in each of the
     * format's forms */
    test_PrintSector("stereo_4bit", 0x01);
    test_PrintSector("mono_4bit", 0x00);
    test_PrintSector("stereo_8bit", 0x11);
    test_PrintSector("mono_8bit_half", 0x14);
}

/* ---- the queue and the change of rate ---- */

static void test_Rate(void) {
    static const u8 wide[] = {0x08};
    u32 i;
    s32 wrong = 0;
    u32 sounding = 0;

    /* a straight line in comes out a straight line: seven samples out
     * for six in, so a line that rises 7 a sample in rises 6 a sample
     * out */
    port_ResetCdAudio();
    for (i = 0; i < 600; i++) {
        port_QueueXaFrame((s32)i * 7, -(s32)i * 7);
    }
    port_ReadCdAudio(g_made_out, 600);
    for (i = 10; i < 600; i++) {
        wrong += g_made_out[2 * i] - g_made_out[2 * (i - 1)] != 6;
        wrong += g_made_out[2 * i + 1] - g_made_out[2 * (i - 1) + 1] != -6;
    }
    CHECK_EQ(wrong, 0);
    /* 600 out took 600 * 6 / 7 = 514 in: 86 are left, which are 100
     * out, and then there is silence */
    CHECK_EQ(g_port_xa.queue_count, 600 - 514);
    port_ReadCdAudio(g_made_out, 110);
    CHECK(g_made_out[2 * 90] != 0);
    CHECK_EQ(g_made_out[2 * 108], 0);
    CHECK_EQ(g_made_out[2 * 108 + 1], 0);

    /* a sector of one number, at half rate: each sample twice, 18 * 4
     * * 28 * 2 = 4032 at 37,800 a second, which are 4704 out; all of
     * them that number once the first few are past */
    port_ResetCdAudio();
    made_Sector(0x14, wide, 1);
    for (i = 0; i < GROUPS * GROUP_BYTES; i++) {
        if (i % GROUP_BYTES >= GROUP_DATA) {
            g_made_sector[PORT_DISC_DATA_OFFSET + i] = 100;
        }
    }
    CHECK_EQ(port_FeedXaSector(g_made_sector), 18 * 4 * 28);
    CHECK_EQ(port_GetXaSectorCount(), 1);
    CHECK_EQ(g_port_xa.queue_count, 4032);
    for (i = 0; i < 5000; i += 100) {
        u32 k;

        port_ReadCdAudio(g_made_out, 100);
        for (k = 0; k < 100; k++) {
            sounding +=
                g_made_out[2 * k] == 100 && g_made_out[2 * k + 1] == 100;
        }
    }
    CHECK(sounding >= 4700);
    CHECK(sounding <= 4704);

    /* the drive's volumes: 0x80 is all of a side; each side can be sent
     * to the other; the sum is held within 16 bits; muted is silent */
    port_ResetCdAudio();
    for (i = 0; i < 2000; i++) {
        port_QueueXaFrame(20000, -1000);
    }
    port_ReadCdAudio(g_made_out, 50);
    CHECK_EQ(g_made_out[2 * 49], 20000);
    CHECK_EQ(g_made_out[2 * 49 + 1], -1000);
    port_SetCdMix(0x40, 0, 0x40, 0);
    port_ReadCdAudio(g_made_out, 50);
    CHECK_EQ(g_made_out[2 * 49], 10000);
    CHECK_EQ(g_made_out[2 * 49 + 1], -500);
    /* the left to the right and the right to the left */
    port_SetCdMix(0, 0x80, 0, 0x80);
    port_ReadCdAudio(g_made_out, 50);
    CHECK_EQ(g_made_out[2 * 49], -1000);
    CHECK_EQ(g_made_out[2 * 49 + 1], 20000);
    /* 0xFF is one part in 128 short of twice: 20000 * 255 / 128 is
     * 39843, more than 16 bits */
    port_SetCdMix(0xFF, 0, 0xFF, 0);
    port_ReadCdAudio(g_made_out, 50);
    CHECK_EQ(g_made_out[2 * 49], 0x7FFF);
    CHECK_EQ(g_made_out[2 * 49 + 1], -1993); /* -1000 * 255 / 128, down */
    port_MuteCdAudio(1);
    port_ReadCdAudio(g_made_out, 50);
    CHECK_EQ(g_made_out[2 * 49], 0);
    port_MuteCdAudio(0);
    port_SetCdMix(0x80, 0, 0x80, 0);
}

void test_Main(void) {
    test_Decoder();
    test_Rate();
}
