/* LIBGTE REG10: SetBackColor, which sets the ambient colour. Probably
 * assembly in Sony's library, as the other REG modules are. The bytes do not
 * say: `rbk <<= 4; gbk <<= 4; bbk <<= 4; gte_ldbkdir(rbk, gbk, bbk);`
 * compiles to the same eight instructions, and at 32 bytes the module needs
 * no padding to tell by. */
#include "include_asm.h"

INCLUDE_ASM("asm/funcs", SetBackColor);
