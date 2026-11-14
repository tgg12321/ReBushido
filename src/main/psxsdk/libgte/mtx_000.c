/* LIBGTE MTX_000: MulMatrix0, the product of two matrices. Assembly in Sony's
 * library: scratch in $t0-$t8 with $v1 idle, and a nop of padding to a
 * multiple of 16 bytes. */
#include "include_asm.h"

#include "rebushido.h"

/* The native port has no assembly: the C here does what the listing does
 * (docs/PORT.md). */
#if RB_PORT
#include "psxsdk/libgte.h"
#include "port_gte.h"

/* In C: m2 = m0 * m1. m0 is left in the GTE as the rotation; the columns
 * of m1 go through it one after another. m2 may be either of the two. */
MATRIX *MulMatrix0(MATRIX *m0, MATRIX *m1, MATRIX *m2) {
    s32 columns[PORT_GTE_MATRIX_ELEMENTS];

    port_GteLoadControl(PORT_GTE_ROTATION, m0, PORT_GTE_MATRIX_WORDS);
    port_GteRotateColumns(m1->m, PORT_GTE_CMD_RTV0, columns);
    port_GteStoreColumns(m2->m, columns, TRUE);
    return m2;
}
#else
INCLUDE_ASM("asm/funcs", MulMatrix0);
#endif
