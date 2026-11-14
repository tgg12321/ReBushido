/* LIBGTE MTX_12: SetTransMatrix, which loads the GTE's translation vector.
 * Assembly in Sony's library: the three words are loaded into $t0-$t2, with
 * $v0 and $v1 idle. */
#include "include_asm.h"

#include "rebushido.h"

/* The native port has no assembly: the C here does what the listing does
 * (docs/PORT.md). */
#if RB_PORT
#include "psxsdk/libgte.h"
#include "port_gte.h"

/* In C. */
void SetTransMatrix(MATRIX *m) { port_GteLoadControl(PORT_GTE_TRX, m->t, 3); }
#else
INCLUDE_ASM("asm/funcs", SetTransMatrix);
#endif
