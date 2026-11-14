/* LIBGTE REG11: SetFarColor, which sets the colour that depth cueing fades
 * to. Probably assembly in Sony's library, as the other REG modules are. The
 * bytes do not say: `rfc <<= 4; gfc <<= 4; bfc <<= 4; gte_ldfcdir(rfc, gfc,
 * bfc);` compiles to the same eight instructions, and at 32 bytes the module
 * needs no padding to tell by. */
#include "include_asm.h"

INCLUDE_ASM("asm/funcs", SetFarColor);
