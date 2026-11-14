#ifndef _LIBSPU_H_
#define _LIBSPU_H_

/* Sony's libspu.h (Run-time Library Release 4.0): the SPU library, as far
 * as the game links it; the comments are translated. Where a prototype is
 * not Sony's, a note says what Sony's is: the functions are declared here as
 * they are defined. */

#include "common.h"

/* ----------------------------------------------------------------
 *	CONSTANT
 * ---------------------------------------------------------------- */

#define SPU_SUCCESS 0
#define SPU_INVALID_ARGS (-3)
#define SPU_DIAG (-2)
#define SPU_CHECK (-1)
#define SPU_OFF 0
#define SPU_ON 1
#define SPU_CLEAR 2
#define SPU_RESET 3
#define SPU_DONT_CARE 4
#define SPU_ALL 0
#define SPU_CDONLY 5
#define SPU_VOICEONLY 6
#define SPU_CONT 7
#define SPU_NULL 0

#define SPU_OFF_ENV_ON 2
#define SPU_ON_ENV_OFF 3

#define SPU_ERROR (-1)

#define SPU_TRANSFER_BY_DMA 0L
#define SPU_TRANSFER_BY_IO 1L

#define SPU_TRANSFER_WAIT 1
#define SPU_TRANSFER_PEEK 0
#define SPU_TRANSFER_GLANCE SPU_TRANSFER_PEEK

/*
 *	Voice designate
 */
#define SPU_KEYCH(x) (0x1L << (x))
#define SPU_VOICECH(x) SPU_KEYCH(x)

/* for Voice setting */
#define SPU_VOICE_VOLL (0x01 << 0)        /* volume (left) */
#define SPU_VOICE_VOLR (0x01 << 1)        /* volume (right) */
#define SPU_VOICE_VOLMODEL (0x01 << 2)    /* volume mode (left) */
#define SPU_VOICE_VOLMODER (0x01 << 3)    /* volume mode (right) */
#define SPU_VOICE_PITCH (0x01 << 4)       /* tone (pitch setting) */
#define SPU_VOICE_NOTE (0x01 << 5)        /* tone (note setting) */
#define SPU_VOICE_SAMPLE_NOTE (0x01 << 6) /* waveform data sample note */
#define SPU_VOICE_WDSA (0x01 << 7)        /* waveform data start address */
#define SPU_VOICE_ADSR_AMODE (0x01 << 8)  /* ADSR Attack rate mode */
#define SPU_VOICE_ADSR_SMODE (0x01 << 9)  /* ADSR Sustain rate mode */
#define SPU_VOICE_ADSR_RMODE (0x01 << 10) /* ADSR Release rate mode */
#define SPU_VOICE_ADSR_AR (0x01 << 11)    /* ADSR Attack rate */
#define SPU_VOICE_ADSR_DR (0x01 << 12)    /* ADSR Decay rate */
#define SPU_VOICE_ADSR_SR (0x01 << 13)    /* ADSR Sustain rate */
#define SPU_VOICE_ADSR_RR (0x01 << 14)    /* ADSR Release rate */
#define SPU_VOICE_ADSR_SL (0x01 << 15)    /* ADSR Sustain level */
#define SPU_VOICE_LSAX (0x01 << 16)       /* start address for loop */
#define SPU_VOICE_ADSR_ADSR1 (0x01 << 17) /* ADSR adsr1 for `VagAtr' */
#define SPU_VOICE_ADSR_ADSR2 (0x01 << 18) /* ADSR adsr2 for `VagAtr' */

#define SPU_VOICE_DIRECT 0
#define SPU_VOICE_LINEARIncN 1
#define SPU_VOICE_LINEARIncR 2
#define SPU_VOICE_LINEARDecN 3
#define SPU_VOICE_LINEARDecR 4
#define SPU_VOICE_EXPIncN 5
#define SPU_VOICE_EXPIncR 6
#define SPU_VOICE_EXPDec 7
#define SPU_VOICE_EXPDecN SPU_VOICE_EXPDec
#define SPU_VOICE_EXPDecR SPU_VOICE_EXPDec

#define SPU_COMMON_MVOLL (0x01 << 0)     /* master volume (left) */
#define SPU_COMMON_MVOLR (0x01 << 1)     /* master volume (right) */
#define SPU_COMMON_MVOLMODEL (0x01 << 2) /* master volume mode (left) */
#define SPU_COMMON_MVOLMODER (0x01 << 3) /* master volume mode (right) */
#define SPU_COMMON_RVOLL (0x01 << 4)     /* reverb volume (left) */
#define SPU_COMMON_RVOLR (0x01 << 5)     /* reverb volume (right) */
#define SPU_COMMON_CDVOLL (0x01 << 6)    /* CD input volume (left) */
#define SPU_COMMON_CDVOLR (0x01 << 7)    /* CD input volume (right) */
#define SPU_COMMON_CDREV (0x01 << 8)     /* CD input reverb on/off */
#define SPU_COMMON_CDMIX (0x01 << 9)     /* CD input on/off */
#define SPU_COMMON_EXTVOLL                                                     \
    (0x01 << 10) /* external digital input volume (left) */
#define SPU_COMMON_EXTVOLR                                                     \
    (0x01 << 11) /* external digital input volume (right) */
#define SPU_COMMON_EXTREV                                                      \
    (0x01 << 12) /* external digital input reverb on/off */
#define SPU_COMMON_EXTMIX (0x01 << 13) /* external digital input on/off */

/* for Reverb setting */
#define SPU_REV_MODE (0x01 << 0)      /* mode setting */
#define SPU_REV_DEPTHL (0x01 << 1)    /* reverb depth (left) */
#define SPU_REV_DEPTHR (0x01 << 2)    /* reverb depth (right) */
#define SPU_REV_DELAYTIME (0x01 << 3) /* Delay Time (ECHO, DELAY only) */
#define SPU_REV_FEEDBACK (0x01 << 4)  /* Feedback   (ECHO only) */

#define SPU_REV_MODE_CHECK (-1)
#define SPU_REV_MODE_OFF 0
#define SPU_REV_MODE_ROOM 1
#define SPU_REV_MODE_STUDIO_A 2
#define SPU_REV_MODE_STUDIO_B 3
#define SPU_REV_MODE_STUDIO_C 4
#define SPU_REV_MODE_HALL 5
#define SPU_REV_MODE_SPACE 6
#define SPU_REV_MODE_ECHO 7
#define SPU_REV_MODE_DELAY 8
#define SPU_REV_MODE_PIPE 9
#define SPU_REV_MODE_MAX 10

#define SPU_REV_MODE_CLEAR_WA 0x100

/* ----------------------------------------------------------------
 *	Structure
 * ---------------------------------------------------------------- */

typedef struct {
    s16 left;  /* Lch */
    s16 right; /* Rch */
} SpuVolume;

typedef struct {
    u32 voice;         /* the voices to set, one bit each */
    u32 mask;          /* the attributes to set */
    SpuVolume volume;  /* volume */
    SpuVolume volmode; /* volume mode */
    SpuVolume volumex; /* current volume (not for setting) */
    u16 pitch;         /* tone (pitch setting) */
    u16 note;          /* tone (note setting) */
    u16 sample_note;   /* the note the waveform was sampled at */
    s16 envx;          /* current envelope value (not for setting) */
    u32 addr;          /* waveform data start address */
    u32 loop_addr;     /* loop start address */
    s32 a_mode;        /* Attack rate mode */
    s32 s_mode;        /* Sustain rate mode */
    s32 r_mode;        /* Release rate mode */
    u16 ar;            /* Attack rate */
    u16 dr;            /* Decay rate */
    u16 sr;            /* Sustain rate */
    u16 rr;            /* Release rate */
    u16 sl;            /* Sustain level */
    u16 adsr1;         /* adsr1 for `VagAtr' */
    u16 adsr2;         /* adsr2 for `VagAtr' */
} SpuVoiceAttr;

typedef struct {
    u32 mask; /* the attributes to set */

    s32 mode;        /* reverb mode */
    SpuVolume depth; /* reverb depth */
    s32 delay;       /* Delay Time (ECHO, DELAY only) */
    s32 feedback;    /* Feedback   (ECHO only) */
} SpuReverbAttr;

typedef struct {
    SpuVolume volume; /* volume */
    s32 reverb;       /* reverb on/off */
    s32 mix;          /* mixing on/off */
} SpuExtAttr;

typedef struct {
    u32 mask; /* the attributes to set */

    SpuVolume mvol;     /* master volume */
    SpuVolume mvolmode; /* master volume mode */
    SpuVolume mvolx;    /* current master volume */
    SpuExtAttr cd;      /* CD input attributes */
    SpuExtAttr ext;     /* external digital input attributes */
} SpuCommonAttr;

typedef void (*SpuIRQCallbackProc)(void);
typedef void (*SpuTransferCallbackProc)(void);

/* ----------------------------------------------------------------
 *	for SPU Malloc (used in SpuInitMalloc())
 * ---------------------------------------------------------------- */

#define SPU_MALLOC_RECSIZ 8

/* ----------------------------------------------------------------
 *	prototype declaration
 * ---------------------------------------------------------------- */
extern void SpuInit(void);
extern void SpuStart(void);
extern void SpuQuit(void);

extern s32 SpuSetNoiseClock(s32 n_clock);
/* Sony: unsigned long SpuSetNoiseVoice(long, unsigned long). */
extern void SpuSetNoiseVoice(s32 on_off, s32 voice_bit);

extern s32 SpuSetReverb(s32 on_off);
extern s32 SpuSetReverbModeParam(SpuReverbAttr *attr);
/* Sony: unsigned long SpuSetReverbVoice(long, unsigned long). */
extern void SpuSetReverbVoice(s32 on_off, s32 voice_bit);
extern s32 SpuClearReverbWorkArea(u32 mode); /* Sony: long mode */

/* Sony: unsigned long (unsigned char *addr, unsigned long size), both. */
extern s32 SpuWrite(s32 addr, s32 size);
extern s32 SpuRead(s32 addr, s32 size);
extern s32 SpuSetTransferMode(s32 mode);
extern s32 SpuSetTransferStartAddr(s32 addr); /* Sony: unsigned long */

extern s32 SpuIsTransferCompleted(s32 flag);

extern void SpuSetVoiceAttr(SpuVoiceAttr *arg);
extern void SpuSetKey(s32 on_off, u32 voice_bit);
extern s32 SpuGetKeyStatus(u32 voice_bit);
extern void SpuGetAllKeysStatus(u8 *status); /* Sony: char *status */

extern void SpuSetCommonAttr(SpuCommonAttr *attr);

extern s32 SpuInitMalloc(s32 num, s32 *top); /* Sony: char *top */
extern s32 SpuMalloc(s32 size);
extern void SpuFree(u32 addr);

/* Declared by the later releases of this header. */
extern void SpuGetVoiceVolume(s32 vNum, s16 *volL, s16 *volR);
extern void SpuGetVoiceEnvelope(s32 vNum, u16 *envx);

#endif /* _LIBSPU_H_ */
