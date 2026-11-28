/* Rollback netplay in the native port (docs/NETPLAY.md, sections 12, 13
 * and 18): programs of one build play the whole session, from power on,
 * as one machine with two or four pads, each program its own pad or pads
 * and the others' over the network (net.c). It is GGPO's way, written in
 * C here: neither GGPO (MIT, C++) nor GekkoNet (C++) can be linked into
 * the headless program, which has no C library.
 *
 * The host runs the game a step at a time (step.c). A step takes one
 * input, a PortInput of every pad, which this file puts in by the input's
 * hook. The local pads' buttons, as the pads' driver read them, are kept
 * for the frame --net-delay frames on and sent to the others with every
 * input of its own they have not yet acknowledged. Each other pad's,
 * until it comes, is taken to be what it was last; one that comes and is
 * not what was taken puts the state of its frame back (port_state.h) and
 * runs the frames from there to now again, quietly (g_port_rerun). No
 * program runs more than NETPLAY_PREDICT_MAX frames past the last frame
 * of which it has every pad's input.
 *
 * A session of four players on four PCs is a star (section 18): each
 * guest has one link, to the host, and the host one to each guest. The
 * host passes on each guest's inputs to the others with its own: to each
 * link go the inputs of every pad that link's program does not play, and
 * each datagram says how many of each pad's inputs its sender has. Two
 * players on each of two PCs are a session of two links' kind whose
 * programs play two pads each.
 *
 * Each program also keeps time with the others, as GGPO does: every
 * datagram says the frame its sender is at and how far ahead it reckons
 * itself, and the one that is ahead holds its window's blanks back a
 * little (g_port_net_hold_hook), so that none runs on far into what it
 * has to guess. A step that loaded something from the disc is a point all
 * wait at: the next frame is not run until every input up to it is in,
 * so that a load is not run again by a rollback.
 *
 * Every NETPLAY_HASH_EVERY frames, once every pad's input before it is
 * known, the state's hash at that frame is final; each program prints
 * it, sends it with its inputs, and stops on a hash of another's that is
 * not its own (a desync). The host holds each guest's to its own, and a
 * guest the host's: every program's to every other's, by way of the
 * host.
 *
 * A guest of four PCs who leaves a fight of four gives his fighter to the
 * CPU (section 19): the host, told or hearing nothing of him, plays his
 * pad on from the first frame of it it does not have as INPUT_CPU, which
 * it passes on with its own as any input; every program that comes to
 * that frame, the host's own among them, runs it again with the pad the
 * CPU's (PORT_PAD_CPU), and the game makes the fighter a CPU there
 * (duo.c). The session goes on for the others. */
#include "common.h"
#include "port_disc.h"
#include "port_host.h"
#include "port_net.h"
#include "port_ranked.h"
#include "port_remix.h"
#include "port_replay.h"
#include "port_room.h"
#include "bb2.h"

/* Frames the state is kept for, and the most run on what is guessed. */
#define NETPLAY_PREDICT_MAX 8
#define NETPLAY_SAVES (NETPLAY_PREDICT_MAX + 2)
/* Inputs kept: a ring, far larger than any delay or window. */
#define NETPLAY_INPUTS 1024
#define NETPLAY_INPUT_MASK (NETPLAY_INPUTS - 1)
/* The pads a session may have, and the most of one pad's inputs a
 * datagram carries. */
#define NETPLAY_PADS PORT_INPUT_PADS
#define NETPLAY_SEND_MAX 64
#define NETPLAY_DELAY_DEFAULT 2
#define NETPLAY_DELAY_MAX 8
#define NETPLAY_HASH_EVERY 30
#define NETPLAY_HASHES 64
/* How long a wait for the others polls, and when one has gone. */
#define NETPLAY_POLL_MS 1
/* A wait sends the inputs again this often. */
#define NETPLAY_RESEND_MS 16
#define NETPLAY_GIVE_UP_MS 15000
#define MS_PER_SECOND 1000
#define NETPLAY_BYES 5
#define NETPLAY_LINGER_MS 3000
/* A goodbye is taken this long after it came: the datagrams before it,
 * the sender's last inputs among them, may come after it (a network
 * that delays each by its own time), and a guest of four that ended on
 * the goodbye alone would end short of the frame its host ended at */
#define NETPLAY_BYE_GRACE_MS 250
#define NETPLAY_QUIET_MS 500
/* A wait longer than this is shown on the window's line. */
#define NETPLAY_WAIT_SHOWN_MS 100
/* A second ask to leave within this long leaves. */
#define NETPLAY_LEAVE_MS 3000
/* A spectator runs this many frames behind the newest it has, so that
 * the inputs' coming in bursts is not seen, and quietly while it is more
 * than this many behind (it came in the middle of a fight). */
#define WATCH_LAG 6
#define WATCH_CATCH_UP 45
/* ... and gives up the match's last frames this long after its end */
#define WATCH_END_WAIT_MS 5000
/* A step that read more sectors than this loaded something. */
#define NETPLAY_LOAD_SECTORS 64
/* Time sync: the frames each side reckons the other is ahead by, averaged
 * over this many; a hold of at least this many sixteenths of a frame,
 * and none for this many frames after one. */
#define NETPLAY_ADVANTAGES 32
#define NETPLAY_HOLD_MIN 16
#define NETPLAY_HOLD_MAX_FRAMES 4
#define NETPLAY_HOLD_EVERY 30
#define SIXTEENTHS 16
/* A vertical blank in microseconds (59.83 a second), and the blanks a
 * frame takes until there are frames to count. */
#define BLANK_MICROSECONDS 16714
#define MICROSECONDS_PER_MS 1000
#define BLANKS_PER_FRAME_START (2 * SIXTEENTHS)
/* The window's line shows the longest rollback of the last so many ms. */
#define NETPLAY_RECENT_MS 3000
/* The round trip and the blanks a frame takes are smoothed over about
 * this many. */
#define PING_SMOOTH 8
#define HUNDREDTHS 100
#define WORD_BYTES 4
#define HALF_BYTES 2
#define WORD_BITS 32
#define BITS_PER_BYTE 8
/* A resync (docs/NETPLAY.md, section 21): the host's state in chunks of
 * this many bytes, at most this many of them; a guest's acknowledgement
 * says the first it lacks and which of the next RESYNC_ACK_SPAN it has. */
#define RESYNC_CHUNK_BYTES 1024
#define RESYNC_CHUNKS_MAX 8192
#define RESYNC_MAP_BYTES (RESYNC_CHUNKS_MAX / BITS_PER_BYTE)
#define RESYNC_ACK_SPAN 256
/* Chunks sent and not yet acknowledged, at most, on a link; and a second
 * at most, on a direct way and through the lobby's relay, which passes
 * 240 datagrams a second of a pair on. */
#define RESYNC_WINDOW 64
#define RESYNC_RATE 3000
#define RESYNC_RATE_RELAYED 150
/* A chunk not acknowledged is sent again after twice the round trip and
 * this; a guest acknowledges after so many chunks or so long. */
#define RESYNC_RESEND_MS 40
#define RESYNC_ACK_EVERY 8
#define RESYNC_ACK_MS 20
/* While a resync's state goes over, the inputs are sent again this
 * often, to say each is still there. */
#define RESYNC_ALIVE_MS 100
/* Parted, the host resyncs by itself after this many frames (three
 * seconds); again so soon after the last, only at a press. A resync is at
 * a frame this far ahead of the host's when it says so: past every frame a
 * guest can have run on the host's inputs it had before (the most delay,
 * and the most frames run on a guess), so that every machine stops at
 * that frame's boundary, its own state and its plan there. */
#define RESYNC_LEAD (NETPLAY_DELAY_MAX + NETPLAY_PREDICT_MAX + 4)
#define RESYNC_AUTO_FRAMES 180
#define RESYNC_AGAIN_FRAMES 1200
/* A controller's press for a resync: Select held a second while parted
 * (Select is the game's no more, then: port_NetplayKeepLocal) */
#define RESYNC_HOLD_FRAMES 60
#define PERCENT 100
#define DECIMAL 10
#define RESYNC_LINE_BYTES 96
/* --net-part-at: the RAM's word so far below its top, and what is added */
#define PART_FROM_TOP 16
#define PART_BY 0x9E3779B9U

enum {
    KIND_INPUT = PORT_NET_KIND_NETPLAY,      /* inputs, acks and a hash */
    KIND_BYE = PORT_NET_KIND_NETPLAY + 1,    /* the other has ended */
    KIND_RESYNC = PORT_NET_KIND_NETPLAY + 2, /* a chunk of the host's state */
    KIND_RESYNC_ACK = PORT_NET_KIND_NETPLAY + 3 /* ... the chunks had */
};

/* An input datagram after the header: how many of each pad's inputs its
 * sender has, the frame of its last final hash and that hash, the frame
 * it is at, how far ahead of this program it reckons itself (sixteenths
 * of a frame), its clock, the last clock of this program's it had and how
 * long ago, the resyncs it has had (its hashes are of the state after the
 * last), a resync asked for (the resyncs it has had, plus one; 0: none),
 * the frame the host's next resync is at (0: none), how many pads' inputs
 * it carries; then for each, the pad, how many and
 * the frame of the first, and those inputs, a half-word each. */
enum {
    INPUT_HAS = PORT_NET_HEADER_BYTES,
    INPUT_HASH_FRAME = INPUT_HAS + NETPLAY_PADS * WORD_BYTES,
    INPUT_HASH = INPUT_HASH_FRAME + WORD_BYTES,
    INPUT_NOW = INPUT_HASH + 2 * WORD_BYTES,
    INPUT_AHEAD = INPUT_NOW + WORD_BYTES,
    INPUT_STAMP = INPUT_AHEAD + WORD_BYTES,
    INPUT_ECHO = INPUT_STAMP + WORD_BYTES,
    INPUT_HELD = INPUT_ECHO + WORD_BYTES,
    INPUT_EPOCH = INPUT_HELD + WORD_BYTES,
    INPUT_ASK = INPUT_EPOCH + WORD_BYTES,
    INPUT_RESYNC_AT = INPUT_ASK + WORD_BYTES,
    INPUT_BLOCKS = INPUT_RESYNC_AT + WORD_BYTES,
    INPUT_FIRST_BLOCK = INPUT_BLOCKS + WORD_BYTES
};

/* A chunk of a resync's state after the header (whose number is the
 * chunk's): the resync's number (the resyncs had, with it), the frame it
 * is put in at, the last frame before it whose hash all held the same,
 * the state's bytes, the host's build (port_StateBuildSum), then the
 * chunk's bytes. An acknowledgement's header's number is the first chunk
 * its sender lacks; after it, the resync's number and a bit for each of
 * the RESYNC_ACK_SPAN chunks after that one, set for one it has. */
enum {
    RESYNC_EPOCH = PORT_NET_HEADER_BYTES,
    RESYNC_FRAME = RESYNC_EPOCH + WORD_BYTES,
    RESYNC_AGREED = RESYNC_FRAME + WORD_BYTES,
    RESYNC_SIZE = RESYNC_AGREED + WORD_BYTES,
    RESYNC_BUILD = RESYNC_SIZE + WORD_BYTES,
    RESYNC_DATA = RESYNC_BUILD + 2 * WORD_BYTES
};

enum {
    ACK_EPOCH = PORT_NET_HEADER_BYTES,
    ACK_MAP = ACK_EPOCH + WORD_BYTES,
    ACK_BYTES = ACK_MAP + RESYNC_ACK_SPAN / BITS_PER_BYTE
};

/* A pad's block: its pad, how many, the first frame, then the inputs. */
enum { BLOCK_PAD = 0, BLOCK_COUNT = 2, BLOCK_FIRST = 4, BLOCK_INPUTS = 8 };

/* KIND_BYE's detail: the pad whose player has left, and one; and that
 * the session ended on a desync another found. */
#define BYE_PAD_MASK 0x0F
#define BYE_DESYNC 0x80

/* Where a pad's inputs come from: this program, a link, or nowhere (a
 * pad the session does not have). */
#define SOURCE_LOCAL (-1)
#define SOURCE_NONE (-2)
/* ... or the CPU's: the host plays on the pad of a guest who left a
 * session of four (docs/NETPLAY.md, section 19). */
#define SOURCE_CPU (-3)

/* A pad's input as netplay keeps, sends and feeds it from the frame the
 * CPU plays the pad on. No pad holds every button at once; a local pad
 * that did is let go of Select. */
#define INPUT_CPU 0xFFFF

typedef struct {
    u32 frame;
    u64 hash;
} PortNetplayHash;

/* One of the links (net.c): what its program has of each pad, its hashes
 * as it sent them (each final) and whether each was held to this
 * program's, and its time. */
typedef struct {
    u32 has[NETPLAY_PADS];
    PortNetplayHash hashes[NETPLAY_HASHES];
    u32 compared[NETPLAY_HASHES];
    u32 now;
    u32 stamp;
    u32 stamp_at;
    s32 has_stamp;
    u32 ping_ms;
    s32 has_ping;
    s32 ahead[NETPLAY_ADVANTAGES];
    s32 their_ahead[NETPLAY_ADVANTAGES];
    s32 their_ahead_now;
    u32 sent_at;
    s32 left;     /* said goodbye */
    s32 left_pad; /* the pad whose player it said had gone, or -1 */
    s32 desync;   /* ... on a desync */
    s32 taken;    /* its pads given to the CPU: the session goes on */
    s32 bye;      /* a goodbye come, not yet taken (NETPLAY_BYE_GRACE_MS) */
    u32 bye_at;
    s32 bye_pad;
    s32 bye_desync;
    u32 agreed; /* the last hashed frame whose hash was this program's */
    /* a resync's state going to it: the chunks it has said it has, and
     * when it last said anything of them */
    u32 resync_had;
    u32 resync_heard_at;
    u32 resync_next;
} PortNetplayLink;

static s32 g_np_on;
/* The pads of the session (2 or 4), this program's (a bit each; the
 * first of them), where each comes from (SOURCE_..., or a link), and the
 * links. */
static s32 g_np_pads = 2;
static u32 g_np_local_mask = 1;
static s32 g_np_local;
static s32 g_np_source[NETPLAY_PADS] = {
    SOURCE_LOCAL, 0, SOURCE_NONE, SOURCE_NONE};
static s32 g_np_links = 1;
static PortNetplayLink g_np_link[PORT_NET_LINKS];
static u32 g_np_delay = NETPLAY_DELAY_DEFAULT;
static s32 g_np_goes_on_alone;
/* The frame whose input the game takes next. */
static u32 g_np_frame;
/* Each pad's inputs, by frame, and how many of its frames are known (the
 * local pads' all that this program has kept). */
static u16 g_np_inputs[NETPLAY_PADS][NETPLAY_INPUTS];
static u32 g_np_count[NETPLAY_PADS];
static u32 g_np_local_count;
/* What was taken for another's input of each frame run on a guess, and
 * which of its buttons the frame's input depended on (the session's own
 * presses depend on none: port_SessionInput). */
static u16 g_np_guessed[NETPLAY_PADS][NETPLAY_INPUTS];
static u16 g_np_used[NETPLAY_INPUTS];
/* The earliest frame whose guess turned out wrong, or none. */
static u32 g_np_wrong_from;
static s32 g_np_is_wrong;
/* Another has ended (its link said goodbye). */
static s32 g_np_peer_left;
/* The pads the CPU plays (a bit each), the last it took and when this
 * program learned of it, for the window's line. */
static u32 g_np_cpu_mask;
static s32 g_np_cpu_pad = -1;
static u32 g_np_cpu_at;
/* The state at the boundary of each of the last frames, and the blank
 * the game was at there. */
static u8 *g_np_saves[NETPLAY_SAVES];
static u32 g_np_save_frames[NETPLAY_SAVES];
static u32 g_np_save_blanks[NETPLAY_SAVES];
/* The hashes of the frames that are hashed: this program's, final from
 * g_np_last_final back. */
static PortNetplayHash g_np_hashes[NETPLAY_HASHES];
static PortNetplayHash g_np_last_final;
static u32 g_np_hashes_compared;
/* The input of the step about to run. */
static PortInput g_np_input;
/* A load: the frame after which the next waits for the others' inputs. */
static u32 g_np_sync_until;
/* Time: the blanks a frame takes (sixteenths), the count of the turns
 * each link's advantage was kept at, and when this one last held back. */
static u32 g_np_blanks_per_frame = BLANKS_PER_FRAME_START;
static u32 g_np_ahead_count;
static u32 g_np_held_at;
static u32 g_np_held_ms;
static u32 g_np_holds;
/* how far apart the two were, for the report: sixteenths, summed */
static u32 g_np_ahead_sum;
/* What the report and the window's line say. */
static u32 g_np_rollbacks;
static u32 g_np_frames_again;
static u32 g_np_longest;
static u32 g_np_recent;
static u32 g_np_recent_at;
static u32 g_np_waits;
static u32 g_np_load_waits;
static u32 g_np_rerun_ticks;
static s32 g_np_state;
static u32 g_np_wait_from;
static u32 g_np_alone_at;
/* A session begun in the running program (online.c): its end is not the
 * program's. Why it ended, PORT_NETPLAY_END_..., or 0 while it goes on. */
static s32 g_np_in_process;
static s32 g_np_ended;
/* The player leaves: asked once (when, for the window's line), then
 * again to leave; --leave-at's frame. --net-plan-pads. */
static u32 g_np_leave_asked_at;
static s32 g_np_leave;
static u32 g_np_leave_at;
static s32 g_np_own_pad_only;
/* A room's match's host: the frames whose inputs are final and sent to
 * the match's feed (watch.c). A spectator: watching, the frames it has,
 * and its own hashes held to the players'. */
static u32 g_np_fed;
/* ... and a ranked match's, to its digest (ranked.c) */
static u32 g_np_ranked_fed;
static s32 g_np_watch;
static u32 g_np_watch_known;
static PortNetplayHash g_np_watch_hashes[NETPLAY_HASHES];
static u32 g_np_watch_held;
/* ... and, parted from the players' (a hash that was not theirs), the
 * frame of that hash, and when: it waits for a resync's state (or the
 * next fight's start) the lobby has from the match's host */
static s32 g_np_watch_parted;
static u32 g_np_watch_parted_frame;
static u32 g_np_watch_parted_at;
static u32 g_np_watch_agreed;

/* A resync (docs/NETPLAY.md, section 21). The resyncs had in the session,
 * the frame of the last (no hash before it is held to another's), and
 * how many frames back it was. Parted: a hash of another's was not this
 * program's own, at that hashed frame and this program's frame then; the
 * last hashed frame every link held the same. Asked for by this player
 * (F5, --resync-ask-at), or by a guest (the host). By itself so many
 * frames after the parting (0: only at a press). --net-part-at. */
static u32 g_np_epoch;
static u32 g_np_resync_frame;
static u32 g_np_resynced_at;
static s32 g_np_parted;
static u32 g_np_parted_hash;
static u32 g_np_parted_frame;
static u32 g_np_parted_clock;
static s32 g_np_resync_asked;
static u32 g_np_resync_ask_at;
/* the local frames Select has been held in, parted */
static u32 g_np_select_held;
static s32 g_np_resync_wanted;
static u32 g_np_resync_auto = RESYNC_AUTO_FRAMES;
/* the frame the next resync is at, the host's word (0: none) */
static u32 g_np_resync_at;
static u32 g_np_part_at;
static s32 g_np_part_done;
/* the frame the last resync was put in at, for the next's by itself */
static u32 g_np_resyncs_at_last;

/* The state going over: its number, frame, last frame all held the same,
 * bytes and chunks; on a guest, the chunks it has (a bit each), how many,
 * when it last had one and last acknowledged them, and whether all have
 * come; and a figure of how far it has got, for the window's line. */
static struct {
    s32 coming; /* a guest: a chunk of a new one has come */
    s32 sending;
    u32 epoch;
    u32 frame;
    u32 agreed;
    u32 size;
    u32 count;
    const u8 *blob;
    u8 *room;
    u32 room_bytes;
    u8 map[RESYNC_MAP_BYTES];
    u32 have;
    u32 had_at;
    u32 acked_at;
    u32 unacked;
    u32 began_at;
    u32 percent;
} g_np_rs;

/* The host: each link's chunks it has said it has (a bit each), and when
 * each was last sent to it. */
static u8 g_np_rs_had[PORT_NET_LINKS][RESYNC_MAP_BYTES];
static u32 g_np_rs_sent[PORT_NET_LINKS][RESYNC_CHUNKS_MAX];

void (*g_port_net_hold_hook)(u32 blanks);
void (*g_port_net_wait_hook)(const char *status);
void (*g_port_net_fail_hook)(const char *why);
void (*g_port_net_rerun_hook)(s32 begins, u32 blanks);

/* --netplay: two programs, a pad each, one link between them. */
void port_SetNetplay(s32 pad, u32 delay) {
    s32 sources[NETPLAY_PADS] = {
        SOURCE_LOCAL, SOURCE_LOCAL, SOURCE_NONE, SOURCE_NONE};

    sources[1 - pad] = 0;
    port_SetNetplayPads(2, sources);
    g_np_on = 1;
    g_np_delay = delay > NETPLAY_DELAY_MAX ? NETPLAY_DELAY_MAX : delay;
}

/* The session's pads and where each comes from (port_net.h). */
void port_SetNetplayPads(s32 pads, const s32 *sources) {
    s32 pad;

    g_np_pads = pads == NETPLAY_PADS ? NETPLAY_PADS : 2;
    g_np_local_mask = 0;
    g_np_local = -1;
    g_np_links = 1;
    for (pad = 0; pad < NETPLAY_PADS; pad++) {
        g_np_source[pad] =
            pad < g_np_pads && sources != NULL ? sources[pad] : SOURCE_NONE;
        if (g_np_source[pad] == SOURCE_LOCAL) {
            g_np_local_mask |= 1U << pad;
            if (g_np_local < 0) {
                g_np_local = pad;
            }
        } else if (g_np_source[pad] >= 0 && g_np_source[pad] + 1 > g_np_links) {
            g_np_links = g_np_source[pad] + 1;
        }
    }
    if (g_np_local < 0) {
        g_np_local = 0;
    }
}

s32 port_IsNetplayOn(void) { return g_np_on; }

s32 port_NetplayPads(void) { return g_np_pads; }

void port_SetNetplayGoesOnAlone(s32 on) { g_np_goes_on_alone = on; }

void port_SetNetplayInProcess(s32 on) { g_np_in_process = on; }

void port_SetNetplayOwnPadOnly(s32 on) { g_np_own_pad_only = on; }

void port_SetNetplayLeaveAt(u32 frame) { g_np_leave_at = frame; }

void port_SetNetplayResyncAuto(u32 frames) { g_np_resync_auto = frames; }

void port_SetNetplayPartAt(u32 frame) { g_np_part_at = frame; }

void port_SetNetplayResyncAskAt(u32 frame) { g_np_resync_ask_at = frame; }

/* The player asks for a resync (F5): heard only where this program has
 * found the session's machines parted, or a spectator its players'. */
void port_NetplayAskResync(void) {
    if (g_np_on && g_np_parted && !g_np_resync_asked) {
        port_Log(
            "netplay: this player asks for a resync at frame %u\n", g_np_frame);
        g_np_resync_asked = TRUE;
    }
}

void port_NetplayLeave(void) {
    if (g_np_on) {
        g_np_leave = TRUE;
    }
}

/* A leave asked (the window's keys): the first ask is shown on the line,
 * and a second within NETPLAY_LEAVE_MS leaves. */
void port_NetplayAskLeave(void) {
    u32 now = port_NetClock();

    if (!g_np_on) {
        return;
    }
    if (g_np_leave_asked_at != 0 &&
        now - g_np_leave_asked_at <= NETPLAY_LEAVE_MS) {
        port_Log(
            "netplay: the player leaves the session at frame %u\n", g_np_frame);
        g_np_leave = TRUE;
        return;
    }
    g_np_leave_asked_at = now == 0 ? 1 : now;
}

u32 port_NetplayFrameNow(void) { return g_np_frame; }

/* The pad whose player left the session (port_net.h). */
s32 port_NetplayLeftPad(void) {
    s32 link;

    for (link = 0; link < g_np_links; link++) {
        if (g_np_link[link].left && !g_np_link[link].taken &&
            g_np_link[link].left_pad >= 0) {
            return g_np_link[link].left_pad;
        }
    }
    return -1;
}

u32 port_NetplayCpuPads(void) { return g_np_cpu_mask; }

u32 port_NetplayLocalPads(void) { return g_np_local_mask; }

static void port_NetplayClearLink(PortNetplayLink *link) {
    u32 i;

    for (i = 0; i < sizeof(*link); i++) {
        ((u8 *)link)[i] = 0;
    }
    link->left_pad = -1;
}

/* Everything of a session that the next one must not find (port_net.h):
 * the inputs, the guesses, the hashes, the time and the counts. The
 * states' buffers are kept for it. */
void port_ResetNetplay(void) {
    u32 i;
    s32 pad;

    g_np_on = 0;
    port_SetNetplayPads(2, NULL);
    g_np_source[0] = SOURCE_LOCAL;
    g_np_source[1] = 0;
    g_np_local_mask = 1;
    g_np_delay = NETPLAY_DELAY_DEFAULT;
    g_np_frame = 0;
    g_np_local_count = 0;
    g_np_wrong_from = 0;
    g_np_is_wrong = 0;
    g_np_peer_left = 0;
    g_np_cpu_mask = 0;
    g_np_cpu_pad = -1;
    g_np_cpu_at = 0;
    for (pad = 0; pad < NETPLAY_PADS; pad++) {
        g_np_count[pad] = 0;
        for (i = 0; i < NETPLAY_INPUTS; i++) {
            g_np_inputs[pad][i] = 0;
            g_np_guessed[pad][i] = 0;
        }
    }
    for (i = 0; i < NETPLAY_INPUTS; i++) {
        g_np_used[i] = 0;
    }
    for (pad = 0; pad < PORT_NET_LINKS; pad++) {
        port_NetplayClearLink(&g_np_link[pad]);
    }
    for (i = 0; i < NETPLAY_SAVES; i++) {
        g_np_save_frames[i] = (u32)-1;
        g_np_save_blanks[i] = 0;
    }
    for (i = 0; i < NETPLAY_HASHES; i++) {
        g_np_hashes[i].frame = 0;
        g_np_hashes[i].hash = 0;
    }
    g_np_last_final.frame = 0;
    g_np_last_final.hash = 0;
    g_np_hashes_compared = 0;
    g_np_sync_until = 0;
    g_np_blanks_per_frame = BLANKS_PER_FRAME_START;
    g_np_ahead_count = 0;
    g_np_held_at = 0;
    g_np_held_ms = 0;
    g_np_holds = 0;
    g_np_ahead_sum = 0;
    g_np_rollbacks = 0;
    g_np_frames_again = 0;
    g_np_longest = 0;
    g_np_recent = 0;
    g_np_recent_at = 0;
    g_np_waits = 0;
    g_np_load_waits = 0;
    g_np_rerun_ticks = 0;
    g_np_state = PORT_NETPLAY_PLAYING;
    g_np_wait_from = 0;
    g_np_alone_at = 0;
    g_np_in_process = 0;
    g_np_ended = 0;
    g_np_leave_asked_at = 0;
    g_np_leave = FALSE;
    g_np_leave_at = 0;
    g_np_fed = 0;
    g_np_ranked_fed = 0;
    g_np_watch = FALSE;
    g_np_watch_known = 0;
    g_np_watch_held = 0;
    for (i = 0; i < NETPLAY_HASHES; i++) {
        g_np_watch_hashes[i].frame = (u32)-1;
    }
    g_np_watch_parted = FALSE;
    g_np_watch_agreed = 0;
    g_np_epoch = 0;
    g_np_resync_frame = 0;
    g_np_resynced_at = 0;
    g_np_parted = FALSE;
    g_np_resync_asked = FALSE;
    g_np_resync_wanted = FALSE;
    g_np_resync_at = 0;
    g_np_select_held = 0;
    g_np_part_done = FALSE;
    g_np_resyncs_at_last = 0;
    g_np_rs.coming = FALSE;
    g_np_rs.sending = FALSE;
    g_np_rs.epoch = 0;
    for (pad = 0; pad < NETPLAY_PADS; pad++) {
        g_np_input.buttons[pad] = 0;
        g_np_input.present[pad] = 0;
    }
    g_port_take_hook = NULL;
    g_port_movies_skipped = FALSE;
}

/* ---- what is known ---- */

/* The frames of which every pad's input is known: all before it. */
static u32 port_NetplayKnown(void) {
    u32 known = g_np_local_count;
    s32 pad;

    if (g_np_watch) {
        return g_np_watch_known;
    }
    for (pad = 0; pad < g_np_pads; pad++) {
        if (g_np_source[pad] >= 0 && g_np_count[pad] < known) {
            known = g_np_count[pad];
        }
    }
    return known;
}

/* ---- the datagrams ---- */

/* To link `link`: the inputs it does not have yet of every pad its
 * program does not play (this program's, and on a host the other guests'
 * it passes on), how many of each pad's this program has, its last final
 * hash, and its time. */
static void port_NetplaySendTo(s32 link) {
    PortNetplayLink *to = &g_np_link[link];
    u8 out[PORT_NET_DATAGRAM_MAX];
    u32 now = port_NetClock();
    u32 at = INPUT_FIRST_BLOCK;
    u32 blocks = 0;
    s32 pad;

    port_NetHeader(out, KIND_INPUT, 0, 0);
    for (pad = 0; pad < NETPLAY_PADS; pad++) {
        port_NetPutWord(out + INPUT_HAS + pad * WORD_BYTES, g_np_count[pad]);
    }
    port_NetPutWord(out + INPUT_HASH_FRAME, g_np_last_final.frame);
    port_NetPutWord(out + INPUT_HASH, (u32)g_np_last_final.hash);
    port_NetPutWord(out + INPUT_HASH + WORD_BYTES,
                    (u32)(g_np_last_final.hash >> WORD_BITS));
    port_NetPutWord(out + INPUT_NOW, g_np_frame);
    port_NetPutWord(
        out + INPUT_AHEAD,
        (u32)(g_np_ahead_count != 0
                  ? to->ahead[(g_np_ahead_count - 1) % NETPLAY_ADVANTAGES]
                  : 0));
    port_NetPutWord(out + INPUT_STAMP, now);
    port_NetPutWord(out + INPUT_ECHO, to->stamp);
    port_NetPutWord(out + INPUT_HELD, to->has_stamp ? now - to->stamp_at : 0);
    port_NetPutWord(out + INPUT_EPOCH, g_np_epoch);
    port_NetPutWord(out + INPUT_ASK, g_np_resync_asked ? g_np_epoch + 1 : 0);
    port_NetPutWord(out + INPUT_RESYNC_AT, g_np_resync_at);
    for (pad = 0; pad < g_np_pads; pad++) {
        u32 first = to->has[pad];
        u32 count;
        u32 i;

        if (g_np_source[pad] == link || g_np_source[pad] == SOURCE_NONE) {
            continue;
        }
        if (first > g_np_count[pad]) {
            first = g_np_count[pad];
        }
        count = g_np_count[pad] - first;
        if (count > NETPLAY_SEND_MAX) {
            first = g_np_count[pad] - NETPLAY_SEND_MAX;
            count = NETPLAY_SEND_MAX;
        }
        out[at + BLOCK_PAD] = (u8)pad;
        out[at + BLOCK_PAD + 1] = 0;
        out[at + BLOCK_COUNT] = (u8)count;
        out[at + BLOCK_COUNT + 1] = (u8)(count >> BITS_PER_BYTE);
        port_NetPutWord(out + at + BLOCK_FIRST, first);
        for (i = 0; i < count; i++) {
            u16 input = g_np_inputs[pad][(first + i) & NETPLAY_INPUT_MASK];

            out[at + BLOCK_INPUTS + HALF_BYTES * i] = (u8)input;
            out[at + BLOCK_INPUTS + HALF_BYTES * i + 1] =
                (u8)(input >> BITS_PER_BYTE);
        }
        at += BLOCK_INPUTS + HALF_BYTES * count;
        blocks++;
    }
    port_NetPutWord(out + INPUT_BLOCKS, blocks);
    port_NetSelectLink(link);
    port_NetSend(out, at);
    port_NetSelectLink(0);
    to->sent_at = now;
}

static void port_NetplaySendInputs(void) {
    s32 link;

    for (link = 0; link < g_np_links; link++) {
        if (!g_np_link[link].left) {
            port_NetplaySendTo(link);
        }
    }
}

static u32 port_NetplayAgreed(void);

/* Link `link`'s hash of `frame` against this program's own, once both
 * are final. This program's is not before every input ahead of the frame
 * is known and the rollback a wrong guess among them asks for is done: a
 * hash of the other's can come in the datagram, or the burst of them,
 * that brings the input which shows this program's own to have been made
 * on a wrong guess (docs/NETPLAY.md, section 12, "Under load"). */
static void port_NetplayCompareHash(s32 link, u32 frame) {
    PortNetplayLink *from = &g_np_link[link];
    s32 slot = (s32)((frame / NETPLAY_HASH_EVERY) % NETPLAY_HASHES);
    const PortNetplayHash *mine = &g_np_hashes[slot];
    const PortNetplayHash *theirs = &from->hashes[slot];

    if (frame == 0 || frame > g_np_last_final.frame || mine->frame != frame ||
        theirs->frame != frame || from->compared[slot] == frame ||
        frame < g_np_resync_frame || g_np_parted) {
        return;
    }
    from->compared[slot] = frame;
    g_np_hashes_compared++;
    if (mine->hash != theirs->hash) {
        port_Log("netplay: DESYNC at frame %u: this program's state hash is "
                 "%08X%08X, the other's %08X%08X%s\n",
                 mine->frame, (u32)(mine->hash >> WORD_BITS), (u32)mine->hash,
                 (u32)(theirs->hash >> WORD_BITS), (u32)theirs->hash,
                 g_np_links > 1 ? " (a guest's)" : "");
        /* the session goes on, parted, until a resync puts the host's
         * state on every machine (docs/NETPLAY.md, section 21) */
        g_np_parted = TRUE;
        g_np_parted_hash = frame;
        g_np_parted_frame = g_np_frame;
        g_np_parted_clock = port_NetClock();
        /* the state the spectators are told the resync's from (replay.c) */
        port_ReplayHoldBase(port_NetplayAgreed());
        port_Log("netplay: the machines' games parted after frame %u; a "
                 "resync puts the host's state on each (%s)\n",
                 port_NetplayAgreed(),
                 g_np_resync_auto != 0 ? "by itself in a moment, or F5" : "F5");
        return;
    }
    if (frame > from->agreed) {
        from->agreed = frame;
    }
}

/* The last hashed frame every link's hash was this program's of (none
 * before the last resync's frame is held). */
static u32 port_NetplayAgreed(void) {
    u32 agreed = (u32)-1;
    s32 link;

    for (link = 0; link < g_np_links; link++) {
        /* (none held since the last resync: its own frame, whose state
         * all had) */
        u32 of = g_np_link[link].agreed > g_np_resync_frame
                     ? g_np_link[link].agreed
                     : g_np_resync_frame;

        if (!g_np_link[link].left && of < agreed) {
            agreed = of;
        }
    }
    return agreed == (u32)-1 ? g_np_resync_frame : agreed;
}

/* The last frame all held the same, and whether this program has
 * parted (port_net.h). */
u32 port_NetplayAgreedFrame(void) { return port_NetplayAgreed(); }

s32 port_NetplayIsParted(void) { return g_np_parted; }

static void port_NetplayCompareAll(u32 frame) {
    s32 link;

    for (link = 0; link < g_np_links; link++) {
        port_NetplayCompareHash(link, frame);
    }
}

/* A link's time, from one of its datagrams: the round trip, and the
 * frame its program is at. */
static void port_NetplayTakeTime(PortNetplayLink *from, const u8 *in) {
    u32 now = port_NetClock();
    u32 echo = port_NetGetWord(in + INPUT_ECHO);
    u32 held = port_NetGetWord(in + INPUT_HELD);
    u32 their_now = port_NetGetWord(in + INPUT_NOW);

    from->stamp = port_NetGetWord(in + INPUT_STAMP);
    from->stamp_at = now;
    from->has_stamp = 1;
    if (echo != 0 && now - echo >= held) {
        u32 trip = now - echo - held;

        from->ping_ms =
            from->has_ping
                ? (from->ping_ms * (PING_SMOOTH - 1) + trip) / PING_SMOOTH
                : trip;
        from->has_ping = 1;
    }
    if (their_now >= from->now) {
        from->now = their_now;
    }
    from->their_ahead_now = (s32)port_NetGetWord(in + INPUT_AHEAD);
}

/* Pad `pad`'s player has left, and the CPU plays it from `frame` on: for
 * the log, and the window's line. */
static void port_NetplayCpuNoted(s32 pad, u32 frame, const char *how) {
    if (g_np_cpu_mask & (1U << pad)) {
        return;
    }
    g_np_cpu_mask |= 1U << pad;
    g_np_cpu_pad = pad;
    g_np_cpu_at = port_NetClock();
    port_Log("netplay: player %d %s: the CPU plays his fighter from frame "
             "%u on (player %d); the session goes on\n",
             pad + 1, how, frame, pad + 1);
}

/* One pad's inputs of a datagram: those that are new, and a wrong guess
 * among them noted. */
static void port_NetplayTakePad(s32 pad, u32 first, u32 count, const u8 *at) {
    u32 i;

    for (i = 0; i < count; i++) {
        u32 frame = first + i;
        u16 input;

        if (frame != g_np_count[pad]) {
            continue;
        }
        input = (u16)(at[HALF_BYTES * i] |
                      (at[HALF_BYTES * i + 1] << BITS_PER_BYTE));
        g_np_inputs[pad][frame & NETPLAY_INPUT_MASK] = input;
        g_np_count[pad]++;
        if (input == INPUT_CPU) {
            port_NetplayCpuNoted(pad, frame, "has left the session");
        }
        /* a frame already run on a guess that was not this, where the
         * frame's input depended on what differs, or where the guess
         * and the input differ in whose the pad is */
        if (frame < g_np_frame &&
            (((g_np_guessed[pad][frame & NETPLAY_INPUT_MASK] ^ input) &
              g_np_used[frame & NETPLAY_INPUT_MASK]) != 0 ||
             (g_np_guessed[pad][frame & NETPLAY_INPUT_MASK] == INPUT_CPU) !=
                 (input == INPUT_CPU)) &&
            (!g_np_is_wrong || frame < g_np_wrong_from)) {
            g_np_wrong_from = frame;
            g_np_is_wrong = 1;
        }
    }
}

/* ---- a resync's state over the links (docs/NETPLAY.md, section 21) ---- */

/* The machines parted and no resync could put them together again: the
 * session ends on the desync, as every one did before there were
 * resyncs (the headless program, not begun in the running one, ends). */
static void port_NetplayEndDesync(void) {
    port_Log("netplay: DESYNC: no resync could be made; the session ends\n");
    if (!g_np_in_process) {
        port_Quit(PORT_EXIT_FAILED);
    }
    g_np_ended = PORT_NETPLAY_END_DESYNC;
}

static s32 port_MapHas(const u8 *map, u32 chunk) {
    return (map[chunk / BITS_PER_BYTE] >> (chunk % BITS_PER_BYTE)) & 1;
}

static void port_MapSet(u8 *map, u32 chunk) {
    map[chunk / BITS_PER_BYTE] |= (u8)(1 << (chunk % BITS_PER_BYTE));
}

/* A guest: what it has of the state coming, to the host. */
static void port_NetplayAckChunks(void) {
    u8 out[ACK_BYTES];
    u32 first = 0;
    u32 i;

    while (first < g_np_rs.count && port_MapHas(g_np_rs.map, first)) {
        first++;
    }
    port_NetHeader(out, KIND_RESYNC_ACK, 0, first);
    port_NetPutWord(out + ACK_EPOCH, g_np_rs.epoch);
    for (i = 0; i < RESYNC_ACK_SPAN / BITS_PER_BYTE; i++) {
        out[ACK_MAP + i] = 0;
    }
    for (i = 0; i < RESYNC_ACK_SPAN && first + i < g_np_rs.count; i++) {
        if (port_MapHas(g_np_rs.map, first + i)) {
            port_MapSet(out + ACK_MAP, i);
        }
    }
    port_NetSelectLink(0);
    port_NetSend(out, sizeof(out));
    g_np_rs.acked_at = port_NetClock();
    g_np_rs.unacked = 0;
}

/* A guest: a chunk of the host's state. The first of a resync this
 * program has not had begins it; one of a resync it has had already is
 * acknowledged again (the host may not have heard it had them all). */
static void port_NetplayTakeChunk(
    s32 link, const u8 *in, s32 bytes, u32 chunk) {
    u32 epoch;
    u32 size;
    u32 count;
    u32 length;
    u32 i;

    if (link != 0 || g_np_links > 1 || (g_np_local_mask & 1) ||
        bytes <= RESYNC_DATA) {
        return;
    }
    epoch = port_NetGetWord(in + RESYNC_EPOCH);
    size = port_NetGetWord(in + RESYNC_SIZE);
    count = (size + RESYNC_CHUNK_BYTES - 1) / RESYNC_CHUNK_BYTES;
    if (epoch <= g_np_epoch) {
        if (epoch == g_np_epoch && epoch == g_np_rs.epoch) {
            port_NetplayAckChunks();
        }
        return;
    }
    if (size == 0 || count > RESYNC_CHUNKS_MAX || chunk >= count) {
        return;
    }
    if (!g_np_rs.coming || g_np_rs.epoch != epoch) {
        u64 build =
            port_NetGetWord(in + RESYNC_BUILD) |
            ((u64)port_NetGetWord(in + RESYNC_BUILD + WORD_BYTES) << WORD_BITS);

        if (build != port_StateBuildSum()) {
            /* a state this program cannot put in: the session ends on
             * the desync, as it did before there were resyncs */
            port_Log("netplay: the host's state for the resync is another "
                     "build's: it cannot be put in\n");
            g_np_rs.coming = FALSE;
            port_NetplayEndDesync();
            return;
        }
        if (g_np_rs.room_bytes < size) {
            g_np_rs.room = port_HostAllocate(size);
            g_np_rs.room_bytes = g_np_rs.room != NULL ? size : 0;
        }
        if (g_np_rs.room == NULL) {
            port_Log(
                "netplay: no memory for the resync's state (%u bytes)\n", size);
            port_NetplayEndDesync();
            return;
        }
        g_np_rs.coming = TRUE;
        g_np_rs.epoch = epoch;
        g_np_rs.frame = port_NetGetWord(in + RESYNC_FRAME);
        if (g_np_resync_at == 0) {
            g_np_resync_at = g_np_rs.frame;
        }
        g_np_rs.agreed = port_NetGetWord(in + RESYNC_AGREED);
        g_np_rs.size = size;
        g_np_rs.count = count;
        g_np_rs.have = 0;
        g_np_rs.unacked = 0;
        g_np_rs.began_at = port_NetClock();
        g_np_rs.acked_at = g_np_rs.began_at;
        for (i = 0; i < RESYNC_MAP_BYTES; i++) {
            g_np_rs.map[i] = 0;
        }
        port_Log("netplay: the host's state for resync %u is coming: frame "
                 "%u, %u bytes\n",
                 epoch, g_np_rs.frame, size);
    }
    g_np_rs.had_at = port_NetClock();
    length = chunk + 1 == count ? size - chunk * RESYNC_CHUNK_BYTES
                                : RESYNC_CHUNK_BYTES;
    if ((u32)bytes != RESYNC_DATA + length) {
        return;
    }
    if (!port_MapHas(g_np_rs.map, chunk)) {
        for (i = 0; i < length; i++) {
            g_np_rs.room[chunk * RESYNC_CHUNK_BYTES + i] = in[RESYNC_DATA + i];
        }
        port_MapSet(g_np_rs.map, chunk);
        g_np_rs.have++;
    }
    g_np_rs.unacked++;
    if (g_np_rs.unacked >= RESYNC_ACK_EVERY || g_np_rs.have == count) {
        port_NetplayAckChunks();
    }
}

/* The host: link `link`'s word on the chunks it has. */
static void port_NetplayTakeAck(s32 link, const u8 *in, s32 bytes, u32 first) {
    PortNetplayLink *from = &g_np_link[link];
    u32 i;

    if (!g_np_rs.sending || bytes < ACK_BYTES ||
        port_NetGetWord(in + ACK_EPOCH) != g_np_rs.epoch) {
        return;
    }
    from->resync_heard_at = port_NetClock();
    for (i = 0; i < first && i < g_np_rs.count; i++) {
        if (!port_MapHas(g_np_rs_had[link], i)) {
            port_MapSet(g_np_rs_had[link], i);
            from->resync_had++;
        }
    }
    for (i = 0; i < RESYNC_ACK_SPAN && first + i < g_np_rs.count; i++) {
        if (port_MapHas(in + ACK_MAP, i) &&
            !port_MapHas(g_np_rs_had[link], first + i)) {
            port_MapSet(g_np_rs_had[link], first + i);
            from->resync_had++;
        }
    }
}

/* One datagram of link `link`'s: what it has, its hash, its time and its
 * inputs, each pad's only where that pad comes from that link. */
static void port_NetplayTake(s32 link, const u8 *in, s32 bytes) {
    PortNetplayLink *from = &g_np_link[link];
    s32 detail;
    u32 number;
    s32 kind = port_NetReadHeader(in, bytes, &detail, &number);
    PortNetplayHash hash;
    u32 at = INPUT_FIRST_BLOCK;
    u32 blocks;
    u32 block;
    s32 pad;

    if (kind == KIND_BYE) {
        if (from->taken) {
            /* (the rest of a goodbye already taken: the CPU plays on) */
            return;
        }
        if (!from->bye) {
            from->bye = 1;
            from->bye_at = port_NetClock();
            from->bye_pad =
                (detail & BYE_PAD_MASK) != 0 ? (detail & BYE_PAD_MASK) - 1 : -1;
            from->bye_desync = (detail & BYE_DESYNC) != 0;
        }
        return;
    }
    if (kind == KIND_RESYNC) {
        port_NetplayTakeChunk(link, in, bytes, number);
        return;
    }
    if (kind == KIND_RESYNC_ACK) {
        port_NetplayTakeAck(link, in, bytes, number);
        return;
    }
    if (kind != KIND_INPUT || bytes < INPUT_FIRST_BLOCK) {
        return;
    }
    for (pad = 0; pad < NETPLAY_PADS; pad++) {
        u32 has = port_NetGetWord(in + INPUT_HAS + pad * WORD_BYTES);

        if (has > from->has[pad]) {
            from->has[pad] = has;
        }
    }
    port_NetplayTakeTime(from, in);
    /* the host's next resync: at that frame's boundary, which this guest
     * does not run past */
    if (!(g_np_local_mask & 1) && g_np_resync_at == 0 &&
        port_NetGetWord(in + INPUT_EPOCH) == g_np_epoch &&
        port_NetGetWord(in + INPUT_RESYNC_AT) != 0) {
        g_np_resync_at = port_NetGetWord(in + INPUT_RESYNC_AT);
        port_Log("netplay: the host resyncs at frame %u\n", g_np_resync_at);
    }
    /* a guest's player asks for a resync of the state it has (the host
     * heeds it, once) */
    if (port_NetGetWord(in + INPUT_ASK) == g_np_epoch + 1 &&
        !g_np_resync_wanted) {
        port_Log(
            "netplay: the player of link %d asks for a resync\n", link + 1);
        g_np_resync_wanted = TRUE;
    }
    hash.frame = port_NetGetWord(in + INPUT_HASH_FRAME);
    hash.hash =
        port_NetGetWord(in + INPUT_HASH) |
        ((u64)port_NetGetWord(in + INPUT_HASH + WORD_BYTES) << WORD_BITS);
    /* (a hash of the state before a resync this program has had, or one
     * it has not had yet, is of another game: not held to its own) */
    if (port_NetGetWord(in + INPUT_EPOCH) == g_np_epoch && hash.frame != 0 &&
        hash.frame % NETPLAY_HASH_EVERY == 0) {
        from->hashes[(hash.frame / NETPLAY_HASH_EVERY) % NETPLAY_HASHES] = hash;
        port_NetplayCompareHash(link, hash.frame);
    }
    blocks = port_NetGetWord(in + INPUT_BLOCKS);
    for (block = 0; block < blocks && block < NETPLAY_PADS; block++) {
        u32 count;

        if ((u32)bytes < at + BLOCK_INPUTS) {
            return;
        }
        pad = in[at + BLOCK_PAD];
        count = (u32)in[at + BLOCK_COUNT] |
                ((u32)in[at + BLOCK_COUNT + 1] << BITS_PER_BYTE);
        if ((u32)bytes < at + BLOCK_INPUTS + HALF_BYTES * count) {
            return;
        }
        if (pad < g_np_pads && g_np_source[pad] == link) {
            port_NetplayTakePad(pad, port_NetGetWord(in + at + BLOCK_FIRST),
                                count, in + at + BLOCK_INPUTS);
        }
        at += BLOCK_INPUTS + HALF_BYTES * count;
    }
}

/* Each goodbye come NETPLAY_BYE_GRACE_MS or more before `now` taken: its
 * link has left, once what was sent before it has had time to come. */
static void port_NetplayTakeByes(u32 now) {
    s32 link;

    for (link = 0; link < g_np_links; link++) {
        PortNetplayLink *from = &g_np_link[link];

        if (from->bye && !from->left && !from->taken &&
            now - from->bye_at >= NETPLAY_BYE_GRACE_MS) {
            from->left = 1;
            from->left_pad = from->bye_pad;
            from->desync = from->bye_desync;
            g_np_peer_left = 1;
        }
    }
}

/* Everything that has come on every link, waiting up to `wait_ms` for
 * the first link's first. */
static void port_NetplayReceive(u32 wait_ms) {
    u8 in[PORT_NET_DATAGRAM_MAX];
    s32 link;
    s32 got;

    for (link = 0; link < g_np_links; link++) {
        port_NetSelectLink(link);
        for (got = port_NetReceive(in, sizeof(in), link == 0 ? wait_ms : 0);
             got > 0; got = port_NetReceive(in, sizeof(in), 0)) {
            port_NetplayTake(link, in, got);
        }
    }
    port_NetSelectLink(0);
    port_NetplayTakeByes(port_NetClock());
}

/* ---- time ---- */

/* How far ahead of link `link`'s program this one is, in sixteenths of
 * a frame: the frame it said it was at, and the frames of half a round
 * trip since it said so, against this one's. As GGPO reckons it: the
 * time since its last word is not counted, for a program that waits says
 * the same frame again and has not gone on. */
static s32 port_NetplayAhead(const PortNetplayLink *of) {
    u32 frame_us = g_np_blanks_per_frame * BLANK_MICROSECONDS / SIXTEENTHS;
    s32 theirs;

    if (frame_us == 0) {
        return 0;
    }
    theirs =
        (s32)(of->now * SIXTEENTHS +
              of->ping_ms / 2 * MICROSECONDS_PER_MS * SIXTEENTHS / frame_us);
    return (s32)(g_np_frame * SIXTEENTHS) - theirs;
}

static s32 port_NetplayAverage(const s32 *values) {
    u32 count = g_np_ahead_count < NETPLAY_ADVANTAGES ? g_np_ahead_count
                                                      : NETPLAY_ADVANTAGES;
    s32 sum = 0;
    u32 i;

    for (i = 0; i < count; i++) {
        sum += values[i];
    }
    return count != 0 ? sum / (s32)count : 0;
}

/* GGPO's time sync: where this program reckons itself further ahead of
 * another than the other reckons itself, it holds the window back half
 * the difference (the other, being behind, catches up by the same); of
 * several links, by the most any asks for. Only the window has time to
 * keep; the headless programs run as fast as they go. */
static void port_NetplayKeepTime(void) {
    s32 slot = (s32)(g_np_ahead_count % NETPLAY_ADVANTAGES);
    s32 hold = 0;
    s32 most = 0;
    s32 heard = 0;
    s32 link;

    for (link = 0; link < g_np_links; link++) {
        PortNetplayLink *of = &g_np_link[link];
        s32 ahead;

        heard |= of->has_stamp;
        of->ahead[slot] = port_NetplayAhead(of);
        of->their_ahead[slot] = of->their_ahead_now;
        ahead = of->ahead[slot] < 0 ? -of->ahead[slot] : of->ahead[slot];
        if (ahead > most) {
            most = ahead;
        }
    }
    if (!heard) {
        return;
    }
    g_np_ahead_count++;
    g_np_ahead_sum += (u32)most;
    if (g_port_net_hold_hook == NULL || g_np_ahead_count < NETPLAY_ADVANTAGES ||
        g_np_frame - g_np_held_at < NETPLAY_HOLD_EVERY) {
        return;
    }
    for (link = 0; link < g_np_links; link++) {
        const PortNetplayLink *of = &g_np_link[link];
        s32 wanted = (port_NetplayAverage(of->ahead) -
                      port_NetplayAverage(of->their_ahead)) /
                     2;

        if (of->has_stamp && !of->left && wanted > hold) {
            hold = wanted;
        }
    }
    if (hold < NETPLAY_HOLD_MIN) {
        return;
    }
    if (hold > NETPLAY_HOLD_MAX_FRAMES * SIXTEENTHS) {
        hold = NETPLAY_HOLD_MAX_FRAMES * SIXTEENTHS;
    }
    g_np_held_at = g_np_frame;
    g_np_holds++;
    g_np_held_ms += (u32)hold * g_np_blanks_per_frame * BLANK_MICROSECONDS /
                    (SIXTEENTHS * SIXTEENTHS * MICROSECONDS_PER_MS);
    g_port_net_hold_hook(
        ((u32)hold * g_np_blanks_per_frame + SIXTEENTHS * SIXTEENTHS / 2) /
        (SIXTEENTHS * SIXTEENTHS));
}

/* ---- a frame ---- */

/* A pad's input of a frame: what came, or the guess (the last that did). */
static u16 port_NetplayPadInput(s32 pad, u32 frame) {
    if (frame < g_np_count[pad]) {
        return g_np_inputs[pad][frame & NETPLAY_INPUT_MASK];
    }
    if (g_np_count[pad] == 0) {
        return 0;
    }
    return g_np_inputs[pad][(g_np_count[pad] - 1) & NETPLAY_INPUT_MASK];
}

/* The game takes its input (pad.c, port_TakeInput): the frame's. */
static void port_NetplayTakeHook(PortInput *input) { *input = g_np_input; }

/* The state at the boundary of `frame`, into its place among the saves;
 * and the frame's hash, if it is one that is hashed. */
static void port_NetplaySave(u32 frame) {
    s32 slot = (s32)(frame % NETPLAY_SAVES);

    port_StateSave(g_np_saves[slot]);
    /* the sound remastered's mix beside it, for the ears (port_remix.h) */
    port_RemixKeep((u32)slot);
    g_np_save_frames[slot] = frame;
    g_np_save_blanks[slot] = port_GetBlankCount();
    if (frame % NETPLAY_HASH_EVERY == 0) {
        PortNetplayHash *hash =
            &g_np_hashes[(frame / NETPLAY_HASH_EVERY) % NETPLAY_HASHES];

        hash->frame = frame;
        hash->hash = port_StateHash();
        if (frame == 0 && !g_port_rerun) {
            /* the session's first frame: the same in a program started
             * for the session and in one that began it as it ran
             * (online.c) */
            port_Log("netplay: the session's first frame, hash %08X%08X\n",
                     (u32)(hash->hash >> WORD_BITS), (u32)hash->hash);
        }
    }
}

/* A pad's input of a frame as the game takes it: the CPU's pad
 * answers, holding nothing (PORT_PAD_CPU). */
static void port_NetplayPutPad(s32 pad, u16 input) {
    if (input == INPUT_CPU) {
        g_np_input.buttons[pad] = 0;
        g_np_input.present[pad] = PORT_PAD_CPU;
        return;
    }
    g_np_input.buttons[pad] = input;
    g_np_input.present[pad] = 1;
}

/* The frame's input of every pad of the session into g_np_input. */
static void port_NetplayFrameInput(u32 frame) {
    s32 pad;

    for (pad = 0; pad < NETPLAY_PADS; pad++) {
        u16 input;

        if (pad >= g_np_pads) {
            g_np_input.buttons[pad] = 0;
            g_np_input.present[pad] = 0;
            continue;
        }
        input = port_NetplayPadInput(pad, frame);
        port_NetplayPutPad(pad, input);
        if (g_np_source[pad] >= 0 && frame >= g_np_count[pad]) {
            g_np_guessed[pad][frame & NETPLAY_INPUT_MASK] = input;
        }
    }
}

/* --net-part-at, for tests: this program's state parted from the
 * others' at the boundary of frame `frame`, after its state there is
 * saved, so that a rollback past it makes it again: by a word of the RAM
 * nothing of the game reads (below the stack's top the port gives the
 * game and never uses, port_platform.h) but the state's hash does. */
static void port_NetplayPartOnPurpose(u32 frame) {
    if (g_np_part_at == 0 || frame != g_np_part_at) {
        return;
    }
    *(volatile u32 *)ADDR(PORT_RAM_ADDR + PORT_RAM_SIZE - PART_FROM_TOP) +=
        PART_BY;
    if (!g_np_part_done) {
        g_np_part_done = TRUE;
        port_Log("netplay: for the test, this program's state is made "
                 "another's at frame %u\n",
                 frame);
    }
}

/* Runs the game's frame g_np_frame with every pad's input. */
static void port_NetplayRunFrame(void) {
    u32 frame = g_np_frame;
    u32 blank = port_GetBlankCount();
    u32 sectors = g_port_disc_sectors_read;
    u16 used;

    port_NetplaySave(frame);
    port_NetplayPartOnPurpose(frame);
    if (frame == 0) {
        port_SessionSeed();
    }
    /* a replay of the session's fights (port_replay.h) */
    port_ReplayBoundary();
    port_NetplayFrameInput(frame);
    port_SessionInput(frame, &g_np_input, &used);
    g_np_used[frame & NETPLAY_INPUT_MASK] = used;
    port_StepGame();
    g_np_frame++;
    if (g_port_rerun) {
        return;
    }
    /* the blanks a frame takes, for time sync, and a load to wait at */
    g_np_blanks_per_frame = (g_np_blanks_per_frame * (PING_SMOOTH - 1) +
                             (port_GetBlankCount() - blank) * SIXTEENTHS) /
                            PING_SMOOTH;
    if (g_port_disc_sectors_read - sectors > NETPLAY_LOAD_SECTORS) {
        g_np_sync_until = g_np_frame;
    }
}

/* A guess was wrong: the state of its frame back, and every frame from
 * there to now run again, quietly. */
static void port_NetplayRollBack(void) {
    u32 now = g_np_frame;
    u32 from = g_np_wrong_from;
    u32 start = port_HostTicks();
    s32 slot = (s32)(from % NETPLAY_SAVES);
    u32 blanks;

    g_np_is_wrong = 0;
    if (g_np_save_frames[slot] != from || now - from > NETPLAY_PREDICT_MAX) {
        port_Fail(
            "netplay: the state of frame %u is not kept (now %u)\n", from, now);
    }
    blanks = port_GetBlankCount() - g_np_save_blanks[slot];
    if (g_port_net_rerun_hook != NULL) {
        g_port_net_rerun_hook(1, blanks);
    }
    port_StateLoad(g_np_saves[slot]);
    /* ... and the mix put back with it: the frames run again are mixed
     * from where it was, its reverb hearing them once */
    port_RemixPutBack((u32)slot);
    g_np_frame = from;
    g_port_rerun = 1;
    while (g_np_frame < now) {
        port_NetplayRunFrame();
    }
    g_port_rerun = 0;
    if (g_port_net_rerun_hook != NULL) {
        g_port_net_rerun_hook(0, blanks);
    }
    g_np_rollbacks++;
    g_np_frames_again += now - from;
    if (now - from > g_np_longest) {
        g_np_longest = now - from;
    }
    if (now - from > g_np_recent ||
        port_NetClock() - g_np_recent_at > NETPLAY_RECENT_MS) {
        g_np_recent = now - from;
        g_np_recent_at = port_NetClock();
    }
    g_np_rerun_ticks += port_HostTicks() - start;
}

/* The hashes that have become final: every frame before them has every
 * pad's input. Each is printed, and the last is sent. */
static void port_NetplayFinalHashes(void) {
    u32 frame =
        (g_np_last_final.frame / NETPLAY_HASH_EVERY + 1) * NETPLAY_HASH_EVERY;
    u32 known = port_NetplayKnown();

    while (frame < g_np_frame && frame <= known && !g_np_is_wrong) {
        const PortNetplayHash *hash =
            &g_np_hashes[(frame / NETPLAY_HASH_EVERY) % NETPLAY_HASHES];

        if (hash->frame != frame) {
            break;
        }
        g_np_last_final = *hash;
        port_Log("netplay: frame %u hash %08X%08X\n", frame,
                 (u32)(hash->hash >> WORD_BITS), (u32)hash->hash);
        if (port_FeedIsOn() &&
            g_np_save_frames[frame % NETPLAY_SAVES] == frame) {
            /* a state a resync's for the spectators may be told from */
            port_ReplayKeepBase(
                frame, hash->hash, g_np_saves[frame % NETPLAY_SAVES]);
        }
        port_FeedHash(frame, hash->hash);
        port_RankedHash(frame, hash->hash);
        port_NetplayCompareAll(frame);
        frame += NETPLAY_HASH_EVERY;
    }
}

/* The local pads' inputs for the frame g_np_delay on: the driver's read
 * at the last blank, as the game would have taken it. In a session a
 * player plays the session's pad with whichever of this PC's pads they
 * hold (the first player's keys, the first controller), so both are read;
 * where this PC plays two pads of the session, its first pad plays the
 * first of them and its second the second. A plan's run (--net-plan-pads)
 * presses each pad of the session on the PC's pad of that number. */
static void port_NetplayKeepLocal(void) {
    PortInput read;

    while (g_np_local_count <= g_np_frame + g_np_delay) {
        s32 nth = 0;
        s32 select = FALSE;
        s32 pad;

        read.buttons[0] = read.buttons[1] = 0;
        read.buttons[2] = read.buttons[3] = 0;
        /* the first frames of the delay have no press */
        if (g_np_local_count >= g_np_delay) {
            port_DriverInput(&read);
        }
        for (pad = 0; pad < NETPLAY_PADS; pad++) {
            u16 buttons;

            if (g_np_source[pad] == SOURCE_CPU) {
                /* the host plays the CPU's pad on (section 19) */
                if (g_np_local_count >= g_np_count[pad]) {
                    g_np_inputs[pad][g_np_local_count & NETPLAY_INPUT_MASK] =
                        INPUT_CPU;
                    g_np_count[pad] = g_np_local_count + 1;
                }
                continue;
            }
            if (g_np_source[pad] != SOURCE_LOCAL) {
                continue;
            }
            if (g_np_own_pad_only ||
                port_GetSessionRole() == PORT_SESSION_NONE) {
                buttons = read.buttons[pad];
            } else if (g_np_local_mask == (1U << pad)) {
                buttons = read.buttons[0] | read.buttons[1];
            } else {
                buttons = read.buttons[nth];
            }
            nth++;
            if (buttons == INPUT_CPU) {
                buttons = (u16)(buttons & ~PADselect);
            }
            if (g_np_parted) {
                /* parted, Select is the resync's (whatever key or button
                 * the player's settings make it), not the game's: held a
                 * second, it asks for one as F5 does (docs/NETPLAY.md,
                 * section 21) */
                select |= (buttons & PADselect) != 0;
                buttons = (u16)(buttons & ~PADselect);
            }
            g_np_inputs[pad][g_np_local_count & NETPLAY_INPUT_MASK] = buttons;
            g_np_count[pad] = g_np_local_count + 1;
        }
        g_np_select_held = select ? g_np_select_held + 1 : 0;
        if (g_np_select_held == RESYNC_HOLD_FRAMES) {
            port_Log("netplay: Select held a second\n");
            port_NetplayAskResync();
        }
        g_np_local_count++;
    }
}

/* Whether the game is in the fights of a match of four, where the CPU
 * may play a fighter whose player has left (docs/NETPLAY.md, section 19):
 * a fight of two on two, or between the fights of its match -- its fall,
 * replay and winner's view, the card between two fights, the next one's
 * load -- where nothing waits on one player's press (the card goes on by
 * itself, or on anyone's Cross: port_MatchAfterFight), and his fighter is
 * the CPU's from the next fight's first step (port_DuoPutFighter). Not
 * once the session's end is set, and not on the menus before the match
 * or after it, which need their players. */
static s32 port_NetplayInFightsOfFour(void) {
    if (port_SessionResult() != 0) {
        return FALSE;
    }
    if (g_sys_screen == SCREEN_FIGHT_UPDATE && port_DuoIsOn()) {
        return TRUE;
    }
    if (g_port_match.teams == PORT_TEAMS_ONE ||
        !port_MatchModeHasRules(g_fight_mode)) {
        return FALSE;
    }
    switch (g_sys_screen) {
    case SCREEN_FIGHT_INIT:
    case SCREEN_FIGHT_UPDATE:
    case SCREEN_REPLAY_INIT:
    case SCREEN_REPLAY_UPDATE:
    case SCREEN_WINNER_VIEW_INIT:
    case SCREEN_WINNER_VIEW_UPDATE:
    case SCREEN_KAISHAKU_INIT:
    case SCREEN_KAISHAKU_UPDATE:
    case SCREEN_AFTER_FIGHT_INIT:
    case SCREEN_AFTER_FIGHT_UPDATE:
    case SCREEN_AFTER_MATCH_INIT:
    case SCREEN_AFTER_MATCH_UPDATE:
        return TRUE;
    default:
        return FALSE;
    }
}

/* Whether the host gives the pads of link `link`, whose player has left
 * or gone silent, to the CPU and plays on (docs/NETPLAY.md, section 19):
 * a host of four PCs, in the fights of a match of four, another guest
 * still in, the leave no desync's. Otherwise the session ends for all, as
 * it did. */
static s32 port_NetplayMayTakeOver(s32 link) {
    s32 others = 0;
    s32 i;

    if (g_np_links < 2 || g_np_watch || g_np_link[link].desync ||
        g_np_link[link].taken || !port_NetplayInFightsOfFour()) {
        return FALSE;
    }
    for (i = 0; i < g_np_links; i++) {
        others += i != link && !g_np_link[i].left;
    }
    return others > 0;
}

/* Whether a link not yet gone, of which this program has heard, may
 * still be given to the CPU when its silence is long enough. */
static s32 port_NetplayAnyMayTakeOver(void) {
    s32 link;

    for (link = 0; link < g_np_links; link++) {
        if (!g_np_link[link].left && g_np_link[link].has_stamp &&
            port_NetplayMayTakeOver(link)) {
            return TRUE;
        }
    }
    return FALSE;
}

/* The host gives link `link`'s pads to the CPU: each from the first
 * frame of it this program does not have, played on as INPUT_CPU and
 * passed to the others with the rest; a frame already run past that on a
 * guess is run again. */
static void port_NetplayTakeOver(s32 link, const char *how) {
    s32 pad;

    g_np_link[link].taken = 1;
    for (pad = 0; pad < g_np_pads; pad++) {
        u32 from = g_np_count[pad];
        u32 frame;

        if (g_np_source[pad] != link) {
            continue;
        }
        g_np_source[pad] = SOURCE_CPU;
        for (frame = from; frame < g_np_local_count; frame++) {
            g_np_inputs[pad][frame & NETPLAY_INPUT_MASK] = INPUT_CPU;
        }
        if (g_np_local_count > from) {
            g_np_count[pad] = g_np_local_count;
        }
        if (from < g_np_frame && (!g_np_is_wrong || from < g_np_wrong_from)) {
            g_np_wrong_from = from;
            g_np_is_wrong = 1;
        }
        port_NetplayCpuNoted(pad, from, how);
        if (g_sys_screen != SCREEN_FIGHT_UPDATE &&
            g_port_match.rule != PORT_MATCH_SINGLE && !g_port_match.over) {
            port_Log("netplay: player %d left between the fights of the "
                     "match: his fighter is the CPU's from the next fight's "
                     "first step\n",
                     pad + 1);
        }
    }
    g_np_peer_left = FALSE;
    for (pad = 0; pad < g_np_links; pad++) {
        g_np_peer_left |= g_np_link[pad].left && !g_np_link[pad].taken;
    }
}

/* The host: each guest that has said goodbye given to the CPU, where it
 * may be; TRUE if one was. */
static s32 port_NetplayTakeOverLeavers(void) {
    s32 taken = FALSE;
    s32 link;

    for (link = 0; link < g_np_links; link++) {
        if (g_np_link[link].left && port_NetplayMayTakeOver(link)) {
            port_NetplayTakeOver(link, "has left the session");
            taken = TRUE;
        }
    }
    return taken;
}

/* Who left, in words for the log. */
static const char *port_NetplayWhoLeft(void) {
    static const char * const players[] = {
        "player 1", "player 2", "player 3", "player 4"};
    s32 pad = port_NetplayLeftPad();

    return pad >= 0 && pad < NETPLAY_PADS ? players[pad] : "the other program";
}

/* Another has gone and every input to the frame it went at is run. The
 * headless program ends there too; the window's plays on alone, its pads
 * all its own; a session begun in the running program ends, and the
 * program goes back to its own play (online.c). With more than one link
 * (the host of four) the session ends for all: the others are told who
 * left as this one ends (port_EndNetplay). */
static void port_NetplayEndWithPeer(const char *why) {
    s32 desync = FALSE;
    s32 link;

    for (link = 0; link < g_np_links; link++) {
        desync |= g_np_link[link].left && g_np_link[link].desync;
    }
    port_Log("netplay: the other program %s after frame %u (%s); %s\n", why,
             port_NetplayKnown(), port_NetplayWhoLeft(),
             g_np_in_process      ? "the session ends here"
             : g_np_goes_on_alone ? "this one plays on alone"
                                  : "this one ends there");
    if (desync) {
        port_Log("netplay: DESYNC: the host found one with another player; "
                 "the session ends\n");
    }
    if (g_np_in_process) {
        g_np_ended =
            desync ? PORT_NETPLAY_END_DESYNC : PORT_NETPLAY_END_PEER_LEFT;
        return;
    }
    if (!g_np_goes_on_alone) {
        port_Quit(desync ? PORT_EXIT_FAILED : PORT_EXIT_OK);
    }
    port_EndNetplay();
    g_np_on = 0;
    g_np_state = PORT_NETPLAY_ALONE;
    g_np_alone_at = port_NetClock();
    g_port_take_hook = NULL;
    g_port_movies_skipped = FALSE;
    for (;;) {
        port_StepGame();
    }
}

/* Waits for the others' inputs until `enough` says there are enough,
 * sending this program's again now and then; the window is told, and
 * keeps its events read. */
static void port_NetplayWait(s32 state, s32 (*enough)(void)) {
    u32 from = port_NetClock();

    g_np_wait_from = from;
    while (!enough() && !g_np_peer_left && !g_np_ended && !g_np_leave) {
        u32 now = port_NetClock();
        s32 link;

        g_np_waits++;
        for (link = 0; link < g_np_links; link++) {
            PortNetplayLink *of = &g_np_link[link];

            /* the host: a guest silent so long has gone, and the CPU
             * plays on for him */
            if (!of->left && of->has_stamp &&
                now - of->stamp_at > NETPLAY_GIVE_UP_MS &&
                port_NetplayMayTakeOver(link)) {
                of->left = 1;
                port_NetplayTakeOver(link, "has stopped answering");
                from = now;
            }
        }
        if (enough()) {
            break;
        }
        /* (a guest of four waits on another guest by way of the host,
         * who gives that one to the CPU: it gives up on the host's own
         * silence alone; and the host, while a guest it waits on may
         * still be given to the CPU, leaves it to the rule above, which
         * counts from that guest's last word: a word come after this
         * wait began would otherwise lose the race to this one, and end
         * the session for all; one that answers and never sends what is
         * waited on is given up on at twice the time) */
        if (now - from > NETPLAY_GIVE_UP_MS &&
            (!port_NetplayAnyMayTakeOver() ||
             now - from > 2 * NETPLAY_GIVE_UP_MS) &&
            (g_np_pads <= 2 || g_np_links > 1 || !g_np_link[0].has_stamp ||
             now - g_np_link[0].stamp_at > NETPLAY_GIVE_UP_MS)) {
            port_Log("netplay: nothing from the other program for %u "
                     "seconds\n",
                     NETPLAY_GIVE_UP_MS / MS_PER_SECOND);
            if (!g_np_goes_on_alone && !g_np_in_process) {
                port_Quit(PORT_EXIT_FAILED);
            }
            port_NetplayEndWithPeer("has stopped answering");
            if (g_np_in_process) {
                g_np_ended = PORT_NETPLAY_END_SILENT;
                break;
            }
        }
        for (link = 0; link < g_np_links; link++) {
            if (!g_np_link[link].left &&
                now - g_np_link[link].sent_at > NETPLAY_RESEND_MS) {
                port_NetplaySendTo(link);
            }
        }
        /* a room's member stays in it, and its match's feed goes on */
        port_RoomTick();
        port_FeedTick();
        if (now - from > NETPLAY_WAIT_SHOWN_MS) {
            g_np_state = state;
            if (g_port_net_wait_hook != NULL) {
                g_port_net_wait_hook(
                    state == PORT_NETPLAY_LOADING
                        ? (g_np_pads > 2 ? "Loading: waiting for the other "
                                           "players"
                                         : "Loading: waiting for the other "
                                           "player")
                    : g_np_pads > 2 ? "Waiting for the other players"
                                    : "Waiting for the other player");
            }
        }
        port_NetplayReceive(NETPLAY_POLL_MS);
    }
    g_np_state = PORT_NETPLAY_PLAYING;
}

static s32 port_NetplayInWindow(void) {
    return g_np_frame < port_NetplayKnown() + NETPLAY_PREDICT_MAX;
}

static s32 port_NetplayLoadIsShared(void) {
    return port_NetplayKnown() >= g_np_sync_until;
}

/* ---- a resync (docs/NETPLAY.md, section 21) ---- */

/* Whether every input before the frame now is in: the state there is
 * final once a wrong guess among them is put right. */
static s32 port_NetplayAllBefore(void) {
    return port_NetplayKnown() >= g_np_frame;
}

/* The session's machines have the state of `frame` from here, resync
 * number `epoch`, the last frame all held the same before it `agreed`:
 * this program's hashes, and those it has of the others', are of the
 * state before it, and no hash before the frame is held to another's. */
static void port_NetplayResynced(u32 epoch, u32 frame, u32 agreed) {
    s32 link;
    s32 i;

    g_np_epoch = epoch;
    g_np_resync_frame = frame;
    g_np_resyncs_at_last = frame;
    g_np_resynced_at = port_NetClock();
    g_np_parted = FALSE;
    g_np_resync_asked = FALSE;
    g_np_resync_wanted = FALSE;
    g_np_resync_at = 0;
    port_ReplayHoldBase((u32)-1);
    for (i = 0; i < NETPLAY_HASHES; i++) {
        g_np_hashes[i].frame = (u32)-1;
    }
    /* the next hash made final is the first at or after the frame */
    g_np_last_final.frame =
        frame > 0 ? (frame - 1) / NETPLAY_HASH_EVERY * NETPLAY_HASH_EVERY : 0;
    g_np_last_final.hash = 0;
    for (link = 0; link < g_np_links; link++) {
        PortNetplayLink *of = &g_np_link[link];

        for (i = 0; i < NETPLAY_HASHES; i++) {
            of->hashes[i].frame = 0;
            of->compared[i] = 0;
        }
        of->agreed = frame;
    }
    port_RankedResynced(agreed);
    port_Log("netplay: resync %u: every machine has the host's state of frame "
             "%u; its hashes are held again from there (the last the same "
             "before it, frame %u)\n",
             epoch, frame, agreed);
}

/* The host: chunk `chunk` of the resync's state to link `link`. */
static void port_NetplaySendChunk(s32 link, u32 chunk) {
    u8 out[PORT_NET_DATAGRAM_MAX];
    u64 build = port_StateBuildSum();
    u32 length = chunk + 1 == g_np_rs.count
                     ? g_np_rs.size - chunk * RESYNC_CHUNK_BYTES
                     : RESYNC_CHUNK_BYTES;
    u32 i;

    port_NetHeader(out, KIND_RESYNC, 0, chunk);
    port_NetPutWord(out + RESYNC_EPOCH, g_np_rs.epoch);
    port_NetPutWord(out + RESYNC_FRAME, g_np_rs.frame);
    port_NetPutWord(out + RESYNC_AGREED, g_np_rs.agreed);
    port_NetPutWord(out + RESYNC_SIZE, g_np_rs.size);
    port_NetPutWord(out + RESYNC_BUILD, (u32)build);
    port_NetPutWord(out + RESYNC_BUILD + WORD_BYTES, (u32)(build >> WORD_BITS));
    for (i = 0; i < length; i++) {
        out[RESYNC_DATA + i] = g_np_rs.blob[chunk * RESYNC_CHUNK_BYTES + i];
    }
    port_NetSelectLink(link);
    port_NetSend(out, RESYNC_DATA + length);
    port_NetSelectLink(0);
}

/* The host's turn of sending: to each link, of the chunks it has not said
 * it has, those not sent lately, no more in flight than RESYNC_WINDOW and
 * no faster than the way allows. */
static void port_NetplaySendChunks(u32 now) {
    u32 rate = port_RelayIsRelayed() ? RESYNC_RATE_RELAYED : RESYNC_RATE;
    s32 link;

    for (link = 0; link < g_np_links; link++) {
        PortNetplayLink *of = &g_np_link[link];
        u32 again = 2 * of->ping_ms + RESYNC_RESEND_MS;
        u32 allowed =
            (now - g_np_rs.began_at) * rate / MS_PER_SECOND + RESYNC_WINDOW;
        u32 flying = 0;
        u32 chunk;

        if (of->left || of->resync_had >= g_np_rs.count) {
            continue;
        }
        for (chunk = 0; chunk < g_np_rs.count; chunk++) {
            u32 sent = g_np_rs_sent[link][chunk];

            if (sent != 0 && now - sent < again &&
                !port_MapHas(g_np_rs_had[link], chunk)) {
                flying++;
            }
        }
        for (chunk = 0; chunk < g_np_rs.count && flying < RESYNC_WINDOW &&
                        of->resync_next < allowed;
             chunk++) {
            u32 sent = g_np_rs_sent[link][chunk];

            if (port_MapHas(g_np_rs_had[link], chunk) ||
                (sent != 0 && now - sent < again)) {
                continue;
            }
            port_NetplaySendChunk(link, chunk);
            g_np_rs_sent[link][chunk] = now != 0 ? now : 1;
            of->resync_next++;
            flying++;
        }
    }
}

/* The least share of the state any link has had, in hundredths. */
static u32 port_NetplayResyncPercent(void) {
    u32 least = PERCENT;
    s32 link;

    for (link = 0; link < g_np_links; link++) {
        const PortNetplayLink *of = &g_np_link[link];
        u32 share;

        if (of->left || g_np_rs.count == 0) {
            continue;
        }
        share = of->resync_had * PERCENT / g_np_rs.count;
        if (share < least) {
            least = share;
        }
    }
    return least;
}

/* The window's line while the state goes over. */
static void port_NetplayResyncShown(const char *what) {
    static char line[RESYNC_LINE_BYTES];
    u32 at = 0;
    u32 percent = g_np_rs.percent;

    g_np_state = PORT_NETPLAY_RESYNCING;
    if (g_port_net_wait_hook == NULL) {
        return;
    }
    while (*what != 0 && at + sizeof("   100%") < sizeof(line)) {
        line[at++] = *what++;
    }
    line[at++] = ' ';
    line[at++] = ' ';
    line[at++] = ' ';
    if (percent >= PERCENT) {
        line[at++] = '1';
    }
    if (percent >= DECIMAL) {
        line[at++] = (char)('0' + percent / DECIMAL % DECIMAL);
    }
    line[at++] = (char)('0' + percent % DECIMAL);
    line[at++] = '%';
    line[at] = 0;
    g_port_net_wait_hook(line);
}

/* The host's resync, at the frame it said (g_np_resync_at), where it
 * stopped: once every input before it is in (so that its state there is
 * final), the state packed and sent whole to every guest, each chunk until
 * it says it has it; the session goes on from that frame on every
 * machine. Spectators who parted are given it by
 * the lobby, as a fight's start (watch.c), and each machine's replay of
 * the fight keeps it. */
static void port_NetplayResyncSend(void) {
    u32 frame = g_np_frame;
    u32 alive_at = 0;
    const u8 *blob;
    u32 size;
    s32 link;

    g_np_wait_from = port_NetClock();
    port_NetplayWait(PORT_NETPLAY_RESYNCING, port_NetplayAllBefore);
    if (!port_NetplayAllBefore() || g_np_ended) {
        return;
    }
    if (g_np_is_wrong) {
        port_NetplayRollBack();
    }
    port_NetplayFinalHashes();
    g_np_rs.agreed = port_NetplayAgreed();
    if (!port_ReplayResyncPack(frame, g_np_rs.agreed, &blob, &size) ||
        size > RESYNC_CHUNKS_MAX * RESYNC_CHUNK_BYTES) {
        port_Log("netplay: the host's state could not be packed for the "
                 "resync\n");
        port_NetplayEndDesync();
        return;
    }
    g_np_rs.sending = TRUE;
    g_np_rs.epoch = g_np_epoch + 1;
    g_np_rs.frame = frame;
    g_np_rs.size = size;
    g_np_rs.count = (size + RESYNC_CHUNK_BYTES - 1) / RESYNC_CHUNK_BYTES;
    g_np_rs.blob = blob;
    g_np_rs.began_at = port_NetClock();
    g_np_rs.percent = 0;
    for (link = 0; link < g_np_links; link++) {
        PortNetplayLink *of = &g_np_link[link];
        u32 i;

        of->resync_had = 0;
        of->resync_next = 0;
        of->resync_heard_at = g_np_rs.began_at;
        for (i = 0; i < RESYNC_MAP_BYTES; i++) {
            g_np_rs_had[link][i] = 0;
        }
        for (i = 0; i < g_np_rs.count; i++) {
            g_np_rs_sent[link][i] = 0;
        }
    }
    port_NetplayResynced(g_np_rs.epoch, frame, g_np_rs.agreed);
    port_ReplayResynced(g_np_rs.agreed);
    port_StatsResynced(frame - g_np_rs.agreed, FALSE);
    for (;;) {
        u32 now = port_NetClock();
        s32 done = TRUE;

        for (link = 0; link < g_np_links; link++) {
            PortNetplayLink *of = &g_np_link[link];

            if (of->left || of->resync_had >= g_np_rs.count) {
                continue;
            }
            done = FALSE;
            if (now - of->resync_heard_at > NETPLAY_GIVE_UP_MS) {
                port_Log("netplay: link %d had none of the resync's state "
                         "for %u seconds\n",
                         link + 1, NETPLAY_GIVE_UP_MS / MS_PER_SECOND);
                g_np_rs.sending = FALSE;
                port_NetplayEndDesync();
                return;
            }
        }
        if (done || g_np_peer_left || g_np_ended || g_np_leave) {
            break;
        }
        port_NetplaySendChunks(now);
        if (now - alive_at >= RESYNC_ALIVE_MS) {
            port_NetplaySendInputs();
            alive_at = now;
        }
        port_RoomTick();
        port_FeedTick();
        g_np_rs.percent = port_NetplayResyncPercent();
        port_NetplayResyncShown(g_np_pads > 2 || g_np_links > 1
                                    ? "Resyncing: the game goes to the others"
                                    : "Resyncing: the game goes to the other "
                                      "player");
        port_NetplayReceive(NETPLAY_POLL_MS);
    }
    g_np_rs.sending = FALSE;
    g_np_state = PORT_NETPLAY_PLAYING;
    port_Log("netplay: resync %u's state, %u bytes in %u chunks, had by every "
             "guest in %u ms\n",
             g_np_rs.epoch, g_np_rs.size, g_np_rs.count,
             port_NetClock() - g_np_rs.began_at);
}

/* A guest's resync, at the frame the host said, where it stopped: the
 * host's state, coming, had whole (each chunk said to the host as it
 * comes) and put in; the session goes on from there. */
static void port_NetplayResyncReceive(void) {
    u32 alive_at = 0;
    u32 waited_from = port_NetClock();
    u32 frame;
    s32 i;

    port_Log("netplay: at frame %u, the host's state for the resync is "
             "waited for\n",
             g_np_frame);
    while (!g_np_rs.coming || g_np_rs.have < g_np_rs.count) {
        u32 now = port_NetClock();

        if (g_np_peer_left || g_np_ended || g_np_leave) {
            g_np_rs.coming = FALSE;
            return;
        }
        if (now - (g_np_rs.coming ? g_np_rs.had_at : waited_from) >
            NETPLAY_GIVE_UP_MS) {
            port_Log("netplay: nothing of the resync's state for %u "
                     "seconds\n",
                     NETPLAY_GIVE_UP_MS / MS_PER_SECOND);
            g_np_rs.coming = FALSE;
            port_NetplayEndDesync();
            return;
        }
        if (now - alive_at >= RESYNC_ALIVE_MS) {
            port_NetplaySendInputs();
            alive_at = now;
        }
        if (g_np_rs.unacked > 0 && now - g_np_rs.acked_at >= RESYNC_ACK_MS) {
            port_NetplayAckChunks();
        }
        port_RoomTick();
        port_FeedTick();
        g_np_rs.percent =
            g_np_rs.coming ? g_np_rs.have * PERCENT / g_np_rs.count : 0;
        port_NetplayResyncShown("Resyncing: the host's game is coming");
        port_NetplayReceive(NETPLAY_POLL_MS);
    }
    port_NetplayAckChunks();
    /* every frame before it run on the inputs as they were, not on a
     * guess: the fight's replay keeps those */
    port_NetplayWait(PORT_NETPLAY_RESYNCING, port_NetplayAllBefore);
    if (!port_NetplayAllBefore() || g_np_ended) {
        g_np_rs.coming = FALSE;
        return;
    }
    if (g_np_is_wrong) {
        port_NetplayRollBack();
    }
    g_np_rs.coming = FALSE;
    g_np_state = PORT_NETPLAY_PLAYING;
    if (!port_ReplayResyncLoad(g_np_rs.room, g_np_rs.size, &frame) ||
        frame != g_np_rs.frame) {
        port_Log("netplay: the host's state for the resync could not be put "
                 "in\n");
        port_NetplayEndDesync();
        return;
    }
    /* (a guest that had run past the frame, which the host's word ahead
     * keeps from happening: the frames from there run again with the
     * inputs as they are known, its plan's own where it was) nothing
     * guessed before stands, and no state before it is gone back to */
    if (frame != g_np_frame) {
        port_Log("netplay: this guest was at frame %u, the resync's at %u\n",
                 g_np_frame, frame);
    }
    g_np_frame = frame;
    g_np_is_wrong = 0;
    g_np_sync_until = frame;
    for (i = 0; i < NETPLAY_SAVES; i++) {
        g_np_save_frames[i] = (u32)-1;
    }
    port_NetplayResynced(g_np_rs.epoch, frame, g_np_rs.agreed);
    port_ReplayResynced(g_np_rs.agreed);
    port_StatsResynced(frame - g_np_rs.agreed, TRUE);
    port_Log("netplay: resync %u's state had in %u ms and put in\n",
             g_np_rs.epoch, port_NetClock() - g_np_rs.began_at);
}

/* Whether the host resyncs now: a guest's player asked, or this program
 * is parted and its player asked, or it is parted long enough (not again
 * so soon after the last by itself). */
static s32 port_NetplayResyncDue(void) {
    if (g_np_resync_wanted) {
        return TRUE;
    }
    if (!g_np_parted) {
        return FALSE;
    }
    if (g_np_resync_asked) {
        return TRUE;
    }
    if (g_np_resync_auto == 0 ||
        (g_np_resyncs_at_last != 0 &&
         g_np_frame - g_np_resyncs_at_last < RESYNC_AGAIN_FRAMES)) {
        return FALSE;
    }
    return g_np_frame - g_np_parted_frame >= g_np_resync_auto;
}

/* The resync's turn of the session's loop: TRUE where it did one (the
 * loop goes round again). */
static s32 port_NetplayResyncTurn(void) {
    if (g_np_ended || g_np_watch || g_np_peer_left || g_np_leave) {
        /* (the loop's own way with an end, a goodbye or a leave first) */
        g_np_rs.coming = FALSE;
        return FALSE;
    }
    if (g_np_resync_ask_at != 0 && g_np_frame >= g_np_resync_ask_at &&
        g_np_parted) {
        g_np_resync_ask_at = 0;
        port_NetplayAskResync();
    }
    if ((g_np_local_mask & 1) && g_np_resync_at == 0 &&
        port_NetplayResyncDue()) {
        g_np_resync_at = g_np_frame + RESYNC_LEAD;
        port_Log("netplay: a resync at frame %u (now %u): every machine "
                 "stops there\n",
                 g_np_resync_at, g_np_frame);
    }
    if (g_np_resync_at == 0 || g_np_frame < g_np_resync_at) {
        return FALSE;
    }
    if (g_np_local_mask & 1) {
        port_NetplayResyncSend();
    } else {
        port_NetplayResyncReceive();
    }
    return TRUE;
}

/* A room's match's host: each frame whose pads are all known (none a
 * guess) to the match's feed, the first pad first; and the room stayed
 * in. */
static void port_NetplayFeed(void) {
    u32 final = port_NetplayKnown();

    port_RoomTick();
    /* a ranked match's digest is made of the same, on both machines */
    while (g_np_ranked_fed < final) {
        u32 at = g_np_ranked_fed & NETPLAY_INPUT_MASK;

        port_RankedFrame(
            g_np_ranked_fed, g_np_inputs[0][at], g_np_inputs[1][at]);
        g_np_ranked_fed++;
    }
    if (!port_FeedIsOn()) {
        return;
    }
    while (g_np_fed < final) {
        u16 pads[NETPLAY_PADS];
        s32 pad;

        for (pad = 0; pad < NETPLAY_PADS; pad++) {
            pads[pad] = pad < g_np_pads
                            ? g_np_inputs[pad][g_np_fed & NETPLAY_INPUT_MASK]
                            : 0;
        }
        port_FeedFrame(g_np_fed, pads);
        g_np_fed++;
    }
    port_FeedTick();
}

/* ---- a spectator (docs/NETPLAY.md, section 16) ---- */

/* A spectator's own hash of a frame, kept to be held to the players' as
 * it comes; and each such pair held: FALSE at one that differs, its frame
 * in `parted`. */
static s32 port_WatchHold(u32 *parted) {
    s32 i;

    for (i = 0; i < NETPLAY_HASHES; i++) {
        PortNetplayHash *mine = &g_np_watch_hashes[i];
        u64 theirs;

        if (mine->frame == (u32)-1 || !port_WatchHash(mine->frame, &theirs)) {
            continue;
        }
        if (theirs != mine->hash) {
            port_Log("watch: DESYNC at frame %u: this spectator's state hash "
                     "is %08X%08X, the players' %08X%08X\n",
                     mine->frame, (u32)(mine->hash >> WORD_BITS),
                     (u32)mine->hash, (u32)(theirs >> WORD_BITS), (u32)theirs);
            *parted = mine->frame;
            return FALSE;
        }
        if (mine->frame > g_np_watch_agreed) {
            g_np_watch_agreed = mine->frame;
        }
        mine->frame = (u32)-1;
        g_np_watch_held++;
    }
    return TRUE;
}

/* A spectator parted from its players (docs/NETPLAY.md, section 21): the
 * match's host resyncs its players, and gives the lobby the state it put
 * on them as a fight's start; the spectator asks for a start after the
 * hash that parted, puts it in when it is whole and watches on from its
 * frame. 1 once it has, 0 while it waits, -1 where none came. */
static s32 port_WatchResync(void) {
    const u8 *blob;
    u32 size;
    u32 frame;
    s32 i;

    if (port_WatchStart(&blob, &size)) {
        if (port_ReplayServedNeedsBase(blob, size, &frame)) {
            /* the difference from a state this spectator has not: a later
             * start is waited for (docs/NETPLAY.md, section 21) */
            port_Log("watch: the start of frame %u is a resync's told from a "
                     "state this spectator has not; a later one is waited "
                     "for\n",
                     frame);
            port_WatchAskStart(frame + 1);
            return 0;
        }
        if (!port_ReplayResyncLoad(blob, size, &frame)) {
            port_Log("watch: the host's state of the resync could not be put "
                     "in\n");
            return -1;
        }
        port_ReplayHoldBase((u32)-1);
        g_np_frame = frame;
        g_np_watch_parted = FALSE;
        g_np_epoch++;
        g_np_resync_frame = frame;
        g_np_resynced_at = port_NetClock();
        for (i = 0; i < NETPLAY_HASHES; i++) {
            g_np_watch_hashes[i].frame = (u32)-1;
        }
        port_WatchStarted(frame);
        port_ReplayResynced(g_np_watch_agreed);
        g_np_watch_agreed = frame;
        g_np_wait_from = 0;
        port_Log("watch: resync %u: the host's state of frame %u put in; "
                 "every hash is held to the players' again from there\n",
                 g_np_epoch, frame);
        return 1;
    }
    if (port_NetClock() - g_np_watch_parted_at > 2 * NETPLAY_GIVE_UP_MS) {
        port_Log("watch: no resync came for %u seconds\n",
                 2 * NETPLAY_GIVE_UP_MS / MS_PER_SECOND);
        return -1;
    }
    g_np_state = PORT_NETPLAY_RESYNCING;
    if (g_port_net_wait_hook != NULL) {
        g_port_net_wait_hook("Watching: the game fell out of step with the "
                             "players'; waiting for their resync");
    }
    port_RoomWait(NETPLAY_POLL_MS);
    return 0;
}

/* Waits a moment for the match's next frames, the window told: FALSE
 * where nothing has been heard of them for NETPLAY_GIVE_UP_MS. */
static s32 port_WatchWait(void) {
    u32 now = port_NetClock();

    if (g_np_wait_from == 0) {
        g_np_wait_from = now;
    }
    g_np_state = PORT_NETPLAY_WAITING;
    if (now - g_np_wait_from > NETPLAY_WAIT_SHOWN_MS &&
        g_port_net_wait_hook != NULL) {
        g_port_net_wait_hook("Watching: waiting for the match's next frames");
    }
    port_RoomWait(NETPLAY_POLL_MS);
    return port_WatchQuietMs() <= NETPLAY_GIVE_UP_MS;
}

/* The match watched (port_net.h): every frame of its, as its players ran
 * it, from the start put in. */
s32 port_RunWatch(u32 frame) {
    u32 end_frame;
    s32 winner;

    g_np_on = 1;
    g_np_watch = TRUE;
    g_np_frame = frame;
    g_np_in_process = 1;
    /* nobody to say goodbye to: the lobby is told by the room */
    g_np_peer_left = 1;
    g_np_link[0].left = 1;
    g_np_wait_from = 0;
    g_port_take_hook = port_NetplayTakeHook;
    g_port_movies_skipped = TRUE;
    port_Log("watch: from frame %u, as the match's players ran it (%d pads)\n",
             frame, g_np_pads);
    for (;;) {
        u16 pads[NETPLAY_PADS];
        u32 known;
        u32 left;
        u16 used;
        s32 pad;

        port_RoomTick();
        port_WatchTick();
        if (g_np_watch_parted) {
            s32 resynced = port_WatchResync();

            if (resynced < 0) {
                return PORT_NETPLAY_END_DESYNC;
            }
            if (resynced == 0 && port_WatchEnd(&end_frame, &winner) &&
                port_NetClock() - g_np_watch_parted_at > WATCH_END_WAIT_MS) {
                port_Log("watch: the match's session ended, and no resync "
                         "came\n");
                return PORT_NETPLAY_END_DESYNC;
            }
            if (resynced == 0) {
                continue;
            }
        }
        {
            u32 parted;

            if (!port_WatchHold(&parted)) {
                g_np_watch_parted = TRUE;
                g_np_watch_parted_frame = parted;
                g_np_watch_parted_at = port_NetClock();
                port_ReplayHoldBase(g_np_watch_agreed);
                port_WatchAskStart(parted);
                port_Log("watch: parted from the players' game after frame "
                         "%u: waiting for their host's resync\n",
                         g_np_watch_agreed);
                continue;
            }
        }
        left = port_SessionLeftAt();
        if (left != 0 && left - 1 < g_np_frame) {
            port_Log("watch: the match was decided at frame %u; %u hashes "
                     "the same as its players'\n",
                     left - 1, g_np_watch_held);
            return port_SessionResult() != 0 ? PORT_NETPLAY_END_MATCH
                                             : PORT_NETPLAY_END_LEFT;
        }
        if (port_WatchEnd(&end_frame, &winner) && g_np_frame >= end_frame) {
            port_Log("watch: the match's session ended at frame %u; %u "
                     "hashes the same as its players'\n",
                     end_frame, g_np_watch_held);
            return PORT_NETPLAY_END_PEER_LEFT;
        }
        if (g_np_leave) {
            port_Log("watch: the spectator stopped watching at frame %u\n",
                     g_np_frame);
            return PORT_NETPLAY_END_QUIT;
        }
        known = port_WatchKnown();
        /* WATCH_LAG frames held in hand while the match goes on; at its
         * end, every frame to its last */
        if (known <=
            g_np_frame + (port_WatchEnd(&end_frame, &winner) ? 0 : WATCH_LAG)) {
            if (port_WatchEnd(&end_frame, &winner) && g_np_wait_from != 0 &&
                port_NetClock() - g_np_wait_from > WATCH_END_WAIT_MS) {
                /* the session ended and its last frames do not come */
                port_Log("watch: the match's session ended at frame %u, and "
                         "its frames from %u did not come; %u hashes the "
                         "same as its players'\n",
                         end_frame, known, g_np_watch_held);
                return PORT_NETPLAY_END_PEER_LEFT;
            }
            if (!port_WatchWait()) {
                port_Log("watch: nothing from the lobby of the match for %u "
                         "seconds\n",
                         NETPLAY_GIVE_UP_MS / MS_PER_SECOND);
                return PORT_NETPLAY_END_SILENT;
            }
            continue;
        }
        g_np_wait_from = 0;
        g_np_state = PORT_NETPLAY_PLAYING;
        g_np_watch_known = known;
        /* far behind (come in the middle of a fight): unseen, unheard */
        g_port_rerun = known - g_np_frame > WATCH_CATCH_UP;
        if (g_np_frame % NETPLAY_HASH_EVERY == 0) {
            PortNetplayHash *mine =
                &g_np_watch_hashes[(g_np_frame / NETPLAY_HASH_EVERY) %
                                   NETPLAY_HASHES];

            mine->frame = g_np_frame;
            mine->hash = port_StateHash();
            /* a state a resync may be told from (replay.c) */
            port_ReplayKeepBase(g_np_frame, mine->hash, NULL);
            port_Log("netplay: frame %u hash %08X%08X\n", g_np_frame,
                     (u32)(mine->hash >> WORD_BITS), (u32)mine->hash);
        }
        port_ReplayBoundary();
        port_WatchInput(g_np_frame, pads);
        for (pad = 0; pad < NETPLAY_PADS; pad++) {
            if (pad >= g_np_pads) {
                g_np_input.buttons[pad] = 0;
                g_np_input.present[pad] = 0;
                continue;
            }
            if (pads[pad] == INPUT_CPU) {
                port_NetplayCpuNoted(pad, g_np_frame, "has left the match");
            }
            port_NetplayPutPad(pad, pads[pad]);
        }
        port_SessionInput(g_np_frame, &g_np_input, &used);
        port_StepGame();
        g_port_rerun = 0;
        g_np_frame++;
    }
}

/* Whether every link has said goodbye, or one has (more than one link:
 * the session is over for all once one has gone). */
static s32 port_NetplayPeerGone(void) {
    s32 link;

    for (link = 0; link < g_np_links; link++) {
        if (g_np_link[link].left && !g_np_link[link].taken) {
            return TRUE;
        }
    }
    return FALSE;
}

/* The session (port_net.h): the game from its first boundary on, a frame
 * at a time, for ever; or, begun in the running program, until it ends,
 * with why (PORT_NETPLAY_END_...). */
s32 port_RunNetplay(void) {
    s32 i;

    for (i = 0; i < NETPLAY_SAVES; i++) {
        if (g_np_saves[i] == NULL) {
            g_np_saves[i] = port_HostAllocate(port_StateSize());
        }
        if (g_np_saves[i] == NULL) {
            port_Fail("netplay: no memory for the states (%u bytes)\n",
                      port_StateSize() * NETPLAY_SAVES);
        }
        g_np_save_frames[i] = (u32)-1;
    }
    if (g_np_pads > 2 || g_np_links > 1) {
        const char *pads = port_NetPadsText(g_np_local_mask);

        port_Log(
            "netplay: %d pads, this program's %s, %d link%s\n", g_np_pads, pads,
            g_np_links,
            g_np_links > 1 ? "s (the host passes each guest's inputs on)" : "");
    }
    g_port_take_hook = port_NetplayTakeHook;
    g_port_movies_skipped = TRUE;
    /* the game to its first boundary: the input of frame 0 is next */
    port_StepGame();
    for (;;) {
        port_NetplayKeepLocal();
        port_NetplaySendInputs();
        port_NetplayReceive(0);
        /* the machines parted: the host's state on every one, and round
         * again from its frame (docs/NETPLAY.md, section 21) */
        if (port_NetplayResyncTurn()) {
            continue;
        }
        /* no further than NETPLAY_PREDICT_MAX past the others' inputs, and
         * past a load only with them */
        port_NetplayWait(PORT_NETPLAY_WAITING, port_NetplayInWindow);
        if (port_NetplayKnown() < g_np_sync_until) {
            g_np_load_waits++;
            port_NetplayWait(PORT_NETPLAY_LOADING, port_NetplayLoadIsShared);
        }
        /* a guest gone, the CPU playing on for him: the waits again, with
         * his pad no longer waited for */
        if (g_np_peer_left && !g_np_ended && port_NetplayTakeOverLeavers()) {
            continue;
        }
        if (g_np_is_wrong && !g_np_ended) {
            port_NetplayRollBack();
        }
        port_NetplayFinalHashes();
        port_NetplayFeed();
        /* (the session's own end first: another, ending there too, may
         * have said goodbye already) */
        if ((g_np_in_process || g_np_cpu_mask != 0) && !g_np_ended &&
            !g_np_is_wrong) {
            /* a way back to the top menu, or a room's match decided, or
             * the match of a session with a CPU for a player who left,
             * taken on frames all have the inputs of, is the session's
             * end on all (session.c) */
            u32 left = port_SessionLeftAt();

            if (left != 0 && left - 1 < port_NetplayKnown()) {
                if (port_SessionResult() != 0) {
                    port_Log("netplay: the match was decided at frame %u: "
                             "the session ends there\n",
                             left - 1);
                    if (!g_np_in_process) {
                        port_Quit(PORT_EXIT_OK);
                    }
                    g_np_ended = PORT_NETPLAY_END_MATCH;
                } else {
                    port_Log("netplay: the players went back to the top "
                             "menu at frame %u: the session ends there\n",
                             left - 1);
                    g_np_ended = PORT_NETPLAY_END_LEFT;
                }
            }
        }
        if (g_np_peer_left && port_NetplayPeerGone() &&
            g_np_frame >= port_NetplayKnown() && !g_np_ended) {
            port_NetplayEndWithPeer("ended");
        }
        if (!g_np_ended && (g_np_leave || (g_np_leave_at != 0 &&
                                           g_np_frame >= g_np_leave_at))) {
            port_Log("netplay: this player left the session at frame %u\n",
                     g_np_frame);
            if (!g_np_in_process) {
                port_Quit(PORT_EXIT_OK);
            }
            g_np_ended = PORT_NETPLAY_END_QUIT;
        }
        if (g_np_ended) {
            return g_np_ended;
        }
        /* never past the window: a wait cut short by a goodbye goes
         * round again (the end above, or the CPU taking over) */
        if (!port_NetplayInWindow() ||
            (g_np_resync_at != 0 && g_np_frame >= g_np_resync_at)) {
            continue;
        }
        port_NetplayKeepTime();
        port_NetplayRunFrame();
    }
}

u32 port_NetplayFinalFrames(void) {
    u32 known = port_NetplayKnown();

    if (g_np_is_wrong && g_np_wrong_from < known) {
        return g_np_wrong_from;
    }
    return known;
}

/* What netplay is doing now, for the window's line (port_net.h). */
void port_GetNetplayStats(PortNetplayStats *stats) {
    u32 now = port_NetClock();
    u32 ping = 0;
    s32 ahead = 0;
    s32 link;

    for (link = 0; link < g_np_links; link++) {
        const PortNetplayLink *of = &g_np_link[link];

        if (of->ping_ms > ping && !of->taken) {
            ping = of->ping_ms;
        }
        if (g_np_ahead_count != 0 && link == 0) {
            ahead = of->ahead[(g_np_ahead_count - 1) % NETPLAY_ADVANTAGES];
        }
    }
    stats->is_on = g_np_on || g_np_state == PORT_NETPLAY_ALONE;
    stats->pad = g_np_local;
    stats->pads = g_np_pads;
    stats->local_mask = g_np_local_mask;
    stats->state = g_np_state;
    stats->delay = g_np_delay;
    stats->ping_ms = ping;
    stats->rollback =
        now - g_np_recent_at <= NETPLAY_RECENT_MS ? g_np_recent : 0;
    stats->rollbacks = g_np_rollbacks;
    stats->ahead = ahead;
    stats->frame = g_np_frame;
    stats->waited_ms =
        g_np_state == PORT_NETPLAY_WAITING || g_np_state == PORT_NETPLAY_LOADING
            ? now - g_np_wait_from
            : 0;
    stats->alone_ms =
        g_np_state == PORT_NETPLAY_ALONE ? now - g_np_alone_at : 0;
    stats->held_ms = g_np_held_ms;
    stats->is_session = port_GetSessionRole() != PORT_SESSION_NONE;
    stats->watching = g_np_watch;
    stats->behind = g_np_watch && port_WatchKnown() > g_np_frame
                        ? port_WatchKnown() - g_np_frame
                        : 0;
    stats->leave_ms =
        g_np_leave_asked_at != 0 &&
                now - g_np_leave_asked_at <= NETPLAY_LEAVE_MS
            ? now - g_np_leave_asked_at + 1
            : 0;
    stats->cpu_pad = g_np_cpu_pad;
    stats->cpu_ms = g_np_cpu_pad >= 0 ? now - g_np_cpu_at : 0;
    /* a guest of four PCs: one pad of four, one link (section 19) */
    stats->cpu_on_leave = g_np_pads > 2 && g_np_links == 1 && !g_np_watch &&
                          port_NetplayInFightsOfFour() &&
                          (g_np_local_mask & (g_np_local_mask - 1)) == 0;
    stats->parted = g_np_watch ? g_np_watch_parted : g_np_parted;
    stats->resync_asked = g_np_resync_asked;
    stats->resync_in = 0;
    if (g_np_parted && !g_np_resync_asked && g_np_resync_auto != 0 &&
        !(g_np_resyncs_at_last != 0 &&
          g_np_frame - g_np_resyncs_at_last < RESYNC_AGAIN_FRAMES)) {
        u32 parted_for = g_np_frame - g_np_parted_frame;

        stats->resync_in =
            parted_for < g_np_resync_auto ? g_np_resync_auto - parted_for : 1;
    }
    stats->resync_percent = g_np_rs.percent;
    stats->resyncs = g_np_epoch;
    stats->resynced_ms = g_np_epoch != 0 ? now - g_np_resynced_at + 1 : 0;
}

/* For a moment after this program ends, until the others have said
 * nothing for NETPLAY_QUIET_MS: whatever one sends is answered with this
 * program's inputs, so that the last of them are not lost with it. */
static void port_NetplayLinger(void) {
    u8 in[PORT_NET_DATAGRAM_MAX];
    u32 start = port_NetClock();
    u32 heard = start;

    for (;;) {
        u32 now = port_NetClock();
        s32 link;

        if (now - start > NETPLAY_LINGER_MS || now - heard > NETPLAY_QUIET_MS) {
            port_NetSelectLink(0);
            return;
        }
        for (link = 0; link < g_np_links; link++) {
            if (g_np_link[link].left) {
                continue;
            }
            port_NetSelectLink(link);
            if (port_NetReceive(
                    in, sizeof(in), link == 0 ? NETPLAY_POLL_MS : 0) > 0) {
                heard = port_NetClock();
                port_NetplaySendTo(link);
            }
        }
    }
}

/* The run ends (main.c, port_Quit), or another has gone: the others are
 * told, a few times, who has left (this program's player, or the one
 * whose link went), and the report. */
void port_EndNetplay(void) {
    u8 out[PORT_NET_HEADER_BYTES];
    s32 gone = port_NetplayLeftPad();
    s32 desync = g_np_ended == PORT_NETPLAY_END_DESYNC;
    s32 any = FALSE;
    s32 link;
    s32 i;

    if (!g_np_on) {
        return;
    }
    g_np_on = 0;
    if (gone < 0) {
        /* none said: the link that went, or this program's own player */
        for (link = 0; link < g_np_links && gone < 0; link++) {
            if (g_np_link[link].left && !g_np_link[link].taken) {
                s32 pad;

                for (pad = 0; pad < g_np_pads && gone < 0; pad++) {
                    gone = g_np_source[pad] == link ? pad : gone;
                }
            }
        }
        if (gone < 0) {
            gone = g_np_local;
        }
    }
    /* the last inputs as many times as the goodbye: one lost, and a
     * goodbye come through, a guest of four would end short of the frame
     * the host ended at (a match decided there), its last inputs never
     * had */
    for (i = 0; i < NETPLAY_BYES; i++) {
        port_NetplaySendInputs();
    }
    port_NetHeader(
        out, KIND_BYE, ((gone + 1) & BYE_PAD_MASK) | (desync ? BYE_DESYNC : 0),
        g_np_local_count);
    for (link = 0; link < g_np_links; link++) {
        if (g_np_link[link].left) {
            continue;
        }
        any = TRUE;
        port_NetSelectLink(link);
        for (i = 0; i < NETPLAY_BYES; i++) {
            port_NetSend(out, sizeof(out));
        }
    }
    port_NetSelectLink(0);
    if (any) {
        port_NetplayLinger();
    }
    port_Log("netplay: %u frames; %u rollbacks, %u frames run again, the "
             "longest %u; %u ms run again in all; %u waits on the other, %u "
             "of them at a load; %u hashes compared, the last final one at "
             "frame %u\n",
             g_np_frame, g_np_rollbacks, g_np_frames_again, g_np_longest,
             g_np_rerun_ticks /
                 (PORT_HOST_TICKS_PER_MICROSECOND * MICROSECONDS_PER_MS),
             g_np_waits, g_np_load_waits, g_np_hashes_compared,
             g_np_last_final.frame);
    port_Log(
        "netplay: time: a round trip of %u ms; %u.%02u frames from the "
        "other on average; held back %u times, %u ms in all, to keep in "
        "step with it\n",
        g_np_link[0].ping_ms,
        g_np_ahead_count != 0 ? g_np_ahead_sum / g_np_ahead_count / SIXTEENTHS
                              : 0,
        g_np_ahead_count != 0 ? g_np_ahead_sum * HUNDREDTHS / g_np_ahead_count /
                                    SIXTEENTHS % HUNDREDTHS
                              : 0,
        g_np_holds, g_np_held_ms);
    if (g_np_epoch != 0) {
        port_Log("netplay: %u resync%s, the last at frame %u\n", g_np_epoch,
                 g_np_epoch == 1 ? "" : "s", g_np_resync_frame);
    }
    if (g_np_links > 1) {
        for (link = 0; link < g_np_links; link++) {
            port_Log("netplay: link %d: a round trip of %u ms%s\n", link + 1,
                     g_np_link[link].ping_ms,
                     g_np_link[link].left ? ", gone" : "");
        }
    }
}
