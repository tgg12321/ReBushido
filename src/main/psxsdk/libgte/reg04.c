/* LIBGTE REG04: ReadSZfifo3, which reads the depths of the last three
 * vertices transformed. Assembly in Sony's library: three nops of padding to
 * a multiple of 16 bytes, which a compiled module never has. */
#include "include_asm.h"

#include "rebushido.h"

/* The native port has no assembly: the C here does what the listing does
 * (docs/PORT.md). */
#if RB_PORT
#include "psxsdk/libgte.h"
#include "port_gte.h"

/* In C: the last three depths, oldest first. */
void ReadSZfifo3(s32 *sz0, s32 *sz1, s32 *sz2) {
    *sz0 = (s32)port_GteReadData(PORT_GTE_SZ1);
    *sz1 = (s32)port_GteReadData(PORT_GTE_SZ2);
    *sz2 = (s32)port_GteReadData(PORT_GTE_SZ3);
}
#else
INCLUDE_ASM("asm/funcs", ReadSZfifo3);
#endif
