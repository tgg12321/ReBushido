/* LIBSPU S_M_INIT: SpuInitMalloc, which gives the allocator its list and
 * makes all of sound memory one free block. */
#include "common.h"
#include "libspu_internal.h"

s32 SpuInitMalloc(s32 num, s32 *top) {
    s32 size;

    if (num > 0) {
        size = 0x10000 << _spu_mem_mode_plus;
        top[0] = 0x40001010;
        _spu_memList = (SpuMemRec *)top;
        _spu_AllocLastNum = 0;
        _spu_AllocBlockNum = num;
        top[1] = size - 0x1010;
        return num;
    }
    return 0;
}
