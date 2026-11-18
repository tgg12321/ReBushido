#ifndef PORT_GL_CLOTH_H
#define PORT_GL_CLOTH_H

/* A fighter's cloth as a smooth surface (docs/PORT.md, "The fighters' own
 * models"): the game moves a cloth's few points itself every frame
 * (cloth.c) and draws its faces flat between them (render_DrawCloth).
 * Drawn from a model's file, the same points are the control points of a
 * Catmull-Clark surface, subdivided a number of times and made to pass
 * through every point (a fit worked out once, as tools/fighter_hd.py fits
 * the body), so that its hem and its folds go where the game's points
 * are. Each picture it is made again from the points of that picture.
 *
 * Every vertex of the surface is a fixed sum of the points, worked out
 * once when the cloth is made: the subdivision and the fit are linear in
 * the points. The texture points are the faces' own, carried linearly
 * within each face of the game's, so the texture is laid as the game lays
 * it. The surface is made from those sums on the card
 * (gl_fighter_card.h); nothing here draws or talks to GL, and nothing is
 * the game's. */

#include "common.h"

/* How many times a cloth is subdivided: a triangle of the game's becomes
 * 48 quads, a quad 64. */
#define PORT_GL_CLOTH_LEVELS 3

typedef struct PortGlCloth PortGlCloth;

/* A vertex to draw: where it is, its normal (not of length one; the
 * surface's own, pointing out of the side the faces' corners go round
 * anticlockwise from, seen in the game's axes), and its texture point. */
typedef struct {
    float place[3];
    float normal[3];
    float uv[2];
} PortGlClothVertex;

/* A cloth of `point_count` points and `face_count` faces of three or four
 * corners (`corners[f]`, round the face), their points `face[f][c]` and
 * texture points `uv[f][c]`. The faces are turned to agree with each
 * other first. NULL where the faces name a point that is not there or the
 * memory is not to be had. */
extern PortGlCloth *port_GlClothMake(
    s32 point_count, s32 face_count, const s32 *corners, const s32 (*face)[4],
    const float (*uv)[4][2]);
extern void port_GlClothFree(PortGlCloth *cloth);

/* How many points it is made from, how many vertices it draws, and the
 * triangles' corners, three to a triangle, into those vertices (in the
 * order whose normal the vertices have, PortGlClothVertex). */
extern s32 port_GlClothPoints(const PortGlCloth *cloth);
extern s32 port_GlClothVertices(const PortGlCloth *cloth);
extern s32 port_GlClothTriangles(const PortGlCloth *cloth);
extern const u32 *port_GlClothIndices(const PortGlCloth *cloth);

/* What the surface is made of, for the card to make it
 * (gl_fighter_card.h): each place a sum of the points (`weight`,
 * place_count rows of point_count), the last level's quads by place,
 * whose turns make the normals, and each vertex's place and texture
 * point. Kept by the cloth. */
typedef struct {
    s32 point_count;
    s32 place_count;
    const float *weight;
    s32 quad_count;
    const s32 (*quad)[4];
    s32 vertex_count;
    const s32 *vertex_place;
    const float (*vertex_uv)[2];
} PortGlClothSums;

extern void port_GlClothSums(const PortGlCloth *cloth, PortGlClothSums *out);

/* A square root: the program is linked with no maths library. */
extern double port_GlClothRoot(double x);

/* The cloth is kept out of the model's body, and its normals made, on
 * the card (gl_fighter_card.h): the game moved a cloth's points round
 * its own low body, and the model drawn in its place is fuller. Nothing
 * of it goes back to the game. */

#endif /* PORT_GL_CLOTH_H */
