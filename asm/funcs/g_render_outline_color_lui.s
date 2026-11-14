# Not code that runs: the two pairs of instructions that load the outline's
# colour, as the outline routines have them. render_SetOutlineColor puts the
# colour into copies of these and writes the copies over the routines' own.
dlabel g_render_outline_color_lui
    /* 44DF4 800545F4 804C083C */ .word 0x3C084C80 /* lui $t0, 0x4C80 */
enddlabel g_render_outline_color_lui
dlabel g_render_outline_color_ori
    /* 44DF8 800545F8 80800835 */ .word 0x35088080 /* ori $t0, $t0, 0x8080 */
enddlabel g_render_outline_color_ori
dlabel g_render_outline_color_lui2
    /* 44DFC 800545FC 8048083C */ .word 0x3C084880 /* lui $t0, 0x4880 */
enddlabel g_render_outline_color_lui2
dlabel g_render_outline_color_ori2
    /* 44E00 80054600 80800835 */ .word 0x35088080 /* ori $t0, $t0, 0x8080 */
enddlabel g_render_outline_color_ori2
