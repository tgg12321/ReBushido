#ifndef MOVIE_H
#define MOVIE_H

/* The movie player (movie.c) and what main.c shares with it. */

#include "common.h"
#include "sdk.h"

/* The movie's picture, and a sector of the disc in bytes. */
#define SCREEN_WIDTH 320
#define SCREEN_HEIGHT 240
#define SECTOR_SIZE 2048

#define RING_SECTORS 32
#define RING_WORDS (RING_SECTORS * SECTOR_SIZE / 4)
#define VLC_WORDS 0xA000
/* one frame in 24-bit colour */
#define FRAME_WORDS (SCREEN_WIDTH * SCREEN_HEIGHT * 3 / 2)

/* The two buffers the game lends the player. Their sizes are what the player
 * prints as "DECENV_R" and "DECENV_B". */

/* Where the CD puts stream sectors until they are used. */
typedef struct {
    u32 words[RING_WORDS];
} MovieRing;

/* Two run-level buffers (a frame's bitstream, expanded) and two decoded
 * frames. Ends exactly where the overlay is loaded. */
typedef struct {
    u32 vlc[2][VLC_WORDS];
    u16 img[2][FRAME_WORDS];
} MovieWork;

extern s32 g_movie_still_reached;

extern void sys_SetCdVolume(
    u8 left, u8 left_to_right, u8 right, u8 right_to_left);

extern void movie_SetArgs(CdlFILE *file, u32 end_frame, MovieRing *ring,
                          MovieWork *work, u32 still_frame);
extern s32 movie_Start(void);
extern s32 movie_PlayFrame(s32 buffer);
extern s32 movie_EndedNormally(void);

#endif /* MOVIE_H */
