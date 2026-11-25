#ifndef PORT_FIBER_H
#define PORT_FIBER_H

/* The switch between the host's stack and the game's (docs/NETPLAY.md,
 * "The step"). It is src/port/fiber.c, in the game's object with the
 * game's code (docs/PORT.md, "Replays": a replay's start holds the game's
 * stack, and so the addresses its frames return to, which have to be the
 * game's own and not the window's), and defines port_HostEnterGame and
 * port_HostLeaveGame of port_host.h. The one host file of each program
 * (src/port/host_linux.c, host_windows.c, window/host_posix.c) defines
 * port_HostStartGame on port_FiberStartGame below.
 *
 * port_FiberSwitch(save, load) pushes the registers a called function
 * must keep for its caller onto the stack it runs on, stores the stack
 * pointer in *save, takes `load` for the stack pointer, pops the same
 * registers from there and returns to whatever that stack's switch was
 * called from. A stack that has never run is given a first frame by
 * port_FiberPrepare, which "returns" into the entry function.
 *
 * Each processor's and system's list of those registers:
 *
 *   - 64-bit Linux: rbp, rbx, r12 to r15;
 *   - 32-bit Linux: ebp, ebx, esi, edi;
 *   - 64-bit Windows: those of Linux, rdi, rsi and xmm6 to xmm15, then
 *     the thread's record of its stack's two ends (gs:8, gs:16), which
 *     Windows checks before it hands a fault to anybody;
 *   - 32-bit Windows: those of 32-bit Linux, then the chain of the
 *     stack's fault handlers and its two ends (fs:0, fs:4, fs:8).
 *
 * The game's files are compiled with no vector instructions; the host's
 * (SDL, the C library) are not, which is why 64-bit Windows keeps the
 * vector registers its convention says a callee keeps. */

#include "common.h"

#ifndef PORT_ADDRESS_DEFINED
#define PORT_ADDRESS_DEFINED
typedef __UINTPTR_TYPE__ PortAddress;
#endif

/* The words a switch leaves on a stack below the address it returns
 * to: its registers, and on Windows the thread's record. */
#if defined(__x86_64__) && defined(_WIN32)
#define PORT_FIBER_GPRS 8
#define PORT_FIBER_XMM_WORDS 20
#define PORT_FIBER_TIB_WORDS 2
#elif defined(__x86_64__)
#define PORT_FIBER_GPRS 6
#define PORT_FIBER_XMM_WORDS 0
#define PORT_FIBER_TIB_WORDS 0
#elif defined(__i386__) && defined(_WIN32)
#define PORT_FIBER_GPRS 4
#define PORT_FIBER_XMM_WORDS 0
#define PORT_FIBER_TIB_WORDS 3
#elif defined(__i386__)
#define PORT_FIBER_GPRS 4
#define PORT_FIBER_XMM_WORDS 0
#define PORT_FIBER_TIB_WORDS 0
#else
#error "port_fiber.h: this processor's switch of stacks is not written here"
#endif
#define PORT_FIBER_WORDS                                                       \
    (PORT_FIBER_GPRS + PORT_FIBER_XMM_WORDS + PORT_FIBER_TIB_WORDS)
/* Room left at a new stack's top: 64-bit Windows gives a called function
 * 32 bytes above its return address, and the stack is kept on 16. */
#define PORT_FIBER_TOP_ROOM 64
#define PORT_FIBER_ALIGN 16

/* The switch itself, written in assembly in src/port/fiber.c. */
extern void port_FiberSwitch(PortAddress *save, PortAddress load);

/* Makes the game's stack ready to call `function` (which must not
 * return) from its first frame below `top`, the words `tib` being the
 * thread's record a switch takes back (PORT_FIBER_TIB_WORDS of them;
 * none but on Windows). */
extern void port_FiberStartGame(
    void (*function)(void), PortAddress top, const PortAddress *tib);

#if defined(__i386__) && defined(_WIN32)
/* 32-bit Windows: the handler of the game's stack's chain of fault
 * handlers, which port_HostStartGame puts at the stack's top (and so in
 * the state): the game's object's, which passes the fault to
 * g_port_fiber_on_fault, the host's. */
extern s32 port_FiberOnFault(
    void *record, void *frame, void *context, void *dispatch);
extern s32 (*g_port_fiber_on_fault)(
    void *record, void *frame, void *context, void *dispatch);
#endif

#endif /* PORT_FIBER_H */
