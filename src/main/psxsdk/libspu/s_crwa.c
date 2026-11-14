/* LIBSPU S_CRWA: SpuClearReverbWorkArea, which fills a reverb mode's work
 * area in sound memory with zeroes. */
#include "common.h"
#include "libspu_internal.h"
#include <psxsdk/libapi.h>

extern s32 _spu_zerobuf[];

s32 SpuClearReverbWorkArea(u32 rev_mode) {
    void (* volatile callback)();
    s32 oldTransmode;
    s32 addr;
    s32 more;
    s32 transmodeCleared;
    u32 chunk;
    u32 size;

    callback = 0;
    transmodeCleared = 0;
    if (rev_mode >= 10 || _SpuIsInAllocateArea_(_spu_rev_startaddr[rev_mode])) {
        return -1;
    }
    if (rev_mode == 0) {
        size = 0x10 << _spu_mem_mode_plus;
        addr = 0xFFF0 << _spu_mem_mode_plus;
    } else {
        size = (0x10000 - _spu_rev_startaddr[rev_mode]) << _spu_mem_mode_plus;
        addr = _spu_rev_startaddr[rev_mode] << _spu_mem_mode_plus;
    }
    oldTransmode = _spu_transMode;
    if (_spu_transMode == 1) {
        _spu_transMode = 0;
        transmodeCleared = 1;
    }
    more = 1;
    if (_spu_transferCallback != 0) {
        callback = _spu_transferCallback;
        _spu_transferCallback = 0;
    }
    while (more != 0) {
        chunk = size;
        if (size > 0x400) {
            chunk = 0x400;
        } else {
            more = 0;
        }

        _spu_t(2, addr);
        _spu_t(1);
        _spu_t(3, &_spu_zerobuf, chunk);
        WaitEvent(_spu_EVdma);
        size -= 0x400;
        addr += 0x400;
    }
    if (transmodeCleared != 0) {
        _spu_transMode = oldTransmode;
    }
    if (callback != 0) {
        _spu_transferCallback = callback;
    }
    return 0;
}
