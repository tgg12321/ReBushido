/* LIBCD C_008: init_ring_status, which marks a run of slots free. */
#include "libcd_st.h"
#include "libcd_reg.h"

s32 init_ring_status(s32 first, u32 count) {
    s32 i;
    StSECTOR *p;

    for (i = 0; i < count; i++) {
        p = &StRingAddr[i + first];
        *(s32 *)p = ST_SLOT_FREE;
    }
}
