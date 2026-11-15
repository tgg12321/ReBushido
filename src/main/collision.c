/* Collision with the stage: casting a segment across the stage's grid of
 * cells to find the nearest face it goes through (collision.h).
 *
 * The collision grid is part of the stage file. It begins with one halfword
 * a cell, the offset of that cell's faces from the start of the grid
 * (NO_FACES: the cell is empty). A cell's faces are a count and then, for
 * each face, all in halfwords:
 *   the plane: a normal (three, 0x4000 = 1) and the distance term (two);
 *   an origin on the face and its two axes (three each, 0x4000 = 1);
 *   the surface kind (high byte) and the number of edges (low byte);
 *   the outline: edges + 1 points (u, v) on those axes, going round.
 * Everything is measured from the cell's corner. A face is flat and convex,
 * and is hit only from its front. */
#include "common.h"
#include "bb2.h"

/* Defined further down. */
static s32 collision_TestCell(s32 cell_x, s32 cell_z);
static s32 collision_TestCellFine(s32 cell_x, s32 cell_z);
static s32 collision_ReadHit(s32 *hit_point, s16 *hit_normal);

/* SegmentCast.nearest_dist_sq before anything is hit */
#define NO_HIT 0x7FFFFFFF
/* an empty cell's entry in the grid */
#define NO_FACES 0xFFFF
/* What a face's origin and axes take up, in halfwords. */
#define FACE_FRAME_SIZE 9

/* A point's distance from a plane comes out of the sum of products scaled
 * by 0x4000, the file's 1. The fine test keeps four more bits of it. */
#define PLANE_SHIFT 14
#define FINE_SHIFT 10
/* From the file's normal to the one handed back, whose length is ONE. */
#define PLANE_NORMAL_SHIFT (PLANE_SHIFT - 12)
/* The halfword after a face's frame: the surface kind over the number of
 * edges. */
#define FACE_SURFACE_SHIFT 8
#define FACE_EDGES_MASK 0xFF
/* x >> n and x / 2, rounding the magnitude so that both signs go the same
 * way. */
#define SHIFT_MAGNITUDE(x, n)                                                  \
    (((x) < 0 ? -1 : 1) * (((x) < 0 ? -(x) : (x)) >> (n)))
#define HALF_ROUNDED(x)                                                        \
    (((x) < 0 ? -1 : 1) * (((x) >= 0 ? (x) + 1 : -(x) + 1) >> 1))

/* A move shorter than this (200 units; the length squared) gets the fine
 * test. */
#define SHORT_MOVE_SQ 40000

/* This file's own work area, and the cast in progress. */
static SegmentCast g_collision_own_cast;

/* The stage's grid, as an address (collision_SetGrid). */
static s32 g_collision_grid;
static SegmentCast *g_collision_cast;
static s16 g_collision_surface;

/* ---- the walk across the cells ---- */

/* Runs the cast that has been set up in g_collision_cast: tests the segment
 * against every cell it passes over, from its start, until a cell reports a
 * hit, and reads the result out. The segment is followed one cell at a time
 * along the axis it covers more of ("u"); whenever it has also moved into
 * the next cell across ("v"), that cell is tested before stepping on. */
static s32 collision_WalkCells(s32 *hit_point, s16 *hit_normal) {
    s32 x_direction;
    s32 z_direction;
    s32 axes_swapped;

    /* the two ends on the grid, and the cells they are in */
    g_collision_cast->walk_start_u =
        g_collision_cast->seg_start.vx + COLLISION_GRID_HALF;
    g_collision_cast->walk_v =
        g_collision_cast->seg_start.vz + COLLISION_GRID_HALF;
    g_collision_cast->walk_end_u =
        g_collision_cast->seg_end.vx + COLLISION_GRID_HALF;
    g_collision_cast->nearest_dist_sq = NO_HIT;
    g_collision_cast->walk_end_v =
        g_collision_cast->seg_end.vz + COLLISION_GRID_HALF;
    g_collision_cast->walk_delta_u =
        g_collision_cast->walk_end_u - g_collision_cast->walk_start_u;
    g_collision_cast->walk_cell.x =
        g_collision_cast->walk_start_u / COLLISION_CELL_SIZE;
    g_collision_cast->walk_cell.z =
        g_collision_cast->walk_v / COLLISION_CELL_SIZE;
    g_collision_cast->end_cell.x =
        g_collision_cast->walk_end_u / COLLISION_CELL_SIZE;
    g_collision_cast->end_cell.z =
        g_collision_cast->walk_end_v / COLLISION_CELL_SIZE;
    g_collision_cast->walk_delta_v =
        g_collision_cast->walk_end_v - g_collision_cast->walk_v;
    if (g_collision_cast->walk_cell.x == g_collision_cast->end_cell.x &&
        g_collision_cast->walk_cell.z == g_collision_cast->end_cell.z) {
        if (g_collision_cast->seg_start.vx == g_collision_cast->seg_end.vx &&
            g_collision_cast->seg_start.vy == g_collision_cast->seg_end.vy &&
            g_collision_cast->seg_start.vz == g_collision_cast->seg_end.vz) {
            return 0;
        }
        /* it stays inside one cell */
        PTR(g_collision_cast->test_cell)
        (g_collision_cast->walk_cell.x, g_collision_cast->walk_cell.z);
    } else {
        /* measure from the middle of the start cell, and flip the axes it
         * travels backwards along, so that both run forwards */
        g_collision_cast->walk_tmp =
            g_collision_cast->walk_cell.x * COLLISION_CELL_SIZE +
            COLLISION_CELL_SIZE / 2;
        g_collision_cast->start_cell_center_z =
            g_collision_cast->walk_cell.z * COLLISION_CELL_SIZE +
            COLLISION_CELL_SIZE / 2;
        g_collision_cast->walk_start_u -= g_collision_cast->walk_tmp;
        g_collision_cast->walk_v -= g_collision_cast->start_cell_center_z;
        g_collision_cast->walk_end_u -= g_collision_cast->walk_tmp;
        g_collision_cast->walk_end_v -= g_collision_cast->start_cell_center_z;
        if (g_collision_cast->walk_delta_u < 0) {
            x_direction = -1;
            g_collision_cast->walk_delta_u = -g_collision_cast->walk_delta_u;
            g_collision_cast->walk_start_u = -g_collision_cast->walk_start_u;
            g_collision_cast->walk_end_u = -g_collision_cast->walk_end_u;
        } else {
            x_direction = 1;
        }
        if (g_collision_cast->walk_delta_v < 0) {
            z_direction = -1;
            g_collision_cast->walk_delta_v = -g_collision_cast->walk_delta_v;
            g_collision_cast->walk_v = -g_collision_cast->walk_v;
            g_collision_cast->walk_end_v = -g_collision_cast->walk_end_v;
        } else {
            z_direction = 1;
        }
        /* u is the axis it covers more of */
        if (g_collision_cast->walk_delta_v > g_collision_cast->walk_delta_u) {
            axes_swapped = 1;
            g_collision_cast->walk_tmp = g_collision_cast->walk_delta_u;
            g_collision_cast->walk_delta_u = g_collision_cast->walk_delta_v;
            g_collision_cast->walk_delta_v = g_collision_cast->walk_tmp;
            g_collision_cast->walk_tmp = g_collision_cast->walk_start_u;
            g_collision_cast->walk_start_u = g_collision_cast->walk_v;
            g_collision_cast->walk_v = g_collision_cast->walk_tmp;
            g_collision_cast->walk_tmp = g_collision_cast->walk_end_u;
            g_collision_cast->walk_end_u = g_collision_cast->walk_end_v;
            g_collision_cast->walk_end_v = g_collision_cast->walk_tmp;
        } else {
            axes_swapped = 0;
        }
        /* now from the start cell's corner: how many cells it crosses
         * along u, and where it enters the first one across v */
        g_collision_cast->walk_end_u += COLLISION_CELL_SIZE / 2;
        g_collision_cast->walk_start_u += COLLISION_CELL_SIZE / 2;
        g_collision_cast->walk_v += COLLISION_CELL_SIZE / 2;
        g_collision_cast->walk_steps_left =
            g_collision_cast->walk_end_u / COLLISION_CELL_SIZE -
            g_collision_cast->walk_start_u / COLLISION_CELL_SIZE + 1;
        /* of ONE */
        g_collision_cast->walk_slope = (g_collision_cast->walk_delta_v << 12) /
                                       g_collision_cast->walk_delta_u;
        g_collision_cast->walk_v -=
            (g_collision_cast->walk_start_u * g_collision_cast->walk_slope) >>
            12;
        g_collision_cast->walk_minor_step =
            (g_collision_cast->walk_slope * COLLISION_CELL_SIZE) >> 12;
        while (--g_collision_cast->walk_steps_left != -1) {
            if ((g_collision_cast->walk_tmp = PTR(g_collision_cast->test_cell)(
                     g_collision_cast->walk_cell.x,
                     g_collision_cast->walk_cell.z)) != 0) {
                break;
            }
            g_collision_cast->walk_v %= COLLISION_CELL_SIZE;
            g_collision_cast->walk_v += g_collision_cast->walk_minor_step;
            if (g_collision_cast->walk_steps_left == 0) {
                break;
            }
            if (g_collision_cast->walk_v > COLLISION_CELL_SIZE) {
                /* it leaves this cell sideways: the next cell across */
                if (axes_swapped) {
                    if (x_direction < 0) {
                        g_collision_cast->walk_cell.x--;
                    } else {
                        g_collision_cast->walk_cell.x++;
                    }
                } else {
                    if (z_direction < 0) {
                        g_collision_cast->walk_cell.z--;
                    } else {
                        g_collision_cast->walk_cell.z++;
                    }
                }
                if ((g_collision_cast->walk_tmp =
                         PTR(g_collision_cast->test_cell)(
                             g_collision_cast->walk_cell.x,
                             g_collision_cast->walk_cell.z)) != 0) {
                    break;
                }
            }
            /* the next cell along */
            if (axes_swapped) {
                if (z_direction < 0) {
                    g_collision_cast->walk_cell.z--;
                } else {
                    g_collision_cast->walk_cell.z++;
                }
            } else {
                if (x_direction < 0) {
                    g_collision_cast->walk_cell.x--;
                } else {
                    g_collision_cast->walk_cell.x++;
                }
            }
        }
        /* rounding can leave the walk a cell short of the end */
        if (g_collision_cast->walk_tmp == 0 &&
            (g_collision_cast->walk_cell.x != g_collision_cast->end_cell.x ||
             g_collision_cast->walk_cell.z != g_collision_cast->end_cell.z)) {
            PTR(g_collision_cast->test_cell)
            (g_collision_cast->end_cell.x, g_collision_cast->end_cell.z);
        }
    }
    return collision_ReadHit(hit_point, hit_normal);
}

/* ---- casts ---- */

/* collision_CastMoveWith with this file's own work area. Nothing calls it. */
static void collision_CastMove(
    s32 *from, s32 *to, s32 *hit_point, s16 *hit_normal) {
    g_collision_cast = &g_collision_own_cast;
    g_collision_own_cast.seg_start = *(VECTOR *)from;
    g_collision_cast->seg_end = *(VECTOR *)to;
    if (math_LengthSquared(
            g_collision_cast->seg_end.vx - g_collision_cast->seg_start.vx,
            g_collision_cast->seg_end.vy - g_collision_cast->seg_start.vy,
            g_collision_cast->seg_end.vz - g_collision_cast->seg_start.vz) <
        SHORT_MOVE_SQ) {
        /* start two lengths further back */
        SET_PTR(g_collision_cast->test_cell, collision_TestCellFine);
        g_collision_cast->seg_start.vx -=
            (g_collision_cast->seg_end.vx - g_collision_cast->seg_start.vx)
            << 1;
        g_collision_cast->seg_start.vy -=
            (g_collision_cast->seg_end.vy - g_collision_cast->seg_start.vy)
            << 1;
        g_collision_cast->seg_start.vz -=
            (g_collision_cast->seg_end.vz - g_collision_cast->seg_start.vz)
            << 1;
    } else {
        SET_PTR(g_collision_cast->test_cell, collision_TestCell);
    }
    collision_WalkCells(hit_point, hit_normal);
}

/* Casts the segment something moves along this frame, in the caller's work
 * area. A move shorter than 200 units is cast from two of its lengths
 * further back and with the fine test, so that a slow mover resting against
 * a face is still found in front of it. */
s32 collision_CastMoveWith(s32 *from, s32 *to, s32 *hit_point, s16 *hit_normal,
                           SegmentCast *work_addr) {
    g_collision_cast = work_addr;
    g_collision_cast->seg_start = *(VECTOR *)from;
    g_collision_cast->seg_end = *(VECTOR *)to;
    if (math_LengthSquared(
            g_collision_cast->seg_end.vx - g_collision_cast->seg_start.vx,
            g_collision_cast->seg_end.vy - g_collision_cast->seg_start.vy,
            g_collision_cast->seg_end.vz - g_collision_cast->seg_start.vz) <
        SHORT_MOVE_SQ) {
        /* start two lengths further back */
        SET_PTR(g_collision_cast->test_cell, collision_TestCellFine);
        g_collision_cast->seg_start.vx -=
            (g_collision_cast->seg_end.vx - g_collision_cast->seg_start.vx)
            << 1;
        g_collision_cast->seg_start.vy -=
            (g_collision_cast->seg_end.vy - g_collision_cast->seg_start.vy)
            << 1;
        g_collision_cast->seg_start.vz -=
            (g_collision_cast->seg_end.vz - g_collision_cast->seg_start.vz)
            << 1;
    } else {
        SET_PTR(g_collision_cast->test_cell, collision_TestCell);
    }
    return collision_WalkCells(hit_point, hit_normal);
}

s32 collision_Cast(s32 *from, s32 *to, s32 *hit_point, s16 *hit_normal) {
    g_collision_cast = &g_collision_own_cast;
    g_collision_own_cast.seg_start = *(VECTOR *)from;
    g_collision_cast->seg_end = *(VECTOR *)to;
    SET_PTR(g_collision_cast->test_cell, collision_TestCell);
    return collision_WalkCells(hit_point, hit_normal);
}

s32 collision_CastWith(s32 *from, s32 *to, s32 *hit_point, s16 *hit_normal,
                       SegmentCast *work_addr) {
    g_collision_cast = work_addr;
    g_collision_cast->seg_start = *(VECTOR *)from;
    g_collision_cast->seg_end = *(VECTOR *)to;
    SET_PTR(g_collision_cast->test_cell, collision_TestCell);
    return collision_WalkCells(hit_point, hit_normal);
}

/* Hands back the nearest hit, in world coordinates, and notes its surface
 * for collision_GetSurface. Returns 0 if there was none. */
static s32 collision_ReadHit(s32 *hit_point, s16 *hit_normal) {
    SegmentCast *cast = g_collision_cast;
    s32 corner;

    if (cast->nearest_dist_sq != NO_HIT) {
        corner = cast->hit_cell_x * COLLISION_CELL_SIZE - COLLISION_GRID_HALF;
        hit_point[0] = cast->hit_local_x + corner;
        hit_point[1] = cast->hit_local_y;
        corner = cast->hit_cell_z * COLLISION_CELL_SIZE - COLLISION_GRID_HALF;
        hit_point[2] = cast->hit_local_z + corner;
        hit_normal[0] = cast->hit_nx >> PLANE_NORMAL_SHIFT;
        hit_normal[1] = cast->hit_ny >> PLANE_NORMAL_SHIFT;
        hit_normal[2] = cast->hit_nz >> PLANE_NORMAL_SHIFT;
        g_collision_surface = cast->hit_surface;
        return 1;
    }
    return 0;
}

/* ---- one cell ---- */

/* Tests the segment against the faces of one cell and keeps the hit nearest
 * the start. Returns nonzero if the cast has hit anything so far. This is
 * the fine test, for short moves: the distances from the plane keep four
 * more bits, and the crossing point is put on the face's axes by hand. */
static s32 collision_TestCellFine(s32 cell_x, s32 cell_z) {
    s32 points;
    s16 *data;
    s32 faces;
    s32 corner_x;
    s32 corner_z;
    s32 first_v;

    if (cell_x < 0 || cell_z < 0 || cell_x >= COLLISION_GRID_CELLS ||
        cell_z >= COLLISION_GRID_CELLS) {
        return 0;
    }
    g_collision_cast->work =
        ((u16 *)ADDR(g_collision_grid))[cell_z * COLLISION_GRID_CELLS + cell_x];
    if (g_collision_cast->work == NO_FACES) {
        return 0;
    }
    data = (s16 *)ADDR(g_collision_grid + g_collision_cast->work);
    corner_x = cell_x * COLLISION_CELL_SIZE - COLLISION_GRID_HALF;
    corner_z = cell_z * COLLISION_CELL_SIZE - COLLISION_GRID_HALF;
    g_collision_cast->local_start_x = g_collision_cast->seg_start.vx - corner_x;
    g_collision_cast->local_start_y = g_collision_cast->seg_start.vy;
    g_collision_cast->local_start_z = g_collision_cast->seg_start.vz - corner_z;
    g_collision_cast->local_end_x = g_collision_cast->seg_end.vx - corner_x;
    g_collision_cast->local_end_y = g_collision_cast->seg_end.vy;
    g_collision_cast->local_end_z = g_collision_cast->seg_end.vz - corner_z;

    faces = *data++;
    while (--faces != -1) {
        /* the face's plane, and how far in front of it each end is */
        g_collision_cast->face_nx = *data++;
        g_collision_cast->face_ny = *data++;
        g_collision_cast->face_nz = *data++;
        g_collision_cast->face_plane_d = (u16)*data++;
        g_collision_cast->face_plane_d =
            (*data++ << 16) | g_collision_cast->face_plane_d;
        g_collision_cast->start_dist =
            g_collision_cast->face_nx * g_collision_cast->local_start_x +
            g_collision_cast->face_ny * g_collision_cast->local_start_y +
            g_collision_cast->face_nz * g_collision_cast->local_start_z +
            g_collision_cast->face_plane_d;
        g_collision_cast->end_dist =
            g_collision_cast->face_nx * g_collision_cast->local_end_x +
            g_collision_cast->face_ny * g_collision_cast->local_end_y +
            g_collision_cast->face_nz * g_collision_cast->local_end_z +
            g_collision_cast->face_plane_d;

        g_collision_cast->start_dist =
            SHIFT_MAGNITUDE(g_collision_cast->start_dist, FINE_SHIFT);
        g_collision_cast->end_dist =
            SHIFT_MAGNITUDE(g_collision_cast->end_dist, FINE_SHIFT);

        if (g_collision_cast->start_dist >= 0 &&
            g_collision_cast->end_dist < 0) {
            /* it goes in through the front: where it crosses the plane */
            g_collision_cast->work =
                g_collision_cast->start_dist - g_collision_cast->end_dist;
            g_collision_cast->start_dist *= 2;
            g_collision_cast->plane_cross_x =
                (g_collision_cast->local_end_x -
                 g_collision_cast->local_start_x) *
                g_collision_cast->start_dist / g_collision_cast->work;
            g_collision_cast->plane_cross_y =
                (g_collision_cast->local_end_y -
                 g_collision_cast->local_start_y) *
                g_collision_cast->start_dist / g_collision_cast->work;
            g_collision_cast->plane_cross_z =
                (g_collision_cast->local_end_z -
                 g_collision_cast->local_start_z) *
                g_collision_cast->start_dist / g_collision_cast->work;
            g_collision_cast->plane_cross_x =
                HALF_ROUNDED(g_collision_cast->plane_cross_x);
            g_collision_cast->plane_cross_y =
                HALF_ROUNDED(g_collision_cast->plane_cross_y);
            g_collision_cast->plane_cross_z =
                HALF_ROUNDED(g_collision_cast->plane_cross_z);
            g_collision_cast->plane_cross_x += g_collision_cast->local_start_x;
            g_collision_cast->plane_cross_y += g_collision_cast->local_start_y;
            g_collision_cast->plane_cross_z += g_collision_cast->local_start_z;
            /* the face's origin, in the three spare words, then the
             * crossing point along each of its axes */
            g_collision_cast->work = *data++;
            g_collision_cast->start_dist = *data++;
            g_collision_cast->end_dist = *data++;
            g_collision_cast->proj_axis_x = *data++;
            g_collision_cast->proj_axis_y = *data++;
            g_collision_cast->proj_axis_z = *data++;
            g_collision_cast->proj_cross_u =
                ((g_collision_cast->plane_cross_x - g_collision_cast->work) *
                     g_collision_cast->proj_axis_x +
                 (g_collision_cast->plane_cross_y -
                  g_collision_cast->start_dist) *
                     g_collision_cast->proj_axis_y +
                 (g_collision_cast->plane_cross_z -
                  g_collision_cast->end_dist) *
                     g_collision_cast->proj_axis_z) >>
                PLANE_SHIFT;
            g_collision_cast->proj_axis_x = *data++;
            g_collision_cast->proj_axis_y = *data++;
            g_collision_cast->proj_axis_z = *data++;
            g_collision_cast->proj_cross_v =
                ((g_collision_cast->plane_cross_x - g_collision_cast->work) *
                     g_collision_cast->proj_axis_x +
                 (g_collision_cast->plane_cross_y -
                  g_collision_cast->start_dist) *
                     g_collision_cast->proj_axis_y +
                 (g_collision_cast->plane_cross_z -
                  g_collision_cast->end_dist) *
                     g_collision_cast->proj_axis_z) >>
                PLANE_SHIFT;
            points = *data++;
            g_collision_cast->face_surface = points >> FACE_SURFACE_SHIFT;
            points &= FACE_EDGES_MASK;
            g_collision_cast->edge_from_u = *data++;
            first_v = *data++;
            /* inside the outline if it is on the inner side of every
             * edge */
            g_collision_cast->work = 1;
            g_collision_cast->edge_from_v = first_v;
            while (--points != -1) {
                g_collision_cast->edge_to_u = *data++;
                g_collision_cast->edge_to_v = *data++;
                if ((g_collision_cast->proj_cross_u -
                     g_collision_cast->edge_from_u) *
                            (g_collision_cast->edge_to_v -
                             g_collision_cast->edge_from_v) -
                        (g_collision_cast->proj_cross_v -
                         g_collision_cast->edge_from_v) *
                            (g_collision_cast->edge_to_u -
                             g_collision_cast->edge_from_u) >
                    0) {
                    g_collision_cast->work = 0;
                    break;
                }
                g_collision_cast->edge_from_u = g_collision_cast->edge_to_u;
                g_collision_cast->edge_from_v = g_collision_cast->edge_to_v;
            }
            /* skip the points not looked at */
            if (points > 0) {
                data += points * 2;
            }
            /* a hit: keep it if it is nearer the start than the last */
            if (g_collision_cast->work) {
                if ((g_collision_cast->work = math_LengthSquared(
                         g_collision_cast->plane_cross_x -
                             g_collision_cast->local_start_x,
                         g_collision_cast->plane_cross_y -
                             g_collision_cast->local_start_y,
                         g_collision_cast->plane_cross_z -
                             g_collision_cast->local_start_z)) <
                    g_collision_cast->nearest_dist_sq) {
                    g_collision_cast->hit_cell_x = cell_x;
                    g_collision_cast->hit_cell_z = cell_z;
                    g_collision_cast->hit_local_x =
                        g_collision_cast->plane_cross_x;
                    g_collision_cast->hit_local_y =
                        g_collision_cast->plane_cross_y;
                    g_collision_cast->hit_local_z =
                        g_collision_cast->plane_cross_z;
                    g_collision_cast->hit_nx = g_collision_cast->face_nx;
                    g_collision_cast->hit_ny = g_collision_cast->face_ny;
                    g_collision_cast->hit_nz = g_collision_cast->face_nz;
                    g_collision_cast->hit_plane_d =
                        g_collision_cast->face_plane_d;
                    g_collision_cast->nearest_dist_sq = g_collision_cast->work;
                    g_collision_cast->hit_surface =
                        g_collision_cast->face_surface;
                }
            }
        } else {
            /* not crossed: on to the next face */
            data += FACE_FRAME_SIZE;
            points = *data++;
            points &= FACE_EDGES_MASK;
            data += (points + 1) * 2;
        }
    }
    return g_collision_cast->nearest_dist_sq != NO_HIT;
}

/* The usual test: the same, with the crossing point put on the face's axes
 * by the GTE. */
static s32 collision_TestCell(s32 cell_x, s32 cell_z) {
    s32 points;
    s16 *data;
    s32 faces;
    s32 corner_x;
    s32 corner_z;
    s32 first_v;

    if (cell_x < 0 || cell_z < 0 || cell_x >= COLLISION_GRID_CELLS ||
        cell_z >= COLLISION_GRID_CELLS) {
        return 0;
    }
    g_collision_cast->work =
        ((u16 *)ADDR(g_collision_grid))[cell_z * COLLISION_GRID_CELLS + cell_x];
    if (g_collision_cast->work == NO_FACES) {
        return 0;
    }
    data = (s16 *)ADDR(g_collision_grid + g_collision_cast->work);
    corner_x = cell_x * COLLISION_CELL_SIZE - COLLISION_GRID_HALF;
    corner_z = cell_z * COLLISION_CELL_SIZE - COLLISION_GRID_HALF;
    g_collision_cast->local_start_x = g_collision_cast->seg_start.vx - corner_x;
    g_collision_cast->local_start_y = g_collision_cast->seg_start.vy;
    g_collision_cast->local_start_z = g_collision_cast->seg_start.vz - corner_z;
    g_collision_cast->local_end_x = g_collision_cast->seg_end.vx - corner_x;
    g_collision_cast->local_end_y = g_collision_cast->seg_end.vy;
    g_collision_cast->local_end_z = g_collision_cast->seg_end.vz - corner_z;

    faces = *data++;
    while (--faces != -1) {
        /* the face's plane, and how far in front of it each end is */
        g_collision_cast->face_nx = *data++;
        g_collision_cast->face_ny = *data++;
        g_collision_cast->face_nz = *data++;
        g_collision_cast->face_plane_d = (u16)*data++;
        g_collision_cast->face_plane_d =
            (*data++ << 16) | g_collision_cast->face_plane_d;
        g_collision_cast->start_dist =
            g_collision_cast->face_nx * g_collision_cast->local_start_x +
            g_collision_cast->face_ny * g_collision_cast->local_start_y +
            g_collision_cast->face_nz * g_collision_cast->local_start_z +
            g_collision_cast->face_plane_d;
        g_collision_cast->end_dist =
            g_collision_cast->face_nx * g_collision_cast->local_end_x +
            g_collision_cast->face_ny * g_collision_cast->local_end_y +
            g_collision_cast->face_nz * g_collision_cast->local_end_z +
            g_collision_cast->face_plane_d;

        g_collision_cast->start_dist =
            SHIFT_MAGNITUDE(g_collision_cast->start_dist, PLANE_SHIFT);
        g_collision_cast->end_dist =
            SHIFT_MAGNITUDE(g_collision_cast->end_dist, PLANE_SHIFT);

        if (g_collision_cast->start_dist >= 0 &&
            g_collision_cast->end_dist < 0) {
            /* it goes in through the front: where it crosses the plane */
            g_collision_cast->work =
                g_collision_cast->start_dist - g_collision_cast->end_dist;
            g_collision_cast->plane_cross_x =
                (g_collision_cast->local_end_x -
                 g_collision_cast->local_start_x) *
                    g_collision_cast->start_dist / g_collision_cast->work +
                g_collision_cast->local_start_x;
            g_collision_cast->plane_cross_y =
                (g_collision_cast->local_end_y -
                 g_collision_cast->local_start_y) *
                    g_collision_cast->start_dist / g_collision_cast->work +
                g_collision_cast->local_start_y;
            g_collision_cast->plane_cross_z =
                (g_collision_cast->local_end_z -
                 g_collision_cast->local_start_z) *
                    g_collision_cast->start_dist / g_collision_cast->work +
                g_collision_cast->local_start_z;
            collision_ProjectOnFace((s32)data, g_collision_cast->plane_cross_x,
                                    g_collision_cast->plane_cross_y,
                                    g_collision_cast->plane_cross_z);
            data += FACE_FRAME_SIZE;
            points = *data++;
            g_collision_cast->face_surface = points >> FACE_SURFACE_SHIFT;
            points &= FACE_EDGES_MASK;
            g_collision_cast->edge_from_u = *data++;
            first_v = *data++;
            /* inside the outline if it is on the inner side of every
             * edge */
            g_collision_cast->work = 1;
            g_collision_cast->edge_from_v = first_v;
            collision_ReadProjection(&g_collision_cast->proj_cross_u,
                                     &g_collision_cast->proj_cross_v);
            while (--points != -1) {
                g_collision_cast->edge_to_u = *data++;
                g_collision_cast->edge_to_v = *data++;
                if ((g_collision_cast->proj_cross_u -
                     g_collision_cast->edge_from_u) *
                            (g_collision_cast->edge_to_v -
                             g_collision_cast->edge_from_v) -
                        (g_collision_cast->proj_cross_v -
                         g_collision_cast->edge_from_v) *
                            (g_collision_cast->edge_to_u -
                             g_collision_cast->edge_from_u) >
                    0) {
                    g_collision_cast->work = 0;
                    break;
                }
                g_collision_cast->edge_from_u = g_collision_cast->edge_to_u;
                g_collision_cast->edge_from_v = g_collision_cast->edge_to_v;
            }
            /* skip the points not looked at */
            if (points > 0) {
                data += points * 2;
            }
            /* a hit: keep it if it is nearer the start than the last */
            if (g_collision_cast->work) {
                if ((g_collision_cast->work = math_LengthSquared(
                         g_collision_cast->plane_cross_x -
                             g_collision_cast->local_start_x,
                         g_collision_cast->plane_cross_y -
                             g_collision_cast->local_start_y,
                         g_collision_cast->plane_cross_z -
                             g_collision_cast->local_start_z)) <
                    g_collision_cast->nearest_dist_sq) {
                    g_collision_cast->hit_cell_x = cell_x;
                    g_collision_cast->hit_cell_z = cell_z;
                    g_collision_cast->hit_local_x =
                        g_collision_cast->plane_cross_x;
                    g_collision_cast->hit_local_y =
                        g_collision_cast->plane_cross_y;
                    g_collision_cast->hit_local_z =
                        g_collision_cast->plane_cross_z;
                    g_collision_cast->hit_nx = g_collision_cast->face_nx;
                    g_collision_cast->hit_ny = g_collision_cast->face_ny;
                    g_collision_cast->hit_nz = g_collision_cast->face_nz;
                    g_collision_cast->hit_plane_d =
                        g_collision_cast->face_plane_d;
                    g_collision_cast->nearest_dist_sq = g_collision_cast->work;
                    g_collision_cast->hit_surface =
                        g_collision_cast->face_surface;
                }
            }
        } else {
            /* not crossed: on to the next face */
            data += FACE_FRAME_SIZE;
            points = *data++;
            points &= FACE_EDGES_MASK;
            data += (points + 1) * 2;
        }
    }
    return g_collision_cast->nearest_dist_sq != NO_HIT;
}

/* ---- the grid ---- */

void collision_SetGrid(s32 grid_addr) { g_collision_grid = grid_addr; }

void collision_RebaseGrid(s32 delta) { g_collision_grid += delta; }

s32 collision_GetSurface(void) { return g_collision_surface; }
