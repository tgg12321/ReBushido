/* Netplay's state hashes: the other's compared with this program's only once
 * both are final. */
/* Rollback netplay (src/port/netplay.c; docs/NETPLAY.md, section 12)
 * prints and sends a state hash every 30 frames once both pads' inputs
 * before it are known, and stops on a hash of the other's that is not its
 * own. A program that has run past a hashed frame on a guess has a hash
 * of that frame which a rollback will make again. The other's final hash
 * of the frame can come in the very datagram, or the same burst of them,
 * that brings the input showing the guess to be wrong: compared there,
 * before the rollback, the two differed and the session stopped on a
 * desync that was not one, now and then under load (a PC busy with
 * something else wakes the program to a burst). Held here, step by step
 * and with no network or clock: the game is a hash of the inputs it took,
 * and the other's datagrams are made in this file. */
#include "common.h"
#include "port_host.h"
#include "port_net.h"
#include "port_platform.h"
#include "port_state.h"
#include "src/port/net.c"
#include "src/port/netplay.c"
#include "runtime/runtime.h"

/* The frame the other's inputs come up to, the frame of the wrong guess,
 * and the frames this program runs on before they come. */
#define TEST_KNOWN 28
#define TEST_CHANGE 29
#define TEST_RUN_TO 33
#define TEST_HASHED 30
/* The other's buttons from TEST_CHANGE on. */
#define TEST_PRESS 0x4000

/* ---- the game: a hash of every input it took ---- */

static u64 g_test_game;
static u32 g_test_blank;
static u32 g_test_desyncs;
static u32 g_test_sessions;

static u64 test_Mix(u64 state, const PortInput *input) {
    state ^= (u64)input->buttons[0] | ((u64)input->buttons[1] << 16);
    state *= 0x100000001B3ULL;
    return state ^ (state >> 29);
}

void port_StepGame(void) {
    PortInput input;

    g_port_take_hook(&input);
    g_test_game = test_Mix(g_test_game, &input);
    g_test_blank += 2;
}

u32 port_StateSize(void) { return sizeof(g_test_game); }

void port_StateSave(void *buf) {
    test_Copy(buf, &g_test_game, sizeof(g_test_game));
}

void port_StateLoad(const void *buf) {
    test_Copy(&g_test_game, buf, sizeof(g_test_game));
}

u64 port_StateHash(void) { return g_test_game; }

u32 port_GetBlankCount(void) { return g_test_blank; }

/* ---- what else netplay reaches ---- */

s32 g_port_rerun;
void (*g_port_take_hook)(PortInput *input);
s32 g_port_movies_skipped;
u32 g_port_disc_sectors_read;

void port_SessionSeed(void) {}

/* no replay is recorded here (src/port/replay.c) */
void port_ReplayBoundary(void) {}

void port_SessionInput(u32 frame, PortInput *input, u16 *used) {
    (void)frame;
    (void)input;
    *used = 0xFFFF;
}

s32 port_GetSessionRole(void) { return PORT_SESSION_NONE; }

void port_DriverInput(PortInput *input) {
    input->buttons[0] = 0;
    input->buttons[1] = 0;
}

u32 port_HostTicks(void) { return 0; }

void *port_HostAllocate(u32 size) { return test_Alloc(size); }

s32 port_HostUdpSend(
    s32 socket, const PortNetAddress *to, const void *bytes, u32 count) {
    (void)socket;
    (void)to;
    (void)bytes;
    return (s32)count;
}

s32 port_HostUdpReceive(
    s32 socket, void *bytes, u32 room, PortNetAddress *from, u32 wait_ms) {
    (void)socket;
    (void)bytes;
    (void)room;
    (void)from;
    (void)wait_ms;
    return -1;
}

static s32 test_StartsWith(const char *text, const char *start) {
    while (*start != '\0') {
        if (*text++ != *start++) {
            return 0;
        }
    }
    return 1;
}

void port_Log(const char *format, ...) {
    if (test_StartsWith(format, "netplay: DESYNC")) {
        g_test_desyncs++;
    }
}

void port_Fail(const char *format, ...) {
    test_Count(0, __FILE__, __LINE__, "netplay failed: %s", format);
    for (;;) {
    }
}

void port_Quit(s32 code) {
    test_Count(0, __FILE__, __LINE__,
               "the session stopped itself (%d) on a desync that is not one",
               code);
    for (;;) {
    }
}

/* ---- the other program ---- */

/* The other's buttons at a frame. */
static u16 test_TheirInput(u32 frame) {
    return frame >= TEST_CHANGE ? TEST_PRESS : 0;
}

/* The hash both programs must reach at `frame`: the game run on every
 * input as it really was, this program's pad (the first) never pressed. */
static u64 test_TrueHash(u32 frame) {
    PortInput input;
    u64 state = 0;
    u32 i;

    input.buttons[0] = 0;
    for (i = 0; i < frame; i++) {
        input.buttons[1] = test_TheirInput(i);
        state = test_Mix(state, &input);
    }
    return state;
}

/* A datagram of the other's: its inputs from `first` to before `end`, and
 * its last final hash, of `hashed` (0 for none). */
static void test_TheyMayTell(u32 first, u32 end, u32 hashed) {
    u8 in[PORT_NET_DATAGRAM_MAX];
    u64 hash = hashed != 0 ? test_TrueHash(hashed) : 0;
    u32 i;

    port_NetHeader(in, KIND_INPUT, 0, first);
    port_NetPutWord(in + INPUT_ACK, TEST_RUN_TO + NETPLAY_DELAY_DEFAULT);
    port_NetPutWord(in + INPUT_HASH_FRAME, hashed);
    port_NetPutWord(in + INPUT_HASH, (u32)hash);
    port_NetPutWord(in + INPUT_HASH + WORD_BYTES, (u32)(hash >> WORD_BITS));
    port_NetPutWord(in + INPUT_NOW, end);
    port_NetPutWord(in + INPUT_AHEAD, 0);
    port_NetPutWord(in + INPUT_STAMP, 0);
    port_NetPutWord(in + INPUT_ECHO, 0);
    port_NetPutWord(in + INPUT_HELD, 0);
    in[INPUT_COUNT] = (u8)(end - first);
    in[INPUT_COUNT + 1] = 0;
    for (i = first; i < end; i++) {
        in[INPUT_FIRST + HALF_BYTES * (i - first)] = (u8)test_TheirInput(i);
        in[INPUT_FIRST + HALF_BYTES * (i - first) + 1] =
            (u8)(test_TheirInput(i) >> BITS_PER_BYTE);
    }
    port_NetplayTake(in, INPUT_FIRST + HALF_BYTES * (end - first));
}

/* ---- the session, step by step as port_RunNetplay takes it ---- */

/* A new session: nothing run, nothing known, this program the first pad. */
static void test_Begin(void) {
    s32 i;

    g_test_game = 0;
    g_test_blank = 0;
    g_test_desyncs = 0;
    port_SetNetplay(0, NETPLAY_DELAY_DEFAULT);
    g_np_frame = 0;
    g_np_local_count = 0;
    g_np_remote_count = 0;
    g_np_peer_has = 0;
    g_np_is_wrong = 0;
    g_np_last_final.frame = 0;
    g_np_last_final.hash = 0;
    g_np_hashes_compared = 0;
    for (i = 0; i < NETPLAY_HASHES; i++) {
        g_np_hashes[i].frame = 0;
        g_np_peer_hashes[i].frame = 0;
        g_np_compared[i] = 0;
    }
    for (i = 0; i < NETPLAY_SAVES; i++) {
        if (g_np_saves[i] == NULL) {
            g_np_saves[i] = port_HostAllocate(port_StateSize());
        }
        g_np_save_frames[i] = (u32)-1;
    }
    g_port_take_hook = port_NetplayTakeHook;
}

/* One turn of port_RunNetplay's loop that has no wait: the rollback a
 * wrong guess asks for, the hashes that are final, and the next frame. */
static void test_Turn(void) {
    port_NetplayKeepLocal();
    if (g_np_is_wrong) {
        port_NetplayRollBack();
    }
    port_NetplayFinalHashes();
    port_NetplayRunFrame();
}

/* The other's inputs to TEST_KNOWN, then this program's frames to
 * TEST_RUN_TO: those from TEST_KNOWN on are run on the guess that the
 * other still holds nothing, and from TEST_CHANGE on the guess is wrong,
 * so that this program's hash of TEST_HASHED is not the one it will be. */
static void test_RunAhead(void) {
    test_Begin();
    test_TheyMayTell(0, TEST_KNOWN, 0);
    while (g_np_frame < TEST_RUN_TO) {
        test_Turn();
    }
    CHECK_EQ(g_np_remote_count, TEST_KNOWN);
    CHECK(g_np_hashes[TEST_HASHED / NETPLAY_HASH_EVERY].frame == TEST_HASHED);
    CHECK(g_np_hashes[TEST_HASHED / NETPLAY_HASH_EVERY].hash !=
          test_TrueHash(TEST_HASHED));
}

/* The rollback that follows, and the hash final on both and compared. */
static void test_CatchUp(void) {
    CHECK(g_np_is_wrong);
    CHECK_EQ(g_np_wrong_from, TEST_CHANGE);
    CHECK_EQ(g_np_hashes_compared, 0);
    test_Turn();
    CHECK_EQ(g_np_last_final.frame, TEST_HASHED);
    CHECK(g_np_last_final.hash == test_TrueHash(TEST_HASHED));
    CHECK_EQ(g_np_hashes_compared, 1);
    CHECK_EQ(g_test_desyncs, 0);
    CHECK_EQ(g_np_rollbacks, ++g_test_sessions);
}

void test_Main(void) {
    /* the other's final hash in the datagram that brings the inputs it
     * was made on */
    test_RunAhead();
    test_TheyMayTell(TEST_KNOWN, TEST_RUN_TO + 1, TEST_HASHED);
    CHECK_EQ(g_test_desyncs, 0);
    test_CatchUp();

    /* the inputs in one datagram and the hash in the next, both read in
     * one burst before the loop's turn comes round to the rollback */
    test_RunAhead();
    test_TheyMayTell(TEST_KNOWN, TEST_RUN_TO + 1, 0);
    test_TheyMayTell(TEST_KNOWN, TEST_RUN_TO + 1, TEST_HASHED);
    CHECK_EQ(g_test_desyncs, 0);
    test_CatchUp();

    /* the hash first, the inputs after it (a datagram that brought them
     * was lost, or came late): kept until this program's is final */
    test_RunAhead();
    test_TheyMayTell(TEST_RUN_TO + 1, TEST_RUN_TO + 1, TEST_HASHED);
    test_TheyMayTell(TEST_KNOWN, TEST_RUN_TO + 1, 0);
    CHECK_EQ(g_test_desyncs, 0);
    test_CatchUp();
}
