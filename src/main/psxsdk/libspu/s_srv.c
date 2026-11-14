/* LIBSPU S_SRV: SpuSetReverbVoice. */
#include "common.h"
#include "libspu_internal.h"

void SpuSetReverbVoice(s32 on_off, s32 voice_bit) {
    _SpuSetAnyVoice(on_off, voice_bit, 0xCC, 0xCD);
}
