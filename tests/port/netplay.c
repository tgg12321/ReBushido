/* Netplay's state hashes: the other's compared with this program's only once
 * both are final; a host of four passing each guest's inputs on; and a
 * guest who leaves a fight of four given to the CPU. */
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
    state ^= (u64)input->buttons[0] | ((u64)input->buttons[1] << 16) |
             ((u64)input->buttons[2] << 32) | ((u64)input->buttons[3] << 48);
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

/* the buttons a frame's input depends on (the session's own presses:
 * none), and whether a fight of four is on (port_duo.h) */
static u16 g_test_used = 0xFFFF;
static s32 g_test_duo_on;

void port_SessionInput(u32 frame, PortInput *input, u16 *used) {
    (void)frame;
    (void)input;
    *used = g_test_used;
}

s32 port_DuoIsOn(void) { return g_test_duo_on; }

/* the game's screen: a fight's (the CPU takes a pad over only there, or
 * between the fights of a match of four) */
s16 g_sys_screen = SCREEN_FIGHT_UPDATE;

/* the match (port_match.h): one on one, of a versus mode */
PortMatch g_port_match;
s16 g_fight_mode;

s32 port_MatchModeHasRules(s32 mode) {
    (void)mode;
    return TRUE;
}

s32 port_GetSessionRole(void) { return PORT_SESSION_NONE; }

/* no room of several, no feed and no spectator here (src/port/room.c,
 * watch.c) */
void port_RoomTick(void) {}

void port_RoomWait(u32 wait_ms) { (void)wait_ms; }

s32 port_FeedIsOn(void) { return 0; }

void port_FeedFrame(u32 frame, const u16 *pads) {
    (void)frame;
    (void)pads;
}

void port_FeedHash(u32 frame, u64 hash) {
    (void)frame;
    (void)hash;
}

void port_FeedTick(void) {}

/* no ranked match either (src/port/ranked.c) */
void port_RankedFrame(u32 frame, u16 first, u16 second) {
    (void)frame;
    (void)first;
    (void)second;
}

void port_RankedHash(u32 frame, u64 hash) {
    (void)frame;
    (void)hash;
}

void port_WatchTick(void) {}

s32 port_WatchHash(u32 frame, u64 *hash) {
    (void)frame;
    (void)hash;
    return 0;
}

s32 port_WatchEnd(u32 *frame, s32 *winner) {
    *frame = 0;
    *winner = 0;
    return 0;
}

u32 port_WatchKnown(void) { return 0; }

u32 port_WatchQuietMs(void) { return 0; }

s32 port_WatchInput(u32 frame, u16 *pads) {
    (void)frame;
    pads[0] = pads[1] = pads[2] = pads[3] = 0;
    return 0;
}

u32 port_SessionLeftAt(void) { return 0; }

s32 port_SessionResult(void) { return 0; }

void port_DriverInput(PortInput *input) {
    input->buttons[0] = 0;
    input->buttons[1] = 0;
}

u32 port_HostTicks(void) { return 0; }

void *port_HostAllocate(u32 size) { return test_Alloc(size); }

/* The last datagram sent on each of the links' sockets (TEST_SOCKET on). */
#define TEST_SOCKET 10
static u8 g_test_sent[PORT_NET_LINKS][PORT_NET_DATAGRAM_MAX];
static u32 g_test_sent_bytes[PORT_NET_LINKS];

s32 port_HostUdpSend(
    s32 socket, const PortNetAddress *to, const void *bytes, u32 count) {
    u32 i;

    (void)to;
    if (socket >= TEST_SOCKET && socket < TEST_SOCKET + PORT_NET_LINKS) {
        for (i = 0; i < count; i++) {
            g_test_sent[socket - TEST_SOCKET][i] = ((const u8 *)bytes)[i];
        }
        g_test_sent_bytes[socket - TEST_SOCKET] = count;
    }
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
    input.buttons[2] = 0;
    input.buttons[3] = 0;
    for (i = 0; i < frame; i++) {
        input.buttons[1] = test_TheirInput(i);
        state = test_Mix(state, &input);
    }
    return state;
}

/* A datagram of link `link`'s: pad `pad`'s inputs from `first` to before
 * `end` (`input` of each frame), its last final hash, `hash` of `hashed`
 * (0 for none), and that it has `others` of every other pad's. */
static void test_LinkTells(s32 link, s32 pad, u32 first, u32 end, u32 hashed,
                           u64 hash, u16 (*input)(u32 frame), u32 others) {
    u8 in[PORT_NET_DATAGRAM_MAX];
    u32 at = INPUT_FIRST_BLOCK;
    u32 i;

    port_NetHeader(in, KIND_INPUT, 0, 0);
    for (i = 0; i < NETPLAY_PADS; i++) {
        port_NetPutWord(
            in + INPUT_HAS + i * WORD_BYTES, (s32)i == pad ? end : others);
    }
    port_NetPutWord(in + INPUT_HASH_FRAME, hashed);
    port_NetPutWord(in + INPUT_HASH, (u32)hash);
    port_NetPutWord(in + INPUT_HASH + WORD_BYTES, (u32)(hash >> WORD_BITS));
    port_NetPutWord(in + INPUT_NOW, end);
    port_NetPutWord(in + INPUT_AHEAD, 0);
    port_NetPutWord(in + INPUT_STAMP, 0);
    port_NetPutWord(in + INPUT_ECHO, 0);
    port_NetPutWord(in + INPUT_HELD, 0);
    port_NetPutWord(in + INPUT_BLOCKS, 1);
    in[at + BLOCK_PAD] = (u8)pad;
    in[at + BLOCK_PAD + 1] = 0;
    in[at + BLOCK_COUNT] = (u8)(end - first);
    in[at + BLOCK_COUNT + 1] = 0;
    port_NetPutWord(in + at + BLOCK_FIRST, first);
    for (i = first; i < end; i++) {
        in[at + BLOCK_INPUTS + HALF_BYTES * (i - first)] = (u8)input(i);
        in[at + BLOCK_INPUTS + HALF_BYTES * (i - first) + 1] =
            (u8)(input(i) >> BITS_PER_BYTE);
    }
    port_NetplayTake(
        link, in, (s32)(at + BLOCK_INPUTS + HALF_BYTES * (end - first)));
}

/* ... the other program's of a session of two: the second pad's. */
static void test_TheyMayTell(u32 first, u32 end, u32 hashed) {
    test_LinkTells(
        0, 1, first, end, hashed, hashed != 0 ? test_TrueHash(hashed) : 0,
        test_TheirInput, TEST_RUN_TO + NETPLAY_DELAY_DEFAULT);
}

/* ---- the session, step by step as port_RunNetplay takes it ---- */

/* A new session: nothing run, nothing known, this program the first pad. */
static void test_Begin(void) {
    s32 i;

    g_test_game = 0;
    g_test_blank = 0;
    g_test_desyncs = 0;
    port_ResetNetplay();
    port_SetNetplay(0, NETPLAY_DELAY_DEFAULT);
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
    CHECK_EQ(g_np_count[1], TEST_KNOWN);
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
    CHECK_EQ(g_np_rollbacks, 1);
    g_test_sessions++;
}

/* ---- a host of four (docs/NETPLAY.md, section 18) ---- */

/* The third player's buttons (the second guest's, on link 1), and the
 * fourth's (link 2), which change at TEST_CHANGE. */
static u16 test_ThirdInput(u32 frame) { return (u16)(frame & 0x0F); }

static u16 test_FourthInput(u32 frame) {
    return frame >= TEST_CHANGE ? TEST_PRESS : 0;
}

static u16 test_NoInput(u32 frame) {
    (void)frame;
    return 0;
}

/* A pad's block in a datagram sent: its count and its first frame, or
 * FALSE where it has none. */
static s32 test_SentBlock(const u8 *out, u32 bytes, s32 pad, u32 *count,
                          u32 *first, const u8 **inputs) {
    u32 at = INPUT_FIRST_BLOCK;
    u32 blocks = port_NetGetWord(out + INPUT_BLOCKS);
    u32 i;

    for (i = 0; i < blocks && at + BLOCK_INPUTS <= bytes; i++) {
        u32 n = (u32)out[at + BLOCK_COUNT] |
                ((u32)out[at + BLOCK_COUNT + 1] << BITS_PER_BYTE);

        if (out[at + BLOCK_PAD] == pad) {
            *count = n;
            *first = port_NetGetWord(out + at + BLOCK_FIRST);
            *inputs = out + at + BLOCK_INPUTS;
            return TRUE;
        }
        at += BLOCK_INPUTS + HALF_BYTES * n;
    }
    return FALSE;
}

/* The host of four: its pad the first, a link for each guest. Each pad's
 * inputs from its own link only; each link sent every pad its program
 * does not play (the host's own and the other guests', passed on); a
 * wrong guess of one guest's pad rolls the session back; and a hash final
 * only once every pad's inputs before it are known. */
static void test_HostOfFour(void) {
    static const s32 sources[NETPLAY_PADS] = {-1, 0, 1, 2};
    const u8 *inputs;
    u32 count;
    u32 first;
    s32 link;
    u32 i;

    test_Begin();
    port_SetNetplayPads(NETPLAY_PADS, sources);
    CHECK_EQ(g_np_links, 3);
    CHECK_EQ(g_np_local_mask, 1);
    for (link = 0; link < PORT_NET_LINKS; link++) {
        g_port_net_links[link].socket = TEST_SOCKET + link;
        g_port_net_links[link].has_peer = 1;
        g_port_net_links[link].is_fixed = 1;
    }
    /* the second's, the third's and the fourth's inputs to TEST_KNOWN */
    test_LinkTells(0, 1, 0, TEST_KNOWN, 0, 0, test_NoInput, 0);
    test_LinkTells(1, 2, 0, TEST_KNOWN, 0, 0, test_ThirdInput, 0);
    test_LinkTells(2, 3, 0, TEST_KNOWN, 0, 0, test_NoInput, 0);
    CHECK_EQ(g_np_count[2], TEST_KNOWN);
    CHECK_EQ(port_NetplayKnown(), 0);
    while (g_np_frame < TEST_RUN_TO) {
        test_Turn();
    }
    CHECK_EQ(port_NetplayKnown(), TEST_KNOWN);
    /* nothing final past what all four have */
    CHECK_EQ(g_np_last_final.frame, 0);
    /* link 0 is sent the host's pad, the third's and the fourth's, from
     * what it has (none: its acknowledgement said so), and never its own */
    port_NetplaySendTo(0);
    CHECK(!test_SentBlock(
        g_test_sent[0], g_test_sent_bytes[0], 1, &count, &first, &inputs));
    CHECK(test_SentBlock(
        g_test_sent[0], g_test_sent_bytes[0], 2, &count, &first, &inputs));
    CHECK_EQ(first, 0);
    CHECK_EQ(count, TEST_KNOWN);
    for (i = 0; i < count; i++) {
        CHECK_EQ(inputs[HALF_BYTES * i], test_ThirdInput(i));
    }
    CHECK(test_SentBlock(
        g_test_sent[0], g_test_sent_bytes[0], 0, &count, &first, &inputs));
    CHECK_EQ(count, g_np_local_count);
    /* the third's inputs sent on link 0, which is not its: not taken */
    test_LinkTells(0, 2, 0, TEST_RUN_TO + 1, 0, 0, test_NoInput, 0);
    CHECK_EQ(g_np_count[2], TEST_KNOWN);
    /* the fourth's inputs past TEST_KNOWN, unlike the guess: a rollback,
     * and the frame of TEST_HASHED final once every pad has come */
    test_LinkTells(
        2, 3, TEST_KNOWN, TEST_RUN_TO + 1, 0, 0, test_FourthInput, 0);
    CHECK(g_np_is_wrong);
    CHECK_EQ(g_np_wrong_from, TEST_CHANGE);
    test_LinkTells(0, 1, TEST_KNOWN, TEST_RUN_TO + 1, 0, 0, test_NoInput, 0);
    test_Turn();
    CHECK_EQ(g_np_rollbacks, 1);
    CHECK_EQ(g_np_last_final.frame, 0);
    test_LinkTells(1, 2, TEST_KNOWN, TEST_RUN_TO + 1, 0, 0, test_ThirdInput, 0);
    test_Turn();
    CHECK_EQ(g_np_last_final.frame, TEST_HASHED);
    /* a guest's goodbye: the pad it said its player had */
    {
        u8 bye[PORT_NET_HEADER_BYTES];

        port_NetHeader(bye, KIND_BYE, 3 + 1, 0);
        port_NetplayTake(2, bye, sizeof(bye));
        CHECK(g_np_link[2].left);
        CHECK_EQ(port_NetplayLeftPad(), 3);
    }
    /* ... with no fight of four on, the session ends as it did */
    g_test_duo_on = FALSE;
    CHECK(!port_NetplayTakeOverLeavers());
    CHECK(port_NetplayPeerGone());
    /* ... nor on the menus of a match of four, which need their players;
     * between its fights (the card after one) the CPU may take him */
    g_port_match.teams = PORT_TEAMS_TWO_PADS;
    g_sys_screen = SCREEN_MENU_UPDATE;
    CHECK(!port_NetplayInFightsOfFour());
    g_sys_screen = SCREEN_AFTER_FIGHT_UPDATE;
    CHECK(port_NetplayInFightsOfFour());
    g_sys_screen = SCREEN_FIGHT_INIT;
    CHECK(port_NetplayInFightsOfFour());
    g_sys_screen = SCREEN_FIGHT_UPDATE;
    g_port_match.teams = PORT_TEAMS_ONE;
    /* in one, the CPU plays the pad on (section 19): from the first frame
     * of it the host lacks, as INPUT_CPU, the frames run past it on a
     * guess run again with the pad the CPU's, and passed on to the
     * others; the session goes on */
    g_test_duo_on = TRUE;
    first = g_np_count[3];
    CHECK(first < g_np_frame);
    CHECK(port_NetplayTakeOverLeavers());
    CHECK_EQ(g_np_source[3], SOURCE_CPU);
    CHECK_EQ(port_NetplayCpuPads(), 1U << 3);
    CHECK_EQ(port_NetplayLeftPad(), -1);
    CHECK(!port_NetplayPeerGone());
    CHECK(!g_np_peer_left);
    /* the rest of his goodbyes change nothing (a wait cut short by each
     * would run past the window) */
    {
        u8 bye[PORT_NET_HEADER_BYTES];

        port_NetHeader(bye, KIND_BYE, 3 + 1, 0);
        port_NetplayTake(2, bye, sizeof(bye));
        CHECK(!g_np_peer_left);
    }
    CHECK_EQ(g_np_count[3], g_np_local_count);
    CHECK_EQ(g_np_inputs[3][first & NETPLAY_INPUT_MASK], INPUT_CPU);
    CHECK(g_np_is_wrong);
    CHECK_EQ(g_np_wrong_from, first);
    test_Turn();
    CHECK(!g_np_is_wrong);
    CHECK_EQ(g_np_input.present[3], PORT_PAD_CPU);
    CHECK_EQ(g_np_input.buttons[3], 0);
    CHECK_EQ(g_np_count[3], g_np_local_count);
    port_NetplaySendTo(0);
    CHECK(test_SentBlock(
        g_test_sent[0], g_test_sent_bytes[0], 3, &count, &first, &inputs));
    CHECK_EQ(first + count, g_np_local_count);
    CHECK_EQ(inputs[HALF_BYTES * (count - 1)], 0xFF);
    CHECK_EQ(inputs[HALF_BYTES * (count - 1) + 1], 0xFF);
    g_test_duo_on = FALSE;
    for (link = 0; link < PORT_NET_LINKS; link++) {
        g_port_net_links[link].socket = -1;
    }
}

/* A guest, its other player's pad passed on by the host as the CPU's
 * from a frame it ran on a guess: run again from there, even where the
 * frame's input depended on no button (the session's own presses). */
static u16 test_CpuInput(u32 frame) {
    (void)frame;
    return INPUT_CPU;
}

static void test_GuestTold(void) {
    test_Begin();
    g_test_used = 0;
    test_TheyMayTell(0, TEST_KNOWN, 0);
    while (g_np_frame < TEST_RUN_TO) {
        test_Turn();
    }
    CHECK(!g_np_is_wrong);
    test_LinkTells(0, 1, TEST_KNOWN, TEST_RUN_TO, 0, 0, test_CpuInput,
                   TEST_RUN_TO + NETPLAY_DELAY_DEFAULT);
    CHECK(g_np_is_wrong);
    CHECK_EQ(g_np_wrong_from, TEST_KNOWN);
    CHECK_EQ(port_NetplayCpuPads(), 1U << 1);
    test_Turn();
    CHECK_EQ(g_np_input.present[1], PORT_PAD_CPU);
    CHECK_EQ(g_np_input.buttons[1], 0);
    g_test_used = 0xFFFF;
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
    CHECK_EQ(g_test_sessions, 3);

    /* four players, the host passing each guest's inputs on */
    test_HostOfFour();
    test_GuestTold();
}
