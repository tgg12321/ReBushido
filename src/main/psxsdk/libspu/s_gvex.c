/* LIBSPU S_GVEX: SpuGetVoiceEnvelope, a voice's envelope level. */
#include "common.h"
#include "libspu_internal.h"

void SpuGetVoiceEnvelope(s32 vNum, u16 *envx) {
    *envx = _spu_RXX->raw[vNum * 8 + 6];
}
