/* LIBAPI A25: HookEntryInt, the BIOS B(0x19) trampoline. */
#define INCLUDE_ASM_USE_MACRO_INC 1
#include "include_asm.h"
#include "bios.h"

BIOS_B_FUNCTION(HookEntryInt, 0x19);
