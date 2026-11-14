/* LIBSND UT_REV: SsUtSetReverbType and SsUtGetReverbType. */
#include "common.h"
#include "libsnd_i.h"

s16 SsUtSetReverbType(s16 type) {
    s32 neg = 0;
    s16 v1 = type;
    if (type < 0) {
        neg = 1;
        v1 = -type;
    }
    if ((u16)v1 < 0xA) {
        _svm_rattr.mask = 1;
        if (neg) {
            _svm_rattr.mode = v1 | 0x100;
        } else {
            _svm_rattr.mode = v1;
        }
        if (v1 == 0) {
            SpuSetReverb(0);
        }
        SpuSetReverbModeParam(&_svm_rattr);
        return v1;
    }
    return -1;
}

s16 SsUtGetReverbType(void) { return _svm_rattr.mode; }
