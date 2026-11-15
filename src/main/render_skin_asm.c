/* Two more pieces of the renderer's assembly (render.h), which sit apart
 * from the rest of it (render_asm.c) in the executable: lighting a fighter's
 * body, and the instructions that hold the outline colour. */
#define INCLUDE_ASM_USE_MACRO_INC 1
#include "common.h"
#include "include_asm.h"
#include "bb2.h"

#if RB_PORT
/* What the port's C of render_LightSkin names: the lights turned into a
 * bone's space, on the scratchpad; the bone number that ends the list;
 * and how many normals are lit at a time, which is also how many numbers
 * a normal is. */
#define SKIN_LIGHTS ((MATRIX *)(SCRATCHPAD + 0x20))
#define SKIN_LIGHT_END (-1)
#define SKIN_LIGHT_RUN 3
#endif

/* The colour of each normal of the body being drawn. */
CVECTOR g_render_skin_colors[500];

/* Works out the colour of every normal of a skinned mesh, into
 * g_render_skin_colors: the normals come in runs, each turned by the matrix
 * of another bone before the lights of g_light_current fall on it. The
 * routines that draw the body (render_EmitSkin...) take the corners' colours
 * from that table. */
/* Assembly: trapping (15); 0x38 bytes of frame for seven saved registers; its
 * jumps are written as branches that always go (bgez $zero). */
#if RB_PORT
/* In C: the normals are lit three at a time, so a run's last three may
 * take in what follows them in the list and write colours past the run's;
 * both cursors are then put back, and the next run writes over those. */
u8 *render_LightSkin(u8 *normals, MatrixSlot *matrices) {
    s16 *list = (s16 *)normals;
    u32 *colors = (u32 *)g_render_skin_colors;
    s32 bone;
    s32 count;
    s32 i;

    port_GteWriteData(PORT_GTE_RGBC, 0);
    while ((bone = *list++) != SKIN_LIGHT_END) {
        math_MulMatrix(
            &g_light_current->lmat, PTR(matrices[bone]), SKIN_LIGHTS);
        port_GteLoadControl(PORT_GTE_LIGHT, SKIN_LIGHTS, PORT_GTE_MATRIX_WORDS);
        count = *list++;
        do {
            for (i = 0; i < SKIN_LIGHT_RUN; i++) {
                port_GteWriteData(
                    PORT_GTE_VXY0 + 2 * i, (u16)list[0] | ((u32)list[1] << 16));
                port_GteWriteData(PORT_GTE_VZ0 + 2 * i, (u32)list[2]);
                list += SKIN_LIGHT_RUN;
            }
            count -= SKIN_LIGHT_RUN;
            port_GteCommand(PORT_GTE_CMD_NCT);
            port_GteStoreData(PORT_GTE_RGB0, colors, SKIN_LIGHT_RUN);
            colors += SKIN_LIGHT_RUN;
        } while (count > 0);
        colors += count;
        list += 3 * count;
    }
    return (u8 *)list;
}
#else
INCLUDE_ASM("asm/funcs", render_LightSkin);
#endif

/* Not a function: the two pairs of instructions (lui, ori) that load the
 * outline's colour, as the outline routines have them. render_SetOutlineColor
 * puts the colour into copies of these and writes them over the routines'
 * own. Four instructions that are never run, after a routine's return:
 * nothing a compiler lays down. */
#if RB_PORT
/* In C: the colour itself, which the outline routines read. It starts as
 * the gray the listings' instructions hold. */
s32 g_render_outline_color = RENDER_OUTLINE_GRAY;
#else
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
#endif
