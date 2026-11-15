/* The geometry of the hit tests (fighter.h, math.h, fighter_hit.c). All of
 * it works in the scratch (WeaponHitScratch): the caller puts a shape's
 * corners in shape_points, a function here gives the shape a frame of its
 * own, and the tests then turn what they are testing into that frame.
 * Distances are the table's square roots (math_TableSqrt and MATH_SQRT,
 * math.h), written out in the three places where neither gives the
 * original's code. The file ends with a few matrix helpers and one question
 * about a fighter that the same scratch answers. */
#define INCLUDE_ASM_USE_MACRO_INC 1
#include "common.h"
#include "include_asm.h"
#include <psxsdk/inline_o.h>
#include <psxsdk/gtemac.h>
#include "bb2.h"

/* Defined further down. */
static s32 g_hit_flat_triangle_count;

/* The normal of a triangle is the cross product of two of its edges, as
 * long as twice its area. One that is shorter than this, or has no term
 * this long, is too thin to be framed by its normal. */
#define THIN_NORMAL 0x4000
/* The normal is brought down by this many bits before it is squared. */
#define HIT_NORMAL_SHIFT 6

/* Where along a segment a line meets a circle: this is the far end. */
#define SEGMENT_T_ONE 0x100

/* Gives a triangle its frame: its two edges from the first corner, their
 * cross product (the normal), and the turn that lays the normal along z, so
 * that the triangle lies flat in x and y; the edges are left turned into
 * the frame. Returns 1. A triangle too thin to have a normal worth the
 * name (a blade that has hardly moved) is laid with its first edge along z
 * instead, and the function returns 0. */
s32 hit_FrameTriangle(WeaponHitScratch *scr) {
    s32 lz; /* the number's leading zeros */
    s32 len_sq;
    s32 xz_sq;
    /* FAKE: a number, then the byte of the table of roots it leads to */
    s32 root_byte;
    s32 nxz_len;

    scr->edge_a.x = PTR(scr->shape_points[1])->x - PTR(scr->shape_points[0])->x;
    scr->edge_a.y = PTR(scr->shape_points[1])->y - PTR(scr->shape_points[0])->y;
    scr->edge_a.z = PTR(scr->shape_points[1])->z - PTR(scr->shape_points[0])->z;
    scr->edge_b.x = PTR(scr->shape_points[2])->x - PTR(scr->shape_points[0])->x;
    scr->edge_b.y = PTR(scr->shape_points[2])->y - PTR(scr->shape_points[0])->y;
    scr->edge_b.z = PTR(scr->shape_points[2])->z - PTR(scr->shape_points[0])->z;

    gte_OuterProduct0(&scr->edge_a, &scr->edge_b, &scr->normal.vector);

    if ((scr->normal.vector.x > -THIN_NORMAL &&
         scr->normal.vector.x < THIN_NORMAL) &&
        (scr->normal.vector.y > -THIN_NORMAL &&
         scr->normal.vector.y < THIN_NORMAL) &&
        (scr->normal.vector.z > -THIN_NORMAL &&
         scr->normal.vector.z < THIN_NORMAL)) {
        u32 len; /* of the normal, then of the first edge across the ground */
        gte_sqr0();
        gte_stlvnl(&scr->hit_points[0]);
        len_sq =
            scr->hit_points[0].x + scr->hit_points[0].y + scr->hit_points[0].z;
        MATH_SQRT(len, len_sq);
        if (len < THIN_NORMAL) {
            scr->angles.vy =
                ANGLE_TURN / 2 - ratan2(scr->edge_a.x, scr->edge_a.z);
            xz_sq =
                scr->edge_a.x * scr->edge_a.x + scr->edge_a.z * scr->edge_a.z;
            MATH_SQRT(len, xz_sq);
            scr->angles.vx = ANGLE_TURN / 2 - ratan2(scr->edge_a.y, len);
            scr->rot_matrix.m[0][0] = ONE;
            scr->rot_matrix.m[0][1] = 0;
            scr->rot_matrix.m[0][2] = 0;
            scr->rot_matrix.m[1][0] = 0;
            scr->rot_matrix.m[1][1] = ONE;
            scr->rot_matrix.m[1][2] = 0;
            scr->rot_matrix.m[2][0] = 0;
            scr->rot_matrix.m[2][1] = 0;
            scr->rot_matrix.m[2][2] = ONE;
            RotMatrixY(scr->angles.vy, &scr->rot_matrix);
            RotMatrixX(scr->angles.vx, &scr->rot_matrix);
            gte_SetRotMatrix(&scr->rot_matrix);
            gte_ldlv0(&scr->edge_a);
            gte_rtv0();
            gte_stlvnl(&scr->edge_a);
            gte_ldlv0(&scr->edge_b);
            gte_rtv0();
            gte_stlvnl(&scr->edge_b);
            return 0;
        }
    }

    scr->angles.vy =
        ANGLE_TURN / 2 - ratan2(scr->normal.vector.x, scr->normal.vector.z);
    scr->normal.vector.x >>= HIT_NORMAL_SHIFT;
    scr->normal.vector.y >>= HIT_NORMAL_SHIFT;
    scr->normal.vector.z >>= HIT_NORMAL_SHIFT;
    root_byte = scr->normal.vector.x * scr->normal.vector.x +
                scr->normal.vector.z * scr->normal.vector.z;
    /* MATH_SQRT, written out */
    if ((u32)root_byte < MATH_SQRT_TABLE_SIZE) {
        nxz_len = g_math_sqrt8_table[root_byte] >> 3;
    } else {
        s32 lzcr = 0;
        if (root_byte >= 0) {
            gte_Lzc(root_byte, &lz);
            lzcr = lz;
        }
        {
            u32 shift = MATH_SQRT_SHIFT - (lzcr & ~1);
            root_byte = g_math_sqrt8_table[(u32)root_byte >> shift];
            nxz_len =
                (u32)(root_byte << 16) >> (MATH_SQRT_UNSHIFT - (shift >> 1));
        }
    }
    scr->angles.vx = ANGLE_TURN / 2 - ratan2(scr->normal.vector.y, nxz_len);
    scr->rot_matrix.m[0][0] = ONE;
    scr->rot_matrix.m[0][1] = 0;
    scr->rot_matrix.m[0][2] = 0;
    scr->rot_matrix.m[1][0] = 0;
    scr->rot_matrix.m[1][1] = ONE;
    scr->rot_matrix.m[1][2] = 0;
    scr->rot_matrix.m[2][0] = 0;
    scr->rot_matrix.m[2][1] = 0;
    scr->rot_matrix.m[2][2] = ONE;
    RotMatrixY(scr->angles.vy, &scr->rot_matrix);
    RotMatrixX(scr->angles.vx, &scr->rot_matrix);
    gte_SetRotMatrix(&scr->rot_matrix);
    gte_ldlv0(&scr->edge_a);
    gte_rtv0();
    gte_stlvnl(&scr->edge_a);
    gte_ldlv0(&scr->edge_b);
    gte_rtv0();
    gte_stlvnl(&scr->edge_b);
    return 1;
}

/* Whether a sphere reaches a triangle that hit_FrameTriangle found too
 * thin: the triangle is taken as the stretch of z its corners span. With
 * `core` 0 the sphere's center is turned into the frame and kept; with 1
 * the center kept is tested against another radius (the core).
 * `reach_sq` is the body point's (BodyPoint.reach_sq): the radius squared
 * for a fighter of full size. */
s32 hit_SphereMeetsSliver(
    s32 core, WeaponHitScratch *scr, Vec3 *center, s32 radius, s32 reach_sq) {
    if (!core) {
        scr->angles.vx = center->x - PTR(scr->shape_points[0])->x;
        scr->angles.vy = center->y - PTR(scr->shape_points[0])->y;
        scr->angles.vz = center->z - PTR(scr->shape_points[0])->z;
        gte_ApplyRotMatrix(&scr->angles, &scr->hit_points[0]);
    }
    {
        s32 x;
        s32 y;
        s32 min_z;
        s32 max_z;
        s32 edge_a_z;
        s32 edge_b_z;
        s32 z;
        s32 neg_radius = -radius;
        s32 dist_sq;
        /* half the length of the sphere's cut along the sliver: squared,
         * then not */
        s32 half_cut;

        x = scr->hit_points[0].x;
        if (x < neg_radius || radius < x) {
            return 0;
        }
        y = scr->hit_points[0].y;
        if (y < neg_radius || radius < y) {
            return 0;
        }

        dist_sq = x * x + y * y;
        if (reach_sq < dist_sq) {
            return 0;
        }
        half_cut = reach_sq - dist_sq;

        MATH_SQRT(half_cut, half_cut);

        max_z = 0;
        min_z = 0;
        edge_a_z = scr->edge_a.z;
        if (edge_a_z < 0) {
            min_z = edge_a_z;
        } else if (min_z < edge_a_z) {
            max_z = edge_a_z;
        }
        edge_b_z = scr->edge_b.z;
        if (edge_b_z < min_z) {
            min_z = edge_b_z;
        } else if (max_z < edge_b_z) {
            max_z = edge_b_z;
        }
        z = scr->hit_points[0].z;
        if (max_z < z - half_cut || z + half_cut < min_z) {
            return 0;
        }
        return 1;
    }
}

/* Whether the segment from `from` to `to` passes within a circle about the
 * origin (`threshold` is the radius and `r_sq` its square): by solving for
 * where the line meets the circle and asking whether that is within the
 * segment. The plane is the points' x and y, which the locals call x and
 * z. */
static s32 math_SegmentMeetsCircle(
    s32 threshold, s32 r_sq, Vec3 *from, Vec3 *to) {
    s32 x1, z1, x2, z2;

    x1 = from->x;
    x2 = to->x;

    if (x1 < x2) {
        if (x2 < -threshold || x1 > threshold) {
            return 0;
        }
    } else {
        if (x1 < -threshold) {
            return 0;
        }
        if (threshold < x2) {
            return 0;
        }
    }

    z1 = from->y;
    z2 = to->y;

    if (z1 < z2) {
        if (z2 < -threshold) {
            return 0;
        }
        /* FAKE: if (threshold < z1) return 0; as for x above */
        if (threshold >= z1) {
            goto dist_calc;
        }
        return 0;
    } else {
        if (z1 < -threshold) {
            return 0;
        }
        if (threshold < z2) {
            return 0;
        }
    }

dist_calc: {
    s32 ax = to->x - from->x;
    s32 x1r = from->x;
    s32 az = to->y - from->y;
    s32 z1r = from->y;

    s32 ax_sq = ax * ax;
    s32 az_sq = az * az;
    s32 cx = ax * x1r;
    s32 cz = az * z1r;
    s32 x1_sq = x1r * x1r;
    s32 z1_sq = z1r * z1r;

    s32 dot2 = (cx + cz) * 2;
    s32 dot2_9 = dot2 >> 9;
    s32 c_val = ((x1_sq + z1_sq) - r_sq) >> 9;
    s32 dist_sq = ax_sq + az_sq;

    s32 disc_root = (dot2_9 * dot2_9) - ((dist_sq >> 9) * (c_val << 2));
    s32 result;

    if (disc_root < 0) {
        result = 0;
    } else {
        /* MATH_SQRT, written out */
        if (disc_root < (u32)MATH_SQRT_TABLE_SIZE) {
            disc_root = g_math_sqrt8_table[disc_root] >> 3;
        } else {
            s32 lz;
            s32 lzcr;
            u32 number;
            number = disc_root;
            lzcr = 0;
            if (disc_root >= 0) {
                /* FAKE: stored again */
                number = disc_root;
                gte_Lzc(number, &lz);
                lzcr = lz;
            }
            {
                u32 shift = MATH_SQRT_SHIFT - (lzcr & ~1);
                s32 root8 = g_math_sqrt8_table[number >> shift];
                s32 half = shift >> 1;
                disc_root = (u32)(root8 << 16) >> (MATH_SQRT_UNSHIFT - half);
            }
        }
        {
            s32 neg_b = -dot2;
            s32 numerator;
            s32 denom;
            s32 t_far;
            s32 t_near;
            disc_root <<= 9;
            numerator = (neg_b + disc_root) << 8;
            denom = dist_sq * 2;
            t_far = numerator / denom;
            t_near = ((neg_b - disc_root) << 8) / denom;

            {
                /* the two crossings, of SEGMENT_T_ONE: one at or past the
                 * start and the other at or before the end */
                s32 meets = 0;
                if (t_far >= 0) {
                    meets = t_near <= SEGMENT_T_ONE;
                }
                result = meets;
            }
        }
    }
    return result;
}
}

/* Whether a sphere reaches a triangle that has a frame. In the frame the
 * triangle is flat, so the sphere cuts its plane in a circle: the circle's
 * center is inside the triangle, or one of the triangle's edges passes
 * through the circle. `core` 1: test the center kept from the last call
 * against another radius (the core). */
s32 hit_SphereMeetsTriangle(
    s32 core, WeaponHitScratch *scr, s32 *center, s32 radius, s32 reach_sq) {
    if (!core) {
        scr->angles.vx = center[0] - PTR(scr->shape_points[0])->x;
        scr->angles.vy = center[1] - PTR(scr->shape_points[0])->y;
        scr->angles.vz = center[2] - PTR(scr->shape_points[0])->z;
        gte_ApplyRotMatrix(&scr->angles, &scr->hit_points[0]);
    }

    {
        /* how far the center is from the triangle's plane */
        s32 height = scr->hit_points[0].z;
        if (height < -radius || radius < height) {
            return 0;
        }
    }

    {
        s32 a_x = scr->edge_a.x;
        s32 b_x = scr->edge_b.x;
        s32 a_y = scr->edge_a.y;
        s32 b_y = scr->edge_b.y;
        s32 mid_x = (a_x + b_x) / 3;
        s32 mid_y = (a_y + b_y) / 3;
        s32 point_x = scr->hit_points[0].x;
        s32 point_y = scr->hit_points[0].y;
        /* which side of an edge the triangle's middle is on, and which
         * side the point: the same for all three edges means inside */
        s32 centroid_side = a_y * mid_x - a_x * mid_y;
        s32 point_side = a_y * point_x - a_x * point_y;

        if ((centroid_side ^ point_side) >= 0) {
            centroid_side = b_y * mid_x - b_x * mid_y;
            point_side = b_y * point_x - b_x * point_y;
            if ((centroid_side ^ point_side) >= 0) {
                s32 mid_dx = mid_x - a_x;
                s32 dx;
                /* FAKE: b_y - a_y kept in the parameter, which is free */
                core = b_y - a_y;
                dx = b_x - a_x;
                centroid_side = (core * mid_dx) - (dx * (mid_y - a_y));
                point_side = (core * (point_x - a_x)) - (dx * (point_y - a_y));
                if ((centroid_side ^ point_side) >= 0) {
                    return 1;
                }
            }
        }
    }

    {
        s32 height = scr->hit_points[0].z;
        s32 lz; /* the number's leading zeros */
        s32 circle_radius_sq = reach_sq - height * height;
        s32 circle_radius;
        /* the triangle's corners from the circle's center */
        Vec3 *corner_a;
        Vec3 *corner_b;
        Vec3 *corner_0;

        /* MATH_SQRT, written out */
        if ((u32)circle_radius_sq < MATH_SQRT_TABLE_SIZE) {
            circle_radius = g_math_sqrt8_table[circle_radius_sq] >> 3;
        } else {
            s32 number = circle_radius_sq;
            s32 lzcr = 0;
            if (circle_radius_sq >= 0) {
                /* FAKE: stored again */
                number = circle_radius_sq;
                gte_Lzc(number, &lz);
                lzcr = lz;
            }
            {
                s32 shift = MATH_SQRT_SHIFT - (lzcr & ~1);
                circle_radius =
                    (u32)(g_math_sqrt8_table[(u32)number >> shift] << 16) >>
                    (MATH_SQRT_UNSHIFT - ((u32)shift >> 1));
            }
        }

        corner_a = &scr->relative_points[0];
        corner_b = &scr->relative_points[1];
        corner_a->x = scr->edge_a.x - scr->hit_points[0].x;
        corner_a->y = scr->edge_a.y - scr->hit_points[0].y;
        corner_b->x = scr->edge_b.x - scr->hit_points[0].x;
        corner_b->y = scr->edge_b.y - scr->hit_points[0].y;
        if (math_SegmentMeetsCircle(
                circle_radius, circle_radius_sq, corner_a, corner_b)) {
            return 1;
        }

        corner_0 = &scr->hit_points[1];
        corner_0->x = -scr->hit_points[0].x;
        corner_0->y = -scr->hit_points[0].y;
        if (math_SegmentMeetsCircle(
                circle_radius, circle_radius_sq, corner_0, corner_a)) {
            return 1;
        }

        if (math_SegmentMeetsCircle(
                circle_radius, circle_radius_sq, corner_0, corner_b)) {
            return 1;
        }
        return 0;
    }
}

/* The frame of hit_FrameTriangle for the blade against blade test, which
 * has no use for a thin triangle: returns 0 for one. */
s32 hit_FrameTriangleByNormal(WeaponHitScratch *scr) {
    MATRIX *mat;
    s32 xz_len;

    scr->edge_a.x = PTR(scr->shape_points[1])->x - PTR(scr->shape_points[0])->x;
    scr->edge_a.y = PTR(scr->shape_points[1])->y - PTR(scr->shape_points[0])->y;
    scr->edge_a.z = PTR(scr->shape_points[1])->z - PTR(scr->shape_points[0])->z;

    scr->edge_b.x = PTR(scr->shape_points[2])->x - PTR(scr->shape_points[0])->x;
    scr->edge_b.y = PTR(scr->shape_points[2])->y - PTR(scr->shape_points[0])->y;
    scr->edge_b.z = PTR(scr->shape_points[2])->z - PTR(scr->shape_points[0])->z;

    gte_OuterProduct0(&scr->edge_a, &scr->edge_b, &scr->normal.vector);

    if ((scr->normal.vector.x > -THIN_NORMAL &&
         scr->normal.vector.x < THIN_NORMAL) &&
        (scr->normal.vector.y > -THIN_NORMAL &&
         scr->normal.vector.y < THIN_NORMAL) &&
        (scr->normal.vector.z > -THIN_NORMAL &&
         scr->normal.vector.z < THIN_NORMAL)) {
        return 0;
    }

    scr->angles.vy =
        ANGLE_TURN / 2 - ratan2(scr->normal.vector.x, scr->normal.vector.z);
    scr->normal.vector.x = scr->normal.vector.x >> HIT_NORMAL_SHIFT;
    scr->normal.vector.y = scr->normal.vector.y >> HIT_NORMAL_SHIFT;
    scr->normal.vector.z = scr->normal.vector.z >> HIT_NORMAL_SHIFT;

    xz_len = math_TableSqrt(scr->normal.vector.x * scr->normal.vector.x +
                            scr->normal.vector.z * scr->normal.vector.z);

    scr->angles.vx = ANGLE_TURN / 2 - ratan2(scr->normal.vector.y, xz_len);
    mat = &scr->rot_matrix;

    scr->rot_matrix.m[0][0] = ONE;
    scr->rot_matrix.m[0][1] = 0;
    scr->rot_matrix.m[0][2] = 0;
    scr->rot_matrix.m[1][0] = 0;
    scr->rot_matrix.m[1][1] = ONE;
    scr->rot_matrix.m[1][2] = 0;
    scr->rot_matrix.m[2][0] = 0;
    scr->rot_matrix.m[2][1] = 0;
    scr->rot_matrix.m[2][2] = ONE;
    RotMatrixY(scr->angles.vy, mat);
    RotMatrixX(scr->angles.vx, mat);

    gte_SetRotMatrix(mat);
    gte_ldlv0(&scr->edge_a);
    gte_rtv0();
    gte_stlvnl(&scr->edge_a);
    gte_ldlv0(&scr->edge_b);
    gte_rtv0();
    gte_stlvnl(&scr->edge_b);

    return 1;
}

/* Whether a second triangle meets the one that has the frame. Its corners
 * are turned into the frame, where the first triangle lies flat at z = 0:
 * if they are all to one side of it the two cannot meet; otherwise the
 * second triangle cuts the plane in a segment, and the two meet if an end
 * of the segment is inside the first triangle or (a looser test than a
 * true crossing) the segment and one of the triangle's edges each lie to
 * one side of the other's line. */
s32 hit_TrianglesMeet(
    WeaponHitScratch *scr, Vec3 *corner_0, Vec3 *corner_1, Vec3 *corner_2) {
    s32 i;
    s32 max_i;
    s32 min_i;
    s32 mid_i;
    s32 min_z;
    s32 max_z;
    s32 z;
    s32 dz_a;
    s32 dz_b;
    s32 x1;
    s32 y1;
    s32 x2;
    s32 y2;
    s32 cx;
    s32 cy;
    /* which side of a line each of two points is on */
    s32 cross_a;
    s32 cross_b;
    /* edge A-B x ((x2,y2) - A) */
    s32 cross_ab2;

    scr->angles.vx = corner_0->x - PTR(scr->shape_points[0])->x;
    scr->angles.vy = corner_0->y - PTR(scr->shape_points[0])->y;
    scr->angles.vz = corner_0->z - PTR(scr->shape_points[0])->z;
    gte_ldv0(&scr->angles);
    gte_rtv0();
    scr->angles.vx = corner_1->x - PTR(scr->shape_points[0])->x;
    scr->angles.vy = corner_1->y - PTR(scr->shape_points[0])->y;
    scr->angles.vz = corner_1->z - PTR(scr->shape_points[0])->z;
    gte_stlvnl(&scr->relative_points[0]);
    gte_ldv0(&scr->angles);
    gte_rtv0();
    scr->angles.vx = corner_2->x - PTR(scr->shape_points[0])->x;
    scr->angles.vy = corner_2->y - PTR(scr->shape_points[0])->y;
    scr->angles.vz = corner_2->z - PTR(scr->shape_points[0])->z;
    gte_stlvnl(&scr->relative_points[1]);
    gte_ApplyRotMatrix(&scr->angles, &scr->relative_points[2]);

    max_i = 0;
    min_i = 0;
    max_z = scr->relative_points[0].z;
    min_z = max_z;
    for (i = 1; i < 3; i++) {
        z = scr->relative_points[i].z;
        if (z < min_z) {
            min_i = i;
            min_z = z;
        } else if (max_z < z) {
            max_i = i;
            max_z = z;
        }
    }
    if (min_z > 0 || max_z < 0) {
        return 0;
    }
    if (min_z == 0 && max_z == 0) {
        min_i = 0;
        max_i = 1;
        scr->relative_points[0].z--;
        scr->relative_points[1].z++;
    }

    /* where the min->max edge crosses z = 0 */
    dz_a = scr->relative_points[max_i].z - scr->relative_points[min_i].z;
    if (dz_a == 0) {
        g_hit_flat_triangle_count++;
        dz_a = 1;
    }
    x1 = scr->relative_points[min_i].x +
         (-scr->relative_points[min_i].z *
          (scr->relative_points[max_i].x - scr->relative_points[min_i].x)) /
             dz_a;
    y1 = scr->relative_points[min_i].y +
         (-scr->relative_points[min_i].z *
          (scr->relative_points[max_i].y - scr->relative_points[min_i].y)) /
             dz_a;

    /* where the edge through the middle point crosses z = 0 */
    mid_i = 3 - min_i - max_i;
    if (scr->relative_points[mid_i].z >= 0) {
        dz_b = scr->relative_points[mid_i].z - scr->relative_points[min_i].z;
        if (dz_b == 0) {
            g_hit_flat_triangle_count++;
            dz_b = 1;
        }
        x2 = scr->relative_points[min_i].x +
             (-scr->relative_points[min_i].z *
              (scr->relative_points[mid_i].x - scr->relative_points[min_i].x)) /
                 dz_b;
        y2 = scr->relative_points[min_i].y +
             (-scr->relative_points[min_i].z *
              (scr->relative_points[mid_i].y - scr->relative_points[min_i].y)) /
                 dz_b;
    } else {
        dz_b = scr->relative_points[max_i].z - scr->relative_points[mid_i].z;
        if (dz_b == 0) {
            g_hit_flat_triangle_count++;
            dz_b = 1;
        }
        x2 = scr->relative_points[mid_i].x +
             (-scr->relative_points[mid_i].z *
              (scr->relative_points[max_i].x - scr->relative_points[mid_i].x)) /
                 dz_b;
        y2 = scr->relative_points[mid_i].y +
             (-scr->relative_points[mid_i].z *
              (scr->relative_points[max_i].y - scr->relative_points[mid_i].y)) /
                 dz_b;
    }

    cx = (scr->edge_a.x + scr->edge_b.x) / 3;
    cy = (scr->edge_a.y + scr->edge_b.y) / 3;

    /* (x1,y1) inside the triangle */
    cross_a = scr->edge_a.y * cx - scr->edge_a.x * cy;
    cross_b = scr->edge_a.y * x1 - scr->edge_a.x * y1;
    if ((cross_a ^ cross_b) >= 0) {
        cross_a = scr->edge_b.y * cx - scr->edge_b.x * cy;
        cross_b = scr->edge_b.y * x1 - scr->edge_b.x * y1;
        if ((cross_a ^ cross_b) >= 0) {
            cross_a = (scr->edge_b.y - scr->edge_a.y) * (cx - scr->edge_a.x) -
                      (scr->edge_b.x - scr->edge_a.x) * (cy - scr->edge_a.y);
            cross_b = (scr->edge_b.y - scr->edge_a.y) * (x1 - scr->edge_a.x) -
                      (scr->edge_b.x - scr->edge_a.x) * (y1 - scr->edge_a.y);
            if ((cross_a ^ cross_b) >= 0) {
                return 1;
            }
        }
    }
    /* (x2,y2) inside the triangle */
    cross_a = scr->edge_a.y * cx - scr->edge_a.x * cy;
    cross_b = scr->edge_a.y * x2 - scr->edge_a.x * y2;
    if ((cross_a ^ cross_b) >= 0) {
        cross_a = scr->edge_b.y * cx - scr->edge_b.x * cy;
        cross_b = scr->edge_b.y * x2 - scr->edge_b.x * y2;
        if ((cross_a ^ cross_b) >= 0) {
            cross_a = (scr->edge_b.y - scr->edge_a.y) * (cx - scr->edge_a.x) -
                      (scr->edge_b.x - scr->edge_a.x) * (cy - scr->edge_a.y);
            cross_b = (scr->edge_b.y - scr->edge_a.y) * (x2 - scr->edge_a.x) -
                      (scr->edge_b.x - scr->edge_a.x) * (y2 - scr->edge_a.y);
            if ((cross_a ^ cross_b) >= 0) {
                return 1;
            }
        }
    }
    /* the segment against edge (0,0)-A */
    cross_a = scr->edge_a.y * x1 - scr->edge_a.x * y1;
    cross_b = scr->edge_a.y * x2 - scr->edge_a.x * y2;
    if ((cross_a ^ cross_b) >= 0) {
        cross_a =
            (y2 - y1) * (scr->edge_a.x - x1) - (x2 - x1) * (scr->edge_a.y - y1);
        cross_b = (y2 - y1) * -x1 - (x2 - x1) * -y1;
        if ((cross_a ^ cross_b) >= 0) {
            return 1;
        }
    }
    /* the segment against edge (0,0)-B */
    cross_a = scr->edge_b.y * x1 - scr->edge_b.x * y1;
    cross_b = scr->edge_b.y * x2 - scr->edge_b.x * y2;
    if ((cross_a ^ cross_b) >= 0) {
        cross_a =
            (y2 - y1) * (scr->edge_b.x - x1) - (x2 - x1) * (scr->edge_b.y - y1);
        cross_b = (y2 - y1) * -x1 - (x2 - x1) * -y1;
        if ((cross_a ^ cross_b) >= 0) {
            return 1;
        }
    }
    /* the segment against edge A-B */
    cross_a = (scr->edge_b.y - scr->edge_a.y) * (x1 - scr->edge_a.x) -
              (scr->edge_b.x - scr->edge_a.x) * (y1 - scr->edge_a.y);
    cross_ab2 = (scr->edge_b.y - scr->edge_a.y) * (x2 - scr->edge_a.x) -
                (scr->edge_b.x - scr->edge_a.x) * (y2 - scr->edge_a.y);
    if ((cross_a ^ cross_ab2) >= 0) {
        cross_a =
            (y2 - y1) * (scr->edge_a.x - x1) - (x2 - x1) * (scr->edge_a.y - y1);
        cross_b =
            (y2 - y1) * (scr->edge_b.x - x1) - (x2 - x1) * (scr->edge_b.y - y1);
        if ((cross_a ^ cross_b) >= 0) {
            return 1;
        }
    }
    return 0;
}

/* Whether a point is inside a triangle, in x and z: it is on the same side
 * of every edge as the triangle's middle. */
s32 math_PointInTriangleXZ(
    s32 *vertex_a, s32 *vertex_b, s32 *vertex_c, s32 *test_point) {
    s32 centroid_x = ((vertex_a[0] + vertex_b[0]) + vertex_c[0]) / 3;
    s32 centroid_z = ((vertex_a[2] + vertex_b[2]) + vertex_c[2]) / 3;
    s32 centroid_cross;
    s32 point_cross;

    {
        s32 dz = vertex_b[2] - vertex_a[2];
        s32 dx = vertex_b[0] - vertex_a[0];
        centroid_cross = (dz * (centroid_x - vertex_a[0])) -
                         (dx * (centroid_z - vertex_a[2]));
        point_cross = (dz * (test_point[0] - vertex_a[0])) -
                      (dx * (test_point[2] - vertex_a[2]));
    }
    if ((centroid_cross ^ point_cross) >= 0) {
        {
            s32 dz = vertex_c[2] - vertex_a[2];
            s32 dx = vertex_c[0] - vertex_a[0];
            centroid_cross = (dz * (centroid_x - vertex_a[0])) -
                             (dx * (centroid_z - vertex_a[2]));
            point_cross = (dz * (test_point[0] - vertex_a[0])) -
                          (dx * (test_point[2] - vertex_a[2]));
        }
        if ((centroid_cross ^ point_cross) >= 0) {
            {
                s32 dz = vertex_c[2] - vertex_b[2];
                s32 dx = vertex_c[0] - vertex_b[0];
                centroid_cross = (dz * (centroid_x - vertex_b[0])) -
                                 (dx * (centroid_z - vertex_b[2]));
                point_cross = (dz * (test_point[0] - vertex_b[0])) -
                              (dx * (test_point[2] - vertex_b[2]));
            }
            if ((centroid_cross ^ point_cross) >= 0) {
                return 1;
            }
        }
    }
    return 0;
}

/* Gives a segment (the first two shape points) its frame: its length along
 * z. */
void hit_FrameSegment(WeaponHitScratch *scr) {
    MATRIX *mat;
    s32 angle;
    s32 dist;

    scr->edge_a.x = PTR(scr->shape_points[1])->x - PTR(scr->shape_points[0])->x;
    scr->edge_a.y = PTR(scr->shape_points[1])->y - PTR(scr->shape_points[0])->y;
    scr->edge_a.z = PTR(scr->shape_points[1])->z - PTR(scr->shape_points[0])->z;
    angle = ratan2(scr->edge_a.x, scr->edge_a.z);
    scr->angles.vy = ANGLE_TURN / 2 - angle;

    dist = math_TableSqrt(
        scr->edge_a.x * scr->edge_a.x + scr->edge_a.z * scr->edge_a.z);

    angle = ratan2(scr->edge_a.y, dist);
    mat = &scr->rot_matrix;
    scr->angles.vx = ANGLE_TURN / 2 - angle;

    scr->rot_matrix.m[0][0] = ONE;
    scr->rot_matrix.m[0][1] = 0;
    scr->rot_matrix.m[0][2] = 0;
    scr->rot_matrix.m[1][0] = 0;
    scr->rot_matrix.m[1][1] = ONE;
    scr->rot_matrix.m[1][2] = 0;
    scr->rot_matrix.m[2][0] = 0;
    scr->rot_matrix.m[2][1] = 0;
    scr->rot_matrix.m[2][2] = ONE;
    RotMatrixY(scr->angles.vy, mat);
    RotMatrixX(scr->angles.vx, mat);

    gte_SetRotMatrix(mat);
    gte_ldlv0(&scr->edge_a);
    gte_rtv0();
    gte_stlvnl(&scr->edge_a);
}

/* Whether a sphere reaches the segment that has the frame. */
s32 hit_SphereMeetsSegment(
    WeaponHitScratch *scr, Vec3 *center, s32 radius, s32 reach_sq) {
    scr->angles.vx = center->x - PTR(scr->shape_points[0])->x;
    scr->angles.vy = center->y - PTR(scr->shape_points[0])->y;
    scr->angles.vz = center->z - PTR(scr->shape_points[0])->z;
    gte_ApplyRotMatrix(&scr->angles, &scr->hit_points[0]);

    {
        s32 y;
        /* half the length of the sphere's cut along the segment: squared,
         * then not */
        s32 half_cut;
        s32 min_z;
        s32 max_z;
        s32 edge_z;
        s32 z;
        s32 x;
        s32 neg_radius = -radius;
        s32 dist_sq;

        x = scr->hit_points[0].x;
        if (x < neg_radius || radius < x) {
            return 0;
        }
        y = scr->hit_points[0].y;
        if (y < neg_radius || radius < y) {
            return 0;
        }

        dist_sq = x * x + y * y;
        if (reach_sq < dist_sq) {
            return 0;
        }
        half_cut = reach_sq - dist_sq;

        MATH_SQRT(half_cut, half_cut);

        max_z = 0;
        min_z = 0;
        edge_z = scr->edge_a.z;
        if (edge_z < 0) {
            min_z = edge_z;
        } else {
            max_z = edge_z;
        }
        z = scr->hit_points[0].z;
        if (max_z < z - half_cut || z + half_cut < min_z) {
            return 0;
        }
        return 1;
    }
}

/* Scales the part of a vector that lies along a surface's normal by
 * `along` and the part across it by `across` (both of SCALE_ALONG_ONE):
 * what is left of a speed or a move after meeting the surface. */
void math_ScaleAlong(
    s32 *vector, s16 *normal, s32 *out, s32 along, s32 across) {
    WeaponHitScratch *scr = &SPAD->tail.weapon_hit;
    MATRIX *mat;
    Vec3 *framed;
    s32 heading;
    s32 pitch;
    s32 dist;

    heading = ratan2(normal[0], normal[2]);
    scr->angles.vy = ANGLE_TURN / 2 - heading;
    dist = math_TableSqrt(normal[0] * normal[0] + normal[2] * normal[2]);

    pitch = ratan2(normal[1], dist);
    mat = &scr->rot_matrix;
    scr->angles.vx = ANGLE_TURN / 2 - pitch;

    scr->rot_matrix.m[0][0] = ONE;
    scr->rot_matrix.m[0][1] = 0;
    scr->rot_matrix.m[0][2] = 0;
    scr->rot_matrix.m[1][0] = 0;
    scr->rot_matrix.m[1][1] = ONE;
    scr->rot_matrix.m[1][2] = 0;
    scr->rot_matrix.m[2][0] = 0;
    scr->rot_matrix.m[2][1] = 0;
    scr->rot_matrix.m[2][2] = ONE;
    RotMatrixY(scr->angles.vy, mat);
    RotMatrixX(scr->angles.vx, mat);

    gte_SetRotMatrix(mat);
    gte_ldlv0(vector);
    gte_rtv0();
    framed = &scr->edge_a;
    gte_stlvnl(framed);

    framed->z = (framed->z * along) / SCALE_ALONG_ONE;
    framed->x = (framed->x * across) / SCALE_ALONG_ONE;
    framed->y = (framed->y * across) / SCALE_ALONG_ONE;

    scr->rot_matrix.m[0][0] = ONE;
    scr->rot_matrix.m[0][1] = 0;
    scr->rot_matrix.m[0][2] = 0;
    scr->rot_matrix.m[1][0] = 0;
    scr->rot_matrix.m[1][1] = ONE;
    scr->rot_matrix.m[1][2] = 0;
    scr->rot_matrix.m[2][0] = 0;
    scr->rot_matrix.m[2][1] = 0;
    scr->rot_matrix.m[2][2] = ONE;
    RotMatrixX(-scr->angles.vx, mat);
    RotMatrixY(-scr->angles.vy, mat);

    gte_SetRotMatrix(mat);
    gte_ldlv0(framed);
    gte_rtv0();
    gte_stlvnl(out);
}

/* The inverse of a matrix's rotation. */
void math_InvertMatrix(MATRIX *matrix, MATRIX *inverse) {
    s16 m22;
    s16 m11;
    s16 m21;
    s16 m12;
    s16 m02;
    s16 m01;
    s32 cofactor00;
    s32 negated_determinant;

    m12 = matrix->m[1][2];
    m21 = matrix->m[2][1];
    m11 = matrix->m[1][1];
    m22 = matrix->m[2][2];
    cofactor00 = (m12 * m21) - (m11 * m22);
    m01 = matrix->m[0][1];
    m02 = matrix->m[0][2];
    /* each product of ONE times ONE, brought down to ONE */
    negated_determinant =
        ((matrix->m[0][0] * (cofactor00 >> 12)) +
         (matrix->m[1][0] * (((m01 * m22) - (m02 * m21)) >> 12)) +
         (matrix->m[2][0] * (((m02 * m11) - (m01 * m12)) >> 12))) >>
        12;
    inverse->m[0][0] = cofactor00 / negated_determinant;
    inverse->m[0][1] = ((matrix->m[0][1] * matrix->m[2][2]) -
                        (matrix->m[0][2] * matrix->m[2][1])) /
                       negated_determinant;
    inverse->m[0][2] = ((matrix->m[0][2] * matrix->m[1][1]) -
                        (matrix->m[0][1] * matrix->m[1][2])) /
                       negated_determinant;
    inverse->m[1][0] = ((matrix->m[1][0] * matrix->m[2][2]) -
                        (matrix->m[1][2] * matrix->m[2][0])) /
                       negated_determinant;
    inverse->m[1][1] = ((matrix->m[0][2] * matrix->m[2][0]) -
                        (matrix->m[0][0] * matrix->m[2][2])) /
                       negated_determinant;
    inverse->m[1][2] = ((matrix->m[0][0] * matrix->m[1][2]) -
                        (matrix->m[0][2] * matrix->m[1][0])) /
                       negated_determinant;
    inverse->m[2][0] = ((matrix->m[1][1] * matrix->m[2][0]) -
                        (matrix->m[1][0] * matrix->m[2][1])) /
                       negated_determinant;
    inverse->m[2][1] = ((matrix->m[0][0] * matrix->m[2][1]) -
                        (matrix->m[0][1] * matrix->m[2][0])) /
                       negated_determinant;
    inverse->m[2][2] = ((matrix->m[0][1] * matrix->m[1][0]) -
                        (matrix->m[0][0] * matrix->m[1][1])) /
                       negated_determinant;
}

/* The three angles that make a rotation matrix. */
void math_MatrixToAngles(MATRIX *matrix, s16 *angles_out) {
    MATRIX *rot;
    WeaponHitScratch *scr;
    MATRIX *mat;
    Vec3 *framed;
    s32 cofactor_0, cofactor_1, cofactor_2;
    /* FAKE: the determinant, then a square root */
    s32 work;
    s32 det_term;
    /* the first two rows of the inverse: where the x and the y axis go */
    s32 i0, i1, i2;
    s32 r0, r1, r2;
    s32 angle_z, angle_y;

    rot = &SPAD->tail.weapon_hit.rot_matrix;
    *rot = *matrix;

    cofactor_0 = rot->m[1][2] * rot->m[2][1] - rot->m[1][1] * rot->m[2][2];
    det_term = rot->m[0][0] * (cofactor_0 >> 12);
    cofactor_1 = rot->m[0][1] * rot->m[2][2] - rot->m[0][2] * rot->m[2][1];
    cofactor_2 = rot->m[0][2] * rot->m[1][1] - rot->m[0][1] * rot->m[1][2];
    /* the determinant, of ONE */
    work = (det_term + rot->m[1][0] * (cofactor_1 >> 12) +
            rot->m[2][0] * (cofactor_2 >> 12)) >>
           12;
    i0 = cofactor_0 / work;
    i1 = cofactor_1 / work;
    i2 = cofactor_2 / work;
    r0 = (rot->m[1][0] * rot->m[2][2] - rot->m[1][2] * rot->m[2][0]) / work;
    r1 = (rot->m[0][2] * rot->m[2][0] - rot->m[0][0] * rot->m[2][2]) / work;
    r2 = (rot->m[0][0] * rot->m[1][2] - rot->m[0][2] * rot->m[1][0]) / work;

    angle_z = -ratan2(i1, i0);
    scr = &SPAD->tail.weapon_hit;
    work = math_TableSqrt(i0 * i0 + i1 * i1);

    angle_y = ratan2(i2, work);
    mat = &scr->rot_matrix;
    scr->rot_matrix.m[0][0] = ONE;
    scr->rot_matrix.m[0][1] = 0;
    scr->rot_matrix.m[0][2] = 0;
    scr->rot_matrix.m[1][0] = 0;
    scr->rot_matrix.m[1][1] = ONE;
    scr->rot_matrix.m[1][2] = 0;
    scr->rot_matrix.m[2][0] = 0;
    scr->rot_matrix.m[2][1] = 0;
    scr->rot_matrix.m[2][2] = ONE;
    RotMatrixZ(angle_z, mat);
    RotMatrixY(angle_y, mat);

    gte_SetRotMatrix(mat);
    framed = &scr->edge_a;
    framed->x = r0;
    framed->y = r1;
    framed->z = r2;
    gte_ldlv0(framed);
    gte_rtv0();
    gte_stlvnl(framed);

    angles_out[0] = ratan2(framed->z, framed->y);
    angles_out[1] = -angle_y;
    angles_out[2] = -angle_z;
}

/* Turns the rotation three angles stand for by three more, and writes the
 * angles of the result back: how a pose's torso is turned towards the
 * opponent. */
void math_TurnAngles(s16 *angles, s32 z, s32 y, s32 x) {
    MATRIX *rot;
    WeaponHitScratch *scr;
    MATRIX *mat;
    Vec3 *framed;
    s32 cofactor_0, cofactor_1, cofactor_2;
    /* FAKE: the determinant, then a square root */
    s32 work;
    s32 det_term;
    /* the first two rows of the inverse: where the x and the y axis go */
    s32 i0, i1, i2;
    s32 r0, r1, r2;
    s32 angle_z, angle_y;

    scr = &SPAD->tail.weapon_hit;
    scr->rot_matrix.m[0][0] = ONE;
    scr->rot_matrix.m[0][1] = 0;
    scr->rot_matrix.m[0][2] = 0;
    scr->rot_matrix.m[1][0] = 0;
    scr->rot_matrix.m[1][1] = ONE;
    scr->rot_matrix.m[1][2] = 0;
    scr->rot_matrix.m[2][0] = 0;
    scr->rot_matrix.m[2][1] = 0;
    scr->rot_matrix.m[2][2] = ONE;
    RotMatrixX(x, &scr->rot_matrix);
    RotMatrixY(y, &scr->rot_matrix);
    RotMatrixZ(-z, &scr->rot_matrix);
    RotMatrixX(angles[0], &scr->rot_matrix);
    RotMatrixY(angles[1], &scr->rot_matrix);
    RotMatrixZ(angles[2], &scr->rot_matrix);

    rot = &scr->rot_matrix;
    cofactor_0 = rot->m[1][2] * rot->m[2][1] - rot->m[1][1] * rot->m[2][2];
    det_term = rot->m[0][0] * (cofactor_0 >> 12);
    cofactor_1 = rot->m[0][1] * rot->m[2][2] - rot->m[0][2] * rot->m[2][1];
    cofactor_2 = rot->m[0][2] * rot->m[1][1] - rot->m[0][1] * rot->m[1][2];
    /* the determinant, of ONE */
    work = (det_term + rot->m[1][0] * (cofactor_1 >> 12) +
            rot->m[2][0] * (cofactor_2 >> 12)) >>
           12;
    i0 = cofactor_0 / work;
    i1 = cofactor_1 / work;
    i2 = cofactor_2 / work;
    r0 = (rot->m[1][0] * rot->m[2][2] - rot->m[1][2] * rot->m[2][0]) / work;
    r1 = (rot->m[0][2] * rot->m[2][0] - rot->m[0][0] * rot->m[2][2]) / work;
    r2 = (rot->m[0][0] * rot->m[1][2] - rot->m[0][2] * rot->m[1][0]) / work;

    angle_z = -ratan2(i1, i0);
    work = math_TableSqrt(i0 * i0 + i1 * i1);

    angle_y = ratan2(i2, work);
    mat = &scr->rot_matrix;
    scr->rot_matrix.m[0][0] = ONE;
    scr->rot_matrix.m[0][1] = 0;
    scr->rot_matrix.m[0][2] = 0;
    scr->rot_matrix.m[1][0] = 0;
    scr->rot_matrix.m[1][1] = ONE;
    scr->rot_matrix.m[1][2] = 0;
    scr->rot_matrix.m[2][0] = 0;
    scr->rot_matrix.m[2][1] = 0;
    scr->rot_matrix.m[2][2] = ONE;
    RotMatrixZ(angle_z, mat);
    RotMatrixY(angle_y, mat);

    gte_SetRotMatrix(mat);
    framed = &scr->edge_a;
    framed->x = r0;
    framed->y = r1;
    framed->z = r2;
    gte_ldlv0(framed);
    gte_rtv0();
    gte_stlvnl(framed);

    angles[0] = ratan2(framed->z, framed->y);
    angles[1] = -angle_y;
    angles[2] = -angle_z;
}

/* The heading a triangle's normal points along, turned about if the normal
 * points down: the way a cut prop falls. */
s32 math_TriangleHeading(
    VECTOR *first_vertex, VECTOR *second_vertex, VECTOR *third_vertex) {
    Vec3 *normal;
    s32 heading;

    SPAD->tail.weapon_hit.edge_a.x = second_vertex->vx - first_vertex->vx;
    SPAD->tail.weapon_hit.edge_a.y = second_vertex->vy - first_vertex->vy;
    SPAD->tail.weapon_hit.edge_a.z = second_vertex->vz - first_vertex->vz;
    SPAD->tail.weapon_hit.edge_b.x = third_vertex->vx - first_vertex->vx;
    SPAD->tail.weapon_hit.edge_b.y = third_vertex->vy - first_vertex->vy;
    SPAD->tail.weapon_hit.edge_b.z = third_vertex->vz - first_vertex->vz;

    /* the two edges' cross product */
    gte_ldopv1(&SPAD->tail.weapon_hit.edge_a);
    gte_ldopv2(&SPAD->tail.weapon_hit.edge_b);
    gte_op0();
    normal = &SPAD->tail.weapon_hit.normal.vector;
    gte_stlvnl(normal);
    heading = ratan2(normal->x, normal->z);
    if (normal->y > 0) {
        heading += ANGLE_TURN / 2;
    }
    return heading;
}

/* Which way up a fighter on the ground lies, from the three points of its
 * chest. Read as face up; it may be the other way. */
s32 fighter_IsFaceUp(Fighter *fighter) {
    Vec3 *body;

    body = SPAD->body_points[fighter->index];
    SPAD->tail.weapon_hit.edge_a.x =
        body[BODY_CHEST + 1].x - body[BODY_CHEST].x;
    SPAD->tail.weapon_hit.edge_a.y =
        body[BODY_CHEST + 1].y - body[BODY_CHEST].y;
    SPAD->tail.weapon_hit.edge_a.z =
        body[BODY_CHEST + 1].z - body[BODY_CHEST].z;
    SPAD->tail.weapon_hit.edge_b.x =
        body[BODY_CHEST + 2].x - body[BODY_CHEST].x;
    SPAD->tail.weapon_hit.edge_b.y =
        body[BODY_CHEST + 2].y - body[BODY_CHEST].y;
    SPAD->tail.weapon_hit.edge_b.z =
        body[BODY_CHEST + 2].z - body[BODY_CHEST].z;

    gte_OuterProduct0(
        &SPAD->tail.weapon_hit.edge_a, &SPAD->tail.weapon_hit.edge_b,
        &SPAD->tail.weapon_hit.normal.vector);
    return 0 < SPAD->tail.weapon_hit.normal.vector.y;
}

/* The file's small data. */
/* counts the times the blade against blade test found a triangle edge-on */
static s32 g_hit_flat_triangle_count = 0;
