/* The native port's replays (port_replay.h; docs/PORT.md, "Replays"):
 * each fight's start state and every input after it, written to a file
 * as the fight ends, and played back by running the game again from that
 * state with those inputs.
 *
 * A file is a head and chunks, each a tag of four letters, its size and
 * its bytes, all numbers little-endian:
 *
 *   RBREPLAY, the format's version
 *   META  what the replay says of itself, a line `key value` each
 *   PIEC  the snapshot's pieces (port_state.h): a name and a size each
 *   STRT  the state at the fight's first boundary: its size, then a
 *         zlib stream of it (deflate.c), this run's own cleared first
 *   VIEW  what the game read of the picture, each kind of it once
 *   INPT  every input from there: runs of frames alike, each its fields
 *         that changed and how many frames it held for
 *   HASH  the state's hash every PORT_REPLAY_HASH_EVERY frames
 *   END   nothing
 *
 * The file is this file's: nothing of it is the game's state, and a load
 * leaves it (config/port_state.txt). */
#include "common.h"
#include "port_card.h"
#include "port_deflate.h"
#include "port_disc.h"
#include "port_host.h"
#include "port_net.h"
#include "port_png.h"
#include "port_replay.h"
#include "port_spu.h"
#include "bb2.h"

#define FORMAT_VERSION 1
#define MAGIC_BYTES 8
#define HEAD_BYTES (MAGIC_BYTES + 4)
#define CHUNK_HEAD_BYTES 8
#define WORD_BYTES 4
#define LONG_BYTES 8
#define HALF_BYTES 2
#define BITS_PER_BYTE 8
#define BYTE_MASK 0xFF
#define WORD_BITS 32
#define VARINT_MORE 0x80
#define VARINT_BITS 7
#define VARINT_MASK 0x7F
/* the longest fight kept: 2^18 frames, over two hours at 30 a second */
#define FRAMES_MAX 0x40000
#define HASHES_MAX (FRAMES_MAX / PORT_REPLAY_HASH_EVERY + 2)
#define VIEWS_MAX 16
/* which of the last boundaries were in a fight, by their input's number */
#define FIGHT_RING 64
#define FIGHT_RING_MASK (FIGHT_RING - 1)
#define PIECES_MAX 16
#define PIECE_NAME_BYTES 16
#define META_BYTES 4096
#define INFO_READ_BYTES 8192
#define LIST_MAX 1024
#define NONE 0xFFFFFFFFU
/* the most of a replay's file that is read (its start stored whole, in
 * the worst case, and two hours of inputs fit), and as much of the work
 * after a fight as there is (a slice of all of it) */
#define FILE_MAX 0x1000000U
/* the speeds, in eighths; a fight's frame is two blanks */
#define SPEED_ONE 8
#define SPEED_MIN 1
#define SPEED_MAX 64
#define BLANKS_A_FRAME 2
/* skip to the kill: this many frames before it (three seconds) */
#define KILL_LEAD 90
/* an input's fields that changed (INPT) */
#define CHANGED_FIRST 1
#define CHANGED_SECOND 2
#define CHANGED_PRESENT 4
#define CHANGED_VIEW 8
/* what the disc holds already (DISC): the game's last sectors read, each
 * 64 bytes of them indexed by a hash, and a run of the disc found in the
 * snapshot that is at least MATCH_LEAST bytes long */
#define SECTOR_LOG 4096
#define SECTOR_LOG_MASK (SECTOR_LOG - 1)
#define BLOCK_BYTES 64
#define BLOCK_WORDS (BLOCK_BYTES / WORD_BYTES)
#define BLOCKS_PER_SECTOR (PORT_DISC_DATA_BYTES / BLOCK_BYTES)
#define INDEX_BITS 18
#define INDEX_SIZE (1U << INDEX_BITS)
#define INDEX_MASK (INDEX_SIZE - 1)
#define INDEX_PROBES 8
/* the table of the indexed sectors by their numbers: twice as many places */
#define SECTOR_SLOT_BITS 13
#define SECTOR_SLOT_MASK ((1U << SECTOR_SLOT_BITS) - 1)
#define INDEX_MIX 2654435761U
#define ROLL 0x01000193U
#define MATCH_LEAST 256
#define MATCHES_MAX 8192
#define MATCH_BYTES (4 * WORD_BYTES)
/* a slice of the work after a fight: how many times as much of the
 * snapshot is looked through as is compressed */
#define SCAN_PER_SLICE 8
#define FNV_OFFSET 0xCBF29CE484222325ULL
#define FNV_PRIME 0x100000001B3ULL
#define HEX_DIGITS 16
#define DECIMAL 10
#define HEX_LETTER 10
#define KILOBYTE 1024
/* a piece of the work after a fight, of the bytes it compresses (it
 * looks through SCAN_PER_SLICE times as many): a millisecond or so */
#define WORK_PIECE 0x8000
#define TICKS_PER_MS (PORT_HOST_TICKS_PER_MICROSECOND * 1000)

#define TAG(a, b, c, d)                                                        \
    ((u32)(a) | ((u32)(b) << BITS_PER_BYTE) |                                  \
     ((u32)(c) << (2 * BITS_PER_BYTE)) | ((u32)(d) << (3 * BITS_PER_BYTE)))
#define TAG_META TAG('M', 'E', 'T', 'A')
#define TAG_PIECES TAG('P', 'I', 'E', 'C')
#define TAG_START TAG('S', 'T', 'R', 'T')
#define TAG_VIEW TAG('V', 'I', 'E', 'W')
#define TAG_INPUT TAG('I', 'N', 'P', 'T')
#define TAG_HASH TAG('H', 'A', 'S', 'H')
#define TAG_END TAG('E', 'N', 'D', ' ')
#define TAG_DISC TAG('D', 'I', 'S', 'C')

static const char g_magic[MAGIC_BYTES] = {
    'R', 'B', 'R', 'E', 'P', 'L', 'A', 'Y'};

/* The names a file is given, by enum FightMode. */
static const char * const g_mode_words[] = {
    "story", "vscpu", "practice", "chanbara", "vs", "team", "network"};
#define MODE_WORDS ((s32)(sizeof(g_mode_words) / sizeof(g_mode_words[0])))

/* One frame of a fight: the two pads' buttons, which pads answered (a
 * bit each), and the view it read (VIEW's number). */
typedef struct {
    u16 buttons[PORT_PADS];
    u8 present;
    u8 view;
} ReplayFrame;

typedef struct {
    char name[PIECE_NAME_BYTES];
    u32 bytes;
} ReplayPiece;

enum { REC_IDLE, REC_FIGHT, REC_ENDED, REC_WRITING };

/* what the writing of a fight's file is doing */
enum { WORK_INDEX, WORK_SCAN, WORK_DEFLATE };

/* A run of the snapshot that the disc holds: where it is in the
 * snapshot, and from which sector and byte of it on, how long. */
typedef struct {
    u32 at;
    u32 sector;
    u32 offset;
    u32 length;
} ReplayMatch;

enum { PLAY_OFF, PLAY_PENDING, PLAY_ON };

/* ---- the settings ---- */

static char g_folder[PORT_REPLAY_PATH_BYTES];
static u32 g_keep_count = 100;
static u32 g_keep_megabytes;
static s32 g_recording_on = TRUE;
static u32 g_slice;
/* the folder's replays' kilobytes, worked out the first time */
static u32 g_folder_kilobytes;
static s32 g_folder_known;

/* ---- the fight being recorded ---- */

static struct {
    s32 state;
    u32 start_count; /* the input's number at the fight's first boundary */
    u32 end_count;   /* ... and at its first boundary out of it */
    u32 length;      /* the frames whose input was taken */
    u32 kill;        /* the first frame with a fighter cut down, or NONE */
    u32 hash_count;  /* the hashes noted, of the boundaries reached */
    s32 winner;
    u8 *snapshot;
    ReplayFrame *frames;
    u64 *hashes;
    PortGameView views[VIEWS_MAX];
    s32 view_count;
    u64 start_hash;
    PortDate date;
    s32 mode;
    s32 stage;
    s32 roster[PORT_PADS];
    s32 weapon[PORT_PADS];
    s32 is_cpu[PORT_PADS];
    s32 stage_kind;
    s32 arena;
    s32 time;
    s32 music;
    s32 online;
    s32 bugfixes;
    s32 unfinished; /* the program ended in the middle of it */
    /* the start, compressed a slice at a boundary */
    PortDeflate deflate;
    u8 *packed;
    u32 packed_room;
} g_rec;

/* The disc, and the sectors the game read last (cd.c). */
static const PortDisc *g_disc;
static u32 g_sector_log[SECTOR_LOG];
static u32 g_sector_log_count;
/* ROLL to the power 15: what the word leaving the hash weighs */
static u32 g_roll_top;

/* The writing of a fight's file, a slice at a time: the sectors indexed,
 * the snapshot looked through, the runs found. */
static struct {
    s32 work;
    u8 *data; /* each indexed sector's 2048 bytes */
    u32 *sectors;
    u32 sector_count;
    u32 indexed;
    u32 *index;   /* a block's number among them all, plus one; 0: none */
    u32 *slot_of; /* by a hash of a sector, its number among them, plus one */
    u32 scanned;
    u32 cleared_to;
    ReplayMatch *matches;
    u32 match_count;
    u32 saved;
    /* what the slices took, in the host's ticks */
    u32 slices;
    u32 ticks;
    u32 ticks_most;
} g_dedupe;

/* The state at the last boundary out of a fight, with its hash, its
 * input's number and its view; and the last input the game took. */
static struct {
    u8 *snapshot;
    u64 hash;
    u32 count;
    PortGameView view;
    s32 is_kept;
} g_before;

static struct {
    u32 index;
    ReplayFrame frame;
} g_last = {NONE, {{0, 0}, 0, 0}};

/* Whether each of the last boundaries was in a fight, by input number. */
static u32 g_ring_count[FIGHT_RING];
static u8 g_ring_fight[FIGHT_RING];

/* ---- the replay being played ---- */

static struct {
    s32 state;
    s32 quiet;
    s32 then_quit;
    s32 paused;
    s32 step_once;
    s32 stop_asked;
    u32 seek_to; /* a frame to go to, or NONE */
    s32 speed;
    u32 start_count;
    u32 frames;
    u32 frame;
    u32 kill;
    u64 start_hash;
    s32 desync;
    u32 hashes_matched;
    char path[PORT_REPLAY_PATH_BYTES];
    u8 *file;
    u8 *start;
    u8 *back; /* the game as it was before the replay */
    ReplayFrame *list;
    u64 *hashes;
    u32 hash_count;
    PortGameView views[VIEWS_MAX];
    s32 view_count;
} g_play;

void (*g_port_replay_wait_hook)(void);
void (*g_port_replay_end_hook)(const char *trouble);

/* What a load of another run's state must leave as this run has it: the
 * hooks the host set (a pointer of this run's, or one that is not this
 * run's way of doing things), the `run` files' variables. */
static void (**g_kept_hooks[])(void) = {
    (void (**)(void)) & g_port_take_hook,
    (void (**)(void)) & g_port_device_hook,
    (void (**)(void)) & g_port_input_hook,
    (void (**)(void)) & g_port_blank_hook,
    (void (**)(void)) & g_port_sound_hook,
    (void (**)(void)) & g_port_rerun_sound_hook,
    (void (**)(void)) & g_port_spu_key_on_hook,
    (void (**)(void)) & g_port_disc_hook,
    (void (**)(void)) & g_port_disc_override,
    (void (**)(void)) & g_port_press_hook,
};
#define KEPT_HOOKS ((s32)(sizeof(g_kept_hooks) / sizeof(g_kept_hooks[0])))
static void (*g_kept_values[KEPT_HOOKS])(void);
static s16 *g_kept_unmixed;
static u8 *g_kept_run;

/* ---- bytes ---- */

static void port_PutWord(u8 *out, u32 word) {
    s32 i;

    for (i = 0; i < WORD_BYTES; i++) {
        out[i] = (u8)(word >> (i * BITS_PER_BYTE));
    }
}

static u32 port_GetWord(const u8 *in) {
    u32 word = 0;
    s32 i;

    for (i = 0; i < WORD_BYTES; i++) {
        word |= (u32)in[i] << (i * BITS_PER_BYTE);
    }
    return word;
}

static void port_CopyBytes(void *to, const void *from, u32 count) {
    u8 *out = to;
    const u8 *in = from;
    u32 i;

    for (i = 0; i < count; i++) {
        out[i] = in[i];
    }
}

static void port_ClearBytes(void *to, u32 count) {
    u8 *out = to;
    u32 i;

    for (i = 0; i < count; i++) {
        out[i] = 0;
    }
}

static u32 port_ReplayText(char *out, u32 room, const char *format, ...) {
    PortArgs args;
    u32 length;

    PORT_ARGS_START(args, format);
    length = port_FormatText(out, room, format, args);
    PORT_ARGS_END(args);
    return length;
}

static u64 port_SumBytes(u64 sum, const void *bytes, u32 count) {
    const u8 *at = bytes;
    u32 i;

    for (i = 0; i < count; i++) {
        sum = (sum ^ at[i]) * FNV_PRIME;
    }
    return sum;
}

static u64 port_SumWord(u64 sum, u32 word) {
    u8 bytes[WORD_BYTES];

    port_PutWord(bytes, word);
    return port_SumBytes(sum, bytes, sizeof(bytes));
}

/* ---- the sums a replay is held to ---- */

/* The state's size but for the port's own primitive buffers, which a run
 * has or not by its draw distance. */
static u32 port_ReplayStateSize(s32 *has_prims) {
    PortStatePiece piece;
    u32 size = port_StateSize();
    s32 i;

    *has_prims = FALSE;
    for (i = 0; i < port_StatePieceCount(); i++) {
        port_StateGetPiece(i, &piece);
        if (port_TextsEqual(piece.name, "prims")) {
            size -= piece.bytes;
            *has_prims = TRUE;
        }
    }
    return size;
}

/* The build, as netplay's (session.c), but for the primitive buffers. */
static u64 port_ReplayBuild(void) {
    s32 has_prims;
    u64 sum = FNV_OFFSET;

    sum = port_SumBytes(sum, g_rb_version, port_TextLength(g_rb_version));
    sum = port_SumWord(sum, (u32)sizeof(void *));
#if defined(_WIN32)
    sum = port_SumWord(sum, 1);
#else
    sum = port_SumWord(sum, 2);
#endif
    sum = port_SumWord(sum, port_ReplayStateSize(&has_prims));
    sum = port_SumWord(sum, (u32)(g_port_data_end - g_port_data_start));
    sum = port_SumWord(sum, (u32)(PortAddress)g_port_data_start);
    return port_SumWord(sum, (u32)(PortAddress)port_Main);
}

static u64 port_ReplayMods(void) {
    return port_WeaponsSessionHash() + port_ArenaSessionHash();
}

/* ---- setting it up ---- */

void port_SetReplayFolder(const char *folder) {
    u32 i = 0;

    g_folder_known = FALSE;
    if (folder != NULL) {
        for (; folder[i] != 0 && i + 1 < sizeof(g_folder); i++) {
            g_folder[i] = folder[i];
        }
    }
    g_folder[i] = 0;
}

const char *port_GetReplayFolder(void) { return g_folder; }

void port_SetReplayKeep(u32 count, u32 megabytes) {
    g_keep_count = count;
    g_keep_megabytes = megabytes;
}

void port_SetReplaySlice(u32 microseconds) { g_slice = microseconds; }

void port_SetReplayRecording(s32 on) {
    g_recording_on = on;
    if (!on && (g_rec.state == REC_FIGHT || g_rec.state == REC_ENDED)) {
        g_rec.state = REC_IDLE;
    }
}

void port_ReplayAbandon(const char *why) {
    if (g_rec.state == REC_FIGHT || g_rec.state == REC_ENDED) {
        port_Log("replay: the fight is not kept: %s\n", why);
        g_rec.state = REC_IDLE;
    }
}

/* ---- pruning ---- */

static char g_list_names[LIST_MAX][PORT_REPLAY_NAME_BYTES];
static s32 g_list_count;

/* A name of the folder's, kept where it is a replay's. */
static void port_ReplayListOne(void *context, const char *name, s32 is_folder) {
    u32 i;

    (void)context;
    if (is_folder || !port_TextEndsWith(name, ".rbr") ||
        g_list_count == LIST_MAX ||
        port_TextLength(name) >= PORT_REPLAY_NAME_BYTES) {
        return;
    }
    for (i = 0; name[i] != 0; i++) {
        g_list_names[g_list_count][i] = name[i];
    }
    g_list_names[g_list_count][i] = 0;
    g_list_count++;
}

/* Whether `first` comes before `second` (bytes compared: the names begin
 * with their dates). */
static s32 port_NameBefore(const char *first, const char *second) {
    u32 i;

    for (i = 0; first[i] != 0 && first[i] == second[i]; i++) {
    }
    return (u8)first[i] < (u8)second[i];
}

/* The folder's replays' names, oldest first. */
static void port_ReplayListNames(void) {
    s32 i;

    g_list_count = 0;
    if (g_folder[0] == 0) {
        return;
    }
    port_HostListFolder(g_folder, port_ReplayListOne, NULL);
    for (i = 1; i < g_list_count; i++) {
        char held[PORT_REPLAY_NAME_BYTES];
        s32 at = i;

        port_CopyBytes(held, g_list_names[i], PORT_REPLAY_NAME_BYTES);
        while (at > 0 && port_NameBefore(held, g_list_names[at - 1])) {
            port_CopyBytes(
                g_list_names[at], g_list_names[at - 1], PORT_REPLAY_NAME_BYTES);
            at--;
        }
        port_CopyBytes(g_list_names[at], held, PORT_REPLAY_NAME_BYTES);
    }
}

static void port_ReplayPathOf(const char *name, char *path, u32 room) {
    port_ReplayText(path, room, "%s/%s", g_folder, name);
}

static u32 port_ReplayFileSize(const char *path) {
    s32 file = port_HostOpen(path);
    u32 size;

    if (file < 0) {
        return 0;
    }
    size = port_HostFileSize(file);
    port_HostClose(file);
    return size;
}

/* The oldest taken away while there are more than the setting keeps. */
static void port_ReplayPrune(u32 written) {
    static char path[PORT_REPLAY_PATH_BYTES];
    s32 first = 0;
    s32 i;

    port_ReplayListNames();
    if (!g_folder_known) {
        /* every file's size once; after that the total is kept up */
        g_folder_kilobytes = 0;
        for (i = 0; i < g_list_count; i++) {
            port_ReplayPathOf(g_list_names[i], path, sizeof(path));
            g_folder_kilobytes += port_ReplayFileSize(path) / KILOBYTE;
        }
        g_folder_known = TRUE;
    } else {
        g_folder_kilobytes += written / KILOBYTE;
    }
    while (first < g_list_count - 1 &&
           ((g_keep_count != 0 && (u32)(g_list_count - first) > g_keep_count) ||
            (g_keep_megabytes != 0 &&
             g_folder_kilobytes > g_keep_megabytes * KILOBYTE))) {
        u32 size;

        port_ReplayPathOf(g_list_names[first], path, sizeof(path));
        size = port_ReplayFileSize(path) / KILOBYTE;
        if (port_HostRemove(path)) {
            g_folder_kilobytes -=
                size < g_folder_kilobytes ? size : g_folder_kilobytes;
            port_Log("replay: %s taken away (replays_keep)\n", path);
        }
        first++;
    }
}

/* ---- recording ---- */

static s32 port_ReplayRoom(void) {
    u32 size = port_StateSize();

    if (g_rec.snapshot != NULL) {
        return TRUE;
    }
    g_rec.snapshot = port_HostAllocate(size);
    g_before.snapshot = port_HostAllocate(size);
    g_rec.frames = port_HostAllocate(FRAMES_MAX * sizeof(ReplayFrame));
    g_rec.hashes = port_HostAllocate(HASHES_MAX * sizeof(u64));
    g_rec.packed_room = port_DeflateBound(size);
    g_rec.packed = port_HostAllocate(g_rec.packed_room);
    g_dedupe.data = port_HostAllocate(SECTOR_LOG * PORT_DISC_DATA_BYTES);
    g_dedupe.sectors = port_HostAllocate(SECTOR_LOG * sizeof(u32));
    g_dedupe.index = port_HostAllocate(INDEX_SIZE * sizeof(u32));
    g_dedupe.slot_of = port_HostAllocate((SECTOR_SLOT_MASK + 1) * sizeof(u32));
    g_dedupe.matches = port_HostAllocate(MATCHES_MAX * sizeof(ReplayMatch));
    if (g_rec.snapshot == NULL || g_before.snapshot == NULL ||
        g_rec.frames == NULL || g_rec.hashes == NULL || g_rec.packed == NULL ||
        g_dedupe.data == NULL || g_dedupe.sectors == NULL ||
        g_dedupe.index == NULL || g_dedupe.matches == NULL ||
        g_dedupe.slot_of == NULL) {
        port_Log("replay: no memory to record fights in; none is kept\n");
        g_recording_on = FALSE;
        g_rec.snapshot = NULL;
        return FALSE;
    }
    return TRUE;
}

/* The state just saved into the recording's snapshot, made ready to be
 * written: this run's own cleared, the drive's played samples cleared,
 * and the snapshot forgotten by the state's pages (its bytes are no
 * longer what was saved). */
static void port_ReplayClearOwn(u8 *snapshot) {
    u32 data = port_StateOffsetOf(g_port_run_data);
    u32 bss = port_StateOffsetOf(g_port_run_bss);

    if (data != PORT_STATE_NOWHERE) {
        port_ClearBytes(
            snapshot + data, (u32)(g_port_state_data_end - g_port_run_data));
    }
    if (bss != PORT_STATE_NOWHERE) {
        port_ClearBytes(
            snapshot + bss, (u32)(g_port_state_bss_end - g_port_run_bss));
    }
    port_XaClearPlayed(snapshot);
    port_StateForget(snapshot);
}

static s32 port_ReplayMayRecord(void) {
    return g_recording_on && g_folder[0] != 0 && !port_LinkIsUp() &&
           g_play.state == PLAY_OFF;
}

/* ---- what the disc holds already ----
 *
 * Most of a fight's start is what the game read from the disc as it
 * loaded the fight: the stage's and the fighters' files in the console's
 * RAM, the sounds in the sound chip's memory. Those runs of bytes are not
 * written, but where on the disc they are: the sectors the game read
 * last are indexed by a hash of each 64 bytes, the snapshot is looked
 * through four bytes at a time for them, and every run found that is
 * long enough is cleared in it (and so compressed to nothing) and noted
 * in DISC. Playing back reads them from the player's own disc, which the
 * disc's sum holds to be the same. */

void port_SetReplayDisc(const PortDisc *disc) { g_disc = disc; }

void port_ReplayNoteSector(u32 sector) {
    g_sector_log[g_sector_log_count++ & SECTOR_LOG_MASK] = sector;
}

/* A data sector's 2048 bytes (mode 2, form 1): FALSE for any other. What
 * is read here is none of the game's, so the count of what it read is
 * left as it was. */
static s32 port_ReplayReadSector(u32 sector, u8 *out) {
    static u8 raw[PORT_DISC_SECTOR_BYTES];
    s32 (*override)(u32 sector, u8 *raw) = g_port_disc_override;
    u32 reads = g_port_disc_sectors_read;
    s32 ok;

    /* the disc as it is: what a mod puts over a sector follows the game's
     * state (the arena in play), and is written with the start */
    g_port_disc_override = NULL;
    ok = g_disc != NULL &&
         port_ReadDiscSector(g_disc, sector, raw) == PORT_DISC_OK &&
         (raw[PORT_DISC_SYNC_BYTES + PORT_DISC_HEADER_BYTES +
              PORT_DISC_SUBMODE] &
          PORT_DISC_SUBMODE_FORM2) == 0;
    g_port_disc_override = override;
    g_port_disc_sectors_read = reads;
    if (ok) {
        port_CopyBytes(out, raw + PORT_DISC_DATA_OFFSET, PORT_DISC_DATA_BYTES);
    }
    return ok;
}

/* The hash of 64 bytes, sixteen words, which rolls a word on:
 * sum of word i times ROLL to the power 15 - i. */
static u32 port_BlockHash(const u8 *bytes) {
    u32 hash = 0;
    s32 i;

    for (i = 0; i < BLOCK_WORDS; i++) {
        hash = hash * ROLL + port_GetWord(bytes + i * WORD_BYTES);
    }
    return hash;
}

static u32 port_IndexSlot(u32 hash) {
    return (hash * INDEX_MIX) >> (WORD_BITS - INDEX_BITS);
}

/* Whether 64 bytes are all one word again and again (cleared memory, a
 * fill), which are not worth looking for. */
static s32 port_BlockIsFlat(const u8 *bytes) {
    u32 first = port_GetWord(bytes);
    s32 i;

    for (i = 1; i < BLOCK_WORDS; i++) {
        if (port_GetWord(bytes + i * WORD_BYTES) != first) {
            return FALSE;
        }
    }
    return TRUE;
}

/* The bytes of an indexed sector, or NULL for one that is not. */
static const u8 *port_ReplayIndexedSector(u32 sector) {
    u32 at = sector * INDEX_MIX >> (WORD_BITS - SECTOR_SLOT_BITS);

    for (;; at = (at + 1) & SECTOR_SLOT_MASK) {
        u32 slot = g_dedupe.slot_of[at];

        if (slot == 0) {
            return NULL;
        }
        if (g_dedupe.sectors[slot - 1] == sector) {
            return g_dedupe.data + (slot - 1) * PORT_DISC_DATA_BYTES;
        }
    }
}

/* The next of the logged sectors into the index: FALSE once all are. */
static s32 port_ReplayIndexOne(void) {
    u32 logged =
        g_sector_log_count < SECTOR_LOG ? g_sector_log_count : SECTOR_LOG;
    u32 sector;
    u8 *data;
    u32 block;
    u32 i;

    if (g_dedupe.indexed >= logged) {
        return FALSE;
    }
    sector = g_sector_log[(g_sector_log_count - logged + g_dedupe.indexed) &
                          SECTOR_LOG_MASK];
    g_dedupe.indexed++;
    /* a sector read again is indexed once */
    if (port_ReplayIndexedSector(sector) != NULL) {
        return TRUE;
    }
    data = g_dedupe.data + g_dedupe.sector_count * PORT_DISC_DATA_BYTES;
    if (!port_ReplayReadSector(sector, data)) {
        return TRUE;
    }
    for (block = 0; block < BLOCKS_PER_SECTOR; block++) {
        const u8 *bytes = data + block * BLOCK_BYTES;
        u32 slot;
        s32 probes;

        if (port_BlockIsFlat(bytes)) {
            continue;
        }
        slot = port_IndexSlot(port_BlockHash(bytes));
        for (probes = 0; probes < INDEX_PROBES; probes++) {
            u32 at = (slot + (u32)probes) & INDEX_MASK;

            if (g_dedupe.index[at] == 0) {
                g_dedupe.index[at] =
                    (g_dedupe.sector_count * BLOCKS_PER_SECTOR + block) + 1;
                break;
            }
        }
    }
    for (i = sector * INDEX_MIX >> (WORD_BITS - SECTOR_SLOT_BITS);;
         i = (i + 1) & SECTOR_SLOT_MASK) {
        if (g_dedupe.slot_of[i] == 0) {
            g_dedupe.slot_of[i] = g_dedupe.sector_count + 1;
            break;
        }
    }
    g_dedupe.sectors[g_dedupe.sector_count++] = sector;
    return TRUE;
}

/* How far the snapshot at `at` goes on as the disc does from `sector` and
 * `offset` on: a run that goes past the sector's end goes on into the
 * sectors after it. */
static u32 port_ReplayRunLength(const u8 *snapshot, u32 at, u32 size,
                                const u8 *first, u32 sector, u32 offset) {
    static u8 next[PORT_DISC_DATA_BYTES];
    const u8 *data = first;
    u32 length = 0;

    while (at + length < size) {
        if (offset == PORT_DISC_DATA_BYTES) {
            sector++;
            offset = 0;
            /* the next sector from the index where it is there */
            data = port_ReplayIndexedSector(sector);
            if (data == NULL) {
                if (!port_ReplayReadSector(sector, next)) {
                    break;
                }
                data = next;
            }
        }
        if (snapshot[at + length] != data[offset]) {
            break;
        }
        length++;
        offset++;
    }
    return length;
}

/* The snapshot looked through from where the last slice left it to
 * `stop`: each run of the disc found long enough noted and cleared. */
static void port_ReplayScan(u8 *snapshot, u32 size, u32 stop) {
    u32 at = g_dedupe.scanned;
    u32 hash = 0;
    s32 is_hashed = FALSE;

    while (at + BLOCK_BYTES <= size && at < stop &&
           g_dedupe.match_count < MATCHES_MAX) {
        u32 slot;
        s32 probes;
        s32 found = FALSE;

        if (!is_hashed) {
            hash = port_BlockHash(snapshot + at);
            is_hashed = TRUE;
        }
        slot = port_IndexSlot(hash);
        for (probes = 0; probes < INDEX_PROBES && !found; probes++) {
            u32 entry = g_dedupe.index[(slot + (u32)probes) & INDEX_MASK];
            u32 number;
            u32 offset;
            u32 back = 0;
            u32 length;
            const u8 *data;

            if (entry == 0) {
                break;
            }
            number = (entry - 1) / BLOCKS_PER_SECTOR;
            offset = (entry - 1) % BLOCKS_PER_SECTOR * BLOCK_BYTES;
            data = g_dedupe.data + number * PORT_DISC_DATA_BYTES;
            if (port_GetWord(data + offset) != port_GetWord(snapshot + at)) {
                continue;
            }
            /* back to where the sector's bytes begin to agree */
            while (back < offset && at - back > g_dedupe.cleared_to &&
                   snapshot[at - back - 1] == data[offset - back - 1]) {
                back++;
            }
            length = port_ReplayRunLength(
                snapshot, at - back, size, data, g_dedupe.sectors[number],
                offset - back);
            if (length < MATCH_LEAST) {
                continue;
            }
            {
                ReplayMatch *match = &g_dedupe.matches[g_dedupe.match_count++];

                match->at = at - back;
                match->sector = g_dedupe.sectors[number];
                match->offset = offset - back;
                match->length = length;
                port_ClearBytes(snapshot + match->at, length);
                g_dedupe.saved += length;
                at = (match->at + length + WORD_BYTES - 1) & ~(WORD_BYTES - 1);
                g_dedupe.cleared_to = match->at + length;
                found = TRUE;
                is_hashed = FALSE;
            }
        }
        if (found) {
            continue;
        }
        if (at + BLOCK_BYTES + WORD_BYTES <= size) {
            hash = (hash - port_GetWord(snapshot + at) * g_roll_top) * ROLL +
                   port_GetWord(snapshot + at + BLOCK_BYTES);
        } else {
            is_hashed = FALSE;
        }
        at += WORD_BYTES;
    }
    g_dedupe.scanned =
        at + BLOCK_BYTES > size || g_dedupe.match_count == MATCHES_MAX
            ? size
            : at;
}

/* The runs of DISC put back into a start as it was inflated (`raw`,
 * `size` bytes): FALSE where one cannot be read. */
static s32 port_ReplayRefill(u8 *raw, u32 size, const u8 *list, u32 count) {
    static u8 data[PORT_DISC_DATA_BYTES];
    u32 i;

    for (i = 0; i < count; i++) {
        const u8 *at = list + i * MATCH_BYTES;
        u32 to = port_GetWord(at);
        u32 sector = port_GetWord(at + WORD_BYTES);
        u32 offset = port_GetWord(at + 2 * WORD_BYTES);
        u32 length = port_GetWord(at + 3 * WORD_BYTES);

        if (to > size || length > size - to || offset >= PORT_DISC_DATA_BYTES) {
            return FALSE;
        }
        while (length > 0) {
            u32 take = PORT_DISC_DATA_BYTES - offset;

            if (take > length) {
                take = length;
            }
            if (!port_ReplayReadSector(sector, data)) {
                return FALSE;
            }
            port_CopyBytes(raw + to, data + offset, take);
            to += take;
            length -= take;
            sector++;
            offset = 0;
        }
    }
    return TRUE;
}

/* ---- writing ---- */

static u32 port_PutVarint(u8 *out, u32 value) {
    u32 used = 0;

    while (value >= VARINT_MORE) {
        out[used++] = (u8)((value & VARINT_MASK) | VARINT_MORE);
        value >>= VARINT_BITS;
    }
    out[used++] = (u8)value;
    return used;
}

/* The frames as INPT has them, into `out`: its length. */
static u32 port_ReplayPackInputs(u8 *out) {
    ReplayFrame last = {{0, 0}, 0, 0};
    u32 at = 0;
    u32 frame = 0;

    while (frame < g_rec.length) {
        const ReplayFrame *now = &g_rec.frames[frame];
        u32 run = 1;
        u8 changed = 0;

        while (frame + run < g_rec.length &&
               g_rec.frames[frame + run].buttons[0] == now->buttons[0] &&
               g_rec.frames[frame + run].buttons[1] == now->buttons[1] &&
               g_rec.frames[frame + run].present == now->present &&
               g_rec.frames[frame + run].view == now->view) {
            run++;
        }
        changed |= now->buttons[0] != last.buttons[0] ? CHANGED_FIRST : 0;
        changed |= now->buttons[1] != last.buttons[1] ? CHANGED_SECOND : 0;
        changed |= now->present != last.present ? CHANGED_PRESENT : 0;
        changed |= now->view != last.view ? CHANGED_VIEW : 0;
        out[at++] = changed;
        if (changed & CHANGED_FIRST) {
            out[at++] = (u8)now->buttons[0];
            out[at++] = (u8)(now->buttons[0] >> BITS_PER_BYTE);
        }
        if (changed & CHANGED_SECOND) {
            out[at++] = (u8)now->buttons[1];
            out[at++] = (u8)(now->buttons[1] >> BITS_PER_BYTE);
        }
        if (changed & CHANGED_PRESENT) {
            out[at++] = now->present;
        }
        if (changed & CHANGED_VIEW) {
            out[at++] = now->view;
        }
        at += port_PutVarint(out + at, run);
        last = *now;
        frame += run;
    }
    return at;
}

/* A 64-bit number as sixteen hex digits, and a 0. */
static void port_PutHex(char *out, u64 value) {
    s32 i;

    for (i = HEX_DIGITS - 1; i >= 0; i--) {
        u32 digit = (u32)(value & (HEX_DIGITS - 1));

        out[i] =
            (char)(digit < HEX_LETTER ? '0' + digit : 'A' + digit - HEX_LETTER);
        value >>= 4;
    }
    out[HEX_DIGITS] = 0;
}

/* What META says. */
static u32 port_ReplayMeta(char *out, u32 room) {
    char build[HEX_DIGITS + 1];
    char disc[HEX_DIGITS + 1];
    char mods[HEX_DIGITS + 1];
    char start[HEX_DIGITS + 1];
    s32 has_prims;

    port_ReplayStateSize(&has_prims);
    port_PutHex(build, port_ReplayBuild());
    port_PutHex(disc, port_SessionDiscSum());
    port_PutHex(mods, port_ReplayMods());
    port_PutHex(start, g_rec.start_hash);
    return port_ReplayText(
        out, room,
        "date %04d-%02d-%02d %02d:%02d:%02d\nutc %d\nversion %s\n"
        "mode %d\nstage %d\nfighter0 %d %d %d\nfighter1 %d %d %d\n"
        "stage_kind %d\narena %d\ntime %d\nmusic %d\nonline %d\n"
        "bugfixes %d\nwinner %d\nframes %u\nkill %u\nhash_every %d\n"
        "start_count %u\nstart_hash %s\nbuild %s\ndisc %s\nmods %s\n"
        "prims %d\nunfinished %d\n",
        g_rec.date.year, g_rec.date.month, g_rec.date.day, g_rec.date.hour,
        g_rec.date.minute, g_rec.date.second, g_rec.date.is_utc, g_rb_version,
        g_rec.mode, g_rec.stage, g_rec.roster[0], g_rec.weapon[0],
        g_rec.is_cpu[0], g_rec.roster[1], g_rec.weapon[1], g_rec.is_cpu[1],
        g_rec.stage_kind, g_rec.arena, g_rec.time, g_rec.music, g_rec.online,
        g_rec.bugfixes, g_rec.winner, g_rec.length,
        g_rec.kill == NONE ? g_rec.length : g_rec.kill, PORT_REPLAY_HASH_EVERY,
        g_rec.start_count, start, build, disc, mods, has_prims,
        g_rec.unfinished);
}

static s32 port_AppendChunk(s32 file, u32 tag, const void *bytes, u32 size) {
    u8 head[CHUNK_HEAD_BYTES];

    port_PutWord(head, tag);
    port_PutWord(head + WORD_BYTES, size);
    return port_HostAppend(file, head, sizeof(head)) &&
           (size == 0 || port_HostAppend(file, bytes, size));
}

/* A name for the file in the folder that no other has. */
static void port_ReplayName(char *path, u32 room) {
    const char *mode = g_rec.mode >= 0 && g_rec.mode < MODE_WORDS
                           ? g_mode_words[g_rec.mode]
                           : "fight";
    s32 again;

    for (again = 1;; again++) {
        s32 file;
        u32 length = port_ReplayText(
            path, room, "%s/%04d-%02d-%02d_%02d%02d%02d_%s", g_folder,
            g_rec.date.year, g_rec.date.month, g_rec.date.day, g_rec.date.hour,
            g_rec.date.minute, g_rec.date.second,
            g_rec.online != 0 ? "online" : mode);

        if (again > 1) {
            length +=
                port_ReplayText(path + length, room - length, "_%d", again);
        }
        port_ReplayText(path + length, room - length, ".rbr");
        file = port_HostOpen(path);
        if (file < 0) {
            return;
        }
        port_HostClose(file);
    }
}

/* The fight's file, its start compressed: written beside, then given its
 * name. */
static void port_ReplayWrite(void) {
    static u8 head[HEAD_BYTES];
    static char meta[META_BYTES];
    static u8 pieces[WORD_BYTES + PIECES_MAX * (PIECE_NAME_BYTES + WORD_BYTES)];
    static u8 views[WORD_BYTES + VIEWS_MAX * 3 * WORD_BYTES];
    static char path[PORT_REPLAY_PATH_BYTES];
    static char part[PORT_REPLAY_PATH_BYTES];
    u8 *inputs;
    u32 at;
    /* the hashes of the boundaries the fight reached: one that ends
     * with the program may not have reached its last */
    u32 hash_count =
        g_rec.hash_count < g_rec.length / PORT_REPLAY_HASH_EVERY + 1
            ? g_rec.hash_count
            : g_rec.length / PORT_REPLAY_HASH_EVERY + 1;
    u32 size;
    s32 file;
    s32 ok;
    s32 i;

    g_rec.state = REC_IDLE;
    if (g_rec.deflate.failed) {
        port_Log("replay: the fight's start would not compress\n");
        return;
    }
    port_HostMakeFolder(g_folder);
    port_ReplayName(path, sizeof(path));
    port_ReplayText(part, sizeof(part), "%s.part", path);
    file = port_HostCreate(part);
    if (file < 0) {
        port_Log("replay: %s cannot be written\n", part);
        return;
    }
    port_CopyBytes(head, g_magic, MAGIC_BYTES);
    port_PutWord(head + MAGIC_BYTES, FORMAT_VERSION);
    ok = port_HostAppend(file, head, HEAD_BYTES);
    ok &= port_AppendChunk(
        file, TAG_META, meta, port_ReplayMeta(meta, sizeof(meta)));
    port_PutWord(pieces, (u32)port_StatePieceCount());
    at = WORD_BYTES;
    for (i = 0; i < port_StatePieceCount() && i < PIECES_MAX; i++) {
        PortStatePiece piece;
        u32 k;

        port_StateGetPiece(i, &piece);
        for (k = 0; k < PIECE_NAME_BYTES; k++) {
            pieces[at + k] = 0;
        }
        for (k = 0; piece.name[k] != 0 && k + 1 < PIECE_NAME_BYTES; k++) {
            pieces[at + k] = (u8)piece.name[k];
        }
        port_PutWord(pieces + at + PIECE_NAME_BYTES, piece.bytes);
        at += PIECE_NAME_BYTES + WORD_BYTES;
    }
    ok &= port_AppendChunk(file, TAG_PIECES, pieces, at);
    {
        u8 raw[WORD_BYTES];
        u8 chunk[CHUNK_HEAD_BYTES];

        port_PutWord(chunk, TAG_START);
        port_PutWord(chunk + WORD_BYTES, WORD_BYTES + g_rec.deflate.out_at);
        port_PutWord(raw, port_StateSize());
        ok &= port_HostAppend(file, chunk, sizeof(chunk));
        ok &= port_HostAppend(file, raw, sizeof(raw));
        ok &= port_HostAppend(file, g_rec.packed, g_rec.deflate.out_at);
    }
    {
        u8 chunk[CHUNK_HEAD_BYTES];
        u8 match[MATCH_BYTES];
        u32 k;

        port_PutWord(chunk, TAG_DISC);
        port_PutWord(chunk + WORD_BYTES,
                     WORD_BYTES + g_dedupe.match_count * MATCH_BYTES);
        port_PutWord(match, g_dedupe.match_count);
        ok &= port_HostAppend(file, chunk, sizeof(chunk));
        ok &= port_HostAppend(file, match, WORD_BYTES);
        for (k = 0; k < g_dedupe.match_count; k++) {
            port_PutWord(match, g_dedupe.matches[k].at);
            port_PutWord(match + WORD_BYTES, g_dedupe.matches[k].sector);
            port_PutWord(match + 2 * WORD_BYTES, g_dedupe.matches[k].offset);
            port_PutWord(match + 3 * WORD_BYTES, g_dedupe.matches[k].length);
            ok &= port_HostAppend(file, match, MATCH_BYTES);
        }
    }
    port_PutWord(views, (u32)g_rec.view_count);
    for (i = 0; i < g_rec.view_count; i++) {
        u8 *out = views + WORD_BYTES + i * 3 * WORD_BYTES;

        port_PutWord(out, (u32)g_rec.views[i].distance);
        port_PutWord(out + WORD_BYTES, (u32)g_rec.views[i].margin_w);
        port_PutWord(out + 2 * WORD_BYTES, (u32)g_rec.views[i].margin_h);
    }
    ok &= port_AppendChunk(file, TAG_VIEW, views,
                           WORD_BYTES + (u32)g_rec.view_count * 3 * WORD_BYTES);
    /* the inputs into the compressed start's room, which is done with */
    inputs = g_rec.packed;
    port_PutWord(inputs, g_rec.length);
    at = WORD_BYTES + port_ReplayPackInputs(inputs + WORD_BYTES);
    ok &= port_AppendChunk(file, TAG_INPUT, inputs, at);
    port_PutWord(inputs, hash_count);
    port_PutWord(inputs + WORD_BYTES, PORT_REPLAY_HASH_EVERY);
    at = 2 * WORD_BYTES;
    for (i = 0; i < (s32)hash_count; i++) {
        port_PutWord(inputs + at, (u32)g_rec.hashes[i]);
        port_PutWord(
            inputs + at + WORD_BYTES, (u32)(g_rec.hashes[i] >> WORD_BITS));
        at += LONG_BYTES;
    }
    ok &= port_AppendChunk(file, TAG_HASH, inputs, at);
    ok &= port_AppendChunk(file, TAG_END, NULL, 0);
    port_HostClose(file);
    if (!ok || !port_HostRename(part, path)) {
        port_Log("replay: %s cannot be written\n", path);
        port_HostRemove(part);
        return;
    }
    size = port_StateSize();
    port_Log("replay: %s: %u frames, the start %u bytes compressed of %u "
             "(%u of them the disc's, in %u runs); written in %u slices, "
             "%u ms in all, %u ms the most a boundary\n",
             path, g_rec.length, g_rec.deflate.out_at, size, g_dedupe.saved,
             g_dedupe.match_count, g_dedupe.slices,
             g_dedupe.ticks / TICKS_PER_MS, g_dedupe.ticks_most / TICKS_PER_MS);
    port_ReplayPrune(port_ReplayFileSize(path));
}

/* The fight's end is final: its start is compressed, all at once or a
 * slice at each boundary from here, and the file written after. */
static void port_ReplayFinish(void) {
    s32 i;

    g_rec.state = REC_WRITING;
    g_dedupe.work = WORK_INDEX;
    g_dedupe.sector_count = 0;
    g_dedupe.indexed = 0;
    g_dedupe.scanned = 0;
    g_dedupe.cleared_to = 0;
    g_dedupe.match_count = 0;
    g_dedupe.saved = 0;
    g_dedupe.slices = 0;
    g_dedupe.ticks = 0;
    g_dedupe.ticks_most = 0;
    port_ClearBytes(g_dedupe.index, INDEX_SIZE * sizeof(u32));
    port_ClearBytes(g_dedupe.slot_of, (SECTOR_SLOT_MASK + 1) * sizeof(u32));
    g_roll_top = 1;
    for (i = 1; i < BLOCK_WORDS; i++) {
        g_roll_top *= ROLL;
    }
}

/* About `budget` bytes' worth of the file's writing: TRUE once all of
 * it is done and the file can be written. */
static s32 port_ReplayWork(u32 budget) {
    u32 size = port_StateSize();

    if (g_dedupe.work == WORK_INDEX) {
        u32 count = budget / PORT_DISC_DATA_BYTES + 1;

        while (count-- > 0) {
            if (!port_ReplayIndexOne()) {
                g_dedupe.work = WORK_SCAN;
                break;
            }
        }
        return FALSE;
    }
    if (g_dedupe.work == WORK_SCAN) {
        u32 stop = budget > (size - g_dedupe.scanned) / SCAN_PER_SLICE
                       ? size
                       : g_dedupe.scanned + budget * SCAN_PER_SLICE;

        port_ReplayScan(g_rec.snapshot, size, stop);
        if (g_dedupe.scanned >= size) {
            g_dedupe.work = WORK_DEFLATE;
            port_DeflateBegin(&g_rec.deflate, g_rec.snapshot, size,
                              g_rec.packed, g_rec.packed_room);
        }
        return FALSE;
    }
    return port_DeflateSome(&g_rec.deflate, budget);
}

/* The view of the frame at `index`, by its number among the fight's. */
static s32 port_ReplayViewNumber(const PortGameView *view) {
    s32 i;

    for (i = 0; i < g_rec.view_count; i++) {
        if (g_rec.views[i].distance == view->distance &&
            g_rec.views[i].margin_w == view->margin_w &&
            g_rec.views[i].margin_h == view->margin_h) {
            return i;
        }
    }
    if (g_rec.view_count == VIEWS_MAX) {
        return -1;
    }
    g_rec.views[g_rec.view_count] = *view;
    return g_rec.view_count++;
}

/* A fight's first boundary: the state kept, and what it is of. */
static void port_ReplayStart(u32 count) {
    s32 i;

    if (g_rec.state == REC_WRITING) {
        /* the last one's start is still being compressed: all of it now */
        while (!port_ReplayWork(FILE_MAX)) {
        }
        port_ReplayWrite();
    }
    if (!port_ReplayRoom()) {
        return;
    }
    g_rec.state = REC_FIGHT;
    g_rec.unfinished = FALSE;
    g_rec.length = 0;
    g_rec.hash_count = 0;
    g_rec.kill = NONE;
    g_rec.view_count = 0;
    if (g_before.count == count - 1 && g_before.is_kept &&
        g_last.index == count - 1) {
        /* the boundary before the fight's load: the playback loads the
         * stage and the fighters as the fight did, and everything the
         * picture makes of a stage as it loads is made for it */
        u32 size = port_StateSize();

        port_CopyBytes(g_rec.snapshot, g_before.snapshot, size);
        g_rec.start_count = g_before.count;
        g_rec.start_hash = g_before.hash;
        g_rec.hashes[0] = g_before.hash;
        g_rec.hash_count = 1;
        g_rec.frames[0] = g_last.frame;
        g_rec.frames[0].view = (u8)port_ReplayViewNumber(&g_before.view);
        g_rec.length = 1;
    } else {
        /* no boundary before it (a run that starts in a fight) */
        port_StateSave(g_rec.snapshot);
        g_rec.start_count = count;
        g_rec.start_hash = port_StateHash();
    }
    port_ReplayClearOwn(g_rec.snapshot);
    port_HostDate(&g_rec.date);
    g_rec.mode = g_fight_mode;
    g_rec.stage = g_fight_stage;
    for (i = 0; i < PORT_PADS; i++) {
        g_rec.roster[i] = g_fighters[i].roster_id;
        g_rec.weapon[i] = g_fighters[i].weapon_id;
        g_rec.is_cpu[i] = g_fighters[i].is_cpu;
    }
    g_rec.stage_kind = g_port_stage_choice.fight_kind;
    g_rec.arena = g_port_stage_choice.arena;
    g_rec.time = g_port_stage_choice.fight_time;
    g_rec.music = g_port_fight_music.track;
    g_rec.online = port_IsNetplayOn()
                       ? (port_GetSessionRole() != 0 ? port_GetSessionRole()
                                                     : PORT_SESSION_JOIN + 1)
                       : 0;
    g_rec.bugfixes = g_rb_settings.bugfixes;
}

/* A boundary of the fight being recorded, `count` the input to be taken
 * next. */
static void port_ReplayRecordFrame(
    u32 count, s32 in_fight, const PortGameView *view) {
    u32 index = count - g_rec.start_count;
    s32 number;

    if (index >= FRAMES_MAX - 1) {
        port_ReplayAbandon("it is longer than a replay can be");
        return;
    }
    if (index % PORT_REPLAY_HASH_EVERY == 0) {
        g_rec.hashes[index / PORT_REPLAY_HASH_EVERY] = port_StateHash();
        g_rec.hash_count = index / PORT_REPLAY_HASH_EVERY + 1;
    }
    if (g_fight_defeat_frames != 0) {
        if (g_rec.kill == NONE || index < g_rec.kill) {
            g_rec.kill = index;
        }
    } else if (g_rec.kill != NONE && index <= g_rec.kill) {
        /* a rollback took the cut back */
        g_rec.kill = NONE;
    }
    if (!in_fight) {
        g_rec.end_count = count;
        g_rec.winner = g_fight_winner;
        g_rec.state = REC_ENDED;
        return;
    }
    number = port_ReplayViewNumber(view);
    if (number < 0) {
        port_ReplayAbandon("its picture changed too often");
        return;
    }
    g_rec.frames[index].view = (u8)number;
}

/* The input the game took kept, as the fight's frame `index` on (a
 * frame run again writes it again). */
void port_ReplayTook(const PortInput *input, u32 index) {
    ReplayFrame *frame;
    u32 at;

    if (g_port_ahead) {
        return;
    }
    g_last.index = index;
    g_last.frame.buttons[0] = input->buttons[0];
    g_last.frame.buttons[1] = input->buttons[1];
    g_last.frame.present =
        (u8)((input->present[0] != 0) | ((input->present[1] != 0) << 1));
    if (g_rec.state != REC_FIGHT || index < g_rec.start_count) {
        return;
    }
    at = index - g_rec.start_count;
    if (at >= FRAMES_MAX) {
        return;
    }
    frame = &g_rec.frames[at];
    frame->buttons[0] = input->buttons[0];
    frame->buttons[1] = input->buttons[1];
    frame->present =
        (u8)((input->present[0] != 0) | ((input->present[1] != 0) << 1));
    g_rec.length = at + 1;
}

/* Every frame of a session's fight is known and run as it was: the
 * local game's are as soon as they run, netplay's once both pads'
 * inputs are in and nothing is to be run again. */
static s32 port_ReplayIsFinal(u32 count) {
    if (!port_IsNetplayOn()) {
        return TRUE;
    }
    return !g_port_rerun && port_NetplayFinalFrames() >= count;
}

/* The program ends: the fight as far as it went (port_replay.h). */
void port_ReplayAtQuit(void) {
    if (g_rec.state == REC_FIGHT && !port_IsNetplayOn() && g_rec.length > 0) {
        /* the fight as far as it went: its inputs taken so far */
        g_rec.end_count = g_rec.start_count + g_rec.length;
        g_rec.winner = -1;
        g_rec.unfinished = TRUE;
        g_rec.state = REC_ENDED;
    }
    if (g_rec.state == REC_ENDED &&
        (!port_IsNetplayOn() || port_ReplayIsFinal(g_rec.end_count))) {
        port_ReplayFinish();
    }
    if (g_rec.state == REC_WRITING) {
        while (!port_ReplayWork(FILE_MAX)) {
        }
        port_ReplayWrite();
    }
}

/* The recording's turn at a boundary: a fight's start seen (and seen
 * again where a rollback went back before it), its frames noted, its end,
 * and the file's writing a slice at a time. */
static void port_ReplayRecordBoundary(
    u32 count, s32 in_fight, const PortGameView *view) {
    s32 was_in_fight =
        count > 0 && g_ring_count[(count - 1) & FIGHT_RING_MASK] == count - 1 &&
        g_ring_fight[(count - 1) & FIGHT_RING_MASK];

    g_ring_count[count & FIGHT_RING_MASK] = count;
    g_ring_fight[count & FIGHT_RING_MASK] = (u8)in_fight;
    if (g_rec.state == REC_WRITING) {
        u32 start = port_HostTicks();
        u32 took;
        s32 done;

        /* pieces of the work until the slice's time is gone */
        do {
            done = port_ReplayWork(g_slice != 0 ? WORK_PIECE : FILE_MAX);
            took = port_HostTicks() - start;
        } while (!done && took < g_slice * PORT_HOST_TICKS_PER_MICROSECOND);

        g_dedupe.ticks += took;
        g_dedupe.slices++;
        if (took > g_dedupe.ticks_most) {
            g_dedupe.ticks_most = took;
        }
        if (done) {
            port_ReplayWrite();
        }
    }
    if (g_rec.state == REC_FIGHT || g_rec.state == REC_ENDED) {
        if (count <= g_rec.start_count) {
            /* a rollback to before the fight's start: it is seen again */
            g_rec.state = REC_IDLE;
        } else if (g_rec.state == REC_ENDED && count <= g_rec.end_count) {
            g_rec.state = REC_FIGHT;
        }
    }
    if ((g_rec.state == REC_IDLE || g_rec.state == REC_WRITING) && in_fight &&
        !was_in_fight && port_ReplayMayRecord() && g_port_steps > 0) {
        port_ReplayStart(count);
    }
    if (g_rec.state == REC_FIGHT) {
        port_ReplayRecordFrame(count, in_fight, view);
    }
    if (!in_fight && port_ReplayMayRecord() && g_port_steps > 0 &&
        port_ReplayRoom()) {
        /* each boundary out of a fight kept, for the fight that may come
         * next: its pages written since the last (port_state.h) */
        port_StateSave(g_before.snapshot);
        g_before.hash = port_StateHash();
        g_before.count = count;
        g_before.view = *view;
        g_before.is_kept = TRUE;
    }
    if (g_rec.state == REC_ENDED && port_ReplayIsFinal(g_rec.end_count)) {
        if (g_rec.length != g_rec.end_count - g_rec.start_count) {
            port_ReplayAbandon("its inputs are not whole");
            return;
        }
        port_ReplayFinish();
        if (g_slice == 0) {
            u32 start = port_HostTicks();

            while (!port_ReplayWork(FILE_MAX)) {
            }
            g_dedupe.ticks = port_HostTicks() - start;
            g_dedupe.ticks_most = g_dedupe.ticks;
            g_dedupe.slices = 1;
            port_ReplayWrite();
        }
    }
}

/* ---- reading ---- */

/* A chunk of the file in `bytes`: its size and where its bytes are, or
 * FALSE where there is none of the tag. */
static s32 port_FindChunk(
    const u8 *bytes, u32 size, u32 tag, const u8 **found, u32 *found_size) {
    u32 at = HEAD_BYTES;

    while (at + CHUNK_HEAD_BYTES <= size) {
        u32 this_tag = port_GetWord(bytes + at);
        u32 this_size = port_GetWord(bytes + at + WORD_BYTES);

        if (this_size > size - at - CHUNK_HEAD_BYTES) {
            return FALSE;
        }
        if (this_tag == tag) {
            *found = bytes + at + CHUNK_HEAD_BYTES;
            *found_size = this_size;
            return TRUE;
        }
        at += CHUNK_HEAD_BYTES + this_size;
    }
    return FALSE;
}

/* The value of META's line `key` into `out`: FALSE where it has none. */
static s32 port_MetaValue(
    const u8 *meta, u32 size, const char *key, char *out, u32 room) {
    u32 at = 0;

    while (at < size) {
        u32 k = 0;
        u32 end = at;

        while (end < size && meta[end] != '\n') {
            end++;
        }
        while (key[k] != 0 && at + k < end && meta[at + k] == (u8)key[k]) {
            k++;
        }
        if (key[k] == 0 && at + k < end && meta[at + k] == ' ') {
            u32 used = 0;

            for (at += k + 1; at < end && used + 1 < room; at++) {
                out[used++] = (char)meta[at];
            }
            out[used] = 0;
            return TRUE;
        }
        at = end + 1;
    }
    return FALSE;
}

/* The number META says for `key`, or 0. */
static s32 port_MetaNumber(const u8 *meta, u32 size, const char *key) {
    char text[PORT_REPLAY_WHY_BYTES];
    s32 value = 0;
    s32 sign = 1;
    s32 i = 0;

    if (!port_MetaValue(meta, size, key, text, sizeof(text))) {
        return 0;
    }
    if (text[0] == '-') {
        sign = -1;
        i++;
    }
    for (; text[i] >= '0' && text[i] <= '9'; i++) {
        value = value * DECIMAL + (text[i] - '0');
    }
    return sign * value;
}

/* The `index`th number of a line of numbers. */
static s32 port_MetaNumberAt(
    const u8 *meta, u32 size, const char *key, s32 index) {
    char text[PORT_REPLAY_WHY_BYTES];
    s32 i = 0;
    s32 value;
    s32 sign;

    if (!port_MetaValue(meta, size, key, text, sizeof(text))) {
        return 0;
    }
    for (; index > 0; index--) {
        while (text[i] != 0 && text[i] != ' ') {
            i++;
        }
        while (text[i] == ' ') {
            i++;
        }
    }
    sign = text[i] == '-' ? -1 : 1;
    i += text[i] == '-';
    for (value = 0; text[i] >= '0' && text[i] <= '9'; i++) {
        value = value * DECIMAL + (text[i] - '0');
    }
    return sign * value;
}

/* The hex number META says for `key` (a sum), or 0. */
static u64 port_MetaHex(const u8 *meta, u32 size, const char *key) {
    char text[PORT_REPLAY_WHY_BYTES];
    u64 value = 0;
    s32 i;

    if (!port_MetaValue(meta, size, key, text, sizeof(text))) {
        return 0;
    }
    for (i = 0; text[i] != 0; i++) {
        char letter = text[i];
        u32 digit = letter >= '0' && letter <= '9' ? (u32)(letter - '0')
                    : letter >= 'A' && letter <= 'F'
                        ? (u32)(letter - 'A' + HEX_LETTER)
                        : 0;

        value = (value << 4) | digit;
    }
    return value;
}

/* Whether this program can play what `bytes` holds, and why not. */
static s32 port_ReplayPlayable(const u8 *bytes, u32 size, char *why, u32 room) {
    const u8 *meta;
    const u8 *pieces;
    u32 meta_size;
    u32 pieces_size;
    s32 has_prims;
    u32 count;
    u32 i;

    if (size < HEAD_BYTES ||
        port_GetWord(bytes + MAGIC_BYTES) != FORMAT_VERSION) {
        port_ReplayText(why, room, "a replay of another version of the format");
        return FALSE;
    }
    if (!port_FindChunk(bytes, size, TAG_META, &meta, &meta_size) ||
        !port_FindChunk(bytes, size, TAG_PIECES, &pieces, &pieces_size) ||
        pieces_size < WORD_BYTES) {
        port_ReplayText(why, room, "the file is not whole");
        return FALSE;
    }
    if (port_MetaHex(meta, meta_size, "build") != port_ReplayBuild()) {
        char version[PORT_REPLAY_WHY_BYTES];

        if (!port_MetaValue(
                meta, meta_size, "version", version, sizeof(version))) {
            version[0] = 0;
        }
        port_ReplayText(why, room,
                        "recorded by another build (%s): only the build that "
                        "recorded it plays it",
                        version);
        return FALSE;
    }
    if (port_MetaHex(meta, meta_size, "disc") != port_SessionDiscSum()) {
        port_ReplayText(why, room, "recorded from another disc image");
        return FALSE;
    }
    if (port_MetaHex(meta, meta_size, "mods") != port_ReplayMods()) {
        port_ReplayText(why, room,
                        "recorded with other gameplay mods (stages, weapons, "
                        "motions or arenas)");
        return FALSE;
    }
    port_ReplayStateSize(&has_prims);
    if (port_MetaNumber(meta, meta_size, "prims") != 0 && !has_prims) {
        port_ReplayText(why, room,
                        "recorded with a draw distance past the console's: "
                        "start with draw_distance far or max to play it");
        return FALSE;
    }
    count = port_GetWord(pieces);
    if (count > PIECES_MAX ||
        pieces_size < WORD_BYTES + count * (PIECE_NAME_BYTES + WORD_BYTES)) {
        port_ReplayText(why, room, "the file is not whole");
        return FALSE;
    }
    for (i = 0; i < count; i++) {
        const u8 *at =
            pieces + WORD_BYTES + i * (PIECE_NAME_BYTES + WORD_BYTES);
        s32 found = FALSE;
        s32 k;

        for (k = 0; k < port_StatePieceCount(); k++) {
            PortStatePiece piece;

            port_StateGetPiece(k, &piece);
            if (port_TextsEqual((const char *)at, piece.name)) {
                found = piece.bytes == port_GetWord(at + PIECE_NAME_BYTES);
            }
        }
        if (!found) {
            port_ReplayText(why, room, "its state is laid out otherwise (%s)",
                            (const char *)at);
            return FALSE;
        }
    }
    why[0] = 0;
    return TRUE;
}

/* What the file in `bytes` says of itself, into `info`. */
static void port_ReplayInfoOf(const u8 *bytes, u32 size, PortReplayInfo *info) {
    const u8 *meta;
    u32 meta_size;
    s32 i;

    if (!port_FindChunk(bytes, size, TAG_META, &meta, &meta_size)) {
        meta = bytes;
        meta_size = 0;
    }
    if (!port_MetaValue(
            meta, meta_size, "date", info->date, sizeof(info->date))) {
        info->date[0] = 0;
    }
    info->mode = port_MetaNumber(meta, meta_size, "mode");
    info->stage = port_MetaNumber(meta, meta_size, "stage");
    for (i = 0; i < PORT_PADS; i++) {
        const char *key = i == 0 ? "fighter0" : "fighter1";

        info->roster[i] = port_MetaNumberAt(meta, meta_size, key, 0);
        info->weapon[i] = port_MetaNumberAt(meta, meta_size, key, 1);
        info->is_cpu[i] = port_MetaNumberAt(meta, meta_size, key, 2);
    }
    info->stage_kind = port_MetaNumber(meta, meta_size, "stage_kind");
    info->arena = port_MetaNumber(meta, meta_size, "arena");
    info->time = port_MetaNumber(meta, meta_size, "time");
    info->music = port_MetaNumber(meta, meta_size, "music");
    info->online = port_MetaNumber(meta, meta_size, "online");
    info->winner = port_MetaNumber(meta, meta_size, "winner");
    info->frames = (u32)port_MetaNumber(meta, meta_size, "frames");
    info->kill = (u32)port_MetaNumber(meta, meta_size, "kill");
    info->playable =
        port_ReplayPlayable(bytes, size, info->why, sizeof(info->why));
}

/* The last part of a path: the file's own name. */
static void port_ReplayNameOf(const char *path, char *name, u32 room) {
    u32 from = 0;
    u32 i;

    for (i = 0; path[i] != 0; i++) {
        if (path[i] == '/' || path[i] == '\\') {
            from = i + 1;
        }
    }
    for (i = 0; path[from + i] != 0 && i + 1 < room; i++) {
        name[i] = path[from + i];
    }
    name[i] = 0;
}

/* The head of a replay's file read, and what it says (port_replay.h). */
s32 port_ReadReplayInfo(const char *path, PortReplayInfo *info) {
    static u8 bytes[INFO_READ_BYTES];
    s32 file = port_HostOpen(path);
    u32 size;
    u32 i;

    if (file < 0) {
        return FALSE;
    }
    info->bytes = port_HostFileSize(file);
    size = port_HostReadAt(file, 0, bytes, sizeof(bytes));
    port_HostClose(file);
    for (i = 0; i < MAGIC_BYTES; i++) {
        if (size < HEAD_BYTES || bytes[i] != (u8)g_magic[i]) {
            return FALSE;
        }
    }
    for (i = 0; path[i] != 0 && i + 1 < sizeof(info->path); i++) {
        info->path[i] = path[i];
    }
    info->path[i] = 0;
    port_ReplayNameOf(path, info->name, sizeof(info->name));
    port_ReplayInfoOf(bytes, size, info);
    return TRUE;
}

/* Each replay of the folder read, the newest first (port_replay.h). */
s32 port_ListReplays(PortReplayInfo *infos, s32 most) {
    static char path[PORT_REPLAY_PATH_BYTES];
    s32 count = 0;
    s32 i;

    port_ReplayListNames();
    for (i = g_list_count - 1; i >= 0 && count < most; i--) {
        port_ReplayPathOf(g_list_names[i], path, sizeof(path));
        if (port_ReadReplayInfo(path, &infos[count])) {
            count++;
        }
    }
    return count;
}

/* ---- playing ---- */

static u32 port_GetVarint(const u8 *in, u32 size, u32 *at) {
    u32 value = 0;
    s32 shift = 0;

    while (*at < size) {
        u8 byte = in[(*at)++];

        value |= (u32)(byte & VARINT_MASK) << shift;
        if ((byte & VARINT_MORE) == 0) {
            break;
        }
        shift += VARINT_BITS;
    }
    return value;
}

/* INPT into the playback's frames: FALSE where it is not whole. */
static s32 port_ReplayUnpackInputs(const u8 *in, u32 size) {
    ReplayFrame now = {{0, 0}, 0, 0};
    u32 frames;
    u32 frame = 0;
    u32 at = WORD_BYTES;

    if (size < WORD_BYTES) {
        return FALSE;
    }
    frames = port_GetWord(in);
    if (frames >= FRAMES_MAX) {
        return FALSE;
    }
    while (frame < frames && at < size) {
        u8 changed = in[at++];
        u32 run;

        if ((changed & CHANGED_FIRST) && at + HALF_BYTES <= size) {
            now.buttons[0] = (u16)(in[at] | (in[at + 1] << BITS_PER_BYTE));
            at += HALF_BYTES;
        }
        if ((changed & CHANGED_SECOND) && at + HALF_BYTES <= size) {
            now.buttons[1] = (u16)(in[at] | (in[at + 1] << BITS_PER_BYTE));
            at += HALF_BYTES;
        }
        if ((changed & CHANGED_PRESENT) && at < size) {
            now.present = in[at++];
        }
        if ((changed & CHANGED_VIEW) && at < size) {
            now.view = in[at++];
        }
        run = port_GetVarint(in, size, &at);
        while (run-- > 0 && frame < frames) {
            g_play.list[frame++] = now;
        }
    }
    g_play.frames = frames;
    return frame == frames;
}

/* The start of the file in `bytes` into the playback's snapshot, laid
 * out as this run's state is: a piece this run has and the file has not
 * (the port's own primitive buffers) is kept as it is now. */
static s32 port_ReplayUnpackStart(const u8 *bytes, u32 size) {
    const u8 *start;
    const u8 *pieces;
    const u8 *disc;
    u32 start_size;
    u32 pieces_size;
    u32 disc_size;
    u32 raw;
    u32 count;
    u32 from = 0;
    u32 i;
    s32 k;

    if (!port_FindChunk(bytes, size, TAG_START, &start, &start_size) ||
        !port_FindChunk(bytes, size, TAG_PIECES, &pieces, &pieces_size) ||
        start_size < WORD_BYTES) {
        return FALSE;
    }
    raw = port_GetWord(start);
    if (raw > port_StateSize() ||
        !port_ZlibInflate(
            start + WORD_BYTES, start_size - WORD_BYTES, g_play.back, raw)) {
        return FALSE;
    }
    if (!port_FindChunk(bytes, size, TAG_DISC, &disc, &disc_size) ||
        disc_size < WORD_BYTES ||
        disc_size < WORD_BYTES + port_GetWord(disc) * MATCH_BYTES ||
        !port_ReplayRefill(
            g_play.back, raw, disc + WORD_BYTES, port_GetWord(disc))) {
        return FALSE;
    }
    count = port_GetWord(pieces);
    for (k = 0; k < port_StatePieceCount(); k++) {
        PortStatePiece piece;

        port_StateGetPiece(k, &piece);
        port_CopyBytes(g_play.start + piece.offset, piece.start, piece.bytes);
    }
    for (i = 0; i < count; i++) {
        const u8 *at =
            pieces + WORD_BYTES + i * (PIECE_NAME_BYTES + WORD_BYTES);
        u32 bytes_of = port_GetWord(at + PIECE_NAME_BYTES);

        for (k = 0; k < port_StatePieceCount(); k++) {
            PortStatePiece piece;

            port_StateGetPiece(k, &piece);
            if (port_TextsEqual((const char *)at, piece.name) &&
                piece.bytes == bytes_of && from + bytes_of <= raw) {
                port_CopyBytes(
                    g_play.start + piece.offset, g_play.back + from, bytes_of);
            }
        }
        from += bytes_of;
    }
    return from == raw;
}

/* INPT, HASH and VIEW into the playback: FALSE where one is not whole. */
static s32 port_ReplayUnpackRest(const u8 *bytes, u32 size) {
    const u8 *chunk;
    u32 chunk_size;
    u32 i;

    if (!port_FindChunk(bytes, size, TAG_INPUT, &chunk, &chunk_size) ||
        !port_ReplayUnpackInputs(chunk, chunk_size)) {
        return FALSE;
    }
    if (!port_FindChunk(bytes, size, TAG_HASH, &chunk, &chunk_size) ||
        chunk_size < 2 * WORD_BYTES ||
        port_GetWord(chunk + WORD_BYTES) != PORT_REPLAY_HASH_EVERY) {
        return FALSE;
    }
    g_play.hash_count = port_GetWord(chunk);
    if (g_play.hash_count > HASHES_MAX ||
        chunk_size < 2 * WORD_BYTES + g_play.hash_count * LONG_BYTES) {
        return FALSE;
    }
    for (i = 0; i < g_play.hash_count; i++) {
        const u8 *at = chunk + 2 * WORD_BYTES + i * LONG_BYTES;

        g_play.hashes[i] = port_GetWord(at) |
                           ((u64)port_GetWord(at + WORD_BYTES) << WORD_BITS);
    }
    if (!port_FindChunk(bytes, size, TAG_VIEW, &chunk, &chunk_size) ||
        chunk_size < WORD_BYTES || port_GetWord(chunk) > VIEWS_MAX) {
        return FALSE;
    }
    g_play.view_count = (s32)port_GetWord(chunk);
    for (i = 0; i < (u32)g_play.view_count; i++) {
        const u8 *at = chunk + WORD_BYTES + i * 3 * WORD_BYTES;

        g_play.views[i].distance = (s32)port_GetWord(at);
        g_play.views[i].margin_w = (s32)port_GetWord(at + WORD_BYTES);
        g_play.views[i].margin_h = (s32)port_GetWord(at + 2 * WORD_BYTES);
    }
    for (i = 0; i < g_play.frames; i++) {
        if (g_play.list[i].view >= g_play.view_count) {
            return FALSE;
        }
    }
    return TRUE;
}

/* The playback's memory, given once: FALSE where there is none. */
static s32 port_ReplayPlayRoom(void) {
    u32 size = port_StateSize();

    if (g_play.start != NULL) {
        return TRUE;
    }
    g_play.file = port_HostAllocate(FILE_MAX);
    g_play.start = port_HostAllocate(size);
    g_play.back = port_HostAllocate(size);
    g_play.list = port_HostAllocate(FRAMES_MAX * sizeof(ReplayFrame));
    g_play.hashes = port_HostAllocate(HASHES_MAX * sizeof(u64));
    g_kept_run =
        port_HostAllocate((u32)(g_port_state_data_end - g_port_run_data) +
                          (u32)(g_port_state_bss_end - g_port_run_bss) + 1);
    if (g_play.file == NULL || g_play.start == NULL || g_play.back == NULL ||
        g_play.list == NULL || g_play.hashes == NULL || g_kept_run == NULL) {
        g_play.start = NULL;
        return FALSE;
    }
    return TRUE;
}

/* A replay read and held to this program, to be played from the next
 * boundary (port_replay.h). */
s32 port_PlayReplay(
    const char *path, s32 quiet, s32 then_quit, char *why, u32 room) {
    s32 file;
    u32 size;
    u32 i;
    const u8 *meta;
    u32 meta_size;

    if (g_play.state != PLAY_OFF) {
        port_ReplayText(why, room, "a replay is being played");
        return FALSE;
    }
    if (port_IsNetplayOn() || port_LinkIsUp()) {
        port_ReplayText(why, room, "not while playing online");
        return FALSE;
    }
    if (g_sys_screen == SCREEN_FIGHT_UPDATE) {
        /* the fight's stage is the picture's until the next load: a
         * replay is watched from a menu, and its own load makes its own */
        port_ReplayText(why, room, "not in the middle of a fight: from a menu");
        return FALSE;
    }
    if (!port_ReplayPlayRoom()) {
        port_ReplayText(why, room, "no memory to play a replay in");
        return FALSE;
    }
    file = port_HostOpen(path);
    if (file < 0) {
        port_ReplayText(why, room, "the file cannot be read");
        return FALSE;
    }
    size = port_HostFileSize(file);
    if (size > FILE_MAX ||
        port_HostReadAt(file, 0, g_play.file, size) != size) {
        port_HostClose(file);
        port_ReplayText(why, room, "the file cannot be read");
        return FALSE;
    }
    port_HostClose(file);
    for (i = 0; i < MAGIC_BYTES; i++) {
        if (size < HEAD_BYTES || g_play.file[i] != (u8)g_magic[i]) {
            port_ReplayText(why, room, "not a replay");
            return FALSE;
        }
    }
    if (!port_ReplayPlayable(g_play.file, size, why, room)) {
        return FALSE;
    }
    if (!port_ReplayUnpackStart(g_play.file, size) ||
        !port_ReplayUnpackRest(g_play.file, size)) {
        port_ReplayText(why, room, "the file is not whole");
        return FALSE;
    }
    port_FindChunk(g_play.file, size, TAG_META, &meta, &meta_size);
    g_play.start_count = (u32)port_MetaNumber(meta, meta_size, "start_count");
    g_play.start_hash = port_MetaHex(meta, meta_size, "start_hash");
    g_play.kill = (u32)port_MetaNumber(meta, meta_size, "kill");
    for (i = 0; path[i] != 0 && i + 1 < sizeof(g_play.path); i++) {
        g_play.path[i] = path[i];
    }
    g_play.path[i] = 0;
    g_play.quiet = quiet;
    g_play.then_quit = then_quit;
    g_play.paused = FALSE;
    g_play.step_once = FALSE;
    g_play.stop_asked = FALSE;
    g_play.seek_to = NONE;
    g_play.speed = SPEED_ONE;
    g_play.frame = 0;
    g_play.desync = FALSE;
    g_play.hashes_matched = 0;
    g_play.state = PLAY_PENDING;
    return TRUE;
}

s32 port_ReplayIsPlaying(void) { return g_play.state != PLAY_OFF; }

/* The game takes its input: the recorded one of its frame. */
static void port_ReplayTakeHook(PortInput *input) {
    u32 index = g_port_input_count - g_play.start_count;
    const ReplayFrame *frame;

    if (g_play.frames == 0) {
        return;
    }
    frame = &g_play.list[index < g_play.frames ? index : g_play.frames - 1];
    input->buttons[0] = frame->buttons[0];
    input->buttons[1] = frame->buttons[1];
    input->present[0] = (u8)(frame->present & 1);
    input->present[1] = (u8)((frame->present >> 1) & 1);
}

/* This run's own, kept out of a load of the replay's start and put back
 * after it (`out`: kept out). */
static void port_ReplayKeep(s32 out) {
    u32 data = (u32)(g_port_state_data_end - g_port_run_data);
    u32 bss = (u32)(g_port_state_bss_end - g_port_run_bss);
    s32 i;

    for (i = 0; i < KEPT_HOOKS; i++) {
        if (out) {
            g_kept_values[i] = *g_kept_hooks[i];
        } else {
            *g_kept_hooks[i] = g_kept_values[i];
        }
    }
    if (out) {
        g_kept_unmixed = g_port_xa_unmixed;
        port_CopyBytes(g_kept_run, g_port_run_data, data);
        port_CopyBytes(g_kept_run + data, g_port_run_bss, bss);
    } else {
        g_port_xa_unmixed = g_kept_unmixed;
        port_CopyBytes(g_port_run_data, g_kept_run, data);
        port_CopyBytes(g_port_run_bss, g_kept_run + data, bss);
    }
}

/* The replay's first boundary put in. */
static void port_ReplayLoadStart(void) {
    port_ReplayKeep(TRUE);
    port_StateLoad(g_play.start);
    port_ReplayKeep(FALSE);
    port_CardsForReplay();
    g_port_take_hook = port_ReplayTakeHook;
}

/* The playback ends: the game as it was before it put back. */
static void port_ReplayEnd(const char *trouble) {
    PortGameView view;

    g_play.state = PLAY_OFF;
    port_StateLoad(g_play.back);
    port_RemasterHoldGameView(NULL);
    port_RemasterGameView(&view);
    port_RemasterHoldGameView(&view);
    if (trouble != NULL) {
        port_Log("replay: %s: %s\n", g_play.path, trouble);
    } else {
        port_Log("replay: %s: %u frames played, %u hashes the same as "
                 "recorded\n",
                 g_play.path, g_play.frame, g_play.hashes_matched);
    }
    if (g_port_replay_end_hook != NULL) {
        g_port_replay_end_hook(trouble);
    }
    if (g_play.quiet || g_play.then_quit) {
        port_Quit(trouble != NULL ? PORT_EXIT_FAILED : PORT_EXIT_OK);
    }
}

/* The playback begins at a boundary: the game kept to come back to,
 * the replay's start put in, and held to the one recorded. */
static void port_ReplayBegin(void) {
    port_ReplayAbandon("a replay is played");
    if (g_rec.state == REC_WRITING) {
        while (!port_ReplayWork(FILE_MAX)) {
        }
        port_ReplayWrite();
    }
    port_StateSave(g_play.back);
    port_ReplayLoadStart();
    g_play.state = PLAY_ON;
    if (port_StateHash() != g_play.start_hash) {
        port_ReplayEnd("its start is not this program's: the state put in "
                       "is not the one recorded");
        return;
    }
    port_Log("replay: %s: %u frames from input %u\n", g_play.path,
             g_play.frames, g_play.start_count);
}

/* A boundary of the playback: the frame's view held and its hash held to
 * the one recorded; FALSE where the playback ended here. */
static s32 port_ReplayPlayFrame(void) {
    static char trouble[PORT_REPLAY_WHY_BYTES];
    u32 index = g_port_input_count - g_play.start_count;

    g_play.frame = index;
    if (index % PORT_REPLAY_HASH_EVERY == 0 &&
        index / PORT_REPLAY_HASH_EVERY < g_play.hash_count) {
        u64 hash = port_StateHash();
        u64 recorded = g_play.hashes[index / PORT_REPLAY_HASH_EVERY];

        if (hash != recorded) {
            g_play.desync = TRUE;
            port_ReplayText(trouble, sizeof(trouble),
                            "DESYNC at frame %u: the state's hash is "
                            "%08X%08X, recorded %08X%08X",
                            index, (u32)(hash >> WORD_BITS), (u32)hash,
                            (u32)(recorded >> WORD_BITS), (u32)recorded);
            port_ReplayEnd(trouble);
            return FALSE;
        }
        g_play.hashes_matched++;
    }
    if (index >= g_play.frames || g_play.stop_asked) {
        port_ReplayEnd(NULL);
        return FALSE;
    }
    port_RemasterHoldGameView(&g_play.views[g_play.list[index].view]);
    return TRUE;
}

/* A step of the playback that is not seen: fast forward and a skip. */
static s32 port_ReplayQuietStep(void) {
    s32 was = g_port_rerun;

    g_port_rerun = TRUE;
    port_StepGame();
    g_port_rerun = was;
    return port_ReplayPlayFrame();
}

/* To frame `to`, from the start again where it is not ahead, the frames
 * between run unseen: FALSE where the playback ended on the way. */
static s32 port_ReplaySeek(u32 to) {
    if (to <= g_play.frame) {
        port_ReplayLoadStart();
        if (!port_ReplayPlayFrame()) {
            return FALSE;
        }
    }
    while (g_play.frame < to) {
        if (!port_ReplayQuietStep()) {
            return FALSE;
        }
    }
    return TRUE;
}

/* --replay-check: the replay played unseen to its end, with a restart
 * from its middle and a skip from the start to before its first cut on
 * the way, as the page's keys ask for them: every hash held each time. */
static void port_ReplayCheckAll(void) {
    u32 half = g_play.frames / 2;
    s32 restarted = FALSE;

    for (;;) {
        if (!restarted && half > 0 && g_play.frame == half) {
            restarted = TRUE;
            if (!port_ReplaySeek(0) ||
                (g_play.kill > KILL_LEAD &&
                 !port_ReplaySeek(g_play.kill - KILL_LEAD))) {
                return;
            }
            continue;
        }
        if (!port_ReplayQuietStep()) {
            return;
        }
    }
}

/* The playback's turn at a boundary: the frame held to the recording;
 * then, as the player asks, a skip, a pause, frames run ahead unseen to
 * go faster, or blanks waited to go slower. */
static void port_ReplayPlayBoundary(void) {
    s32 i;

    if (!port_ReplayPlayFrame()) {
        return;
    }
    if (g_play.quiet) {
        port_ReplayCheckAll();
        return;
    }
    /* a skip asked for, and a pause, until it goes on, a frame is asked
     * for or it stops */
    for (;;) {
        if (g_play.seek_to != NONE) {
            u32 to = g_play.seek_to;

            g_play.seek_to = NONE;
            if (!port_ReplaySeek(to)) {
                return;
            }
        }
        if (g_play.stop_asked) {
            port_ReplayEnd(NULL);
            return;
        }
        if (!g_play.paused || g_play.step_once ||
            g_port_replay_wait_hook == NULL) {
            break;
        }
        g_port_replay_wait_hook();
    }
    g_play.step_once = FALSE;
    for (i = SPEED_ONE; i < g_play.speed; i += SPEED_ONE) {
        if (!port_ReplayQuietStep()) {
            return;
        }
    }
    if (g_port_replay_wait_hook != NULL && g_play.speed < SPEED_ONE) {
        for (i = 0; i < (SPEED_ONE / g_play.speed - 1) * BLANKS_A_FRAME; i++) {
            g_port_replay_wait_hook();
        }
    }
}

/* What the player asks of the playback (port_replay.h). */
void port_ReplayControl(s32 what) {
    if (g_play.state != PLAY_ON) {
        return;
    }
    if (what == PORT_REPLAY_PAUSE) {
        g_play.paused = !g_play.paused;
    } else if (what == PORT_REPLAY_STEP) {
        g_play.paused = TRUE;
        g_play.step_once = TRUE;
    } else if (what == PORT_REPLAY_SLOWER) {
        g_play.speed = g_play.speed > SPEED_MIN ? g_play.speed / 2 : SPEED_MIN;
    } else if (what == PORT_REPLAY_FASTER) {
        g_play.speed = g_play.speed < SPEED_MAX ? g_play.speed * 2 : SPEED_MAX;
    } else if (what == PORT_REPLAY_RESTART) {
        g_play.seek_to = 0;
    } else if (what == PORT_REPLAY_KILL) {
        g_play.seek_to = g_play.kill > KILL_LEAD ? g_play.kill - KILL_LEAD : 0;
    } else if (what == PORT_REPLAY_STOP) {
        g_play.stop_asked = TRUE;
    }
}

void port_GetReplayStatus(PortReplayStatus *status) {
    status->is_playing = g_play.state == PLAY_ON;
    status->is_paused = g_play.paused;
    status->speed = g_play.speed;
    status->frame = g_play.frame;
    status->frames = g_play.frames;
    status->kill = g_play.kill;
    status->desync = g_play.desync;
}

/* ---- the boundary ---- */

void port_ReplayBoundary(void) {
    PortGameView view;

    if (g_play.state == PLAY_PENDING) {
        if (g_port_steps == 0 || g_port_rerun) {
            return;
        }
        port_ReplayBegin();
    }
    if (g_play.state == PLAY_ON) {
        port_ReplayPlayBoundary();
        return;
    }
    /* what the game reads of the picture, held for the step */
    port_RemasterHoldGameView(NULL);
    port_RemasterGameView(&view);
    port_RemasterHoldGameView(&view);
    port_ReplayRecordBoundary(
        g_port_input_count, g_sys_screen == SCREEN_FIGHT_UPDATE, &view);
}
