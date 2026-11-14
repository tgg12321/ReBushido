/* LIBAPI A23: ReturnFromException, the BIOS B(0x17) trampoline. */
#define INCLUDE_ASM_USE_MACRO_INC 1
#include "include_asm.h"
#include "bios.h"

BIOS_B_FUNCTION(ReturnFromException, 0x17);
