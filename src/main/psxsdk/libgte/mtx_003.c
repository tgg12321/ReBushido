/* LIBGTE MTX_003: CompMatrix, which composes two coordinate transforms,
 * rotation and translation. Assembly in Sony's library: three add, which trap
 * on overflow and which the compiler never emits, and scratch in $t0-$t8 with
 * $v1 idle. */
#include "include_asm.h"

INCLUDE_ASM("asm/funcs", CompMatrix);
