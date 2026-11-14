/* The settings' file (rb_save.h): what it holds, and reading and writing it
 * on the memory card in slot 1, in the order the game itself goes about
 * it (memcard.c, memcard_save.c): a look at the card, the directory, then
 * the file with the game's own routines, one thing at a time.
 *
 * Nothing here asks the player anything, formats a card or waits for one:
 * a card that is missing, unformatted, full or broken means only that the
 * settings are not read or not kept. Without RB_OPTIONS_PAGE, and in the
 * quick-boot build, this file is empty. */
#include "common.h"
#include "bb2.h"

#if RB_OPTIONS_PAGE && !RB_QUICK_BOOT

/* The game's save is "BASLUS-00663BUSHIDO2": the same region and product
 * code, so that a card manager lists the two together, and a name that
 * neither begins the other's. */
const char g_rb_save_file_name[] = "BASLUS-00663REBUSHI";

#define RB_SAVE_TITLE "ReBushido Settings"

/* The header every memory-card file starts with (Sony's format). */
#define HEADER_ICON_FRAMES 2 /* 0x11: one frame, which does not move */
#define HEADER_ONE_ICON_FRAME 0x11
#define HEADER_BLOCKS 3
#define HEADER_TITLE 0x04 /* Shift-JIS, two bytes a character */
#define HEADER_TITLE_CHARS 32
#define HEADER_ICON_CLUT 0x60
#define HEADER_ICON_COLORS 16
#define HEADER_ICON 0x80
#define ICON_FRAME_SIZE 0x80

/* Shift-JIS's full-width forms, which is what a card's title is in. */
#define SJIS_SPACE 0x8140
#define SJIS_DIGIT_0 0x824F
#define SJIS_UPPER_A 0x8260
#define SJIS_LOWER_A 0x8281

/* The CRC-32's polynomial, bits reversed. */
#define CRC32_POLYNOMIAL 0xEDB88320
#define BYTE_BITS 8

/* The BIOS's number for the first slot. */
#define CARD_CHANNEL 0
/* Looks at the card that find none before it is taken to be absent: the
 * game's own number (memcard_save.c, NO_CARD_PROBES). */
#define CARD_MISSES 4
/* Frames a look waits for one answer of the BIOS before it gives up: the
 * game's own number (memcard.c, EVENT_TIMEOUT_POLLS). */
#define LOOK_POLLS 120

enum {
    LOOK_IDLE = 0,
    LOOK_WAIT_INFO = 1, /* asked whether a card is there */
    LOOK_WAIT_LOAD = 2, /* asked for its directory */
};

/* The longest a look at the card or a read or write of the file is
 * waited for, in vertical blanks. The card driver gives up by itself long
 * before; this is so that nothing can wait for ever. */
#define CARD_WAIT_FRAMES 600

u8 g_rb_card_status;
u8 g_rb_save_result;

/* Whether the card's directory has the settings' file. */
static u8 g_rb_save_found;
/* Looks at the card in a row that found none. */
static u8 g_rb_card_misses;
/* Where the look at the card has got to. */
static u8 g_rb_look_state;
/* How often its answer has been asked for. */
static u8 g_rb_look_polls;
/* What it will have found if the directory loads (a CardProbeResult). */
static s8 g_rb_look_result;
/* The BIOS has read the directory of the card that is in the slot. */
static u8 g_rb_card_loaded;

/* The icon's colours: none (the card manager's background shows), dark
 * red, white, light red. */
static const u16 g_rb_save_icon_clut[HEADER_ICON_COLORS] = {
    0x0000,
    0x0C52,
    0x7FFF,
    0x31DF,
};
/* The icon: "Re" over a line, on a red tile. Sixteen rows of sixteen
 * pixels, a row to a line; a pixel is four bits, the left one of a pair
 * in the low half of its byte. Drawn for ReBushido. */
/* clang-format off */
static const u8 g_rb_save_icon[ICON_FRAME_SIZE] = {
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x10, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x01,
    0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11,
    0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11,
    0x11, 0x22, 0x22, 0x11, 0x11, 0x11, 0x11, 0x11,
    0x11, 0x12, 0x11, 0x12, 0x11, 0x11, 0x11, 0x11,
    0x11, 0x12, 0x11, 0x12, 0x11, 0x22, 0x12, 0x11,
    0x11, 0x22, 0x22, 0x11, 0x21, 0x11, 0x21, 0x11,
    0x11, 0x12, 0x12, 0x11, 0x21, 0x22, 0x22, 0x11,
    0x11, 0x12, 0x21, 0x11, 0x21, 0x11, 0x11, 0x11,
    0x11, 0x12, 0x11, 0x12, 0x11, 0x22, 0x22, 0x11,
    0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11,
    0x11, 0x33, 0x33, 0x33, 0x33, 0x33, 0x33, 0x11,
    0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11,
    0x10, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x01,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
};
/* clang-format on */

/* ---- what the file holds ---- */

u32 rb_Checksum(const u8 *bytes, s32 size) {
    u32 crc = 0xFFFFFFFF;
    s32 i;
    s32 bit;

    for (i = 0; i < size; i++) {
        crc ^= bytes[i];
        for (bit = 0; bit < BYTE_BITS; bit++) {
            if (crc & 1) {
                crc = (crc >> 1) ^ CRC32_POLYNOMIAL;
            } else {
                crc >>= 1;
            }
        }
    }
    return ~crc;
}

/* Writes a title of letters, digits and spaces as the card's header wants
 * it: Shift-JIS's full-width characters, the high byte first. Anything
 * else becomes a space. */
static void rb_WriteTitle(u8 *out, const char *title) {
    s32 i;

    for (i = 0; i < HEADER_TITLE_CHARS && title[i] != 0; i++) {
        s32 ch = (u8)title[i];
        s32 wide = SJIS_SPACE;

        if (ch >= '0' && ch <= '9') {
            wide = SJIS_DIGIT_0 + (ch - '0');
        } else if (ch >= 'A' && ch <= 'Z') {
            wide = SJIS_UPPER_A + (ch - 'A');
        } else if (ch >= 'a' && ch <= 'z') {
            wide = SJIS_LOWER_A + (ch - 'a');
        }
        out[i * 2] = wide >> BYTE_BITS;
        out[i * 2 + 1] = wide;
    }
}

/* The whole file from g_rb_settings: the card's header with the title and
 * the icon, then the settings under their checksum (rb_save.h). */
void rb_PackSave(u8 *image) {
    u8 *data = image + RB_SAVE_HEADER_SIZE;
    u32 checksum;
    s32 i;

    for (i = 0; i < RB_SAVE_FILE_SIZE; i++) {
        image[i] = 0;
    }

    image[0] = 'S';
    image[1] = 'C';
    image[HEADER_ICON_FRAMES] = HEADER_ONE_ICON_FRAME;
    image[HEADER_BLOCKS] = RB_SAVE_BLOCKS;
    rb_WriteTitle(image + HEADER_TITLE, RB_SAVE_TITLE);
    for (i = 0; i < HEADER_ICON_COLORS; i++) {
        image[HEADER_ICON_CLUT + i * 2] = g_rb_save_icon_clut[i];
        image[HEADER_ICON_CLUT + i * 2 + 1] =
            g_rb_save_icon_clut[i] >> BYTE_BITS;
    }
    for (i = 0; i < ICON_FRAME_SIZE; i++) {
        image[HEADER_ICON + i] = g_rb_save_icon[i];
    }

    data[RB_SAVE_AT_MAGIC + 0] = 'R';
    data[RB_SAVE_AT_MAGIC + 1] = 'B';
    data[RB_SAVE_AT_MAGIC + 2] = 'S';
    data[RB_SAVE_AT_MAGIC + 3] = 'T';
    data[RB_SAVE_AT_VERSION] = RB_SAVE_VERSION;
    data[RB_SAVE_AT_COUNT] = RB_SAVE_SLOT_COUNT;
    for (i = 0; i < RB_OPTION_COUNT; i++) {
        data[RB_SAVE_AT_VALUES + g_rb_options[i].save_slot] =
            *g_rb_options[i].value;
    }
    checksum = rb_Checksum(data, RB_SAVE_AT_CHECKSUM);
    for (i = 0; i < 4; i++) {
        data[RB_SAVE_AT_CHECKSUM + i] = checksum >> (i * BYTE_BITS);
    }
}

/* g_rb_settings from a file's bytes, if its letters, version and checksum
 * are this version's; FALSE, and nothing changed, if not. */
s32 rb_UnpackSave(const u8 *image) {
    const u8 *data = image + RB_SAVE_HEADER_SIZE;
    u32 checksum;
    s32 i;

    if (data[RB_SAVE_AT_MAGIC + 0] != 'R' ||
        data[RB_SAVE_AT_MAGIC + 1] != 'B' ||
        data[RB_SAVE_AT_MAGIC + 2] != 'S' ||
        data[RB_SAVE_AT_MAGIC + 3] != 'T' ||
        data[RB_SAVE_AT_VERSION] != RB_SAVE_VERSION) {
        return FALSE;
    }
    checksum = 0;
    for (i = 0; i < 4; i++) {
        checksum |= (u32)data[RB_SAVE_AT_CHECKSUM + i] << (i * BYTE_BITS);
    }
    if (checksum != rb_Checksum(data, RB_SAVE_AT_CHECKSUM)) {
        return FALSE;
    }

    /* A setting the file is too old to have, or a value this version
     * does not know, is as the original. */
    for (i = 0; i < RB_OPTION_COUNT; i++) {
        const RbOption *option = &g_rb_options[i];
        s32 value = 0;

        if (option->save_slot < data[RB_SAVE_AT_COUNT]) {
            value = data[RB_SAVE_AT_VALUES + option->save_slot];
        }
        if (value >= option->choice_count) {
            value = 0;
        }
        *option->value = value;
    }
    return TRUE;
}

/* ---- the card ---- */

/* The look at the card is the game's own (memcard_ProbeCard): ask the
 * BIOS about the card and, for one it has not seen, clear its "new" mark
 * and read its directory. It is written again here, with the same calls
 * in the same order, because the game's keeps what it found in variables
 * of its own, and the game must find those, when it first looks at the
 * card in the main menu, as the retail game does. */

/* Takes in what a finished look at the card found. */
static void rb_NoteLook(s32 result) {
    g_rb_look_state = LOOK_IDLE;
    switch (result) {
    case CARD_PROBE_READY:
    case CARD_PROBE_READY_NEW_CARD:
        g_rb_card_misses = 0;
        if (g_rb_card_status != RB_CARD_READY ||
            result == CARD_PROBE_READY_NEW_CARD) {
            memcard_ReadDirectory(0, 0);
            g_rb_save_found = memcard_FileExists((char *)g_rb_save_file_name);
            if (g_rb_save_found || memcard_CountFreeBlocks() > 0) {
                g_rb_card_status = RB_CARD_READY;
            } else {
                g_rb_card_status = RB_CARD_FULL;
            }
        }
        break;
    case CARD_PROBE_UNFORMATTED:
        g_rb_card_misses = 0;
        g_rb_card_loaded = FALSE;
        g_rb_save_found = FALSE;
        g_rb_card_status = RB_CARD_UNFORMATTED;
        break;
    default:
        /* no card, or an error: the same to us */
        g_rb_card_loaded = FALSE;
        g_rb_save_found = FALSE;
        if (g_rb_card_misses < CARD_MISSES) {
            g_rb_card_misses++;
        }
        if (g_rb_card_misses >= CARD_MISSES) {
            g_rb_card_status = RB_CARD_NONE;
        }
        break;
    }
}

static void rb_ClearSwEvents(void) {
    TestEvent(g_memcard_sw_event_ioe);
    TestEvent(g_memcard_sw_event_err);
    TestEvent(g_memcard_sw_event_timeout);
    TestEvent(g_memcard_sw_event_new);
}

/* What the BIOS has answered to the look's last question, or
 * CARD_EVENT_ERROR once it has been asked about for too long. */
static s32 rb_PollLookEvent(void) {
    s32 event = memcard_PollSwEvent();

    if (event == CARD_EVENT_NONE && ++g_rb_look_polls >= LOOK_POLLS) {
        event = CARD_EVENT_ERROR;
    }
    return event;
}

/* Clears the card's "new" mark, waiting for the card to have done it. */
static void rb_ClearNewCard(void) {
    s32 frames;

    TestEvent(g_memcard_hw_event_ioe);
    TestEvent(g_memcard_hw_event_err);
    TestEvent(g_memcard_hw_event_timeout);
    TestEvent(g_memcard_hw_event_new);
    _card_clear(CARD_CHANNEL);
    for (frames = 0; frames < LOOK_POLLS; frames++) {
        if (TestEvent(g_memcard_hw_event_ioe) == 1 ||
            TestEvent(g_memcard_hw_event_err) == 1 ||
            TestEvent(g_memcard_hw_event_timeout) == 1 ||
            TestEvent(g_memcard_hw_event_new) == 1) {
            return;
        }
        VSync(0);
    }
}

static void rb_StartDirectoryLoad(void) {
    rb_ClearSwEvents();
    _card_load(CARD_CHANNEL);
    g_rb_look_state = LOOK_WAIT_LOAD;
    g_rb_look_polls = 0;
}

/* One step of the look at the card, to be called once a frame: starts a
 * look, or asks whether the BIOS has answered and goes on from what it
 * said. A look that ends is taken in by rb_NoteLook. */
void rb_PollCard(void) {
    s32 event;

    switch (g_rb_look_state) {
    case LOOK_IDLE:
        rb_ClearSwEvents();
        _card_info(CARD_CHANNEL);
        g_rb_look_state = LOOK_WAIT_INFO;
        g_rb_look_polls = 0;
        break;
    case LOOK_WAIT_INFO:
        event = rb_PollLookEvent();
        switch (event) {
        case CARD_EVENT_NONE:
            break;
        case CARD_EVENT_DONE:
            g_rb_look_result = CARD_PROBE_READY;
            if (g_rb_card_loaded) {
                rb_NoteLook(CARD_PROBE_READY);
            } else {
                rb_StartDirectoryLoad();
            }
            break;
        case CARD_EVENT_NEW_CARD:
            g_rb_look_result = CARD_PROBE_READY_NEW_CARD;
            g_rb_card_loaded = FALSE;
            rb_ClearNewCard();
            rb_StartDirectoryLoad();
            break;
        case CARD_EVENT_TIMEOUT:
            rb_NoteLook(CARD_PROBE_NO_CARD);
            break;
        default:
            rb_NoteLook(CARD_PROBE_ERROR);
            break;
        }
        break;
    case LOOK_WAIT_LOAD:
        event = rb_PollLookEvent();
        switch (event) {
        case CARD_EVENT_NONE:
            break;
        case CARD_EVENT_DONE:
            g_rb_card_loaded = TRUE;
            rb_NoteLook(g_rb_look_result);
            break;
        case CARD_EVENT_TIMEOUT:
            rb_NoteLook(CARD_PROBE_NO_CARD);
            break;
        case CARD_EVENT_NEW_CARD:
            /* the directory would not load: the card is not formatted */
            rb_NoteLook(CARD_PROBE_UNFORMATTED);
            break;
        default:
            rb_NoteLook(CARD_PROBE_ERROR);
            break;
        }
        break;
    }
}

/* Goes on with the look at the card until it has an answer. */
static void rb_FinishLook(void) {
    s32 frames;

    for (frames = 0; frames < CARD_WAIT_FRAMES; frames++) {
        rb_PollCard();
        if (g_rb_look_state == LOOK_IDLE) {
            return;
        }
        VSync(0);
    }
}

void rb_QuietCard(void) {
    if (g_rb_look_state != LOOK_IDLE) {
        rb_FinishLook();
    }
}

/* Looks at the card until it is known what is there. */
static void rb_SettleCard(void) {
    s32 looks;

    g_rb_card_status = RB_CARD_UNKNOWN;
    g_rb_card_misses = 0;
    for (looks = 0; looks < CARD_MISSES; looks++) {
        rb_FinishLook();
        if (g_rb_card_status != RB_CARD_UNKNOWN) {
            return;
        }
    }
    g_rb_card_status = RB_CARD_NONE;
}

/* Waits for the read or write that was started to end. Returns its
 * CardEvent. */
static s32 rb_WaitFileIo(void) {
    s32 frames;
    s32 event;

    for (frames = 0; frames < CARD_WAIT_FRAMES; frames++) {
        event = memcard_PollSwEvent();
        if (event != CARD_EVENT_NONE) {
            return event;
        }
        VSync(0);
    }
    return CARD_EVENT_TIMEOUT;
}

/* Looks at the card and, if the settings' file is on it, reads the file
 * and takes the settings from it. */
void rb_LoadSave(void) {
    u8 *image = (u8 *)memcard_GetSharedBuffer();
    s32 event = CARD_EVENT_ERROR;

    g_rb_save_result = RB_SAVE_NOTHING;
    rb_SettleCard();
    if (g_rb_card_status != RB_CARD_READY || !g_rb_save_found) {
        return;
    }

    /* memcard_ReadFile leaves the file open in g_memcard_fd if it got as
     * far as opening it */
    g_memcard_fd = -1;
    if (memcard_ReadFile(
            0, 0, (char *)g_rb_save_file_name, image, RB_SAVE_FILE_SIZE) == 0) {
        event = rb_WaitFileIo();
    }
    if (g_memcard_fd != -1) {
        close(g_memcard_fd);
    }
    if (event == CARD_EVENT_DONE && rb_UnpackSave(image)) {
        g_rb_save_result = RB_SAVE_LOADED;
    } else {
        g_rb_save_result = RB_SAVE_BAD;
    }
}

/* Writes the settings' file, making it first if the card does not have
 * it. Returns TRUE if it was written. */
s32 rb_WriteSave(void) {
    u8 *image = (u8 *)memcard_GetSharedBuffer();
    s32 event = CARD_EVENT_ERROR;

    g_rb_save_result = RB_SAVE_FAILED;
    if (g_rb_card_status != RB_CARD_READY) {
        return FALSE;
    }

    rb_PackSave(image);
    g_memcard_fd = -1;
    if (memcard_WriteFile(
            0, 0, (char *)g_rb_save_file_name, image, RB_SAVE_BLOCKS,
            RB_SAVE_FILE_SIZE, !g_rb_save_found) == 0) {
        event = rb_WaitFileIo();
    }
    if (g_memcard_fd != -1) {
        close(g_memcard_fd);
    }
    if (event != CARD_EVENT_DONE) {
        return FALSE;
    }
    g_rb_save_found = TRUE;
    g_rb_save_result = RB_SAVE_WRITTEN;
    return TRUE;
}

#endif /* RB_OPTIONS_PAGE && !RB_QUICK_BOOT */
