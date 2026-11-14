/* LIBGTE FGO_01: RotMatrix, which makes a rotation matrix from three angles.
 * Assembly in Sony's library: no frame, all of $t0-$t9 as scratch with $v1
 * idle, and a nop of padding to a multiple of 16 bytes. */
#include "include_asm.h"

INCLUDE_ASM("asm/funcs", RotMatrix);
