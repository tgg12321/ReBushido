/* LIBPRESS LIBPRESS: the MDEC, the console's picture decompressor. The
 * DecDCT functions are the library's interface; the MDEC functions under
 * them drive the chip and its two DMA channels. */
#include "common.h"
#include <psxsdk/libetc.h>
#include "sdk.h"

/* What DecDCTGetEnv and DecDCTPutEnv copy: the two quantisation tables and
 * the cosine table. */
typedef struct {
    u8 iq_y[64];
    u8 iq_c[64];
    s16 dct[64];
} DECDCTENV;

void MDEC_reset(s32 mode);
void MDEC_in(u32 *data, u32 size);
void MDEC_out(u32 *data, u32 size);
s32 MDEC_in_sync(void);
s32 MDEC_out_sync(void);
u32 MDEC_status(void);
s32 timeout(char *name);

/* The command that loads the quantisation tables, and the tables: luminance,
 * then chrominance. */
static u32 mdec_iq[] = {
    0x40000001, 0x13101002, 0x16161310, 0x16161616, 0x1B1A181A, 0x1A1A1B1B,
    0x1B1B1A1A, 0x1D1D1D1B, 0x1D222222, 0x1B1B1D1D, 0x20201D1D, 0x26252222,
    0x22232325, 0x28262623, 0x30302828, 0x38382E2E, 0x5345453A, 0x13101002,
    0x16161310, 0x16161616, 0x1B1A181A, 0x1A1A1B1B, 0x1B1B1A1A, 0x1D1D1D1B,
    0x1D222222, 0x1B1B1D1D, 0x20201D1D, 0x26252222, 0x22232325, 0x28262623,
    0x30302828, 0x38382E2E, 0x5345453A};

/* The command that loads the cosine table, and the table. */
static u32 mdec_coef[] = {
    0x60000000, 0x5A825A82, 0x5A825A82, 0x5A825A82, 0x5A825A82, 0x6A6D7D8A,
    0x18F8471C, 0xB8E3E707, 0x82759592, 0x30FB7641, 0x89BECF04, 0xCF0489BE,
    0x764130FB, 0xE7076A6D, 0xB8E38275, 0x7D8A471C, 0x959218F8, 0xA57D5A82,
    0x5A82A57D, 0xA57D5A82, 0x5A82A57D, 0x8275471C, 0x6A6D18F8, 0xE7079592,
    0xB8E37D8A, 0x89BE30FB, 0xCF047641, 0x7641CF04, 0x30FB89BE, 0xB8E318F8,
    0x82756A6D, 0x95927D8A, 0xE707471C};

void DecDCTReset(s32 mode) {
    if (mode == 0) {
        ResetCallback();
    }
    MDEC_reset(mode);
}

static inline void copy_words(u32 *dst, u32 *src, s32 n) {
    while (--n != -1) {
        *dst++ = *src++;
    }
}

DECDCTENV *DecDCTGetEnv(DECDCTENV *env) {
    copy_words((u32 *)env->iq_y, &mdec_iq[1], 16);
    copy_words((u32 *)env->iq_c, &mdec_iq[17], 16);
    copy_words((u32 *)env->dct, &mdec_coef[1], 32);
    return env;
}

DECDCTENV *DecDCTPutEnv(DECDCTENV *env) {
    copy_words(&mdec_iq[1], (u32 *)env->iq_y, 16);
    copy_words(&mdec_iq[17], (u32 *)env->iq_c, 16);
    MDEC_in(mdec_iq, 32);
    MDEC_in(mdec_coef, 32);
    return env;
}

/* The length, in words, of the run-level data DecDCTvlc made from `bs`. */
s32 DecDCTBufSize(u32 *bs) { return *(u16 *)bs; }

/* Bit 0 of `mode` asks for 24-bit colour, bit 1 for the mask bit to be set
 * in 16-bit pixels. */
void DecDCTin(u32 *buf, s32 mode) {
    if (mode & 1) {
        *buf &= ~0x08000000;
    } else {
        *buf |= 0x08000000;
    }
    if (mode & 2) {
        *buf |= 0x02000000;
    } else {
        *buf &= ~0x02000000;
    }
    MDEC_in(buf, *(u16 *)buf);
}

void DecDCTout(u32 *buf, s32 size) { MDEC_out(buf, size); }

/* With `mode` 0 waits for the MDEC to take its input; otherwise says whether
 * it is still busy with it. */
s32 DecDCTinSync(s32 mode) {
    if (mode != 0) {
        return (MDEC_status() >> 29) & 1;
    }
    return MDEC_in_sync();
}

s32 DecDCToutSync(s32 mode) {
    if (mode != 0) {
        return (MDEC_status() >> 24) & 1;
    }
    return MDEC_out_sync();
}

void DecDCTinCallback(void (*func)(void)) { DMACallback(0, func); }

void DecDCToutCallback(void (*func)(void)) { DMACallback(1, func); }

/* Sony's stamp (the letters Ps and a build number); nothing reads it. */
static u32 stamp[2] = {0x150E7350, 0x0040AE9C};

/* DMA channels 0 (into the MDEC) to 3, the MDEC's two registers and the DMA
 * control register. Channels 2 and 3 are not used here. */
static volatile u32 *d0_madr = (u32 *)0x1F801080;
static volatile u32 *d0_bcr = (u32 *)0x1F801084;
static volatile u32 *d0_chcr = (u32 *)0x1F801088;
static volatile u32 *d1_madr = (u32 *)0x1F801090;
static volatile u32 *d1_bcr = (u32 *)0x1F801094;
static volatile u32 *d1_chcr = (u32 *)0x1F801098;
static volatile u32 *d2_madr = (u32 *)0x1F8010A0;
static volatile u32 *d2_bcr = (u32 *)0x1F8010A4;
static volatile u32 *d2_chcr = (u32 *)0x1F8010A8;
static volatile u32 *d3_madr = (u32 *)0x1F8010B0;
static volatile u32 *d3_bcr = (u32 *)0x1F8010B4;
static volatile u32 *d3_chcr = (u32 *)0x1F8010B8;
static volatile u32 *mdec0 = (u32 *)0x1F801820;
static volatile u32 *mdec1 = (u32 *)0x1F801824;
static volatile u32 *d_pcr = (u32 *)0x1F8010F0;

/* Mode 0 resets the chip and loads the tables; mode 1 only resets it. */
void MDEC_reset(s32 mode) {
    switch (mode) {
    case 0:
        *mdec1 = 0x80000000;
        *d0_chcr = 0;
        *d1_chcr = 0;
        *mdec1 = 0x60000000;
        MDEC_in(mdec_iq, 32);
        MDEC_in(mdec_coef, 32);
        break;
    case 1:
        *mdec1 = 0x80000000;
        *d0_chcr = 0;
        *d1_chcr = 0;
        *d1_chcr;
        *mdec1 = 0x60000000;
        break;
    default:
        printf("MDEC_rest:bad option(%d)\n", mode);
        break;
    }
}

/* Sends a command word and the `size` words after it. */
void MDEC_in(u32 *data, u32 size) {
    MDEC_in_sync();
    *d_pcr |= 0x88;
    *d0_madr = (u32)(data + 1);
    *d0_bcr = ((size >> 5) << 16) | 0x20;
    *mdec0 = *data;
    *d0_chcr = 0x01000201;
}

/* Asks for `size` words of decoded picture at `data`. */
void MDEC_out(u32 *data, u32 size) {
    MDEC_out_sync();
    *d_pcr |= 0x88;
    *d1_chcr = 0;
    *d1_madr = (u32)data;
    *d1_bcr = ((size >> 5) << 16) | 0x20;
    *d1_chcr = 0x01000200;
}

s32 MDEC_in_sync(void) {
    volatile s32 tries = 0x100000;

    while (*mdec1 & 0x20000000) {
        if (--tries == -1) {
            timeout("MDEC_in_sync");
            return -1;
        }
    }
    return 0;
}

s32 MDEC_out_sync(void) {
    volatile s32 tries = 0x100000;

    while (*d1_chcr & 0x01000000) {
        if (--tries == -1) {
            timeout("MDEC_out_sync");
            return -1;
        }
    }
    return 0;
}

u32 MDEC_status(void) { return *mdec1; }

/* Prints what the chip and its DMA channels were doing, and resets it. */
s32 timeout(char *name) {
    char *dma = "\t DMA=(%d,%d), ADDR=(0x%08x->0x%08x)\n";
    char *fifo = "\t FIFO=(%d,%d),BUSY=%d,DREQ=(%d,%d),RGB24=%d,STP=%d\n";
    u32 status;

    printf("%s timeout:\n", name);
    status = *mdec1;
    printf(dma, (*d0_chcr >> 24) & 1, (*d1_chcr >> 24) & 1, *d0_madr, *d1_madr);
    printf(fifo, ~status >> 31, (status >> 30) & 1, (status >> 29) & 1,
           (status >> 28) & 1, (status >> 27) & 1, (status >> 25) & 1,
           (status >> 23) & 1);
    *mdec1 = 0x80000000;
    *d0_chcr = 0;
    *d1_chcr = 0;
    *d1_chcr;
    *mdec1 = 0x60000000;
    return 0;
}
