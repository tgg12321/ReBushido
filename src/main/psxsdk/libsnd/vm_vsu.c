/* LIBSND VM_VSU: _SsVmVSetUp, which makes a bank and one of its programs the
 * current ones. */
#include "common.h"
#include "libsnd_i.h"

ProgAtr *_svm_pg;
VabHdr *_svm_vh;
VagAtr *_svm_tn;

s32 _SsVmVSetUp(s32 a0, s32 a1) {
    s16 vabId = a0;
    s16 prog = a1;
    if (vabId < 0 || vabId >= 0x10 || _svm_vab_used[vabId] != 1 ||
        prog >= kMaxPrograms) {
        return -1;
    }
    _svm_vh = _svm_vab_vh[vabId];
    _svm_pg = _svm_vab_pg[vabId];
    _svm_tn = _svm_vab_tn[vabId];
    _svm_cur.vabId = vabId;
    _svm_cur.prog = prog;
    _svm_cur.field_7_fake_program = _svm_pg[prog].reserved1;
    return 0;
}
