#ifndef CUTSCENE_H
#define CUTSCENE_H

/* Cutscenes: a recorded camera and a recorded motion for each fighter, played
 * back on a stage (the story's scenes, the endings, the finishing blow).
 * cutscene.c starts one, plays it a frame at a time and ends it. The file's
 * camera is a track (bitstream.c), its fighters motion streams
 * (motion_decode.c) and their faces four bits a frame. */

#include "common.h"

/* A cutscene file's header: where its parts are, from the start of the
 * file. */
typedef struct {
    s32 section_offset_count;
    s32 flags_offset;       /* a word: CUTSCENE_... */
    s32 frame_count_offset; /* a halfword */
    s32 camera_stream_offset;
    /* per fighter; a stream that begins "NULL" is a fighter not in the
     * scene */
    s32 motion_stream_offsets[2];
    s32 face_track_offsets[2]; /* there if the flags say so */
} CutsceneFile;

/* The track (g_bitstream_tracks) the camera is read with. */
#define CUTSCENE_CAMERA_TRACK 0

/* The flags word of a cutscene file. */
#define CUTSCENE_WEAPON_1 0x80000000 /* fighter 1 carries its weapon */
#define CUTSCENE_WEAPON_2 0x40000000
#define CUTSCENE_ALL_AREAS 0x8000 /* show every area of the stage */
/* which way the whole scene is turned, in steps of four degrees */
#define CUTSCENE_YAW(flags) (((flags) >> 8) & 0x7F)
#define CUTSCENE_FACES_1 0x80 /* fighter 1's face changes during it */
#define CUTSCENE_FACES_2 0x40
/* the stage to load, plus one; 0: keep this one */
#define CUTSCENE_STAGE 0x3F

/* Cutscene files come in sets: one for each fighter the story can be played
 * with (the story picks it by the player's fighter), and this one for the
 * scenes every fight shares: the endings and the kaishaku. */
#define CUTSCENE_SET_SHARED 22

/* The cutscene that is playing. */
typedef struct {
    s16 current_frame;    /* +0x00 0 before the first; -1 once over */
    s16 frame_count;      /* +0x02 */
    s32 flags;            /* +0x04 CUTSCENE_... */
    s16 start_pair_index; /* +0x08 */
    /* where on the stage it is played: one of the stage's start positions */
    s32 origin_x;          /* +0x0C */
    s32 origin_y;          /* +0x10 */
    s32 origin_z;          /* +0x14 */
    s32 pad18;             /* +0x18 a point's fourth word; not touched */
    s16 rotation_x;        /* +0x1C */
    s16 rotation_y;        /* +0x1E */
    s16 rotation_z;        /* +0x20 */
    s16 pad22;             /* +0x22 not touched */
    s16 view_angles[4];    /* +0x24 the camera's rotation, negated */
    s32 block_base;        /* +0x2C the file (a CutsceneFile) */
    s32 camera_stream;     /* +0x30 */
    s32 motion_streams[2]; /* +0x34 per fighter; 0: not in the scene */
    s32 face_tracks[2];    /* +0x3C per fighter; 0: none */
    /* the weapon models the fighters carry (-1: none): a main weapon each
     * and a second one each */
    s16 weapon_entry_ids[2];        /* +0x44 */
    s16 second_weapon_entry_ids[2]; /* +0x48 */
} Cutscene;

/* Cleared by cutscene_Init (story.c) just before it starts a cutscene;
 * nothing reads it. */
extern u8 g_cutscene_unused_start_flag;

/* Starts cutscene `number` of set `set` (g_cutscene_set_first_file) at the
 * stage's start position `start_pair`. The fighters carry the given weapon
 * models (-1: none). The file is read to `dest_addr`, or into the arena's
 * cutscene block if that is 0. */
extern void cutscene_StartFromSet(
    s32 set, s32 number, s32 start_pair, s32 weapon_1, s32 weapon_2,
    s32 second_weapon_1, s32 second_weapon_2, s32 dest_addr);
/* Plays one frame and draws it. Returns 0 once the cutscene is over. */
extern s32 cutscene_RunFrame(void);
extern void cutscene_End(void);
/* The camera's rotation this frame, negated (three angles). */
extern s16 *cutscene_GetViewAngles(void);
/* The cutscene's file has moved by `delta`. */
extern void cutscene_Rebase(s32 delta);

/* The screens (enum Screen) that play one: a scene of the story
 * (story.c), an ending and the kaishaku (fight_after.c). */
extern void cutscene_Init(void);
extern void cutscene_Update(void);
extern void cutscene_EndingInit(void);
extern void cutscene_EndingUpdate(void);
extern void cutscene_KaishakuInit(void);
extern void cutscene_KaishakuUpdate(void);

/* The cutscene's file in the arena (resource.c). */
extern s32 *cutscene_LoadBlock(s32 ndata_id);
extern void cutscene_FreeBlock(void);

#endif /* CUTSCENE_H */
