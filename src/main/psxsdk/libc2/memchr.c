/* LIBC2 MEMCHR: memchr. */
#include "common.h"
#include <psxsdk/libc.h>

u8 *memchr(u8 *buf, s32 ch, s32 len) {
    if (buf == 0 || len <= 0)
        return 0;
    while (--len >= 0) {
        if (*buf++ == (ch & 0xFF))
            return buf - 1;
    }
    return 0;
}
