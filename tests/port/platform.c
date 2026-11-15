/* The port's platform layer: its clock, pads, events, primitives, printf. */
/* What the native port puts where the console and Sony's libraries are
 * (src/port/), as far as it is more than a stub: each thing held to what
 * the console does, by cases made up here. */
#include "common.h"
#include <psxsdk/kernel.h>
#include <psxsdk/libapi.h>
#include <psxsdk/libetc.h>
#include <psxsdk/libgpu.h>
#include "src/port/clock.c"
#include "src/port/pad.c"
#include "src/port/event.c"
#include "src/port/gpu.c"
#include "src/port/text.c"
#include "src/port/stub.c"
#include "runtime/runtime.h"

/* ---- what those files want of the rest of the port ---- */

static u32 g_seen_blanks;
static u32 g_seen_last_blank;
static s32 g_seen_out_of_order;

void port_OnBlank(u32 blank) {
    g_seen_blanks++;
    g_seen_out_of_order += blank != g_seen_last_blank + 1;
    g_seen_last_blank = blank;
}

static u32 g_seen_frames;

void port_OnFrame(void) { g_seen_frames++; }

void port_HostWrite(s32 stream, const char *bytes, u32 count) {}

/* The sound's turn at a vertical blank (sound.c), counted. */
static u32 g_seen_sound_blanks;

void port_RunSoundBlank(u32 blank) {
    (void)blank;
    g_seen_sound_blanks++;
}

void port_Quit(s32 code) {
    test_Count(0, __FILE__, __LINE__, "the port stopped itself (%d)", code);
    for (;;) {
    }
}

/* ---- time ---- */

/* The wait at the end of main's frame (main.c), which calls rand() for as
 * long as it lasts: how many times it went round. */
static s32 test_FrameWait(s32 period) {
    s32 calls = 0;

    while (GetRCnt(RCntCNT1) < ((period - 1) << 8) + 128) {
        calls++;
    }
    VSync(1);
    VSync(0);
    ResetRCnt(RCntCNT1);
    return calls;
}

static void test_Clock(void) {
    u32 before;
    s32 i;

    CHECK_EQ(port_GetBlankCount(), 0);
    CHECK_EQ(VSync(-1), 0);
    SetRCnt(RCntCNT1, -1, RCntMdNOINTR);
    CHECK_EQ(StartRCnt(RCntCNT1), 1);
    /* VSync(0): the next blank, every time */
    for (i = 1; i <= 5; i++) {
        VSync(0);
        CHECK_EQ(port_GetBlankCount(), i);
    }
    CHECK_EQ(VSync(-1), 5);
    /* VSync(1) does not wait */
    CHECK_EQ(VSync(1), 0);
    CHECK_EQ(port_GetBlankCount(), 5);
    /* VSync(n): n blanks after the last wait */
    VSync(2);
    CHECK_EQ(port_GetBlankCount(), 7);
    VSync(4);
    CHECK_EQ(port_GetBlankCount(), 11);
    /* ... and at least one, when more than n have gone by already */
    ResetRCnt(RCntCNT1);
    while (GetRCnt(RCntCNT1) < 5 * PORT_LINES_PER_BLANK + 10) {
    }
    CHECK_EQ(port_GetBlankCount(), 16);
    CHECK_EQ(VSync(1), 5 * PORT_LINES_PER_BLANK + 11);
    VSync(2);
    CHECK_EQ(port_GetBlankCount(), 17);
    /* it returns the scan lines between the last wait's end and this
     * call: none, when one wait follows another */
    CHECK_EQ(VSync(0), 0);
    CHECK_EQ(VSync(0), 0);
    GetRCnt(RCntCNT1);
    GetRCnt(RCntCNT1);
    CHECK_EQ(VSync(0), 2);

    /* the counter of scan lines: one more each time it is asked */
    ResetRCnt(RCntCNT1);
    CHECK_EQ(GetRCnt(RCntCNT1), 0);
    CHECK_EQ(GetRCnt(RCntCNT1), 1);
    CHECK_EQ(GetRCnt(RCntCNT1), 2);
    ResetRCnt(RCntCNT1);
    CHECK_EQ(GetRCnt(RCntCNT1), 0);
    VSync(0);
    ResetRCnt(RCntCNT1);

    /* the frame's wait: 384 calls and two blanks for a frame of two
     * fields, 128 and one for a frame of one, every time */
    for (i = 0; i < 6; i++) {
        before = port_GetBlankCount();
        CHECK_EQ(test_FrameWait(2), 384);
        CHECK_EQ(port_GetBlankCount() - before, 2);
    }
    for (i = 0; i < 6; i++) {
        before = port_GetBlankCount();
        CHECK_EQ(test_FrameWait(1), 128);
        CHECK_EQ(port_GetBlankCount() - before, 1);
    }
    /* a frame that waited inside, as a load does: no more waiting, on to
     * the next blank */
    VSync(2);
    VSync(2);
    before = port_GetBlankCount();
    CHECK_EQ(test_FrameWait(2), 0);
    CHECK_EQ(port_GetBlankCount() - before, 1);

    /* every blank was told to the rest of the port, once, in order */
    CHECK_EQ(g_seen_blanks, port_GetBlankCount());
    /* and the sound had its turn at every one */
    CHECK_EQ(g_seen_sound_blanks, port_GetBlankCount());
    /* the counter of vertical blanks, which Sony's sound library sets up
     * for its tick, is taken and is not the game's frame */
    before = port_GetBlankCount();
    CHECK_EQ(SetRCnt(RCntCNT3, 1, 0x1000), 1);
    CHECK_EQ(ResetRCnt(RCntCNT3), 1);
    CHECK_EQ(StartRCnt(RCntCNT3), 1);
    CHECK_EQ(port_GetBlankCount(), before);
    /* a frame of the game for each reset of the scan lines' counter */
    CHECK_EQ(g_seen_frames, 17);
    CHECK_EQ(g_seen_out_of_order, 0);
    /* a counter that is not the scan lines' is not the port's */
    CHECK_EQ(GetRCnt(RCntCNT2), 0);
    CHECK_EQ(ResetRCnt(RCntCNT0), 0);
}

/* ---- the pads ---- */

static void test_Pads(void) {
    static u8 packets[2][36];

    packets[0][0] = packets[1][0] = 0x55;
    CHECK_EQ(InitPAD((char *)packets[0], 34, (char *)packets[1], 34), 1);
    /* nobody has asked the pads yet */
    CHECK_EQ(packets[0][0], 0xFF);
    CHECK_EQ(packets[1][0], 0xFF);
    VSync(0);
    CHECK_EQ(packets[0][0], 0xFF); /* the driver is not started */
    StartPAD();
    VSync(0);
    /* two digital pads, nothing held */
    CHECK_EQ(packets[0][0], 0x00);
    CHECK_EQ(packets[0][1], 0x41);
    CHECK_EQ(packets[0][2], 0xFF);
    CHECK_EQ(packets[0][3], 0xFF);
    CHECK_EQ(packets[1][0], 0x00);
    CHECK_EQ(packets[1][1], 0x41);
    /* a press is seen from the next blank, as the game reads it (pad.c) */
    port_SetPadButton(0, PADstart, 1);
    port_SetPadButton(1, PADRdown | PADLleft, 1);
    CHECK_EQ(packets[0][2], 0xFF);
    VSync(0);
    CHECK_EQ((u16) ~((packets[0][2] << 8) | packets[0][3]), PADstart);
    CHECK_EQ(
        (u16) ~((packets[1][2] << 8) | packets[1][3]), PADRdown | PADLleft);
    port_SetPadButton(0, PADstart, 0);
    port_SetPadButton(1, PADLleft, 0);
    port_SetPadButton(2, PADstart, 1); /* no such pad */
    VSync(0);
    CHECK_EQ((u16) ~((packets[0][2] << 8) | packets[0][3]), 0);
    CHECK_EQ((u16) ~((packets[1][2] << 8) | packets[1][3]), PADRdown);
    CHECK_EQ(packets[0][4], 0); /* four bytes, no more */
    StopPAD();
    port_SetPadButton(1, PADRdown, 0);
    VSync(0);
    CHECK_EQ((u16) ~((packets[1][2] << 8) | packets[1][3]), PADRdown);
}

/* ---- events, and the card that is not there ---- */

static void test_Events(void) {
    s32 timeout = OpenEvent(SwCARD, EvSpTIMOUT, EvMdNOINTR, 0);
    s32 done = OpenEvent(SwCARD, EvSpIOE, EvMdNOINTR, 0);
    s32 hw_timeout = OpenEvent(HwCARD, EvSpTIMOUT, EvMdNOINTR, 0);
    s32 closed;

    CHECK(timeout != -1 && done != -1 && hw_timeout != -1);
    CHECK(timeout != done && done != hw_timeout);
    CHECK_EQ(TestEvent(timeout), 0);
    /* an event that is not enabled is not delivered */
    port_DeliverEvent(SwCARD, EvSpTIMOUT);
    CHECK_EQ(TestEvent(timeout), 0);
    CHECK_EQ(EnableEvent(timeout), 1);
    CHECK_EQ(EnableEvent(done), 1);
    CHECK_EQ(EnableEvent(hw_timeout), 1);
    port_DeliverEvent(SwCARD, EvSpTIMOUT);
    /* only its own class and kind; and asking clears it */
    CHECK_EQ(TestEvent(done), 0);
    CHECK_EQ(TestEvent(hw_timeout), 0);
    CHECK_EQ(TestEvent(timeout), 1);
    CHECK_EQ(TestEvent(timeout), 0);
    /* no card: nothing answers a question about one */
    CHECK_EQ(_card_info(0), 1);
    CHECK_EQ(TestEvent(done), 0);
    CHECK_EQ(TestEvent(timeout), 1);
    CHECK_EQ(TestEvent(hw_timeout), 0);
    CHECK_EQ(_card_clear(0), 1);
    CHECK_EQ(TestEvent(hw_timeout), 1);
    CHECK_EQ(_card_load(0x10), 1);
    CHECK_EQ(TestEvent(timeout), 1);
    /* a card's file cannot be opened; the serial port can */
    CHECK_EQ(open("bu00:BASLUS-00663BB2", 1), -1);
    CHECK(open("sio:", 1) != -1);
    CHECK(firstfile("bu00:*", NULL) == NULL);
    /* a closed event's place is used again, and its number means nothing */
    closed = done;
    CHECK_EQ(CloseEvent(done), 1);
    CHECK_EQ(CloseEvent(done), 0);
    CHECK_EQ(TestEvent(closed), 0);
    CHECK_EQ(EnableEvent(12345), 0);
    CHECK_EQ(OpenEvent(HwCARD, EvSpIOE, EvMdNOINTR, 0), closed);
}

/* ---- what libgpu fills in ---- */

static void test_Primitives(void) {
    u32 ot[6];
    DR_MODE mode;
    DR_AREA area;
    DR_OFFSET offset;
    RECT rect;
    RECT window;
    u16 ofs[2];
    s32 i;

    /* an ordering table: each entry linked to the one before, the first
     * ending the list, and nothing past the last touched */
    ot[5] = 0x12345678;
    CHECK(ClearOTagR(ot, 5) == ot);
    CHECK_EQ(ot[0], 0x00FFFFFF);
    for (i = 1; i < 5; i++) {
        CHECK_EQ(ot[i], (u32)&ot[i - 1] & 0x00FFFFFF);
    }
    CHECK_EQ(ot[5], 0x12345678);

    /* the mode: the texture page's nine bits and its two flags, the
     * dither and drawing on the display, and no texture window */
    mode.tag = 0x00ABCDEF;
    SetDrawMode(&mode, 0, 0, 0xFFFF, NULL);
    CHECK_EQ(mode.tag, 0x02ABCDEF); /* two words; the link is the caller's */
    CHECK_EQ(mode.code[0], 0xE10009FF);
    CHECK_EQ(mode.code[1], 0);
    SetDrawMode(&mode, 1, 1, getTPage(1, 2, 320, 256), NULL);
    CHECK_EQ(mode.code[0],
             0xE1000000 | 0x400 | 0x200 | (1 << 7) | (2 << 5) | (1 << 4) | 5);
    window.x = 32, window.y = 64, window.w = 16, window.h = 8;
    SetDrawMode(&mode, 0, 0, 0, &window);
    CHECK_EQ(mode.code[1], 0xE2000000 | (8 << 15) | (4 << 10) | (31 << 5) | 30);

    /* the area: its two corners, kept inside video memory */
    rect.x = 0, rect.y = 240, rect.w = 640, rect.h = 240;
    SetDrawArea(&area, &rect);
    CHECK_EQ(area.code[0], 0xE3000000 | (240 << 10) | 0);
    CHECK_EQ(area.code[1], 0xE4000000 | (479 << 10) | 639);
    rect.x = -5, rect.y = 500, rect.w = 2000, rect.h = 100;
    SetDrawArea(&area, &rect);
    CHECK_EQ(area.code[0], 0xE3000000 | (500 << 10) | 0);
    CHECK_EQ(area.code[1], 0xE4000000 | (511 << 10) | 1023);

    /* the offset: eleven bits each, signed */
    ofs[0] = 320, ofs[1] = 120;
    SetDrawOffset(&offset, ofs);
    CHECK_EQ(offset.code[0], 0xE5000000 | (120 << 11) | 320);
    ofs[0] = (u16)-1, ofs[1] = (u16)-2;
    SetDrawOffset(&offset, ofs);
    CHECK_EQ(offset.code[0], 0xE5000000 | (0x7FE << 11) | 0x7FF);
    CHECK_EQ(offset.code[1], 0);
}

/* ---- printf's work ---- */

static void test_Format(const char *want, const char *format, ...) {
    char out[64];
    PortArgs args;
    u32 length;
    u32 i;

    for (i = 0; i < sizeof(out); i++) {
        out[i] = 0x7F;
    }
    PORT_ARGS_START(args, format);
    length = port_FormatText(out, 40, format, args);
    PORT_ARGS_END(args);
    test_Count(test_StringsEqual(out, want), __FILE__, __LINE__,
               "`%s` gave `%s`, not `%s`", format, out, want);
    CHECK_EQ(length, port_TextLength(want));
    CHECK_EQ(out[40], 0x7F); /* its room and no more */
}

static void test_Text(void) {
    s32 number = 0;

    /* the game's own formats (memcard.c, main.c, options.c) */
    test_Format("bu00:*", "bu%1d%1d:*", 0, 0);
    test_Format("bu10:BASLUS", "bu%1d%1d:%s", 1, 0, "BASLUS");
    test_Format("LIMIT:8010db00\n", "LIMIT:%08x\n", 0x8010DB00);
    test_Format("< Original >", "< %s >", "Original");
    test_Format("vab id:-1 mistake\n", "vab id:%d mistake\n", -1);
    /* and the rest of what it takes */
    test_Format("-2147483648 4294967295", "%d %u", (s32)0x80000000, 0xFFFFFFFF);
    test_Format("  42|42  |00042|-0042", "%4d|%-4d|%05d|%05d", 42, 42, 42, -42);
    test_Format("FF ff 17 %", "%X %x %o %%", 255, 255, 15);
    test_Format("[  ab][ab  ][a]", "[%4s][%-4s][%.1s]", "ab", "ab", "ab");
    test_Format("x 007 y", "%c %.3d %c", 'x', 7, 'y');
    test_Format("   7", "%*d", 4, 7);
    test_Format("(null)", "%s", (char *)0);
    test_Format("100", "%ld", 100);
    /* cut where the room ends, with its ending 0 */
    test_Format("0123456789012345678901234567890123456789" + 1, "%s",
                "123456789012345678901234567890123456789abcdef");

    CHECK(port_ParseNumber("0", &number) && number == 0);
    CHECK(port_ParseNumber("-17", &number) && number == -17);
    CHECK(port_ParseNumber("0x8010DB00", &number) && (u32)number == 0x8010DB00);
    CHECK(!port_ParseNumber("", &number));
    CHECK(!port_ParseNumber("12a", &number));
    CHECK(!port_ParseNumber("0x", &number));
    CHECK(port_TextsEqual("abc", "abc") && !port_TextsEqual("abc", "abd"));
    CHECK(!port_TextsEqual("abc", "ab"));
}

/* ---- the count of stubs ---- */

static void test_Stubs(void) {
    static PortStub first = {"first", 0, NULL};
    static PortStub second = {"second", 0, NULL};

    port_CountStub(&second);
    port_CountStub(&first);
    port_CountStub(&second);
    CHECK_EQ(first.calls, 1);
    CHECK_EQ(second.calls, 2);
    /* in the order of their first calls */
    CHECK(second.next == &first);
    CHECK(first.next == NULL);
}

void test_Main(void) {
    test_Clock();
    test_Pads();
    test_Events();
    test_Primitives();
    test_Text();
    test_Stubs();
}
