/* Cloth, a model's: makes the cloths a model file asks for, steps them every
 * frame with the model's limbs as ellipsoids to stay out of, and keeps up
 * when the model's data moves (see cloth.h). */
#define INCLUDE_ASM_USE_MACRO_INC 1
#include "common.h"
#include "include_asm.h"
#include <psxsdk/inline_o.h>
#include <psxsdk/gtemac.h>
#include "bb2.h"

/* Defined further down. */
static void cloth_MoveColliders(ModelCloth *cloth);
static void cloth_SetSnap(ModelCloth *cloth);
static void cloth_ClearSnap(ModelCloth *cloth);
static u8 *cloth_CreatePiece(ClothPiece *piece, s16 *params, u8 *data);
static s16 *cloth_ReadPins(SVECTOR *vertices, s32 count, s16 *params);
static void cloth_ReadColliders(Model *model, ClothPiece *piece, s16 *params);

static s32 g_cloth_scene_collider_a[CLOTH_COLLIDERS];
static s32 g_cloth_scene_collider_b[CLOTH_COLLIDERS];

/* Two words cleared with the scene's colliders, as every frame's scene
 * begins and when the engine starts; nothing reads them. (Guess) more of
 * the same thing the game no longer does. */
static s32 g_cloth_unused_scene_a;
static s32 g_cloth_unused_scene_b;

/* ---- something the game no longer does ----
 *
 * Three pairs of words that are cleared as every frame's scene begins and
 * handed straight to a function in cloth.c that does nothing; the function
 * that would set a pair is never called. Read here as ellipsoids for every
 * cloth to stay out of, from before each piece had its own: there are three,
 * as a piece has, and two words would be the two foci. That is a guess. */

void cloth_ClearSceneColliders(void) {
    g_cloth_unused_scene_b = 0;
    g_cloth_unused_scene_a = 0;
    g_cloth_scene_collider_a[2] = 0;
    g_cloth_scene_collider_a[1] = 0;
    g_cloth_scene_collider_a[0] = 0;
    g_cloth_scene_collider_b[2] = 0;
    g_cloth_scene_collider_b[1] = 0;
    g_cloth_scene_collider_b[0] = 0;
}

/* Sets one of the three pairs cloth_SendSceneColliders sends. Nothing calls
 * it. */
static void cloth_StoreSceneCollider(
    s32 first_word, s32 second_word, s32 index) {
    g_cloth_scene_collider_a[index] = first_word;
    g_cloth_scene_collider_b[index] = second_word;
}

void cloth_SendSceneColliders(void) {
    cloth_SetSceneCollider(
        g_cloth_scene_collider_a[0], g_cloth_scene_collider_b[0], 0);
    cloth_SetSceneCollider(
        g_cloth_scene_collider_a[1], g_cloth_scene_collider_b[1], 1);
    cloth_SetSceneCollider(
        g_cloth_scene_collider_a[2], g_cloth_scene_collider_b[2], 2);
}

/* ---- a model's cloth ---- */

/* Steps a model's cloth a frame: makes it again or puts it at rest first if
 * that was asked for, moves its ellipsoids with the bones, then steps the
 * pieces. */
void cloth_UpdateModel(Model *model) {
    ModelCloth *cloth;
    cloth = model->cloth;
    if (!cloth) {
        return;
    }
    if (cloth->rebuild_request) {
        cloth_FreeModelAt(model->slot);
        cloth_SetupModel(model, 0);
    }
    if (cloth->snap_request) {
        cloth_SetSnap(cloth);
    }
    cloth_MoveColliders(cloth);
    cloth_Update(cloth->updates, cloth->count);
    if (cloth->snap_request) {
        cloth_ClearSnap(cloth);
        cloth->snap_request = 0;
    }
}

/* Works out where every piece's ellipsoids are now, from the bones they
 * ride on. */
static void cloth_MoveColliders(ModelCloth *cloth) {
    s32 i;
    s32 j;

    for (i = 0; i < cloth->count; i++) {
        ClothBoneColliders *colliders = &cloth->pieces[i].colliders;
        for (j = 0; j < colliders->count; j++) {
            MATRIX *matrix = &colliders->bones[j]->node.world;
            math_Transform(matrix, &colliders->local_foci[j][0],
                           &colliders->world.foci[j][0]);
            math_Transform(matrix, &colliders->local_foci[j][1],
                           &colliders->world.foci[j][1]);
        }
    }
}

void cloth_Init(void) {
    cloth_FreeAll();
    cloth_ResetSceneColliders();
    g_cloth_unused_scene_a = 0;
    g_cloth_unused_scene_b = 0;
}

/* Reads the model file's cloth section (laid out in cloth.h) and makes a
 * piece for each mesh it says is cloth. The pieces' vertices, points and
 * springs go after the ModelCloth, one piece after another. */
void cloth_SetupModel(Model *model, s32 resize_data) {
    ModelCloth *cloth;
    s16 *params;
    u8 *data;
    ClothPiece *piece;
    ModelNode *node;
    s32 mesh;

    cloth = model->cloth;
    if (!cloth) {
        return;
    }
    params = (s16 *)model->cloth_params;
    data = cloth->data;
    if (*params == CLOTH_FILE_END) {
        model->cloth = NULL;
        return;
    }
    cloth->count = 0;
    cloth->snap_request = 0;
    cloth->unk4 = 0;
    cloth->rebuild_request = 0;
    for (mesh = 0; *params != CLOTH_FILE_END; mesh++) {
        if (*params != CLOTH_FILE_NOT_CLOTH) {
            if (cloth->count >= MODEL_CLOTH_PIECES) {
                sys_Trap();
            }
            piece = &cloth->pieces[cloth->count];
            node = model->mesh_nodes[mesh];
            piece->data = data;
            piece->node = node;
            data = cloth_CreatePiece(piece, params, data);
            cloth->updates[cloth->count] = piece->update;
            node->node.kind = NODE_KIND_CLOTH;
            cloth->count++;
            /* past the pins, which cloth_CreatePiece has read */
            if (*params >= 0) {
                params++;
                while (*params >= 0) {
                    params++;
                }
            }
            if (*params == CLOTH_FILE_COLLIDERS) {
                params++;
                cloth_ReadColliders(model, piece, params);
            }
        }
        while (*params++ != CLOTH_FILE_NEXT) {
        }
    }
    if (resize_data) {
        resource_ResizeModelData(model->slot, data - (u8 *)model->data);
    }
}

/* The kind of spring the faces' points are joined with (ClothSpring.kind). */
#define CLOTH_FACE_SPRING 2

/* Makes the cloth of one mesh at `data`: a copy of the mesh's vertices with
 * room for each one's pin, then the points and springs. The list of faces
 * cloth_Create wants is made in the scratchpad. Returns where the next
 * piece's data can go. */
static u8 *cloth_CreatePiece(ClothPiece *piece, s16 *params, u8 *data) {
    ClothDesc desc;
    s32 unused[2]; /* nothing uses it */

    ModelNode *node;
    u16 *src;
    u16 *faces;
    s16 count;
    s16 flags;
    s32 kind;
    s32 spring_capacity;
    s32 corners;
    s32 face_count; /* of a group; `count` is what is left of it */
    s16 *face;
    u16 *dst;

    node = piece->node;
    dst = (u16 *)data;
    src = (u16 *)g_reloc_tables[node->node.mesh_block][node->node.mesh];
    count = *src;
    desc.count = count;
    desc.vertices = (SVECTOR *)data;
    src += 2;
    for (count--; count != -1; count--) {
        dst[0] = *src++;
        dst[1] = *src++;
        dst[2] = *src++;
        dst += 4;
    }
    data = (u8 *)dst;
    if ((u32)src & 3) {
        src++;
    }

    /* count the springs the faces can need: one for every two corners,
     * which is six to a quad and three to a triangle */
    faces = src;
    spring_capacity = 0;
    count = *src++;
    while (count != 0) {
        flags = *src++;
        if (flags & (MESH_FACE_QUAD << 3)) {
            spring_capacity += count * 6; /* a quad's */
        } else {
            spring_capacity += count * 3;
        }
        src += g_cloth_face_strides[MESH_FACE_KIND(flags)] * count;
        count = *src++;
    }

    desc.buffer = data;
    data = data + cloth_BufferSize(desc.count, spring_capacity);
    /* on to a multiple of four, from an even address; here and below */
    data = ((u32)data & 3) ? data + 2 : data;

    src = faces;
    face = (s16 *)SCRATCHPAD;
    desc.faces = face;
    /* FAKE: while ((count = *src++) != 0) */
    for (face_count = (s16)*src++, count = face_count; face_count != 0;
         face_count = (s16)*src++, count = face_count) {
        flags = *src++;
        /* FAKE: kind = MESH_FACE_KIND(flags), and the tests are on `kind` */
        kind = (flags >> 3) & 3;
        if ((flags >> 3) & MESH_FACE_QUAD) {
            while (--count != -1) {
                if ((flags >> 3) & MESH_FACE_LONG) {
                    MeshQuadLong *quad = (MeshQuadLong *)src;
                    corners = (quad->vertices[1] << 16) | quad->vertices[0];
                } else {
                    MeshQuad *quad = (MeshQuad *)src;
                    corners = (quad->vertices[1] << 16) | quad->vertices[0];
                }
                *face++ = 4;
                *face++ = CLOTH_FACE_SPRING;
                *face++ = corners & 0xFF;
                *face++ = (corners >> 8) & 0xFF;
                *face++ = (corners >> 16) & 0xFF;
                *face++ = (u32)corners >> 24;
                src += g_cloth_face_strides[kind];
            }
        } else {
            while (--count != -1) {
                if ((flags >> 3) & MESH_FACE_LONG) {
                    MeshTriLong *tri = (MeshTriLong *)src;
                    corners = (tri->vertices[1] << 16) | tri->vertices[0];
                } else {
                    MeshTri *tri = (MeshTri *)src;
                    corners = (tri->vertices[1] << 16) | tri->vertices[0];
                }
                *face++ = 3;
                *face++ = CLOTH_FACE_SPRING;
                *face++ = corners & 0xFF;
                *face++ = (corners >> 8) & 0xFF;
                *face++ = (corners >> 16) & 0xFF;
                src += g_cloth_face_strides[kind];
            }
        }
        /* the list ends here, after every group: cloth_Build reads no
         * further than the first group's faces (the original's behaviour).
         * Where a mesh of the game's has a second group, its faces join
         * no two points the first's do not (tests/cloth.py) */
        *face++ = 0;
    }

    data = ((u32)data & 3) ? data + 2 : data;
    arena_UpdatePeak((s32)data);
    /* a cloth mesh must be all in one part */
    if (*src != 0) {
        sys_Trap("Multipul Model");
    }
    cloth_ReadPins(desc.vertices, desc.count, params);

    desc.matrix = &node->node.world;
    desc.unk2 = 0xE00; /* not understood: nothing reads it */
    /* -1 if every cloth is in use, which nothing here looks for: the
     * original's behaviour */
    piece->cloth = cloth_Create(&desc);
    node->cloth_points = desc.buffer;
    piece->colliders.count = 0;
    piece->colliders.world.count = 0;
    data = ((u32)data & 3) ? data + 2 : data;
    piece->update.cloth = piece->cloth;
    piece->update.matrix = &node->node.world;
    piece->update.buffer = node->cloth_points;
    piece->update.colliders = &piece->colliders.world;
    piece->update.snap = 0;
    piece->update.unk3 = 0;
    return data;
}

/* Reads a mesh's pins from the file into its vertices' `pad`: every vertex
 * is loose unless a group says otherwise. Returns what follows the pins. */
static s16 *cloth_ReadPins(SVECTOR *vertices, s32 count, s16 *params) {
    s32 i; /* a counter, then a vertex number */
    for (i = 0; i < count; i++) {
        vertices[i].pad = CLOTH_PIN_LOOSE;
    }

    {
        s32 group_count;
        group_count = *params++;
        while (group_count >= 0) {
            s32 pin;
            pin = *params++;
            group_count--;
            while (group_count != -1) {
                i = *params++;
                group_count--;
                vertices[i].pad = pin;
            }
            group_count = *params++;
        }
    }
    return params;
}

/* Reads a mesh's ellipsoids from the file. */
static void cloth_ReadColliders(Model *model, ClothPiece *piece, s16 *params) {
    ClothBoneColliders *colliders = &piece->colliders;
    s32 count = colliders->count;
    s16 bone = params[0];

    if (bone != CLOTH_FILE_NEXT) {
        do {
            params++;
            colliders->bones[count] = &model->bones[bone + 1];
            colliders->local_foci[count][0].vx = *params++;
            colliders->local_foci[count][0].vy = *params++;
            colliders->local_foci[count][0].vz = *params++;
            colliders->local_foci[count][1].vx = *params++;
            colliders->local_foci[count][1].vy = *params++;
            colliders->local_foci[count][1].vz = *params++;
            colliders->world.reach[count] = *params++;
            count++;
            bone = *params;
        } while (bone != CLOTH_FILE_NEXT);
    }
    colliders->count = count;
    colliders->world.count = count;
}

/* Nothing calls it: cloth_CreatePiece rounds its addresses up itself. */
static s32 cloth_AlignUp4(s32 value) {
    if (value & 3) {
        value = (value + 3) & ~3;
    }
    return value;
}

void cloth_RequestRebuild(Model *model) {
    ModelCloth *cloth = model->cloth;
    if (cloth) {
        cloth->rebuild_request = 1;
    }
}

void cloth_RequestSnap(s32 slot) {
    Model *model = model_Get(slot);
    if (model) {
        ModelCloth *cloth = model->cloth;
        if (cloth) {
            cloth->snap_request = 1;
        }
    }
}

static void cloth_SetSnap(ModelCloth *cloth) {
    s32 i;
    for (i = 0; i < cloth->count; i++) {
        cloth->updates[i].snap = 1;
        cloth->pieces[i].update.snap = 1;
    }
}

static void cloth_ClearSnap(ModelCloth *cloth) {
    s32 i;
    for (i = 0; i < cloth->count; i++) {
        cloth->updates[i].snap = 0;
        cloth->pieces[i].update.snap = 0;
    }
}

void cloth_SetGround(Model *model, s32 ground_y) {
    ModelCloth *cloth = model->cloth;
    if (cloth) {
        s32 i;
        for (i = 0; i < cloth->count; i++) {
            cloth->pieces[i].colliders.world.ground_y = ground_y;
        }
    }
}

void cloth_FreeModel(Model *model) {
    ModelCloth *cloth;
    s32 i;
    cloth = model->cloth;
    if (cloth) {
        for (i = 0; i < cloth->count; i++) {
            cloth_Free(cloth->pieces[i].cloth);
        }
    }
}

void cloth_FreeModelAt(s32 slot) {
    Model *model = model_Get(slot);
    if (model) {
        cloth_FreeModel(model);
    }
}

/* The pieces hold addresses inside the data that moved, so they are made
 * again on the next update. */
void cloth_RebaseModel(Model *model, s32 delta) {
    ModelCloth *cloth = model->cloth;
    if (cloth) {
        cloth = (ModelCloth *)((u8 *)cloth + delta);
        model->cloth = cloth;
        model->cloth_params = model->cloth_params + delta;
        cloth->rebuild_request = 1;
    }
}
