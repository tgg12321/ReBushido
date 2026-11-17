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
 *   - the port's own buffers of primitives, where a run has them
 *     (port_remaster.h): at the frame's boundary they hold the frame the
 *     GPU is about to be given;
 *   - the program's variables but those of the files the build names in
 *     OUTSIDE_STATE (tools/port_build.py), which are linked after the
 *     rest of their kind: the game's and Sony's variables, and the
 *     port's clock, drive, XA reader, sound chip and mixer, pads, card,
 *     events, GPU and video memory, GTE, movie decoder and a play test's
 *     plan.
 *
 * Every host pointer among the variables is one of three kinds, and each
 * survives a load: it points into the program's own variables or code,
 * which are at fixed addresses (no position independence) and are
 * themselves in the state or never move; or it was set as the program
 * started, before any save, and is the same in every snapshot (a hook,
 * a path, the plan's text); or it is in a file outside the state. A
 * snapshot is for the same program, in the same run: never written to a
 * file, never given to another build.
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

/* The bytes a snapshot takes: the same all through a run. */
extern u32 port_StateSize(void);
/* The state now into `buf`, port_StateSize() bytes. */
extern void port_StateSave(void *buf);
/* The state of `buf` back, exactly as port_StateSave took it. */
extern void port_StateLoad(const void *buf);
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
 * report of the rollbacks and of how long saving and loading took. */
extern void port_StartRollbackTest(u32 frames, u32 every);
/* Main's frame is at its boundary (main.c, after ResetRCnt), `parity`
 * the buffer of the two it is drawing with (its `index`). A load is only
 * made where the parity is the save's, so that what main's loop keeps
 * of the frame in its own variables is the snapshot's. */
extern void port_OnRollbackFrame(u32 parity);
/* The self-test's report, as the program ends (port_Quit). */
extern void port_PrintRollbackReport(void);

#endif /* PORT_STATE_H */
