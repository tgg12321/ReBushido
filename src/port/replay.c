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
 *   CHSH  the console's own hash (port_StateConsoleHash) at the same
 *         frames
 *   LEAD  the run from power on to the fight's start, for a build that
 *         cannot put STRT in (below, "the lead-in"): its setup, each
 *         change of it, every input with its view and menu sound, the
 *         console's hash every PORT_REPLAY_HASH_EVERY inputs
 *   CARD  the memory cards as the run found them at power on, deflated
 *   RSYN  a resync online, one each: the fight's frame it was put in at,
 *         the frame before it whose hashes are held, and the state put
 *         in, as a spectator's start is served (docs/NETPLAY.md, 21)
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
#include "port_ranked.h"
#include "port_replay.h"
#include "port_spu.h"
#include "bb2.h"

/* 2: a frame may have the third and fourth pads (CHANGED_THIRD,
 * CHANGED_FOURTH, two on two's); 3: and whether a page of the window's
 * covers the top menu (CHANGED_COVER), and the chunks CHSH, LEAD and CARD.
 * A file of an earlier version is read as before. */
#define FORMAT_VERSION 3
#define FORMAT_VERSION_FIRST 1
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
/* The playback's keyframes, for a seek back (the theatre's, port_replay.h):
 * the state every so many frames as the playback passes them, so many at
 * most; when they are all used every other one is let go and the rest are
 * twice as far apart. */
#define KEYFRAMES 8
#define KEYFRAME_EVERY 150
/* an input's fields that changed (INPT) */
#define CHANGED_FIRST 1
#define CHANGED_SECOND 2
#define CHANGED_PRESENT 4
#define CHANGED_VIEW 8
#define CHANGED_THIRD 16
#define CHANGED_FOURTH 32
#define CHANGED_COVER 64
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
#define TAG_CONSOLE TAG('C', 'H', 'S', 'H')
#define TAG_LEAD TAG('L', 'E', 'A', 'D')
#define TAG_CARDS TAG('C', 'A', 'R', 'D')
#define TAG_DISC TAG('D', 'I', 'S', 'C')
/* the menu sounds of the window's pages over the fight's pause: a count,
 * then each frame's number and its sound (a file without it has none) */
#define TAG_SOUND TAG('S', 'O', 'N', 'D')
/* a resync online (docs/NETPLAY.md, section 21): the frame of the fight it
 * was put in at, the frame of the last hash before it every machine held
 * the same, then the state put in, as a spectator's start is (SERV, PIEC,
 * STRT, DISC, END); one chunk each, in their order */
#define TAG_RESYNC TAG('R', 'S', 'Y', 'N')
/* a spectators' start told as the difference from a state they have
 * (docs/NETPLAY.md, section 21): its frame, hash and the difference */
#define TAG_BASE TAG('B', 'A', 'S', 'E')
#define BASE_HEAD_WORDS 5
#define RESYNC_HEAD_BYTES (2 * WORD_BYTES)

static const char g_magic[MAGIC_BYTES] = {
    'R', 'B', 'R', 'E', 'P', 'L', 'A', 'Y'};

/* The names a file is given, by enum FightMode. */
static const char * const g_mode_words[] = {
    "story", "vscpu", "practice", "chanbara", "vs", "team", "network"};
#define MODE_WORDS ((s32)(sizeof(g_mode_words) / sizeof(g_mode_words[0])))

/* One frame of a fight: the two pads' buttons, which pads answered (a
 * bit each), the view it read (VIEW's number), the menu sound the
 * window asked the game for as it took the input (port_top_menu.h), plus
 * one, or 0: SOND; and whether a page of the window's covered the top
 * menu as it took it (port_top_menu.h, covered). */
typedef struct {
    u16 buttons[PORT_INPUT_PADS];
    u8 present;
    u8 view;
    u8 sound;
    u8 cover;
} ReplayFrame;

/* ---- the lead-in's ---- */

/* The inputs from power on a run keeps, at most: as many as a fight's. */
#define LEAD_MAX FRAMES_MAX
#define LEAD_HASHES (LEAD_MAX / PORT_REPLAY_HASH_EVERY + 2)
#define LEAD_VIEWS 64
#define LEAD_EVENTS 256
/* the movie player's reads of the pads whose word changed, at most */
#define LEAD_MOVIE_READS 4096
/* its word with no button held */
#define MOVIE_NO_BUTTONS 0xFFFFFFFFU
#define LEAD_VERSION 1
/* the most bytes a frame takes packed */
#define FRAME_PACKED_MOST 14
/* --replay-differ: how many inputs are changed */
#define DIFFER_INPUTS 30
/* a slice of a playback's run from power on, in the window, in
 * milliseconds: the window's events are read between two */
#define LEAD_SLICE_MS 40

/* What the run takes in from outside its state that is not an input
 * (the settings the game reads when it next asks: port_play.h,
 * port_stage_choice.h, port_fight_music.h, port_match.h), as a session
 * holds the host's (session.c, port_ApplySession). */
enum {
    SETUP_KIND,        /* the stage pages' kind of stage */
    SETUP_PLAY,        /* the gameplay switches (port_PlaySwitchBits) */
    SETUP_MUSIC,       /* the fight's music (port_FightMusicBits) */
    SETUP_TIME,        /* the time of day */
    SETUP_MATCH,       /* the match rule, its number and the teams */
    SETUP_STAGE_MUSIC, /* the mods' stages' own pieces (a sum): not held */
    SETUP_WORDS
};

/* LEAD's words before the events: its version, whose run it is (0 for
 * the run's own; LEAD_SESSION for a session's, whose SESSION_WORDS
 * follow the setup; LEAD_FIGHT for the run's own begun with a fight
 * chosen, --fight, whose FIGHT_WORDS follow it), how many inputs it
 * holds, the setup */
#define LEAD_HEAD_WORDS (3 + SETUP_WORDS)
#define LEAD_SESSION 1
#define LEAD_FIGHT 2

/* What the game reads of the session an online fight's lead-in was
 * played in (port_net.h, port_SessionGameNow), after the setup. */
enum { SESSION_RULES, SESSION_PADS, SESSION_SEED, SESSION_WORDS };

/* The fight a run was begun with (port_platform.h, PortFight; --fight,
 * --fight-2p, --fight-cpu, --partners), after the setup. */
enum {
    FIGHT_MODE,
    FIGHT_STAGE,
    FIGHT_ROSTER,
    FIGHT_WEAPON = FIGHT_ROSTER + 2,
    FIGHT_IS_CPU = FIGHT_WEAPON + 2,
    FIGHT_HAS_PARTNERS = FIGHT_IS_CPU + 2,
    FIGHT_PARTNER_ROSTER,
    FIGHT_PARTNER_WEAPON = FIGHT_PARTNER_ROSTER + 2,
    FIGHT_WORDS = FIGHT_PARTNER_WEAPON + 2
};

/* A word the movie player read of the pads that is not the one before:
 * read number `read` of the run's (pad.c, PAD_dr). */
typedef struct {
    u32 read;
    u32 word;
} ReplayMovieRead;

/* A change of the setup, at the boundary before input `count`. */
typedef struct {
    u32 count;
    u32 words[SETUP_WORDS];
} ReplaySetupChange;

typedef struct {
    char name[PIECE_NAME_BYTES];
    u32 bytes;
} ReplayPiece;

enum { REC_IDLE, REC_FIGHT, REC_ENDED, REC_WRITING };

/* what the writing of a fight's file is doing: done, where the start was
 * compressed already for a room's spectators (port_ReplayServe) */
enum { WORK_INDEX, WORK_SCAN, WORK_DEFLATE, WORK_DONE };

/* the start a match's spectators are given (port_ReplayServe) */
enum { SERVE_NONE, SERVE_PACKING, SERVE_READY };

#define TAG_SERVE TAG('S', 'E', 'R', 'V')
#define SERVE_HEAD_WORDS 4

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
    u32 start_frame; /* netplay's frame there, in a session */
    u32 end_count;   /* ... and at its first boundary out of it */
    u32 length;      /* the frames whose input was taken */
    u32 kill;        /* the first frame with a fighter cut down, or NONE */
    u32 hash_count;  /* the hashes noted, of the boundaries reached */
    s32 winner;
    u8 *snapshot;
    ReplayFrame *frames;
    u64 *hashes;
    u64 *consoles; /* the console's own hash at the same frames */
    u64 start_console;
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
    u32 lead_number; /* an online one's: its session's lead-in's number */
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
 * input's number, netplay's frame there and its view; and the last input
 * the game took. */
static struct {
    u8 *snapshot;
    u64 hash;
    u64 console;
    u32 count;
    u32 frame;
    PortGameView view;
    s32 is_kept;
} g_before;

/* The run from power on, kept for each fight's lead-in (below): the
 * run's own (g_lead), and the session's being played (g_session_lead),
 * from its own power on. */
typedef struct {
    s32 kept;      /* the state at power on is (port_ReplayKeepBoot) */
    s32 plain;     /* ... and the run began from power on as plain runs do */
    s32 started;   /* its first boundary was seen */
    u32 broken_at; /* the first input it does not hold, or NONE */
    char why[PORT_REPLAY_WHY_BYTES];
    u8 *boot;            /* the state at power on, this run's */
    ReplayFrame *frames; /* by the input's number */
    u32 length;
    u64 *hashes; /* the console's hash at each PORT_REPLAY_HASH_EVERY'th */
    PortGameView views[LEAD_VIEWS];
    s32 view_count;
    u32 setup[SETUP_WORDS]; /* at power on */
    u32 now[SETUP_WORDS];   /* at the last boundary */
    ReplaySetupChange events[LEAD_EVENTS];
    s32 event_count;
    ReplayMovieRead movie[LEAD_MOVIE_READS];
    s32 movie_count;
    u32 card_flags;   /* the slots that had a card at power on */
    u8 *cards;        /* their images then */
    u8 *packed_cards; /* one deflated */
    u32 packed_room;
    /* a session's: its rules, pads and seed; whether the boundaries seen
     * since netplay began are its; its number, one a session */
    s32 session;
    u32 session_words[SESSION_WORDS];
    s32 live;
    u32 number;
    /* the run's own begun with a fight chosen: the fight */
    s32 fight;
    u32 fight_words[FIGHT_WORDS];
} ReplayLead;

static ReplayLead g_lead = {.broken_at = NONE};
static ReplayLead g_session_lead = {.broken_at = NONE};

static ReplayLead *port_LeadOfFight(void);

/* The start of the fight a match's host gives its spectators: compressed
 * as soon as it is final, a slice at each boundary, into a replay's own
 * chunks (port_ReplayServe). */
static struct {
    s32 on;
    s32 state; /* SERVE_... */
    u32 count; /* the start's input */
    u32 frame; /* and netplay's frame */
    u64 hash;
    u8 *blob;
    u32 size;
    u32 room;
    u32 made; /* starts made, so that a new one is told from the last */
    /* the latest is a resync's state (port_ReplayResynced), not this */
    s32 resync;
} g_serve;

/* A resync (docs/NETPLAY.md, section 21): the state the host packed or a
 * guest put in, in a replay's chunks as a spectator's start is, and the
 * fight being recorded's resyncs, kept for its file (RSYN). */
#define RESYNCS_MAX 8

static struct {
    u8 *snapshot;
    u8 *packed;
    u32 packed_room;
    PortDeflate deflate;
    ReplayMatch *matches; /* the fight's start's runs, kept meanwhile */
    u8 *blob;
    u32 room;
    u32 size;
    u32 frame;
    /* the fight's: the input each was put in at, the last before it whose
     * hash every machine held the same, and its state */
    s32 count;
    u32 at[RESYNCS_MAX];
    u32 agreed[RESYNCS_MAX];
    u8 *kept[RESYNCS_MAX];
    u32 kept_size[RESYNCS_MAX];
    u32 kept_room[RESYNCS_MAX];
} g_resync;

/* The states a resync's state for the spectators may be told as the
 * difference from (docs/NETPLAY.md, section 21): the last few hashed
 * frames' (the host's as they became final, a spectator's as it ran
 * them), one of them held from where the machines parted to the resync;
 * and the host's difference, raw, deflated and in a replay's chunks.
 * A difference is runs of words: so many the same, then so many new and
 * their bytes; a run of the same shorter than BASE_SAME_LEAST words is
 * among the new. */
#define BASES_KEPT 4
#define BASE_SAME_LEAST 8
#define BASE_PAIR_MOST 10

static struct {
    u8 *state[BASES_KEPT];
    u32 frame[BASES_KEPT];
    u64 hash[BASES_KEPT];
    s32 had[BASES_KEPT];
    s32 held; /* the one kept for the resync to come, or -1 */
    u32 next;
    u8 *raw;
    u32 raw_room;
    u32 raw_size;
    u32 frame_of; /* the base the raw difference is from */
    u64 hash_of;
    u8 *packed;
    u32 packed_room;
    PortDeflate deflate;
    u8 *blob;
    u32 blob_size;
} g_base = {.held = -1};

static struct {
    u32 index;
    ReplayFrame frame;
} g_last = {NONE, {{0, 0, 0, 0}, 0, 0, 0, 0}};

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
    /* played from power on (the lead-in): another build's */
    s32 from_boot;
    s32 in_lead;    /* ... not at the fight's start yet */
    s32 same_build; /* the state's hashes are this build's to hold */
    s32 same_game;  /* one that differs is a desync, not another game */
    ReplayFrame *lead;
    u32 lead_count;
    u64 *lead_hashes;
    u32 lead_hash_count;
    PortGameView lead_views[LEAD_VIEWS];
    s32 lead_view_count;
    ReplaySetupChange events[LEAD_EVENTS];
    s32 event_count;
    s32 event_next;
    u32 setup[SETUP_WORDS];
    u32 card_flags;
    u8 *cards;
    u64 *consoles; /* the fight's console hashes */
    u32 console_count;
    u64 start_console;
    u32 differ_at;   /* --replay-differ: the input changed, or NONE */
    s32 at_power_on; /* the next boundary is power on's, before any step */
    ReplayMovieRead movie[LEAD_MOVIE_READS];
    s32 movie_count;
    /* the lead-in is a session's: what the game read of it */
    s32 session;
    u32 session_words[SESSION_WORDS];
    /* ... or its run was begun with a fight chosen: the fight */
    s32 fight;
    u32 fight_words[FIGHT_WORDS];
    /* the fight's resyncs (RSYN): the frame each is put in at, the frame
     * after which its hashes before it are not held, its state's chunks;
     * and room to put one in */
    s32 resync_count;
    u32 resync_at[RESYNCS_MAX];
    u32 resync_agreed[RESYNCS_MAX];
    const u8 *resync_blob[RESYNCS_MAX];
    u32 resync_size[RESYNCS_MAX];
    u8 *resync_raw;
    u8 *resync_state;
    /* the keyframes: the fight's frame each is of, sorted, and its state */
    s32 keyframe_count;
    u32 keyframe_every;
    u32 keyframe_at[KEYFRAMES];
    u8 *keyframe_state[KEYFRAMES];
} g_play;

/* --replay-from-boot and --replay-differ (port_replay.h) */
static s32 g_from_boot_asked;
static u32 g_differ_asked = NONE;

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

/* The build: where the game's object's core lies and what its code is
 * (port_StateBuildSum), and the state's size but for the primitive
 * buffers. Not the version: a build whose window alone differs plays
 * this one's replays, the next release's too where its game is this
 * one's. A program with no window has no sum of its code, and its
 * version (with its commit) stands for it. */
static u64 port_ReplayBuild(void) {
    s32 has_prims;
    u64 layout = port_StateBuildSum();
    u64 sum = FNV_OFFSET;

    if (g_port_code_sum == 0) {
        sum = port_SumBytes(sum, g_rb_version, port_TextLength(g_rb_version));
    }
    sum = port_SumWord(sum, (u32)layout);
    sum = port_SumWord(sum, (u32)(layout >> 32));
    return port_SumWord(sum, port_ReplayStateSize(&has_prims));
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

/* A session's run from power on is not whole from here. */
static void port_SessionLeadBreak(const char *why) {
    ReplayLead *lead = &g_session_lead;
    u32 i;

    if (!lead->started || lead->broken_at != NONE) {
        return;
    }
    for (i = 0; why[i] != 0 && i + 1 < sizeof(lead->why); i++) {
        lead->why[i] = why[i];
    }
    lead->why[i] = 0;
    lead->broken_at = g_port_input_count;
    port_Log("replay: from input %u of the session on, its fights' replays "
             "play only on a build of this one's core: %s\n",
             lead->broken_at, why);
}

/* The run from power on is not whole from here (port_replay.h). */
void port_ReplayLeadBreak(const char *why) {
    u32 i;

    if (port_IsNetplayOn()) {
        port_SessionLeadBreak(why);
    }
    if (g_lead.broken_at != NONE || g_lead.why[0] != 0) {
        return;
    }
    for (i = 0; why[i] != 0 && i + 1 < sizeof(g_lead.why); i++) {
        g_lead.why[i] = why[i];
    }
    g_lead.why[i] = 0;
    if (!g_lead.kept) {
        /* before the state at power on is kept: an option of the command
         * line's, and the run is not a plain one */
        g_lead.plain = FALSE;
        return;
    }
    g_lead.broken_at = g_port_input_count;
    port_Log("replay: from input %u on, a fight's replay plays only on a "
             "build of this one's core: %s\n",
             g_lead.broken_at, why);
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
    g_rec.consoles = port_HostAllocate(HASHES_MAX * sizeof(u64));
    g_rec.packed_room = port_DeflateBound(size);
    g_rec.packed = port_HostAllocate(g_rec.packed_room);
    g_dedupe.data = port_HostAllocate(SECTOR_LOG * PORT_DISC_DATA_BYTES);
    g_dedupe.sectors = port_HostAllocate(SECTOR_LOG * sizeof(u32));
    g_dedupe.index = port_HostAllocate(INDEX_SIZE * sizeof(u32));
    g_dedupe.slot_of = port_HostAllocate((SECTOR_SLOT_MASK + 1) * sizeof(u32));
    g_dedupe.matches = port_HostAllocate(MATCHES_MAX * sizeof(ReplayMatch));
    if (g_rec.snapshot == NULL || g_before.snapshot == NULL ||
        g_rec.frames == NULL || g_rec.hashes == NULL ||
        g_rec.consoles == NULL || g_rec.packed == NULL ||
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

/* Whether fights are written to files: the folder set and recording on. */
static s32 port_ReplayWrites(void) {
    return g_recording_on && g_folder[0] != 0;
}

/* Whether fights are recorded: written, or their starts given to a
 * room's spectators (port_ReplayServe). */
static s32 port_ReplayMayRecord(void) {
    /* not training's fights: their moments put back and the dummy's
     * recordings are not the replay's (port_training.h) */
    return (port_ReplayWrites() || g_serve.on) && !port_LinkIsUp() &&
           g_play.state == PLAY_OFF && !port_TrainingIsOn();
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

/* `count` frames as INPT has them, into `out`: its length. */
static u32 port_ReplayPackFrames(
    u8 *out, const ReplayFrame *frames, u32 count) {
    ReplayFrame last = {{0, 0, 0, 0}, 0, 0, 0, 0};
    u32 at = 0;
    u32 frame = 0;

    while (frame < count) {
        const ReplayFrame *now = &frames[frame];
        u32 run = 1;
        u8 changed = 0;

        while (frame + run < count &&
               frames[frame + run].buttons[0] == now->buttons[0] &&
               frames[frame + run].buttons[1] == now->buttons[1] &&
               frames[frame + run].buttons[2] == now->buttons[2] &&
               frames[frame + run].buttons[3] == now->buttons[3] &&
               frames[frame + run].present == now->present &&
               frames[frame + run].view == now->view &&
               frames[frame + run].cover == now->cover) {
            run++;
        }
        changed |= now->buttons[0] != last.buttons[0] ? CHANGED_FIRST : 0;
        changed |= now->buttons[1] != last.buttons[1] ? CHANGED_SECOND : 0;
        changed |= now->present != last.present ? CHANGED_PRESENT : 0;
        changed |= now->view != last.view ? CHANGED_VIEW : 0;
        changed |= now->buttons[2] != last.buttons[2] ? CHANGED_THIRD : 0;
        changed |= now->buttons[3] != last.buttons[3] ? CHANGED_FOURTH : 0;
        changed |= now->cover != last.cover ? CHANGED_COVER : 0;
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
        if (changed & CHANGED_THIRD) {
            out[at++] = (u8)now->buttons[2];
            out[at++] = (u8)(now->buttons[2] >> BITS_PER_BYTE);
        }
        if (changed & CHANGED_FOURTH) {
            out[at++] = (u8)now->buttons[3];
            out[at++] = (u8)(now->buttons[3] >> BITS_PER_BYTE);
        }
        if (changed & CHANGED_COVER) {
            out[at++] = now->cover;
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
static s32 port_LeadCovers(void);

/* META into `out`: its length. */
static u32 port_ReplayMeta(char *out, u32 room) {
    char build[HEX_DIGITS + 1];
    char disc[HEX_DIGITS + 1];
    char mods[HEX_DIGITS + 1];
    char start[HEX_DIGITS + 1];
    char code[HEX_DIGITS + 1];
    char console[HEX_DIGITS + 1];
    s32 has_prims;
    s32 lead = port_LeadCovers();
    u32 length;

    port_ReplayStateSize(&has_prims);
    port_PutHex(code, g_port_code_sum);
    port_PutHex(console, g_rec.start_console);
    port_PutHex(build, port_ReplayBuild());
    port_PutHex(disc, port_SessionDiscSum());
    port_PutHex(mods, port_ReplayMods());
    port_PutHex(start, g_rec.start_hash);
    length = port_ReplayText(
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
    length += port_ReplayText(
        out + length, room - length,
        "code %s\nconsole %s\nstage_music %u\nlead %d\nresyncs %d\n", code,
        console, lead ? port_LeadOfFight()->setup[SETUP_STAGE_MUSIC] : 0, lead,
        g_resync.count);
    if (!lead && g_rec.online != 0) {
        const ReplayLead *of = port_LeadOfFight();

        length += port_ReplayText(
            out + length, room - length, "lead_why %s\n",
            g_rec.online == PORT_SESSION_WATCH
                ? "a spectator's: it came in at the fight's start"
            : !of->started || of->number != g_rec.lead_number
                ? "its session kept nothing from power on"
            : of->why[0] != 0 ? of->why
                              : "its session's run from power on is not whole");
    } else if (!lead) {
        length += port_ReplayText(
            out + length, room - length, "lead_why %s\n",
            !g_lead.kept         ? "the run kept nothing from power on"
            : g_lead.why[0] != 0 ? g_lead.why
                                 : "the run from power on is not whole");
    }
    return length;
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

static void port_ReplayServeBuild(void);

/* ---- the lead-in ----
 *
 * A fight's start (STRT) is put in only by a build of the same core
 * (port_StateBuildSum): its stack's frames and its pointers are that
 * build's. So a replay also keeps what plays the fight again from power
 * on, on any build of the game: the run's setup as it began (its memory
 * cards, the switches and defaults the game takes in), every input from
 * power on to the fight's start with the view it read, the menu sound
 * asked and the top menu's cover, each change of the setup on the way,
 * and the console's own hash (port_StateConsoleHash) every
 * PORT_REPLAY_HASH_EVERY inputs, the fight's frames' too (CHSH). Another
 * build puts its own state at power on back, the setup in, and runs the
 * game with those inputs to the fight's start, every console hash held
 * to the one recorded; then the fight as it is played from its start.
 * Where its game is the one that recorded it a hash that differs is a
 * desync; where it is another, the game went otherwise there, and the
 * playback stops and says so.
 *
 * A run keeps it from its first step while it is whole: it began from
 * power on as a plain run does (no session started with the program, no
 * link cable, no mod's arena or weapon asked; a fight chosen, --fight,
 * is kept with the setup and started again the same way), and nothing
 * but its inputs and that setup
 * changed the game (a play test's poke and the settings menu's Bug fixes
 * end it: port_ReplayLeadBreak). A session begun in the program, a
 * spectator's match and a replay's playback put the player's own state
 * back after them, and the lead-in waits while they run. */

/* What the run takes in now of its setup. */
static void port_ReplaySetupWords(u32 *words) {
    words[SETUP_KIND] = (u32)port_GetStageKindDefault();
    words[SETUP_PLAY] = port_PlaySwitchBits();
    words[SETUP_MUSIC] = port_FightMusicBits();
    words[SETUP_TIME] = (u32)port_GetStageTimeDefault();
    words[SETUP_MATCH] = port_MatchBits();
    words[SETUP_STAGE_MUSIC] = port_MusicStageSum();
}

/* The fight the run was begun with into LEAD's words: FALSE for none. */
static s32 port_ReplayFightWords(u32 *words) {
    PortFight fight;
    s32 side;

    if (!port_FightAsked(&fight)) {
        return FALSE;
    }
    words[FIGHT_MODE] = (u32)fight.mode;
    words[FIGHT_STAGE] = (u32)fight.stage;
    words[FIGHT_HAS_PARTNERS] = (u32)fight.has_partners;
    for (side = 0; side < 2; side++) {
        words[FIGHT_ROSTER + side] = (u32)fight.roster[side];
        words[FIGHT_WEAPON + side] = (u32)fight.weapon[side];
        words[FIGHT_IS_CPU + side] = (u32)fight.is_cpu[side];
        words[FIGHT_PARTNER_ROSTER + side] = (u32)fight.partner_roster[side];
        words[FIGHT_PARTNER_WEAPON + side] = (u32)fight.partner_weapon[side];
    }
    return TRUE;
}

/* ... and out of them, held for a playback from power on (NULL words:
 * none, this run's own fight not started either). */
static void port_ReplayHoldFight(const u32 *words) {
    PortFight fight;
    s32 side;

    if (words == NULL) {
        port_HoldFight(NULL);
        return;
    }
    fight.mode = (s32)words[FIGHT_MODE];
    fight.stage = (s32)words[FIGHT_STAGE];
    fight.has_partners = (s32)words[FIGHT_HAS_PARTNERS];
    for (side = 0; side < 2; side++) {
        fight.roster[side] = (s32)words[FIGHT_ROSTER + side];
        fight.weapon[side] = (s32)words[FIGHT_WEAPON + side];
        fight.is_cpu[side] = (s32)words[FIGHT_IS_CPU + side];
        fight.partner_roster[side] = (s32)words[FIGHT_PARTNER_ROSTER + side];
        fight.partner_weapon[side] = (s32)words[FIGHT_PARTNER_WEAPON + side];
    }
    port_HoldFight(&fight);
}

/* The setup held, as a session holds the host's. */
static void port_ReplayHoldSetup(const u32 *words) {
    port_HoldStageKindDefault((s32)words[SETUP_KIND]);
    port_HoldPlaySwitches(words[SETUP_PLAY]);
    port_HoldFightMusicExactly(words[SETUP_MUSIC]);
    port_HoldStageTimeDefault((s32)words[SETUP_TIME]);
    port_HoldMatch(words[SETUP_MATCH]);
}

/* The state at power on, before the first step (port_replay.h). */
void port_ReplayKeepBoot(void) {
    u32 size = port_StateSize();

    if (g_lead.kept) {
        return;
    }
    if (port_IsNetplayOn() || port_LinkIsUp()) {
        port_ReplayLeadBreak("the program began a session or a link");
    }
    if (g_lead.why[0] == 0) {
        g_lead.plain = TRUE;
    }
    g_lead.boot = port_HostAllocate(size);
    g_lead.frames = port_HostAllocate(LEAD_MAX * sizeof(ReplayFrame));
    g_lead.hashes = port_HostAllocate(LEAD_HASHES * sizeof(u64));
    g_lead.cards = port_HostAllocate(PORT_CARD_SLOTS * PORT_CARD_IMAGE_SIZE);
    g_lead.packed_room = port_DeflateBound(PORT_CARD_IMAGE_SIZE);
    g_lead.packed_cards = port_HostAllocate(g_lead.packed_room);
    if (g_lead.boot == NULL || g_lead.frames == NULL || g_lead.hashes == NULL ||
        g_lead.cards == NULL || g_lead.packed_cards == NULL) {
        port_Log("replay: no memory for the run from power on: replays play "
                 "only on a build of this one's core\n");
        g_lead.boot = NULL;
        return;
    }
    port_StateSave(g_lead.boot);
    g_lead.kept = TRUE;
    if (!g_lead.plain) {
        g_lead.broken_at = 0;
    }
}

/* Whether the lead-in is being kept: TRUE where this boundary and this
 * input are the run's own, from power on. */
static s32 port_LeadIsOn(void) {
    return g_lead.kept && g_lead.broken_at == NONE && g_lead.started &&
           g_play.state == PLAY_OFF && !port_IsNetplayOn() && !g_port_ahead &&
           !g_port_rerun;
}

/* The lead-in this input is kept in: the run's own, or the session's,
 * whose frames run again by a rollback write it again, as the fight's
 * do; NULL for none. */
static ReplayLead *port_LeadNow(void) {
    if (port_LeadIsOn()) {
        return &g_lead;
    }
    if (g_session_lead.started && g_session_lead.live &&
        g_session_lead.broken_at == NONE && g_play.state == PLAY_OFF &&
        port_IsNetplayOn() && !g_port_ahead) {
        return &g_session_lead;
    }
    return NULL;
}

/* The number of a view among the lead-in's, a new one kept: -1 where
 * there is no room for it. */
static s32 port_LeadViewNumber(ReplayLead *lead, const PortGameView *view) {
    s32 i;

    for (i = 0; i < lead->view_count; i++) {
        if (lead->views[i].distance == view->distance &&
            lead->views[i].margin_w == view->margin_w &&
            lead->views[i].margin_h == view->margin_h) {
            return i;
        }
    }
    if (lead->view_count == LEAD_VIEWS) {
        return -1;
    }
    lead->views[lead->view_count] = *view;
    return lead->view_count++;
}

/* A boundary of a lead-in under way, before input `count`: a change of
 * the setup, the view, the console's hash. A session's is seen again
 * where a rollback runs its frames again: what it kept from `count` on
 * is that run's again. */
static void port_LeadKeep(ReplayLead *lead, u32 count, const PortGameView *view,
                          void (*broken)(const char *why)) {
    u32 words[SETUP_WORDS];
    s32 number;
    s32 i;

    if (count >= LEAD_MAX) {
        broken("the run is longer than a replay keeps of it");
        return;
    }
    while (lead->event_count > 0 &&
           lead->events[lead->event_count - 1].count > count) {
        lead->event_count--;
    }
    for (i = 0; i < SETUP_WORDS; i++) {
        lead->now[i] = lead->event_count > 0
                           ? lead->events[lead->event_count - 1].words[i]
                           : lead->setup[i];
    }
    port_ReplaySetupWords(words);
    for (i = 0; i < SETUP_WORDS && words[i] == lead->now[i]; i++) {
    }
    if (i < SETUP_WORDS) {
        ReplaySetupChange *event;

        if (lead->event_count > 0 &&
            lead->events[lead->event_count - 1].count == count) {
            event = &lead->events[lead->event_count - 1];
        } else if (lead->event_count == LEAD_EVENTS) {
            broken("its settings changed too often");
            return;
        } else {
            event = &lead->events[lead->event_count++];
        }
        event->count = count;
        for (i = 0; i < SETUP_WORDS; i++) {
            event->words[i] = lead->now[i] = words[i];
        }
    }
    number = port_LeadViewNumber(lead, view);
    if (number < 0) {
        broken("its picture changed too often");
        return;
    }
    lead->frames[count].view = (u8)number;
    if (count % PORT_REPLAY_HASH_EVERY == 0) {
        lead->hashes[count / PORT_REPLAY_HASH_EVERY] = port_StateConsoleHash();
    }
}

/* What a lead-in keeps from its power on: the setup, the cards, the first
 * view. */
static void port_LeadBegin(ReplayLead *lead, const PortGameView *view) {
    s32 number;
    s32 slot;
    s32 i;

    lead->started = TRUE;
    port_ReplaySetupWords(lead->setup);
    for (i = 0; i < SETUP_WORDS; i++) {
        lead->now[i] = lead->setup[i];
    }
    lead->card_flags = 0;
    for (slot = 0; slot < PORT_CARD_SLOTS; slot++) {
        if (port_ReadCardFile(
                slot, lead->cards + slot * PORT_CARD_IMAGE_SIZE)) {
            lead->card_flags |= 1U << slot;
        }
    }
    /* power on is a boundary of its own, before the first step; the
     * next is the one before input 0, the game at its first read of
     * the pads, whose hash is the first */
    number = port_LeadViewNumber(lead, view);
    lead->frames[0].view = (u8)(number < 0 ? 0 : number);
}

/* The memory of a session's lead-in, given once: FALSE where there is
 * none. */
static s32 port_SessionLeadRoom(ReplayLead *lead) {
    if (lead->frames != NULL) {
        return TRUE;
    }
    lead->frames = port_HostAllocate(LEAD_MAX * sizeof(ReplayFrame));
    lead->hashes = port_HostAllocate(LEAD_HASHES * sizeof(u64));
    lead->cards = port_HostAllocate(PORT_CARD_SLOTS * PORT_CARD_IMAGE_SIZE);
    lead->packed_room = port_DeflateBound(PORT_CARD_IMAGE_SIZE);
    lead->packed_cards = port_HostAllocate(lead->packed_room);
    if (lead->frames == NULL || lead->hashes == NULL || lead->cards == NULL ||
        lead->packed_cards == NULL) {
        lead->frames = NULL;
        return FALSE;
    }
    return TRUE;
}

/* A boundary of a session (netplay on): at the session's power on, its
 * lead-in begun (a player's, not a spectator's, who comes in at a fight's
 * start), with what the game reads of the session; after it, kept, also
 * where a rollback runs its frames again. A session that did not begin
 * here at its power on keeps none. */
static void port_SessionLeadBoundary(u32 count, const PortGameView *view) {
    ReplayLead *lead = &g_session_lead;
    u32 *words = lead->session_words;

    if (g_play.state != PLAY_OFF || g_port_ahead) {
        return;
    }
    if (count == 0 && port_NetplayFrameNow() == 0) {
        lead->live = TRUE;
        lead->started = FALSE;
        lead->broken_at = NONE;
        lead->why[0] = 0;
        lead->length = 0;
        lead->view_count = 0;
        lead->event_count = 0;
        lead->movie_count = 0;
        lead->session = TRUE;
        lead->number++;
        if (!port_SessionGameNow(&words[SESSION_RULES], &words[SESSION_PADS],
                                 &words[SESSION_SEED]) ||
            !port_SessionLeadRoom(lead)) {
            return;
        }
        port_LeadBegin(lead, view);
        return;
    }
    if (!lead->live) {
        lead->live = TRUE;
        lead->started = FALSE;
        return;
    }
    if (lead->started && lead->broken_at == NONE) {
        port_LeadKeep(lead, count, view, port_SessionLeadBreak);
    }
}

/* The lead-in's turn at a boundary, before the step: the setup at power
 * on and each change of it, the view, and the console's hash; in a
 * session, the session's. */
static void port_LeadBoundary(u32 count, const PortGameView *view) {
    if (port_IsNetplayOn()) {
        port_SessionLeadBoundary(count, view);
        return;
    }
    g_session_lead.live = FALSE;
    if (!g_lead.kept || g_lead.broken_at != NONE || g_play.state != PLAY_OFF ||
        g_port_rerun) {
        return;
    }
    if (port_LinkIsUp()) {
        port_ReplayLeadBreak(
            "a link cable's game: the other console is not an input");
        return;
    }
    if (!g_lead.started) {
        if (count != 0 || g_port_steps != 0) {
            port_ReplayLeadBreak("the run did not begin at power on");
            return;
        }
        /* begun with a fight chosen (--fight): the fight, which a
         * playback from power on starts as this run did */
        g_lead.fight = port_ReplayFightWords(g_lead.fight_words);
        port_LeadBegin(&g_lead, view);
        return;
    }
    port_LeadKeep(&g_lead, count, view, port_ReplayLeadBreak);
}

/* The lead-in of the fight being written: the run's own, or for an
 * online one its session's. */
static ReplayLead *port_LeadOfFight(void) {
    return g_rec.online != 0 ? &g_session_lead : &g_lead;
}

/* Whether the fight being written has its lead-in: the run (the
 * session's, for an online one) was whole from power on to the fight's
 * end. */
static s32 port_LeadCovers(void) {
    const ReplayLead *lead = port_LeadOfFight();

    if (g_rec.online != 0) {
        return lead->started && lead->number == g_rec.lead_number &&
               (lead->broken_at == NONE || lead->broken_at > g_rec.end_count) &&
               g_rec.start_count <= lead->length;
    }
    return lead->kept && lead->plain && lead->started &&
           (lead->broken_at == NONE || lead->broken_at > g_rec.end_count) &&
           g_rec.start_count <= lead->length;
}

/* LEAD into `out`: its length, or 0 where it has no room. */
static u32 port_LeadPack(u8 *out, u32 room) {
    const ReplayLead *lead = port_LeadOfFight();
    u32 frames = g_rec.start_count;
    u32 hashes = (frames + PORT_REPLAY_HASH_EVERY - 1) / PORT_REPLAY_HASH_EVERY;
    u32 events = 0;
    u32 sounds = 0;
    u32 need;
    u32 at = 0;
    u32 i;

    for (i = 0; i < (u32)lead->event_count; i++) {
        events += lead->events[i].count <= frames;
    }
    for (i = 0; i < frames; i++) {
        sounds += lead->frames[i].sound != 0;
    }
    need = (LEAD_HEAD_WORDS + SESSION_WORDS + FIGHT_WORDS + 1 +
            events * (1 + SETUP_WORDS) + 1 + (u32)lead->view_count * 3 + 2 + 1 +
            sounds * 2 + 1 + (u32)lead->movie_count * 2) *
               WORD_BYTES +
           hashes * LONG_BYTES + frames * FRAME_PACKED_MOST;
    if (need > room) {
        port_Log("replay: the run from power on is too long to keep\n");
        return 0;
    }
    port_PutWord(out, LEAD_VERSION);
    port_PutWord(out + WORD_BYTES, lead->session ? LEAD_SESSION
                                   : lead->fight ? LEAD_FIGHT
                                                 : 0);
    port_PutWord(out + 2 * WORD_BYTES, frames);
    at = 3 * WORD_BYTES;
    for (i = 0; i < SETUP_WORDS; i++, at += WORD_BYTES) {
        port_PutWord(out + at, lead->setup[i]);
    }
    for (i = 0; lead->session && i < SESSION_WORDS; i++, at += WORD_BYTES) {
        port_PutWord(out + at, lead->session_words[i]);
    }
    for (i = 0; !lead->session && lead->fight && i < FIGHT_WORDS; i++,
        at += WORD_BYTES) {
        port_PutWord(out + at, lead->fight_words[i]);
    }
    port_PutWord(out + at, events);
    at += WORD_BYTES;
    for (i = 0; i < (u32)lead->event_count; i++) {
        u32 k;

        if (lead->events[i].count > frames) {
            continue;
        }
        port_PutWord(out + at, lead->events[i].count);
        at += WORD_BYTES;
        for (k = 0; k < SETUP_WORDS; k++, at += WORD_BYTES) {
            port_PutWord(out + at, lead->events[i].words[k]);
        }
    }
    port_PutWord(out + at, (u32)lead->view_count);
    at += WORD_BYTES;
    for (i = 0; i < (u32)lead->view_count; i++) {
        port_PutWord(out + at, (u32)lead->views[i].distance);
        port_PutWord(out + at + WORD_BYTES, (u32)lead->views[i].margin_w);
        port_PutWord(out + at + 2 * WORD_BYTES, (u32)lead->views[i].margin_h);
        at += 3 * WORD_BYTES;
    }
    port_PutWord(out + at, hashes);
    port_PutWord(out + at + WORD_BYTES, PORT_REPLAY_HASH_EVERY);
    at += 2 * WORD_BYTES;
    for (i = 0; i < hashes; i++, at += LONG_BYTES) {
        port_PutWord(out + at, (u32)lead->hashes[i]);
        port_PutWord(
            out + at + WORD_BYTES, (u32)(lead->hashes[i] >> WORD_BITS));
    }
    port_PutWord(out + at, sounds);
    at += WORD_BYTES;
    for (i = 0; i < frames; i++) {
        if (lead->frames[i].sound != 0) {
            port_PutWord(out + at, i);
            port_PutWord(out + at + WORD_BYTES, lead->frames[i].sound);
            at += 2 * WORD_BYTES;
        }
    }
    /* the movie player's reads (all of them the run's: those of the
     * fight's frames are none, a fight has no movie) */
    port_PutWord(out + at, (u32)lead->movie_count);
    at += WORD_BYTES;
    for (i = 0; i < (u32)lead->movie_count; i++) {
        port_PutWord(out + at, lead->movie[i].read);
        port_PutWord(out + at + WORD_BYTES, lead->movie[i].word);
        at += 2 * WORD_BYTES;
    }
    return at + port_ReplayPackFrames(out + at, lead->frames, frames);
}

/* CARD into `out`: the slots that had a card, and each one's image
 * deflated; its length, or 0. */
static u32 port_LeadPackCards(u8 *out, u32 room) {
    const ReplayLead *lead = port_LeadOfFight();
    u32 at = WORD_BYTES;
    s32 slot;

    port_PutWord(out, lead->card_flags);
    for (slot = 0; slot < PORT_CARD_SLOTS; slot++) {
        PortDeflate z;

        if (((lead->card_flags >> slot) & 1) == 0) {
            continue;
        }
        port_DeflateBegin(
            &z, lead->cards + slot * PORT_CARD_IMAGE_SIZE, PORT_CARD_IMAGE_SIZE,
            lead->packed_cards, lead->packed_room);
        while (!port_DeflateSome(&z, FILE_MAX)) {
        }
        if (z.failed || at + 2 * WORD_BYTES + z.out_at > room) {
            return 0;
        }
        port_PutWord(out + at, PORT_CARD_IMAGE_SIZE);
        port_PutWord(out + at + WORD_BYTES, z.out_at);
        port_CopyBytes(out + at + 2 * WORD_BYTES, lead->packed_cards, z.out_at);
        at += 2 * WORD_BYTES + z.out_at;
    }
    return at;
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

    /* the spectators' copy of the start first: the inputs are put where
     * the compressed start is */
    port_ReplayServeBuild();
    g_rec.state = REC_IDLE;
    if (g_rec.deflate.failed) {
        port_Log("replay: the fight's start would not compress\n");
        return;
    }
    if (!port_ReplayWrites()) {
        /* recorded for the spectators alone */
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
    at = WORD_BYTES +
         port_ReplayPackFrames(inputs + WORD_BYTES, g_rec.frames, g_rec.length);
    ok &= port_AppendChunk(file, TAG_INPUT, inputs, at);
    /* the menu sounds, where the window's pages played any */
    at = WORD_BYTES;
    for (i = 0; i < (s32)g_rec.length; i++) {
        if (g_rec.frames[i].sound != 0) {
            port_PutWord(inputs + at, (u32)i);
            port_PutWord(inputs + at + WORD_BYTES, g_rec.frames[i].sound);
            at += 2 * WORD_BYTES;
        }
    }
    port_PutWord(inputs, (at - WORD_BYTES) / (2 * WORD_BYTES));
    if (at > WORD_BYTES) {
        ok &= port_AppendChunk(file, TAG_SOUND, inputs, at);
    }
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
    port_PutWord(inputs, hash_count);
    port_PutWord(inputs + WORD_BYTES, PORT_REPLAY_HASH_EVERY);
    at = 2 * WORD_BYTES;
    for (i = 0; i < (s32)hash_count; i++) {
        port_PutWord(inputs + at, (u32)g_rec.consoles[i]);
        port_PutWord(
            inputs + at + WORD_BYTES, (u32)(g_rec.consoles[i] >> WORD_BITS));
        at += LONG_BYTES;
    }
    ok &= port_AppendChunk(file, TAG_CONSOLE, inputs, at);
    if (port_LeadCovers()) {
        at = port_LeadPack(inputs, g_rec.packed_room);
        ok &= at != 0 && port_AppendChunk(file, TAG_LEAD, inputs, at);
        at = port_LeadPackCards(inputs, g_rec.packed_room);
        ok &= at != 0 && port_AppendChunk(file, TAG_CARDS, inputs, at);
    }
    for (i = 0; i < g_resync.count; i++) {
        u8 chunk[CHUNK_HEAD_BYTES + RESYNC_HEAD_BYTES];

        port_PutWord(chunk, TAG_RESYNC);
        port_PutWord(
            chunk + WORD_BYTES, RESYNC_HEAD_BYTES + g_resync.kept_size[i]);
        port_PutWord(chunk + CHUNK_HEAD_BYTES, g_resync.at[i]);
        port_PutWord(chunk + CHUNK_HEAD_BYTES + WORD_BYTES, g_resync.agreed[i]);
        ok &= port_HostAppend(file, chunk, sizeof(chunk));
        ok &= port_HostAppend(file, g_resync.kept[i], g_resync.kept_size[i]);
    }
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
    /* a ranked match's fight: kept for the lobby, if it asks (a dispute) */
    port_RankedReplayWritten(path);
    /* ... and the player's record's fight (port_stats.h) */
    port_StatsReplayWritten(path, g_rec.end_count);
    port_ReplayPrune(port_ReplayFileSize(path));
}

/* The fight's end is final: its start is compressed, all at once or a
 * slice at each boundary from here, and the file written after. */
static void port_ReplayFinish(void) {
    s32 i;

    g_rec.state = REC_WRITING;
    if (g_serve.state != SERVE_NONE && g_serve.count == g_rec.start_count) {
        /* the start compressed for the spectators already, or being */
        return;
    }
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

    if (g_dedupe.work == WORK_DONE) {
        return TRUE;
    }
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
    if (!port_DeflateSome(&g_rec.deflate, budget)) {
        return FALSE;
    }
    /* the stream is whole: asked again, it is not compressed again */
    g_dedupe.work = WORK_DONE;
    return TRUE;
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
    if (g_serve.state == SERVE_PACKING) {
        /* ... or for the spectators: the snapshot is about to be another */
        while (!port_ReplayWork(FILE_MAX)) {
        }
        port_ReplayServeBuild();
    }
    if (!port_ReplayRoom()) {
        return;
    }
    g_rec.state = REC_FIGHT;
    g_rec.unfinished = FALSE;
    g_resync.count = 0;
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
        g_rec.start_frame = g_before.frame;
        g_rec.start_hash = g_before.hash;
        g_rec.start_console = g_before.console;
        g_rec.hashes[0] = g_before.hash;
        g_rec.consoles[0] = g_before.console;
        g_rec.hash_count = 1;
        g_rec.frames[0] = g_last.frame;
        g_rec.frames[0].view = (u8)port_ReplayViewNumber(&g_before.view);
        g_rec.length = 1;
    } else {
        /* no boundary before it (a run that starts in a fight) */
        port_StateSave(g_rec.snapshot);
        g_rec.start_count = count;
        g_rec.start_frame = port_IsNetplayOn() ? port_NetplayFrameNow() : 0;
        g_rec.start_hash = port_StateHash();
        g_rec.start_console = port_StateConsoleHash();
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
    g_rec.lead_number = g_session_lead.number;
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
        g_rec.consoles[index / PORT_REPLAY_HASH_EVERY] =
            port_StateConsoleHash();
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

/* Which pads answered, a bit each, and above them which the CPU plays
 * (PORT_PAD_CPU: a player who left an online fight of four). */
#define PRESENT_CPU_SHIFT PORT_INPUT_PADS

static u8 port_ReplayPresent(const PortInput *input) {
    u32 bits = 0;
    s32 pad;

    for (pad = 0; pad < PORT_INPUT_PADS; pad++) {
        bits |= (u32)(input->present[pad] != 0) << pad;
        bits |= (u32)(input->present[pad] == PORT_PAD_CPU)
                << (pad + PRESENT_CPU_SHIFT);
    }
    return (u8)bits;
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
    g_last.frame.buttons[2] = input->buttons[2];
    g_last.frame.buttons[3] = input->buttons[3];
    g_last.frame.present = port_ReplayPresent(input);
    g_last.frame.cover = g_port_top_menu.covered;
    if (port_LeadNow() != NULL && index < LEAD_MAX) {
        ReplayLead *of = port_LeadNow();
        ReplayFrame *lead = &of->frames[index];

        lead->buttons[0] = input->buttons[0];
        lead->buttons[1] = input->buttons[1];
        lead->buttons[2] = input->buttons[2];
        lead->buttons[3] = input->buttons[3];
        lead->present = g_last.frame.present;
        lead->cover = g_last.frame.cover;
        lead->sound = 0;
        if (index + 1 > of->length) {
            of->length = index + 1;
        }
    }
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
    frame->buttons[2] = input->buttons[2];
    frame->buttons[3] = input->buttons[3];
    frame->present = port_ReplayPresent(input);
    frame->cover = g_last.frame.cover;
    frame->sound = 0;
    g_rec.length = at + 1;
}

/* The movie player's read of the pads kept (port_replay.h). */
void port_ReplayTookMovieRead(u32 read, u32 word) {
    ReplayLead *lead = port_LeadNow();
    u32 last;

    if (lead == NULL) {
        return;
    }
    /* a read taken back (rollback's run again of a movie is none of a
     * plain run's): the reads after it are its run's again */
    while (lead->movie_count > 0 &&
           lead->movie[lead->movie_count - 1].read >= read) {
        lead->movie_count--;
    }
    last = lead->movie_count > 0 ? lead->movie[lead->movie_count - 1].word
                                 : MOVIE_NO_BUTTONS;
    if (word == last) {
        return;
    }
    if (lead->movie_count == LEAD_MOVIE_READS) {
        if (lead == &g_lead) {
            port_ReplayLeadBreak("a movie's buttons changed too often");
        } else {
            port_SessionLeadBreak("a movie's buttons changed too often");
        }
        return;
    }
    lead->movie[lead->movie_count].read = read;
    lead->movie[lead->movie_count].word = word;
    lead->movie_count++;
}

/* ... and as the replay being played from power on kept it. */
s32 port_ReplayMovieRead(u32 read, u32 *word) {
    s32 i;

    if (g_play.state != PLAY_ON || !g_play.from_boot) {
        return FALSE;
    }
    *word = MOVIE_NO_BUTTONS;
    for (i = 0; i < g_play.movie_count && g_play.movie[i].read <= read; i++) {
        *word = g_play.movie[i].word;
    }
    return TRUE;
}

void port_ReplayTookSound(u32 index, s32 sound) {
    if (port_LeadNow() != NULL && index < LEAD_MAX) {
        port_LeadNow()->frames[index].sound = (u8)(sound + 1);
    }
    if (g_port_ahead || g_rec.state != REC_FIGHT || index < g_rec.start_count ||
        index - g_rec.start_count >= FRAMES_MAX) {
        return;
    }
    g_rec.frames[index - g_rec.start_count].sound = (u8)(sound + 1);
}

/* The menu sound kept with input `index` of the replay being played
 * (port_replay.h). */
s32 port_ReplaySoundAt(u32 index) {
    u32 at = index - g_play.start_count;

    if (g_play.state == PLAY_ON && g_play.in_lead) {
        return index < g_play.lead_count ? (s32)g_play.lead[index].sound - 1
                                         : -1;
    }

    if (g_play.state != PLAY_ON || index < g_play.start_count ||
        at >= g_play.frames) {
        return -1;
    }
    return (s32)g_play.list[at].sound - 1;
}

/* ... and the top menu's cover (port_replay.h). */
s32 port_ReplayCoverAt(u32 index) {
    u32 at = index - g_play.start_count;

    if (g_play.state != PLAY_ON) {
        return -1;
    }
    if (g_play.in_lead) {
        return index < g_play.lead_count ? g_play.lead[index].cover : -1;
    }
    if (index < g_play.start_count || at >= g_play.frames) {
        return -1;
    }
    return g_play.list[at].cover;
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

/* ---- the start a match's spectators are given ----
 *
 * A room's match is watched from the start of its latest fight, as a
 * replay is played from it (docs/NETPLAY.md, section 16): the match's
 * host compresses that start as soon as no rollback can take it back, a
 * slice at each boundary as a fight's file is written, into a replay's
 * own chunks (SERV, PIEC, STRT, DISC); the file of the fight, written
 * after it, takes the same work. A spectator puts it in, as a playback
 * puts in its file's start, and runs on from there with the match's
 * inputs (netplay.c). */

void port_ReplayServe(s32 on) {
    g_serve.on = on;
    g_serve.state = SERVE_NONE;
}

/* A served state's first chunks at `out`: the file's head, SERV (its
 * input's number, netplay's frame it is of and its hash) and PIEC. The
 * bytes written. */
static u32 port_ReplayServeHead(u8 *out, u32 count, u32 frame, u64 hash) {
    u32 at;
    u32 k;
    s32 i;

    port_CopyBytes(out, g_magic, MAGIC_BYTES);
    port_PutWord(out + MAGIC_BYTES, FORMAT_VERSION);
    at = HEAD_BYTES;
    port_PutWord(out + at, TAG_SERVE);
    port_PutWord(out + at + WORD_BYTES, SERVE_HEAD_WORDS * WORD_BYTES);
    at += CHUNK_HEAD_BYTES;
    port_PutWord(out + at, count);
    port_PutWord(out + at + WORD_BYTES, frame);
    port_PutWord(out + at + 2 * WORD_BYTES, (u32)hash);
    port_PutWord(out + at + 3 * WORD_BYTES, (u32)(hash >> WORD_BITS));
    at += SERVE_HEAD_WORDS * WORD_BYTES;
    port_PutWord(out + at, TAG_PIECES);
    port_PutWord(out + at + WORD_BYTES,
                 WORD_BYTES + (u32)port_StatePieceCount() *
                                  (PIECE_NAME_BYTES + WORD_BYTES));
    at += CHUNK_HEAD_BYTES;
    port_PutWord(out + at, (u32)port_StatePieceCount());
    at += WORD_BYTES;
    for (i = 0; i < port_StatePieceCount() && i < PIECES_MAX; i++) {
        PortStatePiece piece;

        port_StateGetPiece(i, &piece);
        port_ClearBytes(out + at, PIECE_NAME_BYTES);
        for (k = 0; piece.name[k] != 0 && k + 1 < PIECE_NAME_BYTES; k++) {
            out[at + k] = (u8)piece.name[k];
        }
        port_PutWord(out + at + PIECE_NAME_BYTES, piece.bytes);
        at += PIECE_NAME_BYTES + WORD_BYTES;
    }
    return at;
}

/* A state compressed (`packed`, `packed_bytes`, its runs of the disc
 * `matches`) into a replay's own chunks, SERV, PIEC, STRT, DISC and END,
 * at `out`: its input's number, netplay's frame it is of and its hash in
 * SERV. The bytes, or 0 where `room` is too small. The spectators' start
 * and a resync's state (port_replay.h) are each one. */
static u32 port_ReplayStateBlob(
    u8 *out, u32 room, u32 count, u32 frame, u64 hash, const u8 *packed,
    u32 packed_bytes, const ReplayMatch *matches, u32 match_count) {
    u32 at;
    u32 k;

    if (packed_bytes + match_count * MATCH_BYTES + KILOBYTE +
            PIECES_MAX * (PIECE_NAME_BYTES + WORD_BYTES) >
        room) {
        return 0;
    }
    at = port_ReplayServeHead(out, count, frame, hash);
    port_PutWord(out + at, TAG_START);
    port_PutWord(out + at + WORD_BYTES, WORD_BYTES + packed_bytes);
    port_PutWord(out + at + CHUNK_HEAD_BYTES, port_StateSize());
    at += CHUNK_HEAD_BYTES + WORD_BYTES;
    port_CopyBytes(out + at, packed, packed_bytes);
    at += packed_bytes;
    port_PutWord(out + at, TAG_DISC);
    port_PutWord(out + at + WORD_BYTES, WORD_BYTES + match_count * MATCH_BYTES);
    port_PutWord(out + at + CHUNK_HEAD_BYTES, match_count);
    at += CHUNK_HEAD_BYTES + WORD_BYTES;
    for (k = 0; k < match_count; k++) {
        port_PutWord(out + at, matches[k].at);
        port_PutWord(out + at + WORD_BYTES, matches[k].sector);
        port_PutWord(out + at + 2 * WORD_BYTES, matches[k].offset);
        port_PutWord(out + at + 3 * WORD_BYTES, matches[k].length);
        at += MATCH_BYTES;
    }
    port_PutWord(out + at, TAG_END);
    port_PutWord(out + at + WORD_BYTES, 0);
    return at + CHUNK_HEAD_BYTES;
}

/* The spectators' copy of the start just compressed, in a replay's chunks:
 * where the start compressed is the served one. */
static void port_ReplayServeBuild(void) {
    u32 size;

    if (g_serve.state != SERVE_PACKING || g_serve.count != g_rec.start_count ||
        g_rec.deflate.failed) {
        return;
    }
    if (g_serve.blob == NULL) {
        g_serve.room = g_rec.packed_room + MATCHES_MAX * MATCH_BYTES +
                       PIECES_MAX * (PIECE_NAME_BYTES + WORD_BYTES) + KILOBYTE;
        g_serve.blob = port_HostAllocate(g_serve.room);
    }
    size =
        g_serve.blob == NULL
            ? 0
            : port_ReplayStateBlob(
                  g_serve.blob, g_serve.room, g_rec.start_count,
                  g_rec.start_frame, g_rec.start_hash, g_rec.packed,
                  g_rec.deflate.out_at, g_dedupe.matches, g_dedupe.match_count);
    if (size == 0) {
        g_serve.state = SERVE_NONE;
        return;
    }
    g_serve.size = size;
    g_serve.frame = g_rec.start_frame;
    g_serve.hash = g_rec.start_hash;
    g_serve.state = SERVE_READY;
    g_serve.resync = FALSE;
    g_serve.made++;
    size = port_StateSize();
    port_Log("replay: the fight's start for the spectators, netplay's frame "
             "%u: %u bytes (of %u)\n",
             g_serve.frame, g_serve.size, size);
}

/* The serving's turn at a boundary: a fight's start, once final, begun
 * compressing, and a slice of it. */
static void port_ReplayServeBoundary(void) {
    u32 start;
    s32 done;

    if (!g_serve.on || g_rec.state == REC_WRITING) {
        return;
    }
    if (g_rec.state == REC_FIGHT &&
        (g_serve.state == SERVE_NONE || g_serve.count != g_rec.start_count) &&
        port_ReplayIsFinal(g_rec.start_count + 1)) {
        s32 i;

        /* port_ReplayFinish's, with the fight going on */
        g_serve.state = SERVE_PACKING;
        g_serve.count = g_rec.start_count;
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
    if (g_serve.state != SERVE_PACKING) {
        return;
    }
    start = port_HostTicks();
    do {
        done = port_ReplayWork(g_slice != 0 ? WORK_PIECE : FILE_MAX);
    } while (!done && port_HostTicks() - start <
                          g_slice * PORT_HOST_TICKS_PER_MICROSECOND);
    if (done) {
        port_ReplayServeBuild();
    }
}

/* The latest start for the spectators (port_replay.h): a resync's state,
 * where there was one since the fight's start was served. */
s32 port_ReplayServedStart(const u8 **blob, u32 *size, u32 *frame, u32 *made) {
    if (g_serve.on && g_serve.resync) {
        /* a resync's state: the spectators who parted from the players
         * put it in (netplay.c); as the difference from a state they have
         * where the whole is more than the lobby keeps */
        s32 told =
            g_resync.size > PORT_REPLAY_SERVED_MOST && g_base.blob_size != 0;

        *blob = told ? g_base.blob : g_resync.blob;
        *size = told ? g_base.blob_size : g_resync.size;
        *frame = g_resync.frame;
        *made = g_serve.made;
        return TRUE;
    }
    if (g_serve.state != SERVE_READY) {
        return FALSE;
    }
    *blob = g_serve.blob;
    *size = g_serve.size;
    *frame = g_serve.frame;
    *made = g_serve.made;
    return TRUE;
}

static s32 port_ReplayPlayRoom(void);
static s32 port_FindChunk(
    const u8 *bytes, u32 size, u32 tag, const u8 **found, u32 *found_size);
static s32 port_ReplayUnpackStart(const u8 *bytes, u32 size);
static s32 port_ReplayUnpackDiff(
    const u8 *bytes, u32 size, const u8 *base, u32 base_size);
static u32 port_GetVarint(const u8 *in, u32 size, u32 *at);
static void port_ReplayKeep(s32 out);

/* A match's start, as its host served it, put into the state, this run's
 * own kept (port_replay.h): netplay's frame it is at, and FALSE where it
 * is not whole or not the state its host had. */
s32 port_ReplayLoadServed(const u8 *blob, u32 size, u32 *frame) {
    const u8 *serve;
    u32 serve_size;
    u64 hash;
    u64 put_in;
    const u8 *base;
    u32 base_size;

    if (!port_ReplayPlayRoom() ||
        !port_FindChunk(blob, size, TAG_SERVE, &serve, &serve_size) ||
        serve_size < SERVE_HEAD_WORDS * WORD_BYTES) {
        return FALSE;
    }
    if (port_FindChunk(blob, size, TAG_BASE, &base, &base_size)
            ? !port_ReplayUnpackDiff(blob, size, base, base_size)
            : !port_ReplayUnpackStart(blob, size)) {
        return FALSE;
    }
    *frame = port_GetWord(serve + WORD_BYTES);
    hash = port_GetWord(serve + 2 * WORD_BYTES) |
           ((u64)port_GetWord(serve + 3 * WORD_BYTES) << WORD_BITS);
    port_ReplayKeep(TRUE);
    port_StateLoad(g_play.start);
    port_ReplayKeep(FALSE);
    port_CardsForReplay();
    put_in = port_StateHash();
    if (put_in != hash) {
        port_Log("replay: the match's start put in is not its host's: its "
                 "hash %08X%08X, the host's %08X%08X\n",
                 (u32)(put_in >> WORD_BITS), (u32)put_in,
                 (u32)(hash >> WORD_BITS), (u32)hash);
        return FALSE;
    }
    return TRUE;
}

/* ---- a resync (docs/NETPLAY.md, section 21) ----
 *
 * The host's state, packed whole as a spectator's start is (what the disc
 * holds left out, the rest deflated), is put on every machine of the
 * session; each machine's replay of the fight it happened in keeps it
 * (RSYN), so that a playback puts it in at the same input. */

static u32 port_ReplayDiffMake(const u8 *base, const u8 *now, u32 size);
static void port_ReplayDiffBlob(u32 count, u32 frame, u64 hash);

/* The host's state packed for a resync (port_replay.h), and for the
 * spectators the difference from the agreed frame's where the whole is
 * too large for the lobby. */
s32 port_ReplayResyncPack(u32 frame, u32 agreed, const u8 **blob, u32 *size) {
    u32 state = port_StateSize();
    u32 kept_work;
    u32 kept_count;
    u32 kept_saved;
    u32 count = g_port_input_count;
    u32 began = port_HostTicks();
    u64 hash;
    s32 i;

    if (!port_ReplayRoom()) {
        return FALSE;
    }
    if (g_resync.snapshot == NULL) {
        g_resync.snapshot = port_HostAllocate(state);
        g_resync.packed_room = port_DeflateBound(state);
        g_resync.packed = port_HostAllocate(g_resync.packed_room);
        g_resync.matches = port_HostAllocate(MATCHES_MAX * sizeof(ReplayMatch));
        g_resync.room = g_resync.packed_room + MATCHES_MAX * MATCH_BYTES +
                        PIECES_MAX * (PIECE_NAME_BYTES + WORD_BYTES) + KILOBYTE;
        g_resync.blob = port_HostAllocate(g_resync.room);
        if (g_resync.snapshot == NULL || g_resync.packed == NULL ||
            g_resync.matches == NULL || g_resync.blob == NULL) {
            g_resync.snapshot = NULL;
            return FALSE;
        }
    }
    /* the compressor is one stream at a time, and the search of the disc's
     * runs one search: a fight's file or a spectators' start being made
     * is finished first, and the runs found for the fight's start kept */
    if (g_rec.state == REC_WRITING) {
        while (!port_ReplayWork(FILE_MAX)) {
        }
        port_ReplayWrite();
    }
    if (g_serve.state == SERVE_PACKING) {
        while (!port_ReplayWork(FILE_MAX)) {
        }
        port_ReplayServeBuild();
    }
    kept_work = (u32)g_dedupe.work;
    kept_count = g_dedupe.match_count;
    kept_saved = g_dedupe.saved;
    port_CopyBytes(
        g_resync.matches, g_dedupe.matches, kept_count * sizeof(ReplayMatch));
    port_StateSave(g_resync.snapshot);
    hash = port_StateHash();
    port_ReplayClearOwn(g_resync.snapshot);
    /* for the spectators, the difference from the state of the frame the
     * machines last held the same, which they have: made before the disc's
     * runs are cleared out of the snapshot, deflated below if the whole
     * is too large for the lobby */
    g_base.raw_size = 0;
    g_base.blob_size = 0;
    g_base.frame_of = agreed;
    for (i = 0; i < BASES_KEPT && g_serve.on && agreed != NONE; i++) {
        if (g_base.had[i] && g_base.frame[i] == agreed) {
            port_ReplayClearOwn(g_base.state[i]);
            g_base.raw_size =
                port_ReplayDiffMake(g_base.state[i], g_resync.snapshot, state);
            g_base.frame_of = agreed;
            g_base.hash_of = g_base.hash[i];
        }
    }
    g_dedupe.sector_count = 0;
    g_dedupe.indexed = 0;
    g_dedupe.scanned = 0;
    g_dedupe.cleared_to = 0;
    g_dedupe.match_count = 0;
    g_dedupe.saved = 0;
    port_ClearBytes(g_dedupe.index, INDEX_SIZE * sizeof(u32));
    port_ClearBytes(g_dedupe.slot_of, (SECTOR_SLOT_MASK + 1) * sizeof(u32));
    g_roll_top = 1;
    for (i = 1; i < BLOCK_WORDS; i++) {
        g_roll_top *= ROLL;
    }
    while (port_ReplayIndexOne()) {
    }
    port_ReplayScan(g_resync.snapshot, state, state);
    port_DeflateBegin(&g_resync.deflate, g_resync.snapshot, state,
                      g_resync.packed, g_resync.packed_room);
    while (!port_DeflateSome(&g_resync.deflate, FILE_MAX)) {
    }
    g_resync.size =
        g_resync.deflate.failed
            ? 0
            : port_ReplayStateBlob(
                  g_resync.blob, g_resync.room, count, frame, hash,
                  g_resync.packed, g_resync.deflate.out_at, g_dedupe.matches,
                  g_dedupe.match_count);
    began = port_HostTicks() - began;
    port_Log("replay: a resync's state, netplay's frame %u: %u bytes (of %u; "
             "%u of them the disc's, in %u runs), packed in %u ms\n",
             frame, g_resync.size, state, g_dedupe.saved, g_dedupe.match_count,
             began / TICKS_PER_MS);
    port_CopyBytes(
        g_dedupe.matches, g_resync.matches, kept_count * sizeof(ReplayMatch));
    g_dedupe.match_count = kept_count;
    g_dedupe.saved = kept_saved;
    g_dedupe.work = (s32)kept_work;
    g_resync.frame = frame;
    if (g_resync.size == 0) {
        return FALSE;
    }
    if (g_serve.on && g_resync.size > PORT_REPLAY_SERVED_MOST) {
        port_ReplayDiffBlob(count, frame, hash);
    }
    *blob = g_resync.blob;
    *size = g_resync.size;
    return TRUE;
}

/* A resync's state put in (port_replay.h), kept as this program's last,
 * for the replay of the fight and the spectators it may serve. */
s32 port_ReplayResyncLoad(const u8 *blob, u32 size, u32 *frame) {
    const u8 *base;
    u32 base_size;

    if (blob != g_resync.blob) {
        /* kept for the spectators, where this program serves them */
        if (g_resync.blob == NULL || g_resync.room < size) {
            g_resync.room = size;
            g_resync.blob = port_HostAllocate(size);
            if (g_resync.blob == NULL) {
                g_resync.room = 0;
                return FALSE;
            }
        }
        port_CopyBytes(g_resync.blob, blob, size);
        g_resync.size = size;
    }
    if (!port_ReplayLoadServed(g_resync.blob, size, frame)) {
        return FALSE;
    }
    g_resync.frame = *frame;
    if (port_FindChunk(g_resync.blob, size, TAG_BASE, &base, &base_size) &&
        port_ReplayWrites()) {
        /* a spectator's: the fight's replay keeps the state whole, which
         * a playback puts in with no base of its own */
        const u8 *whole;
        u32 whole_size;

        port_ReplayResyncPack(*frame, NONE, &whole, &whole_size);
    }
    return TRUE;
}

/* ---- a resync's state for the spectators, told as a difference ---- */

/* A hashed frame's state kept (port_replay.h): in the slot of that frame
 * where one is (the host's frame made again), or the next not held. */
void port_ReplayKeepBase(u32 frame, u64 hash, const void *saved) {
    u32 state = port_StateSize();
    s32 slot = -1;
    s32 i;

    if (g_base.state[0] == NULL) {
        for (i = 0; i < BASES_KEPT; i++) {
            g_base.state[i] = port_HostAllocate(state);
            if (g_base.state[i] == NULL) {
                return;
            }
        }
    }
    if (g_base.state[BASES_KEPT - 1] == NULL) {
        return;
    }
    for (i = 0; i < BASES_KEPT; i++) {
        if (g_base.had[i] && g_base.frame[i] == frame && i != g_base.held) {
            slot = i;
        }
    }
    for (i = 0; i < BASES_KEPT && slot < 0; i++) {
        s32 at = (s32)((g_base.next + (u32)i) % BASES_KEPT);

        if (at != g_base.held) {
            slot = at;
            g_base.next = (u32)at + 1;
        }
    }
    if (slot < 0) {
        return;
    }
    if (saved != NULL) {
        /* the host's, as netplay saved it: copied, so that the state's
         * pages do not take it for what they saved here last */
        port_CopyBytes(g_base.state[slot], saved, state);
        port_StateForget(g_base.state[slot]);
    } else {
        port_StateSave(g_base.state[slot]);
    }
    g_base.had[slot] = TRUE;
    g_base.frame[slot] = frame;
    g_base.hash[slot] = hash;
}

/* The kept state of `frame` held from being gone round (port_replay.h). */
void port_ReplayHoldBase(u32 frame) {
    s32 i;

    g_base.held = -1;
    for (i = 0; i < BASES_KEPT && frame != NONE; i++) {
        if (g_base.had[i] && g_base.frame[i] == frame) {
            g_base.held = i;
        }
    }
}

/* The base of a difference: the one kept of that frame and hash, or -1. */
static s32 port_ReplayFindBase(u32 frame, u64 hash) {
    s32 i;

    for (i = 0; i < BASES_KEPT; i++) {
        if (g_base.had[i] && g_base.frame[i] == frame &&
            g_base.hash[i] == hash) {
            return i;
        }
    }
    return -1;
}

/* Whether a start served is a difference whose base is not kept here
 * (port_replay.h). */
s32 port_ReplayServedNeedsBase(const u8 *blob, u32 size, u32 *frame) {
    const u8 *serve;
    u32 serve_size;
    const u8 *base;
    u32 base_size;

    if (!port_FindChunk(blob, size, TAG_BASE, &base, &base_size) ||
        base_size < BASE_HEAD_WORDS * WORD_BYTES ||
        !port_FindChunk(blob, size, TAG_SERVE, &serve, &serve_size) ||
        serve_size < SERVE_HEAD_WORDS * WORD_BYTES) {
        return FALSE;
    }
    *frame = port_GetWord(serve + WORD_BYTES);
    return port_ReplayFindBase(
               port_GetWord(base),
               port_GetWord(base + WORD_BYTES) |
                   ((u64)port_GetWord(base + 2 * WORD_BYTES) << WORD_BITS)) < 0;
}

/* The difference from `base` to `now` (`size` bytes, each a snapshot)
 * into g_base.raw: its bytes, or 0 where there is no room. */
static u32 port_ReplayDiffMake(const u8 *base, const u8 *now, u32 size) {
    u32 words = size / WORD_BYTES;
    u32 at = 0;
    u32 out = 0;
    u32 room = size + (words / BASE_SAME_LEAST + 2) * BASE_PAIR_MOST;

    if (size % WORD_BYTES != 0) {
        return 0;
    }
    if (g_base.raw_room < room) {
        g_base.raw = port_HostAllocate(room);
        g_base.raw_room = g_base.raw != NULL ? room : 0;
        if (g_base.raw == NULL) {
            return 0;
        }
    }
    while (at < words) {
        u32 same = 0;
        u32 run = 0;
        u32 end;

        while (at + same < words &&
               port_GetWord(base + (at + same) * WORD_BYTES) ==
                   port_GetWord(now + (at + same) * WORD_BYTES)) {
            same++;
        }
        at += same;
        /* the new words, to the next run of the same long enough */
        for (end = at; end < words && run < BASE_SAME_LEAST; end++) {
            run = port_GetWord(base + end * WORD_BYTES) ==
                          port_GetWord(now + end * WORD_BYTES)
                      ? run + 1
                      : 0;
        }
        if (run == BASE_SAME_LEAST) {
            end -= BASE_SAME_LEAST;
        }
        out += port_PutVarint(g_base.raw + out, same);
        out += port_PutVarint(g_base.raw + out, end - at);
        port_CopyBytes(
            g_base.raw + out, now + at * WORD_BYTES, (end - at) * WORD_BYTES);
        out += (end - at) * WORD_BYTES;
        at = end;
    }
    return out;
}

/* The host's difference deflated into a replay's chunks for the
 * spectators (SERV, PIEC, BASE, END), where it was made and fits. */
static void port_ReplayDiffBlob(u32 count, u32 frame, u64 hash) {
    u32 state = port_StateSize();
    u32 room;
    u32 at;
    u8 *out;

    if (g_base.raw_size == 0) {
        port_Log("replay: a resync's state for the spectators: the state of "
                 "frame %u, the last the same, is not kept\n",
                 g_base.frame_of);
        return;
    }
    room = port_DeflateBound(g_base.raw_size);
    if (g_base.packed_room < room) {
        g_base.packed = port_HostAllocate(room);
        g_base.packed_room = g_base.packed != NULL ? room : 0;
        /* the deflated difference, and the chunks round it */
        g_base.blob = port_HostAllocate(
            room + KILOBYTE + PIECES_MAX * (PIECE_NAME_BYTES + WORD_BYTES));
        if (g_base.packed == NULL || g_base.blob == NULL) {
            g_base.packed_room = 0;
            return;
        }
    }
    port_DeflateBegin(&g_base.deflate, g_base.raw, g_base.raw_size,
                      g_base.packed, g_base.packed_room);
    while (!port_DeflateSome(&g_base.deflate, FILE_MAX)) {
    }
    if (g_base.deflate.failed) {
        return;
    }
    out = g_base.blob;
    at = port_ReplayServeHead(out, count, frame, hash);
    port_PutWord(out + at, TAG_BASE);
    port_PutWord(out + at + WORD_BYTES,
                 BASE_HEAD_WORDS * WORD_BYTES + g_base.deflate.out_at);
    at += CHUNK_HEAD_BYTES;
    port_PutWord(out + at, g_base.frame_of);
    port_PutWord(out + at + WORD_BYTES, (u32)g_base.hash_of);
    port_PutWord(out + at + 2 * WORD_BYTES, (u32)(g_base.hash_of >> WORD_BITS));
    port_PutWord(out + at + 3 * WORD_BYTES, g_base.raw_size);
    port_PutWord(out + at + 4 * WORD_BYTES, state);
    at += BASE_HEAD_WORDS * WORD_BYTES;
    port_CopyBytes(out + at, g_base.packed, g_base.deflate.out_at);
    at += g_base.deflate.out_at;
    port_PutWord(out + at, TAG_END);
    port_PutWord(out + at + WORD_BYTES, 0);
    g_base.blob_size = at + CHUNK_HEAD_BYTES;
    port_Log("replay: a resync's state for the spectators, more than the "
             "lobby keeps whole: told as the difference from frame %u's, "
             "%u bytes (%u raw)\n",
             g_base.frame_of, g_base.blob_size, g_base.raw_size);
}

/* A difference served (BASE) put into the playback's start: the base this
 * spectator kept, this run's own cleared, and the new words over it.
 * FALSE where it has not the base, or the difference is not whole or not
 * of this build's pieces. */
static s32 port_ReplayUnpackDiff(
    const u8 *bytes, u32 size, const u8 *base, u32 base_size) {
    const u8 *pieces;
    u32 pieces_size;
    u32 state = port_StateSize();
    u32 raw;
    u32 at = 0;
    u32 word = 0;
    s32 kept;
    s32 i;

    if (base_size < BASE_HEAD_WORDS * WORD_BYTES ||
        port_GetWord(base + 4 * WORD_BYTES) != state ||
        !port_FindChunk(bytes, size, TAG_PIECES, &pieces, &pieces_size) ||
        pieces_size < WORD_BYTES ||
        port_GetWord(pieces) != (u32)port_StatePieceCount() ||
        pieces_size < WORD_BYTES + port_GetWord(pieces) *
                                       (PIECE_NAME_BYTES + WORD_BYTES)) {
        return FALSE;
    }
    for (i = 0; i < port_StatePieceCount(); i++) {
        const u8 *of =
            pieces + WORD_BYTES + (u32)i * (PIECE_NAME_BYTES + WORD_BYTES);
        PortStatePiece piece;

        port_StateGetPiece(i, &piece);
        if (!port_TextsEqual((const char *)of, piece.name) ||
            port_GetWord(of + PIECE_NAME_BYTES) != piece.bytes) {
            return FALSE;
        }
    }
    kept = port_ReplayFindBase(
        port_GetWord(base),
        port_GetWord(base + WORD_BYTES) |
            ((u64)port_GetWord(base + 2 * WORD_BYTES) << WORD_BITS));
    if (kept < 0) {
        port_Log("replay: the start is told as the difference from the state "
                 "of frame %u, which this program has not kept\n",
                 port_GetWord(base));
        return FALSE;
    }
    raw = port_GetWord(base + 3 * WORD_BYTES);
    if (g_base.raw_room < raw) {
        g_base.raw = port_HostAllocate(raw);
        g_base.raw_room = g_base.raw != NULL ? raw : 0;
    }
    if (g_base.raw == NULL || g_base.raw_room < raw ||
        !port_ZlibInflate(
            base + BASE_HEAD_WORDS * WORD_BYTES,
            base_size - BASE_HEAD_WORDS * WORD_BYTES, g_base.raw, raw)) {
        return FALSE;
    }
    port_CopyBytes(g_play.start, g_base.state[kept], state);
    port_ReplayClearOwn(g_play.start);
    while (at < raw) {
        u32 same = port_GetVarint(g_base.raw, raw, &at);
        u32 fresh = port_GetVarint(g_base.raw, raw, &at);

        if (same > state / WORD_BYTES - word ||
            fresh > state / WORD_BYTES - word - same ||
            fresh * WORD_BYTES > raw - at) {
            return FALSE;
        }
        word += same;
        port_CopyBytes(g_play.start + word * WORD_BYTES, g_base.raw + at,
                       fresh * WORD_BYTES);
        word += fresh;
        at += fresh * WORD_BYTES;
    }
    return word == state / WORD_BYTES;
}

/* The last resync's state the session's (port_replay.h): the session's
 * run from power on broken there, the spectators served it, and the fight
 * being recorded keeping it, at its input now (one at the same input or
 * later, which the frames it was in are gone with, given up). */
void port_ReplayResynced(u32 agreed_frame) {
    u32 count = g_port_input_count;
    u32 back =
        g_resync.frame > agreed_frame ? g_resync.frame - agreed_frame : 0;
    s32 resync;

    port_SessionLeadBreak("a resync put the host's state in");
    if (g_serve.on) {
        /* the spectators who parted from the players are given it */
        g_serve.resync = TRUE;
        g_serve.made++;
    }
    if ((g_rec.state != REC_FIGHT && g_rec.state != REC_ENDED) ||
        count <= g_rec.start_count) {
        return;
    }
    /* a resync later than this one is gone with the frames it was in */
    while (g_resync.count > 0 &&
           g_resync.at[g_resync.count - 1] >= count - g_rec.start_count) {
        g_resync.count--;
    }
    if (g_resync.count == RESYNCS_MAX) {
        port_ReplayAbandon("it was resynced too often");
        return;
    }
    resync = g_resync.count;
    if (g_resync.kept_room[resync] < g_resync.size) {
        g_resync.kept[resync] = port_HostAllocate(g_resync.size);
        g_resync.kept_room[resync] =
            g_resync.kept[resync] != NULL ? g_resync.size : 0;
    }
    if (g_resync.kept[resync] == NULL) {
        port_ReplayAbandon("no memory for its resync");
        return;
    }
    port_CopyBytes(g_resync.kept[resync], g_resync.blob, g_resync.size);
    g_resync.kept_size[resync] = g_resync.size;
    g_resync.at[resync] = count - g_rec.start_count;
    g_resync.agreed[resync] =
        g_resync.at[resync] > back ? g_resync.at[resync] - back : 0;
    g_resync.count++;
    port_Log("replay: the fight's resync %d at its frame %u kept; its hashes "
             "from its frame %u to there are not held\n",
             g_resync.count, g_resync.at[resync], g_resync.agreed[resync]);
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
        g_before.console = port_StateConsoleHash();
        g_before.count = count;
        g_before.frame = port_IsNetplayOn() ? port_NetplayFrameNow() : 0;
        g_before.view = *view;
        g_before.is_kept = TRUE;
    }
    port_ReplayServeBoundary();
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
        (port_GetWord(bytes + MAGIC_BYTES) != FORMAT_VERSION &&
         port_GetWord(bytes + MAGIC_BYTES) != FORMAT_VERSION_FIRST)) {
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
                        "recorded by another build (%s), whose game is not "
                        "this one's or lies elsewhere in memory",
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

/* Whether the game that recorded a replay is this one: its code's sum
 * (port_state.h), or where neither program has one (no window) its
 * version with its commit. */
static s32 port_ReplaySameGame(const u8 *meta, u32 size) {
    char version[PORT_REPLAY_WHY_BYTES];
    u64 code = port_MetaHex(meta, size, "code");

    if (code != 0 || g_port_code_sum != 0) {
        return code == g_port_code_sum;
    }
    return port_MetaValue(meta, size, "version", version, sizeof(version)) &&
           port_TextsEqual(version, g_rb_version);
}

/* Whether this program can play what `bytes` holds from power on (its
 * lead-in), and why not. */
static s32 port_ReplayLeadPlayable(
    const u8 *bytes, u32 size, char *why, u32 room) {
    const u8 *meta;
    u32 meta_size;
    s32 has_prims;

    if (size < HEAD_BYTES ||
        port_GetWord(bytes + MAGIC_BYTES) != FORMAT_VERSION ||
        !port_FindChunk(bytes, size, TAG_META, &meta, &meta_size)) {
        port_ReplayText(why, room,
                        "it holds nothing from power on (a replay "
                        "of an earlier version)");
        return FALSE;
    }
    if (port_MetaNumber(meta, meta_size, "lead") != 1) {
        char lead_why[PORT_REPLAY_WHY_BYTES];

        if (!port_MetaValue(
                meta, meta_size, "lead_why", lead_why, sizeof(lead_why))) {
            lead_why[0] = 0;
        }
        port_ReplayText(
            why, room, "it holds nothing from power on (%s)", lead_why);
        return FALSE;
    }
    if (port_MetaHex(meta, meta_size, "disc") != port_SessionDiscSum()) {
        port_ReplayText(why, room, "recorded from another disc image");
        return FALSE;
    }
    if (port_MetaHex(meta, meta_size, "mods") != port_ReplayMods() ||
        (u32)port_MetaNumber(meta, meta_size, "stage_music") !=
            port_MusicStageSum()) {
        port_ReplayText(why, room,
                        "recorded with other gameplay mods (stages, weapons, "
                        "motions, arenas or the stages' music)");
        return FALSE;
    }
    port_ReplayStateSize(&has_prims);
    if (port_MetaNumber(meta, meta_size, "prims") != 0 && !has_prims) {
        port_ReplayText(why, room,
                        "recorded with a draw distance past the console's: "
                        "start with draw_distance far or max to play it");
        return FALSE;
    }
    /* (a replay asked for on the command line is read before the state
     * at power on is kept: the playback's beginning holds it to it) */
    if ((g_lead.kept && !g_lead.plain) || g_lead.why[0] != 0) {
        port_ReplayText(why, room,
                        "recorded by another build, and this run did not "
                        "begin from power on as a plain run does (%s)",
                        g_lead.why[0] != 0 ? g_lead.why : "nothing kept");
        return FALSE;
    }
    why[0] = 0;
    return TRUE;
}

/* Whether and how this program plays what `bytes` holds: from its start
 * (a build of the same core), or from power on (*from_boot); and why
 * not. */
static s32 port_ReplayPlayableHow(
    const u8 *bytes, u32 size, char *why, u32 room, s32 *from_boot) {
    char lead_why[PORT_REPLAY_WHY_BYTES];
    s32 same = port_ReplayPlayable(bytes, size, why, room);

    *from_boot = FALSE;
    if (same && !g_from_boot_asked) {
        return TRUE;
    }
    if (port_ReplayLeadPlayable(bytes, size, lead_why, sizeof(lead_why))) {
        *from_boot = TRUE;
        if (!same) {
            port_ReplayText(why, room, "another build's: played from power on");
        }
        return TRUE;
    }
    if (same) {
        /* --replay-from-boot, and it cannot be */
        port_ReplayText(why, room, "%s", lead_why);
        return FALSE;
    }
    {
        u32 length = port_TextLength(why);

        port_ReplayText(why + length, room - length, "; and %s", lead_why);
    }
    return FALSE;
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
    info->playable = port_ReplayPlayableHow(
        bytes, size, info->why, sizeof(info->why), &info->from_boot);
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

/* `frames` frames packed as INPT has them (port_ReplayPackFrames) into
 * `list`: FALSE where they are not whole. */
static s32 port_ReplayUnpackFrames(
    const u8 *in, u32 size, u32 at, ReplayFrame *list, u32 frames) {
    ReplayFrame now = {{0, 0, 0, 0}, 0, 0, 0, 0};
    u32 frame = 0;

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
        if ((changed & CHANGED_THIRD) && at + HALF_BYTES <= size) {
            now.buttons[2] = (u16)(in[at] | (in[at + 1] << BITS_PER_BYTE));
            at += HALF_BYTES;
        }
        if ((changed & CHANGED_FOURTH) && at + HALF_BYTES <= size) {
            now.buttons[3] = (u16)(in[at] | (in[at + 1] << BITS_PER_BYTE));
            at += HALF_BYTES;
        }
        if ((changed & CHANGED_COVER) && at < size) {
            now.cover = in[at++];
        }
        run = port_GetVarint(in, size, &at);
        while (run-- > 0 && frame < frames) {
            list[frame++] = now;
        }
    }
    return frame == frames;
}

/* INPT into the playback's frames: FALSE where it is not whole. */
static s32 port_ReplayUnpackInputs(const u8 *in, u32 size) {
    if (size < WORD_BYTES || port_GetWord(in) >= FRAMES_MAX) {
        return FALSE;
    }
    g_play.frames = port_GetWord(in);
    return port_ReplayUnpackFrames(
        in, size, WORD_BYTES, g_play.list, g_play.frames);
}

/* A list of 64-bit hashes as HASH and CHSH have them, into `out`: how
 * many, or -1 where it is not whole. */
static s32 port_ReplayUnpackHashes(
    const u8 *chunk, u32 chunk_size, u64 *out, u32 most) {
    u32 count;
    u32 i;

    if (chunk_size < 2 * WORD_BYTES ||
        port_GetWord(chunk + WORD_BYTES) != PORT_REPLAY_HASH_EVERY) {
        return -1;
    }
    count = port_GetWord(chunk);
    if (count > most || chunk_size < 2 * WORD_BYTES + count * LONG_BYTES) {
        return -1;
    }
    for (i = 0; i < count; i++) {
        const u8 *at = chunk + 2 * WORD_BYTES + i * LONG_BYTES;

        out[i] = port_GetWord(at) |
                 ((u64)port_GetWord(at + WORD_BYTES) << WORD_BITS);
    }
    return (s32)count;
}

/* The start of the file in `bytes` into the snapshot `into`, laid out as
 * this run's state is (`raw` the room it is inflated in first): a piece
 * this run has and the file has not (the port's own primitive buffers) is
 * kept as it is now. */
static s32 port_ReplayUnpackStartInto(
    const u8 *bytes, u32 size, u8 *raw_room, u8 *into) {
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
            start + WORD_BYTES, start_size - WORD_BYTES, raw_room, raw)) {
        return FALSE;
    }
    if (!port_FindChunk(bytes, size, TAG_DISC, &disc, &disc_size) ||
        disc_size < WORD_BYTES ||
        disc_size < WORD_BYTES + port_GetWord(disc) * MATCH_BYTES ||
        !port_ReplayRefill(
            raw_room, raw, disc + WORD_BYTES, port_GetWord(disc))) {
        return FALSE;
    }
    count = port_GetWord(pieces);
    for (k = 0; k < port_StatePieceCount(); k++) {
        PortStatePiece piece;

        port_StateGetPiece(k, &piece);
        port_CopyBytes(into + piece.offset, piece.start, piece.bytes);
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
                port_CopyBytes(into + piece.offset, raw_room + from, bytes_of);
            }
        }
        from += bytes_of;
    }
    return from == raw;
}

/* ... the playback's start */
static s32 port_ReplayUnpackStart(const u8 *bytes, u32 size) {
    return port_ReplayUnpackStartInto(bytes, size, g_play.back, g_play.start);
}

/* The file's resyncs (RSYN) into the playback, in their order: FALSE
 * where one is not whole. */
static s32 port_ReplayUnpackResyncs(const u8 *bytes, u32 size) {
    u32 at = HEAD_BYTES;

    g_play.resync_count = 0;
    while (at + CHUNK_HEAD_BYTES <= size) {
        u32 tag = port_GetWord(bytes + at);
        u32 chunk_size = port_GetWord(bytes + at + WORD_BYTES);
        const u8 *chunk = bytes + at + CHUNK_HEAD_BYTES;
        s32 resync = g_play.resync_count;

        if (chunk_size > size - at - CHUNK_HEAD_BYTES) {
            return FALSE;
        }
        at += CHUNK_HEAD_BYTES + chunk_size;
        if (tag != TAG_RESYNC) {
            continue;
        }
        if (resync == RESYNCS_MAX ||
            chunk_size < RESYNC_HEAD_BYTES + HEAD_BYTES) {
            return FALSE;
        }
        g_play.resync_at[resync] = port_GetWord(chunk);
        g_play.resync_agreed[resync] = port_GetWord(chunk + WORD_BYTES);
        g_play.resync_blob[resync] = chunk + RESYNC_HEAD_BYTES;
        g_play.resync_size[resync] = chunk_size - RESYNC_HEAD_BYTES;
        g_play.resync_count++;
    }
    if (g_play.resync_count > 0 && g_play.resync_state == NULL) {
        g_play.resync_raw = port_HostAllocate(port_StateSize());
        g_play.resync_state = port_HostAllocate(port_StateSize());
        if (g_play.resync_raw == NULL || g_play.resync_state == NULL) {
            g_play.resync_state = NULL;
            return FALSE;
        }
    }
    return TRUE;
}

/* INPT, HASH, CHSH and VIEW into the playback: FALSE where one is not
 * whole. */
static s32 port_ReplayUnpackRest(const u8 *bytes, u32 size) {
    const u8 *chunk;
    u32 chunk_size;
    s32 got;
    u32 i;

    if (!port_FindChunk(bytes, size, TAG_INPUT, &chunk, &chunk_size) ||
        !port_ReplayUnpackInputs(chunk, chunk_size)) {
        return FALSE;
    }
    if (port_FindChunk(bytes, size, TAG_SOUND, &chunk, &chunk_size) &&
        chunk_size >= WORD_BYTES) {
        u32 count = port_GetWord(chunk);

        for (i = 0;
             i < count && WORD_BYTES + (i + 1) * 2 * WORD_BYTES <= chunk_size;
             i++) {
            const u8 *at = chunk + WORD_BYTES + i * 2 * WORD_BYTES;
            u32 frame = port_GetWord(at);

            if (frame < g_play.frames) {
                g_play.list[frame].sound = (u8)port_GetWord(at + WORD_BYTES);
            }
        }
    }
    if (!port_FindChunk(bytes, size, TAG_HASH, &chunk, &chunk_size)) {
        return FALSE;
    }
    got = port_ReplayUnpackHashes(chunk, chunk_size, g_play.hashes, HASHES_MAX);
    if (got < 0) {
        return FALSE;
    }
    g_play.hash_count = (u32)got;
    g_play.console_count = 0;
    if (port_FindChunk(bytes, size, TAG_CONSOLE, &chunk, &chunk_size)) {
        got = port_ReplayUnpackHashes(
            chunk, chunk_size, g_play.consoles, HASHES_MAX);
        if (got < 0) {
            return FALSE;
        }
        g_play.console_count = (u32)got;
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

/* LEAD and CARD into the playback: FALSE where one is not whole. */
static s32 port_ReplayUnpackLead(const u8 *bytes, u32 size) {
    const u8 *chunk;
    u32 chunk_size;
    u32 at;
    u32 count;
    u32 i;
    s32 slot;

    if (!port_FindChunk(bytes, size, TAG_LEAD, &chunk, &chunk_size) ||
        chunk_size < (LEAD_HEAD_WORDS + SESSION_WORDS + 1) * WORD_BYTES ||
        port_GetWord(chunk) != LEAD_VERSION ||
        (port_GetWord(chunk + WORD_BYTES) != 0 &&
         port_GetWord(chunk + WORD_BYTES) != LEAD_SESSION &&
         port_GetWord(chunk + WORD_BYTES) != LEAD_FIGHT)) {
        return FALSE;
    }
    g_play.session = port_GetWord(chunk + WORD_BYTES) == LEAD_SESSION;
    g_play.fight = port_GetWord(chunk + WORD_BYTES) == LEAD_FIGHT;
    g_play.lead_count = port_GetWord(chunk + 2 * WORD_BYTES);
    if (g_play.lead_count > LEAD_MAX ||
        (g_play.fight &&
         chunk_size < (LEAD_HEAD_WORDS + FIGHT_WORDS + 1) * WORD_BYTES)) {
        return FALSE;
    }
    at = 3 * WORD_BYTES;
    for (i = 0; i < SETUP_WORDS; i++, at += WORD_BYTES) {
        g_play.setup[i] = port_GetWord(chunk + at);
    }
    for (i = 0; g_play.fight && i < FIGHT_WORDS; i++, at += WORD_BYTES) {
        g_play.fight_words[i] = port_GetWord(chunk + at);
    }
    for (i = 0; g_play.session && i < SESSION_WORDS; i++, at += WORD_BYTES) {
        g_play.session_words[i] = port_GetWord(chunk + at);
    }
    count = port_GetWord(chunk + at);
    at += WORD_BYTES;
    if (count > LEAD_EVENTS ||
        at + count * (1 + SETUP_WORDS) * WORD_BYTES + WORD_BYTES > chunk_size) {
        return FALSE;
    }
    g_play.event_count = (s32)count;
    for (i = 0; i < count; i++) {
        u32 k;

        g_play.events[i].count = port_GetWord(chunk + at);
        at += WORD_BYTES;
        for (k = 0; k < SETUP_WORDS; k++, at += WORD_BYTES) {
            g_play.events[i].words[k] = port_GetWord(chunk + at);
        }
    }
    count = port_GetWord(chunk + at);
    at += WORD_BYTES;
    if (count > LEAD_VIEWS || at + count * 3 * WORD_BYTES > chunk_size) {
        return FALSE;
    }
    g_play.lead_view_count = (s32)count;
    for (i = 0; i < count; i++, at += 3 * WORD_BYTES) {
        g_play.lead_views[i].distance = (s32)port_GetWord(chunk + at);
        g_play.lead_views[i].margin_w =
            (s32)port_GetWord(chunk + at + WORD_BYTES);
        g_play.lead_views[i].margin_h =
            (s32)port_GetWord(chunk + at + 2 * WORD_BYTES);
    }
    {
        s32 hashes = port_ReplayUnpackHashes(
            chunk + at, chunk_size - at, g_play.lead_hashes, LEAD_HASHES);

        if (hashes < 0) {
            return FALSE;
        }
        g_play.lead_hash_count = (u32)hashes;
        at += 2 * WORD_BYTES + (u32)hashes * LONG_BYTES;
    }
    if (at + WORD_BYTES > chunk_size) {
        return FALSE;
    }
    count = port_GetWord(chunk + at);
    at += WORD_BYTES;
    if (at + count * 2 * WORD_BYTES > chunk_size) {
        return FALSE;
    }
    {
        u32 sounds_at = at;
        u32 reads;

        at += count * 2 * WORD_BYTES;
        if (at + WORD_BYTES > chunk_size) {
            return FALSE;
        }
        reads = port_GetWord(chunk + at);
        at += WORD_BYTES;
        if (reads > LEAD_MOVIE_READS ||
            at + reads * 2 * WORD_BYTES > chunk_size) {
            return FALSE;
        }
        g_play.movie_count = (s32)reads;
        for (i = 0; i < reads; i++, at += 2 * WORD_BYTES) {
            g_play.movie[i].read = port_GetWord(chunk + at);
            g_play.movie[i].word = port_GetWord(chunk + at + WORD_BYTES);
        }
        if (!port_ReplayUnpackFrames(
                chunk, chunk_size, at, g_play.lead, g_play.lead_count)) {
            return FALSE;
        }
        for (i = 0; i < count; i++) {
            u32 frame = port_GetWord(chunk + sounds_at + i * 2 * WORD_BYTES);

            if (frame < g_play.lead_count) {
                g_play.lead[frame].sound = (u8)port_GetWord(
                    chunk + sounds_at + i * 2 * WORD_BYTES + WORD_BYTES);
            }
        }
    }
    for (i = 0; i < g_play.lead_count; i++) {
        if (g_play.lead[i].view >= g_play.lead_view_count) {
            return FALSE;
        }
    }
    if (!port_FindChunk(bytes, size, TAG_CARDS, &chunk, &chunk_size) ||
        chunk_size < WORD_BYTES) {
        return FALSE;
    }
    g_play.card_flags = port_GetWord(chunk);
    at = WORD_BYTES;
    for (slot = 0; slot < PORT_CARD_SLOTS; slot++) {
        u32 packed;

        if (((g_play.card_flags >> slot) & 1) == 0) {
            continue;
        }
        if (at + 2 * WORD_BYTES > chunk_size ||
            port_GetWord(chunk + at) != PORT_CARD_IMAGE_SIZE) {
            return FALSE;
        }
        packed = port_GetWord(chunk + at + WORD_BYTES);
        at += 2 * WORD_BYTES;
        if (packed > chunk_size - at ||
            !port_ZlibInflate(
                chunk + at, packed, g_play.cards + slot * PORT_CARD_IMAGE_SIZE,
                PORT_CARD_IMAGE_SIZE)) {
            return FALSE;
        }
        at += packed;
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
    g_play.consoles = port_HostAllocate(HASHES_MAX * sizeof(u64));
    g_play.lead = port_HostAllocate(LEAD_MAX * sizeof(ReplayFrame));
    g_play.lead_hashes = port_HostAllocate(LEAD_HASHES * sizeof(u64));
    g_play.cards = port_HostAllocate(PORT_CARD_SLOTS * PORT_CARD_IMAGE_SIZE);
    g_kept_run =
        port_HostAllocate((u32)(g_port_state_data_end - g_port_run_data) +
                          (u32)(g_port_state_bss_end - g_port_run_bss) + 1);
    if (g_play.file == NULL || g_play.start == NULL || g_play.back == NULL ||
        g_play.list == NULL || g_play.hashes == NULL ||
        g_play.consoles == NULL || g_play.lead == NULL ||
        g_play.lead_hashes == NULL || g_play.cards == NULL ||
        g_kept_run == NULL) {
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
    if (!port_ReplayPlayableHow(
            g_play.file, size, why, room, &g_play.from_boot)) {
        return FALSE;
    }
    g_play.session = FALSE;
    g_play.fight = FALSE;
    if ((g_play.from_boot ? !port_ReplayUnpackLead(g_play.file, size)
                          : !port_ReplayUnpackStart(g_play.file, size)) ||
        !port_ReplayUnpackRest(g_play.file, size) ||
        !port_ReplayUnpackResyncs(g_play.file, size) ||
        (g_play.from_boot && g_play.console_count == 0)) {
        port_ReplayText(why, room, "the file is not whole");
        return FALSE;
    }
    if (g_play.from_boot && g_play.resync_count > 0) {
        port_ReplayText(why, room,
                        "it was resynced online: only a build of the one that "
                        "recorded it plays it");
        return FALSE;
    }
    port_FindChunk(g_play.file, size, TAG_META, &meta, &meta_size);
    {
        char ignored[PORT_REPLAY_WHY_BYTES];

        /* (a session's lead-in, from power on: the state holds the
         * session's own variables, which the playback's has not, so its
         * console's hashes alone are held) */
        g_play.same_build =
            port_ReplayPlayable(g_play.file, size, ignored, sizeof(ignored)) &&
            !(g_play.from_boot && g_play.session);
    }
    g_play.same_game =
        port_ReplaySameGame(meta, meta_size) && g_differ_asked == NONE;
    g_play.differ_at = g_differ_asked;
    g_play.start_console = port_MetaHex(meta, meta_size, "console");
    g_play.in_lead = g_play.from_boot;
    g_play.event_next = 0;
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
    g_play.keyframe_count = 0;
    g_play.keyframe_every = KEYFRAME_EVERY;
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
    s32 pad;

    if (g_play.in_lead && g_play.lead_count > 0) {
        u32 count = g_port_input_count;

        frame = &g_play.lead[count < g_play.lead_count ? count
                                                       : g_play.lead_count - 1];
    } else if (g_play.frames == 0) {
        return;
    } else {
        frame = &g_play.list[index < g_play.frames ? index : g_play.frames - 1];
    }
    for (pad = 0; pad < PORT_INPUT_PADS; pad++) {
        input->buttons[pad] = frame->buttons[pad];
        input->present[pad] = (frame->present >> (pad + PRESENT_CPU_SHIFT)) & 1
                                  ? PORT_PAD_CPU
                                  : (u8)((frame->present >> pad) & 1);
    }
    if (g_play.differ_at != NONE && g_port_input_count >= g_play.differ_at &&
        g_port_input_count - g_play.differ_at < DIFFER_INPUTS) {
        /* --replay-differ: as a game that went otherwise would have it,
         * the first pad's buttons the others for a second */
        input->buttons[0] = (u16)~input->buttons[0];
    }
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
    /* who won by the game's own state as the playback left it, not by
     * what the file says (tools/relay/disputes.py) */
    s32 winner = g_fight_winner;

    g_play.state = PLAY_OFF;
    if (g_play.from_boot) {
        port_RestorePlaySwitches();
        port_RestoreStageDefaults();
        port_RestoreMatch();
        port_ReleaseSessionGame();
        port_ReleaseFight();
    }
    g_play.in_lead = FALSE;
    port_StateLoad(g_play.back);
    port_RemasterHoldGameView(NULL);
    port_RemasterGameView(&view);
    port_RemasterHoldGameView(&view);
    if (trouble != NULL) {
        port_Log("replay: %s: %s\n", g_play.path, trouble);
    } else {
        port_Log("replay: %s: %u frames played, %u hashes the same as "
                 "recorded, the winner by its state %d\n",
                 g_play.path, g_play.frame, g_play.hashes_matched, winner);
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
    if (g_play.from_boot && (!g_lead.kept || !g_lead.plain)) {
        port_ReplayEnd("this run did not keep its state at power on, which a "
                       "replay of another build's is played from");
        return;
    }
    if (g_play.from_boot) {
        s32 slot;

        /* the run from power on: this run's own state at power on, the
         * recorded cards and setup in, as a session puts the host's in */
        port_KeepPlaySwitches();
        port_KeepStageDefaults();
        port_KeepMatch();
        port_ReplayKeep(TRUE);
        port_StateLoad(g_lead.boot);
        port_ReplayKeep(FALSE);
        for (slot = 0; slot < PORT_CARD_SLOTS; slot++) {
            port_SetCardImage(
                slot, ((g_play.card_flags >> slot) & 1)
                          ? g_play.cards + slot * PORT_CARD_IMAGE_SIZE
                          : NULL);
        }
        port_ReplayHoldSetup(g_play.setup);
        /* the fight its run was begun with, started as that run's was
         * (or none, where this run was begun with one) */
        port_ReplayHoldFight(g_play.fight ? g_play.fight_words : NULL);
        if (g_play.session) {
            /* an online fight's: the session as the game read it */
            port_HoldSessionGame(g_play.session_words[SESSION_RULES],
                                 g_play.session_words[SESSION_PADS],
                                 g_play.session_words[SESSION_SEED]);
        }
        g_port_take_hook = port_ReplayTakeHook;
        g_play.at_power_on = TRUE;
        g_play.state = PLAY_ON;
        port_Log("replay: %s: from power on (%s%s): %u inputs to the fight, "
                 "then %u frames, the console's hashes held%s\n",
                 g_play.path, g_play.session ? "its session's, " : "",
                 g_play.same_build  ? "as asked"
                 : g_play.same_game ? "the same game laid out otherwise"
                                    : "another build's game",
                 g_play.lead_count, g_play.frames,
                 g_play.same_build ? " and the state's" : "");
        return;
    }
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

static s32 port_ReplayPlayFrame(void);

/* A hash that is not the recorded one: a desync where the game is the one
 * that recorded the replay, and where it is another the game went
 * otherwise there. The playback ends; FALSE. */
static s32 port_ReplayWentOtherwise(
    const char *where, u32 at, u64 hash, u64 recorded) {
    static char trouble[PORT_REPLAY_WHY_BYTES];
    static char place[PORT_REPLAY_WHY_BYTES];

    port_ReplayText(place, sizeof(place), where, at);
    g_play.desync = TRUE;
    if (g_play.same_game) {
        port_ReplayText(trouble, sizeof(trouble),
                        "DESYNC %s: the state's hash is %08X%08X, recorded "
                        "%08X%08X",
                        place, (u32)(hash >> WORD_BITS), (u32)hash,
                        (u32)(recorded >> WORD_BITS), (u32)recorded);
    } else {
        port_ReplayText(trouble, sizeof(trouble),
                        "this build's game is not the one that recorded the "
                        "replay, and it went otherwise %s: stopped there",
                        place);
    }
    port_ReplayEnd(trouble);
    return FALSE;
}

/* A boundary of the run from power on: the setup's changes held, the
 * console's hash held to the one recorded, the input's view; at the
 * fight's start the state kept as the playback's start, and the fight's
 * first frame. FALSE where the playback ended here. */
static s32 port_ReplayLeadFrame(void) {
    u32 count = g_port_input_count;

    if (g_play.at_power_on) {
        /* the state at power on, put in: the first step is the game's
         * start, to its first read of the pads; a session's seed in
         * first, as its first frame puts it (netplay.c) */
        g_play.at_power_on = FALSE;
        if (g_play.session) {
            port_SessionGameSeed();
        }
        if (g_play.lead_count > 0) {
            port_RemasterHoldGameView(&g_play.lead_views[g_play.lead[0].view]);
            return TRUE;
        }
        /* a run begun with a fight chosen starts its fight before its
         * first input (--fight): its start is at input 0, after the
         * first step, as any input's boundary is */
        if (!g_play.session) {
            if (g_play.lead_view_count > 0) {
                port_RemasterHoldGameView(&g_play.lead_views[0]);
            }
            return TRUE;
        }
    }
    while (g_play.event_next < g_play.event_count &&
           g_play.events[g_play.event_next].count <= count) {
        port_ReplayHoldSetup(g_play.events[g_play.event_next].words);
        g_play.event_next++;
    }
    /* (a session's has no hash at input 0: netplay's first step takes the
     * game from power on through its first read of the pads and its first
     * input at once, with no boundary between, netplay.c) */
    if (count < g_play.lead_count && count % PORT_REPLAY_HASH_EVERY == 0 &&
        count / PORT_REPLAY_HASH_EVERY < g_play.lead_hash_count &&
        !(g_play.session && count == 0)) {
        u64 hash = port_StateConsoleHash();
        u64 recorded = g_play.lead_hashes[count / PORT_REPLAY_HASH_EVERY];

        if (hash != recorded) {
            return port_ReplayWentOtherwise(
                "before the fight, %u inputs after power on", count, hash,
                recorded);
        }
        g_play.hashes_matched++;
    }
    if (count >= g_play.lead_count) {
        u64 hash = port_StateConsoleHash();

        if (count != g_play.start_count || hash != g_play.start_console) {
            return port_ReplayWentOtherwise(
                "before the fight, at its start (input %u)", count, hash,
                g_play.start_console);
        }
        hash = g_play.same_build ? port_StateHash() : g_play.start_hash;
        if (hash != g_play.start_hash) {
            return port_ReplayWentOtherwise("at the fight's start (input %u)",
                                            count, hash, g_play.start_hash);
        }
        /* the fight's start, as this build has it: a restart and a skip
         * go back to it */
        port_StateSave(g_play.start);
        g_play.in_lead = FALSE;
        port_Log("replay: %s: the fight's start reached from power on, its "
                 "console's hash the one recorded\n",
                 g_play.path);
        return port_ReplayPlayFrame();
    }
    g_play.frame = 0;
    port_RemasterHoldGameView(&g_play.lead_views[g_play.lead[count].view]);
    return TRUE;
}

/* A resync of the fight at its frame `index`: its state put in, as the
 * session put the host's in there, and held to the host's hash; FALSE
 * where the playback ended here. */
static s32 port_ReplayResyncAt(u32 index) {
    static char trouble[PORT_REPLAY_WHY_BYTES];
    const u8 *serve;
    u32 serve_size;
    s32 resync;

    for (resync = 0; resync < g_play.resync_count; resync++) {
        u64 hash;
        u64 put_in;

        if (g_play.resync_at[resync] != index) {
            continue;
        }
        if (!port_FindChunk(
                g_play.resync_blob[resync], g_play.resync_size[resync],
                TAG_SERVE, &serve, &serve_size) ||
            serve_size < SERVE_HEAD_WORDS * WORD_BYTES ||
            !port_ReplayUnpackStartInto(
                g_play.resync_blob[resync], g_play.resync_size[resync],
                g_play.resync_raw, g_play.resync_state)) {
            port_ReplayEnd("its resync's state cannot be read");
            return FALSE;
        }
        hash = port_GetWord(serve + 2 * WORD_BYTES) |
               ((u64)port_GetWord(serve + 3 * WORD_BYTES) << WORD_BITS);
        port_ReplayKeep(TRUE);
        port_StateLoad(g_play.resync_state);
        port_ReplayKeep(FALSE);
        port_CardsForReplay();
        put_in = port_StateHash();
        if (put_in != hash) {
            g_play.desync = TRUE;
            port_ReplayText(trouble, sizeof(trouble),
                            "DESYNC at frame %u: the resync's state put in "
                            "has the hash %08X%08X, the host's %08X%08X",
                            index, (u32)(put_in >> WORD_BITS), (u32)put_in,
                            (u32)(hash >> WORD_BITS), (u32)hash);
            port_ReplayEnd(trouble);
            return FALSE;
        }
        if (!g_port_rerun) {
            port_Log("replay: %s: at frame %u, the resync's state put in, as "
                     "the session did\resync",
                     g_play.path, index);
        }
    }
    return TRUE;
}

/* Whether the hash of the fight's frame `index` is one a playback does not
 * hold: after the last every machine held the same before a resync, and
 * before the resync. */
static s32 port_ReplayUnheld(u32 index) {
    s32 resync;

    for (resync = 0; resync < g_play.resync_count; resync++) {
        if (index > g_play.resync_agreed[resync] &&
            index < g_play.resync_at[resync]) {
            return TRUE;
        }
    }
    return FALSE;
}

/* The state of the fight's frame `index` kept as a keyframe where it is
 * one's (port_ReplaySeek goes back to the nearest). */
static void port_ReplayKeyframe(u32 index) {
    s32 count = g_play.keyframe_count;
    s32 i;

    if (index == 0 || index % g_play.keyframe_every != 0 ||
        (count > 0 && g_play.keyframe_at[count - 1] >= index)) {
        return;
    }
    if (count == KEYFRAMES) {
        /* every other one let go: the rest twice as far apart */
        s32 kept = 0;

        g_play.keyframe_every *= 2;
        for (i = 0; i < count; i++) {
            if (g_play.keyframe_at[i] % g_play.keyframe_every == 0) {
                u8 *state = g_play.keyframe_state[kept];

                g_play.keyframe_state[kept] = g_play.keyframe_state[i];
                g_play.keyframe_state[i] = state;
                g_play.keyframe_at[kept] = g_play.keyframe_at[i];
                kept++;
            }
        }
        g_play.keyframe_count = count = kept;
        if (index % g_play.keyframe_every != 0) {
            return;
        }
    }
    if (g_play.keyframe_state[count] == NULL) {
        g_play.keyframe_state[count] = port_HostAllocate(port_StateSize());
        if (g_play.keyframe_state[count] == NULL) {
            return;
        }
    }
    port_StateSave(g_play.keyframe_state[count]);
    g_play.keyframe_at[count] = index;
    g_play.keyframe_count = count + 1;
}

/* A boundary of the playback: the frame's view held and its hash held to
 * the one recorded; FALSE where the playback ended here. */
static s32 port_ReplayPlayFrame(void) {
    static char trouble[PORT_REPLAY_WHY_BYTES];
    u32 index = g_port_input_count - g_play.start_count;

    if (g_play.in_lead) {
        return port_ReplayLeadFrame();
    }
    g_play.frame = index;
    if (!port_ReplayResyncAt(index)) {
        return FALSE;
    }
    if (port_ReplayUnheld(index)) {
        /* between the last hash every machine held the same and a resync:
         * where the fight's machines parted, which a playback cannot be */
        if (index >= g_play.frames || g_play.stop_asked) {
            port_ReplayEnd(NULL);
            return FALSE;
        }
        port_RemasterHoldGameView(&g_play.views[g_play.list[index].view]);
        return TRUE;
    }
    if (g_play.from_boot && index % PORT_REPLAY_HASH_EVERY == 0 &&
        index / PORT_REPLAY_HASH_EVERY < g_play.console_count) {
        u64 hash = port_StateConsoleHash();
        u64 recorded = g_play.consoles[index / PORT_REPLAY_HASH_EVERY];

        if (hash != recorded) {
            return port_ReplayWentOtherwise(
                "at frame %u of the fight", index, hash, recorded);
        }
    }
    if ((!g_play.from_boot || g_play.same_build) &&
        index % PORT_REPLAY_HASH_EVERY == 0 &&
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
    } else if (g_play.from_boot && index % PORT_REPLAY_HASH_EVERY == 0 &&
               index / PORT_REPLAY_HASH_EVERY < g_play.console_count) {
        g_play.hashes_matched++;
    }
    if (index >= g_play.frames || g_play.stop_asked) {
        port_ReplayEnd(NULL);
        return FALSE;
    }
    port_ReplayKeyframe(index);
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
    s32 nearest = -1;
    s32 i;

    /* the latest keyframe not past it, where it is nearer than now */
    for (i = 0; i < g_play.keyframe_count; i++) {
        if (g_play.keyframe_at[i] <= to) {
            nearest = i;
        }
    }
    if (nearest >= 0 &&
        (to < g_play.frame || g_play.keyframe_at[nearest] > g_play.frame)) {
        port_ReplayKeep(TRUE);
        port_StateLoad(g_play.keyframe_state[nearest]);
        port_ReplayKeep(FALSE);
        port_CardsForReplay();
        if (!port_ReplayPlayFrame()) {
            return FALSE;
        }
    } else if (to <= g_play.frame) {
        port_ReplayLoadStart();
        if (!port_ReplayPlayFrame()) {
            return FALSE;
        }
    }
    while (g_play.frame < to) {
        if (g_play.frame + PORT_THEATRE_LOOK_STEPS >= to) {
            /* the theatre's look at `to` runs from the boundary before */
            port_TheatreKeep();
        }
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
    if (g_play.in_lead) {
        /* the run from power on to the fight, unseen: all of it at once
         * where nothing is shown, in slices in the window, whose events
         * are read between two (a step shown between) */
        u32 began = port_HostTicks();

        g_play.seek_to = NONE;
        while (g_play.in_lead) {
            if (g_play.stop_asked) {
                port_ReplayEnd(NULL);
                return;
            }
            if (!g_play.quiet &&
                port_HostTicks() - began > LEAD_SLICE_MS * TICKS_PER_MS) {
                return;
            }
            if (!port_ReplayQuietStep()) {
                return;
            }
        }
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
        if (i + PORT_THEATRE_LOOK_STEPS * SPEED_ONE >= g_play.speed) {
            /* the theatre's look runs from the boundary before the step */
            port_TheatreKeep();
        }
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
    } else if (what == PORT_REPLAY_HOLD) {
        g_play.paused = TRUE;
    }
}

const char *port_ReplayPlayingPath(void) {
    return g_play.state != PLAY_OFF ? g_play.path : NULL;
}

void port_ReplaySeekTo(u32 frame) {
    if (g_play.state != PLAY_ON || g_play.in_lead) {
        return;
    }
    g_play.seek_to = frame < g_play.frames ? frame
                     : g_play.frames > 0   ? g_play.frames - 1
                                           : 0;
}

/* Where the playback is (port_replay.h). */
void port_GetReplayStatus(PortReplayStatus *status) {
    status->is_playing = g_play.state == PLAY_ON;
    status->is_paused = g_play.paused;
    status->speed = g_play.speed;
    status->frame = g_play.frame;
    status->frames = g_play.frames;
    status->kill = g_play.kill;
    status->desync = g_play.desync;
    status->from_boot = g_play.state == PLAY_ON && g_play.from_boot;
    status->in_lead = g_play.state == PLAY_ON && g_play.in_lead;
    status->lead_at = g_port_input_count;
    status->lead_of = g_play.lead_count;
}

void port_SetReplayFromBoot(s32 on) { g_from_boot_asked = on; }

void port_SetReplayDiffer(u32 input) { g_differ_asked = input; }

/* ---- the boundary ---- */

void port_ReplayBoundary(void) {
    PortGameView view;

    /* the player's record first: it counts nothing of a playback */
    port_StatsBoundary();
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
    port_LeadBoundary(g_port_input_count, &view);
    port_ReplayRecordBoundary(
        g_port_input_count, g_sys_screen == SCREEN_FIGHT_UPDATE, &view);
}
