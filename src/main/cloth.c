/* Cloth, the simulation: points joined by springs, hung from a moving
 * matrix (see cloth.h). The first half makes a cloth from a mesh's vertices
 * and faces; the second steps the cloths a frame at a time, working in the
 * scratchpad. */
#define INCLUDE_ASM_USE_MACRO_INC 1
#include "common.h"
#include "include_asm.h"
#include <psxsdk/inline_o.h>
#include <psxsdk/gtemac.h>
#include "bb2.h"

/* Defined further down. */
static u8 g_cloth_peak;
static void cloth_SolveSprings(Cloth *cloth);
static void cloth_Snap(ClothUpdate *update, Cloth *cloth);

Cloth g_cloths[CLOTH_COUNT];

/* What a step keeps in the scratchpad. Colliders are in quarter world
 * units here, the ground in the points' 128ths. */
typedef struct {
    /* 0x00 the vector being measured: a spring from end to end, or a point
     * from an ellipsoid's first focus */
    s32 delta[3];
    /* 0x0C the squares of `delta`, or the point from the second focus */
    s32 delta2[3];
    /* 0x18 where a point's vertex is now, then the point's new velocity */
    s32 work[3];
    /* 0x24 how far the bone moved this step, which every point is carried
     * along by */
    s32 carry[3];
    s32 squares[3];     /* 0x30 */
    s32 pos[3];         /* 0x3C the point */
    s32 near_pos[3];    /* 0x48 the point, in the colliders' units */
    s32 pad54[3];       /* 0x54 not touched */
    s32 collider_count; /* 0x60 */
    s32 foci[CLOTH_COLLIDERS][2][3]; /* 0x64 */
    s32 reach[CLOTH_COLLIDERS];      /* 0xAC */
    s32 ground_y;                    /* 0xB8 */
    /* 0xBC each spring's pull on its first point; its second gets the
     * opposite */
    s32 pull[0][3];
} ClothScratch;

#define CLOTH_SPAD ((ClothScratch *)SCRATCHPAD)

/* The words of scratchpad the fight has saved around a step. */
#define CLOTH_SCRATCH_SAVE_WORDS 0xF8

/* What gravity adds to a loose point's fall every step, and to the fall of
 * a tied point that is not tied at all tightly. */
#define CLOTH_GRAVITY 400
#define CLOTH_TIED_GRAVITY 25

/* A point's distance from the middle of the held points is kept in units
 * of 256 of the points' and no larger than this. */
#define CLOTH_CENTER_DIST_MAX 0x100

/* How much of the bone's movement the points are carried along by: all of
 * one of this many world units or more, three quarters of none at all, and
 * in between by the distance. */
#define CLOTH_CARRY_ONE 0x100
#define CLOTH_CARRY_ALL_DISTANCE 500

/* A point under the ground is pushed up by an eighth of its depth, and one
 * deeper than 100 world units by this much, which is less (the original's
 * behaviour). */
#define CLOTH_GROUND_DEEP (100 << 7)
#define CLOTH_GROUND_DEEP_PUSH 0x400

/* The hardest push out of an ellipsoid: that of a point 32 of the
 * colliders' units inside. */
#define CLOTH_PUSH_MAX 0x400000

u32 g_cloth_scratch_save[CLOTH_SCRATCH_SAVE_WORDS];

void cloth_FreeAll(void) {
    s32 i;
    for (i = 0; i < CLOTH_COUNT; i++) {
        g_cloths[i].vertices = NULL;
    }
}

/* The sizes of a ClothPoint and a ClothSpring. */
s32 cloth_BufferSize(s32 point_count, s32 spring_count) {
    return (point_count << 6) + (spring_count << 4);
}

/* The distance between two points, for points near each other. */
static s32 cloth_Distance(s32 *from, s32 *to) {
    VECTOR in;
    VECTOR out;

    in.vx = (from[0] - to[0]) >> 2;
    in.vy = (from[1] - to[1]) >> 2;
    in.vz = (from[2] - to[2]) >> 2;
    Square12(&in, &out);
    return SquareRoot12(out.vx + out.vy + out.vz) << 2;
}

/* The same, less exact, for points up to four times as far apart. */
static s32 cloth_DistanceFar(s32 *from, s32 *to) {
    VECTOR in;
    VECTOR out;

    in.vx = (from[0] - to[0]) >> 4;
    in.vy = (from[1] - to[1]) >> 4;
    in.vz = (from[2] - to[2]) >> 4;
    Square12(&in, &out);
    return SquareRoot12(out.vx + out.vy + out.vz) << 4;
}

/* Joins two points with a spring as long as they are apart, unless they are
 * joined already or both are held. Returns whether it did. Nothing counts
 * a point's springs against the CLOTH_POINT_SPRINGS it has room for: a
 * ninth from a point would be written over the first to it, and a ninth to
 * it over what follows the point (the original's behaviour). No cloth of
 * the game's has such a point: in the 34 cloths of its models at most eight
 * springs meet at a point, from it and to it together (tests/cloth.py). */
static s32 cloth_AddSpring(Cloth *cloth, s32 kind, s32 from, s32 to) {
    s32 i;
    s32 length;
    ClothSpring *spring;

    if (from == to) {
        return 0;
    }
    if (cloth->points[from].pin >= 0 && cloth->points[to].pin >= 0) {
        return 0;
    }
    for (i = 0; i < cloth->points[from].out_count; i++) {
        if (cloth->springs[cloth->points[from].out_springs[i]].ends.point.to ==
            to) {
            return 0;
        }
    }
    for (i = 0; i < cloth->points[from].in_count; i++) {
        if (cloth->springs[cloth->points[from].in_springs[i]].ends.point.from ==
            to) {
            return 0;
        }
    }
    length = cloth_Distance(cloth->points[from].pos, cloth->points[to].pos);
    spring = &cloth->springs[cloth->spring_count];
    spring->length = length;
    spring->max_length = length * 3;
    spring->kind = kind;
    spring->ends.pair = (from << 16) | to;
    cloth->points[from].out_springs[cloth->points[from].out_count++] =
        cloth->spring_count;
    cloth->points[to].in_springs[cloth->points[to].in_count++] =
        cloth->spring_count;
    cloth->spring_count++;
    return 1;
}

/* Puts every point where its vertex is now, at rest, then joins the points
 * of each face to one another. A spring runs from the point nearer the
 * middle of the held points to the one further out. */
static void cloth_Build(ClothDesc *desc, Cloth *cloth) {
    VECTOR pos;
    VECTOR center;
    s32 gte_flags;
    ClothPoint *points;
    s16 *face;
    s32 held_count;
    s32 i;
    s32 j;
    s32 face_points;
    s32 kind;
    s32 dist;
    s32 center_dist;

    center.vx = 0;
    center.vy = 0;
    center.vz = 0;
    held_count = 0;
    SetRotMatrix(desc->matrix);
    SetTransMatrix(desc->matrix);

    points = cloth->points;
    for (i = 0; i < desc->count; i++) {
        points[i].pin = desc->vertices[i].pad;
        RotTrans(&desc->vertices[i], &pos, &gte_flags);
        pos.vx <<= 7;
        pos.vy <<= 7;
        pos.vz <<= 7;
        if (points[i].pin >= 0) {
            held_count++;
            center.vx += pos.vx;
            center.vy += pos.vy;
            center.vz += pos.vz;
        }
        points[i].pos[0] = pos.vx;
        points[i].pos[1] = pos.vy;
        points[i].pos[2] = pos.vz;
        points[i].vel_x = 0;
        points[i].vel_y = 0;
        points[i].vel_z = 0;
        points[i].out_count = 0;
        points[i].in_count = 0;
    }

    center.vx /= held_count;
    center.vy /= held_count;
    center.vz /= held_count;

    for (i = 0; i < desc->count; i++) {
        center_dist = cloth_DistanceFar((s32 *)&center, points[i].pos) >> 8;
        if (center_dist > CLOTH_CENTER_DIST_MAX) {
            center_dist = CLOTH_CENTER_DIST_MAX;
        }
        points[i].center_dist = center_dist;
    }

    face = desc->faces;
    while ((face_points = *face++) != 0) {
        kind = *face++;
        for (i = 0; i < face_points - 1; i++) {
            dist = cloth_DistanceFar((s32 *)&center, points[face[i]].pos);
            for (j = i + 1; j < face_points; j++) {
                if (dist <
                    cloth_DistanceFar((s32 *)&center, points[face[j]].pos)) {
                    cloth_AddSpring(cloth, kind, face[i], face[j]);
                } else {
                    cloth_AddSpring(cloth, kind, face[j], face[i]);
                }
            }
        }
        face += face_points;
    }
}

/* Makes a cloth in the first free place, its points and springs in the
 * description's buffer, hanging at rest. Returns its index, or -1 if every
 * place is taken. */
s32 cloth_Create(ClothDesc *desc) {
    Cloth *cloth;
    s32 i;
    ClothPoint *points;

    cloth = g_cloths;
    for (i = 0; i < CLOTH_COUNT; i++) {
        if (!cloth->vertices) {
            break;
        }
        cloth++;
    }
    if (i == CLOTH_COUNT) {
        return -1;
    }
    if (g_cloth_peak < i) {
        g_cloth_peak = i;
    }
    cloth->count = desc->count;
    cloth->vertices = desc->vertices;
    cloth->matrix = *desc->matrix;
    cloth->unk8 = desc->unk2;
    points = (ClothPoint *)desc->buffer;
    cloth->spring_count = 0;
    cloth->points = points;
    cloth->springs = (ClothSpring *)(points + cloth->count);
    cloth_Build(desc, cloth);
    return i;
}

void cloth_Free(s32 cloth) { g_cloths[cloth].vertices = NULL; }

/* The cloth's points and springs have moved by `delta` bytes. Nothing calls
 * it: a model's cloth is made again instead (cloth_RequestRebuild). */
static void cloth_RebaseBuffer(s32 index, s32 delta) {
    Cloth *cloth = &g_cloths[index];

    cloth->points = (ClothPoint *)((u8 *)cloth->points + delta);
    cloth->springs = (ClothSpring *)(cloth->points + cloth->count);
}

/* The cloth's vertices have moved by `delta` bytes. Nothing calls it
 * either. */
static void cloth_RebaseVertices(s32 index, s32 delta) {
    Cloth *cloth = &g_cloths[index];
    cloth->vertices = (SVECTOR *)((u8 *)cloth->vertices + delta);
}

void cloth_SaveScratch(void) {
    u32 *src = (u32 *)CLOTH_SPAD;
    u32 *dst = g_cloth_scratch_save;
    u32 i;
    for (i = 0; i < CLOTH_SCRATCH_SAVE_WORDS; i++) {
        *dst++ = *src++;
    }
}

void cloth_RestoreScratch(void) {
    u32 *src = g_cloth_scratch_save;
    u32 *dst = (u32 *)CLOTH_SPAD;
    u32 i;
    for (i = 0; i < CLOTH_SCRATCH_SAVE_WORDS; i++) {
        *dst++ = *src++;
    }
}

/* Two functions with nothing in them. What they were for is a guess: see
 * cloth_ClearSceneColliders, which is where they are called from. */
void cloth_ResetSceneColliders(void) {}

/* Its parameters are not used. */
void cloth_SetSceneCollider(s32 first_word, s32 second_word, s32 index) {}

/* Puts the update's ground and ellipsoids in the scratchpad, in the units
 * the step wants. */
static void cloth_LoadColliders(ClothUpdate *update) {
    ClothColliders *colliders;
    s32 i, j;

    if (!PTR(update->colliders)) {
        return;
    }
    colliders = PTR(update->colliders);
    CLOTH_SPAD->ground_y = colliders->ground_y << 7;
    for (i = 0; i < colliders->count; i++) {
        for (j = 0; j < 2; j++) {
            CLOTH_SPAD->foci[i][j][0] = colliders->foci[i][j].vx << 2;
            CLOTH_SPAD->foci[i][j][1] = colliders->foci[i][j].vy << 2;
            CLOTH_SPAD->foci[i][j][2] = colliders->foci[i][j].vz << 2;
        }
        CLOTH_SPAD->reach[i] = colliders->reach[i] << 2;
    }
    CLOTH_SPAD->collider_count = PTR(update->colliders)->count;
}

/* The square root of a number that is not negative: from the table of
 * roots below 0x400, and above that from the table entry of the number
 * shifted down by an even count of bits, shifted back up by half as
 * many. */
static inline s32 cloth_Sqrt(s32 x) {
    /* FAKE: s32 lz; the array gives the caller its stack frame */
    s32 lz[4];

    if (x < MATH_SQRT_TABLE_SIZE) {
        return g_math_sqrt8_table[x] >> 3;
    } else {
        s32 shift, half_shift;

        gte_Lzc(x, &lz[0]);
        shift = lz[0];
        shift = MATH_SQRT_SHIFT - (shift & ~1);
        half_shift = shift >> 1;
        x = g_math_sqrt8_table[x >> shift];
        return (x << 16) >> (MATH_SQRT_UNSHIFT - half_shift);
    }
}

/* Gets a step ready: the bone's matrix on the GTE, the colliders in the
 * scratchpad, and how far the bone has moved since the last step. The
 * points are carried along by most of that movement: three quarters of a
 * small one, rising to all of one of 500 units or more, so cloth trails a
 * little behind a slow movement and keeps up with a fast one. Then the
 * springs. */
static void cloth_BeginStep(ClothUpdate *update, Cloth *cloth) {
    s32 dx, dy, dz;
    s32 distance; /* squared, then not */
    s32 scale;

    gte_SetRotMatrix(PTR(update->matrix));
    gte_SetTransMatrix(PTR(update->matrix));

    cloth_LoadColliders(update);

    dx = PTR(update->matrix)->t[0] - cloth->matrix.t[0];
    CLOTH_SPAD->carry[0] = dx;
    dy = PTR(update->matrix)->t[1] - cloth->matrix.t[1];
    CLOTH_SPAD->carry[1] = dy;
    dz = PTR(update->matrix)->t[2] - cloth->matrix.t[2];
    CLOTH_SPAD->carry[2] = dz;

    distance = (dx * dx) + (dy * dy) + (dz * dz);

    if (distance > CLOTH_CARRY_ALL_DISTANCE * CLOTH_CARRY_ALL_DISTANCE) {
        scale = CLOTH_CARRY_ONE;
    } else if (distance < 0) {
        /* only when the sum of the squares is more than a word holds */
        scale = 0;
    } else {
        distance = cloth_Sqrt(distance);
        /* the last quarter (1 << 6 of CLOTH_CARRY_ONE) by the distance */
        scale = ((distance << 6) / CLOTH_CARRY_ALL_DISTANCE) +
                3 * CLOTH_CARRY_ONE / 4;
    }
    /* into the points' 128ths */
    CLOTH_SPAD->carry[0] = (CLOTH_SPAD->carry[0] * scale) >> 1;
    CLOTH_SPAD->carry[1] = (CLOTH_SPAD->carry[1] * scale) >> 1;
    CLOTH_SPAD->carry[2] = (CLOTH_SPAD->carry[2] * scale) >> 1;

    cloth->matrix = *PTR(update->matrix);
    cloth_SolveSprings(cloth);
}

/* Works out every spring's pull, into CLOTH_SPAD->pull: along the spring, and
 * in proportion to how much longer or shorter than its length at rest it is. A
 * spring stretched to more than three times its length on any axis is first
 * pulled in: its loose end is put half way to the other and slowed to a
 * quarter, until it is short enough. The last spring is not pulled in that way.
 */
static void cloth_SolveSprings(Cloth *cloth) {
    s32 *pull;
    ClothSpring *spring;
    ClothPoint *points;
    s32 count;
    s32 limit; /* and first the spring's ends */
    s32 rest_length;
    s32 neg_limit;
    ClothPoint *from, *to;
    s32 dx, dy, dz;
    u32 sum;
    s32 len;
    /* FAKE: s32 lz; the array gives the function its stack frame */
    s32 lz[6];
    s32 factor;

    pull = CLOTH_SPAD->pull[0];
    count = cloth->spring_count - 1;
    spring = cloth->springs;
    points = cloth->points;
    limit = spring->ends.pair;
    rest_length = spring->length;
    from = &points[limit >> 16];
    to = &points[limit & 0xFFFF];
    limit = rest_length * 3;
    neg_limit = -limit;
    dx = to->pos[0] - from->pos[0];
    dy = to->pos[1] - from->pos[1];
    dz = to->pos[2] - from->pos[2];
    while (count > 0) {
        while (dx < neg_limit || limit < dx || dy < neg_limit || limit < dy ||
               dz < neg_limit || limit < dz) {
            dx = (to->pos[0] + from->pos[0]) / 2;
            dy = (to->pos[1] + from->pos[1]) / 2;
            dz = (to->pos[2] + from->pos[2]) / 2;
            if (to->pin < 0) {
                to->pos[0] = dx;
                to->pos[1] = dy;
                to->pos[2] = dz;
                to->vel_x /= 4;
                to->vel_y /= 4;
                to->vel_z /= 4;
            } else {
                from->pos[0] = dx;
                from->pos[1] = dy;
                from->pos[2] = dz;
                from->vel_x /= 4;
                from->vel_y /= 4;
                from->vel_z /= 4;
            }
            dx = to->pos[0] - from->pos[0];
            dy = to->pos[1] - from->pos[1];
            dz = to->pos[2] - from->pos[2];
        }
        CLOTH_SPAD->delta[0] = dx >> 3;
        CLOTH_SPAD->delta[1] = dy >> 3;
        CLOTH_SPAD->delta[2] = dz >> 3;
        gte_ldlvl(CLOTH_SPAD->delta);
        gte_sqr0();
        /* the next spring is fetched while the GTE works on this one */
        spring++;
        count--;
        limit = spring->ends.pair;
        gte_stlvnl(CLOTH_SPAD->delta2);
        sum = CLOTH_SPAD->delta2[0] + CLOTH_SPAD->delta2[1] +
              CLOTH_SPAD->delta2[2];
        /* the root is left times 8, as the table has it: the vector was
         * measured in eighths */
        if (sum < MATH_SQRT_TABLE_SIZE) {
            len = g_math_sqrt8_table[sum];
            from = &points[limit >> 16];
            to = &points[limit & 0xFFFF];
        } else {
            u32 shift, root;

            gte_ldlzc(sum);
            from = &points[limit >> 16];
            to = &points[limit & 0xFFFF];
            gte_stlzc(lz);
            /* FAKE: shift = MATH_SQRT_SHIFT - (lz[0] & ~1) */
            len = lz[0];
            shift = MATH_SQRT_SHIFT - (len & ~1);
            root = g_math_sqrt8_table[sum >> shift];
            len = (root << 16) >> (MATH_SQRT_UNSHIFT - 3 - ((s32)shift >> 1));
        }
        gte_ldlvl(CLOTH_SPAD->delta);
        /* how much of its length it is out by, in 16384ths: times the
         * vector in eighths, over 4096, the pull is half the stretch */
        factor = ((len - rest_length) << 14) / len;
        rest_length = spring->length;
        limit = rest_length * 3;
        neg_limit = -limit;
        gte_lddp(factor);
        gte_gpf12();
        dx = to->pos[0] - from->pos[0];
        dy = to->pos[1] - from->pos[1];
        dz = to->pos[2] - from->pos[2];
        gte_stlvl(pull);
        pull += 3;
    }
    CLOTH_SPAD->delta[0] = dx >> 3;
    CLOTH_SPAD->delta[1] = dy >> 3;
    CLOTH_SPAD->delta[2] = dz >> 3;
    gte_Square0(CLOTH_SPAD->delta, CLOTH_SPAD->delta2);
    sum = CLOTH_SPAD->delta2[0] + CLOTH_SPAD->delta2[1] + CLOTH_SPAD->delta2[2];
    if (sum < MATH_SQRT_TABLE_SIZE) {
        len = g_math_sqrt8_table[sum];
    } else {
        gte_Lzc(sum, lz);
        /* FAKE: the shift is held in `len` and the table's root in `sum` */
        len = lz[0];
        len = MATH_SQRT_SHIFT - (len & ~1);
        sum = g_math_sqrt8_table[sum >> len];
        len = (sum << 16) >> (MATH_SQRT_UNSHIFT - 3 - (len >> 1));
    }
    gte_ldlvl(CLOTH_SPAD->delta);
    factor = ((len - rest_length) << 14) / len;
    gte_lddp(factor);
    gte_gpf12();
    gte_stlvl(pull);
}

/* Moves every point by one frame.
 *
 * A held point goes where its vertex is, or is drawn toward it. A loose
 * point takes the pull of its springs; if it is under the ground it is
 * pushed up, and if it is inside an ellipsoid it is pushed away from both
 * foci, the harder the deeper it is. Then it loses an eighth of its speed,
 * gains gravity's, and moves, carried along with the bone. */
static void cloth_Step(ClothUpdate *update, Cloth *cloth) {
    ClothPoint *point;
    s32 i;
    s32 count;
    s32 vx, vy, vz;
    s32 out_springs, in_springs;
    s32 reach;
    s32 vy_new;
    s32 total, push;
    /* FAKE: s32 lz[2]; the longer array gives the function its stack frame */
    s32 lz[6];

    cloth_BeginStep(update, cloth);
    count = cloth->count;
    point = cloth->points;

    for (i = 0; i < count; i++, point++) {
        s32 k;
        s32 spring_count; /* those out, then those in */

        CLOTH_SPAD->pos[0] = point->pos[0];
        CLOTH_SPAD->pos[1] = point->pos[1];
        CLOTH_SPAD->pos[2] = point->pos[2];
        if (point->pin >= 0) {
            gte_ldv0(&cloth->vertices[i]);
            gte_rtv0tr();
            gte_stlvnl(CLOTH_SPAD->work);
            if (point->pin > 0) {
                /* tied: half its old speed, and `pin` 256ths of the way to
                 * its place; the looser, the more it sags */
                CLOTH_SPAD->pos[0] += CLOTH_SPAD->carry[0];
                point->vel_x =
                    point->vel_x / 2 +
                    ((((CLOTH_SPAD->work[0] << 7) - CLOTH_SPAD->pos[0]) *
                      point->pin) >>
                     8);
                point->pos[0] = CLOTH_SPAD->pos[0] + point->vel_x;
                CLOTH_SPAD->pos[1] += CLOTH_SPAD->carry[1];
                point->vel_y =
                    point->vel_y / 2 +
                    ((((CLOTH_SPAD->work[1] << 7) - CLOTH_SPAD->pos[1]) *
                          point->pin +
                      (CLOTH_PIN_ONE - point->pin) * CLOTH_TIED_GRAVITY) >>
                     8);
                point->pos[1] = CLOTH_SPAD->pos[1] + point->vel_y;
                CLOTH_SPAD->pos[2] += CLOTH_SPAD->carry[2];
                point->vel_z =
                    point->vel_z / 2 +
                    ((((CLOTH_SPAD->work[2] << 7) - CLOTH_SPAD->pos[2]) *
                      point->pin) >>
                     8);
                point->pos[2] = CLOTH_SPAD->pos[2] + point->vel_z;
            } else {
                point->pos[0] = CLOTH_SPAD->work[0] << 7;
                point->pos[1] = CLOTH_SPAD->work[1] << 7;
                point->pos[2] = CLOTH_SPAD->work[2] << 7;
            }
            continue;
        }
        if (point->pin > CLOTH_PIN_LOOSE) {
            /* loose, but leaning toward its place (`pin` is negative) */
            gte_ldv0(&cloth->vertices[i]);
            gte_rtv0tr();
            gte_stlvnl(CLOTH_SPAD->work);
            CLOTH_SPAD->pos[0] -=
                (((CLOTH_SPAD->work[0] << 7) - CLOTH_SPAD->pos[0]) *
                 point->pin) >>
                8;
            CLOTH_SPAD->pos[1] -=
                (((CLOTH_SPAD->work[1] << 7) - CLOTH_SPAD->pos[1]) *
                 point->pin) >>
                8;
            CLOTH_SPAD->pos[2] -=
                (((CLOTH_SPAD->work[2] << 7) - CLOTH_SPAD->pos[2]) *
                 point->pin) >>
                8;
        }
        /* the springs: their numbers are read four to a word */
        vx = point->vel_x;
        vy = point->vel_y;
        vz = point->vel_z;
        spring_count = point->out_count;
        out_springs = *(s32 *)&point->out_springs[0];
        for (k = 0; k < spring_count; k++) {
            s32 *pull;

            pull = CLOTH_SPAD->pull[out_springs & 0xFF];
            vx += pull[0];
            vy += pull[1];
            vz += pull[2];
            if (k == 3) {
                out_springs = *(s32 *)&point->out_springs[4];
            } else {
                out_springs >>= 8;
            }
        }
        spring_count = point->in_count;
        in_springs = *(s32 *)&point->in_springs[0];
        for (k = 0; k < spring_count; k++) {
            s32 *pull;

            pull = CLOTH_SPAD->pull[in_springs & 0xFF];
            vx -= pull[0];
            vy -= pull[1];
            vz -= pull[2];
            if (k == 3) {
                in_springs = *(s32 *)&point->in_springs[4];
            } else {
                in_springs >>= 8;
            }
        }
        CLOTH_SPAD->work[0] = vx;
        CLOTH_SPAD->work[1] = vy;
        CLOTH_SPAD->work[2] = vz;
        if (PTR(update->colliders)) {
            s32 delta; /* how far under the ground, then from a focus */

            delta = CLOTH_SPAD->pos[1] - CLOTH_SPAD->ground_y;
            if (delta > 0) {
                if (delta > CLOTH_GROUND_DEEP) {
                    vy_new = vy - CLOTH_GROUND_DEEP_PUSH;
                } else {
                    vy_new = vy - delta / 8; /* an eighth of its depth */
                }
                CLOTH_SPAD->work[1] = vy_new;
            }
            CLOTH_SPAD->near_pos[0] = CLOTH_SPAD->pos[0] >> 5;
            CLOTH_SPAD->near_pos[1] = CLOTH_SPAD->pos[1] >> 5;
            CLOTH_SPAD->near_pos[2] = CLOTH_SPAD->pos[2] >> 5;
            for (k = 0; k < CLOTH_SPAD->collider_count; k++) {
                s32 dx0, dz0, dy1, dx1, dz1;
                s32 sum2, dist2;
                s32 root;
                s32 dist; /* from the first focus: squared, then not */

                /* outside the box around each focus is outside for
                 * certain */
                reach = CLOTH_SPAD->reach[k];
                delta = CLOTH_SPAD->near_pos[1] - CLOTH_SPAD->foci[k][0][1];
                if (delta < -reach || reach < delta) {
                    continue;
                }
                CLOTH_SPAD->delta[1] = delta;
                dx0 = CLOTH_SPAD->near_pos[0] - CLOTH_SPAD->foci[k][0][0];
                if (dx0 < -reach || reach < dx0) {
                    continue;
                }
                CLOTH_SPAD->delta[0] = dx0;
                dz0 = CLOTH_SPAD->near_pos[2] - CLOTH_SPAD->foci[k][0][2];
                if (dz0 < -reach || reach < dz0) {
                    continue;
                }
                CLOTH_SPAD->delta[2] = dz0;
                gte_Square0(CLOTH_SPAD->delta, CLOTH_SPAD->squares);
                dist = CLOTH_SPAD->squares[0] + CLOTH_SPAD->squares[1] +
                       CLOTH_SPAD->squares[2];
                root = dist;
                if (dist < MATH_SQRT_TABLE_SIZE) {
                    dist = g_math_sqrt8_table[dist] >> 3;
                } else {
                    s32 shift;

                    gte_Lzc(root, &lz[0]);
                    shift = lz[0];
                    shift = MATH_SQRT_SHIFT - (shift & ~1);
                    root = g_math_sqrt8_table[dist >> shift];
                    dist = (root << 16) >> (MATH_SQRT_UNSHIFT - (shift >> 1));
                }
                if (dist >= reach) {
                    continue;
                }
                dy1 = CLOTH_SPAD->near_pos[1] - CLOTH_SPAD->foci[k][1][1];
                if (dy1 < -reach || reach < dy1) {
                    continue;
                }
                CLOTH_SPAD->delta2[1] = dy1;
                dx1 = CLOTH_SPAD->near_pos[0] - CLOTH_SPAD->foci[k][1][0];
                if (dx1 < -reach || reach < dx1) {
                    continue;
                }
                CLOTH_SPAD->delta2[0] = dx1;
                dz1 = CLOTH_SPAD->near_pos[2] - CLOTH_SPAD->foci[k][1][2];
                if (dz1 < -reach || reach < dz1) {
                    continue;
                }
                CLOTH_SPAD->delta2[2] = dz1;
                gte_Square0(CLOTH_SPAD->delta2, CLOTH_SPAD->squares);
                sum2 = CLOTH_SPAD->squares[0] + CLOTH_SPAD->squares[1] +
                       CLOTH_SPAD->squares[2];
                if (sum2 < MATH_SQRT_TABLE_SIZE) {
                    dist2 = g_math_sqrt8_table[sum2] >> 3;
                } else {
                    s32 shift2;

                    gte_Lzc(sum2, &lz[1]);
                    shift2 = lz[1];
                    shift2 = MATH_SQRT_SHIFT - (shift2 & ~1);
                    root = g_math_sqrt8_table[sum2 >> shift2];
                    dist2 = (root << 16) >> (MATH_SQRT_UNSHIFT - (shift2 >> 1));
                }
                total = dist + dist2;
                if (total >= reach) {
                    continue;
                }
                /* inside: add a push away from each focus to the
                 * velocity */
                gte_lddp(1);
                gte_ldlvl(CLOTH_SPAD->work);
                gte_gpf0();
                push = (reach - total) << 17;
                if (push > CLOTH_PUSH_MAX) {
                    push = CLOTH_PUSH_MAX;
                }
                if (dist != 0) {
                    dist = push / dist;
                }
                gte_ldlvl(CLOTH_SPAD->delta);
                gte_lddp(dist);
                gte_gpl12();
                if (dist2 != 0) {
                    dist2 = push / dist2;
                }
                gte_ldlvl(CLOTH_SPAD->delta2);
                gte_lddp(dist2);
                gte_gpl12();
                gte_stlvl(CLOTH_SPAD->work);
            }
        }
        /* seven eighths of its speed is left */
        point->vel_x = (CLOTH_SPAD->work[0] * 7) >> 3;
        point->pos[0] =
            CLOTH_SPAD->pos[0] + CLOTH_SPAD->carry[0] + point->vel_x;
        /* seven eighths, and gravity */
        point->vel_y = ((CLOTH_SPAD->work[1] * 7) >> 3) + CLOTH_GRAVITY;
        point->pos[1] =
            CLOTH_SPAD->pos[1] + CLOTH_SPAD->carry[1] + point->vel_y;
        /* seven eighths */
        point->vel_z = (CLOTH_SPAD->work[2] * 7) >> 3;
        point->pos[2] =
            CLOTH_SPAD->pos[2] + CLOTH_SPAD->carry[2] + point->vel_z;
    }
}

/* Steps each cloth of the list a frame, or puts it at rest if it is asked
 * to. */
void cloth_Update(ClothUpdate *updates, s32 count) {
    s32 i;

    for (i = 0; i < count; i++) {
        if (updates[i].snap & 1) {
            cloth_Snap(&updates[i], &g_cloths[updates[i].cloth]);
        } else {
            cloth_Step(&updates[i], &g_cloths[updates[i].cloth]);
        }
    }
}

/* Puts every point where its vertex is now, at rest: the cloth hangs as the
 * mesh was modelled. */
static void cloth_Snap(ClothUpdate *update, Cloth *cloth) {
    s32 out[3];
    s32 i;
    ClothPoint *point;

    gte_SetRotMatrix(PTR(update->matrix));
    gte_SetTransMatrix(PTR(update->matrix));

    point = cloth->points;
    for (i = 0; i < cloth->count; i++) {
        gte_ldv0(&cloth->vertices[i]);
        gte_rtv0tr();
        gte_stlvnl(out);
        point->pos[0] = out[0] << 7;
        point->pos[1] = out[1] << 7;
        point->pos[2] = out[2] << 7;
        point->vel_x = 0;
        point->vel_y = 0;
        point->vel_z = 0;
        point++;
    }
    cloth->matrix = *PTR(update->matrix);
}

/* The highest cloth index that has been in use. Nothing reads it. */
static u8 g_cloth_peak = 0;
