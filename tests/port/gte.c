/* The port's GTE (src/port/gte.c) on cases worked out by hand. */
#include "src/port/gte.c"
#include "runtime/runtime.h"

/* The instruction words of the operations tried here. */
#define RTPS 0x4A180001U
#define RTPT 0x4A280030U
#define NCLIP 0x4B400006U
#define AVSZ3 0x4B58002DU
#define AVSZ4 0x4B68002EU
#define MVMVA_RT_V0_TR 0x4A480012U
#define MVMVA_RT_V0 0x4A486012U
#define MVMVA_RT_V0_TR_SF0 0x4A400012U
#define SQR_SF0 0x4AA00428U
#define SQR 0x4AA80428U
#define OP_SF0 0x4B70000CU
#define GPF 0x4B98003DU
#define GPL 0x4BA8003EU
#define DPCS 0x4A780010U
#define DPCT 0x4AF8002AU
#define INTPL 0x4A980011U
#define DCPL 0x4A680029U
#define NCS 0x4AC8041EU
#define NCT 0x4AD80420U
#define NCCS 0x4B08041BU
#define NCDS 0x4AE80413U
#define CC 0x4B38041CU

#define ONE 0x1000
#define HALVES(low, high) (((u32)(low) & 0xFFFF) | ((u32)(high) << 16))
#define DATA(index) port_GteReadData(index)
#define CONTROL(index) port_GteReadControl(index)

/* Every register 0, then the three matrices as given: nine numbers a
 * matrix, a row after a row; NULL for a matrix of zeroes. */
static void test_Reset(
    const s16 *rotation, const s16 *light, const s16 *light_color) {
    const s16 *matrices[3];
    s32 i;
    s32 j;

    for (i = 0; i < 32; i++) {
        g_port_gte.data[i] = 0;
        g_port_gte.control[i] = 0;
    }
    matrices[0] = rotation;
    matrices[1] = light;
    matrices[2] = light_color;
    for (i = 0; i < 3; i++) {
        const s16 *m = matrices[i];

        if (m) {
            for (j = 0; j < 4; j++) {
                port_GteWriteControl(8 * i + j, HALVES(m[2 * j], m[2 * j + 1]));
            }
            port_GteWriteControl(8 * i + 4, (u32)m[8]);
        }
    }
}

static void test_SetVector(s32 number, s32 x, s32 y, s32 z) {
    port_GteWriteData(PORT_GTE_VXY0 + 2 * number, HALVES(x, y));
    port_GteWriteData(PORT_GTE_VZ0 + 2 * number, (u32)z);
}

static void test_SetIr(s32 ir0, s32 ir1, s32 ir2, s32 ir3) {
    port_GteWriteData(PORT_GTE_IR0, (u32)ir0);
    port_GteWriteData(PORT_GTE_IR1, (u32)ir1);
    port_GteWriteData(PORT_GTE_IR2, (u32)ir2);
    port_GteWriteData(PORT_GTE_IR3, (u32)ir3);
}

static void test_SetFarColor(s32 r, s32 g, s32 b) {
    port_GteWriteControl(PORT_GTE_RFC, (u32)r);
    port_GteWriteControl(PORT_GTE_GFC, (u32)g);
    port_GteWriteControl(PORT_GTE_BFC, (u32)b);
}

#define CHECK_MAC(m1, m2, m3)                                                  \
    CHECK_EQ(DATA(PORT_GTE_MAC1), m1);                                         \
    CHECK_EQ(DATA(PORT_GTE_MAC2), m2);                                         \
    CHECK_EQ(DATA(PORT_GTE_MAC3), m3)
#define CHECK_IR(i1, i2, i3)                                                   \
    CHECK_EQ(DATA(PORT_GTE_IR1), i1);                                          \
    CHECK_EQ(DATA(PORT_GTE_IR2), i2);                                          \
    CHECK_EQ(DATA(PORT_GTE_IR3), i3)
#define CHECK_FLAG(flag) CHECK_EQ(CONTROL(PORT_GTE_FLAG), flag)

static const s16 g_test_identity[9] = {ONE, 0, 0, 0, ONE, 0, 0, 0, ONE};

/* ---- the registers ---- */

static void test_Registers(void) {
    test_Reset(NULL, NULL, NULL);

    /* The 16-bit registers read back with their sign, or without. */
    port_GteWriteData(PORT_GTE_VZ0, 0x12348765);
    CHECK_EQ(DATA(PORT_GTE_VZ0), 0xFFFF8765);
    port_GteWriteData(PORT_GTE_IR1, 0x00017FFF);
    CHECK_EQ(DATA(PORT_GTE_IR1), 0x7FFF);
    port_GteWriteData(PORT_GTE_OTZ, 0x12348765);
    CHECK_EQ(DATA(PORT_GTE_OTZ), 0x8765);
    port_GteWriteData(PORT_GTE_SZ3, 0xFFFFFFFF);
    CHECK_EQ(DATA(PORT_GTE_SZ3), 0xFFFF);
    port_GteWriteData(PORT_GTE_MAC1, 0x12348765);
    CHECK_EQ(DATA(PORT_GTE_MAC1), 0x12348765);
    port_GteWriteControl(PORT_GTE_PLANE, 0x8000);
    CHECK_EQ(CONTROL(PORT_GTE_PLANE), 0xFFFF8000);
    port_GteWriteControl(PORT_GTE_ROTATION + 4, 0x00018000);
    CHECK_EQ(CONTROL(PORT_GTE_ROTATION + 4), 0xFFFF8000);
    port_GteWriteControl(PORT_GTE_ROTATION, 0x80007FFF);
    CHECK_EQ(CONTROL(PORT_GTE_ROTATION), 0x80007FFF);
    port_GteWriteControl(PORT_GTE_DQB, 0x80000001);
    CHECK_EQ(CONTROL(PORT_GTE_DQB), 0x80000001);

    /* A write to SXYP pushes; it reads as the newest. */
    port_GteWriteData(PORT_GTE_SXY0, 1);
    port_GteWriteData(PORT_GTE_SXY1, 2);
    port_GteWriteData(PORT_GTE_SXY2, 3);
    port_GteWriteData(PORT_GTE_SXYP, 4);
    CHECK_EQ(DATA(PORT_GTE_SXY0), 2);
    CHECK_EQ(DATA(PORT_GTE_SXY1), 3);
    CHECK_EQ(DATA(PORT_GTE_SXY2), 4);
    CHECK_EQ(DATA(PORT_GTE_SXYP), 4);

    /* IRGB sets IR1 to IR3 to five bits each, times 0x80; both colour
     * registers read them back, held to 0 and 0x1F. */
    port_GteWriteData(PORT_GTE_IRGB, 0x7FFF);
    CHECK_IR(0xF80, 0xF80, 0xF80);
    CHECK_EQ(DATA(PORT_GTE_ORGB), 0x7FFF);
    port_GteWriteData(PORT_GTE_IRGB, 0xFFFF8443); /* 3, 2, 1 */
    CHECK_IR(0x180, 0x100, 0x80);
    CHECK_EQ(DATA(PORT_GTE_IRGB), 0x0443);
    test_SetIr(0, -1, 0x1000, 0x7F);
    CHECK_EQ(DATA(PORT_GTE_ORGB), 0x1F << 5);
    port_GteWriteData(PORT_GTE_ORGB, 0x7FFF);
    CHECK_IR(0xFFFFFFFF, 0x1000, 0x7F);

    /* The count of leading bits like the sign. */
    port_GteWriteData(PORT_GTE_LZCS, 0);
    CHECK_EQ(DATA(PORT_GTE_LZCR), 32);
    port_GteWriteData(PORT_GTE_LZCS, 0xFFFFFFFF);
    CHECK_EQ(DATA(PORT_GTE_LZCR), 32);
    port_GteWriteData(PORT_GTE_LZCS, 1);
    CHECK_EQ(DATA(PORT_GTE_LZCR), 31);
    port_GteWriteData(PORT_GTE_LZCS, 0x00010000);
    CHECK_EQ(DATA(PORT_GTE_LZCR), 15);
    port_GteWriteData(PORT_GTE_LZCS, 0x7FFFFFFF);
    CHECK_EQ(DATA(PORT_GTE_LZCR), 1);
    port_GteWriteData(PORT_GTE_LZCS, 0x80000000);
    CHECK_EQ(DATA(PORT_GTE_LZCR), 1);
    port_GteWriteData(PORT_GTE_LZCS, 0xC0000000);
    CHECK_EQ(DATA(PORT_GTE_LZCR), 2);
    port_GteWriteData(PORT_GTE_LZCR, 7);
    CHECK_EQ(DATA(PORT_GTE_LZCR), 2);

    /* FLAG keeps bits 12 to 30; bit 31 says one of the error bits is
     * set, which IR3's (22), the colours' (19 to 21) and IR0's (12) are
     * not. */
    port_GteWriteControl(PORT_GTE_FLAG, 0xFFFFFFFF);
    CHECK_FLAG(0xFFFFF000);
    port_GteWriteControl(PORT_GTE_FLAG, 0x00781FFF);
    CHECK_FLAG(0x00781000);
    port_GteWriteControl(PORT_GTE_FLAG, 0x00002000);
    CHECK_FLAG(0x80002000);
}

/* ---- the projection ---- */

/* The screen is 320 by 240 with its centre at (160, 120), the projection
 * plane 500 away; the depth cue is 0x100 * (h / z) + 0x10000. */
static void test_SetScreen(void) {
    port_GteWriteControl(PORT_GTE_OFX, 160 << 16);
    port_GteWriteControl(PORT_GTE_OFY, 120 << 16);
    port_GteWriteControl(PORT_GTE_PLANE, 500);
    port_GteWriteControl(PORT_GTE_DQA, 0x100);
    port_GteWriteControl(PORT_GTE_DQB, 0x10000);
}

static void test_Projection(void) {
    /* (100, 50) at depth 1000: h / z is a half, so the point is 50 right
     * of the centre and 25 below it. The depth cue is 0x100 * 0x8000 +
     * 0x10000 = 0x810000, and IR0 its top bits. */
    test_Reset(g_test_identity, NULL, NULL);
    test_SetScreen();
    test_SetVector(0, 100, 50, 1000);
    port_GteWriteData(PORT_GTE_SXY2, HALVES(7, 8));
    port_GteWriteData(PORT_GTE_SZ3, 9);
    port_GteCommand(RTPS);
    CHECK_MAC(100, 50, 1000);
    CHECK_IR(100, 50, 1000);
    CHECK_EQ(DATA(PORT_GTE_SZ3), 1000);
    CHECK_EQ(DATA(PORT_GTE_SZ2), 9);
    CHECK_EQ(DATA(PORT_GTE_SXY2), HALVES(210, 145));
    CHECK_EQ(DATA(PORT_GTE_SXY1), HALVES(7, 8));
    CHECK_EQ(DATA(PORT_GTE_MAC0), 0x810000);
    CHECK_EQ(DATA(PORT_GTE_IR0), 0x810);
    CHECK_FLAG(0);

    /* The translation moves it: 100 further right, 200 further away.
     * h / z = 500 / 1200, so x = 160 + 200 * 500 / 1200 = 243.3. */
    port_GteWriteControl(PORT_GTE_TRX, 100);
    port_GteWriteControl(PORT_GTE_TRZ, 200);
    port_GteCommand(RTPS);
    CHECK_MAC(200, 50, 1200);
    CHECK_EQ(DATA(PORT_GTE_SZ3), 1200);
    CHECK_EQ((s16)DATA(PORT_GTE_SXY2), 243);
    CHECK_FLAG(0);

    /* A point at depth 0, 1000 to the right: the division overflows and
     * gives 0x1FFFF; x = 160 + 2000 does not fit and is held at 0x3FF;
     * the depth cue is past one and IR0 is held at 0x1000. */
    test_Reset(g_test_identity, NULL, NULL);
    test_SetScreen();
    test_SetVector(0, 1000, 0, 0);
    port_GteCommand(RTPS);
    CHECK_EQ(DATA(PORT_GTE_SZ3), 0);
    CHECK_EQ(DATA(PORT_GTE_SXY2), HALVES(0x3FF, 120));
    CHECK_EQ(DATA(PORT_GTE_MAC0), 0x1FFFF * 0x100 + 0x10000);
    CHECK_EQ(DATA(PORT_GTE_IR0), 0x1000);
    CHECK_FLAG(PORT_GTE_FLAG_ERROR | PORT_GTE_FLAG_DIVIDE | PORT_GTE_FLAG_SX |
               PORT_GTE_FLAG_IR0);

    /* A point to the left and above, behind the eye: the depth is held
     * at 0, the division overflows, x and y are held at -0x400. */
    test_SetVector(0, -1000, -1000, -5);
    port_GteCommand(RTPS);
    CHECK_IR(-1000, -1000, -5);
    CHECK_EQ(DATA(PORT_GTE_SZ3), 0);
    CHECK_EQ(DATA(PORT_GTE_SXY2), HALVES(-0x400, -0x400));
    CHECK_FLAG(
        PORT_GTE_FLAG_ERROR | PORT_GTE_FLAG_DEPTH | PORT_GTE_FLAG_DIVIDE |
        PORT_GTE_FLAG_SX | PORT_GTE_FLAG_SY | PORT_GTE_FLAG_IR0);

    /* A point 0x20000 away: IR3 is held at 0x7FFF and the depth at
     * 0xFFFF, with their two bits. h / z is 500 / 0xFFFF, next to
     * nothing: the point is at the centre. */
    test_Reset(g_test_identity, NULL, NULL);
    test_SetScreen();
    port_GteWriteControl(PORT_GTE_TRZ, 0x20000);
    test_SetVector(0, 100, 50, 0);
    port_GteCommand(RTPS);
    CHECK_MAC(100, 50, 0x20000);
    CHECK_IR(100, 50, 0x7FFF);
    CHECK_EQ(DATA(PORT_GTE_SZ3), 0xFFFF);
    CHECK_EQ(DATA(PORT_GTE_SXY2), HALVES(160, 120));
    CHECK_FLAG(
        PORT_GTE_FLAG_ERROR | (PORT_GTE_FLAG_IR1 >> 2) | PORT_GTE_FLAG_DEPTH);

    /* Three points at once: each is pushed, and the depth cue is the
     * last one's. Depths 500, 1000 and 2000 give h / z of 1, a half and
     * a quarter. */
    test_Reset(g_test_identity, NULL, NULL);
    test_SetScreen();
    test_SetVector(0, 10, 20, 500);
    test_SetVector(1, 100, 50, 1000);
    test_SetVector(2, -40, -80, 2000);
    port_GteCommand(RTPT);
    CHECK_EQ(DATA(PORT_GTE_SXY0), HALVES(170, 140));
    CHECK_EQ(DATA(PORT_GTE_SXY1), HALVES(210, 145));
    CHECK_EQ(DATA(PORT_GTE_SXY2), HALVES(150, 100));
    CHECK_EQ(DATA(PORT_GTE_SZ0), 0);
    CHECK_EQ(DATA(PORT_GTE_SZ1), 500);
    CHECK_EQ(DATA(PORT_GTE_SZ2), 1000);
    CHECK_EQ(DATA(PORT_GTE_SZ3), 2000);
    CHECK_MAC(-40, -80, 2000);
    CHECK_EQ(DATA(PORT_GTE_MAC0), 0x100 * 0x4000 + 0x10000);
    CHECK_EQ(DATA(PORT_GTE_IR0), 0x410);
    CHECK_FLAG(0);
}

/* The division against the same division done exactly. The coprocessor's
 * quotient is not always the true one: measured over 46 million pairs it
 * is the true quotient, rounded, three times in four, and otherwise one,
 * seldom two and very seldom three from it. So here the quotient times
 * the divisor is held to within four divisors of the dividend, for every
 * divisor of sixteen dividends. A sample of the quotients is printed for
 * the Python side, which divides for itself. */
static void test_Division(void) {
    static const u16 dividends[] = {
        0,   1,    2,      3,      100,    255,    256,    499,
        500, 1000, 0x1FFF, 0x2000, 0x7FFF, 0x8000, 0xFFFE, 0xFFFF,
    };
    s32 off = 0;
    s32 tried = 0;
    s32 overflows = 0;
    u32 i;
    u32 depth;

    for (i = 0; i < sizeof(dividends) / sizeof(dividends[0]); i++) {
        u32 h = dividends[i];

        for (depth = 0; depth <= 0xFFFF; depth++) {
            u32 quotient;
            s64 error;

            g_port_gte.control[PORT_GTE_FLAG] = 0;
            quotient = port_GteDivide(h, depth);
            if (h >= 2 * depth) {
                /* The quotient would be two or more. */
                if (quotient != 0x1FFFF ||
                    g_port_gte.control[PORT_GTE_FLAG] != PORT_GTE_FLAG_DIVIDE) {
                    off++;
                }
                overflows++;
                continue;
            }
            tried++;
            error = (s64)quotient * depth - ((s64)h << 16);
            if (g_port_gte.control[PORT_GTE_FLAG] != 0 ||
                error > 4 * (s64)depth || error < -4 * (s64)depth) {
                off++;
            }
            if ((depth * 7 + i) % 1021 == 0) {
                test_Print("divide %u %u %u\n", h, depth, quotient);
            }
        }
    }
    CHECK_EQ(off, 0);
    CHECK(tried > 500000);
    CHECK(overflows > 100000);

    /* By hand: 1 / 1 is one; 500 / 1000 a half; 0 / 1 nothing; the
     * largest that does not overflow, 0xFFFF / 0x8000. */
    CHECK_EQ(port_GteDivide(1, 1), 0x10000);
    CHECK_EQ(port_GteDivide(500, 1000), 0x8000);
    CHECK_EQ(port_GteDivide(0, 1), 0);
    CHECK_EQ(port_GteDivide(0xFFFF, 0x8000), 0x1FFFE);
    CHECK_EQ(port_GteDivide(0, 0), 0x1FFFF);
}

/* ---- sums ---- */

static void test_Sums(void) {
    /* NCLIP: the triangle (0, 0), (10, 0), (0, 10) is 50 in area, and
     * winds the other way with two points swapped. */
    test_Reset(NULL, NULL, NULL);
    port_GteWriteData(PORT_GTE_SXY0, HALVES(0, 0));
    port_GteWriteData(PORT_GTE_SXY1, HALVES(10, 0));
    port_GteWriteData(PORT_GTE_SXY2, HALVES(0, 10));
    port_GteCommand(NCLIP);
    CHECK_EQ(DATA(PORT_GTE_MAC0), 100);
    CHECK_FLAG(0);
    port_GteWriteData(PORT_GTE_SXY1, HALVES(0, 10));
    port_GteWriteData(PORT_GTE_SXY2, HALVES(10, 0));
    port_GteCommand(NCLIP);
    CHECK_EQ(DATA(PORT_GTE_MAC0), -100);
    CHECK_FLAG(0);
    /* The largest triangle there is: twice its area is 0xFFFE0001,
     * which does not fit 31 bits. */
    port_GteWriteData(PORT_GTE_SXY0, HALVES(-0x8000, -0x8000));
    port_GteWriteData(PORT_GTE_SXY1, HALVES(0x7FFF, -0x8000));
    port_GteWriteData(PORT_GTE_SXY2, HALVES(0x7FFF, 0x7FFF));
    port_GteCommand(NCLIP);
    CHECK_EQ(DATA(PORT_GTE_MAC0), 0xFFFE0001);
    CHECK_FLAG(PORT_GTE_FLAG_ERROR | PORT_GTE_FLAG_MAC0_POSITIVE);

    /* AVSZ3: (100 + 200 + 300) * 0x555 / 0x1000 = 199.9. AVSZ4 takes
     * the fourth depth and the other scale: 1000 * 0x400 / 0x1000. */
    test_Reset(NULL, NULL, NULL);
    port_GteWriteData(PORT_GTE_SZ0, 400);
    port_GteWriteData(PORT_GTE_SZ1, 100);
    port_GteWriteData(PORT_GTE_SZ2, 200);
    port_GteWriteData(PORT_GTE_SZ3, 300);
    port_GteWriteControl(PORT_GTE_ZSF3, 0x555);
    port_GteWriteControl(PORT_GTE_ZSF4, 0x400);
    port_GteCommand(AVSZ3);
    CHECK_EQ(DATA(PORT_GTE_MAC0), 600 * 0x555);
    CHECK_EQ(DATA(PORT_GTE_OTZ), 199);
    CHECK_FLAG(0);
    port_GteCommand(AVSZ4);
    CHECK_EQ(DATA(PORT_GTE_MAC0), 1000 * 0x400);
    CHECK_EQ(DATA(PORT_GTE_OTZ), 250);
    CHECK_FLAG(0);
    /* Three depths of 0xFFFF times 0x7FFF is 6442156035: past 31 bits,
     * and OTZ is held at 0xFFFF. A negative scale holds it at 0. */
    port_GteWriteData(PORT_GTE_SZ1, 0xFFFF);
    port_GteWriteData(PORT_GTE_SZ2, 0xFFFF);
    port_GteWriteData(PORT_GTE_SZ3, 0xFFFF);
    port_GteWriteControl(PORT_GTE_ZSF3, 0x7FFF);
    port_GteCommand(AVSZ3);
    CHECK_EQ(DATA(PORT_GTE_MAC0), 0x7FFB8003);
    CHECK_EQ(DATA(PORT_GTE_OTZ), 0xFFFF);
    CHECK_FLAG(PORT_GTE_FLAG_ERROR | PORT_GTE_FLAG_MAC0_POSITIVE |
               PORT_GTE_FLAG_DEPTH);
    port_GteWriteControl(PORT_GTE_ZSF3, (u32)-1);
    port_GteCommand(AVSZ3);
    CHECK_EQ(DATA(PORT_GTE_MAC0), -3 * 0xFFFF);
    CHECK_EQ(DATA(PORT_GTE_OTZ), 0);
    CHECK_FLAG(PORT_GTE_FLAG_ERROR | PORT_GTE_FLAG_DEPTH);
}

/* ---- vectors ---- */

static void test_Vectors(void) {
    /* A quarter turn about x: y goes to z and z to minus y. */
    static const s16 quarter[9] = {ONE, 0, 0, 0, 0, ONE, 0, -ONE, 0};
    static const s16 diagonal[9] = {1, 0, 0, 0, 2, 0, 0, 0, 3};
    static const s16 largest[9] = {0x7FFF, 0, 0, 0, 0, 0, 0, 0, 0};

    /* SQR, the fraction kept: 0x7FFF squared does not fit IR3. */
    test_Reset(NULL, NULL, NULL);
    test_SetIr(0, 3, -4, 0x7FFF);
    port_GteCommand(SQR_SF0);
    CHECK_MAC(9, 16, 0x3FFF0001);
    CHECK_IR(9, 16, 0x7FFF);
    CHECK_FLAG(PORT_GTE_FLAG_IR1 >> 2);
    /* And dropped: one squared is one, two squared four. */
    test_SetIr(0, ONE, 2 * ONE, -ONE);
    port_GteCommand(SQR);
    CHECK_MAC(ONE, 4 * ONE, ONE);
    CHECK_IR(ONE, 4 * ONE, ONE);
    CHECK_FLAG(0);

    /* OP: (1, 2, 3) x (4, 5, 6) = (-3, 6, -3). Held from 0, the two
     * negative numbers are 0, with their bits. */
    test_Reset(diagonal, NULL, NULL);
    test_SetIr(0, 4, 5, 6);
    port_GteCommand(OP_SF0);
    CHECK_MAC(-3, 6, -3);
    CHECK_IR(-3, 6, -3);
    CHECK_FLAG(0);
    test_SetIr(0, 4, 5, 6);
    port_GteCommand(OP_SF0 | PORT_GTE_OP_LM);
    CHECK_MAC(-3, 6, -3);
    CHECK_IR(0, 6, 0);
    CHECK_FLAG(
        PORT_GTE_FLAG_ERROR | PORT_GTE_FLAG_IR1 | (PORT_GTE_FLAG_IR1 >> 2));

    /* MVMVA: the turn, with the translation and without. */
    test_Reset(quarter, NULL, NULL);
    port_GteWriteControl(PORT_GTE_TRX, 1);
    port_GteWriteControl(PORT_GTE_TRY, 2);
    port_GteWriteControl(PORT_GTE_TRZ, 3);
    test_SetVector(0, 10, 20, 30);
    port_GteCommand(MVMVA_RT_V0_TR);
    CHECK_MAC(11, 32, -17);
    CHECK_IR(11, 32, -17);
    CHECK_FLAG(0);
    port_GteCommand(MVMVA_RT_V0);
    CHECK_MAC(10, 30, -20);
    CHECK_FLAG(0);
    /* With the fraction kept the sums are 0x1000 times as large, and
     * none fits its IR. */
    port_GteCommand(MVMVA_RT_V0_TR_SF0);
    CHECK_MAC(11 * ONE, 32 * ONE, -17 * ONE);
    CHECK_IR(0x7FFF, 0x7FFF, -0x8000);
    CHECK_FLAG(PORT_GTE_FLAG_ERROR | PORT_GTE_FLAG_IR1 |
               (PORT_GTE_FLAG_IR1 >> 1) | (PORT_GTE_FLAG_IR1 >> 2));

    /* The largest translation and the largest product: the sum,
     * 0x7FFFFFFF000 + 0x3FFF0001, is past 43 bits. It wraps to a
     * negative number, 0x8003FFEF001 of 44 bits, whose top 32 are
     * MAC1. */
    test_Reset(largest, NULL, NULL);
    port_GteWriteControl(PORT_GTE_TRX, 0x7FFFFFFF);
    test_SetVector(0, 0x7FFF, 0, 0);
    port_GteCommand(MVMVA_RT_V0_TR);
    CHECK_MAC(0x8003FFEF, 0, 0);
    CHECK_IR(-0x8000, 0, 0);
    CHECK_FLAG(
        PORT_GTE_FLAG_ERROR | PORT_GTE_FLAG_MAC1_POSITIVE | PORT_GTE_FLAG_IR1);
    /* And the other way: -0x80000000000 - 0x3FFF8000. */
    port_GteWriteControl(PORT_GTE_TRX, 0x80000000);
    test_SetVector(0, -0x8000, 0, 0);
    port_GteCommand(MVMVA_RT_V0_TR);
    CHECK_MAC(0x7FFC0008, 0, 0);
    CHECK_IR(0x7FFF, 0, 0);
    CHECK_FLAG(
        PORT_GTE_FLAG_ERROR | PORT_GTE_FLAG_MAC1_NEGATIVE | PORT_GTE_FLAG_IR1);
}

/* ---- colours ---- */

#define COLOR(r, g, b, code)                                                   \
    ((u32)(r) | ((u32)(g) << 8) | ((u32)(b) << 16) | ((u32)(code) << 24))

static void test_Colors(void) {
    /* One light, along z; it is (0x800, 0x400, 0x200) in colour: a
     * half, a quarter and an eighth. */
    static const s16 light[9] = {0, 0, ONE, 0, 0, 0, 0, 0, 0};
    static const s16 light_color[9] = {0x800, 0, 0, 0x400, 0, 0, 0x200, 0, 0};

    /* DPCS: a colour half way to the far colour. Red goes from 0x40 to
     * 0x100, green is the far colour's already, blue goes from 0xC0 to
     * 0. (The far colour is in sixteenths of a colour's unit.) */
    test_Reset(NULL, NULL, NULL);
    test_SetFarColor(0x1000, 0x800, 0);
    test_SetIr(0x800, 0, 0, 0);
    port_GteWriteData(PORT_GTE_RGBC, COLOR(0x40, 0x80, 0xC0, 0x2C));
    port_GteWriteData(PORT_GTE_RGB1, 0x11111111);
    port_GteWriteData(PORT_GTE_RGB2, 0x22222222);
    port_GteCommand(DPCS);
    CHECK_MAC(0xA00, 0x800, 0x600);
    CHECK_IR(0xA00, 0x800, 0x600);
    CHECK_EQ(DATA(PORT_GTE_RGB0), 0x11111111);
    CHECK_EQ(DATA(PORT_GTE_RGB1), 0x22222222);
    CHECK_EQ(DATA(PORT_GTE_RGB2), COLOR(0xA0, 0x80, 0x60, 0x2C));
    CHECK_FLAG(0);
    /* A far colour out of all proportion: the difference does not fit
     * 16 bits and is held at 0x7FFF, which IR0 halves: 0x40 + 0x3FF
     * sixteenths more than a byte holds. */
    test_SetFarColor(0x7FFFFFFF, 0x800, 0);
    port_GteCommand(DPCS);
    CHECK_MAC(0x400 + 0x3FFF, 0x800, 0x600);
    CHECK_EQ(DATA(PORT_GTE_RGB2), COLOR(0xFF, 0x80, 0x60, 0x2C));
    CHECK_FLAG(PORT_GTE_FLAG_ERROR | PORT_GTE_FLAG_IR1 | PORT_GTE_FLAG_RED);

    /* DPCT does that to the three colours, oldest first, so each comes
     * back to its place; with IR0 at 0 each is as it was, but for its
     * code, which is RGBC's. */
    test_Reset(NULL, NULL, NULL);
    test_SetFarColor(0x1000, 0x800, 0);
    port_GteWriteData(PORT_GTE_RGBC, COLOR(1, 2, 3, 0x2C));
    port_GteWriteData(PORT_GTE_RGB0, COLOR(0x10, 0x20, 0x30, 0x77));
    port_GteWriteData(PORT_GTE_RGB1, COLOR(0x40, 0x50, 0x60, 0x77));
    port_GteWriteData(PORT_GTE_RGB2, COLOR(0x70, 0x80, 0x90, 0x77));
    port_GteCommand(DPCT);
    CHECK_EQ(DATA(PORT_GTE_RGB0), COLOR(0x10, 0x20, 0x30, 0x2C));
    CHECK_EQ(DATA(PORT_GTE_RGB1), COLOR(0x40, 0x50, 0x60, 0x2C));
    CHECK_EQ(DATA(PORT_GTE_RGB2), COLOR(0x70, 0x80, 0x90, 0x2C));
    CHECK_MAC(0x700, 0x800, 0x900);
    CHECK_FLAG(0);

    /* INTPL: IR all the way to the far colour is the far colour. */
    test_Reset(NULL, NULL, NULL);
    test_SetFarColor(0x400, 0x500, 0x600);
    test_SetIr(ONE, 0x100, 0x200, 0x300);
    port_GteCommand(INTPL);
    CHECK_MAC(0x400, 0x500, 0x600);
    CHECK_EQ(DATA(PORT_GTE_RGB2), COLOR(0x40, 0x50, 0x60, 0));
    CHECK_FLAG(0);

    /* GPF: IR times a half. 0x3FFF is more than a colour's byte holds
     * sixteen times over. */
    test_Reset(NULL, NULL, NULL);
    test_SetIr(0x800, 0x100, 0x200, 0x7FFF);
    port_GteCommand(GPF);
    CHECK_MAC(0x80, 0x100, 0x3FFF);
    CHECK_IR(0x80, 0x100, 0x3FFF);
    CHECK_EQ(DATA(PORT_GTE_RGB2), COLOR(0x08, 0x10, 0xFF, 0));
    CHECK_FLAG(PORT_GTE_FLAG_RED >> 2);
    /* GPL: MAC plus IR times one. A negative colour is held at 0. */
    port_GteWriteData(PORT_GTE_MAC1, 0x10);
    port_GteWriteData(PORT_GTE_MAC2, 0x20);
    port_GteWriteData(PORT_GTE_MAC3, (u32)-0x30);
    test_SetIr(ONE, 1, 2, 3);
    port_GteCommand(GPL);
    CHECK_MAC(0x11, 0x22, -0x2D);
    CHECK_IR(0x11, 0x22, -0x2D);
    CHECK_EQ(DATA(PORT_GTE_RGB2), COLOR(1, 2, 0, 0));
    CHECK_FLAG(PORT_GTE_FLAG_RED >> 2);

    /* NCS: a normal facing the light is lit in full: the light's colour
     * and the background's 0x100 of red. */
    test_Reset(NULL, light, light_color);
    port_GteWriteControl(PORT_GTE_RBK, 0x100);
    port_GteWriteData(PORT_GTE_RGBC, COLOR(0x80, 0x80, 0x80, 0x34));
    test_SetVector(0, 0, 0, ONE);
    port_GteCommand(NCS);
    CHECK_MAC(0x900, 0x400, 0x200);
    CHECK_EQ(DATA(PORT_GTE_RGB2), COLOR(0x90, 0x40, 0x20, 0x34));
    CHECK_FLAG(0);
    /* Facing away it gets the background alone: the light's share is
     * negative and is held at 0. */
    test_SetVector(0, 0, 0, -ONE);
    port_GteCommand(NCS);
    CHECK_MAC(0x100, 0, 0);
    CHECK_EQ(DATA(PORT_GTE_RGB2), COLOR(0x10, 0, 0, 0x34));
    CHECK_FLAG(PORT_GTE_FLAG_ERROR | PORT_GTE_FLAG_IR1);

    /* NCT: the two, and one side on, which is half lit. */
    test_SetVector(0, 0, 0, ONE);
    test_SetVector(1, 0, 0, -ONE);
    test_SetVector(2, 0, 0, ONE / 2);
    port_GteCommand(NCT);
    CHECK_EQ(DATA(PORT_GTE_RGB0), COLOR(0x90, 0x40, 0x20, 0x34));
    CHECK_EQ(DATA(PORT_GTE_RGB1), COLOR(0x10, 0, 0, 0x34));
    CHECK_EQ(DATA(PORT_GTE_RGB2), COLOR(0x50, 0x20, 0x10, 0x34));
    CHECK_FLAG(PORT_GTE_FLAG_ERROR | PORT_GTE_FLAG_IR1);

    /* NCCS: the light times the colour, which is a half. */
    test_SetVector(0, 0, 0, ONE);
    port_GteCommand(NCCS);
    CHECK_MAC(0x480, 0x200, 0x100);
    CHECK_EQ(DATA(PORT_GTE_RGB2), COLOR(0x48, 0x20, 0x10, 0x34));
    CHECK_FLAG(0);
    /* NCDS: and that faded, not at all and all the way. */
    test_SetFarColor(0xFF0, 0x800, 0);
    test_SetIr(0, 0, 0, 0);
    port_GteCommand(NCDS);
    CHECK_EQ(DATA(PORT_GTE_RGB2), COLOR(0x48, 0x20, 0x10, 0x34));
    test_SetIr(ONE, 0, 0, 0);
    port_GteCommand(NCDS);
    CHECK_MAC(0xFF0, 0x800, 0);
    CHECK_EQ(DATA(PORT_GTE_RGB2), COLOR(0xFF, 0x80, 0, 0x34));
    CHECK_FLAG(0);

    /* CC: the same from IR, with no normal. */
    test_SetIr(0, ONE, 0, 0);
    port_GteCommand(CC);
    CHECK_MAC(0x480, 0x200, 0x100);
    CHECK_EQ(DATA(PORT_GTE_RGB2), COLOR(0x48, 0x20, 0x10, 0x34));
    CHECK_FLAG(0);

    /* DCPL: the colour times IR, faded not at all. */
    test_SetIr(0, ONE, 0x800, 0x400);
    port_GteCommand(DCPL);
    CHECK_MAC(0x800, 0x400, 0x200);
    CHECK_EQ(DATA(PORT_GTE_RGB2), COLOR(0x80, 0x40, 0x20, 0x34));
    CHECK_FLAG(0);
}

void test_Main(void) {
    test_Registers();
    test_Projection();
    test_Division();
    test_Sums();
    test_Vectors();
    test_Colors();
}
