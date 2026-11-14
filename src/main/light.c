/* Lights: the point light that effects set off, and each stage's sun,
 * ambient colours and distance colour. The light setups themselves are set
 * up in scene.c. Compiled with small data. */
#include "common.h"
#include "bb2.h"

/* Defined further down. */
static void light_SetStageColor(s32 stage, s32 color, s32 scenery, s32 ambient);
static void light_SetFarColor(s32 color);

/* Defined further down. */
static s16 g_light_point_color[3];

LightSetup g_light_setups[LIGHT_SETUP_COUNT];

/* A stage's lighting: packed colours 0xRRGGBB, the sun's direction, and the
 * colour things fade to in the distance. */
enum {
    PRESET_FIGHTER_SUN,
    PRESET_FIGHTER_AMBIENT,
    PRESET_SCENERY_SUN,
    PRESET_SCENERY_AMBIENT,
    PRESET_SUN_ANGLES, /* yaw in the low half, pitch in the high */
    PRESET_FAR_COLOR,
    PRESET_SIZE,
};

/* Defined after the functions. */
static s32 g_light_stage_presets[STAGE_COUNT][PRESET_SIZE];

/* Zeroed data. */
static VECTOR g_light_point_position;

/* The point light can reach a fighter this far away on each axis... */
#define POINT_LIGHT_BOX 0x7000
/* ...and this far away in all (the distance squared: 5000 units). */
#define POINT_LIGHT_RANGE_SQ 25000000

/* Is `d` less than `n` from 0, either way? */
#define WITHIN(d, n) ((d) >= 0 ? (d) < (n) : -(d) < (n))

/* ---- the point light ---- */

/* Puts the point light at `position` with colour `color` (0xRRGGBB). */
void light_SetPointLight(VECTOR *position, s32 color) {
    g_light_point_color[0] = MATH_BYTE_TO_ONE((color >> 16) & 0xFF);
    g_light_point_color[1] = MATH_BYTE_TO_ONE((color >> 8) & 0xFF);
    g_light_point_color[2] = MATH_BYTE_TO_ONE(color & 0xFF);
    g_light_point_position = *position;
}

/* Turns each fighter's second light to shine from the point light onto the
 * fighter at the given position (the address of three words; 0: that fighter
 * is not lit), or off if there is no point light or it is too far away. */
static void light_ApplyPointLight(s32 fighter_1_pos, s32 fighter_2_pos) {
    LightSetup *first_setup;
    s32 slot;
    LightSetup *setup;
    s32 dx;
    s32 dy;
    s32 dz;
    s32 yaw;
    s32 *pos;

    first_setup = g_light_setups;
    for (slot = 0; slot < 2; slot++) {
        setup = first_setup;
        if (slot == 0) {
            pos = (s32 *)fighter_1_pos;
        } else {
            pos = (s32 *)fighter_2_pos;
            setup = &g_light_setups[LIGHT_FIGHTER_2];
        }

        if (!pos || g_light_point_color[0] < 0) {
            goto off;
        }

        dx = pos[0] - g_light_point_position.vx;
        dy = pos[1] - g_light_point_position.vy;
        dz = pos[2] - g_light_point_position.vz;

        /* Outside the box the squares would overflow, and the light is
         * taken to reach: only inside it is the distance measured. */
        if (WITHIN(dx, POINT_LIGHT_BOX) && WITHIN(dy, POINT_LIGHT_BOX) &&
            WITHIN(dz, POINT_LIGHT_BOX) &&
            math_LengthSquared(dx, dy, dz) > POINT_LIGHT_RANGE_SQ) {
            goto off;
        }

        yaw = ratan2(dx, dz);
        setup->light[LIGHT_POINT].pitch =
            -ratan2(dy, (rcos(yaw) * dz + rsin(yaw) * dx) >> 12);
        setup->light[LIGHT_POINT].yaw = yaw;
        setup->light[LIGHT_POINT].on = 1;
        setup->cmat.m[0][LIGHT_POINT] = g_light_point_color[0];
        setup->cmat.m[1][LIGHT_POINT] = g_light_point_color[1];
        setup->cmat.m[2][LIGHT_POINT] = g_light_point_color[2];
        continue;

    off:
        setup->light[LIGHT_POINT].on = 0;
    }

    light_CalcMatrix(first_setup);
    light_CalcMatrix(&g_light_setups[LIGHT_FIGHTER_2]);
}

/* Where each fighter's model is this frame, and whether it said so. */
static s16 g_light_point_target_set[2];
static s32 g_light_point_targets[2];

/* Start of a frame: no point light and no fighters to light. */
void light_ResetPointLight(void) {
    g_light_point_target_set[1] = 0;
    g_light_point_target_set[0] = 0;
    g_light_point_color[0] = -1;
}

/* The model in slot `slot` is at `position` (the address of three words). */
void light_SetPointLightTarget(s32 slot, s32 position) {
    if (slot < 2) {
        g_light_point_target_set[slot] = 1;
        g_light_point_targets[slot] = position;
    }
}

/* Lights the fighters whose models were updated this frame. */
void light_ApplyPointLightTargets(void) {
    s32 who = g_light_point_target_set[0] * 2 + g_light_point_target_set[1];
    switch (who) {
    case 1:
        light_ApplyPointLight(0, g_light_point_targets[1]);
        break;
    case 2:
        light_ApplyPointLight(g_light_point_targets[0], 0);
        break;
    case 3:
        light_ApplyPointLight(
            g_light_point_targets[0], g_light_point_targets[1]);
        break;
    }
}

void light_DisablePointLight(void) { light_ApplyPointLight(0, 0); }

/* ---- a stage's lighting ---- */

/* Sets up the lighting of stage `stage`. */
void light_SetStage(s32 stage) {
    s32 *preset = g_light_stage_presets[stage];
    s32 angles;
    s32 pitch;
    light_SetStageColor(stage, *preset++, 0, 0);
    light_SetStageColor(stage, *preset++, 0, 1);
    light_SetStageColor(stage, *preset++, 1, 0);
    light_SetStageColor(stage, *preset++, 1, 1);
    angles = *preset;
    g_light_setups[LIGHT_SCENERY].light[LIGHT_SUN].yaw = angles & ANGLE_MASK;
    g_light_setups[LIGHT_FIGHTER_2].light[LIGHT_SUN].yaw = angles & ANGLE_MASK;
    g_light_setups[LIGHT_FIGHTER_1].light[LIGHT_SUN].yaw = angles & ANGLE_MASK;
    pitch = *preset >> 16;
    g_light_setups[LIGHT_SCENERY].light[LIGHT_SUN].pitch = pitch & ANGLE_MASK;
    g_light_setups[LIGHT_FIGHTER_2].light[LIGHT_SUN].pitch = pitch & ANGLE_MASK;
    g_light_setups[LIGHT_FIGHTER_1].light[LIGHT_SUN].pitch = pitch & ANGLE_MASK;
    light_SetFarColor(preset[1]);
}

/* Sets the sun's colour or (`ambient`) the ambient colour, of the fighters'
 * two setups or (`scenery`) of the scenery's. In black and white the colour
 * is made gray first. `stage` is not used. */
static void light_SetStageColor(
    s32 stage, s32 color, s32 scenery, s32 ambient) {
    s32 r = (color >> 16) & 0xFF;
    s32 g = (color >> 8) & 0xFF;
    s32 b = color & 0xFF;
    s16 sun_r;
    s16 sun_g;
    s16 sun_b;

    if (tex_IsGrayscale()) {
        r = g = b = math_Grayscale3(r, g, b);
    }
    if (!ambient) {
        sun_r = MATH_BYTE_TO_ONE(r);
        sun_g = MATH_BYTE_TO_ONE(g);
        sun_b = MATH_BYTE_TO_ONE(b);
        if (!scenery) {
            g_light_setups[LIGHT_FIGHTER_1].cmat.m[0][LIGHT_SUN] = sun_r;
            g_light_setups[LIGHT_FIGHTER_1].cmat.m[1][LIGHT_SUN] = sun_g;
            g_light_setups[LIGHT_FIGHTER_1].cmat.m[2][LIGHT_SUN] = sun_b;
            light_CalcMatrix(&g_light_setups[LIGHT_FIGHTER_1]);
            g_light_setups[LIGHT_FIGHTER_2].cmat.m[0][LIGHT_SUN] = sun_r;
            g_light_setups[LIGHT_FIGHTER_2].cmat.m[1][LIGHT_SUN] = sun_g;
            g_light_setups[LIGHT_FIGHTER_2].cmat.m[2][LIGHT_SUN] = sun_b;
            light_CalcMatrix(&g_light_setups[LIGHT_FIGHTER_2]);
        } else {
            g_light_setups[LIGHT_SCENERY].cmat.m[0][LIGHT_SUN] = sun_r;
            g_light_setups[LIGHT_SCENERY].cmat.m[1][LIGHT_SUN] = sun_g;
            g_light_setups[LIGHT_SCENERY].cmat.m[2][LIGHT_SUN] = sun_b;
            light_CalcMatrix(&g_light_setups[LIGHT_SCENERY]);
        }
    } else if (!scenery) {
        g_light_setups[LIGHT_FIGHTER_1].back[0] = r;
        g_light_setups[LIGHT_FIGHTER_1].back[1] = g;
        g_light_setups[LIGHT_FIGHTER_1].back[2] = b;
        g_light_setups[LIGHT_FIGHTER_2].back[0] = r;
        g_light_setups[LIGHT_FIGHTER_2].back[1] = g;
        g_light_setups[LIGHT_FIGHTER_2].back[2] = b;
    } else {
        g_light_setups[LIGHT_SCENERY].back[0] = r;
        g_light_setups[LIGHT_SCENERY].back[1] = g;
        g_light_setups[LIGHT_SCENERY].back[2] = b;
    }
}

/* Sets the colour of the distance: what the screen is cleared to and what
 * the depth cue fades things towards. */
static void light_SetFarColor(s32 color) {
    s32 r = (color >> 16) & 0xFF;
    s32 g = (color >> 8) & 0xFF;
    s32 b = color & 0xFF;
    if (tex_IsGrayscale()) {
        b = math_Grayscale3(r, g, b);
        g = b;
        r = b;
    }
    disp_SetBackground(1, r, g, b);
    SetFarColor(r, g, b);
}

/* ---- data ---- */

/* The lighting of each stage (the PRESET_ enum): the fighters' sun and
 * ambient colour, the scenery's sun and ambient colour, the sun's pitch
 * and yaw (angles of ANGLE_TURN a turn), and the colour of the distance.
 * A colour is 0xRRGGBB. */
#define SUN(pitch, yaw) ((pitch) << 16 | (yaw))
/* clang-format off */
static s32 g_light_stage_presets[STAGE_COUNT][PRESET_SIZE] = {
    /* STAGE_MAIN_TATARA_SHRINE */
    {0x98A7B5, 0x5F5D71, 0x9A84A3, 0x302846, SUN(0xE9F, 0xCA2), 0x000000},
    /* STAGE_MEIKYOKAN_DOJO */
    {0xDEDED6, 0x1E1E14, 0x9EA9B7, 0x2B3049, SUN(0xD0A, 0xC00), 0x000008},
    /* STAGE_SHAINTO_HQ */
    {0x7FA9DC, 0x36394E, 0xB7A2E3, 0x1F2E46, SUN(0xE30, 0x000), 0x000000},
    /* STAGE_TATARA_SHRINE */
    {0xDEDECA, 0x262F32, 0xCF96AC, 0x1F2730, SUN(0xD0A, 0xC00), 0x000810},
    /* STAGE_BUS_STOP */
    {0xEACD98, 0x18253B, 0x9CAFCC, 0x243E64, SUN(0xE2E, 0x1A0), 0x000018},
    /* STAGE_LOST_BRIDGE */
    {0xEBE8B3, 0x26274A, 0x8FB4D9, 0x1B2946, SUN(0xD3A, 0x20A), 0x000010},
    /* STAGE_TATARA_SHRINE_PATH */
    {0xB1D8F2, 0x3B3C25, 0x70B2DE, 0x3B4B5C, SUN(0xD80, 0xB50), 0x000000},
    /* STAGE_BAMBOO_THICKET */
    {0x8FC9EE, 0x312F26, 0x95BCD5, 0x2F261D, SUN(0xD00, 0x1A0), 0x000008},
    /* STAGE_OUTSIDE_MEIKYOKAN */
    {0xD2DDEC, 0x353446, 0x8995D0, 0x1A2E46, SUN(0xD80, 0x604), 0x000000},
    /* STAGE_FUTURE_DUMP_SITE */
    {0xE2E1B5, 0x272730, 0x000151, 0x797894, SUN(0xE49, 0x382), 0x101000},
    /* STAGE_SNAKE_CLIFF */
    {0xE3E3C4, 0x173146, 0x4F729B, 0x576CAD, SUN(0xD80, 0xEC4), 0x000000},
    /* STAGE_KIMIGAURA_COAST */
    {0xE1CCA5, 0x282638, 0xB5B4D4, 0x283649, SUN(0xE66, 0x2C9), 0x000810},
    /* STAGE_ORANGE_TREE_FIELDS */
    {0x87C8E7, 0x3C2A1B, 0x4B9EF1, 0x566F90, SUN(0xE0E, 0x1FC), 0x080800},
    /* STAGE_CASTLE_ROOFTOP */
    {0x8BB53A, 0x37426B, 0x76A7CF, 0x404B65, SUN(0xCC0, 0x2A2), 0x000010},
    /* STAGE_UNDER_LOST_BRIDGE */
    {0xC8D99C, 0x233149, 0x36425C, 0x232C2B, SUN(0xD3A, 0x20A), 0x000018},
    /* STAGE_TERRACED_FIELDS */
    {0xE5D5C4, 0x1D3145, 0x67AAFF, 0x5A6E7B, SUN(0xD80, 0xB28), 0x000000},
    /* STAGE_UNDER_RAILWAY */
    {0xC9BDAB, 0x534D44, 0x72C4FF, 0x324A57, SUN(0xD80, 0x708), 0x000000},
    /* STAGE_WATER_TOWER_DAM */
    {0xDBD5B4, 0x2D344C, 0x4F9CE2, 0x1F2837, SUN(0xD80, 0xA49), 0x000000},
    /* STAGE_LUMBERYARD */
    {0xD8D6CA, 0x303046, 0x6BA8DB, 0x272B3B, SUN(0xD80, 0x5C0), 0x080810},
    /* STAGE_LIGHTHOUSE */
    {0xA5C8F6, 0x2D404C, 0x60B5DE, 0x2D4458, SUN(0xD80, 0xA30), 0x000810},
    /* STAGE_KABUKI_STAGE */
    {0xC5A4BB, 0x3B2448, 0xC275E4, 0x453A57, SUN(0xEF8, 0xBA8), 0x080808},
    /* STAGE_ABANDONED_SHIP */
    {0x9FD2F0, 0x443332, 0x5DA6DC, 0x3F2A41, SUN(0xD80, 0x61F), 0x080810},
    /* STAGE_FERRY_DOCK */
    {0xDAC29C, 0x392B24, 0x95C7C5, 0x23333C, SUN(0xD80, 0x988), 0x000008},
    /* STAGE_SHAINTO_GATE */
    {0xD7D5BA, 0x2B3360, 0x8EB4D2, 0x20429B, SUN(0xE4F, 0x000), 0x000000},
    /* STAGE_PURPLE_CAVERN */
    {0xB9DAE6, 0x312847, 0xB9A088, 0x6A3D86, SUN(0xCC0, 0x73C), 0x000000},
    /* STAGE_ABANDONED_STATION */
    {0x98C1E7, 0x212530, 0x9BBDD5, 0x273B4F, SUN(0xD80, 0xA7D), 0x081010},
    /* STAGE_RAILWAY */
    {0x86DAFA, 0x31261B, 0x70D3F2, 0x2D3E4D, SUN(0xD80, 0x708), 0x000008},
    /* STAGE_ABANDONED_TUNNEL */
    {0x85CFEF, 0x3E3E48, 0x7CBFD7, 0x22384C, SUN(0xCC0, 0x788), 0x000008},
    /* STAGE_ABANDONED_SHIPYARD */
    {0xE9D6C8, 0x323E40, 0x5DB2DA, 0x3B3131, SUN(0xE31, 0x50E), 0x000008},
    /* STAGE_BUILDING_ROOFTOP */
    {0xA3C8E7, 0x362F44, 0x71B6D6, 0x355169, SUN(0xEB1, 0x4FB), 0x000000},
    /* STAGE_BUILDING_PARKING_LOT */
    {0xCBE7EF, 0x352F44, 0x71B6D6, 0x355169, SUN(0xCC0, 0x510), 0x000008},
    /* STAGE_SHAINTO_CEMETERY */
    {0xFFE4D1, 0x18324E, 0x785FD7, 0x372F70, SUN(0xE18, 0x275), 0x000010},
    /* STAGE_TEAM_1 */
    {0xD7D7D7, 0x1E1E1E, 0xB6B7AC, 0x3F3E46, SUN(0xD80, 0x800), 0x000008},
    /* STAGE_TEAM_2 */
    {0xDCDCDC, 0x26221B, 0xDCB4B1, 0x433D39, SUN(0xE50, 0x128), 0x000008},
    /* STAGE_CHANBARA */
    {0xD2D1D1, 0x202020, 0xC5C6C5, 0x474746, SUN(0xD80, 0xE8E), 0x000810},
    /* STAGE_PRACTICE_1 */
    {0xDBE4E2, 0x585656, 0x9DB1B1, 0x3C3C3B, SUN(0xD80, 0x800), 0x101020},
    /* STAGE_PRACTICE_2 */
    {0xD9D8D9, 0x242323, 0xBDBDBD, 0x444343, SUN(0xD80, 0x800), 0x000010},
    /* stage 37 */
    {0xE6E4A7, 0x161E28, 0x92AFCC, 0x1F253F, SUN(0xE00, 0x650), 0x000000},
};
/* clang-format on */
#undef SUN

/* The point light's colour, each part 0..ONE; red negative while there is
 * no point light. */
static s16 g_light_point_color[3] = {0, 0, 0};
