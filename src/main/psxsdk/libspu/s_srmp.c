/* LIBSPU S_SRMP: SpuSetReverbModeParam, which sets the reverb's mode, depth,
 * delay and feedback. */
#include "common.h"
#include "libspu_internal.h"

extern RevParamEntry _spu_rev_param[];

static inline void _memcpy(char *dst, char *src, u32 size) {
    while (size--) {
        *dst++ = *src++;
    }
}

s32 SpuSetReverbModeParam(SpuReverbAttr *attr) {
    RevParamEntry entry;
    u32 mode;
    u16 cnt;

    s32 reverbOn = 0;
    s32 modeSet = 0;
    s32 delaySet = 0;
    s32 clearArea = 0;
    s32 feedbackSet = 0;

    u32 mask = attr->mask;
    s32 bSetAll = attr->mask == 0;

    entry.flags = 0;
    if (bSetAll || (mask & 0x1)) {
        mode = attr->mode;
        if (attr->mode & 0x100) {
            mode &= ~0x100;
            clearArea = 1;
        }
        if (mode >= 0xA || _SpuIsInAllocateArea_(_spu_rev_startaddr[mode])) {
            return -1;
        }
        modeSet = 1;
        _spu_rev_attr.mode = mode;
        _spu_rev_offsetaddr = _spu_rev_startaddr[_spu_rev_attr.mode];
        _memcpy((char *)&entry, (char *)&_spu_rev_param[_spu_rev_attr.mode],
                sizeof(RevParamEntry));
        switch (_spu_rev_attr.mode) {
        case 7: /* SPU_REV_MODE_ECHO */
            _spu_rev_attr.feedback = 0x7F;
            _spu_rev_attr.delay = 0x7F;
            break;
        case 8: /* SPU_REV_MODE_DELAY */
            _spu_rev_attr.feedback = 0;
            _spu_rev_attr.delay = 0x7F;
            break;
        default:
            _spu_rev_attr.feedback = 0;
            _spu_rev_attr.delay = 0;
            break;
        }
    }
    if (bSetAll || (mask & 0x8)) {
        switch (_spu_rev_attr.mode) {
        case 7: /* SPU_REV_MODE_ECHO */
        case 8: /* SPU_REV_MODE_DELAY */
            delaySet = 1;
            if (!modeSet) {
                _memcpy((char *)&entry,
                        (char *)&_spu_rev_param[_spu_rev_attr.mode],
                        sizeof(RevParamEntry));
                entry.flags = 0x0C011C00;
            }
            _spu_rev_attr.delay = attr->delay;
            entry.mLSAME = ((_spu_rev_attr.delay << 0xD) / 0x7F) - entry.dAPF1;
            entry.mRSAME = ((_spu_rev_attr.delay << 0xC) / 0x7F) - entry.dAPF2;
            entry.mLCOMB1 =
                ((_spu_rev_attr.delay << 0xC) / 0x7F) + entry.mRCOMB1;
            entry.dLSAME = ((_spu_rev_attr.delay << 0xC) / 0x7F) + entry.dRSAME;
            entry.mLAPF1 = ((_spu_rev_attr.delay << 0xC) / 0x7F) + entry.mLAPF2;
            entry.mRAPF1 = ((_spu_rev_attr.delay << 0xC) / 0x7F) + entry.mRAPF2;
            break;
        default:
            break;
        }
    }
    if (bSetAll || (mask & 0x10)) {
        switch (_spu_rev_attr.mode) {
        case 7: /* SPU_REV_MODE_ECHO */
        case 8: /* SPU_REV_MODE_DELAY */
            feedbackSet = 1;
            if (!modeSet) {
                if (!delaySet) {
                    _memcpy((char *)&entry,
                            (char *)&_spu_rev_param[_spu_rev_attr.mode],
                            sizeof(RevParamEntry));
                    entry.flags = 0x80;
                } else {
                    entry.flags |= 0x80;
                }
            }
            _spu_rev_attr.feedback = attr->feedback;
            entry.vWALL = (_spu_rev_attr.feedback * 0x8100) / 0x7F;
            break;
        default:
            break;
        }
    }
    if (modeSet) {
        reverbOn = (_spu_RXX->rxx.spucnt >> 7) & 1;
        if (reverbOn) {
            cnt = _spu_RXX->rxx.spucnt;
            cnt &= ~0x80;
            _spu_RXX->rxx.spucnt = cnt;
        }
    }
    if (!modeSet) {
        if (bSetAll || (mask & 0x2)) {
            _spu_RXX->rxx.rev_vol.left = attr->depth.left;
            _spu_rev_attr.depth.left = attr->depth.left;
        }
        if (bSetAll || (mask & 0x4)) {
            _spu_RXX->rxx.rev_vol.right = attr->depth.right;
            _spu_rev_attr.depth.right = attr->depth.right;
        }
    } else {
        _spu_RXX->rxx.rev_vol.left = 0;
        _spu_RXX->rxx.rev_vol.right = 0;
        _spu_rev_attr.depth.left = 0;
        _spu_rev_attr.depth.right = 0;
    }
    if (modeSet || delaySet || feedbackSet) {
        _spu_setReverbAttr(&entry);
    }
    if (clearArea) {
        SpuClearReverbWorkArea(_spu_rev_attr.mode);
    }
    if (modeSet) {
        _spu_FsetRXX(0xD1, _spu_rev_offsetaddr, 0);
        if (reverbOn) {
            cnt = _spu_RXX->rxx.spucnt;
            cnt |= 0x80;
            _spu_RXX->rxx.spucnt = cnt;
        }
    }
    return 0;
}
