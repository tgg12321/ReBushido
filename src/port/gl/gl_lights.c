/* The lights of a frame beside the stage's own (gl_lights.h; docs/PORT.md,
 * "The lighting overhaul"): the list as it is filled, and as the picture
 * reads it. Nothing here is the game's state or changes a value the game
 * computes. */
#include <string.h>

#include "common.h"
#include "gl_lights.h"

static struct {
    /* the list being filled, between Begin and End */
    PortGlLight building[PORT_GL_LIGHTS_MAX];
    s32 building_count;
    /* the list handed to the picture, and the one before it */
    PortGlLight now[PORT_GL_LIGHTS_MAX];
    s32 now_count;
    PortGlLight before[PORT_GL_LIGHTS_MAX];
    s32 before_count;
} g_lights;

void port_GlLightsBegin(void) { g_lights.building_count = 0; }

s32 port_GlLightsAdd(const PortGlLight *light) {
    s32 at = g_lights.building_count;

    if (light == NULL || at >= PORT_GL_LIGHTS_MAX || !(light->radius > 0.0f) ||
        !(light->intensity > 0.0f) ||
        (light->kind != PORT_GL_LIGHT_POINT &&
         light->kind != PORT_GL_LIGHT_SPOT)) {
        return -1;
    }
    g_lights.building[at] = *light;
    g_lights.building_count = at + 1;
    return at;
}

void port_GlLightsEnd(void) {
    memcpy(g_lights.before, g_lights.now,
           (size_t)g_lights.now_count * sizeof(PortGlLight));
    g_lights.before_count = g_lights.now_count;
    memcpy(g_lights.now, g_lights.building,
           (size_t)g_lights.building_count * sizeof(PortGlLight));
    g_lights.now_count = g_lights.building_count;
}

const PortGlLight *port_GlLightsNow(s32 *count) {
    *count = g_lights.now_count;
    return g_lights.now;
}

const PortGlLight *port_GlLightsBefore(s32 *count) {
    *count = g_lights.before_count;
    return g_lights.before;
}
