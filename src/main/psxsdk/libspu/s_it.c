/* LIBSPU S_IT: _spu_setInTransfer and _spu_getInTransfer, the flag that says
 * a transfer is going on. */
#include "common.h"
#include "libspu_internal.h"

void _spu_setInTransfer(s32 mode) {
    if (mode == 1) {
        _spu_inTransfer = 0;
    } else {
        _spu_inTransfer = 1;
    }
}

s32 _spu_getInTransfer(void) { return _spu_inTransfer != 1; }
