#ifndef HUD_H
#define HUD_H

/* What is drawn flat over the 3D scene (hud.c): the panel of the pause menu
 * and of the menu after a fight, the counters and timers of the modes, the
 * captions, the team battle's table of scores, the best-times table, story
 * mode's stage card, an ending's picture, and the picture shown while a
 * fight loads.
 *
 * Each of the drawing functions is handed the address where its primitives
 * are to be built and the entry of the display's ordering table to link them
 * into, and returns how many bytes of primitives it may have used; the
 * caller steps its cursor on by that. The Hud...Prims types below are those
 * blocks of primitives. */

#include "common.h"
#include <psxsdk/libgpu.h>
#include "sprite.h"

/* A time split up for display. The fight's timer counts frames, 30 a
 * second. */
typedef struct ClockTime {
    s16 time_minutes;
    u8 time_seconds;
    u8 time_hundredths;
} ClockTime;

/* ---- the panel ---- */

/* The three panels hud_DrawPanel draws. */
enum HudPanel {
    /* "pause": continue, to character select, to the main menu */
    HUD_PANEL_PAUSE = 0,
    /* after a fight: again, to character select, to the main menu */
    HUD_PANEL_AFTER_FIGHT = 1,
    /* the pause panel with three on/off options under it */
    HUD_PANEL_PAUSE_OPTIONS = 2,
};

/* The primitives of hud_DrawPanel. */
typedef struct {
    TILE tiles[15];
    SPRT sprites[50];
    DR_MODE draw_mode;
    u8 unused[0xC];
} HudPanelPrims;

/* Draws a panel in the middle of the screen with the row at the cursor
 * lit. `cursor_and_options` is the cursor's row in the low half; for
 * HUD_PANEL_PAUSE_OPTIONS bits 16 up are the options' settings. */
extern s32 hud_DrawPanel(
    s32 panel, s32 cursor_and_options, s32 prims, s32 ot_index);

/* ---- counters and timers, at the top of the screen ---- */

/* The primitives of hud_DrawTimeAndCount, hud_DrawWinCounts and
 * hud_DrawPointsAndTime. */
typedef struct {
    TILE tiles[10];
    SPRT sprites[30];
    DR_MODE draw_mode;
} HudCounterPrims;

/* Chanbara: the time taken so far, and the opponents before the one now
 * faced (the count it is given, less one). */
extern s32 hud_DrawTimeAndCount(
    ClockTime *time, s32 count, s32 prims, s32 ot_index);
/* How many fights each side has won, one on the left and one on the right.
 * With `left_count` negative only the right one is drawn, under another label.
 */
extern s32 hud_DrawWinCounts(
    s32 left_count, s32 right_count, s32 prims, s32 ot_index);
/* Against the CPU: the opponents before the one now faced, likewise. */
extern s32 hud_DrawOpponentCount(s32 count, s32 prims, s32 ot_index);
/* The team battle's match: each side's points and warnings (fight.h) as
 * marks,
 * and the time left. `packed_marks` has the first side's points in bits 0-3
 * and the second's in bits 4-7, then their warnings in bits 8-11 and 12-15.
 * Uses one more DR_MODE than the size it returns. */
extern s32 hud_DrawPointsAndTime(
    ClockTime *time, s32 packed_marks, s32 prims, s32 ot_index);

/* ---- captions ---- */

/* The captions hud_DrawCaption can put up, stretched to double width. */
enum HudCaption {
    HUD_CAPTION_REMATCH = 0, /* "REMATCH": chosen after a fight */
    HUD_CAPTION_IPPON = 1,   /* "IPPON": the team battle, a point is given */
    HUD_CAPTION_OUT_OF_BOUNDS = 2, /* "OUT OF BOUNDS" */
};

extern s32 hud_DrawCaption(s32 caption, s32 prims, s32 ot_index);

/* ---- the team battle's scores ---- */

/* The primitives of hud_DrawTeamScores. */
typedef struct {
    TILE tiles[10];
    SPRT sprites[102];
    POLY_FT4 quads[20];
    DR_MODE draw_mode;
} HudTeamScorePrims;

/* The table of the team battle: a row for each match with the two
 * fighters' names, who won and by how many points, and the totals under
 * them. `packed_scores` has four bits a match, two for each side's points;
 * 3 means the match has not been fought. Uses one more DR_MODE than the
 * size it returns. */
extern s32 hud_DrawTeamScores(u32 packed_scores, s32 prims, s32 ot_index);

/* ---- an ending's picture ---- */

/* The primitives of hud_DrawEndingPicture. */
typedef struct {
    SPRT sprites[32];
    DR_MODE draw_mode;
    POLY_G4 gradients[2];
    DR_AREA areas[3];
} HudEndingPrims;

/* Reads picture `index` from the disc into video memory, using
 * `file_buffer`, and starts its timer. */
extern void hud_LoadEndingPicture(s32 index, u8 *file_buffer);
/* Draws the picture over the scene. After six seconds it parts in the
 * middle: the halves slide out to the sides, each with a red edge. */
extern s32 hud_DrawEndingPicture(s32 prims, s32 ot_index);

/* ---- the count of opponents beaten ---- */

/* The primitives of hud_DrawKillCount. */
typedef struct {
    SPRT sprites[9];
    DR_MODE draw_mode;
} HudKillCountPrims;

extern s32 hud_DrawKillCount(s32 count, s32 prims, s32 ot_index);

/* ---- story mode's stage card ---- */

/* The primitives of hud_DrawStageCardMark and of hud_DrawStageCard. */
typedef struct {
    SPRT sprite;
    DR_MODE draw_mode;
    u8 unused[0xC];
} HudCardMarkPrims;

typedef struct {
    SPRT sprites[63];
    POLY_FT4 quads[6];
    DR_MODE draw_mode;
    u8 unused[0xC];
} HudStageCardPrims;

/* Reads card `index` from the disc: the stage's name and its picture. */
extern void hud_LoadStageCard(s32 index, s32 *file_buffer);
/* The card: its title, the stage's picture and name, the map. */
extern s32 hud_DrawStageCard(s32 prims, s32 ot_index);

/* One mark on the card's map, at place `place` of the table of places; with
 * HUD_CARD_MARK_LAST it is the last one, which has a mark of its own. */
#define HUD_CARD_MARK_LAST 0x8000
extern s32 hud_DrawStageCardMark(s32 place, s32 prims, s32 ot_index);

/* ---- the best times ---- */

/* The table's rows, and how far apart they are on the screen. */
#define BEST_TIMES_ROWS 3
#define BEST_TIMES_ROW_PITCH 26

/* The characters, for the tables of names (the best times and the team
 * battle's scores): the Narukagami's ten, two who have no name (the
 * vanisher and the Narukagami's ninja), then the Shainto's ten.
 * NIGHTSTALKER's name is too long for a name's three cells and has three
 * more. */
#define FIGHTERS_ON_SELECT (CHARACTER_ISOHACHI + 1)
#define FIGHTERS_FIRST_TWELVE CHARACTER_GENGORO
#define FIGHTERS_UNNAMED 2
#define FIGHTER_LONG_NAME CHARACTER_NIGHTSTALKER

/* The sheets a table of best times is drawn from. The main menu's records
 * page draws the same table from its own resource file. */
typedef struct {
    SpriteSheetSlot own_cell_sheets[3]; /* the title, the ranks, the marks */
    SpriteSheetSlot character_sheet;    /* the characters' names */
    PTR32(SpriteCell *)
    character_cells; /* three cells a name, after three more */
    SpriteSheetSlot digit_sheet;
    PTR32(SpriteCell *) digit_cell;
    PTR32(s16 *) character_x_offsets; /* where each name starts */
} BestTimesSheets;

extern void hud_ResetBestTimes(void);
/* The table after a chanbara run, a bar growing under it and another under
 * row `rank` if the run made the table. */
extern s32 hud_DrawBestTimes(s32 prims, s32 ot_index, s32 rank);

/* ---- the loading screen ---- */

/* The picture: `screen` is enum LoadingScreen's plain picture or the next
 * opponent. Returns where the next primitive goes. */
extern s32 hud_DrawLoadingPicture(s32 prims, s32 screen);
/* A burst of streaks of noise over it, somewhere at random. */
extern s32 hud_DrawLoadingNoise(s32 prims, s32 screen);

/* The counters' four sheets: the digits, the labels, and the two halves of
 * the red rule's end. */
extern ScreenPoint g_hud_time_rule_origins[2];
extern SpriteCell g_hud_win_count_cells[3][2];
extern SpriteCell g_hud_opponent_label_cell;
extern SpriteSheet g_hud_point_sheets[2];
extern SpriteCell g_hud_point_cells[2][2];
extern SpriteCell g_hud_point_label_cells[2];
extern SpriteCell g_hud_point_rule_cells[2][2];
extern SpriteCell g_hud_time_separator_cell;

extern SpriteSheet g_hud_team_unfought_sheet;
extern SpriteCell g_hud_team_unfought_cells[5];
extern SpriteSheet g_hud_team_result_sheet;
extern SpriteSheet g_hud_team_mark_sheet;
extern SpriteCell g_hud_team_won_cell;
extern SpriteCell g_hud_team_lost_cell;
extern SpriteCell g_hud_team_drawn_cell;
extern SpriteCell g_hud_team_center_cell;
extern SpriteCell g_hud_team_mark_cell;
extern SpriteSheet g_hud_team_title_0_sheet;
extern SpriteSheet g_hud_team_subtitle_0_sheet;
extern SpriteSheet g_hud_team_title_1_sheet;
extern SpriteSheet g_hud_team_subtitle_1_sheet;
extern SpriteCell g_hud_team_title_cells[3];
extern SpriteCell g_hud_team_subtitle_0_cells[2];
extern SpriteCell g_hud_team_subtitle_1_cells[2];
extern u8 g_hud_team_name_x_offsets[20];
extern SpriteCell g_hud_team_strike_cells[2][2];

/* By enum HudCaption: the caption and the band it stands on. */
extern SpriteSheet g_hud_caption_sheets[3];
extern SpriteCell g_hud_caption_rematch_cells[2];
extern SpriteCell g_hud_caption_ippon_cell;
extern SpriteCell g_hud_caption_out_cells[4];
extern SpriteSheet g_hud_caption_backing_sheets[3];
extern SpriteCell g_hud_caption_backing_cell;

extern SpriteSheet g_hud_ending_sheets[2];
extern SpriteCell g_hud_ending_cells[8];

extern SpriteSheet g_hud_kill_count_sheets[2];
extern SpriteCell g_hud_large_digit_cells[10];
extern SpriteCell g_hud_kill_count_cells[3];

extern SpriteSheet g_hud_card_sheets[4];
extern SpriteSheet g_hud_card_name_sheet;
extern SpriteCell g_hud_card_map_cell;
extern SpriteCell g_hud_card_title_cells[5];
extern SpriteCell g_hud_card_picture_cells[4];
extern SpriteCell g_hud_card_name_cells[4];
extern SpriteCell g_hud_card_rule_cells[2];
extern SpriteSheet g_hud_card_mark_sheets[3];
/* Where on the map each place is, from the map's corner: y in the low
 * HUD_CARD_MARK_Y_BITS bits, x in the bits over them. */
#define HUD_CARD_MARK_Y_BITS 7
extern u16 g_hud_card_mark_positions[32];

#endif /* HUD_H */
