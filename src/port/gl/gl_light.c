/* The game's own lighting worked out at each pixel (gl_light.h;
 * docs/PORT.md, "Lighting"). What is here is the setting and the
 * arithmetic of the ground's smooth normals; the shaders that light at
 * the pixel are the ground's (gl_stage.c) and the fighters'
 * (gl_fighter.c), each beside the NCS it already made per face or per
 * vertex. Nothing here changes a value the game computes. */
#include <stddef.h>
#include <stdlib.h>

#include "common.h"
#include "gl_light.h"

static s32 g_light_is_smooth;

void port_GlLightSetSmooth(s32 on) { g_light_is_smooth = on != 0; }

s32 port_GlLightIsSmooth(void) { return g_light_is_smooth; }

/* A normal of the coprocessor's: 1 is 4096. */
#define NORMAL_ONE 4096.0f
/* The odd number a key is multiplied by to mix its bits (as
 * src/port/remaster.c). */
#define MIX 0x9E3779B1U
#define NO_VERTEX (-1)
/* The most steps the square root takes; it is done in far fewer. */
#define ROOT_STEPS 64

/* The nearest whole number. (The window's C library is not asked for
 * its arithmetic: include/ has a math.h of the game's own.) */
static s32 port_LightRound(float value) {
    return (s32)(value >= 0.0f ? value + 0.5f : value - 0.5f);
}

/* A square root, by Newton's steps from above. */
static float port_LightRoot(float value) {
    float root;
    float last = 0.0f;
    s32 i;

    if (value <= 0.0f) {
        return 0.0f;
    }
    root = value > 1.0f ? value : 1.0f;
    for (i = 0; i < ROOT_STEPS && root != last; i++) {
        last = root;
        root = 0.5f * (root + value / root);
    }
    return root;
}

/* Where a vertex is, to the unit: the key the corners are found by. */
static u32 port_LightKey(const float *place) {
    u32 key = (u32)port_LightRound(place[0]);

    key = key * MIX ^ (u32)port_LightRound(place[1]);
    key = key * MIX ^ (u32)port_LightRound(place[2]);
    key ^= key >> 15;
    return key * MIX;
}

static s32 port_LightSamePlace(const float *a, const float *b) {
    return port_LightRound(a[0]) == port_LightRound(b[0]) &&
           port_LightRound(a[1]) == port_LightRound(b[1]) &&
           port_LightRound(a[2]) == port_LightRound(b[2]);
}

/* A face's normal made one long, and how much of the surface its
 * triangle is: what each corner's share of the mean is weighed by. */
typedef struct {
    float n[3];
    float area;
} LightFace;

s32 port_GlLightSmoothNormals(
    const float *place, s32 stride, s16 *normal, s32 normal_stride, s32 count) {
    u32 slots = 1;
    s32 *first;
    s32 *next;
    LightFace *face;
    float(*sum)[3];
    s32 i;

    if (count <= 0) {
        return TRUE;
    }
    while (slots < (u32)count * 2) {
        slots *= 2;
    }
    first = malloc(slots * sizeof(*first));
    next = malloc((size_t)count * sizeof(*next));
    face = malloc((size_t)count * sizeof(*face));
    sum = malloc((size_t)count * sizeof(*sum));
    if (first == NULL || next == NULL || face == NULL || sum == NULL) {
        free(first);
        free(next);
        free(face);
        free(sum);
        return FALSE;
    }
    for (i = 0; i < (s32)slots; i++) {
        first[i] = NO_VERTEX;
    }
    /* each vertex's face: its normal, and its triangle's area */
    for (i = 0; i < count; i++) {
        const s16 *n = &normal[i * normal_stride];
        const float *a = &place[(i - i % 3) * stride];
        const float *b = a + stride;
        const float *c = b + stride;
        float e0[3];
        float e1[3];
        float cx;
        float cy;
        float cz;
        float length;
        s32 k;
        u32 slot;

        length = port_LightRoot(
            (float)n[0] * n[0] + (float)n[1] * n[1] + (float)n[2] * n[2]);
        for (k = 0; k < 3; k++) {
            face[i].n[k] = length > 0.0f ? (float)n[k] / length : 0.0f;
            e0[k] = b[k] - a[k];
            e1[k] = c[k] - a[k];
        }
        cx = e0[1] * e1[2] - e0[2] * e1[1];
        cy = e0[2] * e1[0] - e0[0] * e1[2];
        cz = e0[0] * e1[1] - e0[1] * e1[0];
        face[i].area = 0.5f * port_LightRoot(cx * cx + cy * cy + cz * cz);
        /* the corners by where they are */
        slot = port_LightKey(&place[i * stride]) & (slots - 1);
        next[i] = first[slot];
        first[slot] = i;
    }
    /* each vertex: the mean of the faces at its corner that turn less
     * than the crease from its own, each weighed by its area */
    for (i = 0; i < count; i++) {
        const float *at = &place[i * stride];
        u32 slot = port_LightKey(at) & (slots - 1);
        s32 j;

        sum[i][0] = 0.0f;
        sum[i][1] = 0.0f;
        sum[i][2] = 0.0f;
        if (face[i].n[0] == 0.0f && face[i].n[1] == 0.0f &&
            face[i].n[2] == 0.0f) {
            continue;
        }
        for (j = first[slot]; j != NO_VERTEX; j = next[j]) {
            float turn =
                face[i].n[0] * face[j].n[0] + face[i].n[1] * face[j].n[1] +
                face[i].n[2] * face[j].n[2];
            /* a face with no area still counts a little, so that a
             * vertex is never left with nothing */
            float weight = face[j].area + 1.0f;

            if (turn < PORT_LIGHT_CREASE_COS ||
                !port_LightSamePlace(at, &place[j * stride])) {
                continue;
            }
            sum[i][0] += face[j].n[0] * weight;
            sum[i][1] += face[j].n[1] * weight;
            sum[i][2] += face[j].n[2] * weight;
        }
    }
    for (i = 0; i < count; i++) {
        s16 *n = &normal[i * normal_stride];
        float length =
            port_LightRoot(sum[i][0] * sum[i][0] + sum[i][1] * sum[i][1] +
                           sum[i][2] * sum[i][2]);
        s32 k;

        if (length <= 0.0f) {
            continue;
        }
        for (k = 0; k < 3; k++) {
            n[k] = (s16)port_LightRound(sum[i][k] / length * NORMAL_ONE);
        }
    }
    free(first);
    free(next);
    free(face);
    free(sum);
    return TRUE;
}
