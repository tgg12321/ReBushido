#ifndef PORT_WEAPONS_H
#define PORT_WEAPONS_H

/* The native port's weapons as data (docs/MODS.md, "Weapons and
 * motions"): a mod's `weapons/NAME.txt` changes the numbers of one of the
 * game's eight weapons, or makes a new slot that fights with an existing
 * weapon's model, motions and CPU but numbers of its own. The game asks
 * here where it reads a weapon's table (fighter_PlacePoints,
 * fighter_Init), and is given its own table's value back when no mod's
 * weapon is in play, so that a run without one computes exactly what it
 * always did.
 *
 * The weapons are read once, as the program starts, and only read after:
 * none of this is the game's state (config/port_state.txt). Which weapon
 * a fighter has is worked out each time from what the game's state says
 * (his place and his weapon) and what the run was told (`--weapon`), so
 * a rollback has nothing of it to put back. The PlayStation builds never
 * see this header. */

#include "common.h"
#include "psxsdk/libgte.h"

/* The version of the file's format this program reads: a file says
 * `format = 1`, 2 for a file that names a model or motions of its own
 * (`model =`, `motions =`), or 3 for one that takes another weapon's
 * sounds or dropped weapon (`sounds =`, `dropped =`). A later program
 * reads every earlier format. */
#define PORT_WEAPON_FORMAT 3
#define PORT_WEAPON_FORMAT_OWN_FILES 2
#define PORT_WEAPON_FORMAT_SOUNDS 3
/* the most points a weapon has (the pole arms' three) */
#define PORT_WEAPON_POINTS_MAX 3
#define PORT_WEAPON_NAME_SIZE 48
#define PORT_WEAPON_KEY_SIZE 64
/* a speed's or a power's factor: this much is the game's own */
#define PORT_WEAPON_SCALE_ONE 4096
#define PORT_WEAPON_SCALE_MIN (PORT_WEAPON_SCALE_ONE / 16)
#define PORT_WEAPON_SCALE_MAX (PORT_WEAPON_SCALE_ONE * 4)
/* a value a file did not give: the base weapon's is used (the CPU's
 * range, which may be below 0, has one of its own) */
#define PORT_WEAPON_UNSET (-1)
#define PORT_WEAPON_CPU_UNSET 0x7FFF
/* the most weapons the mods may give, and a fighter's place (1 or 2) */
#define PORT_WEAPONS_MAX 32
#define PORT_WEAPON_SIDES 2

/* A weapon of a mod's, as its file says. */
typedef struct {
    char key[PORT_WEAPON_KEY_SIZE];   /* its file's name, small letters */
    char name[PORT_WEAPON_NAME_SIZE]; /* `name =`, for the log */
    s32 format;
    s32 base; /* enum WeaponId: whose model, motions and CPU it has */
    /* TRUE: it stands in for the base weapon wherever that is fought
     * with; FALSE: a new slot, which a fighter has only when the run says
     * so (`--weapon 1=KEY`) */
    s32 replaces;
    /* the points along it from the hand (fighter_PlacePoints): as many as
     * the base weapon has, or 0 for the base's own */
    s32 point_count;
    SVECTOR points[PORT_WEAPON_POINTS_MAX];
    /* the character's speed and power with the base weapon, times this
     * over PORT_WEAPON_SCALE_ONE */
    s32 speed;
    s32 power;
    /* the models (enum WeaponModel) for each clan, and in team battles
     * and practice; PORT_WEAPON_UNSET for the base weapon's */
    s32 models[2];
    s32 practice_model;
    /* the stance a fight begins in, 0 to 2, for every character;
     * PORT_WEAPON_UNSET for the game's table */
    s32 default_stance;
    /* what the CPU takes the weapon to reach (cpu_AttackReach, cpu.c):
     * beyond a move of the fighter's own file, and of the common file,
     * in the game's units; the unset values for the game's
     * (g_cpu_weapon_reaches, g_cpu_weapon_ranges) */
    s32 cpu_reach;
    s32 cpu_range;
    /* `model =`: the mod's `models/KEY.glb`, drawn in the hand in place
     * of the weapon model in the remastered picture; empty for none */
    char model_key[PORT_WEAPON_KEY_SIZE];
    /* `motions =`: the mod's `motions/KEY.bbm`, the fighter's motion file
     * in place of his set's; empty for the base's set. Its motion set's
     * weapon class is PORT_WEAPON_MOTION_CLASS + motion_number. */
    char motions_key[PORT_WEAPON_KEY_SIZE];
    s32 motion_number;
    /* `sounds =`: the weapon (enum WeaponId) whose sounds a character
     * makes with it, the cut, the clang, the lock and the landing of the
     * dropped weapon (g_resource_weapon_sound_kinds and _alt, the
     * character's row); `dropped =`: the weapon whose kind of loose
     * object (g_object_dropped_weapon_kinds, the clan's row) it becomes
     * when its fighter is cut down, how it flies and bounces.
     * PORT_WEAPON_UNSET for the base's. */
    s32 sounds;
    s32 dropped;
} PortWeapon;

/* A motion set (include/motion.h, MOTION_SET) whose weapon class is this
 * or more is a mod's weapon's own motion file: class 8 to 15, one for each
 * weapon with `motions =`. The game's own classes are 0 to 7. */
#define PORT_WEAPON_MOTION_CLASS 8
#define PORT_WEAPON_MOTION_FILES 8

/* A weapon's file, `length` bytes of `text`, into `weapon` (whose `key`
 * the caller has set): TRUE if it is a weapon this program can use, else
 * FALSE and why, with the line, in `problem` (`room` bytes). Every name
 * and value is checked: a file with one wrong line is not used at all. */
extern s32 port_WeaponParse(
    const char *text, u32 length, PortWeapon *weapon, char *problem, u32 room);
/* The weapon's number of a name (`katana`, `long_sword` or `long-sword`,
 * ...), or -1. */
extern s32 port_WeaponIdOf(const char *name, u32 length);
extern const char *port_WeaponIdName(s32 weapon_id);

/* ---- the run's weapons ---- */

/* `--weapon SIDE=KEY`: the fighter in place SIDE (1 or 2) fights with the
 * mod's weapon KEY whenever his weapon is that one's base. FALSE for a
 * SIDE or a KEY that cannot be. Before port_WeaponsStart. */
extern s32 port_WeaponsChoose(const char *side_and_key);
/* Reads every mod's weapons (port_mods.h, after port_ModsStart), checks
 * them and says in the run's log what each is, or why it is left out. */
extern void port_WeaponsStart(void);
/* How many of the mods' weapons are in use, and one of them. */
extern s32 port_WeaponsCount(void);
extern const PortWeapon *port_WeaponAt(s32 index);
/* Everything of the mods that changes how the game plays (the weapons,
 * the motions, the stages: port_ModsGameplayHash) and the run's
 * `--weapon`s, as one number: two netplay peers must have the same. 0
 * with none. */
extern u64 port_WeaponsSessionHash(void);

/* ---- what the game asks ---- */

/* The mod's weapon fighter `index` (0 or 1) has with the game's weapon
 * `weapon_id`, or NULL for the game's own. */
extern const PortWeapon *port_WeaponOf(s32 index, s32 weapon_id);
/* The points along the weapon (fighter_PlacePoints): the mod's, or `own`
 * (the game's table). A mod's weapon has as many as its base, so the
 * game's count stands. */
extern SVECTOR *port_WeaponPoints(s32 index, s32 weapon_id, SVECTOR *own);
/* Each the game's own value (`own`) unless a mod's weapon says otherwise:
 * fighter_Init's speed, power, model (the model the game chose says
 * which of the weapon's it is: a clan's, team battles' and practice's,
 * or the story's, which stays) and first stance; the CPU's reach and
 * range of the weapon, in the game's units. */
extern s32 port_WeaponSpeed(s32 index, s32 weapon_id, s32 own);
extern s32 port_WeaponPower(s32 index, s32 weapon_id, s32 own);
extern s32 port_WeaponModel(s32 index, s32 weapon_id, s32 own);
extern s32 port_WeaponDefaultStance(s32 index, s32 weapon_id, s32 own);
extern s32 port_WeaponCpuReach(s32 index, s32 weapon_id, s32 own);
extern s32 port_WeaponCpuRange(s32 index, s32 weapon_id, s32 own);
/* The weapon sound kind (resource_SetWeaponSounds) of fighter `index`,
 * whose character is `character`, with `weapon_id`: the game's (`own`),
 * or the character's with the weapon a mod's weapon's `sounds =` names;
 * `alt` for the table of team battles and practice. */
extern s32 port_WeaponSoundKind(
    s32 index, s32 weapon_id, s32 character, s32 alt, s32 own);
/* The kind of loose object (object_DropWeapon) fighter `index`'s weapon
 * becomes, of the clan `clan`: the game's, or the one of `dropped =`. */
extern s32 port_WeaponDropped(s32 index, s32 weapon_id, s32 clan, s32 own);

/* ---- a weapon's own files ---- */

/* The motion set fighter `index` fights with (motion_LoadFighterFiles):
 * `set`, the game's, or for a mod's weapon with motions of its own the
 * set of its own class with the same stance kinds. */
extern s32 port_WeaponMotionSet(s32 index, s32 weapon_id, s32 set);
/* The weapon class whose rules a set's fighter keeps (the two special
 * stances of the first two classes: motion_AttachFighter): the set's own,
 * or a mod's weapon's base. */
extern s32 port_WeaponMotionClass(s32 set);
/* For a set of a mod's weapon's own: its motion file read into `dest`
 * (the game's room for a fighter's motion file) and TRUE; FALSE for a
 * set of the disc's, which the game reads itself. */
extern s32 port_WeaponReadMotions(s32 set, void *dest);
/* The key of the model a fighter in model slot `slot` draws in his hand
 * for the weapon model `weapon_model`, where his mod's weapon has one
 * (`model =`), or NULL. Read by the picture (fighter_view.c). */
extern const char *port_WeaponHeldModel(s32 slot, s32 weapon_model);

/* ---- the weapon select (src/port/weapon_pick.c, a state file) ---- */

/* The mods' new weapons on one of the game's: how many (a weapon that
 * replaces is not one), and the one at `place` from 1 (NULL past the
 * last). */
extern s32 port_WeaponVariantCount(s32 weapon_id);
extern const PortWeapon *port_WeaponVariant(s32 weapon_id, s32 place);
/* A weapon's place among port_WeaponAt's. */
extern s32 port_WeaponPlaceOf(const PortWeapon *weapon);

/* What the weapon select holds of the mods' weapons: each side's place
 * among the weapon's variants under its cursor (0, the game's own; n,
 * the n-th of port_WeaponVariant), the weapon that was under it, and the
 * mod's weapon each fighter picked (its place in port_WeaponAt plus one, or
 * PORT_WEAPON_PICK_NONE). The game's state: saved, put back and hashed,
 * so a rollback, the two machines of a session and a replay hold it. */
#define PORT_WEAPON_PICK_NONE 0

typedef struct {
    s8 variant[PORT_WEAPON_SIDES];
    s8 cursor_weapon[PORT_WEAPON_SIDES];
    s8 picked[PORT_WEAPON_SIDES];
    u8 is_started;
    u32 row_frame; /* the frame the weapon row last ran (g_disp_frame_count) */
} PortWeaponPick;

extern PortWeaponPick g_port_weapon_pick;

/* The select screen loads (select_Load): nothing picked. */
extern void port_WeaponPickReset(void);
/* A frame of the weapon row for a side still choosing, with `weapon_id`
 * under its cursor: L1 (`back`) and R1 (`on`) go through the weapon's
 * variants. TRUE if it took a press. */
extern s32 port_WeaponPickStep(s32 side, s32 weapon_id, s32 back, s32 on);
/* The side picks `weapon_id` (Cross): the variant under its cursor is
 * its fighter's. */
extern void port_WeaponPickTake(s32 side, s32 weapon_id);
/* For the window: the name of what a side's cursor (or pick) shows, or
 * NULL for the game's own weapon; and how many variants it has. */
extern const char *port_WeaponPickShown(s32 side, s32 *count);

#endif /* PORT_WEAPONS_H */
