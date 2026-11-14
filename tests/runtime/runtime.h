#ifndef TEST_RUNTIME_H
#define TEST_RUNTIME_H

/* What a test program stands on (tests/README.md). The tests are 32-bit
 * programs, because the game keeps addresses in 32-bit integers, and are
 * linked with no C library, because a 32-bit one is not always installed:
 * so the few things a test needs from a library are here, written on the
 * Linux system calls. A test includes this after the game's file. */

/* Every test defines this. The program's exit code is 0 if no check
 * failed. */
extern void test_Main(void);

/* The program's arguments, which tests/run.py passes from the test's Python
 * side: test_Arg(0) is the first of them, NULL past the last. */
extern const char *test_Arg(int index);

/* printf, cut down: %d %u %x %X %s %c %%, with a width and a leading 0
 * (%04x, %6d). Each line a test prints is handed to its Python side. */
extern void test_Print(const char *format, ...);

/* A check that fails prints "FAIL file:line: ..." and is counted; the test
 * goes on. */
extern void test_Count(
    int passed, const char *file, int line, const char *format, ...);
#define CHECK(condition)                                                       \
    test_Count((condition) != 0, __FILE__, __LINE__, "%s", #condition)
#define CHECK_EQ(got, want)                                                    \
    test_CountEq((int)(got), (int)(want), __FILE__, __LINE__, #got)
extern void test_CountEq(
    int got, int want, const char *file, int line, const char *what);

/* `size` bytes of zeroed memory, never given back. */
extern void *test_Alloc(unsigned size);
/* The same at an address of the caller's choosing: the scratchpad, which
 * the game reaches by its PlayStation address. */
extern void *test_AllocAt(unsigned address, unsigned size);
/* A whole file, in memory of test_Alloc's, with a zero word after it; NULL
 * if it cannot be opened. */
extern void *test_ReadFile(const char *path, unsigned *size_out);

/* runtime.c also has memcpy, memmove, memset and memcmp, which the compiler
 * calls for a structure that is copied or cleared. They are not declared
 * here: Sony's libc.h declares them its own way. */

/* Copies and compares for a test's own use. */
extern void test_Copy(void *dest, const void *src, unsigned count);
extern int test_BytesEqual(const void *a, const void *b, unsigned count);
extern int test_StringsEqual(const char *a, const char *b);
/* The number a string of decimal digits spells, with or without a `-`. */
extern int test_ParseInt(const char *text);

#endif /* TEST_RUNTIME_H */
