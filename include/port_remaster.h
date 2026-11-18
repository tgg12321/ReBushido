#ifndef PORT_REMASTER_H
#define PORT_REMASTER_H

/* "Graphics: Remastered" for the native port (docs/PORT.md,
 * "Remastered"): the scene drawn again at the window's resolution from
 * the same primitives the PlayStation's GPU is given, with the
 * full-precision geometry the console threw away.
 *
 * Nothing here draws. This file is the side channel and the decoder:
 *
 *   - the GTE (src/port/gte.c) writes down the exact result of every
 *     projection, where it was stored;
 *   - the renderer (src/main/render_asm.c) says which of those points a
 *     face is made of, which primitive it became, and which face of
 *     which thing of the scene that primitive is a drawing of;
 *   - the GPU in C (src/port/raster.c) hands every command it is given
 *     here as well as drawing it, and the decoder turns it into a
 *     PortRemasterPrim with sub-pixel corners and a depth for each,
 *     which a backend draws.
 *
 * The software GPU stays exactly as it was and its video memory stays
 * the truth: textures are read from it, anything the game reads back is
 * its, and with "Graphics: Original" nothing here is called at all.
 * Nothing here changes a value the game computes.
 *
 * The backend (src/port/gl/) is OpenGL 3.3 core; the PlayStation builds
 * never see this header. */

#include "common.h"

/* An address as a whole number: as wide as a pointer on either system.
 * The port's files are compiled with no C library and no system headers
 * (-nostdinc), so this comes from the compiler itself, not <stdint.h>.
 * common.h declares it where the port's switch is on; the backend's
 * files, which talk to a C library and to SDL or EGL, are not compiled
 * with it on. */
#ifndef PORT_ADDRESS_DEFINED
#define PORT_ADDRESS_DEFINED
typedef __UINTPTR_TYPE__ PortAddress;
#endif

/* ---- which picture ---- */

enum {
    PORT_GRAPHICS_ORIGINAL = 0,   /* the software GPU's picture, 1x */
    PORT_GRAPHICS_REMASTERED = 1, /* the backend's, at the window's size */
    /* follow the game's own setting, g_rb_settings.graphics: what the
     * options page says. This is how a run starts. */
    PORT_GRAPHICS_FOLLOW = 2
};

/* What --graphics or the F2 key says; PORT_GRAPHICS_FOLLOW to go back to
 * the options page's choice. */
extern void port_SetGraphics(s32 which);
extern s32 port_GetGraphics(void);
/* Original and Remastered, one after the other (F2). It leaves
 * PORT_GRAPHICS_FOLLOW. */
extern void port_ToggleGraphics(void);
/* Whether the remastered picture is what is shown now: the setting
 * above, or the game's own where that is FOLLOW, and a backend
 * attached. */
extern s32 port_RemasterIsOn(void);

/* ---- how much is drawn, and how (docs/PORT.md, "Remastered") ---- */

/* How far the stage is drawn. The console draws the cells of a disc of
 * thirteen cells about the camera, inside the wedge it looks down, and
 * the stage's fog is placed to end where that disc does: everything
 * beyond is the fog's colour and the player sees the ground end
 * (STAGE_FAR, g_stage_near_mask). */
enum {
    PORT_DISTANCE_ORIGINAL, /* the console's disc of cells */
    PORT_DISTANCE_FAR,      /* the whole wedge the camera looks down */
    PORT_DISTANCE_MAX,      /* every cell of the stage, whichever way */
    PORT_DISTANCE_KINDS
};

/* How many times over the picture may be drawn and averaged down, which
 * is how the remastered picture's edges are smoothed ("Edges"). */
#define PORT_SUPERSAMPLE_MAX 4

/* The shape of the picture. The console's is four by three; a wider one
 * is a wider view of the scene and not a stretched picture, so the
 * number of pixels across grows with it. */
#define PORT_ASPECT_W_ORIGINAL 4
#define PORT_ASPECT_H_ORIGINAL 3
/* What the window the player runs shows the remastered picture at with
 * nothing said (src/port/window/keymap.c). The builds with no window of
 * their own keep the console's shape, so that their picture can still be
 * held to the software GPU's pixel for pixel (tools/port_gl_prove.py). */
#define PORT_ASPECT_W_WIDE 16
#define PORT_ASPECT_H_WIDE 9

typedef struct {
    s32 distance;    /* PORT_DISTANCE_... */
    s32 supersample; /* 1 (off), 2, 3 or 4 */
    s32 aspect_w;    /* 4 by 3 as the console, 16 by 9, or the window's */
    s32 aspect_h;
} PortRemasterView;

/* What a run was started with: the settings file's and the command
 * line's. Each is held to what there is; the picture's shape is reduced
 * to its lowest terms. These are a run's settings and are not changed
 * while it runs: the primitive buffers the port hands the game are
 * chosen from them before the game starts. */
extern void port_RemasterSetView(const PortRemasterView *view);
extern void port_RemasterGetView(PortRemasterView *view);
/* The settings a run starts with, with nothing said. */
extern void port_RemasterDefaultView(PortRemasterView *view);
/* `original`, `far` or `max` as a setting's value says it: the number,
 * or -1. `port_NameRemasterDistance` is the other way. */
extern s32 port_ReadRemasterDistance(const char *word);
extern const char *port_NameRemasterDistance(s32 distance);

/* What the game's drawing code asks of this frame.
 * PORT_DISTANCE_ORIGINAL where the remastered picture is off, so that
 * "Graphics: Original" draws and computes exactly what it always did. */
extern s32 port_RemasterDistance(void);

/* The fog's reach and the span its levels are spread over, in 256ths of
 * the console's, for a distance setting: pure arithmetic, which the host
 * tests hold (tests/port/remaster.c). */
#define PORT_FOG_FACTOR_ONE 256
extern s32 port_RemasterFogFactor(s32 distance);
/* Whether a distance setting needs the renderer's coarser scale for what
 * is far off (g_scene_settings.far_shift). A stage tile's place and its
 * depth are kept in sixteen bits after that scale: at the near scale a
 * tile 65,536 world units off reads negative and is dropped, and the
 * stage's grid is 90,510 units across its diagonal. */
extern s32 port_RemasterNeedsFarShift(s32 distance);

/* How many pixels of video memory wide the picture is on each side of
 * the display's own rectangle, for the shape asked for: 0 at four by
 * three, 106 for a 640-wide display at 16 by 9. The console's picture is
 * 640 pixels of video memory across and 240 lines down and is shown four
 * by three, so a pixel of it is half as wide as it is tall; the width
 * grows in that measure. It is held so that the picture fits across
 * video memory, which is what the remastered renderer draws into. Pure
 * arithmetic: the host tests hold it (tests/port/remaster.c). */
extern s32 port_RemasterMargin(s32 picture_w, s32 aspect_w, s32 aspect_h);

/* The same margin for the picture as it is now, in the game's own screen
 * pixels, or 0 with the original picture or the console's shape. The
 * game's drawing code asks it where it would cut a face off at the left
 * edge of the console's screen (src/main/render_asm.c,
 * render_FaceOffScreen): in a wider picture that face may still be seen
 * in the left band, so the test is against the picture's own edge. It is
 * worked out once a frame, where the game clears its ordering table
 * (port_RemasterNewFrame), and only read after that: a face is looked at
 * thousands of times a frame. */
extern s32 port_RemasterLeftMargin(void);
/* From now on the margin is worked out for a picture of this shape,
 * whether the remastered picture is on or not: netplay's, where which
 * faces the game keeps is part of the state both machines compare and
 * must not follow either player's picture (docs/NETPLAY.md, section
 * 13). Before the game starts. */
extern void port_RemasterFixMargin(s32 aspect_w, s32 aspect_h);

/* ---- the fight's HUD at a wider picture's edges ---- */

/* A wider picture has a band of its own each side of the console's 640
 * pixels, and the fight's HUD is built at the console's places in the
 * middle of it: the WINS counters, chanbara's clock, the team battle's
 * points and warnings. Each group of it says which edge it belongs to
 * while its primitives are built (src/main/hud.c), and the backend draws
 * those primitives that much further out.
 *
 * Nothing the game computes changes and nothing of video memory does:
 * the game's primitives are built where they always were, so the
 * software GPU draws the console's own picture and "Graphics: Original"
 * is untouched. The shift is the backend's, and it is 0 at the console's
 * shape, where the margin is.
 *
 * Only the fight's HUD says it. The menus, the two select screens, the
 * loading pictures, the panels and the tables after a fight are the
 * other callers of the same sprite code (sprite_Draw): they say nothing
 * and stay four by three in the middle. */
enum {
    PORT_ANCHOR_MIDDLE = 0, /* where the console has it */
    PORT_ANCHOR_LEFT = -1,  /* out to the picture's left edge */
    PORT_ANCHOR_RIGHT = 1   /* ... and to its right */
};

/* Which edge the primitives built from now on belong to. A group of the
 * HUD puts it back to PORT_ANCHOR_MIDDLE when it has built its own, and
 * a new frame clears it. */
extern void port_RemasterSetAnchor(s32 anchor);
/* The primitive at `prim` is one of them: it is written down under that
 * address with the anchor as it is now, as a face is
 * (port_RemasterNoteFace). Nothing is kept with no backend attached or
 * at PORT_ANCHOR_MIDDLE, which is every primitive but the fight HUD's. */
extern void port_RemasterNoteAnchored(PortAddress prim);

/* ---- the primitive buffers ---- */

/* The console's two primitive buffers are 0x13400 bytes each at fixed
 * addresses in its RAM, with the stack above them and the staging area
 * below (include/sys.h): neither can grow there. Drawing the whole of a
 * stage fills them several times over, and the game only looks after
 * the frame is drawn (main.c: "common prim over flow"). So where a run
 * asks for more than the console's draw distance the port hands the
 * game two buffers of its own, out of memory it gives itself at a fixed
 * address whose low 24 bits are no other memory's (port_platform.h,
 * PORT_PRIMS_ADDR), named to the ordering tables' links as the stack and
 * the console's RAM are (port_gpu.h, port_AddLinkRegion). Nothing from
 * `malloc` is ever given to the game.
 *
 * With the console's draw distance the game has the console's buffers at
 * the console's addresses, that memory is never asked for, and nothing
 * here is used at all. Each buffer is this large, and the two together
 * fill PORT_PRIMS_SIZE. */
#define PORT_PRIM_BUFFER_BYTES 0x100000U

/* The buffer for frame `which` (0 or 1), or NULL where this run has the
 * console's. */
extern u32 *port_RemasterPrimBuffer(s32 which);
/* How large those are; 0 where this run has the console's. */
extern u32 port_RemasterPrimBufferBytes(void);

/* ---- the limits a run came near ---- */

/* Every store of a fixed size that the drawing path fills, and the least
 * room any frame left in it. Drawing more of the stage fills all of
 * them, so each is counted and reported. */
typedef struct {
    u32 frames;      /* frames these were measured over */
    s32 prim_bytes;  /* the frame's primitive buffer is this large */
    s32 prim_free;   /* ... and this little was left, at the worst */
    s32 queue_size;  /* entries of the renderer's queue */
    s32 queue_free;  /* ... left at the worst */
    u32 ot_farthest; /* the farthest ordering-table slot a frame used */
    u32 tiles;       /* tiles a frame queued, at most */
    u32 tiles_least; /* ... and at least: the two are equal where the
                      * whole stage is drawn, whichever way the camera
                      * looks, and nothing of it comes and goes */
    u32 nodes;       /* ... and pieces of the stage */
    u32 nodes_least; /* ... and at least, which says the same of the
                      * pieces that stand on the ground as `tiles_least`
                      * says of the ground: they are collected from the
                      * same cells, so where no cell comes and goes no
                      * piece does either */
    u32 side_full;   /* times the side channel's tables were full */
} PortRemasterLimits;

/* What a frame left of its primitive buffer (src/main/main.c). Called
 * whatever the setting, so that the original's own headroom is measured
 * too. */
extern void port_RemasterNoteRoom(s32 prim_free, s32 prim_bytes);
/* How many tiles and pieces of the stage a frame queued, of the
 * renderer's queue of `queue_size` entries (stage.c). */
extern void port_RemasterNoteCells(s32 tiles, s32 nodes, s32 queue_size);
/* The ordering-table slot a primitive was linked at (render_LinkPrim):
 * the farthest any frame used says whether the table's last slots, where
 * every greater depth is clamped, are being reached; and the primitive
 * attached next has it as its own (PortRemasterPrim, `ot_slot`). */
extern void port_RemasterNoteOtSlot(s32 index);
extern const PortRemasterLimits *port_GetRemasterLimits(void);

/* ---- the side channel ---- */

/* What the GTE worked out for one point before the PlayStation threw it
 * away: where the point is with sixteen bits of fraction, how deep it
 * is, and the perspective divide. The screen position the hardware
 * keeps is this x and y shifted down by PORT_REMASTER_FRACTION and held
 * to eleven bits. */
#define PORT_REMASTER_FRACTION 16
#define PORT_REMASTER_ONE (1 << PORT_REMASTER_FRACTION)

typedef struct {
    s32 x; /* 16.16 pixels, before the drawing offset */
    s32 y;
    s32 depth;   /* the sum the GTE shifted into SZ, not held to 16 bits */
    s32 divided; /* h / depth, 16 bits of fraction, as SXY was made with */
    /* Where the point is before the projection: the three sums of the
     * rotation and the translation with PORT_REMASTER_VIEW_FRACTION bits
     * of fraction, which the GTE rounds to whole numbers before it
     * divides. In the renderer's own scale for the thing drawn (a corner's
     * `depth_shift`) and with the second row halved, as the view is
     * loaded. The fighters' shadows are cast from these
     * (port_RemasterCasterFaces). */
    s32 view[3];
} PortPrecisePoint;

#define PORT_REMASTER_VIEW_FRACTION 8

/* The GTE stored a projected point as a word of two halves at `at`
 * (src/port/gte.c). Nothing is kept unless a backend is attached. */
extern void port_RemasterNotePoint(
    PortAddress at, const PortPrecisePoint *point);

/* The program copied a point the GTE stored, a word of two halves, from
 * `from` to `to` itself, with no GTE in between (render_DrawSkinnedMesh
 * carries a joint's points from one bone's section into the next's): the
 * point written down for `from` is written down for `to` as well. A
 * copy the side channel is not told of leaves every face that uses the
 * new address with no place at all. */
extern void port_RemasterCopyPoint(PortAddress from, PortAddress to);

/* The renderer looked a face's corners up among the points it projected
 * (render_LoadFace): `points` is the array they are in and `index` which
 * of them, `count` of them. The next primitive linked into an ordering
 * table is taken to be this face. */
extern void port_RemasterNoteFace(
    PortAddress points, const s32 *index, s32 count);

/* That face became the primitive at `prim` (render_LinkEntry). */
extern void port_RemasterAttachFace(PortAddress prim);

/* ---- what a primitive is a drawing of ---- */

/* Which face of which thing a primitive came from, which is what pairs
 * two frames' primitives for the pictures drawn between them
 * (src/port/gl/gl_tween.c). It is an identity and not a likeness: the
 * address of the thing being drawn, the address of the mesh being
 * walked, which face of that mesh it is, and which piece of that face
 * this primitive is, where a face becomes several (a tile cut sixteen
 * ways, a face cut by the screen's edge). Two primitives of one frame cannot
 * share it, and the same face of the same thing has it again in the next frame
 * however far it moved or however the walk about it changed.
 *
 * `object` 0 means there is none: the 2D layer, and anything drawn
 * outside the renderer's walk. */
typedef struct {
    PortAddress object;
    PortAddress group;
    /* which face of that mesh: the corners it is made of, packed as the
     * mesh itself has them. It is the face's own and not a count of the
     * faces before it, so a face the camera turned away from in one
     * frame and not the other moves nothing else's name. */
    s32 face;
    s32 piece;
} PortRemasterIdentity;

/* The renderer is about to draw this thing: a node of the scene or a
 * tile of the stage (render_DrawQueue, render_DrawStageTiles). The
 * group and the piece start again. */
extern void port_RemasterNoteObject(PortAddress object);
/* ... and this face of its mesh, or this record of a tile's: the
 * primitives linked from now on are its pieces, numbered from 0
 * (render_LoadFace, render_DrawStageTiles). */
extern void port_RemasterNoteGroup(PortAddress group);

/* ---- the fighters' shadows (src/port/gl/gl_shadow.c) ---- */

/* A shadow in this game is the caster's own shape, not a blob: the
 * model's mesh is flattened onto the ground by a shear worked out from
 * the stage's sun (light_SetShadowMatrix, light_ProjectShadow) and drawn
 * as opaque black polygons. The remastered picture can darken the ground
 * under that silhouette instead of painting it black, and for that it has
 * to know exactly which primitives the silhouette is -- not by their
 * colour, which the ground itself may have, but by where they came from.
 *
 * The renderer says it: a node of kind NODE_KIND_SHADOW is where
 * light_ProjectShadow is called and nothing else is drawn under it
 * (render_SetupNode, render_DrawQueue), so every primitive linked while
 * such a node is being walked is a face of a shadow and no other
 * primitive is. It is one call a node and nothing is kept with no
 * backend attached. */
extern void port_RemasterNoteShadow(s32 is_shadow);

/* What is about to be drawn is a fighter's own, and not the stage's: his
 * body, a part of his model, his cloth, a weapon, a shadow of one of
 * those, a model of the tiny mode (render_DrawQueue, by the node's mesh
 * block). The ground drawn as one mesh does not hide such a thing
 * (src/port/gl/gl_draw.c, docs/PORT.md, "Depth: the scene is held against
 * the ground"). A node noted (port_RemasterNoteObject) is not a fighter's
 * until this says it is. */
extern void port_RemasterNoteFighter(s32 is_fighter);

/* What the `shadows` setting is in: how much of what a shadow falls on
 * it takes away, in hundredths. 0 is `shadows original`, the game's own
 * black polygons and nothing of this; `soft` is this much, which is what
 * is worth looking at (src/port/gl/gl_shadow.c). */
#define PORT_SHADOWS_SOFT 55

/* `shadows cast`: the silhouette is not the game's coarse shadow mesh
 * but the fighter's own faces, every one of them, flattened onto the
 * ground along the same sun by the backend in floating point
 * (gl_shadow.c). The setting's number is this bit and the strength in
 * hundredths; `cast` alone is PORT_SHADOWS_CAST_STRENGTH, darker than
 * `soft` (docs/PORT.md, "The shadows", says why). */
#define PORT_SHADOWS_CAST 0x100
#define PORT_SHADOWS_CAST_STRENGTH 68
#define PORT_SHADOWS_STRENGTH_MASK 0xFF

/* The `shadows` setting as a word says it: `original`, `soft`, a number
 * from 1 to 100 (soft, that strong), `cast`, or `cast:N` (cast, N
 * hundredths); -1 for anything else. Pure arithmetic: the host tests
 * hold it (tests/port/remaster.c). */
extern s32 port_ReadRemasterShadows(const char *word);

/* The renderer is walking a shadow node whose caster is the node at
 * `parent`, onto ground at height `ground_y` (render_DrawQueue): which
 * of the fighters' faces cast a shadow, and onto what, is told by the
 * game's own shadow nodes and not guessed. */
extern void port_RemasterNoteShadowCaster(PortAddress parent, s32 ground_y);

/* A face of a fighter as the renderer looked it up (render_LoadFace),
 * whether or not it became a primitive: a shadow is cast by the faces
 * that turn away from the camera as much as by those that face it, and
 * by the ones off the screen. */
typedef struct {
    PortAddress object; /* the node being drawn */
    PortAddress group;  /* the mesh's record, and the face's corners */
    s32 face;
    s32 count;       /* 3 or 4 */
    s32 depth_shift; /* the scale it was drawn at */
    s32 view[4][3];  /* PortPrecisePoint's `view`, a corner each */
} PortShadowCaster;

/* The faces of the frame whose primitives are being given now, and how
 * many; kept until the backend has begun the next frame. */
extern const PortShadowCaster *port_RemasterCasterFaces(s32 *count);
/* Faces the run had no room for. */
extern u32 port_RemasterCasterLost(void);
/* The ground the thing at `object` casts its shadow onto, by the game's
 * own shadow node of it: the node itself, or a bone of the same body
 * for a fighter's skinned body. FALSE where the game casts none. */
extern s32 port_RemasterCasterGround(PortAddress object, s32 *ground_y);
/* The slope the game slides a shadow along, 1.12 (light_SetShadowMatrix):
 * x and z move by the height above the ground times these. */
extern void port_RemasterShadowShear(s32 *x_shear, s32 *z_shear);

/* A new frame of primitives is about to be built: the ordering table was
 * cleared (src/port/gpu.c). What was written down before the frame
 * before it is forgotten. */
extern void port_RemasterNewFrame(void);

/* The primitive whose words are now being given to the GPU
 * (DrawOTag). 0 for words that come from somewhere else. */
extern void port_RemasterSetPrim(PortAddress prim);

/* ---- the effects (src/main/eff.c, src/main/wave.c) ---- */

/* The effects -- blood, sparks, flashes, dust, splashes, footprints, the
 * stage's flames -- are flat quads the game builds itself, outside the
 * renderer's walk, from points it projects through Sony's RotTransPers
 * and the GTE's own macros (docs/PORT.md, "The effects"). Most are a
 * sprite: one point projected, and the quad's corners that point plus
 * so many whole pixels each way, the size worked out from the depth by
 * a quotient the game rounds down. A few are four projected points
 * each (the muzzle flash, the footprints).
 *
 * What `effects` asks of them (port_ReadRemasterEffects):
 * PORT_EFFECTS_ORIGINAL draws them as the console placed them;
 * PORT_EFFECTS_EXACT draws each where the projection really put it,
 * its size without the rounding, and moves it between two frames by its
 * name; PORT_EFFECTS_SMOOTH also reads its texture smoothed, its clear
 * texels fading the edge out. None of them is ever hidden by the
 * ground's depth, as the console hid none of them. */
enum {
    PORT_EFFECTS_ORIGINAL = 0,
    PORT_EFFECTS_EXACT = 1,
    PORT_EFFECTS_SMOOTH = 2
};

/* `original`, `exact` or `smooth`: the number, or -1. */
extern s32 port_ReadRemasterEffects(const char *word);
extern const char *port_NameRemasterEffects(s32 effects);
/* What the backend asks for (gl_draw.c, from the run's setting). */
extern void port_RemasterSetEffects(s32 effects);
extern s32 port_RemasterEffects(void);

/* The primitive at `prim` is an effect's, and `name` (the effect's own
 * record) and `piece` say which: what pairs it with itself in the next
 * frame. Its `count` corners are the points the GTE stored at `points`,
 * one word after another, in the order of the primitive's corners. */
extern void port_RemasterNoteEffect(PortAddress prim, PortAddress points,
                                    s32 count, PortAddress name, s32 piece);
/* ... and it is a sprite about the point stored at `center`: its corners
 * are that point's place in whole pixels plus their own offsets from
 * it, and those offsets were worked out from `scale_number / depth`
 * rounded down, which the decoder takes the rounding out of. A
 * `scale_number` of 0 leaves the size as the game made it. */
extern void port_RemasterNoteSprite(
    PortAddress prim, PortAddress center, s32 scale_number, s32 depth,
    PortAddress name, s32 piece);

/* ---- the stage drawn as one mesh (src/port/gl/gl_stage.c) ---- */

/* The game builds the stage's ground again every frame: 32 by 32 cells of
 * 2,000 units, the cells chosen by the way the camera looks, each tile cut
 * four or sixteen ways near the camera, every piece of it a new primitive
 * (stage_Draw, render_DrawStageTiles). The remastered picture can instead
 * hold the whole of that ground as one mesh in the card's own memory,
 * built once when the stage loads, and draw it with the camera's own
 * transform: nothing comes and goes as the camera turns, the detail never
 * steps, and the pictures between two frames need not pair a tile with a
 * tile. The game goes on emitting its tiles exactly as it did -- its
 * logic, its buffers and its traces are untouched -- and the backend
 * simply does not draw the primitives that came from them.
 *
 * What the game gives up for it is this: where the camera is and what it
 * is looking through, at the moment the stage's tiles are drawn, and
 * where in video memory each level of the stage's fog keeps its colours.
 * Nothing is computed differently and nothing of the game's memory is
 * written. */

/* The stage's fog is 31 rows of colour tables (STAGE_FOG_LEVELS). */
#define PORT_STAGE_FOG_LEVELS 31
/* A MATRIX's nine halfwords. (port_gte.h has the same number under its
 * own name; this header is included where that one is not.) */
#define PORT_GTE_MATRIX_ELEMENTS_PORT 9
/* One bit for each of the stage's tiles (STAGE_MAX_TILES is 500), in
 * whole words. */
#define PORT_STAGE_TILE_WORDS 16

typedef struct {
    /* Bumped whenever the stage's tiles or the meshes they point at may
     * have moved: a stage read in, and the arena moving the stage's block
     * (stage_ReadCells, resource_OnStageMoved). A mesh built for an older
     * version is thrown away. */
    s32 version;
    s32 frames; /* frames in which the stage's tiles were drawn */
    s16 matrix[PORT_GTE_MATRIX_ELEMENTS_PORT]; /* the view's rotation, 1.12 */
    s32 camera[3]; /* where the camera is in the world */
    s32 far_shift; /* how far a tile's place and depth are scaled down */
    s32 h;         /* the distance to the projection plane */
    s32 ofx;       /* where the middle of the screen is, 16.16 */
    s32 ofy;
    s32 depth_shift;  /* a tile's depth into the ordering table */
    s32 fog_ot_index; /* the farthest slot the fog reaches */
    /* Nothing nearer than this slot is drawn (RENDER_OT_CUTOFF): the
     * ground that would stand between the camera and the fight is left
     * out. The game drops a whole face whose deepest corner is nearer
     * than it; the mesh cuts at the line itself, which is a near plane
     * and shows more of the ground, not less. */
    s32 ot_cutoff;
    /* The stage's fog as this frame placed it: for each level, the
     * ordering-table slot its copy was linked at, nearest first. A face
     * drawn at a slot nearer than that of level i has had the colours of
     * every level after i copied over it and then i's: so the level a
     * depth is fogged by is the first whose slot is farther than its own.
     * 0 levels means the stage has no fog this frame. */
    s32 fog_levels;
    s32 fog_slot[PORT_STAGE_FOG_LEVELS];
    /* Where those colours are: the rectangle the textures look at, and
     * the one each level is copied from (stage_InitFog walks them down
     * from the first and on from the top of the next column). */
    s32 fog_x;
    s32 fog_y;
    s32 fog_w;
    s32 fog_h;
    /* Which tiles this frame drew opened up (STAGE_TILE_OPEN), one bit a
     * tile, taken with the view. The flags are the game's and are set
     * again for the next frame before a picture of this one is made, so
     * a picture drawn again between two frames that read them where they
     * stand left out of the mesh a face this frame drew whole. */
    u32 open[PORT_STAGE_TILE_WORDS];
} PortStageView;

/* The renderer is drawing the stage's tiles with this view
 * (render_DrawStageTiles): the view's rotation, where the camera is, and
 * the scale what is far off is kept at. What the projection itself is set
 * up with is read from the coprocessor here. */
extern void port_RemasterNoteStageView(
    const void *matrix, const s32 *camera, s32 far_shift, s32 depth_shift,
    s32 fog_ot_index, s32 ot_cutoff);
/* Where the stage's fogged colour tables are (stage_InitFog): the
 * rectangle the textures look at, the levels following it downwards. */
extern void port_RemasterNoteFogRows(s32 x, s32 y, s32 w, s32 h);
/* Level `level` of the stage's fog was linked at ordering-table slot
 * `slot` this frame (stage_QueueFogMoves). */
extern void port_RemasterNoteFogLevel(s32 level, s32 slot);
/* The stage's tiles, or the meshes they name, may have moved
 * (stage_ReadCells, resource_OnStageMoved). */
extern void port_RemasterStageChanged(void);

extern const PortStageView *port_GetRemasterStageView(void);

/* What the mesh reads of the game. The backend's own files talk to a C
 * library and to SDL or EGL and are linked beside the game rather than
 * into it, so they see only the port's own names (tools/port_sdl_build.py
 * keeps `port_*` and `g_port_*` global and makes every other name the
 * game's own): what they need of the game's variables comes through
 * here. The structures themselves are the headers'. */
/* The stage's tiles (a StageTile each, stage.h) and how many; `count` may
 * be NULL. */
extern PortAddress port_RemasterStageTiles(s32 *count);
/* The mesh a tile names, as a pointer. */
extern PortAddress port_RemasterTileMesh(s32 block, s32 mesh);
/* The light setup the tiles are lit by (a LightSetup, light.h). */
extern const void *port_RemasterTileLights(void);
extern s32 port_RemasterStageNumber(void);

/* ---- the stage's own textures (src/port/gl/gl_scene.c) ---- */

/* A mod may give a picture of a whole texture page of the stage
 * (docs/MODS.md, "The scene's textures by page"). What a page holds that
 * is the stage's own is what the stage's file loaded into it: the rest is
 * whatever the menus or the stage before left there, which differs from
 * one run to the next, so the page's key is made of the stage's own
 * texels alone. tex_LoadStageGroup says which they are, one TIM at a
 * time; nothing is kept with no backend attached. */
#define PORT_STAGE_TEXTURES_MAX 512

typedef struct {
    s16 x; /* where its picture is in video memory, in pixels of 16 bits */
    s16 y;
    s16 w;
    s16 h;
    s16 bits;   /* PORT_REMASTER_DEPTH_..., as its TIM says */
    s16 clut_x; /* where its colour table was loaded, -1 for none */
    s16 clut_y;
    s16 clut_w; /* ... and its size, in colours by rows */
    s16 clut_h;
    /* the row the faces name it by: the file's own, which is the row
     * above where a table left of the display's width is loaded, the row
     * the stage's fog writes over while the frame is drawn
     * (tex_LoadStageGroup) */
    s16 named_y;
} PortStageTexture;

/* The stage's textures are about to be loaded: those of the stage before
 * are forgotten. */
extern void port_RemasterBeginStageTextures(void);
/* One of them: the TIM at `tim`, its picture loaded at (x, y) and its
 * colour table at (clut_x, clut_y), as tex_LoadTim was told, the faces
 * naming that table at (clut_x, named_y). */
extern void port_RemasterNoteStageTexture(
    const void *tim, s32 x, s32 y, s32 clut_x, s32 clut_y, s32 named_y);
/* What has been noted, how many, and a number that changes each time the
 * stage's textures are loaded again. */
extern const PortStageTexture *port_RemasterStageTextures(
    s32 *count, u32 *version);

/* ---- what a primitive is, once decoded ---- */

#define PORT_REMASTER_CORNERS 4

/* The most a primitive's depth is scaled down by (`depth_shift` below):
 * the renderer's own scale is 0, 1 or 2, and the stage's tiles take one
 * of their own, which is three at the most (g_scene_settings.far_shift,
 * RENDER_SHIFT_MASK). */
#define PORT_REMASTER_DEPTH_SHIFT_MAX 3

typedef struct {
    s32 x; /* 16.16 pixels, the drawing offset added */
    s32 y;
    s32 ix; /* where the PlayStation put it: whole pixels, eleven bits
             * with a sign and the drawing offset added, which is what
             * the software GPU drew */
    s32 iy;
    s32 depth; /* 0 where the point has none: then it is drawn flat */
    s32 u;     /* a texture point, 0 to 255 */
    s32 v;
    s32 r; /* 0 to 255 */
    s32 g;
    s32 b;
} PortRemasterCorner;

enum {
    PORT_REMASTER_TRIANGLE,
    PORT_REMASTER_QUAD,
    PORT_REMASTER_LINE,
    PORT_REMASTER_RECT
};

/* The four ways a half-transparent primitive mixes with what is there,
 * as the drawing mode numbers them (src/port/raster.c). */
enum {
    PORT_REMASTER_BLEND_HALF = 0,     /* half of each */
    PORT_REMASTER_BLEND_ADD = 1,      /* the two added */
    PORT_REMASTER_BLEND_SUBTRACT = 2, /* what is there, less this */
    PORT_REMASTER_BLEND_QUARTER = 3   /* what is there and a quarter */
};

/* The texture's depth, as the drawing mode numbers it. */
enum {
    PORT_REMASTER_DEPTH_4 = 0,
    PORT_REMASTER_DEPTH_8 = 1,
    PORT_REMASTER_DEPTH_15 = 2
};

typedef struct {
    s32 kind;
    s32 corner_count;
    PortRemasterCorner corner[PORT_REMASTER_CORNERS];
    s32 rect_w; /* a rectangle's size, in pixels */
    s32 rect_h;
    /* how it is painted: the same words as raster.c's PortPaint */
    s32 is_shaded;
    s32 is_textured;
    s32 is_raw;  /* the texel as it is, not times the colour */
    s32 is_half; /* half-transparent */
    s32 is_dithered;
    s32 blend;      /* PORT_REMASTER_BLEND_... */
    s32 texel_bits; /* PORT_REMASTER_DEPTH_... */
    s32 page_x;     /* the texture page's corner in video memory */
    s32 page_y;
    s32 clut_x; /* the colour table's first pixel */
    s32 clut_y;
    /* the texture window: a texture point is anded with the one and
     * ored with the other */
    s32 window_and_u;
    s32 window_or_u;
    s32 window_and_v;
    s32 window_or_v;
    /* the drawing area, its last pixels included */
    s32 clip_left;
    s32 clip_top;
    s32 clip_right;
    s32 clip_bottom;
    s32 mask_set;   /* ored into every pixel drawn */
    s32 mask_check; /* a pixel that has this is left alone */
    /* every corner has a depth from the GTE: the texture is drawn in
     * perspective and the corners are where the GTE really put them */
    s32 has_depth;
    /* How far down the scale a corner's depth is: the renderer scales
     * what it draws by the distance so that the coprocessor's sixteen
     * bits are enough (`scale_shift`, render_SetupNode), so a corner's
     * depth times two to this is a depth in the world's own units. That
     * is the scale the ground drawn as one mesh keeps its own depth
     * buffer in, so this is what lets the two be held against each other
     * (src/port/gl/gl_stage.c). 0 where there is no depth. */
    s32 depth_shift;
    /* Every corner has the place the GTE worked out, whether or not it
     * has a depth to go with it: the corners are drawn where the
     * projection really put them, and the primitive is moved between
     * its two frames; without a depth its texture runs evenly across it
     * as the console's does. A polygon the renderer flattens -- a
     * shadow is projected with a matrix that flattens it onto the
     * ground -- would come out this way; in the stages measured so far
     * every point the GTE wrote down had a depth with it. */
    s32 has_place;
    /* This is one face of a fighter's or a weapon's shadow: the caster's
     * mesh flattened onto the ground and painted black by the game
     * (port_RemasterNoteShadow). A backend that darkens the ground
     * instead draws it itself and not here. */
    s32 is_shadow;
    /* This is a drawing of a fighter's own and not of the stage
     * (port_RemasterNoteFighter). */
    s32 is_fighter;
    /* This is an effect's (port_RemasterNoteEffect): never hidden by the
     * ground's depth, and its texture smoothed with `effects smooth`. */
    s32 is_effect;
    /* The ordering-table slot the renderer linked it at
     * (port_RemasterNoteOtSlot), which is all the console sorted it by:
     * a larger slot is farther and painted sooner. -1 where it was not
     * said: a tile, which is linked by hand, and whatever is not the
     * renderer's. A fighter's own is held against the ground's mesh by
     * it (src/port/gl/gl_draw.c). */
    s32 ot_slot;
    /* Which face of which thing this is a drawing of, for the pictures
     * between two frames; `object` 0 where there is none. */
    PortRemasterIdentity id;
    /* PORT_ANCHOR_...: which edge of a wider picture this is drawn at.
     * PORT_ANCHOR_MIDDLE for everything but the fight HUD's own
     * primitives, which are rectangles (a sprite or a tile). */
    s32 anchor;
    /* The drawing offset that is already added to every corner above.
     * The game draws its two frames into the two halves of video memory,
     * so this differs by the picture's height from one frame to the
     * next: a backend that holds a frame beside the frame before it
     * takes it off before it compares the two
     * (src/port/gl/gl_tween.c). */
    s32 offset_x;
    s32 offset_y;
    /* Where this primitive is in the frame's primitive buffer, in bytes,
     * or -1 for one that is somewhere else (a 2D primitive built in a
     * pool of its own) or none at all. The renderer builds a frame's
     * primitives in the order it walks the scene, so the same face of
     * the same model is at the same offset in the frame after it: this
     * is the name by which two frames' primitives are paired. */
    s32 key;
} PortRemasterPrim;

/* ---- the backend ---- */

/* What draws the remastered picture. Each may be NULL. */
typedef struct {
    /* The primitives of a frame are about to come. */
    void (*begin)(void);
    void (*draw)(const PortRemasterPrim *prim);
    /* Video memory changed outside a primitive: a fill, a copy, a load
     * from the program. The software GPU has done it already; the
     * rectangle is in video memory's own pixels. */
    void (*wrote_vram)(s32 x, s32 y, s32 w, s32 h);
    /* The frame is done: what the display shows is this rectangle of
     * video memory. */
    void (*end)(s32 x, s32 y, s32 w, s32 h, s32 is_on, s32 is_rgb24);
} PortRemasterBackend;

/* NULL takes the backend away, and with it everything above: the port
 * is then what it is without this file. */
extern void port_RemasterSetBackend(const PortRemasterBackend *backend);
/* The backend as it is, or NULL. A backend that draws a frame again
 * between two of the game's puts itself in front of the one that draws
 * (src/port/gl/gl_tween.c), and this is how it finds it. */
extern const PortRemasterBackend *port_RemasterGetBackend(void);

/* ---- what the GPU in C hands over (src/port/raster.c) ---- */

/* A command whose words have all come, before the software GPU draws
 * it: decoded and given to the backend if the remastered picture is on.
 * `words` is the command, `count` its words. */
extern void port_RemasterCommand(const u32 *words, s32 count);
/* Video memory's words that a load from the program is writing: the
 * rectangle is told to the backend when the load ends. */
extern void port_RemasterLoadedVram(s32 x, s32 y, s32 w, s32 h);
/* The frame is done, at a vertical blank's end: the backend is told the
 * display's rectangle (port_GetDisplay) and finishes its picture. The
 * host calls this where it shows the software GPU's picture. The
 * counts of the frame on show stand until the game clears the next
 * frame's ordering table. */
extern void port_RemasterShow(void);

/* ---- what a run counts ---- */

/* The kinds a primitive is counted under: by its first word's code, so
 * that the two paths can be compared kind by kind. */
enum {
    PORT_REMASTER_COUNT_F3,
    PORT_REMASTER_COUNT_FT3,
    PORT_REMASTER_COUNT_G3,
    PORT_REMASTER_COUNT_GT3,
    PORT_REMASTER_COUNT_F4,
    PORT_REMASTER_COUNT_FT4,
    PORT_REMASTER_COUNT_G4,
    PORT_REMASTER_COUNT_GT4,
    PORT_REMASTER_COUNT_LINE,
    PORT_REMASTER_COUNT_RECT,
    PORT_REMASTER_COUNT_FILL,
    PORT_REMASTER_COUNT_COPY,
    PORT_REMASTER_COUNT_LOAD,
    PORT_REMASTER_COUNT_STORE,
    PORT_REMASTER_COUNT_SETTING,
    PORT_REMASTER_COUNT_KINDS
};

typedef struct {
    u32 seen[PORT_REMASTER_COUNT_KINDS];  /* commands the GPU was given */
    u32 drawn[PORT_REMASTER_COUNT_KINDS]; /* ... that the backend drew */
    u32 with_depth;    /* primitives whose every corner had a depth */
    u32 without_depth; /* ... and that had not: drawn flat */
    /* primitives whose every corner had the place the projection
     * worked out, with or without a depth to go with it: these are
     * drawn where it put them, and moved between two frames */
    u32 with_place;
    u32 points;       /* points the GTE wrote down */
    u32 faces;        /* faces the renderer said */
    u32 attached;     /* primitives a face was attached to */
    u32 point_misses; /* corners whose point was not written down */
    u32 full;         /* times a table was full and something was lost */
    u32 masked;       /* primitives drawn while the mask bit was in use */
    /* textured primitives whose page this frame had drawn into: their
     * texture is pixels the software GPU made, which the backend's
     * mirror of video memory has at the console's resolution and not
     * better. 0 in every run measured so far. */
    u32 texture_from_drawn;
    /* half-transparent primitives drawn, by which of the four mixes */
    u32 mixed[4];
    /* faces of a fighter's or a weapon's shadow (port_RemasterNoteShadow) */
    u32 shadows;
    /* polygons of a fighter's own, his shadow left out
     * (port_RemasterNoteFighter), and how many of them had no place:
     * such a one is drawn at whole pixels and stands still between two
     * frames while the faces about it move, so that a joint tears open
     * (port_RemasterCopyPoint). The prover holds it to none at all
     * (tools/port_gl_prove.py). */
    u32 fighter_faces;
    u32 fighter_unplaced;
    /* the other polygons of the renderer's walk that had no place */
    u32 walked_unplaced;
    /* the effects' polygons (port_RemasterNoteEffect), and how many of
     * them had no place */
    u32 effect_faces;
    u32 effect_unplaced;
} PortRemasterCounts;

extern const PortRemasterCounts *port_GetRemasterCounts(void);
/* The counts of this frame alone, which `begin` clears. */
extern const PortRemasterCounts *port_GetRemasterFrameCounts(void);
extern const char *port_NameRemasterKind(s32 kind);

/* ---- the picture's size (src/port/gl/) ---- */

/* How many times video memory's own resolution the remastered picture is
 * drawn at. 1 is the console's. */
#define PORT_REMASTER_SCALE_MAX 8

/* The scale for a window `window_w` by `window_h` showing a picture
 * `picture_w` by `picture_h`: enough that a picture pixel is never
 * smaller than a window pixel, at most PORT_REMASTER_SCALE_MAX. The
 * console's picture is 320 or 640 across and half as tall as it looks
 * (`picture_h` 240 lines fill a screen of four by three), so the scale
 * follows the window's height against twice the lines. */
extern s32 port_RemasterScaleFor(
    s32 window_w, s32 window_h, s32 picture_w, s32 picture_h);

/* ---- the rectangle of video memory that changed ---- */

/* The backend keeps a mirror of video memory as a texture, and this is
 * what it has to send again: the smallest rectangle holding everything
 * written since it last sent one. It is pure arithmetic, so the host
 * tests hold it (tests/port/remaster.c). */
typedef struct {
    s32 left;
    s32 top;
    s32 right; /* the last pixel, not one past it */
    s32 bottom;
    s32 is_empty;
} PortDirtyRect;

extern void port_ClearDirtyRect(PortDirtyRect *rect);
/* `w` by `h` from (`x`, `y`), which may go round video memory's edges:
 * one that does is taken as the whole width or height, since the mirror
 * is sent as one rectangle. Nothing is added for an empty one. */
extern void port_AddDirtyRect(PortDirtyRect *rect, s32 x, s32 y, s32 w, s32 h);
/* The pixels a dirty rectangle covers: 0 if it is empty. */
extern s32 port_DirtyRectPixels(const PortDirtyRect *rect);

/* ---- the pause (fight_RunPauseMenu, src/main/main.c) ---- */

/* The pause menu draws the fight's last frame again under its panel on
 * every loop, by giving the GPU that frame's ordering table once more.
 * It is the only place the game draws a table it has drawn already. The
 * side channel has long forgotten that frame's points and faces by then,
 * so the backend would draw it as the console did: whole-pixel corners,
 * the game's tiles instead of the stage's mesh, four by three. So while
 * the table goes to the GPU again the backend is told nothing of it, and
 * the fight's last frame as the backend drew it, which it kept, is put
 * where the table would have been drawn; the panel is drawn over it as
 * ever. The software GPU still draws the table, so what the game
 * computes and the original picture are what they were. A pause begun
 * with the remastered picture off draws the table as before. */
enum {
    PORT_PAUSE_OFF,    /* the pause is over: the picture is let go */
    PORT_PAUSE_FROZEN, /* the fight's last table is about to be drawn */
    PORT_PAUSE_PANEL   /* ... and has been: the pause's own primitives */
};

/* `from_x`, `from_y`: with PORT_PAUSE_FROZEN, the corner of the drawing
 * area the fight's last frame was drawn into, in video memory's pixels.
 * The game draws into one half of video memory while the other is
 * shown, so that frame is still whole there as the pause's first loop
 * draws into the other half; it is kept from there. */
extern void port_RemasterPause(s32 what, s32 from_x, s32 from_y);
/* Whether a pause is holding the fight's picture now: a picture between
 * two frames (gl_tween.h) is not drawn then. */
extern s32 port_RemasterIsHolding(void);
/* Whether the fight's pause menu is up, with either picture: the window's
 * settings menu opens there on Select (src/port/window/sdl_menu.c). */
extern s32 port_RemasterIsPaused(void);
/* Whether the primitives coming are the fight's last table drawn again
 * (from PORT_PAUSE_FROZEN to PORT_PAUSE_PANEL): what the scene was, with
 * none of what the side channel said of it. */
extern s32 port_RemasterIsRedrawing(void);
/* What keeps the picture and puts it back: called with PORT_PAUSE_FROZEN,
 * the corner the fight's last frame is at and the corner of the drawing
 * area the table would be drawn into now, and with PORT_PAUSE_OFF as the
 * pause ends. The first call of a pause keeps the picture. NULL: none,
 * and a pause draws the table as before. */
typedef void (*PortPauseHolder)(s32 what, s32 from_x, s32 from_y, s32 x, s32 y);
extern void port_RemasterSetPauseHolder(PortPauseHolder holder);

#endif /* PORT_REMASTER_H */
