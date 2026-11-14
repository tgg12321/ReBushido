/* LIBSND PAUSE: _SsSndPause, a paused sequence's tick. */
#include "common.h"
#include "libsnd_i.h"

void _SsSndPause(s16 sep_no, s16 seq_no) {
    struct SeqStruct *score = &_ss_score[sep_no][seq_no];
    _SsVmSeqKeyOff((s16)(sep_no | (seq_no << 8)));
    score->play_mode = 0;
    _ss_score[sep_no][seq_no].flags &= ~2;
}
