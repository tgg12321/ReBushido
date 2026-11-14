#ifndef RB_SAVE_H
#define RB_SAVE_H

/* The settings' file: ReBushido's own file on the memory card in slot 1,
 * which keeps what the player chose on the options page (save.c). The
 * game's save is another file and is never touched.
 *
 * The file is one block, of which RB_SAVE_FILE_SIZE bytes are written:
 * the header every card file starts with (the title and the icon), then
 *
 *   0x100  "RBST"
 *   0x104  the format's version, RB_SAVE_VERSION
 *   0x105  how many values follow that mean something
 *   0x106  two zeroes
 *   0x108  RB_SAVE_VALUES bytes: the value of each setting at its
 *          RbSaveSlot (rb_settings.h), zero where there is none yet
 *   0x128  the CRC-32 of the 0x28 bytes from 0x100, low byte first
 *
 * and zeroes to the end. A file whose letters, version or checksum are
 * not these is not read: the settings stay as they were. */

#include "common.h"

#define RB_SAVE_FILE_SIZE 0x200
#define RB_SAVE_HEADER_SIZE 0x100
#define RB_SAVE_BLOCKS 1
#define RB_SAVE_VERSION 1
#define RB_SAVE_VALUES 32

/* Where the parts are, from the start of the settings (0x100). */
#define RB_SAVE_AT_MAGIC 0
#define RB_SAVE_AT_VERSION 4
#define RB_SAVE_AT_COUNT 5
#define RB_SAVE_AT_VALUES 8
#define RB_SAVE_AT_CHECKSUM (RB_SAVE_AT_VALUES + RB_SAVE_VALUES)

/* What is known of the card in slot 1. */
enum RbCardStatus {
    RB_CARD_UNKNOWN = 0,     /* not looked at yet */
    RB_CARD_READY = 1,       /* the file is there, or there is room for it */
    RB_CARD_NONE = 2,        /* no card, or one that does not answer */
    RB_CARD_UNFORMATTED = 3, /* never formatted here: left as it is */
    RB_CARD_FULL = 4,        /* no file of ours and no free block */
};

/* What became of the settings' file, for the page and the play tests. */
enum RbSaveResult {
    RB_SAVE_NOTHING = 0, /* nothing was read or written */
    RB_SAVE_LOADED = 1,  /* the settings were read from the file */
    RB_SAVE_BAD = 2,     /* a file was there and could not be used */
    RB_SAVE_WRITTEN = 3, /* the settings were written */
    RB_SAVE_FAILED = 4,  /* they were to be written and were not */
};

extern u8 g_rb_card_status; /* an RbCardStatus */
extern u8 g_rb_save_result; /* an RbSaveResult */
extern const char g_rb_save_file_name[];

/* The CRC-32 of `size` bytes (the one of zip and PNG). */
extern u32 rb_Checksum(const u8 *bytes, s32 size);
/* Writes the file, RB_SAVE_FILE_SIZE bytes, from g_rb_settings. */
extern void rb_PackSave(u8 *image);
/* Reads g_rb_settings from a file's bytes. Returns FALSE, with the
 * settings untouched, if it is not a file this version can read. */
extern s32 rb_UnpackSave(const u8 *image);

/* One step of looking at the card: call once a frame while the page is
 * up. g_rb_card_status follows. */
extern void rb_PollCard(void);
/* Ends a look that rb_PollCard left under way, so that the game's probe
 * is left where it starts from. */
extern void rb_QuietCard(void);
/* Looks at the card and reads the settings from it, if it has them.
 * Never asks anything and never writes. */
extern void rb_LoadSave(void);
/* Writes the settings to a card that g_rb_card_status says is ready.
 * Returns TRUE if they were written. */
extern s32 rb_WriteSave(void);

#endif /* RB_SAVE_H */
