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
 * have computed.
 *
 * Where the host holds a step's blanks (the window, g_port_held_blank_hook)
 * the step itself is the first of those frames, since it computes what the
 * first frame ahead would: its blanks' turns of the window wait for its end,
 * the frames ahead but one run after it, and then the blanks show what they
 * left (docs/PORT.md, "The step itself as the first frame ahead"). */
#include "common.h"
#include "port_host.h"
#include "port_net.h"
#include "port_paint.h"
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
/* the frames the steps ahead being run are, whose blanks they may pass */
static s32 g_port_ahead_running;

void port_StartGame(void (*function)(void)) {
    port_HostStartGame(function, PORT_STACK_ADDR + PORT_STACK_SIZE);
}

/* The window's way of running a step whose blanks it holds: on a thread
 * of its own beside the window's pictures (port_platform.h). */
void (*g_port_step_runner)(void);
void (*g_port_step_to_host)(void);
void (*g_port_foreign_call_hook)(u32 function);

void port_StepGame(void) {
    if (g_port_holding && g_port_step_runner != NULL) {
        g_port_step_runner();
    } else {
        port_HostEnterGame();
    }
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
        (u32)g_port_ahead_running * AHEAD_BLANKS_A_FRAME) {
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

/* `frames` frames run ahead from a saved state that is put back after
 * them; `drawn_before` is how many of them, the last but one and before,
 * the boundary before ran and the card drew. TRUE where they were run to
 * the end, FALSE where they were given up. */
static s32 port_RunAhead(s32 frames, s32 drawn_before, s32 is_after_step) {
    u32 start;
    u32 loaded;
    s32 was;
    s32 i;

    if (g_port_ahead_state == NULL) {
        g_port_ahead_state = port_HostAllocate(port_StateSize());
        if (g_port_ahead_state == NULL) {
            port_Log("rebushido: run-ahead: no memory for the state; off\n");
            g_port_ahead_frames = 0;
            return FALSE;
        }
    }
    start = port_HostTicks();
    was = port_ProfileSwitch(PORT_PROFILE_AHEAD);
    /* After the step itself, with the frames ahead painting nothing, the
     * painters may go on painting the step while they run: video memory
     * is put back by what the frames write of it (port_state.h). */
    if (is_after_step && g_port_ahead_unpainted) {
        port_StateSaveAhead(g_port_ahead_state);
    } else {
        port_StateSave(g_port_ahead_state);
    }
    g_port_ahead_counts.state_ticks += port_HostTicks() - start;
    g_port_ahead = TRUE;
    g_port_ahead_given_up = FALSE;
    g_port_ahead_blanks = 0;
    g_port_ahead_running = g_port_ahead_frames;
    for (i = 0; i < frames && !g_port_ahead_given_up; i++) {
        /* With two frames ahead and more, all but the last were run by the
         * boundary before too, and drawn by the card then: drawing them
         * again cost the card half again (docs/PORT.md, "Measuring on the
         * card"). They are run all the same, for the state, and what they
         * write into video memory reaches the card's mirror. */
        g_port_ahead_kept = i + 1 < frames && drawn_before;
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
    g_port_ahead_counts.runs++;
    if (g_port_ahead_given_up) {
        g_port_ahead_counts.given_up++;
    }
    g_port_ahead_counts.ticks += port_HostTicks() - start;
    return !g_port_ahead_given_up;
}

/* A look of the theatre's (port_platform.h, port_theatre.h): the state
 * `from` put in, `frames` steps run from it as frames ahead are run (drawn,
 * not heard, the window given no turn but the ahead blank's and the
 * done's), and the state `back` put in after them. TRUE where they ran to
 * the end, FALSE where they were given up (a load). */
s32 port_RunLook(const void *from, s32 frames, const void *back) {
    s32 was = port_ProfileSwitch(PORT_PROFILE_AHEAD);
    s32 i;

    port_StateLoad(from);
    /* a look again while the game waits comes after a step unseen: its
     * own steps are the card's to draw */
    g_port_unseen = FALSE;
    g_port_ahead = TRUE;
    g_port_ahead_kept = FALSE;
    g_port_ahead_given_up = FALSE;
    g_port_ahead_blanks = 0;
    g_port_ahead_running = frames;
    for (i = 0; i < frames && !g_port_ahead_given_up; i++) {
        /* The display a step leaves is the frame the step before it drew
         * (the game draws one buffer while the other is shown): the last
         * step is run for the display it puts up, and the card is given
         * only its writes into video memory, so that it draws one frame a
         * look, as it draws one a step, and the picture's own clock (the
         * flames, the sky, the dressing) keeps the game's pace. */
        g_port_ahead_kept = i + 1 == frames && frames > 1;
        port_StepGame();
    }
    g_port_ahead_kept = FALSE;
    if (!g_port_ahead_given_up && g_port_ahead_done_hook != NULL) {
        g_port_ahead_done_hook();
    }
    g_port_ahead = FALSE;
    port_StateLoad(back);
    port_ProfileSwitch(was);
    return !g_port_ahead_given_up;
}

/* ---- the step itself as the first frame ahead ---- */

s32 (*g_port_held_blank_hook)(u32 blank);
s32 g_port_holding;
s32 g_port_ahead_sync = TRUE;
s32 g_port_ahead_quiet = TRUE;
/* The blanks of the step whose window's turns wait for its end. A step
 * of a fight is two blanks; one that passes this many is a load or the
 * movie, and is held no longer. */
#define HELD_BLANKS_MAX AHEAD_BLANKS_A_FRAME
static u32 g_port_held[HELD_BLANKS_MAX];
static s32 g_port_held_count;
void (*g_port_held_begin_hook)(void);

/* The held blanks given their window's turns, one after another:
 * showing what the frames ahead left where `is_ahead_shown`, and
 * otherwise as any step's blank shows. */
static void port_GiveHeldBlanks(s32 is_ahead_shown) {
    s32 i;

    g_port_holding = FALSE;
    g_port_unseen = is_ahead_shown;
    for (i = 0; i < g_port_held_count; i++) {
        if (g_port_blank_hook != NULL) {
            g_port_blank_hook(g_port_held[i]);
        }
    }
    g_port_held_count = 0;
    g_port_unseen = FALSE;
}

/* A blank of a step whose window's turns are held (clock.c): kept for
 * after the step, unless it is one too many or the window ends the
 * holding (a load begins). The step's turns are then given at once, the
 * blank's own among them, and the rest of the step is as any step: no
 * frame is run ahead after it. */
void port_HeldBlank(u32 blank) {
    if (g_port_held_count < HELD_BLANKS_MAX &&
        (g_port_held_blank_hook == NULL || g_port_held_blank_hook(blank))) {
        g_port_held[g_port_held_count++] = blank;
        return;
    }
    /* the window's turns are the window's thread's: a step run beside it
     * goes on there (g_port_step_to_host) */
    if (g_port_step_to_host != NULL) {
        g_port_step_to_host();
    }
    /* the card draws the rest of the step as it comes, and nothing is
     * run ahead after it */
    g_port_ahead_kept = FALSE;
    g_port_ahead_last = 0;
    port_GiveHeldBlanks(FALSE);
    if (g_port_blank_hook != NULL) {
        g_port_blank_hook(blank);
    }
}

/* What port_AfterStepAhead found, where the step's runner ran it
 * (AHEAD_NOT_RUN: it did not). */
#define AHEAD_NOT_RUN (-2)
#define AHEAD_NOT_HELD (-1)
static s32 g_port_ahead_part = AHEAD_NOT_RUN;

/* The frames ahead after a step whose blanks were held (port_platform.h):
 * TRUE where they were run to the end, FALSE where they were given up,
 * AHEAD_NOT_HELD where the step's blanks were not held. */
s32 port_AfterStepAhead(void) {
    s32 is_shown;

    if (!g_port_holding) {
        return g_port_ahead_part = AHEAD_NOT_HELD;
    }
    g_port_holding = FALSE;
    /* the step itself was drawn as a frame ahead's last one, or written
     * into the card's mirror alone (g_port_ahead_kept) */
    g_port_ahead_kept = FALSE;
    if (g_port_ahead_frames > 1) {
        is_shown = port_RunAhead(g_port_ahead_frames - 1, FALSE, TRUE);
    } else {
        if (g_port_ahead_done_hook != NULL) {
            g_port_ahead_done_hook();
        }
        is_shown = TRUE;
    }
    g_port_ahead_last = is_shown ? g_port_ahead_frames : 0;
    return g_port_ahead_part = is_shown;
}

/* The host's turn after a step (port_platform.h): where the step was the
 * first frame ahead, the frames ahead but one, the step's display kept,
 * and the blanks' turns given. */
void port_AfterStep(void) {
    s32 is_shown = g_port_ahead_part;

    if (is_shown == AHEAD_NOT_RUN) {
        is_shown = port_AfterStepAhead();
    }
    g_port_ahead_part = AHEAD_NOT_RUN;
    if (is_shown == AHEAD_NOT_HELD) {
        return;
    }
    if (g_port_ahead_sync) {
        /* the step's primitives, which the painters painted while the side
         * channel decoded them, the card was given the frame and the
         * frames ahead ran, all painted before a picture reads video
         * memory, as at any step's blank */
        s32 was = port_ProfileSwitch(PORT_PROFILE_RASTER);

        port_PaintSyncAll();
        port_ProfileSwitch(was);
    }
    port_GiveHeldBlanks(is_shown);
}

/* The host's turn before a step (port_platform.h): the window's read of
 * the PC's input, then the frames ahead, run from a saved state that is
 * put back after them; or, where the window holds a step's blanks, the
 * step itself as the first of them (port_AfterStep). */
void port_BeforeStep(void) {
    s32 is_drawn;

    g_port_unseen = FALSE;
    g_port_ahead_held = FALSE;
    g_port_ahead_kept = FALSE;
    if (g_port_boundary_hook != NULL) {
        g_port_boundary_hook();
    }
    /* the theatre's look (port_theatre.h): the frame on show drawn again,
     * from the free camera or without the HUD, and the step itself unseen,
     * as after frames run ahead; none is run ahead beside it */
    port_TheatreKeep();
    if (port_TheatreIsOn()) {
        g_port_unseen = port_TheatreLook();
        g_port_ahead_last = 0;
        return;
    }
    if (!port_MayRunAhead()) {
        g_port_ahead_last = 0;
        return;
    }
    if (g_port_held_blank_hook != NULL) {
        /* The step computes what the first frame ahead would have, with
         * the same input: it is that frame. With more than one ahead and
         * the boundary before having run them all, the card drew it then
         * as its last, and is given only its writes into video memory. */
        g_port_holding = TRUE;
        g_port_held_count = 0;
        g_port_ahead_kept =
            g_port_ahead_frames > 1 && g_port_ahead_last == g_port_ahead_frames;
        if (g_port_held_begin_hook != NULL) {
            g_port_held_begin_hook();
        }
        return;
    }
    is_drawn = port_RunAhead(
        g_port_ahead_frames, g_port_ahead_last == g_port_ahead_frames, FALSE);
    /* the step that follows has been drawn already, unless the frames
     * ahead were given up: then it is drawn as ever */
    g_port_unseen = is_drawn;
    g_port_ahead_last = is_drawn ? g_port_ahead_frames : 0;
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
        "the state kept and put back, besides %u first writes to its "
        "pages, which fault\n",
        g_port_ahead_frames, counts->runs, counts->given_up,
        each / PORT_HOST_TICKS_PER_MICROSECOND / MICROSECONDS_A_MILLISECOND,
        each / PORT_HOST_TICKS_PER_MICROSECOND % MICROSECONDS_A_MILLISECOND,
        state / PORT_HOST_TICKS_PER_MICROSECOND / MICROSECONDS_A_MILLISECOND,
        state / PORT_HOST_TICKS_PER_MICROSECOND % MICROSECONDS_A_MILLISECOND,
        port_StateFaults() / counts->runs);
}
