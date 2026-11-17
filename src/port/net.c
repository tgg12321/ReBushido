/* The network in the native port (port_platform.h, "the network";
 * docs/NETPLAY.md, sections 11 and 12): one UDP socket to one other
 * program, the same build of the port on another PC or beside this one.
 *
 * Two things go over it. The link cable (--link-side): the game's own
 * versus mode for two consoles (src/main/link.c) exchanges an 8-byte
 * packet a frame, and here each is a datagram, numbered so that one that
 * is lost is sent again. And rollback netplay (--netplay, netplay.c),
 * whose datagrams this file only carries.
 *
 * A run can make the network worse on purpose, for testing: every
 * datagram it sends waits --net-latency milliseconds before it goes, and
 * --net-loss of every hundred are dropped, by a sequence of random
 * numbers of its own, so that a test is the same each time. */
#include "common.h"
#include "port_host.h"
#include "port_net.h"
#include "bb2.h"

/* A datagram of the port's: a mark, its kind and a number, and then what
 * it carries. */
#define NET_MARK_0 'R'
#define NET_MARK_1 'B'
#define NET_HEADER_BYTES PORT_NET_HEADER_BYTES
#define NET_DATAGRAM_MAX PORT_NET_DATAGRAM_MAX
/* Datagrams held back by --net-latency, at most. */
#define NET_DELAYED_MAX 512
#define NET_LOSS_RANGE 100
#define NET_TICKS_PER_MS (PORT_HOST_TICKS_PER_MICROSECOND * 1000)
/* An address as text: four numbers to 255, a colon and a port to 65535. */
#define ADDRESS_PARTS 4
#define ADDRESS_PART_MAX 0xFF
#define PORT_NUMBER_MAX 0xFFFF
#define PORT_DIGITS_MAX 5
#define DECIMAL 10
#define BITS_PER_BYTE 8
#define WORD_BYTES 4

/* The link cable's packet and how long it is waited for. */
#define LINK_PACKET_BYTES 8
/* A wait sends its own last datagram again this often, and gives up after
 * this long: the other console has gone. */
#define LINK_RESEND_MS 50
#define LINK_GIVE_UP_MS 15000
#define LINK_MEET_GIVE_UP_MS 60000
/* A poll of the socket while waiting. */
#define NET_POLL_MS 5
/* How long a program that ends still answers the other, at most, and
 * once the other has said nothing for how long. */
#define LINGER_MS 3000
#define LINGER_QUIET_MS 500

enum {
    NET_KIND_HELLO = 1, /* the link: here I am; byte 3 says I have heard you */
    NET_KIND_LINK = 2,  /* the link: a packet of the game's */
    NET_KIND_FIRST_NETPLAY = PORT_NET_KIND_NETPLAY /* and up: netplay.c's */
};

/* The header: the mark, the kind, a byte of the kind's own, and the
 * number. */
enum {
    HEADER_MARK = 0,
    HEADER_KIND = 2,
    HEADER_FLAG = 3,
    HEADER_NUMBER = 4,
};

#define HELLO_HEARD 1
#define HELLO_REPLY 2

typedef struct {
    u32 due;
    u32 bytes;
    u8 data[NET_DATAGRAM_MAX];
} PortNetDelayed;

static s32 g_port_net_socket = -1;
static PortNetAddress g_port_net_bind;
static PortNetAddress g_port_net_peer;
static s32 g_port_net_wanted;
/* --host: the peer is whoever is heard first, until it is fixed; until
 * then what is sent goes to the last program heard. */
static s32 g_port_net_has_peer;
static s32 g_port_net_is_fixed;
static PortNetAddress g_port_net_heard;
static s32 g_port_net_has_heard;
/* The clock in milliseconds, and the ticks it was last read at. */
static u32 g_port_net_clock_ms;
static u32 g_port_net_clock_ticks;
static u32 g_port_net_clock_rest;
static s32 g_port_net_clock_started;
static u32 g_port_net_latency_ms;
static u32 g_port_net_loss;
static u32 g_port_net_random = 0x2545F491U;
/* (memory the system gives, for a run with --net-latency alone: it is
 * large, and the program's variables have a place of fixed size) */
static PortNetDelayed *g_port_net_delayed;
static u32 g_port_net_delayed_count;
/* What went and what was dropped, for the report. */
static u32 g_port_net_sent;
static u32 g_port_net_dropped;
static u32 g_port_net_received;

/* The link cable: which side this console is, -1 if no cable. */
static s32 g_port_link_side = -1;
static s32 g_port_link_met;
static s32 g_port_link_heard;
static s32 g_port_link_peer_heard;
static u32 g_port_link_out_number;
static u32 g_port_link_in_number;
static u8 g_port_link_last[NET_HEADER_BYTES + LINK_PACKET_BYTES];
static s32 g_port_link_has_last;
static u32 g_port_link_resent;

/* ---- the socket ---- */

/* `ADDRESS:PORT`, four numbers and a port: 1, or 0 if it is not one
 * (port_net.h). */
s32 port_ParseNetAddress(const char *text, PortNetAddress *out) {
    u32 parts[ADDRESS_PARTS + 1];
    s32 count = 0;
    u32 value = 0;
    s32 digits = 0;
    s32 i;

    for (;; text++) {
        if (*text >= '0' && *text <= '9') {
            value = value * DECIMAL + (u32)(*text - '0');
            if (++digits > PORT_DIGITS_MAX) {
                return 0;
            }
            continue;
        }
        if (digits == 0 || count > ADDRESS_PARTS) {
            return 0;
        }
        parts[count++] = value;
        value = 0;
        digits = 0;
        if (*text == 0) {
            break;
        }
        if (*text != (count < ADDRESS_PARTS ? '.' : ':') ||
            count > ADDRESS_PARTS) {
            return 0;
        }
    }
    if (count != ADDRESS_PARTS + 1 || parts[ADDRESS_PARTS] > PORT_NUMBER_MAX) {
        return 0;
    }
    out->address = 0;
    for (i = 0; i < ADDRESS_PARTS; i++) {
        if (parts[i] > ADDRESS_PART_MAX) {
            return 0;
        }
        out->address = (out->address << BITS_PER_BYTE) | parts[i];
    }
    out->port = (u16)parts[ADDRESS_PARTS];
    return 1;
}

/* Where the socket goes and whom it talks to (port_net.h). */
void port_SetNetAddresses(
    const PortNetAddress *bind, const PortNetAddress *peer) {
    g_port_net_bind = *bind;
    g_port_net_peer = *peer;
    g_port_net_wanted = 1;
    g_port_net_has_peer = 1;
    g_port_net_is_fixed = 1;
}

void port_SetNetListen(const PortNetAddress *bind) {
    g_port_net_bind = *bind;
    g_port_net_wanted = 1;
    g_port_net_has_peer = 0;
    g_port_net_is_fixed = 0;
}

s32 port_GetNetPeer(PortNetAddress *peer) {
    if (g_port_net_has_peer) {
        *peer = g_port_net_peer;
        return 1;
    }
    if (g_port_net_has_heard) {
        *peer = g_port_net_heard;
        return 1;
    }
    return 0;
}

void port_FixNetPeer(void) {
    if (!g_port_net_has_peer && g_port_net_has_heard) {
        g_port_net_peer = g_port_net_heard;
        g_port_net_has_peer = 1;
    }
    g_port_net_is_fixed = g_port_net_has_peer;
}

/* The PC's clock in milliseconds (port_net.h): the host's ticks wrap
 * after minutes, so they are added up as they pass. */
u32 port_NetClock(void) {
    u32 now = port_HostTicks();
    u32 ticks;

    if (!g_port_net_clock_started) {
        g_port_net_clock_started = 1;
        g_port_net_clock_ticks = now;
    }
    ticks = now - g_port_net_clock_ticks + g_port_net_clock_rest;
    g_port_net_clock_ticks = now;
    g_port_net_clock_ms += ticks / NET_TICKS_PER_MS;
    g_port_net_clock_rest = ticks % NET_TICKS_PER_MS;
    return g_port_net_clock_ms;
}

void port_NetPutWord(u8 *out, u32 value) {
    s32 i;

    for (i = 0; i < WORD_BYTES; i++) {
        out[i] = (u8)(value >> (i * BITS_PER_BYTE));
    }
}

u32 port_NetGetWord(const u8 *in) {
    u32 value = 0;
    s32 i;

    for (i = 0; i < WORD_BYTES; i++) {
        value |= (u32)in[i] << (i * BITS_PER_BYTE);
    }
    return value;
}

void port_SetNetTrouble(u32 latency_ms, u32 loss) {
    g_port_net_latency_ms = latency_ms;
    g_port_net_loss = loss > NET_LOSS_RANGE ? NET_LOSS_RANGE : loss;
}

/* The socket, opened as the run starts. */
s32 port_StartNet(void) {
    if (!g_port_net_wanted) {
        return 1;
    }
    if (g_port_net_latency_ms != 0) {
        g_port_net_delayed =
            port_HostAllocate(NET_DELAYED_MAX * (u32)sizeof(PortNetDelayed));
        if (g_port_net_delayed == NULL) {
            port_Log("rebushido: the network: no memory for --net-latency\n");
            return 0;
        }
    }
    g_port_net_socket = port_HostUdpOpen(&g_port_net_bind);
    if (g_port_net_socket < 0) {
        port_Log("rebushido: the network: no socket at %u.%u.%u.%u:%u\n",
                 g_port_net_bind.address >> 24,
                 (g_port_net_bind.address >> 16) & 0xFF,
                 (g_port_net_bind.address >> 8) & 0xFF,
                 g_port_net_bind.address & 0xFF, g_port_net_bind.port);
        return 0;
    }
    return 1;
}

s32 port_IsNetOn(void) { return g_port_net_socket >= 0; }

/* The next of the run's own random numbers, for --net-loss. */
static u32 port_NetRandom(void) {
    u32 x = g_port_net_random;

    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    g_port_net_random = x;
    return x;
}

/* Where a datagram goes: the peer, or before there is one (--host) the
 * program heard last; NULL if there is nobody. */
static const PortNetAddress *port_NetTo(void) {
    if (g_port_net_has_peer) {
        return &g_port_net_peer;
    }
    return g_port_net_has_heard ? &g_port_net_heard : NULL;
}

/* Sends what --net-latency has held back long enough. */
static void port_NetFlush(void) {
    u32 now = port_HostTicks();
    u32 kept = 0;
    u32 i;

    for (i = 0; i < g_port_net_delayed_count; i++) {
        PortNetDelayed *delayed = &g_port_net_delayed[i];

        if ((s32)(now - delayed->due) >= 0) {
            if (port_NetTo() != NULL) {
                port_HostUdpSend(g_port_net_socket, port_NetTo(),
                                 delayed->data, delayed->bytes);
            }
        } else {
            if (kept != i) {
                g_port_net_delayed[kept] = *delayed;
            }
            kept++;
        }
    }
    g_port_net_delayed_count = kept;
}

/* A datagram to the other program (port_net.h): dropped, held back or
 * sent now. */
void port_NetSend(const void *bytes, u32 count) {
    if (g_port_net_socket < 0 || count > NET_DATAGRAM_MAX ||
        port_NetTo() == NULL) {
        return;
    }
    g_port_net_sent++;
    if (g_port_net_loss != 0 &&
        port_NetRandom() % NET_LOSS_RANGE < g_port_net_loss) {
        g_port_net_dropped++;
        return;
    }
    if (g_port_net_latency_ms == 0) {
        port_HostUdpSend(g_port_net_socket, port_NetTo(), bytes, count);
        return;
    }
    port_NetFlush();
    if (g_port_net_delayed_count < NET_DELAYED_MAX) {
        PortNetDelayed *delayed =
            &g_port_net_delayed[g_port_net_delayed_count++];
        const u8 *from = bytes;
        u32 i;

        delayed->due =
            port_HostTicks() + g_port_net_latency_ms * NET_TICKS_PER_MS;
        delayed->bytes = count;
        for (i = 0; i < count; i++) {
            delayed->data[i] = from[i];
        }
    }
}

/* The other program's next datagram (port_net.h); one from anybody else
 * is not looked at. Before a host has fixed its peer anybody's is, and
 * the sender is the one answered. */
s32 port_NetReceive(void *bytes, u32 room, u32 wait_ms) {
    PortNetAddress from;
    s32 got;

    if (g_port_net_socket < 0) {
        return -1;
    }
    for (;;) {
        u32 wait = wait_ms;

        port_NetFlush();
        /* what is held back must go on time while this waits */
        if (g_port_net_delayed_count != 0 && wait > NET_POLL_MS) {
            wait = NET_POLL_MS;
        }
        got = port_HostUdpReceive(g_port_net_socket, bytes, room, &from, wait);
        if (got >= 0) {
            /* only the peer's own, once it is fixed */
            if (g_port_net_is_fixed &&
                (from.address != g_port_net_peer.address ||
                 from.port != g_port_net_peer.port)) {
                continue;
            }
            if (!g_port_net_is_fixed) {
                g_port_net_heard = from;
                g_port_net_has_heard = 1;
            }
            g_port_net_received++;
            return got;
        }
        if (wait >= wait_ms) {
            return -1;
        }
        wait_ms -= wait;
    }
}

/* A datagram's header. */
void port_NetHeader(u8 *out, s32 kind, s32 detail, u32 number) {
    out[HEADER_MARK] = NET_MARK_0;
    out[HEADER_MARK + 1] = NET_MARK_1;
    out[HEADER_KIND] = (u8)kind;
    out[HEADER_FLAG] = (u8)detail;
    out[HEADER_NUMBER] = (u8)number;
    out[HEADER_NUMBER + 1] = (u8)(number >> 8);
    out[HEADER_NUMBER + 2] = (u8)(number >> 16);
    out[HEADER_NUMBER + 3] = (u8)(number >> 24);
}

/* A datagram's kind, its detail and its number: the kind is 0 if it is
 * not the port's. */
s32 port_NetReadHeader(const u8 *in, s32 bytes, s32 *detail, u32 *number) {
    if (bytes < NET_HEADER_BYTES || in[HEADER_MARK] != NET_MARK_0 ||
        in[HEADER_MARK + 1] != NET_MARK_1) {
        return 0;
    }
    *detail = in[HEADER_FLAG];
    *number =
        (u32)in[HEADER_NUMBER] | ((u32)in[HEADER_NUMBER + 1] << 8) |
        ((u32)in[HEADER_NUMBER + 2] << 16) | ((u32)in[HEADER_NUMBER + 3] << 24);
    return in[HEADER_KIND];
}

static s32 port_LinkTake(const u8 *in, s32 bytes, u8 *packet);

/* The run ends: for a while, until the other program has said nothing
 * for LINGER_QUIET_MS, what it asks for again is sent again, so that
 * the last packets it is waiting for are not lost with this program. */
static void port_NetLinger(void) {
    u8 in[NET_DATAGRAM_MAX];
    u32 start = port_HostTicks();
    u32 heard = start;

    for (;;) {
        u32 now = port_HostTicks();
        s32 got;

        if (now - start > LINGER_MS * NET_TICKS_PER_MS ||
            now - heard > LINGER_QUIET_MS * NET_TICKS_PER_MS) {
            break;
        }
        got = port_NetReceive(in, sizeof(in), NET_POLL_MS);
        if (got > 0) {
            heard = port_HostTicks();
            if (g_port_link_side >= 0) {
                port_LinkTake(in, got, NULL);
            }
        }
    }
}

/* What went and what came (port_net.h), once the other program has had
 * what it still waits for. */
void port_PrintNetReport(void) {
    if (g_port_net_socket < 0) {
        return;
    }
    port_NetLinger();
    port_Log("net: %u datagrams sent, %u of them dropped on purpose, %u "
             "received; latency %u ms, loss %u%%\n",
             g_port_net_sent, g_port_net_dropped, g_port_net_received,
             g_port_net_latency_ms, g_port_net_loss);
    if (g_port_link_side >= 0) {
        port_Log("link: side %d, %u packets sent, %u received, %u sent "
                 "again\n",
                 g_port_link_side, g_port_link_out_number,
                 g_port_link_in_number, g_port_link_resent);
    }
}

/* ---- the link cable ---- */

void port_SetLinkSide(s32 side) { g_port_link_side = side; }

s32 port_LinkIsUp(void) {
    return g_port_link_side >= 0 && g_port_net_socket >= 0;
}

s32 port_LinkSide(void) { return g_port_link_side; }

static void port_LinkSendHello(s32 flags) {
    u8 hello[NET_HEADER_BYTES];

    port_NetHeader(hello, NET_KIND_HELLO, flags, 0);
    port_NetSend(hello, sizeof(hello));
}

/* What came while the link waits: a hello is answered, and a packet of
 * the game's is kept if it is the next. Returns 1 for the next packet. */
static s32 port_LinkTake(const u8 *in, s32 bytes, u8 *packet) {
    s32 detail;
    u32 number;
    s32 kind = port_NetReadHeader(in, bytes, &detail, &number);
    s32 i;

    if (kind == NET_KIND_HELLO) {
        g_port_link_heard = 1;
        if (detail & HELLO_HEARD) {
            g_port_link_peer_heard = 1;
        }
        if (!(detail & HELLO_REPLY)) {
            port_LinkSendHello(HELLO_HEARD | HELLO_REPLY);
        }
        return 0;
    }
    if (kind != NET_KIND_LINK ||
        bytes != NET_HEADER_BYTES + LINK_PACKET_BYTES) {
        return 0;
    }
    /* a packet means the other console has met this one */
    g_port_link_heard = 1;
    g_port_link_peer_heard = 1;
    if (number != g_port_link_in_number) {
        /* one it had: what this console sent last was lost on the way */
        if (number < g_port_link_in_number && g_port_link_has_last) {
            port_NetSend(g_port_link_last, sizeof(g_port_link_last));
            g_port_link_resent++;
        }
        return 0;
    }
    g_port_link_in_number++;
    if (packet != NULL) {
        for (i = 0; i < LINK_PACKET_BYTES; i++) {
            packet[i] = in[NET_HEADER_BYTES + i];
        }
    }
    return 1;
}

/* The two consoles meet as they connect (link.c, link_HandshakeStep):
 * each says it is here until it has heard the other and the other has
 * heard it. It waits for that, in the PC's time and none of the game's,
 * so the two take the same frames to connect. 1, or 0 if the other never
 * came. */
s32 port_LinkMeet(void) {
    u8 in[NET_DATAGRAM_MAX];
    u32 start = port_HostTicks();
    u32 last = 0;

    if (g_port_link_met) {
        return 1;
    }
    g_port_link_out_number = 0;
    g_port_link_in_number = 0;
    g_port_link_has_last = 0;
    while (!(g_port_link_heard && g_port_link_peer_heard)) {
        u32 now = port_HostTicks();
        s32 got;

        if (now - start > LINK_MEET_GIVE_UP_MS * NET_TICKS_PER_MS) {
            return 0;
        }
        if (last == 0 || now - last > LINK_RESEND_MS * NET_TICKS_PER_MS) {
            port_LinkSendHello(g_port_link_heard ? HELLO_HEARD : 0);
            last = now;
        }
        got = port_NetReceive(in, sizeof(in), NET_POLL_MS);
        if (got > 0) {
            port_LinkTake(in, got, NULL);
        }
    }
    /* one more, so that the other has heard this one too */
    port_LinkSendHello(HELLO_HEARD);
    g_port_link_met = 1;
    return 1;
}

/* The game's packet to the other console: its two words. */
s32 port_LinkSend(s32 first, s32 second) {
    u8 *out = g_port_link_last;
    u32 words[2];
    s32 i;

    words[0] = (u32)first;
    words[1] = (u32)second;
    port_NetHeader(out, NET_KIND_LINK, 0, g_port_link_out_number++);
    for (i = 0; i < LINK_PACKET_BYTES; i++) {
        out[NET_HEADER_BYTES + i] =
            (u8)(words[i / WORD_BYTES] >> ((i % WORD_BYTES) * BITS_PER_BYTE));
    }
    g_port_link_has_last = 1;
    port_NetSend(out, sizeof(g_port_link_last));
    return 1;
}

/* The other console's next packet into `words`: 1, or 0 if it has
 * gone. While it waits, what this one sent last goes again now and then,
 * in case it was lost. */
s32 port_LinkReceive(s32 *words) {
    u8 in[NET_DATAGRAM_MAX];
    u8 packet[LINK_PACKET_BYTES];
    u32 start = port_HostTicks();
    u32 last = start;
    s32 i;

    for (;;) {
        u32 now = port_HostTicks();
        s32 got;

        if (now - start > LINK_GIVE_UP_MS * NET_TICKS_PER_MS) {
            return 0;
        }
        if (now - last > LINK_RESEND_MS * NET_TICKS_PER_MS) {
            if (g_port_link_has_last) {
                port_NetSend(g_port_link_last, sizeof(g_port_link_last));
                g_port_link_resent++;
            } else {
                port_LinkSendHello(HELLO_HEARD);
            }
            last = now;
        }
        got = port_NetReceive(in, sizeof(in), NET_POLL_MS);
        if (got > 0 && port_LinkTake(in, got, packet)) {
            break;
        }
    }
    for (i = 0; i < 2; i++) {
        words[i] = (s32)((u32)packet[i * 4] | ((u32)packet[i * 4 + 1] << 8) |
                         ((u32)packet[i * 4 + 2] << 16) |
                         ((u32)packet[i * 4 + 3] << 24));
    }
    return 1;
}
