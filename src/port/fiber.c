/* The game's stack as a fiber (port_host.h, port_fiber.h; docs/NETPLAY.md,
 * "The step"): the switch between the host's stack and the game's, and
 * the game's first frame.
 *
 * In the game's object, beside the game's code, whatever the program:
 * at a frame's boundary the game's stack holds the addresses its frames
 * return to, this file's among them (the entry's, and the leave's), and
 * a replay's start or a spectator's holds that stack. A window build
 * whose window code differs lays out its own code otherwise, but not the
 * game's object's (tools/port_sdl_build.py), so these addresses are the
 * same in every build that may play the start (port_StateBuildSum). */
#include "common.h"
#include "port_host.h"
#include "port_fiber.h"
#include "bb2.h"

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

PortAddress g_port_game_context;
static PortAddress g_port_host_context;
static void (*g_port_game_function)(void);

/* Where the game's stack first returns to: the frame pointer is 0 there,
 * which ends the chain of callers a crash report walks. */
static void port_FiberGameEntry(void) {
    g_port_game_function();
    for (;;) {
        port_HostExit(1);
    }
}

void port_FiberStartGame(
    void (*function)(void), PortAddress top, const PortAddress *tib) {
    g_port_game_function = function;
    g_port_game_context = port_FiberPrepare(top, port_FiberGameEntry, tib);
}

void port_HostEnterGame(void) {
    port_FiberSwitch(&g_port_host_context, g_port_game_context);
}

void port_HostLeaveGame(void) {
    port_FiberSwitch(&g_port_game_context, g_port_host_context);
}

#if defined(__i386__) && defined(_WIN32)
s32 (*g_port_fiber_on_fault)(
    void *record, void *frame, void *context, void *dispatch);

s32 port_FiberOnFault(
    void *record, void *frame, void *context, void *dispatch) {
    return g_port_fiber_on_fault(record, frame, context, dispatch);
}
#endif
