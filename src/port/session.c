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
 * session, and so are the gameplay switches unlock_all and
 * stage_select_everywhere (port_play.h): a guest plays with the host's
 * unlocks whatever its own settings say. The host's fight_music
 * (port_fight_music.h) holds likewise, and the stage pages' row of music
 * starts from it on both. Then both start the game from power
 * on, the host the first pad and the guest the second.
 *
 * From power on to the versus select screen the session presses for
 * both, the same buttons on both machines at the same frames: the options
 * page, the opening (whose movie is skipped: pad.c), the memory card's
 * questions and the top menu, down to versus. The select screen is the
 * match's set-up, both players choosing at once as on a couch, and after
 * a fight the game's own menu offers the rematch; a way back to the top
 * menu comes straight back to versus. In a fight Start is nobody's: the
 * pause menu would stop the other player's game too.
 *
 * Begun from the window's online pages, the meeting ends in a lobby
 * before the game: both players' names, and each says ready (or leaves)
 * before the host says go. Such a session's host also sets the rule that
 * a way back to the top menu after the select screen is the session's
 * end, on both machines at the same frame (port_SessionInput), and both
 * go back to their own game (online.c). */
#include "common.h"
#include "port_card.h"
#include "port_disc.h"
#include "port_host.h"
#include "port_net.h"
#include "port_room.h"
#include "bb2.h"

/* The protocol's own number: two programs that differ in it do not meet.
 * 7 (main's 6 and more): a guest that finds the host is not its kind
 * says so, either side gives up on the other after SILENT_GIVE_UP_MS of
 * silence, the disc's sum is of its data alone, and the two may meet by
 * way of the lobby (relay.c). 8: the players' names, the lobby before the
 * game (each ready or gone), and the host's rules. 9: the welcome carries
 * the host's match rule (port_match.h). 10: a room of several's match
 * (the lobby's version 2; NETPLAY.md, section 16): the rule that the
 * match's end is the session's, with who won in the state, and a player
 * who leaves in the middle of a fight. 11: the rule may be Crew, whose
 * crews the game's state carries. 12: the rule is chosen on a page before
 * the first select, by the host's pad; a crew's picks are all different
 * characters; a Stock or Crew fight's winner keeps his wounds, in the
 * state; a Crew match's end goes back to the top menu, which ends a
 * session of two (port_match.h). 13: the host's teams (port_duo.h) in
 * the welcome's match word, two on two's four fighters in the state, the
 * partners the CPU's (a session's input has two pads). 14: four players
 * (docs/NETPLAY.md, section 18): on four PCs, the host meeting three
 * guests, a link each, and passing each one's inputs to the others; or
 * two on each of two PCs. The welcome says the session's pads and the
 * guest's own (MEET_SEATS), the host's lobby how many are here and the
 * session's margin, a second guest at the host's first port is sent on to
 * the next (KIND_MOVE), netplay's datagrams carry each pad's inputs, and
 * the spectators' feed four pads a frame (the lobby's version 4). */
#define SESSION_PROTOCOL 14
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
#define PUNCH_EVERY_MS 100
#define STATUS_EVERY_MS 16
#define LOBBY_EVERY_MS 100
/* The guest gives up when nobody has answered for this long; the host
 * waits as long as it is left to (closing the window ends it), or, with
 * no window, this long. */
#define JOIN_GIVE_UP_MS 30000
#define HOST_GIVE_UP_MS 600000
/* Once the two have met, either gives up on the other after this long
 * with nothing heard: the host waits for another guest, the guest ends. */
#define SILENT_GIVE_UP_MS 15000
/* The disc is told from another by a sum of its sectors: every this
 * many from the first, and the volume's own description. */
#define DISC_SAMPLE_EVERY 997
#define DISC_VOLUME_SECTOR 16
#define FNV_OFFSET 0xCBF29CE484222325ULL
#define FNV_PRIME 0x100000001B3ULL
#define BITS_PER_BYTE 8
#define WORD_BYTES 4
#define WORD_BITS 32
#define STATUS_BYTES 400
#define MS_PER_SECOND 1000
#define SEED_MASK 0xFFFF
#define PERCENT 100
#define SHAPE_SHIFT 16
#define SHAPE_MASK 0xFFFF
#define DECIMAL 10
/* The setup code (port_SessionSetupCode): each sum folded to this many
 * bits, written as four of Crockford's base-32 digits. */
#define CODE_BITS 20
#define CODE_DIGITS 4
#define CODE_DIGIT_BITS 5
#define CODE_DIGIT_MASK 0x1F
/* a part with the dash before it */
#define CODE_PART (CODE_DIGITS + 1)
/* The disc's primary volume descriptor, in its sector's data: its mark
 * and the size of the volume in sectors. */
#define SECTOR_DATA 24
#define VOLUME_SIZE 80
#define FORM1_BYTES 2048
#define FORM2_BYTES 2324
#define SUBHEADER_BYTES 8
#define WHY_BYTES 400

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
    KIND_GO,
    KIND_LOBBY, /* the host's, in the lobby: whether its player is ready,
                 * how many are here, the session's margin */
    KIND_LEAVE, /* either's: its player has left the lobby */
    KIND_MOVE   /* the host's: this port has its guest, go to the next */
};

/* KIND_LOBBY's number: the players here, and the session's, a byte each;
 * its word after the header, the shape every program cuts faces off at. */
#define LOBBY_BYTES (PORT_NET_HEADER_BYTES + WORD_BYTES)
#define LOBBY_OF_SHIFT 8

/* MEET_SEATS: the session's pads (2 or 4), its PCs, and in a welcome the
 * guest's own pads (a bit each), a byte each. */
#define SEATS_PCS_SHIFT 8
#define SEATS_MASK_SHIFT 16
#define BYTE_MASK 0xFF

/* The pads a program plays, a bit each: the first, the second, the
 * first and third (a team), the second and fourth. */
#define MASK_FIRST 0x1U
#define MASK_SECOND 0x2U
#define MASK_FIRST_THIRD 0x5U
#define MASK_SECOND_FOURTH 0xAU

/* The host's guests: a link each (net.c). */
#define SESSION_GUESTS PORT_NET_LINKS

/* What the host keeps of each guest while all are met: whether it is in
 * and has the cards, whether it is ready, its name and its picture's
 * shape, and when it was last heard and told of the lobby. */
typedef struct {
    s32 met;
    s32 ready;
    u32 shape;
    u32 heard_at;
    u32 lobby_at;
    char name[PORT_SESSION_NAME_BYTES];
} SessionGuest;

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
 * stage the stages start as (port_stage_choice.h), the welcome's gameplay
 * switches (port_play.h), the welcome's fight_music (port_fight_music.h),
 * the time of day the stages start at (port_stage_choice.h), the match
 * rule (port_match.h), the welcome's rules (PORT_SESSION_RULE_...), the
 * version's text, and last the player's name. */
enum {
    MEET_BUILD = PORT_NET_HEADER_BYTES,
    MEET_DISC = MEET_BUILD + 2 * WORD_BYTES,
    MEET_MODS = MEET_DISC + 2 * WORD_BYTES,
    MEET_DELAY = MEET_MODS + 2 * WORD_BYTES,
    MEET_SEED = MEET_DELAY + WORD_BYTES,
    MEET_CARDS = MEET_SEED + WORD_BYTES,
    MEET_SHAPE = MEET_CARDS + WORD_BYTES,
    MEET_KIND = MEET_SHAPE + WORD_BYTES,
    MEET_PLAY = MEET_KIND + WORD_BYTES,
    MEET_MUSIC = MEET_PLAY + WORD_BYTES,
    MEET_TIME = MEET_MUSIC + WORD_BYTES,
    MEET_MATCH = MEET_TIME + WORD_BYTES,
    MEET_RULES = MEET_MATCH + WORD_BYTES,
    MEET_SEATS = MEET_RULES + WORD_BYTES,
    MEET_VERSION = MEET_SEATS + WORD_BYTES,
    MEET_NAME = MEET_VERSION + VERSION_BYTES,
    MEET_BYTES = MEET_NAME + PORT_SESSION_NAME_BYTES
};

static struct {
    s32 role;
    s32 differ;                 /* --session-differ: PORT_SESSION_DIFFER_... */
    const PortDisc *disc_image; /* the disc the game runs from */
    char setup[PORT_SETUP_CODE_BYTES]; /* this program's setup code */
    char mapped[STATUS_BYTES];         /* what the window says of its router */
    char path[STATUS_BYTES];           /* the way the two found to each other */
    PortNetAddress punch_to;           /* --punch-to */
    s32 has_punch_to;
    PortNetAddress address;
    u64 build;
    u64 disc;
    u64 mods; /* the mods that change how the game plays */
    u32 delay;
    u32 seed;
    u32 cards;  /* a bit a slot that has a card */
    u32 shape;  /* this program's picture's shape, as MEET_SHAPE has it */
    u32 kind;   /* the host's kind of stage the stages start as */
    u32 play;   /* the host's gameplay switches (port_PlaySwitchBits) */
    u32 music;  /* the host's fight_music (port_FightMusicBits) */
    u32 time;   /* the host's time of day the stages start at */
    u32 rules;  /* the host's PORT_SESSION_RULE_... */
    u32 match;  /* the host's match rule (port_MatchBits) */
    u32 wider;  /* the wider of the two players' */
    u32 have;   /* chunks the guest has */
    u32 needed; /* chunks there are to have */
    u8 map[CHUNK_MAP_BYTES];
    u32 next_chunk; /* the host's turn round them */
    u32 status_at;
    s32 summed;   /* the sums are worked out (they do not change) */
    s32 seed_set; /* --net-seed */
    u32 seed_asked;
    s32 canceled; /* the player gave the meeting up */
    s32 phase;    /* PORT_SESSION_PHASE_... */
    s32 peer_ready;
    char name[PORT_SESSION_NAME_BYTES];
    char peer_name[PORT_SESSION_NAME_BYTES];
    /* four players (section 18): the session's pads and PCs (the host's
     * --players), this program's pads (a bit each), the guests (the
     * host's links), and the players here; a guest heard the host's
     * lobby with every player in it, and the shape it said */
    s32 pads;
    s32 pcs;
    u32 mask;
    s32 guests;
    s32 here;
    s32 all_here;
    u32 peer_shape;
    SessionGuest guest[SESSION_GUESTS];
} g_session = {.pads = 2, .pcs = 2};

s32 (*g_port_session_ready_hook)(void);

static s32 port_HostHere(void);

/* The host's two cards, and what the guest is given of them: memory the
 * system gives as the session begins (none of the state's), copied into
 * the cards' own as the game starts. */
static u8 (*g_session_cards)[PORT_CARD_IMAGE_SIZE];

void port_SetSession(s32 role, const PortNetAddress *address) {
    g_session.role = role;
    g_session.address = *address;
}

s32 port_GetSessionRole(void) { return g_session.role; }

void port_SetSessionDiffer(s32 what) { g_session.differ = what; }

void port_SetSessionDisc(const PortDisc *disc) { g_session.disc_image = disc; }

static void port_CopyName(char *out, const char *text) {
    u32 i;

    for (i = 0; text != NULL && text[i] != 0 && i + 1 < PORT_SESSION_NAME_BYTES;
         i++) {
        out[i] = text[i] >= ' ' && text[i] <= '~' ? text[i] : '?';
    }
    out[i] = 0;
}

void port_SetSessionPlayer(const char *name) {
    port_CopyName(g_session.name, name);
}

void port_SetSessionSeed(u32 seed) {
    g_session.seed_asked = seed & SEED_MASK;
    g_session.seed_set = 1;
}

void port_SetSessionRules(u32 rules) { g_session.rules = rules; }

/* The host's --players (port_net.h): 2, 4 on four PCs, or 4 on two. */
void port_SetSessionPlayers(s32 pads, s32 pcs) {
    g_session.pads = pads == 4 ? 4 : 2;
    g_session.pcs = g_session.pads == 4 && pcs == 4 ? 4 : 2;
}

s32 port_SessionPcs(void) { return g_session.pcs; }

s32 port_SessionPads(void) { return g_session.pads; }

/* (port_match.h) */
s32 port_SessionIsRoomMatch(void) {
    return g_session.role != PORT_SESSION_NONE &&
           (g_session.rules & PORT_SESSION_RULE_ONE_MATCH) != 0;
}

s32 port_SessionIsOn(void) { return g_session.role != PORT_SESSION_NONE; }

void port_CancelSession(void) { g_session.canceled = 1; }

s32 port_SessionIsCanceled(void) { return g_session.canceled; }

void port_SetSessionPhase(s32 phase) { g_session.phase = phase; }

/* The player's answer in the lobby: ready at once with no pages. */
static s32 port_SessionReady(void) {
    return g_port_session_ready_hook != NULL ? g_port_session_ready_hook()
                                             : PORT_SESSION_READY;
}

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

/* A sector's data into the disc's sum: its subheader and what it carries
 * for the game, its 2048 bytes (form 1) or 2324 (form 2). Not its sync,
 * its header's address nor its error codes (EDC and ECC), which follow
 * from where and what it is, and which one dumping program writes again
 * and another keeps as read: two dumps of one disc agree. */
static u64 port_SumSector(u64 sum, const u8 *raw) {
    const u8 *subheader = raw + PORT_DISC_SYNC_BYTES + PORT_DISC_HEADER_BYTES;
    u32 bytes = (subheader[PORT_DISC_SUBMODE] & PORT_DISC_SUBMODE_FORM2)
                    ? FORM2_BYTES
                    : FORM1_BYTES;

    sum = port_SumBytes(sum, subheader, SUBHEADER_BYTES);
    return port_SumBytes(sum, raw + SECTOR_DATA, bytes);
}

/* The disc: the size of its volume as its own description says (sectors
 * a dump adds after the volume, its run-out, are no part of it), the
 * description, and a sector in every DISC_SAMPLE_EVERY of the volume,
 * each by its data (port_SumSector). Two images of the same disc agree
 * however they were dumped; a disc of another region, a hacked one or
 * another game does not. It is the disc as it is, with no mod's stage
 * files in it: those are the gameplay mods' sum. */
static u64 port_SessionDisc(const PortDisc *disc) {
    static u8 raw[PORT_DISC_SECTOR_BYTES];
    s32 (*override)(u32 sector, u8 *raw) = g_port_disc_override;
    u32 reads = g_port_disc_sectors_read;
    u32 sectors = disc->sector_count;
    u64 sum = FNV_OFFSET;
    u32 sector;

    g_port_disc_override = NULL;
    if (port_ReadDiscSector(disc, DISC_VOLUME_SECTOR, raw) == PORT_DISC_OK) {
        const u8 *volume = raw + SECTOR_DATA;
        u32 size = port_NetGetWord(volume + VOLUME_SIZE);

        if (volume[0] == 1 && volume[1] == 'C' && volume[2] == 'D' &&
            size != 0 && size <= sectors) {
            sectors = size;
        }
        sum = port_SumSector(sum, raw);
    }
    sum = port_SumWord(sum, sectors);
    for (sector = 0; sector < sectors; sector += DISC_SAMPLE_EVERY) {
        if (port_ReadDiscSector(disc, sector, raw) == PORT_DISC_OK) {
            sum = port_SumSector(sum, raw);
        }
    }
    g_port_disc_override = override;
    /* what was read here is none of the game's */
    g_port_disc_sectors_read = reads;
    return sum;
}

/* A sum folded to CODE_BITS and written as CODE_DIGITS digits of
 * Crockford's base 32 (no I, L, O or U: none is read for another). */
static void port_PutCodeDigits(char *out, u64 sum) {
    static const char digits[] = "0123456789ABCDEFGHJKMNPQRSTVWXYZ";
    u32 folded = 0;
    s32 i;

    while (sum != 0) {
        folded ^= (u32)(sum & ((1U << CODE_BITS) - 1));
        sum >>= CODE_BITS;
    }
    for (i = CODE_DIGITS - 1; i >= 0; i--) {
        out[i] = digits[folded & CODE_DIGIT_MASK];
        folded >>= CODE_DIGIT_BITS;
    }
}

static u32 port_SessionProtocol(void) {
    return SESSION_PROTOCOL +
           (g_session.differ == PORT_SESSION_DIFFER_PROTOCOL ? 1 : 0);
}

/* The sums as this program says them to the other (--session-differ
 * changes one, for a test), and the setup code of them,
 * `P10-BBBB-DDDD-MMMM` (port_net.h). */
static void port_SessionSums(void) {
    char *code = g_session.setup;
    u32 protocol;
    u32 head;

    if (g_session.summed) {
        return;
    }
    g_session.summed = 1;
    g_session.build = port_SessionBuild();
    g_session.disc = g_session.disc_image != NULL
                         ? port_SessionDisc(g_session.disc_image)
                         : 0;
    g_session.mods = port_WeaponsSessionHash() + port_ArenaSessionHash();
    if (g_session.differ == PORT_SESSION_DIFFER_BUILD) {
        g_session.build ^= 1;
    } else if (g_session.differ == PORT_SESSION_DIFFER_DISC) {
        g_session.disc ^= 1;
    } else if (g_session.differ == PORT_SESSION_DIFFER_MODS) {
        g_session.mods ^= 1;
    }
    /* `P10`, then each part after a dash */
    code[0] = 'P';
    protocol = port_SessionProtocol();
    head = 1;
    if (protocol >= DECIMAL) {
        code[head++] = (char)('0' + protocol / DECIMAL % DECIMAL);
    }
    code[head++] = (char)('0' + protocol % DECIMAL);
    code[head] = '-';
    port_PutCodeDigits(code + head + 1, g_session.build);
    code[head + CODE_PART] = '-';
    port_PutCodeDigits(code + head + CODE_PART + 1, g_session.disc);
    code[head + 2 * CODE_PART] = '-';
    port_PutCodeDigits(code + head + 2 * CODE_PART + 1, g_session.mods);
    code[head + 3 * CODE_PART] = 0;
}

u64 port_SessionDiscSum(void) {
    static u64 sum;
    static s32 is_known;

    if (!is_known && g_session.disc_image != NULL) {
        sum = port_SessionDisc(g_session.disc_image);
        is_known = TRUE;
    }
    return sum;
}

void port_SessionSetupCode(char *out) {
    u32 i;

    if (g_session.setup[0] == 0) {
        port_SessionSums();
    }
    for (i = 0; g_session.setup[i] != 0; i++) {
        out[i] = g_session.setup[i];
    }
    out[i] = 0;
}

/* --check-online (port_net.h). */
void port_PrintSessionCheck(void) {
    port_SessionSums();
    port_Print("%s, for %s, %d-bit; the online protocol %u\n", g_rb_version,
#if defined(_WIN32)
               "Windows",
#else
               "Linux",
#endif
               (s32)(sizeof(void *) * BITS_PER_BYTE), port_SessionProtocol());
    port_Print("  build          %08x%08x\n",
               (u32)(g_session.build >> WORD_BITS), (u32)g_session.build);
    port_Print("  disc           %08x%08x\n",
               (u32)(g_session.disc >> WORD_BITS), (u32)g_session.disc);
    port_Print("  gameplay mods  %08x%08x\n",
               (u32)(g_session.mods >> WORD_BITS), (u32)g_session.mods);
    port_Print("Your setup code: %s\n", g_session.setup);
    port_Print("Two players can play each other online when their codes are "
               "the same.\nIts parts: the protocol, then the build, the disc "
               "and the gameplay mods.\n");
    port_FlushOutput();
}

/* Why a host turned a guest away, in words for the log. */
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

/* ... and in a player's, for the screen. */
static const char *port_RefusalWords(s32 reason) {
    switch (reason) {
    case REFUSE_PROTOCOL:
        return "The other player's ReBushido is another version: you both "
               "need the same release.";
    case REFUSE_BUILD:
        return "The other player has another build of ReBushido: you both "
               "need the same release, for the same system.";
    case REFUSE_DISC:
        return "The other player's disc image is not the same as yours: you "
               "both need an image of the USA disc, SLUS-00663.";
    case REFUSE_MODS:
        return "The other player's gameplay mods (weapons, motions, stages, "
               "arenas) are not the same as yours.";
    default:
        return "The other player's game turned yours away.";
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

/* The pads the guest of the host's link `link` plays, a bit each: the
 * second on four PCs the link's own (the pads are the links' in order),
 * on two PCs the second and the fourth (each PC a team: docs/PORT.md,
 * "Two on two"), else the second. */
static u32 port_GuestMask(s32 link) {
    if (g_session.pads == 4 && g_session.pcs == 4) {
        return 1U << (link + 1);
    }
    return g_session.pads == 4 ? MASK_SECOND_FOURTH : MASK_SECOND;
}

/* The first of the pads of mask. */
static s32 port_FirstPad(u32 mask) {
    s32 pad;

    for (pad = 0; pad < PORT_INPUT_PADS; pad++) {
        if ((mask >> pad) & 1) {
            return pad;
        }
    }
    return 0;
}

/* A hello (the guest's) or a welcome (the host's). */
static void port_SendMeet(s32 kind) {
    u8 out[MEET_BYTES];
    u32 i;

    port_NetHeader(out, kind, 0, port_SessionProtocol());
    port_PutLong(out + MEET_BUILD, g_session.build);
    port_PutLong(out + MEET_DISC, g_session.disc);
    port_PutLong(out + MEET_MODS, g_session.mods);
    port_NetPutWord(out + MEET_DELAY, g_session.delay);
    port_NetPutWord(out + MEET_SEED, g_session.seed);
    port_NetPutWord(out + MEET_CARDS, g_session.cards);
    port_NetPutWord(out + MEET_SHAPE, g_session.shape);
    port_NetPutWord(out + MEET_KIND, g_session.kind);
    port_NetPutWord(out + MEET_PLAY, g_session.play);
    port_NetPutWord(out + MEET_MUSIC, g_session.music);
    port_NetPutWord(out + MEET_TIME, g_session.time);
    port_NetPutWord(out + MEET_RULES, g_session.rules);
    port_NetPutWord(out + MEET_MATCH, g_session.match);
    port_NetPutWord(
        out + MEET_SEATS,
        kind == KIND_WELCOME
            ? (u32)g_session.pads | ((u32)g_session.pcs << SEATS_PCS_SHIFT) |
                  (port_GuestMask(port_NetLink()) << SEATS_MASK_SHIFT)
            : 0);
    port_PutVersion(out + MEET_VERSION);
    for (i = 0; i < PORT_SESSION_NAME_BYTES; i++) {
        out[MEET_NAME + i] = (u8)g_session.name[i];
    }
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

    if (bytes < MEET_BYTES || protocol != port_SessionProtocol()) {
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
    g_session.peer_shape = port_NetGetWord(in + MEET_SHAPE);
    port_TakeShape(g_session.peer_shape);
    for (i = 0; i < PORT_SESSION_NAME_BYTES; i++) {
        version[i] = (char)in[MEET_NAME + i];
    }
    version[PORT_SESSION_NAME_BYTES - 1] = 0;
    port_CopyName(g_session.peer_name, version);
    return 0;
}

/* The lobby's state for the pages (port_net.h). */
void port_GetSessionLobby(PortSessionLobby *out) {
    u32 i;

    out->role = g_session.role;
    out->phase = g_session.phase;
    out->percent = g_session.needed == 0
                       ? PERCENT
                       : (s32)(g_session.have * PERCENT / g_session.needed);
    out->peer_ready = g_session.peer_ready;
    out->players = g_session.pads;
    out->here =
        g_session.role == PORT_SESSION_HOST ? port_HostHere() : g_session.here;
    for (i = 0; i < PORT_SESSION_NAME_BYTES; i++) {
        out->name[i] = g_session.name[i];
        out->peer_name[i] = g_session.peer_name[i];
    }
    for (i = 0; i < PORT_SETUP_CODE_BYTES; i++) {
        out->setup[i] = g_session.setup[i];
    }
    out->code = port_IsRelayOn() ? port_GetRelayCode() : "";
    out->path = g_session.path;
    out->mapped = g_session.mapped;
}

/* The player left the lobby: the other is told, a few times. */
static void port_SendLeave(void) { port_SendShort(KIND_LEAVE, 0, 0); }

/* The status line, now and then, to the window (port_net.h) or once to
 * the log where there is none. */
void port_SessionStatus(const char *text) {
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

/* The session cannot be set up: said in a player's words to the window,
 * which shows it until it has been read, or to the log (port_net.h). */
void port_SessionFail(const char *why) {
    port_Log("session: could not play online: %s\n", why);
    if (g_port_net_fail_hook != NULL) {
        g_port_net_fail_hook(why);
    }
}

/* printf's work into `out`. */
static void port_SessionText(char *out, u32 room, const char *format, ...) {
    PortArgs args;

    PORT_ARGS_START(args, format);
    port_FormatText(out, room, format, args);
    PORT_ARGS_END(args);
}

/* `what` and the address the session is at, `ADDRESS:PORT`, or the
 * relay's room. */
static void port_SessionAt(char *out, u32 room, const char *what) {
    char at[PORT_NET_ADDRESS_TEXT_BYTES];

    if (port_IsRelayOn()) {
        port_SessionText(
            out, room, "%s room %s (lobby)", what, port_GetRelayCode());
        return;
    }
    port_FormatNetAddress(&g_session.address, at, sizeof(at));
    port_SessionText(out, room, "%s %s", what, at);
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

/* The players here, the host's and its guests' that are in with the
 * cards, and the shape every program cuts faces off at: the widest of
 * the host's picture and theirs. */
static s32 port_HostHere(void) {
    s32 here = g_session.pads / g_session.pcs;
    s32 i;

    for (i = 0; i < g_session.guests; i++) {
        here += g_session.guest[i].met ? g_session.pads / g_session.pcs : 0;
    }
    return here;
}

static u32 port_HostWider(void) {
    s32 i;

    g_session.wider = g_session.shape;
    for (i = 0; i < g_session.guests; i++) {
        if (g_session.guest[i].met) {
            port_TakeShape(g_session.guest[i].shape);
        }
    }
    return g_session.wider;
}

/* The lobby, to the guest of the link chosen: whether the host's player
 * is ready, how many are here of how many, and the session's shape. */
static void port_SendLobby(s32 ready) {
    u8 out[LOBBY_BYTES];

    port_NetHeader(
        out, KIND_LOBBY, ready,
        (u32)port_HostHere() | ((u32)g_session.pads << LOBBY_OF_SHIFT));
    port_NetPutWord(out + PORT_NET_HEADER_BYTES, port_HostWider());
    port_NetSend(out, sizeof(out));
}

/* A link's guest's name, for the screen. */
static const char *port_GuestName(s32 link) {
    return g_session.guest[link].name[0] != 0 ? g_session.guest[link].name
                                              : "A player";
}

/* The host's link `link` waits for a guest again: at its own address, or
 * under the same room at the relay (its next meeting, port_HostAll). */
static void port_HostLinkAgain(s32 link) {
    PortNetAddress at = g_session.address;

    g_session.guest[link].met = 0;
    g_session.guest[link].ready = 0;
    g_session.guest[link].name[0] = 0;
    if (port_IsRelayOn()) {
        return;
    }
    if (at.port != 0) {
        at.port = (u16)(at.port + link);
    }
    port_NetSelectLink(link);
    port_SetNetListen(&at);
}

/* A guest that is in, while the host meets another or waits for all to be
 * ready: what it says read (ready, or gone), the lobby told it now and
 * then. FALSE where it has gone (it left, or said nothing for
 * SILENT_GIVE_UP_MS), the link waiting for another and why in `note`. */
static s32 port_HostKeep(
    s32 link, s32 ready, u32 wait_ms, char *note, u32 room) {
    SessionGuest *guest = &g_session.guest[link];
    u8 in[PORT_NET_DATAGRAM_MAX];
    s32 bytes;

    port_NetSelectLink(link);
    for (bytes = port_NetReceive(in, sizeof(in), wait_ms); bytes > 0;
         bytes = port_NetReceive(in, sizeof(in), 0)) {
        s32 detail;
        u32 number;
        s32 kind = port_NetReadHeader(in, bytes, &detail, &number);

        if (kind >= KIND_HELLO && kind <= KIND_MOVE) {
            guest->heard_at = port_NetClock();
        }
        if (kind == KIND_LEAVE) {
            port_Log("session: %s (link %d) left the lobby; waiting for "
                     "another\n",
                     port_GuestName(link), link + 1);
            port_SessionText(note, room, "%s left.", port_GuestName(link));
            port_HostLinkAgain(link);
            return FALSE;
        }
        if (kind == KIND_READY) {
            guest->ready = 1;
        }
    }
    if (port_NetClock() - guest->heard_at > SILENT_GIVE_UP_MS) {
        port_Log("session: %s (link %d) stopped answering; waiting for "
                 "another\n",
                 port_GuestName(link), link + 1);
        port_SessionText(
            note, room, "%s stopped answering.", port_GuestName(link));
        port_HostLinkAgain(link);
        return FALSE;
    }
    if (port_NetClock() - guest->lobby_at >= LOBBY_EVERY_MS) {
        port_SendLobby(ready);
        guest->lobby_at = port_NetClock();
    }
    return TRUE;
}

/* While the host meets one guest (its loops and the lobby's, relay.c):
 * every other guest that is in kept in (port_net.h). The link being met
 * stays the one chosen. */
void port_SessionKeepOthers(void) {
    char note[STATUS_BYTES];
    s32 link = port_NetLink();
    s32 i;

    if (g_session.role != PORT_SESSION_HOST || g_session.guests < 2) {
        return;
    }
    for (i = 0; i < g_session.guests; i++) {
        if (i != link && g_session.guest[i].met) {
            port_HostKeep(i, 0, 0, note, sizeof(note));
        }
    }
    port_NetSelectLink(link);
}

/* A second guest's hello at a port whose link has its guest (port_net.h's
 * stranger): it is told the port of a link that waits, by address alone
 * (the lobby gives each guest its own link). */
static void port_HostStranger(
    const PortNetAddress *from, const u8 *bytes, s32 count) {
    s32 link = port_NetLink();
    s32 detail;
    u32 number;
    s32 next;

    if (port_NetReadHeader(bytes, count, &detail, &number) != KIND_HELLO) {
        return;
    }
    for (next = 0; next < g_session.guests; next++) {
        PortNetAddress bound;
        s32 waits;

        if (next == link || g_session.guest[next].met) {
            continue;
        }
        port_NetSelectLink(next);
        waits = !port_NetIsFixed() && port_GetNetBound(&bound);
        port_NetSelectLink(link);
        if (waits) {
            u8 out[PORT_NET_HEADER_BYTES];

            port_NetHeader(out, KIND_MOVE, 0, bound.port);
            port_NetSendTo(from, out, sizeof(out));
            return;
        }
    }
}

/* The host lets the guest it had go (turned away, or gone quiet) and
 * waits for another: at its own address again, or under the same room
 * at the relay. 0 if the relay cannot be had again. */
static s32 port_HostWaitAgain(void) {
    u32 i;

    g_session.have = 0;
    g_session.next_chunk = 0;
    g_session.peer_ready = 0;
    g_session.peer_name[0] = 0;
    g_session.phase = PORT_SESSION_PHASE_WAITING;
    for (i = 0; i < CHUNK_MAP_BYTES; i++) {
        g_session.map[i] = 0;
    }
    if (port_IsRelayOn()) {
        return port_RelayMeet(PORT_SESSION_HOST, 1);
    }
    port_HostLinkAgain(port_NetLink());
    return 1;
}

/* The host's status where it waits: `at`, and how many are here. */
static void port_HostWaiting(char *out, u32 room, const char *note) {
    char at[STATUS_BYTES];
    char here[STATUS_BYTES];

    port_SessionAt(at, sizeof(at), "Hosting a game at");
    here[0] = 0;
    if (g_session.pads > 2) {
        port_SessionText(here, sizeof(here), " (%d of %d players here)",
                         port_HostHere(), g_session.pads);
    }
    if (note[0] != 0) {
        port_SessionText(out, room, "%s Still waiting: %s%s", note, at, here);
    } else {
        port_SessionText(
            out, room, "%s%s%s%s", at, here,
            g_session.mapped[0] != 0 ? ". " : "", g_session.mapped);
    }
}

/* The host's link chosen: waits for a hello, holds it to this program
 * and turns it away or lets it in; then sends its cards until the guest
 * has them all. A guest that goes quiet for SILENT_GIVE_UP_MS, or that
 * turns this host away, is let go, and another waited for. 1 once the
 * guest is in with the cards, or 0 if nobody came or the host gave up.
 * Every other guest in is kept in meanwhile. */
static s32 port_HostSession(char *note, u32 note_room) {
    char status[STATUS_BYTES];
    u8 in[PORT_NET_DATAGRAM_MAX];
    s32 link = port_NetLink();
    u32 start = port_NetClock();
    u32 sent_at = 0;
    u32 welcomed_at = 0;
    u32 heard_at = 0;
    u32 punched_at = 0;
    s32 met = 0;
    u32 i;

    g_session.have = 0;
    g_session.next_chunk = 0;
    g_session.peer_ready = 0;
    g_session.peer_name[0] = 0;
    for (i = 0; i < CHUNK_MAP_BYTES; i++) {
        g_session.map[i] = 0;
    }
    g_session.phase = PORT_SESSION_PHASE_WAITING;
    if (g_session.has_punch_to) {
        port_SetSessionPath("a punched hole (no lobby)");
    }
    for (;;) {
        u32 now = port_NetClock();
        s32 bytes;

        /* a room's member says it is still there while it meets, and the
         * guests in are kept in */
        port_RoomTick();
        port_SessionKeepOthers();
        port_NetSelectLink(link);
        bytes = port_NetReceive(in, sizeof(in), POLL_MS);
        if (g_session.canceled) {
            if (met) {
                port_SendLeave();
            }
            port_Log("session: the host gave the meeting up\n");
            return 0;
        }

        if (bytes > 0) {
            s32 detail;
            u32 number;
            s32 kind = port_NetReadHeader(in, bytes, &detail, &number);
            s32 reason;

            if (kind >= KIND_HELLO && kind <= KIND_MOVE) {
                heard_at = now;
            }
            if (kind == KIND_LEAVE && met) {
                port_Log("session: the guest left the lobby; waiting for "
                         "another\n");
                port_SessionText(
                    note, note_room, "%s left.",
                    g_session.peer_name[0] != 0 ? g_session.peer_name
                                                : "The other player");
                met = 0;
                if (!port_HostWaitAgain()) {
                    return 0;
                }
                continue;
            }
            if (kind == KIND_HELLO && !met) {
                reason = port_CheckMeet(in, bytes, number);
                if (reason != 0) {
                    port_SendShort(KIND_REFUSE, reason, 0);
                    port_Log("session: a guest was turned away: %s\n",
                             port_RefusalText(reason));
                    port_SessionText(
                        note, note_room, "A player was turned away: %s",
                        port_RefusalWords(reason));
                    if (port_IsRelayOn() && !port_HostWaitAgain()) {
                        return 0;
                    }
                    continue;
                }
                port_FixNetPeer();
                met = 1;
                note[0] = 0;
                g_session.phase = PORT_SESSION_PHASE_CARDS;
                if (g_session.guests > 1) {
                    port_Log("session: a guest has come (link %d, player "
                             "%d); sending it the memory cards\n",
                             link + 1, link + 2);
                } else {
                    port_Log("session: the guest has come; sending it the "
                             "memory cards\n");
                }
            }
            if (kind == KIND_REFUSE && met) {
                /* the guest found this host is not its kind */
                port_Log("session: the guest turned this host away: %s\n",
                         port_RefusalText(detail));
                port_SessionText(note, note_room,
                                 "The other player's game turned yours "
                                 "away: %s",
                                 port_RefusalWords(detail));
                met = 0;
                if (!port_HostWaitAgain()) {
                    return 0;
                }
                continue;
            }
            if (kind == KIND_HELLO && met) {
                port_SendMeet(KIND_WELCOME);
                welcomed_at = now;
            } else if (kind == KIND_CARD_ACK &&
                       (u32)bytes >= PORT_NET_HEADER_BYTES + CHUNK_MAP_BYTES) {
                for (i = 0; i < CHUNK_MAP_BYTES; i++) {
                    g_session.map[i] |= in[PORT_NET_HEADER_BYTES + i];
                }
                g_session.have = number;
            } else if (kind == KIND_READY && met) {
                g_session.peer_ready = 1;
            }
        }
        if (met && g_session.needed == g_session.have) {
            /* in, with the cards: the lobby (port_HostLobby) */
            SessionGuest *guest = &g_session.guest[link];

            guest->met = 1;
            guest->ready = g_session.peer_ready;
            guest->shape = g_session.peer_shape;
            guest->heard_at = now;
            guest->lobby_at = 0;
            port_CopyName(guest->name, g_session.peer_name);
            return 1;
        }
        if (!met && g_session.has_punch_to &&
            now - punched_at >= PUNCH_EVERY_MS) {
            u8 punch[PORT_NET_HEADER_BYTES];

            /* nothing the guest reads: it opens this side's router and
             * firewall to the guest's address, as the guest's hellos open
             * its own to this one */
            port_NetHeader(punch, PORT_NET_KIND_RELAY + 2, 0, 0);
            port_NetSendTo(&g_session.punch_to, punch, sizeof(punch));
            punched_at = now;
        }
        if (met && now - heard_at > SILENT_GIVE_UP_MS) {
            port_Log("session: the guest stopped answering; waiting for "
                     "another\n");
            port_SessionText(
                note, note_room, "The other player stopped answering.");
            met = 0;
            if (!port_HostWaitAgain()) {
                return 0;
            }
            continue;
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
            port_SessionProgress(
                status, sizeof(status),
                g_session.guests > 1 ? "A player has come: sending your "
                                       "memory cards"
                                     : "The other player has come: sending "
                                       "your memory cards");
        } else {
            g_session.phase = PORT_SESSION_PHASE_WAITING;
            port_HostWaiting(status, sizeof(status), note);
        }
        port_SessionStatus(status);
        if (g_port_net_wait_hook == NULL && now - start > HOST_GIVE_UP_MS) {
            port_Log("session: nobody came\n");
            return 0;
        }
    }
}

/* Every guest in: the lobby before the fight. Each guest is told who is
 * here and whether the host's player is ready, and says when its own is;
 * when all are, the host says go to all. 1 then; 0 if the host's player
 * gave it up or left; -1 if a guest went (its link waits for another). */
static s32 port_HostLobby(char *note, u32 room) {
    char status[STATUS_BYTES];
    s32 link;

    for (;;) {
        s32 ready = port_SessionReady();
        s32 all = TRUE;

        port_RoomTick();
        if (g_session.canceled || ready == PORT_SESSION_LEAVE) {
            for (link = 0; link < g_session.guests; link++) {
                port_NetSelectLink(link);
                port_SendLeave();
            }
            port_NetSelectLink(0);
            port_Log(
                "session: the host %s\n",
                g_session.canceled ? "gave the meeting up" : "left the lobby");
            g_session.canceled = 1;
            return 0;
        }
        g_session.phase = PORT_SESSION_PHASE_READY;
        for (link = 0; link < g_session.guests; link++) {
            if (!port_HostKeep(link, ready == PORT_SESSION_READY,
                               link == 0 ? POLL_MS : 0, note, room)) {
                port_NetSelectLink(0);
                return -1;
            }
            all = all && g_session.guest[link].ready;
        }
        g_session.peer_ready = all;
        if (all && ready == PORT_SESSION_READY) {
            /* the session's shape once more with go, for all */
            for (link = 0; link < g_session.guests; link++) {
                port_NetSelectLink(link);
                port_SendLobby(1);
                port_SendShort(KIND_GO, 0, port_HostWider());
            }
            port_NetSelectLink(0);
            if (g_session.guests > 1) {
                port_Log("session: all %d players are here and ready: go\n",
                         g_session.pads);
            }
            return 1;
        }
        if (g_session.guests > 1) {
            port_SessionText(
                status, sizeof(status), "%d of %d players here: %s",
                port_HostHere(), g_session.pads,
                all ? "all ready" : "waiting for all to be ready");
        } else {
            port_SessionText(
                status, sizeof(status), "%s is here: %s",
                g_session.guest[0].name[0] != 0 ? g_session.guest[0].name
                                                : "The other player",
                all ? "ready" : "not ready yet");
            port_CopyName(g_session.peer_name, g_session.guest[0].name);
        }
        port_SessionStatus(status);
    }
}

/* The host gave the meeting up, or could not go on with it: every guest
 * that is in is told, so that none waits for a game that will not come. */
static void port_HostLetAllGo(void) {
    s32 link;

    for (link = 0; link < g_session.guests; link++) {
        if (g_session.guest[link].met) {
            port_NetSelectLink(link);
            port_SendLeave();
        }
    }
    port_NetSelectLink(0);
}

/* The host: each of its links' guests met in turn (every way of a
 * session of two, the lobby's seat or the address's port of each), the
 * others kept in meanwhile; then the lobby of all. 1 once all have been
 * told to go, 0 if the host gave up. */
static s32 port_HostAll(void) {
    char note[STATUS_BYTES];
    s32 tried[SESSION_GUESTS];
    s32 link;

    note[0] = 0;
    for (link = 0; link < SESSION_GUESTS; link++) {
        tried[link] = 0;
        g_session.guest[link].met = 0;
        g_session.guest[link].ready = 0;
        g_session.guest[link].name[0] = 0;
    }
    /* the other guests' links: their sockets, at the ports after the
     * first's where it has one */
    for (link = 1; link < g_session.guests; link++) {
        PortNetAddress at = g_session.address;

        if (at.port != 0) {
            at.port = (u16)(at.port + link);
        }
        port_NetSelectLink(link);
        port_SetNetListen(&at);
        if (!port_StartNet()) {
            port_NetSelectLink(0);
            port_SessionFail("This PC's network port for another player "
                             "could not be opened: is another program "
                             "using it?");
            return 0;
        }
    }
    port_NetSelectLink(0);
    if (g_session.guests > 1 && !port_IsRelayOn()) {
        g_port_net_stranger_hook = port_HostStranger;
    }
    for (;;) {
        s32 got;

        for (link = 0; link < g_session.guests && g_session.guest[link].met;
             link++) {
        }
        if (link < g_session.guests) {
            port_NetSelectLink(link);
            if (port_IsRelayOn() &&
                !port_RelayMeet(PORT_SESSION_HOST, tried[link])) {
                port_HostLetAllGo();
                return 0;
            }
            tried[link] = 1;
            if (!port_HostSession(note, sizeof(note))) {
                port_HostLetAllGo();
                return 0;
            }
            continue;
        }
        got = port_HostLobby(note, sizeof(note));
        if (got >= 0) {
            g_port_net_stranger_hook = NULL;
            return got;
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
 * first inputs come). 1, or 0 if it was turned away or nobody answered,
 * said to the player (port_SessionFail). */
static s32 port_JoinSession(void) {
    char status[STATUS_BYTES];
    char why[WHY_BYTES];
    char at[PORT_NET_ADDRESS_TEXT_BYTES];
    u8 in[PORT_NET_DATAGRAM_MAX];
    u32 start = port_NetClock();
    u32 hello_at = 0;
    u32 ack_at = 0;
    u32 heard_at = 0;
    s32 welcomed = 0;

    g_session.phase = PORT_SESSION_PHASE_JOINING;
    port_SessionAt(status, sizeof(status), "Joining the game at");
    if (port_IsRelayOn()) {
        port_SessionText(at, sizeof(at), "room %s", port_GetRelayCode());
    } else {
        port_FormatNetAddress(&g_session.address, at, sizeof(at));
    }
    for (;;) {
        u32 now = port_NetClock();
        s32 bytes = port_NetReceive(in, sizeof(in), POLL_MS);
        s32 ready = PORT_SESSION_NOT_YET;

        port_RoomTick();
        if (g_session.canceled) {
            if (welcomed) {
                port_SendLeave();
            }
            port_Log("session: the guest gave the meeting up\n");
            return 0;
        }
        if (welcomed && g_session.have == g_session.needed) {
            ready = port_SessionReady();
            if (ready == PORT_SESSION_LEAVE) {
                port_SendLeave();
                port_Log("session: the guest left the lobby\n");
                g_session.canceled = 1;
                return 0;
            }
        }
        if (bytes > 0) {
            s32 detail;
            u32 number;
            s32 kind = port_NetReadHeader(in, bytes, &detail, &number);

            if (kind >= KIND_HELLO && kind <= KIND_MOVE) {
                heard_at = now;
            }
            if (kind == KIND_MOVE && !welcomed && number != 0 &&
                number <= 0xFFFF && !port_IsRelayOn()) {
                /* the host's port has its guest: on to the one it says */
                g_session.address.port = (u16)number;
                port_SetNetPeerNow(&g_session.address);
                port_FormatNetAddress(&g_session.address, at, sizeof(at));
                port_Log(
                    "session: the host's port has a player: on to %s\n", at);
                continue;
            }
            if (kind == KIND_LEAVE && welcomed) {
                port_Log("session: the host left the lobby\n");
                port_SessionText(
                    why, sizeof(why),
                    "%s closed the room before the game began.",
                    g_session.peer_name[0] != 0 ? g_session.peer_name
                                                : "The host");
                port_SessionFail(why);
                return 0;
            }
            if (kind == KIND_LOBBY && welcomed) {
                g_session.peer_ready = detail != 0;
                g_session.here = (s32)(number & BYTE_MASK);
                if (bytes >= LOBBY_BYTES &&
                    g_session.here >=
                        (s32)(number >> LOBBY_OF_SHIFT & BYTE_MASK)) {
                    /* every player here: the shape all cut faces off at */
                    g_session.wider =
                        port_NetGetWord(in + PORT_NET_HEADER_BYTES);
                    g_session.all_here = 1;
                }
            }
            if (kind == KIND_REFUSE) {
                port_Log("session: the host turned this program away: %s\n",
                         port_RefusalText(detail));
                port_SessionText(
                    why, sizeof(why),
                    "The host turned you away. %s Your setup "
                    "code is %s: compare it with the host's "
                    "(Play online page, or rebushido "
                    "--check-online).",
                    port_RefusalWords(detail), g_session.setup);
                port_SessionFail(why);
                return 0;
            }
            if (kind == KIND_WELCOME && !welcomed) {
                s32 reason = port_CheckMeet(in, bytes, number);

                if (reason != 0) {
                    port_Log("session: the host is not this program's kind: "
                             "%s\n",
                             port_RefusalText(reason));
                    /* and the host is told, so that it says so too */
                    port_SendShort(KIND_REFUSE, reason, 0);
                    port_SessionText(
                        why, sizeof(why),
                        "%s Your setup code is %s: compare it "
                        "with the host's.",
                        port_RefusalWords(reason), g_session.setup);
                    port_SessionFail(why);
                    return 0;
                }
                welcomed = 1;
                g_session.phase = PORT_SESSION_PHASE_CARDS;
                g_session.rules = port_NetGetWord(in + MEET_RULES);
                g_session.delay = port_NetGetWord(in + MEET_DELAY);
                g_session.seed = port_NetGetWord(in + MEET_SEED);
                g_session.cards = port_NetGetWord(in + MEET_CARDS);
                g_session.kind = port_NetGetWord(in + MEET_KIND);
                g_session.play = port_NetGetWord(in + MEET_PLAY);
                g_session.music = port_NetGetWord(in + MEET_MUSIC);
                g_session.time = port_NetGetWord(in + MEET_TIME);
                g_session.match = port_NetGetWord(in + MEET_MATCH);
                number = port_NetGetWord(in + MEET_SEATS);
                g_session.pads = (s32)(number & BYTE_MASK) == 4 ? 4 : 2;
                g_session.pcs =
                    (s32)(number >> SEATS_PCS_SHIFT & BYTE_MASK) == 4 ? 4 : 2;
                g_session.mask = number >> SEATS_MASK_SHIFT & BYTE_MASK;
                if (g_session.mask == 0) {
                    g_session.mask = MASK_SECOND;
                }
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
                       welcomed && g_session.have == g_session.needed &&
                       g_session.all_here) {
                /* the host has started: its inputs are coming */
                if (kind == KIND_GO && number != 0) {
                    g_session.wider = number;
                }
                return 1;
            }
        }
        if (!welcomed && now - hello_at >= HELLO_EVERY_MS) {
            port_SendMeet(KIND_HELLO);
            hello_at = now;
        }
        if (welcomed && now - ack_at >= ACK_EVERY_MS) {
            port_SendCardAck();
            if (g_session.have == g_session.needed &&
                ready == PORT_SESSION_READY && g_session.all_here) {
                port_SendShort(KIND_READY, 0, 0);
            }
            ack_at = now;
        }
        if (welcomed && g_session.have == g_session.needed) {
            g_session.phase = PORT_SESSION_PHASE_READY;
            if (g_session.pads > 2 && !g_session.all_here) {
                port_SessionText(
                    status, sizeof(status), "%s's room: %d of %d players here",
                    g_session.peer_name[0] != 0 ? g_session.peer_name
                                                : "The host",
                    g_session.here, g_session.pads);
            } else {
                port_SessionText(
                    status, sizeof(status), "%s's room: %s",
                    g_session.peer_name[0] != 0 ? g_session.peer_name
                                                : "The host",
                    g_session.peer_ready ? "the host is ready"
                                         : "the host is not "
                                           "ready yet");
            }
        } else if (welcomed) {
            port_SessionProgress(status, sizeof(status),
                                 "The host has let you in: receiving its "
                                 "memory cards");
        } else if (now - start > JOIN_GIVE_UP_MS) {
            port_Log("session: nobody answered at %s\n", at);
            port_SessionText(
                why, sizeof(why),
                "Nobody answered at %s in %u seconds. Is the other player "
                "hosting, with the same port, and can your PCs reach each "
                "other (the router's port mapped or forwarded, or a lobby "
                "room)?",
                at, JOIN_GIVE_UP_MS / MS_PER_SECOND);
            port_SessionFail(why);
            return 0;
        }
        if (welcomed && now - heard_at > SILENT_GIVE_UP_MS) {
            /* (in the lobby too: a host of four meeting the others still
             * tells its guests of it every 100 ms) */
            port_Log("session: the host stopped answering\n");
            port_SessionText(why, sizeof(why),
                             "The host stopped answering, after letting "
                             "you in (%u seconds with nothing heard).",
                             SILENT_GIVE_UP_MS / MS_PER_SECOND);
            port_SessionFail(why);
            return 0;
        }
        port_SessionStatus(status);
    }
}

/* ---- the start ---- */

void port_SetSessionPunchTo(const PortNetAddress *at) {
    g_session.punch_to = *at;
    g_session.has_punch_to = 1;
}

void port_SetSessionMapped(const char *text) {
    u32 i;

    for (i = 0; text[i] != 0 && i + 1 < sizeof(g_session.mapped); i++) {
        g_session.mapped[i] = text[i];
    }
    g_session.mapped[i] = 0;
}

void port_SetSessionPath(const char *text) {
    u32 i;

    for (i = 0; text[i] != 0 && i + 1 < sizeof(g_session.path); i++) {
        g_session.path[i] = text[i];
    }
    g_session.path[i] = 0;
}

const char *port_GetSessionPath(void) {
    if (g_session.path[0] != 0) {
        return g_session.path;
    }
    return g_session.role == PORT_SESSION_NONE ? "" : "direct";
}

/* A session over in the running program (port_net.h). */
void port_ResetSession(void) {
    u32 i;

    g_session.role = PORT_SESSION_NONE;
    port_ClearNetAddress(&g_session.address);
    g_session.mapped[0] = 0;
    g_session.path[0] = 0;
    g_session.has_punch_to = 0;
    g_session.seed = 0;
    g_session.cards = 0;
    g_session.rules = 0;
    g_session.have = 0;
    g_session.needed = 0;
    g_session.next_chunk = 0;
    g_session.status_at = 0;
    g_session.canceled = 0;
    g_session.phase = PORT_SESSION_PHASE_NONE;
    g_session.peer_ready = 0;
    g_session.peer_name[0] = 0;
    for (i = 0; i < CHUNK_MAP_BYTES; i++) {
        g_session.map[i] = 0;
    }
    g_session.pads = 2;
    g_session.pcs = 2;
    g_session.mask = 0;
    g_session.guests = 0;
    g_session.here = 0;
    g_session.all_here = 0;
    for (i = 0; i < SESSION_GUESTS; i++) {
        g_session.guest[i].met = 0;
        g_session.guest[i].ready = 0;
        g_session.guest[i].name[0] = 0;
    }
}

/* The meeting (port_net.h): the sums, the other found by way of the lobby
 * or not and held to this program, the cards and the seed. */
s32 port_MeetSession(const PortDisc *disc, u32 delay) {
    PortRemasterView view;
    u32 chunk;
    s32 slot;
    s32 ok;

    g_session.disc_image = disc;
    g_session.canceled = 0;
    g_session.peer_ready = 0;
    g_session.peer_name[0] = 0;
    g_session.have = 0;
    g_session.next_chunk = 0;
    for (chunk = 0; chunk < CHUNK_MAP_BYTES; chunk++) {
        g_session.map[chunk] = 0;
    }
    port_SessionSums();
    port_Log("session: this program's setup code is %s\n", g_session.setup);
    g_session.delay = delay;
    g_session.kind = (u32)port_GetStageKindDefault();
    g_session.play = port_PlaySwitchBits();
    g_session.music = port_FightMusicBits();
    g_session.time = (u32)port_GetStageTimeDefault();
    g_session.match = port_MatchBits();
    g_session.here = 0;
    g_session.all_here = 0;
    if (g_session.role == PORT_SESSION_HOST) {
        g_session.guests = g_session.pcs - 1;
        g_session.mask = g_session.pads == 4 && g_session.pcs == 2
                             ? MASK_FIRST_THIRD
                             : MASK_FIRST;
        if (g_session.pads == 4) {
            /* four players are two on two, every fighter a player's */
            g_session.match =
                port_MatchBitsTeams(g_session.match, PORT_TEAMS_TWO_PADS);
            port_Log("session: four players, on %d PCs: this program plays "
                     "%s\n",
                     g_session.pcs,
                     g_session.pcs == 4 ? "the first"
                                        : "the first and the third (a team)");
        }
    }
    port_RemasterGetView(&view);
    g_session.shape =
        ((u32)view.aspect_w << SHAPE_SHIFT) | ((u32)view.aspect_h & SHAPE_MASK);
    g_session.wider = g_session.shape;
    if (g_session_cards == NULL) {
        g_session_cards =
            port_HostAllocate(PORT_CARD_SLOTS * PORT_CARD_IMAGE_SIZE);
    }
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
        g_session.seed = g_session.seed_set
                             ? g_session.seed_asked
                             : (port_HostTicks() ^ port_NetClock()) & SEED_MASK;
        ok = port_HostAll();
    } else {
        ok = (!port_IsRelayOn() || port_RelayMeet(PORT_SESSION_JOIN, 0)) &&
             port_JoinSession();
    }
    return ok;
}

/* The session as the game starts (port_net.h): the host's cards in both
 * slots, netplay on, the host's switches held and the margin fixed. */
void port_ApplySession(void) {
    s32 slot;

    for (slot = 0; slot < PORT_CARD_SLOTS; slot++) {
        port_SetCardImage(
            slot,
            ((g_session.cards >> slot) & 1) ? g_session_cards[slot] : NULL);
    }
    s32 sources[PORT_INPUT_PADS];
    s32 pad;

    port_SetNetplay(
        g_session.role == PORT_SESSION_HOST ? 0 : 1, g_session.delay);
    /* the session's pads: this program's own, and where each other's
     * comes from: on the host, each guest's link (the second pad the
     * first link's on four PCs, ...); on a guest, its one link */
    for (pad = 0; pad < PORT_INPUT_PADS; pad++) {
        if ((g_session.mask >> pad) & 1) {
            sources[pad] = -1;
        } else if (g_session.role != PORT_SESSION_HOST) {
            sources[pad] = 0;
        } else {
            sources[pad] = g_session.pcs == 4 ? pad - 1 : 0;
        }
    }
    if (g_session.mask != 0) {
        port_SetNetplayPads(g_session.pads, sources);
    }
    port_HoldStageKindDefault((s32)g_session.kind);
    port_HoldPlaySwitches(g_session.play);
    port_HoldFightMusic(g_session.music);
    port_HoldStageTimeDefault((s32)g_session.time);
    port_HoldMatch(g_session.match);
    port_Log("session: the host's match rule is %s, %d\n",
             port_GetMatchRule() == PORT_MATCH_STOCK      ? "stock"
             : port_GetMatchRule() == PORT_MATCH_FIRST_TO ? "first_to"
             : port_GetMatchRule() == PORT_MATCH_CREW     ? "crew"
                                                          : "single",
             port_GetMatchCount());
    port_Log(
        "session: the host's teams are %s\n",
        port_GetTeamsDefault() == PORT_TEAMS_ONE ? "one on one" : "two on two");
    port_Log("session: the host's unlock_all is %s, its "
             "stage_select_everywhere %s, its fight_music %s\n",
             (g_session.play & PORT_PLAY_BIT_UNLOCK_ALL) ? "on" : "off",
             (g_session.play & PORT_PLAY_BIT_STAGE_PAGES) ? "on" : "off",
             port_GetFightMusicRule() == PORT_FIGHT_MUSIC_STAGE  ? "stage"
             : port_GetFightMusicRule() == PORT_FIGHT_MUSIC_NONE ? "none"
                                                                 : "original");
    port_RemasterFixMargin((s32)(g_session.wider >> SHAPE_SHIFT),
                           (s32)(g_session.wider & SHAPE_MASK));
    if (g_session.pads > 2) {
        s32 players = g_session.pads;
        s32 pcs = g_session.pcs;
        const char *pads = port_NetPadsText(g_session.mask);

        port_Log("session: %d players on %d PCs; this program's pads %s\n",
                 players, pcs, pads);
    }
    port_Log("session: begun: this program is player %d, with a delay of %u "
             "frames and the seed %u, by way of %s; the host's memory cards "
             "(%s%s)\n",
             port_FirstPad(g_session.mask) + 1, g_session.delay, g_session.seed,
             port_GetSessionPath(), (g_session.cards & 1) ? "slot 1 " : "",
             (g_session.cards & 2)  ? "slot 2"
             : g_session.cards == 0 ? "none"
                                    : "");
}

s32 port_StartSession(const PortDisc *disc, u32 delay) {
    if (!port_MeetSession(disc, delay)) {
        return 0;
    }
    port_ApplySession();
    return 1;
}

void port_SessionSeed(void) {
    if (g_session.role == PORT_SESSION_NONE) {
        return;
    }
    rng_SetSeed((s32)g_session.seed);
    srand(g_session.seed);
}

u32 port_SessionLeftAt(void) { return g_port_top_menu.session_left; }

s32 port_SessionResult(void) { return g_port_top_menu.session_result; }

s32 port_GetSessionSeedAsked(u32 *seed) {
    *seed = g_session.seed_asked;
    return g_session.seed_set;
}

/* ---- a room's spectators (docs/NETPLAY.md, section 16) ---- */

/* The settings' words, as the feed carries them. */
enum {
    SETTING_PROTOCOL,
    SETTING_KIND,
    SETTING_PLAY,
    SETTING_MUSIC,
    SETTING_TIME,
    SETTING_MATCH,
    SETTING_RULES,
    SETTING_WIDER,
    SETTING_DELAY,
    SETTING_SEED,
    SETTING_PADS, /* the session's pads, 2 or 4 (protocol 14) */
    SETTING_WORDS
};

/* What a spectator must hold as the session's players hold it, into
 * `out` (PORT_SESSION_SETTINGS_BYTES): the host's switches, rule and the
 * margin both cut faces off at. */
void port_SessionSettings(u8 *out) {
    u32 words[SETTING_WORDS];
    u32 i;

    words[SETTING_PROTOCOL] = port_SessionProtocol();
    words[SETTING_KIND] = g_session.kind;
    words[SETTING_PLAY] = g_session.play;
    words[SETTING_MUSIC] = g_session.music;
    words[SETTING_TIME] = g_session.time;
    words[SETTING_MATCH] = g_session.match;
    words[SETTING_RULES] = g_session.rules;
    words[SETTING_WIDER] = g_session.wider;
    words[SETTING_DELAY] = g_session.delay;
    words[SETTING_SEED] = g_session.seed;
    words[SETTING_PADS] = (u32)g_session.pads;
    for (i = 0; i < PORT_SESSION_SETTINGS_BYTES; i++) {
        out[i] = 0;
    }
    for (i = 0; i < SETTING_WORDS; i++) {
        port_NetPutWord(out + i * WORD_BYTES, words[i]);
    }
}

/* A spectator takes the session's settings and holds them as a player
 * does (port_ApplySession), but the cards and netplay's pads: the cards
 * are in the start it puts in. 0 where the settings are of another
 * protocol. */
s32 port_WatchSession(const u8 *in) {
    if (port_NetGetWord(in + SETTING_PROTOCOL * WORD_BYTES) !=
        port_SessionProtocol()) {
        return 0;
    }
    g_session.role = PORT_SESSION_WATCH;
    g_session.kind = port_NetGetWord(in + SETTING_KIND * WORD_BYTES);
    g_session.play = port_NetGetWord(in + SETTING_PLAY * WORD_BYTES);
    g_session.music = port_NetGetWord(in + SETTING_MUSIC * WORD_BYTES);
    g_session.time = port_NetGetWord(in + SETTING_TIME * WORD_BYTES);
    g_session.match = port_NetGetWord(in + SETTING_MATCH * WORD_BYTES);
    g_session.rules = port_NetGetWord(in + SETTING_RULES * WORD_BYTES);
    g_session.wider = port_NetGetWord(in + SETTING_WIDER * WORD_BYTES);
    g_session.delay = port_NetGetWord(in + SETTING_DELAY * WORD_BYTES);
    g_session.seed = port_NetGetWord(in + SETTING_SEED * WORD_BYTES);
    g_session.pads =
        port_NetGetWord(in + SETTING_PADS * WORD_BYTES) == 4 ? 4 : 2;
    /* the players' pads, all of them the feed's (watch.c) */
    port_SetNetplayPads(g_session.pads, NULL);
    port_HoldStageKindDefault((s32)g_session.kind);
    port_HoldPlaySwitches(g_session.play);
    port_HoldFightMusic(g_session.music);
    port_HoldStageTimeDefault((s32)g_session.time);
    port_HoldMatch(g_session.match);
    port_RemasterFixMargin((s32)(g_session.wider >> SHAPE_SHIFT),
                           (s32)(g_session.wider & SHAPE_MASK));
    port_Log("session: watching: the players' rules %u, match rule %s, %d; "
             "their margin %ux%u\n",
             g_session.rules,
             port_GetMatchRule() == PORT_MATCH_STOCK      ? "stock"
             : port_GetMatchRule() == PORT_MATCH_FIRST_TO ? "first_to"
             : port_GetMatchRule() == PORT_MATCH_CREW     ? "crew"
                                                          : "single",
             port_GetMatchCount(), g_session.wider >> SHAPE_SHIFT,
             g_session.wider & SHAPE_MASK);
    return 1;
}

/* Whether the match of a room's session is decided at this frame: the
 * game's menu after a fight, of a single fight or of the match's last; its
 * winner, 0 or 1, or -1 for nobody. */
static s32 port_MatchIsDecided(s32 *winner) {
    if (g_sys_screen != SCREEN_AFTER_FIGHT_UPDATE) {
        return FALSE;
    }
    if (g_port_match.rule != PORT_MATCH_SINGLE) {
        if (!g_port_match.over) {
            return FALSE;
        }
        /* a Crew match whose crews ran out at once is nobody's */
        *winner = g_port_match.winner == PORT_MATCH_DRAWN
                      ? -1
                      : (s32)g_port_match.winner;
        return TRUE;
    }
    *winner = g_fight_winner == 0 || g_fight_winner == 1 ? g_fight_winner : -1;
    return TRUE;
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

/* Every pad of input let go but the first's irst: the session's own
 * press, or none. */
static void port_SessionPadsQuiet(PortInput *input, u16 first) {
    s32 pad;

    input->buttons[0] = first;
    for (pad = 1; pad < PORT_INPUT_PADS; pad++) {
        input->buttons[pad] = 0;
    }
}

/* A frame's input as the session has it (port_net.h): the session's own
 * presses up to the select screen, and no Start in a fight. */
void port_SessionInput(u32 frame, PortInput *input, u16 *used) {
    s32 pad;

    *used = 0xFFFF;
    if (g_session.role == PORT_SESSION_NONE) {
        return;
    }
    if ((g_session.rules & PORT_SESSION_RULE_ONE_MATCH) &&
        g_port_top_menu.session_left == 0) {
        s32 winner;

        /* a room's match: its end is the session's, on both machines at
         * the same frame (netplay.c), who won kept in the state */
        if (port_MatchIsDecided(&winner)) {
            g_port_top_menu.session_left = frame + 1;
            g_port_top_menu.session_result =
                (u8)(winner >= 0 ? winner + 1 : PORT_SESSION_RESULT_DRAW);
        }
    }
    if (g_port_top_menu.session_result != 0) {
        port_SessionPadsQuiet(input, 0);
        *used = 0;
        return;
    }
    if (!(g_session.rules & PORT_SESSION_RULE_ONE_MATCH) &&
        g_port_top_menu.session_left == 0 && port_MatchEndsSession() &&
        g_sys_screen == SCREEN_MENU_UPDATE && g_menu_page == MENU_PAGE_TOP) {
        /* a Crew match over, the game back at its top menu: the session
         * of two ends there, on both machines at the same frame, whatever
         * its rules (port_match.h) */
        g_port_top_menu.session_left = frame + 1;
    }
    if (g_port_top_menu.session_left != 0 && g_port_match.to_top) {
        port_SessionPadsQuiet(input, 0);
        *used = 0;
        return;
    }
    if (g_session.rules & PORT_SESSION_RULE_TOP_MENU_LEAVES) {
        /* back to the top menu from the select screen or after a fight:
         * the session ends there (netplay.c), and nothing is pressed */
        if (g_sys_screen == SCREEN_MENU_UPDATE &&
            (g_menu_page == MENU_PAGE_SELECT ||
             g_menu_page == MENU_PAGE_TEAM_SELECT)) {
            g_port_top_menu.session_away = 1;
        }
        if (g_port_top_menu.session_away &&
            (g_port_top_menu.session_left != 0 ||
             (g_sys_screen == SCREEN_MENU_UPDATE &&
              g_menu_page == MENU_PAGE_TOP))) {
            if (g_port_top_menu.session_left == 0) {
                g_port_top_menu.session_left = frame + 1;
            }
            port_SessionPadsQuiet(input, 0);
            *used = 0;
            return;
        }
    }
    if (port_SessionPilots()) {
        port_SessionPadsQuiet(input, port_SessionPress(frame));
        *used = 0;
        return;
    }
    if (g_sys_screen == SCREEN_FIGHT_UPDATE) {
        for (pad = 0; pad < PORT_INPUT_PADS; pad++) {
            input->buttons[pad] &= (u16)~PADstart;
        }
        *used = (u16)~PADstart;
    }
}
