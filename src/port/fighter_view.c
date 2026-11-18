/* What the remastered picture needs to draw a fighter's own model in place
 * of the game's primitives: his bones, the camera and the lights, as the
 * renderer comes to his body (port_fighter.h). Nothing here is the game's
 * state (config/port_state.txt) and nothing the game computes
 * is changed: it is read and written down. */
#include "common.h"
#include "port_gpu.h"
#include "port_fighter.h"
#include "bb2.h"

/* The near and far reach of render_SetupNode's scale for a skinned body
 * (render_asm.c, RENDER_SKIN_NEAR and RENDER_SKIN_FAR). */
#define SKIN_NEAR 0x5A00
#define SKIN_FAR 0xB500
#define HALF_MASK 0xFFFF
/* a MATRIX's nine halfwords of rotation */
#define MATRIX_ELEMENTS 9

/* The tiny model is projected about another point of the screen
 * (render_DrawPart, TINY_CENTER_X and TINY_CENTER_Y), drawn at the scale
 * one (render_SetupNode), and its body is its first fifteen nodes, the
 * weapon and the thing held after them (model_tiny.c). */
#define TINY_CENTER_X 80
#define TINY_CENTER_Y 60
#define TINY_SCALE_SHIFT 1
#define TINY_BODY_MESHES 15
#define TINY_NODES 17

static PortFighterView g_views[PORT_FIGHTER_SLOTS];
static PortFighterView g_tiny;
static PortFighterWeapon g_weapons[PORT_FIGHTER_WEAPONS];
static s32 g_weapon_count;
static u32 g_serial;

static void port_FighterCopyMatrix(PortFighterMatrix *out, const MATRIX *in) {
    s32 i;

    for (i = 0; i < MATRIX_ELEMENTS; i++) {
        out->m[i] = ((const s16 *)in->m)[i];
    }
    for (i = 0; i < 3; i++) {
        out->t[i] = in->t[i];
    }
}

/* A light setup's light and colour matrices and its colour of the dark. */
static void port_FighterCopyLights(
    s16 *light, s16 *color, s32 *back, s32 setup) {
    const LightSetup *lights = &g_light_setups[setup];
    s32 i;

    for (i = 0; i < MATRIX_ELEMENTS; i++) {
        light[i] = ((const s16 *)lights->lmat.m)[i];
        color[i] = ((const s16 *)lights->cmat.m)[i];
    }
    for (i = 0; i < 3; i++) {
        back[i] = lights->back[i];
    }
}

/* The scale render_SetupNode draws a skinned body at: by how far its root
 * is from the camera, each way's distance or'd together. */
static s32 port_FighterScaleShift(const SceneNode *node) {
    s32 reach = 0;
    s32 i;

    for (i = 0; i < 3; i++) {
        s32 away = node->world.t[i] - g_node_camera_root_ptr->world.t[i];

        reach |= away < 0 ? -away : away;
    }
    return 2 - (reach < SKIN_NEAR) - (reach < SKIN_FAR);
}

/* A model's bones, and the eye it is seen with: its rotation and its
 * place. */
static void port_FighterNoteBones(PortFighterView *view, const Model *model,
                                  const MATRIX *eye, const s32 *place) {
    s32 i;

    view->serial = g_serial;
    for (i = 0; i < PORT_FIGHTER_BONES; i++) {
        port_FighterCopyMatrix(&view->bone[i], &model->bones[i].node.world);
    }
    for (i = 0; i < MATRIX_ELEMENTS; i++) {
        view->view[i] = ((const s16 *)eye->m)[i];
    }
    for (i = 0; i < 3; i++) {
        view->camera[i] = place[i];
    }
}

/* The slot's model is being drawn: its bones, the view, the projection and
 * the lights as the renderer has them now. */
static void port_FighterNoteBody(s32 slot, Model *model) {
    PortFighterView *view = &g_views[slot];

    view->model_id = model->model_id;
    view->nodes_from = (PortAddress)model;
    view->nodes_to = (PortAddress)(model + 1);
    view->body_mesh = (PortAddress)ADDR(g_reloc_tables[model->mesh_block][0]);
    port_FighterNoteBones(
        view, model, &g_render_view_matrix, g_node_camera_root_ptr->world.t);
    /* the projection as the body is drawn with it: the tiny model and
     * the menus change it later in the frame */
    view->h = (s32)(port_GteReadControl(PORT_GTE_PLANE) & HALF_MASK);
    view->ofx = (s32)port_GteReadControl(PORT_GTE_OFX);
    view->ofy = (s32)port_GteReadControl(PORT_GTE_OFY);
    port_FighterCopyLights(view->light, view->color, view->back,
                           model->bones[1].node.light_setup_index);
    view->scale_shift = port_FighterScaleShift(&model->bones[1].node);
}

/* The weapon model (enum WeaponModel) at a place of the weapons' block:
 * the loaded models are numbered in order (resource_LoadWeapons), so it
 * is the place-th of them. -1 if there is none. */
static s32 port_FighterWeaponModel(s32 place) {
    s32 i;

    for (i = 0; i < WEAPON_MODEL_COUNT; i++) {
        if (g_resource_loaded_weapons[i] >= 0 && place-- == 0) {
            return i;
        }
    }
    return -1;
}

/* The tiny model is being drawn (model_QueueTiny): its fighter's bones,
 * which its parts are on, seen through its own matrix and projected about
 * its corner of the screen, lit as its fighter. The first of its body's
 * nodes in a frame says it all; its nodes are in a row, each the number of
 * its mesh. */
static void port_FighterNoteTiny(const SceneNode *node) {
    const ModelNode *first;
    Model *model;
    s32 slot = node->light_setup_index;
    s32 tiny;

    if (node->mesh >= TINY_BODY_MESHES ||
        (g_tiny.model_id >= 0 && g_tiny.serial == g_serial) || slot < 0 ||
        slot >= PORT_FIGHTER_SLOTS) {
        return;
    }
    model = model_Get(slot);
    if (model == NULL) {
        return;
    }
    tiny = g_model_tiny_ids[model->model_id];
    if (tiny == MODEL_TINY_NONE) {
        return;
    }
    first = (const ModelNode *)node - node->mesh;
    g_tiny.model_id = tiny;
    g_tiny.nodes_from = (PortAddress)first;
    g_tiny.nodes_to = (PortAddress)(first + TINY_NODES);
    g_tiny.body_mesh = 0;
    port_FighterNoteBones(
        &g_tiny, model, &g_model_tiny_matrix, g_model_tiny_matrix.t);
    g_tiny.h = (s32)(port_GteReadControl(PORT_GTE_PLANE) & HALF_MASK);
    g_tiny.ofx = TINY_CENTER_X << 16;
    g_tiny.ofy = TINY_CENTER_Y << 16;
    port_FighterCopyLights(g_tiny.light, g_tiny.color, g_tiny.back, slot);
    g_tiny.scale_shift = TINY_SCALE_SHIFT;
    g_tiny.cloth_count = 0;
}

s32 port_FighterIsTinyBody(PortAddress node) {
    return node != 0 && ((const SceneNode *)node)->kind == NODE_KIND_TINY &&
           ((const SceneNode *)node)->mesh < TINY_BODY_MESHES;
}

const PortFighterView *port_GetFighterTinyView(void) { return &g_tiny; }

/* A cloth of the slot's model is being drawn (render_DrawCloth): its
 * points as the game has moved them, which are what it is drawn from. The
 * mesh's first halfword is how many vertices it has, a point for each. */
static void port_FighterNoteCloth(s32 slot, const SceneNode *node) {
    PortFighterView *view = &g_views[slot];
    const ClothPoint *points =
        (const ClothPoint *)PTR(((const ModelNode *)node)->cloth_points);
    const u16 *mesh =
        (const u16 *)ADDR(g_reloc_tables[node->mesh_block][node->mesh]);
    PortFighterCloth *cloth;
    s32 i;
    s32 k;

    if (points == NULL || mesh == NULL ||
        view->cloth_count >= PORT_FIGHTER_CLOTHS ||
        mesh[0] > PORT_FIGHTER_CLOTH_POINTS) {
        return;
    }
    cloth = &view->cloth[view->cloth_count++];
    cloth->node = (PortAddress)node;
    cloth->mesh = node->mesh;
    cloth->count = mesh[0];
    for (i = 0; i < cloth->count; i++) {
        for (k = 0; k < 3; k++) {
            cloth->point[i][k] = points[i].pos[k];
        }
    }
}

s32 port_FighterLoadedModel(s32 slot) {
    Model *model;

    if (slot < 0 || slot >= PORT_FIGHTER_SLOTS) {
        return -1;
    }
    model = model_Get(slot);
    return model != NULL ? model->model_id : -1;
}

s32 port_FighterLoadedTiny(s32 slot) {
    s32 model_id = port_FighterLoadedModel(slot);

    if (model_id < 0 || g_model_tiny_ids[model_id] == MODEL_TINY_NONE) {
        return -1;
    }
    return g_model_tiny_ids[model_id];
}

s32 port_FighterLoadedWeapon(s32 place) {
    return place >= 0 ? port_FighterWeaponModel(place) : -1;
}

/* A fighter's own node (port_fighter.h): his body's root, which draws the
 * whole body, a cloth, or a weapon. */
void port_FighterNoteNode(PortAddress at) {
    const SceneNode *node = (const SceneNode *)at;
    s32 slot;

    if (node == NULL) {
        return;
    }
    if (node->kind == NODE_KIND_TINY) {
        port_FighterNoteTiny(node);
        return;
    }
    for (slot = 0; slot < PORT_FIGHTER_SLOTS; slot++) {
        Model *model = model_Get(slot);

        /* a body drawn as outlines (model_SetOutline: the fighter looked
         * through in the first-person view) is the game's own lines: no
         * model stands in for it */
        if (model != NULL && node == &model->bones[1].node &&
            node->kind == NODE_KIND_SKINNED) {
            if (!(node->draw_flags & NODE_DRAW_OUTLINE)) {
                port_FighterNoteBody(slot, model);
            }
            return;
        }
        if (model != NULL && node->kind == NODE_KIND_CLOTH &&
            at >= (PortAddress)model && at < (PortAddress)(model + 1)) {
            port_FighterNoteCloth(slot, node);
            return;
        }
    }
    /* a weapon in a hand or worn (weapon_Queue, weapon_QueueWorn): drawn
     * from the weapons' block, its shadow being a node of its own */
    if (node->mesh_block == RELOC_WEAPONS && node->kind == NODE_KIND_ROTATED &&
        g_weapon_count < PORT_FIGHTER_WEAPONS) {
        PortFighterWeapon *weapon = &g_weapons[g_weapon_count++];

        weapon->node = at;
        weapon->weapon = port_FighterWeaponModel(node->mesh / 2);
        weapon->serial = g_serial;
        port_FighterCopyMatrix(&weapon->world, &node->world);
        port_FighterCopyLights(weapon->light, weapon->color, weapon->back,
                               node->light_setup_index);
    }
}

void port_FighterNewFrame(void) {
    s32 slot;

    g_serial++;
    for (slot = 0; slot < PORT_FIGHTER_SLOTS; slot++) {
        g_views[slot].model_id = -1;
        g_views[slot].cloth_count = 0;
    }
    g_tiny.model_id = -1;
    g_weapon_count = 0;
}

const PortFighterView *port_GetFighterView(s32 slot) {
    if (slot < 0 || slot >= PORT_FIGHTER_SLOTS) {
        return NULL;
    }
    return &g_views[slot];
}

const PortFighterWeapon *port_GetFighterWeapons(s32 *count) {
    if (count != NULL) {
        *count = g_weapon_count;
    }
    return g_weapons;
}

u32 port_FighterSerial(void) { return g_serial; }

/* The cloth is moved by the game itself (render_DrawCloth) and is no part
 * of a model's file: it stays the game's own primitives. Its nodes are
 * among the model's own (cloth_model.c), so they are told apart here. */
s32 port_FighterIsCloth(PortAddress node) {
    return node != 0 && ((const SceneNode *)node)->kind == NODE_KIND_CLOTH;
}
