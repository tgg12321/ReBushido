/* The pads in the native port: the BIOS's pad driver, as far as the game
 * sees it, and the one place the game's input comes from. The game gives
 * InitPAD two buffers and reads them once a frame (pad.c); the console's
 * driver fills them at every vertical blank. Here the driver's read at a
 * blank is kept, and it is written into the buffers as the game reads
 * them (port_TakeInput), so that what the game reads is one value, a
 * PortInput, which a replay or a network can stand in for (docs/
 * NETPLAY.md, "Input"). The buttons come from a run's script
 * (port_SetPadButton, plan.c) and from the window's keys and controllers
 * (g_port_device_hook, src/port/window/): two digital pads, always
 * plugged in once the driver has started. */
#include "common.h"
#include "port_movie.h"
#include "bb2.h"

/* A pad's packet: a status byte (0 when the pad answered, 0xFF before
 * the driver has asked it), its kind and the half-words of data that
 * follow, then two bytes of buttons, a 0 bit for each one held. */
enum {
    PACKET_STATUS = 0,
    PACKET_KIND = 1,
    PACKET_BUTTONS = 2,
    PACKET_BYTES = 4,
};

#define PACKET_ANSWERED 0x00
#define PACKET_SILENT 0xFF
#define PACKET_DIGITAL_PAD 0x41
#define BUTTON_BITS 16
#define BUTTONS_MASK 0xFFFF
#define BYTE_BITS 8

static u8 *g_port_pad_buffers[PORT_PADS];
static s32 g_port_pad_buffer_sizes[PORT_PADS];
static s32 g_port_pads_started;
/* The buttons a run's script holds on each pad, as Sony's PAD... masks. */
static u32 g_port_pad_held[PORT_PADS];
/* What the driver read at the last vertical blank since it started:
 * nothing answered before that. */
static PortInput g_port_pad_read;
/* ... of it, what the run's script held then: a read of the PC's own
 * input later on keeps it (port_ReadDevicesNow). */
static u32 g_port_pad_read_held[PORT_PADS];

/* The window's keys and controllers, and the hook on each input
 * (port_platform.h): nothing in the build without one. */
u32 (*g_port_device_hook)(s32 pad);
void (*g_port_take_hook)(PortInput *input);
PortInput g_port_input;
u32 g_port_input_count;

void port_SetPadButton(s32 pad, u32 button, s32 down) {
    if (pad < 0 || pad >= PORT_PADS) {
        return;
    }
    if (down) {
        g_port_pad_held[pad] |= button;
    } else {
        g_port_pad_held[pad] &= ~button;
    }
}

/* Both pads now (port_platform.h). */
void port_SampleInput(PortInput *input) {
    s32 pad;

    for (pad = 0; pad < PORT_PADS; pad++) {
        u32 held = g_port_pad_held[pad];

        if (g_port_device_hook != NULL) {
            held |= g_port_device_hook(pad);
        }
        input->buttons[pad] = (u16)(held & BUTTONS_MASK);
        input->present[pad] = 1;
    }
}

/* What the driver read at the last vertical blank (port_platform.h). */
void port_DriverInput(PortInput *input) { *input = g_port_pad_read; }

/* The driver's read at a vertical blank (port_platform.h). */
void port_FillPads(void) {
    s32 pad;

    if (g_port_pads_started) {
        port_SampleInput(&g_port_pad_read);
        for (pad = 0; pad < PORT_PADS; pad++) {
            g_port_pad_read_held[pad] = g_port_pad_held[pad];
        }
    }
}

/* The PC's keys and controllers read again into the driver's last read,
 * with the script's buttons as they were at that blank (port_platform.h):
 * the window's, just before the game takes the input. */
void port_ReadDevicesNow(void) {
    s32 pad;

    if (!g_port_pads_started || g_port_device_hook == NULL) {
        return;
    }
    for (pad = 0; pad < PORT_PADS; pad++) {
        u32 held = g_port_device_hook(pad);

        held |= g_port_pad_read_held[pad];
        if (g_port_pad_read.present[pad]) {
            g_port_pad_read.buttons[pad] = (u16)(held & BUTTONS_MASK);
        }
    }
}

/* The game reads the pads (port_platform.h): the packets, as the driver
 * leaves them, from the input. One that nothing answered says so, and
 * the rest of it is left as it was. This is the frame's boundary: the
 * step ends here, and the input is taken when the host runs the next. */
void port_TakeInput(void) {
    s32 pad;

    port_EndStep();
    g_port_input = g_port_pad_read;
    if (g_port_take_hook != NULL) {
        g_port_take_hook(&g_port_input);
    }
    g_port_input_count++;
    for (pad = 0; pad < PORT_PADS; pad++) {
        u8 *packet = g_port_pad_buffers[pad];

        if (packet == NULL || g_port_pad_buffer_sizes[pad] < PACKET_BYTES) {
            continue;
        }
        if (!g_port_input.present[pad]) {
            packet[PACKET_STATUS] = PACKET_SILENT;
            continue;
        }
        packet[PACKET_STATUS] = PACKET_ANSWERED;
        packet[PACKET_KIND] = PACKET_DIGITAL_PAD;
        packet[PACKET_BUTTONS] = (u8) ~(g_port_input.buttons[pad] >> BYTE_BITS);
        packet[PACKET_BUTTONS + 1] = (u8)~g_port_input.buttons[pad];
    }
}

/* The game's two buffers, one a pad: until the driver first asks a pad,
 * its packet says that nothing answered. */
s32 InitPAD(char *bufA, s32 lenA, char *bufB, s32 lenB) {
    g_port_pad_buffers[0] = (u8 *)bufA;
    g_port_pad_buffers[1] = (u8 *)bufB;
    g_port_pad_buffer_sizes[0] = lenA;
    g_port_pad_buffer_sizes[1] = lenB;
    g_port_pad_read.present[0] = 0;
    g_port_pad_read.present[1] = 0;
    if (bufA != NULL && lenA > 0) {
        bufA[PACKET_STATUS] = (char)PACKET_SILENT;
    }
    if (bufB != NULL && lenB > 0) {
        bufB[PACKET_STATUS] = (char)PACKET_SILENT;
    }
    return 1;
}

s32 StartPAD(void) {
    g_port_pads_started = 1;
    return 1;
}

void StopPAD(void) { g_port_pads_started = 0; }

/* Whether the pad driver or the game answers the vertical blank's
 * interrupt: the port has no interrupts. */
void ChangeClearPAD(s32 clear) { (void)clear; }

/* ---- the older interface, which the movie player uses ---- */

/* The BIOS's own pad reader, under Sony's libetc (movovl/sdk/libetc/
 * pad.c): PAD_init is given one word to keep both pads' buttons in, a
 * bit clear for each button held, the first pad in the low half and the
 * second in the high, and PAD_dr fills it in. The movie player asks for
 * it each time round its loop (movovl/src/main.c), which is where the
 * cross button skips the movie. */
static s32 *g_port_pad_word;

void PAD_init(s32 type, s32 *buf) {
    (void)type;
    g_port_pad_word = buf;
    if (buf != NULL) {
        *buf = -1;
    }
}

/* Set by netplay (port_platform.h): every movie is skipped where it
 * begins, on both machines alike. */
s32 g_port_movies_skipped;

/* The movie player is a program of its own, which reads the pads when it
 * likes and not through the game's buffers: it is given them as they are
 * now. In netplay that would be this PC's own pads, which the other
 * machine never sees, so there it is given the cross button held from its
 * first read, which skips the movie at once on both. */
void PAD_dr(void) {
    if (g_port_pad_word != NULL && g_port_movies_skipped) {
        *g_port_pad_word = (s32) ~(u32)PADRdown;
    } else if (g_port_pad_word != NULL) {
        PortInput now;

        port_SampleInput(&now);
        *g_port_pad_word =
            (s32) ~((u32)now.buttons[0] | ((u32)now.buttons[1] << BUTTON_BITS));
    }
}
