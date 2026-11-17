/* The native port's window: when a vertical blank is shown (window.h;
 * docs/PORT.md, "The window"). No SDL in it and no clock: the times are
 * given to it, so tests/port/window.c runs it on made-up ones.
 *
 * The game's time is the samples it has mixed: 737 and 341/2500 a blank
 * (sound.c), which is the console's 59.83 blanks a second. A blank is
 * shown when the PC's clock, read in samples, has come as far past its
 * reading at the start as the game's time. That is all the PC's clock
 * does: what the game computes for a blank was computed before the
 * wait, and is the same whatever the wait was.
 *
 * Two clocks are not one. The sound card plays 44,100 samples a second
 * by its own crystal, the PC's clock counts by another, and the two
 * differ by a few parts in a hundred thousand: left alone, the queue
 * of samples waiting for the card would slowly empty or fill. So the
 * queue's length steers: where it is longer than it should be the
 * blanks come a little later, where it is shorter a little sooner, by
 * at most PORT_PACE_NUDGE_MAX samples' time a blank. In the long run
 * the game therefore keeps the sound card's time, and no sample is
 * dropped or made up to do it. */
#include "common.h"
#include "window.h"

/* The queue's error is smoothed over about this many blanks (a power of
 * two): the card takes its samples in gulps larger than a blank's. */
#define SMOOTH_SHIFT 5
#define SMOOTH_FRACTION_BITS 8

/* How long ago time was last let go matters only for a while. */
static void port_CountLostTime(PortPace *pace, u32 blank) {
    if (blank - pace->window_start > PORT_PACE_LOST_WINDOW) {
        pace->window_start = blank;
        pace->lost_recent = 0;
    }
    pace->lost++;
    pace->lost_recent++;
}

/* How long until this blank is due (window.h). */
u32 port_PaceBlank(
    PortPace *pace, u32 blank, u32 now, u32 due, s32 queue_error) {
    s32 smoothed;
    s32 nudge = 0;
    s32 lead;

    if (!pace->is_started) {
        pace->is_started = 1;
        pace->base = now - due;
        pace->lost = 0;
        pace->lost_recent = 0;
        pace->window_start = blank;
        pace->nudged = 0;
        pace->queue_average = 0;
        pace->held = 0;
    }
    pace->queue_average +=
        (queue_error * (1 << SMOOTH_FRACTION_BITS) - pace->queue_average) /
        (1 << SMOOTH_SHIFT);
    smoothed = pace->queue_average / (1 << SMOOTH_FRACTION_BITS);
    if (smoothed > PORT_PACE_SLACK) {
        nudge = ((smoothed - PORT_PACE_SLACK) >> PORT_PACE_NUDGE_SHIFT) + 1;
    } else if (smoothed < -PORT_PACE_SLACK) {
        nudge = -(((-smoothed - PORT_PACE_SLACK) >> PORT_PACE_NUDGE_SHIFT) + 1);
    }
    if (nudge > PORT_PACE_NUDGE_MAX) {
        nudge = PORT_PACE_NUDGE_MAX;
    } else if (nudge < -PORT_PACE_NUDGE_MAX) {
        nudge = -PORT_PACE_NUDGE_MAX;
    }
    pace->base += (u32)nudge;
    pace->nudged += nudge;
    /* a hold asked for (port_PaceHold), a part at a time */
    if (pace->held != 0) {
        u32 part =
            pace->held < PORT_PACE_HOLD_STEP ? pace->held : PORT_PACE_HOLD_STEP;

        pace->base += part;
        pace->held -= part;
    }

    lead = (s32)(pace->base + due - now);
    if (lead < -PORT_PACE_LOST || lead > PORT_PACE_LOST) {
        /* the PC held the program up, or its clock jumped: from here */
        pace->base = now - due;
        port_CountLostTime(pace, blank);
        return 0;
    }
    return lead > 0 ? (u32)lead : 0;
}

/* Every blank from here on is due `samples` later (window.h): netplay's
 * time sync, where this program is ahead of the other. It is spread over
 * the blanks to come, at most a quarter of a blank's time in each, so
 * that the sound's queue is drawn down a little and never runs dry. */
void port_PaceHold(PortPace *pace, u32 samples) {
    if (pace->is_started) {
        pace->held += samples;
    }
}

s32 port_PaceIsHeldBack(const PortPace *pace) {
    return pace->is_started && pace->lost_recent >= PORT_PACE_LOST_TIMES;
}
