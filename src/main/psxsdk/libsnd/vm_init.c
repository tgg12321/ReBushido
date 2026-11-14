/* LIBSND VM_INIT: _SsVmInit, which sets the voice manager up for a number of
 * voices. */
#include "common.h"
#include "libsnd_i.h"

u16 _svm_okon1;
u16 _svm_okon2;
u16 _svm_orev1;
u16 _svm_orev2;
struct SpuVoice _svm_voice[24];
SpuReverbAttr _svm_rattr;
u8 _svm_sreg_dirty[24];
s16 _svm_damper;
s16 _svm_stereo_mono;
s16 kMaxPrograms;
u8 _SsVmMaxVoice;
struct struct_svm _svm_cur;
u8 _svm_vab_used[16];
s16 _svm_sreg_buf[192];
u8 _svm_auto_kof_mode;
u16 _svm_vab_count;
u16 _svm_okof1;

/* SpuInitMalloc's table: a record for each block and one to end them. */
static s32 _ss_spu_vm_rec[2 * (0x20 + 1)];

void _SsVmInit(s32 voices) {
    SpuVoiceAttr attr;
    u16 i;

    _spu_setInTransfer(0);
    _svm_damper = 0;
    SpuInitMalloc(0x20, _ss_spu_vm_rec);

    i = 0;
    do {
        _svm_sreg_buf[i] = 0;
        i++;
    } while (i < 0xC0);
    i = 0;
    do {
        _svm_sreg_dirty[i] = 0;
        i++;
    } while (i < 0x18);
    _svm_vab_count = 0;
    i = 0;
    do {
        _svm_vab_used[i] = 0;
        i++;
    } while (i < 0x10);

    {
        u16 masked = (u8)voices;
        if (masked >= 0x18) {
            _SsVmMaxVoice = 0x18;
        } else {
            _SsVmMaxVoice = masked;
        }
    }

    attr.mask = 0x60093;
    i = 0;
    attr.pitch = 0x1000;
    attr.addr = 0x1000;
    attr.adsr1 = 0x80FF;
    attr.volume.left = 0;
    attr.volume.right = 0;
    attr.adsr2 = 0x4000;

    if (_SsVmMaxVoice != 0) {
        do {
            _svm_voice[i].unk2 = 0x18;
            _svm_voice[i].seq_sep_no = -1;
            _svm_voice[i].vag_idx = 0xFF;
            _svm_voice[i].playing = 0;
            _svm_voice[i].pitch = 0;
            _svm_voice[i].envx = 0;
            _svm_voice[i].fake_program = 0;
            _svm_voice[i].prog = 0;
            _svm_voice[i].tone = 0xFF;
            _svm_voice[i].volume = 0;
            _svm_voice[i].channel = 0;
            _svm_voice[i].pan = 0x40;
            _svm_voice[i].auto_vol = 0;
            _svm_voice[i].unk1e = 0;
            _svm_voice[i].unk20 = 0;
            _svm_voice[i].unk22 = 0;
            _svm_voice[i].auto_pan = 0;
            _svm_voice[i].unk2a = 0;
            _svm_voice[i].unk2c = 0;
            _svm_voice[i].unk2e = 0;
            _svm_voice[i].start_pan = 0;
            _svm_voice[i].start_vol = 0;
            attr.voice = 1 << i;
            SpuSetVoiceAttr(&attr);
            _svm_cur.voice = i;
            _SsVmKeyOffNow(1);
            i = i + 1;
        } while (i < _SsVmMaxVoice);
    }

    _svm_rattr.depth.left = 0x3FFF;
    _svm_rattr.depth.right = 0x3FFF;
    _svm_okon1 = 0;
    _svm_okon2 = 0;
    _svm_okof1 = 0;
    _svm_orev1 = 0;
    _svm_orev2 = 0;
    _svm_rattr.mask = 0;
    _svm_rattr.mode = 0;
    _svm_auto_kof_mode = 0;
    _svm_stereo_mono = 0;
    kMaxPrograms = 0x80;
    _SsVmFlush();
}
