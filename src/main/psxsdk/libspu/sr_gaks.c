/* LIBSPU SR_GAKS: SpuGetAllKeysStatus and SpuRGetAllKeysStatus, the key
 * status of every voice, or of a range of them. */
#include "common.h"
#include "libspu_internal.h"

/* Nothing calls it. */
static s32 SpuRGetAllKeysStatus(s32 min, s32 max, s8 *status) {
    s32 voice;
    u16 volumex;

    if (min < 0) {
        min = 0;
    }
    if (min >= 24) {
        return -3;
    }
    if (max >= 24) {
        max = 23;
    }
    if (max < 0 || max < min) {
        return -3;
    }

    max++;
    for (voice = min; voice < max; voice++) {
        s32 bit;
        volumex = _spu_RXX->raw[(8 * voice) + 6];
        bit = _spu_keystat & (1 << voice);
        if (bit) {
            if (volumex != 0) {
                status[voice] = 1;
            } else {
                status[voice] = 3;
            }
        } else {
            if (volumex != 0) {
                status[voice] = 2;
            } else {
                status[voice] = 0;
            }
        }
    }

    return 0;
}

void SpuGetAllKeysStatus(u8 *status) {
    s32 limit = 24;
    s32 voice = 0;

    do {
        u16 volumex;
        s32 bit;
        volumex = _spu_RXX->raw[(8 * voice) + 6];
        bit = _spu_keystat & (1 << voice);
        if (bit) {
            if (volumex != 0) {
                status[voice] = 1;
            } else {
                status[voice] = 3;
            }
        } else if (volumex != 0) {
            status[voice] = 2;
        } else {
            status[voice] = 0;
        }
        voice++;
    } while (voice < limit);
}
