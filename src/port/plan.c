/* A run's script in the native port: what is pressed and when, what is
 * watched, and when the run ends (port_platform.h).
 *
 * The script is a play test's plan, the same file tools/playtest.py
 * writes for the emulator and tools/play/runner.lua follows there, and
 * what this prints of a run are that runner's lines. So the same test
 * can be run on the emulator and on the port and the two logs laid side
 * by side (tools/port_compare.py). The plan's lines, words apart by
 * spaces, numbers decimal:
 *
 *   limit N                         the run ends at vertical blank N
 *   gpu emulator                    the GPU draws as the emulator's
 *                                   where that is not as the console's
 *                                   (port_gpu.h): for a run whose picture
 *                                   is held to the emulator's
 *   var NAME ADDR SIZE SIGNED       a value to write down
 *   dump NAME ADDR SIZE             memory written to NAME.bin at the end
 *   snap NAME ADDR SIZE             memory written to CP.NAME.snap at
 *                                   every checkpoint CP that asks for it
 *   in CP OFFSET PAD BUTTON HOLD    a press, OFFSET blanks after
 *                                   checkpoint number CP was reached (0:
 *                                   after the start), held HOLD blanks
 *   in CP OFFSET PAD BUTTON HOLD f  ... OFFSET frames of the game after
 *                                   it, held HOLD frames
 *   cp NAME SHOT WITHIN at N        a checkpoint at vertical blank N
 *                                   (SHOT: 1 in it asks for the picture,
 *                                   2 for the `snap` memory, 8 for video
 *                                   memory, and from 256 up it has a bit
 *                                   for each `snap` line that is written,
 *                                   the first line's lowest: all if none)
 *   cp NAME SHOT WITHIN after N     ... N blanks after the one before
 *   cp NAME SHOT WITHIN frames N    ... N frames of the game after it,
 *                                   as the Nth begins
 *   cp NAME SHOT WITHIN call ADDR N ... as the function at ADDR is called
 *                                   for the Nth time after it
 *
 * A checkpoint whose SHOT has 4 in it begins a list of calls: from it to
 * the next checkpoint every function of the game that is called is a
 * line of CP.calls, its name after a space for each call it is inside.
 * (Only the port writes one: the game's files are compiled so that each
 * function says when it begins and ends, -finstrument-functions.)
 *   cp NAME SHOT WITHIN when V OP X [V OP X ...]
 *                                   ... when value number V compares so
 *                                   with X, every one of them; given up
 *                                   WITHIN blanks after the one before
 *   cp NAME SHOT WITHIN whenf V OP X [V OP X ...]
 *                                   ... the same, looked for only as a
 *                                   frame of the game begins
 *
 * A frame of the game begins where the game resets its counter of scan
 * lines (ResetRCnt, clock.c), once a frame: port_OnFrame. The emulator
 * is told that function's address by a `frame` line and stops there. A
 * press or a checkpoint counted in frames is so at the same moment of
 * the game on both, however long either took to get there; one counted
 * in blanks is not, because the port computes a frame in no time.
 *
 * The emulator's other lines (stall, alive, movie, text, nocards, frame)
 * are read and not used. What a run prints:
 *
 *   cp NAME BLANK VALUE...          a checkpoint, with every value
 *   shot NAME WIDTH HEIGHT 16       its picture, written to NAME.raw: the
 *                                   displayed rectangle's 16-bit pixels
 *   miss NAME BLANK                 a checkpoint not reached in time
 *   done BLANK                      the last checkpoint was reached
 *   limit BLANK                     the run's last blank came
 *   trace BLANK NAME=VALUE...       (--trace) the values that changed
 *
 * The values are found by the names the build wrote beside the program
 * (tools/port_build.py): the `var` lines of that file are the values of
 * tests/play/state.toml at this program's addresses, and its `sym` lines
 * every function, for a crash to say where it was. */
#include "common.h"
#include "port_gpu.h"
#include "port_host.h"
#include "port_spu.h"
#include "bb2.h"

#define VALUES_MAX 128
#define PRESSES_MAX 1024
#define CHECKPOINTS_MAX 2048
#define TESTS_MAX 8
#define DUMPS_MAX 16
#define SNAPS_MAX 16
#define WORDS_MAX 40
#define FILE_BYTES_MAX 0x800000
/* How many words each kind of line has, and where a checkpoint's line
 * says how it is reached and its tests begin. */
#define VAR_WORDS 5
#define SYM_WORDS 5
#define SYM_ADDRESS 1
#define SYM_KIND 2
#define SYM_SIZE 3
#define SYM_NAME 4
#define IN_WORDS 6
#define IN_WORDS_FRAMES 7
#define IN_BUTTON 4
#define IN_HOLD 5
#define DUMP_WORDS 4
#define CP_WORDS_MIN 6
#define CP_SHOT 2
#define CP_PICTURE_BIT 1
#define CP_SNAP_BIT 2
#define CP_CALLS_BIT 4
#define CP_VRAM_BIT 8
#define CP_SPU_BIT 16
/* A screenshot's pixels are 16 bits. */
#define PICTURE_BITS 16
/* The digits of a blank's number in a picture's name. */
#define BLANK_DIGITS 6
#define DECIMAL 10
#define CP_SNAPS_SHIFT 8
#define CP_CALL_WORDS 7
#define CP_CALL_COUNT 6
#define CALL_INDENT_MAX 64
/* A file of the run's folder is named by the folder and four pieces. */
#define RUN_FILE_PIECES 5
#define CP_WITHIN 3
#define CP_HOW 4
#define CP_FIRST_TEST 5
#define TEST_WORDS 3
#define COMPARE_KINDS 6
#define BITS_PER_BYTE 8
#define HEX_DIGIT_BITS 4
#define HEX_DIGIT_MASK 0xF
#define HEX_LETTER_VALUE 10
#define PATH_BYTES_MAX 512
/* A run with no plan and no --frames ends after a minute of the game's
 * time. */
#define DEFAULT_BLANK_LIMIT 3600

typedef struct {
    const char *name;
    u32 address;
    s32 size;
    s32 is_signed;
    s32 last; /* what the trace last printed */
} PortValue;

typedef struct {
    s32 checkpoint; /* timed from this one's blank; 0 from the start */
    u32 offset;
    s32 pad;
    u32 button;
    u32 hold;
    s32 in_frames; /* the offset and the hold are frames of the game */
    u32 down_at;   /* the blanks, or the frames, it goes down and comes */
    u32 up_at;     /* up at; 0 until its checkpoint has been reached */
} PortPress;

enum PortCheckpointKind {
    CHECKPOINT_AT,
    CHECKPOINT_AFTER,
    CHECKPOINT_WHEN,
    CHECKPOINT_FRAMES,
    CHECKPOINT_CALL,
};

/* Where a run is when it looks for its next checkpoint. */
enum PortEvent {
    EVENT_BLANK, /* a vertical blank */
    EVENT_FRAME, /* a frame of the game begins */
    EVENT_CALL,  /* the function the checkpoint waits for is called */
};

enum PortCompare {
    COMPARE_EQ,
    COMPARE_NE,
    COMPARE_LT,
    COMPARE_LE,
    COMPARE_GT,
    COMPARE_GE,
};

typedef struct {
    s32 value; /* the value's number, from 0 */
    s32 compare;
    s32 with;
} PortTest;

typedef struct {
    const char *name;
    s32 kind;
    s32 picture;  /* the picture is written at it */
    s32 vram;     /* video memory is written at it */
    s32 spu;      /* sound memory is written at it */
    s32 snap;     /* the `snap` memory is written at it */
    u32 snaps;    /* which: a bit a `snap` line; 0 for all */
    s32 at_frame; /* CHECKPOINT_WHEN: only as a frame begins */
    s32 calls;    /* the list of calls begins at it */
    u32 within;
    u32 blanks;   /* or frames of the game, or calls of `function` */
    u32 function; /* CHECKPOINT_CALL: the function's address */
    PortTest tests[TESTS_MAX];
    s32 test_count;
} PortCheckpoint;

typedef struct {
    const char *name;
    u32 address;
    u32 size;
} PortDump;

typedef struct {
    u32 address;
    u32 size;
    const char *name;
} PortFunction;

static PortValue g_port_values[VALUES_MAX];
static s32 g_port_value_count;
static PortPress g_port_presses[PRESSES_MAX];
static s32 g_port_press_count;
static PortCheckpoint g_port_checkpoints[CHECKPOINTS_MAX];
static s32 g_port_checkpoint_count;
static s32 g_port_next_checkpoint;
static u32 g_port_last_checkpoint_blank;
/* The frames of the game that have begun, and how many had at the last
 * checkpoint. */
static u32 g_port_frame;
static u32 g_port_last_checkpoint_frame;
static PortDump g_port_dumps[DUMPS_MAX];
static s32 g_port_dump_count;
static PortDump g_port_snaps[SNAPS_MAX];
static s32 g_port_snap_count;
/* The function the next checkpoint waits for, 0 if it waits for none,
 * and its calls since the last checkpoint. */
static u32 g_port_awaited_function;
static u32 g_port_awaited_calls;
/* The list of calls being written, -1 if none is, and how many calls
 * the game is inside. */
static s32 g_port_calls_file = -1;
static s32 g_port_call_depth;
static PortFunction *g_port_functions;
static u32 g_port_function_count;
static u32 g_port_blank_limit = DEFAULT_BLANK_LIMIT;
/* The folder the dumps are written into, with its last `/`; "" for the
 * one the program was started in. */
static const char *g_port_dump_folder = "";
/* The folder a PNG of the picture goes into every so many blanks, with
 * its last `/`; NULL for none. And a file for video memory at the end. */
static const char *g_port_shots_folder;
static u32 g_port_shots_every;
static const char *g_port_vram_path;
static s32 g_port_trace;
static s32 g_port_trace_started;
static s32 g_port_has_plan;

static const struct {
    const char *name;
    u32 mask;
} g_port_buttons[] = {
    {"SELECT", PADselect}, {"START", PADstart},  {"UP", PADLup},
    {"RIGHT", PADLright},  {"DOWN", PADLdown},   {"LEFT", PADLleft},
    {"L2", PADo},          {"R2", PADm},         {"L1", PADn},
    {"R1", PADl},          {"TRIANGLE", PADRup}, {"CIRCLE", PADRright},
    {"CROSS", PADRdown},   {"SQUARE", PADRleft},
};

/* ---- reading a file of lines ---- */

/* A whole file in memory of its own, with a 0 after it; NULL if it
 * cannot be read. */
static char *port_ReadTextFile(const char *path) {
    s32 file = port_HostOpen(path);
    u32 size;
    char *text;

    if (file < 0) {
        return NULL;
    }
    size = port_HostFileSize(file);
    text = size <= FILE_BYTES_MAX ? port_HostAllocate(size + 1) : NULL;
    if (text != NULL && port_HostReadAt(file, 0, text, size) != size) {
        text = NULL;
    }
    port_HostClose(file);
    if (text != NULL) {
        text[size] = 0;
    }
    return text;
}

/* Cuts the line at `*cursor` into words, in place, and moves the cursor
 * to the next line. Returns the number of words; -1 at the file's end. */
static s32 port_NextLine(char **cursor, char **words) {
    char *at = *cursor;
    s32 count = 0;

    if (*at == 0) {
        return -1;
    }
    while (*at != 0 && *at != '\n') {
        while (*at == ' ' || *at == '\t' || *at == '\r') {
            *at++ = 0;
        }
        if (*at == 0 || *at == '\n') {
            break;
        }
        if (count < WORDS_MAX) {
            words[count++] = at;
        }
        while (*at != 0 && *at != ' ' && *at != '\t' && *at != '\r' &&
               *at != '\n') {
            at++;
        }
    }
    if (*at == '\n') {
        *at++ = 0;
    }
    *cursor = at;
    return count;
}

static s32 port_PlanNumber(const char *word, const char *line_kind) {
    s32 value = 0;

    if (!port_ParseNumber(word, &value)) {
        port_Fail("rebushido: the plan's `%s` line has `%s` where a "
                  "number goes\n",
                  line_kind, word);
    }
    return value;
}

/* A `var` line: a value to write down, by its address, size and
 * whether it has a sign. */
static void port_AddValue(char **words, s32 count) {
    PortValue *value;

    if (count != VAR_WORDS || g_port_value_count == VALUES_MAX) {
        port_Fail("rebushido: a `var` line is `var NAME ADDR SIZE SIGNED`, "
                  "and there are %d at most\n",
                  VALUES_MAX);
    }
    value = &g_port_values[g_port_value_count++];
    value->name = words[1];
    value->address = (u32)port_PlanNumber(words[2], "var");
    value->size = port_PlanNumber(words[3], "var");
    value->is_signed = port_PlanNumber(words[VAR_WORDS - 1], "var");
}

/* ---- the names ---- */

/* The hexadecimal digits of a `sym` line's address. */
static u32 port_ParseHex(const char *word) {
    u32 value = 0;

    for (; *word != 0; word++) {
        u32 digit =
            (u32)(*word >= 'a' ? *word - 'a' + HEX_LETTER_VALUE : *word - '0');

        value = (value << HEX_DIGIT_BITS) | (digit & HEX_DIGIT_MASK);
    }
    return value;
}

/* Reads the build's names: the values' lines, and every function's
 * (port_platform.h). */
s32 port_LoadNames(const char *path) {
    char *text = port_ReadTextFile(path);
    char *words[WORDS_MAX];
    char *cursor = text;
    u32 room = 0;
    s32 count;

    if (text == NULL) {
        return 0;
    }
    /* a first walk for how many functions there are */
    for (cursor = text; *cursor != 0; cursor++) {
        room += *cursor == '\n';
    }
    g_port_functions = port_HostAllocate(room * sizeof(PortFunction));
    if (g_port_functions == NULL) {
        return 0;
    }
    cursor = text;
    while ((count = port_NextLine(&cursor, words)) >= 0) {
        if (count == VAR_WORDS && port_TextsEqual(words[0], "var")) {
            port_AddValue(words, count);
        } else if (count == SYM_WORDS && port_TextsEqual(words[0], "sym") &&
                   words[SYM_KIND][0] == 'f') {
            PortFunction *function = &g_port_functions[g_port_function_count++];
            s32 size = 0;

            function->address = port_ParseHex(words[SYM_ADDRESS]);
            port_ParseNumber(words[SYM_SIZE], &size);
            function->size = (u32)size;
            function->name = words[SYM_NAME];
        }
    }
    return 1;
}

/* The last function that begins at or before an address, if the
 * address is inside it (port_platform.h). */
const char *port_FindFunction(u32 address, u32 *offset) {
    const PortFunction *best = NULL;
    u32 i;

    for (i = 0; i < g_port_function_count; i++) {
        const PortFunction *function = &g_port_functions[i];

        if (function->address <= address &&
            (best == NULL || function->address >= best->address)) {
            best = function;
        }
    }
    /* past the end of the last function before it: in none */
    if (best == NULL || address - best->address > best->size) {
        return NULL;
    }
    *offset = address - best->address;
    return best->name;
}

/* ---- the plan ---- */

static u32 port_ButtonMask(const char *name) {
    u32 i;

    for (i = 0; i < sizeof(g_port_buttons) / sizeof(g_port_buttons[0]); i++) {
        if (port_TextsEqual(g_port_buttons[i].name, name)) {
            return g_port_buttons[i].mask;
        }
    }
    port_Fail("rebushido: the plan presses `%s`, which is no button\n", name);
}

static s32 port_CompareKind(const char *word) {
    static const char * const names[] = {"==", "!=", "<", "<=", ">", ">="};
    s32 i;

    for (i = 0; i < COMPARE_KINDS; i++) {
        if (port_TextsEqual(names[i], word)) {
            return i;
        }
    }
    port_Fail("rebushido: the plan compares with `%s`\n", word);
}

/* An `in` line: a press, timed from a checkpoint. */
static void port_AddPress(char **words, s32 count) {
    PortPress *press;

    if ((count != IN_WORDS && count != IN_WORDS_FRAMES) ||
        g_port_press_count == PRESSES_MAX) {
        port_Fail("rebushido: an `in` line is `in CP OFFSET PAD BUTTON "
                  "HOLD [f]`, and there are %d at most\n",
                  PRESSES_MAX);
    }
    press = &g_port_presses[g_port_press_count++];
    press->checkpoint = port_PlanNumber(words[1], "in");
    press->offset = (u32)port_PlanNumber(words[2], "in");
    press->pad = port_PlanNumber(words[3], "in") - 1;
    press->button = port_ButtonMask(words[IN_BUTTON]);
    press->hold = (u32)port_PlanNumber(words[IN_HOLD], "in");
    press->in_frames = count == IN_WORDS_FRAMES;
    press->down_at = 0;
    press->up_at = 0;
}

/* A `cp` line: a checkpoint, reached at a blank, some blanks after the
 * one before, or when its tests all hold. */
static void port_AddCheckpoint(char **words, s32 count) {
    PortCheckpoint *checkpoint;
    s32 i;

    if (count < CP_WORDS_MIN || g_port_checkpoint_count == CHECKPOINTS_MAX) {
        port_Fail("rebushido: a `cp` line is `cp NAME SHOT WITHIN HOW ...`, "
                  "and there are %d at most\n",
                  CHECKPOINTS_MAX);
    }
    checkpoint = &g_port_checkpoints[g_port_checkpoint_count++];
    checkpoint->name = words[1];
    checkpoint->picture =
        (port_PlanNumber(words[CP_SHOT], "cp") & CP_PICTURE_BIT) != 0;
    checkpoint->vram =
        (port_PlanNumber(words[CP_SHOT], "cp") & CP_VRAM_BIT) != 0;
    checkpoint->spu = (port_PlanNumber(words[CP_SHOT], "cp") & CP_SPU_BIT) != 0;
    checkpoint->snap =
        (port_PlanNumber(words[CP_SHOT], "cp") & CP_SNAP_BIT) != 0;
    checkpoint->calls =
        (port_PlanNumber(words[CP_SHOT], "cp") & CP_CALLS_BIT) != 0;
    checkpoint->snaps =
        (u32)port_PlanNumber(words[CP_SHOT], "cp") >> CP_SNAPS_SHIFT;
    checkpoint->at_frame = port_TextsEqual(words[CP_HOW], "whenf");
    checkpoint->function = 0;
    checkpoint->within = (u32)port_PlanNumber(words[CP_WITHIN], "cp");
    checkpoint->test_count = 0;
    checkpoint->blanks = 0;
    if (port_TextsEqual(words[CP_HOW], "at")) {
        checkpoint->kind = CHECKPOINT_AT;
        checkpoint->blanks = (u32)port_PlanNumber(words[CP_FIRST_TEST], "cp");
    } else if (port_TextsEqual(words[CP_HOW], "after")) {
        checkpoint->kind = CHECKPOINT_AFTER;
        checkpoint->blanks = (u32)port_PlanNumber(words[CP_FIRST_TEST], "cp");
    } else if (port_TextsEqual(words[CP_HOW], "frames")) {
        checkpoint->kind = CHECKPOINT_FRAMES;
        checkpoint->blanks = (u32)port_PlanNumber(words[CP_FIRST_TEST], "cp");
    } else if (
        port_TextsEqual(words[CP_HOW], "call") && count == CP_CALL_WORDS) {
        checkpoint->kind = CHECKPOINT_CALL;
        checkpoint->function = (u32)port_PlanNumber(words[CP_FIRST_TEST], "cp");
        checkpoint->blanks = (u32)port_PlanNumber(words[CP_CALL_COUNT], "cp");
    } else if (port_TextsEqual(words[CP_HOW], "when") ||
               port_TextsEqual(words[CP_HOW], "whenf")) {
        checkpoint->kind = CHECKPOINT_WHEN;
        for (i = CP_FIRST_TEST; i + 2 < count; i += TEST_WORDS) {
            PortTest *test;

            if (checkpoint->test_count == TESTS_MAX) {
                port_Fail("rebushido: checkpoint `%s` tests more than %d "
                          "values\n",
                          checkpoint->name, TESTS_MAX);
            }
            test = &checkpoint->tests[checkpoint->test_count++];
            test->value = port_PlanNumber(words[i], "cp") - 1;
            test->compare = port_CompareKind(words[i + 1]);
            test->with = port_PlanNumber(words[i + 2], "cp");
            if (test->value < 0 || test->value >= g_port_value_count) {
                port_Fail(
                    "rebushido: checkpoint `%s` tests value %d, and "
                    "the plan has %d\n",
                    checkpoint->name, test->value + 1, g_port_value_count);
            }
        }
    } else {
        port_Fail("rebushido: checkpoint `%s` is reached `%s`?\n",
                  checkpoint->name, words[CP_HOW]);
    }
}

/* A piece of the PlayStation's memory to write out at the end
 * (port_platform.h). */
void port_AddDump(const char *name, u32 address, u32 size) {
    if (g_port_dump_count == DUMPS_MAX) {
        port_Fail("rebushido: %d dumps at most\n", DUMPS_MAX);
    }
    if (!port_IsMapped(address, size)) {
        port_Fail("rebushido: dump `%s`: 0x%08X and the %u bytes after it "
                  "are not all the PlayStation's memory\n",
                  name, address, size);
    }
    g_port_dumps[g_port_dump_count].name = name;
    g_port_dumps[g_port_dump_count].address = address;
    g_port_dumps[g_port_dump_count].size = size;
    g_port_dump_count++;
}

/* A piece of memory to write out at each checkpoint that asks for it:
 * the PlayStation's, or the program's own variables. */
static void port_AddSnap(const char *name, u32 address, u32 size) {
    if (g_port_snap_count == SNAPS_MAX) {
        port_Fail("rebushido: %d `snap` lines at most\n", SNAPS_MAX);
    }
    g_port_snaps[g_port_snap_count].name = name;
    g_port_snaps[g_port_snap_count].address = address;
    g_port_snaps[g_port_snap_count].size = size;
    g_port_snap_count++;
}

/* The window's keyboard and controllers in a plan's place
 * (port_platform.h): nothing in the build without one. */
s32 (*g_port_press_hook)(s32 pad, u32 button, s32 down);

/* A press of the plan's: the pad's button itself, unless the window
 * takes it to press a key or a controller's button for it. */
static void port_PressPlanButton(s32 pad, u32 button, s32 down) {
    if (g_port_press_hook == NULL || !g_port_press_hook(pad, button, down)) {
        port_SetPadButton(pad, button, down);
    }
}

/* The presses that go down or come up now: those counted in frames as a
 * frame of the game begins, the others at a vertical blank. */
static void port_PressButtons(s32 in_frames, u32 now) {
    s32 i;

    for (i = 0; i < g_port_press_count; i++) {
        const PortPress *press = &g_port_presses[i];

        if (press->in_frames != in_frames || press->up_at == 0) {
            continue;
        }
        if (press->down_at == now) {
            port_PressPlanButton(press->pad, press->button, 1);
        }
        if (press->up_at == now) {
            port_PressPlanButton(press->pad, press->button, 0);
        }
    }
}

/* The presses timed from checkpoint `checkpoint` (0: the start), which
 * was reached at `blank`, g_port_frame frames of the game into the run. */
static void port_SchedulePresses(s32 checkpoint, u32 blank) {
    s32 i;

    for (i = 0; i < g_port_press_count; i++) {
        PortPress *press = &g_port_presses[i];

        if (press->checkpoint == checkpoint) {
            press->down_at =
                (press->in_frames ? g_port_frame : blank) + press->offset;
            press->up_at = press->down_at + press->hold;
            /* no frame after it: now */
            if (press->in_frames && press->offset == 0) {
                port_PressPlanButton(press->pad, press->button, 1);
            }
        }
    }
}

/* Reads a plan, line by line (port_platform.h). */
void port_LoadPlan(const char *path) {
    char *text = port_ReadTextFile(path);
    char *words[WORDS_MAX];
    char *cursor = text;
    s32 own_values = 0;
    s32 count;

    if (text == NULL) {
        port_Fail("rebushido: the plan %s cannot be read\n", path);
    }
    while ((count = port_NextLine(&cursor, words)) >= 0) {
        if (count == 0) {
            continue;
        }
        if (port_TextsEqual(words[0], "limit") && count == 2) {
            g_port_blank_limit = (u32)port_PlanNumber(words[1], "limit");
        } else if (port_TextsEqual(words[0], "gpu") && count == 2 &&
                   port_TextsEqual(words[1], "emulator")) {
            port_SetGpuLikeEmulator(1);
        } else if (port_TextsEqual(words[0], "var")) {
            /* the plan's values in place of the names' */
            if (!own_values) {
                own_values = 1;
                g_port_value_count = 0;
            }
            port_AddValue(words, count);
        } else if (port_TextsEqual(words[0], "in")) {
            port_AddPress(words, count);
        } else if (port_TextsEqual(words[0], "cp")) {
            port_AddCheckpoint(words, count);
        } else if (port_TextsEqual(words[0], "dump") && count == DUMP_WORDS) {
            u32 address = (u32)port_PlanNumber(words[2], "dump");
            u32 size = (u32)port_PlanNumber(words[DUMP_WORDS - 1], "dump");

            port_AddDump(words[1], address, size);
        } else if (port_TextsEqual(words[0], "snap") && count == DUMP_WORDS) {
            u32 address = (u32)port_PlanNumber(words[2], "snap");
            u32 size = (u32)port_PlanNumber(words[DUMP_WORDS - 1], "snap");

            port_AddSnap(words[1], address, size);
        } else if (!port_TextsEqual(words[0], "stall") &&
                   !port_TextsEqual(words[0], "alive") &&
                   !port_TextsEqual(words[0], "movie") &&
                   !port_TextsEqual(words[0], "text") &&
                   !port_TextsEqual(words[0], "frame") &&
                   !port_TextsEqual(words[0], "nocards")) {
            port_Fail("rebushido: the plan has a line of a kind not known: "
                      "%s\n",
                      words[0]);
        }
    }
    g_port_has_plan = 1;
    port_SchedulePresses(0, 0);
}

void port_SetBlankLimit(u32 blanks) { g_port_blank_limit = blanks; }

void port_SetTrace(s32 on) { g_port_trace = on; }

/* ---- a run ---- */

static s32 port_ReadValue(const PortValue *value) {
    const u8 *at = (const u8 *)ADDR(value->address);
    u32 raw = 0;
    s32 i;

    for (i = value->size - 1; i >= 0; i--) {
        raw = (raw << 8) | at[i];
    }
    if (value->is_signed && value->size < (s32)sizeof(raw) &&
        (raw >> (value->size * BITS_PER_BYTE - 1)) != 0) {
        raw |= ~0U << (value->size * BITS_PER_BYTE);
    }
    return (s32)raw;
}

/* Whether a value compares with a number as a checkpoint's test says. */
static s32 port_TestHolds(const PortTest *test) {
    s32 value = port_ReadValue(&g_port_values[test->value]);

    switch (test->compare) {
    case COMPARE_EQ:
        return value == test->with;
    case COMPARE_NE:
        return value != test->with;
    case COMPARE_LT:
        return value < test->with;
    case COMPARE_LE:
        return value <= test->with;
    case COMPARE_GT:
        return value > test->with;
    default:
        return value >= test->with;
    }
}

/* Whether the run has come to a checkpoint. One counted in frames is
 * reached only as a frame of the game begins, one that waits for a
 * function only as that is called, every other kind only at a blank. */
static s32 port_CheckpointReached(
    const PortCheckpoint *checkpoint, u32 blank, s32 event) {
    s32 i;

    if (checkpoint->kind == CHECKPOINT_FRAMES) {
        return event == EVENT_FRAME &&
               g_port_frame >=
                   g_port_last_checkpoint_frame + checkpoint->blanks;
    }
    if (checkpoint->kind == CHECKPOINT_CALL) {
        return event == EVENT_CALL &&
               g_port_awaited_calls >= checkpoint->blanks;
    }
    if (event != (checkpoint->kind == CHECKPOINT_WHEN && checkpoint->at_frame
                      ? EVENT_FRAME
                      : EVENT_BLANK)) {
        return 0;
    }
    if (checkpoint->kind == CHECKPOINT_AT) {
        return blank >= checkpoint->blanks;
    }
    if (checkpoint->kind == CHECKPOINT_AFTER) {
        return blank >= g_port_last_checkpoint_blank + checkpoint->blanks;
    }
    for (i = 0; i < checkpoint->test_count; i++) {
        if (!port_TestHolds(&checkpoint->tests[i])) {
            return 0;
        }
    }
    return 1;
}

static void port_PrintValues(void) {
    s32 i;

    for (i = 0; i < g_port_value_count; i++) {
        port_Print(" %d", port_ReadValue(&g_port_values[i]));
    }
    port_Print("\n");
}

/* A `trace` line with the values that are not what they were at the
 * last one; the first has them all. */
static void port_PrintTrace(u32 blank) {
    s32 printed = 0;
    s32 i;

    for (i = 0; i < g_port_value_count; i++) {
        PortValue *value = &g_port_values[i];
        s32 now = port_ReadValue(value);

        if (g_port_trace_started && now == value->last) {
            continue;
        }
        value->last = now;
        if (!printed) {
            port_Print("trace %u", blank);
            printed = 1;
        }
        port_Print(" %s=%d", value->name, now);
    }
    if (printed) {
        port_Print("\n");
    }
    g_port_trace_started = 1;
}

/* A file of the run's folder: the folder and the pieces of the name. */
static s32 port_CreateRunFile(const char *first, const char *second,
                              const char *third, const char *fourth) {
    char path[PATH_BYTES_MAX];
    const char *pieces[RUN_FILE_PIECES];
    u32 length = 0;
    u32 piece;

    pieces[0] = g_port_dump_folder;
    pieces[1] = first;
    pieces[2] = second;
    pieces[3] = third;
    pieces[4] = fourth;
    for (piece = 0; piece < sizeof(pieces) / sizeof(pieces[0]); piece++) {
        const char *text = pieces[piece];

        for (; *text != 0 && length + 1 < sizeof(path); text++) {
            path[length++] = *text;
        }
    }
    path[length] = 0;
    return port_HostCreate(path);
}

/* The `snap` memory as it is now, each piece to CHECKPOINT.NAME.snap:
 * the pieces `which` has a bit for, all of them if it has none. */
static void port_WriteSnaps(const char *checkpoint, u32 which) {
    s32 i;

    for (i = 0; i < g_port_snap_count; i++) {
        const PortDump *snap = &g_port_snaps[i];
        s32 file;

        if (which != 0 && (which & (1U << i)) == 0) {
            continue;
        }
        file = port_CreateRunFile(checkpoint, ".", snap->name, ".snap");
        if (file < 0 ||
            !port_HostAppend(
                file, (const void *)ADDR(snap->address), snap->size)) {
            port_Fail("rebushido: the memory `%s` at checkpoint `%s` cannot "
                      "be written\n",
                      snap->name, checkpoint);
        }
        port_HostClose(file);
    }
}

/* The picture at a checkpoint, as the emulator's script writes one: the
 * displayed rectangle's pixels to CHECKPOINT.raw, and a line that says
 * its size. */
static void port_WriteCheckpointPicture(const char *checkpoint) {
    PortDisplay display;
    s32 file = port_CreateRunFile(checkpoint, ".raw", "", "");

    port_GetDisplay(&display);
    if (file < 0 || !port_WriteDisplayPixels(file)) {
        port_Fail("rebushido: the picture at checkpoint `%s` cannot be "
                  "written\n",
                  checkpoint);
    }
    port_HostClose(file);
    port_Print(
        "shot %s %d %d %d\n", checkpoint, display.w, display.h, PICTURE_BITS);
}

/* All of video memory at a checkpoint, to CHECKPOINT.vram. */
static void port_WriteCheckpointVram(const char *checkpoint) {
    s32 file = port_CreateRunFile(checkpoint, ".vram", "", "");

    if (file < 0 || !port_WriteVram(file)) {
        port_Fail("rebushido: video memory at checkpoint `%s` cannot be "
                  "written\n",
                  checkpoint);
    }
    port_HostClose(file);
}

/* Sound memory and the sound chip's registers at a checkpoint, to
 * CHECKPOINT.spu. */
static void port_WriteCheckpointSpu(const char *checkpoint) {
    s32 file = port_CreateRunFile(checkpoint, ".spu", "", "");

    if (file < 0 || !port_WriteSpu(file)) {
        port_Fail("rebushido: sound memory at checkpoint `%s` cannot be "
                  "written\n",
                  checkpoint);
    }
    port_HostClose(file);
}

/* The checkpoints the run is at, one after another, and the run's end
 * at the last. */
static void port_TakeCheckpoints(u32 blank, s32 event) {
    while (g_port_next_checkpoint < g_port_checkpoint_count) {
        const PortCheckpoint *checkpoint =
            &g_port_checkpoints[g_port_next_checkpoint];

        if (port_CheckpointReached(checkpoint, blank, event)) {
            port_Print("cp %s %u", checkpoint->name, blank);
            port_PrintValues();
            if (checkpoint->picture) {
                port_WriteCheckpointPicture(checkpoint->name);
            }
            if (checkpoint->snap) {
                port_WriteSnaps(checkpoint->name, checkpoint->snaps);
            }
            if (checkpoint->vram) {
                port_WriteCheckpointVram(checkpoint->name);
            }
            if (checkpoint->spu) {
                port_WriteCheckpointSpu(checkpoint->name);
            }
            /* the list of calls ends here, and this one's begins */
            if (g_port_calls_file >= 0) {
                port_HostClose(g_port_calls_file);
                g_port_calls_file = -1;
            }
            if (checkpoint->calls) {
                g_port_calls_file =
                    port_CreateRunFile(checkpoint->name, ".calls", "", "");
            }
            g_port_next_checkpoint++;
            g_port_awaited_calls = 0;
            g_port_awaited_function =
                g_port_next_checkpoint < g_port_checkpoint_count
                    ? g_port_checkpoints[g_port_next_checkpoint].function
                    : 0;
            g_port_last_checkpoint_blank = blank;
            g_port_last_checkpoint_frame = g_port_frame;
            port_SchedulePresses(g_port_next_checkpoint, blank);
        } else if (event == EVENT_BLANK && checkpoint->within > 0 &&
                   blank - g_port_last_checkpoint_blank > checkpoint->within) {
            port_Print("miss %s %u\n", checkpoint->name, blank);
            port_Print("cp (failure) %u", blank);
            port_PrintValues();
            port_Quit(PORT_EXIT_FAILED);
        } else {
            break;
        }
    }
    if (g_port_has_plan && g_port_checkpoint_count > 0 &&
        g_port_next_checkpoint == g_port_checkpoint_count) {
        port_Print("done %u\n", blank);
        port_Quit(PORT_EXIT_OK);
    }
}

void port_SetShots(const char *folder, u32 every) {
    g_port_shots_folder = folder;
    g_port_shots_every = every;
}

void port_SetVramPath(const char *path) { g_port_vram_path = path; }

/* The picture as it is at this blank, to blank-NNNNNN.png of the
 * folder. */
static void port_WriteBlankPicture(u32 blank) {
    static const char stem[] = "blank-";
    static const char ending[] = ".png";
    char path[PATH_BYTES_MAX];
    u32 length = 0;
    u32 scale = 1;
    const char *text;
    s32 file;
    s32 i;

    for (text = g_port_shots_folder; *text != 0; text++) {
        if (length + sizeof(stem) + BLANK_DIGITS + sizeof(ending) <
            sizeof(path)) {
            path[length++] = *text;
        }
    }
    for (text = stem; *text != 0; text++) {
        path[length++] = *text;
    }
    for (i = 1; i < BLANK_DIGITS; i++) {
        scale *= DECIMAL;
    }
    for (i = 0; i < BLANK_DIGITS; i++) {
        path[length++] = (char)('0' + (blank / scale) % DECIMAL);
        scale /= DECIMAL;
    }
    for (text = ending; *text != 0; text++) {
        path[length++] = *text;
    }
    path[length] = 0;
    file = port_HostCreate(path);
    if (file < 0 || !port_WriteDisplayPng(file)) {
        port_Fail("rebushido: the picture %s cannot be written\n", path);
    }
    port_HostClose(file);
}

/* A vertical blank (port_platform.h): the presses due at it, the
 * picture if this is a blank it is kept at, the trace, the checkpoints
 * it reaches or gives up, and the run's end. */
void port_OnBlank(u32 blank) {
    port_PressButtons(0, blank);
    if (g_port_shots_folder != NULL && blank % g_port_shots_every == 0) {
        port_WriteBlankPicture(blank);
    }
    if (g_port_trace) {
        port_PrintTrace(blank);
    }
    port_TakeCheckpoints(blank, EVENT_BLANK);
    if (blank >= g_port_blank_limit) {
        if (g_port_next_checkpoint < g_port_checkpoint_count) {
            port_Print("miss %s %u\n",
                       g_port_checkpoints[g_port_next_checkpoint].name, blank);
            port_Print("cp (failure) %u", blank);
            port_PrintValues();
            port_Quit(PORT_EXIT_FAILED);
        }
        port_Print("limit %u\n", blank);
        port_Quit(PORT_EXIT_OK);
    }
}

/* A frame of the game begins (port_platform.h): the presses and the
 * checkpoints that are counted in its frames. */
void port_OnFrame(void) {
    g_port_frame++;
    port_PressButtons(1, g_port_frame);
    port_TakeCheckpoints(port_GetBlankCount(), EVENT_FRAME);
}

/* A function of the game begins: the compiler calls this at the top of
 * every function of the files tools/port_build.py compiles with
 * -finstrument-functions, which are the game's and Sony's and none of
 * the port's own. A line of the list of calls, if one is being written,
 * and the checkpoint that waits for this function. */
void __cyg_profile_func_enter(void *function, void *call_site) {
    (void)call_site;
    if (g_port_calls_file >= 0) {
        char line[CALL_INDENT_MAX + PATH_BYTES_MAX];
        u32 offset = 0;
        const char *name = port_FindFunction((u32)ADDR(function), &offset);
        u32 length = 0;
        s32 i;

        for (i = 0; i < g_port_call_depth && i < CALL_INDENT_MAX; i++) {
            line[length++] = ' ';
        }
        for (; name != NULL && *name != 0 && length + 2 < sizeof(line);
             name++) {
            line[length++] = *name;
        }
        line[length++] = '\n';
        port_HostAppend(g_port_calls_file, line, length);
    }
    g_port_call_depth++;
    if ((u32)ADDR(function) == g_port_awaited_function) {
        g_port_awaited_calls++;
        port_TakeCheckpoints(port_GetBlankCount(), EVENT_CALL);
    }
}

/* ... and ends. */
void __cyg_profile_func_exit(void *function, void *call_site) {
    (void)function;
    (void)call_site;
    g_port_call_depth--;
}

/* ---- the end ---- */

void port_SetDumpFolder(const char *folder) { g_port_dump_folder = folder; }

/* Each dump to its file: the folder, the dump's name, ".bin"
 * (port_platform.h). */
void port_WriteDumps(void) {
    s32 i;

    if (g_port_vram_path != NULL) {
        s32 file = port_HostCreate(g_port_vram_path);

        if (file < 0 || !port_WriteVram(file)) {
            port_Log("rebushido: video memory cannot be written to %s\n",
                     g_port_vram_path);
        }
        if (file >= 0) {
            port_HostClose(file);
        }
    }

    for (i = 0; i < g_port_dump_count; i++) {
        const PortDump *dump = &g_port_dumps[i];
        s32 file = port_CreateRunFile(dump->name, ".bin", "", "");

        if (file < 0 ||
            !port_HostAppend(
                file, (const void *)ADDR(dump->address), dump->size)) {
            port_Log(
                "rebushido: the dump `%s` cannot be written\n", dump->name);
        }
        if (file >= 0) {
            port_HostClose(file);
        }
    }
}
