/* The time of day the remastered picture lights a stage at (gl_time.h;
 * docs/PORT.md, "The time of day"). Nothing here is the game's state: the
 * time is read of the stage choice (port_StageTime) and the stage's own
 * light of the game's setup, and what is worked out is the picture's. */
#include <string.h>

#include "common.h"
#include "gl_sky.h"
#include "gl_time.h"
#include "light.h"
#include "port_lights.h"
#include "port_remaster.h"
#include "port_stage_choice.h"

/* the console's 1.12 numbers */
#define ONE_12 4096.0f
/* a colour's channel at its brightest */
#define CHANNEL_MAX 255.0f
#define PI_F 3.14159265f
/* The stage's own light on its floor is held to this range before the
 * time's is worked out from it, so a stage lit very dim or very bright
 * keeps its place without a day of black or of white. */
#define FLOOR_LEAST 60.0f
#define FLOOR_MOST 230.0f
/* How much of the time's look a stage under a roof takes (a hall, a cave,
 * a garage: gl_sky.c's closed stages): its light through its doors and
 * windows, not a sky's, and the sun's way its own. */
#define INDOOR_SHARE 0.5f
/* How much of the stage's own hue its ambient keeps at any time. */
#define OWN_HUE_SHARE 0.25f

/* A time's look (judged on the pictures of docs/PORT.md, "The time of
 * day"; the owner's eye wanted): the sun's height and its bearing from the
 * stage's own sun's, in degrees; its colour and the ambient's hues; the
 * stage's own floor light times `floor`, of which `sun_share` is the
 * sun's, the sun's own light at most `sun_most`; the fog, the sky, the
 * stars, the moon, the shadows' softness, the bloom and the stage's own
 * lights' brightness; and which bit of a light's `times` it is. */
typedef struct {
    float height;
    float bearing;
    float sun_hue[3];
    float back_hue[3];
    float floor;
    float sun_share;
    float sun_most;
    float fog[3];
    float fog_amount;
    float horizon[3];
    float zenith[3];
    float stars;
    float has_moon;
    float soft;
    float bloom;
    float lights;
    u32 bit;
} TimeLook;

static const TimeLook g_time_looks[PORT_TIME_COUNT] = {
    /* the stage's own: night, as the game has every stage */
    {0},
    /* dawn: a low sun turned back from the stage's own, pale gold over a
     * cool lilac morning */
    {
        .height = 8.0f,
        .bearing = -50.0f,
        .sun_hue = {1.0f, 0.74f, 0.52f},
        .back_hue = {0.62f, 0.64f, 0.82f},
        .floor = 0.8f,
        .sun_share = 0.42f,
        .sun_most = 215.0f,
        .fog = {0.66f, 0.58f, 0.62f},
        .fog_amount = 0.7f,
        .horizon = {0.9f, 0.66f, 0.52f},
        .zenith = {0.3f, 0.42f, 0.66f},
        .stars = 0.1f,
        .has_moon = 0.0f,
        .soft = 1.3f,
        .bloom = 1.0f,
        .lights = 0.7f,
        .bit = PORT_LIGHT_TIME_DAWN,
    },
    /* day: a high white sun, a sky-blue ambient, a light haze */
    {
        .height = 55.0f,
        .bearing = 0.0f,
        .sun_hue = {1.0f, 0.96f, 0.89f},
        .back_hue = {0.7f, 0.8f, 1.0f},
        .floor = 1.15f,
        .sun_share = 0.65f,
        .sun_most = 235.0f,
        .fog = {0.7f, 0.77f, 0.86f},
        .fog_amount = 0.5f,
        .horizon = {0.74f, 0.82f, 0.92f},
        .zenith = {0.24f, 0.46f, 0.84f},
        .stars = 0.0f,
        .has_moon = 0.0f,
        .soft = 1.0f,
        .bloom = 0.6f,
        .lights = 0.4f,
        .bit = PORT_LIGHT_TIME_DAY,
    },
    /* dusk: a low sun turned on from the stage's own, deep orange over a
     * violet dark */
    {
        .height = 7.0f,
        .bearing = 55.0f,
        .sun_hue = {1.0f, 0.56f, 0.3f},
        .back_hue = {0.62f, 0.52f, 0.66f},
        .floor = 0.72f,
        .sun_share = 0.42f,
        .sun_most = 205.0f,
        .fog = {0.42f, 0.32f, 0.38f},
        .fog_amount = 0.75f,
        .horizon = {0.88f, 0.5f, 0.34f},
        .zenith = {0.18f, 0.15f, 0.36f},
        .stars = 0.35f,
        .has_moon = 0.0f,
        .soft = 1.3f,
        .bloom = 1.25f,
        .lights = 1.2f,
        .bit = PORT_LIGHT_TIME_DUSK,
    },
    /* night: a weak, cool moon, soft; a dark between the lights; the
     * stage's own lights the brightest of the day */
    {
        .height = 11.0f,
        .bearing = 25.0f,
        .sun_hue = {0.62f, 0.74f, 1.0f},
        .back_hue = {0.36f, 0.46f, 0.78f},
        .floor = 0.4f,
        .sun_share = 0.55f,
        .sun_most = 115.0f,
        .fog = {0.03f, 0.045f, 0.09f},
        .fog_amount = 0.85f,
        .horizon = {0.05f, 0.07f, 0.13f},
        .zenith = {0.005f, 0.01f, 0.03f},
        .stars = 1.0f,
        .has_moon = 1.0f,
        .soft = 2.5f,
        .bloom = 1.6f,
        .lights = 1.6f,
        .bit = PORT_LIGHT_TIME_NIGHT,
    },
};

static struct {
    s32 is_made;
    s32 stage;
    s32 time;
    LightSetup own; /* the game's setup it was worked out from */
    LightSetup setup;
    PortGlTime look;
} g_time;

/* ---- the arithmetic (the window's C library is not asked for it) ---- */

static float port_TimeRoot(float value) {
    float root;
    float last = 0.0f;
    s32 i;

    if (value <= 0.0f) {
        return 0.0f;
    }
    root = value > 1.0f ? value : 1.0f;
    for (i = 0; i < 64 && root != last; i++) {
        last = root;
        root = 0.5f * (root + value / root);
    }
    return root;
}

/* the sine of `a` radians, by its series after bringing it to -pi..pi */
static float port_TimeSin(float a) {
    float term;
    float sum;
    s32 i;

    while (a > PI_F) {
        a -= 2.0f * PI_F;
    }
    while (a < -PI_F) {
        a += 2.0f * PI_F;
    }
    term = a;
    sum = a;
    for (i = 1; i < 10; i++) {
        term *= -a * a / (float)((2 * i) * (2 * i + 1));
        sum += term;
    }
    return sum;
}

static float port_TimeCos(float a) { return port_TimeSin(a + 0.5f * PI_F); }

/* the angle of (`x`, `z`) from the z axis towards x, radians (atan2 of
 * x over z, to about a quarter of a degree) */
static float port_TimeBearing(float x, float z) {
    float ax = x < 0.0f ? -x : x;
    float az = z < 0.0f ? -z : z;
    float r;
    float a;

    if (ax == 0.0f && az == 0.0f) {
        return 0.0f;
    }
    r = ax < az ? ax / az : az / ax;
    a = r * (0.25f * PI_F + 0.273f * (1.0f - r));
    if (ax > az) {
        a = 0.5f * PI_F - a;
    }
    if (z < 0.0f) {
        a = PI_F - a;
    }
    return x < 0.0f ? -a : a;
}

static float port_TimeLuma(const float *c) {
    return 0.299f * c[0] + 0.587f * c[1] + 0.114f * c[2];
}

static float port_TimeMix(float a, float b, float t) { return a + (b - a) * t; }

/* ---- the look ---- */

/* The light of `setup` on a level floor, each channel, in bytes, as the
 * coprocessor's NCS has it (before its clamp). */
static void port_TimeFloorOf(const LightSetup *setup, float *out) {
    s32 i;
    s32 c;

    for (c = 0; c < 3; c++) {
        out[c] = (float)setup->back[c];
    }
    for (i = 0; i < LIGHT_SETUP_LIGHTS; i++) {
        /* a level floor's normal is up, which is minus y */
        float turn = -(float)setup->lmat.m[i][1] / ONE_12;

        if (turn <= 0.0f) {
            continue;
        }
        for (c = 0; c < 3; c++) {
            out[c] += (float)setup->cmat.m[c][i] * turn / 16.0f;
        }
    }
}

static void port_TimeMake(const LightSetup *own, s32 stage, s32 time) {
    const TimeLook *at = &g_time_looks[time];
    PortGlTime *look = &g_time.look;
    LightSetup *setup = &g_time.setup;
    float to_sun[3];
    float own_sun[3];
    float own_back[3];
    float own_floor[3];
    float now_floor[3];
    float length;
    float floor;
    float rise;
    float bearing;
    float height;
    float sun_light;
    float back_light;
    float own_back_light;
    float share;
    s32 c;

    memset(look, 0, sizeof(*look));
    *setup = *own;
    look->time = time;
    look->is_open = port_GlSkyIsOpen(stage);
    look->soft = 1.0f;
    look->bloom = 1.0f;
    look->floor[0] = look->floor[1] = look->floor[2] = 1.0f;
    if (time <= PORT_TIME_OWN || time >= PORT_TIME_COUNT) {
        return;
    }
    look->on = TRUE;
    share = look->is_open ? 1.0f : INDOOR_SHARE;
    /* the stage's own sun: where it is, its colour, its ambient */
    for (c = 0; c < 3; c++) {
        to_sun[c] = (float)own->lmat.m[LIGHT_SUN][c] / ONE_12;
        own_sun[c] = (float)own->cmat.m[c][LIGHT_SUN] / 16.0f;
        own_back[c] = (float)own->back[c];
    }
    length = port_TimeRoot(
        to_sun[0] * to_sun[0] + to_sun[1] * to_sun[1] + to_sun[2] * to_sun[2]);
    if (length < 0.01f) {
        to_sun[0] = 0.4f;
        to_sun[1] = -0.7f;
        to_sun[2] = 0.5f;
    }
    port_TimeFloorOf(own, own_floor);
    floor = port_TimeLuma(own_floor);
    floor = floor < FLOOR_LEAST  ? FLOOR_LEAST
            : floor > FLOOR_MOST ? FLOOR_MOST
                                 : floor;
    floor *= at->floor;
    /* the time's sun: its height, and the stage's own bearing turned */
    height = at->height * PI_F / 180.0f;
    bearing =
        port_TimeBearing(to_sun[0], to_sun[2]) + at->bearing * PI_F / 180.0f;
    rise = port_TimeSin(height);
    if (look->is_open) {
        look->to_sun[0] = port_TimeCos(height) * port_TimeSin(bearing);
        look->to_sun[1] = -rise;
        look->to_sun[2] = port_TimeCos(height) * port_TimeCos(bearing);
    } else {
        /* under a roof the sun's way is the stage's own */
        for (c = 0; c < 3; c++) {
            look->to_sun[c] = to_sun[c] / (length < 0.01f ? 1.0f : length);
        }
        rise = -look->to_sun[1] > 0.1f ? -look->to_sun[1] : 0.1f;
    }
    /* how bright the sun is where it meets a face, so that its share of
     * the floor is the look's, no brighter than the look allows */
    sun_light = at->sun_share * floor / rise;
    if (sun_light > at->sun_most) {
        sun_light = at->sun_most;
    }
    back_light = floor - sun_light * rise;
    if (back_light < 0.15f * floor) {
        back_light = 0.15f * floor;
    }
    own_back_light = port_TimeLuma(own_back);
    for (c = 0; c < 3; c++) {
        float sun = at->sun_hue[c] * sun_light / port_TimeLuma(at->sun_hue);
        float back = at->back_hue[c] * back_light / port_TimeLuma(at->back_hue);

        /* the stage keeps a share of its own ambient's hue */
        if (own_back_light > 1.0f) {
            back = port_TimeMix(
                back, own_back[c] * back_light / own_back_light, OWN_HUE_SHARE);
        }
        look->sun[c] = port_TimeMix(own_sun[c], sun, share);
        look->back[c] = port_TimeMix(own_back[c], back, share);
        look->fog[c] = at->fog[c];
        look->horizon[c] = at->horizon[c];
        look->zenith[c] = at->zenith[c];
    }
    look->fog_amount = look->is_open ? at->fog_amount : 0.0f;
    look->stars = at->stars;
    look->has_moon = at->has_moon;
    look->soft = at->soft;
    look->bloom = at->bloom;
    /* the setup the picture lights with in place of the stage's own */
    for (c = 0; c < 3; c++) {
        float sun = look->sun[c] * 16.0f;
        float back = look->back[c];

        setup->lmat.m[LIGHT_SUN][c] = (s16)(look->to_sun[c] * ONE_12);
        setup->cmat.m[c][LIGHT_SUN] =
            (s16)(sun > 4095.0f ? 4095.0f
                  : sun < 0.0f  ? 0.0f
                                : sun);
        setup->back[c] = (u8)(back > CHANNEL_MAX ? CHANNEL_MAX
                              : back < 0.0f      ? 0.0f
                                                 : back);
    }
    port_TimeFloorOf(setup, now_floor);
    for (c = 0; c < 3; c++) {
        look->floor[c] =
            now_floor[c] / (own_floor[c] > 8.0f ? own_floor[c] : 8.0f);
    }
}

const PortGlTime *port_GlTime(void) {
    const LightSetup *own = (const LightSetup *)port_RemasterTileLights();
    s32 stage = port_RemasterStageNumber();
    s32 time = port_StageTime();

    if (own == NULL) {
        time = PORT_TIME_OWN;
    }
    if (!g_time.is_made || g_time.stage != stage || g_time.time != time ||
        (own != NULL && memcmp(&g_time.own, own, sizeof(*own)) != 0)) {
        static const LightSetup none;

        if (own == NULL) {
            own = &none;
        }
        g_time.is_made = TRUE;
        g_time.stage = stage;
        g_time.time = time;
        g_time.own = *own;
        port_TimeMake(own, stage, time);
    }
    return &g_time.look;
}

const void *port_GlTimeTileLights(void) {
    const PortGlTime *look = port_GlTime();

    return look->on ? (const void *)&g_time.setup : port_RemasterTileLights();
}

float port_GlTimeLightGain(u32 times) {
    s32 time = port_StageTime();
    const TimeLook *at;

    if (time <= PORT_TIME_OWN || time >= PORT_TIME_COUNT) {
        /* the stage's own time: the game's night */
        return times == 0 || (times & PORT_LIGHT_TIME_NIGHT) != 0 ? 1.0f : 0.0f;
    }
    at = &g_time_looks[time];
    if (times != 0 && (times & at->bit) == 0) {
        return 0.0f;
    }
    return at->lights;
}

void port_GlTimeSetupFloats(
    const void *setup, float *light, float *color, float *back) {
    const LightSetup *lights = (const LightSetup *)setup;
    s32 i;

    for (i = 0; i < 9; i++) {
        light[i] = (float)lights->lmat.m[i / 3][i % 3] / ONE_12;
        color[i] = (float)lights->cmat.m[i / 3][i % 3] / ONE_12;
    }
    for (i = 0; i < 3; i++) {
        back[i] = (float)lights->back[i];
    }
}
