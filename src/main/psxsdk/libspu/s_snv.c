/* LIBSPU S_SNV: SpuSetNoiseVoice. */
#include "common.h"
#include "libspu_internal.h"

void SpuSetNoiseVoice(s32 on_off, s32 voice_bit) {
    _SpuSetAnyVoice(on_off, voice_bit, 0xCA, 0xCB);
}
