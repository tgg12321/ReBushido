/* LIBGTE REG13: SetGeomScreen, which sets the distance from the eye to the
 * screen. Assembly in Sony's library: a nop of padding to a multiple of 16
 * bytes, which a compiled module never has. */
#include "include_asm.h"

INCLUDE_ASM("asm/funcs", SetGeomScreen);
