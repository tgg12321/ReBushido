/* LIBGTE MSC06: the LoadAverage functions, each a weighted average of two
 * vectors or colours. Assembly in Sony's library: scratch in the $t registers
 * with $v1 idle, and a nop of padding to a multiple of 16 bytes. */
#include "include_asm.h"

#include "rebushido.h"

/* The native port has no assembly: the C here does what the listings do
 * (docs/PORT.md). */
#if RB_PORT
#include "psxsdk/libgte.h"
#include "port_gte.h"

/* The weighted sum of two vectors that are already in memory as words
 * of IR: first * p0, then + second * p1. */
static void Average(const s32 *first, const s32 *second, s32 count, s32 p0,
                    s32 p1, u32 scale, u32 add) {
    port_GteWriteData(PORT_GTE_IR0, (u32)p0);
    port_GteLoadData(PORT_GTE_IR1, first, count);
    port_GteCommand(scale);
    port_GteWriteData(PORT_GTE_IR0, (u32)p1);
    port_GteLoadData(PORT_GTE_IR1, second, count);
    port_GteCommand(add);
}

/* In C: v2 = v0 * p0 + v1 * p1, each number held to 16 bits. */
void LoadAverage12(VECTOR *v0, VECTOR *v1, s32 p0, s32 p1, VECTOR *v2) {
    Average(
        &v0->vx, &v1->vx, 3, p0, p1, PORT_GTE_CMD_GPF12, PORT_GTE_CMD_GPL12);
    port_GteStoreData(PORT_GTE_IR1, v2, 3);
}

/* In C: the same with the fraction kept. */
void LoadAverage0(VECTOR *v0, VECTOR *v1, s32 p0, s32 p1, VECTOR *v2) {
    Average(&v0->vx, &v1->vx, 3, p0, p1, PORT_GTE_CMD_GPF0, PORT_GTE_CMD_GPL0);
    port_GteStoreData(PORT_GTE_IR1, v2, 3);
}

/* The same for vectors of halfwords. The third number is stored as a
 * word, which writes the vector's fourth halfword as well. */
static void AverageShort(
    SVECTOR *v0, SVECTOR *v1, s32 p0, s32 p1, SVECTOR *v2, u32 scale, u32 add) {
    s32 first[3];
    s32 second[3];

    first[0] = v0->vx;
    first[1] = v0->vy;
    first[2] = v0->vz;
    second[0] = v1->vx;
    second[1] = v1->vy;
    second[2] = v1->vz;
    Average(first, second, 3, p0, p1, scale, add);
    v2->vx = (s16)port_GteReadData(PORT_GTE_IR1);
    v2->vy = (s16)port_GteReadData(PORT_GTE_IR2);
    ((u32 *)v2)[1] = port_GteReadData(PORT_GTE_IR3);
}

/* In C. */
void LoadAverageShort12(SVECTOR *v0, SVECTOR *v1, s32 p0, s32 p1, SVECTOR *v2) {
    AverageShort(v0, v1, p0, p1, v2, PORT_GTE_CMD_GPF12, PORT_GTE_CMD_GPL12);
}

/* In C. */
void LoadAverageShort0(SVECTOR *v0, SVECTOR *v1, s32 p0, s32 p1, SVECTOR *v2) {
    AverageShort(v0, v1, p0, p1, v2, PORT_GTE_CMD_GPF0, PORT_GTE_CMD_GPL0);
}

/* The same for `count` bytes, two or three: the sums keep their
 * fraction, and each byte is its sum's whole part. */
static void AverageBytes(
    const u8 *v0, const u8 *v1, s32 p0, s32 p1, u8 *v2, s32 count) {
    s32 first[3];
    s32 second[3];
    s32 i;

    for (i = 0; i < count; i++) {
        first[i] = v0[i];
        second[i] = v1[i];
    }
    Average(first, second, count, p0, p1, PORT_GTE_CMD_GPF0, PORT_GTE_CMD_GPL0);
    for (i = 0; i < count; i++) {
        v2[i] = (u8)((s32)port_GteReadData(PORT_GTE_MAC1 + i) >> 12);
    }
}

/* In C: two bytes. */
void LoadAverageByte(u8 *v0, u8 *v1, s32 p0, s32 p1, u8 *v2) {
    AverageBytes(v0, v1, p0, p1, v2, 2);
}

/* In C: a colour's three bytes. */
void LoadAverageCol(u8 *v0, u8 *v1, s32 p0, s32 p1, u8 *v2) {
    AverageBytes(v0, v1, p0, p1, v2, 3);
}
#else
INCLUDE_ASM("asm/funcs", LoadAverage12);
INCLUDE_ASM("asm/funcs", LoadAverage0);
INCLUDE_ASM("asm/funcs", LoadAverageShort12);
INCLUDE_ASM("asm/funcs", LoadAverageShort0);
INCLUDE_ASM("asm/funcs", LoadAverageByte);
INCLUDE_ASM("asm/funcs", LoadAverageCol);
#endif
