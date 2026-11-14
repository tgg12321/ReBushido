/* LIBGTE MTX_09: SetRotMatrix, which loads the GTE's rotation matrix.
 * Assembly in Sony's library: the five words are loaded into $t0-$t4 in a
 * block, with $v0 and $v1 idle. */
#include "include_asm.h"

#include "rebushido.h"

/* The native port has no assembly: the C here does what the listing does
 * (docs/PORT.md). */
#if RB_PORT
#include "psxsdk/libgte.h"
#include "port_gte.h"

/* In C: the matrix's nine numbers, as five words. */
void SetRotMatrix(MATRIX *m) {
    port_GteLoadControl(PORT_GTE_ROTATION, m, PORT_GTE_MATRIX_WORDS);
}
#else
INCLUDE_ASM("asm/funcs", SetRotMatrix);
#endif
