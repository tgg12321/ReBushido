/* LIBCARD INIT: InitCARD, StartCARD and StopCARD, which start and stop the
 * BIOS memory card driver. */
#include "common.h"
#include <psxsdk/libapi.h>

extern void _ExitCard(void);

void InitCARD(s32 val) {
    ChangeClearPAD(0);
    EnterCriticalSection();
    if (ReadInitPadFlag() == 0) {
        val = 0;
    }
    InitCARD2(val);
    ExitCriticalSection();
}

s32 StartCARD(void) {
    EnterCriticalSection();
    StartCARD2();
    ChangeClearPAD(0);
    ExitCriticalSection();
}

s32 StopCARD(void) {
    StopCARD2();
    _ExitCard();
}

/* Sony's stamp (the letters Ps and a build number) at the head of the
 * module's data; nothing reads it. */
u32 _card_init_stamp[2] = {0x15037350, 0x0040859C};
