/* LIBGTE MTX_00A: ScaleMatrixL, which scales a matrix by a vector, row by
 * row. Assembly in Sony's library: scratch in $t0-$t5 with $v1 idle, and
 * three nops of padding to a multiple of 16 bytes. */
#include "include_asm.h"

INCLUDE_ASM("asm/funcs", ScaleMatrixL);
