/* LIBSND VS_VFB: SsVabFakeBody, which opens a bank whose waveforms are
 * already in sound memory. */
#include "common.h"
#include "libsnd_i.h"

s16 SsVabFakeBody(s16 vabid) {
    if ((u16)vabid < 0x11) {
        if (_svm_vab_used[vabid] == 2) {
            _spu_setInTransfer(0);
            _svm_vab_used[vabid] = 1;
            return vabid;
        }
    }
    return -1;
}
