#ifndef _LIBC_H_
#define _LIBC_H_

/* The C library, as far as the game links it: what Sony declares in
 * ctype.h, memory.h, rand.h, setjmp.h, stdio.h and strings.h (Run-time
 * Library Release 4.0). Where a prototype is not Sony's, a note says what
 * Sony's is. */

#include "common.h"

/* ctype.h */
#define _U 0x01 /* upper case letter */
#define _L 0x02 /* lower case letter */
#define _N 0x04 /* digit */
#define _S                                                                     \
    0x08        /* space, tab, newline, vertical tab, formfeed, or             \
                   carriage return */
#define _P 0x10 /* punctuation character */
#define _C 0x20 /* control character or delete */
#define _X 0x40 /* hexadecimal digit [0-9a-fA-F]*/
#define _B 0x80 /* blank (space) */

extern u8 _ctype_[]; /* Sony: char */
extern char toupper(char);
extern char tolower(char);

/* memory.h. Sony leaves memcpy's parameters out, and has memchr take an
 * unsigned char. */
extern u8 *memcpy(u8 *, u8 *, s32);
extern void *memmove(u8 *, u8 *, s32);
extern u8 *memchr(u8 *, s32, s32);

/* rand.h */
#define RAND_MAX 32767

extern s32 rand(void);
extern void srand(u32);

/* setjmp.h */
#define JB_PC 0
#define JB_SP 1
#define JB_FP 2
#define JB_S0 3
#define JB_S1 4
#define JB_S2 5
#define JB_S3 6
#define JB_S4 7
#define JB_S5 8
#define JB_S6 9
#define JB_S7 10
#define JB_GP 11

#define JB_SIZE 12

typedef s32 jmp_buf[JB_SIZE];
extern s32 setjmp(jmp_buf);

/* stdio.h. Sony: int printf(char *fmt, ...); the library's own takes four
 * words, so the game calls it without a prototype. putchar is declared by
 * its callers, also without one. */
#define EOF (-1)

extern void printf();
extern s32 sprintf(char *buffer, char *fmt, ...);
extern void puts(char *);

/* strings.h. Sony leaves the parameters of both out; strcpy's are char *. */
extern u8 *strcpy(u8 *, u8 *);
extern s32 strlen(char *);

#endif /* _LIBC_H_ */
