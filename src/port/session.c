/* A session of netplay between two players (port_net.h; docs/NETPLAY.md,
 * section 13): what happens before the game starts, and the few presses
 * that are nobody's once it has.
 *
 * The host waits at an address and a port (--host); the guest goes there
 * (--join). Before either starts the game they meet and hold each other
 * to the same thing: the same protocol, the same build of the port (the
 * game's state is only comparable between programs whose variables lie
 * alike: NETPLAY.md, section 7), the same disc and the same mods that
 * change how the game plays (port_WeaponsSessionHash), the arenas the
 * stage select offers among them (port_ArenaSessionHash). The host then
 * sends
 * the guest its two memory cards, whole, and a random seed. Both play on
 * the host's cards, kept in memory (port_card.h): the unlocks, the button
 * layouts, the records and ReBushido's own settings, Bugfixes among them,
 * are the host's on both machines, and neither writes the session's saves
 * to a file. The kind of stage the stages start as (Classic or Remastered
 * Classic: port_stage_choice.h) is the host's too, and held for the
 * session. Then both start the game from power on, the host the first
 * pad and the guest the second.
 *
 * From power on to the versus select screen the session presses for
 * both, the same buttons on both machines at the same frames: the options
 * page, the opening (whose movie is skipped: pad.c), the memory card's
 * questions and the top menu, down to versus. The select screen is the
 * match's set-up, both players choosing at once as on a couch, and after
 * a fight the game's own menu offers the rematch; a way back to the top
 * menu comes straight back to versus. In a fight Start is nobody's: the
 * pause menu would stop the other player's game too. */
#include "common.h"
#include "port_card.h"
#include "port_disc.h"
#include "port_host.h"
#include "port_net.h"
#include "bb2.h"

/* The protocol's own number: two programs that differ in it do not meet. */
#define SESSION_PROTOCOL 3
/* A part of a memory card a datagram carries, and so many to a card. */
#define CHUNK_BYTES 1024
#define CHUNKS_PER_CARD (PORT_CARD_IMAGE_SIZE / CHUNK_BYTES)
#define CHUNKS (PORT_CARD_SLOTS * CHUNKS_PER_CARD)
#define CHUNK_MAP_BYTES (CHUNKS / 8)
/* The version's text a datagram carries. */
#define VERSION_BYTES 64
/* How often things are said again until they are heard. */
#define HELLO_EVERY_MS 100
#define ACK_EVERY_MS 20
#define CHUNKS_EVERY_MS 10
#define CHUNKS_AT_ONCE 8
#define GOES 4
#define POLL_MS 2
#define STATUS_EVERY_MS 16
/* The guest gives up when nobody has answered for this long; the host
 * waits as long as it is left to (closing the window ends it), or, with
 * no window, this long. */
#define JOIN_GIVE_UP_MS 30000
#define HOST_GIVE_UP_MS 600000
/* The disc is told from another by a sum of its sectors: every this
 * many from the first, and the volume's own description. */
#define DISC_SAMPLE_EVERY 997
#define DISC_VOLUME_SECTOR 16
#define FNV_OFFSET 0xCBF29CE484222325ULL
#define FNV_PRIME 0x100000001B3ULL
#define BITS_PER_BYTE 8
#define WORD_BYTES 4
#define WORD_BITS 32
#define STATUS_BYTES 160
#define SEED_MASK 0xFFFF
#define PERCENT 100
#define SHAPE_SHIFT 16
#define SHAPE_MASK 0xFFFF

/* The session's presses (port_SessionInput): one every this many frames;
 * on the top menu, whose cursor glides from row to row over several
 * frames, one when nothing has been held for this many. */
#define PILOT_EVERY 8
#define PILOT_TOP_SETTLE 30
/* The main menu's pages (src/main/menu_screen.c, enum MenuPage), which no
 * header has: the two select screens are the players'. */
#define MENU_PAGE_TOP 1
#define MENU_PAGE_SELECT 3
#define MENU_PAGE_TEAM_SELECT 10

enum {
    KIND_HELLO = PORT_NET_KIND_SESSION,
    KIND_WELCOME,
    KIND_REFUSE,
    KIND_CARD,
    KIND_CARD_ACK,
    KIND_READY,
    KIND_GO
};

/* Why a host turned a guest away (KIND_REFUSE's detail). */
enum {
    REFUSE_PROTOCOL = 1,
    REFUSE_BUILD = 2,
    REFUSE_DISC = 3,
    REFUSE_MODS = 4,
};

/* A hello and a welcome after the header (whose number is the
 * protocol): the build's identity, the disc's, the gameplay mods', then
 * the welcome's delay,
 * seed and cards, the shape of the sender's remastered picture (its width
 * in the high half and its height in the low), the welcome's kind of
 * stage the stages start as (port_stage_choice.h), and last the
 * version's text. */
enum {
    MEET_BUILD = PORT_NET_HEADER_BYTES,
    MEET_DISC = MEET_BUILD + 2 * WORD_BYTES,
    MEET_MODS = MEET_DISC + 2 * WORD_BYTES,
    MEET_DELAY = MEET_MODS + 2 * WORD_BYTES,
    MEET_SEED = MEET_DELAY + WORD_BYTES,
    MEET_CARDS = MEET_SEED + WORD_BYTES,
    MEET_SHAPE = MEET_CARDS + WORD_BYTES,
    MEET_KIND = MEET_SHAPE + WORD_BYTES,
    MEET_VERSION = MEET_KIND + WORD_BYTES,
    MEET_BYTES = MEET_VERSION + VERSION_BYTES
};

static struct {
    s32 role;
    PortNetAddress address;
    u64 build;
    u64 disc;
    u64 mods; /* the mods that change how the game plays */
    u32 delay;
    u32 seed;
    u32 cards;  /* a bit a slot that has a card */
    u32 shape;  /* this program's picture's shape, as MEET_SHAPE has it */
    u32 kind;   /* the host's kind of stage the stages start as */
    u32 wider;  /* the wider of the two players' */
    u32 have;   /* chunks the guest has */
    u32 needed; /* chunks there are to have */
    u8 map[CHUNK_MAP_BYTES];
    u32 next_chunk; /* the host's turn round them */
    u32 status_at;
} g_session;

/* The host's two cards, and what the guest is given of them: memory the
 * system gives as the session begins (none of the state's), copied into
 * the cards' own as the game starts. */
static u8 (*g_session_cards)[PORT_CARD_IMAGE_SIZE];

void port_SetSession(s32 role, const PortNetAddress *address) {
    g_session.role = role;
    g_session.address = *address;
}

s32 port_GetSessionRole(void) { return g_session.role; }

/* ---- who is who ---- */

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

    port_NetPutWord(bytes, word);
    return port_SumBytes(sum, bytes, sizeof(bytes));
}

/* The build: the game's version (with its commit), the size of a
 * pointer, the system, and where the program's variables lie and how
 * large its state is. Two programs that agree on these lay the game's
 * memory out alike, which is what makes their states' hashes comparable. */
static u64 port_SessionBuild(void) {
    u64 sum = FNV_OFFSET;

    sum = port_SumBytes(sum, g_rb_version, port_TextLength(g_rb_version));
    sum = port_SumWord(sum, (u32)sizeof(void *));
#if defined(_WIN32)
    sum = port_SumWord(sum, 1);
#else
    sum = port_SumWord(sum, 2);
#endif
    sum = port_SumWord(sum, port_StateSize());
    sum = port_SumWord(sum, (u32)(g_port_data_end - g_port_data_start));
    sum = port_SumWord(sum, (u32)(PortAddress)g_port_data_start);
    return port_SumWord(sum, (u32)(PortAddress)port_Main);
}

/* The disc: its size, its volume's description and a sector in every
 * DISC_SAMPLE_EVERY, read whole. Two images of the same disc agree; a
 * disc of another region, a hacked one or another game does not. */
static u64 port_SessionDisc(const PortDisc *disc) {
    static u8 raw[PORT_DISC_SECTOR_BYTES];
    u32 reads = g_port_disc_sectors_read;
    u64 sum = FNV_OFFSET;
    u32 sector;

    sum = port_SumWord(sum, disc->sector_count);
    if (port_ReadDiscSector(disc, DISC_VOLUME_SECTOR, raw) == PORT_DISC_OK) {
        sum = port_SumBytes(sum, raw, sizeof(raw));
    }
    for (sector = 0; sector < disc->sector_count; sector += DISC_SAMPLE_EVERY) {
        if (port_ReadDiscSector(disc, sector, raw) == PORT_DISC_OK) {
            sum = port_SumBytes(sum, raw, sizeof(raw));
        }
    }
    /* what was read here is none of the game's */
    g_port_disc_sectors_read = reads;
    return sum;
}

/* Why a host turned a guest away, in words for the log and the window. */
static const char *port_RefusalText(s32 reason) {
    switch (reason) {
    case REFUSE_PROTOCOL:
        return "the other program speaks another version of the session's "
               "protocol";
    case REFUSE_BUILD:
        return "the other program is another build of ReBushido (the same "
               "version, commit and system, 64 or 32 bits, are needed)";
    case REFUSE_DISC:
        return "the other player's disc image is not the same as this one's";
    case REFUSE_MODS:
        return "the other player's mods that change how the game plays "
               "(weapons, motions, stages, the arena in play) or --weapon "
               "and --arena are not the same as this one's";
    default:
        return "the other program turned this one away";
    }
}

/* ---- the datagrams ---- */

static void port_PutVersion(u8 *out) {
    u32 length = port_TextLength(g_rb_version);
    u32 i;

    for (i = 0; i < VERSION_BYTES; i++) {
        out[i] =
            (u8)(i < length && i < VERSION_BYTES - 1 ? g_rb_version[i] : 0);
    }
}

static void port_PutLong(u8 *out, u64 value) {
    port_NetPutWord(out, (u32)value);
    port_NetPutWord(out + WORD_BYTES, (u32)(value >> WORD_BITS));
}

static u64 port_GetLong(const u8 *in) {
    return port_NetGetWord(in) |
           ((u64)port_NetGetWord(in + WORD_BYTES) << WORD_BITS);
}

/* A hello (the guest's) or a welcome (the host's). */
static void port_SendMeet(s32 kind) {
    u8 out[MEET_BYTES];

    port_NetHeader(out, kind, 0, SESSION_PROTOCOL);
    port_PutLong(out + MEET_BUILD, g_session.build);
    port_PutLong(out + MEET_DISC, g_session.disc);
    port_PutLong(out + MEET_MODS, g_session.mods);
    port_NetPutWord(out + MEET_DELAY, g_session.delay);
    port_NetPutWord(out + MEET_SEED, g_session.seed);
    port_NetPutWord(out + MEET_CARDS, g_session.cards);
    port_NetPutWord(out + MEET_SHAPE, g_session.shape);
    port_NetPutWord(out + MEET_KIND, g_session.kind);
    port_PutVersion(out + MEET_VERSION);
    port_NetSend(out, sizeof(out));
}

static void port_SendShort(s32 kind, s32 detail, u32 number) {
    u8 out[PORT_NET_HEADER_BYTES];
    s32 i;

    port_NetHeader(out, kind, detail, number);
    /* these say something once and for all: a few times */
    for (i = 0; i < (kind == KIND_READY || kind == KIND_CARD_ACK ? 1 : GOES);
         i++) {
        port_NetSend(out, sizeof(out));
    }
}

/* The shape of the other player's picture: the wider of the two is what
 * both games cut faces off at (port_RemasterFixMargin), so that neither
 * misses a face its picture shows and the two compute the same. */
static void port_TakeShape(u32 theirs) {
    u32 their_w = theirs >> SHAPE_SHIFT;
    u32 their_h = theirs & SHAPE_MASK;
    u32 wider_w = g_session.wider >> SHAPE_SHIFT;
    u32 wider_h = g_session.wider & SHAPE_MASK;

    if (their_w != 0 && their_h != 0 && their_w * wider_h > wider_w * their_h) {
        g_session.wider = theirs;
    }
}

/* The reason the other's hello or welcome is not this program's: 0 if it
 * is. Its version's text is said in the log. */
static s32 port_CheckMeet(const u8 *in, s32 bytes, u32 protocol) {
    char version[VERSION_BYTES];
    s32 i;

    if (bytes < MEET_BYTES || protocol != SESSION_PROTOCOL) {
        return REFUSE_PROTOCOL;
    }
    for (i = 0; i < VERSION_BYTES; i++) {
        version[i] = (char)in[MEET_VERSION + i];
    }
    version[VERSION_BYTES - 1] = 0;
    if (port_GetLong(in + MEET_BUILD) != g_session.build) {
        port_Log("session: the other program is %s, this one %s\n", version,
                 g_rb_version);
        return REFUSE_BUILD;
    }
    if (port_GetLong(in + MEET_DISC) != g_session.disc) {
        return REFUSE_DISC;
    }
    if (port_GetLong(in + MEET_MODS) != g_session.mods) {
        return REFUSE_MODS;
    }
    port_TakeShape(port_NetGetWord(in + MEET_SHAPE));
    return 0;
}

/* The status line, now and then, to the window (port_net.h) or once to
 * the log where there is none. */
static void port_SessionStatus(const char *text) {
    static char said[STATUS_BYTES];
    u32 now = port_NetClock();
    u32 i;

    if (g_port_net_wait_hook != NULL) {
        if (now - g_session.status_at >= STATUS_EVERY_MS) {
            g_session.status_at = now;
            g_port_net_wait_hook(text);
        }
        return;
    }
    if (port_TextsEqual(said, text)) {
        return;
    }
    for (i = 0; i + 1 < STATUS_BYTES && text[i] != 0; i++) {
        said[i] = text[i];
    }
    said[i] = 0;
    port_Log("session: %s\n", text);
}

/* printf's work into `out`. */
static void port_SessionText(char *out, u32 room, const char *format, ...) {
    PortArgs args;

    PORT_ARGS_START(args, format);
    port_FormatText(out, room, format, args);
    PORT_ARGS_END(args);
}

/* `what` and the address the session is at, `ADDRESS:PORT`. */
static void port_SessionAt(char *out, u32 room, const char *what) {
    const PortNetAddress *at = &g_session.address;

    port_SessionText(out, room, "%s %u.%u.%u.%u:%u", what, at->address >> 24,
                     (at->address >> 16) & 0xFF, (at->address >> 8) & 0xFF,
                     at->address & 0xFF, at->port);
}

/* `what`, and how far the cards have come. */
static void port_SessionProgress(char *out, u32 room, const char *what) {
    port_SessionText(
        out, room, "%s: %u%%", what,
        g_session.needed == 0 ? PERCENT
                              : g_session.have * PERCENT / g_session.needed);
}

static s32 port_HasChunk(u32 chunk) {
    return (g_session.map[chunk / BITS_PER_BYTE] >> (chunk % BITS_PER_BYTE)) &
           1;
}

static s32 port_ChunkIsNeeded(u32 chunk) {
    return (g_session.cards >> (chunk / CHUNKS_PER_CARD)) & 1;
}

/* ---- the host ---- */

/* The cards' chunks the guest has not said it has, a few at a time. */
static void port_SendChunks(void) {
    u8 out[PORT_NET_HEADER_BYTES + CHUNK_BYTES];
    u32 sent = 0;
    u32 tried;

    for (tried = 0; tried < CHUNKS && sent < CHUNKS_AT_ONCE; tried++) {
        u32 chunk = g_session.next_chunk;
        u32 i;

        g_session.next_chunk = (g_session.next_chunk + 1) % CHUNKS;
        if (!port_ChunkIsNeeded(chunk) || port_HasChunk(chunk)) {
            continue;
        }
        port_NetHeader(out, KIND_CARD, 0, chunk);
        for (i = 0; i < CHUNK_BYTES; i++) {
            out[PORT_NET_HEADER_BYTES + i] =
                g_session_cards[chunk / CHUNKS_PER_CARD]
                               [(chunk % CHUNKS_PER_CARD) * CHUNK_BYTES + i];
        }
        port_NetSend(out, sizeof(out));
        sent++;
    }
}

/* The host: waits for a hello, holds it to this program and turns it
 * away or lets it in; then sends its cards until the guest has them all
 * and says it is ready, and tells it to go. 1, or 0 if nobody came. */
static s32 port_HostSession(void) {
    char status[STATUS_BYTES];
    u8 in[PORT_NET_DATAGRAM_MAX];
    u32 start = port_NetClock();
    u32 sent_at = 0;
    u32 welcomed_at = 0;
    s32 met = 0;

    port_SessionAt(status, sizeof(status), "Hosting a game at");
    for (;;) {
        u32 now = port_NetClock();
        s32 bytes = port_NetReceive(in, sizeof(in), POLL_MS);

        if (bytes > 0) {
            s32 detail;
            u32 number;
            s32 kind = port_NetReadHeader(in, bytes, &detail, &number);
            s32 reason;

            if (kind == KIND_HELLO && !met) {
                reason = port_CheckMeet(in, bytes, number);
                if (reason != 0) {
                    port_SendShort(KIND_REFUSE, reason, 0);
                    port_Log("session: a guest was turned away: %s\n",
                             port_RefusalText(reason));
                    continue;
                }
                port_FixNetPeer();
                met = 1;
                port_Log("session: the guest has come; sending it the "
                         "memory cards\n");
            }
            if (kind == KIND_HELLO && met) {
                port_SendMeet(KIND_WELCOME);
                welcomed_at = now;
            } else if (kind == KIND_CARD_ACK &&
                       (u32)bytes >= PORT_NET_HEADER_BYTES + CHUNK_MAP_BYTES) {
                u32 i;

                for (i = 0; i < CHUNK_MAP_BYTES; i++) {
                    g_session.map[i] |= in[PORT_NET_HEADER_BYTES + i];
                }
                g_session.have = number;
            } else if (kind == KIND_READY && met) {
                port_SendShort(KIND_GO, 0, 0);
                return 1;
            }
        }
        if (met) {
            if (now - welcomed_at >= HELLO_EVERY_MS && g_session.have == 0) {
                port_SendMeet(KIND_WELCOME);
                welcomed_at = now;
            }
            if (now - sent_at >= CHUNKS_EVERY_MS) {
                port_SendChunks();
                sent_at = now;
            }
            port_SessionProgress(status, sizeof(status),
                                 "The other player has come: sending your "
                                 "memory cards");
        }
        port_SessionStatus(status);
        if (g_port_net_wait_hook == NULL && now - start > HOST_GIVE_UP_MS) {
            port_Log("session: nobody came\n");
            return 0;
        }
    }
}

/* ---- the guest ---- */

static void port_SendCardAck(void) {
    u8 out[PORT_NET_HEADER_BYTES + CHUNK_MAP_BYTES];
    u32 i;

    port_NetHeader(out, KIND_CARD_ACK, 0, g_session.have);
    for (i = 0; i < CHUNK_MAP_BYTES; i++) {
        out[PORT_NET_HEADER_BYTES + i] = g_session.map[i];
    }
    port_NetSend(out, sizeof(out));
}

/* The guest: says hello until the host lets it in or turns it away,
 * takes the host's delay, seed and cards, says which parts of the cards
 * it has until it has them all, and goes when the host says go (or its
 * first inputs come). 1, or 0 if it was turned away or nobody answered. */
static s32 port_JoinSession(void) {
    char status[STATUS_BYTES];
    u8 in[PORT_NET_DATAGRAM_MAX];
    u32 start = port_NetClock();
    u32 hello_at = 0;
    u32 ack_at = 0;
    s32 welcomed = 0;

    port_SessionAt(status, sizeof(status), "Joining the game at");
    for (;;) {
        u32 now = port_NetClock();
        s32 bytes = port_NetReceive(in, sizeof(in), POLL_MS);

        if (bytes > 0) {
            s32 detail;
            u32 number;
            s32 kind = port_NetReadHeader(in, bytes, &detail, &number);

            if (kind == KIND_REFUSE) {
                port_Log("session: the host turned this program away: %s\n",
                         port_RefusalText(detail));
                return 0;
            }
            if (kind == KIND_WELCOME && !welcomed) {
                s32 reason = port_CheckMeet(in, bytes, number);

                if (reason != 0) {
                    port_Log("session: the host is not this program's kind: "
                             "%s\n",
                             port_RefusalText(reason));
                    return 0;
                }
                welcomed = 1;
                g_session.delay = port_NetGetWord(in + MEET_DELAY);
                g_session.seed = port_NetGetWord(in + MEET_SEED);
                g_session.cards = port_NetGetWord(in + MEET_CARDS);
                g_session.kind = port_NetGetWord(in + MEET_KIND);
                g_session.needed = 0;
                for (number = 0; number < CHUNKS; number++) {
                    g_session.needed += (u32)port_ChunkIsNeeded(number);
                }
                port_Log(
                    "session: the host has let this program in; its "
                    "delay is %u frames, its cards %s%s\n",
                    g_session.delay, (g_session.cards & 1) ? "slot 1 " : "",
                    (g_session.cards & 2)  ? "slot 2"
                    : g_session.cards == 0 ? "none"
                                           : "");
            } else if (kind == KIND_CARD && welcomed && number < CHUNKS &&
                       bytes >= PORT_NET_HEADER_BYTES + CHUNK_BYTES &&
                       port_ChunkIsNeeded(number) && !port_HasChunk(number)) {
                u32 i;

                for (i = 0; i < CHUNK_BYTES; i++) {
                    g_session_cards[number / CHUNKS_PER_CARD]
                                   [(number % CHUNKS_PER_CARD) * CHUNK_BYTES +
                                    i] = in[PORT_NET_HEADER_BYTES + i];
                }
                g_session.map[number / BITS_PER_BYTE] |=
                    (u8)(1 << (number % BITS_PER_BYTE));
                g_session.have++;
            } else if ((kind == KIND_GO || kind == PORT_NET_KIND_NETPLAY) &&
                       welcomed && g_session.have == g_session.needed) {
                /* the host has started: its inputs are coming */
                return 1;
            }
        }
        if (!welcomed && now - hello_at >= HELLO_EVERY_MS) {
            port_SendMeet(KIND_HELLO);
            hello_at = now;
        }
        if (welcomed && now - ack_at >= ACK_EVERY_MS) {
            port_SendCardAck();
            if (g_session.have == g_session.needed) {
                port_SendShort(KIND_READY, 0, 0);
            }
            ack_at = now;
        }
        if (welcomed) {
            port_SessionProgress(status, sizeof(status),
                                 "The host has let you in: receiving its "
                                 "memory cards");
            start = now;
        } else if (now - start > JOIN_GIVE_UP_MS) {
            port_Log("session: nobody answered at %u.%u.%u.%u:%u\n",
                     g_session.address.address >> 24,
                     (g_session.address.address >> 16) & 0xFF,
                     (g_session.address.address >> 8) & 0xFF,
                     g_session.address.address & 0xFF, g_session.address.port);
            return 0;
        }
        port_SessionStatus(status);
    }
}

/* ---- the start ---- */

s32 port_StartSession(const PortDisc *disc, u32 delay) {
    PortRemasterView view;
    u32 chunk;
    s32 slot;
    s32 ok;

    g_session.build = port_SessionBuild();
    g_session.disc = port_SessionDisc(disc);
    g_session.mods = port_WeaponsSessionHash() + port_ArenaSessionHash();
    g_session.delay = delay;
    g_session.kind = (u32)port_GetStageKindDefault();
    port_RemasterGetView(&view);
    g_session.shape =
        ((u32)view.aspect_w << SHAPE_SHIFT) | ((u32)view.aspect_h & SHAPE_MASK);
    g_session.wider = g_session.shape;
    g_session_cards = port_HostAllocate(PORT_CARD_SLOTS * PORT_CARD_IMAGE_SIZE);
    if (g_session_cards == NULL) {
        port_Log("session: no memory for the memory cards\n");
        return 0;
    }
    if (g_session.role == PORT_SESSION_HOST) {
        g_session.cards = 0;
        for (slot = 0; slot < PORT_CARD_SLOTS; slot++) {
            if (port_ReadCardFile(slot, g_session_cards[slot])) {
                g_session.cards |= 1U << slot;
            }
        }
        g_session.needed = 0;
        for (chunk = 0; chunk < CHUNKS; chunk++) {
            g_session.needed += (u32)port_ChunkIsNeeded(chunk);
        }
        /* nothing of the game's: only that two sessions differ */
        g_session.seed = (port_HostTicks() ^ port_NetClock()) & SEED_MASK;
        ok = port_HostSession();
    } else {
        ok = port_JoinSession();
    }
    if (!ok) {
        return 0;
    }
    for (slot = 0; slot < PORT_CARD_SLOTS; slot++) {
        port_SetCardImage(
            slot,
            ((g_session.cards >> slot) & 1) ? g_session_cards[slot] : NULL);
    }
    port_SetNetplay(
        g_session.role == PORT_SESSION_HOST ? 0 : 1, g_session.delay);
    port_HoldStageKindDefault((s32)g_session.kind);
    port_RemasterFixMargin((s32)(g_session.wider >> SHAPE_SHIFT),
                           (s32)(g_session.wider & SHAPE_MASK));
    port_Log("session: begun: this program is player %d, with a delay of %u "
             "frames and the seed %u; the host's memory cards (%s%s)\n",
             g_session.role == PORT_SESSION_HOST ? 1 : 2, g_session.delay,
             g_session.seed, (g_session.cards & 1) ? "slot 1 " : "",
             (g_session.cards & 2)  ? "slot 2"
             : g_session.cards == 0 ? "none"
                                    : "");
    return 1;
}

void port_SessionSeed(void) {
    if (g_session.role == PORT_SESSION_NONE) {
        return;
    }
    rng_SetSeed((s32)g_session.seed);
    srand(g_session.seed);
}

/* ---- the presses that are nobody's ---- */

/* Whether the session presses for both at this frame: everything from
 * power on to the select screen is the session's. */
static s32 port_SessionPilots(void) {
#if RB_OPTIONS_PAGE
    if (g_rb_page_state == RB_PAGE_UP) {
        return TRUE;
    }
#endif
    if (g_sys_screen == SCREEN_MOVIE || g_sys_screen == SCREEN_MENU_INIT) {
        return TRUE;
    }
    return g_sys_screen == SCREEN_MENU_UPDATE &&
           g_menu_page != MENU_PAGE_SELECT &&
           g_menu_page != MENU_PAGE_TEAM_SELECT;
}

/* The session's press at this frame, where it is pressing: a button every
 * PILOT_EVERY frames and nothing between, so that each is a press. */
static u16 port_SessionPress(u32 frame) {
    s32 beat = frame / PILOT_EVERY;

    if (g_sys_screen == SCREEN_MENU_UPDATE && g_menu_page == MENU_PAGE_TOP) {
        /* the top menu counts the frames since a button was last held,
         * which is how long the cursor has had to settle */
        if (g_sys_screen_frames < PILOT_TOP_SETTLE) {
            return 0;
        }
        return menu_GetTopRowMode() == FIGHT_MODE_VS ? PADRdown : PADLdown;
    }
    if (frame % PILOT_EVERY != 0) {
        return 0;
    }
#if RB_OPTIONS_PAGE
    if (g_rb_page_state == RB_PAGE_UP) {
        return PADstart;
    }
#endif
    if (g_sys_screen == SCREEN_MOVIE) {
        /* the title wants Start; the memory card's message, Cross */
        return (beat & 1) ? PADRdown : PADstart;
    }
    if (g_sys_screen != SCREEN_MENU_UPDATE) {
        return 0;
    }
    /* the memory card's questions at boot: yes, load the host's save */
    return PADRdown;
}

/* A frame's input as the session has it (port_net.h): the session's own
 * presses up to the select screen, and no Start in a fight. */
void port_SessionInput(u32 frame, PortInput *input, u16 *used) {
    s32 pad;

    *used = 0xFFFF;
    if (g_session.role == PORT_SESSION_NONE) {
        return;
    }
    if (port_SessionPilots()) {
        input->buttons[0] = port_SessionPress(frame);
        input->buttons[1] = 0;
        *used = 0;
        return;
    }
    if (g_sys_screen == SCREEN_FIGHT_UPDATE) {
        for (pad = 0; pad < PORT_PADS; pad++) {
            input->buttons[pad] &= (u16)~PADstart;
        }
        *used = (u16)~PADstart;
    }
}
