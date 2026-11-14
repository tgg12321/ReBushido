/* LIBC2 RAND: rand and srand. */
#include "common.h"
#include <psxsdk/libc.h>

static u32 _randx; /* the last number */

s32 rand(void) {
    _randx = _randx * 0x41C64E6D + 0x3039;
    return (_randx >> 16) & 0x7FFF;
}

void srand(u32 seed) { _randx = seed; }
