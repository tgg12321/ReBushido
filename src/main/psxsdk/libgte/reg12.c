/* LIBGTE REG12: SetGeomOffset, which sets where on the screen the center of
 * projection is. Assembly in Sony's library: two nops of padding to a
 * multiple of 16 bytes, which a compiled module never has. */
#include "include_asm.h"

INCLUDE_ASM("asm/funcs", SetGeomOffset);
