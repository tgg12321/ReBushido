/* LIBSPU S_SR: SpuSetReverb, which turns the reverb on or off. */
#include "common.h"
#include "libspu_internal.h"

s32 SpuSetReverb(s32 on_off) {
    u16 cnt;
    switch (on_off) {
    case 0:
        cnt = _spu_RXX->rxx.spucnt;
        _spu_rev_flag = 0;
        cnt &= ~0x80;
        _spu_RXX->rxx.spucnt = cnt;
        break;

    case 1:
        if ((_spu_rev_reserve_wa != on_off) &&
            _SpuIsInAllocateArea_(_spu_rev_offsetaddr)) {
            cnt = _spu_RXX->rxx.spucnt;
            _spu_rev_flag = 0;
            cnt &= ~0x80;
            _spu_RXX->rxx.spucnt = cnt;
        } else {
            cnt = _spu_RXX->rxx.spucnt;
            _spu_rev_flag = on_off;
            cnt |= 0x80;
            _spu_RXX->rxx.spucnt = cnt;
        }
        break;
    }

    return _spu_rev_flag;
}
