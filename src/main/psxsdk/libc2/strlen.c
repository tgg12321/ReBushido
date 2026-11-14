/* LIBC2 STRLEN: strlen. */
#include "common.h"
#include <psxsdk/libc.h>

s32 strlen(char *s) {
    s32 n = 0;
    if (!s) {
        return 0;
    }
    while (*s++ != 0) {
        n++;
    }
    return n;
}
