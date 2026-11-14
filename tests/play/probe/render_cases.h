#ifndef PROBE_RENDER_CASES_H
#define PROBE_RENDER_CASES_H

/* The cases the renderer's C is held to its assembly by
 * (tests/play/README.md, "The GTE probe"). As with routines.h, two
 * programs include this file: the probe runs the renderer's listings as
 * the game links them, on the emulator; the host test
 * tests/port/render_vectors.c runs the C the port has beside them. A
 * case makes up a mesh, the scratchpad and the GTE's registers, runs one
 * routine, and what is compared is everything it could have written: the
 * ordering tables, the primitives, the mesh, the scratchpad, where the
 * cursors were left, and the GTE's 64 registers.
 *
 * Both programs keep what a routine reads and writes at the same
 * addresses (the arena below, and the scratchpad where the console has
 * it), so the links and pointers a routine stores are the same numbers on
 * both and are compared as they are.
 *
 * Every number is made up by the generator of cases.h. Nothing is the
 * game's data.
 *
 * Wants cases.h and the game's headers ahead of it, and from the program
 * that includes it the three ways of calling a routine, which differ
 * (the listings take their arguments in registers the C does not have):
 *
 *   PROBE_HANDLER(routine, count)    a routine of the handler tables
 *   PROBE_PROJECT(routine, count, points, depths)
 *   PROBE_DRAW_GROUPS()
 *   PROBE_DRAW_SKINNED_MESH(matrices, mesh, view, depth_shift)
 *
 * each reading and leaving the mesh cursor, the node and the handler
 * table in g_probe_walk. */

/* How many cases a routine gets. */
#define PROBE_RENDER_VECTORS 200

/* The arena: the ordering table, the tiny model's, the place primitives
 * are built, the mesh, and room for nodes and matrices. */
#define PROBE_ARENA 0x80180000
#define PROBE_OT ((u32 *)PROBE_ARENA)
#define PROBE_OT_WORDS 0x1000
#define PROBE_TINY_OT ((u32 *)(PROBE_ARENA + 0x4000))
#define PROBE_TINY_OT_WORDS 0x100
#define PROBE_PRIMS ((u32 *)(PROBE_ARENA + 0x4400))
#define PROBE_PRIMS_WORDS 0xB00
#define PROBE_MESH ((u8 *)(PROBE_ARENA + 0x7000))
#define PROBE_MESH_WORDS 0xC00
#define PROBE_WORK ((u8 *)(PROBE_ARENA + 0xA000))
#define PROBE_WORK_WORDS 0x400
#define PROBE_ARENA_WORDS 0x2C00
#define PROBE_SCRATCH ((u32 *)SCRATCHPAD)
#define PROBE_SCRATCH_WORDS 0x100

/* The scratchpad's words the renderer keeps (render.h), by number, and
 * where the projected points and their depths are. */
#define PROBE_SCRATCH_RETURN                                                   \
    1 /* the listing's return address: not compared                            \
       */
#define PROBE_SCRATCH_DEPTH_SHIFT 2
#define PROBE_SCRATCH_OT 3
#define PROBE_SCRATCH_FOG 4
#define PROBE_SCRATCH_FRAME_MASK 5
#define PROBE_SCRATCH_CUTOFF 6
#define PROBE_SCRATCH_HANDLERS                                                 \
    7 /* an address of the program's: not compared                             \
       */
#define PROBE_POINTS ((u32 *)(SCRATCHPAD + 0x20))
#define PROBE_DEPTHS ((s16 *)(SCRATCHPAD + 0x2B4))
#define PROBE_SKIN_DEPTHS ((s16 *)(SCRATCHPAD + 0x244))

/* What a case's result is, in words: where the mesh cursor and the
 * primitive cursor were left, a number for each stretch of memory, and
 * what the routine returned. The 64 registers follow. */
enum {
    PROBE_RENDER_MESH_END,
    PROBE_RENDER_PRIM_END,
    PROBE_RENDER_OT,
    PROBE_RENDER_TINY_OT,
    PROBE_RENDER_PRIMS,
    PROBE_RENDER_MESH,
    PROBE_RENDER_WORK,
    PROBE_RENDER_SCRATCH,
    PROBE_RENDER_VALUE,
    PROBE_RENDER_SKIN_COLORS,
    PROBE_RENDER_RESULTS = 12
};

/* The tiny model's ordering table (model_tiny.c), which no header of the
 * PlayStation build declares. */
extern u32 *g_render_tiny_ot;

/* What the assembly names and no header of the PlayStation build
 * declares: the view matrix, the colours of a body's normals and the
 * routine that works them out, and the body's walk. */
extern MATRIX g_render_view_matrix;
extern CVECTOR g_render_skin_colors[500];
extern u8 *render_LightSkin();
extern void render_DrawSkinned(
    s32 matrices, s32 mesh, s16 light_setup_index, u8 *node);
#define PROBE_SKIN_COLORS 500

/* The ordering-table entry of a depth, in C (render.c). */
extern s32 render_DepthToOt(s32 depth);

/* The scale the GTE averages three depths by in the case being made:
 * one of the registers the case loads. */
static s32 g_probe_depth_scale;

/* What the routines pass each other in registers. */
typedef struct {
    u8 *mesh;
    u8 *node;
    void *handlers;
} ProbeWalk;

static ProbeWalk g_probe_walk;

/* ---- stand-ins ---- */

/* Two functions of the game's C that the walk over the queue calls, for
 * a shadow and for the surf. Neither is under test, and both programs
 * have these in their place: the shadow's matrix is the parent's with
 * its second row emptied and the ground's height in its translation;
 * the surf leaves a count of its calls in the arena. */
#define PROBE_WAVE_CALLS (*(u32 *)(PROBE_WORK + 0xFFC))

void light_ProjectShadow(MATRIX *src, MATRIX *out, s32 ground_y) {
    s32 i;

    *out = *src;
    for (i = 0; i < 3; i++) {
        out->m[1][i] = 0;
    }
    out->t[1] = ground_y;
}

void wave_Draw(void) { PROBE_WAVE_CALLS++; }

/* ---- what every case starts from ---- */

/* How many projected points a case has. */
#define PROBE_POINT_COUNT 40

static void probe_FillWords(u32 *words, s32 count, u32 seed) {
    s32 i;

    for (i = 0; i < count; i++) {
        seed = seed * 0x9E3779B1 + 0x7F4A7C15;
        words[i] = seed;
    }
}

/* The arena and the scratchpad filled with numbers a routine would not
 * write, the ordering tables with links, and the renderer's words of the
 * scratchpad set. */
static void probe_RenderBegin(u32 *state, s32 wild) {
    u32 pick = probe_Random(state);
    s32 i;

    probe_FillWords(PROBE_PRIMS, PROBE_ARENA_WORDS - 0x1100, pick);
    for (i = 0; i < PROBE_OT_WORDS; i++) {
        PROBE_OT[i] = (0xC00000 + 4 * (u32)i) ^ (pick & 0xFFF0);
    }
    for (i = 0; i < PROBE_TINY_OT_WORDS; i++) {
        PROBE_TINY_OT[i] = (0xD00000 + 4 * (u32)i) ^ (pick & 0xFFF0);
    }
    probe_FillWords(PROBE_SCRATCH, PROBE_SCRATCH_WORDS, ~pick);
    PROBE_SCRATCH[PROBE_SCRATCH_DEPTH_SHIFT] = (pick >> 12) & (wild ? 7 : 3);
    PROBE_SCRATCH[PROBE_SCRATCH_OT] = (u32)PROBE_OT;
    PROBE_SCRATCH[PROBE_SCRATCH_FOG] = (pick >> 16) & 0xFFF;
    PROBE_SCRATCH[PROBE_SCRATCH_FRAME_MASK] = (pick & 0x8000) ? 0xFFFFFFFF : 0;
    PROBE_SCRATCH[PROBE_SCRATCH_CUTOFF] =
        (pick & 0x4000) ? 0x182 : ((pick >> 20) & 0xFFF);
    g_disp_prim_cursor = PROBE_PRIMS;
    g_render_tiny_ot = PROBE_TINY_OT;
    probe_FillWords((u32 *)g_render_skin_colors, PROBE_SKIN_COLORS, pick * 3);
    g_probe_walk.mesh = PROBE_MESH;
    g_probe_walk.node = PROBE_WORK;
    g_probe_walk.handlers = NULL;
}

/* Projected points and their depths, as if a part's vertices had been
 * projected: mostly on a screen 320 wide, in one case of eight all off
 * its left and in one all above it; depths mostly in front, in some cases
 * any, in some far. `depths` is where the routine reads them. */
static void probe_MakePoints(u32 *state, s16 *depths) {
    u32 pick = probe_Random(state);
    s32 where = pick & 7;
    s32 how_deep = (pick >> 3) & 7;
    s32 i;

    for (i = 0; i < PROBE_POINT_COUNT; i++) {
        s32 x = (s32)(probe_Random(state) % 480) - 80;
        s32 y = (s32)(probe_Random(state) % 360) - 60;
        s32 z;

        if (where == 0) {
            x = -1 - (s32)(probe_Random(state) & 0xFF);
        } else if (where == 1) {
            y = -1 - (s32)(probe_Random(state) & 0xFF);
        }
        PROBE_POINTS[i] = ((u32)x & 0xFFFF) | ((u32)y << 16);
        if (how_deep == 0) {
            z = (s32)probe_Signed(state, 16);
        } else if (how_deep == 1) {
            z = 0x3000 + (s32)(probe_Random(state) % 0x4FFF);
        } else {
            z = 1 + (s32)(probe_Random(state) & 0x1FFF);
        }
        depths[i] = (s16)z;
    }
}

/* A record of `size` bytes at `record`: any numbers, and a corner a byte
 * at `corners_at`, each one of the projected points. */
static u8 *probe_MakeRecord(
    u32 *state, u8 *record, s32 size, s32 corners_at, s32 corner_count) {
    s32 i;

    for (i = 0; i < size; i++) {
        record[i] = (u8)(probe_Random(state) >> 8);
    }
    for (i = 0; i < corner_count; i++) {
        record[corners_at + i] = (u8)(probe_Random(state) % PROBE_POINT_COUNT);
    }
    return record + size;
}

/* A record of a fighter's body: as probe_MakeRecord's, and then where
 * its corners' colours are in the table, ten bits each in a word, and for
 * a quad a halfword more. */
#define PROBE_SKIN_QUAD_SIZE 0x18
#define PROBE_SKIN_QUAD_COLOR 0x0E

static u8 *probe_MakeSkinRecord(
    u32 *state, u8 *record, s32 size, s32 corners_at, s32 corner_count) {
    u8 *end = probe_MakeRecord(state, record, size, corners_at, corner_count);
    u32 colors = probe_Random(state) % PROBE_SKIN_COLORS;

    colors |= (probe_Random(state) % PROBE_SKIN_COLORS) << 10;
    colors |= (probe_Random(state) % PROBE_SKIN_COLORS) << 20;
    *(u32 *)(record + corners_at + 4) = colors;
    if (size == PROBE_SKIN_QUAD_SIZE) {
        *(s16 *)(record + PROBE_SKIN_QUAD_COLOR) =
            (s16)(probe_Random(state) % PROBE_SKIN_COLORS);
    }
    return end;
}

/* A matrix of the sizes the game has, the two bytes after its numbers
 * marked. */
static void probe_MakeBone(u32 *state, MATRIX *matrix) {
    s32 i;

    for (i = 0; i < 9; i++) {
        ((s16 *)matrix->m)[i] = (s16)probe_Signed(state, 13);
    }
    ((u16 *)matrix)[9] = 0x5A5A;
    for (i = 0; i < 3; i++) {
        matrix->t[i] = (s32)probe_Signed(state, 15);
    }
}

/* The node the handlers may read: a SceneNode's bytes and after them the
 * number the tinted faces are scaled by. */
static void probe_MakeNode(u32 *state) {
    probe_FillWords((u32 *)PROBE_WORK, 0x20, probe_Random(state));
    *(s16 *)(PROBE_WORK + 0x60) = (s16)(probe_Random(state) & 0x1FFF);
}

/* What a case left, into `out`: PROBE_RENDER_RESULTS words. */
static void probe_RenderEnd(u32 *out) {
    PROBE_SCRATCH[PROBE_SCRATCH_RETURN] = 0;
    PROBE_SCRATCH[PROBE_SCRATCH_HANDLERS] = 0;
    out[PROBE_RENDER_MESH_END] = (u32)g_probe_walk.mesh;
    out[PROBE_RENDER_PRIM_END] = (u32)g_disp_prim_cursor;
    out[PROBE_RENDER_OT] = probe_Hash(PROBE_OT, PROBE_OT_WORDS);
    out[PROBE_RENDER_TINY_OT] = probe_Hash(PROBE_TINY_OT, PROBE_TINY_OT_WORDS);
    out[PROBE_RENDER_PRIMS] = probe_Hash(PROBE_PRIMS, PROBE_PRIMS_WORDS);
    out[PROBE_RENDER_MESH] = probe_Hash((u32 *)PROBE_MESH, PROBE_MESH_WORDS);
    out[PROBE_RENDER_WORK] = probe_Hash((u32 *)PROBE_WORK, PROBE_WORK_WORDS);
    out[PROBE_RENDER_SCRATCH] = probe_Hash(PROBE_SCRATCH, PROBE_SCRATCH_WORDS);
    out[PROBE_RENDER_SKIN_COLORS] =
        probe_Hash((u32 *)g_render_skin_colors, PROBE_SKIN_COLORS);
}

/* ---- the routines of the handler tables ---- */

/* How a routine's records are made: their size, where their corners are
 * and how many, and where it reads the corners' depths. A routine whose
 * primitives are in the mesh may be given a group of none. */
typedef struct {
    void (*routine)();
    u8 size;
    u8 corners_at;
    u8 corner_count;
    u8 skin_depths; /* 1: the body's depths; 2: and the body's colours */
    u8 may_be_empty;
} ProbeEmit;

/* clang-format off */
#define PROBE_EMITS(X)                                                         \
    X(render_EmitFT3,            0x18, 0x0C, 3, 0, 0)                          \
    X(render_EmitFT4,            0x18, 0x0E, 4, 0, 0)                          \
    X(render_EmitGT3,            0x24, 0x0C, 3, 0, 0)                          \
    X(render_EmitGT4,            0x2C, 0x10, 4, 0, 0)                          \
    X(render_EmitPackedFT3,      0x18, 0x0C, 3, 0, 0)                          \
    X(render_EmitPackedFT4,      0x18, 0x0E, 4, 0, 0)                          \
    X(render_EmitPackedGT3,      0x24, 0x0C, 3, 0, 0)                          \
    X(render_EmitPackedGT4,      0x2C, 0x10, 4, 0, 0)                          \
    X(render_EmitUnlitFT3,       0x10, 0x0C, 3, 0, 0)                          \
    X(render_EmitUnlitFT4,       0x14, 0x0E, 4, 0, 0)                          \
    X(render_EmitTwoSidedFT3,    0x18, 0x0C, 3, 0, 0)                          \
    X(render_EmitTwoSidedFT4,    0x18, 0x0E, 4, 0, 0)                          \
    X(render_EmitTwoSidedGT3,    0x24, 0x0C, 3, 0, 0)                          \
    X(render_EmitTwoSidedGT4,    0x2C, 0x10, 4, 0, 0)                          \
    X(render_EmitTintedFT3,      0x18, 0x0C, 3, 0, 0)                          \
    X(render_EmitTintedFT4,      0x18, 0x0E, 4, 0, 0)                          \
    X(render_EmitBlackF3,        0x04, 0x00, 3, 0, 0)                          \
    X(render_EmitBlackF4,        0x04, 0x00, 4, 0, 0)                          \
    X(render_EmitOutlineFT3,     0x18, 0x0C, 3, 0, 0)                          \
    X(render_EmitOutlineFT4,     0x18, 0x0E, 4, 0, 0)                          \
    X(render_EmitOutlineGT3,     0x24, 0x0C, 3, 0, 0)                          \
    X(render_EmitOutlineGT4,     0x2C, 0x10, 4, 0, 0)                          \
    X(render_EmitSkinOutlineFT3, 0x14, 0x0C, 3, 1, 0)                          \
    X(render_EmitSkinOutlineFT4, 0x18, 0x10, 4, 1, 0)                          \
    X(render_EmitSkinOutlineGT3, 0x14, 0x0C, 3, 1, 0)                          \
    X(render_EmitSkinOutlineGT4, 0x18, 0x10, 4, 1, 0)                          \
    X(render_EmitSkinFT3,        0x14, 0x0C, 3, 2, 0)                          \
    X(render_EmitSkinFT4,        0x18, 0x10, 4, 2, 0)                          \
    X(render_EmitSkinGT3,        0x14, 0x0C, 3, 2, 0)                          \
    X(render_EmitSkinGT4,        0x18, 0x10, 4, 2, 0)                          \
    X(render_EmitF3,             0x38, 0x28, 3, 0, 1)                          \
    X(render_EmitModeF4,         0x50, 0x30, 4, 0, 1)                          \
    X(render_EmitTinyF3,         0x38, 0x28, 3, 0, 1)                          \
    X(render_EmitTinyF4,         0x40, 0x30, 4, 0, 1)
/* clang-format on */

/* The routines no header declares: nothing of the game's C calls them. */
extern void render_EmitF3();
extern void render_EmitModeF4();

#define PROBE_EMIT_ENTRY(routine, size, corners_at, corner_count, skin, empty) \
    {routine, size, corners_at, corner_count, skin, empty},
static const ProbeEmit g_probe_emits[] = {PROBE_EMITS(PROBE_EMIT_ENTRY)};
#define PROBE_EMIT_COUNT                                                       \
    ((s32)(sizeof(g_probe_emits) / sizeof(g_probe_emits[0])))

/* Where render_EmitModeF4's record has the number it adds to a depth; it
 * adds with an instruction that traps, so the number is kept small. */
#define PROBE_MODE_F4_SIZE 0x50
#define PROBE_MODE_F4_BIAS 0x34

/* A group of one routine's records, and the routine on it. In one case of
 * sixteen the outline's colour is set first, as the game sets it. */
static void probe_DoEmit(const ProbeEmit *emit, u32 *state, u32 *out) {
    u32 pick = probe_Random(state);
    s32 count = 1 + (s32)(pick % 6);
    u8 *record = PROBE_MESH;
    s32 i;

    if (emit->may_be_empty && (pick & 0xF00) == 0) {
        count = 0;
    }
    probe_MakeNode(state);
    probe_MakePoints(
        state, emit->skin_depths ? PROBE_SKIN_DEPTHS : PROBE_DEPTHS);
    if (emit->may_be_empty) {
        /* the cursor is moved back by a group of none */
        record += 0x40;
        g_probe_walk.mesh = record;
    }
    for (i = 0; i < count; i++) {
        u8 *next =
            (emit->skin_depths == 2)
                ? probe_MakeSkinRecord(state, record, emit->size,
                                       emit->corners_at, emit->corner_count)
                : probe_MakeRecord(state, record, emit->size, emit->corners_at,
                                   emit->corner_count);

        if (emit->size == PROBE_MODE_F4_SIZE) {
            *(s32 *)(record + PROBE_MODE_F4_BIAS) =
                (s32)probe_Signed(state, 14);
        }
        record = next;
    }
    /* In one case of four the cutoff is the ordering-table entry of the
     * first face, or the one after: a face is left out or not by where
     * exactly the cutoff is. */
    if ((pick & 0x30000) == 0 && count != 0) {
        const u8 *corners = g_probe_walk.mesh + emit->corners_at;
        const s16 *depths =
            emit->skin_depths ? PROBE_SKIN_DEPTHS : PROBE_DEPTHS;
        s32 depth = 0;

        for (i = 0; i < emit->corner_count; i++) {
            depth += depths[corners[i]];
        }
        if (emit->corner_count == 4) {
            depth >>= 2;
        } else {
            depth = ((g_probe_depth_scale * depth) >> 12) << 2;
        }
        PROBE_SCRATCH[PROBE_SCRATCH_CUTOFF] =
            (u32)render_DepthToOt(depth) + ((pick >> 18) & 1);
    }
    if ((pick & 0xF000) == 0) {
        render_SetOutlineColor((s32)(probe_Random(state) & 0xFFFFFF));
    }
    PROBE_HANDLER(emit->routine, count);
    render_SetOutlineColor(0x808080);
}

/* ---- projecting a part's vertices ---- */

/* The listings' two routines, which no header declares. */
#ifndef PROBE_HOST
extern void render_ProjectVertices();
extern void render_ProjectPackedVertices();
extern void render_DrawGroups();
#endif

static void probe_DoProjectVertices(u32 *state, s32 wild, u32 *out) {
    s32 count = 1 + (s32)(probe_Random(state) % 12);
    s16 *vertices = (s16 *)PROBE_MESH;
    s32 i;

    for (i = 0; i < count * 12; i++) {
        vertices[i] = (s16)probe_Signed(state, wild ? 16 : 12);
    }
    PROBE_PROJECT(render_ProjectVertices, count, PROBE_POINTS, PROBE_DEPTHS);
}

static void probe_DoProjectPackedVertices(u32 *state, s32 wild, u32 *out) {
    s32 count = 1 + (s32)(probe_Random(state) % 30);
    s16 *vertices = (s16 *)PROBE_MESH;
    s32 i;

    /* a mesh's packed vertices start on a word or between two */
    if (probe_Random(state) & 1) {
        vertices++;
        g_probe_walk.mesh += 2;
    }
    for (i = 0; i < count * 3 + 8; i++) {
        vertices[i] = (s16)probe_Signed(state, wild ? 16 : 12);
    }
    PROBE_PROJECT(
        render_ProjectPackedVertices, count, PROBE_POINTS, PROBE_DEPTHS);
}

/* ---- the groups of a part ---- */

/* The types that have a routine in each of the three tables the walk
 * over the queue chooses, and the record each takes: its size, where its
 * corners are, how many. */
typedef struct {
    u8 type;
    u8 size;
    u8 corners_at;
    u8 corner_count;
} ProbeType;

/* clang-format off */
static const ProbeType g_probe_usual_types[] = {
    {4, 0x18, 0x0C, 3},  {5, 0x10, 0x0C, 3},  {6, 0x18, 0x0C, 3},
    {12, 0x18, 0x0E, 4}, {13, 0x14, 0x0E, 4}, {14, 0x18, 0x0E, 4},
    {20, 0x24, 0x0C, 3}, {22, 0x24, 0x0C, 3},
    {28, 0x2C, 0x10, 4}, {30, 0x2C, 0x10, 4},
    /* the packed table ends here */
    {32, 0x04, 0x00, 3}, {33, 0x04, 0x00, 4},
    {42, 0x18, 0x0C, 3}, {43, 0x18, 0x0E, 4},
    {48, 0x18, 0x0C, 3}, {50, 0x18, 0x0C, 3},
    {56, 0x18, 0x0E, 4}, {58, 0x18, 0x0E, 4},
    {64, 0x24, 0x0C, 3}, {66, 0x24, 0x0C, 3},
    {72, 0x2C, 0x10, 4}, {74, 0x2C, 0x10, 4},
};
static const ProbeType g_probe_tiny_types[] = {
    {0, 0x38, 0x28, 3}, {8, 0x40, 0x30, 4},
};
/* clang-format on */
#define PROBE_USUAL_TYPE_COUNT 22
#define PROBE_PACKED_TYPE_COUNT 10
#define PROBE_TINY_TYPE_COUNT 2

/* The tables themselves (render.c). */
extern void (*g_render_packed_handlers[])();
extern void (*g_render_tiny_handlers[])();

/* Groups for one of the three tables, ended by a count of none, at
 * `mesh`; returns the end. In a long case every type of the table is
 * there. */
static u8 *probe_MakeGroups(u32 *state, u8 *mesh, s32 table, s32 every_type) {
    const ProbeType *types = g_probe_usual_types;
    s32 type_count = PROBE_USUAL_TYPE_COUNT;
    s32 groups = 1 + (s32)(probe_Random(state) & 3);
    s32 i;
    s32 j;

    if (table == 1) {
        type_count = PROBE_PACKED_TYPE_COUNT;
    } else if (table == 2) {
        types = g_probe_tiny_types;
        type_count = PROBE_TINY_TYPE_COUNT;
    }
    if (every_type) {
        groups = type_count;
    }
    for (i = 0; i < groups; i++) {
        const ProbeType *type =
            &types[every_type ? i : (s32)(probe_Random(state) % type_count)];
        s32 count = 1 + (s32)(probe_Random(state) % (every_type ? 2 : 3));

        ((u16 *)mesh)[0] = (u16)count;
        ((u16 *)mesh)[1] = type->type;
        mesh += 4;
        for (j = 0; j < count; j++) {
            mesh = probe_MakeRecord(
                state, mesh, type->size, type->corners_at, type->corner_count);
        }
    }
    ((u16 *)mesh)[0] = 0;
    return mesh + 2;
}

static void probe_DoDrawGroups(u32 *state, s32 wild, u32 *out) {
    u32 pick = probe_Random(state);
    s32 table = (s32)(pick % 3);

    probe_MakeNode(state);
    probe_MakePoints(state, PROBE_DEPTHS);
    probe_MakeGroups(state, PROBE_MESH, table, (pick & 0x700) == 0);
    g_probe_walk.handlers = (table == 0)   ? (void *)g_render_handlers
                            : (table == 1) ? (void *)g_render_packed_handlers
                                           : (void *)g_render_tiny_handlers;
    PROBE_DRAW_GROUPS();
}

/* ---- a fighter's body ---- */

static u8 *probe_PutHalf(u8 *mesh, s32 value) {
    *(s16 *)mesh = (s16)value;
    return mesh + 2;
}

static u8 *probe_PutVertex(u32 *state, u8 *mesh, s32 bits) {
    s32 i;

    for (i = 0; i < 3; i++) {
        mesh = probe_PutHalf(mesh, (s32)probe_Signed(state, bits));
    }
    return mesh;
}

/* The bones: thirteen matrices, and the list of pointers to them. */
#define PROBE_BONES 13
#define PROBE_BONE_LIST ((MATRIX **)(PROBE_WORK + 0x200))
#define PROBE_BONE_MATRICES ((MATRIX *)(PROBE_WORK + 0x240))
#define PROBE_CAMERA ((SceneNode *)(PROBE_WORK + 0x100))
#define PROBE_LIGHTS ((LightSetup *)(PROBE_WORK + 0x400))
#define PROBE_VIEW ((MATRIX *)(PROBE_WORK + 0x480))

static void probe_MakeBones(u32 *state) {
    s32 i;

    for (i = 0; i < PROBE_BONES; i++) {
        probe_MakeBone(state, &PROBE_BONE_MATRICES[i]);
        PROBE_BONE_LIST[i] = &PROBE_BONE_MATRICES[i];
    }
}

/* A light setup: any bytes, and a light matrix of the game's sizes. */
static void probe_MakeLights(u32 *state, LightSetup *setup) {
    probe_FillWords((u32 *)setup, sizeof(LightSetup) / 4, probe_Random(state));
    probe_MakeBone(state, &setup->lmat);
    probe_MakeBone(state, &setup->cmat);
}

/* The list render_LightSkin reads, at `mesh`: runs of normals, each a
 * bone, a count and the normals, and a bone of -1 after the last. */
static u8 *probe_MakeSkinNormals(u32 *state, u8 *mesh, s32 wild) {
    s32 runs = 1 + (s32)(probe_Random(state) % 3);
    s32 i;
    s32 j;

    for (i = 0; i < runs; i++) {
        s32 count = (s32)(probe_Random(state) % 8);

        mesh = probe_PutHalf(mesh, (s32)(probe_Random(state) % PROBE_BONES));
        mesh = probe_PutHalf(mesh, count);
        for (j = 0; j < count; j++) {
            mesh = probe_PutVertex(state, mesh, wild ? 16 : 13);
        }
    }
    return probe_PutHalf(mesh, -1);
}

static void probe_DoLightSkin(u32 *state, s32 wild, u32 *out) {
    probe_MakeBones(state);
    probe_MakeLights(state, PROBE_LIGHTS);
    g_light_current = PROBE_LIGHTS;
    probe_MakeSkinNormals(state, PROBE_MESH, wild);
    out[PROBE_RENDER_VALUE] =
        (u32)render_LightSkin(PROBE_MESH, PROBE_BONE_LIST);
}

/* The types a body's groups have, in the table of a body and in the
 * table of one drawn as outlines (render.c). */
/* clang-format off */
static const ProbeType g_probe_skin_types[] = {
    {38, 0x14, 0x0C, 3}, {39, 0x18, 0x10, 4},
    {40, 0x14, 0x0C, 3}, {41, 0x18, 0x10, 4},
    /* the table of a body ends here */
    {4, 0x18, 0x0C, 3},  {6, 0x18, 0x0C, 3},
    {12, 0x18, 0x0E, 4}, {14, 0x18, 0x0E, 4},
    {20, 0x24, 0x0C, 3}, {22, 0x24, 0x0C, 3},
    {28, 0x2C, 0x10, 4}, {30, 0x2C, 0x10, 4},
};
/* clang-format on */
#define PROBE_SKIN_TYPE_COUNT 4
#define PROBE_SKIN_OUTLINE_TYPE_COUNT 12

/* A table of the probe's own for the body's walk called by itself: the
 * four routines of a body, and the outline routines in the places the
 * game's table of outlines has them. */
static void (*g_probe_skin_handlers[42])();

static void probe_MakeSkinHandlers(void) {
    g_probe_skin_handlers[38] = render_EmitSkinFT3;
    g_probe_skin_handlers[39] = render_EmitSkinFT4;
    g_probe_skin_handlers[40] = render_EmitSkinGT3;
    g_probe_skin_handlers[41] = render_EmitSkinGT4;
    g_probe_skin_handlers[4] = render_EmitOutlineFT3;
    g_probe_skin_handlers[6] = render_EmitOutlineFT3;
    g_probe_skin_handlers[12] = render_EmitOutlineFT4;
    g_probe_skin_handlers[14] = render_EmitOutlineFT4;
    g_probe_skin_handlers[20] = render_EmitOutlineGT3;
    g_probe_skin_handlers[22] = render_EmitOutlineGT3;
    g_probe_skin_handlers[28] = render_EmitOutlineGT4;
    g_probe_skin_handlers[30] = render_EmitOutlineGT4;
}

/* A whole skinned mesh at the start of the mesh's room: a halfword, the
 * normals, and thirteen sections, each its five lists of vertices, a
 * halfword, its groups from the next whole word, and (but after the
 * last) the points carried over and where the next section's points go.
 * `type_count` is how many of g_probe_skin_types its groups may be. */
static void probe_MakeSkinnedMesh(u32 *state, s32 wild, s32 type_count) {
    u8 *mesh = PROBE_MESH;
    s32 section;
    s32 count;
    s32 i;
    s32 j;

    mesh = probe_PutHalf(mesh, 0x8000 | (s32)(probe_Random(state) & 0xFF));
    mesh = probe_MakeSkinNormals(state, mesh, wild);
    for (section = 0; section < PROBE_BONES; section++) {
        if (section != 0) {
            mesh = probe_PutHalf(mesh, (s32)probe_Random(state));
        }
        /* turned by the bone, and moved */
        count = (s32)(probe_Random(state) % 3);
        mesh = probe_PutHalf(mesh, count);
        for (i = 0; i < count; i++) {
            mesh = probe_PutVertex(state, mesh, 12);
            mesh = probe_PutVertex(state, mesh, 10);
        }
        /* built on a point */
        count = (s32)(probe_Random(state) % 3);
        mesh = probe_PutHalf(mesh, count);
        for (i = 0; i < count; i++) {
            mesh = probe_PutHalf(
                mesh, (s32)(probe_Random(state) % PROBE_POINT_COUNT));
            mesh = probe_PutVertex(state, mesh, 12);
            mesh = probe_PutVertex(state, mesh, 10);
        }
        /* turned by the bone and the view */
        count = (s32)(probe_Random(state) % 3);
        mesh = probe_PutHalf(mesh, count);
        for (i = 0; i < count; i++) {
            mesh = probe_PutVertex(state, mesh, 12);
        }
        /* projected from a point */
        count = (s32)(probe_Random(state) % 3);
        mesh = probe_PutHalf(mesh, count);
        for (i = 0; i < count; i++) {
            mesh = probe_PutHalf(
                mesh, (s32)(probe_Random(state) % PROBE_POINT_COUNT));
            mesh = probe_PutVertex(state, mesh, 12);
        }
        /* projected, three at a time */
        count = (s32)(probe_Random(state) % 6);
        mesh = probe_PutHalf(mesh, count);
        for (i = 0; i < count; i++) {
            mesh = probe_PutVertex(state, mesh, 12);
        }
        mesh = probe_PutHalf(mesh, (s32)probe_Random(state));
        mesh += (u32)mesh & 3;
        /* the groups */
        count = (s32)(probe_Random(state) % 3);
        for (i = 0; i < count; i++) {
            const ProbeType *type =
                &g_probe_skin_types[probe_Random(state) % type_count];
            s32 records = 1 + (s32)(probe_Random(state) & 1);

            mesh = probe_PutHalf(mesh, records);
            mesh = probe_PutHalf(mesh, type->type);
            for (j = 0; j < records; j++) {
                mesh = (type->type >= 38)
                           ? probe_MakeSkinRecord(
                                 state, mesh, type->size, type->corners_at,
                                 type->corner_count)
                           : probe_MakeRecord(
                                 state, mesh, type->size, type->corners_at,
                                 type->corner_count);
            }
        }
        mesh = probe_PutHalf(mesh, 0);
        if (section == PROBE_BONES - 1) {
            break;
        }
        /* the points carried over */
        count = (s32)(probe_Random(state) % 3);
        for (i = 0; i < count; i++) {
            s32 run = 1 + (s32)(probe_Random(state) % 3);

            mesh = probe_PutHalf(
                mesh,
                run - 1 +
                    (s32)(probe_Random(state) % (PROBE_POINT_COUNT - run + 1)));
            mesh = probe_PutHalf(
                mesh, PROBE_POINT_COUNT + (s32)(probe_Random(state) % 60));
            mesh = probe_PutHalf(mesh, run);
        }
        mesh = probe_PutHalf(mesh, 0xFFFF);
        mesh = probe_PutHalf(
            mesh, PROBE_POINT_COUNT + (s32)(probe_Random(state) % 40));
    }
}

/* What a body's walk works from: the camera, the bones, the lights. */
static void probe_MakeSkinScene(u32 *state) {
    s32 i;

    probe_MakeNode(state);
    probe_MakePoints(state, PROBE_SKIN_DEPTHS);
    probe_FillWords(
        (u32 *)PROBE_CAMERA, sizeof(SceneNode) / 4, probe_Random(state));
    for (i = 0; i < 3; i++) {
        PROBE_CAMERA->world.t[i] = (s32)probe_Signed(state, 15);
    }
    g_node_camera_root_ptr = PROBE_CAMERA;
    probe_MakeBones(state);
}

/* The walk by itself, with the probe's table of handlers. */
static void probe_DoDrawSkinnedMesh(u32 *state, s32 wild, u32 *out) {
    u32 pick = probe_Random(state);

    probe_MakeSkinScene(state);
    probe_MakeLights(state, PROBE_LIGHTS);
    g_light_current = PROBE_LIGHTS;
    probe_MakeBone(state, PROBE_VIEW);
    probe_MakeSkinHandlers();
    PROBE_SCRATCH[PROBE_SCRATCH_HANDLERS] = (u32)g_probe_skin_handlers;
    probe_MakeSkinnedMesh(
        state, wild,
        (pick & 1) ? PROBE_SKIN_TYPE_COUNT : PROBE_SKIN_OUTLINE_TYPE_COUNT);
    PROBE_DRAW_SKINNED_MESH(
        PROBE_BONE_LIST, PROBE_MESH, PROBE_VIEW, (pick >> 1) & 3);
}

/* And as the game calls it: through render_DrawSkinned (render.c), which
 * sets the view up and chooses the game's own table by the node. That
 * function returns nothing, so where the mesh cursor was left is not
 * compared here. */
static void probe_DoDrawSkinned(u32 *state, s32 wild, u32 *out) {
    u32 pick = probe_Random(state);
    s32 setup = (s32)(pick & 7);
    s32 outline = (s32)((pick >> 3) & 1);

    probe_MakeSkinScene(state);
    PROBE_WORK[1] = (u8)outline;
    probe_MakeLights(state, &g_light_setups[setup]);
    probe_MakeBone(state, &g_render_view_matrix);
    probe_MakeSkinnedMesh(
        state, wild,
        outline ? PROBE_SKIN_OUTLINE_TYPE_COUNT : PROBE_SKIN_TYPE_COUNT);
    render_DrawSkinned(
        (s32)PROBE_BONE_LIST, (s32)PROBE_MESH, (s16)setup, PROBE_WORK);
    g_probe_walk.mesh = NULL;
}

/* ---- the list ---- */

/* The routines of the handler tables first, one case function for all of
 * them; then the others, a function each. */
#define PROBE_RENDER_OTHERS(X)                                                 \
    X(ProjectVertices)                                                         \
    X(ProjectPackedVertices)                                                   \
    X(DrawGroups)                                                              \
    X(LightSkin)                                                               \
    X(DrawSkinnedMesh)                                                         \
    X(DrawSkinned)

typedef void (*ProbeRenderRoutine)(u32 *state, s32 wild, u32 *out);

#define PROBE_RENDER_OTHER_ENTRY(name) probe_Do##name,
static const ProbeRenderRoutine g_probe_render_others[] = {
    PROBE_RENDER_OTHERS(PROBE_RENDER_OTHER_ENTRY)};
#define PROBE_RENDER_OTHER_COUNT                                               \
    ((s32)(sizeof(g_probe_render_others) / sizeof(g_probe_render_others[0])))
#define PROBE_RENDER_COUNT (PROBE_EMIT_COUNT + PROBE_RENDER_OTHER_COUNT)

/* The 64 registers of case `vector` of routine `routine` (cases.h; the
 * renderer's routines are numbered after the others). */
static void probe_MakeRenderInput(s32 routine, s32 vector, u32 *in) {
    probe_MakeInput(0x100 + routine, vector, in);
    g_probe_depth_scale = (s16)in[PROBE_CONTROL + 29];
}

/* The case itself, after the registers are loaded: the memory is made
 * up, the routine runs, and `out` gets PROBE_RENDER_RESULTS words. Half
 * the cases have numbers of the game's sizes, half anything. */
static void probe_RunRender(s32 routine, s32 vector, u32 *out) {
    u32 state =
        ((u32)(routine + 1) * 0xB5297A4D) ^ ((u32)(vector + 1) * 0x68E31DA4);
    s32 wild = (vector & 3) >= 2;
    s32 i;

    if (state == 0) {
        state = 1;
    }
    for (i = 0; i < 4; i++) {
        probe_Random(&state);
    }
    for (i = 0; i < PROBE_RENDER_RESULTS; i++) {
        out[i] = 0;
    }
    probe_RenderBegin(&state, wild);
    if (routine < PROBE_EMIT_COUNT) {
        probe_DoEmit(&g_probe_emits[routine], &state, out);
    } else {
        g_probe_render_others[routine - PROBE_EMIT_COUNT](&state, wild, out);
    }
    probe_RenderEnd(out);
}

#endif /* PROBE_RENDER_CASES_H */
