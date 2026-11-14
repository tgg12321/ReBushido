/* LIBETC INTR_DMA: the DMA interrupt, which calls the callback of each
 * channel whose transfer has ended. */
#include "common.h"
#include "libetc_internal.h"
#include <psxsdk/libc.h>

/* The DMA interrupt register: a flag and an enable bit for each channel. */
volatile u32 *d_icr = (volatile u32 *)0x1F8010F4;
s32 dma_callbacks[8] = {0};      /* the seven channels' callbacks */
u32 *d_madr = (u32 *)0x1F801080; /* channel 0's address register */

void trapIntrDMA(void);
s32 setIntrDMA(s32, s32);
void memclr_dma(s32 *, s32);

s32 startIntrDMA(void) {
    memclr_dma((s32 *)&dma_callbacks, 8);
    *d_icr = 0;
    InterruptCallback(3, trapIntrDMA);
    return (s32)setIntrDMA;
}

void trapIntrDMA(void) {
    u32 mask;
    s32 i;

    while ((mask = (*d_icr >> 24) & 0x7F) != 0) {
        for (i = 0; mask != 0 && i < 7; i++, mask >>= 1) {
            if (mask & 1) {
                *d_icr &= 0xFFFFFF | (1 << (i + 24));
                if (dma_callbacks[i] != 0) {
                    ((void (*)(void))dma_callbacks[i])();
                }
            }
        }
    }

    if ((*d_icr & 0xFF000000) == 0x80000000 || *d_icr & 0x8000) {
        printf("DMA bus error: code=%08x\n", *d_icr);
        for (i = 0; i < 7; i++) {
            printf("MADR[%d]=%08x\n", i, d_madr[4 * i]);
        }
    }
}

/* Sets a channel's callback and enables its interrupt, or with 0 disables
 * it. Returns the callback it replaces. */
s32 setIntrDMA(s32 dma, s32 callback) {
    s32 prev = dma_callbacks[dma];
    if (callback != prev) {
        if (callback != 0) {
            dma_callbacks[dma] = callback;
            *d_icr = (*d_icr & 0xFFFFFF) | 0x800000 | (1 << (dma + 16));
        } else {
            dma_callbacks[dma] = 0;
            *d_icr = ((*d_icr & 0xFFFFFF) | 0x800000) & ~(1 << (dma + 16));
        }
    }
    return prev;
}

void memclr_dma(s32 *ptr, s32 size) {
    s32 i;
    for (i = size - 1; i != -1; i--) {
        *ptr++ = 0;
    }
}
