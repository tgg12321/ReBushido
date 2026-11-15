#ifndef PORT_HOST_H
#define PORT_HOST_H

/* The native port's one boundary with the PC's operating system
 * (docs/PORT.md, "The program"): what the port asks of it, and nothing
 * else in src/port/ asks anything. One file defines these for each
 * system: src/port/host_linux.c, on the kernel's own calls with no C
 * library, and src/port/host_windows.c. */

#include "common.h"

/* Where the system starts the program is the host file's; it calls this
 * with the program's arguments (the first is the program's own path) and
 * ends the program with what it returns (src/port/main.c). */
extern s32 port_Main(s32 argc, char **argv);
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

/* `size` bytes of zeroed memory that can be read and written, at exactly
 * `address`: 1, or 0 if the system will not give memory there (something
 * else has the place, or it cannot be had at all). */
extern s32 port_HostMapAt(u32 address, u32 size);
/* The same wherever the system likes; NULL if there is none. Never given
 * back. */
extern void *port_HostAllocate(u32 size);

/* Calls `function` with the stack pointer at `stack_top`, the end of
 * memory the port has for it. The function does not return. */
extern void port_HostCallOnStack(void (*function)(void), u32 stack_top)
    __attribute__((noreturn));

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
