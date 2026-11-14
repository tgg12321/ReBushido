/* LIBSPU S_GVV: SpuGetVoiceVolume. */
#include "common.h"
#include "libspu_internal.h"

static inline void assign(u16 val, s16 *out) {
    u32 offset = 0x8000;
    u32 uval = val;

    if (uval >= 0x4000) {
        *out = uval - offset;
    } else {
        *out = val;
    }
}

void SpuGetVoiceVolume(s32 vNum, s16 *volL, s16 *volR) {
    u16 left = _spu_RXX->raw[vNum * 8];
    u16 right = _spu_RXX->raw[vNum * 8 + 1];

    assign(left, volL);
    assign(right, volR);
}
