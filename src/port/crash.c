/* What the native port says when the game stops it: a fault (memory that
 * is not there, a division by zero), or the game's own sys_Trap. It
 * names the function and the ones that called it, by the names the
 * build wrote beside the program; where there is no name, the address.
 * The program is built with frame pointers kept, so the callers are a
 * chain on the stack. */
#include "common.h"
#include "port_host.h"
#include "bb2.h"

#define CALLERS_MAX 12
/* A frame further up the stack than this from the one before it is not
 * a frame. */
#define FRAME_GAP_MAX 0x100000U
/* An address is printed as eight digits where it fits 32 bits. */
#define ADDRESS_BITS_USUAL 32
#define ADDRESS_DIGITS_USUAL 8
#define ADDRESS_DIGITS_WIDE 16
#define BITS_PER_DIGIT 4
#define DIGIT_MASK 0xF
#define ADDRESS_TEXT_BYTES (ADDRESS_DIGITS_WIDE + 1)

/* An address as text: eight digits, or sixteen where it is past what 32
 * bits hold, which nothing of the program's or the game's is. */
static const char *port_AddressText(PortAddress address, char *text) {
    static const char digits[] = "0123456789ABCDEF";
    s32 count = ADDRESS_DIGITS_USUAL;
    s32 i;

    if (((u64)address >> ADDRESS_BITS_USUAL) != 0) {
        count = ADDRESS_DIGITS_WIDE;
    }
    for (i = 0; i < count; i++) {
        text[i] = digits[((u64)address >> ((count - 1 - i) * BITS_PER_DIGIT)) &
                         DIGIT_MASK];
    }
    text[count] = 0;
    return text;
}

/* A line for one place of the program: the function it is in and how far
 * into it, where the names know, and the address. */
static void port_PrintPlace(const char *lead, PortAddress address) {
    char text[ADDRESS_TEXT_BYTES];
    u32 offset = 0;
    const char *name = ((u64)address >> ADDRESS_BITS_USUAL) != 0
                           ? NULL
                           : port_FindFunction((u32)address, &offset);

    if (name != NULL) {
        port_Log("rebushido:   %s %s+0x%X (0x%s)\n", lead, name, offset,
                 port_AddressText(address, text));
    } else {
        port_Log(
            "rebushido:   %s 0x%s\n", lead, port_AddressText(address, text));
    }
}

/* The functions that called the one whose frame is `frame`: each frame
 * begins with the caller's frame and the address it returns to. */
static void port_PrintCallers(PortAddress frame) {
    s32 i;

    for (i = 0; i < CALLERS_MAX; i++) {
        const PortAddress *words = (const PortAddress *)frame;
        PortAddress next;

        if (frame == 0 || (frame & (sizeof(frame) - 1)) != 0) {
            break;
        }
        next = words[0];
        port_PrintPlace("called from", words[1]);
        if (next <= frame || next - frame > FRAME_GAP_MAX) {
            break;
        }
        frame = next;
    }
}

/* The window's turn after a fault's report (port_platform.h): nothing in
 * the build without one. */
void (*g_port_crash_hook)(void);

static void port_PrintGameState(void) {
    port_Log("rebushido:   at vertical blank %u; the game's screen %d, its "
             "frame %d\n",
             port_GetBlankCount(), g_sys_screen, g_disp_frame_count);
}

/* What the host calls for a fault: what happened and where, the
 * instruction's own function, where the game was, the chain of callers,
 * and whatever the window has to add to it. */
static void port_ReportCrash(const PortCrash *crash) {
    char text[ADDRESS_TEXT_BYTES];

    port_Log("rebushido: stopped: %s, address 0x%s\n", crash->what,
             port_AddressText(crash->address, text));
    port_PrintPlace("in", crash->pc);
    port_PrintGameState();
    port_PrintCallers(crash->frame);
    if (g_port_crash_hook != NULL) {
        g_port_crash_hook();
    }
}

void port_CatchCrashes(void) { port_HostCatchCrashes(port_ReportCrash); }

/* The game's own stop: on the console a write to where there is no
 * memory. */
void port_Trap(void) {
    port_Log("rebushido: stopped: the game cannot go on (sys_Trap)\n");
    port_PrintGameState();
    port_PrintCallers((PortAddress)__builtin_frame_address(0));
    port_Quit(PORT_EXIT_FAILED);
}
