/* The step's drawing written down, and drawn by the window afterwards
 * (gl_record.h). */
#include <stdlib.h>
#include <string.h>

#include "common.h"
#include "gl.h"
#include "gl_record.h"
#include "port_fighter.h"
#include "port_gpu.h"
#include "port_paint.h"
#include "port_remaster.h"

/* the port's log (port_platform.h, which is the game's) */
extern void port_Log(const char *format, ...);

/* A record is a chain of blocks, kept for the next one: a fight's step
 * comes to some 1 to 3 MB (two thousand primitives and the frame's clear),
 * a stage drawn whole at `max` to more. A block is never moved, so that
 * a snapshot in it stays where the getters were told it is. */
#define BLOCK_BYTES (4U << 20)
#define ALIGN 8
#define ROUND(n) (((n) + ALIGN - 1) & ~(size_t)(ALIGN - 1))

enum {
    CALL_BEGIN,
    CALL_DRAW,
    CALL_WROTE,
    CALL_KEPT,
    CALL_END,
    CALL_FINISH,
    CALL_PAUSE,
    CALL_VIEWS,
    CALL_SIDE,
    /* what follows is drawn only once the step is still
     * (port_GlRecordHold) */
    CALL_HOLD
};

typedef struct {
    u32 kind;
    u32 bytes; /* of what follows, on eight */
} Call;

/* a rectangle of video memory told of: as it was asked, and the pixels of
 * it as port_AddDirtyRect takes it */
typedef struct {
    s32 x;
    s32 y;
    s32 w;
    s32 h;
    s32 left;
    s32 top;
    s32 width;
    s32 height;
} Told;

typedef struct {
    s32 args[6];
} Args;

/* `used` is the step's thread's; `published` is how far the window's
 * thread may read, and `record` which record the block is of now. */
typedef struct Block {
    struct Block *next;
    size_t used;
    size_t published;
    u32 record;
    u8 *data;
} Block;

static struct {
    s32 is_on;
    const PortRemasterBackend *real;
    PortPauseHolder real_holder;
    Block *first;
    Block *writing;
    size_t bytes;
    s32 lost;
    u32 record; /* the record being written, by number */
    /* what the last snapshot written down was taken at */
    u32 views_version;
    u32 side_stamp;
    s32 has_views;
    s32 has_side;
    const PortFighterViews *last_views;
    const PortRemasterSide *last_side;
    /* the primitives written down since the record began
     * (port_GlRecordHasDraws), and who is told of the first */
    u32 draws;
    void (*wake)(void);
    /* where the window's thread has drawn up to (NULL: nothing yet), and
     * whether it may draw no more while the step runs */
    Block *read_block;
    size_t read_at;
    s32 is_held;
    /* the stage's view's version the backend has last drawn with */
    s32 drawn_version;
    s32 has_drawn_version;
    PortGlRecordCounts counts;
} g_rec;

/* ---- writing down ---- */

static Block *port_RecordNewBlock(void) {
    Block *block = malloc(sizeof(*block));

    if (block == NULL) {
        return NULL;
    }
    block->data = malloc(BLOCK_BYTES);
    if (block->data == NULL) {
        free(block);
        return NULL;
    }
    block->next = NULL;
    block->used = 0;
    block->published = 0;
    block->record = 0;
    return block;
}

/* Room for a call of `bytes` after its head, in the block being written
 * or the next; NULL where there is none (the record is then lost, and the
 * log says so once). The call is the window's to read once published
 * (port_RecordPublish). */
static void *port_RecordRoom(u32 kind, size_t bytes) {
    size_t need = sizeof(Call) + ROUND(bytes);
    Block *block = g_rec.writing;
    Call *call;

    if (g_rec.lost || need > BLOCK_BYTES) {
        g_rec.lost = TRUE;
        return NULL;
    }
    if (block == NULL || block->used + need > BLOCK_BYTES) {
        Block *next = block != NULL ? block->next : g_rec.first;

        if (next == NULL) {
            next = port_RecordNewBlock();
            if (next == NULL) {
                g_rec.lost = TRUE;
                return NULL;
            }
        }
        next->used = 0;
        __atomic_store_n(&next->published, 0, __ATOMIC_RELAXED);
        if (block != NULL) {
            __atomic_store_n(&block->next, next, __ATOMIC_RELEASE);
        } else {
            __atomic_store_n(&g_rec.first, next, __ATOMIC_RELEASE);
        }
        /* the window's thread goes on into it from here */
        __atomic_store_n(&next->record, g_rec.record, __ATOMIC_RELEASE);
        g_rec.writing = block = next;
    }
    call = (Call *)(void *)(block->data + block->used);
    call->kind = kind;
    call->bytes = (u32)ROUND(bytes);
    block->used += need;
    g_rec.bytes += need;
    g_rec.counts.calls++;
    return call + 1;
}

/* The calls written so far, for the window's thread to read. */
static void port_RecordPublish(void) {
    if (g_rec.writing != NULL) {
        __atomic_store_n(
            &g_rec.writing->published, g_rec.writing->used, __ATOMIC_RELEASE);
    }
}

/* The fighters' views and the side channel, written down where they have
 * changed since they last were: what the backend reads of them at the
 * call that follows is then what it would have read at it. */
static void port_RecordState(void) {
    u32 version = port_FighterViewsVersion();
    u32 stamp = port_RemasterSideStamp();

    if (!g_rec.has_views || version != g_rec.views_version) {
        PortFighterViews *views =
            port_RecordRoom(CALL_VIEWS, sizeof(PortFighterViews));

        if (views != NULL) {
            port_FighterKeepViews(views);
            g_rec.last_views = views;
            g_rec.views_version = version;
            g_rec.has_views = TRUE;
            g_rec.counts.views_kept++;
        }
    }
    if (!g_rec.has_side || stamp != g_rec.side_stamp) {
        u32 room = port_RemasterSideBytes();
        PortRemasterSide *side =
            port_RecordRoom(CALL_SIDE, ROUND(sizeof(PortRemasterSide)) + room);

        if (side != NULL) {
            port_RemasterKeepSide(
                side, (u8 *)side + ROUND(sizeof(PortRemasterSide)));
            g_rec.last_side = side;
            g_rec.side_stamp = stamp;
            g_rec.has_side = TRUE;
            g_rec.counts.sides_kept++;
        }
    }
}

static void port_RecordArgs(
    u32 kind, s32 a, s32 b, s32 c, s32 d, s32 e, s32 f) {
    Args *args;

    port_RecordState();
    args = port_RecordRoom(kind, sizeof(Args));
    if (args != NULL) {
        args->args[0] = a;
        args->args[1] = b;
        args->args[2] = c;
        args->args[3] = d;
        args->args[4] = e;
        args->args[5] = f;
    }
    port_RecordPublish();
}

/* A rectangle of video memory, with its pixels as they are now. */
static void port_RecordVram(u32 kind, s32 x, s32 y, s32 w, s32 h) {
    PortDirtyRect rect;
    Told *told;
    u16 *pixels;
    s32 width;
    s32 height;
    s32 row;

    if (w <= 0 || h <= 0) {
        port_RecordArgs(kind, x, y, w, h, 0, 0);
        return;
    }
    port_ClearDirtyRect(&rect);
    port_AddDirtyRect(&rect, x, y, w, h);
    width = rect.right - rect.left + 1;
    height = rect.bottom - rect.top + 1;
    port_RecordState();
    told = port_RecordRoom(
        kind, sizeof(Told) + (size_t)width * (size_t)height * sizeof(u16));
    if (told == NULL) {
        return;
    }
    told->x = x;
    told->y = y;
    told->w = w;
    told->h = h;
    told->left = rect.left;
    told->top = rect.top;
    told->width = width;
    told->height = height;
    pixels = (u16 *)(void *)(told + 1);
    for (row = 0; row < height; row++) {
        memcpy(pixels + (size_t)row * (size_t)width,
               &g_port_vram[(rect.top + row) * PORT_VRAM_WIDTH + rect.left],
               (size_t)width * sizeof(u16));
    }
    port_RecordPublish();
}

static void port_RecordBegin(void) {
    port_RecordArgs(CALL_BEGIN, 0, 0, 0, 0, 0, 0);
}

static void port_RecordDraw(const PortRemasterPrim *prim) {
    PortRemasterPrim *to;

    port_RecordState();
    to = port_RecordRoom(CALL_DRAW, sizeof(*to));
    if (to != NULL) {
        *to = *prim;
    }
    port_RecordPublish();
    /* the frame's list has begun (port_GlRecordHasDraws) */
    if (__atomic_add_fetch(&g_rec.draws, 1, __ATOMIC_RELEASE) == 1 &&
        g_rec.wake != NULL) {
        g_rec.wake();
    }
}

static void port_RecordWrote(s32 x, s32 y, s32 w, s32 h) {
    port_RecordVram(CALL_WROTE, x, y, w, h);
}

static void port_RecordEnd(
    s32 x, s32 y, s32 w, s32 h, s32 is_on, s32 is_rgb24) {
    port_RecordArgs(CALL_END, x, y, w, h, is_on, is_rgb24);
}

/* A write of a frame the card does not draw: the backend's own waits for
 * the painters to have painted the rectangle before it reads it
 * (port_GlUploadVram), and so does this before the pixels are taken. */
static void port_RecordKept(s32 x, s32 y, s32 w, s32 h) {
    if (w > 0 && h > 0) {
        port_PaintSyncRead(x, y, w, h);
    }
    port_RecordVram(CALL_KEPT, x, y, w, h);
}

static void port_RecordPause(s32 what, s32 from_x, s32 from_y, s32 x, s32 y) {
    port_RecordArgs(CALL_PAUSE, what, from_x, from_y, x, y, 0);
}

/* The recorder's table: a call is there where the backend's is. */
static PortRemasterBackend g_recorder;

void port_GlStepRecords(s32 on) {
    const PortRemasterBackend *real;

    if (on == g_rec.is_on) {
        return;
    }
    if (!on) {
        /* the backend's own again; the record waits to be drawn */
        port_RemasterSetBackend(g_rec.real);
        port_RemasterSwapPauseHolder(g_rec.real_holder);
        g_rec.is_on = FALSE;
        return;
    }
    real = port_RemasterGetBackend();
    if (real == NULL) {
        return;
    }
    g_rec.real = real;
    g_recorder.begin = real->begin != NULL ? port_RecordBegin : NULL;
    g_recorder.draw = real->draw != NULL ? port_RecordDraw : NULL;
    g_recorder.wrote_vram = real->wrote_vram != NULL ? port_RecordWrote : NULL;
    g_recorder.end = real->end != NULL ? port_RecordEnd : NULL;
    g_recorder.kept_vram = real->kept_vram != NULL ? port_RecordKept : NULL;
    g_rec.record++;
    g_rec.writing = NULL;
    g_rec.bytes = 0;
    g_rec.lost = FALSE;
    g_rec.has_views = FALSE;
    g_rec.has_side = FALSE;
    g_rec.read_block = NULL;
    g_rec.read_at = 0;
    g_rec.is_held = FALSE;
    __atomic_store_n(&g_rec.draws, 0, __ATOMIC_RELAXED);
    /* the views and the side channel as the step begins: what a picture
     * drawn beside the step reads, from here until the record is drawn */
    g_rec.last_views = NULL;
    g_rec.last_side = NULL;
    port_RecordState();
    port_FighterUseViews(g_rec.last_views);
    port_RemasterUseSide(g_rec.last_side);
    port_RemasterSetBackend(&g_recorder);
    g_rec.real_holder = port_RemasterSwapPauseHolder(NULL);
    if (g_rec.real_holder != NULL) {
        port_RemasterSwapPauseHolder(port_RecordPause);
    }
    g_rec.is_on = TRUE;
    g_rec.counts.records++;
}

s32 port_GlIsRecording(void) { return g_rec.is_on; }

void port_GlFinishOrRecord(void) {
    if (g_rec.is_on) {
        port_RecordArgs(CALL_FINISH, 0, 0, 0, 0, 0, 0);
        return;
    }
    port_GlFinishFrame();
}

void port_GlRecordHold(void) {
    if (g_rec.is_on) {
        port_RecordArgs(CALL_HOLD, 0, 0, 0, 0, 0, 0);
    }
}

void port_GlRecordWake(void (*wake)(void)) { g_rec.wake = wake; }

s32 port_GlRecordHasDraws(void) {
    return __atomic_load_n(&g_rec.draws, __ATOMIC_ACQUIRE) != 0;
}

/* ---- drawing it ---- */

/* One call made again, as it was made of the backend. */
static void port_RecordCall(const Call *call) {
    const PortRemasterBackend *real = g_rec.real;
    const void *body = call + 1;
    const s32 *args = ((const Args *)body)->args;

    switch (call->kind) {
    case CALL_BEGIN:
        real->begin();
        break;
    case CALL_DRAW:
        real->draw((const PortRemasterPrim *)body);
        break;
    case CALL_WROTE:
    case CALL_KEPT: {
        const Told *told = body;

        port_GlTellPixels((const u16 *)(const void *)(told + 1), told->left,
                          told->top, told->width, told->height);
        if (call->kind == CALL_WROTE) {
            real->wrote_vram(told->x, told->y, told->w, told->h);
        } else {
            real->kept_vram(told->x, told->y, told->w, told->h);
        }
        port_GlTellPixels(NULL, 0, 0, 0, 0);
        break;
    }
    case CALL_END:
        real->end(args[0], args[1], args[2], args[3], args[4], args[5]);
        break;
    case CALL_FINISH:
        port_GlFinishFrame();
        break;
    case CALL_PAUSE:
        if (g_rec.real_holder != NULL) {
            g_rec.real_holder(args[0], args[1], args[2], args[3], args[4]);
        }
        break;
    case CALL_VIEWS:
        port_FighterUseViews((const PortFighterViews *)body);
        break;
    case CALL_SIDE: {
        const PortRemasterSide *side = body;

        port_RemasterUseSide(side);
        g_rec.drawn_version = side->view.version;
        g_rec.has_drawn_version = TRUE;
        break;
    }
    default:
        break;
    }
}

/* Whether the call is one a step that runs stops the drawing at: a
 * hold, or the stage's view of another version than was drawn last. */
static s32 port_RecordHolds(const Call *call) {
    const PortRemasterSide *side;

    if (call->kind == CALL_HOLD) {
        return TRUE;
    }
    if (call->kind != CALL_SIDE) {
        return FALSE;
    }
    side = (const PortRemasterSide *)(const void *)(call + 1);
    return !g_rec.has_drawn_version ||
           side->view.version != g_rec.drawn_version;
}

/* The calls published and not yet drawn, made, from where the window's
 * thread has got to; `is_still`: the step's thread is still, and all of
 * it is made. While the step runs, a hold stops it for good, and so does
 * the stage's view of another version: the stage's meshes are then built
 * again, from the game's memory and with its coprocessor, which only a
 * still step leaves alone (gl_stage.h). Returns how many were made. */
static u32 port_RecordDrawFrom(s32 is_still) {
    u32 made = 0;

    if (g_rec.is_held && !is_still) {
        return 0;
    }
    if (g_rec.read_block == NULL) {
        Block *first = __atomic_load_n(&g_rec.first, __ATOMIC_ACQUIRE);

        if (first == NULL ||
            __atomic_load_n(&first->record, __ATOMIC_ACQUIRE) != g_rec.record) {
            return 0;
        }
        g_rec.read_block = first;
        g_rec.read_at = 0;
    }
    for (;;) {
        Block *block = g_rec.read_block;
        size_t published = __atomic_load_n(&block->published, __ATOMIC_ACQUIRE);
        Block *next;

        while (g_rec.read_at < published) {
            const Call *call =
                (const Call *)(const void *)(block->data + g_rec.read_at);

            if (!is_still && port_RecordHolds(call)) {
                g_rec.is_held = TRUE;
                return made;
            }
            port_RecordCall(call);
            g_rec.read_at += sizeof(Call) + call->bytes;
            made++;
        }
        /* the step's thread goes on into the next block only once all of
         * this one is published */
        next = __atomic_load_n(&block->next, __ATOMIC_ACQUIRE);
        if (next == NULL ||
            __atomic_load_n(&next->record, __ATOMIC_ACQUIRE) != g_rec.record) {
            return made;
        }
        if (g_rec.read_at <
            __atomic_load_n(&block->published, __ATOMIC_ACQUIRE)) {
            continue;
        }
        g_rec.read_block = next;
        g_rec.read_at = 0;
    }
}

u32 port_GlDrawSoFar(void) {
    u32 made;

    if (!g_rec.is_on || g_rec.real == NULL) {
        return 0;
    }
    /* what the backend looks up of video memory is what it was sent of
     * it, as at a picture between frames (gl.h, g_port_gl_vram) */
    port_GlReadMirrorCopy(TRUE);
    made = port_RecordDrawFrom(FALSE);
    port_GlReadMirrorCopy(FALSE);
    g_rec.counts.streamed += made;
    return made;
}

void port_GlDrawPending(void) {
    if (g_rec.is_on || g_rec.real == NULL) {
        return;
    }
    if (g_rec.lost && g_rec.bytes != 0) {
        g_rec.counts.lost++;
        if (g_rec.counts.lost == 1) {
            port_Log("rebushido: a record of the step had no room: its "
                     "picture is drawn without the rest of it\n");
        }
    }
    port_GlReadMirrorCopy(TRUE);
    if (g_rec.writing != NULL) {
        port_RecordDrawFrom(TRUE);
    }
    port_GlReadMirrorCopy(FALSE);
    /* the game's own again: the step is still, and what is noted now is
     * what it left */
    port_FighterUseViews(NULL);
    port_RemasterUseSide(NULL);
    if (g_rec.bytes > g_rec.counts.most_bytes) {
        g_rec.counts.most_bytes = (u32)g_rec.bytes;
    }
    g_rec.counts.bytes += g_rec.bytes;
    g_rec.writing = NULL;
    g_rec.bytes = 0;
}

const PortGlRecordCounts *port_GlRecordCounts(void) { return &g_rec.counts; }
