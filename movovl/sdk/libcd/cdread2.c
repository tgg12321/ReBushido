/* LIBCD CDREAD2: CdRead2, which starts the drive reading a stream. */
#include "libcd_st.h"

s32 StMode;

static void StCdInterrupt2(u8 intr, u8 *result);

s32 CdRead2(s32 mode) {
    u8 param = mode;

    CdControl(CdlSetmode, &param, NULL);
    if (mode & CdlModeStream) {
        if (mode & CdlModeSize1) {
            StMode = 0;
        } else {
            StMode = 1;
        }
        CdDataCallback(data_ready_callback);
        CdReadyCallback(StCdInterrupt2);
    }
    return CdControl(CdlReadS, NULL, NULL);
}

static void StCdInterrupt2(u8 intr, u8 *result) { StCdInterrupt(); }
