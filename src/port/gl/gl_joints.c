/* The fighters' joints blended (gl_joints.h; docs/PORT.md, "The joints
 * blended"). */

#include <stdlib.h>
#include <string.h>

#include "common.h"
#include "gl_cloth.h"
#include "gl_joints.h"

/* A joint of the skeleton every fighter's model shares (g_model_bones):
 * bone `a`, the part before it, and bone `b`, whose place at the bind pose
 * is the joint's and whose own x axis runs into its part. The band reaches
 * `reach_a` of part a's length from the joint and `reach_b` of part b's;
 * where b stays rigid (`is_b_rigid`: the head) only part a bends, up to
 * the joint. Its width is the given centile of the distances of part b's
 * vertices in the band from the joint's axis. */
typedef struct {
    s32 a;
    s32 b;
    float reach_a;
    float reach_b;
    s32 is_b_rigid;
    float centile;
} Joint;

static const Joint g_joints[] = {
    /* the neck: the chest's end bends to the head; the head, the face and
     * the game's own vertices between them stay as the game carries them */
    {2, 3, 0.20f, 0.0f, TRUE, 40.0f},
    /* each shoulder: the shoulder's piece carried toward the upper arm,
     * which stays rigid (the arm's top bent toward the shoulder's bone
     * pushed it through a vest: Kannuki's) */
    {4, 5, 0.50f, 0.0f, TRUE, 90.0f},
    {8, 9, 0.50f, 0.0f, TRUE, 90.0f},
    /* the elbows */
    {5, 6, 0.30f, 0.30f, FALSE, 90.0f},
    {9, 10, 0.30f, 0.30f, FALSE, 90.0f},
    /* the waist: the chest and the hips, both on the body's place */
    {2, 12, 0.25f, 0.40f, FALSE, 90.0f},
    /* the hips */
    {12, 13, 0.40f, 0.25f, FALSE, 90.0f},
    {12, 16, 0.40f, 0.25f, FALSE, 90.0f},
    /* the knees */
    {13, 14, 0.25f, 0.25f, FALSE, 90.0f},
    {16, 17, 0.25f, 0.25f, FALSE, 90.0f},
};
#define JOINTS ((s32)(sizeof(g_joints) / sizeof(g_joints[0])))

/* The band's width: full to this times the part's thickness, and nothing
 * from that times WIDTH_OUT. */
#define WIDTH_IN 1.25f
#define WIDTH_OUT 1.75f
/* A vertex moved by less than this share of the way is left as it was. */
#define LEAST_SHARE 0.01f
/* The most a pose's bones are off rigid, in the world's units, for the
 * boxes (the game's matrices are 4096ths: a few thousandths of a turn). */
#define REACH_SLACK 4.0f

static s32 g_joints_on = TRUE;

void port_GlJointsSetOn(s32 is_on) { g_joints_on = is_on ? TRUE : FALSE; }

s32 port_GlJointsIsOn(void) { return g_joints_on; }

/* A 3 by 3 matrix's inverse (row after row), or FALSE. */
static s32 port_GlJointsInvert(const double *m, double *out) {
    double det =
        m[0] * (m[4] * m[8] - m[5] * m[7]) -
        m[1] * (m[3] * m[8] - m[5] * m[6]) + m[2] * (m[3] * m[7] - m[4] * m[6]);

    if (det > -1e-12 && det < 1e-12) {
        return FALSE;
    }
    out[0] = (m[4] * m[8] - m[5] * m[7]) / det;
    out[1] = (m[2] * m[7] - m[1] * m[8]) / det;
    out[2] = (m[1] * m[5] - m[2] * m[4]) / det;
    out[3] = (m[5] * m[6] - m[3] * m[8]) / det;
    out[4] = (m[0] * m[8] - m[2] * m[6]) / det;
    out[5] = (m[2] * m[3] - m[0] * m[5]) / det;
    out[6] = (m[3] * m[7] - m[4] * m[6]) / det;
    out[7] = (m[1] * m[6] - m[0] * m[7]) / det;
    out[8] = (m[0] * m[4] - m[1] * m[3]) / det;
    return TRUE;
}

/* A bone's place and its x axis at the bind pose, from its inverse bind
 * matrix (a column after a column). */
static s32 port_GlJointsBind(const float *ibm, double *place, double *axis) {
    double r[9];
    double inv[9];
    double size;
    s32 i;
    s32 k;

    for (i = 0; i < 3; i++) {
        for (k = 0; k < 3; k++) {
            r[i * 3 + k] = ibm[k * 4 + i];
        }
    }
    if (!port_GlJointsInvert(r, inv)) {
        return FALSE;
    }
    for (i = 0; i < 3; i++) {
        place[i] = -(inv[i * 3] * ibm[12] + inv[i * 3 + 1] * ibm[13] +
                     inv[i * 3 + 2] * ibm[14]);
        axis[i] = inv[i * 3];
    }
    size = port_GlClothRoot(
        axis[0] * axis[0] + axis[1] * axis[1] + axis[2] * axis[2]);
    if (size < 1e-9) {
        return FALSE;
    }
    for (i = 0; i < 3; i++) {
        axis[i] /= size;
    }
    return TRUE;
}

/* The bone that carries a vertex: the one whose term is a vector with no
 * share of its place (the game's vertex of a joint: a place on the parent,
 * an offset the child turns), else the one with the largest share of its
 * place (tools/port_models/geometry.py's carry_bone). */
static s32 port_GlJointsOwner(const PortFighterVertex *v) {
    s32 best = -1;
    double best_size = 0.0;
    s32 k;

    for (k = 0; k < PORT_FIGHTER_TERMS; k++) {
        const float *t = v->term[k];
        double size =
            (double)t[0] * t[0] + (double)t[1] * t[1] + (double)t[2] * t[2];

        if (t[3] > -1e-6f && t[3] < 1e-6f && size > 1e-12 && size > best_size) {
            best = v->term_joint[k];
            best_size = size;
        }
    }
    if (best >= 0) {
        return best % PORT_FIGHTER_FILE_JOINTS;
    }
    best = 0;
    for (k = 1; k < PORT_FIGHTER_TERMS; k++) {
        if (v->term[k][3] > v->term[best][3]) {
            best = k;
        }
    }
    return v->term_joint[best] % PORT_FIGHTER_FILE_JOINTS;
}

static float port_GlJointsSmooth(float t) {
    t = t < 0.0f ? 0.0f : t > 1.0f ? 1.0f : t;
    return t * t * (3.0f - 2.0f * t);
}

/* Where a vertex lies about a joint: along the axis, and away from it. */
static void port_GlJointsWhere(const float *place, const double *at,
                               const double *axis, float *along, float *away) {
    double d[3];
    double s;
    double r;
    s32 i;

    for (i = 0; i < 3; i++) {
        d[i] = (double)place[i] - at[i];
    }
    s = d[0] * axis[0] + d[1] * axis[1] + d[2] * axis[2];
    r = 0.0;
    for (i = 0; i < 3; i++) {
        double e = d[i] - s * axis[i];

        r += e * e;
    }
    *along = (float)s;
    *away = (float)port_GlClothRoot(r);
}

s32 port_GlJointsWeigh(const PortFighterFile *file, float (*band)[4]) {
    float *best;
    u8 *owner;
    s32 banded = 0;
    s32 i;
    s32 j;

    for (i = 0; i < file->vertex_count; i++) {
        band[i][0] = band[i][1] = band[i][2] = 0.0f;
        band[i][3] = 1.0f;
    }
    best = malloc((size_t)file->vertex_count * sizeof(*best));
    owner = malloc((size_t)file->vertex_count);
    if (best == NULL || owner == NULL) {
        free(best);
        free(owner);
        return 0;
    }
    for (i = 0; i < file->vertex_count; i++) {
        best[i] = LEAST_SHARE;
        owner[i] = (u8)port_GlJointsOwner(&file->vertices[i]);
    }
    for (j = 0; j < JOINTS; j++) {
        const Joint *joint = &g_joints[j];
        double at[3];
        double axis[3];
        double unused[3];
        float extent_a = 0.0f;
        float extent_b = 0.0f;
        float lo;
        float hi;
        float width_lo = 0.0f;
        float width_hi = 0.0f;
        float width;
        s32 count_a = 0;
        s32 count_b = 0;
        s32 near = 0;
        s32 step;

        if (!file->has_joint[joint->a] || !file->has_joint[joint->b] ||
            !port_GlJointsBind(file->inverse_bind[joint->b], at, axis) ||
            !port_GlJointsBind(file->inverse_bind[joint->a], unused, unused)) {
            continue;
        }
        /* each part's length from the joint, by its own vertices */
        for (i = 0; i < file->vertex_count; i++) {
            float along;
            float away;

            if (owner[i] != joint->a && owner[i] != joint->b) {
                continue;
            }
            port_GlJointsWhere(
                file->vertices[i].place, at, axis, &along, &away);
            if (owner[i] == joint->a) {
                count_a++;
                extent_a = -along > extent_a ? -along : extent_a;
            } else {
                count_b++;
                extent_b = along > extent_b ? along : extent_b;
                width_hi = away > width_hi ? away : width_hi;
            }
        }
        if (count_a == 0 || count_b == 0) {
            continue;
        }
        lo = -joint->reach_a * (extent_a > 1.0f ? extent_a : 1.0f);
        hi = joint->reach_b * (extent_b > 1.0f ? extent_b : 1.0f);
        /* the second part's thickness at the joint: the centile of its
         * vertices' distances from the axis within the band (on both sides
         * of the joint where its own side has no length), found by halves */
        for (i = 0; i < file->vertex_count; i++) {
            float along;
            float away;

            if (owner[i] != joint->b) {
                continue;
            }
            port_GlJointsWhere(
                file->vertices[i].place, at, axis, &along, &away);
            if (along >= lo && along <= (hi > -lo ? hi : -lo)) {
                near++;
            }
        }
        if (near < 3) {
            continue;
        }
        for (step = 0; step < 32; step++) {
            float middle = 0.5f * (width_lo + width_hi);
            s32 below = 0;

            for (i = 0; i < file->vertex_count; i++) {
                float along;
                float away;

                if (owner[i] != joint->b) {
                    continue;
                }
                port_GlJointsWhere(
                    file->vertices[i].place, at, axis, &along, &away);
                if (along >= lo && along <= (hi > -lo ? hi : -lo) &&
                    away <= middle) {
                    below++;
                }
            }
            if ((float)below * 100.0f >= joint->centile * (float)near) {
                width_hi = middle;
            } else {
                width_lo = middle;
            }
        }
        width = width_hi > 1.0f ? width_hi : 1.0f;
        for (i = 0; i < file->vertex_count; i++) {
            float along;
            float away;
            float field;
            float full;
            float own;
            float share;
            float moved;

            if (owner[i] != joint->a &&
                (owner[i] != joint->b || joint->is_b_rigid)) {
                continue;
            }
            port_GlJointsWhere(
                file->vertices[i].place, at, axis, &along, &away);
            field = joint->is_b_rigid
                        ? port_GlJointsSmooth((along - lo) / -lo)
                        : port_GlJointsSmooth((along - lo) / (hi - lo));
            full = 1.0f - port_GlJointsSmooth((away - WIDTH_IN * width) /
                                              ((WIDTH_OUT - WIDTH_IN) * width));
            own = owner[i] == joint->b ? 1.0f : 0.0f;
            share = own + (field - own) * full;
            moved = share > own ? share - own : own - share;
            if (moved > best[i]) {
                best[i] = moved;
                band[i][0] = (float)joint->a;
                band[i][1] = (float)joint->b;
                band[i][2] = share;
                band[i][3] = 0.0f;
            }
        }
    }
    for (i = 0; i < file->vertex_count; i++) {
        banded += band[i][3] == 0.0f;
    }
    free(best);
    free(owner);
    return banded;
}

float port_GlJointsReach(const PortFighterFile *file, const float *band,
                         const float *place, s32 bone) {
    double at[3];
    double from[3];
    double axis[3];
    double d0 = 0.0;
    double d1 = 0.0;
    s32 i;

    if (band[3] != 0.0f ||
        !port_GlJointsBind(file->inverse_bind[(s32)band[1]], at, axis) ||
        !port_GlJointsBind(file->inverse_bind[bone], from, axis)) {
        return -1.0f;
    }
    /* turned about the joint, it stays as far from the joint; the joint
     * is as far from the bone's place as at the bind pose */
    for (i = 0; i < 3; i++) {
        d0 += ((double)place[i] - at[i]) * ((double)place[i] - at[i]);
        d1 += (at[i] - from[i]) * (at[i] - from[i]);
    }
    return (float)(port_GlClothRoot(d0) + port_GlClothRoot(d1)) + REACH_SLACK;
}

/* A rotation matrix's unit quaternion (x, y, z, w), row after row. */
static void port_GlJointsQuaternion(const double *m, double *q) {
    double trace = m[0] + m[4] + m[8];
    double s;
    double size;
    s32 i;

    if (trace > 0.0) {
        s = port_GlClothRoot(trace + 1.0) * 2.0;
        q[0] = (m[7] - m[5]) / s;
        q[1] = (m[2] - m[6]) / s;
        q[2] = (m[3] - m[1]) / s;
        q[3] = 0.25 * s;
    } else if (m[0] > m[4] && m[0] > m[8]) {
        s = port_GlClothRoot(1.0 + m[0] - m[4] - m[8]) * 2.0;
        q[0] = 0.25 * s;
        q[1] = (m[1] + m[3]) / s;
        q[2] = (m[2] + m[6]) / s;
        q[3] = (m[7] - m[5]) / s;
    } else if (m[4] > m[8]) {
        s = port_GlClothRoot(1.0 + m[4] - m[0] - m[8]) * 2.0;
        q[0] = (m[1] + m[3]) / s;
        q[1] = 0.25 * s;
        q[2] = (m[5] + m[7]) / s;
        q[3] = (m[2] - m[6]) / s;
    } else {
        s = port_GlClothRoot(1.0 + m[8] - m[0] - m[4]) * 2.0;
        q[0] = (m[2] + m[6]) / s;
        q[1] = (m[5] + m[7]) / s;
        q[2] = 0.25 * s;
        q[3] = (m[3] - m[1]) / s;
    }
    size =
        port_GlClothRoot(q[0] * q[0] + q[1] * q[1] + q[2] * q[2] + q[3] * q[3]);
    for (i = 0; i < 4; i++) {
        q[i] /= size;
    }
}

void port_GlJointsDuals(const float (*inverse_bind)[16],
                        const float (*world)[16], float (*dual)[4]) {
    s32 b;

    for (b = 0; b < PORT_FIGHTER_FILE_JOINTS; b++) {
        const float *w = world[b];
        const float *ibm = inverse_bind[b];
        double m[9];
        double q[4];
        double place[3];
        double axis[3];
        double t[3];
        double turned[3];
        s32 r;
        s32 c;
        s32 k;

        /* the skinning matrix's rotation, row after row */
        for (r = 0; r < 3; r++) {
            for (c = 0; c < 3; c++) {
                double sum = 0.0;

                for (k = 0; k < 3; k++) {
                    sum += (double)w[k * 4 + r] * (double)ibm[c * 4 + k];
                }
                m[r * 3 + c] = sum;
            }
        }
        port_GlJointsQuaternion(m, q);
        /* made rigid about the bone's own place: that place goes where the
         * bone's world matrix puts it */
        if (!port_GlJointsBind(ibm, place, axis)) {
            place[0] = place[1] = place[2] = 0.0;
        }
        for (r = 0; r < 3; r++) {
            double x = q[0];
            double y = q[1];
            double z = q[2];
            double s = q[3];
            double rot[9];

            rot[0] = 1.0 - 2.0 * (y * y + z * z);
            rot[1] = 2.0 * (x * y - z * s);
            rot[2] = 2.0 * (x * z + y * s);
            rot[3] = 2.0 * (x * y + z * s);
            rot[4] = 1.0 - 2.0 * (x * x + z * z);
            rot[5] = 2.0 * (y * z - x * s);
            rot[6] = 2.0 * (x * z - y * s);
            rot[7] = 2.0 * (y * z + x * s);
            rot[8] = 1.0 - 2.0 * (x * x + y * y);
            turned[r] = rot[r * 3] * place[0] + rot[r * 3 + 1] * place[1] +
                        rot[r * 3 + 2] * place[2];
            t[r] = (double)w[12 + r] - turned[r];
        }
        /* the dual part: half the translation times the rotation */
        dual[b * 2][0] = (float)q[0];
        dual[b * 2][1] = (float)q[1];
        dual[b * 2][2] = (float)q[2];
        dual[b * 2][3] = (float)q[3];
        dual[b * 2 + 1][0] =
            (float)(0.5 * (t[0] * q[3] + t[1] * q[2] - t[2] * q[1]));
        dual[b * 2 + 1][1] =
            (float)(0.5 * (t[1] * q[3] + t[2] * q[0] - t[0] * q[2]));
        dual[b * 2 + 1][2] =
            (float)(0.5 * (t[2] * q[3] + t[0] * q[1] - t[1] * q[0]));
        dual[b * 2 + 1][3] =
            (float)(-0.5 * (t[0] * q[0] + t[1] * q[1] + t[2] * q[2]));
    }
}
