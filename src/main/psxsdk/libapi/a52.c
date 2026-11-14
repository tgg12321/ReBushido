/* LIBAPI A52: read, the BIOS B(0x34) trampoline. */
#define INCLUDE_ASM_USE_MACRO_INC 1
#include "include_asm.h"
#include "bios.h"

BIOS_B_FUNCTION(read, 0x34);
