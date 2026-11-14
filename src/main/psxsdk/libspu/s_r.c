/* LIBSPU S_R: SpuRead, which starts a transfer from sound memory. */
#include "common.h"
#include "libspu_internal.h"

s32 SpuRead(s32 addr, s32 size) {
    if ((u32)size > 0x7EFF0u) {
        size = 0x7EFF0;
    }
    _spu_Fr(addr, size);
    if (_spu_transferCallback == NULL) {
        _spu_inTransfer = 0;
    }
    return size;
}
