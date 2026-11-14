#ifndef LIGHT_H
#define LIGHT_H

/* Lighting. There are eight light setups and every node names the one that
 * lights it; three are used. A setup's first light is the stage's sun. Its
 * second is the "point light": a flash an effect sets off somewhere in the
 * world, turned for each fighter to shine from where the flash is. The sun
 * also casts the shadows (light.c, scene.c, and the shadow matrix in
 * scene_frame.c). */

#include "common.h"
#include <psxsdk/libgte.h>

#define LIGHT_SETUP_COUNT 8
#define LIGHT_SETUP_LIGHTS 3

/* One of a setup's three lights: where it shines from, and whether it is
 * on. */
typedef struct {
    s16 pitch;
    s16 yaw;
    s16 on;
    s16 pad;
} LightDir;

/* A light setup: the lights, the light matrix worked out from them
 * (light_CalcMatrix), their colours as a colour matrix (one column a light)
 * and the ambient colour. */
typedef struct {
    LightDir light[LIGHT_SETUP_LIGHTS]; /* +0x00 */
    MATRIX lmat;                        /* +0x18 */
    MATRIX cmat;                        /* +0x38 */
    u8 back[3];                         /* +0x58 */
    s16 light_dir_scale; /* +0x5C length of the light vectors, ONE = 1 */
} LightSetup;            /* 0x60 bytes */

/* The setups in use (SceneNode.light_setup_index). A fighter's is its model
 * slot. */
enum LightSetupId {
    LIGHT_FIGHTER_1 = 0,
    LIGHT_FIGHTER_2 = 1,
    LIGHT_SCENERY = 4, /* the stage's props, weapons, the surf */
};

/* A setup's lights. */
enum {
    LIGHT_SUN = 0,
    LIGHT_POINT = 1,
};

extern LightSetup g_light_setups[LIGHT_SETUP_COUNT];
/* The setup the renderer is lighting with. */
extern LightSetup *g_light_current;

/* A setup as 22 halfwords: the colour matrix by columns, three lights, the
 * ambient colour, the scale. */
typedef struct {
    u16 v[22];
} LightPacked;

/* Sets all eight setups to the default: one white light from above. */
extern void light_InitDefaults(void);
/* Works out a setup's light matrix from its lights' directions. */
extern void light_CalcMatrix(LightSetup *setup);

/* Sets the sun's colour and direction, the ambient colours and the colour
 * of the distance for stage `stage`. */
extern void light_SetStage(s32 stage);

/* The point light. An effect sets it each frame it lasts; the models say
 * where they are (model_Update); the lights are then turned towards it for
 * each fighter near enough. */
extern void light_SetPointLight(VECTOR *position, s32 color);
extern void light_DisablePointLight(void);
extern void light_ResetPointLight(void);
extern void light_SetPointLightTarget(s32 slot, s32 position);
extern void light_ApplyPointLightTargets(void);

/* Shadows: the matrix that slides a shape along the sun's direction down
 * onto level ground. */
extern void light_SetShadowMatrix(void);
/* `src` flattened onto the ground at height `ground_y`. */
extern void light_ProjectShadow(MATRIX *src, MATRIX *out, s32 ground_y);

#endif /* LIGHT_H */
