/* LIBCD C_011: StCdInterrupt, the streaming library's handler for a sector
 * arriving, and the two ways it copies the sector into the ring. */
#include "libcd_st.h"
#include "libcd_reg.h"
#include "include_asm.h"

void mem2mem(u32 *dst, u32 *src, u32 n, u32 last);
void dma_execute(
    s32 ch, u32 *madr, u32 bcr_hi, u32 bcr_lo, u32 chcr, u8 irq, s32 unused);

/* The CD-ROM's bus timing registers, the DMA control and interrupt
 * registers, DMA channels 1, 2, 3 and 5 (control, then address) and the
 * interrupt status and mask. Most are not used here. */
static volatile u32 *cdrom_delay = (u32 *)0x1F801018;
static volatile u32 *com_delay = (u32 *)0x1F801020;
static volatile u32 *d_pcr = (u32 *)0x1F8010F0;
static volatile u32 *d_icr = (u32 *)0x1F8010F4;
static volatile u32 *d1_chcr = (u32 *)0x1F801098;
static volatile u32 *d1_madr = (u32 *)0x1F801090;
static volatile u32 *d2_chcr = (u32 *)0x1F8010A8;
static volatile u32 *d2_madr = (u32 *)0x1F8010A0;
static volatile u32 *d3_chcr = (u32 *)0x1F8010B8;
static volatile u32 *d3_madr = (u32 *)0x1F8010B0;
static volatile u32 *d5_chcr = (u32 *)0x1F8010D8;
static volatile u32 *d5_madr = (u32 *)0x1F8010D0;
static volatile u32 *i_stat = (u32 *)0x1F801070;
static volatile u32 *i_mask = (u32 *)0x1F801074;

/* Why the last interrupt ended as it did; nothing reads it. */
static s32 debug_cause = 0;
static s32 st_unused = 0;

/* When the stream comes from memory: the sector to copy next. */
s32 StEmu_Idx;
/* Where the sector's body goes. */
u32 *StRingBase;

void StCdInterrupt(void) {
    /* The header of the sector being taken in. */
    static volatile u16 *HeaderArea;
    volatile s16 status[4];
    CdlLOC loc;
    u8 result[8];
    u32 *dst;
    u32 chcr;
    u32 *src;
    u32 i;
    u8 *p;

    if (StFinalSector == 1) {
        return;
    }
    if (StRgb24 != 0 && (*d1_chcr & 0x01000000)) {
        StCdIntrFlag = 1;
        if (StEmu_Addr != 0) {
            StEmu_Idx++;
        }
        debug_cause = 1;
        return;
    }
    if (CdReady(1, result) == CdlDiskError) {
        return;
    }
    status[1] = result[0];
    status[2] = result[1];
    if (status[1] & 4) {
        debug_cause = 3;
        return;
    }
    HeaderArea = (u16 *)&StRingAddr[StRingIdx1];
    if (HeaderArea[0] != ST_SLOT_FREE) {
        if (StEmu_Addr != 0) {
            StEmu_Idx++;
        }
        debug_cause = 4;
        return;
    }
    *cd_reg0 = 0;
    *cd_reg3 = 0;
    *cd_reg0 = 0;
    *cd_reg3 = 0x80;
    *cdrom_delay = 0x20943;
    *com_delay = 0x1323;
    if (StMode == 0) {
        p = (u8 *)&loc;
        do {
            *p++ = *cd_reg2;
        } while (p < (u8 *)&loc + 4);
        for (i = 0; i < 8; i++) {
            *cd_reg2;
        }
    }
    chcr = 0x11000000;
    if (StEmu_Addr != 0) {
        mem2mem(
            (u32 *)HeaderArea, (u32 *)(StEmu_Addr + (StEmu_Idx << 11)), 8, 0);
    } else {
        dma_execute(3, (u32 *)HeaderArea, 0, 8, chcr, 0, 0);
    }
    while (*d3_chcr & 0x01000000) {
    }
    ((StSECTOR *)HeaderArea)->loc = loc;
    *cdrom_delay = 0x20843;
    *com_delay = 0x1325;
    if (StSTART_FLAG == 1 && StStartFrame != 0) {
        if (StStartFrame != HeaderArea[4]) {
            HeaderArea[0] = ST_SLOT_FREE;
            if (StEmu_Addr != 0) {
                StEmu_Idx++;
            }
            return;
        }
        StSTART_FLAG = 0;
    }
    if (HeaderArea[0] != 0x160 || ((HeaderArea[1] >> 10) & 0x1F) != CChannel) {
        if (StEmu_Addr != 0) {
            StEmu_Idx = 0;
        } else {
            HeaderArea[0];
        }
        debug_cause = 5;
        HeaderArea[0] = ST_SLOT_FREE;
        return;
    }
    if (Stsector_offset != HeaderArea[2] ||
        (Stframe_no != 0 && Stframe_no != HeaderArea[4])) {
        Stframe_no = 0;
        Stsector_offset = 0;
        init_ring_status(StRingIdx2, StRingIdx1 - StRingIdx2);
        StRingIdx1 = StRingIdx2;
        HeaderArea[0] = ST_SLOT_FREE;
        if (StEmu_Addr != 0) {
            StEmu_Idx++;
        }
        debug_cause = 6;
        return;
    }
    if (HeaderArea[2] == 0) {
        Stsector_offset = 0;
        Stframe_no = HeaderArea[4];
        if (StEndFrame != 0 && Stframe_no >= StEndFrame) {
            Stframe_no = 0;
            Stsector_offset = 0;
            init_ring_status(StRingIdx2, StRingIdx1 - StRingIdx2);
            StRingIdx1 = StRingIdx2;
            HeaderArea[0] = ST_SLOT_FREE;
            StSTART_FLAG = 1;
            if (StFunc2 != NULL) {
                StFunc2();
            }
            if (StEmu_Addr != 0) {
                StEmu_Idx++;
            }
            debug_cause = 7;
            return;
        }
        if ((u32)(StRingSize - StRingIdx1 - 1) < HeaderArea[3]) {
            if (StEndFrame == 0) {
                HeaderArea[0] = ST_SLOT_WRAP;
                StSTART_FLAG = 1;
                if (StFunc2 != NULL) {
                    StFunc2();
                }
                if (StEmu_Addr != 0) {
                    StEmu_Idx++;
                }
                debug_cause = 8;
                return;
            }
            if ((s16)StRingAddr->id != ST_SLOT_FREE) {
                HeaderArea[0] = ST_SLOT_FREE;
                if (StEmu_Addr != 0) {
                    StEmu_Idx++;
                }
                debug_cause = 9;
                return;
            }
            HeaderArea[0] = ST_SLOT_WRAP;
            dst = (u32 *)StRingAddr;
            src = (u32 *)HeaderArea;
            StRingIdx1 = 0;
            for (i = 0; i < 8; i++) {
                *dst++ = *src++;
            }
            HeaderArea = (u16 *)StRingAddr;
        }
        StRingIdx2 = StRingIdx1;
    }
    debug_cause = 10;
    Stsector_offset++;
    StRingBase = (u32 *)(&StRingAddr[StRingSize] + StRingIdx1 * 0x3F);
    if (StRgb24 != 0) {
        chcr = 0x11000000;
        *cdrom_delay = 0x20943;
        *com_delay = 0x1323;
    } else {
        *cdrom_delay = 0x21020843;
        chcr = 0x11400100;
    }
    if (HeaderArea[3] - 1 == HeaderArea[2]) {
        StFinalSector = 1;
        if (StEmu_Addr != 0) {
            mem2mem(StRingBase, (u32 *)(StEmu_Addr + (StEmu_Idx << 11) + 0x20),
                    0x1F8, 1);
            StEmu_Idx++;
        } else {
            dma_execute(3, StRingBase, 0, 0x1F8, chcr, 1, 0);
        }
        Stsector_offset = 0;
        Stframe_no = 0;
        CChannel = StCHANNEL;
    } else {
        if (StEmu_Addr != 0) {
            mem2mem(StRingBase, (u32 *)(StEmu_Addr + (StEmu_Idx << 11) + 0x20),
                    0x1F8, 0);
            StEmu_Idx++;
        } else {
            dma_execute(3, StRingBase, 0, 0x1F8, chcr, 0, 0);
        }
    }
    *com_delay = 0x1325;
    HeaderArea[0] = ST_SLOT_LOADING;
    StRingIdx1++;
    if (StEmu_Addr != 0 && StFinalSector != 0) {
        data_ready_callback();
    }
}

void mem2mem(u32 *dst, u32 *src, u32 n, u32 last) {
    u32 i;

    for (i = 0; i < n; i++) {
        *dst++ = *src++;
    }
}

/* What dma_execute prints when a channel stays busy. */
static const char dma_status_error[] = "DMA STATUS ERROR %x\n";

/* Starts a DMA transfer on channel `ch`, with or without an interrupt at its
 * end: waits for the channel, sets or clears its interrupt bit in d_icr,
 * enables it in d_pcr, writes the address, the count and the control word.
 * FAKE: compiled in Sony's library, a listing here. In C it comes to within
 * about 70 instructions of 100. The original keeps `irq` as a byte and
 * widens it again before comparing it with 1 (`andi $v0, $s1, 0xFF`), which
 * no declaration tried gives (u8, char, K&R style, a cast, a local copy, a
 * switch), and it stores the changed byte of d_icr once for both branches.
 * The channel's registers as an array of structs at 0x1F801080 give the
 * original's addressing; that much is settled. */
INCLUDE_ASM("movovl/asm/funcs", dma_execute);
