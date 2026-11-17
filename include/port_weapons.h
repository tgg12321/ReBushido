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
 * `format = 1`. A later program reads every earlier format. */
#define PORT_WEAPON_FORMAT 1
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
} PortWeapon;

/* A weapon's file, `length` bytes of `text`, into `weapon` (whose `key`
 * the caller has set): TRUE if it is a weapon this program can use, else
 * FALSE and why, with the line, in `problem` (`room` bytes). Every name
 * and value is checked: a file with one wrong line is not used at all. */
extern s32 port_WeaponParse(const char *text, u32 length, PortWeapon *weapon,
                            char *problem, u32 room);
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

#endif /* PORT_WEAPONS_H */
