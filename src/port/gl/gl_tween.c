/* Pictures between the game's frames (gl_tween.h; docs/PORT.md,
 * "Remastered", "More pictures a second").
 *
 * What a frame is, here: everything the decoder hands a backend between
 * one end of a frame and the next. The game builds its primitives and
 * then gives them all to the GPU at once (DrawOTag), so that is one
 * frame's drawing, and the blanks at which the game drew nothing leave
 * nothing between two ends and are passed over.
 *
 * Every primitive of a frame is written down as the decoder made it,
 * and every rectangle of video memory the frame wrote with the pixels as
 * they were: a picture drawn again needs the cleared screen the frame
 * started from, not the finished one the software GPU has left behind.
 *
 * A picture between two frames is then the frame on show handed to the
 * renderer again, each corner that the frame after it has as well moved
 * part of the way. Nothing of the game is read and nothing of it is
 * written; the only memory touched outside this file is video memory,
 * which is put back exactly as it was before anything else looks at
 * it. */
#include <stdlib.h>
#include <string.h>

#include "common.h"
#include "gl.h"
#include "gl_fighter.h"
#include "gl_shadow.h"
#include "gl_stage.h"
#include "gl_tween.h"
#include "port_gpu.h"
#include "port_remaster.h"

/* Frames kept: the one being written down, the frame after the one on
 * show, and the one on show. */
#define TWEEN_KEPT 3
/* The most of each a frame may have. A fight frame of the console's own
 * draw distance comes to some two thousand primitives; the whole of a
 * stage at `max` is what fills these. */
#define TWEEN_PRIMS_MAX 16384
#define TWEEN_EVENTS_MAX (TWEEN_PRIMS_MAX + 512)
#define TWEEN_VRAMS_MAX 512
/* Pixels of video memory a frame may write and have written down. A
 * frame's own clear is 640 by 240 of them; the frame that changes screen
 * clears both halves at once, 640 by 480, and with half of video memory
 * to write them in that frame could not be written down at all and the
 * pictures between it and the frame after it were lost. All of video
 * memory holds any frame measured, with the fog's thirty-one colour
 * tables and every load of a texture beside the clear. */
#define TWEEN_WORDS_MAX (PORT_VRAM_PIXELS)

#define TWEEN_EMPTY (-1)
/* The odd number a key is multiplied by to mix its bits (as
 * src/port/remaster.c). */
#define MIX 0x9E3779B1U

/* The table from a primitive's identity -- which face of which thing it
 * is a drawing of (port_remaster.h) -- to the primitive: a power of two,
 * more than twice the most primitives, looked in from the identity's own
 * slot onwards. This is how two frames' primitives are paired, and it is
 * exact: the same face of the same thing has the same identity in both
 * frames however far it moved, however the camera turned, and however
 * the walk about it changed, and no two primitives of one frame share
 * one. */
#define TWEEN_SLOTS 32768
#define TWEEN_PROBES 16

/* The table that finds a face again when its place in the buffer has
 * moved: by what does not change from frame to frame — the kind of
 * primitive, its texture page, its colour table and the texture point of
 * every corner, which belong to the face of the model and not to where
 * it is — and by which square of the screen it was in, so that two faces
 * of one texture are told apart by which is nearer. A corner may move by
 * a square's width and still be found, because the nine squares about
 * the one it was in are looked in. */
#define TWEEN_BUCKETS 8192
#define TWEEN_CELL 128
#define TWEEN_CHAIN_MAX 32

/* The farthest from the middle of video memory a corner may be and still
 * be moved. The decoder keeps the projection's own place for a point the
 * chip held to eleven bits, as long as the point is in front of the
 * camera and no further out than this (src/port/remaster.c,
 * port_PlaceIsNear): such a corner is off the side of the console's
 * screen and is the very thing a wider picture shows, so it is moved
 * like any other. Beyond it is a projection that came apart, which the
 * decoder has already thrown the place of away. */
#define PORT_TWEEN_SCREEN_MAX (4 * PORT_VRAM_WIDTH)

#define PERCENT_256 256

/* The table of a frame's corners by which point of which mesh they are
 * (port_TweenNotePoints): a power of two, more than twice the corners of
 * the faces a fight frame names, looked in from a point's own slot
 * onwards. */
#define TWEEN_POINTS 32768
#define TWEEN_POINT_PROBES 16
/* Two of a frame's faces that share a point put it within this of the
 * same place, in 16.16 pixels; further apart, the point is not trusted
 * (port_TweenNotePoints). */
#define TWEEN_POINT_NEAR (1 << PORT_REMASTER_FRACTION)

enum { POINT_EMPTY, POINT_ONE, POINT_UNSURE };

/* One point of a mesh as a frame's faces placed it: the drawing offset
 * taken off, so that the two halves of video memory compare. */
typedef struct {
    PortAddress object;
    PortAddress group;
    s32 index; /* which point of the mesh: a byte of the face's name */
    s32 state; /* POINT_... */
    s32 x;     /* 16.16 */
    s32 y;
    s32 ix; /* whole pixels */
    s32 iy;
    s32 depth;
    s32 depth_shift;
} TweenPoint;

typedef struct {
    s32 x;
    s32 y;
    s32 w;
    s32 h;
    u32 at; /* where its pixels are in the frame's words */
} TweenVram;

typedef struct {
    s32 is_vram; /* a rectangle of video memory, not a primitive */
    s32 at;      /* which primitive, or which rectangle */
} TweenEvent;

typedef struct {
    s32 is_whole; /* nothing of it overflowed: it can be drawn again */
    s32 prim_count;
    s32 event_count;
    s32 vram_count;
    u32 words_used;
    PortRemasterPrim *prim;
    TweenEvent *event;
    TweenVram *vram;
    u16 *words;
    u32 *sig;    /* TWEEN_PRIMS_MAX: each primitive's signature */
    s32 *slot;   /* TWEEN_SLOTS of them: a key's primitive */
    s32 *bucket; /* TWEEN_BUCKETS: a signature's first primitive */
    s32 *next;   /* TWEEN_PRIMS_MAX: the next of the same bucket */
    /* Where the stage's ground was drawn, where it is drawn as one mesh
     * (gl_stage.h): the primitives the game made of its tiles are not
     * written down at all -- there is nothing to pair, and they are
     * thousands of a frame's -- and in their place the mesh is drawn
     * again at the event the first of them came at, with the camera
     * between the two frames. */
    s32 has_mesh;
    s32 mesh_at; /* which event */
    PortRemasterPrim mesh_paint;
    PortStageView view;
    /* the first primitive of it that has a place: its drawing offset and
     * its drawing area are the frame's own half of video memory's */
    s32 placed_at;
} TweenFrame;

static struct {
    s32 is_on;
    const PortRemasterBackend *real;
    TweenFrame frame[TWEEN_KEPT];
    s32 writing; /* which of them the decoder is filling now */
    s32 ready;   /* frames finished: two are needed */
    u32 generation;
    /* video memory as it is, while a frame is drawn again over it */
    u16 *saved;
    PortTweenCounts counts;
    /* Which primitive of the frame after the one on show each of its own
     * is, worked out once for the pair of frames and read again by every
     * picture drawn between them: finding them is most of the work, and
     * it does not change from one phase to the next. */
    s32 *pair;
    /* The other way about: which primitive of the frame on show has
     * taken each primitive of the frame after it, so that no two take
     * the same one. */
    s32 *taken;
    u32 pair_frames; /* the generation the pairing was worked out for */
    s32 is_one_scene;
    /* Each frame's points, for a face the other frame has not: the
     * frame on show's [0] and the frame after it's [1]. */
    TweenPoint *points[2];
    /* A face of the frame on show that the frame after it has not, with
     * its corners where the frame after it put the same points
     * (`has_going`), and a face of the frame after it that the frame on
     * show has not, with its corners where the frame on show put them,
     * drawn after the primitive of the frame on show that `coming_first`
     * of it names and the ones `coming_next` chains on. */
    PortRemasterPrim *going;
    s32 *has_going;
    PortRemasterPrim *coming;
    s32 *coming_first;
    s32 *coming_last;
    s32 *coming_next;
    /* pair by likeness alone and let a moved primitive go undrawn, as
     * this file did before the primitives were named (gl_tween.h) */
    s32 guesses;
    /* the point watched from phase to phase (gl_tween.h) */
    s32 track_key;
    /* ... which primitive of the frame on show it is a corner of: the
     * paired one whose corner moves farthest, chosen once for the pair
     * of frames (port_TweenPairFrames) */
    s32 track_at;
    s32 track_x;
    s32 track_y;
    s32 has_track;
    /* the mirror was put back from video memory as it is, and not as the
     * card's frames left it (port_TweenGiveVramBack) */
    s32 mirror_behind;
} g_tween;

/* ---- the room a frame is written down in ---- */

static void port_TweenFreeFrame(TweenFrame *frame) {
    free(frame->prim);
    free(frame->event);
    free(frame->vram);
    free(frame->words);
    free(frame->sig);
    free(frame->slot);
    free(frame->bucket);
    free(frame->next);
    memset(frame, 0, sizeof(*frame));
}

static s32 port_TweenMakeFrame(TweenFrame *frame) {
    frame->prim = malloc(TWEEN_PRIMS_MAX * sizeof(*frame->prim));
    frame->event = malloc(TWEEN_EVENTS_MAX * sizeof(*frame->event));
    frame->vram = malloc(TWEEN_VRAMS_MAX * sizeof(*frame->vram));
    frame->words = malloc(TWEEN_WORDS_MAX * sizeof(*frame->words));
    frame->sig = malloc(TWEEN_PRIMS_MAX * sizeof(*frame->sig));
    frame->slot = malloc(TWEEN_SLOTS * sizeof(*frame->slot));
    frame->bucket = malloc(TWEEN_BUCKETS * sizeof(*frame->bucket));
    frame->next = malloc(TWEEN_PRIMS_MAX * sizeof(*frame->next));
    return frame->prim != NULL && frame->event != NULL && frame->vram != NULL &&
           frame->words != NULL && frame->sig != NULL && frame->slot != NULL &&
           frame->bucket != NULL && frame->next != NULL;
}

static void port_TweenClearFrame(TweenFrame *frame) {
    s32 i;

    frame->is_whole = TRUE;
    frame->has_mesh = FALSE;
    frame->mesh_at = 0;
    frame->placed_at = TWEEN_EMPTY;
    frame->prim_count = 0;
    frame->event_count = 0;
    frame->vram_count = 0;
    frame->words_used = 0;
    for (i = 0; i < TWEEN_SLOTS; i++) {
        frame->slot[i] = TWEEN_EMPTY;
    }
    for (i = 0; i < TWEEN_BUCKETS; i++) {
        frame->bucket[i] = TWEEN_EMPTY;
    }
}

/* ---- the arithmetic (gl_tween.h) ---- */

/* A whole pixel of a 16.16 position, held to eleven bits with a sign as
 * the chip holds it (src/port/remaster.c, port_RemasterPosition). */
static s32 port_TweenWhole(s32 fixed) {
    s32 value = (fixed >> PORT_REMASTER_FRACTION) & 0x7FF;

    return (value ^ 0x400) - 0x400;
}

/* Part of the way from one number to another. In sixty-four bits,
 * because a place is 16.16 pixels and a depth is the sum the GTE shifted
 * into SZ: either, times a phase of sixteen bits, is more than thirty-two
 * bits hold. */
static s32 port_TweenLerp(s32 before, s32 after, s32 phase) {
    return before +
           (s32)((((s64)after - before) * phase) >> PORT_TWEEN_FRACTION);
}

void port_TweenMixCorner(
    PortRemasterCorner *out, const PortRemasterCorner *before,
    const PortRemasterCorner *after, s32 phase) {
    double part = (double)phase / (double)PORT_TWEEN_ONE;
    double weight_before = (1.0 - part) * (double)before->depth;
    double weight_after = part * (double)after->depth;
    double weight = weight_before + weight_after;

    *out = *after;
    out->u = port_TweenLerp(before->u, after->u, phase);
    out->v = port_TweenLerp(before->v, after->v, phase);
    out->r = port_TweenLerp(before->r, after->r, phase);
    out->g = port_TweenLerp(before->g, after->g, phase);
    out->b = port_TweenLerp(before->b, after->b, phase);
    out->depth = port_TweenLerp(before->depth, after->depth, phase);
    if (weight <= 0.0) {
        /* no depth to weigh the two by: evenly, on the screen */
        out->x = port_TweenLerp(before->x, after->x, phase);
        out->y = port_TweenLerp(before->y, after->y, phase);
    } else {
        out->x = (s32)((weight_before * (double)before->x +
                        weight_after * (double)after->x) /
                       weight);
        out->y = (s32)((weight_before * (double)before->y +
                        weight_after * (double)after->y) /
                       weight);
    }
    out->ix = port_TweenWhole(out->x);
    out->iy = port_TweenWhole(out->y);
}

/* A corner's place with the drawing offset taken off, in whole pixels:
 * the game's two frames are the two halves of video memory, and this is
 * what makes them comparable. */
static s32 port_TweenPlainX(const PortRemasterPrim *prim, s32 corner) {
    return (prim->corner[corner].x >> PORT_REMASTER_FRACTION) - prim->offset_x;
}

static s32 port_TweenPlainY(const PortRemasterPrim *prim, s32 corner) {
    return (prim->corner[corner].y >> PORT_REMASTER_FRACTION) - prim->offset_y;
}

s32 port_TweenStep(
    const PortRemasterPrim *before, const PortRemasterPrim *after) {
    s32 worst = 0;
    s32 i;

    for (i = 0; i < before->corner_count && i < after->corner_count; i++) {
        s32 dx = port_TweenPlainX(after, i) - port_TweenPlainX(before, i);
        s32 dy = port_TweenPlainY(after, i) - port_TweenPlainY(before, i);

        dx = dx < 0 ? -dx : dx;
        dy = dy < 0 ? -dy : dy;
        worst = dx > worst ? dx : worst;
        worst = dy > worst ? dy : worst;
    }
    return worst;
}

/* Whether a primitive's corners are all where the projection really put
 * them and all within the range the chip holds a screen position in: one
 * that is not is a polygon the projection put behind the camera, and
 * moving it could turn a dropped polygon into a band across the
 * picture. A depth is not asked for -- a shadow has none and is moved
 * all the same (port_remaster.h, `has_place`). */
static s32 port_TweenIsOnScreen(const PortRemasterPrim *prim) {
    s32 i;

    if (!prim->has_place) {
        return FALSE;
    }
    for (i = 0; i < prim->corner_count; i++) {
        s32 x = port_TweenPlainX(prim, i);
        s32 y = port_TweenPlainY(prim, i);

        if (x < -PORT_TWEEN_SCREEN_MAX || x > PORT_TWEEN_SCREEN_MAX ||
            y < -PORT_TWEEN_SCREEN_MAX || y > PORT_TWEEN_SCREEN_MAX) {
            return FALSE;
        }
    }
    return TRUE;
}

/* Whether any part of a primitive is in the picture: its own drawing
 * area, widened by the bands of a wider picture, against the rectangle
 * its corners cover.
 *
 * Only these are weighed when the two frames are held against each
 * other (port_TweenPairFrames): whether the frame after holds a
 * primitive that is nowhere near the picture says nothing about whether
 * the two are one scene. On the stages measured at `draw_distance max`
 * this moves the shares little -- what the camera has turned away from
 * is projected behind it and has no place to be weighed by in the first
 * place -- and what it does is make them shares of what a player can
 * see, whatever a stage hands over. */
static s32 port_TweenInPicture(const PortRemasterPrim *prim) {
    s32 margin = port_GlPictureMargin();
    s32 left = prim->corner[0].x >> PORT_REMASTER_FRACTION;
    s32 right = left;
    s32 top = prim->corner[0].y >> PORT_REMASTER_FRACTION;
    s32 bottom = top;
    s32 i;

    for (i = 1; i < prim->corner_count; i++) {
        s32 x = prim->corner[i].x >> PORT_REMASTER_FRACTION;
        s32 y = prim->corner[i].y >> PORT_REMASTER_FRACTION;

        left = x < left ? x : left;
        right = x > right ? x : right;
        top = y < top ? y : top;
        bottom = y > bottom ? y : bottom;
    }
    return right >= prim->clip_left - margin &&
           left <= prim->clip_right + margin && bottom >= prim->clip_top &&
           top <= prim->clip_bottom;
}

/* Whether two primitives are painted the same way out of the same part
 * of the same texture: everything that belongs to the face of the model
 * and not to where the model is. The colours are left out, because the
 * light on a vertex changes as it turns. */
static s32 port_TweenSameKind(
    const PortRemasterPrim *before, const PortRemasterPrim *after) {
    s32 i;

    if (before->kind != after->kind ||
        before->corner_count != after->corner_count || !before->has_place ||
        !after->has_place || before->has_depth != after->has_depth ||
        /* the renderer drew the two at different scales, so their depths
         * are in different units and there is nothing to mix
         * (port_remaster.h, `depth_shift`) */
        before->depth_shift != after->depth_shift) {
        return FALSE;
    }
    if (before->is_shaded != after->is_shaded ||
        before->is_textured != after->is_textured ||
        before->is_raw != after->is_raw || before->is_half != after->is_half ||
        before->is_dithered != after->is_dithered ||
        before->blend != after->blend ||
        before->texel_bits != after->texel_bits ||
        before->page_x != after->page_x || before->page_y != after->page_y ||
        before->clut_x != after->clut_x || before->clut_y != after->clut_y) {
        return FALSE;
    }
    if (before->window_and_u != after->window_and_u ||
        before->window_or_u != after->window_or_u ||
        before->window_and_v != after->window_and_v ||
        before->window_or_v != after->window_or_v ||
        before->mask_set != after->mask_set ||
        before->mask_check != after->mask_check) {
        return FALSE;
    }
    for (i = 0; i < before->corner_count; i++) {
        if (before->corner[i].u != after->corner[i].u ||
            before->corner[i].v != after->corner[i].v) {
            return FALSE;
        }
    }
    return TRUE;
}

/* Whether `here` lies between `one` and `two`, a pixel either side
 * allowed for the rounding of the two ends. */
static s32 port_TweenBetween(s32 here, s32 one, s32 two) {
    s32 low = one < two ? one : two;
    s32 high = one < two ? two : one;

    return here >= low - 1 && here <= high + 1;
}

/* One primitive of the frame on show, moved part of the way towards the
 * frame after it. */
static void port_TweenMixPrim(
    PortRemasterPrim *out, const PortRemasterPrim *before,
    const PortRemasterPrim *after, s32 phase) {
    s32 shift_before_x = before->offset_x << PORT_REMASTER_FRACTION;
    s32 shift_before_y = before->offset_y << PORT_REMASTER_FRACTION;
    s32 shift_after_x = after->offset_x << PORT_REMASTER_FRACTION;
    s32 shift_after_y = after->offset_y << PORT_REMASTER_FRACTION;
    s32 i;

    /* everything but the geometry is the frame on show's: its own half of
     * video memory, its own drawing area */
    *out = *before;
    for (i = 0; i < before->corner_count; i++) {
        PortRemasterCorner one = before->corner[i];
        PortRemasterCorner two = after->corner[i];

        one.x -= shift_before_x;
        one.y -= shift_before_y;
        two.x -= shift_after_x;
        two.y -= shift_after_y;
        port_TweenMixCorner(&out->corner[i], &one, &two, phase);
        out->corner[i].x += shift_before_x;
        out->corner[i].y += shift_before_y;
        /* The whole-pixel corner, which is the one the chip's own rule
         * about what it will not draw is applied to (gl_draw.c,
         * port_GlIsDrawn), is moved by the same step: so at the first
         * phase it is the frame on show's own and the picture comes out
         * as the game's, pixel for pixel, and at the last it is the
         * other frame's. Taking it from the mixed 16.16 place instead
         * would differ by a pixel wherever the game moved a primitive's
         * corner after the projection stored it. */
        out->corner[i].ix = port_TweenWhole(
            (before->corner[i].ix +
             port_TweenLerp(
                 0, port_TweenPlainX(after, i) - port_TweenPlainX(before, i),
                 phase))
            << PORT_REMASTER_FRACTION);
        out->corner[i].iy = port_TweenWhole(
            (before->corner[i].iy +
             port_TweenLerp(
                 0, port_TweenPlainY(after, i) - port_TweenPlainY(before, i),
                 phase))
            << PORT_REMASTER_FRACTION);
        /* and it lies between the two frames' own places for it: a
         * corner that does not is a partner wrongly found or a mistake
         * in the arithmetic, and would be a polygon stretched across the
         * picture. Counted, never corrected: it must be none. */
        if (!port_TweenBetween(
                port_TweenPlainX(out, i), port_TweenPlainX(before, i),
                port_TweenPlainX(after, i)) ||
            !port_TweenBetween(
                port_TweenPlainY(out, i), port_TweenPlainY(before, i),
                port_TweenPlainY(after, i))) {
            g_tween.counts.off_the_line++;
        }
    }
}

/* ---- the two tables ---- */

static u32 port_TweenMix(u32 value) {
    value ^= value >> 13;
    value *= MIX;
    value ^= value >> 15;
    return value;
}

/* An identity's first slot, and whether two identities are the one
 * thing. */
static u32 port_TweenSlotOf(const PortRemasterIdentity *id) {
    u32 value =
        port_TweenMix((u32)id->object) +
        port_TweenMix((u32)(id->object >> 16) * 3) +
        port_TweenMix((u32)id->group * 5) +
        port_TweenMix((u32)(id->group >> 16) * 7) +
        port_TweenMix((u32)id->face * 11) + port_TweenMix((u32)id->piece + 1);

    return port_TweenMix(value) & (TWEEN_SLOTS - 1);
}

static s32 port_TweenSameId(
    const PortRemasterIdentity *a, const PortRemasterIdentity *b) {
    return a->object == b->object && a->group == b->group &&
           a->face == b->face && a->piece == b->piece;
}

/* What does not change from frame to frame, in one number. */
static u32 port_TweenSignature(const PortRemasterPrim *prim) {
    u32 value = (u32)prim->kind * 7 + (u32)prim->corner_count * 31;
    s32 i;

    value = value * 65599 + (u32)prim->page_x;
    value = value * 65599 + (u32)prim->page_y;
    value = value * 65599 + (u32)prim->clut_x;
    value = value * 65599 + (u32)prim->clut_y;
    value = value * 65599 + (u32)prim->texel_bits;
    value = value * 65599 +
            (u32)(prim->is_shaded + 2 * prim->is_textured + 4 * prim->is_raw +
                  8 * prim->is_half + 16 * prim->blend);
    for (i = 0; i < prim->corner_count; i++) {
        value = value * 65599 + (u32)prim->corner[i].u;
        value = value * 65599 + (u32)prim->corner[i].v;
    }
    return port_TweenMix(value);
}

/* The bucket of a signature in the square (`cell_x`, `cell_y`) of the
 * screen. */
static u32 port_TweenBucketOf(u32 signature, s32 cell_x, s32 cell_y) {
    u32 value = signature ^ port_TweenMix((u32)(cell_x * 1013 + cell_y * 263));

    return value & (TWEEN_BUCKETS - 1);
}

static s32 port_TweenCellX(const PortRemasterPrim *prim) {
    return port_TweenPlainX(prim, 0) / TWEEN_CELL;
}

static s32 port_TweenCellY(const PortRemasterPrim *prim) {
    return port_TweenPlainY(prim, 0) / TWEEN_CELL;
}

static void port_TweenRemember(TweenFrame *frame, s32 at) {
    const PortRemasterPrim *prim = &frame->prim[at];
    u32 bucket;

    if (prim->id.object != 0) {
        u32 slot = port_TweenSlotOf(&prim->id);
        u32 i;

        for (i = 0; i < TWEEN_PROBES; i++) {
            s32 *found = &frame->slot[(slot + i) & (TWEEN_SLOTS - 1)];

            if (*found == TWEEN_EMPTY) {
                *found = at;
                break;
            }
        }
    }
    if (!prim->has_place) {
        frame->sig[at] = 0;
        return;
    }
    frame->sig[at] = port_TweenSignature(prim);
    bucket = port_TweenBucketOf(
        frame->sig[at], port_TweenCellX(prim), port_TweenCellY(prim));
    frame->next[at] = frame->bucket[bucket];
    frame->bucket[bucket] = at;
}

/* Which primitive of this frame has this identity, or TWEEN_EMPTY. */
static s32 port_TweenAtId(
    const TweenFrame *frame, const PortRemasterIdentity *id) {
    u32 slot;
    u32 i;

    if (id->object == 0) {
        return TWEEN_EMPTY;
    }
    slot = port_TweenSlotOf(id);
    for (i = 0; i < TWEEN_PROBES; i++) {
        s32 at = frame->slot[(slot + i) & (TWEEN_SLOTS - 1)];

        if (at == TWEEN_EMPTY) {
            return TWEEN_EMPTY;
        }
        if (port_TweenSameId(&frame->prim[at].id, id)) {
            return at;
        }
    }
    return TWEEN_EMPTY;
}

/* Which primitive of `frame` is the same face of the same model one
 * frame on, where its place in the primitive buffer has moved: the
 * nearest of those with the same signature that no other primitive has
 * taken yet. TWEEN_EMPTY where there is none near enough.
 *
 * `taken` says which of `frame`'s primitives are another's partner
 * already, and is what makes the pairing one-to-one. Without it two
 * faces of one texture a little apart — which a stage's ground is made
 * of, every tile the same signature — can both take the same partner,
 * and one of them is then drawn stretched from its own place to the
 * other's: a tile that flies across the picture as the camera turns.
 * `clashes` counts the times the nearest partner was taken, which is
 * how many such pairs there would otherwise have been. */
static s32 port_TweenFind(const TweenFrame *frame, const PortRemasterPrim *prim,
                          u32 signature, const s32 *taken, u32 *clashes) {
    s32 best = TWEEN_EMPTY;
    s32 best_step = PORT_TWEEN_STEP_MAX + 1;
    s32 nearest_step = PORT_TWEEN_STEP_MAX + 1;
    s32 cell_x;
    s32 cell_y;
    s32 dx;
    s32 dy;

    if (!prim->has_depth || !port_TweenIsOnScreen(prim)) {
        return TWEEN_EMPTY;
    }
    cell_x = port_TweenCellX(prim);
    cell_y = port_TweenCellY(prim);
    for (dy = -1; dy <= 1; dy++) {
        for (dx = -1; dx <= 1; dx++) {
            u32 bucket =
                port_TweenBucketOf(signature, cell_x + dx, cell_y + dy);
            s32 at = frame->bucket[bucket];
            s32 walked;

            for (walked = 0; at != TWEEN_EMPTY && walked < TWEEN_CHAIN_MAX;
                 walked++, at = frame->next[at]) {
                s32 step;

                /* the signature is every field that does not change from
                 * frame to frame, in one number: a primitive of another
                 * signature is another face */
                if (frame->sig[at] != signature) {
                    continue;
                }
                step = port_TweenStep(prim, &frame->prim[at]);
                if (step > PORT_TWEEN_STEP_MAX ||
                    !port_TweenIsOnScreen(&frame->prim[at])) {
                    continue;
                }
                if (step < nearest_step) {
                    nearest_step = step;
                }
                if ((taken == NULL || taken[at] == TWEEN_EMPTY) &&
                    step < best_step) {
                    best_step = step;
                    best = at;
                }
            }
        }
    }
    if (nearest_step < best_step && clashes != NULL) {
        *clashes += 1;
    }
    return best;
}

/* ---- the decoder's four calls, written down ---- */

static TweenFrame *port_TweenWriting(void) {
    return &g_tween.frame[g_tween.writing];
}

static void port_TweenBegin(void) {
    if (g_tween.real != NULL && g_tween.real->begin != NULL) {
        g_tween.real->begin();
    }
}

static void port_TweenNotePrim(const PortRemasterPrim *prim) {
    TweenFrame *frame = port_TweenWriting();

    /* a primitive of a stage tile is the mesh's business and is not
     * written down: the mesh is drawn again where the first of them came
     * (gl_stage.h) */
    if (port_GlStageWouldTake(prim)) {
        if (!frame->has_mesh) {
            frame->has_mesh = TRUE;
            frame->mesh_at = frame->event_count;
            frame->mesh_paint = *prim;
            /* What the ground is drawn with, taken here and not at the
             * frame's end: the game builds its next frame's primitives
             * before the picture of this one has been read, so by the
             * end of a frame the side channel already holds the next
             * frame's camera (gl_stage.h). */
            port_GlStageGetView(&frame->view);
        }
        return;
    }
    if (frame->prim_count >= TWEEN_PRIMS_MAX ||
        frame->event_count >= TWEEN_EVENTS_MAX) {
        frame->is_whole = FALSE;
        return;
    }
    frame->prim[frame->prim_count] = *prim;
    frame->next[frame->prim_count] = TWEEN_EMPTY;
    if (frame->placed_at == TWEEN_EMPTY && prim->has_place) {
        frame->placed_at = frame->prim_count;
    }
    port_TweenRemember(frame, frame->prim_count);
    frame->event[frame->event_count].is_vram = FALSE;
    frame->event[frame->event_count].at = frame->prim_count;
    frame->event_count++;
    frame->prim_count++;
}

static void port_TweenDrawPrim(const PortRemasterPrim *prim) {
    port_TweenNotePrim(prim);
    if (g_tween.real != NULL && g_tween.real->draw != NULL) {
        g_tween.real->draw(prim);
    }
}

static void port_TweenNoteVram(s32 x, s32 y, s32 w, s32 h) {
    TweenFrame *frame = port_TweenWriting();
    TweenVram *one;
    s32 row;

    if (w <= 0 || h <= 0) {
        return;
    }
    /* a write that goes round an edge of video memory is not written
     * down: the frame it is in cannot be drawn again */
    if (x < 0 || y < 0 || x + w > PORT_VRAM_WIDTH || y + h > PORT_VRAM_HEIGHT ||
        frame->vram_count >= TWEEN_VRAMS_MAX ||
        frame->event_count >= TWEEN_EVENTS_MAX ||
        frame->words_used + (u32)(w * h) > TWEEN_WORDS_MAX) {
        frame->is_whole = FALSE;
        return;
    }
    one = &frame->vram[frame->vram_count];
    one->x = x;
    one->y = y;
    one->w = w;
    one->h = h;
    one->at = frame->words_used;
    for (row = 0; row < h; row++) {
        memcpy(frame->words + one->at + (u32)(row * w),
               g_port_vram + (y + row) * PORT_VRAM_WIDTH + x,
               (size_t)w * sizeof(u16));
    }
    frame->words_used += (u32)(w * h);
    frame->event[frame->event_count].is_vram = TRUE;
    frame->event[frame->event_count].at = frame->vram_count;
    frame->event_count++;
    frame->vram_count++;
}

static void port_TweenWroteVram(s32 x, s32 y, s32 w, s32 h) {
    port_TweenNoteVram(x, y, w, h);
    if (g_tween.real != NULL && g_tween.real->wrote_vram != NULL) {
        g_tween.real->wrote_vram(x, y, w, h);
    }
}

static void port_TweenEnd(s32 x, s32 y, s32 w, s32 h, s32 is_on, s32 is_rgb24) {
    TweenFrame *frame = port_TweenWriting();

    if (frame->event_count != 0) {
        /* the game drew a frame: it is finished, and the next goes into
         * the slot the frame before the one on show was in */
        if (!frame->is_whole) {
            g_tween.counts.overflowed++;
        }
        g_tween.counts.frames++;
        g_tween.generation++;
        g_tween.writing = (g_tween.writing + 1) % TWEEN_KEPT;
        if (g_tween.ready < TWEEN_KEPT) {
            g_tween.ready++;
        }
        port_TweenClearFrame(port_TweenWriting());
    }
    if (g_tween.real != NULL && g_tween.real->end != NULL) {
        g_tween.real->end(x, y, w, h, is_on, is_rgb24);
    }
}

/* A frame the card does not draw is no frame of the pictures between
 * frames either: its writes go to the mirror alone. */
static void port_TweenKeptVram(s32 x, s32 y, s32 w, s32 h) {
    if (g_tween.real != NULL && g_tween.real->kept_vram != NULL) {
        g_tween.real->kept_vram(x, y, w, h);
    }
}

static const PortRemasterBackend g_tween_backend = {
    port_TweenBegin, port_TweenDrawPrim, port_TweenWroteVram, port_TweenEnd,
    port_TweenKeptVram};

/* ---- starting and stopping ---- */

const char *port_TweenStart(void) {
    s32 i;

    if (g_tween.is_on) {
        return NULL;
    }
    g_tween.real = port_RemasterGetBackend();
    if (g_tween.real == NULL || !port_GlIsStarted()) {
        return "there is no renderer to draw between the game's frames";
    }
    for (i = 0; i < TWEEN_KEPT; i++) {
        if (!port_TweenMakeFrame(&g_tween.frame[i])) {
            port_TweenStop();
            return "there is no memory for the frames to be drawn again";
        }
        port_TweenClearFrame(&g_tween.frame[i]);
    }
    g_tween.saved = malloc(TWEEN_WORDS_MAX * sizeof(u16));
    g_tween.pair = malloc(TWEEN_PRIMS_MAX * sizeof(*g_tween.pair));
    g_tween.taken = malloc(TWEEN_PRIMS_MAX * sizeof(*g_tween.taken));
    for (i = 0; i < 2; i++) {
        g_tween.points[i] = malloc(TWEEN_POINTS * sizeof(TweenPoint));
    }
    g_tween.going = malloc(TWEEN_PRIMS_MAX * sizeof(*g_tween.going));
    g_tween.has_going = malloc(TWEEN_PRIMS_MAX * sizeof(*g_tween.has_going));
    g_tween.coming = malloc(TWEEN_PRIMS_MAX * sizeof(*g_tween.coming));
    g_tween.coming_first =
        malloc(TWEEN_PRIMS_MAX * sizeof(*g_tween.coming_first));
    g_tween.coming_last =
        malloc(TWEEN_PRIMS_MAX * sizeof(*g_tween.coming_last));
    g_tween.coming_next =
        malloc(TWEEN_PRIMS_MAX * sizeof(*g_tween.coming_next));
    if (g_tween.saved == NULL || g_tween.pair == NULL ||
        g_tween.taken == NULL || g_tween.points[0] == NULL ||
        g_tween.points[1] == NULL || g_tween.going == NULL ||
        g_tween.has_going == NULL || g_tween.coming == NULL ||
        g_tween.coming_first == NULL || g_tween.coming_last == NULL ||
        g_tween.coming_next == NULL) {
        port_TweenStop();
        return "there is no memory to keep video memory in";
    }
    g_tween.writing = 0;
    g_tween.ready = 0;
    g_tween.pair_frames = ~0U;
    g_tween.is_one_scene = FALSE;
    g_tween.is_on = TRUE;
    port_RemasterSetBackend(&g_tween_backend);
    return NULL;
}

void port_TweenStop(void) {
    s32 i;

    if (g_tween.real != NULL) {
        port_RemasterSetBackend(g_tween.real);
    }
    for (i = 0; i < TWEEN_KEPT; i++) {
        port_TweenFreeFrame(&g_tween.frame[i]);
    }
    free(g_tween.saved);
    free(g_tween.pair);
    free(g_tween.taken);
    for (i = 0; i < 2; i++) {
        free(g_tween.points[i]);
        g_tween.points[i] = NULL;
    }
    free(g_tween.going);
    free(g_tween.has_going);
    free(g_tween.coming);
    free(g_tween.coming_first);
    free(g_tween.coming_last);
    free(g_tween.coming_next);
    g_tween.going = NULL;
    g_tween.has_going = NULL;
    g_tween.coming = NULL;
    g_tween.coming_first = NULL;
    g_tween.coming_last = NULL;
    g_tween.coming_next = NULL;
    g_tween.saved = NULL;
    g_tween.pair = NULL;
    g_tween.taken = NULL;
    g_tween.real = NULL;
    g_tween.is_on = FALSE;
    g_tween.ready = 0;
}

s32 port_TweenIsOn(void) { return g_tween.is_on; }

s32 port_TweenTakeMirrorBehind(void) {
    s32 was = g_tween.mirror_behind;

    g_tween.mirror_behind = FALSE;
    return was;
}

void port_TweenGuessOnly(s32 on) {
    g_tween.guesses = on;
    g_tween.pair_frames = ~0U;
}

void port_TweenForget(void) {
    if (!g_tween.is_on) {
        return;
    }
    g_tween.ready = 0;
    g_tween.pair_frames = ~0U;
    g_tween.is_one_scene = FALSE;
    port_TweenClearFrame(port_TweenWriting());
}

u32 port_TweenGeneration(void) { return g_tween.generation; }

/* The frame on show, and the frame after it: the game draws into one
 * half of video memory and puts the other on the display, so the frame
 * it has just handed over is not the one the screen holds. */
static const TweenFrame *port_TweenShown(void) {
    return &g_tween.frame[(g_tween.writing + TWEEN_KEPT - 2) % TWEEN_KEPT];
}

static const TweenFrame *port_TweenAfter(void) {
    return &g_tween.frame[(g_tween.writing + TWEEN_KEPT - 1) % TWEEN_KEPT];
}

s32 port_TweenCanDraw(void) {
    const TweenFrame *shown;
    const TweenFrame *after;

    if (!g_tween.is_on || g_tween.ready < 2) {
        return FALSE;
    }
    shown = port_TweenShown();
    after = port_TweenAfter();
    return shown->is_whole && after->is_whole && shown->event_count != 0 &&
           after->prim_count != 0;
}

/* ---- a picture between two frames ---- */

/* Video memory's own pixels, under each rectangle the frame on show
 * wrote, kept aside at the same places as the frame's own copy of them:
 * all of them before any of them is written over, so that rectangles
 * that overlap are all put back as they were. Only what the frame wrote
 * is touched, which is far less than the smallest rectangle holding all
 * of it (the screen and the fog's colour tables are at the two ends of
 * video memory). */
static void port_TweenHoldVram(const TweenFrame *frame) {
    s32 i;

    for (i = 0; i < frame->vram_count; i++) {
        const TweenVram *one = &frame->vram[i];
        s32 row;

        for (row = 0; row < one->h; row++) {
            memcpy(g_tween.saved + one->at + (u32)(row * one->w),
                   g_port_vram + (one->y + row) * PORT_VRAM_WIDTH + one->x,
                   (size_t)one->w * sizeof(u16));
        }
    }
}

static void port_TweenGiveVramBack(const TweenFrame *frame, s32 is_held) {
    PortDirtyRect sent;
    s32 i;

    /* the mirror on the card, by what the picture changed of it */
    is_held = is_held && port_GlMirrorGiveBack();
    port_ClearDirtyRect(&sent);
    for (i = 0; i < frame->vram_count; i++) {
        const TweenVram *one = &frame->vram[i];
        s32 row;

        for (row = 0; row < one->h; row++) {
            memcpy(g_port_vram + (one->y + row) * PORT_VRAM_WIDTH + one->x,
                   g_tween.saved + one->at + (u32)(row * one->w),
                   (size_t)one->w * sizeof(u16));
        }
        port_AddDirtyRect(&sent, one->x, one->y, one->w, one->h);
    }
    /* Where what the picture changed of the mirror was too much to keep,
     * the mirror is sent video memory as it is under everything the frame
     * wrote, as every picture did before (gl.h, port_GlMirrorHold). */
    if (!is_held && !sent.is_empty) {
        port_GlRestoreVram(sent.left, sent.top, sent.right - sent.left + 1,
                           sent.bottom - sent.top + 1);
        g_tween.mirror_behind = TRUE;
    }
}

static void port_TweenPutVram(const TweenFrame *frame, const TweenVram *one) {
    s32 row;

    for (row = 0; row < one->h; row++) {
        memcpy(g_port_vram + (one->y + row) * PORT_VRAM_WIDTH + one->x,
               frame->words + one->at + (u32)(row * one->w),
               (size_t)one->w * sizeof(u16));
    }
}

/* ---- a face one frame has and the other has not ---- */

/* **The points a face is made of outlast the face.** A face of a fighter
 * that turns away from the camera between two frames is not drawn in the
 * second (the renderer drops a face whose corners turn the wrong way),
 * and one that turns towards it is not in the first: so at a fighter's
 * edge, every frame, some faces are in one frame only. Drawn where it is,
 * a face that is going stands still while the faces about it move, and a
 * face that is coming is not drawn at all until the frame that has it is
 * on show: the edge of a fighter flickers between two shapes at the
 * display's rate, which is what the owner saw of a fighter standing
 * still.
 *
 * But the points such a face is made of are in both frames all the same:
 * its neighbours, which both frames draw, are made of them too. A face's
 * name says which points of its mesh its corners are (port_remaster.h,
 * `face`, a byte each), so each frame's points are written down by which
 * point of which mesh of which thing they are, and a face one frame has
 * not is given the places that frame put its points at. It is then moved
 * like any other, and drawn only while it still faces the way it faced in
 * the frame that has it: a face that is going is drawn until it has
 * turned edge on, a face that is coming from the moment it has turned
 * past it -- which is what the renderer itself would have drawn of the
 * thing at that moment.
 *
 * A point two faces of one frame put at places more than a pixel apart
 * is not trusted (`POINT_UNSURE`): that is a name that does not say which
 * points the corners are -- a piece of a face cut in two, a mesh whose
 * points are numbered over again -- and a face with one is drawn as it
 * was before. */

static u32 port_TweenPointSlot(
    PortAddress object, PortAddress group, s32 index) {
    u32 value =
        port_TweenMix((u32)object) + port_TweenMix((u32)(object >> 16) * 3) +
        port_TweenMix((u32)group * 5) + port_TweenMix((u32)(group >> 16) * 7) +
        port_TweenMix((u32)index * 11 + 1);

    return port_TweenMix(value) & (TWEEN_POINTS - 1);
}

/* The point of a mesh, or NULL. With `make`, the empty slot it would go
 * in where there is none. */
static TweenPoint *port_TweenFindPoint(TweenPoint *table, PortAddress object,
                                       PortAddress group, s32 index, s32 make) {
    u32 slot = port_TweenPointSlot(object, group, index);
    u32 i;

    for (i = 0; i < TWEEN_POINT_PROBES; i++) {
        TweenPoint *one = &table[(slot + i) & (TWEEN_POINTS - 1)];

        if (one->state == POINT_EMPTY) {
            return make ? one : NULL;
        }
        if (one->object == object && one->group == group &&
            one->index == index) {
            return one;
        }
    }
    return NULL;
}

/* Whether a primitive's corners are the points its name says, in order:
 * a whole face of a mesh, drawn in perspective. */
static s32 port_TweenIsWholeFace(const PortRemasterPrim *prim) {
    return prim->id.object != 0 && prim->id.piece == 0 && prim->has_place &&
           prim->has_depth && !prim->is_effect &&
           (prim->kind == PORT_REMASTER_TRIANGLE ||
            prim->kind == PORT_REMASTER_QUAD) &&
           port_TweenIsOnScreen(prim);
}

/* Every point of a frame's faces into `table`, with the drawing offset
 * taken off. */
static void port_TweenNotePoints(const TweenFrame *frame, TweenPoint *table) {
    s32 i;

    memset(table, 0, TWEEN_POINTS * sizeof(TweenPoint));
    for (i = 0; i < frame->prim_count; i++) {
        const PortRemasterPrim *prim = &frame->prim[i];
        s32 corner;

        if (!port_TweenIsWholeFace(prim)) {
            continue;
        }
        for (corner = 0; corner < prim->corner_count; corner++) {
            const PortRemasterCorner *at = &prim->corner[corner];
            s32 index = (s32)(((u32)prim->id.face >> (corner * 8)) & 0xFF);
            s32 x = at->x - (prim->offset_x << PORT_REMASTER_FRACTION);
            s32 y = at->y - (prim->offset_y << PORT_REMASTER_FRACTION);
            TweenPoint *one = port_TweenFindPoint(
                table, prim->id.object, prim->id.group, index, TRUE);

            if (one == NULL) {
                continue;
            }
            if (one->state == POINT_EMPTY) {
                one->object = prim->id.object;
                one->group = prim->id.group;
                one->index = index;
                one->state = POINT_ONE;
                one->x = x;
                one->y = y;
                one->ix = at->ix - prim->offset_x;
                one->iy = at->iy - prim->offset_y;
                one->depth = at->depth;
                one->depth_shift = prim->depth_shift;
            } else if (one->x - x > TWEEN_POINT_NEAR ||
                       x - one->x > TWEEN_POINT_NEAR ||
                       one->y - y > TWEEN_POINT_NEAR ||
                       y - one->y > TWEEN_POINT_NEAR ||
                       one->depth_shift != prim->depth_shift) {
                one->state = POINT_UNSURE;
            }
        }
    }
}

/* `face` with its corners where the other frame put its points, and that
 * frame's drawing offset and drawing area (`frame_of`). FALSE where the
 * other frame has not every one of them. */
static s32 port_TweenPlaceFace(
    PortRemasterPrim *out, const PortRemasterPrim *face, TweenPoint *table,
    const PortRemasterPrim *frame_of) {
    s32 corner;

    if (!port_TweenIsWholeFace(face) || frame_of == NULL) {
        return FALSE;
    }
    *out = *face;
    out->offset_x = frame_of->offset_x;
    out->offset_y = frame_of->offset_y;
    out->clip_left = frame_of->clip_left;
    out->clip_top = frame_of->clip_top;
    out->clip_right = frame_of->clip_right;
    out->clip_bottom = frame_of->clip_bottom;
    for (corner = 0; corner < face->corner_count; corner++) {
        s32 index = (s32)(((u32)face->id.face >> (corner * 8)) & 0xFF);
        const TweenPoint *one = port_TweenFindPoint(
            table, face->id.object, face->id.group, index, FALSE);

        if (one == NULL || one->state != POINT_ONE ||
            one->depth_shift != face->depth_shift || one->depth <= 0) {
            return FALSE;
        }
        out->corner[corner].x =
            one->x + (out->offset_x << PORT_REMASTER_FRACTION);
        out->corner[corner].y =
            one->y + (out->offset_y << PORT_REMASTER_FRACTION);
        out->corner[corner].ix = one->ix + out->offset_x;
        out->corner[corner].iy = one->iy + out->offset_y;
        out->corner[corner].depth = one->depth;
    }
    return port_TweenIsOnScreen(out);
}

/* Which way a primitive's first three corners turn: twice the area, with
 * its sign. It is what the renderer drops a face by (NCLIP). */
static double port_TweenTurn(const PortRemasterPrim *prim) {
    double x0 = (double)prim->corner[0].x;
    double y0 = (double)prim->corner[0].y;

    return ((double)prim->corner[1].x - x0) * ((double)prim->corner[2].y - y0) -
           ((double)prim->corner[2].x - x0) * ((double)prim->corner[1].y - y0);
}

/* Whether two primitives' corners turn the same way, and neither is edge
 * on. */
static s32 port_TweenTurnsAlike(
    const PortRemasterPrim *one, const PortRemasterPrim *two) {
    double a = port_TweenTurn(one);
    double b = port_TweenTurn(two);

    return (a > 0.0 && b > 0.0) || (a < 0.0 && b < 0.0);
}

/* The faces one of the two frames has and the other has not, given the
 * other's places for their points (`going`, `coming`). */
static void port_TweenPlaceUnpaired(void) {
    const TweenFrame *shown = port_TweenShown();
    const TweenFrame *after = port_TweenAfter();
    const PortRemasterPrim *shown_frame =
        shown->placed_at != TWEEN_EMPTY ? &shown->prim[shown->placed_at] : NULL;
    const PortRemasterPrim *after_frame =
        after->placed_at != TWEEN_EMPTY ? &after->prim[after->placed_at] : NULL;
    s32 anchor = TWEEN_EMPTY;
    s32 i;

    g_tween.counts.going = 0;
    g_tween.counts.coming = 0;
    for (i = 0; i < shown->prim_count; i++) {
        g_tween.has_going[i] = FALSE;
        g_tween.coming_first[i] = TWEEN_EMPTY;
        g_tween.coming_last[i] = TWEEN_EMPTY;
    }
    if (g_tween.guesses) {
        return;
    }
    port_TweenNotePoints(shown, g_tween.points[0]);
    port_TweenNotePoints(after, g_tween.points[1]);
    /* a face that is going: the frame after it's places for its points */
    for (i = 0; i < shown->prim_count; i++) {
        if (g_tween.pair[i] != TWEEN_EMPTY) {
            continue;
        }
        if (port_TweenPlaceFace(&g_tween.going[i], &shown->prim[i],
                                g_tween.points[1], after_frame)) {
            g_tween.has_going[i] = TRUE;
            g_tween.counts.going++;
        }
    }
    /* A face that is coming: the frame on show's places for its points,
     * and it is drawn after the primitive of the frame on show that is
     * the partner of the last one before it in the frame after it, which
     * is where the frame after it painted it. Only one the frame on show
     * had turned away from is drawn at all: so a picture at phase 0 is
     * the frame on show's own. */
    for (i = 0; i < after->prim_count; i++) {
        const PortRemasterPrim *two = &after->prim[i];

        g_tween.coming_next[i] = TWEEN_EMPTY;
        if (g_tween.taken[i] != TWEEN_EMPTY) {
            anchor = g_tween.taken[i];
            continue;
        }
        if (anchor == TWEEN_EMPTY ||
            !port_TweenPlaceFace(
                &g_tween.coming[i], two, g_tween.points[0], shown_frame) ||
            port_TweenTurnsAlike(&g_tween.coming[i], two)) {
            continue;
        }
        /* chained after the anchor's, in the order the frame after it
         * painted them */
        if (g_tween.coming_first[anchor] == TWEEN_EMPTY) {
            g_tween.coming_first[anchor] = i;
        } else {
            g_tween.coming_next[g_tween.coming_last[anchor]] = i;
        }
        g_tween.coming_last[anchor] = i;
        g_tween.counts.coming++;
    }
}

/* **A thing whose node moved.** A thing is named by the address of the
 * node the renderer draws it from (port_RemasterNoteObject), and most
 * nodes stay where they are while a fight runs. Some do not: the nodes of
 * a fighter's weapons and of the shadows are built again every frame in
 * one of two places, the one and the other in turn, so every face of
 * them has another name in the next frame and none was paired. Each was
 * drawn where it was until the frame after it was on show: a fighter's
 * sword stood still for the pictures between two frames and then jumped,
 * and his shadow with it.
 *
 * So the things with faces left unpaired are held against the things of
 * the frame after it with faces nothing has taken, and a thing of the one
 * is taken to be a thing of the other where the most of its faces are
 * found under the other's name: the same mesh, the same face, the same
 * piece, of the same kind and near. Its faces are then paired under that
 * name as any others are. */
#define TWEEN_MOVED_MAX 64

static void port_TweenPairMoved(
    const TweenFrame *shown, const TweenFrame *after) {
    PortAddress from[TWEEN_MOVED_MAX];
    PortAddress to[TWEEN_MOVED_MAX];
    s32 from_count = 0;
    s32 to_count = 0;
    s32 i;
    s32 j;

    if (g_tween.guesses) {
        return;
    }
    for (i = 0; i < shown->prim_count; i++) {
        PortAddress object = shown->prim[i].id.object;

        if (g_tween.pair[i] != TWEEN_EMPTY || object == 0 ||
            !shown->prim[i].has_place || shown->prim[i].is_effect) {
            continue;
        }
        for (j = 0; j < from_count && from[j] != object; j++) {
        }
        if (j == from_count && from_count < TWEEN_MOVED_MAX) {
            from[from_count++] = object;
        }
    }
    for (i = 0; i < after->prim_count; i++) {
        PortAddress object = after->prim[i].id.object;

        if (g_tween.taken[i] != TWEEN_EMPTY || object == 0 ||
            !after->prim[i].has_place || after->prim[i].is_effect) {
            continue;
        }
        for (j = 0; j < to_count && to[j] != object; j++) {
        }
        if (j == to_count && to_count < TWEEN_MOVED_MAX) {
            to[to_count++] = object;
        }
    }
    for (i = 0; i < from_count; i++) {
        PortAddress best = 0;
        s32 best_count = 0;
        s32 k;

        for (j = 0; j < to_count; j++) {
            s32 count = 0;

            if (to[j] == from[i]) {
                continue;
            }
            for (k = 0; k < shown->prim_count; k++) {
                const PortRemasterPrim *one = &shown->prim[k];
                PortRemasterIdentity id;
                s32 at;

                if (one->id.object != from[i] ||
                    g_tween.pair[k] != TWEEN_EMPTY) {
                    continue;
                }
                id = one->id;
                id.object = to[j];
                at = port_TweenAtId(after, &id);
                if (at != TWEEN_EMPTY && g_tween.taken[at] == TWEEN_EMPTY &&
                    port_TweenSameKind(one, &after->prim[at]) &&
                    port_TweenStep(one, &after->prim[at]) <=
                        PORT_TWEEN_STEP_MAX) {
                    count++;
                }
            }
            if (count > best_count) {
                best_count = count;
                best = to[j];
            }
        }
        if (best == 0) {
            continue;
        }
        for (k = 0; k < shown->prim_count; k++) {
            const PortRemasterPrim *one = &shown->prim[k];
            PortRemasterIdentity id;
            s32 at;

            if (one->id.object != from[i] || g_tween.pair[k] != TWEEN_EMPTY) {
                continue;
            }
            id = one->id;
            id.object = best;
            at = port_TweenAtId(after, &id);
            if (at != TWEEN_EMPTY && g_tween.taken[at] == TWEEN_EMPTY &&
                port_TweenSameKind(one, &after->prim[at]) &&
                port_TweenIsOnScreen(one) &&
                port_TweenIsOnScreen(&after->prim[at])) {
                g_tween.pair[k] = at;
                g_tween.taken[at] = k;
                g_tween.counts.named++;
                g_tween.counts.moved++;
            }
        }
        /* a thing of the frame after it is another's at most once */
        for (j = 0; j < to_count; j++) {
            if (to[j] == best) {
                to[j] = 0;
            }
        }
    }
}

/* Which primitive of the frame after the one on show each of its own is,
 * and whether the two are one scene a frame apart: a camera that cut, a
 * screen that changed or a model that was replaced leaves nothing to
 * move, and the game's own picture is then what to show.
 *
 * It is worked out once for the pair of frames; every picture drawn
 * between them reads it again. */
static void port_TweenPairFrames(void) {
    const TweenFrame *shown = port_TweenShown();
    const TweenFrame *after = port_TweenAfter();
    s32 paired = 0;
    s32 unpaired = 0;
    s32 jumped = 0; /* ... paired, and a screen's width from where it was */
    s32 scene = 0;  /* primitives of the frame on show that have a depth */
    s32 i;

    if (g_tween.pair_frames == g_tween.generation) {
        return;
    }
    g_tween.pair_frames = g_tween.generation;
    g_tween.counts.scene = 0;
    g_tween.counts.named = 0;
    g_tween.counts.searched = 0;
    g_tween.counts.clashes = 0;
    g_tween.counts.held = 0;
    g_tween.counts.moved = 0;
    for (i = 0; i < after->prim_count; i++) {
        g_tween.taken[i] = TWEEN_EMPTY;
    }
    /* **Named, not guessed.** Every primitive of the scene says which
     * face of which thing it is a drawing of (port_remaster.h), and the
     * same face of the same thing has the same name in the next frame
     * however far it moved and however the walk about it changed. So
     * the pairing is a look-up, exact and one-to-one, and a camera
     * swinging round is no harder than a camera standing still. */
    for (i = 0; i < shown->prim_count; i++) {
        const PortRemasterPrim *one = &shown->prim[i];
        s32 at;

        g_tween.pair[i] = TWEEN_EMPTY;
        if (!one->has_place) {
            continue;
        }
        scene++;
        if (g_tween.guesses) {
            continue;
        }
        at = port_TweenAtId(after, &one->id);
        if (at != TWEEN_EMPTY && g_tween.taken[at] == TWEEN_EMPTY &&
            port_TweenSameKind(one, &after->prim[at]) &&
            port_TweenIsOnScreen(one) &&
            port_TweenIsOnScreen(&after->prim[at])) {
            g_tween.pair[i] = at;
            g_tween.taken[at] = i;
            g_tween.counts.named++;
        }
    }
    port_TweenPairMoved(shown, after);
    /* What has no name at all -- drawing the renderer's walk knows
     * nothing of -- is looked for by what it is painted out of and where
     * it was, as it always was: the nearest of the same signature that
     * no other primitive has taken. */
    for (i = 0; i < shown->prim_count; i++) {
        const PortRemasterPrim *one = &shown->prim[i];
        s32 counts;
        s32 at;

        if (!one->has_place) {
            continue;
        }
        /* only what is in the picture is weighed (port_TweenInPicture) */
        counts = port_TweenInPicture(one);
        if (g_tween.pair[i] != TWEEN_EMPTY) {
            paired += counts;
            /* ... and the one thing a frame apart is not a screen's width
             * from where it was: a camera that cut leaves every face of
             * the scene paired, by name, and all of them somewhere
             * else */
            if (port_TweenStep(one, &after->prim[g_tween.pair[i]]) >
                PORT_TWEEN_JUMP) {
                jumped += counts;
            }
            continue;
        }
        if (one->id.object != 0 && !g_tween.guesses) {
            /* it has a name and the next frame has not drawn it: it
             * waits where it is, and nothing else may be taken for it */
            unpaired += counts;
            continue;
        }
        at = port_TweenFind(
            after, one, shown->sig[i], g_tween.taken, &g_tween.counts.clashes);
        g_tween.pair[i] = at;
        if (at == TWEEN_EMPTY) {
            unpaired += counts;
        } else {
            g_tween.taken[at] = i;
            g_tween.counts.searched++;
            paired += counts;
        }
    }
    g_tween.counts.scene = (u32)scene;
    g_tween.counts.jumped = (u32)jumped;
    g_tween.counts.weighed = (u32)(paired + unpaired);
    /* **What is a cut.** The two frames are one scene a frame apart
     * unless most of what the picture holds says otherwise: either the
     * frame after it does not draw it at all -- a screen that changed, a
     * fighter that was replaced -- or it draws it a screen's width from
     * where it was, which is a camera that cut and not a camera that
     * moved.
     *
     * Both lines are half, where the one line used to be a quarter. In
     * a fight on the practice stage at `draw_distance max` an eighth of
     * the faces the picture holds cannot be paired -- a tile the camera
     * came nearer to is cut another way and no piece of it has its own
     * name -- so with a quarter for a line an ordinary frame sat just
     * under it and a frame with a little more coming and going sat just
     * over, and the picture alternated between the display's rate and
     * the game's. A real cut leaves almost nothing paired and near, so
     * the lines need not be fine. */
    g_tween.is_one_scene =
        paired != 0 &&
        unpaired * PERCENT_256 <= paired * PORT_TWEEN_CUT_SHARE &&
        jumped * PERCENT_256 <= paired * PORT_TWEEN_JUMP_SHARE;
    if (g_tween.is_one_scene) {
        port_TweenPlaceUnpaired();
    }
    /* The point watched from phase to phase: the first corner of the
     * paired primitive that moves farthest, of those in the picture that
     * the chip would draw at both ends. One it would not draw at the end
     * -- a face a few screens off the side -- is drawn where it is at
     * the last phase (`held`) and was taken for a point off its line. */
    {
        s32 widest = -1;

        g_tween.track_at = TWEEN_EMPTY;
        for (i = 0; i < shown->prim_count; i++) {
            s32 step;

            if (g_tween.pair[i] == TWEEN_EMPTY ||
                !port_TweenInPicture(&shown->prim[i]) ||
                !port_GlWouldDraw(&shown->prim[i]) ||
                !port_GlWouldDraw(&after->prim[g_tween.pair[i]])) {
                continue;
            }
            step =
                port_TweenStep(&shown->prim[i], &after->prim[g_tween.pair[i]]);
            if (step > widest) {
                widest = step;
                g_tween.track_at = i;
            }
        }
    }
}

/* Where the watched point is in this picture, if primitive `at` of the
 * frame on show is the one watched: whether it was drawn moved or held
 * where it is, it is the same primitive at every phase (gl_tween.h,
 * port_TweenTrack). */
static void port_TweenWatch(s32 at, const PortRemasterPrim *drawn) {
    if (at != g_tween.track_at) {
        return;
    }
    g_tween.track_key = drawn->key;
    g_tween.track_x =
        drawn->corner[0].x - (drawn->offset_x << PORT_REMASTER_FRACTION);
    g_tween.track_y =
        drawn->corner[0].y - (drawn->offset_y << PORT_REMASTER_FRACTION);
    g_tween.has_track = TRUE;
}

/* Primitive `at` of the frame on show, at `phase`. */
static void port_TweenDrawOne(
    const TweenFrame *shown, const TweenFrame *after, s32 at, s32 phase) {
    const PortRemasterPrim *one = &shown->prim[at];
    const PortRemasterPrim *two;
    PortRemasterPrim mixed;
    s32 step;

    g_tween.counts.prims++;
    if (!one->has_place) {
        g_tween.counts.flat++;
        if (g_tween.real->draw != NULL) {
            g_tween.real->draw(one);
        }
        return;
    }
    if (g_tween.pair[at] == TWEEN_EMPTY) {
        g_tween.counts.unpaired++;
        if (g_tween.has_going[at]) {
            /* A face the frame after it has turned away from: moved
             * towards where that frame put its points, and drawn while
             * it still faces the camera (port_TweenPlaceUnpaired). */
            port_TweenMixPrim(&mixed, one, &g_tween.going[at], phase);
            if (!port_TweenTurnsAlike(&mixed, one)) {
                return;
            }
            if (port_GlWouldDraw(&mixed)) {
                if (g_tween.real->draw != NULL) {
                    g_tween.real->draw(&mixed);
                }
                return;
            }
        }
        /* Otherwise the next frame does not hold this face at all, or
         * not its points -- a piece of a tile that is cut another way
         * now -- and it is drawn where it is. (Carrying it the way the
         * thing it belongs to went was tried, and measured worse: a face
         * with no partner is usually one that is going, and moving it
         * uncovers what it was covering.) */
        if (g_tween.real->draw != NULL) {
            g_tween.real->draw(one);
        }
        return;
    }
    two = &after->prim[g_tween.pair[at]];
    step = port_TweenStep(one, two);
    port_TweenMixPrim(&mixed, one, two, phase);
    /* **A picture between two frames holds every primitive the frame on
     * show held.** The chip draws no triangle that spans more than 1023
     * pixels across or 511 down and none of no area, and the backend
     * leaves out the same ones by the same whole-pixel rule: a corner
     * moved part of the way could fall outside it where the frame on
     * show's did not, and the picture would have a hole in it where that
     * face was. So a primitive that would not be drawn moved is drawn
     * where it is. (Its two places are more than a screen apart, which is
     * a face at the very edge of the view; carrying it the way the thing
     * it belongs to went was measured to open more of the backdrop than
     * it closed.) */
    if (!g_tween.guesses && !port_GlWouldDraw(&mixed) &&
        port_GlWouldDraw(one)) {
        g_tween.counts.held++;
        port_TweenWatch(at, one);
        if (g_tween.real->draw != NULL) {
            g_tween.real->draw(one);
        }
        return;
    }
    g_tween.counts.mixed++;
    if ((u32)step > g_tween.counts.widest_step) {
        g_tween.counts.widest_step = (u32)step;
    }
    port_TweenWatch(at, &mixed);
    if (g_tween.real->draw != NULL) {
        g_tween.real->draw(&mixed);
    }
}

/* The faces of the frame after it that the frame on show had turned away
 * from and that come after primitive `at` of the frame on show, at
 * `phase`: each from where the frame on show put its points, drawn once
 * it has turned to face the camera as it does in the frame after it
 * (port_TweenPlaceUnpaired). Never at phase 0, which is the frame on
 * show's own picture. */
static void port_TweenDrawComing(const TweenFrame *after, s32 at, s32 phase) {
    s32 i;

    if (phase <= 0) {
        return;
    }
    for (i = g_tween.coming_first[at]; i != TWEEN_EMPTY;
         i = g_tween.coming_next[i]) {
        PortRemasterPrim mixed;

        port_TweenMixPrim(&mixed, &g_tween.coming[i], &after->prim[i], phase);
        if (port_TweenTurnsAlike(&mixed, &after->prim[i]) &&
            port_GlWouldDraw(&mixed) && g_tween.real->draw != NULL) {
            g_tween.real->draw(&mixed);
        }
    }
}

s32 port_TweenDraw(s32 phase) {
    const TweenFrame *shown;
    const TweenFrame *after;
    u32 aside[PORT_GL_COUNTS];
    u32 none[PORT_GL_COUNTS];
    s32 stage_state;
    s32 is_held;
    s32 i;

    if (!port_TweenCanDraw()) {
        return FALSE;
    }
    if (phase < 0) {
        phase = 0;
    }
    if (phase > PORT_TWEEN_ONE) {
        phase = PORT_TWEEN_ONE;
    }
    shown = port_TweenShown();
    after = port_TweenAfter();
    port_TweenPairFrames();
    if (!g_tween.is_one_scene) {
        g_tween.counts.cuts++;
        return FALSE;
    }
    g_tween.counts.prims = 0;
    g_tween.counts.mixed = 0;
    g_tween.counts.unpaired = 0;
    g_tween.counts.flat = 0;
    g_tween.counts.widest_step = 0;
    g_tween.counts.off_the_line = 0;
    g_tween.has_track = FALSE;
    port_GlSaveCounts(aside);
    /* the stage's mesh is drawn again here and must be left as it was:
     * the game's own frame is painted after this picture is made, and it
     * needs its ground (gl_stage.h) */
    stage_state = port_GlStageSaveFrame();
    for (i = 0; i < PORT_GL_COUNTS; i++) {
        none[i] = 0;
    }
    port_GlLoadCounts(none);
    port_TweenHoldVram(shown);
    is_held = port_GlMirrorHold();
    port_TweenBegin();
    /* the shadows cast from the fighters' faces are moved with them
     * (gl_shadow.h) */
    port_GlShadowSetPhase(phase);
    if (shown->has_mesh) {
        /* the stage's ground, with the camera between the two frames:
         * all that moves in it is the view (gl_stage.h) */
        PortStageView between;

        port_GlStageMixView(
            &between, &shown->view, &after->view, phase, PORT_TWEEN_ONE);
        port_GlStageSetView(&between);
    }
    /* the fighters drawn from models of their own, their bones between
     * the two frames' (gl_fighter.h) */
    port_GlFighterTween(phase);
    for (i = 0; i <= shown->event_count; i++) {
        const TweenEvent *event;

        if (shown->has_mesh && i == shown->mesh_at) {
            port_GlStageTakes(&shown->mesh_paint);
        }
        if (i == shown->event_count) {
            break;
        }
        event = &shown->event[i];
        if (event->is_vram) {
            const TweenVram *one = &shown->vram[event->at];

            port_TweenPutVram(shown, one);
            if (g_tween.real->wrote_vram != NULL) {
                g_tween.real->wrote_vram(one->x, one->y, one->w, one->h);
            }
            continue;
        }
        port_TweenDrawOne(shown, after, event->at, phase);
        port_TweenDrawComing(after, event->at, phase);
    }
    port_GlStageSetView(NULL);
    port_GlFighterTween(-1);
    port_GlStageLoadFrame(stage_state);
    /* the batch has to be drawn before video memory is put back: the
     * card reads its texels from the mirror of it */
    port_GlFinishFrame();
    port_GlShadowSetPhase(-1);
    port_TweenGiveVramBack(shown, is_held);
    port_GlLoadCounts(aside);
    g_tween.counts.pictures++;
    return TRUE;
}

const PortTweenCounts *port_TweenGetCounts(void) { return &g_tween.counts; }

s32 port_TweenTrack(s32 *key, s32 *x, s32 *y) {
    *key = g_tween.track_key;
    *x = g_tween.track_x;
    *y = g_tween.track_y;
    return g_tween.has_track;
}
