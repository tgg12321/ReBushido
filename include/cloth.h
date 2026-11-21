#ifndef CLOTH_H
#define CLOTH_H

/* Cloth: the parts of a fighter that hang and swing, such as skirts, sleeves,
 * coat tails and long hair. A model file says which of its meshes are cloth.
 * Such a mesh is not drawn from its own vertices: every vertex becomes a
 * point with a position and a velocity, the points of each face are joined
 * by springs, and the mesh is drawn from the points. Some points are pinned
 * to the bone the mesh hangs from, the rest fall, are pulled along by the
 * springs, and are kept out of the ground and out of the body's limbs, which
 * are given as ellipsoids.
 *
 * cloth.c is the simulation and knows nothing of models; cloth_model.c sets
 * a model's cloth up from its file and steps it every frame. */

#include "common.h"
#include <psxsdk/libgte.h>
#include "model.h"

#if RB_PORT
/* the native port's two on two has two more models' cloth (port_duo.h) */
#define CLOTH_COUNT 20
#else
#define CLOTH_COUNT 8 /* cloths alive at once */
#endif
#define CLOTH_POINT_SPRINGS 8 /* springs from, and to, one point */
#define CLOTH_COLLIDERS 3     /* ellipsoids one cloth can meet */
#define MODEL_CLOTH_PIECES 5  /* cloth meshes one model can have */

/* How a point is held (ClothPoint.pin; in the file, and in the `pad` of the
 * mesh's vertex). 0 holds it on the bone. 1..256 ties it to its place on
 * the bone, the tighter the larger. Below zero it is loose: -1..-255 still
 * lean it toward its place, by that many 256ths a step. */
#define CLOTH_PIN_FIXED 0
#define CLOTH_PIN_ONE 256 /* tied as tightly as can be */
#define CLOTH_PIN_LOOSE (-256)

/* A point. Positions and velocities are in 128ths of a world unit. */
typedef struct {
    s32 pos[3];                          /* +0x00 */
    s32 vel_x;                           /* +0x0C per step */
    s32 vel_y;                           /* +0x10 */
    s32 vel_z;                           /* +0x14 */
    s32 pin;                             /* +0x18 CLOTH_PIN_ */
    s32 out_count;                       /* +0x1C */
    s32 in_count;                        /* +0x20 */
    u8 out_springs[CLOTH_POINT_SPRINGS]; /* +0x24 springs that start here */
    u8 in_springs[CLOTH_POINT_SPRINGS];  /* +0x2C springs that end here */
    /* +0x34 how far it is from the middle of the held points, in world
     * units, at most 0x100. Nothing reads it. */
    s32 center_dist;
    s32 pad38[2]; /* +0x38 not touched: a point is 0x40 bytes */
} ClothPoint;     /* 0x40 bytes */

/* A spring between two points. It starts at the one nearer the middle of
 * the held points. */
typedef struct {
    s32 length; /* +0x00 at rest */

    union {
        s32 pair; /* from << 16 | to */

        struct {
            u16 to;
            s16 from;
        } point;
    } ends; /* +0x04 */

    s32 max_length; /* +0x08 three times `length`. Nothing reads it. */
    s32 kind;       /* +0x0C from the face list: always 2. Nothing reads it. */
} ClothSpring;      /* 0x10 bytes */

/* One cloth. */
typedef struct {
    /* +0x00 the mesh's vertices, in the bone's space: where each point
     * would be if the cloth were stiff. Null: this cloth is free. */
    SVECTOR *vertices;
    s16 count;            /* +0x04 points */
    s16 spring_count;     /* +0x06 */
    s32 unk8;             /* +0x08 ClothDesc.unk2, copied; never read */
    ClothPoint *points;   /* +0x0C */
    ClothSpring *springs; /* +0x10 right after the points */
    MATRIX matrix;        /* +0x14 the bone's, as of the last step */
} Cloth;                  /* 0x34 bytes */

/* What cloth_Create makes a cloth from. */
typedef struct {
    s16 count; /* +0x00 vertices */
    /* +0x02 cloth_CreatePiece sets it to 0xE00 and cloth_Create copies it
     * to Cloth.unk8; nothing reads it */
    s16 unk2;
    SVECTOR *vertices; /* +0x04 with each one's CLOTH_PIN_ in its `pad` */
    /* +0x08 the faces: for each a count, a spring kind and that many vertex
     * numbers; a count of zero ends the list */
    s16 *faces;
    MATRIX *matrix; /* +0x0C the bone's */
    u8 *buffer;     /* +0x10 room for the points and springs */
} ClothDesc;

/* The ellipsoids a cloth is kept out of, and the ground. An ellipsoid is
 * every place whose distances from two foci add up to less than its
 * `reach`. In world units. */
typedef struct ClothColliders {
    s32 ground_y;                    /* +0x00 */
    s32 count;                       /* +0x04 */
    VECTOR foci[CLOTH_COLLIDERS][2]; /* +0x08 */
    s32 reach[CLOTH_COLLIDERS];      /* +0x68 */
} ClothColliders;                    /* 0x74 bytes */

/* One cloth to step: cloth_Update takes a list of these. */
typedef struct {
    s16 cloth;              /* +0x00 index in g_cloths */
    u8 snap;                /* +0x02 bit 0: put it at rest instead */
    u8 unk3;                /* +0x03 cleared by cloth_CreatePiece; never read */
    PTR32(MATRIX *) matrix; /* +0x04 the bone's, now */
    PTR32(u8 *) buffer;     /* +0x08 the cloth's points; not read */
    PTR32(ClothColliders *) colliders; /* +0x0C null: none, and no ground */
} ClothUpdate;                         /* 0x10 bytes */

/* A piece's ellipsoids, each riding on a bone of the model. */
typedef struct {
    s32 count;                                 /* +0x00 */
    PTR32(ModelNode *) bones[CLOTH_COLLIDERS]; /* +0x04 */
    ClothColliders world;                      /* +0x10 where they are now */
    SVECTOR local_foci[CLOTH_COLLIDERS][2];    /* +0x84 in the bone's space */
} ClothBoneColliders;                          /* 0xB4 bytes */

/* One cloth mesh of a model. */
typedef struct {
    s16 cloth;                    /* +0x00 index in g_cloths */
    ClothUpdate update;           /* +0x04 */
    PTR32(ModelNode *) node;      /* +0x14 the node that draws the mesh */
    PTR32(u8 *) data;             /* +0x18 its vertices, points and springs */
    ClothBoneColliders colliders; /* +0x1C */
} ClothPiece;                     /* 0xD0 bytes */

/* A model's cloth (Model.cloth): a block at the end of the model's data. */
typedef struct ModelCloth {
    s16 count;        /* +0x000 pieces */
    s16 snap_request; /* +0x002 put the cloth at rest on the next update */
    s16 unk4;         /* +0x004 cleared by cloth_SetupModel; never read */
    /* +0x006 make the pieces again on the next update: the model's data
     * has moved, or a cutscene has begun or ended */
    s16 rebuild_request;
    ClothPiece pieces[MODEL_CLOTH_PIECES];   /* +0x008 */
    ClothUpdate updates[MODEL_CLOTH_PIECES]; /* +0x418 the pieces' own */
    u8 data[1]; /* +0x468 the pieces' vertices, points and springs */
} ModelCloth;

/* The cloth section of a model file (Model.cloth_params) is a list of
 * halfwords, a record for each mesh in turn:
 *   CLOTH_FILE_NOT_CLOTH, or
 *   the pins: (count, CLOTH_PIN_, count vertex numbers)..., then
 *   CLOTH_FILE_COLLIDERS and the ellipsoids: (bone, focus x y z, focus
 *   x y z, reach)..., then CLOTH_FILE_NEXT.
 * CLOTH_FILE_END closes the list. */
#define CLOTH_FILE_COLLIDERS -1
#define CLOTH_FILE_NEXT -2
#define CLOTH_FILE_NOT_CLOTH -2
#define CLOTH_FILE_END -3

/* How many halfwords a face of a mesh takes, by the kind of face. */
extern s16 g_cloth_face_strides[4];

/* cloth.c */

extern void cloth_FreeAll(void);
/* The room a cloth of so many points and springs needs. */
extern s32 cloth_BufferSize(s32 point_count, s32 spring_count);
/* Makes a cloth, hanging at rest. Returns its index, or -1 if all are in
 * use. */
extern s32 cloth_Create(ClothDesc *desc);
extern void cloth_Free(s32 cloth);
/* Steps each cloth of a list by one frame. */
extern void cloth_Update(ClothUpdate *updates, s32 count);
/* The simulation works in the scratchpad, where the fight keeps things of
 * its own: the fight saves it before a model's update and restores it
 * after. */
extern void cloth_SaveScratch(void);
extern void cloth_RestoreScratch(void);
/* Both do nothing: see cloth_ClearSceneColliders. */
extern void cloth_ResetSceneColliders(void);
extern void cloth_SetSceneCollider(s32 first_word, s32 second_word, s32 index);

/* cloth_model.c */

/* Once, when the engine starts. */
extern void cloth_Init(void);
/* Every frame, as the scene begins. What they clear and pass on is never
 * set: see cloth_ClearSceneColliders. */
extern void cloth_ClearSceneColliders(void);
extern void cloth_SendSceneColliders(void);
/* Makes the cloths of a model whose file is loaded. `resize_data` gives the
 * model's data its final size, now that the cloth is part of it. */
extern void cloth_SetupModel(Model *model, s32 resize_data);
/* Steps a model's cloth by one frame. */
extern void cloth_UpdateModel(Model *model);
extern void cloth_SetGround(Model *model, s32 ground_y);
/* Has the model's cloth made again on its next update. */
extern void cloth_RequestRebuild(Model *model);
/* Has the cloth of the model in a slot hang at rest on its next update:
 * for when a fighter is put somewhere new. */
extern void cloth_RequestSnap(s32 slot);
extern void cloth_FreeModel(Model *model);
extern void cloth_FreeModelAt(s32 slot);
/* The model's data has moved by `delta` bytes. */
extern void cloth_RebaseModel(Model *model, s32 delta);

/* Used only inside cloth.c. */

/* The cloths. */
extern Cloth g_cloths[CLOTH_COUNT];

#endif /* CLOTH_H */
