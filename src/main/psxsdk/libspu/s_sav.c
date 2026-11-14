/* LIBSPU S_SAV: _SpuSetAnyVoice, which turns a per-voice switch (noise,
 * reverb) on or off for a set of voices. */
#include "common.h"
#include "libspu_internal.h"

s32 _SpuSetAnyVoice(s32 on_off, u32 bits, s32 addr1, s32 addr2) {
    u32 state;

    if (_spu_env & 1) {
        state = ((_spu_RQ[addr2 - 0xC4] & 0xFF) << 16) | _spu_RQ[addr1 - 0xC4];
    } else {
        state = ((_spu_RXX->raw[addr2] & 0xFF) << 16) | _spu_RXX->raw[addr1];
    }
    switch (on_off) {
    case 1:
        if (_spu_env & 1) {
            _spu_RQ[addr1 - 0xC4] |= bits;
            _spu_RQ[addr2 - 0xC4] |= (bits >> 16) & 0xFF;
            _spu_RQmask |= 1 << ((addr1 - 0xC6) >> 1);
        } else {
            _spu_RXX->raw[addr1] |= bits;
            _spu_RXX->raw[addr2] |= (bits >> 16) & 0xFF;
        }
        state |= bits & 0xFFFFFF;
        break;
    case 0:
        if (_spu_env & 1) {
            _spu_RQ[addr1 - 0xC4] &= ~bits;
            _spu_RQ[addr2 - 0xC4] &= ~((bits >> 16) & 0xFF);
            _spu_RQmask |= 1 << ((addr1 - 0xC6) >> 1);
        } else {
            _spu_RXX->raw[addr1] &= ~bits;
            _spu_RXX->raw[addr2] &= ~((bits >> 16) & 0xFF);
        }
        state &= ~(bits & 0xFFFFFF);
        break;
    case 8:
        if (_spu_env & 1) {
            _spu_RQ[addr1 - 0xC4] = bits;
            _spu_RQ[addr2 - 0xC4] = (bits >> 16) & 0xFF;
            _spu_RQmask |= 1 << ((addr1 - 0xC6) >> 1);
        } else {
            _spu_RXX->raw[addr1] = bits;
            _spu_RXX->raw[addr2] = (bits >> 16) & 0xFF;
        }
        state = bits & 0xFFFFFF;
        break;
    }
    return state & 0xFFFFFF;
}
