/* LIBGTE CMB_00: RotTransPers4, the perspective transform of four vertices at
 * once. Assembly in Sony's library: GTE instructions throughout, and two nops
 * of padding to a multiple of 16 bytes, which a compiled module never has. */
#include "include_asm.h"

#include "rebushido.h"

/* The native port has no assembly: the C here does what the listing does
 * (docs/PORT.md). */
#if RB_PORT
#include "psxsdk/libgte.h"
#include "port_gte.h"

/* In C: three points to the screen, then the fourth; the flags of the
 * two operations together. Returns a quarter of the fourth's depth. */
s32 RotTransPers4(SVECTOR *v0, SVECTOR *v1, SVECTOR *v2, SVECTOR *v3, s32 *sxy0,
                  s32 *sxy1, s32 *sxy2, s32 *sxy3, s32 *p, s32 *flag) {
    u32 first_flag;

    port_GteLoadData(PORT_GTE_VXY0, v0, 2);
    port_GteLoadData(PORT_GTE_VXY1, v1, 2);
    port_GteLoadData(PORT_GTE_VXY2, v2, 2);
    port_GteCommand(PORT_GTE_CMD_RTPT);
    port_GteStoreData(PORT_GTE_SXY0, sxy0, 1);
    port_GteStoreData(PORT_GTE_SXY1, sxy1, 1);
    port_GteStoreData(PORT_GTE_SXY2, sxy2, 1);
    first_flag = port_GteReadControl(PORT_GTE_FLAG);
    port_GteLoadData(PORT_GTE_VXY0, v3, 2);
    port_GteCommand(PORT_GTE_CMD_RTPS);
    port_GteStoreData(PORT_GTE_SXY2, sxy3, 1);
    *p = (s32)port_GteReadData(PORT_GTE_IR0);
    *flag = (s32)(port_GteReadControl(PORT_GTE_FLAG) | first_flag);
    return (s32)port_GteReadData(PORT_GTE_SZ3) >> 2;
}
#else
INCLUDE_ASM("asm/funcs", RotTransPers4);
#endif
