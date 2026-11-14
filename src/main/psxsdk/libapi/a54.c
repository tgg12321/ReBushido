/* LIBAPI A54: close, the BIOS B(0x36) trampoline. */
#define INCLUDE_ASM_USE_MACRO_INC 1
#include "include_asm.h"
#include "bios.h"

BIOS_B_FUNCTION(close, 0x36);
