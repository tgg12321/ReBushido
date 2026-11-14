/* LIBSND SSTICK: SsSetTickMode, which sets how often the sound system ticks. */
#include "common.h"
#include "libsnd_i.h"
#include <psxsdk/libetc.h>

void SsSetTickMode(s32 arg) {
    s32 mode;

    mode = GetVideoMode();
    if (arg & 0x1000) {
        _snd_seq_tick_env.manual_tick = 1;
        _snd_seq_tick_env.tick_rate = arg & 0xFFF;
    } else {
        _snd_seq_tick_env.manual_tick = 0;
        _snd_seq_tick_env.tick_rate = arg;
    }
    if (_snd_seq_tick_env.tick_rate < 6) {
        switch (_snd_seq_tick_env.tick_rate) {
        case 4:
            VBLANK_MINUS = 50;
            if (mode != 1) {
                _snd_seq_tick_env.tick_rate = 50;
            } else {
                _snd_seq_tick_env.tick_rate = 5;
            }
            return;
        case 1:
            VBLANK_MINUS = 60;
            if (mode == 0) {
                _snd_seq_tick_env.tick_rate = 5;
            } else {
                _snd_seq_tick_env.tick_rate = 60;
            }
            return;
        case 3:
            VBLANK_MINUS = 120;
            return;
        case 2:
            VBLANK_MINUS = 240;
            return;
        case 5:
            if (mode == 0) {
                VBLANK_MINUS = 60;
            } else if (mode == 1) {
                VBLANK_MINUS = 50;
            } else {
                VBLANK_MINUS = 60;
            }
            break;
        case 0:
            if (mode == 0) {
                VBLANK_MINUS = 60;
            } else if (mode == 1) {
                VBLANK_MINUS = 50;
            } else {
                VBLANK_MINUS = 60;
            }
            return;
        default:
            VBLANK_MINUS = 60;
            return;
        }
    } else {
        VBLANK_MINUS = _snd_seq_tick_env.tick_rate;
    }
}
