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
    /* which of the stage's pieces stand as they were taken down, one bit a
     * piece (PortStageView's `standing`): a mesh standing in for a piece,
     * or set at its culms, goes when the game cuts the piece down */
    u32 standing[2];
    /* ... and which of them this frame queued (PortStageView's `pieces`):
     * a mesh is drawn where its piece would be, and casts where it stands */
    u32 queued[2];
} PortGlDressingView;

/* Loads the functions and makes the programs. Called where the backend
 * starts (gl_draw.c). Returns why it could not, or NULL. */
extern const char *port_GlDressingStart(void *(*getproc)(const char *name));

/* The window's `dressing` (on by default there; the prover's `--dressing`,
 * off by default). */
extern void port_GlDressingSetOn(s32 on);
extern s32 port_GlDressingIsOn(void);
/* Whether the shafts of light want the fighters' depth (gl_post.h,
 * port_GlPostFighterDepthNow): a shaft is held by it where a fighter
 * stands in front of it. */
extern s32 port_GlDressingWantsFighters(void);

/* The ground's mesh has been built (gl_stage.c, before its vertices go to
 * the card): its vertices, gl_stage.c's StageVertex of `stride` bytes,
 * `count` of them, those from `pieces_from` the pieces'. What grows where is
 * worked out from them when the stage's dressing is first asked for. A
 * ground the same as the last noted in all that is read of it (the mesh
 * built again for the game's lights, gl_stage.h, port_GlStagePrepare)
 * keeps what grew on it. */
extern void port_GlDressingNoteGround(
    const void *vertices, s32 stride, s32 count, s32 pieces_from);
/* The dressing grown now on the ground last noted, if it has not been
 * (gl_stage.c, port_GlStagePrepare: while the game loads). */
extern void port_GlDressingPrepare(void);

/* Once a game frame (gl_draw.c, where the frame begins): the fighters'
 * steps and slashes, the dust they kick up and the ripples they make. */
extern void port_GlDressingFrame(void);

/* Whether the stage has a dressing to draw now: on, a Remastered Classic
 * stage with files, its ground the mesh's. */
extern s32 port_GlDressingIsShown(void);
/* What stands on the ground and hides what is behind it -- the foliage and
 * the meshes -- into the colour buffer, the ground's slots and its depth,
 * while the mesh's slots are bound (gl_stage.c, between
 * port_GlGroundSlotsBegin and End). */
extern void port_GlDressingDrawSolid(const PortGlDressingView *view);
/* What mixes with what is behind it -- the water, the particles -- after
 * the ground, the picture's framebuffer bound again. */
extern void port_GlDressingDrawMixed(const PortGlDressingView *view);
/* The shafts of light of a shafts block, kept by the last DrawMixed and
 * drawn here, at the picture's end (gl_draw.c, port_GlFinishFrame), into
 * `framebuffer` of `w` by `h`: over the walls and pieces the game painted
 * after the ground, held by the ground's depth. Nothing where the last
 * ground drew none; TRUE where it drew, the card's state then changed. */
extern s32 port_GlDressingDrawLate(u32 framebuffer, s32 w, s32 h);
/* Whether the stage's mist is shown now: it reads the ground's depth a
 * pixel, which the ground's mesh then writes beside its slots (gl_draw.c,
 * port_GlGroundSlotsBegin), as for the lighting's passes. */
extern s32 port_GlDressingWantsDepth(void);

/* The meshes as the sun or a light sees them, through `matrix` (a column
 * after a column), into the depth map bound now (gl_sun.h, gl_lights.h):
 * the triangles drawn. */
extern u32 port_GlDressingDrawSunCasters(const float *matrix);
/* The foliage as the sun sees it, through `matrix`, thinned by its
 * distance from `eye` (the camera, the world's): into the sun's nearer
 * maps (gl_sun.c), where its shadows are larger than a texel. The
 * triangles drawn. */
extern u32 port_GlDressingDrawFoliageCasters(
    const float *matrix, const float *eye);
/* The foliage as a light sees it, through matrix (a face of its cube),
 * from place (x y z, and its reach): the squares of foliage within the
 * reach and the face's view, thinned by their distance from the light, into
 * the face's depth map bound now (gl_lights.c, each picture, beside the
 * fighters: the grass sways). The triangles drawn. */
extern u32 port_GlDressingDrawLightCasters(
    const float *matrix, const float *place);
/* While the game loads (sdl_window.c): each of the dressing's programs
 * drawn once into the picture, in the state it is drawn in, through a
 * scissor of no pixel, so that the driver makes them ready for the card
 * then (the particles' alone were 38 ms of a dressed fight's first frame
 * with the driver's cache of shaders empty, RTX 3090). Once a run. */
extern void port_GlDressingWarm(void);
/* The same for the programs that draw into the sun's and the lights' maps,
 * with a map's framebuffer bound (gl_sun.c, port_GlSunWarm): the foliage
 * and the meshes as a light sees them, drawn into a light's cube each
 * picture. */
extern void port_GlDressingWarmCasters(void);
/* The pieces a mesh stands in for this frame, a bit each (as the mesh's
 * u_pieces): the ground's mesh leaves them out. */
extern void port_GlDressingHiddenPieces(u32 *words, s32 count);

/* The stage's weather as its dressing has it, for the sky (gl_sky.c):
 * the wind's way across the ground (x and z, of length one), its
 * strength and its gusts (0 to 1, as `u_wind`), the dressing's default
 * where the stage's file says none; and whether it rains. Read of the
 * stage's file, nothing grown. FALSE where the stage is not dressed. */
extern s32 port_GlDressingWeather(float *wind, s32 *is_raining);

/* What the last picture drew, for the run's log and the bench: things,
 * triangles and draws. */
extern u32 port_GlDressingThings(void);
extern u32 port_GlDressingTriangles(void);
extern u32 port_GlDressingDraws(void);
/* A line for the run's log each time a stage's dressing is grown, or NULL
 * when there is nothing new. */
extern const char *port_GlDressingNews(void);

#endif /* PORT_GL_DRESSING_H */
