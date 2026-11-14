/* LIBAPI CHCLRPAD: _remove_ChgclrPAD. Assembly in Sony's library: the return
 * address is kept in a variable, not on the stack, the BIOS is called through
 * $t2, and there is an addi. */
#define INCLUDE_ASM_USE_MACRO_INC 1
#include "include_asm.h"
#include "common.h"

/* The return address, kept while the BIOS is called. The name is not Sony's. */
s32 _remove_ChgclrPAD_ra;

INCLUDE_ASM("asm/funcs", _remove_ChgclrPAD);
