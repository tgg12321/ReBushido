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
INCLUDE_ASM("asm/funcs", mem_MoveWords);

/* ---- blending rotations ---- */

/* Blends two rotation matrices: the first two rows are blended and made
 * unit length again, and the rows are then squared up against each other,
 * so that the result is a rotation. */
/* Assembly: trapping; gte_NormalizeIR hands it three values in $t0-$t2; 0x38
 * bytes of frame for eight saved registers. */
INCLUDE_ASM("asm/funcs", math_LerpRotation);
/* Scales the vector in the GTE's IR1..IR3 to unit length, with the table of
 * reciprocal square roots. The result is left in $t0..$t2: only assembly
 * calls it. */
/* Assembly: trapping; its results are in $t0-$t2, not $v0. */
INCLUDE_ASM("asm/funcs", gte_NormalizeIR);

/* ---- square roots and lengths ---- */

/* The square root of a whole number, from a table: the GTE counts the
 * leading zeroes, the top bits pick the entry. */
/* Assembly: trapping; $t scratch. */
INCLUDE_ASM("asm/funcs", math_Sqrt);
/* The length of (x, y, z): the GTE squares them, and the sum goes straight
 * on into math_Sqrt. */
/* Assembly: trapping; it ends by jumping into math_Sqrt, not calling it. */
INCLUDE_ASM("asm/funcs", math_Length);
/* The same without the root. */
/* Assembly: $t scratch; math_Length to the last two instructions. */
INCLUDE_ASM("asm/funcs", math_LengthSquared);

/* ---- blends ---- */

/* `weight` of the second and 0x1000 - `weight` of the first: of two vectors,
 * and of the nine numbers of two matrices (which is not a rotation any
 * more: math_LerpRotation). Nothing calls either. */
/* Assembly: trapping; $t scratch. Both of them. */
INCLUDE_ASM("asm/funcs", math_LerpSVector);
INCLUDE_ASM("asm/funcs", math_LerpMatrix3x3);

/* ---- matrices ---- */

/* The product of two rotations, like Sony's MulMatrix0. It leaves the first
 * in the GTE as the rotation, with no translation. The three products run
 * one behind the other: each column is packed while the GTE works on the
 * one before. */
/* Assembly: it works in $t0-$t7 and $t9 with $a3 never touched; the packing
 * of each column is placed by hand in the GTE's wait for the one before. */
INCLUDE_ASM("asm/funcs", math_MulMatrix);
/* A point taken through a matrix and its translation: the point as an
 * SVECTOR, and as three words. */
/* Assembly: GTE in the delay slot; $t scratch. Both of them. */
INCLUDE_ASM("asm/funcs", math_Transform);
INCLUDE_ASM("asm/funcs", math_TransformVector);
/* Loads a matrix into the GTE: with its translation, and with none. Nothing
 * calls either. */
/* Assembly: GTE in the delay slot; $t scratch. Both of them. Sony's
 * gte_SetRotMatrix and gte_SetTransMatrix compile to other instructions. */
INCLUDE_ASM("asm/funcs", gte_LoadMatrix);
INCLUDE_ASM("asm/funcs", gte_LoadRotation);
/* A point through a matrix's rotation, with the translation given apart, as
 * three halfwords. Nothing calls it. */
/* Assembly: GTE in the delay slot; $t scratch. */
INCLUDE_ASM("asm/funcs", math_TransformOffset);

/* ---- odds and ends ---- */

/* The GTE's far colour, the colour of the fog, as three bytes. */
/* Assembly: $t scratch; empty delay slot. */
INCLUDE_ASM("asm/funcs", gte_ReadFarColor);
/* Stops the game where something cannot go on: a write just past the end of
 * the scratchpad, where there is no memory. */
/* Assembly: the address is built in $t9 with lui and ori; the compiler stores
 * through $at. */
INCLUDE_ASM("asm/funcs", sys_Trap);
/* render.h: the ordering-table entry for a depth. */
/* Assembly: trapping; $t scratch. The same sum written in C comes out in
 * $v0 and $v1, with addiu and subu. */
INCLUDE_ASM("asm/funcs", render_DepthToOtShifted);

/* ---- the collision test's two helpers (collision.h) ----
 * The last two routines of the assembly, just ahead of collision.c's code. */

/* Puts (point - origin) on the two axes of a face's frame, in the GTE. */
/* Assembly: trapping; $t scratch. */
INCLUDE_ASM("asm/funcs", collision_ProjectOnFace);
/* Reads the two results back. */
/* Assembly: $t scratch; empty delay slot. */
INCLUDE_ASM("asm/funcs", collision_ReadProjection);
