/* The replay theatre and photo mode (port_theatre.h; ROADMAP 29;
 * docs/PORT.md, "The replay theatre and photo mode"): the view a look
 * draws with, the ring of states and inputs the looks run from, and the
 * game's three questions (the camera, the HUD, the time of day).
 *
 * Outside the state (config/port_state.txt): a load puts none of it back,
 * and nothing of it reaches what the game computes but in a look, whose
 * state is put back after it. */
#include "common.h"
#include "port_host.h"
#include "port_net.h"
#include "bb2.h"

/* A look runs the step before the boundary again: the picture a boundary
 * shows is the one that step's frame left on the display (step.c, as a
 * frame run ahead shows its own). */
#define LOOK_STEPS PORT_THEATRE_LOOK_STEPS
/* the states kept: the boundary before, and this one to come back to */
#define RING (LOOK_STEPS + 1)
/* the inputs kept, more than the looks reach back */
#define INPUTS 16
#define LINK_END 0x00FFFFFFU

typedef struct {
    u8 *state;
    u32 at; /* g_port_input_count it was kept at, plus one; 0: empty */
} TheatreKept;

static struct {
    PortTheatreView view;
    s32 on;
    s32 looking;
    s32 shown; /* the last look drew the frame on show */
    TheatreKept ring[RING];
    u32 loads_seen;
    PortInput inputs[INPUTS];
    u32 input_index[INPUTS];
    PortTheatreCamera game;
    s32 has_game;
    PortTheatreCounts counts;
} g_theatre;

/* ---- the view ---- */

void port_TheatreSetView(const PortTheatreView *view) {
    g_theatre.view = *view;
}

void port_TheatreSetOn(s32 on) {
    g_theatre.on = on;
    if (!on) {
        g_theatre.shown = FALSE;
    }
}

void port_TheatreGetView(PortTheatreView *view) { *view = g_theatre.view; }

s32 port_TheatreIsOn(void) { return g_theatre.on; }

s32 port_TheatreGameCamera(PortTheatreCamera *camera) {
    if (!g_theatre.has_game) {
        return FALSE;
    }
    *camera = g_theatre.game;
    return TRUE;
}

/* ---- the game's ---- */

void port_TheatreScene(s32 *focus, s16 *angles, s32 *distance) {
    s32 i;

    if (g_theatre.looking) {
        if (g_theatre.view.free) {
            for (i = 0; i < 3; i++) {
                focus[i] = g_theatre.view.camera.focus[i];
                angles[i] = g_theatre.view.camera.angles[i];
            }
            *distance = g_theatre.view.camera.distance;
        }
        return;
    }
    if (g_port_ahead) {
        /* a frame run ahead is not the frame on show */
        return;
    }
    for (i = 0; i < 3; i++) {
        g_theatre.game.focus[i] = focus[i];
        g_theatre.game.angles[i] = angles[i];
    }
    g_theatre.game.distance = *distance;
    g_theatre.has_game = TRUE;
}

void port_TheatreCutHud(u32 *ot) {
    if (g_theatre.looking && g_theatre.view.hide_hud) {
        /* the list is drawn from its last entry down to the first: it ends
         * at this one, before the 2D layer's */
        ot[PORT_THEATRE_HUD_SLOTS - 1] = LINK_END;
    }
}

s32 port_TheatreTime(void) { return g_theatre.on ? g_theatre.view.time : -1; }

/* ---- the ring ---- */

void port_TheatreInput(PortInput *input, u32 index) {
    u32 slot = index % INPUTS;

    if (g_theatre.looking) {
        if (g_theatre.input_index[slot] == index) {
            *input = g_theatre.inputs[slot];
        }
        return;
    }
    if (g_port_ahead) {
        return;
    }
    g_theatre.inputs[slot] = *input;
    g_theatre.input_index[slot] = index;
}

/* The ring emptied where a load not the theatre's own has come since it
 * was kept: its states are of another moment, whatever their count. */
static void port_TheatreCheckLoads(void) {
    s32 i;

    if (g_port_state_loads == g_theatre.loads_seen) {
        return;
    }
    g_theatre.loads_seen = g_port_state_loads;
    for (i = 0; i < RING; i++) {
        if (g_theatre.ring[i].at != 0) {
            g_theatre.counts.forgotten++;
        }
        g_theatre.ring[i].at = 0;
    }
}

static TheatreKept *port_TheatreFind(u32 count) {
    s32 i;

    for (i = 0; i < RING; i++) {
        if (g_theatre.ring[i].at == count + 1) {
            return &g_theatre.ring[i];
        }
    }
    return NULL;
}

/* The state at this boundary into the ring (port_theatre.h), over the
 * oldest one kept, where it is not there already. */
void port_TheatreKeep(void) {
    u32 count = g_port_input_count;
    TheatreKept *slot = NULL;
    s32 i;

    if (!g_theatre.on || g_port_ahead || g_port_rerun || port_IsNetplayOn()) {
        return;
    }
    port_TheatreCheckLoads();
    if (port_TheatreFind(count) != NULL) {
        /* this boundary's already: nothing has stepped since */
        return;
    }
    /* the slot of the oldest count, or an empty one */
    for (i = 0; i < RING; i++) {
        TheatreKept *kept = &g_theatre.ring[i];

        if (kept->at == 0) {
            slot = kept;
            break;
        }
        if (slot == NULL || kept->at < slot->at) {
            slot = kept;
        }
    }
    if (slot->state == NULL) {
        slot->state = port_HostAllocate(port_StateSize());
        if (slot->state == NULL) {
            port_Log("rebushido: theatre: no memory for a state\n");
            return;
        }
    }
    port_StateSave(slot->state);
    slot->at = count + 1;
    g_theatre.counts.kept++;
}

s32 port_TheatreCanLook(void) {
    u32 count = g_port_input_count;

    port_TheatreCheckLoads();
    return count >= LOOK_STEPS && port_TheatreFind(count - LOOK_STEPS) != NULL;
}

/* A look (port_theatre.h): the state LOOK_STEPS boundaries back put in,
 * those steps run again with the inputs they took and the theatre's view,
 * the last for the display it puts up, and this boundary's state put
 * back. */
s32 port_TheatreLook(void) {
    u32 count = g_port_input_count;
    TheatreKept *from;
    TheatreKept *back;
    s32 drawn;

    if (g_port_ahead || g_port_rerun || port_IsNetplayOn() || port_LinkIsUp()) {
        return FALSE;
    }
    port_TheatreKeep();
    port_TheatreCheckLoads();
    back = port_TheatreFind(count);
    from = count >= LOOK_STEPS ? port_TheatreFind(count - LOOK_STEPS) : NULL;
    if (back == NULL || from == NULL) {
        g_theatre.counts.failed++;
        g_theatre.shown = FALSE;
        return FALSE;
    }
    g_theatre.looking = TRUE;
    drawn = port_RunLook(from->state, LOOK_STEPS, back->state);
    g_theatre.looking = FALSE;
    /* the look's own loads empty nothing */
    g_theatre.loads_seen = g_port_state_loads;
    if (drawn) {
        g_theatre.counts.looks++;
    } else {
        g_theatre.counts.failed++;
    }
    g_theatre.shown = drawn;
    return drawn;
}

s32 port_TheatreShowsLook(void) { return g_theatre.on && g_theatre.shown; }

void port_TheatreGetCounts(PortTheatreCounts *counts) {
    *counts = g_theatre.counts;
}
