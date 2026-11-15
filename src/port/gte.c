/* The GTE in C, for the native port (port_gte.h, docs/PORT.md): the
 * registers of the PlayStation's geometry coprocessor and every operation
 * it has, written from the public descriptions of the hardware. It is
 * held to the emulator's coprocessor register for register by a play test
 * (tests/play/gte_model.toml) and to cases worked out by hand by a host
 * test (tests/port/gte.c). */
#include "common.h"
#include "bb2.h"

PortGte g_port_gte;

/* The widths of the sums: MAC1 to MAC3 are kept to 44 bits while they are
 * added up, MAC0 to 32. */
#define MAC_SUM_MAX 0x7FFFFFFFFFFLL
#define MAC_SUM_MIN (-0x80000000000LL)
#define MAC_SUM_SPARE_BITS 20
#define MAC0_MAX 0x7FFFFFFFLL
#define MAC0_MIN (-0x80000000LL)

/* What the limited registers hold at most and at least. */
#define IR_MAX 0x7FFF
#define IR_MIN (-0x8000)
#define IR0_MAX 0x1000
#define COLOR_MAX 0xFF
#define DEPTH_MAX 0xFFFF
#define SCREEN_MAX 0x3FF
#define SCREEN_MIN (-0x400)
#define DIVIDE_MAX 0x1FFFF

/* The fraction of a fixed-point number: what an operation drops from its
 * results when it is asked to, and what a translation is moved up by. */
#define FRACTION_BITS 12

/* A matrix: nine numbers, a row after a row, in five registers; the
 * three matrices are eight registers apart. */
#define MATRIX_ELEMENTS 9
#define MATRIX_REGISTERS 8

enum { M11, M12, M13, M21, M22, M23, M31, M32, M33 };

#define SIGN_BIT 0x80000000U
#define WORD_BITS 32
#define BYTE_BITS 8

/* IRGB and ORGB: IR1 to IR3 as five bits each, a 0x80th of each. */
#define IRGB_BITS 5
#define IRGB_MAX 0x1F
#define IRGB_SHIFT 7

/* The division's first guess is never below this. */
#define DIVIDE_GUESS_MIN 0x101

/* The three vectors MVMVA can add that are not "none": the translation,
 * the background colour, the far colour. */
enum { ADD_TRANSLATION, ADD_BACKGROUND, ADD_FAR_COLOR, ADD_NOTHING };

/* ---- the registers, read as numbers ---- */

static s32 port_GteData16(s32 index) { return (s16)g_port_gte.data[index]; }

static s32 port_GteControl32(s32 index) {
    return (s32)g_port_gte.control[index];
}

/* Vector `number` (0 to 2) as three numbers. */
static void port_GteGetVector(s32 number, s32 *vector) {
    u32 xy = g_port_gte.data[PORT_GTE_VXY0 + 2 * number];

    vector[0] = (s16)xy;
    vector[1] = (s16)(xy >> 16);
    vector[2] = port_GteData16(PORT_GTE_VZ0 + 2 * number);
}

static void port_GteGetIr(s32 *vector) {
    vector[0] = port_GteData16(PORT_GTE_IR1);
    vector[1] = port_GteData16(PORT_GTE_IR2);
    vector[2] = port_GteData16(PORT_GTE_IR3);
}

/* The matrix whose first register is `first`, as nine numbers. */
static void port_GteGetMatrix(s32 first, s32 *matrix) {
    s32 i;

    for (i = 0; i < MATRIX_ELEMENTS; i++) {
        u32 word = g_port_gte.control[first + i / 2];

        matrix[i] = (s16)((i & 1) ? word >> 16 : word);
    }
}

/* A colour register's byte `index`: red, green, blue, the code. */
static s32 port_GteColorByte(s32 reg, s32 index) {
    return (g_port_gte.data[reg] >> (BYTE_BITS * index)) & 0xFF;
}

/* ---- the limits ---- */

static void port_GteSetFlag(u32 bits) {
    g_port_gte.control[PORT_GTE_FLAG] |= bits;
}

/* A step of the sum that becomes MAC `number` (1 to 3): it is held to 44
 * bits, and FLAG says so if it did not fit. */
static s64 port_GteCheckSum(s32 number, s64 sum) {
    if (sum > MAC_SUM_MAX) {
        port_GteSetFlag(PORT_GTE_FLAG_MAC1_POSITIVE >> (number - 1));
    } else if (sum < MAC_SUM_MIN) {
        port_GteSetFlag(PORT_GTE_FLAG_MAC1_NEGATIVE >> (number - 1));
    }
    return (s64)((u64)sum << MAC_SUM_SPARE_BITS) >> MAC_SUM_SPARE_BITS;
}

/* The sum that becomes MAC0: FLAG says if it does not fit 32 bits. */
static s64 port_GteCheckMac0(s64 sum) {
    if (sum > MAC0_MAX) {
        port_GteSetFlag(PORT_GTE_FLAG_MAC0_POSITIVE);
    } else if (sum < MAC0_MIN) {
        port_GteSetFlag(PORT_GTE_FLAG_MAC0_NEGATIVE);
    }
    return sum;
}

/* `value` held between two bounds; FLAG gets `limit_bit` if it was
 * outside. */
static s32 port_GteLimit(s64 value, s32 low, s32 high, u32 limit_bit) {
    if (value < low) {
        port_GteSetFlag(limit_bit);
        return low;
    }
    if (value > high) {
        port_GteSetFlag(limit_bit);
        return high;
    }
    return (s32)value;
}

/* MAC `number` (1 to 3) goes into its IR, limited. */
static void port_GteSetIr(s32 number, s32 value, s32 lm) {
    g_port_gte.data[PORT_GTE_IR0 + number] = (u32)port_GteLimit(
        value, lm ? 0 : IR_MIN, IR_MAX, PORT_GTE_FLAG_IR1 >> (number - 1));
}

/* The three sums become MAC1 to MAC3, less their fraction, and IR1 to
 * IR3. */
static void port_GteSetMacAndIr(const s64 *sums, s32 shift, s32 lm) {
    s32 i;

    for (i = 0; i < 3; i++) {
        s32 mac = (s32)(sums[i] >> shift);

        g_port_gte.data[PORT_GTE_MAC1 + i] = (u32)mac;
        port_GteSetIr(i + 1, mac, lm);
    }
}

/* ---- the three queues ---- */

/* What the projection worked out for the points now in SXY0 to SXY2,
 * before the hardware cut them down to whole pixels and held them to
 * eleven bits: the remastered renderer's side channel
 * (port_remaster.h; docs/PORT.md, "Remastered"). A point whose depth is
 * 0 is one this queue does not know: the program put it in the
 * register itself. Nothing of the game is changed by any of it. */
static PortPrecisePoint g_port_precise[3];

/* The point of the queue that an SXY register holds, or NULL. */
static PortPrecisePoint *port_PreciseOf(s32 index) {
    switch (index) {
    case PORT_GTE_SXY0:
        return &g_port_precise[0];
    case PORT_GTE_SXY1:
        return &g_port_precise[1];
    case PORT_GTE_SXY2:
    case PORT_GTE_SXYP:
        return &g_port_precise[2];
    default:
        return NULL;
    }
}

static void port_GtePushScreenXy(u32 xy) {
    g_port_gte.data[PORT_GTE_SXY0] = g_port_gte.data[PORT_GTE_SXY1];
    g_port_gte.data[PORT_GTE_SXY1] = g_port_gte.data[PORT_GTE_SXY2];
    g_port_gte.data[PORT_GTE_SXY2] = xy;
    g_port_precise[0] = g_port_precise[1];
    g_port_precise[1] = g_port_precise[2];
    g_port_precise[2].x = 0;
    g_port_precise[2].y = 0;
    g_port_precise[2].depth = 0;
    g_port_precise[2].divided = 0;
}

static void port_GtePushScreenZ(s32 depth) {
    g_port_gte.data[PORT_GTE_SZ0] = g_port_gte.data[PORT_GTE_SZ1];
    g_port_gte.data[PORT_GTE_SZ1] = g_port_gte.data[PORT_GTE_SZ2];
    g_port_gte.data[PORT_GTE_SZ2] = g_port_gte.data[PORT_GTE_SZ3];
    g_port_gte.data[PORT_GTE_SZ3] =
        (u32)port_GteLimit(depth, 0, DEPTH_MAX, PORT_GTE_FLAG_DEPTH);
}

/* MAC1 to MAC3, a sixteenth of each, become the newest colour; its code
 * is RGBC's. */
static void port_GtePushColor(void) {
    u32 color = (u32)port_GteColorByte(PORT_GTE_RGBC, 3) << 24;
    s32 i;

    for (i = 0; i < 3; i++) {
        s32 mac = (s32)g_port_gte.data[PORT_GTE_MAC1 + i];

        color |=
            (u32)port_GteLimit(mac >> 4, 0, COLOR_MAX, PORT_GTE_FLAG_RED >> i)
            << (BYTE_BITS * i);
    }
    g_port_gte.data[PORT_GTE_RGB0] = g_port_gte.data[PORT_GTE_RGB1];
    g_port_gte.data[PORT_GTE_RGB1] = g_port_gte.data[PORT_GTE_RGB2];
    g_port_gte.data[PORT_GTE_RGB2] = color;
}

/* ---- the pieces the operations are made of ---- */

/* matrix * vector + (added << 12), each row added up in three steps; the
 * sums are left in `sums`. `added` may be NULL for nothing. */
static void port_GteMultiplyRows(
    const s32 *matrix, const s32 *vector, const s32 *added, s64 *sums) {
    s32 i;

    for (i = 0; i < 3; i++) {
        s64 sum = added ? (s64)added[i] << FRACTION_BITS : 0;

        sum = port_GteCheckSum(i + 1, sum + (s64)matrix[3 * i] * vector[0]);
        sum = port_GteCheckSum(i + 1, sum + (s64)matrix[3 * i + 1] * vector[1]);
        sums[i] =
            port_GteCheckSum(i + 1, sum + (s64)matrix[3 * i + 2] * vector[2]);
    }
}

/* MAC and IR = (matrix * vector + added) with the fraction dropped. */
static void port_GteTransform(
    s32 matrix_reg, const s32 *vector, s32 added_reg, s32 shift, s32 lm) {
    s32 matrix[MATRIX_ELEMENTS];
    s32 added[3];
    s64 sums[3];
    s32 i;

    port_GteGetMatrix(matrix_reg, matrix);
    for (i = 0; i < 3; i++) {
        added[i] = (added_reg < 0) ? 0 : port_GteControl32(added_reg + i);
    }
    port_GteMultiplyRows(matrix, vector, added, sums);
    port_GteSetMacAndIr(sums, shift, lm);
}

/* The three numbers of `sums` fade to the far colour by IR0 and become
 * MAC and IR: each is sum + (far - sum) * IR0. The difference is limited
 * as an IR is, to -0x8000 whatever `lm` says. */
static void port_GteFadeToFarColor(s64 *sums, s32 shift, s32 lm) {
    s32 factor = port_GteData16(PORT_GTE_IR0);
    s32 i;

    for (i = 0; i < 3; i++) {
        s64 far = (s64)port_GteControl32(PORT_GTE_RFC + i) << FRACTION_BITS;
        s32 difference = (s32)(port_GteCheckSum(i + 1, far - sums[i]) >> shift);

        difference =
            port_GteLimit(difference, IR_MIN, IR_MAX, PORT_GTE_FLAG_IR1 >> i);
        sums[i] = port_GteCheckSum(i + 1, (s64)difference * factor + sums[i]);
    }
    port_GteSetMacAndIr(sums, shift, lm);
}

/* sums = the colour of `color_reg` times IR1 to IR3, times sixteen. */
static void port_GteMultiplyColor(s32 color_reg, s64 *sums) {
    s32 i;

    for (i = 0; i < 3; i++) {
        sums[i] = (s64)(port_GteColorByte(color_reg, i) *
                        port_GteData16(PORT_GTE_IR1 + i))
                  << 4;
    }
}

/* How many of a number's top bits are the same as its sign: 1 to 32. */
static u32 port_GteLeadingBits(u32 value) {
    u32 count = 0;

    if (value & SIGN_BIT) {
        value = ~value;
    }
    while (count < WORD_BITS && !(value & SIGN_BIT)) {
        value <<= 1;
        count++;
    }
    return count;
}

/* h / depth as the coprocessor divides for the projection: 16 bits of
 * fraction, at most 0x1FFFF. It multiplies by a reciprocal it reaches in
 * two steps from a first guess of eight bits, so the last bits are not
 * always those of a true division. */
static u32 port_GteDivide(u32 h, u32 depth) {
    u32 shift;
    u32 numerator;
    u32 divisor;
    u32 guess;
    u32 reciprocal;
    u32 quotient;

    if (h >= depth * 2) {
        port_GteSetFlag(PORT_GTE_FLAG_DIVIDE);
        return DIVIDE_MAX;
    }
    /* Both are moved up until the divisor's top bit is set: it is then
     * 0x8000 to 0xFFFF, a number from 1 to 2 with 15 bits of fraction. */
    shift = port_GteLeadingBits(depth) - 16;
    numerator = h << shift;
    divisor = depth << shift;
    /* The first guess at 1 / divisor, from the divisor's top eight bits
     * under the first: the hardware has these 257 numbers in a table. It
     * is 0x101 to 0x200: a number from 0.5 to 1 with 9 bits of fraction,
     * rounded to nearest and never below 0x101. */
    guess = (0x40000 / (((divisor - 0x7FC0) >> 7) + 0x100) + 1) / 2;
    if (guess < DIVIDE_GUESS_MIN) {
        guess = DIVIDE_GUESS_MIN;
    }
    /* One step of Newton's rule, guess * (2 - divisor * guess), in two
     * multiplications that each round. */
    reciprocal = (0x2000080 - divisor * guess) >> 8; /* 2, and a half */
    reciprocal = (0x80 + reciprocal * guess) >> 8;   /* a half */
    /* 16 bits of fraction, rounded */
    quotient = (u32)(((u64)numerator * reciprocal + 0x8000) >> 16);
    return (quotient > DIVIDE_MAX) ? DIVIDE_MAX : quotient;
}

/* ---- the operations ---- */

/* RTPS and each third of RTPT: a point goes through the rotation and the
 * translation, and is projected. Only the last point of an operation
 * gets a depth cue. */
static void port_GteProject(const s32 *vector, s32 shift, s32 lm, s32 last) {
    s32 matrix[MATRIX_ELEMENTS];
    s32 translation[3];
    s64 sums[3] = {0, 0, 0};
    s32 depth;
    s32 mac3;
    s64 quotient;
    s64 sum;
    s32 x;
    s32 y;
    s32 precise_x;
    s32 precise_y;
    s32 i;

    port_GteGetMatrix(PORT_GTE_ROTATION, matrix);
    for (i = 0; i < 3; i++) {
        translation[i] = port_GteControl32(PORT_GTE_TRX + i);
    }
    port_GteMultiplyRows(matrix, vector, translation, sums);
    for (i = 0; i < 3; i++) {
        g_port_gte.data[PORT_GTE_MAC1 + i] = (u32)(s32)(sums[i] >> shift);
    }
    port_GteSetIr(1, (s32)g_port_gte.data[PORT_GTE_MAC1], lm);
    port_GteSetIr(2, (s32)g_port_gte.data[PORT_GTE_MAC2], lm);

    /* IR3 is not limited as the other two are. Its bit of FLAG is set by
     * the depth, which is the sum less 12 bits whatever the operation's
     * shift, against -0x8000 whatever `lm`; the register is MAC3 held to
     * its bounds, with no bit set for it. (With the shift of 12 and no
     * `lm`, as the game has it, that is the plain limit.) */
    depth = (s32)(sums[2] >> FRACTION_BITS);
    port_GteLimit(depth, IR_MIN, IR_MAX, PORT_GTE_FLAG_IR1 >> 2);
    mac3 = (s32)g_port_gte.data[PORT_GTE_MAC3];
    if (mac3 > IR_MAX) {
        mac3 = IR_MAX;
    } else if (mac3 < (lm ? 0 : IR_MIN)) {
        mac3 = lm ? 0 : IR_MIN;
    }
    g_port_gte.data[PORT_GTE_IR3] = (u32)mac3;

    port_GtePushScreenZ(depth);
    quotient = port_GteDivide(g_port_gte.control[PORT_GTE_PLANE] & 0xFFFF,
                              g_port_gte.data[PORT_GTE_SZ3]);

    sum = port_GteCheckMac0(quotient * port_GteData16(PORT_GTE_IR1) +
                            port_GteControl32(PORT_GTE_OFX));
    x = port_GteLimit(sum >> 16, SCREEN_MIN, SCREEN_MAX, PORT_GTE_FLAG_SX);
    precise_x = (s32)sum;
    sum = port_GteCheckMac0(quotient * port_GteData16(PORT_GTE_IR2) +
                            port_GteControl32(PORT_GTE_OFY));
    y = port_GteLimit(sum >> 16, SCREEN_MIN, SCREEN_MAX, PORT_GTE_FLAG_SY);
    precise_y = (s32)sum;
    port_GtePushScreenXy(((u32)x & 0xFFFF) | ((u32)y << 16));
    /* the side channel: where the point really came out, which the two
     * halves above have lost (port_remaster.h) */
    g_port_precise[2].x = precise_x;
    g_port_precise[2].y = precise_y;
    g_port_precise[2].depth = depth;
    g_port_precise[2].divided = (s32)quotient;

    /* The depth cue: how far the point is towards the far colour. The
     * first two points of RTPT get none, and so set none of its bits of
     * FLAG: that is the emulator's reading, taken here. */
    if (!last) {
        return;
    }
    sum = port_GteCheckMac0(quotient * port_GteControl32(PORT_GTE_DQA) +
                            port_GteControl32(PORT_GTE_DQB));
    g_port_gte.data[PORT_GTE_MAC0] = (u32)sum;
    g_port_gte.data[PORT_GTE_IR0] =
        (u32)port_GteLimit(sum >> FRACTION_BITS, 0, IR0_MAX, PORT_GTE_FLAG_IR0);
}

/* NCLIP: twice the area of the triangle of the three screen positions,
 * signed by the way it winds. */
static void port_GteNormalClip(void) {
    s32 x[3];
    s32 y[3];
    s32 i;
    s64 sum;

    for (i = 0; i < 3; i++) {
        u32 xy = g_port_gte.data[PORT_GTE_SXY0 + i];

        x[i] = (s16)xy;
        y[i] = (s16)(xy >> 16);
    }
    sum = (s64)x[0] * y[1] + (s64)x[1] * y[2] + (s64)x[2] * y[0];
    sum -= (s64)x[0] * y[2] + (s64)x[1] * y[0] + (s64)x[2] * y[1];
    g_port_gte.data[PORT_GTE_MAC0] = (u32)port_GteCheckMac0(sum);
}

/* AVSZ3 and AVSZ4: the last three or four depths, added and scaled, are
 * the ordering table's index. */
static void port_GteAverageDepth(s32 count) {
    s64 sum = 0;
    s32 i;

    for (i = 0; i < count; i++) {
        sum += g_port_gte.data[PORT_GTE_SZ3 - i];
    }
    sum = port_GteCheckMac0(
        sum * port_GteControl32((count == 3) ? PORT_GTE_ZSF3 : PORT_GTE_ZSF4));
    g_port_gte.data[PORT_GTE_MAC0] = (u32)sum;
    g_port_gte.data[PORT_GTE_OTZ] = (u32)port_GteLimit(
        sum >> FRACTION_BITS, 0, DEPTH_MAX, PORT_GTE_FLAG_DEPTH);
}

/* OP: the outer product of the rotation matrix's diagonal and IR. */
static void port_GteOuterProduct(s32 shift, s32 lm) {
    s32 matrix[MATRIX_ELEMENTS];
    s32 ir[3];
    s64 sums[3];

    port_GteGetMatrix(PORT_GTE_ROTATION, matrix);
    port_GteGetIr(ir);
    sums[0] = port_GteCheckSum(
        1, (s64)ir[2] * matrix[M22] - (s64)ir[1] * matrix[M33]);
    sums[1] = port_GteCheckSum(
        2, (s64)ir[0] * matrix[M33] - (s64)ir[2] * matrix[M11]);
    sums[2] = port_GteCheckSum(
        3, (s64)ir[1] * matrix[M11] - (s64)ir[0] * matrix[M22]);
    port_GteSetMacAndIr(sums, shift, lm);
}

/* SQR: each of IR1 to IR3 times itself. */
static void port_GteSquare(s32 shift, s32 lm) {
    s32 ir[3];
    s64 sums[3];
    s32 i;

    port_GteGetIr(ir);
    for (i = 0; i < 3; i++) {
        sums[i] = (s64)ir[i] * ir[i];
    }
    port_GteSetMacAndIr(sums, shift, lm);
}

/* MVMVA: one of the three matrices times one of four vectors, plus one of
 * three vectors or nothing. */
static void port_GteMultiplyAndAdd(u32 word, s32 shift, s32 lm) {
    static const s32 added_regs[3] = {PORT_GTE_TRX, PORT_GTE_RBK, PORT_GTE_RFC};
    s32 which_matrix = (word >> PORT_GTE_OP_MATRIX_SHIFT) & 3;
    s32 which_vector = (word >> PORT_GTE_OP_VECTOR_SHIFT) & 3;
    s32 which_added = (word >> PORT_GTE_OP_TRANSLATION_SHIFT) & 3;
    s32 matrix[MATRIX_ELEMENTS];
    s32 vector[3];
    s32 added[3];
    s64 sums[3];
    s32 i;

    if (which_matrix == 3) {
        /* No matrix has this number. The hardware multiplies by what its
         * wiring leaves there: RGBC's red, IR0 and two elements of the
         * rotation, each said three times. Not checked on a console; the
         * game never asks for it. */
        s32 red = port_GteColorByte(PORT_GTE_RGBC, 0) << 4;

        port_GteGetMatrix(PORT_GTE_ROTATION, matrix);
        matrix[M31] = matrix[M32] = matrix[M33] = matrix[M22];
        matrix[M21] = matrix[M22] = matrix[M23] = matrix[M13];
        matrix[M11] = -red;
        matrix[M12] = red;
        matrix[M13] = port_GteData16(PORT_GTE_IR0);
    } else {
        port_GteGetMatrix(
            PORT_GTE_ROTATION + MATRIX_REGISTERS * which_matrix, matrix);
    }
    if (which_vector == 3) {
        port_GteGetIr(vector);
    } else {
        port_GteGetVector(which_vector, vector);
    }
    for (i = 0; i < 3; i++) {
        added[i] = (which_added == ADD_NOTHING)
                       ? 0
                       : port_GteControl32(added_regs[which_added] + i);
    }

    if (which_added == ADD_FAR_COLOR) {
        /* Adding the far colour does not work on the hardware: the far
         * colour and the first column's product are added, limited as an
         * IR for what that does to FLAG, and thrown away; the result is
         * the other two columns' products. The game never asks for it. */
        for (i = 0; i < 3; i++) {
            s64 sum =
                port_GteCheckSum(i + 1, ((s64)added[i] << FRACTION_BITS) +
                                            (s64)matrix[3 * i] * vector[0]);

            port_GteLimit(
                (s32)(sum >> shift), IR_MIN, IR_MAX, PORT_GTE_FLAG_IR1 >> i);
            sum = port_GteCheckSum(i + 1, (s64)matrix[3 * i + 1] * vector[1]);
            sums[i] = port_GteCheckSum(
                i + 1, sum + (s64)matrix[3 * i + 2] * vector[2]);
        }
    } else {
        port_GteMultiplyRows(matrix, vector, added, sums);
    }
    port_GteSetMacAndIr(sums, shift, lm);
}

/* What the lighting operations have in common. A normal is lit: the
 * light matrix gives how much each of three lights falls on it, and the
 * light colour matrix, with the background colour, the colour of that
 * light. `with_color` multiplies it by RGBC; `with_fade` fades it to the
 * far colour. The result is the newest colour. */
static void port_GteLight(
    const s32 *normal, s32 shift, s32 lm, s32 with_color, s32 with_fade) {
    s32 ir[3];
    s64 sums[3];

    if (normal) {
        port_GteTransform(PORT_GTE_LIGHT, normal, -1, shift, lm);
    }
    port_GteGetIr(ir);
    port_GteTransform(PORT_GTE_LIGHT_COLOR, ir, PORT_GTE_RBK, shift, lm);
    if (with_color) {
        port_GteMultiplyColor(PORT_GTE_RGBC, sums);
        if (with_fade) {
            port_GteFadeToFarColor(sums, shift, lm);
        } else {
            port_GteSetMacAndIr(sums, shift, lm);
        }
    }
    port_GtePushColor();
}

/* DPCS and each third of DPCT: a colour fades to the far colour. */
static void port_GteFadeColor(s32 color_reg, s32 shift, s32 lm) {
    s64 sums[3];
    s32 i;

    for (i = 0; i < 3; i++) {
        sums[i] = (s64)port_GteColorByte(color_reg, i) << 16;
    }
    port_GteFadeToFarColor(sums, shift, lm);
    port_GtePushColor();
}

/* GPF and GPL: IR1 to IR3 times IR0, alone or added to MAC1 to MAC3. */
static void port_GteScale(s32 add_mac, s32 shift, s32 lm) {
    s32 factor = port_GteData16(PORT_GTE_IR0);
    s64 sums[3];
    s32 i;

    for (i = 0; i < 3; i++) {
        s64 sum = 0;

        if (add_mac) {
            sum = (s64)(s32)g_port_gte.data[PORT_GTE_MAC1 + i] << shift;
        }
        sums[i] = port_GteCheckSum(
            i + 1, sum + (s64)port_GteData16(PORT_GTE_IR1 + i) * factor);
    }
    port_GteSetMacAndIr(sums, shift, lm);
    port_GtePushColor();
}

/* One operation: FLAG is cleared, the word's low six bits say which
 * operation it is and three more of its bits how (the fraction dropped,
 * IR held from 0, and for MVMVA its three choices). */
void port_GteCommand(u32 word) {
    s32 shift = (word & PORT_GTE_OP_SF) ? FRACTION_BITS : 0;
    s32 lm = (word & PORT_GTE_OP_LM) != 0;
    s32 vector[3];
    s64 sums[3];
    s32 i;

    g_port_gte.control[PORT_GTE_FLAG] = 0;
    switch (word & PORT_GTE_OP_MASK) {
    case PORT_GTE_RTPS:
        port_GteGetVector(0, vector);
        port_GteProject(vector, shift, lm, TRUE);
        break;
    case PORT_GTE_RTPT:
        for (i = 0; i < 3; i++) {
            port_GteGetVector(i, vector);
            port_GteProject(vector, shift, lm, i == 2);
        }
        break;
    case PORT_GTE_NCLIP:
        port_GteNormalClip();
        break;
    case PORT_GTE_AVSZ3:
        port_GteAverageDepth(3);
        break;
    case PORT_GTE_AVSZ4:
        port_GteAverageDepth(4);
        break;
    case PORT_GTE_OP:
        port_GteOuterProduct(shift, lm);
        break;
    case PORT_GTE_SQR:
        port_GteSquare(shift, lm);
        break;
    case PORT_GTE_MVMVA:
        port_GteMultiplyAndAdd(word, shift, lm);
        break;
    case PORT_GTE_NCS:
    case PORT_GTE_NCCS:
    case PORT_GTE_NCDS:
        port_GteGetVector(0, vector);
        port_GteLight(
            vector, shift, lm, (word & PORT_GTE_OP_MASK) != PORT_GTE_NCS,
            (word & PORT_GTE_OP_MASK) == PORT_GTE_NCDS);
        break;
    case PORT_GTE_NCT:
    case PORT_GTE_NCCT:
    case PORT_GTE_NCDT:
        for (i = 0; i < 3; i++) {
            port_GteGetVector(i, vector);
            port_GteLight(
                vector, shift, lm, (word & PORT_GTE_OP_MASK) != PORT_GTE_NCT,
                (word & PORT_GTE_OP_MASK) == PORT_GTE_NCDT);
        }
        break;
    case PORT_GTE_CC:
        port_GteLight(NULL, shift, lm, TRUE, FALSE);
        break;
    case PORT_GTE_CDP:
        port_GteLight(NULL, shift, lm, TRUE, TRUE);
        break;
    case PORT_GTE_DCPL:
        port_GteMultiplyColor(PORT_GTE_RGBC, sums);
        port_GteFadeToFarColor(sums, shift, lm);
        port_GtePushColor();
        break;
    case PORT_GTE_DPCS:
        port_GteFadeColor(PORT_GTE_RGBC, shift, lm);
        break;
    case PORT_GTE_DPCT:
        for (i = 0; i < 3; i++) {
            port_GteFadeColor(PORT_GTE_RGB0, shift, lm);
        }
        break;
    case PORT_GTE_INTPL:
        for (i = 0; i < 3; i++) {
            sums[i] = (s64)port_GteData16(PORT_GTE_IR1 + i) << FRACTION_BITS;
        }
        port_GteFadeToFarColor(sums, shift, lm);
        port_GtePushColor();
        break;
    case PORT_GTE_GPF:
        port_GteScale(FALSE, shift, lm);
        break;
    case PORT_GTE_GPL:
        port_GteScale(TRUE, shift, lm);
        break;
    default:
        /* No such operation: the registers are left as they are. What
         * the hardware does with such a word is not modelled. */
        break;
    }
    if (g_port_gte.control[PORT_GTE_FLAG] & PORT_GTE_FLAG_ERROR_BITS) {
        g_port_gte.control[PORT_GTE_FLAG] |= PORT_GTE_FLAG_ERROR;
    }
}

/* ---- the registers, as the instructions reach them ---- */

/* IR1 to IR3 as five bits each, which is what registers 28 and 29 read. */
static u32 port_GteIrAsColor(void) {
    u32 color = 0;
    s32 i;

    for (i = 0; i < 3; i++) {
        s32 value = port_GteData16(PORT_GTE_IR1 + i) >> IRGB_SHIFT;

        if (value < 0) {
            value = 0;
        } else if (value > IRGB_MAX) {
            value = IRGB_MAX;
        }
        color |= (u32)value << (IRGB_BITS * i);
    }
    return color;
}

/* A data register written. The 16-bit ones keep 16 bits; two of them
 * write others; two cannot be written. */
void port_GteWriteData(s32 index, u32 value) {
    s32 i;

    switch (index) {
    case PORT_GTE_VZ0:
    case PORT_GTE_VZ1:
    case PORT_GTE_VZ2:
    case PORT_GTE_IR0:
    case PORT_GTE_IR1:
    case PORT_GTE_IR2:
    case PORT_GTE_IR3:
        g_port_gte.data[index] = (u32)(s32)(s16)value;
        break;
    case PORT_GTE_OTZ:
    case PORT_GTE_SZ0:
    case PORT_GTE_SZ1:
    case PORT_GTE_SZ2:
    case PORT_GTE_SZ3:
        g_port_gte.data[index] = value & 0xFFFF;
        break;
    case PORT_GTE_SXYP:
        port_GtePushScreenXy(value);
        break;
    case PORT_GTE_IRGB:
        for (i = 0; i < 3; i++) {
            g_port_gte.data[PORT_GTE_IR1 + i] =
                ((value >> (IRGB_BITS * i)) & IRGB_MAX) << IRGB_SHIFT;
        }
        break;
    case PORT_GTE_ORGB:
    case PORT_GTE_LZCR:
        /* read only */
        break;
    default: {
        /* a screen position the program puts in itself is not one the
         * projection worked out: the side channel forgets it
         * (port_remaster.h) */
        PortPrecisePoint *precise = port_PreciseOf(index);

        if (precise != NULL) {
            precise->depth = 0;
        }
        g_port_gte.data[index & (PORT_GTE_DATA_COUNT - 1)] = value;
        break;
    }
    }
}

/* A data register read: four of them are made from others. */
u32 port_GteReadData(s32 index) {
    switch (index) {
    case PORT_GTE_SXYP:
        return g_port_gte.data[PORT_GTE_SXY2];
    case PORT_GTE_IRGB:
    case PORT_GTE_ORGB:
        return port_GteIrAsColor();
    case PORT_GTE_LZCR:
        return port_GteLeadingBits(g_port_gte.data[PORT_GTE_LZCS]);
    default:
        return g_port_gte.data[index & (PORT_GTE_DATA_COUNT - 1)];
    }
}

/* A control register written: seven keep 16 bits, with their sign, and
 * FLAG its 19 bits and the one that sums them up. */
void port_GteWriteControl(s32 index, u32 value) {
    switch (index) {
    case PORT_GTE_ROTATION + 4:
    case PORT_GTE_LIGHT + 4:
    case PORT_GTE_LIGHT_COLOR + 4:
    case PORT_GTE_PLANE:
    case PORT_GTE_DQA:
    case PORT_GTE_ZSF3:
    case PORT_GTE_ZSF4:
        /* 16 bits. H too is read back with its sign, though the
         * projection takes it as unsigned. */
        g_port_gte.control[index] = (u32)(s32)(s16)value;
        break;
    case PORT_GTE_FLAG:
        value &= PORT_GTE_FLAG_WRITABLE;
        if (value & PORT_GTE_FLAG_ERROR_BITS) {
            value |= PORT_GTE_FLAG_ERROR;
        }
        g_port_gte.control[index] = value;
        break;
    default:
        g_port_gte.control[index & (PORT_GTE_CONTROL_COUNT - 1)] = value;
        break;
    }
}

u32 port_GteReadControl(s32 index) {
    return g_port_gte.control[index & (PORT_GTE_CONTROL_COUNT - 1)];
}

/* ---- runs of registers ---- */

void port_GteLoadData(s32 first, const void *words, s32 count) {
    s32 i;

    for (i = 0; i < count; i++) {
        port_GteWriteData(first + i, ((const u32 *)words)[i]);
    }
}

/* Where a screen position is stored is how the remastered renderer
 * finds what the projection really worked out for it: the renderer
 * stores the points of a part onto the scratchpad and names its faces'
 * corners among them, and a stage tile that is cut has the GTE store
 * its corners into the primitive itself (port_remaster.h; docs/PORT.md,
 * "Remastered"). Nothing else is different from a plain copy. */
void port_GteStoreData(s32 first, void *words, s32 count) {
    s32 i;

    for (i = 0; i < count; i++) {
        const PortPrecisePoint *precise = port_PreciseOf(first + i);

        ((u32 *)words)[i] = port_GteReadData(first + i);
        if (precise != NULL && precise->depth != 0) {
            port_RemasterNotePoint((PortAddress)&((u32 *)words)[i], precise);
        }
    }
}

void port_GteLoadControl(s32 first, const void *words, s32 count) {
    s32 i;

    for (i = 0; i < count; i++) {
        port_GteWriteControl(first + i, ((const u32 *)words)[i]);
    }
}

/* ---- the products of two matrices ---- */

void port_GteRotateColumns(const void *matrix, u32 command, s32 *columns) {
    const s16 *elements = (const s16 *)matrix;
    s32 column;

    for (column = 0; column < 3; column++) {
        port_GteWriteData(
            PORT_GTE_VXY0, ((u32)elements[M11 + column] & 0xFFFF) |
                               ((u32)elements[M21 + column] << 16));
        port_GteWriteData(PORT_GTE_VZ0, (u32)elements[M31 + column]);
        port_GteCommand(command);
        port_GteStoreData(PORT_GTE_IR1, &columns[3 * column], 3);
    }
}

/* The results of port_GteRotateColumns, a column after a column, into
 * a matrix's nine halfwords, a row after a row. */
void port_GteStoreColumns(void *matrix, const s32 *columns, s32 last_as_word) {
    s16 *elements = (s16 *)matrix;
    s32 row;
    s32 column;

    for (row = 0; row < 3; row++) {
        for (column = 0; column < 3; column++) {
            elements[3 * row + column] = (s16)columns[3 * column + row];
        }
    }
    if (last_as_word) {
        ((u32 *)matrix)[M33 / 2] = (u32)columns[M33];
    }
}
