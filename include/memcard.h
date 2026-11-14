#ifndef MEMCARD_H
#define MEMCARD_H

/* Memory card.
 *   memcard.c        BIOS card I/O, the card probe, formatting
 *   memcard_save.c   the save image, the save / load request, the screens */

#include "common.h"

/* What the BIOS reported for a card operation (memcard_PollSwEvent and
 * friends). */
enum CardEvent {
    CARD_EVENT_NONE = 0,
    CARD_EVENT_DONE = 1,
    CARD_EVENT_ERROR = 2,
    CARD_EVENT_TIMEOUT = 3,
    CARD_EVENT_NEW_CARD = 4,
};

/* Result of memcard_ProbeCard. */
enum CardProbeResult {
    CARD_PROBE_ERROR = -3,
    CARD_PROBE_UNFORMATTED = -2,
    CARD_PROBE_NO_CARD = -1,
    CARD_PROBE_BUSY = 0,
    CARD_PROBE_READY = 1,
    CARD_PROBE_READY_NEW_CARD = 2,
};

/* What memcard_Update reports for the current request. */
enum MemcardStatus {
    MEMCARD_STATUS_SAVE_BUSY = 1,
    MEMCARD_STATUS_SAVE_DONE = 2,
    MEMCARD_STATUS_SAVE_FAILED = 3,
    MEMCARD_STATUS_LOAD_BUSY = 4,
    MEMCARD_STATUS_LOAD_DONE = 5,
    MEMCARD_STATUS_LOAD_FAILED = 6,
    MEMCARD_STATUS_CARD_FULL = 7,
    MEMCARD_STATUS_NO_CARD = 8,
    MEMCARD_STATUS_FORMAT_BUSY = 9,
    MEMCARD_STATUS_UNFORMATTED = 10,
    MEMCARD_STATUS_FORMAT_DONE = 11,
    MEMCARD_STATUS_FORMAT_FAILED = 12,
    MEMCARD_STATUS_SAVE_FILE_EXISTS = 13,
    MEMCARD_STATUS_NO_SAVE_FILE = 14,
    MEMCARD_STATUS_LOAD_BAD_DATA = 15,
};

/* What the game saves (g_saved_record, 0x24 bytes) is the unlocks, the
 * three best times and the option flags. A best time of chanbara: who made
 * it, with which weapon, and the time. */
typedef struct {
    u8 holder_character_index;
    u8 holder_weapon_id;
    s32 time_frames;
} TimeRec;

#define SAVE_TIME_COUNT 3

/* The save record. */
typedef struct {
    s32 unlock_mask;
    u8 staff_roll_character_mask;
    u8 pad05[3];
    TimeRec times[SAVE_TIME_COUNT];
    u8 button_config[3]; /* see pad.h */
    u8 flags;            /* SAVE_FLAG_... */
} SaveRecord;

/* The fighters that are there from the start (SaveRecord.unlock_mask):
 * KANNUKI, TATSUMI, MIKADO, GENGORO, KAUN and JO. */
#define SAVE_DEFAULT_UNLOCKS 0x7007

/* SaveRecord.flags: the options that are saved. */
#define SAVE_FLAG_GRAYSCALE 1 /* black and white */
#define SAVE_FLAG_EASY_CPU 2  /* weaker CPU opponents */
#define SAVE_FLAG_MUSIC_OFF 4
/* A record with this is not loaded: only its file's patch table is applied.
 * The game never sets it. */
#define SAVE_FLAG_PATCHES_ONLY 0x80

extern SaveRecord g_saved_record;

/* The save file is one 0x200-byte image: the card's file header (title and
 * icon), then SaveData. */
#define MEMCARD_FILE_SIZE 0x200
#define MEMCARD_HEADER_SIZE 0x100
#define SAVE_PATCH_COUNT 22
#define SAVE_COPIES 3

/* Three copies of the save record, each with a byte checksum; loading takes
 * the first copy whose checksum holds. The patch table lets a save file poke
 * 16-bit values into RAM on load; the game always writes it empty. */
typedef struct {
    SaveRecord copies[SAVE_COPIES];
    s32 checksums[SAVE_COPIES];
    u16 *patch_addrs[SAVE_PATCH_COUNT];
    u16 patch_values[SAVE_PATCH_COUNT];
    s32 unused;
} SaveData; /* 0x100 bytes */

/* memcard.c: BIOS card I/O */
extern s32 g_memcard_fd;
extern s32 g_memcard_timeout_counter;
#if RB_OPTIONS_PAGE
/* The card's events, which ReBushido's own look at the card waits on
 * (src/rebushido/save.c). */
extern s32 g_memcard_sw_event_ioe;
extern s32 g_memcard_sw_event_err;
extern s32 g_memcard_sw_event_timeout;
extern s32 g_memcard_sw_event_new;
extern s32 g_memcard_hw_event_ioe;
extern s32 g_memcard_hw_event_err;
extern s32 g_memcard_hw_event_timeout;
extern s32 g_memcard_hw_event_new;
#endif
extern void memcard_Init(void);
extern void memcard_Quit(void);
extern s32 memcard_PollSwEvent(void);
extern s32 memcard_ReadDirectory(s32 port, s32 card);
extern s32 memcard_CountFreeBlocks(void);
extern s32 memcard_FileExists(char *name);
extern s32 memcard_ReadFile(
    s32 port, s32 card, char *name, void *dst, s32 size);
extern s32 memcard_WriteFile(s32 port, s32 card, char *name, void *src,
                             s32 blocks, s32 size, s32 create);

extern s32 memcard_ProbeCard(s32 port, s32 card);
extern s32 memcard_Format(s32 port, s32 card);

/* memcard_save.c: the screens; each returns nonzero once it is finished. */
extern s32 memcard_BootCheckScreen(void);
extern s32 memcard_BootAskLoadScreen(void);
extern s32 memcard_LoadScreen(void);
extern s32 memcard_SaveScreen(void);
extern s32 *memcard_GetSharedBuffer(void);

#endif /* MEMCARD_H */
