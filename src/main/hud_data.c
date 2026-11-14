/* The sheets and cells of what is drawn over the scene (hud.h): the fight's
 * counts and timers, the team battle's table, the captions, an ending's
 * picture, the kill count and the story's stage card. A sheet says where a
 * picture is in video memory, a cell which part of it goes where on the
 * screen. The panels' and the digits' are in hud.c.
 *
 * A sheet is {tp0, tp1, cells, unk3, cx, cy, ubase, vbase}: its texture
 * page's bits, how many cells it has, where its colour table is in video
 * memory, and the place in the page its cells' u and v are measured from.
 * A cell is {x, y, u, v, w, h}: where it goes on the screen, and the
 * rectangle of texture it shows.
 *
 * The file has no code. It is built without small data, which keeps its
 * single cells here among the others. */
#include "common.h"
#include "bb2.h"

/* ---- the counters over a fight ---- */

/* Where chanbara's two rules begin: the time's, then the count's. */
ScreenPoint g_hud_time_rule_origins[2] = {{418, 36}, {503, 55}};
/* The win counts: the two labels, right then left; then the two halves of
 * the piece at each rule's end, the right rule's and the left's. */
SpriteCell g_hud_win_count_cells[3][2] = {
    {{565, 23, 192, 28, 60, 14}, {15, 23, 192, 28, 60, 14}},
    {{568, 0, 192, 219, 35, 1}, {603, 0, 227, 219, 29, 1}},
    {{38, 0, 222, 218, 34, 1}, {8, 0, 192, 218, 30, 1}}};
SpriteCell g_hud_opponent_label_cell = {565, 23, 192, 42, 60, 14};

/* ---- the team battle's table ---- */

SpriteCell g_hud_team_strike_cells[2][2] = {
    {{242, 0, 192, 219, 35, 1}, {365, 0, 222, 218, 33, 1}},
    {{277, 0, 227, 219, 28, 1}, {335, 0, 192, 218, 30, 1}}};
SpriteSheet g_hud_team_unfought_sheet = {0x1E, 0x00, 1, 0x00, 896, 510, 0, 0};
SpriteCell g_hud_team_unfought_cells[5] = {
    {297, 0, 160, 0, 46, 12},
    {297, 0, 206, 0, 46, 12},
    {297, 0, 160, 12, 46, 12},
    {297, 0, 206, 12, 46, 12},
    {297, 0, 160, 24, 46, 12}};
SpriteSheet g_hud_team_result_sheet = {0x1E, 0x00, 1, 0x00, 896, 510, 0, 0};
SpriteSheet g_hud_team_mark_sheet = {0x1E, 0x00, 1, 0x00, 896, 511, 0, 0};
SpriteCell g_hud_team_won_cell = {272, 0, 160, 81, 26, 16};
SpriteCell g_hud_team_lost_cell = {272, 0, 160, 97, 26, 16};
SpriteCell g_hud_team_drawn_cell = {272, 0, 160, 113, 26, 16};
SpriteCell g_hud_team_center_cell = {312, 0, 160, 129, 16, 16};
SpriteCell g_hud_team_mark_cell = {0, 0, 186, 60, 20, 10};
SpriteSheet g_hud_team_title_0_sheet = {0x1E, 0x00, 3, 0x00, 896, 510, 0, 0};
SpriteSheet g_hud_team_subtitle_0_sheet = {
    0x1E, 0x00, 2, 0x00, 944, 511, 134, 160};
SpriteSheet g_hud_team_title_1_sheet = {0x1E, 0x00, 3, 0x00, 896, 510, 0, 32};
SpriteSheet g_hud_team_subtitle_1_sheet = {
    0x1E, 0x00, 2, 0x00, 944, 511, 134, 170};
SpriteCell g_hud_team_title_cells[3] = {{93, 28, 0, 160, 64, 32},
                                        {157, 28, 64, 160, 64, 32},
                                        {221, 28, 128, 160, 6, 32}};
SpriteCell g_hud_team_subtitle_0_cells[2] = {
    {101, 18, 0, 0, 64, 10}, {165, 18, 64, 0, 53, 10}};
SpriteCell g_hud_team_subtitle_1_cells[2] = {
    {123, 18, 0, 0, 64, 10}, {187, 18, 64, 0, 9, 10}};
/* Where each of the twenty names starts in its half of the table, in the
 * order of g_hud_name_cells. */
u8 g_hud_team_name_x_offsets[20] = {
    123, 123, 127, 117, 102, 119, 120, 100, 96,  103, /* KANNUKI.. */
    116, 136, 149, 121, 134, 99,  124, 118, 102, 119, /* GENGORO.. */
};

/* ---- points and the clock ---- */

SpriteSheet g_hud_point_sheets[2] = {{0x1E, 0x00, 1, 0x00, 928, 510, 0, 0},
                                     {0x1E, 0x00, 1, 0x00, 944, 510, 0, 0}};
SpriteCell g_hud_point_cells[2][2] = {
    {{81, 22, 186, 36, 28, 14}, {81, 22, 214, 36, 14, 14}},
    {{81, 46, 186, 50, 20, 10}, {81, 46, 206, 50, 12, 10}}};
SpriteCell g_hud_point_label_cells[2] = {
    {15, 23, 192, 28, 60, 14}, {15, 42, 192, 42, 64, 14}};
SpriteCell g_hud_time_separator_cell = {0, 0, 252, 0, 4, 5};
SpriteCell g_hud_point_rule_cells[2][2] = {
    {{568, 0, 192, 219, 35, 1}, {603, 0, 227, 219, 29, 1}},
    {{38, 0, 222, 218, 34, 1}, {8, 0, 192, 218, 30, 1}}};

/* ---- the captions (enum HudCaption) ---- */

SpriteSheet g_hud_caption_backing_sheets[3] = {
    {0x1F, 0x00, 1, 0x00, 960, 508, 0, 0},
    {0x1E, 0x00, 1, 0x00, 912, 510, 0, 0},
    {0x1E, 0x00, 1, 0x00, 912, 511, 0, 80}};
SpriteCell g_hud_caption_backing_cell = {160, 75, 0, 0, 159, 80};
SpriteSheet g_hud_caption_sheets[3] = {{0x1F, 0x00, 2, 0x00, 992, 510, 160, 56},
                                       {0x1E, 0x00, 1, 0x00, 928, 511, 0, 0},
                                       {0x1E, 0x00, 4, 0x00, 928, 511, 0, 0}};
SpriteCell g_hud_caption_rematch_cells[2] = {
    {230, 155, 0, 0, 64, 10}, {294, 155, 64, 0, 26, 10}};
SpriteCell g_hud_caption_ippon_cell = {263, 155, 134, 190, 56, 10};
SpriteCell g_hud_caption_out_cells[4] = {
    {165, 155, 134, 200, 64, 10},
    {229, 155, 198, 200, 52, 10},
    {281, 155, 178, 190, 12, 10},
    {293, 155, 199, 190, 27, 10}};

/* ---- an ending's picture ---- */

SpriteSheet g_hud_ending_sheets[2] = {{0x0E, 0x00, 8, 0x00, 992, 511, 0, 0},
                                      {0x0E, 0x00, 8, 0x00, 992, 511, 0, 128}};
SpriteCell g_hud_ending_cells[8] = {
    {64, 56, 0, 0, 64, 64},     {128, 56, 64, 0, 64, 64},
    {192, 56, 128, 0, 64, 64},  {256, 56, 192, 0, 64, 64},
    {320, 56, 0, 64, 64, 64},   {384, 56, 64, 64, 64, 64},
    {448, 56, 128, 64, 64, 64}, {512, 56, 192, 64, 64, 64}};

/* ---- the kill count ---- */

SpriteSheet g_hud_kill_count_sheets[2] = {
    {0x1E, 0x00, 3, 0x00, 944, 510, 0, 0},
    {0x1E, 0x00, 1, 0x00, 944, 510, 192, 40}};
SpriteCell g_hud_large_digit_cells[10] = {
    {0, 100, 0, 0, 32, 40},   {0, 100, 32, 0, 32, 40},
    {0, 100, 0, 40, 32, 40},  {0, 100, 32, 40, 32, 40},
    {0, 100, 0, 80, 32, 40},  {0, 100, 32, 80, 32, 40},
    {0, 100, 0, 120, 32, 40}, {0, 100, 32, 120, 32, 40},
    {0, 100, 0, 160, 32, 40}, {0, 100, 32, 160, 32, 40}};
SpriteCell g_hud_kill_count_cells[3] = {{0, 104, 0, 0, 192, 40},
                                        {192, 104, 192, 0, 64, 40},
                                        {256, 104, 0, 40, 136, 40}};

/* ---- the story's stage card ---- */

SpriteSheet g_hud_card_sheets[4] = {
    {0x1E, 0x00, 5, 0x00, 912, 510, 180, 0},
    {0x1E, 0x00, 4, 0x00, 912, 511, 128, 127},
    {0x1E, 0x00, 4, 0x00, 896, 511, 0, 127},
    {0x1E, 0x00, 1, 0x00, 896, 510, 0, 0}};
SpriteSheet g_hud_card_name_sheet = {0x1F, 0x00, 4, 0x00, 960, 511, 0, 128};
SpriteSheet g_hud_card_mark_sheets[3] = {
    {0x1E, 0x00, 1, 0x00, 928, 510, 180, 113},
    {0x1E, 0x00, 1, 0x00, 944, 510, 200, 113},
    {0x1E, 0x00, 1, 0x00, 928, 511, 220, 113}};
SpriteCell g_hud_card_map_cell = {65, 52, 0, 0, 179, 127};
SpriteCell g_hud_card_title_cells[5] = {
    {65, 30, 0, 0, 64, 16},
    {129, 30, 64, 0, 12, 16},
    {141, 30, 0, 16, 64, 16},
    {205, 30, 64, 16, 12, 16},
    {217, 30, 0, 32, 64, 16}};
SpriteCell g_hud_card_picture_cells[4] = {
    {447, 52, 0, 0, 64, 64},
    {511, 52, 64, 0, 64, 64},
    {447, 116, 0, 64, 64, 63},
    {511, 116, 64, 64, 64, 63}};
SpriteCell g_hud_card_name_cells[4] = {
    {319, 193, 0, 0, 64, 22},
    {383, 193, 64, 0, 64, 22},
    {447, 193, 128, 0, 64, 22},
    {511, 193, 192, 0, 64, 22}};
SpriteCell g_hud_card_rule_cells[2] = {
    {575, 214, 192, 219, 35, 1}, {610, 214, 227, 219, 29, 1}};

/* Where on the map each stage is, from the map's corner: x in the bits
 * from 7 up, y in the low seven. */
#define MARK(x, y) ((x) << HUD_CARD_MARK_Y_BITS | (y))
/* clang-format off */
u16 g_hud_card_mark_positions[32] = {
    MARK(323,  56), /* STAGE_MAIN_TATARA_SHRINE */
    MARK(337,  82), /* STAGE_MEIKYOKAN_DOJO */
    MARK( 34,  56), /* STAGE_SHAINTO_HQ */
    MARK(331,  54), /* STAGE_TATARA_SHRINE */
    MARK(300, 100), /* STAGE_BUS_STOP */
    MARK(294,  69), /* STAGE_LOST_BRIDGE */
    MARK(318,  66), /* STAGE_TATARA_SHRINE_PATH */
    MARK(259,  53), /* STAGE_BAMBOO_THICKET */
    MARK(326,  87), /* STAGE_OUTSIDE_MEIKYOKAN */
    MARK(207,  19), /* STAGE_FUTURE_DUMP_SITE */
    MARK(142,  51), /* STAGE_SNAKE_CLIFF */
    MARK(163,  25), /* STAGE_KIMIGAURA_COAST */
    MARK( 46,  85), /* STAGE_ORANGE_TREE_FIELDS */
    MARK(281,  62), /* STAGE_CASTLE_ROOFTOP */
    MARK(285,  76), /* STAGE_UNDER_LOST_BRIDGE */
    MARK(241, 102), /* STAGE_TERRACED_FIELDS */
    MARK(238,  80), /* STAGE_UNDER_RAILWAY */
    MARK(206,  60), /* STAGE_WATER_TOWER_DAM */
    MARK(161,  89), /* STAGE_LUMBERYARD */
    MARK(146,  70), /* STAGE_LIGHTHOUSE */
    MARK(145,  12), /* STAGE_KABUKI_STAGE */
    MARK( 95, 106), /* STAGE_ABANDONED_SHIP */
    MARK( 66,  70), /* STAGE_FERRY_DOCK */
    MARK( 38,  62), /* STAGE_SHAINTO_GATE */
    MARK(329,  25), /* STAGE_PURPLE_CAVERN */
    MARK(263,  20), /* STAGE_ABANDONED_STATION */
    MARK(244,  71), /* STAGE_RAILWAY */
    MARK(210,  90), /* STAGE_ABANDONED_TUNNEL */
    MARK(143, 116), /* STAGE_ABANDONED_SHIPYARD */
    MARK(181,  43), /* STAGE_BUILDING_ROOFTOP */
    MARK(176,  52), /* STAGE_BUILDING_PARKING_LOT */
    MARK( 25,  70), /* STAGE_SHAINTO_CEMETERY */
};
/* clang-format on */
#undef MARK
