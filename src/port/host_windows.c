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
 * The program has 64-bit pointers or 32-bit ones (docs/PORT.md, "64
 * bits"). Either way it is linked without address randomisation and at
 * an address of the port's choosing below 2 GB, so that its own
 * variables are where a 32-bit number reaches them and nothing of the
 * system's is where the PlayStation's memory goes; if something is, it
 * says so and stops, as on Linux. A 32-bit program is also told that it
 * may use addresses from 2 GB up (the linker's --large-address-aware:
 * the PlayStation's RAM begins at exactly 2 GB). */
#if defined(_WIN32)

#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <x86intrin.h>

#include "port_host.h"
#include "port_fiber.h"

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

/* The session's log, in the program with a window (port_host.h). */
void (*g_port_host_tee)(s32 stream, const char *bytes, u32 count);

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
    if (g_port_host_tee != NULL) {
        g_port_host_tee(stream, bytes, count);
    }
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

/* `count` bytes of the file from `offset` into `dest`: how many it got
 * (port_host.h). */
u32 port_HostReadAt(s32 file, u32 offset, void *dest, u32 count) {
    LARGE_INTEGER at;
    DWORD got = 0;

    at.QuadPart = offset;
    port_HostTouchPages(dest, count);
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

s32 port_HostOpenReadWrite(const char *path) {
    return port_WindowsKeep(
        CreateFileA(path, GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING,
                    FILE_ATTRIBUTE_NORMAL, NULL));
}

s32 port_HostWriteAt(s32 file, u32 offset, const void *bytes, u32 count) {
    LARGE_INTEGER at;
    DWORD wrote = 0;

    at.QuadPart = offset;
    return SetFilePointerEx(g_port_windows_files[file], at, NULL, FILE_BEGIN) &&
           WriteFile(g_port_windows_files[file], bytes, count, &wrote, NULL) &&
           wrote == count;
}

void port_HostFlush(s32 file) { FlushFileBuffers(g_port_windows_files[file]); }

/* ---- the network (port_host.h): Windows' sockets, started once ---- */

#define MS_PER_SECOND 1000
#define US_PER_MS 1000

static s32 g_port_windows_sockets_started;

/* Windows' sockets, the first time one is wanted: 1 if they are. */
static s32 port_WindowsStartSockets(void) {
    if (!g_port_windows_sockets_started) {
        WSADATA data;

        if (WSAStartup(MAKEWORD(2, 2), &data) != 0) {
            return 0;
        }
        g_port_windows_sockets_started = 1;
    }
    return 1;
}

/* The sockets opened for IPv6 (and IPv4 with it), whose addresses are
 * given as IPv6 with IPv4 mapped. */
#define V6_SOCKETS_MAX 8
static SOCKET g_port_v6_sockets[V6_SOCKETS_MAX];
static s32 g_port_v6_socket_count;
/* WSAIoctl's SIO_UDP_CONNRESET, which MinGW's headers may not have. */
#define UDP_CONNRESET_CODE 0x9800000CUL
/* ::ffff:a.b.c.d: ten bytes of zeros, two of ones, then the IPv4, its
 * most significant byte first. */
#define V6_MAPPED_AT 10
#define V6_V4_AT 12
#define V4_BYTES 4
#define BYTE_BITS 8
/* A public address of each kind, which nothing is sent to: Google's
 * DNS, 8.8.8.8 and 2001:4860:4860::8888, port 53. */
#define PUBLIC_V4 0x08080808U
#define PUBLIC_PORT 53
#define PUBLIC_V6                                                              \
    {0x20, 0x01, 0x48, 0x60, 0x48, 0x60, 0, 0, 0, 0, 0, 0, 0, 0, 0x88, 0x88}

static s32 port_WindowsIsV6(SOCKET file) {
    s32 i;

    for (i = 0; i < g_port_v6_socket_count; i++) {
        if (g_port_v6_sockets[i] == file) {
            return 1;
        }
    }
    return 0;
}

static void port_WindowsZero(void *bytes, u32 count) {
    u32 i;

    for (i = 0; i < count; i++) {
        ((u8 *)bytes)[i] = 0;
    }
}

/* The port's address as Windows takes it, for a socket of IPv6 or of
 * IPv4: its length. */
static s32 port_WindowsToSocket(
    struct sockaddr_storage *out, const PortNetAddress *in, s32 v6) {
    u32 i;

    port_WindowsZero(out, sizeof(*out));
    if (v6) {
        struct sockaddr_in6 *six = (struct sockaddr_in6 *)out;
        u8 *bytes = (u8 *)&six->sin6_addr;

        six->sin6_family = AF_INET6;
        six->sin6_port = htons(in->port);
        if (in->is_v6) {
            for (i = 0; i < PORT_NET_V6_BYTES; i++) {
                bytes[i] = in->v6[i];
            }
        } else {
            bytes[V6_MAPPED_AT] = 0xFF;
            bytes[V6_MAPPED_AT + 1] = 0xFF;
            for (i = 0; i < V4_BYTES; i++) {
                bytes[V6_V4_AT + i] =
                    (u8)(in->address >> (BYTE_BITS * (V4_BYTES - 1 - i)));
            }
        }
        return sizeof(*six);
    } else {
        struct sockaddr_in *four = (struct sockaddr_in *)out;

        four->sin_family = AF_INET;
        four->sin_port = htons(in->port);
        four->sin_addr.s_addr = htonl(in->address);
        return sizeof(*four);
    }
}

/* ... and back: an IPv4-mapped address as IPv4. */
static void port_WindowsFromSocket(
    PortNetAddress *out, const struct sockaddr_storage *in) {
    u32 i;

    port_WindowsZero(out, sizeof(*out));
    if (in->ss_family == AF_INET6) {
        const struct sockaddr_in6 *six = (const struct sockaddr_in6 *)in;
        const u8 *bytes = (const u8 *)&six->sin6_addr;
        s32 is_mapped =
            bytes[V6_MAPPED_AT] == 0xFF && bytes[V6_MAPPED_AT + 1] == 0xFF;

        for (i = 0; i < V6_MAPPED_AT; i++) {
            is_mapped = is_mapped && bytes[i] == 0;
        }
        out->port = ntohs(six->sin6_port);
        if (is_mapped) {
            for (i = 0; i < V4_BYTES; i++) {
                out->address =
                    (out->address << BYTE_BITS) | bytes[V6_V4_AT + i];
            }
        } else {
            out->is_v6 = 1;
            for (i = 0; i < PORT_NET_V6_BYTES; i++) {
                out->v6[i] = bytes[i];
            }
        }
    } else {
        const struct sockaddr_in *four = (const struct sockaddr_in *)in;

        out->port = ntohs(four->sin_port);
        out->address = ntohl(four->sin_addr.s_addr);
    }
}

/* A socket of datagrams, bound where it is told: of both kinds where the
 * address is IPv6. An answer of "port unreachable" to something it sent
 * is not taken for an error of the socket's (SIO_UDP_CONNRESET off):
 * Windows would otherwise fail its next receive, as the hole punching
 * and a guest that comes before its host meet often. */
s32 port_HostUdpOpen(const PortNetAddress *at) {
    struct sockaddr_storage address;
    SOCKET file;
    BOOL off = FALSE;
    DWORD returned = 0;
    s32 length;

    if (!port_WindowsStartSockets()) {
        return -1;
    }
    if (at->is_v6 && g_port_v6_socket_count == V6_SOCKETS_MAX) {
        return -1;
    }
    file = socket(at->is_v6 ? AF_INET6 : AF_INET, SOCK_DGRAM, 0);
    if (file == INVALID_SOCKET) {
        return -1;
    }
    if (at->is_v6) {
        DWORD v6_only = 0;

        setsockopt(file, IPPROTO_IPV6, IPV6_V6ONLY, (const char *)&v6_only,
                   sizeof(v6_only));
    }
    WSAIoctl(file, UDP_CONNRESET_CODE, &off, sizeof(off), NULL, 0, &returned,
             NULL, NULL);
    length = port_WindowsToSocket(&address, at, at->is_v6);
    if (bind(file, (struct sockaddr *)&address, length) != 0) {
        closesocket(file);
        return -1;
    }
    if (at->is_v6) {
        g_port_v6_sockets[g_port_v6_socket_count++] = file;
    }
    /* a socket's handle is a small number in practice; the port keeps
     * it as one */
    return (s32)file;
}

void port_HostUdpClose(s32 socket) {
    s32 i;

    for (i = 0; i < g_port_v6_socket_count; i++) {
        if (g_port_v6_sockets[i] == (SOCKET)socket) {
            g_port_v6_sockets[i] = g_port_v6_sockets[--g_port_v6_socket_count];
            break;
        }
    }
    closesocket((SOCKET)socket);
}

s32 port_HostUdpBound(s32 socket, PortNetAddress *at) {
    struct sockaddr_storage address;
    s32 length = sizeof(address);

    if (getsockname((SOCKET)socket, (struct sockaddr *)&address, &length) !=
        0) {
        return 0;
    }
    port_WindowsFromSocket(at, &address);
    return 1;
}

/* This PC's own address of the kind (port_host.h): a socket connected to a
 * public address, which sends nothing, and the address it was given. */
s32 port_HostOwnAddress(s32 v6, PortNetAddress *at) {
    static const PortNetAddress four = {PUBLIC_V4, PUBLIC_PORT, 0, {0}};
    static const PortNetAddress six = {0, PUBLIC_PORT, 1, PUBLIC_V6};
    struct sockaddr_storage address;
    SOCKET file;
    s32 length;
    s32 ok = 0;

    if (!port_WindowsStartSockets()) {
        return 0;
    }
    file = socket(v6 ? AF_INET6 : AF_INET, SOCK_DGRAM, 0);
    if (file == INVALID_SOCKET) {
        return 0;
    }
    length = port_WindowsToSocket(&address, v6 ? &six : &four, v6);
    if (connect(file, (struct sockaddr *)&address, length) == 0) {
        length = sizeof(address);
        if (getsockname(file, (struct sockaddr *)&address, &length) == 0) {
            port_WindowsFromSocket(at, &address);
            ok = 1;
        }
    }
    closesocket(file);
    return ok;
}

/* One datagram to the peer. */
s32 port_HostUdpSend(
    s32 socket, const PortNetAddress *to, const void *bytes, u32 count) {
    struct sockaddr_storage address;
    s32 v6 = port_WindowsIsV6((SOCKET)socket);
    s32 length;

    if (to->is_v6 && !v6) {
        return 0;
    }
    length = port_WindowsToSocket(&address, to, v6);
    return sendto((SOCKET)socket, (const char *)bytes, (s32)count, 0,
                  (struct sockaddr *)&address, length) == (s32)count;
}

/* select, then recvfrom: a datagram, if one comes in time. */
s32 port_HostUdpReceive(
    s32 socket, void *bytes, u32 room, PortNetAddress *from, u32 wait_ms) {
    struct sockaddr_storage address;
    s32 address_bytes = sizeof(address);
    fd_set readable;
    struct timeval wait;
    s32 got;

    FD_ZERO(&readable);
    FD_SET((SOCKET)socket, &readable);
    wait.tv_sec = wait_ms / MS_PER_SECOND;
    wait.tv_usec = (wait_ms % MS_PER_SECOND) * US_PER_MS;
    if (select(0, &readable, NULL, NULL, &wait) <= 0) {
        return -1;
    }
    port_HostTouchPages(bytes, room);
    got = recvfrom((SOCKET)socket, (char *)bytes, (s32)room, 0,
                   (struct sockaddr *)&address, &address_bytes);
    if (got < 0) {
        return -1;
    }
    if (from != NULL) {
        port_WindowsFromSocket(from, &address);
    }
    return (s32)got;
}

/* The folder's names by FindFirstFile and FindNextFile (port_host.h). */
s32 port_HostListFolder(
    const char *path,
    void (*each)(void *context, const char *name, s32 is_folder),
    void *context) {
    char pattern[MAX_PATH];
    WIN32_FIND_DATAA found;
    HANDLE listing;
    u32 length = 0;

    while (path[length] != 0 && length + 3 < MAX_PATH) {
        pattern[length] = path[length];
        length++;
    }
    pattern[length++] = '\\';
    pattern[length++] = '*';
    pattern[length] = 0;
    listing = FindFirstFileA(pattern, &found);
    if (listing == INVALID_HANDLE_VALUE) {
        return 0;
    }
    do {
        const char *name = found.cFileName;

        if (name[0] == '.' &&
            (name[1] == 0 || (name[1] == '.' && name[2] == 0))) {
            continue;
        }
        each(context, name,
             (found.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0);
    } while (FindNextFileA(listing, &found));
    FindClose(listing);
    return 1;
}

s32 port_HostMakeFolder(const char *path) {
    return CreateDirectoryA(path, NULL) ||
           GetLastError() == ERROR_ALREADY_EXISTS;
}

/* The system's time is counted in tenths of a microsecond already: its
 * low half is the port's count (port_host.h). */
u32 port_HostTicks(void) {
    FILETIME now;

    GetSystemTimePreciseAsFileTime(&now);
    return (u32)now.dwLowDateTime;
}

/* The counter's rate is measured over this part of a second. */
#define CYCLES_MEASURED_PART 50

/* The calling thread's own processor time, in the host's ticks (100 ns):
 * its cycles of the processor's time-stamp counter (QueryThreadCycleTime),
 * at the counter's rate, which is measured once against the clock. For
 * --profile with RB_PROFILE_CPU (sdl_window.c): the time other programs
 * take the processor for is not counted. */
u32 port_HostThreadTicks(void) {
    static double ticks_a_cycle;
    ULONG64 cycles = 0;

    if (ticks_a_cycle == 0.0) {
        LARGE_INTEGER rate;
        LARGE_INTEGER from;
        LARGE_INTEGER to;
        ULONG64 start = __rdtsc();
        ULONG64 end;

        QueryPerformanceFrequency(&rate);
        QueryPerformanceCounter(&from);
        do {
            QueryPerformanceCounter(&to);
        } while ((to.QuadPart - from.QuadPart) * CYCLES_MEASURED_PART <
                 rate.QuadPart);
        end = __rdtsc();
        ticks_a_cycle = (double)(to.QuadPart - from.QuadPart) * 1.0e7 /
                        (double)rate.QuadPart / (double)(end - start);
    }
    QueryThreadCycleTime(GetCurrentThread(), &cycles);
    return (u32)(u64)((double)cycles * ticks_a_cycle);
}

/* Memory at an address. Windows gives addresses in blocks of 64 KB: a
 * piece that begins inside a block cannot be asked for by itself, and
 * the page of the sound chip's registers is 4 KB past the scratchpad.
 * So whole blocks are kept for each piece, and a later piece inside a
 * block this file was given before only has its pages made usable.
 * Anything else must be free, or the answer is no. */
s32 port_HostMapAt(u32 address, u32 size) {
    void *wanted = (void *)(PortAddress)address;
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
        VirtualAlloc((void *)(PortAddress)block, blocks_size, MEM_RESERVE,
                     PAGE_READWRITE) != (void *)(PortAddress)block) {
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
    crash.address = record->NumberParameters >= 2
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
 * stack's chain, which port_HostStartGame puts at its top. It is asked
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
 * or running what is not an instruction is taken for the game's; a
 * fault on any other thread, the sound card's among them, is left to
 * port_WindowsOnFault. */
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

/* What a write to a page made read-only is handed to first
 * (port_HostWatchWrites). */
static s32 (*g_port_windows_on_write)(PortAddress address);

/* The system calls this first for every fault of the program, on any
 * thread: a write the watcher takes goes on where it was; anything
 * else is left to the handlers after it. */
static LONG WINAPI port_WindowsOnWrite(EXCEPTION_POINTERS *fault) {
    const EXCEPTION_RECORD *record = fault->ExceptionRecord;

    if (record->ExceptionCode == EXCEPTION_ACCESS_VIOLATION &&
        record->NumberParameters >= 2 && record->ExceptionInformation[0] == 1 &&
        g_port_windows_on_write((PortAddress)record->ExceptionInformation[1])) {
        return EXCEPTION_CONTINUE_EXECUTION;
    }
    return EXCEPTION_CONTINUE_SEARCH;
}

void port_HostWatchWrites(s32 (*on_write)(PortAddress address)) {
    if (g_port_windows_on_write == NULL) {
        AddVectoredExceptionHandler(1, port_WindowsOnWrite);
    }
    g_port_windows_on_write = on_write;
}

s32 port_HostProtect(void *start, u32 bytes, s32 is_writable) {
    DWORD was;

    return VirtualProtect(start, bytes,
                          is_writable ? PAGE_READWRITE : PAGE_READONLY, &was) !=
           0;
}

/* ---- the game's stack, as a fiber (port_host.h, port_fiber.h) ---- */

PortAddress g_port_game_context;
static PortAddress g_port_host_context;
static void (*g_port_game_function)(void);

/* Where the game's stack first returns to: the frame pointer is 0 there,
 * which ends the chain of callers a crash report walks. */
static void port_WindowsGameEntry(void) {
    g_port_game_function();
    for (;;) {
        port_HostExit(1);
    }
}

/* Windows keeps where the thread's stack is, and hands a fault to nobody
 * while the stack pointer is outside it: so the switch keeps each
 * stack's own copy of the thread's record (port_fiber.h), and the game's
 * starts as the two ends of its stack. In a 32-bit program the record
 * begins with the chain of the stack's fault handlers: the game's is one
 * handler, the port's, in two words at its stack's top. In a 64-bit one
 * there is no such chain (the system keeps tables of handlers instead).
 * The record is at fs:0, fs:4, fs:8 in a 32-bit program and at gs:8,
 * gs:16 in a 64-bit one; the words are given in the order the switch
 * takes them back. */
void port_HostStartGame(void (*function)(void), u32 stack_top) {
    PortAddress top = stack_top;
    PortAddress bottom = top;
    MEMORY_BASIC_INFORMATION memory;

    if (VirtualQuery((void *)(top - 1), &memory, sizeof(memory)) != 0) {
        bottom = (PortAddress)memory.AllocationBase;
    }
    g_port_game_function = function;
#if defined(__i386__)
    {
        PortAddress *chain = (PortAddress *)top - 2;
        PortAddress record[PORT_FIBER_TIB_WORDS];

        chain[0] = (PortAddress)-1;
        chain[1] = (PortAddress)port_WindowsOnGameFault;
        record[0] = bottom;
        record[1] = top;
        record[2] = (PortAddress)chain;
        g_port_game_context = port_FiberPrepare(
            (PortAddress)chain, port_WindowsGameEntry, record);
    }
#else
    {
        PortAddress record[PORT_FIBER_TIB_WORDS];

        record[0] = bottom;
        record[1] = top;
        g_port_game_context =
            port_FiberPrepare(top, port_WindowsGameEntry, record);
    }
#endif
}

void port_HostEnterGame(void) {
    port_FiberSwitch(&g_port_host_context, g_port_game_context);
}

void port_HostLeaveGame(void) {
    port_FiberSwitch(&g_port_game_context, g_port_host_context);
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
