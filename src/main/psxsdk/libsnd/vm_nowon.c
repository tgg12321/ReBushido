/* LIBSND VM_NOWON: _SsVmKeyOnNow, which works out the current voice's volumes
 * and pitch and keys it on. */
#include "common.h"
#include "libsnd_i.h"

/* The volume is the bank's, the note's, the program's and the tone's
 * multiplied together, with the sequence channel's for a sequence's voice; it
 * is then divided between left and right by the tone's, the program's and the
 * note's pan in turn. A sequence's voices have the result squared. */
void _SsVmKeyOnNow(s32 vagCount, u16 pitch) {
    struct SeqStruct *score;
    u16 pos;
    s16 bitsLower;
    s16 bitsUpper;
    u32 voll, volr;
    u32 voll_t, volr_t;
    s32 mvol_scaled;

    mvol_scaled = _svm_vh->mvol * 0x3FFF;
    voll_t = (_svm_cur.volume * mvol_scaled) / 16129;
    volr_t = ((voll_t * _svm_cur.mvol) * _svm_cur.tone_vol) / 16129;
    pos = _svm_cur.voice * 8;
    voll_t = volr_t;
    score = &_ss_score[_svm_cur.seq_sep_no & 0xFF]
                      [(_svm_cur.seq_sep_no & 0xFF00) >> 8];
    if (_svm_cur.seq_sep_no != 0x21) {
        voll_t = (voll_t * score->voll) / 127;
        volr_t = (volr_t * score->volr) / 127;
    }
    if (_svm_cur.tone_pan < 64) {
        voll = voll_t;
        volr = (volr_t * _svm_cur.tone_pan) / 63;
    } else {
        voll = (voll_t * (127 - _svm_cur.tone_pan)) / 63;
        volr = volr_t;
    }
    if (_svm_cur.mpan < 64) {
        volr = (volr * _svm_cur.mpan) / 63;
    } else {
        voll = (voll * (127 - _svm_cur.mpan)) / 63;
    }
    if (_svm_cur.pan < 64) {
        volr = (volr * _svm_cur.pan) / 63;
    } else {
        voll = (voll * (127 - _svm_cur.pan)) / 63;
    }
    if (_svm_stereo_mono == 1) {
        if (voll < volr) {
            voll = volr;
        } else {
            volr = voll;
        }
    }
    if (_svm_cur.seq_sep_no != 0x21) {
        voll = (voll * voll) / 0x3FFF;
        volr = (volr * volr) / 0x3FFF;
    }
    _svm_sreg_buf[pos + 2] = pitch;
    _svm_sreg_buf[pos + 0] = voll;
    _svm_sreg_buf[pos + 1] = volr;
    _svm_sreg_dirty[_svm_cur.voice] |= 7;
    _svm_voice[_svm_cur.voice].pitch = pitch;
    if (_svm_cur.voice < 16) {
        bitsLower = 1 << _svm_cur.voice;
        bitsUpper = 0;
    } else {
        bitsLower = 0;
        bitsUpper = 1 << (_svm_cur.voice - 16);
    }
    if (_svm_cur.tone_mode & 4) {
        _svm_orev1 |= bitsLower;
        _svm_orev2 |= bitsUpper;
    } else {
        _svm_orev1 &= ~bitsLower;
        _svm_orev2 &= ~bitsUpper;
    }
    _svm_okon1 |= bitsLower;
    _svm_okon2 |= bitsUpper;
    _svm_okof1 &= ~_svm_okon1;
    _svm_okof2 &= ~_svm_okon2;
}
