#ifndef PORT_GL_STAGE_H
#define PORT_GL_STAGE_H

/* The stage's ground as one static mesh, for the remastered picture
 * alone (port_remaster.h, "the stage drawn as one mesh"; docs/PORT.md,
 * "Remastered", "The ground as one mesh").
 *
 * The game goes on building its 32 by 32 grid of tiles every frame,
 * cutting them four and sixteen ways near the camera and emitting every
 * piece as a primitive. Nothing of that changes. What changes is what
 * the card draws: the primitives that came from a stage tile are left
 * out, and in their place the whole of the ground -- every cell, at the
 * finest detail, with its textures, its colour tables and the colours the
 * coprocessor lit its faces with -- is drawn from vertex buffers built
 * once when the stage loaded, with the camera's own transform.
 *
 * So nothing of the ground comes and goes as the camera turns, its
 * detail never steps, and a picture drawn between two of the game's
 * frames has only to move the camera. */

#include "common.h"
#include "port_remaster.h"

/* The functions are loaded and the mesh's shaders made; it draws nothing
 * until a stage is drawn. Called where the backend starts (gl_draw.c).
 * Returns why it could not, or NULL. */
extern const char *port_GlStageStart(void *(*getproc)(const char *name));

/* Whether the ground is drawn as one mesh: the setting, and a backend
 * that can. 0 off; 1 on, which is the stages the mesh is measured good
 * on; 2 on for every stage whatever the measurements say (gl.h,
 * `is_stage_mesh`). */
extern void port_GlStageSetOn(s32 on);
/* Whether what stands on the ground -- the stage's pieces: railings,
 * fences, walls, roofs -- is drawn from the mesh as well
 * (`stage_props_mesh`): 0 off, 1 on the stages it is measured good on, 2
 * on every stage. Nothing comes of it where the ground is not the
 * mesh's. */
extern void port_GlStageSetPropsOn(s32 on);
/* For the prover alone: the pieces taken from the game and drawn not at
 * all, the picture of the stage without them, which says which pixels
 * are theirs (tools/port_props_prove.py). */
#define PORT_GL_PROPS_HIDE 3

/* The pieces the mesh drew in the picture being finished, into the
 * shadows' mask's alpha as the game's own faces are marked there
 * (gl_shadow.c, port_GlDrawOccluders): where a piece is in front of
 * the ground, the ground's shadow does not darken it. */
extern void port_GlStageDrawOccluders(u32 framebuffer);
extern s32 port_GlStageIsOn(void);

/* Which path the stage's ground takes, once each time the stage or the
 * setting changes, for the run's log; NULL when there is nothing new.
 * The host asks after each frame the card drew (gl_main.c,
 * sdl_window.c). */
extern const char *port_GlStageNews(void);

/* A frame's primitives are about to come (gl_draw.c, the backend's
 * `begin`). */
extern void port_GlStageNewFrame(void);

/* Whether the mesh has been drawn in the frame being built, kept aside
 * and put back. A picture drawn between two of the game's frames draws
 * the mesh again and must not leave it marked as drawn: the game's own
 * frame has not been painted yet when that picture is made (its
 * primitives come past the vertical blank), and it needs its ground
 * (gl_tween.c; the same keeping-aside as `port_GlSaveCounts`). */
extern s32 port_GlStageSaveFrame(void);
extern void port_GlStageLoadFrame(s32 state);

/* Whether this primitive is the mesh's business and must not be drawn as
 * it is: a primitive the renderer made from a stage tile, where the mesh
 * is on and holds this stage. The first such primitive of a frame is
 * where the mesh itself is drawn, so that it takes the tiles' place in
 * the order the game paints in; `prim` gives it the paint of the frame
 * (the drawing area, the texture window, the drawing offset). */
extern s32 port_GlStageTakes(const PortRemasterPrim *prim);

/* Whether it would take this primitive, without drawing anything: the
 * pictures between two of the game's frames ask so that a tile's
 * primitive is never written down or paired with another frame's
 * (gl_tween.c). */
extern s32 port_GlStageWouldTake(const PortRemasterPrim *prim);

/* The view the mesh is drawn with until this is called with NULL: a
 * picture between two of the game's frames draws the ground with the
 * camera between the two, which is the whole of what moves in it. */
extern void port_GlStageSetView(const PortStageView *view);
/* The view as the side channel last had it, for a caller that keeps one
 * frame's beside another's (gl_tween.c). */
extern void port_GlStageGetView(PortStageView *view);
/* The view the ground is being drawn with now: a picture's own between
 * two frames (`*is_between` TRUE), or the renderer's. For what is drawn
 * with the ground's camera (gl_sky.c, gl_see.c). */
extern const PortStageView *port_GlStageViewNow(s32 *is_between);
/* One view between two others, `phase` of `one` (256ths of the way from
 * `one` to `two`). Pure arithmetic. */
extern void port_GlStageMixView(PortStageView *out, const PortStageView *one,
                                const PortStageView *two, s32 phase, s32 whole);

/* What the mesh is: the faces it holds and the vertices they came to, 0
 * where there is none. For a run's report. */
extern s32 port_GlStageFaces(void);
extern s32 port_GlStageVertices(void);
/* Vertices drawn and draws made for the mesh in the last frame. */
extern u32 port_GlStageDrawnVertices(void);
extern u32 port_GlStageDraws(void);
/* Tiles the mesh holds, and faces of them the mesh could not take (a
 * group of a kind it does not know). */
extern s32 port_GlStageTiles(void);
extern s32 port_GlStageMissed(void);

#endif /* PORT_GL_STAGE_H */
