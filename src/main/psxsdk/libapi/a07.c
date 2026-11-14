/* LIBAPI A07: DeliverEvent, the BIOS B(0x07) trampoline. */
#define INCLUDE_ASM_USE_MACRO_INC 1
#include "include_asm.h"
#include "bios.h"

BIOS_B_FUNCTION(DeliverEvent, 0x7);
