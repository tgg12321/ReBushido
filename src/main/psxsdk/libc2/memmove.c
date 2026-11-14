/* LIBC2 MEMMOVE: memmove. */
#include "common.h"
#include <psxsdk/libc.h>

void *memmove(u8 *dst, u8 *src, s32 n) {
    if (dst >= src) {
        while (n-- > 0) {
            dst[n] = src[n];
        }
    } else {
        while (n-- > 0) {
            *dst++ = *src++;
        }
    }
    return dst;
}
