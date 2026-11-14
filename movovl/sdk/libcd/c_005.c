/* LIBCD C_005: StSetStream, which says how the stream is to be played. */
#include "libcd_st.h"
#include "libcd_reg.h"

s32 StEmu_Addr;
void (*st_frame_callback)(void);
s32 StRgb24;
s32 CChannel;
s32 StCHANNEL;
void (*StFunc2)(void);

void StSetStream(s32 mode, u32 start_frame, u32 end_frame, void (*func1)(void),
                 void (*func2)(void)) {
    StSetMask(1, start_frame, end_frame);
    StEmu_Addr = 0;
    st_frame_callback = func1;
    StRgb24 = mode & 1;
    CChannel = 0;
    StCHANNEL = 0;
    Stsector_offset = 0;
    Stframe_no = 0;
    StFunc2 = func2;
}
