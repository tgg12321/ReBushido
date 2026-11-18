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
 * it. Nothing here draws or talks to GL, and nothing is the game's. */

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

/* What a place of the surface is held by before its normal is made (the
 * body it must stay out of, below), or NULL. */
typedef void (*PortGlClothHold)(void *context, double *place);

/* The surface through `points` (three numbers each, any units) into `out`,
 * port_GlClothVertices of them; each of its places given to `hold` first
 * where there is one. */
extern void port_GlClothPose(
    const PortGlCloth *cloth, const double *points, PortGlClothHold hold,
    void *context, PortGlClothVertex *out);

/* ---- the body a cloth is kept out of ---- */

/* The game moved a cloth's points round its own low body; the model drawn
 * in its place is fuller and smoother, and would come through the cloth
 * the game moved. So the cloth is kept outside the model's own surface as
 * the picture poses it: the surface is its vertices and their normals,
 * and a point's distance from it is the vertices' about the point, each
 * one's distance along its own normal, weighed by how near it is (a
 * smooth sum, so that a point moves smoothly from picture to picture). A
 * point nearer than `gap` to the outside, or inside, is put out to `gap`
 * along their normal. Nothing of it goes back to the game. */
typedef struct PortGlClothBody PortGlClothBody;

extern PortGlClothBody *port_GlClothBodyMake(void);
extern void port_GlClothBodyFree(PortGlClothBody *body);
/* The body's vertices as posed now: `count` places and normals (a normal
 * of nought is no part of the surface). FALSE where there is no room. */
extern s32 port_GlClothBodySet(PortGlClothBody *body, const float *places,
                               const float *normals, s32 count);
/* `point` (three numbers) put out of the body to `gap`. Whether it was
 * inside the body's surface. */
extern s32 port_GlClothBodyHold(
    const PortGlClothBody *body, double *point, double gap);

#endif /* PORT_GL_CLOTH_H */
