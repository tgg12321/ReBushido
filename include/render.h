#ifndef RENDER_H
#define RENDER_H

/* The 3D renderer as the rest of the game sees it (render.c; the inner loops
 * are the assembly of render_asm.c and render_skin_asm.c). The game queues
 * nodes; once a frame the queue is turned into GPU primitives in the
 * ordering table. */

#include "common.h"
#include <psxsdk/libgte.h>

/* ---- the draw queue ---- */

/* The nodes to draw this frame, as pointers to SceneNode (or to a record
 * that starts with one). To queue a node, store it at g_render_queue_end and
 * step that on. */
#define RENDER_QUEUE_SIZE 640
extern void *g_render_queue[RENDER_QUEUE_SIZE];
extern void **g_render_queue_end;

/* The scene's ordering table: the display's, from its fifth entry, so that
 * what is drawn over the scene can go in front of it; and the display's as
 * the scene found it. Entry 0 is nearest the camera and the last farthest:
 * what is linked in there is done before anything else of the scene. */
#define RENDER_OT_FARTHEST 0xFFF
extern u32 *g_render_ot;
extern u32 *g_render_ot_base;

/* Works out the view, draws every queued node into the ordering table and
 * empties the queue. */
extern void render_Flush(void);
extern void render_FlushCutscene(void);

/* How near the camera things are still drawn (g_scene_settings.clip_near).
 * Usually what is very near is left out; render_AllowNear, or
 * render_SetClipNear(0), has the next flush draw it, as the first-person
 * view always does. */
extern void render_SetClipNear(s32 clip);
extern s32 render_GetClipNear(void);

/* The ordering-table entry for a depth, with the depth scaled down by
 * `shift` first (the assembly's form of render_DepthToOt). */
extern s32 render_DepthToOtShifted(s32 depth, s32 shift);

/* Draws `count` flat textured triangles or quads from `records`, as a
 * stage tile's are drawn and the surf's: the corners are projected points
 * on the scratchpad, `vertices` the packed vertices they came from. The low
 * byte of `flags` is how many times each is cut into four smaller ones,
 * which a tile near the camera asks for; the surf passes 0. Returns the end
 * of the records. */
extern s16 *render_EmitSplitFT3(
    s32 count, s16 *vertices, s16 *records, s32 flags);
extern s16 *render_EmitSplitFT4(
    s32 count, s16 *vertices, s16 *records, s32 flags);

/* RENDER_OPT_...: what the renderer does besides drawing. Only the fog bit
 * is read in C. */
#define RENDER_OPT_FOG 8
#define RENDER_OPT_DEFAULT 0x23
extern s32 g_render_options;

/* Three angles applied on top of the camera's: a tilt of the whole view. */
extern SVECTOR g_render_view_tilt;

/* The colour models are outlined in (model_SetOutline): written into the
 * instructions of the outline routines themselves. */
extern void render_SetOutlineColor(s32 color);
/* The four instructions that load the outline colour (two ways to load the
 * same register), and the eight places in the outline routines they are
 * written to. */
extern void * const g_render_outline_color_sites[8];
extern s32 g_render_outline_color_lui;
extern s32 g_render_outline_color_ori;
extern s32 g_render_outline_color_lui2;
extern s32 g_render_outline_color_ori2;

/* ---- the assembly (render_asm.c, render_skin_asm.c) ---- */

/* Walks the queue and draws every node in it. */
extern void render_DrawQueue(void);
/* Draws a fighter's body: the list of the bones' matrices, the skinned
 * mesh, the view, and how far depths are scaled down. */
extern void render_DrawSkinnedMesh(
    s32 matrices, s32 mesh, s32 view, s32 depth_shift);

/* The routines of the handler tables: the comment at the tables in render.c
 * says what each is. They take the count of primitives in $a0 and the mesh
 * cursor in $s0, and are only called by the assembly. */
extern void render_EmitPackedFT3();
extern void render_EmitPackedFT4();
extern void render_EmitPackedGT3();
extern void render_EmitPackedGT4();
extern void render_EmitSkinFT3();
extern void render_EmitSkinFT4();
extern void render_EmitSkinGT3();
extern void render_EmitSkinGT4();
extern void render_EmitTwoSidedFT3();
extern void render_EmitTwoSidedFT4();
extern void render_EmitTwoSidedGT3();
extern void render_EmitTwoSidedGT4();
extern void render_EmitUnlitFT3();
extern void render_EmitUnlitFT4();
extern void render_EmitFT3();
extern void render_EmitFT4();
extern void render_EmitTintedFT3();
extern void render_EmitTintedFT4();
extern void render_EmitGT3();
extern void render_EmitGT4();
extern void render_EmitBlackF3();
extern void render_EmitBlackF4();
extern void render_EmitSkinOutlineFT3();
extern void render_EmitSkinOutlineFT4();
extern void render_EmitSkinOutlineGT3();
extern void render_EmitSkinOutlineGT4();
extern void render_EmitOutlineFT3();
extern void render_EmitOutlineFT4();
extern void render_EmitOutlineGT3();
extern void render_EmitOutlineGT4();
extern void render_EmitTinyF3();
extern void render_EmitTinyF4();
extern void render_EmitClothFT3();
extern void render_EmitClothFT4();
extern void render_EmitClothGT3();
extern void render_EmitClothGT4();

/* ---- the renderer's scratchpad ----
 * While it draws, the renderer keeps its state in the first words of the
 * scratchpad, where the assembly reads it. */
/* the place in the mesh being walked (tex_ShiftMeshAtCursor) */
#define RENDER_MESH_CURSOR (*(u16 **)SCRATCHPAD)
/* how far a depth is shifted down before it becomes a table index */
#define RENDER_DEPTH_SHIFT (*(s32 *)(SCRATCHPAD + 0x08))
/* the ordering table being drawn into */
#define RENDER_OT (*(s32 *)(SCRATCHPAD + 0x0C))
/* the fog starts at this entry */
#define RENDER_FOG_OT_INDEX (*(s32 *)(SCRATCHPAD + 0x10))
/* -1 on odd frames */
#define RENDER_FRAME_MASK (*(s32 *)(SCRATCHPAD + 0x14))
/* nothing nearer than this entry is drawn */
#define RENDER_OT_CUTOFF (*(s32 *)(SCRATCHPAD + 0x18))
/* the handler table in use */
#define RENDER_HANDLERS (*(s32 *)(SCRATCHPAD + 0x1C))
/* the view the skinned mesh is drawn in */
#define RENDER_VIEW_MATRIX ((MATRIX *)(SCRATCHPAD + 0x3A0))

/* ---- mesh pointer tables (reloc_) ----
 * A loaded file's meshes are found through a table of pointers at its start:
 * a word holding the count (bit 15 set once the offsets have been made
 * pointers), then one word per mesh. The renderer keeps the address and size
 * of the table of each block in use, by enum RelocBlock (resource.h), and a
 * node names its mesh as a block and an index. */
#define RELOC_BLOCK_COUNT 20
/* Set in a table's count once its offsets have been made pointers. */
#define RELOC_DONE 0x8000
extern s32 *g_reloc_tables[RELOC_BLOCK_COUNT];

extern void reloc_ClearCounts(void);
/* Turns a table's offsets into pointers and registers it as `block`. */
extern void reloc_RelocateBlock(s32 *table, s16 block);
extern void reloc_UnrelocateBlock(s16 block);
/* The file moved by `byte_offset`. No prototype: resource_OnStageMoved
 * calls it with one argument. */
extern void reloc_RebaseBlock();
/* The marks of a list of entries to keep: one for each entry (anything
 * negative drops it), and the end after the last. */
#define RELOC_KEEP 1
#define RELOC_DROP (-1)
#define RELOC_LIST_END (-2)
/* Cut a file down to some of its entries: the ones listed after the count,
 * or the ones a list of s16 marks. The kept entries are moved up, or copied
 * to `dest`; the end of the result is returned. */
extern s32 reloc_KeepSelectedEntries(s32 *file, s16 *selected);
extern s32 reloc_CopySelectedEntries(s32 src, s32 *dest, s16 *selected);

/* ---- the native port (docs/PORT.md, "The renderer in C") ---- */
#if RB_PORT
#include "light.h"
#include "node.h"

/* Where the walk over the queue is. The renderer's assembly keeps these
 * in registers from one routine to the next; the port's C keeps them
 * here. */
typedef struct {
    void **queue;        /* the next node of the queue */
    SceneNode *node;     /* the node being drawn */
    u8 *mesh;            /* the place in its mesh: the mesh cursor */
    void (**handlers)(); /* the handler table in use */
    s32 scale_shift;     /* bits the node's coordinates are scaled down by */
    MATRIX *view;        /* the view matrix */
    LightSetup *lights;  /* the node's light setup */
} RenderWalk;

extern RenderWalk g_render_walk;

/* The colour outlined models are drawn in, 0xRRGGBB, which the assembly
 * has in its instructions (render_SetOutlineColor). */
#define RENDER_OUTLINE_GRAY 0x808080
#define RENDER_OUTLINE_COLOR_MASK 0xFFFFFF
extern s32 g_render_outline_color;

/* The tiny model's ordering table (model_tiny.c). */
extern u32 *g_render_tiny_ot;

/* What the assembly names and no header declared: the view matrix
 * render_Flush works out, the colour of each normal of the body being
 * drawn, and the routine that fills that table (it returns the end of the
 * list of normals). */
extern MATRIX g_render_view_matrix;
extern CVECTOR g_render_skin_colors[500];
extern u8 *render_LightSkin(u8 *normals, MATRIX **matrices);

/* What the walk over the queue reaches in render.c: the two tables it
 * chooses for some parts, and the C that draws a fighter's body. */
extern void (*g_render_packed_handlers[32])();
extern void (*g_render_tiny_handlers[32])();
extern void render_DrawSkinned(
    s32 matrices, s32 mesh, s16 light_setup_index, u8 *node);
/* The projection of a part of format 3, which does nothing
 * (render_asm.c). */
extern void render_ProjectFormat3(void);

#endif

/* Used only inside render.c. */
extern void (*g_render_handlers[])();

#endif /* RENDER_H */
