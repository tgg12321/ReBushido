/* The effects' tables (eff.h): where their textures are in video memory,
 * every effect's handlers, the view's scale, and the corners of a muzzle
 * flash's three quads and of a footprint.
 *
 * The file has no code. It is built without small data, which keeps its
 * single records here among the others. */
#include "common.h"
#include "bb2.h"

/* ---- textures ----
 * Each is {clut x, clut y, u, v}: where the texture's colour table is in
 * video memory, and its top left corner in its texture page. The pages are
 * the two that every GAME*.BNK fills (docs/formats/BNK.md); the size of a
 * texture is in eff.c, with the effect that draws it. A table of several is
 * an effect's frames or its kinds. */

TexRec g_eff_drop_tex[4] = {
    {896, 255, 160, 224},
    {896, 255, 160, 232},
    {896, 255, 160, 232},
    {896, 255, 160, 232},
};
TexRec g_eff_water_tex[3] = {
    {896, 255, 160, 232},
    {896, 255, 160, 232},
    {896, 255, 160, 232},
};
TexRec g_eff_star_tex[1] = {{896, 252, 0, 0}};
TexRec g_eff_spark_far_tex[1] = {{912, 252, 64, 0}};
TexRec g_eff_spark_near_tex[1] = {{928, 252, 64, 64}};
TexRec g_eff_lock_flash_alt_tex[1] = {{944, 252, 64, 128}};
TexRec g_eff_dust_texs[7] = {
    {912, 255, 192, 0},   {912, 255, 192, 32},  {912, 255, 192, 64},
    {912, 255, 192, 96},  {912, 255, 192, 128}, {912, 255, 192, 160},
    {912, 255, 192, 192},
};
TexRec g_eff_footprint_texs[4] = {
    {912, 254, 128, 224},
    {912, 254, 144, 224},
    {928, 254, 152, 224},
    {944, 254, 136, 224},
};
TexRec g_eff_splash_crown_tex[1] = {{928, 255, 128, 0}};
TexRec g_eff_splash_drops_tex[1] = {{928, 255, 128, 64}};
TexRec g_eff_splash_drops_late_tex[1] = {{928, 255, 128, 128}};
TexRec g_eff_muzzle_flash_end_tex[1] = {{928, 253, 0, 192}};
TexRec g_eff_muzzle_flash_side_tex[1] = {{928, 253, 64, 192}};
TexRec g_eff_muzzle_smoke_end_tex[1] = {{928, 253, 32, 192}};
TexRec g_eff_muzzle_smoke_side_tex[1] = {{928, 253, 128, 192}};
TexRec g_eff_glow_late_tex[1] = {{896, 253, 0, 64}};
TexRec g_eff_glow_tex[1] = {{912, 253, 0, 128}};
TexRec g_eff_streak_tex[1] = {{944, 253, 0, 224}};
TexRec g_eff_streak_late_tex[1] = {{896, 254, 64, 224}};
TexRec g_eff_dirt_texs[4] = {
    {992, 252, 192, 64},
    {992, 252, 224, 64},
    {992, 252, 192, 96},
    {992, 252, 224, 96},
};
TexRec g_eff_grass_texs[4] = {
    {976, 252, 192, 0},
    {976, 252, 224, 0},
    {976, 252, 192, 32},
    {976, 252, 224, 32},
};
TexRec g_eff_streak_close_tex[1] = {{960, 252, 0, 0}};
TexRec g_eff_streak_close_late_tex[1] = {{960, 252, 64, 0}};
TexRec g_eff_blunt_hit_tex[1] = {{992, 253, 64, 180}};
TexRec g_eff_blunt_hit_mid_tex[2] = {
    {992, 253, 0, 180},
    {960, 252, 0, 0},
};
/* The fire's six frames, the torch's four, then a grey palette for each. */
TexRec g_eff_flame_texs[12] = {
    {960, 253, 0, 148},   {960, 253, 32, 148},  {960, 253, 64, 148},
    {960, 253, 96, 148},  {960, 253, 128, 148}, {960, 253, 160, 148},
    {1008, 252, 0, 128},  {1008, 252, 16, 128}, {1008, 252, 32, 128},
    {1008, 252, 48, 128}, {960, 254, 0, 148},   {1008, 253, 48, 128},
};

/* ---- handlers ---- */

/* Where each effect's handlers begin in g_eff_handlers. */
/* clang-format off */
u8 g_eff_first_handler[EFF_COUNT] = {
    0,  /* EFF_FOOTPRINTS_0 */
    2,  /* EFF_FOOTPRINTS_1 */
    4,  /* EFF_BLOOD_0 */
    7,  /* EFF_BLOOD_1 */
    10, /* EFF_LOCK_FLASH */
    12, /* EFF_CLASH_SPARK_0 */
    14, /* EFF_CLASH_SPARK_1 */
    16, /* EFF_DUST */
    18, /* EFF_SPLASH */
    20, /* EFF_HIT_FLASH_0 */
    22, /* EFF_HIT_FLASH_1 */
    24, /* EFF_LOCK_FLASH_RATED */
    26, /* EFF_CLASH_STREAK_0 */
    28, /* EFF_CLASH_STREAK_1 */
    30, /* EFF_MATCHED_STREAK_0 */
    32, /* EFF_MATCHED_STREAK_1 */
    34, /* EFF_BLUNT_HIT_0 */
    36, /* EFF_BLUNT_HIT_1 */
    38, /* EFF_ARMOR_SPARK_0 */
    40, /* EFF_ARMOR_SPARK_1 */
    42, /* EFF_IMPACT_0 */
    45, /* EFF_IMPACT_1 */
    48, /* EFF_DIRT_0 */
    51, /* EFF_DIRT_1 */
    54, /* EFF_WATER_0 */
    57, /* EFF_WATER_1 */
    60, /* EFF_MUZZLE_FLASH */
    0,  /* not used */
};
/* clang-format on */

/* Every effect's handlers, in turn: the one or two that start it, then
 * the one that runs and draws it each frame. */
void *g_eff_handlers[62] = {
    /* EFF_FOOTPRINTS_0 */
    eff_SpawnFootprint0,
    eff_DrawFootprints0,
    /* EFF_FOOTPRINTS_1 */
    eff_SpawnFootprint1,
    eff_DrawFootprints1,
    /* EFF_BLOOD_0 */
    eff_SpawnBlood0A,
    eff_SpawnBlood0B,
    eff_DrawBlood0,
    /* EFF_BLOOD_1 */
    eff_SpawnBlood1A,
    eff_SpawnBlood1B,
    eff_DrawBlood1,
    /* EFF_LOCK_FLASH */
    eff_SpawnLockFlash,
    eff_DrawLockFlash,
    /* EFF_CLASH_SPARK_0 */
    eff_SpawnClashSpark0,
    eff_DrawClashSpark0,
    /* EFF_CLASH_SPARK_1 */
    eff_SpawnClashSpark1,
    eff_DrawClashSpark1,
    /* EFF_DUST */
    eff_SpawnDust,
    eff_DrawDust,
    /* EFF_SPLASH */
    eff_SpawnSplash,
    eff_DrawSplashes,
    /* EFF_HIT_FLASH_0 */
    eff_SpawnHitFlash0,
    eff_DrawHitFlash0,
    /* EFF_HIT_FLASH_1 */
    eff_SpawnHitFlash1,
    eff_DrawHitFlash1,
    /* EFF_LOCK_FLASH_RATED */
    eff_SpawnLockFlashRated,
    eff_DrawLockFlashRated,
    /* EFF_CLASH_STREAK_0 */
    eff_SpawnClashStreak0,
    eff_DrawClashStreak0,
    /* EFF_CLASH_STREAK_1 */
    eff_SpawnClashStreak1,
    eff_DrawClashStreak1,
    /* EFF_MATCHED_STREAK_0 */
    eff_SpawnMatchedStreak0,
    eff_DrawMatchedStreak0,
    /* EFF_MATCHED_STREAK_1 */
    eff_SpawnMatchedStreak1,
    eff_DrawMatchedStreak1,
    /* EFF_BLUNT_HIT_0 */
    eff_SpawnBluntHit0,
    eff_DrawBluntHit0,
    /* EFF_BLUNT_HIT_1 */
    eff_SpawnBluntHit1,
    eff_DrawBluntHit1,
    /* EFF_ARMOR_SPARK_0 */
    eff_SpawnArmorSpark0,
    eff_DrawArmorSpark0,
    /* EFF_ARMOR_SPARK_1 */
    eff_SpawnArmorSpark1,
    eff_DrawArmorSpark1,
    /* EFF_IMPACT_0 */
    eff_SpawnImpact0A,
    eff_SpawnImpact0B,
    eff_DrawImpact0,
    /* EFF_IMPACT_1 */
    eff_SpawnImpact1A,
    eff_SpawnImpact1B,
    eff_DrawImpact1,
    /* EFF_DIRT_0 */
    eff_SpawnDirt0A,
    eff_SpawnDirt0B,
    eff_DrawDirt0,
    /* EFF_DIRT_1 */
    eff_SpawnDirt1A,
    eff_SpawnDirt1B,
    eff_DrawDirt1,
    /* EFF_WATER_0 */
    eff_SpawnWater0A,
    eff_SpawnWater0B,
    eff_DrawWater0,
    /* EFF_WATER_1 */
    eff_SpawnWater1A,
    eff_SpawnWater1B,
    eff_DrawWater1,
    /* EFF_MUZZLE_FLASH */
    eff_SpawnMuzzleFlash,
    eff_DrawMuzzleFlashes,
};

/* ---- shapes ---- */

/* The scale of the view the effects are drawn in: half height, as the
 * renderer's is. */
VECTOR g_eff_view_scale = {ONE, ONE / 2, ONE, 0};

/* The corners of a muzzle flash's three quads: one across the muzzle, and
 * two along the barrel's line that cross each other. */
/* clang-format off */
SVECTOR g_eff_muzzle_flash_vertices[3][4] = {
    {{-125, -125,   0, 0}, { 125, -125,   0, 0},
     {-125,  125,   0, 0}, { 125,  125,   0, 0}},
    {{-125,    0,   0, 0}, {-125,    0, 500, 0},
     { 125,    0,   0, 0}, { 125,    0, 500, 0}},
    {{   0, -125,   0, 0}, {   0, -125, 500, 0},
     {   0,  125,   0, 0}, {   0,  125, 500, 0}},
};
/* clang-format on */

/* The corners of a footprint. */
SVECTOR g_eff_footprint_vertex_0 = {-82, 0, -220, 0};
SVECTOR g_eff_footprint_vertex_1 = {82, 0, -220, 0};
SVECTOR g_eff_footprint_vertex_2 = {-82, 0, 78, 0};
SVECTOR g_eff_footprint_vertex_3 = {82, 0, 78, 0};
