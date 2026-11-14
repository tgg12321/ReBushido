/* LIBSPU S_INI: _SpuInit, which resets the SPU and the library's state, and
 * SpuStart, which opens the event that the end of a transfer delivers. */
#include "common.h"
#include <psxsdk/libetc.h>
#include "libspu_internal.h"
#include <psxsdk/libapi.h>

/* Sony's stamp (the letters Ps and a build number) at the head of the
 * module's data; nothing reads it. */
u32 _spu_stamp[2] = {0x17117350, 0x30404D7C};
s32 _spu_EVdma = 0;
s32 _spu_keystat = 0;
s32 _spu_trans_mode = 0;
s32 _spu_rev_flag = 0;
s32 _spu_rev_reserve_wa = 0;
s32 _spu_rev_offsetaddr = 0;
SpuRevAttr _spu_rev_attr = {0};
volatile s32 _spu_RQvoice = 0;
volatile s32 _spu_RQmask = 0;
/* The note each voice's waveform was recorded at; 0xC000 until it is set. */
u16 _spu_voice_centerNote[24] = {
    0xC000, 0xC000, 0xC000, 0xC000, 0xC000, 0xC000, 0xC000, 0xC000,
    0xC000, 0xC000, 0xC000, 0xC000, 0xC000, 0xC000, 0xC000, 0xC000,
    0xC000, 0xC000, 0xC000, 0xC000, 0xC000, 0xC000, 0xC000, 0xC000};
/* A kilobyte of zeroes: what SpuClearReverbWorkArea writes, over and over. */
s32 _spu_zerobuf[256] = {0};
volatile s32 _spu_env = 0;
s32 _spu_isCalled = 0;

void _SpuInit(s32 mode) {
    s32 i;
    s32 unset;

    ResetCallback();
    _spu_init(mode);
    unset = 0xC000;
    if (mode == 0) {
        for (i = 0; i < 24; i++) {
            _spu_voice_centerNote[i] = unset;
        }
    }
    SpuStart();
    _spu_rev_flag = 0;
    _spu_rev_reserve_wa = 0;
    _spu_rev_attr.mode = 0;
    _spu_rev_attr.depth.left = 0;
    _spu_rev_attr.depth.right = 0;
    _spu_rev_attr.delay = 0;
    _spu_rev_attr.feedback = 0;
    _spu_rev_offsetaddr = _spu_rev_startaddr[0];
    _spu_FsetRXX(0xD1, _spu_rev_startaddr[0], 0);
    _spu_AllocBlockNum = 0;
    _spu_AllocLastNum = 0;
    _spu_memList = 0;
    _spu_trans_mode = 0;
    _spu_transMode = 0;
    _spu_keystat = 0;
    _spu_RQmask = 0;
    _spu_RQvoice = 0;
    _spu_env = 0;
}

void SpuStart(void) {
    if (_spu_isCalled == 0) {
        _spu_isCalled = 1;
        EnterCriticalSection();
        _SpuDataCallback((s32)_spu_FiDMA);
        _spu_EVdma = OpenEvent((s32)0xF0000009, 0x20, 0x2000, 0);
        EnableEvent(_spu_EVdma);
        ExitCriticalSection();
    }
}
