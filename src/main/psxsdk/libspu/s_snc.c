/* LIBSPU S_SNC: SpuSetNoiseClock. */
#include "common.h"
#include "libspu_internal.h"

s32 SpuSetNoiseClock(s32 n_clock) {
    s32 val;
    if (n_clock < 0) {
        val = 0;
    } else if (n_clock >= 0x40) {
        val = 0x3F;
    } else {
        val = n_clock;
    }
    _spu_RXX->rxx.spucnt =
        (_spu_RXX->rxx.spucnt & 0xC0FF) | ((val & 0x3F) << 8);
    return val;
}
