/* LIBC2 PUTCHAR: putchar, which writes a character to the console and keeps
 * count of the column for its tabs. */
#include "common.h"
#include <psxsdk/libc.h>

#include <psxsdk/libapi.h>

static s32 column;

void putchar(s8 c) {
    u8 ch;
    s32 code;

    ch = c;
    code = c & 0xFF;
    switch (code) {
    case '\n':
        putchar('\r');
        column = 0;
        break;
    case '\t':
        for (;;) {
            putchar(' ');
            if ((column & 7) == 0)
                return;
        }
    default:
        if ((_ctype_ + 1)[code] & (_P | _U | _L | _N | _B)) {
            column += 1;
        }
        break;
    }
    write(1, &ch, 1);
}
