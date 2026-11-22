#ifndef PORT_GL_DRESSING_H
#define PORT_GL_DRESSING_H

/* A stage's dressing drawn in the remastered picture (ROADMAP 17;
 * docs/PORT.md, "The dressing"; docs/MODS.md, "Dressing"): grass, bushes,
 * reeds and flowers drawn by instancing, swayed by the stage's wind and
 * pushed aside by the fighters and their slashes; water that reflects the
 * sky and ripples where a fighter wades; dust kicked up by steps, leaves,
 * embers, snow, petals and fireflies; scenery meshes, standing alone or in
 * a piece's place within its silhouette. All of it from a stage's
 * dressing files (port_dressing.h), on a Remastered Classic stage
 * (port_StageIsDressed), with the ground drawn as one mesh (gl_stage.h),
 * never in the Original picture.
 *
 * Where it grows is worked out once a stage, from the ground's own mesh as
 * it is built: each place a block asks for is looked up on the ground
 * under it, kept where the ground is level enough and its texture's colour
 * is the kind of ground the block grows on, and coloured by that texture.
 * Nothing of the disc goes into a mod: a dressing file says where and how
 * much, the stage's own textures say the colour.
 *
 * It is lit as the ground is: the stage's lights (or the time of day's),
 * the sun's maps, the frame's lights, the tonemap, the fog. It reads where
 * the fighters and their weapons are from what the renderer already sees
 * (gl_see.h, port_fighter.h). Nothing here is the game's state, nor
 * changes a value the game computes. */

#include "common.h"

/* The view the ground's mesh is drawn with, as gl_stage.c works it out
 * for its own program, handed to the dressing so that both are one
 * projection and one depth. */
typedef struct {
    float view[9];     /* the mesh's u_view: a column after a column */
    float rotation[9]; /* the camera's turn, 1 long, a row after a row */
    float camera[3];
    float h;
    float screen[2];
    float target[2];
    float scale[2];
    float near_plane;
    float depth; /* what a depth in the tiles' scale is divided by */
    float far;   /* the tiles' scale: 1 << far_shift */
    s32 depth_shift;
    s32 fog_levels;
    s32 fog_slot[32];
    s32 cutoff;
} PortGlDressingView;

/* Loads the functions and makes the programs. Called where the backend
 * starts (gl_draw.c). Returns why it could not, or NULL. */
extern const char *port_GlDressingStart(void *(*getproc)(const char *name));

/* The window's `dressing` (on by default there; the prover's `--dressing`,
 * off by default). */
extern void port_GlDressingSetOn(s32 on);
extern s32 port_GlDressingIsOn(void);

/* The ground's mesh has been built (gl_stage.c, before its vertices go to
 * the card): its vertices, gl_stage.c's StageVertex of `stride` bytes,
 * `count` of them, those from `pieces_from` the pieces'. What grows where is
 * worked out from them when the stage's dressing is first asked for. */
extern void port_GlDressingNoteGround(
    const void *vertices, s32 stride, s32 count, s32 pieces_from);

/* Once a game frame (gl_draw.c, where the frame begins): the fighters'
 * steps and slashes, the dust they kick up and the ripples they make. */
extern void port_GlDressingFrame(void);

/* What stands on the ground and hides what is behind it -- the foliage and
 * the meshes -- into the colour buffer, the ground's slots and its depth,
 * while the mesh's slots are bound (gl_stage.c, between
 * port_GlGroundSlotsBegin and End). */
extern void port_GlDressingDrawSolid(const PortGlDressingView *view);
/* What mixes with what is behind it -- the water, the particles -- after
 * the ground, the picture's framebuffer bound again. */
extern void port_GlDressingDrawMixed(const PortGlDressingView *view);

/* The pieces a mesh stands in for this frame, a bit each (as the mesh's
 * u_pieces): the ground's mesh leaves them out. */
extern void port_GlDressingHiddenPieces(u32 *words, s32 count);

/* What the last picture drew, for the run's log and the bench: things,
 * triangles and draws. */
extern u32 port_GlDressingThings(void);
extern u32 port_GlDressingTriangles(void);
extern u32 port_GlDressingDraws(void);
/* A line for the run's log each time a stage's dressing is grown, or NULL
 * when there is nothing new. */
extern const char *port_GlDressingNews(void);

#endif /* PORT_GL_DRESSING_H */
