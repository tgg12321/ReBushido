#ifndef SELECT_H
#define SELECT_H

/* Character select (select.c): each side picks a fighter from a grid, then a
 * weapon from a row; the two weapons are shown crossed; in the two-player
 * modes a stage is picked last. Team battle has a character select of its
 * own (select_team.c), declared in the second half of this file. */

#include "common.h"
#include "disp.h"
#include "sprite.h"
#include "menu.h"

/* The fighters of the screen, in the screen's own order: the number picks
 * the name, the portrait, the voice and the flags. The names are the ones
 * the screen shows: SEL.BIN's fighter sheets have each of the first twenty
 * as a picture, in this order. The last two are the crests beside the grid,
 * which have no name. Team battle's select has the first twenty. */
enum SelectFighter {
    SELECT_KANNUKI = 0,
    SELECT_TATSUMI = 1,
    SELECT_MIKADO = 2,
    SELECT_UTSUSEMI = 3,
    SELECT_RED_SHADOW = 4,
    SELECT_TSUBAME = 5,
    SELECT_SAZANKA = 6,
    SELECT_SUMINAGASHI = 7,
    SELECT_NIGHTSTALKER = 8,
    SELECT_MATSUMUSHI = 9,
    SELECT_GENGORO = 10,
    SELECT_KAUN = 11,
    SELECT_JO = 12,
    SELECT_HONGOU = 13,
    SELECT_KATZE = 14,
    SELECT_HIGHWAYMAN = 15,
    SELECT_CHIHIRO = 16,
    SELECT_UTAMARU = 17,
    SELECT_TONY_UMEDA = 18,
    SELECT_ISOHACHI = 19,
    SELECT_LAST_1 = 20, /* the Narukagami's crest: CHARACTER_LAST_1 */
    SELECT_LAST_2 = 21, /* the Shainto's crest: CHARACTER_LAST_2 */
    SELECT_FIGHTERS = 22,
};

/* One place of the fighter grid: which of the screen's fighters is there
 * (enum SelectFighter) and the character that goes to the fight (enum
 * CharacterId). */
typedef struct SelectCell {
    u8 value;
    u8 fighter;
} SelectCell;

#define SELECT_GRID_ROWS 5
#define SELECT_GRID_COLS 6

/* g_select_fighter_flags, per fighter of the screen. */
#define SELECT_FIGHTER_OPEN 1 /* unlocked, and in this mode */
/* Of the Narukagami clan, not the Shainto: the first ten fighters and one
 * more (the left half of the grid, over the Narukagami crest). The clans
 * have different weapons on offer and different pictures of them, and the
 * one-player modes start on different stages. */
#define SELECT_FIGHTER_FIRST_CLAN 2
#define SELECT_FIGHTER_TAKEN 4 /* a cursor is on it */

/* The portraits: a set of cells for each side, then a sheet for every
 * fighter of the screen. Five fighters have a second sheet, drawn on the
 * right from pixels loaded when wanted (SelectFile.mirrored_portraits):
 * a portrait is mirrored on the right, and theirs must not be. */
typedef struct {
    PTR32(SpriteCell *) side_cells[2];
    s32 offset_list_terminator;
    SpriteSheet sheets[0];
} SelectPortraits;

/* SEL.BIN, SEL1.BIN or SEL2.BIN: character select's resource file, one for
 * the two-player modes, one for the one-player modes and one for story
 * mode. After the head:
 * - weapon_pixels: for each weapon two pictures, one a clan, loaded into
 *   video memory when the weapon is picked, at the side's weapon_rects;
 * - nine lists (select_RelocateFile): the portraits; weapon_sheets, the
 *   weapons' pictures as the crossed weapons are drawn; door_sheets, the
 *   sliding doors and the screen's frame; fighter_sheets, the grid;
 *   weapon_row_sheets, the row of weapons; the three pages of stages;
 *   cursor_sheets, the two sides' cursors, the mark left on a pick and the
 *   stages' names;
 * - stage_codes: for each page of stages eight bytes: the six stages, then
 *   for the left and right edge the page to go to and the door to use. */
typedef struct {
    SpriteFileHead head;
    s32 weapon_pixels[8][2];
    PTR32(SelectPortraits *) portraits;
    SpriteListSlot weapon_sheets;
    SpriteListSlot door_sheets;
    SpriteListSlot fighter_sheets;
    SpriteListSlot weapon_row_sheets;
    SpriteListSlot stage_page1_sheets;
    SpriteListSlot stage_page2_sheets;
    SpriteListSlot stage_page3_sheets;
    SpriteListSlot cursor_sheets;
    /* door_sheets' offset a second time, in all three files; it is not
     * made an address, and no code reads it */
    s32 door_sheets_offset;
    PTR32(RECT *) weapon_rects;
    PTR32(u8 *) stage_codes;
    s32 mirrored_portraits[5];
} SelectFile;

/* D_SEL.BIN, the resource file of team battle's select. After the head
 * come ten lists of sprite sheets (select_RelocateTeamFile):
 * - name_sheets: the two clans' emblems, then the twenty fighters' names
 *   and the line that strikes one out (enum TeamNameSheet, select_team.c);
 * - panel_sheets: a side's panel and the edge that moves when the panel
 *   changes pages (enum TeamPanelSheet);
 * - heading_sheets: the heading of each step after the rules;
 * - slot_sheets: the numbered places of a team, for three, four and five
 *   matches;
 * - clan_sheets: the choice of clan: a frame for each side, then each
 *   side's cursor;
 * - weapon_sheets: two sheets a weapon, then the cursor of the weapon step;
 * - rule_sheets: the rules page (enum TeamRuleSheet);
 * - frame_sheets: the three sheets behind everything.
 * bar_rects are the coloured bars behind one place of a team. */
typedef struct TeamSelectFile {
    SpriteFileHead head;
    SpriteListSlot name_sheets;
    SpriteListSlot panel_sheets;
    SpriteListSlot heading_sheets;
    SpriteListSlot slot_sheets[3];
    SpriteListSlot clan_sheets;
    SpriteListSlot weapon_sheets;
    SpriteListSlot rule_sheets;
    SpriteListSlot frame_sheets;
    PTR32(ColorRect *) bar_rects;
} TeamSelectFile;

/* What the draw functions of a 2D screen are handed each frame: the screen's
 * resource file and where the next primitive of each kind goes. Character
 * select and team battle's select use it; the narration leaves the file
 * unset. */
typedef struct {
    union {
        SelectFile *select;
        TeamSelectFile *team_select;
    } file;

    PrimCursors prims;
    void *tail;
} MenuDrawCtx;

/* The paper of the panels the two select screens are drawn on, in colour
 * and on a black and white monitor (which only the first asks about). */
#define SELECT_PAPER_R 0xD0
#define SELECT_PAPER_G 0xC8
#define SELECT_PAPER_B 0xB8
#define SELECT_PAPER_GRAY 0xC8

/* The steps of the screen (g_select_steps). */
enum SelectStep {
    SELECT_STEP_FIGHTER = 0,
    SELECT_STEP_WEAPON = 1,
    SELECT_STEP_WEAPONS_IN = 2,  /* the picked weapons come in and cross */
    SELECT_STEP_WEAPONS_OUT = 3, /* and the screen fades */
    SELECT_STEP_STAGE_1 = 4,     /* the three pages of stages */
    SELECT_STEP_STAGE_2 = 5,
    SELECT_STEP_STAGE_3 = 6,
    SELECT_STEP_COUNT
};

extern void select_FighterStep(MenuDrawCtx *ctx);
extern void select_WeaponStep(MenuDrawCtx *ctx);
extern void select_WeaponsInStep(MenuDrawCtx *ctx);
extern void select_WeaponsOutStep(MenuDrawCtx *ctx);
extern void select_StagePage1Step(MenuDrawCtx *ctx);
extern void select_StagePage2Step(MenuDrawCtx *ctx);
extern void select_StagePage3Step(MenuDrawCtx *ctx);
extern void (*g_select_steps[SELECT_STEP_COUNT])(MenuDrawCtx *);
extern SelectCell g_select_grid[SELECT_GRID_ROWS][SELECT_GRID_COLS];
extern u8 g_select_fighter_flags[24];
/* The weapon at each place of the weapon row. */
extern u8 g_select_weapon_ids[];
/* Where each side's weapon is drawn when the two are crossed, by its place
 * in the row. */
extern ScreenPoint g_select_weapon_places[2][6];
/* How far each page of stages is scrolled across the stages' picture, and
 * the scroll on its way from one page to the next (in thirtieths). */
extern s16 g_select_stage_scrolls[3][2];
extern s16 g_select_stage_scroll[2];

/* Loads the mode's file to `memory` and starts the screen. `extra_players`
 * is 1 when the second pad picks for the second side. `unlocks` has a bit
 * for each fighter of the screen that can be picked (select_SetUnlocks). */
extern s32 select_Load(
    s32 memory, s32 extra_players, GameSettings *settings, u32 unlocks);
/* A frame of the screen. Returns PAGE_CONFIRMED (menu.h) when everything is
 * picked (the picks are in the settings), PAGE_CANCELED when the screen is
 * left, PAGE_RUNNING otherwise. */
extern s32 select_Update(s32 held, s32 pressed);

/* The screen as the main menu runs it (menu_run.c). */
extern void select_SetUnlocks(s32 unlock_mask);
extern s32 select_Open(s32 memory);
extern s32 select_Run(s32 held, s32 pressed);

/* ---- team battle's select (select_team.c) ---- */

/* The fighters a team can be made of: ten to a clan. */
#define TEAM_FIGHTERS 20
#define TEAM_CLAN_FIGHTERS 10
/* A team is three to five fighters, one for each match. */
#define TEAM_MIN_MATCHES 3
#define TEAM_MAX_MATCHES 5
/* The weapons a clan has; a team uses each once. */
#define TEAM_WEAPONS 5

/* The steps of the screen. The rules are set by either pad for both sides;
 * from then on each side has a panel of its own. */
enum TeamStep {
    TEAM_STEP_RULES = 0,
    TEAM_STEP_CLAN = 1, /* one side takes a clan, the other gets the other */
    TEAM_STEP_FIGHTERS = 2, /* a fighter for each match, from the clan's ten */
    TEAM_STEP_WEAPONS = 3,  /* a weapon for each of them */
    TEAM_STEP_READY = 4,    /* waiting for the other side */
    TEAM_STEP_FADE = 5,     /* both are ready: the screen fades out */
};

/* The rows of the rules page. */
enum TeamRule {
    TEAM_RULE_MATCHES = 0, /* how many fighters a team has */
    /* points that win a match: one or two, which the page calls "1 point"
     * and "3 point" (the best of three) */
    TEAM_RULE_POINTS = 1,
    TEAM_RULE_TIME = 2, /* a match's time limit */
    TEAM_RULE_OK = 3,
    TEAM_RULE_EXIT = 4,
};

/* How a side's panel is changing pages. Going on, the old page slides up
 * and away and the new one is uncovered; going back, the page is covered
 * and the earlier one slides back down. */
enum TeamPanelMove {
    TEAM_PANEL_STILL = 0,
    TEAM_PANEL_SLIDE_AWAY = 1,
    TEAM_PANEL_REVEAL = 2,
    TEAM_PANEL_COVER = 3,
    TEAM_PANEL_SLIDE_BACK = 4,
};

/* Two values, one a side, that are also read as one word. */
typedef union {
    s16 half[2];
    s32 word;
} TeamPair;

/* The screen's work area, kept after the primitive pools. What is in twos
 * is one for each side.
 * - settings: where the picks go when the screen is done; file: D_SEL.BIN.
 * - panel_scroll, panel_cover: how far a panel's page is pushed up and how
 *   much of the panel is covered from below, in pixels; panel_move says
 *   which is changing; at the turn of a move the side goes to next_step
 *   with its cursor at next_cursor.
 * - fighter_column, fighter_row: the cursor on the grid of fighters.
 * - cursor: the row of the rules page, then the clan, then the place of
 *   the team being filled; nudge: the push the thing under it has.
 * - pulse_frames drives the cursors' glow; fade is how far the screen has
 *   faded out at the end.
 * - fighters: the team so far (fighters of the screen; the place being
 *   filled holds the one under the cursor, -1 past it). weapons: the weapon
 *   of each place; weapons_left the weapons not yet given out, in order,
 *   with a cursor and a count.
 * - the rules as they are being set; time_sheet is the sheet of the time
 *   limit showing (g_select_team_time_sheets), clan each side's clan. */
typedef struct {
    PTR32(GameSettings *) settings;
    PTR32(TeamSelectFile *) file;
    s16 panel_scroll[2];
    s16 panel_cover[2];
    TeamPair panel_move;
    TeamPair step;
    s16 next_step[2];
    TeamPair fighter_column;
    TeamPair fighter_row;
    PTR32(GpuDb *) db;
    u8 pad28[4];
    PTR32(PrimPool *) pool;
    s32 pool_frame;
    u16 pulse_frames;
    u16 fade;
    s16 next_cursor[2];
    s16 cursor[2];
    s16 nudge[2][2];
    s16 weapons_left[2][TEAM_WEAPONS];
    s16 weapon_cursor[2];
    s16 weapons_left_count[2];
    u8 max_match_count_index;
    u8 match_count_index;
    u8 time_sheet;
    u8 win_target_index;
    u8 clan[2];
    s16 fighters[2][TEAM_MAX_MATCHES];
    s16 weapons[2][TEAM_MAX_MATCHES];
} TeamSelect;

/* What each fighter of the screen is: SELECT_FIGHTER_OPEN,
 * SELECT_FIGHTER_FIRST_CLAN, and SELECT_FIGHTER_TAKEN for the first side,
 * twice that for the second. */
extern u8 g_select_team_fighter_flags[TEAM_FIGHTERS];
/* Each clan's grid of fighters: two columns of five, a column after the
 * other; `value` is the fighter of the screen at a place. `fighter` is not
 * about the place: see menu_data.c. */
extern SelectCell g_select_team_grid[2][TEAM_CLAN_FIGHTERS];
/* For one point a match and for two, the two time limits on offer, as
 * sheets of the rules page (counted from TEAM_RULE_SHEET_TIMES - 1). */
extern u8 g_select_team_time_sheets[2][2];

/* Loads D_SEL.BIN to `memory` and starts the screen at the rules. `unlocks`
 * has a bit for each of the twenty fighters that can be picked. */
extern s32 select_LoadTeam(s32 memory, GameSettings *settings, s32 unlocks);
/* A frame of the screen. Returns PAGE_CONFIRMED (menu.h) when both teams
 * are picked (they are in the settings), PAGE_CANCELED when the screen is
 * left, PAGE_RUNNING otherwise. */
extern s32 select_UpdateTeam(s32 held, s32 pressed);

/* The screen as the main menu runs it (menu_run.c). */
extern s32 select_OpenTeam(s32 memory);
extern s32 select_RunTeam(s32 held, s32 pressed);

/* Used only inside select_team.c. */

/* The screen's work. */
extern TeamSelect *g_select_team;

#endif /* SELECT_H */
