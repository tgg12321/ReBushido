/* The native port on a C library (port_host.h): what host_linux.c asks
 * of the kernel itself, asked of the library here, for the build with a
 * window (docs/PORT.md, "The window"), which has one because SDL does.
 * Linux so far; nothing but the two pieces marked is Linux's or a
 * processor's. Windows has its own file, src/port/host_windows.c.
 *
 * The program starts at the library's main, which is in sdl_main.c. */
#if !defined(_WIN32)

#define _GNU_SOURCE
#include <arpa/inet.h>
#include <netinet/in.h>
#include <poll.h>
#include <dirent.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <time.h>
#include <ucontext.h>
#include <unistd.h>

#include "port_host.h"
#include "port_fiber.h"

#define FILES_MAX 16
#define PATH_BYTES_MAX 1024
#define FOLDER_MODE 0755
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
    port_HostTouchPages(dest, count);
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

/* ---- the network (port_host.h) ---- */

/* The sockets opened for IPv6 (and IPv4 with it), whose addresses are
 * given as IPv6 with IPv4 mapped. */
#define V6_SOCKETS_MAX 8
static int g_port_v6_sockets[V6_SOCKETS_MAX];
static int g_port_v6_socket_count;

static int port_PosixIsV6(int file) {
    int i;

    for (i = 0; i < g_port_v6_socket_count; i++) {
        if (g_port_v6_sockets[i] == file) {
            return 1;
        }
    }
    return 0;
}

/* The port's address as the library takes it, for a socket of IPv6 or of
 * IPv4: its length. */
static socklen_t port_PosixToSocket(
    struct sockaddr_storage *out, const PortNetAddress *in, int v6) {
    memset(out, 0, sizeof(*out));
    if (v6) {
        struct sockaddr_in6 *six = (struct sockaddr_in6 *)out;

        six->sin6_family = AF_INET6;
        six->sin6_port = htons(in->port);
        if (in->is_v6) {
            memcpy(six->sin6_addr.s6_addr, in->v6, PORT_NET_V6_BYTES);
        } else {
            u32 address = htonl(in->address);

            six->sin6_addr.s6_addr[10] = 0xFF;
            six->sin6_addr.s6_addr[11] = 0xFF;
            memcpy(six->sin6_addr.s6_addr + 12, &address, 4);
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
static void port_PosixFromSocket(
    PortNetAddress *out, const struct sockaddr_storage *in) {
    static const u8 mapped[12] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0xFF, 0xFF};

    memset(out, 0, sizeof(*out));
    if (in->ss_family == AF_INET6) {
        const struct sockaddr_in6 *six = (const struct sockaddr_in6 *)in;

        out->port = ntohs(six->sin6_port);
        if (memcmp(six->sin6_addr.s6_addr, mapped, sizeof(mapped)) == 0) {
            u32 address;

            memcpy(&address, six->sin6_addr.s6_addr + 12, 4);
            out->address = ntohl(address);
        } else {
            out->is_v6 = 1;
            memcpy(out->v6, six->sin6_addr.s6_addr, PORT_NET_V6_BYTES);
        }
    } else {
        const struct sockaddr_in *four = (const struct sockaddr_in *)in;

        out->port = ntohs(four->sin_port);
        out->address = ntohl(four->sin_addr.s_addr);
    }
}

/* A socket of datagrams, bound where it is told: of both kinds where the
 * address is IPv6. */
s32 port_HostUdpOpen(const PortNetAddress *at) {
    struct sockaddr_storage address;
    socklen_t length;
    int file = socket(at->is_v6 ? AF_INET6 : AF_INET, SOCK_DGRAM, 0);

    if (file < 0) {
        return -1;
    }
    if (at->is_v6) {
        int off = 0;

        if (g_port_v6_socket_count == V6_SOCKETS_MAX) {
            close(file);
            return -1;
        }
        setsockopt(file, IPPROTO_IPV6, IPV6_V6ONLY, &off, sizeof(off));
    }
    length = port_PosixToSocket(&address, at, at->is_v6);
    if (bind(file, (struct sockaddr *)&address, length) != 0) {
        close(file);
        return -1;
    }
    if (at->is_v6) {
        g_port_v6_sockets[g_port_v6_socket_count++] = file;
    }
    return file;
}

void port_HostUdpClose(s32 socket) {
    int i;

    for (i = 0; i < g_port_v6_socket_count; i++) {
        if (g_port_v6_sockets[i] == socket) {
            g_port_v6_sockets[i] = g_port_v6_sockets[--g_port_v6_socket_count];
            break;
        }
    }
    close(socket);
}

s32 port_HostUdpBound(s32 socket, PortNetAddress *at) {
    struct sockaddr_storage address;
    socklen_t length = sizeof(address);

    if (getsockname(socket, (struct sockaddr *)&address, &length) != 0) {
        return 0;
    }
    port_PosixFromSocket(at, &address);
    return 1;
}

s32 port_HostOwnAddress(s32 v6, PortNetAddress *at) {
    /* a public address of each kind: nothing is sent to it */
    static const PortNetAddress four = {0x08080808U, 53, 0, {0}};
    static const PortNetAddress six = {
        0,
        53,
        1,
        {0x20, 0x01, 0x48, 0x60, 0x48, 0x60, 0, 0, 0, 0, 0, 0, 0, 0, 0x88,
         0x88}};
    struct sockaddr_storage address;
    socklen_t length;
    int file = socket(v6 ? AF_INET6 : AF_INET, SOCK_DGRAM, 0);
    s32 ok = 0;

    if (file < 0) {
        return 0;
    }
    length = port_PosixToSocket(&address, v6 ? &six : &four, v6);
    if (connect(file, (struct sockaddr *)&address, length) == 0) {
        length = sizeof(address);
        if (getsockname(file, (struct sockaddr *)&address, &length) == 0) {
            port_PosixFromSocket(at, &address);
            ok = 1;
        }
    }
    close(file);
    return ok;
}

/* One datagram to the peer. */
s32 port_HostUdpSend(
    s32 socket, const PortNetAddress *to, const void *bytes, u32 count) {
    struct sockaddr_storage address;
    int v6 = port_PosixIsV6(socket);
    socklen_t length;

    if (to->is_v6 && !v6) {
        return 0;
    }
    length = port_PosixToSocket(&address, to, v6);
    return sendto(socket, bytes, count, 0, (struct sockaddr *)&address,
                  length) == (ssize_t)count;
}

/* poll, then recvfrom: a datagram, if one comes in time. */
s32 port_HostUdpReceive(
    s32 socket, void *bytes, u32 room, PortNetAddress *from, u32 wait_ms) {
    struct sockaddr_storage address;
    socklen_t address_bytes = sizeof(address);
    struct pollfd poll_file;
    ssize_t got;

    poll_file.fd = socket;
    poll_file.events = POLLIN;
    poll_file.revents = 0;
    if (poll(&poll_file, 1, (int)wait_ms) <= 0) {
        return -1;
    }
    port_HostTouchPages(bytes, room);
    got = recvfrom(
        socket, bytes, room, 0, (struct sockaddr *)&address, &address_bytes);
    if (got < 0) {
        return -1;
    }
    if (from != NULL) {
        port_PosixFromSocket(from, &address);
    }
    return (s32)got;
}

s32 port_HostListFolder(
    const char *path,
    void (*each)(void *context, const char *name, s32 is_folder),
    void *context) {
    DIR *listing = opendir(path);
    struct dirent *entry;

    if (listing == NULL) {
        return 0;
    }
    while ((entry = readdir(listing)) != NULL) {
        const char *name = entry->d_name;
        s32 is_folder = entry->d_type == DT_DIR;

        if (name[0] == '.' &&
            (name[1] == 0 || (name[1] == '.' && name[2] == 0))) {
            continue;
        }
        if (entry->d_type == DT_UNKNOWN || entry->d_type == DT_LNK) {
            /* the file system did not say, or it is a link: what it
             * leads to */
            char whole[PATH_BYTES_MAX];
            struct stat about;

            snprintf(whole, sizeof(whole), "%s/%s", path, name);
            is_folder = stat(whole, &about) == 0 && S_ISDIR(about.st_mode);
        }
        each(context, name, is_folder);
    }
    closedir(listing);
    return 1;
}

s32 port_HostMakeFolder(const char *path) {
    struct stat about;

    if (mkdir(path, FOLDER_MODE) == 0) {
        return 1;
    }
    return stat(path, &about) == 0 && S_ISDIR(about.st_mode);
}

/* The kernel's random bytes, by /dev/urandom. */
s32 port_HostRandom(void *out, u32 count) {
    FILE *file = fopen("/dev/urandom", "rb");
    size_t had;

    if (file == NULL) {
        return 0;
    }
    had = fread(out, 1, count, file);
    fclose(file);
    return had == count;
}

s32 port_HostRemove(const char *path) { return unlink(path) == 0; }

s32 port_HostRename(const char *from, const char *to) {
    return rename(from, to) == 0;
}

void port_HostDate(PortDate *date) {
    time_t now = time(NULL);
    struct tm parts;

    localtime_r(&now, &parts);
    date->year = parts.tm_year + 1900;
    date->month = parts.tm_mon + 1;
    date->day = parts.tm_mday;
    date->hour = parts.tm_hour;
    date->minute = parts.tm_min;
    date->second = parts.tm_sec;
    date->is_utc = FALSE;
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

/* What a write to a page made read-only is handed to first
 * (port_HostWatchWrites). */
static s32 (*g_port_posix_on_write)(PortAddress address);

/* SIGSEGV while writes are watched: a write the watcher takes goes on
 * where it was; any other is a crash, or the system's own end where
 * crashes are not caught. */
static void port_PosixOnSegv(int number, siginfo_t *info, void *context) {
    struct sigaction action;

    if (g_port_posix_on_write((PortAddress)info->si_addr)) {
        return;
    }
    if (g_port_posix_report != NULL) {
        port_PosixOnSignal(number, info, context);
    }
    /* the fault again, with nothing to take it */
    memset(&action, 0, sizeof(action));
    action.sa_handler = SIG_DFL;
    sigaction(SIGSEGV, &action, NULL);
}

/* The handlers run on a stack of their own: the fault may be the game's
 * stack running out. */
static void port_PosixSignalStack(void) {
    static char signal_stack[SIGNAL_STACK_BYTES];
    stack_t stack;

    stack.ss_sp = signal_stack;
    stack.ss_flags = 0;
    stack.ss_size = sizeof(signal_stack);
    sigaltstack(&stack, NULL);
}

/* SIGSEGV to the watcher of writes, for good: it comes back from the
 * faults it takes. */
static void port_PosixWatchSegv(void) {
    struct sigaction action;

    action.sa_sigaction = port_PosixOnSegv;
    action.sa_flags = SA_SIGINFO | SA_ONSTACK;
    sigemptyset(&action.sa_mask);
    sigaction(SIGSEGV, &action, NULL);
}

/* The four faults (port_host.h). */
void port_HostCatchCrashes(void (*report)(const PortCrash *crash)) {
    static const int signals[] = {SIGSEGV, SIGBUS, SIGFPE, SIGILL};
    struct sigaction action;
    size_t i;

    g_port_posix_report = report;
    port_PosixSignalStack();
    action.sa_sigaction = port_PosixOnSignal;
    /* a second fault, in the report itself, ends the program as the
     * system would have */
    action.sa_flags = SA_SIGINFO | SA_ONSTACK | SA_RESETHAND;
    sigemptyset(&action.sa_mask);
    for (i = 0; i < sizeof(signals) / sizeof(signals[0]); i++) {
        sigaction(signals[i], &action, NULL);
    }
    if (g_port_posix_on_write != NULL) {
        port_PosixWatchSegv();
    }
}

void port_HostWatchWrites(s32 (*on_write)(PortAddress address)) {
    if (g_port_posix_on_write == NULL && g_port_posix_report == NULL) {
        port_PosixSignalStack();
    }
    g_port_posix_on_write = on_write;
    port_PosixWatchSegv();
}

s32 port_HostProtect(void *start, u32 bytes, s32 is_writable) {
    return mprotect(start, bytes,
                    is_writable ? PROT_READ | PROT_WRITE : PROT_READ) == 0;
}

/* ---- the game's stack, as a fiber (port_host.h, port_fiber.h) ---- */

void port_HostStartGame(void (*function)(void), u32 stack_top) {
    port_FiberStartGame(function, stack_top, NULL);
}

#endif /* !_WIN32 */
