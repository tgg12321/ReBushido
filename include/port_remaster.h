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
 *     face is made of, and which primitive it became;
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
    u32 frames;        /* frames these were measured over */
    s32 prim_bytes;    /* the frame's primitive buffer is this large */
    s32 prim_free;     /* ... and this little was left, at the worst */
    s32 queue_size;    /* entries of the renderer's queue */
    s32 queue_free;    /* ... left at the worst */
    u32 ot_farthest;   /* the farthest ordering-table slot a frame used */
    u32 tiles;         /* tiles a frame queued, at most */
    u32 nodes;         /* ... and pieces of the stage */
    u32 side_full;     /* times the side channel's tables were full */
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
 * every greater depth is clamped, are being reached. */
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
    s32 x;       /* 16.16 pixels, before the drawing offset */
    s32 y;
    s32 depth;   /* the sum the GTE shifted into SZ, not held to 16 bits */
    s32 divided; /* h / depth, 16 bits of fraction, as SXY was made with */
} PortPrecisePoint;

/* The GTE stored a projected point as a word of two halves at `at`
 * (src/port/gte.c). Nothing is kept unless a backend is attached. */
extern void port_RemasterNotePoint(
    PortAddress at, const PortPrecisePoint *point);

/* The renderer looked a face's corners up among the points it projected
 * (render_LoadFace): `points` is the array they are in and `index` which
 * of them, `count` of them. The next primitive linked into an ordering
 * table is taken to be this face. */
extern void port_RemasterNoteFace(
    PortAddress points, const s32 *index, s32 count);

/* That face became the primitive at `prim` (render_LinkEntry). */
extern void port_RemasterAttachFace(PortAddress prim);

/* A new frame of primitives is about to be built: the ordering table was
 * cleared (src/port/gpu.c). What was written down before the frame
 * before it is forgotten. */
extern void port_RemasterNewFrame(void);

/* The primitive whose words are now being given to the GPU
 * (DrawOTag). 0 for words that come from somewhere else. */
extern void port_RemasterSetPrim(PortAddress prim);

/* ---- what a primitive is, once decoded ---- */

#define PORT_REMASTER_CORNERS 4

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
    u32 with_depth;   /* primitives whose every corner had a depth */
    u32 without_depth; /* ... and that had not: drawn flat */
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

#endif /* PORT_REMASTER_H */
