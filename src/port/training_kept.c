/* What the training mode keeps outside the game's state (port_training.h):
 * the run's switches as the window or the command line asked for them,
 * the dummy's recorded slots and the moment kept. Outside the state, so
 * that a moment put back, run-ahead or a rollback takes none of them
 * away; the game's step reads them (src/port/training.c). */
#include "common.h"
#include "port_host.h"
#include "port_replay.h"
#include "bb2.h"

PortTrainingAreas g_port_training_areas;

static PortTrainingSwitches g_port_training_switches = {
    PORT_TRAINING_STAND, 0, FALSE, PORT_TRAINING_SLOW_LEAST};

/* The slots: each one's frames of the second pad's buttons, and how many
 * were recorded. */
static u16
    g_port_training_slots[PORT_TRAINING_SLOTS][PORT_TRAINING_SLOT_FRAMES];
static s32 g_port_training_lengths[PORT_TRAINING_SLOTS];

/* The moment: a snapshot of the state (port_state.h), made the first time
 * one is kept. */
static u8 *g_port_training_moment;
static s32 g_port_training_has_moment;
static s32 g_port_training_asked;
static u32 g_port_training_kept;
static u32 g_port_training_put_back;

/* The run's switches, each held to what it can be. */
void port_SetTrainingSwitches(const PortTrainingSwitches *switches) {
    PortTrainingSwitches *own = &g_port_training_switches;

    own->dummy = switches->dummy >= 0 && switches->dummy < PORT_TRAINING_DUMMIES
                     ? switches->dummy
                     : PORT_TRAINING_STAND;
    own->slot = switches->slot >= 0 && switches->slot < PORT_TRAINING_SLOTS
                    ? switches->slot
                    : 0;
    own->random = switches->random != 0;
    own->slow =
        switches->slow < PORT_TRAINING_SLOW_LEAST ? PORT_TRAINING_SLOW_LEAST
        : switches->slow > PORT_TRAINING_SLOW_MOST
            ? PORT_TRAINING_SLOW_MOST
            : switches->slow;
}

const PortTrainingSwitches *port_GetTrainingSwitches(void) {
    return &g_port_training_switches;
}

/* A frame of a slot written, the slot's length up to it; or, at -1, the
 * slot emptied. */
void port_TrainingKeep(s32 slot, s32 at, u16 buttons) {
    if (slot < 0 || slot >= PORT_TRAINING_SLOTS ||
        at >= PORT_TRAINING_SLOT_FRAMES) {
        return;
    }
    if (at < 0) {
        /* the slot emptied, to be recorded again */
        g_port_training_lengths[slot] = 0;
        return;
    }
    g_port_training_slots[slot][at] = buttons;
    g_port_training_lengths[slot] = at + 1;
}

s32 port_TrainingKeptLength(s32 slot) {
    return slot >= 0 && slot < PORT_TRAINING_SLOTS
               ? g_port_training_lengths[slot]
               : 0;
}

u16 port_TrainingKept(s32 slot, s32 at) {
    return slot >= 0 && slot < PORT_TRAINING_SLOTS && at >= 0 &&
                   at < g_port_training_lengths[slot]
               ? g_port_training_slots[slot][at]
               : 0;
}

void port_TrainingAsk(s32 what) { g_port_training_asked = what; }

/* The state's runs that are the run's own (config/port_state.txt's
 * `run`: a test's plan), which a moment put back leaves as they are: the
 * run's input goes on from where it is, as a player's hands do. */
static u8 *g_port_training_run;

/* Those runs copied aside (`out`), or back. */
static void port_TrainingRunCopy(s32 out) {
    u32 data = (u32)(g_port_state_data_end - g_port_run_data);
    u32 bss = (u32)(g_port_state_bss_end - g_port_run_bss);
    u32 i;

    if (g_port_training_run == NULL) {
        g_port_training_run = port_HostAllocate(data + bss + 1);
        if (g_port_training_run == NULL) {
            return;
        }
    }
    for (i = 0; i < data; i++) {
        if (out) {
            g_port_training_run[i] = (u8)g_port_run_data[i];
        } else {
            g_port_run_data[i] = (char)g_port_training_run[i];
        }
    }
    for (i = 0; i < bss; i++) {
        if (out) {
            g_port_training_run[data + i] = (u8)g_port_run_bss[i];
        } else {
            g_port_run_bss[i] = (char)g_port_training_run[data + i];
        }
    }
}

static u16 g_port_training_held;

void port_TrainingNoteHeld(u16 buttons) { g_port_training_held = buttons; }

static void port_TrainingPutBack(void) {
    u32 held[PORT_INPUT_PADS];

    /* the plan's runs, and the buttons it holds on the pads (pad.c) */
    port_GetScriptPads(held);
    port_TrainingRunCopy(TRUE);
    port_StateLoad(g_port_training_moment);
    port_TrainingRunCopy(FALSE);
    port_SetScriptPads(held);
    g_port_training.last = g_port_training_held;
}

/* At a frame's boundary, between two steps, where the state is whole: the
 * moment kept, or put back. */
void port_TrainingBoundary(void) {
    s32 what = g_port_training_asked;

    g_port_training_asked = 0;
    if (what == 0 || port_ReplayIsPlaying()) {
        return;
    }
    if (what == PORT_TRAINING_KEEP) {
        if (g_port_training_moment == NULL) {
            g_port_training_moment = port_HostAllocate(port_StateSize());
            if (g_port_training_moment == NULL) {
                port_Log("rebushido: training: no memory for a moment\n");
                return;
            }
        }
        port_StateSave(g_port_training_moment);
        g_port_training_has_moment = TRUE;
        g_port_training_kept++;
        port_Log("rebushido: training: the moment kept\n");
    } else if (what == PORT_TRAINING_PUT_BACK && g_port_training_has_moment) {
        port_TrainingPutBack();
        g_port_training_put_back++;
        port_Log("rebushido: training: the moment put back\n");
    }
}

u32 port_TrainingMomentsKept(void) { return g_port_training_kept; }

u32 port_TrainingMomentsPutBack(void) { return g_port_training_put_back; }
