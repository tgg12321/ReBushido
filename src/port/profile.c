/* The native port: where a blank's time goes (port_profile.h;
 * docs/PORT.md, "Where the time goes"). One part runs at a time; a
 * switch adds the time since the last one to the part that ran. */
#include "common.h"
#include "port_host.h"
#include "port_profile.h"
#include "bb2.h"

s32 g_port_profile_on;

u32 (*g_port_profile_clock)(void) = port_HostTicks;
s32 (*g_port_profile_thread)(void);

/* Each thread's own count: the window's, and the step's where the window
 * runs it on a thread of its own (port_profile.h). */
typedef struct {
    s32 part;
    u32 since;
    u32 ticks[PORT_PROFILE_PARTS];
} PortProfileCount;

static PortProfileCount g_port_profile_count[PORT_PROFILE_THREADS];

static PortProfileCount *port_ProfileMine(void) {
    s32 thread = g_port_profile_thread != NULL ? g_port_profile_thread() : 0;

    return &g_port_profile_count[thread > 0 ? 1 : 0];
}

static const char * const g_port_profile_names[PORT_PROFILE_PARTS] = {
    "game",  "raster", "remaster", "card",   "sound", "finish", "picture",
    "tween", "swap",   "wait",     "window", "ahead", "cloth",  "shadow",
};

/* The time since the last switch is the running part's; `part` runs
 * from now (port_profile.h). */
s32 port_ProfileSwitch(s32 part) {
    PortProfileCount *count;
    s32 was;
    u32 now;

    if (!g_port_profile_on) {
        return g_port_profile_count[0].part;
    }
    count = port_ProfileMine();
    was = count->part;
    now = g_port_profile_clock();
    count->ticks[was] += now - count->since;
    count->since = now;
    count->part = part;
    return was;
}

void port_ProfileTake(u32 *ticks) {
    PortProfileCount *count = &g_port_profile_count[0];
    s32 i;

    /* what the running part has had so far is counted to this take */
    port_ProfileSwitch(count->part);
    for (i = 0; i < PORT_PROFILE_PARTS; i++) {
        ticks[i] = count->ticks[i];
        count->ticks[i] = 0;
    }
}

void port_ProfileTakeStep(u32 *ticks) {
    PortProfileCount *count = &g_port_profile_count[1];
    s32 i;

    for (i = 0; i < PORT_PROFILE_PARTS; i++) {
        ticks[i] = count->ticks[i];
        count->ticks[i] = 0;
    }
}

s32 port_ProfilePart(void) { return g_port_profile_count[0].part; }

const char *port_ProfileName(s32 part) {
    return part >= 0 && part < PORT_PROFILE_PARTS
               ? g_port_profile_names[part]
               : "?";
}
