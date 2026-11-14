#ifndef LIBSND_I_H
#define LIBSND_I_H

/* What LIBSND's modules share: the voice manager's and the sequencer's state.
 */

#include <psxsdk/libspu.h>
#include <psxsdk/libsnd.h>

/* The tone being worked on: what the voice manager's functions pass among
 * themselves. */
struct struct_svm {
    u8 prog_tones;
    u8 vabId;
    u8 note;
    u8 fine;
    u8 volume;
    u8 pan;
    u8 prog;
    u8 field_7_fake_program;
    u8 field_8_unknown;
    u8 field_0x9;
    u8 mvol;
    u8 mpan;
    u8 tone;
    u8 tone_vol;
    u8 tone_pan;
    u8 tone_prior;
    u8 tone_center;
    u8 tone_shift;
    u8 tone_min;
    u8 tone_max;
    u8 tone_mode;
    u8 pad;
    short seq_sep_no;
    short tone_vag_idx;
    short voice;
    short voiceOffset;
    short field_0x1e;
};
extern struct struct_svm _svm_cur;

/* One of the SPU's 24 voices, as the voice manager keeps it. The members
 * named unk are not understood: the code only clears them. */
struct SpuVoice {
    s16 vag_idx; /* the waveform it plays; 0xFF, noise */
    s16 unk2;
    s16 pitch;
    u16 envx;
    s16 volume;
    u8 pan;
    u8 unkb;
    s16 channel;
    s16 note;
    s16 seq_sep_no;
    s16 fake_program; /* the program's place among the bank's used ones */
    s16 prog;
    s16 tone;
    s16 vabId;
    s16 priority;
    u8 pad4[1];
    u8 playing; /* 0 silent, 1 a waveform, 2 noise */
    s16 auto_vol;
    s16 unk1e;
    s16 unk20;
    s16 unk22;
    s16 start_vol;
    s16 end_vol;
    s16 auto_pan;
    s16 unk2a;
    s16 unk2c;
    s16 unk2e;
    s16 start_pan;
    s16 end_pan;
};
extern struct SpuVoice _svm_voice[24];

extern VabHdr *_svm_vh; /* the current bank's header */

/* A sequence being played. _ss_score[sep][seq] is sequence `seq` of sequence
 * file `sep`. The members named unk are not understood, or not read here.
 * The names of the crescendo's, the tempo change's and the timing's members
 * are read from what the code does with them, not Sony's. */
struct SeqStruct {
    u8 *read_pos;
    u8 *next_sep_pos;
    u8 *loop_pos;
    /* used in place of next_sep_pos when flag 0x400 is set */
    u8 *unk0C;
    /* compared with read_pos when flags 0x401 are set */
    u8 *unk10;
    u8 play_mode; /* 1 = playing: set by replay, cleared by pause */
    u8 unk15;
    u8 running_status; /* the MIDI status byte the last event had */
    u8 channel;
    u8 unk18;
    u8 unk19;
    u8 unk1A;
    u8 unk1B;
    u8 unk1C;
    u8 unk1D;
    u8 unk1E;
    u8 unk1F;
    u8 l_count;  /* times to play it; 0, for ever */
    u8 l_played; /* times it has been played */
    u8 next_sep; /* the sequence to start when this one ends; 0xFF, none */
    u8 next_seq;
    u8 unk24[3];
    u8 panpot[16];
    u8 programs[16];
    s16 v_vol;      /* how far a crescendo moves the volume */
    s16 v_left;     /* how much of that is still to come */
    s16 v_rate;     /* ticks to a step of volume; negative, steps to a tick */
    s16 t_rate;     /* the same, of a change of tempo */
    s16 resolution; /* the sequence's divisions of a quarter note */
    s16 unk52;
    s16 step;      /* the sequence's time that passes in a tick */
    s16 init_step; /* what a stop puts step back to */
    u16 voll;      /* sequence L volume */
    u16 volr;      /* sequence R volume */
    s16 cur_voll;  /* the volume as the voice manager has it */
    s16 cur_volr;
    s16 vol[16];
    s32 unk80;
    s32 init_delta_value; /* what a stop puts delta_value back to */
    s32 elapsed;          /* the delta times read since the start */
    s32 init_tempo;       /* what a stop puts tempo back to */
    s32 delta_value;
    u32 tempo;       /* tempo */
    s32 flags;       /* play-state flags */
    s32 v_time;      /* ticks a crescendo takes */
    s32 v_time_left; /* ticks of it still to come */
    s32 unkA4;
    s32 t_time_left;  /* ticks of a change of tempo still to come */
    u32 target_tempo; /* target tempo */
};
extern struct SeqStruct *_ss_score[32];

/* The functions the sequencer calls for each kind of MIDI event (Sony's
 * libsnd.h). */
typedef struct {
    void (*noteon)();
    void (*programchange)();
    void (*pitchbend)();
    void (*metaevent)();
    void (*control[13])();
    void (*ccentry[20])();
} _SsFCALL;

extern _SsFCALL SsFCALL;

/* How the tick is driven. */
typedef struct {
    s32 tick_rate;    /* ticks a second */
    s32 manual_tick;  /* the program calls SsSeqCalledTbyT itself */
    s32 tick_func;    /* what a tick calls */
    s32 old_callback; /* the vertical blank handler the tick replaced */
    u8 on_vsync;      /* the tick is a vertical blank callback */
    u8 half_rate;     /* every other interrupt is a tick */
    u8 irq;           /* the interrupt the tick is on; 0x7F, none */
    u8 pad;
    u32 half_toggle;
} SndSeqTickEnv;

extern SndSeqTickEnv _snd_seq_tick_env;

/* The voice manager's and the sequencer's state. */
extern u8 _SsVmMaxVoice;
extern s16 kMaxPrograms;
extern s32 VBLANK_MINUS;
extern s32 _snd_ev_flag;
extern s32 _snd_openflag;
extern ProgAtr *_svm_pg;
extern VagAtr *_svm_tn;
extern s16 _svm_damper;
extern s16 _svm_stereo_mono;
extern u8 _svm_auto_kof_mode;
extern s16 _svm_sreg_buf[];
extern u8 _svm_sreg_dirty[];
extern s32 _svm_envx_hist[];
extern u16 _svm_okon1;
extern u16 _svm_okon2;
extern u16 _svm_okof1;
extern u16 _svm_okof2;
extern u16 _svm_orev1;
extern u16 _svm_orev2;
extern u16 _svm_vab_count;
extern u8 _svm_vab_used[];
extern s32 _svm_vab_start[];
extern s32 _svm_vab_total[];
extern VabHdr *_svm_vab_vh[];
extern ProgAtr *_svm_vab_pg[];
extern VagAtr *_svm_vab_tn[];
/* the voice manager's reverb attribute block */
extern SpuReverbAttr _svm_rattr;

extern void _SsInit(void);
extern void _SsVmInit(s32);
extern void _SsVmFlush(void);
extern void _SsVmKeyOnNow(s32, u16);
extern void _SsVmKeyOffNow(s32);
extern void _SsVmSeqKeyOff(s16);
extern void _SsVmDoAllocate(void);
extern s32 _SsVmVSetUp(s32, s32);
extern void _SsVmDamperOff(void);
extern s16 _SsVmGetSeqVol(s32, s16 *, s16 *);
extern s16 _SsVmSetSeqVol(s16, u16, u16, s16);
extern void vmNoiseOn(u8);
extern s32 note2pitch2(u16, u16);
extern s32 _SsReadDeltaValue(s16, s16);
extern void _SsSeqPlay(s16, s16);
extern s32 _SsGetSeqData(s16, s16);
extern void _SsSndNextSep(s16, s16);
extern void _SsSndPlay(s16, s16);
extern void _SsSndCrescendo(s16, s16);
extern void _SsSndDecrescendo(s16, s16);
extern void _SsSndTempo(s16, s16);
extern void _SsSndPause(s16, s16);
extern void _SsSndReplay(s16, s16);
extern void _SsSndStop(s16, s16);

/* LIBSPU */
extern void _spu_setInTransfer(s32);
extern s32 _spu_getInTransfer(void);
extern void SpuSetVoiceAttr(SpuVoiceAttr *);

#endif /* LIBSND_I_H */
