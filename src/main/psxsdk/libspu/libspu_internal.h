#ifndef LIBSPU_INTERNAL_H
#define LIBSPU_INTERNAL_H

/* What LIBSPU's modules share. */

#include <psxsdk/libspu.h>

/* The SPU's registers: eight for each voice, then those of the whole. `raw`
 * is the same as an array of halfwords. */
typedef struct tagSpuVoiceRegister {
    SpuVolume volume;
    u16 pitch;
    u16 addr;
    u16 adsr[2];
    u16 volumex;
    u16 loop_addr;
} SPU_VOICE_REG;

typedef struct tagSpuControl {
    SPU_VOICE_REG voice[24];
    SpuVolume main_vol;
    SpuVolume rev_vol;
    u16 key_on[2];
    u16 key_off[2];
    u16 chan_fm[2];
    u16 noise_mode[2];
    u16 rev_mode[2];
    u32 chan_on;
    u16 unk; /* not used here */
    u16 rev_work_addr;
    u16 irq_addr;
    u16 trans_addr;
    u16 trans_fifo;
    u16 spucnt;
    u16 data_trans;
    u16 spustat;
    SpuVolume cd_vol;
    SpuVolume ex_vol;
    SpuVolume main_volx;
    SpuVolume unk_vol; /* not used here */
    u16 dAPF1;
    u16 dAPF2;
    u16 vIIR;
    u16 vCOMB1;
    u16 vCOMB2;
    u16 vCOMB3;
    u16 vCOMB4;
    u16 vWALL;
    u16 vAPF1;
    u16 vAPF2;
    u16 mLSAME;
    u16 mRSAME;
    u16 mLCOMB1;
    u16 mRCOMB1;
    u16 mLCOMB2;
    u16 mRCOMB2;
    u16 dLSAME;
    u16 dRSAME;
    u16 mLDIFF;
    u16 mRDIFF;
    u16 mLCOMB3;
    u16 mRCOMB3;
    u16 mLCOMB4;
    u16 mRCOMB4;
    u16 dLDIFF;
    u16 dRDIFF;
    u16 mLAPF1;
    u16 mRAPF1;
    u16 mLAPF2;
    u16 mRAPF2;
    u16 vLIN;
    u16 vRIN;
} SPU_RXX;

typedef union SpuUnion {
    volatile SPU_RXX rxx;
    volatile u16 raw[0x100];
} SpuUnion;

/* A set of reverb settings: which of the registers to set, then a value for
 * each of the reverb's registers in their order. */
typedef struct {
    u32 flags;
    u16 dAPF1, dAPF2;
    u16 vIIR, vCOMB1, vCOMB2, vCOMB3, vCOMB4;
    u16 vWALL, vAPF1, vAPF2;
    u16 mLSAME, mRSAME, mLCOMB1, mRCOMB1, mLCOMB2, mRCOMB2;
    u16 dLSAME, dRSAME;
    u16 mLDIFF, mRDIFF, mLCOMB3, mRCOMB3, mLCOMB4, mRCOMB4;
    u16 dLDIFF, dRDIFF;
    u16 mLAPF1, mRAPF1, mLAPF2, mRAPF2;
    u16 vLIN, vRIN;
} RevParamEntry;

/* An entry in the sound memory allocator's list. */
typedef struct {
    u32 addr;
    u32 size;
} SpuMemRec;

/* The reverb as it is set: mode, depth, delay, feedback. */
typedef struct {
    u32 unk0; /* not used here */
    s32 mode;
    SpuVolume depth;
    s32 delay;
    s32 feedback;
} SpuRevAttr;

extern SpuUnion *_spu_RXX;
extern s32 _spu_transMode;
extern s32 _spu_trans_mode;
extern s32 _spu_inTransfer;
extern u16 _spu_tsa;
extern s32 _spu_mem_mode_plus;
extern s32 _spu_mem_mode_unitM;
extern s32 _spu_EVdma;
extern s32 _spu_isCalled;
extern s32 _spu_IRQCallback;
extern s32 _spu_keystat;
extern u16 _spu_voice_centerNote[]; /* one for each voice */
extern s32 _spu_rev_flag;
extern s32 _spu_rev_reserve_wa;
extern s32 _spu_rev_offsetaddr;
extern s32 _spu_rev_startaddr[];
extern SpuRevAttr _spu_rev_attr;
extern s32 _spu_AllocBlockNum;
extern s32 _spu_AllocLastNum;
extern SpuMemRec *_spu_memList;
/* what the end of a transfer calls */
extern void (* volatile _spu_transferCallback)();
/* The keys on ([0], [1]) and off ([2], [3]) that are waiting to be written;
 * and the voices' key status, the events' and the transfer's state, which the
 * interrupt changes. */
extern volatile u16 _spu_RQ[10];
extern volatile s32 _spu_RQvoice;
extern volatile s32 _spu_RQmask;
extern volatile s32 _spu_env;

extern s32 _spu_init(s32);
extern void _spu_FiDMA(void);
extern s32 _spu_t(s32, ...);
extern s32 _spu_Fw(s32, s32);
extern s32 _spu_Fr(s32, s32);
extern void _spu_FsetRXX(s32, u32, s32);
extern s32 _spu_FsetRXXa(s32, s32);
extern void _spu_gcSPU(void);
extern void _spu_setReverbAttr(RevParamEntry *);
extern void _SpuInit(s32);
extern void _SpuDataCallback(s32);
extern s32 _SpuSetAnyVoice(s32, u32, s32, s32);
extern s32 _SpuIsInAllocateArea_(u32);
extern u16 _spu_note2pitch(u16, u16, u16, u16);

#endif /* LIBSPU_INTERNAL_H */
