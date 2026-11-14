/* LIBGTE FGO_03: RotMatrixZYX, RotMatrix with the three rotations applied in
 * the opposite order. Assembly in Sony's library: no frame, all of $t0-$t9 as
 * scratch with $v1 idle, and a nop of padding to a multiple of 16 bytes. */
#include "include_asm.h"

INCLUDE_ASM("asm/funcs", RotMatrixZYX);
