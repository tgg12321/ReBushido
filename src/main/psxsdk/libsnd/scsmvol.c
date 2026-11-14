/* LIBSND SSSMV: SsSetMVol, which sets the main volume. */
#include "common.h"
#include "libsnd_i.h"

void SsSetMVol(s16 voll, s16 volr) {
    SpuCommonAttr attr;
    attr.mask = 3;
    attr.mvol.left = voll * 129;
    attr.mvol.right = volr * 129;
    SpuSetCommonAttr(&attr);
}
