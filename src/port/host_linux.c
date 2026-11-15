/* The native port on Linux (port_host.h): a 32-bit x86 program that asks
 * the kernel directly for everything, with no C library, as the host
 * tests do (tests/runtime/runtime.c): a 32-bit C library is not always
 * installed, and the port so far needs a dozen calls. When the port
 * takes a library for its window and sound (docs/PORT.md) this file is
 * the one that changes. */
#if defined(__linux__) && defined(__i386__)

#include "common.h"
#include "port_host.h"
#include "bb2.h"

/* ---- system calls (32-bit x86 Linux: `int $0x80`) ---- */

enum {
    LINUX_EXIT = 1,
    LINUX_READ = 3,
    LINUX_WRITE = 4,
    LINUX_OPEN = 5,
    LINUX_CLOSE = 6,
    LINUX_LSEEK = 19,
    LINUX_MUNMAP = 91,
    LINUX_FSYNC = 118,
    LINUX_RT_SIGACTION = 174,
    LINUX_SIGALTSTACK = 186,
    LINUX_MMAP2 = 192, /* the offset in pages, the six arguments in registers */
};

#define LINUX_SEEK_SET 0
#define LINUX_SEEK_END 2
#define LINUX_O_RDONLY 0
#define LINUX_O_WRONLY 1
#define LINUX_O_RDWR 2
#define LINUX_O_CREAT 0x40
#define LINUX_O_TRUNC 0x200
#define LINUX_O_LARGEFILE 0x8000
#define LINUX_FILE_MODE 0644
#define LINUX_PROT_READ_WRITE 3
#define LINUX_MAP_PRIVATE 0x02
#define LINUX_MAP_ANONYMOUS 0x20
/* At the address asked or not at all: a kernel that does not know the
 * flag (before 4.17) takes the address for a wish, and the answer is
 * looked at. */
#define LINUX_MAP_FIXED_NOREPLACE 0x100000
#define LINUX_PAGE_SIZE 0x1000U
/* The kernel's answers from -1 to -4095 are errors. */
#define LINUX_IS_ERROR(result) ((u32)(result) > 0xFFFFF000U)

#define LINUX_SIGILL 4
#define LINUX_SIGBUS 7
#define LINUX_SIGFPE 8
#define LINUX_SIGSEGV 11
#define LINUX_SA_SIGINFO 0x00000004U
#define LINUX_SA_ONSTACK 0x08000000U
#define LINUX_SA_RESETHAND 0x80000000U
#define LINUX_SIGNAL_SET_BYTES 8

static s32 port_LinuxCall(s32 number, s32 first, s32 second, s32 third) {
    s32 result;

    __asm__ volatile("int $0x80"
                     : "=a"(result)
                     : "a"(number), "b"(first), "c"(second), "d"(third)
                     : "memory");
    return result;
}

/* mmap2 takes six. The sixth goes in %ebp, which holds this function's
 * frame, so it is put there by hand around the call. */
static s32 port_LinuxMap(u32 address, u32 size, s32 flags) {
    s32 result;
    s32 no_file = -1;

    __asm__ volatile(
        "pushl %%ebp\n"
        "xorl %%ebp, %%ebp\n"
        "int $0x80\n"
        "popl %%ebp"
        : "=a"(result)
        : "a"(LINUX_MMAP2), "b"(address), "c"(size), "d"(LINUX_PROT_READ_WRITE),
          "S"(flags), "D"(no_file)
        : "memory");
    return result;
}

void port_HostExit(s32 code) {
    for (;;) {
        port_LinuxCall(LINUX_EXIT, code, 0, 0);
    }
}

/* All of it, however many pieces the kernel takes it in. */
void port_HostWrite(s32 stream, const char *bytes, u32 count) {
    u32 done = 0;

    while (done < count) {
        s32 wrote = port_LinuxCall(
            LINUX_WRITE, stream, (s32)(bytes + done), (s32)(count - done));
        if (wrote <= 0) {
            return;
        }
        done += (u32)wrote;
    }
}

s32 port_HostOpen(const char *path) {
    s32 file = port_LinuxCall(
        LINUX_OPEN, (s32)path, LINUX_O_RDONLY | LINUX_O_LARGEFILE, 0);

    return LINUX_IS_ERROR(file) ? -1 : file;
}

s32 port_HostCreate(const char *path) {
    s32 file = port_LinuxCall(
        LINUX_OPEN, (s32)path, LINUX_O_WRONLY | LINUX_O_CREAT | LINUX_O_TRUNC,
        LINUX_FILE_MODE);

    return LINUX_IS_ERROR(file) ? -1 : file;
}

void port_HostClose(s32 file) { port_LinuxCall(LINUX_CLOSE, file, 0, 0); }

u32 port_HostFileSize(s32 file) {
    s32 size = port_LinuxCall(LINUX_LSEEK, file, 0, LINUX_SEEK_END);

    return LINUX_IS_ERROR(size) ? 0 : (u32)size;
}

/* Reads until it has `count` bytes or the file ends (port_host.h). */
u32 port_HostReadAt(s32 file, u32 offset, void *dest, u32 count) {
    u32 done = 0;

    if (LINUX_IS_ERROR(
            port_LinuxCall(LINUX_LSEEK, file, (s32)offset, LINUX_SEEK_SET))) {
        return 0;
    }
    while (done < count) {
        s32 got = port_LinuxCall(
            LINUX_READ, file, (s32)((char *)dest + done), (s32)(count - done));
        if (got <= 0) {
            break;
        }
        done += (u32)got;
    }
    return done;
}

/* Writes all of it at the file's end (port_host.h). */
s32 port_HostAppend(s32 file, const void *bytes, u32 count) {
    u32 done = 0;

    while (done < count) {
        s32 wrote =
            port_LinuxCall(LINUX_WRITE, file, (s32)((const char *)bytes + done),
                           (s32)(count - done));
        if (wrote <= 0) {
            return 0;
        }
        done += (u32)wrote;
    }
    return 1;
}

/* A file that is there already, read and written in place (port_host.h). */
s32 port_HostOpenReadWrite(const char *path) {
    s32 file = port_LinuxCall(
        LINUX_OPEN, (s32)path, LINUX_O_RDWR | LINUX_O_LARGEFILE, 0);

    return LINUX_IS_ERROR(file) ? -1 : file;
}

/* Writes all of it at `offset` (port_host.h). */
s32 port_HostWriteAt(s32 file, u32 offset, const void *bytes, u32 count) {
    u32 done = 0;

    if (LINUX_IS_ERROR(
            port_LinuxCall(LINUX_LSEEK, file, (s32)offset, LINUX_SEEK_SET))) {
        return 0;
    }
    while (done < count) {
        s32 wrote =
            port_LinuxCall(LINUX_WRITE, file, (s32)((const char *)bytes + done),
                           (s32)(count - done));
        if (wrote <= 0) {
            return 0;
        }
        done += (u32)wrote;
    }
    return 1;
}

void port_HostFlush(s32 file) { port_LinuxCall(LINUX_FSYNC, file, 0, 0); }

/* Whole pages at exactly `address`, or nothing (port_host.h). */
s32 port_HostMapAt(u32 address, u32 size) {
    s32 got;

    size = (size + LINUX_PAGE_SIZE - 1) & ~(LINUX_PAGE_SIZE - 1);
    got = port_LinuxMap(
        address, size,
        LINUX_MAP_PRIVATE | LINUX_MAP_ANONYMOUS | LINUX_MAP_FIXED_NOREPLACE);
    if (LINUX_IS_ERROR(got)) {
        return 0;
    }
    if ((u32)got != address) {
        /* somewhere else: not what was asked */
        port_LinuxCall(LINUX_MUNMAP, got, (s32)size, 0);
        return 0;
    }
    return 1;
}

void *port_HostAllocate(u32 size) {
    s32 got;

    size = (size + LINUX_PAGE_SIZE - 1) & ~(LINUX_PAGE_SIZE - 1);
    got = port_LinuxMap(0, size, LINUX_MAP_PRIVATE | LINUX_MAP_ANONYMOUS);
    return LINUX_IS_ERROR(got) ? NULL : (void *)got;
}

/* ---- faults ---- */

/* What the kernel hands a handler that asks for it (SA_SIGINFO), as far
 * as this reads it: the faulting address in the first, the registers in
 * the second. */
typedef struct {
    s32 number;
    s32 error;
    s32 code;
    u32 address;
} LinuxSignalInfo;

typedef struct {
    u32 flags;
    u32 link;
    u32 stack_base;
    s32 stack_flags;
    u32 stack_size;
    u32 segments[4]; /* gs, fs, es, ds */
    u32 edi, esi, ebp, esp, ebx, edx, ecx, eax;
    u32 trap, error;
    u32 eip;
} LinuxSignalContext;

typedef struct {
    void (*handler)(s32 number, LinuxSignalInfo *info, void *context);
    u32 flags;
    void (*restorer)(void);
    u8 mask[LINUX_SIGNAL_SET_BYTES];
} LinuxSignalAction;

typedef struct {
    void *base;
    s32 flags;
    u32 size;
} LinuxSignalStack;

/* The handler runs on a stack of its own: the fault may be the game's
 * stack running out. */
static u8 g_port_linux_signal_stack[0x8000];
static void (*g_port_linux_report)(const PortCrash *crash);

/* The kernel calls this for a fault: says what and where, and ends the
 * program. */
static void port_LinuxOnSignal(
    s32 number, LinuxSignalInfo *info, void *context) {
    LinuxSignalContext *registers = context;
    PortCrash crash;

    switch (number) {
    case LINUX_SIGSEGV:
        crash.what = "memory that is not there was touched (SIGSEGV)";
        break;
    case LINUX_SIGBUS:
        crash.what = "memory that cannot be reached was touched (SIGBUS)";
        break;
    case LINUX_SIGFPE:
        crash.what = "a division by zero (SIGFPE)";
        break;
    default:
        crash.what = "what is not an instruction was run (SIGILL)";
        break;
    }
    crash.address = info->address;
    crash.pc = registers->eip;
    crash.frame = registers->ebp;
    crash.stack = registers->esp;
    g_port_linux_report(&crash);
    port_HostExit(PORT_EXIT_CRASH);
}

/* Asks the kernel to call port_LinuxOnSignal for the four faults, on a
 * stack of its own (port_host.h). */
void port_HostCatchCrashes(void (*report)(const PortCrash *crash)) {
    static const s32 signals[] = {
        LINUX_SIGSEGV,
        LINUX_SIGBUS,
        LINUX_SIGFPE,
        LINUX_SIGILL,
    };
    LinuxSignalStack stack;
    LinuxSignalAction action;
    u32 i;

    g_port_linux_report = report;
    stack.base = g_port_linux_signal_stack;
    stack.flags = 0;
    stack.size = sizeof(g_port_linux_signal_stack);
    port_LinuxCall(LINUX_SIGALTSTACK, (s32)&stack, 0, 0);

    action.handler = port_LinuxOnSignal;
    /* a second fault, in the report itself, ends the program as the
     * system would have */
    action.flags = LINUX_SA_SIGINFO | LINUX_SA_ONSTACK | LINUX_SA_RESETHAND;
    action.restorer = NULL;
    for (i = 0; i < sizeof(action.mask); i++) {
        action.mask[i] = 0;
    }
    for (i = 0; i < sizeof(signals) / sizeof(signals[0]); i++) {
        /* rt_sigaction's fourth argument, the size of the mask, is in
         * %esi */
        s32 result;

        __asm__ volatile("int $0x80"
                         : "=a"(result)
                         : "a"(LINUX_RT_SIGACTION), "b"(signals[i]),
                           "c"(&action), "d"(0), "S"(LINUX_SIGNAL_SET_BYTES)
                         : "memory");
        (void)result;
    }
}

/* The stack pointer to the top of the memory given, no frame above the
 * function's, and the call. */
void port_HostCallOnStack(void (*function)(void), u32 stack_top) {
    __asm__ volatile(
        "movl %0, %%esp\n"
        "xorl %%ebp, %%ebp\n"
        "call *%1\n"
        :
        : "r"(stack_top), "r"(function)
        : "memory");
    for (;;) {
        port_HostExit(1);
    }
}

/* ---- start-up ---- */

/* The kernel starts the program here with the argument count and the
 * arguments on the stack. */
static void port_LinuxStart(s32 *stack) __attribute__((used, noreturn));

__asm__(".text\n"
        ".globl _start\n"
        "_start:\n"
        "    xorl %ebp, %ebp\n"
        "    movl %esp, %eax\n"
        "    andl $-16, %esp\n"
        "    subl $12, %esp\n"
        "    pushl %eax\n"
        "    call port_LinuxStart\n"
        "    hlt\n");

static void port_LinuxStart(s32 *stack) {
    port_HostExit(port_Main(stack[0], (char **)(stack + 1)));
}

#endif /* __linux__ && __i386__ */
