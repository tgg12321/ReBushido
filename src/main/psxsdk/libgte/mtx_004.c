/* LIBGTE MTX_004: ApplyMatrixLV, a matrix applied to a vector of long
 * components. Assembly in Sony's library: scratch in $t0-$t5 with $v1 idle. */
#include "include_asm.h"

INCLUDE_ASM("asm/funcs", ApplyMatrixLV);
