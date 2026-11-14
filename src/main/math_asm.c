/* The assembly that is not the renderer's: moving memory, square roots and
 * lengths, blends and products of matrices, a few routines that only load
 * the GTE or read it back (math.h), and two helpers of the collision test
 * (collision.h). Written in assembly in the original and kept as assembly;
 * the listings are in asm/funcs/.
 *
 * That each was written by hand, and is not what the compiler made of a C
 * function, is read from its instructions. The line above each says which
 * of these it shows:
 *   "trapping": it has add, addi, sub or neg, which trap on overflow. The
 *     compiler only ever emits addu, addiu and subu. A number after the
 *     word is how many of them the routine has.
 *   "$t scratch": it calls nothing and works in $t0 and up while $v0 or $v1
 *     lies unused. The compiler takes $v0, $v1 and the free argument
 *     registers first: the same routine written in C comes out in those,
 *     instruction for instruction otherwise.
 *   "GTE in the delay slot": a GTE instruction sits in the delay slot of
 *     its return. After a macro's instructions the compiler leaves a nop
 *     there; and Sony's macros work through $t4-$t6, not $t0 and up.
 *   "empty delay slot": a store stands just before the return, and a nop
 *     in its delay slot, where the compiler moves the store. */
#define INCLUDE_ASM_USE_MACRO_INC 1
#include "common.h"
#include "include_asm.h"
#include "bb2.h"

/* ---- tables ---- */

/* 0x1000 over the square root of n / 64, for n from 64 to 255, in
 * halfwords (gte_NormalizeIR). */
const u32 g_math_rsqrt_table[96] = {
    0x0FE01000, 0x0FA30FC1, 0x0F680F85, 0x0F300F4C, 0x0EFB0F15, 0x0EC70EE1,
    0x0E960EAE, 0x0E660E7E, 0x0E380E4F, 0x0E0C0E22, 0x0DE20DF7, 0x0DB90DCD,
    0x0D910DA5, 0x0D6B0D7E, 0x0D450D58, 0x0D210D33, 0x0CFF0D10, 0x0CDD0CEE,
    0x0CBC0CCC, 0x0C9C0CAC, 0x0C7D0C8D, 0x0C5F0C6E, 0x0C420C51, 0x0C260C34,
    0x0C0A0C18, 0x0BEF0BFD, 0x0BD50BE2, 0x0BBB0BC8, 0x0BA20BAF, 0x0B8A0B96,
    0x0B720B7E, 0x0B5B0B67, 0x0B450B50, 0x0B2E0B39, 0x0B190B24, 0x0B040B0E,
    0x0AEF0AF9, 0x0ADB0AE5, 0x0AC70AD1, 0x0AB40ABD, 0x0AA10AAA, 0x0A8E0A97,
    0x0A7C0A85, 0x0A6A0A73, 0x0A590A61, 0x0A470A50, 0x0A370A3F, 0x0A260A2E,
    0x0A160A1E, 0x0A060A0E, 0x09F609FE, 0x09E709EF, 0x09D809E0, 0x09C909D1,
    0x09BB09C2, 0x09AD09B4, 0x099E09A5, 0x09910998, 0x0983098A, 0x0976097C,
    0x0969096F, 0x095C0962, 0x094F0955, 0x09430949, 0x0936093C, 0x092A0930,
    0x091E0924, 0x09120918, 0x0907090D, 0x08FB0901, 0x08F008F6, 0x08E508EB,
    0x08DA08E0, 0x08CF08D5, 0x08C508CA, 0x08BA08BF, 0x08B008B5, 0x08A608AB,
    0x089C08A1, 0x08920897, 0x0888088D, 0x087E0883, 0x0875087A, 0x086B0870,
    0x08620867, 0x0859085E, 0x08500855, 0x0847084C, 0x083E0843, 0x0836083A,
    0x082D0831, 0x08240829, 0x081C0820, 0x08140818, 0x080C0810, 0x08040808,
};

/* The square root of n, times 512, for n from 0 to 255, in halfwords
 * (math_Sqrt). */
const u32 g_math_sqrt_table[128] = {
    0x02000000, 0x037602D4, 0x04780400, 0x054A04E6, 0x060005A8, 0x06A20653,
    0x073606ED, 0x07BE077B, 0x083F0800, 0x08B7087C, 0x092A08F1, 0x09970961,
    0x0A0009CC, 0x0A640A32, 0x0AC50A95, 0x0B220AF4, 0x0B7D0B50, 0x0BD50BA9,
    0x0C2A0C00, 0x0C7D0C54, 0x0CCE0CA6, 0x0D1D0CF6, 0x0D6A0D44, 0x0DB60D90,
    0x0E000DDB, 0x0E480E24, 0x0E8F0E6C, 0x0ED50EB2, 0x0F190EF7, 0x0F5C0F3B,
    0x0F9E0F7D, 0x0FDF0FBF, 0x101F1000, 0x105E103F, 0x109C107E, 0x10DA10BB,
    0x111610F8, 0x11521134, 0x118C116F, 0x11C611A9, 0x120011E3, 0x1238121C,
    0x12701254, 0x12A7128C, 0x12DE12C2, 0x131412F9, 0x1349132E, 0x137E1364,
    0x13B21398, 0x13E613CC, 0x14191400, 0x144C1432, 0x147E1465, 0x14B01497,
    0x14E114C8, 0x151214F9, 0x1542152A, 0x1572155A, 0x15A2158A, 0x15D115B9,
    0x160015E8, 0x162E1617, 0x165C1645, 0x16891673, 0x16B716A0, 0x16E416CD,
    0x171016FA, 0x173C1726, 0x17681752, 0x1794177E, 0x17BF17AA, 0x17EA17D5,
    0x18151800, 0x183F182A, 0x18691854, 0x1893187E, 0x18BD18A8, 0x18E618D1,
    0x190F18FA, 0x19381923, 0x1960194C, 0x19881974, 0x19B0199C, 0x19D819C4,
    0x1A0019EC, 0x1A271A13, 0x1A4E1A3A, 0x1A751A61, 0x1A9B1A88, 0x1AC21AAE,
    0x1AE81AD5, 0x1B0E1AFB, 0x1B331B21, 0x1B591B46, 0x1B7E1B6C, 0x1BA31B91,
    0x1BC81BB6, 0x1BED1BDB, 0x1C121C00, 0x1C361C24, 0x1C5A1C48, 0x1C7E1C6C,
    0x1CA21C90, 0x1CC61CB4, 0x1CE91CD8, 0x1D0D1CFB, 0x1D301D1E, 0x1D531D41,
    0x1D761D64, 0x1D981D87, 0x1DBB1DAA, 0x1DDD1DCC, 0x1E001DEE, 0x1E221E11,
    0x1E431E33, 0x1E651E54, 0x1E871E76, 0x1EA81E98, 0x1ECA1EB9, 0x1EEB1EDA,
    0x1F0C1EFB, 0x1F2D1F1C, 0x1F4E1F3D, 0x1F6E1F5E, 0x1F8F1F7E, 0x1FAF1F9F,
    0x1FCF1FBF, 0x1FEF1FDF,
};

/* ---- memory ---- */

/* Moves `size` bytes, rounded up to whole words, from `src` to `dest`. The
 * two may overlap: the words are copied from whichever end is safe, sixteen
 * at a time and the rest by a jump into a run of single copies. */
/* Assembly: trapping (20); the frame is made after the first branch; it jumps
 * by a computed address into runs of copies. */
#if RB_PORT
void mem_MoveWords(s32 src, s32 dest, s32 size) {
    u32 *from = (u32 *)src;
    u32 *to = (u32 *)dest;
    u32 words = ((u32)size >> 2) + ((size & 3) != 0);
    u32 i;

    if (src < dest) {
        for (i = words; i > 0; i--) {
            to[i - 1] = from[i - 1];
        }
    } else {
        for (i = 0; i < words; i++) {
            to[i] = from[i];
        }
    }
}
#else
INCLUDE_ASM("asm/funcs", mem_MoveWords);
#endif

/* ---- blending rotations ---- */

/* Blends two rotation matrices: the first two rows are blended and made
 * unit length again, and the rows are then squared up against each other,
 * so that the result is a rotation. */
/* Assembly: trapping; gte_NormalizeIR hands it three values in $t0-$t2; 0x38
 * bytes of frame for eight saved registers. */
#if RB_PORT
/* Defined further down. */
static void math_NormalizeIr(s32 *unit);

/* In C. Every number of the two matrices is read before the first of
 * `out` is written, so `out` may be either of them. */
void math_LerpRotation(MATRIX *first, MATRIX *second, s32 weight, MATRIX *out) {
    s32 rest = ONE - weight;
    s32 rows[4][3]; /* the first two rows of `first`, then of `second` */
    s32 first_row[3];
    s32 second_row[3];
    u32 diagonal[3];
    s32 along;
    s32 i;

    for (i = 0; i < 3; i++) {
        rows[0][i] = first->m[0][i];
        rows[1][i] = first->m[1][i];
        rows[2][i] = second->m[0][i];
        rows[3][i] = second->m[1][i];
    }

    /* The first row: blended, and made unit length. */
    port_GteWriteData(PORT_GTE_IR0, (u32)rest);
    port_GteLoadData(PORT_GTE_IR1, rows[0], 3);
    port_GteCommand(PORT_GTE_CMD_GPF12);
    port_GteWriteData(PORT_GTE_IR0, (u32)weight);
    port_GteLoadData(PORT_GTE_IR1, rows[2], 3);
    port_GteCommand(PORT_GTE_CMD_GPL12);
    math_NormalizeIr(first_row);

    /* The second row: blended; what it has along the first is taken off
     * it; made unit length. */
    port_GteWriteData(PORT_GTE_IR0, (u32)rest);
    port_GteLoadData(PORT_GTE_IR1, rows[1], 3);
    port_GteCommand(PORT_GTE_CMD_GPF12);
    port_GteWriteData(PORT_GTE_IR0, (u32)weight);
    port_GteLoadData(PORT_GTE_IR1, rows[3], 3);
    port_GteCommand(PORT_GTE_CMD_GPL12);
    port_GteStoreData(PORT_GTE_IR1, second_row, 3);
    along = (s32)((u32)first_row[0] * (u32)second_row[0] +
                  (u32)first_row[1] * (u32)second_row[1] +
                  (u32)first_row[2] * (u32)second_row[2]) >>
            12;
    port_GteWriteData(PORT_GTE_IR0, 0U - (u32)along);
    port_GteLoadData(PORT_GTE_IR1, first_row, 3);
    port_GteLoadData(PORT_GTE_MAC1, second_row, 3);
    port_GteCommand(PORT_GTE_CMD_GPL12);
    math_NormalizeIr(second_row);

    /* The third row: the first crossed with the second. The GTE takes
     * the first from its rotation's diagonal, which is put back. */
    for (i = 0; i < 3; i++) {
        diagonal[i] = port_GteReadControl(PORT_GTE_ROTATION + 2 * i);
        port_GteWriteControl(PORT_GTE_ROTATION + 2 * i, (u32)first_row[i]);
    }
    port_GteLoadData(PORT_GTE_IR1, second_row, 3);
    port_GteCommand(PORT_GTE_CMD_OP12);
    for (i = 0; i < 3; i++) {
        out->m[0][i] = (s16)first_row[i];
        out->m[1][i] = (s16)second_row[i];
        out->m[2][i] = (s16)port_GteReadData(PORT_GTE_MAC1 + i);
        port_GteWriteControl(PORT_GTE_ROTATION + 2 * i, diagonal[i]);
    }
}
#else
INCLUDE_ASM("asm/funcs", math_LerpRotation);
#endif
/* Scales the vector in the GTE's IR1..IR3 to unit length, with the table of
 * reciprocal square roots. The result is left in $t0..$t2: only assembly
 * calls it. */
/* Assembly: trapping; its results are in $t0-$t2, not $v0. */
#if RB_PORT
/* gte_NormalizeIR in C. The assembly leaves its three results in
 * registers for the assembly that called it; here they go to `unit`.
 *
 * The sum of the squares is brought to eight bits, its first bit at an
 * even place, and those pick one over its root from the table; the
 * vector times that is shifted back by half of what the sum was moved.
 * A vector of zeroes, whose sum has no first bit, makes the assembly
 * read outside the table (the original's behaviour: what it reads is
 * multiplied by zeroes); here the factor is then 0. */
static void math_NormalizeIr(s32 *unit) {
    s32 vector[3];
    s32 sum;
    s32 zeroes;
    s32 top;
    s32 factor = 0;
    s32 i;

    port_GteStoreData(PORT_GTE_IR1, vector, 3);
    port_GteCommand(PORT_GTE_CMD_SQR0);
    sum = (s32)(port_GteReadData(PORT_GTE_MAC1) +
                port_GteReadData(PORT_GTE_MAC2) +
                port_GteReadData(PORT_GTE_MAC3));
    port_GteWriteData(PORT_GTE_LZCS, (u32)sum);
    zeroes = (s32)port_GteReadData(PORT_GTE_LZCR) & 0x1E; /* made even */
    /* the sum's top eight bits, the first of them at bit 6 or 7 */
    top = (zeroes >= 24) ? sum << (zeroes - 24) : sum >> (24 - zeroes);
    /* the table begins at 0x40, the least a number so placed can be */
    if (top >= 0x40 && top < 0x40 + 2 * 96) {
        /* (the same 0x40) */
        factor = ((const s16 *)g_math_rsqrt_table)[top - 0x40];
    }
    port_GteWriteData(PORT_GTE_IR0, (u32)factor);
    port_GteLoadData(PORT_GTE_IR1, vector, 3);
    port_GteCommand(PORT_GTE_CMD_GPF0);
    for (i = 0; i < 3; i++) {
        /* back down by half as far as the sum was moved up */
        unit[i] =
            (s32)port_GteReadData(PORT_GTE_MAC1 + i) >> ((31 - zeroes) >> 1);
    }
}
#else
INCLUDE_ASM("asm/funcs", gte_NormalizeIR);
#endif

/* ---- square roots and lengths ---- */

/* The square root of a whole number, from a table: the GTE counts the
 * leading zeroes, the top bits pick the entry. */
/* Assembly: trapping; $t scratch. */
#if RB_PORT
/* In C. A number below 256 is looked up as it is; a larger one by its
 * top eight bits, taken at an even bit, and the root moved back up by
 * half as far. A negative number has no root: the assembly reads outside
 * the table for it (the original's behaviour), and here it is 0. */
s32 math_Sqrt(s32 number) {
    const s16 *roots = (const s16 *)g_math_sqrt_table;
    s32 zeroes;

    port_GteWriteData(PORT_GTE_LZCS, (u32)number);
    if (number <= 0) {
        return 0;
    }
    zeroes = (s32)port_GteReadData(PORT_GTE_LZCR) & 0x1E; /* made even */
    /* 24 leading zeroes: a number of eight bits */
    if (zeroes >= 24) {
        return (s32)((u32)roots[number] >> 9);
    }
    /* the top eight bits' root, moved up by half as far as they were
     * moved down */
    return (
        s32)((u32)(roots[number >> (24 - zeroes)] << ((31 - zeroes) >> 1)) >>
             12);
}
#else
INCLUDE_ASM("asm/funcs", math_Sqrt);
#endif
/* The length of (x, y, z): the GTE squares them, and the sum goes straight
 * on into math_Sqrt. */
/* Assembly: trapping; it ends by jumping into math_Sqrt, not calling it. */
#if RB_PORT
/* In C. The GTE squares 16 bits of each number. */
s32 math_Length(s32 x, s32 y, s32 z) {
    return math_Sqrt(math_LengthSquared(x, y, z));
}
#else
INCLUDE_ASM("asm/funcs", math_Length);
#endif
/* The same without the root. */
/* Assembly: $t scratch; math_Length to the last two instructions. */
#if RB_PORT
/* In C. The GTE squares 16 bits of each number. */
s32 math_LengthSquared(s32 x, s32 y, s32 z) {
    port_GteWriteData(PORT_GTE_IR1, (u32)x);
    port_GteWriteData(PORT_GTE_IR2, (u32)y);
    port_GteWriteData(PORT_GTE_IR3, (u32)z);
    port_GteCommand(PORT_GTE_CMD_SQR0);
    return (
        s32)(port_GteReadData(PORT_GTE_MAC1) + port_GteReadData(PORT_GTE_MAC2) +
             port_GteReadData(PORT_GTE_MAC3));
}
#else
INCLUDE_ASM("asm/funcs", math_LengthSquared);
#endif

/* ---- blends ---- */

/* `weight` of the second and 0x1000 - `weight` of the first: of two vectors,
 * and of the nine numbers of two matrices (which is not a rotation any
 * more: math_LerpRotation). Nothing calls either. */
/* Assembly: trapping; $t scratch. Both of them. */
#if RB_PORT
/* One row of three halfwords blended in the GTE: `weight` of the second
 * and the rest of the first. */
static void math_LerpRow(
    const s16 *first, const s16 *second, s32 weight, s16 *out) {
    s32 row[3];
    s32 i;

    for (i = 0; i < 3; i++) {
        row[i] = first[i];
    }
    port_GteWriteData(PORT_GTE_IR0, (u32)(ONE - weight));
    port_GteLoadData(PORT_GTE_IR1, row, 3);
    port_GteCommand(PORT_GTE_CMD_GPF12);
    for (i = 0; i < 3; i++) {
        row[i] = second[i];
    }
    port_GteWriteData(PORT_GTE_IR0, (u32)weight);
    port_GteLoadData(PORT_GTE_IR1, row, 3);
    port_GteCommand(PORT_GTE_CMD_GPL12);
    for (i = 0; i < 3; i++) {
        out[i] = (s16)port_GteReadData(PORT_GTE_IR1 + i);
    }
}

/* In C. */
void math_LerpSVector(
    SVECTOR *first, SVECTOR *second, s32 weight, SVECTOR *out) {
    math_LerpRow(&first->vx, &second->vx, weight, &out->vx);
}

/* In C. A row is read from both matrices before it is written, so `out`
 * may be either of them. */
void math_LerpMatrix3x3(
    MATRIX *first, MATRIX *second, s32 weight, MATRIX *out) {
    s32 row;

    for (row = 0; row < 3; row++) {
        math_LerpRow(first->m[row], second->m[row], weight, out->m[row]);
    }
}
#else
INCLUDE_ASM("asm/funcs", math_LerpSVector);
INCLUDE_ASM("asm/funcs", math_LerpMatrix3x3);
#endif

/* ---- matrices ---- */

/* The product of two rotations, like Sony's MulMatrix0. It leaves the first
 * in the GTE as the rotation, with no translation. The three products run
 * one behind the other: each column is packed while the GTE works on the
 * one before. */
/* Assembly: it works in $t0-$t7 and $t9 with $a3 never touched; the packing
 * of each column is placed by hand in the GTE's wait for the one before. */
#if RB_PORT
/* In C. */
void math_MulMatrix(MATRIX *first, MATRIX *second, MATRIX *out) {
    s32 columns[PORT_GTE_MATRIX_ELEMENTS];

    port_GteLoadControl(PORT_GTE_ROTATION, first, PORT_GTE_MATRIX_WORDS);
    port_GteWriteControl(PORT_GTE_TRX, 0);
    port_GteWriteControl(PORT_GTE_TRY, 0);
    port_GteWriteControl(PORT_GTE_TRZ, 0);
    port_GteRotateColumns(second->m, PORT_GTE_CMD_RTV0TR, columns);
    port_GteStoreColumns(out->m, columns, FALSE);
}
#else
INCLUDE_ASM("asm/funcs", math_MulMatrix);
#endif
/* A point taken through a matrix and its translation: the point as an
 * SVECTOR, and as three words. */
/* Assembly: GTE in the delay slot; $t scratch. Both of them. */
#if RB_PORT
/* In C. The results are the GTE's 16-bit ones (IR), each held to 16
 * bits. */
void math_Transform(MATRIX *matrix, SVECTOR *point, VECTOR *out) {
    port_GteLoadControl(
        PORT_GTE_ROTATION, matrix, PORT_GTE_MATRIX_AND_TRANSLATION_WORDS);
    port_GteLoadData(PORT_GTE_VXY0, point, 2);
    port_GteCommand(PORT_GTE_CMD_RTV0TR);
    port_GteStoreData(PORT_GTE_IR1, out, 3);
}

/* In C. Sixteen bits of each of the point's three words are taken. */
void math_TransformVector(void *matrix, void *point, void *out) {
    const u32 *words = (const u32 *)point;

    port_GteLoadControl(
        PORT_GTE_ROTATION, matrix, PORT_GTE_MATRIX_AND_TRANSLATION_WORDS);
    port_GteWriteData(PORT_GTE_VXY0, (words[0] & 0xFFFF) | (words[1] << 16));
    port_GteWriteData(PORT_GTE_VZ0, words[2]);
    port_GteCommand(PORT_GTE_CMD_RTV0TR);
    port_GteStoreData(PORT_GTE_IR1, out, 3);
}
#else
INCLUDE_ASM("asm/funcs", math_Transform);
INCLUDE_ASM("asm/funcs", math_TransformVector);
#endif
/* Loads a matrix into the GTE: with its translation, and with none. Nothing
 * calls either. */
/* Assembly: GTE in the delay slot; $t scratch. Both of them. Sony's
 * gte_SetRotMatrix and gte_SetTransMatrix compile to other instructions. */
#if RB_PORT
/* In C. */
void gte_LoadMatrix(MATRIX *matrix) {
    port_GteLoadControl(
        PORT_GTE_ROTATION, matrix, PORT_GTE_MATRIX_AND_TRANSLATION_WORDS);
}

/* In C. */
void gte_LoadRotation(MATRIX *matrix) {
    port_GteLoadControl(PORT_GTE_ROTATION, matrix, PORT_GTE_MATRIX_WORDS);
    port_GteWriteControl(PORT_GTE_TRX, 0);
    port_GteWriteControl(PORT_GTE_TRY, 0);
    port_GteWriteControl(PORT_GTE_TRZ, 0);
}
#else
INCLUDE_ASM("asm/funcs", gte_LoadMatrix);
INCLUDE_ASM("asm/funcs", gte_LoadRotation);
#endif
/* A point through a matrix's rotation, with the translation given apart, as
 * three halfwords. Nothing calls it. */
/* Assembly: GTE in the delay slot; $t scratch. */
#if RB_PORT
/* In C. */
void math_TransformOffset(
    MATRIX *matrix, SVECTOR *offset, SVECTOR *point, VECTOR *out) {
    port_GteLoadControl(PORT_GTE_ROTATION, matrix, PORT_GTE_MATRIX_WORDS);
    port_GteWriteControl(PORT_GTE_TRX, (u32)offset->vx);
    port_GteWriteControl(PORT_GTE_TRY, (u32)offset->vy);
    port_GteWriteControl(PORT_GTE_TRZ, (u32)offset->vz);
    port_GteLoadData(PORT_GTE_VXY0, point, 2);
    port_GteCommand(PORT_GTE_CMD_RTV0TR);
    port_GteStoreData(PORT_GTE_IR1, out, 3);
}
#else
INCLUDE_ASM("asm/funcs", math_TransformOffset);
#endif

/* ---- odds and ends ---- */

/* The GTE's far colour, the colour of the fog, as three bytes. */
/* Assembly: $t scratch; empty delay slot. */
#if RB_PORT
/* In C. The GTE keeps the far colour in sixteenths. */
void gte_ReadFarColor(CVECTOR *color) {
    color->r = (u8)(port_GteReadControl(PORT_GTE_RFC) >> 4);
    color->g = (u8)(port_GteReadControl(PORT_GTE_GFC) >> 4);
    color->b = (u8)(port_GteReadControl(PORT_GTE_BFC) >> 4);
}
#else
INCLUDE_ASM("asm/funcs", gte_ReadFarColor);
#endif
/* Stops the game where something cannot go on: a write just past the end of
 * the scratchpad, where there is no memory. */
/* Assembly: the address is built in $t9 with lui and ori; the compiler stores
 * through $at. */
#if RB_PORT
/* In C: the port has no such address; its platform layer stops the game
 * (port_platform.h). */
void sys_Trap(void) { port_Trap(); }
#else
INCLUDE_ASM("asm/funcs", sys_Trap);
#endif
/* render.h: the ordering-table entry for a depth. */
/* Assembly: trapping; $t scratch. The same sum written in C comes out in
 * $v0 and $v1, with addiu and subu. */
#if RB_PORT
/* In C. The depth, less the bits `shift` leaves it, is a number of 11
 * bits and above them how far to shift it; the entry counts down from
 * the table's top. */
s32 render_DepthToOtShifted(s32 depth, s32 shift) {
    /* (a shift takes five bits of its count) */
    u32 value = (u32)depth >> ((3 - shift) & 31);
    /* the 11 bits, less the table's size */
    s32 mantissa = (s32)(value & 0x7FF) - 0x1000;

    /* shifted by what is above them, and cut to the table's size */
    return (mantissa >> ((value >> 11) & 31)) & 0xFFF;
}
#else
INCLUDE_ASM("asm/funcs", render_DepthToOtShifted);
#endif

/* ---- the collision test's two helpers (collision.h) ----
 * The last two routines of the assembly, just ahead of collision.c's code. */

/* Puts (point - origin) on the two axes of a face's frame, in the GTE. */
/* Assembly: trapping; $t scratch. */
#if RB_PORT
/* In C. `frame` is the address of nine halfwords: the origin, then the
 * two axes, which become the first two rows of the GTE's rotation. Its
 * third row is left as it was, and so is what the GTE makes of it. */
void collision_ProjectOnFace(s32 frame, s32 x, s32 y, s32 z) {
    const s16 *origin = (const s16 *)frame;
    const u16 *axes = (const u16 *)frame + 3;
    s32 i;

    for (i = 0; i < 3; i++) {
        port_GteWriteControl(
            PORT_GTE_ROTATION + i, axes[2 * i] | ((u32)axes[2 * i + 1] << 16));
        port_GteWriteControl(PORT_GTE_TRX + i, 0);
    }
    port_GteWriteData(PORT_GTE_VXY0, ((u32)(x - origin[0]) & 0xFFFF) |
                                         ((u32)(y - origin[1]) << 16));
    port_GteWriteData(PORT_GTE_VZ0, (u32)(z - origin[2]));
    port_GteCommand(PORT_GTE_CMD_RTV0TR);
}
#else
INCLUDE_ASM("asm/funcs", collision_ProjectOnFace);
#endif
/* Reads the two results back. */
/* Assembly: $t scratch; empty delay slot. */
#if RB_PORT
/* In C. */
void collision_ReadProjection(s32 *u, s32 *v) {
    *u = (s32)port_GteReadData(PORT_GTE_IR1) >> 2;
    *v = (s32)port_GteReadData(PORT_GTE_IR2) >> 2;
}
#else
INCLUDE_ASM("asm/funcs", collision_ReadProjection);
#endif
