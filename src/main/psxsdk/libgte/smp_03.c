/* LIBGTE SMP_03: RotTransPers3, the perspective transform of three vertices
 * at once. Assembly in Sony's library: three nops of padding to a multiple of
 * 16 bytes, which a compiled module never has. */
#include "include_asm.h"

INCLUDE_ASM("asm/funcs", RotTransPers3);
