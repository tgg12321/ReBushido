/* LIBSND VM_ALOC2: _SsVmDoAllocate, which gives the voice that was chosen its
 * waveform and its envelope. */
#include "common.h"
#include "libsnd_i.h"

static inline void vmSetStartAddr(u16 addr) {
    _svm_sreg_buf[_svm_cur.voiceOffset + 3] = addr;
    _svm_sreg_dirty[_svm_cur.voice] |= 8;
}

void _SsVmDoAllocate(void) {
    int i;

    _svm_cur.voiceOffset = _svm_cur.voice * 8;
    _svm_cur.field_0x1e = _svm_cur.field_7_fake_program * 16 + _svm_cur.tone;
    _svm_voice[_svm_cur.voice].envx = 0x7FFF;
    for (i = 0; i < 16; i++) {
        _svm_envx_hist[i] &= ~(1 << _svm_cur.voice);
    }
    if ((_svm_cur.tone_vag_idx & 1) > 0) {
        vmSetStartAddr(_svm_pg[(_svm_cur.tone_vag_idx - 1) / 2].reserved2);
    } else {
        vmSetStartAddr(_svm_pg[(_svm_cur.tone_vag_idx - 1) / 2].reserved3);
    }
    _svm_sreg_buf[_svm_cur.voiceOffset + 4] =
        _svm_tn[_svm_cur.field_7_fake_program * 16 + _svm_cur.tone].adsr1;
    _svm_sreg_buf[_svm_cur.voiceOffset + 5] =
        _svm_tn[_svm_cur.field_7_fake_program * 16 + _svm_cur.tone].adsr2 +
        _svm_damper;
    _svm_sreg_dirty[_svm_cur.voice] |= 0x30;
}
