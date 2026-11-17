#ifndef PORT_SPU_H
#define PORT_SPU_H

/* The native port's sound (docs/PORT.md, "Sound"): the sound chip in C
 * (src/port/spu.c), the disc's sound (src/port/xa.c), and what joins
 * them to Sony's libraries, to the port's clock and to a file
 * (src/port/sound.c, src/port/wav.c). Sony's libsnd and libspu are
 * compiled as they are, all but the one module that talks to the chip;
 * they write the chip's registers, and the chip here reads them. */

#include "common.h"

/* ---- the chip (src/port/spu.c) ---- */

/* Where the console has the chip's registers, 0x100 halfwords of them:
 * the port has memory there, because Sony's code writes there. */
#define PORT_SPU_REGS_ADDR 0x1F801C00U
#define PORT_SPU_REG_COUNT 0x100
/* Sound memory: 512 KB. */
#define PORT_SPU_RAM_BYTES 0x80000U
#define PORT_SPU_VOICES 24
/* Samples a second, each a left and a right of 16 bits. */
#define PORT_SPU_RATE 44100

/* The registers, by halfword. A voice has eight, voice n's from n * 8. */
enum {
    PORT_SPU_VOICE_VOLUME_LEFT = 0,
    PORT_SPU_VOICE_VOLUME_RIGHT = 1,
    PORT_SPU_VOICE_PITCH = 2,
    PORT_SPU_VOICE_START = 3, /* its waveform's place, in units of 8 bytes */
    PORT_SPU_VOICE_ADSR_LOW = 4,
    PORT_SPU_VOICE_ADSR_HIGH = 5,
    PORT_SPU_VOICE_ENVELOPE = 6, /* the envelope's level now: the chip's */
    PORT_SPU_VOICE_LOOP = 7,     /* where a waveform's end goes back to */
    PORT_SPU_VOICE_REGS = 8,

    PORT_SPU_MAIN_VOLUME = 0xC0,   /* left, right */
    PORT_SPU_REVERB_VOLUME = 0xC2, /* left, right */
    PORT_SPU_KEY_ON = 0xC4,        /* a bit a voice, in two halfwords */
    PORT_SPU_KEY_OFF = 0xC6,
    PORT_SPU_PITCH_MODULATION = 0xC8,
    PORT_SPU_NOISE = 0xCA,
    PORT_SPU_REVERB_VOICES = 0xCC,
    PORT_SPU_ENDED = 0xCE, /* the voices that reached an end: the chip's */
    PORT_SPU_REVERB_BASE = 0xD1,
    PORT_SPU_TRANSFER_ADDR = 0xD3,
    PORT_SPU_CONTROL = 0xD5,
    PORT_SPU_STATUS = 0xD7,
    PORT_SPU_CD_VOLUME = 0xD8, /* left, right */
    PORT_SPU_REVERB_REGS = 0xE0,
};

/* The control register's bits. */
#define PORT_SPU_CONTROL_ON 0x8000
#define PORT_SPU_CONTROL_UNMUTED 0x4000
#define PORT_SPU_CONTROL_NOISE_SHIFT 8 /* six bits: the noise's clock */
#define PORT_SPU_CONTROL_REVERB 0x0080 /* the reverb writes its memory */
#define PORT_SPU_CONTROL_TRANSFER 0x0030
#define PORT_SPU_CONTROL_CD_REVERB 0x0004
#define PORT_SPU_CONTROL_CD 0x0001

/* What a run did, for the end of it to say. */
typedef struct {
    u32 samples;        /* mixed */
    u32 key_ons;        /* voices keyed on */
    u32 key_offs;       /* voices keyed off */
    u32 ends;           /* waveforms that reached their end and stopped */
    u32 noise_samples;  /* a voice's samples that were noise */
    u32 modulated;      /* a voice's samples whose pitch another moved */
    u32 swept;          /* a volume's samples that were a sweep's */
    u32 reverb_samples; /* samples the reverb worked on */
    u32 clipped;        /* samples whose sum was more than 16 bits hold */
} PortSpuCounts;

/* Gives the chip its registers, PORT_SPU_REG_COUNT halfwords that the
 * program writes and the chip reads, and resets it: sound memory
 * cleared, every voice silent. */
extern void port_AttachSpu(u16 *regs);
/* Sound memory, PORT_SPU_RAM_BYTES of it. */
extern u8 *port_GetSpuRam(void);
/* The keys on and off that were written to the registers since the last
 * call take effect, and the two registers are cleared: what the chip
 * does at its next sample. port_RunSpu does this first; whoever writes
 * the key registers twice with no sound between calls it between. */
extern void port_TakeSpuKeys(void);
/* The next `frames` samples into `out`, a left and a right each. `cd`
 * is the disc's sound for the same samples, or NULL for none. `heard`, if
 * not NULL, is what is heard in its place (a mod's music, port_music.h):
 * the chip goes on as it would with `cd`, its reverb, its counts and its
 * memory the same, and only `out` has `heard` where it had `cd`. */
extern void port_RunSpu(const s16 *cd, const s16 *heard, s16 *out, u32 frames);
extern const PortSpuCounts *port_GetSpuCounts(void);
/* Called as a voice is keyed on, if not NULL: for a run's log. */
extern void (*g_port_spu_key_on_hook)(s32 voice, u32 addr, u32 pitch);

/* ---- the disc's sound (src/port/xa.c) ---- */

/* A sector of XA sound as it is on the disc, 2352 bytes: its samples go
 * to the end of what is waiting to be played. Returns how many samples
 * it held, 0 if it is not a sector of sound. */
extern u32 port_FeedXaSector(const u8 *raw);
/* Forgets what is waiting and the decoder's memory: a new stream. */
extern void port_ResetCdAudio(void);
/* The drive's four volumes (Sony's CdMix): how much of the disc's left
 * goes left and right, and of its right, right and left; 0x80 is all of
 * it. */
extern void port_SetCdMix(
    u8 left_to_left, u8 left_to_right, u8 right_to_right, u8 right_to_left);
extern void port_MuteCdAudio(s32 muted);
/* The next `frames` samples of the disc's sound at the chip's rate, a
 * left and a right each, with the drive's volumes applied; silence
 * where nothing is waiting. */
extern void port_ReadCdAudio(s16 *out, u32 frames);
/* The drive's volumes and its mute, as port_ReadCdAudio gives them, on
 * other samples: a mod's music in place of the disc's (port_music.h). */
extern void port_MixCdAudio(s16 *samples, u32 frames);
/* If not NULL, port_ReadCdAudio copies its samples here before the
 * drive's volumes: --dump-music (port_music.h). */
extern s16 *g_port_xa_unmixed;
/* Sectors of sound taken since the program started. */
extern u32 port_GetXaSectorCount(void);

/* ---- the drive, as the sound's clock moves it (src/port/cd.c) ---- */

/* The drive goes on for `frames` samples from now: the sectors of a
 * stream of sound that pass under it in that time are played. */
extern void port_PlayDisc(u32 frames);
/* The stream of sound the drive is playing, if it is: the sector it
 * began at, the channel it was chosen by (-1 if none was) and the sample
 * of the sound's clock it began at. 0 if the drive plays none. */
extern s32 port_GetCdStream(u32 *sector, s32 *channel, u32 *since);

/* ---- the port's sound as a whole (src/port/sound.c) ---- */

/* Samples mixed since the program started: the sound's clock, which is
 * the port's clock of scan lines in other units. */
extern u32 port_GetSampleCount(void);
/* A vertical blank: what Sony's libraries hung on the blank's interrupt
 * runs (the sound library's tick), then the samples of one blank are
 * mixed. */
extern void port_RunSoundBlank(u32 blank);
/* Called with each blank's samples as they are made, if not NULL: the
 * window's sound card (src/port/window/). It may be called more than
 * once in a blank: the blank's samples are made in two pieces, before
 * the sound library's tick and after it. */
extern void (*g_port_sound_hook)(const s16 *samples, u32 frames);
/* What Sony's libcd does to the chip as the drive's library starts
 * (CD_initvol): the disc's sound at half of full scale and the chip's CD
 * input on. The port's libcd is its own, so its CdInit calls this
 * (src/port/cd.c); without it the chip multiplies the disc's sound by 0
 * until the game turns the disc's sound on for itself, and the opening
 * movie, which runs before that, is silent. */
extern void port_InitCdSpuVolume(void);
/* What the run's sound did, to the program's output: `sound ...` lines.
 * Only a run that keeps its sound says it. */
extern void port_PrintSoundSummary(void);
/* Sound memory and then the registers, PORT_SPU_RAM_BYTES and
 * PORT_SPU_REG_COUNT halfwords, to a file that is open: 1 if written. */
extern s32 port_WriteSpu(s32 file);
/* The same to a file of this name, when the run ends. */
extern void port_SetSpuDump(const char *path);
extern void port_WriteSpuDump(void);

/* ---- the sound, kept and written to a file (src/port/wav.c) ---- */

/* From now on every sample mixed is kept; port_WriteWav writes them all
 * to `path` as a WAV file when the run ends. */
extern void port_StartWav(const char *path);
extern s32 port_IsWavOn(void);
extern void port_AppendWav(const s16 *samples, u32 frames);
extern void port_WriteWav(void);

#endif /* PORT_SPU_H */
