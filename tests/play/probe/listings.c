/* The one routine the probe runs that it cannot take from an object of
 * the game's: math_RotMatrixZYX is in render_asm.c with the whole
 * renderer, so its listing is included here, as render_asm.c includes
 * it. The others come with the game's own objects (the Makefile's
 * PROBE_GAME_OBJS). */
#define INCLUDE_ASM_USE_MACRO_INC 1
#include "common.h"
#include "include_asm.h"

INCLUDE_ASM("asm/funcs", math_RotMatrixZYX);
