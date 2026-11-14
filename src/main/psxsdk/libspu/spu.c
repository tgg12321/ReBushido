/* LIBSPU SPU: what the library does to the SPU itself: resetting it, and
 * transfers to and from sound memory, by hand or by DMA. */
#include "common.h"
#include "psx.h"
#include "libspu_internal.h"
#include <psxsdk/libapi.h>

#include <psxsdk/libc.h>

volatile u16 _spu_RQ[10];
extern void _spu_Fw1ts(void);

/* The SPU's registers. */
SpuUnion *_spu_RXX = (SpuUnion *)0x1F801C00;
/* DMA channel 4, which is the SPU's; the DMA control register; and the SPU's
 * bus timing. */
volatile s32 *dma_spu_madr = (volatile s32 *)0x1F8010C0;
volatile s32 *dma_spu_bcr = (volatile s32 *)0x1F8010C4;
volatile s32 *dma_spu_chcr = (volatile s32 *)0x1F8010C8;
volatile u32 *dma_dpcr = (volatile u32 *)0x1F8010F0;
volatile u32 *spu_delay = (volatile u32 *)0x1F801014;
u16 _spu_tsa = 0;
s32 _spu_transMode = 0;
s32 _spu_addrMode = 0;
s32 _spu_mem_mode = 2;
s32 _spu_mem_mode_plus = 3;
s32 _spu_mem_mode_unit = 8;
s32 _spu_mem_mode_unitM = 7;
s32 _spu_inTransfer = 1;
void (* volatile _spu_transferCallback)() = 0;
s32 _spu_IRQCallback = 0;
/* One block of a waveform, silent and marked as looping on itself: _spu_init
 * puts it at the start of sound memory, and every voice plays it until it is
 * given something else. */
u8 spu_silent_block[16] = {7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7};
s32 spu_reading = 0;
s32 spu_madr = 0;
s32 spu_bcr = 0;

s32 _spu_init(s32 mode) {
    u32 i;
    s32 channel;
    volatile u16 *spucnt;

    *dma_dpcr |= 0xB0000;

    _spu_transMode = 0;
    _spu_addrMode = 0;
    _spu_tsa = 0;
    _spu_RXX->rxx.main_vol.left = 0;
    _spu_RXX->rxx.main_vol.right = 0;
    _spu_RXX->rxx.spucnt = 0;
    _spu_Fw1ts();

    _spu_RXX->rxx.main_vol.left = 0;
    _spu_RXX->rxx.main_vol.right = 0;

    if (_spu_RXX->rxx.spustat & 0x7FF) {
        i = 0;
        do {
            if (++i > 0xF00) {
                printf("SPU:T/O [%s]\n", "wait (reset)");
                break;
            }
        } while (_spu_RXX->rxx.spustat & 0x7FF);
    }

    channel = 0;
    _spu_mem_mode = 2;
    _spu_mem_mode_plus = 3;
    _spu_mem_mode_unit = 8;
    _spu_mem_mode_unitM = 7;
    _spu_RXX->rxx.data_trans = 4;
    _spu_RXX->rxx.rev_vol.left = 0;
    _spu_RXX->rxx.rev_vol.right = 0;
    _spu_RXX->rxx.key_off[0] = 0xFFFF;
    _spu_RXX->rxx.key_off[1] = 0xFFFF;
    _spu_RXX->rxx.rev_mode[0] = 0;
    _spu_RXX->rxx.rev_mode[1] = 0;
    for (channel = 0; channel < 10; channel++) {
        _spu_RQ[channel] = 0;
    }

    if (mode == 0) {

        _spu_tsa = 0x200;
        _spu_RXX->rxx.chan_fm[0] = 0;
        _spu_RXX->rxx.chan_fm[1] = 0;
        _spu_RXX->rxx.noise_mode[0] = 0;
        _spu_RXX->rxx.noise_mode[1] = 0;
        _spu_RXX->rxx.cd_vol.left = 0;
        _spu_RXX->rxx.cd_vol.right = 0;
        _spu_RXX->rxx.ex_vol.left = 0;
        _spu_RXX->rxx.ex_vol.right = 0;
        _spu_FwriteByIO((s32)&spu_silent_block, 0x10);

        for (channel = 0; channel < 0x18; channel++) {
            _spu_RXX->rxx.voice[channel].volume.left = 0;
            _spu_RXX->rxx.voice[channel].volume.right = 0;
            _spu_RXX->rxx.voice[channel].pitch = 0x3FFF;
            _spu_RXX->rxx.voice[channel].addr = 0x200;
            _spu_RXX->rxx.voice[channel].adsr[0] = 0;
            _spu_RXX->rxx.voice[channel].adsr[1] = 0;
        }

        _spu_RXX->rxx.key_on[0] = 0xFFFF;
        _spu_RXX->rxx.key_on[1] = 0xFF;
        _spu_Fw1ts();
        _spu_Fw1ts();
        _spu_Fw1ts();
        _spu_Fw1ts();

        _spu_RXX->rxx.key_off[0] = 0xFFFF;
        _spu_RXX->rxx.key_off[1] = 0xFF;
        _spu_Fw1ts();
        _spu_Fw1ts();
        _spu_Fw1ts();
        _spu_Fw1ts();
    }

    _spu_inTransfer = 1;
    /* FAKE: _spu_RXX->rxx.spucnt = 0xC000 */
    spucnt = &_spu_RXX->rxx.spucnt;
    *spucnt = 0xC000;
    _spu_transferCallback = 0;
    _spu_IRQCallback = 0;
    return 0;
}

/* Writes to sound memory by hand, 64 bytes at a time. */

void _spu_FwriteByIO(u8 *addr, u32 size) {
    u16 spustat;
    s32 num;
    u16 *cur;
    s32 i;
    u32 j;
    u16 cnt;

    cur = (u16 *)addr;
    spustat = _spu_RXX->rxx.spustat & 0x7FF;
    _spu_RXX->rxx.trans_addr = _spu_tsa;
    _spu_Fw1ts();
    while (size != 0) {
        num = (size > 0x40) ? 0x40 : size;
        for (i = 0; i < num; i += 2) {
            _spu_RXX->rxx.trans_fifo = *cur++;
        }
        cnt = _spu_RXX->rxx.spucnt;
        cnt &= ~0x30;
        cnt |= 0x10;
        _spu_RXX->rxx.spucnt = cnt;
        _spu_Fw1ts();
        if (_spu_RXX->rxx.spustat & 0x400) {
            j = 0;
            do {
                if (++j > 0xF00) {
                    printf("SPU:T/O [%s]\n", "wait (wrdy H -> L)");
                    break;
                }
            } while (_spu_RXX->rxx.spustat & 0x400);
        }
        _spu_Fw1ts();
        _spu_Fw1ts();
        size -= num;
    }
    cnt = _spu_RXX->rxx.spucnt;
    j = 0;
    cnt &= ~0x30;
    _spu_RXX->rxx.spucnt = cnt;
    if ((_spu_RXX->rxx.spustat & 0x7FF) != spustat) {
        do {
            if (++j > 0xF00) {
                printf("SPU:T/O [%s]\n", "wait (dmaf clear/W)");
                break;
            }
        } while ((_spu_RXX->rxx.spustat & 0x7FF) != spustat);
    }
}

/* The interrupt handler for the end of a DMA transfer: takes the SPU out of
 * its transfer mode, then calls the transfer callback, or without one
 * delivers the event. */
void _spu_FiDMA(void) {
    u32 timeout;

    if (spu_reading == 0) {
        _spu_Fw1ts();
    }
    _spu_RXX->rxx.spucnt = _spu_RXX->rxx.spucnt & ~0x30;
    timeout = 0;
    while (_spu_RXX->rxx.spucnt & 0x30) {
        timeout++;
        if (timeout > 0xF00) {
            break;
        }
    }
    if (_spu_transferCallback) {
        _spu_transferCallback();
        return;
    }
    DeliverEvent(HwSPU, EvSpCOMP);
}

/* Starts a DMA read from sound memory. Nothing calls it. */
void _spu_Fr_(s32 addr, u16 spu_addr, s32 size) {
    _spu_RXX->rxx.trans_addr = spu_addr;
    _spu_Fw1ts();
    _spu_RXX->rxx.spucnt = _spu_RXX->rxx.spucnt | 0x30;
    _spu_Fw1ts();
    _spu_FsetDelayR();
    *dma_spu_madr = addr;
    *dma_spu_bcr = (size << 16) | 0x10;
    spu_reading = 1;
    *dma_spu_chcr = 0x1000200;
}

/* The steps of a transfer: 2 sets the address in sound memory, 1 and 0 put
 * the SPU in write and in read mode, 3 starts the DMA. */
typedef char *va_list;
#define va_start(ap, parmN) ((ap) = (va_list)(&(parmN) + 1))
#define va_arg(ap, T) ((ap) += sizeof(T), *(T *)((ap) - sizeof(T)))

s32 _spu_t(s32 mode, ...) {
    s32 chcr;
    u32 i;
    va_list args;
    u32 count;
    u16 ck2;
    u16 cnt;
    u16 t;

    va_start(args, mode);

    switch (mode) {
    case 2:
        count = va_arg(args, u32);
        _spu_tsa = count >> _spu_mem_mode_plus;
        _spu_RXX->rxx.trans_addr = _spu_tsa;
        break;

    case 1:
        t = _spu_RXX->rxx.trans_addr;
        i = 0;
        spu_reading = 0;
        if ((t & 0xFFFF) != _spu_tsa) {
            do {
                if (++i > 0xF00) {
                    return -2;
                }
            } while (_spu_RXX->rxx.trans_addr != _spu_tsa);
        }
        cnt = _spu_RXX->rxx.spucnt;
        cnt &= ~0x30;
        cnt |= 0x20;
        _spu_RXX->rxx.spucnt = cnt;
        break;

    case 0:
        t = _spu_RXX->rxx.trans_addr;
        i = 0;
        spu_reading = 1;
        if ((t & 0xFFFF) != _spu_tsa) {
            do {
                if (++i > 0xF00) {
                    return -2;
                }
            } while (_spu_RXX->rxx.trans_addr != _spu_tsa);
        }
        cnt = _spu_RXX->rxx.spucnt;
        cnt &= ~0x30;
        cnt |= 0x30;
        _spu_RXX->rxx.spucnt = cnt;
        break;

    case 3:
        if (spu_reading == 1) {
            ck2 = 0x30;
        } else {
            ck2 = 0x20;
        }
        i = 0;
        while ((_spu_RXX->rxx.spucnt & 0x30) != ck2) {
            if (++i > 0xF00) {
                return -2;
            }
        }
        if (spu_reading == 1) {
            _spu_FsetDelayR();
        } else {
            _spu_FsetDelayW();
        }
        count = va_arg(args, u32);
        spu_madr = count;
        count = va_arg(args, u32);
        spu_bcr = (count / 64);
        spu_bcr += ((count % 64) ? 1 : 0);
        *dma_spu_madr = spu_madr;
        *dma_spu_bcr = (spu_bcr << 16) | 0x10;
        if (spu_reading == 1) {
            chcr = 0x1000200;
        } else {
            chcr = 0x1000201;
        }
        *dma_spu_chcr = chcr;
        break;
    }
    return 0;
}

s32 _spu_Fw(s32 addr, s32 size) {
    if (_spu_transMode == 0) {
        _spu_t(2, _spu_tsa << _spu_mem_mode_plus);
        _spu_t(1);
        _spu_t(3, addr, size);
    } else {
        _spu_FwriteByIO(addr, size);
    }
    return size;
}

s32 _spu_Fr(s32 addr, s32 size) {
    _spu_t(2, _spu_tsa << _spu_mem_mode_plus);
    _spu_t(0);
    _spu_t(3, addr, size);
    return size;
}

void _spu_FsetRXX(s32 arg0, u32 arg1, s32 arg2) {
    if (arg2 == 0) {
        _spu_RXX->raw[arg0] = arg1;
        return;
    }
    _spu_RXX->raw[arg0] = arg1 >> _spu_mem_mode_plus;
}

s32 _spu_FsetRXXa(s32 index, s32 val) {
    s32 aligned;
    if (_spu_mem_mode != 0) {
        u32 step = _spu_mem_mode_unit;
        if ((u32)val % step != 0) {
            val += step;
            val &= ~_spu_mem_mode_unitM;
        }
    }
    aligned = (s32)((u32)val >> _spu_mem_mode_plus);
    switch (index) {
    case -1:
        return aligned & 0xFFFF;
    case -2:
        return val;
    default:
        _spu_RXX->raw[index] = aligned;
        return val;
    }
}

s32 _spu_FgetRXXa(s32 index, s32 mode) {
    u16 val = _spu_RXX->raw[index];
    if (mode == -1) {
        return val;
    }
    return val << _spu_mem_mode_plus;
}

void _spu_FsetPCR(s32 arg0) {
    *dma_dpcr &= 0xFFF8FFFF;
    if (arg0 != 0) {
        *dma_dpcr |= 0x30000;
    } else {
        *dma_dpcr |= 0x50000;
    }
}

void _spu_FsetDelayW(void) {
    *spu_delay = (*spu_delay & DMA_CHAN_MASK) | DMA_SPU_FROM_RAM;
}

void _spu_FsetDelayR(void) {
    *spu_delay = (*spu_delay & DMA_CHAN_MASK) | DMA_SPU_TO_RAM;
}

/* A short wait. */
void _spu_Fw1ts(void) {
    /* volatile so that the compiler keeps the loop */
    volatile s32 i;
    volatile s32 v = 0xD;
    for (i = 0; i < 0x3C; i++) {
        v = v * 13;
    }
}
