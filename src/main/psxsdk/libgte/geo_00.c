/* LIBGTE GEO_00: rsin, the sine of an angle of 4096 to the turn, as a number
 * with twelve fraction bits. */
#include "common.h"
#include <psxsdk/libgte.h>

extern s32 sin_1(s32);
extern s16 rsin_tbl[];

s32 rsin(s32 a) {
    s32 v;
    if (a < 0) {
        v = sin_1((-a) & 0xFFF);
        return -v;
    }
    return sin_1(a & 0xFFF);
}

/* The sine of an angle within one turn, from the table of the first quarter. */
s32 sin_1(s32 a) {
    if (a < 0x801) {
        if (a < 0x401) {
            return rsin_tbl[a];
        }
        return rsin_tbl[0x800 - a];
    }
    if (a < 0xC01) {
        return -rsin_tbl[a - 0x800];
    }
    return -rsin_tbl[0x1000 - a];
}
