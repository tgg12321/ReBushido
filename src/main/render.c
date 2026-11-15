/* The renderer's C side: turning the queue of nodes into primitives once a
 * frame, the tables of routines that draw each kind of primitive, the
 * tables of mesh pointers (reloc_), and moving a mesh's textures (tex_Shift).
 * The drawing itself is the assembly in render_asm.c, which this file calls
 * and is called by. */
#include "common.h"
#include "bb2.h"

/* Defined further down. */
static void render_FlushTo(u32 *ot);
static s16 tex_ShiftTPage(s16 tpage, s16 du, s16 dv);
static s16 tex_ShiftClut(s16 clut, s16 dcx, s32 dcy);

/* Defined further down. */
static void (*g_render_skinned_handlers[42])();
static void (*g_render_outline_handlers[42])();
static s16 g_render_prim_sizes[44];
static void tex_ShiftMeshAtCursor(s16 du, s16 dv, s16 dcx, s16 dcy);
static void tex_ShiftPolyFT3(POLY_FT3 *prim, s16 du, s16 dv, s16 dcx, s32 dcy);
static void tex_ShiftPolyFT4(POLY_FT4 *prim, s16 du, s16 dv, s16 dcx, s32 dcy);
static void tex_ShiftPolyGT3(POLY_GT3 *prim, s16 du, s16 dv, s16 dcx, s32 dcy);
static void tex_ShiftPolyGT4(POLY_GT4 *prim, s16 du, s16 dv, s16 dcx, s32 dcy);
static void tex_ShiftPackedA(s16 *prim, s16 du, s16 dv, s16 dcx, s16 dcy);
static void tex_ShiftPackedB(s16 *prim, s16 du, s16 dv, s16 dcx, s16 dcy);

void *g_render_queue[RENDER_QUEUE_SIZE];
s32 *g_reloc_tables[RELOC_BLOCK_COUNT];

/* The four textured primitives, as MESH_FACE_KIND numbers them. */
enum TexPolyKind {
    TEX_POLY_KIND_FT3 = 0,
    TEX_POLY_KIND_FT4 = MESH_FACE_QUAD,
    TEX_POLY_KIND_GT3 = MESH_FACE_LONG,
    TEX_POLY_KIND_GT4 = MESH_FACE_QUAD | MESH_FACE_LONG,
};

/* A primitive type: whether it is textured, the four types of a skinned
 * mesh's textured primitives, and its two untextured ones. */
#define PRIM_TYPE_TEXTURED 4
#define PRIM_TYPE_SKINNED_FIRST 0x26
#define PRIM_TYPE_SKINNED_F3 0
#define PRIM_TYPE_SKINNED_F4 8

/* How long things are in a mesh, in halfwords: a vertex; a record of a
 * compact mesh, for each of the four textured primitives; one of a skinned
 * mesh, whose untextured ones the table of sizes does not have; and a vertex
 * a skinned section carries over to the next. */
#define VERTEX_SIZE 3
#define PREBUILT_VERTEX_SIZE 12
#define COMPACT_FT3_SIZE 12
#define COMPACT_FT4_SIZE 12
#define COMPACT_GT3_SIZE 18
#define COMPACT_GT4_SIZE 22
#define SKINNED_FT3_SIZE 10
#define SKINNED_FT4_SIZE 12
#define SKINNED_GT3_SIZE 10
#define SKINNED_GT4_SIZE 12
#define SKINNED_F3_SIZE 26
#define SKINNED_F4_SIZE 32
#define SKINNED_CARRY_SIZE 3

/* Where corner `n`'s v is in a record, in bytes: a compact record has its
 * corners' u and v in a row after three halfwords, a skinned one has them a
 * word apart as a finished primitive does. */
#define COMPACT_V(n) (7 + 2 * (n))
#define SKINNED_V(n) (1 + 4 * (n))

/* The ordering table has this many entries to each doubling of the
 * distance (render_DepthToOt). */
#define OT_OCTAVE_SHIFT 11
#define OT_OCTAVE (1 << OT_OCTAVE_SHIFT)

/* How near the camera things are still drawn, as an ordering-table index:
 * usually, and in the first-person view or when a caller has asked for
 * nearer (render_SetClipNear(0); the request lasts one frame, and nothing
 * makes it). */
#define OT_CUTOFF_USUAL 0x182
#define OT_CUTOFF_NEAR 0xBE

/* Zeroed data. */
MATRIX g_render_view_matrix;
MATRIX g_render_tilt_matrix;
u16 g_reloc_counts[RELOC_BLOCK_COUNT];

/* ---- drawing ---- */

/* Draws a fighter's body. The assembly that walks the queue calls this for a
 * skinned node: `matrices` is the list of the bones' world matrices
 * (Model.skin_matrices), `mesh` the skinned mesh, `node` the queued node.
 * A fighter is drawn in a view squashed to half height, as the whole frame
 * is, with the depth scale turned round, and with its own handlers: the
 * body's, or the outline ones if the node asks for an outline. */
void render_DrawSkinned(
    s32 matrices, s32 mesh, s16 light_setup_index, u8 *node) {
    MATRIX *view = RENDER_VIEW_MATRIX;
    s32 depth_shift;

    depth_shift = RENDER_DEPTH_SHIFT;
    RENDER_DEPTH_SHIFT = 3 - depth_shift;

    *view = g_render_view_matrix;

    g_light_current = &g_light_setups[light_setup_index];

    view->m[1][0] >>= 1;
    view->m[1][1] >>= 1;
    view->m[1][2] >>= 1;
    view->t[1] >>= 1;

    /* node[1] is SceneNode.draw_flags */
    if (node[1] & NODE_DRAW_OUTLINE) {
        RENDER_HANDLERS = (s32)&g_render_outline_handlers;
    } else {
        RENDER_HANDLERS = (s32)&g_render_skinned_handlers;
    }

    render_DrawSkinnedMesh(matrices, mesh, (s32)view, depth_shift);

    RENDER_HANDLERS = (s32)&g_render_handlers;
}

/* Which of 1/4, 1/2, 1 and 2 a 16.16 number is nearest to, as 0..3: the
 * boundaries are those numbers times the square root of 2. Nothing calls
 * it. */
static s32 math_Pow2Band(s32 value) {
    s32 band;
    if (value > 0x16A09) { /* 1.414 */
        band = 3;
    } else if (value > 0xB500) { /* 0.707 */
        band = 2;
    } else {
        band = value >= 0x5A01; /* 0.352 */
    }
    return band;
}

/* The ordering-table index for a depth: the table is finer near the camera,
 * each doubling of the distance taking as many entries as the one before.
 * The depth's place in its octave is shifted down once for each octave
 * before it, with ones coming in from the top. */
s32 render_DepthToOt(s32 depth) {
    s32 scaled = depth >> RENDER_DEPTH_SHIFT;
    s32 octave = scaled >> OT_OCTAVE_SHIFT;
    s32 fraction = (scaled & (OT_OCTAVE - 1)) | -(RENDER_OT_FARTHEST + 1);
    return (fraction >> octave) & RENDER_OT_FARTHEST;
}

/* ---- moving a mesh's textures ---- */

/* Shifts the textures of every mesh of block `block`. */
void tex_ShiftBlock(s16 block, s16 du, s16 dv, s16 dcx, s32 dcy) {
    u16 dcy_lo = dcy;
    s16 i;

    for (i = 0; i < g_reloc_counts[block]; i++) {
        SET_PTR(RENDER_MESH_CURSOR, (u16 *)ADDR(g_reloc_tables[block][i]));
        tex_ShiftMeshAtCursor(du, dv, dcx, dcy_lo);
    }
}

/* The same in whole texture pages: the shifts are TPAGE_WIDTH and
 * TPAGE_HEIGHT. */
void tex_ShiftBlockPages(
    s16 block, s16 du_pages, s16 dv_pages, s16 dcx_pages, s16 dcy_pages) {
    tex_ShiftBlock(block, (du_pages << 6), (dv_pages << 8), (dcx_pages << 6),
                   (s16)(dcy_pages << 8));
}

/* Shifts the textures of mesh `mesh` of block `block`. Defined old-style:
 * its caller passes plain ints with no prototype in sight. */
void tex_ShiftMesh(block, mesh, du, dv, dcx, dcy) s16 block;
s16 mesh;
s16 du;
s16 dv;
s16 dcx;
s16 dcy;
{
    SET_PTR(RENDER_MESH_CURSOR, (u16 *)ADDR(g_reloc_tables[block][mesh]));
    tex_ShiftMeshAtCursor(du, dv, dcx, dcy);
}

/* Walks the mesh at RENDER_MESH_CURSOR and shifts every textured primitive
 * in it: the texture page by (du, dv) pixels and the colour table by (dcx,
 * dcy). Untextured groups are stepped over by their records' sizes. */
static void tex_ShiftMeshAtCursor(s16 du, s16 dv, s16 dcx, s16 dcy) {
    s32 count;
    s32 layout;
    s32 type;
    s32 kind;
    s32 skinned;
    s32 sections_done;
    u16 *header;
    u8 *record;

    sections_done = 0;
    do {
        /* The vertices. */
        header = PTR(RENDER_MESH_CURSOR);
        SET_PTR(RENDER_MESH_CURSOR, header + 1);
        count = *header;
        if (count & MESH_SKINNED_FLAG) {
            skinned = 1;
            layout = MESH_SKINNED;
            SET_PTR(RENDER_MESH_CURSOR,
                    PTR(RENDER_MESH_CURSOR) +
                        (count & (MESH_SKINNED_FLAG - 1)) + 1);
            if ((u32)PTR(RENDER_MESH_CURSOR) & 3) {
                SET_PTR(RENDER_MESH_CURSOR,
                        (u16 *)((u8 *)PTR(RENDER_MESH_CURSOR) +
                                (4 - ((u32)PTR(RENDER_MESH_CURSOR) & 3))));
            }
        } else {
            SET_PTR(RENDER_MESH_CURSOR, header + 2);
            layout = header[1];
            skinned = 0;
            if (layout == MESH_PREBUILT) {
                ADD_PTR(RENDER_MESH_CURSOR, count * PREBUILT_VERTEX_SIZE);
            } else {
                ADD_PTR(RENDER_MESH_CURSOR, count * VERTEX_SIZE);
            }
        }
        if (layout == MESH_COMPACT) {
            if ((u32)PTR(RENDER_MESH_CURSOR) & 3) {
                NEXT_PTR(RENDER_MESH_CURSOR);
            }
        }
        /* The groups of primitives. */
        while ((count = *NEXT_PTR(RENDER_MESH_CURSOR)) != 0) {
            type = *NEXT_PTR(RENDER_MESH_CURSOR);
            if ((type & PRIM_TYPE_TEXTURED) ||
                (type >= PRIM_TYPE_SKINNED_FIRST &&
                 type <= PRIM_TYPE_SKINNED_FIRST + 3)) {
                if ((type >= PRIM_TYPE_SKINNED_FIRST &&
                     type <= PRIM_TYPE_SKINNED_FIRST + 3)) {
                    kind = type - PRIM_TYPE_SKINNED_FIRST;
                } else {
                    kind = MESH_FACE_KIND(type);
                }
                switch (layout) {
                case MESH_PREBUILT:
                    while (--count != -1) {
                        switch (kind) {
                        case TEX_POLY_KIND_FT3:
                            tex_ShiftPolyFT3(
                                (POLY_FT3 *)PTR(RENDER_MESH_CURSOR), du, dv,
                                dcx, dcy);
                            tex_ShiftPolyFT3(
                                (POLY_FT3 *)PTR(RENDER_MESH_CURSOR) + 1, du, dv,
                                dcx, dcy);
                            break;
                        case TEX_POLY_KIND_FT4:
                            tex_ShiftPolyFT4(
                                (POLY_FT4 *)PTR(RENDER_MESH_CURSOR), du, dv,
                                dcx, dcy);
                            tex_ShiftPolyFT4(
                                (POLY_FT4 *)PTR(RENDER_MESH_CURSOR) + 1, du, dv,
                                dcx, dcy);
                            break;
                        case TEX_POLY_KIND_GT3:
                            tex_ShiftPolyGT3(
                                (POLY_GT3 *)PTR(RENDER_MESH_CURSOR), du, dv,
                                dcx, dcy);
                            tex_ShiftPolyGT3(
                                (POLY_GT3 *)PTR(RENDER_MESH_CURSOR) + 1, du, dv,
                                dcx, dcy);
                            break;
                        case TEX_POLY_KIND_GT4:
                            tex_ShiftPolyGT4(
                                (POLY_GT4 *)PTR(RENDER_MESH_CURSOR), du, dv,
                                dcx, dcy);
                            tex_ShiftPolyGT4(
                                (POLY_GT4 *)PTR(RENDER_MESH_CURSOR) + 1, du, dv,
                                dcx, dcy);
                            break;
                        }
                        ADD_PTR(RENDER_MESH_CURSOR, g_render_prim_sizes[type]);
                    }
                    break;
                case MESH_COMPACT:
                    while (--count != -1) {
                        tex_ShiftPackedA(
                            PTR(RENDER_MESH_CURSOR), du, dv, dcx, dcy);
                        record = (u8 *)PTR(RENDER_MESH_CURSOR);
                        switch (kind) {
                        case TEX_POLY_KIND_FT3:
                            record[COMPACT_V(0)] += dv;
                            record[COMPACT_V(1)] += dv;
                            record[COMPACT_V(2)] += dv;
                            ADD_PTR(RENDER_MESH_CURSOR, COMPACT_FT3_SIZE);
                            break;
                        case TEX_POLY_KIND_FT4:
                            record[COMPACT_V(0)] += dv;
                            record[COMPACT_V(1)] += dv;
                            record[COMPACT_V(2)] += dv;
                            record[COMPACT_V(3)] += dv;
                            ADD_PTR(RENDER_MESH_CURSOR, COMPACT_FT4_SIZE);
                            break;
                        case TEX_POLY_KIND_GT3:
                            record[COMPACT_V(0)] += dv;
                            record[COMPACT_V(1)] += dv;
                            record[COMPACT_V(2)] += dv;
                            ADD_PTR(RENDER_MESH_CURSOR, COMPACT_GT3_SIZE);
                            break;
                        case TEX_POLY_KIND_GT4:
                            record[COMPACT_V(0)] += dv;
                            record[COMPACT_V(1)] += dv;
                            record[COMPACT_V(2)] += dv;
                            record[COMPACT_V(3)] += dv;
                            ADD_PTR(RENDER_MESH_CURSOR, COMPACT_GT4_SIZE);
                            break;
                        default:
                            sys_Trap();
                            break;
                        }
                    }
                    break;
                case MESH_SKINNED:
                    while (--count != -1) {
                        tex_ShiftPackedB(
                            PTR(RENDER_MESH_CURSOR), du, dv, dcx, dcy);
                        record = (u8 *)PTR(RENDER_MESH_CURSOR);
                        switch (kind) {
                        case TEX_POLY_KIND_FT3:
                            record[SKINNED_V(0)] += dv;
                            record[SKINNED_V(1)] += dv;
                            record[SKINNED_V(2)] += dv;
                            ADD_PTR(RENDER_MESH_CURSOR, SKINNED_FT3_SIZE);
                            break;
                        case TEX_POLY_KIND_FT4:
                            record[SKINNED_V(0)] += dv;
                            record[SKINNED_V(1)] += dv;
                            record[SKINNED_V(2)] += dv;
                            record[SKINNED_V(3)] += dv;
                            ADD_PTR(RENDER_MESH_CURSOR, SKINNED_FT4_SIZE);
                            break;
                        case TEX_POLY_KIND_GT3:
                            /* the same as the two cases above, written out
                             * again as the original has it */
                            record[SKINNED_V(0)] += dv;
                            record[SKINNED_V(1)] += dv;
                            record[SKINNED_V(2)] += dv;
                            ADD_PTR(RENDER_MESH_CURSOR, SKINNED_GT3_SIZE);
                            break;
                        case TEX_POLY_KIND_GT4:
                            record[SKINNED_V(0)] += dv;
                            record[SKINNED_V(1)] += dv;
                            record[SKINNED_V(2)] += dv;
                            record[SKINNED_V(3)] += dv;
                            ADD_PTR(RENDER_MESH_CURSOR, SKINNED_GT4_SIZE);
                            break;
                        default:
                            sys_Trap();
                            break;
                        }
                    }
                    break;
                }
            } else if (!skinned) {
                ADD_PTR(RENDER_MESH_CURSOR,
                        (s16)(count * g_render_prim_sizes[type]));
            } else {
                switch (type) {
                case PRIM_TYPE_SKINNED_F3:
                    ADD_PTR(RENDER_MESH_CURSOR, count * SKINNED_F3_SIZE);
                    break;
                case PRIM_TYPE_SKINNED_F4:
                    ADD_PTR(RENDER_MESH_CURSOR, count * SKINNED_F4_SIZE);
                    break;
                default:
                    ADD_PTR(RENDER_MESH_CURSOR,
                            (s16)(count * g_render_prim_sizes[type]));
                    break;
                }
            }
        }
        /* A skinned mesh's section ends with a list of vertices to carry
         * over to the next, ended by 0xFFFF. */
        if (skinned) {
            if (sections_done == MESH_SKINNED_SECTIONS - 1) {
                return;
            }
            sections_done++;
            while (*PTR(RENDER_MESH_CURSOR) != 0xFFFF) {
                ADD_PTR(RENDER_MESH_CURSOR, SKINNED_CARRY_SIZE);
            }
            NEXT_PTR(RENDER_MESH_CURSOR);
        }
    } while (*NEXT_PTR(RENDER_MESH_CURSOR) != 0);
}

/* The four functions below shift one finished primitive: its texture-page
 * word by whole pages, its colour-table word, and each corner's v by the
 * rows moved (tex.h has the two words' fields). This one a flat textured
 * triangle. */
static void tex_ShiftPolyFT3(POLY_FT3 *prim, s16 du, s16 dv, s16 dcx, s32 dcy) {
    u16 tpage, clut;
    s32 tx, ty, cx, cy;

    tpage = prim->tpage;
    tx = ((tpage & TPAGE_COLUMN_MASK) + (du >> 6)) & TPAGE_COLUMN_MASK;
    ty = (((tpage >> TPAGE_ROW_SHIFT) & 1) + (dv >> 8)) & 1;
    prim->tpage =
        tx | ((tpage & (u16)~TPAGE_PLACE_MASK) | (ty << TPAGE_ROW_SHIFT));
    prim->v0 += dv;
    prim->v1 += dv;
    prim->v2 += dv;
    clut = prim->clut;
    cx = ((dcx >> 4) + (clut & CLUT_X_MASK)) & CLUT_X_MASK;
    cy = (dcy + ((clut >> CLUT_Y_SHIFT) & CLUT_Y_MASK)) & CLUT_Y_MASK;
    prim->clut = cx | ((clut & CLUT_TOP_BIT) | (cy << CLUT_Y_SHIFT));
}

/* A flat textured quad. */
static void tex_ShiftPolyFT4(POLY_FT4 *prim, s16 du, s16 dv, s16 dcx, s32 dcy) {
    u16 tpage, clut;
    s32 tx, ty, cx, cy;

    tpage = prim->tpage;
    tx = ((tpage & TPAGE_COLUMN_MASK) + (du >> 6)) & TPAGE_COLUMN_MASK;
    ty = (((tpage >> TPAGE_ROW_SHIFT) & 1) + (dv >> 8)) & 1;
    prim->tpage =
        tx | ((tpage & (u16)~TPAGE_PLACE_MASK) | (ty << TPAGE_ROW_SHIFT));
    prim->v0 += dv;
    prim->v1 += dv;
    prim->v2 += dv;
    prim->v3 += dv;
    clut = prim->clut;
    cx = ((dcx >> 4) + (clut & CLUT_X_MASK)) & CLUT_X_MASK;
    cy = (dcy + ((clut >> CLUT_Y_SHIFT) & CLUT_Y_MASK)) & CLUT_Y_MASK;
    prim->clut = cx | ((clut & CLUT_TOP_BIT) | (cy << CLUT_Y_SHIFT));
}

/* A Gouraud textured triangle. */
static void tex_ShiftPolyGT3(POLY_GT3 *prim, s16 du, s16 dv, s16 dcx, s32 dcy) {
    u16 tpage, clut;
    s32 tx, ty, cx, cy;

    tpage = prim->tpage;
    tx = ((tpage & TPAGE_COLUMN_MASK) + (du >> 6)) & TPAGE_COLUMN_MASK;
    ty = (((tpage >> TPAGE_ROW_SHIFT) & 1) + (dv >> 8)) & 1;
    prim->tpage =
        tx | ((tpage & (u16)~TPAGE_PLACE_MASK) | (ty << TPAGE_ROW_SHIFT));
    prim->v0 += dv;
    prim->v1 += dv;
    prim->v2 += dv;
    clut = prim->clut;
    cx = ((dcx >> 4) + (clut & CLUT_X_MASK)) & CLUT_X_MASK;
    cy = (dcy + ((clut >> CLUT_Y_SHIFT) & CLUT_Y_MASK)) & CLUT_Y_MASK;
    prim->clut = cx | ((clut & CLUT_TOP_BIT) | (cy << CLUT_Y_SHIFT));
}

/* A Gouraud textured quad. */
static void tex_ShiftPolyGT4(POLY_GT4 *prim, s16 du, s16 dv, s16 dcx, s32 dcy) {
    u16 tpage, clut;
    s32 tx, ty, cx, cy;

    tpage = prim->tpage;
    tx = ((tpage & TPAGE_COLUMN_MASK) + (du >> 6)) & TPAGE_COLUMN_MASK;
    ty = (((tpage >> TPAGE_ROW_SHIFT) & 1) + (dv >> 8)) & 1;
    prim->tpage =
        tx | ((tpage & (u16)~TPAGE_PLACE_MASK) | (ty << TPAGE_ROW_SHIFT));
    prim->v0 += dv;
    prim->v1 += dv;
    prim->v2 += dv;
    prim->v3 += dv;
    clut = prim->clut;
    cx = ((dcx >> 4) + (clut & CLUT_X_MASK)) & CLUT_X_MASK;
    cy = (dcy + ((clut >> CLUT_Y_SHIFT) & CLUT_Y_MASK)) & CLUT_Y_MASK;
    prim->clut = cx | ((clut & CLUT_TOP_BIT) | (cy << CLUT_Y_SHIFT));
}

/* A compact record's texture page and colour table: halfwords 0 and 2. */
static void tex_ShiftPackedA(s16 *prim, s16 du, s16 dv, s16 dcx, s16 dcy) {
    s16 tpage;
    tpage = tex_ShiftTPage(prim[0], du, dv);
    prim[0] = tpage;
    prim[2] = tex_ShiftClut(prim[2], dcx, dcy);
}

/* A skinned record's: halfwords 3 and 1. */
static void tex_ShiftPackedB(s16 *prim, s16 du, s16 dv, s16 dcx, s16 dcy) {
    s16 tpage;
    tpage = tex_ShiftTPage(prim[3], du, dv);
    prim[3] = tpage;
    prim[1] = tex_ShiftClut(prim[1], dcx, dcy);
}

/* A texture-page word moved by (du, dv) pixels, in whole pages. */
static s16 tex_ShiftTPage(s16 tpage, s16 du, s16 dv) {
    s32 column = (tpage & TPAGE_COLUMN_MASK) + (du >> 6);
    s32 row;
    column &= TPAGE_COLUMN_MASK;
    row = ((tpage >> TPAGE_ROW_SHIFT) & 1) + (dv >> 8);
    row &= 1;
    return column | ((tpage & ~TPAGE_PLACE_MASK) | (row << TPAGE_ROW_SHIFT));
}

/* A colour-table word moved by (dcx, dcy) pixels. Its y is taken out by
 * shifting the word up past its top bit and down again. */
static s16 tex_ShiftClut(s16 clut, s16 dcx, s32 dcy) {
    s32 x = (dcx >> 4) + (clut & CLUT_X_MASK);
    s32 y;
    x &= CLUT_X_MASK;
    y = (dcy + (((u32)(clut << 17)) >> 23)) & CLUT_Y_MASK;
    return x | ((clut & CLUT_TOP_BIT) | (y << CLUT_Y_SHIFT));
}

/* ---- mesh pointer tables ---- */

/* Registers the table at `table` as block `block` and, if that has not been
 * done to it before, turns its offsets (from the table's start) into
 * pointers. */
void reloc_RelocateBlock(s32 *table, s16 block) {
    s32 *base = table;
    s32 header;
    s32 i;
    u16 count;

    header = *table;
    *table = (header | RELOC_DONE) & 0xFFFF;
    table++;
    g_reloc_tables[block] = table;
    g_reloc_counts[block] = header & (RELOC_DONE - 1);
    if (!(header & RELOC_DONE)) {
        count = header;
        for (i = 0; i < count; i++) {
            *table++ += (s32)base;
        }
    }
}

/* Turns block `block`'s pointers back into offsets. */
void reloc_UnrelocateBlock(s16 block) {
    s32 *entry;
    s32 count;
    s32 *base;

    entry = g_reloc_tables[block];
    count = *(entry - 1);
    base = entry - 1;
    if (count & RELOC_DONE) {
        count = count & (RELOC_DONE - 1);
        *(entry - 1) = count;
        count = count - 1;
        if (count != -1) {
            do {
                *entry -= (s32)base;
                /* FAKE: entry++, with a step forward and back before it */
                entry++;
                entry--;
                entry++;
                count--;
            } while (count != -1);
        }
    }
}

/* Block `block`'s file has moved by `byte_offset`: moves the table's address
 * and every pointer in it. */
void reloc_RebaseBlock(s32 block, s32 byte_offset) {
    s32 *entry = g_reloc_tables[block];
    s32 count = g_reloc_counts[block];
    g_reloc_tables[block] = (entry = entry + (byte_offset / 4));
    for (count--; count != -1; count--) {
        *entry += byte_offset;
        entry++;
    }
}

/* The three functions below cut a file that starts with a table of offsets
 * (a count, the offset of each entry, and the offset of the end) down to
 * some of its entries, which are moved up to follow the new, shorter table.
 * Each returns the address of the end of the result.
 *
 * This one keeps the entries listed after `file`: how many, then their
 * numbers. Nothing calls it. */
static s32 reloc_KeepListedEntries(s32 *file, ...) {
    s32 *base;
    s32 old_count;
    s32 count;
    s32 *offset;
    s32 dest;
    s32 entry;
    u32 size;
    va_list ap;

    base = file;
    old_count = *file++;
    va_start(ap, file);
    count = va_arg(ap, s32);
    *base = count;
    offset = file;
    dest = (s32)base + *offset;
    count--;
    if (count != -1) {
        do {
            s32 start;

            entry = va_arg(ap, s32);
            if (entry >= old_count) {
                sys_Trap();
            }
            count--;
            start = file[entry];
            size = file[entry + 1];
            *offset = dest - (s32)base;
            offset++;
            size = size - start;
            mem_MoveWords((s32)base + start, dest, size);
            size = (size >> 2) * 4;
            dest += size;
        } while (count != -1);
    }
    *offset = dest - (s32)base;
    return dest;
}

/* Keeps the entries that `selected` marks: one s16 per entry, negative for
 * an entry to drop, the list ended by RELOC_LIST_END. */
s32 reloc_KeepSelectedEntries(s32 *file, s16 *selected) {
    /* FAKE: a copy of the parameter */
    s32 *base = file;
    s32 *offset = base + 1;
    s32 count = 0;
    s32 *dest = (s32 *)ADDR((s32)base + base[1]);
    s32 mark;
    s32 *entries;
    s32 end;
    u32 size;
    s32 start;
    s32 result;
    s32 i;

    mark = *selected;
    selected++;
    if (mark == RELOC_LIST_END) {
        result = (s32)dest;
    } else {
        entries = offset;
        i = 0;
        do {
            if (mark >= 0) {
                end = entries[i + 1];
                start = entries[i];
                *offset = (s32)dest - (s32)base;
                offset++;
                count++;
                size = end - start;
                mem_MoveWords((s32)base + start, (s32)dest, size);
                dest = (s32 *)ADDR((s32)dest + ((size >> 2) << 2));
            }
            i++;
            mark = *selected;
            selected++;
        } while (mark != RELOC_LIST_END);
        result = (s32)dest;
    }

    /* `mark` goes on to hold the offset of the end */
    mark = result - (s32)base;
    *base = count;
    *offset = mark;
    return result;
}

/* The same, but the result is built at `dest` and the file at `src` is left
 * as it was. */
s32 reloc_CopySelectedEntries(s32 src, s32 *dest, s16 *selected) {
    s16 *mark_cursor;
    s16 *count_cursor;
    s32 *dest_base;
    s32 mark;
    s32 data;
    s32 src_base;
    s32 end_mark;
    s32 count;
    s32 *entry;
    s32 start;
    s32 end;
    u32 size;

    mark_cursor = selected;
    count = 0;
    count_cursor = mark_cursor + 1;
    src_base = src;
    dest_base = dest;

    mark = *mark_cursor;
    if (mark != RELOC_LIST_END) {
        do {
            if (mark >= 0) {
                count++;
            }
            mark = *count_cursor;
            count_cursor++;
        } while (mark != RELOC_LIST_END);
    }

    *dest = count;
    dest++;
    src += 4;
    data = (s32)dest_base + (count + 2) * 4;

    mark = *mark_cursor;
    mark_cursor++;
    if (mark != RELOC_LIST_END) {
        end_mark = RELOC_LIST_END;
        entry = (s32 *)ADDR(src);
        do {
            if (mark >= 0) {
                end = entry[1];
                start = *entry;
                *dest = data - (s32)dest_base;
                dest++;
                size = end - start;
                mem_MoveWords(src_base + start, data, size);
                data += (size >> 2) << 2;
            }
            entry++;
            mark = *mark_cursor;
            mark_cursor++;
        } while (mark != end_mark);
    }

    *dest = data - (s32)dest_base;
    return data;
}

/* No block has a table. */
void reloc_ClearCounts(void) {
    s32 i;

    for (i = 0; i < RELOC_BLOCK_COUNT; i++) {
        g_reloc_counts[i] = 0;
    }
}

/* ---- flushing the queue ---- */

/* The end of a frame of the fight: draws what was queued. */
void render_Flush(void) { render_FlushTo(g_render_ot); }

/* The same at the end of a frame of a cutscene. */
void render_FlushCutscene(void) { render_FlushTo(g_render_ot); }

/* Works out the view matrix from the two view roots and the view's tilt,
 * sets up the renderer's scratch, has every queued node drawn into the
 * ordering table `ot`, and empties the queue. */
static void render_FlushTo(u32 *ot) {
    math_RotMatrixZXY(&g_render_view_tilt, &g_render_tilt_matrix);
    MulMatrix(&g_render_tilt_matrix, &g_node_camera_root_ptr->world);
    MulMatrix2(&g_node_identity_root_ptr->world, &g_render_tilt_matrix);
    MulMatrix0(&g_node_identity_root_ptr->world, &g_node_camera_root_ptr->world,
               &g_render_view_matrix);
    if (g_disp_frame_count & 1) {
        RENDER_FRAME_MASK = -1;
    } else {
        RENDER_FRAME_MASK = 0;
    }
    light_SetShadowMatrix();
    RENDER_HANDLERS = (s32)&g_render_handlers;
    RENDER_OT = (s32)ot;
    if (g_render_options & RENDER_OPT_FOG) {
        RENDER_FOG_OT_INDEX = stage_GetFogOtIndex();
    } else {
        RENDER_FOG_OT_INDEX = 0x7FFFFFFF; /* deeper than anything: no fog */
    }
    if (stage_IsFirstPerson()) {
        RENDER_OT_CUTOFF = OT_CUTOFF_NEAR;
    } else if (render_GetClipNear()) {
        RENDER_OT_CUTOFF = OT_CUTOFF_USUAL;
    } else {
        RENDER_OT_CUTOFF = OT_CUTOFF_NEAR;
    }
    render_DrawQueue();
    render_SetClipNear(1);
    g_render_queue_end = g_render_queue;
}

/* The handler of every primitive type a table has no routine for. */
static void render_BadPrimType(void) { sys_Trap(); }

/* ---- the handler tables ----
 * A table has a routine for each primitive type; the renderer calls the one
 * for each group of a mesh. Which table is in use depends on what is being
 * drawn. The routines are in render_asm.c.
 *
 * In a type, 4 says textured, 8 four corners, 16 a normal for each corner
 * (Gouraud shading); the low two bits pick a variant. The routines are named
 * for the primitive they make, in Sony's letters (FT3 a flat textured
 * triangle, GT4 a Gouraud textured quad, F3 a flat untextured triangle) and
 * for the variant:
 *  - plain: lit by the node's lights, drawn if it faces the camera;
 *  - Unlit: the texture as it is, and nothing nearer than the cutoff;
 *  - TwoSided: drawn whichever way it faces, lit from the side seen;
 *  - Black: flat black (types 32 and 33);
 *  - Tinted: lit, then scaled by a colour (types 42 and 43);
 *  - Packed: for the meshes whose vertices are stored packed; the flat ones
 *    draw nothing nearer than the cutoff, the Gouraud ones are copies of the
 *    plain routines;
 *  - Skin: a fighter's body, the corners' colours from the table worked out
 *    for the whole mesh (FT: their average);
 *  - Outline: the edges as lines in the outline colour, in the slots of the
 *    textured types 48 up and of the body;
 *  - Tiny: the tiny model's flat faces, drawn into its own ordering table;
 *  - Cloth: seen from both sides, and built where the frame's primitives
 *    are (the table of them is with the read-only data);
 *  - Split: a stage tile, or the surf: cut into smaller ones near the
 *    camera. */
/* clang-format off */
/* The tables, four types to a row: the number is the row's first type, and
 * the letters the primitive the types of the row make. */
#define NONE render_BadPrimType
/* A fighter's body. */
static void (*g_render_skinned_handlers[42])() = {
    /*  0 F3  */ NONE, NONE, NONE, NONE,
    /*  4 FT3 */ NONE, NONE, NONE, NONE,
    /*  8 F4  */ NONE, NONE, NONE, NONE,
    /* 12 FT4 */ NONE, NONE, NONE, NONE,
    /* 16 G3  */ NONE, NONE, NONE, NONE,
    /* 20 GT3 */ NONE, NONE, NONE, NONE,
    /* 24 G4  */ NONE, NONE, NONE, NONE,
    /* 28 GT4 */ NONE, NONE, NONE, NONE,
    /* 32     */ NONE, NONE, NONE, NONE,
    /* 36     */ NONE, NONE, render_EmitSkinFT3, render_EmitSkinFT4,
    /* 40     */ render_EmitSkinGT3, render_EmitSkinGT4,
};
/* A fighter's body drawn as outlines. */
static void (*g_render_outline_handlers[42])() = {
    /*  0 F3  */ NONE, NONE, NONE, NONE,
    /*  4 FT3 */ render_EmitOutlineFT3, NONE, render_EmitOutlineFT3, NONE,
    /*  8 F4  */ NONE, NONE, NONE, NONE,
    /* 12 FT4 */ render_EmitOutlineFT4, NONE, render_EmitOutlineFT4, NONE,
    /* 16 G3  */ NONE, NONE, NONE, NONE,
    /* 20 GT3 */ render_EmitOutlineGT3, NONE, render_EmitOutlineGT3, NONE,
    /* 24 G4  */ NONE, NONE, NONE, NONE,
    /* 28 GT4 */ render_EmitOutlineGT4, NONE, render_EmitOutlineGT4, NONE,
    /* 32     */ NONE, NONE, NONE, NONE,
    /* 36     */ NONE, NONE,
                 render_EmitSkinOutlineFT3, render_EmitSkinOutlineFT4,
    /* 40     */ render_EmitSkinOutlineGT3, render_EmitSkinOutlineGT4,
};
/* Everything else: the table in use unless another is set. */
void (*g_render_handlers[88])() = {
    /*  0 F3  */ NONE, NONE, NONE, NONE,
    /*  4 FT3 */ render_EmitFT3, render_EmitUnlitFT3,
                 render_EmitTwoSidedFT3, NONE,
    /*  8 F4  */ NONE, NONE, NONE, NONE,
    /* 12 FT4 */ render_EmitFT4, render_EmitUnlitFT4,
                 render_EmitTwoSidedFT4, NONE,
    /* 16 G3  */ NONE, NONE, NONE, NONE,
    /* 20 GT3 */ render_EmitGT3, NONE, render_EmitTwoSidedGT3, NONE,
    /* 24 G4  */ NONE, NONE, NONE, NONE,
    /* 28 GT4 */ render_EmitGT4, NONE, render_EmitTwoSidedGT4, NONE,
    /* 32     */ render_EmitBlackF3, render_EmitBlackF4, NONE, NONE,
    /* 36     */ NONE, NONE, NONE, NONE,
    /* 40     */ NONE, NONE, render_EmitTintedFT3, render_EmitTintedFT4,
    /* 44     */ NONE, NONE, NONE, NONE,
    /* 48     */ render_EmitOutlineFT3, NONE, render_EmitOutlineFT3, NONE,
    /* 52     */ NONE, NONE, NONE, NONE,
    /* 56     */ render_EmitOutlineFT4, NONE, render_EmitOutlineFT4, NONE,
    /* 60     */ NONE, NONE, NONE, NONE,
    /* 64     */ render_EmitOutlineGT3, NONE, render_EmitOutlineGT3, NONE,
    /* 68     */ NONE, NONE, NONE, NONE,
    /* 72     */ render_EmitOutlineGT4, NONE, render_EmitOutlineGT4, NONE,
    /* 76     */ NONE, NONE, NONE, NONE,
    /* 80     */ NONE, NONE, NONE, NONE,
    /* 84     */ NONE, NONE, NONE, NONE,
};
/* The two tables below are chosen by the assembly that walks the queue,
 * for some kinds of node. */
void (*g_render_packed_handlers[32])() = {
    /*  0 F3  */ NONE, NONE, NONE, NONE,
    /*  4 FT3 */ render_EmitPackedFT3, render_EmitUnlitFT3,
                 render_EmitTwoSidedFT3, NONE,
    /*  8 F4  */ NONE, NONE, NONE, NONE,
    /* 12 FT4 */ render_EmitPackedFT4, render_EmitUnlitFT4,
                 render_EmitTwoSidedFT4, NONE,
    /* 16 G3  */ NONE, NONE, NONE, NONE,
    /* 20 GT3 */ render_EmitPackedGT3, NONE, render_EmitTwoSidedGT3, NONE,
    /* 24 G4  */ NONE, NONE, NONE, NONE,
    /* 28 GT4 */ render_EmitPackedGT4, NONE, render_EmitTwoSidedGT4, NONE,
};
void (*g_render_tiny_handlers[32])() = {
    /*  0 F3  */ render_EmitTinyF3, NONE, NONE, NONE,
    /*  4 FT3 */ NONE, NONE, NONE, NONE,
    /*  8 F4  */ render_EmitTinyF4, NONE, NONE, NONE,
    /* 12 FT4 */ NONE, NONE, NONE, NONE,
    /* 16 G3  */ NONE, NONE, NONE, NONE,
    /* 20 GT3 */ NONE, NONE, NONE, NONE,
    /* 24 G4  */ NONE, NONE, NONE, NONE,
    /* 28 GT4 */ NONE, NONE, NONE, NONE,
};
#undef NONE
/* clang-format on */

/* The size of a prebuilt mesh's record of each primitive type, in
 * halfwords: both copies of the primitive. */
/* clang-format off */
static s16 g_render_prim_sizes[44] = {
    /*  0 F3  */ 28, 22, 28, 22,
    /*  4 FT3 */ 38, 34, 38,  0,
    /*  8 F4  */ 32, 26,  0,  0,
    /* 12 FT4 */ 46, 42, 46,  0,
    /* 16 G3  */ 48,  0, 48,  0,
    /* 20 GT3 */ 52,  0, 52,  0,
    /* 24 G4  */ 62,  0,  0,  0,
    /* 28 GT4 */ 66,  0, 66,  0,
    /* 32     */  2,  2, 38, 46,
    /* 36     */  0, 40, 10, 12,
    /* 40     */ 10, 12, 38, 46,
};
/* clang-format on */

/* Small data that this file reaches through the global pointer. */
SceneNode *g_node_camera_root_ptr;
u32 *g_render_ot;
s32 g_render_options;
void **g_render_queue_end;
LightSetup *g_light_current;
