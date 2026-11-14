/* LIBGTE MTX_08: ScaleMatrix, which scales a matrix by a vector, column by
 * column. Assembly in Sony's library: scratch in $t0-$t5 with $v1 idle, and
 * three nops of padding to a multiple of 16 bytes. */
#include "include_asm.h"

INCLUDE_ASM("asm/funcs", ScaleMatrix);
