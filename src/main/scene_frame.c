/* A frame of the 3D scene: setting the engine up, beginning a frame (the
 * camera, the lights, an empty draw queue) and ending it (the stage, the
 * fog, drawing the queue); the shadow matrix; and a few small things about
 * the view and the nodes. Compiled with small data. */
#include "common.h"
#include "bb2.h"

SceneSettings g_scene_settings;

static MATRIX g_light_shadow_matrix;
static MATRIX g_scene_squashed_view;

/* Four bytes that no code names (their size is the gap), and a word
 * sys_InitEngineState clears that nothing reads. */
static s32 g_scene_unused_word;
static s32 g_scene_unused_init_word;
/* How far back the camera is this frame, for the stage's fog. */
static s32 g_scene_eye_distance;

/* Starts the counter of scan lines, counting freely. */
void sys_StartHblankCounter(void) {
    SetRCnt(RCntCNT1, -1, RCntMdNOINTR);
    StartRCnt(RCntCNT1);
}

/* Reads the counter and does nothing with it. Nothing calls it. */
static void sys_ReadHblankCounter(void) { GetRCnt(RCntCNT1); }

/* Resets everything the engine keeps between frames. Run at boot and again
 * after the movie player returns. */
void sys_InitEngineState(void) {
    ndata_StartReadInf();
    arena_Reset();
    math_InitRotMatrixFuncs();
    reloc_ClearCounts();
    node_InitViewRoots();
    light_InitDefaults();
    stage_InitFog();
    model_ClearAll();
    cloth_Init();
    g_scene_settings.grayscale = 0;
    g_scene_settings.unused_flag = 0;
    g_scene_settings.clip_near = 1;
    g_scene_settings.far_view.value = 0;
    g_scene_settings.far_shift = 2;
    g_scene_settings.unk0A = 1;
    g_scene_settings.first_person = 0;
    g_render_options = RENDER_OPT_DEFAULT;
    g_scene_unused_init_word = 0;
}

/* Begins a frame of the scene: empties the draw queue, takes the display's
 * ordering table from its fifth entry on, and puts the camera `eye_distance`
 * back from `focus_point`, looking at it along `camera_angles`. With no
 * focus point the camera stays where it is. Then the lights and the cloth's
 * colliders are made ready for the frame. */
void scene_Begin(Vec3 *focus_point, SVECTOR *camera_angles, s32 eye_distance) {
    s32 eye_offset[3];
    s32 straight_back[3];
    SVECTOR angles;
    MATRIX rotation;

    g_render_queue_end = g_render_queue;
    g_render_ot_base = g_disp_ot;
    g_scene_count++;
    g_render_ot = g_disp_ot + 4;

    if (focus_point) {
        g_node_camera_root.node.rot.vx = -camera_angles->vx;
        g_node_camera_root.node.rot.vy = -camera_angles->vy;
        g_node_camera_root.node.rot.vz = -camera_angles->vz;

        straight_back[0] = 0;
        straight_back[1] = 0;
        straight_back[2] = -eye_distance;

        angles.vx = -camera_angles->vx;
        angles.vy = -camera_angles->vy;
        angles.vz = -camera_angles->vz;

        g_math_rot_matrix_funcs[ROTATION_ORDER_ZYX](&angles, &rotation);

        ApplyMatrixLV(&rotation, (VECTOR *)straight_back, (VECTOR *)eye_offset);

        {
            g_node_camera_root.node.local.t[0] = eye_offset[0] + focus_point->x;
            g_node_camera_root.node.local.t[1] = eye_offset[1] + focus_point->y;
            g_node_camera_root.node.local.t[2] = eye_offset[2] + focus_point->z;
        }

        node_SetInverseRotation(&g_node_camera_root.node);
        scene_SetSquashedView();
        stage_ResetCellFlags();

        g_scene_eye_distance = eye_distance;
    }

    light_CalcMatrix(&g_light_setups[LIGHT_FIGHTER_1]);
    light_CalcMatrix(&g_light_setups[LIGHT_FIGHTER_2]);
    light_CalcMatrix(&g_light_setups[LIGHT_SCENERY]);
    light_ResetPointLight();
    cloth_ClearSceneColliders();
    cloth_SendSceneColliders();
}

/* Ends the frame: queues the stage (bit 0 of `draw_stage`), lets the point
 * light fall on what it should, and has the renderer draw the queue. */
void scene_End(s32 draw_stage) {
    if (draw_stage & 1) {
        scene_DrawStage(g_scene_eye_distance);
    }
    light_ApplyPointLightTargets();
    render_Flush();
}

/* Returns 0. Nothing calls it. */
static s32 scene_ReturnZero(void) { return 0; }

/* ---- two questions about a model (model.h) ---- */

/* The list of the world matrices of the bones of the model in slot `slot`,
 * from bone 1 up, or NULL if the slot is empty. */
MATRIX **model_GetBoneMatrices(s32 slot) {
    Model *model = model_Get(slot);
    if (model) {
        return model->bone_matrices;
    }
    return NULL;
}

/* The model's bones themselves. Nothing calls it. */
static ModelNode *model_GetBones(s32 slot) {
    Model *model = model_Get(slot);
    if (model) {
        return model->bones;
    }
    return NULL;
}

/* ---- how near the camera things are drawn (render.h) ---- */

/* The next flush draws what is very near the camera as well. Nothing calls
 * it. */
static void render_AllowNear(void) { g_scene_settings.clip_near = 0; }

void render_SetClipNear(s32 clip) {
    if (clip) {
        g_scene_settings.clip_near = 1;
    } else {
        g_scene_settings.clip_near = 0;
    }
}

s32 render_GetClipNear(void) { return g_scene_settings.clip_near; }

/* Nothing calls it. */
static void render_ResetOptions(void) { g_render_options = RENDER_OPT_DEFAULT; }

/* ---- the stage ---- */

/* Queues the stage's visible cells, then the fog for the stage loaded. */
void scene_DrawStage(s32 eye_distance) {
    stage_Draw(eye_distance);
    {
        s32 stage = resource_GetStage();
        stage_QueueFog(stage, eye_distance);
    }
}

void stage_Load(s32 stage, s32 staging_addr) {
    resource_LoadStage(stage, staging_addr);
    light_SetStage(stage);
    stage_BuildFog();
}

void *fight_GetStartPositions(void) { return g_fight_start_positions; }

/* ---- shadows (light.h) ----
 * A shadow is its caster's shape slid along the sun's direction until it
 * lies on the ground: x and z move by the height above the ground times the
 * slope of the light, and y becomes the ground's. */

/* Works out the shadow matrix from the first fighter's sun: called once a
 * frame, before the queue is drawn. */
void light_SetShadowMatrix(void) {
    s32 sun_x = g_light_setups[LIGHT_FIGHTER_1].lmat.m[LIGHT_SUN][0] << 12;
    s32 sun_y = g_light_setups[LIGHT_FIGHTER_1].lmat.m[LIGHT_SUN][1];
    s32 x_shear = sun_x / sun_y;
    s32 z_shear =
        (g_light_setups[LIGHT_FIGHTER_1].lmat.m[LIGHT_SUN][2] << 12) / sun_y;
    g_light_shadow_matrix.m[0][2] = 0;
    g_light_shadow_matrix.m[1][0] = 0;
    z_shear = -(s16)z_shear;
    g_light_shadow_matrix.m[1][1] = 0;
    g_light_shadow_matrix.m[1][2] = 0;
    g_light_shadow_matrix.m[2][0] = 0;
    g_light_shadow_matrix.m[0][0] = ONE;
    g_light_shadow_matrix.m[2][2] = ONE;
    x_shear = -(s16)x_shear;
    g_light_shadow_matrix.m[0][1] = x_shear;
    g_light_shadow_matrix.m[2][1] = z_shear;
}

/* The matrix of the shadow that something with matrix `src` casts on
 * ground at height `ground_y`. The assembly that draws shadow nodes calls
 * it. */
void light_ProjectShadow(MATRIX *src, MATRIX *out, s32 ground_y) {
    math_MulMatrix(&g_light_shadow_matrix, src, out);
    out->t[0] =
        src->t[0] +
        (((src->t[1] - ground_y) * g_light_shadow_matrix.m[0][1]) >> 12);
    out->t[1] = ground_y;
    out->t[2] =
        src->t[2] +
        (((src->t[1] - ground_y) * g_light_shadow_matrix.m[2][1]) >> 12);
}

/* The same for the first light of any light setup. Nothing calls it. */
static void light_ProjectShadowFor(
    s32 light_setup_index, MATRIX *src, MATRIX *out, s32 ground_y) {
    MATRIX light_projection;

    light_projection.m[0][0] = ONE;
    light_projection.m[0][1] =
        -((g_light_setups[light_setup_index].lmat.m[0][0] << 12) /
          g_light_setups[light_setup_index].lmat.m[0][1]);
    light_projection.m[0][2] = 0;
    light_projection.m[1][0] = 0;
    light_projection.m[1][1] = 0;
    light_projection.m[1][2] = 0;
    light_projection.m[2][0] = 0;
    light_projection.m[2][1] =
        -((g_light_setups[light_setup_index].lmat.m[0][2] << 12) /
          g_light_setups[light_setup_index].lmat.m[0][1]);
    light_projection.m[2][2] = ONE;
    math_MulMatrix(&light_projection, src, out);
    out->t[0] =
        src->t[0] + (((src->t[1] - ground_y) * light_projection.m[0][1]) >> 12);
    out->t[1] = ground_y;
    out->t[2] =
        src->t[2] + (((src->t[1] - ground_y) * light_projection.m[2][1]) >> 12);
}

/* ---- the view ---- */

/* Keeps a copy of the camera's matrix squashed to half height, as the
 * picture is (scene.h). */
void scene_SetSquashedView(void) {
    g_scene_squashed_view = g_node_camera_root.node.world;
    g_scene_squashed_view.m[1][0] >>= 1;
    g_scene_squashed_view.m[1][1] >>= 1;
    g_scene_squashed_view.m[1][2] >>= 1;
}

MATRIX *scene_GetSquashedView(void) { return &g_scene_squashed_view; }

/* Makes `node` a plain node at the origin: unturned, with no parent, drawing
 * the first of the stage's own props under the scenery's light. */
void node_Init(SceneNode *node) {
    node->mesh_block = RELOC_STAGE_PROPS;
    node->rotation_order = ROTATION_ORDER_ZYX;
    node->mesh = 0;
    node->kind = NODE_KIND_ROTATED;
    node->draw_flags = 0;
    node->parent = NULL;
    node->light_setup_index = LIGHT_SCENERY;
    node->rot.vx = 0;
    node->rot.vy = 0;
    node->rot.vz = 0;
    g_math_rot_matrix_funcs[node->rotation_order](&node->rot, &node->local);
    node->local.t[2] = 0;
    node->local.t[1] = 0;
    node->local.t[0] = 0;
    node->world = node->local;
}
