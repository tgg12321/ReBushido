#ifndef PORT_CARD_H
#define PORT_CARD_H

/* The native port's memory card (src/port/card.c; docs/PORT.md, "The
 * memory card"): a standard raw card image, 128 KB, one file a slot, under
 * the BIOS calls the game makes. What is declared here is the port's own;
 * the BIOS's own names (open, read, firstfile, _card_info and the rest)
 * are Sony's headers' and src/port/card.c defines them. The PlayStation
 * builds never see this header. */

#include "common.h"

/* ---- the image ---- */

/* A card is a header block and fifteen blocks of 8192 bytes; a block is
 * 64 frames of 128 bytes, which is what the card reads and writes at a
 * time. */
#define PORT_CARD_FRAME_SIZE 128
#define PORT_CARD_BLOCK_SIZE 0x2000
#define PORT_CARD_FRAMES_PER_BLOCK (PORT_CARD_BLOCK_SIZE / PORT_CARD_FRAME_SIZE)
/* The blocks a file may be in: block 0 is the header. */
#define PORT_CARD_BLOCKS 15
#define PORT_CARD_IMAGE_SIZE                                                   \
    ((u32)(PORT_CARD_BLOCKS + 1) * PORT_CARD_BLOCK_SIZE)
#define PORT_CARD_FRAMES (PORT_CARD_IMAGE_SIZE / PORT_CARD_FRAME_SIZE)
#define PORT_CARD_SLOTS 2

/* The frames of the header block. The directory has a frame for each of
 * the fifteen blocks, then come the twenty entries of the list of broken
 * frames and the twenty frames that replace them; the last frame of all
 * is the identifier again. */
#define PORT_CARD_ID_FRAME 0
#define PORT_CARD_DIR_FRAME 1
#define PORT_CARD_BROKEN_FRAME 16
#define PORT_CARD_BROKEN_ENTRIES 20
#define PORT_CARD_ID_COPY_FRAME 63

/* A frame's last byte is the exclusive or of its other 127. */
#define PORT_CARD_SUM_AT (PORT_CARD_FRAME_SIZE - 1)

/* The identifier frame: two letters and nothing else. */
#define PORT_CARD_ID_HIGH 'M'
#define PORT_CARD_ID_LOW 'C'

/* A directory frame, by offset: what the block is, the whole file's size
 * in bytes (in the frame of its first block), the next block of the file,
 * and the file's name. */
#define PORT_CARD_DIR_STATE 0
#define PORT_CARD_DIR_SIZE 4
#define PORT_CARD_DIR_NEXT 8
#define PORT_CARD_DIR_NAME 10
/* A file's name is twenty characters and a zero. */
#define PORT_CARD_NAME_SIZE 20

/* A block's state. The high half says whether a file has it (5), whether
 * it is free (A) or whether it cannot be used (F); the low half, which
 * part of its file it is. A file of one block is "first" with no next. */
#define PORT_CARD_FREE 0xA0
#define PORT_CARD_FIRST 0x51
#define PORT_CARD_MIDDLE 0x52
#define PORT_CARD_LAST 0x53
#define PORT_CARD_UNUSABLE 0xFF
/* PORT_CARD_DIR_NEXT where the file ends here, and a block number counted
 * from 0 (block 1 of the card) where it does not. */
#define PORT_CARD_NO_NEXT 0xFFFF
/* An entry of the list of broken frames that names none. */
#define PORT_CARD_NOT_BROKEN 0xFFFFFFFFU

/* ---- how long a card operation takes ---- */

/* The card moves one frame of 128 bytes for each vertical blank, which is
 * the console's speed as far as it is known: the BIOS's driver takes a
 * frame at each of its turns, and a whole block of 64 frames takes about
 * the second a console takes to write one (docs/PORT.md, "The memory
 * card"). */
#define PORT_CARD_FRAME_BLANKS 1
/* Asking whether a card is there is one command. */
#define PORT_CARD_INFO_BLANKS 1
/* Reading the directory is the identifier's frame and the fifteen
 * directory frames. */
#define PORT_CARD_LOAD_BLANKS 16

/* ---- the port's own ---- */

/* The image of the card in slot `slot` (0 or 1): --card1, --card2. A path
 * that is not there is made, formatted and empty, when the game first
 * looks at the card. */
extern void port_SetCardPath(s32 slot, const char *path);
/* Both slots empty, whatever else was said: --no-cards. */
extern void port_SetCardsOff(void);
/* The whole image of a slot's card as this program would play on it, into
 * `out` (PORT_CARD_IMAGE_SIZE): its file, or a card formatted and empty
 * where there is no file yet. 0 where the slot is empty (--no-cards). */
extern s32 port_ReadCardFile(s32 slot, u8 *out);
/* The slot's card is a copy of `image`, kept in memory, which is part of
 * the game's state: nothing is read from or written to a file for it.
 * NULL empties the slot. Before the game starts: netplay's guest plays on
 * the host's cards, and neither machine writes the session's saves. */
extern void port_SetCardImage(s32 slot, const u8 *image);
/* A replay's start was just put in (port_replay.h): its cards are
 * another run's, so each that had a file is kept in memory from now on,
 * its header block as the fight's start had it, and no file is read or
 * written for it until the game's own state is put back. */
extern void port_CardsForReplay(void);
/* `card1.mcd` and `card2.mcd` beside the program, for the slots that
 * --card1 and --card2 did not name: `program` is the program's own path
 * (argv[0]). */
extern void port_SetDefaultCardPaths(const char *program);
/* The card driver's turn at a vertical blank (clock.c): a card operation
 * that is done by now reports itself through its event. */
extern void port_RunCardBlank(u32 blank);

/* The BIOS's _new_card with its one parameter written. Sony's libapi.h
 * leaves the parameters out, because Sony's _card_clear passes its
 * channel along to a function that is declared to take none. */
extern void _new_card(s32 chan);

/* The BIOS's delete, which takes a card file's path ("bu00:NAME"): 1 if
 * the file was there and is gone, 0 if not. Sony's libapi.h of this
 * project does not declare it, because the game never calls it; the port
 * has it because the boundary is the BIOS's, and tools/card.py and the
 * host test use it. */
extern s32 delete(char *name);

#endif /* PORT_CARD_H */
