/* The native port's game state, saved, put back and hashed, and the
 * self-test that holds it to its word (port_state.h; docs/NETPLAY.md,
 * "Save, load and hash").
 *
 * The self-test is the probe of docs/NETPLAY.md made permanent: with
 * --rollback-test N a run saves the state at a frame's boundary, runs N
 * frames while it notes a hash of every page of the state and of what
 * the run printed, puts the state back and runs the same frames again.
 * Any page or any line that comes out otherwise stops the program and
 * says where. What the frames printed the first time is printed once;
 * the second time it is only compared, so a run with rollbacks prints
 * what the same run without prints. */
#include "common.h"
#include "port_host.h"
#include "port_gpu.h"
#include "port_paint.h"
#include "port_spu.h"
#include "bb2.h"

/* The pieces of memory of a snapshot and of the hash, at most. */
#define STATE_RUNS_MAX 10
/* What the self-test compares the state by. */
#define STATE_PAGE_BYTES 0x1000U
#define STATE_IO_PAGE_ADDR (PORT_SPU_REGS_ADDR & ~(STATE_PAGE_BYTES - 1))
#define STATE_SPU_REGS_BYTES (PORT_SPU_REG_COUNT * 2)
/* The top of the game's stack that a snapshot holds: at a frame's
 * boundary the game is a few calls deep (main, pad_Update,
 * port_TakeInput, the switch), a kilobyte or two. */
#define STATE_STACK_BYTES 0x10000U
#define STATE_STACK_TOP (PORT_STACK_ADDR + PORT_STACK_SIZE)
/* Snapshots whose epochs are kept: netplay's ring, run-ahead's and the
 * self-test's two. */
#define STATE_SLOTS_MAX 32
/* Pages of a difference that are named. */
#define ROLLBACK_NAMED_MAX 24

/* MurmurHash3's mixing (public domain), in two lanes of 32 bits: as
 * quick in the 32-bit program as in the 64-bit one, and the same on
 * either for the same bytes. */
#define HASH_SEED_A 0x9E3779B9U
#define HASH_SEED_B 0x85EBCA77U
#define HASH_WORD_MUL_A 0xCC9E2D51U
#define HASH_WORD_MUL_B 0x1B873593U
#define HASH_WORD_ROTATE 15
#define HASH_LANE_ROTATE 13
#define HASH_LANE_MUL 5
#define HASH_LANE_ADD 0xE6546B64U
#define HASH_FINAL_MUL_A 0x85EBCA6BU
#define HASH_FINAL_MUL_B 0xC2B2AE35U
#define HASH_FINAL_SHIFT_A 16
#define HASH_FINAL_SHIFT_B 13
#define HASH_WORD_BYTES 4
#define HASH_PAIR_BYTES 8
#define BITS_PER_BYTE 8
#define WORD_BITS 32

enum {
    ROLLBACK_OFF,
    ROLLBACK_WAIT,  /* frames to run before the next save */
    ROLLBACK_FIRST, /* the frames after a save, noted */
    ROLLBACK_AGAIN  /* the same frames after the load, compared */
};

/* A word read wherever it lies: the pieces need not start on a word. */
typedef u32 __attribute__((may_alias, aligned(1))) PortLooseWord;

typedef struct {
    u8 *start;
    u32 bytes;
    const char *name;
    s32 is_paged; /* big enough to be kept by the pages written */
} PortStateRun;

typedef struct {
    u32 a;
    u32 b;
} PortHashLanes;

/* The shortest, the longest and all of a kind of time, in the host's
 * ticks. */
typedef struct {
    u32 count;
    u32 least;
    u32 most;
    u32 total;
} PortTiming;

static PortStateRun g_port_state_saved[STATE_RUNS_MAX];
static s32 g_port_state_saved_count;
static PortStateRun g_port_state_hashed[STATE_RUNS_MAX];
static s32 g_port_state_hashed_count;
static u32 g_port_state_bytes;

static s32 g_port_rollback_phase;
static u32 g_port_rollback_frames;
static u32 g_port_rollback_every;
/* WAIT: frames to the next save. FIRST and AGAIN: frames since it. */
static u32 g_port_rollback_count;
/* The frames the first run took, and the save's blank. */
static u32 g_port_rollback_length;
static u32 g_port_rollback_blank;
static u8 *g_port_rollback_snapshot;
/* The state after the first frame of the first run, kept whole so that a
 * difference there is named to the word. */
static u8 *g_port_rollback_first_frame;
/* For each frame of the first run: a hash of each page of the state, a
 * hash of the state as port_StateHash makes it, and of what it printed. */
static u64 *g_port_rollback_pages;
static u32 g_port_rollback_page_count;
static u64 *g_port_rollback_hashes;
static u64 *g_port_rollback_texts;
static PortHashLanes g_port_rollback_text;
static u32 g_port_rollback_text_bytes;
static u64 *g_port_rollback_here;
/* What the report says. */
static u32 g_port_rollback_rounds;
static u32 g_port_rollback_compared;
static s32 g_port_rollback_failed;
static PortTiming g_port_rollback_saves;
static PortTiming g_port_rollback_loads;
static PortTiming g_port_rollback_hashing;
/* --rollback-quiet: the frames run again as netplay runs them, quietly
 * (g_port_rerun), and compared but for video memory, which they do not
 * paint. */
static s32 g_port_rollback_quiet;
/* How long a step took: between two saves, run first, run again. */
static PortTiming g_port_rollback_steps[ROLLBACK_AGAIN + 1];
/* ... of those, the steps of a fight (its screen's handler ran). */
static PortTiming g_port_rollback_fight_steps[ROLLBACK_AGAIN + 1];
static u32 g_port_rollback_step_start;

/* ---- the pieces ---- */

static void port_StateAddRun(PortStateRun *runs, s32 *count, const char *name,
                             void *start, PortAddress end) {
    if (*count >= STATE_RUNS_MAX) {
        port_Fail(
            "rebushido: the state has more than %d pieces\n", STATE_RUNS_MAX);
    }
    runs[*count].start = start;
    runs[*count].bytes = (u32)(end - (PortAddress)start);
    runs[*count].name = name;
    runs[*count].is_paged = 0;
    (*count)++;
}

/* What is saved and what is hashed (port_state.h), found once. */
static void port_StateFindPieces(void) {
    PortStateRun *saved = g_port_state_saved;
    PortStateRun *hashed = g_port_state_hashed;
    u8 *ram = (u8 *)ADDR(PORT_RAM_ADDR);
    u8 *scratchpad = (u8 *)ADDR(SCRATCHPAD);
    u8 *io_page = (u8 *)ADDR(STATE_IO_PAGE_ADDR);
    u8 *spu_regs = (u8 *)ADDR(PORT_SPU_REGS_ADDR);
    u8 *spu_ram = port_GetSpuRam();
    u8 *stack = (u8 *)ADDR(STATE_STACK_TOP - STATE_STACK_BYTES);
    s32 i;

    if (g_port_state_saved_count != 0) {
        return;
    }
    port_StateAddRun(saved, &g_port_state_saved_count, "ram", ram,
                     (PortAddress)ram + PORT_RAM_SIZE);
    port_StateAddRun(saved, &g_port_state_saved_count, "scratchpad", scratchpad,
                     (PortAddress)scratchpad + PORT_SCRATCHPAD_SIZE);
    port_StateAddRun(saved, &g_port_state_saved_count, "io", io_page,
                     (PortAddress)io_page + STATE_PAGE_BYTES);
    port_StateAddRun(saved, &g_port_state_saved_count, "stack", stack,
                     (PortAddress)stack + STATE_STACK_BYTES);
    port_StateAddRun(
        saved, &g_port_state_saved_count, "context", &g_port_game_context,
        (PortAddress)(&g_port_game_context + 1));
    if (port_RemasterPrimBufferBytes() != 0) {
        u8 *prims = (u8 *)ADDR(PORT_PRIMS_ADDR);

        port_StateAddRun(saved, &g_port_state_saved_count, "prims", prims,
                         (PortAddress)prims + PORT_PRIMS_SIZE);
    }
    port_StateAddRun(saved, &g_port_state_saved_count, "data", g_port_game_data,
                     (PortAddress)g_port_state_data_end);
    port_StateAddRun(saved, &g_port_state_saved_count, "bss", g_port_game_bss,
                     (PortAddress)g_port_state_bss_end);
    /* the four big pieces are kept by the pages written; the stack never
     * is, which a fault's handler may run on */
    for (i = 0; i < g_port_state_saved_count; i++) {
        u8 *start = saved[i].start;

        saved[i].is_paged =
            start == ram || start == (u8 *)ADDR(PORT_PRIMS_ADDR) ||
            start == (u8 *)g_port_game_data || start == (u8 *)g_port_game_bss;
    }

    port_StateAddRun(hashed, &g_port_state_hashed_count, "ram", ram,
                     (PortAddress)ram + PORT_RAM_SIZE);
    port_StateAddRun(
        hashed, &g_port_state_hashed_count, "scratchpad", scratchpad,
        (PortAddress)scratchpad + PORT_SCRATCHPAD_SIZE);
    port_StateAddRun(hashed, &g_port_state_hashed_count, "spu", spu_regs,
                     (PortAddress)spu_regs + STATE_SPU_REGS_BYTES);
    port_StateAddRun(hashed, &g_port_state_hashed_count, "spu ram", spu_ram,
                     (PortAddress)spu_ram + PORT_SPU_RAM_BYTES);
    port_StateAddRun(hashed, &g_port_state_hashed_count, "gte", &g_port_gte,
                     (PortAddress)(&g_port_gte + 1));
    port_StateAddRun(hashed, &g_port_state_hashed_count, "game data",
                     g_port_game_data, (PortAddress)g_port_game_data_end);
    port_StateAddRun(hashed, &g_port_state_hashed_count, "game bss",
                     g_port_game_bss, (PortAddress)g_port_game_bss_end);

    g_port_state_bytes = 0;
    for (i = 0; i < g_port_state_saved_count; i++) {
        g_port_state_bytes += saved[i].bytes;
    }
}

/* ---- save and load ---- */

/* What lies below the game's stack pointer at a boundary is no frame's:
 * whatever the last calls deepest down left there, which is not the
 * game's alone (the self-test's tap prints the first time and only
 * compares the second). It is cleared before the stack is saved or
 * compared, so that a snapshot holds the stack's frames and nothing
 * else, and a function that reads a local it never set reads the same
 * the second time as the first, or the self-test says so. */
static void port_StateClearDeadStack(void) {
    PortAddress bottom = (PortAddress)ADDR(STATE_STACK_TOP - STATE_STACK_BYTES);
    PortAddress sp = g_port_game_context;
    u8 *byte;

    if (sp == 0) {
        return;
    }
    if (sp < bottom || sp > (PortAddress)ADDR(STATE_STACK_TOP)) {
        port_Fail(
            "rebushido: the game's stack is %u bytes deep at a "
            "frame's boundary, more than the state holds (%u)\n",
            (u32)((PortAddress)ADDR(STATE_STACK_TOP) - sp), STATE_STACK_BYTES);
    }
    for (byte = (u8 *)bottom; byte < (u8 *)sp; byte++) {
        *byte = 0;
    }
}

/* `bytes` from `src` to `dest`. */
static void port_StateCopy(void *dest, const void *src, u32 bytes) {
#if defined(__i386__) || defined(__x86_64__)
    PortAddress count = bytes;

    /* the processor's own copy, which is the quickest it has */
    __asm__ volatile("rep movsb"
                     : "+D"(dest), "+S"(src), "+c"(count)
                     :
                     : "memory");
#else
    u8 *to = dest;
    const u8 *from = src;
    u32 i;

    for (i = 0; i < bytes; i++) {
        to[i] = from[i];
    }
#endif
}

u32 port_StateSize(void) {
    port_StateFindPieces();
    return g_port_state_bytes;
}

/* ---- the pages written ---- */

/* Where a run starts tracking, the pieces that are big enough to be
 * tracked are cut into pages (docs/NETPLAY.md, "Save, load and hash"):
 * each page notes the epoch it was last written in. The epoch goes on
 * by one at every save and load, and a snapshot notes the epoch it was
 * taken in, so that a page written in a later epoch is one the snapshot
 * may not hold as it is now, and a page not written since is one it
 * does. A save copies only the first kind into its snapshot, a load
 * only the first kind back.
 *
 * What writes a page is found two ways. Video memory is written by the
 * software GPU alone, a line at a time, and it marks the lines it
 * writes (g_port_vram_lines_written). Every other page is made
 * read-only by the system after each save and load: the first write to
 * it faults, and port_StateOnWrite notes it and makes it writable for
 * the rest of the epoch. */

/* The piece's pages: what lies between its first and last page bound;
 * the bytes before and after are copied every time. */
typedef struct {
    u8 *start;
    u32 pages;
    u32 first;  /* the number of its first page among all of them */
    u32 offset; /* where `start` is in a snapshot */
    s32 run;    /* its piece */
} PortStateSpan;

typedef struct {
    const void *buf;
    u32 epoch;
} PortStateSlot;

enum {
    PAGE_WATCHED,  /* read-only: a write to it is noted */
    PAGE_WRITABLE, /* written in this epoch, writable until its end */
    PAGE_PAINTED,  /* video memory's, which the GPU marks itself */
    PAGE_HOT       /* written in the last two epochs: left writable, and
                    * taken for written in every epoch, which is cheaper
                    * than a fault every epoch */
};

/* Epochs a hot page is left writable for before it is watched again, to
 * see whether it is still written. */
#define HOT_EPOCHS 128

enum {
    TRACKING_NOT_YET,
    TRACKING_ON,
    TRACKING_OFF /* --whole-state, or the system would not */
};

u8 g_port_vram_lines_written[PORT_VRAM_HEIGHT];

static PortStateSpan g_port_state_spans[STATE_RUNS_MAX];
static s32 g_port_state_span_count;
static u32 g_port_state_page_count;
static u32 *g_port_state_page_epochs;
static u8 *g_port_state_page_kinds;
/* Whether the page was written in the epoch before (a fault or a load):
 * a second such epoch makes it hot. */
static u8 *g_port_state_page_recent;
static u32 g_port_state_writable;
static u32 g_port_state_epoch = 1;
static PortStateSlot g_port_state_slots[STATE_SLOTS_MAX];
static s32 g_port_state_slot_count;
static s32 g_port_state_tracking;
/* Writes noted by a fault, all through the run. */
static u32 g_port_state_faults;

u32 port_StateFaults(void) { return g_port_state_faults; }

void port_SetWholeState(void) { g_port_state_tracking = TRACKING_OFF; }

/* A write to a page made read-only (port_HostWatchWrites): one of the
 * state's, noted and made writable, or not the state's (0). */
static s32 port_StateOnWrite(PortAddress address) {
    s32 i;

    for (i = 0; i < g_port_state_span_count; i++) {
        const PortStateSpan *span = &g_port_state_spans[i];
        PortAddress start = (PortAddress)span->start;
        u32 page;

        if (address < start ||
            address - start >= (PortAddress)span->pages * STATE_PAGE_BYTES) {
            continue;
        }
        page = (u32)((address - start) / STATE_PAGE_BYTES);
        if (g_port_state_page_kinds[span->first + page] != PAGE_WATCHED ||
            !port_HostProtect(
                span->start + page * STATE_PAGE_BYTES, STATE_PAGE_BYTES, 1)) {
            return 0;
        }
        g_port_state_page_kinds[span->first + page] = PAGE_WRITABLE;
        g_port_state_page_epochs[span->first + page] = g_port_state_epoch;
        g_port_state_writable++;
        g_port_state_faults++;
        return 1;
    }
    return 0;
}

/* The pages of `span` from `from` to `to` made read-only (or writable):
 * as one, or a page at a time where the system keeps them in pieces of
 * its own. */
static s32 port_StateProtect(
    const PortStateSpan *span, u32 from, u32 to, s32 is_writable) {
    u32 page;

    if (port_HostProtect(span->start + from * STATE_PAGE_BYTES,
                         (to - from) * STATE_PAGE_BYTES, is_writable)) {
        return 1;
    }
    for (page = from; page < to; page++) {
        if (!port_HostProtect(span->start + page * STATE_PAGE_BYTES,
                              STATE_PAGE_BYTES, is_writable)) {
            return 0;
        }
    }
    return 1;
}

/* Every page that is of kind `from` made kind `to`, read-only or
 * writable to match. */
static s32 port_StateProtectAll(u8 from, u8 to) {
    s32 i;

    for (i = 0; i < g_port_state_span_count; i++) {
        const PortStateSpan *span = &g_port_state_spans[i];
        u8 *kinds = g_port_state_page_kinds + span->first;
        u32 page = 0;

        while (page < span->pages) {
            u32 end;

            if (kinds[page] != from) {
                page++;
                continue;
            }
            for (end = page; end < span->pages && kinds[end] == from; end++) {
                kinds[end] = to;
            }
            if (!port_StateProtect(span, page, end, to == PAGE_WRITABLE)) {
                return 0;
            }
            page = end;
        }
    }
    return 1;
}

/* The end of an epoch: what was written in it is watched again. */
static void port_StateNextEpoch(void) {
    s32 is_cooling;
    s32 i;

    g_port_state_epoch++;
    is_cooling = g_port_state_epoch % HOT_EPOCHS == 0;
    g_port_state_writable = 0;
    for (i = 0; i < g_port_state_span_count; i++) {
        const PortStateSpan *span = &g_port_state_spans[i];
        u8 *kinds = g_port_state_page_kinds + span->first;
        u8 *recent = g_port_state_page_recent + span->first;
        u32 page = 0;

        while (page < span->pages) {
            u32 end;

            switch (kinds[page]) {
            case PAGE_WATCHED:
                recent[page] = 0;
                page++;
                continue;
            case PAGE_WRITABLE:
                if (recent[page]) {
                    kinds[page] = PAGE_HOT;
                    page++;
                    continue;
                }
                break;
            case PAGE_HOT:
                if (is_cooling) {
                    break;
                }
                page++;
                continue;
            default:
                page++;
                continue;
            }
            /* this page and those after it of the same kind that are
             * watched again: read-only, as one */
            for (end = page; end < span->pages &&
                             ((kinds[end] == PAGE_WRITABLE && !recent[end]) ||
                              (kinds[end] == PAGE_HOT && is_cooling));
                 end++) {
                recent[end] = kinds[end] == PAGE_WRITABLE;
                kinds[end] = PAGE_WATCHED;
            }
            if (!port_StateProtect(span, page, end, 0)) {
                port_Fail("rebushido: the state's pages cannot be made "
                          "read-only again\n");
            }
            page = end;
        }
    }
}

/* The hot pages, taken for written in this epoch. */
static void port_StateTakeHot(void) {
    u32 page;

    for (page = 0; page < g_port_state_page_count; page++) {
        if (g_port_state_page_kinds[page] == PAGE_HOT) {
            g_port_state_page_epochs[page] = g_port_state_epoch;
        }
    }
}

/* The lines of video memory the GPU has written since the last look,
 * noted as their pages written in this epoch. */
static void port_StateTakePainted(void) {
    s32 i;
    u32 line;

    for (line = 0; line < PORT_VRAM_HEIGHT; line++) {
        PortAddress from;
        PortAddress to;

        if (!g_port_vram_lines_written[line]) {
            continue;
        }
        g_port_vram_lines_written[line] = 0;
        from = (PortAddress)&g_port_vram[line * PORT_VRAM_WIDTH];
        to = from + PORT_VRAM_WIDTH * sizeof(u16);
        for (i = 0; i < g_port_state_span_count; i++) {
            const PortStateSpan *span = &g_port_state_spans[i];
            PortAddress start = (PortAddress)span->start;
            PortAddress end =
                start + (PortAddress)span->pages * STATE_PAGE_BYTES;
            PortAddress at;

            if (to <= start || from >= end) {
                continue;
            }
            for (at = from < start ? start : from; at < to && at < end;
                 at = (at & ~(PortAddress)(STATE_PAGE_BYTES - 1)) +
                      STATE_PAGE_BYTES) {
                g_port_state_page_epochs
                    [span->first + (u32)((at - start) / STATE_PAGE_BYTES)] =
                        g_port_state_epoch;
            }
        }
    }
}

/* The pages, found and watched the first time a snapshot is taken: 0
 * if the run keeps none (--whole-state), or the system will not. */
static s32 port_StateTrack(void) {
    PortAddress vram_from = (PortAddress)g_port_vram;
    PortAddress vram_to = vram_from + PORT_VRAM_BYTES;
    u32 offset = 0;
    u32 page;
    s32 i;

    if (g_port_state_tracking != TRACKING_NOT_YET) {
        return g_port_state_tracking == TRACKING_ON;
    }
    g_port_state_tracking = TRACKING_OFF;
    for (i = 0; i < g_port_state_saved_count; i++) {
        const PortStateRun *run = &g_port_state_saved[i];
        PortAddress start = ((PortAddress)run->start + STATE_PAGE_BYTES - 1) &
                            ~(PortAddress)(STATE_PAGE_BYTES - 1);
        PortAddress end = ((PortAddress)run->start + run->bytes) &
                          ~(PortAddress)(STATE_PAGE_BYTES - 1);

        if (run->is_paged && end > start) {
            PortStateSpan *span =
                &g_port_state_spans[g_port_state_span_count++];

            span->start = (u8 *)start;
            span->pages = (u32)((end - start) / STATE_PAGE_BYTES);
            span->first = g_port_state_page_count;
            span->offset = offset + (u32)(start - (PortAddress)run->start);
            span->run = i;
            g_port_state_page_count += span->pages;
        }
        offset += run->bytes;
    }
    g_port_state_page_epochs =
        port_HostAllocate(g_port_state_page_count * sizeof(u32));
    g_port_state_page_kinds = port_HostAllocate(g_port_state_page_count);
    g_port_state_page_recent = port_HostAllocate(g_port_state_page_count);
    if (g_port_state_page_epochs == NULL || g_port_state_page_kinds == NULL ||
        g_port_state_page_recent == NULL) {
        g_port_state_span_count = 0;
        return 0;
    }
    for (i = 0; i < g_port_state_span_count; i++) {
        const PortStateSpan *span = &g_port_state_spans[i];

        for (page = 0; page < span->pages; page++) {
            PortAddress at = (PortAddress)span->start + page * STATE_PAGE_BYTES;

            g_port_state_page_epochs[span->first + page] = g_port_state_epoch;
            /* a page that is video memory's alone is the GPU's to mark */
            g_port_state_page_kinds[span->first + page] =
                at >= vram_from && at + STATE_PAGE_BYTES <= vram_to
                    ? PAGE_PAINTED
                    : PAGE_WRITABLE;
        }
    }
    for (page = 0; page < PORT_VRAM_HEIGHT; page++) {
        g_port_vram_lines_written[page] = 0;
    }
    port_HostWatchWrites(port_StateOnWrite);
    if (!port_StateProtectAll(PAGE_WRITABLE, PAGE_WATCHED)) {
        port_StateProtectAll(PAGE_WATCHED, PAGE_WRITABLE);
        g_port_state_span_count = 0;
        port_Log("rebushido: the state's pages cannot be watched: it is "
                 "saved and put back whole\n");
        return 0;
    }
    g_port_state_tracking = TRACKING_ON;
    return 1;
}

/* The snapshot `buf` was last taken in, or a new one (epoch 0: it holds
 * nothing yet); NULL where one is not known and `is_new` is 0. */
static PortStateSlot *port_StateFindSlot(const void *buf, s32 is_new) {
    s32 i;

    for (i = 0; i < g_port_state_slot_count; i++) {
        if (g_port_state_slots[i].buf == buf) {
            return &g_port_state_slots[i];
        }
    }
    if (!is_new) {
        return NULL;
    }
    if (g_port_state_slot_count == STATE_SLOTS_MAX) {
        port_Fail("rebushido: more than %d snapshots of the state\n",
                  STATE_SLOTS_MAX);
    }
    g_port_state_slots[g_port_state_slot_count].buf = buf;
    g_port_state_slots[g_port_state_slot_count].epoch = 0;
    return &g_port_state_slots[g_port_state_slot_count++];
}

/* Whether no other snapshot is newer than `slot`. */
static s32 port_StateIsNewest(const PortStateSlot *slot) {
    s32 i;

    for (i = 0; i < g_port_state_slot_count; i++) {
        if (g_port_state_slots[i].epoch > slot->epoch) {
            return 0;
        }
    }
    return 1;
}

/* The bytes of the pieces that are not a span's pages, between the live
 * state and a snapshot: `to_snapshot` says which way. */
static void port_StateCopyRest(u8 *snapshot, s32 to_snapshot) {
    u32 offset = 0;
    s32 span = 0;
    s32 i;

    for (i = 0; i < g_port_state_saved_count; i++) {
        const PortStateRun *run = &g_port_state_saved[i];
        /* the part that is pages, if any: from `from` to `to` */
        u32 from = run->bytes;
        u32 to = run->bytes;
        u32 part;

        if (span < g_port_state_span_count &&
            g_port_state_spans[span].run == i) {
            from = g_port_state_spans[span].offset - offset;
            to = from + g_port_state_spans[span].pages * STATE_PAGE_BYTES;
            span++;
        }
        for (part = 0; part < 2; part++) {
            u32 start = part == 0 ? 0 : to;
            u32 end = part == 0 ? from : run->bytes;

            if (end <= start) {
                continue;
            }
            if (to_snapshot) {
                port_StateCopy(
                    snapshot + offset + start, run->start + start, end - start);
            } else {
                port_StateCopy(
                    run->start + start, snapshot + offset + start, end - start);
            }
        }
        offset += run->bytes;
    }
}

/* The pieces, one after another (port_state.h). */
void port_StateSave(void *buf) {
    u8 *out = buf;
    PortStateSlot *slot;
    s32 i;

    /* video memory as every primitive given leaves it (port_paint.h) */
    port_PaintSyncAll();
    port_StateFindPieces();
    port_StateClearDeadStack();
    if (!port_StateTrack()) {
        for (i = 0; i < g_port_state_saved_count; i++) {
            port_StateCopy(
                out, g_port_state_saved[i].start, g_port_state_saved[i].bytes);
            out += g_port_state_saved[i].bytes;
        }
        return;
    }
    slot = port_StateFindSlot(buf, 1);
    port_StateTakePainted();
    port_StateTakeHot();
    port_StateCopyRest(out, 1);
    for (i = 0; i < g_port_state_span_count; i++) {
        const PortStateSpan *span = &g_port_state_spans[i];
        const u32 *epochs = g_port_state_page_epochs + span->first;
        u32 page = 0;

        while (page < span->pages) {
            u32 end;

            if (epochs[page] <= slot->epoch) {
                page++;
                continue;
            }
            for (end = page; end < span->pages && epochs[end] > slot->epoch;
                 end++) {
            }
            port_StateCopy(out + span->offset + page * STATE_PAGE_BYTES,
                           span->start + page * STATE_PAGE_BYTES,
                           (end - page) * STATE_PAGE_BYTES);
            page = end;
        }
    }
    slot->epoch = g_port_state_epoch;
    port_StateNextEpoch();
}

/* The pages written since `buf` was taken, back from it, and the rest
 * of the pieces whole. */
static void port_StateLoadPieces(const void *buf) {
    const u8 *in = buf;
    PortStateSlot *slot;
    u32 since;
    u32 mark;
    s32 i;

    port_StateFindPieces();
    if (!port_StateTrack()) {
        for (i = 0; i < g_port_state_saved_count; i++) {
            port_StateCopy(
                g_port_state_saved[i].start, in, g_port_state_saved[i].bytes);
            in += g_port_state_saved[i].bytes;
        }
        return;
    }
    port_StateTakePainted();
    port_StateTakeHot();
    slot = port_StateFindSlot(buf, 0);
    /* a snapshot not taken here: every page, as written now */
    since = slot != NULL ? slot->epoch : 0;
    /* a page put back holds what the snapshot does; where no snapshot is
     * newer, it holds what every snapshot taken since it does not, and
     * is noted as of its epoch: a save into it again leaves it */
    mark = slot != NULL && port_StateIsNewest(slot)
               ? slot->epoch
               : g_port_state_epoch;
    port_StateCopyRest((u8 *)in, 0);
    for (i = 0; i < g_port_state_span_count; i++) {
        const PortStateSpan *span = &g_port_state_spans[i];
        u32 *epochs = g_port_state_page_epochs + span->first;
        u8 *kinds = g_port_state_page_kinds + span->first;
        u32 page = 0;

        while (page < span->pages) {
            u32 end;
            u32 at;

            if (epochs[page] <= since) {
                page++;
                continue;
            }
            for (end = page; end < span->pages && epochs[end] > since; end++) {
            }
            /* written before an epoch that has ended: read-only again */
            at = page;
            while (at < end) {
                u32 last;

                if (kinds[at] != PAGE_WATCHED) {
                    at++;
                    continue;
                }
                for (last = at; last < end && kinds[last] == PAGE_WATCHED;
                     last++) {
                    kinds[last] = PAGE_WRITABLE;
                    g_port_state_writable++;
                }
                if (!port_StateProtect(span, at, last, 1)) {
                    port_Fail("rebushido: the state's pages cannot be made "
                              "writable\n");
                }
                at = last;
            }
            port_StateCopy(span->start + page * STATE_PAGE_BYTES,
                           in + span->offset + page * STATE_PAGE_BYTES,
                           (end - page) * STATE_PAGE_BYTES);
            for (at = page; at < end; at++) {
                epochs[at] = mark;
            }
            page = end;
        }
    }
    port_StateNextEpoch();
}

/* The state put back (port_state.h), with nothing left to paint first,
 * and the GPU's settings given to the painter, which keeps its own
 * outside the state (port_paint.h). */
void port_StateLoad(const void *buf) {
    port_PaintSyncAll();
    port_StateLoadPieces(buf);
    port_PaintTakeSettings();
}

/* ---- the hash ---- */

static u32 port_HashRotate(u32 value, s32 bits) {
    return (value << bits) | (value >> (WORD_BITS - bits));
}

static u32 port_HashMix(u32 lane, u32 word) {
    word *= HASH_WORD_MUL_A;
    word = port_HashRotate(word, HASH_WORD_ROTATE);
    word *= HASH_WORD_MUL_B;
    lane ^= word;
    lane = port_HashRotate(lane, HASH_LANE_ROTATE);
    return lane * HASH_LANE_MUL + HASH_LANE_ADD;
}

static u32 port_HashFinal(u32 lane, u32 bytes) {
    lane ^= bytes;
    lane ^= lane >> HASH_FINAL_SHIFT_A;
    lane *= HASH_FINAL_MUL_A;
    lane ^= lane >> HASH_FINAL_SHIFT_B;
    lane *= HASH_FINAL_MUL_B;
    lane ^= lane >> HASH_FINAL_SHIFT_A;
    return lane;
}

static void port_HashStart(PortHashLanes *lanes) {
    lanes->a = HASH_SEED_A;
    lanes->b = HASH_SEED_B;
}

/* `bytes` more bytes: a word to each lane in turn, and what is left of a
 * pair to the first, a byte at a time. */
static void port_HashBytes(PortHashLanes *lanes, const u8 *start, u32 bytes) {
    const PortLooseWord *words = (const PortLooseWord *)start;
    u32 pairs = bytes / HASH_PAIR_BYTES;
    u32 first = lanes->a;
    u32 second = lanes->b;
    u32 i;

    for (i = 0; i < pairs; i++) {
        first = port_HashMix(first, words[2 * i]);
        second = port_HashMix(second, words[2 * i + 1]);
    }
    for (i = pairs * HASH_PAIR_BYTES; i < bytes; i++) {
        first = port_HashMix(first, start[i]);
    }
    lanes->a = first;
    lanes->b = second;
}

static u64 port_HashEnd(const PortHashLanes *lanes, u32 bytes) {
    return ((u64)port_HashFinal(lanes->a, bytes) << WORD_BITS) |
           port_HashFinal(lanes->b, bytes);
}

/* What the console would hold, as one hash (port_state.h). */
u64 port_StateHash(void) {
    PortHashLanes lanes;
    u32 bytes = 0;
    s32 i;

    port_PaintSyncAll();
    port_StateFindPieces();
    port_HashStart(&lanes);
    for (i = 0; i < g_port_state_hashed_count; i++) {
        port_HashBytes(
            &lanes, g_port_state_hashed[i].start, g_port_state_hashed[i].bytes);
        bytes += g_port_state_hashed[i].bytes;
    }
    return port_HashEnd(&lanes, bytes);
}

/* A hash of each page of what a snapshot holds, into `out`, in the
 * snapshot's order: g_port_rollback_page_count of them. */
static void port_HashPages(u64 *out) {
    s32 i;

    port_StateClearDeadStack();
    for (i = 0; i < g_port_state_saved_count; i++) {
        const PortStateRun *run = &g_port_state_saved[i];
        u32 offset;

        for (offset = 0; offset < run->bytes; offset += STATE_PAGE_BYTES) {
            u32 bytes = run->bytes - offset;
            PortHashLanes lanes;

            if (bytes > STATE_PAGE_BYTES) {
                bytes = STATE_PAGE_BYTES;
            }
            port_HashStart(&lanes);
            port_HashBytes(&lanes, run->start + offset, bytes);
            *out++ = port_HashEnd(&lanes, bytes);
        }
    }
}

/* ---- the self-test ---- */

static void port_TimingAdd(PortTiming *timing, u32 ticks) {
    if (timing->count == 0 || ticks < timing->least) {
        timing->least = ticks;
    }
    if (ticks > timing->most) {
        timing->most = ticks;
    }
    timing->total += ticks;
    timing->count++;
}

/* What the run prints: noted while the frames after a save run, and
 * only compared, not printed, while they run again. */
static s32 port_RollbackTap(const char *text, u32 length) {
    if (g_port_rollback_phase == ROLLBACK_FIRST ||
        g_port_rollback_phase == ROLLBACK_AGAIN) {
        port_HashBytes(&g_port_rollback_text, (const u8 *)text, length);
        g_port_rollback_text_bytes += length;
    }
    return g_port_rollback_phase != ROLLBACK_AGAIN;
}

/* What was printed since the last frame's boundary, as one hash. */
static u64 port_RollbackTakeText(void) {
    u64 hash = port_HashEnd(&g_port_rollback_text, g_port_rollback_text_bytes);

    port_HashStart(&g_port_rollback_text);
    g_port_rollback_text_bytes = 0;
    return hash;
}

void port_StartRollbackTest(u32 frames, u32 every, s32 quiet) {
    g_port_rollback_quiet = quiet;
    g_port_rollback_frames = frames;
    g_port_rollback_every = every < frames ? frames : every;
    g_port_rollback_phase = ROLLBACK_WAIT;
    g_port_rollback_count = g_port_rollback_every;
    g_port_print_tap = port_RollbackTap;
}

/* The snapshot and the notes of a round, the first time they are
 * needed: the state is only whole once the game runs. */
static void port_RollbackAllocate(void) {
    u32 frames = g_port_rollback_frames;
    u32 pages = 0;
    s32 i;

    if (g_port_rollback_snapshot != NULL) {
        return;
    }
    port_StateFindPieces();
    for (i = 0; i < g_port_state_saved_count; i++) {
        pages += (g_port_state_saved[i].bytes + STATE_PAGE_BYTES - 1) /
                 STATE_PAGE_BYTES;
    }
    g_port_rollback_page_count = pages;
    g_port_rollback_snapshot = port_HostAllocate(g_port_state_bytes);
    g_port_rollback_first_frame = port_HostAllocate(g_port_state_bytes);
    g_port_rollback_pages = port_HostAllocate(frames * pages * sizeof(u64));
    g_port_rollback_here = port_HostAllocate(pages * sizeof(u64));
    g_port_rollback_hashes = port_HostAllocate(frames * sizeof(u64));
    g_port_rollback_texts = port_HostAllocate(frames * sizeof(u64));
    if (g_port_rollback_snapshot == NULL ||
        g_port_rollback_first_frame == NULL || g_port_rollback_pages == NULL ||
        g_port_rollback_here == NULL || g_port_rollback_hashes == NULL ||
        g_port_rollback_texts == NULL) {
        port_Fail("rebushido: no memory for the rollback self-test's "
                  "snapshot (%u bytes)\n",
                  g_port_state_bytes);
    }
}

/* A round begins: the snapshot, and the first run of its frames. */
static void port_RollbackSave(void) {
    u32 start;

    port_RollbackAllocate();
    start = port_HostTicks();
    port_StateSave(g_port_rollback_snapshot);
    port_TimingAdd(&g_port_rollback_saves, port_HostTicks() - start);
    g_port_rollback_phase = ROLLBACK_FIRST;
    g_port_rollback_count = 0;
    g_port_rollback_blank = port_GetBlankCount();
    port_RollbackTakeText();
}

/* Where a page of the snapshot is: its piece and its offset there. */
static const PortStateRun *port_RollbackFindPage(u32 page, u32 *offset) {
    s32 i;

    for (i = 0; i < g_port_state_saved_count; i++) {
        u32 pages = (g_port_state_saved[i].bytes + STATE_PAGE_BYTES - 1) /
                    STATE_PAGE_BYTES;

        if (page < pages) {
            *offset = page * STATE_PAGE_BYTES;
            return &g_port_state_saved[i];
        }
        page -= pages;
    }
    *offset = 0;
    return NULL;
}

/* Whether `bytes` from `at` are ones a quiet run again need not give as
 * the first run did: video memory, which it does not paint, and the
 * game's stack, where the registers its functions keep for their callers
 * may hold what a painting function left in them and nobody reads again
 * (the run that is not quiet holds the stack to the byte). */
static s32 port_RollbackIsUnpainted(PortAddress at, u32 bytes) {
    if (!g_port_rollback_quiet) {
        return 0;
    }
    if (at + bytes > (PortAddress)ADDR(STATE_STACK_TOP - STATE_STACK_BYTES) &&
        at < (PortAddress)ADDR(STATE_STACK_TOP)) {
        return 1;
    }
    return at + bytes > (PortAddress)g_port_vram &&
           at < (PortAddress)(g_port_vram + PORT_VRAM_PIXELS);
}

/* The same for a page of the snapshot. */
static s32 port_RollbackIsUnpaintedPage(u32 page) {
    const PortStateRun *run;
    u32 offset;

    if (!g_port_rollback_quiet) {
        return 0;
    }
    run = port_RollbackFindPage(page, &offset);
    return run != NULL &&
           port_RollbackIsUnpainted(
               (PortAddress)(run->start + offset), STATE_PAGE_BYTES);
}

/* The first frame after the save, run again, against the state it left
 * the first time, which is kept whole: the words that differ. */
static void port_RollbackNameWords(void) {
    const u8 *first = g_port_rollback_first_frame;
    u32 named = 0;
    s32 i;

    for (i = 0; i < g_port_state_saved_count; i++) {
        const PortStateRun *run = &g_port_state_saved[i];
        const PortLooseWord *was = (const PortLooseWord *)first;
        const PortLooseWord *now = (const PortLooseWord *)run->start;
        u32 word;

        for (word = 0; word < run->bytes / HASH_WORD_BYTES; word++) {
            if (was[word] == now[word] ||
                port_RollbackIsUnpainted(
                    (PortAddress)(run->start + word * HASH_WORD_BYTES),
                    HASH_WORD_BYTES)) {
                continue;
            }
            if (named++ == ROLLBACK_NAMED_MAX) {
                port_Log("rollback: differs: and more words\n");
                return;
            }
            port_Log("rollback: differs: the word at 0x%08X: 0x%08X, the "
                     "first time 0x%08X\n",
                     (u32)(PortAddress)(run->start + word * HASH_WORD_BYTES),
                     now[word], was[word]);
        }
        first += run->bytes;
    }
}

/* The frame run again is not the frame run first: what differs, and
 * the end of the program. */
static void port_RollbackFail(const u64 *first, u64 text, u64 hash) {
    u32 named = 0;
    u32 page;

    g_port_rollback_failed = 1;
    port_Log("rollback: FAILED: frame %u of %u, run again after the save at "
             "blank %u, is not what it was\n",
             g_port_rollback_count, g_port_rollback_length,
             g_port_rollback_blank);
    for (page = 0; page < g_port_rollback_page_count; page++) {
        const PortStateRun *run;
        u32 offset;

        if (first[page] == g_port_rollback_here[page] ||
            port_RollbackIsUnpaintedPage(page)) {
            continue;
        }
        if (named++ == ROLLBACK_NAMED_MAX) {
            port_Log("rollback: differs: and more\n");
            break;
        }
        run = port_RollbackFindPage(page, &offset);
        if (run == NULL) {
            break;
        }
        port_Log("rollback: differs: %s +0x%X at 0x%08X\n", run->name, offset,
                 (u32)(PortAddress)(run->start + offset));
    }
    if (g_port_rollback_count == 1) {
        port_RollbackNameWords();
    }
    if (text != g_port_rollback_texts[g_port_rollback_count - 1]) {
        port_Log("rollback: differs: what the frame printed\n");
    }
    if (hash != g_port_rollback_hashes[g_port_rollback_count - 1]) {
        port_Log("rollback: differs: port_StateHash\n");
    }
    port_Quit(PORT_EXIT_FAILED);
}

/* The first run of a round has come to its last frame: back to the
 * save. */
static void port_RollbackLoad(void) {
    u32 start = port_HostTicks();

    port_StateLoad(g_port_rollback_snapshot);
    port_TimingAdd(&g_port_rollback_loads, port_HostTicks() - start);
    g_port_rollback_length = g_port_rollback_count;
    g_port_rollback_phase = ROLLBACK_AGAIN;
    g_port_rollback_count = 0;
    g_port_rerun = g_port_rollback_quiet;
}

/* The next save after a round that took `length` frames. */
static void port_RollbackWait(u32 length) {
    g_port_rerun = 0;
    g_port_rollback_phase = ROLLBACK_WAIT;
    g_port_rollback_count =
        g_port_rollback_every > length ? g_port_rollback_every - length : 0;
    if (g_port_rollback_count == 0) {
        port_RollbackSave();
    }
}

static void port_RollbackTurn(void);

/* A frame's boundary (port_state.h): how long the step took, and the
 * round's next turn. */
void port_OnRollbackFrame(void) {
    u32 now = port_HostTicks();

    if (g_port_rollback_phase == ROLLBACK_OFF) {
        return;
    }
    if (g_port_rollback_step_start != 0) {
        port_TimingAdd(&g_port_rollback_steps[g_port_rollback_phase],
                       now - g_port_rollback_step_start);
        if (g_sys_screen == SCREEN_FIGHT_UPDATE) {
            port_TimingAdd(&g_port_rollback_fight_steps[g_port_rollback_phase],
                           now - g_port_rollback_step_start);
        }
    }
    port_RollbackTurn();
    g_port_rollback_step_start = port_HostTicks();
}

/* The round's next turn, after a step. */
static void port_RollbackTurn(void) {
    u64 *first;
    u64 text;
    u64 hash;
    u32 start;

    if (g_port_rollback_phase == ROLLBACK_WAIT) {
        if (g_port_rollback_count > 0) {
            g_port_rollback_count--;
        }
        if (g_port_rollback_count == 0) {
            port_RollbackSave();
        }
        return;
    }
    g_port_rollback_count++;
    first = g_port_rollback_pages +
            (g_port_rollback_count - 1) * g_port_rollback_page_count;
    text = port_RollbackTakeText();
    start = port_HostTicks();
    hash = port_StateHash();
    port_TimingAdd(&g_port_rollback_hashing, port_HostTicks() - start);
    if (g_port_rollback_phase == ROLLBACK_FIRST) {
        if (g_port_rollback_count == 1) {
            port_StateSave(g_port_rollback_first_frame);
        }
        port_HashPages(first);
        g_port_rollback_texts[g_port_rollback_count - 1] = text;
        g_port_rollback_hashes[g_port_rollback_count - 1] = hash;
        if (g_port_rollback_count >= g_port_rollback_frames) {
            port_RollbackLoad();
        }
        return;
    }
    port_HashPages(g_port_rollback_here);
    g_port_rollback_compared++;
    {
        u32 page;

        for (page = 0; page < g_port_rollback_page_count; page++) {
            if (first[page] != g_port_rollback_here[page] &&
                !port_RollbackIsUnpaintedPage(page)) {
                port_RollbackFail(first, text, hash);
            }
        }
    }
    if (text != g_port_rollback_texts[g_port_rollback_count - 1] ||
        hash != g_port_rollback_hashes[g_port_rollback_count - 1]) {
        port_RollbackFail(first, text, hash);
    }
    if (g_port_rollback_count == g_port_rollback_length) {
        g_port_rollback_rounds++;
        port_RollbackWait(g_port_rollback_length);
    }
}

/* A kind of time, in microseconds: the mean and the longest. */
static void port_PrintTiming(const char *what, const PortTiming *timing) {
    u32 mean = timing->count == 0 ? 0 : timing->total / timing->count;

    port_Log("rollback: %s %u us on average, %u at most, %u at least (%u "
             "times)\n",
             what, mean / PORT_HOST_TICKS_PER_MICROSECOND,
             timing->most / PORT_HOST_TICKS_PER_MICROSECOND,
             timing->least / PORT_HOST_TICKS_PER_MICROSECOND, timing->count);
}

/* How big the state is, how long it took to save, load and hash, and
 * how many rounds the run made (port_state.h). */
void port_PrintRollbackReport(void) {
    if (g_port_rollback_phase == ROLLBACK_OFF) {
        return;
    }
    port_Log("rollback: the state is %u bytes in %d pieces; the hash reads "
             "%d pieces\n",
             g_port_state_bytes, g_port_state_saved_count,
             g_port_state_hashed_count);
    port_PrintTiming("save", &g_port_rollback_saves);
    port_PrintTiming("load", &g_port_rollback_loads);
    port_PrintTiming("port_StateHash", &g_port_rollback_hashing);
    port_PrintTiming(
        "a step between rounds", &g_port_rollback_steps[ROLLBACK_WAIT]);
    port_PrintTiming(
        "a step run first", &g_port_rollback_steps[ROLLBACK_FIRST]);
    port_PrintTiming(g_port_rollback_quiet ? "a step run again, quietly"
                                           : "a step run again",
                     &g_port_rollback_steps[ROLLBACK_AGAIN]);
    port_PrintTiming("a step of a fight run first",
                     &g_port_rollback_fight_steps[ROLLBACK_FIRST]);
    port_PrintTiming(
        g_port_rollback_quiet ? "a step of a fight run again, quietly"
                              : "a step of a fight run again",
        &g_port_rollback_fight_steps[ROLLBACK_AGAIN]);
    port_Log("rollback: %s: %u rounds of %u frames run twice, %u frames "
             "compared\n",
             g_port_rollback_failed ? "FAILED" : "ok", g_port_rollback_rounds,
             g_port_rollback_frames, g_port_rollback_compared);
}
