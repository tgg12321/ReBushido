/* Tables of the menus that are not small data in the original, so apart
 * from the files that use them: team battle's select (select_team.c), the
 * settings (menu.h) and the top menu's rows (menu_run.c). */
#include "common.h"
#include "bb2.h"

/* What each fighter of team battle's select is (SELECT_FIGHTER_...), by
 * enum SelectFighter: the first ten are the first clan. */
#define CLAN SELECT_FIGHTER_FIRST_CLAN
u8 g_select_team_fighter_flags[TEAM_FIGHTERS] = {
    CLAN, CLAN, CLAN, CLAN, CLAN, CLAN, CLAN, CLAN, CLAN, CLAN, /* KANNUKI.. */
    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    /* GENGORO.. */
};
#undef CLAN

/* Each clan's grid: two columns of five, a column after the other. The
 * first of a pair is the fighter of the screen at that place. The second
 * is not about the place: counted through both clans it is, for each
 * fighter of the screen in turn, its character. */
/* clang-format off */
SelectCell g_select_team_grid[2][TEAM_CLAN_FIGHTERS] = {
    /* the Narukagami's left column */
    {{SELECT_KANNUKI,      CHARACTER_KANNUKI},
     {SELECT_MIKADO,       CHARACTER_TATSUMI},
     {SELECT_TATSUMI,      CHARACTER_MIKADO},
     {SELECT_UTSUSEMI,     CHARACTER_UTSUSEMI},
     {SELECT_RED_SHADOW,   CHARACTER_RED_SHADOW},
     /* and right column */
     {SELECT_MATSUMUSHI,   CHARACTER_TSUBAME},
     {SELECT_SUMINAGASHI,  CHARACTER_SAZANKA},
     {SELECT_NIGHTSTALKER, CHARACTER_SUMINAGASHI},
     {SELECT_SAZANKA,      CHARACTER_NIGHTSTALKER},
     {SELECT_TSUBAME,      CHARACTER_MATSUMUSHI}},
    /* the Shainto's left column */
    {{SELECT_GENGORO,      CHARACTER_GENGORO},
     {SELECT_KAUN,         CHARACTER_KAUN},
     {SELECT_JO,           CHARACTER_JO},
     {SELECT_CHIHIRO,      CHARACTER_HONGOU},
     {SELECT_ISOHACHI,     CHARACTER_KATZE},
     /* and right column */
     {SELECT_UTAMARU,      CHARACTER_HIGHWAYMAN},
     {SELECT_HONGOU,       CHARACTER_CHIHIRO},
     {SELECT_TONY_UMEDA,   CHARACTER_UTAMARU},
     {SELECT_HIGHWAYMAN,   CHARACTER_TONY_UMEDA},
     {SELECT_KATZE,        CHARACTER_ISOHACHI}},
};
/* clang-format on */

/* For one point a match and for two, the two time limits on offer, as
 * sheets of the rules page counted from the one before its first time
 * (TEAM_RULE_SHEET_TIMES - 1). */
u8 g_select_team_time_sheets[2][2] = {{1, 2}, {3, 4}};

/* The settings the menus edit (menu.h), as they start: each action on one
 * button for both players, the "Difficulty" option at easy, and best times
 * of 59 minutes. */
#define DEFAULT_BUTTONS                                                        \
    {PAD_CONFIG_BOTH(PAD_CONFIG_CIRCLE), PAD_CONFIG_BOTH(PAD_CONFIG_CROSS),    \
     PAD_CONFIG_BOTH(PAD_CONFIG_SQUARE)}
#define NO_TIME {59, 0, 0, CHARACTER_KANNUKI}

GameSettings g_game_settings = {
    /* match_picks */
    {{{CHARACTER_KANNUKI, WEAPON_ID_KATANA},
      {CHARACTER_KANNUKI, WEAPON_ID_KATANA},
      {CHARACTER_KANNUKI, WEAPON_ID_KATANA},
      {CHARACTER_KANNUKI, WEAPON_ID_KATANA},
      {CHARACTER_KANNUKI, WEAPON_ID_KATANA}},
     {{CHARACTER_KANNUKI, WEAPON_ID_KATANA},
      {CHARACTER_KANNUKI, WEAPON_ID_KATANA},
      {CHARACTER_KANNUKI, WEAPON_ID_KATANA},
      {CHARACTER_KANNUKI, WEAPON_ID_KATANA},
      {CHARACTER_KANNUKI, WEAPON_ID_KATANA}}},
    FIGHT_MODE_STORY,            /* menu_mode: the top menu's first row */
    0,                           /* stage_index */
    0,                           /* match_count_index: three matches */
    0,                           /* time_limit_index */
    0,                           /* win_target_index: one point */
    0,                           /* team_clan_bits */
    0,                           /* practice_round */
    0,                           /* toggle_option_bits */
    0,                           /* pad14_21 */
    DEFAULT_BUTTONS,             /* button_mappings */
    DEFAULT_BUTTONS,             /* default_button_mappings */
    DEFAULT_BUTTONS,             /* committed_button_mappings */
    0,                           /* grayscale */
    1,                           /* easy_cpu */
    0,                           /* music_off */
    0,                           /* first_person */
    0,                           /* pad20_4 */
    {NO_TIME, NO_TIME, NO_TIME}, /* time_records */
    {0, 0, 0},                   /* fight_time */
    CHARACTER_KANNUKI,           /* fight_character_index */
};

#undef DEFAULT_BUTTONS
#undef NO_TIME

/* For each row of the top menu as the settings keep it: the mode it starts
 * (the last row is the options), and 1 if the mode's character select is
 * for two pads. */
/* clang-format off */
u8 g_menu_row_modes[8][2] = {
    /* mode,              two pads */
    {FIGHT_MODE_STORY,    0},
    {FIGHT_MODE_VSCPU,    0},
    {FIGHT_MODE_PRACTICE, 0},
    {FIGHT_MODE_CHANBARA, 0},
    {FIGHT_MODE_VS,       1},
    {FIGHT_MODE_TEAM,     1},
    {FIGHT_MODE_NETWORK,  1},
    {MENU_ROW_OPTIONS,    0},
};
/* clang-format on */
