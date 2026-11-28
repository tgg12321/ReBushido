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

void port_TrainingPause(s32 up) {
    /* nothing of training's written outside it */
    if (g_port_training.is_on) {
        g_port_training.in_pause = (u8)(up != 0);
    }
}

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
 * button configuration (pad.c, pad_ResetConfig).
 *
 * Only a blow that reaches: as the CPU judges an attack coming (cpu.c,
 * the threat's CPU_THREAT_NEAR, its reach at least the fighters'
 * distance), the motion's reach, the weapon's for the kind of move (by
 * the attacker's own move, cpu_PortBlowReach) and the attacker's size;
 * one that falls short is let by, as a player would let a swing at the
 * air go by. */
static u16 port_TrainingGuard(void) {
    PortTraining *training = &g_port_training;
    Fighter *player = &g_fighters[0];
    s32 start = player->hit_start_frames[0] != NO_FRAME
                    ? player->hit_start_frames[0]
                    : player->hit_start_frames[1];

    if (!player->attack_armed || start == NO_FRAME ||
        player->move_frame + GUARD_LEAD < start || player->move_frame > start) {
        return 0;
    }
    if (cpu_PortBlowReach(player) < g_fight_distance) {
        training->let_by++;
        return 0;
    }
    training->guards++;
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
        if (!g_port_ahead) {
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
    /* asked of what is outside the state: not by a frame run ahead, which
     * is put back after; a frame run again is the frame's own, and asks
     * again what it asked the first time */
    s32 is_heard = !g_port_ahead;

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

/* ---- the hit areas ---- */

/* The projection, as the renderer projects the frame (duo.c's tags): the
 * view's rotation from the camera's place, the coprocessor's plane and
 * offset, the frame squashed to half its height. */
#define FIXED_SHIFT 12
#define OFFSET_SHIFT 16
#define NEAREST_DEPTH 64
#define SCREEN_LIMIT 0x7000

/* A world point on the game's screen, and a radius about it there: FALSE
 * behind the camera or far off the screen. */
static s32 port_TrainingProject(
    const Vec3 *at, s32 radius, s16 *x, s16 *y, s16 *radius_out) {
    const MATRIX *view = &g_render_view_matrix;
    const s32 *eye = g_node_camera_root_ptr->world.t;
    s32 rel[3];
    s32 out[3];
    s32 h = (s32)(port_GteReadControl(PORT_GTE_PLANE) & 0xFFFF);
    s32 ofx = (s32)port_GteReadControl(PORT_GTE_OFX) >> OFFSET_SHIFT;
    s32 ofy = (s32)port_GteReadControl(PORT_GTE_OFY) >> OFFSET_SHIFT;
    s32 sx;
    s32 sy;
    s32 i;

    rel[0] = (at->x - eye[0]) >> 2;
    rel[1] = (at->y - eye[1]) >> 2;
    rel[2] = (at->z - eye[2]) >> 2;
    for (i = 0; i < 3; i++) {
        out[i] = (view->m[i][0] * rel[0] + view->m[i][1] * rel[1] +
                  view->m[i][2] * rel[2]) >>
                 (FIXED_SHIFT - 2);
    }
    if (out[2] < NEAREST_DEPTH) {
        return FALSE;
    }
    sx = ofx + out[0] * h / out[2];
    sy = ofy + (out[1] / 2) * h / out[2];
    if (sx < -SCREEN_LIMIT || sx > SCREEN_LIMIT || sy < -SCREEN_LIMIT ||
        sy > SCREEN_LIMIT) {
        return FALSE;
    }
    *x = (s16)sx;
    *y = (s16)sy;
    if (radius_out != NULL) {
        *radius_out = (s16)(radius * h / out[2]);
    }
    return TRUE;
}

/* The group of a body point (enum BodyPointId): what the window colours
 * it by. */
static u8 port_TrainingAreaGroup(s32 point) {
    return point == BODY_HEAD     ? 0
           : point < BODY_OFF_ARM ? 1
           : point < BODY_LEGS    ? 2
                                  : 3;
}

/* Before the frame's hit tests: where each blade was the frame before,
 * which its sweep goes from (fighter.c gives a fighter's first frame
 * its first point's, before this), whether the tests are the plain ones
 * (fight_UpdateFighterPair: not a lock's, not a paired move's), and
 * whose weapon they sweep. */
void port_TrainingNoteSweep(void) {
    PortTrainingAreas *areas = &g_port_training_areas;
    u16 kind = g_fighters[0].move_kind;
    s32 side;
    s32 i;

    if (!g_port_training.is_on) {
        return;
    }
    areas->sweep_path = (u8)(!MOVE_IS_LOCK(kind) && kind != MOVE_KIND_PAIRED);
    for (side = 0; side < 2; side++) {
        const Fighter *fighter = &g_fighters[side];

        /* tested against the other, as fight_TestBladesOnBodies has it
         * before the blow: the weapon out, its holder standing and not
         * the victim's */
        areas->sweep_count[side] =
            (u8)(!fighter->is_defeated &&
                 fighter->weapon_live_state != WEAPON_LIVE_OFF &&
                 fighter->frames_alive != 0 &&
                 fighter->roster_id != ROSTER_VICTIM);
        for (i = 0; i < PORT_TRAINING_BLADE_POINTS; i++) {
            areas->sweep_last[side][i] = fighter->prev_weapon_points[i];
        }
    }
}

/* The corners of a sweep's triangles, as fight_TestBladesOnBodies takes
 * them: 0 and 1 the segment now, 2 and 3 the frame before, 4 and 5 the
 * halves between (SWEEP_HALFWAY); four where the far end moved this far,
 * and two otherwise. */
#define SWEEP_SPLIT_DISTANCE 2500
static const u8 g_sweep_two[2][3] = {{2, 3, 1}, {0, 1, 2}};
static const u8 g_sweep_four[4][3] = {
    {3, 2, 5}, {5, 4, 2}, {5, 4, 1}, {1, 0, 4}};

/* A segment's sweep, from `near` to `far` of the weapon's points, into
 * the window's triangles. */
static void port_TrainingNoteSegment(s32 side, s32 segment, s32 near, s32 far) {
    PortTrainingAreas *areas = &g_port_training_areas;
    Vec3 corner[6];
    s16 x[6];
    s16 y[6];
    u8 seen[6];
    s32 dx;
    s32 dy;
    s32 dz;
    s32 count;
    s32 t;
    s32 k;

    corner[0] = SPAD->weapon_points[side][near];
    corner[1] = SPAD->weapon_points[side][far];
    corner[2] = areas->sweep_last[side][near];
    corner[3] = areas->sweep_last[side][far];
    corner[4].x = (corner[0].x + corner[2].x) / 2;
    corner[4].y = (corner[0].y + corner[2].y) / 2;
    corner[4].z = (corner[0].z + corner[2].z) / 2;
    corner[5].x = (corner[1].x + corner[3].x) / 2;
    corner[5].y = (corner[1].y + corner[3].y) / 2;
    corner[5].z = (corner[1].z + corner[3].z) / 2;
    dx = corner[1].x - corner[3].x;
    dy = corner[1].y - corner[3].y;
    dz = corner[1].z - corner[3].z;
    count = dx * dx + dy * dy + dz * dz >=
                    SWEEP_SPLIT_DISTANCE * SWEEP_SPLIT_DISTANCE
                ? 4
                : 2;
    for (k = 0; k < 6; k++) {
        seen[k] = (u8)port_TrainingProject(&corner[k], 0, &x[k], &y[k], NULL);
    }
    areas->sweep_triangles[side][segment] = (u8)count;
    for (t = 0; t < count; t++) {
        const u8 *tri = count == 2 ? g_sweep_two[t] : g_sweep_four[t];

        areas->sweep_seen[side][segment][t] =
            (u8)(seen[tri[0]] && seen[tri[1]] && seen[tri[2]]);
        for (k = 0; k < 3; k++) {
            areas->sweep[side][segment][t][k][0] = x[tri[k]];
            areas->sweep[side][segment][t][k][1] = y[tri[k]];
        }
    }
}

/* The frame's hit areas into what the window reads (port_training.h). */
void port_TrainingNoteAreas(void) {
    PortTrainingAreas *areas = &g_port_training_areas;
    s32 side;
    s32 i;

    if (!g_port_training.is_on) {
        areas->on = FALSE;
        return;
    }
    areas->frame = g_disp_frame_count;
    areas->on = TRUE;
    for (side = 0; side < 2; side++) {
        const Fighter *fighter = &g_fighters[side];

        /* the points as fighter_PlacePoints left them for the hit tests
         * this frame, the scratchpad's (fight.h) */
        for (i = 0; i < PORT_TRAINING_BODY_POINTS; i++) {
            const BodyPoint *point = &g_fighter_body_points[side][i];
            PortTrainingArea *area = &areas->body[side][i];
            s16 core = 0;

            area->group = port_TrainingAreaGroup(i);
            area->seen = (u8)port_TrainingProject(
                &SPAD->body_points[side][i], point->radius, &area->x, &area->y,
                &area->radius);
            if (area->seen && point->has_core) {
                port_TrainingProject(
                    &SPAD->body_points[side][i], point->core_radius, &area->x,
                    &area->y, &core);
            }
            area->core_radius = core;
        }
        areas->blade_points[side] =
            (u8)(fighter->weapon_id == WEAPON_ID_NAGINATA ||
                         fighter->weapon_id == WEAPON_ID_YARI
                     ? PORT_TRAINING_BLADE_POINTS
                     : PORT_TRAINING_BLADE_POINTS - 1);
        areas->blade_live[side] =
            (u8)(fighter->weapon_live_state != WEAPON_LIVE_OFF &&
                 !fighter->is_defeated);
        areas->blade_hits[side] = (u8)(fighter->hit_window_open != 0);
        for (i = 0; i < PORT_TRAINING_BLADE_POINTS; i++) {
            areas->blade_seen[side][i] = (u8)port_TrainingProject(
                &SPAD->weapon_points[side][i], 0, &areas->blade[side][i][0],
                &areas->blade[side][i][1], NULL);
        }
        /* the sweeps, as fight_TestBladesOnBodies tests this fighter's
         * weapon against the other: the plain test, the blades not met,
         * the weapon out (and its holder not the victim's) */
        if (!areas->sweep_path || g_fight_blades_met >= 0) {
            areas->sweep_count[side] = 0;
        }
        if (areas->sweep_count[side] != 0) {
            areas->sweep_count[side] = (u8)(areas->blade_points[side] - 1);
            for (i = 0; i < areas->sweep_count[side]; i++) {
                port_TrainingNoteSegment(side, i, i, i + 1);
            }
        }
    }
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
