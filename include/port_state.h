#ifndef PORT_STATE_H
#define PORT_STATE_H

/* The native port's game state as one block of bytes, for rollback
 * (docs/NETPLAY.md, "Save, load and hash"): everything a step of the
 * game reads and writes, and nothing of what is shown or heard of it.
 *
 * What is saved, in this order:
 *
 *   - the console's 2 MB of RAM and its kilobyte of scratchpad;
 *   - the page of I/O registers the sound chip's are on;
 *   - the top 64 KB of the game's own stack, with what lies below its
 *     stack pointer cleared, and that stack pointer (port_host.h): the
 *     game is between two steps, a few calls deep, wherever it reads
 *     the pads (port_platform.h, "the step");
 *   - the port's own buffers of primitives, where a run has them
 *     (port_remaster.h): the pause menu draws the fight's last frame
 *     from them again every frame;
 *   - the program's variables but those of the files that
 *     config/port_state.txt puts outside the state, which are linked
 *     after the rest of their kind: the game's and Sony's variables, and the
 *     port's clock, drive, XA reader, sound chip and mixer, pads, card,
 *     events, GPU and video memory, GTE, movie decoder and a play test's
 *     plan.
 *
 * Every host pointer among the variables is one of three kinds, and each
 * survives a load: it points into the program's own variables or code,
 * which are at fixed addresses (no position independence) and are
 * themselves in the state or never move; or it was set as the program
 * started, before any save, and is the same in every snapshot (a hook,
 * a path, the plan's text); or it is in a file outside the state. The
 * code and the variables they point into are the core's (below), and so
 * is every address the stack's frames return to (src/port/fiber.c): a
 * snapshot is for a program of the same port_StateBuildSum, the same
 * program or one whose window alone differs (a replay's start, a
 * spectator's, with what is a run's own kept: port_replay.h).
 *
 * The state is whole from the game's first frame on: the sound chip's
 * page is mapped as the game starts its sound. */

#include "common.h"

/* ---- save, load and hash ---- */

/* The link's marks (tools/port_build.py, tools/port_sdl_build.py): each
 * kind of variable begins with the PlayStation's own, in its order, which
 * end at the first mark; the rest of the state ends at the second. */
extern char g_port_game_data[];
extern char g_port_game_data_end[];
extern char g_port_state_data_end[];
extern char g_port_game_bss[];
extern char g_port_game_bss_end[];
extern char g_port_state_bss_end[];
/* ... and the state's last files in each kind are the run's own
 * (config/port_state.txt's `run`), from these marks to the second. */
extern char g_port_run_data[];
extern char g_port_run_bss[];
/* ... and each kind's part of the game's object that is its core: the
 * game, Sony's code and the port's own, but not the window's files (in a
 * window's build they follow the core; tools/port_sdl_build.py,
 * BLOB_LD_TEXT). The code's sum is the core's bytes' and names'
 * (port_sdl_build.code_sum), 0 in a program with no window. */
extern char g_port_core_text[];
extern char g_port_core_text_end[];
extern char g_port_core_rodata[];
extern char g_port_core_rodata_end[];
extern char g_port_core_data[];
extern char g_port_core_data_end[];
extern char g_port_core_bss[];
extern char g_port_core_bss_end[];
extern const u64 g_port_code_sum;

/* Where the core lies in this program and, where the build knows it, what
 * it is (g_port_code_sum): two programs with the same sum run the same
 * game at the same addresses, so that a state one saved is the other's to
 * put in (a replay's start, a spectator's), its stack's return addresses
 * and the pointers among its variables included. What the window's code
 * is and where it lies is not in it (docs/PORT.md, "Replays"). */
extern u64 port_StateBuildSum(void);

/* The bytes a snapshot takes: the same all through a run. */
extern u32 port_StateSize(void);
/* The state now into `buf`, port_StateSize() bytes. */
extern void port_StateSave(void *buf);
/* The state of `buf` back, exactly as port_StateSave took it. */
extern void port_StateLoad(const void *buf);
/* The state saved for the frames run ahead after the step itself (step.c):
 * as port_StateSave, but for video memory, which is neither waited for
 * (the painters may still be painting the step) nor kept. What the frames
 * ahead write of it is kept as they write it (port_VramUndoBegin), and
 * port_StateLoad puts that back, with nothing waited for either. The
 * painters must be given nothing between the two. */
extern void port_StateSaveAhead(void *buf);
/* What a write of the GPU's side of the game's thread (a copy, a load, a
 * fill) is about to write over, kept for that load, once nothing still to
 * be painted is in its way (raster.c): FALSE where there is no room left,
 * and the step ahead is then given up before the write. */
extern s32 port_StateKeepVram(s32 x, s32 y, s32 w, s32 h);

/* The snapshot's pieces, in its order (a replay's file names them,
 * port_replay.h): the piece's name, where it lies in the program now,
 * its size, and where it begins in a snapshot. */
typedef struct {
    const char *name;
    u8 *start;
    u32 bytes;
    u32 offset;
} PortStatePiece;

extern s32 port_StatePieceCount(void);
extern void port_StateGetPiece(s32 piece, PortStatePiece *out);
/* Where a byte of the state lies in a snapshot, or PORT_STATE_NOWHERE for
 * one that is not the state's. */
#define PORT_STATE_NOWHERE 0xFFFFFFFFU
extern u32 port_StateOffsetOf(const void *address);
/* `buf`, saved into before, holds nothing this program knows of from now
 * on: its next save copies every page, and a load of it puts every page
 * back. For a snapshot whose bytes were changed after it was saved (a
 * replay's start, cleared of this run's own before it is written). */
extern void port_StateForget(const void *buf);
/* Every byte saved and put back each time, not only the pages written
 * since (--whole-state): what the state cost before, for measuring. */
extern void port_SetWholeState(void);
/* How many first writes to a page have faulted so far: what keeping the
 * pages written costs besides the copies. */
extern u32 port_StateFaults(void);
/* A hash of what the console itself would hold: its RAM, scratchpad,
 * sound registers and sound memory, the GTE's registers, and the game's
 * and Sony's variables (the PlayStation's own, which the console keeps
 * in its RAM). Not the port's bookkeeping, nor anything with a host
 * pointer that is not a variable's: two runs of one program on two PCs
 * give the same hash for the same state. */
extern u64 port_StateHash(void);

/* ---- the self-test (--rollback-test) ---- */

/* Every `every` frames of the game, save the state, run `frames` frames,
 * put it back, run them again, and stop the program at the first
 * difference of the state or of what the run printed; at the end, a
 * report of the rollbacks and of how long saving, loading and a step
 * took. `quiet`: the frames are run again as netplay runs them
 * (g_port_rerun, port_platform.h), and video memory, which they do not
 * paint, is not compared. */
extern void port_StartRollbackTest(u32 frames, u32 every, s32 quiet);
/* The game is at a frame's boundary, between two steps (src/port/
 * main.c; port_platform.h, "the step"): the round's next turn. The
 * game's stack and its stack pointer are in the state, so a load can be
 * made at any boundary, in main's loop or in one of its own (the pause
 * menu's). */
extern void port_OnRollbackFrame(void);
/* The self-test's report, as the program ends (port_Quit). */
extern void port_PrintRollbackReport(void);

#endif /* PORT_STATE_H */
