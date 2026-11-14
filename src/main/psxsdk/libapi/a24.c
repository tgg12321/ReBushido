/* LIBAPI A24: ResetEntryInt, the BIOS B(0x18) trampoline. */
#define INCLUDE_ASM_USE_MACRO_INC 1
#include "include_asm.h"
#include "bios.h"

BIOS_B_FUNCTION(ResetEntryInt, 0x18);
