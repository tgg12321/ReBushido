/* LIBCD C_004: data_ready_callback, run when the last sector of a frame is
 * in the ring, and StGetBackloc. */
#include "libcd_st.h"
#include "libcd_reg.h"

/* Where on the disc the last complete frame began, and its number. */
static CdlLOC fp;
static s32 frame_count;

void data_ready_callback(void) {
    StSECTOR *ptr = &StRingAddr[StRingIdx2];

    /* FAKE: without the empty loop the load of StRingIdx1 comes before
     * that of ptr->frameCount. Tried: volatile on either variable, a local
     * for the count, the assignment split from the declaration. */
    do {
    } while (0);
    ptr->id = ST_SLOT_READY;
    fp = ptr->loc;
    frame_count = ptr->frameCount;
    StRingIdx2 = StRingIdx1;
    if (st_frame_callback != NULL) {
        st_frame_callback();
    }
    StFinalSector = 0;
}

s32 StGetBackloc(CdlLOC *loc) {
    if (StMode) {
        return -1;
    }
    CdIntToPos(CdPosToInt(&fp) + 1, loc);
    return frame_count;
}
