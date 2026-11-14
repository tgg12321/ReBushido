#ifndef OBJECT_H
#define OBJECT_H

/* Loose objects (object.c): the weapons that have left a fighter's hand.
 * A fighter starts with a few sub-weapons, throws them, and can pick them
 * up again where they lie; a defeated fighter's weapon falls beside it.
 * Also the handfuls of dirt or water a fighter throws up at the other. */

#include "common.h"
#include <psxsdk/libgte.h>
#include "weapon.h"
#include "fighter.h"

/* ---- kinds ---- */

/* What a loose object is (LooseObject.kind, Fighter.sub_weapons,
 * Fighter.sub_weapon_kind). Each character has its own kind of sub-weapon,
 * so most kinds have no name here; the ones that do behave in a way of their
 * own. g_object_weapon_models gives a kind's weapon model. */
enum ObjectKindId {
    OBJECT_NONE = -1,
    /* 0 to 0x10: the characters' sub-weapons. Five of them (1, 3, 4, 7 and
     * 12) are second swords: held in the off hand in the two-sword stance,
     * worn otherwise. */
    /* JO's: the only kind that strikes in a paired move, and JO has no
     * paired move without it. */
    OBJECT_JO_SUB_WEAPON = 10,
    /* CHIHIRO's frog. It lies still for a while, then hops off somewhere
     * by itself; it is drawn as another model (stretched out) while in the
     * air, and some characters take a hit from it in a way of their own. */
    OBJECT_FROG = 14,
    /* SUMINAGASHI's bomb: a small ball that is gone where it lands, in a
     * burst of smoke and sparks (a bullet's impact). */
    OBJECT_BURSTING = 15,
    /* TONY UMEDA's: a two-armed boomerang. It flies straight, then turns
     * aside and climbs, round a wide loop and back to where it was thrown
     * from, if nothing is in its way. */
    OBJECT_BOOMERANG = 16,
    /* The second sword of team battles and practice. */
    OBJECT_PRACTICE_SWORD = 17,
    /* 0x12 to 0x1D: a fighter's own weapon, dropped when it is defeated
     * (g_object_dropped_weapon_kinds). Nobody can pick these up, and they
     * are drawn as the model the fighter had, not the kind's. */
    OBJECT_WEAPON_FIRST = 18,
    OBJECT_WEAPON_END = 30,
    /* Never loose: the second gun two characters hold in the off hand. */
    OBJECT_OFF_HAND_GUN_1 = 30,
    OBJECT_OFF_HAND_GUN_2 = 31,
    OBJECT_KIND_COUNT = 32,
};

#define OBJECT_IS_WEAPON(kind)                                                 \
    ((kind) >= OBJECT_WEAPON_FIRST && (kind) < OBJECT_WEAPON_END)

/* How a kind flies. */
enum ObjectFlight {
    OBJECT_FLIES_STRAIGHT = 0, /* point first, without turning */
    OBJECT_SPINS_FLAT = 1,     /* spins about its upright axis */
    /* turns end over end; once it can no longer hurt it comes to lie flat */
    OBJECT_TUMBLES = 2,
    OBJECT_CURVES = 3, /* spins flat; the curving kind */
    OBJECT_HOPS = 4,   /* the hopping kind */
};

/* What a kind of loose object does: how it is thrown, how it flies and
 * bounces, and what a fighter it hits makes of it. */
typedef struct LooseObjectKind {
    s16 flight;       /* +0x00 enum ObjectFlight */
    s16 unk2;         /* +0x02 100 for every kind; not read */
    s16 launch_speed; /* +0x04 along the thrower's facing, a frame */
    s16 launch_lift;  /* +0x06 upward speed at the throw (negative) */
    u16 spin_rate;    /* +0x08 angle a frame, about the flight's axis */
    s16 bounce;       /* +0x0A 4.12: speed kept after touching ground */
    /* What it does to a fighter (fighter_TakeHit). clash_outcome: thrown at
     * a fighter whose attack has just begun, 0: the attack beats it aside,
     * 1: and the fighter is pushed back, other: it lands all the same.
     * harm: 0: it can reach a body point's core, and kill; 2: its wounds
     * count double. */
    u8 clash_outcome; /* +0x0C */
    u8 harm;          /* +0x0D */
} LooseObjectKind;

/* LooseObjectKind.clash_outcome */
enum {
    OBJECT_CLASH_ASIDE = 0, /* an attack just begun beats it aside */
    OBJECT_CLASH_PUSH = 1,  /* and its fighter is pushed back */
};

/* LooseObjectKind.harm */
enum {
    OBJECT_HARM_KILLS = 0,  /* it can reach a core */
    OBJECT_HARM_DOUBLE = 2, /* its wounds count double */
};

extern LooseObjectKind g_object_kinds[30];
/* A kind's weapon model (enum WeaponModel). */
extern u8 g_object_weapon_models[OBJECT_KIND_COUNT];

/* ---- the objects ---- */

#define OBJECT_COUNT 12
#define OBJECT_NO_SLOT 0xFF

/* LooseObject.rest_state */
enum ObjectRestState {
    OBJECT_REST_NOT_YET = 0, /* thrown or dropped, has not lain still yet */
    OBJECT_REST_AT_REST = 1,
    OBJECT_REST_HOPPING = 2, /* the hopping kind: in the air again */
    OBJECT_REST_HOPPED = 3,  /* and down again, not yet still */
};

/* LooseObject.drop_state */
enum ObjectDropState {
    OBJECT_DROP_NONE = 0,     /* thrown */
    OBJECT_DROP_RELEASED = 1, /* let go of: falling, turning as it goes */
    OBJECT_DROP_SETTLING = 2, /* has touched the ground: turns to lie flat */
};

/* A loose object: in flight, lying on the ground, or stuck in a fighter. */
typedef struct {
    s16 frames_moving;  /* +0x00 frames it has been in motion */
    s16 kind;           /* +0x02 enum ObjectKindId; OBJECT_NONE: free */
    u8 can_hit_fighter; /* +0x04 thrown and has touched nothing yet */
    u8 rest_state;      /* +0x05 */
    u8 owner;           /* +0x06 the fighter it came from */
    u8 drop_state;      /* +0x07 */
    /* Stuck in the other fighter: it rides on one of that fighter's bones,
     * placed by pos and stuck_matrix in the bone's frame. */
    u8 is_stuck;   /* +0x08 */
    u8 stuck_bone; /* +0x09 */
    /* Its own place among the objects, by which the replay tells objects
     * apart. An object that is removed keeps it for the rest of the frame,
     * so its place is not given away before the frame is over. */
    u8 slot;             /* +0x0A */
    u8 weapon_model;     /* +0x0B a dropped weapon's model */
    MATRIX stuck_matrix; /* +0x0C */
    Vec3 pos;            /* +0x2C */
    Vec3 prev_pos;       /* +0x38 a frame ago */
    Vec3 vel;            /* +0x44 */
    s32 is_moving;       /* +0x50 */
    s16 rot[3];          /* +0x54 angles about x, y and z */
    s16 pad_5A;          /* +0x5A not touched */
    s16 rot_vel[3];      /* +0x5C */
    s16 pad_62;          /* +0x62 not touched */
} LooseObject;           /* 0x64 bytes */

extern LooseObject g_object_pool[OBJECT_COUNT];

/* A round's start and end. */
extern void object_ClearAll(void);
extern void object_RemoveStuck(void);
extern void object_RemoveDroppedWeapons(void);

/* A frame: every object moves, bounces and is queued for drawing; then
 * (from the fight's hit tests) the ones in flight are tested against the
 * fighter they were thrown at. */
extern void object_Update(void);
extern void object_HitFighters(void);

/* ---- a fighter's sub-weapons ---- */

#define SUB_WEAPON_COUNT 12 /* that a fighter can carry */
#define SUB_WEAPONS_START 5 /* that a character can start with */

/* What each character starts a fight with. */
extern s8 g_object_start_sub_weapons[CHARACTER_COUNT][SUB_WEAPONS_START];
/* The kind a fighter's own weapon is once dropped: [its clan][weapon]. */
extern u8 g_object_dropped_weapon_kinds[2][WEAPON_ID_COUNT];
/* How an object that glances off a fighter is turned, by the body point it
 * struck. */
extern u16 g_object_glance_angles[BODY_POINT_COUNT];

extern void object_InitSubWeapons(Fighter *fighter);
/* Both return the kind thrown or picked up, or OBJECT_NONE. */
extern s32 object_ThrowSubWeapon(Fighter *fighter);
extern s32 object_PickUp(Fighter *fighter);
/* The first sub-weapon carried falls from `position`: a second sword from a
 * hand that can no longer hold it. */
extern void object_DropSubWeapon(Fighter *fighter, Vec3 *position);
/* A defeated fighter's weapon falls from `position`. */
extern void object_DropWeapon(Fighter *fighter, s32 kind, Vec3 *position);

/* ---- thrown dirt ----
 * "Suna", sand, is the game's name for the move (MOVE_KIND_DIRT). */

#define DIRT_COUNT 4

/* ThrownDirt.kind */
enum DirtKind {
    DIRT_NONE = 0, /* free */
    DIRT_EARTH = 1,
    DIRT_WATER = 2,
};

/* A handful of dirt or water on its way. It is not drawn (the effect that
 * starts with it is what is seen); it is only where the handful has got to,
 * for the other fighter to be caught by. */
typedef struct ThrownDirt {
    u8 kind;          /* +0x00 enum DirtKind */
    u8 was_thrown;    /* +0x01 set at the throw and never read */
    u8 flight_frames; /* +0x02 */
    u8 owner;         /* +0x03 the fighter that threw it */
    Vec3 pos;         /* +0x04 */
    Vec3 prev_pos;    /* +0x10 */
    Vec3 vel;         /* +0x1C */
    s32 floor_y;      /* +0x28 the thrower's height: it ends below that */
} ThrownDirt;         /* 0x2C bytes */

extern void object_ClearDirt(void);
/* Returns the handful, or NULL if four are in the air already. */
extern ThrownDirt *object_ThrowDirt(Fighter *fighter, s32 kind);
extern void object_UpdateDirt(void);
extern void object_DirtHitFighters(void);

/* Used only inside object.c. */

/* The kind of sub-weapon each fighter started with. An object makes the
 * sounds of the fighter whose kind it is: see OBJECT_SOUNDS_OF. */
extern u8 g_object_fighter_kinds[2];

/* The handfuls of thrown dirt. */
extern ThrownDirt g_object_dirt[DIRT_COUNT];

#endif /* OBJECT_H */
