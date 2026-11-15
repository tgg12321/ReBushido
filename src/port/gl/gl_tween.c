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
#define TWEEN_VRAMS_MAX 256
/* Pixels of video memory a frame may write and have written down: a
 * frame's own clear is 640 by 240 of them. */
#define TWEEN_WORDS_MAX (PORT_VRAM_PIXELS / 2)

#define TWEEN_EMPTY (-1)
/* The odd number a key is multiplied by to mix its bits (as
 * src/port/remaster.c). */
#define MIX 0x9E3779B1U

/* The table from a primitive's offset in the frame's primitive buffer to
 * the primitive: a power of two, more than twice the most primitives,
 * looked in from the offset's own slot onwards. The renderer builds a
 * frame's primitives in the order it walks the scene, so a face is often
 * at the offset it was at in the frame before; but the stage queues the
 * cells about the camera, and one cell more or fewer moves everything
 * after it, so this finds only some of them. */
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
 * be moved: the chip holds a screen position to eleven bits with a sign,
 * so a corner outside that is a polygon the projection put behind the
 * camera, which the chip drops and which must go on being dropped. */
#define PORT_TWEEN_SCREEN_MAX 1023

#define PERCENT_256 256

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
    u32 pair_frames; /* the generation the pairing was worked out for */
    s32 is_one_scene;
    /* the point watched from phase to phase (gl_tween.h) */
    s32 track_key;
    s32 track_x;
    s32 track_y;
    s32 has_track;
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

/* Whether a primitive's corners are all where the chip could hold them
 * without wrapping, and all have a depth: one that is not is a polygon
 * the projection put behind the camera, and moving it could turn a
 * dropped polygon into a band across the picture. */
static s32 port_TweenIsOnScreen(const PortRemasterPrim *prim) {
    s32 i;

    for (i = 0; i < prim->corner_count; i++) {
        s32 x = port_TweenPlainX(prim, i);
        s32 y = port_TweenPlainY(prim, i);

        if (x < -PORT_TWEEN_SCREEN_MAX || x > PORT_TWEEN_SCREEN_MAX ||
            y < -PORT_TWEEN_SCREEN_MAX || y > PORT_TWEEN_SCREEN_MAX ||
            prim->corner[i].depth <= 0) {
            return FALSE;
        }
    }
    return TRUE;
}

/* Whether two primitives are painted the same way out of the same part
 * of the same texture: everything that belongs to the face of the model
 * and not to where the model is. The colours are left out, because the
 * light on a vertex changes as it turns. */
static s32 port_TweenSameKind(
    const PortRemasterPrim *before, const PortRemasterPrim *after) {
    s32 i;

    if (before->kind != after->kind ||
        before->corner_count != after->corner_count || !before->has_depth ||
        !after->has_depth) {
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

s32 port_TweenPairs(
    const PortRemasterPrim *before, const PortRemasterPrim *after) {
    if (!port_TweenSameKind(before, after)) {
        return FALSE;
    }
    if (!port_TweenIsOnScreen(before) || !port_TweenIsOnScreen(after)) {
        return FALSE;
    }
    return port_TweenStep(before, after) <= PORT_TWEEN_STEP_MAX;
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
    }
}

/* ---- the two tables ---- */

static u32 port_TweenMix(u32 value) {
    value ^= value >> 13;
    value *= MIX;
    value ^= value >> 15;
    return value;
}

static u32 port_TweenSlotOf(s32 key) {
    return port_TweenMix((u32)key >> 2) & (TWEEN_SLOTS - 1);
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
    s32 key = prim->key;

    if (key >= 0) {
        u32 slot = port_TweenSlotOf(key);
        u32 i;

        for (i = 0; i < TWEEN_PROBES; i++) {
            s32 *found = &frame->slot[(slot + i) & (TWEEN_SLOTS - 1)];

            if (*found == TWEEN_EMPTY) {
                *found = at;
                break;
            }
        }
    }
    if (!prim->has_depth) {
        frame->sig[at] = 0;
        return;
    }
    frame->sig[at] = port_TweenSignature(prim);
    bucket = port_TweenBucketOf(
        frame->sig[at], port_TweenCellX(prim), port_TweenCellY(prim));
    frame->next[at] = frame->bucket[bucket];
    frame->bucket[bucket] = at;
}

/* Which primitive of this frame is at `key`, or TWEEN_EMPTY. */
static s32 port_TweenAtKey(const TweenFrame *frame, s32 key) {
    u32 slot;
    u32 i;

    if (key < 0) {
        return TWEEN_EMPTY;
    }
    slot = port_TweenSlotOf(key);
    for (i = 0; i < TWEEN_PROBES; i++) {
        s32 at = frame->slot[(slot + i) & (TWEEN_SLOTS - 1)];

        if (at == TWEEN_EMPTY) {
            return TWEEN_EMPTY;
        }
        if (frame->prim[at].key == key) {
            return at;
        }
    }
    return TWEEN_EMPTY;
}

/* Which primitive of `frame` is the same face of the same model one
 * frame on: the one at the same offset in the primitive buffer where
 * that is still the same face, and otherwise the nearest of those with
 * the same signature. TWEEN_EMPTY where there is none near enough. */
static s32 port_TweenFind(
    const TweenFrame *frame, const PortRemasterPrim *prim, u32 signature) {
    s32 best = TWEEN_EMPTY;
    s32 best_step = PORT_TWEEN_STEP_MAX + 1;
    s32 at_key;
    s32 cell_x;
    s32 cell_y;
    s32 dx;
    s32 dy;

    if (!prim->has_depth || !port_TweenIsOnScreen(prim)) {
        return TWEEN_EMPTY;
    }
    at_key = port_TweenAtKey(frame, prim->key);
    if (at_key != TWEEN_EMPTY && port_TweenPairs(prim, &frame->prim[at_key])) {
        return at_key;
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
                if (step < best_step &&
                    port_TweenIsOnScreen(&frame->prim[at])) {
                    best_step = step;
                    best = at;
                }
            }
        }
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

    if (frame->prim_count >= TWEEN_PRIMS_MAX ||
        frame->event_count >= TWEEN_EVENTS_MAX) {
        frame->is_whole = FALSE;
        return;
    }
    frame->prim[frame->prim_count] = *prim;
    frame->next[frame->prim_count] = TWEEN_EMPTY;
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

static const PortRemasterBackend g_tween_backend = {
    port_TweenBegin, port_TweenDrawPrim, port_TweenWroteVram, port_TweenEnd};

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
    if (g_tween.saved == NULL || g_tween.pair == NULL) {
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
    g_tween.saved = NULL;
    g_tween.pair = NULL;
    g_tween.real = NULL;
    g_tween.is_on = FALSE;
    g_tween.ready = 0;
}

s32 port_TweenIsOn(void) { return g_tween.is_on; }

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

static void port_TweenGiveVramBack(const TweenFrame *frame) {
    PortDirtyRect sent;
    s32 i;

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
    /* The mirror the card reads texels from is sent the true pixels again
     * in one go: a frame writes video memory a hundred times over (its
     * clear, and the fog's thirty-one colour tables), and a hundred small
     * sends cost far more than the one rectangle holding them all. */
    if (!sent.is_empty) {
        port_GlUploadVram(sent.left, sent.top, sent.right - sent.left + 1,
                          sent.bottom - sent.top + 1);
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
    s32 i;

    if (g_tween.pair_frames == g_tween.generation) {
        return;
    }
    g_tween.pair_frames = g_tween.generation;
    for (i = 0; i < shown->prim_count; i++) {
        const PortRemasterPrim *one = &shown->prim[i];

        if (!one->has_depth) {
            g_tween.pair[i] = TWEEN_EMPTY;
            continue;
        }
        g_tween.pair[i] = port_TweenFind(after, one, shown->sig[i]);
        if (g_tween.pair[i] == TWEEN_EMPTY) {
            unpaired++;
        } else {
            paired++;
        }
    }
    /* nothing paired: a screen of 2D alone, or a scene whose faces are
     * nowhere near where they were */
    g_tween.is_one_scene =
        paired != 0 && unpaired * PERCENT_256 <= paired * PORT_TWEEN_CUT_SHARE;
}

s32 port_TweenDraw(s32 phase) {
    const TweenFrame *shown;
    const TweenFrame *after;
    u32 aside[PORT_GL_COUNTS];
    u32 none[PORT_GL_COUNTS];
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
    g_tween.has_track = FALSE;
    port_GlSaveCounts(aside);
    none[0] = 0;
    none[1] = 0;
    none[2] = 0;
    port_GlLoadCounts(none);
    port_TweenHoldVram(shown);
    port_TweenBegin();
    for (i = 0; i < shown->event_count; i++) {
        const TweenEvent *event = &shown->event[i];

        if (event->is_vram) {
            const TweenVram *one = &shown->vram[event->at];

            port_TweenPutVram(shown, one);
            if (g_tween.real->wrote_vram != NULL) {
                g_tween.real->wrote_vram(one->x, one->y, one->w, one->h);
            }
            continue;
        }
        {
            const PortRemasterPrim *one = &shown->prim[event->at];
            const PortRemasterPrim *two;
            PortRemasterPrim mixed;

            g_tween.counts.prims++;
            if (!one->has_depth) {
                g_tween.counts.flat++;
                if (g_tween.real->draw != NULL) {
                    g_tween.real->draw(one);
                }
                continue;
            }
            if (g_tween.pair[event->at] == TWEEN_EMPTY) {
                g_tween.counts.unpaired++;
                if (g_tween.real->draw != NULL) {
                    g_tween.real->draw(one);
                }
                continue;
            }
            two = &after->prim[g_tween.pair[event->at]];
            {
                s32 step = port_TweenStep(one, two);

                port_TweenMixPrim(&mixed, one, two, phase);
                g_tween.counts.mixed++;
                if ((u32)step > g_tween.counts.widest_step ||
                    !g_tween.has_track) {
                    g_tween.counts.widest_step = (u32)step;
                    g_tween.track_key = one->key;
                    g_tween.track_x =
                        mixed.corner[0].x -
                        (mixed.offset_x << PORT_REMASTER_FRACTION);
                    g_tween.track_y =
                        mixed.corner[0].y -
                        (mixed.offset_y << PORT_REMASTER_FRACTION);
                    g_tween.has_track = TRUE;
                }
                if (g_tween.real->draw != NULL) {
                    g_tween.real->draw(&mixed);
                }
            }
        }
    }
    /* the batch has to be drawn before video memory is put back: the
     * card reads its texels from the mirror of it */
    port_GlFinishFrame();
    port_TweenGiveVramBack(shown);
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
