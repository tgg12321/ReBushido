/* LIBGTE MTX_005: ApplyRotMatrix, the GTE's rotation matrix applied to a
 * short vector. Assembly in Sony's library: its two temporaries are in $t0
 * and $t1, where the compiler would take $v0 and $v1. */
#include "include_asm.h"

#include "rebushido.h"

/* The native port has no assembly: the C here does what the listing does
 * (docs/PORT.md). */
#if RB_PORT
#include "psxsdk/libgte.h"
#include "port_gte.h"

/* In C: a vector through the rotation in the GTE. The result is the
 * 16-bit one (IR), each number held to 16 bits. The assembly returns
 * whatever its caller left in the third argument's register; here it is
 * the result's address. */
VECTOR *ApplyRotMatrix(SVECTOR *v0, VECTOR *v1) {
    port_GteLoadData(PORT_GTE_VXY0, v0, 2);
    port_GteCommand(PORT_GTE_CMD_RTV0);
    port_GteStoreData(PORT_GTE_IR1, v1, 3);
    return v1;
}
#else
INCLUDE_ASM("asm/funcs", ApplyRotMatrix);
#endif
