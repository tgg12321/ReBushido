/* LIBCD C_002: StClearRing, which empties the ring. */
#include "libcd_st.h"
#include "libcd_reg.h"

s32 st_read_slot;
s32 StRingIdx2;
s32 StRingIdx1;
s32 StFinalSector;
s32 StCdIntrFlag;
s16 Stsector_offset;
s32 Stframe_no;

void StClearRing(void) {
    st_read_slot = 0;
    StRingIdx2 = 0;
    StRingIdx1 = 0;
    StFinalSector = 0;
    init_ring_status(0, StRingSize);
    StCdIntrFlag = 0;
    Stsector_offset = 0;
    Stframe_no = 0;
}
