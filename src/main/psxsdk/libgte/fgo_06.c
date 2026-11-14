/* LIBGTE FGO_06: RotMatrixZ, which turns a matrix about the Z axis. Assembly
 * in Sony's library: no frame, all of $t0-$t9 as scratch with $v1 idle, and
 * two nops of padding to a multiple of 16 bytes. */
#include "include_asm.h"

#include "rebushido.h"

/* The native port has no assembly: the C here does what the listing does
 * (docs/PORT.md). */
#if RB_PORT
#include "psxsdk/libgte.h"
#include "port_gte.h"
extern s16 rcossin_tbl[];

/* In C: the matrix turned by `r` about z. The sine and the cosine are
 * the table's, with 12 bits of fraction; two rows of the matrix are
 * mixed. */
MATRIX *RotMatrixZ(s32 r, MATRIX *m) {
    s32 index = (r < 0) ? (s32)(0U - (u32)r) & 0xFFF : r & 0xFFF;
    s32 sine = (r < 0) ? -rcossin_tbl[2 * index] : rcossin_tbl[2 * index];
    s32 cosine = rcossin_tbl[2 * index + 1];
    s32 column;

    for (column = 0; column < 3; column++) {
        s32 upper = m->m[0][column];
        s32 lower = m->m[1][column];

        m->m[0][column] =
            (s16)((s32)((u32)cosine * (u32)upper - (u32)sine * (u32)lower) >>
                  12);
        m->m[1][column] =
            (s16)((s32)((u32)cosine * (u32)lower + (u32)sine * (u32)upper) >>
                  12);
    }
    return m;
}
#else
INCLUDE_ASM("asm/funcs", RotMatrixZ);
#endif
