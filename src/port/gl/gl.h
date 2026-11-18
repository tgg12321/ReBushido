#ifndef PORT_GL_H
#define PORT_GL_H

/* The remastered renderer's backend: OpenGL 4.5 core (docs/PORT.md,
 * "Remastered"). It draws the primitives port_remaster.h decodes into a
 * colour buffer that is all of video memory at `scale` times its
 * resolution, reading texels from a mirror of the software GPU's video
 * memory, which stays the truth.
 *
 * Nothing here opens a window or a display: it is given a function that
 * looks GL's functions up (SDL_GL_GetProcAddress, eglGetProcAddress),
 * draws into a framebuffer object of its own, and hands back the
 * pixels. The window and the headless prover both use it. */

#include "common.h"
#include "port_remaster.h"

/* How the picture is drawn. */
typedef struct {
    s32 scale; /* across: 1 to PORT_REMASTER_SCALE_MAX */
    /* Down: 1 to PORT_REMASTER_SCALE_MAX, and 0 for the same as
     * across. A line of the console's picture is two lines of the
     * screen (240 of them fill a screen of four by three), so twice
     * `scale` is what gives the picture square pixels; the same as
     * `scale` is what the comparison with the software GPU needs. */
    s32 tall;
    /* How many times over each way the scene is drawn before it is
     * averaged down to the picture: 1 (off) to PORT_SUPERSAMPLE_MAX.
     * This is how the remastered picture's edges are smoothed
     * (docs/PORT.md, "Remastered", "Edges"). Averaging whole blocks of
     * `sample` by `sample` leaves the 2D layer exactly as crisp as it
     * was, because a pixel of video memory is a block of
     * `scale * sample` pixels of one colour and every block averaged
     * lies inside one of those. */
    s32 sample;
    /* The shape of the picture: four by three is the console's, and a
     * wider one is a wider view of the same scene, not a stretched
     * picture (docs/PORT.md, "Remastered", "A wider picture"). The
     * scene is drawn across the whole of it; the 2D layer stays in the
     * four-by-three band in the middle. 0 for the console's shape. */
    s32 aspect_w;
    s32 aspect_h;
    s32 is_bilinear; /* texels smoothed, not blocks */
    /* The stage's ground drawn as one static mesh instead of from the
     * game's tile primitives (gl_stage.h, docs/PORT.md). It is the
     * drawing path alone: the game emits its tiles exactly as it did and
     * the card leaves them out. 0 off; 1 on, which is on for the stages
     * the mesh is measured good on; 2 on for every stage whatever the
     * table says, which is what the prover measures a stage with
     * (tools/port_stage_prove.py). */
    s32 is_stage_mesh;
    /* What stands on the ground -- the stage's pieces: railings, fences,
     * walls, roofs -- drawn from the same mesh, with the same camera
     * (gl_stage.h, `stage_props_mesh`). 0 off; 1 on, for the stages the
     * pieces are measured good on; 2 on every stage. Nothing comes of it
     * where the ground is not the mesh's. */
    s32 is_stage_props;
    /* The scene held against the ground's own depth, and a fighter
     * against the slots its faces were sorted at, so that a piece of
     * ground between the camera and a fighter veils him again
     * (PORT_GL_DEPTH_RANGE below, gl_draw.c). On with the mesh; off is
     * for measuring what the test itself changed. */
    s32 is_stage_depth;
    /* How much the fighters' shadows darken the ground they fall on, in
     * hundredths: 0 for the game's own black silhouettes, which is what
     * `shadows original` means (gl_shadow.h, docs/PORT.md). */
    s32 shadows;
    /* The fighters drawn from models of their own where there is a file
     * for the model (gl_fighter.h, docs/PORT.md, "The fighters' own
     * models"): 0 the game's own primitives. */
    s32 is_fighter_models;
    /* The game's own lighting worked out again at each pixel, from the
     * normals the ground's mesh and the fighters' models have, with the
     * stage's own lights (docs/PORT.md, "Lighting"): 0 as each vertex or
     * face was lit. */
    s32 is_smooth_light;
    /* How the effects are drawn: PORT_EFFECTS_... (port_remaster.h). */
    s32 effects;
    /* A sky of the port's own behind the stages open to it, in the place
     * of the game's fill of the colour of the distance (gl_sky.h):
     * PORT_SKY_ORIGINAL (0) or PORT_SKY_NIGHT. */
    s32 sky;
    /* The stage's walls and pieces that stand between the camera and a
     * fighter faded so that he can be seen (gl_see.h): 0 off. */
    s32 see_through;
    /* The three things that make the hardware's picture the console's,
     * for the proof at scale 1 (docs/PORT.md, "Remastered"): the
     * corners where the PlayStation held them, the texture flat across
     * a polygon, and five bits a channel out with the chip's dither. */
    s32 is_exact;
} PortGlOptions;

/* Loads the functions, makes the shaders, the mirror of video memory
 * and the colour buffer, and attaches itself as the remastered
 * renderer's backend. Returns NULL, or why it could not. `getproc` is
 * the host's way of finding a GL function. */
extern const char *port_GlStart(
    void *(*getproc)(const char *name), const PortGlOptions *options);

/* The options again: the scale, the filter and the exact picture can
 * each change between frames. */
extern void port_GlSetOptions(const PortGlOptions *options);
extern void port_GlGetOptions(PortGlOptions *options);

/* Whether the backend is there and its context is current. */
extern s32 port_GlIsStarted(void);

/* The four functions the decoder calls, as this file has them. The
 * renderer that draws a frame again between two of the game's puts
 * itself in front of these and calls them itself (gl_tween.c). */
extern const PortRemasterBackend *port_GlBackend(void);

/* Whether every triangle of a primitive is one the chip would draw at
 * all: inside 1023 pixels across and 511 down, and of some area. A
 * caller that moves a primitive's corners (gl_tween.c) asks before it
 * draws the moved one, so that a picture between two frames holds
 * exactly the primitives the frame on show held and never leaves a hole
 * where one of them was. */
extern s32 port_GlWouldDraw(const PortRemasterPrim *prim);

/* How many pixels of video memory wide the picture is on each side of
 * the display's own rectangle, as the renderer has it now: 0 at the
 * console's shape (port_RemasterMargin). A caller that has to know which
 * primitives the picture holds asks it (gl_tween.c). */
extern s32 port_GlPictureMargin(void);

/* The colour the game last cleared its picture to, as the picture holds
 * it (0xFFRRGGBB), or 0 where it has not cleared one of one colour. It
 * is the stage's colour of the distance in a fight (light.c,
 * light_SetFarColor), and it is what shows through a hole in the scene:
 * the prover counts the pixels of a picture between two frames that are
 * this colour where both of the frames it lies between are not
 * (gl_main.c, port_CountHoles). */
extern u32 port_GlBackground(void);

/* A rectangle of video memory into the mirror the card reads texels
 * from, and nothing else: no batch is flushed and nothing is drawn.
 * `port_GlWroteVram` is the game's own write, which is also drawn into
 * the colour buffer; this is for a caller that has changed what
 * `g_port_vram` holds and has to put it back (gl_tween.c). */
extern void port_GlUploadVram(s32 x, s32 y, s32 w, s32 h);
/* The same for pixels put back exactly as they were before a picture
 * between frames wrote over them: the stage's pages are not looked up
 * again for it (gl_scene.h). Every picture between two frames gave the
 * whole of video memory back, so every page of the stage was looked up
 * again each picture: a sixth of the processor's time in a fight on the
 * owner's PC (2026-11-18). */
extern void port_GlRestoreVram(s32 x, s32 y, s32 w, s32 h);
/* A picture between two frames writes again, into the mirror, what the
 * frame on show wrote of video memory, and then puts the mirror back as it
 * was. Put back by what the picture changed of the mirror -- nearly
 * nothing, since a frame's clear and its fog's colour tables are mostly
 * what the mirror holds already -- and not by what video memory holds
 * now, which is the software GPU's picture where the frame was cleared
 * and is not the mirror's business: putting that back sent the card some
 * 400,000 pixels each picture, and sending them to a texture the card was
 * still reading from held the window's thread for 3 ms (2026-11-18).
 * Hold before the picture's first write; give back after its last.
 * FALSE from GiveBack where what was changed was too much to keep: the
 * caller then puts the rectangles back as before (port_GlRestoreVram). */
extern s32 port_GlMirrorHold(void);
extern s32 port_GlMirrorGiveBack(void);

/* **Video memory as the card's pictures read it.** What the backend and
 * the files about it look up in video memory while they draw (a mod's
 * pictures by their keys, the stage's pages, the fighters' colour tables,
 * a frame's clear colour) is read through this, which is `g_port_vram`
 * but while a picture between two frames is drawn: then it is the
 * backend's own copy of what the card's mirror holds, into which the
 * picture has written again what the frame on show wrote
 * (port_GlReplayVram). So that picture touches neither video memory nor
 * the software GPU's painters, which may still be painting the frame
 * after it (docs/PORT.md, "The tween off video memory"). Declared in
 * port_gpu.h. */
/* TRUE: read the backend's copy (above); FALSE: `g_port_vram` again. */
extern void port_GlReadMirrorCopy(s32 on);
/* A rectangle of video memory a frame wrote, given again by a picture
 * between frames with the frame's own pixels (`pixels`, `w` a line):
 * into the mirror and the colour buffer as port_GlWroteVram puts the
 * game's, but from `pixels` and not from `g_port_vram`. */
extern void port_GlReplayVram(s32 x, s32 y, s32 w, s32 h, const u16 *pixels);
/* The pixels of the rectangle (left, top, w, h) of video memory that the
 * calls of the backend to come tell of are these, not video memory's own
 * (gl_record.h: the step's record, drawn after the step has gone on);
 * NULL: video memory's again. A rectangle that goes round an edge is
 * given as port_AddDirtyRect takes it. */
extern void port_GlTellPixels(
    const u16 *pixels, s32 left, s32 top, s32 w, s32 h);
/* The pixel at (x, y): the told one, where there are pixels told and it
 * is among them, or video memory's. A row runs on to the told
 * rectangle's right edge. */
extern const u16 *port_GlVramAt(s32 x, s32 y);

/* **The mirror is a buffer**, which the card reads as a texture of 16-bit
 * words, a line of video memory after another: `vram_texture` of
 * PortGlPicture is bound to PORT_GL_VRAM_TARGET, and every shader reads it
 * through `vram(x, y)` (PORT_GL_VRAM_SHADER).
 *
 * Why not a texture of 1024 by 512, as it was: a frame writes video memory
 * between its draws -- its fog's colour tables, three times a fight frame,
 * and a picture between frames writes them again and puts them back --
 * and a texture the card may still be reading can only be written by
 * waiting for the card: in the driver, which held the window's thread for
 * 1 to 2 ms a write on the owner's card (2026-11-18), or on the card,
 * where a send through a pixel buffer cost its video memory pass 7 ms. A
 * buffer's lines are sent by glBufferSubData, which the driver queues in
 * its turn among the draws, and neither waits. */
#define PORT_GL_VRAM_TARGET 0x8C2A /* GL_TEXTURE_BUFFER */
#define PORT_GL_VRAM_SHADER                                                    \
    "uniform usamplerBuffer u_vram;\n"                                         \
    "uint vram(int x, int y) {\n"                                              \
    "    return texelFetch(u_vram, ((y & 511) << 10) | (x & 1023)).r;\n"       \
    "}\n"

/* The counts of the frame being drawn (vertices, draws, pixels of the
 * mirror sent, triangles left out), kept aside and put back: a picture
 * drawn between two of the game's frames is not one of them and is not
 * counted with them (gl_tween.c). */
#define PORT_GL_COUNTS 4
extern void port_GlSaveCounts(u32 *four);
extern void port_GlLoadCounts(const u32 *four);

/* Triangles left out so far in the frame being drawn, by the chip's own
 * rule (port_GlWouldDraw). A picture between two frames must leave out
 * exactly as many as the frame on show did, or it has a hole in it. */
extern u32 port_GlDroppedNow(void);

/* The display's rectangle of the colour buffer, as 0xFFRRGGBB a pixel,
 * a row after a row from the top, into `out`: at most `room` pixels.
 * `out_w` and `out_h` are what it wrote. 0 if there is nothing to
 * read. */
extern s32 port_GlReadPicture(u32 *out, u32 room, s32 *out_w, s32 *out_h);

/* The picture is in the colour buffer; nothing is shown. A window calls
 * this and then draws the buffer into itself (and swaps); the prover
 * reads it back. */
extern void port_GlFinishFrame(void);
/* Until the card has done everything it was given, a swap among it: the
 * window's `low_latency`, after each swap, so that no picture waits in
 * the driver's queue behind another (docs/PORT.md, "Input lag"). */
extern void port_GlWaitForCard(void);

/* ---- what the stage's own mesh needs of the picture ---- */

/* What a depth is divided by to make a value for the depth buffer: a
 * depth in the view, in the world's own units. The ground's mesh writes
 * it per pixel and the scene's primitives are tested against it
 * (gl_stage.h; docs/PORT.md, "The ground as one mesh"). The coprocessor
 * keeps sixteen bits of a depth scaled down by the shift the renderer
 * chose for the thing being drawn (`scale_shift`, at most 2), so a depth
 * in the world's units reaches 65535 << 2. */
#define PORT_GL_DEPTH_RANGE 262144.0f

/* How much nearer than it is a primitive of the scene is taken to be
 * when it is tested against the ground, in the world's own units. The
 * things that lie *on* the ground -- a fighter's shadow, which is his
 * model flattened onto it, his feet, blood, a weapon he dropped -- have
 * the ground's own depth to within the coprocessor's rounding, and
 * without an offset the ground would eat them. A piece of ground that
 * stands between the camera and a fighter, which is what the test is
 * for, is thousands of units nearer than he is, so the offset has a
 * wide field to sit in: this is a fighter's own width, about. Nothing
 * of the scene writes the depth buffer, so this is a decal's offset and
 * the painter's order among the scene's primitives is untouched. */
#define PORT_GL_DEPTH_BIAS 128.0f

/* Where the picture is being drawn and at what scale, for a caller that
 * draws into the colour buffer itself (gl_stage.c). */
typedef struct {
    u32 framebuffer;   /* what the renderer draws through */
    u32 color_texture; /* the colour buffer */
    u32 vram_texture;  /* the mirror of video memory */
    /* The depth buffer attached to that framebuffer, which the ground's
     * mesh writes and the scene is tested against. The renderer owns it
     * so that it is always of the buffer's own size, whether or not the
     * mesh is drawn at all. */
    u32 depth_buffer;
    /* the slots of the ground's faces, which a fighter is held against
     * (port_GlGroundSlotsBegin) */
    u32 slot_texture;
    /* the colour and depth buffers' samples a pixel: 1 for one, more where
     * the picture is multisampled (gl_draw.c, port_GlSamplesOf) */
    s32 samples;
    s32 target_w;
    s32 target_h;
    s32 draw_x; /* a pixel of video memory is this many of the buffer */
    s32 draw_y;
    s32 margin; /* a wider picture's band, in video memory's pixels */
    s32 display_x;
    s32 display_y;
    s32 display_w;
    s32 display_h;
} PortGlPicture;

extern void port_GlGetPicture(PortGlPicture *picture);
/* A texture's corner of `source_w` by `source_h` texels drawn into the
 * colour buffer at (x, y), w by h of its pixels (a blit cannot draw into
 * a buffer of several samples a pixel). */
extern void port_GlDrawTexture(
    u32 texture, s32 texture_w, s32 texture_h, s32 source_w, s32 source_h,
    s32 x, s32 y, s32 w, s32 h, s32 is_linear);

/* ---- what the fighters' shadows need of the picture (gl_shadow.h) ---- */

/* A corner of a primitive as the picture draws it, for a caller that
 * draws the same geometry into a buffer of its own: where it is in video
 * memory's own pixels with a wider picture's margin added, and the `w`
 * the picture's own shader is given. */
typedef struct {
    float x;
    float y;
    float w;
} PortGlPoint;

/* The triangles of a primitive as the picture would draw them, three
 * points each: how many points it wrote (0, 3 or 6). A triangle the chip
 * itself would not have drawn is left out, exactly as the picture leaves
 * it out (port_GlIsDrawn). */
extern s32 port_GlPlacePoints(const PortRemasterPrim *prim, PortGlPoint *out);

/* The scissor the picture draws this primitive through, in the colour
 * buffer's own pixels: the game's drawing area, the margin of a wider
 * picture, and the whole width where the scene is drawn across it. */
extern void port_GlScissorFor(
    const PortRemasterPrim *prim, s32 *x, s32 *y, s32 *w, s32 *h);

/* The primitives of the frame that are not the stage's ground, as the
 * picture drew them, into `framebuffer`'s **alpha alone**: every
 * fragment the picture kept marks its pixel as something the shadows
 * must not darken. It is the picture's own shader, so a texel the
 * picture threw away is thrown away here too and the ground under it
 * keeps its shadow. The caller sets the scissor; the colour buffer is
 * bound again afterwards. */
extern void port_GlDrawOccluders(u32 framebuffer);

/* The vertices kept for that pass in the picture being built, and those
 * it had no room for. */
extern s32 port_GlOccluderCount(void);
extern s32 port_GlOccluderLost(void);
/* They belong to the picture whose shadows have just been laid down and
 * are kept no longer: a picture drawn between two of the game's frames
 * is a picture of its own, and the frame after it must not be given its
 * vertices as well (gl_shadow.h). */
extern void port_GlForgetOccluders(void);
/* The batch so far drawn, so that something else may draw in its turn
 * among the primitives, and the batching's state forgotten after it. */
extern void port_GlFlushBatch(void);
extern void port_GlForgetState(void);
/* Vertices and draws added to the frame's counts. */
extern void port_GlCountDraw(u32 vertices, u32 draws);
/* Around the draw of the ground's mesh (gl_stage.c): its second output,
 * the ordering-table slot of the face each pixel shows, goes into a
 * buffer of its own, against which a fighter is held as the console held
 * him (gl_draw.c, docs/PORT.md, "Depth: the scene is held against the
 * ground"). Begin clears it; End leaves it to be read. */
extern void port_GlGroundSlotsBegin(void);
extern void port_GlGroundSlotsEnd(void);

/* ---- into a window (src/port/window/sdl_window.c) ---- */

/* What the card says it is, for the session's log. */
enum {
    PORT_GL_VENDOR,
    PORT_GL_RENDERER,
    PORT_GL_VERSION,
};

extern const char *port_GlDescribe(s32 which);

/* Video memory's mirror sent to the card again, all of it. The decoder
 * tells the backend only what the game writes while the remastered
 * picture is on (port_remaster.h), so what was written while it was off
 * has to be taken in when it comes back: the window calls this whenever
 * the picture changes. */
extern void port_GlRefresh(void);

/* The window, `window_w` by `window_h` pixels, cleared to black. The
 * window's own framebuffer is drawn into: the colour buffer is left
 * bound again afterwards, so the game's next primitive goes where it
 * did. */
extern void port_GlClearWindow(s32 window_w, s32 window_h);

/* The picture into the rectangle `x`, `y`, `w` by `h` of the window, `y`
 * from its top. `is_linear` smooths it where the rectangle is not a whole
 * multiple of the picture. FALSE if there is no picture to draw. */
extern s32 port_GlShowPicture(
    s32 window_w, s32 window_h, s32 x, s32 y, s32 w, s32 h, s32 is_linear);

/* Whether the whole of the display is a picture drawn from a mod's (a
 * replacement of a picture the game wrote into video memory, not written
 * over since): the movie player's title picture, which the window then
 * shows from the card although the player is running (docs/MODS.md,
 * "The title picture"). */
extern s32 port_GlShowsReplacedPicture(void);

/* Pixels of the host's own (0xAARRGGBB each, a row after a row from the
 * top) into the same kind of rectangle: the software GPU's picture where
 * that is what is shown, and the counter in the corner over it
 * (`is_over`: what is there shows through where the pixels are clear). */
extern s32 port_GlShowPixels(
    const u32 *pixels, s32 pixels_w, s32 pixels_h, s32 window_w, s32 window_h,
    s32 x, s32 y, s32 w, s32 h, s32 is_linear, s32 is_over);

/* The window's own pixels as they are now, 0xFFRRGGBB each, a row after a
 * row from the top: at most `room` of them. For a test's picture of what
 * a player would see; nothing else reads them back. */
extern s32 port_GlReadWindow(u32 *out, u32 room, s32 window_w, s32 window_h);

/* What the last frame cost, in microseconds of the PC's clock, as the
 * host measures it: the caller gives the time. */
extern void port_GlNoteFrameTime(u32 microseconds);
extern u32 port_GlGetFrameTime(void);
/* The vertices and the draws of the last frame. */
extern u32 port_GlGetVertexCount(void);
extern u32 port_GlGetDrawCount(void);
/* Pixels of the mirror of video memory sent to the card in the last
 * frame. */
extern u32 port_GlGetUploadCount(void);
/* Of the last frame: vertices the projection put outside the console's
 * own 640 pixels, which only a wider picture shows, and the runs of
 * primitives drawn across the whole of a wider picture. */
extern u32 port_GlGetOutsideCount(void);
extern u32 port_GlGetWideCount(void);

/* Triangles of the last frame the chip itself would not have drawn:
 * more than 1023 pixels across, 511 down, or of no area. */
extern u32 port_GlGetDroppedCount(void);

#endif /* PORT_GL_H */
