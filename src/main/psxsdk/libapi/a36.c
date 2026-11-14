/* LIBAPI A36: EnterCriticalSection (syscall 1). Assembly in Sony's library: a
 * syscall has no C form. */
#define INCLUDE_ASM_USE_MACRO_INC 1
#include "include_asm.h"

INCLUDE_ASM("asm/funcs", EnterCriticalSection);
