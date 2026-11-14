/* LIBAPI PAD: starting and stopping the BIOS controller driver, with the
 * patches this library makes to it. */
#include "common.h"
#include <psxsdk/libapi.h>

s32 _Pad1(void);
extern volatile SioRegs *_pad_sio;
extern volatile u32 *_pad_intr;

extern s32 is_pad_init;

void SetInitPadFlag(s32 flag) { is_pad_init = flag; }

s32 ReadInitPadFlag(void) { return is_pad_init; }

void _remove_ChgclrPAD(void);
void _patch_pad(void);
s32 SetPatchPad(void);
void PAD_init2(s32, s32, s32, s32);
void InitPAD2(char *, s32, char *, s32);
void StartPAD2(void);
void StopPAD2(void);
s32 RemovePatchPad(void);
void _send_pad(void);

void PAD_init(s32 a0, s32 a1, s32 a2, s32 a3) {
    _remove_ChgclrPAD();
    EnterCriticalSection();
    _patch_pad();
    ExitCriticalSection();
    ChangeClearPAD(0);
    SetPatchPad();
    PAD_init2(a0, a1, a2, a3);
    _send_pad();
    is_pad_init = 1;
}

s32 InitPAD(char *bufA, s32 lenA, char *bufB, s32 lenB) {
    _remove_ChgclrPAD();
    EnterCriticalSection();
    _patch_pad();
    ExitCriticalSection();
    ChangeClearPAD(0);
    SetPatchPad();
    InitPAD2(bufA, lenA, bufB, lenB);
    _send_pad();
    is_pad_init = 1;
}

s32 StartPAD(void) {
    StartPAD2();
    ChangeClearPAD(0);
    EnablePAD();
}

void StopPAD(void) {
    DisablePAD();
    StopPAD2();
    RemovePatchPad();
    is_pad_init = 0;
}

extern void SysDeqIntRP(s32, u32 *);
extern void SysEnqIntRP(s32, u32 *);

extern s32 _IsVSync(void);
/* The interrupt handler record SetPatchPad queues: next, the handler, the
 * test that says whether to call it, and a spare word.
 * FAKE: one record of four words, patch0 */
static u32 patch0;
static u32 patch0_plus_0x4;
static u32 patch0_plus_0x8;
static u32 patch0_plus_0xC;

s32 SetPatchPad(void) {
    u32 *v1 = &patch0_plus_0x4;
    u32 *s0 = v1 - 1;
    EnterCriticalSection();
    *v1 = (u32)_Pad1;
    patch0_plus_0x8 = (u32)_IsVSync;
    patch0 = 0;
    patch0_plus_0xC = 0;
    SysDeqIntRP(1, s0);
    SysEnqIntRP(1, s0);
    ExitCriticalSection();
    return 1;
}

s32 RemovePatchPad(void) {
    EnterCriticalSection();
    SysDeqIntRP(1, &patch0);
    ExitCriticalSection();
    return 1;
}

/* The queued handler: clears the controller port's control register and
 * waits a moment. */
s32 _Pad1(void) {
    volatile s32 i; /* the counter of a short wait */
    s32 unused[2];  /* nothing uses it */
    _pad_sio->ctrl = 0;
    i = 10;
    while (--i != -1) {
    }
    return 0;
}

/* Whether the pending interrupt is an enabled vertical blank. */
s32 _IsVSync(void) {
    volatile u32 *p = _pad_intr;
    s32 ret;
    if ((p[1] & 1) == 0)
        return 0;
    if ((p[0] & 1) != 0) {
        ret = 1;
    } else {
        /* FAKE: ret = 0 */
        ret = 1;
        ret = 0;
    }
    return ret;
}

s32 is_pad_init = 0;
volatile SioRegs *_pad_sio = (SioRegs *)0x1F801040; /* the controller port */
volatile u32 *_pad_intr = (u32 *)0x1F801070;        /* I_STAT; [1] is I_MASK */
