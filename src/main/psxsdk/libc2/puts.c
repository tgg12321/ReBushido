/* LIBC2 PUTS: puts. */
#include "common.h"
#include <psxsdk/libc.h>

extern void putchar();

void puts(char *s) {
    char c;

    if (s == NULL) {
        s = "<NULL>";
    }
    while ((c = *s++) != 0) {
        putchar(c);
    }
}

/* Sony's stamp (the letters Ps and a build number) at the head of the
 * module's data; nothing reads it. */
u32 _puts_stamp[2] = {0x15027350, 0x0040849C};
