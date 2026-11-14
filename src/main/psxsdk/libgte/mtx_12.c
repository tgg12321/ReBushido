/* LIBGTE MTX_12: SetTransMatrix, which loads the GTE's translation vector.
 * Assembly in Sony's library: the three words are loaded into $t0-$t2, with
 * $v0 and $v1 idle. */
#include "include_asm.h"

INCLUDE_ASM("asm/funcs", SetTransMatrix);
