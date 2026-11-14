/* LIBSND VM_F: _SsVmFlush, the voice manager's tick: it reads each voice's
 * envelope, runs the automatic volume and pan changes, and writes to the SPU
 * whatever was changed since the last tick, the keys on and off last. */
#include "common.h"
#include "libsnd_i.h"

s32 _svm_envx_hist[16];
u16 _svm_okof2;

s32 _svm_envx_ptr; /* where in each history this tick is written */
void (*_autovol)(s32);
void (*_autopan)(s32);

void _SsVmFlush(void) {
    s32 i;
    u32 env_mask;
    SpuVoiceAttr attr;

    _svm_envx_ptr = (_svm_envx_ptr + 1) & 0xF;
    _svm_envx_hist[_svm_envx_ptr] = 0;

    for (i = 0; i < _SsVmMaxVoice; i++) {
        SpuGetVoiceEnvelope(i, &_svm_voice[i].envx);
        if (_svm_voice[i].envx == 0) {
            _svm_envx_hist[_svm_envx_ptr] |= 1 << i;
        }
    }
    if (_svm_auto_kof_mode == 0) {
        env_mask = 0xFFFFFFFF;
        for (i = 0; i < 0xF; i++) {
            env_mask &= _svm_envx_hist[i];
        }
        for (i = 0; i < _SsVmMaxVoice; i++) {
            if (env_mask & (1 << i)) {
                if (_svm_voice[i].playing == 2) {
                    SpuSetNoiseVoice(0, 0xFFFFFF);
                }
                _svm_voice[i].playing = 0;
            }
        }
    }

    _svm_okon1 &= ~_svm_okof1;
    _svm_okon2 &= ~_svm_okof2;
    for (i = 0; i < 24; i++) {
        if (_svm_voice[i].auto_vol != 0) {
            _autovol(i);
        }
        if (_svm_voice[i].auto_pan != 0) {
            _autopan(i);
        }
    }

    for (i = 0; i < 24; i++) {
        attr.mask = 0;
        attr.voice = 1 << i;
        if (_svm_sreg_dirty[i] & 1) {
            attr.mask = 3;
            attr.volume.left = _svm_sreg_buf[i * 8 + 0];
            attr.volume.right = _svm_sreg_buf[i * 8 + 1];
        }
        if (_svm_sreg_dirty[i] & 4) {
            attr.mask |= 0x10;
            attr.pitch = _svm_sreg_buf[i * 8 + 2];
        }
        if (_svm_sreg_dirty[i] & 8) {
            attr.mask |= 0x80;
            attr.addr = (u16)_svm_sreg_buf[i * 8 + 3] << 3;
        }
        if (_svm_sreg_dirty[i] & 0x10) {
            attr.mask |= 0x60000;
            attr.adsr1 = _svm_sreg_buf[i * 8 + 4];
            attr.adsr2 = _svm_sreg_buf[i * 8 + 5];
        }
        if (attr.mask != 0) {
            SpuSetVoiceAttr(&attr);
        }
        _svm_sreg_dirty[i] = 0;
    }

    SpuSetKey(0, ((_svm_okof2 & 0xFF) << 16) | _svm_okof1);
    SpuSetKey(1, ((_svm_okon2 & 0xFF) << 16) | _svm_okon1);
    SpuSetReverbVoice(8, ((_svm_orev2 & 0xFF) << 16) | _svm_orev1);

    _svm_okof1 = 0;
    _svm_okof2 = 0;
    _svm_okon1 = 0;
    _svm_okon2 = 0;
}
