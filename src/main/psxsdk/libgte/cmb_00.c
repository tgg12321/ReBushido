/* LIBGTE CMB_00: RotTransPers4, the perspective transform of four vertices at
 * once. Assembly in Sony's library: GTE instructions throughout, and two nops
 * of padding to a multiple of 16 bytes, which a compiled module never has. */
#include "include_asm.h"

INCLUDE_ASM("asm/funcs", RotTransPers4);
