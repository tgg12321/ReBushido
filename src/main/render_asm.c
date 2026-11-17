/* The renderer's inner loops, which were written in assembly and are kept as
 * assembly (render.h; render.c is the C side).
 *
 * render_DrawQueue walks the frame's queue of nodes. For each it sets the
 * GTE up for the node's kind (the view and the node's matrix, its light
 * setup), then goes through the node's mesh: each part's vertices are
 * projected onto the scratchpad, and each group of primitives in the part
 * is handed to the routine its type has in the handler table in use. Those
 * routines, render_Emit..., test each primitive against the screen, light
 * it, work out its depth and link it into the ordering table. Stage tiles,
 * cloth and a fighter's body have their own walks (render_DrawStageTiles,
 * render_DrawCloth, render_DrawSkinnedMesh).
 *
 * The routines do not follow the C calling convention among themselves: the
 * mesh cursor stays in $s0, the scratchpad in $s1, the handler table in $s4
 * and so on, as the comments in the listings (asm/funcs/) say. Only the
 * ones render.h declares with arguments can be called from C.
 *
 * That is the first sign that these were written by hand and are not what
 * the compiler made of C; the top of math_asm.c names the others, and the
 * line above each routine here says which it shows. Many open with a frame
 * in the compiler's pattern ($ra, $fp, then $s7 down), but it is 0x38 bytes
 * whatever is saved, and saves $ra in routines that call nothing: the
 * compiler does neither. */
#define INCLUDE_ASM_USE_MACRO_INC 1
#include "common.h"
#include "include_asm.h"
#include "bb2.h"

/* ---- the tables the walks go through ----
 * Code addresses in them are symbols: the jump labels (.L<addr>) of the
 * listings, and functions. The __asm__ on each line below is not an
 * instruction: it gives C the name of a label. */

#if !RB_PORT
/* (The port has no listings and so no labels: its two switches are
 * render_SetupNode and render_DrawPart, further down.) */
extern u8 L8004A5D0[] __asm__(".L8004A5D0");
extern u8 L8004A614[] __asm__(".L8004A614");
extern u8 L8004A644[] __asm__(".L8004A644");
extern u8 L8004A694[] __asm__(".L8004A694");
extern u8 L8004A6F0[] __asm__(".L8004A6F0");
extern u8 L8004A730[] __asm__(".L8004A730");
extern u8 L8004A990[] __asm__(".L8004A990");
extern u8 L8004ABD8[] __asm__(".L8004ABD8");
extern u8 L8004AE0C[] __asm__(".L8004AE0C");
extern u8 L8004AF84[] __asm__(".L8004AF84");
extern u8 L8004B0C8[] __asm__(".L8004B0C8");
extern u8 L8004B0E4[] __asm__(".L8004B0E4");
extern u8 L8004B27C[] __asm__(".L8004B27C");
extern u8 L8004B4D8[] __asm__(".L8004B4D8");

/* Where render_DrawQueue goes for each kind of node (enum NodeKind): the
 * setup of that kind, or straight on to the next node for a kind that is
 * not drawn. */
void * const g_render_kind_jumps[16] = {
    L8004A990, L8004ABD8, L8004A5D0, L8004AE0C, L8004A5D0, L8004A5D0,
    L8004A5D0, L8004A5D0, L8004A5D0, L8004A5D0, L8004AF84, L8004A5D0,
    L8004B0C8, L8004B0E4, L8004B4D8, L8004B27C,
};

/* Where it goes for each part of a mesh, by the format of the part's
 * vertices: SVECTORs; packed; the tiny model's; format 3, which no mesh
 * has; and packed, drawn with the packed handlers. */
void * const g_render_format_jumps[5] = {
    L8004A614, L8004A644, L8004A694, L8004A6F0, L8004A730,
};
#endif

/* The routines that draw a stage tile's primitives: triangles and quads
 * (the primitive's type, less its low three bits). */
void * const g_render_tile_handlers[4] = {
    render_EmitSplitFT3,
    render_EmitSplitFT4,
    sys_Trap,
    sys_Trap,
};

/* The routines that draw cloth, by the same number: the four textured
 * primitives, and then the four again for a cloth drawn as outlines. */
void (* const g_render_cloth_handlers[8])() = {
    render_EmitClothFT3,   render_EmitClothFT4,   render_EmitClothGT3,
    render_EmitClothGT4,   render_EmitOutlineFT3, render_EmitOutlineFT4,
    render_EmitOutlineGT3, render_EmitOutlineGT4,
};

/* ---- the port's C: what the routines share ----
 * docs/PORT.md, "The renderer in C". The listings pass their state from
 * one routine to the next in registers; here it is g_render_walk
 * (render.h). What the listings keep on the scratchpad stays there. */
#if RB_PORT
RenderWalk g_render_walk;

/* On the scratchpad after render.h's words: the screen points of the
 * part's vertices, a word each (y above x), and their depths, a halfword
 * each; a fighter's body keeps its depths lower down. */
#define RENDER_POINTS ((u32 *)(SCRATCHPAD + 0x20))
#define RENDER_DEPTHS ((s16 *)(SCRATCHPAD + 0x2B4))
#define RENDER_SKIN_DEPTHS ((s16 *)(SCRATCHPAD + 0x244))

/* The vertices are projected three at a time. */
#define RENDER_RUN 3
/* A packed vertex: three halfwords. */
#define PACKED_VERTEX_SIZE 6

/* In a screen point: the sign of y, and the sign of x. A face whose
 * corners all have one of them is off the top or the left of the
 * screen. */
#define RENDER_ABOVE 0x80000000U
#define RENDER_LEFT 0x8000
/* A screen point's x, which is its low halfword with a sign. */
#define RENDER_POINT_X(point) ((s32)(s16)(u16)(point))

/* What turns a normal round for a face seen from behind. */
#define RENDER_FLIP 0xFFFFFFFFU

/* A triangle's depth comes from the GTE's average, which is a quarter of
 * the scale a quad's plain average has. */
#define RENDER_AVERAGE_SHIFT 2
/* Some routines draw nothing as deep as this. */
#define RENDER_DEPTH_END 0x10000

/* render_DepthToOt's numbers (render.c). */
#define RENDER_OCTAVE_SHIFT 11
#define RENDER_OCTAVE (1 << RENDER_OCTAVE_SHIFT)
/* The console shifts by the low five bits of a count. */
#define RENDER_SHIFT_MASK 31

/* An ordering-table link: the next primitive's address in 24 bits, under
 * the primitive's length in words. */
#define RENDER_LINK_MASK 0xFFFFFF
#define RENDER_LINK_LENGTH(words) ((u32)(words) << 24)
/* The length of one of Sony's primitives: its words but the link. */
#define RENDER_PRIM_LENGTH(type) (sizeof(type) / sizeof(u32) - 1)

/* The colour words of the primitives that are not lit: a code above a
 * colour. */
#define RENDER_UNLIT_FT3 0x25808080
#define RENDER_UNLIT_FT4 0x2D808080
#define RENDER_BLACK_F3 0x20000000
#define RENDER_BLACK_F4 0x28000000
/* An outline is a line through a triangle's corners and back to the
 * first, or two lines of two segments for a quad; a word of fives ends a
 * line. */
#define RENDER_OUTLINE_LINE4 0x4C000000
#define RENDER_OUTLINE_LINE3 0x48000000
#define RENDER_LINE_END 0x55555555

/* A colour's byte times sixteen, as the GTE takes one. */
#define RENDER_COLOR_FIELD 0xFF0

/* The tiny model's ordering table has 256 entries, and its depths start a
 * thousand in. */
#define TINY_DEPTH_BIAS 1000
#define RENDER_TINY_OT_INDEX(index) (((index) >> 4) & 0xFF)

/* A word or halfword at a byte offset of a record; and a word or halfword
 * of a primitive, by the member it starts at. */
#define RENDER_WORD(record, offset) (*(u32 *)((u8 *)(record) + (offset)))
#define RENDER_HALF(record, offset) (*(u16 *)((u8 *)(record) + (offset)))
#define RENDER_SHALF(record, offset) (*(s16 *)((u8 *)(record) + (offset)))
#define RENDER_WORD_OF(member) (*(u32 *)&(member))
#define RENDER_HALF_OF(member) (*(u16 *)&(member))

/* A compact record (the meshes of stages and objects), in bytes. It
 * starts with what the primitive's texture words are made of; the first
 * word is also loaded as the GTE's colour and code. */
#define EMIT_TPAGE 0x00
#define EMIT_CLUT 0x04
#define EMIT_UV0 0x06
#define EMIT_UV1 0x08
#define EMIT_UV2 0x0A
#define EMIT_UV3 0x0C      /* quads */
#define EMIT_CORNERS3 0x0C /* triangles: a byte a corner */
#define EMIT_FT3_NORMAL 0x10
#define EMIT_FT3_SIZE 0x18
#define EMIT_UNLIT_FT3_SIZE 0x10
#define EMIT_FT4_CORNERS 0x0E /* not on a word */
#define EMIT_FT4_NORMAL_Z 0x12
#define EMIT_FT4_NORMAL_XY 0x14
#define EMIT_FT4_SIZE 0x18
#define EMIT_UNLIT_FT4_SIZE 0x14
#define EMIT_GT3_NORMAL_XY0 0x10
#define EMIT_GT3_NORMAL_Z01 0x14 /* the first's z under the second's */
#define EMIT_GT3_NORMAL_XY1 0x18
#define EMIT_GT3_NORMAL_XY2 0x1C
#define EMIT_GT3_NORMAL_Z2 0x20
#define EMIT_GT3_SIZE 0x24
#define EMIT_GT4_CORNERS 0x10
#define EMIT_GT4_NORMAL_XY_FACE 0x14
#define EMIT_GT4_NORMAL_Z_FACE_0 0x18 /* the face's z under the first's */
#define EMIT_GT4_NORMAL_XY0 0x1C
#define EMIT_GT4_NORMAL_XY1 0x20
#define EMIT_GT4_NORMAL_Z12 0x24
#define EMIT_GT4_NORMAL_XY2 0x28
#define EMIT_GT4_SIZE 0x2C
#define EMIT_UV0_CLUT(record)                                                  \
    (RENDER_HALF(record, EMIT_UV0) | (RENDER_HALF(record, EMIT_CLUT) << 16))
#define EMIT_UV1_TPAGE(record)                                                 \
    (RENDER_HALF(record, EMIT_UV1) | (RENDER_HALF(record, EMIT_TPAGE) << 16))

/* A record of a mesh that holds its own primitives: two of each, one for
 * each frame of the double buffer, and then these. */
#define EMIT_OWN_F3_CORNERS (2 * sizeof(POLY_F3))
#define EMIT_OWN_F3_NORMAL (EMIT_OWN_F3_CORNERS + 0x04)
#define EMIT_OWN_F3_COLOR (EMIT_OWN_F3_CORNERS + 0x0C)
#define EMIT_OWN_F3_SIZE (EMIT_OWN_F3_CORNERS + 0x10)
#define EMIT_OWN_F4_CORNERS (2 * sizeof(POLY_F4))
#define EMIT_OWN_F4_NORMAL (EMIT_OWN_F4_CORNERS + 0x04)
#define EMIT_OWN_F4_COLOR (EMIT_OWN_F4_CORNERS + 0x0C)
#define EMIT_OWN_F4_SIZE (EMIT_OWN_F4_CORNERS + 0x10)
/* render_EmitModeF4's: a number for the depth after the corners, and the
 * two second primitives, three words each with their link. */
#define EMIT_OWN_MODE_F4_CORNERS (2 * sizeof(POLY_F4))
#define EMIT_OWN_MODE_F4_DEPTH_BIAS (EMIT_OWN_MODE_F4_CORNERS + 0x04)
#define EMIT_OWN_MODE_F4_MODES (EMIT_OWN_MODE_F4_CORNERS + 0x08)
#define EMIT_OWN_MODE_SIZE 0x0C
#define EMIT_OWN_MODE_LENGTH 2
#define EMIT_OWN_MODE_F4_SIZE (EMIT_OWN_MODE_F4_MODES + 2 * EMIT_OWN_MODE_SIZE)

/* A record of a fighter's body, in bytes: the primitive's texture words
 * as they are (the third is loaded as the GTE's colour and code too), the
 * corners, and where the corners' colours are in g_render_skin_colors,
 * ten bits each; a quad's fourth is a halfword of its own. */
#define EMIT_SKIN_UV0_CLUT 0x00
#define EMIT_SKIN_UV1_TPAGE 0x04
#define EMIT_SKIN_UV2 0x08
#define EMIT_SKIN_UV3 0x0C
#define EMIT_SKIN_COLOR3 0x0E
#define EMIT_SKIN_CORNERS3 0x0C
#define EMIT_SKIN_COLORS3 0x10
#define EMIT_SKIN_COLORS4 0x14
#define SKIN_COLOR(colors, n) (((colors) >> (10 * (n))) & 0x3FF)
#define RENDER_SKIN_COLOR(index) (*(u32 *)&g_render_skin_colors[index])
/* The codes of the body's Gouraud primitives, above the first colour. */
#define RENDER_SKIN_GT3 0x34000000
#define RENDER_SKIN_GT4 0x3C000000
#define EMIT_SKIN_CORNERS4 0x10
#define EMIT_SKIN_FT3_SIZE 0x14
#define EMIT_SKIN_FT4_SIZE 0x18
#define EMIT_SKIN_GT3_SIZE 0x14
#define EMIT_SKIN_GT4_SIZE 0x18

/* What render_DrawSkinnedMesh keeps on the scratchpad for a section: the
 * bone's matrix scaled down; the view times that, with where the bone is
 * from the camera as its translation; and that place turned into the
 * view. */
#define RENDER_SKIN_BONE ((MATRIX *)(SCRATCHPAD + 0x360))
#define RENDER_SKIN_COMBINED ((MATRIX *)(SCRATCHPAD + 0x374))
#define RENDER_SKIN_TURNED ((s32 *)(SCRATCHPAD + 0x394))
/* The entries of a section's lists of vertices, in bytes: a packed vertex
 * and three numbers it is moved by; the point one is built on, a vertex
 * and three numbers; a point and a vertex. And a run of points carried
 * over to the sections to come: from, to, how many; 0xFFFF ends them. */
#define SKIN_MOVED_X 6
#define SKIN_MOVED_Y 8
#define SKIN_MOVED_Z 10
#define SKIN_MOVED_SIZE 12
#define SKIN_BUILT_X 8
#define SKIN_BUILT_Y 10
#define SKIN_BUILT_Z 12
#define SKIN_BUILT_SIZE 14
#define SKIN_FROM_POINT_SIZE 8
#define SKIN_CARRY_TO 2
#define SKIN_CARRY_COUNT 4
#define SKIN_CARRY_SIZE 6
#define SKIN_CARRY_END 0xFFFF

/* A cloth's node is followed by a pointer to its points, each this many
 * bytes and beginning with where it is, in units of 1/128. The table of
 * the cloth's routines has the four that draw it as outlines after the
 * four that draw it. */
#define CLOTH_NODE_POINTS 0x60
#define CLOTH_POINT_SIZE 0x40
#define CLOTH_POINT_SHIFT 7
#define CLOTH_OUTLINE_HANDLERS 4
/* A group's routine is found by its type less its low three bits. */
#define CLOTH_TYPE_SHIFT 3

/* ---- the walk over the queue ---- */

/* What the setup of a node keeps on the scratchpad: where the node is
 * from the camera, scaled down, as a vector; the view times the node's
 * matrix (for a shadow, the matrix that flattens it); and the lights
 * turned into the node's space. */
#define RENDER_NODE_PLACE ((s16 *)(SCRATCHPAD + 0x20))
#define RENDER_NODE_MATRIX ((MATRIX *)(SCRATCHPAD + 0x28))
#define RENDER_NODE_LIGHTS ((MATRIX *)(SCRATCHPAD + 0x48))
/* The word after a SceneNode in the records that begin with one: a
 * shadow's ground height, a body's list of bone matrices. */
#define RENDER_NODE_EXTRA 0x58
/* A node's coordinates are scaled down by two bits, by one if it is
 * nearer the camera than the second of these any way, by none if nearer
 * than the first; a body has its own two. */
#define RENDER_NEAR 0x4A00
#define RENDER_FAR 0xA500
#define RENDER_SKIN_NEAR 0x5A00
#define RENDER_SKIN_FAR 0xB500
/* The formats of a part's vertices past the two of mesh.h: the tiny
 * model's; none, which no mesh has; and packed, drawn with the packed
 * table. */
#define RENDER_FORMAT_TINY 2
#define RENDER_FORMAT_NONE 3
#define RENDER_FORMAT_PACKED 4
/* Where the outline routines start in the usual table. */
#define RENDER_OUTLINE_HANDLERS 44
/* The middle of the screen the scene is projected about, and the tiny
 * model's. */
#define RENDER_CENTER_X 320
#define RENDER_CENTER_Y 120
#define TINY_CENTER_X 80
#define TINY_CENTER_Y 60

/* ---- stage tiles ---- */

/* What the tiles' routines keep on the scratchpad. The walk over the
 * tiles: the corners' depths; the view as it is (four words and a
 * halfword) and scaled down with its second row halved (nine halfwords);
 * the part's vertices; where the camera is; the renderer's options; how
 * far a depth is shifted down; the tiles' table of mesh pointers. */
#define RENDER_TILE_DEPTHS ((s16 *)(SCRATCHPAD + 0x110))
#define TILE_VIEW ((u32 *)(SCRATCHPAD + 0x188))
#define TILE_SCALED_VIEW ((s16 *)(SCRATCHPAD + 0x19C))
#define TILE_VERTICES (*(PTR32(s16 *) *)(SCRATCHPAD + 0x1B0))
#define TILE_CAMERA ((s32 *)(SCRATCHPAD + 0x1B4))
#define TILE_OPTIONS (*(s32 *)(SCRATCHPAD + 0x1C0))
#define TILE_DEPTH_SHIFT (*(s16 *)(SCRATCHPAD + 0x1C6))
#define TILE_MESHES (*(s32 *)(SCRATCHPAD + 0x1C8))
/* The routines that draw a group: the top half of a quad's colour word;
 * the face's colour table and texture page; its colour once lit; how
 * many faces are left; the vertices; the ordering-table entry a cut face
 * is linked at; the tile's flags, whose low byte is how often to cut and
 * whose next has the blending bit; the corners' texture points; and the
 * levels of the cutting, which start four bytes on for a quad. */
#define TILE_CODE (*(u16 *)(SCRATCHPAD + 0x1C4))
#define TILE_CLUT (*(u16 *)(SCRATCHPAD + 0x1CC))
#define TILE_TPAGE (*(u16 *)(SCRATCHPAD + 0x1CE))
#define TILE_COLOR (*(u32 *)(SCRATCHPAD + 0x1D0))
#define TILE_LEFT (*(s32 *)(SCRATCHPAD + 0x1D4))
#define TILE_SPLIT_VERTICES (*(PTR32(s16 *) *)(SCRATCHPAD + 0x1D8))
#define TILE_ENTRY (*(PTR32(u32 *) *)(SCRATCHPAD + 0x1DC))
#define TILE_FLAGS (*(s16 *)(SCRATCHPAD + 0x1E0))
#define TILE_CUTS (*(s8 *)(SCRATCHPAD + 0x1E0))
#define TILE_BLEND (*(s8 *)(SCRATCHPAD + 0x1E1))
#define TILE_UV ((u16 *)(SCRATCHPAD + 0x1E2))
#define TILE_FRAMES_FT3 ((TileFrame *)(SCRATCHPAD + 0x210))
#define TILE_FRAMES_FT4 ((TileFrame *)(SCRATCHPAD + 0x214))

/* A tile in the queue, in bytes: its kind and no flags in a halfword (a
 * tile is not a node), the block and entry of its mesh, its flags, and
 * where it is on the ground. */
#define RENDER_KIND_TILE 12
#define TILE_BLOCK 2
#define TILE_MESH 4
#define TILE_FLAGS_AT 6
#define TILE_X 8
#define TILE_Z 12
/* The tiles are lit by this light setup, and their meshes are of this
 * block. */
#define TILE_LIGHT_SETUP 4
#define TILE_MESH_BLOCK 7
/* A group's routine is found by its type less its low three bits. */
#define TILE_TYPE_SHIFT 3

/* The third byte of a tile's record: bit 0 sorts a quad a thousand
 * farther off, and the two bits above it take cuts away from the tile's
 * (0: the face is never cut). */
#define EMIT_TILE_BITS 2
#define TILE_FAR_BIT 1
#define TILE_FAR_BIAS 1000
#define TILE_CUT_FIELD(bits) (((bits) >> 1) & 3)
/* The blending bit of a primitive's code, in its colour word. */
#define TILE_BLEND_SHIFT 25
/* A face is drawn only if a corner is above this line of the screen. */
#define TILE_SCREEN_HEIGHT 240
#define RENDER_HALF_SIGN 0x8000
/* The renderer's options, as the tiles read them: the low two bits say
 * how a face's texture points are scaled down (not at all; by the tile's
 * cuts; by the face's size on the screen; by both), and the next colours
 * the cut faces by how often they were cut. */
#define TILE_OPTION_SHRINK 3
#define TILE_SHRINK_BY_CUTS 1
#define TILE_SHRINK_BY_SIZE 2
#define TILE_OPTION_SHOW_CUTS 4
/* The sizes on the screen under which a triangle's texture is taken
 * smaller, twice and once; and a quad's. */
#define TILE_FT3_SMALLEST 0x200
#define TILE_FT3_SMALL 0x300
#define TILE_FT4_SMALLEST 0x400
#define TILE_FT4_SMALL 0x700
/* The colours of a cut face, with the option that shows the cuts. */
#define TILE_CUT_ONCE 0xFFFFFF00
#define TILE_CUT_MORE 0xFFFF4000
/* The v of a texture point. */
#define TILE_V_MASK 0xFF00

/* A corner of a face being cut: where it is and its texture point. */
typedef struct {
    s16 x;
    s16 y;
    s16 z;
    u16 uv;
} TileCorner;

/* A level of the cutting, on the scratchpad as the listing has it: the
 * listing's return address, which the C leaves alone; how many cuts are
 * still to make; the corners. A triangle's fourth is a spare. */
typedef struct {
    u32 return_address;
    s16 cuts_left;
    s16 pad06;
    TileCorner corner[4];
} TileFrame;

/* What the cutting keeps in registers: where the next primitive is
 * built, the ordering table, and the link the next primitive gets, which
 * is the last one built. */
typedef struct {
    u32 *prim;
    u32 *ot;
    u32 chain;
} TileSplit;

/* A routine of g_render_tile_handlers. */
typedef s16 *(*TileHandler)(s32 count, s16 *vertices, s16 *records, s32 flags);

/* Defined further down. */
static void render_SplitFT3(TileSplit *split, TileFrame *frame, s32 cuts);
static void render_SplitFT4(TileSplit *split, TileFrame *frame, s32 cuts);
static void render_SplitMidpoint(
    const TileCorner *first, const TileCorner *second, TileCorner *middle);

/* In a record that begins with a SceneNode: how much of its light a tinted
 * face keeps, ONE for all of it. */
#define RENDER_NODE_TINT 0x60

/* A record names its corners in a word, a byte each: which of the
 * projected points each is. */
#define RENDER_CORNER(corners, n) (((corners) >> (8 * (n))) & 0xFF)

/* A face as the routines look at it: its corners' screen points, which of
 * the projected points they are, and the points' common bits. */
typedef struct {
    u32 xy[4];
    s32 index[4];
    u32 all;
    /* how many of those are this face's: the wider picture's own test
     * looks at each corner, where the console's looks only at `all`
     * (render_FaceOffScreen) */
    s32 count;
} RenderFace;

/* Three projected points and their depths from the GTE to the
 * scratchpad. */
static void render_StoreProjected(u32 *points, s16 *depths) {
    s32 i;

    port_GteStoreData(PORT_GTE_SXY0, points, RENDER_RUN);
    for (i = 0; i < RENDER_RUN; i++) {
        depths[i] = (s16)port_GteReadData(PORT_GTE_SZ1 + i);
    }
}

/* No translation in the GTE. */
static void render_ClearTranslation(void) {
    port_GteWriteControl(PORT_GTE_TRX, 0);
    port_GteWriteControl(PORT_GTE_TRY, 0);
    port_GteWriteControl(PORT_GTE_TRZ, 0);
}

/* A packed vertex of a fighter's body into one of the GTE's three
 * vectors, by its first register. */
static void render_LoadSkinVertex(s32 first, const u8 *vertex) {
    port_GteWriteData(
        first, RENDER_HALF(vertex, 0) | (RENDER_SHALF(vertex, 2) << 16));
    port_GteWriteData(first + 1, (u32)RENDER_SHALF(vertex, 4));
}

/* A colour of the body's table into the GTE's vector, each of its three
 * bytes times sixteen. */
static void render_LoadSkinColor(s32 index) {
    u32 color = RENDER_SKIN_COLOR(index);

    port_GteWriteData(PORT_GTE_IR1, (color << 4) & RENDER_COLOR_FIELD);
    port_GteWriteData(PORT_GTE_IR2, (color >> 4) & RENDER_COLOR_FIELD);
    port_GteWriteData(PORT_GTE_IR3, (color >> 12) & RENDER_COLOR_FIELD);
}

/* Looks a face's corners up among the projected points, a byte of
 * `corners` each, and has the GTE work out which way the first three
 * turn. */
static void render_LoadFace(RenderFace *face, u32 corners, s32 corner_count) {
    s32 i;

    face->all = RENDER_FLIP;
    face->count = corner_count;
    for (i = 0; i < corner_count; i++) {
        face->index[i] = RENDER_CORNER(corners, i);
        face->xy[i] = RENDER_POINTS[face->index[i]];
        face->all &= face->xy[i];
    }
    /* the remastered renderer's side channel: which points these
     * corners are, so that the primitive this face becomes can be drawn
     * where the projection really put it, and where in the mesh this
     * face is, which is how the primitive is found again in the next
     * frame (port_remaster.h) */
    port_RemasterNoteGroup((PortAddress)g_render_walk.mesh);
    port_RemasterNoteFace(
        (PortAddress)RENDER_POINTS, face->index, corner_count);
    port_GteLoadData(PORT_GTE_SXY0, face->xy, 3);
    port_GteCommand(PORT_GTE_CMD_NCLIP);
}

/* Whether every corner is above the screen, or every corner left of it.
 *
 * The console's test is the sign bits of the packed corners and nothing
 * more: its screen begins at x 0, so a corner left of that is off the
 * picture. A wider remastered picture reaches further left than the
 * console's screen, and a face every corner of which is off the console's
 * left edge may still be seen in the new band: there the test is against
 * the picture's own edge, which is `port_RemasterLeftMargin` pixels
 * further out (port_remaster.h; docs/PORT.md, "A wider picture"). The
 * margin is 0 with the original picture or the console's shape, and then
 * this is the console's own test. Nothing to the right or the bottom is
 * ever culled here, so a wider picture needs nothing of them. */
static s32 render_FaceOffScreen(const RenderFace *face) {
    s32 margin = port_RemasterLeftMargin();

    if (margin != 0) {
        s32 i;

        if ((face->all & RENDER_ABOVE) != 0) {
            return TRUE;
        }
        for (i = 0; i < face->count; i++) {
            if (RENDER_POINT_X(face->xy[i]) >= -margin) {
                return FALSE;
            }
        }
        return TRUE;
    }
    return (face->all & (RENDER_ABOVE | RENDER_LEFT)) != 0;
}

/* Whether the face render_LoadFace loaded is seen from its front. */
static s32 render_FaceFront(void) {
    return (s32)port_GteReadData(PORT_GTE_MAC0) < 0;
}

/* A triangle's depth: the GTE's average of its corners'. */
static s32 render_FaceDepth3(const RenderFace *face, const s16 *depths) {
    s32 i;

    for (i = 0; i < 3; i++) {
        port_GteWriteData(PORT_GTE_SZ1 + i, (u32)depths[face->index[i]]);
    }
    port_GteCommand(PORT_GTE_CMD_AVSZ3);
    return (s32)port_GteReadData(PORT_GTE_OTZ);
}

/* A quad's: the plain average. */
static s32 render_FaceDepth4(const RenderFace *face, const s16 *depths) {
    return (depths[face->index[0]] + depths[face->index[1]] +
            depths[face->index[2]] + depths[face->index[3]]) >>
           2;
}

/* The ordering-table index of a depth: render_DepthToOt's sum (render.c),
 * with the console's shifts. */
static s32 render_OtIndexShifted(u32 depth, s32 shift) {
    u32 scaled = depth >> (shift & RENDER_SHIFT_MASK);
    u32 octave = scaled >> RENDER_OCTAVE_SHIFT;
    s32 fraction =
        (s32)(scaled & (RENDER_OCTAVE - 1)) - (RENDER_OT_FARTHEST + 1);

    return (fraction >> (octave & RENDER_SHIFT_MASK)) & RENDER_OT_FARTHEST;
}

/* The same, scaled down as the mesh being drawn is. */
static s32 render_OtIndex(u32 depth) {
    return render_OtIndexShifted(depth, RENDER_DEPTH_SHIFT);
}

/* Links a primitive of `length` words in at an entry of an ordering
 * table, ahead of what is there. */
static void render_LinkEntry(u32 *entry, void *prim, u32 length) {
    u32 next = *entry;

    *entry = (u32)prim & RENDER_LINK_MASK;
    *(u32 *)prim = next | RENDER_LINK_LENGTH(length);
    /* the remastered renderer's side channel: this primitive is the
     * face render_LoadFace last looked up (port_remaster.h) */
    port_RemasterAttachFace((PortAddress)prim);
}

/* The same at entry `index` of the table being drawn into. */
static void render_LinkPrim(s32 index, void *prim, u32 length) {
#if RB_REMASTER_VIEW
    /* the farthest slot a frame used, for the report of every limit the
     * drawing path has: a depth beyond the table's reach is clamped to
     * its last slot, where primitives no longer sort (port_remaster.h) */
    port_RemasterNoteOtSlot(index);
#endif
    render_LinkEntry(&((u32 *)ADDR(RENDER_OT))[index], prim, length);
}

/* A light setup's ambient colour into the GTE: its three bytes, each
 * times sixteen. */
static void render_LoadBackColor(const LightSetup *lights) {
    u32 color = RENDER_WORD(lights->back, 0);

    port_GteWriteControl(PORT_GTE_RBK, (color << 4) & RENDER_COLOR_FIELD);
    port_GteWriteControl(PORT_GTE_GBK, (color >> 4) & RENDER_COLOR_FIELD);
    port_GteWriteControl(PORT_GTE_BBK, (color >> 12) & RENDER_COLOR_FIELD);
}

/* The vector the GTE has just turned becomes its translation, with y
 * halved: the frame is half height. */
static void render_LoadHalfHeightTranslation(void) {
    port_GteWriteControl(PORT_GTE_TRX, port_GteReadData(PORT_GTE_IR1));
    port_GteWriteControl(
        PORT_GTE_TRY, (s32)port_GteReadData(PORT_GTE_IR2) >> 1);
    port_GteWriteControl(PORT_GTE_TRZ, port_GteReadData(PORT_GTE_IR3));
}

/* How many times a tile's face has its texture points halved, by the
 * renderer's options: `area` is its size on the screen, under `smallest`
 * twice and under `small` once. */
static s32 render_TileShrink(s32 area, s32 smallest, s32 small) {
    s32 by_size = (area < smallest) + (area < small);

    switch (TILE_OPTIONS & TILE_OPTION_SHRINK) {
    case 0:
        return 0;
    case TILE_SHRINK_BY_CUTS:
        return 2 - TILE_CUTS;
    case TILE_SHRINK_BY_SIZE:
        return by_size;
    default:
        return (TILE_CUTS < by_size) ? by_size - TILE_CUTS : 0;
    }
}

/* A texture point with u and v each halved `shrink` times. */
static u32 render_ShrinkUv(u32 uv, s32 shrink) {
    u32 mask;

    if (shrink == 0) {
        return uv;
    }
    mask = 0xFFU >> (shrink & RENDER_SHIFT_MASK);
    mask |= mask << 8;
    return (uv >> (shrink & RENDER_SHIFT_MASK)) & mask;
}

/* A primitive of the cutting finished: the lit colour, and the link to
 * the one built before it. */
static void render_ChainTile(TileSplit *split, u32 length) {
    u32 *prim = split->prim;

    /* the remastered renderer's side channel: the next piece of the
     * tile's record this is, by which the picture between two frames
     * finds it again (port_remaster.h). A tile's primitive is chained
     * here and not through render_LinkEntry, so this is where it is
     * named. */
    port_RemasterAttachFace((PortAddress)prim);
    prim[0] = split->chain;
    prim[1] = TILE_COLOR;
    split->chain = ((u32)prim & RENDER_LINK_MASK) | RENDER_LINK_LENGTH(length);
    split->prim = prim + length + 1;
}

/* One face of a tile, a triangle or a quad: drawn if it faces the camera,
 * is on the screen and between the cutoff and the fog's end; whole, or
 * cut. */
static void render_EmitTile(TileSplit *split, u8 *record, s32 quad) {
    s32 corner_count = quad ? 4 : 3;
    TileFrame *frame = quad ? TILE_FRAMES_FT4 : TILE_FRAMES_FT3;
    u32 length =
        quad ? RENDER_PRIM_LENGTH(POLY_FT4) : RENDER_PRIM_LENGTH(POLY_FT3);
    /* a triangle's members are where a quad's first three are */
    POLY_FT4 *prim = (POLY_FT4 *)split->prim;
    u32 color = RENDER_WORD(record, 0);
    RenderFace face;
    s16 *vertex;
    u32 *entry;
    s32 area;
    s32 depth;
    s32 index;
    s32 shrink;
    s32 cuts;
    s32 above;
    s32 i;

    color |= (u32)(TILE_BLEND & 1) << TILE_BLEND_SHIFT;
    port_GteWriteData(PORT_GTE_RGBC, color);
    if (quad) {
        TILE_CODE = (u16)(color >> 16);
    }
    render_LoadFace(
        &face, RENDER_WORD(record, quad ? EMIT_FT4_CORNERS : EMIT_CORNERS3),
        corner_count);
    if (render_FaceOffScreen(&face) || !render_FaceFront()) {
        return;
    }
    area = -(s32)port_GteReadData(PORT_GTE_MAC0);
    /* some corner above the bottom of the screen; the deepest corner */
    above = 0;
    depth = RENDER_TILE_DEPTHS[face.index[0]];
    for (i = 0; i < corner_count; i++) {
        above |= ((s32)face.xy[i] >> 16) - TILE_SCREEN_HEIGHT;
        if (depth < RENDER_TILE_DEPTHS[face.index[i]]) {
            depth = RENDER_TILE_DEPTHS[face.index[i]];
        }
    }
    if (!(above & RENDER_HALF_SIGN)) {
        return;
    }
    if (quad && (record[EMIT_TILE_BITS] & TILE_FAR_BIT)) {
        depth += TILE_FAR_BIAS;
    }
    index = render_OtIndexShifted(depth, TILE_DEPTH_SHIFT);
    if (RENDER_FOG_OT_INDEX < index || index < RENDER_OT_CUTOFF) {
        return;
    }
    if (quad) {
        port_GteWriteData(PORT_GTE_VZ0, RENDER_HALF(record, EMIT_FT4_NORMAL_Z));
        port_GteLoadData(PORT_GTE_VXY0, record + EMIT_FT4_NORMAL_XY, 1);
    } else {
        port_GteLoadData(PORT_GTE_VXY0, record + EMIT_FT3_NORMAL, 2);
    }
    TILE_CLUT = RENDER_HALF(record, EMIT_CLUT);
    TILE_TPAGE = RENDER_HALF(record, EMIT_TPAGE);
    port_GteCommand(PORT_GTE_CMD_NCS);
    shrink = quad ? render_TileShrink(area, TILE_FT4_SMALLEST, TILE_FT4_SMALL)
                  : render_TileShrink(area, TILE_FT3_SMALLEST, TILE_FT3_SMALL);
    for (i = 0; i < corner_count; i++) {
        TILE_UV[i] = (u16)render_ShrinkUv(
            RENDER_HALF(record, EMIT_UV0 + i * sizeof(u16)), shrink);
    }
    cuts = 0;
    if (TILE_CUT_FIELD(record[EMIT_TILE_BITS]) != 0) {
        cuts = TILE_CUTS - (TILE_CUT_FIELD(record[EMIT_TILE_BITS]) - 1);
    }
    if (cuts <= 0) {
        RENDER_WORD_OF(prim->x0) = face.xy[0];
        RENDER_WORD_OF(prim->x1) = face.xy[1];
        RENDER_WORD_OF(prim->x2) = face.xy[2];
        RENDER_WORD_OF(prim->u0) = TILE_UV[0] | (TILE_CLUT << 16);
        RENDER_WORD_OF(prim->u1) = TILE_UV[1] | (TILE_TPAGE << 16);
        RENDER_HALF_OF(prim->u2) = TILE_UV[2];
        if (quad) {
            RENDER_WORD_OF(prim->x3) = face.xy[3];
            RENDER_HALF_OF(prim->u3) = TILE_UV[3];
        }
        port_GteStoreData(PORT_GTE_RGB2, &prim->r0, 1);
        render_LinkEntry(&split->ot[index], prim, length);
        split->prim += length + 1;
        return;
    }
    if (TILE_OPTIONS & TILE_OPTION_SHOW_CUTS) {
        color = (RENDER_WORD(record, 0) | RENDER_LINK_MASK) & TILE_CUT_ONCE;
        if (cuts != 1) {
            color &= TILE_CUT_MORE;
        }
        port_GteWriteData(PORT_GTE_RGB2, color);
    }
    entry = &split->ot[index];
    split->chain = *entry | RENDER_LINK_LENGTH(length);
    SET_PTR(TILE_ENTRY, entry);
    port_GteStoreData(PORT_GTE_RGB2, &TILE_COLOR, 1);
    for (i = 0; i < corner_count; i++) {
        vertex = PTR(TILE_SPLIT_VERTICES) + 3 * face.index[i];
        frame->corner[i].x = vertex[0];
        frame->corner[i].y = vertex[1];
        frame->corner[i].z = vertex[2];
        frame->corner[i].uv = TILE_UV[i];
    }
    if (quad) {
        render_SplitFT4(split, frame, cuts);
    } else {
        render_SplitFT3(split, frame, cuts);
    }
    *PTR(TILE_ENTRY) = split->chain & RENDER_LINK_MASK;
}

/* render_EmitSplitFT3 and render_EmitSplitFT4: a group of a tile's
 * faces. */
static s16 *render_EmitTiles(
    s32 count, s16 *vertices, s16 *records, s32 flags, s32 quad) {
    u8 *record = (u8 *)records;
    TileSplit split;
    s32 left = count - 1;

    TILE_FLAGS = (s16)flags;
    SET_PTR(TILE_SPLIT_VERTICES, vertices);
    split.prim = g_disp_prim_cursor;
    split.ot = (u32 *)ADDR(RENDER_OT);
    do {
        TILE_LEFT = left;
        render_EmitTile(&split, record, quad);
        record += EMIT_FT3_SIZE;
    } while (left-- != 0);
    g_disp_prim_cursor = split.prim;
    return (s16 *)record;
}

/* What the eight outline routines have in common, for triangles: the
 * edges as one line in the outline colour. `stride` is a record's size;
 * with `loads_color` each record's first word is left in the GTE. */
static void render_EmitOutline3(
    s32 count, s32 stride, s32 loads_color, const s16 *depths) {
    u8 *record = g_render_walk.mesh;
    LINE_F4 *prim = (LINE_F4 *)g_disp_prim_cursor;
    RenderFace face;
    s32 depth;

    if (count == 0) {
        return;
    }
    for (; count != 0; count--, record += stride) {
        if (loads_color) {
            port_GteLoadData(PORT_GTE_RGBC, record, 1);
        }
        render_LoadFace(&face, RENDER_WORD(record, EMIT_CORNERS3), 3);
        if (render_FaceOffScreen(&face) || !render_FaceFront()) {
            continue;
        }
        depth = render_FaceDepth3(&face, depths);
        RENDER_WORD_OF(prim->x0) = face.xy[0];
        RENDER_WORD_OF(prim->x3) = face.xy[0];
        RENDER_WORD_OF(prim->x1) = face.xy[1];
        RENDER_WORD_OF(prim->x2) = face.xy[2];
        if (depth <= 0) {
            continue;
        }
        render_LinkPrim(render_OtIndex(depth << RENDER_AVERAGE_SHIFT), prim,
                        RENDER_PRIM_LENGTH(LINE_F4));
        RENDER_WORD_OF(prim->r0) =
            RENDER_OUTLINE_LINE4 | g_render_outline_color;
        prim->pad = RENDER_LINE_END;
        prim++;
    }
    g_render_walk.mesh = record;
    g_disp_prim_cursor = (u32 *)prim;
}

/* And for quads: two lines, each round one half of the quad. The corners
 * are a word at `corners_offset` of a record. */
static void render_EmitOutline4(s32 count, s32 stride, s32 corners_offset,
                                s32 loads_color, const s16 *depths) {
    u8 *record = g_render_walk.mesh;
    LINE_F3 *prim = (LINE_F3 *)g_disp_prim_cursor;
    RenderFace face;
    s32 depth;
    s32 index;

    if (count == 0) {
        return;
    }
    for (; count != 0; count--, record += stride) {
        if (loads_color) {
            port_GteLoadData(PORT_GTE_RGBC, record, 1);
        }
        render_LoadFace(&face, RENDER_WORD(record, corners_offset), 4);
        if (render_FaceOffScreen(&face) || !render_FaceFront()) {
            continue;
        }
        depth = render_FaceDepth4(&face, depths);
        if (depth <= 0) {
            continue;
        }
        RENDER_WORD_OF(prim[0].x0) = face.xy[1];
        RENDER_WORD_OF(prim[0].x1) = face.xy[0];
        RENDER_WORD_OF(prim[0].x2) = face.xy[2];
        RENDER_WORD_OF(prim[1].x0) = face.xy[2];
        RENDER_WORD_OF(prim[1].x1) = face.xy[3];
        RENDER_WORD_OF(prim[1].x2) = face.xy[1];
        RENDER_WORD_OF(prim[0].r0) =
            RENDER_OUTLINE_LINE3 | g_render_outline_color;
        prim[0].pad = RENDER_LINE_END;
        RENDER_WORD_OF(prim[1].r0) =
            RENDER_OUTLINE_LINE3 | g_render_outline_color;
        prim[1].pad = RENDER_LINE_END;
        index = render_OtIndex(depth);
        render_LinkPrim(index, &prim[0], RENDER_PRIM_LENGTH(LINE_F3));
        render_LinkPrim(index, &prim[1], RENDER_PRIM_LENGTH(LINE_F3));
        prim += 2;
    }
    g_render_walk.mesh = record;
    g_disp_prim_cursor = (u32 *)prim;
}

/* The depth a face drawn from both sides is sorted by. Cloth is sorted
 * eighty nearer than it is where it faces the camera, and seventy-nine
 * farther where it faces away: the number, turned as the normal is. */
#define CLOTH_DEPTH_BIAS 0xFFFFFFB0U

static u32 render_SidedDepth(s32 depth, u32 flip, s32 cloth) {
    if (cloth) {
        return (u32)depth + (flip ^ CLOTH_DEPTH_BIAS);
    }
    return (u32)depth;
}

/* render_EmitTwoSidedFT3 and render_EmitClothFT3: flat textured
 * triangles drawn whichever way they face. */
static void render_EmitBothSidesFT3(s32 count, s32 cloth) {
    u8 *record = g_render_walk.mesh;
    POLY_FT3 *prim = (POLY_FT3 *)g_disp_prim_cursor;
    RenderFace face;
    s32 depth;
    u32 flip;

    if (count == 0) {
        return;
    }
    for (; count != 0; count--, record += EMIT_FT3_SIZE) {
        port_GteLoadData(PORT_GTE_RGBC, record, 1);
        render_LoadFace(&face, RENDER_WORD(record, EMIT_CORNERS3), 3);
        if (render_FaceOffScreen(&face)) {
            continue;
        }
        flip = render_FaceFront() ? 0 : RENDER_FLIP;
        depth = render_FaceDepth3(&face, RENDER_DEPTHS);
        RENDER_WORD_OF(prim->x0) = face.xy[0];
        RENDER_WORD_OF(prim->x1) = face.xy[1];
        RENDER_WORD_OF(prim->x2) = face.xy[2];
        if (depth <= 0 || (depth << RENDER_AVERAGE_SHIFT) >= RENDER_DEPTH_END) {
            continue;
        }
        port_GteWriteData(
            PORT_GTE_VXY0, RENDER_WORD(record, EMIT_FT3_NORMAL) ^ flip);
        port_GteWriteData(
            PORT_GTE_VZ0,
            RENDER_WORD(record, EMIT_FT3_NORMAL + sizeof(u32)) ^ flip);
        port_GteCommand(PORT_GTE_CMD_NCS);
        port_GteStoreData(PORT_GTE_RGB2, &prim->r0, 1);
        RENDER_WORD_OF(prim->u0) = EMIT_UV0_CLUT(record);
        RENDER_WORD_OF(prim->u1) = EMIT_UV1_TPAGE(record);
        RENDER_HALF_OF(prim->u2) = RENDER_HALF(record, EMIT_UV2);
        render_LinkPrim(render_OtIndex(render_SidedDepth(
                            depth << RENDER_AVERAGE_SHIFT, flip, cloth)),
                        prim, RENDER_PRIM_LENGTH(POLY_FT3));
        prim++;
    }
    g_render_walk.mesh = record;
    g_disp_prim_cursor = (u32 *)prim;
}

/* render_EmitTwoSidedFT4 and render_EmitClothFT4. */
static void render_EmitBothSidesFT4(s32 count, s32 cloth) {
    u8 *record = g_render_walk.mesh;
    POLY_FT4 *prim = (POLY_FT4 *)g_disp_prim_cursor;
    RenderFace face;
    s32 depth;
    u32 flip;

    if (count == 0) {
        return;
    }
    for (; count != 0; count--, record += EMIT_FT4_SIZE) {
        port_GteLoadData(PORT_GTE_RGBC, record, 1);
        render_LoadFace(&face, RENDER_WORD(record, EMIT_FT4_CORNERS), 4);
        if (render_FaceOffScreen(&face)) {
            continue;
        }
        flip = render_FaceFront() ? 0 : RENDER_FLIP;
        depth = render_FaceDepth4(&face, RENDER_DEPTHS);
        if (depth <= 0 || depth >= RENDER_DEPTH_END) {
            continue;
        }
        RENDER_WORD_OF(prim->x0) = face.xy[0];
        RENDER_WORD_OF(prim->x1) = face.xy[1];
        RENDER_WORD_OF(prim->x2) = face.xy[2];
        RENDER_WORD_OF(prim->x3) = face.xy[3];
        port_GteWriteData(
            PORT_GTE_VZ0, RENDER_HALF(record, EMIT_FT4_NORMAL_Z) ^ flip);
        port_GteWriteData(
            PORT_GTE_VXY0, RENDER_WORD(record, EMIT_FT4_NORMAL_XY) ^ flip);
        port_GteCommand(PORT_GTE_CMD_NCS);
        port_GteStoreData(PORT_GTE_RGB2, &prim->r0, 1);
        RENDER_WORD_OF(prim->u0) = EMIT_UV0_CLUT(record);
        RENDER_WORD_OF(prim->u1) = EMIT_UV1_TPAGE(record);
        RENDER_HALF_OF(prim->u2) = RENDER_HALF(record, EMIT_UV2);
        RENDER_HALF_OF(prim->u3) = RENDER_HALF(record, EMIT_UV3);
        render_LinkPrim(render_OtIndex(render_SidedDepth(depth, flip, cloth)),
                        prim, RENDER_PRIM_LENGTH(POLY_FT4));
        prim++;
    }
    g_render_walk.mesh = record;
    g_disp_prim_cursor = (u32 *)prim;
}

/* render_EmitTwoSidedGT3 and render_EmitClothGT3. */
static void render_EmitBothSidesGT3(s32 count, s32 cloth) {
    u8 *record = g_render_walk.mesh;
    POLY_GT3 *prim = (POLY_GT3 *)g_disp_prim_cursor;
    RenderFace face;
    s32 depth;
    u32 flip;
    u32 depths01; /* the first normal's z under the second's */

    if (count == 0) {
        return;
    }
    port_GteLoadData(PORT_GTE_RGBC, record, 1);
    for (; count != 0; count--, record += EMIT_GT3_SIZE) {
        render_LoadFace(&face, RENDER_WORD(record, EMIT_CORNERS3), 3);
        if (render_FaceOffScreen(&face)) {
            continue;
        }
        flip = render_FaceFront() ? 0 : RENDER_FLIP;
        depth = render_FaceDepth3(&face, RENDER_DEPTHS);
        RENDER_WORD_OF(prim->x0) = face.xy[0];
        RENDER_WORD_OF(prim->x1) = face.xy[1];
        RENDER_WORD_OF(prim->x2) = face.xy[2];
        if (depth <= 0 || (depth << RENDER_AVERAGE_SHIFT) >= RENDER_DEPTH_END) {
            continue;
        }
        depths01 = RENDER_WORD(record, EMIT_GT3_NORMAL_Z01) ^ flip;
        port_GteWriteData(
            PORT_GTE_VXY0, RENDER_WORD(record, EMIT_GT3_NORMAL_XY0) ^ flip);
        port_GteWriteData(PORT_GTE_VZ0, depths01);
        port_GteWriteData(
            PORT_GTE_VXY1, RENDER_WORD(record, EMIT_GT3_NORMAL_XY1) ^ flip);
        port_GteWriteData(PORT_GTE_VZ1, depths01 >> 16);
        port_GteWriteData(
            PORT_GTE_VXY2, RENDER_WORD(record, EMIT_GT3_NORMAL_XY2) ^ flip);
        port_GteWriteData(
            PORT_GTE_VZ2, RENDER_WORD(record, EMIT_GT3_NORMAL_Z2) ^ flip);
        port_GteCommand(PORT_GTE_CMD_NCT);
        RENDER_WORD_OF(prim->u0) = EMIT_UV0_CLUT(record);
        RENDER_WORD_OF(prim->u1) = EMIT_UV1_TPAGE(record);
        RENDER_HALF_OF(prim->u2) = RENDER_HALF(record, EMIT_UV2);
        render_LinkPrim(render_OtIndex(render_SidedDepth(
                            depth << RENDER_AVERAGE_SHIFT, flip, cloth)),
                        prim, RENDER_PRIM_LENGTH(POLY_GT3));
        port_GteStoreData(PORT_GTE_RGB0, &prim->r0, 1);
        port_GteStoreData(PORT_GTE_RGB1, &prim->r1, 1);
        port_GteStoreData(PORT_GTE_RGB2, &prim->r2, 1);
        prim++;
    }
    g_render_walk.mesh = record;
    g_disp_prim_cursor = (u32 *)prim;
}

/* render_EmitTwoSidedGT4 and render_EmitClothGT4. */
static void render_EmitBothSidesGT4(s32 count, s32 cloth) {
    u8 *record = g_render_walk.mesh;
    POLY_GT4 *prim = (POLY_GT4 *)g_disp_prim_cursor;
    RenderFace face;
    s32 depth;
    u32 flip;
    u32 depths; /* two normals' z, one under the other */

    if (count == 0) {
        return;
    }
    port_GteLoadData(PORT_GTE_RGBC, record, 1);
    for (; count != 0; count--, record += EMIT_GT4_SIZE) {
        render_LoadFace(&face, RENDER_WORD(record, EMIT_GT4_CORNERS), 4);
        if (render_FaceOffScreen(&face)) {
            continue;
        }
        flip = render_FaceFront() ? 0 : RENDER_FLIP;
        depth = render_FaceDepth4(&face, RENDER_DEPTHS);
        if (depth <= 0 || depth >= RENDER_DEPTH_END) {
            continue;
        }
        RENDER_WORD_OF(prim->x0) = face.xy[0];
        RENDER_WORD_OF(prim->x1) = face.xy[1];
        RENDER_WORD_OF(prim->x2) = face.xy[2];
        RENDER_WORD_OF(prim->x3) = face.xy[3];
        /* The face's own normal lights the first corner. */
        depths = RENDER_WORD(record, EMIT_GT4_NORMAL_Z_FACE_0) ^ flip;
        port_GteWriteData(
            PORT_GTE_VXY0, RENDER_WORD(record, EMIT_GT4_NORMAL_XY_FACE) ^ flip);
        port_GteWriteData(PORT_GTE_VZ0, depths);
        port_GteCommand(PORT_GTE_CMD_NCS);
        port_GteStoreData(PORT_GTE_RGB2, &prim->r0, 1);
        /* The other three have a normal each. */
        port_GteWriteData(
            PORT_GTE_VXY0, RENDER_WORD(record, EMIT_GT4_NORMAL_XY0));
        port_GteWriteData(PORT_GTE_VZ0, depths >> 16);
        depths = RENDER_WORD(record, EMIT_GT4_NORMAL_Z12) ^ flip;
        port_GteWriteData(
            PORT_GTE_VXY1, RENDER_WORD(record, EMIT_GT4_NORMAL_XY1) ^ flip);
        port_GteWriteData(PORT_GTE_VZ1, depths);
        port_GteWriteData(
            PORT_GTE_VXY2, RENDER_WORD(record, EMIT_GT4_NORMAL_XY2));
        port_GteWriteData(PORT_GTE_VZ2, depths >> 16);
        port_GteCommand(PORT_GTE_CMD_NCT);
        RENDER_WORD_OF(prim->u0) = EMIT_UV0_CLUT(record);
        RENDER_WORD_OF(prim->u1) = EMIT_UV1_TPAGE(record);
        RENDER_HALF_OF(prim->u2) = RENDER_HALF(record, EMIT_UV2);
        RENDER_HALF_OF(prim->u3) = RENDER_HALF(record, EMIT_UV3);
        render_LinkPrim(render_OtIndex(render_SidedDepth(depth, flip, cloth)),
                        prim, RENDER_PRIM_LENGTH(POLY_GT4));
        port_GteStoreData(PORT_GTE_RGB0, &prim->r1, 1);
        port_GteStoreData(PORT_GTE_RGB1, &prim->r2, 1);
        port_GteStoreData(PORT_GTE_RGB2, &prim->r3, 1);
        prim++;
    }
    g_render_walk.mesh = record;
    g_disp_prim_cursor = (u32 *)prim;
}

/* render_EmitF3 and render_EmitTinyF3: lit flat triangles whose
 * primitives are in the mesh, linked into the table `ot`; `tiny` is the
 * tiny model's way with a depth. */
static void render_EmitMeshF3(s32 count, u32 *ot, s32 tiny) {
    u8 *record = g_render_walk.mesh;
    POLY_F3 *prim;
    RenderFace face;
    s32 depth;
    s32 index;

    if (count == 0) {
        g_render_walk.mesh = record - 2 * sizeof(POLY_F3);
        return;
    }
    port_GteLoadData(
        PORT_GTE_RGBC,
        record + (RENDER_FRAME_MASK & sizeof(POLY_F3)) + sizeof(u32), 1);
    for (; count != 0; count--, record += EMIT_OWN_F3_SIZE) {
        prim = (POLY_F3 *)(record + (RENDER_FRAME_MASK & sizeof(POLY_F3)));
        render_LoadFace(&face, RENDER_WORD(record, EMIT_OWN_F3_CORNERS), 3);
        if (render_FaceOffScreen(&face) || !render_FaceFront()) {
            continue;
        }
        depth = render_FaceDepth3(&face, RENDER_DEPTHS);
        RENDER_WORD_OF(prim->x0) = face.xy[0];
        RENDER_WORD_OF(prim->x1) = face.xy[1];
        RENDER_WORD_OF(prim->x2) = face.xy[2];
        depth <<= RENDER_AVERAGE_SHIFT;
        if (depth <= 0 || depth >= RENDER_DEPTH_END) {
            continue;
        }
        port_GteLoadData(PORT_GTE_VXY0, record + EMIT_OWN_F3_NORMAL, 2);
        port_GteLoadData(PORT_GTE_RGBC, record + EMIT_OWN_F3_COLOR, 1);
        port_GteCommand(PORT_GTE_CMD_NCCS);
        if (tiny) {
            index =
                RENDER_TINY_OT_INDEX(render_OtIndex(depth - TINY_DEPTH_BIAS));
        } else {
            index = render_OtIndex(depth);
        }
        render_LinkEntry(&ot[index], prim, RENDER_PRIM_LENGTH(POLY_F3));
        port_GteStoreData(PORT_GTE_RGB2, &prim->r0, 1);
    }
    g_render_walk.mesh = record;
}
#endif /* RB_PORT */

/* ---- maths: the first routine of the file ---- */

/* The rotation matrix for three angles, turned about Z, then Y, then X
 * (math.h). */
/* Assembly: trapping (15); it saves $ra and nine registers, calls nothing and
 * uses five of them. */
#if RB_PORT
/* In C, step for step as the assembly has it: the sines and cosines
 * times each other in the GTE, three at a time, and the rest by the
 * processor. The GTE's registers are left as the assembly leaves them. */
void math_RotMatrixZYX(SVECTOR *angles, MATRIX *matrix) {
    s32 sin_x = g_math_sin_table[angles->vx & ANGLE_MASK];
    s32 sin_y = g_math_sin_table[angles->vy & ANGLE_MASK];
    s32 sin_z = g_math_sin_table[angles->vz & ANGLE_MASK];
    s32 cos_x = g_math_sin_table[(angles->vx + ANGLE_TURN / 4) & ANGLE_MASK];
    s32 cos_y = g_math_sin_table[(angles->vy + ANGLE_TURN / 4) & ANGLE_MASK];
    s32 cos_z = g_math_sin_table[(angles->vz + ANGLE_TURN / 4) & ANGLE_MASK];
    s32 by_cos_z[3]; /* sin x, cos x and cos y, each times cos z */
    s32 by_sin_z[3]; /* and times sin z */
    s32 sums[3];

    /* (sin x, cos x, cos y) * cos z */
    port_GteWriteData(PORT_GTE_IR0, (u32)cos_z);
    port_GteWriteData(PORT_GTE_IR1, (u32)sin_x);
    port_GteWriteData(PORT_GTE_IR2, (u32)cos_x);
    port_GteWriteData(PORT_GTE_IR3, (u32)cos_y);
    port_GteCommand(PORT_GTE_CMD_GPF12);
    port_GteStoreData(PORT_GTE_IR1, by_cos_z, 3);
    matrix->m[2][0] = (s16)-sin_y;
    matrix->m[0][0] = (s16)by_cos_z[2];

    /* (sin x, cos x, cos y) * sin z */
    port_GteWriteData(PORT_GTE_IR0, (u32)sin_z);
    port_GteWriteData(PORT_GTE_IR1, (u32)sin_x);
    port_GteWriteData(PORT_GTE_IR2, (u32)cos_x);
    port_GteWriteData(PORT_GTE_IR3, (u32)cos_y);
    port_GteCommand(PORT_GTE_CMD_GPF12);
    port_GteStoreData(PORT_GTE_IR1, by_sin_z, 3);
    matrix->m[2][1] = (s16)((sin_x * cos_y) >> 12);
    matrix->m[1][0] = (s16)by_sin_z[2];
    matrix->m[2][2] = (s16)((cos_x * cos_y) >> 12);

    /* (cos x cos z, -sin x cos z, sin x sin z) +
     * (sin x sin z, cos x sin z, cos x cos z) * sin y */
    port_GteWriteData(PORT_GTE_IR0, (u32)sin_y);
    port_GteWriteData(PORT_GTE_MAC1, (u32)by_cos_z[1]);
    port_GteWriteData(PORT_GTE_MAC2, (u32)-by_cos_z[0]);
    port_GteWriteData(PORT_GTE_MAC3, (u32)by_sin_z[0]);
    port_GteWriteData(PORT_GTE_IR3, (u32)by_cos_z[1]);
    port_GteCommand(PORT_GTE_CMD_GPL12);
    port_GteStoreData(PORT_GTE_IR1, sums, 3);
    matrix->m[1][1] = (s16)sums[0];
    matrix->m[1][2] = (s16)sums[1];
    matrix->m[0][2] = (s16)sums[2];
    matrix->m[0][1] = (s16)(((sin_y * by_cos_z[0]) >> 12) - by_sin_z[1]);
}
#else
INCLUDE_ASM("asm/funcs", math_RotMatrixZYX);
#endif

/* ---- the walk over the queue ---- */

/* Assembly: trapping; it jumps through the two tables above to labels in
 * render_DrawGroups, which jump back; what it calls shares its registers. */
#if RB_PORT
/* What render_DrawQueue is made of in C: the setup of each kind of node,
 * which the listing has after render_DrawGroups, and the drawing of a
 * part by the format of its vertices. */

/* Defined further down. */
static void render_ProjectVertices(s32 count, u32 *points, s16 *depths);
static void render_ProjectPackedVertices(s32 count, u32 *points, s16 *depths);
static void render_DrawGroups(void);
static void **render_DrawStageTiles(void **queue, u8 *tile, MATRIX *view);
static void render_DrawCloth(s32 scale_shift);

/* How far the node is from a place, each way, into `offset`; and the
 * three distances' bits together, which is as good as the largest for
 * telling how far to scale the node down. */
static s32 render_NodeOffset(
    const SceneNode *node, const s32 *from, s32 *offset) {
    s32 reach = 0;
    s32 i;

    for (i = 0; i < 3; i++) {
        offset[i] = node->world.t[i] - from[i];
        reach |= (offset[i] < 0) ? -offset[i] : offset[i];
    }
    return reach;
}

/* That offset scaled down, onto the scratchpad. */
static void render_StoreNodePlace(const s32 *offset, s32 shift) {
    s32 i;

    for (i = 0; i < 3; i++) {
        RENDER_NODE_PLACE[i] = (s16)(offset[i] >> (shift & RENDER_SHIFT_MASK));
    }
}

/* The node's place turned by a rotation, five words of it; the result
 * is left in the GTE's vector. */
static void render_TurnNodePlace(const void *rotation) {
    port_GteLoadControl(PORT_GTE_ROTATION, rotation, PORT_GTE_MATRIX_WORDS);
    render_ClearTranslation();
    port_GteLoadData(PORT_GTE_VXY0, RENDER_NODE_PLACE, 2);
    port_GteCommand(PORT_GTE_CMD_RTV0TR);
}

/* A matrix's nine numbers scaled down by `shift`, the second row by one
 * more (the frame is half height): what the vertices are projected
 * through. */
static void render_ScaleRotation(const s16 *matrix, s32 shift, s32 *scaled) {
    s32 i;

    for (i = 0; i < PORT_GTE_MATRIX_ELEMENTS; i++) {
        scaled[i] = matrix[i] >> ((shift + (i / 3 == 1)) & RENDER_SHIFT_MASK);
    }
}

/* That, into the GTE. */
static void render_LoadScaledRotation(const s16 *matrix, s32 shift) {
    s32 scaled[PORT_GTE_MATRIX_ELEMENTS];
    u32 words[PORT_GTE_MATRIX_WORDS];
    s32 i;

    render_ScaleRotation(matrix, shift, scaled);
    for (i = 0; i < PORT_GTE_MATRIX_WORDS - 1; i++) {
        words[i] = (scaled[2 * i] & 0xFFFF) | (scaled[2 * i + 1] << 16);
    }
    words[PORT_GTE_MATRIX_WORDS - 1] = scaled[PORT_GTE_MATRIX_ELEMENTS - 1];
    port_GteLoadControl(PORT_GTE_ROTATION, words, PORT_GTE_MATRIX_WORDS);
}

/* And into nine halfwords of memory. */
static void render_StoreScaledRotation(const s16 *matrix, s32 shift, s16 *out) {
    s32 scaled[PORT_GTE_MATRIX_ELEMENTS];
    s32 i;

    render_ScaleRotation(matrix, shift, scaled);
    for (i = 0; i < PORT_GTE_MATRIX_ELEMENTS; i++) {
        out[i] = (s16)scaled[i];
    }
}

/* The node's light setup, and its lights' colours into the GTE. */
static LightSetup *render_NodeLights(const SceneNode *node) {
    LightSetup *lights = &g_light_setups[node->light_setup_index];

    g_render_walk.lights = lights;
    return lights;
}

/* The lights turned into the node's space, into the GTE. */
static void render_LoadTurnedLights(LightSetup *lights, SceneNode *node) {
    math_MulMatrix(&lights->lmat, &node->world, RENDER_NODE_LIGHTS);
    port_GteLoadControl(
        PORT_GTE_LIGHT, RENDER_NODE_LIGHTS, PORT_GTE_MATRIX_WORDS);
}

/* Sets the GTE up for a node by its kind. Returns whether the node's
 * mesh is then to be walked: the kinds that are drawn by a routine of
 * their own are drawn here, and the kinds that are not drawn are left. */
static s32 render_SetupNode(SceneNode *node) {
    MATRIX *view = g_render_walk.view;
    LightSetup *lights;
    s32 offset[3];
    s32 reach;
    s32 shift;
    s32 i;

    switch (node->kind) {
    case NODE_KIND_ROTATED:
        reach =
            render_NodeOffset(node, g_node_camera_root_ptr->world.t, offset);
        shift = 2 - (reach < RENDER_NEAR) - (reach < RENDER_FAR);
        g_render_walk.scale_shift = shift;
        render_StoreNodePlace(offset, shift);
        lights = render_NodeLights(node);
        render_LoadTurnedLights(lights, node);
        port_GteLoadControl(
            PORT_GTE_LIGHT_COLOR, &lights->cmat, PORT_GTE_MATRIX_WORDS);
        math_MulMatrix(view, &node->world, RENDER_NODE_MATRIX);
        render_TurnNodePlace(view);
        render_LoadBackColor(lights);
        render_LoadHalfHeightTranslation();
        render_LoadScaledRotation((s16 *)RENDER_NODE_MATRIX->m, shift);
        return TRUE;

    case NODE_KIND_UNROTATED:
        /* The listing works a scale out from the distance and then takes
         * this one. */
        render_NodeOffset(node, g_node_camera_root_ptr->world.t, offset);
        shift = 2;
        g_render_walk.scale_shift = shift;
        render_StoreNodePlace(offset, shift);
        render_TurnNodePlace(view);
        lights = render_NodeLights(node);
        render_LoadHalfHeightTranslation();
        port_GteLoadControl(
            PORT_GTE_LIGHT, &lights->lmat, PORT_GTE_MATRIX_WORDS);
        port_GteLoadControl(
            PORT_GTE_LIGHT_COLOR, &lights->cmat, PORT_GTE_MATRIX_WORDS);
        render_LoadBackColor(lights);
        render_LoadScaledRotation((s16 *)view->m, shift);
        return TRUE;

    case NODE_KIND_SHADOW:
        /* The matrix that flattens the parent onto the ground, behind
         * the view. The view matrix's own translation is used to carry
         * the shadow's place; no light is loaded, the shadow being
         * black. */
        light_ProjectShadow(&PTR(node->parent)->world, RENDER_NODE_MATRIX,
                            (s32)RENDER_WORD(node, RENDER_NODE_EXTRA));
        for (i = 0; i < 3; i++) {
            offset[i] =
                RENDER_NODE_MATRIX->t[i] - g_node_camera_root_ptr->world.t[i];
        }
        shift = 2;
        render_StoreNodePlace(offset, shift);
        render_TurnNodePlace(view);
        port_GteStoreData(PORT_GTE_IR1, view->t, 3);
        MulMatrix2(view, RENDER_NODE_MATRIX);
        port_GteWriteControl(PORT_GTE_TRX, view->t[0]);
        port_GteWriteControl(PORT_GTE_TRY, view->t[1] >> 1);
        port_GteWriteControl(PORT_GTE_TRZ, view->t[2]);
        render_LoadScaledRotation((s16 *)RENDER_NODE_MATRIX->m, shift);
        g_render_walk.scale_shift = shift;
        return TRUE;

    case NODE_KIND_SKINNED:
        reach =
            render_NodeOffset(node, g_node_camera_root_ptr->world.t, offset);
        shift = 2 - (reach < RENDER_SKIN_NEAR) - (reach < RENDER_SKIN_FAR);
        g_render_walk.scale_shift = shift;
        lights = render_NodeLights(node);
        port_GteLoadControl(
            PORT_GTE_LIGHT_COLOR, &lights->cmat, PORT_GTE_MATRIX_WORDS);
        render_LoadBackColor(lights);
        RENDER_DEPTH_SHIFT = shift;
        render_DrawSkinned((s32)RENDER_WORD(node, RENDER_NODE_EXTRA),
                           (s32)g_reloc_tables[node->mesh_block][node->mesh],
                           node->light_setup_index, (u8 *)node);
        return FALSE;

    case RENDER_KIND_TILE:
        g_render_walk.queue =
            render_DrawStageTiles(g_render_walk.queue, (u8 *)node, view);
        return FALSE;

    case NODE_KIND_CLOTH:
        /* The cloth's points are in the world: the view alone, its
         * second row halved, and no translation. */
        reach =
            render_NodeOffset(node, g_node_camera_root_ptr->world.t, offset);
        shift = 2 - (reach < RENDER_NEAR) - (reach < RENDER_FAR);
        g_render_walk.scale_shift = shift;
        lights = render_NodeLights(node);
        render_LoadTurnedLights(lights, node);
        port_GteLoadControl(
            PORT_GTE_LIGHT_COLOR, &lights->cmat, PORT_GTE_MATRIX_WORDS);
        render_LoadBackColor(lights);
        port_GteWriteControl(PORT_GTE_ROTATION, RENDER_WORD(view, 0));
        port_GteWriteControl(PORT_GTE_ROTATION + 1,
                             (u16)view->m[0][2] | ((view->m[1][0] >> 1) << 16));
        port_GteWriteControl(
            PORT_GTE_ROTATION + 2,
            ((view->m[1][1] >> 1) & 0xFFFF) | ((view->m[1][2] >> 1) << 16));
        port_GteLoadControl(PORT_GTE_ROTATION + 3, &view->m[2][0], 2);
        render_ClearTranslation();
        render_DrawCloth(shift);
        return FALSE;

    case NODE_KIND_WAVE:
        render_NodeOffset(node, g_node_camera_root_ptr->world.t, offset);
        shift = g_scene_settings.far_shift;
        g_render_walk.scale_shift = shift;
        render_StoreNodePlace(offset, shift);
        render_TurnNodePlace(view);
        lights = render_NodeLights(node);
        render_LoadHalfHeightTranslation();
        port_GteLoadControl(
            PORT_GTE_LIGHT, &lights->lmat, PORT_GTE_MATRIX_WORDS);
        port_GteLoadControl(
            PORT_GTE_LIGHT_COLOR, &lights->cmat, PORT_GTE_MATRIX_WORDS);
        render_LoadBackColor(lights);
        /* the scaled view is kept on the scratchpad for the surf */
        render_StoreScaledRotation(
            (s16 *)view->m, shift, (s16 *)RENDER_NODE_MATRIX->m);
        port_GteLoadControl(
            PORT_GTE_ROTATION, RENDER_NODE_MATRIX, PORT_GTE_MATRIX_WORDS);
        TILE_OPTIONS = g_render_options;
        wave_Draw();
        return FALSE;

    case NODE_KIND_TINY:
        /* Seen through the tiny model's matrix in place of the camera's.
         * The listing works a scale out and then takes this one. */
        render_NodeOffset(node, g_model_tiny_matrix.t, offset);
        shift = 1;
        g_render_walk.scale_shift = shift;
        render_StoreNodePlace(offset, shift);
        lights = render_NodeLights(node);
        render_LoadTurnedLights(lights, node);
        port_GteLoadControl(
            PORT_GTE_LIGHT_COLOR, &lights->cmat, PORT_GTE_MATRIX_WORDS);
        math_MulMatrix(&g_model_tiny_matrix, &node->world, RENDER_NODE_MATRIX);
        render_TurnNodePlace(&g_model_tiny_matrix);
        render_LoadBackColor(lights);
        render_LoadHalfHeightTranslation();
        render_LoadScaledRotation((s16 *)RENDER_NODE_MATRIX->m, shift);
        return TRUE;

    default:
        return FALSE;
    }
}

/* Projects a part's vertices and draws its groups, by the format the
 * vertices are stored in. */
static void render_DrawPart(s32 count, s32 format) {
    RENDER_DEPTH_SHIFT = 3 - g_render_walk.scale_shift;
    switch (format) {
    case MESH_PREBUILT:
        render_ProjectVertices(count, RENDER_POINTS, RENDER_DEPTHS);
        render_DrawGroups();
        return;

    case MESH_COMPACT:
        /* A node drawn as outlines uses the second half of the usual
         * table. */
        render_ProjectPackedVertices(count, RENDER_POINTS, RENDER_DEPTHS);
        g_render_walk.handlers = g_render_handlers;
        if (g_render_walk.node->draw_flags & NODE_DRAW_OUTLINE) {
            g_render_walk.handlers += RENDER_OUTLINE_HANDLERS;
        }
        break;

    case RENDER_FORMAT_TINY:
        /* The tiny model is projected about another point of the
         * screen. */
        port_GteWriteControl(PORT_GTE_OFX, TINY_CENTER_X << 16);
        port_GteWriteControl(PORT_GTE_OFY, TINY_CENTER_Y << 16);
        render_ProjectVertices(count, RENDER_POINTS, RENDER_DEPTHS);
        port_GteWriteControl(PORT_GTE_OFX, RENDER_CENTER_X << 16);
        port_GteWriteControl(PORT_GTE_OFY, RENDER_CENTER_Y << 16);
        g_render_walk.handlers = g_render_tiny_handlers;
        break;

    case RENDER_FORMAT_NONE:
        render_ProjectFormat3();
        g_render_walk.handlers = g_render_handlers;
        break;

    case RENDER_FORMAT_PACKED:
        render_ProjectPackedVertices(count, RENDER_POINTS, RENDER_DEPTHS);
        g_render_walk.handlers = g_render_packed_handlers;
        break;

    default:
        return;
    }
    render_DrawGroups();
    g_render_walk.handlers = (void (**)())ADDR(RENDER_HANDLERS);
}

/* In C: the two tables of labels are two switches, render_SetupNode by
 * the node's kind and render_DrawPart by the format of a part's
 * vertices, and the registers the listings share are g_render_walk. A
 * kind or a format past the end of its table, which the listing would
 * jump through whatever follows the table for, draws nothing. */
void render_DrawQueue(void) {
    SceneNode *node;
    u32 part;

    if (g_render_queue_end == g_render_queue) {
        return;
    }
    g_render_walk.queue = g_render_queue;
    g_render_walk.scale_shift = 0;
    g_render_walk.view = &g_render_view_matrix;
    g_render_walk.handlers = (void (**)())ADDR(RENDER_HANDLERS);
    do {
        node = *g_render_walk.queue++;
        g_render_walk.node = node;
        /* the remastered renderer's side channel: the thing being drawn
         * from here on, which its primitives are named by, and whether it
         * is a shadow -- the caster flattened onto the ground by
         * light_ProjectShadow and painted black, which the remastered
         * picture may darken the ground under instead (port_remaster.h) */
        port_RemasterNoteObject((PortAddress)node);
        port_RemasterNoteShadow(node->kind == NODE_KIND_SHADOW);
        /* ... and whether it is a fighter's own -- his model, its
         * shadows, a weapon, the tiny mode's model -- and not the stage's,
         * by where its mesh is from. A tile is not a node and has no
         * mesh block where a node has it; the surf's node leaves its
         * block 0. */
        port_RemasterNoteFighter(
            node->kind != STAGE_TILE_KIND && node->kind != NODE_KIND_WAVE &&
            (node->mesh_block < RELOC_STAGE ||
             node->mesh_block == RELOC_TINY_MODEL));
        if (render_SetupNode(node)) {
            /* The node's mesh: a row of parts, each a word (how many
             * vertices, and above that their format), the vertices and
             * the groups; a halfword that is not zero says another
             * follows. No vertices ends the mesh. */
            g_render_walk.mesh =
                (u8 *)ADDR(g_reloc_tables[node->mesh_block][node->mesh]);
            do {
                part = RENDER_WORD(g_render_walk.mesh, 0);
                g_render_walk.mesh += sizeof(u32);
                if ((part & 0xFFFF) == 0) {
                    break;
                }
                render_DrawPart(part & 0xFFFF, part >> 16);
                g_render_walk.mesh += sizeof(u16);
            } while (RENDER_HALF(g_render_walk.mesh, -(s32)sizeof(u16)) != 0);
        }
    } while (g_render_walk.queue != g_render_queue_end);
}
#else
INCLUDE_ASM("asm/funcs", render_DrawQueue);
#endif

/* Projecting a part's vertices: stored as SVECTORs, and stored packed, three
 * halfwords each. */
/* Assembly: the mesh cursor comes in $s0 and is left moved on; no frame;
 * the packed one is trapping too. */
#if RB_PORT
/* In C: the mesh cursor is g_render_walk.mesh. `count` is how many threes
 * of vertices there are, each projected by one operation; the listing
 * keeps a three's depths in registers until the next is under way, which
 * changes nothing. */
static void render_ProjectVertices(s32 count, u32 *points, s16 *depths) {
    u8 *vertices = g_render_walk.mesh;

    for (; count != 0; count--) {
        port_GteLoadData(PORT_GTE_VXY0, vertices, 2 * RENDER_RUN);
        port_GteCommand(PORT_GTE_CMD_RTPT);
        vertices += RENDER_RUN * sizeof(SVECTOR);
        render_StoreProjected(points, depths);
        points += RENDER_RUN;
        depths += RENDER_RUN;
    }
    g_render_walk.mesh = vertices;
}
#else
INCLUDE_ASM("asm/funcs", render_ProjectVertices);
#endif
#if RB_PORT
/* In C: `count` is how many vertices there are. They are still read three
 * at a time, so the last three may take in what follows them in the mesh;
 * the cursor is then put back, and moved on two bytes if that leaves it on
 * a whole word. */
static void render_ProjectPackedVertices(s32 count, u32 *points, s16 *depths) {
    u8 *vertices = g_render_walk.mesh;
    s32 i;

    do {
        count -= RENDER_RUN;
        for (i = 0; i < RENDER_RUN; i++) {
            port_GteWriteData(PORT_GTE_VXY0 + 2 * i, RENDER_WORD(vertices, 0));
            port_GteWriteData(
                PORT_GTE_VZ0 + 2 * i, RENDER_HALF(vertices, sizeof(u32)));
            vertices += PACKED_VERTEX_SIZE;
        }
        port_GteCommand(PORT_GTE_CMD_RTPT);
        render_StoreProjected(points, depths);
        points += RENDER_RUN;
        depths += RENDER_RUN;
    } while (count > 0);
    vertices += count * PACKED_VERTEX_SIZE;
    vertices += (u32)vertices & (sizeof(u32) - 1);
    g_render_walk.mesh = vertices;
}
#else
INCLUDE_ASM("asm/funcs", render_ProjectPackedVertices);
#endif

/* Projecting the vertices of a part of format 3: nothing. No mesh has such
 * a part. (An empty routine is the same two instructions from the compiler
 * and from the assembler; this and the two below were probably assembly
 * like everything round them.) */
void render_ProjectFormat3(void) {}

/* Draws the groups of a part; the setup of each kind of node follows it in
 * the listing. */
/* Assembly: trapping (55); no frame: it runs in render_DrawQueue's registers
 * and jumps back into the middle of it. */
#if RB_PORT
/* In C: the mesh cursor and the handler table are g_render_walk's. The
 * listing keeps its return address in the second word of the scratchpad,
 * which nothing reads; the C writes nothing there. A handler is called
 * with the count of its group. */
static void render_DrawGroups(void) {
    s32 count;
    s32 type;

    count = RENDER_HALF(g_render_walk.mesh, 0);
    g_render_walk.mesh += sizeof(u16);
    while (count != 0) {
        type = RENDER_HALF(g_render_walk.mesh, 0);
        g_render_walk.mesh += sizeof(u16);
        g_render_walk.handlers[type](count);
        count = RENDER_HALF(g_render_walk.mesh, 0);
        g_render_walk.mesh += sizeof(u16);
    }
}
#else
INCLUDE_ASM("asm/funcs", render_DrawGroups);
#endif

/* ---- stage tiles ---- */

/* A tile's primitives are built from scratch, and where the stage asks for
 * it (near the camera) each is cut into four, and those again, so that the
 * texture does not swim. The surf is drawn with render_EmitSplitFT4 too. */
/* Assembly: trapping, all six. The four that split keep their state in
 * $s1-$s7 and $fp between them and save none of it; render_DrawStageTiles
 * can leave through a jump to sys_Trap. */
#if RB_PORT
/* In C: the listing keeps its saved registers on the scratchpad, and the
 * levels of the cutting have their return addresses there; the C writes
 * neither. Everything else the listing keeps there the C keeps there
 * too. */
s16 *render_EmitSplitFT3(s32 count, s16 *vertices, s16 *records, s32 flags) {
    return render_EmitTiles(count, vertices, records, flags, FALSE);
}
#else
INCLUDE_ASM("asm/funcs", render_EmitSplitFT3);
#endif
#if RB_PORT
/* In C: the frames are an array on the scratchpad (TileFrame), a level a
 * frame; what the listing keeps in registers between the levels is
 * `split`. The four smaller triangles are made in the listing's order:
 * the first corner's, the middle one, the third corner's, the second's. */
static void render_SplitFT3(TileSplit *split, TileFrame *frame, s32 cuts) {
    TileFrame *next = frame + 1;
    POLY_FT3 *prim = (POLY_FT3 *)split->prim;

    if (cuts == 0) {
        port_GteLoadData(PORT_GTE_VXY0, frame->corner, 2 * RENDER_RUN);
        port_GteCommand(PORT_GTE_CMD_RTPT);
        RENDER_WORD_OF(prim->u0) = frame->corner[0].uv | (TILE_CLUT << 16);
        RENDER_WORD_OF(prim->u1) = frame->corner[1].uv | (TILE_TPAGE << 16);
        RENDER_HALF_OF(prim->u2) = frame->corner[2].uv;
        port_GteStoreData(PORT_GTE_SXY0, &prim->x0, 1);
        port_GteStoreData(PORT_GTE_SXY1, &prim->x1, 1);
        port_GteStoreData(PORT_GTE_SXY2, &prim->x2, 1);
        render_ChainTile(split, RENDER_PRIM_LENGTH(POLY_FT3));
        return;
    }
    next->cuts_left = (s16)(cuts - 1);
    next->corner[0] = frame->corner[0];
    render_SplitMidpoint(
        &frame->corner[0], &frame->corner[1], &next->corner[1]);
    render_SplitMidpoint(
        &frame->corner[0], &frame->corner[2], &next->corner[2]);
    render_SplitFT3(split, next, next->cuts_left);
    render_SplitMidpoint(
        &frame->corner[1], &frame->corner[2], &next->corner[0]);
    render_SplitFT3(split, next, next->cuts_left);
    next->corner[3] = next->corner[1];
    next->corner[1] = frame->corner[2];
    render_SplitFT3(split, next, next->cuts_left);
    next->corner[1] = next->corner[3];
    next->corner[2] = frame->corner[1];
    render_SplitFT3(split, next, next->cuts_left);
}
#else
INCLUDE_ASM("asm/funcs", render_SplitFT3);
#endif
#if RB_PORT
/* In C: as render_EmitSplitFT3. */
s16 *render_EmitSplitFT4(s32 count, s16 *vertices, s16 *records, s32 flags) {
    return render_EmitTiles(count, vertices, records, flags, TRUE);
}
#else
INCLUDE_ASM("asm/funcs", render_EmitSplitFT4);
#endif
#if RB_PORT
/* In C: as render_SplitFT3. The four smaller quads meet at the middle of
 * the first and fourth corners, and are made in the order of the first
 * corner's, the second's, the fourth's, the third's. */
static void render_SplitFT4(TileSplit *split, TileFrame *frame, s32 cuts) {
    TileFrame *next = frame + 1;
    POLY_FT4 *prim = (POLY_FT4 *)split->prim;

    if (cuts == 0) {
        port_GteLoadData(PORT_GTE_VXY0, frame->corner, 2 * RENDER_RUN);
        port_GteCommand(PORT_GTE_CMD_RTPT);
        RENDER_WORD_OF(prim->u0) = frame->corner[0].uv | (TILE_CLUT << 16);
        RENDER_WORD_OF(prim->u1) = frame->corner[1].uv | (TILE_TPAGE << 16);
        RENDER_HALF_OF(prim->u2) = frame->corner[2].uv;
        RENDER_HALF_OF(prim->u3) = frame->corner[3].uv;
        port_GteStoreData(PORT_GTE_SXY0, &prim->x0, 1);
        port_GteStoreData(PORT_GTE_SXY1, &prim->x1, 1);
        port_GteStoreData(PORT_GTE_SXY2, &prim->x2, 1);
        port_GteLoadData(PORT_GTE_VXY0, &frame->corner[3], 2);
        port_GteCommand(PORT_GTE_CMD_RTPS);
        port_GteStoreData(PORT_GTE_SXY2, &prim->x3, 1);
        render_ChainTile(split, RENDER_PRIM_LENGTH(POLY_FT4));
        return;
    }
    next->cuts_left = (s16)(cuts - 1);
    next->corner[0] = frame->corner[0];
    render_SplitMidpoint(
        &frame->corner[0], &frame->corner[1], &next->corner[1]);
    render_SplitMidpoint(
        &frame->corner[0], &frame->corner[2], &next->corner[2]);
    render_SplitMidpoint(
        &frame->corner[0], &frame->corner[3], &next->corner[3]);
    render_SplitFT4(split, next, next->cuts_left);
    next->corner[0] = frame->corner[1];
    render_SplitMidpoint(
        &frame->corner[1], &frame->corner[3], &next->corner[2]);
    render_SplitFT4(split, next, next->cuts_left);
    next->corner[0] = frame->corner[3];
    render_SplitMidpoint(
        &frame->corner[2], &frame->corner[3], &next->corner[1]);
    render_SplitFT4(split, next, next->cuts_left);
    next->corner[0] = frame->corner[2];
    render_SplitMidpoint(
        &frame->corner[0], &frame->corner[2], &next->corner[2]);
    render_SplitFT4(split, next, next->cuts_left);
}
#else
INCLUDE_ASM("asm/funcs", render_SplitFT4);
#endif
#if RB_PORT
/* In C: the two bytes of the texture point are averaged apart. */
static void render_SplitMidpoint(
    const TileCorner *first, const TileCorner *second, TileCorner *middle) {
    s32 x = (first->x + second->x) >> 1;
    s32 y = (first->y + second->y) >> 1;
    s32 z = (first->z + second->z) >> 1;
    u32 u = ((first->uv & 0xFF) + (second->uv & 0xFF)) >> 1;
    u32 v = ((first->uv & TILE_V_MASK) + (second->uv & TILE_V_MASK)) >> 1;

    middle->x = (s16)x;
    middle->y = (s16)y;
    middle->z = (s16)z;
    middle->uv = (u16)((u & 0xFF) | (v & TILE_V_MASK));
}
#else
INCLUDE_ASM("asm/funcs", render_SplitMidpoint);
#endif
#if RB_PORT
/* In C: a C function in the listing too, called by the walk over the
 * queue. The view is kept twice on the scratchpad: as it is, to turn the
 * tile's place into the view, and scaled down with its second row halved,
 * to project the tile's vertices. A mesh that does not end after its one
 * part stops the game. */
static void **render_DrawStageTiles(void **queue, u8 *tile, MATRIX *view) {
    s32 far_shift = g_scene_settings.far_shift;
    s32 shift = far_shift & RENDER_SHIFT_MASK;
    u32 *view_words = (u32 *)view;
    LightSetup *lights = &g_light_setups[TILE_LIGHT_SETUP];
    TileHandler *handlers = (TileHandler *)g_render_tile_handlers;
    u8 *mesh;
    s32 row[3];
    s32 place[3];
    s32 high_shift;
    s32 count;
    s32 type;
    s32 flags;
    s32 i;

    TILE_DEPTH_SHIFT = (s16)(3 - far_shift);
    TILE_MESHES = (s32)g_reloc_tables[TILE_MESH_BLOCK];
    TILE_OPTIONS = g_render_options;
    for (i = 0; i < 4; i++) {
        TILE_VIEW[i] = view_words[i];
    }
    RENDER_HALF_OF(TILE_VIEW[4]) = view->m[2][2];
    /* The scaled view: each word's low half and high half, shifted
     * down. The listing shifts a high half down as the word it is in,
     * by sixteen bits and the scale at once. */
    high_shift = (far_shift + 16) & RENDER_SHIFT_MASK;
    for (i = 0; i < 4; i++) {
        TILE_SCALED_VIEW[2 * i] = (s16)((s16)view_words[i] >> shift);
        TILE_SCALED_VIEW[2 * i + 1] = (s16)((s32)view_words[i] >> high_shift);
    }
    TILE_SCALED_VIEW[PORT_GTE_MATRIX_ELEMENTS - 1] =
        (s16)(view->m[2][2] >> shift);
    /* The three numbers of the second row once more. */
    row[0] = ((s32)view_words[1] >> high_shift) >> 1;
    row[1] = ((s16)view_words[2] >> shift) >> 1;
    row[2] = ((s32)view_words[2] >> high_shift) >> 1;
    for (i = 0; i < 3; i++) {
        TILE_SCALED_VIEW[3 + i] = (s16)row[i];
    }

    for (i = 0; i < 3; i++) {
        TILE_CAMERA[i] = g_node_camera_root_ptr->world.t[i];
    }
    port_GteLoadControl(PORT_GTE_LIGHT, &lights->lmat, PORT_GTE_MATRIX_WORDS);
    port_GteLoadControl(
        PORT_GTE_LIGHT_COLOR, &lights->cmat, PORT_GTE_MATRIX_WORDS);
    render_LoadBackColor(lights);
#if RB_REMASTER_VIEW
    /* the remastered renderer's side channel: what the stage's ground is
     * being drawn with, so that it can be drawn again as one mesh with
     * the camera's own transform (port_remaster.h) */
    port_RemasterNoteStageView(
        view->m, TILE_CAMERA, far_shift, TILE_DEPTH_SHIFT, RENDER_FOG_OT_INDEX,
        RENDER_OT_CUTOFF);
#endif

    for (;;) {
        /* the remastered renderer's side channel: this tile is what is
         * being drawn now (port_remaster.h) */
        port_RemasterNoteObject((PortAddress)tile);
        /* Where the tile is from the camera, turned into the view: the
         * translation its vertices are projected with. A tile is on the
         * ground. */
        place[0] = (s32)(RENDER_WORD(tile, TILE_X) - TILE_CAMERA[0]) >> shift;
        place[1] = (0 - TILE_CAMERA[1]) >> shift;
        place[2] = (s32)(RENDER_WORD(tile, TILE_Z) - TILE_CAMERA[2]) >> shift;
        port_GteWriteData(
            PORT_GTE_VXY0, (place[0] & 0xFFFF) | (place[1] << 16));
        port_GteWriteData(PORT_GTE_VZ0, place[2]);
        port_GteLoadControl(
            PORT_GTE_ROTATION, TILE_VIEW, PORT_GTE_MATRIX_WORDS);
        render_ClearTranslation();
        port_GteCommand(PORT_GTE_CMD_RTV0TR);
        render_LoadHalfHeightTranslation();
        port_GteLoadControl(
            PORT_GTE_ROTATION, TILE_SCALED_VIEW, PORT_GTE_MATRIX_WORDS);

        mesh = (u8 *)ADDR(g_reloc_tables[RENDER_SHALF(
            tile, TILE_BLOCK)][RENDER_SHALF(tile, TILE_MESH)]);
        flags = RENDER_SHALF(tile, TILE_FLAGS_AT);
        count = RENDER_HALF(mesh, 0);
        mesh += sizeof(u32);
        if (count != 0) {
            SET_PTR(TILE_VERTICES, (s16 *)mesh);
            if (count == 4) {
                /* The usual tile: three corners at once, then the
                 * fourth. */
                for (i = 0; i < RENDER_RUN; i++) {
                    port_GteWriteData(
                        PORT_GTE_VXY0 + 2 * i, RENDER_WORD(mesh, 0));
                    port_GteWriteData(
                        PORT_GTE_VZ0 + 2 * i, RENDER_HALF(mesh, sizeof(u32)));
                    mesh += PACKED_VERTEX_SIZE;
                }
                port_GteCommand(PORT_GTE_CMD_RTPT);
                render_StoreProjected(RENDER_POINTS, RENDER_TILE_DEPTHS);
                port_GteWriteData(PORT_GTE_VXY0, RENDER_WORD(mesh, 0));
                port_GteWriteData(PORT_GTE_VZ0, RENDER_HALF(mesh, sizeof(u32)));
                mesh += PACKED_VERTEX_SIZE;
                port_GteCommand(PORT_GTE_CMD_RTPS);
                RENDER_TILE_DEPTHS[RENDER_RUN] =
                    (s16)port_GteReadData(PORT_GTE_SZ3);
                port_GteStoreData(PORT_GTE_SXY2, &RENDER_POINTS[RENDER_RUN], 1);
            } else {
                g_render_walk.mesh = mesh;
                render_ProjectPackedVertices(
                    count, RENDER_POINTS, RENDER_TILE_DEPTHS);
                mesh = g_render_walk.mesh;
            }
            do {
                count = RENDER_HALF(mesh, 0);
                type = RENDER_HALF(mesh, sizeof(u16));
                /* ... and these are the pieces of this record of it */
                port_RemasterNoteGroup((PortAddress)mesh);
                mesh += 2 * sizeof(u16);
                mesh = (u8 *)handlers[type >> TILE_TYPE_SHIFT](
                    count, PTR(TILE_VERTICES), (s16 *)mesh, flags);
            } while (RENDER_HALF(mesh, 0) != 0);
            mesh += sizeof(u16);
            if (RENDER_HALF(mesh, 0) != 0) {
                sys_Trap();
                return queue;
            }
        }
        /* Go on if the next thing in the queue is a tile too. */
        if (g_render_queue_end == queue) {
            break;
        }
        tile = *queue;
        if (RENDER_SHALF(tile, 0) != RENDER_KIND_TILE) {
            break;
        }
        queue++;
    }
    return queue;
}
#else
INCLUDE_ASM("asm/funcs", render_DrawStageTiles);
#endif

/* ---- cloth's routines ---- */

/* Cloth is seen from both sides: a face that looks away is lit from behind
 * and sorted a little farther off. */
/* Assembly: trapping, all four; each saves $ra and calls nothing; the mesh
 * cursor comes in $s0 and is left moved on. */
#if RB_PORT
/* In C: render_EmitTwoSidedFT3 with the depth it is sorted by moved a
 * little: nearer for a face seen from its front, farther for one seen
 * from behind. */
void render_EmitClothFT3(s32 count) { render_EmitBothSidesFT3(count, TRUE); }
#else
INCLUDE_ASM("asm/funcs", render_EmitClothFT3);
#endif
#if RB_PORT
/* In C: as render_EmitClothFT3 is to render_EmitTwoSidedFT3. */
void render_EmitClothFT4(s32 count) { render_EmitBothSidesFT4(count, TRUE); }
#else
INCLUDE_ASM("asm/funcs", render_EmitClothFT4);
#endif
#if RB_PORT
/* In C: as render_EmitClothFT3 is to render_EmitTwoSidedFT3. */
void render_EmitClothGT3(s32 count) { render_EmitBothSidesGT3(count, TRUE); }
#else
INCLUDE_ASM("asm/funcs", render_EmitClothGT3);
#endif
#if RB_PORT
/* In C: as render_EmitClothFT3 is to render_EmitTwoSidedFT3. */
void render_EmitClothGT4(s32 count) { render_EmitBothSidesGT4(count, TRUE); }
#else
INCLUDE_ASM("asm/funcs", render_EmitClothGT4);
#endif

/* ---- the routines of the handler tables ----
 * The comment at the tables in render.c says what each variant is. */

/* Assembly: all four as cloth's four. */
#if RB_PORT
/* In C: as render_EmitFT3, but the colour and code are taken from the
 * group's first record alone, and a face nearer than the cutoff is left
 * out after it has been lit. */
void render_EmitPackedFT3(s32 count) {
    u8 *record = g_render_walk.mesh;
    POLY_FT3 *prim = (POLY_FT3 *)g_disp_prim_cursor;
    RenderFace face;
    s32 depth;
    s32 index;

    if (count == 0) {
        return;
    }
    port_GteLoadData(PORT_GTE_RGBC, record, 1);
    for (; count != 0; count--, record += EMIT_FT3_SIZE) {
        render_LoadFace(&face, RENDER_WORD(record, EMIT_CORNERS3), 3);
        if (render_FaceOffScreen(&face) || !render_FaceFront()) {
            continue;
        }
        depth = render_FaceDepth3(&face, RENDER_DEPTHS);
        RENDER_WORD_OF(prim->x0) = face.xy[0];
        RENDER_WORD_OF(prim->x1) = face.xy[1];
        RENDER_WORD_OF(prim->x2) = face.xy[2];
        if (depth <= 0) {
            continue;
        }
        port_GteLoadData(PORT_GTE_VXY0, record + EMIT_FT3_NORMAL, 2);
        port_GteCommand(PORT_GTE_CMD_NCS);
        index = render_OtIndex(depth << RENDER_AVERAGE_SHIFT);
        if (index < RENDER_OT_CUTOFF) {
            continue;
        }
        RENDER_WORD_OF(prim->u0) = EMIT_UV0_CLUT(record);
        RENDER_WORD_OF(prim->u1) = EMIT_UV1_TPAGE(record);
        RENDER_HALF_OF(prim->u2) = RENDER_HALF(record, EMIT_UV2);
        port_GteStoreData(PORT_GTE_RGB2, &prim->r0, 1);
        render_LinkPrim(index, prim, RENDER_PRIM_LENGTH(POLY_FT3));
        prim++;
    }
    g_render_walk.mesh = record;
    g_disp_prim_cursor = (u32 *)prim;
}
#else
INCLUDE_ASM("asm/funcs", render_EmitPackedFT3);
#endif
#if RB_PORT
/* In C: as render_EmitFT4, but a face nearer than the cutoff is left out
 * after it has been lit. */
void render_EmitPackedFT4(s32 count) {
    u8 *record = g_render_walk.mesh;
    POLY_FT4 *prim = (POLY_FT4 *)g_disp_prim_cursor;
    RenderFace face;
    s32 depth;
    s32 index;

    if (count == 0) {
        return;
    }
    for (; count != 0; count--, record += EMIT_FT4_SIZE) {
        port_GteLoadData(PORT_GTE_RGBC, record, 1);
        render_LoadFace(&face, RENDER_WORD(record, EMIT_FT4_CORNERS), 4);
        if (render_FaceOffScreen(&face) || !render_FaceFront()) {
            continue;
        }
        depth = render_FaceDepth4(&face, RENDER_DEPTHS);
        if (depth <= 0) {
            continue;
        }
        RENDER_WORD_OF(prim->x0) = face.xy[0];
        RENDER_WORD_OF(prim->x1) = face.xy[1];
        RENDER_WORD_OF(prim->x2) = face.xy[2];
        RENDER_WORD_OF(prim->x3) = face.xy[3];
        port_GteWriteData(PORT_GTE_VZ0, RENDER_HALF(record, EMIT_FT4_NORMAL_Z));
        port_GteLoadData(PORT_GTE_VXY0, record + EMIT_FT4_NORMAL_XY, 1);
        port_GteCommand(PORT_GTE_CMD_NCS);
        index = render_OtIndex(depth);
        if (index < RENDER_OT_CUTOFF) {
            continue;
        }
        RENDER_WORD_OF(prim->u0) = EMIT_UV0_CLUT(record);
        RENDER_WORD_OF(prim->u1) = EMIT_UV1_TPAGE(record);
        RENDER_HALF_OF(prim->u2) = RENDER_HALF(record, EMIT_UV2);
        RENDER_HALF_OF(prim->u3) = RENDER_HALF(record, EMIT_UV3);
        port_GteStoreData(PORT_GTE_RGB2, &prim->r0, 1);
        render_LinkPrim(index, prim, RENDER_PRIM_LENGTH(POLY_FT4));
        prim++;
    }
    g_render_walk.mesh = record;
    g_disp_prim_cursor = (u32 *)prim;
}
#else
INCLUDE_ASM("asm/funcs", render_EmitPackedFT4);
#endif
#if RB_PORT
/* In C: the listing is render_EmitGT3's, instruction for instruction. */
void render_EmitPackedGT3(s32 count) { render_EmitGT3(count); }
#else
INCLUDE_ASM("asm/funcs", render_EmitPackedGT3);
#endif
#if RB_PORT
/* In C: the listing is render_EmitGT4's, instruction for instruction. */
void render_EmitPackedGT4(s32 count) { render_EmitGT4(count); }
#else
INCLUDE_ASM("asm/funcs", render_EmitPackedGT4);
#endif

/* Between them, the walks over a cloth and over a fighter's body. */
/* Assembly: both trapping (27 and 85); the handlers they call share their
 * registers; render_DrawSkinnedMesh's frame is 0x8C bytes, and the compiler's
 * is always a multiple of 8. */
#if RB_PORT
/* In C: the node is g_render_walk.node. The count at the head of the mesh
 * is how many of the mesh's own vertices are stepped over, and how many
 * threes of the cloth's points are projected: three times as many points
 * as the mesh has vertices, as in the listing. The first group is drawn
 * without its count being looked at. */
static void render_DrawCloth(s32 scale_shift) {
    SceneNode *node = g_render_walk.node;
    SceneNode *camera = g_node_camera_root_ptr;
    u8 *mesh = (u8 *)ADDR(g_reloc_tables[node->mesh_block][node->mesh]);
    u8 *cloth_point;
    u32 *points = RENDER_POINTS;
    s16 *depths = RENDER_DEPTHS;
    s32 shift = scale_shift & RENDER_SHIFT_MASK;
    s32 count = RENDER_HALF(mesh, 0);
    s32 type;
    s32 place[3];
    s32 i;
    s32 j;

    if (count == 0) {
        return;
    }
    mesh += sizeof(u32) + count * PACKED_VERTEX_SIZE;
    mesh += (u32)mesh & (sizeof(u32) - 1);
    cloth_point = PTR(*(PTR32(u8 *) *)((u8 *)node + CLOTH_NODE_POINTS));
    for (; count != 0; count--) {
        for (i = 0; i < RENDER_RUN; i++) {
            for (j = 0; j < 3; j++) {
                place[j] = (((s32)RENDER_WORD(cloth_point, j * sizeof(s32)) >>
                             CLOTH_POINT_SHIFT) -
                            camera->world.t[j]) >>
                           shift;
            }
            port_GteWriteData(
                PORT_GTE_VXY0 + 2 * i, (place[0] & 0xFFFF) | (place[1] << 16));
            port_GteWriteData(PORT_GTE_VZ0 + 2 * i, place[2]);
            cloth_point += CLOTH_POINT_SIZE;
        }
        port_GteCommand(PORT_GTE_CMD_RTPT);
        render_StoreProjected(points, depths);
        points += RENDER_RUN;
        depths += RENDER_RUN;
    }
    RENDER_DEPTH_SHIFT = 3 - scale_shift;
    g_render_walk.mesh = mesh;
    do {
        count = RENDER_HALF(g_render_walk.mesh, 0);
        type = RENDER_HALF(g_render_walk.mesh, sizeof(u16));
        g_render_walk.mesh += 2 * sizeof(u16);
        type >>= CLOTH_TYPE_SHIFT;
        if (node->draw_flags & NODE_DRAW_OUTLINE) {
            type += CLOTH_OUTLINE_HANDLERS;
        }
        g_render_cloth_handlers[type](count);
    } while (RENDER_HALF(g_render_walk.mesh, 0) != 0);
    g_render_walk.mesh += sizeof(u16);
}
#else
INCLUDE_ASM("asm/funcs", render_DrawCloth);
#endif
#if RB_PORT
/* In C: the mesh cursor past the mesh is left in g_render_walk.mesh,
 * where the listing returns it. A section's vertices come in five lists,
 * each a count and its entries:
 *  - turned by the bone alone and moved by a number each: kept as they
 *    come out, for the lists that build on them;
 *  - built on a point already there, then seen through the view and
 *    projected in its place;
 *  - turned by the bone and the view at once, kept as they come out;
 *  - projected from a point already there;
 *  - projected, three at a time: the last three may take in what follows
 *    them, and the cursor is then put back. */
void render_DrawSkinnedMesh(s32 matrices, s32 mesh, s32 view, s32 depth_shift) {
    MatrixSlot *bones = (MatrixSlot *)ADDR(matrices);
    MATRIX *view_matrix = (MATRIX *)ADDR(view);
    MATRIX *bone;
    u8 *cursor;
    u32 *points = RENDER_POINTS;
    s16 *depths = RENDER_SKIN_DEPTHS;
    s16 *point;
    s16 *depth;
    s32 sections_left = MESH_SKINNED_SECTIONS - 1;
    s32 shift = depth_shift & RENDER_SHIFT_MASK;
    s32 count;
    s32 type;
    s32 from;
    s32 to;
    s32 i;

    cursor =
        render_LightSkin((u8 *)ADDR(mesh) + sizeof(u16), bones) - sizeof(u16);
    for (;;) {
        /* The bone, scaled down, behind the view; and where it is from
         * the camera, as it is and turned into the view. */
        bone = PTR(*bones++);
        for (i = 0; i < 3; i++) {
            RENDER_SKIN_COMBINED->t[i] =
                (bone->t[i] - g_node_camera_root_ptr->world.t[i]) >> shift;
        }
        for (i = 0; i < PORT_GTE_MATRIX_ELEMENTS; i++) {
            ((s16 *)RENDER_SKIN_BONE->m)[i] = ((s16 *)bone->m)[i] >> shift;
        }
        math_MulMatrix(view_matrix, RENDER_SKIN_BONE, RENDER_SKIN_COMBINED);
        port_GteLoadControl(
            PORT_GTE_ROTATION, view_matrix, PORT_GTE_MATRIX_WORDS);
        render_ClearTranslation();
        port_GteWriteData(
            PORT_GTE_VXY0, (RENDER_SKIN_COMBINED->t[0] & 0xFFFF) |
                               (RENDER_SKIN_COMBINED->t[1] << 16));
        port_GteWriteData(PORT_GTE_VZ0, RENDER_SKIN_COMBINED->t[2]);
        port_GteCommand(PORT_GTE_CMD_RTV0TR);
        port_GteStoreData(PORT_GTE_MAC1, RENDER_SKIN_TURNED, 3);
        cursor += sizeof(u16);

        /* Turned by the bone, and moved. */
        count = RENDER_SHALF(cursor, 0);
        cursor += sizeof(u16);
        if (count != 0) {
            port_GteLoadControl(
                PORT_GTE_ROTATION, RENDER_SKIN_BONE, PORT_GTE_MATRIX_WORDS);
            port_GteLoadControl(PORT_GTE_TRX, RENDER_SKIN_COMBINED->t, 3);
            for (; count != 0; count--) {
                render_LoadSkinVertex(PORT_GTE_VXY0, cursor);
                port_GteCommand(PORT_GTE_CMD_RTV0TR);
                point = (s16 *)points;
                point[0] = RENDER_SHALF(cursor, SKIN_MOVED_X) +
                           port_GteReadData(PORT_GTE_MAC1);
                point[1] = RENDER_SHALF(cursor, SKIN_MOVED_Y) +
                           port_GteReadData(PORT_GTE_MAC2);
                *depths = RENDER_SHALF(cursor, SKIN_MOVED_Z) +
                          port_GteReadData(PORT_GTE_MAC3);
                cursor += SKIN_MOVED_SIZE;
                points++;
                depths++;
            }
        }

        /* Built on a point: turned by the bone from there, then seen
         * through the view. */
        count = RENDER_SHALF(cursor, 0);
        cursor += sizeof(u16);
        for (; count != 0; count--) {
            point = (s16 *)&RENDER_POINTS[RENDER_SHALF(cursor, 0)];
            depth = &RENDER_SKIN_DEPTHS[RENDER_SHALF(cursor, 0)];
            render_LoadSkinVertex(PORT_GTE_VXY0, cursor + sizeof(u16));
            port_GteLoadControl(
                PORT_GTE_ROTATION, RENDER_SKIN_BONE, PORT_GTE_MATRIX_WORDS);
            port_GteWriteControl(
                PORT_GTE_TRX, point[0] + RENDER_SHALF(cursor, SKIN_BUILT_X));
            port_GteWriteControl(
                PORT_GTE_TRY, point[1] + RENDER_SHALF(cursor, SKIN_BUILT_Y));
            port_GteWriteControl(
                PORT_GTE_TRZ, *depth + RENDER_SHALF(cursor, SKIN_BUILT_Z));
            port_GteCommand(PORT_GTE_CMD_RTV0TR);
            port_GteLoadControl(
                PORT_GTE_ROTATION, view_matrix, PORT_GTE_MATRIX_WORDS);
            render_ClearTranslation();
            port_GteWriteData(
                PORT_GTE_VXY0, (port_GteReadData(PORT_GTE_MAC1) & 0xFFFF) |
                                   (port_GteReadData(PORT_GTE_MAC2) << 16));
            port_GteWriteData(PORT_GTE_VZ0, port_GteReadData(PORT_GTE_MAC3));
            port_GteCommand(PORT_GTE_CMD_RTPS);
            cursor += SKIN_BUILT_SIZE;
            port_GteStoreData(PORT_GTE_SXY2, point, 1);
            *depth = (s16)port_GteReadData(PORT_GTE_SZ3);
        }

        /* Turned by the bone and the view. */
        count = RENDER_SHALF(cursor, 0);
        cursor += sizeof(u16);
        if (count != 0) {
            port_GteLoadControl(
                PORT_GTE_ROTATION, RENDER_SKIN_COMBINED, PORT_GTE_MATRIX_WORDS);
            render_ClearTranslation();
            for (; count != 0; count--) {
                render_LoadSkinVertex(PORT_GTE_VXY0, cursor);
                port_GteCommand(PORT_GTE_CMD_RTV0TR);
                cursor += PACKED_VERTEX_SIZE;
                point = (s16 *)points;
                point[0] = (s16)port_GteReadData(PORT_GTE_MAC1);
                point[1] = (s16)port_GteReadData(PORT_GTE_MAC2);
                *depths = (s16)port_GteReadData(PORT_GTE_MAC3);
                points++;
                depths++;
            }
        }

        /* Projected from a point. */
        count = RENDER_SHALF(cursor, 0);
        cursor += sizeof(u16);
        if (count != 0) {
            port_GteLoadControl(
                PORT_GTE_ROTATION, RENDER_SKIN_COMBINED, PORT_GTE_MATRIX_WORDS);
            for (; count != 0; count--) {
                point = (s16 *)&RENDER_POINTS[RENDER_SHALF(cursor, 0)];
                depth = &RENDER_SKIN_DEPTHS[RENDER_SHALF(cursor, 0)];
                render_LoadSkinVertex(PORT_GTE_VXY0, cursor + sizeof(u16));
                port_GteWriteControl(
                    PORT_GTE_TRX, point[0] + RENDER_SKIN_TURNED[0]);
                port_GteWriteControl(
                    PORT_GTE_TRY, point[1] + RENDER_SKIN_TURNED[1]);
                port_GteWriteControl(
                    PORT_GTE_TRZ, *depth + RENDER_SKIN_TURNED[2]);
                port_GteCommand(PORT_GTE_CMD_RTPS);
                cursor += SKIN_FROM_POINT_SIZE;
                port_GteStoreData(PORT_GTE_SXY2, point, 1);
                *depth = (s16)port_GteReadData(PORT_GTE_SZ3);
            }
        }

        /* Projected, three at a time. */
        count = RENDER_SHALF(cursor, 0);
        cursor += sizeof(u16);
        if (count != 0) {
            port_GteLoadControl(
                PORT_GTE_ROTATION, RENDER_SKIN_COMBINED, PORT_GTE_MATRIX_WORDS);
            port_GteLoadControl(PORT_GTE_TRX, RENDER_SKIN_TURNED, 3);
            for (;;) {
                count -= RENDER_RUN;
                for (i = 0; i < RENDER_RUN; i++) {
                    render_LoadSkinVertex(PORT_GTE_VXY0 + 2 * i, cursor);
                    cursor += PACKED_VERTEX_SIZE;
                }
                port_GteCommand(PORT_GTE_CMD_RTPT);
                render_StoreProjected(points, depths);
                if (count <= 0) {
                    break;
                }
                points += RENDER_RUN;
                depths += RENDER_RUN;
            }
            cursor += count * PACKED_VERTEX_SIZE;
        }

        /* The section's groups, after a halfword, from the next whole
         * word. */
        cursor += sizeof(u16);
        cursor += (u32)cursor & (sizeof(u32) - 1);
        g_render_walk.mesh = cursor;
        for (;;) {
            count = RENDER_SHALF(g_render_walk.mesh, 0);
            g_render_walk.mesh += sizeof(u16);
            if (count == 0) {
                break;
            }
            type = RENDER_HALF(g_render_walk.mesh, 0);
            g_render_walk.mesh += sizeof(u16);
            ((void (**)())ADDR(RENDER_HANDLERS))[type](count);
        }
        cursor = g_render_walk.mesh;
        if (sections_left == 0) {
            break;
        }
        sections_left--;

        /* The points the sections to come build on: runs of them copied,
         * read backwards and written forwards. Then where the next
         * section's own points go. */
        while (RENDER_HALF(cursor, 0) != SKIN_CARRY_END) {
            from = RENDER_HALF(cursor, 0);
            to = RENDER_HALF(cursor, SKIN_CARRY_TO);
            count = RENDER_SHALF(cursor, SKIN_CARRY_COUNT);
            cursor += SKIN_CARRY_SIZE;
            do {
                RENDER_POINTS[to] = RENDER_POINTS[from];
                /* the remastered renderer's side channel: the GTE wrote the
                 * point down where it stored it, and this copy is not the
                 * GTE's, so the joint's faces would have no place
                 * (port_remaster.h) */
                port_RemasterCopyPoint((PortAddress)&RENDER_POINTS[from],
                                       (PortAddress)&RENDER_POINTS[to]);
                RENDER_SKIN_DEPTHS[to] = RENDER_SKIN_DEPTHS[from];
                from--;
                to++;
                count--;
            } while (count != 0);
        }
        cursor += sizeof(u16);
        points = &RENDER_POINTS[RENDER_SHALF(cursor, 0)];
        depths = &RENDER_SKIN_DEPTHS[RENDER_SHALF(cursor, 0)];
        cursor += sizeof(u16);
    }
    g_render_walk.mesh = cursor;
}
#else
INCLUDE_ASM("asm/funcs", render_DrawSkinnedMesh);
#endif

/* Two empty routines after the skinned mesh's, which nothing calls. */
static void render_Unused1(void) {}

/* Nothing calls it. */
static void render_Unused2(void) {}

/* Assembly: trapping, all twenty-six down to render_EmitOutlineGT4; each
 * saves $ra and calls nothing; the mesh cursor comes in $s0 and is left
 * moved on. The eight outline routines are also written over as the game
 * runs (render_SetOutlineColor). */
#if RB_PORT
/* In C: the face's colour is the average of its corners', taken from
 * the table render_LightSkin filled: a third of each, summed in the GTE.
 * The record's third word goes to the GTE as the colour and code. */
void render_EmitSkinFT3(s32 count) {
    u8 *record = g_render_walk.mesh;
    POLY_FT3 *prim = (POLY_FT3 *)g_disp_prim_cursor;
    RenderFace face;
    s32 depth;
    u32 colors;

    if (count == 0) {
        return;
    }
    for (; count != 0; count--, record += EMIT_SKIN_FT3_SIZE) {
        port_GteLoadData(PORT_GTE_RGBC, record + EMIT_SKIN_UV2, 1);
        render_LoadFace(&face, RENDER_WORD(record, EMIT_SKIN_CORNERS3), 3);
        if (render_FaceOffScreen(&face) || !render_FaceFront()) {
            continue;
        }
        depth = render_FaceDepth3(&face, RENDER_SKIN_DEPTHS);
        RENDER_WORD_OF(prim->x0) = face.xy[0];
        RENDER_WORD_OF(prim->x1) = face.xy[1];
        RENDER_WORD_OF(prim->x2) = face.xy[2];
        if (depth <= 0) {
            continue;
        }
        colors = RENDER_WORD(record, EMIT_SKIN_COLORS3);
        port_GteWriteData(PORT_GTE_IR0, ONE / 3);
        render_LoadSkinColor(SKIN_COLOR(colors, 2));
        port_GteCommand(PORT_GTE_CMD_GPF12);
        render_LoadSkinColor(SKIN_COLOR(colors, 1));
        port_GteCommand(PORT_GTE_CMD_GPL12);
        render_LoadSkinColor(SKIN_COLOR(colors, 0));
        port_GteCommand(PORT_GTE_CMD_GPL12);
        RENDER_WORD_OF(prim->u0) = RENDER_WORD(record, EMIT_SKIN_UV0_CLUT);
        RENDER_WORD_OF(prim->u1) = RENDER_WORD(record, EMIT_SKIN_UV1_TPAGE);
        RENDER_HALF_OF(prim->u2) = RENDER_HALF(record, EMIT_SKIN_UV2);
        render_LinkPrim(render_OtIndex(depth << RENDER_AVERAGE_SHIFT), prim,
                        RENDER_PRIM_LENGTH(POLY_FT3));
        port_GteStoreData(PORT_GTE_RGB2, &prim->r0, 1);
        prim++;
    }
    g_render_walk.mesh = record;
    g_disp_prim_cursor = (u32 *)prim;
}
#else
INCLUDE_ASM("asm/funcs", render_EmitSkinFT3);
#endif
#if RB_PORT
/* In C: as render_EmitSkinFT3, a quarter of each of four colours; the
 * fourth corner's place in the table is a halfword of its own. */
void render_EmitSkinFT4(s32 count) {
    u8 *record = g_render_walk.mesh;
    POLY_FT4 *prim = (POLY_FT4 *)g_disp_prim_cursor;
    RenderFace face;
    s32 depth;
    u32 colors;

    if (count == 0) {
        return;
    }
    for (; count != 0; count--, record += EMIT_SKIN_FT4_SIZE) {
        port_GteLoadData(PORT_GTE_RGBC, record + EMIT_SKIN_UV2, 1);
        render_LoadFace(&face, RENDER_WORD(record, EMIT_SKIN_CORNERS4), 4);
        if (render_FaceOffScreen(&face) || !render_FaceFront()) {
            continue;
        }
        depth = render_FaceDepth4(&face, RENDER_SKIN_DEPTHS);
        if (depth <= 0) {
            continue;
        }
        RENDER_WORD_OF(prim->x0) = face.xy[0];
        RENDER_WORD_OF(prim->x1) = face.xy[1];
        RENDER_WORD_OF(prim->x2) = face.xy[2];
        RENDER_WORD_OF(prim->x3) = face.xy[3];
        colors = RENDER_WORD(record, EMIT_SKIN_COLORS4);
        port_GteWriteData(PORT_GTE_IR0, ONE / 4);
        render_LoadSkinColor(SKIN_COLOR(colors, 0));
        port_GteCommand(PORT_GTE_CMD_GPF12);
        render_LoadSkinColor(SKIN_COLOR(colors, 1));
        port_GteCommand(PORT_GTE_CMD_GPL12);
        render_LoadSkinColor(SKIN_COLOR(colors, 2));
        port_GteCommand(PORT_GTE_CMD_GPL12);
        render_LoadSkinColor(RENDER_SHALF(record, EMIT_SKIN_COLOR3));
        port_GteCommand(PORT_GTE_CMD_GPL12);
        RENDER_WORD_OF(prim->u0) = RENDER_WORD(record, EMIT_SKIN_UV0_CLUT);
        RENDER_WORD_OF(prim->u1) = RENDER_WORD(record, EMIT_SKIN_UV1_TPAGE);
        RENDER_HALF_OF(prim->u2) = RENDER_HALF(record, EMIT_SKIN_UV2);
        RENDER_HALF_OF(prim->u3) = RENDER_HALF(record, EMIT_SKIN_UV3);
        render_LinkPrim(
            render_OtIndex(depth), prim, RENDER_PRIM_LENGTH(POLY_FT4));
        port_GteStoreData(PORT_GTE_RGB2, &prim->r0, 1);
        prim++;
    }
    g_render_walk.mesh = record;
    g_disp_prim_cursor = (u32 *)prim;
}
#else
INCLUDE_ASM("asm/funcs", render_EmitSkinFT4);
#endif
#if RB_PORT
/* In C: the corners' colours are copied from the table as they are; the
 * GTE only says which way the face turns. */
void render_EmitSkinGT3(s32 count) {
    u8 *record = g_render_walk.mesh;
    POLY_GT3 *prim = (POLY_GT3 *)g_disp_prim_cursor;
    RenderFace face;
    s32 depth;
    u32 colors;

    if (count == 0) {
        return;
    }
    for (; count != 0; count--, record += EMIT_SKIN_GT3_SIZE) {
        render_LoadFace(&face, RENDER_WORD(record, EMIT_SKIN_CORNERS3), 3);
        if (render_FaceOffScreen(&face) || !render_FaceFront()) {
            continue;
        }
        depth = render_FaceDepth3(&face, RENDER_SKIN_DEPTHS);
        RENDER_WORD_OF(prim->x0) = face.xy[0];
        RENDER_WORD_OF(prim->x1) = face.xy[1];
        RENDER_WORD_OF(prim->x2) = face.xy[2];
        if (depth <= 0) {
            continue;
        }
        colors = RENDER_WORD(record, EMIT_SKIN_COLORS3);
        RENDER_WORD_OF(prim->r0) =
            RENDER_SKIN_COLOR(SKIN_COLOR(colors, 2)) | RENDER_SKIN_GT3;
        RENDER_WORD_OF(prim->r1) = RENDER_SKIN_COLOR(SKIN_COLOR(colors, 1));
        RENDER_WORD_OF(prim->r2) = RENDER_SKIN_COLOR(SKIN_COLOR(colors, 0));
        RENDER_WORD_OF(prim->u0) = RENDER_WORD(record, EMIT_SKIN_UV0_CLUT);
        RENDER_WORD_OF(prim->u1) = RENDER_WORD(record, EMIT_SKIN_UV1_TPAGE);
        RENDER_HALF_OF(prim->u2) = RENDER_HALF(record, EMIT_SKIN_UV2);
        render_LinkPrim(render_OtIndex(depth << RENDER_AVERAGE_SHIFT), prim,
                        RENDER_PRIM_LENGTH(POLY_GT3));
        prim++;
    }
    g_render_walk.mesh = record;
    g_disp_prim_cursor = (u32 *)prim;
}
#else
INCLUDE_ASM("asm/funcs", render_EmitSkinGT3);
#endif
#if RB_PORT
/* In C: as render_EmitSkinGT3 does it for a triangle. */
void render_EmitSkinGT4(s32 count) {
    u8 *record = g_render_walk.mesh;
    POLY_GT4 *prim = (POLY_GT4 *)g_disp_prim_cursor;
    RenderFace face;
    s32 depth;
    u32 colors;

    if (count == 0) {
        return;
    }
    for (; count != 0; count--, record += EMIT_SKIN_GT4_SIZE) {
        render_LoadFace(&face, RENDER_WORD(record, EMIT_SKIN_CORNERS4), 4);
        if (render_FaceOffScreen(&face) || !render_FaceFront()) {
            continue;
        }
        depth = render_FaceDepth4(&face, RENDER_SKIN_DEPTHS);
        if (depth <= 0) {
            continue;
        }
        RENDER_WORD_OF(prim->x0) = face.xy[0];
        RENDER_WORD_OF(prim->x1) = face.xy[1];
        RENDER_WORD_OF(prim->x2) = face.xy[2];
        RENDER_WORD_OF(prim->x3) = face.xy[3];
        colors = RENDER_WORD(record, EMIT_SKIN_COLORS4);
        RENDER_WORD_OF(prim->r0) =
            RENDER_SKIN_COLOR(SKIN_COLOR(colors, 0)) | RENDER_SKIN_GT4;
        RENDER_WORD_OF(prim->r1) = RENDER_SKIN_COLOR(SKIN_COLOR(colors, 1));
        RENDER_WORD_OF(prim->r2) = RENDER_SKIN_COLOR(SKIN_COLOR(colors, 2));
        RENDER_WORD_OF(prim->r3) =
            RENDER_SKIN_COLOR(RENDER_SHALF(record, EMIT_SKIN_COLOR3));
        RENDER_WORD_OF(prim->u0) = RENDER_WORD(record, EMIT_SKIN_UV0_CLUT);
        RENDER_WORD_OF(prim->u1) = RENDER_WORD(record, EMIT_SKIN_UV1_TPAGE);
        RENDER_HALF_OF(prim->u2) = RENDER_HALF(record, EMIT_SKIN_UV2);
        RENDER_HALF_OF(prim->u3) = RENDER_HALF(record, EMIT_SKIN_UV3);
        render_LinkPrim(
            render_OtIndex(depth), prim, RENDER_PRIM_LENGTH(POLY_GT4));
        prim++;
    }
    g_render_walk.mesh = record;
    g_disp_prim_cursor = (u32 *)prim;
}
#else
INCLUDE_ASM("asm/funcs", render_EmitSkinGT4);
#endif
#if RB_PORT
/* In C: a face seen from behind has its normal's three numbers turned bit
 * for bit (each one less than its negative) before it is lit. */
void render_EmitTwoSidedFT3(s32 count) {
    render_EmitBothSidesFT3(count, FALSE);
}
#else
INCLUDE_ASM("asm/funcs", render_EmitTwoSidedFT3);
#endif
#if RB_PORT
/* In C: as render_EmitTwoSidedFT3 does it for a triangle. */
void render_EmitTwoSidedFT4(s32 count) {
    render_EmitBothSidesFT4(count, FALSE);
}
#else
INCLUDE_ASM("asm/funcs", render_EmitTwoSidedFT4);
#endif
#if RB_PORT
/* In C: the colour and code are taken from the group's first record alone.
 * A face seen from behind has every number of its three normals turned bit
 * for bit. */
void render_EmitTwoSidedGT3(s32 count) {
    render_EmitBothSidesGT3(count, FALSE);
}
#else
INCLUDE_ASM("asm/funcs", render_EmitTwoSidedGT3);
#endif
#if RB_PORT
/* In C: the colour and code are taken from the group's first record alone.
 * For a face seen from behind the listing turns the face's normal and the
 * second corner's whole, but of the first and third corners' only z: their
 * x and y go to the lights as they are. The C does the same. */
void render_EmitTwoSidedGT4(s32 count) {
    render_EmitBothSidesGT4(count, FALSE);
}
#else
INCLUDE_ASM("asm/funcs", render_EmitTwoSidedGT4);
#endif
#if RB_PORT
/* In C: nothing the listing does not say. */
void render_EmitUnlitFT3(s32 count) {
    u8 *record = g_render_walk.mesh;
    POLY_FT3 *prim = (POLY_FT3 *)g_disp_prim_cursor;
    RenderFace face;
    s32 depth;
    s32 index;

    if (count == 0) {
        return;
    }
    for (; count != 0; count--, record += EMIT_UNLIT_FT3_SIZE) {
        render_LoadFace(&face, RENDER_WORD(record, EMIT_CORNERS3), 3);
        if (render_FaceOffScreen(&face) || !render_FaceFront()) {
            continue;
        }
        depth = render_FaceDepth3(&face, RENDER_DEPTHS);
        RENDER_WORD_OF(prim->x0) = face.xy[0];
        RENDER_WORD_OF(prim->x1) = face.xy[1];
        RENDER_WORD_OF(prim->x2) = face.xy[2];
        if (depth <= 0) {
            continue;
        }
        index = render_OtIndex(depth << RENDER_AVERAGE_SHIFT);
        if (index < RENDER_OT_CUTOFF) {
            continue;
        }
        RENDER_WORD_OF(prim->u0) = EMIT_UV0_CLUT(record);
        RENDER_WORD_OF(prim->u1) = EMIT_UV1_TPAGE(record);
        RENDER_HALF_OF(prim->u2) = RENDER_HALF(record, EMIT_UV2);
        RENDER_WORD_OF(prim->r0) = RENDER_UNLIT_FT3;
        render_LinkPrim(index, prim, RENDER_PRIM_LENGTH(POLY_FT3));
        prim++;
    }
    g_render_walk.mesh = record;
    g_disp_prim_cursor = (u32 *)prim;
}
#else
INCLUDE_ASM("asm/funcs", render_EmitUnlitFT3);
#endif
#if RB_PORT
/* In C: nothing the listing does not say. */
void render_EmitUnlitFT4(s32 count) {
    u8 *record = g_render_walk.mesh;
    POLY_FT4 *prim = (POLY_FT4 *)g_disp_prim_cursor;
    RenderFace face;
    s32 depth;
    s32 index;

    if (count == 0) {
        return;
    }
    for (; count != 0; count--, record += EMIT_UNLIT_FT4_SIZE) {
        render_LoadFace(&face, RENDER_WORD(record, EMIT_FT4_CORNERS), 4);
        if (render_FaceOffScreen(&face) || !render_FaceFront()) {
            continue;
        }
        depth = render_FaceDepth4(&face, RENDER_DEPTHS);
        if (depth <= 0) {
            continue;
        }
        RENDER_WORD_OF(prim->x0) = face.xy[0];
        RENDER_WORD_OF(prim->x1) = face.xy[1];
        RENDER_WORD_OF(prim->x2) = face.xy[2];
        RENDER_WORD_OF(prim->x3) = face.xy[3];
        index = render_OtIndex(depth);
        if (index < RENDER_OT_CUTOFF) {
            continue;
        }
        RENDER_WORD_OF(prim->u0) = EMIT_UV0_CLUT(record);
        RENDER_WORD_OF(prim->u1) = EMIT_UV1_TPAGE(record);
        RENDER_HALF_OF(prim->u2) = RENDER_HALF(record, EMIT_UV2);
        RENDER_HALF_OF(prim->u3) = RENDER_HALF(record, EMIT_UV3);
        RENDER_WORD_OF(prim->r0) = RENDER_UNLIT_FT4;
        render_LinkPrim(index, prim, RENDER_PRIM_LENGTH(POLY_FT4));
        prim++;
    }
    g_render_walk.mesh = record;
    g_disp_prim_cursor = (u32 *)prim;
}
#else
INCLUDE_ASM("asm/funcs", render_EmitUnlitFT4);
#endif
#if RB_PORT
/* In C: the mesh cursor is g_render_walk.mesh. The corners' points are
 * written to the primitive before its depth is known, so a face with no
 * depth leaves them behind at the cursor, as in the listing. */
void render_EmitFT3(s32 count) {
    u8 *record = g_render_walk.mesh;
    POLY_FT3 *prim = (POLY_FT3 *)g_disp_prim_cursor;
    RenderFace face;
    s32 depth;

    if (count == 0) {
        return;
    }
    for (; count != 0; count--, record += EMIT_FT3_SIZE) {
        port_GteLoadData(PORT_GTE_RGBC, record, 1);
        render_LoadFace(&face, RENDER_WORD(record, EMIT_CORNERS3), 3);
        if (render_FaceOffScreen(&face) || !render_FaceFront()) {
            continue;
        }
        depth = render_FaceDepth3(&face, RENDER_DEPTHS);
        RENDER_WORD_OF(prim->x0) = face.xy[0];
        RENDER_WORD_OF(prim->x1) = face.xy[1];
        RENDER_WORD_OF(prim->x2) = face.xy[2];
        if (depth <= 0) {
            continue;
        }
        port_GteLoadData(PORT_GTE_VXY0, record + EMIT_FT3_NORMAL, 2);
        port_GteCommand(PORT_GTE_CMD_NCS);
        RENDER_WORD_OF(prim->u0) = EMIT_UV0_CLUT(record);
        RENDER_WORD_OF(prim->u1) = EMIT_UV1_TPAGE(record);
        RENDER_HALF_OF(prim->u2) = RENDER_HALF(record, EMIT_UV2);
        port_GteStoreData(PORT_GTE_RGB2, &prim->r0, 1);
        render_LinkPrim(render_OtIndex(depth << RENDER_AVERAGE_SHIFT), prim,
                        RENDER_PRIM_LENGTH(POLY_FT3));
        prim++;
    }
    g_render_walk.mesh = record;
    g_disp_prim_cursor = (u32 *)prim;
}
#else
INCLUDE_ASM("asm/funcs", render_EmitFT3);
#endif
#if RB_PORT
/* In C: a quad's depth is the plain average of its corners', worked out
 * by the processor; nothing is written before it is known. */
void render_EmitFT4(s32 count) {
    u8 *record = g_render_walk.mesh;
    POLY_FT4 *prim = (POLY_FT4 *)g_disp_prim_cursor;
    RenderFace face;
    s32 depth;

    if (count == 0) {
        return;
    }
    for (; count != 0; count--, record += EMIT_FT4_SIZE) {
        port_GteLoadData(PORT_GTE_RGBC, record, 1);
        render_LoadFace(&face, RENDER_WORD(record, EMIT_FT4_CORNERS), 4);
        if (render_FaceOffScreen(&face) || !render_FaceFront()) {
            continue;
        }
        depth = render_FaceDepth4(&face, RENDER_DEPTHS);
        if (depth <= 0) {
            continue;
        }
        RENDER_WORD_OF(prim->x0) = face.xy[0];
        RENDER_WORD_OF(prim->x1) = face.xy[1];
        RENDER_WORD_OF(prim->x2) = face.xy[2];
        RENDER_WORD_OF(prim->x3) = face.xy[3];
        port_GteWriteData(PORT_GTE_VZ0, RENDER_HALF(record, EMIT_FT4_NORMAL_Z));
        port_GteLoadData(PORT_GTE_VXY0, record + EMIT_FT4_NORMAL_XY, 1);
        port_GteCommand(PORT_GTE_CMD_NCS);
        RENDER_WORD_OF(prim->u0) = EMIT_UV0_CLUT(record);
        RENDER_WORD_OF(prim->u1) = EMIT_UV1_TPAGE(record);
        RENDER_HALF_OF(prim->u2) = RENDER_HALF(record, EMIT_UV2);
        RENDER_HALF_OF(prim->u3) = RENDER_HALF(record, EMIT_UV3);
        port_GteStoreData(PORT_GTE_RGB2, &prim->r0, 1);
        render_LinkPrim(
            render_OtIndex(depth), prim, RENDER_PRIM_LENGTH(POLY_FT4));
        prim++;
    }
    g_render_walk.mesh = record;
    g_disp_prim_cursor = (u32 *)prim;
}
#else
INCLUDE_ASM("asm/funcs", render_EmitFT4);
#endif
#if RB_PORT
/* In C: the tint is a number of the record the node begins, read through
 * g_render_walk.node (the listing reads it through the walk's register).
 * The place for the next primitive moves on for every face, drawn or
 * not. */
void render_EmitTintedFT3(s32 count) {
    u8 *record = g_render_walk.mesh;
    POLY_FT3 *prim = (POLY_FT3 *)g_disp_prim_cursor;
    s32 tint = RENDER_SHALF(g_render_walk.node, RENDER_NODE_TINT);
    RenderFace face;
    s32 depth;

    if (count == 0) {
        return;
    }
    for (; count != 0; count--, record += EMIT_FT3_SIZE, prim++) {
        port_GteLoadData(PORT_GTE_RGBC, record, 1);
        render_LoadFace(&face, RENDER_WORD(record, EMIT_CORNERS3), 3);
        if (render_FaceOffScreen(&face) || !render_FaceFront()) {
            continue;
        }
        depth = render_FaceDepth3(&face, RENDER_DEPTHS);
        RENDER_WORD_OF(prim->x0) = face.xy[0];
        RENDER_WORD_OF(prim->x1) = face.xy[1];
        RENDER_WORD_OF(prim->x2) = face.xy[2];
        if (depth <= 0) {
            continue;
        }
        port_GteLoadData(PORT_GTE_VXY0, record + EMIT_FT3_NORMAL, 2);
        port_GteCommand(PORT_GTE_CMD_NCS);
        port_GteWriteData(PORT_GTE_IR0, (u32)tint);
        RENDER_WORD_OF(prim->u0) = EMIT_UV0_CLUT(record);
        RENDER_WORD_OF(prim->u1) = EMIT_UV1_TPAGE(record);
        RENDER_HALF_OF(prim->u2) = RENDER_HALF(record, EMIT_UV2);
        port_GteCommand(PORT_GTE_CMD_GPF12);
        render_LinkPrim(render_OtIndex(depth << RENDER_AVERAGE_SHIFT), prim,
                        RENDER_PRIM_LENGTH(POLY_FT3));
        port_GteStoreData(PORT_GTE_RGB2, &prim->r0, 1);
    }
    g_render_walk.mesh = record;
    g_disp_prim_cursor = (u32 *)prim;
}
#else
INCLUDE_ASM("asm/funcs", render_EmitTintedFT3);
#endif
#if RB_PORT
/* In C: as render_EmitTintedFT3 does it for a triangle. */
void render_EmitTintedFT4(s32 count) {
    u8 *record = g_render_walk.mesh;
    POLY_FT4 *prim = (POLY_FT4 *)g_disp_prim_cursor;
    s32 tint = RENDER_SHALF(g_render_walk.node, RENDER_NODE_TINT);
    RenderFace face;
    s32 depth;

    if (count == 0) {
        return;
    }
    for (; count != 0; count--, record += EMIT_FT4_SIZE, prim++) {
        port_GteLoadData(PORT_GTE_RGBC, record, 1);
        render_LoadFace(&face, RENDER_WORD(record, EMIT_FT4_CORNERS), 4);
        if (render_FaceOffScreen(&face) || !render_FaceFront()) {
            continue;
        }
        depth = render_FaceDepth4(&face, RENDER_DEPTHS);
        if (depth <= 0) {
            continue;
        }
        RENDER_WORD_OF(prim->x0) = face.xy[0];
        RENDER_WORD_OF(prim->x1) = face.xy[1];
        RENDER_WORD_OF(prim->x2) = face.xy[2];
        RENDER_WORD_OF(prim->x3) = face.xy[3];
        port_GteWriteData(PORT_GTE_VZ0, RENDER_HALF(record, EMIT_FT4_NORMAL_Z));
        port_GteLoadData(PORT_GTE_VXY0, record + EMIT_FT4_NORMAL_XY, 1);
        port_GteCommand(PORT_GTE_CMD_NCS);
        port_GteWriteData(PORT_GTE_IR0, (u32)tint);
        RENDER_WORD_OF(prim->u0) = EMIT_UV0_CLUT(record);
        RENDER_WORD_OF(prim->u1) = EMIT_UV1_TPAGE(record);
        RENDER_HALF_OF(prim->u2) = RENDER_HALF(record, EMIT_UV2);
        RENDER_HALF_OF(prim->u3) = RENDER_HALF(record, EMIT_UV3);
        port_GteCommand(PORT_GTE_CMD_GPF12);
        render_LinkPrim(
            render_OtIndex(depth), prim, RENDER_PRIM_LENGTH(POLY_FT4));
        port_GteStoreData(PORT_GTE_RGB2, &prim->r0, 1);
    }
    g_render_walk.mesh = record;
    g_disp_prim_cursor = (u32 *)prim;
}
#else
INCLUDE_ASM("asm/funcs", render_EmitTintedFT4);
#endif
#if RB_PORT
/* In C: the colour and code are taken from the group's first record
 * alone. */
void render_EmitGT3(s32 count) {
    u8 *record = g_render_walk.mesh;
    POLY_GT3 *prim = (POLY_GT3 *)g_disp_prim_cursor;
    RenderFace face;
    s32 depth;

    if (count == 0) {
        return;
    }
    port_GteLoadData(PORT_GTE_RGBC, record, 1);
    for (; count != 0; count--, record += EMIT_GT3_SIZE) {
        render_LoadFace(&face, RENDER_WORD(record, EMIT_CORNERS3), 3);
        if (render_FaceOffScreen(&face) || !render_FaceFront()) {
            continue;
        }
        depth = render_FaceDepth3(&face, RENDER_DEPTHS);
        RENDER_WORD_OF(prim->x0) = face.xy[0];
        RENDER_WORD_OF(prim->x1) = face.xy[1];
        RENDER_WORD_OF(prim->x2) = face.xy[2];
        if (depth <= 0 || (depth << RENDER_AVERAGE_SHIFT) >= RENDER_DEPTH_END) {
            continue;
        }
        port_GteLoadData(PORT_GTE_VXY0, record + EMIT_GT3_NORMAL_XY0, 2);
        port_GteLoadData(PORT_GTE_VXY1, record + EMIT_GT3_NORMAL_XY1, 1);
        port_GteWriteData(
            PORT_GTE_VZ1, RENDER_WORD(record, EMIT_GT3_NORMAL_Z01) >> 16);
        port_GteLoadData(PORT_GTE_VXY2, record + EMIT_GT3_NORMAL_XY2, 2);
        port_GteCommand(PORT_GTE_CMD_NCT);
        RENDER_WORD_OF(prim->u0) = EMIT_UV0_CLUT(record);
        RENDER_WORD_OF(prim->u1) = EMIT_UV1_TPAGE(record);
        RENDER_HALF_OF(prim->u2) = RENDER_HALF(record, EMIT_UV2);
        render_LinkPrim(render_OtIndex(depth << RENDER_AVERAGE_SHIFT), prim,
                        RENDER_PRIM_LENGTH(POLY_GT3));
        port_GteStoreData(PORT_GTE_RGB0, &prim->r0, 1);
        port_GteStoreData(PORT_GTE_RGB1, &prim->r1, 1);
        port_GteStoreData(PORT_GTE_RGB2, &prim->r2, 1);
        prim++;
    }
    g_render_walk.mesh = record;
    g_disp_prim_cursor = (u32 *)prim;
}
#else
INCLUDE_ASM("asm/funcs", render_EmitGT3);
#endif
#if RB_PORT
/* In C: the colour and code are taken from the group's first record
 * alone. The first corner is lit by the face's own normal, the other three
 * by theirs. */
void render_EmitGT4(s32 count) {
    u8 *record = g_render_walk.mesh;
    POLY_GT4 *prim = (POLY_GT4 *)g_disp_prim_cursor;
    RenderFace face;
    s32 depth;
    u32 depths; /* two normals' z, one under the other */

    if (count == 0) {
        return;
    }
    port_GteLoadData(PORT_GTE_RGBC, record, 1);
    for (; count != 0; count--, record += EMIT_GT4_SIZE) {
        render_LoadFace(&face, RENDER_WORD(record, EMIT_GT4_CORNERS), 4);
        if (render_FaceOffScreen(&face) || !render_FaceFront()) {
            continue;
        }
        depth = render_FaceDepth4(&face, RENDER_DEPTHS);
        if (depth <= 0 || depth >= RENDER_DEPTH_END) {
            continue;
        }
        RENDER_WORD_OF(prim->x0) = face.xy[0];
        RENDER_WORD_OF(prim->x1) = face.xy[1];
        RENDER_WORD_OF(prim->x2) = face.xy[2];
        RENDER_WORD_OF(prim->x3) = face.xy[3];
        depths = RENDER_WORD(record, EMIT_GT4_NORMAL_Z_FACE_0);
        port_GteLoadData(PORT_GTE_VXY0, record + EMIT_GT4_NORMAL_XY_FACE, 1);
        port_GteWriteData(PORT_GTE_VZ0, depths);
        port_GteCommand(PORT_GTE_CMD_NCS);
        port_GteStoreData(PORT_GTE_RGB2, &prim->r0, 1);
        port_GteLoadData(PORT_GTE_VXY0, record + EMIT_GT4_NORMAL_XY0, 1);
        port_GteWriteData(PORT_GTE_VZ0, depths >> 16);
        depths = RENDER_WORD(record, EMIT_GT4_NORMAL_Z12);
        port_GteLoadData(PORT_GTE_VXY1, record + EMIT_GT4_NORMAL_XY1, 1);
        port_GteWriteData(PORT_GTE_VZ1, depths);
        port_GteLoadData(PORT_GTE_VXY2, record + EMIT_GT4_NORMAL_XY2, 1);
        port_GteWriteData(PORT_GTE_VZ2, depths >> 16);
        port_GteCommand(PORT_GTE_CMD_NCT);
        RENDER_WORD_OF(prim->u0) = EMIT_UV0_CLUT(record);
        RENDER_WORD_OF(prim->u1) = EMIT_UV1_TPAGE(record);
        RENDER_HALF_OF(prim->u2) = RENDER_HALF(record, EMIT_UV2);
        RENDER_HALF_OF(prim->u3) = RENDER_HALF(record, EMIT_UV3);
        render_LinkPrim(
            render_OtIndex(depth), prim, RENDER_PRIM_LENGTH(POLY_GT4));
        port_GteStoreData(PORT_GTE_RGB0, &prim->r1, 1);
        port_GteStoreData(PORT_GTE_RGB1, &prim->r2, 1);
        port_GteStoreData(PORT_GTE_RGB2, &prim->r3, 1);
        prim++;
    }
    g_render_walk.mesh = record;
    g_disp_prim_cursor = (u32 *)prim;
}
#else
INCLUDE_ASM("asm/funcs", render_EmitGT4);
#endif
#if RB_PORT
/* In C: a record is its corners and nothing else. The place for the next
 * primitive moves on for every face, drawn or not. */
void render_EmitBlackF3(s32 count) {
    u8 *record = g_render_walk.mesh;
    POLY_F3 *prim = (POLY_F3 *)g_disp_prim_cursor;
    RenderFace face;
    s32 depth;

    if (count == 0) {
        return;
    }
    for (; count != 0; count--, record += sizeof(u32), prim++) {
        render_LoadFace(&face, RENDER_WORD(record, 0), 3);
        if (render_FaceOffScreen(&face) || !render_FaceFront()) {
            continue;
        }
        depth = render_FaceDepth3(&face, RENDER_DEPTHS);
        RENDER_WORD_OF(prim->x0) = face.xy[0];
        RENDER_WORD_OF(prim->x1) = face.xy[1];
        RENDER_WORD_OF(prim->x2) = face.xy[2];
        if (depth <= 0) {
            continue;
        }
        RENDER_WORD_OF(prim->r0) = RENDER_BLACK_F3;
        render_LinkPrim(render_OtIndex(depth << RENDER_AVERAGE_SHIFT), prim,
                        RENDER_PRIM_LENGTH(POLY_F3));
    }
    g_render_walk.mesh = record;
    g_disp_prim_cursor = (u32 *)prim;
}
#else
INCLUDE_ASM("asm/funcs", render_EmitBlackF3);
#endif
#if RB_PORT
/* In C: as render_EmitBlackF3 does it for a triangle. */
void render_EmitBlackF4(s32 count) {
    u8 *record = g_render_walk.mesh;
    POLY_F4 *prim = (POLY_F4 *)g_disp_prim_cursor;
    RenderFace face;
    s32 depth;

    if (count == 0) {
        return;
    }
    for (; count != 0; count--, record += sizeof(u32), prim++) {
        render_LoadFace(&face, RENDER_WORD(record, 0), 4);
        if (render_FaceOffScreen(&face) || !render_FaceFront()) {
            continue;
        }
        depth = render_FaceDepth4(&face, RENDER_DEPTHS);
        if (depth <= 0) {
            continue;
        }
        RENDER_WORD_OF(prim->x0) = face.xy[0];
        RENDER_WORD_OF(prim->x1) = face.xy[1];
        RENDER_WORD_OF(prim->x2) = face.xy[2];
        RENDER_WORD_OF(prim->x3) = face.xy[3];
        RENDER_WORD_OF(prim->r0) = RENDER_BLACK_F4;
        render_LinkPrim(
            render_OtIndex(depth), prim, RENDER_PRIM_LENGTH(POLY_F4));
    }
    g_render_walk.mesh = record;
    g_disp_prim_cursor = (u32 *)prim;
}
#else
INCLUDE_ASM("asm/funcs", render_EmitBlackF4);
#endif
#if RB_PORT
/* In C: the outline's colour is g_render_outline_color, where the listing
 * has it in two of its instructions. */
void render_EmitSkinOutlineFT3(s32 count) {
    render_EmitOutline3(count, EMIT_SKIN_FT3_SIZE, FALSE, RENDER_SKIN_DEPTHS);
}
#else
INCLUDE_ASM("asm/funcs", render_EmitSkinOutlineFT3);
#endif
#if RB_PORT
/* In C: as render_EmitSkinOutlineFT3. */
void render_EmitSkinOutlineFT4(s32 count) {
    render_EmitOutline4(count, EMIT_SKIN_FT4_SIZE, EMIT_SKIN_CORNERS4, FALSE,
                        RENDER_SKIN_DEPTHS);
}
#else
INCLUDE_ASM("asm/funcs", render_EmitSkinOutlineFT4);
#endif
#if RB_PORT
/* In C: the listing is render_EmitSkinOutlineFT3's, instruction for
 * instruction. */
void render_EmitSkinOutlineGT3(s32 count) {
    render_EmitOutline3(count, EMIT_SKIN_GT3_SIZE, FALSE, RENDER_SKIN_DEPTHS);
}
#else
INCLUDE_ASM("asm/funcs", render_EmitSkinOutlineGT3);
#endif
#if RB_PORT
/* In C: the listing is render_EmitSkinOutlineFT4's, instruction for
 * instruction. */
void render_EmitSkinOutlineGT4(s32 count) {
    render_EmitOutline4(count, EMIT_SKIN_GT4_SIZE, EMIT_SKIN_CORNERS4, FALSE,
                        RENDER_SKIN_DEPTHS);
}
#else
INCLUDE_ASM("asm/funcs", render_EmitSkinOutlineGT4);
#endif
#if RB_PORT
/* In C: as render_EmitSkinOutlineFT3; this one leaves each record's colour
 * and code in the GTE, which nothing then uses. */
void render_EmitOutlineFT3(s32 count) {
    render_EmitOutline3(count, EMIT_FT3_SIZE, TRUE, RENDER_DEPTHS);
}
#else
INCLUDE_ASM("asm/funcs", render_EmitOutlineFT3);
#endif
#if RB_PORT
/* In C: as render_EmitOutlineFT3. */
void render_EmitOutlineFT4(s32 count) {
    render_EmitOutline4(
        count, EMIT_FT4_SIZE, EMIT_FT4_CORNERS, TRUE, RENDER_DEPTHS);
}
#else
INCLUDE_ASM("asm/funcs", render_EmitOutlineFT4);
#endif
#if RB_PORT
/* In C: as render_EmitSkinOutlineFT3. */
void render_EmitOutlineGT3(s32 count) {
    render_EmitOutline3(count, EMIT_GT3_SIZE, FALSE, RENDER_DEPTHS);
}
#else
INCLUDE_ASM("asm/funcs", render_EmitOutlineGT3);
#endif
#if RB_PORT
/* In C: as render_EmitSkinOutlineFT3. */
void render_EmitOutlineGT4(s32 count) {
    render_EmitOutline4(
        count, EMIT_GT4_SIZE, EMIT_GT4_CORNERS, FALSE, RENDER_DEPTHS);
}
#else
INCLUDE_ASM("asm/funcs", render_EmitOutlineGT4);
#endif

/* Two routines no table has: a lit flat triangle with no texture, and a
 * flat quad linked in together with a second, two-word primitive that
 * follows it in the mesh (a guess: the drawing mode that blends it). */
/* Assembly: all four as the routines of the tables. */
#if RB_PORT
/* In C: the primitives are in the mesh itself, two of each, one for each
 * frame of the double buffer; the record's corners, normal and colour
 * follow them. A group of no faces moves the cursor back by the two
 * primitives' size, as the listing does. */
void render_EmitF3(s32 count) {
    render_EmitMeshF3(count, (u32 *)ADDR(RENDER_OT), FALSE);
}
#else
INCLUDE_ASM("asm/funcs", render_EmitF3);
#endif
#if RB_PORT
/* In C: as render_EmitF3 has its primitives, and after the record a second
 * primitive, of two words, again one for each frame. The record has a
 * number added to the face's depth. Nothing is lit: the colour is the
 * mesh's own. */
void render_EmitModeF4(s32 count) {
    u8 *record = g_render_walk.mesh;
    POLY_F4 *prim;
    u32 *mode;
    RenderFace face;
    s32 depth;
    s32 index;

    if (count == 0) {
        g_render_walk.mesh = record - 2 * sizeof(POLY_F4);
        return;
    }
    for (; count != 0; count--, record += EMIT_OWN_MODE_F4_SIZE) {
        prim = (POLY_F4 *)(record + (RENDER_FRAME_MASK & sizeof(POLY_F4)));
        mode = (u32 *)(record + EMIT_OWN_MODE_F4_MODES +
                       (RENDER_FRAME_MASK & EMIT_OWN_MODE_SIZE));
        render_LoadFace(
            &face, RENDER_WORD(record, EMIT_OWN_MODE_F4_CORNERS), 4);
        if (render_FaceOffScreen(&face) || !render_FaceFront()) {
            continue;
        }
        depth = render_FaceDepth4(&face, RENDER_DEPTHS);
        if (depth <= 0 || depth >= RENDER_DEPTH_END) {
            continue;
        }
        RENDER_WORD_OF(prim->x0) = face.xy[0];
        RENDER_WORD_OF(prim->x1) = face.xy[1];
        RENDER_WORD_OF(prim->x2) = face.xy[2];
        RENDER_WORD_OF(prim->x3) = face.xy[3];
        depth += RENDER_WORD(record, EMIT_OWN_MODE_F4_DEPTH_BIAS);
        index = render_OtIndex(depth);
        render_LinkPrim(index, prim, RENDER_PRIM_LENGTH(POLY_F4));
        render_LinkPrim(index, mode, EMIT_OWN_MODE_LENGTH);
    }
    g_render_walk.mesh = record;
}
#else
INCLUDE_ASM("asm/funcs", render_EmitModeF4);
#endif

#if RB_PORT
/* In C: as render_EmitF3, into the tiny model's ordering table: a depth is
 * taken a thousand nearer and its entry is one of that table's 256. */
void render_EmitTinyF3(s32 count) {
    render_EmitMeshF3(count, g_render_tiny_ot, TRUE);
}
#else
INCLUDE_ASM("asm/funcs", render_EmitTinyF3);
#endif
#if RB_PORT
/* In C: as render_EmitTinyF3 does it for a triangle. */
void render_EmitTinyF4(s32 count) {
    u8 *record = g_render_walk.mesh;
    POLY_F4 *prim;
    RenderFace face;
    s32 depth;
    s32 index;

    if (count == 0) {
        g_render_walk.mesh = record - 2 * sizeof(POLY_F4);
        return;
    }
    port_GteLoadData(
        PORT_GTE_RGBC,
        record + (RENDER_FRAME_MASK & sizeof(POLY_F4)) + sizeof(u32), 1);
    for (; count != 0; count--, record += EMIT_OWN_F4_SIZE) {
        prim = (POLY_F4 *)(record + (RENDER_FRAME_MASK & sizeof(POLY_F4)));
        render_LoadFace(&face, RENDER_WORD(record, EMIT_OWN_F4_CORNERS), 4);
        if (render_FaceOffScreen(&face) || !render_FaceFront()) {
            continue;
        }
        depth = render_FaceDepth4(&face, RENDER_DEPTHS);
        if (depth <= 0 || depth >= RENDER_DEPTH_END) {
            continue;
        }
        RENDER_WORD_OF(prim->x0) = face.xy[0];
        RENDER_WORD_OF(prim->x1) = face.xy[1];
        RENDER_WORD_OF(prim->x2) = face.xy[2];
        RENDER_WORD_OF(prim->x3) = face.xy[3];
        port_GteLoadData(PORT_GTE_VXY0, record + EMIT_OWN_F4_NORMAL, 2);
        port_GteLoadData(PORT_GTE_RGBC, record + EMIT_OWN_F4_COLOR, 1);
        port_GteCommand(PORT_GTE_CMD_NCCS);
        index = RENDER_TINY_OT_INDEX(render_OtIndex(depth - TINY_DEPTH_BIAS));
        render_LinkEntry(
            &g_render_tiny_ot[index], prim, RENDER_PRIM_LENGTH(POLY_F4));
        port_GteStoreData(PORT_GTE_RGB2, &prim->r0, 1);
    }
    g_render_walk.mesh = record;
}
#else
INCLUDE_ASM("asm/funcs", render_EmitTinyF4);
#endif
