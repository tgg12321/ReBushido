/* LIBGTE SMP_02: RotTransPers, the perspective transform of one vertex.
 * Assembly in Sony's library: a nop of padding to a multiple of 16 bytes,
 * which a compiled module never has. */
#include "include_asm.h"

#include "rebushido.h"

/* The native port has no assembly: the C here does what the listing does
 * (docs/PORT.md). */
#if RB_PORT
#include "psxsdk/libgte.h"
#include "port_gte.h"

/* In C: a point to the screen. Returns a quarter of its depth. */
s32 RotTransPers(SVECTOR *v0, s32 *sxy, s32 *p, s32 *flag) {
    port_GteLoadData(PORT_GTE_VXY0, v0, 2);
    port_GteCommand(PORT_GTE_CMD_RTPS);
    *sxy = (s32)port_GteReadData(PORT_GTE_SXY2);
    *p = (s32)port_GteReadData(PORT_GTE_IR0);
    *flag = (s32)port_GteReadControl(PORT_GTE_FLAG);
    return (s32)port_GteReadData(PORT_GTE_SZ3) >> 2;
}
#else
INCLUDE_ASM("asm/funcs", RotTransPers);
#endif
