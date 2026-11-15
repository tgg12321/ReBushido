/* The BIOS's events in the native port, and the memory card, which is how
 * the game uses them: it opens an event for each thing a card can
 * answer, starts something on the card, and asks the events which
 * answer came.
 *
 * The events are real: a table of flags (docs/PORT.md, "The BIOS calls
 * the game makes"). The card is not there yet (stage 7): both slots are
 * empty. Each of the BIOS's card calls is a stub that is counted, and
 * answers as the console does with no card in the slot, by the event
 * that says nothing answered in time (EvSpTIMOUT). The answer is there
 * as soon as the call returns: the game waits for it in loops with no
 * frame between, so it cannot come later. The card's files cannot be
 * opened. */
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

/* ---- the memory card: no card in either slot ---- */

void InitCARD(s32 pads_too) {
    PORT_STUB("InitCARD (memory card)");
    (void)pads_too;
}

s32 StartCARD(void) {
    PORT_STUB("StartCARD (memory card)");
    return 1;
}

s32 StopCARD(void) {
    PORT_STUB("StopCARD (memory card)");
    return 1;
}

void _bu_init(void) { PORT_STUB("_bu_init (memory card)"); }

s32 _card_info(s32 chan) {
    PORT_STUB("_card_info (memory card: no card)");
    (void)chan;
    port_DeliverEvent(SwCARD, EvSpTIMOUT);
    return 1;
}

s32 _card_load(s32 chan) {
    PORT_STUB("_card_load (memory card: no card)");
    (void)chan;
    port_DeliverEvent(SwCARD, EvSpTIMOUT);
    return 1;
}

s32 _card_clear(s32 chan) {
    PORT_STUB("_card_clear (memory card: no card)");
    (void)chan;
    port_DeliverEvent(HwCARD, EvSpTIMOUT);
    return 1;
}

/* ---- the BIOS's files: the card's, and the link cable's port ---- */

/* The game opens two kinds of thing by name: a card's file ("bu00:...")
 * and the serial port. No card's file can be opened. The serial port
 * can, because the game asks until it does; nothing comes through it. */
#define NOT_A_CARDS_FILE 3

s32 open(char *name, u32 mode) {
    (void)mode;
    if (name[0] == 'b' && name[1] == 'u') {
        PORT_STUB("open (a memory card's file: no card)");
        return -1;
    }
    PORT_STUB("open (the link cable's port)");
    return NOT_A_CARDS_FILE;
}

s32 close(s32 fd) {
    PORT_STUB("close (memory card, link cable)");
    (void)fd;
    return 0;
}

s32 read(s32 fd, void *buf, s32 count) {
    PORT_STUB("read (memory card, link cable)");
    (void)fd;
    (void)buf;
    (void)count;
    return -1;
}

s32 write(s32 fd, void *buf, s32 count) {
    PORT_STUB("write (memory card, link cable)");
    (void)fd;
    (void)buf;
    (void)count;
    return -1;
}

struct DIRENTRY *firstfile(char *name, struct DIRENTRY *dir) {
    PORT_STUB("firstfile (memory card: no card)");
    (void)name;
    (void)dir;
    return NULL;
}

struct DIRENTRY *nextfile(struct DIRENTRY *dir) {
    PORT_STUB("nextfile (memory card: no card)");
    (void)dir;
    return NULL;
}

s32 format(char *fs) {
    PORT_STUB("format (memory card: no card)");
    (void)fs;
    return 0;
}
