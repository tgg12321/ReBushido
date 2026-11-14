/* LIBGTE MTX_11: SetColorMatrix, which loads the GTE's light colour matrix.
 * Assembly in Sony's library: the five words are loaded into $t0-$t4 in a
 * block, with $v0 and $v1 idle. */
#include "include_asm.h"

INCLUDE_ASM("asm/funcs", SetColorMatrix);
