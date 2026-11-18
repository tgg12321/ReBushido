/* The native port on Linux (port_host.h): an x86 program, of 64-bit
 * pointers or of 32-bit ones, that asks the kernel directly for
 * everything, with no C library, as the host tests do
 * (tests/runtime/runtime.c): a 32-bit C library is not always installed,
 * and the port so far needs a dozen calls. When the port takes a library
 * for its window and sound (docs/PORT.md) this file is the one that
 * changes.
 *
 * The two sizes differ in how the kernel is called, in the numbers of
 * its calls and in what it hands a fault's handler; each of those is
 * here twice, and everything else once. */
#if defined(__linux__) && (defined(__i386__) || defined(__x86_64__))

#include "common.h"
#include "port_host.h"
#include "port_fiber.h"
#include "bb2.h"

/* What the kernel takes and gives: a number as wide as a pointer. */
typedef __INTPTR_TYPE__ LinuxWord;

#define LINUX_SEEK_SET 0
#define LINUX_SEEK_END 2
#define LINUX_O_RDONLY 0
#define LINUX_O_WRONLY 1
#define LINUX_O_RDWR 2
#define LINUX_O_RDWR 2
#define LINUX_O_CREAT 0x40
#define LINUX_O_TRUNC 0x200
#define LINUX_O_LARGEFILE 0x8000
#define LINUX_FILE_MODE 0644
#define LINUX_FOLDER_MODE 0755
#define LINUX_O_DIRECTORY 0x10000
#define LINUX_EEXIST 17
/* getdents64's record: the inode's number and the next record's place
 * (eight bytes each), the record's length, the kind, then the name */
#define LINUX_DIRENT_LENGTH_AT 16
#define LINUX_DIRENT_KIND_AT 18
#define LINUX_DIRENT_NAME_AT 19
#define LINUX_DT_UNKNOWN 0
#define LINUX_DT_DIR 4
#define LINUX_DT_LNK 10
#define LINUX_FOLDER_BUFFER_BYTES 0x2000
#define LINUX_PATH_BYTES 1024
#define LINUX_PROT_READ_WRITE 3
#define LINUX_PROT_READ 1
#define LINUX_MAP_PRIVATE 0x02
#define LINUX_MAP_ANONYMOUS 0x20
/* At the address asked or not at all: a kernel that does not know the
 * flag (before 4.17) takes the address for a wish, and the answer is
 * looked at. */
#define LINUX_MAP_FIXED_NOREPLACE 0x100000
#define LINUX_PAGE_SIZE 0x1000U
/* The kernel's answers from -1 to -4095 are errors. */
#define LINUX_IS_ERROR(result) ((PortAddress)(result) > (PortAddress) - 4096)
/* A file larger than this has no size the port can count
 * (port_HostFileSize). */
#define LINUX_FILE_SIZE_MAX 0x7FFFFFFF

#define LINUX_SIGILL 4
#define LINUX_SIGBUS 7
#define LINUX_SIGFPE 8
#define LINUX_SIGSEGV 11
#define LINUX_SA_SIGINFO 0x00000004U
#define LINUX_SA_RESTORER 0x04000000U
#define LINUX_SA_ONSTACK 0x08000000U
#define LINUX_SA_RESETHAND 0x80000000U
#define LINUX_SIGNAL_SET_BYTES 8
#define LINUX_CLOCK_MONOTONIC 1
#define LINUX_TICKS_PER_SECOND 10000000U
#define LINUX_NANOSECONDS_PER_TICK 100U
#define LINUX_AF_INET 2
#define LINUX_SOCK_DGRAM 2
#define LINUX_POLLIN 1
/* The most arguments a call of the sockets takes. */
#define LINUX_CALL_ARGS 6
#define BITS_PER_BYTE 8

#if defined(__x86_64__)

/* ---- system calls (64-bit x86 Linux: `syscall`) ---- */

enum {
    LINUX_READ = 0,
    LINUX_WRITE = 1,
    LINUX_OPEN = 2,
    LINUX_CLOSE = 3,
    LINUX_LSEEK = 8,
    LINUX_MMAP = 9,
    LINUX_MPROTECT = 10,
    LINUX_MUNMAP = 11,
    LINUX_RT_SIGACTION = 13,
    LINUX_RT_SIGRETURN = 15,
    LINUX_EXIT = 60,
    LINUX_FSYNC = 74,
    LINUX_SIGALTSTACK = 131,
    LINUX_CLOCK_GETTIME = 228,
    LINUX_POLL = 7,
    LINUX_SOCKET = 41,
    LINUX_SENDTO = 44,
    LINUX_RECVFROM = 45,
    LINUX_BIND = 49,
    LINUX_MKDIR = 83,
    LINUX_GETDENTS64 = 217,
};

/* The call's number in %rax, its arguments in %rdi, %rsi, %rdx, %r10,
 * %r8 and %r9; the kernel leaves %rcx and %r11 changed. */
static LinuxWord port_LinuxCall6(
    LinuxWord number, LinuxWord first, LinuxWord second, LinuxWord third,
    LinuxWord fourth, LinuxWord fifth, LinuxWord sixth) {
    register LinuxWord in_r10 __asm__("r10") = fourth;
    register LinuxWord in_r8 __asm__("r8") = fifth;
    register LinuxWord in_r9 __asm__("r9") = sixth;
    LinuxWord result;

    __asm__ volatile("syscall"
                     : "=a"(result)
                     : "a"(number), "D"(first), "S"(second), "d"(third),
                       "r"(in_r10), "r"(in_r8), "r"(in_r9)
                     : "rcx", "r11", "memory");
    return result;
}

static LinuxWord port_LinuxCall(
    LinuxWord number, LinuxWord first, LinuxWord second, LinuxWord third) {
    return port_LinuxCall6(number, first, second, third, 0, 0, 0);
}

static LinuxWord port_LinuxMap(u32 address, u32 size, s32 flags) {
    return port_LinuxCall6(LINUX_MMAP, (LinuxWord)address, (LinuxWord)size,
                           LINUX_PROT_READ_WRITE, flags, -1, 0);
}

/* rt_sigaction's fourth argument is the size of the mask. */
static void port_LinuxSetAction(s32 number, const void *action) {
    port_LinuxCall6(LINUX_RT_SIGACTION, number, (LinuxWord)action, 0,
                    LINUX_SIGNAL_SET_BYTES, 0, 0);
}

#else

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
    LINUX_MPROTECT = 125,
    LINUX_RT_SIGACTION = 174,
    LINUX_SIGALTSTACK = 186,
    LINUX_MMAP2 = 192, /* the offset in pages, the six arguments in registers */
    LINUX_CLOCK_GETTIME = 265,
    LINUX_POLL = 168,
    /* the sockets' calls, by one number and a list of arguments */
    LINUX_SOCKETCALL = 102,
    LINUX_SOCKETCALL_SOCKET = 1,
    LINUX_SOCKETCALL_BIND = 2,
    LINUX_SOCKETCALL_SENDTO = 11,
    LINUX_SOCKETCALL_RECVFROM = 12,
    LINUX_MKDIR = 39,
    LINUX_GETDENTS64 = 220,
};

static LinuxWord port_LinuxCall(
    LinuxWord number, LinuxWord first, LinuxWord second, LinuxWord third) {
    LinuxWord result;

    __asm__ volatile("int $0x80"
                     : "=a"(result)
                     : "a"(number), "b"(first), "c"(second), "d"(third)
                     : "memory");
    return result;
}

/* mmap2 takes six. The sixth goes in %ebp, which holds this function's
 * frame, so it is put there by hand around the call. */
static LinuxWord port_LinuxMap(u32 address, u32 size, s32 flags) {
    LinuxWord result;
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

/* rt_sigaction's fourth argument, the size of the mask, is in %esi. */
static void port_LinuxSetAction(s32 number, const void *action) {
    s32 result;

    __asm__ volatile("int $0x80"
                     : "=a"(result)
                     : "a"(LINUX_RT_SIGACTION), "b"(number), "c"(action),
                       "d"(0), "S"(LINUX_SIGNAL_SET_BYTES)
                     : "memory");
    (void)result;
}

#endif

void port_HostExit(s32 code) {
    for (;;) {
        port_LinuxCall(LINUX_EXIT, code, 0, 0);
    }
}

/* All of it, however many pieces the kernel takes it in. */
void port_HostWrite(s32 stream, const char *bytes, u32 count) {
    u32 done = 0;

    while (done < count) {
        LinuxWord wrote =
            port_LinuxCall(LINUX_WRITE, stream, (LinuxWord)(bytes + done),
                           (LinuxWord)(count - done));
        if (wrote <= 0) {
            return;
        }
        done += (u32)wrote;
    }
}

s32 port_HostOpen(const char *path) {
    LinuxWord file = port_LinuxCall(
        LINUX_OPEN, (LinuxWord)path, LINUX_O_RDONLY | LINUX_O_LARGEFILE, 0);

    return LINUX_IS_ERROR(file) ? -1 : (s32)file;
}

s32 port_HostCreate(const char *path) {
    LinuxWord file = port_LinuxCall(
        LINUX_OPEN, (LinuxWord)path,
        LINUX_O_WRONLY | LINUX_O_CREAT | LINUX_O_TRUNC, LINUX_FILE_MODE);

    return LINUX_IS_ERROR(file) ? -1 : (s32)file;
}

void port_HostClose(s32 file) { port_LinuxCall(LINUX_CLOSE, file, 0, 0); }

u32 port_HostFileSize(s32 file) {
    LinuxWord size = port_LinuxCall(LINUX_LSEEK, file, 0, LINUX_SEEK_END);

    return LINUX_IS_ERROR(size) || size > LINUX_FILE_SIZE_MAX ? 0 : (u32)size;
}

/* Reads until it has `count` bytes or the file ends (port_host.h). */
u32 port_HostReadAt(s32 file, u32 offset, void *dest, u32 count) {
    u32 done = 0;

    if (LINUX_IS_ERROR(port_LinuxCall(
            LINUX_LSEEK, file, (LinuxWord)offset, LINUX_SEEK_SET))) {
        return 0;
    }
    port_HostTouchPages(dest, count);
    while (done < count) {
        LinuxWord got =
            port_LinuxCall(LINUX_READ, file, (LinuxWord)((char *)dest + done),
                           (LinuxWord)(count - done));
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
        LinuxWord wrote = port_LinuxCall(
            LINUX_WRITE, file, (LinuxWord)((const char *)bytes + done),
            (LinuxWord)(count - done));
        if (wrote <= 0) {
            return 0;
        }
        done += (u32)wrote;
    }
    return 1;
}

/* A file that is there already, read and written in place (port_host.h). */
s32 port_HostOpenReadWrite(const char *path) {
    LinuxWord file = port_LinuxCall(
        LINUX_OPEN, (LinuxWord)path, LINUX_O_RDWR | LINUX_O_LARGEFILE, 0);

    return LINUX_IS_ERROR(file) ? -1 : (s32)file;
}

/* Writes all of it at `offset` (port_host.h). */
s32 port_HostWriteAt(s32 file, u32 offset, const void *bytes, u32 count) {
    u32 done = 0;

    if (LINUX_IS_ERROR(port_LinuxCall(
            LINUX_LSEEK, file, (LinuxWord)offset, LINUX_SEEK_SET))) {
        return 0;
    }
    while (done < count) {
        LinuxWord wrote = port_LinuxCall(
            LINUX_WRITE, file, (LinuxWord)((const char *)bytes + done),
            (LinuxWord)(count - done));
        if (wrote <= 0) {
            return 0;
        }
        done += (u32)wrote;
    }
    return 1;
}

void port_HostFlush(s32 file) { port_LinuxCall(LINUX_FSYNC, file, 0, 0); }

/* ---- the network (port_host.h) ---- */

/* struct sockaddr_in: the port and the address in the network's order
 * of bytes, the most significant first. */
typedef struct {
    u16 family;
    u16 port;
    u32 address;
    u8 zero[8];
} LinuxSocketAddress;

/* struct pollfd. */
typedef struct {
    s32 file;
    s16 events;
    s16 answered;
} LinuxPoll;

/* A port's or an address's bytes, the other way round. */
static u16 port_LinuxSwap16(u16 value) {
    return (u16)((value >> BITS_PER_BYTE) | (value << BITS_PER_BYTE));
}

static u32 port_LinuxSwap32(u32 value) {
    return ((u32)port_LinuxSwap16((u16)value) << (2 * BITS_PER_BYTE)) |
           port_LinuxSwap16((u16)(value >> (2 * BITS_PER_BYTE)));
}

/* The port's address as the kernel takes it. */
static void port_LinuxToSocket(
    LinuxSocketAddress *out, const PortNetAddress *in) {
    u32 i;

    out->family = LINUX_AF_INET;
    out->port = port_LinuxSwap16(in->port);
    out->address = port_LinuxSwap32(in->address);
    for (i = 0; i < sizeof(out->zero); i++) {
        out->zero[i] = 0;
    }
}

#if defined(__x86_64__)
#define LINUX_SOCKET_CALL(name, args)                                          \
    port_LinuxCall6(LINUX_##name, (args)[0], (args)[1], (args)[2], (args)[3],  \
                    (args)[4], (args)[5])
#else
/* socketcall: the call's number and the list of its arguments. */
#define LINUX_SOCKET_CALL(name, args)                                          \
    port_LinuxCall(                                                            \
        LINUX_SOCKETCALL, LINUX_SOCKETCALL_##name, (LinuxWord)(args), 0)
#endif

/* A socket of datagrams, bound where it is told. */
s32 port_HostUdpOpen(const PortNetAddress *at) {
    LinuxSocketAddress address;
    LinuxWord args[LINUX_CALL_ARGS] = {
        LINUX_AF_INET, LINUX_SOCK_DGRAM, 0, 0, 0, 0};
    LinuxWord file = LINUX_SOCKET_CALL(SOCKET, args);

    if (LINUX_IS_ERROR(file)) {
        return -1;
    }
    port_LinuxToSocket(&address, at);
    args[0] = file;
    args[1] = (LinuxWord)&address;
    args[2] = sizeof(address);
    if (LINUX_IS_ERROR(LINUX_SOCKET_CALL(BIND, args))) {
        port_LinuxCall(LINUX_CLOSE, file, 0, 0);
        return -1;
    }
    return (s32)file;
}

/* One datagram to the peer. */
s32 port_HostUdpSend(
    s32 socket, const PortNetAddress *to, const void *bytes, u32 count) {
    LinuxSocketAddress address;
    LinuxWord args[LINUX_CALL_ARGS];
    LinuxWord sent;

    port_LinuxToSocket(&address, to);
    args[0] = (LinuxWord)socket;
    args[1] = (LinuxWord)bytes;
    args[2] = count;
    args[3] = 0;
    args[4] = (LinuxWord)&address;
    args[LINUX_CALL_ARGS - 1] = sizeof(address);
    sent = LINUX_SOCKET_CALL(SENDTO, args);
    return !LINUX_IS_ERROR(sent) && (u32)sent == count;
}

/* poll, then recvfrom: a datagram, if one comes in time. */
s32 port_HostUdpReceive(
    s32 socket, void *bytes, u32 room, PortNetAddress *from, u32 wait_ms) {
    LinuxSocketAddress address;
    u32 address_bytes = sizeof(address);
    LinuxWord args[LINUX_CALL_ARGS];
    LinuxPoll poll;
    LinuxWord got;

    poll.file = socket;
    poll.events = LINUX_POLLIN;
    poll.answered = 0;
    got = port_LinuxCall(LINUX_POLL, (LinuxWord)&poll, 1, wait_ms);
    if (LINUX_IS_ERROR(got) || got == 0) {
        return -1;
    }
    port_HostTouchPages(bytes, room);
    args[0] = (LinuxWord)socket;
    args[1] = (LinuxWord)bytes;
    args[2] = room;
    args[3] = 0;
    args[4] = (LinuxWord)&address;
    args[LINUX_CALL_ARGS - 1] = (LinuxWord)&address_bytes;
    got = LINUX_SOCKET_CALL(RECVFROM, args);
    if (LINUX_IS_ERROR(got)) {
        return -1;
    }
    if (from != NULL) {
        from->address = port_LinuxSwap32(address.address);
        from->port = port_LinuxSwap16(address.port);
    }
    return (s32)got;
}

/* Whether a path is a folder, for a name whose kind the listing did not
 * say: it opens as one. */
static s32 port_LinuxIsFolder(const char *path) {
    LinuxWord file = port_LinuxCall(
        LINUX_OPEN, (LinuxWord)path, LINUX_O_RDONLY | LINUX_O_DIRECTORY, 0);

    if (LINUX_IS_ERROR(file)) {
        return 0;
    }
    port_LinuxCall(LINUX_CLOSE, file, 0, 0);
    return 1;
}

/* The folder's records by getdents64, a buffer at a time (port_host.h). */
s32 port_HostListFolder(
    const char *path,
    void (*each)(void *context, const char *name, s32 is_folder),
    void *context) {
    static u8 records[LINUX_FOLDER_BUFFER_BYTES];
    char whole[LINUX_PATH_BYTES];
    u32 path_length = 0;
    LinuxWord folder = port_LinuxCall(
        LINUX_OPEN, (LinuxWord)path, LINUX_O_RDONLY | LINUX_O_DIRECTORY, 0);

    if (LINUX_IS_ERROR(folder)) {
        return 0;
    }
    while (path[path_length] != 0 && path_length + 2 < LINUX_PATH_BYTES) {
        whole[path_length] = path[path_length];
        path_length++;
    }
    whole[path_length++] = '/';
    for (;;) {
        LinuxWord got = port_LinuxCall(
            LINUX_GETDENTS64, folder, (LinuxWord)records, sizeof(records));
        LinuxWord at = 0;

        if (LINUX_IS_ERROR(got) || got == 0) {
            break;
        }
        while (at < got) {
            const u8 *record = records + at;
            const char *name = (const char *)record + LINUX_DIRENT_NAME_AT;
            u32 length = record[LINUX_DIRENT_LENGTH_AT] |
                         ((u32)record[LINUX_DIRENT_LENGTH_AT + 1] << 8);
            s32 kind = record[LINUX_DIRENT_KIND_AT];
            s32 is_folder = kind == LINUX_DT_DIR;

            at += length;
            if (name[0] == '.' &&
                (name[1] == 0 || (name[1] == '.' && name[2] == 0))) {
                continue;
            }
            if (kind == LINUX_DT_UNKNOWN || kind == LINUX_DT_LNK) {
                /* the file system did not say, or it is a link: what it
                 * opens as */
                u32 i;

                for (i = 0;
                     name[i] != 0 && path_length + i + 1 < LINUX_PATH_BYTES;
                     i++) {
                    whole[path_length + i] = name[i];
                }
                whole[path_length + i] = 0;
                is_folder = port_LinuxIsFolder(whole);
            }
            each(context, name, is_folder);
        }
    }
    port_LinuxCall(LINUX_CLOSE, folder, 0, 0);
    return 1;
}

s32 port_HostMakeFolder(const char *path) {
    LinuxWord made =
        port_LinuxCall(LINUX_MKDIR, (LinuxWord)path, LINUX_FOLDER_MODE, 0);

    return !LINUX_IS_ERROR(made) || (s32)(-made) == LINUX_EEXIST;
}

/* The clock that only goes forward, in seconds and nanoseconds; the
 * arithmetic wraps, which is all the port's count does (port_host.h). */
u32 port_HostTicks(void) {
    LinuxWord time[2] = {0, 0};

    port_LinuxCall(
        LINUX_CLOCK_GETTIME, LINUX_CLOCK_MONOTONIC, (LinuxWord)time, 0);
    return (u32)time[0] * LINUX_TICKS_PER_SECOND +
           (u32)time[1] / LINUX_NANOSECONDS_PER_TICK;
}

/* Whole pages at exactly `address`, or nothing (port_host.h). */
s32 port_HostMapAt(u32 address, u32 size) {
    LinuxWord got;

    size = (size + LINUX_PAGE_SIZE - 1) & ~(LINUX_PAGE_SIZE - 1);
    got = port_LinuxMap(
        address, size,
        LINUX_MAP_PRIVATE | LINUX_MAP_ANONYMOUS | LINUX_MAP_FIXED_NOREPLACE);
    if (LINUX_IS_ERROR(got)) {
        return 0;
    }
    if ((PortAddress)got != address) {
        /* somewhere else: not what was asked */
        port_LinuxCall(LINUX_MUNMAP, got, (LinuxWord)size, 0);
        return 0;
    }
    return 1;
}

void *port_HostAllocate(u32 size) {
    LinuxWord got;

    size = (size + LINUX_PAGE_SIZE - 1) & ~(LINUX_PAGE_SIZE - 1);
    got = port_LinuxMap(0, size, LINUX_MAP_PRIVATE | LINUX_MAP_ANONYMOUS);
    return LINUX_IS_ERROR(got) ? NULL : (void *)got;
}

/* ---- faults ---- */

/* What the kernel hands a handler that asks for it (SA_SIGINFO), as far
 * as this reads it: the faulting address in the first, the registers in
 * the second. Three numbers, then the address on a word's boundary. */
typedef struct {
    s32 number;
    s32 error;
    s32 code;
    PortAddress address;
} LinuxSignalInfo;

#if defined(__x86_64__)

typedef struct {
    PortAddress flags;
    PortAddress link;
    PortAddress stack_base;
    s32 stack_flags;
    PortAddress stack_size;
    PortAddress r8_to_r15[8];
    PortAddress rdi, rsi, rbp, rbx, rdx, rax, rcx, rsp;
    PortAddress rip;
} LinuxSignalContext;

#define LINUX_CONTEXT_PC(context) ((context)->rip)
#define LINUX_CONTEXT_FRAME(context) ((context)->rbp)
#define LINUX_CONTEXT_STACK(context) ((context)->rsp)

/* Where a handler returns to, which a 64-bit kernel has to be given: it
 * asks the kernel to put back what the fault interrupted. A crash's
 * handler never returns; the watcher of writes does
 * (port_HostWatchWrites). */
static __attribute__((naked)) void port_LinuxRestore(void) {
    __asm__("movl $15, %eax\n"
            "syscall\n");
}

#define LINUX_ACTION_FLAGS LINUX_SA_RESTORER

#else

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

#define LINUX_CONTEXT_PC(context) ((context)->eip)
#define LINUX_CONTEXT_FRAME(context) ((context)->ebp)
#define LINUX_CONTEXT_STACK(context) ((context)->esp)

/* The same for a 32-bit program: the kernel would find one of its own,
 * but a handler that returns is given the C library's, rt_sigreturn. */
static __attribute__((naked)) void port_LinuxRestore(void) {
    __asm__("movl $173, %eax\n"
            "int $0x80\n");
}

#define LINUX_ACTION_FLAGS LINUX_SA_RESTORER

#endif

/* The flags are a word as wide as a pointer, the mask 64 signals. */
typedef struct {
    void (*handler)(s32 number, LinuxSignalInfo *info, void *context);
    PortAddress flags;
    void (*restorer)(void);
    u8 mask[LINUX_SIGNAL_SET_BYTES];
} LinuxSignalAction;

typedef struct {
    void *base;
    s32 flags;
    PortAddress size;
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
    crash.pc = LINUX_CONTEXT_PC(registers);
    crash.frame = LINUX_CONTEXT_FRAME(registers);
    crash.stack = LINUX_CONTEXT_STACK(registers);
    g_port_linux_report(&crash);
    port_HostExit(PORT_EXIT_CRASH);
}

/* What a write to a page made read-only is handed to first
 * (port_HostWatchWrites). */
static s32 (*g_port_linux_on_write)(PortAddress address);

/* The kernel calls `handler` for signal `number`, on the handlers' own
 * stack, with `flags` besides. */
static void port_LinuxSetHandler(
    s32 number, void (*handler)(s32, LinuxSignalInfo *, void *), u32 flags) {
    LinuxSignalAction action;
    u32 i;

    action.handler = handler;
    action.flags =
        flags | LINUX_SA_SIGINFO | LINUX_SA_ONSTACK | LINUX_ACTION_FLAGS;
    action.restorer = port_LinuxRestore;
    for (i = 0; i < sizeof(action.mask); i++) {
        action.mask[i] = 0;
    }
    port_LinuxSetAction(number, &action);
}

/* SIGSEGV while writes are watched: a write the watcher takes goes on
 * where it was; any other is a crash, or the system's own end where
 * crashes are not caught. */
static void port_LinuxOnSegv(s32 number, LinuxSignalInfo *info, void *context) {
    LinuxSignalAction action;
    u32 i;

    if (g_port_linux_on_write(info->address)) {
        return;
    }
    if (g_port_linux_report != NULL) {
        port_LinuxOnSignal(number, info, context);
    }
    /* the fault again, with nothing to take it */
    action.handler = NULL;
    action.flags = 0;
    action.restorer = NULL;
    for (i = 0; i < sizeof(action.mask); i++) {
        action.mask[i] = 0;
    }
    port_LinuxSetAction(LINUX_SIGSEGV, &action);
}

/* The handlers run on a stack of their own: the fault may be the game's
 * stack running out. */
static void port_LinuxSignalStack(void) {
    LinuxSignalStack stack;

    stack.base = g_port_linux_signal_stack;
    stack.flags = 0;
    stack.size = sizeof(g_port_linux_signal_stack);
    port_LinuxCall(LINUX_SIGALTSTACK, (LinuxWord)&stack, 0, 0);
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
    u32 i;

    g_port_linux_report = report;
    port_LinuxSignalStack();
    /* a second fault, in the report itself, ends the program as the
     * system would have */
    for (i = 0; i < sizeof(signals) / sizeof(signals[0]); i++) {
        port_LinuxSetHandler(
            signals[i], port_LinuxOnSignal, LINUX_SA_RESETHAND);
    }
    if (g_port_linux_on_write != NULL) {
        port_LinuxSetHandler(LINUX_SIGSEGV, port_LinuxOnSegv, 0);
    }
}

void port_HostWatchWrites(s32 (*on_write)(PortAddress address)) {
    if (g_port_linux_on_write == NULL && g_port_linux_report == NULL) {
        port_LinuxSignalStack();
    }
    g_port_linux_on_write = on_write;
    port_LinuxSetHandler(LINUX_SIGSEGV, port_LinuxOnSegv, 0);
}

s32 port_HostProtect(void *start, u32 bytes, s32 is_writable) {
    LinuxWord got =
        port_LinuxCall(LINUX_MPROTECT, (LinuxWord)start, (LinuxWord)bytes,
                       is_writable ? LINUX_PROT_READ_WRITE : LINUX_PROT_READ);

    return !LINUX_IS_ERROR(got);
}

/* ---- the game's stack, as a fiber (port_host.h, port_fiber.h) ---- */

PortAddress g_port_game_context;
static PortAddress g_port_host_context;
static void (*g_port_game_function)(void);

/* Where the game's stack first returns to: the frame pointer is 0 there,
 * which ends the chain of callers a crash report walks. */
static void port_LinuxGameEntry(void) {
    g_port_game_function();
    for (;;) {
        port_HostExit(1);
    }
}

void port_HostStartGame(void (*function)(void), u32 stack_top) {
    g_port_game_function = function;
    g_port_game_context =
        port_FiberPrepare(stack_top, port_LinuxGameEntry, NULL);
}

void port_HostEnterGame(void) {
    port_FiberSwitch(&g_port_host_context, g_port_game_context);
}

void port_HostLeaveGame(void) {
    port_FiberSwitch(&g_port_game_context, g_port_host_context);
}

/* ---- start-up ---- */

/* The kernel starts the program here with the argument count and the
 * arguments on the stack, each a word as wide as a pointer. */
static void port_LinuxStart(PortAddress *stack) __attribute__((used, noreturn));

#if defined(__x86_64__)
__asm__(".text\n"
        ".globl _start\n"
        "_start:\n"
        "    xorl %ebp, %ebp\n"
        "    movq %rsp, %rdi\n"
        "    andq $-16, %rsp\n"
        "    call port_LinuxStart\n"
        "    hlt\n");
#else
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
#endif

static void port_LinuxStart(PortAddress *stack) {
    port_HostExit(port_Main((s32)stack[0], (char **)(stack + 1)));
}

#endif /* __linux__, and an x86 of either size */
