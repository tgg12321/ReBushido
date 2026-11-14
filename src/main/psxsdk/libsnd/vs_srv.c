/* LIBSND VS_SRV: SsSetReservedVoice, how many voices the voice manager may
 * use. */
#include "common.h"
#include "libsnd_i.h"

s32 SsSetReservedVoice(s32 voices) {
    u8 v = (u8)voices;
    if (v >= 0x19 || v == 0) {
        return 0xFF;
    }
    _SsVmMaxVoice = voices;
    return v;
}
