#ifndef DISP_H
#define DISP_H

/* The display: the two frame buffers the game draws to in turn, and the
 * pictures shown while something loads (main.c). */

#include "common.h"
#include <psxsdk/libgpu.h>

/* The game runs in 640x240. The two buffers are one above the other in video
 * memory. */
#define DISP_WIDTH 640
#define DISP_HEIGHT 240
/* Entries in a frame's ordering table. Entry 0 is nearest the viewer; drawing
 * starts from the last. */
#define DISP_OT_SIZE 0x1008

/* One of the two display buffers: where to draw, what to show, and the
 * frame's ordering table. */
typedef struct {
    DRAWENV draw;
    DISPENV disp;
    u32 ot[DISP_OT_SIZE];
} GpuDb;

/* What disp_DrawLoadingScreen puts on screen while the fight loads. */
enum LoadingScreen {
    LOADING_SCREEN_PLAIN = 1,
    LOADING_SCREEN_NEXT_OPPONENT = 2,
    /* the team battle's scores so far */
    LOADING_SCREEN_TEAM_SCORES = 10,
    /* story mode's stage card, its lines appearing one by one */
    LOADING_SCREEN_STAGE_CARD = 20,
    LOADING_SCREEN_NONE = 0xFF,
};

/* The buffers, used on alternate frames: g_disp_db[g_disp_frame_count & 1]. */
extern GpuDb g_disp_db[2];
/* Frames shown so far. */
extern s32 g_disp_frame_count;
/* This frame's ordering table (entry n is g_disp_ot + n). */
extern u32 *g_disp_ot;
/* Where the next GPU primitive of this frame is built. */
extern u32 *g_disp_prim_cursor;
/* The two primitive buffers, one per frame parity: the "common prim" of
 * the game's message, as the effects' own are the "eff prim". */
extern u32 g_disp_prim_buffers[2];
/* How large each of those is. The console's two are SYS_PRIM_BUFFER_SIZE;
 * where the port draws more of the stage than the console does, its own
 * larger pair takes their place (port_remaster.h). */
extern u32 g_disp_prim_buffer_size;
/* enum LoadingScreen */
extern u8 g_disp_loading_screen;
/* Set to make the loading screen's noise streaks come in bursts. */
extern u8 g_disp_loading_noise_burst;

/* Whether drawing clears the buffer first, and to which colour. */
extern void disp_SetBackground(s32 clear, s32 red, s32 green, s32 blue);
/* Abandons whatever the GPU is still drawing. */
extern void disp_Reset(void);
/* Turns the picture off and clears both buffers to black. */
extern void disp_Blank(void);
/* Turns the picture on again. */
extern void disp_Show(void);
/* The "black and white" option. */
extern u32 disp_IsGrayscale(void);
/* Draws the current loading screen straight to the buffer on show. Called
 * between the steps of a long load. */
extern void disp_DrawLoadingScreen(void);

#endif /* DISP_H */
