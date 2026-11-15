/* The port's memory card: the card image's format, the BIOS's calls over
 * it, and the events each of them reports. */
/* Everything is made up here: the images are built in memory by the
 * test's own stand-ins for the host's files, and no card image, save or
 * game data is read (docs/PORT.md, "The memory card"). What the events of
 * each call are held to is the table at "the events, call by call", which
 * was derived from Sony's libcard (src/main/psxsdk/libcard/) and from
 * what the game and ReBushido wait for, in the order they wait for it
 * (src/main/memcard.c, memcard_save.c, src/rebushido/save.c): the
 * sources are named beside each row. */
#include "common.h"
#include "port_card.h"
#include "port_host.h"
#include <psxsdk/kernel.h>
#include <psxsdk/libapi.h>
#include "src/main/psxsdk/libcard/card.c"
#include "src/port/event.c"
#include "src/port/stub.c"
#include "src/port/text.c"
#include "src/port/card.c"
#include "runtime/runtime.h"

/* ---- the host's files, in memory ---- */

/* The paths the test's card images have; nothing else can be opened or
 * made. */
#define TEST_PATHS 3
#define TEST_PATH_FIRST "card1.mcd"
#define TEST_PATH_SECOND "card2.mcd"
#define TEST_PATH_OTHER "other.mcd"

typedef struct {
    const char *path;
    s32 made;  /* the file is there */
    u32 size;  /* how much of it has been written */
    s32 open;  /* it is open, under the number of its place */
    u8 bytes[PORT_CARD_IMAGE_SIZE];
} TestHostFile;

static TestHostFile g_test_host_files[TEST_PATHS];
/* The system refuses every write, or every read. */
static s32 g_test_write_fails;
static s32 g_test_read_fails;
static u32 g_test_flushes;
/* What port_Log said last. */
#define TEST_SAID_SIZE 512
static char g_test_said[TEST_SAID_SIZE];
static u32 g_test_said_length;
static u32 g_test_said_lines;

static TestHostFile *test_FileOf(s32 file) {
    if (file < 0 || file >= TEST_PATHS || !g_test_host_files[file].open) {
        return NULL;
    }
    return &g_test_host_files[file];
}

static s32 test_PlaceOf(const char *path) {
    s32 i;

    for (i = 0; i < TEST_PATHS; i++) {
        if (test_StringsEqual(g_test_host_files[i].path, path)) {
            return i;
        }
    }
    return -1;
}

s32 port_HostOpen(const char *path) {
    s32 place = test_PlaceOf(path);

    if (place < 0 || !g_test_host_files[place].made) {
        return -1;
    }
    g_test_host_files[place].open = 1;
    return place;
}

s32 port_HostOpenReadWrite(const char *path) { return port_HostOpen(path); }

s32 port_HostCreate(const char *path) {
    s32 place = test_PlaceOf(path);

    if (place < 0) {
        return -1;
    }
    g_test_host_files[place].made = 1;
    g_test_host_files[place].size = 0;
    g_test_host_files[place].open = 1;
    return place;
}

void port_HostClose(s32 file) {
    TestHostFile *found = test_FileOf(file);

    if (found != NULL) {
        found->open = 0;
    }
}

u32 port_HostFileSize(s32 file) {
    TestHostFile *found = test_FileOf(file);

    return found == NULL ? 0 : found->size;
}

u32 port_HostReadAt(s32 file, u32 offset, void *dest, u32 count) {
    TestHostFile *found = test_FileOf(file);
    u32 i;

    if (found == NULL || g_test_read_fails || offset + count > found->size) {
        return 0;
    }
    for (i = 0; i < count; i++) {
        ((u8 *)dest)[i] = found->bytes[offset + i];
    }
    return count;
}

s32 port_HostAppend(s32 file, const void *bytes, u32 count) {
    TestHostFile *found = test_FileOf(file);
    u32 i;

    if (found == NULL || g_test_write_fails ||
        found->size + count > PORT_CARD_IMAGE_SIZE) {
        return 0;
    }
    for (i = 0; i < count; i++) {
        found->bytes[found->size + i] = ((const u8 *)bytes)[i];
    }
    found->size += count;
    return 1;
}

s32 port_HostWriteAt(s32 file, u32 offset, const void *bytes, u32 count) {
    TestHostFile *found = test_FileOf(file);
    u32 i;

    if (found == NULL || g_test_write_fails ||
        offset + count > PORT_CARD_IMAGE_SIZE) {
        return 0;
    }
    for (i = 0; i < count; i++) {
        found->bytes[offset + i] = ((const u8 *)bytes)[i];
    }
    if (offset + count > found->size) {
        found->size = offset + count;
    }
    return 1;
}

void port_HostFlush(s32 file) {
    (void)file;
    g_test_flushes++;
}

/* What the port says on its error output, kept for the test to read. */
void port_HostWrite(s32 stream, const char *bytes, u32 count) {
    u32 i;

    (void)stream;
    for (i = 0; i < count; i++) {
        if (bytes[i] == '\n') {
            g_test_said_lines++;
        } else if (g_test_said_length + 1 < TEST_SAID_SIZE) {
            g_test_said[g_test_said_length++] = bytes[i];
            g_test_said[g_test_said_length] = 0;
        }
    }
}

static void test_ForgetWhatWasSaid(void) {
    g_test_said_length = 0;
    g_test_said_lines = 0;
    g_test_said[0] = 0;
}

void port_Quit(s32 code) {
    test_Count(0, __FILE__, __LINE__, "the port stopped itself (%d)", code);
    for (;;) {
    }
}

/* ---- the clock, which the card's events ride on ---- */

static u32 g_test_blank;

u32 port_GetBlankCount(void) { return g_test_blank; }

/* `blanks` vertical blanks, with the card driver's turn at each, as the
 * port's clock gives it (clock.c). */
static void test_Blanks(u32 blanks) {
    u32 i;

    for (i = 0; i < blanks; i++) {
        g_test_blank++;
        port_RunCardBlank(g_test_blank);
    }
}

/* ---- the events the game opens ---- */

/* The eight the game opens (memcard.c, memcard_Init), in the order the
 * test asks them. */
enum {
    TEST_SW_IOE = 0,
    TEST_SW_ERROR = 1,
    TEST_SW_TIMEOUT = 2,
    TEST_SW_NEW = 3,
    TEST_HW_IOE = 4,
    TEST_HW_ERROR = 5,
    TEST_HW_TIMEOUT = 6,
    TEST_HW_NEW = 7,
    TEST_EVENTS = 8,
};

static s32 g_test_events[TEST_EVENTS];

static void test_OpenEvents(void) {
    g_test_events[TEST_SW_IOE] = OpenEvent(SwCARD, EvSpIOE, EvMdNOINTR, 0);
    g_test_events[TEST_SW_ERROR] = OpenEvent(SwCARD, EvSpERROR, EvMdNOINTR, 0);
    g_test_events[TEST_SW_TIMEOUT] =
        OpenEvent(SwCARD, EvSpTIMOUT, EvMdNOINTR, 0);
    g_test_events[TEST_SW_NEW] = OpenEvent(SwCARD, EvSpNEW, EvMdNOINTR, 0);
    g_test_events[TEST_HW_IOE] = OpenEvent(HwCARD, EvSpIOE, EvMdNOINTR, 0);
    g_test_events[TEST_HW_ERROR] = OpenEvent(HwCARD, EvSpERROR, EvMdNOINTR, 0);
    g_test_events[TEST_HW_TIMEOUT] =
        OpenEvent(HwCARD, EvSpTIMOUT, EvMdNOINTR, 0);
    g_test_events[TEST_HW_NEW] = OpenEvent(HwCARD, EvSpNEW, EvMdNOINTR, 0);
    {
        s32 i;

        for (i = 0; i < TEST_EVENTS; i++) {
            EnableEvent(g_test_events[i]);
        }
    }
}

/* Which of the eight have come since they were last asked: a bit each,
 * cleared as the game's own polling clears them. */
static s32 test_Events(void) {
    s32 found = 0;
    s32 i;

    for (i = 0; i < TEST_EVENTS; i++) {
        found |= TestEvent(g_test_events[i]) << i;
    }
    return found;
}

static void test_ClearEvents(void) { (void)test_Events(); }

#define TEST_NO_EVENT 0
#define TEST_EVENT(which) (1 << (which))

/* ---- the card images the test works on ---- */

static void test_SetUpHost(void) {
    g_test_host_files[0].path = TEST_PATH_FIRST;
    g_test_host_files[1].path = TEST_PATH_SECOND;
    g_test_host_files[2].path = TEST_PATH_OTHER;
}

/* Everything the card code knows forgotten, and the images gone: the
 * state of a program that has just started. */
static void test_StartOver(void) {
    s32 i;

    for (i = 0; i < TEST_PATHS; i++) {
        g_test_host_files[i].made = 0;
        g_test_host_files[i].size = 0;
        g_test_host_files[i].open = 0;
    }
    for (i = 0; i < PORT_CARD_SLOTS; i++) {
        g_port_card_slots[i].path = NULL;
        g_port_card_slots[i].file = -1;
        g_port_card_slots[i].tried = 0;
        g_port_card_slots[i].present = 0;
        g_port_card_slots[i].formatted = 0;
        g_port_card_slots[i].is_new = 0;
        g_port_card_slots[i].reporting = 0;
    }
    g_port_cards_off = 0;
    g_test_write_fails = 0;
    g_test_read_fails = 0;
    g_test_flushes = 0;
    test_ForgetWhatWasSaid();
    test_ClearEvents();
}

/* A card in slot 1 whose image is not there yet: the port makes it,
 * formatted and empty, the first time the game looks. */
static void test_OneNewCard(void) {
    test_StartOver();
    port_SetCardPath(0, TEST_PATH_FIRST);
    InitCARD(1);
    StartCARD();
    _bu_init();
}

static u8 *test_Image(s32 place) { return g_test_host_files[place].bytes; }

static u8 *test_Frame(s32 place, s32 frame) {
    return test_Image(place) + frame * PORT_CARD_FRAME_SIZE;
}

/* The card's directory as the game reads it, for the test's own counts. */
static s32 test_CountFiles(s32 port, s32 card) {
    struct DIRENTRY entry;
    char pattern[PORT_CARD_NAME_SIZE + 8];
    s32 count = 0;

    pattern[0] = 'b';
    pattern[1] = 'u';
    pattern[2] = (char)('0' + port);
    pattern[3] = (char)('0' + card);
    pattern[4] = ':';
    pattern[5] = '*';
    pattern[6] = 0;
    if (firstfile(pattern, &entry) != NULL) {
        do {
            count++;
        } while (nextfile(&entry) != NULL);
    }
    return count;
}

/* A card file's path, "buPC:NAME", into `path`. */
static void test_Path(char *path, s32 port, s32 card, const char *name) {
    s32 i;

    path[0] = 'b';
    path[1] = 'u';
    path[2] = (char)('0' + port);
    path[3] = (char)('0' + card);
    path[4] = ':';
    for (i = 0; name[i] != 0 && i < PORT_CARD_NAME_SIZE; i++) {
        path[5 + i] = name[i];
    }
    path[5 + i] = 0;
}

/* ---- the format of a card as the port makes one ---- */

static void test_Format(void) {
    s32 frame;
    s32 i;

    test_OneNewCard();
    /* nothing has been made yet: the image is made when the card is first
     * looked at */
    CHECK_EQ(g_test_host_files[0].made, 0);
    CHECK_EQ(_card_info(0), 1);
    CHECK_EQ(g_test_host_files[0].made, 1);
    CHECK_EQ(g_test_host_files[0].size, PORT_CARD_IMAGE_SIZE);

    /* the identifier, at the start and in the last frame of the header */
    CHECK_EQ(test_Frame(0, PORT_CARD_ID_FRAME)[0], PORT_CARD_ID_HIGH);
    CHECK_EQ(test_Frame(0, PORT_CARD_ID_FRAME)[1], PORT_CARD_ID_LOW);
    CHECK_EQ(test_Frame(0, PORT_CARD_ID_COPY_FRAME)[0], PORT_CARD_ID_HIGH);
    CHECK_EQ(test_Frame(0, PORT_CARD_ID_COPY_FRAME)[1], PORT_CARD_ID_LOW);

    /* a directory frame for each of the fifteen blocks, free, with no
     * next block, no size and no name */
    for (i = 1; i <= PORT_CARD_BLOCKS; i++) {
        u8 *dir = test_Frame(0, PORT_CARD_DIR_FRAME + i - 1);

        CHECK_EQ(dir[PORT_CARD_DIR_STATE], PORT_CARD_FREE);
        CHECK_EQ(port_CardGetHalf(dir + PORT_CARD_DIR_NEXT), PORT_CARD_NO_NEXT);
        CHECK_EQ(port_CardGetWord(dir + PORT_CARD_DIR_SIZE), 0);
        CHECK_EQ(dir[PORT_CARD_DIR_NAME], 0);
    }
    /* twenty entries that name no broken frame */
    for (i = 0; i < PORT_CARD_BROKEN_ENTRIES; i++) {
        u8 *broken = test_Frame(0, PORT_CARD_BROKEN_FRAME + i);

        CHECK_EQ(port_CardGetWord(broken), PORT_CARD_NOT_BROKEN);
    }
    /* every frame of the header block carries its own exclusive or */
    for (frame = 0; frame < PORT_CARD_FRAMES_PER_BLOCK; frame++) {
        u8 *at = test_Frame(0, frame);

        CHECK_EQ(at[PORT_CARD_SUM_AT], port_CardFrameSum(at));
    }
    /* the fifteen blocks a file may be in are zeroes */
    {
        s32 set = 0;

        for (i = PORT_CARD_BLOCK_SIZE; i < (s32)PORT_CARD_IMAGE_SIZE; i++) {
            set += test_Image(0)[i] != 0;
        }
        CHECK_EQ(set, 0);
    }

    /* the header block, for card.py to work out a second time */
    for (frame = 0; frame < PORT_CARD_FRAMES_PER_BLOCK; frame++) {
        test_Print("frame %d ", frame);
        for (i = 0; i < PORT_CARD_FRAME_SIZE; i++) {
            test_Print("%02x", test_Frame(0, frame)[i]);
        }
        test_Print("\n");
    }
}

/* ---- the events, call by call ---- */

/* What each call reports and how long it takes. The rows are derived from
 * Sony's libcard and from what the game waits for:
 *
 *   _card_info      one command to the card: EvSpNEW for a card the
 *                   driver has not been told about, EvSpIOE for one it
 *                   has, EvSpTIMOUT for an empty slot. The game polls the
 *                   software events once a frame and gives up after 120
 *                   (memcard.c, memcard_PollSwEventTimed,
 *                   EVENT_TIMEOUT_POLLS; src/rebushido/save.c,
 *                   LOOK_POLLS), so an answer must come within those.
 *   _card_load      the directory: EvSpIOE for a formatted card, EvSpNEW
 *                   for one that is not, which is how the game reads
 *                   "unformatted" (memcard.c, PROBE_WAIT_LOAD, and
 *                   save.c, LOOK_WAIT_LOAD), EvSpTIMOUT for an empty
 *                   slot.
 *   _card_clear     Sony's C (libcard/card.c) is _new_card and
 *                   _card_write of the last frame of the header block
 *                   with no buffer. The game waits for the card's own
 *                   event in a loop with no vertical blank in it
 *                   (memcard_WaitHwEvent), so HwCARD EvSpIOE is there
 *                   when the call returns; the software event comes with
 *                   it, and the game clears both before it uses them
 *                   (memcard_ProbeCard, rb_StartDirectoryLoad).
 *   open, close     done when they return, and no event: the game clears
 *   firstfile,      the card's events after it has opened the file
 *   nextfile,       (memcard_ReadFile) and waits for none of these
 *   delete, format
 *   read, write     HwCARD EvSpIOE when the call returns, which is what
 *                   the game waits for, and SwCARD EvSpIOE a frame of 128
 *                   bytes a vertical blank later, which it polls for
 *                   (memcard_FinishFileIo, rb_WaitFileIo). A write the
 *                   system refuses is HwCARD EvSpERROR at once, which the
 *                   game turns into "the save failed".
 */
static void test_EventSequence(void) {
    char path[PORT_CARD_NAME_SIZE + 8];
    u8 one[PORT_CARD_FRAME_SIZE];
    s32 fd;
    s32 i;

    test_OneNewCard();
    test_OpenEvents();
    test_ClearEvents();

    /* a card nothing has been told about: EvSpNEW, after one blank and
     * not before */
    CHECK_EQ(_card_info(0), 1);
    CHECK_EQ(test_Events(), TEST_NO_EVENT);
    test_Blanks(PORT_CARD_INFO_BLANKS);
    CHECK_EQ(test_Events(), TEST_EVENT(TEST_SW_NEW));

    /* the card's directory will not load until the driver has been told
     * about it, as the game's probe has it: _card_clear first */
    CHECK_EQ(_card_load(0), 1);
    test_Blanks(PORT_CARD_LOAD_BLANKS - 1);
    CHECK_EQ(test_Events(), TEST_NO_EVENT);
    test_Blanks(1);
    CHECK_EQ(test_Events(), TEST_EVENT(TEST_SW_IOE));

    /* _card_clear: the card answers at once, and the card stops being new */
    _card_clear(0);
    CHECK_EQ(test_Events(), TEST_EVENT(TEST_HW_IOE) | TEST_EVENT(TEST_SW_IOE));
    CHECK_EQ(_card_info(0), 1);
    test_Blanks(PORT_CARD_INFO_BLANKS);
    CHECK_EQ(test_Events(), TEST_EVENT(TEST_SW_IOE));
    /* and the frame it writes leaves the identifier where it was */
    CHECK_EQ(test_Frame(0, PORT_CARD_ID_COPY_FRAME)[0], PORT_CARD_ID_HIGH);

    /* the empty slot: nothing answers in time */
    CHECK_EQ(_card_info(0x10), 1);
    test_Blanks(PORT_CARD_INFO_BLANKS);
    CHECK_EQ(test_Events(), TEST_EVENT(TEST_SW_TIMEOUT));
    CHECK_EQ(_card_load(0x10), 1);
    test_Blanks(PORT_CARD_LOAD_BLANKS);
    CHECK_EQ(test_Events(), TEST_EVENT(TEST_SW_TIMEOUT));
    /* a card of a multitap, which the port has none of */
    CHECK_EQ(_card_info(1), 1);
    CHECK_EQ(test_Events(), TEST_EVENT(TEST_SW_TIMEOUT));

    /* making and opening a file reports nothing */
    test_Path(path, 0, 0, "A-FILE");
    fd = open(path, (1 << 16) | O_CREAT);
    CHECK(fd != -1);
    CHECK_EQ(test_Events(), TEST_NO_EVENT);
    CHECK_EQ(close(fd), 0);
    CHECK_EQ(test_Events(), TEST_NO_EVENT);
    CHECK_EQ(test_CountFiles(0, 0), 1);
    CHECK_EQ(test_Events(), TEST_NO_EVENT);

    /* a write: the card at once, the whole write a frame a blank later */
    for (i = 0; i < PORT_CARD_FRAME_SIZE; i++) {
        one[i] = (u8)(i + 1);
    }
    fd = open(path, O_NOWAIT | O_WRONLY);
    CHECK(fd != -1);
    CHECK_EQ(write(fd, one, (s32)sizeof(one)), (s32)sizeof(one));
    CHECK_EQ(test_Events(), TEST_EVENT(TEST_HW_IOE));
    test_Blanks(PORT_CARD_FRAME_BLANKS);
    CHECK_EQ(test_Events(), TEST_EVENT(TEST_SW_IOE));
    /* and it is on the disc, not waiting somewhere */
    CHECK(g_test_flushes > 0);
    CHECK_EQ(close(fd), 0);

    /* a write of four frames takes four blanks */
    fd = open(path, O_NOWAIT | O_WRONLY);
    CHECK_EQ(write(fd, test_Image(0), 4 * PORT_CARD_FRAME_SIZE),
             4 * PORT_CARD_FRAME_SIZE);
    test_ClearEvents();
    test_Blanks(3);
    CHECK_EQ(test_Events(), TEST_NO_EVENT);
    test_Blanks(1);
    CHECK_EQ(test_Events(), TEST_EVENT(TEST_SW_IOE));
    CHECK_EQ(close(fd), 0);

    /* a write the system refuses: the card's error, at once */
    g_test_write_fails = 1;
    fd = open(path, O_NOWAIT | O_WRONLY);
    CHECK_EQ(write(fd, one, (s32)sizeof(one)), -1);
    CHECK_EQ(test_Events(), TEST_EVENT(TEST_HW_ERROR));
    CHECK_EQ(close(fd), 0);
    g_test_write_fails = 0;

    /* a read the system refuses, the same way */
    g_test_read_fails = 1;
    fd = open(path, O_NOWAIT | O_RDONLY);
    CHECK_EQ(read(fd, one, (s32)sizeof(one)), -1);
    CHECK_EQ(test_Events(), TEST_EVENT(TEST_HW_ERROR));
    CHECK_EQ(close(fd), 0);
    g_test_read_fails = 0;
}

/* ---- files: made, written, read back, listed, deleted ---- */

static void test_Files(void) {
    char path[PORT_CARD_NAME_SIZE + 8];
    struct DIRENTRY entry;
    u8 written[0x400];
    u8 read_back[0x400];
    s32 fd;
    s32 i;

    test_OneNewCard();
    for (i = 0; i < (s32)sizeof(written); i++) {
        written[i] = (u8)(i * 7 + 3);
    }

    /* a file of one block, written and read back */
    test_Path(path, 0, 0, "ONE-BLOCK");
    fd = open(path, (1 << 16) | O_CREAT);
    CHECK(fd != -1);
    CHECK_EQ(close(fd), 0);
    fd = open(path, O_NOWAIT | O_WRONLY);
    CHECK_EQ(write(fd, written, (s32)sizeof(written)), (s32)sizeof(written));
    CHECK_EQ(close(fd), 0);
    for (i = 0; i < (s32)sizeof(read_back); i++) {
        read_back[i] = 0;
    }
    fd = open(path, O_NOWAIT | O_RDONLY);
    CHECK(fd != -1);
    CHECK_EQ(read(fd, read_back, (s32)sizeof(read_back)),
             (s32)sizeof(read_back));
    CHECK(test_BytesEqual(read_back, written, sizeof(written)));
    CHECK_EQ(close(fd), 0);
    /* the bytes are in the file's block of the image */
    CHECK(test_BytesEqual(test_Image(0) + PORT_CARD_BLOCK_SIZE, written,
                          sizeof(written)));

    /* a file of three blocks: its directory frames are first, middle and
     * last, linked, with the whole size and the name in the first */
    test_Path(path, 0, 0, "THREE-BLOCKS");
    fd = open(path, (3 << 16) | O_CREAT);
    CHECK(fd != -1);
    CHECK_EQ(close(fd), 0);
    {
        u8 *first = test_Frame(0, PORT_CARD_DIR_FRAME + 1);
        u8 *middle = test_Frame(0, PORT_CARD_DIR_FRAME + 2);
        u8 *last = test_Frame(0, PORT_CARD_DIR_FRAME + 3);

        CHECK_EQ(first[PORT_CARD_DIR_STATE], PORT_CARD_FIRST);
        CHECK_EQ(middle[PORT_CARD_DIR_STATE], PORT_CARD_MIDDLE);
        CHECK_EQ(last[PORT_CARD_DIR_STATE], PORT_CARD_LAST);
        /* the field counts the blocks from 0: block 3 is 2 */
        CHECK_EQ(port_CardGetHalf(first + PORT_CARD_DIR_NEXT), 2);
        CHECK_EQ(port_CardGetHalf(middle + PORT_CARD_DIR_NEXT), 3);
        CHECK_EQ(port_CardGetHalf(last + PORT_CARD_DIR_NEXT),
                 PORT_CARD_NO_NEXT);
        CHECK_EQ(port_CardGetWord(first + PORT_CARD_DIR_SIZE),
                 3 * PORT_CARD_BLOCK_SIZE);
        CHECK_EQ(middle[PORT_CARD_DIR_NAME], 0);
        CHECK_EQ(first[PORT_CARD_SUM_AT], port_CardFrameSum(first));
    }

    /* a write across the three blocks and a read back of the same bytes */
    fd = open(path, O_NOWAIT | O_WRONLY);
    for (i = 0; i < 3 * PORT_CARD_BLOCK_SIZE / (s32)sizeof(written); i++) {
        CHECK_EQ(write(fd, written, (s32)sizeof(written)),
                 (s32)sizeof(written));
    }
    /* the file is full: a write past its end writes nothing */
    CHECK_EQ(write(fd, written, (s32)sizeof(written)), 0);
    CHECK_EQ(close(fd), 0);
    fd = open(path, O_NOWAIT | O_RDONLY);
    for (i = 0; i < 3 * PORT_CARD_BLOCK_SIZE / (s32)sizeof(written); i++) {
        CHECK_EQ(read(fd, read_back, (s32)sizeof(read_back)),
                 (s32)sizeof(read_back));
        CHECK(test_BytesEqual(read_back, written, sizeof(written)));
    }
    CHECK_EQ(read(fd, read_back, (s32)sizeof(read_back)), 0);
    CHECK_EQ(close(fd), 0);
    /* and the second and third blocks of the image have them too */
    CHECK(test_BytesEqual(test_Image(0) + 3 * PORT_CARD_BLOCK_SIZE, written,
                          sizeof(written)));
    CHECK(test_BytesEqual(test_Image(0) + 4 * PORT_CARD_BLOCK_SIZE, written,
                          sizeof(written)));

    /* the directory: two files, their names and sizes */
    CHECK_EQ(test_CountFiles(0, 0), 2);
    test_Path(path, 0, 0, "*");
    CHECK(firstfile(path, &entry) == &entry);
    CHECK(test_StringsEqual(entry.name, "ONE-BLOCK"));
    CHECK_EQ(entry.size, PORT_CARD_BLOCK_SIZE);
    CHECK(nextfile(&entry) == &entry);
    CHECK(test_StringsEqual(entry.name, "THREE-BLOCKS"));
    CHECK_EQ(entry.size, 3 * PORT_CARD_BLOCK_SIZE);
    CHECK(nextfile(&entry) == NULL);

    /* a pattern with a run and with one character of its own */
    test_Path(path, 0, 0, "THREE*");
    CHECK(firstfile(path, &entry) == &entry);
    CHECK(test_StringsEqual(entry.name, "THREE-BLOCKS"));
    CHECK(nextfile(&entry) == NULL);
    test_Path(path, 0, 0, "?NE-BLOCK");
    CHECK(firstfile(path, &entry) == &entry);
    CHECK(test_StringsEqual(entry.name, "ONE-BLOCK"));
    test_Path(path, 0, 0, "*BLOCK");
    CHECK(firstfile(path, &entry) == &entry);
    CHECK(test_StringsEqual(entry.name, "ONE-BLOCK"));
    CHECK(nextfile(&entry) == NULL);
    test_Path(path, 0, 0, "NOTHING*");
    CHECK(firstfile(path, &entry) == NULL);

    /* a file that is there cannot be made again, and one that is not
     * cannot be opened */
    test_Path(path, 0, 0, "ONE-BLOCK");
    CHECK_EQ(open(path, (1 << 16) | O_CREAT), -1);
    test_Path(path, 0, 0, "NOT-THERE");
    CHECK_EQ(open(path, O_NOWAIT | O_RDONLY), -1);

    /* delete: the blocks are free again, the state keeps what the block
     * was, and the file is gone from the directory */
    test_Path(path, 0, 0, "THREE-BLOCKS");
    CHECK_EQ(delete(path), 1);
    CHECK_EQ(delete(path), 0);
    CHECK_EQ(test_CountFiles(0, 0), 1);
    CHECK_EQ(test_Frame(0, PORT_CARD_DIR_FRAME + 1)[PORT_CARD_DIR_STATE],
             (PORT_CARD_FREE & PORT_CARD_KIND_MASK) |
                 (PORT_CARD_FIRST & PORT_CARD_PART_MASK));
    CHECK_EQ(test_Frame(0, PORT_CARD_DIR_FRAME + 3)[PORT_CARD_DIR_STATE],
             (PORT_CARD_FREE & PORT_CARD_KIND_MASK) |
                 (PORT_CARD_LAST & PORT_CARD_PART_MASK));
    /* and its blocks can be had again */
    test_Path(path, 0, 0, "AFTER-DELETE");
    fd = open(path, (3 << 16) | O_CREAT);
    CHECK(fd != -1);
    CHECK_EQ(close(fd), 0);
    CHECK_EQ(test_CountFiles(0, 0), 2);

    /* the directory as the port left it, for card.py to read */
    test_Print("directory ");
    for (i = 0; i < (PORT_CARD_BLOCKS + 1) * PORT_CARD_FRAME_SIZE; i++) {
        test_Print("%02x", test_Image(0)[i]);
    }
    test_Print("\n");
}

/* ---- a full card ---- */

static void test_FullCard(void) {
    char path[PORT_CARD_NAME_SIZE + 8];
    char name[PORT_CARD_NAME_SIZE + 1];
    s32 fd;
    s32 i;

    test_OneNewCard();
    /* fifteen files of one block each fill it */
    for (i = 0; i < PORT_CARD_BLOCKS; i++) {
        name[0] = 'F';
        name[1] = (char)('A' + i);
        name[2] = 0;
        test_Path(path, 0, 0, name);
        fd = open(path, (1 << 16) | O_CREAT);
        CHECK(fd != -1);
        CHECK_EQ(close(fd), 0);
    }
    CHECK_EQ(test_CountFiles(0, 0), PORT_CARD_BLOCKS);
    /* the sixteenth has no block */
    test_Path(path, 0, 0, "ONE-TOO-MANY");
    CHECK_EQ(open(path, (1 << 16) | O_CREAT), -1);

    /* on an empty card a file of all fifteen blocks fits and one of
     * sixteen does not */
    test_OneNewCard();
    test_Path(path, 0, 0, "THE-WHOLE-CARD");
    fd = open(path, (PORT_CARD_BLOCKS << 16) | O_CREAT);
    CHECK(fd != -1);
    CHECK_EQ(close(fd), 0);
    CHECK_EQ(test_CountFiles(0, 0), 1);
    test_OneNewCard();
    test_Path(path, 0, 0, "TOO-BIG");
    CHECK_EQ(open(path, ((PORT_CARD_BLOCKS + 1) << 16) | O_CREAT), -1);
    CHECK_EQ(open(path, (0 << 16) | O_CREAT), -1);
}

/* ---- a card that is not formatted, and one with a damaged frame ---- */

static void test_Unformatted(void) {
    char path[PORT_CARD_NAME_SIZE + 8];
    s32 i;

    /* an image of the right size and nothing in it */
    test_StartOver();
    g_test_host_files[0].made = 1;
    g_test_host_files[0].size = PORT_CARD_IMAGE_SIZE;
    for (i = 0; i < (s32)PORT_CARD_IMAGE_SIZE; i++) {
        test_Image(0)[i] = 0;
    }
    port_SetCardPath(0, TEST_PATH_FIRST);
    InitCARD(1);
    test_OpenEvents();
    test_ClearEvents();

    /* a card is there and answers; its directory will not load */
    CHECK_EQ(_card_info(0), 1);
    test_Blanks(PORT_CARD_INFO_BLANKS);
    CHECK_EQ(test_Events(), TEST_EVENT(TEST_SW_NEW));
    _card_clear(0);
    test_ClearEvents();
    CHECK_EQ(_card_load(0), 1);
    test_Blanks(PORT_CARD_LOAD_BLANKS);
    CHECK_EQ(test_Events(), TEST_EVENT(TEST_SW_NEW));
    /* and no file of it can be opened or made */
    test_Path(path, 0, 0, "ANYTHING");
    CHECK_EQ(open(path, (1 << 16) | O_CREAT), -1);
    CHECK(firstfile(path, NULL) == NULL);
    /* the image was not written to */
    {
        s32 set = 0;

        for (i = 0; i < (s32)PORT_CARD_IMAGE_SIZE; i++) {
            set += test_Image(0)[i] != 0;
        }
        CHECK_EQ(set, 0);
    }

    /* formatting it: the port's own format, and then it works */
    test_Path(path, 0, 0, "");
    CHECK_EQ(format(path), 1);
    CHECK_EQ(test_Frame(0, PORT_CARD_ID_FRAME)[0], PORT_CARD_ID_HIGH);
    CHECK_EQ(test_CountFiles(0, 0), 0);
    test_Path(path, 0, 0, "AFTER-FORMAT");
    CHECK(open(path, (1 << 16) | O_CREAT) != -1);
    CHECK_EQ(test_CountFiles(0, 0), 1);
    /* a format leaves the blocks' own bytes alone, as the BIOS does */
    CHECK_EQ(format(path), 1);
    CHECK_EQ(test_CountFiles(0, 0), 0);

    /* a damaged directory frame: one byte of it, and the card is not
     * formatted any more */
    test_OneNewCard();
    CHECK_EQ(_card_info(0), 1);
    test_Frame(0, PORT_CARD_DIR_FRAME + 4)[PORT_CARD_DIR_NAME] = 'X';
    test_ClearEvents();
    CHECK_EQ(_card_load(0), 1);
    test_Blanks(PORT_CARD_LOAD_BLANKS);
    CHECK_EQ(test_Events(), TEST_EVENT(TEST_SW_NEW));
    test_Path(path, 0, 0, "ANYTHING");
    CHECK_EQ(open(path, (1 << 16) | O_CREAT), -1);
    /* the identifier's own frame, the same way */
    test_OneNewCard();
    CHECK_EQ(_card_info(0), 1);
    test_Frame(0, PORT_CARD_ID_FRAME)[1] = 'X';
    test_ClearEvents();
    CHECK_EQ(_card_load(0), 1);
    test_Blanks(PORT_CARD_LOAD_BLANKS);
    CHECK_EQ(test_Events(), TEST_EVENT(TEST_SW_NEW));

    /* an image that is not 128 KB is no card at all */
    test_StartOver();
    g_test_host_files[0].made = 1;
    g_test_host_files[0].size = PORT_CARD_IMAGE_SIZE / 2;
    port_SetCardPath(0, TEST_PATH_FIRST);
    InitCARD(1);
    test_ClearEvents();
    CHECK_EQ(_card_info(0), 1);
    test_Blanks(PORT_CARD_INFO_BLANKS);
    CHECK_EQ(test_Events(), TEST_EVENT(TEST_SW_TIMEOUT));
    CHECK(g_test_said_lines > 0);
}

/* ---- two slots, and no card at all ---- */

static void test_TwoSlots(void) {
    char path[PORT_CARD_NAME_SIZE + 8];
    s32 fd;

    test_StartOver();
    port_SetCardPath(0, TEST_PATH_FIRST);
    port_SetCardPath(1, TEST_PATH_SECOND);
    InitCARD(1);
    test_OpenEvents();
    test_ClearEvents();

    /* a file on the card in slot 2 is not on the one in slot 1 */
    test_Path(path, 1, 0, "IN-SLOT-TWO");
    fd = open(path, (2 << 16) | O_CREAT);
    CHECK(fd != -1);
    CHECK_EQ(close(fd), 0);
    CHECK_EQ(test_CountFiles(1, 0), 1);
    CHECK_EQ(test_CountFiles(0, 0), 0);
    /* each slot has its own image file */
    CHECK_EQ(g_test_host_files[0].made, 1);
    CHECK_EQ(g_test_host_files[1].made, 1);
    CHECK_EQ(test_Frame(1, PORT_CARD_DIR_FRAME)[PORT_CARD_DIR_STATE],
             PORT_CARD_FIRST);
    CHECK_EQ(test_Frame(0, PORT_CARD_DIR_FRAME)[PORT_CARD_DIR_STATE],
             PORT_CARD_FREE);
    /* and each answers for itself: the card in slot 2 is new until it is
     * told about, and the one in slot 1 stays new meanwhile */
    CHECK_EQ(_card_info(0x10), 1);
    test_Blanks(PORT_CARD_INFO_BLANKS);
    CHECK_EQ(test_Events(), TEST_EVENT(TEST_SW_NEW));
    _card_clear(0x10);
    test_ClearEvents();
    CHECK_EQ(_card_info(0x10), 1);
    test_Blanks(PORT_CARD_INFO_BLANKS);
    CHECK_EQ(test_Events(), TEST_EVENT(TEST_SW_IOE));
    CHECK_EQ(_card_info(0), 1);
    test_Blanks(PORT_CARD_INFO_BLANKS);
    CHECK_EQ(test_Events(), TEST_EVENT(TEST_SW_NEW));

    /* --no-cards: both slots are empty, whatever was named */
    test_StartOver();
    port_SetCardPath(0, TEST_PATH_FIRST);
    port_SetCardPath(1, TEST_PATH_SECOND);
    port_SetCardsOff();
    InitCARD(1);
    test_ClearEvents();
    CHECK_EQ(_card_info(0), 1);
    test_Blanks(PORT_CARD_INFO_BLANKS);
    CHECK_EQ(test_Events(), TEST_EVENT(TEST_SW_TIMEOUT));
    CHECK_EQ(_card_load(0), 1);
    test_Blanks(PORT_CARD_LOAD_BLANKS);
    CHECK_EQ(test_Events(), TEST_EVENT(TEST_SW_TIMEOUT));
    test_Path(path, 0, 0, "ANYTHING");
    CHECK_EQ(open(path, (1 << 16) | O_CREAT), -1);
    CHECK(firstfile(path, NULL) == NULL);
    CHECK_EQ(format(path), 0);
    CHECK_EQ(delete(path), 0);
    /* no image was made */
    CHECK_EQ(g_test_host_files[0].made, 0);
    CHECK_EQ(g_test_host_files[1].made, 0);
    /* and a name that is not a card's is the link cable's port, which can
     * be opened and says nothing (event.c had this before the card did) */
    CHECK(open("sio:", O_NOWAIT | O_RDONLY) != -1);
}

/* ---- the game's save's table of patches ---- */

/* A save file of the game's own size with one patch in it: an address of
 * the PlayStation's memory and a halfword for it. */
static void test_PatchTable(void) {
    char path[PORT_CARD_NAME_SIZE + 8];
    u8 save[MEMCARD_FILE_SIZE];
    u8 read_back[MEMCARD_FILE_SIZE];
    s32 fd;
    u32 i;

    test_OneNewCard();
    for (i = 0; i < sizeof(save); i++) {
        save[i] = 0;
    }
    /* a patch: 0x801FFF00, then 0xDEAD */
    port_CardPutWord(save + PORT_CARD_PATCH_ADDRS_AT, PORT_RAM_ADDR + 0x1FFF00);
    port_CardPutHalf(save + PORT_CARD_PATCH_VALUES_AT, 0xDEAD);
    /* and a byte of the record, to see that the rest comes back whole */
    save[MEMCARD_HEADER_SIZE] = 0x5A;

    test_Path(path, 0, 0, "BASLUS-00663BUSHIDO2");
    fd = open(path, (1 << 16) | O_CREAT);
    CHECK(fd != -1);
    CHECK_EQ(close(fd), 0);
    fd = open(path, O_NOWAIT | O_WRONLY);
    CHECK_EQ(write(fd, save, (s32)sizeof(save)), (s32)sizeof(save));
    CHECK_EQ(close(fd), 0);
    /* the port wrote what it was given: the table is the card's business,
     * not the writer's */
    CHECK(test_BytesEqual(test_Image(0) + PORT_CARD_BLOCK_SIZE, save,
                          sizeof(save)));

    /* read back, the table is empty and the port has said so */
    test_ForgetWhatWasSaid();
    fd = open(path, O_NOWAIT | O_RDONLY);
    CHECK_EQ(read(fd, read_back, (s32)sizeof(read_back)),
             (s32)sizeof(read_back));
    CHECK_EQ(close(fd), 0);
    CHECK_EQ(g_test_said_lines, 1);
    CHECK_EQ(read_back[MEMCARD_HEADER_SIZE], 0x5A);
    {
        s32 set = 0;

        for (i = PORT_CARD_PATCH_ADDRS_AT; i < PORT_CARD_PATCH_END; i++) {
            set += read_back[i] != 0;
        }
        CHECK_EQ(set, 0);
    }

    /* a save with an empty table, which is the only kind the game writes,
     * is read as it is and nothing is said */
    for (i = 0; i < sizeof(save); i++) {
        save[i] = (u8)(i & 0x7F);
    }
    for (i = PORT_CARD_PATCH_ADDRS_AT; i < PORT_CARD_PATCH_END; i++) {
        save[i] = 0;
    }
    fd = open(path, O_NOWAIT | O_WRONLY);
    CHECK_EQ(write(fd, save, (s32)sizeof(save)), (s32)sizeof(save));
    CHECK_EQ(close(fd), 0);
    test_ForgetWhatWasSaid();
    fd = open(path, O_NOWAIT | O_RDONLY);
    CHECK_EQ(read(fd, read_back, (s32)sizeof(read_back)),
             (s32)sizeof(read_back));
    CHECK_EQ(close(fd), 0);
    CHECK_EQ(g_test_said_lines, 0);
    CHECK(test_BytesEqual(read_back, save, sizeof(save)));

    /* another file's bytes are never looked at: ReBushido's settings file
     * of the same size comes back as it is */
    test_Path(path, 0, 0, "BASLUS-00663REBUSHI");
    fd = open(path, (1 << 16) | O_CREAT);
    CHECK(fd != -1);
    CHECK_EQ(close(fd), 0);
    for (i = 0; i < sizeof(save); i++) {
        save[i] = (u8)(0xFF - (i & 0x7F));
    }
    fd = open(path, O_NOWAIT | O_WRONLY);
    CHECK_EQ(write(fd, save, (s32)sizeof(save)), (s32)sizeof(save));
    CHECK_EQ(close(fd), 0);
    test_ForgetWhatWasSaid();
    fd = open(path, O_NOWAIT | O_RDONLY);
    CHECK_EQ(read(fd, read_back, (s32)sizeof(read_back)),
             (s32)sizeof(read_back));
    CHECK_EQ(close(fd), 0);
    CHECK_EQ(g_test_said_lines, 0);
    CHECK(test_BytesEqual(read_back, save, sizeof(save)));

    /* where the table is, against the structure the game reads the file
     * as (memcard.h) */
    CHECK_EQ(PORT_CARD_PATCH_ADDRS_AT, MEMCARD_HEADER_SIZE + 0x78);
    CHECK_EQ(PORT_CARD_PATCH_END, MEMCARD_FILE_SIZE - 4);
    CHECK_EQ(sizeof(SaveData), MEMCARD_FILE_SIZE - MEMCARD_HEADER_SIZE);
}

/* ---- the names of a card's paths ---- */

static void test_Paths(void) {
    char path[PORT_CARD_NAME_SIZE + 8];
    s32 i;

    test_OneNewCard();
    test_Path(path, 0, 0, "A-NAME");
    CHECK(open(path, (1 << 16) | O_CREAT) != -1);

    /* a name of twenty characters, which has no zero after it in the
     * directory's twenty-one bytes */
    for (i = 0; i < PORT_CARD_NAME_SIZE; i++) {
        path[PORT_CARD_PATH_NAME + i] = (char)('A' + i);
    }
    path[PORT_CARD_PATH_NAME + PORT_CARD_NAME_SIZE] = 0;
    CHECK(open(path, (1 << 16) | O_CREAT) != -1);
    CHECK(open(path, O_NOWAIT | O_RDONLY) != -1);
    CHECK_EQ(test_CountFiles(0, 0), 2);

    /* a path that is not "buPC:" and a name */
    CHECK_EQ(open("bux0:NAME", O_NOWAIT | O_RDONLY), -1);
    CHECK_EQ(open("bu00NAME", O_NOWAIT | O_RDONLY), -1);
    /* port 2, which the port has no slot for */
    CHECK_EQ(open("bu20:NAME", O_NOWAIT | O_RDONLY), -1);
    CHECK_EQ(format("bu20:"), 0);
}

void test_Main(void) {
    test_SetUpHost();
    test_Format();
    test_EventSequence();
    test_Files();
    test_FullCard();
    test_Unformatted();
    test_TwoSlots();
    test_PatchTable();
    test_Paths();
}
