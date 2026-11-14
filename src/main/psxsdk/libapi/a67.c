/* LIBAPI A67: nextfile, the BIOS B(0x43) trampoline. */
#define INCLUDE_ASM_USE_MACRO_INC 1
#include "include_asm.h"
#include "bios.h"

BIOS_B_FUNCTION(nextfile, 0x43);
