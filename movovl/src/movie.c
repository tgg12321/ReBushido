/* The movie player: streams an MDEC movie from the disc and decodes it a frame
 * at a time. Built on Sony's streaming sample: the CD fills a ring buffer, each
 * frame's bitstream is expanded into one of two run-level buffers, and the MDEC
 * turns that into 24-bit pixels a slice at a time, in one of two frame buffers
 * in main memory. While one frame decodes, the one before it is copied to the
 * display. */
#include "common.h"
#include "sdk.h"
#include "movie.h"
#include <psxsdk/libetc.h>
#include <psxsdk/libgpu.h>

/* Polls before giving up on the stream or the decoder. */
#define MOVIE_WAIT 0x10000000

/* The MDEC decodes in blocks of 16 pixels a side. A slice is one block wide:
 * 24 words of video memory in 24-bit colour. */
#define MACROBLOCK 16
#define SLICE_W_16BIT MACROBLOCK
#define SLICE_W_24BIT 24
#define SLICES_PER_FRAME (SCREEN_WIDTH / MACROBLOCK)

/* The stream is taken from its first frame and has no last one: the player
 * stops it. */
#define STREAM_FIRST_FRAME 1
#define STREAM_NO_END 0xFFFFFFFF

/* Room for the drive's answer to a command. */
#define CD_RESULT_SIZE 8
/* Vertical blanks between asking a drive that has failed to spin up. */
#define DRIVE_RETRY_VBLANKS 4

enum MovieState {
    MOVIE_ENDED = 0, /* reached the end frame */
    MOVIE_PLAYING = 1,
    MOVIE_ERROR = 2, /* the disc or the stream failed */
};

/* MDEC modes for DecDCTin. */
enum {
    DCT_16BIT = 2,
    DCT_24BIT = 3,
};

/* The decoder's state. Widths are in words of video memory, so half as much
 * again as the pixel count in 24-bit colour. */
typedef struct {
    u8 isdone; /* the frame being decoded is complete */
    u8 is24bit;
    u8 vlcid;       /* which run-level buffer holds the frame being decoded */
    u8 imgid;       /* which frame buffer it is decoded into */
    u8 rectid;      /* which of `rect` the frame fills */
    u8 first_slice; /* no slice of this frame has come back yet */
    u8 state;       /* MovieState */
    s16 width;      /* the movie's frame size in pixels, from the stream */
    s16 height;
    RECT rect[2];
    RECT slice; /* the slice the MDEC is working on */
    s32 unk24;  /* no code reads or writes it */
} DecEnv;

/* These two are not `static`: the linker placed them among the libraries'
 * variables, by their names. */
DecEnv g_dec_storage;
CdlFILE g_movie_file;

static DecEnv *g_dec = &g_dec_storage;

static MovieRing *g_ring;
static u32 g_still_frame;
static s32 g_ended_normally;
static u32 g_end_frame;
static MovieWork *g_work;

/* Called by the MDEC when a slice is decoded: asks for the next one, or marks
 * the frame complete. */
static void movie_SliceDone(void) {
    s32 remainder;

    /* A stream sector arrived while this was running; take it now. */
    if (StCdIntrFlag) {
        StCdInterrupt();
        StCdIntrFlag = 0;
    }

    /* The first slice is the odd-sized one when the frame is not a whole
     * number of slices wide. */
    if (g_dec->first_slice &&
        (remainder = g_dec->rect[g_dec->rectid].w % g_dec->slice.w) != 0) {
        g_dec->slice.x += remainder;
        g_dec->first_slice = 0;
    } else {
        g_dec->slice.x += g_dec->slice.w;
    }

    if (g_dec->slice.x <
        g_dec->rect[g_dec->rectid].x + g_dec->rect[g_dec->rectid].w) {
        DecDCTout(
            (u32 *)&g_work->img[g_dec->imgid][g_dec->slice.x * g_dec->slice.h],
            g_dec->slice.w * g_dec->slice.h / 2);
    } else {
        g_dec->isdone = 1;
        g_dec->first_slice = 1;
        g_dec->rectid = g_dec->rectid == 0;
        g_dec->slice.x = g_dec->rect[g_dec->rectid].x;
        g_dec->slice.y = g_dec->rect[g_dec->rectid].y;
    }
}

/* Takes what the game passed: the movie's place on the disc, the frame to stop
 * at, the two buffers, and the frame from which the still picture is shown. */
void movie_SetArgs(CdlFILE *file, u32 end_frame, MovieRing *ring,
                   MovieWork *work, u32 still_frame) {
    g_movie_file = *file;
    g_end_frame = end_frame;
    g_ring = ring;
    g_work = work;
    g_still_frame = still_frame;
    printf("size of DECENV_R = 0x%08X\n", sizeof(MovieRing));
    printf("size of DECENV_B = 0x%08X\n", sizeof(MovieWork));
}

/* The decoder as it is before the first frame: 24-bit colour, both frames
 * at the corner, a frame of the screen's size until the stream says. */
static void movie_InitDecEnv(void) {
    g_dec->vlcid = 0;
    g_dec->imgid = 0;
    g_dec->rect[0].x = 0;
    g_dec->rect[0].y = 0;
    g_dec->rect[1].x = 0;
    g_dec->rect[1].y = 0;
    g_dec->rectid = 0;
    g_dec->is24bit = 1;
    g_dec->slice.x = 0;
    g_dec->slice.y = 0;
    g_dec->slice.w = g_dec->is24bit ? SLICE_W_24BIT : SLICE_W_16BIT;
    g_dec->isdone = 0;
    g_dec->width = SCREEN_WIDTH;
    g_dec->height = SCREEN_HEIGHT;
}

/* Resets the MDEC and starts the CD streaming from `loc`. */
static s32 movie_StartStream(CdlLOC *loc, void (*slice_done)(void)) {
    DecDCTReset(0);
    DecDCToutCallback(slice_done);
    StSetRing(g_ring->words, RING_SECTORS);
    StSetStream(g_dec->is24bit, STREAM_FIRST_FRAME, STREAM_NO_END, NULL, NULL);
    if (!CdControlB(CdlSetloc, (u8 *)loc, NULL)) {
        return 0;
    }
    return CdRead2(CdlModeStream | CdlModeSpeed | CdlModeRT);
}

/* Waits for the next frame's bitstream in the ring buffer. NULL on timeout.
 * Also where the frame number is checked against the end and still frames. */
static u32 *movie_NextFrame(void) {
    u32 *addr;
    StHEADER *sector;
    s32 count = MOVIE_WAIT;

    while (StGetNext(&addr, (u32 **)&sector)) {
        if (--count == 0) {
            return NULL;
        }
    }

    if (sector->frameCount >= g_end_frame) {
        g_dec->state = MOVIE_ENDED;
    } else if (sector->frameCount >= g_still_frame) {
        g_movie_still_reached = 1;
    }

    g_dec->width = sector->width;
    g_dec->height = sector->height;
    g_dec->rect[0].w = g_dec->rect[1].w =
        g_dec->is24bit ? g_dec->width * 3 / 2 : g_dec->width;
    g_dec->rect[0].h = g_dec->rect[1].h = g_dec->height;
    g_dec->slice.h = g_dec->height;
    return addr;
}

/* Expands the next frame's bitstream into the run-level buffer not in use. */
static void movie_DecodeNextVlc(void) {
    u32 *next;

    next = movie_NextFrame();
    if (next == NULL) {
        printf("strNext is NULL\n");
        g_dec->state = MOVIE_ERROR;
        return;
    }
    g_dec->vlcid = g_dec->vlcid == 0;
    DecDCTvlc(next, g_work->vlc[g_dec->vlcid]);
    StFreeRing(next);
}

/* Clears the buffers and starts the movie. 0 if the disc would not start. */
s32 movie_Start(void) {
    s32 i, j;

    for (i = 0; i < RING_WORDS; i++) {
        g_ring->words[i] = 0;
    }
    for (j = 0; j < 2; j++) {
        for (i = 0; i < VLC_WORDS; i++) {
            g_work->vlc[j][i] = 0;
        }
        for (i = 0; i < FRAME_WORDS; i++) {
            g_work->img[j][i] = 0;
        }
    }

    g_dec->first_slice = 1;
    movie_InitDecEnv();
    if (!movie_StartStream(&g_movie_file.pos, movie_SliceDone)) {
        DecDCToutCallback(NULL);
        StUnSetRing();
        StClearRing();
        return 0;
    }
    movie_DecodeNextVlc();
    g_dec->state = MOVIE_PLAYING;
    return 1;
}

/* Waits for the MDEC to finish the frame. On timeout it turns to the other
 * frame's rectangle and goes on waiting, as nothing marks the frame done:
 * the original's behaviour. */
static void movie_WaitFrame(void) {
    s32 count = MOVIE_WAIT;

    while (!g_dec->isdone) {
        if (--count == 0) {
            g_dec->rectid = g_dec->rectid == 0;
            g_dec->slice.x = g_dec->rect[g_dec->rectid].x;
            g_dec->slice.y = g_dec->rect[g_dec->rectid].y;
        }
    }
    g_dec->isdone = 0;
}

/* Copies the finished frame (the one not being decoded into) to display
 * buffer `buffer`, a slice at a time. */
static void movie_ShowFrame(s32 buffer) {
    RECT rect;
    u16 *img;
    u16 *strip;
    s32 x;
    s32 i;

    img = g_work->img[0];
    if (g_dec->imgid == 0) {
        img += FRAME_WORDS;
    }
    rect.w = SLICE_W_24BIT;
    rect.h = SCREEN_HEIGHT;
    if (buffer) {
        rect.y = SCREEN_HEIGHT;
    } else {
        rect.y = 0;
    }
    for (x = 0, i = 0, strip = img; i < SLICES_PER_FRAME; i++,
        strip += SLICE_W_24BIT * SCREEN_HEIGHT, x += SLICE_W_24BIT) {
        rect.x = x;
        LoadImage(&rect, (u32 *)strip);
    }
}

/* One frame: starts the MDEC on the frame whose run-level data is ready,
 * shows the frame before it, and meanwhile expands the one after. 0 on a disc
 * error. */
static s32 movie_DecodeFrame(s32 buffer) {
    u8 result[CD_RESULT_SIZE];
    u8 unused[CD_RESULT_SIZE]; /* nothing uses it */

    if (CdSync(1, result) == CdlDiskError) {
        printf("Cd error!!!\n");
        g_dec->state = MOVIE_ERROR;
        return 0;
    }
    DecDCTin(g_work->vlc[g_dec->vlcid], g_dec->is24bit ? DCT_24BIT : DCT_16BIT);
    /* The first slice, its height rounded up to whole macroblocks. */
    DecDCTout(
        (u32 *)g_work->img[g_dec->imgid],
        ((g_dec->slice.w << 4) * ((g_dec->slice.h - 1) / MACROBLOCK + 1)) >> 1);
    movie_ShowFrame(buffer);
    movie_DecodeNextVlc();
    movie_WaitFrame();
    g_dec->imgid = g_dec->imgid == 0;
    return 1;
}

/* Plays one frame into display buffer `buffer`. Returns 1 while the movie is
 * playing; when it returns 0 the movie is over and has been shut down, and
 * movie_EndedNormally says how. */
s32 movie_PlayFrame(s32 buffer) {
    u8 result[CD_RESULT_SIZE];

    if (movie_DecodeFrame(buffer) && g_dec->state == MOVIE_PLAYING) {
        return 1;
    }
    DecDCToutCallback(NULL);
    StUnSetRing();
    StClearRing();
    CdControlF(CdlPause, NULL);
    if (g_dec->state == MOVIE_ERROR) {
        /* Silence the disc and wait until the drive answers again. */
        sys_SetCdVolume(0, 0, 0, 0);
        while (CdReady(1, result) == CdlDiskError) {
            CdControlF(CdlStandby, NULL);
            VSync(DRIVE_RETRY_VBLANKS);
        }
        g_ended_normally = 0;
        return 0;
    }
    g_ended_normally = 1;
    return 0;
}

/* After movie_PlayFrame has returned 0: 1 if the movie reached its end frame,
 * 0 if it stopped on an error. */
s32 movie_EndedNormally(void) { return g_ended_normally; }
