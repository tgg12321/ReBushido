#ifndef PORT_FIBER_H
#define PORT_FIBER_H

/* The switch between the host's stack and the game's (docs/NETPLAY.md,
 * "The step"): included by the one host file of each program
 * (src/port/host_linux.c, host_windows.c, window/host_posix.c), which
 * defines port_HostStartGame, port_HostEnterGame and port_HostLeaveGame
 * of port_host.h on it.
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

extern void port_FiberSwitch(PortAddress *save, PortAddress load);

#if defined(__x86_64__) && defined(_WIN32)
__asm__(
    ".text\n"
    ".globl port_FiberSwitch\n"
    "port_FiberSwitch:\n"
    "    pushq %rbp\n"
    "    pushq %rbx\n"
    "    pushq %rdi\n"
    "    pushq %rsi\n"
    "    pushq %r12\n"
    "    pushq %r13\n"
    "    pushq %r14\n"
    "    pushq %r15\n"
    "    subq $160, %rsp\n"
    "    movdqu %xmm6, 0(%rsp)\n"
    "    movdqu %xmm7, 16(%rsp)\n"
    "    movdqu %xmm8, 32(%rsp)\n"
    "    movdqu %xmm9, 48(%rsp)\n"
    "    movdqu %xmm10, 64(%rsp)\n"
    "    movdqu %xmm11, 80(%rsp)\n"
    "    movdqu %xmm12, 96(%rsp)\n"
    "    movdqu %xmm13, 112(%rsp)\n"
    "    movdqu %xmm14, 128(%rsp)\n"
    "    movdqu %xmm15, 144(%rsp)\n"
    "    pushq %gs:8\n"
    "    pushq %gs:16\n"
    "    movq %rsp, (%rcx)\n"
    "    movq %rdx, %rsp\n"
    "    popq %gs:16\n"
    "    popq %gs:8\n"
    "    movdqu 0(%rsp), %xmm6\n"
    "    movdqu 16(%rsp), %xmm7\n"
    "    movdqu 32(%rsp), %xmm8\n"
    "    movdqu 48(%rsp), %xmm9\n"
    "    movdqu 64(%rsp), %xmm10\n"
    "    movdqu 80(%rsp), %xmm11\n"
    "    movdqu 96(%rsp), %xmm12\n"
    "    movdqu 112(%rsp), %xmm13\n"
    "    movdqu 128(%rsp), %xmm14\n"
    "    movdqu 144(%rsp), %xmm15\n"
    "    addq $160, %rsp\n"
    "    popq %r15\n"
    "    popq %r14\n"
    "    popq %r13\n"
    "    popq %r12\n"
    "    popq %rsi\n"
    "    popq %rdi\n"
    "    popq %rbx\n"
    "    popq %rbp\n"
    "    ret\n");
#elif defined(__x86_64__)
__asm__(
    ".text\n"
    ".globl port_FiberSwitch\n"
    ".type port_FiberSwitch, @function\n"
    "port_FiberSwitch:\n"
    "    pushq %rbp\n"
    "    pushq %rbx\n"
    "    pushq %r12\n"
    "    pushq %r13\n"
    "    pushq %r14\n"
    "    pushq %r15\n"
    "    movq %rsp, (%rdi)\n"
    "    movq %rsi, %rsp\n"
    "    popq %r15\n"
    "    popq %r14\n"
    "    popq %r13\n"
    "    popq %r12\n"
    "    popq %rbx\n"
    "    popq %rbp\n"
    "    ret\n");
#elif defined(__i386__) && defined(_WIN32)
__asm__(
    ".text\n"
    ".globl _port_FiberSwitch\n"
    "_port_FiberSwitch:\n"
    "    movl 4(%esp), %eax\n"
    "    movl 8(%esp), %edx\n"
    "    pushl %ebp\n"
    "    pushl %ebx\n"
    "    pushl %esi\n"
    "    pushl %edi\n"
    "    pushl %fs:0\n"
    "    pushl %fs:4\n"
    "    pushl %fs:8\n"
    "    movl %esp, (%eax)\n"
    "    movl %edx, %esp\n"
    "    popl %fs:8\n"
    "    popl %fs:4\n"
    "    popl %fs:0\n"
    "    popl %edi\n"
    "    popl %esi\n"
    "    popl %ebx\n"
    "    popl %ebp\n"
    "    ret\n");
#else
__asm__(
    ".text\n"
    ".globl port_FiberSwitch\n"
    ".type port_FiberSwitch, @function\n"
    "port_FiberSwitch:\n"
    "    movl 4(%esp), %eax\n"
    "    movl 8(%esp), %edx\n"
    "    pushl %ebp\n"
    "    pushl %ebx\n"
    "    pushl %esi\n"
    "    pushl %edi\n"
    "    movl %esp, (%eax)\n"
    "    movl %edx, %esp\n"
    "    popl %edi\n"
    "    popl %esi\n"
    "    popl %ebx\n"
    "    popl %ebp\n"
    "    ret\n");
#endif

/* A first frame for a stack that has never run, below `top`: the words
 * the switch pops, all 0 but the thread's record `tib` (Windows: the
 * words in the order the switch pops them, the last end of the stack
 * first), then `entry` as the address it returns to, then a word where
 * the entry's own return address would be, so that it starts with the
 * stack as a called function does. The entry must not return. Returns
 * the stack pointer to switch to. */
static PortAddress port_FiberPrepare(
    PortAddress top, void (*entry)(void), const PortAddress *tib) {
    PortAddress *word;
    s32 i;

    top = (top - PORT_FIBER_TOP_ROOM) & ~(PortAddress)(PORT_FIBER_ALIGN - 1);
    word = (PortAddress *)top;
    *--word = 0;
    *--word = (PortAddress)entry;
    for (i = 0; i < PORT_FIBER_GPRS + PORT_FIBER_XMM_WORDS; i++) {
        *--word = 0;
    }
    for (i = PORT_FIBER_TIB_WORDS - 1; i >= 0; i--) {
        *--word = tib[i];
    }
    return (PortAddress)word;
}

#endif /* PORT_FIBER_H */
