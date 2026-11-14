/* LIBSPU S_M_UTIL: _SpuIsInAllocateArea and _SpuIsInAllocateArea_, whether an
 * address is in an allocated block. */
#include "common.h"
#include "libspu_internal.h"

s32 _SpuIsInAllocateArea(u32 addr) {
    s32 i;

    if (_spu_memList == 0) {
        return 0;
    }
    for (i = 0;; i++) {
        if (_spu_memList[i].addr & 0x80000000) {
            continue;
        }
        if (_spu_memList[i].addr & 0x40000000) {
            break;
        }
        if (addr <= (_spu_memList[i].addr & 0x0FFFFFFF)) {
            return 1;
        }
        if (addr < (_spu_memList[i].addr & 0x0FFFFFFF) + _spu_memList[i].size) {
            return 1;
        }
    }
    return 0;
}

s32 _SpuIsInAllocateArea_(u32 addr) {
    s32 i;

    addr <<= _spu_mem_mode_plus;
    if (_spu_memList == 0) {
        return 0;
    }
    for (i = 0;; i++) {
        if (_spu_memList[i].addr & 0x80000000) {
            continue;
        }
        if (_spu_memList[i].addr & 0x40000000) {
            break;
        }
        if (addr <= (_spu_memList[i].addr & 0x0FFFFFFF)) {
            return 1;
        }
        if (addr < (_spu_memList[i].addr & 0x0FFFFFFF) + _spu_memList[i].size) {
            return 1;
        }
    }
    return 0;
}
