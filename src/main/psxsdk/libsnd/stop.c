/* LIBSND SSSTOP: _SsSndStop, SsSeqStop and SsSepStop, which stop a sequence
 * and silence its voices. */
#include "common.h"
#include "libsnd_i.h"

void _SsSndStop(s16 sep_no, s16 seq_no) {
    struct SeqStruct *score = &_ss_score[sep_no][seq_no];
    s32 i;

    _ss_score[sep_no][seq_no].flags &= ~1;
    _ss_score[sep_no][seq_no].flags &= ~2;
    _ss_score[sep_no][seq_no].flags &= ~8;
    _ss_score[sep_no][seq_no].flags &= ~0x400;
    _ss_score[sep_no][seq_no].flags |= 4;

    _SsVmSeqKeyOff((s16)(sep_no | (seq_no << 8)));
    _SsVmDamperOff();

    score->play_mode = 0;
    score->elapsed = 0;
    score->unk1C = 0;
    score->unk18 = 0;
    score->unk19 = 0;
    score->unk1E = 0;
    score->unk1A = 0;
    score->unk1B = 0;
    score->unk1F = 0;
    score->channel = 0;
    score->l_played = 0;
    score->unk1C = 0;
    score->unk1D = 0;
    score->unk15 = 0;
    score->running_status = 0;

    score->delta_value = score->init_delta_value;
    score->tempo = score->init_tempo;
    score->step = score->init_step;
    score->read_pos = score->next_sep_pos;
    score->loop_pos = score->next_sep_pos;

    for (i = 0; i < 16; i++) {
        score->programs[i] = i;
        score->panpot[i] = 0x40;
        score->vol[i] = 0x7F;
    }
    score->cur_voll = 0x7F;
    score->cur_volr = 0x7F;
}

void SsSeqStop(s16 seq_access_num) { _SsSndStop(seq_access_num, 0); }

void SsSepStop(s16 sep_access_num, s16 seq_num) {
    _SsSndStop(sep_access_num, seq_num);
}
