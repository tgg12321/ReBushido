#ifndef RESOURCE_H
#define RESOURCE_H

/* Resources: getting what a fight needs out of the archive and into the
 * arena. Each kind has its own block: the fighters' models, the weapons, the
 * stage, the tiny model (resource.c, resource_weapon.c). */

#include "common.h"
#include "arena.h"
#include "model.h"
#include "weapon.h"

/* A loaded file's meshes are reached through a table of pointers; the
 * renderer keeps one table per kind of file, by these numbers (reloc_ in
 * render.h; SceneNode.mesh_block). */
enum RelocBlock {
    RELOC_MODEL = 0,         /* + model slot: the model's meshes */
    RELOC_MODEL_SHADOWS = 3, /* + model slot: its shadows' meshes */
    RELOC_WEAPONS = 6,
    RELOC_STAGE = 7,
    RELOC_STAGE_PROPS = 8, /* the props some stages have besides their cells */
    RELOC_TINY_MODEL = 9,
};

/* ---- models ---- */

/* Opens model slot `slot`'s two blocks and reads model `model_id` into the
 * second, unless it is there already. With a staging address the file is
 * read there instead and the caller copies what it keeps. */
extern Model *resource_LoadModel(s32 slot, s32 model_id, s32 staging_addr);
extern void resource_ResizeModelData(s32 slot, s32 new_size);
extern void resource_FreeModel(s32 slot);

/* ---- weapons ---- */

/* The weapon models: everything a fighter can hold or throw, and the things
 * that go with them. They come in two sets that are never loaded together;
 * a set's file holds all its models, and only the ones asked for are kept. */
#define WEAPON_MODEL_COUNT 58
#define WEAPON_SET_1_FIRST_MODEL WEAPON_MODEL_PRACTICE
/* A set's file also has the sounds of two weapon sound kinds, one per
 * fighter: there is a file for every pair of kinds. */
#define WEAPON_SET_0_SOUND_KINDS 16
#define WEAPON_SET_1_SOUND_KINDS 8

/* A list of the weapon models has, for each, its place among the loaded
 * ones or WEAPON_SLOT_NONE, and WEAPON_SLOTS_END after the last. */
#define WEAPON_SLOT_NONE (-1)
#define WEAPON_SLOTS_END (-2)

/* The list as of the last load, to tell whether anything changed. */
extern s16 g_resource_loaded_weapons[WEAPON_MODEL_COUNT + 1];
/* Up to two more models that are loaded along with a weapon model
 * (WEAPON_LINKED_NONE: none). */
#define WEAPON_LINKED_NONE 0xFF
extern u8 g_resource_weapon_linked[WEAPON_MODEL_COUNT][2];
/* The weapon sound kind of each character with each weapon; the second table
 * is used in team battles and in practice against the ninja. */
extern u8 g_resource_weapon_sound_kinds[27][WEAPON_ID_COUNT];
extern u8 g_resource_weapon_sound_kinds_alt[27][6];

/* Saying what the fight needs, then loading it: clear, request each weapon
 * model and each fighter's sound kind, load. Nothing is read if the same
 * things are already loaded. */
extern void resource_ClearWeaponRequests(void);
extern void resource_RequestWeapon(s32 weapon_model);
extern void resource_SetWeaponSounds(s32 fighter, s32 sound_kind);
extern void resource_LoadWeapons(s32 staging_addr);
/* Makes the next resource_LoadWeapons read the file whatever is loaded. */
extern void resource_ForgetWeapons(void);
#if RB_PORT
/* The native port's two on two (port_duo.h): the partners' weapon sound
 * kinds. The bank of the two fighters' that has kind `kind`
 * (SND_VAB_WEAPON_1 or _2), -1 if neither has; and the weapon file of the
 * set asked for that has the kinds `first` and `second` (either -1 for
 * none), read to `staging` in one go: the address of each one's pack of
 * sounds (as snd_LoadVab takes it), 0 for none. FALSE if there are none. */
extern s32 resource_PortWeaponSoundsBank(s32 kind);
extern s32 resource_PortReadWeaponSounds(
    s32 first, s32 second, s32 staging, s32 *first_pack, s32 *second_pack);
#endif

extern void resource_LoadWeaponFile(
    s32 weapon_set, s32 file, s16 *slots, s32 staging_addr);
extern void resource_FreeWeapons(void);

/* ---- the stage ---- */

extern s16 g_resource_stage;           /* the stage loaded, or -1 */
extern s16 g_resource_stage_has_areas; /* see resource_SelectStageArea */

extern void resource_LoadStage(s32 stage, s32 staging_addr);
extern s32 resource_GetStage(void);
/* Three stages' files hold several areas, of which one at a time is the
 * stage being fought in. Selecting a negative area settles on the first for
 * good and gives the others' memory back. */
extern void resource_SelectStageArea(s32 area);
extern s32 resource_HasStageAreas(void);
/* The stage file's list of what a blade can meet: its obstacles and the
 * props that can be cut down (fighter_hit.c reads it). */
extern s32 resource_GetStageObstacles(void);

/* ---- the tiny model ---- */

extern s32 *resource_LoadTinyModel(s32 tiny_model);
extern void resource_ResizeTinyModel(s32 new_size);
extern void resource_FreeTinyModel(void);

/* ---- the rest ---- */

/* Frees both fighters' models, the stage, the weapons and the tiny model. */
extern void resource_FreeAll(void);

#endif /* RESOURCE_H */
