/* LIBGTE MSC06: the LoadAverage functions, each a weighted average of two
 * vectors or colours. Assembly in Sony's library: scratch in the $t registers
 * with $v1 idle, and a nop of padding to a multiple of 16 bytes. */
#include "include_asm.h"

INCLUDE_ASM("asm/funcs", LoadAverage12);
INCLUDE_ASM("asm/funcs", LoadAverage0);
INCLUDE_ASM("asm/funcs", LoadAverageShort12);
INCLUDE_ASM("asm/funcs", LoadAverageShort0);
INCLUDE_ASM("asm/funcs", LoadAverageByte);
INCLUDE_ASM("asm/funcs", LoadAverageCol);
