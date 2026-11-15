/* The pads in the native port: the BIOS's pad driver, as far as the game
 * sees it. The game gives InitPAD two buffers and reads them once a
 * frame (pad.c); the console's driver fills them at every vertical
 * blank. Here they are filled the same way from the buttons that
 * port_SetPadButton has been told are down, which so far come only from
 * a run's script (plan.c): two digital pads, always plugged in. The
 * PC's keyboard and controllers are stage 5 of docs/PORT.md. */
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

static u8 *g_port_pad_buffers[2];
static s32 g_port_pad_buffer_sizes[2];
static s32 g_port_pads_started;
/* The buttons held on each pad, as Sony's PAD... masks. */
static u32 g_port_pad_held[2];

void port_SetPadButton(s32 pad, u32 button, s32 down) {
    if (pad < 0 || pad > 1) {
        return;
    }
    if (down) {
        g_port_pad_held[pad] |= button;
    } else {
        g_port_pad_held[pad] &= ~button;
    }
}

/* The two packets, as the driver leaves them at a vertical blank
 * (port_platform.h). */
void port_FillPads(void) {
    s32 pad;

    if (!g_port_pads_started) {
        return;
    }
    for (pad = 0; pad < 2; pad++) {
        u8 *packet = g_port_pad_buffers[pad];

        if (packet == NULL || g_port_pad_buffer_sizes[pad] < PACKET_BYTES) {
            continue;
        }
        packet[PACKET_STATUS] = PACKET_ANSWERED;
        packet[PACKET_KIND] = PACKET_DIGITAL_PAD;
        packet[PACKET_BUTTONS] = (u8) ~(g_port_pad_held[pad] >> 8);
        packet[PACKET_BUTTONS + 1] = (u8)~g_port_pad_held[pad];
    }
}

/* The game's two buffers, one a pad: until the driver first asks a pad,
 * its packet says that nothing answered. */
s32 InitPAD(char *bufA, s32 lenA, char *bufB, s32 lenB) {
    g_port_pad_buffers[0] = (u8 *)bufA;
    g_port_pad_buffers[1] = (u8 *)bufB;
    g_port_pad_buffer_sizes[0] = lenA;
    g_port_pad_buffer_sizes[1] = lenB;
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

void PAD_dr(void) {
    if (g_port_pad_word != NULL) {
        *g_port_pad_word =
            (s32) ~((g_port_pad_held[0] & 0xFFFF) | (g_port_pad_held[1] << 16));
    }
}
