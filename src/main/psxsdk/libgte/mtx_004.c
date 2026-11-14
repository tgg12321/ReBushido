/* LIBGTE MTX_004: ApplyMatrixLV, a matrix applied to a vector of long
 * components. Assembly in Sony's library: scratch in $t0-$t5 with $v1 idle. */
#include "include_asm.h"

#include "rebushido.h"

/* The native port has no assembly: the C here does what the listing does
 * (docs/PORT.md). */
#if RB_PORT
#include "psxsdk/libgte.h"
#include "port_gte.h"

/* A 32-bit vector through the rotation in the GTE, which multiplies 16
 * bits at a time: each number is split into its low 15 bits and the
 * rest, with the sign on both; the two halves go through the rotation,
 * the upper with its fraction kept; and the results are put together. */
static void ApplyLongVector(const VECTOR *in, VECTOR *out) {
    const s32 *from = &in->vx;
    s32 *to = &out->vx;
    s32 low[3];
    s32 high[3];
    s32 upper[3];
    s32 i;

    for (i = 0; i < 3; i++) {
        s32 value = from[i];

        if (value < 0) {
            high[i] = -((s32)(0U - (u32)value) >> 15);
            low[i] = -((s32)(0U - (u32)value) & 0x7FFF);
        } else {
            high[i] = value >> 15;
            low[i] = value & 0x7FFF;
        }
    }
    port_GteLoadData(PORT_GTE_IR1, high, 3);
    port_GteCommand(PORT_GTE_CMD_RTIR_SF0);
    port_GteStoreData(PORT_GTE_MAC1, upper, 3);
    port_GteLoadData(PORT_GTE_IR1, low, 3);
    port_GteCommand(PORT_GTE_CMD_RTIR);
    for (i = 0; i < 3; i++) {
        to[i] =
            (s32)(port_GteReadData(PORT_GTE_MAC1 + i) + ((u32)upper[i] << 3));
    }
}

/* In C: a 32-bit vector through a matrix's rotation, which is left in
 * the GTE. */
VECTOR *ApplyMatrixLV(MATRIX *m, VECTOR *v0, VECTOR *v1) {
    port_GteLoadControl(PORT_GTE_ROTATION, m, PORT_GTE_MATRIX_WORDS);
    ApplyLongVector(v0, v1);
    return v1;
}
#else
INCLUDE_ASM("asm/funcs", ApplyMatrixLV);
#endif
