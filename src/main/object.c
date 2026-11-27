/* Loose objects: the weapons that have left a fighter's hand. A fighter
 * carries a few sub-weapons (knives and the like, a kind for each
 * character) and throws them with its sub-weapon move; one that is thrown
 * flies, spins and falls, sticks in the other fighter or glances off, bounces
 * on the stage and comes to rest, where either fighter can pick it up. A
 * second sword that a wounded arm lets go of, and the weapon of a fighter
 * that is defeated, fall the same way. There are twelve objects at most.
 *
 * At the end of the file: the handfuls of dirt or water a fighter throws up
 * at the other from loose ground. */
#define INCLUDE_ASM_USE_MACRO_INC 1
#include "common.h"
#include "include_asm.h"
#include <psxsdk/inline_o.h>
#include <psxsdk/gtemac.h>
#include "bb2.h"

ThrownDirt g_object_dirt[DIRT_COUNT];
LooseObject g_object_pool[OBJECT_COUNT];

/* Whose sounds an object of this kind makes: the first fighter's if it is
 * the kind that fighter started with, otherwise the second's. */
#define OBJECT_SOUNDS_OF(kind) (((kind) ^ g_object_fighter_kinds[0]) != 0)

/* An event of an object: in two on two its sounds are its thrower's
 * (port_duo.h), where the game's are the slot's; `kind` is the kind whose
 * fighter's sounds the game gives it (OBJECT_SOUNDS_OF), OBJECT_NONE where
 * they are its owner's. */
#if RB_PORT
static void object_RaiseEvent(
    const LooseObject *obj, s32 kind, s32 fighter_index, s32 event, s32 *pos,
    s16 *dir) {
    port_DuoObjectSounds(obj - g_object_pool, kind);
    fight_RaiseEvent(fighter_index, event, pos, dir);
    port_DuoObjectSoundsDone();
}
#else
#define object_RaiseEvent(obj, kind, fighter_index, event, pos, dir)           \
    fight_RaiseEvent(fighter_index, event, pos, dir)
#endif

#define OBJECT_GRAVITY 13
/* The stage's floor is a little under where an object's point is. */
#define OBJECT_GROUND_CLEARANCE 8
/* An object this far down has fallen off the stage. */
#define OBJECT_LOWEST_Y 15000
/* A face is ground if its normal points up at least this much (up is
 * negative); anything steeper is a wall. */
#define OBJECT_WALL_NORMAL_Y (-0x7FF)
/* The hopping kind's hop. */
#define OBJECT_HOP_LIFT (-150)

/* The curving kind starts to turn this many frames into its flight, and the
 * turn dies away by the second. */
#define OBJECT_CURVE_FIRST_FRAME 14
#define OBJECT_CURVE_LAST_FRAME 78
#define OBJECT_CURVE_TURN_MAX 0x42

/* A fighter picks up what lies within this of its feet. */
#define OBJECT_PICK_UP_HEIGHT 100
#define OBJECT_PICK_UP_REACH_SQ 999999

/* Something let go of leaves with up to this much speed either way across
 * the ground and this much upward, and up to half as much again. */
#define OBJECT_DROP_DRIFT 0x80
#define OBJECT_DROP_LIFT 0x80
/* The bit of a random number that says which way it tumbles. */
#define OBJECT_DROP_TUMBLE_FORWARD 0x1000

/* What an object keeps of its spin a frame: the curving kind while it
 * curves, and everything else. */
#define OBJECT_CURVE_SPIN_KEPT(spin) ((spin) * 63 / 64)
#define OBJECT_SPIN_KEPT(spin) ((spin) * 15 / 16)
/* The spin a knock gives, by the square of the speed along the ground. */
#define OBJECT_KNOCK_SPIN(speed_sq) ((speed_sq) / 64)
/* An object that touches down with more than this (the square of 4 a frame)
 * along the ground is heard. */
#define OBJECT_LANDING_SPEED_SQ 16
/* One whose speed is within these, downward and along the ground, is all
 * but still. */
#define OBJECT_STILL_FALL 15
#define OBJECT_STILL_DRIFT 3

/* The hopping kind's hop: its speed along the ground, and the bits of a
 * random number that must all be clear for it to hop of itself, five of
 * them: one frame in 32. */
#define OBJECT_HOP_SPEED 64
#define OBJECT_HOP_CHANCE_MASK 0x133

u8 g_object_fighter_kinds[2];

/* Defined further down. */
static s32 object_EaseAngle(s32 angle, s32 target, s32 kept);

/* ---- stuck in a fighter ---- */

/* The object has gone into the other fighter at one of its bones: from now
 * on it is carried by that bone. Its place and its turn are taken into the
 * bone's frame. */
static void object_StickInBone(LooseObject *obj, s16 bone) {
    MATRIX bone_inverse;
    MatrixSlot *bone_matrices;
    MATRIX *bone_matrix;
    MATRIX *stuck_matrix;

    obj->is_stuck = 1;
    obj->stuck_bone = bone;
    bone_matrices = model_GetBoneMatrices(obj->owner < 1);
    stuck_matrix = &obj->stuck_matrix;
    bone_matrix = PTR(bone_matrices[obj->stuck_bone]);

    obj->stuck_matrix.m[0][0] = ONE;
    obj->stuck_matrix.m[0][1] = 0;
    obj->stuck_matrix.m[0][2] = 0;
    obj->stuck_matrix.m[1][0] = 0;
    obj->stuck_matrix.m[1][1] = ONE;
    obj->stuck_matrix.m[1][2] = 0;
    obj->stuck_matrix.m[2][0] = 0;
    obj->stuck_matrix.m[2][1] = 0;
    obj->stuck_matrix.m[2][2] = ONE;
    RotMatrixX(obj->rot[0], stuck_matrix);
    RotMatrixY(obj->rot[1], stuck_matrix);
    RotMatrixZ(obj->rot[2], stuck_matrix);
    math_InvertMatrix(bone_matrix, &bone_inverse);
    MulMatrix0(&bone_inverse, stuck_matrix, stuck_matrix);

    obj->pos.x -= bone_matrix->t[0];
    obj->pos.y -= bone_matrix->t[1];
    obj->pos.z -= bone_matrix->t[2];

    gte_SetRotMatrix(&bone_inverse);
    gte_ldlv0(&obj->pos);
    gte_rtv0();
    gte_stlvnl(&obj->pos);

    /* half way out again, so that it does not sit too deep */
    obj->pos.x >>= 1;
    obj->pos.y >>= 1;
    obj->pos.z >>= 1;
}

/* Queues an object that is stuck in a fighter for drawing, where the bone
 * it is in has carried it. */
static void object_QueueStuck(LooseObject *obj) {
    s32 pos[3];
    s16 rot[4];
    MATRIX world;
    MatrixSlot *bone_matrices;
    MATRIX *bone_matrix;
    s32 weapon_model;

    bone_matrices = model_GetBoneMatrices(obj->owner < 1);
    bone_matrix = PTR(bone_matrices[obj->stuck_bone]);

    gte_SetRotMatrix(bone_matrix);
    gte_ldlv0(&obj->pos);
    gte_rtv0();
    gte_stlvnl(pos);

    pos[0] += bone_matrix->t[0];
    pos[1] += bone_matrix->t[1];
    pos[2] += bone_matrix->t[2];

    MulMatrix0(bone_matrix, &obj->stuck_matrix, &world);
    math_MatrixToAngles(&world, rot);

    weapon_model = g_object_weapon_models[obj->kind];
    weapon_Queue(weapon_model, WEAPON_DRAW_FREE, pos, rot);
    replay_RecordObjectDraw(obj->slot, weapon_model, pos, rot);
}

/* Queues every object for drawing, and tells the replay. An object is not
 * drawn in the first frame of its flight. */
static void object_QueueAll(void) {
    LooseObject *obj;
    s32 i;
    s16 kind;
    s32 weapon_model;

    for (obj = g_object_pool, i = 0; i < OBJECT_COUNT; i++, obj++) {
        kind = obj->kind;
        if (kind == OBJECT_NONE) {
            continue;
        }
        if (obj->is_stuck) {
            object_QueueStuck(obj);
            continue;
        }
        if (obj->frames_moving < 2) {
            continue;
        }
        weapon_model = g_object_weapon_models[kind];
        if (OBJECT_IS_WEAPON(kind)) {
            weapon_model = obj->weapon_model;
        } else if (
            kind == OBJECT_FROG && obj->rest_state == OBJECT_REST_HOPPING) {
            weapon_model += WEAPON_MODEL_FROG_IN_AIR;
        }
        weapon_Queue(weapon_model, WEAPON_DRAW_FREE, &obj->pos.x, obj->rot);
        replay_RecordObjectDraw(obj->slot, weapon_model, &obj->pos.x, obj->rot);
    }
}

/* Nothing calls this. It mirrors an object's speed about the line `heading`
 * points along on the ground and leaves an eighth of it, and a quarter of
 * its fall: a rebound, at a guess from something upright. */
static void object_Rebound(LooseObject *obj, s16 *heading) {
    s32 angle;
    s32 cosine;
    s32 sine;
    s32 vx;
    s32 vz;
    s32 along;
    s32 across;

    angle = ratan2(heading[0], heading[2]);
    cosine = g_math_sin_table[(angle + ANGLE_TURN / 4) & ANGLE_MASK];
    sine = g_math_sin_table[angle & ANGLE_MASK];
    vx = obj->vel.x;
    vz = obj->vel.z;
    along = ((vx * cosine) + (vz * sine)) >> 12;
    across = -((((-vx) * sine) + (vz * cosine)) >> 12);
    obj->vel.x = ((along * cosine) - (across * sine)) >> 15;
    obj->vel.z = ((along * sine) + (across * cosine)) >> 15;
    obj->vel.y /= 4;
}

/* ---- a round's start and end ---- */

void object_ClearAll(void) {
    s32 i;

    for (i = 0; i < OBJECT_COUNT; i++) {
        g_object_pool[i].kind = OBJECT_NONE;
        g_object_pool[i].slot = OBJECT_NO_SLOT;
    }
}

/* Gives a fighter the sub-weapons its character starts a fight with. */
void object_InitSubWeapons(Fighter *fighter) {
    s32 i;
    s16 kind;

    fighter->sub_weapon_count = 0;
    for (i = 0; i < SUB_WEAPONS_START; i++) {
        kind = g_object_start_sub_weapons[fighter->character][i];
        fighter->sub_weapons[i] = kind;
        if (kind != OBJECT_NONE) {
            fighter->sub_weapon_count++;
        }
    }
    g_object_fighter_kinds[fighter->index] =
        g_object_start_sub_weapons[fighter->character][0];
}

/* Takes out what is stuck in a fighter, for the next round. */
void object_RemoveStuck(void) {
    s32 i;

    for (i = 0; i < OBJECT_COUNT; i++) {
        if (g_object_pool[i].kind != OBJECT_NONE) {
            if (g_object_pool[i].is_stuck) {
                g_object_pool[i].kind = OBJECT_NONE;
            }
        }
    }
}

/* ---- throwing, dropping, picking up ---- */

/* A new object of `kind` leaves the fighter the way it faces, at the speed,
 * lift and spin of its kind. With all twelve places taken it is written
 * after the last: the original's behaviour. */
static LooseObject *object_Launch(Fighter *fighter, s32 kind) {
    VECTOR unused; /* nothing uses it */
    LooseObject *obj;
    LooseObjectKind *params;
    s32 i;

    obj = g_object_pool;
    for (i = 0; i < OBJECT_COUNT; i++, obj++) {
        if (obj->kind == OBJECT_NONE && obj->slot == OBJECT_NO_SLOT) {
            break;
        }
    }
#if RB_FIX_OBJECT_POOL
    /* no place is free: the new object takes the last one's, where it
     * was written after the pool */
    if (g_rb_settings.bugfixes && i == OBJECT_COUNT) {
        i = OBJECT_COUNT - 1;
        obj = &g_object_pool[i];
    }
#endif
    obj->slot = i;
    obj->kind = kind;
    obj->drop_state = OBJECT_DROP_NONE;
    obj->is_stuck = 0;
    obj->can_hit_fighter = 1;
    obj->owner = fighter->index;
#if RB_PORT
    port_DuoObjectLaunched(i, fighter->index);
#endif
    obj->pos.x = fighter->world_pos.x;
    /* above the fighter's place by its size: 128 for one of full size */
    obj->pos.y = fighter->world_pos.y - fighter->size_scale / 32;
    obj->pos.z = fighter->world_pos.z;
    params = &g_object_kinds[kind];
    obj->vel.x = (g_math_sin_table[fighter->orientation.vy & ANGLE_MASK] *
                  params->launch_speed) >>
                 12;
    obj->vel.y = params->launch_lift;
    obj->vel.z = (g_math_sin_table[(fighter->orientation.vy + ANGLE_TURN / 4) &
                                   ANGLE_MASK] *
                  params->launch_speed) >>
                 12;
    /* it starts a frame and a half out from the fighter */
    obj->pos.x += obj->vel.x;
    obj->pos.y += obj->vel.y;
    obj->pos.z += obj->vel.z;
    obj->pos.x += obj->vel.x / 2;
    obj->pos.y += obj->vel.y / 2;
    obj->pos.z += obj->vel.z / 2;
    obj->prev_pos = obj->pos;
    obj->rot[0] = 0;
    obj->rot[1] = fighter->orientation.vy;
    obj->rot[2] = 0;
    if (params->flight == OBJECT_SPINS_FLAT) {
        obj->rot_vel[0] = 0;
        obj->rot_vel[1] = params->spin_rate;
        obj->rot_vel[2] = 0;
    } else if (params->flight == OBJECT_TUMBLES) {
        obj->rot_vel[0] = params->spin_rate;
        obj->rot_vel[1] = 0;
        obj->rot_vel[2] = 0;
    } else if (params->flight == OBJECT_CURVES) {
        obj->rot_vel[0] = 0;
        obj->rot_vel[1] = params->spin_rate;
        obj->rot_vel[2] = 0;
    } else {
        obj->rot_vel[0] = 0;
        obj->rot_vel[1] = 0;
        obj->rot_vel[2] = 0;
    }
    obj->is_moving = 1;
    obj->rest_state = OBJECT_REST_NOT_YET;
    obj->frames_moving = 0;
    return obj;
}

/* The fighter throws one of the sub-weapons it carries: the first, or, for
 * a fighter with a second sword that still has something else to throw, the
 * something else. Returns the kind thrown, or OBJECT_NONE if it has
 * nothing. */
s32 object_ThrowSubWeapon(Fighter *fighter) {
    s32 count;
    s32 which;
    s32 kind;
    s32 thrown_kind;
    LooseObject *obj;
    s32 i;

    count = fighter->sub_weapon_count;
    if (count == 0) {
        return OBJECT_NONE;
    }
    which = 0;
    if (count >= 2 && fighter->two_sword_stance != -1) {
        which = fighter->sub_weapons[0] == fighter->sub_weapon_kind;
    }
    thrown_kind = fighter->sub_weapons[which];
    obj = object_Launch(fighter, thrown_kind);
    for (i = which; i < fighter->sub_weapon_count - 1; i++) {
        fighter->sub_weapons[i] = fighter->sub_weapons[i + 1];
    }

    fighter->sub_weapon_count--;
    kind = obj->kind;
    if (kind == OBJECT_FROG) {
        object_RaiseEvent(obj, OBJECT_FROG, OBJECT_SOUNDS_OF(OBJECT_FROG),
                          FIGHT_EVENT_HOP, &obj->pos.x, NULL);
    } else {
        object_RaiseEvent(obj, kind, OBJECT_SOUNDS_OF(kind), FIGHT_EVENT_THROW,
                          &obj->pos.x, NULL);
    }
    return thrown_kind;
}

/* The first sub-weapon the fighter carries falls from `position`: it drops
 * with a little speed any way and turns as it falls. It cannot hurt. */
void object_DropSubWeapon(Fighter *fighter, Vec3 *position) {
    LooseObject *obj;
    s32 random;
    s32 i;

    obj = object_Launch(fighter, fighter->sub_weapons[0]);
    obj->can_hit_fighter = 0;
    obj->pos = *position;
    obj->vel.x = (rng_Next() & (2 * OBJECT_DROP_DRIFT - 1)) - OBJECT_DROP_DRIFT;
    obj->vel.y = -(rng_Next() & (OBJECT_DROP_LIFT / 2 - 1)) - OBJECT_DROP_LIFT;
    obj->vel.z = (rng_Next() & (2 * OBJECT_DROP_DRIFT - 1)) - OBJECT_DROP_DRIFT;
    /* it tumbles an eighth to three eighths of a turn a frame, one way or
     * the other, and turns up to a quarter either way */
    random = rng_Next();
    if (random & OBJECT_DROP_TUMBLE_FORWARD) {
        obj->rot_vel[0] = (random & (ANGLE_TURN / 4 - 1)) + ANGLE_TURN / 8;
    } else {
        obj->rot_vel[0] = -(random & (ANGLE_TURN / 4 - 1)) - ANGLE_TURN / 8;
    }
    obj->rot_vel[1] = (rng_Next() & (ANGLE_TURN / 2 - 1)) - ANGLE_TURN / 4;
    obj->rot_vel[2] = 0;
    obj->drop_state = OBJECT_DROP_RELEASED;
    for (i = 0; i < fighter->sub_weapon_count - 1; i++) {
        fighter->sub_weapons[i] = fighter->sub_weapons[i + 1];
    }
    fighter->sub_weapon_count--;
}

/* A defeated fighter's own weapon falls from `position`, the same way. The
 * object is of the kind that stands for the weapon and is drawn as the
 * model the fighter had. */
void object_DropWeapon(Fighter *fighter, s32 kind, Vec3 *position) {
    LooseObject *obj;
    s32 random;

    obj = object_Launch(fighter, kind);
    obj->can_hit_fighter = 0;
    obj->pos = *position;
    obj->vel.x = (rng_Next() & (2 * OBJECT_DROP_DRIFT - 1)) - OBJECT_DROP_DRIFT;
    obj->vel.y = -(rng_Next() & (OBJECT_DROP_LIFT / 2 - 1)) - OBJECT_DROP_LIFT;
    obj->vel.z = (rng_Next() & (2 * OBJECT_DROP_DRIFT - 1)) - OBJECT_DROP_DRIFT;
    /* tumbling and turning as a sub-weapon let go of does */
    random = rng_Next();
    if (random & OBJECT_DROP_TUMBLE_FORWARD) {
        obj->rot_vel[0] = (random & (ANGLE_TURN / 4 - 1)) + ANGLE_TURN / 8;
    } else {
        obj->rot_vel[0] = -(random & (ANGLE_TURN / 4 - 1)) - ANGLE_TURN / 8;
    }
    obj->rot_vel[1] = (rng_Next() & (ANGLE_TURN / 2 - 1)) - ANGLE_TURN / 4;
    obj->rot_vel[2] = 0;
    obj->drop_state = OBJECT_DROP_RELEASED;
    obj->weapon_model = fighter->weapon_model;
}

/* Adds a sub-weapon to those the fighter carries, unless it has twelve. A
 * second sword goes to the front, where the two-sword stance looks for it;
 * anything else to the back. */
static s32 object_AddSubWeapon(Fighter *fighter, s32 kind) {
    s32 i;

    if (fighter->sub_weapon_count == SUB_WEAPON_COUNT) {
        return 0;
    }
    if (fighter->two_sword_stance != -1 && kind == fighter->sub_weapon_kind) {
        for (i = fighter->sub_weapon_count; i > 0; i--) {
            fighter->sub_weapons[i] = fighter->sub_weapons[i - 1];
        }
        fighter->sub_weapons[0] = kind;
        fighter->sub_weapon_count++;
    } else {
        fighter->sub_weapons[fighter->sub_weapon_count++] = kind;
    }
    return 1;
}

/* The fighter picks up the first sub-weapon lying still within reach of
 * its feet. Returns its kind, or OBJECT_NONE if there is none or the
 * fighter can carry no more. */
s32 object_PickUp(Fighter *fighter) {
    LooseObject *obj;
    s32 i;
    s32 picked_kind;

    for (i = 0, obj = g_object_pool; i < OBJECT_COUNT; i++, obj++) {
        s16 kind = obj->kind;
        if (OBJECT_IS_WEAPON(kind)) {
            continue;
        }
        if (kind == OBJECT_NONE) {
            continue;
        }
        if (obj->is_moving) {
            continue;
        }
        {
            s32 feet_y = fighter->foot_pos.vy;
            s32 y = obj->pos.y;
            if (feet_y - OBJECT_PICK_UP_HEIGHT >= y) {
                continue;
            }
            if (y >= feet_y + OBJECT_PICK_UP_HEIGHT) {
                continue;
            }
        }
        {
            s32 dx = fighter->world_pos.x - obj->pos.x;
            s32 dz = fighter->world_pos.z - obj->pos.z;
            if (dx * dx + dz * dz > OBJECT_PICK_UP_REACH_SQ) {
                continue;
            }
        }
        if (!object_AddSubWeapon(fighter, kind)) {
            return OBJECT_NONE;
        }
        picked_kind = obj->kind;
        obj->kind = OBJECT_NONE;
        if (picked_kind == OBJECT_FROG) {
            object_RaiseEvent(obj, OBJECT_FROG, OBJECT_SOUNDS_OF(OBJECT_FROG),
                              FIGHT_EVENT_HOP, &fighter->world_pos.x, NULL);
        } else {
            fight_RaiseEvent(fighter->index, FIGHT_EVENT_PICK_UP,
                             &fighter->world_pos.x, NULL);
        }
        return picked_kind;
    }
    return OBJECT_NONE;
}

/* Takes away the weapons that defeated fighters dropped, for the next
 * round. */
void object_RemoveDroppedWeapons(void) {
    s32 i;

    for (i = 0; i < OBJECT_COUNT; i++) {
        if (OBJECT_IS_WEAPON(g_object_pool[i].kind)) {
            g_object_pool[i].kind = OBJECT_NONE;
        }
    }
}

/* An empty function that nothing calls. */
static void object_Unused(void) {}

/* ---- a frame ---- */

/* `angle` brought toward `target` the short way round, keeping `kept`
 * (4.12) of the difference. */
static s32 object_EaseAngle(s32 angle, s32 target, s32 kept) {
    angle = (angle - target) & ANGLE_MASK;
    if (angle >= ANGLE_TURN / 2) {
        angle -= ANGLE_TURN;
    }
    return target + ((angle * kept) >> 12);
}

/* Moves every loose object a frame: spin, gravity, a cast against the stage
 * along the frame's step, and off whatever it meets a bounce with what the
 * kind keeps of its speed. An object that is all but still on the ground
 * comes to rest. Then all of them are queued for drawing. */
void object_Update(void) {
    ObjectCastScratch *scr;
    LooseObject *obj;
    s32 i;
    s32 half_turn;
    s32 into_face;
    s32 ground_speed_sq;
    s16 kind;
    s16 *normal;

    scr = &SPAD->tail.object_cast;
    obj = g_object_pool;
    for (i = 0; i < OBJECT_COUNT; i++, obj++) {
        /* the curve's turn this frame; later, what the kind keeps of its
         * speed in a bounce */
        s32 work;
        /* the heading the object flies along; later, whether the cast met
         * the stage */
        s32 heading_then_hit;

        if (obj->kind == OBJECT_NONE) {
            obj->slot = OBJECT_NO_SLOT;
            continue;
        }
        if (obj->is_stuck) {
            continue;
        }
        obj->prev_pos = obj->pos;
        if (obj->is_moving) {
            obj->frames_moving++;
        }
        if (obj->kind == OBJECT_BOOMERANG && obj->is_moving &&
            obj->rest_state == OBJECT_REST_NOT_YET &&
            obj->frames_moving >= OBJECT_CURVE_FIRST_FRAME) {
            /* The curving kind in flight: its speed is turned to one side
             * and tipped upward by half as much, less each frame, and it
             * keeps its spin longer. The speed is turned to lie along z,
             * pitched there, and turned back a little further round. The
             * cast's hit point is only where the turned speed is kept. */
            heading_then_hit = ratan2(obj->vel.x, obj->vel.z);
            /* one and a half for every frame of the curve still to come */
            work = (OBJECT_CURVE_LAST_FRAME - obj->frames_moving) * 96 / 64;
            if (work < 0) {
                work = 0;
            } else if (work > OBJECT_CURVE_TURN_MAX) {
                work = OBJECT_CURVE_TURN_MAX;
            }
            scr->hit_point.x =
                (g_math_sin_table[(heading_then_hit + ANGLE_TURN / 4) &
                                  ANGLE_MASK] *
                     obj->vel.x -
                 g_math_sin_table[heading_then_hit & ANGLE_MASK] *
                     obj->vel.z) >>
                12;
            scr->hit_point.y = obj->vel.y;
            scr->hit_point.z =
                (g_math_sin_table[heading_then_hit & ANGLE_MASK] * obj->vel.x +
                 g_math_sin_table[(heading_then_hit + ANGLE_TURN / 4) &
                                  ANGLE_MASK] *
                     obj->vel.z) >>
                12;
            scr->pitched_vel.x = scr->hit_point.x;
            half_turn = work / 2;
            scr->pitched_vel.y =
                (g_math_sin_table[(half_turn + ANGLE_TURN / 4) & ANGLE_MASK] *
                     scr->hit_point.y -
                 g_math_sin_table[half_turn & ANGLE_MASK] * scr->hit_point.z) >>
                12;
            scr->pitched_vel.z =
                (g_math_sin_table[half_turn & ANGLE_MASK] * scr->hit_point.y +
                 g_math_sin_table[(half_turn + ANGLE_TURN / 4) & ANGLE_MASK] *
                     scr->hit_point.z) >>
                12;
            obj->vel.x =
                (g_math_sin_table[(work - heading_then_hit + ANGLE_TURN / 4) &
                                  ANGLE_MASK] *
                     scr->pitched_vel.x -
                 g_math_sin_table[(work - heading_then_hit) & ANGLE_MASK] *
                     scr->pitched_vel.z) >>
                12;
            obj->vel.y = scr->pitched_vel.y;
            obj->vel.z =
                (g_math_sin_table[(work - heading_then_hit) & ANGLE_MASK] *
                     scr->pitched_vel.x +
                 g_math_sin_table[(work - heading_then_hit + ANGLE_TURN / 4) &
                                  ANGLE_MASK] *
                     scr->pitched_vel.z) >>
                12;
            obj->rot_vel[0] = OBJECT_CURVE_SPIN_KEPT(obj->rot_vel[0]);
            obj->rot_vel[1] = OBJECT_CURVE_SPIN_KEPT(obj->rot_vel[1]);
            obj->rot_vel[2] = OBJECT_CURVE_SPIN_KEPT(obj->rot_vel[2]);
        } else {
            obj->rot_vel[0] = OBJECT_SPIN_KEPT(obj->rot_vel[0]);
            obj->rot_vel[1] = OBJECT_SPIN_KEPT(obj->rot_vel[1]);
            obj->rot_vel[2] = OBJECT_SPIN_KEPT(obj->rot_vel[2]);
        }
        obj->rot[0] += obj->rot_vel[0];
        obj->rot[1] += obj->rot_vel[1];
        obj->rot[2] += obj->rot_vel[2];
        obj->vel.y += OBJECT_GRAVITY;
        scr->end_slot.cast_end.x = obj->pos.x + obj->vel.x;
        scr->end_slot.cast_end.y = obj->pos.y + obj->vel.y;
        scr->end_slot.cast_end.z = obj->pos.z + obj->vel.z;
        obj->pos.y -= OBJECT_GROUND_CLEARANCE;
        normal = scr->cast_normal;
        heading_then_hit = collision_CastMoveWith(
            &obj->pos.x, &scr->end_slot.cast_end.x, &scr->hit_point.x, normal,
            &scr->cast_work);
        if (heading_then_hit && collision_GetSurface() == SURFACE_BARRIER) {
            heading_then_hit = 0;
        }
        if (heading_then_hit) {
            if (obj->kind == OBJECT_BURSTING) {
                object_RaiseEvent(
                    obj, OBJECT_NONE, obj->owner, FIGHT_EVENT_BULLET_IMPACT,
                    &scr->hit_point.x, normal);
                obj->kind = OBJECT_NONE;
                continue;
            }
            /* it leaves the face with the speed it came in at turned back
             * out of it: twice its speed into the face is taken off along
             * the normal */
            into_face = (obj->vel.x * normal[0] + obj->vel.y * normal[1] +
                         obj->vel.z * normal[2]) /
                        (ONE / 2);
            obj->vel.x -= normal[0] * into_face / ONE;
            obj->vel.y -= normal[1] * into_face / ONE;
            obj->vel.z -= normal[2] * into_face / ONE;
            obj->pos = scr->hit_point;
            kind = obj->kind;
            work = g_object_kinds[kind].bounce;
            if (obj->is_moving) {
                obj->vel.x = obj->vel.x * work / ONE;
                obj->vel.y = obj->vel.y * work / ONE;
                obj->vel.z = obj->vel.z * work / ONE;
                ground_speed_sq =
                    obj->vel.x * obj->vel.x + obj->vel.z * obj->vel.z;
                if (scr->cast_normal[1] >= OBJECT_WALL_NORMAL_Y) {
                    /* off a wall it is knocked spinning, and one that could
                     * still hurt strikes a spark */
                    obj->rot_vel[1] +=
                        (rng_Next() & 1) ? OBJECT_KNOCK_SPIN(ground_speed_sq)
                                         : OBJECT_KNOCK_SPIN(-ground_speed_sq);
                    if (obj->kind != OBJECT_FROG && obj->can_hit_fighter) {
                        fight_RaiseEvent(
                            obj->owner, FIGHT_EVENT_SPARK, &obj->pos.x, NULL);
                    }
                }
                if (obj->kind == OBJECT_FROG) {
                    if (obj->can_hit_fighter) {
                        object_RaiseEvent(
                            obj, obj->kind, OBJECT_SOUNDS_OF(obj->kind),
                            FIGHT_EVENT_HOP, &obj->pos.x, NULL);
                    }
                } else if (obj->kind < OBJECT_WEAPON_FIRST) {
                    if (ground_speed_sq > OBJECT_LANDING_SPEED_SQ) {
                        object_RaiseEvent(
                            obj, obj->kind, OBJECT_SOUNDS_OF(obj->kind),
                            FIGHT_EVENT_OBJECT_LANDS, &obj->pos.x, NULL);
                    }
                } else if (OBJECT_IS_WEAPON(obj->kind)) {
                    if (ground_speed_sq > OBJECT_LANDING_SPEED_SQ) {
                        object_RaiseEvent(
                            obj, OBJECT_NONE, obj->owner,
                            FIGHT_EVENT_WEAPON_LANDS, &obj->pos.x, NULL);
                    }
                }
            } else if (scr->cast_normal[1] >= OBJECT_WALL_NORMAL_Y &&
                       kind == OBJECT_FROG) {
                /* the hopping kind does not lie against a wall: it turns
                 * about (half a turn, a thirty-second more or less) and
                 * hops away */
                if (obj->rest_state == OBJECT_REST_AT_REST) {
                    obj->is_moving = 1;
                    obj->rot[1] += ANGLE_TURN / 2 - ANGLE_TURN / 32 +
                                   (rng_Next() & (ANGLE_TURN / 16 - 1));
                    obj->vel.x = g_math_sin_table[obj->rot[1] & ANGLE_MASK] /
                                 (ONE / OBJECT_HOP_SPEED);
                    obj->vel.y = OBJECT_HOP_LIFT;
                    obj->vel.z =
                        g_math_sin_table[(obj->rot[1] + ANGLE_TURN / 4) &
                                         ANGLE_MASK] /
                        (ONE / OBJECT_HOP_SPEED);
                    obj->rest_state = OBJECT_REST_HOPPING;
                    object_RaiseEvent(
                        obj, obj->kind, OBJECT_SOUNDS_OF(obj->kind),
                        FIGHT_EVENT_HOP, &obj->pos.x, NULL);
                }
            }
            obj->can_hit_fighter = 0;
            if (obj->drop_state == OBJECT_DROP_RELEASED) {
                obj->drop_state = OBJECT_DROP_SETTLING;
            }
            if (obj->rest_state == OBJECT_REST_HOPPING) {
                obj->rest_state = OBJECT_REST_HOPPED;
            }
        } else {
            obj->pos = scr->end_slot.cast_end;
        }
        if (obj->vel.y >= -OBJECT_STILL_FALL &&
            obj->vel.y <= OBJECT_STILL_FALL &&
            obj->vel.x >= -OBJECT_STILL_DRIFT &&
            obj->vel.x <= OBJECT_STILL_DRIFT &&
            obj->vel.z >= -OBJECT_STILL_DRIFT &&
            obj->vel.z <= OBJECT_STILL_DRIFT) {
            /* all but still: on the ground it comes to rest (in chanbara
             * it is gone instead) */
            if (heading_then_hit) {
                if (g_fight_mode == FIGHT_MODE_CHANBARA) {
                    obj->kind = OBJECT_NONE;
                } else {
                    obj->is_moving = 0;
                    obj->rest_state = OBJECT_REST_AT_REST;
                    obj->vel.x = 0;
                    obj->vel.y = 0;
                    obj->vel.z = 0;
                }
            }
        } else {
            obj->is_moving = 1;
        }
        if (obj->kind == OBJECT_FROG) {
            /* now and then the hopping kind hops off the way it faces */
            if (obj->rest_state == OBJECT_REST_AT_REST &&
                !(rng_Next() & OBJECT_HOP_CHANCE_MASK)) {
                obj->is_moving = 1;
                /* with a spin of up to 64 a frame either way */
                obj->rot_vel[1] += (rng_Next() & 0x7F) - 0x40;
                obj->vel.x = g_math_sin_table[obj->rot[1] & ANGLE_MASK] /
                             (ONE / OBJECT_HOP_SPEED);
                obj->vel.y = OBJECT_HOP_LIFT;
                obj->vel.z = g_math_sin_table[(obj->rot[1] + ANGLE_TURN / 4) &
                                              ANGLE_MASK] /
                             (ONE / OBJECT_HOP_SPEED);
                obj->rest_state = OBJECT_REST_HOPPING;
                object_RaiseEvent(obj, obj->kind, OBJECT_SOUNDS_OF(obj->kind),
                                  FIGHT_EVENT_HOP, &obj->pos.x, NULL);
            }
        }
        if (obj->drop_state == OBJECT_DROP_SETTLING) {
            /* something dropped turns to lie on its side */
            obj->rot[1] = object_EaseAngle(obj->rot[1], 0, ONE / 2);
            obj->rot[2] =
                object_EaseAngle(obj->rot[2], ANGLE_TURN / 4, 7 * ONE / 8);
            obj->rot_vel[1] = obj->rot_vel[1] * 3 / 4;
        } else if (!obj->can_hit_fighter &&
                   g_object_kinds[obj->kind].flight == OBJECT_TUMBLES) {
            /* and so does a tumbling one that has landed, on the other.
             * (One that chanbara let go above has no kind any more, and
             * the table is read one entry ahead of its first: the
             * original's behaviour.) */
            obj->rot[0] = object_EaseAngle(obj->rot[0], 0, 7 * ONE / 8);
            obj->rot[1] = object_EaseAngle(obj->rot[1], 0, 3 * ONE / 8);
            obj->rot[2] =
                object_EaseAngle(obj->rot[2], -ANGLE_TURN / 4, 7 * ONE / 8);
        }
        if (obj->pos.y > OBJECT_LOWEST_Y) {
            obj->kind = OBJECT_NONE;
        }
    }
    object_QueueAll();
}

/* ---- hitting a fighter ---- */

/* An object that struck a fighter without going in flies off it: turned by
 * an angle that goes with the body point it struck and tipped by one made
 * up from where it is, with a quarter of its speed, or an eighth if that
 * turns it back the way it came. */
static void object_GlanceOff(
    WeaponHitScratch *scr, LooseObject *obj, s32 body_point) {
    MATRIX *turn;
    s32 yaw;
    s32 pitch;
    s32 ground_speed_sq;
    s32 spin;

    if (obj->kind != OBJECT_FROG) {
        s32 vx = obj->vel.x;
        s32 vz = obj->vel.z;
        s32 spin_y = obj->rot_vel[1];
        ground_speed_sq = vx * vx + vz * vz;
        if (rng_Next() & 1) {
            spin = OBJECT_KNOCK_SPIN(ground_speed_sq);
        } else {
            spin = OBJECT_KNOCK_SPIN(-ground_speed_sq);
        }
        obj->rot_vel[1] = spin_y + spin;
    }

    turn = &scr->rot_matrix;
    yaw = g_object_glance_angles[body_point] & ANGLE_MASK;
    /* up to a quarter of a turn either way, by a mix of its coordinates */
    pitch = (((obj->pos.x * 16) + obj->pos.y + (obj->pos.z * 8)) &
             (ANGLE_TURN / 2 - 1)) -
            ANGLE_TURN / 4;
    scr->rot_matrix.m[0][0] = ONE;
    scr->rot_matrix.m[0][1] = 0;
    scr->rot_matrix.m[0][2] = 0;
    scr->rot_matrix.m[1][0] = 0;
    scr->rot_matrix.m[1][1] = ONE;
    scr->rot_matrix.m[1][2] = 0;
    scr->rot_matrix.m[2][0] = 0;
    scr->rot_matrix.m[2][1] = 0;
    scr->rot_matrix.m[2][2] = ONE;
    RotMatrixY(yaw, turn);
    RotMatrixX(pitch, turn);

    gte_SetRotMatrix(turn);
    gte_ldlv0(&obj->vel);
    gte_rtv0();
    gte_stlvnl(&obj->vel);

    /* turned by more than a quarter and less than three: back the way it
     * came */
    if ((u32)(yaw - (ANGLE_TURN / 4 + 1)) < ANGLE_TURN / 2 - 1) {
        obj->vel.x /= 8;
        obj->vel.y /= 8;
        obj->vel.z /= 8;
    } else {
        obj->vel.x /= 4;
        obj->vel.y /= 4;
        obj->vel.z /= 4;
    }
    obj->pos.x += obj->vel.x / 2;
    obj->pos.y += obj->vel.y / 2;
    obj->pos.z += obj->vel.z / 2;
}

/* Tests every object in flight that can still hurt against the fighter it
 * was thrown at: the frame's step against the fighter's body points, as a
 * blade's edge is tested. What the hit does to the fighter is the fighter's
 * code; here the object sticks in, or glances off, or (the bursting kind)
 * is gone. */
void object_HitFighters(void) {
    WeaponHitScratch *scr = &SPAD->tail.weapon_hit;
    s32 i;
    s32 core_hit;
    LooseObject *obj;
    Vec3 *step = scr->point_slots.work_points;
    Fighter *target;
    s32 target_index;
    u16 move_kind;
    BodyPoint *body_point;
    s32 j; /* the body point's number */
    s32 hit;
    s32 facing_away;
    s32 result;
    s32 kind;
    s32 clashed;

    obj = g_object_pool;
    for (i = 0; i < OBJECT_COUNT; i++, obj++) {
        if (obj->kind == OBJECT_NONE) {
            continue;
        }
        if (!obj->can_hit_fighter) {
            continue;
        }
        if (!obj->is_moving) {
            continue;
        }
        target_index = obj->owner == 0;
        target = &g_fighters[target_index];
        /* nothing can be hit in these moves */
        move_kind = target->move_kind;
        if (move_kind == MOVE_KIND_END_GAME || move_kind == MOVE_KIND_DTH ||
            move_kind == MOVE_KIND_LOCK || move_kind == MOVE_KIND_START ||
            move_kind == MOVE_KIND_LOCK_EVEN ||
            move_kind == MOVE_KIND_LOCK_WINNING_1 ||
            move_kind == MOVE_KIND_LOCK_WINNING_2 ||
            move_kind == MOVE_KIND_LOCK_LOSING_1 ||
            move_kind == MOVE_KIND_LOCK_LOSING_2 ||
            move_kind == MOVE_KIND_PAIRED) {
            continue;
        }

        step[0] = obj->prev_pos;
        step[1] = obj->pos;
        SET_PTR(scr->shape_points[0], &step[0]);
        SET_PTR(scr->shape_points[1], &step[1]);
        hit_FrameSegment(scr);

        /* the first body point the step passes through; an arm that is lost
         * has none */
        hit = 0;
        body_point = g_fighter_body_points[target_index];
        for (j = 0; j < BODY_POINT_COUNT; j++, body_point++) {
            Vec3 *point;
            if (!target->off_arm_ok && j >= BODY_OFF_ARM &&
                j <= BODY_SWORD_ARM - 1) {
                continue;
            }
            point = &SPAD->body_points[target_index][j];
            hit = hit_SphereMeetsSegment(
                scr, point, body_point->radius, body_point->reach_sq);
            if (hit) {
                core_hit = 0;
                if (body_point->has_core &&
                    g_object_kinds[obj->kind].harm == OBJECT_HARM_KILLS) {
                    core_hit = hit_SphereMeetsSegment(
                                   scr, point, body_point->core_radius,
                                   body_point->core_reach_sq) != 0;
                }
                break;
            }
        }
        if (!hit) {
            continue;
        }

        /* how far from facing the way the object flies: up to half a
         * turn */
        facing_away =
            (target->orientation.vy - ratan2(obj->vel.x, obj->vel.z)) &
            ANGLE_MASK;
        if (facing_away >= ANGLE_TURN / 2) {
            facing_away = ANGLE_TURN - facing_away;
        }
        /* the blow's x, y and z, written as one vector (see fight.h) */
        math_ReverseUnit((s32 *)&obj->vel, &g_fight_blow_dir_x);
        obj->pos.x -= obj->vel.x / 2;
        obj->pos.y -= obj->vel.y / 2;
        obj->pos.z -= obj->vel.z / 2;
        result = fighter_TakeHit(1, target, j, facing_away, core_hit,
                                 &g_object_kinds[obj->kind], 0, &clashed);
        if (result == 2) { /* the vanisher, gone */
            continue;
        }
        if (result != 0) {
            /* it went in */
            object_RaiseEvent(obj, obj->kind, OBJECT_SOUNDS_OF(obj->kind),
                              FIGHT_EVENT_OBJECT_HITS,
                              &SPAD->body_points[target_index][j].x, NULL);
            object_StickInBone(obj, body_point->bone);
            obj->can_hit_fighter = 0;
            move_kind = target->move_kind;
            /* struck while climbing: the loading screen's noise comes in
             * bursts from here on */
            if (move_kind == MOVE_KIND_CLIMB ||
                move_kind == MOVE_KIND_CLIMB_ED) {
                g_disp_loading_noise_burst = 1;
            }
            continue;
        }
        kind = obj->kind;
        if (kind == OBJECT_BURSTING) {
            object_RaiseEvent(
                obj, OBJECT_NONE, target_index ^ 1, FIGHT_EVENT_BULLET_IMPACT,
                &SPAD->body_points[target_index][j].x, &g_fight_blow_dir_x);
            obj->kind = OBJECT_NONE;
            continue;
        }
        if (kind == OBJECT_FROG) {
            object_RaiseEvent(obj, OBJECT_FROG, OBJECT_SOUNDS_OF(OBJECT_FROG),
                              FIGHT_EVENT_HOP, &obj->pos.x, NULL);
        } else if (!clashed) {
            object_RaiseEvent(
                obj, kind, OBJECT_SOUNDS_OF(kind), FIGHT_EVENT_OBJECT_HITS,
                &SPAD->body_points[target_index][j].x, NULL);
        }
        object_GlanceOff(scr, obj, j);
        obj->can_hit_fighter = 0;
    }
}

/* ---- thrown dirt ---- */

#define DIRT_SPEED 80
#define DIRT_LIFT (-200)
/* How near a handful has to pass to catch the other fighter: it spreads as
 * it flies. */
#define DIRT_REACH 500
#define DIRT_SPREAD 30

void object_ClearDirt(void) {
    s32 i;

    for (i = 0; i < DIRT_COUNT; i++) {
        g_object_dirt[i].kind = DIRT_NONE;
    }
}

/* The fighter throws up a handful of what it stands on, the way it faces,
 * and the effect and sound for it start. */
ThrownDirt *object_ThrowDirt(Fighter *fighter, s32 kind) {
    s32 speed = DIRT_SPEED;
    s32 lift = DIRT_LIFT; /* FAKE: the constant, kept in a local */
    s32 i = 0;
    ThrownDirt *next = g_object_dirt;
    ThrownDirt *dirt;
    s16 direction[3];

    for (; i < DIRT_COUNT; i++) {
        dirt = next;
        if (dirt->kind == DIRT_NONE) {
            break;
        }
        next = dirt + 1;
    }
    if (i == DIRT_COUNT) {
        return NULL;
    }

    dirt->kind = kind;
    dirt->was_thrown = 1;
    dirt->flight_frames = 0;
    dirt->owner = fighter->index;
    dirt->pos.x = fighter->world_pos.x;
    /* above its feet by its size: 128 for a fighter of full size */
    dirt->pos.y = fighter->foot_pos.vy - fighter->size_scale / 32;
    dirt->pos.z = fighter->world_pos.z;
    dirt->vel.x =
        (g_math_sin_table[fighter->orientation.vy & ANGLE_MASK] * speed) >> 12;
    dirt->vel.y = lift;
    dirt->vel.z = (g_math_sin_table[(fighter->orientation.vy + ANGLE_TURN / 4) &
                                    ANGLE_MASK] *
                   speed) >>
                  12;
    dirt->prev_pos = dirt->pos;
    dirt->floor_y = fighter->foot_pos.vy;
    direction[1] = fighter->orientation.vy;
    fight_RaiseEvent(fighter->surface_class,
                     kind == DIRT_EARTH ? FIGHT_EVENT_THROWN_EARTH
                                        : FIGHT_EVENT_THROWN_WATER,
                     &dirt->pos.x, direction);
    return dirt;
}

/* A frame of each handful's flight. It ends where it meets the stage or
 * falls back below the height it was thrown from. */
void object_UpdateDirt(void) {
    ObjectCastScratch *scr = &SPAD->tail.object_cast;
    ThrownDirt *dirt = g_object_dirt;
    s32 i;

    for (i = 0; i < DIRT_COUNT; i++, dirt++) {
        if (dirt->kind != DIRT_NONE) {
            dirt->flight_frames += 1;
            dirt->prev_pos = dirt->pos;
            dirt->vel.y += OBJECT_GRAVITY;
            scr->end_slot.cast_end.x = dirt->pos.x + dirt->vel.x;
            scr->end_slot.cast_end.y = dirt->pos.y + dirt->vel.y;
            scr->end_slot.cast_end.z = dirt->pos.z + dirt->vel.z;
            if (collision_CastMoveWith(
                    &dirt->pos.x, &scr->end_slot.cast_end.x, &scr->hit_point.x,
                    scr->cast_normal, &scr->cast_work) ||
                dirt->pos.y > dirt->floor_y) {
                dirt->kind = DIRT_NONE;
            } else {
                dirt->pos = scr->end_slot.cast_end;
            }
        }
    }
}

/* A handful that comes near enough to the other fighter catches it (the
 * fighter is told so, and its code takes it from there) and is used up. */
void object_DirtHitFighters(void) {
    ThrownDirt *dirt = g_object_dirt;
    s32 i;

    for (i = 0; i < DIRT_COUNT; i++, dirt++) {
        Fighter *target;
        u16 move_kind;
        s32 dx, dy, dz;
        u32 dist;

        if (dirt->kind == DIRT_NONE) {
            continue;
        }
        {
            s32 target_index = (dirt->owner == 0);
            target = &g_fighters[target_index];
        }
        /* nothing can be hit in these moves */
        move_kind = target->move_kind;
        if (move_kind == MOVE_KIND_END_GAME || move_kind == MOVE_KIND_DTH ||
            move_kind == MOVE_KIND_LOCK || move_kind == MOVE_KIND_START ||
            move_kind == MOVE_KIND_LOCK_EVEN ||
            move_kind == MOVE_KIND_LOCK_WINNING_1 ||
            move_kind == MOVE_KIND_LOCK_WINNING_2 ||
            move_kind == MOVE_KIND_LOCK_LOSING_1 ||
            move_kind == MOVE_KIND_LOCK_LOSING_2 ||
            move_kind == MOVE_KIND_PAIRED) {
            continue;
        }

        dx = target->world_pos.x - dirt->pos.x;
        dy = target->world_pos.y - dirt->pos.y;
        dz = target->world_pos.z - dirt->pos.z;
        dist = math_TableSqrt(dx * dx + dy * dy + dz * dz);
        if (dist < (dirt->flight_frames * DIRT_SPREAD + DIRT_REACH)) {
            target->pending_event = MOVE_EVENT_STAGGER;
            dirt->kind = DIRT_NONE;
        }
    }
}
