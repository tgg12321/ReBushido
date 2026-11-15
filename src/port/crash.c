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

static void port_PrintPlace(const char *lead, u32 address) {
    u32 offset = 0;
    const char *name = port_FindFunction(address, &offset);

    if (name != NULL) {
        port_Log(
            "rebushido:   %s %s+0x%X (0x%08X)\n", lead, name, offset, address);
    } else {
        port_Log("rebushido:   %s 0x%08X\n", lead, address);
    }
}

/* The functions that called the one whose frame is `frame`: each frame
 * begins with the caller's frame and the address it returns to. */
static void port_PrintCallers(u32 frame) {
    s32 i;

    for (i = 0; i < CALLERS_MAX; i++) {
        const u32 *words = (const u32 *)frame;
        u32 next;

        if (frame == 0 || (frame & 3) != 0) {
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

static void port_PrintGameState(void) {
    port_Log("rebushido:   at vertical blank %u; the game's screen %d, its "
             "frame %d\n",
             port_GetBlankCount(), g_sys_screen, g_disp_frame_count);
}

static void port_ReportCrash(const PortCrash *crash) {
    port_Log("rebushido: stopped: %s, address 0x%08X\n", crash->what,
             crash->address);
    port_PrintPlace("in", crash->pc);
    port_PrintGameState();
    port_PrintCallers(crash->frame);
}

void port_CatchCrashes(void) { port_HostCatchCrashes(port_ReportCrash); }

/* The game's own stop: on the console a write to where there is no
 * memory. */
void port_Trap(void) {
    port_Log("rebushido: stopped: the game cannot go on (sys_Trap)\n");
    port_PrintGameState();
    port_PrintCallers((u32)__builtin_frame_address(0));
    port_Quit(PORT_EXIT_FAILED);
}
