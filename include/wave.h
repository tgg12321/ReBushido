#ifndef WAVE_H
#define WAVE_H

/* The surf of the Kimigaura coast stage: a sheet of quads whose leading edge
 * runs up and down the beach (wave.c). */

#include "common.h"
#include <psxsdk/libgte.h>

#define WAVE_ROWS 8     /* of quads */
#define WAVE_COLUMNS 16 /* of quads */
#define WAVE_ROW_SPACING 2000
#define WAVE_BUFFER_SIZE 0xD00

/* The sheet's quads are kept at the end of the stage's arena block. */
extern s32 wave_GetBufferSize(void);
extern void wave_SetBuffer(s32 buffer_addr);
extern void wave_RebaseBuffer(s32 delta);

/* Builds the quads. Returns the bytes used. */
extern s32 wave_Init(void);
/* Moves the surf and queues it for drawing. Call once a frame. */
extern void wave_Update(void);
/* The same two as the stage's hooks. */
extern void wave_InitCallback(void);
extern void wave_UpdateCallback(void);
/* Called by the renderer for the queued node. */
extern void wave_Draw(void);

#endif /* WAVE_H */
