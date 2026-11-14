/* LIBSPU S_STM: SpuSetTransferMode. */
#include "common.h"
#include "libspu_internal.h"

s32 SpuSetTransferMode(s32 mode) {
    s32 transMode;

    switch (mode) {
    case 0:
        transMode = 0;
        break;
    case 1:
        transMode = 1;
        break;
    default:
        transMode = 0;
    }
    _spu_trans_mode = mode;
    _spu_transMode = transMode;
    return transMode;
}
