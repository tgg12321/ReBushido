/* LIBSND DECRES: _SsSndDecrescendo, one tick of a sequence's decrescendo. */
#include "common.h"
#include "libsnd_i.h"

void _SsSndDecrescendo(s16 sep_no, s16 seq_no) {
    struct SeqStruct *score = &_ss_score[sep_no][seq_no];
    u16 voll, volr;

    if (--(score->v_time_left) < 0) {
        _ss_score[sep_no][seq_no].flags &= ~0x20;
    } else if (score->v_rate > 0) {
        if ((score->v_time_left % score->v_rate) == 0) {
            score->v_left = score->v_left - 1;
            if (score->v_left >= 0) {
                _SsVmGetSeqVol(
                    (s16)(sep_no | (seq_no << 8)), (s16 *)&voll, (s16 *)&volr);
                if ((((u16)voll - 1) >= ((u16)voll - score->v_left)) ||
                    (((u16)volr - 1) >= ((u16)volr - score->v_left))) {
                    if ((voll == 0) || (volr == 0)) {
                        _ss_score[sep_no][seq_no].flags &= ~0x20;
                    } else {
                        _SsVmSetSeqVol((s16)(sep_no | (seq_no << 8)),
                                       (u16)(voll - 1), (u16)(volr - 1), 1);
                    }
                }
            } else {
                _SsVmSetSeqVol((s16)(sep_no | (seq_no << 8)), 0, 0, 1);
                _ss_score[sep_no][seq_no].flags &= ~0x20;
            }
            if ((score->v_time_left == 0) || (score->v_left <= 0))
                _ss_score[sep_no][seq_no].flags &= ~0x20;
        }
    } else if (score->v_rate < 0) {
        score->v_left = score->v_left + score->v_rate;
        if (score->v_left >= 0) {
            _SsVmGetSeqVol(
                (s16)(sep_no | (seq_no << 8)), (s16 *)&voll, (s16 *)&volr);
            if ((((u16)voll + score->v_rate) <= 0) &&
                (((u16)volr + score->v_rate) <= 0)) {
                _SsVmSetSeqVol((s16)(sep_no | (seq_no << 8)), 0, 0, 1);
                _ss_score[sep_no][seq_no].flags &= ~0x20;
            }
            if (((score->v_time - score->v_time_left) * -score->v_rate) <
                score->v_vol) {
                if ((voll == 0) || (volr == 0)) {
                    _ss_score[sep_no][seq_no].flags &= ~0x20;
                } else {
                    _SsVmSetSeqVol((s16)(sep_no | (seq_no << 8)),
                                   (u16)(voll + score->v_rate),
                                   (u16)(volr + score->v_rate), 1);
                }
            }
        } else {
            _SsVmSetSeqVol((s16)(sep_no | (seq_no << 8)), 0, 0, 1);
            _ss_score[sep_no][seq_no].flags &= ~0x20;
        }
        if ((score->v_time_left == 0) || (score->v_left <= 0))
            _ss_score[sep_no][seq_no].flags &= ~0x20;
    }
    _SsVmGetSeqVol(
        (s16)(sep_no | (seq_no << 8)), &score->cur_voll, &score->cur_volr);
}
