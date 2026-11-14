/* LIBGTE SMP_00: the small GTE operations: lighting and depth cueing a
 * colour, interpolation, squares, average depths, outer products and the
 * count of leading zeros. Assembly in Sony's library: scratch in the $t
 * registers with $v1 idle, and a nop of padding to a multiple of 16 bytes. */
#include "include_asm.h"

#include "rebushido.h"

/* The native port has no assembly: the C here does what the listings do
 * (docs/PORT.md). */
#if RB_PORT
#include "psxsdk/libgte.h"
#include "port_gte.h"

/* In C: the colour of the lights that fall as `v0` says. */
void LightColor(VECTOR *v0, VECTOR *v1) {
    port_GteLoadData(PORT_GTE_IR1, v0, 3);
    port_GteCommand(PORT_GTE_CMD_LCIRBK);
    port_GteStoreData(PORT_GTE_IR1, v1, 3);
}

/* In C: a colour lit by `v0` and faded by `p` to the far colour. */
void DpqColorLight(VECTOR *v0, CVECTOR *v1, s32 p, CVECTOR *v2) {
    port_GteLoadData(PORT_GTE_IR1, v0, 3);
    port_GteLoadData(PORT_GTE_RGBC, v1, 1);
    port_GteWriteData(PORT_GTE_IR0, (u32)p);
    port_GteCommand(PORT_GTE_CMD_DPCL);
    port_GteStoreData(PORT_GTE_RGB2, v2, 1);
}

/* In C: three colours faded by `p` to the far colour. */
void DpqColor3(CVECTOR *v0, CVECTOR *v1, CVECTOR *v2, s32 p, CVECTOR *v3,
               CVECTOR *v4, CVECTOR *v5) {
    port_GteLoadData(PORT_GTE_RGB0, v0, 1);
    port_GteLoadData(PORT_GTE_RGB1, v1, 1);
    port_GteLoadData(PORT_GTE_RGB2, v2, 1);
    port_GteLoadData(PORT_GTE_RGBC, v2, 1);
    port_GteWriteData(PORT_GTE_IR0, (u32)p);
    port_GteCommand(PORT_GTE_CMD_DPCT);
    port_GteStoreData(PORT_GTE_RGB0, v3, 1);
    port_GteStoreData(PORT_GTE_RGB1, v4, 1);
    port_GteStoreData(PORT_GTE_RGB2, v5, 1);
}

/* In C: a vector faded by `p` to the far colour, as a colour. */
void Intpl(VECTOR *v0, s32 p, CVECTOR *v1) {
    port_GteLoadData(PORT_GTE_IR1, v0, 3);
    port_GteWriteData(PORT_GTE_IR0, (u32)p);
    port_GteCommand(PORT_GTE_CMD_INTPL);
    port_GteStoreData(PORT_GTE_RGB2, v1, 1);
}

/* In C: each number squared; the numbers have 12 bits of fraction. */
VECTOR *Square12(VECTOR *v0, VECTOR *v1) {
    port_GteLoadData(PORT_GTE_IR1, v0, 3);
    port_GteCommand(PORT_GTE_CMD_SQR12);
    port_GteStoreData(PORT_GTE_MAC1, v1, 3);
    return v1;
}

/* In C: each number squared; the numbers are whole. */
VECTOR *Square0(VECTOR *v0, VECTOR *v1) {
    port_GteLoadData(PORT_GTE_IR1, v0, 3);
    port_GteCommand(PORT_GTE_CMD_SQR0);
    port_GteStoreData(PORT_GTE_MAC1, v1, 3);
    return v1;
}

/* In C: the ordering table's index for three depths. */
s32 AverageZ3(s32 sz0, s32 sz1, s32 sz2) {
    port_GteWriteData(PORT_GTE_SZ1, (u32)sz0);
    port_GteWriteData(PORT_GTE_SZ2, (u32)sz1);
    port_GteWriteData(PORT_GTE_SZ3, (u32)sz2);
    port_GteCommand(PORT_GTE_CMD_AVSZ3);
    return (s32)port_GteReadData(PORT_GTE_OTZ);
}

/* In C: and for four. */
s32 AverageZ4(s32 sz0, s32 sz1, s32 sz2, s32 sz3) {
    port_GteWriteData(PORT_GTE_SZ0, (u32)sz0);
    port_GteWriteData(PORT_GTE_SZ1, (u32)sz1);
    port_GteWriteData(PORT_GTE_SZ2, (u32)sz2);
    port_GteWriteData(PORT_GTE_SZ3, (u32)sz3);
    port_GteCommand(PORT_GTE_CMD_AVSZ4);
    return (s32)port_GteReadData(PORT_GTE_OTZ);
}

/* The outer product of two vectors. The GTE takes the first from the
 * rotation matrix's diagonal, which is put back afterwards. */
static void OuterProduct(VECTOR *v0, VECTOR *v1, VECTOR *v2, u32 command) {
    u32 diagonal[3];
    s32 i;

    for (i = 0; i < 3; i++) {
        diagonal[i] = port_GteReadControl(PORT_GTE_ROTATION + 2 * i);
        port_GteWriteControl(PORT_GTE_ROTATION + 2 * i, (u32)(&v0->vx)[i]);
    }
    port_GteLoadData(PORT_GTE_IR1, v1, 3);
    port_GteCommand(command);
    port_GteStoreData(PORT_GTE_MAC1, v2, 3);
    for (i = 0; i < 3; i++) {
        port_GteWriteControl(PORT_GTE_ROTATION + 2 * i, diagonal[i]);
    }
}

/* In C: v2 = v0 x v1; the numbers have 12 bits of fraction. */
void OuterProduct12(VECTOR *v0, VECTOR *v1, VECTOR *v2) {
    OuterProduct(v0, v1, v2, PORT_GTE_CMD_OP12);
}

/* In C: v2 = v0 x v1; the numbers are whole. */
void OuterProduct0(VECTOR *v0, VECTOR *v1, VECTOR *v2) {
    OuterProduct(v0, v1, v2, PORT_GTE_CMD_OP0);
}

/* In C: how many of a number's top bits are the same as its sign. */
s32 Lzc(s32 data) {
    port_GteWriteData(PORT_GTE_LZCS, (u32)data);
    return (s32)port_GteReadData(PORT_GTE_LZCR);
}
#else
INCLUDE_ASM("asm/funcs", LightColor);
INCLUDE_ASM("asm/funcs", DpqColorLight);
INCLUDE_ASM("asm/funcs", DpqColor3);
INCLUDE_ASM("asm/funcs", Intpl);
INCLUDE_ASM("asm/funcs", Square12);
INCLUDE_ASM("asm/funcs", Square0);
INCLUDE_ASM("asm/funcs", AverageZ3);
INCLUDE_ASM("asm/funcs", AverageZ4);
INCLUDE_ASM("asm/funcs", OuterProduct12);
INCLUDE_ASM("asm/funcs", OuterProduct0);
INCLUDE_ASM("asm/funcs", Lzc);
#endif
