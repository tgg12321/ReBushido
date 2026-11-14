/* LIBSND NEXT: _SsSndNextSep, which starts the sequence that was set to
 * follow one that has ended. */
#include "common.h"
#include "libsnd_i.h"

void _SsSndNextSep(s16 sep_no, s16 seq_no) {
    struct SeqStruct *score = &_ss_score[sep_no][seq_no];
    score->l_count = 1;
    score->l_played = 0;
    _ss_score[sep_no][seq_no].flags &= ~0x100;
    _ss_score[sep_no][seq_no].flags &= ~8;
    _ss_score[sep_no][seq_no].flags &= ~2;
    _ss_score[sep_no][seq_no].flags &= ~4;
    _ss_score[sep_no][seq_no].flags &= ~0x200;
    score->read_pos = score->next_sep_pos;
    score->play_mode = 1;
    _ss_score[sep_no][seq_no].flags |= 1;
}
