/* LIBGTE REG10: SetBackColor, which sets the ambient colour. Probably
 * assembly in Sony's library, as the other REG modules are. The bytes do not
 * say: `rbk <<= 4; gbk <<= 4; bbk <<= 4; gte_ldbkdir(rbk, gbk, bbk);`
 * compiles to the same eight instructions, and at 32 bytes the module needs
 * no padding to tell by. */
#include "include_asm.h"

#include "rebushido.h"

/* The native port has no assembly: the C here does what the listing does
 * (docs/PORT.md). */
#if RB_PORT
#include "psxsdk/libgte.h"
#include "port_gte.h"

/* In C: the background colour, in sixteenths of a colour's unit. */
void SetBackColor(s32 rbk, s32 gbk, s32 bbk) {
    port_GteWriteControl(PORT_GTE_RBK, (u32)rbk << 4);
    port_GteWriteControl(PORT_GTE_GBK, (u32)gbk << 4);
    port_GteWriteControl(PORT_GTE_BBK, (u32)bbk << 4);
}
#else
INCLUDE_ASM("asm/funcs", SetBackColor);
#endif
