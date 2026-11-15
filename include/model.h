#ifndef MODEL_H
#define MODEL_H

/* A fighter's model: the body as drawn. There are three model slots; the two
 * fighters use slots 0 and 1. A model is loaded from the archive
 * (resource_LoadModel), given a skeleton of nodes, and posed every frame
 * from a decoded motion frame; the fight code then reads the bones' matrices
 * to place weapons and hit points (model.c, model_face.c, model_tiny.c). */

#include "common.h"
#include <psxsdk/libgpu.h>
#include <psxsdk/libgte.h>
#include "node.h"

#define MODEL_SLOT_COUNT 3
#define MODEL_BONE_COUNT 21
/* Bones 1 to 18 are the body; a motion frame poses them. Bone 0 is the root
 * that places the model in the world, bone 1 the body's own root that the
 * others hang from, and the last two carry what the hands hold. */
#define MODEL_BODY_BONES 18
#define MODEL_HAND_BONE 19  /* and 20 */
#define MODEL_WAIST_BONE 12 /* the hips: the legs hang from it */
/* Every bone but the root has a place in the model's lists of matrices. */
#define MODEL_MATRIX_COUNT (MODEL_BONE_COUNT - 1)
/* The most meshes a model's file has, and the most shadows and extra nodes
 * a model is given, the node that ends each list among them. */
#define MODEL_MESH_COUNT 20
#define MODEL_SHADOW_NODES 20
#define MODEL_EXTRA_NODES 20

/* Model.load_status: what model_FinishLoad still has to do. Nothing; all of
 * it, the file having just been read; or the file is to be read again for
 * other extra textures. */
#define MODEL_STATUS_DONE 0
#define MODEL_STATUS_READ 1
#define MODEL_STATUS_WANTS_TEXTURES (-2)

/* Model.load_flags: the file has its own sound bank; and the extra texture
 * groups that are loaded, a bit each. */
#define MODEL_LOAD_HAS_VAB 2
#define MODEL_TEXTURE_GROUPS 5
#define MODEL_TEXTURE_GROUP_MASK ((1 << MODEL_TEXTURE_GROUPS) - 1)
#define MODEL_LOAD_TEXTURES_SHIFT 16

/* Where a model's textures are in video memory. A model's file is made for
 * the first slot: its textures from (640, 0), and under them its colour
 * tables, six rows of four from (640, 248). The second slot's are two
 * texture pages to the right. */
#define MODEL_TEXTURES_X 640
#define MODEL_CLUT_X 640
#define MODEL_CLUT_Y 248
#define MODEL_CLUT_ROWS 6
#define MODEL_SECOND_SLOT_PAGES 2
#define MODEL_SECOND_SLOT_DX (MODEL_SECOND_SLOT_PAGES * TPAGE_WIDTH)
/* The model that the stage's fog falls on (the one seen in the first-person
 * view: stage_FogModel) has its colour tables in the fog's block instead,
 * which is this far from where the first slot's are: the ones its meshes
 * use, which the fog rewrites as the frame is drawn, and under them the
 * plain ones the fogged copies are made from. */
#define MODEL_FOG_CLUT_DX (STAGE_FOG_MODEL_X - MODEL_CLUT_X)
#define MODEL_FOG_CLUT_DY (STAGE_FOG_MODEL_Y - MODEL_CLUT_Y)
#define MODEL_FOG_PLAIN_CLUT_DY (MODEL_FOG_CLUT_DY + STAGE_FOG_MODEL_HEIGHT)

/* A node of a model: the scene node and what goes with it. */
typedef struct ModelNode {
    SceneNode node; /* +0x00 */
    /* +0x58 by the node's kind: a shadow's ground height; an extra node's
     * bone (a ModelNode *); the skinned body's list of matrices */
    s32 extra;
    s32 unk5C;                /* +0x5C no code reads or writes it */
    PTR32(u8 *) cloth_points; /* +0x60 a cloth mesh's points (ClothPoint) */
    s32 unk64;                /* +0x64 no code reads or writes it */
} ModelNode;                  /* 0x68 bytes */

/* The word after a model node's scene node, written through a pointer and
 * not as the member `extra`. Three functions store it this way in the
 * original: after such a store the compiler reads again every variable it
 * had read, which it does not do after a store to a member. (The other
 * stores to `extra` come out the same either way.) */
#define NODE_EXTRA(n) (*(s32 *)(&(n)->node + 1))

/* A loaded model. `data` is the model's file in its arena block: a table of
 * offsets to its meshes, its mesh-to-bone list, cloth parameters, shadow
 * meshes, textures, and sometimes a sound bank. */
typedef struct Model {
    /* +0x0000 MODEL_LOAD_HAS_VAB, and from MODEL_LOAD_TEXTURES_SHIFT up the
     * extra texture groups that are loaded */
    union {
        s32 word;
        s16 half[2];
    } load_flags;

    s16 slot;              /* +0x0004 */
    s16 load_status;       /* +0x0006 MODEL_STATUS_...; not 0:
                              model_FinishLoad has work to do */
    s16 model_id;          /* +0x0008 */
    u8 pad0A[6];           /* +0x000A */
    u16 light_setup_index; /* +0x0010 */
    s16 scale;             /* +0x0012 the character's size, ONE = 1 */
    s16 mesh_block;        /* +0x0014 enum RelocBlock */
    s16 shadow_block;      /* +0x0016 */

    /* +0x0018 the colour its outline is drawn in: r, g, b */
    union {
        s32 word;
        u8 byte[4];
    } outline_color;

    s32 data;                         /* +0x001C */
    s32 staging;                      /* +0x0020 where the file was read, if not
                                         straight into `data` */
    PTR32(struct ModelCloth *) cloth; /* +0x0024 null: it has none (cloth.h) */
    s32 cloth_params;                 /* +0x0028 the file's cloth section */
    ModelNode bones[MODEL_BONE_COUNT];     /* +0x002C */
    ModelNode shadows[MODEL_SHADOW_NODES]; /* +0x08B4 ends with mesh -1 */
    /* +0x10D4 for the bones that carry more than one mesh; ends with
     * extra 0 */
    ModelNode extra_nodes[MODEL_EXTRA_NODES];
    /* +0x18F4 the bones' world matrices in the order the body's mesh
     * sections use */
    MatrixSlot skin_matrices[MODEL_MATRIX_COUNT];
    u8 pad1944[0x50]; /* +0x1944 */
    /* +0x1994 the same from bone 1 up */
    MatrixSlot bone_matrices[MODEL_MATRIX_COUNT];
    u8 pad19E4[0x50]; /* +0x19E4 */
    /* +0x1A34 the node that draws each mesh */
    PTR32(ModelNode *) mesh_nodes[MODEL_MESH_COUNT];
    s16 ground_y;  /* +0x1A84 */
    u8 pad1A86[2]; /* +0x1A86 */
} Model;           /* 0x1A88 bytes */

/* A bone of the skeleton every model shares. */
typedef struct {
    s16 parent;      /* bone index, -1 for none */
    s16 offset[3];   /* from the parent, before the model's scale */
    s16 shadow_mesh; /* the bone's shadow, -1 for none */
} ModelBone;

/* The size of each model, by model id; ends with -1. */
extern u16 g_model_scales[74];

/* How model_Update poses the body. */
enum ModelPoseMode {
    MODEL_POSE_FRAME = 0,   /* from the decoded motion frame */
    MODEL_POSE_BLENDED = 1, /* model_BlendPoses has already set the bones */
};

/* ---- loading ---- */

extern void model_ClearAll(void);
/* Loads model `model_id` into slot `slot` and builds its skeleton. With a
 * staging address the file is read there first. */
extern Model *model_Load(s32 slot, s32 model_id, s32 staging_addr);
extern void model_Free(s32 slot);
extern Model *model_Get(s32 slot);
/* The arena moved the model by `delta` bytes. */
extern void model_Rebase(s32 slot, s32 delta);

/* A model's file has up to five extra groups of textures, loaded only when
 * asked for, as a mask. Story mode asks for them for its fighter and they
 * stay through its fights: white wrappings on the limbs wounded so far,
 * from the next stage on. Asking for other groups than are loaded makes the
 * next model_Load read the textures again. */
extern void model_RequestTextures(s32 slot, s32 groups);

/* Some opponents come in several colours: the model in slot 1 keeps a copy
 * of the colour tables of its clothes, and this writes them back tinted. */
extern void model_TintOpponent(s32 red, s32 green, s32 blue);

/* ---- every frame ---- */

/* Places the model at `position` turned by `rotation`, poses it, queues it
 * for drawing and steps its cloth. */
extern void model_Update(s32 slot, s32 pose_mode, s32 *position, s16 *rotation,
                         s16 *frame, s32 ground_y);
/* Poses the body between two decoded frames instead: `weight` of the
 * second. `scratch` is room for two matrices. */
extern void model_BlendPoses(
    s32 slot, u8 *from_frame, u8 *to_frame, s32 weight, MATRIX *scratch);
/* The world matrices of bones 1 up, or NULL if the slot is empty. */
extern MatrixSlot *model_GetBoneMatrices(s32 slot);

/* The face. A model's face texture has spare copies of the eyes (three) and
 * of the mouth (two) beside it; a face is one of each, copied into place:
 * 0..2 are the three pairs of eyes with one mouth, 3..5 with the other. */
#define MODEL_FACE_NORMAL 0
#define MODEL_FACE_DEAD 4
#define MODEL_FACE_RESTORE 6 /* put back the texture's own eyes and mouth */
extern void model_SetFace(s32 slot, s32 face);

/* Draws the model in slot `slot` as outlines in its own colour, or solid
 * again: what a fighter looks like to itself in the first-person view. */
extern void model_SetOutline(s32 slot, s32 on);

/* ---- the tiny model ----
 * In the first-person view the fighter being looked through is also drawn as
 * a small figure that copies its pose, seen by a camera of its own: it
 * stands in the top left corner of the screen, under the wins. It has its
 * own short ordering table. */
extern MATRIX g_model_tiny_matrix;
/* The tiny model of each model, by model id (MODEL_TINY_NONE: it has
 * none), and the bone each part of the figure follows (an index into the
 * model's bone matrices), ended by -1 (resource_data.c). */
#define MODEL_TINY_NONE 0xFF
extern u8 g_model_tiny_ids[70];
extern s16 g_model_tiny_bones[16];
extern s32 model_LoadTiny(s32 slot);
extern void model_RebaseTiny(s32 delta);
extern void model_QueueTiny(s32 yaw, s32 weapon_id, s32 has_object);

#endif /* MODEL_H */
