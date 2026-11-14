/* LIBAPI C68: FlushCache, the BIOS A(0x44) trampoline. */
#define INCLUDE_ASM_USE_MACRO_INC 1
#include "include_asm.h"
#include "bios.h"

BIOS_A_FUNCTION(FlushCache, 0x44);
