/* The stage's walls faded where they stand between the camera and a
 * fighter (gl_see.h). What is here is where the fighters are; the fading
 * itself is the ground mesh's shader (gl_stage.c), which is the one place
 * a face of the stage knows where it is in the world. */
#include <stddef.h>

#include "common.h"
#include "gl_see.h"
#include "port_fighter.h"

/* How far about his bones a fighter's cylinder reaches, in the world's
 * units: past the widest of them by this, and at least the least. The
 * bones are the joints, so the flesh and the clothes about them, a held
 * sword's reach and the hem of a hakama are outside them. */
#define SEE_PAD 220.0f
#define SEE_RADIUS_LEAST 420.0f
#define SEE_RADIUS_MOST 1100.0f
/* and above the highest bone, which is the neck's or the head's own
 * joint: the head and the hair */
#define SEE_HEAD 260.0f

static s32 g_see_on;

/* A square root, to a part in a million: Newton's, from above. (The
 * window's build has no mathematics library to link.) */
static double port_GlSeeRoot(double value) {
    double root = value > 1.0 ? value : 1.0;
    s32 i;

    if (value <= 0.0) {
        return 0.0;
    }
    for (i = 0; i < 64; i++) {
        double next = 0.5 * (root + value / root);

        if (next >= root) {
            break;
        }
        root = next;
    }
    return root;
}

void port_GlSeeSetOn(s32 on) { g_see_on = on; }

s32 port_GlSeeIsOn(void) { return g_see_on; }

s32 port_GlSeeFighters(PortSeeFighter *out) {
    u32 serial = port_FighterSerial();
    s32 count = 0;
    s32 slot;

    if (!g_see_on) {
        return 0;
    }
    for (slot = 0; slot < PORT_FIGHTER_SLOTS && count < PORT_SEE_FIGHTERS;
         slot++) {
        const PortFighterView *view = port_GetFighterView(slot);
        double x = 0.0;
        double z = 0.0;
        double top = 0.0;
        double bottom = 0.0;
        double widest = 0.0;
        s32 bone;

        /* a slot whose body the frame drew: the one being drawn, or the
         * one before it, which a picture between two frames is of */
        if (view == NULL || view->model_id < 0 || serial - view->serial > 1) {
            continue;
        }
        for (bone = 0; bone < PORT_FIGHTER_BONES; bone++) {
            const s32 *t = view->bone[bone].t;

            x += t[0];
            z += t[2];
            if (bone == 0 || t[1] < top) {
                top = t[1];
            }
            if (bone == 0 || t[1] > bottom) {
                bottom = t[1];
            }
        }
        x /= PORT_FIGHTER_BONES;
        z /= PORT_FIGHTER_BONES;
        for (bone = 0; bone < PORT_FIGHTER_BONES; bone++) {
            const s32 *t = view->bone[bone].t;
            double dx = t[0] - x;
            double dz = t[2] - z;
            double apart = port_GlSeeRoot(dx * dx + dz * dz);

            if (apart > widest) {
                widest = apart;
            }
        }
        out[count].x = (float)x;
        out[count].z = (float)z;
        out[count].top = (float)top - SEE_HEAD;
        out[count].bottom = (float)bottom;
        out[count].radius = (float)widest + SEE_PAD;
        if (out[count].radius < SEE_RADIUS_LEAST) {
            out[count].radius = SEE_RADIUS_LEAST;
        }
        if (out[count].radius > SEE_RADIUS_MOST) {
            out[count].radius = SEE_RADIUS_MOST;
        }
        count++;
    }
    return count;
}
