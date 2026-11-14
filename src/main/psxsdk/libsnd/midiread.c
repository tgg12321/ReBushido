/* LIBSND MIDIREAD: reading a sequence: _SsSeqPlay plays what is due this
 * tick, _SsGetSeqData reads the next event and acts on it, _SsSeqGetEof
 * handles the end of the data. */
#include "common.h"
#include "libsnd_i.h"

_SsFCALL SsFCALL;

void _SsSeqPlay(s16 sep_no, s16 seq_no) {
    struct SeqStruct *score = &_ss_score[sep_no][seq_no];
    s32 delta;

    if (score->delta_value - score->step > 0) {
        if (score->unk52 > 0) {
            score->unk52--;
        } else if (score->unk52 == 0) {
            score->unk52 = score->step;
            score->delta_value--;
        } else {
            score->delta_value -= score->step;
        }
    } else if (score->delta_value <= score->step) {
        delta = score->delta_value;
        do {
            do {
                _SsGetSeqData(sep_no, seq_no);
            } while (score->delta_value == 0);
            delta += score->delta_value;
        } while (delta < score->step);
        score->delta_value = delta - score->step;
    }
}

void _SsSeqGetEof(s16 sep_no, s16 seq_no) {
    struct SeqStruct *score = &_ss_score[sep_no][seq_no];

    score->l_played++;
    if (score->l_count == 0) {
        score->elapsed = 0;
        score->unk1C = 0;
        score->delta_value = 0;
        if (_ss_score[sep_no][seq_no].flags & 0x400) {
            score->read_pos = score->unk0C;
        } else {
            score->read_pos = score->next_sep_pos;
        }
        return;
    }

    if (score->l_played < score->l_count) {
        score->elapsed = 0;
        score->unk1C = 0;
        score->delta_value = 0;
        if (_ss_score[sep_no][seq_no].flags & 0x400) {
            score->read_pos = score->unk0C;
            score->loop_pos = score->unk0C;
        } else {
            score->read_pos = score->next_sep_pos;
            score->loop_pos = score->next_sep_pos;
        }
        return;
    }

    _ss_score[sep_no][seq_no].flags &= ~1;
    _ss_score[sep_no][seq_no].flags &= ~8;
    _ss_score[sep_no][seq_no].flags &= ~2;
    _ss_score[sep_no][seq_no].flags |= 0x200;
    _ss_score[sep_no][seq_no].flags |= 4;
    score->play_mode = 0;

    if (_ss_score[sep_no][seq_no].flags & 0x400) {
        score->loop_pos = score->unk0C;
    } else {
        score->loop_pos = score->next_sep_pos;
    }

    if (score->next_sep != 0xFF) {
        score->play_mode = 0;
        _SsSndNextSep(score->next_sep, score->next_seq);
        _SsVmSeqKeyOff((s16)(sep_no | (seq_no << 8)));
    }
    _SsVmSeqKeyOff((s16)(sep_no | (seq_no << 8)));
    score->delta_value = score->step;
}

s32 _SsGetSeqData(s16 sep_no, s16 seq_no) {
    /* velocity is written by both note arms and read only where the note is
     * played. */
    struct SeqStruct *state;
    s32 cmd;
    u8 *ptr;
    u8 b;
    u8 prev;
    u8 velocity;
    s32 ret;
    state = &_ss_score[sep_no][seq_no];
    ptr = state->read_pos;
    state->read_pos = ptr + 1;
    b = ptr[0];
    ret = 0;
    if ((_ss_score[sep_no][seq_no].flags & 0x401) == 0x401) {
        if (state->read_pos == state->unk10 + 1) {
            /* It is called with a third argument, which it does not read. */
            ((void (*)(s16, s16, u8))_SsSeqGetEof)(
                sep_no, seq_no, state->unk10[1]);
            return -1;
        }
    }
    if (b & 0x80) {
        state->channel = b & 0xF;
        cmd = b & 0xF0;
        switch (cmd) {
        case 0x90: {
            u8 *cmd_ptr;
            u32 note;
            state->running_status = 0x90;
            cmd_ptr = state->read_pos;
            state->read_pos = cmd_ptr + 1;
            note = cmd_ptr[0];
            state->read_pos = cmd_ptr + 2;
            velocity = cmd_ptr[1];
            state->delta_value = _SsReadDeltaValue(sep_no, seq_no);
            SsFCALL.noteon(sep_no, seq_no, note, velocity);
            break;
        }

        case 0xB0: {
            u8 *cmd_ptr;
            u8 databyte;
            state->running_status = 0xB0;
            cmd_ptr = state->read_pos;
            state->read_pos = cmd_ptr + 1;
            databyte = cmd_ptr[0];
            SsFCALL.control[0](sep_no, seq_no, databyte);
            break;
        }

        case 0xC0: {
            u8 *cmd_ptr;
            u8 databyte;
            state->running_status = 0xC0;
            cmd_ptr = state->read_pos;
            state->read_pos = cmd_ptr + 1;
            databyte = cmd_ptr[0];
            SsFCALL.programchange(sep_no, seq_no, databyte);
            break;
        }

        case 0xE0:
            state->running_status = 0xE0;
            state->read_pos++;
            SsFCALL.pitchbend(sep_no, seq_no);
            break;

        case 0xF0: {
            u8 *cmd_ptr;
            u8 databyte;
            state->running_status = 0xFF;
            cmd_ptr = state->read_pos;
            state->read_pos = cmd_ptr + 1;
            databyte = cmd_ptr[0];
            if (databyte == 0x2F) {
                ret = 1;
                ((void (*)(s16, s16, u8))_SsSeqGetEof)(sep_no, seq_no, 0x2F);
                break;
            }
            SsFCALL.metaevent(sep_no, seq_no, databyte);
            break;
        }

        default:
            break;
        }

    } else {
        prev = state->running_status;
        switch (prev) {
        case 0x90: {
            u8 *cmd_ptr = state->read_pos;
            state->read_pos = cmd_ptr + 1;
            velocity = cmd_ptr[0];
            state->delta_value = _SsReadDeltaValue(sep_no, seq_no);
            SsFCALL.noteon(sep_no, seq_no, b, velocity);
            break;
        }

        case 0xB0:
            SsFCALL.control[0](sep_no, seq_no, b);
            break;

        case 0xC0:
            SsFCALL.programchange(sep_no, seq_no, b);
            break;

        case 0xE0:
            SsFCALL.pitchbend(sep_no, seq_no);
            break;

        case 0xFF:
            if (b == 0x2F) {
                ret = 1;
                ((void (*)(s16, s16, u8))_SsSeqGetEof)(sep_no, seq_no, 0x2F);
                break;
            }
            SsFCALL.metaevent(sep_no, seq_no, b);
            break;

        default:
            break;
        }
    }
    return ret;
}
