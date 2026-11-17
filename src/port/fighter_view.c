/* What the remastered picture needs to draw a fighter's own model in place
 * of the game's primitives: his bones, the camera and the lights, as the
 * renderer comes to his body (port_fighter.h). Nothing here is the game's
 * state (tools/port_build.py, OUTSIDE_STATE) and nothing the game computes
 * is changed: it is read and written down. */
#include "common.h"
#include "port_gpu.h"
#include "bb2.h"
#include "port_fighter.h"

/* The near and far reach of render_SetupNode's scale for a skinned body
 * (render_asm.c, RENDER_SKIN_NEAR and RENDER_SKIN_FAR). */
#define SKIN_NEAR 0x5A00
#define SKIN_FAR 0xB500
#define HALF_MASK 0xFFFF

static PortFighterView g_views[PORT_FIGHTER_SLOTS];
static PortFighterWeapon g_weapons[PORT_FIGHTER_WEAPONS];
static s32 g_weapon_count;
static u32 g_serial;

static void port_FighterCopyMatrix(PortFighterMatrix *out, const MATRIX *in) {
    s32 i;

    for (i = 0; i < 9; i++) {
        out->m[i] = ((const s16 *)in->m)[i];
    }
    for (i = 0; i < 3; i++) {
        out->t[i] = in->t[i];
    }
}

static void port_FighterCopyLights(
    s16 *light, s16 *color, s32 *back, s32 setup) {
    const LightSetup *lights = &g_light_setups[setup];
    s32 i;

    for (i = 0; i < 9; i++) {
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
        s32 d = node->world.t[i] - g_node_camera_root_ptr->world.t[i];

        reach |= d < 0 ? -d : d;
    }
    return 2 - (reach < SKIN_NEAR) - (reach < SKIN_FAR);
}

static void port_FighterNoteBody(s32 slot, Model *model) {
    PortFighterView *view = &g_views[slot];
    s32 i;

    view->model_id = model->model_id;
    view->serial = g_serial;
    view->nodes_from = (PortAddress)model;
    view->nodes_to = (PortAddress)(model + 1);
    view->body_mesh = (PortAddress)ADDR(g_reloc_tables[model->mesh_block][0]);
    for (i = 0; i < PORT_FIGHTER_BONES; i++) {
        port_FighterCopyMatrix(&view->bone[i], &model->bones[i].node.world);
    }
    for (i = 0; i < 9; i++) {
        view->view[i] = ((const s16 *)g_render_view_matrix.m)[i];
    }
    for (i = 0; i < 3; i++) {
        view->camera[i] = g_node_camera_root_ptr->world.t[i];
    }
    /* the projection as the body is drawn with it: the tiny model and
     * the menus change it later in the frame */
    view->h = (s32)(port_GteReadControl(PORT_GTE_PLANE) & HALF_MASK);
    view->ofx = (s32)port_GteReadControl(PORT_GTE_OFX);
    view->ofy = (s32)port_GteReadControl(PORT_GTE_OFY);
    port_FighterCopyLights(view->light, view->color, view->back,
                           model->bones[1].node.light_setup_index);
    view->scale_shift = port_FighterScaleShift(&model->bones[1].node);
}

void port_FighterNoteNode(PortAddress at) {
    const SceneNode *node = (const SceneNode *)at;
    s32 slot;

    if (node == NULL) {
        return;
    }
    for (slot = 0; slot < PORT_FIGHTER_SLOTS; slot++) {
        Model *model = model_Get(slot);

        if (model != NULL && node == &model->bones[1].node &&
            node->kind == NODE_KIND_SKINNED) {
            port_FighterNoteBody(slot, model);
            return;
        }
    }
    /* a weapon in a hand or worn (weapon_Queue, weapon_QueueWorn): drawn
     * from the weapons' block, its shadow being a node of its own */
    if (node->mesh_block == RELOC_WEAPONS && node->kind == NODE_KIND_ROTATED &&
        g_weapon_count < PORT_FIGHTER_WEAPONS) {
        PortFighterWeapon *weapon = &g_weapons[g_weapon_count++];

        weapon->node = at;
        weapon->weapon = node->mesh;
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
    }
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
