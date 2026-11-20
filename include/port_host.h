#ifndef PORT_HOST_H
#define PORT_HOST_H

/* The native port's one boundary with the PC's operating system
 * (docs/PORT.md, "The program"): what the port asks of it, and nothing
 * else in src/port/ asks anything. One file defines these for each
 * system: src/port/host_linux.c, on the kernel's own calls with no C
 * library, and src/port/host_windows.c. */

#include "common.h"

/* The port's own address type, as wide as a pointer. `common.h` declares
 * it where the port's switch is on; the files that talk to the system, to
 * a C library or to SDL, EGL and OpenGL are compiled without the game's
 * switches (tools/port_sdl_build.py, tools/port_gl_build.py), so they
 * declare it for themselves. The shared guard keeps the typedef to one:
 * writing it twice is C11 and these files are gnu99. */
#ifndef PORT_ADDRESS_DEFINED
#define PORT_ADDRESS_DEFINED
typedef __UINTPTR_TYPE__ PortAddress;
#endif

/* Where the system starts the program is the host file's; it calls this
 * with the program's arguments (the first is the program's own path) and
 * ends the program with what it returns (src/port/main.c). */
extern s32 port_Main(s32 argc, char **argv);
/* Every piece of memory the port has at a fixed address -- the RAM, the
 * scratchpad, the page of registers, the game's stack and the port's
 * own buffers of primitives (port_platform.h, src/port/memory.c) --
 * given at once, or the program stops, saying why. A program with
 * libraries of its own (the window, the remastered renderer's prover)
 * calls this first thing, before any of them starts: a library's memory
 * may land anywhere, and the scratchpad at 0x1F800000 is low enough for
 * the C library's heap to reach. port_Main calls it as well; a second
 * call does nothing. */
extern void port_MapFixedMemory(void);
/* Where Windows starts the program (host_windows.c): the linker is given
 * this name for its entry. */
extern void port_WindowsStart(void);

/* Ends the program at once. */
extern void port_HostExit(s32 code) __attribute__((noreturn));

/* Bytes to the program's output (PORT_HOST_OUTPUT) or to its error
 * output. */
#define PORT_HOST_OUTPUT 1
#define PORT_HOST_ERRORS 2
extern void port_HostWrite(s32 stream, const char *bytes, u32 count);
/* If not NULL, given everything port_HostWrite is, as well: the session's
 * log of the build with a window (src/port/window/). The hosts of that
 * build define it; the host with no C library has no use for it. */
extern void (*g_port_host_tee)(s32 stream, const char *bytes, u32 count);

/* A file, for reading, or made new for writing: a number that is not
 * negative, or -1. */
extern s32 port_HostOpen(const char *path);
extern s32 port_HostCreate(const char *path);
extern void port_HostClose(s32 file);
/* Its size in bytes; 0 if it cannot be told or is 2 GB or more. */
extern u32 port_HostFileSize(s32 file);
/* `count` bytes from `offset` of a file opened for reading. Returns how
 * many were read: fewer than asked only at the file's end or on an
 * error. */
extern u32 port_HostReadAt(s32 file, u32 offset, void *dest, u32 count);
/* Appends to a file made by port_HostCreate. Returns 1 if all of it was
 * written. */
extern s32 port_HostAppend(s32 file, const void *bytes, u32 count);
/* A file that is there already, for reading and writing in place (the
 * memory card's image): a number that is not negative, or -1. */
extern s32 port_HostOpenReadWrite(const char *path);
/* `count` bytes to `offset` of such a file. Returns 1 if all of them were
 * written. */
extern s32 port_HostWriteAt(s32 file, u32 offset, const void *bytes, u32 count);
/* Asks the system to put what has been written on the disc, so that what
 * the game has been told is saved survives a crash. */
extern void port_HostFlush(s32 file);

/* The names in a folder, each given to `each` with whether it is a
 * folder itself (`.` and `..` left out), in no particular order: 1, or 0
 * if `path` is not a folder that can be read (the mods, docs/MODS.md). */
extern s32 port_HostListFolder(
    const char *path,
    void (*each)(void *context, const char *name, s32 is_folder),
    void *context);
/* Makes a folder, if it is not there: 1 if it is there afterwards. */
extern s32 port_HostMakeFolder(const char *path);
/* A file taken away, and one given a new name over whatever had it: 1
 * where it was done (the replays, src/port/replay.c). */
extern s32 port_HostRemove(const char *path);
extern s32 port_HostRename(const char *from, const char *to);

/* The date and time now: the PC's own where the host knows its zone (the
 * builds with a window), and otherwise UTC (`is_utc`). Only for what a
 * replay says of itself and its name; nothing of the game reads it. */
typedef struct {
    s32 year;
    s32 month; /* 1 to 12 */
    s32 day;   /* 1 to 31 */
    s32 hour;
    s32 minute;
    s32 second;
    s32 is_utc;
} PortDate;

extern void port_HostDate(PortDate *date);

/* The PC's clock, in tenths of a microsecond from some moment: only for
 * how long something took (the rollback self-test's report, state.c).
 * It wraps after seven minutes; nothing of the game reads it. */
#define PORT_HOST_TICKS_PER_MICROSECOND 10
extern u32 port_HostTicks(void);

/* ---- the network: datagrams of UDP over IPv4 and IPv6 (docs/NETPLAY.md,
 * sections 11 and 14; src/port/net.c) ---- */

/* An address and a port, in the host's own order of bytes: 127.0.0.1 is
 * 0x7F000001. An IPv6 one has `is_v6` and its sixteen bytes in `v6`, in
 * the network's order, and `address` 0; an IPv4 address is never given
 * as IPv6 (an IPv4-mapped one, ::ffff:a.b.c.d, is told as IPv4). */
#define PORT_NET_V6_BYTES 16

typedef struct {
    u32 address;
    u16 port;
    u8 is_v6;
    u8 v6[PORT_NET_V6_BYTES];
} PortNetAddress;

/* A socket bound to `at`: a number that is not negative, or -1. Nothing
 * here chooses the address: the program binds where it is told. An IPv6
 * address binds a socket of both kinds (IPV6_V6ONLY off), which sends
 * and receives IPv4 too; [::] is every address of the PC of either kind. */
extern s32 port_HostUdpOpen(const PortNetAddress *at);
/* A socket closed. */
extern void port_HostUdpClose(s32 socket);
/* The address and port a socket is bound to (its port, where the system
 * chose it): 1, or 0. */
extern s32 port_HostUdpBound(s32 socket, PortNetAddress *at);
/* This PC's own address of the kind asked (IPv6 if `v6`), the one it
 * would send from to the internet: found by a socket connected to a
 * public address, which sends nothing. 1, or 0 if it has none (no route
 * of that kind). */
extern s32 port_HostOwnAddress(s32 v6, PortNetAddress *at);
/* One datagram to `to`. Returns 1 if it went. */
extern s32 port_HostUdpSend(
    s32 socket, const PortNetAddress *to, const void *bytes, u32 count);
/* One datagram, waiting up to `wait_ms` milliseconds for it (0: not at
 * all): its length, with at most `room` of its bytes, and who sent it;
 * -1 if none came. */
extern s32 port_HostUdpReceive(
    s32 socket, void *bytes, u32 room, PortNetAddress *from, u32 wait_ms);

/* `size` bytes of zeroed memory that can be read and written, at exactly
 * `address`: 1, or 0 if the system will not give memory there (something
 * else has the place, or it cannot be had at all). */
extern s32 port_HostMapAt(u32 address, u32 size);
/* The same wherever the system likes; NULL if there is none. Never given
 * back. */
extern void *port_HostAllocate(u32 size);

/* The pages a run writes, found by the system (src/port/state.c keeps
 * only those of the state; docs/NETPLAY.md, "Save, load and hash"). The
 * pages from `start` for `bytes`, both on a page's bounds, made
 * read-only or writable again: 1, or 0 if the system would not. */
#define PORT_HOST_PAGE_BYTES 0x1000U
extern s32 port_HostProtect(void *start, u32 bytes, s32 is_writable);
/* From now on a write to a page made read-only, on any thread, calls
 * `on_write` with the address written first: if it returns 1 it has
 * made the page writable and the write is made again; if 0, the fault is
 * a crash as any other. A handler that is called for a fault must
 * neither print nor allocate. */
extern void port_HostWatchWrites(s32 (*on_write)(PortAddress address));

/* A page of `bytes` from `dest` written by the program before the system
 * writes it: a read of a file or a datagram into a page made read-only
 * would fail in the system, which does not fault, where a write of the
 * program's own is taken by `on_write`. Each host calls it before it has
 * the system write into memory it was given. */
static inline void port_HostTouchPages(void *dest, u32 bytes) {
    volatile u8 *at = (volatile u8 *)dest;
    PortAddress end = (PortAddress)dest + bytes;

    if (bytes == 0) {
        return;
    }
    for (;;) {
        *at = *at;
        at = (volatile u8 *)(((PortAddress)at &
                              ~(PortAddress)(PORT_HOST_PAGE_BYTES - 1)) +
                             PORT_HOST_PAGE_BYTES);
        if ((PortAddress)at >= end) {
            break;
        }
    }
}

/* The game runs on a stack of its own, the memory below `stack_top`, as
 * a fiber the host enters and the game leaves (docs/NETPLAY.md, "The
 * step"; the switch is include/port_fiber.h). port_HostStartGame makes
 * it ready to call `function`, which must not return, and runs nothing.
 * port_HostEnterGame runs the game from where it last left off until it
 * calls port_HostLeaveGame, and then returns. Only the game calls
 * port_HostLeaveGame, which returns when the host enters it again. */
extern void port_HostStartGame(void (*function)(void), u32 stack_top);
extern void port_HostEnterGame(void);
extern void port_HostLeaveGame(void);
/* The game's stack pointer while it is not running, which is part of the
 * game's state (src/port/state.c saves it with the stack); 0 before it
 * has started. */
extern PortAddress g_port_game_context;

/* What the processor was doing when the program was stopped for touching
 * memory it does not have, dividing by zero or running what is not an
 * instruction. */
typedef struct {
    const char *what;    /* the kind of fault, in words */
    PortAddress address; /* the memory it touched, where that is known */
    PortAddress pc;      /* the instruction */
    PortAddress frame;   /* the frame pointer there: the chain of callers */
    PortAddress stack;   /* the stack pointer there */
} PortCrash;

/* From now on such a fault calls `report` and then ends the program with
 * PORT_EXIT_CRASH, where it would have died saying nothing. */
#define PORT_EXIT_CRASH 70
extern void port_HostCatchCrashes(void (*report)(const PortCrash *crash));

#endif /* PORT_HOST_H */
