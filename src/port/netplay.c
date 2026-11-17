/* Rollback netplay in the native port (docs/NETPLAY.md, section 12): two
 * programs of one build play the whole session, from power on, as one
 * machine with two pads, each the other's second pad over the network
 * (net.c). It is GGPO's way, written in C here: neither GGPO (MIT, C++)
 * nor GekkoNet (C++) can be linked into the headless program, which has
 * no C library.
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
 * Every NETPLAY_HASH_EVERY frames, once both pads' inputs before it are
 * known, the state's hash at that frame is final; each program prints
 * it, sends it with its inputs, and stops on a hash of the other's that
 * is not its own (a desync). */
#include "common.h"
#include "port_host.h"
#include "port_net.h"
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
#define NETPLAY_TICKS_PER_MS (PORT_HOST_TICKS_PER_MICROSECOND * 1000)
#define NETPLAY_BYES 5
#define NETPLAY_LINGER_MS 3000
#define NETPLAY_QUIET_MS 500
#define DATAGRAM_MAX 512
#define BITS_PER_BYTE 8
#define WORD_BYTES 4
#define HALF_BYTES 2
#define WORD_BITS 32

enum {
    KIND_INPUT = PORT_NET_KIND_NETPLAY,  /* inputs, an ack and a hash */
    KIND_BYE = PORT_NET_KIND_NETPLAY + 1 /* the other has ended */
};

/* An input datagram after the header (whose number is the first frame of
 * its inputs): the frames of this program's inputs it has, the frame of
 * its last final hash and that hash, how many inputs, then each, a
 * half-word. */
enum {
    INPUT_ACK = PORT_NET_HEADER_BYTES,
    INPUT_HASH_FRAME = INPUT_ACK + WORD_BYTES,
    INPUT_HASH = INPUT_HASH_FRAME + WORD_BYTES,
    INPUT_COUNT = INPUT_HASH + 2 * WORD_BYTES,
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
/* The frame whose input the game takes next. */
static u32 g_np_frame;
/* The local pad's inputs, by frame; those of the other, and how many of
 * its frames have come (all before g_np_remote_count are known). */
static u16 g_np_local_inputs[NETPLAY_INPUTS];
static u32 g_np_local_count;
static u16 g_np_remote_inputs[NETPLAY_INPUTS];
static u32 g_np_remote_count;
/* What was taken for the other's input of each frame run on a guess. */
static u16 g_np_guessed[NETPLAY_INPUTS];
/* The frames of this program's inputs the other has. */
static u32 g_np_peer_has;
/* The earliest frame whose guess turned out wrong, or none. */
static u32 g_np_wrong_from;
static s32 g_np_is_wrong;
/* The other has ended, after this many frames of its inputs. */
static s32 g_np_peer_left;
/* The state at the boundary of each of the last frames. */
static u8 *g_np_saves[NETPLAY_SAVES];
static u32 g_np_save_frames[NETPLAY_SAVES];
/* Final hashes, this program's and the other's last. */
static PortNetplayHash g_np_hashes[NETPLAY_HASHES];
static PortNetplayHash g_np_peer_hash;
static PortNetplayHash g_np_last_final;
static u32 g_np_hashes_compared;
static u32 g_np_compared_frame;
/* The input of the step about to run. */
static PortInput g_np_input;
/* What the report says. */
static u32 g_np_rollbacks;
static u32 g_np_frames_again;
static u32 g_np_longest;
static u32 g_np_waits;
static u32 g_np_rerun_ticks;
/* When the inputs were last sent. */
static u32 g_np_sent_at;

void port_SetNetplay(s32 pad, u32 delay) {
    g_np_on = 1;
    g_np_local = pad;
    g_np_delay = delay > NETPLAY_DELAY_MAX ? NETPLAY_DELAY_MAX : delay;
}

s32 port_IsNetplayOn(void) { return g_np_on; }

/* ---- the datagrams ---- */

static void port_PutWord(u8 *out, u32 value) {
    s32 i;

    for (i = 0; i < WORD_BYTES; i++) {
        out[i] = (u8)(value >> (i * BITS_PER_BYTE));
    }
}

static u32 port_GetWord(const u8 *in) {
    u32 value = 0;
    s32 i;

    for (i = 0; i < WORD_BYTES; i++) {
        value |= (u32)in[i] << (i * BITS_PER_BYTE);
    }
    return value;
}

/* This program's inputs the other does not have yet, its count of the
 * other's, and its last final hash. */
static void port_NetplaySendInputs(void) {
    u8 out[DATAGRAM_MAX];
    u32 first = g_np_peer_has;
    u32 count = g_np_local_count - first;
    u32 i;

    if (count > NETPLAY_SEND_MAX) {
        first = g_np_local_count - NETPLAY_SEND_MAX;
        count = NETPLAY_SEND_MAX;
    }
    port_NetHeader(out, KIND_INPUT, 0, first);
    port_PutWord(out + INPUT_ACK, g_np_remote_count);
    port_PutWord(out + INPUT_HASH_FRAME, g_np_last_final.frame);
    port_PutWord(out + INPUT_HASH, (u32)g_np_last_final.hash);
    port_PutWord(out + INPUT_HASH + WORD_BYTES,
                 (u32)(g_np_last_final.hash >> WORD_BITS));
    out[INPUT_COUNT] = (u8)count;
    out[INPUT_COUNT + 1] = (u8)(count >> BITS_PER_BYTE);
    for (i = 0; i < count; i++) {
        u16 input = g_np_local_inputs[(first + i) & NETPLAY_INPUT_MASK];

        out[INPUT_FIRST + HALF_BYTES * i] = (u8)input;
        out[INPUT_FIRST + HALF_BYTES * i + 1] = (u8)(input >> BITS_PER_BYTE);
    }
    port_NetSend(out, INPUT_FIRST + HALF_BYTES * count);
    g_np_sent_at = port_HostTicks();
}

/* The other's hash of a frame against this program's own. */
static void port_NetplayCompareHash(const PortNetplayHash *theirs) {
    const PortNetplayHash *mine =
        &g_np_hashes[(theirs->frame / NETPLAY_HASH_EVERY) % NETPLAY_HASHES];

    if (theirs->frame == 0 || mine->frame != theirs->frame ||
        theirs->frame == g_np_compared_frame) {
        return;
    }
    g_np_compared_frame = theirs->frame;
    g_np_hashes_compared++;
    if (mine->hash != theirs->hash) {
        port_Log("netplay: DESYNC at frame %u: this program's state hash is "
                 "%08X%08X, the other's %08X%08X\n",
                 mine->frame, (u32)(mine->hash >> WORD_BITS), (u32)mine->hash,
                 (u32)(theirs->hash >> WORD_BITS), (u32)theirs->hash);
        port_Quit(PORT_EXIT_FAILED);
    }
}

/* One datagram of the other's: its inputs that are new, and a wrong guess
 * among them noted. */
static void port_NetplayTake(const u8 *in, s32 bytes) {
    s32 detail;
    u32 first;
    s32 kind = port_NetReadHeader(in, bytes, &detail, &first);
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
    if (port_GetWord(in + INPUT_ACK) > g_np_peer_has) {
        g_np_peer_has = port_GetWord(in + INPUT_ACK);
    }
    g_np_peer_hash.frame = port_GetWord(in + INPUT_HASH_FRAME);
    g_np_peer_hash.hash =
        port_GetWord(in + INPUT_HASH) |
        ((u64)port_GetWord(in + INPUT_HASH + WORD_BYTES) << WORD_BITS);
    port_NetplayCompareHash(&g_np_peer_hash);
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
        /* a frame already run on a guess that was not this */
        if (frame < g_np_frame &&
            g_np_guessed[frame & NETPLAY_INPUT_MASK] != input &&
            (!g_np_is_wrong || frame < g_np_wrong_from)) {
            g_np_wrong_from = frame;
            g_np_is_wrong = 1;
        }
    }
}

/* Everything that has come, waiting up to `wait_ms` for the first. */
static void port_NetplayReceive(u32 wait_ms) {
    u8 in[DATAGRAM_MAX];
    s32 got;

    for (got = port_NetReceive(in, sizeof(in), wait_ms); got > 0;
         got = port_NetReceive(in, sizeof(in), 0)) {
        port_NetplayTake(in, got);
    }
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
    if (frame % NETPLAY_HASH_EVERY == 0) {
        PortNetplayHash *hash =
            &g_np_hashes[(frame / NETPLAY_HASH_EVERY) % NETPLAY_HASHES];

        hash->frame = frame;
        hash->hash = port_StateHash();
    }
}

/* Runs the game's frame g_np_frame with both pads' inputs. */
static void port_NetplayRunFrame(void) {
    u32 frame = g_np_frame;
    u16 local = g_np_local_inputs[frame & NETPLAY_INPUT_MASK];
    u16 remote = port_NetplayRemoteInput(frame);

    port_NetplaySave(frame);
    if (frame >= g_np_remote_count) {
        g_np_guessed[frame & NETPLAY_INPUT_MASK] = remote;
    }
    g_np_input.buttons[g_np_local] = local;
    g_np_input.buttons[1 - g_np_local] = remote;
    g_np_input.present[0] = 1;
    g_np_input.present[1] = 1;
    port_StepGame();
    g_np_frame++;
}

/* A guess was wrong: the state of its frame back, and every frame from
 * there to now run again, quietly. */
static void port_NetplayRollBack(void) {
    u32 now = g_np_frame;
    u32 from = g_np_wrong_from;
    u32 start = port_HostTicks();
    s32 slot = (s32)(from % NETPLAY_SAVES);

    g_np_is_wrong = 0;
    if (g_np_save_frames[slot] != from || now - from > NETPLAY_PREDICT_MAX) {
        port_Fail(
            "netplay: the state of frame %u is not kept (now %u)\n", from, now);
    }
    port_StateLoad(g_np_saves[slot]);
    g_np_frame = from;
    g_port_rerun = 1;
    while (g_np_frame < now) {
        port_NetplayRunFrame();
    }
    g_port_rerun = 0;
    g_np_rollbacks++;
    g_np_frames_again += now - from;
    if (now - from > g_np_longest) {
        g_np_longest = now - from;
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
        port_NetplayCompareHash(&g_np_peer_hash);
        frame += NETPLAY_HASH_EVERY;
    }
}

/* The local pad's input for the frame g_np_delay on: the driver's read at
 * the last blank, as the game would have taken it. */
static void port_NetplayKeepLocal(void) {
    PortInput read;

    while (g_np_local_count <= g_np_frame + g_np_delay) {
        u16 buttons = 0;

        /* the first frames of the delay have no press */
        if (g_np_local_count >= g_np_delay) {
            port_DriverInput(&read);
            buttons = read.buttons[g_np_local];
        }
        g_np_local_inputs[g_np_local_count & NETPLAY_INPUT_MASK] = buttons;
        g_np_local_count++;
    }
}

/* The other has ended and every input of its is run: this one ends too. */
static void port_NetplayEndWithPeer(void) {
    port_Log("netplay: the other program ended after frame %u; this one ends "
             "there\n",
             g_np_remote_count);
    port_Quit(PORT_EXIT_OK);
}

/* The session (port_net.h): the game from its first boundary on, a frame
 * at a time, for ever. */
void port_RunNetplay(void) {
    s32 i;

    for (i = 0; i < NETPLAY_SAVES; i++) {
        g_np_saves[i] = port_HostAllocate(port_StateSize());
        if (g_np_saves[i] == NULL) {
            port_Fail("netplay: no memory for the states (%u bytes)\n",
                      port_StateSize() * NETPLAY_SAVES);
        }
        g_np_save_frames[i] = (u32)-1;
    }
    g_port_take_hook = port_NetplayTakeHook;
    /* the game to its first boundary: the input of frame 0 is next */
    port_StepGame();
    for (;;) {
        u32 waited_from = port_HostTicks();

        port_NetplayKeepLocal();
        port_NetplaySendInputs();
        port_NetplayReceive(0);
        /* no further than NETPLAY_PREDICT_MAX past the other's inputs */
        while (g_np_frame >= g_np_remote_count + NETPLAY_PREDICT_MAX &&
               !g_np_peer_left) {
            g_np_waits++;
            if (port_HostTicks() - waited_from >
                NETPLAY_GIVE_UP_MS * NETPLAY_TICKS_PER_MS) {
                port_Log("netplay: nothing from the other program for %u "
                         "seconds\n",
                         NETPLAY_GIVE_UP_MS / MS_PER_SECOND);
                port_Quit(PORT_EXIT_FAILED);
            }
            /* the inputs again now and then, in case they were lost */
            if (port_HostTicks() - g_np_sent_at >
                NETPLAY_RESEND_MS * NETPLAY_TICKS_PER_MS) {
                port_NetplaySendInputs();
            }
            port_NetplayReceive(NETPLAY_POLL_MS);
        }
        if (g_np_is_wrong) {
            port_NetplayRollBack();
        }
        port_NetplayFinalHashes();
        if (g_np_peer_left && g_np_frame >= g_np_remote_count) {
            port_NetplayEndWithPeer();
        }
        port_NetplayRunFrame();
    }
}

/* For a moment after this program ends, until the other has said
 * nothing for NETPLAY_QUIET_MS: whatever it sends is answered with this
 * program's inputs, so that the last of them are not lost with it. */
static void port_NetplayLinger(void) {
    u8 in[DATAGRAM_MAX];
    u32 start = port_HostTicks();
    u32 heard = start;

    for (;;) {
        u32 now = port_HostTicks();

        if (now - start > NETPLAY_LINGER_MS * NETPLAY_TICKS_PER_MS ||
            now - heard > NETPLAY_QUIET_MS * NETPLAY_TICKS_PER_MS) {
            return;
        }
        if (port_NetReceive(in, sizeof(in), NETPLAY_POLL_MS) > 0) {
            heard = port_HostTicks();
            port_NetplaySendInputs();
        }
    }
}

/* The run ends (main.c, port_Quit): the other is told, a few times. */
void port_EndNetplay(void) {
    u8 out[PORT_NET_HEADER_BYTES];
    s32 i;

    if (!g_np_on) {
        return;
    }
    port_NetplaySendInputs();
    port_NetHeader(out, KIND_BYE, 0, g_np_local_count);
    for (i = 0; i < NETPLAY_BYES; i++) {
        port_NetSend(out, sizeof(out));
    }
    port_NetplayLinger();
    port_Log("netplay: %u frames; %u rollbacks, %u frames run again, the "
             "longest %u; %u ms run again in all; %u waits on the other; "
             "%u hashes compared, the last final one at frame %u\n",
             g_np_frame, g_np_rollbacks, g_np_frames_again, g_np_longest,
             g_np_rerun_ticks / NETPLAY_TICKS_PER_MS, g_np_waits,
             g_np_hashes_compared, g_np_last_final.frame);
}
