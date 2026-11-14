/* What is loaded, and tables of the things that are loaded (resource.h):
 * the stage that is in memory and where fighters start on each stage, the
 * tiny models, the presets of the scrolling textures, and the weapon models
 * (which are loaded, which come along with which, where a worn one sits).
 *
 * The file has no code. It is built without small data, which keeps the
 * two words at its head here. */
#include "common.h"
#include "bb2.h"

/* ---- the stage ---- */

/* The stage that is loaded (-1: none), and whether it still has its areas. */
s16 g_resource_stage = -1;
s16 g_resource_stage_has_areas = 0;

/* Where fighters start, by stage: four pairs of places, a fighter's each
 * (fight_GetStartPosition), each x, y, z. Which pair a fight takes is the
 * mode's to say; the team battle's and chanbara's stages and the two of
 * practice have one pair four times, or nearly. */
/* clang-format off */
s16 g_fight_start_positions[STAGE_COUNT][FIGHT_START_PAIRS][2]
                           [FIGHT_START_POINT_SIZE] = {
    /* STAGE_MAIN_TATARA_SHRINE */
    {{{ -2000,     0,      0}, {  2000,     0,      0}},
     {{  2000,     0,   2000}, {  2000,     0,  -2000}},
     {{ -4600,     0,   5400}, { -7400,     0,   2600}},
     {{ -7400,     0,  -2600}, { -4600,     0,  -5400}}},
    /* STAGE_MEIKYOKAN_DOJO */
    {{{     0,     0,      0}, { -4000,     0,      0}},
     {{     0,     0,  -5000}, {  4000,     0,  -5000}},
     {{  4000,     0,   5000}, {     0,     0,   5000}},
     {{ 10000,     0,      0}, {  6000,     0,      0}}},
    /* STAGE_SHAINTO_HQ */
    {{{     0,     0,   2000}, {     0,     0,   6000}},
     {{     0,     0,  -6000}, {     0,     0,  -2000}},
     {{ -5000,     0,  -2000}, { -5000,     0,   2000}},
     {{  5000,     0,  -2000}, {  5000,     0,   2000}}},
    /* STAGE_TATARA_SHRINE */
    {{{ -2000,     0,  -2000}, { -2000,     0,   2000}},
     {{     0,     0,  -4000}, {  4000,     0,  -4000}},
     {{  6000,     0,  -2000}, {  6000,     0,   2000}},
     {{     0,     0,   4000}, {  4000,     0,   4000}}},
    /* STAGE_BUS_STOP */
    {{{  6400,     0,   -600}, {  3600,     0,  -3400}},
     {{     0,     0,  -8000}, { -4000,     0,  -8000}},
     {{ -1400,     0,   6600}, {  1400,     0,   9400}},
     {{  4600,     0,   7400}, {  7400,     0,   4600}}},
    /* STAGE_LOST_BRIDGE */
    {{{     0,     0,      0}, {     0,     0,  -4000}},
     {{     0,     0,   6000}, {     0,     0,   2000}},
     {{     0,     0,  -6000}, {     0,     0, -10000}},
     {{ -2000,     0, -12000}, {  2000,     0, -12000}}},
    /* STAGE_TATARA_SHRINE_PATH */
    {{{  5500,  2000,      0}, {  9500,  2000,      0}},
     {{ 16000,  2000,  -2000}, { 16000,  2000,  -6000}},
     {{ -8000,     0,   2000}, { -4000,     0,   2000}},
     {{-16600,     0,   4000}, {-12000,     0,   4000}}},
    /* STAGE_BAMBOO_THICKET */
    {{{     0,     0,   1000}, {  4000,     0,   1000}},
     {{ -3400,     0,  -2600}, {  -600,     0,  -5400}},
     {{ -2600,     0,   7400}, { -5400,     0,   4600}},
     {{ 10000,     0,      0}, { 10000,     0,   4000}}},
    /* STAGE_OUTSIDE_MEIKYOKAN */
    {{{ -4000,     0,  11000}, {     0,     0,  11000}},
     {{ -8000,     0,  10000}, { -8000,     0,   6000}},
     {{ -4000,     0,      0}, {     0,     0,      0}},
     {{ -1000,     0,  -6000}, { -1000,     0, -10000}}},
    /* STAGE_FUTURE_DUMP_SITE */
    {{{  2000,     0,      0}, {  6000,     0,      0}},
     {{ 16000,     0,  12000}, { 16000,     0,   8000}},
     {{  4000,     0, -12000}, {  8000,     0, -12000}},
     {{-10000,     0,  -2000}, {-10000,     0,  -6000}}},
    /* STAGE_SNAKE_CLIFF */
    {{{ -5000,     0,   2000}, { -5000,     0,  -2000}},
     {{  4000,  2000,   4000}, {  8000,  2000,   4000}},
     {{  5000,  2000,  -5000}, {  9000,  2000,  -5000}},
     {{ 14000,  2000,   2000}, { 14000,  2000,  -2000}}},
    /* STAGE_KIMIGAURA_COAST */
    {{{  7000,     0,   4000}, {  7000,     0,      0}},
     {{  8600,     0,   9400}, { 11400,     0,   6600}},
     {{ 12000,     0,   1000}, { 12000,     0,  -3000}},
     {{ 10400,     0,  -7600}, {  7600,     0, -10400}}},
    /* STAGE_ORANGE_TREE_FIELDS */
    {{{     0, -4000,  13000}, {     0, -4000,  17000}},
     {{ -6000, -2000,   8000}, { -6000, -2000,   4000}},
     {{  2000, -2000,   8000}, {  2000, -2000,   4000}},
     {{ -8000,     0,  -4000}, { -4000,     0,  -4000}}},
    /* STAGE_CASTLE_ROOFTOP */
    {{{     0,     0,      0}, {  4000,     0,      0}},
     {{-11000,     0,      0}, { -7000,     0,      0}},
     {{ -5500,     0,      0}, { -1500,     0,      0}},
     {{ 11000,     0,      0}, {  7000,     0,      0}}},
    /* STAGE_UNDER_LOST_BRIDGE */
    {{{  2000,     0,   3000}, {  2000,     0,  -1000}},
     {{     0,     0,   8000}, {  4000,     0,   8000}},
     {{ -6000,     0,   3000}, { -6000,     0,  -1000}},
     {{ -2000,     0,  -6000}, {  2000,     0,  -6000}}},
    /* STAGE_TERRACED_FIELDS */
    {{{  6500, -2000,  -8500}, {  2500, -2000,  -8500}},
     {{ -1000,     0,      0}, { -5000,     0,      0}},
     {{     0,  4000,  11000}, { -4000,  4000,  11000}},
     {{  5000,  2000,   6500}, {  1000,  2000,   6500}}},
    /* STAGE_UNDER_RAILWAY */
    {{{ 14000,     0,   8000}, { 14000,     0,  12000}},
     {{  4000,  2000,  -6000}, {  4000,  2000, -10000}},
     {{  4000,  2000,   5000}, {  4000,  2000,   9000}},
     {{ 15000,     0, -11000}, { 15000,     0,  -7000}}},
    /* STAGE_WATER_TOWER_DAM */
    {{{ -1400,     0,  -3400}, {  1400,     0,   -600}},
     {{ -4000,     0,   3000}, {     0,     0,   3000}},
     {{ -3400,     0,  -4600}, {  -600,     0,  -7400}},
     {{  4000,     0,  -4000}, {  4000,     0,  -8000}}},
    /* STAGE_LUMBERYARD */
    {{{ -1000,     0,  -2000}, { -1000,     0,  -6000}},
     {{ -1000,     0,  -9000}, { -1000,     0, -13000}},
     {{ -1000,     0,   4000}, { -1000,     0,      0}},
     {{ -1000,     0,  10000}, { -1000,     0,   6000}}},
    /* STAGE_LIGHTHOUSE */
    {{{  3400,     0,   7400}, {   600,     0,   4600}},
     {{ -4000,     0,  -2000}, { -8000,     0,  -2000}},
     {{ -8000, -2000, -12000}, { -4000, -2000, -12000}},
     {{  4000, -2000, -10000}, {     0, -2000, -10000}}},
    /* STAGE_KABUKI_STAGE */
    {{{     0,     0,  -3000}, { -4000,     0,  -3000}},
     {{ -8000,     0,  -2000}, { -8000,     0,   2000}},
     {{ -8000,     0, -12000}, { -8000,     0,  -8000}},
     {{ -3000,     0,   3000}, {  1000,     0,   3000}}},
    /* STAGE_ABANDONED_SHIP */
    {{{  2000,     0,   3000}, { -2000,     0,   3000}},
     {{ -2000,     0,  -3000}, {  2000,     0,  -3000}},
     {{ -3000,     0,  -2000}, { -3000,     0,   2000}},
     {{  3000,     0,   2000}, {  3000,     0,  -2000}}},
    /* STAGE_FERRY_DOCK */
    {{{  2000,     0,      0}, {  2000,     0,  -4000}},
     {{ -4000,     0,  -8000}, {     0,     0,  -8000}},
     {{     0,  -500,  10000}, {  4000,  -500,  10000}},
     {{ -4000,     0,   4000}, {     0,     0,   4000}}},
    /* STAGE_SHAINTO_GATE */
    {{{  6000,     0,   4000}, { 10000,     0,   4000}},
     {{-12000,     0,   4000}, { -8000,     0,   4000}},
     {{  6000,     0,  -4000}, { 10000,     0,  -4000}},
     {{-12000,     0,  -4000}, { -8000,     0,  -4000}}},
    /* STAGE_PURPLE_CAVERN */
    {{{ -3000,  2000,   2000}, { -7000,  2000,   2000}},
     {{  5000,  2000,   1000}, {  1000,  2000,   1000}},
     {{ -7000,     0,  -6000}, { -3000,     0,  -6000}},
     {{  1000,     0,  -5000}, {  5000,     0,  -5000}}},
    /* STAGE_ABANDONED_STATION */
    {{{ -3000,     0,  -2000}, { -3000,     0,  -6000}},
     {{  6000, -2000,  -8000}, {  6000, -2000,  -4000}},
     {{  6000, -2000,  18000}, {  6000, -2000,  14000}},
     {{ -6000,     0,  14000}, { -6000,     0,  10000}}},
    /* STAGE_RAILWAY */
    {{{ -2000,     0,      0}, {  2000,     0,      0}},
     {{-16000,  -100,  -6000}, {-16000,  -100, -10000}},
     {{-16000,  -100,  10000}, {-16000,  -100,   6000}},
     {{ 14000,     0,      0}, { 10000,     0,      0}}},
    /* STAGE_ABANDONED_TUNNEL */
    {{{  2000,     0,      0}, {  6000,     0,      0}},
     {{-13000,     0,      0}, { -9000,     0,      0}},
     {{ -6000,     0,  -2000}, { -2000,     0,  -2000}},
     {{  7600,     0,    400}, { 10400,     0,  -2400}}},
    /* STAGE_ABANDONED_SHIPYARD */
    {{{  1000,     0,  -5000}, {  1000,     0,  -1000}},
     {{  2000,     0,  -6500}, {  -500,     0,  -9500}},
     {{ -1500,     0,   9500}, {  1500,     0,   6500}},
     {{     0,     0,  14000}, {     0,     0,  10000}}},
    /* STAGE_BUILDING_ROOFTOP */
    {{{     0,     0,   2000}, {     0,     0,  -2000}},
     {{ -5400,     0,   7400}, { -2600,     0,   4600}},
     {{ -7400,     0,  -7400}, { -4600,     0,  -4600}},
     {{  5400,     0,  -7400}, {  2600,     0,  -4600}}},
    /* STAGE_BUILDING_PARKING_LOT */
    {{{ -5000,     0,  11000}, { -5000,     0,   7000}},
     {{ -5000,     0,  -7000}, { -5000,     0, -11000}},
     {{  2000,     0,  11000}, {  2000,     0,   7000}},
     {{  2000,     0,  -7000}, {  2000,     0, -11000}}},
    /* STAGE_SHAINTO_CEMETERY */
    {{{  2000,     0,  -1000}, {  2000,     0,   3000}},
     {{ -8000,     0,   3000}, {-12000,     0,   3000}},
     {{ 15400,     0,   1400}, { 12600,     0,  -1400}},
     {{ -7400,     0,  -5400}, { -4600,     0,  -2600}}},
    /* STAGE_TEAM_1 */
    {{{ -3000,     0,      0}, {  1000,     0,      0}},
     {{ -3000,     0,      0}, {  1000,     0,      0}},
     {{ -3000,     0,      0}, {  1000,     0,      0}},
     {{ -3000,     0,      0}, {  1000,     0,      0}}},
    /* STAGE_TEAM_2 */
    {{{ -2300,     0,      0}, {  2300,     0,      0}},
     {{ -2300,     0,      0}, {  2300,     0,      0}},
     {{ -2300,     0,      0}, {  2300,     0,      0}},
     {{ -2300,     0,      0}, {  2300,     0,      0}}},
    /* STAGE_CHANBARA */
    {{{ -2000,     0,      0}, {  2000,     0,      0}},
     {{ -2000,     0,      0}, {  2000,     0,      0}},
     {{ -2000,     0,      0}, {  2000,     0,      0}},
     {{     0,     0,   2000}, {     0,     0,  -2000}}},
    /* STAGE_PRACTICE_1 */
    {{{  2000,     0,      0}, { -2000,     0,      0}},
     {{ -2000,     0,   6000}, {  2000,     0,   6000}},
     {{ -7400,     0,  -7400}, { -4600,     0,  -4600}},
     {{  4600,     0,  -4600}, {  7400,     0,  -7400}}},
    /* STAGE_PRACTICE_2 */
    {{{ -2000,     0,      0}, {  2000,     0,      0}},
     {{ -2000,     0,   8000}, {  2000,     0,   8000}},
     {{-11400,     0, -11400}, { -8600,     0,  -8600}},
     {{  8600,     0,  -8600}, { 11400,     0, -11400}}},
    /* stage 37 */
    {{{ -8000,     0, -24000}, { 18000,     0,  24000}},
     {{-20000,     0,  24000}, { 18000,     0, -26000}},
     {{ 18000,     0,  24000}, {  8000,     0, -24000}},
     {{ 18000,     0, -26000}, {-20000,     0,  24000}}},
};
/* clang-format on */

/* 48 bytes after the start positions that nothing reads: six halfwords and
 * three places, by their values. What they were for is not known. */
struct {
    s16 values[6];
    s32 pos[3][3];
} g_resource_unused_places = {
    {800, 256, 128, 128, 128, 480},
    {{-9300, -6000, 8000}, {500, -6000, 8000}, {10700, -6000, 8000}}};

/* ---- the tiny model (model_tiny.c) ---- */

/* The tiny model of each model, by model id, which for the first 31 is the
 * roster entry's number (g_fighter_roster_models): the twenty characters
 * with a name have one, in both dresses, and the Narukagami's crest. */
#define NONE MODEL_TINY_NONE
/* clang-format off */
u8 g_model_tiny_ids[70] = {
    21,   /* ROSTER_KANNUKI_STORY */
    22,   /* ROSTER_TATSUMI_STORY */
    23,   /* ROSTER_MIKADO_STORY */
    0,    /* ROSTER_KANNUKI */
    1,    /* ROSTER_TATSUMI */
    2,    /* ROSTER_MIKADO */
    3,    /* ROSTER_UTSUSEMI */
    4,    /* ROSTER_RED_SHADOW */
    5,    /* ROSTER_TSUBAME */
    6,    /* ROSTER_SAZANKA */
    7,    /* ROSTER_SUMINAGASHI */
    8,    /* ROSTER_NIGHTSTALKER */
    9,    /* ROSTER_MATSUMUSHI */
    NONE, /* ROSTER_VANISHER */
    NONE, /* ROSTER_SIDEARM_1 */
    24,   /* ROSTER_GENGORO_STORY */
    25,   /* ROSTER_KAUN_STORY */
    26,   /* ROSTER_JO_STORY */
    10,   /* ROSTER_GENGORO */
    11,   /* ROSTER_KAUN */
    12,   /* ROSTER_JO */
    13,   /* ROSTER_HONGOU */
    14,   /* ROSTER_KATZE */
    15,   /* ROSTER_HIGHWAYMAN */
    16,   /* ROSTER_CHIHIRO */
    17,   /* ROSTER_UTAMARU */
    18,   /* ROSTER_TONY_UMEDA */
    19,   /* ROSTER_ISOHACHI */
    NONE, /* ROSTER_ARMORED */
    NONE, /* ROSTER_SIDEARM_2 */
    20,   /* ROSTER_LAST_1 */
    /* the other models: chanbara's opponents, the victim and the
     * Shainto's crest, and the places no model has */
    NONE, NONE, NONE, NONE, NONE, NONE, NONE, NONE, NONE, NONE, NONE, NONE,
    NONE, NONE, NONE, NONE, NONE, NONE, NONE, NONE, NONE, NONE, NONE, NONE,
    NONE, NONE, NONE, NONE, NONE, NONE, NONE, NONE, NONE, NONE, NONE, NONE,
    NONE, NONE, NONE,
};
/* clang-format on */
#undef NONE

/* The bone each part of the tiny model follows, ended by -1. */
s16 g_model_tiny_bones[16] = {
    1, 2, 4, 5, 6, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, -1};

/* ---- scrolling textures (tex_scroll.c) ---- */

/* The presets tex_InitScroll sets a scroll up from, by enum
 * TexScrollControl: where in video memory the texture is copied from (x,
 * y), a strip's width and the texture's height, where it is copied to (x,
 * y), and the lines it moves a frame. */
/* clang-format off */
u16 g_tex_scroll_presets[2][TEX_SCROLL_CONTROLS] = {
    {792, 384,  8, 32, 744, 256, 1}, /* STAGE_LOST_BRIDGE */
    {752, 320, 16, 64, 672, 384, 1}, /* STAGE_UNDER_LOST_BRIDGE */
};
/* clang-format on */

/* ---- weapon models (resource_weapon.c) ---- */

/* For each weapon model, its place among the loaded ones as of the last
 * load, or WEAPON_SLOT_NONE; WEAPON_SLOTS_END ends it. None is loaded to
 * begin with. */
#define NONE WEAPON_SLOT_NONE
/* clang-format off */
s16 g_resource_loaded_weapons[WEAPON_MODEL_COUNT + 1] = {
    NONE, NONE, NONE, NONE, NONE, NONE, NONE, NONE, NONE, NONE,
    NONE, NONE, NONE, NONE, NONE, NONE, NONE, NONE, NONE, NONE,
    NONE, NONE, NONE, NONE, NONE, NONE, NONE, NONE, NONE, NONE,
    NONE, NONE, NONE, NONE, NONE, NONE, NONE, NONE, NONE, NONE,
    NONE, NONE, NONE, NONE, NONE, NONE, NONE, NONE, NONE, NONE,
    NONE, NONE, NONE, NONE, NONE, NONE, NONE, NONE,
    WEAPON_SLOTS_END,
};
/* clang-format on */
#undef NONE

/* Up to two more models that are loaded along with a weapon model, or
 * WEAPON_LINKED_NONE: what weapon_QueueWorn draws at the waist (a
 * reading: scabbards). The katana, the long sword and the five second
 * swords have a pair each; the practice katana and long sword have the
 * practice second sword, which has itself. */
#define NONE WEAPON_LINKED_NONE
/* clang-format off */
u8 g_resource_weapon_linked[WEAPON_MODEL_COUNT][2] = {
    /* the weapons, a model a clan: the katana's two, the long sword's, */
    {28, 30},
    {29, 31},
    {32, 34},
    {33, 35},
    /* the nodachi's, the broadsword's and the pole arms' */
    {NONE, NONE}, {NONE, NONE}, {NONE, NONE}, {NONE, NONE}, {NONE, NONE},
    {NONE, NONE},
    /* 10: WEAPON_MODEL_SUB_WEAPONS: a model a kind of sub-weapon */
    {NONE, NONE},
    {36, 37}, /* 11: TATSUMI's second sword */
    {NONE, NONE},
    {38, 39}, /* 13: UTSUSEMI's */
    {42, 43}, /* 14: RED SHADOW's */
    {NONE, NONE}, {NONE, NONE},
    {44, 45}, /* 17: MATSUMUSHI's */
    {NONE, NONE}, {NONE, NONE}, {NONE, NONE}, {NONE, NONE},
    {40, 41}, /* 22: HIGHWAYMAN's */
    {NONE, NONE}, {NONE, NONE}, {NONE, NONE}, {NONE, NONE},
    /* 27: the models that come along with another */
    {NONE, NONE}, {NONE, NONE}, {NONE, NONE}, {NONE, NONE}, {NONE, NONE},
    {NONE, NONE}, {NONE, NONE}, {NONE, NONE}, {NONE, NONE}, {NONE, NONE},
    {NONE, NONE}, {NONE, NONE}, {NONE, NONE}, {NONE, NONE}, {NONE, NONE},
    {NONE, NONE}, {NONE, NONE}, {NONE, NONE}, {NONE, NONE},
    /* 46: WEAPON_MODEL_GUNS */
    {NONE, NONE}, {NONE, NONE},
    /* 48: WEAPON_MODEL_OFF_HAND_GUNS */
    {NONE, NONE}, {NONE, NONE},
    /* 50: WEAPON_MODEL_STORY */
    {NONE, NONE},
    /* 51: WEAPON_MODEL_PRACTICE */
    {WEAPON_MODEL_PRACTICE_SECOND, WEAPON_MODEL_PRACTICE_SECOND},
    {WEAPON_MODEL_PRACTICE_SECOND, WEAPON_MODEL_PRACTICE_SECOND},
    /* 53: the rest of the practice weapons */
    {NONE, NONE}, {NONE, NONE}, {NONE, NONE}, {NONE, NONE},
    /* 57: WEAPON_MODEL_PRACTICE_SECOND */
    {WEAPON_MODEL_PRACTICE_SECOND, WEAPON_MODEL_PRACTICE_SECOND},
};
/* clang-format on */
#undef NONE

/* Where a worn weapon sits on the waist, by side: a position (x, y, z, for
 * a model of full size) and a rotation (angles about x, y and z). */
/* clang-format off */
s16 g_weapon_worn_poses[2][WORN_POSE_SIZE] = {
    {95, 40, 189,   0x96A, 0x35C, 0xC00},
    {95, 38, 149,   0x0FC, 0x419, 0x255},
};
/* clang-format on */
