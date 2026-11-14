#ifndef WEAPON_H
#define WEAPON_H

/* The weapons: the numbers a fighter's weapon and the weapon models go by,
 * and queueing a weapon model for drawing. Loading the models is
 * resource_weapon.c (resource.h); what a weapon does in a fighter's hands
 * is the fighter's code; a weapon out of its hands is a loose object
 * (object.h). */

#include "common.h"

/* A fighter's weapon (Fighter.weapon_id): the choice on the select screen.
 * It is also the weapon class of the fighter's motion set (motion.h). The
 * long gun is TSUBAME's rifle, the short one KATZE's pistol. */
enum WeaponId {
    WEAPON_ID_KATANA = 0,
    WEAPON_ID_LONG_SWORD = 1,
    WEAPON_ID_NODACHI = 2,
    WEAPON_ID_BROADSWORD = 3,
    WEAPON_ID_NAGINATA = 4,
    WEAPON_ID_YARI = 5,
    WEAPON_ID_LONG_GUN = 6,
    WEAPON_ID_SHORT_GUN = 7,
    WEAPON_ID_COUNT = 8,
};

/* Everything from here up is a gun: it has ammunition and no blade. */
#define WEAPON_ID_FIRST_GUN WEAPON_ID_LONG_GUN
#define WEAPON_IS_GUN(weapon)                                                  \
    ((weapon) == WEAPON_ID_LONG_GUN || (weapon) == WEAPON_ID_SHORT_GUN)

/* The weapon models (Fighter.weapon_model, resource_RequestWeapon,
 * weapon_Queue): 58 of them, in two sets (resource.h). Each character's
 * weapon models are g_fighter_clan_weapon_models (fighter.h). */
enum WeaponModel {
    /* The weapons, each as one clan and as the other has it: two models a
     * weapon, the naginata and the yari sharing a pair
     * (g_fighter_clan_weapon_models). */
    WEAPON_MODEL_FIRST = 0,
    /* The sub-weapons: one for each kind of loose object a fighter can
     * start with (g_object_weapon_models). The hopping one has a second
     * model three further on. */
    WEAPON_MODEL_SUB_WEAPONS = 10,
    WEAPON_MODEL_FROG_IN_AIR = 3, /* added to the hopping one's */
    /* From 27 on: what is loaded along with another model
     * (g_resource_weapon_linked): the guns and the story's weapon are
     * among them. */
    WEAPON_MODEL_GUNS = 46,          /* one a clan */
    WEAPON_MODEL_OFF_HAND_GUNS = 48, /* two characters' second gun */
    /* The weapon the story's scenes put in a fighter's hands; it reaches as
     * far as a nodachi. */
    WEAPON_MODEL_STORY = 50,
    /* The second set: what team battles and practice against the ninja are
     * fought with (g_fighter_practice_weapon_models), and last the second
     * sword that goes with them. */
    WEAPON_MODEL_PRACTICE = 51,
    WEAPON_MODEL_PRACTICE_SECOND = 57,
};

/* ---- drawing a weapon (resource_weapon.c) ---- */

/* Where a weapon model is drawn (weapon_Queue's `where`): nowhere this
 * frame; at a place and rotation of its own (a loose object); or in a
 * fighter's hand, HELD with twice the fighter's model slot, plus 1 for the
 * second hand. */
#define WEAPON_DRAW_NOWHERE 0
#define WEAPON_DRAW_FREE 1
#define WEAPON_DRAW_HELD 0x8000
/* Queues a loaded weapon model for drawing. */
extern void weapon_Queue(s32 weapon_model, s32 where, s32 *pos, s16 *rot);
/* Queues one worn at a fighter's side. */
extern void weapon_QueueWorn(s32 weapon_model, s32 slot_and_side, s32 which);
/* Where a worn weapon sits on the waist, by side: a position and a
 * rotation (resource_data.c). */
#define WORN_POSE_SIZE 6
extern s16 g_weapon_worn_poses[2][WORN_POSE_SIZE];

#endif /* WEAPON_H */
