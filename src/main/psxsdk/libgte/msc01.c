/* LIBGTE MSC01: SquareRoot0, the square root of an integer. Assembly in
 * Sony's library: four addi and sub, which trap on overflow and which the
 * compiler never emits, scratch in $t0-$t5 with $v1 idle, and three nops of
 * padding to a multiple of 16 bytes. */
#include "include_asm.h"

INCLUDE_ASM("asm/funcs", SquareRoot0);
