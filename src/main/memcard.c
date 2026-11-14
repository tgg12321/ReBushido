/* Memory card I/O on top of the BIOS file system. The event handling follows
 * the memory-card sample in the Sony SDK: card operations are asynchronous and
 * report through two sets of four events, "software" (BIOS level) and
 * "hardware" (card level), which are polled here. */
#include "common.h"
#include "bb2.h"

/* Defined further down. */
static s32 g_memcard_dir_loaded;
static s32 g_memcard_probe_state;

#define EVENT_TIMEOUT_POLLS 120
/* A card has fifteen blocks of 8192 bytes, and so at most fifteen files. */
#define CARD_BLOCK_SIZE 0x2000
#define CARD_BLOCK_SHIFT 13
#define CARD_BLOCK_COUNT 15
/* A card file's name is twenty characters; a path to it ("bu00:" and the
 * name) and a device's name ("bu00:") fit in these. */
#define CARD_NAME_LENGTH 20
#define CARD_PATH_SIZE 32
#define CARD_DEVICE_SIZE 8
/* The BIOS's number for a slot: the port in the high nibble, the card of a
 * multitap in the low. */
#define CARD_CHANNEL(port, card) (((port) << 4) + (card))

s32 g_memcard_sw_event_ioe;
s32 g_memcard_sw_event_err;
s32 g_memcard_sw_event_timeout;
s32 g_memcard_sw_event_new;
s32 g_memcard_hw_event_ioe;
s32 g_memcard_hw_event_err;
s32 g_memcard_hw_event_timeout;
s32 g_memcard_hw_event_new;

/* The directory of the card, as of the last memcard_ReadDirectory. */
s32 g_memcard_file_count;
struct DIRENTRY g_memcard_file_list[CARD_BLOCK_COUNT];

/* Starts the BIOS's card driver beside the pad driver, and opens and enables
 * the eight events its operations report through. */
void memcard_Init(void) {
    InitCARD(1); /* 1: the pads are in use too */
    StartCARD();
    _bu_init();
    ChangeClearPAD(0);
    EnterCriticalSection();
    g_memcard_sw_event_ioe = OpenEvent(SwCARD, EvSpIOE, EvMdNOINTR, 0);
    g_memcard_sw_event_err = OpenEvent(SwCARD, EvSpERROR, EvMdNOINTR, 0);
    g_memcard_sw_event_timeout = OpenEvent(SwCARD, EvSpTIMOUT, EvMdNOINTR, 0);
    g_memcard_sw_event_new = OpenEvent(SwCARD, EvSpNEW, EvMdNOINTR, 0);
    g_memcard_hw_event_ioe = OpenEvent(HwCARD, EvSpIOE, EvMdNOINTR, 0);
    g_memcard_hw_event_err = OpenEvent(HwCARD, EvSpERROR, EvMdNOINTR, 0);
    g_memcard_hw_event_timeout = OpenEvent(HwCARD, EvSpTIMOUT, EvMdNOINTR, 0);
    g_memcard_hw_event_new = OpenEvent(HwCARD, EvSpNEW, EvMdNOINTR, 0);
    ExitCriticalSection();
    EnableEvent(g_memcard_sw_event_ioe);
    EnableEvent(g_memcard_sw_event_err);
    EnableEvent(g_memcard_sw_event_timeout);
    EnableEvent(g_memcard_sw_event_new);
    EnableEvent(g_memcard_hw_event_ioe);
    EnableEvent(g_memcard_hw_event_err);
    EnableEvent(g_memcard_hw_event_timeout);
    EnableEvent(g_memcard_hw_event_new);
}

/* Closes the events and stops the card driver (before the movie player is
 * run). */
void memcard_Quit(void) {
    EnterCriticalSection();
    CloseEvent(g_memcard_sw_event_ioe);
    CloseEvent(g_memcard_sw_event_err);
    CloseEvent(g_memcard_sw_event_timeout);
    CloseEvent(g_memcard_sw_event_new);
    CloseEvent(g_memcard_hw_event_ioe);
    CloseEvent(g_memcard_hw_event_err);
    CloseEvent(g_memcard_hw_event_timeout);
    CloseEvent(g_memcard_hw_event_new);
    ExitCriticalSection();
    StopCARD();
}

/* Like memcard_PollSwEvent, but gives up with CARD_EVENT_ERROR once it has
 * been polled EVENT_TIMEOUT_POLLS times since g_memcard_timeout_counter was
 * cleared. */
static s32 memcard_PollSwEventTimed(void) {
    s32 event;
    s32 polls;

    event = (TestEvent(g_memcard_sw_event_ioe) == 1);
    if (TestEvent(g_memcard_sw_event_err) == 1) {
        event = CARD_EVENT_ERROR;
    }
    if (TestEvent(g_memcard_sw_event_timeout) == 1) {
        event = CARD_EVENT_TIMEOUT;
    }
    if (TestEvent(g_memcard_sw_event_new) == 1) {
        event = CARD_EVENT_NEW_CARD;
    }
    polls = g_memcard_timeout_counter;
    g_memcard_timeout_counter = polls + 1;
    if (polls >= EVENT_TIMEOUT_POLLS) {
        event = CARD_EVENT_ERROR;
    }
    return event;
}

/* Which of the four software events has come since they were last looked
 * at: a CardEvent, CARD_EVENT_NONE if none has. */
s32 memcard_PollSwEvent(void) {
    if (TestEvent(g_memcard_sw_event_ioe) == 1) {
        return CARD_EVENT_DONE;
    }
    if (TestEvent(g_memcard_sw_event_err) == 1) {
        return CARD_EVENT_ERROR;
    }
    if (TestEvent(g_memcard_sw_event_timeout) == 1) {
        return CARD_EVENT_TIMEOUT;
    }
    return (TestEvent(g_memcard_sw_event_new) == 1) * CARD_EVENT_NEW_CARD;
}

static void memcard_ClearSwEvents(void) {
    TestEvent(g_memcard_sw_event_ioe);
    TestEvent(g_memcard_sw_event_err);
    TestEvent(g_memcard_sw_event_timeout);
    TestEvent(g_memcard_sw_event_new);
}

/* Blocks until the card answers. */
static s32 memcard_WaitHwEvent(void) {
    while (1) {
        if (TestEvent(g_memcard_hw_event_ioe) == 1) {
            return CARD_EVENT_DONE;
        }
        if (TestEvent(g_memcard_hw_event_err) == 1) {
            return CARD_EVENT_ERROR;
        }
        if (TestEvent(g_memcard_hw_event_timeout) == 1) {
            return CARD_EVENT_TIMEOUT;
        }
        if (TestEvent(g_memcard_hw_event_new) == 1) {
            return CARD_EVENT_NEW_CARD;
        }
    }
}

static void memcard_ClearHwEvents(void) {
    TestEvent(g_memcard_hw_event_ioe);
    TestEvent(g_memcard_hw_event_err);
    TestEvent(g_memcard_hw_event_timeout);
    TestEvent(g_memcard_hw_event_new);
}

/* Lists the files on the card into g_memcard_file_list; returns how many. */
s32 memcard_ReadDirectory(s32 port, s32 card) {
    struct DIRENTRY *entry;
    s32 count;
    char pattern[CARD_PATH_SIZE];

    entry = g_memcard_file_list;
    sprintf(pattern, "bu%1d%1d:*", port, card);
    count = 0;
    if (firstfile(pattern, entry) != 0) {
        do {
            count++;
            entry++;
        } while (nextfile(entry) != 0);
    }
    g_memcard_file_count = count;
    return count;
}

/* The blocks of the card that no file of the directory last read has. */
s32 memcard_CountFreeBlocks(void) {
    struct DIRENTRY *entry;
    s32 i;
    s32 count;
    s32 used;
    s32 rounded;
    s32 shift;

    shift = CARD_BLOCK_SHIFT;
    i = 0;
    used = 0;
    count = g_memcard_file_count;
    if (i < count) {
        entry = g_memcard_file_list;
        do {
            used += entry++->size;
            i += 1;
        } while (i < count);
    }
    /* FAKE: used / CARD_BLOCK_SIZE, written out with the shift in a local */
    rounded = used;
    if (used < 0) {
        rounded = used + (CARD_BLOCK_SIZE - 1);
    }
    used = rounded >> shift;
    return CARD_BLOCK_COUNT - used;
}

/* Whether the directory last read has a file whose name begins with `name`.
 * The loops keep the shape the compiled code needs. */
s32 memcard_FileExists(char *name) {
    struct DIRENTRY *entry;
    s8 *have;
    s8 *want;
    s8 *end;
    s32 want_byte;
    s32 have_byte;
    s32 mismatch;
    s32 count;
    s32 i;

    i = 0;
    if (i < g_memcard_file_count) {
        count = g_memcard_file_count;
        entry = g_memcard_file_list;
        while (i < count) {
            mismatch = 0;
            have = entry->name;
            want = name;
            /* one byte past the name: the original's behaviour */
            end = entry->name + CARD_NAME_LENGTH + 1;
            while (1) {
                want_byte = (u8)*want;
                if (want_byte == 0) {
                    break;
                }
                have_byte = (u8)*have;
                if (want_byte != have_byte) {
                    mismatch = 1;
                    break;
                }
                have += 1;
                want += 1;
                if ((s32)have >= (s32)end) {
                    break;
                }
            }
            i += 1;
            if (mismatch == 0) {
                return 1;
            }
            entry++;
        }
    }
    return 0;
}

/* Starts an asynchronous read or write and waits for the card to accept it;
 * completion arrives later as a software event (memcard_PollSwEvent). Returns
 * 0 on success and -1 on failure, leaving the file open in g_memcard_fd. */
s32 memcard_ReadFile(s32 port, s32 card, char *name, void *dst, s32 size) {
    char path[CARD_PATH_SIZE];
    s32 fd;

    sprintf(path, "bu%1d%1d:%s", port, card, name);
    fd = open(path, O_NOWAIT | O_RDONLY);
    if (fd == -1) {
        return -1;
    }
    g_memcard_fd = fd;
    memcard_ClearSwEvents();
    memcard_ClearHwEvents();
    read(fd, dst, size);
    return -(memcard_WaitHwEvent() != CARD_EVENT_DONE);
}

/* The same for a write of `size` bytes. With `create` the file is first
 * made, `blocks` blocks long. */
s32 memcard_WriteFile(s32 port, s32 card, char *name, void *src, s32 blocks,
                      s32 size, s32 create) {
    char path[CARD_PATH_SIZE];
    s32 fd;

    sprintf(path, "bu%1d%1d:%s", port, card, name);
    if (create) {
        /* the number of blocks rides above the flags */
        fd = open(path, (blocks << 16) | O_CREAT);
        if (fd == -1) {
            return -1;
        }
        close(fd);
    }
    fd = open(path, O_NOWAIT | O_WRONLY);
    if (fd == -1) {
        return -1;
    }
    g_memcard_fd = fd;
    memcard_ClearSwEvents();
    memcard_ClearHwEvents();
    write(fd, src, size);
    return -(memcard_WaitHwEvent() != CARD_EVENT_DONE);
}

/* ---- the card probe: is there a usable card in the slot? ---- */

enum ProbeState {
    PROBE_START_INFO = 0,
    PROBE_WAIT_INFO = 1,
    PROBE_START_LOAD = 2,
    PROBE_WAIT_LOAD = 3,
    PROBE_DONE = 4,
};

s32 g_memcard_probe_result;
s32 g_memcard_probe_pending_result;

/* Call every frame. Asks the BIOS about the card (_card_info) and, for a card
 * it has not seen before, reads its directory (_card_load). Returns
 * CARD_PROBE_BUSY until an answer is in, then a CardProbeResult for one call,
 * and starts over. */
s32 memcard_ProbeCard(s32 port, s32 card) {
    s32 channel = CARD_CHANNEL(port, card);
    s32 event;

    switch (g_memcard_probe_state) {
    case PROBE_START_INFO:
        _card_info(channel);
        g_memcard_probe_state = PROBE_WAIT_INFO;
        g_memcard_timeout_counter = 0;
        g_memcard_probe_result = CARD_PROBE_BUSY;
        break;
    case PROBE_WAIT_INFO:
        event = memcard_PollSwEventTimed();
        if (event == CARD_EVENT_NONE) {
            break;
        }
        switch (event) {
        case CARD_EVENT_DONE:
            g_memcard_probe_pending_result = CARD_PROBE_READY;
            if (g_memcard_dir_loaded) {
                g_memcard_probe_state = PROBE_DONE;
            } else {
                g_memcard_probe_state = PROBE_START_LOAD;
            }
            break;
        case CARD_EVENT_TIMEOUT:
            g_memcard_probe_pending_result = CARD_PROBE_NO_CARD;
            g_memcard_probe_state = PROBE_DONE;
            g_memcard_dir_loaded = 0;
            break;
        case CARD_EVENT_NEW_CARD:
            g_memcard_probe_pending_result = CARD_PROBE_READY_NEW_CARD;
            memcard_ClearHwEvents();
            _card_clear(channel);
            memcard_WaitHwEvent();
            g_memcard_probe_state = PROBE_START_LOAD;
            g_memcard_dir_loaded = 0;
            break;
        case CARD_EVENT_ERROR:
        default:
            g_memcard_probe_pending_result = CARD_PROBE_ERROR;
            g_memcard_probe_state = PROBE_DONE;
            g_memcard_dir_loaded = 0;
            break;
        }
        break;
    case PROBE_START_LOAD:
        memcard_ClearSwEvents();
        _card_load(channel);
        g_memcard_probe_state = PROBE_WAIT_LOAD;
        g_memcard_timeout_counter = 0;
        break;
    case PROBE_WAIT_LOAD:
        event = memcard_PollSwEventTimed();
        if (event == CARD_EVENT_NONE) {
            break;
        }
        g_memcard_probe_state = PROBE_DONE;
        switch (event) {
        case CARD_EVENT_DONE:
            g_memcard_dir_loaded = 1;
            break;
        case CARD_EVENT_TIMEOUT:
            g_memcard_probe_pending_result = CARD_PROBE_NO_CARD;
            g_memcard_dir_loaded = 0;
            break;
        case CARD_EVENT_NEW_CARD:
            /* the directory would not load: the card is not formatted */
            g_memcard_probe_pending_result = CARD_PROBE_UNFORMATTED;
            g_memcard_dir_loaded = 0;
            break;
        case CARD_EVENT_ERROR:
        default:
            g_memcard_probe_pending_result = CARD_PROBE_ERROR;
            g_memcard_dir_loaded = 0;
            break;
        }
        break;
    case PROBE_DONE:
        g_memcard_probe_result = g_memcard_probe_pending_result;
        g_memcard_probe_state = PROBE_START_INFO;
        break;
    }
    return g_memcard_probe_result;
}

s32 memcard_Format(s32 port, s32 card) {
    char device[CARD_DEVICE_SIZE];
    sprintf(device, "bu%1d%1d:", port, card);
    return format(device);
}

/* Set once the directory of the card in the slot has been read; cleared when
 * the card goes away or is swapped. */
static s32 g_memcard_dir_loaded = 0;
/* Where memcard_ProbeCard has got to (enum ProbeState). */
static s32 g_memcard_probe_state = 0;
s32 g_memcard_fd;
s32 g_memcard_timeout_counter;
