/* What the renderer's objects (render_asm.c, render_skin_asm.c and
 * render.c of the game, linked into the probe as they are) name and the
 * probe does not have: the variables, which the cases set, and the
 * functions of the game's C that the walk over the queue calls or that
 * render.c's own functions call. None of the functions here is under
 * test. */
#include "common.h"
#include "bb2.h"

u32 *g_disp_prim_cursor = NULL;
u32 *g_render_tiny_ot = NULL;
s32 g_disp_frame_count = 0;
LightSetup g_light_setups[LIGHT_SETUP_COUNT] = {{{{0}}}};
MATRIX g_model_tiny_matrix = {{{0}}};
SceneSettings g_scene_settings = {0};
SceneNode *g_node_identity_root_ptr = NULL;
SVECTOR g_render_view_tilt = {0};

/* render_Flush's, which no case runs. */
void light_SetShadowMatrix(void) {}

void math_RotMatrixZXY(SVECTOR *angles, MATRIX *matrix) {}

void render_SetClipNear(s32 clip) {}

s32 render_GetClipNear(void) { return 0; }

s32 stage_IsFirstPerson(void) { return 0; }

u32 stage_GetFogOtIndex(void) { return 0; }
