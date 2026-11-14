/* LIBSPU S_SRA: _spu_setReverbAttr, which writes a set of reverb settings to
 * the SPU's registers. */
#include "common.h"
#include "libspu_internal.h"

void _spu_setReverbAttr(RevParamEntry *attr) {
    s32 flags = attr->flags;
    s32 zero = flags == 0;

    if (zero || (flags & 0x1)) {
        _spu_RXX->rxx.dAPF1 = attr->dAPF1;
    }
    if (zero || (flags & 0x2)) {
        _spu_RXX->rxx.dAPF2 = attr->dAPF2;
    }
    if (zero || (flags & 0x4)) {
        _spu_RXX->rxx.vIIR = attr->vIIR;
    }
    if (zero || (flags & 0x8)) {
        _spu_RXX->rxx.vCOMB1 = attr->vCOMB1;
    }
    if (zero || (flags & 0x10)) {
        _spu_RXX->rxx.vCOMB2 = attr->vCOMB2;
    }
    if (zero || (flags & 0x20)) {
        _spu_RXX->rxx.vCOMB3 = attr->vCOMB3;
    }
    if (zero || (flags & 0x40)) {
        _spu_RXX->rxx.vCOMB4 = attr->vCOMB4;
    }
    if (zero || (flags & 0x80)) {
        _spu_RXX->rxx.vWALL = attr->vWALL;
    }
    if (zero || (flags & 0x100)) {
        _spu_RXX->rxx.vAPF1 = attr->vAPF1;
    }
    if (zero || (flags & 0x200)) {
        _spu_RXX->rxx.vAPF2 = attr->vAPF2;
    }
    if (zero || (flags & 0x400)) {
        _spu_RXX->rxx.mLSAME = attr->mLSAME;
    }
    if (zero || (flags & 0x800)) {
        _spu_RXX->rxx.mRSAME = attr->mRSAME;
    }
    if (zero || (flags & 0x1000)) {
        _spu_RXX->rxx.mLCOMB1 = attr->mLCOMB1;
    }
    if (zero || (flags & 0x2000)) {
        _spu_RXX->rxx.mRCOMB1 = attr->mRCOMB1;
    }
    if (zero || (flags & 0x4000)) {
        _spu_RXX->rxx.mLCOMB2 = attr->mLCOMB2;
    }
    if (zero || (flags & 0x8000)) {
        _spu_RXX->rxx.mRCOMB2 = attr->mRCOMB2;
    }
    if (zero || (flags & 0x10000)) {
        _spu_RXX->rxx.dLSAME = attr->dLSAME;
    }
    if (zero || (flags & 0x20000)) {
        _spu_RXX->rxx.dRSAME = attr->dRSAME;
    }
    if (zero || (flags & 0x40000)) {
        _spu_RXX->rxx.mLDIFF = attr->mLDIFF;
    }
    if (zero || (flags & 0x80000)) {
        _spu_RXX->rxx.mRDIFF = attr->mRDIFF;
    }
    if (zero || (flags & 0x100000)) {
        _spu_RXX->rxx.mLCOMB3 = attr->mLCOMB3;
    }
    if (zero || (flags & 0x200000)) {
        _spu_RXX->rxx.mRCOMB3 = attr->mRCOMB3;
    }
    if (zero || (flags & 0x400000)) {
        _spu_RXX->rxx.mLCOMB4 = attr->mLCOMB4;
    }
    if (zero || (flags & 0x800000)) {
        _spu_RXX->rxx.mRCOMB4 = attr->mRCOMB4;
    }
    if (zero || (flags & 0x1000000)) {
        _spu_RXX->rxx.dLDIFF = attr->dLDIFF;
    }
    if (zero || (flags & 0x2000000)) {
        _spu_RXX->rxx.dRDIFF = attr->dRDIFF;
    }
    if (zero || (flags & 0x4000000)) {
        _spu_RXX->rxx.mLAPF1 = attr->mLAPF1;
    }
    if (zero || (flags & 0x8000000)) {
        _spu_RXX->rxx.mRAPF1 = attr->mRAPF1;
    }
    if (zero || (flags & 0x10000000)) {
        _spu_RXX->rxx.mLAPF2 = attr->mLAPF2;
    }
    if (zero || (flags & 0x20000000)) {
        _spu_RXX->rxx.mRAPF2 = attr->mRAPF2;
    }
    if (zero || (flags & 0x40000000)) {
        _spu_RXX->rxx.vLIN = attr->vLIN;
    }
    if (zero || (flags < 0)) {
        _spu_RXX->rxx.vRIN = attr->vRIN;
    }
}
