/* The motion decoder (motion_decode.c) against the disc's motion files. */
#include "src/main/motion_decode.c"
#include "runtime/runtime.h"

/* Where a motion file says its frame stream starts. */
#define FILE_STREAM_OFFSET 0x10

/* The decoder's work area: a pose, its rates and the codes. */
static u16 g_test_work[MOTION_FRAME_VALUES * 3];

static void test_PrintFrame(
    s32 file_index, s32 frame_index, MotionFrame *frame) {
    u16 *values = (u16 *)frame;
    s32 i;

    test_Print("%d %d ", file_index, frame_index);
    for (i = 0; i < MOTION_FRAME_VALUES; i++) {
        test_Print("%04x", values[i]);
    }
    test_Print("\n");
}

/* The arguments are pairs: a motion file and how many frames it has
 * (motion_decode.py works that out). Each file's frames are decoded in
 * order, as the game plays them, and printed for motion_decode.py to
 * compare with its own decoding. Here: the same frames asked for in another
 * order, backwards and by leaps, through a second decoder, come out the
 * same. */
void test_Main(void) {
    s32 file_index;

    for (file_index = 0; test_Arg(file_index * 2); file_index++) {
        s32 frame_count = test_ParseInt(test_Arg(file_index * 2 + 1));
        u8 *file = test_ReadFile(test_Arg(file_index * 2), NULL);
        u32 *stream;
        MotionFrame *frames;
        MotionFrame frame;
        s32 i;

        CHECK(file != NULL);
        if (!file) {
            continue;
        }
        stream = (u32 *)(file + *(u32 *)(file + FILE_STREAM_OFFSET));
        frames = test_Alloc(frame_count * sizeof(MotionFrame));

        motion_OpenStream(0, stream);
        for (i = 0; i < frame_count; i++) {
            motion_DecodeFrame(0, i, &frames[i], g_test_work);
            test_PrintFrame(file_index, i, &frames[i]);
        }

        motion_OpenStream(1, stream);
        for (i = frame_count - 1; i >= 0; i--) {
            motion_DecodeFrame(1, i, &frame, g_test_work);
            CHECK(test_BytesEqual(&frame, &frames[i], sizeof(MotionFrame)));
        }
        for (i = 0; i < frame_count; i++) {
            /* 37 has no factor in common with a count of frames that is
             * not a multiple of it: every frame, in leaps; and each twice,
             * as a fighter standing still asks */
            s32 leap = (i * 37) % frame_count;

            motion_DecodeFrame(1, leap, &frame, g_test_work);
            CHECK(test_BytesEqual(&frame, &frames[leap], sizeof(MotionFrame)));
            motion_DecodeFrame(1, leap, &frame, g_test_work);
            CHECK(test_BytesEqual(&frame, &frames[leap], sizeof(MotionFrame)));
        }
    }
}
