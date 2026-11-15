#ifndef NODE_H
#define NODE_H

/* The scene graph. Everything drawn in 3D is a node: a transform, an
 * optional parent, and a mesh. The camera, the stage's pieces, the bones of a
 * fighter's model, held and loose weapons and falling props are all nodes.
 * Each frame the game queues the nodes it wants drawn (render.h) and the
 * renderer walks the queue. */

#include "common.h"
#include <psxsdk/libgte.h>

/* A matrix's address in four bytes (PTR32, common.h): an entry of a
 * model's lists of its bones' matrices, which the renderer walks. */
typedef PTR32(MATRIX *) MatrixSlot;

/* How a node is drawn (SceneNode.kind). */
enum NodeKind {
    NODE_KIND_ROTATED = 0,   /* a mesh, lit by the node's light setup */
    NODE_KIND_UNROTATED = 1, /* the same without the lighting rotation */
    /* the parent's shape flattened onto the ground along the sun's direction
     * and drawn black: the node's mesh is the shadow's own, simpler one */
    NODE_KIND_SHADOW = 3,
    /* a fighter's body: one mesh in sections, each moved by another bone */
    NODE_KIND_SKINNED = 10,
    /* 12 is a stage tile, which is not a node (stage.h) */
    /* a cloth mesh: drawn from the cloth's points, which are already in the
     * world, in place of its own vertices (cloth.h) */
    NODE_KIND_CLOTH = 13,
    NODE_KIND_WAVE = 14, /* the surf (wave.h) */
    NODE_KIND_TINY = 15, /* a part of the tiny model (model.h) */
    /* The two view roots are never queued; the numbers only mark them. */
    NODE_KIND_CAMERA_ROOT = 100,
    NODE_KIND_IDENTITY_ROOT = 101,
};

/* SceneNode.draw_flags */
#define NODE_DRAW_OUTLINE 1 /* draw the mesh as outlines (model_SetOutline) */

/* Whether a node's world matrix is up to date (SceneNode.matrix_state). */
enum NodeMatrixState {
    NODE_MATRIX_REBUILD = 0, /* build the local matrix from the angles, then
                                the world matrix */
    NODE_MATRIX_DONE = 1,
    NODE_MATRIX_LOCAL_SET = 2, /* the local matrix was written directly: only
                                  the world matrix is to do */
};

/* The order a node's three angles apply in (SceneNode.rotation_order): an
 * index into g_math_rot_matrix_funcs. */
enum RotationOrder {
    ROTATION_ORDER_ZYX = 0,
    ROTATION_ORDER_ZXY = 2,
    ROTATION_ORDER_YXZ = 4,
    ROTATION_ORDER_XYZ = 5,
};

/* A node. `mesh_block` and `mesh` find its mesh: the block is one of the
 * renderer's pointer tables (enum RelocBlock in resource.h), the mesh an
 * entry of it. `rot` and `local.t` place the node in its parent; `local` is
 * the matrix built from them, `world` the result of applying the parent's
 * world matrix to it (node_UpdateWorldMatrix). */
typedef struct SceneNode {
    u8 kind;               /* +0x00 enum NodeKind */
    u8 draw_flags;         /* +0x01 NODE_DRAW_... */
    s16 mesh;              /* +0x02 */
    s16 mesh_block;        /* +0x04 */
    s16 matrix_state;      /* +0x06 enum NodeMatrixState */
    s16 rotation_order;    /* +0x08 enum RotationOrder */
    s16 light_setup_index; /* +0x0A which LightSetup lights it (light.h) */
    PTR32(struct SceneNode *) parent; /* +0x0C */
    SVECTOR rot;                      /* +0x10 */
    MATRIX world;                     /* +0x18 */
    MATRIX local;                     /* +0x38 */
} SceneNode;                          /* 0x58 bytes */

/* A node that is nobody's. Every node in the game is 0x68 bytes: the scene
 * node, then sixteen that are its owner's (a model's ModelNode, the stage's
 * StageNode and StageProp). The four nodes that stand alone have the sixteen
 * too. */
typedef struct {
    SceneNode node; /* +0x00 */
    u8 pad58[0x10]; /* +0x58 not touched */
} PlainNode;        /* 0x68 bytes */

/* The view. The camera is a node whose matrix is the inverse of where the
 * camera is and looks; the second root never moves. The renderer's view
 * matrix is the one applied to the other, through these two pointers. */
extern PlainNode g_node_camera_root;
extern PlainNode g_node_identity_root;
extern SceneNode *g_node_camera_root_ptr;
extern SceneNode *g_node_identity_root_ptr;

extern void node_InitViewRoots(void);
/* Where the camera is, in the world. */
extern s32 *node_GetCameraPosition(void);

/* Makes `node` a plain unparented node at the origin, with a stage prop's
 * mesh block and light. */
extern void node_Init(SceneNode *node);
/* Brings `node`'s world matrix up to date, and its parents' first. */
extern void node_UpdateWorldMatrix(SceneNode *node);
/* Sets `node`'s matrices to the rotation by its angles negated, with no
 * parent: how a view root is turned. */
extern void node_SetInverseRotation(SceneNode *node);

#endif /* NODE_H */
