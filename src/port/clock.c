/* Time in the native port (docs/PORT.md, "Time"): Sony's VSync and the
 * BIOS's root counters, on a clock of the port's own.
 *
 * The clock is a count of scan lines. Nothing moves it but the game:
 *
 *   - VSync waits, so the clock goes on to the vertical blank it waits
 *     for;
 *   - GetRCnt of the scan line counter moves it one line each time it is
 *     asked.
 *
 * What the game computes between two waits takes no time at all. So a
 * run is the same every time and on every machine, whatever the PC's
 * speed: nothing here reads the PC's clock. (A window, at stage 4 of
 * docs/PORT.md, will make VSync wait for the real blank as well; the
 * count stays this one.)
 *
 * The second rule is for the wait at the end of main's frame, which asks
 * the counter over and over until it has reached half a field short of
 * the frame's length, calling rand() each time. On the console the
 * number of those calls is whatever time the frame had left. Here it is
 * the number of lines asked for: 384 calls in a frame of two fields, 128
 * in a frame of one, every frame.
 *
 * What a vertical blank brings is the pads' packets, the card driver's
 * turn and a blank's worth of sound, in that order (port_AdvanceClock). */
#include "common.h"
#include "port_card.h"
#include "port_spu.h"
#include "bb2.h"

#define COUNTER_MASK 0xFFFF

/* Scan lines since the program started; a vertical blank is every
 * PORT_LINES_PER_BLANK of them. */
static u32 g_port_clock_lines;
/* Where the scan line counter was last reset. */
static u32 g_port_counter_base;
/* The vertical blank and the line of the last VSync that waited. */
static u32 g_port_vsync_blank;
static u32 g_port_vsync_line;

u32 port_GetBlankCount(void) {
    return g_port_clock_lines / PORT_LINES_PER_BLANK;
}

/* Moves the clock on to `line`, with whatever a vertical blank on the way
 * brings. */
static void port_AdvanceClock(u32 line) {
    while (g_port_clock_lines < line) {
        u32 next_blank = (g_port_clock_lines / PORT_LINES_PER_BLANK + 1) *
                         PORT_LINES_PER_BLANK;

        if (next_blank > line) {
            g_port_clock_lines = line;
            break;
        }
        g_port_clock_lines = next_blank;
        /* the script's presses first: the pads are read at this blank */
        port_OnBlank(next_blank / PORT_LINES_PER_BLANK);
        port_FillPads();
        /* the card driver's turn, which the console's takes beside the
         * pads': a card operation that is done by now reports itself */
        port_RunCardBlank(next_blank / PORT_LINES_PER_BLANK);
        /* the sound library's tick, and a blank's worth of sound */
        port_RunSoundBlank(next_blank / PORT_LINES_PER_BLANK);
    }
}

/* As Sony's: 0 waits for the next vertical blank; n over 1 until n have
 * passed since the last call that waited, and at least one; 1 does not
 * wait. Each returns the scan lines since the last call that waited. A
 * negative mode returns the vertical blanks since the program started. */
s32 VSync(s32 mode) {
    u32 blank = port_GetBlankCount();
    s32 lines = (s32)((g_port_clock_lines - g_port_vsync_line) & COUNTER_MASK);
    u32 target;

    if (mode < 0) {
        return (s32)blank;
    }
    if (mode == 1) {
        return lines;
    }
    target = g_port_vsync_blank;
    if (mode > 0) {
        target += (u32)mode - 1;
    }
    if (target < blank) {
        target = blank;
    }
    port_AdvanceClock((target + 1) * PORT_LINES_PER_BLANK);
    g_port_vsync_blank = port_GetBlankCount();
    g_port_vsync_line = g_port_clock_lines;
    return lines;
}

/* ---- the root counters ---- */

/* Only the counter of scan lines is the port's: the game uses no other.
 * It counts freely, whatever target and mode it is given. Sony's sound
 * library sets the counter of vertical blanks up for its tick, which
 * needs nothing: the blank's interrupt is the port's clock itself
 * (sound.c). */
s32 SetRCnt(u32 spec, s32 target, s32 mode) {
    (void)target;
    (void)mode;
    if (spec == RCntCNT3) {
        return 1;
    }
    if (spec != RCntCNT1) {
        PORT_STUB("SetRCnt (a counter other than the scan lines')");
        return 0;
    }
    g_port_counter_base = g_port_clock_lines;
    return 1;
}

s32 StartRCnt(u32 spec) { return spec == RCntCNT1 || spec == RCntCNT3; }

/* The game resets the counter of scan lines once a frame, as the frame
 * begins (main.c, after the wait for the vertical blank): that is where
 * a run's script counts the game's frames (plan.c). */
s32 ResetRCnt(u32 spec) {
    if (spec == RCntCNT3) {
        return 1;
    }
    if (spec != RCntCNT1) {
        PORT_STUB("ResetRCnt (a counter other than the scan lines')");
        return 0;
    }
    g_port_counter_base = g_port_clock_lines;
    port_OnFrame();
    return 1;
}

s32 GetRCnt(u32 spec) {
    s32 count;

    if (spec != RCntCNT1) {
        PORT_STUB("GetRCnt (a counter other than the scan lines')");
        return 0;
    }
    count = (s32)((g_port_clock_lines - g_port_counter_base) & COUNTER_MASK);
    port_AdvanceClock(g_port_clock_lines + 1);
    return count;
}
