/* The CD: reading files, and playing music and voice off the disc.
 *
 * Every file the game uses is listed in g_cdrom_files with its position and
 * size, so nothing is looked up in the disc's directory. One request runs at
 * a time, either a read or an audio stream. A cdrom_Start* call records what
 * is wanted; cdrom_Update, called every frame, sends the drive one command at
 * a time and waits for each to be acknowledged, so the game never blocks on
 * the drive. If the drive reports an error or the lid is opened, the request
 * starts again from the top once the drive answers.
 *
 * Music and voice are XA streams (several interleaved in one file, picked by
 * channel) or plain CD audio; their volume is the CD mix, which can be faded.
 * The last part of the file runs the movie player, a separate program. */
#include "common.h"
#include "bb2.h"

/* Defined further down. */
static CdromFile g_cdrom_files[159];
static CdMusicTrack g_cdrom_music_tracks[12];
static s16 g_cdrom_group_first_file[8];
static u8 g_cdrom_init_done;

#define SECTOR_SIZE 0x800
#define SECTOR_WORDS (SECTOR_SIZE / 4)
/* With CdlModeSize1 a sector arrives with 12 bytes ahead of its data: its own
 * position, then the subheader. */
#define SECTOR_HEADER_WORDS 3

/* Updates to wait for the drive before giving up on a command. */
#define COMMAND_TIMEOUT 60
#define RECOVER_NOP_TIMEOUT 10
#define RECOVER_GETTN_TIMEOUT 30
#define GETLOC_TIMEOUT 30
/* Updates between a mode change and the seek that follows it. */
#define SETTLE_FRAMES 3
/* Updates of playing before the volume comes up, and before a stop is taken. */
#define AUDIO_UNMUTE_FRAMES 2
#define AUDIO_MIN_FRAMES 5

/* Audio ends this many sectors before the end of its file. */
#define AUDIO_END_MARGIN 150
/* Sectors an XA stream is taken to advance per position poll while the last
 * of it plays. */
#define XA_SECTORS_PER_POLL 4

/* The drive's mode for a read (double speed, whole sectors), for an XA
 * stream (double speed, the sound chip decoding it, one channel let through)
 * and for CD audio (the drive reporting where it is as it plays). */
#define MODE_READ (CdlModeSpeed | CdlModeSize1)
#define MODE_XA (CdlModeSpeed | CdlModeRT | CdlModeSF)
#define MODE_CD_AUDIO (CdlModeRept | CdlModeDA)
/* The XA file number the streams are recorded under. */
#define XA_FILE_NUMBER 1

/* The drive's answers (g_cdrom_result), by byte. CdlGetlocP's ends with the
 * absolute position. A report of CD audio has a position from its fourth
 * byte: absolute, unless its seconds have the top bit, when it is the place
 * in the track. */
#define GETLOCP_ABSOLUTE 5
#define REPORT_POSITION 3
#define REPORT_SECOND 4
#define REPORT_RELATIVE 0x80

/* Vertical blanks to leave the drive alone after it is reset or has come
 * back. */
#define DRIVE_REST_VBLANKS 4

/* A program file's first sector: the EXEC block follows the 16-byte
 * "PS-X EXE" signature. */
#define EXE_HEADER_EXEC 16

/* The movie player's stack: the first part of the shared buffer. */
#define MOVIE_STACK_SIZE 0x800

/* What the movie player is passed (MovieArgs in movovl/src/main.c). */
enum {
    MOVIE_ARG_FILE = 0,        /* the movie's table entry */
    MOVIE_ARG_END_FRAME = 1,   /* the movie stops here */
    MOVIE_ARG_RING = 2,        /* memory for the sectors read ahead */
    MOVIE_ARG_WORK = 3,        /* memory for the decoder */
    MOVIE_ARG_STILL = 4,       /* the title picture's table entry */
    MOVIE_ARG_STILL_FRAME = 5, /* the picture is shown from here on */
    MOVIE_ARG_COUNT = 6,
};

/* cdrom_Update's state. A state named _SYNC waits for the command the state
 * before it sent. */
enum CdromIoState {
    CDROM_STATE_IDLE = 0,

    CDROM_STATE_READ_SETMODE = 2,
    CDROM_STATE_READ_SETMODE_SYNC = 3,
    CDROM_STATE_READ_SETLOC = 4,
    CDROM_STATE_READ_SETLOC_SYNC = 5,
    CDROM_STATE_READ_SECTORS = 6, /* cdrom_ReadyCallback takes each sector */
    CDROM_STATE_READ_DONE = 8,
    /* Recovery: wait for the lid to be closed (CdlNop returns the drive's
     * status), ask for the disc's tracks to see that it is being read again,
     * then start over. */
    CDROM_STATE_READ_ERROR = 9,
    CDROM_STATE_READ_RECOVER_NOP = 10,
    CDROM_STATE_READ_RECOVER_SYNC = 11,
    CDROM_STATE_READ_RECOVER_GETTN = 12,
    CDROM_STATE_READ_RECOVER_GETTN_SYNC = 13,

    CDROM_STATE_AUDIO_SETMODE = 16,
    CDROM_STATE_AUDIO_SETMODE_SYNC = 17,
    CDROM_STATE_AUDIO_SETLOC_SYNC = 18,
    CDROM_STATE_AUDIO_SEEK_SYNC = 19, /* a cued request waits here */
    CDROM_STATE_AUDIO_PLAY_SYNC = 20,
    /* Playing: the two states alternate, asking where the drive is and
     * comparing the answer with the end. */
    CDROM_STATE_AUDIO_PLAYING = 21,
    CDROM_STATE_AUDIO_PLAYING_SYNC = 22,
    CDROM_STATE_AUDIO_ERROR = 23,
    CDROM_STATE_AUDIO_RECOVER_NOP = 24,
    CDROM_STATE_AUDIO_RECOVER_SYNC = 25,
    CDROM_STATE_AUDIO_RECOVER_GETTN = 26,
    CDROM_STATE_AUDIO_RECOVER_GETTN_SYNC = 27,
    CDROM_STATE_AUDIO_PAUSE = 28,
    CDROM_STATE_AUDIO_PAUSE_SYNC = 29,
    CDROM_STATE_AUDIO_STOP_DELAY = 30,
};

/* What cdrom_ReadyCallback leaves in sectors_left when a read went wrong. */
#define READ_FAILED -1
#define READ_WRONG_SECTOR -2

/* The current request. */
typedef struct {
    CdlFILTER filter;    /* the XA stream being played */
    s32 recover_timeout; /* updates spent waiting in a recovery state */
    s16 file_index;
    s16 state;      /* CdromIoState */
    s16 audio_cued; /* hold the audio at its start until released */
    s16 unk0E; /* cdrom_Update clears it when a read starts; nothing reads it */
    s16 stop_request; /* end the request at the next chance */
    s16 audio_loop;   /* start the audio again when it ends */
    CdromFile file;   /* the file's table entry; loc is where to seek */
    s32 audio_end_sector;
    s32 read_sector_count;
    s32 read_dest;
    s32 sectors_left; /* of the read under way; negative if it failed */
    s32 read_cursor;  /* where the next sector goes */
    s32 audio_frames; /* updates since the audio started, or stopped */
    s32 settle_frames;
    u8 audio_mode; /* the drive's mode: MODE_XA or MODE_CD_AUDIO */
    s32 is_xa;
    s16 read_timeout;
    s16 getloc_timeout;
    s16 report_timeout; /* updates since CD audio last reported its place */
    u16 wait_vblanks;   /* retraces spent in cdrom_WaitIdle; never read */
    s32 next_sector;    /* the sector the read expects to arrive next */
    s32 end_countdown;  /* sectors of XA audio left, once near the end */
} CdromState;

CdromState g_cdrom;

/* The CD volume as last set, and the volume a fade is heading for. */
CdlATV g_cdrom_mix;
CdlATV g_cdrom_fade_target;
/* A fade is under way while g_cdrom_fade_len is not zero. */
s16 g_cdrom_fade_step;
s16 g_cdrom_fade_len;
/* The drive's answer to the last command: its status byte, then whatever the
 * command returns. */
u8 g_cdrom_result[8];

/* Sets the disc's volume now, and ends any fade. */
static void cdrom_SetMix(s32 left_to_left, s32 left_to_right,
                         s32 right_to_right, s32 right_to_left) {
    g_cdrom_mix.val0 = left_to_left;
    g_cdrom_mix.val1 = left_to_right;
    g_cdrom_mix.val2 = right_to_right;
    g_cdrom_mix.val3 = right_to_left;
    CdMix(&g_cdrom_mix);
    g_cdrom_fade_len = 0;
}

/* Starts a fade to the given volume over `frames` updates of the audio. */
void cdrom_FadeMix(s16 frames, s32 left_to_left, s32 left_to_right,
                   s32 right_to_right, s32 right_to_left) {
    g_cdrom_fade_target.val0 = left_to_left;
    g_cdrom_fade_target.val1 = left_to_right;
    g_cdrom_fade_target.val2 = right_to_right;
    g_cdrom_fade_len = frames;
    g_cdrom_fade_step = 0;
    g_cdrom_fade_target.val3 = right_to_left;
}

void snd_EnableCdAudio(void) {
    SsSetSerialAttr(SS_SERIAL_A, SS_MIX, SS_SON);
    SsSetSerialVol(SS_SERIAL_A, SND_VOLUME_MAX, SND_VOLUME_MAX);
}

void cdrom_Init(void) {
    CdInit();
    CdSetDebug(0);
    cdrom_SetMix(0, 0, 0, 0);
    g_cdrom.state = CDROM_STATE_IDLE;
    if (!g_cdrom_init_done) {
        g_cdrom_init_done = 1;
    }
}

/* Starts the drive's library again and gives the drive a rest. Nothing calls
 * it. */
static void cdrom_Reset(void) {
    CdFlush();
    CdInit();
    VSync(DRIVE_REST_VBLANKS);
}

/* Called by the drive, during a read, each time a sector is ready: checks
 * that it is the sector expected and copies it to its place. After the last
 * sector, or on anything unexpected, stops the drive and leaves the outcome
 * in sectors_left for cdrom_Update. The drive's answer, `result`, is
 * not used. */
static void cdrom_ReadyCallback(u8 intr, u8 *result) {
    s32 header[4];

    if (intr == CdlDataReady) {
        g_cdrom.read_timeout = 0;
        if (g_cdrom.sectors_left <= 0) {
            return;
        }
        CdGetSector(header, SECTOR_HEADER_WORDS);
        if (CdPosToInt((CdlLOC *)header) != g_cdrom.next_sector) {
            g_cdrom.sectors_left = READ_WRONG_SECTOR;
        } else {
            CdGetSector((void *)ADDR(g_cdrom.read_cursor), SECTOR_WORDS);
            g_cdrom.read_cursor += SECTOR_SIZE;
            g_cdrom.sectors_left--;
            g_cdrom.next_sector++;
            if (g_cdrom.sectors_left != 0) {
                return;
            }
        }
    } else {
        g_cdrom.sectors_left = READ_FAILED;
    }
    CdReadyCallback(NULL);
    CdControlF(CdlPause, NULL);
}

/* A fading volume: `from` at step 0, `to` at step g_cdrom_fade_len. */
#define FADE(from, to)                                                         \
    (((to) * g_cdrom_fade_step +                                               \
      (from) * (g_cdrom_fade_len - g_cdrom_fade_step)) /                       \
     g_cdrom_fade_len)

/* Where the audio goes when it reaches its end. */
#define AUDIO_END_STATE                                                        \
    (g_cdrom.audio_loop ? CDROM_STATE_AUDIO_SETMODE : CDROM_STATE_AUDIO_PAUSE)

/* cdrom_Update for an audio request; also steps the volume fade. */
static void cdrom_UpdateAudio(void) {
    CdlATV atv;

    if (g_cdrom_fade_len > 0) {
        atv.val0 = FADE(g_cdrom_mix.val0, g_cdrom_fade_target.val0);
        atv.val1 = FADE(g_cdrom_mix.val1, g_cdrom_fade_target.val1);
        atv.val2 = FADE(g_cdrom_mix.val2, g_cdrom_fade_target.val2);
        atv.val3 = FADE(g_cdrom_mix.val3, g_cdrom_fade_target.val3);
        CdMix(&atv);
        if (++g_cdrom_fade_step >= g_cdrom_fade_len) {
            g_cdrom_fade_len = 0;
            g_cdrom_mix = g_cdrom_fade_target;
        }
    }

    switch (g_cdrom.state) {
    case CDROM_STATE_AUDIO_SETMODE:
        cdrom_SetMix(0, 0, 0, 0);
        CdControlF(CdlSetmode, &g_cdrom.audio_mode);
        g_cdrom.audio_frames = 0;
        g_cdrom.settle_frames = 0;
        g_cdrom.end_countdown = 0;
        g_cdrom.state = CDROM_STATE_AUDIO_SETMODE_SYNC;
        break;
    case CDROM_STATE_AUDIO_SETMODE_SYNC: {
        s32 sync = CdSync(1, g_cdrom_result);
        if (sync == CdlComplete) {
            CdControlF(CdlSetloc, (u8 *)&g_cdrom.file.loc);
            g_cdrom.state = CDROM_STATE_AUDIO_SETLOC_SYNC;
        } else if (sync == CdlDiskError) {
            g_cdrom.state = CDROM_STATE_AUDIO_ERROR;
        }
        break;
    }
    case CDROM_STATE_AUDIO_SETLOC_SYNC:
        if (++g_cdrom.settle_frames >= SETTLE_FRAMES) {
            s32 sync = CdSync(1, g_cdrom_result);
            if (sync == CdlComplete) {
                CdControlF(CdlSeekP, NULL);
                g_cdrom.state = CDROM_STATE_AUDIO_SEEK_SYNC;
            } else if (sync == CdlDiskError) {
                g_cdrom.state = CDROM_STATE_AUDIO_ERROR;
            }
        }
        break;
    case CDROM_STATE_AUDIO_SEEK_SYNC: {
        s32 sync = CdSync(1, g_cdrom_result);
        if (sync == CdlComplete) {
            if (!g_cdrom.audio_cued) {
                CdControlF(g_cdrom.is_xa ? CdlReadS : CdlPlay, NULL);
                g_cdrom.state = CDROM_STATE_AUDIO_PLAY_SYNC;
            }
        } else if (sync == CdlDiskError) {
            g_cdrom.state = CDROM_STATE_AUDIO_ERROR;
        }
        break;
    }
    case CDROM_STATE_AUDIO_PLAY_SYNC: {
        s32 sync = CdSync(1, g_cdrom_result);
        if (sync == CdlComplete) {
            g_cdrom.report_timeout = 0;
            g_cdrom.state = CDROM_STATE_AUDIO_PLAYING;
        } else if (sync == CdlDiskError) {
            g_cdrom.state = CDROM_STATE_AUDIO_ERROR;
        }
        break;
    }
    case CDROM_STATE_AUDIO_PLAYING:
        if (++g_cdrom.audio_frames == AUDIO_UNMUTE_FRAMES) {
            cdrom_SetMix(CDROM_MIX_MAX, 0, CDROM_MIX_MAX, 0);
        }
        if (g_cdrom.stop_request && g_cdrom.audio_frames >= AUDIO_MIN_FRAMES) {
            g_cdrom.state = CDROM_STATE_AUDIO_PAUSE;
            break;
        }
        g_cdrom.state = CDROM_STATE_AUDIO_PLAYING_SYNC;
        if (g_cdrom.is_xa) {
            /* XA: ask for the position; the answer is read next update. */
            CdControlF(CdlGetlocP, g_cdrom_result);
            g_cdrom.getloc_timeout = 0;
            break;
        }
        /* CD audio: the drive reports its position by itself (CdlModeRept).
         * Only a report of the absolute position is looked at. */
        CdControlF(CdlNop, NULL);
        {
            s32 ready = CdReady(1, g_cdrom_result);
            if (ready == CdlDataReady) {
                if (!(g_cdrom_result[REPORT_SECOND] & REPORT_RELATIVE)) {
                    g_cdrom.report_timeout = 0;
                    if (CdPosToInt(
                            (CdlLOC *)&g_cdrom_result[REPORT_POSITION]) >=
                        g_cdrom.audio_end_sector) {
                        cdrom_SetMix(0, 0, 0, 0);
                        g_cdrom.state = AUDIO_END_STATE;
                    }
                }
            } else if (ready == CdlDiskError) {
                g_cdrom.state = CDROM_STATE_AUDIO_ERROR;
            }
        }
        if (g_cdrom.report_timeout++ > COMMAND_TIMEOUT) {
            g_cdrom.state = CDROM_STATE_AUDIO_ERROR;
        }
        break;
    case CDROM_STATE_AUDIO_PLAYING_SYNC:
        /* Near the end of an XA stream the end is timed from the last known
         * position, not waited for. */
        if (g_cdrom.end_countdown != 0) {
            g_cdrom.end_countdown -= XA_SECTORS_PER_POLL;
            if (g_cdrom.end_countdown <= 0) {
                cdrom_SetMix(0, 0, 0, 0);
                g_cdrom.state = AUDIO_END_STATE;
                break;
            }
        }
        {
            s32 sync = CdSync(1, g_cdrom_result);
            if (sync == CdlComplete) {
                g_cdrom.state = CDROM_STATE_AUDIO_PLAYING;
                if (g_cdrom.is_xa) {
                    s32 pos =
                        CdPosToInt((CdlLOC *)&g_cdrom_result[GETLOCP_ABSOLUTE]);
                    if (pos >= g_cdrom.audio_end_sector) {
                        cdrom_SetMix(0, 0, 0, 0);
                        g_cdrom.state = AUDIO_END_STATE;
                    } else if (
                        pos >= g_cdrom.audio_end_sector - AUDIO_END_MARGIN) {
                        if (g_cdrom.end_countdown == 0) {
                            g_cdrom.end_countdown =
                                g_cdrom.audio_end_sector - pos;
                        }
                    }
                }
            } else if (sync == CdlDiskError) {
                g_cdrom.state = CDROM_STATE_AUDIO_ERROR;
            } else if (g_cdrom.is_xa) {
                if (++g_cdrom.getloc_timeout > GETLOC_TIMEOUT) {
                    CdFlush();
                    g_cdrom.state = CDROM_STATE_AUDIO_PLAYING;
                }
            }
        }
        break;
    case CDROM_STATE_AUDIO_PAUSE:
        cdrom_SetMix(0, 0, 0, 0);
        CdControlF(CdlPause, NULL);
        g_cdrom.state = CDROM_STATE_AUDIO_PAUSE_SYNC;
        break;
    case CDROM_STATE_AUDIO_PAUSE_SYNC: {
        s32 sync = CdSync(1, g_cdrom_result);
        if (sync == CdlComplete) {
            g_cdrom.audio_frames = 0;
            g_cdrom.state = CDROM_STATE_AUDIO_STOP_DELAY;
        } else if (sync == CdlDiskError) {
            g_cdrom.state = CDROM_STATE_AUDIO_ERROR;
        }
        break;
    }
    case CDROM_STATE_AUDIO_STOP_DELAY:
        if (++g_cdrom.audio_frames >= AUDIO_MIN_FRAMES) {
            g_cdrom.state = CDROM_STATE_IDLE;
        }
        break;
    case CDROM_STATE_AUDIO_ERROR:
        cdrom_SetMix(0, 0, 0, 0);
        if (g_cdrom_result[0] & CdlStatShellOpen) {
            g_cdrom.state = CDROM_STATE_AUDIO_RECOVER_NOP;
        } else {
            g_cdrom.state = CDROM_STATE_AUDIO_RECOVER_GETTN;
        }
        break;
    case CDROM_STATE_AUDIO_RECOVER_NOP:
        CdControlF(CdlNop, NULL);
        g_cdrom.state = CDROM_STATE_AUDIO_RECOVER_SYNC;
        break;
    case CDROM_STATE_AUDIO_RECOVER_SYNC: {
        s32 sync = CdSync(1, g_cdrom_result);
        if (sync == CdlComplete) {
            if (g_cdrom_result[0] & CdlStatShellOpen) {
                g_cdrom.state = CDROM_STATE_AUDIO_RECOVER_NOP;
            } else {
                g_cdrom.state = CDROM_STATE_AUDIO_RECOVER_GETTN;
            }
        } else if (sync == CdlDiskError) {
            g_cdrom.state = CDROM_STATE_AUDIO_RECOVER_NOP;
        }
        break;
    }
    case CDROM_STATE_AUDIO_RECOVER_GETTN:
        CdControlF(CdlGetTN, NULL);
        g_cdrom.state = CDROM_STATE_AUDIO_RECOVER_GETTN_SYNC;
        break;
    case CDROM_STATE_AUDIO_RECOVER_GETTN_SYNC: {
        s32 sync = CdSync(1, g_cdrom_result);
        if (sync == CdlComplete) {
            g_cdrom.state = g_cdrom.stop_request ? CDROM_STATE_IDLE
                                                 : CDROM_STATE_AUDIO_SETMODE;
        } else if (sync == CdlDiskError) {
            g_cdrom.state = CDROM_STATE_AUDIO_ERROR;
        }
        break;
    }
    }
}

/* Carries the current request one step forward: sends the drive its next
 * command, or looks whether the last one is done. The read's states are here,
 * the audio's in cdrom_UpdateAudio. */
void cdrom_Update(void) {
    u8 drive_mode[4];

    if (g_cdrom.state >= CDROM_STATE_AUDIO_SETMODE) {
        cdrom_UpdateAudio();
        return;
    }
    switch (g_cdrom.state) {
    case CDROM_STATE_IDLE:
        break;
    case CDROM_STATE_READ_SETMODE:
        if (g_cdrom.stop_request) {
            g_cdrom.state = CDROM_STATE_IDLE;
            break;
        }
        drive_mode[0] = MODE_READ;
        CdControlF(CdlSetmode, drive_mode);
        g_cdrom.unk0E = 0;
        g_cdrom.read_timeout = 0;
        g_cdrom.state = CDROM_STATE_READ_SETMODE_SYNC;
        break;
    case CDROM_STATE_READ_SETMODE_SYNC: {
        s32 sync = CdSync(1, g_cdrom_result);
        if (sync == CdlComplete) {
            g_cdrom.state = CDROM_STATE_READ_SETLOC;
            g_cdrom.settle_frames = 0;
        } else if (sync == CdlDiskError) {
            g_cdrom.state = CDROM_STATE_READ_ERROR;
        } else if (++g_cdrom.read_timeout > COMMAND_TIMEOUT) {
            g_cdrom.state = CDROM_STATE_READ_RECOVER_NOP;
        }
        break;
    }
    case CDROM_STATE_READ_SETLOC:
        if (++g_cdrom.settle_frames >= SETTLE_FRAMES) {
            g_cdrom.read_cursor = g_cdrom.read_dest;
            g_cdrom.sectors_left = g_cdrom.read_sector_count;
            g_cdrom.next_sector = CdPosToInt(&g_cdrom.file.loc);
            CdControl(CdlSetloc, (u8 *)&g_cdrom.file.loc, NULL);
            g_cdrom.read_timeout = 0;
            g_cdrom.state = CDROM_STATE_READ_SETLOC_SYNC;
        }
        break;
    case CDROM_STATE_READ_SETLOC_SYNC: {
        s32 sync = CdSync(1, g_cdrom_result);
        if (sync == CdlComplete) {
            g_cdrom.read_timeout = 0;
            CdReadyCallback(cdrom_ReadyCallback);
            CdControlF(CdlReadN, (u8 *)&g_cdrom.file.loc);
            g_cdrom.state = CDROM_STATE_READ_SECTORS;
        } else if (sync == CdlDiskError) {
            g_cdrom.state = CDROM_STATE_READ_ERROR;
        } else if (++g_cdrom.read_timeout > COMMAND_TIMEOUT) {
            g_cdrom.state = CDROM_STATE_READ_RECOVER_NOP;
        }
        break;
    }
    case CDROM_STATE_READ_SECTORS: {
        /* The callback pauses the drive when it has finished or failed, and
         * clears the timeout at every sector. */
        s32 sync = CdSync(1, g_cdrom_result);
        if (sync == CdlComplete) {
            if (g_cdrom.sectors_left == 0) {
                g_cdrom.state = CDROM_STATE_READ_DONE;
            } else if (g_cdrom.sectors_left < 0) {
                g_cdrom.state = CDROM_STATE_READ_ERROR;
            } else if (++g_cdrom.read_timeout > COMMAND_TIMEOUT) {
                CdReadyCallback(NULL);
                g_cdrom.state = CDROM_STATE_READ_RECOVER_NOP;
            }
        } else if (sync == CdlDiskError) {
            CdReadyCallback(NULL);
            g_cdrom.state = CDROM_STATE_READ_ERROR;
        } else if (++g_cdrom.read_timeout > COMMAND_TIMEOUT) {
            CdReadyCallback(NULL);
            g_cdrom.state = CDROM_STATE_READ_RECOVER_NOP;
        }
        break;
    }
    case CDROM_STATE_READ_DONE:
        g_cdrom.state = CDROM_STATE_IDLE;
        break;
    case CDROM_STATE_READ_ERROR:
        if (g_cdrom_result[0] & CdlStatShellOpen) {
            g_cdrom.state = CDROM_STATE_READ_RECOVER_NOP;
        } else {
            g_cdrom.state = CDROM_STATE_READ_RECOVER_GETTN;
        }
        break;
    case CDROM_STATE_READ_RECOVER_NOP:
        CdControlF(CdlNop, NULL);
        g_cdrom.state = CDROM_STATE_READ_RECOVER_SYNC;
        g_cdrom.recover_timeout = 0;
        break;
    case CDROM_STATE_READ_RECOVER_SYNC: {
        s32 sync = CdSync(1, g_cdrom_result);
        if (sync == CdlComplete) {
            if (g_cdrom_result[0] & CdlStatShellOpen) {
                g_cdrom.state = CDROM_STATE_READ_RECOVER_NOP;
            } else {
                g_cdrom.state = CDROM_STATE_READ_RECOVER_GETTN;
            }
        } else if (sync == CdlDiskError) {
            g_cdrom.state = CDROM_STATE_READ_RECOVER_NOP;
        } else if (++g_cdrom.recover_timeout > RECOVER_NOP_TIMEOUT) {
            CdFlush();
            g_cdrom.state = CDROM_STATE_READ_RECOVER_NOP;
        }
        break;
    }
    case CDROM_STATE_READ_RECOVER_GETTN:
        CdControlF(CdlGetTN, NULL);
        g_cdrom.state = CDROM_STATE_READ_RECOVER_GETTN_SYNC;
        g_cdrom.recover_timeout = 0;
        break;
    case CDROM_STATE_READ_RECOVER_GETTN_SYNC: {
        s32 sync = CdSync(1, g_cdrom_result);
        if (sync == CdlComplete) {
            /* Start the read again (or end it, if it was stopped), once the
             * drive has had sixteen blanks to itself. */
            g_cdrom.state = CDROM_STATE_READ_SETMODE;
            VSync(DRIVE_REST_VBLANKS);
            VSync(DRIVE_REST_VBLANKS);
            VSync(DRIVE_REST_VBLANKS);
            VSync(DRIVE_REST_VBLANKS);
        } else if (sync == CdlDiskError) {
            g_cdrom.state = CDROM_STATE_READ_ERROR;
        } else if (++g_cdrom.recover_timeout > RECOVER_GETTN_TIMEOUT) {
            CdFlush();
            g_cdrom.state = CDROM_STATE_READ_RECOVER_NOP;
        }
        break;
    }
    }
}

static s32 cdrom_IsIdle(void) { return g_cdrom.state == CDROM_STATE_IDLE; }

/* Asks for the whole of file `file_index` of the table to be read to `dest`.
 * Returns 0, having done nothing, if a request is running. */
s32 cdrom_StartRead(s32 file_index, s32 dest) {
    if (g_cdrom.state != CDROM_STATE_IDLE) {
        return 0;
    }

    g_cdrom.file_index = file_index;
    g_cdrom.file = g_cdrom_files[(s16)file_index];
    g_cdrom.read_dest = dest;
    g_cdrom.stop_request = 0;
    g_cdrom.state = CDROM_STATE_READ_SETMODE;
    g_cdrom.wait_vblanks = 0;
    g_cdrom.read_sector_count =
        (g_cdrom.file.size + SECTOR_SIZE - 1) / SECTOR_SIZE;
    return 1;
}

s32 cdrom_StartReadAt(
    s32 file_index, s32 dest, s32 sector_offset, s32 sector_count) {
    if (!cdrom_StartRead(file_index, dest)) {
        return 0;
    }
    CdIntToPos(
        CdPosToInt(&g_cdrom.file.loc) + sector_offset, &g_cdrom.file.loc);
    g_cdrom.read_sector_count = sector_count;
    return 1;
}

s32 cdrom_GetFileIndex(s32 group, s32 file) {
    return g_cdrom_group_first_file[group] + file;
}

/* Silences and pauses the drive at once, then goes through the recovery
 * states, which end the request because stop_request is set. */
static void cdrom_Stop(void) {
    CdReadyCallback(NULL);
    cdrom_SetMix(0, 0, 0, 0);
    CdFlush();
    CdControlF(CdlPause, NULL);
    g_cdrom.stop_request = 1;
    g_cdrom.state = CDROM_STATE_READ_RECOVER_SYNC;
    g_cdrom.recover_timeout = 0;
}

u32 cdrom_GetFileSize(s32 file_index) { return g_cdrom_files[file_index].size; }

/* Waits for the current request to finish, doing meanwhile what a frame
 * must: the link cable is answered, queued sounds start and the loading
 * screen is drawn, every second blank. */
void cdrom_WaitIdle(void) {
#if RB_PORT
    /* the window may hurry these blanks, the link cable's too: nothing
     * here waits on the player (docs/PORT.md, "Quick loading") */
    g_port_load_wait++;
#endif
    link_BeginWait();
    while (!cdrom_IsIdle()) {
        link_ExchangeIdle();
        cdrom_Update();
        if (g_snd_initialized) {
            snd_FlushQueue();
        }
        disp_DrawLoadingScreen();
        g_cdrom.wait_vblanks += 2;
        VSync(2);
    }
    link_EndWait();
#if RB_PORT
    g_port_load_wait--;
#endif
}

/* Asks for file `file_index` to be played: stream `xa_channel` of it, or the
 * file as CD audio if `xa_channel` is negative. It ends AUDIO_END_MARGIN
 * sectors before the file does. Returns 0 if a request is running. */
s32 cdrom_StartAudio(s32 file_index, s32 xa_channel) {
    if (g_cdrom.state != CDROM_STATE_IDLE) {
        return 0;
    }

    g_cdrom.file_index = file_index;
    g_cdrom.file = g_cdrom_files[g_cdrom.file_index];
    g_cdrom.audio_end_sector =
        CdPosToInt(&g_cdrom_files[g_cdrom.file_index].loc) +
        g_cdrom_files[g_cdrom.file_index].size / SECTOR_SIZE - AUDIO_END_MARGIN;

    if (xa_channel < 0) {
        g_cdrom.is_xa = 0;
        g_cdrom.audio_mode = MODE_CD_AUDIO;
    } else {
        g_cdrom.is_xa = 1;
        g_cdrom.filter.file = XA_FILE_NUMBER;
        g_cdrom.filter.chan = xa_channel;
        CdControlB(CdlSetfilter, (u8 *)&g_cdrom.filter, NULL);
        g_cdrom.audio_mode = MODE_XA;
    }

    g_cdrom.audio_cued = 0;
    g_cdrom.stop_request = 0;
    g_cdrom.audio_loop = 0;
    g_cdrom.state = CDROM_STATE_AUDIO_SETMODE;

    return 1;
}

/* Asks for music track `track` to be played: its stream, as far as the
 * track's length. Returns 0 if a request is running. */
s32 cdrom_StartMusic(s32 track) {
    CdMusicTrack *music = &g_cdrom_music_tracks[track];
    if (cdrom_StartAudio(cdrom_GetFileIndex(CDROM_GROUP_AUDIO, music->file),
                         music->xa_channel)) {
        if (music->length_sectors != MUSIC_WHOLE_FILE) {
            g_cdrom.audio_end_sector =
                CdPosToInt(&g_cdrom_files[g_cdrom.file_index].loc) +
                music->length_sectors;
        }
        return 1;
    }
    return 0;
}

/* Starts a piece of music and holds it at its start. Nothing calls it, nor
 * any of the three after it that cue or let go. */
static s32 cdrom_CueMusic(s32 track) {
    s32 started = cdrom_StartMusic(track);
    if (started) {
        g_cdrom.audio_cued = 1;
        return 1;
    }
    return 0;
}

void cdrom_SetAudioLoop(s16 loop) { g_cdrom.audio_loop = loop; }

/* The same for a channel of an audio file. Nothing calls it. */
static s32 cdrom_CueAudio(s32 file_index, s32 xa_channel) {
    if (cdrom_StartAudio(file_index, xa_channel)) {
        g_cdrom.audio_cued = 1;
        return 1;
    }
    return 0;
}

/* Ends the audio at the next update. Nothing calls it. */
static void cdrom_StopAudio(void) {
    g_cdrom.audio_cued = 0;
    g_cdrom.stop_request = 1;
}

/* The Cue forms seek to the start and wait there for cdrom_PlayCued.
 * Nothing calls it. */
static void cdrom_PlayCued(void) { g_cdrom.audio_cued = 0; }

void cdrom_WaitAudioStarted(void) {
    while (g_cdrom.state != CDROM_STATE_AUDIO_PLAYING_SYNC) {
        link_ExchangeIdle();
        cdrom_Update();
        VSync(2);
    }
}

void cdrom_StopAndWait(void) {
    if (g_cdrom.state != CDROM_STATE_IDLE) {
        cdrom_Stop();
    }
    cdrom_WaitIdle();
}

/* Reads `nbytes` from where the drive was last told to go, with Sony's
 * blocking reader, and does not return until it is done. Returns 0 if the
 * read worked. For use when the game's own systems are shut down. */
static s32 cdrom_ReadBlocking(s32 nbytes, s32 buf, s32 mode) {
    s32 left;

    CdRead((nbytes + SECTOR_SIZE - 1) / SECTOR_SIZE, (u32 *)ADDR(buf), mode);
    do {
        left = CdReadSync(1, NULL);
        if (left > 0) {
            VSync(0);
        }
    } while (left > 0);
    return left;
}

/* Loads the movie player: reads the first sector of MOVOVL.EXE, copies its
 * load information to *exec, then reads the program to its load address.
 * Tries again from the start until both reads work. */
static void cdrom_LoadExec(struct EXEC *exec) {
    u8 header_sector[SECTOR_SIZE];
    CdlLOC body_loc;
    s32 file_index;
    s32 failed;
    s32 pos;
    s32 mode;

    mode = CdlModeSpeed;
    file_index = cdrom_GetFileIndex(CDROM_GROUP_MOVIE, CDROM_MOVIE_PLAYER);

    for (;;) {
        CdControl(CdlSetloc, (u8 *)&g_cdrom_files[file_index].loc, NULL);
        failed = cdrom_ReadBlocking(SECTOR_SIZE, (s32)header_sector, mode);
        if (failed != 0) {
            continue;
        }

        *exec = *(struct EXEC *)&header_sector[EXE_HEADER_EXEC];

        pos = CdPosToInt(&g_cdrom_files[file_index].loc);
        CdIntToPos(pos + 1, &body_loc);
        CdControl(CdlSetloc, (u8 *)&body_loc, NULL);
        failed = cdrom_ReadBlocking(exec->t_size, exec->t_addr, mode);
        if (failed == 0) {
            break;
        }
    }
}

/* Runs the movie player as a child program: shuts down everything that uses
 * interrupts, loads it, hands it the machine, and brings the game's systems
 * back up when it returns. */
static void sys_Exec(s32 argc, s32 *argv, s32 stack_addr) {
    struct EXEC exec;
    VSync(0);
    SetDispMask(0);
    disp_Reset();
    snd_Quit();
    memcard_Quit();
    ResetCallback();
    CdInit();
    cdrom_LoadExec(&exec);
    DrawSync(0);
    ResetGraph(0);
    StopPAD();
    StopCallback();
    exec.s_addr = stack_addr;
    exec.s_size = 0;
    EnterCriticalSection();
    Exec(&exec, argc, (char **)argv);
    sys_Init();
    snd_Startup();
    VSync(0);
    SetDispMask(1);
}

/* Plays movie `movie_file` of CDROM_GROUP_MOVIE up to `end_frame` with the
 * movie player, the title picture on show from `still_image_frame` on. The
 * player is given the two files' table entries and the two pieces of memory,
 * and the shared buffer for its stack. */
void sys_ExecMovieOverlay(s32 arena_base, s32 work_buffer, s32 movie_file,
                          s32 end_frame, s32 still_image_frame) {
    /* six are passed; the frame has room for eight */
    s32 argv[8];
    s32 file_index;
    s32 stack;

    file_index = cdrom_GetFileIndex(CDROM_GROUP_MOVIE, movie_file);
    argv[MOVIE_ARG_FILE] = (s32)&g_cdrom_files[file_index];
    argv[MOVIE_ARG_END_FRAME] = end_frame;
    argv[MOVIE_ARG_RING] = arena_base;
    argv[MOVIE_ARG_WORK] = work_buffer;
    file_index = cdrom_GetFileIndex(CDROM_GROUP_MOVIE, CDROM_MOVIE_TITLE);
    argv[MOVIE_ARG_STILL] = (s32)&g_cdrom_files[file_index];
    argv[MOVIE_ARG_STILL_FRAME] = still_image_frame;
    stack = (s32)memcard_GetSharedBuffer();
    /* the stack's first word is its last */
    sys_Exec(MOVIE_ARG_COUNT, argv, stack + MOVIE_STACK_SIZE - 4);
}

/* Set the first time cdrom_Init runs; nothing else looks at it. */
static u8 g_cdrom_init_done = 0;

/* Every file on the disc that the game uses, a group after another (enum
 * CdromGroup) and in each group in the order of the game's file numbers,
 * which is not the order on the disc: where it starts and its size in bytes.
 * The game never reads the disc's directory; this table is that directory's
 * positions and sizes written into the program (all of the disc's files but
 * the program itself, SYSTEM.CNF and TIM2D/LAS_DD.TIM, which nothing reads),
 * and tools/build_disc.py rewrites an entry when it moves a file.
 *
 * A position is the minute, second and frame (a 75th of a second: a sector)
 * the drive is sent to, each two decimal digits in a byte, and is written
 * here as it is read: AT(07, 27, 19) is 7 minutes 27 seconds and 19 frames
 * in. The disc's sector 0 is at 00:02:00, so the file there starts at sector
 * (7 * 60 + 27 - 2) * 75 + 19 = 33394 of the disc. */
#define AT(minute, second, frame) {0x##minute, 0x##second, 0x##frame, 0}

/* clang-format off */
static CdromFile g_cdrom_files[159] = {
    /*  min sec frame   bytes            number in its group, and the file */

    /* CDROM_GROUP_NDATA: the archive and its directory */
    {AT(01, 08, 68), 58114048}, /*  0 NDATA/NDATA.DAT */
    {AT(07, 27, 19),     3604}, /*  1 NDATA/NDATA.INF */

    /* CDROM_GROUP_MOTION (MOTION_FILE_...): the winners' poses, the motions
     * every fighter has, then a motion set a file (g_motion_set_files) */
    {AT(01, 04, 39),   387072}, /*  0 MOTION/WIN.DAT */
    {AT(00, 38, 32),   165860}, /*  1 MOTION/COMMON.BBM */
    {AT(00, 53, 46),   252816}, /*  2 MOTION/U123.BBM */
    {AT(00, 55, 20),   294060}, /*  3 MOTION/U124.BBM */
    {AT(00, 57, 14),   278076}, /*  4 MOTION/U125.BBM */
    {AT(00, 59, 00),   293716}, /*  5 MOTION/U134.BBM */
    {AT(01, 00, 69),   278972}, /*  6 MOTION/U135.BBM */
    {AT(01, 02, 56),   271600}, /*  7 MOTION/U235.BBM */
    {AT(00, 44, 41),   253672}, /*  8 MOTION/S123.BBM */
    {AT(00, 46, 15),   287740}, /*  9 MOTION/S124.BBM */
    {AT(00, 48, 06),   269308}, /* 10 MOTION/S125.BBM */
    {AT(00, 49, 63),   292724}, /* 11 MOTION/S234.BBM */
    {AT(00, 51, 56),   286120}, /* 12 MOTION/T123.BBM */
    {AT(00, 40, 63),   284880}, /* 13 MOTION/K123.BBM */
    {AT(00, 42, 53),   276596}, /* 14 MOTION/N123.BBM */
    {AT(01, 07, 03),   282848}, /* 15 MOTION/Y123.BBM */
    {AT(00, 39, 38),    96120}, /* 16 MOTION/GN.BBM */
    {AT(00, 40, 10),   108468}, /* 17 MOTION/GS.BBM */
    {AT(00, 44, 39),     3480}, /* 18 MOTION/ON.BBM */

    /* CDROM_GROUP_DATA (enum CdromDataFile) */
    {AT(07, 30, 31),   129920}, /*  0 TIM2D/GAME.BNK */
    {AT(27, 04, 59),    27260}, /*  1 LOADSE/SYSTEM.SE */
    {AT(07, 40, 73),   656904}, /*  2 TIM2D/MOD.BIN */
    {AT(07, 49, 24),   774388}, /*  3 TIM2D/SEL.BIN */
    {AT(07, 54, 28),   774364}, /*  4 TIM2D/SEL1.BIN */
    {AT(07, 59, 32),   774364}, /*  5 TIM2D/SEL2.BIN */
    {AT(07, 27, 22),   477936}, /*  6 TIM2D/D_SEL.BIN */
    {AT(07, 36, 71),   307220}, /*  7 TIM2D/LINK.TIM */
    {AT(26, 48, 63),   163204}, /*  8 LOADSE/JADGE.SE */
    {AT(26, 51, 30),   128988}, /*  9 LOADSE/STAGE00.BIN */
    {AT(26, 52, 18),   192780}, /* 10 LOADSE/STAGE01.BIN */
    {AT(26, 53, 38),   177996}, /* 11 LOADSE/STAGE02.BIN */
    {AT(26, 54, 50),   239244}, /* 12 LOADSE/STAGE03.BIN */
    {AT(26, 56, 17),   223628}, /* 13 LOADSE/STAGE04.BIN */
    {AT(26, 57, 52),   230700}, /* 14 LOADSE/STAGE05.BIN */
    {AT(26, 59, 15),   165852}, /* 15 LOADSE/STAGE06.BIN */
    {AT(27, 00, 21),   242012}, /* 16 LOADSE/STAGE07.BIN */
    {AT(27, 01, 65),   231620}, /* 17 LOADSE/STAGE08.BIN */
    {AT(27, 03, 29),   213836}, /* 18 LOADSE/STAGE09.BIN */
    {AT(00, 06, 24),   166268}, /* 19 LOADSE1/STAGE10.BIN */
    {AT(00, 07, 31),   244284}, /* 20 LOADSE1/STAGE11.BIN */
    {AT(00, 09, 01),   174732}, /* 21 LOADSE1/STAGE12.BIN */
    {AT(00, 10, 12),   164892}, /* 22 LOADSE1/STAGE13.BIN */
    {AT(00, 11, 18),   259276}, /* 23 LOADSE1/STAGE14.BIN */
    {AT(00, 12, 70),   163388}, /* 24 LOADSE1/STAGE15.BIN */
    {AT(00, 14, 00),   218876}, /* 25 LOADSE1/STAGE16.BIN */
    {AT(00, 15, 32),   136204}, /* 26 LOADSE1/STAGE17.BIN */
    {AT(00, 16, 24),   155116}, /* 27 LOADSE1/STAGE18.BIN */
    {AT(00, 17, 25),   231100}, /* 28 LOADSE1/STAGE19.BIN */
    {AT(00, 18, 63),   161468}, /* 29 LOADSE1/STAGE20.BIN */
    {AT(00, 19, 67),   146796}, /* 30 LOADSE1/STAGE21.BIN */
    {AT(00, 20, 64),   213196}, /* 31 LOADSE1/STAGE22.BIN */
    {AT(00, 22, 19),   169012}, /* 32 LOADSE1/STAGE23.BIN */
    {AT(00, 23, 27),   151452}, /* 33 LOADSE1/STAGE24.BIN */
    {AT(00, 24, 26),   218444}, /* 34 LOADSE1/STAGE25.BIN */
    {AT(00, 25, 58),   202332}, /* 35 LOADSE1/STAGE26.BIN */
    {AT(00, 27, 07),   221868}, /* 36 LOADSE1/STAGE27.BIN */
    {AT(00, 28, 41),   132332}, /* 37 LOADSE1/STAGE28.BIN */
    {AT(00, 29, 31),   127484}, /* 38 LOADSE1/STAGE29.BIN */
    {AT(00, 30, 19),   164908}, /* 39 LOADSE1/STAGE30.BIN */
    {AT(00, 31, 25),   146788}, /* 40 LOADSE1/STAGE31.BIN */
    {AT(00, 32, 22),   136268}, /* 41 LOADSE1/STAGE32.BIN */
    {AT(00, 33, 14),   136316}, /* 42 LOADSE1/STAGE33.BIN */
    {AT(00, 34, 06),   164332}, /* 43 LOADSE1/STAGE34.BIN */
    {AT(00, 35, 12),   136268}, /* 44 LOADSE1/STAGE35.BIN */
    {AT(00, 36, 04),   136268}, /* 45 LOADSE1/STAGE36.BIN */
    {AT(00, 36, 71),   224732}, /* 46 LOADSE1/STAGE37.BIN */
    {AT(07, 31, 20),   129920}, /* 47 TIM2D/GAMECHAN.BNK */
    {AT(07, 33, 62),   129920}, /* 48 TIM2D/GAMETRA.BNK */
    {AT(07, 32, 09),   129920}, /* 49 TIM2D/GAMEDAN.BNK */
    {AT(07, 45, 19),   596544}, /* 50 TIM2D/NAR.BIN */
    {AT(07, 36, 37),    32832}, /* 51 TIM2D/LAS_SN.TIM */
    {AT(07, 36, 54),    32832}, /* 52 TIM2D/LAS_SS.TIM */
    {AT(07, 36, 03),    32832}, /* 53 TIM2D/LAS_KN.TIM */
    {AT(07, 36, 20),    32832}, /* 54 TIM2D/LAS_KS.TIM */
    {AT(07, 35, 44),    32832}, /* 55 TIM2D/LAS_DN.TIM */
    {AT(07, 35, 61),    32832}, /* 56 TIM2D/LAS_DS.TIM */
    {AT(07, 34, 51),    32832}, /* 57 TIM2D/LAS_CN.TIM */
    {AT(07, 34, 68),    32832}, /* 58 TIM2D/LAS_CS.TIM */
    {AT(07, 35, 10),    32832}, /* 59 TIM2D/LAS_CT.TIM */
    {AT(07, 32, 73),   129920}, /* 60 TIM2D/GAMESTO.BNK */
    {AT(08, 07, 45),    19240}, /* 61 U_PIC/STG00.BIN */
    {AT(08, 07, 55),    19240}, /* 62 U_PIC/STG01.BIN */
    {AT(08, 07, 65),    19240}, /* 63 U_PIC/STG02.BIN */
    {AT(08, 08, 00),    19240}, /* 64 U_PIC/STG03.BIN */
    {AT(08, 08, 10),    19240}, /* 65 U_PIC/STG04.BIN */
    {AT(08, 08, 20),    19240}, /* 66 U_PIC/STG05.BIN */
    {AT(08, 08, 30),    19240}, /* 67 U_PIC/STG06.BIN */
    {AT(08, 08, 40),    19240}, /* 68 U_PIC/STG07.BIN */
    {AT(08, 08, 50),    19240}, /* 69 U_PIC/STG08.BIN */
    {AT(08, 08, 60),    19240}, /* 70 U_PIC/STG09.BIN */
    {AT(08, 08, 70),    19240}, /* 71 U_PIC/STG10.BIN */
    {AT(08, 09, 05),    19240}, /* 72 U_PIC/STG11.BIN */
    {AT(08, 09, 15),    19240}, /* 73 U_PIC/STG12.BIN */
    {AT(08, 09, 25),    19240}, /* 74 U_PIC/STG13.BIN */
    {AT(08, 09, 35),    19240}, /* 75 U_PIC/STG14.BIN */
    {AT(08, 09, 45),    19240}, /* 76 U_PIC/STG15.BIN */
    {AT(08, 09, 55),    19240}, /* 77 U_PIC/STG16.BIN */
    {AT(08, 09, 65),    19240}, /* 78 U_PIC/STG17.BIN */
    {AT(08, 10, 00),    19240}, /* 79 U_PIC/STG18.BIN */
    {AT(08, 10, 10),    19240}, /* 80 U_PIC/STG19.BIN */
    {AT(08, 10, 20),    19240}, /* 81 U_PIC/STG20.BIN */
    {AT(08, 10, 30),    19240}, /* 82 U_PIC/STG21.BIN */
    {AT(08, 10, 40),    19240}, /* 83 U_PIC/STG22.BIN */
    {AT(08, 10, 50),    19240}, /* 84 U_PIC/STG23.BIN */
    {AT(08, 10, 60),    19240}, /* 85 U_PIC/STG24.BIN */
    {AT(08, 10, 70),    19240}, /* 86 U_PIC/STG25.BIN */
    {AT(08, 11, 05),    19240}, /* 87 U_PIC/STG26.BIN */
    {AT(08, 11, 15),    19240}, /* 88 U_PIC/STG27.BIN */
    {AT(08, 11, 25),    19240}, /* 89 U_PIC/STG28.BIN */
    {AT(08, 11, 35),    19240}, /* 90 U_PIC/STG29.BIN */
    {AT(08, 07, 24),    19240}, /* 91 TIM2D/STG30.BIN */
    {AT(08, 07, 34),    19240}, /* 92 TIM2D/STG31.BIN */
    {AT(26, 50, 23),   167672}, /* 93 LOADSE/OTHER_C.SE */
    {AT(26, 49, 68),    60312}, /* 94 LOADSE/NINJA_C.SE */
    {AT(08, 04, 36),   434504}, /* 95 TIM2D/STAFF.BIN */
    {AT(07, 38, 72),   307220}, /* 96 TIM2D/LINK_M.TIM */
    {AT(07, 49, 11),    24596}, /* 97 TIM2D/SEA2_16.TIM */

    /* CDROM_GROUP_AUDIO (enum CdromAudioFile): XA streams. A stream's size is
     * its sectors times 2048, as the disc's directory gives it. */
    {AT(27, 04, 74),  2260992}, /*  0 XA_0/MAR_XA00.XA */
    {AT(27, 19, 53),  1572864}, /*  1 XA_0/MAR_XA01.XA */
    {AT(27, 29, 71),  1179648}, /*  2 XA_0/MAR_XA02.XA */
    {AT(27, 37, 47),  1343488}, /*  3 XA_0/MAR_XA03.XA */
    {AT(27, 46, 28),  1310720}, /*  4 XA_0/MAR_XA04.XA */
    {AT(27, 54, 68),  1572864}, /*  5 XA_0/MAR_XA05.XA */
    {AT(28, 05, 11),  1703936}, /*  6 XA_0/MAR_XA06.XA */
    {AT(28, 16, 18),  1572864}, /*  7 XA_0/MAR_XA07.XA */
    {AT(28, 26, 36),  2293760}, /*  8 XA_0/MAR_XA08.XA */
    {AT(28, 41, 31),  1835008}, /*  9 XA_0/MAR_XA09.XA */
    {AT(28, 53, 27),  1966080}, /* 10 XA_0/MAR_XA10.XA */
    {AT(29, 06, 12),  2523136}, /* 11 XA_0/MAR_XA11.XA */
    {AT(29, 22, 44),  2424832}, /* 12 XA_0/MAR_XA12.XA */
    {AT(29, 38, 28),  2785280}, /* 13 XA_0/MAR_XA13.XA */
    {AT(29, 56, 38),  3047424}, /* 14 XA_0/MAR_XA14.XA */
    {AT(30, 16, 26),  3604480}, /* 15 XA_0/MAR_XA15.XA */
    {AT(30, 39, 61),  3375104}, /* 16 XA_0/MAR_XA16.XA */
    {AT(31, 01, 59),  3473408}, /* 17 XA_0/MAR_XA17.XA */
    {AT(31, 24, 30),  3899392}, /* 18 XA_0/MAR_XA18.XA */
    {AT(31, 49, 59),  4325376}, /* 19 XA_0/MAR_XA19.XA */
    {AT(32, 17, 71),  4587520}, /* 20 XA_0/MAR_XA20.XA */
    {AT(32, 47, 61),  4915200}, /* 21 XA_0/MAR_XA21.XA */
    {AT(33, 19, 61),  6225920}, /* 22 XA_0/MAR_XA22.XA */
    {AT(34, 00, 26),  7733248}, /* 23 XA_0/MAR_XA23.XA */
    {AT(34, 50, 52),  8978432}, /* 24 XA_0/MAR_XA24.XA */
    {AT(35, 49, 11), 13303808}, /* 25 XA_0/MAR_XA25.XA */
    {AT(37, 15, 57), 33554432}, /* 26 XA_0/MAR_XA26.XA */
    {AT(40, 54, 16), 41746432}, /* 27 XA_0/MAR_XA27.XA */
    {AT(45, 26, 00), 70680576}, /* 28 XA_0/MAR_XA28.XA */
    {AT(61, 45, 72),   589824}, /* 29 XA_1/KAISYA_1.XA */
    {AT(61, 49, 60),   950272}, /* 30 XA_1/KAISYA_2.XA */
    {AT(08, 11, 46), 74842112}, /* 31 LOADSE/BB2_M1.XA */
    {AT(16, 18, 65), 89112576}, /* 32 LOADSE/BB2_M2.XA */
    {AT(25, 59, 02),  7651328}, /* 33 LOADSE/BB2_M3.XA */
    {AT(53, 06, 13), 19693568}, /* 34 XA_1/END_XA1.XA */
    {AT(55, 14, 29), 24870912}, /* 35 XA_1/END_XA2.XA */
    {AT(57, 56, 23), 35274752}, /* 36 XA_1/END_XA3.XA */

    /* CDROM_GROUP_MOVIE (enum CdromMovieFile) */
    {AT(61, 56, 00),   124928}, /*  0 STR/MOVOVL.EXE */
    {AT(61, 57, 62), 39958528}, /*  1 STR/OPENING.STR */
    {AT(61, 56, 61),   153620}, /*  2 STR/TITLE.TIM */
};
/* clang-format on */

/* The table's entry of each group's first file. Groups 3 and 4 have no
 * files, and nothing asks for one of theirs or of group 7. */
static s16 g_cdrom_group_first_file[8] = {
    0,   /* CDROM_GROUP_NDATA */
    2,   /* CDROM_GROUP_MOTION */
    21,  /* CDROM_GROUP_DATA */
    119, /* 3 */
    119, /* 4 */
    119, /* CDROM_GROUP_AUDIO */
    156, /* CDROM_GROUP_MOVIE */
    0,   /* 7 */
};

/* The music: eight streams of BB2_M1.XA and four of BB2_M2.XA. A length is
 * the sectors of the file that go by while the track plays, 150 a second:
 * the times beside them are worked out from that. */
#define M1 CDROM_AUDIO_BB2_M1
#define M2 CDROM_AUDIO_BB2_M2

/* clang-format off */
static CdMusicTrack g_cdrom_music_tracks[12] = {
    /* file, stream, pad, sectors */
    {M1, 0, 0, 15300}, /*  0 1:42  the narration, and stages */
    {M1, 1, 0, 29850}, /*  1 3:19  the menu's pages */
    {M1, 2, 0, 31200}, /*  2 3:28  stages */
    {M1, 3, 0, 36450}, /*  3 4:03  stages */
    {M1, 4, 0, 33300}, /*  4 3:42  stages */
    {M1, 5, 0, 31200}, /*  5 3:28  nothing asks for it */
    {M1, 6, 0, 36450}, /*  6 4:03  nothing asks for it */
    {M1, 7, 0, 33600}, /*  7 3:44  the top menu */
    {M2, 0, 0, 38250}, /*  8 4:15  chanbara, one clan */
    {M2, 1, 0, 38400}, /*  9 4:16  chanbara, the other */
    {M2, 2, 0, 30300}, /* 10 3:22  a practice round */
    {M2, 3, 0, 43500}, /* 11 4:50  practice */
};
/* clang-format on */
