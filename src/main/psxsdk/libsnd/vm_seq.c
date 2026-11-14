/* LIBSND VM_SEQ: a sequence's volume, and silencing a sequence's voices. */
#include "common.h"
#include "libsnd_i.h"

/* Sets a sequence's volume (at most 127 a side), and works out again the
 * volumes of every voice that is playing for it, as _SsVmKeyOnNow does. The
 * fourth argument is not read. */
s16 _SsVmSetSeqVol(s16 seq_sep_no, u16 voll, u16 volr, s16 arg3) {
    struct SeqStruct *score;
    s16 voice;
    u32 voll_t;
    u32 volr_t;
    u16 left;
    u16 right;
    u8 tone_pan;
    u8 prog_pan;
    u8 voice_pan;

    score = &_ss_score[seq_sep_no & 0xFF][(seq_sep_no & 0xFF00) >> 8];
    score->voll = voll;
    score->volr = volr;
    if (score->voll >= 0x7F) {
        score->voll = 0x7F;
    }
    if (score->volr >= 0x7F) {
        score->volr = 0x7F;
    }

    for (voice = 0; voice < _SsVmMaxVoice; voice++) {
        if (_svm_voice[voice].seq_sep_no == seq_sep_no) {
            _SsVmVSetUp(
                _svm_voice[voice].vabId, _svm_voice[voice].fake_program);
            voll_t = _svm_vh->mvol * 0x3FFF *
                     (_svm_voice[voice].volume *
                      score->vol[_svm_voice[voice].channel] / 0x7F) /
                     0x3F01;
            volr_t = voll_t * _svm_pg[_svm_voice[voice].prog].mvol *
                     _svm_tn[_svm_voice[voice].fake_program * 16 +
                             _svm_voice[voice].tone]
                         .vol /
                     0x3F01;
            voll_t = voll_t * _svm_pg[_svm_voice[voice].prog].mvol *
                     _svm_tn[_svm_voice[voice].fake_program * 16 +
                             _svm_voice[voice].tone]
                         .vol /
                     0x3F01;
            voll_t = voll_t * score->voll / 0x7F;
            volr_t = volr_t * score->volr / 0x7F;

            tone_pan = _svm_tn[_svm_voice[voice].fake_program * 16 +
                               _svm_voice[voice].tone]
                           .pan;
            if (tone_pan < 0x40) {
                left = voll_t;
                right = volr_t * tone_pan / 0x3F;
            } else {
                left = voll_t * (0x7F - tone_pan) / 0x3F;
                right = volr_t;
            }
            prog_pan = _svm_pg[_svm_voice[voice].prog].mpan;
            if (prog_pan < 0x40) {
                right = right * prog_pan / 0x3F;
            } else {
                left = left * (0x7F - prog_pan) / 0x3F;
            }
            voice_pan = _svm_voice[voice].pan;
            if (voice_pan < 0x40) {
                right = right * voice_pan / 0x3F;
            } else {
                left = left * (0x7F - voice_pan) / 0x3F;
            }
            if (_svm_stereo_mono == 1) {
                if (right > left) {
                    left = right;
                } else {
                    right = left;
                }
            }
            left = left * left / 0x3FFF;
            right = right * right / 0x3FFF;
            _svm_sreg_buf[voice * 8] = left;
            _svm_sreg_buf[voice * 8 + 1] = right;
            _svm_sreg_dirty[voice] |= 3;
        }
    }
    return seq_sep_no;
}

s16 _SsVmGetSeqVol(s32 seq_sep_no, s16 *voll, s16 *volr) {
    struct SeqStruct *score =
        &_ss_score[seq_sep_no & 0xFF][(seq_sep_no & 0xFF00) >> 8];
    _svm_cur.seq_sep_no = seq_sep_no;
    *voll = score->voll;
    *volr = score->volr;
    return _svm_cur.seq_sep_no;
}

s16 _SsVmGetSeqLVol(s32 seq_sep_no) {
    struct SeqStruct *score =
        &_ss_score[seq_sep_no & 0xFF][(seq_sep_no & 0xFF00) >> 8];
    _svm_cur.seq_sep_no = seq_sep_no;
    return score->voll;
}

s16 _SsVmGetSeqRVol(s32 seq_sep_no) {
    struct SeqStruct *score =
        &_ss_score[seq_sep_no & 0xFF][(seq_sep_no & 0xFF00) >> 8];
    _svm_cur.seq_sep_no = seq_sep_no;
    return score->volr;
}

void _SsVmSeqKeyOff(s16 seq_sep_no) {
    s32 s0 = 0;
    s16 s1;
    if (_SsVmMaxVoice == 0) {
        return;
    }
    s1 = (s16)seq_sep_no;
    do {
        if (_svm_voice[(u8)s0].seq_sep_no == s1) {
            _svm_cur.voice = (u8)s0;
            _SsVmKeyOffNow(0);
        }
        s0++;
    } while ((u8)s0 < _SsVmMaxVoice);
}
