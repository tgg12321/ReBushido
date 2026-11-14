/* LIBC2 MEMCPY: memcpy. */
#include "common.h"
#include <psxsdk/libc.h>

u8 *memcpy(u8 *dst, u8 *src, s32 len) {
    u8 *ret;
    if (!dst) {
        return 0;
    }
    ret = dst;
    while (len > 0) {
        *dst = *src;
        src++;
        len--;
        dst++;
    }
    return ret;
}
