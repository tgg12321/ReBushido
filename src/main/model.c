/* A fighter's model: loading it and building its skeleton, then, from frame
 * to frame, posing the skeleton and queueing it for drawing, the outline it
 * is drawn as in the first-person view, its extra textures and tints, and
 * the world-matrix update every node uses. The face is in model_face.c.
 * Compiled with small data. */
#include "common.h"
#include "bb2.h"

/* Defined further down. */
static void model_SetMatrixTables(Model *model);
static void model_BuildShadows(Model *model);
static s32 g_model_texture_requests[MODEL_SLOT_COUNT];
static void model_LoadExtraTextures(
    s32 slot, s32 model_id, s32 groups, s32 textures);
static void model_SaveTintCluts(Model *model);

/* Defined further down. */
static s32 g_model_pose_channels[17];
static u8 g_model_extra_texture_masks[64];
static s16 *g_model_tint_clut_lists[6];
static u8 g_model_tint_clut_list_ids[64];
static void model_FinishLoad(Model *model);
static void model_BuildSkeleton(Model *model);
static void model_AssignMeshes(Model *model);
static void model_RebaseNodeParents(s32 delta, ModelNode *node, s32 count);

/* In g_model_tint_clut_list_ids: the model cannot be tinted. */
#define MODEL_TINT_NONE 0xFF

/* The loaded models, by slot (model_Get): room for four, of which the three
 * slots are used. */
static Model *g_models[MODEL_SLOT_COUNT + 1];

/* The colours model_SaveTintCluts keeps for model_TintOpponent to put back,
 * and whose they are. */
static struct {
    s16 model_id;
    s16 pad;
    u16 cluts[8][CLUT_COLORS];
} g_model_tint_saved;

/* A model's file is a table of offsets: after the count, to these and then
 * to the textures. A file with MODEL_FILE_WITH_VAB entries or more has a
 * sound bank's header before the textures and its samples after them. */
enum {
    MODEL_FILE_MESHES = 1,
    MODEL_FILE_MESH_BONES = 2, /* which bone draws which mesh */
    MODEL_FILE_CLOTH = 3,      /* the cloth's parameters */
    MODEL_FILE_SHADOWS = 4,    /* the shadows' meshes */
    MODEL_FILE_WITH_VAB = 6,
};

/* The skeleton: a halfword that nothing reads, then the bones. */
typedef struct {
    s16 unk_00; /* -1 in the data; no code reads it */
    ModelBone bones[MODEL_BONE_COUNT];
} ModelBoneTable;

/* This file's data, defined after the functions. */
static ModelBoneTable g_model_bones;

/* ---- small things about a model's nodes ---- */

/* Adds a shadow with mesh 2 of `block`, cast by bone 6, to a list of shadow
 * nodes. Nothing calls it. */
static void model_AddShadow(ModelNode *bones, ModelNode *shadow, s16 block) {
    while (shadow->node.mesh != -1) {
        shadow++;
    }
    shadow->node.mesh = 2;
    shadow->node.kind = NODE_KIND_SHADOW;
    shadow->node.draw_flags = 0;
    SET_PTR(shadow->node.parent, &bones[6].node); /* an arm's forearm */
    shadow->node.matrix_state = NODE_MATRIX_DONE;
    shadow->node.rotation_order = ROTATION_ORDER_ZYX;
    shadow->node.light_setup_index = 0;
    shadow->node.mesh_block = block;
    shadow->extra = 0;
    shadow[1].node.mesh = -1;
}

/* The node that draws mesh `mesh` of the model in slot `slot`. Nothing calls
 * it. */
static ModelNode *model_GetMeshNode(s32 slot, s32 mesh) {
    Model *model = model_Get(slot);
    return PTR(model->mesh_nodes[mesh]);
}

/* Says how high the ground is under a list of shadows. */
static void model_SetShadowHeight(ModelNode *shadow, s32 ground_y) {
    while (shadow->node.mesh != -1) {
        shadow->extra = ground_y;
        shadow++;
    }
}

/* ---- loading ---- */

void model_ClearAll(void) {
    s32 i;
#if RB_PORT
    /* two on two's copies of the models are gone with them (port_duo.h) */
    port_DuoForget();
#endif
    for (i = 0; i < MODEL_SLOT_COUNT; i++) {
        g_models[i] = NULL;
        g_model_texture_requests[i] = 0;
    }
}

/* Loads model `model_id` into slot `slot` and makes it ready to pose: its
 * file's parts put where they belong, its skeleton, the nodes of its meshes
 * and shadows, its cloth, and the stage's fog on it if it is the one fogged.
 * With a staging address the file is read there first. */
Model *model_Load(s32 slot, s32 model_id, s32 staging_addr) {
    Model *model;
    model = resource_LoadModel(slot, model_id, staging_addr);
    g_models[slot] = model;
    model_FinishLoad(model);
    model_BuildSkeleton(model);
    model_AssignMeshes(model);
    cloth_SetupModel(model, 1);
    cloth_RequestRebuild(model);
    model_BuildShadows(model);
    stage_BuildModelFog();
    return model;
}

/* Does what a model's file needs once it is read: opens its sound bank,
 * fixes up its mesh tables, uploads its textures, and gives back the part of
 * its arena block that held what has now gone to the sound and video
 * hardware. Does nothing if the file was already there. */
static void model_FinishLoad(Model *model) {
    s32 *file;
    s32 *textures;
    s32 count;
    s32 *kept_end;
    s32 kept_size;

    if (model->load_status == MODEL_STATUS_DONE) {
        return;
    }

    model->load_status = MODEL_STATUS_DONE;

    if (model->staging) {
        file = (s32 *)ADDR(model->staging);
    } else {
        file = (s32 *)ADDR(model->data);
    }

    count = file[0];
    textures = (s32 *)ADDR((s32)file + ALIGN4(file[count]));

    /* The second slot's bank stays open while the archive's pack is. */
    if (!ndata_IsPackLoaded() || model->slot != 1) {
        snd_CloseModelVab(model->slot);
    }

    if (count >= MODEL_FILE_WITH_VAB) {
        kept_end = (s32 *)ADDR((s32)file + ALIGN4(file[count - 1]));
        kept_end = (s32 *)ADDR(
            (s32)kept_end +
            snd_LoadVab((SoundVab *)kept_end,
                        model->slot * SND_VAB_MODELS_STEP + SND_VAB_MODEL,
                        (s32)file + ALIGN4(file[count + 1])));

        if (!kept_end) {
            sys_Trap();
        }

        model->load_flags.word |= MODEL_LOAD_HAS_VAB;
    } else {
        kept_end = textures;
    }

    kept_size = (s32)kept_end - (s32)file;

    if (model->staging) {
        mem_MoveWords(model->staging, model->data, kept_size);
        file = (s32 *)ADDR(model->data);
        /* MODEL_LOAD_HAS_VAB, tested as a bit */
        if ((model->load_flags.word >> 1) & 1) {
            snd_RebaseVab((s32)file - model->staging,
                          model->slot * SND_VAB_MODELS_STEP + SND_VAB_MODEL);
        }
    }

    {
        s32 *meshes = (s32 *)ADDR((s32)file + ALIGN4(file[MODEL_FILE_MESHES]));
        s32 *shadow_meshes;
        shadow_meshes =
            (s32 *)ADDR((s32)file + ALIGN4(file[MODEL_FILE_SHADOWS]));
        model->cloth_params = (s32)file + ALIGN4(file[MODEL_FILE_CLOTH]);
        reloc_RelocateBlock(meshes, model->mesh_block);
        reloc_RelocateBlock(shadow_meshes, model->shadow_block);
    }

    /* The textures. The first slot's go where they were made for and the
     * second's two pages to the right, its meshes shifted to match. The
     * model the stage's fog falls on has its colour tables loaded a second
     * time, into the fog's block, and its meshes pointed at that. */
#if RB_REMASTER_VIEW
    /* which TIMs are the fighter's, for a mod's picture of his whole pages
     * (port_remaster.h); a fogged model's faces name the fog's rows, and
     * the tables as loaded are the plain copy below them */
    port_RemasterBeginFighterTextures(model->slot);
    port_RemasterNoteFighterTextures(model->slot, 0);
#endif
    switch (model->slot) {
    case 0:
        tex_LoadGroup(textures, 0);
        if (stage_GetFoggedModel() == 0) {
            tex_ShiftBlock(
                model->mesh_block, 0, 0, MODEL_FOG_CLUT_DX, MODEL_FOG_CLUT_DY);
#if RB_REMASTER_VIEW
            port_RemasterNoteFighterTextures(
                model->slot, MODEL_FOG_CLUT_DY - MODEL_FOG_PLAIN_CLUT_DY);
#endif
            tex_LoadGroupAtSplit(
                textures, 0, 0, 0, MODEL_FOG_CLUT_DX, MODEL_FOG_PLAIN_CLUT_DY);
        }
        break;
    case 1:
        tex_LoadGroupAt(textures, 0, MODEL_SECOND_SLOT_DX, 0);
        if (stage_GetFoggedModel() == 1) {
            tex_ShiftBlock(model->mesh_block, MODEL_SECOND_SLOT_DX, 0,
                           MODEL_FOG_CLUT_DX, MODEL_FOG_CLUT_DY);
#if RB_REMASTER_VIEW
            port_RemasterNoteFighterTextures(
                model->slot, MODEL_FOG_CLUT_DY - MODEL_FOG_PLAIN_CLUT_DY);
#endif
            tex_LoadGroupAtSplit(textures, 0, MODEL_SECOND_SLOT_DX, 0,
                                 MODEL_FOG_CLUT_DX, MODEL_FOG_PLAIN_CLUT_DY);
        } else {
            tex_ShiftBlockPages(model->mesh_block, MODEL_SECOND_SLOT_PAGES, 0,
                                MODEL_SECOND_SLOT_PAGES, 0);
        }
        model_SaveTintCluts(model);
        break;
    case 2:
        break;
    }
#if RB_REMASTER_VIEW
    port_RemasterNoteFighterTextures(-1, 0);
#endif

    DrawSync(0);
    model_LoadExtraTextures(
        model->slot, model->model_id, g_model_texture_requests[model->slot],
        (s32)textures);

    model->load_flags.word =
        (model->load_flags.word &
         ~(MODEL_TEXTURE_GROUP_MASK << MODEL_LOAD_TEXTURES_SHIFT)) |
        ((g_model_texture_requests[model->slot] & MODEL_TEXTURE_GROUP_MASK)
         << MODEL_LOAD_TEXTURES_SHIFT);
    g_model_texture_requests[model->slot] = 0;

    DrawSync(0);
    SET_PTR(model->cloth, (void *)ADDR(model->data + kept_size));
    resource_ResizeModelData(model->slot, kept_size);
}

/* Builds the model's bones from the shared skeleton, scaled to the
 * character. */
static void model_BuildSkeleton(Model *model) {
    s16 *scale;
    s32 count;
    s16 model_id;

    scale = g_model_scales;
    count = 0;
    while (*scale++ != -1) {
        count++;
    }

    model_id = model->model_id;
    if (model_id < count) {
        model->scale = g_model_scales[model_id];
    } else {
        model->scale = ONE;
    }

    {
        ModelNode *bones = model->bones;
        ModelNode *bone = model->bones;
        s32 i;

        for (i = 0; i < MODEL_BONE_COUNT; i++) {
            s16 parent;
            bone->node.kind = NODE_KIND_ROTATED;
            bone->node.draw_flags = 0;
            bone->node.rotation_order = ROTATION_ORDER_ZYX;
            bone->node.mesh = -1;
            parent = g_model_bones.bones[i].parent;
            if (parent != -1) {
                SET_PTR(bone->node.parent, &bones[parent].node);
            } else {
                SET_PTR(bone->node.parent, NULL);
            }
            bone->node.local.t[0] =
                (g_model_bones.bones[i].offset[0] * model->scale) >> 12;
            bone->node.local.t[1] =
                (g_model_bones.bones[i].offset[1] * model->scale) >> 12;
            bone->node.local.t[2] =
                (g_model_bones.bones[i].offset[2] * model->scale) >> 12;
            bone->node.rot.vx = 0;
            bone->node.rot.vy = 0;
            bone->node.rot.vz = 0;
            bone->node.matrix_state = NODE_MATRIX_REBUILD;
            bone->node.light_setup_index = model->light_setup_index;
            bone->node.mesh_block = model->mesh_block;
            bone++;
        }
    }

    model_SetMatrixTables(model);
}

/* Fills in the two lists of pointers to the bones' world matrices: in the
 * order of the body's mesh sections, and in the bones' own. The bones are
 * named here as the skeleton's comment has them, by their shape. */
static void model_SetMatrixTables(Model *model) {
    s32 i;
    ModelNode *bone;

    i = 0;
    /* one arm: the forearm */
    SET_PTR(model->skin_matrices[0], &model->bones[6].node.world);
    /* the upper arm */
    SET_PTR(model->skin_matrices[1], &model->bones[5].node.world);
    /* the shoulder */
    SET_PTR(model->skin_matrices[2], &model->bones[4].node.world);
    /* the other arm: the forearm */
    SET_PTR(model->skin_matrices[3], &model->bones[10].node.world);
    /* the upper arm */
    SET_PTR(model->skin_matrices[4], &model->bones[9].node.world);
    /* the shoulder */
    SET_PTR(model->skin_matrices[5], &model->bones[8].node.world);
    /* the head */
    SET_PTR(model->skin_matrices[6], &model->bones[3].node.world);
    /* the trunk */
    SET_PTR(model->skin_matrices[7], &model->bones[2].node.world);
    /* one leg: the shin */
    SET_PTR(model->skin_matrices[8], &model->bones[17].node.world);
    /* the thigh */
    SET_PTR(model->skin_matrices[9], &model->bones[16].node.world);
    /* the other leg: the shin */
    SET_PTR(model->skin_matrices[10], &model->bones[14].node.world);
    /* the thigh */
    SET_PTR(model->skin_matrices[11], &model->bones[13].node.world);
    /* the hips */
    SET_PTR(
        model->skin_matrices[12], &model->bones[MODEL_WAIST_BONE].node.world);
    /* the body's root */
    SET_PTR(model->skin_matrices[13], &model->bones[1].node.world);
    /* the first arm's hand */
    SET_PTR(model->skin_matrices[14], &model->bones[7].node.world);
    /* the other arm's hand */
    SET_PTR(model->skin_matrices[15], &model->bones[11].node.world);
    /* the first leg's foot */
    SET_PTR(model->skin_matrices[16], &model->bones[18].node.world);
    /* the other leg's foot */
    SET_PTR(model->skin_matrices[17], &model->bones[15].node.world);
    /* what one hand holds */
    SET_PTR(
        model->skin_matrices[18], &model->bones[MODEL_HAND_BONE].node.world);
    /* what the other holds */
    SET_PTR(model->skin_matrices[19],
            &model->bones[MODEL_HAND_BONE + 1].node.world);
    bone = &model->bones[1];
    do {
        SET_PTR(model->bone_matrices[i], &bone->node.world);
        bone++;
        i += 1;
    } while (i < MODEL_MATRIX_COUNT);
}

/* Reads the file's list of (mesh, bone) pairs and gives each bone the mesh it
 * draws. A bone that already has one gets an extra node, a copy of itself,
 * for each mesh more. */
static void model_AssignMeshes(Model *model) {
    s32 bone_used[MODEL_BODY_BONES];
    ModelNode *extra;
    ModelNode *body;
    s32 *file;
    u16 *pair;
    s32 i;

    extra = model->extra_nodes;
    body = &model->bones[1];
    file = (s32 *)ADDR(model->data);
    pair = (u16 *)((u8 *)file + file[MODEL_FILE_MESH_BONES]);

    for (i = 0; i < MODEL_BODY_BONES; i++) {
        bone_used[i] = 0;
    }
    for (i = 0; i < MODEL_MESH_COUNT; i++) {
        SET_PTR(model->mesh_nodes[i], NULL);
    }

    while (*pair != 0xFFFF) {
        s32 mesh = *pair++;
        s32 bone = *pair++;

        if (!bone_used[bone]) {
            body[bone].node.mesh = mesh;
            bone_used[bone] = 1;
            SET_PTR(model->mesh_nodes[mesh], &body[bone]);
        } else {
            *extra = body[bone];
            extra->node.mesh = mesh;
            extra->extra = (s32)&body[bone];
            SET_PTR(model->mesh_nodes[mesh], extra);
            extra++;
        }
    }
    extra->extra = 0;
}

/* Makes a shadow node for every body bone that has a shadow mesh. */
static void model_BuildShadows(Model *model) {
    s32 i;
    ModelNode *shadow;
    /* FAKE: &model->bones[i + 1].node and g_model_bones.bones[i].shadow_mesh */
    ModelNode *body;
    s16 *mesh;

    shadow = model->shadows;
    body = &model->bones[1];
    for (i = 0; i < MODEL_BODY_BONES; i++) {
        mesh = &g_model_bones.bones[i].shadow_mesh;
        if (*mesh != -1) {
            shadow->node.mesh = *mesh;
            shadow->node.kind = NODE_KIND_SHADOW;
            shadow->node.draw_flags = 0;
            shadow->node.rotation_order = ROTATION_ORDER_ZYX;
            SET_PTR(shadow->node.parent, &body[i].node);
            shadow->node.matrix_state = NODE_MATRIX_DONE;
            shadow->node.light_setup_index = 0;
            shadow->node.mesh_block = model->shadow_block;
            shadow->extra = 0;
            shadow++;
        }
    }
    shadow->node.mesh = -1;
}

/* ---- from frame to frame ---- */
/* A decoded motion frame is rows of three s16. Row 0 places the body; rows 1
 * to 17 are the angles of the body's bones, in the motion data's order (see
 * g_model_pose_channels); then a position and angles for each hand bone. */
#define FRAME_ROW_SIZE 6
#define FRAME_HAND_ROWS 18

/* Places the model of slot `slot` at `position` turned by `rotation`, poses
 * its body, works out every bone's world matrix and queues the model's nodes
 * for drawing: the body, the bones that draw a mesh of their own, the extra
 * nodes and the shadows. `ground_y` is the height its shadows fall at. */
void model_Update(s32 slot, s32 pose_mode, s32 *position, s16 *rotation,
                  s16 *frame, s32 ground_y) {
    Model *model;
    ModelNode *bone;
    ModelNode *body;
    ModelNode *hands;
    s32 i;

    if (!g_models[slot]) {
        return;
    }
    model = g_models[slot];

    model->bones[0].node.rot.vx = rotation[0];
    model->bones[0].node.rot.vy = rotation[1];
    model->bones[0].node.rot.vz = rotation[2];

    model->bones[0].node.local.t[0] = position[0];
    /* FAKE: `bone` is the root here, and each extra node's bone further down */
    bone = model->bones;
    model->bones[0].node.local.t[1] = position[1];
    body = &model->bones[1];
    model->bones[0].node.local.t[2] = position[2];

    hands = &model->bones[MODEL_HAND_BONE];

    switch (pose_mode) {
    case MODEL_POSE_FRAME: {
        s32 *channel;
        /* FAKE: the frame is read through this copy of the parameter */
        s16(*rows)[3];
        i = 1;
        channel = g_model_pose_channels;
        rows = (s16(*)[3])frame;

        body->node.local.t[0] = 0;
        body->node.local.t[1] = 0;
        body->node.local.t[2] = 0;
        body->node.rot.vx = 0;
        body->node.rot.vy = 0;
        body->node.rot.vz = 0;

        /* The motion data's y and z run the other way from the world's. */
        do {
            ModelNode *posed;
            posed = &body[i];
            posed->node.rot.vx = rows[*channel][0];
            posed->node.rot.vy = -rows[*channel][1];
            posed->node.rot.vz = -rows[*channel][2];
            i++;
            channel++;
        } while (i < MODEL_BODY_BONES);

        for (i = 0; i < MODEL_BODY_BONES; i++) {
            body[i].node.matrix_state = NODE_MATRIX_REBUILD;
        }

        hands->node.local.t[0] = rows[FRAME_HAND_ROWS][0];
        hands->node.local.t[1] = -rows[FRAME_HAND_ROWS][1];
        hands->node.local.t[2] = -rows[FRAME_HAND_ROWS][2];
        hands->node.rot.vx = rows[FRAME_HAND_ROWS + 1][0];
        hands->node.rot.vy = -rows[FRAME_HAND_ROWS + 1][1];
        hands->node.rot.vz = -rows[FRAME_HAND_ROWS + 1][2];

        g_math_rot_matrix_funcs[ROTATION_ORDER_ZYX](
            &hands->node.rot, &hands->node.local);

        hands[1].node.local.t[0] = rows[FRAME_HAND_ROWS + 2][0];
        hands[1].node.local.t[1] = -rows[FRAME_HAND_ROWS + 2][1];
        hands[1].node.local.t[2] = -rows[FRAME_HAND_ROWS + 2][2];
        hands[1].node.rot.vx = rows[FRAME_HAND_ROWS + 3][0];
        hands[1].node.rot.vy = -rows[FRAME_HAND_ROWS + 3][1];
        hands[1].node.rot.vz = -rows[FRAME_HAND_ROWS + 3][2];

        g_math_rot_matrix_funcs[ROTATION_ORDER_ZYX](
            &hands[1].node.rot, &hands[1].node.local);
        break;
    }
    case MODEL_POSE_BLENDED:
        body->node.rot.vx = 0;
        body->node.rot.vy = 0;
        body->node.rot.vz = 0;
        body->node.matrix_state = NODE_MATRIX_REBUILD;
        body->node.local.t[0] = 0;
        body->node.local.t[2] = 0;
        break;
    /* five more modes that do nothing */
    case 2:
        break;
    case 3:
        break;
    case 4:
        break;
    case 5:
        break;
    case 6:
        break;
    }

    {
        ModelNode *each;
        i = 0;
        each = body;
        body->node.local.t[1] = (body->node.local.t[1] * model->scale) >> 12;
        bone->node.matrix_state = NODE_MATRIX_REBUILD;
        do {
            node_UpdateWorldMatrix(&each->node);
            i++;
            each++;
        } while (i < MODEL_BODY_BONES);
    }

    /* The body's root draws the whole skinned body. */
    i = 1;
    body->node.kind = NODE_KIND_SKINNED;
    NODE_EXTRA(body) = (s32)model->skin_matrices;
    {
        void **queue;
        ModelNode *each;
        queue = g_render_queue_end;
        each = &body[1];
        body->node.mesh = 0;
        g_render_queue_end = queue + 1;
        *queue = body;

        do {
            if (each->node.mesh >= 0) {
                void **queue2;
                queue2 = g_render_queue_end;
                g_render_queue_end = queue2 + 1;
                *queue2 = each;
            }
            i++;
            each++;
        } while (i < MODEL_BODY_BONES);
    }

    {
        ModelNode *node;
        node = model->extra_nodes;
        for (;;) {
            void **queue;
            bone = (ModelNode *)ADDR(node->extra);
            if (!bone) {
                break;
            }
            node->node.world = bone->node.world;
            queue = g_render_queue_end;
            g_render_queue_end = queue + 1;
            *queue = node;
            node++;
        }

        node = model->shadows;
        if (model->shadows[0].node.mesh != -1) {
            do {
                void **queue;
                queue = g_render_queue_end;
                g_render_queue_end = queue + 1;
                *queue = node;
                node++;
            } while (node->node.mesh != -1);
        }
    }

    model_SetShadowHeight(model->shadows, ground_y);
    model->ground_y = ground_y;
    cloth_SetGround(model, ground_y);
    cloth_UpdateModel(model);
    light_SetPointLightTarget(slot, (s32)body->node.world.t);
}

/* Sets the local matrices of the body's bones and the hand bones to a pose
 * between two decoded frames, `weight` of the way from the first to the
 * second. model_Update is then called with MODEL_POSE_BLENDED. `scratch` is
 * room for two matrices. */
void model_BlendPoses(
    s32 slot, u8 *from_frame, u8 *to_frame, s32 weight, MATRIX *scratch) {
    s32 i = 1;
    s32 *channel = g_model_pose_channels;
    Model *model = g_models[slot];
    SVECTOR angles;
    ModelNode *body;
    MATRIX *second;
    s32 row;
    u16 *from;
    u16 *to;
    ModelNode *hand;
    body = &model->bones[1];
    do {
        row = (*channel) * FRAME_ROW_SIZE;
        from = (u16 *)ADDR(row + (s32)from_frame);
        angles.vx = from[0];
        angles.vy = -from[1];
        angles.vz = -from[2];
        math_RotMatrixZYX(&angles, scratch);
        channel++;
        to = (u16 *)ADDR(row + (s32)to_frame);
        angles.vx = to[0];
        angles.vy = -to[1];
        angles.vz = -to[2];
        math_RotMatrixZYX(&angles, scratch + 1);
        math_LerpRotation(scratch, scratch + 1, weight, &body[i].node.local);
        body[i].node.matrix_state = NODE_MATRIX_LOCAL_SET;
        i++;
    } while (i < MODEL_BODY_BONES);
    from_frame += FRAME_HAND_ROWS * FRAME_ROW_SIZE;
    to_frame += FRAME_HAND_ROWS * FRAME_ROW_SIZE;
    i = MODEL_BODY_BONES;
    hand = &body[MODEL_BODY_BONES];
    /* FAKE: named here, where the loop above writes scratch + 1 */
    second = scratch + 1;
    /* FAKE: do { ... } while (i < MODEL_BODY_BONES + 2); */
loop2:
    math_LerpFramePoint(
        (s16 *)from_frame, (s16 *)to_frame, weight, (s32)hand->node.local.t);
    from_frame += FRAME_ROW_SIZE;
    to_frame += FRAME_ROW_SIZE;
    angles.vx = *((u16 *)from_frame);
    from_frame += 2;
    angles.vy = -(*((u16 *)from_frame));
    from_frame += 2;
    angles.vz = -(*((u16 *)from_frame));
    from_frame += 2;
    math_RotMatrixZYX(&angles, scratch);
    angles.vx = *((u16 *)to_frame);
    to_frame += 2;
    angles.vy = -(*((u16 *)to_frame));
    to_frame += 2;
    angles.vz = -(*((u16 *)to_frame));
    to_frame += 2;
    math_RotMatrixZYX(&angles, second);
    math_LerpRotation(scratch, second, weight, &hand->node.local);
    hand->node.matrix_state = NODE_MATRIX_DONE;
    hand++;
    i++;
    if (i < MODEL_BODY_BONES + 2) {
        goto loop2;
    }
}

#if RB_PORT
/* In C: the port's outline routines read the colour from a variable
 * (render.h), so there are no instructions to rewrite. */
void render_SetOutlineColor(s32 color) {
    g_render_outline_color = color & RENDER_OUTLINE_COLOR_MASK;
}
#else
/* Sets the colour outlined models are drawn in, 0xRRGGBB. The outline
 * routines are assembly with the colour as a constant in their code, so the
 * instructions that load it are rewritten: four pairs of places, each place
 * a `lui` that takes the high byte and an `ori` that takes the rest. */
void render_SetOutlineColor(s32 color) {
    s32 **site = (s32 **)g_render_outline_color_sites;
    s32 pair = 0;
    s32 mask8 = ~0xFF;
    s32 high = (color >> 16) & 0xFF;
    s32 mask16 = ~0xFFFF;
    s32 low = color & 0xFFFF;
    s32 lui = (g_render_outline_color_lui & mask8) | high;
    s32 ori = (g_render_outline_color_ori & mask16) | low;
    s32 lui2 = (g_render_outline_color_lui2 & mask8) | high;
    s32 ori2 = (g_render_outline_color_ori2 & mask16) | low;
    do {
        s32 *first = *site;
        site++;
        pair++;
        first[0] = lui;
        first[1] = ori;
        {
            s32 *second = *site;
            second[0] = lui2;
            second[1] = ori2;
        }
        site++;
    } while (pair < 4);
}
#endif

/* The arena moved the model of slot `slot` by `delta` bytes: moves every
 * pointer the model holds into itself. */
void model_Rebase(s32 slot, s32 delta) {
    Model **entry;
    Model *model;
    s32 i;
#if RB_PORT
    /* the slot's own model, not a copy two on two has put there
     * (port_duo.h) */
    port_DuoModelsHome();
#endif
    entry = &g_models[slot];
    model = (Model *)((u8 *)*entry + delta);
    *entry = model;
    model_RebaseNodeParents(delta, model->bones, MODEL_BONE_COUNT);
    model_RebaseNodeParents(delta, model->shadows, MODEL_SHADOW_NODES);
    model_RebaseNodeParents(delta, model->extra_nodes, MODEL_EXTRA_NODES);
    {
        ModelNode *node = model->extra_nodes;
        do {
            s32 bone = node->extra;
            if (bone) {
                node->extra = bone + delta;
            } else {
                break;
            }
            node++;
        } while (1);
    }
    for (i = 0; i < MODEL_MESH_COUNT; i++) {
        ModelNode *node = PTR(model->mesh_nodes[i]);
        if (node) {
            SET_PTR(model->mesh_nodes[i], (ModelNode *)((u8 *)node + delta));
        }
    }
    model_SetMatrixTables(model);
}

/* Moves the parent pointers of `count` nodes by `delta` bytes: the model they
 * belong to, and so the nodes they point at, has moved in the arena. */
static void model_RebaseNodeParents(s32 delta, ModelNode *node, s32 count) {
    s32 i;

    for (i = count - 1; i != -1; i--) {
        if (PTR(node->node.parent)) {
            SET_PTR(node->node.parent,
                    (SceneNode *)((u8 *)PTR(node->node.parent) + delta));
        }
        node++;
    }
}

/* ---- the model slots ---- */

Model *model_Get(s32 slot) { return g_models[slot]; }

#if RB_PORT
void model_PortPut(s32 slot, Model *model) { g_models[slot] = model; }
#endif

/* The number of the model in slot `slot`, or -1. Nothing calls it. */
static s32 model_GetId(s32 slot) {
    Model *model = g_models[slot];
    if (model) {
        return model->model_id;
    }
    return -1;
}

/* The first slot with no model, or -1. Nothing calls it. */
static s32 model_FindFreeSlot(void) {
    s32 i;
    s32 slot = -1;
    for (i = 0; i < MODEL_SLOT_COUNT; i++) {
        if (!g_models[i]) {
            slot = i;
            break;
        }
    }
    return slot;
}

void model_Free(s32 slot) {
    cloth_FreeModelAt(slot);
    resource_FreeModel(slot);
    g_models[slot] = NULL;
}

/* Asks for the extra texture groups `groups` (a mask) for whatever model is
 * loaded into slot `slot` next. If a model is there with other groups
 * loaded, the next model_Load loads its textures again. */
void model_RequestTextures(s32 slot, s32 groups) {
    Model *model = g_models[slot];
    if (model) {
        if (((model->load_flags.word >> MODEL_LOAD_TEXTURES_SHIFT) &
             MODEL_TEXTURE_GROUP_MASK) != groups) {
            model->load_status = MODEL_STATUS_WANTS_TEXTURES;
        }
    }
    g_model_texture_requests[slot] = groups;
}

/* The extra texture groups the model in slot `slot` has loaded. Nothing
 * calls it. */
static s32 model_GetLoadedTextures(s32 slot) {
    Model *model = g_models[slot];
    if (model) {
        return (model->load_flags.word >> MODEL_LOAD_TEXTURES_SHIFT) &
               MODEL_TEXTURE_GROUP_MASK;
    }
    return -1;
}

/* Has a model's extra nodes drawn as outlines or solid. */
static inline void model_OutlineExtraNodes(Model *model, s32 on) {
    ModelNode *node;

    for (node = model->extra_nodes;; node++) {
        if (node->extra == 0) {
            return;
        }
        if (on) {
            node->node.draw_flags |= NODE_DRAW_OUTLINE;
        } else {
            node->node.draw_flags &= ~NODE_DRAW_OUTLINE;
        }
    }
}

/* Has the model in slot `slot` drawn as outlines (`on`) or solid, and sets
 * the outline colour to that model's own. One colour serves both slots: only
 * the fighter looked through in the first-person view is ever outlined. */
void model_SetOutline(s32 slot, s32 on) {
    MATRIX unused; /* not used */
    Model *model;
    s32 i;
    ModelNode *bone;
    s32 b, r, g, gray;

    model = g_models[slot];
    if (model == NULL) {
        return;
    }

    bone = &model->bones[1];
    if (on) {
        bone->node.draw_flags |= NODE_DRAW_OUTLINE;
    } else {
        bone->node.draw_flags &= ~NODE_DRAW_OUTLINE;
    }

    /* FAKE: for (i = 1; i < MODEL_BODY_BONES; i++) */
    i = 1;
loop1:
    bone++;
    /* FAKE: the test goes through `b`, which holds blue further down */
    b = bone->node.mesh >= 0;
    if (b) {
        if (on) {
            bone->node.draw_flags |= NODE_DRAW_OUTLINE;
        } else {
            bone->node.draw_flags &= ~NODE_DRAW_OUTLINE;
        }
    }
    i++;
    if (i < MODEL_BODY_BONES) {
        goto loop1;
    }

    model_OutlineExtraNodes(model, on);

    if (tex_IsGrayscale()) {
        gray = math_Grayscale3(
            model->outline_color.byte[2], model->outline_color.byte[1],
            model->outline_color.byte[0]);
        render_SetOutlineColor((gray << 16) | (gray << 8) | gray);
    } else {
        r = model->outline_color.byte[0];
        g = model->outline_color.byte[1];
        b = model->outline_color.byte[2];
        render_SetOutlineColor(b | ((r << 16) | (g << 8)));
    }
}

/* ---- node matrices ---- */

/* Brings a node's world matrix up to date: the local matrix from the node's
 * angles (unless it was written directly), then the parent's world matrix
 * applied to it, the parent's being brought up to date first if need be. */
void node_UpdateWorldMatrix(SceneNode *node) {
    RotMatrixFunc func;

    if (node->matrix_state == NODE_MATRIX_DONE) {
        return;
    }
    if (node->matrix_state != NODE_MATRIX_LOCAL_SET) {
        func = g_math_rot_matrix_funcs[node->rotation_order];
        func(&node->rot, &node->local);
    }
    if (PTR(node->parent)) {
        if (PTR(node->parent)->matrix_state == NODE_MATRIX_REBUILD) {
            node_UpdateWorldMatrix(PTR(node->parent));
        }
        math_MulMatrix(&PTR(node->parent)->world, &node->local, &node->world);
        math_TransformVector(
            &PTR(node->parent)->world, node->local.t, node->world.t);
    } else {
        node->world = node->local;
    }
    node->matrix_state = NODE_MATRIX_DONE;
}

/* Sets a node's matrices to the rotation by its angles negated, ignoring
 * any parent: the view roots are turned this way, the view being the inverse
 * of where the camera looks. */
void node_SetInverseRotation(SceneNode *node) {
    SVECTOR neg_rot;
    neg_rot.vx = -node->rot.vx;
    neg_rot.vy = -node->rot.vy;
    neg_rot.vz = -node->rot.vz;
    g_math_rot_matrix_funcs[node->rotation_order](&neg_rot, &node->local);
    node->world = node->local;
}

/* ---- textures ---- */

/* Uploads the extra texture groups of a fighter's model that are both asked
 * for (`groups`) and exist for that model. Group i is the file's texture
 * group i + 1. They go where the model's main textures went (see
 * model_FinishLoad), but for the model the fog falls on the colour tables go
 * to the rows the fog rewrites and not to the plain ones under them, where
 * the main group's went: the original's behaviour. (A guess that it is a
 * slip: the fog's next copy would then write over them.) */
static void model_LoadExtraTextures(
    s32 slot, s32 model_id, s32 groups, s32 textures) {
    s32 has;
    s32 bit;
    s32 i;

    if (slot < 0 || slot >= 2) {
        return;
    }
    has = g_model_extra_texture_masks[model_id];
    for (bit = 1 << (MODEL_TEXTURE_GROUPS - 1), i = 0; i < MODEL_TEXTURE_GROUPS;
         bit >>= 1, i++) {
        if (!(has & bit) || !(groups & bit)) {
            continue;
        }
#if RB_REMASTER_VIEW
        /* the fighter's (port_remaster.h), but a fogged model's, whose
         * tables go straight into the fog's rows and have no plain copy
         * to be keyed by */
        port_RemasterNoteFighterTextures(
            stage_GetFoggedModel() == slot ? -1 : slot, 0);
#endif
        switch (slot) {
        case 0:
            if (stage_GetFoggedModel() == 0) {
                tex_LoadGroupAtSplit((u32 *)ADDR(textures), i + 1, 0, 0,
                                     MODEL_FOG_CLUT_DX, MODEL_FOG_CLUT_DY);
            } else {
                tex_LoadGroup((u32 *)ADDR(textures), i + 1);
            }
            break;
        case 1:
            if (stage_GetFoggedModel() == slot) {
                tex_LoadGroupAtSplit(
                    (u32 *)ADDR(textures), i + 1, MODEL_SECOND_SLOT_DX, 0,
                    MODEL_FOG_CLUT_DX, MODEL_FOG_CLUT_DY);
            } else {
                tex_LoadGroupAt(
                    (u32 *)ADDR(textures), i + 1, MODEL_SECOND_SLOT_DX, 0);
            }
            break;
        }
    }
#if RB_REMASTER_VIEW
    port_RemasterNoteFighterTextures(-1, 0);
#endif
}

/* If `model` is one that can be tinted and is in the second slot, reads
 * the colour tables of its clothes back from video memory and keeps them,
 * so that model_TintOpponent always starts from the untinted colours. */
static void model_SaveTintCluts(Model *model) {
    RECT rect;
    s16 *clut;
    s32 i;
    s32 dx;
    s32 dy;

    if (model->slot != 1) {
        return;
    }
    if (g_model_tint_clut_list_ids[model->model_id] == MODEL_TINT_NONE) {
        return;
    }
    g_model_tint_saved.model_id = model->model_id;
    clut = g_model_tint_clut_lists[g_model_tint_clut_list_ids[model->model_id]];
    if (stage_GetFoggedModel() == 1) {
        dx = MODEL_FOG_CLUT_DX;
        dy = MODEL_FOG_PLAIN_CLUT_DY;
    } else {
        dx = MODEL_SECOND_SLOT_DX;
        dy = 0;
    }
    for (i = 0; clut[0] >= 0; clut += 2, i++) {
        rect.x = clut[0] + dx;
        rect.y = clut[1] + dy;
        rect.w = CLUT_COLORS;
        rect.h = 1;
        StoreImage(&rect, (u32 *)g_model_tint_saved.cluts[i]);
    }
    DrawSync(0);
}

/* Tints the clothes of the opponent (the model in the second slot) with a
 * colour, each part 0..255: the saved colour tables are written back to both
 * places the model's colour tables can be (the fog's block and its own) and
 * scaled there. Used for the opponents that come in several colours: story
 * mode's and chanbara's. */
void model_TintOpponent(s32 red, s32 green, s32 blue) {
    Model *model;
    s32 r;
    s32 g;
    s32 b;
    s32 place;
    s32 dx;
    s32 dy;
    s16 *clut;
    s32 i;
    s32 slot = 1;
    RECT rect;

    model = model_Get(slot);
    if (!model) {
        return;
    }
    if (model->model_id != g_model_tint_saved.model_id) {
        return;
    }
    if (g_model_tint_clut_list_ids[model->model_id] == MODEL_TINT_NONE) {
        return;
    }
    r = MATH_BYTE_TO_ONE(red);
    g = MATH_BYTE_TO_ONE(green);
    b = MATH_BYTE_TO_ONE(blue);
    if (tex_IsGrayscale()) {
        r = g = b = math_Grayscale3(r, g, b);
    }
    for (place = 0; place < 2; place++) {
        if (place == 0) {
            dx = MODEL_FOG_CLUT_DX;
            dy = MODEL_FOG_PLAIN_CLUT_DY;
        } else {
            dx = MODEL_SECOND_SLOT_DX;
            dy = 0;
        }
        clut = g_model_tint_clut_lists
            [g_model_tint_clut_list_ids[model->model_id]];
        for (i = 0; clut[0] >= 0; clut += 2) {
            rect.x = clut[0] + dx;
            rect.y = clut[1] + dy;
            rect.w = CLUT_COLORS;
            rect.h = 1;
            LoadImage(&rect, (u32 *)g_model_tint_saved.cluts[i++]);
            DrawSync(0);
            tex_TintClutInPlace(rect.x, rect.y, CLUT_COLORS, r, g, b);
        }
    }
    if (stage_GetFoggedModel() == slot) {
        stage_BuildModelFog();
    }
}

/* ---- data ---- */

/* The extra texture groups asked for, by slot (model_RequestTextures). */
static s32 g_model_texture_requests[MODEL_SLOT_COUNT] = {-1, -1, -1};

/* The skeleton. Bone 0 places the model and bone 1 is the body's root. By
 * its shape, 2 is the trunk, 3 the head, 4 to 7 and 8 to 11 the arms, 12 the
 * hips, and 13 to 15 and 16 to 18 the legs. 19 and 20 carry what the hands
 * hold. */
static ModelBoneTable g_model_bones = {
    -1,
    {
        {-1, {0, 0, 0}, -1},   {0, {0, 0, 0}, 0},        {1, {0, 10, 0}, 1},
        {2, {468, 0, 0}, -1},  {2, {289, 0, -100}, 2},   {4, {140, 0, 0}, 3},
        {5, {262, 0, 0}, 4},   {6, {211, 0, -3}, -1},    {2, {289, 0, 100}, 5},
        {8, {140, 0, 0}, 6},   {9, {262, 0, 0}, 7},      {10, {211, 0, -2}, 8},
        {1, {0, 10, 0}, 9},    {12, {180, 0, 110}, 10},  {13, {390, 0, 0}, 11},
        {14, {441, 0, 0}, 12}, {12, {180, 0, -109}, 13}, {16, {390, 0, 0}, 14},
        {17, {444, 0, 0}, -1}, {1, {0, 0, 0}, -1},       {1, {0, 0, 0}, 0},
    },
};

/* Each model's size, by model id: 0x1000 (ONE) is 1. A fighter's model is
 * its roster entry's number (g_fighter_roster_models), but for the last
 * two. Ends with 0xFFFF. */
/* clang-format off */
u16 g_model_scales[74] = {
    0x107A, /* ROSTER_KANNUKI_STORY */
    0x1000, /* ROSTER_TATSUMI_STORY */
    0x0F5C, /* ROSTER_MIKADO_STORY */
    0x107A, /* ROSTER_KANNUKI */
    0x1000, /* ROSTER_TATSUMI */
    0x0F5C, /* ROSTER_MIKADO */
    0x1000, /* ROSTER_UTSUSEMI */
    0x1000, /* ROSTER_RED_SHADOW */
    0x1000, /* ROSTER_TSUBAME */
    0x1028, /* ROSTER_SAZANKA */
    0x10A3, /* ROSTER_SUMINAGASHI */
    0x1000, /* ROSTER_NIGHTSTALKER */
    0x1000, /* ROSTER_MATSUMUSHI */
    0x1000, /* ROSTER_VANISHER */
    0x1000, /* ROSTER_SIDEARM_1 */
    0x1051, /* ROSTER_GENGORO_STORY */
    0x1000, /* ROSTER_KAUN_STORY */
    0x0F85, /* ROSTER_JO_STORY */
    0x1051, /* ROSTER_GENGORO */
    0x1000, /* ROSTER_KAUN */
    0x0F85, /* ROSTER_JO */
    0x1000, /* ROSTER_HONGOU */
    0x1000, /* ROSTER_KATZE */
    0x1051, /* ROSTER_HIGHWAYMAN */
    0x0F0A, /* ROSTER_CHIHIRO */
    0x1000, /* ROSTER_UTAMARU */
    0x1051, /* ROSTER_TONY_UMEDA */
    0x1000, /* ROSTER_ISOHACHI */
    0x107A, /* ROSTER_ARMORED */
    0x1000, /* ROSTER_SIDEARM_2 */
    0x1000, /* ROSTER_LAST_1 */
    /* models 31 to 57, which no roster entry has */
    0x107A, 0x1000, 0x0F5C, 0x1000, 0x1000, 0x1000, 0x1028, 0x10A3, 0x1000,
    0x1000, 0x1051, 0x1000, 0x0F85, 0x1000, 0x1000, 0x1051, 0x0F0A, 0x1000,
    0x1051, 0x1000, 0x1000, 0x1000, 0x1000, 0x1000, 0x1000, 0x1000, 0x1000,
    0x1000, /* chanbara's kind 0 */
    0x1000, /* chanbara's kind 1 */
    0x1000, /* chanbara's kind 2 */
    0x1000, /* chanbara's kind 3 */
    0x1000, /* ROSTER_VICTIM */
    0x1000, /* ROSTER_LAST_2 */
    /* six more, then the end */
    0x1000, 0x1000, 0x1000, 0x1000, 0x1000, 0x1000,
    0xFFFF,
    0, 0, 0,
};
/* clang-format on */

/* Which row of a decoded motion frame holds the angles of each body bone
 * from the third on: the motion data keeps the bones in another order. */
static s32 g_model_pose_channels[17] = {
    8, 9, 14, 15, 16, 17, 10, 11, 12, 13, 1, 2, 3, 4, 5, 6, 7};

/* Which of the five extra texture groups each model's file has, by model
 * id: all of them or none. */
#define ALL MODEL_TEXTURE_GROUP_MASK
/* clang-format off */
static u8 g_model_extra_texture_masks[64] = {
    ALL, /* ROSTER_KANNUKI_STORY */
    ALL, /* ROSTER_TATSUMI_STORY */
    ALL, /* ROSTER_MIKADO_STORY */
    ALL, /* ROSTER_KANNUKI */
    ALL, /* ROSTER_TATSUMI */
    ALL, /* ROSTER_MIKADO */
    ALL, /* ROSTER_UTSUSEMI */
    ALL, /* ROSTER_RED_SHADOW */
    ALL, /* ROSTER_TSUBAME */
    ALL, /* ROSTER_SAZANKA */
    ALL, /* ROSTER_SUMINAGASHI */
    ALL, /* ROSTER_NIGHTSTALKER */
    ALL, /* ROSTER_MATSUMUSHI */
    0,   /* ROSTER_VANISHER */
    0,   /* ROSTER_SIDEARM_1 */
    ALL, /* ROSTER_GENGORO_STORY */
    ALL, /* ROSTER_KAUN_STORY */
    ALL, /* ROSTER_JO_STORY */
    ALL, /* ROSTER_GENGORO */
    ALL, /* ROSTER_KAUN */
    ALL, /* ROSTER_JO */
    ALL, /* ROSTER_HONGOU */
    ALL, /* ROSTER_KATZE */
    ALL, /* ROSTER_HIGHWAYMAN */
    ALL, /* ROSTER_CHIHIRO */
    ALL, /* ROSTER_UTAMARU */
    ALL, /* ROSTER_TONY_UMEDA */
    ALL, /* ROSTER_ISOHACHI */
    0,   /* ROSTER_ARMORED */
    0,   /* ROSTER_SIDEARM_2 */
    ALL, /* ROSTER_LAST_1 */
    /* models 31 to 57, which no roster entry has */
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0,
    0,   /* chanbara's kind 0 */
    0,   /* chanbara's kind 1 */
    0,   /* chanbara's kind 2 */
    0,   /* chanbara's kind 3 */
    0,   /* ROSTER_VICTIM */
    ALL, /* ROSTER_LAST_2 */
};
/* clang-format on */
#undef ALL

/* The colour tables that are tinted, for each model that can be: where each
 * is in video memory (x, y) for the first slot, ended by -1. They are named
 * here by column and row among the model's colour tables. */
#define TINT_CLUT(column, row)                                                 \
    MODEL_CLUT_X + (column) * CLUT_COLORS, MODEL_CLUT_Y + (row)
#define TINT_END -1, 0
static s16 g_model_tint_cluts_14[16] = {
    TINT_CLUT(3, 2), TINT_CLUT(1, 0), TINT_CLUT(2, 0), TINT_CLUT(0, 0),
    TINT_CLUT(3, 0), TINT_CLUT(1, 1), TINT_CLUT(3, 1), TINT_END};
static s16 g_model_tint_cluts_29[8] = {
    TINT_CLUT(0, 0), TINT_CLUT(3, 1), TINT_CLUT(0, 1), TINT_END};
static s16 g_model_tint_cluts_58[12] = {
    TINT_CLUT(0, 1), TINT_CLUT(3, 2), TINT_CLUT(3, 0),
    TINT_CLUT(3, 1), TINT_CLUT(0, 0), TINT_END};
static s16 g_model_tint_cluts_59[8] = {
    TINT_CLUT(0, 4), TINT_CLUT(1, 3), TINT_CLUT(1, 2), TINT_END};
static s16 g_model_tint_cluts_60[6] = {
    TINT_CLUT(2, 0), TINT_CLUT(3, 1), TINT_END};
static s16 g_model_tint_cluts_61[6] = {
    TINT_CLUT(0, 3), TINT_CLUT(3, 3), TINT_END};
static s16 *g_model_tint_clut_lists[6] = {
    g_model_tint_cluts_14, g_model_tint_cluts_29, g_model_tint_cluts_58,
    g_model_tint_cluts_59, g_model_tint_cluts_60, g_model_tint_cluts_61};
/* Which of those lists each model uses, by model id (MODEL_TINT_NONE: it
 * cannot be tinted): the two ninja, who are the story's henchmen and the
 * partner of practice, and chanbara's four kinds. */
#define NONE MODEL_TINT_NONE
/* clang-format off */
static u8 g_model_tint_clut_list_ids[64] = {
    NONE, /* ROSTER_KANNUKI_STORY */
    NONE, /* ROSTER_TATSUMI_STORY */
    NONE, /* ROSTER_MIKADO_STORY */
    NONE, /* ROSTER_KANNUKI */
    NONE, /* ROSTER_TATSUMI */
    NONE, /* ROSTER_MIKADO */
    NONE, /* ROSTER_UTSUSEMI */
    NONE, /* ROSTER_RED_SHADOW */
    NONE, /* ROSTER_TSUBAME */
    NONE, /* ROSTER_SAZANKA */
    NONE, /* ROSTER_SUMINAGASHI */
    NONE, /* ROSTER_NIGHTSTALKER */
    NONE, /* ROSTER_MATSUMUSHI */
    NONE, /* ROSTER_VANISHER */
    0,    /* ROSTER_SIDEARM_1 */
    NONE, /* ROSTER_GENGORO_STORY */
    NONE, /* ROSTER_KAUN_STORY */
    NONE, /* ROSTER_JO_STORY */
    NONE, /* ROSTER_GENGORO */
    NONE, /* ROSTER_KAUN */
    NONE, /* ROSTER_JO */
    NONE, /* ROSTER_HONGOU */
    NONE, /* ROSTER_KATZE */
    NONE, /* ROSTER_HIGHWAYMAN */
    NONE, /* ROSTER_CHIHIRO */
    NONE, /* ROSTER_UTAMARU */
    NONE, /* ROSTER_TONY_UMEDA */
    NONE, /* ROSTER_ISOHACHI */
    NONE, /* ROSTER_ARMORED */
    1,    /* ROSTER_SIDEARM_2 */
    NONE, /* ROSTER_LAST_1 */
    /* models 31 to 57, which no roster entry has */
    NONE, NONE, NONE, NONE, NONE, NONE, NONE, NONE, NONE, NONE, NONE, NONE,
    NONE, NONE, NONE, NONE, NONE, NONE, NONE, NONE, NONE, NONE, NONE, NONE,
    NONE, NONE, NONE,
    2,    /* chanbara's kind 0 */
    3,    /* chanbara's kind 1 */
    4,    /* chanbara's kind 2 */
    5,    /* chanbara's kind 3 */
    NONE, /* ROSTER_VICTIM */
    NONE, /* ROSTER_LAST_2 */
};
/* clang-format on */
#undef NONE
