/* The native port on a C library (port_host.h): what host_linux.c asks
 * of the kernel itself, asked of the library here, for the build with a
 * window (docs/PORT.md, "The window"), which has one because SDL does.
 * Linux so far; nothing but the two pieces marked is Linux's or a
 * processor's. Windows has its own file, src/port/host_windows.c.
 *
 * The program starts at the library's main, which is in sdl_main.c. */
#if !defined(_WIN32)

#define _GNU_SOURCE
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <time.h>
#include <ucontext.h>
#include <unistd.h>

#include "port_host.h"

#define FILES_MAX 16
/* A file this large or larger has no size the port can count. */
#define TWO_GIGABYTES 0x7FFFFFFFL
#define SIGNAL_STACK_BYTES 0x10000

#ifndef MAP_FIXED_NOREPLACE
/* At the address asked or not at all: a kernel that does not know the
 * flag takes the address for a wish, and the answer is looked at. */
#define MAP_FIXED_NOREPLACE 0x100000
#endif

static FILE *g_port_posix_files[FILES_MAX];
static void (*g_port_posix_report)(const PortCrash *crash);

/* What the library keeps for its files is written out first. */
void port_HostExit(s32 code) {
    for (;;) {
        exit(code);
    }
}

void (*g_port_host_tee)(s32 stream, const char *bytes, u32 count);

void port_HostWrite(s32 stream, const char *bytes, u32 count) {
    FILE *out = stream == PORT_HOST_OUTPUT ? stdout : stderr;

    fwrite(bytes, 1, count, out);
    fflush(out);
    if (g_port_host_tee != NULL) {
        g_port_host_tee(stream, bytes, count);
    }
}

/* A place in the table for a file the library opened: its number, or -1
 * if it did not open or there is no place. */
static s32 port_PosixKeep(FILE *file) {
    s32 i;

    if (file == NULL) {
        return -1;
    }
    for (i = 0; i < FILES_MAX; i++) {
        if (g_port_posix_files[i] == NULL) {
            g_port_posix_files[i] = file;
            return i;
        }
    }
    fclose(file);
    return -1;
}

static FILE *port_PosixFile(s32 file) {
    return file >= 0 && file < FILES_MAX ? g_port_posix_files[file] : NULL;
}

s32 port_HostOpen(const char *path) {
    return port_PosixKeep(fopen(path, "rb"));
}

s32 port_HostCreate(const char *path) {
    return port_PosixKeep(fopen(path, "wb"));
}

void port_HostClose(s32 file) {
    if (port_PosixFile(file) != NULL) {
        fclose(g_port_posix_files[file]);
        g_port_posix_files[file] = NULL;
    }
}

u32 port_HostFileSize(s32 file) {
    FILE *stream = port_PosixFile(file);
    off_t size;

    if (stream == NULL || fseeko(stream, 0, SEEK_END) != 0) {
        return 0;
    }
    size = ftello(stream);
    return size < 0 || size >= TWO_GIGABYTES ? 0 : (u32)size;
}

u32 port_HostReadAt(s32 file, u32 offset, void *dest, u32 count) {
    FILE *stream = port_PosixFile(file);

    if (stream == NULL || fseeko(stream, (off_t)offset, SEEK_SET) != 0) {
        return 0;
    }
    return (u32)fread(dest, 1, count, stream);
}

s32 port_HostAppend(s32 file, const void *bytes, u32 count) {
    FILE *stream = port_PosixFile(file);

    return stream != NULL && fwrite(bytes, 1, count, stream) == count;
}

/* A file that is there already, read and written in place (port_host.h):
 * the memory card's image. */
s32 port_HostOpenReadWrite(const char *path) {
    return port_PosixKeep(fopen(path, "r+b"));
}

/* Writes all of it at `offset` (port_host.h). */
s32 port_HostWriteAt(s32 file, u32 offset, const void *bytes, u32 count) {
    FILE *stream = port_PosixFile(file);

    if (stream == NULL || fseeko(stream, (off_t)offset, SEEK_SET) != 0) {
        return 0;
    }
    return fwrite(bytes, 1, count, stream) == count;
}

/* What has been written, on the disc (port_host.h): the library's buffer
 * out first, then the system's. */
void port_HostFlush(s32 file) {
    FILE *stream = port_PosixFile(file);

    if (stream != NULL && fflush(stream) == 0) {
        fsync(fileno(stream));
    }
}

/* Whole pages at exactly `address`, or nothing (port_host.h). */
/* The clock that only goes forward (port_host.h). */
u32 port_HostTicks(void) {
    struct timespec now;

    clock_gettime(CLOCK_MONOTONIC, &now);
    return (u32)now.tv_sec * 10000000U + (u32)now.tv_nsec / 100U;
}

s32 port_HostMapAt(u32 address, u32 size) {
    void *wanted = (void *)(uintptr_t)address;
    void *got = mmap(wanted, size, PROT_READ | PROT_WRITE,
                     MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE, -1, 0);

    if (got == MAP_FAILED) {
        return 0;
    }
    if (got != wanted) {
        /* somewhere else: not what was asked */
        munmap(got, size);
        return 0;
    }
    return 1;
}

void *port_HostAllocate(u32 size) {
    void *got = mmap(
        NULL, size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);

    return got == MAP_FAILED ? NULL : got;
}

/* ---- faults ---- */

/* The system calls this for a fault: says what and where, and ends the
 * program without the library's leave-taking, which a broken program
 * may not survive. The registers are the processor's. */
static void port_PosixOnSignal(int number, siginfo_t *info, void *context) {
    const ucontext_t *registers = context;
    PortCrash crash;

    switch (number) {
    case SIGSEGV:
        crash.what = "memory that is not there was touched (SIGSEGV)";
        break;
    case SIGBUS:
        crash.what = "memory that cannot be reached was touched (SIGBUS)";
        break;
    case SIGFPE:
        crash.what = "a division by zero (SIGFPE)";
        break;
    default:
        crash.what = "what is not an instruction was run (SIGILL)";
        break;
    }
    crash.address = (PortAddress)info->si_addr;
#if defined(__i386__)
    crash.pc = (PortAddress)registers->uc_mcontext.gregs[REG_EIP];
    crash.frame = (PortAddress)registers->uc_mcontext.gregs[REG_EBP];
    crash.stack = (PortAddress)registers->uc_mcontext.gregs[REG_ESP];
#elif defined(__x86_64__)
    crash.pc = (PortAddress)registers->uc_mcontext.gregs[REG_RIP];
    crash.frame = (PortAddress)registers->uc_mcontext.gregs[REG_RBP];
    crash.stack = (PortAddress)registers->uc_mcontext.gregs[REG_RSP];
#else
#error "host_posix.c: this processor's registers are not written here"
#endif
    g_port_posix_report(&crash);
    fflush(stdout);
    _exit(PORT_EXIT_CRASH);
}

/* The four faults, on a stack of the handler's own: the fault may be
 * the game's stack running out (port_host.h). */
void port_HostCatchCrashes(void (*report)(const PortCrash *crash)) {
    static const int signals[] = {SIGSEGV, SIGBUS, SIGFPE, SIGILL};
    static char signal_stack[SIGNAL_STACK_BYTES];
    struct sigaction action;
    stack_t stack;
    size_t i;

    g_port_posix_report = report;
    stack.ss_sp = signal_stack;
    stack.ss_flags = 0;
    stack.ss_size = sizeof(signal_stack);
    sigaltstack(&stack, NULL);

    action.sa_sigaction = port_PosixOnSignal;
    /* a second fault, in the report itself, ends the program as the
     * system would have */
    action.sa_flags = SA_SIGINFO | SA_ONSTACK | SA_RESETHAND;
    sigemptyset(&action.sa_mask);
    for (i = 0; i < sizeof(signals) / sizeof(signals[0]); i++) {
        sigaction(signals[i], &action, NULL);
    }
}

/* The stack pointer to the top of the memory given, no frame above the
 * function's, and the call. The instructions are the processor's. */
void port_HostCallOnStack(void (*function)(void), u32 stack_top) {
    PortAddress top = stack_top;

#if defined(__i386__)
    __asm__ volatile(
        "movl %0, %%esp\n"
        "xorl %%ebp, %%ebp\n"
        "call *%1\n"
        :
        : "r"(top), "r"(function)
        : "memory");
#elif defined(__x86_64__)
    __asm__ volatile(
        "movq %0, %%rsp\n"
        "xorl %%ebp, %%ebp\n"
        "call *%1\n"
        :
        : "r"(top), "r"(function)
        : "memory");
#else
#error "host_posix.c: this processor's stack switch is not written here"
#endif
    for (;;) {
        port_HostExit(1);
    }
}

#endif /* !_WIN32 */
