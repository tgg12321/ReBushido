/* LIBETC VSYNC: VSync, which waits for the vertical blank. */
#include "common.h"
#include <psxsdk/libapi.h>
#include <psxsdk/libc.h>
#include <psxsdk/libetc.h>

extern volatile s32 Vcount;

extern volatile s32 *vs_gpu_stat;
extern volatile s32 *vs_rcnt1_count;
extern s32 Hcount;
extern s32 prevVcount;
void v_wait(s32 count, s32 max);

/* mode 0: waits for the next vertical blank. mode n > 1: waits until n have
 * passed since the last call. mode 1: does not wait. Each returns the
 * horizontal blanks since the last call that waited. A negative mode returns
 * the vertical blanks since the program started. */
s32 VSync(s32 mode) {
    s32 s0_val;
    s32 s1_val;

    s0_val = *vs_gpu_stat;
    s1_val = (*vs_rcnt1_count - Hcount) & 0xFFFF;

    if (mode < 0) {
        return Vcount;
    }
    if (mode == 1) {
        return s1_val;
    }

    {
        s32 frame;
        s32 count;

        if (mode > 0) {
            s32 base = prevVcount - 1;
            frame = base + mode;
        } else {
            frame = prevVcount;
        }
        count = 0;
        if (mode > 0) {
            count = mode - 1;
        }
        v_wait(frame, count);
    }

    s0_val = *vs_gpu_stat;
    v_wait(Vcount + 1, 1);

    if (s0_val & 0x400000) {
        volatile s32 *ptr = vs_gpu_stat;
        if ((s32)(s0_val ^ *ptr) >= 0) {
            do {
            } while (!((s0_val ^ *ptr) & 0x80000000));
        }
    }

    prevVcount = Vcount;
    Hcount = *vs_rcnt1_count;

    return s1_val;
}

/* Waits until Vcount reaches `count`, for at most about `max` frames. */
void v_wait(s32 count, s32 max) {
    volatile s32 timeout;

    timeout = max << 0xF;
    while (Vcount < count) {
        if (--timeout == -1) {
            puts("VSync: timeout\n");
            ChangeClearPAD(0);
            ChangeClearRCnt(3, 0);
            return;
        }
    }
}

/* Sony's stamp (the letters Ps and a build number) at the head of the
 * module's data; nothing reads it. */
u32 _vsync_stamp[2] = {0x15067350, 0x0040889C};

/* The GPU's status, and root counter 1, which counts horizontal blanks. */
volatile s32 *vs_gpu_stat = (volatile s32 *)0x1F801814;
volatile s32 *vs_rcnt1_count = (volatile s32 *)0x1F801110;
s32 Hcount = 0;     /* the counter at the last wait */
s32 prevVcount = 0; /* Vcount at the last wait */
