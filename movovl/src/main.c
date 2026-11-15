/* MOVOVL.EXE: the overlay the game runs to play the opening movie. It plays
 * the movie, shows the title picture from a given frame on while the movie's
 * sound runs out, holds it for ten seconds and fades it to black. The cross
 * button skips straight to the end. Then it returns to the game. */
#include "common.h"
#include "sdk.h"
#include "movie.h"
#include <psxsdk/libetc.h>
#include <psxsdk/libgpu.h>

/* The title picture is kept in video memory to the right of the two display
 * buffers. */
#define STILL_X 480

/* The title picture's file: 76 sectors, its pixels after a 20-byte header. */
#define STILL_SECTORS 76
#define TIM_PIXELS_NO_CLUT 20

#define PAD_SKIP PADRdown /* cross */

#define CD_VOLUME_MAX 255

#define STILL_HOLD_FRAMES 600
#define FADE_STEPS 64
/* Texture page word for subtractive blending (rate 2: what is there less the
 * polygon): the fade darkens the picture. */
#define TPAGE_SUBTRACT getTPage(0, 2, 0, 0)

#define clampToByte(v)                                                         \
    if ((v) < 0) {                                                             \
        (v) = 0;                                                               \
    } else if ((v) > 255) {                                                    \
        (v) = 255;                                                             \
    }

/* What the game passes in argv (sys_ExecMovieOverlay in the main program):
 * six words, so its pointers are four bytes wide wherever this is compiled
 * (PTR32, include/common.h; docs/PORT.md, "64 bits"). On the PlayStation
 * each is the plain pointer and the bytes are what they always were. */
typedef struct {
    PTR32(CdlFILE *) movie_file;
    u32 end_frame; /* the movie stops here */
    PTR32(MovieRing *) ring;
    /* also where the title picture's file is read first */
    PTR32(MovieWork *) work;
    PTR32(CdlLOC *) still_loc;
    u32 still_frame; /* the title picture is shown from here on */
} MovieArgs;

/* Set by the player when the movie reaches the still frame. */
s32 g_movie_still_reached = 0;

/* The two display buffers, one over the other, and where the title picture
 * is kept. */
RECT g_clear_rect = {0, 0, STILL_X, 2 * SCREEN_HEIGHT};
RECT g_still_rect = {STILL_X, 0, SCREEN_WIDTH, SCREEN_HEIGHT};

void sys_SetCdVolume(u8 left, u8 left_to_right, u8 right, u8 right_to_left) {
    CdlATV vol;

    vol.val0 = left;
    vol.val1 = left_to_right;
    vol.val2 = right;
    vol.val3 = right_to_left;
    CdMix(&vol);
}

/* Plays the movie, holds the title picture and fades it, and returns to the
 * game. `argc` is not used. */
void main(s32 argc, MovieArgs *args) {
    DISPENV disp[2];
    DRAWENV draw[2];
    POLY_G4 poly;
    DR_TPAGE tpage;
    s32 skipped;
    s32 buffer;
    u32 pad;
    u32 old_pad;
    s32 still_shown;
    s32 i;
    s32 steps;
    s32 level;
    s32 level_first, level_middle;
    s32 side;
    POLY_G4 *fade = &poly;

    __main();
    skipped = 0;
    ResetCallback();
    CdInit();
    PadInit(0);
    ResetGraph(0);
    SetGraphDebug(0);
    VSync(0);
    SetDispMask(0);
    sys_SetCdVolume(CD_VOLUME_MAX, 0, CD_VOLUME_MAX, 0);

    /* Load the title picture off screen. */
    CdControl(CdlSetloc, (u8 *)PTR(args->still_loc), NULL);
    CdRead(STILL_SECTORS, (u32 *)PTR(args->work), CdlModeSpeed);
    while (CdReadSync(1, NULL) > 0) {
        VSync(0);
    }
    LoadImage(
        &g_still_rect, (u32 *)((u8 *)PTR(args->work) + TIM_PIXELS_NO_CLUT));
    DrawSync(0);

    /* The movie is shown in 24-bit colour. */
    SetDefDispEnv(&disp[0], 0, 0, SCREEN_WIDTH, SCREEN_HEIGHT);
    SetDefDispEnv(&disp[1], 0, SCREEN_HEIGHT, SCREEN_WIDTH, SCREEN_HEIGHT);
    disp[1].isrgb24 = 1;
    disp[0].isrgb24 = 1;
    PutDispEnv(&disp[0]);
    ClearImage(&g_clear_rect, 0, 0, 0);
    DrawSync(0);
    SetDispMask(1);
    VSync(4);

    movie_SetArgs(PTR(args->movie_file), args->end_frame, PTR(args->ring),
                  PTR(args->work), args->still_frame);
    movie_Start();
    buffer = 0;
    pad = 0;
    still_shown = 0;
    i = 0;
    for (;;) {
        buffer = !buffer;
        if (still_shown) {
            buffer = 0;
        }
        old_pad = pad;
        pad = PadRead(0);
        if (pad & ~old_pad & PAD_SKIP) {
            skipped = 1;
            break;
        }
        if (!still_shown && g_movie_still_reached) {
            /* From here the display stays on the title picture, in 16-bit
             * colour; the movie goes on playing unseen for its sound. */
            still_shown = 1;
            SetDefDispEnv(&disp[0], STILL_X, 0, SCREEN_WIDTH, SCREEN_HEIGHT);
            disp[0].isrgb24 = 0;
            PutDispEnv(&disp[0]);
            buffer = 0;
        }
        if (!movie_PlayFrame(buffer)) {
            if (movie_EndedNormally()) {
                sys_SetCdVolume(0, 0, 0, 0);
                i = STILL_HOLD_FRAMES;
            } else {
                skipped = 1;
            }
            break;
        }
        DrawSync(0);
        VSync(0);
        PutDispEnv(&disp[buffer]);
    }

    /* Hold the title picture. */
    if (!skipped) {
        while (--i != 0) {
            old_pad = pad;
            pad = PadRead(0);
            if (pad & ~old_pad & PAD_SKIP) {
                skipped = 1;
                break;
            }
            VSync(0);
        }
    }

    /* Fade it out: each frame copy it into the buffer not on show and draw a
     * grey rectangle over it that is subtracted from it, darker every frame. */
    if (!skipped) {
        SetDefDispEnv(&disp[0], 0, 0, SCREEN_WIDTH, SCREEN_HEIGHT);
        SetDefDispEnv(&disp[1], 0, SCREEN_HEIGHT, SCREEN_WIDTH, SCREEN_HEIGHT);
        disp[1].isrgb24 = 0;
        disp[0].isrgb24 = 0;
        SetDefDrawEnv(&draw[0], 0, 0, SCREEN_WIDTH, SCREEN_HEIGHT);
        SetDefDrawEnv(&draw[1], 0, SCREEN_HEIGHT, SCREEN_WIDTH, SCREEN_HEIGHT);
        draw[1].isbg = 0;
        draw[0].isbg = 0;
        steps = FADE_STEPS;
        for (i = 0; i < steps; i++) {
            side = i & 1;
            PutDrawEnv(&draw[side]);
            MoveImage(&g_still_rect, 0, side * SCREEN_HEIGHT);
            SetDrawTPage(&tpage, 0, 1, TPAGE_SUBTRACT);
            DrawPrim(&tpage);
            SetPolyG4(fade);
            SetSemiTrans(fade, 1);
            level = (i << 8) / steps; /* from none towards all of a byte */
            setXY4(fade, 0, 0, SCREEN_WIDTH, 0, 0, SCREEN_HEIGHT, SCREEN_WIDTH,
                   SCREEN_HEIGHT);
            /* The same level at all four corners. */
            level_middle = level;
            level_first = level;
            clampToByte(level_first);
            clampToByte(level_middle);
            clampToByte(level);
            setRGB0(fade, level_first, level_first, level_first);
            setRGB1(fade, level_middle, level_middle, level_middle);
            setRGB2(fade, level_middle, level_middle, level_middle);
            setRGB3(fade, level, level, level);
            DrawPrim(fade);
            DrawSync(0);
            VSync(0);
            PutDispEnv(&disp[i & 1]);
        }
    }

    VSync(0);
    SetDispMask(0);
    ClearImage(&g_clear_rect, 0, 0, 0);
    DrawSync(0);
    PadStop();
    ResetGraph(3);
    StopCallback();
}
