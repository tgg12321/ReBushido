/* LIBAPI A11: TestEvent, the BIOS B(0x0B) trampoline. */
#define INCLUDE_ASM_USE_MACRO_INC 1
#include "include_asm.h"
#include "bios.h"

BIOS_B_FUNCTION(TestEvent, 0xB);
