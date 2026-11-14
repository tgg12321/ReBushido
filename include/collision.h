#ifndef COLLISION_H
#define COLLISION_H

/* Collision with the stage (collision.c). The stage file carries, besides
 * what is drawn, a grid of cells that each list the faces crossing them. A
 * "cast" follows a segment from one point to another across the cells and
 * finds the nearest face it goes through: where, the face's normal, and what
 * kind of surface it is. Fighters, loose objects, thrown things and blades
 * all meet the stage this way. */

#include "common.h"
#include <psxsdk/libgte.h>

/* The grid: 32 x 32 cells of 2000 units, centred on the world's origin. */
#define COLLISION_GRID_CELLS 32
#define COLLISION_CELL_SIZE 2000
#define COLLISION_GRID_HALF 32000

/* What a face is made of. The kinds come in pairs, and which of the pair a
 * fighter stands on is its "surface class" (0 for the odd kinds, 1 for the
 * even): the class chooses between a stage's two sets of footstep and
 * landing sounds. The names are inferred from what the fight code does with
 * each kind. */
enum Surface {
    SURFACE_PLAIN = 0, /* no class of its own (inferred: walls) */
    SURFACE_FIRM = 1,  /* nothing to kick up */
    /* the ground beyond an edge: outside a team battle's court (stepping
     * on it is out of bounds), round the floor where the story's victim
     * kneels */
    SURFACE_FIRM_2 = 2,
    SURFACE_SAND = 3, /* sand can be thrown from these four... */
    SURFACE_SAND_2 = 4,
    SURFACE_WET = 5,   /* ...and these two splash under foot (inferred) */
    SURFACE_WET_2 = 6, /* the edge of the stream under the lost bridge */
    /* Stops a fighter and nothing else: blades and thrown things pass. Also
     * what a fighter has before it has touched anything. */
    SURFACE_BARRIER = 7,
};

/* The surface class of a kind that has one (not PLAIN or BARRIER). */
#define SURFACE_CLASS(surface) (((0x2A >> (surface)) ^ 1) & 1)
/* Tests of a surface kind against a set of kinds. */
#define SURFACE_IN(surface, mask) (((mask) >> (surface)) & 1)
#define SURFACES_SAND ((1 << SURFACE_SAND) | (1 << SURFACE_SAND_2))
#define SURFACES_WET ((1 << SURFACE_WET) | (1 << SURFACE_WET_2))
/* Something loose to throw: the dirt-throwing move (MOVE_KIND_DIRT) works on
 * these. */
#define SURFACES_LOOSE (SURFACES_SAND | SURFACES_WET)

/* A cell of the grid. */
typedef struct {
    s16 x;
    s16 z;
} GridCell;

/* The work area of a cast: the segment, the nearest hit so far, the state of
 * the walk across the cells and the face being tested. The caller chooses
 * where it lives; the fight code keeps it on the scratchpad. Points named
 * "local" are measured from the corner of the cell being tested. */
typedef struct {
    s32 nearest_dist_sq; /* +0x00 from the start to the hit; 0x7FFFFFFF if none
                          */
    u16 hit_surface;     /* +0x04 */
    VECTOR seg_start;    /* +0x08 */
    VECTOR seg_end;      /* +0x18 */
    /* the nearest hit: the face's plane and the point, local to hit_cell */
    s32 hit_nx; /* +0x28 */
    s32 hit_ny;
    s32 hit_nz;
    s32 hit_plane_d;
    s32 hit_local_x; /* +0x38 */
    s32 hit_local_y;
    s32 hit_local_z;
    s32 pad44;      /* a point's fourth word; not touched */
    s16 hit_cell_x; /* +0x48 */
    s16 hit_cell_z;
    /* the segment, local to the cell being tested */
    s16 local_start_x; /* +0x4C */
    s16 local_start_y;
    s16 local_start_z;
    s16 pad52;       /* not touched */
    s16 local_end_x; /* +0x54 */
    s16 local_end_y;
    s16 local_end_z;
    s16 pad5A; /* not touched */
    /* +0x5C tests the segment against one cell's faces; nonzero once
     * anything has been hit */
    s32 (*test_cell)(s32 cell_x, s32 cell_z);
    /* the walk: u runs along the axis the segment covers more of, v along
     * the other; both are measured in the direction of travel */
    s32 walk_start_u; /* +0x60 */
    s32 walk_v;       /* +0x64 where the segment is within its cell, across */
    s32 walk_end_u;
    s32 walk_end_v;
    s32 walk_delta_u;    /* +0x70 */
    s32 walk_delta_v;    /* +0x74 */
    s32 walk_minor_step; /* +0x78 how far v moves per cell of u */
    s32 walk_slope;      /* +0x7C dv/du, of ONE */
    s32 walk_tmp;        /* +0x80 the start cell's center x; then a spare */
    s32 start_cell_center_z;
    GridCell walk_cell;  /* +0x88 */
    GridCell end_cell;   /* +0x8C */
    s16 walk_steps_left; /* +0x90 */
    u8 unk92[0xA];       /* no code reads or writes it */
    /* the face being tested */
    s32 proj_axis_x; /* +0x9C one of the face's two axes */
    s32 proj_axis_y;
    s32 proj_axis_z;
    s32 plane_cross_x; /* +0xA8 where the segment crosses the face's plane */
    s32 plane_cross_y;
    s32 plane_cross_z;
    s32 edge_from_u; /* +0xB4 an edge of the face's outline */
    s32 edge_from_v;
    s32 edge_to_u;
    s32 edge_to_v;
    s32 proj_cross_u; /* +0xC4 the crossing point on the face's axes */
    s32 proj_cross_v;
    s32 face_surface; /* +0xCC */
    s32 face_nx;      /* +0xD0 */
    s32 face_ny;
    s32 face_nz;
    s32 face_plane_d;
    /* three spare words: mostly the two ends' distances from the plane and
     * their sum, then the face's origin, whether the point is inside the
     * outline, and the hit's distance */
    s32 work;       /* +0xE0 */
    s32 start_dist; /* +0xE4 */
    s32 end_dist;   /* +0xE8 */
} SegmentCast;

/* The stage's collision grid (part of the stage file), and where it moved
 * to when its arena block was shifted. */
extern void collision_SetGrid(s32 grid_addr);
extern void collision_RebaseGrid(s32 delta);

/* Casts the segment from `from` to `to` (each x, y, z and a spare word).
 * Returns 1 if it goes through a face, with the nearest crossing in
 * hit_point[3] and that face's normal (length ONE) in hit_normal[3].
 * Faces are one-sided: only going in through the front counts. */
extern s32 collision_Cast(s32 *from, s32 *to, s32 *hit_point, s16 *hit_normal);
/* The same with the caller's work area (the address of a SegmentCast). */
extern s32 collision_CastWith(s32 *from, s32 *to, s32 *hit_point,
                              s16 *hit_normal, SegmentCast *work_addr);
/* For something moving from `from` to `to` this frame: a short step is
 * tested with finer arithmetic and from further back, so that a slow mover
 * resting against a face cannot slip through it. */
extern s32 collision_CastMoveWith(s32 *from, s32 *to, s32 *hit_point,
                                  s16 *hit_normal, SegmentCast *work_addr);

/* The kind of surface the last cast that hit anything went through. */
extern s32 collision_GetSurface(void);

/* Two helpers written in assembly (math_asm.c). The first puts
 * (point - origin) on the two axes of the face frame at `frame` (an origin
 * and two axes, in halfwords), in the GTE; the second reads the two results
 * back. */
extern void collision_ProjectOnFace(s32 frame, s32 x, s32 y, s32 z);
extern void collision_ReadProjection(s32 *u, s32 *v);

#endif /* COLLISION_H */
