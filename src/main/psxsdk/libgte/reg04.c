/* LIBGTE REG04: ReadSZfifo3, which reads the depths of the last three
 * vertices transformed. Assembly in Sony's library: three nops of padding to
 * a multiple of 16 bytes, which a compiled module never has. */
#include "include_asm.h"

INCLUDE_ASM("asm/funcs", ReadSZfifo3);
