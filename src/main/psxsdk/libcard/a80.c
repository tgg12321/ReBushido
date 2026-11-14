/* LIBCARD A80: _new_card, the BIOS B(0x50) trampoline. */
#define INCLUDE_ASM_USE_MACRO_INC 1
#include "include_asm.h"
#include "bios.h"

BIOS_B_FUNCTION(_new_card, 0x50);
