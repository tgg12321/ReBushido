/* LIBSPU S_DCB: _SpuDataCallback, which sets what the end of a transfer to or
 * from sound memory calls. */
#include "common.h"
#include "libspu_internal.h"

extern void DMACallback(s32, s32);

void _SpuDataCallback(s32 func) { DMACallback(4, func); }
