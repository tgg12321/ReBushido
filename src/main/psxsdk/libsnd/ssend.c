/* LIBSND SSEND: SsEnd, which stops the tick. */
#include "common.h"
#include "libsnd_i.h"
#include <psxsdk/libapi.h>
#include <psxsdk/libetc.h>

void SsEnd(void) {
    if (_snd_seq_tick_env.manual_tick != 0) {
        return;
    }
    _snd_seq_tick_env.half_rate = 0;
    if (_snd_seq_tick_env.irq == 0x7F) {
        return;
    }
    EnterCriticalSection();
    if (_snd_seq_tick_env.on_vsync != 0) {
        VSyncCallback(0);
        _snd_seq_tick_env.on_vsync = 0;
    } else if (_snd_seq_tick_env.irq == 0) {
        InterruptCallback(0, (void (*)())_snd_seq_tick_env.old_callback);
        _snd_seq_tick_env.old_callback = 0;
    } else {
        InterruptCallback(6, 0);
    }
    ExitCriticalSection();
    _snd_seq_tick_env.irq = 0x7F;
}
