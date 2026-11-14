/* LIBETC INTR_VB: the vertical blank interrupt, which counts the frames and
 * calls up to eight callbacks. */
#include "common.h"
#include "libetc_internal.h"

s32 vsync_callbacks[8] = {0};        /* the callbacks */
volatile s32 Vcount = 0;             /* vertical blanks so far */
s32 *rcnt1_mode = (s32 *)0x1F801114; /* root counter 1's mode */

void trapIntrVSync(void);
void setIntrVSync(s32, s32);
void memclr_vb(s32 *, s32);

s32 startIntrVSync(void) {
    *rcnt1_mode = 0x107; /* count horizontal blanks */
    Vcount = 0;
    memclr_vb(&vsync_callbacks[0], 8);
    InterruptCallback(0, trapIntrVSync);
    return (s32)setIntrVSync;
}

void trapIntrVSync(void) {
    s32 i;
    s32 *p;

    ++Vcount;

    i = 0;
    p = &vsync_callbacks[0];
    for (; i < 8; i++) {
        s32 fp = *p;
        if (fp != 0) {
            ((void (*)(void))fp)();
        }
        p++;
    }
}

void setIntrVSync(s32 index, s32 callback) {
    if (callback != vsync_callbacks[index]) {
        vsync_callbacks[index] = callback;
    }
}

void memclr_vb(s32 *ptr, s32 size) {
    s32 i;
    for (i = size - 1; i != -1; i--) {
        *ptr++ = 0;
    }
}
