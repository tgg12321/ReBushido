/* LIBGTE MTX_01: ApplyRotMatrixLV, the GTE's rotation matrix applied to a
 * vector of long components. Assembly in Sony's library: scratch in $t0-$t5
 * with $v1 idle, and two nops of padding to a multiple of 16 bytes. */
#include "include_asm.h"

INCLUDE_ASM("asm/funcs", ApplyRotMatrixLV);
