/* LIBSPU S_W: SpuWrite, which starts a transfer to sound memory. */
#include "common.h"
#include "libspu_internal.h"

s32 SpuWrite(s32 addr, s32 size) {
    if ((u32)size > 0x7EFF0u) {
        size = 0x7EFF0;
    }
    _spu_Fw(addr, size);
    if (_spu_transferCallback == NULL) {
        _spu_inTransfer = 0;
    }
    return size;
}
