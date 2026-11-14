/* LIBSND SSINIT_C: SsInit, which starts the SPU and the sound system. */
#include "common.h"
#include "libsnd_i.h"
#include <psxsdk/libetc.h>

void SsInit(void) {
    ResetCallback();
    SpuInit();
    _SsInit();
}
