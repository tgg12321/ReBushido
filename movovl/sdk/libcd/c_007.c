/* LIBCD C_007: StFreeRing, which gives a frame's slots back to the ring. */
#include "libcd_st.h"
#include "libcd_reg.h"

u32 StFreeRing(u32 *base) {
    s32 slot;
    s32 i;
    s16 nSectors;
    StSECTOR *sector;
    StSECTOR *p;

    slot = (base - (u32 *)&StRingAddr[StRingSize]) / 504;
    sector = &StRingAddr[slot];
    nSectors = sector->nSectors;
    if ((s16)sector->id != ST_SLOT_TAKEN) {
        return 1;
    }
    for (i = 0; i < nSectors; i++) {
        p = &StRingAddr[i + slot];
        *(s16 *)p = ST_SLOT_FREE;
    }
    st_read_slot = i + slot;
    return 0;
}
