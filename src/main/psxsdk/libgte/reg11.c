/* LIBGTE REG11: SetFarColor, which sets the colour that depth cueing fades
 * to. Probably assembly in Sony's library, as the other REG modules are. The
 * bytes do not say: `rfc <<= 4; gfc <<= 4; bfc <<= 4; gte_ldfcdir(rfc, gfc,
 * bfc);` compiles to the same eight instructions, and at 32 bytes the module
 * needs no padding to tell by. */
#include "include_asm.h"

#include "rebushido.h"

/* The native port has no assembly: the C here does what the listing does
 * (docs/PORT.md). */
#if RB_PORT
#include "psxsdk/libgte.h"
#include "port_gte.h"

/* In C: the far colour, in sixteenths of a colour's unit. */
void SetFarColor(s32 rfc, s32 gfc, s32 bfc) {
    port_GteWriteControl(PORT_GTE_RFC, (u32)rfc << 4);
    port_GteWriteControl(PORT_GTE_GFC, (u32)gfc << 4);
    port_GteWriteControl(PORT_GTE_BFC, (u32)bfc << 4);
}
#else
INCLUDE_ASM("asm/funcs", SetFarColor);
#endif
