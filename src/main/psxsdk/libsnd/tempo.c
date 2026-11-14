/* LIBSND TEMPO: _SsSndTempo, one tick of a sequence's change of tempo. */
#include "common.h"
#include "libsnd_i.h"

void _SsSndTempo(s16 sep_no, s16 seq_no) {
    struct SeqStruct *score = &_ss_score[sep_no][seq_no];

    score->t_time_left--;
    if (score->t_time_left < 0) {
        _ss_score[sep_no][seq_no].flags &= ~0x40;
        _ss_score[sep_no][seq_no].flags &= ~0x80;
        return;
    }

    if (score->t_rate > 0) {
        if ((score->t_time_left % score->t_rate) != 0) {
            return;
        }
        if (score->tempo > score->target_tempo ||
            score->tempo < score->target_tempo) {
            score->tempo = (score->tempo > score->target_tempo)
                               ? score->tempo - 1
                               : score->tempo + 1;
        }
    } else {
        if (score->tempo > score->target_tempo) {
            score->tempo += score->t_rate;
            if (score->tempo < score->target_tempo) {
                score->tempo = score->target_tempo;
            }
        } else if (score->tempo < score->target_tempo) {
            score->tempo -= score->t_rate;
            if (score->tempo > score->target_tempo) {
                score->tempo = score->target_tempo;
            }
        }
    }

    score->step = (score->resolution * score->tempo * 10) / (VBLANK_MINUS * 60);
    if (score->step <= 0) {
        score->step = 1;
    }
    if ((score->t_time_left == 0) || (score->tempo == score->target_tempo)) {
        _ss_score[sep_no][seq_no].flags &= ~0x40;
        _ss_score[sep_no][seq_no].flags &= ~0x80;
    }
}
