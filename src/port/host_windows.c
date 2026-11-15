/* The native port on Windows (port_host.h). WRITTEN, COMPILED AND NOT YET
 * LINKED OR RUN: the port is built and tested on Linux so far
 * (host_linux.c), and this file is what the same boundary is on Windows,
 * to be tried when the port takes a window (docs/PORT.md, "What is
 * left"). It compiles to nothing anywhere else; MinGW's compiler makes an
 * object of it for 64 bits and for 32 with no warning, which is as far
 * as it has been taken.
 *
 * It asks only kernel32 and is started at port_WindowsStart (the linker's
 * /ENTRY, or -e with MinGW), with no C runtime's start-up: the game's own
 * function is called `main`, which a C runtime would take for the
 * program's. The program, of 64-bit pointers or of 32-bit ones, has to be
 * linked at a fixed address below 2 GB with no address randomisation, so
 * that its own variables are where a 32-bit number reaches them
 * (docs/PORT.md, "64 bits") and nothing of the system's is where the
 * PlayStation's memory goes; if something is, it says so and stops, as on
 * Linux.
 *
 * Known and not done: the system looks for a fault's handler between the
 * limits of the thread's stack as it knows them, and the game runs on a
 * stack of the port's own (port_HostCallOnStack), which it has not been
 * told of. */
#if defined(_WIN32)

#include <windows.h>

#include "port_host.h"

#define FILES_MAX 16
/* A file this large or larger has no size the port can count. */
#define TWO_GIGABYTES 0x80000000LL
#define ARGUMENTS_MAX 32
/* Windows gives memory at an address in blocks of this size. */
#define BLOCK_SIZE 0x10000U
#define BLOCKS_MAX 8
/* What a called function may write above its return address, by the
 * 64-bit calling convention. */
#define SHADOW_BYTES 32

static HANDLE g_port_windows_files[FILES_MAX];
/* The blocks port_HostMapAt has been given: where each starts and ends. */
static u32 g_port_windows_blocks[BLOCKS_MAX][2];
static s32 g_port_windows_block_count;
static void (*g_port_windows_report)(const PortCrash *crash);

void port_HostExit(s32 code) {
    for (;;) {
        ExitProcess((UINT)code);
    }
}

void port_HostWrite(s32 stream, const char *bytes, u32 count) {
    HANDLE out = GetStdHandle(
        stream == PORT_HOST_OUTPUT ? STD_OUTPUT_HANDLE : STD_ERROR_HANDLE);
    DWORD wrote;

    WriteFile(out, bytes, count, &wrote, NULL);
}

/* A place in the table for a file the system opened: its number, or -1
 * if it did not open or there is no place. */
static s32 port_WindowsKeep(HANDLE file) {
    s32 i;

    if (file == INVALID_HANDLE_VALUE) {
        return -1;
    }
    for (i = 0; i < FILES_MAX; i++) {
        if (g_port_windows_files[i] == NULL) {
            g_port_windows_files[i] = file;
            return i;
        }
    }
    CloseHandle(file);
    return -1;
}

s32 port_HostOpen(const char *path) {
    return port_WindowsKeep(
        CreateFileA(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING,
                    FILE_ATTRIBUTE_NORMAL, NULL));
}

s32 port_HostCreate(const char *path) {
    return port_WindowsKeep(
        CreateFileA(path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS,
                    FILE_ATTRIBUTE_NORMAL, NULL));
}

void port_HostClose(s32 file) {
    if (file >= 0 && file < FILES_MAX && g_port_windows_files[file] != NULL) {
        CloseHandle(g_port_windows_files[file]);
        g_port_windows_files[file] = NULL;
    }
}

u32 port_HostFileSize(s32 file) {
    LARGE_INTEGER size;

    if (!GetFileSizeEx(g_port_windows_files[file], &size) ||
        size.QuadPart >= TWO_GIGABYTES) {
        return 0;
    }
    return (u32)size.QuadPart;
}

u32 port_HostReadAt(s32 file, u32 offset, void *dest, u32 count) {
    LARGE_INTEGER at;
    DWORD got = 0;

    at.QuadPart = offset;
    if (!SetFilePointerEx(g_port_windows_files[file], at, NULL, FILE_BEGIN) ||
        !ReadFile(g_port_windows_files[file], dest, count, &got, NULL)) {
        return 0;
    }
    return got;
}

s32 port_HostAppend(s32 file, const void *bytes, u32 count) {
    DWORD wrote = 0;

    return WriteFile(g_port_windows_files[file], bytes, count, &wrote, NULL) &&
           wrote == count;
}

/* Memory at an address: Windows gives it in blocks of 64 KB, so the
 * scratchpad's kilobyte takes one, and the sound chip's registers, a page
 * further on, are in the block the scratchpad was given. */
s32 port_HostMapAt(u32 address, u32 size) {
    u32 start = address & ~(BLOCK_SIZE - 1);
    u32 end = (address + size + BLOCK_SIZE - 1) & ~(BLOCK_SIZE - 1);
    void *place = (void *)(PortAddress)start;
    s32 i;

    for (i = 0; i < g_port_windows_block_count; i++) {
        if (start >= g_port_windows_blocks[i][0] &&
            end <= g_port_windows_blocks[i][1]) {
            return 1;
        }
    }
    if (g_port_windows_block_count == BLOCKS_MAX ||
        VirtualAlloc(place, end - start, MEM_RESERVE | MEM_COMMIT,
                     PAGE_READWRITE) != place) {
        return 0;
    }
    g_port_windows_blocks[g_port_windows_block_count][0] = start;
    g_port_windows_blocks[g_port_windows_block_count][1] = end;
    g_port_windows_block_count++;
    return 1;
}

void *port_HostAllocate(u32 size) {
    return VirtualAlloc(NULL, size, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
}

/* As on Linux, for a compiler that takes GCC's assembly; not run. */
void port_HostCallOnStack(void (*function)(void), u32 stack_top) {
#if defined(_WIN64)
    __asm__ volatile(
        "movq %0, %%rsp\n"
        "xorl %%ebp, %%ebp\n"
        "call *%1\n"
        :
        : "r"((PortAddress)stack_top - SHADOW_BYTES), "r"(function)
        : "memory");
#else
    __asm__ volatile(
        "movl %0, %%esp\n"
        "xorl %%ebp, %%ebp\n"
        "call *%1\n"
        :
        : "r"(stack_top), "r"(function)
        : "memory");
#endif
    for (;;) {
        port_HostExit(1);
    }
}

/* The system calls this for a fault nothing else took: says what and
 * where, and ends the program. */
static LONG WINAPI port_WindowsOnFault(EXCEPTION_POINTERS *fault) {
    PortCrash crash;

    switch (fault->ExceptionRecord->ExceptionCode) {
    case EXCEPTION_ACCESS_VIOLATION:
        crash.what = "memory that is not there was touched";
        break;
    case EXCEPTION_INT_DIVIDE_BY_ZERO:
        crash.what = "a division by zero";
        break;
    case EXCEPTION_ILLEGAL_INSTRUCTION:
        crash.what = "what is not an instruction was run";
        break;
    default:
        crash.what = "the system stopped the program";
        break;
    }
    crash.address =
        fault->ExceptionRecord->NumberParameters >= 2
            ? (PortAddress)fault->ExceptionRecord->ExceptionInformation[1]
            : 0;
#if defined(_WIN64)
    crash.pc = (PortAddress)fault->ContextRecord->Rip;
    crash.frame = (PortAddress)fault->ContextRecord->Rbp;
    crash.stack = (PortAddress)fault->ContextRecord->Rsp;
#else
    crash.pc = (PortAddress)fault->ContextRecord->Eip;
    crash.frame = (PortAddress)fault->ContextRecord->Ebp;
    crash.stack = (PortAddress)fault->ContextRecord->Esp;
#endif
    g_port_windows_report(&crash);
    ExitProcess(PORT_EXIT_CRASH);
    return EXCEPTION_EXECUTE_HANDLER;
}

void port_HostCatchCrashes(void (*report)(const PortCrash *crash)) {
    g_port_windows_report = report;
    SetUnhandledExceptionFilter(port_WindowsOnFault);
}

/* The program's arguments out of its command line, cut in place: apart
 * by spaces, a piece in double quotes kept whole. */
void port_WindowsStart(void) {
    static char *arguments[ARGUMENTS_MAX];
    char *at = GetCommandLineA();
    s32 count = 0;

    while (*at != 0 && count < ARGUMENTS_MAX) {
        char *to;
        s32 quoted = 0;

        while (*at == ' ') {
            at++;
        }
        if (*at == 0) {
            break;
        }
        arguments[count++] = to = at;
        for (; *at != 0 && (quoted || *at != ' '); at++) {
            if (*at == '"') {
                quoted = !quoted;
            } else {
                *to++ = *at;
            }
        }
        if (*at != 0) {
            at++;
        }
        *to = 0;
    }
    port_HostExit(port_Main(count, arguments));
}

#endif /* _WIN32 */
