/* Saving and loading the game on the memory card: the save image, the
 * background request that reads or writes it, and the screens that drive the
 * request and tell the player what is happening. */
#define INCLUDE_ASM_USE_MACRO_INC 1
#include "common.h"
#include "include_asm.h"
#include "bb2.h"

/* Defined further down. */
static u8 g_memcard_save_title[68];
static u8 g_memcard_title_digits[24];
static u8 g_memcard_title_digits_short[12];
static u8 g_memcard_title_plus_hi;
static u8 g_memcard_title_plus_lo;
static u8 g_unlock_bit_faction[28];
static char *g_memcard_save_file_name;
static s32 g_memcard_request_state;
static s32 g_memcard_card_status;
static u8 g_memcard_verify_only;
static u8 g_memcard_boot_check_restart;
static u8 g_memcard_boot_ask_restart;
static u8 g_memcard_load_screen_restart;
static u8 g_memcard_save_screen_state;
static u8 g_memcard_format_choice_touched;

SaveRecord g_saved_record;

#define MESSAGE_FRAMES 90 /* how long a result message stays up */
#define MAX_RETRIES 5
/* Probes that find no card before a request gives up. */
#define NO_CARD_PROBES 4
/* The bits of the unlocks that the title counts. */
#define TITLE_UNLOCK_BITS 27

/* Main memory, which is where a save file's patches may write. */
#define RAM_START 0x80000000
#define RAM_END 0x80200000

/* What a request is for. */
enum MemcardOperation {
    MEMCARD_OP_LOAD = 0,
    MEMCARD_OP_SAVE = 1,      /* refuses to replace an existing save */
    MEMCARD_OP_OVERWRITE = 2, /* replaces an existing save */
    MEMCARD_OP_FORMAT = 3,
};

/* Where the current request has got to. */
enum MemcardRequestState {
    MEMCARD_REQUEST_IDLE = 0,
    MEMCARD_REQUEST_PROBE_CARD = 1,
    MEMCARD_REQUEST_WRITE_START = 3,
    MEMCARD_REQUEST_WRITE_WAIT = 4,
    MEMCARD_REQUEST_READ_START = 5,
    MEMCARD_REQUEST_READ_WAIT = 6,
    MEMCARD_REQUEST_FORMAT = 7,
};

/* The save screen shows MemcardStatus, plus these two questions of its own. */
enum {
    MEMCARD_STATUS_ASK_SAVE = 0,
    MEMCARD_STATUS_ASK_OVERWRITE = 17,
};

enum SaveScreenState {
    SAVE_SCREEN_INIT = 0,
    SAVE_SCREEN_ASK_SAVE = 1,
    SAVE_SCREEN_BUSY = 2,
    SAVE_SCREEN_AFTER_FORMAT = 3,
    SAVE_SCREEN_ASK_OVERWRITE = 4,
    SAVE_SCREEN_FORMAT_DECLINED = 5,
};

/* The two answers of a yes / no message box, and a box without them. */
enum {
    CHOICE_NONE = -1,
    CHOICE_YES = 0,
    CHOICE_NO = 1,
};

/* The header every memory-card file starts with (Sony format), by offset. */
#define HEADER_TITLE 0x04 /* Shift-JIS */
#define HEADER_TITLE_SIZE 64
#define HEADER_RESERVED 0x44
#define HEADER_RESERVED_SIZE 28
#define HEADER_ICON_CLUT 0x60
#define HEADER_ICON_COLORS 16
#define HEADER_ICON 0x80
#define HEADER_ONE_ICON_FRAME                                                  \
    0x11                /* the third byte: an icon that does not move */
#define HEADER_BLOCKS 1 /* the fourth: the file's size in blocks */
/* An icon frame: 16 by 16 pixels of four bits. */
#define ICON_FRAME_SIZE 0x80
#define ICON_FRAME_HALFWORDS (ICON_FRAME_SIZE / 2)

/* The save title reads "BB2 Narukagami:? VS Shainto:?"; the two question
 * marks become the number of characters unlocked on each side, and "+N" is
 * appended for unlocked extra characters. Byte offsets into the title: */
#define TITLE_NARUKAGAMI_COUNT 30
#define TITLE_SHAINTO_COUNT 56
#define TITLE_EXTRA_COUNT 60

enum UnlockFaction {
    FACTION_NARUKAGAMI = 0,
    FACTION_SHAINTO = 1,
    FACTION_EXTRA = 2,
    FACTION_NONE = 3, /* the unlock is not a character */
};

/* The request. */
s16 g_memcard_request_operation;
s16 g_memcard_request_status;
s16 g_memcard_request_retry_count;
u8 g_memcard_request_write_started;

static u8 g_memcard_boot_ask_choice;
static u8 g_memcard_load_no_card_retries;
static u8 g_memcard_load_verify_passes;
static u8 g_memcard_load_no_file_retries;
static u8 g_memcard_load_unformatted_retries;
static u8 g_memcard_load_bad_data_retries;
static u8 g_memcard_load_message_timer;
static u8 g_memcard_load_unused; /* cleared, never read */
static u8 g_memcard_load_verified;
static u8 g_memcard_load_no_card;
static u8 g_memcard_save_verify_retries;
static u8 g_memcard_save_unformatted_retries;
static u8 g_memcard_save_no_card_retries;
static u8 g_memcard_save_message_timer;
static u8 g_memcard_save_choice;
static u8 g_memcard_save_no_card;

/* Fills in the save data from g_saved_record. */
static void memcard_PackSaveData(u8 *dst) {
    SaveData *data;
    s32 checksum;
    u8 *byte;
    s32 i;
    s32 j;

    checksum = 0;
    byte = (u8 *)&g_saved_record;
    for (i = 0; i < sizeof(SaveRecord); i++) {
        checksum += *byte++;
    }

    /* FAKE: assigned here and not where it is declared */
    data = (SaveData *)dst;
    for (i = 0; i < SAVE_COPIES; i++) {
        data->copies[i] = g_saved_record;
        data->checksums[i] = checksum;
        for (j = 0; j < SAVE_PATCH_COUNT; j++) {
            SET_PTR(data->patch_addrs[j], NULL);
            data->patch_values[j] = 0;
        }
    }
    data->unused = 0;
}

/* Copies loaded save data into g_saved_record. Returns 0 if no copy of the
 * record has a good checksum. */
static s32 memcard_UnpackSaveData(SaveData *data) {
    s32 i;
    s32 j;

    for (i = 0; i < SAVE_COPIES; i++) {
        s32 checksum;
        u8 *byte;

        checksum = 0;
        byte = (u8 *)&data->copies[i];
        for (j = 0; j < sizeof(SaveRecord); j++) {
            checksum += *byte;
            byte++;
        }
        if (checksum == data->checksums[i]) {
            break;
        }
    }

    if (i == SAVE_COPIES) {
        return 0;
    }

    if (g_memcard_verify_only) {
        return 1;
    }

    {
        SaveRecord *record = &data->copies[i];

        if (!(record->flags & SAVE_FLAG_PATCHES_ONLY)) {
            g_saved_record = *record;
        }

        for (j = 0; j < SAVE_PATCH_COUNT; j++) {
            u16 *addr = PTR(data->patch_addrs[j]);
            if ((u32)addr >= RAM_START && (u32)addr < RAM_END) {
                *addr = data->patch_values[j];
            }
        }
    }

    return 1;
}

static void memcard_ClearBuffer(void) {
    u8 *byte = g_shared_buffer;
    u32 i;

    for (i = 0; i < MEMCARD_FILE_SIZE; i++) {
        *byte++ = 0;
    }
}

/* The icon's sixteen colours, from white down to red and then black, and
 * sixteen more that nothing reads. */
static const u16 g_memcard_icon_clut[32] = {
    0x7FFF, 0x77DF, 0x6B9F, 0x635E, 0x5B1E, 0x56FD, 0x4ABD, 0x427C,
    0x323C, 0x2DFB, 0x21BB, 0x199B, 0x115A, 0x0D3A, 0x051A, 0x8000,
    0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
    0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
};
/* The icon: sixteen rows of sixteen pixels, a row to a line; a pixel is
 * four bits, the left one of a pair in the low half of its byte. */
/* clang-format off */
static const u8 g_memcard_icon_image[ICON_FRAME_SIZE] = {
    0x00, 0x00, 0x00, 0x00, 0x10, 0x51, 0x05, 0x00,
    0x00, 0x00, 0x00, 0x00, 0xA3, 0xC5, 0x3E, 0x00,
    0x00, 0x10, 0x53, 0x38, 0xEA, 0x58, 0x29, 0x00,
    0x00, 0x84, 0xBA, 0x7D, 0xDC, 0x8D, 0x03, 0x00,
    0x00, 0x10, 0x41, 0xB7, 0xDD, 0xBB, 0x1C, 0x00,
    0x00, 0x94, 0xDB, 0xCD, 0xAD, 0x11, 0x01, 0x00,
    0x00, 0xE8, 0x8E, 0x66, 0x8E, 0x00, 0x02, 0x00,
    0x00, 0xA9, 0x16, 0x62, 0x8E, 0x00, 0x17, 0x00,
    0x10, 0x32, 0xC6, 0x6A, 0xAE, 0x00, 0x18, 0x00,
    0x10, 0x84, 0x9A, 0x44, 0xDD, 0x03, 0x29, 0x00,
    0x00, 0x00, 0x50, 0x38, 0xEB, 0x08, 0x6B, 0x00,
    0x00, 0x30, 0xD9, 0x06, 0xE7, 0x5D, 0x6B, 0x00,
    0x00, 0xE5, 0x6C, 0x00, 0x91, 0xCD, 0x6D, 0x00,
    0x00, 0xC3, 0x09, 0x00, 0x20, 0xEA, 0x7E, 0x00,
    0x00, 0x10, 0x00, 0x00, 0x00, 0xB2, 0x8E, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x02, 0x00,
};
/* clang-format on */

/* Builds the file header: the title, with its unlock counts, and the icon.
 * The loops keep the pointer-walking shape the compiled code needs. */
static void memcard_BuildFileHeader(u8 *out) {
    s32 narukagami_count, shainto_count, extra_count;
    s32 i;
    s32 unlocks;
    s32 bit;

    extra_count = 0;
    shainto_count = 0;
    narukagami_count = 0;
    unlocks = g_saved_record.unlock_mask;

    for (i = 0; i < TITLE_UNLOCK_BITS; i++) {
        bit = 1 << i;
        if (unlocks & bit) {
            s32 faction = g_unlock_bit_faction[i];
            switch (faction) {
            case FACTION_NARUKAGAMI:
                narukagami_count++;
                break;
            case FACTION_SHAINTO:
                shainto_count++;
                break;
            case FACTION_EXTRA:
                extra_count++;
                break;
            }
        }
    }

    out[0] = 'S';
    out[1] = 'C';
    out[2] = HEADER_ONE_ICON_FRAME;
    out[3] = HEADER_BLOCKS;

    for (i = 0; i < HEADER_TITLE_SIZE; i++) {
        out[HEADER_TITLE + i] = 0;
    }

    strcpy(out + HEADER_TITLE, g_memcard_save_title);

    out[HEADER_TITLE + TITLE_NARUKAGAMI_COUNT + 0] =
        g_memcard_title_digits[narukagami_count * 2 + 0];
    out[HEADER_TITLE + TITLE_NARUKAGAMI_COUNT + 1] =
        g_memcard_title_digits[narukagami_count * 2 + 1];
    out[HEADER_TITLE + TITLE_SHAINTO_COUNT + 0] =
        g_memcard_title_digits[shainto_count * 2 + 0];
    out[HEADER_TITLE + TITLE_SHAINTO_COUNT + 1] =
        g_memcard_title_digits[shainto_count * 2 + 1];

    if (extra_count > 0) {
        out[HEADER_TITLE + TITLE_EXTRA_COUNT + 0] = g_memcard_title_plus_hi;
        out[HEADER_TITLE + TITLE_EXTRA_COUNT + 1] = g_memcard_title_plus_lo;
        out[HEADER_TITLE + TITLE_EXTRA_COUNT + 2] =
            g_memcard_title_digits_short[extra_count * 2 + 0];
        out[HEADER_TITLE + TITLE_EXTRA_COUNT + 3] =
            g_memcard_title_digits_short[extra_count * 2 + 1];
    }

    for (i = 0; i < HEADER_RESERVED_SIZE; i++) {
        out[HEADER_RESERVED + i] = 0;
    }

    i = 0;
    {
        const u16 *src = g_memcard_icon_clut;
        u8 *dst = out;
        do {
            *(u16 *)(dst + HEADER_ICON_CLUT) = *src;
            src++;
            i++;
            dst += 2;
        } while (i < HEADER_ICON_COLORS);
    }

    /* the one icon frame, copied a halfword at a time */
    i = 0;
    {
        const u8 *frame = g_memcard_icon_image;
        u8 *frame_out = out;
        do {
            s32 j = 0;
            u16 *dst = (u16 *)(frame_out + HEADER_ICON);
            const u16 *src = (const u16 *)frame;
            do {
                *dst = *src;
                src++;
                j++;
                dst++;
            } while (j < ICON_FRAME_HALFWORDS);
            frame += ICON_FRAME_SIZE;
            i++;
            frame_out += ICON_FRAME_SIZE;
        } while (i <= 0);
    }
}

/* Advances the request: waits for the card probe, then starts the read,
 * write or format. */
static void memcard_RunRequest(void) {
    s32 free_blocks;
    s32 create;

    if (g_memcard_request_state == MEMCARD_REQUEST_PROBE_CARD) {
        /* Waiting for the card probe. */
        switch (g_memcard_card_status) {
        case CARD_PROBE_READY:
        case CARD_PROBE_READY_NEW_CARD:
            if (g_memcard_request_operation == MEMCARD_OP_LOAD) {
                g_memcard_request_state = MEMCARD_REQUEST_READ_START;
            } else if (g_memcard_request_operation == MEMCARD_OP_FORMAT) {
                g_memcard_request_state = MEMCARD_REQUEST_FORMAT;
            } else {
                g_memcard_request_state = MEMCARD_REQUEST_WRITE_START;
            }
            break;
        case CARD_PROBE_UNFORMATTED:
            if (g_memcard_request_operation == MEMCARD_OP_FORMAT) {
                g_memcard_request_state = MEMCARD_REQUEST_FORMAT;
            } else {
                g_memcard_request_state = MEMCARD_REQUEST_IDLE;
                g_memcard_request_status = MEMCARD_STATUS_UNFORMATTED;
            }
            break;
        case CARD_PROBE_NO_CARD:
            g_memcard_request_retry_count++;
            if (g_memcard_request_retry_count >= NO_CARD_PROBES) {
                g_memcard_request_status = MEMCARD_STATUS_NO_CARD;
                g_memcard_request_state = MEMCARD_REQUEST_IDLE;
            }
            break;
        }
        return;
    }

    switch (g_memcard_request_state) {
    case MEMCARD_REQUEST_WRITE_START:
        g_memcard_request_status = MEMCARD_STATUS_SAVE_BUSY;
        memcard_ReadDirectory(0, 0);
        free_blocks = memcard_CountFreeBlocks();
        if (memcard_FileExists(g_memcard_save_file_name)) {
            create = 0;
            if (g_memcard_request_operation == MEMCARD_OP_SAVE) {
                g_memcard_request_status = MEMCARD_STATUS_SAVE_FILE_EXISTS;
                g_memcard_request_state = MEMCARD_REQUEST_IDLE;
                break;
            }
        } else {
            create = 1;
            if (free_blocks == 0) {
                g_memcard_request_status = MEMCARD_STATUS_CARD_FULL;
                g_memcard_request_state = MEMCARD_REQUEST_IDLE;
                break;
            }
        }
        g_memcard_request_write_started = 1;
        memcard_ClearBuffer();
        memcard_BuildFileHeader(g_shared_buffer);
        memcard_PackSaveData(g_shared_buffer + MEMCARD_HEADER_SIZE);
        if (memcard_WriteFile(0, 0, g_memcard_save_file_name, g_shared_buffer,
                              HEADER_BLOCKS, MEMCARD_FILE_SIZE, create) != 0) {
            close(g_memcard_fd);
            g_memcard_request_status = MEMCARD_STATUS_SAVE_FAILED;
            g_memcard_request_state = MEMCARD_REQUEST_IDLE;
        } else {
            g_memcard_request_state = MEMCARD_REQUEST_WRITE_WAIT;
        }
        break;
    case MEMCARD_REQUEST_READ_START:
        memcard_ReadDirectory(0, 0);
        memcard_CountFreeBlocks();
        if (!memcard_FileExists(g_memcard_save_file_name)) {
            g_memcard_request_status = MEMCARD_STATUS_NO_SAVE_FILE;
            g_memcard_request_state = MEMCARD_REQUEST_IDLE;
            break;
        }
        g_memcard_request_status = MEMCARD_STATUS_LOAD_BUSY;
        memcard_ClearBuffer();
        if (memcard_ReadFile(0, 0, g_memcard_save_file_name, g_shared_buffer,
                             MEMCARD_FILE_SIZE) != 0) {
            close(g_memcard_fd);
            g_memcard_request_status = MEMCARD_STATUS_LOAD_FAILED;
            g_memcard_request_state = MEMCARD_REQUEST_IDLE;
        } else {
            g_memcard_request_state = MEMCARD_REQUEST_READ_WAIT;
        }
        break;
    case MEMCARD_REQUEST_FORMAT:
        if (memcard_Format(0, 0) != 0) {
            g_memcard_request_status = MEMCARD_STATUS_FORMAT_DONE;
        } else {
            g_memcard_request_status = MEMCARD_STATUS_FORMAT_FAILED;
        }
        g_memcard_request_state = MEMCARD_REQUEST_IDLE;
        break;
    }
}

/* Finishes a read or write once the card reports it complete. */
static void memcard_FinishFileIo(void) {
    s32 event;
    s32 busy_status;

    switch (g_memcard_request_state) {
    case MEMCARD_REQUEST_WRITE_WAIT:
        event = memcard_PollSwEvent();
        if (event == CARD_EVENT_NONE) {
            busy_status = MEMCARD_STATUS_SAVE_BUSY;
            break;
        }
        close(g_memcard_fd);
        if (event == CARD_EVENT_DONE) {
            g_memcard_request_status = MEMCARD_STATUS_SAVE_DONE;
        } else {
            g_memcard_request_status = MEMCARD_STATUS_SAVE_FAILED;
        }
        g_memcard_request_state = MEMCARD_REQUEST_IDLE;
        return;
    case MEMCARD_REQUEST_READ_WAIT:
        event = memcard_PollSwEvent();
        if (event == CARD_EVENT_NONE) {
            busy_status = MEMCARD_STATUS_LOAD_BUSY;
            break;
        }
        close(g_memcard_fd);
        if (event == CARD_EVENT_DONE) {
            g_memcard_request_status = MEMCARD_STATUS_LOAD_DONE;
            if (memcard_UnpackSaveData(
                    (SaveData *)(g_shared_buffer + MEMCARD_HEADER_SIZE)) == 0) {
                g_memcard_request_status = MEMCARD_STATUS_LOAD_BAD_DATA;
            }
        } else {
            g_memcard_request_status = MEMCARD_STATUS_LOAD_FAILED;
        }
        g_memcard_request_state = MEMCARD_REQUEST_IDLE;
        return;
    default:
        return;
    }
    /* Not finished yet. */
    g_memcard_request_status = busy_status;
}

/* Runs the current request for one frame and returns its MemcardStatus. Call
 * every frame while a memory-card screen is up. */
s32 memcard_Update(void) {
    if (g_memcard_request_state == MEMCARD_REQUEST_IDLE ||
        g_memcard_request_state == MEMCARD_REQUEST_PROBE_CARD) {
        g_memcard_card_status = memcard_ProbeCard(0, 0);
    }
    memcard_RunRequest();
    memcard_FinishFileIo();
    return g_memcard_request_status;
}

static void memcard_RequestLoad(void) {
    g_memcard_request_status = MEMCARD_STATUS_LOAD_BUSY;
    g_memcard_request_retry_count = 0;
    g_memcard_request_operation = MEMCARD_OP_LOAD;
    g_memcard_request_state = MEMCARD_REQUEST_PROBE_CARD;
}

static void memcard_RequestSave(void) {
    g_memcard_request_status = MEMCARD_STATUS_SAVE_BUSY;
    g_memcard_request_operation = MEMCARD_OP_SAVE;
    g_memcard_request_write_started = 0;
    g_memcard_request_retry_count = 0;
    g_memcard_request_state = MEMCARD_REQUEST_PROBE_CARD;
}

static void memcard_RequestOverwrite(void) {
    g_memcard_request_status = MEMCARD_STATUS_SAVE_BUSY;
    g_memcard_request_operation = MEMCARD_OP_OVERWRITE;
    g_memcard_request_write_started = 0;
    g_memcard_request_retry_count = 0;
    g_memcard_request_state = MEMCARD_REQUEST_PROBE_CARD;
}

static void memcard_RequestFormat(void) {
    g_memcard_request_status = MEMCARD_STATUS_FORMAT_BUSY;
    g_memcard_request_operation = MEMCARD_OP_FORMAT;
    g_memcard_request_retry_count = 0;
    g_memcard_request_state = MEMCARD_REQUEST_PROBE_CARD;
}

/* Boot: looks for a usable save on the card while showing "checking".
 * Returns 1 if there is one, -1 if not, 0 while still looking. */
s32 memcard_BootCheckScreen(void) {
    s32 result;

    result = 0;
    if (g_memcard_boot_check_restart) {
        g_memcard_boot_check_restart = 0;
        g_memcard_verify_only = 1;
        memcard_RequestLoad();
    }
    switch (memcard_Update()) {
    case MEMCARD_STATUS_LOAD_BUSY:
        break;
    case MEMCARD_STATUS_LOAD_DONE:
        result = 1;
        break;
    case MEMCARD_STATUS_LOAD_FAILED:
        result = -1;
        break;
    case MEMCARD_STATUS_CARD_FULL:
        result = -1;
        break;
    case MEMCARD_STATUS_NO_CARD:
        result = -1;
        break;
    case MEMCARD_STATUS_FORMAT_BUSY:
        result = -1;
        break;
    case MEMCARD_STATUS_UNFORMATTED:
        result = -1;
        break;
    case MEMCARD_STATUS_FORMAT_DONE:
        result = -1;
        break;
    case MEMCARD_STATUS_FORMAT_FAILED:
        result = -1;
        break;
    case MEMCARD_STATUS_SAVE_FILE_EXISTS:
        result = -1;
        break;
    case MEMCARD_STATUS_NO_SAVE_FILE:
        result = -1;
        break;
    case MEMCARD_STATUS_LOAD_BAD_DATA:
        result = -1;
        break;
    default:
        result = -1;
        break;
    }
    menu_DrawMessageBox(MSG_CHECKING, CHOICE_NONE);
    if (result) {
        g_memcard_boot_check_restart = 1;
        g_memcard_verify_only = 0;
    }
    return result;
}

/* Boot: asks whether to load the save that was found. Returns 1 for yes and
 * -1 for no. */
s32 memcard_BootAskLoadScreen(void) {
    s32 result = 0;
    u32 pressed;

    if (g_memcard_boot_ask_restart) {
        g_memcard_boot_ask_restart = 0;
        g_memcard_boot_ask_choice = CHOICE_YES;
    }
    pressed = g_pad_state.pressed;
    if (pressed & PAD_ANY(PADRdown)) {
        snd_Play(MENU_SOUND_CONFIRM, SND_VOLUME_MAX, SND_VOLUME_MAX);
        if (g_memcard_boot_ask_choice == CHOICE_YES) {
            result = 1;
        } else {
            result = -1;
        }
    } else if (pressed & PAD_ANY(PADLleft)) {
        snd_Play(MENU_SOUND_CURSOR, SND_VOLUME_MAX, SND_VOLUME_MAX);
        g_memcard_boot_ask_choice = CHOICE_YES;
    } else if (pressed & PAD_ANY(PADLright)) {
        snd_Play(MENU_SOUND_CURSOR, SND_VOLUME_MAX, SND_VOLUME_MAX);
        g_memcard_boot_ask_choice = CHOICE_NO;
    }
    menu_DrawMessageBox(MSG_ASK_LOAD, g_memcard_boot_ask_choice);
    if (result != 0) {
        g_memcard_boot_ask_restart = 1;
    }
    return result;
}

/* Loads the save and shows how it went. The card is first read MAX_RETRIES
 * times in verify-only mode; only when every pass succeeds is the data loaded
 * for real. Returns 1 once the result message has been dismissed. */
s32 memcard_LoadScreen(void) {
    s32 result = 0;
    s32 status;
    s32 message;

    if (g_memcard_load_screen_restart) {
        g_memcard_load_no_card_retries = 0;
        g_memcard_load_verify_passes = 0;
        g_memcard_load_no_file_retries = 0;
        g_memcard_load_unformatted_retries = 0;
        g_memcard_load_bad_data_retries = 0;
        g_memcard_verify_only = 1;
        memcard_RequestLoad();
        g_memcard_load_screen_restart = 0;
        g_memcard_load_message_timer = MESSAGE_FRAMES;
        g_memcard_load_unused = 0;
        g_memcard_load_verified = 0;
        g_memcard_load_no_card = 0;
    }

    status = memcard_Update();

    if (g_memcard_load_no_card) {
        menu_DrawMessageBox(MSG_NO_CARD, CHOICE_NONE);
        g_memcard_load_message_timer--;
        if (g_memcard_load_message_timer == 0 ||
            (g_pad_state.pressed & PAD_ANY(PADRup))) {
            snd_Play(MENU_SOUND_CANCEL, SND_VOLUME_MAX, SND_VOLUME_MAX);
            result = 1;
        }
    } else {

        message = MSG_CHECKING;
        if (!g_memcard_verify_only) {
            if (g_memcard_load_verified == 1) {
                switch (status) {
                case MEMCARD_STATUS_LOAD_BUSY:
                    message = MSG_LOADING;
                    break;
                case MEMCARD_STATUS_LOAD_DONE:
                    message = MSG_LOAD_DONE;
                    break;
                case MEMCARD_STATUS_LOAD_FAILED:
                case MEMCARD_STATUS_UNFORMATTED:
                case MEMCARD_STATUS_NO_SAVE_FILE:
                case MEMCARD_STATUS_LOAD_BAD_DATA:
                    message = MSG_LOAD_FAILED;
                    break;
                case MEMCARD_STATUS_NO_CARD:
                    message = MSG_NONE;
                    break;
                default:
                    message = MSG_CHECKING;
                    break;
                }
            } else {
                switch (status) {
                case MEMCARD_STATUS_LOAD_BUSY:
                    message = MSG_LOADING;
                    break;
                case MEMCARD_STATUS_LOAD_DONE:
                    message = MSG_LOAD_DONE;
                    break;
                case MEMCARD_STATUS_LOAD_FAILED:
                    message = MSG_LOAD_FAILED;
                    break;
                case MEMCARD_STATUS_NO_SAVE_FILE:
                    message = MSG_NO_SAVE_FILE;
                    break;
                case MEMCARD_STATUS_LOAD_BAD_DATA:
                    message = MSG_BAD_DATA;
                    break;
                case MEMCARD_STATUS_NO_CARD:
                    message = MSG_NONE;
                    break;
                case MEMCARD_STATUS_UNFORMATTED:
                    message = MSG_UNFORMATTED;
                    break;
                default:
                    message = MSG_CHECKING;
                    break;
                }
            }
        }

        if (message >= 0) {
            menu_DrawMessageBox(message, CHOICE_NONE);
        }

        if (g_memcard_verify_only) {
            /* Verifying: retry each failure a few times before believing it. */
            switch (status) {
            case MEMCARD_STATUS_LOAD_BUSY:
                break;
            case MEMCARD_STATUS_NO_CARD:
                g_memcard_load_no_card_retries++;
                if (g_memcard_load_no_card_retries >= MAX_RETRIES) {
                    g_memcard_load_no_card = 1;
                    g_memcard_load_message_timer = MESSAGE_FRAMES;
                    break;
                }
                memcard_RequestLoad();
                break;
            case MEMCARD_STATUS_LOAD_BAD_DATA:
                g_memcard_load_bad_data_retries++;
                if (g_memcard_load_bad_data_retries >= MAX_RETRIES) {
                    g_memcard_verify_only = 0;
                    break;
                }
                memcard_RequestLoad();
                break;
            case MEMCARD_STATUS_UNFORMATTED:
                g_memcard_load_unformatted_retries++;
                if (g_memcard_load_unformatted_retries >= MAX_RETRIES) {
                    g_memcard_verify_only = 0;
                    break;
                }
                memcard_RequestLoad();
                break;
            case MEMCARD_STATUS_NO_SAVE_FILE:
                g_memcard_load_no_file_retries++;
                if (g_memcard_load_no_file_retries >= MAX_RETRIES) {
                    g_memcard_verify_only = 0;
                    break;
                }
                memcard_RequestLoad();
                break;
            case MEMCARD_STATUS_LOAD_FAILED:
                g_memcard_verify_only = 0;
                break;
            case MEMCARD_STATUS_LOAD_DONE:
                g_memcard_load_verify_passes++;
                if (g_memcard_load_verify_passes >= MAX_RETRIES) {
                    g_memcard_verify_only = 0;
                    g_memcard_load_verified = 1;
                }
                memcard_RequestLoad();
                break;
            default:
                break;
            }
        } else {
            switch (status) {
            case MEMCARD_STATUS_NO_CARD:
                g_memcard_load_no_card = 1;
                g_memcard_load_message_timer = MESSAGE_FRAMES;
                break;
            case MEMCARD_STATUS_LOAD_DONE:
            case MEMCARD_STATUS_LOAD_FAILED:
            case MEMCARD_STATUS_UNFORMATTED:
            case MEMCARD_STATUS_NO_SAVE_FILE:
            case MEMCARD_STATUS_LOAD_BAD_DATA:
                /* A result message: wait for it to be dismissed. */
                g_memcard_load_message_timer--;
                if (g_memcard_load_message_timer == 0 ||
                    (g_pad_state.pressed & PAD_ANY(PADRup))) {
                    snd_Play(MENU_SOUND_CANCEL, SND_VOLUME_MAX, SND_VOLUME_MAX);
                    result = 1;
                }
                break;
            default:
                break;
            }
        }
    }

    if (result != 0) {
        g_memcard_load_screen_restart = 1;
    }
    return result;
}

/* Saves the game, asking first, and offering to overwrite an existing save
 * or format an unformatted card. Like loading, the card is checked in
 * verify-only mode before anything is written. Returns 1 when the player is
 * done with the screen. */
s32 memcard_SaveScreen(void) {
    s32 result = 0;
    s32 choice = CHOICE_NONE;
    s32 status;
    s32 message;

    if (!g_memcard_save_screen_state) {
        g_memcard_save_screen_state = SAVE_SCREEN_ASK_SAVE;
        g_memcard_save_message_timer = MESSAGE_FRAMES;
        g_memcard_save_choice = CHOICE_YES;
        g_memcard_save_no_card = 0;
        g_memcard_verify_only = 0;
    }

    status = memcard_Update();

    if (g_memcard_save_no_card) {
        menu_DrawMessageBox(MSG_NO_CARD, CHOICE_NONE);
        g_memcard_save_message_timer--;
        if (g_memcard_save_message_timer == 0 ||
            (g_pad_state.pressed & PAD_ANY(PADRup))) {
            snd_Play(MENU_SOUND_CANCEL, SND_VOLUME_MAX, SND_VOLUME_MAX);
            g_memcard_save_screen_state = SAVE_SCREEN_ASK_SAVE;
            g_memcard_save_message_timer = MESSAGE_FRAMES;
            g_memcard_save_choice = CHOICE_YES;
            g_memcard_save_no_card = 0;
            g_memcard_verify_only = 0;
        }
    } else {

        /* While a question is up, it stands in for the request status. */
        if (g_memcard_save_screen_state == SAVE_SCREEN_ASK_SAVE) {
            status = MEMCARD_STATUS_ASK_SAVE;
        } else if (g_memcard_save_screen_state == SAVE_SCREEN_BUSY) {
            /* FAKE: this arm and the next are empty; read it without them */
        } else if (g_memcard_save_screen_state == SAVE_SCREEN_AFTER_FORMAT) {
        } else if (g_memcard_save_screen_state == SAVE_SCREEN_ASK_OVERWRITE) {
            status = MEMCARD_STATUS_ASK_OVERWRITE;
        }

        /* Pick the message. */
        message = MSG_CHECKING;
        if (!g_memcard_verify_only) {
            if (g_memcard_save_screen_state == SAVE_SCREEN_AFTER_FORMAT) {
                switch (status) {
                case MEMCARD_STATUS_NO_CARD:
                    message = MSG_NONE;
                    break;
                case MEMCARD_STATUS_UNFORMATTED:
                    message = MSG_FORMAT_FAILED;
                    break;
                default:
                    message = MSG_FORMATTING;
                    break;
                }
            } else {
                switch (status) {
                case MEMCARD_STATUS_ASK_SAVE:
                    message = MSG_ASK_SAVE;
                    choice = g_memcard_save_choice;
                    break;
                case MEMCARD_STATUS_SAVE_FILE_EXISTS:
                case MEMCARD_STATUS_ASK_OVERWRITE:
                    message = MSG_ASK_OVERWRITE;
                    choice = g_memcard_save_choice;
                    break;
                case MEMCARD_STATUS_SAVE_BUSY:
                    message = g_memcard_request_write_started ? MSG_SAVING
                                                              : MSG_CHECKING;
                    break;
                case MEMCARD_STATUS_SAVE_DONE:
                    message = MSG_SAVE_DONE;
                    break;
                case MEMCARD_STATUS_SAVE_FAILED:
                    message = MSG_SAVE_FAILED;
                    break;
                case MEMCARD_STATUS_NO_CARD:
                    message = MSG_NONE;
                    break;
                case MEMCARD_STATUS_CARD_FULL:
                    message = MSG_CARD_FULL;
                    break;
                case MEMCARD_STATUS_UNFORMATTED:
                    /* "Format the card?" starts on No. */
                    if (!g_memcard_format_choice_touched) {
                        g_memcard_save_choice = CHOICE_NO;
                    }
                    if (g_pad_state.pressed & PAD_ANY(PADRdown)) {
                        g_memcard_format_choice_touched = 0;
                        snd_Play(
                            MENU_SOUND_CONFIRM, SND_VOLUME_MAX, SND_VOLUME_MAX);
                        message = MSG_FORMATTING;
                        if (g_memcard_save_choice == CHOICE_YES) {
                            memcard_RequestFormat();
                            break;
                        }
                        status = MEMCARD_STATUS_ASK_SAVE;
                        g_memcard_save_screen_state =
                            SAVE_SCREEN_FORMAT_DECLINED;
                        g_memcard_save_message_timer = MESSAGE_FRAMES;
                        g_memcard_save_choice = CHOICE_YES;
                        message = MSG_NONE;
                        break;
                    }
                    message = MSG_ASK_FORMAT;
                    if (g_pad_state.pressed & PAD_ANY(PADLleft | PADLright)) {
                        g_memcard_format_choice_touched = 1;
                    }
                    choice = g_memcard_save_choice;
                    break;
                case MEMCARD_STATUS_FORMAT_BUSY:
                case MEMCARD_STATUS_FORMAT_DONE:
                    message = MSG_FORMATTING;
                    break;
                case MEMCARD_STATUS_FORMAT_FAILED:
                    message = MSG_FORMAT_FAILED;
                    break;
                default:
                    message = MSG_CHECKING;
                    break;
                }
            }
        }

        if (message >= 0) {
            menu_DrawMessageBox(message, choice);
        }

        /* React to the status. */
        if (g_memcard_verify_only) {
            /* Checking the card before saving. */
            switch (status) {
            case MEMCARD_STATUS_LOAD_BUSY:
                break;
            case MEMCARD_STATUS_NO_CARD:
                g_memcard_save_no_card_retries++;
                if (g_memcard_save_no_card_retries >= MAX_RETRIES) {
                    g_memcard_save_no_card = 1;
                    g_memcard_save_message_timer = MESSAGE_FRAMES;
                    break;
                }
                memcard_RequestLoad();
                break;
            case MEMCARD_STATUS_LOAD_DONE:
            case MEMCARD_STATUS_LOAD_FAILED:
            case MEMCARD_STATUS_CARD_FULL:
            case MEMCARD_STATUS_SAVE_FILE_EXISTS:
            case MEMCARD_STATUS_NO_SAVE_FILE:
            case MEMCARD_STATUS_LOAD_BAD_DATA:
                /* The card is there and formatted: go on to save. */
                g_memcard_save_verify_retries++;
                if (g_memcard_save_verify_retries >= MAX_RETRIES) {
                    g_memcard_verify_only = 0;
                    memcard_RequestSave();
                    break;
                }
                memcard_RequestLoad();
                break;
            case MEMCARD_STATUS_UNFORMATTED:
                g_memcard_save_unformatted_retries++;
                if (g_memcard_save_unformatted_retries >= MAX_RETRIES) {
                    g_memcard_verify_only = 0;
                    break;
                }
                memcard_RequestLoad();
                break;
            default:
                break;
            }
        } else if (g_memcard_save_screen_state == SAVE_SCREEN_AFTER_FORMAT) {
            /* Re-reading the card after a format. */
            switch (status) {
            case MEMCARD_STATUS_LOAD_BUSY:
                break;
            case MEMCARD_STATUS_NO_CARD:
                result = 1;
                break;
            case MEMCARD_STATUS_LOAD_DONE:
            case MEMCARD_STATUS_LOAD_FAILED:
            case MEMCARD_STATUS_CARD_FULL:
            case MEMCARD_STATUS_FORMAT_BUSY:
            case MEMCARD_STATUS_FORMAT_DONE:
            case MEMCARD_STATUS_FORMAT_FAILED:
            case MEMCARD_STATUS_SAVE_FILE_EXISTS:
            case MEMCARD_STATUS_NO_SAVE_FILE:
            case MEMCARD_STATUS_LOAD_BAD_DATA:
            default:
                memcard_RequestSave();
                g_memcard_save_screen_state = SAVE_SCREEN_BUSY;
                g_memcard_save_message_timer = MESSAGE_FRAMES;
                g_memcard_save_choice = CHOICE_YES;
                break;
            case MEMCARD_STATUS_UNFORMATTED:
                g_memcard_save_message_timer--;
                if (g_memcard_save_message_timer == 0 ||
                    (g_pad_state.pressed & PAD_ANY(PADRup))) {
                    snd_Play(MENU_SOUND_CANCEL, SND_VOLUME_MAX, SND_VOLUME_MAX);
                    result = 1;
                    break;
                }
                break;
            }
        } else {
            switch (status) {
            case MEMCARD_STATUS_NO_CARD:
                g_memcard_save_no_card = 1;
                g_memcard_save_message_timer = MESSAGE_FRAMES;
                break;
            case MEMCARD_STATUS_SAVE_DONE:
            case MEMCARD_STATUS_SAVE_FAILED:
            case MEMCARD_STATUS_CARD_FULL:
            case MEMCARD_STATUS_FORMAT_FAILED:
                /* A result message: wait for it to be dismissed. */
                g_memcard_save_message_timer--;
                if (g_memcard_save_message_timer == 0 ||
                    (g_pad_state.pressed & PAD_ANY(PADRup))) {
                    snd_Play(MENU_SOUND_CANCEL, SND_VOLUME_MAX, SND_VOLUME_MAX);
                    if (status != MEMCARD_STATUS_CARD_FULL) {
                        result = 1;
                        break;
                    }
                    g_memcard_save_screen_state = SAVE_SCREEN_ASK_SAVE;
                    g_memcard_save_message_timer = MESSAGE_FRAMES;
                    g_memcard_save_choice = CHOICE_YES;
                    g_memcard_save_no_card = 0;
                    g_memcard_verify_only = 0;
                }
                break;
            case MEMCARD_STATUS_FORMAT_DONE:
                g_memcard_verify_only = 1;
                memcard_RequestLoad();
                g_memcard_save_screen_state = SAVE_SCREEN_AFTER_FORMAT;
                g_memcard_save_message_timer = MESSAGE_FRAMES;
                break;
            case MEMCARD_STATUS_SAVE_FILE_EXISTS:
                g_memcard_save_screen_state = SAVE_SCREEN_ASK_OVERWRITE;
                g_memcard_save_message_timer = MESSAGE_FRAMES;
                g_memcard_save_choice = CHOICE_YES;
                break;
            case MEMCARD_STATUS_ASK_SAVE:
                if (g_memcard_save_screen_state ==
                    SAVE_SCREEN_FORMAT_DECLINED) {
                    g_memcard_save_screen_state = SAVE_SCREEN_ASK_SAVE;
                    break;
                }
                /* fall through */
            case MEMCARD_STATUS_ASK_OVERWRITE:
                if (g_pad_state.pressed & PAD_ANY(PADRdown)) {
                    snd_Play(
                        MENU_SOUND_CONFIRM, SND_VOLUME_MAX, SND_VOLUME_MAX);
                    if (status == MEMCARD_STATUS_ASK_SAVE) {
                        if (g_memcard_save_choice != CHOICE_YES) {
                            result = 1;
                            break;
                        }
                        g_memcard_save_no_card_retries = 0;
                        g_memcard_save_verify_retries = 0;
                        g_memcard_save_unformatted_retries = 0;
                        g_memcard_verify_only = 1;
                        memcard_RequestLoad();
                        g_memcard_save_screen_state = SAVE_SCREEN_BUSY;
                    } else if (g_memcard_save_choice == CHOICE_YES) {
                        memcard_RequestOverwrite();
                        g_memcard_save_screen_state = SAVE_SCREEN_BUSY;
                    } else {
                        status = MEMCARD_STATUS_ASK_SAVE;
                        g_memcard_save_screen_state = SAVE_SCREEN_ASK_SAVE;
                        g_memcard_save_message_timer = MESSAGE_FRAMES;
                        g_memcard_save_choice = CHOICE_YES;
                    }
                    break;
                }
                /* fall through */
            case MEMCARD_STATUS_UNFORMATTED:
                if (g_pad_state.pressed & PAD_ANY(PADLleft)) {
                    snd_Play(MENU_SOUND_CURSOR, SND_VOLUME_MAX, SND_VOLUME_MAX);
                    g_memcard_save_choice = CHOICE_YES;
                    break;
                }
                if (g_pad_state.pressed & PAD_ANY(PADLright)) {
                    snd_Play(MENU_SOUND_CURSOR, SND_VOLUME_MAX, SND_VOLUME_MAX);
                    g_memcard_save_choice = CHOICE_NO;
                }
                break;
            default:
                break;
            }
        }

        /* The card being pulled out interrupts a busy or waiting screen. */
        switch (status) {
        case MEMCARD_STATUS_SAVE_BUSY:
        case MEMCARD_STATUS_FORMAT_BUSY:
        case MEMCARD_STATUS_UNFORMATTED:
        case MEMCARD_STATUS_ASK_OVERWRITE:
            if (g_memcard_card_status == CARD_PROBE_NO_CARD) {
                g_memcard_save_no_card = 1;
                g_memcard_save_message_timer = MESSAGE_FRAMES;
            }
            break;
        }
    }

    if (result != 0) {
        g_memcard_save_screen_state = SAVE_SCREEN_INIT;
        g_memcard_verify_only = 0;
    }
    return result;
}

s32 *memcard_GetSharedBuffer(void) { return (s32 *)g_shared_buffer; }

/* The save title and the full-width digits written into it
 * (memcard_BuildFileHeader). All Shift-JIS. */
static u8 g_memcard_title_digits_short[12] = {
    /* "01234" */
    0x82, 0x4F, 0x82, 0x50, 0x82, 0x51, 0x82, 0x52, 0x82, 0x53, 0x00, 0x00,
};
static u8 g_memcard_title_digits[24] = {
    /* "0123456789" */
    0x82, 0x4F, 0x82, 0x50, 0x82, 0x51, 0x82, 0x52, 0x82, 0x53, 0x82, 0x54,
    0x82, 0x55, 0x82, 0x56, 0x82, 0x57, 0x82, 0x58, 0x00, 0x00, 0x00, 0x00,
};
/* "BB2 Narukagami:? VS Shainto:?   ", two bytes a character */
/* clang-format off */
static u8 g_memcard_save_title[68] = {
    /* "BB2 " */
    0x82, 0x61, 0x82, 0x61, 0x82, 0x51, 0x81, 0x40,
    /* "Narukagami" */
    0x82, 0x6D, 0x82, 0x81, 0x82, 0x92, 0x82, 0x95, 0x82, 0x8B,
    0x82, 0x81, 0x82, 0x87, 0x82, 0x81, 0x82, 0x8D, 0x82, 0x89,
    /* ":? " */
    0x81, 0x46, 0x81, 0x48, 0x81, 0x40,
    /* "VS " */
    0x82, 0x75, 0x82, 0x72, 0x81, 0x40,
    /* "Shainto" */
    0x82, 0x72, 0x82, 0x88, 0x82, 0x81, 0x82, 0x89, 0x82, 0x8E,
    0x82, 0x94, 0x82, 0x8F,
    /* ":?   " */
    0x81, 0x46, 0x81, 0x48, 0x81, 0x40, 0x81, 0x40, 0x81, 0x40,
    0x00, 0x00, 0x00, 0x00,
};
/* clang-format on */
/* Which side each bit of the unlocks counts for in the title, by the
 * character whose bit it is; the last bit is not looked at. The two
 * gunners count apart from their clans. The original's data: the
 * victim's bit, which nothing sets, counts with the gunners, and the two
 * crests' bits count for nothing. */
/* clang-format off */
static u8 g_unlock_bit_faction[28] = {
    FACTION_NARUKAGAMI, /* CHARACTER_KANNUKI */
    FACTION_NARUKAGAMI, /* CHARACTER_TATSUMI */
    FACTION_NARUKAGAMI, /* CHARACTER_MIKADO */
    FACTION_NARUKAGAMI, /* CHARACTER_UTSUSEMI */
    FACTION_NARUKAGAMI, /* CHARACTER_RED_SHADOW */
    FACTION_EXTRA,      /* CHARACTER_TSUBAME */
    FACTION_NARUKAGAMI, /* CHARACTER_SAZANKA */
    FACTION_NARUKAGAMI, /* CHARACTER_SUMINAGASHI */
    FACTION_NARUKAGAMI, /* CHARACTER_NIGHTSTALKER */
    FACTION_NARUKAGAMI, /* CHARACTER_MATSUMUSHI */
    FACTION_NONE,       /* CHARACTER_VANISHER */
    FACTION_NONE,       /* CHARACTER_SIDEARM_1 */
    FACTION_SHAINTO,    /* CHARACTER_GENGORO */
    FACTION_SHAINTO,    /* CHARACTER_KAUN */
    FACTION_SHAINTO,    /* CHARACTER_JO */
    FACTION_SHAINTO,    /* CHARACTER_HONGOU */
    FACTION_EXTRA,      /* CHARACTER_KATZE */
    FACTION_SHAINTO,    /* CHARACTER_HIGHWAYMAN */
    FACTION_SHAINTO,    /* CHARACTER_CHIHIRO */
    FACTION_SHAINTO,    /* CHARACTER_UTAMARU */
    FACTION_SHAINTO,    /* CHARACTER_TONY_UMEDA */
    FACTION_SHAINTO,    /* CHARACTER_ISOHACHI */
    FACTION_NONE,       /* CHARACTER_ARMORED */
    FACTION_NONE,       /* CHARACTER_SIDEARM_2 */
    FACTION_NONE,       /* CHARACTER_LAST_1 */
    FACTION_EXTRA,      /* CHARACTER_VICTIM */
    FACTION_NONE,       /* CHARACTER_LAST_2 */
    FACTION_NARUKAGAMI, /* no character */
};
/* clang-format on */

/* Small data, in address order. */
static char *g_memcard_save_file_name = "BASLUS-00663BUSHIDO2";
/* The request. */
static s32 g_memcard_request_state = 0;
/* The latest memcard_ProbeCard result. */
static s32 g_memcard_card_status = 0;
/* Set while a load is only checking what is on the card: the data is
 * validated but not applied. */
static u8 g_memcard_verify_only = 0;
/* Two bytes and, after the "+", one that no code or data names; each's size
 * is the gap it fills (two bytes here could as well be two variables). */
static s16 g_memcard_unused_halfword = 0;
static u8 g_memcard_title_plus_hi = 0x81; /* full-width "+" */
static u8 g_memcard_title_plus_lo = 0x7B;
static u8 g_memcard_unused_byte = 0;
/* The screens. A "restart" flag is set when a screen finishes, so that its
 * next call starts over. */
static u8 g_memcard_boot_check_restart = 1;
static u8 g_memcard_boot_ask_restart = 1;
static u8 g_memcard_load_screen_restart = 1;
/* Set once left or right has been pressed at "Format the card?", which
 * starts on No until then. */
static u8 g_memcard_format_choice_touched = 0;
/* enum SaveScreenState */
static u8 g_memcard_save_screen_state = 0;
