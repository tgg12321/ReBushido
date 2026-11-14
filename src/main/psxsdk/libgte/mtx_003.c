/* LIBGTE MTX_003: CompMatrix, which composes two coordinate transforms,
 * rotation and translation. Assembly in Sony's library: three add, which trap
 * on overflow and which the compiler never emits, and scratch in $t0-$t8 with
 * $v1 idle. */
#include "include_asm.h"

#include "rebushido.h"

/* The native port has no assembly: the C here does what the listing does
 * (docs/PORT.md). */
#if RB_PORT
#include "psxsdk/libgte.h"
#include "port_gte.h"

/* In C: m2 = m0 * m1 as two coordinate systems are put together: the
 * rotations are multiplied, and m1's translation goes through m0's
 * rotation and has m0's added. Only the low 16 bits of m1's translation
 * are taken. m0's rotation is left in the GTE. */
MATRIX *CompMatrix(MATRIX *m0, MATRIX *m1, MATRIX *m2) {
    s32 columns[PORT_GTE_MATRIX_ELEMENTS];
    s32 i;

    port_GteLoadControl(PORT_GTE_ROTATION, m0, PORT_GTE_MATRIX_WORDS);
    port_GteRotateColumns(m1->m, PORT_GTE_CMD_RTV0, columns);
    port_GteStoreColumns(m2->m, columns, TRUE);
    port_GteWriteData(
        PORT_GTE_VXY0, ((u32)m1->t[0] & 0xFFFF) | ((u32)m1->t[1] << 16));
    port_GteWriteData(PORT_GTE_VZ0, (u32)m1->t[2]);
    port_GteCommand(PORT_GTE_CMD_RTV0);
    for (i = 0; i < 3; i++) {
        m2->t[i] = (s32)(port_GteReadData(PORT_GTE_MAC1 + i) + (u32)m0->t[i]);
    }
    return m2;
}
#else
INCLUDE_ASM("asm/funcs", CompMatrix);
#endif
