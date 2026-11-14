/* LIBGTE FGO_04: RotMatrixX, which turns a matrix about the X axis. Assembly
 * in Sony's library: no frame, all of $t0-$t9 as scratch with $v1 idle, and
 * two nops of padding to a multiple of 16 bytes. */
#include "include_asm.h"

INCLUDE_ASM("asm/funcs", RotMatrixX);
