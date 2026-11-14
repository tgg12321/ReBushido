/* LIBSND UT_AKO: SsUtAllKeyOff, which silences every voice. Its argument is
 * not read. */
#include "common.h"
#include "libsnd_i.h"

/* Clears every voice's record and keys the voice off. */
void SsUtAllKeyOff(s16 mode) {
    SpuVoiceAttr attr;
    s16 voice;

    attr.mask = 0x60093;
    attr.pitch = 0x1000;
    attr.addr = 0x1000;
    attr.adsr1 = 0x80FF;
    attr.volume.left = 0;
    attr.volume.right = 0;
    attr.adsr2 = 0x4000;
    for (voice = 0; voice < _SsVmMaxVoice; voice++) {
        _svm_voice[voice].unk2 = 0x18;
        _svm_voice[voice].envx = 0;
        _svm_voice[voice].seq_sep_no = 0xFF;
        _svm_voice[voice].fake_program = 0;
        _svm_voice[voice].prog = 0;
        _svm_voice[voice].tone = 0xFF;
        attr.voice = 1 << voice;
        SpuSetVoiceAttr(&attr);
        _svm_cur.voice = voice;
        _SsVmKeyOffNow(1);
    }
}
