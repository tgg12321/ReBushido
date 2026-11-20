/* A room's match watched (port_room.h; docs/NETPLAY.md, section 16): its
 * host's feed to the lobby, and a spectator's side of it. Outside the
 * state: nothing here is the game's.
 *
 * The match's host (its first player, the session's host) sends the
 * server what a spectator needs and the server keeps it while the match
 * lasts: the session's settings (session.c, port_SessionSettings); the
 * start of its latest fight as a replay keeps it, compressed as soon as
 * it is final (replay.c, port_ReplayServe), a kilobyte a datagram; and
 * from that start's frame on, every frame's two pads once both are known,
 * with every final hash. The server says what it has, and the host sends
 * what it has not, as the cards are sent in a meeting (session.c).
 *
 * A spectator asks the server for the same: the settings, the start, the
 * chunks it has not, then the inputs from the frame it wants. It puts the
 * start in (port_ReplayLoadServed) and runs the inputs on (netplay.c,
 * port_RunWatch), holding each hash it makes to the players'. A
 * spectator who comes in the middle of a fight runs from that fight's
 * start, quietly, until it is with the players. */
#include "common.h"
#include "port_host.h"
#include "port_net.h"
#include "port_replay.h"
#include "port_room.h"
#include "bb2.h"

#define CODE_FIELD 8
#define TOKEN_BYTES 16
#define WORD_BYTES 4
#define HALF_BYTES 2
#define LONG_BYTES 8
#define BITS_PER_BYTE 8
#define WORD_BITS 32
#define CHUNK_BYTES 1024
#define CHUNKS_MAX 1024
#define CHUNK_MAP_BYTES (CHUNKS_MAX / BITS_PER_BYTE)
#define START_MAX (CHUNKS_MAX * CHUNK_BYTES)
#define NO_START 0xFFFFFFFFU
/* the frames kept, from the session's first: over two hours */
#define FRAMES_MAX 0x40000
#define INPUT_BYTES 4
/* eight hashes' frames: every hash among them fits in a datagram */
#define INPUTS_A_DATAGRAM 240
#define HASHES_A_DATAGRAM 8
#define HASH_BYTES 12
#define HASHES_KEPT 256
#define HASHES_MASK (HASHES_KEPT - 1)
#define HASH_EVERY 30
/* how often the host sends and a spectator asks, and how much a turn */
#define FEED_EVERY_MS 20
#define CHUNKS_A_TURN 6
#define INPUT_DATAGRAMS_A_TURN 2
#define WATCH_EVERY_MS 25
#define FINISH_MS 3000
#define POLL_MS 2
#define FEED_ACK_BYTES 160
#define WATCH_BYTES 192
#define SETTINGS_BYTES PORT_SESSION_SETTINGS_BYTES

enum {
    KIND_FEED = PORT_NET_KIND_RELAY + 11,
    KIND_FEED_ACK,
    KIND_WATCH,
    KIND_SCENE
};

/* FEED's and SCENE's parts (their detail). */
enum {
    PART_SETTINGS = 1,
    PART_START = 2,
    PART_INPUTS = 3,
    PART_END = 4,
    PART_NONE = 5,
};

/* FEED's fields: the match's code and ticket, then each part's. */
enum {
    FEED_CODE = PORT_NET_HEADER_BYTES,
    FEED_TICKET = FEED_CODE + CODE_FIELD,
    FEED_BODY = FEED_TICKET + PORT_RELAY_TICKET_BYTES,
    /* a start's chunk: its frame, its size, the chunk's place and count */
    FEED_START_DATA = FEED_BODY + 3 * WORD_BYTES,
    /* inputs: the first frame, how many, how many hashes, then those */
    FEED_INPUT_HASHES = FEED_BODY + 2 * WORD_BYTES,
};

/* FEED_ACK's: the start's frame, the settings had, the end had, the
 * next frame the server wants, the start's chunks it has. */
enum {
    ACK_START = PORT_NET_HEADER_BYTES + CODE_FIELD,
    ACK_SETTINGS = ACK_START + WORD_BYTES,
    ACK_END = ACK_SETTINGS + 1,
    ACK_NEXT = ACK_SETTINGS + WORD_BYTES,
    ACK_MAP = ACK_NEXT + WORD_BYTES,
};

/* WATCH's: the room's code, the member's token, the match's code, the
 * start it has, the frame it wants, the settings had, a stamp, the map. */
enum {
    WATCH_ROOM = PORT_NET_HEADER_BYTES,
    WATCH_TOKEN = WATCH_ROOM + CODE_FIELD,
    WATCH_MATCH = WATCH_TOKEN + TOKEN_BYTES,
    WATCH_START = WATCH_MATCH + CODE_FIELD,
    WATCH_WANTS = WATCH_START + WORD_BYTES,
    WATCH_FLAGS = WATCH_WANTS + WORD_BYTES,
    WATCH_STAMP = WATCH_FLAGS + WORD_BYTES,
    WATCH_MAP = WATCH_STAMP + WORD_BYTES,
};

/* SCENE's: the match's code, then each part's. */
enum {
    SCENE_BODY = PORT_NET_HEADER_BYTES + CODE_FIELD,
    SCENE_NAMES = SCENE_BODY + SETTINGS_BYTES,
    SCENE_START_DATA = SCENE_BODY + 3 * WORD_BYTES,
    SCENE_INPUT_HASHES = SCENE_BODY + 2 * WORD_BYTES,
};

/* A frame's hash, in two words: laid out alike in both sizes of the port. */
typedef struct {
    u32 frame;
    u32 low;
    u32 high;
} WatchHash;

/* ---- the host's feed ---- */

static struct {
    s32 on;
    PortRoomMatch match;
    u8 settings[SETTINGS_BYTES];
    s32 settings_had;
    /* the start being sent, by the count replay.c gives each */
    u32 made;
    const u8 *blob;
    u32 size;
    u32 frame;
    u32 start_had; /* the server's start frame, NO_START for none */
    u8 map[CHUNK_MAP_BYTES];
    u32 next_chunk;
    /* the frames' inputs, from the session's first, and the server's
     * next wanted */
    u32 *inputs;
    u32 fed;
    u32 server_next;
    WatchHash hashes[HASHES_KEPT];
    u32 hash_count;
    s32 ended;
    u32 end_frame;
    s32 winner;
    s32 end_had;
    u32 sent_at;
} g_feed;

static void port_WatchPutCode(u8 *out, const char *code) {
    u32 i;

    for (i = 0; i < CODE_FIELD; i++) {
        out[i] = 0;
    }
    for (i = 0; code[i] != 0 && i < CODE_FIELD; i++) {
        out[i] = (u8)code[i];
    }
}

static void port_WatchGetCode(char *out, const u8 *in) {
    u32 i;

    for (i = 0; i + 1 < PORT_ROOM_CODE_BYTES && in[i] != 0; i++) {
        out[i] = (char)in[i];
    }
    out[i] = 0;
}

static s32 port_WatchCodeIs(const u8 *in, const char *code) {
    char theirs[PORT_ROOM_CODE_BYTES];

    port_WatchGetCode(theirs, in);
    return port_TextsEqual(theirs, code);
}

static s32 port_MapHas(const u8 *map, u32 chunk) {
    return (map[chunk / BITS_PER_BYTE] >> (chunk % BITS_PER_BYTE)) & 1;
}

/* This program hosts a room's match (port_room.h): the feed's words
 * forgotten, the session's settings taken, the fights' starts served. */
void port_FeedBegin(const PortRoomMatch *match) {
    u32 i;

    if (g_feed.inputs == NULL) {
        g_feed.inputs = port_HostAllocate(FRAMES_MAX * INPUT_BYTES);
    }
    g_feed.on = g_feed.inputs != NULL;
    g_feed.match = *match;
    g_feed.settings_had = FALSE;
    g_feed.made = 0;
    g_feed.blob = NULL;
    g_feed.start_had = NO_START;
    g_feed.server_next = NO_START;
    g_feed.next_chunk = 0;
    g_feed.fed = 0;
    g_feed.hash_count = 0;
    g_feed.ended = FALSE;
    g_feed.end_had = FALSE;
    g_feed.sent_at = 0;
    for (i = 0; i < CHUNK_MAP_BYTES; i++) {
        g_feed.map[i] = 0;
    }
    port_SessionSettings(g_feed.settings);
    port_ReplayServe(g_feed.on);
    port_Log("watch: this program sends match %d's feed to the lobby, for "
             "its spectators\n",
             match->number);
}

s32 port_FeedIsOn(void) { return g_feed.on; }

void port_FeedFrame(u32 frame, u16 first, u16 second) {
    if (!g_feed.on || frame >= FRAMES_MAX || frame != g_feed.fed) {
        return;
    }
    g_feed.inputs[frame] = (u32)first | ((u32)second << (2 * BITS_PER_BYTE));
    g_feed.fed = frame + 1;
}

void port_FeedHash(u32 frame, u64 hash) {
    WatchHash *at;

    if (!g_feed.on) {
        return;
    }
    at = &g_feed.hashes[g_feed.hash_count++ & HASHES_MASK];
    at->frame = frame;
    at->low = (u32)hash;
    at->high = (u32)(hash >> WORD_BITS);
}

void port_FeedEnd(u32 frame, s32 winner) {
    if (!g_feed.on) {
        return;
    }
    g_feed.ended = TRUE;
    g_feed.end_frame = frame;
    g_feed.winner = winner;
    g_feed.sent_at = 0;
    port_FeedTick();
}

/* A FEED's head: its part, the match's code and ticket. */
static u32 port_FeedHead(u8 *out, s32 part, u32 number) {
    u32 i;

    port_NetHeader(out, KIND_FEED, part, number);
    port_WatchPutCode(out + FEED_CODE, g_feed.match.code);
    for (i = 0; i < PORT_RELAY_TICKET_BYTES; i++) {
        out[FEED_TICKET + i] = g_feed.match.ticket[i];
    }
    return FEED_BODY;
}

/* The start's chunks the server has not said it has, a few. */
static void port_FeedChunks(void) {
    u8 out[PORT_NET_DATAGRAM_MAX];
    u32 count = (g_feed.size + CHUNK_BYTES - 1) / CHUNK_BYTES;
    u32 sent = 0;
    u32 tried;

    for (tried = 0; tried < count && sent < CHUNKS_A_TURN; tried++) {
        u32 chunk = g_feed.next_chunk;
        u32 bytes;
        u32 i;

        g_feed.next_chunk = (g_feed.next_chunk + 1) % count;
        if (g_feed.start_had == g_feed.frame &&
            port_MapHas(g_feed.map, chunk)) {
            continue;
        }
        bytes = g_feed.size - chunk * CHUNK_BYTES < CHUNK_BYTES
                    ? g_feed.size - chunk * CHUNK_BYTES
                    : CHUNK_BYTES;
        port_FeedHead(out, PART_START, 0);
        port_NetPutWord(out + FEED_BODY, g_feed.frame);
        port_NetPutWord(out + FEED_BODY + WORD_BYTES, g_feed.size);
        out[FEED_BODY + 2 * WORD_BYTES] = (u8)chunk;
        out[FEED_BODY + 2 * WORD_BYTES + 1] = (u8)(chunk >> BITS_PER_BYTE);
        out[FEED_BODY + 2 * WORD_BYTES + 2] = (u8)count;
        out[FEED_BODY + 2 * WORD_BYTES + 3] = (u8)(count >> BITS_PER_BYTE);
        for (i = 0; i < bytes; i++) {
            out[FEED_START_DATA + i] = g_feed.blob[chunk * CHUNK_BYTES + i];
        }
        port_RoomSend(out, FEED_START_DATA + bytes);
        sent++;
    }
}

/* The inputs from the server's next on, with the final hashes among
 * them. */
static void port_FeedInputs(void) {
    u8 out[PORT_NET_DATAGRAM_MAX];
    u32 first = g_feed.server_next;
    s32 datagram;

    for (datagram = 0; datagram < INPUT_DATAGRAMS_A_TURN && first < g_feed.fed;
         datagram++) {
        u32 count = g_feed.fed - first < INPUTS_A_DATAGRAM ? g_feed.fed - first
                                                           : INPUTS_A_DATAGRAM;
        u32 hashes = 0;
        u32 at;
        u32 i;

        port_FeedHead(out, PART_INPUTS, 0);
        port_NetPutWord(out + FEED_BODY, first);
        out[FEED_BODY + WORD_BYTES] = (u8)count;
        out[FEED_BODY + WORD_BYTES + 1] = (u8)(count >> BITS_PER_BYTE);
        for (i = 0; i < HASHES_KEPT && hashes < HASHES_A_DATAGRAM &&
                    i < g_feed.hash_count;
             i++) {
            const WatchHash *hash = &g_feed.hashes[i];

            if (hash->frame >= first && hash->frame < first + count) {
                u8 *put = out + FEED_INPUT_HASHES + hashes * HASH_BYTES;

                port_NetPutWord(put, hash->frame);
                port_NetPutWord(put + WORD_BYTES, hash->low);
                port_NetPutWord(put + 2 * WORD_BYTES, hash->high);
                hashes++;
            }
        }
        out[FEED_BODY + WORD_BYTES + HALF_BYTES] = (u8)hashes;
        out[FEED_BODY + WORD_BYTES + HALF_BYTES + 1] = 0;
        at = FEED_INPUT_HASHES + hashes * HASH_BYTES;
        for (i = 0; i < count; i++) {
            port_NetPutWord(
                out + at + i * INPUT_BYTES, g_feed.inputs[first + i]);
        }
        port_RoomSend(out, at + count * INPUT_BYTES);
        first += count;
    }
}

/* The feed's turn (port_room.h): the settings, the newest start's chunks
 * and the inputs the server lacks, and the end, every FEED_EVERY_MS. */
void port_FeedTick(void) {
    u8 out[PORT_NET_DATAGRAM_MAX];
    const u8 *blob;
    u32 size;
    u32 frame;
    u32 made;
    u32 now = port_NetClock();
    u32 i;

    if (!g_feed.on || now - g_feed.sent_at < FEED_EVERY_MS) {
        return;
    }
    g_feed.sent_at = now;
    if (!g_feed.settings_had) {
        port_FeedHead(out, PART_SETTINGS, 0);
        for (i = 0; i < SETTINGS_BYTES; i++) {
            out[FEED_BODY + i] = g_feed.settings[i];
        }
        port_RoomSend(out, FEED_BODY + SETTINGS_BYTES);
    }
    if (port_ReplayServedStart(&blob, &size, &frame, &made) &&
        made != g_feed.made && size <= START_MAX) {
        /* a fight's new start: the server's copy is replaced */
        g_feed.made = made;
        g_feed.blob = blob;
        g_feed.size = size;
        g_feed.frame = frame;
        g_feed.next_chunk = 0;
        port_Log("watch: the start of the fight at frame %u is sent to the "
                 "lobby (%u bytes)\n",
                 frame, size);
    }
    if (g_feed.blob != NULL) {
        port_FeedChunks();
    }
    if (g_feed.server_next != NO_START) {
        port_FeedInputs();
    }
    if (g_feed.ended && !g_feed.end_had) {
        port_FeedHead(out, PART_END, 0);
        port_NetPutWord(out + FEED_BODY, g_feed.end_frame);
        out[FEED_BODY + WORD_BYTES] = (u8)g_feed.winner;
        out[FEED_BODY + WORD_BYTES + 1] = 0;
        out[FEED_BODY + WORD_BYTES + 2] = 0;
        out[FEED_BODY + WORD_BYTES + 3] = 0;
        port_RoomSend(out, FEED_BODY + 2 * WORD_BYTES);
    }
}

/* The server's word on the feed: what it has. */
static void port_FeedTakeAck(const u8 *in, s32 bytes) {
    u32 i;

    if (!g_feed.on || bytes != FEED_ACK_BYTES ||
        !port_WatchCodeIs(in + PORT_NET_HEADER_BYTES, g_feed.match.code)) {
        return;
    }
    g_feed.settings_had = in[ACK_SETTINGS] != 0;
    g_feed.end_had = in[ACK_END] != 0;
    g_feed.start_had = port_NetGetWord(in + ACK_START);
    g_feed.server_next = port_NetGetWord(in + ACK_NEXT);
    for (i = 0; i < CHUNK_MAP_BYTES; i++) {
        g_feed.map[i] = in[ACK_MAP + i];
    }
    if (g_feed.blob != NULL && g_feed.start_had == g_feed.frame) {
        u32 count = (g_feed.size + CHUNK_BYTES - 1) / CHUNK_BYTES;
        u32 had = 0;

        for (i = 0; i < count; i++) {
            had += (u32)port_MapHas(g_feed.map, i);
        }
        if (had == count) {
            /* all of it: nothing more of this start to send */
            g_feed.blob = NULL;
            port_Log("watch: the lobby has the fight's start of frame %u\n",
                     g_feed.frame);
        }
    }
}

/* The feed's last words (port_room.h). */
void port_FeedFinish(void) {
    u32 start = port_NetClock();

    /* the end and every input before it, had by the server */
    while (g_feed.on && g_feed.ended &&
           (!g_feed.end_had || (g_feed.server_next != NO_START &&
                                g_feed.server_next < g_feed.fed)) &&
           port_NetClock() - start < FINISH_MS) {
        port_FeedTick();
        port_RoomWait(POLL_MS);
    }
    g_feed.on = FALSE;
    port_ReplayServe(FALSE);
}

/* ---- a spectator ---- */

static struct {
    s32 on;
    PortRoomMatch match;
    u8 settings[SETTINGS_BYTES];
    s32 has_settings;
    u8 *start;
    u32 start_frame; /* the start being had: NO_START for none */
    u32 start_size;
    u32 start_chunks;
    u32 have;
    u8 map[CHUNK_MAP_BYTES];
    s32 started; /* put in: the inputs are asked for */
    u32 *inputs;
    u32 from;  /* the start's frame */
    u32 known; /* every frame before it has come */
    WatchHash hashes[HASHES_KEPT];
    s32 ended;
    u32 end_frame;
    s32 winner;
    s32 nothing;
    u32 asked_at;
    u32 heard_at;
} g_watch;

/* A spectator of `match` (port_room.h): the memory for a start and the
 * inputs, given once, and what was had of another match forgotten. */
void port_WatchBegin(const PortRoomMatch *match) {
    u32 i;

    if (g_watch.start == NULL) {
        g_watch.start = port_HostAllocate(START_MAX);
        g_watch.inputs = port_HostAllocate(FRAMES_MAX * INPUT_BYTES);
    }
    g_watch.on = g_watch.start != NULL && g_watch.inputs != NULL;
    g_watch.match = *match;
    g_watch.has_settings = FALSE;
    g_watch.start_frame = NO_START;
    g_watch.have = 0;
    g_watch.started = FALSE;
    g_watch.known = 0;
    g_watch.ended = FALSE;
    g_watch.nothing = 0;
    g_watch.asked_at = 0;
    g_watch.heard_at = port_NetClock();
    for (i = 0; i < CHUNK_MAP_BYTES; i++) {
        g_watch.map[i] = 0;
    }
    for (i = 0; i < HASHES_KEPT; i++) {
        g_watch.hashes[i].frame = NO_START;
    }
}

void port_WatchStop(void) { g_watch.on = FALSE; }

/* A spectator's ask, every WATCH_EVERY_MS: the start it has and its
 * chunks, or the frame it wants next. */
void port_WatchTick(void) {
    u8 out[WATCH_BYTES];
    char code[CODE_FIELD];
    u8 token[TOKEN_BYTES];
    u32 now = port_NetClock();
    u32 i;

    if (!g_watch.on || now - g_watch.asked_at < WATCH_EVERY_MS ||
        !port_RoomCode(code, token)) {
        return;
    }
    g_watch.asked_at = now;
    for (i = 0; i < sizeof(out); i++) {
        out[i] = 0;
    }
    port_NetHeader(out, KIND_WATCH, 0, 0);
    port_WatchPutCode(out + WATCH_ROOM, code);
    for (i = 0; i < TOKEN_BYTES; i++) {
        out[WATCH_TOKEN + i] = token[i];
    }
    port_WatchPutCode(out + WATCH_MATCH, g_watch.match.code);
    port_NetPutWord(out + WATCH_START, g_watch.start_frame);
    port_NetPutWord(
        out + WATCH_WANTS, g_watch.started ? g_watch.known : NO_START);
    out[WATCH_FLAGS] = (u8)(g_watch.has_settings ? 1 : 0);
    port_NetPutWord(out + WATCH_STAMP, now);
    for (i = 0; i < CHUNK_MAP_BYTES; i++) {
        out[WATCH_MAP + i] = g_watch.map[i];
    }
    port_RoomSend(out, sizeof(out));
}

/* A chunk of the start: a new start forgets the one being had. */
static void port_WatchTakeChunk(const u8 *in, s32 bytes) {
    u32 frame = port_NetGetWord(in + SCENE_BODY);
    u32 size = port_NetGetWord(in + SCENE_BODY + WORD_BYTES);
    const u8 *at = in + SCENE_BODY + 2 * WORD_BYTES;
    u32 chunk = (u32)(at[0] | (at[1] << BITS_PER_BYTE));
    u32 count = (u32)(at[2] | (at[3] << BITS_PER_BYTE));
    u32 length = (u32)bytes - SCENE_START_DATA;
    u32 i;

    if (g_watch.started || size == 0 || size > START_MAX ||
        count != (size + CHUNK_BYTES - 1) / CHUNK_BYTES || chunk >= count ||
        length != (size - chunk * CHUNK_BYTES < CHUNK_BYTES
                       ? size - chunk * CHUNK_BYTES
                       : CHUNK_BYTES)) {
        return;
    }
    if (frame != g_watch.start_frame || size != g_watch.start_size) {
        g_watch.start_frame = frame;
        g_watch.start_size = size;
        g_watch.start_chunks = count;
        g_watch.have = 0;
        for (i = 0; i < CHUNK_MAP_BYTES; i++) {
            g_watch.map[i] = 0;
        }
    }
    if (port_MapHas(g_watch.map, chunk)) {
        return;
    }
    for (i = 0; i < length; i++) {
        g_watch.start[chunk * CHUNK_BYTES + i] = in[SCENE_START_DATA + i];
    }
    g_watch.map[chunk / BITS_PER_BYTE] |= (u8)(1 << (chunk % BITS_PER_BYTE));
    g_watch.have++;
}

/* Inputs, and the players' hashes among them, from where they are had. */
static void port_WatchTakeInputs(const u8 *in, s32 bytes) {
    u32 first = port_NetGetWord(in + SCENE_BODY);
    const u8 *at = in + SCENE_BODY + WORD_BYTES;
    u32 count = (u32)(at[0] | (at[1] << BITS_PER_BYTE));
    u32 hashes = at[HALF_BYTES];
    u32 inputs = SCENE_INPUT_HASHES + hashes * HASH_BYTES;
    u32 i;

    if (!g_watch.started || hashes > HASHES_A_DATAGRAM ||
        (u32)bytes != inputs + count * INPUT_BYTES) {
        return;
    }
    for (i = 0; i < hashes; i++) {
        const u8 *put = in + SCENE_INPUT_HASHES + i * HASH_BYTES;
        u32 frame = port_NetGetWord(put);
        WatchHash *hash = &g_watch.hashes[(frame / HASH_EVERY) & HASHES_MASK];

        hash->frame = frame;
        hash->low = port_NetGetWord(put + WORD_BYTES);
        hash->high = port_NetGetWord(put + 2 * WORD_BYTES);
    }
    for (i = 0; i < count; i++) {
        u32 frame = first + i;

        if (frame == g_watch.known && frame < FRAMES_MAX) {
            g_watch.inputs[frame] =
                port_NetGetWord(in + inputs + i * INPUT_BYTES);
            g_watch.known++;
        }
    }
}

/* The server's word of a feed or a scene (port_room.h), by its part. */
void port_WatchTake(const u8 *in, s32 bytes) {
    s32 detail;
    u32 number;
    s32 kind = port_NetReadHeader(in, bytes, &detail, &number);
    u32 i;

    if (kind == KIND_FEED_ACK) {
        port_FeedTakeAck(in, bytes);
        return;
    }
    if (kind != KIND_SCENE || !g_watch.on || bytes < SCENE_BODY ||
        !port_WatchCodeIs(in + PORT_NET_HEADER_BYTES, g_watch.match.code)) {
        return;
    }
    g_watch.heard_at = port_NetClock();
    if (detail == PART_SETTINGS && bytes >= SCENE_NAMES) {
        for (i = 0; i < SETTINGS_BYTES; i++) {
            g_watch.settings[i] = in[SCENE_BODY + i];
        }
        g_watch.has_settings = TRUE;
    } else if (detail == PART_START && bytes > SCENE_START_DATA) {
        port_WatchTakeChunk(in, bytes);
    } else if (detail == PART_INPUTS && bytes >= SCENE_INPUT_HASHES) {
        port_WatchTakeInputs(in, bytes);
    } else if (detail == PART_END && bytes >= SCENE_BODY + WORD_BYTES + 1) {
        g_watch.ended = TRUE;
        g_watch.end_frame = port_NetGetWord(in + SCENE_BODY);
        g_watch.winner = in[SCENE_BODY + WORD_BYTES];
    } else if (detail == PART_NONE && bytes > SCENE_BODY) {
        g_watch.nothing = in[SCENE_BODY];
    }
}

const u8 *port_WatchSettings(void) {
    return g_watch.has_settings ? g_watch.settings : NULL;
}

s32 port_WatchStart(const u8 **blob, u32 *size) {
    if (g_watch.start_frame == NO_START ||
        g_watch.have < g_watch.start_chunks) {
        return FALSE;
    }
    *blob = g_watch.start;
    *size = g_watch.start_size;
    return TRUE;
}

void port_WatchStarted(u32 frame) {
    g_watch.started = TRUE;
    g_watch.from = frame;
    g_watch.known = frame;
    g_watch.asked_at = 0;
}

s32 port_WatchInput(u32 frame, u16 *first, u16 *second) {
    if (!g_watch.started || frame < g_watch.from || frame >= g_watch.known) {
        return FALSE;
    }
    *first = (u16)g_watch.inputs[frame];
    *second = (u16)(g_watch.inputs[frame] >> (2 * BITS_PER_BYTE));
    return TRUE;
}

u32 port_WatchKnown(void) { return g_watch.known; }

s32 port_WatchHash(u32 frame, u64 *hash) {
    const WatchHash *at = &g_watch.hashes[(frame / HASH_EVERY) & HASHES_MASK];

    if (at->frame != frame) {
        return FALSE;
    }
    *hash = at->low | ((u64)at->high << WORD_BITS);
    return TRUE;
}

s32 port_WatchEnd(u32 *frame, s32 *winner) {
    *frame = g_watch.end_frame;
    *winner = g_watch.winner;
    return g_watch.ended;
}

u32 port_WatchQuietMs(void) { return port_NetClock() - g_watch.heard_at; }

s32 port_WatchNothing(void) { return g_watch.nothing; }
