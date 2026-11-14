/* LIBSND UT_VVOL: SsUtGetDetVVol, SsUtSetDetVVol, SsUtGetVVol and
 * SsUtSetVVol, a voice's volume, in the SPU's units and in the library's. */
#include "common.h"
#include "libsnd_i.h"

s32 SsUtGetDetVVol(s16 voice, s16 *voll, s16 *volr) {
    if ((u16)voice < 0x18) {
        SpuGetVoiceVolume(voice, voll, volr);
        return 0;
    }
    return -1;
}

s32 SsUtSetDetVVol(s16 idx, s16 x, s16 y) {
    if ((u16)idx < 0x18) {
        _svm_sreg_buf[idx * 8 + 1] = y;
        _svm_sreg_buf[idx * 8] = x;
        _svm_sreg_dirty[idx] |= 3;
        return 0;
    }
    return -1;
}

s32 SsUtGetVVol(s16 voice, s16 *voll, s16 *volr) {
    s16 raw1, raw2;

    if ((u16)voice < 0x18) {
        SpuGetVoiceVolume(voice, &raw1, &raw2);
        *voll = raw1 / 129;
        *volr = raw2 / 129;
        return 0;
    }
    return -1;
}

s32 SsUtSetVVol(s16 idx, s16 x, s16 y) {
    if ((u16)idx < 0x18) {
        s16 vx = x * 129;
        s16 vy = y * 129;

        _svm_sreg_buf[idx * 8 + 1] = vy;
        _svm_sreg_buf[idx * 8] = vx;
        _svm_sreg_dirty[idx] |= 3;
        return 0;
    }
    return -1;
}
