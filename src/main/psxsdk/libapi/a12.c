/* LIBAPI A12: EnableEvent, the BIOS B(0x0C) trampoline. */
#define INCLUDE_ASM_USE_MACRO_INC 1
#include "include_asm.h"
#include "bios.h"

BIOS_B_FUNCTION(EnableEvent, 0xC);
