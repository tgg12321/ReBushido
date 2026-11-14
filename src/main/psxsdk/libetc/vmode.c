/* LIBETC VMODE: SetVideoMode and GetVideoMode. */
#include "common.h"
#include <psxsdk/libetc.h>

s32 video_mode = MODE_NTSC;

s32 SetVideoMode(s32 mode) {
    s32 old = video_mode;
    video_mode = mode;
    return old;
}

s32 GetVideoMode(void) { return video_mode; }
