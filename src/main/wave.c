/* The surf (wave.h). The water of the Kimigaura coast is a sheet of 8 x 16
 * textured quads lying on the beach. Its points are a grid of 9 rows of 17;
 * only how far up the beach each point is changes. The first row is driven
 * by a sine, each column a little out of step with the next; every other row
 * follows the row before it, keeping its distance loosely, so the motion
 * runs through the sheet and the last row is the edge of the water.
 * Compiled with small data. */
#include "common.h"
#include "bb2.h"

/* The surf's two textures, 64 texels square, one above the other on the
 * texture page whose corner is (704, 256); the last rows of quads, the
 * water's edge, have the lower one. One CLUT for both. */
#define WAVE_TEXTURE_X 704
#define WAVE_TEXTURE_Y 384
#define WAVE_EDGE_TEXTURE_Y 448
#define WAVE_CLUT_X 16
#define WAVE_CLUT_Y 480
#define WAVE_TEXEL_MAX 63
#define WAVE_V 128      /* the upper texture's first row within the page */
#define WAVE_EDGE_V 192 /* the lower one's */
/* A corner's texture coordinates as the mesh has them. */
#define WAVE_UV(u, v) ((s16)((u) | (v) << 8))
/* The first row of quads that has the edge's texture. */
#define WAVE_EDGE_FIRST_ROW 5
/* The primitive code of a textured quad (a POLY_FT4's). */
#define WAVE_QUAD_CODE 0x2C

/* The rows of projected points on the scratchpad, where the renderer reads
 * a mesh's points: a word of screen coordinates each and, further on, a
 * halfword of depth. The even rows of points are projected to the first
 * place and the odd rows to the one after it, a row being 18 points. */
#define WAVE_ROW_POINTS (WAVE_COLUMNS + 2)
#define WAVE_SCREEN_POINTS ((s32 *)(SCRATCHPAD + 0x20))
#define WAVE_POINT_DEPTHS ((s16 *)(SCRATCHPAD + 0x110))

/* Where the sheet lies. Its first column of points is this far back along
 * z and the columns are as far apart as the rows are at rest; its first
 * row at rest is this far back along x. The node it is drawn through is
 * placed with the same two distances, the other way round. */
#define WAVE_BACK_Z 12000
#define WAVE_BACK_X 4000
#define WAVE_COLUMN_SPACING 2000

/* The first row goes this far either way, and its phase advances this much
 * a frame: a swell takes some 228 frames. */
#define WAVE_SWELL 2500
#define WAVE_PHASE_STEP 18

/* The two samplers that nothing calls have another layout: columns this
 * far apart, the first this far back, and the edge this much further
 * down the beach. Outside the sheet the edge is far away. */
#define WAVE_SAMPLE_SPACING 3200
#define WAVE_SAMPLE_BACK_Z 32000
#define WAVE_SAMPLE_BACK_X 16200
#define WAVE_SAMPLE_NOWHERE (-100000)

/* The node the surf is queued for drawing as. */
static PlainNode g_wave_draw_node;
/* A row of points as the renderer wants them: three at a time, so a row of
 * seventeen is read as eighteen. There is room for eight rows, one for
 * each row of quads; only the first two are used, by the even rows of
 * points and the odd. */
static SVECTOR g_wave_row_vertices[WAVE_ROWS][WAVE_ROW_POINTS];
/* The phases of the first row's points, and how far up the beach each
 * point is. */
static s32 g_wave_phases[WAVE_COLUMNS + 1];
static s32 g_wave_rows[WAVE_ROWS + 1][WAVE_COLUMNS + 1];
/* How far each point of the last row moved this frame. The samplers read
 * one entry past the last row of g_wave_rows, which is this. */
static s32 g_wave_edge_steps[WAVE_ROW_POINTS];

static s16 *g_wave_quads; /* the surf's quads, in the stage's arena block */
/* What wave_GetEdgeAt last worked out, for wave_GetEdgeSpeed. */
static s32 g_wave_sample_column;
static s32 g_wave_sample_weight;

void wave_SetBuffer(s32 buffer_addr) { g_wave_quads = (s16 *)buffer_addr; }

/* Builds the quads of the sheet in the buffer, in the form the renderer
 * draws meshes from: twelve halfwords a quad.
 *  0     texture page
 *  1     flags, and the primitive code (a textured quad) in the high byte
 *  2     CLUT
 *  3-6   the texture coordinates of the corners
 *  7-8   the points of the corners, a byte each: indices into the two rows
 *        of projected points on the scratchpad (0.. the even row, 0x12..
 *        the odd row). The first two corners are on the row of the quad.
 *  9-11  the normal, z first: straight up
 * The first five rows of quads use one texture and the last three another.
 * Also sets up the node the sheet is drawn through and lays the points out
 * at rest. Returns the bytes written. */
s32 wave_Init(void) {
    s16 *quad;
    u16 edge_tpage;
    u16 tpage;
    u16 edge_clut;
    u16 clut;
    s32 row;
    s32 column;
    s32 odd_next;
    s32 even_this;
    s32 odd_this;
    s32 even_next;
    s32 flags_and_code;
    s32 halfword;
    s32 *phase;
    s32 *row_points;
    s32 distance;
    s32 row_distance;

    quad = g_wave_quads;
    edge_tpage = GetTPage(0, 0, WAVE_TEXTURE_X, WAVE_EDGE_TEXTURE_Y);
    tpage = GetTPage(0, 0, WAVE_TEXTURE_X, WAVE_TEXTURE_Y);
    edge_clut = GetClut(WAVE_CLUT_X, WAVE_CLUT_Y);
    clut = GetClut(WAVE_CLUT_X, WAVE_CLUT_Y);
    row = 0;
    flags_and_code = WAVE_QUAD_CODE << 8;
    column = 0;
    do {
        /* a corner's point: this column's in the high byte, the next
         * column's in the low one */
        odd_this = WAVE_ROW_POINTS << 8;
        odd_next = WAVE_ROW_POINTS + 1;
        even_this = 0;
        even_next = 1;
        do {
            if (row >= WAVE_EDGE_FIRST_ROW) {
                *quad = edge_tpage;
                quad += 1;
                *quad = flags_and_code;
                quad += 1;
                *quad = edge_clut;
                quad += 1;
                halfword =
                    WAVE_UV(WAVE_TEXEL_MAX, WAVE_EDGE_V + WAVE_TEXEL_MAX);
                *quad = halfword;
                quad += 1;
                halfword = WAVE_UV(0, WAVE_EDGE_V + WAVE_TEXEL_MAX);
                *quad = halfword;
                quad += 1;
                halfword = WAVE_UV(WAVE_TEXEL_MAX, WAVE_EDGE_V);
                *quad = halfword;
                quad += 1;
                halfword = WAVE_UV(0, WAVE_EDGE_V);
            } else {
                *quad = tpage;
                quad += 1;
                *quad = flags_and_code;
                quad += 1;
                *quad = clut;
                quad += 1;
                halfword = WAVE_UV(WAVE_TEXEL_MAX, WAVE_V + WAVE_TEXEL_MAX);
                *quad = halfword;
                quad += 1;
                halfword = WAVE_UV(0, WAVE_V + WAVE_TEXEL_MAX);
                *quad = halfword;
                quad += 1;
                halfword = WAVE_UV(WAVE_TEXEL_MAX, WAVE_V);
                *quad = halfword;
                quad += 1;
                halfword = WAVE_UV(0, WAVE_V);
            }
            *quad = halfword;
            quad += 1;
            if (row & 1) {
                halfword = odd_next | odd_this;
                *quad = halfword;
                quad += 1;
                halfword = even_next | even_this;
            } else {
                halfword = even_next | even_this;
                *quad = halfword;
                quad += 1;
                halfword = odd_next | odd_this;
            }
            *quad = halfword;
            quad += 1;
            *quad = 0;
            quad += 1;
            *quad = 0;
            quad += 1;
            halfword = -ONE;
            *quad = halfword;
            quad += 1;
            odd_this += 1 << 8;
            odd_next += 1;
            even_this += 1 << 8;
            column += 1;
            even_next += 1;
        } while (column < WAVE_COLUMNS);
        row += 1;
        column = 0;
    } while (row < WAVE_ROWS);

    {
        SceneNode *node = &g_wave_draw_node.node;
        node->kind = NODE_KIND_WAVE;
        node->light_setup_index = LIGHT_SCENERY;
        node->local.t[0] = -WAVE_BACK_Z;
        node->draw_flags = 0;
        node->local.t[1] = 0;
        node->local.t[2] = -WAVE_BACK_X;
        node->rot.vx = 0;
        node->rot.vy = 0;
        node->rot.vz = 0;
        node->rotation_order = ROTATION_ORDER_ZYX;
        node->parent = NULL;
        node->matrix_state = NODE_MATRIX_REBUILD;
        node_UpdateWorldMatrix(node);
    }

    row = 0;
    distance = 0;
    do {
        row_distance = distance;
        row_points = g_wave_rows[row];
        for (column = WAVE_COLUMNS; column >= 0; column--) {
            row_points[column] = row_distance;
        }
        distance += WAVE_ROW_SPACING;
        row += 1;
    } while (row < WAVE_ROWS + 1);

    /* each column a thirty-second of a turn behind the one before */
    /* FAKE: for (column = 0; column < WAVE_COLUMNS + 1; column++) */
    column = 0;
    phase = &g_wave_phases[0];
loop3:
    *phase = (column << 7) & ANGLE_MASK;
    column++;
    phase++;
    if (WAVE_COLUMNS + 1 > column) {
        goto loop3;
    }

    return (s32)quad - (s32)g_wave_quads;
}

/* Moves the surf a frame: the first row by its sine, every other row after
 * the row before it; then queues the sheet for drawing. */
void wave_Update(void) {
    s32 i;
    s32 j;
    s32 stretch;
    s32 step;
    void **draw_queue_slot;

    for (i = 0; i < WAVE_COLUMNS + 1; i++) {
        /* the sine's ONE is four times 1 << 10 */
        g_wave_rows[0][i] = (g_math_sin_table[g_wave_phases[i] & ANGLE_MASK] *
                             (WAVE_SWELL / 4)) >>
                            10;
        g_wave_phases[i] += WAVE_PHASE_STEP;
    }
    /* each row is drawn after the one before it: by a tenth of what it is
     * short of its distance when it is too close, by a sixteenth of what it
     * is over when it has got too far ahead */
    for (i = 1; i < WAVE_ROWS + 1; i++) {
        for (j = 0; j < WAVE_COLUMNS + 1; j++) {
            stretch =
                WAVE_ROW_SPACING - (g_wave_rows[i][j] - g_wave_rows[i - 1][j]);
            if (stretch < 0) {
                step = (stretch + 15) >> 4; /* stretch / 16, toward zero */
            } else {
                step = stretch / 10; /* a tenth of what it is short */
            }
            g_wave_rows[i][j] += step;
            if (i == WAVE_ROWS) {
                g_wave_edge_steps[j] = step;
            }
        }
    }
    draw_queue_slot = g_render_queue_end;
    g_render_queue_end = draw_queue_slot + 1;
    *draw_queue_slot = &g_wave_draw_node.node;
}

/* Projects the points a row at a time onto the scratchpad, the even and the
 * odd rows each to their own place, and after each row but the first has the
 * renderer draw the row of quads between it and the one before. */
void wave_Draw(void) {
    s32 sxy0, sxy1, sxy2, depth_cue, gte_flags;
    s32 sz0, sz1, sz2;
    s16 *quad;
    s32 i, j;
    s32 *src;
    SVECTOR *base;
    SVECTOR *vertex;
    s32 *screen_out;
    s16 *depth_out;
    s16 z;

    quad = g_wave_quads;
    i = 0;
    src = g_wave_rows[0];
    while (i < WAVE_ROWS + 1) {
        if (i & 1) {
            base = g_wave_row_vertices[1];
            screen_out = WAVE_SCREEN_POINTS + WAVE_ROW_POINTS;
            depth_out = WAVE_POINT_DEPTHS + WAVE_ROW_POINTS;
        } else {
            base = g_wave_row_vertices[0];
            screen_out = WAVE_SCREEN_POINTS;
            depth_out = WAVE_POINT_DEPTHS;
        }
        vertex = base;
        j = 0;
        z = -WAVE_BACK_Z;
        while (j < WAVE_COLUMNS + 1) {
            vertex->vx = src[j] - WAVE_BACK_X;
            vertex->vy = 0;
            vertex->vz = z;
            vertex++;
            z += WAVE_COLUMN_SPACING;
            j++;
        }
        vertex = base;
        for (j = 0; j < WAVE_ROW_POINTS / 3; j++) {
            RotTransPers3(&vertex[0], &vertex[1], &vertex[2], &sxy0, &sxy1,
                          &sxy2, &depth_cue, &gte_flags);
            vertex += 3;
            ReadSZfifo3(&sz0, &sz1, &sz2);
            *screen_out++ = sxy0;
            *screen_out++ = sxy1;
            *screen_out++ = sxy2;
            *depth_out++ = sz0;
            *depth_out++ = sz1;
            *depth_out++ = sz2;
        }
        if (i != 0) {
            quad = render_EmitSplitFT4(WAVE_COLUMNS, (s16 *)base, quad, 0);
        }
        i++;
        src += WAVE_COLUMNS + 1;
    }
}

/* How far up the beach the water's edge is at `z`: the last row's points
 * either side of it, weighed. The weight is the low bit of the remainder,
 * where the share of the column that the remainder covers was surely meant,
 * so this returns the value at the next column or very nearly; and a row
 * has 17 points where 18 columns are let through, so the last two read
 * past it: the original's behaviour, in a function nothing calls. */
static s32 wave_GetEdgeAt(s32 z) {
    s32 column = (z + WAVE_SAMPLE_BACK_Z) / WAVE_SAMPLE_SPACING;
    s32 quotient = z / WAVE_SAMPLE_SPACING;
    s32 remainder = z - quotient * WAVE_SAMPLE_SPACING;
    s32 weight = remainder & 1;

    g_wave_sample_column = column;
    g_wave_sample_weight = weight;
    if (column < 0 || column >= WAVE_ROW_POINTS) {
        return WAVE_SAMPLE_NOWHERE;
    }
    {
        s32 here = g_wave_rows[WAVE_ROWS][column] * weight;
        s32 next = g_wave_rows[WAVE_ROWS][column + 1] * (ONE - weight);

        return ((here + next) >> 12) - WAVE_SAMPLE_BACK_X;
    }
}

/* How fast the edge is moving at the place wave_GetEdgeAt was last asked
 * about: the last row's steps of this frame, weighed the same way. Nothing
 * calls it either. */
static s32 wave_GetEdgeSpeed(void) {
    s32 column = g_wave_sample_column;

    if (column < 0 || column >= WAVE_ROW_POINTS) {
        return 0;
    }
    {
        s32 step = g_wave_edge_steps[column];
        s32 weight = g_wave_sample_weight;
        s32 here = step * weight;
        s32 next_step = g_wave_edge_steps[column + 1];
        s32 next = next_step * (ONE - weight);

        return (here + next) >> 12;
    }
}

s32 wave_GetBufferSize(void) { return WAVE_BUFFER_SIZE; }

void wave_RebaseBuffer(s32 delta) {
    g_wave_quads = (s16 *)((u8 *)g_wave_quads + delta);
}
