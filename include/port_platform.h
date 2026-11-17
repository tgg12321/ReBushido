#ifndef PORT_PLATFORM_H
#define PORT_PLATFORM_H

/* The native port's platform layer (docs/PORT.md, "The program"): what
 * stands where the console, its BIOS and Sony's libraries stand on the
 * PlayStation. Sony's functions and the BIOS's keep the names Sony's
 * headers declare, and src/port/ defines them; what is declared here is
 * the port's own, which its files share. The game's C asks for one name
 * of these, port_Trap. The PlayStation builds never see this header. */

#include "common.h"

/* ---- stopping ---- */

/* The game cannot go on (sys_Trap): on the console it writes where there
 * is no memory, and the machine stops. The port's does not return. (It
 * is not declared so: the host tests have their own, which counts.) */
extern void port_Trap(void);
/* Ends the program: what was asked for at the end is written (the stubs
 * that were called, the memory to dump), then it exits with `code`. */
extern void port_Quit(s32 code) __attribute__((noreturn));
/* What the program exits with. */
#define PORT_EXIT_OK 0
#define PORT_EXIT_FAILED 1 /* it could not start, or the game stopped */
#define PORT_EXIT_USAGE 2

/* ---- the game's, and the compiler's ---- */

/* The game's own main (main.c), which the port's calls when the memory
 * and the disc are there. It does not return. */
extern void main(void);
/* The two functions the compiler itself calls, for a structure that is
 * cleared or compared: Sony's library has neither as C (src/port/
 * api.c). */
extern void *memset(void *dest, s32 value, u32 count);
extern s32 memcmp(const void *first, const void *second, u32 count);

/* ---- text (src/port/text.c) ---- */

/* A function's variable arguments. (The game's sys.h has its own va_list,
 * which walks a PlayStation's stack; the port's files use the
 * compiler's.) */
typedef __builtin_va_list PortArgs;
#define PORT_ARGS_START(args, last) __builtin_va_start(args, last)
#define PORT_ARGS_END(args) __builtin_va_end(args)
#define PORT_ARG(args, type) __builtin_va_arg(args, type)

/* printf's work, as far as the game and the port use it: %d %i %u %x %X
 * %o %c %s %p %%, with a width (or *), `-`, `0`, a precision and an `l` or
 * `h` that changes nothing. Writes at most `room` bytes with the 0 that
 * ends them, and returns how many characters it wrote before that 0. */
extern u32 port_FormatText(
    char *out, u32 room, const char *format, PortArgs args);
/* To the program's output: what a run is asked to print (--trace, a
 * plan's lines, the stubs). Kept until there is a buffer of it. */
extern void port_Print(const char *format, ...)
    __attribute__((format(printf, 1, 2)));
extern void port_FlushOutput(void);
/* Where it is set, what port_Print is given goes to this first, and is
 * printed only if it answers 1: the rollback self-test's look at what a
 * run prints (port_state.h). */
extern s32 (*g_port_print_tap)(const char *text, u32 length);
/* To the error output, at once: what went wrong, and the game's own
 * printf. */
extern void port_Log(const char *format, ...)
    __attribute__((format(printf, 1, 2)));
/* A message, then port_Quit(PORT_EXIT_FAILED). */
extern void port_Fail(const char *format, ...)
    __attribute__((format(printf, 1, 2), noreturn));

extern u32 port_TextLength(const char *text);
extern s32 port_TextsEqual(const char *first, const char *second);
/* Whether `text` ends with `ending`, letters of either case. */
extern s32 port_TextEndsWith(const char *text, const char *ending);
/* A whole number, decimal or 0x and hexadecimal, with or without a `-`,
 * that is all of `text`: 1 and the number, or 0. */
extern s32 port_ParseNumber(const char *text, s32 *value);

/* ---- stubs (src/port/stub.c) ---- */

/* A function of Sony's or of the BIOS that the port does not do yet: it
 * counts that it was called and gives a harmless answer. The list of them
 * with their counts is what is left to write. */
typedef struct PortStub {
    const char *name;
    u32 calls;
    struct PortStub *next; /* the next one in the order they were first
                            * called */
} PortStub;

extern void port_CountStub(PortStub *stub);
/* In a stub's body: counts a call of it under `NAME`. */
#define PORT_STUB(NAME)                                                        \
    do {                                                                       \
        static PortStub port_stub = {NAME, 0, NULL};                           \
        port_CountStub(&port_stub);                                            \
    } while (0)
/* A line for each stub that was called, in the order of the first calls:
 * `stub NAME COUNT`. */
extern void port_PrintStubs(void);

/* ---- memory (src/port/memory.c) ---- */

/* The PlayStation's memory, at the PlayStation's addresses: the 2 MB of
 * RAM and the kilobyte of scratchpad. */
#define PORT_RAM_ADDR 0x80000000U
#define PORT_RAM_SIZE 0x200000U
#define PORT_SCRATCHPAD_SIZE 0x400U
/* Zeroes, for the few places where the game reads at address 0, which
 * a PC will not give a program (docs/PORT.md, "Memory"): the console has
 * its kernel's memory there, whose first words this stands for. Nothing
 * writes it. */
#define PORT_LOW_MEMORY_WORDS 16
extern const u32 g_port_low_memory[PORT_LOW_MEMORY_WORDS];
/* A stack of the port's own for the game, at an address that is the
 * same in every run: what the game leaves on its stack and reads again
 * is then the same too, and a primitive on the stack (an ordering table
 * of a function's own) has an address whose low 24 bits are no other
 * memory's (port_gpu.h, port_AddLinkRegion). */
#define PORT_STACK_ADDR 0x7FC00000U
#define PORT_STACK_SIZE 0x100000U
/* Where the port's own two buffers of primitives go, for a run that
 * asks the remastered picture to draw more of the stage than the
 * console does (port_remaster.h, PORT_PRIM_BUFFER_BYTES): the
 * console's own two cannot grow where they are, with the stack above
 * them and the staging area below (include/sys.h). As with the stack,
 * the address is the same in every run and its low 24 bits are no
 * other memory's; it is given memory only for such a run, and with
 * the console's draw distance nothing of it exists. */
#define PORT_PRIMS_ADDR 0x7ED00000U
#define PORT_PRIMS_SIZE 0x200000U
/* Where the program's own variables begin and end: the link's script
 * says (tools/port_build.py). */
extern char g_port_data_start[];
extern char g_port_data_end[];
/* Gives all three memory; stops the program, saying why, if the system
 * will not. */
extern void port_MapMemory(void);
/* Whether `count` bytes from `address` are in one of the two. */
extern s32 port_IsMapped(u32 address, u32 count);

/* ---- time (src/port/clock.c) ---- */

/* The port's clock is a count of scan lines that moves only when the
 * game waits (VSync) or looks at its counter (GetRCnt); a vertical blank
 * comes every PORT_LINES_PER_BLANK of them. */
#define PORT_LINES_PER_BLANK 263
/* Vertical blanks since the program started, and scan lines. */
extern u32 port_GetBlankCount(void);
extern u32 port_GetLineCount(void);
/* Moves the clock on one scan line, running a vertical blank if one
 * falls there: for a wait that is not the game's own VSync (the movie's
 * wait on the drive, src/port/stream.c). */
extern void port_WaitOneLine(void);

/* ---- the pads (src/port/pad.c) ---- */

#define PORT_PADS 2

/* What the game reads of both pads at one of its reads: the input of a
 * frame, a value that can be kept, sent and played again (docs/
 * NETPLAY.md, "Input"). The game reads a digital pad's sixteen buttons
 * and never a stick, so that is all there is. */
typedef struct PortInput {
    u16 buttons[PORT_PADS]; /* Sony's PAD... masks, a bit set for each held */
    u8 present[PORT_PADS];  /* 0: the pad's packet says that none answered */
} PortInput;

/* A button of pad 0 or 1 goes down or comes up for a run's script; the
 * game sees it from the next vertical blank. `button` is one of Sony's
 * PAD... masks (libetc.h). */
extern void port_SetPadButton(s32 pad, u32 button, s32 down);
/* The window's keys and controllers (src/port/window/): the buttons
 * they hold on a pad now. NULL in the build without a window. */
extern u32 (*g_port_device_hook)(s32 pad);
/* The pads as they are now: the script's buttons and the window's. */
extern void port_SampleInput(PortInput *input);
/* At each vertical blank: the console's driver reads the pads, and what
 * it read is what the game finds until the next blank. */
extern void port_FillPads(void);
/* What the driver read at the last blank: the input the game would take
 * now, but for g_port_take_hook (netplay's local pad, netplay.c). */
extern void port_DriverInput(PortInput *input);
/* The PC's keys and controllers read again into what the driver read at
 * the last blank, the script's buttons as they were then: what the
 * window does just before the game takes the input, so that a press is
 * taken as late as it can be (docs/PORT.md, "Input lag"). */
extern void port_ReadDevicesNow(void);
/* The one place the game's input comes from: called as the game reads
 * the pads (pad_Update, and the options page's own read), it takes the
 * input the driver read at the last blank, lets g_port_take_hook replace
 * it, keeps it in g_port_input and writes it into the two buffers the
 * game gave InitPAD, from which the game reads it. */
extern void port_TakeInput(void);
/* Given each input before the game reads it, and may change it: a replay
 * or the other player's, over a network. NULL: the pads' own. */
extern void (*g_port_take_hook)(PortInput *input);
/* The input the game read last, and how many it has read. */
extern PortInput g_port_input;
extern u32 g_port_input_count;
/* The movie player reads the pads its own way (PAD_dr), at moments no
 * other machine shares. Netplay sets this before the game starts: every
 * movie is then skipped where it begins, on both machines alike (docs/
 * NETPLAY.md, section 13). */
extern s32 g_port_movies_skipped;

/* ---- the step (src/port/step.c; docs/NETPLAY.md, "The step") ---- */

/* The game runs on its own stack as a fiber (port_host.h), and the host
 * runs it a step at a time. A step ends at the game's next frame
 * boundary, which is where it reads the pads: port_TakeInput ends the
 * step before it takes the input, so the host has the turn between a
 * frame's input being read by the pads' driver and taken by the game.
 * Main's loop, the pause menu, the link cable's connecting and the
 * options page each read the pads once a frame, so a step is one of
 * their frames; a frame that does not read them (a load, the movie, the
 * boot picture) is part of the step it falls in. Nothing of the game
 * runs between steps, and its stack holds its locals, so the state can
 * be saved, put back and hashed there (port_state.h). */
extern void port_StartGame(void (*function)(void));
/* Runs the game to its next frame boundary. */
extern void port_StepGame(void);
/* The game is at a frame's boundary: back to the host (pad.c). */
extern void port_EndStep(void);
/* Steps the host has run since the game started. */
extern u32 g_port_steps;
/* Set by the host while it runs a step again after putting an earlier
 * state back (rollback): the step computes all the game's state as it
 * would, and nothing of what is seen or heard of it. The GPU keeps its
 * registers and takes loads into video memory but paints no pixel
 * (raster.c), the remastered renderer is given no command, the sound is
 * mixed but neither the card nor the file is given it (sound.c), the
 * window has no turn at a blank (clock.c), a run writes no picture and
 * prints nothing (plan.c, text.c). docs/NETPLAY.md, section 10. */
extern s32 g_port_rerun;

/* Run-ahead (step.c; docs/PORT.md, "Input lag"): before each step the
 * host saves the state, runs the next `frames` steps with the input the
 * step is about to take, and puts the state back. Those steps are drawn
 * but not heard: the GPU paints and the remastered renderer is given
 * every command, the sound is mixed but given to nobody, the window has
 * no turn at a blank but g_port_ahead_blank_hook, a run writes no
 * picture and prints nothing, a plan's press is not made as the PC's
 * input, and a memory card's write or the program's end gives them up
 * (port_GiveUpAhead). The step itself then runs as it would, but the
 * remastered renderer is not given what it was given already
 * (g_port_unseen). 0 to PORT_AHEAD_MAX frames; 0 is off. */
#define PORT_AHEAD_MAX 2
extern void port_SetRunAhead(s32 frames);
extern s32 port_GetRunAhead(void);
/* An ahead step runs (g_port_ahead), and the step after the frames ahead
 * whose pictures they drew (g_port_unseen). */
extern s32 g_port_ahead;
extern s32 g_port_unseen;
/* The host's turn before a step (main.c): g_port_boundary_hook, then the
 * frames ahead. */
extern void port_BeforeStep(void);
/* A blank in an ahead step (clock.c), and an ahead step stopped where it
 * is: the state is put back. */
extern void port_AheadBlank(u32 blank);
extern void port_GiveUpAhead(void);
/* The window's: before every step, where it reads the PC's input again;
 * at a blank of an ahead step; and when the frames ahead are done, with
 * the state they left still in place. */
extern void (*g_port_boundary_hook)(void);
extern void (*g_port_ahead_blank_hook)(u32 blank);
extern void (*g_port_ahead_done_hook)(void);
typedef struct PortAheadCounts {
    u32 runs;     /* boundaries the frames were run ahead at */
    u32 given_up; /* ... and given up at: a load, the movie, a card */
    u32 ticks;    /* the time all of it took, in the host's ticks */
} PortAheadCounts;
extern PortAheadCounts g_port_ahead_counts;
/* What the frames ahead came to, on the error output, as the run ends. */
extern void port_PrintAheadReport(void);

/* ---- the link cable (src/port/net.c; docs/NETPLAY.md, section 11) ---- */

/* The game's link mode (src/main/link.c) over a socket to another
 * program of the port: whether the run was given a side (--link-side)
 * and a network, and which side it is. */
extern s32 port_LinkIsUp(void);
extern s32 port_LinkSide(void);
/* The two programs meet as they connect: 1 once each has heard the
 * other, waited for in the PC's time; 0 if the other never came. */
extern s32 port_LinkMeet(void);
/* The game's packet, its two words, to the other program: 1. */
extern s32 port_LinkSend(s32 first, s32 second);
/* The other program's next packet into `words`: 1, or 0 if it has gone. */
extern s32 port_LinkReceive(s32 *words);

/* ---- events (src/port/event.c) ---- */

/* The BIOS's DeliverEvent: every open, enabled event of this class and
 * kind is set, for TestEvent to find. */
extern void port_DeliverEvent(u32 desc, s32 spec);

/* ---- the disc (src/port/cd.c) ---- */

struct PortDisc;
/* The disc in the drive: Sony's libcd reads its sectors from this image.
 * NULL takes it out. */
extern void port_InsertDisc(struct PortDisc *disc);

/* ---- a run's script (src/port/plan.c) ---- */

/* Reads the names the build wrote beside the program (tools/
 * port_build.py): 1, or 0 if the file is not there. */
extern s32 port_LoadNames(const char *path);
/* The function `address` is in, and how far into it: NULL if the names
 * have none. */
extern const char *port_FindFunction(u32 address, u32 *offset);
/* Reads a play test's plan (tools/play/runner.lua has the format): the
 * presses and the checkpoints of a run. Stops the program if it cannot. */
extern void port_LoadPlan(const char *path);
/* The run ends after this many vertical blanks (--frames). */
extern void port_SetBlankLimit(u32 blanks);
/* Print the values of tests/play/state.toml at every vertical blank where
 * one has changed (--trace). */
extern void port_SetTrace(s32 on);
/* A piece of memory to write to a file when the run ends (--dump). */
extern void port_AddDump(const char *name, u32 address, u32 size);
/* The folder they go into, with its last `/` (--out). */
extern void port_SetDumpFolder(const char *folder);
extern void port_WriteDumps(void);
/* A PNG of the picture into this folder (with its last `/`) every
 * `every` vertical blanks (--shots, --every). */
extern void port_SetShots(const char *folder, u32 every);
/* All of video memory to this file when the run ends (--vram). */
extern void port_SetVramPath(const char *path);
/* Called at every vertical blank: presses the plan's buttons, looks for
 * its next checkpoint, prints the trace, ends the run at its limit. */
extern void port_OnBlank(u32 blank);
/* Called as each frame of the game begins, which is where the game
 * resets its counter of scan lines: the plan's presses and checkpoints
 * that are counted in the game's frames. */
extern void port_OnFrame(void);
/* What the compiler calls as each function of the game's files begins
 * and ends (tools/port_build.py compiles them with
 * -finstrument-functions): a checkpoint that waits for a call of a
 * function, and the list of a frame's calls. */
extern void __cyg_profile_func_enter(void *function, void *call_site);
extern void __cyg_profile_func_exit(void *function, void *call_site);

/* ---- the window (src/port/window/; docs/PORT.md, "The window") ---- */

/* What the build with a window hangs on the port. Each is NULL in the
 * build without one, and then nothing is called: that build does what
 * it did. None of them changes what the game computes. */
/* At a vertical blank, after the plan's presses and before the driver
 * reads the pads: the PC's keys and controllers are read, and
 * g_port_device_hook ("the pads") says what they hold. */
extern void (*g_port_input_hook)(u32 blank);
/* At a vertical blank's end, when its sound is made: the picture is
 * shown, and the blank waited for by the PC's clock. */
extern void (*g_port_blank_hook)(u32 blank);
/* A press of a plan's, in place of port_SetPadButton if it returns 1:
 * the window presses a key or a controller's button for it, so that a
 * plan can go through the PC's own input. */
extern s32 (*g_port_press_hook)(s32 pad, u32 button, s32 down);
/* As port_Quit ends the program, after what it writes, with the code
 * it ends with. */
extern void (*g_port_quit_hook)(s32 code);
/* After a fault's report (crash.c): what the session's log adds to it. */
extern void (*g_port_crash_hook)(void);
/* The disc's drive begins to read data at a sector (`kind` 1), begins
 * to play a stream of sound there (2), or stops reading at one (0). */
extern void (*g_port_disc_hook)(s32 kind, u32 sector);
/* The last functions of the game that began, for a fault's report: a
 * ring, the newest at (g_port_recent_call_count - 1) modulo its size. */
#define PORT_RECENT_CALLS 32
extern u32 g_port_recent_calls[PORT_RECENT_CALLS];
extern u32 g_port_recent_call_count;

/* ---- a crash (src/port/crash.c) ---- */

/* From now on a fault says where it was: the function and its callers. */
extern void port_CatchCrashes(void);

#endif /* PORT_PLATFORM_H */
