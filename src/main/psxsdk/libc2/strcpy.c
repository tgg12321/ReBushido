/* LIBC2 STRCPY: strcpy. */
#include "common.h"
#include <psxsdk/libc.h>

u8 *strcpy(u8 *dst, u8 *src) {
    u8 *ret;
    if (!dst) {
        return 0;
    }
    if (!src) {
        return 0;
    }
    ret = dst;
    while ((*dst++ = *src++) != 0) {
    }
    return ret;
}
