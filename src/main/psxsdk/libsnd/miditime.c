/* LIBSND MIDITIME: _SsReadDeltaValue, which reads the time to the next event.
 */
#include "common.h"
#include "libsnd_i.h"

s32 _SsReadDeltaValue(s16 sep_no, s16 seq_no) {
    s32 result;
    u8 *ptr;
    struct SeqStruct *score;
    s32 val;
    s32 byte;

    score = &_ss_score[sep_no][seq_no];
    ptr = score->read_pos;
    score->read_pos = ptr + 1;
    val = *ptr;
    if (val == 0) {
        return 0;
    }
    if (val & 0x80) {
        val &= 0x7F;
        do {
            ptr = score->read_pos;
            score->read_pos = ptr + 1;
            byte = *ptr;
            val = (val << 7) + (byte & 0x7F);
        } while (byte & 0x80);
    }
    result = val * 10;
    score->elapsed += result;
    return result;
}
