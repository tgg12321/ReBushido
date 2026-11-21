/* The ranked ladder's client (port_ranked.h; docs/NETPLAY.md, section 17).
 * Outside the state, like the lobby's client (relay.c): nothing here is
 * the game's.
 *
 * The identity is a seed of 32 random bytes in a file of its own, from
 * which Monocypher makes the Ed25519 key pair (include/port_monocypher.h);
 * the file is never written over, and a damaged one is said, not replaced.
 * Every word to the lobby is a RANK of 240 bytes; a NAME, a QUEUE and a
 * REPORT are signed over the lobby's nonce, which each RANKED answer
 * gives afresh, so that a signature heard is no good a few minutes on.
 * Each ask is said again until the lobby answers it; while queued or in a
 * match, a STAY says every second that the player is there, which is how
 * the lobby knows who left. The match's digest is BLAKE2b of every frame's
 * two final inputs and of every final state hash before the frame the
 * match was decided at: the same on both machines of a match played to its
 * end, as netplay's hashes are. */
#include "common.h"
#include "port_host.h"
#include "port_monocypher.h"
#include "port_net.h"
#include "port_ranked.h"
#include "port_room.h"
#include "bb2.h"

#define RANK_BYTES 240
#define SIGNED_BYTES 176
#define CHUNK_BYTES 1024
#define UPLOAD_BYTES (SIGNED_BYTES + CHUNK_BYTES)
#define ANSWER_BYTES 168
#define ROW_BYTES 48
#define KEY_BYTES 32
#define NONCE_BYTES 16
#define TOKEN_BYTES 16
#define TICKET_BYTES 16
#define DIGEST_BYTES 32
#define NAME_FIELD 16
#define SETUP_FIELD 20
#define CODE_FIELD 8
#define WORD_BYTES 4
#define HALF_BYTES 2
#define BITS_PER_BYTE 8
#define HEX_DIGITS 16
/* a's value as a hex digit */
#define HEX_LETTERS_FROM 10
#define PRINT_KEY_BYTES 8
/* the signature's domain, before the 176 bytes it covers (ladder.py) */
#define DOMAIN "ReBushido ranked 1"
#define DOMAIN_BYTES 19
#define RELAY_VERSION 3
/* how often an unanswered ask is said again, a STAY is said, and an
 * upload's next chunks are sent; how long a start waits */
#define ASK_EVERY_MS 600
#define STAY_EVERY_MS 1000
#define UPLOAD_EVERY_MS 60
#define UPLOAD_WINDOW 8
#define START_GIVE_UP_MS 5000
#define POLL_MS 5
#define RTT_SMOOTH 4
/* the frames of a match the digest is made of: over two hours of them */
#define FRAMES_MOST 0x40000
#define HASH_EVERY 30
#define HASHES_MOST (FRAMES_MOST / HASH_EVERY + 1)
#define FRAME_BYTES 8
#define HASH_BYTES 12
/* the replays of a match kept for a dispute */
#define REPLAYS_MOST 16
#define UPLOAD_MOST (4U << 20)
#define UPLOAD_MAGIC "RBRANKUP"
#define UPLOAD_MAGIC_BYTES 8
#define READ_PIECE 4096
/* the identity's file */
#define IDENTITY_HEAD "ReBushido ranked identity 1"
#define IDENTITY_BYTES 512
/* the seed's line: after a line's end, the word and a space */
#define SEED_WORD "\nseed "
#define SEED_WORD_BYTES 6
/* a file written beside first: its name and ".part" */
#define PART_ENDING ".part"
#define PART_PATH_BYTES (PORT_RANKED_PATH_BYTES + sizeof(PART_ENDING))
#define DEFAULT_IDENTITY "rebushido-identity.key"
#define KEY_ENDING ".key"
#define KEY_ENDING_BYTES 4
/* the stage pages' places: three pages of six */
#define STAGE_PLACES 18
#define MATCH_COUNT_SHIFT 8

enum {
    KIND_RANKED = PORT_NET_KIND_RELAY + 6,
    KIND_RANK = PORT_NET_KIND_RELAY + 15,
};

/* RANK's actions. */
enum {
    ACTION_HELLO = 1,
    ACTION_NAME,
    ACTION_QUEUE,
    ACTION_STAY,
    ACTION_LEAVE,
    ACTION_REPORT,
    ACTION_UPLOAD,
    ACTION_LADDER
};

/* RANKED's status and what it answers with. */
enum {
    STATUS_OK = 1,
    STATUS_VERSION = 0x84,
    STATUS_RATE_LIMITED = 0x86,
    STATUS_BAD_REQUEST = 0x87,
    STATUS_INCOMPATIBLE = 0x88,
    STATUS_BAD_SIGNATURE = 0x89,
    STATUS_STALE = 0x8A,
    STATUS_NAME_TAKEN = 0x8B,
    STATUS_NAME_BAD = 0x8C,
    STATUS_NOT_REGISTERED = 0x8D,
    STATUS_NO_MATCH = 0x8E,
    STATUS_DISABLED = 0x90,
    STATUS_TOO_MANY = 0x91,
    STATUS_FULL = 0x92,
};

enum {
    WHAT_PROFILE = 1,
    WHAT_QUEUED,
    WHAT_MATCHED,
    WHAT_REPORTED,
    WHAT_LADDER,
    WHAT_UPLOADED
};

#define FLAG_REGISTERED 0x01
#define FLAG_PROVISIONAL 0x02

/* RANK's fields. */
enum {
    RANK_KEY = PORT_NET_HEADER_BYTES,
    RANK_NONCE = RANK_KEY + KEY_BYTES,
    RANK_NAME = RANK_NONCE + NONCE_BYTES,
    RANK_SETUP = RANK_NAME + NAME_FIELD,
    RANK_STAMP = RANK_SETUP + SETUP_FIELD,
    RANK_RTT = RANK_STAMP + WORD_BYTES,
    RANK_FLAGS = RANK_RTT + HALF_BYTES,
    RANK_REASON,
    RANK_MATCH,
    RANK_TOKEN = RANK_MATCH + WORD_BYTES,
    RANK_WINNER = RANK_TOKEN + TOKEN_BYTES,
    RANK_RULE,
    RANK_COUNT,
    RANK_FIGHTS,
    RANK_FRAMES,
    RANK_DIGEST = RANK_FRAMES + WORD_BYTES,
    RANK_ARG = RANK_DIGEST + DIGEST_BYTES,
    RANK_ARG2 = RANK_ARG + WORD_BYTES,
    RANK_SIGNATURE = SIGNED_BYTES,
};

/* RANKED's fields. */
enum {
    ANSWER_NONCE = PORT_NET_HEADER_BYTES,
    ANSWER_STAMP = ANSWER_NONCE + NONCE_BYTES,
    ANSWER_FLAGS = ANSWER_STAMP + WORD_BYTES,
    ANSWER_WHAT,
    ANSWER_FIRST_TO,
    ANSWER_NAME = ANSWER_FIRST_TO + 2,
    ANSWER_RATING = ANSWER_NAME + NAME_FIELD,
    ANSWER_RD = ANSWER_RATING + HALF_BYTES,
    ANSWER_RANK = ANSWER_RD + HALF_BYTES,
    ANSWER_OF = ANSWER_RANK + WORD_BYTES,
    ANSWER_WINS = ANSWER_OF + WORD_BYTES,
    ANSWER_LOSSES = ANSWER_WINS + HALF_BYTES,
    ANSWER_DRAWS = ANSWER_LOSSES + HALF_BYTES,
    ANSWER_TOKEN = ANSWER_DRAWS + 2 * HALF_BYTES,
    ANSWER_WAITED = ANSWER_TOKEN + TOKEN_BYTES,
    ANSWER_QUEUE = ANSWER_WAITED + HALF_BYTES,
    ANSWER_MATCH = ANSWER_QUEUE + HALF_BYTES,
    ANSWER_ROLE = ANSWER_MATCH + WORD_BYTES,
    ANSWER_STAGE,
    ANSWER_COUNT,
    ANSWER_CODE = ANSWER_COUNT + 2,
    ANSWER_TICKET = ANSWER_CODE + CODE_FIELD,
    ANSWER_OPPONENT = ANSWER_TICKET + TICKET_BYTES,
    ANSWER_OPPONENT_RATING = ANSWER_OPPONENT + NAME_FIELD,
    ANSWER_STATE = ANSWER_OPPONENT_RATING + 2 * HALF_BYTES,
    ANSWER_UPLOAD,
    ANSWER_WINNER,
    ANSWER_BEFORE = ANSWER_WINNER + 2,
    ANSWER_AFTER = ANSWER_BEFORE + HALF_BYTES,
    ANSWER_OTHER_BEFORE = ANSWER_AFTER + HALF_BYTES,
    ANSWER_OTHER_AFTER = ANSWER_OTHER_BEFORE + HALF_BYTES,
    ANSWER_HAVE = ANSWER_OTHER_AFTER + HALF_BYTES,
    ANSWER_MOST = ANSWER_HAVE + WORD_BYTES,
    ANSWER_FIRST = ANSWER_MOST + WORD_BYTES,
    ANSWER_ROWS = ANSWER_FIRST + WORD_BYTES,
    ANSWER_KIND,
};

/* A row's fields: a place, or a match. */
enum {
    ROW_RANK = 0,
    ROW_RATING = ROW_RANK + WORD_BYTES,
    ROW_RD = ROW_RATING + HALF_BYTES,
    ROW_WINS = ROW_RD + HALF_BYTES,
    ROW_LOSSES = ROW_WINS + HALF_BYTES,
    ROW_DRAWS = ROW_LOSSES + HALF_BYTES,
    ROW_PROVISIONAL = ROW_DRAWS + HALF_BYTES,
    ROW_PLAYER = ROW_PROVISIONAL + HALF_BYTES,
    ROW_WHEN = 0,
    ROW_WINNER = ROW_WHEN + WORD_BYTES,
    ROW_STATE,
    ROW_DELTA_FIRST,
    ROW_DELTA_SECOND = ROW_DELTA_FIRST + HALF_BYTES,
    ROW_FIRST = ROW_DELTA_SECOND + 2 * HALF_BYTES,
    ROW_SECOND = ROW_FIRST + NAME_FIELD,
};

/* A REPORT's words, kept to be said again until the lobby has it. */
typedef struct {
    s32 reason;
    s32 winner;
    s32 rule;
    s32 count;
    u32 frames;
    u8 digest[DIGEST_BYTES];
} RankedReport;

static struct {
    s32 is_on;
    s32 socket;
    PortNetAddress server;
    char lobby[PORT_NET_ADDRESS_TEXT_BYTES];
    /* the identity */
    char path[PORT_RANKED_PATH_BYTES];
    s32 has_key;
    u8 secret[PORT_ED25519_SECRET_BYTES];
    u8 key[PORT_ED25519_PUBLIC_BYTES];
    char player[PORT_RELAY_PLAYER_BYTES];
    u8 nonce[NONCE_BYTES];
    u8 token[TOKEN_BYTES];
    /* the ask said again until answered (ACTION_..., or 0), and when */
    s32 asking;
    u32 asked_at;
    u32 stay_at;
    u32 heard_at;
    u32 rtt;
    s32 queued;
    /* the match the lobby gave */
    u32 match;
    char code[PORT_RELAY_CODE_BYTES];
    u8 ticket[TICKET_BYTES];
    s32 count;
    s32 handed; /* given to online.c to play */
    s32 playing;
    RankedReport report;
    s32 has_report;
    s32 lie; /* a test's: the other winner reported */
    /* the digest's makings: each frame's number and two inputs, each
     * hash's frame and hash, as they are hashed */
    u8 *frames;
    u32 fed;
    u8 *hashes;
    u32 hash_count;
    /* the match's replays, and an upload of them */
    char replays[REPLAYS_MOST][PORT_RANKED_PATH_BYTES];
    s32 replay_count;
    u8 *blob;
    u32 blob_size;
    u32 sent_to;
    u32 upload_at;
    PortRankedView view;
} g_ranked = {.socket = -1};

/* ---- words ---- */

static void port_RankedText(char *out, u32 room, const char *format, ...) {
    PortArgs args;

    PORT_ARGS_START(args, format);
    port_FormatText(out, room, format, args);
    PORT_ARGS_END(args);
}

static void port_RankedPutText(u8 *out, u32 room, const char *text) {
    u32 i;

    for (i = 0; i < room; i++) {
        out[i] = 0;
    }
    for (i = 0; text != NULL && text[i] != 0 && i < room; i++) {
        out[i] = (u8)(text[i] >= ' ' && text[i] <= '~' ? text[i] : '?');
    }
}

static void port_RankedGetText(char *out, const u8 *in, u32 room) {
    u32 i;

    for (i = 0; i < room && in[i] != 0; i++) {
        out[i] = (char)(in[i] >= ' ' && in[i] <= '~' ? in[i] : '?');
    }
    out[i] = 0;
}

static u32 port_RankedHalf(const u8 *in) {
    return (u32)in[0] | ((u32)in[1] << BITS_PER_BYTE);
}

static void port_RankedPutHalf(u8 *out, u32 value) {
    out[0] = (u8)value;
    out[1] = (u8)(value >> BITS_PER_BYTE);
}

static void port_RankedCopy(u8 *out, const u8 *in, u32 count) {
    u32 i;

    for (i = 0; i < count; i++) {
        out[i] = in[i];
    }
}

static void port_RankedKeep(char *out, u32 room, const char *text) {
    u32 i;

    for (i = 0; text != NULL && text[i] != 0 && i + 1 < room; i++) {
        out[i] = text[i];
    }
    out[i] = 0;
}

/* A hex digit's value, of either case: -1 where it is not one. */
static s32 port_RankedHexDigit(char letter) {
    if (letter >= '0' && letter <= '9') {
        return letter - '0';
    }
    if (letter >= 'a' && letter <= 'f') {
        return letter - 'a' + HEX_LETTERS_FROM;
    }
    if (letter >= 'A' && letter <= 'F') {
        return letter - 'A' + HEX_LETTERS_FROM;
    }
    return -1;
}

static void port_RankedPutHex(char *out, const u8 *bytes, u32 count) {
    static const char digits[] = "0123456789abcdef";
    u32 i;

    for (i = 0; i < count; i++) {
        out[2 * i] = digits[bytes[i] >> 4];
        out[2 * i + 1] = digits[bytes[i] & (HEX_DIGITS - 1)];
    }
    out[2 * count] = 0;
}

/* ---- the identity ---- */

void port_RankedSetIdentity(const char *path) {
    port_RankedKeep(g_ranked.path, sizeof(g_ranked.path), path);
    g_ranked.has_key = FALSE;
}

const char *port_RankedIdentityPath(void) {
    return g_ranked.path[0] != 0 ? g_ranked.path : DEFAULT_IDENTITY;
}

/* The key pair made of a seed, which is wiped after. */
static void port_RankedTakeSeed(u8 *seed) {
    char print[2 * PRINT_KEY_BYTES + 1];

    crypto_ed25519_key_pair(g_ranked.secret, g_ranked.key, seed);
    crypto_wipe(seed, PORT_ED25519_SEED_BYTES);
    g_ranked.has_key = TRUE;
    port_RankedPutHex(print, g_ranked.key, PRINT_KEY_BYTES);
    port_RankedKeep(g_ranked.view.print, sizeof(g_ranked.view.print), print);
}

/* The seed of an identity's file: "seed " and 64 hex digits on a line of
 * their own, under its first line. 1, or 0 where it is not one. */
static s32 port_RankedParse(const char *text, u32 size, u8 *seed) {
    static const char head[] = IDENTITY_HEAD;
    static const char seed_word[] = SEED_WORD;
    u32 i;
    u32 at;

    for (i = 0; head[i] != 0; i++) {
        if (i >= size || text[i] != head[i]) {
            return 0;
        }
    }
    for (at = i; at + SEED_WORD_BYTES <= size; at++) {
        u32 k;

        for (k = 0; k < SEED_WORD_BYTES && text[at + k] == seed_word[k]; k++) {
        }
        if (k == SEED_WORD_BYTES) {
            u32 from = at + SEED_WORD_BYTES;

            for (i = 0; i < PORT_ED25519_SEED_BYTES; i++) {
                s32 high = from + 2 * i + 1 < size
                               ? port_RankedHexDigit(text[from + 2 * i])
                               : -1;
                s32 low = high >= 0
                              ? port_RankedHexDigit(text[from + 2 * i + 1])
                              : -1;

                if (low < 0) {
                    return 0;
                }
                seed[i] = (u8)(high * HEX_DIGITS + low);
            }
            return 1;
        }
    }
    return 0;
}

/* The identity's file written: beside, then given its name, so that a
 * file half written is never the identity. */
static s32 port_RankedWriteIdentity(const char *path, const u8 *seed) {
    char text[IDENTITY_BYTES];
    char hex[2 * PORT_ED25519_SEED_BYTES + 1];
    char key_hex[2 * PORT_ED25519_PUBLIC_BYTES + 1];
    char part[PART_PATH_BYTES];
    u32 length;
    s32 file;
    s32 ok;

    port_RankedPutHex(hex, seed, PORT_ED25519_SEED_BYTES);
    port_RankedPutHex(key_hex, g_ranked.key, PORT_ED25519_PUBLIC_BYTES);
    port_RankedText(
        text, sizeof(text),
        "%s\n"
        "seed %s\n"
        "public %s\n"
        "# This file is your ranked identity at the ReBushido lobby: the\n"
        "# ladder knows you by it. Keep a copy of it somewhere safe; lose it\n"
        "# and your rating and name are gone, and you begin again. Give it\n"
        "# to nobody: whoever has it can play as you.\n",
        IDENTITY_HEAD, hex, key_hex);
    crypto_wipe(hex, sizeof(hex));
    for (length = 0; text[length] != 0; length++) {
    }
    port_RankedText(part, sizeof(part), "%s" PART_ENDING, path);
    file = port_HostCreate(part);
    if (file < 0) {
        crypto_wipe(text, sizeof(text));
        return 0;
    }
    ok = port_HostAppend(file, text, length);
    port_HostFlush(file);
    port_HostClose(file);
    crypto_wipe(text, sizeof(text));
    if (!ok || !port_HostRename(part, path)) {
        port_HostRemove(part);
        return 0;
    }
    return 1;
}

/* The identity read, or made where there is none: 1, or 0 with why. */
static s32 port_RankedIdentity(char *why, u32 room) {
    const char *path = port_RankedIdentityPath();
    char text[IDENTITY_BYTES];
    u8 seed[PORT_ED25519_SEED_BYTES];
    s32 file;
    u32 size;

    if (g_ranked.has_key) {
        return 1;
    }
    file = port_HostOpen(path);
    if (file >= 0) {
        size = port_HostReadAt(file, 0, text, sizeof(text) - 1);
        port_HostClose(file);
        text[size] = 0;
        if (!port_RankedParse(text, size, seed)) {
            crypto_wipe(text, sizeof(text));
            port_RankedText(why, room,
                            "Your identity file %s is damaged. Put your copy "
                            "of it back, or move it away to begin a new "
                            "identity.",
                            path);
            return 0;
        }
        crypto_wipe(text, sizeof(text));
        port_RankedTakeSeed(seed);
        port_Log("ranked: the identity is %s (key %s...)\n", path,
                 g_ranked.view.print);
        return 1;
    }
    if (!port_HostRandom(seed, sizeof(seed))) {
        port_RankedText(why, room,
                        "This PC gave no random numbers to make an identity "
                        "of.");
        return 0;
    }
    {
        u8 kept[PORT_ED25519_SEED_BYTES];

        port_RankedCopy(kept, seed, sizeof(kept));
        port_RankedTakeSeed(seed);
        if (!port_RankedWriteIdentity(path, kept)) {
            crypto_wipe(kept, sizeof(kept));
            g_ranked.has_key = FALSE;
            port_RankedText(
                why, room, "Your identity could not be written to %s.", path);
            return 0;
        }
        crypto_wipe(kept, sizeof(kept));
    }
    port_Log("ranked: a new identity made, %s (key %s...)\n", path,
             g_ranked.view.print);
    return 1;
}

/* A copy of the identity's file beside it, named by the date. */
s32 port_RankedBackUp(char *out, u32 room) {
    const char *path = port_RankedIdentityPath();
    char text[IDENTITY_BYTES];
    char stem[PORT_RANKED_PATH_BYTES];
    PortDate date;
    s32 file;
    u32 size;
    u32 length;
    s32 ok;

    file = port_HostOpen(path);
    if (file < 0) {
        port_RankedText(out, room,
                        "There is no identity yet: it is made the first time "
                        "you play ranked.");
        return 0;
    }
    size = port_HostReadAt(file, 0, text, sizeof(text));
    port_HostClose(file);
    port_HostDate(&date);
    /* NAME-backup-DATE.key, of NAME.key */
    for (length = 0; path[length] != 0; length++) {
    }
    if (length > KEY_ENDING_BYTES &&
        port_TextsEqual(path + length - KEY_ENDING_BYTES, KEY_ENDING)) {
        length -= KEY_ENDING_BYTES;
    }
    port_RankedKeep(
        stem, length + 1 < sizeof(stem) ? length + 1 : sizeof(stem), path);
    port_RankedText(
        out, room, "%s-backup-%04d%02d%02d-%02d%02d%02d%s", stem, date.year,
        date.month, date.day, date.hour, date.minute, date.second, KEY_ENDING);
    file = port_HostCreate(out);
    ok = file >= 0 && port_HostAppend(file, text, size);
    if (file >= 0) {
        port_HostFlush(file);
        port_HostClose(file);
    }
    crypto_wipe(text, sizeof(text));
    if (!ok) {
        port_RankedText(
            out, room, "The copy could not be written beside %s.", path);
        return 0;
    }
    port_Log("ranked: the identity copied to %s\n", out);
    return 1;
}

/* ---- the datagrams ---- */

/* A RANK: `action`, with this player's words and `out`'s fields already
 * put; signed where the action is one of those the ladder holds to the
 * key. */
static void port_RankedSend(u8 *out, s32 action, u32 count) {
    u8 message[DOMAIN_BYTES + SIGNED_BYTES];
    char setup[PORT_SETUP_CODE_BYTES];
    static const char domain[DOMAIN_BYTES] = DOMAIN;

    if (g_ranked.socket < 0) {
        return;
    }
    port_NetHeader(out, KIND_RANK, action, RELAY_VERSION);
    port_RankedCopy(out + RANK_KEY, g_ranked.key, KEY_BYTES);
    port_RankedCopy(out + RANK_NONCE, g_ranked.nonce, NONCE_BYTES);
    port_RankedPutText(out + RANK_NAME, NAME_FIELD, g_ranked.player);
    port_SessionSetupCode(setup);
    port_RankedPutText(out + RANK_SETUP, SETUP_FIELD, setup);
    port_NetPutWord(out + RANK_STAMP, port_NetClock());
    port_RankedPutHalf(out + RANK_RTT, g_ranked.rtt);
    if (action == ACTION_NAME || action == ACTION_QUEUE ||
        action == ACTION_REPORT) {
        port_RankedCopy(message, (const u8 *)domain, DOMAIN_BYTES);
        port_RankedCopy(message + DOMAIN_BYTES, out, SIGNED_BYTES);
        crypto_ed25519_sign(
            out + RANK_SIGNATURE, g_ranked.secret, message, sizeof(message));
    }
    port_HostUdpSend(g_ranked.socket, &g_ranked.server, out, count);
}

static void port_RankedClear(u8 *out, u32 count) {
    u32 i;

    for (i = 0; i < count; i++) {
        out[i] = 0;
    }
}

/* An ask: its fields, from what is kept. */
static void port_RankedSay(s32 action) {
    u8 out[RANK_BYTES];

    port_RankedClear(out, sizeof(out));
    if (action == ACTION_STAY || action == ACTION_LEAVE ||
        action == ACTION_REPORT) {
        port_RankedCopy(out + RANK_TOKEN, g_ranked.token, TOKEN_BYTES);
    }
    if (action == ACTION_REPORT || (action == ACTION_STAY && g_ranked.match)) {
        port_NetPutWord(out + RANK_MATCH, g_ranked.match);
    }
    if (action == ACTION_REPORT) {
        const RankedReport *report = &g_ranked.report;

        out[RANK_REASON] = (u8)report->reason;
        out[RANK_WINNER] = (u8)report->winner;
        out[RANK_RULE] = (u8)report->rule;
        out[RANK_COUNT] = (u8)report->count;
        port_NetPutWord(out + RANK_FRAMES, report->frames);
        port_RankedCopy(out + RANK_DIGEST, report->digest, DIGEST_BYTES);
    }
    if (action == ACTION_LADDER) {
        port_NetPutWord(out + RANK_ARG, g_ranked.view.list_first);
        port_NetPutWord(out + RANK_ARG2, (u32)g_ranked.view.list_kind);
    }
    port_RankedSend(out, action, sizeof(out));
    if (action == ACTION_STAY) {
        g_ranked.stay_at = port_NetClock();
    }
}

/* An ask said now, and again until answered. */
static void port_RankedAsk(s32 action) {
    g_ranked.asking = action;
    g_ranked.asked_at = port_NetClock();
    port_RankedSay(action);
}

/* The lobby's refusal in a player's words. */
static void port_RankedRefusal(s32 status) {
    char *why = g_ranked.view.why;
    u32 room = sizeof(g_ranked.view.why);

    switch (status) {
    case STATUS_VERSION:
        port_RankedText(why, room,
                        "The lobby server is of another version: this "
                        "release cannot play ranked there.");
        break;
    case STATUS_DISABLED:
        port_RankedText(why, room, "This lobby server keeps no ladder.");
        break;
    case STATUS_NAME_TAKEN:
        port_RankedText(why, room,
                        "The name %s is another player's (or too like it): "
                        "choose another in Your name.",
                        g_ranked.player);
        break;
    case STATUS_NAME_BAD:
        port_RankedText(why, room,
                        "%s cannot be a ranked name: 3 to 15 letters, digits, "
                        "spaces, dots, dashes and underscores, beginning with "
                        "a letter or a digit.",
                        g_ranked.player);
        break;
    case STATUS_TOO_MANY:
        port_RankedText(why, room,
                        "Too many new identities came from your address "
                        "today: try again tomorrow.");
        break;
    case STATUS_BAD_SIGNATURE:
        port_RankedText(why, room,
                        "The lobby could not check your identity's "
                        "signature.");
        break;
    case STATUS_RATE_LIMITED:
        port_RankedText(why, room,
                        "The lobby has had too many asks from your address: "
                        "wait a moment.");
        break;
    case STATUS_FULL:
        port_RankedText(why, room, "The lobby is full just now.");
        break;
    case STATUS_NO_MATCH:
        port_RankedText(why, room, "The lobby has no such match of yours.");
        break;
    default:
        port_RankedText(
            why, room, "The lobby could not take this request (%d).", status);
        break;
    }
    port_Log("ranked: the lobby refused (%d): %s\n", status, why);
}

/* A row of a list. */
static void port_RankedTakeRow(PortRankedRow *row, const u8 *at, s32 kind) {
    if (kind == PORT_RANKED_TOP || kind == PORT_RANKED_AROUND) {
        row->rank = port_NetGetWord(at + ROW_RANK);
        row->rating = (s32)port_RankedHalf(at + ROW_RATING);
        row->rd = (s32)port_RankedHalf(at + ROW_RD);
        row->wins = (s32)port_RankedHalf(at + ROW_WINS);
        row->losses = (s32)port_RankedHalf(at + ROW_LOSSES);
        row->draws = (s32)port_RankedHalf(at + ROW_DRAWS);
        row->provisional = (s32)port_RankedHalf(at + ROW_PROVISIONAL);
        port_RankedGetText(row->player, at + ROW_PLAYER, NAME_FIELD);
        return;
    }
    row->when = port_NetGetWord(at + ROW_WHEN);
    row->winner = at[ROW_WINNER];
    row->state = at[ROW_STATE];
    row->delta_first = (s16)port_RankedHalf(at + ROW_DELTA_FIRST);
    row->delta_second = (s16)port_RankedHalf(at + ROW_DELTA_SECOND);
    port_RankedGetText(row->player, at + ROW_FIRST, NAME_FIELD);
    port_RankedGetText(row->second, at + ROW_SECOND, NAME_FIELD);
}

/* What a decided match came to, for this player. */
static void port_RankedTakeResult(const u8 *in) {
    PortRankedView *view = &g_ranked.view;
    s32 first = view->role == PORT_SESSION_HOST;

    view->state = in[ANSWER_STATE];
    view->winner = in[ANSWER_WINNER];
    view->before = (s32)port_RankedHalf(in + ANSWER_BEFORE);
    view->after = (s32)port_RankedHalf(in + ANSWER_AFTER);
    view->other_before = (s32)port_RankedHalf(in + ANSWER_OTHER_BEFORE);
    view->other_after = (s32)port_RankedHalf(in + ANSWER_OTHER_AFTER);
    view->won = view->winner == PORT_ROOM_DRAW         ? 2
                : view->winner == PORT_ROOM_WON_FIRST  ? first
                : view->winner == PORT_ROOM_WON_SECOND ? !first
                                                       : -1;
}

/* An upload of the match's replays begun, where the lobby asks for them:
 * the files one after another in the container ladder.py unpacks. */
static void port_RankedStartUpload(void) {
    u32 total = UPLOAD_MAGIC_BYTES + WORD_BYTES;
    u32 at;
    s32 i;

    if (g_ranked.blob != NULL || g_ranked.replay_count == 0) {
        return;
    }
    for (i = 0; i < g_ranked.replay_count; i++) {
        s32 file = port_HostOpen(g_ranked.replays[i]);
        u32 name;

        if (file < 0) {
            continue;
        }
        for (name = 0; g_ranked.replays[i][name] != 0; name++) {
        }
        total += 2 * WORD_BYTES + name + port_HostFileSize(file);
        port_HostClose(file);
    }
    if (total > UPLOAD_MOST) {
        port_Log("ranked: the match's replays (%u bytes) are more than the "
                 "lobby takes\n",
                 total);
        return;
    }
    g_ranked.blob = port_HostAllocate(total);
    if (g_ranked.blob == NULL) {
        return;
    }
    port_RankedCopy(
        g_ranked.blob, (const u8 *)UPLOAD_MAGIC, UPLOAD_MAGIC_BYTES);
    port_NetPutWord(g_ranked.blob + UPLOAD_MAGIC_BYTES, 0);
    at = UPLOAD_MAGIC_BYTES + WORD_BYTES;
    for (i = 0; i < g_ranked.replay_count; i++) {
        const char *path = g_ranked.replays[i];
        const char *name = path;
        s32 file = port_HostOpen(path);
        u32 size;
        u32 length;
        u32 k;

        if (file < 0) {
            continue;
        }
        for (k = 0; path[k] != 0; k++) {
            if (path[k] == '/' || path[k] == '\\') {
                name = path + k + 1;
            }
        }
        for (length = 0; name[length] != 0; length++) {
        }
        size = port_HostFileSize(file);
        if (at + 2 * WORD_BYTES + length + size > total) {
            port_HostClose(file);
            break;
        }
        port_NetPutWord(g_ranked.blob + at, length);
        port_RankedCopy(
            g_ranked.blob + at + WORD_BYTES, (const u8 *)name, length);
        at += WORD_BYTES + length;
        port_NetPutWord(g_ranked.blob + at, size);
        at += WORD_BYTES;
        at += port_HostReadAt(file, 0, g_ranked.blob + at, size);
        port_HostClose(file);
        port_NetPutWord(
            g_ranked.blob + UPLOAD_MAGIC_BYTES,
            port_NetGetWord(g_ranked.blob + UPLOAD_MAGIC_BYTES) + 1);
    }
    g_ranked.blob_size = at;
    g_ranked.sent_to = 0;
    g_ranked.view.uploading = TRUE;
    g_ranked.view.uploaded = 0;
    g_ranked.view.upload_total = at;
    port_Log("ranked: match %u is disputed: its %d replays (%u bytes) go up "
             "for the owner to check\n",
             g_ranked.match, g_ranked.replay_count, at);
}

/* The upload's next chunks, from what the lobby has. */
static void port_RankedUploadTurn(void) {
    u8 out[UPLOAD_BYTES];
    u32 now = port_NetClock();
    u32 at;
    s32 i;

    if (!g_ranked.view.uploading ||
        now - g_ranked.upload_at < UPLOAD_EVERY_MS) {
        return;
    }
    g_ranked.upload_at = now;
    at = g_ranked.view.uploaded;
    for (i = 0; i < UPLOAD_WINDOW && at < g_ranked.blob_size; i++) {
        u32 piece = g_ranked.blob_size - at < CHUNK_BYTES
                        ? g_ranked.blob_size - at
                        : CHUNK_BYTES;

        port_RankedClear(out, sizeof(out));
        port_RankedCopy(out + RANK_TOKEN, g_ranked.token, TOKEN_BYTES);
        port_NetPutWord(out + RANK_MATCH, g_ranked.match);
        port_NetPutWord(out + RANK_ARG, at);
        port_NetPutWord(out + RANK_ARG2, g_ranked.blob_size);
        port_RankedCopy(out + SIGNED_BYTES, g_ranked.blob + at, piece);
        port_RankedSend(out, ACTION_UPLOAD, sizeof(out));
        at += piece;
    }
}

/* A RANKED from the lobby. */
static void port_RankedTake(const u8 *in, s32 status, u32 request) {
    PortRankedView *view = &g_ranked.view;
    u32 stamp = port_NetGetWord(in + ANSWER_STAMP);
    u32 trip = port_NetClock() - stamp;
    s32 what = in[ANSWER_WHAT];
    s32 i;

    g_ranked.heard_at = port_NetClock();
    if (stamp != 0 && trip < START_GIVE_UP_MS) {
        g_ranked.rtt =
            g_ranked.rtt == 0
                ? trip
                : (g_ranked.rtt * (RTT_SMOOTH - 1) + trip) / RTT_SMOOTH;
        view->rtt = g_ranked.rtt;
    }
    for (i = 0; i < NONCE_BYTES; i++) {
        g_ranked.nonce[i] = in[ANSWER_NONCE + i];
    }
    if (status == STATUS_STALE) {
        /* the nonce had grown old: the ask signed again with the new */
        if (g_ranked.asking != 0) {
            port_RankedAsk(g_ranked.asking);
        }
        return;
    }
    if (status == STATUS_RATE_LIMITED) {
        g_ranked.asked_at = port_NetClock();
        return;
    }
    if (status != STATUS_OK) {
        port_RankedRefusal(status);
        if ((s32)request == g_ranked.asking || g_ranked.asking == 0) {
            g_ranked.asking = 0;
        }
        if (request == ACTION_QUEUE || request == ACTION_NAME) {
            g_ranked.queued = FALSE;
            if (view->phase == PORT_RANKED_QUEUED ||
                view->phase == PORT_RANKED_ASKING) {
                view->phase = PORT_RANKED_IDLE;
            }
        }
        if (request == ACTION_UPLOAD) {
            view->uploading = FALSE;
        }
        return;
    }
    view->registered = (in[ANSWER_FLAGS] & FLAG_REGISTERED) != 0;
    view->provisional = (in[ANSWER_FLAGS] & FLAG_PROVISIONAL) != 0;
    view->first_to = in[ANSWER_FIRST_TO];
    if (view->registered) {
        port_RankedGetText(view->player, in + ANSWER_NAME, NAME_FIELD);
    }
    view->rating = (s32)port_RankedHalf(in + ANSWER_RATING);
    view->rd = (s32)port_RankedHalf(in + ANSWER_RD);
    view->rank = port_NetGetWord(in + ANSWER_RANK);
    view->of = port_NetGetWord(in + ANSWER_OF);
    view->wins = (s32)port_RankedHalf(in + ANSWER_WINS);
    view->losses = (s32)port_RankedHalf(in + ANSWER_LOSSES);
    view->draws = (s32)port_RankedHalf(in + ANSWER_DRAWS);
    if ((s32)request == g_ranked.asking) {
        g_ranked.asking = 0;
    }
    if (view->phase == PORT_RANKED_ASKING) {
        view->phase = PORT_RANKED_IDLE;
    }
    switch (what) {
    case WHAT_QUEUED:
        port_RankedCopy(g_ranked.token, in + ANSWER_TOKEN, TOKEN_BYTES);
        view->waited = port_RankedHalf(in + ANSWER_WAITED);
        view->queue = port_RankedHalf(in + ANSWER_QUEUE);
        if (g_ranked.queued && view->phase == PORT_RANKED_IDLE) {
            view->phase = PORT_RANKED_QUEUED;
        }
        break;
    case WHAT_MATCHED: {
        u32 number = port_NetGetWord(in + ANSWER_MATCH);

        port_RankedCopy(g_ranked.token, in + ANSWER_TOKEN, TOKEN_BYTES);
        if (number != g_ranked.match || view->phase < PORT_RANKED_MATCHED) {
            g_ranked.match = number;
            g_ranked.handed = FALSE;
            g_ranked.has_report = FALSE;
            g_ranked.replay_count = 0;
            g_ranked.queued = FALSE;
            port_RankedGetText(g_ranked.code, in + ANSWER_CODE, CODE_FIELD - 2);
            port_RankedCopy(g_ranked.ticket, in + ANSWER_TICKET, TICKET_BYTES);
            g_ranked.count = in[ANSWER_COUNT];
            view->match = number;
            view->role =
                in[ANSWER_ROLE] == 1 ? PORT_SESSION_HOST : PORT_SESSION_JOIN;
            view->stage = in[ANSWER_STAGE];
            port_RankedGetText(
                view->opponent, in + ANSWER_OPPONENT, NAME_FIELD);
            view->opponent_rating =
                (s32)port_RankedHalf(in + ANSWER_OPPONENT_RATING);
            view->state = PORT_RANKED_PLAYING;
            view->won = -1;
            view->phase = PORT_RANKED_MATCHED;
            port_Log("ranked: match %u: %s (%d) against %s (%d), first to %d, "
                     "stage %d; this program is player %d\n",
                     number, g_ranked.player, view->rating, view->opponent,
                     view->opponent_rating, g_ranked.count, view->stage,
                     view->role == PORT_SESSION_HOST ? 1 : 2);
        }
        break;
    }
    case WHAT_REPORTED:
        if (port_NetGetWord(in + ANSWER_MATCH) != 0 &&
            port_NetGetWord(in + ANSWER_MATCH) != g_ranked.match) {
            break;
        }
        if (in[ANSWER_STATE] == PORT_RANKED_PLAYING) {
            if (view->phase < PORT_RANKED_REPORTED && g_ranked.has_report) {
                view->phase = PORT_RANKED_REPORTED;
                port_Log("ranked: match %u: the lobby has the report; the "
                         "other's is awaited\n",
                         g_ranked.match);
            }
            break;
        }
        if (view->phase != PORT_RANKED_DECIDED) {
            port_RankedTakeResult(in);
            view->phase = PORT_RANKED_DECIDED;
            port_Log("ranked: match %u is %s: %s, %d to %d\n", g_ranked.match,
                     view->state == PORT_RANKED_RATED      ? "rated"
                     : view->state == PORT_RANKED_DISPUTED ? "disputed"
                                                           : "void",
                     view->won == 1   ? "won"
                     : view->won == 0 ? "lost"
                     : view->won == 2 ? "a draw"
                                      : "no result",
                     view->before, view->after);
        }
        if (in[ANSWER_UPLOAD] != 0) {
            port_RankedStartUpload();
        }
        break;
    case WHAT_UPLOADED:
        view->uploaded = port_NetGetWord(in + ANSWER_HAVE);
        if (view->uploaded >= g_ranked.blob_size && view->uploading) {
            view->uploading = FALSE;
            port_Log("ranked: match %u: the lobby has its replays (%u "
                     "bytes)\n",
                     g_ranked.match, view->uploaded);
        }
        break;
    case WHAT_LADDER: {
        s32 count = in[ANSWER_ROWS];

        view->list_kind = in[ANSWER_KIND];
        view->list_first = port_NetGetWord(in + ANSWER_FIRST);
        view->list_count = count < PORT_RANKED_ROWS ? count : PORT_RANKED_ROWS;
        for (i = 0; i < view->list_count; i++) {
            port_RankedTakeRow(
                &view->rows[i], in + ANSWER_BYTES + i * ROW_BYTES,
                view->list_kind);
        }
        view->list_made++;
        break;
    }
    default:
        break;
    }
}

/* Everything the client's socket has, waiting up to `wait_ms` for the
 * first. */
static void port_RankedReceive(u32 wait_ms) {
    u8 in[PORT_NET_DATAGRAM_MAX];
    PortNetAddress from;
    s32 got;

    if (g_ranked.socket < 0) {
        return;
    }
    for (got = port_HostUdpReceive(
             g_ranked.socket, in, sizeof(in), &from, wait_ms);
         got > 0;
         got = port_HostUdpReceive(g_ranked.socket, in, sizeof(in), &from, 0)) {
        s32 detail;
        u32 number;
        s32 kind = port_NetReadHeader(in, got, &detail, &number);

        if (kind == KIND_RANKED && got >= ANSWER_BYTES &&
            port_NetAddressesEqual(&from, &g_ranked.server)) {
            port_RankedTake(in, detail, number);
        }
    }
}

/* ---- the client ---- */

s32 port_RankedStart(
    const char *lobby, const char *player, char *why, u32 room) {
    u32 start;

    if (g_ranked.is_on && port_TextsEqual(lobby, g_ranked.lobby)) {
        port_RankedRename(player);
        return 1;
    }
    port_RankedStop();
    if (!port_RankedIdentity(why, room)) {
        return 0;
    }
    if (!port_ParseNetAddress(lobby, &g_ranked.server) ||
        g_ranked.server.port == 0) {
        port_RankedText(why, room,
                        "The lobby server's address is not one (as "
                        "203.0.113.7:7845).");
        return 0;
    }
    g_ranked.socket = port_RelayOpenSocket(&g_ranked.server);
    if (g_ranked.socket < 0) {
        port_RankedText(why, room, "This PC's network could not be used.");
        return 0;
    }
    port_RankedKeep(g_ranked.lobby, sizeof(g_ranked.lobby), lobby);
    port_RankedKeep(g_ranked.player, sizeof(g_ranked.player), player);
    g_ranked.view.phase = PORT_RANKED_ASKING;
    g_ranked.view.why[0] = 0;
    g_ranked.is_on = TRUE;
    start = port_NetClock();
    port_RankedAsk(ACTION_HELLO);
    while (g_ranked.view.phase == PORT_RANKED_ASKING &&
           g_ranked.view.why[0] == 0) {
        if (port_NetClock() - start > START_GIVE_UP_MS) {
            port_RankedStop();
            port_RankedText(why, room,
                            "The lobby server does not answer. Is its address "
                            "right, and does your network let UDP through?");
            return 0;
        }
        if (port_NetClock() - g_ranked.asked_at >= ASK_EVERY_MS) {
            port_RankedAsk(ACTION_HELLO);
        }
        port_RankedReceive(POLL_MS);
    }
    if (g_ranked.view.why[0] != 0) {
        port_RankedKeep(why, room, g_ranked.view.why);
        port_RankedStop();
        return 0;
    }
    port_Log("ranked: at the lobby %s as %s: %s%s, rating %d (RD %d)\n", lobby,
             player, g_ranked.view.registered ? "known" : "new",
             g_ranked.view.provisional ? ", provisional" : "",
             g_ranked.view.rating, g_ranked.view.rd);
    return 1;
}

/* The client let go: out of the queue if in it, its socket closed, what
 * it knew forgotten but the key's print. */
void port_RankedStop(void) {
    PortRankedView kept = g_ranked.view;

    if (g_ranked.is_on && g_ranked.queued) {
        port_RankedSay(ACTION_LEAVE);
    }
    if (g_ranked.socket >= 0) {
        port_HostUdpClose(g_ranked.socket);
    }
    g_ranked.socket = -1;
    g_ranked.is_on = FALSE;
    g_ranked.queued = FALSE;
    g_ranked.asking = 0;
    g_ranked.lobby[0] = 0;
    port_RankedClear((u8 *)&g_ranked.view, sizeof(g_ranked.view));
    port_RankedKeep(
        g_ranked.view.print, sizeof(g_ranked.view.print), kept.print);
}

s32 port_RankedIsOn(void) { return g_ranked.is_on; }

/* The client's turn (port_ranked.h): the lobby's words read, an ask
 * unanswered said again, an upload's chunks sent, and once a second a STAY
 * where the player is queued, in a match or waiting for its decision. */
void port_RankedTick(void) {
    u32 now;
    s32 phase = g_ranked.view.phase;

    if (!g_ranked.is_on) {
        return;
    }
    port_RankedReceive(0);
    now = port_NetClock();
    if (g_ranked.asking != 0 && now - g_ranked.asked_at >= ASK_EVERY_MS) {
        port_RankedAsk(g_ranked.asking);
    }
    port_RankedUploadTurn();
    if (now - g_ranked.stay_at < STAY_EVERY_MS) {
        return;
    }
    /* there: in the queue, in a match, or waiting for its decision */
    if ((phase == PORT_RANKED_QUEUED && g_ranked.asking != ACTION_QUEUE) ||
        phase == PORT_RANKED_MATCHED || phase == PORT_RANKED_REPORTED ||
        (phase == PORT_RANKED_DECIDED && g_ranked.view.uploading)) {
        port_RankedSay(ACTION_STAY);
    }
}

void port_RankedGet(PortRankedView *out) { *out = g_ranked.view; }

void port_RankedQueue(void) {
    if (!g_ranked.is_on) {
        return;
    }
    g_ranked.view.why[0] = 0;
    g_ranked.queued = TRUE;
    g_ranked.view.phase = PORT_RANKED_QUEUED;
    g_ranked.view.waited = 0;
    port_Log("ranked: %s goes into the queue\n", g_ranked.player);
    port_RankedAsk(ACTION_QUEUE);
}

/* Out of the queue (port_ranked.h): the lobby told once; a LEAVE lost is
 * a player the queue lets go of after ten seconds unheard. */
void port_RankedLeaveQueue(void) {
    if (!g_ranked.is_on || !g_ranked.queued) {
        return;
    }
    port_RankedSay(ACTION_LEAVE);
    g_ranked.queued = FALSE;
    if (g_ranked.asking == ACTION_QUEUE) {
        g_ranked.asking = 0;
    }
    if (g_ranked.view.phase == PORT_RANKED_QUEUED) {
        g_ranked.view.phase = PORT_RANKED_IDLE;
    }
    port_Log("ranked: %s leaves the queue\n", g_ranked.player);
}

void port_RankedRename(const char *player) {
    if (port_TextsEqual(player, g_ranked.player)) {
        return;
    }
    port_RankedKeep(g_ranked.player, sizeof(g_ranked.player), player);
    if (g_ranked.is_on && g_ranked.view.registered) {
        g_ranked.view.why[0] = 0;
        port_RankedAsk(ACTION_NAME);
    }
}

void port_RankedAskList(s32 kind, u32 first) {
    if (!g_ranked.is_on) {
        return;
    }
    g_ranked.view.list_kind = kind;
    g_ranked.view.list_first = first;
    port_RankedAsk(ACTION_LADDER);
}

void port_RankedWait(u32 wait_ms) {
    if (g_ranked.is_on) {
        port_RankedReceive(wait_ms);
    }
}

/* A list asked for and waited for: 1 where it came. */
static s32 port_RankedFetch(s32 kind, u32 first) {
    u32 made = g_ranked.view.list_made;
    u32 start = port_NetClock();

    port_RankedAskList(kind, first);
    while (g_ranked.view.list_made == made) {
        if (port_NetClock() - start > START_GIVE_UP_MS) {
            return 0;
        }
        port_RankedTick();
        port_RankedReceive(POLL_MS);
    }
    return 1;
}

/* The player, the ladder's top and their own matches, to the log
 * (port_ranked.h). */
void port_RankedPrintLists(void) {
    const PortRankedView *view = &g_ranked.view;
    s32 i;

    port_Log("ranked: the ladder: you are %s, %d%s (RD %d), rank %u of %u, "
             "%d won, %d lost, %d drawn\n",
             view->player, view->rating, view->provisional ? "?" : "", view->rd,
             view->rank, view->of, view->wins, view->losses, view->draws);
    if (port_RankedFetch(PORT_RANKED_TOP, 1)) {
        for (i = 0; i < view->list_count; i++) {
            const PortRankedRow *row = &view->rows[i];

            port_Log("ranked: the ladder: %u. %s %d%s (RD %d) %d-%d-%d\n",
                     row->rank, row->player, row->rating,
                     row->provisional ? "?" : "", row->rd, row->wins,
                     row->losses, row->draws);
        }
    }
    if (port_RankedFetch(PORT_RANKED_MINE, 0)) {
        for (i = 0; i < view->list_count; i++) {
            const PortRankedRow *row = &view->rows[i];

            port_Log("ranked: your matches: %s vs %s, %s, %s (%+d, %+d)\n",
                     row->player, row->second,
                     row->winner == PORT_ROOM_WON_FIRST    ? "the first won"
                     : row->winner == PORT_ROOM_WON_SECOND ? "the second won"
                     : row->winner == PORT_ROOM_DRAW       ? "a draw"
                                                           : "no winner",
                     row->state == PORT_RANKED_RATED      ? "rated"
                     : row->state == PORT_RANKED_DISPUTED ? "disputed"
                                                          : "void",
                     row->delta_first, row->delta_second);
        }
    }
}

/* ---- the match ---- */

s32 port_RankedNext(PortRoomNext *out) {
    const PortRankedView *view = &g_ranked.view;
    u32 i;

    if (!g_ranked.is_on || view->phase != PORT_RANKED_MATCHED ||
        g_ranked.handed) {
        return 0;
    }
    port_RankedClear((u8 *)out, sizeof(*out));
    out->what = PORT_ROOM_NEXT_PLAY;
    out->role = view->role;
    out->match.number = (s32)g_ranked.match;
    out->match.a = 0;
    out->match.b = 1;
    for (i = 0; i < PORT_ROOM_CODE_BYTES && g_ranked.code[i] != 0; i++) {
        out->match.code[i] = g_ranked.code[i];
    }
    port_RankedCopy(out->match.ticket, g_ranked.ticket, TICKET_BYTES);
    out->match.has_ticket = TRUE;
    port_RankedKeep(
        out->first, sizeof(out->first),
        view->role == PORT_SESSION_HOST ? g_ranked.player : view->opponent);
    port_RankedKeep(
        out->second, sizeof(out->second),
        view->role == PORT_SESSION_HOST ? view->opponent : g_ranked.player);
    g_ranked.handed = TRUE;
    return 1;
}

u32 port_RankedRule(void) {
    return (u32)PORT_MATCH_FIRST_TO |
           ((u32)(g_ranked.count > 0 ? g_ranked.count : 1)
            << MATCH_COUNT_SHIFT);
}

/* A match begins (port_ranked.h): the room for its digest's makings,
 * made once, and its replays' list emptied. */
void port_RankedMatchBegins(void) {
    if (g_ranked.frames == NULL) {
        g_ranked.frames = port_HostAllocate(FRAMES_MOST * FRAME_BYTES);
        g_ranked.hashes = port_HostAllocate(HASHES_MOST * HASH_BYTES);
    }
    g_ranked.fed = 0;
    g_ranked.hash_count = 0;
    g_ranked.replay_count = 0;
    g_ranked.playing = g_ranked.frames != NULL && g_ranked.hashes != NULL;
    port_Log("ranked: match %u begins: first to %d, the lobby's stage %d\n",
             g_ranked.match, g_ranked.count, g_ranked.view.stage);
}

s32 port_RankedStagePlace(s32 *place) {
    if (!g_ranked.playing || g_ranked.view.stage < 0 ||
        g_ranked.view.stage >= STAGE_PLACES) {
        return FALSE;
    }
    *place = g_ranked.view.stage;
    return TRUE;
}

/* A frame's two final inputs, in order (port_ranked.h): as they are
 * hashed, its number and the two pads. */
void port_RankedFrame(u32 frame, u16 first, u16 second) {
    if (!g_ranked.playing || frame != g_ranked.fed || frame >= FRAMES_MOST) {
        return;
    }
    port_NetPutWord(g_ranked.frames + frame * FRAME_BYTES, frame);
    port_RankedPutHalf(
        g_ranked.frames + frame * FRAME_BYTES + WORD_BYTES, first);
    port_RankedPutHalf(
        g_ranked.frames + frame * FRAME_BYTES + WORD_BYTES + HALF_BYTES,
        second);
    g_ranked.fed = frame + 1;
}

/* A final state hash (port_ranked.h): its frame and its eight bytes. */
void port_RankedHash(u32 frame, u64 hash) {
    u8 *at;
    s32 i;

    if (!g_ranked.playing || g_ranked.hash_count >= HASHES_MOST) {
        return;
    }
    at = g_ranked.hashes + g_ranked.hash_count * HASH_BYTES;
    port_NetPutWord(at, frame);
    for (i = 0; i < (s32)sizeof(hash); i++) {
        at[WORD_BYTES + i] = (u8)(hash >> (BITS_PER_BYTE * i));
    }
    g_ranked.hash_count++;
}

void port_RankedReplayWritten(const char *path) {
    if (!g_ranked.playing || g_ranked.replay_count >= REPLAYS_MOST) {
        return;
    }
    port_RankedKeep(
        g_ranked.replays[g_ranked.replay_count], PORT_RANKED_PATH_BYTES, path);
    g_ranked.replay_count++;
}

void port_RankedLie(void) { g_ranked.lie = TRUE; }

/* The match's digest: BLAKE2b of the frames' inputs before `end`, of the
 * hashes of frames before it, and of `end` itself. */
static void port_RankedDigest(u32 end, u8 *digest) {
    u8 parts[3 * DIGEST_BYTES];
    u32 frames = end < g_ranked.fed ? end : g_ranked.fed;
    u32 count = 0;
    u32 i;

    crypto_blake2b(parts, DIGEST_BYTES, g_ranked.frames, frames * FRAME_BYTES);
    for (i = 0; i < g_ranked.hash_count; i++) {
        if (port_NetGetWord(g_ranked.hashes + i * HASH_BYTES) < end) {
            count = i + 1;
        }
    }
    crypto_blake2b(parts + DIGEST_BYTES, DIGEST_BYTES, g_ranked.hashes,
                   count * HASH_BYTES);
    port_RankedClear(parts + 2 * DIGEST_BYTES, DIGEST_BYTES);
    port_NetPutWord(parts + 2 * DIGEST_BYTES, end);
    crypto_blake2b(digest, DIGEST_BYTES, parts, sizeof(parts));
    port_Log("ranked: the match's digest: %u frames' inputs, %u hashes, to "
             "frame %u\n",
             frames, count, end);
}

/* The match's end (port_ranked.h): its report made of why it ended, the
 * winner the session saw, the rule it held and the digest, and asked of
 * the lobby until it has it. */
void port_RankedMatchEnds(s32 end, s32 winner, s32 met) {
    RankedReport *report = &g_ranked.report;
    char hex[2 * DIGEST_BYTES + 1];
    u32 left = port_SessionLeftAt();

    report->rule = PORT_MATCH_FIRST_TO;
    report->count = g_ranked.count;
    report->winner = winner;
    report->frames = 0;
    port_RankedClear(report->digest, DIGEST_BYTES);
    if (!met) {
        report->reason = PORT_RANKED_NOT_MET;
        report->winner = PORT_ROOM_NOBODY;
    } else if (end == PORT_NETPLAY_END_MATCH && left != 0) {
        report->reason = PORT_RANKED_ENDED;
        report->frames = left;
        port_RankedDigest(left, report->digest);
        /* the rule the session held: the host's word, which must be the
         * ladder's */
        report->rule = port_GetMatchRule();
        report->count = port_GetMatchCount();
    } else if (end == PORT_NETPLAY_END_QUIT) {
        report->reason = PORT_RANKED_I_LEFT;
    } else if (
        end == PORT_NETPLAY_END_PEER_LEFT || end == PORT_NETPLAY_END_SILENT) {
        report->reason = PORT_RANKED_OTHER_LEFT;
    } else {
        /* a desync, or back at the top menu before the match was decided
         * (whoever took the way there, which the two cannot tell apart):
         * nobody's, and void where both say so */
        report->reason = PORT_RANKED_DESYNC;
        report->winner = PORT_ROOM_NOBODY;
    }
    if (g_ranked.lie && (report->winner == PORT_ROOM_WON_FIRST ||
                         report->winner == PORT_ROOM_WON_SECOND)) {
        report->winner =
            PORT_ROOM_WON_FIRST + PORT_ROOM_WON_SECOND - report->winner;
        port_Log("ranked: (a test's lie: player %d is reported the "
                 "winner)\n",
                 report->winner + 1);
    }
    g_ranked.has_report = TRUE;
    g_ranked.playing = FALSE;
    port_RankedPutHex(hex, report->digest, DIGEST_BYTES);
    port_Log(
        "ranked: match %u ended (%s): reported %s, digest %s\n", g_ranked.match,
        report->reason == PORT_RANKED_ENDED        ? "played to its end"
        : report->reason == PORT_RANKED_I_LEFT     ? "this player left"
        : report->reason == PORT_RANKED_OTHER_LEFT ? "the other left"
        : report->reason == PORT_RANKED_NOT_MET    ? "never met"
                                                   : "not to an end",
        report->winner == PORT_ROOM_WON_FIRST    ? "player 1 won"
        : report->winner == PORT_ROOM_WON_SECOND ? "player 2 won"
        : report->winner == PORT_ROOM_DRAW       ? "a draw"
                                                 : "no winner",
        hex);
    port_RankedAsk(ACTION_REPORT);
}
