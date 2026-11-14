/* LIBSND VS_VTB: SsVabTransBody, which sends a bank's waveforms to sound
 * memory. */
#include "common.h"
#include "libsnd_i.h"

s16 SsVabTransBody(u8 *addr, s16 vabid) {
    if ((u16)vabid >= 0x11) {
        _spu_setInTransfer(0);
        return -1;
    }
    if (_svm_vab_used[vabid] != 2) {
        _spu_setInTransfer(0);
        return -1;
    }
    {
        s32 s0 = _svm_vab_start[vabid];
        SpuSetTransferMode(0);
        SpuSetTransferStartAddr(s0);
        SpuWrite((s32)addr, _svm_vab_total[vabid]);
        _svm_vab_used[vabid] = 1;
    }
    return vabid;
}
