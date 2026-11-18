/* The port's one input point: a frame's pads, and the keys mapped onto them. */
/* What the game reads of the pads in the port comes from one place,
 * port_TakeInput (src/port/pad.c; docs/NETPLAY.md, "Input"): the input
 * the driver read at the last vertical blank, which a hook may replace,
 * written into the game's packets as the game reads them. Held here to
 * what pad_Update (src/main/pad.c) then finds, with the PC's keys and
 * controllers mapped onto the pads by the settings file
 * (src/port/window/keymap.c), all made up in this file. */
#include "common.h"
#include "src/port/window/keymap.c"
#include "src/port/pad.c"
#include "src/main/pad.c"
#include "runtime/runtime.h"

/* The host's turn at a frame's boundary, and whether a step is run again
 * (src/port/step.c): no host here, and every step is the first. */
void port_EndStep(void) {}

s32 g_port_rerun;

/* ---- what pad_Update reaches ---- */

s16 g_sys_screen;
s16 g_fight_mode;

/* The camera's pad control and the link: nothing here. */
void camera_ReadPad(s32 *held) { (void)held; }

void link_ExchangePad(PadState *pad) { (void)pad; }

/* ---- names of keys and buttons, as a window's library would have them
 * ---- */

typedef struct {
    s32 source;
    const char *name;
    s32 code;
} TestName;

static const TestName g_test_names[] = {
    {PORT_SOURCE_KEY, "Up", 82},
    {PORT_SOURCE_KEY, "Down", 81},
    {PORT_SOURCE_KEY, "Left", 80},
    {PORT_SOURCE_KEY, "Right", 79},
    {PORT_SOURCE_KEY, "Z", 29},
    {PORT_SOURCE_KEY, "X", 27},
    {PORT_SOURCE_KEY, "A", 4},
    {PORT_SOURCE_KEY, "S", 22},
    {PORT_SOURCE_KEY, "Q", 20},
    {PORT_SOURCE_KEY, "W", 26},
    {PORT_SOURCE_KEY, "1", 30},
    {PORT_SOURCE_KEY, "2", 31},
    {PORT_SOURCE_KEY, "Return", 40},
    {PORT_SOURCE_KEY, "Right Shift", 229},
    {PORT_SOURCE_KEY, "I", 12},
    {PORT_SOURCE_KEY, "K", 14},
    {PORT_SOURCE_KEY, "J", 13},
    {PORT_SOURCE_KEY, "L", 15},
    {PORT_SOURCE_KEY, "G", 10},
    {PORT_SOURCE_KEY, "H", 11},
    {PORT_SOURCE_KEY, "T", 23},
    {PORT_SOURCE_KEY, "Y", 28},
    {PORT_SOURCE_KEY, "R", 21},
    {PORT_SOURCE_KEY, "U", 24},
    {PORT_SOURCE_KEY, "5", 34},
    {PORT_SOURCE_KEY, "6", 35},
    {PORT_SOURCE_KEY, "P", 19},
    {PORT_SOURCE_KEY, "O", 18},
    {PORT_SOURCE_KEY, "Space", 44},
    {PORT_SOURCE_BUTTON, "a", 0},
    {PORT_SOURCE_BUTTON, "b", 1},
    {PORT_SOURCE_BUTTON, "x", 2},
    {PORT_SOURCE_BUTTON, "y", 3},
    {PORT_SOURCE_BUTTON, "back", 4},
    {PORT_SOURCE_BUTTON, "start", 6},
    {PORT_SOURCE_BUTTON, "leftshoulder", 9},
    {PORT_SOURCE_BUTTON, "rightshoulder", 10},
    {PORT_SOURCE_BUTTON, "dpup", 11},
    {PORT_SOURCE_BUTTON, "dpdown", 12},
    {PORT_SOURCE_BUTTON, "dpleft", 13},
    {PORT_SOURCE_BUTTON, "dpright", 14},
    {PORT_SOURCE_AXIS, "leftx", 0},
    {PORT_SOURCE_AXIS, "lefty", 1},
    {PORT_SOURCE_AXIS, "lefttrigger", 4},
    {PORT_SOURCE_AXIS, "righttrigger", 5},
};

#define KEY_Z 29
#define KEY_G 10
#define KEY_SPACE 44
#define BUTTON_A 0
#define AXIS_LEFTX 0

static s32 test_LookUp(s32 source, const char *name) {
    u32 i;

    for (i = 0; i < sizeof(g_test_names) / sizeof(g_test_names[0]); i++) {
        if (g_test_names[i].source == source &&
            test_StringsEqual(g_test_names[i].name, name)) {
            return g_test_names[i].code;
        }
    }
    return -1;
}

static s32 g_test_complaints;

static void test_Complain(s32 line, const char *why, const char *text) {
    (void)line;
    (void)why;
    (void)text;
    g_test_complaints++;
}

static PortSettings g_test_settings;

static void test_ReadText(const char *text) {
    static char copy[1024];
    u32 i;

    for (i = 0; text[i] != 0; i++) {
        copy[i] = text[i];
    }
    copy[i] = 0;
    g_test_complaints = 0;
    port_ReadSettings(&g_test_settings, copy, test_LookUp, test_Complain);
}

/* The window's side of the pads, as sdl_window.c has it: what the
 * settings say the keys and controllers hold. */
static u32 test_DeviceButtons(s32 pad) {
    return port_GetHeldButtons(&g_test_settings, pad);
}

/* ---- a frame ---- */

/* The game's buffers given to the driver, and the driver started, with
 * nothing held: as main.c's start leaves them. */
static void test_StartPads(void) {
    s32 pad;

    g_port_device_hook = NULL;
    g_port_take_hook = NULL;
    for (pad = 0; pad < PORT_PADS; pad++) {
        port_SetPadButton(pad, 0xFFFF, 0);
    }
    InitPAD((char *)g_pad_packets[0], PAD_PACKET_SIZE, (char *)g_pad_packets[1],
            PAD_PACKET_SIZE);
    StartPAD();
    pad_Startup();
    g_sys_screen = SCREEN_MENU_INIT;
    g_fight_mode = FIGHT_MODE_VS;
}

/* A frame: a vertical blank, then the game's read. */
static void test_Frame(void) {
    port_FillPads();
    pad_Update(0);
}

/* A replay's input, given in place of the pads' own. */
static PortInput g_test_replay;

static void test_Replay(PortInput *input) { *input = g_test_replay; }

/* ---- the record ---- */

static void test_Record(void) {
    PortInput kept;
    u32 count;

    /* a value of a fixed size: sixteen buttons a pad and whether it is
     * there */
    CHECK_EQ(sizeof(PortInput), 6);

    /* before the driver's first blank, nothing has answered */
    test_StartPads();
    count = g_port_input_count;
    port_SetPadButton(0, PADRdown, 1);
    pad_Update(0);
    CHECK_EQ(g_port_input_count, count + 1);
    CHECK_EQ(g_port_input.present[0], 0);
    CHECK_EQ(g_port_input.present[1], 0);
    CHECK_EQ(((u8 *)g_pad_packets[0])[0], 0xFF);
    CHECK_EQ(g_pad_state.valid[0], 0);
    CHECK_EQ(g_pad_state.held, 0);

    /* at the blank the driver reads what is held, and the game finds it
     * at its read: the packet as a digital pad's, a 0 bit a button */
    test_Frame();
    CHECK_EQ(g_port_input.present[0], 1);
    CHECK_EQ(g_port_input.present[1], 1);
    CHECK_EQ(g_port_input.buttons[0], PADRdown);
    CHECK_EQ(g_port_input.buttons[1], 0);
    CHECK_EQ(((u8 *)g_pad_packets[0])[0], 0x00);
    CHECK_EQ(((u8 *)g_pad_packets[0])[1], 0x41);
    CHECK_EQ(((u8 *)g_pad_packets[0])[2], (u8) ~(PADRdown >> 8));
    CHECK_EQ(((u8 *)g_pad_packets[0])[3], (u8)~PADRdown);
    CHECK_EQ(g_pad_state.valid[0], 1);
    CHECK_EQ(g_pad_state.held, PADRdown);
    CHECK_EQ(g_pad_state.pressed, PADRdown);

    /* a press between the blank and the read waits for the next blank, as
     * on the console */
    port_SetPadButton(1, PADstart, 1);
    pad_Update(0);
    CHECK_EQ(g_pad_state.held, PADRdown);
    CHECK_EQ(g_pad_state.pressed, 0);
    test_Frame();
    CHECK_EQ(g_port_input.buttons[1], PADstart);
    CHECK_EQ(g_pad_state.held, PADRdown | PAD_OF(PADstart, 1));
    CHECK_EQ(g_pad_state.pressed, PAD_OF(PADstart, 1));
    port_SetPadButton(0, PADRdown, 0);
    test_Frame();
    CHECK_EQ(g_pad_state.held, PAD_OF(PADstart, 1));
    CHECK_EQ(g_pad_state.released, PADRdown);

    /* a stopped driver reads nothing more: the game finds the last */
    StopPAD();
    port_SetPadButton(0, PADL1, 1);
    test_Frame();
    CHECK_EQ(g_pad_state.held, PAD_OF(PADstart, 1));
    StartPAD();
    test_Frame();
    CHECK_EQ(g_pad_state.held, PADL1 | PAD_OF(PADstart, 1));

    /* the record is the whole of what the game reads: kept, and given
     * again in place of the pads' own, it reads the same */
    kept = g_port_input;
    port_SetPadButton(0, 0xFFFF, 0);
    port_SetPadButton(1, 0xFFFF, 0);
    pad_ResetState();
    g_test_replay = kept;
    g_port_take_hook = test_Replay;
    test_Frame();
    CHECK(test_BytesEqual(&g_port_input, &kept, sizeof(kept)));
    CHECK_EQ(g_pad_state.held, PADL1 | PAD_OF(PADstart, 1));
    CHECK_EQ(g_pad_state.pressed, PADL1 | PAD_OF(PADstart, 1));
    g_port_take_hook = NULL;
}

/* A pad that is not there counts as Start in a fight, from the frame after
 * the one that found it gone (pad.c): an input can say so, as a network's
 * must not. */
static void test_Unplugged(void) {
    test_StartPads();
    test_Frame();
    g_sys_screen = SCREEN_FIGHT_UPDATE;
    g_fight_mode = FIGHT_MODE_VSCPU;
    g_test_replay = g_port_input;
    g_test_replay.present[0] = 0;
    g_port_take_hook = test_Replay;
    test_Frame();
    CHECK_EQ(g_pad_state.valid[0], 0);
    CHECK_EQ(g_pad_state.held, 0);
    test_Frame();
    CHECK_EQ(g_pad_state.held, PAD_ANY(PADstart));
    /* on a frame that is not shown it does not */
    port_FillPads();
    pad_Update(1);
    CHECK_EQ(g_pad_state.held, 0);
    /* the second pad matters only in the modes of two players */
    g_test_replay.present[0] = 1;
    g_test_replay.present[1] = 0;
    test_Frame();
    test_Frame();
    CHECK_EQ(g_pad_state.held, 0);
    g_fight_mode = FIGHT_MODE_VS;
    test_Frame();
    CHECK_EQ(g_pad_state.held, PAD_ANY(PADstart));
    g_port_take_hook = NULL;
}

/* ---- the keys and buttons onto the pads ---- */

static void test_Mapping(void) {
    PortInput now;

    test_StartPads();
    port_SetDefaultSettings(&g_test_settings, test_LookUp);
    g_port_device_hook = test_DeviceButtons;

    /* the layout built in: Z is player one's Cross, G player two's, and
     * the first controller's A is player one's Cross too */
    port_ApplyInput(&g_test_settings, PORT_SOURCE_KEY, 0, KEY_Z, 1);
    port_ApplyInput(&g_test_settings, PORT_SOURCE_KEY, 0, KEY_G, 1);
    test_Frame();
    CHECK_EQ(g_port_input.buttons[0], PADRdown);
    CHECK_EQ(g_port_input.buttons[1], PADRdown);
    CHECK_EQ(g_pad_state.held, PAD_ANY(PADRdown));
    port_ApplyInput(&g_test_settings, PORT_SOURCE_KEY, 0, KEY_Z, 0);
    port_ApplyInput(&g_test_settings, PORT_SOURCE_KEY, 0, KEY_G, 0);
    port_ApplyInput(&g_test_settings, PORT_SOURCE_BUTTON, 0, BUTTON_A, 1);
    /* a stick pushed past the dead zone is the direction's button */
    port_ApplyInput(&g_test_settings, PORT_SOURCE_AXIS, 1, AXIS_LEFTX, -20000);
    test_Frame();
    CHECK_EQ(g_pad_state.held, PADRdown | PAD_OF(PADLleft, 1));
    port_ApplyInput(&g_test_settings, PORT_SOURCE_BUTTON, 0, BUTTON_A, 0);
    port_ApplyInput(&g_test_settings, PORT_SOURCE_AXIS, 1, AXIS_LEFTX, -100);
    test_Frame();
    CHECK_EQ(g_pad_state.held, 0);

    /* a script's buttons and the PC's together */
    port_SetPadButton(1, PADR1, 1);
    port_ApplyInput(&g_test_settings, PORT_SOURCE_KEY, 0, KEY_Z, 1);
    port_SampleInput(&now);
    CHECK_EQ(now.buttons[0], PADRdown);
    CHECK_EQ(now.buttons[1], PADR1);
    port_SetPadButton(1, PADR1, 0);
    port_ApplyInput(&g_test_settings, PORT_SOURCE_KEY, 0, KEY_Z, 0);

    /* remapped: Space is player one's Cross, and Z nothing */
    port_SetDefaultSettings(&g_test_settings, test_LookUp);
    test_ReadText("p1.cross = key Space\n");
    CHECK_EQ(g_test_complaints, 0);
    port_ApplyInput(&g_test_settings, PORT_SOURCE_KEY, 0, KEY_Z, 1);
    test_Frame();
    CHECK_EQ(g_pad_state.held, 0);
    port_ApplyInput(&g_test_settings, PORT_SOURCE_KEY, 0, KEY_SPACE, 1);
    test_Frame();
    CHECK_EQ(g_pad_state.held, PADRdown);
    port_ApplyInput(&g_test_settings, PORT_SOURCE_KEY, 0, KEY_SPACE, 0);
    port_ApplyInput(&g_test_settings, PORT_SOURCE_KEY, 0, KEY_Z, 0);

    /* per player: player two's controller's A is Start, and no longer
     * Cross; player one's A is still Cross */
    test_ReadText("p2.start = pad a\n");
    CHECK_EQ(g_test_complaints, 0);
    port_ApplyInput(&g_test_settings, PORT_SOURCE_BUTTON, 1, BUTTON_A, 1);
    port_ApplyInput(&g_test_settings, PORT_SOURCE_BUTTON, 0, BUTTON_A, 1);
    test_Frame();
    CHECK_EQ(g_pad_state.held, PADRdown | PAD_OF(PADstart, 1));
    port_ApplyInput(&g_test_settings, PORT_SOURCE_BUTTON, 1, BUTTON_A, 0);
    port_ApplyInput(&g_test_settings, PORT_SOURCE_BUTTON, 0, BUTTON_A, 0);

    /* one key for several buttons at once */
    CHECK_EQ(port_FindPadButtons("l1+r1"), PADL1 | PADR1);
    CHECK_EQ(port_FindPadButtons("square"), PADRleft);
    CHECK_EQ(port_FindPadButtons("l1+"), 0);
    CHECK_EQ(port_FindPadButtons("l1+l3"), 0);
    CHECK_EQ(port_FindPadButtons("+r1"), 0);
    port_SetDefaultSettings(&g_test_settings, test_LookUp);
    test_ReadText("p1.l1+r1 = key Space\np2.l1+bad = key Z\n");
    CHECK_EQ(g_test_complaints, 1);
    port_ApplyInput(&g_test_settings, PORT_SOURCE_KEY, 0, KEY_SPACE, 1);
    test_Frame();
    CHECK_EQ(g_pad_state.held, PADL1 | PADR1);
    port_ApplyInput(&g_test_settings, PORT_SOURCE_KEY, 0, KEY_SPACE, 0);
    test_Frame();
    CHECK_EQ(g_pad_state.held, 0);
    /* ... and L1 and R1 keep their own keys, Q and W */
    CHECK_EQ(port_FindBinding(&g_test_settings, 1, 0, PADL1)->code, 20);
    CHECK_EQ(port_FindBinding(&g_test_settings, 1, 0, PADR1)->code, 26);

    g_port_device_hook = NULL;
}

/* The layouts a file starts from (`controls = NAME`). */
static void test_Layouts(void) {
    s32 count;

    port_SetDefaultSettings(&g_test_settings, test_LookUp);
    count = g_test_settings.count;
    CHECK_EQ(count, 2 * (14 + 18));
    CHECK_EQ(g_test_settings.layout, 0);
    CHECK(test_StringsEqual(g_port_layouts[0].name, "original"));
    /* naming the layout built in changes nothing */
    test_ReadText("controls = original\n");
    CHECK_EQ(g_test_complaints, 0);
    CHECK_EQ(g_test_settings.count, count);
    CHECK_EQ(port_FindBinding(&g_test_settings, 1, 0, PADRdown)->code, KEY_Z);
    /* it comes before the file's own lines, which are read over it */
    test_ReadText("controls = Original\np1.cross = key Space\n");
    CHECK_EQ(g_test_complaints, 0);
    CHECK_EQ(
        port_FindBinding(&g_test_settings, 1, 0, PADRdown)->code, KEY_SPACE);
    test_ReadText("controls = original\n");
    CHECK_EQ(g_test_complaints, 1);
    CHECK_EQ(
        port_FindBinding(&g_test_settings, 1, 0, PADRdown)->code, KEY_SPACE);
    port_SetDefaultSettings(&g_test_settings, test_LookUp);
    test_ReadText("controls = modern\n");
    CHECK_EQ(g_test_complaints, 1);
    CHECK_EQ(g_test_settings.count, count);
}

/* The keys read again just before the game takes its input
 * (port_ReadDevicesNow): what the PC holds now goes in, and the plan's
 * buttons stay as the blank found them, so that a plan's press made
 * after the blank still reaches the game a frame later. */
static void test_ReadLate(void) {
    test_StartPads();
    port_SetDefaultSettings(&g_test_settings, test_LookUp);
    g_port_device_hook = test_DeviceButtons;

    /* the blank's read: nothing held */
    port_FillPads();
    /* after it, a key goes down and a plan's button too */
    port_ApplyInput(&g_test_settings, PORT_SOURCE_KEY, 0, KEY_Z, 1);
    port_SetPadButton(1, PADstart, 1);
    port_ReadDevicesNow();
    pad_Update(0);
    /* the key is taken now; the plan's Start waits for the next blank */
    CHECK_EQ(g_port_input.buttons[0], PADRdown);
    CHECK_EQ(g_port_input.buttons[1], 0);
    CHECK_EQ(g_pad_state.pressed, PADRdown);
    port_FillPads();
    port_ReadDevicesNow();
    pad_Update(0);
    CHECK_EQ(g_port_input.buttons[1], PADstart);
    CHECK_EQ(g_pad_state.held, PADRdown | PAD_OF(PADstart, 1));
    /* a key let go after the blank is let go now; the plan's button held
     * at the blank is still held */
    port_ApplyInput(&g_test_settings, PORT_SOURCE_KEY, 0, KEY_Z, 0);
    port_ReadDevicesNow();
    pad_Update(0);
    CHECK_EQ(g_port_input.buttons[0], 0);
    CHECK_EQ(g_port_input.buttons[1], PADstart);
    /* with no window it changes nothing */
    port_SetPadButton(1, PADstart, 0);
    g_port_device_hook = NULL;
    port_FillPads();
    port_SetPadButton(0, PADRdown, 1);
    port_ReadDevicesNow();
    pad_Update(0);
    CHECK_EQ(g_port_input.buttons[0], 0);
    port_SetPadButton(0, PADRdown, 0);
}

/* The movie player's reader, which is given the pads as they are now. */
static void test_MoviePads(void) {
    s32 word;

    test_StartPads();
    PAD_init(0, &word);
    CHECK_EQ(word, -1);
    port_SetPadButton(0, PADRdown, 1);
    port_SetPadButton(1, PADstart, 1);
    PAD_dr();
    CHECK_EQ(word, (s32) ~(PADRdown | ((u32)PADstart << 16)));
    port_SetPadButton(0, PADRdown, 0);
    port_SetPadButton(1, PADstart, 0);
    PAD_dr();
    CHECK_EQ(word, -1);
}

void test_Main(void) {
    test_Record();
    test_Unplugged();
    test_Mapping();
    test_Layouts();
    test_ReadLate();
    test_MoviePads();
}
