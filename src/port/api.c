/* The rest of what the game asks of the console in the native port: the
 * BIOS's calls about the machine itself, Sony's interrupt set-up, the
 * link cable, and the C library where Sony's own C is not compiled
 * (docs/PORT.md, "Sony's libraries in the port"). */
#include "common.h"
#include "port_monocypher.h"
#include <psxsdk/libsn.h>
#include "bb2.h"

/* ---- the machine: nothing to do on a PC ---- */

/* Sony's start-up, which main calls first: it runs the constructors of a
 * C++ program, and the game has none. */
void __main(void) {}

/* The stack is the PC's own, and so is the memory. */
u32 SetSp(u32 sp) {
    (void)sp;
    return 0;
}

void SetMem(s32 megabytes) { (void)megabytes; }

/* Interrupts off and on: the port has none, and one thread. */
s32 EnterCriticalSection(void) { return 1; }

void ExitCriticalSection(void) {}

s32 ResetCallback(void) { return 0; }

s32 StopCallback(void) { return 0; }

/* ---- the link cable: nothing at the other end ---- */

void AddCOMB(void) { PORT_STUB("AddCOMB (link cable)"); }

void DelCOMB(void) { PORT_STUB("DelCOMB (link cable)"); }

/* Every question about the serial port is answered 0: no line is up,
 * nothing has arrived. */
s32 _comb_control(u32 command, u32 argument, u32 value) {
    PORT_STUB("_comb_control (link cable)");
    (void)command;
    (void)argument;
    (void)value;
    return 0;
}

/* ---- the C library ---- */

/* The compiler calls these two for a structure that is cleared or
 * compared; Sony's library has neither as C of its own. */
void *memset(void *dest, s32 value, u32 count) {
    u8 *to = dest;

    while (count-- != 0) {
        *to++ = (u8)value;
    }
    return dest;
}

s32 memcmp(const void *first, const void *second, u32 count) {
    const u8 *left = first;
    const u8 *right = second;

    for (; count-- != 0; left++, right++) {
        if (*left != *right) {
            return *left - *right;
        }
    }
    return 0;
}

#if !defined(__x86_64__)
#define BITS_PER_BYTE 8

/* The remainder of two 64-bit numbers, which a 32-bit x86 compiler calls
 * for `%` (the C compiler's own library has it, and this program links
 * none): Monocypher's Argon2 has one (include/port_monocypher.h), though
 * the port never calls it. Long division, a bit at a time. */
u64 __umoddi3(u64 dividend, u64 divisor) {
    u64 left = 0;
    s32 bit;

    if (divisor == 0) {
        return 0;
    }
    for (bit = (s32)(sizeof(dividend) * BITS_PER_BYTE) - 1; bit >= 0; bit--) {
        left = (left << 1) | ((dividend >> bit) & 1);
        if (left >= divisor) {
            left -= divisor;
        }
    }
    return left;
}
#endif

#define GAME_LINE_MAX 512

/* The game's printf goes to the development host on the console, where
 * nobody is listening on a retail machine. Here it goes to the error
 * output, marked as the game's. Sony's own walks a PlayStation's
 * argument list; this is the port's (text.c). */
void printf(const char *fmt, ...) {
    char line[GAME_LINE_MAX];
    PortArgs args;

    /* the step itself says it, when it runs (port_platform.h) */
    if (g_port_ahead) {
        return;
    }
    PORT_ARGS_START(args, fmt);
    port_FormatText(line, sizeof(line), fmt, args);
    PORT_ARGS_END(args);
    port_Log("game: %s", line);
}

/* As much room as the caller's buffer has is not known: as Sony's. */
#define SPRINTF_ROOM 0x7FFFFFFFU

s32 sprintf(char *buffer, char *fmt, ...) {
    PortArgs args;
    u32 length;

    PORT_ARGS_START(args, fmt);
    length = port_FormatText(buffer, SPRINTF_ROOM, fmt, args);
    PORT_ARGS_END(args);
    return (s32)length;
}

/* What Sony's libgpu prints its debugging through. */
void (*GPU_printf)() = (void (*)())printf;
