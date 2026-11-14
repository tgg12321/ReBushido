/* LIBGTE MTX_04: MulMatrix2, the product of two matrices, written over the
 * second. Assembly in Sony's library: scratch in $t0-$t8 with $v1 idle, and a
 * nop of padding to a multiple of 16 bytes. */
#include "include_asm.h"

#include "rebushido.h"

/* The native port has no assembly: the C here does what the listing does
 * (docs/PORT.md). */
#if RB_PORT
#include "psxsdk/libgte.h"
#include "port_gte.h"

/* In C: m1 = m0 * m1. m0 is left in the GTE as the rotation. */
MATRIX *MulMatrix2(MATRIX *m0, MATRIX *m1) {
    s32 columns[PORT_GTE_MATRIX_ELEMENTS];

    port_GteLoadControl(PORT_GTE_ROTATION, m0, PORT_GTE_MATRIX_WORDS);
    port_GteRotateColumns(m1->m, PORT_GTE_CMD_RTV0, columns);
    port_GteStoreColumns(m1->m, columns, TRUE);
    return m1;
}
#else
INCLUDE_ASM("asm/funcs", MulMatrix2);
#endif
