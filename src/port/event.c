/* The BIOS's events in the native port: a table of flags (docs/PORT.md,
 * "The BIOS calls the game makes").
 *
 * The memory card is how the game uses them: it opens an event for each
 * thing a card can answer, starts something on the card, and asks the
 * events which answer came. What sets them is src/port/card.c; the sound
 * libraries' own events are here too. */
#include "common.h"
#include "bb2.h"

#define EVENTS_MAX 32
/* An event's number, as the BIOS gives it: this and its place in the
 * table. */
#define EVENT_HANDLE 0xF1000000U
#define EVENT_HANDLE_MASK 0xFFFF0000U
#define EVENT_INDEX_MASK 0x0000FFFFU

typedef struct {
    s32 open;
    s32 enabled;
    s32 pending;
    u32 desc; /* its class: what it is about */
    s32 spec; /* its kind: what happened */
} PortEvent;

static PortEvent g_port_events[EVENTS_MAX];

static PortEvent *port_FindEvent(s32 event) {
    u32 index = (u32)event & EVENT_INDEX_MASK;

    if (((u32)event & EVENT_HANDLE_MASK) != EVENT_HANDLE ||
        index >= EVENTS_MAX || !g_port_events[index].open) {
        return NULL;
    }
    return &g_port_events[index];
}

/* A new event of this class and kind, not yet enabled: its number, or
 * -1 if the table is full. */
s32 OpenEvent(u32 desc, s32 spec, s32 mode, s32 (*func)()) {
    u32 i;

    /* the game opens every event to be asked, never to be called */
    (void)mode;
    (void)func;
    for (i = 0; i < EVENTS_MAX; i++) {
        if (!g_port_events[i].open) {
            g_port_events[i].open = 1;
            g_port_events[i].enabled = 0;
            g_port_events[i].pending = 0;
            g_port_events[i].desc = desc;
            g_port_events[i].spec = spec;
            return (s32)(EVENT_HANDLE | i);
        }
    }
    return -1;
}

s32 CloseEvent(s32 event) {
    PortEvent *found = port_FindEvent(event);

    if (found == NULL) {
        return 0;
    }
    found->open = 0;
    return 1;
}

s32 EnableEvent(s32 event) {
    PortEvent *found = port_FindEvent(event);

    if (found == NULL) {
        return 0;
    }
    found->enabled = 1;
    return 1;
}

/* Sony's sound library closes its event and then disables it. */
s32 DisableEvent(s32 event) {
    PortEvent *found = port_FindEvent(event);

    if (found == NULL) {
        return 0;
    }
    found->enabled = 0;
    return 1;
}

s32 TestEvent(s32 event) {
    PortEvent *found = port_FindEvent(event);

    if (found == NULL || !found->pending) {
        return 0;
    }
    found->pending = 0;
    return 1;
}

/* Sets every event of this class and kind (port_platform.h). */
void port_DeliverEvent(u32 desc, s32 spec) {
    u32 i;

    for (i = 0; i < EVENTS_MAX; i++) {
        PortEvent *event = &g_port_events[i];

        if (event->open && event->enabled && event->desc == desc &&
            event->spec == spec) {
            event->pending = 1;
        }
    }
}
