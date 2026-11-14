/* Two more pieces of the renderer's assembly (render.h), which sit apart
 * from the rest of it (render_asm.c) in the executable: lighting a fighter's
 * body, and the instructions that hold the outline colour. */
#define INCLUDE_ASM_USE_MACRO_INC 1
#include "common.h"
#include "include_asm.h"
#include "bb2.h"

/* The colour of each normal of the body being drawn. */
CVECTOR g_render_skin_colors[500];

/* Works out the colour of every normal of a skinned mesh, into
 * g_render_skin_colors: the normals come in runs, each turned by the matrix
 * of another bone before the lights of g_light_current fall on it. The
 * routines that draw the body (render_EmitSkin...) take the corners' colours
 * from that table. */
/* Assembly: trapping (15); 0x38 bytes of frame for seven saved registers; its
 * jumps are written as branches that always go (bgez $zero). */
INCLUDE_ASM("asm/funcs", render_LightSkin);

/* Not a function: the two pairs of instructions (lui, ori) that load the
 * outline's colour, as the outline routines have them. render_SetOutlineColor
 * puts the colour into copies of these and writes them over the routines'
 * own. Four instructions that are never run, after a routine's return:
 * nothing a compiler lays down. */
INCLUDE_ASM("asm/funcs", g_render_outline_color_lui);

/* The places in the outline routines where render_SetOutlineColor writes
 * the colour: jump labels of the listings (render_asm.c). The __asm__ gives
 * C the label's name; it is not an instruction. */
extern u8 L80050C0C[] __asm__(".L80050C0C");
extern u8 L80050DAC[] __asm__(".L80050DAC");
extern u8 L80050FB4[] __asm__(".L80050FB4");
extern u8 L80051154[] __asm__(".L80051154");
extern u8 L80051358[] __asm__(".L80051358");
extern u8 L800514FC[] __asm__(".L800514FC");
extern u8 L800516FC[] __asm__(".L800516FC");
extern u8 L80051894[] __asm__(".L80051894");

void * const g_render_outline_color_sites[8] = {
    L80050C0C, L80050DAC, L80050FB4, L80051154,
    L80051358, L800514FC, L800516FC, L80051894,
};
