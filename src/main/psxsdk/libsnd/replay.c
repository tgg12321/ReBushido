/* LIBSND REPLAY: _SsSndReplay, which takes a sequence out of its pause. */
#include "common.h"
#include "libsnd_i.h"

void _SsSndReplay(s16 sep_no, s16 seq_no) {
    struct SeqStruct *score = &_ss_score[sep_no][seq_no];
    score->play_mode = 1;
    _ss_score[sep_no][seq_no].flags &= ~8;
}
