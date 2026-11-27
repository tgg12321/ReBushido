#ifndef PORT_THEATRE_H
#define PORT_THEATRE_H

/* The replay theatre and photo mode (ROADMAP 29; docs/PORT.md, "The
 * replay theatre and photo mode"; src/port/theatre.c): the picture of a
 * frame drawn again from another camera, without the HUD, at another
 * time of day, while the game computes exactly what it always did.
 *
 * How: the picture of a frame is what its step drew. The states at the
 * last boundaries are kept (a ring of snapshots, port_state.h), and with
 * them the inputs the game took there. A "look" puts the state of the
 * boundary before back, runs that step again as a frame run ahead is run
 * (drawn, never heard, the window given no turn, step.c), with the game's
 * camera replaced in scene_Begin by the free one and the ordering table's
 * 2D layer cut where the HUD is hidden, and then puts the state of now
 * back. The window shows what the look drew, and the step itself runs
 * unseen (g_port_unseen), as after frames run ahead. So nothing of the
 * free camera, the hidden HUD or the time of day ever reaches the game's
 * state: a replay holds every hash, the rollback's self-test passes, and
 * a fight paused for a photo goes on as it would have.
 *
 * Nothing here is the game's state (config/port_state.txt: outside). */

#include "common.h"

struct PortInput;

/* A camera as the game's scene_Begin takes one (camera.h, CameraView):
 * the point looked at, the angles it is looked at along (pitch, yaw,
 * roll, 4096 a turn) and the distance of the eye back from it. */
typedef struct {
    s32 focus[3];
    s16 angles[3];
    s32 distance;
} PortTheatreCamera;

/* What a look draws differently from the game's own picture. */
typedef struct {
    s32 free;     /* the camera below in the game's place */
    s32 hide_hud; /* the 2D layer of the fight's frame left out */
    s32 time;     /* a time of day (PORT_TIME_...) for the picture, or -1 */
    PortTheatreCamera camera;
} PortTheatreView;

/* The view the looks draw with. */
extern void port_TheatreSetView(const PortTheatreView *view);
extern void port_TheatreGetView(PortTheatreView *view);
/* Whether every boundary's picture is a look (the window's, while a
 * replay plays or photo mode is on; never online): a look with the game's
 * own view draws the picture the step itself would have. */
extern void port_TheatreSetOn(s32 on);
extern s32 port_TheatreIsOn(void);

/* The camera the game's own last frame was drawn from (not a look's):
 * FALSE before a scene was begun. The free camera starts from it. */
extern s32 port_TheatreGameCamera(PortTheatreCamera *camera);

/* ---- the game's (RB_PORT in src/main/) ---- */

/* scene_Begin: the camera it is about to place, replaced by the free one
 * in a look; the game's own noted otherwise. */
extern void port_TheatreScene(s32 *focus, s16 *angles, s32 *distance);
/* The fight's frame is about to be drawn from `ot` (main.c): in a look
 * that hides the HUD, its first PORT_THEATRE_HUD_SLOTS entries, where the
 * 2D layer is linked, are left out. */
#define PORT_THEATRE_HUD_SLOTS 4
extern void port_TheatreCutHud(u32 *ot);
/* The time of day the picture lights the stage at in a look, or -1 for
 * the fight's own (port_StageTime). */
extern s32 port_TheatreTime(void);

/* ---- the host's ---- */

/* The steps a look runs again: the display a step leaves is the frame the
 * step before it drew (the game draws one buffer while the other is
 * shown), so the picture of a boundary is the two steps before it. A
 * seek keeps the states of so many boundaries before where it stops
 * (replay.c). */
#define PORT_THEATRE_LOOK_STEPS 2

/* The input the game takes as number `index` (pad.c, after every other
 * hook): noted, and in a look the one noted for that index put in its
 * place, so that the step run again is the step that ran. */
extern void port_TheatreInput(struct PortInput *input, u32 index);
/* The state at this boundary kept, where looks are asked for: the ring
 * the looks run from (a boundary's state once, however often asked). */
extern void port_TheatreKeep(void);
/* A look at this boundary (before the step): TRUE where it drew the
 * frame on show again, FALSE where it could not (no state of the boundary
 * before it, or the step was a load's). The boundary's state is kept
 * first. */
extern s32 port_TheatreLook(void);
/* Whether the boundary before this one is kept, so that a look can be run
 * now. */
extern s32 port_TheatreCanLook(void);
/* Whether the picture on show is the last look's (g_port_unseen as the
 * step after it has it). */
extern s32 port_TheatreShowsLook(void);

/* What the theatre counts, for a test and the log. */
typedef struct {
    u32 looks;     /* drawn */
    u32 failed;    /* asked for and not drawn */
    u32 kept;      /* states kept */
    u32 forgotten; /* the ring emptied by a load not its own */
} PortTheatreCounts;

extern void port_TheatreGetCounts(PortTheatreCounts *counts);

#endif /* PORT_THEATRE_H */
