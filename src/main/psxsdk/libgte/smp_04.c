/* LIBGTE SMP_04: RotTrans, the rotation and translation of one vertex.
 * Assembly in Sony's library: two nops of padding to a multiple of 16 bytes,
 * which a compiled module never has. */
#include "include_asm.h"

#include "rebushido.h"

/* The native port has no assembly: the C here does what the listing does
 * (docs/PORT.md). */
#if RB_PORT
#include "psxsdk/libgte.h"
#include "port_gte.h"

/* In C: a point through the rotation and the translation. */
void RotTrans(SVECTOR *v0, VECTOR *v1, s32 *flag) {
    port_GteLoadData(PORT_GTE_VXY0, v0, 2);
    port_GteCommand(PORT_GTE_CMD_RTV0TR);
    port_GteStoreData(PORT_GTE_MAC1, v1, 3);
    *flag = (s32)port_GteReadControl(PORT_GTE_FLAG);
}
#else
INCLUDE_ASM("asm/funcs", RotTrans);
#endif
