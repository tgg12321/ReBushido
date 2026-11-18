/* The game as a step the host calls (port_platform.h; docs/NETPLAY.md,
 * "The step"). The game's main never returns: it runs on its own stack
 * as a fiber, and gives the host the turn at each frame's boundary,
 * which is where it reads the pads. The host's loop (main.c) then owns
 * the time: it runs a step, and between two steps it may save the
 * state, put an earlier one back, or set the next frame's input.
 *
 * Run-ahead is one use of that turn (docs/PORT.md, "Input lag"): before
 * a step, the state is saved, the next frames are run with the input the
 * step is about to take, drawn but not heard and with the window having
 * no turn, and the state is put back. What the window shows while the
 * step itself runs is the picture those frames left on the display:
 * the game's own frames, as many frames sooner. The step then runs as
 * it would have, heard and paced, and computes exactly what it would
 * have computed. */
#include "common.h"
#include "port_host.h"
#include "port_net.h"
#include "port_profile.h"
#include "bb2.h"

/* An ahead step that has passed this many blanks is given up: it is a
 * load or the movie, not a frame (a frame of a fight is two). */
#define AHEAD_BLANKS_A_FRAME 4
#define MICROSECONDS_A_MILLISECOND 1000U

u32 g_port_steps;
s32 g_port_rerun;
s32 g_port_ahead;
s32 g_port_unseen;
s32 g_port_ahead_kept;
/* the frames ahead the boundary before ran in full, or 0 */
static s32 g_port_ahead_last;
s32 g_port_ahead_held;
s32 g_port_ahead_unpainted;
void (*g_port_boundary_hook)(void);
void (*g_port_ahead_blank_hook)(u32 blank);
void (*g_port_ahead_done_hook)(void);
PortAheadCounts g_port_ahead_counts;

static s32 g_port_ahead_frames;
static u8 *g_port_ahead_state;
static s32 g_port_ahead_given_up;
static u32 g_port_ahead_blanks;

void port_StartGame(void (*function)(void)) {
    port_HostStartGame(function, PORT_STACK_ADDR + PORT_STACK_SIZE);
}

void port_StepGame(void) {
    port_HostEnterGame();
    g_port_steps++;
}

void port_EndStep(void) { port_HostLeaveGame(); }

/* ---- run-ahead ---- */

void port_SetRunAhead(s32 frames) {
    g_port_ahead_frames = frames < 0                ? 0
                          : frames > PORT_AHEAD_MAX ? PORT_AHEAD_MAX
                                                    : frames;
}

s32 port_GetRunAhead(void) { return g_port_ahead_frames; }

/* An ahead step stops where it is: the host puts the state back, so
 * nothing of where it stopped is kept (port_platform.h). */
void port_GiveUpAhead(void) {
    g_port_ahead_given_up = TRUE;
    port_EndStep();
}

/* A vertical blank in an ahead step (clock.c). */
void port_AheadBlank(u32 blank) {
    if (++g_port_ahead_blanks >
        (u32)g_port_ahead_frames * AHEAD_BLANKS_A_FRAME) {
        port_GiveUpAhead();
        return;
    }
    if (g_port_ahead_blank_hook != NULL) {
        g_port_ahead_blank_hook(blank);
    }
}

/* Whether frames may be run ahead at this boundary: not before the game
 * has first read the pads, not in a step run again, and never where a
 * step reaches another machine (the link cable; netplay has a loop of
 * its own and never comes here). */
static s32 port_MayRunAhead(void) {
    return g_port_ahead_frames > 0 && !g_port_ahead_held && !g_port_rerun &&
           g_port_input_count > 0 && !port_LinkIsUp() && !port_IsNetplayOn();
}

/* The host's turn before a step (port_platform.h): the window's read of
 * the PC's input, then the frames ahead, run from a saved state that is
 * put back after them. */
void port_BeforeStep(void) {
    u32 start;
    u32 loaded;
    s32 was;
    s32 i;

    g_port_unseen = FALSE;
    g_port_ahead_held = FALSE;
    g_port_ahead_kept = FALSE;
    if (g_port_boundary_hook != NULL) {
        g_port_boundary_hook();
    }
    if (!port_MayRunAhead()) {
        g_port_ahead_last = 0;
        return;
    }
    if (g_port_ahead_state == NULL) {
        g_port_ahead_state = port_HostAllocate(port_StateSize());
        if (g_port_ahead_state == NULL) {
            port_Log("rebushido: run-ahead: no memory for the state; off\n");
            g_port_ahead_frames = 0;
            return;
        }
    }
    start = port_HostTicks();
    was = port_ProfileSwitch(PORT_PROFILE_AHEAD);
    port_StateSave(g_port_ahead_state);
    g_port_ahead_counts.state_ticks += port_HostTicks() - start;
    g_port_ahead = TRUE;
    g_port_ahead_given_up = FALSE;
    g_port_ahead_blanks = 0;
    for (i = 0; i < g_port_ahead_frames && !g_port_ahead_given_up; i++) {
        /* With two frames ahead and more, all but the last were run by the
         * boundary before too, and drawn by the card then: drawing them
         * again cost the card half again (docs/PORT.md, "Measuring on the
         * card"). They are run all the same, for the state, and what they
         * write into video memory reaches the card's mirror. */
        g_port_ahead_kept = i + 1 < g_port_ahead_frames &&
                            g_port_ahead_last == g_port_ahead_frames;
        port_StepGame();
    }
    g_port_ahead_kept = FALSE;
    if (!g_port_ahead_given_up && g_port_ahead_done_hook != NULL) {
        g_port_ahead_done_hook();
    }
    g_port_ahead = FALSE;
    loaded = port_HostTicks();
    port_StateLoad(g_port_ahead_state);
    g_port_ahead_counts.state_ticks += port_HostTicks() - loaded;
    port_ProfileSwitch(was);
    /* the step that follows has been drawn already, unless the frames
     * ahead were given up: then it is drawn as ever */
    g_port_unseen = !g_port_ahead_given_up;
    g_port_ahead_last = g_port_ahead_given_up ? 0 : g_port_ahead_frames;
    g_port_ahead_counts.runs++;
    if (g_port_ahead_given_up) {
        g_port_ahead_counts.given_up++;
    }
    g_port_ahead_counts.ticks += port_HostTicks() - start;
}

/* How many boundaries ran frames ahead, how many gave them up, and what
 * a boundary cost (port_platform.h). */
void port_PrintAheadReport(void) {
    const PortAheadCounts *counts = &g_port_ahead_counts;
    u32 each;
    u32 state;

    if (counts->runs == 0) {
        return;
    }
    each = counts->ticks / counts->runs;
    state = counts->state_ticks / counts->runs;
    port_Log(
        "rebushido: run-ahead: %d frames at %u boundaries, %u given up "
        "(a load, the movie, a card); %u.%03u ms a boundary, %u.%03u of it "
        "the state kept and put back\n",
        g_port_ahead_frames, counts->runs, counts->given_up,
        each / PORT_HOST_TICKS_PER_MICROSECOND / MICROSECONDS_A_MILLISECOND,
        each / PORT_HOST_TICKS_PER_MICROSECOND % MICROSECONDS_A_MILLISECOND,
        state / PORT_HOST_TICKS_PER_MICROSECOND / MICROSECONDS_A_MILLISECOND,
        state / PORT_HOST_TICKS_PER_MICROSECOND % MICROSECONDS_A_MILLISECOND);
}
