/* LIBSND UT_GVBA: SsUtGetVBaddrInSB, where in sound memory a bank's waveforms
 * are. */
#include "common.h"
#include "libsnd_i.h"

u32 SsUtGetVBaddrInSB(s16 vabid) {
    if ((u16)vabid >= 0x11) {
        return -1;
    }
    if (_svm_vab_used[vabid] != 1) {
        return -1;
    }
    return _svm_vab_start[vabid];
}
