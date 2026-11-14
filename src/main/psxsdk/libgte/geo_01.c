/* LIBGTE GEO_01: rcos, the cosine of an angle of 4096 to the turn. */
#include "common.h"
#include <psxsdk/libgte.h>

extern s16 rsin_tbl[];

s32 rcos(s32 a) {
    if (a < 0) {
        a = -a;
    }
    a = a & 0xFFF;
    if (a < 0x801) {
        if (a < 0x401) {
            return rsin_tbl[0x400 - a];
        }
        return -rsin_tbl[a - 0x400];
    }
    if (a < 0xC01) {
        return -rsin_tbl[0xC00 - a];
    }
    return rsin_tbl[a - 0xC00];
}
