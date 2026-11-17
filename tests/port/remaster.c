/* The remastered renderer's side channel and decoder, without a card. */
/* "Graphics: Remastered" for the native port (src/port/remaster.c;
 * docs/PORT.md, "Remastered"): the table of what the GTE projected, the
 * faces the renderer attaches to its primitives, the decoder that turns
 * a GPU command back into a primitive with sub-pixel corners and a
 * depth, the rectangle of video memory that changed, and the scale for
 * a window. Nothing here opens anything: the backend is one made up
 * below that writes down what it is given. The commands are made up;
 * nothing is the game's. */
#include "common.h"
#include <psxsdk/libgpu.h>
#include "src/port/raster.c"
#include "src/port/remaster.c"
#include "runtime/runtime.h"

/* ---- what those files want of the rest of the port ---- */

RbSettings g_rb_settings;

/* The frame's two primitive buffers, by which the decoder names where a
 * primitive is (`prim->key`, port_remaster.h): the game's own are
 * SYS_PRIM_BUFFER_0 and _1, and these are the made-up ones the cases
 * below put their primitives in. */
s32 g_disp_frame_count;
u32 g_disp_prim_buffers[2] = {0x80100000, 0x80200000};
u32 g_disp_prim_buffer_size = 0x13400;

/* Where the renderer's walk over the queue is (render.h): the side
 * channel reads the scale it is drawing at from it, which is the scale a
 * corner's depth is in (`depth_shift`, port_remaster.h). The renderer is
 * not in this test, so the walk is this one, and the cases that care set
 * its scale themselves. */
RenderWalk g_render_walk;

void port_Fail(const char *format, ...) {
    test_Count(0, __FILE__, __LINE__, "the port stopped itself: %s", format);
    for (;;) {
    }
}

/* ---- the backend that writes down what it is given ---- */

#define DRAWN_MAX 64
static PortRemasterPrim g_drawn_prims[DRAWN_MAX];
static s32 g_drawn_count;
static s32 g_begins;
static s32 g_ends;

static struct {
    s32 x;
    s32 y;
    s32 w;
    s32 h;
} g_wrote;

static s32 g_writes;

static void test_Begin(void) { g_begins++; }

static void test_Drawn(const PortRemasterPrim *prim) {
    if (g_drawn_count < DRAWN_MAX) {
        g_drawn_prims[g_drawn_count] = *prim;
    }
    g_drawn_count++;
}

static void test_Wrote(s32 x, s32 y, s32 w, s32 h) {
    g_wrote.x = x;
    g_wrote.y = y;
    g_wrote.w = w;
    g_wrote.h = h;
    g_writes++;
}

static void test_End(s32 x, s32 y, s32 w, s32 h, s32 is_on, s32 is_rgb24) {
    (void)x;
    (void)y;
    (void)w;
    (void)h;
    (void)is_on;
    (void)is_rgb24;
    g_ends++;
}

static const PortRemasterBackend g_test_backend = {
    test_Begin, test_Drawn, test_Wrote, test_End};

static void test_Reset(void) {
    g_drawn_count = 0;
    g_begins = 0;
    g_ends = 0;
    g_writes = 0;
    g_render_walk.scale_shift = 0;
    port_RemasterSetBackend(&g_test_backend);
    port_SetGraphics(PORT_GRAPHICS_REMASTERED);
    port_RemasterSetPrim(0);
    port_RemasterNewFrame();
}

/* ---- writing commands ---- */

#define XY(x, y) ((((u32)(y) & 0xFFFF) << 16) | ((u32)(x) & 0xFFFF))
#define RGB(r, g, b) (((u32)(b) << 16) | ((u32)(g) << 8) | (u32)(r))
#define CODE(code) ((u32)(code) << 24)
#define UV(u, v, half) ((u32)(u) | ((u32)(v) << 8) | ((u32)(half) << 16))

static void gp0(u32 word) { port_WriteGp0(word); }

static void test_SetArea(s32 left, s32 top, s32 right, s32 bottom) {
    gp0(0xE3000000 | ((u32)top << 10) | (u32)left);
    gp0(0xE4000000 | ((u32)bottom << 10) | (u32)right);
}

/* ---- which picture ---- */

static void test_Which(void) {
    port_RemasterSetBackend(NULL);
    port_SetGraphics(PORT_GRAPHICS_REMASTERED);
    /* with no backend there is no remastered picture at all */
    CHECK_EQ(port_RemasterIsOn(), FALSE);
    port_RemasterSetBackend(&g_test_backend);
    CHECK_EQ(port_RemasterIsOn(), TRUE);
    port_SetGraphics(PORT_GRAPHICS_ORIGINAL);
    CHECK_EQ(port_RemasterIsOn(), FALSE);
    /* following the game's own options page */
    port_SetGraphics(PORT_GRAPHICS_FOLLOW);
    g_rb_settings.graphics = RB_GRAPHICS_ORIGINAL;
    CHECK_EQ(port_RemasterIsOn(), FALSE);
    g_rb_settings.graphics = RB_GRAPHICS_ENHANCED;
    CHECK_EQ(port_RemasterIsOn(), TRUE);
    /* the key that switches: it leaves `follow` behind */
    port_ToggleGraphics();
    CHECK_EQ(port_GetGraphics(), PORT_GRAPHICS_ORIGINAL);
    port_ToggleGraphics();
    CHECK_EQ(port_GetGraphics(), PORT_GRAPHICS_REMASTERED);
    g_rb_settings.graphics = RB_GRAPHICS_ORIGINAL;
    CHECK_EQ(port_RemasterIsOn(), TRUE);
    /* a number that is none of the three changes nothing */
    port_SetGraphics(7);
    CHECK_EQ(port_GetGraphics(), PORT_GRAPHICS_REMASTERED);
    port_RemasterSetBackend(NULL);
}

/* ---- the rectangle of video memory that changed ---- */

static void test_Dirty(void) {
    PortDirtyRect rect;

    port_ClearDirtyRect(&rect);
    CHECK_EQ(rect.is_empty, TRUE);
    CHECK_EQ(port_DirtyRectPixels(&rect), 0);
    /* nothing is added for a rectangle with no size */
    port_AddDirtyRect(&rect, 10, 10, 0, 5);
    port_AddDirtyRect(&rect, 10, 10, 5, -1);
    CHECK_EQ(rect.is_empty, TRUE);
    port_AddDirtyRect(&rect, 10, 20, 4, 3);
    CHECK_EQ(rect.is_empty, FALSE);
    CHECK_EQ(rect.left, 10);
    CHECK_EQ(rect.top, 20);
    CHECK_EQ(rect.right, 13);
    CHECK_EQ(rect.bottom, 22);
    CHECK_EQ(port_DirtyRectPixels(&rect), 12);
    /* one inside it changes nothing */
    port_AddDirtyRect(&rect, 11, 21, 2, 1);
    CHECK_EQ(rect.left, 10);
    CHECK_EQ(rect.right, 13);
    CHECK_EQ(port_DirtyRectPixels(&rect), 12);
    /* one beside it takes in both */
    port_AddDirtyRect(&rect, 100, 5, 2, 2);
    CHECK_EQ(rect.left, 10);
    CHECK_EQ(rect.top, 5);
    CHECK_EQ(rect.right, 101);
    CHECK_EQ(rect.bottom, 22);
    CHECK_EQ(port_DirtyRectPixels(&rect), 92 * 18);
    /* one that goes round the right edge is the whole width */
    port_ClearDirtyRect(&rect);
    port_AddDirtyRect(&rect, 1000, 10, 40, 4);
    CHECK_EQ(rect.left, 0);
    CHECK_EQ(rect.right, PORT_VRAM_WIDTH - 1);
    CHECK_EQ(rect.top, 10);
    CHECK_EQ(rect.bottom, 13);
    /* and one round the bottom is the whole height */
    port_ClearDirtyRect(&rect);
    port_AddDirtyRect(&rect, 10, 500, 4, 40);
    CHECK_EQ(rect.top, 0);
    CHECK_EQ(rect.bottom, PORT_VRAM_HEIGHT - 1);
    CHECK_EQ(rect.left, 10);
    CHECK_EQ(rect.right, 13);
    /* the whole of video memory */
    port_ClearDirtyRect(&rect);
    port_AddDirtyRect(&rect, 0, 0, PORT_VRAM_WIDTH, PORT_VRAM_HEIGHT);
    CHECK_EQ(port_DirtyRectPixels(&rect), PORT_VRAM_PIXELS);
}

/* ---- the picture's size ---- */

static void test_Scale(void) {
    /* a line of the console's picture is two of the window's */
    CHECK_EQ(port_RemasterScaleFor(640, 480, 640, 240), 1);
    CHECK_EQ(port_RemasterScaleFor(1280, 960, 640, 240), 2);
    CHECK_EQ(port_RemasterScaleFor(1920, 1080, 640, 240), 3);
    CHECK_EQ(port_RemasterScaleFor(2560, 1440, 640, 240), 4);
    CHECK_EQ(port_RemasterScaleFor(3840, 2160, 640, 240), 6);
    /* the narrow mode needs more of them across */
    CHECK_EQ(port_RemasterScaleFor(1280, 960, 320, 240), 4);
    /* a window smaller than the picture still draws it once */
    CHECK_EQ(port_RemasterScaleFor(320, 240, 640, 240), 1);
    /* nothing is asked for more than the most */
    CHECK_EQ(
        port_RemasterScaleFor(40000, 40000, 640, 240), PORT_REMASTER_SCALE_MAX);
    /* a window or a picture with no size */
    CHECK_EQ(port_RemasterScaleFor(0, 0, 640, 240), 1);
    CHECK_EQ(port_RemasterScaleFor(640, 480, 0, 0), 1);
}

/* ---- what a run's settings come to (docs/PORT.md, "Remastered") ---- */

/* Two texts letter for letter: the tests have no C library. */
static s32 test_TextsAreEqual(const char *a, const char *b) {
    s32 i;

    for (i = 0; a[i] != 0 && a[i] == b[i]; i++) {
    }
    return a[i] == b[i];
}

static void test_View(void) {
    PortRemasterView view;

    /* with nothing said, the console's own picture in every way */
    port_RemasterDefaultView(&view);
    CHECK_EQ(view.distance, PORT_DISTANCE_ORIGINAL);
    CHECK_EQ(view.supersample, 1);
    CHECK_EQ(view.aspect_w, PORT_ASPECT_W_ORIGINAL);
    CHECK_EQ(view.aspect_h, PORT_ASPECT_H_ORIGINAL);
    port_RemasterSetView(&view);
    /* ... and then the port hands the game no buffers of its own */
    CHECK(port_RemasterPrimBuffer(0) == NULL);
    CHECK_EQ((s32)port_RemasterPrimBufferBytes(), 0);

    /* each setting's name */
    CHECK_EQ(port_ReadRemasterDistance("original"), PORT_DISTANCE_ORIGINAL);
    CHECK_EQ(port_ReadRemasterDistance("far"), PORT_DISTANCE_FAR);
    CHECK_EQ(port_ReadRemasterDistance("max"), PORT_DISTANCE_MAX);
    CHECK_EQ(port_ReadRemasterDistance("maxi"), -1);
    CHECK_EQ(port_ReadRemasterDistance("ma"), -1);
    CHECK_EQ(port_ReadRemasterDistance(""), -1);
    CHECK(test_TextsAreEqual(
        port_NameRemasterDistance(PORT_DISTANCE_MAX), "max"));
    CHECK(test_TextsAreEqual(port_NameRemasterDistance(-1), "?"));

    /* the `shadows` setting's words */
    CHECK_EQ(port_ReadRemasterShadows("original"), 0);
    CHECK_EQ(port_ReadRemasterShadows("soft"), PORT_SHADOWS_SOFT);
    CHECK_EQ(port_ReadRemasterShadows("75"), 75);
    CHECK_EQ(port_ReadRemasterShadows("100"), 100);
    CHECK_EQ(port_ReadRemasterShadows("cast"),
             PORT_SHADOWS_CAST | PORT_SHADOWS_CAST_STRENGTH);
    CHECK_EQ(port_ReadRemasterShadows("cast:40"), PORT_SHADOWS_CAST | 40);
    CHECK_EQ(port_ReadRemasterShadows("0"), -1);
    CHECK_EQ(port_ReadRemasterShadows("101"), -1);
    CHECK_EQ(port_ReadRemasterShadows("cast:"), -1);
    CHECK_EQ(port_ReadRemasterShadows("cast:0"), -1);
    CHECK_EQ(port_ReadRemasterShadows("casts"), -1);
    CHECK_EQ(port_ReadRemasterShadows("softer"), -1);
    CHECK_EQ(port_ReadRemasterShadows(""), -1);

    /* the fog's reach for each, in 256ths of the console's */
    CHECK_EQ(
        port_RemasterFogFactor(PORT_DISTANCE_ORIGINAL), PORT_FOG_FACTOR_ONE);
    CHECK_EQ(
        port_RemasterFogFactor(PORT_DISTANCE_FAR), 2 * PORT_FOG_FACTOR_ONE);
    CHECK_EQ(
        port_RemasterFogFactor(PORT_DISTANCE_MAX), 4 * PORT_FOG_FACTOR_ONE);
    CHECK_EQ(port_RemasterFogFactor(-1), PORT_FOG_FACTOR_ONE);
    CHECK_EQ(port_RemasterFogFactor(PORT_DISTANCE_KINDS), PORT_FOG_FACTOR_ONE);
    /* only the whole stage needs the renderer's coarser far scale */
    CHECK_EQ(port_RemasterNeedsFarShift(PORT_DISTANCE_ORIGINAL), FALSE);
    CHECK_EQ(port_RemasterNeedsFarShift(PORT_DISTANCE_FAR), FALSE);
    CHECK_EQ(port_RemasterNeedsFarShift(PORT_DISTANCE_MAX), TRUE);

    /* a setting out of its range is held to it, and the smoothing to a
     * power of two, which is what leaves the 2D layer where it was */
    view.distance = 99;
    view.supersample = 3;
    view.aspect_w = 32;
    view.aspect_h = 18;
    port_RemasterSetView(&view);
    port_RemasterGetView(&view);
    CHECK_EQ(view.distance, PORT_DISTANCE_ORIGINAL);
    CHECK_EQ(view.supersample, 2);
    /* the shape in its lowest terms */
    CHECK_EQ(view.aspect_w, 16);
    CHECK_EQ(view.aspect_h, 9);
    view.supersample = 100;
    view.aspect_w = 0;
    view.aspect_h = -3;
    port_RemasterSetView(&view);
    port_RemasterGetView(&view);
    CHECK_EQ(view.supersample, PORT_SUPERSAMPLE_MAX);
    CHECK_EQ(view.aspect_w, PORT_ASPECT_W_ORIGINAL);
    CHECK_EQ(view.aspect_h, PORT_ASPECT_H_ORIGINAL);

    /* a run that draws further than the console is given the port's own
     * buffers of primitives, which are not the console's two */
    view.distance = PORT_DISTANCE_MAX;
    port_RemasterSetView(&view);
    CHECK_EQ((s32)port_RemasterPrimBufferBytes(), (s32)PORT_PRIM_BUFFER_BYTES);
    CHECK(port_RemasterPrimBuffer(0) != NULL);
    CHECK(port_RemasterPrimBuffer(1) != NULL);
    CHECK(port_RemasterPrimBuffer(2) == NULL);
    CHECK(port_RemasterPrimBuffer(-1) == NULL);
    CHECK_EQ((s32)((u8 *)port_RemasterPrimBuffer(1) -
                   (u8 *)port_RemasterPrimBuffer(0)),
             (s32)PORT_PRIM_BUFFER_BYTES);
    /* the two of them fill the memory the port asks for */
    CHECK_EQ((s32)(2 * PORT_PRIM_BUFFER_BYTES), (s32)PORT_PRIMS_SIZE);

    /* what the drawing code asks of a frame: the console's where the
     * remastered picture is off, whatever the run was started with */
    test_Reset();
    port_SetGraphics(PORT_GRAPHICS_ORIGINAL);
    CHECK_EQ(port_RemasterDistance(), PORT_DISTANCE_ORIGINAL);
    port_SetGraphics(PORT_GRAPHICS_REMASTERED);
    CHECK_EQ(port_RemasterDistance(), PORT_DISTANCE_MAX);
    /* ... and with no backend, which is every build but the window's */
    port_RemasterSetBackend(NULL);
    CHECK_EQ(port_RemasterDistance(), PORT_DISTANCE_ORIGINAL);

    port_RemasterDefaultView(&view);
    port_RemasterSetView(&view);
}

/* How much wider than the console's picture each shape is. */
static void test_Margin(void) {
    /* the console's own shape adds nothing */
    CHECK_EQ(port_RemasterMargin(640, 4, 3), 0);
    CHECK_EQ(port_RemasterMargin(320, 4, 3), 0);
    /* 16 by 9 is four thirds of it: 853 across, 106 each side */
    CHECK_EQ(port_RemasterMargin(640, 16, 9), 106);
    CHECK_EQ(port_RemasterMargin(320, 16, 9), 53);
    /* 16 by 10 is less */
    CHECK_EQ(port_RemasterMargin(640, 16, 10), 64);
    /* a shape narrower than the console's takes nothing away */
    CHECK_EQ(port_RemasterMargin(640, 1, 1), 0);
    CHECK_EQ(port_RemasterMargin(640, 5, 4), 0);
    /* a picture wider than video memory is held to it: the remastered
     * renderer draws into a buffer of that size */
    CHECK_EQ(port_RemasterMargin(640, 100, 9), (PORT_VRAM_WIDTH - 640) / 2);
    /* nothing to work from */
    CHECK_EQ(port_RemasterMargin(0, 16, 9), 0);
    CHECK_EQ(port_RemasterMargin(640, 0, 9), 0);
    CHECK_EQ(port_RemasterMargin(640, 16, 0), 0);
}

/* Every store of a fixed size the drawing path fills, and the least
 * room a frame left in it. */
static void test_Limits(void) {
    const PortRemasterLimits *limits;

    port_RemasterNoteRoom(5000, 78848);
    port_RemasterNoteRoom(4000, 78848);
    port_RemasterNoteRoom(9000, 78848);
    port_RemasterNoteCells(183, 5, 640);
    port_RemasterNoteCells(328, 8, 640);
    port_RemasterNoteCells(100, 2, 640);
    port_RemasterNoteOtSlot(2237);
    port_RemasterNoteOtSlot(3341);
    port_RemasterNoteOtSlot(900);
    limits = port_GetRemasterLimits();
    CHECK_EQ((s32)limits->frames, 3);
    CHECK_EQ(limits->prim_bytes, 78848);
    CHECK_EQ(limits->prim_free, 4000);
    CHECK_EQ(limits->queue_size, 640);
    CHECK_EQ(limits->queue_free, 640 - 328 - 8);
    CHECK_EQ((s32)limits->tiles, 328);
    CHECK_EQ((s32)limits->tiles_least, 100);
    CHECK_EQ((s32)limits->nodes, 8);
    CHECK_EQ((s32)limits->nodes_least, 2);
    CHECK_EQ((s32)limits->ot_farthest, 3341);
}

/* ---- the side channel ---- */

static void test_NotePoint(PortAddress at, s32 x, s32 y, s32 depth) {
    PortPrecisePoint point;

    point.x = x;
    point.y = y;
    point.depth = depth;
    point.divided = 0x10000;
    port_RemasterNotePoint(at, &point);
}

/* Four corners of a quad on the scratchpad, as the renderer's points
 * are, and the face that names them. */
static u32 g_points[8];

static void test_Side(void) {
    static const s32 index[4] = {0, 1, 2, 3};
    const PortRemasterPrim *prim;
    PortAddress base = (PortAddress)g_points;

    test_Reset();
    /* the GTE projected four points, each a sixteenth of a pixel past
     * a whole one */
    test_NotePoint(base + 0, (10 << 16) + 0x1000, (20 << 16), 0x2000);
    test_NotePoint(base + 4, (50 << 16) + 0x2000, (20 << 16), 0x2100);
    test_NotePoint(base + 8, (10 << 16), (60 << 16) + 0x4000, 0x1000);
    test_NotePoint(base + 12, (50 << 16), (60 << 16), 0x1100);
    port_RemasterNoteFace(base, index, 4);
    /* the scale the renderer is drawing at, which is the scale the
     * depths above are in: the primitive carries it so that the ground
     * drawn as one mesh and the rest of the scene can be held against
     * each other in the world's own units (src/port/gl/gl_stage.c) */
    g_render_walk.scale_shift = 2;
    port_RemasterAttachFace(0x80100000);
    CHECK_EQ(port_GetRemasterCounts()->point_misses, 0);

    /* the primitive those four corners became: a flat textured quad */
    test_SetArea(0, 0, 639, 239);
    port_RemasterSetPrim(0x80100000);
    gp0(CODE(0x2C) | RGB(0x80, 0x40, 0x20));
    gp0(XY(10, 20));
    gp0(UV(4, 8, 0x1234));
    gp0(XY(50, 20));
    gp0(UV(60, 8, 0x001A)); /* the page: 10 across, 256 down */
    gp0(XY(10, 60));
    gp0(UV(4, 40, 0));
    gp0(XY(50, 60));
    gp0(UV(60, 40, 0));
    port_RemasterSetPrim(0);
    CHECK_EQ(g_drawn_count, 1);
    prim = &g_drawn_prims[0];
    CHECK_EQ(prim->kind, PORT_REMASTER_QUAD);
    CHECK_EQ(prim->corner_count, 4);
    CHECK_EQ(prim->has_depth, TRUE);
    CHECK_EQ(prim->is_textured, TRUE);
    CHECK_EQ(prim->is_raw, FALSE);
    CHECK_EQ(prim->is_shaded, FALSE);
    CHECK_EQ(prim->is_half, FALSE);
    /* where the PlayStation held it, and where the GTE put it */
    CHECK_EQ(prim->corner[0].ix, 10);
    CHECK_EQ(prim->corner[0].iy, 20);
    CHECK_EQ(prim->corner[0].x, (10 << 16) + 0x1000);
    CHECK_EQ(prim->corner[0].y, (20 << 16));
    CHECK_EQ(prim->corner[1].x, (50 << 16) + 0x2000);
    CHECK_EQ(prim->corner[2].y, (60 << 16) + 0x4000);
    CHECK_EQ(prim->corner[0].depth, 0x2000);
    CHECK_EQ(prim->corner[3].depth, 0x1100);
    CHECK_EQ(prim->depth_shift, 2);
    /* its texture, its colour and its page */
    CHECK_EQ(prim->corner[0].u, 4);
    CHECK_EQ(prim->corner[0].v, 8);
    CHECK_EQ(prim->corner[3].u, 60);
    CHECK_EQ(prim->corner[0].r, 0x80);
    CHECK_EQ(prim->corner[0].g, 0x40);
    CHECK_EQ(prim->corner[0].b, 0x20);
    CHECK_EQ(prim->corner[2].r, 0x80); /* one colour for the whole */
    CHECK_EQ(prim->clut_x, 0x34 * 16);
    CHECK_EQ(prim->clut_y, 0x12 * 4 + 0);
    CHECK_EQ(prim->page_x, 10 * 64);
    CHECK_EQ(prim->page_y, 256);
    CHECK_EQ(prim->texel_bits, PORT_REMASTER_DEPTH_4);
    /* where it is in this frame's primitive buffer, and the drawing
     * offset that is already in its corners: what a renderer that holds
     * a frame beside the frame before it pairs them by
     * (src/port/gl/gl_tween.c) */
    CHECK_EQ(prim->key, 0);
    CHECK_EQ(prim->offset_x, 0);
    CHECK_EQ(prim->offset_y, 0);
    CHECK_EQ(prim->clip_left, 0);
    CHECK_EQ(prim->clip_right, 639);
    CHECK_EQ(port_GetRemasterCounts()->with_depth, 1);

    /* a primitive no face was attached to, and no point stored into:
     * drawn flat, where the PlayStation held its corners */
    test_Reset();
    g_drawn_count = 0;
    port_RemasterSetPrim(0x80200000);
    gp0(CODE(0x2C) | RGB(0x80, 0x40, 0x20));
    gp0(XY(10, 20));
    gp0(UV(4, 8, 0x1234));
    gp0(XY(50, 20));
    gp0(UV(60, 8, 0x001A));
    gp0(XY(10, 60));
    gp0(UV(4, 40, 0));
    gp0(XY(50, 60));
    gp0(UV(60, 40, 0));
    CHECK_EQ(g_drawn_count, 1);
    CHECK_EQ(g_drawn_prims[0].has_depth, FALSE);
    CHECK_EQ(g_drawn_prims[0].corner[0].x, 10 << 16);
    CHECK_EQ(g_drawn_prims[0].corner[0].depth, 0);
    /* this frame's buffer is the first of the two, so a primitive in
     * the second has no place in it */
    CHECK_EQ(g_drawn_prims[0].key, -1);

    /* the other frame's buffer, where the frame count is odd; a
     * primitive a long way into it, and one in neither */
    test_Reset();
    g_drawn_count = 0;
    g_disp_frame_count = 1;
    port_RemasterSetPrim(0x80200000 + 0x400);
    gp0(CODE(0x20) | RGB(0, 0, 0));
    gp0(XY(1, 2));
    gp0(XY(3, 4));
    gp0(XY(5, 6));
    CHECK_EQ(g_drawn_count, 1);
    CHECK_EQ(g_drawn_prims[0].key, 0x400);
    g_drawn_count = 0;
    port_RemasterSetPrim(0x80200000 + 0x13400);
    gp0(CODE(0x20) | RGB(0, 0, 0));
    gp0(XY(1, 2));
    gp0(XY(3, 4));
    gp0(XY(5, 6));
    CHECK_EQ(g_drawn_count, 1);
    CHECK_EQ(g_drawn_prims[0].key, -1);
    /* a word of no primitive at all */
    g_drawn_count = 0;
    port_RemasterSetPrim(0);
    gp0(CODE(0x20) | RGB(0, 0, 0));
    gp0(XY(1, 2));
    gp0(XY(3, 4));
    gp0(XY(5, 6));
    CHECK_EQ(g_drawn_count, 1);
    CHECK_EQ(g_drawn_prims[0].key, -1);
    g_disp_frame_count = 0;
}

/* A point the GTE stored into the primitive itself, as a stage tile
 * that is cut has it: the corner is found by the word it is in. */
static void test_SideInPrimitive(void) {
    static u32 prim_words[8];
    PortAddress prim = (PortAddress)prim_words;

    test_Reset();
    /* the words of the primitive after its link are the command's: the
     * triangle's first corner is the command's second word */
    test_NotePoint(
        prim + 2 * sizeof(u32), (30 << 16) + 0x8000, (40 << 16), 0x3000);
    test_NotePoint(prim + 4 * sizeof(u32), (70 << 16), (40 << 16), 0x3100);
    test_NotePoint(prim + 6 * sizeof(u32), (30 << 16), (80 << 16), 0x3200);
    port_RemasterSetPrim(prim);
    gp0(CODE(0x24) | RGB(0x10, 0x20, 0x30));
    gp0(XY(30, 40));
    gp0(UV(0, 0, 0));
    gp0(XY(70, 40));
    gp0(UV(63, 0, 0x001A));
    gp0(XY(30, 80));
    gp0(UV(0, 63, 0));
    CHECK_EQ(g_drawn_count, 1);
    CHECK_EQ(g_drawn_prims[0].kind, PORT_REMASTER_TRIANGLE);
    CHECK_EQ(g_drawn_prims[0].has_depth, TRUE);
    CHECK_EQ(g_drawn_prims[0].corner[0].x, (30 << 16) + 0x8000);
    CHECK_EQ(g_drawn_prims[0].corner[2].depth, 0x3200);
}

/* **Which face of which thing a primitive is a drawing of** -- the name
 * the pictures between two frames pair by (port_remaster.h). The face
 * is named by the corners it is made of and not by its order, and the
 * pieces of one face are numbered from 0. */
static void test_Identity(void) {
    static const s32 index[3] = {2, 5, 7};
    static const s32 other[3] = {2, 5, 6};
    PortAddress base = (PortAddress)g_points;

    test_Reset();
    test_NotePoint(base + 2 * 4, 30 << 16, 40 << 16, 0x3000);
    test_NotePoint(base + 5 * 4, 70 << 16, 40 << 16, 0x3100);
    test_NotePoint(base + 7 * 4, 30 << 16, 80 << 16, 0x3200);
    test_NotePoint(base + 6 * 4, 30 << 16, 80 << 16, 0x3200);
    port_RemasterNoteObject(0x80201000);
    port_RemasterNoteGroup(0x80301000);
    port_RemasterNoteFace(base, index, 3);
    /* two pieces of the one face, as a face cut by the screen's edge */
    port_RemasterAttachFace(0x80101000);
    port_RemasterAttachFace(0x80101020);
    port_RemasterSetPrim(0x80101020);
    gp0(CODE(0x20) | RGB(1, 2, 3));
    gp0(XY(30, 40));
    gp0(XY(70, 40));
    gp0(XY(30, 80));
    CHECK_EQ(g_drawn_count, 1);
    CHECK_EQ(g_drawn_prims[0].id.object, 0x80201000);
    CHECK_EQ(g_drawn_prims[0].id.group, 0x80301000);
    CHECK_EQ(g_drawn_prims[0].id.face, 2 | (5 << 8) | (7 << 16));
    CHECK_EQ(g_drawn_prims[0].id.piece, 1);
    CHECK_EQ(g_drawn_prims[0].has_place, TRUE);

    /* another face of the same mesh has another name, and its pieces
     * start again */
    g_drawn_count = 0;
    port_RemasterNoteFace(base, other, 3);
    port_RemasterAttachFace(0x80101040);
    port_RemasterSetPrim(0x80101040);
    gp0(CODE(0x20) | RGB(1, 2, 3));
    gp0(XY(30, 40));
    gp0(XY(70, 40));
    gp0(XY(30, 80));
    CHECK_EQ(g_drawn_count, 1);
    CHECK_EQ(g_drawn_prims[0].id.face, 2 | (5 << 8) | (6 << 16));
    CHECK_EQ(g_drawn_prims[0].id.piece, 0);

    /* a primitive of no thing at all -- the 2D layer -- has no name */
    g_drawn_count = 0;
    port_RemasterNoteObject(0);
    port_RemasterSetPrim(0x80101060);
    gp0(CODE(0x20) | RGB(1, 2, 3));
    gp0(XY(30, 40));
    gp0(XY(70, 40));
    gp0(XY(30, 80));
    CHECK_EQ(g_drawn_count, 1);
    CHECK_EQ(g_drawn_prims[0].id.object, 0);
    CHECK_EQ(g_drawn_prims[0].id.piece, 0);
}

/* **Which primitives are a shadow's** (port_remaster.h): the renderer
 * says it while it walks a node of kind NODE_KIND_SHADOW, and every
 * primitive linked until it says otherwise is a face of that shadow.
 * Nothing else is, whatever its colour: the ground has black faces of
 * its own. */
static void test_Shadow(void) {
    static const s32 index[3] = {0, 1, 2};
    PortAddress base = (PortAddress)g_points;
    const PortRemasterCounts *counts = port_GetRemasterCounts();
    u32 before;

    test_Reset();
    test_NotePoint(base + 0, 30 << 16, 40 << 16, 0x3000);
    test_NotePoint(base + 4, 70 << 16, 40 << 16, 0x3100);
    test_NotePoint(base + 8, 30 << 16, 80 << 16, 0x3200);
    before = counts->shadows;
    port_RemasterNoteObject(0x80201000);
    port_RemasterNoteShadow(TRUE);
    port_RemasterNoteFace(base, index, 3);
    port_RemasterAttachFace(0x80101000);
    port_RemasterSetPrim(0x80101000);
    gp0(CODE(0x20) | RGB(0, 0, 0));
    gp0(XY(30, 40));
    gp0(XY(70, 40));
    gp0(XY(30, 80));
    CHECK_EQ(g_drawn_count, 1);
    CHECK_EQ(g_drawn_prims[0].is_shadow, TRUE);
    CHECK_EQ(counts->shadows, before + 1);

    /* the next thing the renderer draws is not a shadow, black or not */
    g_drawn_count = 0;
    port_RemasterNoteObject(0x80202000);
    port_RemasterNoteShadow(FALSE);
    port_RemasterNoteFace(base, index, 3);
    port_RemasterAttachFace(0x80101020);
    port_RemasterSetPrim(0x80101020);
    gp0(CODE(0x20) | RGB(0, 0, 0));
    gp0(XY(30, 40));
    gp0(XY(70, 40));
    gp0(XY(30, 80));
    CHECK_EQ(g_drawn_count, 1);
    CHECK_EQ(g_drawn_prims[0].is_shadow, FALSE);
    CHECK_EQ(counts->shadows, before + 1);

    /* a new frame has no shadow being drawn */
    g_drawn_count = 0;
    port_RemasterNoteShadow(TRUE);
    port_RemasterNewFrame();
    port_RemasterNoteFace(base, index, 3);
    port_RemasterAttachFace(0x80101040);
    port_RemasterSetPrim(0x80101040);
    gp0(CODE(0x20) | RGB(0, 0, 0));
    gp0(XY(30, 40));
    gp0(XY(70, 40));
    gp0(XY(30, 80));
    CHECK_EQ(g_drawn_count, 1);
    CHECK_EQ(g_drawn_prims[0].is_shadow, FALSE);
}

/* What was written down for a frame is gone two frames later: the
 * primitive buffers and the ordering tables are used on alternate
 * frames, so nothing of a frame's own can have been written over. */
static void test_Frames(void) {
    static const s32 index[3] = {0, 1, 2};
    PortAddress base = (PortAddress)g_points;

    test_Reset();
    test_NotePoint(base + 0, 1 << 16, 2 << 16, 0x100);
    test_NotePoint(base + 4, 3 << 16, 4 << 16, 0x100);
    test_NotePoint(base + 8, 5 << 16, 6 << 16, 0x100);
    port_RemasterNoteFace(base, index, 3);
    port_RemasterAttachFace(0x80101000);
    port_RemasterNewFrame(); /* the frame after it: still there */
    port_RemasterSetPrim(0x80101000);
    gp0(CODE(0x20) | RGB(1, 2, 3));
    gp0(XY(1, 2));
    gp0(XY(3, 4));
    gp0(XY(5, 6));
    CHECK_EQ(g_drawn_count, 1);
    CHECK_EQ(g_drawn_prims[0].has_depth, TRUE);
    /* two frames on it counts as nothing */
    port_RemasterNewFrame();
    port_RemasterNewFrame();
    g_drawn_count = 0;
    port_RemasterSetPrim(0x80101000);
    gp0(CODE(0x20) | RGB(1, 2, 3));
    gp0(XY(1, 2));
    gp0(XY(3, 4));
    gp0(XY(5, 6));
    CHECK_EQ(g_drawn_count, 1);
    CHECK_EQ(g_drawn_prims[0].has_depth, FALSE);
}

/* The fight's HUD at the edges of a wider picture: a sprite or a tile
 * the game said belongs to an edge carries that edge to the backend, and
 * every other primitive carries PORT_ANCHOR_MIDDLE. */
static void test_Anchor(void) {
    static u32 prim_words[4];
    PortAddress prim = (PortAddress)prim_words;

    test_Reset();
    test_SetArea(0, 0, 639, 239);
    /* a tile the game anchored to the left edge */
    port_RemasterSetAnchor(PORT_ANCHOR_LEFT);
    port_RemasterNoteAnchored(prim);
    port_RemasterSetAnchor(PORT_ANCHOR_MIDDLE);
    port_RemasterSetPrim(prim);
    gp0(CODE(0x60) | RGB(0x10, 0x20, 0x30));
    gp0(XY(72, 36));
    gp0(XY(48, 1));
    CHECK_EQ(g_drawn_count, 1);
    CHECK_EQ(g_drawn_prims[0].kind, PORT_REMASTER_RECT);
    CHECK_EQ(g_drawn_prims[0].anchor, PORT_ANCHOR_LEFT);

    /* the next one, which nothing said anything about */
    g_drawn_count = 0;
    port_RemasterSetPrim(prim + 0x40);
    gp0(CODE(0x60) | RGB(0x10, 0x20, 0x30));
    gp0(XY(521, 36));
    gp0(XY(48, 1));
    CHECK_EQ(g_drawn_count, 1);
    CHECK_EQ(g_drawn_prims[0].anchor, PORT_ANCHOR_MIDDLE);

    /* a group that is built and never put back is still only its own
     * frame's: the anchor is cleared at the top of each */
    port_RemasterSetAnchor(PORT_ANCHOR_RIGHT);
    port_RemasterNewFrame();
    port_RemasterNoteAnchored(prim + 0x80);
    g_drawn_count = 0;
    port_RemasterSetPrim(prim + 0x80);
    gp0(CODE(0x60) | RGB(0x10, 0x20, 0x30));
    gp0(XY(521, 36));
    gp0(XY(48, 1));
    CHECK_EQ(g_drawn_count, 1);
    CHECK_EQ(g_drawn_prims[0].anchor, PORT_ANCHOR_MIDDLE);

    /* two frames on, what was written down counts as nothing */
    test_Reset();
    port_RemasterSetAnchor(PORT_ANCHOR_RIGHT);
    port_RemasterNoteAnchored(prim);
    port_RemasterSetAnchor(PORT_ANCHOR_MIDDLE);
    port_RemasterNewFrame();
    port_RemasterNewFrame();
    test_SetArea(0, 0, 639, 239);
    g_drawn_count = 0;
    port_RemasterSetPrim(prim);
    gp0(CODE(0x60) | RGB(0x10, 0x20, 0x30));
    gp0(XY(521, 36));
    gp0(XY(48, 1));
    CHECK_EQ(g_drawn_count, 1);
    CHECK_EQ(g_drawn_prims[0].anchor, PORT_ANCHOR_MIDDLE);
}

/* ---- what the decoder makes of the other commands ---- */

static void test_Commands(void) {
    const PortRemasterPrim *prim;

    test_Reset();
    test_SetArea(0, 0, 639, 239);
    /* a rectangle of its own size, textured and half-transparent */
    gp0(CODE(0x66) | RGB(0x20, 0x30, 0x40));
    gp0(XY(100, 110));
    gp0(UV(16, 32, 0x0040));
    gp0(XY(24, 12));
    CHECK_EQ(g_drawn_count, 1);
    prim = &g_drawn_prims[0];
    CHECK_EQ(prim->kind, PORT_REMASTER_RECT);
    CHECK_EQ(prim->rect_w, 24);
    CHECK_EQ(prim->rect_h, 12);
    CHECK_EQ(prim->corner[0].ix, 100);
    CHECK_EQ(prim->corner[0].iy, 110);
    CHECK_EQ(prim->corner[0].u, 16);
    CHECK_EQ(prim->corner[0].v, 32);
    CHECK_EQ(prim->is_half, TRUE);
    CHECK_EQ(prim->is_textured, TRUE);
    CHECK_EQ(prim->has_depth, FALSE);
    /* a rectangle of one of the three fixed sizes */
    g_drawn_count = 0;
    gp0(CODE(0x70) | RGB(1, 2, 3));
    gp0(XY(10, 10));
    CHECK_EQ(g_drawn_count, 1);
    CHECK_EQ(g_drawn_prims[0].rect_w, 8);
    CHECK_EQ(g_drawn_prims[0].rect_h, 8);
    /* a shaded triangle: a colour for each corner, and the dither */
    g_drawn_count = 0;
    gp0(0xE1000200); /* the drawing mode, dithering on */
    gp0(CODE(0x30) | RGB(10, 20, 30));
    gp0(XY(1, 2));
    gp0(RGB(40, 50, 60));
    gp0(XY(3, 4));
    gp0(RGB(70, 80, 90));
    gp0(XY(5, 6));
    CHECK_EQ(g_drawn_count, 1);
    prim = &g_drawn_prims[0];
    CHECK_EQ(prim->is_shaded, TRUE);
    CHECK_EQ(prim->is_dithered, TRUE);
    CHECK_EQ(prim->corner[0].r, 10);
    CHECK_EQ(prim->corner[1].r, 40);
    CHECK_EQ(prim->corner[2].b, 90);
    /* a flat polygon is never dithered */
    g_drawn_count = 0;
    gp0(CODE(0x20) | RGB(10, 20, 30));
    gp0(XY(1, 2));
    gp0(XY(3, 4));
    gp0(XY(5, 6));
    CHECK_EQ(g_drawn_prims[0].is_dithered, FALSE);
    /* the drawing offset is in every corner */
    g_drawn_count = 0;
    gp0(0xE5000000 | (7 << 11) | 5); /* 5 across, 7 down */
    gp0(CODE(0x20) | RGB(10, 20, 30));
    gp0(XY(1, 2));
    gp0(XY(3, 4));
    gp0(XY(5, 6));
    CHECK_EQ(g_drawn_prims[0].corner[0].ix, 6);
    CHECK_EQ(g_drawn_prims[0].corner[0].iy, 9);
    CHECK_EQ(g_drawn_prims[0].corner[0].x, 6 << 16);
    gp0(0xE5000000);

    /* a fill and a copy are not primitives: the backend is told which
     * pixels of video memory they changed */
    g_writes = 0;
    gp0(CODE(0x02) | RGB(0, 0, 0));
    gp0(XY(320, 100));
    gp0(XY(64, 16));
    CHECK_EQ(g_writes, 1);
    CHECK_EQ(g_wrote.x, 320);
    CHECK_EQ(g_wrote.y, 100);
    CHECK_EQ(g_wrote.w, 64);
    CHECK_EQ(g_wrote.h, 16);
    gp0(CODE(0x80));
    gp0(XY(0, 0));
    gp0(XY(700, 300));
    gp0(XY(8, 4));
    CHECK_EQ(g_writes, 2);
    CHECK_EQ(g_wrote.x, 700);
    CHECK_EQ(g_wrote.y, 300);
    CHECK_EQ(g_wrote.w, 8);
    CHECK_EQ(g_wrote.h, 4);
    /* a load from the program: when its last word has come */
    g_writes = 0;
    gp0(CODE(0xA0));
    gp0(XY(16, 32));
    gp0(XY(2, 2));
    CHECK_EQ(g_writes, 0);
    gp0(0x11112222);
    CHECK_EQ(g_writes, 0);
    gp0(0x33334444);
    CHECK_EQ(g_writes, 1);
    CHECK_EQ(g_wrote.x, 16);
    CHECK_EQ(g_wrote.y, 32);
    CHECK_EQ(g_wrote.w, 2);
    CHECK_EQ(g_wrote.h, 2);
}

/* With the original picture the backend is never called, and with no
 * backend nothing is written down at all. */
static void test_Original(void) {
    const PortRemasterCounts *counts = port_GetRemasterCounts();
    u32 before;

    test_Reset();
    port_SetGraphics(PORT_GRAPHICS_ORIGINAL);
    g_drawn_count = 0;
    g_writes = 0;
    gp0(CODE(0x20) | RGB(10, 20, 30));
    gp0(XY(1, 2));
    gp0(XY(3, 4));
    gp0(XY(5, 6));
    gp0(CODE(0x02) | RGB(0, 0, 0));
    gp0(XY(320, 100));
    gp0(XY(64, 16));
    CHECK_EQ(g_drawn_count, 0);
    CHECK_EQ(g_writes, 0);
    /* the commands are still counted, so that the two paths can be
     * held to each other */
    CHECK(counts->seen[PORT_REMASTER_COUNT_F3] > 0);

    port_RemasterSetBackend(NULL);
    before = counts->points;
    test_NotePoint((PortAddress)g_points, 1 << 16, 1 << 16, 0x100);
    CHECK_EQ(counts->points, before);
    g_drawn_count = 0;
    gp0(CODE(0x20) | RGB(10, 20, 30));
    gp0(XY(1, 2));
    gp0(XY(3, 4));
    gp0(XY(5, 6));
    CHECK_EQ(g_drawn_count, 0);
}

void test_Main(void) {
    test_Which();
    test_Dirty();
    test_Scale();
    test_View();
    test_Margin();
    test_Limits();
    test_Side();
    test_SideInPrimitive();
    test_Identity();
    test_Shadow();
    test_Frames();
    test_Anchor();
    test_Commands();
    test_Original();
}
