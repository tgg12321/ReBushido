/* The native port's training mode (port_training.h; docs/PORT.md,
 * "Training"): what a fight of training is, as the game's state. A state
 * file: what is here is saved, put back and hashed with the game's own
 * variables, so a moment put back puts the dummy, the freeze and the
 * presses shown back with the fighters. The recordings are kept outside
 * the state (training_kept.c). */
#include "common.h"
#include "bb2.h"

/* The game's mark of a blade frame not in the move (Fighter's
 * hit_start_frames). */
#define NO_FRAME 0xFF
/* The dummy that guards presses its answer from this many frames before
 * the blow can land to the frame it can (the game's CPU from the one
 * before: cpu.c's threat, other->move_frame + 1 >= its hit frame). */
#define GUARD_LEAD 1
/* Training's own numbers for a random slot: a linear congruential
 * generator's (Numerical Recipes'), never the game's rng. */
#define SEED_MUL 1664525U
#define SEED_ADD 1013904223U
#define SEED_START 0x5EED7A11U
#define SEED_SHIFT 16

#define PAD_BUTTONS 0xFFFF

PortTraining g_port_training;

s32 port_TrainingIsOn(void) { return g_port_training.is_on; }

/* A mode chosen on the main menu: training afresh, nothing recorded,
 * played or held from before but what is kept outside the state; or not
 * training. Never in a session. */
void port_TrainingChoose(s32 on) {
    PortTraining *training = &g_port_training;
    u32 i;

    /* a fresh training: nothing recorded, played or held from before but
     * what training keeps outside the state */
    for (i = 0; i < sizeof(*training); i++) {
        ((u8 *)training)[i] = 0;
    }
    training->is_on = (u8)(on != 0 && !port_SessionIsOn());
    training->seed = SEED_START;
}

void port_TrainingPause(s32 up) { g_port_training.in_pause = (u8)(up != 0); }

/* ---- the presses shown ---- */

static void port_TrainingShow(s32 pad, u16 buttons) {
    PortTraining *training = &g_port_training;
    PortTrainingPress *shown = training->shown[pad];
    s32 i;

    if (training->shown_count[pad] != 0 && shown[0].buttons == buttons) {
        if (shown[0].frames < PAD_BUTTONS) {
            shown[0].frames++;
        }
        return;
    }
    for (i = PORT_TRAINING_SHOWN - 1; i > 0; i--) {
        shown[i] = shown[i - 1];
    }
    shown[0].buttons = buttons;
    shown[0].frames = 1;
    if (training->shown_count[pad] < PORT_TRAINING_SHOWN) {
        training->shown_count[pad]++;
    }
}

/* ---- the dummy ---- */

/* The guard: as the game's CPU answers an attack about to land (cpu.c,
 * the threat's guard scripts g_cpu_script_guard_a and _b), the frontal
 * attack's button against an attack begun with the second kind, the
 * reverse attack's against one begun with the first, pressed from a frame
 * before the blow can land. Those are Circle and Cross in the game's own
 * button configuration (pad.c, pad_ResetConfig). */
static u16 port_TrainingGuard(void) {
    const Fighter *player = &g_fighters[0];
    s32 start = player->hit_start_frames[0] != NO_FRAME
                    ? player->hit_start_frames[0]
                    : player->hit_start_frames[1];

    if (!player->attack_armed || start == NO_FRAME ||
        player->move_frame + GUARD_LEAD < start || player->move_frame > start) {
        return 0;
    }
    return (player->attack_kind & 1) ? FIGHTER_PAD_FRONTAL_ATTACK
                                     : FIGHTER_PAD_REVERSE_ATTACK;
}

/* The slot the playback plays next: the window's, or one of those
 * recorded at random. -1 where none has anything. */
static s32 port_TrainingNextSlot(void) {
    PortTraining *training = &g_port_training;
    const PortTrainingSwitches *switches = port_GetTrainingSwitches();
    s32 kept[PORT_TRAINING_SLOTS];
    s32 count = 0;
    s32 i;

    if (!switches->random) {
        return port_TrainingKeptLength(switches->slot) > 0
                   ? switches->slot
                   : -1;
    }
    for (i = 0; i < PORT_TRAINING_SLOTS; i++) {
        if (port_TrainingKeptLength(i) > 0) {
            kept[count++] = i;
        }
    }
    if (count == 0) {
        return -1;
    }
    training->seed = training->seed * SEED_MUL + SEED_ADD;
    return kept[(training->seed >> SEED_SHIFT) % (u32)count];
}

static void port_TrainingPlayFromStart(void) {
    PortTraining *training = &g_port_training;
    s32 slot = port_TrainingNextSlot();

    training->playing = (u8)(slot >= 0 ? slot + 1 : 0);
    training->played_slot = (u16)(slot >= 0 ? slot : 0);
    training->play_at = 0;
}

/* The dummy's buttons this frame, from the player's own where the dummy
 * is being recorded. */
static u16 port_TrainingDummy(u16 *own) {
    PortTraining *training = &g_port_training;
    const PortTrainingSwitches *switches = port_GetTrainingSwitches();
    u16 buttons;

    if (training->recording) {
        buttons = *own;
        *own = 0;
        if (!g_port_ahead && !g_port_rerun) {
            port_TrainingKeep(
                training->recording - 1, training->record_at, buttons);
        }
        if (++training->record_at >= PORT_TRAINING_SLOT_FRAMES) {
            training->recording = 0;
        }
        return buttons;
    }
    training->dummy = (u8)switches->dummy;
    if (training->playing &&
        training->play_at >= port_TrainingKeptLength(training->played_slot)) {
        /* the slot played to its end: round again from the start of one
         * where the dummy plays back, and otherwise back to what it does */
        training->playing = 0;
        if (switches->dummy == PORT_TRAINING_PLAYBACK) {
            port_TrainingPlayFromStart();
        }
    } else if (
        !training->playing && switches->dummy == PORT_TRAINING_PLAYBACK) {
        port_TrainingPlayFromStart();
    }
    if (training->playing) {
        training->dummy = PORT_TRAINING_PLAYBACK;
        return port_TrainingKept(training->played_slot, training->play_at++);
    }
    if (switches->dummy == PORT_TRAINING_GUARD) {
        return port_TrainingGuard();
    }
    return 0;
}

/* ---- Select's presses ---- */

static void port_TrainingCommand(u16 fresh) {
    PortTraining *training = &g_port_training;
    /* asked of what is outside the state once: not by a frame run ahead
     * or run again, which put the state back after */
    s32 is_heard = !g_port_ahead && !g_port_rerun;

    if (fresh & PADRdown) {
        if (training->recording) {
            training->recording = 0;
        } else {
            training->recording = (u8)(port_GetTrainingSwitches()->slot + 1);
            training->record_at = 0;
            training->playing = 0;
            if (is_heard) {
                /* the slot begins again empty */
                port_TrainingKeep(training->recording - 1, -1, 0);
            }
        }
    }
    if ((fresh & PADRright) && !training->recording) {
        port_TrainingPlayFromStart();
    }
    if ((fresh & PADL1) && is_heard) {
        port_TrainingAsk(PORT_TRAINING_KEEP);
    }
    if ((fresh & PADR1) && is_heard) {
        port_TrainingAsk(PORT_TRAINING_PUT_BACK);
    }
    if (fresh & PADL2) {
        training->slow =
            training->slow ? 0 : (u8)port_GetTrainingSwitches()->slow;
    }
    if (fresh & PADRup) {
        training->frozen = (u8)!training->frozen;
        training->step = 0;
    }
    if (fresh & PADRleft) {
        training->frozen = 1;
        training->step = 1;
    }
}

/* The input the game takes (port_training.h): outside a fight of
 * training as it is; in one, Select's presses are training's, the
 * first pad's buttons its fighter's (with what was pressed while the
 * fight was held) and the second pad's the dummy's. While the fight is
 * held none of it is read: the presses are kept for its next frame. */
void port_TrainingTakeInput(struct PortInput *input) {
    PortTraining *training = &g_port_training;
    u16 own;
    u16 theirs;
    u16 fresh;

    if (!training->is_on) {
        return;
    }
    own = input->buttons[0];
    fresh = own & (u16)~training->last;
    training->last = own;
    port_TrainingNoteHeld(own);
    if (g_sys_screen != SCREEN_FIGHT_UPDATE || training->in_pause) {
        return;
    }
    if (own & PADselect) {
        port_TrainingCommand(fresh);
        own = 0;
    }
    if (training->holding) {
        /* held: the fight does not read this input; what is pressed is
         * kept for its next frame, and Start is the pause's */
        training->latched[0] |= own & (u16)~PADstart;
        if (fresh & own & PADstart) {
            training->wants_pause = 1;
        }
        input->buttons[1] = 0;
        return;
    }
    own |= training->latched[0];
    training->latched[0] = 0;
    theirs = port_TrainingDummy(&own);
    input->buttons[0] = own;
    input->buttons[1] = theirs;
    port_TrainingShow(0, own);
    port_TrainingShow(1, theirs);
    training->frames++;
}

/* ---- a kill ---- */

s32 port_TrainingRestart(void) {
    if (!g_port_training.is_on) {
        return FALSE;
    }
    g_port_training.restarts++;
    /* as practice starts again (fight.c): where the fighters are, the one
     * standing (or the first) nearest his start */
    g_fight_restart = 1;
    g_fight_restart_pair = fight_FindNearestStartPair(
        &g_fighters[g_fight_winner == FIGHT_NOBODY ? 0 : g_fight_winner]
             .world_pos);
    /* the dummy's playback from its start, with the fight's */
    if (g_port_training.playing) {
        g_port_training.play_at = 0;
    }
    g_sys_screen = SCREEN_FIGHT_INIT;
    return TRUE;
}

/* ---- slow motion and the freeze ---- */

s32 port_TrainingHolds(void) {
    PortTraining *training = &g_port_training;

    if (!training->is_on) {
        return FALSE;
    }
    if (training->frozen) {
        if (training->step) {
            /* this frame was the step's */
            training->step = 0;
        }
        return TRUE;
    }
    return training->slow > 1;
}

void port_TrainingHoldBegin(void) {
    g_port_training.holding = 1;
    g_port_training.held = 0;
    g_port_training.wants_pause = 0;
}

/* A frame held: TRUE while the hold goes on, frozen until a step or a run
 * on, slow motion for its frames, either until Start. */
s32 port_TrainingHoldGoesOn(void) {
    PortTraining *training = &g_port_training;

    training->held++;
    training->holds++;
    if (training->wants_pause) {
        return FALSE;
    }
    if (training->frozen) {
        return !training->step;
    }
    return training->slow > 1 && training->held < training->slow - 1;
}

s32 port_TrainingHoldEnd(void) {
    s32 pause = g_port_training.wants_pause;

    g_port_training.holding = 0;
    g_port_training.wants_pause = 0;
    return pause;
}
