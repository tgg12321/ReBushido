/* LIBSPU S_M_M: SpuMalloc. */
#include "common.h"
#include "libspu_internal.h"

s32 SpuMalloc(s32 size) {
    s32 found;
    s32 reverbArea;
    s32 i;

    i = 0;
    found = -1;

    if (_spu_rev_reserve_wa == 0) {
        reverbArea = 0;
    } else {
        reverbArea = (0x10000 - _spu_rev_offsetaddr) << _spu_mem_mode_plus;
    }

    size += (size & ~_spu_mem_mode_unitM) ? _spu_mem_mode_unitM : 0;
    size >>= _spu_mem_mode_plus;
    size <<= _spu_mem_mode_plus;

    if (_spu_memList[0].addr & 0x40000000) {
        found = 0;
    } else {
        _spu_gcSPU();

        for (; i < _spu_AllocBlockNum; i++) {
            if (_spu_memList[i].addr & 0x40000000 ||
                (_spu_memList[i].addr & 0x80000000 &&
                 _spu_memList[i].size >= size)) {
                found = i;
                break;
            }
        }
    }

    if (found == -1)
        return -1;

    if (_spu_memList[found].addr & 0x40000000) {
        if (found < _spu_AllocBlockNum &&
            _spu_memList[found].size - reverbArea >= size) {
            s32 next = found + 1;

            /* FAKE: not volatile */
            _spu_memList[next].addr =
                (*(volatile u32 *)&_spu_memList[found].addr & 0x0FFFFFFF) +
                    size |
                0x40000000;
            _spu_memList[next].size = _spu_memList[found].size - size;

            _spu_AllocLastNum = next;
            _spu_memList[found].size = size;
            _spu_memList[found].addr &= 0x0FFFFFFF;

            _spu_gcSPU();

            return _spu_memList[found].addr;
        }
    } else {
        if (size < _spu_memList[found].size &&
            _spu_AllocLastNum < _spu_AllocBlockNum) {
            u32 _addr = _spu_memList[found].addr + size;
            u32 _size = _spu_memList[found].size - size;
            u32 swapAddr = _spu_memList[_spu_AllocLastNum].addr;
            u32 swapSize = _spu_memList[_spu_AllocLastNum].size;

            _spu_memList[_spu_AllocLastNum].addr = _addr | 0x80000000;
            _spu_memList[_spu_AllocLastNum].size = _size;
            _spu_memList[_spu_AllocLastNum + 1].addr = swapAddr;
            _spu_memList[_spu_AllocLastNum + 1].size = swapSize;
            _spu_AllocLastNum++;
        }

        _spu_memList[found].size = size;
        _spu_memList[found].addr &= 0x0FFFFFFF;
        _spu_gcSPU();

        return _spu_memList[found].addr;
    }
    return -1;
}
