/* The fight's tables: what the characters, weapons, loose objects, modes and
 * stages are given. Each is declared in its subsystem's header, where the
 * comments say more: the roster, the body's points, the weapons' points and
 * the characters' speeds, powers, stances and jumps (fighter.h, motion.h,
 * weapon.h), the kinds of loose object (object.h), the opponents of a run
 * against the CPU and of chanbara, the endings' voices and the kaishaku's
 * timing (fight.h), the stages' music and ambient sounds (snd.h), and at the
 * head a table of square roots (math.h).
 *
 * The file has no code. It is built without small data, which keeps its
 * small tables here among the others. */
#include "common.h"
#include "bb2.h"

/* The square root of i, times 8 and rounded down, for i below
 * MATH_SQRT_TABLE_SIZE: a quick root for the distances of the camera and of
 * the hit tests. Entry i is the whole part of the root of 64 * i. */
/* clang-format off */
u8 g_math_sqrt8_table[MATH_SQRT_TABLE_SIZE] = {
      0,   8,  11,  13,  16,  17,  19,  21,  22,  24,  25,  26,  27,  28,  29,
     30,  32,  32,  33,  34,  35,  36,  37,  38,  39,  40,  40,  41,  42,  43,
     43,  44,  45,  45,  46,  47,  48,  48,  49,  49,  50,  51,  51,  52,  53,
     53,  54,  54,  55,  56,  56,  57,  57,  58,  58,  59,  59,  60,  60,  61,
     61,  62,  62,  63,  64,  64,  64,  65,  65,  66,  66,  67,  67,  68,  68,
     69,  69,  70,  70,  71,  71,  72,  72,  72,  73,  73,  74,  74,  75,  75,
     75,  76,  76,  77,  77,  77,  78,  78,  79,  79,  80,  80,  80,  81,  81,
     81,  82,  82,  83,  83,  83,  84,  84,  85,  85,  85,  86,  86,  86,  87,
     87,  88,  88,  88,  89,  89,  89,  90,  90,  90,  91,  91,  91,  92,  92,
     92,  93,  93,  93,  94,  94,  94,  95,  95,  96,  96,  96,  96,  97,  97,
     97,  98,  98,  98,  99,  99,  99, 100, 100, 100, 101, 101, 101, 102, 102,
    102, 103, 103, 103, 104, 104, 104, 104, 105, 105, 105, 106, 106, 106, 107,
    107, 107, 107, 108, 108, 108, 109, 109, 109, 109, 110, 110, 110, 111, 111,
    111, 112, 112, 112, 112, 113, 113, 113, 113, 114, 114, 114, 115, 115, 115,
    115, 116, 116, 116, 117, 117, 117, 117, 118, 118, 118, 118, 119, 119, 119,
    120, 120, 120, 120, 121, 121, 121, 121, 122, 122, 122, 122, 123, 123, 123,
    123, 124, 124, 124, 124, 125, 125, 125, 125, 126, 126, 126, 126, 127, 127,
    127, 128, 128, 128, 128, 128, 129, 129, 129, 129, 130, 130, 130, 130, 131,
    131, 131, 131, 132, 132, 132, 132, 133, 133, 133, 133, 134, 134, 134, 134,
    135, 135, 135, 135, 136, 136, 136, 136, 136, 137, 137, 137, 137, 138, 138,
    138, 138, 139, 139, 139, 139, 139, 140, 140, 140, 140, 141, 141, 141, 141,
    141, 142, 142, 142, 142, 143, 143, 143, 143, 144, 144, 144, 144, 144, 145,
    145, 145, 145, 145, 146, 146, 146, 146, 147, 147, 147, 147, 147, 148, 148,
    148, 148, 149, 149, 149, 149, 149, 150, 150, 150, 150, 150, 151, 151, 151,
    151, 152, 152, 152, 152, 152, 153, 153, 153, 153, 153, 154, 154, 154, 154,
    154, 155, 155, 155, 155, 155, 156, 156, 156, 156, 156, 157, 157, 157, 157,
    157, 158, 158, 158, 158, 158, 159, 159, 159, 159, 160, 160, 160, 160, 160,
    160, 161, 161, 161, 161, 161, 162, 162, 162, 162, 162, 163, 163, 163, 163,
    163, 164, 164, 164, 164, 164, 165, 165, 165, 165, 165, 166, 166, 166, 166,
    166, 167, 167, 167, 167, 167, 168, 168, 168, 168, 168, 168, 169, 169, 169,
    169, 169, 170, 170, 170, 170, 170, 171, 171, 171, 171, 171, 171, 172, 172,
    172, 172, 172, 173, 173, 173, 173, 173, 173, 174, 174, 174, 174, 174, 175,
    175, 175, 175, 175, 176, 176, 176, 176, 176, 176, 177, 177, 177, 177, 177,
    177, 178, 178, 178, 178, 178, 179, 179, 179, 179, 179, 179, 180, 180, 180,
    180, 180, 181, 181, 181, 181, 181, 181, 182, 182, 182, 182, 182, 182, 183,
    183, 183, 183, 183, 184, 184, 184, 184, 184, 184, 185, 185, 185, 185, 185,
    185, 186, 186, 186, 186, 186, 186, 187, 187, 187, 187, 187, 187, 188, 188,
    188, 188, 188, 188, 189, 189, 189, 189, 189, 189, 190, 190, 190, 190, 190,
    190, 191, 191, 191, 191, 191, 192, 192, 192, 192, 192, 192, 192, 193, 193,
    193, 193, 193, 193, 194, 194, 194, 194, 194, 194, 195, 195, 195, 195, 195,
    195, 196, 196, 196, 196, 196, 196, 197, 197, 197, 197, 197, 197, 198, 198,
    198, 198, 198, 198, 199, 199, 199, 199, 199, 199, 200, 200, 200, 200, 200,
    200, 200, 201, 201, 201, 201, 201, 201, 202, 202, 202, 202, 202, 202, 203,
    203, 203, 203, 203, 203, 203, 204, 204, 204, 204, 204, 204, 205, 205, 205,
    205, 205, 205, 205, 206, 206, 206, 206, 206, 206, 207, 207, 207, 207, 207,
    207, 208, 208, 208, 208, 208, 208, 208, 209, 209, 209, 209, 209, 209, 209,
    210, 210, 210, 210, 210, 210, 211, 211, 211, 211, 211, 211, 211, 212, 212,
    212, 212, 212, 212, 213, 213, 213, 213, 213, 213, 213, 214, 214, 214, 214,
    214, 214, 214, 215, 215, 215, 215, 215, 215, 216, 216, 216, 216, 216, 216,
    216, 217, 217, 217, 217, 217, 217, 217, 218, 218, 218, 218, 218, 218, 218,
    219, 219, 219, 219, 219, 219, 219, 220, 220, 220, 220, 220, 220, 220, 221,
    221, 221, 221, 221, 221, 221, 222, 222, 222, 222, 222, 222, 222, 223, 223,
    223, 223, 223, 223, 224, 224, 224, 224, 224, 224, 224, 224, 225, 225, 225,
    225, 225, 225, 225, 226, 226, 226, 226, 226, 226, 226, 227, 227, 227, 227,
    227, 227, 227, 228, 228, 228, 228, 228, 228, 228, 229, 229, 229, 229, 229,
    229, 229, 230, 230, 230, 230, 230, 230, 230, 231, 231, 231, 231, 231, 231,
    231, 232, 232, 232, 232, 232, 232, 232, 232, 233, 233, 233, 233, 233, 233,
    233, 234, 234, 234, 234, 234, 234, 234, 235, 235, 235, 235, 235, 235, 235,
    235, 236, 236, 236, 236, 236, 236, 236, 237, 237, 237, 237, 237, 237, 237,
    237, 238, 238, 238, 238, 238, 238, 238, 239, 239, 239, 239, 239, 239, 239,
    240, 240, 240, 240, 240, 240, 240, 240, 241, 241, 241, 241, 241, 241, 241,
    241, 242, 242, 242, 242, 242, 242, 242, 243, 243, 243, 243, 243, 243, 243,
    243, 244, 244, 244, 244, 244, 244, 244, 245, 245, 245, 245, 245, 245, 245,
    245, 246, 246, 246, 246, 246, 246, 246, 246, 247, 247, 247, 247, 247, 247,
    247, 248, 248, 248, 248, 248, 248, 248, 248, 249, 249, 249, 249, 249, 249,
    249, 249, 250, 250, 250, 250, 250, 250, 250, 250, 251, 251, 251, 251, 251,
    251, 251, 251, 252, 252, 252, 252, 252, 252, 252, 252, 253, 253, 253, 253,
    253, 253, 253, 253, 254, 254, 254, 254, 254, 254, 254, 254, 255, 255, 255,
    255, 255, 255, 255,
};
/* clang-format on */

/* ---- music ---- */

/* The music track of a versus fight, by stage (one of the first five of
 * cdrom.c's tracks). */
/* clang-format off */
u8 g_snd_stage_music_tracks[32] = {
    0, /* STAGE_MAIN_TATARA_SHRINE */
    4, /* STAGE_MEIKYOKAN_DOJO */
    0, /* STAGE_SHAINTO_HQ */
    0, /* STAGE_TATARA_SHRINE */
    0, /* STAGE_BUS_STOP */
    0, /* STAGE_LOST_BRIDGE */
    0, /* STAGE_TATARA_SHRINE_PATH */
    3, /* STAGE_BAMBOO_THICKET */
    0, /* STAGE_OUTSIDE_MEIKYOKAN */
    3, /* STAGE_FUTURE_DUMP_SITE */
    2, /* STAGE_SNAKE_CLIFF */
    2, /* STAGE_KIMIGAURA_COAST */
    0, /* STAGE_ORANGE_TREE_FIELDS */
    4, /* STAGE_CASTLE_ROOFTOP */
    0, /* STAGE_UNDER_LOST_BRIDGE */
    4, /* STAGE_TERRACED_FIELDS */
    4, /* STAGE_UNDER_RAILWAY */
    0, /* STAGE_WATER_TOWER_DAM */
    2, /* STAGE_LUMBERYARD */
    0, /* STAGE_LIGHTHOUSE */
    0, /* STAGE_KABUKI_STAGE */
    0, /* STAGE_ABANDONED_SHIP */
    2, /* STAGE_FERRY_DOCK */
    2, /* STAGE_SHAINTO_GATE */
    3, /* STAGE_PURPLE_CAVERN */
    3, /* STAGE_ABANDONED_STATION */
    4, /* STAGE_RAILWAY */
    4, /* STAGE_ABANDONED_TUNNEL */
    0, /* STAGE_ABANDONED_SHIPYARD */
    3, /* STAGE_BUILDING_ROOFTOP */
    3, /* STAGE_BUILDING_PARKING_LOT */
    2, /* STAGE_SHAINTO_CEMETERY */
};
/* clang-format on */

/* ---- the roster ---- */

/* Each roster entry's character (the last three entries are spare). */
/* clang-format off */
u8 g_fighter_roster_characters[36] = {
    CHARACTER_KANNUKI,      /* ROSTER_KANNUKI_STORY */
    CHARACTER_TATSUMI,      /* ROSTER_TATSUMI_STORY */
    CHARACTER_MIKADO,       /* ROSTER_MIKADO_STORY */
    CHARACTER_KANNUKI,      /* ROSTER_KANNUKI */
    CHARACTER_TATSUMI,      /* ROSTER_TATSUMI */
    CHARACTER_MIKADO,       /* ROSTER_MIKADO */
    CHARACTER_UTSUSEMI,     /* ROSTER_UTSUSEMI */
    CHARACTER_RED_SHADOW,   /* ROSTER_RED_SHADOW */
    CHARACTER_TSUBAME,      /* ROSTER_TSUBAME */
    CHARACTER_SAZANKA,      /* ROSTER_SAZANKA */
    CHARACTER_SUMINAGASHI,  /* ROSTER_SUMINAGASHI */
    CHARACTER_NIGHTSTALKER, /* ROSTER_NIGHTSTALKER */
    CHARACTER_MATSUMUSHI,   /* ROSTER_MATSUMUSHI */
    CHARACTER_VANISHER,     /* ROSTER_VANISHER */
    CHARACTER_SIDEARM_1,    /* ROSTER_SIDEARM_1 */
    CHARACTER_GENGORO,      /* ROSTER_GENGORO_STORY */
    CHARACTER_KAUN,         /* ROSTER_KAUN_STORY */
    CHARACTER_JO,           /* ROSTER_JO_STORY */
    CHARACTER_GENGORO,      /* ROSTER_GENGORO */
    CHARACTER_KAUN,         /* ROSTER_KAUN */
    CHARACTER_JO,           /* ROSTER_JO */
    CHARACTER_HONGOU,       /* ROSTER_HONGOU */
    CHARACTER_KATZE,        /* ROSTER_KATZE */
    CHARACTER_HIGHWAYMAN,   /* ROSTER_HIGHWAYMAN */
    CHARACTER_CHIHIRO,      /* ROSTER_CHIHIRO */
    CHARACTER_UTAMARU,      /* ROSTER_UTAMARU */
    CHARACTER_TONY_UMEDA,   /* ROSTER_TONY_UMEDA */
    CHARACTER_ISOHACHI,     /* ROSTER_ISOHACHI */
    CHARACTER_ARMORED,      /* ROSTER_ARMORED */
    CHARACTER_SIDEARM_2,    /* ROSTER_SIDEARM_2 */
    CHARACTER_LAST_1,       /* ROSTER_LAST_1 */
    CHARACTER_VICTIM,       /* ROSTER_VICTIM */
    CHARACTER_LAST_2,       /* ROSTER_LAST_2 */
    0, 0, 0, /* spare */
};
/* clang-format on */

/* Each character's roster entry: of a character with two, the one that
 * is not story mode's. */
/* clang-format off */
u8 g_fighter_character_rosters[CHARACTER_COUNT + 1] = {
    ROSTER_KANNUKI,      /* CHARACTER_KANNUKI */
    ROSTER_TATSUMI,      /* CHARACTER_TATSUMI */
    ROSTER_MIKADO,       /* CHARACTER_MIKADO */
    ROSTER_UTSUSEMI,     /* CHARACTER_UTSUSEMI */
    ROSTER_RED_SHADOW,   /* CHARACTER_RED_SHADOW */
    ROSTER_TSUBAME,      /* CHARACTER_TSUBAME */
    ROSTER_SAZANKA,      /* CHARACTER_SAZANKA */
    ROSTER_SUMINAGASHI,  /* CHARACTER_SUMINAGASHI */
    ROSTER_NIGHTSTALKER, /* CHARACTER_NIGHTSTALKER */
    ROSTER_MATSUMUSHI,   /* CHARACTER_MATSUMUSHI */
    ROSTER_VANISHER,     /* CHARACTER_VANISHER */
    ROSTER_SIDEARM_1,    /* CHARACTER_SIDEARM_1 */
    ROSTER_GENGORO,      /* CHARACTER_GENGORO */
    ROSTER_KAUN,         /* CHARACTER_KAUN */
    ROSTER_JO,           /* CHARACTER_JO */
    ROSTER_HONGOU,       /* CHARACTER_HONGOU */
    ROSTER_KATZE,        /* CHARACTER_KATZE */
    ROSTER_HIGHWAYMAN,   /* CHARACTER_HIGHWAYMAN */
    ROSTER_CHIHIRO,      /* CHARACTER_CHIHIRO */
    ROSTER_UTAMARU,      /* CHARACTER_UTAMARU */
    ROSTER_TONY_UMEDA,   /* CHARACTER_TONY_UMEDA */
    ROSTER_ISOHACHI,     /* CHARACTER_ISOHACHI */
    ROSTER_ARMORED,      /* CHARACTER_ARMORED */
    ROSTER_SIDEARM_2,    /* CHARACTER_SIDEARM_2 */
    ROSTER_LAST_1,       /* CHARACTER_LAST_1 */
    ROSTER_VICTIM,       /* CHARACTER_VICTIM */
    ROSTER_LAST_2,       /* CHARACTER_LAST_2 */
    0, /* spare */
};
/* clang-format on */

/* Each roster entry's model, which also picks its scale: the entry's own
 * number but for the victim and the Shainto's crest, whose models are the
 * last two (the four before those are chanbara's opponents). */
/* clang-format off */
u8 g_fighter_roster_models[36] = {
    0,  /* ROSTER_KANNUKI_STORY */
    1,  /* ROSTER_TATSUMI_STORY */
    2,  /* ROSTER_MIKADO_STORY */
    3,  /* ROSTER_KANNUKI */
    4,  /* ROSTER_TATSUMI */
    5,  /* ROSTER_MIKADO */
    6,  /* ROSTER_UTSUSEMI */
    7,  /* ROSTER_RED_SHADOW */
    8,  /* ROSTER_TSUBAME */
    9,  /* ROSTER_SAZANKA */
    10, /* ROSTER_SUMINAGASHI */
    11, /* ROSTER_NIGHTSTALKER */
    12, /* ROSTER_MATSUMUSHI */
    13, /* ROSTER_VANISHER */
    14, /* ROSTER_SIDEARM_1 */
    15, /* ROSTER_GENGORO_STORY */
    16, /* ROSTER_KAUN_STORY */
    17, /* ROSTER_JO_STORY */
    18, /* ROSTER_GENGORO */
    19, /* ROSTER_KAUN */
    20, /* ROSTER_JO */
    21, /* ROSTER_HONGOU */
    22, /* ROSTER_KATZE */
    23, /* ROSTER_HIGHWAYMAN */
    24, /* ROSTER_CHIHIRO */
    25, /* ROSTER_UTAMARU */
    26, /* ROSTER_TONY_UMEDA */
    27, /* ROSTER_ISOHACHI */
    28, /* ROSTER_ARMORED */
    29, /* ROSTER_SIDEARM_2 */
    30, /* ROSTER_LAST_1 */
    62, /* ROSTER_VICTIM */
    63, /* ROSTER_LAST_2 */
    0, 0, 0, /* spare */
};
/* clang-format on */

/* ---- a fighter's body and weapon ---- */

/* The points of a body that can be hit, before fighter_ScaleBodyPoints
 * scales them to the fighter's model: the bone a point is on, where it is
 * from the bone (x and z: y is 0 for all), its radius, and for the head,
 * the chest and the hips the radius of the core. The table holds the
 * squares of the radii as well. */
#define POINT(bone, x, z, radius)                                              \
    {0, bone, {x, 0, z, 0}, radius, (radius) * (radius), 0, 0}
#define CORED(bone, x, z, radius, core)                                        \
    {1, bone, {x, 0, z, 0}, radius, (radius) * (radius), core, (core) * (core)}
/* clang-format off */
BodyPoint g_fighter_body_point_table[BODY_POINT_COUNT] = {
    CORED( 2,  56,  -57,  228, 200), /* BODY_HEAD */
    CORED( 1,  94,    0,  210, 190), /* BODY_CHEST */
    CORED( 1, 279, -120,  210, 190),
    CORED( 1, 279,  120,  210, 190),
    CORED(11, 133,  -82,  174, 161), /* BODY_HIP */
    CORED(11, 133,   82,  174, 161),
    POINT( 8,  41,    0,  90),       /* BODY_OFF_ARM */
    POINT( 8, 180,    0,  90),
    POINT( 9,  45,    0,  65),
    POINT( 9, 144,    0,  65),
    POINT( 4,  41,    0,  90),       /* BODY_SWORD_ARM */
    POINT( 4, 180,    0,  90),
    POINT( 5,  45,    0,  65),
    POINT( 5, 144,    0,  65),
    POINT(12, 120,    0, 130),       /* BODY_LEGS */
    POINT(12, 291,    0, 130),
    POINT(13, 103,    0, 130),
    POINT(13, 287,    0, 130),
    POINT(15, 120,    0, 130),       /* BODY_SECOND_LEG */
    POINT(15, 291,    0, 130),
    POINT(16, 103,    0, 130),
    POINT(16, 287,    0, 130),
};
/* clang-format on */
#undef POINT
#undef CORED

/* Where the points of a weapon are from the hand that holds it
 * (fighter_PlacePoints): two for each weapon, three for the pole arms; then
 * the same for the sub-weapons that are followed while they are live. */
static SVECTOR g_fighter_katana_weapon_points[2] = {
    {0, -60, 950, 0}, {0, 0, 100, 0}};

static SVECTOR g_fighter_long_sword_weapon_points[2] = {
    {0, 0, 919, 0}, {0, 0, 103, 0}};

SVECTOR g_fighter_story_weapon_points[2] = {
    {0, -111, 1244, 0}, {0, -13, 102, 0}};

static SVECTOR g_fighter_broadsword_weapon_points[2] = {
    {0, 0, 1200, 0}, {0, 0, 100, 0}};

static SVECTOR g_fighter_naginata_weapon_points[3] = {
    {0, -46, 1502, 0}, {0, 0, 925, 0}, {0, 0, -840, 0}};

static SVECTOR g_fighter_yari_weapon_points[3] = {
    {0, 0, 1930, 0}, {0, 0, 1503, 0}, {0, 0, -318, 0}};

static SVECTOR g_fighter_long_gun_weapon_points[2] = {
    {0, -96, 864, 0}, {0, -96, 0, 0}};

static SVECTOR g_fighter_short_gun_weapon_points[2] = {
    {0, -144, 346, 0}, {0, -144, 0, 0}};

static SVECTOR g_fighter_sub_weapon_1_points[2] = {
    {0, -50, 969, 0}, {0, 0, 100, 0}};

static SVECTOR g_fighter_sub_weapon_3_points[2] = {
    {0, -50, 869, 0}, {0, 0, 100, 0}};

static SVECTOR g_fighter_sub_weapon_4_points[2] = {
    {0, -50, 869, 0}, {0, 0, 100, 0}};

static SVECTOR g_fighter_sub_weapon_7_points[2] = {
    {0, 0, 700, 0}, {0, 0, 100, 0}};

static SVECTOR g_fighter_sub_weapon_12_points[2] = {
    {0, 0, 1000, 0}, {0, 0, 150, 0}};

static SVECTOR g_fighter_sub_weapon_17_points[2] = {
    {0, -50, 969, 0}, {0, 0, 100, 0}};

static SVECTOR g_fighter_sub_weapon_30_points[2] = {
    {0, 0, 224, 0}, {0, 0, -150, 0}};

static SVECTOR g_fighter_sub_weapon_31_points[2] = {
    {0, -111, 289, 0}, {0, -111, 0, 0}};

/* How many points each set of g_fighter_weapon_point_sets has, and the
 * sets, by weapon (enum WeaponId): the nodachi's place has the points of
 * the weapon of the story's scenes. */
u8 g_fighter_weapon_point_counts[WEAPON_ID_COUNT] = {2, 2, 2, 2, 3, 3, 2, 2};

SVECTOR *g_fighter_weapon_point_sets[WEAPON_ID_COUNT] = {
    g_fighter_katana_weapon_points,     /* WEAPON_ID_KATANA */
    g_fighter_long_sword_weapon_points, /* WEAPON_ID_LONG_SWORD */
    g_fighter_story_weapon_points,      /* WEAPON_ID_NODACHI */
    g_fighter_broadsword_weapon_points, /* WEAPON_ID_BROADSWORD */
    g_fighter_naginata_weapon_points,   /* WEAPON_ID_NAGINATA */
    g_fighter_yari_weapon_points,       /* WEAPON_ID_YARI */
    g_fighter_long_gun_weapon_points,   /* WEAPON_ID_LONG_GUN */
    g_fighter_short_gun_weapon_points,  /* WEAPON_ID_SHORT_GUN */
};

/* By Fighter.sub_weapon_kind (enum ObjectKindId): the two points along a
 * second sword, or an off-hand gun, that are followed while it is live. */
SVECTOR *g_fighter_sub_weapon_points[OBJECT_KIND_COUNT] = {
    NULL,
    g_fighter_sub_weapon_1_points, /* 1: TATSUMI's second sword */
    NULL,
    g_fighter_sub_weapon_3_points, /* 3: UTSUSEMI's */
    g_fighter_sub_weapon_4_points, /* 4: RED SHADOW's */
    NULL,
    NULL,
    g_fighter_sub_weapon_7_points, /* 7: MATSUMUSHI's */
    NULL,
    NULL,
    NULL,
    NULL,
    g_fighter_sub_weapon_12_points, /* 12: HIGHWAYMAN's */
    NULL,
    NULL,
    NULL,
    NULL,
    g_fighter_sub_weapon_17_points, /* 17: OBJECT_PRACTICE_SWORD */
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    g_fighter_sub_weapon_30_points, /* 30: OBJECT_OFF_HAND_GUN_1 */
    g_fighter_sub_weapon_31_points, /* 31: OBJECT_OFF_HAND_GUN_2 */
};

/* [clan][weapon]: the kind of loose object a defeated fighter's weapon
 * becomes (object_DropWeapon): one for each clan's look of a weapon, the
 * naginata and the yari sharing one and the two guns another. */
#define DROPPED(n) (OBJECT_WEAPON_FIRST + (n))
/* clang-format off */
u8 g_object_dropped_weapon_kinds[2][WEAPON_ID_COUNT] = {
    /* the Narukagami's */
    {DROPPED(0), DROPPED(2), DROPPED(4), DROPPED(6),
     DROPPED(8), DROPPED(8), DROPPED(10), DROPPED(10)},
    /* the Shainto's */
    {DROPPED(1), DROPPED(3), DROPPED(5), DROPPED(7),
     DROPPED(9), DROPPED(9), DROPPED(11), DROPPED(11)},
};
/* clang-format on */
#undef DROPPED

/* ---- moves, stances and jumps ---- */

/* The names of the move kinds (enum MoveKind). Nothing reads them. */
static const char *g_fighter_move_kind_names[52] = {
    g_str_move_kind_null,
    g_str_move_kind_normal,
    g_str_move_kind_attack,
    g_str_move_kind_dam,
    g_str_move_kind_end_game,
    g_str_move_kind_surrender,
    g_str_move_kind_down,
    g_str_move_kind_stagger,
    g_str_move_kind_climb,
    g_str_move_kind_get_up,
    g_str_move_kind_drop,
    g_str_move_kind_subwep,
    g_str_move_kind_crouch_step,
    g_str_move_kind_block,
    g_str_move_kind_deflect,
    g_str_move_kind_lock,
    g_str_move_kind_reload,
    g_str_move_kind_paired,
    g_str_move_kind_dirt,
    g_str_move_kind_run,
    g_str_move_kind_dth,
    g_str_move_kind_stance,
    g_str_move_kind_sjump,
    g_str_move_kind_mjump,
    g_str_move_kind_ljump,
    g_str_move_kind_walk,
    g_str_move_kind_step,
    g_str_move_kind_arun,
    g_str_move_kind_start,
    g_str_move_kind_lock_even,
    g_str_move_kind_lock_winning_1,
    g_str_move_kind_lock_winning_2,
    g_str_move_kind_lock_losing_1,
    g_str_move_kind_lock_losing_2,
    g_str_move_kind_climb_st,
    g_str_move_kind_climb_ed,
    g_str_move_kind_move,
    g_str_move_kind_stance_change,
    g_str_move_kind_climb_attack,
    g_str_move_kind_hom_st,
    g_str_move_kind_hom_at,
    g_str_move_kind_hom_ed,
    g_str_move_kind_crouch,
    g_str_move_kind_lock_exit,
    g_str_move_kind_deflected,
    g_str_move_kind_surrender_st,
    g_str_move_kind_appear,
    g_str_move_kind_land,
    g_str_move_kind_run_st,
    g_str_move_kind_run_ed,
    g_str_move_kind_paired_ed,
    g_str_move_kind_win};

/* Each character's clan, 0 or 1, by Fighter.character. */
/* clang-format off */
u8 g_fighter_clans[CHARACTER_COUNT + 1] = {
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, /* KANNUKI..: the Narukagami */
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, /* GENGORO..: the Shainto */
    0, /* the Narukagami's crest */
    0, /* the victim */
    1, /* the Shainto's crest */
    0, /* spare */
};
/* clang-format on */

/* How fast each character can run: a class, whose limit is in
 * g_fighter_run_speed_limits (fighter_UpdateRun). The table is there twice
 * over (the original's data: only a character's place in the first half is
 * read). */
/* clang-format off */
u8 g_fighter_run_classes[72] = {
    0, 1, 2, 1, 2, 1, 0, 0, 2, 1, 1, 2, /* KANNUKI.. */
    0, 1, 2, 0, 1, 1, 2, 0, 2, 1, 1, 2, /* GENGORO.. */
    /* the crests and the victim, and nine spare */
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    /* the same again, which nothing reads */
    0, 1, 2, 1, 2, 1, 0, 0, 2, 1, 1, 2,
    0, 1, 2, 0, 1, 1, 2, 0, 2, 1, 1, 2,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
};
/* clang-format on */

/* How each character jumps: the lift and the forward speed of a long
 * jump, and the lift of a jump, in units a frame (a lift is upward, so
 * negative). Each table has a place for every roster entry and one more,
 * but is read by character. */
/* clang-format off */
s16 g_fighter_long_jump_lifts[ROSTER_COUNT + 1] = {
    /* KANNUKI.. */
    -330, -350, -365, -330, -370, -350, -330, -330, -365, -350, -350, -365,
    /* GENGORO.. */
    -330, -350, -365, -330, -350, -350, -365, -330, -365, -350, -350, -365,
    /* the crests, the victim between them */
    -365, -320, -320,
    /* no character's */
    -320, -320, -320, -320, -320, -320,    0,
};
/* clang-format on */

/* clang-format off */
s16 g_fighter_long_jump_speeds[ROSTER_COUNT + 1] = {
    /* KANNUKI.. */
    200, 220, 250, 200, 250, 220, 200, 200, 250, 220, 220, 250,
    /* GENGORO.. */
    200, 220, 250, 200, 220, 220, 250, 200, 250, 220, 220, 250,
    /* the crests, the victim between them */
    250, 256, 256,
    /* no character's */
    256, 256, 256, 256, 256, 256,   0,
};
/* clang-format on */

/* clang-format off */
s16 g_fighter_jump_lifts[ROSTER_COUNT + 1] = {
    /* KANNUKI.. */
    -320, -330, -340, -320, -340, -330, -320, -320, -340, -330, -330, -340,
    /* GENGORO.. */
    -320, -330, -340, -320, -330, -330, -340, -320, -340, -330, -330, -340,
    /* the crests, the victim between them */
    -340, -320, -320,
    /* no character's */
    -320, -320, -320, -320, -320, -320,    0,
};
/* clang-format on */

/* A fighter's three stance kinds as one number, the first lowest: S124 is
 * stances 1, 2 and STANCE_KIND_TWO_SWORDS. With the weapon on top it is
 * the fighter's motion set (motion.h), and the motion file's name says the
 * same. S666 is the victim's, who has no stance. */
#define STANCES(first, second, third) ((first) | (second) << 4 | (third) << 8)
#define S123 STANCES(1, 2, 3)
#define S124 STANCES(1, 2, STANCE_KIND_TWO_SWORDS)
#define S125 STANCES(1, 2, STANCE_KIND_SHEATHED)
#define S134 STANCES(1, 3, STANCE_KIND_TWO_SWORDS)
#define S135 STANCES(1, 3, STANCE_KIND_SHEATHED)
#define S234 STANCES(2, 3, STANCE_KIND_TWO_SWORDS)
#define S235 STANCES(2, 3, STANCE_KIND_SHEATHED)
#define S666 STANCES(6, 6, 6)

/* The three stance kinds [character][weapon] has. Columns:
 * katana, long sword, nodachi, broadsword, naginata, yari,
 * long gun, short gun. */
/* clang-format off */
u16 g_fighter_stance_kinds[CHARACTER_COUNT][WEAPON_ID_COUNT] = {
    {S123, S123, S123, S123, S123, S123, S123, S123}, /* KANNUKI */
    {S134, S123, S123, S123, S123, S123, S123, S123}, /* TATSUMI */
    {S123, S123, S123, S123, S123, S123, S123, S123}, /* MIKADO */
    {S134, S123, S123, S123, S123, S123, S123, S123}, /* UTSUSEMI */
    {S123, S234, S123, S123, S123, S123, S123, S123}, /* RED SHADOW */
    {S123, S123, S123, S123, S123, S123, S123, S123}, /* TSUBAME */
    {S123, S123, S123, S123, S123, S123, S123, S123}, /* SAZANKA */
    {S123, S123, S123, S123, S123, S123, S123, S123}, /* SUMINAGASHI */
    {S123, S123, S123, S123, S123, S123, S123, S123}, /* NIGHTSTALKER */
    {S124, S123, S123, S123, S123, S123, S123, S123}, /* MATSUMUSHI */
    {S123, S123, S123, S123, S123, S123, S123, S123}, /* CHARACTER_VANISHER */
    {S123, S123, S123, S123, S123, S123, S123, S123}, /* CHARACTER_SIDEARM_1 */
    {S125, S123, S123, S123, S123, S123, S123, S123}, /* GENGORO */
    {S235, S123, S123, S123, S123, S123, S123, S123}, /* KAUN */
    {S123, S123, S123, S123, S123, S123, S123, S123}, /* JO */
    {S123, S123, S123, S123, S123, S123, S123, S123}, /* HONGOU */
    {S123, S123, S123, S123, S123, S123, S123, S123}, /* KATZE */
    {S123, S124, S123, S123, S123, S123, S123, S123}, /* HIGHWAYMAN */
    {S123, S123, S123, S123, S123, S123, S123, S123}, /* CHIHIRO */
    {S123, S125, S123, S123, S123, S123, S123, S123}, /* UTAMARU */
    {S125, S123, S123, S123, S123, S123, S123, S123}, /* TONY UMEDA */
    {S123, S123, S123, S123, S123, S123, S123, S123}, /* ISOHACHI */
    {S123, S123, S123, S123, S123, S123, S123, S123}, /* CHARACTER_ARMORED */
    {S123, S123, S123, S123, S123, S123, S123, S123}, /* CHARACTER_SIDEARM_2 */
    {S123, S123, S123, S123, S123, S123, S123, S123}, /* CHARACTER_LAST_1 */
    {S666, S123, S123, S123, S123, S123, S123, S123}, /* CHARACTER_VICTIM */
    {S123, S123, S123, S123, S123, S123, S123, S123}, /* CHARACTER_LAST_2 */
};
/* clang-format on */

/* The motion sets that have a file, in the order of the files on the disc,
 * ended by a zero (motion.h): the two swords that have the special stances,
 * then a file a weapon, and last the victim's. */
/* clang-format off */
MotionSetFile g_motion_set_files[18] = {
    {MOTION_SET(WEAPON_ID_KATANA, S123),     g_str_motion_file_u123},
    {MOTION_SET(WEAPON_ID_KATANA, S124),     g_str_motion_file_u124},
    {MOTION_SET(WEAPON_ID_KATANA, S125),     g_str_motion_file_u125},
    {MOTION_SET(WEAPON_ID_KATANA, S134),     g_str_motion_file_u134},
    {MOTION_SET(WEAPON_ID_KATANA, S135),     g_str_motion_file_u135},
    {MOTION_SET(WEAPON_ID_KATANA, S235),     g_str_motion_file_u235},
    {MOTION_SET(WEAPON_ID_LONG_SWORD, S123), g_str_motion_file_s123},
    {MOTION_SET(WEAPON_ID_LONG_SWORD, S124), g_str_motion_file_s124},
    {MOTION_SET(WEAPON_ID_LONG_SWORD, S125), g_str_motion_file_s125},
    {MOTION_SET(WEAPON_ID_LONG_SWORD, S234), g_str_motion_file_s234},
    {MOTION_SET(WEAPON_ID_NODACHI, S123),    g_str_motion_file_t123},
    {MOTION_SET(WEAPON_ID_BROADSWORD, S123), g_str_motion_file_k123},
    {MOTION_SET(WEAPON_ID_NAGINATA, S123),   g_str_motion_file_n123},
    {MOTION_SET(WEAPON_ID_YARI, S123),       g_str_motion_file_y123},
    {MOTION_SET(WEAPON_ID_LONG_GUN, S123),   g_str_motion_file_gn},
    {MOTION_SET(WEAPON_ID_SHORT_GUN, S123),  g_str_motion_file_gs},
    {S666,                                   g_str_motion_file_on},
    {0,                                      NULL},
};
/* clang-format on */

#undef STANCES
#undef S123
#undef S124
#undef S125
#undef S134
#undef S135
#undef S234
#undef S235
#undef S666

/* The stance [character][weapon] begins a fight in: the first, second or
 * third of its three. Columns as above. */
/* clang-format off */
u8 g_fighter_default_stances[CHARACTER_COUNT][WEAPON_ID_COUNT] = {
    {0, 0, 0, 0, 0, 0, 0, 0}, /* KANNUKI */
    {0, 1, 1, 1, 1, 0, 0, 0}, /* TATSUMI */
    {2, 2, 2, 2, 2, 0, 0, 0}, /* MIKADO */
    {1, 1, 0, 2, 1, 0, 0, 0}, /* UTSUSEMI */
    {1, 1, 1, 0, 0, 0, 0, 0}, /* RED SHADOW */
    {0, 0, 0, 0, 0, 0, 0, 0}, /* TSUBAME */
    {2, 2, 0, 0, 1, 0, 0, 0}, /* SAZANKA */
    {0, 0, 2, 2, 1, 0, 0, 0}, /* SUMINAGASHI */
    {2, 2, 1, 1, 2, 0, 0, 0}, /* NIGHTSTALKER */
    {1, 1, 2, 2, 0, 0, 0, 0}, /* MATSUMUSHI */
    {2, 1, 0, 1, 2, 0, 0, 0}, /* CHARACTER_VANISHER */
    {1, 0, 0, 0, 0, 0, 0, 0}, /* CHARACTER_SIDEARM_1 */
    {0, 0, 0, 0, 0, 0, 0, 0}, /* GENGORO */
    {0, 1, 1, 1, 0, 1, 0, 0}, /* KAUN */
    {2, 2, 2, 2, 0, 2, 0, 0}, /* JO */
    {1, 0, 0, 2, 0, 1, 0, 0}, /* HONGOU */
    {0, 0, 0, 0, 0, 0, 0, 0}, /* KATZE */
    {0, 1, 2, 1, 0, 0, 0, 0}, /* HIGHWAYMAN */
    {2, 0, 2, 2, 0, 2, 0, 0}, /* CHIHIRO */
    {0, 0, 0, 2, 0, 1, 0, 0}, /* UTAMARU */
    {1, 2, 1, 0, 0, 2, 0, 0}, /* TONY UMEDA */
    {1, 1, 1, 1, 0, 2, 0, 0}, /* ISOHACHI */
    {1, 0, 0, 2, 0, 1, 0, 0}, /* CHARACTER_ARMORED */
    {0, 0, 0, 0, 0, 0, 0, 0}, /* CHARACTER_SIDEARM_2 */
    {1, 0, 0, 2, 0, 2, 0, 0}, /* CHARACTER_LAST_1 */
    {0, 0, 0, 0, 0, 0, 0, 0}, /* CHARACTER_VICTIM */
    {1, 0, 0, 2, 0, 1, 0, 0}, /* CHARACTER_LAST_2 */
};
/* clang-format on */

/* ---- the characters' strengths ---- */

/* How fast each character's moves run with each weapon
 * (FIGHTER_SPEED_ONE, 0x1000: as they were recorded). Columns:
 * katana, long sword, nodachi, broadsword, naginata, yari. */
/* clang-format off */
u16 g_fighter_speeds[CHARACTER_COUNT][6] = {
    { 0xDF0,  0xDD0,  0xEB0,  0xF56,  0xD40, 0x1000}, /* KANNUKI */
    { 0xE38,  0xF20,  0xDEE,  0xD40,  0xE00, 0x1000}, /* TATSUMI */
    { 0xEE0,  0xFE0,  0xD40,  0xCB0,  0xF60, 0x1000}, /* MIKADO */
    { 0xE30,  0xFBB,  0xF68,  0xE10,  0xEC0, 0x1000}, /* UTSUSEMI */
    { 0xED6,  0xFE7,  0xD30,  0xCA6,  0xF57, 0x1000}, /* RED SHADOW */
    { 0xEE0,  0xFE0,  0xD40,  0xCB0,  0xF60, 0x1000}, /* TSUBAME */
    { 0xE20,  0xF10,  0xF60,  0xEB0,  0xF68, 0x1000}, /* SAZANKA */
    { 0xE96,  0xEE7,  0xEC8,  0xDE8,  0xEB0, 0x1000}, /* SUMINAGASHI */
    { 0xF90,  0xFF0,  0xCC8,  0xCA0,  0xF50, 0x1000}, /* NIGHTSTALKER */
    { 0xF60,  0xFD0,  0xDF6,  0xE08,  0xEB0, 0x1000}, /* MATSUMUSHI */
    {0x1000, 0x1000, 0x1000, 0x1000, 0x1000, 0x1000}, /* CHARACTER_VANISHER */
    { 0xA00, 0x1000, 0x1000, 0x1000, 0x1000, 0x1000}, /* CHARACTER_SIDEARM_1 */
    { 0xE18,  0xE70,  0xEA0,  0xE98, 0x1000,  0xE10}, /* GENGORO */
    { 0xE27,  0xF00,  0xF70,  0xE17, 0x1000,  0xF48}, /* KAUN */
    { 0xF80,  0xFD9,  0xD60,  0xCC0, 0x1000,  0xCD9}, /* JO */
    { 0xE10,  0xF17,  0xF58,  0xF40, 0x1000,  0xD60}, /* HONGOU */
    { 0xEE0,  0xFE0,  0xD40,  0xCB0,  0xF60, 0x1000}, /* KATZE */
    { 0xEC0,  0xFC9,  0xE20,  0xF37, 0x1000,  0xCC9}, /* HIGHWAYMAN */
    { 0xD80,  0xF30,  0xCD8,  0xCB0, 0x1000,  0xD67}, /* CHIHIRO */
    { 0xEB0,  0xFC0,  0xE00,  0xCC0, 0x1000,  0xEC8}, /* UTAMARU */
    { 0xF70,  0xFD4,  0xF50,  0xDF9, 0x1000,  0xE20}, /* TONY UMEDA */
    { 0xD70,  0xE90,  0xE10,  0xCD0, 0x1000,  0xD58}, /* ISOHACHI */
    {0x1000, 0x1000, 0x1000, 0x1000, 0x1000, 0x1000}, /* CHARACTER_ARMORED */
    { 0xA00, 0x1000, 0x1000, 0x1000, 0x1000, 0x1000}, /* CHARACTER_SIDEARM_2 */
    {0x1000, 0x1000, 0x1000, 0x1000, 0x1000, 0x1000}, /* CHARACTER_LAST_1 */
    {0x1000, 0x1000, 0x1000, 0x1000, 0x1000, 0x1000}, /* CHARACTER_VICTIM */
    {0x1000, 0x1000, 0x1000, 0x1000, 0x1000, 0x1000}, /* CHARACTER_LAST_2 */
};
/* clang-format on */

/* And how hard it hits (0x1000: as the move says). The same columns. */
/* clang-format off */
u16 g_fighter_powers[CHARACTER_COUNT][6] = {
    {0x1300, 0x1000, 0x1650, 0x1670, 0x1300, 0x1000}, /* KANNUKI */
    {0x1070, 0x0CE0, 0x1270, 0x1230, 0x1000, 0x1000}, /* TATSUMI */
    {0x0CCA, 0x0AD0, 0x0F85, 0x0FAE, 0x1600, 0x1000}, /* MIKADO */
    {0x1620, 0x1000, 0x1660, 0x1000, 0x1000, 0x1000}, /* UTSUSEMI */
    {0x0B60, 0x0B00, 0x0CE0, 0x1000, 0x0CE0, 0x1000}, /* RED SHADOW */
    {0x1000, 0x1000, 0x1000, 0x1000, 0x1000, 0x1000}, /* TSUBAME */
    {0x1300, 0x1240, 0x1300, 0x1300, 0x1340, 0x1000}, /* SAZANKA */
    {0x12C0, 0x11F0, 0x1208, 0x1010, 0x1005, 0x1000}, /* SUMINAGASHI */
    {0x0B80, 0x0B00, 0x0CE0, 0x0D00, 0x0CE0, 0x1000}, /* NIGHTSTALKER */
    {0x0FD7, 0x0CA3, 0x1000, 0x1028, 0x0CE6, 0x1000}, /* MATSUMUSHI */
    {0x1700, 0x1700, 0x1700, 0x1700, 0x1700, 0x1000}, /* CHARACTER_VANISHER */
    {0x0B00, 0x1000, 0x1000, 0x1000, 0x1000, 0x1000}, /* CHARACTER_SIDEARM_1 */
    {0x1250, 0x1220, 0x1300, 0x1300, 0x1000, 0x1280}, /* GENGORO */
    {0x1000, 0x0CE0, 0x1000, 0x10A0, 0x1000, 0x1660}, /* KAUN */
    {0x0CBA, 0x0ADF, 0x128F, 0x0FAE, 0x1000, 0x0FD7}, /* JO */
    {0x1660, 0x1000, 0x1290, 0x1300, 0x1000, 0x1350}, /* HONGOU */
    {0x1000, 0x1000, 0x1000, 0x1000, 0x1000, 0x1000}, /* KATZE */
    {0x0FF8, 0x0FF0, 0x1000, 0x1300, 0x1000, 0x1660}, /* HIGHWAYMAN */
    {0x0C02, 0x0C00, 0x0C04, 0x0C05, 0x1000, 0x0C03}, /* CHIHIRO */
    {0x0CE0, 0x0B00, 0x1000, 0x1660, 0x1000, 0x1300}, /* UTAMARU */
    {0x0B70, 0x0B00, 0x1000, 0x10A0, 0x1000, 0x1070}, /* TONY UMEDA */
    {0x0CE8, 0x0CE0, 0x1300, 0x1350, 0x1000, 0x1000}, /* ISOHACHI */
    {0x1700, 0x1700, 0x1700, 0x1700, 0x1000, 0x1700}, /* CHARACTER_ARMORED */
    {0x0B00, 0x1000, 0x1000, 0x1000, 0x1000, 0x1000}, /* CHARACTER_SIDEARM_2 */
    {0x1700, 0x1700, 0x1700, 0x1700, 0x1700, 0x1700}, /* CHARACTER_LAST_1 */
    {0x1700, 0x1700, 0x1700, 0x1700, 0x1700, 0x1700}, /* CHARACTER_VICTIM */
    {0x1700, 0x1700, 0x1700, 0x1700, 0x1700, 0x1700}, /* CHARACTER_LAST_2 */
};
/* clang-format on */

/* What is left of a character's power after 0 to 3 wounds to the body
 * (0x1000: all of it). */
/* clang-format off */
s16 g_fighter_wound_powers[CHARACTER_COUNT][4] = {
    {0x1000, 0x0DC0, 0x0CD0, 0x09C0}, /* KANNUKI */
    {0x1000, 0x0DC0, 0x0C80, 0x0940}, /* TATSUMI */
    {0x1000, 0x0D60, 0x0C48, 0x0937}, /* MIKADO */
    {0x1000, 0x0DC0, 0x0CC0, 0x09A0}, /* UTSUSEMI */
    {0x1000, 0x0D60, 0x0C48, 0x0937}, /* RED SHADOW */
    {0x1000, 0x0DC0, 0x0C80, 0x0940}, /* TSUBAME */
    {0x1000, 0x0DE0, 0x0CE0, 0x09A0}, /* SAZANKA */
    {0x1000, 0x0DD0, 0x0CC0, 0x09B0}, /* SUMINAGASHI */
    {0x1000, 0x0D60, 0x0C48, 0x0937}, /* NIGHTSTALKER */
    {0x1000, 0x0D60, 0x0C48, 0x0937}, /* MATSUMUSHI */
    {0x1000, 0x0DF0, 0x0CE0, 0x09D0}, /* CHARACTER_VANISHER */
    {0x1000, 0x0D60, 0x0C30, 0x0900}, /* CHARACTER_SIDEARM_1 */
    {0x1000, 0x0DE0, 0x0CC0, 0x09A0}, /* GENGORO */
    {0x1000, 0x0DC0, 0x0C80, 0x0940}, /* KAUN */
    {0x1000, 0x0D60, 0x0C48, 0x0937}, /* JO */
    {0x1000, 0x0D60, 0x0C48, 0x0937}, /* HONGOU */
    {0x1000, 0x0DC0, 0x0C80, 0x0940}, /* KATZE */
    {0x1000, 0x0DC0, 0x0C80, 0x0940}, /* HIGHWAYMAN */
    {0x1000, 0x0D60, 0x0C48, 0x0937}, /* CHIHIRO */
    {0x1000, 0x0D60, 0x0C48, 0x0937}, /* UTAMARU */
    {0x1000, 0x0DE0, 0x0CC0, 0x09A0}, /* TONY UMEDA */
    {0x1000, 0x0DC0, 0x0C80, 0x0940}, /* ISOHACHI */
    {0x1000, 0x0DF0, 0x0CE0, 0x09D0}, /* CHARACTER_ARMORED */
    {0x1000, 0x0D60, 0x0C30, 0x0900}, /* CHARACTER_SIDEARM_2 */
    {0x1000, 0x0DF0, 0x0CE0, 0x09D0}, /* CHARACTER_LAST_1 */
    {0x1000, 0x0DF0, 0x0CE0, 0x09D0}, /* CHARACTER_VICTIM */
    {0x1000, 0x0DF0, 0x0CE0, 0x09D0}, /* CHARACTER_LAST_2 */
};
/* clang-format on */

/* ---- loose objects ---- */

/* What each kind of loose object does (object.h), by enum ObjectKindId:
 * first the characters' sub-weapons, each beside the character that starts
 * with it (g_object_start_sub_weapons below; TATSUMI's, UTSUSEMI's, RED
 * SHADOW's, MATSUMUSHI's and HIGHWAYMAN's are second swords, JO's is
 * OBJECT_JO_SUB_WEAPON, CHIHIRO's OBJECT_FROG, SUMINAGASHI's
 * OBJECT_BURSTING and TONY UMEDA's OBJECT_BOOMERANG), then
 * OBJECT_PRACTICE_SWORD. The off-hand guns, which are never loose, have
 * none. Columns: how it flies; unk2; how fast it is thrown along and up, a
 * frame; its spin, an angle a frame; the part of its speed it keeps in a
 * bounce (0x1000: all); what an attack just begun does to it (2 is
 * neither of the two with a name: it lands all the same); what it does to
 * who it hits (1 has no name: its wounds are single and it cannot reach a
 * core). */
#define STRAIGHT OBJECT_FLIES_STRAIGHT
#define FLAT OBJECT_SPINS_FLAT
#define TUMBLES OBJECT_TUMBLES
#define CURVES OBJECT_CURVES
#define HOPS OBJECT_HOPS
#define ASIDE OBJECT_CLASH_ASIDE
#define PUSH OBJECT_CLASH_PUSH
#define KILLS OBJECT_HARM_KILLS
#define DOUBLE OBJECT_HARM_DOUBLE
/* clang-format off */
LooseObjectKind g_object_kinds[30] = {
    {FLAT,     100, 400,  -80,  0x480,  0x800, PUSH,  DOUBLE}, /* KANNUKI */
    {STRAIGHT, 100, 400,  -80,  0,      0x800, PUSH,  KILLS},  /* TATSUMI */
    {STRAIGHT, 100, 500,  -80,  0,      0x800, ASIDE, 1},      /* MIKADO */
    {STRAIGHT, 100, 400,  -80,  0,      0x800, PUSH,  KILLS},  /* UTSUSEMI */
    {STRAIGHT, 100, 400,  -80,  0,      0x800, PUSH,  KILLS},  /* RED SHADOW */
    {STRAIGHT, 100, 400,  -80,  0,      0x800, 2,     1},      /* SAZANKA */
    {TUMBLES,  100, 700,  -80,  0x800,  0x800, ASIDE, DOUBLE}, /* NIGHTSTALKER */
    {STRAIGHT, 100, 400,  -80,  0,      0x800, PUSH,  KILLS},  /* MATSUMUSHI */
    {STRAIGHT, 100, 500,  -80,  0,      0x800, ASIDE, 1},      /* GENGORO */
    {STRAIGHT, 100, 400,  -80,  0,      0x800, ASIDE, KILLS},  /* KAUN */
    {STRAIGHT, 100, 300,  -80,  0,      0x800, ASIDE, DOUBLE}, /* JO */
    {TUMBLES,  100, 400,  -80,  -0x300, 0x800, ASIDE, KILLS},  /* HONGOU */
    {STRAIGHT, 100, 400,  -80,  0,      0x800, PUSH,  KILLS},  /* HIGHWAYMAN */
    {TUMBLES,  100, 500,  -80,  0,      0x800, ASIDE, 1},      /* UTAMARU */
    {HOPS,     100, 300,  -100, 0,      0x400, 2,     1},      /* CHIHIRO */
    {TUMBLES,  100, 400,  -80,  0x300,  0x800, 2,     1},      /* SUMINAGASHI */
    {CURVES,   100, 400,  -100, 0x880,  0x800, ASIDE, 1},      /* TONY UMEDA */
    {TUMBLES,  100, 1000, 0,    0,      0x800, ASIDE, KILLS},  /* practice */
    /* OBJECT_WEAPON_FIRST: the twelve kinds of dropped weapon */
    {TUMBLES,  100, 1000, 0,    0,      0x800, ASIDE, KILLS},
    {TUMBLES,  100, 1000, 0,    0,      0x800, ASIDE, KILLS},
    {TUMBLES,  100, 1000, 0,    0,      0x800, ASIDE, KILLS},
    {TUMBLES,  100, 1000, 0,    0,      0x800, ASIDE, KILLS},
    {TUMBLES,  100, 1000, 0,    0,      0x700, ASIDE, KILLS},
    {TUMBLES,  100, 1000, 0,    0,      0x700, ASIDE, KILLS},
    {TUMBLES,  100, 1000, 0,    0,      0x700, ASIDE, KILLS},
    {TUMBLES,  100, 1000, 0,    0,      0x700, ASIDE, KILLS},
    {TUMBLES,  100, 1000, 0,    0,      0x780, ASIDE, KILLS},
    {TUMBLES,  100, 1000, 0,    0,      0x800, ASIDE, KILLS},
    {TUMBLES,  100, 1000, 0,    0,      0x600, ASIDE, KILLS},
    {TUMBLES,  100, 1000, 0,    0,      0x600, ASIDE, KILLS},
};
/* clang-format on */
#undef STRAIGHT
#undef FLAT
#undef TUMBLES
#undef CURVES
#undef HOPS
#undef ASIDE
#undef PUSH
#undef KILLS
#undef DOUBLE

/* The sub-weapons each character starts a fight with, by kind (enum
 * ObjectKindId): most have one or two of a kind of their own, NIGHTSTALKER
 * five. */
#define NONE OBJECT_NONE
/* clang-format off */
s8 g_object_start_sub_weapons[CHARACTER_COUNT][SUB_WEAPONS_START] = {
    {0, NONE, NONE, NONE, NONE},    /* KANNUKI */
    {1, NONE, NONE, NONE, NONE},    /* TATSUMI */
    {2, 2, NONE, NONE, NONE},       /* MIKADO */
    {3, NONE, NONE, NONE, NONE},    /* UTSUSEMI */
    {4, NONE, NONE, NONE, NONE},    /* RED SHADOW */
    {NONE, NONE, NONE, NONE, NONE}, /* TSUBAME */
    {5, 5, NONE, NONE, NONE},       /* SAZANKA */
    {OBJECT_BURSTING, OBJECT_BURSTING, NONE, NONE, NONE}, /* SUMINAGASHI */
    {6, 6, 6, 6, 6},                /* NIGHTSTALKER */
    {7, NONE, NONE, NONE, NONE},    /* MATSUMUSHI */
    {NONE, NONE, NONE, NONE, NONE}, /* CHARACTER_VANISHER */
    {NONE, NONE, NONE, NONE, NONE}, /* CHARACTER_SIDEARM_1 */
    {8, 8, NONE, NONE, NONE},       /* GENGORO */
    {9, NONE, NONE, NONE, NONE},    /* KAUN */
    {OBJECT_JO_SUB_WEAPON, NONE, NONE, NONE, NONE}, /* JO */
    {11, NONE, NONE, NONE, NONE},   /* HONGOU */
    {NONE, NONE, NONE, NONE, NONE}, /* KATZE */
    {12, NONE, NONE, NONE, NONE},   /* HIGHWAYMAN */
    {OBJECT_FROG, NONE, NONE, NONE, NONE}, /* CHIHIRO */
    {13, 13, NONE, NONE, NONE},     /* UTAMARU */
    {OBJECT_BOOMERANG, OBJECT_BOOMERANG, NONE, NONE, NONE}, /* TONY UMEDA */
    {NONE, NONE, NONE, NONE, NONE}, /* ISOHACHI */
    {NONE, NONE, NONE, NONE, NONE}, /* CHARACTER_ARMORED */
    {NONE, NONE, NONE, NONE, NONE}, /* CHARACTER_SIDEARM_2 */
    {NONE, NONE, NONE, NONE, NONE}, /* CHARACTER_LAST_1 */
    {NONE, NONE, NONE, NONE, NONE}, /* CHARACTER_VICTIM */
    {NONE, NONE, NONE, NONE, NONE}, /* CHARACTER_LAST_2 */
};
/* clang-format on */
#undef NONE

/* ---- wounds ---- */

/* How much slower wounds make a character (0x1000: no slower): a lost off
 * arm, */
/* clang-format off */
u16 g_fighter_lost_arm_speeds[CHARACTER_COUNT + 1] = {
    /* KANNUKI.. */
    0xFE0, 0xFB0, 0xFA0, 0xFE0, 0xF98, 0xFB0,
    0xFE0, 0xFB0, 0xFA0, 0xFA0, 0xFE0, 0xF40,
    /* GENGORO.. */
    0xFE0, 0xFB0, 0xFA0, 0xFA0, 0xFB0, 0xFB0,
    0xFA0, 0xFA0, 0xFE0, 0xFB0, 0xFE0, 0xF40,
    /* the crests, the victim between them */
    0xFE0, 0xFE0, 0xFE0,
    0, /* spare */
};
/* clang-format on */

/* 0 to 3 wounds to the sword arm, */
/* clang-format off */
u16 g_fighter_arm_wound_speeds[CHARACTER_COUNT][4] = {
    {0x1000, 0xFE0, 0xFC0, 0xFA0}, /* KANNUKI */
    {0x1000, 0xFC0, 0xF80, 0xF40}, /* TATSUMI */
    {0x1000, 0xFA0, 0xF60, 0xF20}, /* MIKADO */
    {0x1000, 0xFE0, 0xFC0, 0xFA0}, /* UTSUSEMI */
    {0x1000, 0xFA0, 0xF60, 0xF30}, /* RED SHADOW */
    {0x1000, 0xFC0, 0xF80, 0xF40}, /* TSUBAME */
    {0x1000, 0xFE0, 0xFC0, 0xFA0}, /* SAZANKA */
    {0x1000, 0xFD0, 0xFB0, 0xF90}, /* SUMINAGASHI */
    {0x1000, 0xFA0, 0xF60, 0xF20}, /* NIGHTSTALKER */
    {0x1000, 0xFA0, 0xF70, 0xF30}, /* MATSUMUSHI */
    {0x1000, 0xFF0, 0xFE0, 0xFD0}, /* CHARACTER_VANISHER */
    {0x1000, 0xF80, 0xF40, 0xF00}, /* CHARACTER_SIDEARM_1 */
    {0x1000, 0xFE0, 0xFC0, 0xFA0}, /* GENGORO */
    {0x1000, 0xFC0, 0xF80, 0xF40}, /* KAUN */
    {0x1000, 0xFA0, 0xF60, 0xF20}, /* JO */
    {0x1000, 0xFC0, 0xFA0, 0xF80}, /* HONGOU */
    {0x1000, 0xFC0, 0xF80, 0xF40}, /* KATZE */
    {0x1000, 0xFC0, 0xFA0, 0xF80}, /* HIGHWAYMAN */
    {0x1000, 0xF90, 0xF60, 0xF00}, /* CHIHIRO */
    {0x1000, 0xF90, 0xF60, 0xF10}, /* UTAMARU */
    {0x1000, 0xFD0, 0xFB0, 0xF90}, /* TONY UMEDA */
    {0x1000, 0xFE0, 0xFA0, 0xF40}, /* ISOHACHI */
    {0x1000, 0xFF0, 0xFE0, 0xFD0}, /* CHARACTER_ARMORED */
    {0x1000, 0xF80, 0xF40, 0xF00}, /* CHARACTER_SIDEARM_2 */
    {0x1000, 0xFF0, 0xFE0, 0xFD0}, /* CHARACTER_LAST_1 */
    {0x1000, 0xFF0, 0xFE0, 0xFD0}, /* CHARACTER_VICTIM */
    {0x1000, 0xFF0, 0xFE0, 0xFD0}, /* CHARACTER_LAST_2 */
};
/* clang-format on */

/* and 0 to 3 to the legs and body together. */
/* clang-format off */
u16 g_fighter_wound_speeds[CHARACTER_COUNT][4] = {
    {0x1000, 0xFE0, 0xFC0, 0xFA0}, /* KANNUKI */
    {0x1000, 0xFC0, 0xF80, 0xF40}, /* TATSUMI */
    {0x1000, 0xF80, 0xF50, 0xF20}, /* MIKADO */
    {0x1000, 0xFE0, 0xFC0, 0xFA0}, /* UTSUSEMI */
    {0x1000, 0xF90, 0xF60, 0xF30}, /* RED SHADOW */
    {0x1000, 0xFC0, 0xF80, 0xF40}, /* TSUBAME */
    {0x1000, 0xFE0, 0xFC0, 0xFA0}, /* SAZANKA */
    {0x1000, 0xFE0, 0xFB0, 0xF90}, /* SUMINAGASHI */
    {0x1000, 0xF80, 0xF40, 0xF00}, /* NIGHTSTALKER */
    {0x1000, 0xF90, 0xF50, 0xF40}, /* MATSUMUSHI */
    {0x1000, 0xFF0, 0xFE0, 0xFD0}, /* CHARACTER_VANISHER */
    {0x1000, 0xF60, 0xF20, 0xF00}, /* CHARACTER_SIDEARM_1 */
    {0x1000, 0xFE0, 0xFC0, 0xFA0}, /* GENGORO */
    {0x1000, 0xFC0, 0xF80, 0xF40}, /* KAUN */
    {0x1000, 0xF60, 0xF40, 0xF30}, /* JO */
    {0x1000, 0xFC0, 0xFA0, 0xF80}, /* HONGOU */
    {0x1000, 0xFC0, 0xF80, 0xF40}, /* KATZE */
    {0x1000, 0xFB0, 0xF90, 0xF70}, /* HIGHWAYMAN */
    {0x1000, 0xF60, 0xF20, 0xF00}, /* CHIHIRO */
    {0x1000, 0xFC0, 0xF80, 0xF40}, /* UTAMARU */
    {0x1000, 0xFC0, 0xFA0, 0xF80}, /* TONY UMEDA */
    {0x1000, 0xFE0, 0xFA0, 0xF40}, /* ISOHACHI */
    {0x1000, 0xFF0, 0xFE0, 0xFD0}, /* CHARACTER_ARMORED */
    {0x1000, 0xF60, 0xF30, 0xF00}, /* CHARACTER_SIDEARM_2 */
    {0x1000, 0xFF0, 0xFE0, 0xFD0}, /* CHARACTER_LAST_1 */
    {0x1000, 0xFF0, 0xFE0, 0xFD0}, /* CHARACTER_VICTIM */
    {0x1000, 0xFF0, 0xFE0, 0xFD0}, /* CHARACTER_LAST_2 */
};
/* clang-format on */

/* ---- sounds ---- */

/* Each roster entry's class, one of five: it picks which of a stage's
 * banks of sounds the fighter gets (SND_VAB_CLASS_...) and the footprint it
 * leaves (g_eff_footprint_shape_bits). */
/* clang-format off */
u8 g_fighter_roster_classes[36] = {
    0, /* ROSTER_KANNUKI_STORY */
    0, /* ROSTER_TATSUMI_STORY */
    0, /* ROSTER_MIKADO_STORY */
    1, /* ROSTER_KANNUKI */
    3, /* ROSTER_TATSUMI */
    1, /* ROSTER_MIKADO */
    2, /* ROSTER_UTSUSEMI */
    2, /* ROSTER_RED_SHADOW */
    0, /* ROSTER_TSUBAME */
    3, /* ROSTER_SAZANKA */
    2, /* ROSTER_SUMINAGASHI */
    2, /* ROSTER_NIGHTSTALKER */
    2, /* ROSTER_MATSUMUSHI */
    4, /* ROSTER_VANISHER */
    2, /* ROSTER_SIDEARM_1 */
    0, /* ROSTER_GENGORO_STORY */
    0, /* ROSTER_KAUN_STORY */
    0, /* ROSTER_JO_STORY */
    2, /* ROSTER_GENGORO */
    2, /* ROSTER_KAUN */
    2, /* ROSTER_JO */
    1, /* ROSTER_HONGOU */
    0, /* ROSTER_KATZE */
    3, /* ROSTER_HIGHWAYMAN */
    1, /* ROSTER_CHIHIRO */
    0, /* ROSTER_UTAMARU */
    0, /* ROSTER_TONY_UMEDA */
    0, /* ROSTER_ISOHACHI */
    2, /* ROSTER_ARMORED */
    0, /* ROSTER_SIDEARM_2 */
    2, /* ROSTER_LAST_1 */
    0, /* ROSTER_VICTIM */
    2, /* ROSTER_LAST_2 */
    0, 0, 0, /* spare */
};
/* clang-format on */

/* The weapon sound kind of each character with each weapon, one of
 * WEAPON_SET_0_SOUND_KINDS (resource.h). Columns:
 * katana, long sword, nodachi, broadsword, naginata, yari,
 * long gun, short gun. */
/* clang-format off */
u8 g_resource_weapon_sound_kinds[CHARACTER_COUNT][WEAPON_ID_COUNT] = {
    { 0,  3,  7,  9, 10, 12, 14, 15}, /* KANNUKI */
    { 2,  5,  6,  8, 10, 12, 14, 15}, /* TATSUMI */
    { 0,  3,  6,  8, 11, 12, 14, 15}, /* MIKADO */
    { 2,  5,  7,  8, 10, 12, 14, 15}, /* UTSUSEMI */
    { 2,  5,  6,  8, 10, 12, 14, 15}, /* RED SHADOW */
    {15, 15, 15, 15, 15, 15, 14, 15}, /* TSUBAME */
    { 0,  3,  6,  9, 11, 12, 14, 15}, /* SAZANKA */
    { 0,  3,  6,  8, 10, 12, 14, 15}, /* SUMINAGASHI */
    { 0,  3,  6,  8, 10, 12, 14, 15}, /* NIGHTSTALKER */
    { 2,  5,  6,  8, 10, 12, 14, 15}, /* MATSUMUSHI */
    { 1,  4,  7,  9, 11, 13, 14, 15}, /* CHARACTER_VANISHER */
    { 0,  3,  6,  8, 10, 12, 14, 15}, /* CHARACTER_SIDEARM_1 */
    { 1,  3,  7,  7, 10, 12, 14, 15}, /* GENGORO */
    { 1,  3,  6,  8, 10, 13, 14, 15}, /* KAUN */
    { 1,  4,  6,  8, 10, 12, 14, 15}, /* JO */
    { 0,  3,  6,  9, 10, 12, 14, 15}, /* HONGOU */
    {14, 14, 14, 14, 14, 14, 14, 15}, /* KATZE */
    { 2,  5,  6,  8, 10, 12, 14, 15}, /* HIGHWAYMAN */
    { 0,  3,  6,  8, 10, 12, 14, 15}, /* CHIHIRO */
    { 0,  3,  6,  8, 10, 12, 14, 15}, /* UTAMARU */
    { 0,  4,  6,  8, 10, 12, 14, 15}, /* TONY UMEDA */
    { 1,  3,  7,  8, 10, 13, 14, 15}, /* ISOHACHI */
    { 1,  4,  7,  9, 11, 13, 14, 15}, /* CHARACTER_ARMORED */
    { 0,  3,  6,  8, 10, 12, 14, 15}, /* CHARACTER_SIDEARM_2 */
    { 1,  4,  7,  9, 11, 13, 14, 15}, /* CHARACTER_LAST_1 */
    { 1,  1,  1,  1,  1,  1, 14, 15}, /* CHARACTER_VICTIM */
    { 1,  4,  7,  9, 11, 13, 14, 15}, /* CHARACTER_LAST_2 */
};
/* clang-format on */

/* And in team battles and in practice against the ninja, one of
 * WEAPON_SET_1_SOUND_KINDS. The first six columns. */
/* clang-format off */
u8 g_resource_weapon_sound_kinds_alt[CHARACTER_COUNT][6] = {
    {0, 2, 4, 5, 6, 7}, /* KANNUKI */
    {1, 3, 4, 5, 6, 7}, /* TATSUMI */
    {0, 2, 4, 5, 6, 7}, /* MIKADO */
    {1, 3, 4, 5, 6, 7}, /* UTSUSEMI */
    {1, 3, 4, 5, 6, 7}, /* RED SHADOW */
    {1, 2, 4, 5, 6, 7}, /* TSUBAME */
    {0, 2, 4, 5, 6, 7}, /* SAZANKA */
    {0, 2, 4, 5, 6, 7}, /* SUMINAGASHI */
    {0, 2, 4, 5, 6, 7}, /* NIGHTSTALKER */
    {1, 3, 4, 5, 6, 7}, /* MATSUMUSHI */
    {0, 2, 4, 5, 6, 7}, /* CHARACTER_VANISHER */
    {0, 2, 4, 5, 6, 7}, /* CHARACTER_SIDEARM_1 */
    {0, 2, 4, 5, 6, 7}, /* GENGORO */
    {0, 2, 4, 5, 6, 7}, /* KAUN */
    {0, 2, 4, 5, 6, 7}, /* JO */
    {0, 2, 4, 5, 6, 7}, /* HONGOU */
    {1, 2, 4, 5, 6, 7}, /* KATZE */
    {1, 3, 4, 5, 6, 7}, /* HIGHWAYMAN */
    {0, 2, 4, 5, 6, 7}, /* CHIHIRO */
    {0, 2, 4, 5, 6, 7}, /* UTAMARU */
    {0, 2, 4, 5, 6, 7}, /* TONY UMEDA */
    {0, 2, 4, 5, 6, 7}, /* ISOHACHI */
    {0, 2, 4, 5, 6, 7}, /* CHARACTER_ARMORED */
    {0, 2, 4, 5, 6, 7}, /* CHARACTER_SIDEARM_2 */
    {0, 2, 4, 5, 6, 7}, /* CHARACTER_LAST_1 */
    {0, 1, 4, 5, 6, 7}, /* CHARACTER_VICTIM */
    {0, 2, 4, 5, 6, 7}, /* CHARACTER_LAST_2 */
};
/* clang-format on */

/* ---- the modes' opponents ---- */

/* VSCPU (fight.h): the weapon of each opponent of a run, in the order they
 * come (the gunners take none from it; a naginata is a yari in a Shainto's
 * hands), */
u8 g_vscpu_weapons[20] = {
    WEAPON_ID_KATANA,
    WEAPON_ID_NAGINATA,
    WEAPON_ID_BROADSWORD,
    WEAPON_ID_LONG_SWORD,
    WEAPON_ID_NODACHI,

    WEAPON_ID_KATANA,
    WEAPON_ID_NAGINATA,
    WEAPON_ID_BROADSWORD,
    WEAPON_ID_LONG_SWORD,
    WEAPON_ID_NODACHI,

    WEAPON_ID_KATANA,
    WEAPON_ID_NAGINATA,
    WEAPON_ID_LONG_SWORD,
    WEAPON_ID_BROADSWORD,
    WEAPON_ID_NODACHI,

    WEAPON_ID_LONG_SWORD,
    WEAPON_ID_NAGINATA,
    0,
    0,
    0, /* spare */
};

/* and each character's stage. The characters a run never meets have the
 * first (the two crests are met, and have it too). */
/* clang-format off */
u8 g_vscpu_stages[CHARACTER_COUNT + 1] = {
    STAGE_BUS_STOP,           /* KANNUKI */
    STAGE_LOST_BRIDGE,        /* TATSUMI */
    STAGE_TATARA_SHRINE,      /* MIKADO */
    STAGE_MEIKYOKAN_DOJO,     /* UTSUSEMI */
    STAGE_PURPLE_CAVERN,      /* RED SHADOW */
    STAGE_FUTURE_DUMP_SITE,   /* TSUBAME */
    STAGE_SHAINTO_GATE,       /* SAZANKA */
    STAGE_TATARA_SHRINE_PATH, /* SUMINAGASHI */
    STAGE_OUTSIDE_MEIKYOKAN,  /* NIGHTSTALKER */
    STAGE_FERRY_DOCK,         /* MATSUMUSHI */
    STAGE_MAIN_TATARA_SHRINE, /* CHARACTER_VANISHER */
    STAGE_MAIN_TATARA_SHRINE, /* CHARACTER_SIDEARM_1 */
    STAGE_RAILWAY,            /* GENGORO */
    STAGE_ORANGE_TREE_FIELDS, /* KAUN */
    STAGE_LIGHTHOUSE,         /* JO */
    STAGE_CASTLE_ROOFTOP,     /* HONGOU */
    STAGE_BUILDING_ROOFTOP,   /* KATZE */
    STAGE_SHAINTO_CEMETERY,   /* HIGHWAYMAN */
    STAGE_ABANDONED_TUNNEL,   /* CHIHIRO */
    STAGE_ABANDONED_SHIPYARD, /* UTAMARU */
    STAGE_WATER_TOWER_DAM,    /* TONY UMEDA */
    STAGE_SHAINTO_HQ,         /* ISOHACHI */
    STAGE_MAIN_TATARA_SHRINE, /* CHARACTER_ARMORED */
    STAGE_MAIN_TATARA_SHRINE, /* CHARACTER_SIDEARM_2 */
    STAGE_MAIN_TATARA_SHRINE, /* CHARACTER_LAST_1 */
    STAGE_MAIN_TATARA_SHRINE, /* CHARACTER_VICTIM */
    STAGE_MAIN_TATARA_SHRINE, /* CHARACTER_LAST_2 */
    0, /* spare */
};
/* clang-format on */

/* Chanbara's hundred opponents (fight.h): the kind of each, which also
 * picks how the CPU plays it, and its colour (red, green, blue). A line is
 * ten of them: nine alike and a tenth of another kind and colour. The last
 * ten are each their own. */
#define FOE(kind, red, green, blue) {kind, red, green, blue}
#define NINE(foe) foe, foe, foe, foe, foe, foe, foe, foe, foe
/* clang-format off */
u8 g_chanbara_opponents[CHANBARA_OPPONENTS][4] = {
    NINE(FOE(0, 255, 64, 52)),  FOE(1, 224, 64, 96),
    NINE(FOE(0, 72, 207, 88)),  FOE(1, 64, 255, 80),
    NINE(FOE(0, 55, 48, 240)),  FOE(1, 48, 101, 208),
    NINE(FOE(0, 193, 1, 198)),  FOE(3, 192, 32, 207),
    NINE(FOE(0, 8, 8, 8)),      FOE(3, 16, 16, 16),
    NINE(FOE(1, 255, 199, 32)), FOE(2, 255, 167, 64),
    NINE(FOE(1, 128, 48, 144)), FOE(2, 255, 64, 255),
    NINE(FOE(1, 144, 255, 32)), FOE(4, 96, 255, 48),
    NINE(FOE(3, 255, 48, 128)), FOE(2, 255, 48, 48),
    /* the last ten */
    FOE(3, 32, 255, 255),  FOE(3, 255, 48, 112),  FOE(3, 112, 112, 32),
    FOE(3, 80, 32, 175),   FOE(3, 48, 48, 255),   FOE(3, 255, 255, 48),
    FOE(3, 48, 255, 48),   FOE(2, 144, 48, 144),  FOE(2, 255, 48, 48),
    FOE(4, 0, 0, 0),
};
/* clang-format on */
#undef FOE
#undef NINE

/* The roster entry of each kind of chanbara's opponents: whose character,
 * and so whose moves and strengths, the kind has under its own model
 * (g_chanbara_models). The last kind is a crest: it has the roster entry of
 * the player's clan's and the model of the other's. */
u8 g_chanbara_rosters[2][CHANBARA_KINDS] = {
    /* the player is of the Shainto */
    {ROSTER_RED_SHADOW, ROSTER_MIKADO_STORY, ROSTER_SUMINAGASHI,
     ROSTER_KANNUKI_STORY, ROSTER_LAST_2},
    /* the player is of the Narukagami */
    {ROSTER_RED_SHADOW, ROSTER_MIKADO_STORY, ROSTER_SUMINAGASHI,
     ROSTER_KANNUKI_STORY, ROSTER_LAST_1},
};

/* ---- the stages' ambient sounds ---- */

/* Per stage: its four ambient sounds, each an (enum StageAmbience, volume)
 * pair. A sound either runs all the time or comes back now and then, as
 * one of the kinds 2 to 6 of g_snd_ambient_timings. */
#define NONE AMBIENCE_NONE, 0
#define LOOP(volume) AMBIENCE_LOOP, volume
#define RANDOM(kind, volume) kind, volume
#define SILENT(volume) AMBIENCE_NONE, volume
/* clang-format off */
u8 g_snd_stage_ambience[STAGE_COUNT][8] = {
    /* STAGE_MAIN_TATARA_SHRINE */
    {LOOP(0x40),      NONE,            NONE,            NONE},
    /* STAGE_MEIKYOKAN_DOJO */
    {LOOP(0x40),      NONE,            NONE,            NONE},
    /* STAGE_SHAINTO_HQ */
    {LOOP(0x40),      NONE,            NONE,            NONE},
    /* STAGE_TATARA_SHRINE */
    {LOOP(0x50),      RANDOM(3, 0x40), RANDOM(3, 0x20), RANDOM(4, 0x7F)},
    /* STAGE_BUS_STOP */
    {RANDOM(3, 0x40), RANDOM(2, 0x60), NONE,            NONE},
    /* STAGE_LOST_BRIDGE */
    {RANDOM(3, 0x40), RANDOM(2, 0x60), LOOP(0x40),      NONE},
    /* STAGE_TATARA_SHRINE_PATH */
    {RANDOM(3, 0x40), RANDOM(2, 0x60), LOOP(0x40),      NONE},
    /* STAGE_BAMBOO_THICKET */
    {RANDOM(3, 0x40), LOOP(0x40),      NONE,            NONE},
    /* STAGE_OUTSIDE_MEIKYOKAN */
    {RANDOM(3, 0x40), RANDOM(2, 0x60), LOOP(0x20),      NONE},
    /* STAGE_FUTURE_DUMP_SITE */
    {RANDOM(3, 0x40), RANDOM(2, 0x60), LOOP(0x40),      RANDOM(2, 0x60)},
    /* STAGE_SNAKE_CLIFF */
    {LOOP(0x40),      RANDOM(2, 0x60), NONE,            NONE},
    /* STAGE_KIMIGAURA_COAST */
    {LOOP(0x7F),      NONE,            NONE,            NONE},
    /* STAGE_ORANGE_TREE_FIELDS */
    {LOOP(0x7F),      RANDOM(4, 0x7F), NONE,            NONE},
    /* STAGE_CASTLE_ROOFTOP */
    {RANDOM(2, 0x7F), RANDOM(4, 0x7F), LOOP(0x40),      RANDOM(4, 0x3F)},
    /* STAGE_UNDER_LOST_BRIDGE */
    {LOOP(0x40),      LOOP(0x40),      NONE,            NONE},
    /* STAGE_TERRACED_FIELDS */
    {LOOP(0x40),      RANDOM(4, 0x7F), NONE,            NONE},
    /* STAGE_UNDER_RAILWAY */
    {RANDOM(3, 0x40), RANDOM(2, 0x60), LOOP(0x30),      NONE},
    /* STAGE_WATER_TOWER_DAM */
    {LOOP(0x40),      NONE,            NONE,            NONE},
    /* STAGE_LUMBERYARD */
    {LOOP(0x40),      NONE,            NONE,            NONE},
    /* STAGE_LIGHTHOUSE */
    {LOOP(0x40),      RANDOM(4, 0x7F), NONE,            NONE},
    /* STAGE_KABUKI_STAGE */
    {LOOP(0x40),      RANDOM(4, 0x7F), NONE,            NONE},
    /* STAGE_ABANDONED_SHIP */
    {LOOP(0x40),      RANDOM(4, 0x05), NONE,            NONE},
    /* STAGE_FERRY_DOCK */
    {LOOP(0x40),      NONE,            NONE,            NONE},
    /* STAGE_SHAINTO_GATE. The original's data: a volume and no sound */
    {RANDOM(3, 0x60), NONE,            SILENT(0x40),    NONE},
    /* STAGE_PURPLE_CAVERN */
    {LOOP(0x40),      RANDOM(4, 0x10), NONE,            NONE},
    /* STAGE_ABANDONED_STATION */
    {RANDOM(3, 0x40), RANDOM(2, 0x60), LOOP(0x20),      NONE},
    /* STAGE_RAILWAY */
    {RANDOM(3, 0x40), RANDOM(2, 0x60), LOOP(0x40),      RANDOM(3, 0x40)},
    /* STAGE_ABANDONED_TUNNEL */
    {RANDOM(3, 0x40), LOOP(0x60),      RANDOM(6, 0x7F), RANDOM(3, 0x40)},
    /* STAGE_ABANDONED_SHIPYARD */
    {LOOP(0x40),      NONE,            NONE,            NONE},
    /* STAGE_BUILDING_ROOFTOP */
    {RANDOM(3, 0x40), RANDOM(2, 0x60), LOOP(0x40),      RANDOM(4, 0x50)},
    /* STAGE_BUILDING_PARKING_LOT */
    {LOOP(0x40),      RANDOM(3, 0x10), NONE,            NONE},
    /* STAGE_SHAINTO_CEMETERY */
    {RANDOM(3, 0x40), RANDOM(2, 0x60), LOOP(0x40),      RANDOM(3, 0x40)},
    /* STAGE_TEAM_1 */
    {NONE,            NONE,            NONE,            NONE},
    /* STAGE_TEAM_2 */
    {LOOP(0x40),      NONE,            NONE,            NONE},
    /* STAGE_CHANBARA */
    {NONE,            NONE,            NONE,            NONE},
    /* STAGE_PRACTICE_1 */
    {NONE,            NONE,            NONE,            NONE},
    /* STAGE_PRACTICE_2 */
    {NONE,            NONE,            NONE,            NONE},
    /* stage 37 */
    {NONE,            NONE,            NONE,            NONE},
};
/* clang-format on */
#undef NONE
#undef LOOP
#undef RANDOM
#undef SILENT

/* How often the now-and-then ambient sounds come back: the seconds a sound
 * stays quiet, and the seconds more after which it is certain. */
AmbientTiming g_snd_ambient_timings[5] = {
    {6, 9},   /* kind 2 */
    {15, 15}, /* kind 3 */
    {10, 13}, /* kind 4 */
    {10, 10}, /* kind 5: AMBIENCE_RANDOM_PLACED */
    {5, 15},  /* kind 6 */
};

/* Where the ambient sounds of kinds 5 and 6 are heard from. */
s32 g_snd_ambient_positions[2][4] = {{0, 0, 0, 0}, {190, 0, -10, 0}};

/* ---- endings and the kaishaku ---- */

/* Each ending's voice (enum FightEnding, two a kind): an audio file (enum
 * CdromAudioFile) and the channel of it. */
#define NO_VOICE (-1)
/* clang-format off */
s8 g_ending_voices[40][2] = {
    {CDROM_AUDIO_BB2_M3, 4},   /* ENDING_STORY */
    {CDROM_AUDIO_BB2_M3, 5},   /* ENDING_STORY + 1 */
    {CDROM_AUDIO_BB2_M3, 0},   /* ENDING_VSCPU */
    {CDROM_AUDIO_BB2_M3, 1},   /* ENDING_VSCPU + 1 */
    {CDROM_AUDIO_BB2_M3, 2},   /* ENDING_TEAM */
    {CDROM_AUDIO_BB2_M3, 3},   /* ENDING_TEAM + 1 */
    {NO_VOICE, 0},             /* ENDING_CHANBARA_FAST */
    {NO_VOICE, 0},             /* ENDING_CHANBARA_FAST + 1 */
    {NO_VOICE, 0},             /* ENDING_CHANBARA_CLEAN */
    {NO_VOICE, 0},             /* ENDING_CHANBARA_CLEAN + 1 */
    /* thirty more that no ending reads; what they were for is not known */
    {NO_VOICE, 0},  {0, 0},         {1, 7},         {1, 13},
    {2, 1},         {1, 2},         {1, 3},         {NO_VOICE, 0},
    {2, 0},         {1, 4},         {1, 5},         {1, 14},
    {NO_VOICE, 0},  {NO_VOICE, 3},  {1, 0},         {1, 1},
    {1, 6},         {1, 12},        {NO_VOICE, 0},  {1, 9},
    {1, 11},        {1, 10},        {1, 8},         {2, 2},
    {NO_VOICE, 0},  {NO_VOICE, 0},  {NO_VOICE, 0},  {NO_VOICE, 0},
    {NO_VOICE, 0},  {0, 0},
};
/* clang-format on */
#undef NO_VOICE

/* The kaishaku cutscene (fight_after.c): the frame at which the winner
 * speaks, for each character (-1: never: the characters without a name
 * say nothing). The table has a place for every roster entry and one more,
 * but is read by character. */
/* clang-format off */
s16 g_kaishaku_voice_frames[ROSTER_COUNT + 1] = {
    /* KANNUKI.. */
    130, 130, 130, 130, 130, 130, 130, 130, 130, 130,  -1,  -1,
    /* GENGORO.. */
    135, 230, 130, 130, 130, 130, 230, 130, 130, 130,  -1,  -1,
    /* the crests, the victim between them */
     -1,  -1,  -1,
    /* no character's */
    130,  -1,  -1, 130,  -1, 130,   0,
};
/* clang-format on */

/* The frames of the kaishaku's other sounds and of its blood, and where
 * the blood is from the first fighter's start position. */
KaishakuCues g_kaishaku_cues = {
    155,  /* swing_frame */
    159,  /* cut_frame */
    160,  /* cry_frame */
    198,  /* fall_frame */
    159,  /* blood_frame */
    0,    /* blood_x */
    -800, /* blood_y */
    0,    /* blood_z */
};

/* ---- odds and ends of the fight ---- */

/* For each column of chanbara's ladder, the two stance kinds its opponents
 * would rather begin in (motion_PickChanbaraStance). */
u8 g_motion_chanbara_stance_kinds[6][2] = {
    {1, 2}, {3, 1}, {1, 2}, {2, 1}, {2, 1}, {0, 0}};

/* A fighter's weapon model (enum WeaponModel): [weapon][clan], counted
 * from the first of each group. */
#define GUN(n) (WEAPON_MODEL_GUNS + (n))
/* clang-format off */
u8 g_fighter_clan_weapon_models[WEAPON_ID_COUNT][2] = {
    {0, 1},           /* WEAPON_ID_KATANA */
    {2, 3},           /* WEAPON_ID_LONG_SWORD */
    {4, 5},           /* WEAPON_ID_NODACHI */
    {6, 7},           /* WEAPON_ID_BROADSWORD */
    {8, 9},           /* WEAPON_ID_NAGINATA */
    {8, 9},           /* WEAPON_ID_YARI */
    {GUN(0), GUN(1)}, /* WEAPON_ID_LONG_GUN */
    {GUN(0), GUN(1)}, /* WEAPON_ID_SHORT_GUN */
};
/* clang-format on */
#undef GUN

/* The same in team battles and in practice against the ninja: one for
 * both clans. The guns have the katana's. */
#define PRACTICE(n) (WEAPON_MODEL_PRACTICE + (n))
u8 g_fighter_practice_weapon_models[WEAPON_ID_COUNT] = {
    PRACTICE(0), PRACTICE(1), PRACTICE(2), PRACTICE(3),
    PRACTICE(4), PRACTICE(5), PRACTICE(0), PRACTICE(0),
};
#undef PRACTICE

/* The heading a direction of the pad asks for, as an angle from straight
 * ahead: [forward, neither, back][one side, neither, the other]
 * (fighter_FindLedge, fighter_UpdateRun). */
/* clang-format off */
s16 g_fighter_pad_headings[3][3] = {
    {    -ANGLE_TURN / 8,              0,     ANGLE_TURN / 8},
    {    -ANGLE_TURN / 4,              0,     ANGLE_TURN / 4},
    {-3 * ANGLE_TURN / 8, ANGLE_TURN / 2, 3 * ANGLE_TURN / 8},
};
/* clang-format on */

/* Where a blade lock's flash is, for each kind of lock move the first
 * fighter can be in (fight_UpdateLock): how far towards the second
 * fighter from the middle, and how far up. */
LockFlashPlace g_fight_lock_flash_places[6] = {
    {0, -1280}, {-149, -680}, {-112, -1280},
    {0, -1280}, {112, -1280}, {149, -680}};

/* And the event that starts it. */
u8 g_fight_lock_flash_events[6] = {
    FIGHT_EVENT_LOCK,      FIGHT_EVENT_LOCK_FAST,   FIGHT_EVENT_LOCK_MEDIUM,
    FIGHT_EVENT_LOCK_SLOW, FIGHT_EVENT_LOCK_MEDIUM, FIGHT_EVENT_LOCK_FAST};

/* What a clash does to a fighter (fighter_TakeHit): [how far apart the two
 * fighters' powers are][0: it is not the stronger, 1: it is][whether the
 * two attacks are of one kind]. */
#define EVEN MOVE_EVENT_CLASH_EVEN
#define PUSHED MOVE_EVENT_CLASH_PUSHED
#define BEATEN MOVE_EVENT_CLASH_BEATEN
/* clang-format off */
u8 g_fight_clash_events[3][2][2] = {
    /* not the stronger  the stronger */
    {{PUSHED, BEATEN}, {EVEN, EVEN}},   /* far apart (POWER_GAP_LARGE) */
    {{EVEN,   PUSHED}, {EVEN, PUSHED}}, /* close (POWER_GAP_SMALL) */
    {{EVEN,   BEATEN}, {EVEN, PUSHED}}, /* between (POWER_GAP_MEDIUM) */
};
/* clang-format on */
#undef EVEN
#undef PUSHED
#undef BEATEN

/* The weapon model of each kind of loose object (enum ObjectKindId, enum
 * WeaponModel). */
#define SUB(n) (WEAPON_MODEL_SUB_WEAPONS + (n))
/* clang-format off */
u8 g_object_weapon_models[OBJECT_KIND_COUNT] = {
    /* the characters' sub-weapons: a model each */
    SUB(0),  SUB(1),  SUB(2),  SUB(3),  SUB(4),  SUB(5),
    SUB(6),  SUB(7),  SUB(8),  SUB(9),  SUB(10), SUB(11),
    SUB(12), SUB(13), SUB(14), SUB(15), SUB(16),
    WEAPON_MODEL_PRACTICE_SECOND, /* OBJECT_PRACTICE_SWORD */
    /* OBJECT_WEAPON_FIRST: the dropped weapons, as
     * g_fighter_clan_weapon_models has them (a dropped weapon is drawn as
     * the model its fighter had) */
    0, 1, 2, 3, 4, 5, 6, 7, 8, 9,
    WEAPON_MODEL_GUNS, WEAPON_MODEL_GUNS + 1,
    WEAPON_MODEL_OFF_HAND_GUNS,     /* OBJECT_OFF_HAND_GUN_1 */
    WEAPON_MODEL_OFF_HAND_GUNS + 1, /* OBJECT_OFF_HAND_GUN_2 */
};
/* clang-format on */
#undef SUB

/* How an object that glances off a fighter is turned, by the body point it
 * struck (object_GlanceOff): an angle, of which the low twelve bits are
 * used, so the ones from 0xF880 up are turns the other way. */
/* clang-format off */
u16 g_object_glance_angles[BODY_POINT_COUNT] = {
    0x0780,                           /* BODY_HEAD */
    0xF880, 0xFA00, 0x0600,           /* BODY_CHEST */
    0xFA80, 0x0580,                   /* BODY_HIP */
    0xFB00, 0xFB80, 0xFE00, 0xFE80,   /* BODY_OFF_ARM */
    0x0500, 0x0480, 0x0200, 0x0180,   /* BODY_SWORD_ARM */
    0xFD00, 0xFD80, 0xFF00, 0xFF80,   /* BODY_LEGS */
    0x0300, 0x0280, 0x0100, 0x0080,   /* BODY_SECOND_LEG */
};
/* clang-format on */

/* Chanbara: where each kind of opponent's voice is among the mode's
 * sounds, from the second fighter's own. */
s32 g_chanbara_voice_offsets[5] = {0, 51, 58, 65, 72};

/* The footprint each class of roster entry leaves (g_fighter_roster_classes,
 * eff_AddFootprint). */
s32 g_eff_footprint_shape_bits[5] = {
    EFF_FOOTPRINT_SHAPE(0), EFF_FOOTPRINT_SHAPE(2), EFF_FOOTPRINT_SHAPE(1),
    EFF_FOOTPRINT_SHAPE(3), EFF_FOOTPRINT_SHAPE(0),
};

/* The sounds of a prop that a weapon cuts down, for each of the six stages
 * that have one (snd_GetStagePropKind): the first of its three sounds,
 * which follow the stage's ambient sounds (g_snd_stage_ambience has one,
 * two or three for these stages), */
#define AFTER_AMBIENCE(count) (SND_STAGE_AMBIENT_FIRST + (count))
/* clang-format off */
u8 g_snd_prop_first_sounds[6] = {
    AFTER_AMBIENCE(1), /* STAGE_SHAINTO_HQ */
    AFTER_AMBIENCE(2), /* STAGE_BUS_STOP */
    AFTER_AMBIENCE(2), /* STAGE_BAMBOO_THICKET */
    AFTER_AMBIENCE(3), /* STAGE_OUTSIDE_MEIKYOKAN */
    AFTER_AMBIENCE(1), /* STAGE_LUMBERYARD */
    AFTER_AMBIENCE(2), /* STAGE_PURPLE_CAVERN */
};
/* clang-format on */
#undef AFTER_AMBIENCE

/* and the frames at which the second and the third play. */
/* clang-format off */
PropCueTicks g_snd_prop_cue_ticks[6] = {
    {46, 92}, /* STAGE_SHAINTO_HQ */
    {46, 98}, /* STAGE_BUS_STOP */
    {46, 98}, /* STAGE_BAMBOO_THICKET */
    {46, 46}, /* STAGE_OUTSIDE_MEIKYOKAN */
    {50, 98}, /* STAGE_LUMBERYARD */
    {46, 98}, /* STAGE_PURPLE_CAVERN */
};
/* clang-format on */

/* The names of the modes (enum FightMode). Nothing reads them. */
const char *g_fight_mode_names[7] = {
    g_str_story,    /* FIGHT_MODE_STORY */
    g_str_vscpu,    /* FIGHT_MODE_VSCPU */
    g_str_practice, /* FIGHT_MODE_PRACTICE */
    g_str_chanbara, /* FIGHT_MODE_CHANBARA */
    g_str_vs,       /* FIGHT_MODE_VS */
    g_str_team,     /* FIGHT_MODE_TEAM */
    g_str_network,  /* FIGHT_MODE_NETWORK */
};

/* The model of each kind of chanbara's opponents, in rows as
 * g_chanbara_rosters: four of the mode's own, then the Narukagami's
 * crest's model (ROSTER_LAST_1's) for a player of the Shainto and the
 * Shainto's crest's for a player of the Narukagami. */
u8 g_chanbara_models[2][CHANBARA_KINDS] = {
    {58, 59, 60, 61, 30}, {58, 59, 60, 61, 63}};

/* The team battle's time limits on offer, in seconds (0: none). */
u8 g_team_time_limits[4] = {30, 60, 120, 0};
