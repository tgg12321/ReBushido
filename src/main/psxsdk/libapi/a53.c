/* LIBAPI A53: write, the BIOS B(0x35) trampoline. */
#define INCLUDE_ASM_USE_MACRO_INC 1
#include "include_asm.h"
#include "bios.h"

BIOS_B_FUNCTION(write, 0x35);
