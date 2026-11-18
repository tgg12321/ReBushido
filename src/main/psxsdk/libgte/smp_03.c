/* LIBGTE SMP_03: RotTransPers3, the perspective transform of three vertices
 * at once. Assembly in Sony's library: three nops of padding to a multiple of
 * 16 bytes, which a compiled module never has. */
#include "include_asm.h"

#include "rebushido.h"

/* The native port has no assembly: the C here does what the listing does
 * (docs/PORT.md). */
#if RB_PORT
#include "psxsdk/libgte.h"
#include "port_gte.h"

/* In C: three points to the screen. Returns a quarter of the last one's
 * depth. */
s32 RotTransPers3(SVECTOR *v0, SVECTOR *v1, SVECTOR *v2, s32 *sxy0, s32 *sxy1,
                  s32 *sxy2, s32 *p, s32 *flag) {
    port_GteLoadData(PORT_GTE_VXY0, v0, 2);
    port_GteLoadData(PORT_GTE_VXY1, v1, 2);
    port_GteLoadData(PORT_GTE_VXY2, v2, 2);
    port_GteCommand(PORT_GTE_CMD_RTPT);
    port_GteStoreData(PORT_GTE_SXY0, sxy0, 1);
    port_GteStoreData(PORT_GTE_SXY1, sxy1, 1);
    port_GteStoreData(PORT_GTE_SXY2, sxy2, 1);
    *p = (s32)port_GteReadData(PORT_GTE_IR0);
    *flag = (s32)port_GteReadControl(PORT_GTE_FLAG);
    return (s32)port_GteReadData(PORT_GTE_SZ3) >> 2;
}
#else
INCLUDE_ASM("asm/funcs", RotTransPers3);
#endif
