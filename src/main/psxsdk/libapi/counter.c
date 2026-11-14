/* LIBAPI COUNTER: the root counters (SetRCnt, GetRCnt, StartRCnt, StopRCnt,
 * ResetRCnt). */
#include "common.h"
#include <psxsdk/libapi.h>

/* One root counter's registers. */
typedef struct {
    u16 count; /* current value */
    u16 pad2;
    u16 mode;
    u16 pad6;
    u16 target; /* the value it counts up to */
    u16 padA[3];
} RCnt;

extern volatile s32 *_rcnt_intr;
extern volatile RCnt *_counters;
extern s32 _rcnt_intr_masks[];

s32 SetRCnt(u32 spec, s32 target, s32 mode) {
    s32 t0 = spec & 0xFFFF;
    s32 a3 = 0x48;
    if (t0 >= 3) {
        return 0;
    }
    _counters[t0].mode = 0;
    _counters[t0].target = target;
    if ((u32)t0 < 2U) {
        if (mode & RCntMdGATE) {
            a3 = 0x49;
        }
        if (!(mode & RCntMdSC)) {
            a3 |= 0x100;
        }
    } else if (t0 == 2) {
        if (!(mode & RCntMdSC)) {
            a3 = 0x248;
        }
    }
    if ((mode & RCntMdINTR) != 0) {
        a3 |= 0x10;
    }
    _counters[t0].mode = a3;
    return 1;
}

s32 GetRCnt(u32 spec) {
    s32 v = spec & 0xFFFF;
    if (v >= 3) {
        return 0;
    }
    return _counters[v].count;
}

s32 StartRCnt(u32 spec) {
    s32 v;
    v = spec & 0xFFFF;
    _rcnt_intr[1] |= _rcnt_intr_masks[v];
    return v < 3;
}

s32 StopRCnt(u32 spec) {
    s32 v;
    v = spec & 0xFFFF;
    _rcnt_intr[1] &= ~_rcnt_intr_masks[v];
    return 1;
}

s32 ResetRCnt(u32 spec) {
    s32 v = spec & 0xFFFF;
    if (v >= 3) {
        return 0;
    }
    _counters[v].count = 0;
    return 1;
}

/* The interrupt status and mask registers, the three counters, and each
 * counter's bit in the interrupt mask (the fourth is the vertical blank). */
volatile s32 *_rcnt_intr = (s32 *)0x1F801070; /* I_STAT; [1] is I_MASK */
volatile RCnt *_counters = (RCnt *)0x1F801100;
s32 _rcnt_intr_masks[4] = {0x10, 0x20, 0x40, 0x01};
