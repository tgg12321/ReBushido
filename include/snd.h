#ifndef SND_H
#define SND_H

/* Sound effects (snd.c, and the fight's side of it in fight.c, fight_event.c
 * and fight_modes.c).
 *
 * Every sound is one note of a Sony VAB sound bank. Up to sixteen banks are
 * open at once, each with a fixed id (enum SndVabId); their samples sit in the
 * sound chip's memory and their headers in main RAM. The game asks for a sound
 * by number (enum SoundId) with snd_Play or snd_PlayAt, which only queue it;
 * snd_FlushQueue, once a frame, starts the queued sounds on free voices.
 * Music and speech are streamed from the disc (cdrom.h), not played here. */

#include "common.h"

#define SND_VAB_COUNT 16
#define SND_VOICE_COUNT 24
#define SND_QUEUE_SIZE 24
#define SND_VOLUME_MAX 0x7F

/* The bank ids. Each fighter has three banks: 1 + 3 * fighter, and the two
 * after it. */
enum SndVabId {
    /* Menu sounds (SYSTEM.SE); loaded at boot and never closed. */
    SND_VAB_COMMON = 0,
    /* A model's own bank, by model slot: the fighters' voices. The stage is
     * model slot 2 and would get SND_VAB_STAGE. */
    SND_VAB_MODEL = 1,
    SND_VAB_MODELS_STEP = 3,
    /* A 2D screen's bank, from its resource file; fighter 1's id, as the two
     * are never loaded together. */
    SND_VAB_MENU = 1,
    /* Chosen by character and weapon (so, a guess: the weapon's sounds). */
    SND_VAB_WEAPON_1 = 2,
    /* Chosen by the fighter's class, out of the stage's file (a guess at what
     * the class is: footwear, for footsteps). */
    SND_VAB_CLASS_1 = 3,
    SND_VAB_MODEL_2 = 4, /* in chanbara, the second chanbara bank */
    SND_VAB_WEAPON_2 = 5,
    SND_VAB_CLASS_2 = 6, /* shares fighter 1's when the classes are equal */
    SND_VAB_STAGE = 7,
    /* Chanbara mode's enemies (OTHER_C.SE and NINJA_C.SE; the second takes
     * SND_VAB_MODEL_2). */
    SND_VAB_CHANBARA = 8,
    SND_VAB_JUDGE = 9, /* team battle's judge (JADGE.SE) */
};

/* Sound numbers: indices into g_snd_sounds. Only the menu sounds and the
 * starts of the ranges are named here. */
enum SoundId {
    MENU_SOUND_CURSOR = 0,
    MENU_SOUND_CONFIRM = 1,
    MENU_SOUND_CANCEL = 2, /* also "not allowed" */
    MENU_SOUND_PAUSE = 3,  /* the pause menu opening */
    SND_MENU_SCREEN_FIRST = 5,
    /* A fighter's 40 sounds: 16 of its model bank, 16 of its weapon bank, 8
     * of its class bank. Fighter 2's follow fighter 1's. */
    SND_FIGHTER_FIRST = 33,
    SND_FIGHTER_COUNT = 40,
    SND_STAGE_FIRST = 113,
    SND_STAGE_AMBIENT_FIRST = 123, /* one per ambience slot */
    SND_CHANBARA_FIRST = 128,
    SND_JUDGE_FIRST = 156,
    SND_SOUND_COUNT = 170,
};

/* A sound bank ("vab" in the game's messages: "common_vab start", "vab id")
 * as stored in a file: three offsets from its own start, which
 * snd_LoadVab turns into addresses, and a size. key_on_table has one packed
 * word per sound of the bank (see snd_FlushQueue); vab_header and vab_body
 * are Sony's VAB header and samples; vab_body_size is how much of the sound
 * chip's memory the samples take. Once the samples are sent, only the part
 * before vab_body has to stay in RAM. */
typedef struct SoundVab {
    s32 key_on_table;
    s32 vab_header;
    s32 vab_body;
    s32 vab_body_size;
} SoundVab;

/* What a sound number means: which bank, and which entry of its key-on table.
 */
typedef struct {
    u16 vab;
    u16 key;
} SoundDef;

/* A queued sound. */
typedef struct {
    SoundDef *sound; /* NULL: free */
    u8 right;
    u8 left;
} SoundRequest;

/* How a stage's ambient sound comes back (StageAmbience kinds 2..6): silent
 * for at least `quiet_seconds`, then more and more likely each frame, certain
 * after `ramp_seconds` more. */
typedef struct {
    u8 quiet_seconds;
    u8 ramp_seconds;
} AmbientTiming;

/* The frames at which a stage prop's second and third sounds play, counted
 * from its first. */
typedef struct {
    u8 second;
    u8 third;
} PropCueTicks;

/* What an ambience slot of a stage does (the first byte of each pair in
 * g_snd_stage_ambience; the second is the volume). */
enum StageAmbience {
    AMBIENCE_NONE = 0,
    AMBIENCE_LOOP = 1, /* started once: wind, water */
    AMBIENCE_RANDOM_FIRST =
        2, /* 2..6: now and then, by g_snd_ambient_timings */
    AMBIENCE_RANDOM_PLACED = 5, /* 5, 6: heard from g_snd_ambient_positions */
    AMBIENCE_RANDOM_LAST = 6,
};

/* ---- snd.c ---- */

extern SoundDef g_snd_sounds[SND_SOUND_COUNT];
/* The three banks that come out of a stage's file: four bytes, in
 * snd_data.c. Declared without its size, as snd.c's code saw it: the code
 * reaches it as a variable too large for the small data. */
extern u8 g_snd_stage_vab_ids[];

extern void snd_Init(void);
extern void snd_Quit(void);
/* Starts the sound system and loads the common bank (boot, and after a
 * movie). */
extern void snd_Startup(void);
extern void snd_SetDefaultOutput(void);
/* Lets the disc's audio through the sound chip's mixer (cdrom.c). */
extern void snd_EnableCdAudio(void);

/* Queues sound `sound` at the given volumes (0..SND_VOLUME_MAX); nothing
 * happens if its bank is not loaded.
 * It returns nothing, though it is an int function (the callers' code shows
 * it was one). */
extern s32 snd_Play(s32 sound, s32 right, s32 left);
/* The same, with the volumes worked out from where `position` (x, y, z) is
 * from the camera. */
extern void snd_PlayAt(s32 sound, s32 *position);
/* Starts the queued sounds. Call once a frame. */
extern void snd_FlushQueue(void);
extern void snd_StopAllVoices(void);
/* Silences every voice and empties the queue. */
extern void snd_Reset(void);

/* Opens `vab` as bank `id`, sending its samples to the sound chip. `scratch`
 * is free memory to use if the banks already there have to be moved. Returns
 * the size of the part that must stay in RAM, or 0 on failure. */
extern s32 snd_LoadVab(SoundVab *vab, s32 id, s32 scratch);
/* Tells the sound system that bank `id`'s part in RAM has moved by
 * `rebase_delta` bytes. */
extern s32 snd_RebaseVab(s32 rebase_delta, s32 id);
extern void snd_CloseAllVabs(void); /* all but the common bank */

/* Each Load reads a file to `load_addr`, opens its banks and returns how many
 * bytes from `load_addr` must be kept; the caller copies them somewhere
 * lasting and calls the matching Rebase. */
extern s32 snd_LoadCommonVab(s32 load_addr);
extern s32 snd_LoadChanbaraVabs(s32 load_addr);
extern void snd_RebaseChanbaraVabs(s32 rebase_delta);
extern void snd_CloseChanbaraVabs(void);
extern s32 snd_LoadJudgeVab(s32 load_addr);
extern void snd_RebaseJudgeVab(s32 rebase_delta);
extern void snd_CloseJudgeVab(void);
/* The stage's bank and the two fighters' class banks. The file also carries
 * the stage's effect billboards, which are handed to the effects. */
extern s32 snd_LoadStageVabs(
    s32 load_addr, s32 stage, s32 first_class, s32 second_class);
extern void snd_RebaseStageVabs(s32 rebase_delta);
extern void snd_CloseStageVabs(void);
extern void snd_CloseModelVab(s32 model_slot);
extern void snd_CloseWeaponVabs(void);
extern void snd_CloseMenuVab(void);
/* In resource_weapon.c: copies the two fighters' weapon banks out of a pack
 * table to `output_addr` and opens them. Returns the end of what it wrote. */
extern s32 snd_LoadWeaponVabs(s32 pack_table_addr, s32 output_addr);

/* Sets the reverb of a stage. Returns the reverb type, or -1 if the stage has
 * none. */
extern s32 snd_SetStageReverb(s32 stage);
extern void snd_StopReverb(void);

/* ---- the fight's sounds (fight.c, fight_event.c, fight_modes.c) ---- */

/* Per stage, the music track of a versus fight. */
extern u8 g_snd_stage_music_tracks[32];
/* Per stage, four ambience slots of (enum StageAmbience, volume). */
extern u8 g_snd_stage_ambience[38][8];
extern AmbientTiming g_snd_ambient_timings[5];
extern s32 g_snd_ambient_positions[2][4];
/* Per ambience slot: 1 once a loop is started, or frames since a random
 * sound last played. */
#define SND_AMBIENCE_SLOTS 4
extern s16 g_snd_ambient_timers[SND_AMBIENCE_SLOTS];
/* Up to six stage props making their sounds: the frame each is at (0 = free)
 * and where it is. */
#define SND_PROP_SLOTS 6
extern u8 g_snd_prop_sound_ticks[SND_PROP_SLOTS];
extern Vec3 g_snd_prop_sound_positions[SND_PROP_SLOTS];
extern u8 g_snd_prop_first_sounds[SND_PROP_SLOTS];
extern PropCueTicks g_snd_prop_cue_ticks[SND_PROP_SLOTS];
extern u8 g_snd_initialized;

/* The "music off" setting of the saved options. */
extern u32 snd_IsMusicOff(void);
/* Ambient sounds play when there is no music over the fight. */
extern s32 snd_AmbienceEnabled(void);
extern void snd_EndFight(void);
extern void snd_ResetStageSounds(void);
/* A stage prop at `position` starts its run of three sounds. */
extern void snd_StartPropSounds(Vec3 *position);
extern void snd_UpdateStageSounds(void);

#endif /* SND_H */
