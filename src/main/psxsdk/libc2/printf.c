/* LIBC2 PRINTF: printf, which hands its arguments to prnt. */
#include "common.h"
#include <psxsdk/libc.h>

/* s32 prnt(s32 fd, u8 *fmt, char *argp). Without a prototype here, as printf
 * passes its format as the word it arrived in. */
extern s32 prnt();

/* Written before <stdarg.h>: the three argument registers are stored after
 * the format by hand, to make one list of them and what is on the stack. */
void printf(s32 fmt, s32 a, s32 b, s32 c) {
    s32 *ap = &fmt;
    ap[1] = a;
    ap[2] = b;
    ap[3] = c;
    prnt(1, fmt, ap + 1);
}
