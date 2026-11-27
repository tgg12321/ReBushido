/* The test programs' start-up and their stand-in for a C library
 * (runtime.h): a Linux program that asks the kernel directly for the four
 * things a test needs: to write its output, to read a file, to get memory
 * and to exit. A test is a 32-bit program; the port's tests are built as
 * 64-bit programs as well (tests/run.py), so the kernel is called both
 * ways here. Nothing here divides a 64-bit number, which would need the
 * compiler's own 32-bit library. */
#include "runtime.h"

/* What the kernel takes and gives: a number as wide as a pointer. */
typedef __INTPTR_TYPE__ TestWord;

#if defined(__x86_64__)

/* ---- system calls (64-bit x86 Linux: `syscall`) ---- */

enum {
    SYS_READ = 0,
    SYS_WRITE = 1,
    SYS_OPEN = 2,
    SYS_CLOSE = 3,
    SYS_LSEEK = 8,
    SYS_MMAP = 9,
    SYS_EXIT = 60,
    SYS_CLOCK_GETTIME = 228,
};

/* Memory below 2 GB: the game keeps addresses in 32-bit numbers. */
#define MAP_LOW 0x40

static TestWord test_Syscall6(TestWord number, TestWord a, TestWord b,
                              TestWord c, TestWord d, TestWord e, TestWord f) {
    register TestWord in_r10 __asm__("r10") = d;
    register TestWord in_r8 __asm__("r8") = e;
    register TestWord in_r9 __asm__("r9") = f;
    TestWord result;

    __asm__ volatile("syscall"
                     : "=a"(result)
                     : "a"(number), "D"(a), "S"(b), "d"(c), "r"(in_r10),
                       "r"(in_r8), "r"(in_r9)
                     : "rcx", "r11", "memory");
    return result;
}

static TestWord test_Syscall(
    TestWord number, TestWord a, TestWord b, TestWord c) {
    return test_Syscall6(number, a, b, c, 0, 0, 0);
}

#else

/* ---- system calls (32-bit x86 Linux: `int $0x80`) ---- */

enum {
    SYS_EXIT = 1,
    SYS_READ = 3,
    SYS_WRITE = 4,
    SYS_OPEN = 5,
    SYS_CLOSE = 6,
    SYS_LSEEK = 19,
    SYS_MMAP = 90, /* the old one, which takes its six arguments in memory */
    SYS_CLOCK_GETTIME = 265,
};

static TestWord test_Syscall(
    TestWord number, TestWord a, TestWord b, TestWord c) {
    TestWord result;

    __asm__ volatile("int $0x80"
                     : "=a"(result)
                     : "a"(number), "b"(a), "c"(b), "d"(c)
                     : "memory");
    return result;
}

#endif

#define STDOUT 1
#define SEEK_SET 0
#define SEEK_END 2
#define PROT_READ_WRITE 3
#define CLOCK_MONOTONIC 1
#define MAP_PRIVATE 0x02
#define MAP_FIXED 0x10
#define MAP_ANONYMOUS 0x20
#define PAGE_SIZE 0x1000
/* The stack a 64-bit test runs on. */
#define STACK_SIZE 0x400000

/* ---- output, kept until there is a buffer of it ---- */

static char g_test_output[0x2000];
static unsigned g_test_output_used;

static void test_Flush(void) {
    unsigned done = 0;

    while (done < g_test_output_used) {
        TestWord wrote =
            test_Syscall(SYS_WRITE, STDOUT, (TestWord)(g_test_output + done),
                         (TestWord)(g_test_output_used - done));
        if (wrote <= 0) {
            break;
        }
        done += (unsigned)wrote;
    }
    g_test_output_used = 0;
}

static void test_PutChar(char c) {
    if (g_test_output_used == sizeof(g_test_output)) {
        test_Flush();
    }
    g_test_output[g_test_output_used++] = c;
}

/* A number in `base`, at least `width` characters wide. */
static void test_PutNumber(unsigned value, unsigned base, int negative,
                           int width, char pad, int upper) {
    char digits[12];
    int count = 0;
    const char *alphabet = upper ? "0123456789ABCDEF" : "0123456789abcdef";

    do {
        digits[count++] = alphabet[value % base];
        value /= base;
    } while (value != 0);
    if (negative && pad == '0') {
        test_PutChar('-');
    }
    for (width -= count + negative; width > 0; width--) {
        test_PutChar(pad);
    }
    if (negative && pad != '0') {
        test_PutChar('-');
    }
    while (count > 0) {
        test_PutChar(digits[--count]);
    }
}

static void test_PrintArgs(const char *format, __builtin_va_list args) {
    for (; *format; format++) {
        char pad = ' ';
        int width = 0;

        if (*format != '%') {
            test_PutChar(*format);
            continue;
        }
        format++;
        if (*format == '0') {
            pad = '0';
            format++;
        }
        while (*format >= '0' && *format <= '9') {
            width = width * 10 + (*format++ - '0');
        }
        switch (*format) {
        case 'd': {
            int value = __builtin_va_arg(args, int);
            if (value < 0) {
                test_PutNumber(0U - (unsigned)value, 10, 1, width, pad, 0);
            } else {
                test_PutNumber((unsigned)value, 10, 0, width, pad, 0);
            }
            break;
        }
        case 'u':
            test_PutNumber(
                __builtin_va_arg(args, unsigned), 10, 0, width, pad, 0);
            break;
        case 'x':
            test_PutNumber(
                __builtin_va_arg(args, unsigned), 16, 0, width, pad, 0);
            break;
        case 'X':
            test_PutNumber(
                __builtin_va_arg(args, unsigned), 16, 0, width, pad, 1);
            break;
        case 's': {
            const char *text = __builtin_va_arg(args, const char *);
            while (*text) {
                test_PutChar(*text++);
            }
            break;
        }
        case 'c':
            test_PutChar((char)__builtin_va_arg(args, int));
            break;
        case '%':
            test_PutChar('%');
            break;
        default: /* not a format this printf has: show it as it is */
            test_PutChar('%');
            test_PutChar(*format);
            break;
        }
    }
}

/* The monotonic clock, in microseconds (runtime.h). */
unsigned test_Microseconds(void) {
    /* a timespec: seconds and nanoseconds, each a word of the program's */
    TestWord now[2] = {0, 0};

    test_Syscall(SYS_CLOCK_GETTIME, CLOCK_MONOTONIC, (TestWord)now, 0);
    return (unsigned)now[0] * 1000000U + (unsigned)now[1] / 1000U;
}

void test_Print(const char *format, ...) {
    __builtin_va_list args;

    __builtin_va_start(args, format);
    test_PrintArgs(format, args);
    __builtin_va_end(args);
}

/* ---- checks ---- */

static unsigned g_test_checks;
static unsigned g_test_failures;
/* Past this many, a failing test only counts: one wrong table is thousands
 * of lines. */
#define FAILURES_SHOWN 20

void test_Count(
    int passed, const char *file, int line, const char *format, ...) {
    __builtin_va_list args;

    g_test_checks++;
    if (passed) {
        return;
    }
    g_test_failures++;
    if (g_test_failures > FAILURES_SHOWN) {
        return;
    }
    test_Print("FAIL %s:%d: ", file, line);
    __builtin_va_start(args, format);
    test_PrintArgs(format, args);
    __builtin_va_end(args);
    test_PutChar('\n');
}

void test_CountEq(
    int got, int want, const char *file, int line, const char *what) {
    test_Count(got == want, file, line, "%s is %d (0x%x), not %d (0x%x)", what,
               got, got, want, want);
}

/* ---- memory and files ---- */

static void *test_Map(unsigned address, unsigned size, int flags) {
    TestWord result;
#if defined(__x86_64__)
    result = test_Syscall6(
        SYS_MMAP, (TestWord)address,
        (TestWord)((size + PAGE_SIZE - 1) & ~(unsigned)(PAGE_SIZE - 1)),
        PROT_READ_WRITE,
        MAP_PRIVATE | MAP_ANONYMOUS | flags | (address ? 0 : MAP_LOW), -1, 0);
#else
    /* the old mmap's arguments: address, length, protection, flags, file,
     * offset */
    int request[6];

    request[0] = (int)address;
    request[1] = (int)((size + PAGE_SIZE - 1) & ~(unsigned)(PAGE_SIZE - 1));
    request[2] = PROT_READ_WRITE;
    request[3] = MAP_PRIVATE | MAP_ANONYMOUS | flags;
    request[4] = -1;
    request[5] = 0;
    result = test_Syscall(SYS_MMAP, (TestWord)request, 0, 0);
#endif
    /* an error is a small negative number */
    if ((unsigned __INTPTR_TYPE__)result > (unsigned __INTPTR_TYPE__) - 4096) {
        test_Print("FAIL the kernel gave no memory (%d bytes at 0x%x)\n",
                   (int)size, address);
        test_Flush();
        test_Syscall(SYS_EXIT, 2, 0, 0);
    }
    return (void *)result;
}

void *test_Alloc(unsigned size) { return test_Map(0, size, 0); }

void *test_AllocAt(unsigned address, unsigned size) {
    return test_Map(address, size, MAP_FIXED);
}

void *test_ReadFile(const char *path, unsigned *size_out) {
    int file = (int)test_Syscall(SYS_OPEN, (TestWord)path, 0, 0);
    int size;
    int done = 0;
    char *contents;

    if (file < 0) {
        return (void *)0;
    }
    size = (int)test_Syscall(SYS_LSEEK, file, 0, SEEK_END);
    test_Syscall(SYS_LSEEK, file, 0, SEEK_SET);
    contents = test_Alloc((unsigned)size + 4);
    while (done < size) {
        int got = (int)test_Syscall(
            SYS_READ, file, (TestWord)(contents + done), size - done);
        if (got <= 0) {
            break;
        }
        done += got;
    }
    test_Syscall(SYS_CLOSE, file, 0, 0);
    if (size_out) {
        *size_out = (unsigned)done;
    }
    return contents;
}

/* ---- what the compiler and the tests want of a library ---- */

void *memcpy(void *dest, const void *src, unsigned count) {
    char *to = dest;
    const char *from = src;

    while (count-- != 0) {
        *to++ = *from++;
    }
    return dest;
}

void *memmove(void *dest, const void *src, unsigned count) {
    char *to = dest;
    const char *from = src;

    if (to <= from) {
        while (count-- != 0) {
            *to++ = *from++;
        }
    } else {
        while (count-- != 0) {
            to[count] = from[count];
        }
    }
    return dest;
}

void *memset(void *dest, int value, unsigned count) {
    char *to = dest;

    while (count-- != 0) {
        *to++ = (char)value;
    }
    return dest;
}

int memcmp(const void *a, const void *b, unsigned count) {
    const unsigned char *left = a;
    const unsigned char *right = b;

    for (; count-- != 0; left++, right++) {
        if (*left != *right) {
            return *left - *right;
        }
    }
    return 0;
}

void test_Copy(void *dest, const void *src, unsigned count) {
    memmove(dest, src, count);
}

int test_BytesEqual(const void *a, const void *b, unsigned count) {
    return memcmp(a, b, count) == 0;
}

int test_StringsEqual(const char *a, const char *b) {
    while (*a && *a == *b) {
        a++;
        b++;
    }
    return *a == *b;
}

int test_ParseInt(const char *text) {
    int negative = *text == '-';
    int value = 0;

    for (text += negative; *text >= '0' && *text <= '9'; text++) {
        value = value * 10 + (*text - '0');
    }
    return negative ? -value : value;
}

/* ---- start-up ---- */

static int g_test_arg_count;
static char **g_test_args;

const char *test_Arg(int index) {
    if (index < 0 || index + 1 >= g_test_arg_count) {
        return (void *)0;
    }
    return g_test_args[index + 1];
}

/* The kernel starts the program here with the argument count and the
 * arguments on the stack. */
__asm__(".text\n"
        ".globl _start\n"
        "_start:\n"
#if defined(__x86_64__)
        "    xorl %ebp, %ebp\n"
        "    movq %rsp, %rdi\n"
        "    andq $-16, %rsp\n"
        "    call test_Start\n"
#else
        "    xorl %ebp, %ebp\n"
        "    movl %esp, %eax\n"
        "    andl $-16, %esp\n"
        "    subl $12, %esp\n"
        "    pushl %eax\n"
        "    call test_Start\n"
#endif
        "    hlt\n");

/* The test, and the program's end. */
static void test_Run(void) {
    test_Main();
    /* the last line: tests/run.py reads the counts from it */
    test_Print("checks %u failed %u\n", g_test_checks, g_test_failures);
    test_Flush();
    test_Syscall(SYS_EXIT, g_test_failures != 0, 0, 0);
}

void test_Start(TestWord *stack) {
    g_test_arg_count = (int)stack[0];
    g_test_args = (char **)(stack + 1);
#if defined(__x86_64__)
    {
        /* A 64-bit program's stack is far above 4 GB, and a test hands
         * the game the addresses of its locals, which the game keeps in
         * 32 bits: the test runs on a stack of its own, low down, as the
         * game does in the port (docs/PORT.md, "64 bits"). */
        char *low = test_Map(0, STACK_SIZE, 0);

        __asm__ volatile(
            "movq %0, %%rsp\n"
            "xorl %%ebp, %%ebp\n"
            "call *%1\n"
            :
            : "r"(low + STACK_SIZE), "r"(test_Run)
            : "memory");
    }
#else
    test_Run();
#endif
}
