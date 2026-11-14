#ifndef CAMERA_H
#define CAMERA_H

/* The camera (camera.c): the fight's side view of both fighters, the
 * first-person view through one fighter's eyes, the views of the winner
 * after a fight, and the replay's two views. */

#include "common.h"
#include "replay.h"

/* A view: the point looked at, and the angles and distance it is looked at
 * from. scene_Begin places the scene's camera from one. */
typedef struct CameraView {
    Vec3 focus;       /* +0x00 */
    s32 pad0C;        /* +0x0C a point's fourth word; not touched */
    SVECTOR angles;   /* +0x10 pitch, yaw, roll */
    s32 eye_distance; /* +0x18 from the focus back to the eye */
    /* The first-person view: half a turn when the fighter looked through
     * spins round, dying away as the view catches up. The tiny model is
     * drawn turned by it. */
    s16 turn_lag;      /* +0x1C */
    u8 far_view;       /* +0x1E the side view has pulled back to the far view */
    u8 replay_started; /* +0x1F the replay has placed this view */
    Vec3 eye;          /* +0x20 where the eye was when last drawn from */
    s32 pad2C;         /* +0x2C not touched */
    /* For each fighter, the stage cell whose ground hides it from the eye
     * (column, unused, row, unused), or CAMERA_NO_CELL: the scene draws
     * those cells opened. */
    s16 open_cells[2][4]; /* +0x30 */
    /* camera_InitFirstPerson clears it in the second view; never read */
    u8 unk40; /* +0x40 */
} CameraView;

#define CAMERA_NO_CELL 100 /* a column or row outside the grid */

/* The main view: the fight's side view, the views of the winner, the
 * turning view behind the fight's menus, the replay's first view. */
extern CameraView g_camera_main;
/* The second view: the first-person view, and the replay's second view. */
extern CameraView g_camera_second;
/* The view the scene was last drawn from. Sounds are heard from its eye. */
extern CameraView *g_camera_current;

/* Which view a fight with the first-person setting on is drawn from. */
enum CameraMode {
    CAMERA_FIRST_PERSON = 0,
    /* the side view, while the two fighters are locked together in one
     * move */
    CAMERA_SIDE = 1,
    /* a fighter was defeated in that move: the side view to the end */
    CAMERA_SIDE_HELD = 2,
};

extern u8 g_camera_mode;

/* The first-person view's zoom: how far the player has moved the eye back
 * from the fighter's head with L2 and R1 / R2, in sixteenths of a unit, and
 * the offset made from it each frame. */
extern s16 g_camera_zoom;
extern u8 g_camera_zoom_used; /* set once the buttons have been used */
/* What is added to a view before the fight's scene is drawn from it. It has
 * a whole view's room; only the focus, the angles and the distance are
 * read, and only the distance is ever not 0. */
extern CameraView g_camera_offset;
/* Reads the zoom's buttons and strips them, and L2 of both pads, from the
 * held buttons (pad 1 in the low half, pad 2 in the high). */
extern void camera_ReadPad(s32 *held);

/* Frames left of the first-person view easing round after its fighter
 * turned sharply. */
extern s16 g_camera_turn_frames;

/* Where a view's eye is (x, y, z). */
extern void camera_CalcEye(CameraView *view, s32 *eye);

/* The side view (g_camera_main): placed when a round starts, then moved
 * every frame of the fight from the fighters' camera points. */
extern void camera_InitSideView(
    Fighter *first_fighter, Fighter *second_fighter);
extern void camera_UpdateSideView(
    Vec3 *first_point, Vec3 *second_point, Fighter *first_fighter,
    Fighter *second_fighter);

/* The first-person view (g_camera_second), through the eyes of fighter
 * g_fight_viewer. camera_EaseTurn is told when a move turns a fighter at
 * once. */
extern void camera_InitFirstPerson(Fighter *fighter, Fighter *second_fighter);
extern void camera_UpdateFirstPerson(Fighter *viewer);
extern void camera_EaseTurn(s32 fighter_index, s32 turn);

/* After the last blow the main view swings round to the winner (`progress`
 * from 0 to ONE); after the fight it looks at a fighter from `yaw`. */
extern void camera_SwingToFighter(Fighter *fighter, s32 progress);
extern void camera_LookAtFighter(Fighter *fighter, s32 yaw);

/* The replay: both views follow one fighter's recorded frames, `progress`
 * being how far through the replay it is (of ONE);
 * camera_BeginReplayScene draws the middle third from the second view and
 * the rest from the main one. */
extern void camera_StartReplay(void);
extern void camera_UpdateReplayMain(
    ReplayFighterFrame *followed, ReplayFighterFrame *other, s32 progress);
extern void camera_UpdateReplaySecond(
    ReplayFighterFrame *followed, ReplayFighterFrame *other, s32 progress);
extern void camera_BeginReplayScene(s32 progress);

#endif /* CAMERA_H */
