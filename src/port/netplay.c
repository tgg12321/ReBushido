/* Rollback netplay in the native port (docs/NETPLAY.md, sections 12 and
 * 13): two programs of one build play the whole session, from power on,
 * as one machine with two pads, each the other's second pad over the
 * network (net.c). It is GGPO's way, written in C here: neither GGPO
 * (MIT, C++) nor GekkoNet (C++) can be linked into the headless program,
 * which has no C library.
 *
 * The host runs the game a step at a time (step.c). A step takes one
 * input, a PortInput of both pads, which this file puts in by the
 * input's hook. The local pad's buttons, as the pads' driver read them,
 * are kept for the frame --net-delay frames on and sent to the other
 * program with every input of its own it has not yet acknowledged. The
 * other pad's, until they come, are taken to be what they were last;
 * one that comes and is not what was taken puts the state of its frame
 * back (port_state.h) and runs the frames from there to now again,
 * quietly (g_port_rerun). Neither program runs more than
 * NETPLAY_PREDICT_MAX frames past the other's last input it has.
 *
 * Each program also keeps time with the other, as GGPO does: every
 * datagram says the frame its sender is at and how far ahead it reckons
 * itself, and the one that is ahead holds its window's blanks back a
 * little (g_port_net_hold_hook), so that neither runs on far into what it
 * has to guess. A step that loaded something from the disc is a point
 * both wait at: the next frame is not run until the other's inputs up to
 * it are in, so that a load is not run again by a rollback.
 *
 * Every NETPLAY_HASH_EVERY frames, once both pads' inputs before it are
 * known, the state's hash at that frame is final; each program prints
 * it, sends it with its inputs, and stops on a hash of the other's that
 * is not its own (a desync). */
#include "common.h"
#include "port_disc.h"
#include "port_host.h"
#include "port_net.h"
#include "port_replay.h"
#include "port_room.h"
#include "bb2.h"

/* Frames the state is kept for, and the most run on what is guessed. */
#define NETPLAY_PREDICT_MAX 8
#define NETPLAY_SAVES (NETPLAY_PREDICT_MAX + 2)
/* Inputs kept: a ring, far larger than any delay or window. */
#define NETPLAY_INPUTS 1024
#define NETPLAY_INPUT_MASK (NETPLAY_INPUTS - 1)
/* The most of its inputs a datagram carries. */
#define NETPLAY_SEND_MAX 64
#define NETPLAY_DELAY_DEFAULT 2
#define NETPLAY_DELAY_MAX 8
#define NETPLAY_HASH_EVERY 30
#define NETPLAY_HASHES 64
/* How long a wait for the other program polls, and when it has gone. */
#define NETPLAY_POLL_MS 1
/* A wait sends the inputs again this often. */
#define NETPLAY_RESEND_MS 16
#define NETPLAY_GIVE_UP_MS 15000
#define MS_PER_SECOND 1000
#define NETPLAY_BYES 5
#define NETPLAY_LINGER_MS 3000
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

enum {
    KIND_INPUT = PORT_NET_KIND_NETPLAY,  /* inputs, an ack and a hash */
    KIND_BYE = PORT_NET_KIND_NETPLAY + 1 /* the other has ended */
};

/* An input datagram after the header (whose number is the first frame of
 * its inputs): the frames of this program's inputs it has, the frame of
 * its last final hash and that hash, the frame it is at, how far ahead
 * of the other it reckons itself (sixteenths of a frame), its clock, the
 * last clock of the other's it had and how long ago, how many inputs,
 * then each, a half-word. */
enum {
    INPUT_ACK = PORT_NET_HEADER_BYTES,
    INPUT_HASH_FRAME = INPUT_ACK + WORD_BYTES,
    INPUT_HASH = INPUT_HASH_FRAME + WORD_BYTES,
    INPUT_NOW = INPUT_HASH + 2 * WORD_BYTES,
    INPUT_AHEAD = INPUT_NOW + WORD_BYTES,
    INPUT_STAMP = INPUT_AHEAD + WORD_BYTES,
    INPUT_ECHO = INPUT_STAMP + WORD_BYTES,
    INPUT_HELD = INPUT_ECHO + WORD_BYTES,
    INPUT_COUNT = INPUT_HELD + WORD_BYTES,
    INPUT_FIRST = INPUT_COUNT + HALF_BYTES
};

typedef struct {
    u32 frame;
    u64 hash;
} PortNetplayHash;

static s32 g_np_on;
/* The pad this program plays: 0 or 1. */
static s32 g_np_local;
static u32 g_np_delay = NETPLAY_DELAY_DEFAULT;
static s32 g_np_goes_on_alone;
/* The frame whose input the game takes next. */
static u32 g_np_frame;
/* The local pad's inputs, by frame; those of the other, and how many of
 * its frames have come (all before g_np_remote_count are known). */
static u16 g_np_local_inputs[NETPLAY_INPUTS];
static u32 g_np_local_count;
static u16 g_np_remote_inputs[NETPLAY_INPUTS];
static u32 g_np_remote_count;
/* What was taken for the other's input of each frame run on a guess, and
 * which of its buttons the frame's input depended on (the session's own
 * presses depend on none: port_SessionInput). */
static u16 g_np_guessed[NETPLAY_INPUTS];
static u16 g_np_used[NETPLAY_INPUTS];
/* The frames of this program's inputs the other has. */
static u32 g_np_peer_has;
/* The earliest frame whose guess turned out wrong, or none. */
static u32 g_np_wrong_from;
static s32 g_np_is_wrong;
/* The other has ended, after this many frames of its inputs. */
static s32 g_np_peer_left;
/* The state at the boundary of each of the last frames, and the blank
 * the game was at there. */
static u8 *g_np_saves[NETPLAY_SAVES];
static u32 g_np_save_frames[NETPLAY_SAVES];
static u32 g_np_save_blanks[NETPLAY_SAVES];
/* The hashes of the frames that are hashed: this program's, final from
 * g_np_last_final back, the other's as it sent them (each final), and
 * whether the two of a frame have been compared. */
static PortNetplayHash g_np_hashes[NETPLAY_HASHES];
static PortNetplayHash g_np_peer_hashes[NETPLAY_HASHES];
static u32 g_np_compared[NETPLAY_HASHES];
static PortNetplayHash g_np_last_final;
static u32 g_np_hashes_compared;
/* The input of the step about to run. */
static PortInput g_np_input;
/* A load: the frame after which the next waits for the other's inputs. */
static u32 g_np_sync_until;
/* Time: the other's frame and clock as last heard, the round trip,
 * the blanks a frame takes (sixteenths), how far ahead each side reckons
 * itself, and when this one last held back. */
static u32 g_np_peer_now;
static u32 g_np_peer_stamp;
static u32 g_np_peer_stamp_at;
static s32 g_np_has_peer_stamp;
static u32 g_np_ping_ms;
static s32 g_np_has_ping;
static u32 g_np_blanks_per_frame = BLANKS_PER_FRAME_START;
static s32 g_np_ahead[NETPLAY_ADVANTAGES];
static s32 g_np_peer_ahead[NETPLAY_ADVANTAGES];
static u32 g_np_ahead_count;
static s32 g_np_peer_ahead_now;
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
/* When the inputs were last sent. */
static u32 g_np_sent_at;
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
 * the match's feed (watch.c). A spectator: watching, and its own hashes
 * held to the players'. */
static u32 g_np_fed;
static s32 g_np_watch;
static PortNetplayHash g_np_watch_hashes[NETPLAY_HASHES];
static u32 g_np_watch_held;

void (*g_port_net_hold_hook)(u32 blanks);
void (*g_port_net_wait_hook)(const char *status);
void (*g_port_net_fail_hook)(const char *why);
void (*g_port_net_rerun_hook)(s32 begins, u32 blanks);

void port_SetNetplay(s32 pad, u32 delay) {
    g_np_on = 1;
    g_np_local = pad;
    g_np_delay = delay > NETPLAY_DELAY_MAX ? NETPLAY_DELAY_MAX : delay;
}

s32 port_IsNetplayOn(void) { return g_np_on; }

void port_SetNetplayGoesOnAlone(s32 on) { g_np_goes_on_alone = on; }

void port_SetNetplayInProcess(s32 on) { g_np_in_process = on; }

void port_SetNetplayOwnPadOnly(s32 on) { g_np_own_pad_only = on; }

void port_SetNetplayLeaveAt(u32 frame) { g_np_leave_at = frame; }

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

/* Everything of a session that the next one must not find (port_net.h):
 * the inputs, the guesses, the hashes, the time and the counts. The
 * states' buffers are kept for it. */
void port_ResetNetplay(void) {
    u32 i;

    g_np_on = 0;
    g_np_local = 0;
    g_np_delay = NETPLAY_DELAY_DEFAULT;
    g_np_frame = 0;
    g_np_local_count = 0;
    g_np_remote_count = 0;
    g_np_peer_has = 0;
    g_np_wrong_from = 0;
    g_np_is_wrong = 0;
    g_np_peer_left = 0;
    for (i = 0; i < NETPLAY_INPUTS; i++) {
        g_np_local_inputs[i] = 0;
        g_np_remote_inputs[i] = 0;
        g_np_guessed[i] = 0;
        g_np_used[i] = 0;
    }
    for (i = 0; i < NETPLAY_SAVES; i++) {
        g_np_save_frames[i] = (u32)-1;
        g_np_save_blanks[i] = 0;
    }
    for (i = 0; i < NETPLAY_HASHES; i++) {
        g_np_hashes[i].frame = 0;
        g_np_hashes[i].hash = 0;
        g_np_peer_hashes[i] = g_np_hashes[i];
        g_np_compared[i] = 0;
    }
    g_np_last_final.frame = 0;
    g_np_last_final.hash = 0;
    g_np_hashes_compared = 0;
    g_np_sync_until = 0;
    g_np_peer_now = 0;
    g_np_peer_stamp = 0;
    g_np_peer_stamp_at = 0;
    g_np_has_peer_stamp = 0;
    g_np_ping_ms = 0;
    g_np_has_ping = 0;
    g_np_blanks_per_frame = BLANKS_PER_FRAME_START;
    for (i = 0; i < NETPLAY_ADVANTAGES; i++) {
        g_np_ahead[i] = 0;
        g_np_peer_ahead[i] = 0;
    }
    g_np_ahead_count = 0;
    g_np_peer_ahead_now = 0;
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
    g_np_sent_at = 0;
    g_np_in_process = 0;
    g_np_ended = 0;
    g_np_leave_asked_at = 0;
    g_np_leave = FALSE;
    g_np_leave_at = 0;
    g_np_fed = 0;
    g_np_watch = FALSE;
    g_np_watch_held = 0;
    for (i = 0; i < NETPLAY_HASHES; i++) {
        g_np_watch_hashes[i].frame = (u32)-1;
    }
    g_port_take_hook = NULL;
    g_port_movies_skipped = FALSE;
}

/* ---- the datagrams ---- */

/* This program's inputs the other does not have yet, its count of the
 * other's, its last final hash, and its time. */
static void port_NetplaySendInputs(void) {
    u8 out[PORT_NET_DATAGRAM_MAX];
    u32 first = g_np_peer_has;
    u32 count = g_np_local_count - first;
    u32 now = port_NetClock();
    u32 i;

    if (count > NETPLAY_SEND_MAX) {
        first = g_np_local_count - NETPLAY_SEND_MAX;
        count = NETPLAY_SEND_MAX;
    }
    port_NetHeader(out, KIND_INPUT, 0, first);
    port_NetPutWord(out + INPUT_ACK, g_np_remote_count);
    port_NetPutWord(out + INPUT_HASH_FRAME, g_np_last_final.frame);
    port_NetPutWord(out + INPUT_HASH, (u32)g_np_last_final.hash);
    port_NetPutWord(out + INPUT_HASH + WORD_BYTES,
                    (u32)(g_np_last_final.hash >> WORD_BITS));
    port_NetPutWord(out + INPUT_NOW, g_np_frame);
    port_NetPutWord(
        out + INPUT_AHEAD,
        (u32)(g_np_ahead_count != 0
                  ? g_np_ahead[(g_np_ahead_count - 1) % NETPLAY_ADVANTAGES]
                  : 0));
    port_NetPutWord(out + INPUT_STAMP, now);
    port_NetPutWord(out + INPUT_ECHO, g_np_peer_stamp);
    port_NetPutWord(
        out + INPUT_HELD, g_np_has_peer_stamp ? now - g_np_peer_stamp_at : 0);
    out[INPUT_COUNT] = (u8)count;
    out[INPUT_COUNT + 1] = (u8)(count >> BITS_PER_BYTE);
    for (i = 0; i < count; i++) {
        u16 input = g_np_local_inputs[(first + i) & NETPLAY_INPUT_MASK];

        out[INPUT_FIRST + HALF_BYTES * i] = (u8)input;
        out[INPUT_FIRST + HALF_BYTES * i + 1] = (u8)(input >> BITS_PER_BYTE);
    }
    port_NetSend(out, INPUT_FIRST + HALF_BYTES * count);
    g_np_sent_at = now;
}

/* The other's hash of `frame` against this program's own, once both are
 * final. This program's is not before every input ahead of the frame is
 * known and the rollback a wrong guess among them asks for is done: a
 * hash of the other's can come in the datagram, or the burst of them,
 * that brings the input which shows this program's own to have been made
 * on a wrong guess (docs/NETPLAY.md, section 12, "Under load"). */
static void port_NetplayCompareHash(u32 frame) {
    s32 slot = (s32)((frame / NETPLAY_HASH_EVERY) % NETPLAY_HASHES);
    const PortNetplayHash *mine = &g_np_hashes[slot];
    const PortNetplayHash *theirs = &g_np_peer_hashes[slot];

    if (frame == 0 || frame > g_np_last_final.frame || mine->frame != frame ||
        theirs->frame != frame || g_np_compared[slot] == frame) {
        return;
    }
    g_np_compared[slot] = frame;
    g_np_hashes_compared++;
    if (mine->hash != theirs->hash) {
        port_Log("netplay: DESYNC at frame %u: this program's state hash is "
                 "%08X%08X, the other's %08X%08X\n",
                 mine->frame, (u32)(mine->hash >> WORD_BITS), (u32)mine->hash,
                 (u32)(theirs->hash >> WORD_BITS), (u32)theirs->hash);
        if (!g_np_in_process) {
            port_Quit(PORT_EXIT_FAILED);
        }
        g_np_ended = PORT_NETPLAY_END_DESYNC;
    }
}

/* The other's time, from one of its datagrams: the round trip, and the
 * frame it is at. */
static void port_NetplayTakeTime(const u8 *in) {
    u32 now = port_NetClock();
    u32 echo = port_NetGetWord(in + INPUT_ECHO);
    u32 held = port_NetGetWord(in + INPUT_HELD);
    u32 their_now = port_NetGetWord(in + INPUT_NOW);

    g_np_peer_stamp = port_NetGetWord(in + INPUT_STAMP);
    g_np_peer_stamp_at = now;
    g_np_has_peer_stamp = 1;
    if (echo != 0 && now - echo >= held) {
        u32 trip = now - echo - held;

        g_np_ping_ms =
            g_np_has_ping
                ? (g_np_ping_ms * (PING_SMOOTH - 1) + trip) / PING_SMOOTH
                : trip;
        g_np_has_ping = 1;
    }
    if (their_now >= g_np_peer_now) {
        g_np_peer_now = their_now;
    }
    g_np_peer_ahead_now = (s32)port_NetGetWord(in + INPUT_AHEAD);
}

/* One datagram of the other's: its inputs that are new, and a wrong guess
 * among them noted. */
static void port_NetplayTake(const u8 *in, s32 bytes) {
    s32 detail;
    u32 first;
    s32 kind = port_NetReadHeader(in, bytes, &detail, &first);
    PortNetplayHash hash;
    u32 count;
    u32 i;

    if (kind == KIND_BYE) {
        g_np_peer_left = 1;
        return;
    }
    if (kind != KIND_INPUT || bytes < INPUT_FIRST) {
        return;
    }
    count = (u32)in[INPUT_COUNT] | ((u32)in[INPUT_COUNT + 1] << BITS_PER_BYTE);
    if ((u32)bytes < INPUT_FIRST + HALF_BYTES * count) {
        return;
    }
    if (port_NetGetWord(in + INPUT_ACK) > g_np_peer_has) {
        g_np_peer_has = port_NetGetWord(in + INPUT_ACK);
    }
    port_NetplayTakeTime(in);
    hash.frame = port_NetGetWord(in + INPUT_HASH_FRAME);
    hash.hash =
        port_NetGetWord(in + INPUT_HASH) |
        ((u64)port_NetGetWord(in + INPUT_HASH + WORD_BYTES) << WORD_BITS);
    if (hash.frame != 0 && hash.frame % NETPLAY_HASH_EVERY == 0) {
        g_np_peer_hashes[(hash.frame / NETPLAY_HASH_EVERY) % NETPLAY_HASHES] =
            hash;
        port_NetplayCompareHash(hash.frame);
    }
    for (i = 0; i < count; i++) {
        u32 frame = first + i;
        u16 input;

        if (frame != g_np_remote_count) {
            continue;
        }
        input = (u16)(in[INPUT_FIRST + HALF_BYTES * i] |
                      (in[INPUT_FIRST + HALF_BYTES * i + 1] << BITS_PER_BYTE));
        g_np_remote_inputs[frame & NETPLAY_INPUT_MASK] = input;
        g_np_remote_count++;
        /* a frame already run on a guess that was not this, where the
         * frame's input depended on what differs */
        if (frame < g_np_frame &&
            ((g_np_guessed[frame & NETPLAY_INPUT_MASK] ^ input) &
             g_np_used[frame & NETPLAY_INPUT_MASK]) != 0 &&
            (!g_np_is_wrong || frame < g_np_wrong_from)) {
            g_np_wrong_from = frame;
            g_np_is_wrong = 1;
        }
    }
}

/* Everything that has come, waiting up to `wait_ms` for the first. */
static void port_NetplayReceive(u32 wait_ms) {
    u8 in[PORT_NET_DATAGRAM_MAX];
    s32 got;

    for (got = port_NetReceive(in, sizeof(in), wait_ms); got > 0;
         got = port_NetReceive(in, sizeof(in), 0)) {
        port_NetplayTake(in, got);
    }
}

/* ---- time ---- */

/* How far ahead of the other this program is, in sixteenths of a frame:
 * the frame the other said it was at, and the frames of half a round
 * trip since it said so, against this one's. As GGPO reckons it: the
 * time since the other's last word is not counted, for a program that
 * waits says the same frame again and has not gone on. */
static s32 port_NetplayAhead(void) {
    u32 frame_us = g_np_blanks_per_frame * BLANK_MICROSECONDS / SIXTEENTHS;
    s32 theirs;

    if (frame_us == 0) {
        return 0;
    }
    theirs =
        (s32)(g_np_peer_now * SIXTEENTHS +
              g_np_ping_ms / 2 * MICROSECONDS_PER_MS * SIXTEENTHS / frame_us);
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

/* GGPO's time sync: where this program reckons itself further ahead than
 * the other reckons itself, it holds the window back half the difference
 * (the other, being behind, catches up by the same). Only the window has
 * time to keep; the headless programs run as fast as they go. */
static void port_NetplayKeepTime(void) {
    s32 slot = (s32)(g_np_ahead_count % NETPLAY_ADVANTAGES);
    s32 hold;

    if (!g_np_has_peer_stamp) {
        return;
    }
    g_np_ahead[slot] = port_NetplayAhead();
    g_np_peer_ahead[slot] = g_np_peer_ahead_now;
    g_np_ahead_count++;
    g_np_ahead_sum +=
        g_np_ahead[slot] < 0 ? -g_np_ahead[slot] : g_np_ahead[slot];
    if (g_port_net_hold_hook == NULL || g_np_ahead_count < NETPLAY_ADVANTAGES ||
        g_np_frame - g_np_held_at < NETPLAY_HOLD_EVERY) {
        return;
    }
    hold = (port_NetplayAverage(g_np_ahead) -
            port_NetplayAverage(g_np_peer_ahead)) /
           2;
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

/* The other pad's input of a frame: what came, or the guess. */
static u16 port_NetplayRemoteInput(u32 frame) {
    if (frame < g_np_remote_count) {
        return g_np_remote_inputs[frame & NETPLAY_INPUT_MASK];
    }
    if (g_np_remote_count == 0) {
        return 0;
    }
    return g_np_remote_inputs[(g_np_remote_count - 1) & NETPLAY_INPUT_MASK];
}

/* The game takes its input (pad.c, port_TakeInput): the frame's. */
static void port_NetplayTakeHook(PortInput *input) { *input = g_np_input; }

/* The state at the boundary of `frame`, into its place among the saves;
 * and the frame's hash, if it is one that is hashed. */
static void port_NetplaySave(u32 frame) {
    s32 slot = (s32)(frame % NETPLAY_SAVES);

    port_StateSave(g_np_saves[slot]);
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

/* Runs the game's frame g_np_frame with both pads' inputs. */
static void port_NetplayRunFrame(void) {
    u32 frame = g_np_frame;
    u16 local = g_np_local_inputs[frame & NETPLAY_INPUT_MASK];
    u16 remote = port_NetplayRemoteInput(frame);
    u32 blank = port_GetBlankCount();
    u32 sectors = g_port_disc_sectors_read;
    u16 used;

    port_NetplaySave(frame);
    if (frame == 0) {
        port_SessionSeed();
    }
    /* a replay of the session's fights (port_replay.h) */
    port_ReplayBoundary();
    if (frame >= g_np_remote_count) {
        g_np_guessed[frame & NETPLAY_INPUT_MASK] = remote;
    }
    g_np_input.buttons[g_np_local] = local;
    g_np_input.buttons[1 - g_np_local] = remote;
    g_np_input.present[0] = 1;
    g_np_input.present[1] = 1;
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

/* The hashes that have become final: every frame before them has both
 * pads' inputs. Each is printed, and the last is sent. */
static void port_NetplayFinalHashes(void) {
    u32 frame =
        (g_np_last_final.frame / NETPLAY_HASH_EVERY + 1) * NETPLAY_HASH_EVERY;

    while (frame < g_np_frame && frame <= g_np_remote_count && !g_np_is_wrong) {
        const PortNetplayHash *hash =
            &g_np_hashes[(frame / NETPLAY_HASH_EVERY) % NETPLAY_HASHES];

        if (hash->frame != frame) {
            break;
        }
        g_np_last_final = *hash;
        port_Log("netplay: frame %u hash %08X%08X\n", frame,
                 (u32)(hash->hash >> WORD_BITS), (u32)hash->hash);
        port_FeedHash(frame, hash->hash);
        port_NetplayCompareHash(frame);
        frame += NETPLAY_HASH_EVERY;
    }
}

/* The local pad's input for the frame g_np_delay on: the driver's read at
 * the last blank, as the game would have taken it. In a session the
 * player plays the session's pad with whichever of this PC's pads they
 * hold (the first player's keys, the first controller), so both are
 * read. */
static void port_NetplayKeepLocal(void) {
    PortInput read;

    while (g_np_local_count <= g_np_frame + g_np_delay) {
        u16 buttons = 0;

        /* the first frames of the delay have no press */
        if (g_np_local_count >= g_np_delay) {
            port_DriverInput(&read);
            buttons = read.buttons[g_np_local];
            if (port_GetSessionRole() != PORT_SESSION_NONE &&
                !g_np_own_pad_only) {
                buttons |= read.buttons[1 - g_np_local];
            }
        }
        g_np_local_inputs[g_np_local_count & NETPLAY_INPUT_MASK] = buttons;
        g_np_local_count++;
    }
}

/* The other has gone and every input of its is run. The headless program
 * ends there too; the window's plays on alone, its pads both its own; a
 * session begun in the running program ends, and the program goes back
 * to its own play (online.c). */
static void port_NetplayEndWithPeer(const char *why) {
    port_Log("netplay: the other program %s after frame %u; %s\n", why,
             g_np_remote_count,
             g_np_in_process      ? "the session ends here"
             : g_np_goes_on_alone ? "this one plays on alone"
                                  : "this one ends there");
    if (g_np_in_process) {
        g_np_ended = PORT_NETPLAY_END_PEER_LEFT;
        return;
    }
    if (!g_np_goes_on_alone) {
        port_Quit(PORT_EXIT_OK);
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

/* Waits for the other's inputs until `enough` says there are enough,
 * sending this program's again now and then; the window is told, and
 * keeps its events read. */
static void port_NetplayWait(s32 state, s32 (*enough)(void)) {
    u32 from = port_NetClock();

    g_np_wait_from = from;
    while (!enough() && !g_np_peer_left && !g_np_ended && !g_np_leave) {
        u32 now = port_NetClock();

        g_np_waits++;
        if (now - from > NETPLAY_GIVE_UP_MS) {
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
        if (now - g_np_sent_at > NETPLAY_RESEND_MS) {
            port_NetplaySendInputs();
        }
        /* a room's member stays in it, and its match's feed goes on */
        port_RoomTick();
        port_FeedTick();
        if (now - from > NETPLAY_WAIT_SHOWN_MS) {
            g_np_state = state;
            if (g_port_net_wait_hook != NULL) {
                g_port_net_wait_hook(state == PORT_NETPLAY_LOADING
                                         ? "Loading: waiting for the other "
                                           "player"
                                         : "Waiting for the other player");
            }
        }
        port_NetplayReceive(NETPLAY_POLL_MS);
    }
    g_np_state = PORT_NETPLAY_PLAYING;
}

static s32 port_NetplayInWindow(void) {
    return g_np_frame < g_np_remote_count + NETPLAY_PREDICT_MAX;
}

static s32 port_NetplayLoadIsShared(void) {
    return g_np_remote_count >= g_np_sync_until;
}

/* A room's match's host: each frame whose two pads are known (this
 * program's and the other's, neither a guess) to the match's feed, the
 * first pad first; and the room stayed in. */
static void port_NetplayFeed(void) {
    u32 final = g_np_local_count < g_np_remote_count ? g_np_local_count
                                                     : g_np_remote_count;

    port_RoomTick();
    if (!port_FeedIsOn()) {
        return;
    }
    while (g_np_fed < final) {
        u16 mine = g_np_local_inputs[g_np_fed & NETPLAY_INPUT_MASK];
        u16 theirs = g_np_remote_inputs[g_np_fed & NETPLAY_INPUT_MASK];

        port_FeedFrame(g_np_fed, g_np_local == 0 ? mine : theirs,
                       g_np_local == 0 ? theirs : mine);
        g_np_fed++;
    }
    port_FeedTick();
}

/* ---- a spectator (docs/NETPLAY.md, section 16) ---- */

/* A spectator's own hash of a frame, kept to be held to the players' as
 * it comes; and each such pair held: FALSE at one that differs. */
static s32 port_WatchHold(void) {
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
            return FALSE;
        }
        mine->frame = (u32)-1;
        g_np_watch_held++;
    }
    return TRUE;
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
    g_np_wait_from = 0;
    g_port_take_hook = port_NetplayTakeHook;
    g_port_movies_skipped = TRUE;
    port_Log("watch: from frame %u, as the match's players ran it\n", frame);
    for (;;) {
        u32 known;
        u32 left;
        u16 first;
        u16 second;
        u16 used;

        port_RoomTick();
        port_WatchTick();
        if (!port_WatchHold()) {
            return PORT_NETPLAY_END_DESYNC;
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
        g_np_remote_count = known;
        /* far behind (come in the middle of a fight): unseen, unheard */
        g_port_rerun = known - g_np_frame > WATCH_CATCH_UP;
        if (g_np_frame % NETPLAY_HASH_EVERY == 0) {
            PortNetplayHash *mine =
                &g_np_watch_hashes[(g_np_frame / NETPLAY_HASH_EVERY) %
                                   NETPLAY_HASHES];

            mine->frame = g_np_frame;
            mine->hash = port_StateHash();
            port_Log("netplay: frame %u hash %08X%08X\n", g_np_frame,
                     (u32)(mine->hash >> WORD_BITS), (u32)mine->hash);
        }
        port_ReplayBoundary();
        port_WatchInput(g_np_frame, &first, &second);
        g_np_input.buttons[0] = first;
        g_np_input.buttons[1] = second;
        g_np_input.present[0] = 1;
        g_np_input.present[1] = 1;
        port_SessionInput(g_np_frame, &g_np_input, &used);
        port_StepGame();
        g_port_rerun = 0;
        g_np_frame++;
    }
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
    g_port_take_hook = port_NetplayTakeHook;
    g_port_movies_skipped = TRUE;
    /* the game to its first boundary: the input of frame 0 is next */
    port_StepGame();
    for (;;) {
        port_NetplayKeepLocal();
        port_NetplaySendInputs();
        port_NetplayReceive(0);
        /* no further than NETPLAY_PREDICT_MAX past the other's inputs, and
         * past a load only with the other */
        port_NetplayWait(PORT_NETPLAY_WAITING, port_NetplayInWindow);
        if (g_np_remote_count < g_np_sync_until) {
            g_np_load_waits++;
            port_NetplayWait(PORT_NETPLAY_LOADING, port_NetplayLoadIsShared);
        }
        if (g_np_is_wrong && !g_np_ended) {
            port_NetplayRollBack();
        }
        port_NetplayFinalHashes();
        port_NetplayFeed();
        /* (the session's own end first: the other, ending there too, may
         * have said goodbye already) */
        if (g_np_in_process && !g_np_ended && !g_np_is_wrong) {
            /* a way back to the top menu, or a room's match decided,
             * taken on frames both have the inputs of, is the session's
             * end on both (session.c) */
            u32 left = port_SessionLeftAt();

            if (left != 0 && left - 1 < g_np_remote_count) {
                if (port_SessionResult() != 0) {
                    port_Log("netplay: the match was decided at frame %u: "
                             "the session ends there\n",
                             left - 1);
                    g_np_ended = PORT_NETPLAY_END_MATCH;
                } else {
                    port_Log("netplay: the players went back to the top "
                             "menu at frame %u: the session ends there\n",
                             left - 1);
                    g_np_ended = PORT_NETPLAY_END_LEFT;
                }
            }
        }
        if (g_np_peer_left && g_np_frame >= g_np_remote_count && !g_np_ended) {
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
        port_NetplayKeepTime();
        port_NetplayRunFrame();
    }
}

u32 port_NetplayFinalFrames(void) {
    if (g_np_is_wrong && g_np_wrong_from < g_np_remote_count) {
        return g_np_wrong_from;
    }
    return g_np_remote_count;
}

/* What netplay is doing now, for the window's line (port_net.h). */
void port_GetNetplayStats(PortNetplayStats *stats) {
    u32 now = port_NetClock();

    stats->is_on = g_np_on || g_np_state == PORT_NETPLAY_ALONE;
    stats->pad = g_np_local;
    stats->state = g_np_state;
    stats->delay = g_np_delay;
    stats->ping_ms = g_np_ping_ms;
    stats->rollback =
        now - g_np_recent_at <= NETPLAY_RECENT_MS ? g_np_recent : 0;
    stats->rollbacks = g_np_rollbacks;
    stats->ahead = g_np_ahead_count != 0
                       ? g_np_ahead[(g_np_ahead_count - 1) % NETPLAY_ADVANTAGES]
                       : 0;
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
}

/* For a moment after this program ends, until the other has said
 * nothing for NETPLAY_QUIET_MS: whatever it sends is answered with this
 * program's inputs, so that the last of them are not lost with it. */
static void port_NetplayLinger(void) {
    u8 in[PORT_NET_DATAGRAM_MAX];
    u32 start = port_NetClock();
    u32 heard = start;

    for (;;) {
        u32 now = port_NetClock();

        if (now - start > NETPLAY_LINGER_MS || now - heard > NETPLAY_QUIET_MS) {
            return;
        }
        if (port_NetReceive(in, sizeof(in), NETPLAY_POLL_MS) > 0) {
            heard = port_NetClock();
            port_NetplaySendInputs();
        }
    }
}

/* The run ends (main.c, port_Quit), or the other has gone: the other is
 * told, a few times, and the report. */
void port_EndNetplay(void) {
    u8 out[PORT_NET_HEADER_BYTES];
    s32 i;

    if (!g_np_on) {
        return;
    }
    g_np_on = 0;
    port_NetplaySendInputs();
    port_NetHeader(out, KIND_BYE, 0, g_np_local_count);
    for (i = 0; i < NETPLAY_BYES; i++) {
        port_NetSend(out, sizeof(out));
    }
    if (!g_np_peer_left) {
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
        g_np_ping_ms,
        g_np_ahead_count != 0 ? g_np_ahead_sum / g_np_ahead_count / SIXTEENTHS
                              : 0,
        g_np_ahead_count != 0 ? g_np_ahead_sum * HUNDREDTHS / g_np_ahead_count /
                                    SIXTEENTHS % HUNDREDTHS
                              : 0,
        g_np_holds, g_np_held_ms);
}
