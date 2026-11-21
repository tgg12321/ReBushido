#ifndef CDROM_H
#define CDROM_H

/* The CD: reading files, and playing music and voice off the disc (cdrom.c).
 *
 * The game does not use the disc's directory. Every file it needs is in a
 * table built into the executable (position and size), and is asked for by
 * group and number. One request runs at a time, started with a cdrom_Start*
 * call and carried forward by cdrom_Update once a frame. */

#include "common.h"
#include <psxsdk/libcd.h>

/* The folders of the disc, as cdrom_GetFileIndex's first argument. */
enum CdromGroup {
    CDROM_GROUP_NDATA = 0,  /* NDATA.DAT (the main archive) and NDATA.INF */
    CDROM_GROUP_MOTION = 1, /* WIN.DAT, COMMON.BBM, then a file per fighter */
    CDROM_GROUP_DATA = 2,   /* images, sound banks, stage files */
    CDROM_GROUP_AUDIO = 5,  /* the XA music and voice streams */
    CDROM_GROUP_MOVIE = 6,  /* CdromMovieFile */
};

/* The files of CDROM_GROUP_MOVIE. */
enum CdromMovieFile {
    CDROM_MOVIE_PLAYER = 0,  /* MOVOVL.EXE, the program that plays movies */
    CDROM_MOVIE_OPENING = 1, /* OPENING.STR */
    CDROM_MOVIE_TITLE = 2,   /* TITLE.TIM, the still shown after the movie */
};

/* The files of CDROM_GROUP_DATA, named for the disc's files (the folder is
 * TIM2D where none is given). */
enum CdromDataFile {
    /* the effects' textures: the one for the modes that have none of their
     * own, and further on chanbara's, practice's, the team battle's and the
     * story's */
    CDROM_DATA_GAME_BNK = 0,
    CDROM_DATA_SYSTEM_SE = 1, /* LOADSE: the common sound bank */
    CDROM_DATA_MOD_BIN = 2,   /* the main menu's pictures */
    /* character select's pictures: for two players, for one, for the story */
    CDROM_DATA_SEL_BIN = 3,
    CDROM_DATA_SEL1_BIN = 4,
    CDROM_DATA_SEL2_BIN = 5,
    CDROM_DATA_D_SEL_BIN = 6, /* the team battle's select */
    CDROM_DATA_LINK_TIM = 7,  /* the picture shown while the cable connects */
    CDROM_DATA_JADGE_SE = 8,  /* LOADSE: the judge's sound bank */
    /* + stage: LOADSE/STAGE00.BIN to STAGE09.BIN and LOADSE1/STAGE10.BIN to
     * STAGE37.BIN, a stage's sound banks and flames */
    CDROM_DATA_STAGE_BIN = 9,
    CDROM_DATA_GAMECHAN_BNK = 47,
    CDROM_DATA_GAMETRA_BNK = 48,
    CDROM_DATA_GAMEDAN_BNK = 49,
    CDROM_DATA_NAR_BIN = 50, /* the narration's pictures */
    /* + n: the nine pictures of the endings, LAS_SN.TIM, LAS_SS, LAS_KN,
     * LAS_KS, LAS_DN, LAS_DS, LAS_CN, LAS_CS, LAS_CT */
    CDROM_DATA_LAS_TIM = 51,
    CDROM_DATA_GAMESTO_BNK = 60,
    /* + stage: U_PIC/STG00.BIN to STG29.BIN and TIM2D/STG30.BIN, STG31.BIN,
     * the story's stage cards */
    CDROM_DATA_STG_BIN = 61,
    /* LOADSE: the two sound banks of chanbara's opponents */
    CDROM_DATA_OTHER_C_SE = 93,
    CDROM_DATA_NINJA_C_SE = 94,
    CDROM_DATA_STAFF_BIN = 95,   /* the staff roll's pictures */
    CDROM_DATA_LINK_M_TIM = 96,  /* LINK.TIM in grayscale */
    CDROM_DATA_SEA2_16_TIM = 97, /* the boot image */
};

/* The files of CDROM_GROUP_AUDIO: XA streams. */
enum CdromAudioFile {
    /* + n: XA_0/MAR_XA00.XA to MAR_XA28.XA (the story's script and the
     * endings' table give a file and a stream of it for a voice) */
    CDROM_AUDIO_MAR_XA = 0,
    CDROM_AUDIO_KAISYA_1 = 29, /* XA_1/KAISYA_1.XA */
    CDROM_AUDIO_KAISYA_2 = 30, /* XA_1/KAISYA_2.XA */
    CDROM_AUDIO_BB2_M1 = 31,   /* LOADSE/BB2_M1.XA: music tracks 0 to 7 */
    /* LOADSE/BB2_M2.XA: music tracks 8 to 11 and the staff roll's music */
    CDROM_AUDIO_BB2_M2 = 32,
    /* LOADSE/BB2_M3.XA: the voices of six endings (g_ending_voices) */
    CDROM_AUDIO_BB2_M3 = 33,
    /* + n: XA_1/END_XA1.XA to END_XA3.XA: the voices of the story's long
     * last scenes (story.c's scripts) */
    CDROM_AUDIO_END_XA = 34,
};

/* A file on the disc. */
typedef struct {
    CdlLOC loc; /* where it starts */
    u32 size;   /* in bytes */
} CdromFile;

/* A music track: a stretch of one stream of an XA file. */
typedef struct {
    u8 file;       /* enum CdromAudioFile */
    u8 xa_channel; /* which stream of the file */
    u16 pad;
    /* played from the file's start; MUSIC_WHOLE_FILE for all of it */
    s32 length_sectors;
} CdMusicTrack;

#define MUSIC_WHOLE_FILE (-1)

/* The most a channel of the disc's audio can be turned up (cdrom_FadeMix). */
#define CDROM_MIX_MAX 0xFF

extern void cdrom_Init(void);
/* Carries the current request forward. Call once a frame. */
extern void cdrom_Update(void);
/* Keeps the frame loop turning (link cable, sound, display) until the current
 * request is finished. */
extern void cdrom_WaitIdle(void);

/* The table index of file number `file` of a CdromGroup. */
extern s32 cdrom_GetFileIndex(s32 group, s32 file);
extern u32 cdrom_GetFileSize(s32 file_index);

/* Start reading a whole file, or `sector_count` sectors of it from
 * `sector_offset`, to `dest`. Return 0 if a request is already running. */
extern s32 cdrom_StartRead(s32 file_index, s32 dest);
extern s32 cdrom_StartReadAt(
    s32 file_index, s32 dest, s32 sector_offset, s32 sector_count);

/* Starts playing a file as audio: stream `xa_channel` of an XA file, or the
 * file as CD audio if `xa_channel` is negative. Returns 0 if a request is
 * already running. Call snd_EnableCdAudio first or nothing is heard. */
extern s32 cdrom_StartAudio(s32 file_index, s32 xa_channel);
/* The same for one of the twelve music tracks. */
extern s32 cdrom_StartMusic(s32 track);
/* Whether the audio starts again when it reaches its end. */
extern void cdrom_SetAudioLoop(s16 loop);
extern void cdrom_WaitAudioStarted(void);

/* Abandons the current request, whatever it is. */
extern void cdrom_StopAndWait(void);
#if RB_PORT
/* (The native port.) The music's step alone, while the game's frames are
 * stopped and its sound goes on (src/port/sound.c). */
extern void cdrom_KeepMusicGoing(void);
#endif

/* The volume of the disc's audio: how much of its left channel goes to the
 * left and right speakers, and of its right channel to the right and left
 * (0..CDROM_MIX_MAX). cdrom_FadeMix gets there over `frames` updates. */
extern void cdrom_FadeMix(s16 frames, s32 left_to_left, s32 left_to_right,
                          s32 right_to_right, s32 right_to_left);

#endif /* CDROM_H */
