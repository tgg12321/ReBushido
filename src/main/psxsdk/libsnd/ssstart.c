/* LIBSND SSSTART: SsStart, which starts the tick, on the vertical blank or on
 * a root counter according to SsSetTickMode. */
#include "common.h"
#include "libsnd_i.h"
#include <psxsdk/libapi.h>
#include <psxsdk/libetc.h>

static void _SsTrapIntrVSync(void);
static void _SsSeqCalledTbyT_1per2(void);
extern void SsSeqCalledTbyT(void);

/* How the tick is driven: sixty a second, by an interrupt not yet chosen,
 * calling SsSeqCalledTbyT. */
#if RB_PORT
/* In the port the function's address is put in as the tick is started
 * (_SsStart): a 64-bit program's compiler cannot put an address into a
 * 32-bit word of a table it fills (docs/PORT.md, "64 bits"). */
SndSeqTickEnv _snd_seq_tick_env = {60, 1, 0, 0, 0, 0, 0x7F, 0, 0};
#else
SndSeqTickEnv _snd_seq_tick_env = {
    60, 1, (s32)SsSeqCalledTbyT, 0, 0, 0, 0x7F, 0, 0};
#endif

/* arg0: 1 for SsStart, 0 for SsStart2, which does not tick on the vertical
 * blank even at a rate that fits it. */
void _SsStart(s32 use_vsync) {
    u16 rcnt_target;
    u32 rcnt_spec;

    s32 wait = 1000;
    while (--wait >= 0) {
    }

#if RB_PORT
    _snd_seq_tick_env.tick_func = (s32)SsSeqCalledTbyT;
#endif
    _snd_seq_tick_env.on_vsync = 0;
    _snd_seq_tick_env.irq = 6;
    _snd_seq_tick_env.half_rate = 0;
    _snd_seq_tick_env.old_callback = 0;
    rcnt_spec = 0xF2000002;
    rcnt_target = 0x44E8;
    switch (_snd_seq_tick_env.tick_rate) {
    case 0:
        _snd_seq_tick_env.irq = 0x7F;
        return;

    case 5:
        _snd_seq_tick_env.irq = 0;
        if (use_vsync == 0) {
            _snd_seq_tick_env.on_vsync = 1;
        } else {
            rcnt_spec = 0xF2000003;
            rcnt_target = 1;
        }
        break;

    case 3:
        rcnt_target = 0x89D0;
        break;

    case 2:
        break;

    default:
        if (_snd_seq_tick_env.manual_tick == 0) {
            if (_snd_seq_tick_env.tick_rate < 0x46) {
                rcnt_target = 0x204CC0 / _snd_seq_tick_env.tick_rate;
                _snd_seq_tick_env.half_rate++;
            } else {
                rcnt_target = 0x409980 / _snd_seq_tick_env.tick_rate;
            }
        } else {
            return;
        }
        break;
    }

    if (_snd_seq_tick_env.on_vsync != 0) {
        EnterCriticalSection();
        VSyncCallback((void (*)())ADDR(_snd_seq_tick_env.tick_func));
    } else {
        s32 de;
        s32 a1_val;
        EnterCriticalSection();
        ResetRCnt(rcnt_spec);
        SetRCnt(rcnt_spec, rcnt_target, 0x1000);
        de = _snd_seq_tick_env.irq;
        if (de == 0) {
            s32 ret = (s32)InterruptCallback(0, 0);
            de = _snd_seq_tick_env.irq;
            a1_val = (s32)&_SsTrapIntrVSync;
            _snd_seq_tick_env.old_callback = ret;
        } else {
            a1_val = (s32)&_SsSeqCalledTbyT_1per2;
            if (_snd_seq_tick_env.half_rate == 0) {
                a1_val = _snd_seq_tick_env.tick_func;
            }
        }
        InterruptCallback(de, (void (*)())ADDR(a1_val));
    }
    ExitCriticalSection();
}

void SsStart(void) { _SsStart(1); }

static void SsStart2(void) { _SsStart(0); }

static void _SsTrapIntrVSync(void) {
    if (_snd_seq_tick_env.old_callback != 0) {
        ((void (*)(void))ADDR(_snd_seq_tick_env.old_callback))();
    }
    ((void (*)(void))ADDR(_snd_seq_tick_env.tick_func))();
}

/* The interrupt handler when the rate asked for is half the vertical blank:
 * every other call is a tick. */
static void _SsSeqCalledTbyT_1per2(void) {
    SndSeqTickEnv *env = &_snd_seq_tick_env;

    if (env->half_toggle == 0) {
        env->half_toggle = 1;
    } else {
        env->half_toggle = 0;
        ((void (*)(void))ADDR(env->tick_func))();
    }
}
