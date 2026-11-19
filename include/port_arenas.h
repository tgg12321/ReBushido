#ifndef PORT_ARENAS_H
#define PORT_ARENAS_H

/* The native port's custom arenas (docs/MODS.md, "Custom arenas"): a
 * mod's `arenas/NAME.arena`, made by tools/arena_compile.py, is a stage of
 * its own -- a ground file (meshes, collision grid, obstacles, cells,
 * textures) and the program's tables for it (where the fighters start,
 * the fog, the light, the music, the sounds, the CPU's obstacles) -- that
 * stands in for one of the game's stages, its host, when the run says so
 * (`--arena NAME`, or `--fight NAME ...`).
 *
 * While an arena is in play the host's ground is read from the arena's
 * file in place of the disc's sectors (port_discfiles.h), and where the
 * game reads one of its stage tables it asks here, with the table's own
 * value, and is given the arena's for the host's row; for any other
 * stage, and with no arena in play, it is given its own value back, so a
 * run without one computes exactly what it always did.
 *
 * The arenas are read once as the program starts and only read after:
 * none of this is the game's state (config/port_state.txt). What the game
 * makes of the tables (the fighters' places, the light, the ground it
 * loaded into its arena block) is in its own variables, as the disc's
 * stage's is. The PlayStation builds never see this header. */

#include "common.h"

/* The version of the file's format this program reads; a later program
 * reads every earlier one. */
#define PORT_ARENA_FORMAT 1
#define PORT_ARENA_MAGIC "RBARENA"
#define PORT_ARENA_NAME_SIZE 48
#define PORT_ARENA_KEY_SIZE 64
#define PORT_ARENAS_MAX 32
/* the CPU's obstacles (NavSet, cpu.h): so many chains of so many corners */
#define PORT_ARENA_CHAINS_MAX 8
#define PORT_ARENA_CHAIN_CORNERS_MAX 16
/* the host an arena stands in for when its file does not say: Abandoned
 * Station, which has no hook, no areas and nothing the code does of its
 * own, and a ground of 87 sectors */
#define PORT_ARENA_HOST_DEFAULT 25
/* the light preset of a stage (light.c): the fighters' sun and ambient,
 * the scenery's sun and ambient, the sun's yaw and pitch, the distance's
 * colour */
#define PORT_ARENA_LIGHT_WORDS 6
/* g_fight_start_positions' row: four pairs of two points of x, y, z */
#define PORT_ARENA_START_HALVES 24
/* a value the file did not give: the host's own is used */
#define PORT_ARENA_UNSET (-1)

/* One chain of the CPU's obstacles, as NavChain (cpu.h) has it. */
typedef struct {
    u8 flags; /* NAV_CHAIN_OPEN: not a ring */
    u8 kind;
    u8 margin;
    u8 corners;
    s16 at[PORT_ARENA_CHAIN_CORNERS_MAX][2];
} PortArenaChain;

/* An arena, as its file says. */
typedef struct {
    char key[PORT_ARENA_KEY_SIZE];    /* its file's name, small letters */
    char name[PORT_ARENA_NAME_SIZE];  /* `name =` */
    char lights[PORT_ARENA_KEY_SIZE]; /* `lights =`: the remastered light's
                                       * key (gl's lights/), or empty */
    s32 format;
    s32 host; /* enum StageIndex it stands in for */
    s32 has_starts;
    s16 starts[PORT_ARENA_START_HALVES];
    s32 has_light;
    s32 light[PORT_ARENA_LIGHT_WORDS];
    s32 fog;          /* 0 or 1; PORT_ARENA_UNSET: the host's */
    s32 fog_span;     /* g_stage_fog_spans' value; UNSET: the host's */
    s32 fog_farthest; /* g_stage_fog_settings' second; UNSET: the host's */
    s32 music;        /* a track of g_snd_stage_music_tracks; UNSET */
    s32 sounds;       /* the stage whose sound file, reverb and ambience
                       * are heard; UNSET: the host's */
    s32 chain_count;  /* PORT_ARENA_UNSET: the host's own obstacles */
    PortArenaChain chains[PORT_ARENA_CHAINS_MAX];
    u64 hash;      /* FNV-1a of the whole file, for netplay's set-up */
    u32 ground_at; /* where the ground is in the file, and how long */
    u32 ground_bytes;
} PortArena;

/* An arena's file, `length` bytes of `data`, into `arena` (whose `key`
 * the caller has set): TRUE if it can be used, else FALSE and why, with
 * the line, in `problem` (`room` bytes). A file wrong in one line is not
 * used at all. The ground itself is checked by the validator
 * (tools/arena_validate.py), not here: only that it is a ground file the
 * host has room for. */
extern s32 port_ArenaParse(
    const u8 *data, u32 length, PortArena *arena, char *problem, u32 room);

/* `--arena NAME`, or `--fight NAME` with a name no stage has: the arena to
 * play, by its key, resolved by port_ArenasStart. FALSE for a name too
 * long. */
extern s32 port_ArenaAsk(const char *key);
/* Reads every mod's arenas, and puts the one asked for in play: its
 * ground over its host's sectors. After port_StartStageMods. A name asked
 * for that no mod has ends the program with the usage code. */
extern void port_ArenasStart(void);

/* For a stage select (ROADMAP 16): how many arenas the mods have, the
 * one at `index` (NULL past the end), and the one in play (NULL: none). */
extern s32 port_ArenaCount(void);
extern const PortArena *port_ArenaAt(s32 index);
extern const PortArena *port_ArenaInPlay(void);
/* The remastered light's key for the stage in play (lighting's lights/):
 * the arena's `lights =`, or NULL for the stage's own. */
extern const char *port_ArenaLightsKey(void);
/* What netplay's set-up holds both players to: the arena in play, its key
 * and its file's bytes; 0 with none. */
extern u64 port_ArenaSessionHash(void);

/* ---- what the game asks, where it reads its stage tables ---- */

/* g_fight_start_positions (fight_GetStartPositions): the table, or with
 * an arena in play a copy of it whose host row is the arena's. */
extern void *port_ArenaStartPositions(void *table);
/* light_SetStage's preset for `stage`. */
extern s32 *port_ArenaLightPreset(s32 stage, s32 *preset);
/* stage_QueueFog: whether there is fog, its farthest copy, its span. */
extern s32 port_ArenaFogOn(s32 stage, s32 on);
extern s32 port_ArenaFogFarthest(s32 stage, s32 farthest);
extern s32 port_ArenaFogSpan(s32 stage, s32 span);
/* the versus fight's music (snd_StartFightMusic) */
extern s32 port_ArenaMusic(s32 stage, s32 track);
/* the stage whose sound file, reverb and ambience a fight on `stage` has */
extern s32 port_ArenaSoundStage(s32 stage);
/* the CPU's obstacles (cpu.c): `set` (a NavSet) or the arena's */
extern void *port_ArenaNavSet(s32 stage, void *set);

#endif /* PORT_ARENAS_H */
