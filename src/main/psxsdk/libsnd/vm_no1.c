/* LIBSND VM_NO1: vmNoiseOn, which plays a noise tone. */
#include "common.h"
#include "libsnd_i.h"

/* The volumes are worked out as _SsVmKeyOnNow's are; the pitch of the note
 * sets the noise's clock, and the voice becomes the one noise voice. */
void vmNoiseOn(u8 vc) {
    struct SeqStruct *score;
    s16 voice;
    s16 bitsLower;
    s16 bitsUpper;
    u32 voll_t, volr_t;
    u32 voll, volr;
    /* temp is the tone's pan, then the program's, then the voice's. */
    u32 temp;
    u32 idx;

    score = &_ss_score[_svm_cur.seq_sep_no & 0xFF]
                      [(_svm_cur.seq_sep_no & 0xFF00) >> 8];

    voll_t = score->voll * 0x81;
    volr_t = score->volr * 0x81;

    voll_t = (voll_t * _svm_cur.mvol) / 0x7F;
    volr_t = (volr_t * _svm_cur.mvol) / 0x7F;

    voll_t = (voll_t * _svm_cur.tone_vol) / 0x7F;
    volr_t = (volr_t * _svm_cur.tone_vol) / 0x7F;

    temp = _svm_cur.tone_pan;
    if (temp < 0x40) {
        voll = voll_t;
        volr = (volr_t * temp) / 0x3F;
    } else {
        voll = (voll_t * (0x7F - temp)) / 0x3F;
        volr = volr_t;
    }
    temp = _svm_cur.mpan;
    if (temp < 0x40) {
        volr = (volr * temp) / 0x3F;
    } else {
        voll = (voll * (0x7F - temp)) / 0x3F;
    }
    temp = _svm_cur.pan;
    if (temp < 0x40) {
        volr = (temp * volr) / 0x3F;
    } else {
        voll = (voll * (0x7F - temp)) / 0x3F;
    }

    if (_svm_stereo_mono == 1) {
        if (voll < volr) {
            voll = volr;
        } else {
            volr = voll;
        }
    }

    idx = vc;
    SpuSetNoiseClock((_svm_cur.note - _svm_cur.tone_center) & 0x3F);

    _svm_sreg_buf[idx * 8 + 0] = voll;
    _svm_sreg_buf[idx * 8 + 1] = volr;
    _svm_sreg_dirty[idx] |= 3;
    if (idx < 0x10) {
        bitsLower = 1 << idx;
        bitsUpper = 0;
    } else {
        bitsLower = 0;
        bitsUpper = 1 << (idx - 0x10);
    }
    _svm_voice[vc].pitch = 0xA;
    for (voice = 0; voice < _SsVmMaxVoice; voice++) {
        _svm_voice[voice].playing &= 1;
    }
    _svm_voice[vc].playing = 2;

    _svm_okon1 |= bitsLower;
    _svm_okon2 |= bitsUpper;

    _svm_okof1 &= ~_svm_okon1;
    _svm_okof2 &= ~_svm_okon2;

    if (_svm_cur.tone_mode & 4) {
        _svm_orev1 |= bitsLower;
        _svm_orev2 |= bitsUpper;
    } else {
        _svm_orev1 &= ~bitsLower;
        _svm_orev2 &= ~bitsUpper;
    }

    SpuSetNoiseVoice(1, ((bitsUpper & 0xFF) << 16) | bitsLower);
}
