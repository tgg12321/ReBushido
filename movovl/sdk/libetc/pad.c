/* LIBETC PAD: the old controller interface: PadInit, PadRead and PadStop. */
#include "common.h"
#include <psxsdk/libapi.h>
#include <psxsdk/libetc.h>
#include "sdk.h"

void PAD_init(s32 type, s32 *buf);
void PAD_dr(void);

s32 PadIdentifier;

/* Both controllers' buttons, a bit clear where one is held. */
static s32 pad_buf;

void PadInit(s32 mode) {
    PadIdentifier = mode;
    pad_buf = -1;
    ResetCallback();
    PAD_init(0x20000001, &pad_buf);
    ChangeClearPAD(0);
}

u32 PadRead(s32 id) {
    PAD_dr();
    return ~pad_buf;
}

void PadStop(void) { StopPAD(); }
