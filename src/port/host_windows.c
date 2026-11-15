/* The native port on Windows (port_host.h). It compiles to nothing
 * anywhere else. Built with MinGW by tools/port_sdl_build.py --windows,
 * twice (docs/PORT.md, "The window"):
 *
 *   - into rebushido.exe, the program with a window, beside SDL 2 and a
 *     C runtime, whose `main` is in src/port/window/sdl_main.c;
 *   - into rebushido-headless.exe, the program without one
 *     (PORT_NO_C_RUNTIME), which asks only kernel32 and is started at
 *     port_WindowsStart (the linker's -e), with no C runtime's start-up.
 *
 * It has to be a 32-bit program that may use addresses from 2 GB up
 * (the linker's --large-address-aware: the PlayStation's RAM begins at
 * exactly 2 GB), linked without address randomisation and at an address
 * of the port's choosing, so that nothing of the system's is where the
 * PlayStation's memory goes; if something is, it says so and stops, as
 * on Linux. */
#if defined(_WIN32)

#include <windows.h>

#include "port_host.h"

#define FILES_MAX 16
/* A file this large or larger has no size the port can count. */
#define TWO_GIGABYTES 0x80000000LL
#define ARGUMENTS_MAX 32
/* Windows gives addresses in blocks of this size, whatever is asked. */
#define BLOCK_BYTES 0x10000U
#define BLOCKS_MAX 8

static HANDLE g_port_windows_files[FILES_MAX];
static void (*g_port_windows_report)(const PortCrash *crash);
/* The thread the game runs on: only its faults are the game's. */
static DWORD g_port_windows_thread;
/* The first block of each piece of memory port_HostMapAt was given. */
static u32 g_port_windows_blocks[BLOCKS_MAX];
static s32 g_port_windows_block_count;

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

/* Memory at an address. Windows gives addresses in blocks of 64 KB: a
 * piece that begins inside a block cannot be asked for by itself, and
 * the page of the sound chip's registers is 4 KB past the scratchpad.
 * So whole blocks are kept for each piece, and a later piece inside a
 * block this file was given before only has its pages made usable.
 * Anything else must be free, or the answer is no. */
s32 port_HostMapAt(u32 address, u32 size) {
    void *wanted = (void *)(ULONG_PTR)address;
    u32 block = address & ~(BLOCK_BYTES - 1);
    u32 blocks_size =
        (address - block + size + BLOCK_BYTES - 1) & ~(BLOCK_BYTES - 1);
    s32 i;

    for (i = 0; i < g_port_windows_block_count; i++) {
        if (g_port_windows_blocks[i] == block) {
            MEMORY_BASIC_INFORMATION memory;

            /* kept and not yet in use, all of it: or it is taken */
            return VirtualQuery(wanted, &memory, sizeof(memory)) != 0 &&
                   memory.State == MEM_RESERVE && memory.RegionSize >= size &&
                   VirtualAlloc(wanted, size, MEM_COMMIT, PAGE_READWRITE) !=
                       NULL;
        }
    }
    if (g_port_windows_block_count == BLOCKS_MAX ||
        VirtualAlloc((void *)(ULONG_PTR)block, blocks_size, MEM_RESERVE,
                     PAGE_READWRITE) != (void *)(ULONG_PTR)block) {
        return 0;
    }
    g_port_windows_blocks[g_port_windows_block_count++] = block;
    return VirtualAlloc(wanted, size, MEM_COMMIT, PAGE_READWRITE) != NULL;
}

void *port_HostAllocate(u32 size) {
    return VirtualAlloc(NULL, size, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
}

/* The stack pointer to the top of the memory given, no frame above the
 * function's, and the call, as on Linux. Windows also keeps where the
 * thread's stack is, and will not hand a fault on to anyone while the
 * stack pointer is outside it: so it is told the new stack's two ends,
 * and that no handler of the old stack's is left. (A fault of the
 * game's is caught before any of that is looked at, below.) */
void port_HostCallOnStack(void (*function)(void), u32 stack_top) {
    ULONG_PTR top = stack_top;
    ULONG_PTR bottom = top;
    MEMORY_BASIC_INFORMATION memory;

    if (VirtualQuery((void *)(top - 1), &memory, sizeof(memory)) != 0) {
        bottom = (ULONG_PTR)memory.AllocationBase;
    }
    /* the thread's own record begins with its chain of handlers, the
     * stack's top and the stack's bottom */
#if defined(__i386__)
    __asm__ volatile(
        "movl $-1, %%fs:0\n"
        "movl %0, %%fs:4\n"
        "movl %2, %%fs:8\n"
        "movl %0, %%esp\n"
        "xorl %%ebp, %%ebp\n"
        "call *%1\n"
        :
        : "r"(top), "r"(function), "r"(bottom)
        : "memory");
#elif defined(__x86_64__)
    __asm__ volatile(
        "movq %0, %%gs:8\n"
        "movq %2, %%gs:16\n"
        "movq %0, %%rsp\n"
        "subq $32, %%rsp\n"
        "xorl %%ebp, %%ebp\n"
        "call *%1\n"
        :
        : "r"(top), "r"(function), "r"(bottom)
        : "memory");
#else
#error "host_windows.c: this processor's stack switch is not written here"
#endif
    for (;;) {
        port_HostExit(1);
    }
}

/* The system calls this first for every fault of the program, before
 * it looks for anyone else to handle it. The game's thread touching
 * memory it does not have, dividing by zero or running what is not an
 * instruction is the game's fault: says what and where, and ends the
 * program. Everything else is passed on. */
static LONG WINAPI port_WindowsOnFault(EXCEPTION_POINTERS *fault) {
    PortCrash crash;

    if (GetCurrentThreadId() != g_port_windows_thread) {
        return EXCEPTION_CONTINUE_SEARCH;
    }
    switch (fault->ExceptionRecord->ExceptionCode) {
    case EXCEPTION_ACCESS_VIOLATION:
    case EXCEPTION_IN_PAGE_ERROR:
    case EXCEPTION_STACK_OVERFLOW:
        crash.what = "memory that is not there was touched";
        break;
    case EXCEPTION_INT_DIVIDE_BY_ZERO:
        crash.what = "a division by zero";
        break;
    case EXCEPTION_ILLEGAL_INSTRUCTION:
    case EXCEPTION_PRIV_INSTRUCTION:
        crash.what = "what is not an instruction was run";
        break;
    default:
        return EXCEPTION_CONTINUE_SEARCH;
    }
    crash.address = fault->ExceptionRecord->NumberParameters >= 2
                        ? (u32)fault->ExceptionRecord->ExceptionInformation[1]
                        : 0;
#if defined(__i386__)
    crash.pc = (u32)fault->ContextRecord->Eip;
    crash.frame = (u32)fault->ContextRecord->Ebp;
    crash.stack = (u32)fault->ContextRecord->Esp;
#elif defined(__x86_64__)
    crash.pc = (u32)fault->ContextRecord->Rip;
    crash.frame = (u32)fault->ContextRecord->Rbp;
    crash.stack = (u32)fault->ContextRecord->Rsp;
#else
#error "host_windows.c: this processor's registers are not written here"
#endif
    g_port_windows_report(&crash);
    ExitProcess(PORT_EXIT_CRASH);
    return EXCEPTION_CONTINUE_SEARCH;
}

void port_HostCatchCrashes(void (*report)(const PortCrash *crash)) {
    g_port_windows_report = report;
    g_port_windows_thread = GetCurrentThreadId();
    AddVectoredExceptionHandler(1, port_WindowsOnFault);
}

/* Where the program without a C runtime starts. Its arguments out of
 * its command line, cut in place: apart by spaces, a piece in double
 * quotes kept whole. */
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

#if defined(PORT_NO_C_RUNTIME)
/* What MinGW's compiler calls at the top of a function named `main`,
 * for a C runtime's start-up work: there is none, and the game's own
 * function has that name. */
void __main(void) {}

/* What MinGW's linker has every program call at its start to put right
 * the addresses it could not: the port has none of those. */
void _pei386_runtime_relocator(void) {}
#endif

#endif /* _WIN32 */
