/* LIBGTE MTX_05: ApplyMatrix, a matrix applied to a short vector. Assembly in
 * Sony's library: scratch in $t0-$t4 with $v1 idle. */
#include "include_asm.h"

INCLUDE_ASM("asm/funcs", ApplyMatrix);
