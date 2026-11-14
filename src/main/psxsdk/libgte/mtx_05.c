/* LIBGTE MTX_05: ApplyMatrix, a matrix applied to a short vector. Assembly in
 * Sony's library: scratch in $t0-$t4 with $v1 idle. */
#include "include_asm.h"

#include "rebushido.h"

/* The native port has no assembly: the C here does what the listing does
 * (docs/PORT.md). */
#if RB_PORT
#include "psxsdk/libgte.h"
#include "port_gte.h"

/* In C: a vector through a matrix's rotation, which is left in the GTE. */
VECTOR *ApplyMatrix(MATRIX *m, SVECTOR *v0, VECTOR *v1) {
    port_GteLoadControl(PORT_GTE_ROTATION, m, PORT_GTE_MATRIX_WORDS);
    port_GteLoadData(PORT_GTE_VXY0, v0, 2);
    port_GteCommand(PORT_GTE_CMD_RTV0);
    port_GteStoreData(PORT_GTE_MAC1, v1, 3);
    return v1;
}
#else
INCLUDE_ASM("asm/funcs", ApplyMatrix);
#endif
