/* LIBSPU S_GKS: SpuGetKeyStatus, whether a voice is keyed on and whether its
 * envelope is still sounding. */
#include "common.h"
#include "libspu_internal.h"

s32 SpuGetKeyStatus(u32 voice_bit) {
    volatile SPU_VOICE_REG *voices;
    s32 voice;
    s32 i;
    s32 voice_mask;
    u16 volumex;

    voice = -1;
    for (i = 0; i < 0x18; i++) {
        if (voice_bit & (1 << i)) {
            voice = i;
            break;
        }
    }
    if (voice == -1) {
        return -1;
    }
    voices = _spu_RXX->rxx.voice;
    volumex = voices[voice].volumex;
    voice_mask = 1 << voice;
    if (_spu_keystat & voice_mask) {
        if (volumex > 0) {
            return 1;
        } else {
            return 3;
        }
    } else if (volumex > 0) {
        return 2;
    } else {
        return 0;
    }
}
