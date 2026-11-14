/* LIBGTE MTX_005: ApplyRotMatrix, the GTE's rotation matrix applied to a
 * short vector. Assembly in Sony's library: its two temporaries are in $t0
 * and $t1, where the compiler would take $v0 and $v1. */
#include "include_asm.h"

INCLUDE_ASM("asm/funcs", ApplyRotMatrix);
