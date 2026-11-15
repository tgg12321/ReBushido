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
 * It is linked without address randomisation and at an address of the
 * port's choosing, so that nothing of the system's is where the
 * PlayStation's memory goes; if something is, it says so and stops, as
 * on Linux. A 32-bit program must also be allowed addresses from 2 GB
 * up (the linker's --large-address-aware: the PlayStation's RAM begins
 * at exactly 2 GB); a 64-bit one must be linked below 4 GB, so that its
 * own variables are where a 32-bit number reaches them (docs/PORT.md,
 * "64 bits").
 *
 * WRITTEN AND BUILT, NOT YET RUN AS A 64-BIT PROGRAM: the port with a
 * window is a 32-bit program so far. */
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

/* What a fault was, to the port's report, and the program's end. */
static void port_WindowsReport(
    const EXCEPTION_RECORD *record, const CONTEXT *context) {
    PortCrash crash;

    switch (record->ExceptionCode) {
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
        crash.what = "the system stopped the program";
        break;
    }
    crash.address =
        record->NumberParameters >= 2
            ? (PortAddress)record->ExceptionInformation[1]
            : 0;
#if defined(__i386__)
    crash.pc = (PortAddress)context->Eip;
    crash.frame = (PortAddress)context->Ebp;
    crash.stack = (PortAddress)context->Esp;
#elif defined(__x86_64__)
    crash.pc = (PortAddress)context->Rip;
    crash.frame = (PortAddress)context->Rbp;
    crash.stack = (PortAddress)context->Rsp;
#else
#error "host_windows.c: this processor's registers are not written here"
#endif
    g_port_windows_report(&crash);
    ExitProcess(PORT_EXIT_CRASH);
}

/* The system calls this for a fault nothing else took, while the
 * program is still on the stack it was started on. */
static LONG WINAPI port_WindowsOnFault(EXCEPTION_POINTERS *fault) {
    port_WindowsReport(fault->ExceptionRecord, fault->ContextRecord);
    return EXCEPTION_EXECUTE_HANDLER;
}

#if defined(__i386__)
/* ... and this one on the game's stack: the last handler of that
 * stack's chain, which port_HostCallOnStack puts at its top. It is asked
 * only when nothing above it took the fault, a system library's own
 * handlers among them. */
static EXCEPTION_DISPOSITION __cdecl port_WindowsOnGameFault(
    EXCEPTION_RECORD *record, void *frame, CONTEXT *context, void *dispatch) {
    (void)frame;
    (void)dispatch;
    if ((record->ExceptionFlags &
         (EXCEPTION_UNWINDING | EXCEPTION_EXIT_UNWIND)) == 0) {
        port_WindowsReport(record, context);
    }
    return ExceptionContinueSearch;
}
#else
/* Where a stack has no chain of handlers (64-bit Windows keeps tables
 * instead): the system calls this first for every fault of the program.
 * The game's thread touching memory it does not have, dividing by zero
 * or running what is not an instruction is taken for the game's.
 * WRITTEN AND NOT BUILT OR RUN: the port is a 32-bit program so far. */
static LONG WINAPI port_WindowsOnFirstFault(EXCEPTION_POINTERS *fault) {
    DWORD code = fault->ExceptionRecord->ExceptionCode;

    if (GetCurrentThreadId() == g_port_windows_thread &&
        (code == EXCEPTION_ACCESS_VIOLATION ||
         code == EXCEPTION_INT_DIVIDE_BY_ZERO ||
         code == EXCEPTION_ILLEGAL_INSTRUCTION)) {
        port_WindowsReport(fault->ExceptionRecord, fault->ContextRecord);
    }
    return EXCEPTION_CONTINUE_SEARCH;
}
#endif

void port_HostCatchCrashes(void (*report)(const PortCrash *crash)) {
    g_port_windows_report = report;
    g_port_windows_thread = GetCurrentThreadId();
    SetUnhandledExceptionFilter(port_WindowsOnFault);
#if !defined(__i386__)
    AddVectoredExceptionHandler(1, port_WindowsOnFirstFault);
#endif
}

/* The stack pointer to the top of the memory given, no frame above the
 * function's, and the call, as on Linux. Windows also keeps where the
 * thread's stack is, and hands a fault to nobody while the stack
 * pointer is outside it: so the thread's own record is told the new
 * stack's two ends. That record begins with the chain of the stack's
 * fault handlers, the stack's top and the stack's bottom; the old
 * stack's handlers are left behind with it, and the new chain is one
 * handler, the port's, in two words at the new stack's top. */
void port_HostCallOnStack(void (*function)(void), u32 stack_top) {
    ULONG_PTR top = stack_top;
    ULONG_PTR bottom = top;
    MEMORY_BASIC_INFORMATION memory;

    if (VirtualQuery((void *)(top - 1), &memory, sizeof(memory)) != 0) {
        bottom = (ULONG_PTR)memory.AllocationBase;
    }
#if defined(__i386__)
    __asm__ volatile(
        "movl %0, %%fs:4\n"
        "movl %2, %%fs:8\n"
        "movl %0, %%esp\n"
        "pushl %3\n"
        "pushl $-1\n"
        "movl %%esp, %%fs:0\n"
        "subl $8, %%esp\n"
        "xorl %%ebp, %%ebp\n"
        "call *%1\n"
        :
        : "r"(top), "r"(function), "r"(bottom), "r"(port_WindowsOnGameFault)
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
