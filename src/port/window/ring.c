/* The native port's window: the sound between the game, which makes a
 * vertical blank's samples at a time, and the sound card, which asks for
 * them when it wants them (window.h; docs/PORT.md, "The window"). No SDL
 * in it: tests/port/window.c runs it on made-up samples.
 *
 * When the sound card asks for more than is waiting, it is not given a
 * hole: the last pair played fades to silence over PORT_RING_FADE
 * samples, silence follows, and play begins again, fading in, only when
 * the usual number of samples is waiting. So a stall is heard as a
 * short silence with soft edges, not as a crackle. */
#include "common.h"
#include "window.h"

void port_InitRing(PortRing *ring, s16 *samples, u32 capacity, u32 target) {
    ring->samples = samples;
    ring->capacity = capacity;
    ring->start = 0;
    ring->count = 0;
    ring->target = target < capacity ? target : capacity;
    ring->is_waiting = 1;
    ring->fade = 0;
    ring->last[0] = 0;
    ring->last[1] = 0;
    ring->underruns = 0;
    ring->dropped = 0;
}

/* The newest are kept (window.h). */
void port_WriteRing(PortRing *ring, const s16 *samples, u32 frames) {
    u32 i;

    if (ring->capacity == 0) {
        return;
    }
    if (frames > ring->capacity) {
        /* more than the ring holds: only its last part can stay */
        ring->dropped += frames - ring->capacity;
        samples += (frames - ring->capacity) * 2;
        frames = ring->capacity;
    }
    if (frames > ring->capacity - ring->count) {
        u32 over = frames - (ring->capacity - ring->count);

        ring->start = (ring->start + over) % ring->capacity;
        ring->count -= over;
        ring->dropped += over;
    }
    for (i = 0; i < frames; i++) {
        u32 at = (ring->start + ring->count + i) % ring->capacity;

        ring->samples[at * 2] = samples[i * 2];
        ring->samples[at * 2 + 1] = samples[i * 2 + 1];
    }
    ring->count += frames;
}

/* Every pair asked for, whatever is waiting (window.h). */
void port_ReadRing(PortRing *ring, s16 *out, u32 frames) {
    u32 i;

    for (i = 0; i < frames; i++) {
        if (ring->is_waiting && ring->count != 0 &&
            ring->count >= ring->target) {
            ring->is_waiting = 0;
        } else if (!ring->is_waiting && ring->count == 0) {
            ring->is_waiting = 1;
            ring->underruns++;
        }
        if (ring->is_waiting) {
            /* the way down from the last pair played, then silence */
            if (ring->fade > 0) {
                ring->fade--;
            }
        } else {
            ring->last[0] = ring->samples[ring->start * 2];
            ring->last[1] = ring->samples[ring->start * 2 + 1];
            ring->start = (ring->start + 1) % ring->capacity;
            ring->count--;
            if (ring->fade < PORT_RING_FADE) {
                ring->fade++;
            }
        }
        out[i * 2] = (s16)(ring->last[0] * ring->fade / PORT_RING_FADE);
        out[i * 2 + 1] = (s16)(ring->last[1] * ring->fade / PORT_RING_FADE);
    }
}
