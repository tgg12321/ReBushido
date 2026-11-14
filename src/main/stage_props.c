/* Stage props: the things on three stages that a blade cuts through (the
 * bamboo of the bamboo thicket, the posts of the lumberyard, and the two
 * signs of the bus stop). A prop stands as one of the stage's pieces until it
 * is cut; then that piece is given the mesh of what remains, and the prop's own
 * node plays the stage's falling motion, lies for a moment and fades away.
 * The fight decides when a prop is cut (stage_CutProp); the stage's hook
 * steps them every frame. */
#include "common.h"
#include "bb2.h"

static StageProps g_stage_props;

/* How many props each of the three stages has. The bus stop's are two signs
 * on posts beside the road. They fall in fixed directions and are not
 * removed once fallen; but the stage's list of obstacles is empty, so no
 * blade finds them and they stand. */
#define BAMBOO_COUNT 17
#define BUS_STOP_SIGN_COUNT 2
#define LUMBER_POST_COUNT 10

/* The props' meshes in their block. */
#define PROP_MESH_FALLING 0
#define PROP_MESH_REMAINS 1

/* A frame of the falling motion: a position and three angles. In the
 * lumberyard the motion is twice as long, the second half moving what
 * remains. */
#define PROP_FRAME_SIZE 12

/* How much a fallen prop fades each frame: it is gone in 64. */
#define PROP_FADE_STEP (ONE / 64)

/* Called while the stage's file is being loaded. `motion` is the falling
 * motion in the file (a frame count, then the frames), `mesh_block` the
 * props' mesh block, and `buffer` where the props' nodes go. Returns the end of
 * the nodes. */
s32 stage_SetUpProps(s16 *motion, s16 mesh_block, s32 buffer) {
    s16 stage;
    g_stage_props.mesh_block = mesh_block;
    g_stage_props.frame_count = *motion++;
    g_stage_props.frames = (s32)motion;
    g_stage_props.props = buffer;
    stage = resource_GetStage();
    g_stage_props.stage = stage;
    switch (stage) {
    case STAGE_BAMBOO_THICKET:
        g_stage_props.count = BAMBOO_COUNT;
        break;
    case STAGE_BUS_STOP:
        g_stage_props.count = BUS_STOP_SIGN_COUNT;
        break;
    case STAGE_LUMBERYARD:
        g_stage_props.count = LUMBER_POST_COUNT;
        break;
    }
    buffer += g_stage_props.count * sizeof(StageProp);
    return buffer;
}

/* Makes every prop a standing one, at the place of the stage piece it stands
 * as: the first pieces of the stage, in order. */
void stage_InitProps(void) {
    StageNode *piece;
    StageProp *prop;
    s32 i;

    g_stage_props.remains = (s32)(piece = stage_GetNodes());
    prop = (StageProp *)g_stage_props.props;
    for (i = 0; i < g_stage_props.count; prop++, piece++, i++) {
        prop->node.kind = NODE_KIND_ROTATED;
        prop->node.draw_flags = 0;
        prop->node.mesh = PROP_MESH_FALLING;
        prop->node.mesh_block = g_stage_props.mesh_block;
        prop->node.rotation_order = ROTATION_ORDER_ZYX;
        prop->node.parent = NULL;
        prop->node.light_setup_index = LIGHT_SCENERY;
        prop->node.rot.vx = 0;
        prop->node.rot.vy = 0;
        prop->node.rot.vz = 0;
        prop->node.local.t[0] = piece->node.world.t[0];
        prop->node.local.t[1] = piece->node.world.t[1];
        prop->node.local.t[2] = piece->node.world.t[2];
        prop->node.matrix_state = NODE_MATRIX_REBUILD;
        prop->frame = PROP_STANDING;
    }
}

/* Steps every prop that has been cut and queues it for drawing: the next
 * frame of the falling motion, turned to the way the prop falls, or once the
 * motion is over a step of the fade. */
void stage_UpdateProps(void) {
    /* the angles of the turn to the way the prop falls, then the frame's
     * position */
    SVECTOR work;
    MATRIX turn;
    StageProp *prop;
    StageProp *falling;
    s32 i;
    s32 frame;
    s32 yaw;
    s16 *motion;
    s16 cos_yaw;
    s16 sin_yaw;
    s32 cos_z;
    s32 sin_z;
    s32 cos_x;
    s32 sin_x;
    s32 last;
    s32 fade;
    void **queue;

    prop = (StageProp *)g_stage_props.props;
    for (i = 0; i < g_stage_props.count; prop++, i++) {
        frame = prop->frame;
        if (frame < 0) {
            continue;
        }
        if (frame < g_stage_props.frame_count) {
            work.vx = 0;
            yaw = prop->yaw;
            work.vz = 0;
            motion = (s16 *)(g_stage_props.frames + frame * PROP_FRAME_SIZE);
            work.vy = yaw;
            math_RotMatrixZYX(&work, &turn);
            work.vx = *motion++;
            work.vy = *motion++;
            work.vz = *motion++;
            prop->node.rot.vx = *motion++;
            prop->node.rot.vy = *motion++;
            prop->node.rot.vz = *motion++;
            prop->node.matrix_state = NODE_MATRIX_REBUILD;
            node_UpdateWorldMatrix(&prop->node);
            MulMatrix2(&turn, &prop->node.world);
            cos_yaw = g_math_sin_table[(yaw + ANGLE_TURN / 4) & ANGLE_MASK];
            cos_z = cos_yaw * work.vz;
            sin_yaw = g_math_sin_table[yaw & ANGLE_MASK];
            sin_z = sin_yaw * work.vz;
            cos_x = cos_yaw * work.vx;
            sin_x = sin_yaw * work.vx;
            prop->node.world.t[0] += (sin_z + cos_x) >> 12;
            prop->node.world.t[1] += work.vy;
            prop->node.world.t[2] += (cos_z - sin_x) >> 12;
            frame++;
            if (frame >= g_stage_props.frame_count) {
                prop->fade = ONE;
            }
            prop->frame = frame;
            if (g_stage_props.stage == STAGE_LUMBERYARD) {
                /* What remains moves too, by the motion's second half. */
                /* FAKE: a StageNode pointer of its own for the stage piece */
                falling = prop;
                prop = (StageProp *)((StageNode *)g_stage_props.remains + i);
                last = g_stage_props.frame_count - 1;
                motion = (s16 *)(g_stage_props.frames +
                                 (frame + last) * PROP_FRAME_SIZE);
                work.vx = *motion++;
                work.vy = *motion++;
                work.vz = *motion++;
                prop->node.rot.vx = *motion++;
                prop->node.rot.vy = *motion++;
                prop->node.rot.vz = *motion++;
                prop->node.matrix_state = NODE_MATRIX_REBUILD;
                node_UpdateWorldMatrix(&prop->node);
                prop->node.world.t[0] += work.vx;
                prop->node.world.t[1] += work.vy;
                prop->node.world.t[2] += work.vz;
                prop = falling;
            }
        } else {
            fade = prop->fade - PROP_FADE_STEP;
            if (fade < 0) {
                fade = 0;
            }
            if (fade == 0 && g_stage_props.stage != STAGE_BUS_STOP) {
                prop->frame = PROP_GONE;
            }
            prop->fade = fade;
        }
        queue = g_render_queue_end;
        g_render_queue_end = queue + 1;
        *queue = prop;
    }
}

/* Prop `index` has been cut: it starts to fall towards `yaw`, and the stage
 * piece it stood as becomes what remains of it. Does nothing if it was cut
 * before. */
void stage_CutProp(s32 index, s32 yaw) {
    StageProp *prop;

    if (index >= g_stage_props.count) {
        return;
    }

    prop = (StageProp *)g_stage_props.props + index;
    if (prop->frame != PROP_STANDING) {
        return;
    }

    switch (g_stage_props.stage) {
    case STAGE_BUS_STOP:
        if (index == 0) {
            yaw = ANGLE_TURN / 2;
        }
        if (index == 1) {
            yaw = 0;
        }
        break;
    case STAGE_LUMBERYARD:
        yaw = 0;
        break;
    }
    prop->yaw = yaw;
    prop->frame = 0;
    /* FAKE: a StageNode pointer of its own for the stage piece */
    prop = (StageProp *)((StageNode *)g_stage_props.remains + index);
    prop->node.mesh = PROP_MESH_REMAINS;
    prop->node.mesh_block = g_stage_props.mesh_block;
    prop->node.rot.vy = yaw;
    prop->node.kind = NODE_KIND_ROTATED;
    prop->node.parent = NULL;
    prop->node.rot.vx = 0;
    prop->node.rot.vz = 0;
    prop->node.matrix_state = NODE_MATRIX_DONE;
    g_math_rot_matrix_funcs[prop->node.rotation_order](
        &prop->node.rot, &prop->node.world);
    if (g_stage_props.stage == STAGE_LUMBERYARD) {
        prop->node.local.t[0] = prop->node.world.t[0];
        prop->node.local.t[1] = prop->node.world.t[1];
        prop->node.local.t[2] = prop->node.world.t[2];
    }
}

/* The stage's block moved by `delta` bytes. */
void stage_RebaseProps(s32 delta) {
    reloc_RebaseBlock(g_stage_props.mesh_block, delta);
    g_stage_props.props += delta;
    g_stage_props.frames += delta;
}
