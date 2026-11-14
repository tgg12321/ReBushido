/* LIBSPU S_ITC: SpuIsTransferCompleted, which waits for a transfer to end, or
 * only looks. */
#include "common.h"
#include "libspu_internal.h"
#include <psxsdk/libapi.h>

s32 SpuIsTransferCompleted(s32 flag) {
    s32 done;

    if ((_spu_trans_mode == 1) || (_spu_inTransfer == 1)) {
        return 1;
    }
    done = TestEvent(_spu_EVdma);
    if (flag == 1) {
        while (done == 0) {
            done = TestEvent(_spu_EVdma);
        }
        _spu_inTransfer = 1;
        return 1;
    }
    if (done == 1) {
        _spu_inTransfer = done;
    }
    return done;
}
