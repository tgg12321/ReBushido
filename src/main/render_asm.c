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

/* ---- maths: the first routine of the file ---- */

/* The rotation matrix for three angles, turned about Z, then Y, then X
 * (math.h). */
/* Assembly: trapping (15); it saves $ra and nine registers, calls nothing and
 * uses five of them. */
INCLUDE_ASM("asm/funcs", math_RotMatrixZYX);

/* ---- the walk over the queue ---- */

/* Assembly: trapping; it jumps through the two tables above to labels in
 * render_DrawGroups, which jump back; what it calls shares its registers. */
INCLUDE_ASM("asm/funcs", render_DrawQueue);

/* Projecting a part's vertices: stored as SVECTORs, and stored packed, three
 * halfwords each. */
/* Assembly: the mesh cursor comes in $s0 and is left moved on; no frame;
 * the packed one is trapping too. */
INCLUDE_ASM("asm/funcs", render_ProjectVertices);
INCLUDE_ASM("asm/funcs", render_ProjectPackedVertices);

/* Projecting the vertices of a part of format 3: nothing. No mesh has such
 * a part. (An empty routine is the same two instructions from the compiler
 * and from the assembler; this and the two below were probably assembly
 * like everything round them.) */
void render_ProjectFormat3(void) {}

/* Draws the groups of a part; the setup of each kind of node follows it in
 * the listing. */
/* Assembly: trapping (55); no frame: it runs in render_DrawQueue's registers
 * and jumps back into the middle of it. */
INCLUDE_ASM("asm/funcs", render_DrawGroups);

/* ---- stage tiles ---- */

/* A tile's primitives are built from scratch, and where the stage asks for
 * it (near the camera) each is cut into four, and those again, so that the
 * texture does not swim. The surf is drawn with render_EmitSplitFT4 too. */
/* Assembly: trapping, all six. The four that split keep their state in
 * $s1-$s7 and $fp between them and save none of it; render_DrawStageTiles
 * can leave through a jump to sys_Trap. */
INCLUDE_ASM("asm/funcs", render_EmitSplitFT3);
INCLUDE_ASM("asm/funcs", render_SplitFT3);
INCLUDE_ASM("asm/funcs", render_EmitSplitFT4);
INCLUDE_ASM("asm/funcs", render_SplitFT4);
INCLUDE_ASM("asm/funcs", render_SplitMidpoint);
INCLUDE_ASM("asm/funcs", render_DrawStageTiles);

/* ---- cloth's routines ---- */

/* Cloth is seen from both sides: a face that looks away is lit from behind
 * and sorted a little farther off. */
/* Assembly: trapping, all four; each saves $ra and calls nothing; the mesh
 * cursor comes in $s0 and is left moved on. */
INCLUDE_ASM("asm/funcs", render_EmitClothFT3);
INCLUDE_ASM("asm/funcs", render_EmitClothFT4);
INCLUDE_ASM("asm/funcs", render_EmitClothGT3);
INCLUDE_ASM("asm/funcs", render_EmitClothGT4);

/* ---- the routines of the handler tables ----
 * The comment at the tables in render.c says what each variant is. */

/* Assembly: all four as cloth's four. */
INCLUDE_ASM("asm/funcs", render_EmitPackedFT3);
INCLUDE_ASM("asm/funcs", render_EmitPackedFT4);
INCLUDE_ASM("asm/funcs", render_EmitPackedGT3);
INCLUDE_ASM("asm/funcs", render_EmitPackedGT4);

/* Between them, the walks over a cloth and over a fighter's body. */
/* Assembly: both trapping (27 and 85); the handlers they call share their
 * registers; render_DrawSkinnedMesh's frame is 0x8C bytes, and the compiler's
 * is always a multiple of 8. */
INCLUDE_ASM("asm/funcs", render_DrawCloth);
INCLUDE_ASM("asm/funcs", render_DrawSkinnedMesh);

/* Two empty routines after the skinned mesh's, which nothing calls. */
static void render_Unused1(void) {}

/* Nothing calls it. */
static void render_Unused2(void) {}

/* Assembly: trapping, all twenty-six down to render_EmitOutlineGT4; each
 * saves $ra and calls nothing; the mesh cursor comes in $s0 and is left
 * moved on. The eight outline routines are also written over as the game
 * runs (render_SetOutlineColor). */
INCLUDE_ASM("asm/funcs", render_EmitSkinFT3);
INCLUDE_ASM("asm/funcs", render_EmitSkinFT4);
INCLUDE_ASM("asm/funcs", render_EmitSkinGT3);
INCLUDE_ASM("asm/funcs", render_EmitSkinGT4);
INCLUDE_ASM("asm/funcs", render_EmitTwoSidedFT3);
INCLUDE_ASM("asm/funcs", render_EmitTwoSidedFT4);
INCLUDE_ASM("asm/funcs", render_EmitTwoSidedGT3);
INCLUDE_ASM("asm/funcs", render_EmitTwoSidedGT4);
INCLUDE_ASM("asm/funcs", render_EmitUnlitFT3);
INCLUDE_ASM("asm/funcs", render_EmitUnlitFT4);
INCLUDE_ASM("asm/funcs", render_EmitFT3);
INCLUDE_ASM("asm/funcs", render_EmitFT4);
INCLUDE_ASM("asm/funcs", render_EmitTintedFT3);
INCLUDE_ASM("asm/funcs", render_EmitTintedFT4);
INCLUDE_ASM("asm/funcs", render_EmitGT3);
INCLUDE_ASM("asm/funcs", render_EmitGT4);
INCLUDE_ASM("asm/funcs", render_EmitBlackF3);
INCLUDE_ASM("asm/funcs", render_EmitBlackF4);
INCLUDE_ASM("asm/funcs", render_EmitSkinOutlineFT3);
INCLUDE_ASM("asm/funcs", render_EmitSkinOutlineFT4);
INCLUDE_ASM("asm/funcs", render_EmitSkinOutlineGT3);
INCLUDE_ASM("asm/funcs", render_EmitSkinOutlineGT4);
INCLUDE_ASM("asm/funcs", render_EmitOutlineFT3);
INCLUDE_ASM("asm/funcs", render_EmitOutlineFT4);
INCLUDE_ASM("asm/funcs", render_EmitOutlineGT3);
INCLUDE_ASM("asm/funcs", render_EmitOutlineGT4);

/* Two routines no table has: a lit flat triangle with no texture, and a
 * flat quad linked in together with a second, two-word primitive that
 * follows it in the mesh (a guess: the drawing mode that blends it). */
/* Assembly: all four as the routines of the tables. */
INCLUDE_ASM("asm/funcs", render_EmitF3);
INCLUDE_ASM("asm/funcs", render_EmitModeF4);

INCLUDE_ASM("asm/funcs", render_EmitTinyF3);
INCLUDE_ASM("asm/funcs", render_EmitTinyF4);
