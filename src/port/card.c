/* The memory card in the native port (docs/PORT.md, "The memory card"):
 * the BIOS calls the game makes, over a standard raw card image of 128 KB,
 * one file a slot. So a save the port writes is an ordinary PlayStation
 * memory card file, which an emulator or a card tool opens, and a card
 * file an emulator left works here.
 *
 * The boundary is the BIOS's, because that is where the game's card code
 * is (memcard.c, memcard_save.c) and ReBushido's settings code with it
 * (src/rebushido/save.c): the game starts something on the card and waits
 * for the events the card driver reports through.
 *
 *   _card_info      is a card in the slot, and is it one the driver has
 *                   not seen since it started?
 *   _card_load      read the card's directory
 *   _new_card,      Sony's _card_clear (libcard/card.c) is these two: the
 *   _card_write     driver stops calling the card new, and a frame is
 *                   written to prove the card takes one
 *   open, close,    the card's files, by the names "bu00:..." (slot 1) and
 *   read, write,    "bu10:..." (slot 2)
 *   firstfile,
 *   nextfile,
 *   delete, format
 *
 * The events are the port's own table (event.c). A software event (SwCARD)
 * is what the whole operation reports, and the game polls for it once a
 * frame: the port gives it the number of vertical blanks the console's
 * card takes ("How long it takes" below). A hardware event (HwCARD) is
 * the card itself answering, and the game waits for one in loops with no
 * vertical blank in them (memcard_WaitHwEvent), where the port's clock
 * does not move at all: those are reported the moment the call returns.
 *
 * The image's format is the card's own, which is public knowledge and is
 * written out here from the rules (port_card.h has the numbers): a header
 * block of 64 frames of 128 bytes - the identifier, a directory frame for
 * each of the fifteen blocks, the list of broken frames and the frames
 * that replace them - and then the fifteen blocks a file may be in. Every
 * frame's last byte is the exclusive or of its other 127. */
#include "common.h"
#include "port_card.h"
#include "port_host.h"
#include "bb2.h"

/* A slot's number in the BIOS's channel: the port in the high half, the
 * card of a multitap in the low (memcard.c, CARD_CHANNEL). The port has
 * no multitap, so only card 0 of each port answers. */
#define PORT_CARD_PORT_SHIFT 4
#define PORT_CARD_HALF_MASK 0x0F

/* The high half of a block's state says what it is; the low half, which
 * part of its file. A block the BIOS has freed keeps the low half, so
 * that a card tool can see what the file was. */
#define PORT_CARD_KIND_MASK 0xF0
#define PORT_CARD_PART_MASK 0x0F

#define PORT_CARD_BYTE_BITS 8

/* How many of the card's files may be open at once. The game has one. */
#define PORT_CARD_FILES_MAX 4
/* What open() gives for a card's file: this and the file's place in the
 * table. The BIOS's own numbers are small; nothing but the port reads
 * these, and they are kept clear of the link cable's. */
#define PORT_CARD_FD_FIRST 16
/* What open() gives for a name that is not a card's: the link cable's
 * port, which the game asks for until it answers and which nothing is at
 * the other end of (event.c had this before the card did). */
#define PORT_CARD_LINK_FD 3

/* The number of blocks a file is to have rides above open()'s flags
 * (memcard_WriteFile). */
#define PORT_CARD_CREATE_BLOCKS_SHIFT 16

/* A path to a card's file is "buPC:" and a name: the port's digit, the
 * card's, and the colon. */
#define PORT_CARD_PATH_PORT 2
#define PORT_CARD_PATH_CARD 3
#define PORT_CARD_PATH_COLON 4
#define PORT_CARD_PATH_NAME 5

/* Where the images go, and how long a path may be. */
#define PORT_CARD_PATH_SIZE 1024
#define PORT_CARD_DEFAULT_NAME_SIZE 16
static const char
    g_port_card_default_names[PORT_CARD_SLOTS][PORT_CARD_DEFAULT_NAME_SIZE] = {
        "card1.mcd",
        "card2.mcd",
};

/* The game's own save (memcard_save.c, g_memcard_save_file_name). Its
 * table of patches is addresses of the PlayStation's memory that the
 * loader writes into; the game always writes the table empty, and the
 * port will not write through one that is not ("The patch table" below). */
#define PORT_CARD_GAME_SAVE "BASLUS-00663BUSHIDO2"
/* An address in that table is four bytes on the card and a value two,
 * whatever a pointer is on the host. Where the table is, counted from the
 * start of the file: the card file's header, then SaveData's three copies
 * of the record and their three checksums (memcard.h). */
#define PORT_CARD_PATCH_ADDR_BYTES 4
#define PORT_CARD_PATCH_VALUE_BYTES 2
#define PORT_CARD_PATCH_ADDRS_AT                                               \
    ((u32)MEMCARD_HEADER_SIZE + SAVE_COPIES * (u32)sizeof(SaveRecord) +        \
     SAVE_COPIES * (u32)sizeof(s32))
#define PORT_CARD_PATCH_VALUES_AT                                              \
    (PORT_CARD_PATCH_ADDRS_AT + SAVE_PATCH_COUNT * PORT_CARD_PATCH_ADDR_BYTES)
#define PORT_CARD_PATCH_END                                                    \
    (PORT_CARD_PATCH_VALUES_AT + SAVE_PATCH_COUNT * PORT_CARD_PATCH_VALUE_BYTES)

/* One slot: its image, what the driver knows about the card in it, and
 * the one operation whose software event is still to come. The header
 * block is kept here as it is on the card: the directory is read and
 * changed in it, and a frame that changes is written out at once. */
typedef struct {
    const char *path; /* NULL: nothing is in this slot */
    s32 file;         /* the host's number for the image; -1 if not open */
    s32 tried;        /* the image has been opened or made, or has failed */
    s32 present;      /* a card is there */
    s32 formatted;    /* its identifier and directory frames are good */
    s32 is_new;       /* the driver has not been told about this card yet */
    u32 report_at;    /* the blank the software event is due at */
    s32 report_spec;  /* which event it is */
    s32 reporting;    /* one is due */
    /* the card is g_port_card_images[slot], and no file: netplay's,
     * where both machines play on the host's cards (port_SetCardImage) */
    s32 in_memory;
    u8 header[PORT_CARD_BLOCK_SIZE];
} PortCardSlot;

/* One open file of a card. */
typedef struct {
    s32 open;
    s32 slot;
    s32 first_block; /* 1 to PORT_CARD_BLOCKS */
    u32 size;        /* what the directory says, in bytes */
    u32 at;          /* where the next read or write starts */
    char name[PORT_CARD_NAME_SIZE + 1];
} PortCardFile;

static PortCardSlot g_port_card_slots[PORT_CARD_SLOTS];
static PortCardFile g_port_card_files[PORT_CARD_FILES_MAX];
/* Both slots empty, whatever --card1 and --card2 said (--no-cards). */
static s32 g_port_cards_off;
/* A block of zeroes, for making an image. */
static const u8 g_port_card_zeroes[PORT_CARD_FRAME_SIZE];
/* The cards of a slot that is in memory: part of the state, so a frame
 * that netplay takes back takes back what it saved too. */
static u8 g_port_card_images[PORT_CARD_SLOTS][PORT_CARD_IMAGE_SIZE];

/* Where firstfile and nextfile have got to: the slot, the pattern and the
 * block to look at next. The BIOS keeps the walk itself, and the game
 * hands nextfile a fresh entry each time (memcard_ReadDirectory). */
static s32 g_port_card_walk_slot = -1;
static s32 g_port_card_walk_block;
static char g_port_card_walk_pattern[PORT_CARD_NAME_SIZE + 1];

/* ---- bytes ---- */

static u32 port_CardGetHalf(const u8 *at) {
    return (u32)at[0] | ((u32)at[1] << PORT_CARD_BYTE_BITS);
}

static void port_CardPutHalf(u8 *at, u32 value) {
    at[0] = (u8)value;
    at[1] = (u8)(value >> PORT_CARD_BYTE_BITS);
}

static u32 port_CardGetWord(const u8 *at) {
    return port_CardGetHalf(at) |
           (port_CardGetHalf(at + 2) << (2 * PORT_CARD_BYTE_BITS));
}

static void port_CardPutWord(u8 *at, u32 value) {
    port_CardPutHalf(at, value);
    port_CardPutHalf(at + 2, value >> (2 * PORT_CARD_BYTE_BITS));
}

static void port_CardFillBytes(u8 *at, u8 value, u32 count) {
    u32 i;

    for (i = 0; i < count; i++) {
        at[i] = value;
    }
}

static void port_CardCopyBytes(u8 *to, const u8 *from, u32 count) {
    u32 i;

    for (i = 0; i < count; i++) {
        to[i] = from[i];
    }
}

/* A name of at most PORT_CARD_NAME_SIZE characters, with its zero. */
static void port_CardCopyName(char *to, const char *from) {
    s32 i;

    for (i = 0; i < PORT_CARD_NAME_SIZE && from[i] != 0; i++) {
        to[i] = from[i];
    }
    for (; i <= PORT_CARD_NAME_SIZE; i++) {
        to[i] = 0;
    }
}

/* ---- the image's frames ---- */

/* The exclusive or of a frame's first 127 bytes, which its last byte is. */
static u8 port_CardFrameSum(const u8 *frame) {
    u8 sum = 0;
    s32 i;

    for (i = 0; i < PORT_CARD_SUM_AT; i++) {
        sum = (u8)(sum ^ frame[i]);
    }
    return sum;
}

static u8 *port_CardFrame(u8 *block, s32 frame) {
    return block + frame * PORT_CARD_FRAME_SIZE;
}

/* The directory frame of block `block` (1 to PORT_CARD_BLOCKS). */
static u8 *port_CardDirOf(PortCardSlot *slot, s32 block) {
    return port_CardFrame(slot->header, PORT_CARD_DIR_FRAME + block - 1);
}

/* The identifier, a free frame for each block, a list of broken frames
 * that names none, and the identifier again at the end: a card as it
 * comes formatted. */
static void port_CardFormatBlock(u8 *block) {
    s32 i;

    port_CardFillBytes(block, 0, PORT_CARD_BLOCK_SIZE);
    port_CardFrame(block, PORT_CARD_ID_FRAME)[0] = PORT_CARD_ID_HIGH;
    port_CardFrame(block, PORT_CARD_ID_FRAME)[1] = PORT_CARD_ID_LOW;
    port_CardFrame(block, PORT_CARD_ID_COPY_FRAME)[0] = PORT_CARD_ID_HIGH;
    port_CardFrame(block, PORT_CARD_ID_COPY_FRAME)[1] = PORT_CARD_ID_LOW;
    for (i = 0; i < PORT_CARD_BLOCKS; i++) {
        u8 *dir = port_CardFrame(block, PORT_CARD_DIR_FRAME + i);

        dir[PORT_CARD_DIR_STATE] = PORT_CARD_FREE;
        port_CardPutHalf(dir + PORT_CARD_DIR_NEXT, PORT_CARD_NO_NEXT);
    }
    for (i = 0; i < PORT_CARD_BROKEN_ENTRIES; i++) {
        u8 *broken = port_CardFrame(block, PORT_CARD_BROKEN_FRAME + i);

        /* the frame it stands for, and no next, at the same offsets a
         * directory frame has them */
        port_CardPutWord(broken, PORT_CARD_NOT_BROKEN);
        port_CardPutHalf(broken + PORT_CARD_DIR_NEXT, PORT_CARD_NO_NEXT);
    }
    for (i = 0; i < PORT_CARD_FRAMES_PER_BLOCK; i++) {
        u8 *frame = port_CardFrame(block, i);

        frame[PORT_CARD_SUM_AT] = port_CardFrameSum(frame);
    }
}

/* Whether a header block is a formatted card's: the identifier, and every
 * directory frame's own byte of exclusive or. The frames of the list of
 * broken frames are not looked at: a card with one of those spoiled still
 * holds its files. */
static s32 port_CardHeaderIsGood(const u8 *block) {
    s32 i;

    if (block[0] != PORT_CARD_ID_HIGH || block[1] != PORT_CARD_ID_LOW) {
        return 0;
    }
    for (i = 0; i <= PORT_CARD_BLOCKS; i++) {
        const u8 *frame = block + i * PORT_CARD_FRAME_SIZE;

        if (frame[PORT_CARD_SUM_AT] != port_CardFrameSum(frame)) {
            return 0;
        }
    }
    return 1;
}

/* ---- the image file ---- */

/* Says once what went wrong with a slot's image, and leaves the slot
 * empty: the game then finds no card, which is a state it is written
 * for. */
static void port_CardCannotUse(PortCardSlot *slot, const char *problem) {
    port_Log("rebushido: %s: %s: the memory card slot is empty\n", slot->path,
             problem);
    if (slot->file >= 0) {
        port_HostClose(slot->file);
        slot->file = -1;
    }
    slot->present = 0;
}

/* A new image, formatted and empty: the header block and fifteen blocks
 * of zeroes. Returns 1 if it was written. */
static s32 port_CardMakeImage(PortCardSlot *slot) {
    s32 file = port_HostCreate(slot->path);
    s32 written;
    s32 i;

    if (file < 0) {
        return 0;
    }
    port_CardFormatBlock(slot->header);
    written = port_HostAppend(file, slot->header, PORT_CARD_BLOCK_SIZE);
    for (i = 0; i < PORT_CARD_BLOCKS * PORT_CARD_FRAMES_PER_BLOCK; i++) {
        written &=
            port_HostAppend(file, g_port_card_zeroes, PORT_CARD_FRAME_SIZE);
    }
    port_HostClose(file);
    return written;
}

/* Opens the slot's image, making it if it is not there, and reads its
 * header block. */
static void port_CardOpenImage(PortCardSlot *slot) {
    if (slot->in_memory) {
        /* netplay's: the image is in memory already (port_SetCardImage) */
        port_CardCopyBytes(
            slot->header, g_port_card_images[slot - g_port_card_slots],
            PORT_CARD_BLOCK_SIZE);
        slot->present = 1;
        slot->formatted = port_CardHeaderIsGood(slot->header);
        return;
    }
    slot->file = port_HostOpenReadWrite(slot->path);
    if (slot->file < 0) {
        if (!port_CardMakeImage(slot)) {
            port_CardCannotUse(slot, "no card image here, and none can be "
                                     "made");
            return;
        }
        slot->file = port_HostOpenReadWrite(slot->path);
        if (slot->file < 0) {
            port_CardCannotUse(slot, "the card image cannot be opened for "
                                     "writing");
            return;
        }
    }
    if (port_HostFileSize(slot->file) != PORT_CARD_IMAGE_SIZE) {
        port_CardCannotUse(slot, "not a memory card image: it is not 128 KB");
        return;
    }
    if (port_HostReadAt(slot->file, 0, slot->header, PORT_CARD_BLOCK_SIZE) !=
        PORT_CARD_BLOCK_SIZE) {
        port_CardCannotUse(slot, "the card image cannot be read");
        return;
    }
    slot->present = 1;
    slot->formatted = port_CardHeaderIsGood(slot->header);
}

/* The slot's card, opened the first time the game looks at it: 1 if there
 * is one to talk to. */
static s32 port_CardReady(PortCardSlot *slot) {
    if (slot == NULL ||
        (!slot->in_memory && (g_port_cards_off || slot->path == NULL))) {
        return 0;
    }
    if (!slot->tried) {
        slot->tried = 1;
        port_CardOpenImage(slot);
    }
    return slot->present;
}

/* `count` bytes at `at` of the image. Returns 1 if all of them were
 * there. */
static s32 port_CardReadImage(
    PortCardSlot *slot, u32 at, void *bytes, u32 count) {
    if (slot->in_memory) {
        if (at > PORT_CARD_IMAGE_SIZE || count > PORT_CARD_IMAGE_SIZE - at) {
            return 0;
        }
        port_CardCopyBytes(
            bytes, g_port_card_images[slot - g_port_card_slots] + at, count);
        return 1;
    }
    return port_HostReadAt(slot->file, at, bytes, count) == count;
}

/* `count` bytes to `at` of the image, pushed out to the disc at once: a
 * save the game has been told is written is on the disc, whatever happens
 * next. A card in memory keeps them there and writes no file. */
static s32 port_CardWriteImage(
    PortCardSlot *slot, u32 at, const void *bytes, u32 count) {
    if (slot->in_memory) {
        if (at > PORT_CARD_IMAGE_SIZE || count > PORT_CARD_IMAGE_SIZE - at) {
            return 0;
        }
        port_CardCopyBytes(
            g_port_card_images[slot - g_port_card_slots] + at, bytes, count);
        return 1;
    }
    if (!port_HostWriteAt(slot->file, at, bytes, count)) {
        return 0;
    }
    port_HostFlush(slot->file);
    return 1;
}

/* The directory frame of block `block` as it now stands in memory, with
 * its byte of exclusive or, out to the image. */
static s32 port_CardWriteDir(PortCardSlot *slot, s32 block) {
    s32 frame = PORT_CARD_DIR_FRAME + block - 1;
    u8 *dir = port_CardDirOf(slot, block);

    dir[PORT_CARD_SUM_AT] = port_CardFrameSum(dir);
    return port_CardWriteImage(
        slot, (u32)frame * PORT_CARD_FRAME_SIZE, dir, PORT_CARD_FRAME_SIZE);
}

/* ---- the directory ---- */

static s32 port_CardBlockIsFree(PortCardSlot *slot, s32 block) {
    return (port_CardDirOf(slot, block)[PORT_CARD_DIR_STATE] &
            PORT_CARD_KIND_MASK) == (PORT_CARD_FREE & PORT_CARD_KIND_MASK);
}

/* Whether a file begins in this block. */
static s32 port_CardBlockIsFirst(PortCardSlot *slot, s32 block) {
    return port_CardDirOf(slot, block)[PORT_CARD_DIR_STATE] == PORT_CARD_FIRST;
}

/* The name of the file that begins in this block, with its zero: a card
 * from anywhere may have twenty characters and no zero after them. */
static void port_CardGetName(PortCardSlot *slot, s32 block, char *name) {
    port_CardCopyName(
        name, (const char *)(port_CardDirOf(slot, block) + PORT_CARD_DIR_NAME));
}

/* The block a file goes on in, or 0 where it ends. */
static s32 port_CardNextBlock(PortCardSlot *slot, s32 block) {
    u32 next =
        port_CardGetHalf(port_CardDirOf(slot, block) + PORT_CARD_DIR_NEXT);

    /* the field counts the blocks from 0 */
    if (next >= PORT_CARD_BLOCKS) {
        return 0;
    }
    return (s32)next + 1;
}

/* The block the file of this name begins in, or 0 if the card has no such
 * file. */
static s32 port_CardFindFile(PortCardSlot *slot, const char *name) {
    char found[PORT_CARD_NAME_SIZE + 1];
    s32 block;

    for (block = 1; block <= PORT_CARD_BLOCKS; block++) {
        port_CardGetName(slot, block, found);
        if (port_CardBlockIsFirst(slot, block) &&
            port_TextsEqual(found, name)) {
            return block;
        }
    }
    return 0;
}

/* Makes a file of `blocks` blocks: the blocks are the free ones in the
 * order they come, linked into a chain, with the whole file's size and
 * its name in the frame of the first. Returns that block, or 0 if the
 * card is full or already has the name. */
static s32 port_CardCreateFile(
    PortCardSlot *slot, const char *name, s32 blocks) {
    s32 chain[PORT_CARD_BLOCKS];
    s32 found = 0;
    s32 block;
    s32 i;

    if (blocks < 1 || blocks > PORT_CARD_BLOCKS ||
        port_CardFindFile(slot, name) != 0) {
        return 0;
    }
    for (block = 1; block <= PORT_CARD_BLOCKS && found < blocks; block++) {
        if (port_CardBlockIsFree(slot, block)) {
            chain[found++] = block;
        }
    }
    if (found < blocks) {
        return 0;
    }
    for (i = 0; i < blocks; i++) {
        u8 *dir = port_CardDirOf(slot, chain[i]);
        s32 state = PORT_CARD_MIDDLE;

        if (i == 0) {
            state = PORT_CARD_FIRST;
        } else if (i == blocks - 1) {
            state = PORT_CARD_LAST;
        }
        port_CardFillBytes(dir, 0, PORT_CARD_FRAME_SIZE);
        dir[PORT_CARD_DIR_STATE] = (u8)state;
        if (i + 1 < blocks) {
            port_CardPutHalf(dir + PORT_CARD_DIR_NEXT, (u32)chain[i + 1] - 1);
        } else {
            port_CardPutHalf(dir + PORT_CARD_DIR_NEXT, PORT_CARD_NO_NEXT);
        }
        /* the size and the name are the first block's; the others carry
         * neither, as a card's directory has it */
        if (i == 0) {
            port_CardPutWord(
                dir + PORT_CARD_DIR_SIZE, (u32)blocks * PORT_CARD_BLOCK_SIZE);
            port_CardCopyName((char *)(dir + PORT_CARD_DIR_NAME), name);
        }
        if (!port_CardWriteDir(slot, chain[i])) {
            return 0;
        }
    }
    return chain[0];
}

/* Frees the blocks of the file of this name, as the BIOS's delete does:
 * the state's high half says free and its low half stays, so that a card
 * tool can still see what the file was, and the name and the chain stay
 * with it. Returns 1 if there was such a file. */
static s32 port_CardDeleteFile(PortCardSlot *slot, const char *name) {
    s32 block = port_CardFindFile(slot, name);

    if (block == 0) {
        return 0;
    }
    while (block != 0) {
        u8 *dir = port_CardDirOf(slot, block);
        s32 next = port_CardNextBlock(slot, block);

        dir[PORT_CARD_DIR_STATE] =
            (u8)((PORT_CARD_FREE & PORT_CARD_KIND_MASK) |
                 (dir[PORT_CARD_DIR_STATE] & PORT_CARD_PART_MASK));
        if (!port_CardWriteDir(slot, block)) {
            return 0;
        }
        block = next;
    }
    return 1;
}

/* The pattern of firstfile: `*` stands for any run of characters, `?` for
 * any one, and anything else for itself. */
static s32 port_CardNameMatches(const char *pattern, const char *name) {
    const char *star = NULL;
    const char *after_star = NULL;

    while (*name != 0) {
        if (*pattern == '?' || *pattern == *name) {
            pattern++;
            name++;
        } else if (*pattern == '*') {
            star = pattern++;
            after_star = name;
        } else if (star != NULL) {
            /* the last `*` takes one character more */
            pattern = star + 1;
            name = ++after_star;
        } else {
            return 0;
        }
    }
    while (*pattern == '*') {
        pattern++;
    }
    return *pattern == 0;
}

/* ---- the slots, and the events ---- */

static PortCardSlot *port_CardOfChannel(s32 chan) {
    s32 port = (chan >> PORT_CARD_PORT_SHIFT) & PORT_CARD_HALF_MASK;
    s32 card = chan & PORT_CARD_HALF_MASK;

    if (card != 0 || port >= PORT_CARD_SLOTS) {
        return NULL;
    }
    return &g_port_card_slots[port];
}

/* The software event the whole operation reports, `blanks` vertical blanks
 * from now: the game polls for it once a frame. A slot has one operation
 * at a time, as the driver has. */
static void port_CardReportLater(PortCardSlot *slot, s32 spec, u32 blanks) {
    slot->report_spec = spec;
    slot->report_at = port_GetBlankCount() + blanks;
    slot->reporting = 1;
}

/* How long a transfer of `bytes` bytes takes: a frame of 128 bytes a
 * vertical blank, and never nothing. */
static u32 port_CardTransferBlanks(u32 bytes) {
    u32 frames = (bytes + PORT_CARD_FRAME_SIZE - 1) / PORT_CARD_FRAME_SIZE;

    if (frames == 0) {
        frames = 1;
    }
    return frames * PORT_CARD_FRAME_BLANKS;
}

/* The card driver's turn at a vertical blank (port_card.h): an operation
 * whose time is up reports itself. */
void port_RunCardBlank(u32 blank) {
    s32 i;

    for (i = 0; i < PORT_CARD_SLOTS; i++) {
        PortCardSlot *slot = &g_port_card_slots[i];

        if (slot->reporting && blank >= slot->report_at) {
            slot->reporting = 0;
            port_DeliverEvent(SwCARD, slot->report_spec);
        }
    }
}

/* ---- what the program's options say ---- */

void port_SetCardPath(s32 slot, const char *path) {
    if (slot >= 0 && slot < PORT_CARD_SLOTS) {
        g_port_card_slots[slot].path = path;
    }
}

void port_SetCardsOff(void) { g_port_cards_off = 1; }

/* The image of a slot's card as this program would play on it
 * (port_card.h): its file, or a formatted and empty card where there is
 * none yet. */
s32 port_ReadCardFile(s32 slot, u8 *out) {
    PortCardSlot *of;
    s32 file;
    u32 i;

    if (slot < 0 || slot >= PORT_CARD_SLOTS || g_port_cards_off) {
        return 0;
    }
    of = &g_port_card_slots[slot];
    if (of->in_memory) {
        port_CardCopyBytes(out, g_port_card_images[slot], PORT_CARD_IMAGE_SIZE);
        return 1;
    }
    if (of->path == NULL) {
        return 0;
    }
    file = port_HostOpen(of->path);
    if (file >= 0 && port_HostFileSize(file) == PORT_CARD_IMAGE_SIZE &&
        port_HostReadAt(file, 0, out, PORT_CARD_IMAGE_SIZE) ==
            PORT_CARD_IMAGE_SIZE) {
        port_HostClose(file);
        return 1;
    }
    if (file >= 0) {
        port_HostClose(file);
    }
    port_CardFormatBlock(out);
    for (i = PORT_CARD_BLOCK_SIZE; i < PORT_CARD_IMAGE_SIZE; i++) {
        out[i] = 0;
    }
    return 1;
}

/* The slot's card is `image` in memory, or the slot is empty (NULL): no
 * file is read or written for it (port_card.h). */
void port_SetCardImage(s32 slot, const u8 *image) {
    PortCardSlot *of;

    if (slot < 0 || slot >= PORT_CARD_SLOTS) {
        return;
    }
    of = &g_port_card_slots[slot];
    of->in_memory = image != NULL;
    of->tried = 0;
    of->present = 0;
    if (image == NULL) {
        of->path = NULL;
        return;
    }
    port_CardCopyBytes(g_port_card_images[slot], image, PORT_CARD_IMAGE_SIZE);
}

/* A card image beside the program for each slot no option named
 * (port_card.h): the program's own folder, which is the one place the
 * port is sure to be able to write today. */
void port_SetDefaultCardPaths(const char *program) {
    static char paths[PORT_CARD_SLOTS][PORT_CARD_PATH_SIZE];
    u32 length = port_TextLength(program);
    s32 slot;
    u32 cut;

    /* the program's own folder: its path up to the last separator */
    cut = 0;
    while (cut < length) {
        if (program[length - 1 - cut] == '/' ||
            program[length - 1 - cut] == '\\') {
            break;
        }
        cut++;
    }
    length -= cut;
    for (slot = 0; slot < PORT_CARD_SLOTS; slot++) {
        u32 name = port_TextLength(g_port_card_default_names[slot]);
        u32 i;

        if (g_port_card_slots[slot].path != NULL ||
            length + name + 1 > PORT_CARD_PATH_SIZE) {
            continue;
        }
        for (i = 0; i < length; i++) {
            paths[slot][i] = program[i];
        }
        for (i = 0; i <= name; i++) {
            paths[slot][length + i] = g_port_card_default_names[slot][i];
        }
        g_port_card_slots[slot].path = paths[slot];
    }
}

/* ---- the BIOS's card driver ---- */

/* Starts the driver: every open file is forgotten and each card is one the
 * driver has not been told about, as after power on. `pads_too` says the
 * pads are in use as well, which the port's need nothing for. */
void InitCARD(s32 pads_too) {
    s32 i;

    (void)pads_too;
    for (i = 0; i < PORT_CARD_FILES_MAX; i++) {
        g_port_card_files[i].open = 0;
    }
    for (i = 0; i < PORT_CARD_SLOTS; i++) {
        PortCardSlot *slot = &g_port_card_slots[i];

        slot->reporting = 0;
        slot->is_new = 1;
    }
    g_port_card_walk_slot = -1;
}

s32 StartCARD(void) { return 1; }

/* Stops the driver (before the movie player runs). Everything written is
 * on the disc already; what is open is closed. */
s32 StopCARD(void) {
    s32 i;

    for (i = 0; i < PORT_CARD_FILES_MAX; i++) {
        g_port_card_files[i].open = 0;
    }
    for (i = 0; i < PORT_CARD_SLOTS; i++) {
        g_port_card_slots[i].reporting = 0;
    }
    return 1;
}

/* The BIOS's file system for the card. The port's directory is the
 * image's own, read when it is wanted, so there is nothing to set up. */
void _bu_init(void) {}

/* Is a card in the slot? EvSpNEW for one the driver has not been told
 * about, EvSpIOE for one it has, EvSpTIMOUT for an empty slot. */
s32 _card_info(s32 chan) {
    PortCardSlot *slot = port_CardOfChannel(chan);
    s32 spec = EvSpTIMOUT;

    if (port_CardReady(slot)) {
        spec = slot->is_new ? EvSpNEW : EvSpIOE;
    }
    if (slot != NULL) {
        port_CardReportLater(slot, spec, PORT_CARD_INFO_BLANKS);
    } else {
        port_DeliverEvent(SwCARD, spec);
    }
    return 1;
}

/* Reads the card's directory. EvSpIOE for a formatted card, EvSpNEW for
 * one that is not (which is how the game reads "unformatted": memcard.c,
 * PROBE_WAIT_LOAD), EvSpTIMOUT for an empty slot or an image that cannot
 * be read. */
s32 _card_load(s32 chan) {
    PortCardSlot *slot = port_CardOfChannel(chan);
    s32 spec = EvSpTIMOUT;

    if (port_CardReady(slot)) {
        if (!port_CardReadImage(slot, 0, slot->header, PORT_CARD_BLOCK_SIZE)) {
            port_CardCannotUse(slot, "the card image cannot be read");
        } else {
            slot->formatted = port_CardHeaderIsGood(slot->header);
            spec = slot->formatted ? EvSpIOE : EvSpNEW;
        }
    }
    if (slot != NULL) {
        port_CardReportLater(slot, spec, PORT_CARD_LOAD_BLANKS);
    } else {
        port_DeliverEvent(SwCARD, spec);
    }
    return 1;
}

/* Sony's _card_clear is this and a write of one frame (libcard/card.c):
 * the driver has been told about the card in this slot, and stops saying
 * it is new. (There is no directory to forget: the port reads the card's
 * own whenever it is wanted.) */
void _new_card(s32 chan) {
    PortCardSlot *slot = port_CardOfChannel(chan);

    if (slot != NULL) {
        slot->is_new = 0;
    }
}

/* One frame of 128 bytes to the card. Sony's _card_clear is the only
 * caller, with the last frame of the header block and no buffer at all:
 * the console writes the 128 bytes at address 0 there, which the port has
 * no memory for, and a formatted card holds its identifier again in that
 * frame, so the port writes nothing and only answers. Both events are
 * reported at once, because the game waits for the card's in a loop with
 * no vertical blank in it. */
s32 _card_write(s32 chan, s32 frame, u8 *buf) {
    PortCardSlot *slot = port_CardOfChannel(chan);

    if (!port_CardReady(slot) || frame < 0 || (u32)frame >= PORT_CARD_FRAMES) {
        port_DeliverEvent(HwCARD, EvSpTIMOUT);
        return 1;
    }
    if (buf != NULL &&
        !port_CardWriteImage(slot, (u32)frame * PORT_CARD_FRAME_SIZE, buf,
                             PORT_CARD_FRAME_SIZE)) {
        port_DeliverEvent(HwCARD, EvSpERROR);
        return 1;
    }
    if (buf != NULL && frame < PORT_CARD_FRAMES_PER_BLOCK) {
        port_CardCopyBytes(
            port_CardFrame(slot->header, frame), buf, PORT_CARD_FRAME_SIZE);
    }
    port_DeliverEvent(HwCARD, EvSpIOE);
    port_DeliverEvent(SwCARD, EvSpIOE);
    return 1;
}

/* ---- the card's files ---- */

static PortCardFile *port_CardFileOf(s32 fd) {
    s32 index = fd - PORT_CARD_FD_FIRST;

    if (index < 0 || index >= PORT_CARD_FILES_MAX ||
        !g_port_card_files[index].open) {
        return NULL;
    }
    return &g_port_card_files[index];
}

/* Whether a name is a card's file or the link cable's port. */
static s32 port_CardIsCardPath(const char *path) {
    return path[0] == 'b' && path[1] == 'u';
}

/* The slot a path names, with the file's name after the colon: NULL if
 * the path is not "buPC:" and a name. */
static PortCardSlot *port_CardPathSlot(const char *path, const char **name) {
    s32 chan;

    if (path[PORT_CARD_PATH_PORT] < '0' || path[PORT_CARD_PATH_PORT] > '9' ||
        path[PORT_CARD_PATH_CARD] < '0' || path[PORT_CARD_PATH_CARD] > '9' ||
        path[PORT_CARD_PATH_COLON] != ':') {
        return NULL;
    }
    chan = ((path[PORT_CARD_PATH_PORT] - '0') << PORT_CARD_PORT_SHIFT) +
           (path[PORT_CARD_PATH_CARD] - '0');
    *name = path + PORT_CARD_PATH_NAME;
    return port_CardOfChannel(chan);
}

/* The game's save holds a table of addresses of the PlayStation's memory
 * and halfwords to write into them when it is loaded. The game always
 * writes it empty; on a PC a save from anywhere could write anything
 * anywhere, so the port hands the game an empty table and says that it
 * did (docs/PORT.md, "The patch table"). */
static void port_CardDropPatches(const char *name, u8 *bytes, u32 count) {
    s32 found = 0;
    u32 i;

    if (count < PORT_CARD_PATCH_END ||
        !port_TextsEqual(name, PORT_CARD_GAME_SAVE)) {
        return;
    }
    for (i = 0; i < (u32)(SAVE_PATCH_COUNT * PORT_CARD_PATCH_ADDR_BYTES); i++) {
        found |= bytes[PORT_CARD_PATCH_ADDRS_AT + i] != 0;
    }
    if (!found) {
        return;
    }
    port_CardFillBytes(bytes + PORT_CARD_PATCH_ADDRS_AT, 0,
                       PORT_CARD_PATCH_END - PORT_CARD_PATCH_ADDRS_AT);
    port_Log("rebushido: the save on the memory card has addresses in its "
             "table of patches; the port hands the game an empty table, as "
             "the game itself always writes one (docs/PORT.md)\n");
}

/* Reads or writes `count` bytes at `at` of the file, following its chain
 * of blocks. Returns 1 if all of them were. */
static s32 port_CardFileBytes(
    PortCardFile *file, u32 at, void *bytes, u32 count, s32 writing) {
    PortCardSlot *slot = &g_port_card_slots[file->slot];
    s32 block = file->first_block;
    u32 done = 0;

    /* over the blocks the first `at` bytes are in */
    while (at >= PORT_CARD_BLOCK_SIZE && block != 0) {
        at -= PORT_CARD_BLOCK_SIZE;
        block = port_CardNextBlock(slot, block);
    }
    while (done < count && block != 0) {
        u32 piece = PORT_CARD_BLOCK_SIZE - at;
        u32 where = (u32)block * PORT_CARD_BLOCK_SIZE + at;

        if (piece > count - done) {
            piece = count - done;
        }
        if (writing) {
            if (!port_CardWriteImage(slot, where, (u8 *)bytes + done, piece)) {
                return 0;
            }
        } else if (
            !port_CardReadImage(slot, where, (u8 *)bytes + done, piece)) {
            return 0;
        }
        done += piece;
        at = 0;
        block = port_CardNextBlock(slot, block);
    }
    return done == count;
}

/* A card's file by its path, or the link cable's port. With O_CREAT the
 * file is made, as many blocks long as the number above the flags says
 * (memcard_WriteFile); the game then closes it and opens it again to
 * write. Nothing here reports an event: the game clears the card's events
 * after it has opened the file. */
s32 open(char *name, u32 mode) {
    PortCardSlot *slot;
    PortCardFile *file = NULL;
    const char *in_card;
    s32 block;
    s32 i;

    if (!port_CardIsCardPath(name)) {
        PORT_STUB("open (the link cable's port)");
        return PORT_CARD_LINK_FD;
    }
    slot = port_CardPathSlot(name, &in_card);
    if (!port_CardReady(slot) || !slot->formatted) {
        return -1;
    }
    if (mode & O_CREAT) {
        block = port_CardCreateFile(
            slot, in_card, (s32)(mode >> PORT_CARD_CREATE_BLOCKS_SHIFT));
    } else {
        block = port_CardFindFile(slot, in_card);
    }
    if (block == 0) {
        return -1;
    }
    for (i = 0; i < PORT_CARD_FILES_MAX; i++) {
        if (!g_port_card_files[i].open) {
            file = &g_port_card_files[i];
            break;
        }
    }
    if (file == NULL) {
        return -1;
    }
    file->open = 1;
    file->slot = (s32)(slot - g_port_card_slots);
    file->first_block = block;
    file->size =
        port_CardGetWord(port_CardDirOf(slot, block) + PORT_CARD_DIR_SIZE);
    file->at = 0;
    port_CardCopyName(file->name, in_card);
    return PORT_CARD_FD_FIRST + i;
}

s32 close(s32 fd) {
    PortCardFile *file = port_CardFileOf(fd);

    if (file == NULL) {
        PORT_STUB("close (the link cable's port)");
        return 0;
    }
    file->open = 0;
    return 0;
}

/* The bytes of a card's file. The card answers at once (HwCARD), which is
 * what the game waits for before it goes on, and the whole read reports
 * itself a frame of 128 bytes a vertical blank later (SwCARD), which the
 * game polls for. */
s32 read(s32 fd, void *buf, s32 count) {
    PortCardFile *file = port_CardFileOf(fd);
    PortCardSlot *slot;
    u32 want;

    if (file == NULL) {
        PORT_STUB("read (the link cable's port)");
        return -1;
    }
    slot = &g_port_card_slots[file->slot];
    want = (u32)(count > 0 ? count : 0);
    if (want > file->size - file->at) {
        want = file->size - file->at;
    }
    if (!port_CardFileBytes(file, file->at, buf, want, 0)) {
        port_DeliverEvent(HwCARD, EvSpERROR);
        return -1;
    }
    if (file->at == 0) {
        port_CardDropPatches(file->name, (u8 *)buf, want);
    }
    file->at += want;
    port_DeliverEvent(HwCARD, EvSpIOE);
    port_CardReportLater(slot, EvSpIOE, port_CardTransferBlanks(want));
    return (s32)want;
}

/* The bytes of a card's file, the other way, pushed out to the disc as
 * they go: the same events as a read, in the same order. */
s32 write(s32 fd, void *buf, s32 count) {
    PortCardFile *file = port_CardFileOf(fd);
    PortCardSlot *slot;
    u32 want;

    if (file == NULL) {
        PORT_STUB("write (the link cable's port)");
        return -1;
    }
    slot = &g_port_card_slots[file->slot];
    want = (u32)(count > 0 ? count : 0);
    if (want > file->size - file->at) {
        want = file->size - file->at;
    }
    if (!port_CardFileBytes(file, file->at, buf, want, 1)) {
        port_DeliverEvent(HwCARD, EvSpERROR);
        return -1;
    }
    file->at += want;
    port_DeliverEvent(HwCARD, EvSpIOE);
    port_CardReportLater(slot, EvSpIOE, port_CardTransferBlanks(want));
    return (s32)want;
}

/* Fills in one entry of the directory: what the game reads of it is the
 * name and the file's size (memcard_FileExists, memcard_CountFreeBlocks). */
static void port_CardFillEntry(
    PortCardSlot *slot, s32 block, struct DIRENTRY *dir) {
    const u8 *at = port_CardDirOf(slot, block);
    s32 i;

    for (i = 0; i < PORT_CARD_NAME_SIZE; i++) {
        dir->name[i] = (char)at[PORT_CARD_DIR_NAME + i];
    }
    dir->attr = 0;
    dir->size = (s32)port_CardGetWord(at + PORT_CARD_DIR_SIZE);
    dir->next = NULL;
    dir->head = block;
    for (i = 0; i < (s32)sizeof(dir->system); i++) {
        dir->system[i] = 0;
    }
}

/* The next file of the walk from `block` on, or NULL. */
static struct DIRENTRY *port_CardWalk(struct DIRENTRY *dir) {
    PortCardSlot *slot;

    if (dir == NULL || g_port_card_walk_slot < 0) {
        return NULL;
    }
    slot = &g_port_card_slots[g_port_card_walk_slot];
    while (g_port_card_walk_block <= PORT_CARD_BLOCKS) {
        char name[PORT_CARD_NAME_SIZE + 1];
        s32 block = g_port_card_walk_block++;

        port_CardGetName(slot, block, name);
        if (port_CardBlockIsFirst(slot, block) &&
            port_CardNameMatches(g_port_card_walk_pattern, name)) {
            port_CardFillEntry(slot, block, dir);
            return dir;
        }
    }
    g_port_card_walk_slot = -1;
    return NULL;
}

/* The first file of the card whose name fits the pattern after "buPC:"
 * (a pattern of over twenty characters is cut to twenty, which is as long
 * as a name can be anyway). The directory is read here, where the console
 * would have it from the last _card_load: these calls are done when they
 * return and report no event, which is what the game's code takes them
 * for (it waits for none). */
struct DIRENTRY *firstfile(char *name, struct DIRENTRY *dir) {
    PortCardSlot *slot;
    const char *pattern;

    g_port_card_walk_slot = -1;
    if (!port_CardIsCardPath(name)) {
        return NULL;
    }
    slot = port_CardPathSlot(name, &pattern);
    if (!port_CardReady(slot) || !slot->formatted) {
        return NULL;
    }
    if (!port_CardReadImage(slot, 0, slot->header, PORT_CARD_BLOCK_SIZE)) {
        port_CardCannotUse(slot, "the card image cannot be read");
        return NULL;
    }
    slot->formatted = port_CardHeaderIsGood(slot->header);
    if (!slot->formatted) {
        return NULL;
    }
    port_CardCopyName(g_port_card_walk_pattern, pattern);
    g_port_card_walk_slot = (s32)(slot - g_port_card_slots);
    g_port_card_walk_block = 1;
    return port_CardWalk(dir);
}

struct DIRENTRY *nextfile(struct DIRENTRY *dir) { return port_CardWalk(dir); }

/* The BIOS's delete (port_card.h). */
s32 delete(char *name) {
    PortCardSlot *slot;
    const char *in_card;

    if (!port_CardIsCardPath(name)) {
        return 0;
    }
    slot = port_CardPathSlot(name, &in_card);
    if (!port_CardReady(slot) || !slot->formatted) {
        return 0;
    }
    return port_CardDeleteFile(slot, in_card);
}

/* Formats the card of the device "buPC:": a fresh header block, which
 * leaves the card empty. The blocks a file was in are not written, as the
 * BIOS does not write them either. Returns 1 if it was done. */
s32 format(char *fs) {
    PortCardSlot *slot;
    const char *rest;

    if (!port_CardIsCardPath(fs)) {
        return 0;
    }
    slot = port_CardPathSlot(fs, &rest);
    if (!port_CardReady(slot)) {
        return 0;
    }
    port_CardFormatBlock(slot->header);
    if (!port_CardWriteImage(slot, 0, slot->header, PORT_CARD_BLOCK_SIZE)) {
        return 0;
    }
    slot->formatted = 1;
    return 1;
}
