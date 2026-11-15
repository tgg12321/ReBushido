/* What the scene starts from: the two view roots and the eight light setups
 * (sys_InitEngineState sets them up once). Compiled with small data. */
#include "common.h"
#include "bb2.h"

/* Defined further down. */
static const LightPacked g_light_default;
static void light_Unpack(LightSetup *setup, u16 *packed);

PlainNode g_node_identity_root;
PlainNode g_node_camera_root;

/* Sets up the two view roots, both unturned at the origin. They differ in
 * the order their angles apply in. */
void node_InitViewRoots(void) {
    SceneNode *camera_root = &g_node_camera_root.node;
    SceneNode *identity_root = &g_node_identity_root.node;
    camera_root->kind = NODE_KIND_CAMERA_ROOT;
    g_node_camera_root.node.draw_flags = 0;
    g_node_camera_root.node.rot.vx = 0;
    g_node_camera_root.node.rot.vy = 0;
    g_node_camera_root.node.rot.vz = 0;
    g_node_camera_root.node.local.t[0] = 0;
    g_node_camera_root.node.local.t[1] = 0;
    g_node_camera_root.node.local.t[2] = 0;
    SET_PTR(g_node_camera_root.node.parent, NULL);
    g_node_camera_root.node.rotation_order = ROTATION_ORDER_XYZ;
    node_SetInverseRotation(camera_root);
    identity_root->kind = NODE_KIND_IDENTITY_ROOT;
    g_node_identity_root.node.draw_flags = 0;
    g_node_identity_root.node.rot.vx = 0;
    g_node_identity_root.node.rot.vy = 0;
    g_node_identity_root.node.rot.vz = 0;
    g_node_identity_root.node.local.t[0] = 0;
    g_node_identity_root.node.local.t[1] = 0;
    g_node_identity_root.node.local.t[2] = 0;
    SET_PTR(g_node_identity_root.node.parent, NULL);
    g_node_identity_root.node.rotation_order = ROTATION_ORDER_ZXY;
    node_SetInverseRotation(identity_root);
    g_node_camera_root_ptr = camera_root;
    g_node_identity_root_ptr = identity_root;
}

/* Gives all eight light setups the default lighting and loads the first
 * into the GTE. */
void light_InitDefaults(void) {
    LightPacked packed;
    s32 i;
    packed = g_light_default;
    for (i = 0; i < LIGHT_SETUP_COUNT; i++) {
        light_Unpack(&g_light_setups[i], packed.v);
    }
    SetColorMatrix(&g_light_setups[0].cmat);
    SetBackColor(g_light_setups[0].back[0], g_light_setups[0].back[1],
                 g_light_setups[0].back[2]);
}

/* Fills in a light setup from its packed form (LightPacked). */
static void light_Unpack(LightSetup *setup, u16 *packed) {
    setup->cmat.m[0][0] = *packed++;
    setup->cmat.m[1][0] = *packed++;
    setup->cmat.m[2][0] = *packed++;
    setup->cmat.m[0][1] = *packed++;
    setup->cmat.m[1][1] = *packed++;
    setup->cmat.m[2][1] = *packed++;
    setup->cmat.m[0][2] = *packed++;
    setup->cmat.m[1][2] = *packed++;
    {
        s16 last = *packed++;
        setup->lmat.m[0][0] = 0;
        setup->lmat.m[0][1] = 0;
        setup->lmat.m[0][2] = 0;
        setup->lmat.m[1][0] = 0;
        setup->lmat.m[1][1] = 0;
        setup->lmat.m[1][2] = 0;
        setup->lmat.m[2][0] = 0;
        setup->lmat.m[2][1] = 0;
        setup->lmat.m[2][2] = 0;
        setup->cmat.m[2][2] = last;
    }
    setup->light[0].pitch = *packed++;
    setup->light[0].yaw = *packed++;
    setup->light[0].on = *packed++;
    setup->light[1].pitch = *packed++;
    setup->light[1].yaw = *packed++;
    setup->light[1].on = *packed++;
    setup->light[2].pitch = *packed++;
    setup->light[2].yaw = *packed++;
    setup->light[2].on = *packed++;
    light_CalcMatrix(setup);
    setup->back[0] = *packed++;
    setup->back[1] = *packed++;
    setup->back[2] = *packed;
    setup->light_dir_scale = *(packed + 1);
}

/* Works out a setup's light matrix: a row for each light that is on, the
 * vector pointing from where the light shines, light_dir_scale long.
 * Defined with an old-style parameter list, which is what matches. */
void light_CalcMatrix(setup) LightSetup *setup;
{
    s16 i;
    LightDir *light;
    s16 cos_pitch;
    s32 scaled;

    i = 0;
    do {
        light = &setup->light[i];
        if (light->on) {
            cos_pitch = rcos(light->pitch);
            scaled =
                ((rsin(light->yaw) * cos_pitch) >> 12) * setup->light_dir_scale;
            setup->lmat.m[i][0] = -scaled >> 12;
            scaled = rsin(light->pitch) * setup->light_dir_scale;
            setup->lmat.m[i][1] = scaled >> 12;
            scaled =
                ((rcos(light->yaw) * cos_pitch) >> 12) * setup->light_dir_scale;
            setup->lmat.m[i][2] = -scaled >> 12;
        } else {
            setup->lmat.m[i][0] = 0;
            setup->lmat.m[i][1] = 0;
            setup->lmat.m[i][2] = 0;
        }
        i++;
    } while (i < LIGHT_SETUP_LIGHTS);
}

/* The light setup that light_InitDefaults gives all eight: one light, nearly
 * white, and a dark gray where it does not fall. */
/* clang-format off */
static const LightPacked g_light_default = {{
    0x0E00, 0x0E00, 0x0E00, /* the first light's colour: red, green, blue */
    0x0000, 0x0000, 0x0000, /* the second's */
    0x0000, 0x0000, 0x0000, /* the third's */
    /* pitch,           yaw,                on */
    7 * ANGLE_TURN / 8, 5 * ANGLE_TURN / 8, 1,
    0,                  0,                  0,
    0,                  0,                  0,
    0x30, 0x30, 0x30,       /* the ambient colour */
    ONE,                    /* the length of the light vectors */
}};
/* clang-format on */
