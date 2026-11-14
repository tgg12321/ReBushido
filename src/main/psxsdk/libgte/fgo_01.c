/* LIBGTE FGO_01: RotMatrix, which makes a rotation matrix from three angles.
 * Assembly in Sony's library: no frame, all of $t0-$t9 as scratch with $v1
 * idle, and a nop of padding to a multiple of 16 bytes. */
#include "include_asm.h"

#include "rebushido.h"

/* The native port has no assembly: the C here does what the listing does
 * (docs/PORT.md). */
#if RB_PORT
#include "psxsdk/libgte.h"
#include "port_gte.h"
extern s16 rcossin_tbl[];

/* The sine and the cosine of an angle, from the table. */
static void SineAndCosine(s32 angle, s32 *sine, s32 *cosine) {
    s32 index = (angle < 0) ? (s32)(0U - (u32)angle) & 0xFFF : angle & 0xFFF;

    *sine = (angle < 0) ? -rcossin_tbl[2 * index] : rcossin_tbl[2 * index];
    *cosine = rcossin_tbl[2 * index + 1];
}

/* A product of two numbers with 12 bits of fraction, as the assembly
 * makes it: 32 bits of the product, then the fraction dropped. */
static s32 Product(s32 a, s32 b) { return (s32)((u32)a * (u32)b) >> 12; }

/* In C: the rotation matrix of three angles, turned about x, then y,
 * then z. Each number is made as the assembly makes it, the fraction
 * dropped after every product, so that the last bits are the same. */
MATRIX *RotMatrix(SVECTOR *r, MATRIX *m) {
    s32 sx, cx, sy, cy, sz, cz;
    s32 work;

    SineAndCosine(r->vx, &sx, &cx);
    SineAndCosine(r->vy, &sy, &cy);
    SineAndCosine(r->vz, &sz, &cz);
    m->m[0][2] = (s16)sy;
    m->m[1][2] = (s16)((s32)(0U - (u32)cy * (u32)sx) >> 12);
    m->m[2][2] = (s16)Product(cy, cx);
    m->m[0][0] = (s16)Product(cz, cy);
    m->m[0][1] = (s16)((s32)(0U - (u32)sz * (u32)cy) >> 12);
    work = Product(cz, -sy);
    m->m[1][0] = (s16)(Product(sz, cx) - Product(work, sx));
    m->m[2][0] = (s16)(Product(sz, sx) + Product(work, cx));
    work = Product(sz, -sy);
    m->m[1][1] = (s16)(Product(cz, cx) + Product(work, sx));
    m->m[2][1] = (s16)(Product(cz, sx) - Product(work, cx));
    return m;
}
#else
INCLUDE_ASM("asm/funcs", RotMatrix);
#endif
