/* What single stages have of their own: for each stage that has something,
 * a hook that sets it up when the stage is loaded and one that steps it
 * every frame the stage is drawn (g_stage_hooks, in stage_data.c). The things
 * themselves are elsewhere (props, the surf, scrolling textures); only the
 * castle rooftop's backdrop is here. Compiled with small data. */
#include "common.h"
#include "bb2.h"

/* A stage's far background: one node kept in front of the camera. */
static PlainNode g_stage_backdrop_node;

/* The camera's pitch and yaw, as stage_CalcViewAngles left them. */
static s16 g_stage_view_angles[2];

/* Works out the camera's pitch and yaw from its three angles: the way it
 * looks, as a turn about the vertical and a tilt up or down. The stage's
 * draw calls it once a frame; nothing reads the result. */
s16 *stage_CalcViewAngles(void) {
    SVECTOR ahead;
    VECTOR view_dir;
    MATRIX matrix;
    s16 yaw;

    math_RotMatrixYXZ(&g_node_camera_root_ptr->rot, &matrix);
    ahead.vx = 0;
    ahead.vy = 0;
    ahead.vz = ONE;
    ApplyMatrix(&matrix, &ahead, &view_dir);
    yaw = ratan2(view_dir.vx, view_dir.vz);
    /* its length along the ground */
    view_dir.vz =
        (g_math_sin_table[(yaw + ANGLE_TURN / 4) & ANGLE_MASK] * view_dir.vz +
         g_math_sin_table[yaw & ANGLE_MASK] * view_dir.vx) >>
        12;
    g_stage_view_angles[0] = -ratan2(view_dir.vy, view_dir.vz);
    g_stage_view_angles[1] = yaw;
    return g_stage_view_angles;
}

/* The bus stop: its two signs (stage_props.c). */
void stage_InitBusStop(void) { stage_InitProps(); }

void stage_UpdateBusStop(void) { stage_UpdateProps(); }

/* The lost bridge: a scrolling texture (tex.h). */
void stage_InitLostBridge(void) { tex_InitScroll(0, 0); }

void stage_UpdateLostBridge(void) { tex_QueueScroll(0); }

/* The bamboo thicket: its bamboo. */
void stage_InitBamboo(void) { stage_InitProps(); }

void stage_UpdateBamboo(void) { stage_UpdateProps(); }

/* The Kimigaura coast: the surf (wave.h). */
void wave_InitCallback(void) { wave_Init(); }

void wave_UpdateCallback(void) { wave_Update(); }

/* The castle rooftop: a backdrop. One prop is kept a fixed way off from the
 * camera, always in the same direction of the world, and turned to face the
 * camera: something far away that never gets nearer. It is the full moon
 * over the stage's tiled roof. It is as far off as the scene reaches. */
#define BACKDROP_DISTANCE STAGE_FAR

void stage_InitBackdrop(void) {
    node_Init(&g_stage_backdrop_node.node);
    g_stage_backdrop_node.node.rotation_order = ROTATION_ORDER_YXZ;
}

/* Queues the backdrop: at the camera's place moved BACKDROP_DISTANCE along
 * the world's z, turned by the pitch and yaw of that direction as the camera
 * sees it. Not while the stage file's several areas are all loaded (story
 * mode, which moves between them): only once it has settled on the first. */
void stage_DrawBackdrop(void) {
    SVECTOR ahead;
    VECTOR direction;
    MATRIX facing;
    MATRIX view;
    s16 yaw;
    s16 pitch;
    s32 horizontal_length;
    SceneNode *node;

    if (resource_HasStageAreas()) {
        return;
    }

    ahead.vx = 0;
    ahead.vy = 0;
    ahead.vz = BACKDROP_DISTANCE;
    ApplyMatrix(&g_node_camera_root.node.world, &ahead, &direction);

    yaw = ratan2(direction.vx, direction.vz);

    horizontal_length =
        (g_math_sin_table[(yaw + ANGLE_TURN / 4) & ANGLE_MASK] * direction.vz +
         g_math_sin_table[yaw & ANGLE_MASK] * direction.vx) >>
        12;
    direction.vz = horizontal_length;
    pitch = -ratan2(direction.vy, horizontal_length);

    node = &g_stage_backdrop_node.node;
    node->rot.vx = pitch;
    node->rot.vy = yaw;
    node->world.t[0] = g_node_camera_root.node.world.t[0];
    node->world.t[1] = g_node_camera_root.node.world.t[1];
    node->world.t[2] = g_node_camera_root.node.world.t[2] + BACKDROP_DISTANCE;
    g_math_rot_matrix_funcs[ROTATION_ORDER_YXZ](&node->rot, &facing);
    g_math_rot_matrix_funcs[ROTATION_ORDER_ZYX](
        &g_node_camera_root.node.rot, &view);
    MulMatrix0(&view, &facing, &node->world);

    {
        void **queue = g_render_queue_end;
        g_render_queue_end = queue + 1;
        *queue = node;
    }
}

/* Under the lost bridge: a scrolling texture, the stream's water. */
void stage_InitUnderLostBridge(void) { tex_InitScroll(1, 0); }

void stage_UpdateUnderLostBridge(void) { tex_QueueScroll(0); }

/* The lumberyard: its timber. */
void stage_InitLumber(void) { stage_InitProps(); }

void stage_UpdateLumber(void) { stage_UpdateProps(); }

/* Hooks that do nothing: the Shainto gate's, and the pair that the stage
 * outside the Meikyokan, the purple cavern and team battle's second court
 * share. */
void stage_InitShaintoGate(void) {}

void stage_UpdateShaintoGate(void) {}

void stage_InitNothing(void) {}

void stage_UpdateNothing(void) {}
