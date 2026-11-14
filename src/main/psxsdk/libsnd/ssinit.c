/* LIBSND SSINIT: _SsInit, which clears the SPU's registers and the sound
 * system's tables. */
#include "common.h"
#include "libsnd_i.h"

s32 _snd_ev_flag;
s32 _snd_openflag;
s32 VBLANK_MINUS;

/* Sony's stamp (the letters Ps and a build number) at the head of the
 * module's data; nothing reads it. */
u32 _snd_stamp[2] = {0x17107350, 0x404097E4};
/* What each voice's registers are set to: no volume, the pitch of the
 * waveform as recorded, an address and an envelope. */
u16 default_voice[8] = {0, 0, 0x1000, 0x3000, 0xBF, 0, 0, 0};
/* What the SPU's sixteen control registers are set to: the main volume full,
 * the rest 0. */
u16 default_state[16] = {0x3FFF, 0x3FFF};
s32 _SsMarkCallback[32][16];

void _SsInit(void) {
    u16 *reg;
    int i, j;

    reg = (u16 *)0x1F801C00;
    for (i = 0; i < 24; i++) {
        for (j = 0; j < 8; j++) {
            *reg++ = default_voice[j];
        }
    }

    reg = (u16 *)0x1F801D80;
    for (i = 0; i < 16; i++) {
        *reg++ = default_state[i];
    }

    _SsVmInit(0x18);

    for (j = 0; j < 32; j++) {
        for (i = 0; i < 16; i++) {
            _SsMarkCallback[j][i] = 0;
        }
    }

    VBLANK_MINUS = 60;
    _snd_openflag = 0;
    _snd_ev_flag = 0;
}
