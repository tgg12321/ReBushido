/* LIBSND VS_VAB: SsVabClose. */
#include "common.h"
#include "libsnd_i.h"

void SsVabClose(s16 vabid) {
    if ((u16)vabid < 0x10) {
        s16 idx = vabid;
        if (_svm_vab_used[idx] == 1) {
            SpuFree(_svm_vab_start[idx]);
            _svm_vab_used[idx] = 0;
            _svm_vab_count--;
        }
    }
}
