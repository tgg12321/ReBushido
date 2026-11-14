/* LIBGTE SMP_04: RotTrans, the rotation and translation of one vertex.
 * Assembly in Sony's library: two nops of padding to a multiple of 16 bytes,
 * which a compiled module never has. */
#include "include_asm.h"

INCLUDE_ASM("asm/funcs", RotTrans);
