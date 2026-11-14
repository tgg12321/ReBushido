/* LIBGTE REG12: SetGeomOffset, which sets where on the screen the center of
 * projection is. Assembly in Sony's library: two nops of padding to a
 * multiple of 16 bytes, which a compiled module never has. */
#include "include_asm.h"

#include "rebushido.h"

/* The native port has no assembly: the C here does what the listing does
 * (docs/PORT.md). */
#if RB_PORT
#include "psxsdk/libgte.h"
#include "port_gte.h"

/* In C: the screen's offset, which the GTE keeps with 16 bits of
 * fraction. */
void SetGeomOffset(s32 ofx, s32 ofy) {
    port_GteWriteControl(PORT_GTE_OFX, (u32)ofx << 16);
    port_GteWriteControl(PORT_GTE_OFY, (u32)ofy << 16);
}
#else
INCLUDE_ASM("asm/funcs", SetGeomOffset);
#endif
