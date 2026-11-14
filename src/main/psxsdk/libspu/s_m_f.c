/* LIBSPU S_M_F: SpuFree. */
#include "common.h"
#include "libspu_internal.h"

void SpuFree(u32 addr) {
    s32 i;

    for (i = 0; i < _spu_AllocBlockNum; i++) {
        if (_spu_memList[i].addr & 0x40000000) {
            break;
        }
        if (_spu_memList[i].addr == addr) {
            _spu_memList[i].addr |= 0x80000000;
            break;
        }
    }
    _spu_gcSPU();
}
