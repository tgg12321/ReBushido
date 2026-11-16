/* "Graphics: Remastered" for the native port (port_remaster.h;
 * docs/PORT.md, "Remastered"): the side channel that carries the
 * geometry the PlayStation threw away, and the decoder that turns a GPU
 * command back into a primitive a graphics card can draw.
 *
 * Nothing here draws and nothing here opens anything: it is arithmetic
 * and bookkeeping, so the host tests run all of it
 * (tests/port/remaster.c). With no backend attached — every build but
 * the one with a window and the one that proves it — every function
 * here returns at once, and the port is what it was.
 *
 * None of this changes a value the game computes: the game's memory is
 * read and never written. */
#include "common.h"
#include "port_gpu.h"
#include "bb2.h"

/* ---- the tables ---- */

/* Points the GTE projected, by the address its screen position was
 * stored at, and faces by the primitive they became. Both are open
 * tables: a key is looked for from its own slot onwards, over this many
 * slots, and what is found there from a frame long past counts as
 * nothing. A frame of the fight has about two thousand of each. */
#define POINT_SLOTS 8192
#define FACE_SLOTS 8192
/* The third table is the fight HUD's primitives that a wider picture
 * draws at its own edges (port_remaster.h): a frame has some forty. */
#define ANCHOR_SLOTS 256
#define PROBES 8

typedef struct {
    PortAddress at;
    u32 epoch;
    PortPrecisePoint point;
} PointSlot;

typedef struct {
    PortAddress prim;
    u32 epoch;
    s32 count;
    /* which face of which thing this primitive is a drawing of; its
     * `object` is 0 where the renderer said nothing */
    PortRemasterIdentity id;
    PortPrecisePoint point[PORT_REMASTER_CORNERS];
} FaceSlot;

typedef struct {
    PortAddress prim;
    u32 epoch;
    s32 anchor; /* PORT_ANCHOR_... */
} AnchorSlot;

/* A frame's own primitives are built after the frame before it was
 * drawn, so a point or a face is used in the frame it was written down
 * in. The frame before that one is kept as well, because the game has
 * two primitive buffers and two ordering tables and uses them on
 * alternate frames: an address cannot be written again inside one
 * frame's reach. */
#define EPOCHS_KEPT 2

static PointSlot g_point_slots[POINT_SLOTS];
static FaceSlot g_face_slots[FACE_SLOTS];
static AnchorSlot g_anchor_slots[ANCHOR_SLOTS];
static u32 g_epoch = EPOCHS_KEPT;
/* What the renderer is drawing now, and which piece of it comes next:
 * the identity every primitive linked from here on is given. */
static PortRemasterIdentity g_id;

/* Which edge of a wider picture the primitives now being built belong to
 * (port_remaster.h). */
static s32 g_anchor;

/* The face the renderer last looked up, waiting for the primitive it
 * becomes. */
static PortPrecisePoint g_face[PORT_REMASTER_CORNERS];
static s32 g_face_count;
/* The primitive whose words the GPU is being given (DrawOTag). */
static PortAddress g_prim;

static const PortRemasterBackend *g_backend;
static s32 g_graphics = PORT_GRAPHICS_FOLLOW;
static PortRemasterCounts g_counts;
static PortRemasterCounts g_frame_counts;
/* ... and the frame last shown, which is what a run reports. */
static PortRemasterCounts g_shown_counts;
/* What this frame has drawn into, in video memory's own pixels: a
 * texture read from inside it would be read from pixels the software
 * GPU drew, which the backend's mirror of video memory does not have.
 * Counted, not corrected. */
static PortDirtyRect g_drawn;

/* ---- the numbers of the chip (src/port/raster.c has the same) ---- */

#define COMMAND_SHIFT 24
#define COMMAND_FILL 0x02
#define COMMAND_POLYGONS 0x20
#define COMMAND_LINES 0x40
#define COMMAND_RECTANGLES 0x60
#define COMMAND_COPY 0x80
#define COMMAND_LOAD 0xA0
#define COMMAND_STORE 0xC0
#define COMMAND_SETTINGS 0xE0
#define COMMAND_KIND_MASK 0xE0
#define CODE_RAW 0x01
#define CODE_HALF 0x02
#define CODE_TEXTURED 0x04
#define CODE_QUAD 0x08
#define CODE_MANY 0x08
#define CODE_SHADED 0x10
#define CODE_SIZE_MASK 0x18
#define CODE_SIZE_SHIFT 3
#define CODE_SIZE_OWN 0

#define HALF_SHIFT 16
#define HALF_MASK 0xFFFF
#define BYTE_MASK 0xFF
#define BITS_PER_BYTE 8
#define GREEN_SHIFT 8
#define BLUE_SHIFT 16
#define POSITION_BITS 11
#define POSITION_SIGN (1 << (POSITION_BITS - 1))
#define POSITION_MASK ((1 << POSITION_BITS) - 1)
/* the two places the GTE holds a screen position at (src/port/gte.c) */
#define POSITION_HIGH (POSITION_SIGN - 1)
#define POSITION_LOW (-POSITION_SIGN)
/* How far outside those a place may be and still be a point in front of
 * the camera. A point behind it has a place tens of thousands of pixels
 * out, because the GTE's divide saturates; four times the width of video
 * memory is far more than any picture is wide, and a polygon with a
 * corner further out than that is a projection that came apart
 * (port_PlaceIsNear). */
#define PLACE_FAR_MAX (4 * PORT_VRAM_WIDTH)
#define VRAM_X_MASK (PORT_VRAM_WIDTH - 1)
#define VRAM_Y_MASK (PORT_VRAM_HEIGHT - 1)
#define FILL_X_MASK 0x3F0
#define FILL_STEP 16

#define MODE_PAGE_X_MASK 0xF
#define MODE_PAGE_X_STEP 64
#define MODE_PAGE_Y_BIT 0x10
#define MODE_PAGE_Y_STEP 256
#define MODE_BLEND_SHIFT 5
#define MODE_BLEND_MASK 3
#define MODE_DEPTH_SHIFT 7
#define MODE_DEPTH_MASK 3
#define MODE_DITHER 0x200
#define MODE_POLYGON 0x1FF
#define CLUT_X_MASK 0x3F
#define CLUT_X_STEP 16
#define CLUT_Y_SHIFT 6

/* A rectangle's size, by the two bits of its code. */
static const s32 g_rect_sizes[] = {0, 1, 8, 16};

/* ---- how much is drawn (port_remaster.h) ---- */

/* The fog's reach, in 256ths of the console's, for each draw distance.
 * The console's fog ends where its disc of cells does, so a farther
 * stage needs a farther fog or the ground it draws is the fog's colour.
 * The colours themselves are the stage's own and are not touched: a
 * night stage stays as dark as it was, its gradient only reaching
 * further. `far` doubles it, which covers the wedge the camera looks
 * down; `max` is enough for the stage's diagonal (90,510 world units
 * against the console's 26,000). */
static const s32 g_fog_factors[PORT_DISTANCE_KINDS] = {
    PORT_FOG_FACTOR_ONE, 2 * PORT_FOG_FACTOR_ONE, 4 * PORT_FOG_FACTOR_ONE};

static const char * const g_distance_names[PORT_DISTANCE_KINDS] = {
    "original", "far", "max"};

static PortRemasterView g_view;
static PortRemasterLimits g_limits;
/* How far left of the console's screen this frame's picture reaches
 * (port_RemasterLeftMargin), worked out once a frame. */
static s32 g_left_margin;
/* Whether the port's own primitive buffers are the game's this run
 * (port_platform.h, PORT_PRIMS_ADDR). */
static s32 g_prim_buffers_are_the_games;

void port_RemasterDefaultView(PortRemasterView *view) {
    view->distance = PORT_DISTANCE_ORIGINAL;
    view->supersample = 1;
    view->aspect_w = PORT_ASPECT_W_ORIGINAL;
    view->aspect_h = PORT_ASPECT_H_ORIGINAL;
}

/* The greatest common divisor, for the picture's shape in its lowest
 * terms. */
static s32 port_GreatestDivisor(s32 first, s32 second) {
    while (second != 0) {
        s32 left = first % second;

        first = second;
        second = left;
    }
    return first;
}

/* A run's settings (port_remaster.h): each held to what there is, the
 * smoothing down to a power of two and the shape to its lowest terms.
 * It also settles whose primitive buffers the game is given, which it
 * asks for once as it starts. */
void port_RemasterSetView(const PortRemasterView *view) {
    s32 divisor;

    g_view = *view;
    if (g_view.distance < 0 || g_view.distance >= PORT_DISTANCE_KINDS) {
        g_view.distance = PORT_DISTANCE_ORIGINAL;
    }
    if (g_view.supersample < 1) {
        g_view.supersample = 1;
    }
    if (g_view.supersample > PORT_SUPERSAMPLE_MAX) {
        g_view.supersample = PORT_SUPERSAMPLE_MAX;
    }
    /* Down to a power of two: only those leave the 2D layer where it was
     * (docs/PORT.md, "Remastered", "Edges"), and three was measured to
     * move 0.7% of a flat picture's pixels. */
    while (g_view.supersample & (g_view.supersample - 1)) {
        g_view.supersample--;
    }
    if (g_view.aspect_w <= 0 || g_view.aspect_h <= 0) {
        g_view.aspect_w = PORT_ASPECT_W_ORIGINAL;
        g_view.aspect_h = PORT_ASPECT_H_ORIGINAL;
    }
    divisor = port_GreatestDivisor(g_view.aspect_w, g_view.aspect_h);
    g_view.aspect_w /= divisor;
    g_view.aspect_h /= divisor;
    /* the game is given the port's own primitive buffers from now on,
     * which it asks for once as it starts (src/main/main.c) */
    g_prim_buffers_are_the_games = g_view.distance != PORT_DISTANCE_ORIGINAL;
}

void port_RemasterGetView(PortRemasterView *view) { *view = g_view; }

/* A draw distance by its name, or -1: the tests and the port's files
 * have no C library, so the letters are compared here. */
s32 port_ReadRemasterDistance(const char *word) {
    s32 i;
    s32 j;

    for (i = 0; i < PORT_DISTANCE_KINDS; i++) {
        const char *name = g_distance_names[i];

        for (j = 0; name[j] != 0 && name[j] == word[j]; j++) {
        }
        if (name[j] == 0 && word[j] == 0) {
            return i;
        }
    }
    return -1;
}

const char *port_NameRemasterDistance(s32 distance) {
    if (distance < 0 || distance >= PORT_DISTANCE_KINDS) {
        return "?";
    }
    return g_distance_names[distance];
}

s32 port_RemasterDistance(void) {
    if (!port_RemasterIsOn()) {
        return PORT_DISTANCE_ORIGINAL;
    }
    return g_view.distance;
}

s32 port_RemasterFogFactor(s32 distance) {
    if (distance < 0 || distance >= PORT_DISTANCE_KINDS) {
        return PORT_FOG_FACTOR_ONE;
    }
    return g_fog_factors[distance];
}

s32 port_RemasterNeedsFarShift(s32 distance) {
    return distance == PORT_DISTANCE_MAX;
}

/* How much wider than the display's own rectangle the picture is, each
 * side (port_remaster.h). */
s32 port_RemasterMargin(s32 picture_w, s32 aspect_w, s32 aspect_h) {
    s32 wide;
    s32 margin;

    if (picture_w <= 0 || aspect_w <= 0 || aspect_h <= 0 ||
        aspect_w * PORT_ASPECT_H_ORIGINAL <=
            aspect_h * PORT_ASPECT_W_ORIGINAL) {
        return 0;
    }
    /* the width the shape asks for, in the same measure as the picture's
     * own: w / h over 4 / 3 times it */
    wide = (picture_w * aspect_w * PORT_ASPECT_H_ORIGINAL) /
           (aspect_h * PORT_ASPECT_W_ORIGINAL);
    if (wide > PORT_VRAM_WIDTH) {
        wide = PORT_VRAM_WIDTH;
    }
    margin = (wide - picture_w) / 2;
    return margin < 0 ? 0 : margin;
}

u32 *port_RemasterPrimBuffer(s32 which) {
    if (!g_prim_buffers_are_the_games || which < 0 || which > 1) {
        return NULL;
    }
    return (u32 *)ADDR(PORT_PRIMS_ADDR + (u32)which * PORT_PRIM_BUFFER_BYTES);
}

u32 port_RemasterPrimBufferBytes(void) {
    return g_prim_buffers_are_the_games ? PORT_PRIM_BUFFER_BYTES : 0;
}

/* ---- the limits a run came near (port_remaster.h) ---- */

void port_RemasterNoteRoom(s32 prim_free, s32 prim_bytes) {
    if (g_limits.frames == 0 || prim_free < g_limits.prim_free) {
        g_limits.prim_free = prim_free;
    }
    g_limits.prim_bytes = prim_bytes;
    g_limits.frames++;
}

/* What a frame queued of the stage, and what that left of the
 * renderer's queue (port_remaster.h). */
void port_RemasterNoteCells(s32 tiles, s32 nodes, s32 queue_size) {
    s32 queue_free = queue_size - tiles - nodes;

    if ((u32)tiles > g_limits.tiles) {
        g_limits.tiles = (u32)tiles;
    }
    /* and the fewest any frame queued: with the whole stage drawn the
     * two are equal, which is what says that no tile comes and goes */
    if (g_limits.queue_size == 0 || (u32)tiles < g_limits.tiles_least) {
        g_limits.tiles_least = (u32)tiles;
    }
    if ((u32)nodes > g_limits.nodes) {
        g_limits.nodes = (u32)nodes;
    }
    if (g_limits.queue_size == 0 || queue_free < g_limits.queue_free) {
        g_limits.queue_free = queue_free;
    }
    g_limits.queue_size = queue_size;
}

void port_RemasterNoteOtSlot(s32 index) {
    if ((u32)index > g_limits.ot_farthest) {
        g_limits.ot_farthest = (u32)index;
    }
}

const PortRemasterLimits *port_GetRemasterLimits(void) {
    g_limits.side_full = g_counts.full;
    return &g_limits;
}

/* ---- which picture ---- */

void port_SetGraphics(s32 which) {
    if (which == PORT_GRAPHICS_ORIGINAL || which == PORT_GRAPHICS_REMASTERED ||
        which == PORT_GRAPHICS_FOLLOW) {
        g_graphics = which;
    }
}

s32 port_GetGraphics(void) { return g_graphics; }

void port_ToggleGraphics(void) {
    port_SetGraphics(port_RemasterIsOn() ? PORT_GRAPHICS_ORIGINAL
                                         : PORT_GRAPHICS_REMASTERED);
}

s32 port_RemasterIsOn(void) {
    if (g_backend == NULL) {
        return FALSE;
    }
    if (g_graphics == PORT_GRAPHICS_FOLLOW) {
        return g_rb_settings.graphics == RB_GRAPHICS_ENHANCED;
    }
    return g_graphics == PORT_GRAPHICS_REMASTERED;
}

void port_RemasterSetBackend(const PortRemasterBackend *backend) {
    g_backend = backend;
}

const PortRemasterBackend *port_RemasterGetBackend(void) { return g_backend; }

const PortRemasterCounts *port_GetRemasterCounts(void) { return &g_counts; }

const PortRemasterCounts *port_GetRemasterFrameCounts(void) {
    return &g_shown_counts;
}

const char *port_NameRemasterKind(s32 kind) {
    static const char * const names[PORT_REMASTER_COUNT_KINDS] = {
        "F3",   "FT3",  "G3",   "GT3",  "F4",   "FT4",   "G4",     "GT4",
        "line", "rect", "fill", "copy", "load", "store", "setting"};

    if (kind < 0 || kind >= PORT_REMASTER_COUNT_KINDS) {
        return "?";
    }
    return names[kind];
}

/* ---- the two tables ---- */

/* The odd number a key is multiplied by to mix its bits: two to the
 * 32nd over the golden ratio, which is the usual choice. */
#define MIX 0x9E3779B1U

/* A slot to start looking in: the address's own bits, mixed so that
 * neighbouring addresses are not neighbouring slots. */
static u32 port_RemasterSlot(PortAddress at, u32 slots) {
    u32 key = (u32)(at >> 2);

    key ^= key >> 13;
    key *= MIX;
    key ^= key >> 15;
    return key & (slots - 1);
}

static s32 port_EpochIsLive(u32 epoch) {
    return epoch != 0 && epoch + EPOCHS_KEPT > g_epoch;
}

/* The GTE stored a projected point at `at` (port_remaster.h): it is
 * written down under that address, over whatever is there from a frame
 * long past. */
void port_RemasterNotePoint(PortAddress at, const PortPrecisePoint *point) {
    u32 slot;
    u32 i;

    if (g_backend == NULL) {
        return;
    }
    slot = port_RemasterSlot(at, POINT_SLOTS);
    for (i = 0; i < PROBES; i++) {
        PointSlot *found = &g_point_slots[(slot + i) & (POINT_SLOTS - 1)];

        if (found->at == at || !port_EpochIsLive(found->epoch)) {
            found->at = at;
            found->epoch = g_epoch;
            found->point = *point;
            g_counts.points++;
            g_frame_counts.points++;
            return;
        }
    }
    /* every slot it could have is another live point's: the oldest of
     * them goes, and something will be drawn flat */
    g_point_slots[slot].at = at;
    g_point_slots[slot].epoch = g_epoch;
    g_point_slots[slot].point = *point;
    g_counts.points++;
    g_frame_counts.points++;
    g_counts.full++;
    g_frame_counts.full++;
}

/* The point written down for an address, or NULL. */
static const PortPrecisePoint *port_FindPoint(PortAddress at) {
    u32 slot = port_RemasterSlot(at, POINT_SLOTS);
    u32 i;

    for (i = 0; i < PROBES; i++) {
        const PointSlot *found = &g_point_slots[(slot + i) & (POINT_SLOTS - 1)];

        if (found->at == at && port_EpochIsLive(found->epoch)) {
            return &found->point;
        }
    }
    return NULL;
}

/* The renderer looked a face's corners up (port_remaster.h): what the
 * projection worked out for each is kept until the primitive it
 * becomes is linked. A corner the table does not have leaves a point
 * of no depth, and the primitive is drawn flat. */
void port_RemasterNoteFace(PortAddress points, const s32 *index, s32 count) {
    s32 i;

    if (g_backend == NULL) {
        return;
    }
    if (count > PORT_REMASTER_CORNERS) {
        count = PORT_REMASTER_CORNERS;
    }
    g_face_count = count;
    /* this face of the mesh, by the corners it is made of, and its
     * pieces start again (port_remaster.h) */
    g_id.face = 0;
    g_id.piece = 0;
    for (i = 0; i < count; i++) {
        g_id.face |= (s32)((u32)(index[i] & BYTE_MASK) << (i * BITS_PER_BYTE));
    }
    for (i = 0; i < count; i++) {
        const PortPrecisePoint *found =
            port_FindPoint(points + (PortAddress)((u32)index[i] * sizeof(u32)));

        if (found != NULL) {
            g_face[i] = *found;
        } else {
            g_face[i].x = 0;
            g_face[i].y = 0;
            g_face[i].depth = 0;
            g_face[i].divided = 0;
            g_counts.point_misses++;
            g_frame_counts.point_misses++;
        }
    }
    g_counts.faces++;
    g_frame_counts.faces++;
}

/* That face became the primitive at `prim` (port_remaster.h). */
void port_RemasterAttachFace(PortAddress prim) {
    u32 slot;
    u32 i;
    s32 corner;

    if (g_backend == NULL) {
        return;
    }
    /* A primitive with no face of its own is written down all the same,
     * for its identity: a tile of the stage has no face looked up --
     * the GTE stored its corners into the primitive itself -- and it is
     * a tile that most needs to be found again in the next frame. */
    slot = port_RemasterSlot(prim, FACE_SLOTS);
    for (i = 0; i <= PROBES; i++) {
        FaceSlot *found = &g_face_slots[(slot + i) & (FACE_SLOTS - 1)];

        if (i == PROBES) {
            /* as the points' table: the oldest of the slots it could
             * have goes */
            found = &g_face_slots[slot];
            g_counts.full++;
            g_frame_counts.full++;
        } else if (found->prim != prim && port_EpochIsLive(found->epoch)) {
            continue;
        }
        found->prim = prim;
        found->epoch = g_epoch;
        found->count = g_face_count;
        found->id = g_id;
        g_id.piece++;
        for (corner = 0; corner < g_face_count; corner++) {
            found->point[corner] = g_face[corner];
        }
        if (g_face_count != 0) {
            g_counts.attached++;
            g_frame_counts.attached++;
        }
        return;
    }
}

void port_RemasterNoteObject(PortAddress object) {
    g_id.object = object;
    g_id.group = 0;
    g_id.face = 0;
    g_id.piece = 0;
    /* no face has been looked up for what is about to be drawn: a
     * primitive linked before one is has corners of its own */
    g_face_count = 0;
}

void port_RemasterNoteGroup(PortAddress group) {
    g_id.group = group;
    g_id.face = 0;
    g_id.piece = 0;
    g_face_count = 0;
}

/* The face attached to a primitive, or NULL. */
static const FaceSlot *port_FindFace(PortAddress prim) {
    u32 slot = port_RemasterSlot(prim, FACE_SLOTS);
    u32 i;

    if (prim == 0) {
        return NULL;
    }
    for (i = 0; i < PROBES; i++) {
        const FaceSlot *found = &g_face_slots[(slot + i) & (FACE_SLOTS - 1)];

        if (found->prim == prim && port_EpochIsLive(found->epoch)) {
            return found;
        }
    }
    return NULL;
}

/* ---- the fight's HUD at the picture's edges (port_remaster.h) ---- */

void port_RemasterSetAnchor(s32 anchor) { g_anchor = anchor; }

/* A primitive of the fight's HUD, written down under its address with
 * the edge the game said it belongs to (port_remaster.h). */
void port_RemasterNoteAnchored(PortAddress prim) {
    u32 slot;
    u32 i;

    if (g_backend == NULL || g_anchor == PORT_ANCHOR_MIDDLE) {
        return;
    }
    slot = port_RemasterSlot(prim, ANCHOR_SLOTS);
    for (i = 0; i <= PROBES; i++) {
        AnchorSlot *found = &g_anchor_slots[(slot + i) & (ANCHOR_SLOTS - 1)];

        if (i == PROBES) {
            /* as the faces' table: the oldest of the slots it could have
             * goes, and a counter of the HUD is drawn where the console
             * has it */
            found = &g_anchor_slots[slot];
            g_counts.full++;
            g_frame_counts.full++;
        } else if (found->prim != prim && port_EpochIsLive(found->epoch)) {
            continue;
        }
        found->prim = prim;
        found->epoch = g_epoch;
        found->anchor = g_anchor;
        return;
    }
}

/* Which edge the primitive at `prim` was built for, or
 * PORT_ANCHOR_MIDDLE, which is everything but the fight HUD's own. */
static s32 port_FindAnchor(PortAddress prim) {
    u32 slot;
    u32 i;

    if (prim == 0) {
        return PORT_ANCHOR_MIDDLE;
    }
    slot = port_RemasterSlot(prim, ANCHOR_SLOTS);
    for (i = 0; i < PROBES; i++) {
        const AnchorSlot *found =
            &g_anchor_slots[(slot + i) & (ANCHOR_SLOTS - 1)];

        if (found->prim == prim && port_EpochIsLive(found->epoch)) {
            return found->anchor;
        }
    }
    return PORT_ANCHOR_MIDDLE;
}

/* The left margin for this frame (port_remaster.h): the picture's own
 * shape against the display's rectangle, or 0 where the original picture
 * is shown. It is settled at the top of a frame and not touched again, so
 * that every face of that frame is cut off at the same edge. */
static void port_RemasterFindLeftMargin(void) {
    PortDisplay display;

    if (!port_RemasterIsOn()) {
        g_left_margin = 0;
        return;
    }
    port_GetDisplay(&display);
    g_left_margin =
        port_RemasterMargin(display.w, g_view.aspect_w, g_view.aspect_h);
}

s32 port_RemasterLeftMargin(void) { return g_left_margin; }

/* The frame the side channel is writing down begins (port_remaster.h):
 * the game has cleared this frame's ordering table and is about to build
 * its primitives. What the picture's own edges are is settled here. */
void port_RemasterNewFrame(void) {
    port_RemasterFindLeftMargin();
    g_anchor = PORT_ANCHOR_MIDDLE;
    if (g_backend == NULL) {
        return;
    }
    g_epoch++;
    g_face_count = 0;
    /* nothing of the scene is being drawn yet: what the game builds
     * before the renderer's walk is the 2D layer, which has no identity
     * and is never moved */
    port_RemasterNoteObject(0);
    port_ClearDirtyRect(&g_drawn);
    if (port_RemasterIsOn() && g_backend->begin != NULL) {
        g_backend->begin();
    }
}

void port_RemasterSetPrim(PortAddress prim) { g_prim = prim; }

/* ---- the rectangle of video memory that changed ---- */

void port_ClearDirtyRect(PortDirtyRect *rect) {
    rect->left = 0;
    rect->top = 0;
    rect->right = 0;
    rect->bottom = 0;
    rect->is_empty = TRUE;
}

/* A rectangle of video memory that changed, taken into `rect`
 * (port_remaster.h). */
void port_AddDirtyRect(PortDirtyRect *rect, s32 x, s32 y, s32 w, s32 h) {
    s32 left = x & VRAM_X_MASK;
    s32 top = y & VRAM_Y_MASK;
    s32 right = left + w - 1;
    s32 bottom = top + h - 1;

    if (w <= 0 || h <= 0) {
        return;
    }
    /* a rectangle that goes round an edge is taken as the whole of that
     * way: the mirror is sent as one rectangle */
    if (right > PORT_VRAM_WIDTH - 1) {
        left = 0;
        right = PORT_VRAM_WIDTH - 1;
    }
    if (bottom > PORT_VRAM_HEIGHT - 1) {
        top = 0;
        bottom = PORT_VRAM_HEIGHT - 1;
    }
    if (rect->is_empty) {
        rect->left = left;
        rect->top = top;
        rect->right = right;
        rect->bottom = bottom;
        rect->is_empty = FALSE;
        return;
    }
    if (left < rect->left) {
        rect->left = left;
    }
    if (top < rect->top) {
        rect->top = top;
    }
    if (right > rect->right) {
        rect->right = right;
    }
    if (bottom > rect->bottom) {
        rect->bottom = bottom;
    }
}

s32 port_DirtyRectPixels(const PortDirtyRect *rect) {
    if (rect->is_empty) {
        return 0;
    }
    return (rect->right - rect->left + 1) * (rect->bottom - rect->top + 1);
}

/* ---- the picture's size ---- */

s32 port_RemasterScaleFor(
    s32 window_w, s32 window_h, s32 picture_w, s32 picture_h) {
    s32 across;
    s32 down;
    s32 scale;

    if (picture_w <= 0 || picture_h <= 0 || window_w <= 0 || window_h <= 0) {
        return 1;
    }
    /* a line of the console's picture is two of the window's: 240 lines
     * fill a screen of four by three */
    across = (window_w + picture_w - 1) / picture_w;
    down = (window_h + 2 * picture_h - 1) / (2 * picture_h);
    scale = across > down ? across : down;
    if (scale < 1) {
        scale = 1;
    }
    if (scale > PORT_REMASTER_SCALE_MAX) {
        scale = PORT_REMASTER_SCALE_MAX;
    }
    return scale;
}

/* ---- a command, decoded ---- */

static s32 port_RemasterPosition(s32 value) {
    value &= POSITION_MASK;
    return (value ^ POSITION_SIGN) - POSITION_SIGN;
}

/* A copy's width or height, as the chip takes it: 0 is all of video
 * memory that way (src/port/raster.c has the same). */
static s32 port_RemasterTransferSize(u32 value, s32 mask) {
    return (s32)((value - 1) & (u32)mask) + 1;
}

/* Which of PORT_REMASTER_COUNT_... a command is counted under. */
static s32 port_KindOfCommand(u32 code) {
    switch (code & COMMAND_KIND_MASK) {
    case COMMAND_POLYGONS:
        return (
            ((code & CODE_QUAD) ? PORT_REMASTER_COUNT_F4
                                : PORT_REMASTER_COUNT_F3) +
            ((code & CODE_SHADED) ? 2 : 0) + ((code & CODE_TEXTURED) ? 1 : 0));
    case COMMAND_LINES:
        return PORT_REMASTER_COUNT_LINE;
    case COMMAND_RECTANGLES:
        return PORT_REMASTER_COUNT_RECT;
    case COMMAND_COPY:
        return PORT_REMASTER_COUNT_COPY;
    case COMMAND_LOAD:
        return PORT_REMASTER_COUNT_LOAD;
    case COMMAND_STORE:
        return PORT_REMASTER_COUNT_STORE;
    case COMMAND_SETTINGS:
        return PORT_REMASTER_COUNT_SETTING;
    default:
        return PORT_REMASTER_COUNT_FILL;
    }
}

static void port_RemasterCornerColor(PortRemasterCorner *corner, u32 color) {
    corner->r = (s32)(color & BYTE_MASK);
    corner->g = (s32)((color >> GREEN_SHIFT) & BYTE_MASK);
    corner->b = (s32)((color >> BLUE_SHIFT) & BYTE_MASK);
}

/* Where the primitive whose words are being drawn is in the frame's
 * primitive buffer, in bytes, or -1 (port_remaster.h). The frame being
 * drawn is the one the game has just built: DrawOTag comes before
 * g_disp_frame_count is counted on (src/main/main.c). */
static s32 port_RemasterPrimKey(void) {
    u32 base;
    u32 offset;

    if (g_prim == 0) {
        return -1;
    }
    base = g_disp_prim_buffers[g_disp_frame_count & 1];
    offset = (u32)g_prim - base;
    if (base == 0 || offset >= g_disp_prim_buffer_size) {
        return -1;
    }
    return (s32)offset;
}

/* The paint of a primitive: the drawing mode as it is now, with the
 * page a textured polygon brought with it already in it (the software
 * GPU drew the command before this is called). */
static void port_RemasterPaint(
    PortRemasterPrim *prim, u32 code, u32 clut, const PortGpuState *state) {
    prim->is_shaded = (code & CODE_SHADED) != 0;
    prim->is_textured = (code & CODE_TEXTURED) != 0;
    prim->is_raw = (code & CODE_RAW) != 0;
    prim->is_half = (code & CODE_HALF) != 0;
    prim->is_dithered = 0;
    prim->blend = (s32)((state->mode >> MODE_BLEND_SHIFT) & MODE_BLEND_MASK);
    prim->texel_bits =
        (s32)((state->mode >> MODE_DEPTH_SHIFT) & MODE_DEPTH_MASK);
    prim->page_x = (s32)(state->mode & MODE_PAGE_X_MASK) * MODE_PAGE_X_STEP;
    prim->page_y = (state->mode & MODE_PAGE_Y_BIT) ? MODE_PAGE_Y_STEP : 0;
    prim->clut_x = (s32)(clut & CLUT_X_MASK) * CLUT_X_STEP;
    prim->clut_y = (s32)((clut >> CLUT_Y_SHIFT) & VRAM_Y_MASK);
    prim->window_and_u = state->window_and_u;
    prim->window_or_u = state->window_or_u;
    prim->window_and_v = state->window_and_v;
    prim->window_or_v = state->window_or_v;
    prim->clip_left = state->clip_left;
    prim->clip_top = state->clip_top;
    prim->clip_right = state->clip_right;
    prim->clip_bottom = state->clip_bottom;
    prim->mask_set = state->mask_set;
    prim->mask_check = state->mask_check;
    prim->offset_x = state->offset_x;
    prim->offset_y = state->offset_y;
    prim->key = port_RemasterPrimKey();
}

/* What this primitive covers of video memory, for the count of textures
 * read from pixels the software GPU drew. */
static void port_NoteDrawn(const PortRemasterPrim *prim) {
    s32 left = prim->corner[0].ix;
    s32 right = left;
    s32 top = prim->corner[0].iy;
    s32 bottom = top;
    s32 i;

    for (i = 1; i < prim->corner_count; i++) {
        s32 x = prim->corner[i].ix;
        s32 y = prim->corner[i].iy;

        left = x < left ? x : left;
        right = x > right ? x : right;
        top = y < top ? y : top;
        bottom = y > bottom ? y : bottom;
    }
    if (prim->kind == PORT_REMASTER_RECT) {
        right = left + prim->rect_w - 1;
        bottom = top + prim->rect_h - 1;
    }
    /* nothing is drawn outside the drawing area */
    if (left < prim->clip_left) {
        left = prim->clip_left;
    }
    if (top < prim->clip_top) {
        top = prim->clip_top;
    }
    if (right > prim->clip_right) {
        right = prim->clip_right;
    }
    if (bottom > prim->clip_bottom) {
        bottom = prim->clip_bottom;
    }
    if (right < left || bottom < top) {
        return;
    }
    port_AddDirtyRect(&g_drawn, left, top, right - left + 1, bottom - top + 1);
}

/* Whether a textured primitive reads a page that this frame drew into. */
static s32 port_TextureIsDrawn(const PortRemasterPrim *prim) {
    s32 right;
    s32 bottom;

    if (!prim->is_textured || g_drawn.is_empty) {
        return FALSE;
    }
    right = prim->page_x + MODE_PAGE_X_STEP * 4 - 1;
    bottom = prim->page_y + MODE_PAGE_Y_STEP - 1;
    return prim->page_x <= g_drawn.right && right >= g_drawn.left &&
           prim->page_y <= g_drawn.bottom && bottom >= g_drawn.top;
}

/* A decoded primitive to the backend, counted. */
static void port_RemasterDrawPrim(PortRemasterPrim *prim, s32 kind) {
    if (prim->has_depth) {
        g_counts.with_depth++;
        g_frame_counts.with_depth++;
    } else {
        g_counts.without_depth++;
        g_frame_counts.without_depth++;
    }
    if (prim->has_place) {
        g_counts.with_place++;
        g_frame_counts.with_place++;
    }
    if (prim->is_half) {
        g_counts.mixed[prim->blend & 3]++;
        g_frame_counts.mixed[prim->blend & 3]++;
    }
    if (prim->mask_set != 0 || prim->mask_check != 0) {
        g_counts.masked++;
        g_frame_counts.masked++;
    }
    if (port_TextureIsDrawn(prim)) {
        g_counts.texture_from_drawn++;
        g_frame_counts.texture_from_drawn++;
    }
    g_counts.drawn[kind]++;
    g_frame_counts.drawn[kind]++;
    if (g_backend->draw != NULL) {
        g_backend->draw(prim);
    }
    port_NoteDrawn(prim);
}

/* Whether the place the projection worked out is the place the chip
 * drew at, within a pixel: a point the projection put behind the camera
 * has a screen position the chip held to eleven bits and this one did
 * not, and drawing at it would be a band across the picture. The chip's
 * own whole pixel is what says which primitives are drawn at all
 * (gl_draw.c, port_GlIsDrawn), so a place that is not near it is not
 * used.
 *
 * `stored` is the half word the GTE wrote, before the drawing offset:
 * the GTE keeps a screen position in eleven bits with a sign and holds
 * at that limit what the projection put further out (src/port/gte.c,
 * SCREEN_MIN and SCREEN_MAX). A point held there has a place that goes
 * on past the whole pixel with nothing wrong at all -- it is a point
 * off the side of the console's screen, which is the very thing a wider
 * picture shows -- so that place is kept, as long as the point is in
 * front of the camera and not far enough out to be a projection that
 * came apart. Without this every polygon with a corner more than 1023
 * pixels out was drawn squashed against that limit or not at all, which
 * is what emptied the bands of a wider picture (docs/PORT.md, "A wider
 * picture"). */
static s32 port_PlaceIsNear(
    s32 precise, s32 offset, s32 whole, s32 stored, s32 depth) {
    s32 at = precise >> PORT_REMASTER_FRACTION;
    s32 apart = (at + offset) - whole;

    if (apart >= -1 && apart <= 1) {
        return TRUE;
    }
    if (stored == POSITION_HIGH) {
        return depth > 0 && at > POSITION_HIGH && at <= PLACE_FAR_MAX;
    }
    if (stored == POSITION_LOW) {
        return depth > 0 && at < POSITION_LOW && at >= -PLACE_FAR_MAX;
    }
    return FALSE;
}

/* Whether a point the side channel wrote down is this corner's own: the
 * place it worked out is the place the chip drew the corner at.
 * `stored_x` and `stored_y` are the halves the GTE wrote, before the
 * drawing offset (port_PlaceIsNear). */
static s32 port_PointFits(
    const PortPrecisePoint *point, const PortGpuState *state,
    const PortRemasterCorner *corner, s32 stored_x, s32 stored_y) {
    return point != NULL &&
           port_PlaceIsNear(
               point->x, state->offset_x, corner->ix, stored_x, point->depth) &&
           port_PlaceIsNear(
               point->y, state->offset_y, corner->iy, stored_y, point->depth);
}

/* A polygon: three corners or four, each a place and, by the code, a
 * colour and a texture point. The side channel gives each corner where
 * the GTE really put it and how deep it is: by the face the renderer
 * attached to this primitive, or, for a primitive whose corners the GTE
 * stored into it itself (a stage tile that was cut), by the address of
 * the word the corner is in. It gives the primitive its name as well --
 * which face of which thing it is a drawing of -- which the pictures
 * between two frames pair by (gl_tween.c). */
static void port_DecodePolygon(
    const u32 *words, u32 code, const PortGpuState *state) {
    PortRemasterPrim prim;
    const FaceSlot *slot = port_FindFace(g_prim);
    const FaceSlot *face = slot;
    s32 corner_count = (code & CODE_QUAD) ? 4 : 3;
    u32 clut = 0;
    s32 at = 0;
    s32 i;

    prim.kind =
        (corner_count == 4) ? PORT_REMASTER_QUAD : PORT_REMASTER_TRIANGLE;
    prim.corner_count = corner_count;
    prim.rect_w = 0;
    prim.rect_h = 0;
    prim.has_depth = TRUE;
    prim.has_place = TRUE;
    /* which face of which thing this is a drawing of: the primitive's
     * own, whether or not the face's points are this primitive's
     * corners (a tile of the stage has an identity and no points) */
    if (slot != NULL) {
        prim.id = slot->id;
    } else {
        prim.id.object = 0;
        prim.id.group = 0;
        prim.id.face = 0;
        prim.id.piece = 0;
    }
    /* the scene, which a wider picture is wider of itself */
    prim.anchor = PORT_ANCHOR_MIDDLE;
    if (face != NULL && face->count != corner_count) {
        face = NULL;
    }
    for (i = 0; i < corner_count; i++) {
        PortRemasterCorner *corner = &prim.corner[i];
        const PortPrecisePoint *point = NULL;
        s32 place;
        s32 stored_x;
        s32 stored_y;

        if (i == 0 || (code & CODE_SHADED)) {
            port_RemasterCornerColor(corner, words[at++]);
        } else {
            port_RemasterCornerColor(corner, words[0]);
        }
        place = at++;
        /* the two halves the GTE wrote, before the drawing offset */
        stored_x = port_RemasterPosition((s32)words[place]);
        stored_y = port_RemasterPosition((s32)(words[place] >> HALF_SHIFT));
        corner->ix = port_RemasterPosition(stored_x + state->offset_x);
        corner->iy = port_RemasterPosition(stored_y + state->offset_y);
        corner->u = 0;
        corner->v = 0;
        if (code & CODE_TEXTURED) {
            corner->u = (s32)(words[at] & BYTE_MASK);
            corner->v = (s32)((words[at] >> GREEN_SHIFT) & BYTE_MASK);
            if (i == 0) {
                clut = words[at] >> HALF_SHIFT;
            }
            at++;
        }
        if (face != NULL) {
            point = &face->point[i];
        }
        if (!port_PointFits(point, state, corner, stored_x, stored_y) &&
            g_prim != 0) {
            /* The word of the primitive this corner's place came in: the
             * words given to the GPU are the primitive's after its link.
             * This is where a primitive with no face of its own has its
             * points, and where one whose face is not its own has them
             * too: a stage tile cut into pieces is one face of the
             * renderer's, and each piece has the GTE store its own
             * corners into the primitive (render_SplitFT4). Taking the
             * whole tile's corners for a piece of it left a third of the
             * ground of a stage drawn at `max` with no place at all, so
             * that it stood still between two frames while the ground
             * about it moved, and the gaps that opened showed the colour
             * of the distance through the floor (docs/PORT.md, "More
             * pictures a second"). */
            const PortPrecisePoint *own = port_FindPoint(
                g_prim + (PortAddress)((u32)(place + 1) * sizeof(u32)));

            if (port_PointFits(own, state, corner, stored_x, stored_y)) {
                point = own;
            }
        }
        if (port_PointFits(point, state, corner, stored_x, stored_y)) {
            corner->x = point->x + (state->offset_x << PORT_REMASTER_FRACTION);
            corner->y = point->y + (state->offset_y << PORT_REMASTER_FRACTION);
            /* A place without a depth is still a place: a polygon the
             * renderer flattened -- a shadow is projected with a matrix
             * that flattens it onto the ground -- is drawn where the
             * projection put it and moved with everything else, with
             * its texture run evenly across it as the console's is. */
            corner->depth = point->depth > 0 ? point->depth : 0;
            if (corner->depth == 0) {
                prim.has_depth = FALSE;
            }
        } else {
            corner->x = corner->ix << PORT_REMASTER_FRACTION;
            corner->y = corner->iy << PORT_REMASTER_FRACTION;
            corner->depth = 0;
            prim.has_depth = FALSE;
            prim.has_place = FALSE;
        }
    }
    port_RemasterPaint(&prim, code, clut, state);
    prim.is_dithered = (state->mode & MODE_DITHER) != 0 &&
                       (prim.is_shaded || (prim.is_textured && !prim.is_raw));
    port_RemasterDrawPrim(&prim, port_KindOfCommand(code));
}

/* A rectangle: its corner, its texture's corner if it has one, and its
 * size if that is its own. It is flat on the screen, so it has no
 * depth: the backend draws it as it is, crisply, at the scale. */
static void port_DecodeRectangle(
    const u32 *words, u32 code, const PortGpuState *state) {
    PortRemasterPrim prim;
    PortRemasterCorner *corner = &prim.corner[0];
    s32 size = (s32)((code & CODE_SIZE_MASK) >> CODE_SIZE_SHIFT);
    u32 clut = 0;
    s32 at = 1;

    prim.kind = PORT_REMASTER_RECT;
    prim.corner_count = 1;
    prim.has_depth = FALSE;
    prim.has_place = FALSE;
    prim.id.object = 0;
    prim.id.group = 0;
    prim.id.face = 0;
    prim.id.piece = 0;
    /* a sprite or a tile of the fight's HUD is drawn at the picture's own
     * edge, where the game said which edge it belongs to */
    prim.anchor = port_FindAnchor(g_prim);
    port_RemasterCornerColor(corner, words[0]);
    corner->ix = port_RemasterPosition(
        port_RemasterPosition((s32)words[at]) + state->offset_x);
    corner->iy = port_RemasterPosition(
        port_RemasterPosition((s32)(words[at] >> HALF_SHIFT)) +
        state->offset_y);
    at++;
    corner->x = corner->ix << PORT_REMASTER_FRACTION;
    corner->y = corner->iy << PORT_REMASTER_FRACTION;
    corner->depth = 0;
    corner->u = 0;
    corner->v = 0;
    if (code & CODE_TEXTURED) {
        corner->u = (s32)(words[at] & BYTE_MASK);
        corner->v = (s32)((words[at] >> GREEN_SHIFT) & BYTE_MASK);
        clut = words[at++] >> HALF_SHIFT;
    }
    if (size == CODE_SIZE_OWN) {
        prim.rect_w = (s32)(words[at] & VRAM_X_MASK);
        prim.rect_h = (s32)((words[at] >> HALF_SHIFT) & VRAM_Y_MASK);
    } else {
        prim.rect_w = g_rect_sizes[size];
        prim.rect_h = g_rect_sizes[size];
    }
    port_RemasterPaint(&prim, code & ~(u32)CODE_SHADED, clut, state);
    port_RemasterDrawPrim(&prim, PORT_REMASTER_COUNT_RECT);
}

/* A command the GPU has just drawn (port_remaster.h): counted, and
 * decoded for the backend where the remastered picture is on. */
void port_RemasterCommand(const u32 *words, s32 count) {
    u32 code;
    s32 kind;
    PortGpuState state;

    if (g_backend == NULL || count <= 0) {
        return;
    }
    code = words[0] >> COMMAND_SHIFT;
    kind = port_KindOfCommand(code);
    g_counts.seen[kind]++;
    g_frame_counts.seen[kind]++;
    if (!port_RemasterIsOn()) {
        return;
    }
    port_GetGpuState(&state);
    switch (code & COMMAND_KIND_MASK) {
    case COMMAND_POLYGONS:
        port_DecodePolygon(words, code, &state);
        break;
    case COMMAND_RECTANGLES:
        port_DecodeRectangle(words, code, &state);
        break;
    case COMMAND_COPY:
        /* a copy inside video memory: its destination changed */
        if (g_backend->wrote_vram != NULL) {
            g_backend->wrote_vram(
                (s32)(words[2] & VRAM_X_MASK),
                (s32)((words[2] >> HALF_SHIFT) & VRAM_Y_MASK),
                port_RemasterTransferSize(words[3] & HALF_MASK, VRAM_X_MASK),
                port_RemasterTransferSize(words[3] >> HALF_SHIFT, VRAM_Y_MASK));
        }
        break;
    case COMMAND_SETTINGS:
    case COMMAND_LINES:
    case COMMAND_STORE:
        break;
    default:
        if (code == COMMAND_FILL && g_backend->wrote_vram != NULL) {
            s32 w = (s32)(((words[2] & VRAM_X_MASK) + FILL_STEP - 1) &
                          ~(u32)(FILL_STEP - 1));

            g_backend->wrote_vram(
                (s32)(words[1] & FILL_X_MASK),
                (s32)((words[1] >> HALF_SHIFT) & VRAM_Y_MASK), w,
                (s32)((words[2] >> HALF_SHIFT) & VRAM_Y_MASK));
        }
        break;
    }
}

void port_RemasterLoadedVram(s32 x, s32 y, s32 w, s32 h) {
    if (g_backend == NULL || !port_RemasterIsOn()) {
        return;
    }
    if (g_backend->wrote_vram != NULL) {
        g_backend->wrote_vram(x, y, w, h);
    }
}

/* The frame is done (port_remaster.h). */
void port_RemasterShow(void) {
    PortDisplay display;
    u32 *at = (u32 *)&g_frame_counts;
    u32 drawn = 0;
    u32 i;

    if (g_backend == NULL || !port_RemasterIsOn()) {
        return;
    }
    if (g_backend->end != NULL) {
        port_GetDisplay(&display);
        g_backend->end(display.x, display.y, display.w, display.h,
                       display.is_on, display.is_rgb24);
    }
    /* The counts of the frame on show. The game draws at its own rate,
     * which is half the console's in a fight, so at a blank where it
     * drew nothing the frame before it is still what is shown and
     * still what is counted. */
    for (i = 0; i < PORT_REMASTER_COUNT_KINDS; i++) {
        drawn += g_frame_counts.drawn[i];
    }
    if (drawn == 0) {
        return;
    }
    g_shown_counts = g_frame_counts;
    for (i = 0; i < sizeof(g_frame_counts) / sizeof(u32); i++) {
        at[i] = 0;
    }
}
