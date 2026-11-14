/* LIBSPU S_STSA: SpuSetTransferStartAddr. */
#include "common.h"
#include "libspu_internal.h"

s32 SpuSetTransferStartAddr(s32 addr) {
    s32 v0;
    if ((u32)(addr - 0x1010) > (u32)0x7EFE8) {
        return 0;
    }
    v0 = _spu_FsetRXXa(-1, addr);
    _spu_tsa = (u16)v0;
    return (u32)(u16)v0 << _spu_mem_mode_plus;
}
