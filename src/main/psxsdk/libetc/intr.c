/* LIBETC INTR: the interrupt dispatcher ($Id: intr.c,v 1.76). It hooks the
 * kernel's interrupt entry, and on an interrupt calls the handler of each
 * source that is pending, on a stack of its own. */
#include "common.h"
#include <psxsdk/libapi.h>
#include "libetc_internal.h"
#include <psxsdk/libc.h>

typedef struct {
    u16 interruptsInitialized;
    u16 inInterrupt;
    void (*handlers[11])(void); /* one for each interrupt source */
    u16 enabledInterruptsMask;
    u16 savedMask; /* the mask and DMA control registers, while stopped */
    s32 savedPcr;
    jmp_buf buf;     /* where the kernel jumps on an interrupt: trapIntr */
    s32 stack[1024]; /* and the stack it runs on */
} intrEnv_t;

typedef void (*IntrCallback)(void);

static IntrCallback setIntr(s32 irq, IntrCallback handler);
intrEnv_t *startIntr(void);
static intrEnv_t *stopIntr(void);
static intrEnv_t *restartIntr(void);
void memclr();

/* Nothing reads it. */
char lib_copyright[] =
    "Library Programs (c) 1993-1997 Sony Computer Entertainment Inc., All Rights Reserved.";

intrEnv_t intrEnv = {0};

/* What the functions below call through: the version, DMACallback's (set by
 * startIntr), InterruptCallback's, ResetCallback's, StopCallback's,
 * VSyncCallback's (set by startIntr), RestartCallback's, and the state. */
void *callbacks[8] = {
    "$Id: intr.c,v 1.76 1997/02/12 12:45:05 makoto Exp $",
    0,
    setIntr,
    startIntr,
    stopIntr,
    0,
    restartIntr,
    &intrEnv,
};

s32 *pCallbacks = (s32 *)callbacks;

volatile u16 *i_stat = (volatile u16 *)0x1F801070; /* I_STAT */
volatile u16 *imask = (volatile u16 *)0x1F801074;  /* I_MASK */
volatile s32 *d_pcr = (volatile s32 *)0x1F8010F0;  /* DPCR */

/* How many times in a row trapIntr has left an interrupt pending. */
s32 trapMissedCount = 0;

s32 ResetCallback(void) { return ((s32(*)(void))pCallbacks[3])(); }

void *InterruptCallback(s32 irq, void (*func)()) {
    return ((void *(*)(s32, void (*)()))pCallbacks[2])(irq, func);
}

void *DMACallback(s32 dma, void (*func)()) {
    return ((void *(*)(s32, void (*)()))pCallbacks[1])(dma, func);
}

s32 VSyncCallback(void (*f)()) {
    return ((s32(*)(s32, void (*)()))pCallbacks[5])(4, f);
}

void VSyncCallbacks(void) { ((void (*)(void))pCallbacks[5])(); }

s32 StopCallback(void) { return ((s32(*)(void))pCallbacks[4])(); }

s32 RestartCallback(void) { return ((s32(*)(void))pCallbacks[6])(); }

s32 CheckCallback(void) { return intrEnv.inInterrupt; }

u32 GetIntrMask(void) { return *imask; }

extern void trapIntr(void);
/* The BIOS routine takes nothing; startIntr's code loads pCallbacks for it
 * all the same, as an argument would be. */
extern void _96_remove(s32 *);

u16 SetIntrMask(u16 mask) {
    u16 old = *imask;
    *imask = mask;
    return old;
}

/* ResetCallback: takes over the interrupts, with nothing enabled. */
intrEnv_t *startIntr(void) {
    if (intrEnv.interruptsInitialized) {
        return 0;
    }
    *i_stat = (*imask = 0);
    *d_pcr = 0x33333333;
    memclr(&intrEnv, sizeof(intrEnv) / 4);
    if (setjmp(intrEnv.buf) != 0) {
        trapIntr();
    }
    intrEnv.buf[JB_SP] = (s32)&intrEnv.stack[1004];
    HookEntryInt(intrEnv.buf);
    intrEnv.interruptsInitialized = 1;
    pCallbacks[5] = startIntrVSync();
    pCallbacks[1] = startIntrDMA();
    _96_remove(pCallbacks);
    ExitCriticalSection();
    return &intrEnv;
}

/* Where the kernel lands on an interrupt: calls the handler of every source
 * that is pending and enabled, lowest first. */
void trapIntr(void) {
    s32 i;
    u16 mask;

    if (!intrEnv.interruptsInitialized) {
        printf("unexpected interrupt(%04x)\n", *i_stat);
        ReturnFromException();
    }
    intrEnv.inInterrupt = 1;
    while ((mask = (intrEnv.enabledInterruptsMask & *i_stat) & *imask) != 0) {
        for (i = 0; mask && i < 11; ++i, mask >>= 1) {
            if (mask & 1) {
                *i_stat = ~(1 << i);
                if (intrEnv.handlers[i] != 0) {
                    intrEnv.handlers[i]();
                }
            }
        }
    }
    if (*i_stat & *imask) {
        if (trapMissedCount++ > 0x800) {
            printf("intr timeout(%04x:%04x)\n", *i_stat, *imask);
            trapMissedCount = 0;
            *i_stat = 0;
        }
    } else {
        trapMissedCount = 0;
    }
    intrEnv.inInterrupt = 0;
    ReturnFromException();
}

/* InterruptCallback: sets a source's handler (or with 0 turns the source
 * off), and tells the kernel whether to go on acknowledging the vertical
 * blank and the root counters itself. */
static IntrCallback setIntr(s32 irq, IntrCallback handler) {
    IntrCallback prevHandler;
    s32 mask;

    prevHandler = intrEnv.handlers[irq];
    if (handler != prevHandler && intrEnv.interruptsInitialized) {
        mask = *imask;
        *imask = 0;
        if (handler != 0) {
            intrEnv.handlers[irq] = handler;
            mask = mask | (1 << irq);
            intrEnv.enabledInterruptsMask |= 1 << irq;
        } else {
            intrEnv.handlers[irq] = 0;
            mask = mask & ~(1 << irq);
            intrEnv.enabledInterruptsMask &= ~(1 << irq);
        }
        if (irq == 0) {
            ChangeClearPAD(handler == 0);
            ChangeClearRCnt(3, handler == 0);
        }
        if (irq == 4) {
            ChangeClearRCnt(0, handler == 0);
        }
        if (irq == 5) {
            ChangeClearRCnt(1, handler == 0);
        }
        if (irq == 6) {
            ChangeClearRCnt(2, handler == 0);
        }
        *imask = mask;
    }
    return prevHandler;
}

/* StopCallback: gives the interrupts back to the kernel. */
static intrEnv_t *stopIntr(void) {
    if (!intrEnv.interruptsInitialized) {
        return 0;
    }
    EnterCriticalSection();
    intrEnv.savedMask = *imask;
    intrEnv.savedPcr = *d_pcr;
    *i_stat = (*imask = 0);
    *d_pcr &= 0x77777777;
    ResetEntryInt();
    intrEnv.interruptsInitialized = 0;
    return &intrEnv;
}

/* RestartCallback: takes them again, as they were. */
static intrEnv_t *restartIntr(void) {
    if (intrEnv.interruptsInitialized) {
        return 0;
    }
    HookEntryInt(intrEnv.buf);
    intrEnv.interruptsInitialized = 1;
    *imask = intrEnv.savedMask;
    *d_pcr = intrEnv.savedPcr;
    ExitCriticalSection();
    return &intrEnv;
}

void memclr(u16 *ptr, s32 size) {
    s32 *e = (s32 *)ptr;
    s32 i;

    for (i = size - 1; i != -1; i--) {
        *e++ = 0;
    }
}
