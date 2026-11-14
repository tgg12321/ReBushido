/* LIBGTE MTX_03: MulMatrix, the product of two matrices, written over the
 * first. Assembly in Sony's library: scratch in $t0-$t8 with $v1 idle, and a
 * nop of padding to a multiple of 16 bytes. */
#include "include_asm.h"

INCLUDE_ASM("asm/funcs", MulMatrix);
