/* LIBSND UT_RDEP: SsUtSetReverbDepth. */
#include "common.h"
#include "libsnd_i.h"

void SsUtSetReverbDepth(s16 ldepth, s16 rdepth) {
    _svm_rattr.mask = 6;
    _svm_rattr.depth.left = (s16)ldepth * 32767 / 127;
    _svm_rattr.depth.right = (s16)rdepth * 32767 / 127;
    SpuSetReverbModeParam(&_svm_rattr);
}
