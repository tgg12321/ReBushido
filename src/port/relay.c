/* The lobby and the ways between two players (port_net.h; docs/NETPLAY.md,
 * section 14): the client of the lobby server (tools/relay/), which keeps
 * rooms, tells each player of a room the other's addresses, and passes
 * the datagrams on itself where no way between the two PCs is found.
 *
 * A host opens a room at the server, with a name and, if it likes, a
 * password; the server gives it a code. A guest asks for the room by its
 * code; a locked room asks it for proof of the password, which never
 * goes over the network: the host gives the server the room's salt and
 * SHA-256(salt || password), the server gives the guest the salt and a
 * fresh challenge, and the guest answers HMAC-SHA256 of the challenge
 * with that key. Then each is sent the other's candidates: the address
 * the server saw it at, its own address on its network, the port its
 * router mapped (UPnP, NAT-PMP or PCP: the window's portmap.c), and its
 * IPv6 address. Each sends a punch to every one of them at once, which
 * also opens its own router's and firewall's way for the other's; a
 * punch answered says that way works both ways, and the server, told
 * which kinds each has had answered, decides for both alike: IPv6 if both
 * have, else IPv4, else after PUNCH_WINDOW its own relay. The session's
 * own checks (session.c) then run over the way decided, as ever.
 *
 * Nothing here is the game's: the datagrams are the server's and the
 * punches, and the session's peer is set before the session begins. */
#include "common.h"
#include "port_host.h"
#include "port_net.h"
#include "bb2.h"

#define RELAY_VERSION 1
#define SALT_BYTES 16
#define KEY_BYTES 32
#define CHALLENGE_BYTES 16
#define NAME_BYTES 24
#define PLAYER_BYTES 16
#define SETUP_BYTES 20
#define CODE_FIELD 8
#define ADDRESS_FIELD 20
#define CANDIDATES_MAX 4
#define HELLO_BYTES 208
#define STATE_BYTES 208
#define PUNCH_BYTES 16
#define ROOM_BYTES 92
#define WHOAMI_BYTES 16
#define YOUARE_BYTES 28
#define LIST_BYTES 16
#define BITS_PER_BYTE 8
#define WORD_BYTES 4
#define V4_BYTES 4
#define HEX_LETTER_VALUE 10
#define HEX_DIGIT_BITS 4
/* An address's family as the server writes it. */
#define FAMILY_V6 6
/* 127.0.0.1, and the first byte of every address of the loopback. */
#define LOOPBACK 0x7F000001U
#define LOOPBACK_FIRST 127
#define ADDRESS_FIRST_SHIFT 24
/* A salt made of the clock: a linear congruential step (Numerical
 * Recipes'). */
#define SALT_MULTIPLY 1664525U
#define SALT_ADD 1013904223U
#define SALT_BYTE_SHIFT 24
#define SALT_SEED 0x9E3779B9U
#define SALT_CLOCK_SHIFT 16

/* How often things are said, and how long the server is waited for. */
#define HELLO_EVERY_MS 250
#define PUNCH_EVERY_MS 100
#define SERVER_GIVE_UP_MS 10000
#define POLL_MS 5
#define LIST_WAIT_MS 1500
#define STATUS_BYTES 400
#define TEXT_BYTES 400

enum {
    KIND_HELLO = PORT_NET_KIND_RELAY,
    KIND_STATE,
    KIND_PUNCH,
    KIND_PUNCH_ACK,
    KIND_LIST,
    KIND_ROOMS,
    KIND_WHOAMI = PORT_NET_KIND_RELAY + 7,
    KIND_YOUARE
};

/* HELLO's detail. */
enum {
    HELLO_HOST = 0x01,
    HELLO_GUEST = 0x02,
    HELLO_RESET = 0x04,
    HELLO_PUNCHED4 = 0x08,
    HELLO_PUNCHED6 = 0x10,
};

/* STATE's detail. */
enum {
    STATE_WAITING = 1,
    STATE_PASSWORD = 2,
    STATE_PUNCH = 3,
    STATE_DIRECT4 = 4,
    STATE_DIRECT6 = 5,
    STATE_RELAYED = 6,
    STATE_NO_ROOM = 0x81,
    STATE_ROOM_FULL = 0x82,
    STATE_SERVER_FULL = 0x83,
    STATE_VERSION = 0x84,
    STATE_WRONG_PASSWORD = 0x85,
    STATE_RATE_LIMITED = 0x86,
    STATE_BAD_REQUEST = 0x87,
    STATE_INCOMPATIBLE = 0x88,
};

/* A candidate's type. */
enum {
    CANDIDATE_PUBLIC = 0,
    CANDIDATE_LAN = 1,
    CANDIDATE_MAPPED = 2,
    CANDIDATE_GLOBAL6 = 3,
};

/* HELLO's fields. */
enum {
    HELLO_CODE = PORT_NET_HEADER_BYTES,
    HELLO_NAME = HELLO_CODE + CODE_FIELD,
    HELLO_PLAYER = HELLO_NAME + NAME_BYTES,
    HELLO_SETUP = HELLO_PLAYER + PLAYER_BYTES,
    HELLO_SALT = HELLO_SETUP + SETUP_BYTES,
    HELLO_SECRET = HELLO_SALT + SALT_BYTES,
    HELLO_COUNT = HELLO_SECRET + KEY_BYTES,
    HELLO_CANDIDATES = HELLO_COUNT + WORD_BYTES,
};

/* STATE's fields. */
enum {
    STATE_CODE = PORT_NET_HEADER_BYTES,
    STATE_YOU = STATE_CODE + CODE_FIELD,
    STATE_PEER = STATE_YOU + ADDRESS_FIELD,
    STATE_SALT = STATE_PEER + ADDRESS_FIELD,
    STATE_CHALLENGE = STATE_SALT + SALT_BYTES,
    STATE_COUNT = STATE_CHALLENGE + CHALLENGE_BYTES,
    STATE_CANDIDATES = STATE_COUNT + WORD_BYTES,
    STATE_PLAYER = STATE_CANDIDATES + CANDIDATES_MAX * ADDRESS_FIELD,
    STATE_SETUP = STATE_PLAYER + PLAYER_BYTES,
};

/* A room of ROOMS. */
enum {
    ROOM_CODE = 0,
    ROOM_NAME = ROOM_CODE + CODE_FIELD,
    ROOM_PLAYER = ROOM_NAME + NAME_BYTES,
    ROOM_SETUP = ROOM_PLAYER + PLAYER_BYTES,
    ROOM_PLAYERS = ROOM_SETUP + SETUP_BYTES,
    ROOM_LOCKED = ROOM_PLAYERS + 1,
    ROOM_SALT = ROOM_LOCKED + 3,
    ROOM_AGE = ROOM_SALT + SALT_BYTES,
};

typedef struct {
    s32 type;
    PortNetAddress at;
} RelayCandidate;

static struct {
    s32 is_on;
    PortNetAddress server;
    u32 paths; /* PORT_NET_PATH_... allowed */
    char code[PORT_RELAY_CODE_BYTES];
    char room[NAME_BYTES + 1];
    char player[PLAYER_BYTES + 1];
    char password[PORT_RELAY_PASSWORD_BYTES];
    u8 salt[SALT_BYTES];
    u8 key[KEY_BYTES]; /* SHA-256(salt || password) */
    s32 has_key;
    s32 has_mapped;
    PortNetAddress mapped;
} g_relay;

/* ---- SHA-256 (FIPS 180-4) and HMAC (RFC 2104) ---- */

#define SHA_BLOCK 64
#define SHA_ROUNDS 64
#define SHA_WORDS 8 /* the hash's state, a..h */
#define SHA_BLOCK_WORDS 16
#define SHA_WORD_BITS 32
#define SHA_LENGTH_BYTES 8
/* The bit that ends a message, and the pads of HMAC's two passes. */
#define SHA_END_BIT 0x80
#define HMAC_INNER 0x36
#define HMAC_OUTER 0x5C

/* The state's words by name (FIPS 180-4, 6.2.2). */
enum { SHA_A, SHA_B, SHA_C, SHA_D, SHA_E, SHA_F, SHA_G, SHA_H };

/* The turns of the message's schedule (sigma) and of a round (Sigma). */
enum {
    SCHEDULE0_TURN1 = 7,
    SCHEDULE0_TURN2 = 18,
    SCHEDULE0_SHIFT = 3,
    SCHEDULE1_TURN1 = 17,
    SCHEDULE1_TURN2 = 19,
    SCHEDULE1_SHIFT = 10,
    ROUND_A_TURN1 = 2,
    ROUND_A_TURN2 = 13,
    ROUND_A_TURN3 = 22,
    ROUND_E_TURN1 = 6,
    ROUND_E_TURN2 = 11,
    ROUND_E_TURN3 = 25,
};

/* The words of the schedule a word is made of: 16, 15, 7 and 2 before it. */
enum {
    SCHEDULE_BACK_16 = 16,
    SCHEDULE_BACK_15 = 15,
    SCHEDULE_BACK_7 = 7,
    SCHEDULE_BACK_2 = 2,
};

typedef struct {
    u32 state[SHA_WORDS];
    u8 block[SHA_BLOCK];
    u32 used;
    u32 length; /* bytes: the messages here are short */
} Sha256;

/* The rounds' constants and the hash's first state (FIPS 180-4, 4.2.2
 * and 5.3.3). */
static const u32 g_sha_k[SHA_ROUNDS] = {
    0x428A2F98, 0x71374491, 0xB5C0FBCF, 0xE9B5DBA5, 0x3956C25B, 0x59F111F1,
    0x923F82A4, 0xAB1C5ED5, 0xD807AA98, 0x12835B01, 0x243185BE, 0x550C7DC3,
    0x72BE5D74, 0x80DEB1FE, 0x9BDC06A7, 0xC19BF174, 0xE49B69C1, 0xEFBE4786,
    0x0FC19DC6, 0x240CA1CC, 0x2DE92C6F, 0x4A7484AA, 0x5CB0A9DC, 0x76F988DA,
    0x983E5152, 0xA831C66D, 0xB00327C8, 0xBF597FC7, 0xC6E00BF3, 0xD5A79147,
    0x06CA6351, 0x14292967, 0x27B70A85, 0x2E1B2138, 0x4D2C6DFC, 0x53380D13,
    0x650A7354, 0x766A0ABB, 0x81C2C92E, 0x92722C85, 0xA2BFE8A1, 0xA81A664B,
    0xC24B8B70, 0xC76C51A3, 0xD192E819, 0xD6990624, 0xF40E3585, 0x106AA070,
    0x19A4C116, 0x1E376C08, 0x2748774C, 0x34B0BCB5, 0x391C0CB3, 0x4ED8AA4A,
    0x5B9CCA4F, 0x682E6FF3, 0x748F82EE, 0x78A5636F, 0x84C87814, 0x8CC70208,
    0x90BEFFFA, 0xA4506CEB, 0xBEF9A3F7, 0xC67178F2};
static const u32 g_sha_start[SHA_WORDS] = {
    0x6A09E667, 0xBB67AE85, 0x3C6EF372, 0xA54FF53A,
    0x510E527F, 0x9B05688C, 0x1F83D9AB, 0x5BE0CD19};

/* A word turned right by `count` bits. */
static u32 port_ShaTurn(u32 word, s32 count) {
    return (word >> count) | (word << (SHA_WORD_BITS - count));
}

/* A word of the block, its most significant byte first. */
static u32 port_ShaWordOf(const u8 *bytes) {
    u32 word = 0;
    s32 i;

    for (i = 0; i < WORD_BYTES; i++) {
        word = (word << BITS_PER_BYTE) | bytes[i];
    }
    return word;
}

/* One block of 64 bytes into the hash's state. */
static void port_ShaBlock(Sha256 *sha) {
    u32 schedule[SHA_ROUNDS];
    u32 work[SHA_WORDS];
    s32 i;

    for (i = 0; i < SHA_BLOCK_WORDS; i++) {
        schedule[i] = port_ShaWordOf(sha->block + WORD_BYTES * i);
    }
    for (; i < SHA_ROUNDS; i++) {
        u32 back15 = schedule[i - SCHEDULE_BACK_15];
        u32 back2 = schedule[i - SCHEDULE_BACK_2];
        u32 sigma0 =
            port_ShaTurn(back15, SCHEDULE0_TURN1) ^
            port_ShaTurn(back15, SCHEDULE0_TURN2) ^ (back15 >> SCHEDULE0_SHIFT);
        u32 sigma1 =
            port_ShaTurn(back2, SCHEDULE1_TURN1) ^
            port_ShaTurn(back2, SCHEDULE1_TURN2) ^ (back2 >> SCHEDULE1_SHIFT);

        schedule[i] = schedule[i - SCHEDULE_BACK_16] + sigma0 +
                      schedule[i - SCHEDULE_BACK_7] + sigma1;
    }
    for (i = 0; i < SHA_WORDS; i++) {
        work[i] = sha->state[i];
    }
    for (i = 0; i < SHA_ROUNDS; i++) {
        u32 big_e = port_ShaTurn(work[SHA_E], ROUND_E_TURN1) ^
                    port_ShaTurn(work[SHA_E], ROUND_E_TURN2) ^
                    port_ShaTurn(work[SHA_E], ROUND_E_TURN3);
        u32 choose = (work[SHA_E] & work[SHA_F]) ^ (~work[SHA_E] & work[SHA_G]);
        u32 first = work[SHA_H] + big_e + choose + g_sha_k[i] + schedule[i];
        u32 big_a = port_ShaTurn(work[SHA_A], ROUND_A_TURN1) ^
                    port_ShaTurn(work[SHA_A], ROUND_A_TURN2) ^
                    port_ShaTurn(work[SHA_A], ROUND_A_TURN3);
        u32 major = (work[SHA_A] & work[SHA_B]) ^ (work[SHA_A] & work[SHA_C]) ^
                    (work[SHA_B] & work[SHA_C]);
        s32 k;

        for (k = SHA_H; k > SHA_A; k--) {
            work[k] = work[k - 1];
        }
        work[SHA_E] += first;
        work[SHA_A] = first + big_a + major;
    }
    for (i = 0; i < SHA_WORDS; i++) {
        sha->state[i] += work[i];
    }
}

/* A hash begun. */
static void port_ShaStart(Sha256 *sha) {
    s32 i;

    for (i = 0; i < SHA_WORDS; i++) {
        sha->state[i] = g_sha_start[i];
    }
    sha->used = 0;
    sha->length = 0;
}

/* `count` bytes more of the message. */
static void port_ShaAdd(Sha256 *sha, const u8 *bytes, u32 count) {
    u32 i;

    for (i = 0; i < count; i++) {
        sha->block[sha->used++] = bytes[i];
        sha->length++;
        if (sha->used == SHA_BLOCK) {
            port_ShaBlock(sha);
            sha->used = 0;
        }
    }
}

/* The message's end, its length in bits, and the hash into `out`
 * (KEY_BYTES), its most significant byte first. */
static void port_ShaEnd(Sha256 *sha, u8 *out) {
    u64 length = (u64)sha->length * BITS_PER_BYTE;
    u8 end[SHA_LENGTH_BYTES];
    u8 one = SHA_END_BIT;
    u8 zero = 0;
    s32 i;

    port_ShaAdd(sha, &one, 1);
    while (sha->used != SHA_BLOCK - SHA_LENGTH_BYTES) {
        port_ShaAdd(sha, &zero, 1);
    }
    for (i = 0; i < SHA_LENGTH_BYTES; i++) {
        end[i] = (u8)(length >> (BITS_PER_BYTE * (SHA_LENGTH_BYTES - 1 - i)));
    }
    port_ShaAdd(sha, end, sizeof(end));
    for (i = 0; i < KEY_BYTES; i++) {
        out[i] = (u8)(sha->state[i / WORD_BYTES] >>
                      (BITS_PER_BYTE * (WORD_BYTES - 1 - i % WORD_BYTES)));
    }
}

/* HMAC-SHA256 of `message` with a key of KEY_BYTES. */
static void port_Hmac(const u8 *key, const u8 *message, u32 count, u8 *out) {
    u8 pad[SHA_BLOCK];
    u8 inner[KEY_BYTES];
    Sha256 sha;
    s32 i;

    for (i = 0; i < SHA_BLOCK; i++) {
        pad[i] = (u8)((i < KEY_BYTES ? key[i] : 0) ^ HMAC_INNER);
    }
    port_ShaStart(&sha);
    port_ShaAdd(&sha, pad, SHA_BLOCK);
    port_ShaAdd(&sha, message, count);
    port_ShaEnd(&sha, inner);
    for (i = 0; i < SHA_BLOCK; i++) {
        pad[i] = (u8)((i < KEY_BYTES ? key[i] : 0) ^ HMAC_OUTER);
    }
    port_ShaStart(&sha);
    port_ShaAdd(&sha, pad, SHA_BLOCK);
    port_ShaAdd(&sha, inner, KEY_BYTES);
    port_ShaEnd(&sha, out);
}

void port_RelayRoomKey(const u8 *salt, const char *password, u8 *key) {
    Sha256 sha;

    port_ShaStart(&sha);
    port_ShaAdd(&sha, salt, SALT_BYTES);
    port_ShaAdd(&sha, (const u8 *)password, port_TextLength(password));
    port_ShaEnd(&sha, key);
}

/* printf's work into `out`. */
static void port_RelayText(char *out, u32 room, const char *format, ...) {
    PortArgs args;

    PORT_ARGS_START(args, format);
    port_FormatText(out, room, format, args);
    PORT_ARGS_END(args);
}

/* ---- the settings ---- */

static void port_CopyText(char *out, u32 room, const char *text) {
    u32 i;

    for (i = 0; text != NULL && text[i] != 0 && i + 1 < room; i++) {
        out[i] = text[i];
    }
    out[i] = 0;
}

/* The lobby to meet at (port_net.h); a code typed in small letters is the
 * same room. */
void port_SetRelay(const PortNetAddress *server, const char *code, u32 paths) {
    u32 i;

    g_relay.is_on = 1;
    g_relay.server = *server;
    g_relay.paths = paths != 0 ? paths : PORT_NET_PATHS_ALL;
    port_CopyText(g_relay.code, sizeof(g_relay.code), code);
    for (i = 0; g_relay.code[i] != 0; i++) {
        if (g_relay.code[i] >= 'a' && g_relay.code[i] <= 'z') {
            g_relay.code[i] = (char)(g_relay.code[i] - 'a' + 'A');
        }
    }
}

void port_SetRelayRoom(const char *name, const char *player) {
    port_CopyText(g_relay.room, sizeof(g_relay.room), name);
    port_CopyText(g_relay.player, sizeof(g_relay.player), player);
}

void port_SetRelayPassword(const char *password) {
    port_CopyText(g_relay.password, sizeof(g_relay.password), password);
}

void port_SetRelayKey(const u8 *salt, const u8 *key) {
    u32 i;

    for (i = 0; i < SALT_BYTES; i++) {
        g_relay.salt[i] = salt != NULL ? salt[i] : 0;
    }
    for (i = 0; i < KEY_BYTES; i++) {
        g_relay.key[i] = key[i];
    }
    g_relay.has_key = 1;
}

/* A hex digit's value, or -1. */
static s32 port_HexValue(char digit) {
    if (digit >= '0' && digit <= '9') {
        return digit - '0';
    }
    if (digit >= 'a' && digit <= 'f') {
        return digit - 'a' + HEX_LETTER_VALUE;
    }
    if (digit >= 'A' && digit <= 'F') {
        return digit - 'A' + HEX_LETTER_VALUE;
    }
    return -1;
}

/* `count` bytes of hex into `out`: 1, or 0 if they are not. */
static s32 port_ParseHex(const char *text, u8 *out, u32 count) {
    u32 i;

    for (i = 0; i < count; i++) {
        s32 high = port_HexValue(text[2 * i]);
        s32 low = high < 0 ? -1 : port_HexValue(text[2 * i + 1]);

        if (low < 0) {
            return 0;
        }
        out[i] = (u8)((high << HEX_DIGIT_BITS) | low);
    }
    return 1;
}

s32 port_ParseRoomKey(const char *text, u8 *salt, u8 *key) {
    return port_TextLength(text) == 2 * SALT_BYTES + 1 + 2 * KEY_BYTES &&
           text[2 * SALT_BYTES] == ':' &&
           port_ParseHex(text, salt, SALT_BYTES) &&
           port_ParseHex(text + 2 * SALT_BYTES + 1, key, KEY_BYTES);
}

void port_SetRelayMapped(const PortNetAddress *mapped) {
    g_relay.mapped = *mapped;
    g_relay.has_mapped = 1;
}

s32 port_IsRelayOn(void) { return g_relay.is_on; }

void port_ResetRelay(void) {
    u32 i;

    for (i = 0; i < sizeof(g_relay); i++) {
        ((u8 *)&g_relay)[i] = 0;
    }
}

/* --net-paths (port_net.h): each word of the list a bit. */
u32 port_ParseNetPaths(const char *text) {
    static const char * const words[] = {
        "public", "lan", "mapped", "v6", "relay"};
    u32 paths = 0;

    while (*text != 0) {
        u32 length = 0;
        u32 word;

        while (text[length] != 0 && text[length] != ',') {
            length++;
        }
        for (word = 0; word < sizeof(words) / sizeof(words[0]); word++) {
            u32 i;

            for (i = 0; i < length && words[word][i] == text[i]; i++) {
            }
            if (i == length && words[word][i] == 0) {
                paths |= 1U << word;
                break;
            }
        }
        if (word == sizeof(words) / sizeof(words[0])) {
            return 0;
        }
        text += length;
        if (*text == ',') {
            text++;
        }
    }
    return paths;
}

const char *port_GetRelayCode(void) { return g_relay.code; }

/* ---- the datagrams ---- */

static void port_PutText(u8 *out, u32 room, const char *text) {
    u32 i;

    for (i = 0; i < room; i++) {
        out[i] = 0;
    }
    for (i = 0; text != NULL && text[i] != 0 && i < room; i++) {
        out[i] = (u8)(text[i] >= ' ' && text[i] <= '~' ? text[i] : '?');
    }
}

static void port_GetText(char *out, const u8 *in, u32 room) {
    u32 i;

    for (i = 0; i < room && in[i] != 0; i++) {
        out[i] = (char)(in[i] >= ' ' && in[i] <= '~' ? in[i] : '?');
    }
    out[i] = 0;
}

/* An ADDRESS or a CANDIDATE (20 bytes) of `at`, its first byte `type`. */
static void port_PutAddress(u8 *out, s32 type, const PortNetAddress *at) {
    u32 i;

    for (i = 0; i < ADDRESS_FIELD; i++) {
        out[i] = 0;
    }
    out[0] = (u8)type;
    out[2] = (u8)at->port;
    out[3] = (u8)(at->port >> BITS_PER_BYTE);
    for (i = 0; i < PORT_NET_V6_BYTES; i++) {
        out[WORD_BYTES + i] =
            at->is_v6 ? at->v6[i]
            : i < V4_BYTES
                ? (u8)(at->address >> (BITS_PER_BYTE * (V4_BYTES - 1 - i)))
                : 0;
    }
}

/* An ADDRESS (family 4 or 6) or a CANDIDATE (its type gives the family):
 * 1, or 0 if it is none. */
static s32 port_GetAddress(const u8 *in, s32 is_v6, PortNetAddress *at) {
    u32 i;

    port_ClearNetAddress(at);
    at->port = (u16)(in[2] | (in[3] << BITS_PER_BYTE));
    at->is_v6 = (u8)is_v6;
    for (i = 0; i < PORT_NET_V6_BYTES; i++) {
        if (is_v6) {
            at->v6[i] = in[WORD_BYTES + i];
        } else if (i < V4_BYTES) {
            at->address = (at->address << BITS_PER_BYTE) | in[WORD_BYTES + i];
        }
    }
    return at->port != 0;
}

/* The server's datagram is from the server. */
static s32 port_IsServer(const PortNetAddress *from) {
    return port_NetAddressesEqual(from, &g_relay.server);
}

/* What the meeting knows. */
static struct {
    s32 state;
    u32 token;
    u8 challenge[CHALLENGE_BYTES];
    s32 has_challenge;
    RelayCandidate peer[CANDIDATES_MAX];
    s32 peer_count;
    s32 punched4;
    s32 punched6;
    PortNetAddress via4; /* where the other's answer came from */
    PortNetAddress via6;
    s32 via4_type;
    u32 rtt4;
    u32 rtt6;
    char peer_name[PLAYER_BYTES + 1];
    char peer_setup[SETUP_BYTES + 1];
} g_meet;

/* This program's own candidates: its address on its network and its
 * IPv6 address where its socket is of every address, and the port the
 * window's router mapped. */
static s32 port_OwnCandidates(RelayCandidate *out) {
    PortNetAddress bound;
    PortNetAddress own;
    s32 count = 0;
    s32 any;
    u32 i;

    if (!port_GetNetBound(&bound)) {
        return 0;
    }
    any = !bound.is_v6 ? bound.address == 0 : 1;
    for (i = 0; bound.is_v6 && i < PORT_NET_V6_BYTES; i++) {
        any = any && bound.v6[i] == 0;
    }
    if (any && (g_relay.paths & PORT_NET_PATH_LAN) &&
        port_HostOwnAddress(0, &own)) {
        own.port = bound.port;
        out[count].type = CANDIDATE_LAN;
        out[count++].at = own;
    }
    if (g_relay.has_mapped && (g_relay.paths & PORT_NET_PATH_MAPPED)) {
        out[count].type = CANDIDATE_MAPPED;
        out[count++].at = g_relay.mapped;
    }
    /* a global unicast address, 2000::/3 */
    if (any && bound.is_v6 && (g_relay.paths & PORT_NET_PATH_V6) &&
        port_HostOwnAddress(1, &own) && own.is_v6 &&
        (own.v6[0] & 0xE0) == 0x20) {
        own.port = bound.port;
        out[count].type = CANDIDATE_GLOBAL6;
        out[count++].at = own;
    }
    return count;
}

/* A HELLO to the server: who this is, the room, the password's salt and
 * verifier (the host) or its proof (a guest), and this side's ways. */
static void port_SendHello(s32 role, s32 reset) {
    u8 out[HELLO_BYTES];
    RelayCandidate own[CANDIDATES_MAX];
    char setup[PORT_SETUP_CODE_BYTES];
    s32 count = port_OwnCandidates(own);
    s32 detail = role == PORT_SESSION_HOST ? HELLO_HOST : HELLO_GUEST;
    s32 i;

    if (reset) {
        detail |= HELLO_RESET;
    }
    if (g_meet.punched4) {
        detail |= HELLO_PUNCHED4;
    }
    if (g_meet.punched6) {
        detail |= HELLO_PUNCHED6;
    }
    for (i = 0; i < HELLO_BYTES; i++) {
        out[i] = 0;
    }
    port_NetHeader(out, KIND_HELLO, detail, RELAY_VERSION);
    port_PutText(out + HELLO_CODE, CODE_FIELD, g_relay.code);
    port_PutText(out + HELLO_NAME, NAME_BYTES, g_relay.room);
    port_PutText(out + HELLO_PLAYER, PLAYER_BYTES, g_relay.player);
    port_SessionSetupCode(setup);
    port_PutText(out + HELLO_SETUP, SETUP_BYTES, setup);
    if (role == PORT_SESSION_HOST && g_relay.has_key) {
        for (i = 0; i < SALT_BYTES; i++) {
            out[HELLO_SALT + i] = g_relay.salt[i];
        }
        for (i = 0; i < KEY_BYTES; i++) {
            out[HELLO_SECRET + i] = g_relay.key[i];
        }
    } else if (
        role != PORT_SESSION_HOST && g_relay.has_key && g_meet.has_challenge) {
        port_Hmac(
            g_relay.key, g_meet.challenge, CHALLENGE_BYTES, out + HELLO_SECRET);
    }
    out[HELLO_COUNT] = (u8)count;
    for (i = 0; i < count; i++) {
        port_PutAddress(out + HELLO_CANDIDATES + i * ADDRESS_FIELD, own[i].type,
                        &own[i].at);
    }
    port_NetSendTo(&g_relay.server, out, sizeof(out));
}

/* Whether a candidate of the other's is one this program tries. */
static s32 port_TriesCandidate(const RelayCandidate *candidate) {
    static const u32 path_of[] = {PORT_NET_PATH_PUBLIC, PORT_NET_PATH_LAN,
                                  PORT_NET_PATH_MAPPED, PORT_NET_PATH_V6};

    if (candidate->at.is_v6 && !port_NetIsV6()) {
        return 0;
    }
    if (candidate->at.is_v6 && candidate->type == CANDIDATE_PUBLIC) {
        return (g_relay.paths & PORT_NET_PATH_V6) != 0;
    }
    return (g_relay.paths & path_of[candidate->type]) != 0;
}

/* A punch to each of the other's candidates this program tries. */
static void port_SendPunches(void) {
    u8 out[PUNCH_BYTES];
    s32 i;

    for (i = 0; i < g_meet.peer_count; i++) {
        if (!port_TriesCandidate(&g_meet.peer[i])) {
            continue;
        }
        port_NetHeader(out, KIND_PUNCH, i, g_meet.token);
        port_NetPutWord(out + PORT_NET_HEADER_BYTES, port_NetClock());
        port_NetPutWord(out + PORT_NET_HEADER_BYTES + WORD_BYTES, 0);
        port_NetSendTo(&g_meet.peer[i].at, out, sizeof(out));
    }
}

/* Which of the two setup codes' parts differ, in words. */
static const char *port_SetupDiffers(const char *mine, const char *theirs) {
    static const char * const parts[] = {
        "the online protocol (another release)",
        "the build (another release, or another system)",
        "the disc image (the USA disc, SLUS-00663, is needed)",
        "the gameplay mods"};
    s32 part = 0;
    s32 i;

    for (i = 0; mine[i] != 0 || theirs[i] != 0; i++) {
        if (mine[i] == '-') {
            part++;
        }
        if (mine[i] != theirs[i]) {
            return parts[part < 4 ? part : 3];
        }
    }
    return "nothing that the codes tell";
}

/* The server's error in a player's words, into `out`. */
static void port_RelayError(char *out, u32 room, s32 state) {
    char mine[PORT_SETUP_CODE_BYTES];

    port_SessionSetupCode(mine);
    switch (state) {
    case STATE_NO_ROOM:
        port_RelayText(out, room,
                       "There is no room %s at the lobby: it may have "
                       "closed.",
                       g_relay.code);
        break;
    case STATE_ROOM_FULL:
        port_RelayText(
            out, room, "Room %s already has two players.", g_relay.code);
        break;
    case STATE_SERVER_FULL:
        port_RelayText(out, room,
                       "The lobby is full just now: try again "
                       "in a while.");
        break;
    case STATE_VERSION:
        port_RelayText(out, room,
                       "The lobby server is of another version: "
                       "this release cannot use it.");
        break;
    case STATE_WRONG_PASSWORD:
        port_RelayText(
            out, room, "The password is not room %s's.", g_relay.code);
        break;
    case STATE_RATE_LIMITED:
        port_RelayText(out, room,
                       "The lobby has had too many tries from "
                       "your address: wait a minute and try "
                       "again.");
        break;
    case STATE_INCOMPATIBLE:
        port_RelayText(out, room,
                       "Room %s's game is not compatible with yours: "
                       "the host's setup code is %s, yours %s; they "
                       "differ in %s.",
                       g_relay.code, g_meet.peer_setup, mine,
                       port_SetupDiffers(mine, g_meet.peer_setup));
        break;
    default:
        port_RelayText(out, room,
                       "The lobby could not take this request "
                       "(%d).",
                       state);
        break;
    }
}

/* What the way found is called, with its round trip. */
static void port_NamePath(s32 state) {
    char text[STATUS_BYTES];

    if (state == STATE_DIRECT6) {
        port_RelayText(text, sizeof(text), "IPv6, direct (%u ms)", g_meet.rtt6);
    } else if (state == STATE_DIRECT4) {
        port_RelayText(
            text, sizeof(text), "%s (%u ms)",
            g_meet.via4_type == CANDIDATE_MAPPED ? "the host's mapped port"
            : g_meet.via4_type == CANDIDATE_LAN  ? "the local network"
                                                 : "a punched hole",
            g_meet.rtt4);
    } else {
        port_RelayText(text, sizeof(text), "the lobby's relay");
    }
    port_SetSessionPath(text);
}

/* The meeting at the lobby (port_net.h): HELLO until the server decides
 * the way, punches meanwhile, and the answers to the other's. */
s32 port_RelayMeet(s32 role, s32 again) {
    char status[STATUS_BYTES];
    char why[TEXT_BYTES];
    char server[PORT_NET_ADDRESS_TEXT_BYTES];
    u8 in[PORT_NET_DATAGRAM_MAX];
    u32 start = port_NetClock();
    u32 heard_at = start;
    u32 hello_at = 0;
    u32 punch_at = 0;
    /* a host's first hello lets go of any guest the lobby still has in a
     * room of this address: one of a session that has ended in the
     * running program (online.c), whose room the lobby keeps a while */
    s32 resetting = again || role == PORT_SESSION_HOST;
    s32 told_code = 0;
    u32 i;

    for (i = 0; i < sizeof(g_meet); i++) {
        ((u8 *)&g_meet)[i] = 0;
    }
    port_SetSessionPhase(PORT_SESSION_PHASE_LOBBY_SERVER);
    port_FormatNetAddress(&g_relay.server, server, sizeof(server));
    port_RelayText(
        status, sizeof(status), "Reaching the lobby server at %s", server);
    if (role == PORT_SESSION_HOST && !again) {
        g_relay.code[0] = 0;
    }
    if (role == PORT_SESSION_HOST && !g_relay.has_key &&
        g_relay.password[0] != 0) {
        /* the room's salt: it only has to differ between rooms */
        u32 seed = port_HostTicks() ^ (port_NetClock() << SALT_CLOCK_SHIFT) ^
                   SALT_SEED;

        for (i = 0; i < SALT_BYTES; i++) {
            seed = seed * SALT_MULTIPLY + SALT_ADD;
            g_relay.salt[i] = (u8)(seed >> SALT_BYTE_SHIFT);
        }
        port_RelayRoomKey(g_relay.salt, g_relay.password, g_relay.key);
        g_relay.has_key = 1;
    }
    for (;;) {
        u32 now = port_NetClock();
        PortNetAddress from;
        s32 got;

        if (port_SessionIsCanceled()) {
            port_Log("relay: the meeting was given up\n");
            return 0;
        }
        if (now - hello_at >= HELLO_EVERY_MS) {
            port_SendHello(role, resetting);
            hello_at = now;
        }
        if (g_meet.state == STATE_PUNCH && now - punch_at >= PUNCH_EVERY_MS) {
            port_SendPunches();
            punch_at = now;
        }
        got = port_NetReceiveFrom(in, sizeof(in), POLL_MS, &from);
        if (got > 0) {
            s32 detail;
            u32 number;
            s32 kind = port_NetReadHeader(in, got, &detail, &number);

            if (kind == KIND_STATE && got == STATE_BYTES &&
                port_IsServer(&from)) {
                heard_at = now;
                port_GetText(g_meet.peer_setup, in + STATE_SETUP, SETUP_BYTES);
                if (detail >= STATE_NO_ROOM) {
                    port_RelayError(why, sizeof(why), detail);
                    port_Log("relay: the lobby said no (%d)\n", detail);
                    port_SessionFail(why);
                    return 0;
                }
                if (role == PORT_SESSION_HOST) {
                    port_GetText(g_relay.code, in + STATE_CODE, CODE_FIELD - 2);
                }
                if (detail == STATE_WAITING) {
                    resetting = 0;
                    port_SetSessionPhase(
                        role == PORT_SESSION_HOST ? PORT_SESSION_PHASE_WAITING
                                                  : PORT_SESSION_PHASE_JOINING);
                    if (!told_code && g_relay.code[0] != 0) {
                        told_code = 1;
                        port_Log(
                            "relay: the room's code is %s\n", g_relay.code);
                    }
                    port_RelayText(
                        status, sizeof(status),
                        "Room %s is open at the lobby: waiting "
                        "for the other player (they pick it in "
                        "Browse rooms, or join code %s)",
                        g_relay.room[0] != 0 ? g_relay.room : g_relay.code,
                        g_relay.code);
                } else if (detail == STATE_PASSWORD) {
                    for (i = 0; i < CHALLENGE_BYTES; i++) {
                        g_meet.challenge[i] = in[STATE_CHALLENGE + i];
                    }
                    if (!g_relay.has_key && g_relay.password[0] != 0) {
                        port_RelayRoomKey(
                            in + STATE_SALT, g_relay.password, g_relay.key);
                        g_relay.has_key = 1;
                    }
                    if (!g_relay.has_key) {
                        port_RelayText(why, sizeof(why),
                                       "Room %s is locked: it needs its "
                                       "password.",
                                       g_relay.code);
                        port_SessionFail(why);
                        return 0;
                    }
                    if (!g_meet.has_challenge) {
                        g_meet.has_challenge = 1;
                        hello_at = 0; /* the proof at once */
                    }
                } else if (
                    detail == STATE_PUNCH && g_meet.state != STATE_PUNCH) {
                    g_meet.state = STATE_PUNCH;
                    g_meet.token = number;
                    g_meet.peer_count = in[STATE_COUNT] < CANDIDATES_MAX
                                            ? in[STATE_COUNT]
                                            : CANDIDATES_MAX;
                    for (i = 0; i < (u32)g_meet.peer_count; i++) {
                        const u8 *at =
                            in + STATE_CANDIDATES + i * ADDRESS_FIELD;
                        s32 type = at[0];

                        g_meet.peer[i].type = type;
                        if (type > CANDIDATE_GLOBAL6 ||
                            !port_GetAddress(at,
                                             type == CANDIDATE_GLOBAL6 ||
                                                 (type == CANDIDATE_PUBLIC &&
                                                  in[STATE_PEER] == FAMILY_V6),
                                             &g_meet.peer[i].at)) {
                            g_meet.peer[i].type = CANDIDATE_PUBLIC;
                            port_ClearNetAddress(&g_meet.peer[i].at);
                        }
                    }
                    port_GetText(
                        g_meet.peer_name, in + STATE_PLAYER, PLAYER_BYTES);
                    port_Log("relay: %s is in the room; trying %d ways to "
                             "them\n",
                             g_meet.peer_name[0] != 0 ? g_meet.peer_name
                                                      : "the other player",
                             g_meet.peer_count);
                    port_RelayText(
                        status, sizeof(status),
                        "%s is here: finding the way between "
                        "your PCs",
                        g_meet.peer_name[0] != 0 ? g_meet.peer_name
                                                 : "The other player");
                    punch_at = 0;
                } else if (detail == STATE_DIRECT6 && g_meet.punched6) {
                    port_SetNetPeerNow(&g_meet.via6);
                    port_NamePath(detail);
                    port_Log("relay: the way is %s\n", port_GetSessionPath());
                    return 1;
                } else if (detail == STATE_DIRECT4 && g_meet.punched4) {
                    port_SetNetPeerNow(&g_meet.via4);
                    port_NamePath(detail);
                    port_Log("relay: the way is %s\n", port_GetSessionPath());
                    return 1;
                } else if (detail == STATE_RELAYED) {
                    if (!(g_relay.paths & PORT_NET_PATH_RELAY)) {
                        port_SessionFail("No way between your PCs was found, "
                                         "and the relay is not allowed "
                                         "(--net-paths).");
                        return 0;
                    }
                    port_SetNetPeerNow(&g_relay.server);
                    port_NamePath(detail);
                    port_Log("relay: no hole could be punched: the lobby's "
                             "server passes the datagrams on\n");
                    return 1;
                }
            } else if (kind == KIND_PUNCH && got == PUNCH_BYTES &&
                       g_meet.state == STATE_PUNCH && number == g_meet.token &&
                       !port_IsServer(&from)) {
                in[2] = (u8)KIND_PUNCH_ACK;
                port_NetSendTo(&from, in, PUNCH_BYTES);
            } else if (kind == KIND_PUNCH_ACK && got == PUNCH_BYTES &&
                       g_meet.state == STATE_PUNCH && number == g_meet.token &&
                       !port_IsServer(&from)) {
                u32 rtt = now - port_NetGetWord(in + PORT_NET_HEADER_BYTES);

                if (from.is_v6 && !g_meet.punched6) {
                    g_meet.punched6 = 1;
                    g_meet.via6 = from;
                    g_meet.rtt6 = rtt;
                    hello_at = 0;
                } else if (!from.is_v6 && !g_meet.punched4) {
                    g_meet.punched4 = 1;
                    g_meet.via4 = from;
                    g_meet.rtt4 = rtt;
                    g_meet.via4_type = detail < g_meet.peer_count
                                           ? g_meet.peer[detail].type
                                           : CANDIDATE_PUBLIC;
                    hello_at = 0;
                }
            }
        }
        if (now - heard_at > SERVER_GIVE_UP_MS) {
            port_RelayText(why, sizeof(why),
                           "The lobby server at %s does not answer. Is "
                           "its address right, and does your network let "
                           "UDP through?",
                           server);
            port_SessionFail(why);
            return 0;
        }
        port_SessionStatus(status);
    }
}

/* ---- the lobby's list, and this PC's address as the server sees it ---- */

/* A socket of its own for asking the server something, out of a session:
 * on this PC alone where the server is, else every address of the kind. */
static s32 port_AskSocket(u16 port) {
    PortNetAddress at;

    port_ClearNetAddress(&at);
    at.port = port;
    at.is_v6 = g_relay.server.is_v6;
    if (!at.is_v6 &&
        (g_relay.server.address >> ADDRESS_FIRST_SHIFT) == LOOPBACK_FIRST) {
        at.address = LOOPBACK;
    } else if (at.is_v6) {
        s32 loopback = g_relay.server.v6[PORT_NET_V6_BYTES - 1] == 1;
        u32 i;

        for (i = 0; i + 1 < PORT_NET_V6_BYTES; i++) {
            loopback = loopback && g_relay.server.v6[i] == 0;
        }
        if (loopback) {
            at.v6[PORT_NET_V6_BYTES - 1] = 1;
        }
    }
    return port_HostUdpOpen(&at);
}

/* The lobby's list (port_net.h), page by page. */
s32 port_RelayListRooms(
    const PortNetAddress *server, PortRoom *rooms, s32 most, s32 *total) {
    u8 out[LIST_BYTES];
    u8 in[PORT_NET_DATAGRAM_MAX];
    s32 socket;
    s32 count = 0;
    s32 asked = -1;
    s32 answered = 0;
    u32 start = port_NetClock();
    u32 sent_at = 0;
    s32 i;

    g_relay.server = *server;
    socket = port_AskSocket(0);
    *total = 0;
    if (socket < 0) {
        return -1;
    }
    while (port_NetClock() - start < LIST_WAIT_MS) {
        PortNetAddress from;
        s32 got;

        if (asked < count && count < most &&
            (asked != count || port_NetClock() - sent_at > HELLO_EVERY_MS)) {
            for (i = 0; i < LIST_BYTES; i++) {
                out[i] = 0;
            }
            port_NetHeader(out, KIND_LIST, 0, (u32)count);
            port_HostUdpSend(socket, server, out, sizeof(out));
            asked = count;
            sent_at = port_NetClock();
        }
        got = port_HostUdpReceive(socket, in, sizeof(in), &from, POLL_MS);
        if (got > 0 && got >= PORT_NET_HEADER_BYTES && in[0] == 'R' &&
            in[1] == 'B' && in[2] == KIND_ROOMS &&
            port_NetAddressesEqual(&from, server)) {
            s32 listed = in[3];

            if (got != PORT_NET_HEADER_BYTES + listed * ROOM_BYTES) {
                continue;
            }
            answered = 1;
            *total = (s32)port_NetGetWord(in + 4);
            for (i = 0; i < listed && count < most; i++) {
                const u8 *at = in + PORT_NET_HEADER_BYTES + i * ROOM_BYTES;
                PortRoom *room = &rooms[count++];
                s32 k;

                port_GetText(room->code, at + ROOM_CODE, CODE_FIELD - 2);
                port_GetText(room->name, at + ROOM_NAME, NAME_BYTES);
                port_GetText(room->player, at + ROOM_PLAYER, PLAYER_BYTES);
                port_GetText(room->setup, at + ROOM_SETUP, SETUP_BYTES);
                room->players = at[ROOM_PLAYERS];
                room->locked = at[ROOM_LOCKED];
                for (k = 0; k < SALT_BYTES; k++) {
                    room->salt[k] = at[ROOM_SALT + k];
                }
                room->age = port_NetGetWord(at + ROOM_AGE);
            }
            if (count >= *total || listed == 0) {
                break;
            }
        }
    }
    port_HostUdpClose(socket);
    return answered ? count : -1;
}

/* WHOAMI until YOUARE comes (port_net.h). */
s32 port_RelayWhoAmI(
    const PortNetAddress *server, u16 port, PortNetAddress *out) {
    u8 ask[WHOAMI_BYTES];
    u8 in[PORT_NET_DATAGRAM_MAX];
    u32 start = port_NetClock();
    u32 sent_at = 0;
    s32 socket;
    s32 found = 0;
    s32 i;

    g_relay.server = *server;
    socket = port_AskSocket(port);
    if (socket < 0) {
        return 0;
    }
    for (i = 0; i < WHOAMI_BYTES; i++) {
        ask[i] = 0;
    }
    port_NetHeader(ask, KIND_WHOAMI, 0, 0);
    while (!found && port_NetClock() - start < LIST_WAIT_MS) {
        PortNetAddress from;
        s32 got;

        if (sent_at == 0 || port_NetClock() - sent_at > HELLO_EVERY_MS) {
            port_HostUdpSend(socket, server, ask, sizeof(ask));
            sent_at = port_NetClock();
        }
        got = port_HostUdpReceive(socket, in, sizeof(in), &from, POLL_MS);
        if (got == YOUARE_BYTES && in[0] == 'R' && in[1] == 'B' &&
            in[2] == KIND_YOUARE && port_NetAddressesEqual(&from, server)) {
            found =
                port_GetAddress(in + PORT_NET_HEADER_BYTES,
                                in[PORT_NET_HEADER_BYTES] == FAMILY_V6, out);
        }
    }
    port_HostUdpClose(socket);
    return found;
}
