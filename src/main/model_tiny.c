/* The tiny model (model.h). In the first-person view the fighter being
 * looked through cannot be seen, so it is drawn a second time as a small
 * figure: a simple model of the same character whose parts take the matrices
 * of the real model's bones, seen by a camera of its own that circles the
 * fighter. (A reading of the code; the figure has not been looked at in the
 * running game.) Compiled with small data. */
#include "common.h"
#include "bb2.h"

MATRIX g_model_tiny_matrix;

static s32 g_model_tiny_slot;  /* whose figure the tiny model is */
static s32 g_model_tiny_nodes; /* its nodes: ModelNode[TINY_NODE_COUNT] */

#define TINY_NODE_COUNT 17  /* the body, a weapon, a second thing held */
#define TINY_MESH_WEAPON 15 /* + weapon id: one for each of six weapons */
#define TINY_MESH_OBJECT 21 /* the second thing held */
#define TINY_CAMERA_DISTANCE 6000
#define TINY_OT_SIZE 0x100

/* The figure's own ordering table, one for each frame's buffer. */
u32 *g_render_tiny_ot;
u32 g_render_tiny_ot_db[2][TINY_OT_SIZE];

/* Loads the tiny model of the model in slot `slot` and makes its nodes after
 * the meshes, in the same block. Returns 0 if there is no model there or it
 * has no tiny model. */
s32 model_LoadTiny(s32 slot) {
    Model *model;
    s32 i;
    u8 *file;
    s32 meshes_size;
    u8 *nodes;
    ModelNode *node;

    model = model_Get(slot);
    if (!model) {
        return 0;
    }
    g_model_tiny_slot = slot;
    i = g_model_tiny_ids[model->model_id];
    if (i == MODEL_TINY_NONE) {
        return 0;
    }
    file = (u8 *)resource_LoadTinyModel(i);
    /* the file's third word is where its meshes end */
    nodes = file + ((*(u32 *)(file + 8) >> 2) << 2);
    meshes_size = nodes - file;
    g_model_tiny_nodes = (s32)nodes;
    node = (ModelNode *)nodes;
    /* FAKE: `i`, the tiny model's number above, is reused to count */
    for (i = 0; i < TINY_NODE_COUNT; node++, i++) {
        node->node.mesh = i;
        node->node.mesh_block = RELOC_TINY_MODEL;
        node->node.kind = NODE_KIND_TINY;
        node->node.draw_flags = 0;
        node->node.light_setup_index = slot;
    }
    resource_ResizeTinyModel(meshes_size + TINY_NODE_COUNT * sizeof(ModelNode));
    return 1;
}

/* The tiny model's block moved. */
void model_RebaseTiny(s32 delta) { g_model_tiny_nodes += delta; }

/* Queues the figure in the pose its fighter has this frame. `yaw` is where
 * the figure's camera stands around the fighter; `weapon_id` the weapon it
 * holds, or -1 for none; `has_object` whether it holds a second thing. The
 * figure is drawn into its own ordering table, which is linked in at the
 * near end of the frame's, so that it is drawn after the scene. */
void model_QueueTiny(s32 yaw, s32 weapon_id, s32 has_object) {
    MATRIX turned;
    SVECTOR work; /* the camera's place, then its angles */
    s32 bone;
    s32 old_link; /* what the frame's table began with */
    s16 *bones;
    void **queue;
    MatrixSlot *matrices;
    ModelNode *node;

    matrices = model_GetBoneMatrices(g_model_tiny_slot);
    if (!matrices) {
        return;
    }
    if (weapon_id >= WEAPON_ID_FIRST_GUN) {
        weapon_id = -1;
    }

    /* The camera: TINY_CAMERA_DISTANCE out from the body's root, looking
     * back at it. Its y and z come from the next two bones' matrices and
     * not the root's: the original's behaviour. */
    work.vx = TINY_CAMERA_DISTANCE;
    work.vy = 0;
    work.vz = 0;
    work.vz = (g_math_sin_table[yaw & ANGLE_MASK] * work.vx) >> 12;
    work.vx =
        (g_math_sin_table[(yaw + ANGLE_TURN / 4) & ANGLE_MASK] * work.vx) >> 12;
    node = (ModelNode *)ADDR(g_model_tiny_nodes);
    ApplyMatrix(PTR(matrices[0]), &work, (VECTOR *)g_model_tiny_matrix.t);
    g_model_tiny_matrix.t[0] += PTR(matrices[0])->t[0];
    g_model_tiny_matrix.t[1] += PTR(matrices[1])->t[1];
    g_model_tiny_matrix.t[2] += PTR(matrices[2])->t[2];

    work.vx = 0;
    work.vy = 3 * ANGLE_TURN / 4 - yaw;
    work.vz = 0;
    bones = g_model_tiny_bones;
    math_RotMatrixZYX(&work, &turned);
    math_MulMatrix(PTR(matrices[0]), &turned, &turned);
    /* the camera's rotation is the inverse: the transpose */
    g_model_tiny_matrix.m[0][0] = turned.m[0][0];
    g_model_tiny_matrix.m[0][1] = turned.m[1][0];
    g_model_tiny_matrix.m[0][2] = turned.m[2][0];
    g_model_tiny_matrix.m[1][0] = turned.m[0][1];
    g_model_tiny_matrix.m[1][1] = turned.m[1][1];
    g_model_tiny_matrix.m[1][2] = turned.m[2][1];
    g_model_tiny_matrix.m[2][0] = turned.m[0][2];
    g_model_tiny_matrix.m[2][1] = turned.m[1][2];
    g_model_tiny_matrix.m[2][2] = turned.m[2][2];

    while ((bone = *bones++) >= 0) {
        node->node.world = *PTR(matrices[bone]);
        *g_render_queue_end++ = node;
        node++;
    }
    /* the list starts at bone 1: these are the two hand bones */
    if (weapon_id >= 0) {
        node->node.world = *PTR(matrices[MODEL_HAND_BONE - 1]);
        queue = g_render_queue_end;
        node->node.mesh = weapon_id + TINY_MESH_WEAPON;
        g_render_queue_end = queue + 1;
        *queue = node;
        node++;
    }
    if (has_object) {
        node->node.world = *PTR(matrices[MODEL_HAND_BONE]);
        queue = g_render_queue_end;
        node->node.mesh = TINY_MESH_OBJECT;
        g_render_queue_end = queue + 1;
        *queue = node;
    }

    g_render_tiny_ot = g_render_tiny_ot_db[g_disp_frame_count & 1];
    ClearOTagR(g_render_tiny_ot, TINY_OT_SIZE);
    old_link = g_render_ot[0];
    /* a link is the low 24 bits of an address */
    g_render_ot[0] = (u32)(g_render_tiny_ot + (TINY_OT_SIZE - 1)) & 0xFFFFFF;
    *g_render_tiny_ot = old_link;
}
