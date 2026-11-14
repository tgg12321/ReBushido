#ifndef SCENE_H
#define SCENE_H

/* A frame of the 3D scene (scene_frame.c). Whatever shows the scene, the
 * fight or a replay or a menu over a turning view of the stage, begins the
 * frame with scene_Begin, queues its nodes, and ends it with scene_End,
 * which queues the stage and has the renderer draw the queue. */

#include "common.h"
#include <psxsdk/libgte.h>
#include "stage.h"

/* The engine's switches, one object: what the textures, the renderer and the
 * stage each keep between frames. sys_InitEngineState sets them. */
typedef struct {
    s16 unused_flag;       /* +0x00 math.h */
    s16 grayscale;         /* +0x02 tex.h */
    s16 clip_near;         /* +0x04 render.h */
    StageFarView far_view; /* +0x06 stage.h */
    /* Read by the renderer's assembly: 2 in the stage's far view and 1
     * otherwise, and whether the view is the first-person one. */
    s16 far_shift; /* +0x08 */
    s16 unk0A;     /* +0x0A set to 1 when the engine starts; nothing reads it */
    s16 first_person; /* +0x0C */
    s16 unk0E;        /* +0x0E nothing reads or writes it */
} SceneSettings;      /* 0x10 bytes */

extern SceneSettings g_scene_settings;

/* Empties the draw queue, takes this frame's ordering table, and places the
 * camera `eye_distance` back from `focus_point`, looking at it along
 * `camera_angles`. With `focus_point` NULL the camera is left where it is. */
extern void scene_Begin(
    Vec3 *focus_point, SVECTOR *camera_angles, s32 eye_distance);
/* Draws what was queued; with bit 0 of `draw_stage` set, the stage and its
 * fog as well. */
extern void scene_End(s32 draw_stage);
/* Queues the stage's visible cells and its fog. */
extern void scene_DrawStage(s32 eye_distance);

/* The camera's matrix squashed to half height, as the 640x240 picture is:
 * what the fight uses to find where a point of the world is on the screen.
 * scene_Begin works it out. */
extern void scene_SetSquashedView(void);
extern MATRIX *scene_GetSquashedView(void);

/* How many scenes have been begun. */
extern u16 g_scene_count;

#endif /* SCENE_H */
