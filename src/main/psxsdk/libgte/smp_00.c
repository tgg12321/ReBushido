/* LIBGTE SMP_00: the small GTE operations: lighting and depth cueing a
 * colour, interpolation, squares, average depths, outer products and the
 * count of leading zeros. Assembly in Sony's library: scratch in the $t
 * registers with $v1 idle, and a nop of padding to a multiple of 16 bytes. */
#include "include_asm.h"

INCLUDE_ASM("asm/funcs", LightColor);
INCLUDE_ASM("asm/funcs", DpqColorLight);
INCLUDE_ASM("asm/funcs", DpqColor3);
INCLUDE_ASM("asm/funcs", Intpl);
INCLUDE_ASM("asm/funcs", Square12);
INCLUDE_ASM("asm/funcs", Square0);
INCLUDE_ASM("asm/funcs", AverageZ3);
INCLUDE_ASM("asm/funcs", AverageZ4);
INCLUDE_ASM("asm/funcs", OuterProduct12);
INCLUDE_ASM("asm/funcs", OuterProduct0);
INCLUDE_ASM("asm/funcs", Lzc);
