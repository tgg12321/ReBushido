/* The rest of what the game asks of the console in the native port: the
 * BIOS's calls about the machine itself, Sony's interrupt set-up, the
 * link cable, and the C library where Sony's own C is not compiled
 * (docs/PORT.md, "Sony's libraries in the port"). */
#include "common.h"
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

/* ---- running another program: the movie ---- */

/* The game runs the movie player, a second PlayStation program it has
 * just read from the disc, and goes on when that returns. The port will
 * play the movie itself here (stage 7); until then there is no movie,
 * and the game goes on at once. */
s32 Exec(struct EXEC *exec, s32 argc, char **argv) {
    PORT_STUB("Exec (the movie player: no movie)");
    (void)exec;
    (void)argc;
    (void)argv;
    return 1;
}

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

/* The game's printf goes to the development host on the console, where
 * nobody is listening on a retail machine. Here it goes to the error
 * output, marked as the game's. Sony's own walks a PlayStation's
 * argument list; this is the port's (text.c). */
#define GAME_LINE_MAX 512

void printf(const char *fmt, ...) {
    char line[GAME_LINE_MAX];
    PortArgs args;

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
