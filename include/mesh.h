#ifndef MESH_H
#define MESH_H

/* What a mesh is made of. A mesh is one entry of a loaded file's table of
 * pointers (render.h): a stream of halfwords, in one of three layouts.
 *  - prebuilt: finished GPU primitives, two copies of each (one for each
 *    frame's buffer), with the vertices to project in front;
 *  - compact: vertices, then short records the renderer builds primitives
 *    from;
 *  - skinned (a fighter's body): the normals, then thirteen sections, each
 *    with the vertices one bone moves and the records that use them.
 * After the vertices come groups of faces: a count, a primitive type, and
 * that many records; a count of 0 ends them. The type is an index into the
 * renderer's handler table in use (render.c). */

#include "common.h"

/* The three layouts of a mesh. */
enum MeshLayout {
    MESH_PREBUILT = 0,
    MESH_COMPACT = 1,
    MESH_SKINNED = 2,
};

/* A skinned mesh starts with a halfword with this bit set. */
#define MESH_SKINNED_FLAG 0x8000
#define MESH_SKINNED_SECTIONS 13

/* ---- the faces of a compact mesh ----
 * Of a group's type, two bits say what its records look like: whether the
 * faces have four corners or three, and whether they carry a normal for
 * each corner or one for the face. */
#define MESH_FACE_KIND(flags) (((flags) >> 3) & 3)
#define MESH_FACE_QUAD 1 /* else a triangle */
#define MESH_FACE_LONG 2 /* a normal for each corner, not one for the face */

/* A face of each kind: a triangle and a quad with the face's normal, then
 * each with a normal a corner. vertices: the numbers of its corners'
 * vertices, a byte each, read as two halfwords because the short quad has
 * them off a word boundary. */
typedef struct {
    u16 prim[6];
    u16 vertices[2];
    u16 normal[4];
} MeshTri;

typedef struct {
    u16 prim[7];
    u16 vertices[2];
    u16 normal[3];
} MeshQuad;

typedef struct {
    u16 prim[6];
    u16 vertices[2];
    u16 normals[10];
} MeshTriLong;

typedef struct {
    u16 prim[8];
    u16 vertices[2];
    u16 normals[12];
} MeshQuadLong;

#endif /* MESH_H */
