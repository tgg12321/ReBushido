/* A fighter's cloth as a smooth surface through the game's own points
 * (gl_cloth.h): Catmull-Clark subdivision worked out once as a fixed sum
 * of the points for every vertex, and fitted so that the surface passes
 * through each point. Nothing here draws, and nothing is the game's. */
#include <stdlib.h>
#include <string.h>

#include "common.h"
#include "gl_cloth.h"

/* A mesh of one level of the subdivision: each vertex a sum of the points
 * (a row of `points` numbers), and faces of three or four corners. */
typedef struct {
    s32 vertex_count;
    double *row;
    s32 face_count;
    s32 *corners;
    s32 (*face)[4];
    float (*uv)[4][2];
} ClothLevel;

/* The edges of a level: each the two vertices it joins (the smaller
 * first), and how many faces have it and which (the first two). */
typedef struct {
    s32 count;
    s32 *a;
    s32 *b;
    s32 *faces;
    s32 (*face)[2];
    s32 *next; /* the next edge of the same smaller vertex, or -1 */
    s32 *head; /* a vertex's first edge, or -1 */
} ClothEdges;

struct PortGlCloth {
    s32 point_count;
    /* the surface's places, each a sum of the points */
    s32 place_count;
    float *weight; /* place_count rows of point_count */
    /* the vertices drawn: a place and a texture point each */
    s32 vertex_count;
    s32 *vertex_place;
    float (*vertex_uv)[2];
    /* the last level's quads, by place, for the normals */
    s32 quad_count;
    s32 (*quad)[4];
    s32 triangle_count;
    u32 *index;
    /* room for a pose */
    double (*place)[3];
    double (*normal)[3];
};

static void port_GlClothLevelFree(ClothLevel *level) {
    free(level->row);
    free(level->corners);
    free(level->face);
    free(level->uv);
    memset(level, 0, sizeof(*level));
}

static s32 port_GlClothLevelMake(
    ClothLevel *level, s32 vertices, s32 faces, s32 points) {
    memset(level, 0, sizeof(*level));
    level->vertex_count = vertices;
    level->face_count = faces;
    level->row = calloc((size_t)vertices * (size_t)points, sizeof(double));
    level->corners = calloc((size_t)faces + 1, sizeof(s32));
    level->face = calloc((size_t)faces + 1, sizeof(*level->face));
    level->uv = calloc((size_t)faces + 1, sizeof(*level->uv));
    if (level->row == NULL || level->corners == NULL || level->face == NULL ||
        level->uv == NULL) {
        port_GlClothLevelFree(level);
        return FALSE;
    }
    return TRUE;
}

static void port_GlClothEdgesFree(ClothEdges *edges) {
    free(edges->a);
    free(edges->b);
    free(edges->faces);
    free(edges->face);
    free(edges->next);
    free(edges->head);
    memset(edges, 0, sizeof(*edges));
}

/* The edge from `from` to `to`, made if it is not there yet. */
static s32 port_GlClothEdge(ClothEdges *edges, s32 from, s32 to) {
    s32 a = from < to ? from : to;
    s32 b = from < to ? to : from;
    s32 e;

    for (e = edges->head[a]; e >= 0; e = edges->next[e]) {
        if (edges->b[e] == b) {
            return e;
        }
    }
    e = edges->count++;
    edges->a[e] = a;
    edges->b[e] = b;
    edges->faces[e] = 0;
    edges->next[e] = edges->head[a];
    edges->head[a] = e;
    return e;
}

/* Every edge of the level's faces, and `edge_of[f][c]`, the edge from a
 * face's corner c to the next. */
static s32 port_GlClothEdgesOf(
    const ClothLevel *level, ClothEdges *edges, s32 (*edge_of)[4]) {
    s32 room = 4 * level->face_count + 1;
    s32 f;
    s32 c;

    memset(edges, 0, sizeof(*edges));
    edges->a = malloc((size_t)room * sizeof(s32));
    edges->b = malloc((size_t)room * sizeof(s32));
    edges->faces = malloc((size_t)room * sizeof(s32));
    edges->face = malloc((size_t)room * sizeof(*edges->face));
    edges->next = malloc((size_t)room * sizeof(s32));
    edges->head = malloc(((size_t)level->vertex_count + 1) * sizeof(s32));
    if (edges->a == NULL || edges->b == NULL || edges->faces == NULL ||
        edges->face == NULL || edges->next == NULL || edges->head == NULL) {
        port_GlClothEdgesFree(edges);
        return FALSE;
    }
    for (c = 0; c < level->vertex_count; c++) {
        edges->head[c] = -1;
    }
    for (f = 0; f < level->face_count; f++) {
        s32 n = level->corners[f];

        for (c = 0; c < n; c++) {
            s32 e = port_GlClothEdge(
                edges, level->face[f][c], level->face[f][(c + 1) % n]);

            if (edges->faces[e] < 2) {
                edges->face[e][edges->faces[e]] = f;
            }
            edges->faces[e]++;
            edge_of[f][c] = e;
        }
    }
    return TRUE;
}

/* Whether face f goes from `from` to `to` (and not the other way). */
static s32 port_GlClothGoes(const ClothLevel *level, s32 f, s32 from, s32 to) {
    s32 n = level->corners[f];
    s32 c;

    for (c = 0; c < n; c++) {
        if (level->face[f][c] == from && level->face[f][(c + 1) % n] == to) {
            return TRUE;
        }
    }
    return FALSE;
}

/* A face turned the other way round, its first corner kept. */
static void port_GlClothTurn(ClothLevel *level, s32 f) {
    s32 n = level->corners[f];
    s32 c;

    for (c = 1; c < n - c; c++) {
        s32 point = level->face[f][c];
        float u = level->uv[f][c][0];
        float v = level->uv[f][c][1];

        level->face[f][c] = level->face[f][n - c];
        level->uv[f][c][0] = level->uv[f][n - c][0];
        level->uv[f][c][1] = level->uv[f][n - c][1];
        level->face[f][n - c] = point;
        level->uv[f][n - c][0] = u;
        level->uv[f][n - c][1] = v;
    }
}

/* The faces turned to agree with each other across every edge two of them
 * share, piece by piece: the game draws a cloth from both sides and does
 * not mind which way a face goes round, but a normal made of the faces
 * does. */
static void port_GlClothAgree(ClothLevel *level) {
    ClothEdges edges;
    s32(*edge_of)[4] = calloc((size_t)level->face_count + 1, sizeof(*edge_of));
    s32 *queue = calloc((size_t)level->face_count + 1, sizeof(s32));
    u8 *seen = calloc((size_t)level->face_count + 1, 1);
    s32 start;

    if (edge_of == NULL || queue == NULL || seen == NULL ||
        !port_GlClothEdgesOf(level, &edges, edge_of)) {
        free(edge_of);
        free(queue);
        free(seen);
        return;
    }
    for (start = 0; start < level->face_count; start++) {
        s32 got = 0;
        s32 put = 0;

        if (seen[start]) {
            continue;
        }
        seen[start] = TRUE;
        queue[put++] = start;
        while (got < put) {
            s32 f = queue[got++];
            s32 n = level->corners[f];
            s32 c;

            for (c = 0; c < n; c++) {
                s32 p = level->face[f][c];
                s32 q = level->face[f][(c + 1) % n];
                s32 e = port_GlClothEdge(&edges, p, q);
                s32 k;

                for (k = 0; k < edges.faces[e] && k < 2; k++) {
                    s32 g = edges.face[e][k];

                    if (g == f || seen[g]) {
                        continue;
                    }
                    if (port_GlClothGoes(level, g, p, q)) {
                        port_GlClothTurn(level, g);
                    }
                    seen[g] = TRUE;
                    queue[put++] = g;
                }
            }
        }
    }
    port_GlClothEdgesFree(&edges);
    free(edge_of);
    free(queue);
    free(seen);
}

/* row `to` = row `to` + w times row `from` */
static void port_GlClothAdd(
    double *to, const double *from, double w, s32 points) {
    s32 i;

    for (i = 0; i < points; i++) {
        to[i] += w * from[i];
    }
}

/* What a round of the subdivision counts of the level it starts from. */
typedef struct {
    ClothEdges edges;
    s32 (*edge_of)[4]; /* each face's corner's edge to the next corner */
    s32 *valence;      /* each vertex's edges */
    s32 *open;         /* ... and how many of them are open */
    s32 *open_end;     /* the other ends of the first two open ones */
    s32 *face_count;   /* each vertex's faces */
} ClothCount;

static void port_GlClothCountFree(ClothCount *count) {
    port_GlClothEdgesFree(&count->edges);
    free(count->edge_of);
    free(count->valence);
    free(count->open);
    free(count->open_end);
    free(count->face_count);
}

static s32 port_GlClothCountOf(const ClothLevel *in, ClothCount *count) {
    size_t vertices = (size_t)in->vertex_count + 1;
    s32 f;
    s32 e;

    memset(count, 0, sizeof(*count));
    count->edge_of =
        calloc((size_t)in->face_count + 1, sizeof(*count->edge_of));
    count->valence = calloc(vertices, sizeof(s32));
    count->open = calloc(vertices, sizeof(s32));
    count->open_end = calloc(vertices * 2, sizeof(s32));
    count->face_count = calloc(vertices, sizeof(s32));
    if (count->edge_of == NULL || count->valence == NULL ||
        count->open == NULL || count->open_end == NULL ||
        count->face_count == NULL ||
        !port_GlClothEdgesOf(in, &count->edges, count->edge_of)) {
        port_GlClothCountFree(count);
        return FALSE;
    }
    for (f = 0; f < in->face_count; f++) {
        s32 c;

        for (c = 0; c < in->corners[f]; c++) {
            count->face_count[in->face[f][c]]++;
        }
    }
    for (e = 0; e < count->edges.count; e++) {
        s32 ends[2] = {count->edges.a[e], count->edges.b[e]};
        s32 k;

        for (k = 0; k < 2; k++) {
            count->valence[ends[k]]++;
            if (count->edges.faces[e] != 2) {
                if (count->open[ends[k]] < 2) {
                    count->open_end[ends[k] * 2 + count->open[ends[k]]] =
                        ends[1 - k];
                }
                count->open[ends[k]]++;
            }
        }
    }
    return TRUE;
}

/* A vertex's new place, from the level before (`in`): (Q + 2R + (n - 3) V)
 * / n inside the surface, Q being the middle of its faces' points and R of
 * its edges' middles; along a crease (6 V + A + B) / 8; at a corner it
 * stays. */
static void port_GlClothVertexRule(
    const ClothLevel *in, const ClothCount *count, const double *face_rows,
    s32 vertex, double *row, s32 points) {
    const ClothEdges *edges = &count->edges;
    const double *old = in->row + (size_t)vertex * points;
    s32 n = count->valence[vertex];
    s32 faces = count->face_count[vertex];
    s32 f;
    s32 e;

    if (count->open[vertex] == 0 && n >= 3 && faces > 0) {
        for (e = 0; e < edges->count; e++) {
            s32 other;

            if (edges->a[e] == vertex) {
                other = edges->b[e];
            } else if (edges->b[e] == vertex) {
                other = edges->a[e];
            } else {
                continue;
            }
            port_GlClothAdd(row, old, 1.0 / ((double)n * n), points);
            port_GlClothAdd(row, in->row + (size_t)other * points,
                            1.0 / ((double)n * n), points);
        }
        for (f = 0; f < in->face_count; f++) {
            s32 c;

            for (c = 0; c < in->corners[f]; c++) {
                if (in->face[f][c] == vertex) {
                    port_GlClothAdd(row, face_rows + (size_t)f * points,
                                    1.0 / ((double)faces * n), points);
                }
            }
        }
        port_GlClothAdd(row, old, (double)(n - 3) / n, points);
    } else if (count->open[vertex] == 2 && faces > 1) {
        port_GlClothAdd(row, old, 0.75, points);
        port_GlClothAdd(
            row, in->row + (size_t)count->open_end[vertex * 2] * points, 0.125,
            points);
        port_GlClothAdd(
            row, in->row + (size_t)count->open_end[vertex * 2 + 1] * points,
            0.125, points);
    } else {
        port_GlClothAdd(row, old, 1.0, points);
    }
}

/* A face's quads, one for each corner: the corner, the edge after it, the
 * face's point, the edge before it; their texture points the corner's own,
 * the edges' middles and the face's middle. The next quad's number. */
static s32 port_GlClothSplitFace(
    const ClothLevel *in, const ClothCount *count, s32 f, s32 edge_first,
    s32 face_first, ClothLevel *out, s32 quad) {
    s32 n = in->corners[f];
    float middle[2] = {0.0f, 0.0f};
    float half[4][2];
    s32 c;

    for (c = 0; c < n; c++) {
        middle[0] += in->uv[f][c][0] / (float)n;
        middle[1] += in->uv[f][c][1] / (float)n;
        half[c][0] = (in->uv[f][c][0] + in->uv[f][(c + 1) % n][0]) * 0.5f;
        half[c][1] = (in->uv[f][c][1] + in->uv[f][(c + 1) % n][1]) * 0.5f;
    }
    for (c = 0; c < n; c++, quad++) {
        s32 before = (c + n - 1) % n;

        out->corners[quad] = 4;
        out->face[quad][0] = in->face[f][c];
        out->face[quad][1] = edge_first + count->edge_of[f][c];
        out->face[quad][2] = face_first + f;
        out->face[quad][3] = edge_first + count->edge_of[f][before];
        out->uv[quad][0][0] = in->uv[f][c][0];
        out->uv[quad][0][1] = in->uv[f][c][1];
        out->uv[quad][1][0] = half[c][0];
        out->uv[quad][1][1] = half[c][1];
        out->uv[quad][2][0] = middle[0];
        out->uv[quad][2][1] = middle[1];
        out->uv[quad][3][0] = half[before][0];
        out->uv[quad][3][1] = half[before][1];
    }
    return quad;
}

/* One round of Catmull-Clark: the vertices first (their new places), the
 * edges' points after them, then the faces'; each face of n corners
 * becomes n quads. An open edge, and an edge of more than two faces, is a
 * crease: its point is its middle, and a vertex on two such edges moves
 * along them only; a vertex at the corner of one face alone, or of more
 * open edges than two, stays where it is. The texture points are carried
 * linearly within each face. */
static s32 port_GlClothRefine(
    const ClothLevel *in, ClothLevel *out, s32 points) {
    ClothCount count;
    const ClothEdges *edges = &count.edges;
    double *face_rows;
    s32 quads = 0;
    s32 edge_first;
    s32 face_first;
    s32 f;
    s32 e;
    s32 v;

    memset(out, 0, sizeof(*out));
    if (!port_GlClothCountOf(in, &count)) {
        return FALSE;
    }
    for (f = 0; f < in->face_count; f++) {
        quads += in->corners[f];
    }
    edge_first = in->vertex_count;
    face_first = edge_first + edges->count;
    if (!port_GlClothLevelMake(
            out, face_first + in->face_count, quads, points)) {
        port_GlClothCountFree(&count);
        return FALSE;
    }
    /* the faces' points: the middle of their corners */
    face_rows = out->row + (size_t)face_first * points;
    for (f = 0; f < in->face_count; f++) {
        s32 c;

        for (c = 0; c < in->corners[f]; c++) {
            port_GlClothAdd(face_rows + (size_t)f * points,
                            in->row + (size_t)in->face[f][c] * points,
                            1.0 / in->corners[f], points);
        }
    }
    /* the edges' points: inside, the middle of its ends and its faces'
     * points; on a crease, the middle of its ends */
    for (e = 0; e < edges->count; e++) {
        double *row = out->row + (size_t)(edge_first + e) * points;
        s32 is_inside = edges->faces[e] == 2;
        double share = is_inside ? 0.25 : 0.5;

        port_GlClothAdd(
            row, in->row + (size_t)edges->a[e] * points, share, points);
        port_GlClothAdd(
            row, in->row + (size_t)edges->b[e] * points, share, points);
        if (is_inside) {
            port_GlClothAdd(row, face_rows + (size_t)edges->face[e][0] * points,
                            share, points);
            port_GlClothAdd(row, face_rows + (size_t)edges->face[e][1] * points,
                            share, points);
        }
    }
    for (v = 0; v < in->vertex_count; v++) {
        port_GlClothVertexRule(
            in, &count, face_rows, v, out->row + (size_t)v * points, points);
    }
    quads = 0;
    for (f = 0; f < in->face_count; f++) {
        quads = port_GlClothSplitFace(
            in, &count, f, edge_first, face_first, out, quads);
    }
    port_GlClothCountFree(&count);
    return TRUE;
}

/* The inverse of an n by n matrix, by Gauss and Jordan with the largest
 * pivot. FALSE where it has none. */
static s32 port_GlClothInvert(double *a, double *inverse, s32 n) {
    s32 i;
    s32 j;
    s32 k;

    for (i = 0; i < n; i++) {
        for (j = 0; j < n; j++) {
            inverse[i * n + j] = i == j ? 1.0 : 0.0;
        }
    }
    for (k = 0; k < n; k++) {
        s32 best = k;
        double pivot;

        for (i = k + 1; i < n; i++) {
            double x = a[i * n + k] < 0 ? -a[i * n + k] : a[i * n + k];
            double y = a[best * n + k] < 0 ? -a[best * n + k] : a[best * n + k];

            if (x > y) {
                best = i;
            }
        }
        pivot = a[best * n + k];
        if (pivot < 1e-9 && pivot > -1e-9) {
            return FALSE;
        }
        if (best != k) {
            for (j = 0; j < n; j++) {
                double t = a[k * n + j];

                a[k * n + j] = a[best * n + j];
                a[best * n + j] = t;
                t = inverse[k * n + j];
                inverse[k * n + j] = inverse[best * n + j];
                inverse[best * n + j] = t;
            }
        }
        for (j = 0; j < n; j++) {
            a[k * n + j] /= pivot;
            inverse[k * n + j] /= pivot;
        }
        for (i = 0; i < n; i++) {
            double m = a[i * n + k];

            if (i == k || m == 0.0) {
                continue;
            }
            for (j = 0; j < n; j++) {
                a[i * n + j] -= m * a[k * n + j];
                inverse[i * n + j] -= m * inverse[k * n + j];
            }
        }
    }
    return TRUE;
}

/* The drawn vertices of the last level: one for each place and texture
 * point the faces' corners have, and two triangles of each quad. */
static s32 port_GlClothVerticesOf(PortGlCloth *cloth, const ClothLevel *last) {
    s32 corners = last->face_count * 4;
    s32 *first = malloc(((size_t)last->vertex_count + 1) * sizeof(s32));
    s32 *next = malloc(((size_t)corners + 1) * sizeof(s32));
    s32 f;
    s32 c;

    cloth->vertex_place = malloc(((size_t)corners + 1) * sizeof(s32));
    cloth->vertex_uv =
        malloc(((size_t)corners + 1) * sizeof(*cloth->vertex_uv));
    cloth->quad = malloc(((size_t)last->face_count + 1) * sizeof(*cloth->quad));
    cloth->index = malloc(((size_t)last->face_count * 6 + 1) * sizeof(u32));
    if (first == NULL || next == NULL || cloth->vertex_place == NULL ||
        cloth->vertex_uv == NULL || cloth->quad == NULL ||
        cloth->index == NULL) {
        free(first);
        free(next);
        return FALSE;
    }
    for (c = 0; c < last->vertex_count; c++) {
        first[c] = -1;
    }
    cloth->vertex_count = 0;
    cloth->quad_count = last->face_count;
    for (f = 0; f < last->face_count; f++) {
        u32 at[4];

        for (c = 0; c < 4; c++) {
            s32 place = last->face[f][c];
            s32 r;

            cloth->quad[f][c] = place;
            for (r = first[place]; r >= 0; r = next[r]) {
                if (cloth->vertex_uv[r][0] == last->uv[f][c][0] &&
                    cloth->vertex_uv[r][1] == last->uv[f][c][1]) {
                    break;
                }
            }
            if (r < 0) {
                r = cloth->vertex_count++;
                cloth->vertex_place[r] = place;
                cloth->vertex_uv[r][0] = last->uv[f][c][0];
                cloth->vertex_uv[r][1] = last->uv[f][c][1];
                next[r] = first[place];
                first[place] = r;
            }
            at[c] = (u32)r;
        }
        cloth->index[f * 6 + 0] = at[0];
        cloth->index[f * 6 + 1] = at[1];
        cloth->index[f * 6 + 2] = at[2];
        cloth->index[f * 6 + 3] = at[0];
        cloth->index[f * 6 + 4] = at[2];
        cloth->index[f * 6 + 5] = at[3];
    }
    cloth->triangle_count = last->face_count * 2;
    free(first);
    free(next);
    return TRUE;
}

/* The fit: a vertex of the game's stays the first of the places at every
 * level, so the surface's place there is row i of the last level. Taken
 * through the inverse of those rows, the surface passes through every
 * point. Without an inverse it is left as it is, inside the points. */
static s32 port_GlClothFit(PortGlCloth *cloth, const ClothLevel *last) {
    s32 n = cloth->point_count;
    double *square = malloc((size_t)n * n * sizeof(double));
    double *inverse = malloc((size_t)n * n * sizeof(double));
    s32 i;

    if (square == NULL || inverse == NULL) {
        free(square);
        free(inverse);
        return FALSE;
    }
    memcpy(square, last->row, (size_t)n * n * sizeof(double));
    if (!port_GlClothInvert(square, inverse, n)) {
        for (i = 0; i < n * n; i++) {
            inverse[i] = i / n == i % n ? 1.0 : 0.0;
        }
    }
    for (i = 0; i < last->vertex_count; i++) {
        const double *row = last->row + (size_t)i * n;
        s32 j;

        for (j = 0; j < n; j++) {
            double sum = 0.0;
            s32 k;

            for (k = 0; k < n; k++) {
                sum += row[k] * inverse[k * n + j];
            }
            cloth->weight[(size_t)i * n + j] = (float)sum;
        }
    }
    free(square);
    free(inverse);
    return TRUE;
}

/* The game's faces as the first level: each point its own row. */
static s32 port_GlClothFirstLevel(
    ClothLevel *level, s32 point_count, s32 face_count, const s32 *corners,
    const s32 (*face)[4], const float (*uv)[4][2]) {
    s32 f;
    s32 c;
    s32 i;

    if (!port_GlClothLevelMake(level, point_count, face_count, point_count)) {
        return FALSE;
    }
    for (i = 0; i < point_count; i++) {
        level->row[(size_t)i * point_count + i] = 1.0;
    }
    for (f = 0; f < face_count; f++) {
        if (corners[f] != 3 && corners[f] != 4) {
            port_GlClothLevelFree(level);
            return FALSE;
        }
        level->corners[f] = corners[f];
        for (c = 0; c < corners[f]; c++) {
            if (face[f][c] < 0 || face[f][c] >= point_count) {
                port_GlClothLevelFree(level);
                return FALSE;
            }
            level->face[f][c] = face[f][c];
            level->uv[f][c][0] = uv[f][c][0];
            level->uv[f][c][1] = uv[f][c][1];
        }
    }
    return TRUE;
}

PortGlCloth *port_GlClothMake(
    s32 point_count, s32 face_count, const s32 *corners, const s32 (*face)[4],
    const float (*uv)[4][2]) {
    ClothLevel level;
    PortGlCloth *cloth;
    s32 i;

    if (point_count <= 0 || face_count <= 0 ||
        !port_GlClothFirstLevel(
            &level, point_count, face_count, corners, face, uv)) {
        return NULL;
    }
    port_GlClothAgree(&level);
    for (i = 0; i < PORT_GL_CLOTH_LEVELS; i++) {
        ClothLevel finer;

        if (!port_GlClothRefine(&level, &finer, point_count)) {
            port_GlClothLevelFree(&level);
            return NULL;
        }
        port_GlClothLevelFree(&level);
        level = finer;
    }
    cloth = calloc(1, sizeof(*cloth));
    if (cloth != NULL) {
        size_t places = (size_t)level.vertex_count;

        cloth->point_count = point_count;
        cloth->place_count = level.vertex_count;
        cloth->weight = malloc(places * (size_t)point_count * sizeof(float));
        cloth->place = malloc(places * sizeof(*cloth->place));
        cloth->normal = malloc(places * sizeof(*cloth->normal));
        if (cloth->weight == NULL || cloth->place == NULL ||
            cloth->normal == NULL || !port_GlClothVerticesOf(cloth, &level) ||
            !port_GlClothFit(cloth, &level)) {
            port_GlClothFree(cloth);
            cloth = NULL;
        }
    }
    port_GlClothLevelFree(&level);
    return cloth;
}

void port_GlClothFree(PortGlCloth *cloth) {
    if (cloth == NULL) {
        return;
    }
    free(cloth->weight);
    free(cloth->vertex_place);
    free(cloth->vertex_uv);
    free(cloth->quad);
    free(cloth->index);
    free(cloth->place);
    free(cloth->normal);
    free(cloth);
}

s32 port_GlClothPoints(const PortGlCloth *cloth) { return cloth->point_count; }

s32 port_GlClothVertices(const PortGlCloth *cloth) {
    return cloth->vertex_count;
}

s32 port_GlClothTriangles(const PortGlCloth *cloth) {
    return cloth->triangle_count;
}

const u32 *port_GlClothIndices(const PortGlCloth *cloth) {
    return cloth->index;
}

/* A square root, by Newton's steps from a power of two near it: the
 * program is linked with no maths library. */
static double port_GlClothRoot(double x) {
    double root = 1.0;
    s32 i;

    if (x <= 0.0) {
        return 0.0;
    }
    while (root * root * 4.0 < x) {
        root *= 2.0;
    }
    while (root * root > x * 4.0) {
        root *= 0.5;
    }
    for (i = 0; i < 6; i++) {
        root = 0.5 * (root + x / root);
    }
    return root;
}

void port_GlClothPose(
    const PortGlCloth *cloth, const double *points, PortGlClothHold hold,
    void *context, PortGlClothVertex *out) {
    s32 n = cloth->point_count;
    s32 i;
    s32 k;

    for (i = 0; i < cloth->place_count; i++) {
        const float *weight = cloth->weight + (size_t)i * n;
        double *place = cloth->place[i];
        s32 j;

        for (k = 0; k < 3; k++) {
            place[k] = 0.0;
            cloth->normal[i][k] = 0.0;
        }
        for (j = 0; j < n; j++) {
            if (weight[j] != 0.0f) {
                for (k = 0; k < 3; k++) {
                    place[k] += weight[j] * points[j * 3 + k];
                }
            }
        }
        if (hold != NULL) {
            hold(context, place);
        }
    }
    /* each quad's turn across its diagonals, on its four corners */
    for (i = 0; i < cloth->quad_count; i++) {
        const s32 *quad = cloth->quad[i];
        double x[3];
        double y[3];
        double turn[3];
        s32 corner;

        for (k = 0; k < 3; k++) {
            x[k] = cloth->place[quad[2]][k] - cloth->place[quad[0]][k];
            y[k] = cloth->place[quad[3]][k] - cloth->place[quad[1]][k];
        }
        turn[0] = x[1] * y[2] - x[2] * y[1];
        turn[1] = x[2] * y[0] - x[0] * y[2];
        turn[2] = x[0] * y[1] - x[1] * y[0];
        for (corner = 0; corner < 4; corner++) {
            for (k = 0; k < 3; k++) {
                cloth->normal[quad[corner]][k] += turn[k];
            }
        }
    }
    for (i = 0; i < cloth->place_count; i++) {
        double *normal = cloth->normal[i];
        double length =
            port_GlClothRoot(normal[0] * normal[0] + normal[1] * normal[1] +
                             normal[2] * normal[2]);

        for (k = 0; k < 3 && length > 0.0; k++) {
            normal[k] /= length;
        }
    }
    for (i = 0; i < cloth->vertex_count; i++) {
        s32 place = cloth->vertex_place[i];

        for (k = 0; k < 3; k++) {
            out[i].place[k] = (float)cloth->place[place][k];
            out[i].normal[k] = (float)cloth->normal[place][k];
        }
        out[i].uv[0] = cloth->vertex_uv[i][0];
        out[i].uv[1] = cloth->vertex_uv[i][1];
    }
}

/* ---- the body a cloth is kept out of ---- */

/* The body's vertices are found by a grid of cubes this many of the
 * world's units across, hashed into so many lists: the models' vertices
 * are some 26 units apart. */
#define BODY_CELL 64.0
#define BODY_BUCKETS 4096
/* The least reach of the sum that says how far a point is from the
 * surface, and how much farther than its nearest vertex it reaches. */
#define BODY_REACH 48.0
#define BODY_REACH_OVER 1.5
/* How many cubes about a point its nearest vertex is looked for in, and
 * the farthest that vertex may be for the point to be near the body. */
#define BODY_LOOK 2
#define BODY_FAR 100.0
/* How many times a point is put out and looked at again, at most. */
#define BODY_STEPS 4

struct PortGlClothBody {
    s32 count;
    s32 room;
    float *place;
    float *normal;
    s32 *order; /* the vertices, list after list */
    s32 first[BODY_BUCKETS + 1];
};

PortGlClothBody *port_GlClothBodyMake(void) {
    return calloc(1, sizeof(PortGlClothBody));
}

void port_GlClothBodyFree(PortGlClothBody *body) {
    if (body == NULL) {
        return;
    }
    free(body->place);
    free(body->normal);
    free(body->order);
    free(body);
}

/* The cube a number of the world is in, along one axis. */
static s32 port_GlClothCell(double x) {
    double cells = x / BODY_CELL;
    s32 whole = (s32)cells;

    return (double)whole > cells ? whole - 1 : whole;
}

static u32 port_GlClothBucket(s32 x, s32 y, s32 z) {
    return ((u32)x * 73856093U ^ (u32)y * 19349663U ^ (u32)z * 83492791U) &
           (BODY_BUCKETS - 1);
}

s32 port_GlClothBodySet(PortGlClothBody *body, const float *places,
                        const float *normals, s32 count) {
    s32 *bucket_of;
    s32 at[BODY_BUCKETS];
    s32 i;

    if (body->room < count) {
        float *place = realloc(body->place, (size_t)count * 3 * sizeof(float));
        float *normal =
            place == NULL
                ? NULL
                : realloc(body->normal, (size_t)count * 3 * sizeof(float));
        s32 *order = normal == NULL
                         ? NULL
                         : realloc(body->order, (size_t)count * sizeof(s32));

        if (place != NULL) {
            body->place = place;
        }
        if (normal != NULL) {
            body->normal = normal;
        }
        if (order == NULL) {
            body->count = 0;
            return FALSE;
        }
        body->order = order;
        body->room = count;
    }
    bucket_of = malloc(((size_t)count + 1) * sizeof(s32));
    if (bucket_of == NULL) {
        body->count = 0;
        return FALSE;
    }
    memcpy(body->place, places, (size_t)count * 3 * sizeof(float));
    memcpy(body->normal, normals, (size_t)count * 3 * sizeof(float));
    body->count = count;
    memset(body->first, 0, sizeof(body->first));
    for (i = 0; i < count; i++) {
        const float *place = places + i * 3;

        bucket_of[i] = (s32)port_GlClothBucket(
            port_GlClothCell(place[0]), port_GlClothCell(place[1]),
            port_GlClothCell(place[2]));
        body->first[bucket_of[i] + 1]++;
    }
    for (i = 0; i < BODY_BUCKETS; i++) {
        body->first[i + 1] += body->first[i];
        at[i] = body->first[i];
    }
    for (i = 0; i < count; i++) {
        body->order[at[bucket_of[i]]++] = i;
    }
    free(bucket_of);
    return TRUE;
}

/* What the vertices within `reach` of a point say (the cubes `look`
 * about its own): the nearest one's distance squared, and, where `sums`
 * is given, the sum of their distances along their normals and of their
 * normals, each weighed by (1 - d^2 / reach^2)^2, and of the weights. */
typedef struct {
    double nearest;
    double along;
    double normal[3];
    double weight;
} BodySums;

static void port_GlClothBodyNear(
    const PortGlClothBody *body, const double *point, s32 look, double reach,
    BodySums *sums) {
    s32 cx = port_GlClothCell(point[0]);
    s32 cy = port_GlClothCell(point[1]);
    s32 cz = port_GlClothCell(point[2]);
    double reach2 = reach * reach;
    s32 x;
    s32 y;
    s32 z;

    memset(sums, 0, sizeof(*sums));
    sums->nearest = -1.0;
    for (x = cx - look; x <= cx + look; x++) {
        for (y = cy - look; y <= cy + look; y++) {
            for (z = cz - look; z <= cz + look; z++) {
                u32 bucket = port_GlClothBucket(x, y, z);
                s32 i;

                for (i = body->first[bucket]; i < body->first[bucket + 1];
                     i++) {
                    s32 v = body->order[i];
                    const float *place = body->place + v * 3;
                    const float *m = body->normal + v * 3;
                    double d[3] = {point[0] - place[0], point[1] - place[1],
                                   point[2] - place[2]};
                    double d2 = d[0] * d[0] + d[1] * d[1] + d[2] * d[2];
                    double w;

                    if (m[0] == 0.0f && m[1] == 0.0f && m[2] == 0.0f) {
                        continue;
                    }
                    if (sums->nearest < 0.0 || d2 < sums->nearest) {
                        sums->nearest = d2;
                    }
                    if (d2 >= reach2) {
                        continue;
                    }
                    w = 1.0 - d2 / reach2;
                    w *= w;
                    sums->along +=
                        w * (d[0] * m[0] + d[1] * m[1] + d[2] * m[2]);
                    sums->normal[0] += w * m[0];
                    sums->normal[1] += w * m[1];
                    sums->normal[2] += w * m[2];
                    sums->weight += w;
                }
            }
        }
    }
}

/* How far a point is outside the body's surface (below nought inside),
 * and which way out is (`out`, of length one). FALSE where the body says
 * nothing of it: no vertex near enough, or their normals cancel (a thin
 * piece seen from both its sides). */
static s32 port_GlClothBodyDistance(
    const PortGlClothBody *body, const double *point, double *distance,
    double *out) {
    BodySums sums;
    double reach;
    double length;
    s32 k;

    /* the nearest vertex: none within the cubes looked in, or none
     * within BODY_FAR, is far outside, and nothing to do with the body
     * (no part of a fighter is so thick that a point inside it is that
     * far from all its vertices) */
    port_GlClothBodyNear(body, point, 1, 0.0, &sums);
    if (sums.nearest < 0.0) {
        port_GlClothBodyNear(body, point, BODY_LOOK, 0.0, &sums);
    }
    if (sums.nearest < 0.0 || sums.nearest > BODY_FAR * BODY_FAR) {
        return FALSE;
    }
    /* the sum reaches past the nearest vertex, and grows with it, so
     * that it changes smoothly as the point moves */
    reach = BODY_REACH_OVER * port_GlClothRoot(sums.nearest);
    if (reach < BODY_REACH) {
        reach = BODY_REACH;
    }
    port_GlClothBodyNear(
        body, point, (s32)(reach / BODY_CELL) + 1, reach, &sums);
    length = port_GlClothRoot(
        sums.normal[0] * sums.normal[0] + sums.normal[1] * sums.normal[1] +
        sums.normal[2] * sums.normal[2]);
    if (sums.weight <= 0.0 || length < 0.25 * sums.weight) {
        return FALSE;
    }
    *distance = sums.along / sums.weight;
    for (k = 0; k < 3; k++) {
        out[k] = sums.normal[k] / length;
    }
    return TRUE;
}

s32 port_GlClothBodyHold(
    const PortGlClothBody *body, double *point, double gap) {
    double distance;
    double out[3];
    s32 was_inside;
    s32 step;
    s32 k;

    if (body == NULL || body->count == 0 ||
        !port_GlClothBodyDistance(body, point, &distance, out)) {
        return FALSE;
    }
    was_inside = distance < 0.0;
    /* put out along the way out, and looked at again from there: the
     * surface about the new place may say it is not out yet */
    for (step = 0; step < BODY_STEPS && distance < gap; step++) {
        for (k = 0; k < 3; k++) {
            point[k] += (gap - distance) * out[k];
        }
        if (!port_GlClothBodyDistance(body, point, &distance, out)) {
            break;
        }
    }
    return was_inside;
}
