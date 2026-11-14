/* LIBCD C_010: StSetMask, which sets the frames the stream is played
 * between. */
#include "libcd_st.h"
#include "libcd_reg.h"

u32 StSTART_FLAG;
u32 StStartFrame;
u32 StEndFrame;

void StSetMask(u32 mask, u32 start, u32 end) {
    StSTART_FLAG = mask;
    StStartFrame = start;
    StEndFrame = end;
}
