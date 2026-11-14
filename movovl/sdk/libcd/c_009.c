/* LIBCD C_009: StGetNext, which hands out the next complete frame. */
#include "libcd_st.h"
#include "libcd_reg.h"

u32 StGetNext(u32 **addr, u32 **header) {
    volatile StSECTOR *ptr = &StRingAddr[st_read_slot];

    if (ptr->id == ST_SLOT_WRAP) {
        st_read_slot = 0;
        if (StEndFrame != 0) {
            ptr->id = ST_SLOT_FREE;
        }
        ptr = &StRingAddr[st_read_slot];
    }
    if (ptr->id == ST_SLOT_READY) {
        ptr->id = ST_SLOT_TAKEN;
        *addr = (u32 *)(&StRingAddr[StRingSize] + st_read_slot * 0x3F);
        *header = (u32 *)ptr;
        return 0;
    }
    return 1;
}
