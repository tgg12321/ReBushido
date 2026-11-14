/* Character select's tables (select.c): its steps, the fighter grid, the
 * fighters' flags, and where things are drawn. They are apart from select.c
 * because that file is compiled with small data on, and these are not small
 * data in the original. */
#include "common.h"
#include "bb2.h"

/* What runs each frame of each step (enum SelectStep). */
void (*g_select_steps[SELECT_STEP_COUNT])(MenuDrawCtx *) = {
    select_FighterStep,    select_WeaponStep,     select_WeaponsInStep,
    select_WeaponsOutStep, select_StagePage1Step, select_StagePage2Step,
    select_StagePage3Step,
};

/* The weapon at each place of the weapon row, from the left. */
u8 g_select_weapon_ids[6] = {
    WEAPON_ID_NAGINATA, WEAPON_ID_BROADSWORD, WEAPON_ID_KATANA,
    WEAPON_ID_NODACHI,  WEAPON_ID_LONG_SWORD, WEAPON_ID_YARI,
};

/* The grid: five rows of four fighters, the Narukagami's two columns and
 * then the Shainto's, and in every row the same two more, the crests beside
 * the grid (the Shainto's first). A row is six lines here, a place a line:
 * the fighter of the screen, its character. */
/* clang-format off */
SelectCell g_select_grid[SELECT_GRID_ROWS][SELECT_GRID_COLS] = {
    {{SELECT_KANNUKI,      CHARACTER_KANNUKI},
     {SELECT_MATSUMUSHI,   CHARACTER_MATSUMUSHI},
     {SELECT_GENGORO,      CHARACTER_GENGORO},
     {SELECT_UTAMARU,      CHARACTER_UTAMARU},
     {SELECT_LAST_2,       CHARACTER_LAST_2},
     {SELECT_LAST_1,       CHARACTER_LAST_1}},

    {{SELECT_MIKADO,       CHARACTER_MIKADO},
     {SELECT_SUMINAGASHI,  CHARACTER_SUMINAGASHI},
     {SELECT_KAUN,         CHARACTER_KAUN},
     {SELECT_HONGOU,       CHARACTER_HONGOU},
     {SELECT_LAST_2,       CHARACTER_LAST_2},
     {SELECT_LAST_1,       CHARACTER_LAST_1}},

    {{SELECT_TATSUMI,      CHARACTER_TATSUMI},
     {SELECT_NIGHTSTALKER, CHARACTER_NIGHTSTALKER},
     {SELECT_JO,           CHARACTER_JO},
     {SELECT_TONY_UMEDA,   CHARACTER_TONY_UMEDA},
     {SELECT_LAST_2,       CHARACTER_LAST_2},
     {SELECT_LAST_1,       CHARACTER_LAST_1}},

    {{SELECT_UTSUSEMI,     CHARACTER_UTSUSEMI},
     {SELECT_SAZANKA,      CHARACTER_SAZANKA},
     {SELECT_CHIHIRO,      CHARACTER_CHIHIRO},
     {SELECT_HIGHWAYMAN,   CHARACTER_HIGHWAYMAN},
     {SELECT_LAST_2,       CHARACTER_LAST_2},
     {SELECT_LAST_1,       CHARACTER_LAST_1}},

    {{SELECT_RED_SHADOW,   CHARACTER_RED_SHADOW},
     {SELECT_TSUBAME,      CHARACTER_TSUBAME},
     {SELECT_ISOHACHI,     CHARACTER_ISOHACHI},
     {SELECT_KATZE,        CHARACTER_KATZE},
     {SELECT_LAST_2,       CHARACTER_LAST_2},
     {SELECT_LAST_1,       CHARACTER_LAST_1}},
};
/* clang-format on */

/* Each fighter of the screen's flags (SELECT_FIGHTER_...), by enum
 * SelectFighter, and two spare. As they start they say only which clan a
 * fighter is of: the first ten and the first crest are the Narukagami's. */
#define CLAN SELECT_FIGHTER_FIRST_CLAN
u8 g_select_fighter_flags[24] = {
    CLAN, CLAN, CLAN, CLAN, CLAN, CLAN, CLAN, CLAN, CLAN, CLAN, /* KANNUKI.. */
    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    /* GENGORO.. */
    CLAN, 0,                                                    /* the crests */
    0,    0,
};
#undef CLAN

/* Where each side's weapon is drawn when the two are crossed, from the
 * middle of the screen, by its place in the weapon row: x, y. The second
 * side's are the first's mirrored. */
/* clang-format off */
ScreenPoint g_select_weapon_places[2][6] = {
    {{-56, -31},  /* WEAPON_ID_NAGINATA */
     { -6, -14},  /* WEAPON_ID_BROADSWORD */
     {-26, -16},  /* WEAPON_ID_KATANA */
     {-68, -32},  /* WEAPON_ID_NODACHI */
     {-34, -32},  /* WEAPON_ID_LONG_SWORD */
     {-56, -31}}, /* WEAPON_ID_YARI */
    {{ 56, -31},
     {  6, -14},
     { 26, -16},
     { 68, -32},
     { 34, -32},
     { 56, -31}},
};
/* clang-format on */

/* How far each page of stages is scrolled across the picture of the
 * stages: x, y in pixels. */
s16 g_select_stage_scrolls[3][2] = {{0, 23}, {153, 3}, {160, 36}};

/* The scroll on its way from one page to the next, in thirtieths of a
 * pixel. */
s16 g_select_stage_scroll[2] = {0, 0};
