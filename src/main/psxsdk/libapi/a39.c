/* LIBAPI A39: SetSp, which sets the stack pointer and returns the old one.
 * Assembly in Sony's library: C cannot name $sp. */
#define INCLUDE_ASM_USE_MACRO_INC 1
#include "include_asm.h"

INCLUDE_ASM("asm/funcs", SetSp);
