/* LIBGTE MSC09: SquareRoot12, the square root of a number with twelve
 * fraction bits. Assembly in Sony's library: five addi and sub, which trap on
 * overflow and which the compiler never emits, scratch in $t0-$t5 with $v1
 * idle, and three nops of padding to a multiple of 16 bytes. */
#include "include_asm.h"

#include "rebushido.h"

/* The native port has no assembly: the C here does what the listing does
 * (docs/PORT.md). */
#if RB_PORT
#include "psxsdk/libgte.h"
#include "port_gte.h"
extern s16 SQRT[192];

/* The table's root for the top bits of `a`, and through `shift` how far
 * the number was moved to bring them there: the count of its leading
 * zeroes, made even. NULL for 0. A negative number has no root: the
 * assembly reads outside the table for it, and here it is taken for 0. */
static const s16 *RootEntry(s32 a, s32 *shift) {
    s32 zeroes;
    s32 top;

    port_GteWriteData(PORT_GTE_LZCS, (u32)a);
    zeroes = (s32)port_GteReadData(PORT_GTE_LZCR);
    if (zeroes == 32 || a < 0) {
        return NULL;
    }
    zeroes &= ~1;
    *shift = zeroes;
    /* the number's top eight bits, the first of them at bit 6 or 7 */
    top = (zeroes >= 24) ? a << (zeroes - 24) : a >> (24 - zeroes);
    return &SQRT[top - 0x40];
}

/* In C: the square root of a number with 12 bits of fraction, which has
 * 12 bits of fraction too. */
s32 SquareRoot12(s32 a) {
    s32 shift;
    const s16 *entry = RootEntry(a, &shift);

    if (!entry) {
        return 0;
    }
    shift = (19 - shift) >> 1;
    return (shift >= 0) ? *entry << shift : (s32)((u32)*entry >> -shift);
}
#else
INCLUDE_ASM("asm/funcs", SquareRoot12);
#endif
