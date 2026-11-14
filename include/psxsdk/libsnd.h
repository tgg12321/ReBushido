#ifndef _LIBSND_H_
#define _LIBSND_H_

/* Sony's libsnd.h (Run-time Library Release 4.0): the sound library, as
 * far as the game links it. Where a prototype is not Sony's, a note says
 * what Sony's is: the library the game links is a little later than 4.0, and
 * its functions are declared here as they are defined. */

#include "common.h"

/*
 * Macro
 */
#define SSPLAY_INFINITY 0
#define SS_NOTICK 0x1000
#define SS_NOTICK0 0
#define SS_TICK60 1
#define SS_TICK240 2
#define SS_TICK120 3
#define SS_TICK50 4
#define SS_TICKVSYNC 5
#define SS_TICKMODE_MAX 6
#define SSPLAY_PAUSE 0
#define SSPLAY_PLAY 1
#define SS_SOFF 0
#define SS_SON 1
#define SS_MIX 0
#define SS_REV 1
#define SS_SERIAL_A 0
#define SS_SERIAL_B 1
#define SS_MUTE_OFF 0
#define SS_MUTE_ON 1

#define SS_IMEDIATE 0
#define SS_IMMEDIATE 0
#define SS_WAIT_COMPLETED 1

#define SS_REV_TYPE_OFF 0
#define SS_REV_TYPE_ROOM 1
#define SS_REV_TYPE_STUDIO_A 2
#define SS_REV_TYPE_STUDIO_B 3
#define SS_REV_TYPE_STUDIO_C 4
#define SS_REV_TYPE_HALL 5
#define SS_REV_TYPE_SPACE 6
#define SS_REV_TYPE_ECHO 7
#define SS_REV_TYPE_DELAY 8
#define SS_REV_TYPE_PIPE 9
#define SSSKIP_TICK 0
#define SSSKIP_NOTE4 1
#define SSSKIP_NOTE8 2
#define SSSKIP_BAR 3

#define SS_SEQ_TABSIZ 176

/*
 * Vag & Vab Structure
 */
typedef struct VabHdr { /* VAB Bank Headdings */
    s32 form;           /* always 'VABp' */
    s32 ver;            /* VAB file version number */
    s32 id;             /* VAB id */
    u32 fsize;          /* VAB file size */
    u16 reserved0;      /* system reserved */
    u16 ps;             /* # of the programs in this bank */
    u16 ts;             /* # of the tones in this bank */
    /* # of the vags in this bank. Sony: unsigned short; the library reads
     * the low byte. */
    u8 vs;
    u8 vspad;
    u8 mvol;       /* master volume for this bank */
    u8 pan;        /* master panning for this bank */
    u8 attr1;      /* bank attributes1 */
    u8 attr2;      /* bank attributes2 */
    u32 reserved1; /* system reserved */
} VabHdr;          /* 32 byte */

typedef struct ProgAtr { /* Program Headdings */
    u8 tones;            /* # of tones */
    u8 mvol;             /* program volume */
    u8 prior;            /* program priority */
    u8 mode;             /* program mode */
    u8 mpan;             /* program pan */
    s8 reserved0;        /* system reserved */
    s16 attr;            /* program attribute */
    u32 reserved1;       /* system reserved */
    /* system reserved. Sony: one unsigned long reserved2; the library keeps
     * two numbers of its own in it. */
    u16 reserved2;
    u16 reserved3;
} ProgAtr; /* 16 byte */

typedef struct VagAtr { /* VAG Tone Headdings */
    u8 prior;           /* tone priority */
    u8 mode;            /* play mode */
    u8 vol;             /* tone volume*/
    u8 pan;             /* tone panning */
    u8 center;          /* center note */
    u8 shift;           /* center note fine tune */
    u8 min;             /* minimam note limit */
    u8 max;             /* maximam note limit */
    u8 vibW;            /* vibrate depth */
    u8 vibT;            /* vibrate duration */
    u8 porW;            /* portamento depth */
    u8 porT;            /* portamento duration */
    u8 pbmin;           /* under pitch bend max */
    u8 pbmax;           /* upper pitch bend max */
    u8 reserved1;       /* system reserved */
    u8 reserved2;       /* system reserved */
    u16 adsr1;          /* adsr1 */
    u16 adsr2;          /* adsr2 */
    s16 prog;           /* parent program*/
    s16 vag;            /* vag reference */
    s16 reserved[4];    /* system reserved */
} VagAtr;               /* 32 byte */

/*
 * Volume Structure
 */
typedef struct {
    u16 left;  /* L Channel */
    u16 right; /* R Channel */
} SndVolume;

/*
 * CallBack
 */
typedef void (*SsMarkCallbackProc)(s16, s16, s16);

/*
 * Prototype
 */
extern s16 SsVabOpenHead(u8 *addr, s16 vabid);
extern s16 SsVabOpenHeadSticky(u8 *addr, s16 vabid, u32 sbaddr);
extern s16 SsVabTransBody(u8 *addr, s16 vabid);
extern s16 SsVabTransCompleted(s16 immediateFlag);
extern void SsVabClose(s16 vabid);
extern void SsInit(void);
extern void SsSetTickMode(s32 tick_mode);
extern void SsStart(void);
extern void SsEnd(void);
extern void SsQuit(void);
extern void SsSeqCalledTbyT(void);
extern void SsSeqStop(s16 seq_access_num);
extern void SsSepStop(s16 sep_access_num, s16 seq_num);
extern void SsSetMVol(s16 voll, s16 volr);
/* Sony: (char, char, char). */
extern void SsSetSerialAttr(s32 s_num, s32 attr, s32 mode);
/* Sony: (char, short, short). */
extern void SsSetSerialVol(s16 s_num, s16 voll, s16 volr);
extern void SsSetMono(void);
extern void SsSetStereo(void);
/* Sony: char SsSetReservedVoice(char). */
extern s32 SsSetReservedVoice(s32 voices);

extern s16 SsUtKeyOnV(s16 voice, s16 vabId, s16 prog, s16 tone, s16 note,
                      s16 fine, s16 voll, s16 volr);
extern s16 SsUtKeyOffV(s16 voice);
/* Sony: the four return short. */
extern s32 SsUtSetDetVVol(s16 voice, s16 detvoll, s16 detvolr);
extern s32 SsUtGetDetVVol(s16 voice, s16 *detvoll, s16 *detvolr);
extern s32 SsUtSetVVol(s16 voice, s16 voll, s16 volr);
extern s32 SsUtGetVVol(s16 voice, s16 *voll, s16 *volr);
extern void SsUtReverbOn(void);
extern void SsUtReverbOff(void);
extern s16 SsUtSetReverbType(s16 type);
extern s16 SsUtGetReverbType(void);
extern void SsUtSetReverbDepth(s16 ldepth, s16 rdepth);
extern void SsUtAllKeyOff(s16 mode);
extern void SsSetAutoKeyOffMode(s16 mode);
extern s16 SsVabFakeHead(u8 *addr, s16 vabid, u32 sbaddr);
extern s16 SsVabFakeBody(s16 vabid);
extern u32 SsUtGetVBaddrInSB(s16 vabid);

/* Not in Sony's header: what the SsVabOpenHead functions call. */
extern s16 SsVabOpenHeadWithMode(u8 *addr, s16 vabid, s16 mode, u32 sbaddr);

#endif /* _LIBSND_H_ */
