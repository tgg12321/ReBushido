/* LIBGTE MTX_08: ScaleMatrix, which scales a matrix by a vector, column by
 * column. Assembly in Sony's library: scratch in $t0-$t5 with $v1 idle, and
 * three nops of padding to a multiple of 16 bytes. */
#include "include_asm.h"

#include "rebushido.h"

/* The native port has no assembly: the C here does what the listing does
 * (docs/PORT.md). */
#if RB_PORT
#include "psxsdk/libgte.h"
#include "port_gte.h"

/* In C: each number of the matrix times the number of `v` for its column,
 * with the fraction dropped. The last one is stored as a word, which
 * writes the two bytes after the matrix's numbers as well. */
MATRIX *ScaleMatrix(MATRIX *m, VECTOR *v) {
    const s32 *scale = &v->vx;
    s32 row;
    s32 column;
    s32 product = 0;

    for (row = 0; row < 3; row++) {
        for (column = 0; column < 3; column++) {
            product = (s32)((u32)m->m[row][column] * (u32)scale[column]) >> 12;
            m->m[row][column] = (s16)product;
        }
    }
    ((u32 *)m)[4] = (u32)product;
    return m;
}
#else
INCLUDE_ASM("asm/funcs", ScaleMatrix);
#endif
