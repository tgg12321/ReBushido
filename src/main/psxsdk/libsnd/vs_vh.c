/* LIBSND VS_VH: opening a bank's header: SsVabOpenHead and its variants. */
#include "common.h"
#include "libsnd_i.h"

ProgAtr *_svm_vab_pg[16];
VabHdr *_svm_vab_vh[16];
VagAtr *_svm_vab_tn[16];
s32 _svm_vab_total[16];
s32 _svm_vab_start[16];

s16 SsVabOpenHead(u8 *addr, s16 vabid) {
    return SsVabOpenHeadWithMode(addr, vabid, 0, 0);
}

s16 SsVabOpenHeadSticky(u8 *addr, s16 vabid, u32 sbaddr) {
    return SsVabOpenHeadWithMode(addr, vabid, 1, sbaddr);
}

s16 SsVabFakeHead(u8 *addr, s16 vabid, u32 sbaddr) {
    return SsVabOpenHeadWithMode(addr, vabid, 1, sbaddr);
}

/* mode 0: room for the bank's waveforms is taken from the sound memory's
 * allocator. mode 1: they are, or will be, at sbaddr. */
s16 SsVabOpenHeadWithMode(u8 *addr, s16 vabid, s16 mode, u32 sbaddr) {
    int vagLens[256];
    s32 i;
    s32 total;
    s16 id;
    u16 size;
    u16 *vagSizes;
    u32 magic;
    u32 start;
    u8 vags;
    ProgAtr *progs;
    u8 *p;
    VabHdr *hdr;
    id = 0x10;
    if (_spu_getInTransfer() == 1) {
        return -1;
    }
    _spu_setInTransfer(1);
    if (vabid >= 0x10) {
        _spu_setInTransfer(0);
        return -1;
    }
    if (vabid == -1) {
        for (i = 0; i < 16; i++) {
            if (_svm_vab_used[i] == 0) {
                _svm_vab_used[i] = 1;
                id = i;
                _svm_vab_count++;
                break;
            }
        }
    } else {
        /* FAKE: _svm_vab_used[vabid], not through p */
        p = _svm_vab_used;
        if (p[vabid] == 0) {
            _svm_vab_used[vabid] = 1;
            id = vabid;
            _svm_vab_count++;
        }
    }
    if (id >= 0x10) {
        _spu_setInTransfer(0);
        return -1;
    }
    p = addr;
    _svm_vab_vh[id] = (VabHdr *)p;

    p = p + 0x20;
    hdr = (VabHdr *)addr;
    magic = hdr->form;
    if ((magic >> 8) != ('V' << 0x10 | 'A' << 0x8 | 'B')) {
        _svm_vab_used[id] = 0;
        _spu_setInTransfer(0);
        _svm_vab_count -= 1;
        return -1;
    }
    if ((magic & 0xFF) == 'p') {
        if (hdr->ver >= 5) {
            kMaxPrograms = 0x80;
        } else {
            kMaxPrograms = 0x40;
        }
    } else {
        kMaxPrograms = 0x40;
    }
    if (hdr->ps > kMaxPrograms) {
        _svm_vab_used[id] = 0;
        _spu_setInTransfer(0);
        _svm_vab_count -= 1;
        return -1;
    }
    _svm_vab_pg[id] = (ProgAtr *)p;
    progs = (ProgAtr *)p;
    p = p + (kMaxPrograms * 0x10);
    total = 0;
    for (i = 0; i < kMaxPrograms; i++) {
        progs[i].reserved1 = total;
        if (progs[i].tones != 0) {
            total++;
        }
    }
    total = 0;
    _svm_vab_tn[id] = (VagAtr *)p;
    vagSizes = (u16 *)(p + (hdr->ps << 9));
    vags = hdr->vs;
    for (i = 0; i < 256; i++) {
        if (vags >= i) {
            size = *vagSizes;
            if (hdr->ver >= 5) {
                vagLens[i] = size * 8;
            } else {
                vagLens[i] = size * 4;
            }
            total += vagLens[i];
        }
        vagSizes++;
    }
    if (mode == 0) {
        start = SpuMalloc(total);
        if (start == -1) {
            _svm_vab_used[id] = 0;
            _spu_setInTransfer(0);
            _svm_vab_count -= 1;
            return -1;
        }
    } else {
        start = sbaddr;
    }
    if (start + total > 0x80000U) {
        _svm_vab_used[id] = 0;
        _spu_setInTransfer(0);
        _svm_vab_count -= 1;
        return -1;
    }
    _svm_vab_start[id] = start;
    total = 0;
    for (i = 0; i <= vags; i++) {
        total += vagLens[i];
        if (!(i & 1)) {
            progs[i / 2].reserved2 = (start + total) >> 3;
        } else {
            progs[i / 2].reserved3 = (start + total) >> 3;
        }
    }

    _svm_vab_total[id] = total;
    _svm_vab_used[id] = 2;
    return id;
}
