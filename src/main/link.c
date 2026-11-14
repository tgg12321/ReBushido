/* Link-cable play: two consoles joined by the serial cable. Every frame each
 * console sends an 8-byte packet with its pad and receives the other's, so
 * both run the same fight from the same inputs. Built on the Sony serial
 * driver (libcomb). */
#include "common.h"
#include "bb2.h"

/* Defined further down. */
static char g_link_device_name[5];
static u8 g_link_active;

#define LINK_BAUD 57600

/* Timeouts, in ticks of root counter 1 (horizontal retraces, about 15,700 a
 * second). The counter is restarted when it gets past COUNTER_RESTART. */
#define COUNTER_RESTART 1024
#define SEND_TIMEOUT 30720    /* ~2 s for the other side to be ready */
#define RECEIVE_TIMEOUT 15360 /* ~1 s for a packet to arrive */
#define RECEIVE_RETRIES 5
#define WRITE_WAIT_POLLS 65536
/* The other console is ready for a packet when its CTS has stayed up this
 * many polls. */
#define CTS_STEADY_POLLS 1000
/* Ticks between exchanges while a wait is ending (~16 ms). */
#define END_WAIT_GAP 256

/* A packet: two words. */
#define PACKET_SIZE 8

/* The first word of a packet. The second word is a 16-bit payload (used only
 * while connecting) over a 16-bit checksum. */
#define PACKET_SIDE_SHIFT 31
#define PACKET_CLOSE 0x40000000 /* the sender is leaving link play */
#define PACKET_CLOSE_SHIFT 30
#define PACKET_WAIT_SHIFT 28 /* LinkWaitState, 2 bits */
#define PACKET_WAIT_MASK 3
#define PACKET_PAD_TYPE_SHIFT 16
#define PACKET_PAD_TYPE 0xF /* enum PadType, as it is read back */
#define PACKET_BUTTONS 0xFFFF

/* Lets one console hold the other up while it loads. */
enum LinkWaitState {
    LINK_WAIT_IDLE = 0,
    LINK_WAIT_ACTIVE = 1,
    LINK_WAIT_ENDING = 2,
};

enum LinkHandshakeStep {
    LINK_HANDSHAKE_START = 0,
    LINK_HANDSHAKE_PICK_SIDE = 1,
    LINK_HANDSHAKE_SIDE0_WAIT_CTS = 2,
    LINK_HANDSHAKE_SIDE1_WAIT_CTS_CLEAR = 3,
    LINK_HANDSHAKE_LINKED = 4,
    LINK_HANDSHAKE_DELAY_1 = 5,
    LINK_HANDSHAKE_DELAY_2 = 6,
    LINK_HANDSHAKE_DONE = 7,
};

s32 g_link_read_fd;
s32 g_link_write_fd;
s32 g_link_event_read_done;
s32 g_link_event_error;
s32 g_link_close_requested;
s32 g_link_wait_state;
u8 g_link_write_ok;

/* The packet being sent, the one sent last frame and the one received. Each
 * is two words. FAKE: the second word reached from the first, not named */
s32 g_link_send_pad_word;
s32 g_link_prev_send_pad_word;
s32 g_link_recv_packet[2];
s32 g_link_recv_pad_word;
u16 g_link_send_payload;

/* The first exchange after connecting only starts things off: one side
 * sends, both start a receive, and no pad is delivered. */
u8 g_link_first_exchange;
u8 g_link_handshake_step;
s32 g_link_handshake_retries;

/* Statistics; nothing reads them. */
s32 g_link_checksum_errors;
s32 g_link_recv_bit_count;
s32 g_link_sent_bit_count;

/* Opens the serial port: the driver's two events (a read is done, an
 * error), the driver itself, and the port once for writing and once for
 * reading without waiting; a packet goes a byte at a time. Every step is
 * tried until it works. */
static void link_Init(void) {
    EnterCriticalSection();
    do {
        g_link_event_read_done = OpenEvent(HwSIO, EvSpIOER, EvMdNOINTR, 0);
    } while (g_link_event_read_done == -1);
    do {
        g_link_event_error = OpenEvent(HwSIO, EvSpERROR, EvMdNOINTR, 0);
    } while (g_link_event_error == -1);
    ExitCriticalSection();
    VSync(2);
    AddCOMB();
    do {
        g_link_write_fd = open(g_link_device_name, O_WRONLY);
    } while (g_link_write_fd == -1);
    do {
        g_link_read_fd = open(g_link_device_name, O_NOWAIT | O_RDONLY);
    } while (g_link_read_fd == -1);
    CombReset();
    CombSetBPS(LINK_BAUD);
    CombSetPacketSize(1);
}

/* Closes the port, the events and the driver. */
static void link_Close(void) {
    close(g_link_read_fd);
    close(g_link_write_fd);
    EnterCriticalSection();
    CloseEvent(g_link_event_read_done);
    CloseEvent(g_link_event_error);
    ExitCriticalSection();
    VSync(2);
    DelCOMB();
    CombSetControl(0);
}

/* Whether the cable is out: neither of the other console's lines is up.
 * Nothing calls it. */
static s32 link_IsUnplugged(void) {
    return (CombSioStatus() & (COMB_CTS | COMB_DSR)) == 0;
}

/* Decides which console is which: the one that finds the other already
 * signalling is side 1. Then signals in turn. */
static void link_PickSide(void) {
    if (CombCTS()) {
        g_link_side = 1;
    } else {
        g_link_side = 0;
    }
    CombSetRTS(1);
}

/* The consoles are linked: from here link_ExchangePad exchanges packets. */
static void link_Start(void) {
    EnableEvent(g_link_event_error);
    EnableEvent(g_link_event_read_done);
    g_link_active = 1;
    g_link_close_requested = 0;
}

/* Leaves link play and returns to the main menu. */
static void link_Shutdown(void) {
    g_link_active = 0;
    g_link_close_requested = 0;
    CombReset();
    CombSetControl(0);
    link_Close();
    g_sys_screen = SCREEN_MENU_INIT;
}

/* The other console stopped answering. */
static void link_Abort(void) {
    link_Shutdown();
    g_link_aborted = 1;
}

/* Asks to leave link play; the other console is told in the next packet. */
void link_RequestClose(void) { g_link_close_requested = 1; }

/* Called by the driver while a write waits; returning 0 gives the write up.
 * `kind` is not used. */
static s32 link_WriteWaitCallback(s32 kind, u32 polls) {
    if (polls > WRITE_WAIT_POLLS) {
        g_link_write_ok = 0;
        return 0;
    }
    return 1;
}

/* Sends the outgoing packet once the other console is ready for it, which it
 * shows by holding CTS up. Returns 0 if that never happens or the write is
 * given up. `packet` is not used: what is sent is always the packet at
 * g_link_send_pad_word. */
static s32 link_SendPacket(void *packet) {
    s32 start;
    s32 polls;

    start = GetRCnt(RCntCNT1);
    if (start > COUNTER_RESTART) {
        ResetRCnt(RCntCNT1);
        start = 0;
    }

    while (1) {
        while (1) {
            if (CombCTS()) {
                break;
            }
            if (GetRCnt(RCntCNT1) - start > SEND_TIMEOUT) {
                return 0;
            }
        }

        polls = 0;
        do {
            if (!CombCTS()) {
                break;
            }
            polls++;
        } while (polls < CTS_STEADY_POLLS);

        if (polls >= CTS_STEADY_POLLS) {
            break;
        }
    }

    CombSetControl(COMB_BIT_DTR);
    g_link_write_ok = 1;
    CombWaitCallback((s32)&link_WriteWaitCallback);
    write(g_link_write_fd, &g_link_send_pad_word, PACKET_SIZE);
    CombWaitCallback(0);
    CombSetControl(0);
    return g_link_write_ok;
}

/* Starts an asynchronous read of the next packet. */
static void link_StartReceive(void) {
    read(g_link_read_fd, g_link_recv_packet, PACKET_SIZE);
}

/* Waits for the packet being read and checks it. Returns 0 if it does not
 * arrive, even after restarting the read, or if its checksum is wrong. */
static s32 link_ReceivePacket(void) {
    s32 start;
    s32 retries;
    s32 pad_word;
    s32 check_word;
    s32 checksum;

    retries = 0;
    start = GetRCnt(RCntCNT1);
    if (start > COUNTER_RESTART) {
        ResetRCnt(RCntCNT1);
        start = 0;
    }
    while (!TestEvent(g_link_event_read_done)) {
        if (TestEvent(g_link_event_error)) {
            /* A serial error: the port is reset and the read started again. */
            retries += 1;
            if (retries >= RECEIVE_RETRIES) {
                return 0;
            }
            CombReset();
            start = 0;
            link_StartReceive();
            ResetRCnt(RCntCNT1);
        }
        /* The clock only runs while the other console is not holding this
         * one off (DSR set, CTS clear). */
        if (((CombSioStatus() >> 7) & 3) != 1 &&
            GetRCnt(RCntCNT1) - start > RECEIVE_TIMEOUT) {
            retries += 1;
            if (retries >= RECEIVE_RETRIES) {
                return 0;
            }
            ResetRCnt(RCntCNT1);
            start = 0;
        }
    }
    pad_word = g_link_recv_packet[0];
    check_word = g_link_recv_packet[1];
    checksum = pad_word ^ (pad_word >> 16);
    checksum = checksum ^ (check_word >> 16);
    checksum = checksum & 0xFFFF;
    if ((check_word & 0xFFFF) == checksum) {
        g_link_recv_pad_word = pad_word;
        g_link_recv_check_word = check_word;
        return 1;
    }
    g_link_checksum_errors += 1;
    return 0;
}

/* How many bits of `word` are set. */
static s32 math_Popcount32(u32 word) {
    s32 count = 0;
    s32 i;
    /* every bit of the word */
    for (i = 0; i < 32; i++) {
        count += (word >> i) & 1;
    }
    return count;
}

/* One frame of link play. Builds this console's packet from `pad`, exchanges
 * packets with the other console, then rewrites pad->held and pad->type so
 * that side 0 is player 1 and side 1 is player 2 on both consoles. To keep
 * the two in step, each console acts on the pad it sent the frame before.
 * Leaves link play if the other console asks to, or stops answering. */
void link_ExchangePad(PadState *pad) {
    /* FAKE: one variable for the local buttons and, later, the outgoing ones */
    s32 buttons;
    s32 word;
    s32 check;
    s32 first;
    s32 side;
    s32 peer_buttons;
    s32 type;
    s32 zero; /* FAKE: 0 held in a variable */

    if (g_link_active) {
        buttons = pad->held;
        side = g_link_side;
        zero = 0;
        word = (side << PACKET_SIDE_SHIFT) |
               (g_link_close_requested << PACKET_CLOSE_SHIFT) |
               ((g_link_wait_state & PACKET_WAIT_MASK) << PACKET_WAIT_SHIFT) |
               (pad->type[0] << PACKET_PAD_TYPE_SHIFT) |
               (buttons & PACKET_BUTTONS);
        g_link_send_pad_word = word;
        check = g_link_send_payload << 16;
        word = word ^ (word >> 16);
        word = word ^ (check >> 16);
        word = word & 0xFFFF;
        check = check | word;
        g_link_send_check_word = check;
        first = g_link_first_exchange;

        if (first) {
            if (side == 0) {
                link_StartReceive();
            } else {
                if (!link_SendPacket(&g_link_send_pad_word)) {
                    link_Abort();
                    return;
                }
                g_link_sent_bit_count +=
                    math_Popcount32(buttons & PACKET_BUTTONS);
                link_StartReceive();
            }
        } else {
            if (!link_ReceivePacket()) {
                link_Abort();
                return;
            }
            if (g_link_side == 1) {
                if (g_link_recv_pad_word & PACKET_CLOSE) {
                    link_Shutdown();
                    return;
                }
                if (g_link_prev_send_pad_word & PACKET_CLOSE) {
                    link_Shutdown();
                    return;
                }
            }
            if (!link_SendPacket(&g_link_send_pad_word)) {
                link_Abort();
                return;
            }
            g_link_sent_bit_count += math_Popcount32(buttons & PACKET_BUTTONS);
            link_StartReceive();
            if (g_link_side == 0) {
                if (g_link_close_requested != zero ||
                    (g_link_recv_pad_word & PACKET_CLOSE)) {
                    link_Shutdown();
                    return;
                }
            }
        }

        if (!g_link_first_exchange) {
            g_link_recv_bit_count += math_Popcount32((u16)g_link_recv_pad_word);
            peer_buttons = (u16)g_link_recv_pad_word;
            /* FAKE: type = word >> 16, read through a pointer instead */
            if (g_link_side == 0) {
                buttons = (u16)g_link_send_pad_word;
                pad->held = (peer_buttons << 16) | buttons;
                type = ((s16 *)&g_link_recv_pad_word)[1];
                pad->type[1] = type & PACKET_PAD_TYPE;
            } else {
                buttons = (u16)g_link_prev_send_pad_word;
                pad->held = (buttons << 16) | peer_buttons;
                type = ((s16 *)&g_link_recv_pad_word)[1];
                pad->type[0] = type & PACKET_PAD_TYPE;
                type = ((s16 *)&g_link_prev_send_pad_word)[1];
                pad->type[1] = type & PACKET_PAD_TYPE;
            }
            /* A wait is over once both consoles have said so. */
            if (g_link_side == 0) {
                if (((g_link_recv_pad_word >> PACKET_WAIT_SHIFT) &
                     PACKET_WAIT_MASK) == LINK_WAIT_ENDING &&
                    g_link_wait_state == LINK_WAIT_ENDING) {
                    g_link_wait_state = LINK_WAIT_IDLE;
                }
            } else {
                if (((g_link_recv_pad_word >> PACKET_WAIT_SHIFT) &
                     PACKET_WAIT_MASK) == LINK_WAIT_ENDING &&
                    ((g_link_prev_send_pad_word >> PACKET_WAIT_SHIFT) &
                     PACKET_WAIT_MASK) == LINK_WAIT_ENDING) {
                    g_link_wait_state = LINK_WAIT_IDLE;
                }
            }
        }
        g_link_first_exchange = 0;
        g_link_prev_send_pad_word = g_link_send_pad_word;
        g_link_prev_send_check_word = g_link_send_check_word;
    } else {
        g_link_wait_state = LINK_WAIT_IDLE;
    }
}

/* An exchange with no buttons held, for when no pad is being read. */
void link_ExchangeIdle(void) {
    PadState pad;
    pad.held = 0;
    pad.type[1] = PAD_TYPE_DIGITAL;
    pad.type[0] = PAD_TYPE_DIGITAL;
    link_ExchangePad(&pad);
}

/* This console is about to be busy: tells the other one to wait. */
void link_BeginWait(void) {
    g_link_wait_state = LINK_WAIT_ACTIVE;
    VSync(2);
    link_ExchangeIdle();
    VSync(2);
}

/* Keeps exchanging until the other console has seen that the wait is over. */
void link_EndWait(void) {
    g_link_wait_state = LINK_WAIT_ENDING;
    VSync(2);
    do {
        link_ExchangeIdle();
        if (!g_link_active) {
            break;
        }
        ResetRCnt(RCntCNT1);
        do {
        } while (GetRCnt(RCntCNT1) < END_WAIT_GAP);
    } while (g_link_wait_state == LINK_WAIT_ENDING);
    VSync(2);
    link_ExchangeIdle();
    VSync(2);
}

/* One step of connecting, called every frame by link_Connect. Returns 1 when
 * linked, -1 when given up, 0 to keep going. */
static s32 link_HandshakeStep(void) {
    g_sys_screen_frames++;
    switch (g_link_handshake_step) {
    case LINK_HANDSHAKE_START:
        g_link_handshake_step = LINK_HANDSHAKE_PICK_SIDE;
        break;
    case LINK_HANDSHAKE_PICK_SIDE:
        link_PickSide();
        g_link_handshake_retries = 0;
        if (g_link_side == 0) {
            /* First to arrive: the waiting screen is shown. */
            SetDispMask(1);
            g_link_handshake_step = LINK_HANDSHAKE_SIDE0_WAIT_CTS;
        } else {
            g_link_handshake_step = LINK_HANDSHAKE_SIDE1_WAIT_CTS_CLEAR;
        }
        break;
    case LINK_HANDSHAKE_SIDE0_WAIT_CTS:
        /* Waiting for the other console, until triangle cancels. */
        if (g_pad_state.pressed & PADRup) {
            link_Shutdown();
            return -1;
        }
        if (CombCTS()) {
            CombSetRTS(0);
            g_link_handshake_step = LINK_HANDSHAKE_LINKED;
        }
        break;
    case LINK_HANDSHAKE_SIDE1_WAIT_CTS_CLEAR:
        /* The other console should answer within a few frames. */
        if (!CombCTS()) {
            CombSetRTS(0);
            g_link_handshake_step = LINK_HANDSHAKE_LINKED;
            break;
        }
        g_link_handshake_retries++;
        if (g_link_handshake_retries < 4) {
            break;
        }
        link_Shutdown();
        return -1;
    case LINK_HANDSHAKE_LINKED:
        SetDispMask(0);
        /* fall through */
    case LINK_HANDSHAKE_DELAY_1:
    case LINK_HANDSHAKE_DELAY_2:
        g_link_handshake_step++;
        break;
    case LINK_HANDSHAKE_DONE:
        g_link_first_exchange = 1;
        link_Start();
        return 1;
    }
    return 0;
}

/* Connects to the other console, then trades what both sides must agree on:
 * unlocks, button configuration and the random seed (side 0's is used).
 * Returns 1 when linked, -1 when given up. Given up, it still goes through
 * the three trades: with the link shut the exchanges do nothing, and the
 * other side's unlocks, its buttons and the seed are taken from whatever
 * was received last (the original's behaviour). */
s32 link_Connect(void) {
    s32 result;

    menu_ShowLinkPicture();
    SetDispMask(0);
    g_sys_screen_frames = 0;
    g_link_handshake_step = LINK_HANDSHAKE_START;
    g_link_checksum_errors = 0;
    g_link_sent_bit_count = 0;
    g_link_recv_bit_count = 0;
    link_Init();
    do {
        pad_Update(1);
        snd_FlushQueue();
        result = link_HandshakeStep();
        VSync(2);
    } while (result == 0);
    menu_HideLinkPicture();
    pad_ResetState();

    g_link_send_payload = link_PackUnlockMask(g_saved_record.unlock_mask);
    link_ExchangeIdle();
    VSync(1);
    link_ExchangeIdle();
    g_link_peer_unlock_mask =
        link_UnpackUnlockMask((g_link_recv_check_word >> 16));

    g_link_send_payload = pad_PackButtonConfig();
    VSync(1);
    link_ExchangeIdle();
    VSync(1);
    link_ExchangeIdle();
    VSync(1);
    link_ExchangeIdle();
    pad_SetRemoteButtonConfig((g_link_recv_check_word >> 16));

    g_link_send_payload = rand();
    VSync(1);
    link_ExchangeIdle();
    VSync(1);
    link_ExchangeIdle();
    VSync(1);
    link_ExchangeIdle();
    {
        s32 seed;
        if (g_link_side == 0) {
            seed = g_link_send_payload;
        } else {
            seed = g_link_recv_check_word >> 16;
        }
        g_fight_rng_seed = seed;
    }
    disp_Blank();
    disp_Show();
    ResetRCnt(RCntCNT1);
    return result;
}

/* Small data, in address order. */
/* Set while linked; link_ExchangePad does nothing otherwise. */
static u8 g_link_active = 0;
static char g_link_device_name[5] = "sio:";
