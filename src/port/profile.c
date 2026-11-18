/* The native port: where a blank's time goes (port_profile.h;
 * docs/PORT.md, "Where the time goes"). One part runs at a time; a
 * switch adds the time since the last one to the part that ran. */
#include "common.h"
#include "port_host.h"
#include "port_profile.h"
#include "bb2.h"

s32 g_port_profile_on;

static s32 g_port_profile_part;
static u32 g_port_profile_since;
u32 (*g_port_profile_clock)(void) = port_HostTicks;
static u32 g_port_profile_ticks[PORT_PROFILE_PARTS];

static const char * const g_port_profile_names[PORT_PROFILE_PARTS] = {
    "game",  "raster", "remaster", "card",   "sound", "finish", "picture",
    "tween", "swap",   "wait",     "window", "ahead", "cloth",  "shadow",
};

/* The time since the last switch is the running part's; `part` runs
 * from now (port_profile.h). */
s32 port_ProfileSwitch(s32 part) {
    s32 was = g_port_profile_part;
    u32 now;

    if (!g_port_profile_on) {
        return was;
    }
    now = g_port_profile_clock();
    g_port_profile_ticks[was] += now - g_port_profile_since;
    g_port_profile_since = now;
    g_port_profile_part = part;
    return was;
}

void port_ProfileTake(u32 *ticks) {
    s32 i;

    /* what the running part has had so far is counted to this take */
    port_ProfileSwitch(g_port_profile_part);
    for (i = 0; i < PORT_PROFILE_PARTS; i++) {
        ticks[i] = g_port_profile_ticks[i];
        g_port_profile_ticks[i] = 0;
    }
}

s32 port_ProfilePart(void) { return g_port_profile_part; }

const char *port_ProfileName(s32 part) {
    return part >= 0 && part < PORT_PROFILE_PARTS
               ? g_port_profile_names[part]
               : "?";
}
