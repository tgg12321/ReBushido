/* LIBSND SSCALL: SsSeqCalledTbyT, the sound system's tick: the voices are
 * brought up to date and every open sequence is moved on. */
#include "common.h"
#include "libsnd_i.h"

struct SeqStruct *_ss_score[32];

/* How many sequence files can be open, and how many sequences in each. */
s16 _snd_seq_s_max;
s16 _snd_seq_t_max;

void SsSeqCalledTbyT(void) {
    int i;
    int j;
    if (_snd_ev_flag != 1) {
        _snd_ev_flag = 1;

        _SsVmFlush();

        for (i = 0; i < _snd_seq_s_max; i++) {
            s32 bit = 1 << i;
            if (_snd_openflag & bit) {
                for (j = 0; j < _snd_seq_t_max; j++) {
                    if (_ss_score[i][j].flags & 1) {
                        _SsSndPlay(i, j);

                        if (_ss_score[i][j].flags & 0x10) {
                            _SsSndCrescendo(i, j);
                        }
                        if (_ss_score[i][j].flags & 0x20) {
                            _SsSndDecrescendo(i, j);
                        }
                        if (_ss_score[i][j].flags & 0x40) {
                            _SsSndTempo(i, j);
                        }
                        if (_ss_score[i][j].flags & 0x80) {
                            _SsSndTempo(i, j);
                        }
                    }
                    if (_ss_score[i][j].flags & 2) {
                        _SsSndPause(i, j);
                    }
                    if (_ss_score[i][j].flags & 8) {
                        _SsSndReplay(i, j);
                    }
                    if (_ss_score[i][j].flags & 4) {
                        _SsSndStop(i, j);
                        _ss_score[i][j].flags = 0;
                    }
                }
            }
        }
        _snd_ev_flag = 0;
    }
}
