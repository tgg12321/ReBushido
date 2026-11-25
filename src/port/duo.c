/* The native port's two on two (port_duo.h; docs/PORT.md, "Two on two";
 * ROADMAP 24): four fighters held in the game's two slots, a team a slot.
 * A state file (config/port_state.txt): the four fighters, the copies of
 * the two models, the four pairs' globals and the replay's rings are
 * saved, put back and hashed with the game's own variables, so a rollback
 * and the machines of a session hold them as they hold the game. */
#include "common.h"
#include "port_net.h"
#include "port_gpu.h"
#include "bb2.h"

/* The scratchpad's points of one slot (FightScratch): three of the
 * weapon, two of a second blade or a held thing, two that keep bodies
 * apart and the body's own. */
#define SPAD_WEAPON_POINTS 3
#define SPAD_SECOND_POINTS 2
#define SPAD_OVERLAP_POINTS 2

/* A team mate's place at the start: this far to the side of his first,
 * square to the line between the teams; nearer if that is in a wall, the
 * other side, then behind him. */
#define MATE_SIDE_FAR 1600
#define MATE_SIDE_NEAR 900
#define MATE_BEHIND 1200
#define MATE_CAST_HEIGHT 500
#define MATE_DROP 4000

/* Team mates' bodies are kept this far apart, across the ground, by a
 * push that grows with how far in one is (fighter.push, which dies away
 * as the game's own pushes do). */
#define MATES_APART 700
#define MATES_PUSH_SHARE 4

/* The CPU turns to the other enemy when he is this much nearer (two
 * thirds), looking again every so many of its frames. */
#define CPU_NEARER_TIMES 2
#define CPU_NEARER_PARTS 3
#define CPU_LOOK_FRAMES 30

/* The widest a number of the camera's sums may be, either way. */
#define FARTHEST 0x7FFFFFFF
/* The line's direction is worked out in sixteenths, so that its squares
 * fit a word; and a length under one sixteenth is no direction. */
#define LINE_SIXTEENTH 16

/* The room a model's cloth may take in its copy (ModelCloth and what
 * follows it: the pieces' vertices, points and springs). */
#define CLONE_CLOTH_BYTES 0x4000

/* What the window's tags stand over: above the head. */
#define TAG_ABOVE_HEAD 260
/* GTE fractions */
#define FIXED_SHIFT 12
#define OFFSET_SHIFT 16
#define NEAREST_DEPTH 64

/* No one (PortDuo.bound, a fighter's last fall). */
#define NOBODY 0xFF

/* The game's pair globals, kept for each pair of enemies. */
typedef struct {
    s32 distance;
    s32 distance_change;
    s32 paired_move_separation;
    MoveScript *paired_move;
    s16 paired_move_initiator;
    s16 paired_move_start_pending;
    u16 paired_move_flags;
    s16 paired_move_is_common;
    u8 paired_move_break_frame;
    u8 falling_fighter;
    u8 falling_forward;
    s16 blades_met;
    s16 lock_requested;
    s16 lock_runner;
    s16 lock_frames;
    s16 lock_flash_delay;
} PortDuoPair;

/* A fighter's motions, where a team's fighters have characters or
 * weapons of their own (PortDuo.motions_out): the game's room of a motion
 * file in his slot (g_motion_fighter_files, _sections, _loaded_sets), the
 * decoder of its stream, and his win pose's frames. */
typedef struct {
    MotionFile *file;
    MotionSections sections;
    MotionFrame *win_frames;
    MotionDecoder decoder;
    u16 set;
} PortDuoMotion;

/* What one fighter has of a slot's. */
typedef struct {
    Fighter fighter;
    PortDuoMotion motion;
    Model *model;
    Vec3 weapon_points[SPAD_WEAPON_POINTS];
    Vec3 second_points[SPAD_SECOND_POINTS];
    Vec3 overlap_points[SPAD_OVERLAP_POINTS];
    Vec3 body_points[BODY_POINT_COUNT];
    BodyPoint body_shape[BODY_POINT_COUNT];
    ReplayFighterFrame ring[REPLAY_FRAMES];
    u16 pad_held; /* the third or fourth pad's buttons last frame */
    u16 fell_at;  /* the order he fell in, 0 while he stands */
    u8 was_down;  /* is_defeated after his last step */
    u8 pad_index; /* the pad that plays him (a person's) */
} PortDuoFighter;

typedef struct {
    /* What the play tests read (tests/play/state.toml), first and in
     * bytes, so that both sizes of the port have them at one offset. */
    u8 on;
    u8 teams;   /* PORT_TEAMS_... of this fight */
    u8 decided; /* a team is out: the fight is decided */
    u8 frame_pad;
    u8 target[PORT_DUO_FIGHTERS];
    u8 down[PORT_DUO_FIGHTERS];        /* fallen */
    u8 hits_taken[PORT_DUO_FIGHTERS];  /* blows that wounded or felled him */
    u8 last_hit_by[PORT_DUO_FIGHTERS]; /* and whose the last was, or NOBODY */
    u8 rest[2];                  /* who rests in each slot between steps */
    u8 in_slot[2];               /* who is in each slot of g_fighters */
    u8 pair_in;                  /* whose globals the game's pair globals are */
    u8 bound[PORT_DUO_FIGHTERS]; /* in a paired move or a lock with, or NOBODY
                                  */
    u8 kind[PORT_DUO_FIGHTERS];  /* each one's move_kind after his step */
    u8 character[PORT_DUO_FIGHTERS]; /* each one's (enum CharacterId) */
    u8 models_out;               /* a copy is in a slot of the game's models */
    u8 clone_made[2];
    u16 falls; /* fighters fallen, for the order */
    /* the port's primitive buffers are the game's for two on two, and
     * what it had before */
    u8 prims_taken;
    u32 kept_prim_buffers[2];
    u32 kept_prim_size;
    Model *own_model[2]; /* each slot's own, the first of each team's */
    PortDuoPair pairs[PORT_DUO_PAIRS];
    PortDuoFighter f[PORT_DUO_FIGHTERS];
    Model clone[2];
    u8 clone_cloth[2][CLONE_CLOTH_BYTES];
    /* Each team's second of a character and weapon of his own (port_duo.h,
     * "Teams of different characters"): his pick, whether clone[] holds
     * his own model (loaded into the room at PORT_DUO_AREA_ADDR, his
     * textures in the second bank of video memory) rather than a copy of
     * his first's, and whether the motion files, their decoders and the
     * win poses go with each fighter (motions_out, the game's own kept in
     * motion_home), with what each room of the area holds. */
    u8 will_be_on;
    u8 partner_own[2];
    u8 motions_out;
    u8 area_win[2]; /* the character whose win pose is in it, plus 1; 0 */
    s16 partner_roster[2];
    u16 first_set[2]; /* each first's motion set, for a fight again */
    s16 partner_weapon[2];
    u16 area_set[2]; /* the motion set in it, 0 for none */
    PortDuoMotion motion_home[2];
} PortDuo;

/* Where tests/play/state.toml reads the duo (its `port_duo_*`): a member
 * that moved fails the build, and tests/rebushido/play_state.py holds
 * state.toml's offsets to these. */
#define PORT_DUO_AT(member, at)                                                \
    typedef char port_duo_at_##member                                          \
        [__builtin_offsetof(PortDuo, member) == (at) ? 1 : -1]
PORT_DUO_AT(on, 0);
PORT_DUO_AT(decided, 2);
PORT_DUO_AT(target, 4);
PORT_DUO_AT(down, 8);
PORT_DUO_AT(hits_taken, 12);
PORT_DUO_AT(last_hit_by, 16);
PORT_DUO_AT(rest, 20);
PORT_DUO_AT(bound, 25);
PORT_DUO_AT(kind, 29);
PORT_DUO_AT(character, 33);

static PortDuo g_port_duo;

s32 port_DuoIsOn(void) { return g_port_duo.on; }

/* The port's two buffers of primitives the game's while two on two is
 * on and the run has the console's; the console's back after
 * (port_duo.h). */
void port_DuoPrimBuffers(void) {
    PortDuo *duo = &g_port_duo;
    s32 i;

    if (duo->on && !duo->prims_taken && port_RemasterPrimBufferBytes() == 0) {
        for (i = 0; i < 2; i++) {
            duo->kept_prim_buffers[i] = g_disp_prim_buffers[i];
            g_disp_prim_buffers[i] =
                PORT_PRIMS_ADDR + (u32)i * PORT_PRIM_BUFFER_BYTES;
        }
        duo->kept_prim_size = g_disp_prim_buffer_size;
        g_disp_prim_buffer_size = PORT_PRIM_BUFFER_BYTES;
        duo->prims_taken = TRUE;
    } else if (!duo->on && duo->prims_taken) {
        for (i = 0; i < 2; i++) {
            g_disp_prim_buffers[i] = duo->kept_prim_buffers[i];
        }
        g_disp_prim_buffer_size = duo->kept_prim_size;
        duo->prims_taken = FALSE;
    }
}

/* The pair of two enemies: the first team's fighter (0 or 2) and the
 * second's (1 or 3). */
static s32 port_DuoPairOf(s32 first, s32 second) {
    if (PORT_DUO_SLOT(first) != 0) {
        s32 swap = first;

        first = second;
        second = swap;
    }
    return (first >> 1) * 2 + (second >> 1);
}

static s32 port_DuoPairFirst(s32 pair) { return (pair >> 1) * 2; }

static s32 port_DuoPairSecond(s32 pair) { return (pair & 1) * 2 + 1; }

/* A fighter's record as it is now: in his slot, or kept here. */
static Fighter *port_DuoNow(s32 who) {
    PortDuo *duo = &g_port_duo;
    s32 slot = PORT_DUO_SLOT(who);

    return duo->in_slot[slot] == who ? &g_fighters[slot] : &duo->f[who].fighter;
}

static s32 port_DuoAlive(s32 who) { return !port_DuoNow(who)->is_defeated; }

void port_DuoPlace(s32 who, Vec3 *at) { *at = port_DuoNow(who)->world_pos; }

/* ---- the slots ---- */

/* The motions of the fighter in `slot` (PortDuoMotion), kept; and put
 * in. */
static void port_DuoMotionKeep(s32 slot, PortDuoMotion *motion) {
    motion->file = g_motion_fighter_files[slot];
    motion->sections = g_motion_fighter_sections[slot];
    motion->win_frames = g_motion_win_frames[slot];
    motion->decoder = g_motion_decoders[MOTION_DECODER_FILE + slot];
    motion->set = g_motion_loaded_sets[slot];
}

static void port_DuoMotionTake(s32 slot, const PortDuoMotion *motion) {
    g_motion_fighter_files[slot] = motion->file;
    g_motion_fighter_sections[slot] = motion->sections;
    g_motion_win_frames[slot] = motion->win_frames;
    g_motion_decoders[MOTION_DECODER_FILE + slot] = motion->decoder;
    g_motion_loaded_sets[slot] = motion->set;
}

/* What the fighter in `slot` has of the slot's, kept with him. */
static void port_DuoKeep(s32 slot) {
    PortDuo *duo = &g_port_duo;
    PortDuoFighter *own = &duo->f[duo->in_slot[slot]];
    s32 i;

    own->fighter = g_fighters[slot];
    if (duo->motions_out) {
        port_DuoMotionKeep(slot, &own->motion);
    }
    own->model = model_Get(slot);
    for (i = 0; i < SPAD_WEAPON_POINTS; i++) {
        own->weapon_points[i] = SPAD->weapon_points[slot][i];
    }
    for (i = 0; i < SPAD_SECOND_POINTS; i++) {
        own->second_points[i] = SPAD->second_weapon_points[slot][i];
    }
    for (i = 0; i < SPAD_OVERLAP_POINTS; i++) {
        own->overlap_points[i] = SPAD->body_overlap_points[slot][i];
    }
    for (i = 0; i < BODY_POINT_COUNT; i++) {
        own->body_points[i] = SPAD->body_points[slot][i];
        own->body_shape[i] = g_fighter_body_points[slot][i];
    }
}

/* `who` into his slot, the one there kept first. */
static void port_DuoPut(s32 who) {
    PortDuo *duo = &g_port_duo;
    s32 slot = PORT_DUO_SLOT(who);
    PortDuoFighter *own = &duo->f[who];
    s32 i;

    if (duo->in_slot[slot] == who) {
        return;
    }
    port_DuoKeep(slot);
    g_fighters[slot] = own->fighter;
    if (duo->motions_out) {
        port_DuoMotionTake(slot, &own->motion);
    }
    model_PortPut(slot, own->model);
    duo->models_out = TRUE;
    for (i = 0; i < SPAD_WEAPON_POINTS; i++) {
        SPAD->weapon_points[slot][i] = own->weapon_points[i];
    }
    for (i = 0; i < SPAD_SECOND_POINTS; i++) {
        SPAD->second_weapon_points[slot][i] = own->second_points[i];
    }
    for (i = 0; i < SPAD_OVERLAP_POINTS; i++) {
        SPAD->body_overlap_points[slot][i] = own->overlap_points[i];
    }
    for (i = 0; i < BODY_POINT_COUNT; i++) {
        SPAD->body_points[slot][i] = own->body_points[i];
        g_fighter_body_points[slot][i] = own->body_shape[i];
    }
    duo->in_slot[slot] = (u8)who;
}

/* The game's pair globals into a pair's keeping, and from it. */
static void port_DuoPairKeep(PortDuoPair *pair) {
    pair->distance = g_fight_distance;
    pair->distance_change = g_fight_distance_change;
    pair->paired_move_separation = g_fight_paired_move_separation;
    pair->paired_move = g_fight_paired_move;
    pair->paired_move_initiator = g_fight_paired_move_initiator;
    pair->paired_move_start_pending = g_fight_paired_move_start_pending;
    pair->paired_move_flags = g_fight_paired_move_flags;
    pair->paired_move_is_common = g_fight_paired_move_is_common;
    pair->paired_move_break_frame = g_fight_paired_move_break_frame;
    pair->falling_fighter = g_fight_falling_fighter;
    pair->falling_forward = g_fight_falling_forward;
    pair->blades_met = g_fight_blades_met;
    pair->lock_requested = g_fight_lock_requested;
    pair->lock_runner = g_fight_lock_runner;
    pair->lock_frames = g_fight_lock_frames;
    pair->lock_flash_delay = g_fight_lock_flash_delay;
}

/* A pair's keeping into the game's pair globals. */
static void port_DuoPairTake(const PortDuoPair *pair) {
    g_fight_distance = pair->distance;
    g_fight_distance_change = pair->distance_change;
    g_fight_paired_move_separation = pair->paired_move_separation;
    g_fight_paired_move = pair->paired_move;
    g_fight_paired_move_initiator = pair->paired_move_initiator;
    g_fight_paired_move_start_pending = pair->paired_move_start_pending;
    g_fight_paired_move_flags = pair->paired_move_flags;
    g_fight_paired_move_is_common = pair->paired_move_is_common;
    g_fight_paired_move_break_frame = pair->paired_move_break_frame;
    g_fight_falling_fighter = pair->falling_fighter;
    g_fight_falling_forward = pair->falling_forward;
    g_fight_blades_met = pair->blades_met;
    g_fight_lock_requested = pair->lock_requested;
    g_fight_lock_runner = pair->lock_runner;
    g_fight_lock_frames = pair->lock_frames;
    g_fight_lock_flash_delay = pair->lock_flash_delay;
}

/* Two enemies in the slots, with their pair's globals. */
static void port_DuoPutTwo(s32 first, s32 second) {
    PortDuo *duo = &g_port_duo;
    s32 pair = port_DuoPairOf(first, second);

    port_DuoPut(first);
    port_DuoPut(second);
    if (duo->pair_in != pair) {
        port_DuoPairKeep(&duo->pairs[duo->pair_in]);
        port_DuoPairTake(&duo->pairs[pair]);
        duo->pair_in = (u8)pair;
    }
}

/* The game's own motion files back in its two rooms, the fighters'
 * let go (port_DuoFightBegin). */
static void port_DuoMotionsHome(void) {
    PortDuo *duo = &g_port_duo;
    s32 slot;

    if (!duo->motions_out) {
        return;
    }
    for (slot = 0; slot < 2; slot++) {
        port_DuoMotionTake(slot, &duo->motion_home[slot]);
    }
    duo->motions_out = FALSE;
}

/* Each slot's own model back in it (port_duo.h): a copy may be there
 * from the last fighter put in. */
void port_DuoModelsHome(void) {
    PortDuo *duo = &g_port_duo;
    s32 slot;

    if (!duo->models_out) {
        return;
    }
    for (slot = 0; slot < 2; slot++) {
        if (duo->own_model[slot] != NULL) {
            model_PortPut(slot, duo->own_model[slot]);
        }
    }
    duo->models_out = FALSE;
}

/* ---- the copy of a model ---- */

/* The cloth's pieces a copy made, let go. */
static void port_DuoFreeCloneCloth(s32 slot) {
    PortDuo *duo = &g_port_duo;
    ModelCloth *cloth = PTR(duo->clone[slot].cloth);
    s32 i;

    if (!duo->clone_made[slot] || cloth == NULL) {
        return;
    }
    for (i = 0; i < cloth->count; i++) {
        cloth_Free(cloth->pieces[i].cloth);
    }
    cloth->count = 0;
}

/* The slot's model copied, its pointers into itself moved to the copy
 * (model_Rebase, as the arena moves a model), and a cloth of its own,
 * made again at its first step. */
static void port_DuoCloneModel(s32 slot) {
    PortDuo *duo = &g_port_duo;
    Model *own = duo->own_model[slot];
    Model *copy = &duo->clone[slot];
    ModelCloth *cloth;
    ArenaBlock *block;
    s32 delta;
    s32 bytes;

    port_DuoFreeCloneCloth(slot);
    duo->clone_made[slot] = FALSE;
    if (own == NULL) {
        return;
    }
    *copy = *own;
    delta = (s32)((u8 *)copy - (u8 *)own);
    model_Rebase(slot, delta);
    model_PortPut(slot, own);
    cloth = PTR(own->cloth);
    block = arena_FindBlock(slot + ARENA_MODEL_DATA);
    SET_PTR(copy->cloth, NULL);
    if (cloth != NULL && block != NULL) {
        bytes = block->start + block->size - (s32)ADDR(cloth);
        if (bytes > 0 && bytes <= CLONE_CLOTH_BYTES) {
            ModelCloth *own_cloth = (ModelCloth *)duo->clone_cloth[slot];

            memcpy((u8 *)own_cloth, (u8 *)cloth, bytes);
            own_cloth->count = 0;
            own_cloth->rebuild_request = 1;
            own_cloth->snap_request = 1;
            SET_PTR(copy->cloth, own_cloth);
        }
    }
    duo->clone_made[slot] = TRUE;
}

/* A view's model (port_duo.h): the slots' own, then the copies. */
Model *port_DuoViewModel(s32 view) {
    const PortDuo *duo = &g_port_duo;

    if (!duo->on) {
        return NULL;
    }
    if (view >= 0 && view < 2) {
        return duo->own_model[view];
    }
    if (view >= PORT_DUO_FIGHTERS - 1 && view < PORT_DUO_FIGHTERS + 1 &&
        duo->clone_made[view - (PORT_DUO_FIGHTERS - 1)]) {
        return (Model *)&duo->clone[view - (PORT_DUO_FIGHTERS - 1)];
    }
    return NULL;
}

s32 port_DuoHoldsAddress(s32 address) {
    u32 from = (u32)ADDR(g_port_duo.clone_cloth);
    u32 at = (u32)address;

    /* a copy's cloth, or a partner's own model's in the area */
    return (at >= from && at <= from + sizeof(g_port_duo.clone_cloth)) ||
           (at >= PORT_DUO_AREA_ADDR && at <= PORT_DUO_AREA_END);
}

s32 port_DuoModelIsBanked(const Model *model) {
    const PortDuo *duo = &g_port_duo;
    s32 slot;

    for (slot = 0; slot < 2; slot++) {
        if (duo->partner_own[slot] && model == &duo->clone[slot]) {
            return TRUE;
        }
    }
    return FALSE;
}

void port_DuoForget(void) {
    PortDuo *duo = &g_port_duo;

    duo->on = FALSE;
    duo->models_out = FALSE;
    duo->clone_made[0] = duo->clone_made[1] = FALSE;
    duo->partner_own[0] = duo->partner_own[1] = FALSE;
    duo->own_model[0] = duo->own_model[1] = NULL;
    duo->in_slot[0] = 0;
    duo->in_slot[1] = 1;
    /* the game's motion files are made again from nothing after this */
    duo->motions_out = FALSE;
    port_SetVramBankOn(FALSE);
}

/* The arena moved a slot's model data (port_duo.h): the copy reads the
 * same data, and makes its cloth again from it. */
void port_DuoModelMoved(s32 slot, s32 delta) {
    PortDuo *duo = &g_port_duo;
    ModelCloth *cloth;

    if (slot < 0 || slot >= 2 || !duo->clone_made[slot] ||
        duo->partner_own[slot]) {
        /* (a partner's own model is not in the arena) */
        return;
    }
    duo->clone[slot].data += delta;
    cloth = PTR(duo->clone[slot].cloth);
    if (cloth != NULL) {
        duo->clone[slot].cloth_params += delta;
        cloth->rebuild_request = 1;
    }
}

/* ---- the fight's start ---- */

/* The team mate's place: to the side of his first, square to the line
 * to the other team (`across`), where the way there is clear and level
 * ground is under it. */
static s32 port_DuoTryPlace(const VECTOR *from, s32 dx, s32 dz, Vec3 *out) {
    Vec3 start;
    Vec3 end;
    Vec3 hit;
    s16 normal[4];

    start.x = from->vx;
    start.y = from->vy - MATE_CAST_HEIGHT;
    start.z = from->vz;
    end.x = start.x + dx;
    end.y = start.y;
    end.z = start.z + dz;
    if (collision_CastWith(&start.x, &end.x, &hit.x, normal, FIGHT_CAST_WORK) !=
        0) {
        return FALSE;
    }
    start = end;
    end.y = start.y + MATE_DROP;
    if (collision_CastWith(&start.x, &end.x, &hit.x, normal, FIGHT_CAST_WORK) ==
            0 ||
        normal[1] != -ONE) {
        return FALSE;
    }
    out->x = start.x;
    out->y = hit.y;
    out->z = start.z;
    return TRUE;
}

/* Where a team's second stands at the fight's start: beside his first,
 * `side` the way square to `across`, the line from his first to the
 * enemy's; the other way, nearer, or behind if a wall or no ground is
 * there. */
static void port_DuoFindPlace(
    const Fighter *mate, s32 across_x, s32 across_z, s32 side, Vec3 *out) {
    s32 heading = ratan2(across_x, across_z);
    s32 left = (heading + side * (ANGLE_TURN / 4)) & ANGLE_MASK;
    s32 sin_left = g_math_sin_table[left];
    s32 cos_left = g_math_sin_table[(left + ANGLE_TURN / 4) & ANGLE_MASK];
    s32 sin_back = g_math_sin_table[(heading + ANGLE_TURN / 2) & ANGLE_MASK];
    s32 cos_back =
        g_math_sin_table[(heading + ANGLE_TURN / 2 + ANGLE_TURN / 4) &
                         ANGLE_MASK];
    static const s32 g_reaches[2] = {MATE_SIDE_FAR, MATE_SIDE_NEAR};
    s32 i;
    s32 turn;

    for (i = 0; i < 2; i++) {
        for (turn = 1; turn >= -1; turn -= 2) {
            s32 dx = turn * (sin_left * g_reaches[i]) / ONE;
            s32 dz = turn * (cos_left * g_reaches[i]) / ONE;

            if (port_DuoTryPlace(&mate->foot_pos, dx, dz, out)) {
                return;
            }
        }
    }
    if (port_DuoTryPlace(&mate->foot_pos, (sin_back * MATE_BEHIND) / ONE,
                         (cos_back * MATE_BEHIND) / ONE, out)) {
        return;
    }
    /* nowhere better: where his first stands, and the push parts them */
    out->x = mate->foot_pos.vx + 1;
    out->y = mate->foot_pos.vy;
    out->z = mate->foot_pos.vz;
}

/* The fighter in his slot stood at `place`, facing `toward`, as
 * fighter_Init stands one. */
static void port_DuoStand(
    Fighter *fighter, const Vec3 *place, const Vec3 *toward) {
    s32 bearing;

    fighter->base_pos = *place;
    fighter->world_pos.x = place->x + fighter->root_offset.x;
    fighter->world_pos.y = place->y + fighter->root_offset.y;
    fighter->world_pos.z = place->z + fighter->root_offset.z;
    fighter->foot_pos.vx = fighter->world_pos.x;
    fighter->foot_pos.vy = fighter->base_pos.y;
    fighter->foot_pos.vz = fighter->world_pos.z;
    fighter->prev_foot_pos = fighter->foot_pos;
    fighter->ground_y = fighter->foot_pos.vy;
    fighter->camera_target = fighter->world_pos;
    fighter->hip_pos = fighter->world_pos;
    fighter->head_pos = fighter->world_pos;
    fighter->chest_pos = fighter->world_pos;
    bearing = ratan2(toward->x - place->x, toward->z - place->z) & ANGLE_MASK;
    fighter->orientation.vy = (s16)bearing;
    fighter->opponent_bearing = (s16)bearing;
    fighter->initial_orientation = fighter->orientation;
}

/* Is the second fighter of a team the CPU's: the CPU's partners, a team
 * whose first is the CPU's, or a session's whose input is two pads, one
 * a machine (docs/NETPLAY.md, section 18); a session of four players has
 * a player for every fighter. */
static s32 port_DuoPartnerIsCpu(s32 slot) {
    return g_port_duo.teams != PORT_TEAMS_TWO_PADS ||
           g_fight_config.is_cpu[slot] ||
           (port_IsNetplayOn() && port_NetplayPads() < PORT_DUO_FIGHTERS);
}

/* ---- teams of characters of their own ---- */

/* A team's second's motion set: his weapon's with his character's
 * stances, or his side's mod's weapon's (as motion_LoadFighterFiles
 * works out his first's). */
static u16 port_DuoPartnerSet(s32 slot) {
    const PortDuo *duo = &g_port_duo;
    s32 weapon = duo->partner_weapon[slot];
    s32 character = g_fighter_roster_characters[duo->partner_roster[slot]];
    u16 set =
        (u16)MOTION_SET(weapon, g_fighter_stance_kinds[character][weapon]);

    return (u16)port_WeaponMotionSet(slot, weapon, set);
}

/* The parts of a motion file in memory, as motion_LoadFighterFiles finds
 * them. */
static void port_DuoSections(u32 address, MotionSections *out) {
    const MotionFile *file = (const MotionFile *)ADDR(address);

    out->moves =
        (u16 *)ADDR(address + 0x6C + (u32)(file->stance_move_rows - 1) * 6);
    out->entries = (u16 *)ADDR(address + (u32)file->section_offsets[0]);
    out->metadata = (u8 *)ADDR(address + (u32)file->section_offsets[1]);
    out->cpu_scripts = (u16 *)ADDR(address + (u32)file->section_offsets[2]);
    out->stream = (u32 *)ADDR(address + (u32)file->section_offsets[3]);
}

/* The weapon models the second of team `slot` holds and throws, asked
 * for (fighter_Init's, as the modes with matches give them). */
static void port_DuoAskWeapons(s32 slot, s32 roster, s32 weapon) {
    s32 character = g_fighter_roster_characters[roster];
    s32 kind = g_object_start_sub_weapons[character][0];

    resource_RequestWeapon(port_WeaponModel(
        slot, weapon,
        g_fighter_clan_weapon_models[weapon][g_fighter_clans[character]]));
    if (roster == ROSTER_SIDEARM_2) {
        kind = OBJECT_OFF_HAND_GUN_2;
    } else if (roster == ROSTER_SIDEARM_1) {
        kind = OBJECT_OFF_HAND_GUN_1;
    }
    if (kind != OBJECT_NONE) {
        resource_RequestWeapon(g_object_weapon_models[kind]);
        if (kind == OBJECT_FROG) {
            resource_RequestWeapon(g_object_weapon_models[OBJECT_FROG] +
                                   WEAPON_MODEL_FROG_IN_AIR);
        }
    }
}

void port_DuoLoadPartners(s32 staging) {
    PortDuo *duo = &g_port_duo;
    s32 slot;

    for (slot = 0; slot < 2; slot++) {
        s32 roster = duo->partner_roster[slot];
        s32 model_id = g_fighter_roster_models[roster];

        duo->partner_own[slot] = FALSE;
        if (!duo->will_be_on) {
            continue;
        }
        if (model_id != g_fighter_roster_models[g_fight_config.roster[slot]]) {
            ndata_ReadModel(model_id, staging);
            duo->partner_own[slot] = (u8)model_PortLoadPartner(
                &duo->clone[slot], slot, model_id, staging,
                (s32)PORT_DUO_MODEL_ADDR(slot), (s32)PORT_DUO_MODEL_BYTES,
                PORT_DUO_RELOC_BLOCK(slot));
            if (!duo->partner_own[slot]) {
                /* (no model of the game's is this big) */
                port_Log("two on two: model %d does not fit its room; the "
                         "partner is his first's character\n",
                         model_id);
                duo->partner_roster[slot] = g_fight_config.roster[slot];
                roster = duo->partner_roster[slot];
            }
        }
        port_DuoAskWeapons(slot, roster, duo->partner_weapon[slot]);
    }
}

void port_DuoLoadPartnerMotions(void) {
    PortDuo *duo = &g_port_duo;
    s32 slot;

    for (slot = 0; slot < 2 && duo->will_be_on; slot++) {
        u16 set = port_DuoPartnerSet(slot);
        s32 character = g_fighter_roster_characters[duo->partner_roster[slot]];

        if (set != g_motion_loaded_sets[0] && set != g_motion_loaded_sets[1] &&
            set != duo->area_set[slot]) {
            u32 dest = PORT_DUO_MOTION_ADDR(slot);
            s32 work;

            if (!port_WeaponReadMotions(set, (void *)ADDR(dest))) {
                for (work = 0; g_motion_set_files[work].set != 0; work++) {
                    if (g_motion_set_files[work].set == set) {
                        break;
                    }
                }
                cdrom_StartRead(cdrom_GetFileIndex(CDROM_GROUP_MOTION,
                                                   work + MOTION_FILE_SETS),
                                (s32)dest);
                cdrom_WaitIdle();
            }
            duo->area_set[slot] = set;
        }
        if (character != g_motion_win_frames_loaded[0] &&
            character != g_motion_win_frames_loaded[1] &&
            character + 1 != duo->area_win[slot]) {
            cdrom_StartReadAt(
                cdrom_GetFileIndex(CDROM_GROUP_MOTION, MOTION_FILE_WIN),
                (s32)PORT_DUO_WIN_ADDR(slot), character * MOTION_WIN_SECTORS,
                MOTION_WIN_SECTORS);
            cdrom_WaitIdle();
            duo->area_win[slot] = (u8)(character + 1);
        }
    }
}

/* The motions of fighter `who` (PortDuoMotion): his set's file where the
 * game has it or in his room, its decoder opened afresh, his win pose's
 * frames where they are. */
static void port_DuoMotionMade(s32 who, u16 set, s32 character) {
    PortDuo *duo = &g_port_duo;
    PortDuoMotion *motion = &duo->f[who].motion;
    s32 slot = PORT_DUO_SLOT(who);
    s32 room;

    motion->set = set;
    motion->file = NULL;
    for (room = 0; room < 2; room++) {
        if (motion->file == NULL && duo->motion_home[room].set == set) {
            motion->file = duo->motion_home[room].file;
            motion->sections = duo->motion_home[room].sections;
        }
    }
    if (motion->file == NULL) {
        motion->file = (MotionFile *)ADDR(PORT_DUO_MOTION_ADDR(slot));
        port_DuoSections(PORT_DUO_MOTION_ADDR(slot), &motion->sections);
    }
    motion->win_frames = (MotionFrame *)ADDR(PORT_DUO_WIN_ADDR(slot));
    for (room = 1; room >= 0; room--) {
        if (g_motion_win_frames_loaded[room] == character) {
            motion->win_frames = duo->motion_home[room].win_frames;
        }
    }
    if (g_motion_win_frames_loaded[slot] == character) {
        motion->win_frames = duo->motion_home[slot].win_frames;
    }
    motion_OpenStream(MOTION_DECODER_FILE + slot, motion->sections.stream);
    motion->decoder = g_motion_decoders[MOTION_DECODER_FILE + slot];
}

/* Where a team's second's character or weapon is not his first's, each
 * fighter's motions go with him from now on (PortDuo.motions_out): the
 * game's two rooms kept, each of the four given his own, the firsts' put
 * in, each fighter's file the room of his slot. */
static void port_DuoMotionsOut(void) {
    PortDuo *duo = &g_port_duo;
    s32 own = FALSE;
    s32 slot;

    for (slot = 0; slot < 2; slot++) {
        if (port_DuoPartnerSet(slot) != g_fighters[slot].motion_set ||
            duo->partner_roster[slot] != g_fight_config.roster[slot]) {
            own = TRUE;
        }
    }
    if (!own) {
        return;
    }
    for (slot = 0; slot < 2; slot++) {
        port_DuoMotionKeep(slot, &duo->motion_home[slot]);
    }
    for (slot = 0; slot < 2; slot++) {
        u16 set = port_DuoPartnerSet(slot);
        s32 character = g_fighter_roster_characters[duo->partner_roster[slot]];

        port_DuoMotionMade(
            slot, g_fighters[slot].motion_set, g_fighters[slot].character);
        port_DuoMotionMade(slot + 2, set, character);
    }
    for (slot = 0; slot < 2; slot++) {
        port_DuoMotionTake(slot, &duo->f[slot].motion);
        g_fighters[slot].motion_slot = (s16)slot;
    }
    duo->motions_out = TRUE;
}

/* The second fighter of the team of `slot` set up in the slot as the
 * fight sets up its first: who he is (his own pick, or his first's), his
 * place, his model (his own, or his first's copied), his motion and
 * first move, the CPU. */
static void port_DuoSetPartner(
    s32 slot, const Vec3 *place, const Vec3 *toward) {
    PortDuo *duo = &g_port_duo;
    s32 who = slot + 2;
    s32 is_cpu = port_DuoPartnerIsCpu(slot);
    u16 motion_set = g_fighters[slot].motion_set;
    u8 object_kind = g_object_fighter_kinds[slot];
    Fighter *fighter = &g_fighters[slot];
    MoveScript *move;

    port_DuoKeep(slot);
    duo->in_slot[slot] = (u8)who;
    if (duo->motions_out) {
        port_DuoMotionTake(slot, &duo->f[who].motion);
        motion_set = duo->f[who].motion.set;
    }
    fighter_Init(slot, is_cpu, duo->partner_roster[slot],
                 duo->partner_weapon[slot], 0);
    /* whose sounds a thrown thing makes stays the slot's */
    g_object_fighter_kinds[slot] = object_kind;
    fighter->motion_set = motion_set;
    port_DuoStand(fighter, place, toward);
    if (duo->clone_made[slot]) {
        model_PortPut(slot, &duo->clone[slot]);
        duo->models_out = TRUE;
    }
    motion_AttachFighter(slot);
    if (duo->motions_out) {
        fighter->motion_slot = (s16)slot;
    }
    if (is_cpu) {
        fighter->cpu_base_level = g_fight_config.cpu_levels[slot];
        cpu_Init(
            slot, g_motion_fighter_sections[fighter->motion_slot].cpu_scripts,
            g_motion_fighter_sections[fighter->other->motion_slot].cpu_scripts);
    }
    motion_ResetStance(slot);
    move = motion_GetDefaultStanceMove(slot);
    fighter->move_is_common = 0;
    fighter_StartMove(slot, move, 0);
    cloth_RequestSnap(slot);
    duo->f[who].pad_index = (u8)(slot + 2);
    port_DuoKeep(slot);
    port_DuoPut(slot);
}

void port_DuoFightBegin(void) {
    PortDuo *duo = &g_port_duo;
    s32 slot;

    port_DuoModelsHome();
    port_DuoMotionsHome();
    port_DuoFreeCloneCloth(0);
    port_DuoFreeCloneCloth(1);
    duo->clone_made[0] = duo->clone_made[1] = FALSE;
    duo->on = FALSE;
    duo->in_slot[0] = 0;
    duo->in_slot[1] = 1;
    port_SetVramBankOn(FALSE);
    duo->will_be_on = (u8)(g_port_match.teams != PORT_TEAMS_ONE &&
                           port_MatchModeHasRules(g_fight_mode) &&
                           !g_fight_first_person);
    if (g_fight_restart) {
        /* the fight again: its fighters and what is loaded for them stay,
         * the firsts' motion sets theirs (a second may have rested in the
         * slot) */
        if (duo->will_be_on) {
            g_fighters[0].motion_set = duo->first_set[0];
            g_fighters[1].motion_set = duo->first_set[1];
        }
        return;
    }
    for (slot = 0; slot < 2; slot++) {
        s32 character;
        s32 weapon;

        duo->partner_roster[slot] = g_fight_config.roster[slot];
        duo->partner_weapon[slot] = g_fight_config.weapon[slot];
        if (port_MatchPartner(slot, &character, &weapon)) {
            duo->partner_roster[slot] =
                (s16)g_fighter_character_rosters[character];
            duo->partner_weapon[slot] = (s16)weapon;
        }
    }
}

/* The fight set up (port_duo.h): two on two if the match asks for it,
 * the slots' own models kept and copied (or the partners' own models
 * taken), the fighters' motions given to each, the four fighters'
 * keeping made, the seconds set up beside their firsts, each facing the
 * other team's second, and the first targets: the one across. */
void port_DuoFightSet(void) {
    PortDuo *duo = &g_port_duo;
    s32 teams = g_port_match.teams;
    Vec3 place[PORT_DUO_FIGHTERS];
    s32 across_x;
    s32 across_z;
    s32 who;
    s32 slot;
    s32 i;

    if (teams == PORT_TEAMS_ONE || !port_MatchModeHasRules(g_fight_mode) ||
        g_fight_first_person) {
        duo->on = FALSE;
        return;
    }
    duo->on = TRUE;
    duo->teams = (u8)teams;
    duo->in_slot[0] = 0;
    duo->in_slot[1] = 1;
    duo->rest[0] = 0;
    duo->rest[1] = 1;
    duo->decided = FALSE;
    duo->falls = 0;
    duo->models_out = FALSE;
    for (slot = 0; slot < 2; slot++) {
        duo->first_set[slot] = g_fighters[slot].motion_set;
        duo->own_model[slot] = model_Get(slot);
        if (duo->partner_own[slot]) {
            ModelCloth *cloth = PTR(duo->clone[slot].cloth);

            /* his own, loaded (port_DuoLoadPartners): his cloth made
             * again at his first step */
            if (cloth != NULL) {
                cloth->rebuild_request = 1;
                cloth->snap_request = 1;
            }
            duo->clone_made[slot] = TRUE;
            port_SetVramBankOn(TRUE);
        } else {
            port_DuoCloneModel(slot);
        }
    }
    port_DuoMotionsOut();
    for (who = 0; who < PORT_DUO_FIGHTERS; who++) {
        PortDuoFighter *own = &duo->f[who];

        own->pad_held = 0;
        own->fell_at = 0;
        duo->down[who] = FALSE;
        duo->hits_taken[who] = 0;
        duo->last_hit_by[who] = NOBODY;
        own->was_down = FALSE;
        own->pad_index = (u8)who;
        duo->bound[who] = NOBODY;
        for (i = 0; i < REPLAY_FRAMES; i++) {
            memset(&own->ring[i], 0, sizeof(own->ring[i]));
        }
    }
    port_DuoKeep(0);
    port_DuoKeep(1);
    for (i = 0; i < PORT_DUO_PAIRS; i++) {
        port_DuoPairKeep(&duo->pairs[i]);
    }
    duo->pair_in = 0;
    /* the second of each team to the same side of his first, so that the
     * two seconds face each other as the two firsts do */
    across_x = g_fighters[1].world_pos.x - g_fighters[0].world_pos.x;
    across_z = g_fighters[1].world_pos.z - g_fighters[0].world_pos.z;
    port_DuoFindPlace(&g_fighters[0], across_x, across_z, 1, &place[2]);
    port_DuoFindPlace(&g_fighters[1], -across_x, -across_z, -1, &place[3]);
    port_DuoSetPartner(0, &place[2], &place[3]);
    port_DuoSetPartner(1, &place[3], &place[2]);
    port_DuoPut(0);
    port_DuoPut(1);
    for (who = 0; who < PORT_DUO_FIGHTERS; who++) {
        duo->character[who] = (u8)port_DuoNow(who)->character;
    }
    duo->target[0] = 1;
    duo->target[1] = 0;
    duo->target[2] = 3;
    duo->target[3] = 2;
}

/* ---- a frame ---- */

/* How far apart two fighters are across the ground (squared, halved
 * until it fits). */
static u32 port_DuoApart(const Fighter *first, const Fighter *second) {
    s32 dx = (second->world_pos.x - first->world_pos.x) >> 2;
    s32 dz = (second->world_pos.z - first->world_pos.z) >> 2;

    return (u32)(dx * dx) + (u32)(dz * dz);
}

/* In a paired move or a blade lock, or about to be. */
static s32 port_DuoHeldKind(const Fighter *fighter) {
    u16 kind = fighter->move_kind;

    return MOVE_IS_LOCK(kind) || kind == MOVE_KIND_PAIRED ||
           fighter->pending_event == MOVE_EVENT_LOCK;
}

/* A pair held by a paired move or a lock lets go when neither is in one
 * any longer and none is to begin. */
static void port_DuoLetGo(void) {
    PortDuo *duo = &g_port_duo;
    s32 who;

    for (who = 0; who < PORT_DUO_FIGHTERS; who += 2) {
        s32 other = duo->bound[who];
        s32 pair;

        if (other == NOBODY) {
            continue;
        }
        pair = port_DuoPairOf(who, other);
        if (duo->pair_in == pair) {
            port_DuoPairKeep(&duo->pairs[pair]);
        }
        if (!port_DuoHeldKind(port_DuoNow(who)) &&
            !port_DuoHeldKind(port_DuoNow(other)) &&
            !duo->pairs[pair].paired_move_start_pending) {
            duo->bound[who] = NOBODY;
            duo->bound[other] = NOBODY;
        }
    }
}

/* Two held together, each the other's target: the pair whose globals
 * hold their paired move or lock is the one each is stepped beside. */
static void port_DuoBind(s32 first, s32 second) {
    PortDuo *duo = &g_port_duo;

    if (duo->bound[first] == NOBODY && duo->bound[second] == NOBODY) {
        duo->bound[first] = (u8)second;
        duo->bound[second] = (u8)first;
        duo->target[first] = (u8)second;
        duo->target[second] = (u8)first;
    }
}

/* The retarget button pressed this frame on the fighter's pad: the
 * game's two pads from their state, the third and fourth from the input. */
static s32 port_DuoRetargetPressed(s32 who) {
    PortDuo *duo = &g_port_duo;
    const Fighter *fighter = port_DuoNow(who);

    if (fighter->is_cpu) {
        return FALSE;
    }
    if (who < 2) {
        return (g_pad_state.pressed >> (who * PAD_BITS)) & PADR2 ? TRUE : FALSE;
    }
    return (g_port_input.buttons[duo->f[who].pad_index] &
            ~duo->f[who].pad_held & PADR2)
               ? TRUE
               : FALSE;
}

/* The targets of the frame (port_duo.h): held ones let go, then each
 * fighter's: the one he is held with; a fallen one's kept; the living
 * enemy where his target fell; the other enemy on his retarget press,
 * or, the CPU's, when that one is much the nearer. */
void port_DuoChooseTargets(void) {
    PortDuo *duo = &g_port_duo;
    s32 who;

    port_DuoLetGo();
    for (who = 0; who < PORT_DUO_FIGHTERS; who++) {
        s32 enemy_slot = PORT_DUO_SLOT(who) ^ 1;
        s32 target = duo->target[who];
        s32 other = PORT_DUO_MATE(target);
        const Fighter *fighter = port_DuoNow(who);

        if (PORT_DUO_SLOT(target) != enemy_slot) {
            target = enemy_slot;
            other = PORT_DUO_MATE(target);
        }
        if (duo->bound[who] != NOBODY) {
            target = duo->bound[who];
        } else if (fighter->is_defeated) {
            /* the fallen look at whoever they looked at */
        } else if (!port_DuoAlive(target)) {
            if (port_DuoAlive(other)) {
                target = other;
            }
        } else if (port_DuoAlive(other) && duo->bound[other] == NOBODY) {
            if (port_DuoRetargetPressed(who)) {
                target = other;
            } else if (fighter->is_cpu &&
                       fighter->frames_alive % CPU_LOOK_FRAMES == 0 &&
                       port_DuoApart(fighter, port_DuoNow(other)) *
                               (CPU_NEARER_PARTS * CPU_NEARER_PARTS) <
                           port_DuoApart(fighter, port_DuoNow(target)) *
                               (CPU_NEARER_TIMES * CPU_NEARER_TIMES)) {
                target = other;
            }
        }
        duo->target[who] = (u8)target;
    }
}

/* Whether the pair is to be measured (meet 0) or to meet (meet 1): a
 * pair held together always; one of whom either is held by a third
 * never; the fallen not, unless held. */
s32 port_DuoPutPair(s32 pair, s32 meet) {
    PortDuo *duo = &g_port_duo;
    s32 first = port_DuoPairFirst(pair);
    s32 second = port_DuoPairSecond(pair);
    s32 held_together = duo->bound[first] == second;

    if (!held_together &&
        (duo->bound[first] != NOBODY || duo->bound[second] != NOBODY)) {
        if (meet) {
            return FALSE;
        }
    }
    if (meet && !held_together &&
        (!port_DuoAlive(first) || !port_DuoAlive(second))) {
        return FALSE;
    }
    port_DuoPutTwo(first, second);
    return TRUE;
}

/* Whether the two in the slots may begin a paired move (port_duo.h). */
s32 port_DuoMayPair(void) {
    PortDuo *duo = &g_port_duo;
    s32 first;
    s32 second;

    if (!duo->on) {
        return TRUE;
    }
    first = duo->in_slot[0];
    second = duo->in_slot[1];
    if (duo->bound[first] == second) {
        return TRUE;
    }
    if (duo->bound[first] != NOBODY || duo->bound[second] != NOBODY ||
        g_fighters[0].is_defeated || g_fighters[1].is_defeated) {
        g_fight_paired_move_start_pending = 0;
        return FALSE;
    }
    return TRUE;
}

/* A paired move begun by the measure binds the two in the slots
 * (port_duo.h): fight_StartPairedMoves begins one of a move's own (a
 * throw at its distance) with no ask that a step would have bound. */
void port_DuoMeasured(void) {
    PortDuo *duo = &g_port_duo;

    if (g_fighters[0].move_started == MOVE_START_PAIRED &&
        g_fighters[1].move_started == MOVE_START_PAIRED) {
        port_DuoBind(duo->in_slot[0], duo->in_slot[1]);
    }
}

/* The pad of a fighter of the third or fourth pad, as the game's own pad
 * state would have it in his slot's half, read through the game's own
 * reader (button configuration, mirroring). */
static void port_DuoReadOwnPad(s32 who, PadState *pad) {
    PortDuo *duo = &g_port_duo;
    PortDuoFighter *own = &duo->f[who];
    s32 slot = PORT_DUO_SLOT(who);
    u32 shift = (u32)slot * PAD_BITS;
    u32 mask = (u32)0xFFFF << shift;
    u32 held = (u32)(g_port_input.buttons[own->pad_index] & ~PADL2);
    u32 before = own->pad_held;
    PadState kept = g_pad_state;

    g_pad_state.held = (kept.held & ~mask) | (held << shift);
    g_pad_state.pressed = (kept.pressed & ~mask) | ((held & ~before) << shift);
    g_pad_state.released =
        (kept.released & ~mask) | ((~held & before & 0xFFFF) << shift);
    g_pad_state.unheld = (kept.unheld & ~mask) | ((~held & 0xFFFF) << shift);
    fighter_ReadPad(slot, pad);
    g_pad_state = kept;
}

/* The player of fighter `who`, in his slot, has left an online fight of
 * four, and his pad says the CPU's from this frame (PORT_PAD_CPU;
 * docs/NETPLAY.md, section 19): the CPU plays him on as it plays a
 * partner, at the match's level, against the one he faces. In a fight of
 * the same match after it his pad says so from its first step. */
static void port_DuoTakeOver(s32 who) {
    s32 slot = PORT_DUO_SLOT(who);
    Fighter *fighter = &g_fighters[slot];

    fighter->is_cpu = TRUE;
    fighter->cpu_base_level = g_fight_config.cpu_levels[slot];
    cpu_Init(
        slot, g_motion_fighter_sections[fighter->motion_slot].cpu_scripts,
        g_motion_fighter_sections[fighter->other->motion_slot].cpu_scripts);
}

/* A fighter and his target in the slots with their pair's globals, and
 * his pad (port_duo.h): the third and fourth pads' through the game's
 * reader, the CPU's its own; the retarget button and Select left out. */
void port_DuoPutFighter(s32 who, PadState *pad) {
    PortDuo *duo = &g_port_duo;
    PortDuoFighter *own = &duo->f[who];
    Fighter *fighter;
    u32 gone = PADR2 | PADselect;

    port_DuoPutTwo(who, duo->target[who]);
    fighter = &g_fighters[PORT_DUO_SLOT(who)];
    if (!fighter->is_cpu &&
        g_port_input.present[own->pad_index] == PORT_PAD_CPU) {
        port_DuoTakeOver(who);
    }
    if (who >= 2 && !fighter->is_cpu) {
        port_DuoReadOwnPad(who, pad);
    } else {
        fighter_ReadPad(PORT_DUO_SLOT(who), pad);
    }
    if (who >= 2) {
        own->pad_held = g_port_input.buttons[own->pad_index];
    }
    /* the retarget button is the duo's, and no one gives up (a
     * kaishaku is between two) */
    pad->held &= ~gone;
    pad->pressed &= ~gone;
    pad->released &= ~gone;
    pad->unheld |= gone;
}

/* After a fighter's step (port_duo.h): his frame of the replay's ring
 * kept, his fall noted in its order, a paired move he has begun holding
 * him and his target. */
void port_DuoFighterDone(s32 who) {
    PortDuo *duo = &g_port_duo;
    PortDuoFighter *own = &duo->f[who];
    s32 slot = PORT_DUO_SLOT(who);
    Fighter *fighter = &g_fighters[slot];
    s32 target = duo->target[who];

    own->ring[g_replay_write_frame] =
        g_replay_fighter_frames[g_replay_write_frame][slot];
    if (fighter->is_defeated && !own->was_down) {
        own->was_down = TRUE;
        own->fell_at = ++duo->falls;
    }
    duo->down[who] = (u8)(fighter->is_defeated != 0);
    duo->kind[who] = (u8)fighter->move_kind;
    if (g_fight_paired_move_start_pending) {
        port_DuoBind(who, target);
    }
}

/* What a fighter's wounds come to, to see a blow land. */
static s32 port_DuoHurt(const Fighter *fighter) {
    return (fighter->off_arm_ok == 0) + fighter->leg_wounds +
           fighter->arm_wounds + fighter->body_wounds +
           (fighter->is_defeated != 0) + (fighter->bleed_timer != 0);
}

/* Before a pair meets, how hurt each of the two is. */
static s32 g_port_duo_hurt[2];

void port_DuoBeforeMeet(void) {
    g_port_duo_hurt[0] = port_DuoHurt(&g_fighters[0]);
    g_port_duo_hurt[1] = port_DuoHurt(&g_fighters[1]);
}

/* After a pair met (port_duo.h): a blow taken counted, as the other's;
 * a lock begun holds them. */
void port_DuoAfterMeet(void) {
    PortDuo *duo = &g_port_duo;
    s32 slot;

    for (slot = 0; slot < 2; slot++) {
        if (port_DuoHurt(&g_fighters[slot]) > g_port_duo_hurt[slot]) {
            s32 who = duo->in_slot[slot];

            if (duo->hits_taken[who] < 0xFF) {
                duo->hits_taken[who]++;
            }
            duo->last_hit_by[who] = duo->in_slot[slot ^ 1];
        }
    }
    /* a lock begun between them holds them */
    if (g_fighters[0].pending_event == MOVE_EVENT_LOCK &&
        g_fighters[1].pending_event == MOVE_EVENT_LOCK) {
        port_DuoBind(duo->in_slot[0], duo->in_slot[1]);
    }
}

/* Team mates' bodies apart (port_duo.h): a push of each from the other
 * that grows as they come within MATES_APART. */
void port_DuoKeepMatesApart(void) {
    s32 slot;

    for (slot = 0; slot < 2; slot++) {
        Fighter *first = port_DuoNow(slot);
        Fighter *second = port_DuoNow(slot + 2);
        s32 dx = second->foot_pos.vx - first->foot_pos.vx;
        s32 dz = second->foot_pos.vz - first->foot_pos.vz;
        s32 apart;
        s32 push;

        if (dx > MATES_APART || dx < -MATES_APART || dz > MATES_APART ||
            dz < -MATES_APART) {
            continue;
        }
        apart = SquareRoot0(dx * dx + dz * dz);
        if (apart >= MATES_APART) {
            continue;
        }
        if (apart == 0) {
            dx = 1;
            apart = 1;
        }
        push = (MATES_APART - apart) / MATES_PUSH_SHARE;
        first->push.vx -= dx * push / apart;
        first->push.vz -= dz * push / apart;
        second->push.vx += dx * push / apart;
        second->push.vz += dz * push / apart;
    }
}

/* Of a team, who rests in its slot: a living one (the decided fight's
 * winner the one who looks at the last fallen), or the last of it to
 * fall. */
static s32 port_DuoRestOf(s32 slot, s32 victim) {
    PortDuo *duo = &g_port_duo;
    s32 first = slot;
    s32 second = slot + 2;

    if (port_DuoAlive(first) && port_DuoAlive(second)) {
        if (victim != NOBODY && duo->target[second] == victim &&
            duo->target[first] != victim) {
            return second;
        }
        return first;
    }
    if (port_DuoAlive(first)) {
        return first;
    }
    if (port_DuoAlive(second)) {
        return second;
    }
    return duo->f[second].fell_at > duo->f[first].fell_at ? second : first;
}

/* The end of a frame of two on two (port_duo.h): the pair the fight
 * rests on put in, the winner said once a team is out (none before), the
 * replay's ring the resting pair's, all of it once the fight is decided. */
void port_DuoEndFrame(void) {
    PortDuo *duo = &g_port_duo;
    s32 out[2];
    s32 victim = NOBODY;
    s32 slot;
    s32 i;

    for (slot = 0; slot < 2; slot++) {
        out[slot] = !port_DuoAlive(slot) && !port_DuoAlive(slot + 2);
    }
    if (out[0] || out[1]) {
        /* the last of a team out to fall */
        s32 latest = 0;

        for (i = 0; i < PORT_DUO_FIGHTERS; i++) {
            if (out[PORT_DUO_SLOT(i)] && duo->f[i].fell_at > latest) {
                latest = duo->f[i].fell_at;
                victim = i;
            }
        }
    }
    for (slot = 0; slot < 2; slot++) {
        duo->rest[slot] = (u8)port_DuoRestOf(slot, victim);
    }
    port_DuoPutTwo(duo->rest[0], duo->rest[1]);
    if (!out[0] && !out[1]) {
        g_fight_winner = FIGHT_NOBODY;
    } else {
        if (!duo->decided) {
            duo->decided = TRUE;
            /* the replay is of the two the fight rests on */
            for (i = 0; i < REPLAY_FRAMES; i++) {
                g_replay_fighter_frames[i][0] = duo->f[duo->rest[0]].ring[i];
                g_replay_fighter_frames[i][1] = duo->f[duo->rest[1]].ring[i];
            }
        }
        if (out[0] && out[1]) {
            /* both out: the winner the team out last, as the game's draw
             * names the one who fell second */
            g_fight_winner = (s8)PORT_DUO_SLOT(victim);
        } else {
            g_fight_winner = (s8)(out[0] ? 1 : 0);
        }
    }
    for (slot = 0; slot < 2; slot++) {
        g_replay_fighter_frames[g_replay_write_frame][slot] =
            duo->f[duo->rest[slot]].ring[g_replay_write_frame];
    }
}

/* ---- the camera ---- */

void port_DuoCameraPoints(Vec3 *first_point, Vec3 *second_point,
                          Fighter **first_fighter, Fighter **second_fighter) {
    PortDuo *duo = &g_port_duo;
    Fighter *fighters[PORT_DUO_FIGHTERS];
    s32 center[2][2];
    s32 counted[2];
    s32 low = FARTHEST;
    s32 high = -FARTHEST;
    s32 mid_x = 0;
    s32 mid_y = 0;
    s32 mid_z = 0;
    s32 shown = 0;
    s32 ux;
    s32 uz;
    s32 length;
    s32 low_who = 0;
    s32 high_who = 1;
    s32 who;
    s32 slot;

    counted[0] = counted[1] = 0;
    center[0][0] = center[0][1] = center[1][0] = center[1][1] = 0;
    for (who = 0; who < PORT_DUO_FIGHTERS; who++) {
        fighters[who] = port_DuoNow(who);
    }
    /* the line from the first team to the second, through the living
     * (all four if a team has none) */
    for (who = 0; who < PORT_DUO_FIGHTERS; who++) {
        slot = PORT_DUO_SLOT(who);
        if (port_DuoAlive(who) ||
            (!port_DuoAlive(slot) && !port_DuoAlive(slot + 2))) {
            center[slot][0] += fighters[who]->camera_target.x;
            center[slot][1] += fighters[who]->camera_target.z;
            counted[slot]++;
        }
    }
    for (slot = 0; slot < 2; slot++) {
        center[slot][0] /= counted[slot];
        center[slot][1] /= counted[slot];
    }
    ux = center[1][0] - center[0][0];
    uz = center[1][1] - center[0][1];
    length = SquareRoot0((ux >> 4) * (ux >> 4) + (uz >> 4) * (uz >> 4)) << 4;
    if (length < LINE_SIXTEENTH) {
        ux = ONE;
        uz = 0;
    } else {
        ux = ux * (ONE / LINE_SIXTEENTH) / (length / LINE_SIXTEENTH);
        uz = uz * (ONE / LINE_SIXTEENTH) / (length / LINE_SIXTEENTH);
    }
    /* along it, the ends of those to be seen (the living, and the fallen
     * while the fight is decided), and across it the widest */
    for (who = 0; who < PORT_DUO_FIGHTERS; who++) {
        const Vec3 *at = &fighters[who]->camera_target;
        s32 along;
        if (!port_DuoAlive(who) && !duo->decided) {
            continue;
        }
        along = (at->x * (ux >> 4) + at->z * (uz >> 4)) >> (FIXED_SHIFT - 4);
        if (along < low) {
            low = along;
            low_who = who;
        }
        if (along > high) {
            high = along;
            high_who = who;
        }
        mid_x += at->x;
        mid_y += at->y;
        mid_z += at->z;
        shown++;
    }
    if (shown == 0) {
        *first_point = fighters[0]->camera_target;
        *second_point = fighters[1]->camera_target;
        *first_fighter = fighters[0];
        *second_fighter = fighters[1];
        return;
    }
    mid_x /= shown;
    mid_y /= shown;
    mid_z /= shown;
    {
        s32 mid_along =
            (mid_x * (ux >> 4) + mid_z * (uz >> 4)) >> (FIXED_SHIFT - 4);
        s32 mid_side =
            (mid_z * (ux >> 4) - mid_x * (uz >> 4)) >> (FIXED_SHIFT - 4);
        s32 spread_side = 0;

        /* those off the line want the view further back: half of the
         * widest off it on each end */
        for (who = 0; who < PORT_DUO_FIGHTERS; who++) {
            const Vec3 *at = &fighters[who]->camera_target;
            s32 side;

            if (!port_DuoAlive(who) && !duo->decided) {
                continue;
            }
            side =
                ((at->z * (ux >> 4) - at->x * (uz >> 4)) >> (FIXED_SHIFT - 4)) -
                mid_side;
            if (side < 0) {
                side = -side;
            }
            if (side > spread_side) {
                spread_side = side;
            }
        }
        low -= spread_side / 2;
        high += spread_side / 2;
        first_point->x = mid_x + ((low - mid_along) * ux >> FIXED_SHIFT);
        first_point->z = mid_z + ((low - mid_along) * uz >> FIXED_SHIFT);
        second_point->x = mid_x + ((high - mid_along) * ux >> FIXED_SHIFT);
        second_point->z = mid_z + ((high - mid_along) * uz >> FIXED_SHIFT);
        first_point->y = mid_y;
        second_point->y = mid_y;
    }
    /* the fighters it looks over the ground for: the two at the ends, the
     * first not one in a lock (whose camera would close in on it) */
    *first_fighter = fighters[low_who];
    *second_fighter = fighters[high_who];
    if (MOVE_IS_LOCK(fighters[low_who]->move_kind) &&
        !MOVE_IS_LOCK(fighters[high_who]->move_kind)) {
        *first_fighter = fighters[high_who];
        *second_fighter = fighters[low_who];
    }
}

/* ---- what the window shows ---- */

/* A world point on the game's screen (640 by 240), as the renderer
 * projects the frame: the view's rotation from the camera's place, then
 * the coprocessor's plane and offset. */
static s32 port_DuoProject(const Vec3 *at, PortDuoTag *tag) {
    const MATRIX *view = &g_render_view_matrix;
    const s32 *eye = g_node_camera_root_ptr->world.t;
    s32 rel[3];
    s32 out[3];
    s32 h = (s32)(port_GteReadControl(PORT_GTE_PLANE) & 0xFFFF);
    s32 ofx = (s32)port_GteReadControl(PORT_GTE_OFX) >> OFFSET_SHIFT;
    s32 ofy = (s32)port_GteReadControl(PORT_GTE_OFY) >> OFFSET_SHIFT;
    s32 i;

    /* in quarters, so that the products fit a word */
    rel[0] = (at->x - eye[0]) >> 2;
    rel[1] = (at->y - eye[1]) >> 2;
    rel[2] = (at->z - eye[2]) >> 2;
    for (i = 0; i < 3; i++) {
        out[i] = (view->m[i][0] * rel[0] + view->m[i][1] * rel[1] +
                  view->m[i][2] * rel[2]) >>
                 (FIXED_SHIFT - 2);
    }
    if (out[2] < NEAREST_DEPTH) {
        tag->seen = FALSE;
        return FALSE;
    }
    tag->x = (s16)(ofx + out[0] * h / out[2]);
    /* the frame is drawn squashed to half its height (render.c) */
    tag->y = (s16)(ofy + (out[1] / 2) * h / out[2]);
    tag->seen = TRUE;
    return TRUE;
}

/* What the window shows (port_duo.h): each fighter standing or not, his
 * target, the CPU's or not, and where over his head is on the screen. */
void port_DuoShown(PortDuoShown *shown) {
    const PortDuo *duo = &g_port_duo;
    s32 who;

    memset(shown, 0, sizeof(*shown));
    shown->on = duo->on && (g_sys_screen == SCREEN_FIGHT_UPDATE);
    if (!duo->on) {
        return;
    }
    for (who = 0; who < PORT_DUO_FIGHTERS; who++) {
        const Fighter *fighter = port_DuoNow(who);
        Vec3 above = fighter->head_pos;

        shown->alive[who] = (u8)!fighter->is_defeated;
        shown->target[who] = duo->target[who];
        shown->is_cpu[who] = (u8)(fighter->is_cpu != 0);
        above.y -= TAG_ABOVE_HEAD;
        port_DuoProject(&above, &shown->tag[who]);
    }
}
