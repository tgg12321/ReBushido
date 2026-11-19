/* The character select and the sprite sheets it and the other flat screens
 * are drawn with. One file of the original's: sprite_Draw's "warning" text
 * lies before select_Load's jump table (a file's strings come before its
 * jump tables), with select's functions between the two in the code. */
#define INCLUDE_ASM_USE_MACRO_INC 1
#include "common.h"
#include "include_asm.h"
#include "bb2.h"

/* Defined further down. */
static s32 select_RelocateFile(SelectFile *file);
static void select_DrawBannerLines(MenuDrawCtx *ctx);
static s32 sprite_DrawRotated();

/* Defined further down. */
static u8 g_select_portrait_loaded;
static u8 g_select_portrait_extra;
static RECT g_select_palette_rect;
static RECT g_select_portrait_rect;
static void select_StepDoor(void);
static void select_DrawDoors(MenuDrawCtx *ctx);
static void select_DrawBanner(MenuDrawCtx *ctx);
static void select_DrawFrame(MenuDrawCtx *ctx);
static void select_MoveFighterCursors(MenuDrawCtx *ctx);
static void select_MoveWeaponCursors(MenuDrawCtx *ctx, SpriteDraw *draw);
static void select_StageStep(
    MenuDrawCtx *ctx, SpriteSheetSlot *sheets, s32 page);
static VECTOR g_sprite_rotation_scale;

/* ---- select ---- */

/* Character select, drawn from SEL.BIN (or SEL1.BIN, SEL2.BIN: a file for
 * the two-player modes, one for the one-player modes, one for story mode).
 *
 * The screen goes through steps (enum SelectStep): each side moves a cursor
 * over the grid of fighters and picks one; then over the row of weapons; the
 * two weapons fly in and cross; in versus and link mode a stage is picked
 * from three pages of them. Between steps two doors slide shut across the
 * panel and open again on the next one. Along the top is a banner with the
 * screen's title, and behind it the portrait of the fighter each cursor is
 * on.
 *
 * There are two sides, and a pick for each. In the two-player modes each
 * pad picks its own side's. In training mode the first pad picks both, one
 * after the other: in a "Practice Round" the opponent's fighter after its
 * own (g_select_extra_fighters), and in either kind the opponent's weapon
 * after its own (g_select_extra_weapons).
 *
 * The file begins with the two functions that load any 2D screen's resource
 * file. */

/* No portrait of the second side's is loaded. */
#define SELECT_PORTRAIT_NONE 0x7F

/* A pick not made yet. */
#define SELECT_NONE 0xFF

/* The two fighters that come with their weapon, a gun: the weapon row is
 * skipped for them. */
#define SELECT_FIGHTER_FIXED_1 CHARACTER_TSUBAME
#define SELECT_FIGHTER_FIXED_2 CHARACTER_KATZE
#define SELECT_WEAPON_FIXED_1 WEAPON_ID_LONG_GUN
#define SELECT_WEAPON_FIXED_2 WEAPON_ID_SHORT_GUN

/* The grid: four columns of names, then the two emblems. */
#define SELECT_GRID_NAME_COLS 4
#define SELECT_COL_EMBLEM_1 4
#define SELECT_COL_EMBLEM_2 5
/* The two places the one-player modes leave out: the last row's, in these
 * columns. */
#define SELECT_ROW_LEFT_OUT (SELECT_GRID_ROWS - 1)
#define SELECT_COL_LEFT_OUT_1 1
#define SELECT_COL_LEFT_OUT_2 3
/* Where the second side's cursor starts on the grid. */
#define SELECT_SECOND_START_COL 2

/* The weapon row: six places; a cursor starts in the middle. A clan has
 * the first five or the last five. */
#define SELECT_WEAPON_PLACES 6
#define SELECT_WEAPON_START 2

/* The doors (g_select_door). A door closes, the step changes behind it, and
 * it opens: the right door on the way forward, the left one on the way
 * back. */
enum SelectDoor {
    SELECT_DOOR_OPEN = 0,
    SELECT_DOOR_RIGHT_CLOSING = 1,
    SELECT_DOOR_RIGHT_OPENING = 2,
    SELECT_DOOR_LEFT_CLOSING = 3,
    SELECT_DOOR_LEFT_OPENING = 4,
    /* added to a closing door: it stays shut */
    SELECT_DOOR_STAYS_SHUT = 0x100,
};

#define SELECT_DOOR_STATE 0xFF /* g_select_door without what is added */
#define SELECT_DOOR_SHUT_X 488 /* how far a door slides */
#define SELECT_DOOR_SPEED 32   /* a frame */

/* The files in the disc's data group. */
#define SELECT_FILE_TWO_PLAYERS CDROM_DATA_SEL_BIN
#define SELECT_FILE_ONE_PLAYER CDROM_DATA_SEL1_BIN
#define SELECT_FILE_STORY CDROM_DATA_SEL2_BIN

/* Frames a pick is shown before the screen moves on. */
#define SELECT_PICK_FRAMES 30
/* The mark left on a pick is drawn a cell more each frame. */
#define SELECT_STAMP_CELLS 12
/* Frames the two weapons take to fly in; the first starts nearer, and both
 * come from as far as the second. The sound of the flight is played with
 * this many frames to go. */
#define SELECT_WEAPON_FLY_1 15
#define SELECT_WEAPON_FLY_2 20
#define SELECT_WEAPON_FLY_SOUND 10
/* Frames the crossed weapons are up before the screen fades. */
#define SELECT_WEAPONS_FRAMES 120
/* g_select_fade: the screen darkens behind the weapon row this much a
 * frame, and behind the crossed weapons this much, up to FADE_FULL. */
#define FADE_IN_STEP 16
#define FADE_OUT_STEP 8
#define FADE_FULL 0xFF

/* The glow of a cursor: the brightness swings with a sine, SELECT_PULSE_DEPTH
 * either side of SELECT_PULSE_CENTER (SPRITE_GLOW). `speed` is how fast:
 * once round in SELECT_PULSE_FRAMES frames while choosing, four times on a
 * pick. `phase` sets the two sides apart: the second starts an eighth of a
 * turn round, less one. */
#define SELECT_PULSE_CHOOSING 7
#define SELECT_PULSE_PICKED 9
#define SELECT_PULSE_FRAMES 32
#define SELECT_PULSE_DEPTH 63
#define SELECT_PULSE_CENTER 0xC0
#define SELECT_PULSE_SIDE_PHASE (ANGLE_TURN / 8 - 1)
#define SELECT_PULSE(speed, phase)                                             \
    SPRITE_GLOW(rsin(((g_select_work->frames & (SELECT_PULSE_FRAMES - 1))      \
                      << (speed)) +                                            \
                     (phase)),                                                 \
                SELECT_PULSE_DEPTH, SELECT_PULSE_CENTER)

/* The banner, the frame's corners and a portrait that can be picked change
 * between two looks every FLICKER_FRAMES frames. */
#define FLICKER_FRAMES 4

/* An emblem a cursor is on sways: five pixels across, three times round in
 * 64 frames, and three up and down, once round in 32 (the angles in steps
 * of a 64th and a 32nd of a turn). While its pick is shown it bobs as well,
 * by as many pixels as frames of the showing are left, nine times round in
 * 128 frames. */
#define EMBLEM_SWAY_X                                                          \
    ((rsin((g_select_work->frames * (3 * ANGLE_TURN / 64)) &                   \
           (ANGLE_MASK & ~(ANGLE_TURN / 64 - 1))) *                            \
      5) >>                                                                    \
     12)
#define EMBLEM_SWAY_Y                                                          \
    ((rcos((g_select_work->frames << 7) &                                      \
           (ANGLE_MASK & ~(ANGLE_TURN / 32 - 1))) *                            \
      3) >>                                                                    \
     12)
#define EMBLEM_BOB_ANGLE                                                       \
    ((g_select_work->frames * (9 * ANGLE_TURN / 128)) &                        \
     (ANGLE_MASK & ~(ANGLE_TURN / 128 - 1)))

/* The five fighters of the screen with a portrait of their own for the
 * right, in the order of SelectFile.mirrored_portraits. */
enum {
    SELECT_OWN_PORTRAIT_1 = SELECT_JO,
    SELECT_OWN_PORTRAIT_2 = SELECT_HONGOU,
    SELECT_OWN_PORTRAIT_3 = SELECT_KATZE,
    SELECT_OWN_PORTRAIT_4 = SELECT_KANNUKI,
    SELECT_OWN_PORTRAIT_5 = SELECT_UTSUSEMI,
};

/* A page of stages: three rows of two. SelectFile.stage_codes has eight
 * bytes a page: the six stages, then the left and the right edge: the step
 * to go to in the low four bits (SELECT_STAGE_EDGE_NONE: nowhere) and the door
 * to use, as g_select_door, in the rest. */
#define SELECT_STAGE_PAGE_ROWS 3
#define SELECT_STAGE_PAGE_COLS 2
#define SELECT_STAGE_PAGE_PLACES                                               \
    (SELECT_STAGE_PAGE_ROWS * SELECT_STAGE_PAGE_COLS)
#define SELECT_STAGE_PAGE_CODES 8
#define SELECT_STAGE_EDGE_STEP 0xF
#define SELECT_STAGE_EDGE_NONE 0xF
#define SELECT_STAGE_EDGE_DOOR_SHIFT 4
/* A page's sheets have a fourth row, which no cursor reaches. */
#define SELECT_STAGE_SHEET_ROWS 4
/* The scroll from one page to the next is kept in thirtieths of a pixel. */
#define SELECT_STAGE_SCROLL_UNIT 30

/* The stage a pick of the stage pages stands for in link mode, and what goes
 * to the fight in its place; the place of the third page that link mode
 * shows another picture for, and no name. */
#define SELECT_STAGE_LINK_FROM STAGE_CASTLE_ROOFTOP
#define SELECT_STAGE_LINK_TO 37
#define SELECT_STAGE_LINK_PAGE 2
#define SELECT_STAGE_LINK_PLACE 3

/* Sounds of this screen (the menu's sound bank). */
enum {
    SELECT_SOUND_DOOR_OPEN = 5,
    SELECT_SOUND_DOOR_CLOSE = 6,
    SELECT_SOUND_WEAPON_FLY = 7,
    SELECT_SOUND_WEAPON_LAND = 8,
    SELECT_SOUND_LOCKED = 10,
    SELECT_SOUND_FIGHTER_FIRST = 11, /* each fighter's voice */
};

/* ---- the sheets ---- */

/* SelectFile.door_sheets. */
enum DoorSheet {
    /* a door: the door, then a strip one pixel high that is stretched down
     * the panel's height */
    DOOR_SHEET_DOOR = 0,
    DOOR_SHEET_DOOR_OVER = 1,  /* drawn over a door, with it */
    DOOR_SHEET_PANEL_EDGE = 2, /* the panel's top edge; turned over, its foot */
    DOOR_SHEET_FRAME_PAGE = 3, /* only its texture page is used */
    DOOR_SHEET_CORNERS = 4,    /* the frame's corner piece, a cell a corner */
    DOOR_SHEET_TITLE = 5,      /* the screen's title */
    DOOR_SHEET_PICKING = 10,   /* four pictures, one a frame */
    DOOR_SHEET_PICKING_COUNT = 4,
};

/* SelectFile.fighter_sheets. The emblems' sheet is three ahead of one set
 * of cells (the plain look, then one lit for each side); the cells are the
 * second emblem's, then the first's. */
enum FighterSheet {
    SELECT_FIGHTER_SHEET_EMBLEMS = 0,
    SELECT_FIGHTER_SHEET_NAMES = 1,   /* twenty, by the fighter of the screen */
    SELECT_FIGHTER_SHEET_LOCKED = 21, /* the blank of a name that is locked */
    SELECT_FIGHTER_SHEET_TITLE = 22,
};

#define EMBLEM_LOOKS 3

/* SelectFile.weapon_row_sheets. The weapons' sheet is six ahead of one set
 * of cells, a sheet a place of the row. */
enum WeaponRowSheet {
    SELECT_WEAPON_ROW_SHEET_WEAPONS = 0,
    SELECT_WEAPON_ROW_SHEET_TITLE = 1,
    /* two cells for each place of the row: (guess) the name of the weapon a
     * side's cursor is on */
    SELECT_WEAPON_ROW_SHEET_NAMES = 2,
};

#define SELECT_WEAPON_NAME_CELLS 2

/* A page of stages (SelectFile.stage_page1_sheets and the two after). */
enum StageSheet {
    SELECT_STAGE_SHEET_UNDER =
        0, /* drawn where the picture's window is, under it */
    SELECT_STAGE_SHEET_PICTURE =
        1, /* the stages' picture: a half, and its mirror */
    SELECT_STAGE_SHEET_TITLE = 2,
    SELECT_STAGE_SHEET_PLACES = 3, /* eight: four rows of two */
    SELECT_STAGE_SHEET_REST = 11,  /* what is round the places */
    SELECT_STAGE_SHEET_LINK_PLACE = 12,
};

/* SelectFile.cursor_sheets. The stages' names are two sheets ahead of one
 * set of cells (the name under the cursor, then the others), a cell a
 * stage. */
enum CursorSheet {
    CURSOR_SHEET_SIDE_1 = 0,
    CURSOR_SHEET_SIDE_2 = 1,
    /* the first pad's, while it picks for the second side */
    CURSOR_SHEET_FIRST_PAD_SECOND = 2,
    CURSOR_SHEET_STAMP = 3, /* the mark left on a pick */
    CURSOR_SHEET_STAGE_NAMES = 4,
};

/* A weapon's sheets as the crossed weapons are drawn: a sheet a clan ahead
 * of one set of cells; the second side's picture is this far along the
 * texture. */
#define SELECT_WEAPON_LOOKS 2
#define SELECT_WEAPON_SECOND_SIDE_U 64

/* ---- the layers ----
 * Entries of the display's ordering table, the nearest first. What is on
 * the panel is between the two entries that set and unset its window. */
enum SelectLayer {
    /* a step's title and the rule under it; the weapons' names; the crossed
     * weapons as they go out */
    SELECT_LAYER_TOP = 1,
    SELECT_LAYER_PANEL_END =
        6, /* the window and the offset are the screen's again */
    SELECT_LAYER_CURSORS = 7, /* the cursors and the marks left on a pick */
    SELECT_LAYER_CURSORS_MODE = 8,
    SELECT_LAYER_NAMES =
        10, /* the fighters' names; the weapons; the stage places */
    SELECT_LAYER_NAMES_MODE = 11, /* and the stages' picture */
    SELECT_LAYER_EMBLEMS = 12,
    SELECT_LAYER_EMBLEMS_MODE = 13,
    SELECT_LAYER_PANEL = 14, /* the panel's paper and its edges */
    /* the panel's window, and the opening door's offset */
    SELECT_LAYER_PANEL_BEGIN = 15,
    SELECT_LAYER_DOORS = 16,
    SELECT_LAYER_STAGES =
        17, /* the stages' names, and the window of their picture */
    SELECT_LAYER_TITLE = 18,     /* the screen's title */
    SELECT_LAYER_WEAPONS = 19,   /* the crossed weapons as they come in */
    SELECT_LAYER_PORTRAITS = 20, /* and the frame's corners */
    SELECT_LAYER_BANNER_LINES = 23,
    SELECT_LAYER_BANNER = 24,
    SELECT_LAYER_FRAME_MODE = 28,
};

/* ---- colours ---- */

/* A portrait that can be picked, every other four frames; one that cannot
 * is black, see-through. */
#define PORTRAIT_R 0x94
#define PORTRAIT_G 0x80
#define PORTRAIT_B 0x6E
/* An emblem whose pick has been shown. */
#define TINT_EMBLEM_PICKED 0x70
/* The frame's corners on the picking steps, in their two looks, and the
 * title's tint on the stage pages. */
#define CORNER_R_1 0xBC
#define CORNER_G_1 0x78
#define CORNER_R_2 0xA8
#define CORNER_G_2 0x6E
#define CORNER_B 0x14
#define SELECT_STAGE_TITLE_R 0x32
#define SELECT_STAGE_TITLE_G 0x32
#define SELECT_STAGE_TITLE_B 0x5A

/* ---- where things are on the screen (640 x 240) ----
 * A sheet's cells have their places in them; these are the places the code
 * works out. */

/* The rule under a step's title is the main menu's, at MENU_TITLE_RULE_Y;
 * its place and length are the step's. */
#define SELECT_FIGHTER_RULE_X 198
#define SELECT_FIGHTER_RULE_WIDTH 243
#define SELECT_WEAPON_RULE_X 204
#define SELECT_WEAPON_RULE_WIDTH 231
#define SELECT_STAGE_RULE_X 183
#define SELECT_STAGE_RULE_WIDTH 273

/* The panel between the doors: its paper, and the window that what is on
 * it is drawn through (a little taller than the paper). The right door is
 * the left one mirrored about DOOR_RIGHT_X; the panel's foot edge is the
 * top one turned over, this much lower. */
#define PANEL_X 76
#define PANEL_Y 128
#define PANEL_WIDTH 533
#define PANEL_HEIGHT 76
#define PANEL_WINDOW_Y 124
#define PANEL_WINDOW_HEIGHT 84
#define PANEL_FOOT_Y 80
#define DOOR_RIGHT_X 512

/* The grid of fighters. The names' columns are GRID_COL_PITCH apart, with
 * GRID_PAIR_GAP more between the second and the third; the rows are
 * GRID_ROW_PITCH apart. A cursor and the mark left on a pick are placed
 * from these; the names and the blank of a locked name from 0. */
#define GRID_COL_PITCH 116
#define GRID_PAIR_GAP 20
#define GRID_ROW_PITCH 16
#define GRID_CURSOR_X 78
#define GRID_STAMP_X 110
#define GRID_Y PANEL_Y
#define GRID_COL_X(col) ((col) * GRID_COL_PITCH + ((col) >> 1) * GRID_PAIR_GAP)
/* The two emblems beside the grid: the second column's, then the first's. */
#define EMBLEM_2_X 130
#define EMBLEM_1_X 382
#define EMBLEM_Y 134

/* The weapon row: its places are SELECT_WEAPON_PITCH apart; the first side's
 * cursor is on the lower line and the second's SELECT_WEAPON_LINE_PITCH higher.
 * The weapons' names are over the row's ends, or in the middle when one
 * side picks. */
#define SELECT_WEAPON_PITCH 64
#define SELECT_WEAPON_LINE_PITCH 32
#define SELECT_WEAPON_CURSOR_X 128
#define SELECT_WEAPON_CURSOR_Y 172
#define SELECT_WEAPON_STAMP_X 133
#define SELECT_WEAPON_STAMP_Y 182
#define SELECT_WEAPON_NAME_X 80
#define SELECT_WEAPON_NAME_PITCH 364
#define SELECT_WEAPON_NAME_ALONE_X 261

/* The crossed weapons: each is drawn about a place near the middle of the
 * screen (g_select_weapon_places, from this height), tilted by
 * SELECT_WEAPON_TILT one way or the other, and flies in from this far out and
 * up. */
#define WEAPONS_Y 157
#define SELECT_WEAPON_TILT (7 * ANGLE_TURN / 64)
#define SELECT_WEAPON_FLY_X 800
#define SELECT_WEAPON_FLY_Y 664

/* A page of stages: the window the stages' picture is seen through, the
 * picture's scale (one and a half across, one and an eighth down), and the
 * stages' names. */
#define STAGES_WINDOW_X 144
#define STAGES_WINDOW_Y 40
#define STAGES_WINDOW_WIDTH 352
#define STAGES_WINDOW_HEIGHT 74
#define STAGES_SCALE_X (SPRITE_SCALE_ONE + SPRITE_SCALE_ONE / 2)
#define STAGES_SCALE_Y (SPRITE_SCALE_ONE + SPRITE_SCALE_ONE / 8)
#define SELECT_STAGE_NAMES_X (-10)
#define SELECT_STAGE_NAMES_Y (-5)

/* The banner: four shaded quads about its middle, and its lines: upright
 * ones BANNER_LINE_PITCH apart, three across, and at each end five stripes
 * that thin out towards the middle. */
#define BANNER_LEFT 80
#define BANNER_RIGHT 561
#define BANNER_TOP 50
#define BANNER_MIDDLE_Y 82
#define BANNER_BOTTOM 113
#define BANNER_HEIGHT (BANNER_BOTTOM - BANNER_TOP)
#define BANNER_LINE_X 111
#define BANNER_LINE_END 528
#define BANNER_LINE_PITCH 32
#define BANNER_ACROSS_X 81
#define BANNER_ACROSS_WIDTH 480
#define BANNER_ACROSS_Y_1 65
#define BANNER_ACROSS_Y_2 97
#define BANNER_ACROSS_Y_3 80 /* this one two pixels thick */
#define BANNER_STRIPES 5
#define BANNER_STRIPE_LEFT_X 106
#define BANNER_STRIPE_LEFT_PITCH 33
#define BANNER_STRIPE_RIGHT_X 529

/* A side's pick: the fighter (an index into the fighters' data;
 * SELECT_NONE until picked), the fighter of the screen its cursor is on,
 * and the weapon. */
typedef struct {
    u8 fighter;
    u8 hover;
    u8 weapon;
} SelectPick;

/* The two picks; `word` is the first four bytes at once. */
typedef union {
    SelectPick rec[2];
    s32 word;
} SelectPicks;

/* The screen's work area, kept after the primitive pools: frames left of
 * showing each side's fighter pick and weapon pick, the frames the screen
 * has run, the frames drawn (the low bit picks the pool) and the draw
 * offset being built. */
typedef struct {
    s16 fighter_frames[2];
    s16 weapon_frames[2];
    s32 frames;
    s32 pool_frame;
    u16 draw_offset[2];
} SelectWork;

/* This file's variables, in the order the original has them. */
static s16 g_select_fighter_pulse[2]; /* SELECT_PULSE_, per side */
static s16 g_select_fighter_stamp[2]; /* cells of the pick's mark drawn */
/* eight bytes between the fighters' marks and the weapons' that no code
 * names; (guess) another pair of pairs like those around them */
static s32 g_select_unused_word_a;
static s32 g_select_unused_word_b;
static s16 g_select_weapon_stamp[2];
static s16 g_select_weapon_pulse[2];
static s32 g_select_held;
static s32 g_select_pressed;
static s16 g_select_fade; /* how dark the screen is, to 0xFF; 0: not fading */
/* 1 once the first pad is picking the second side's fighter, and weapon */
static s16 g_select_extra_fighters;
static s16 g_select_extra_weapons;
static s16 g_select_weapons_frames; /* frames the crossed weapons are up */
static SelectPicks g_select_picks;
static GameSettings *g_select_settings;
static s32 g_select_free_ram;
static s16 g_select_door_x;        /* how far the moving door is across */
static s32 g_select_unused_word_c; /* four bytes that no code names */
static s16 g_select_door;          /* enum SelectDoor */
/* cleared when the screen loads, between the door and the step; nothing
 * reads it */
static s16 g_select_unused_load_halfword;
static s16 g_select_step; /* enum SelectStep */
static s16 g_select_next_step;
/* each side's cursor on the grid */
static s16 g_select_fighter_cols[2];
static s16 g_select_fighter_rows[2];
/* each side's place in the weapon row, and which of the two lines its
 * cursor is drawn on */
static s16 g_select_weapon_cursors[2];
static s16 g_select_weapon_cursor_rows[2];
static s16 g_select_stage_col;
static s16 g_select_stage_row;
static s32 g_select_result; /* what select_Update returns: a page's */
static PrimPool *g_select_pool;
static SelectFile *g_select_file;
static PrimPool *g_select_pools;
static s32 g_select_extra_players; /* 1: the second pad picks its side */
static s16 g_select_last_col;
static s16 g_select_extra_unlocks; /* two more bits of the unlocks; not read */
static s32 g_select_mode;          /* enum FightMode */
static GpuDb *g_select_db;
static SelectWork *g_select_work;
static s16 g_select_weapon_fly[2]; /* frames left of each weapon's flight */

/* ---- loading ---- */

/* Loads the mode's file to `memory` (the two frames' pool records come
 * first) and starts the screen at the grid, the first side's cursor on the
 * first fighter and the second's two columns along. `extra_players` is 1
 * when the second pad picks for the second side. `unlocks` has a bit for
 * each fighter of the screen that can be picked (select_SetUnlocks). */
s32 select_Load(
    s32 memory, s32 extra_players, GameSettings *settings, u32 unlocks) {
    RECT rect;
    s16 i;

    ClearOTagR(g_disp_ot, DISP_OT_SIZE);
    g_select_portrait_loaded = SELECT_PORTRAIT_NONE;
    g_select_mode = settings->menu_mode;
    g_select_pools = (PrimPool *)ADDR(memory);
    g_select_settings = settings;
    g_select_extra_weapons = 0;
    g_select_extra_fighters = 0;
    g_select_portrait_extra = 0;
    g_select_extra_players = extra_players;
    g_select_free_ram = memory + 2 * sizeof(PrimPool);
    g_select_file = (SelectFile *)ADDR(memory + 2 * sizeof(PrimPool));
    resource_FreeAll();

    switch (g_select_mode) {
    case FIGHT_MODE_STORY:
        sprite_LoadFile(
            SELECT_FILE_STORY, (SpriteFileHead *)ADDR(g_select_free_ram));
        break;
    case FIGHT_MODE_VS:
    case FIGHT_MODE_NETWORK:
        sprite_LoadFile(
            SELECT_FILE_TWO_PLAYERS, (SpriteFileHead *)ADDR(g_select_free_ram));
        break;
    case FIGHT_MODE_PRACTICE:
        if (g_select_settings->practice_round) {
            sprite_LoadFile(SELECT_FILE_TWO_PLAYERS,
                            (SpriteFileHead *)ADDR(g_select_free_ram));
        } else {
            sprite_LoadFile(SELECT_FILE_ONE_PLAYER,
                            (SpriteFileHead *)ADDR(g_select_free_ram));
        }
        break;
    case FIGHT_MODE_VSCPU:
    case FIGHT_MODE_CHANBARA:
        sprite_LoadFile(
            SELECT_FILE_ONE_PLAYER, (SpriteFileHead *)ADDR(g_select_free_ram));
        break;
    }

    g_select_free_ram =
        select_RelocateFile((SelectFile *)ADDR(g_select_free_ram));
    g_select_free_ram = sprite_InitPools(g_select_free_ram, g_select_pools);
    g_select_picks.word = -1;
    g_select_door_x = 0;
    g_select_door = SELECT_DOOR_OPEN;
    g_select_unused_load_halfword = 0;
    g_select_step = SELECT_STEP_FIGHTER;
    g_select_result = PAGE_RUNNING;
    g_select_fighter_cols[0] = 0;
    g_select_fighter_rows[0] = 0;
    g_select_fighter_cols[1] = SELECT_SECOND_START_COL;
    g_select_fighter_rows[1] = 0;
    g_select_picks.rec[0].hover = g_select_grid[0][0].value;
    g_select_picks.rec[1].hover =
        g_select_grid[0][SELECT_SECOND_START_COL].value;

    for (i = 0; i < SELECT_FIGHTERS; i++) {
        g_select_fighter_flags[i] &=
            ~(SELECT_FIGHTER_OPEN | SELECT_FIGHTER_TAKEN);
        if (unlocks & (1 << i)) {
            g_select_fighter_flags[i] |= SELECT_FIGHTER_OPEN;
        }
    }

    /* the one-player modes leave two places of the grid out: no cursor
     * can go there */
    if (g_select_mode < FIGHT_MODE_VS) {
        if (g_select_mode >= 0) {
            g_select_fighter_flags[g_select_grid[SELECT_ROW_LEFT_OUT]
                                                [SELECT_COL_LEFT_OUT_1]
                                                    .value] |=
                SELECT_FIGHTER_TAKEN;
            g_select_fighter_flags[g_select_grid[SELECT_ROW_LEFT_OUT]
                                                [SELECT_COL_LEFT_OUT_2]
                                                    .value] |=
                SELECT_FIGHTER_TAKEN;
        }
    }
    /* the two bits after the twenty names' */
    g_select_extra_unlocks = (unlocks >> 20) & 3;
    g_select_last_col = SELECT_GRID_COLS - 1;
    g_select_fighter_flags[g_select_grid[g_select_fighter_rows[0]]
                                        [g_select_fighter_cols[0]]
                                            .value] |= SELECT_FIGHTER_TAKEN;
    if (g_select_extra_players != 0) {
        g_select_fighter_flags[g_select_grid[g_select_fighter_rows[1]]
                                            [g_select_fighter_cols[1]]
                                                .value] |= SELECT_FIGHTER_TAKEN;
    }

    rect = g_select_palette_rect;
    g_select_stage_row = 0;
    g_select_stage_col = 0;
#if RB_PORT
    /* a mode begun again: no arena chosen, the kinds' default
     * (port_stage_choice.h), so that a story's cutscene never loads its
     * stage with an arena versus mode chose */
    port_StageChoiceReset();
    port_PlayReset();
#endif
    DrawSync(0);
    MoveImage(&rect, SPRITE_STRIP_COPY_X, SPRITE_STRIP_COPY_Y);
    DrawSync(0);
    g_select_work = (SelectWork *)ADDR(g_select_free_ram);
    g_select_free_ram += sizeof(SelectWork);
    g_select_work->frames = 0;
    g_select_work->pool_frame = 0;
    return 1;
}

/* The pool of frame `frame` (0 or 1). */
static PrimPool *select_GetPrimPool(s32 frame) {
    return &g_select_pools[frame];
}

/* Loads a resource file's bottom strip, the colour tables, into video
 * memory: the grayscale one when the "Monitor" option is black and white. */
void sprite_LoadBottomStrip(SpriteFileHead *file) {
    GameSettings *settings;
    s32 data;
    RECT rect;
    settings = menu_GetSettings();
    if (settings->grayscale) {
        data = file->grayscale_bottom_strip;
    } else {
        data = file->bottom_strip;
    }
    rect.x = 0;
    rect.y = VRAM_COLORS_Y;
    rect.w = DISP_WIDTH;
    rect.h = VRAM_COLORS_HEIGHT;
    DrawSync(0);
    LoadImage(&rect, (u32 *)ADDR(data));
    DrawSync(0);
}

/* Reads resource file `data_file` of the disc's data group to `file`,
 * relocates its head and loads its image into video memory, to the right of
 * the display buffers: the page, and under it a strip not so wide, whose
 * pixels follow the page's in the file. Then the bottom strip. */
void sprite_LoadFile(s32 data_file, SpriteFileHead *file) {
    s32 image;
    RECT rect;

    cdrom_WaitIdle();
    cdrom_StartRead(cdrom_GetFileIndex(CDROM_GROUP_DATA, data_file), (s32)file);
    cdrom_WaitIdle();
    sprite_RelocateFile((s32 *)file);

    image = file->image;

    rect.x = VRAM_PAGE_X;
    rect.y = 0;
    rect.w = VRAM_PAGE_WIDTH;
    rect.h = VRAM_PAGE_HEIGHT;
    DrawSync(0);
    LoadImage(&rect, (u32 *)ADDR(image));

    rect.w = VRAM_PAGE_FOOT_WIDTH;
    rect.x = VRAM_PAGE_X;
    rect.y = VRAM_PAGE_HEIGHT;
    rect.h = VRAM_PAGE_FOOT_HEIGHT;
    DrawSync(0);
    LoadImage(&rect, (u32 *)ADDR(image + VRAM_PAGE_BYTES));

    sprite_LoadBottomStrip(file);
}

/* Turns the offsets in the file's nine lists into addresses and loads the
 * screen's sounds. Returns where the file's image began, which is free once
 * it is in video memory. */
static s32 select_RelocateFile(SelectFile *file) {
    sprite_RelocateList(file, PTR(file->portraits));
    sprite_RelocateList(file, PTR(file->weapon_sheets));
    sprite_RelocateList(file, PTR(file->door_sheets));
    sprite_RelocateList(file, PTR(file->fighter_sheets));
    sprite_RelocateList(file, PTR(file->weapon_row_sheets));
    sprite_RelocateList(file, PTR(file->stage_page1_sheets));
    sprite_RelocateList(file, PTR(file->stage_page2_sheets));
    sprite_RelocateList(file, PTR(file->stage_page3_sheets));
    sprite_RelocateList(file, PTR(file->cursor_sheets));
    snd_LoadVab(
        PTR(file->head.sounds), SND_VAB_MENU, file->head.free_ram_start);
    return file->head.free_ram_start;
}

/* ---- a frame of the screen ---- */

/* A frame of the screen: moves the doors, starts the frame's pool, draws
 * the doors (not while the weapons are up) and runs the step. Returns 1
 * when everything is picked, -1 when the screen is left, 0 otherwise. In
 * training mode only the first pad is read. */
s32 select_Update(s32 held, s32 pressed) {
    MenuDrawCtx ctx;
    PrimPool *pool;
    s32 pool_frame;

    g_select_db = &g_disp_db[g_disp_frame_count & 1];
    g_select_held = held;
    g_select_pressed = pressed;
    if (g_select_mode == FIGHT_MODE_PRACTICE) {
        g_select_pressed = pressed & 0xFFFF;
    }
#if RB_PORT
    if (g_select_step < SELECT_STEP_STAGE_1 ||
        g_select_step >= SELECT_STEP_COUNT) {
        port_StageLeftPages();
    }
#endif
    select_StepDoor();
    pool_frame = g_select_work->pool_frame + 1;
    g_select_work->frames = g_select_work->frames + 1;
    g_select_work->pool_frame = pool_frame;
    pool = select_GetPrimPool(pool_frame & 1);
    ctx.file.select = g_select_file;
    ctx.prims.ft4 = pool->ft4;
    ctx.prims.g4 = PTR(pool->g4);
    ctx.prims.sprt = pool->sprt;
    ctx.prims.tile = PTR(pool->tile);
    ctx.prims.mode = PTR(pool->mode);
    ctx.prims.area = PTR(pool->area);
    g_select_pool = pool;
    ctx.prims.offset = PTR(pool->offset);
    ctx.tail = PTR(pool->tail_region);
    /* not while the crossed weapons are up */
    if (g_select_step < SELECT_STEP_WEAPONS_IN ||
        g_select_step > SELECT_STEP_WEAPONS_OUT) {
        select_DrawDoors(&ctx);
    }
    g_select_steps[g_select_step](&ctx);
    return g_select_result;
}

/* Moves the door that is moving. When a closing door is shut the step
 * changes behind it and it starts to open, unless it is to stay shut. */
static void select_StepDoor(void) {
    switch (g_select_door & SELECT_DOOR_STATE) {
    case SELECT_DOOR_RIGHT_CLOSING:
    case SELECT_DOOR_LEFT_CLOSING:
        g_select_door_x += SELECT_DOOR_SPEED;
        if (g_select_door_x >= SELECT_DOOR_SHUT_X) {
            g_select_door_x = SELECT_DOOR_SHUT_X;
            g_select_step = g_select_next_step;
            /* not one that stays shut */
            if ((g_select_door >> 8) == 0) {
                g_select_door++;
            }
        }
        break;
    case SELECT_DOOR_RIGHT_OPENING:
    case SELECT_DOOR_LEFT_OPENING:
        if (g_select_door_x == SELECT_DOOR_SHUT_X) {
            snd_Play(SELECT_SOUND_DOOR_OPEN, SND_VOLUME_MAX, SND_VOLUME_MAX);
        }
        g_select_door_x -= SELECT_DOOR_SPEED;
        if (g_select_door_x <= 0) {
            g_select_door_x = 0;
            g_select_door = SELECT_DOOR_OPEN;
        }
        break;
    }
}

/* ---- the portraits and the fade ---- */

/* The portraits behind the banner: the fighter under each side's cursor,
 * the second side's mirrored. A fighter that cannot be picked is a dark
 * shape; one that can flickers. Five fighters are not mirrored but have a
 * portrait of their own for the right, whose pixels are loaded when the
 * second cursor comes to one. With one side picking, the portrait is drawn
 * on the side of its clan. */
static void select_DrawPortraits(MenuDrawCtx *ctx) {
    SpriteDraw draw;
    SelectPortraits *portraits;
    SpriteSheet *sheets;
    s32 who;
    s32 pixels;
    s16 side;
    RECT rect;

    draw.ot_index = SELECT_LAYER_PORTRAITS;
    /* twice as wide as they are in the texture */
    draw.scale_x = 2 * SPRITE_SCALE_ONE;
    draw.x = 0;
    draw.y = 0;
    draw.scale_y = SPRITE_SCALE_ONE;

    portraits = PTR(ctx->file.select->portraits);
    sheets = portraits->sheets;

    for (side = 0; side < g_select_extra_players + 1 + g_select_extra_fighters;
         side++) {
        who = g_select_grid[g_select_fighter_rows[side]]
                           [g_select_fighter_cols[side]]
                               .value;
        if (g_select_fighter_flags[who] & SELECT_FIGHTER_OPEN) {
            draw.semi = 0;
            if (g_select_work->frames & FLICKER_FRAMES) {
                draw.has_color = 1;
            } else {
                draw.has_color = 0;
            }
            draw.col_r = PORTRAIT_R;
            draw.col_g = PORTRAIT_G;
            draw.col_b = PORTRAIT_B;
        } else {
            draw.semi = 1;
            draw.has_color = 1;
            draw.col_b = 0;
            draw.col_g = 0;
            draw.col_r = 0;
        }

        /* the five with a portrait of their own for the right */
        switch (who) {
        case SELECT_OWN_PORTRAIT_1:
            pixels = g_select_file->mirrored_portraits[0];
            draw.header = sheets + SELECT_FIGHTERS;
            break;
        case SELECT_OWN_PORTRAIT_2:
            pixels = g_select_file->mirrored_portraits[1];
            draw.header = sheets + SELECT_FIGHTERS + 1;
            break;
        case SELECT_OWN_PORTRAIT_3:
            pixels = g_select_file->mirrored_portraits[2];
            draw.header = sheets + SELECT_FIGHTERS + 2;
            break;
        case SELECT_OWN_PORTRAIT_4:
            pixels = g_select_file->mirrored_portraits[3];
            draw.header = sheets + SELECT_FIGHTERS + 3;
            break;
        case SELECT_OWN_PORTRAIT_5:
            pixels = g_select_file->mirrored_portraits[4];
            draw.header = sheets + SELECT_FIGHTERS + 4;
            break;
        default:
            draw.header = sheets + who;
            goto draw;
        }
        if (side != 0) {
            if (g_select_portrait_loaded != who ||
                g_select_portrait_extra != g_select_extra_fighters) {
                rect = g_select_portrait_rect;
                LoadImage(&rect, (u32 *)ADDR(pixels));
                DrawSync(0);
            }
        } else {
            draw.header = sheets + who;
        }
    draw:
        if (g_select_extra_players == 0 &&
            !(g_select_fighter_flags[g_select_picks.rec[side].hover] &
              SELECT_FIGHTER_FIRST_CLAN) &&
            (g_select_mode != FIGHT_MODE_PRACTICE ||
             !g_select_settings->practice_round)) {
            draw.table = PTR(portraits->side_cells[1]);
        } else {
            draw.table = PTR(portraits->side_cells[side]);
        }
        draw.ft4_out = ctx->prims.ft4;
        if (side != 0) {
            ctx->prims.ft4 = sprite_DrawScaled(&draw, SPRITE_QUAD_MIRROR_X);
        } else {
            ctx->prims.ft4 = sprite_DrawScaled(&draw, SPRITE_QUAD_NORMAL);
        }
    }
}

/* A tile over the whole screen that takes g_select_fade off what is under
 * it. */
static void select_DrawFade(MenuDrawCtx *ctx) {
    TILE *tile;
    s16 red;
    s16 green;
    s16 blue;

    tile = ctx->prims.tile;
    SetTile(tile);
    red = g_select_fade;
    tile->x0 = 0;
    tile->y0 = 0;
    tile->r0 = red;
    green = g_select_fade;
    tile->w = DISP_WIDTH;
    tile->g0 = green;
    blue = g_select_fade;
    tile->h = DISP_HEIGHT;
    tile->b0 = blue;
    SetSemiTrans(tile, 1);
    AddPrim(g_disp_ot, tile);
    tile++;
    ctx->prims.tile = tile;
    SetDrawMode(ctx->prims.mode, 1, 0, SPRITE_ABR_SUBTRACT, 0);
    AddPrim(g_disp_ot, ctx->prims.mode);
    ctx->prims.mode++;
}

/* ---- the crossed weapons come in ---- */

/* SELECT_STEP_WEAPONS_IN. The screen first darkens quickly behind the last
 * frame of the weapon row; then the two picked weapons fly in from the
 * sides and cross, each a picture tilted, the second's mirrored. When they
 * have been up SELECT_WEAPONS_FRAMES frames the next step is asked for. */
void select_WeaponsInStep(MenuDrawCtx *ctx) {
    SpriteDraw draw;
    s32 side;
    SpriteSheetSlot *sheets;
    SpriteSheet *sheet;
    s32 look;
    s16 x_offset;
    s16 y_offset;
    s16 width;
    s16 height;

    if (g_select_fade != 0) {
        g_select_fade += FADE_IN_STEP;
        if (g_select_fade > FADE_FULL) {
            g_select_fade = FADE_FULL;
            select_DrawFade(ctx);
            g_select_fade = 0;
        } else {
            select_DrawFade(ctx);
        }
        select_DrawPortraits(ctx);
        select_DrawBanner(ctx);
        select_DrawFrame(ctx);
        return;
    }

    if (g_select_weapons_frames > SELECT_WEAPONS_FRAMES) {
        g_select_next_step = SELECT_STEP_WEAPONS_OUT;
    }
    draw.scale_x = SPRITE_SCALE_ONE;
    draw.scale_y = SPRITE_SCALE_ONE;
    draw.ot_index = SELECT_LAYER_WEAPONS;
    draw.semi = 0;
    draw.has_color = 0;
    sheets = PTR(g_select_file->weapon_sheets);
    for (side = 0; side < 1 + g_select_extra_players + g_select_extra_weapons;
         side++) {
        sheet = PTR(sheets[g_select_picks.rec[side].weapon]);
        look = 1;
        if (g_select_mode == FIGHT_MODE_PRACTICE &&
            !(g_select_settings->practice_round) && side == 1) {
            if (g_select_fighter_flags[g_select_picks.rec[0].hover] &
                SELECT_FIGHTER_FIRST_CLAN) {
                look = 1;
            } else {
                look = 0;
            }
        } else if (g_select_fighter_flags[g_select_picks.rec[side].hover] &
                   SELECT_FIGHTER_FIRST_CLAN) {
            look = 0;
        }
        draw.header = &sheet[look];
        if (side != 0) {
            draw.header->ubase = SELECT_WEAPON_SECOND_SIDE_U;
        } else {
            draw.header->ubase = 0;
        }
        draw.table = sheet[SELECT_WEAPON_LOOKS - 1].cells;
        x_offset = -(g_select_weapon_fly[side] * SELECT_WEAPON_FLY_X) /
                   SELECT_WEAPON_FLY_2;
        /* the picture's size: from its first cell's corner to its last
         * cell's far one */
        {
            s32 index = draw.header->count - 1;
            width = draw.table[index].x + draw.table[index].w -
                    sheet[SELECT_WEAPON_LOOKS - 1].cells[0].x;
            height = draw.table[index].y + draw.table[index].h -
                     sheet[SELECT_WEAPON_LOOKS - 1].cells[0].y;
        }
        y_offset = -(g_select_weapon_fly[side] * SELECT_WEAPON_FLY_Y) /
                   SELECT_WEAPON_FLY_2;
        if (side != 0) {
            x_offset = -x_offset;
        }
        draw.x =
            (g_select_weapon_places[side][g_select_weapon_cursors[side]].x +
             DISP_WIDTH / 2) +
            x_offset - ((width * draw.scale_x >> 8) / 2);
        draw.y =
            (g_select_weapon_places[side][g_select_weapon_cursors[side]].y +
             WEAPONS_Y) +
            y_offset - ((height * draw.scale_y >> 8) / 2);
        draw.ft4_out = ctx->prims.ft4;
        if (side != 0) {
            ctx->prims.ft4 = sprite_DrawRotated(
                &draw, SELECT_WEAPON_TILT, SPRITE_QUAD_MIRROR_X);
        } else {
            ctx->prims.ft4 = sprite_DrawRotated(
                &draw, ANGLE_TURN - SELECT_WEAPON_TILT, SPRITE_QUAD_NORMAL);
        }
        if (g_select_weapon_fly[side] > 0) {
            g_select_weapon_fly[side]--;
        }
        if (g_select_weapon_fly[side] == SELECT_WEAPON_FLY_SOUND) {
            snd_Play(SELECT_SOUND_WEAPON_FLY, SND_VOLUME_MAX, SND_VOLUME_MAX);
        }
        if (g_select_weapon_fly[side] < 0) {
            g_select_weapon_fly[side] = 0;
            snd_Play(SELECT_SOUND_WEAPON_LAND, SND_VOLUME_MAX, SND_VOLUME_MAX);
        }
    }
    g_select_weapons_frames++;
}

/* ---- the doors ---- */

/* The two sliding doors and what is between them: the panel the grid, the
 * row or the stages are drawn on, with the draw area and offset set so
 * that what is on the panel slides with the door that is opening. The left
 * door is the one that moves on the way back, the right one on the way
 * forward. */
static void select_DrawDoors(MenuDrawCtx *ctx) {
    SpriteDraw draw;
    RECT rect;
    SpriteSheetSlot *sheets;
    TILE *tile;
    s16 door;
    SpriteCell *cells;

    draw.semi = 0;
    draw.y = 0;
    draw.scale_x = SPRITE_SCALE_ONE;
    draw.scale_y = SPRITE_SCALE_ONE;
    draw.has_color = 0;

    sheets = PTR(g_select_file->door_sheets);
    draw.ot_index = SELECT_LAYER_DOORS;
    door = g_select_door & SELECT_DOOR_STATE;
    draw.header = PTR(sheets[DOOR_SHEET_DOOR]);
    cells = draw.header->cells;
    if (door >= SELECT_DOOR_LEFT_CLOSING) {
        draw.x = g_select_door_x;
    } else {
        draw.x = 0;
    }
    draw.table = cells;
    draw.ft4_out = ctx->prims.ft4;
    ctx->prims.ft4 = sprite_DrawScaled(&draw, SPRITE_QUAD_NORMAL);

    draw.table++;
    draw.scale_y = PANEL_HEIGHT * SPRITE_SCALE_ONE;
    draw.ft4_out = ctx->prims.ft4;
    ctx->prims.ft4 = sprite_DrawScaled(&draw, SPRITE_QUAD_NORMAL);

    draw.scale_y = SPRITE_SCALE_ONE;
    draw.header = PTR(sheets[DOOR_SHEET_DOOR_OVER]);
    cells = draw.header->cells;
    draw.table = cells;
    draw.ft4_out = ctx->prims.ft4;
    ctx->prims.ft4 = sprite_DrawScaled(&draw, SPRITE_QUAD_NORMAL);

    draw.header = PTR(sheets[DOOR_SHEET_DOOR]);
    cells = draw.header->cells;
    if (door < SELECT_DOOR_LEFT_CLOSING) {
        draw.x = -g_select_door_x + DOOR_RIGHT_X;
    } else {
        draw.x = DOOR_RIGHT_X;
    }
    draw.table = cells;
    draw.ft4_out = ctx->prims.ft4;
    ctx->prims.ft4 = sprite_DrawScaled(&draw, SPRITE_QUAD_MIRROR_X);

    draw.table++;
    draw.scale_y = PANEL_HEIGHT * SPRITE_SCALE_ONE;
    draw.ft4_out = ctx->prims.ft4;
    ctx->prims.ft4 = sprite_DrawScaled(&draw, SPRITE_QUAD_NORMAL);

    draw.scale_y = SPRITE_SCALE_ONE;
    draw.header = PTR(sheets[DOOR_SHEET_DOOR_OVER]);
    cells = draw.header->cells;
    draw.table = cells;
    draw.ft4_out = ctx->prims.ft4;
    ctx->prims.ft4 = sprite_DrawScaled(&draw, SPRITE_QUAD_MIRROR_X);

    /* the panel's window: what the doors do not cover */
    if (door >= SELECT_DOOR_LEFT_CLOSING) {
        rect.x = g_select_door_x + PANEL_X;
    } else {
        rect.x = PANEL_X;
    }
    rect.y = g_select_db->draw.clip.y + PANEL_WINDOW_Y;
    rect.w = SELECT_DOOR_SHUT_X - g_select_door_x;
    rect.h = PANEL_WINDOW_HEIGHT;
    SetDrawArea(ctx->prims.area, &rect);
    AddPrim(g_disp_ot + SELECT_LAYER_PANEL_BEGIN, ctx->prims.area);
    ctx->prims.area++;

    rect.x = g_select_db->draw.clip.x;
    rect.y = g_select_db->draw.clip.y;
    rect.w = g_select_db->draw.clip.w;
    rect.h = g_select_db->draw.clip.h;
    SetDrawArea(ctx->prims.area, &rect);
    AddPrim(g_disp_ot + SELECT_LAYER_PANEL_END, ctx->prims.area);
    ctx->prims.area++;

    switch (door) {
    case SELECT_DOOR_RIGHT_OPENING:
    case SELECT_DOOR_LEFT_OPENING:
        g_select_work->draw_offset[0] =
            g_select_db->draw.ofs[0] +
            (door == SELECT_DOOR_RIGHT_OPENING ? -g_select_door_x
                                               : g_select_door_x);
        g_select_work->draw_offset[1] = g_select_db->draw.ofs[1];
        SetDrawOffset(ctx->prims.offset, g_select_work->draw_offset);
        AddPrim(g_disp_ot + SELECT_LAYER_PANEL_BEGIN, ctx->prims.offset);
        ctx->prims.offset++;

        g_select_work->draw_offset[0] = g_select_db->draw.ofs[0];
        g_select_work->draw_offset[1] = g_select_db->draw.ofs[1];
        SetDrawOffset(ctx->prims.offset, g_select_work->draw_offset);
        AddPrim(g_disp_ot + SELECT_LAYER_PANEL_END, ctx->prims.offset);
        ctx->prims.offset++;
        break;
    }

    tile = ctx->prims.tile;
    SetTile(tile);
    SetSemiTrans(tile, 0);
    if (g_select_settings->grayscale) {
        setRGB0(tile, SELECT_PAPER_GRAY, SELECT_PAPER_GRAY, SELECT_PAPER_GRAY);
    } else {
        setRGB0(tile, SELECT_PAPER_R, SELECT_PAPER_G, SELECT_PAPER_B);
    }
    setXY0(tile, PANEL_X, PANEL_Y);
    setWH(tile, PANEL_WIDTH, PANEL_HEIGHT);
    AddPrim(g_disp_ot + SELECT_LAYER_PANEL, tile);
    tile++;
    ctx->prims.tile = tile;

    draw.header = PTR(sheets[DOOR_SHEET_PANEL_EDGE]);
    cells = draw.header->cells;
    draw.table = cells;
    draw.scale_y = SPRITE_SCALE_ONE;
    draw.x = 0;
    draw.ot_index = SELECT_LAYER_PANEL;
    draw.ft4_out = ctx->prims.ft4;
    ctx->prims.ft4 = sprite_DrawScaled(&draw, SPRITE_QUAD_NORMAL);

    draw.y = PANEL_FOOT_Y;
    draw.ft4_out = ctx->prims.ft4;
    ctx->prims.ft4 = sprite_DrawScaled(&draw, SPRITE_QUAD_MIRROR_Y);
}

/* ---- the grid of fighters ---- */

/* SELECT_STEP_FIGHTER: the grid. Four columns of five names, and to the
 * side two more fighters as emblems (columns 4 and 5, the same in every
 * row), which sway when a cursor is on them. A name that is locked is
 * drawn as a blank; a name a cursor is on glows. In the one-player modes
 * two of the places are left out. Then the cursors, the portraits and the
 * banner. */
void select_FighterStep(MenuDrawCtx *ctx) {
    SpriteDraw draw;
    s16 pulse_speed[2];
    u16 rule[4];
    SpriteSheetSlot *sheets;
    SpriteCell *cells;
    s16 side;
    s16 grid_col;
    s16 grid_row;

    draw.scale_x = SPRITE_SCALE_ONE;
    draw.scale_y = SPRITE_SCALE_ONE;
    draw.semi = 0;
    draw.col_r = draw.col_g = draw.col_b = TINT_EMBLEM_PICKED;
    sheets = PTR(g_select_file->fighter_sheets);
    draw.header = PTR(sheets[SELECT_FIGHTER_SHEET_EMBLEMS]);
    SetDrawMode(ctx->prims.mode, 1, 0, sprite_GetTPage(draw.header, 0), 0);
    AddPrim(g_disp_ot + SELECT_LAYER_EMBLEMS_MODE, ctx->prims.mode);
    ctx->prims.mode++;

    /* the second emblem, in the look of the side whose cursor is on it */
    draw.ot_index = SELECT_LAYER_EMBLEMS;
    draw.x = EMBLEM_2_X;
    draw.y = EMBLEM_Y;
    draw.header = PTR(sheets[SELECT_FIGHTER_SHEET_EMBLEMS]);
    cells = draw.header[EMBLEM_LOOKS - 1].cells;
    draw.has_color = 0;
    for (side = 0; side < 1 + g_select_extra_players + g_select_extra_fighters;
         side++) {
        if (g_select_fighter_cols[side] == SELECT_COL_EMBLEM_2) {
            draw.header += 1 + side;
            if (g_select_work->fighter_frames[side] != 0) {
                draw.x += EMBLEM_SWAY_X;
                draw.y += EMBLEM_SWAY_Y;
            } else {
                draw.has_color = 1;
            }
            if (g_select_work->fighter_frames[side] != SELECT_PICK_FRAMES) {
                draw.y += (g_select_work->fighter_frames[side] *
                           rsin(EMBLEM_BOB_ANGLE)) >>
                          12;
            }
            break;
        }
    }
    draw.table = cells;
    draw.sprt_out = ctx->prims.sprt;
    ctx->prims.sprt = sprite_Draw(&draw);

    /* the first, the same way */
    draw.x = EMBLEM_1_X;
    draw.y = EMBLEM_Y;
    draw.header = PTR(sheets[SELECT_FIGHTER_SHEET_EMBLEMS]);
    draw.has_color = 0;
    for (side = 0; side < 1 + g_select_extra_players + g_select_extra_fighters;
         side++) {
        if (g_select_fighter_cols[side] == SELECT_COL_EMBLEM_1) {
            draw.header += 1 + side;
            if (g_select_work->fighter_frames[side] != 0) {
                draw.x += EMBLEM_SWAY_X;
                draw.y += EMBLEM_SWAY_Y;
            } else {
                draw.has_color = 1;
            }
            if (g_select_work->fighter_frames[side] != SELECT_PICK_FRAMES) {
                draw.y += (g_select_work->fighter_frames[side] *
                           rsin(EMBLEM_BOB_ANGLE)) >>
                          12;
            }
            break;
        }
    }
    draw.table += draw.header->count;
    draw.sprt_out = ctx->prims.sprt;
    ctx->prims.sprt = sprite_Draw(&draw);

    draw.x = 0;
    draw.y = 0;
    draw.header = PTR(sheets[SELECT_FIGHTER_SHEET_NAMES]);
    SetDrawMode(ctx->prims.mode, 1, 0,
                sprite_GetTPage(draw.header, SPRITE_ABR_ADD_QUARTER), 0);
    AddPrim(g_disp_ot + SELECT_LAYER_NAMES_MODE, ctx->prims.mode);
    ctx->prims.mode++;

    for (grid_col = 0; grid_col < SELECT_GRID_NAME_COLS; grid_col++) {
        for (grid_row = 0; grid_row < SELECT_GRID_ROWS; grid_row++) {
            if ((g_select_mode == FIGHT_MODE_STORY ||
                 g_select_mode == FIGHT_MODE_VSCPU ||
                 g_select_mode == FIGHT_MODE_PRACTICE ||
                 g_select_mode == FIGHT_MODE_CHANBARA) &&
                grid_row == SELECT_ROW_LEFT_OUT &&
                (grid_col == SELECT_COL_LEFT_OUT_1 ||
                 grid_col == SELECT_COL_LEFT_OUT_2)) {
                continue;
            }
            if (g_select_fighter_flags[g_select_grid[grid_row][grid_col]
                                           .value] &
                SELECT_FIGHTER_OPEN) {
                draw.y = 0;
                draw.x = 0;
                draw.has_color = 0;
                for (side = 0; side < 1 + g_select_extra_players +
                                          g_select_extra_fighters;
                     side++) {
                    if (g_select_picks.rec[side].fighter == SELECT_NONE) {
                        pulse_speed[side] = SELECT_PULSE_CHOOSING;
                    } else {
                        pulse_speed[side] = SELECT_PULSE_PICKED;
                    }
                    if (grid_row == g_select_fighter_rows[side] &&
                        grid_col == g_select_fighter_cols[side] &&
                        g_select_work->fighter_frames[side] != 0) {
                        draw.has_color = 1;
                        draw.col_r = draw.col_g = draw.col_b = SELECT_PULSE(
                            pulse_speed[side], side * SELECT_PULSE_SIDE_PHASE);
                        break;
                    }
                }
                draw.header =
                    PTR(sheets[g_select_grid[grid_row][grid_col].value +
                               SELECT_FIGHTER_SHEET_NAMES]);
                cells = draw.header->cells;
                draw.table = cells;
                draw.sprt_out = ctx->prims.sprt;
                draw.ot_index = SELECT_LAYER_NAMES;
                ctx->prims.sprt = sprite_Draw(&draw);
            } else {
                draw.y = grid_row * GRID_ROW_PITCH;
                draw.x = GRID_COL_X(grid_col);
                draw.has_color = 0;
                draw.header = PTR(sheets[SELECT_FIGHTER_SHEET_LOCKED]);
                cells = draw.header->cells;
                draw.table = cells;
                draw.sprt_out = ctx->prims.sprt;
                draw.ot_index = SELECT_LAYER_NAMES;
                ctx->prims.sprt = sprite_Draw(&draw);
            }
        }
    }

    draw.y = 0;
    draw.x = 0;
    draw.has_color = 0;
    draw.header = PTR(sheets[SELECT_FIGHTER_SHEET_TITLE]);
    cells = draw.header->cells;
    draw.table = cells;
    draw.sprt_out = ctx->prims.sprt;
    draw.ot_index = SELECT_LAYER_TOP;
    ctx->prims.sprt = sprite_Draw(&draw);
    SetDrawMode(ctx->prims.mode, 1, 0,
                sprite_GetTPage(draw.header, SPRITE_ABR_ADD_QUARTER), 0);
    AddPrim(g_disp_ot + SELECT_LAYER_TOP, ctx->prims.mode);
    ctx->prims.mode++;
    rule[2] = SELECT_FIGHTER_RULE_WIDTH;
    rule[0] = SELECT_FIGHTER_RULE_X;
    rule[1] = MENU_TITLE_RULE_Y;
    rule[3] = 1;
    /* The rule is drawn by the main menu's function, which wants its own
     * context and is handed this one as it is: a word out, so the tile is
     * built where this screen keeps its draw modes (the original's
     * behaviour). */
    menu_DrawTitleRule((MenuCtx *)ctx, rule, SELECT_LAYER_TOP);
    select_MoveFighterCursors(ctx);
    select_DrawPortraits(ctx);
    g_select_portrait_loaded = g_select_picks.rec[1].hover;
    g_select_portrait_extra = g_select_extra_fighters;
    select_DrawBanner(ctx);
    select_DrawFrame(ctx);
}

/* The grid's cursors: each side's moves over the fighters, skipping one
 * the other cursor is on (and, sideways, the emblems that are locked).
 * Cross picks the fighter, if it is unlocked, and it calls its name; the
 * cursor then glows fast and a mark is drawn across the name. Triangle
 * takes the pick back, or leaves the screen. When every side has picked
 * and been shown, the door closes on the weapon row; in a "Practice Round"
 * the first pad first picks the opponent, starting from the other clan. */
static void select_MoveFighterCursors(MenuDrawCtx *ctx) {
    SpriteDraw draw;
    SpriteSheetSlot *sheets;
    s16 *col;
    s16 *row;
    u8 *flags;
    s16 side;
    s16 pad;
    s16 extra;

    draw.semi = 0;
    draw.scale_x = SPRITE_SCALE_ONE;
    draw.scale_y = SPRITE_SCALE_ONE;
    sheets = PTR(g_select_file->cursor_sheets);
    draw.header = PTR(sheets[CURSOR_SHEET_SIDE_1]);
    SetDrawMode(ctx->prims.mode, 1, 0,
                sprite_GetTPage(draw.header, SPRITE_ABR_ADD_QUARTER), 0);
    AddPrim(g_disp_ot + SELECT_LAYER_CURSORS_MODE, ctx->prims.mode);
    ctx->prims.mode++;
    for (side = 0;
         side < 1 + g_select_extra_players + (extra = g_select_extra_fighters);
         side++) {
        col = &g_select_fighter_cols[side];
        row = &g_select_fighter_rows[side];
        flags = &g_select_fighter_flags[g_select_grid[*row][*col].value];
        g_select_picks.rec[side].hover = g_select_grid[*row][*col].value;
        /* the first pad, for the second side, while it picks for both */
        pad = side - extra;
        if (g_select_picks.rec[side].fighter == SELECT_NONE) {
            g_select_work->fighter_frames[side] = SELECT_PICK_FRAMES;
            if (g_select_pressed & PAD_OF(PADLdown, pad)) {
                snd_Play(MENU_SOUND_CURSOR, SND_VOLUME_MAX, SND_VOLUME_MAX);
                *flags &= ~SELECT_FIGHTER_TAKEN;
                do {
                    (*row)++;
                    if (*row >= SELECT_GRID_ROWS) {
                        *row = 0;
                    }
                    flags = &g_select_fighter_flags[g_select_grid[*row][*col]
                                                        .value];
                    g_select_picks.rec[side].hover =
                        g_select_grid[*row][*col].value;
                } while (*flags & SELECT_FIGHTER_TAKEN);
                *flags |= SELECT_FIGHTER_TAKEN;
            } else if (g_select_pressed & PAD_OF(PADLup, pad)) {
                snd_Play(MENU_SOUND_CURSOR, SND_VOLUME_MAX, SND_VOLUME_MAX);
                *flags &= ~SELECT_FIGHTER_TAKEN;
                do {
                    (*row)--;
                    if (*row < 0) {
                        *row = SELECT_GRID_ROWS - 1;
                    }
                    flags = &g_select_fighter_flags[g_select_grid[*row][*col]
                                                        .value];
                    g_select_picks.rec[side].hover =
                        g_select_grid[*row][*col].value;
                } while (*flags & SELECT_FIGHTER_TAKEN);
                *flags |= SELECT_FIGHTER_TAKEN;
            }
            if (g_select_pressed & PAD_OF(PADLright, pad)) {
                snd_Play(MENU_SOUND_CURSOR, SND_VOLUME_MAX, SND_VOLUME_MAX);
                *flags &= ~SELECT_FIGHTER_TAKEN;
                do {
                    (*col)++;
                    if (*col > g_select_last_col) {
                        *col = 0;
                    }
                    flags = &g_select_fighter_flags[g_select_grid[*row][*col]
                                                        .value];
                    g_select_picks.rec[side].hover =
                        g_select_grid[*row][*col].value;
                } while ((!(*flags & SELECT_FIGHTER_OPEN) &&
                          *col >= SELECT_GRID_NAME_COLS) ||
                         (*flags & SELECT_FIGHTER_TAKEN));
                *flags |= SELECT_FIGHTER_TAKEN;
            } else if (g_select_pressed & PAD_OF(PADLleft, pad)) {
                snd_Play(MENU_SOUND_CURSOR, SND_VOLUME_MAX, SND_VOLUME_MAX);
                *flags &= ~SELECT_FIGHTER_TAKEN;
                do {
                    (*col)--;
                    if (*col < 0) {
                        *col = g_select_last_col;
                    }
                    flags = &g_select_fighter_flags[g_select_grid[*row][*col]
                                                        .value];
                    g_select_picks.rec[side].hover =
                        g_select_grid[*row][*col].value;
                } while ((!(*flags & SELECT_FIGHTER_OPEN) &&
                          *col >= SELECT_GRID_NAME_COLS) ||
                         (*flags & SELECT_FIGHTER_TAKEN));
                *flags |= SELECT_FIGHTER_TAKEN;
            }
            g_select_fighter_pulse[side] = SELECT_PULSE_CHOOSING;
            g_select_fighter_stamp[side] = 0;
        } else {
            g_select_fighter_pulse[side] = SELECT_PULSE_PICKED;
            if (g_select_work->fighter_frames[side] != 0) {
                g_select_work->fighter_frames[side]--;
            }
            if (g_select_fighter_stamp[side] < SELECT_STAMP_CELLS) {
                g_select_fighter_stamp[side]++;
            }
            draw.header = PTR(sheets[CURSOR_SHEET_STAMP]);
            draw.header->count = g_select_fighter_stamp[side];
            draw.table = draw.header->cells;
            draw.has_color = 0;
            draw.x = *col * GRID_COL_PITCH + GRID_STAMP_X +
                     (*col >> 1) * GRID_PAIR_GAP;
            draw.y = *row * GRID_ROW_PITCH + GRID_Y;
            draw.ot_index = SELECT_LAYER_CURSORS;
            draw.sprt_out = ctx->prims.sprt;
            ctx->prims.sprt = sprite_Draw(&draw);
        }
        if ((g_select_pressed & PAD_OF(PADRdown, pad)) &&
            g_select_picks.rec[side].fighter == SELECT_NONE) {
            if (*flags & SELECT_FIGHTER_OPEN) {
                g_select_picks.rec[side].weapon = SELECT_NONE;
                g_select_weapon_cursors[side] = SELECT_WEAPON_START;
                g_select_picks.rec[side].fighter =
                    g_select_grid[*row][*col].fighter;
                snd_Play(g_select_grid[*row][*col].value +
                             SELECT_SOUND_FIGHTER_FIRST,
                         SND_VOLUME_MAX, SND_VOLUME_MAX);
                g_select_weapon_fly[0] = SELECT_WEAPON_FLY_1;
                g_select_weapon_fly[1] = SELECT_WEAPON_FLY_2;
                if (g_select_mode == FIGHT_MODE_PRACTICE &&
                    (g_select_settings->practice_round) && side == 0) {
                    if (g_select_fighter_flags[g_select_picks.rec[0].hover] &
                        SELECT_FIGHTER_FIRST_CLAN) {
                        g_select_fighter_cols[1] = SELECT_SECOND_START_COL;
                    } else {
                        g_select_fighter_cols[1] = 0;
                    }
                    g_select_fighter_rows[1] = 0;
                    if (g_select_work->fighter_frames[0] == 0) {
                        g_select_picks.rec[1].hover =
                            g_select_grid[0][g_select_fighter_cols[1]].value;
                        g_select_fighter_flags
                            [g_select_grid[0][g_select_fighter_cols[1]]
                                 .value] |= SELECT_FIGHTER_TAKEN;
                    }
                    break;
                }
            } else {
                snd_Play(SELECT_SOUND_LOCKED, SND_VOLUME_MAX, SND_VOLUME_MAX);
            }
        } else if (g_select_pressed & PAD_OF(PADRup, pad)) {
            if (g_select_door == SELECT_DOOR_OPEN) {
                snd_Play(MENU_SOUND_CANCEL, SND_VOLUME_MAX, SND_VOLUME_MAX);
                if (g_select_picks.rec[side].fighter == SELECT_NONE &&
                    g_select_extra_fighters == 0) {
                    g_select_result = PAGE_CANCELED;
                } else if (g_select_extra_fighters == 1) {
                    if (g_select_picks.rec[1].fighter == SELECT_NONE) {
                        g_select_extra_fighters = 0;
                        g_select_picks.rec[0].fighter = SELECT_NONE;
                    } else {
                        g_select_picks.rec[1].fighter = SELECT_NONE;
                    }
                    *flags &= ~SELECT_FIGHTER_TAKEN;
                } else if (g_select_picks.rec[side].fighter != SELECT_NONE &&
                           (*flags & SELECT_FIGHTER_OPEN)) {
                    g_select_picks.rec[side].fighter = SELECT_NONE;
                }
            }
        }
        if (g_select_work->fighter_frames[side] != 0) {
            draw.has_color = 1;
        } else {
            draw.has_color = 0;
        }
        /* the first pad's cursor for the second side is the sheet after */
        if (g_select_extra_fighters != 0 && side != 0) {
            draw.header = PTR(sheets[side + 1]);
        } else {
            draw.header = PTR(sheets[side]);
        }
        draw.table = draw.header->cells;
        draw.col_r = draw.col_g = draw.col_b = SELECT_PULSE(
            g_select_fighter_pulse[side], side * SELECT_PULSE_SIDE_PHASE);
        draw.x =
            *col * GRID_COL_PITCH + GRID_CURSOR_X + (*col >> 1) * GRID_PAIR_GAP;
        draw.y = *row * GRID_ROW_PITCH + GRID_Y;
        draw.ot_index = SELECT_LAYER_CURSORS;
        draw.sprt_out = ctx->prims.sprt;
        ctx->prims.sprt = sprite_Draw(&draw);
    }
    if (g_select_step == SELECT_STEP_FIGHTER &&
        g_select_picks.rec[0].fighter != SELECT_NONE &&
        g_select_work->fighter_frames[0] == 0) {
        if (g_select_mode == FIGHT_MODE_PRACTICE &&
            g_select_settings->practice_round) {
            g_select_extra_fighters = 1;
        }
        if ((g_select_picks.rec[1].fighter != SELECT_NONE &&
             g_select_work->fighter_frames[1] == 0) ||
            g_select_extra_players + g_select_extra_fighters == 0) {
            g_select_door = SELECT_DOOR_RIGHT_CLOSING;
            g_select_extra_weapons = 0;
            if (g_select_mode == FIGHT_MODE_PRACTICE) {
                g_select_picks.rec[1].weapon = SELECT_NONE;
                g_select_weapon_cursors[1] = SELECT_WEAPON_START;
            }
            g_select_next_step = SELECT_STEP_WEAPON;
        }
    }
    snd_FlushQueue();
}

/* ---- the row of weapons ---- */

/* SELECT_STEP_WEAPON: the row of six weapons, over it a pair of cells for
 * the place each side's cursor is on, and the cursors. */
void select_WeaponStep(MenuDrawCtx *ctx) {
    s32 abr = SPRITE_ABR_ADD_QUARTER;
    SpriteDraw draw;
    u16 rule[4];
    SpriteSheetSlot *sheets;
    s32 side;
    SpriteCell *cells;
    u8 fighter;

    draw.semi = 0;
    draw.x = 0;
    draw.y = 0;
    draw.scale_x = SPRITE_SCALE_ONE;
    draw.scale_y = SPRITE_SCALE_ONE;
    draw.has_color = 0;
    sheets = PTR(g_select_file->weapon_row_sheets);
    /* (the original sets both twice) */
    draw.y = 0;
    draw.x = 0;
    draw.header = PTR(sheets[SELECT_WEAPON_ROW_SHEET_TITLE]);
    cells = draw.header->cells;
    draw.table = cells;
    draw.sprt_out = ctx->prims.sprt;
    draw.ot_index = SELECT_LAYER_TOP;
    ctx->prims.sprt = sprite_Draw(&draw);
    SetDrawMode(ctx->prims.mode, 1, 0, sprite_GetTPage(draw.header, abr), 0);
    AddPrim(g_disp_ot + SELECT_LAYER_TOP, ctx->prims.mode);
    ctx->prims.mode++;
    rule[2] = SELECT_WEAPON_RULE_WIDTH;
    rule[0] = SELECT_WEAPON_RULE_X;
    rule[1] = MENU_TITLE_RULE_Y;
    rule[3] = 1;
    /* The rule is drawn by the main menu's function, which wants its own
     * context and is handed this one as it is: a word out, so the tile is
     * built where this screen keeps its draw modes (the original's
     * behaviour). */
    menu_DrawTitleRule((MenuCtx *)ctx, rule, SELECT_LAYER_TOP);
    /* the row: `side` counts its places here */
    draw.header = PTR(sheets[SELECT_WEAPON_ROW_SHEET_WEAPONS]);
    cells = draw.header[SELECT_WEAPON_PLACES - 1].cells;
    draw.table = cells;
    for (side = 0; side < SELECT_WEAPON_PLACES; side++) {
        draw.x = side * SELECT_WEAPON_PITCH;
        draw.sprt_out = ctx->prims.sprt;
        draw.ot_index = SELECT_LAYER_NAMES;
        ctx->prims.sprt = sprite_Draw(&draw);
        draw.header++;
    }
    draw.header = PTR(sheets[SELECT_WEAPON_ROW_SHEET_WEAPONS]);
    SetDrawMode(ctx->prims.mode, 1, 0, sprite_GetTPage(draw.header, abr), 0);
    AddPrim(g_disp_ot + SELECT_LAYER_NAMES, ctx->prims.mode);
    ctx->prims.mode++;
    draw.header = PTR(sheets[SELECT_WEAPON_ROW_SHEET_NAMES]);
    for (side = 0; side < 1 + g_select_extra_players + g_select_extra_weapons;
         side++) {
        fighter = g_select_picks.rec[side].fighter;
        if ((fighter != SELECT_FIGHTER_FIXED_1) &&
            (fighter != SELECT_FIGHTER_FIXED_2)) {
            cells = draw.header->cells;
            draw.table = cells;
            draw.table +=
                g_select_weapon_cursors[side] * SELECT_WEAPON_NAME_CELLS;
            if (((g_select_extra_players + g_select_extra_weapons) != 0) ||
                (g_select_mode == FIGHT_MODE_PRACTICE)) {
                draw.x = SELECT_WEAPON_NAME_X + side * SELECT_WEAPON_NAME_PITCH;
            } else {
                draw.x = SELECT_WEAPON_NAME_ALONE_X;
            }
            draw.sprt_out = ctx->prims.sprt;
            draw.ot_index = SELECT_LAYER_TOP;
            ctx->prims.sprt = sprite_Draw(&draw);
        }
    }
    SetDrawMode(ctx->prims.mode, 1, 0, sprite_GetTPage(draw.header, abr), 0);
    AddPrim(g_disp_ot + SELECT_LAYER_TOP, ctx->prims.mode);
    ctx->prims.mode++;
    select_MoveWeaponCursors(ctx, &draw);
    select_DrawPortraits(ctx);
    select_DrawBanner(ctx);
    select_DrawFrame(ctx);
}

/* The weapon row's cursors. A clan has five of the six weapons, so a
 * side's cursor keeps to its fighter's clan's five (in training against
 * the ninja the opponent's are the other clan's). Cross picks, and the
 * weapon's picture for the clan is loaded for the crossing; chanbara
 * gives both sides the katana at once. A fighter that comes with its
 * weapon has no cursor and waits for the other side. Triangle takes the
 * pick back or goes back to the grid. When every side has picked and been
 * shown, the doors stay shut and the weapons come in. */
static void select_MoveWeaponCursors(MenuDrawCtx *ctx, SpriteDraw *draw) {
    SpriteSheetSlot *sheets;
    s32 skipped;
    SpriteCell *cells;
    s16 side;
    s16 pad;
    s16 extra;

    skipped = 0;
    sheets = PTR(g_select_file->cursor_sheets);
    draw->header = PTR(sheets[CURSOR_SHEET_SIDE_1]);
    SetDrawMode(ctx->prims.mode, 1, 0,
                sprite_GetTPage(draw->header, SPRITE_ABR_ADD_QUARTER), 0);
    AddPrim(g_disp_ot + SELECT_LAYER_CURSORS_MODE, ctx->prims.mode);
    ctx->prims.mode++;
    draw->has_color = 1;
    draw->scale_x = SPRITE_SCALE_ONE;
    draw->scale_y = SPRITE_SCALE_ONE;
    for (side = 0; side < 1 + g_select_extra_players + g_select_extra_weapons;
         side++) {
        if (g_select_door == SELECT_DOOR_OPEN) {
            if (g_select_picks.rec[side].fighter == SELECT_FIGHTER_FIXED_1) {
                g_select_picks.rec[side].weapon = SELECT_WEAPON_FIXED_1;
                skipped = 1;
                g_select_work->weapon_frames[side] = 0;
            } else if (
                g_select_picks.rec[side].fighter == SELECT_FIGHTER_FIXED_2) {
                g_select_picks.rec[side].weapon = SELECT_WEAPON_FIXED_2;
                skipped = 1;
                g_select_work->weapon_frames[side] = 0;
            }
        }
    }
    for (side = 0;
         side < 1 + g_select_extra_players + (extra = g_select_extra_weapons);
         side++) {
        /* where in video memory this side's weapon pictures go */
        RECT *rects;

        /* FAKE: weapon_rects + side * 8, in two steps */
        rects = PTR(g_select_file->weapon_rects);
        rects += side * WEAPON_ID_COUNT;
        /* the first pad, for the second side, while it picks for both */
        pad = side - extra;
        if (g_select_picks.rec[side].fighter != SELECT_FIGHTER_FIXED_1 &&
            g_select_picks.rec[side].fighter != SELECT_FIGHTER_FIXED_2) {
            /* the clan's five places of the row */
            s32 last_place;
            s32 first_place;

            if (g_select_mode == FIGHT_MODE_PRACTICE &&
                !(g_select_settings->practice_round) && side == 1) {
                if (g_select_fighter_flags[g_select_picks.rec[0].hover] &
                    SELECT_FIGHTER_FIRST_CLAN) {
                    last_place = SELECT_WEAPON_PLACES - 1;
                    first_place = 1;
                } else {
                    last_place = SELECT_WEAPON_PLACES - 2;
                    first_place = 0;
                }
            } else if (g_select_fighter_flags[g_select_picks.rec[side].hover] &
                       SELECT_FIGHTER_FIRST_CLAN) {
                last_place = SELECT_WEAPON_PLACES - 2;
                first_place = 0;
            } else {
                last_place = SELECT_WEAPON_PLACES - 1;
                first_place = 1;
            }
            if (g_select_picks.rec[side].weapon == SELECT_NONE) {
                g_select_weapon_pulse[side] = SELECT_PULSE_CHOOSING;
                g_select_weapon_stamp[side] = 0;
                g_select_work->weapon_frames[side] = SELECT_PICK_FRAMES;
                if ((g_select_pressed & PAD_OF(PADLright, pad)) &&
                    g_select_weapon_cursors[side] < last_place) {
                    snd_Play(MENU_SOUND_CURSOR, SND_VOLUME_MAX, SND_VOLUME_MAX);
                    g_select_weapon_cursors[side]++;
                } else if ((g_select_pressed & PAD_OF(PADLleft, pad)) &&
                           first_place < g_select_weapon_cursors[side]) {
                    snd_Play(MENU_SOUND_CURSOR, SND_VOLUME_MAX, SND_VOLUME_MAX);
                    g_select_weapon_cursors[side]--;
                }
                g_select_weapon_cursor_rows[side] = side;
                if (g_select_mode == FIGHT_MODE_CHANBARA) {
                    s32 clan;
                    s32 weapon;
                    /* FAKE: the address of the picture, through a local */
                    s32 *pixels;

                    snd_Play(
                        MENU_SOUND_CONFIRM, SND_VOLUME_MAX, SND_VOLUME_MAX);
                    g_select_picks.rec[1].weapon = WEAPON_ID_KATANA;
                    g_select_picks.rec[0].weapon = WEAPON_ID_KATANA;
                    if (g_select_fighter_flags[g_select_picks.rec[side].hover] &
                        SELECT_FIGHTER_FIRST_CLAN) {
                        clan = 0;
                    } else {
                        clan = 1;
                    }
                    weapon = g_select_picks.rec[side].weapon;
                    pixels = &g_select_file->weapon_pixels[weapon][clan];
                    LoadImage(&rects[weapon], (u32 *)ADDR(*pixels));
                    DrawSync(0);
                } else if (g_select_pressed & PAD_OF(PADRdown, pad)) {
                    s32 clan;
                    s32 weapon;
                    /* FAKE: the address of the picture, through a local */
                    s32 *pixels;

                    snd_Play(
                        MENU_SOUND_CONFIRM, SND_VOLUME_MAX, SND_VOLUME_MAX);
                    g_select_picks.rec[side].weapon =
                        g_select_weapon_ids[g_select_weapon_cursors[side]];
                    clan = 1;
                    if (g_select_mode == FIGHT_MODE_PRACTICE &&
                        !(g_select_settings->practice_round) && side == 1) {
                        if (!(g_select_fighter_flags[g_select_picks.rec[0]
                                                         .hover] &
                              SELECT_FIGHTER_FIRST_CLAN)) {
                            clan = 0;
                        }
                    } else if (
                        g_select_fighter_flags[g_select_picks.rec[side].hover] &
                        SELECT_FIGHTER_FIRST_CLAN) {
                        clan = 0;
                    }
                    weapon = g_select_picks.rec[side].weapon;
                    pixels = &g_select_file->weapon_pixels[weapon][clan];
                    LoadImage(&rects[weapon], (u32 *)ADDR(*pixels));
                    DrawSync(0);
                } else if (g_select_pressed & PAD_OF(PADRup, pad)) {
                    snd_Play(MENU_SOUND_CANCEL, SND_VOLUME_MAX, SND_VOLUME_MAX);
                    if (g_select_extra_weapons == 1) {
                        g_select_extra_weapons = 0;
                        g_select_picks.rec[0].weapon = SELECT_NONE;
                    } else {
                        g_select_picks.rec[1].weapon = SELECT_NONE;
                        g_select_picks.rec[0].weapon = SELECT_NONE;
                        g_select_door = SELECT_DOOR_LEFT_CLOSING;
                        g_select_next_step = SELECT_STEP_FIGHTER;
                        snd_Play(SELECT_SOUND_DOOR_CLOSE, SND_VOLUME_MAX,
                                 SND_VOLUME_MAX);
                        if (g_select_extra_fighters == 1) {
                            g_select_picks.rec[1].fighter = SELECT_NONE;
                        } else {
                            g_select_picks.rec[side].fighter = SELECT_NONE;
                        }
                        break;
                    }
                }
            } else {
                if ((g_select_pressed & PAD_OF(PADRup, pad)) &&
                    g_select_door == SELECT_DOOR_OPEN &&
                    g_select_mode != FIGHT_MODE_CHANBARA) {
                    snd_Play(MENU_SOUND_CANCEL, SND_VOLUME_MAX, SND_VOLUME_MAX);
                    g_select_weapon_fly[0] = SELECT_WEAPON_FLY_1;
                    g_select_weapon_fly[1] = SELECT_WEAPON_FLY_2;
                    g_select_picks.rec[side].weapon = SELECT_NONE;
                }
                g_select_weapon_pulse[side] = SELECT_PULSE_PICKED;
                if (g_select_work->weapon_frames[side] != 0) {
                    g_select_work->weapon_frames[side]--;
                }
                /* (the original loads the sheets again here) */
                sheets = PTR(g_select_file->cursor_sheets);
                draw->header = PTR(sheets[CURSOR_SHEET_STAMP]);
                if (g_select_weapon_stamp[side] < SELECT_STAMP_CELLS) {
                    g_select_weapon_stamp[side]++;
                }
                draw->header->count = g_select_weapon_stamp[side];
                draw->has_color = 0;
                cells = draw->header->cells;
                draw->table = cells;
                draw->x = g_select_weapon_cursors[side] * SELECT_WEAPON_PITCH +
                          SELECT_WEAPON_STAMP_X;
                draw->y = SELECT_WEAPON_STAMP_Y -
                          g_select_weapon_cursor_rows[side] *
                              SELECT_WEAPON_LINE_PITCH;
                draw->ot_index = SELECT_LAYER_CURSORS;
                draw->sprt_out = ctx->prims.sprt;
                ctx->prims.sprt = sprite_Draw(draw);
            }
            if (g_select_door != SELECT_DOOR_LEFT_CLOSING) {
                draw->scale_x = SPRITE_SCALE_ONE;
                draw->scale_y = SPRITE_SCALE_ONE;
                if (g_select_work->weapon_frames[side] != 0) {
                    draw->has_color = 1;
                } else {
                    draw->has_color = 0;
                }
                if (g_select_extra_weapons != 0 && side != 0) {
                    draw->header = PTR(sheets[side + 1]);
                } else {
                    draw->header = PTR(sheets[side]);
                }
                cells = draw->header->cells;
                draw->table = cells;
                draw->col_r = draw->col_g = draw->col_b =
                    SELECT_PULSE(g_select_weapon_pulse[side],
                                 side * SELECT_PULSE_SIDE_PHASE);
                draw->x = g_select_weapon_cursors[side] * SELECT_WEAPON_PITCH +
                          SELECT_WEAPON_CURSOR_X;
                draw->y = SELECT_WEAPON_CURSOR_Y -
                          g_select_weapon_cursor_rows[side] *
                              SELECT_WEAPON_LINE_PITCH;
                draw->ft4_out = ctx->prims.ft4;
                draw->ot_index = SELECT_LAYER_CURSORS;
                ctx->prims.ft4 = sprite_DrawScaled(draw, SPRITE_QUAD_NORMAL);
            }
        } else {
            if (g_select_door == SELECT_DOOR_OPEN) {
                if (g_select_picks.rec[side == 0 ? 1 : 0].weapon !=
                        SELECT_NONE &&
                    g_select_work->weapon_frames[side == 0 ? 1 : 0] == 0) {
                    s32 clan;
                    s32 weapon;
                    /* FAKE: the address of the picture, through a local */
                    s32 *pixels;

                    snd_Play(
                        MENU_SOUND_CONFIRM, SND_VOLUME_MAX, SND_VOLUME_MAX);
                    skipped = 2;
                    /* (the original works `rects` out again here) */
                    rects = PTR(g_select_file->weapon_rects);
                    rects += side * WEAPON_ID_COUNT;
                    if (g_select_fighter_flags[g_select_picks.rec[side].hover] &
                        SELECT_FIGHTER_FIRST_CLAN) {
                        clan = 0;
                    } else {
                        clan = 1;
                    }
                    weapon = g_select_picks.rec[side].weapon;
                    pixels = &g_select_file->weapon_pixels[weapon][clan];
                    LoadImage(&rects[weapon], (u32 *)ADDR(*pixels));
                    DrawSync(0);
                }
            }
            /* the side's own pad, where every other test here reads the pad
             * that picks for it: the original's behaviour */
            if (g_select_pressed & PAD_OF(PADRup, side)) {
                snd_Play(MENU_SOUND_CANCEL, SND_VOLUME_MAX, SND_VOLUME_MAX);
                g_select_door = SELECT_DOOR_LEFT_CLOSING;
                snd_Play(
                    SELECT_SOUND_DOOR_CLOSE, SND_VOLUME_MAX, SND_VOLUME_MAX);
                g_select_next_step = SELECT_STEP_FIGHTER;
                g_select_picks.rec[1].weapon = SELECT_NONE;
                g_select_picks.rec[0].weapon = SELECT_NONE;
                if (g_select_extra_weapons == 1) {
                    g_select_extra_weapons = 0;
                    g_select_picks.rec[1].fighter = SELECT_NONE;
                } else {
                    g_select_picks.rec[side].fighter = SELECT_NONE;
                }
                g_select_work->weapon_frames[side] = SELECT_PICK_FRAMES;
            }
        }
    }
    /* each side's clan's emblem at half size, where its cursor is */
    sheets = PTR(g_select_file->fighter_sheets);
    draw->header = PTR(sheets[SELECT_FIGHTER_SHEET_EMBLEMS]);
    draw->scale_x = SPRITE_SCALE_ONE / 2;
    draw->scale_y = SPRITE_SCALE_ONE / 2;
    draw->has_color = 0;
    draw->ot_index = SELECT_LAYER_EMBLEMS;
    cells = draw->header[EMBLEM_LOOKS - 1].cells;
    for (side = 0; side < 1 + g_select_extra_players + g_select_extra_weapons;
         side++) {
        if (g_select_picks.rec[side].fighter != SELECT_FIGHTER_FIXED_1 &&
            g_select_picks.rec[side].fighter != SELECT_FIGHTER_FIXED_2 &&
            g_select_door != SELECT_DOOR_LEFT_CLOSING) {
            draw->table = cells;
            draw->x = g_select_weapon_cursors[side] * SELECT_WEAPON_PITCH +
                      SELECT_WEAPON_CURSOR_X;
            draw->y =
                SELECT_WEAPON_CURSOR_Y -
                g_select_weapon_cursor_rows[side] * SELECT_WEAPON_LINE_PITCH;
            if (g_select_mode == FIGHT_MODE_PRACTICE &&
                !(g_select_settings->practice_round) && side == 1) {
                if (g_select_fighter_flags[g_select_picks.rec[0].hover] &
                    SELECT_FIGHTER_FIRST_CLAN) {
                    draw->table += draw->header->count;
                }
            } else if (
                !(g_select_fighter_flags[g_select_picks.rec[side].hover] &
                  SELECT_FIGHTER_FIRST_CLAN)) {
                draw->table += draw->header->count;
            }
            draw->ft4_out = ctx->prims.ft4;
            ctx->prims.ft4 = sprite_DrawScaled(draw, SPRITE_QUAD_NORMAL);
        }
    }
    if (g_select_picks.rec[0].weapon != SELECT_NONE &&
        g_select_work->weapon_frames[0] == 0) {
        if (g_select_mode == FIGHT_MODE_PRACTICE) {
            g_select_extra_weapons = 1;
        }
        if (((g_select_picks.rec[1].weapon != SELECT_NONE &&
              g_select_work->weapon_frames[1] == 0) ||
             g_select_extra_players + g_select_extra_weapons == 0) &&
            skipped != 1) {
            g_select_door = SELECT_DOOR_RIGHT_CLOSING | SELECT_DOOR_STAYS_SHUT;
            g_select_next_step = SELECT_STEP_WEAPONS_IN;
            g_select_fade = 1;
            g_select_weapons_frames = 0;
        }
    }
}

/* ---- the crossed weapons go out ---- */

/* The stage the one-player modes start on, which goes by the first side's
 * clan. */
static s32 select_GetClanStage(void) {
    s32 stage_index;
    stage_index = STAGE_TATARA_SHRINE;
    if (g_select_fighter_flags[g_select_picks.rec[0].hover] &
        SELECT_FIGHTER_FIRST_CLAN) {
        stage_index = STAGE_FUTURE_DUMP_SITE;
    }
    return stage_index;
}

/* SELECT_STEP_WEAPONS_OUT: the crossed weapons stay while the screen fades
 * to black. Then the picks go into the settings, and the screen is done,
 * or goes on to the stages: versus mode to the second page, link mode to
 * the third. */
void select_WeaponsOutStep(MenuDrawCtx *ctx) {
    SpriteDraw draw;
    s32 side;
    SpriteSheetSlot *sheets;
    SpriteSheet *sheet;
    s32 look;
    s16 width;
    s16 height;

    sheets = PTR(g_select_file->weapon_sheets);
    for (side = 0; side < 1 + g_select_extra_players + g_select_extra_weapons;
         side++) {
        if (g_select_picks.rec[side].fighter != SELECT_FIGHTER_FIXED_1 &&
            g_select_picks.rec[side].fighter != SELECT_FIGHTER_FIXED_2) {
            draw.ot_index = SELECT_LAYER_TOP;
            draw.semi = 0;
            draw.scale_x = SPRITE_SCALE_ONE;
            draw.scale_y = SPRITE_SCALE_ONE;
            draw.has_color = 0;
            sheet = PTR(sheets[g_select_picks.rec[side].weapon]);
            look = 1;
            if (g_select_mode == FIGHT_MODE_PRACTICE &&
                !(g_select_settings->practice_round) && side == 1) {
                if (g_select_fighter_flags[g_select_picks.rec[0].hover] &
                    SELECT_FIGHTER_FIRST_CLAN) {
                    look = 1;
                } else {
                    look = 0;
                }
            } else if (g_select_fighter_flags[g_select_picks.rec[side].hover] &
                       SELECT_FIGHTER_FIRST_CLAN) {
                look = 0;
            }
            draw.header = &sheet[look];
            if (side != 0) {
                draw.header->ubase = SELECT_WEAPON_SECOND_SIDE_U;
            } else {
                draw.header->ubase = 0;
            }
            draw.table = sheet[SELECT_WEAPON_LOOKS - 1].cells;
            /* the picture's size, as in select_WeaponsInStep */
            {
                s32 index = draw.header->count - 1;
                width = draw.table[index].x + draw.table[index].w -
                        sheet[SELECT_WEAPON_LOOKS - 1].cells[0].x;
                height = draw.table[index].y + draw.table[index].h -
                         sheet[SELECT_WEAPON_LOOKS - 1].cells[0].y;
            }
            draw.x =
                g_select_weapon_places[side][g_select_weapon_cursors[side]].x +
                DISP_WIDTH / 2 - ((width * draw.scale_x >> 8) / 2);
            draw.y =
                g_select_weapon_places[side][g_select_weapon_cursors[side]].y +
                WEAPONS_Y - ((height * draw.scale_y >> 8) / 2);
            draw.ft4_out = ctx->prims.ft4;
            if (side != 0) {
                ctx->prims.ft4 = sprite_DrawRotated(
                    &draw, SELECT_WEAPON_TILT, SPRITE_QUAD_MIRROR_X);
            } else {
                ctx->prims.ft4 = sprite_DrawRotated(
                    &draw, ANGLE_TURN - SELECT_WEAPON_TILT, SPRITE_QUAD_NORMAL);
            }
        }
    }

    g_select_fade += FADE_OUT_STEP;
    if (g_select_fade >= FADE_FULL) {
        g_select_fade = FADE_FULL;
        g_select_door = SELECT_DOOR_RIGHT_CLOSING;
        snd_Play(SELECT_SOUND_DOOR_CLOSE, SND_VOLUME_MAX, SND_VOLUME_MAX);
#if RB_PORT
        /* versus the CPU and training to the stage pages too, with
         * stage_select_everywhere (port_play.h), on the page versus mode
         * opens on */
        if (port_PlayGoesToStages(g_select_mode)) {
            g_select_next_step = SELECT_STEP_STAGE_2;
        } else
#endif
            if (g_select_mode >= FIGHT_MODE_STORY &&
                g_select_mode <= FIGHT_MODE_VSCPU) {
            g_select_settings->stage_index = select_GetClanStage();
            g_select_result = PAGE_CONFIRMED;
        } else if (g_select_mode == FIGHT_MODE_VS) {
            g_select_next_step = SELECT_STEP_STAGE_2;
        } else if (g_select_mode == FIGHT_MODE_NETWORK) {
            g_select_next_step = SELECT_STEP_STAGE_3;
            g_select_stage_row = 1;
            g_select_stage_col = 1;
        } else {
            g_select_result = PAGE_CONFIRMED;
        }
        for (side = 0;
             side < 1 + g_select_extra_players + g_select_extra_fighters;
             side++) {
            g_select_settings->match_picks[side][0].fighter =
                g_select_picks.rec[side].fighter;
        }
        for (side = 0;
             side < 1 + g_select_extra_players + g_select_extra_weapons;
             side++) {
            g_select_settings->match_picks[side][0].weapon =
                g_select_picks.rec[side].weapon;
        }
    }
    select_DrawFade(ctx);
}

/* ---- the stages ---- */

/* SELECT_STEP_STAGE_1 to 3. */
void select_StagePage1Step(MenuDrawCtx *ctx) {
    select_StageStep(ctx, PTR(g_select_file->stage_page1_sheets), 0);
}

void select_StagePage2Step(MenuDrawCtx *ctx) {
    select_StageStep(ctx, PTR(g_select_file->stage_page2_sheets), 1);
}

void select_StagePage3Step(MenuDrawCtx *ctx) {
    select_StageStep(ctx, PTR(g_select_file->stage_page3_sheets), 2);
}

/* A page of stages: six pictures in three rows of two, cut out of one
 * picture of the stages that scrolls from page to page, with the name of
 * the stage under the cursor. Left and right over the edge go to the next
 * page, through the doors (stage_codes says which page and which door).
 * Cross picks the stage and ends the screen; triangle goes back to the
 * weapons, or to the grid for a fighter that has no choice of weapon. In
 * link mode one place of the third page is another stage, and not there to
 * be read. `i` counts the places, then the two scrolls, the rows and the
 * pads. */
static void select_StageStep(
    MenuDrawCtx *ctx, SpriteSheetSlot *sheets, s32 page) {
    SpriteDraw draw;
    u16 rule[4];
    RECT rect;
    u8 *codes;
    SpriteSheetSlot *cursor_sheets;
    SpriteCell *cells;
    s32 i;
    s32 col;
    s32 travel;
    u8 edge;
    u8 stage;

    codes = PTR(g_select_file->stage_codes);
    draw.semi = 0;
    draw.has_color = 0;
    draw.ot_index = SELECT_LAYER_STAGES;
    rect.x = g_select_db->draw.clip.x;
    rect.y = g_select_db->draw.clip.y;
    rect.w = g_select_db->draw.clip.w;
    rect.h = g_select_db->draw.clip.h;
    SetDrawArea(ctx->prims.area, &rect);
    AddPrim(g_disp_ot + draw.ot_index, ctx->prims.area);
    ctx->prims.area++;
    g_select_work->draw_offset[0] = g_select_db->draw.ofs[0];
    g_select_work->draw_offset[1] = g_select_db->draw.ofs[1];
    SetDrawOffset(ctx->prims.offset, g_select_work->draw_offset);
    AddPrim(g_disp_ot + draw.ot_index, ctx->prims.offset);
    ctx->prims.offset++;

    /* the stages' names: the one under the cursor, then the others */
    if ((g_select_door & SELECT_DOOR_STATE) == SELECT_DOOR_OPEN) {
        draw.x = SELECT_STAGE_NAMES_X;
        draw.y = SELECT_STAGE_NAMES_Y;
        cursor_sheets = PTR(g_select_file->cursor_sheets);
        draw.header = PTR(cursor_sheets[CURSOR_SHEET_STAGE_NAMES]);
        cells = draw.header[1].cells;
        if (!(g_select_stage_row * SELECT_STAGE_PAGE_COLS +
                      g_select_stage_col ==
                  SELECT_STAGE_LINK_PLACE &&
              g_select_mode == FIGHT_MODE_NETWORK &&
              page == SELECT_STAGE_LINK_PAGE)) {
            draw.table = cells;
            draw.table += (g_select_stage_row + page * SELECT_STAGE_PAGE_ROWS) *
                              SELECT_STAGE_PAGE_COLS +
                          g_select_stage_col;
            draw.sprt_out = ctx->prims.sprt;
            ctx->prims.sprt = sprite_Draw(&draw);
        }
        draw.header++;
        for (i = 0; i < SELECT_STAGE_PAGE_PLACES; i++) {
            if (!(i == SELECT_STAGE_LINK_PLACE &&
                  g_select_mode == FIGHT_MODE_NETWORK &&
                  page == SELECT_STAGE_LINK_PAGE) &&
                g_select_stage_row * SELECT_STAGE_PAGE_COLS +
                        g_select_stage_col !=
                    i) {
                draw.table = cells + (page * SELECT_STAGE_PAGE_PLACES + i);
                draw.sprt_out = ctx->prims.sprt;
                ctx->prims.sprt = sprite_Draw(&draw);
            }
        }
        draw.header = PTR(cursor_sheets[CURSOR_SHEET_STAGE_NAMES]);
        SetDrawMode(ctx->prims.mode, 1, 0, sprite_GetTPage(draw.header, 0), 0);
        AddPrim(g_disp_ot + draw.ot_index, ctx->prims.mode);
        ctx->prims.mode++;
    }

    draw.x = STAGES_WINDOW_X;
    draw.y = STAGES_WINDOW_Y;
    draw.header = PTR(sheets[SELECT_STAGE_SHEET_UNDER]);
    cells = draw.header->cells;
    draw.table = cells;
    draw.sprt_out = ctx->prims.sprt;
    ctx->prims.sprt = sprite_Draw(&draw);
    g_select_work->draw_offset[0] =
        g_select_db->draw.ofs[0] - g_select_stage_scrolls[page][0];
    g_select_work->draw_offset[1] =
        g_select_db->draw.ofs[1] - g_select_stage_scrolls[page][1];
    /* a door is closing on another page of stages: the picture scrolls
     * towards that page's place as the door comes across */
    if (((g_select_door & SELECT_DOOR_STATE) == SELECT_DOOR_RIGHT_CLOSING ||
         (g_select_door & SELECT_DOOR_STATE) == SELECT_DOOR_LEFT_CLOSING) &&
        g_select_next_step >= SELECT_STEP_STAGE_1 &&
        g_select_next_step < SELECT_STEP_COUNT &&
        g_select_step >= SELECT_STEP_STAGE_1 &&
        g_select_step < SELECT_STEP_COUNT) {
        for (i = 0; i < 2; i++) {
            travel =
                g_select_stage_scrolls[g_select_next_step - SELECT_STEP_STAGE_1]
                                      [i] -
                g_select_stage_scrolls[g_select_step - SELECT_STEP_STAGE_1][i];
            travel *= SELECT_STAGE_SCROLL_UNIT;
            if ((travel >= 0 ? travel : -travel) >
                (g_select_stage_scroll[i] >= 0 ? g_select_stage_scroll[i]
                                               : -g_select_stage_scroll[i])) {
                g_select_stage_scroll[i] +=
                    travel * SELECT_DOOR_SPEED / SELECT_DOOR_SHUT_X;
            } else {
                g_select_stage_scroll[i] = travel;
            }
            g_select_work->draw_offset[i] -=
                g_select_stage_scroll[i] / SELECT_STAGE_SCROLL_UNIT;
        }
    } else {
        g_select_stage_scroll[1] = 0;
        g_select_stage_scroll[0] = 0;
    }
    SetDrawOffset(ctx->prims.offset, g_select_work->draw_offset);
    AddPrim(g_disp_ot + draw.ot_index, ctx->prims.offset);
    ctx->prims.offset++;
    rect.x = draw.x;
    rect.y = g_select_db->draw.clip.y + draw.y;
    rect.w = STAGES_WINDOW_WIDTH;
    rect.h = STAGES_WINDOW_HEIGHT;
    SetDrawArea(ctx->prims.area, &rect);
    AddPrim(g_disp_ot + draw.ot_index, ctx->prims.area);
    ctx->prims.area++;
    SetDrawMode(ctx->prims.mode, 1, 0, sprite_GetTPage(draw.header, 0), 0);
    AddPrim(g_disp_ot + draw.ot_index, ctx->prims.mode);
    ctx->prims.mode++;

    draw.scale_x = SPRITE_SCALE_ONE;
    draw.scale_y = SPRITE_SCALE_ONE;
    draw.y = 0;
    draw.x = 0;
    draw.ot_index = SELECT_LAYER_NAMES_MODE;
    draw.header = PTR(sheets[SELECT_STAGE_SHEET_PICTURE]);
    draw.scale_x += STAGES_SCALE_X - SPRITE_SCALE_ONE;
    draw.scale_y += STAGES_SCALE_Y - SPRITE_SCALE_ONE;
    cells = draw.header->cells;
    draw.table = cells;
    draw.ft4_out = ctx->prims.ft4;
    ctx->prims.ft4 = sprite_DrawScaled(&draw, SPRITE_QUAD_NORMAL);
    draw.table += draw.header->count;
    draw.ft4_out = ctx->prims.ft4;
    ctx->prims.ft4 = sprite_DrawScaled(&draw, SPRITE_QUAD_MIRROR_X);

#if RB_PORT
    /* the port's kinds of stage (port_stage_choice.h): Square switches the
     * stage under the cursor between Classic and Remastered Classic,
     * Select opens the arenas' page, which then has the presses */
    {
        s32 chosen = codes[(g_select_step - SELECT_STEP_STAGE_1) *
                               SELECT_STAGE_PAGE_CODES +
                           g_select_stage_row * SELECT_STAGE_PAGE_COLS +
                           g_select_stage_col];
        s32 taken;

        /* a one-player mode's stage pages (port_play.h): the first pad's
         * alone, as its grid is */
        if (g_select_mode < FIGHT_MODE_VS) {
            g_select_pressed &= PAD_OF(0xFFFF, 0);
        }
        if (chosen == SELECT_STAGE_LINK_FROM &&
            g_select_mode == FIGHT_MODE_NETWORK) {
            chosen = SELECT_STAGE_LINK_TO;
        }
        taken = port_StagePageStep(
            g_select_pressed, &chosen, g_select_door == SELECT_DOOR_OPEN);
        if (taken == PORT_STAGE_PAGE_ARENA) {
            g_select_settings->stage_index = chosen;
            g_select_result = PAGE_CONFIRMED;
        }
        if (taken != PORT_STAGE_PAGE_GRID) {
            g_select_pressed = 0;
        }
    }
#endif
    if (g_select_pressed & PAD_ANY(PADLright | PADLleft)) {
        if ((g_select_door & SELECT_DOOR_STATE) == SELECT_DOOR_OPEN) {
            snd_Play(MENU_SOUND_CURSOR, SND_VOLUME_MAX, SND_VOLUME_MAX);
            if (((g_select_pressed & PAD_ANY(PADLright)) &&
                 g_select_stage_col != 0) ||
                ((g_select_pressed & PAD_ANY(PADLleft)) &&
                 g_select_stage_col == 0)) {
                edge = codes[(g_select_step - SELECT_STEP_STAGE_1) *
                                 SELECT_STAGE_PAGE_CODES +
                             SELECT_STAGE_PAGE_PLACES + g_select_stage_col];
                g_select_next_step = edge & SELECT_STAGE_EDGE_STEP;
                if (g_select_next_step != SELECT_STAGE_EDGE_NONE) {
                    g_select_door = edge >> SELECT_STAGE_EDGE_DOOR_SHIFT;
                    snd_Play(SELECT_SOUND_DOOR_CLOSE, SND_VOLUME_MAX,
                             SND_VOLUME_MAX);
                }
            }
            g_select_stage_col = g_select_stage_col == 0;
        }
    }
    if (g_select_pressed & PAD_ANY(PADLdown)) {
        snd_Play(MENU_SOUND_CURSOR, SND_VOLUME_MAX, SND_VOLUME_MAX);
        g_select_stage_row++;
    } else if (g_select_pressed & PAD_ANY(PADLup)) {
        snd_Play(MENU_SOUND_CURSOR, SND_VOLUME_MAX, SND_VOLUME_MAX);
        g_select_stage_row--;
    }
    if (g_select_stage_row >= SELECT_STAGE_PAGE_ROWS) {
        g_select_stage_row = 0;
    } else if (g_select_stage_row < 0) {
        g_select_stage_row = SELECT_STAGE_PAGE_ROWS - 1;
    }

    /* the glow of the place under the cursor */
    draw.col_r = draw.col_g = draw.col_b =
        SELECT_PULSE(SELECT_PULSE_CHOOSING, SELECT_PULSE_SIDE_PHASE);
    draw.scale_x = SPRITE_SCALE_ONE;
    draw.scale_y = SPRITE_SCALE_ONE;
    draw.y = 0;
    draw.x = 0;
    draw.ot_index = SELECT_LAYER_TOP;
    draw.header = PTR(sheets[SELECT_STAGE_SHEET_TITLE]);
    cells = draw.header->cells;
    draw.table = cells;
    draw.ft4_out = ctx->prims.ft4;
    ctx->prims.ft4 = sprite_DrawScaled(&draw, SPRITE_QUAD_NORMAL);
    rule[2] = SELECT_STAGE_RULE_WIDTH;
    rule[0] = SELECT_STAGE_RULE_X;
    rule[1] = MENU_TITLE_RULE_Y;
    rule[3] = 1;
    /* The rule is drawn by the main menu's function, which wants its own
     * context and is handed this one as it is: a word out, so the tile is
     * built where this screen keeps its draw modes (the original's
     * behaviour). */
    menu_DrawTitleRule((MenuCtx *)ctx, rule, SELECT_LAYER_TOP);
    draw.ot_index = SELECT_LAYER_NAMES;
    for (i = 0; i < SELECT_STAGE_SHEET_ROWS; i++) {
        for (col = 0; col < SELECT_STAGE_PAGE_COLS; col++) {
            if (i == g_select_stage_row && col == g_select_stage_col &&
                (g_select_door & SELECT_DOOR_STATE) == SELECT_DOOR_OPEN) {
                draw.has_color = 1;
            } else {
                draw.has_color = 0;
            }
            if (i + i + col != SELECT_STAGE_LINK_PLACE ||
                g_select_mode != FIGHT_MODE_NETWORK ||
                page != SELECT_STAGE_LINK_PAGE) {
                draw.header =
                    PTR((sheets + (i + i))[col + SELECT_STAGE_SHEET_PLACES]);
            } else {
                draw.header = PTR(sheets[SELECT_STAGE_SHEET_LINK_PLACE]);
            }
            cells = draw.header->cells;
            draw.table = cells;
            draw.ft4_out = ctx->prims.ft4;
            ctx->prims.ft4 = sprite_DrawScaled(&draw, SPRITE_QUAD_NORMAL);
        }
    }
    draw.has_color = 0;
    draw.header = PTR(sheets[SELECT_STAGE_SHEET_REST]);
    cells = draw.header->cells;
    draw.table = cells;
    draw.ft4_out = ctx->prims.ft4;
    ctx->prims.ft4 = sprite_DrawScaled(&draw, SPRITE_QUAD_NORMAL);

    if (g_select_door == SELECT_DOOR_OPEN) {
        for (i = 0; i < g_select_extra_players + 1; i++) {
            if (g_select_pressed & PAD_OF(PADRup, i)) {
                s32 fighter;

                snd_Play(MENU_SOUND_CANCEL, SND_VOLUME_MAX, SND_VOLUME_MAX);
                fighter = g_select_picks.rec[i].fighter;
                g_select_picks.rec[i].weapon = SELECT_NONE;
                if (fighter == SELECT_FIGHTER_FIXED_1 ||
                    fighter == SELECT_FIGHTER_FIXED_2) {
                    g_select_step = SELECT_STEP_FIGHTER;
                    g_select_picks.rec[i == 0 ? 1 : 0].weapon = SELECT_NONE;
                    g_select_picks.rec[i].fighter = SELECT_NONE;
                } else {
                    g_select_step = SELECT_STEP_WEAPON;
                }
                g_select_weapon_fly[0] = SELECT_WEAPON_FLY_1;
                g_select_weapon_fly[1] = SELECT_WEAPON_FLY_2;
                g_select_work->weapon_frames[1] = 0;
                g_select_work->weapon_frames[0] = 0;
                goto end;
            }
        }
        if (g_select_pressed & PAD_ANY(PADRdown)) {
            stage = codes[(g_select_step - SELECT_STEP_STAGE_1) *
                              SELECT_STAGE_PAGE_CODES +
                          g_select_stage_row * SELECT_STAGE_PAGE_COLS +
                          g_select_stage_col];
            snd_Play(MENU_SOUND_CONFIRM, SND_VOLUME_MAX, SND_VOLUME_MAX);
            if (stage != SELECT_STAGE_LINK_FROM ||
                g_select_mode != FIGHT_MODE_NETWORK) {
                g_select_settings->stage_index = stage;
            } else {
                g_select_settings->stage_index = SELECT_STAGE_LINK_TO;
            }
            g_select_result = PAGE_CONFIRMED;
#if RB_PORT
            port_StageChosen(g_select_settings->stage_index);
#endif
        }
    }
end:
    select_DrawBanner(ctx);
    select_DrawFrame(ctx);
    snd_FlushQueue();
}

/* ---- the banner and the frame ---- */

/* The upper half of one side of the banner, shaded from the edge to the
 * middle: gold on the picking steps, where it flickers, and dark blue on
 * the stage pages. The corners are in `quad` already: the outer one at the
 * top, the outer one at the banner's middle height, then the two at the
 * middle of the screen. */
static POLY_G4 *select_AddBannerTop(s32 step, POLY_G4 *quad) {
    SetPolyG4(quad);
    SetSemiTrans(quad, 0);
    if (step < SELECT_STEP_STAGE_1) {
        setRGB0(quad, 0x00, 0x00, 0x00); /* black */
        setRGB1(quad, 0xFC, 0x82, 0x00); /* orange */
        setRGB2(quad, 0xFC, 0x82, 0x00); /* orange */
        if (g_select_work->frames & FLICKER_FRAMES) {
            setRGB3(quad, 0xFC, 0xC3, 0x1E); /* yellow */
        } else {
            setRGB3(quad, 0xFC, 0xC3, 0x50); /* a paler yellow */
        }
    } else {
        setRGB0(quad, 0x00, 0x00, 0x00); /* black */
        setRGB1(quad, 0x40, 0x00, 0x80); /* violet */
        setRGB2(quad, 0x50, 0xA0, 0x40); /* green */
        setRGB3(quad, 0x10, 0x40, 0x80); /* blue */
    }
    AddPrim(g_disp_ot + SELECT_LAYER_BANNER, quad);
    return quad + 1;
}

/* The lower half, the same way: its corners are the two at the middle of
 * the screen, at the banner's middle height and at its foot, then the two
 * outer ones. */
static POLY_G4 *select_AddBannerBottom(s32 step, POLY_G4 *quad) {
    SetPolyG4(quad);
    SetSemiTrans(quad, 0);
    if (step < SELECT_STEP_STAGE_1) {
        if (g_select_work->frames & FLICKER_FRAMES) {
            setRGB0(quad, 0xFC, 0xC3, 0x1E); /* yellow */
            setRGB1(quad, 0xFC, 0xC8, 0x32); /* yellow */
        } else {
            setRGB0(quad, 0xFC, 0xC3, 0x50); /* a paler yellow */
            setRGB1(quad, 0xFC, 0xDC, 0x46); /* a paler yellow */
        }
        /* orange at the outer middle and dark brown at the outer foot,
         * stored in this order */
        quad->r2 = 0xFC; /* orange */
        quad->g2 = 0x82; /* orange */
        quad->r3 = 0x32; /* dark brown */
        quad->g3 = 0x28; /* dark brown */
        quad->b2 = 0x00; /* orange */
        quad->b3 = 0x0A; /* dark brown */
    } else {
        setRGB0(quad, 0x10, 0x30, 0x60); /* dark blue */
        setRGB1(quad, 0x18, 0x00, 0x40); /* dark violet */
        setRGB2(quad, 0x30, 0x00, 0x60); /* violet */
        setRGB3(quad, 0x00, 0x00, 0x00); /* black */
    }
    AddPrim(g_disp_ot + SELECT_LAYER_BANNER, quad);
    return quad + 1;
}

/* The banner along the top: its lines, and four shaded quads. */
static void select_DrawBanner(MenuDrawCtx *ctx) {
    POLY_G4 *quad;
    select_DrawBannerLines(ctx);
    quad = ctx->prims.g4;
    setXY4(quad, BANNER_LEFT, BANNER_TOP, BANNER_LEFT, BANNER_MIDDLE_Y,
           DISP_WIDTH / 2, BANNER_TOP, DISP_WIDTH / 2, BANNER_MIDDLE_Y);
    quad = select_AddBannerTop(g_select_step, quad);
    setXY4(quad, BANNER_RIGHT, BANNER_TOP, BANNER_RIGHT, BANNER_MIDDLE_Y,
           DISP_WIDTH / 2, BANNER_TOP, DISP_WIDTH / 2, BANNER_MIDDLE_Y);
    quad = select_AddBannerTop(g_select_step, quad);
    setXY4(quad, DISP_WIDTH / 2, BANNER_MIDDLE_Y, DISP_WIDTH / 2, BANNER_BOTTOM,
           BANNER_LEFT, BANNER_MIDDLE_Y, BANNER_LEFT, BANNER_BOTTOM);
    quad = select_AddBannerBottom(g_select_step, quad);
    setXY4(quad, DISP_WIDTH / 2, BANNER_MIDDLE_Y, DISP_WIDTH / 2, BANNER_BOTTOM,
           BANNER_RIGHT, BANNER_MIDDLE_Y, BANNER_RIGHT, BANNER_BOTTOM);
    quad = select_AddBannerBottom(g_select_step, quad);
    ctx->prims.g4 = quad;
}

/* One line of the banner; the size and place are in `tile` already. */
static TILE *select_AddBannerLine(s32 step, TILE *tile) {
    SetTile(tile);
    if (step < SELECT_STEP_STAGE_1) {
        setRGB0(tile, 0x9E, 0x64, 0x00); /* brown, see-through */
        SetSemiTrans(tile, 1);
    } else {
        setRGB0(tile, 0x28, 0x28, 0x18); /* dark gray */
        SetSemiTrans(tile, 0);
    }
    AddPrim(g_disp_ot + SELECT_LAYER_BANNER_LINES, tile);
    return tile + 1;
}

/* One of the dark stripes at the banner's ends. `step` is not used: the
 * screen's step is read where it is kept. */
static TILE *select_AddBannerShade(s32 step, TILE *tile) {
    SetTile(tile);
    if (g_select_step < SELECT_STEP_STAGE_1) {
        setRGB0(tile, 0x46, 0x24, 0x0A); /* dark brown, see-through */
        SetSemiTrans(tile, 1);
    } else {
        setRGB0(tile, 0x00, 0x00, 0x00); /* black */
        SetSemiTrans(tile, 0);
    }
    AddPrim(g_disp_ot + SELECT_LAYER_BANNER_LINES, tile);
    return tile + 1;
}

/* The banner's lines: upright ones all along it, three across, and stripes
 * that thin out from each end. */
static void select_DrawBannerLines(MenuDrawCtx *ctx) {
    TILE *tile;
    s32 i;
    tile = ctx->prims.tile;
    /* `i` is a line's x here */
    for (i = BANNER_LINE_X; i < BANNER_LINE_END; i += BANNER_LINE_PITCH) {
        setXY0(tile, i, BANNER_TOP);
        setWH(tile, 2, BANNER_HEIGHT);
        tile = select_AddBannerLine(g_select_step, tile);
    }
    setXY0(tile, BANNER_ACROSS_X, BANNER_ACROSS_Y_1);
    setWH(tile, BANNER_ACROSS_WIDTH, 1);
    tile = select_AddBannerLine(g_select_step, tile);
    setXY0(tile, BANNER_ACROSS_X, BANNER_ACROSS_Y_2);
    setWH(tile, BANNER_ACROSS_WIDTH, 1);
    tile = select_AddBannerLine(g_select_step, tile);
    setXY0(tile, BANNER_ACROSS_X, BANNER_ACROSS_Y_3);
    setWH(tile, BANNER_ACROSS_WIDTH, 2);
    tile = select_AddBannerLine(g_select_step, tile);
    for (i = 0; i < BANNER_STRIPES; i++) {
        setXY0(tile, BANNER_STRIPE_LEFT_X + i * BANNER_STRIPE_LEFT_PITCH,
               BANNER_TOP);
        setWH(tile, BANNER_STRIPES - i, BANNER_HEIGHT);
        tile = select_AddBannerShade(g_select_step, tile);
    }
    for (i = 0; i < BANNER_STRIPES; i++) {
        setXY0(tile, BANNER_STRIPE_RIGHT_X - i * BANNER_LINE_PITCH, BANNER_TOP);
        setWH(tile, BANNER_STRIPES - i, BANNER_HEIGHT);
        tile = select_AddBannerShade(g_select_step, tile);
    }
    ctx->prims.tile = tile;
}

/* The screen's frame, over everything: on the picking steps four corner
 * pieces, see-through and flickering with the banner; the title; and
 * while a pick is being made a see-through sheet that runs through four
 * pictures. */
static void select_DrawFrame(MenuDrawCtx *ctx) {
    SpriteDraw draw;
    SpriteSheetSlot *sheets;
    SpriteCell *cells;

    draw.x = 0;
    draw.y = 0;
    draw.scale_x = SPRITE_SCALE_ONE;
    draw.scale_y = SPRITE_SCALE_ONE;
    sheets = PTR(g_select_file->door_sheets);
    draw.header = PTR(sheets[DOOR_SHEET_FRAME_PAGE]);
    SetDrawMode(ctx->prims.mode, 1, 0, sprite_GetTPage(draw.header, 0), 0);
    AddPrim(g_disp_ot + SELECT_LAYER_FRAME_MODE, ctx->prims.mode);
    ctx->prims.mode++;
    draw.has_color = 1;
    draw.header = PTR(sheets[DOOR_SHEET_CORNERS]);
    cells = draw.header->cells;
    if (g_select_step < SELECT_STEP_STAGE_1) {
        draw.semi = 1;
        if (g_select_work->frames & FLICKER_FRAMES) {
            draw.col_r = CORNER_R_1;
            draw.col_g = CORNER_G_1;
            draw.col_b = CORNER_B;
        } else {
            draw.col_r = CORNER_R_2;
            draw.col_g = CORNER_G_2;
            draw.col_b = CORNER_B;
        }
        draw.ot_index = SELECT_LAYER_PORTRAITS;
        draw.table = cells;
        draw.ft4_out = ctx->prims.ft4;
        ctx->prims.ft4 = sprite_DrawScaled(&draw, SPRITE_QUAD_NORMAL);
        draw.table = cells + 1;
        draw.ft4_out = ctx->prims.ft4;
        ctx->prims.ft4 = sprite_DrawScaled(&draw, SPRITE_QUAD_MIRROR_X);
        draw.table = cells + 2;
        draw.ft4_out = ctx->prims.ft4;
        ctx->prims.ft4 = sprite_DrawScaled(&draw, SPRITE_QUAD_MIRROR_Y);
        draw.table = cells + 3;
        draw.ft4_out = ctx->prims.ft4;
        ctx->prims.ft4 = sprite_DrawScaled(&draw, SPRITE_QUAD_MIRROR_XY);
        SetDrawMode(ctx->prims.mode, 1, 0, SPRITE_ABR_ADD_QUARTER, 0);
        AddPrim(g_disp_ot + draw.ot_index, ctx->prims.mode);
        ctx->prims.mode++;
    } else {
        draw.col_r = SELECT_STAGE_TITLE_R;
        draw.col_g = SELECT_STAGE_TITLE_G;
        draw.col_b = SELECT_STAGE_TITLE_B;
    }
    draw.y = 0;
    draw.x = 0;
    draw.ot_index = SELECT_LAYER_TITLE;
    draw.semi = 0;
    draw.header = PTR(sheets[DOOR_SHEET_TITLE]);
    cells = draw.header->cells;
    draw.table = cells;
    draw.sprt_out = ctx->prims.sprt;
    ctx->prims.sprt = sprite_Draw(&draw);
    SetDrawMode(ctx->prims.mode, 1, 0,
                sprite_GetTPage(draw.header, SPRITE_ABR_ADD_QUARTER), 0);
    AddPrim(g_disp_ot + draw.ot_index, ctx->prims.mode);
    ctx->prims.mode++;
    if (g_select_step < SELECT_STEP_WEAPONS_IN) {
        draw.ot_index = SELECT_LAYER_TITLE;
        draw.semi = 1;
        draw.header =
            PTR(sheets[DOOR_SHEET_PICKING +
                       g_select_work->frames % DOOR_SHEET_PICKING_COUNT]);
        cells = draw.header->cells;
        draw.table = cells;
        draw.sprt_out = ctx->prims.sprt;
        ctx->prims.sprt = sprite_Draw(&draw);
        SetDrawMode(ctx->prims.mode, 1, 0,
                    sprite_GetTPage(draw.header, SPRITE_ABR_ADD), 0);
        AddPrim(g_disp_ot + draw.ot_index, ctx->prims.mode);
        ctx->prims.mode++;
    }
}

/* The second side's portrait whose pixels are in video memory, and whether
 * the first pad was picking for it then. */
static u8 g_select_portrait_loaded = 0;
static u8 g_select_portrait_extra = 0;
/* The strip of the colour tables that is copied when the screen loads. */
static RECT g_select_palette_rect = {144, 507, 48, 1};
/* Where in video memory the second side's own portrait goes. */
static RECT g_select_portrait_rect = {896, 384, 64, 64};

/* ---- sprite ---- */

/* Sprite sheets (sprite.h): drawing a sheet's cells as sprites, as scaled
 * quads, or as quads turned about the middle of the sheet. */

/* A request for an ordering-table entry from here up is drawn in entry 1. */
#define SPRITE_OT_LIMIT 0x1006

/* Draws the cells of a sheet as sprites, last cell first, leaving out any
 * that are wholly off the screen. Returns where the next sprite would go. */
s32 sprite_Draw(SpriteDraw *draw) {
    SpriteSheet *sheet = draw->header;
    SPRT *sprt = (SPRT *)ADDR(draw->sprt_out);
    SpriteCell *cell;
    s32 clut;
    s16 i;
    s32 x0, y0, x1, y1;

    clut = GetClut(sheet->cx, sheet->cy);
    for (i = sheet->count - 1; i >= 0; i--) {
        cell = draw->table + i;
        x0 = cell->x + draw->x;
        y0 = cell->y + draw->y;
        x1 = x0 + cell->w;
        y1 = y0 + cell->h;
        if (x1 > 0 && x0 < DISP_WIDTH && y0 < DISP_HEIGHT && y1 > 0) {
            SetSprt(sprt);
            sprt->clut = clut;
            sprt->x0 = x0;
            sprt->y0 = y0;
            sprt->u0 = cell->u + sheet->ubase;
            sprt->v0 = cell->v + sheet->vbase;
            sprt->w = cell->w;
            sprt->h = cell->h;
            if (draw->has_color) {
                SetShadeTex(sprt, 0);
                sprt->r0 = draw->col_r;
                sprt->g0 = draw->col_g;
                sprt->b0 = draw->col_b;
            } else {
                SetShadeTex(sprt, 1);
            }
            SetSemiTrans(sprt, draw->semi);
            if (draw->ot_index >= SPRITE_OT_LIMIT) {
                draw->ot_index = 1;
                /* called with the text alone, as the original has it */
                ((void (*)())font_Printf)("warning\n");
            }
            AddPrim(g_disp_ot + draw->ot_index, sprt);
#if RB_REMASTER_VIEW
            /* A sprite of the fight's HUD, which a wider remastered
             * picture draws out at its own edge (port_remaster.h): the
             * sheet being drawn says which edge it belongs to, and says
             * nothing for the menus, the select screens and the panels,
             * which stay four by three in the middle. Nothing is written
             * down for those, and nothing at all on a PlayStation. */
            port_RemasterNoteAnchored((PortAddress)sprt);
#endif
            sprt++;
        }
    }
    return (s32)sprt;
}

/* Draws the cells as textured quads: scaled by scale_x and scale_y about
 * the first cell's corner, mirrored or blended as `mode` says. In the
 * mirrored modes the texture is read from the far edge of a cell back. */
s32 sprite_DrawScaled(SpriteDraw *draw, s32 mode) {
    SpriteCell *cell = draw->table;
    SpriteSheet *sheet = draw->header;
    POLY_FT4 *quad;
    s16 i;
    u32 tpage;
    u32 clut;
    /* how far into a cell's texture the quad's near and far edges read:
     * 0 and the cell's size, or the other way round when mirrored */
    s16 du0, du1, dv0, dv1;
    u16 u_base, v_base;
    s16 origin_x = cell->x, origin_y = cell->y;
    s32 scale_x, scale_y;
    s32 u, v;

    if (mode == SPRITE_QUAD_ABR_1) {
        tpage = SPRITE_TPAGE(sheet) + SPRITE_ABR_ADD;
    } else if (mode == SPRITE_QUAD_ABR_2) {
        tpage = SPRITE_TPAGE(sheet) + SPRITE_ABR_SUBTRACT;
    } else if (mode == SPRITE_QUAD_ABR_3) {
        tpage = SPRITE_TPAGE(sheet) + SPRITE_ABR_ADD_QUARTER;
    } else {
        tpage = SPRITE_TPAGE(sheet);
    }
    clut = GetClut(sheet->cx, sheet->cy);
    u_base = sheet->ubase;
    v_base = sheet->vbase;
    if (mode == SPRITE_QUAD_MIRROR_X) {
        du1 = 1;
        dv0 = 0;
    } else if (mode == SPRITE_QUAD_MIRROR_Y) {
        du0 = 0;
        dv1 = -1;
    } else if (mode == SPRITE_QUAD_MIRROR_XY) {
        du1 = 1;
        dv1 = -1;
    } else {
        du0 = 0;
        dv0 = 0;
    }
    quad = (POLY_FT4 *)ADDR(draw->ft4_out);
    for (i = 0; i < sheet->count; i++) {
        if (mode == SPRITE_QUAD_MIRROR_X) {
            du0 = cell->w - 1;
            dv1 = cell->h;
        } else if (mode == SPRITE_QUAD_MIRROR_Y) {
            du1 = cell->w;
            dv0 = cell->h - 1;
        } else if (mode == SPRITE_QUAD_MIRROR_XY) {
            du0 = cell->w - 1;
            dv0 = cell->h - 1;
        } else {
            dv1 = cell->h;
            du1 = cell->w;
            if (dv1 == 1) {
                dv1 = 0;
            }
        }
        SetPolyFT4(quad);
        quad->tpage = tpage;
        quad->clut = clut;
        scale_x = draw->scale_x;
        scale_y = draw->scale_y;
        quad->x0 = origin_x + draw->x + (((cell->x - origin_x) * scale_x) >> 8);
        quad->y0 = origin_y + draw->y + (((cell->y - origin_y) * scale_y) >> 8);
        quad->x1 =
            origin_x + draw->x + (((cell->x - origin_x) * scale_x) >> 8) +
            ((cell->w * scale_x) >> 8);
        quad->y1 = origin_y + draw->y + (((cell->y - origin_y) * scale_y) >> 8);
        quad->x2 = origin_x + draw->x + (((cell->x - origin_x) * scale_x) >> 8);
        quad->y2 =
            origin_y + draw->y + (((cell->y - origin_y) * scale_y) >> 8) +
            ((cell->h * scale_y) >> 8);
        quad->x3 =
            origin_x + draw->x + (((cell->x - origin_x) * scale_x) >> 8) +
            ((cell->w * scale_x) >> 8);
        quad->y3 =
            origin_y + draw->y + (((cell->y - origin_y) * scale_y) >> 8) +
            ((cell->h * scale_y) >> 8);
        u = u_base + cell->u;
        v = v_base + cell->v;
        quad->u0 = u + du0;
        quad->v0 = v + dv0;
        quad->u1 = u + du1;
        quad->v1 = v + dv0;
        quad->u2 = u + du0;
        quad->v2 = v + dv1;
        quad->u3 = u + du1;
        quad->v3 = v + dv1;
        if (draw->has_color) {
            SetShadeTex(quad, 0);
            quad->r0 = draw->col_r;
            quad->g0 = draw->col_g;
            quad->b0 = draw->col_b;
        } else {
            SetShadeTex(quad, 1);
        }
        SetSemiTrans(quad, draw->semi);
        if (draw->ot_index >= SPRITE_OT_LIMIT) {
            draw->ot_index = 1;
        }
        AddPrim(g_disp_ot + draw->ot_index, quad);
        quad++;
        cell++;
    }
    draw->ft4_out = (s32)quad;
    return (s32)quad;
}

/* Draws the cells as textured quads turned by `angle` about the middle of
 * the box that holds them all. The corners are worked out at twice their
 * distance from the middle and turned by a matrix scaled by
 * g_sprite_rotation_scale, y by a half: the picture's pixels are twice as
 * high as they are wide. Defined in the old style: `angle` is a halfword
 * that the callers pass as a word. */
static s32 sprite_DrawRotated(draw, angle, mode)
SpriteDraw *draw;
s16 angle;
s32 mode;
{
    s16 i;
    u32 tpage;
    u32 clut;
    /* as in sprite_DrawScaled, where a mirrored mode changes them; always 0
     * here */
    s16 du0, dv0;
    s16 du1, dv1;
    u16 u_base, v_base;
    SpriteSheet *sheet;
    SVECTOR corners[4];
    SVECTOR turn;
    VECTOR turned[4];
    MATRIX matrix;
    POLY_FT4 *quad;
    SpriteCell *cell;
    s16 j;
    s16 left, top, right, bottom;
    s16 center_x, center_y;
    s32 u, v;
    s32 x, y;

    sheet = draw->header;
    cell = draw->table;
    tpage = SPRITE_TPAGE(sheet);
    clut = GetClut(sheet->cx, sheet->cy);
    left = cell->x;
    top = cell->y;
    right = left + ((cell->w * draw->scale_x) >> 8);
    bottom = top + ((cell->h * draw->scale_y) >> 8);
    for (i = 1; i < sheet->count; i++) {
        cell++;
        if (left > cell->x) {
            left = cell->x;
        }
        if (right < cell->x + ((cell->w * draw->scale_x) >> 8)) {
            right = cell->x + ((cell->w * draw->scale_x) >> 8);
        }
        if (top > cell->y) {
            top = cell->y;
        }
        if (bottom < cell->y + ((cell->h * draw->scale_y) >> 8)) {
            bottom = cell->y + ((cell->h * draw->scale_y) >> 8);
        }
    }
    center_y = (top + bottom) / 2;
    center_x = (left + right) / 2;
    cell = draw->table;
    quad = (POLY_FT4 *)ADDR(draw->ft4_out);
    du0 = 0;
    dv0 = 0;
    u_base = sheet->ubase;
    v_base = sheet->vbase;
    for (i = 0; i < sheet->count; i++) {
        du1 = cell->w;
        dv1 = cell->h;
        SetPolyFT4(quad);
        quad->tpage = tpage;
        quad->clut = clut;
        turn.vy = 0;
        turn.vx = 0;
        turn.vz = angle;
        RotMatrix(&turn, &matrix);
        ScaleMatrixL(&matrix, &g_sprite_rotation_scale);
        SetRotMatrix(&matrix);
        for (j = 0; j < 4; j++) {
            corners[j].vx =
                (((cell->x - center_x) + cell->w * (j & 1)) * draw->scale_x >>
                 8) *
                2;
            if (mode == SPRITE_QUAD_MIRROR_X || mode == SPRITE_QUAD_MIRROR_XY) {
                corners[j].vx = -corners[j].vx;
            }
            corners[j].vy =
                (((cell->y - center_y) + ((cell->h * (j & 2)) >> 1)) *
                     draw->scale_y >>
                 8) *
                2;
            if (mode == SPRITE_QUAD_MIRROR_Y || mode == SPRITE_QUAD_MIRROR_XY) {
                corners[j].vy = -corners[j].vy;
            }
            ApplyRotMatrix(&corners[j], &turned[j]);
        }
        x = draw->x;
        y = draw->y;
        quad->x0 = center_x + turned[0].vx + x;
        quad->y0 = center_y + turned[0].vy + y;
        quad->x1 = center_x + turned[1].vx + x;
        quad->y1 = center_y + turned[1].vy + y;
        quad->x2 = center_x + turned[2].vx + x;
        quad->y2 = center_y + turned[2].vy + y;
        quad->x3 = center_x + turned[3].vx + x;
        quad->y3 = center_y + turned[3].vy + y;
        u = u_base + cell->u;
        v = v_base + cell->v;
        quad->u0 = u + du0;
        quad->v0 = v + dv0;
        quad->u1 = u + du1;
        quad->v1 = v + dv0;
        quad->u2 = u + du0;
        quad->v2 = v + dv1;
        quad->u3 = u + du1;
        quad->v3 = v + dv1;
        if (draw->has_color) {
            SetShadeTex(quad, 0);
            quad->r0 = draw->col_r;
            quad->g0 = draw->col_g;
            quad->b0 = draw->col_b;
        } else {
            SetShadeTex(quad, 1);
        }
        SetSemiTrans(quad, draw->semi);
        if (draw->ot_index >= SPRITE_OT_LIMIT) {
            draw->ot_index = 1;
        }
        AddPrim(g_disp_ot + draw->ot_index, quad);
        quad++;
        cell++;
    }
    draw->ft4_out = (s32)quad;
    return (s32)quad;
}

/* The scale sprite_DrawRotated turns with: half height, as the picture is. */
static VECTOR g_sprite_rotation_scale = {ONE, ONE / 2, ONE, 0};
