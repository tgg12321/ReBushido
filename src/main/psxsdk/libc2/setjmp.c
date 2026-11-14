/* LIBC2 SETJMP: setjmp, with longjmp's half after it under no label of its
 * own. Assembly in Sony's library: it stores and loads $ra, $sp, $fp, $gp and
 * $s0-$s7 themselves, and has two nops of padding to a multiple of 16 bytes. */
#include "include_asm.h"

INCLUDE_ASM("asm/funcs", setjmp);
