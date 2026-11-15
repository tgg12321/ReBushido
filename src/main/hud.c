/* What is drawn flat over the 3D scene (hud.h): the pause panel, the
 * modes' counters and timers, captions, the team battle's scores, the best
 * times, story mode's stage card, an ending's picture and the loading
 * picture. All of it is sprite sheets (sprite.h) and plain rectangles, built
 * where the caller says and linked into the display's ordering table. */
#include "common.h"
#include "bb2.h"

/* Defined further down. */
static SpriteSheet g_hud_panel_sheets[9];
static SpriteSheet g_hud_panel_option_sheet;
static SpriteSheet g_hud_panel_on_off_sheet;
static SpriteCell g_hud_panel_corner_cells[2][2];
static SpriteCell g_hud_panel_rule_end_cells[2];
static SpriteCell g_hud_panel_title_cells[3];
static SpriteCell g_hud_panel_continue_cells[2];
static SpriteCell g_hud_panel_rematch_cells[2];
static SpriteCell g_hud_panel_footer_cells[6];
static SpriteCell g_hud_panel_option_cells[12];
static SpriteCell g_hud_panel_on_off_cells[2];
static SpriteCell *g_hud_panel_item_cells[4];
static ScreenSize g_hud_panel_sizes[3];
static SpriteSheet g_hud_loading_sheets[2][5];
static SpriteCell g_hud_loading_title_cells[3];
static SpriteCell g_hud_loading_picture_cells[6];
static SpriteCell g_hud_noise_cells[2];
static s32 g_hud_noise_cycle;
static SpriteSheet g_hud_counter_sheets[4];
static SpriteCell g_hud_digit_cells[10];
static SpriteCell g_hud_time_cells[7];
static SpriteSheet g_hud_name_sheet;
static SpriteCell g_hud_name_8_tail_cells[3];
static SpriteCell g_hud_name_cells[20][3];
static u8 g_hud_team_unfought_cell_ids[8];
static RECT g_hud_ending_image_rect;
static RECT g_hud_ending_clut_rect;
static s32 g_hud_ending_frame;
static RECT g_hud_card_name_image_rect;
static RECT g_hud_card_name_clut_rect;
static RECT g_hud_card_picture_image_rect;
static RECT g_hud_card_picture_clut_rect;
static SpriteCell g_hud_card_mark_cell;
static BestTimesSheets g_hud_best_times_sheets;
#if RB_PORT
static void hud_FillBestTimesSheets(void);
#endif
static SpriteCell g_hud_best_times_digit_cell;
static s16 g_hud_best_times_bar;
static s16 g_hud_best_times_row_bar;

/* The loading screen's noise: a running random number. */
static u32 g_hud_noise_seed;

/* ---- colours ---- */

/* The panel's frame: a light line over a dark one. */
#define PANEL_LIGHT 0x52
#define PANEL_DARK 0x3C
/* The red of the rules under the counters, and of the pieces at their
 * ends. */
#define RULE_R 0xFF
#define RULE_G 0x10
#define RULE_B 0x10
/* What a sheet is dimmed with: a loser's name in the team battle's table. */
#define DIM_GRAY 0x40

/* ---- the sheets ---- */

/* g_hud_panel_sheets. */
enum HudPanelSheet {
    PANEL_SHEET_TOP_CORNERS = 0,
    PANEL_SHEET_BOTTOM_CORNERS = 1,
    PANEL_SHEET_RULE_ENDS = 2,
    PANEL_SHEET_TITLE = 3,
    PANEL_SHEET_CONTINUE = 4, /* the first row; the other two follow it */
    PANEL_SHEET_REMATCH = 5,
    PANEL_SHEET_TO_SELECT = 6,
    PANEL_SHEET_TO_MENU = 7,
    PANEL_SHEET_FOOTER = 8,
};

/* g_hud_counter_sheets. The piece at a rule's end is in two halves. */
enum HudCounterSheet {
    COUNTER_SHEET_DIGITS = 0,
    COUNTER_SHEET_LABELS = 1,
    COUNTER_SHEET_RULE_END = 2,
    COUNTER_SHEET_RULE_END_2 = 3,
};

/* g_hud_time_cells. */
enum HudTimeCell {
    TIME_CELL_MARKS = 0,    /* three: the marks between the time's digits */
    TIME_CELL_LABELS = 3,   /* the time's label, then the count's */
    TIME_CELL_RULE_END = 5, /* its two halves */
};

/* A set of g_hud_loading_sheets. */
enum HudLoadingSheet {
    LOADING_SHEET_TITLE = 0,
    LOADING_SHEET_PICTURE = 1,
    LOADING_SHEET_LONG_STREAK = 2,
    LOADING_SHEET_SHORT_STREAKS = 3, /* two */
};

/* g_hud_card_sheets. */
enum HudCardSheet {
    STAGE_CARD_SHEET_TITLE = 0,
    STAGE_CARD_SHEET_PICTURE = 1, /* two */
    STAGE_CARD_SHEET_MAP = 3,
    STAGE_CARD_SHEETS
};

/* g_hud_card_mark_sheets. */
enum HudCardMarkSheet {
    STAGE_CARD_MARK_SHEET_LAST = 0,
    STAGE_CARD_MARK_SHEET_NARUKAGAMI = 1,
    STAGE_CARD_MARK_SHEET_SHAINTO = 2,
};

/* ---- where things are on the screen (640 x 240) ----
 * Most cells have their place in them; these are the places the code
 * works out. */

/* The panel is in the middle of the screen, g_hud_panel_sizes[panel] big.
 * Down from its top: the title; a rule; the menu's three rows. The panel
 * with options has its three options between the rule and the rows, which
 * are that much lower, and under the rows a second rule and a footer. The
 * frame's corners and the rules' ends are pieces that begin
 * PANEL_END_INSET inside the right edge. */
#define PANEL_ROWS 3
#define PANEL_OPTIONS 3
#define PANEL_RULE_Y 14
#define PANEL_OPTIONS_DROP 51
#define PANEL_OPTIONS_RULE_Y 115
#define PANEL_OPTION_PITCH 15
#define PANEL_END_INSET 12
#define PANEL_SIDE_WIDTH 2 /* each of the two lines of a side */

/* The loading screen's noise. A burst is around a place somewhere in this
 * area; a streak is off from there by up to half its spread each way. */
#define NOISE_AREA_WIDTH 608
#define NOISE_AREA_HEIGHT 220
#define NOISE_LONG_SPREAD_X 50
#define NOISE_LONG_SPREAD_Y 25
#define NOISE_SHORT_SPREAD_X 100
#define NOISE_SHORT_SPREAD_Y 50
#define NOISE_CYCLE 4 /* bursts before the smallest comes round again */
/* A number under `range` from the seed, which has fifteen bits. */
#define NOISE_RANDOM(range) ((g_hud_noise_seed * (range)) >> 15)

/* The counters at the top of the screen. A number's digits are DIGIT_PITCH
 * apart, and a 1, which is narrower, is moved over to the middle of its
 * place. One counter is at the left and one at the right, their digits on
 * one row and a red rule under each. */
#define DIGIT_PITCH 20
#define DIGIT_ONE_SHIFT 3
#define COUNTER_DIGITS_Y 22
#define COUNTER_RULE_Y 36
#define COUNTER_LEFT_X 80
#define COUNTER_RIGHT_X 521
#define COUNTER_LEFT_RULE_X 72
#define COUNTER_RULE_WIDTH 48
/* The label of a count is cut short when the count is one ((guess) of the
 * last letter of a plural). */
#define LABEL_WIDTH 60
#define LABEL_WIDTH_ONE 45

/* Chanbara's time, at the right: minutes, seconds and hundredths on the
 * digits' row, and under them the count, whose digits are a little wider
 * apart. Both rules run to the piece at their end. */
#define TIME_MINUTES_X 418
#define TIME_SECONDS_X 467
#define TIME_HUNDREDTHS_X COUNTER_RIGHT_X
#define TIME_COUNT_X 499
#define TIME_COUNT_Y 41
#define TIME_COUNT_PITCH 21
#define TIME_RULE_END_X 568

/* The team battle's match. Each side has two rows of marks, its points and
 * under them its warnings: the first side's run right from the left edge,
 * the second's left from MARK_RIGHT_X. The warnings' marks are MARK_SHRINK
 * closer together, and their rule that much shorter twice over. */
#define POINT_ROWS 2
#define WARNING_SLOTS 2
#define MARK_PITCH 28
#define MARK_SHRINK 8
#define MARK_RIGHT_X 450
#define POINT_LABEL_SIDE_2_X 550 /* the second side's, from the first's */
#define POINT_ROW_PITCH 20
#define POINT_RULE_WIDTH 66
#define POINT_RULE_SIDE_2_X 431 /* the second side's rule, from the first's */
/* The time left, in the middle: seconds, two marks, hundredths. With three
 * digits of seconds (the two-minute limit) the seconds begin
 * MATCH_TIME_LONG_SHIFT further left and the rest as much further right. */
#define TIME_LIMIT_TWO_MINUTES 2 /* g_game_settings.time_limit_index */
#define MATCH_TIME_X 275
#define MATCH_TIME_HUNDREDTHS 52 /* from the seconds */
#define MATCH_TIME_MARK_X 315
#define MATCH_TIME_MARK_PITCH 6
#define MATCH_TIME_LONG_SHIFT 10

/* The team battle's table. g_game_settings.match_count_index is the
 * matches less TEAM_MIN_MATCHES; the table is laid out for each number. */
#define FOUR_MATCHES 1
#define FIVE_MATCHES 2
/* A side's points in a match are two bits; this says it was not fought. */
#define SCORE_UNFOUGHT 3
/* The rows: closer together for four and five matches, and from higher up
 * for five. A name's cells are 80 down already, and go 4 under the top of
 * their row. */
#define SCORE_ROW_PITCH 24
#define SCORE_ROW_PITCH_3 34
#define SCORE_ROWS_Y 79
#define SCORE_ROWS_Y_5 68
#define SCORE_NAME_Y (4 - 80)
#define SCORE_UNFOUGHT_DROP 2 /* the "not fought" words, under a row's top */
#define SCORE_MARK_DROP 5     /* the points' marks */
#define SCORE_STRIKE_DROP 6   /* the line through an unfought match's names */
/* In the middle of a row: who won, the second side's SCORE_SIDE_2_X right
 * of the first's, and each side's points as marks, outward from the
 * middle. */
#define SCORE_SIDE_2_X 70
#define SCORE_MARK_PITCH 16
#define SCORE_MARK_LEFT_X 226
#define SCORE_MARK_RIGHT_X 377
/* The totals under the rows: all a side's points as marks, in columns of
 * two, outward from the middle; the matches each side won as a digit; and a
 * rule over them. */
#define SCORE_TOTALS_Y_3 190
#define SCORE_TOTALS_Y_4 194
#define SCORE_TOTALS_Y_5 198
#define SCORE_TOTALS_DROP 3 /* the middle piece and the digits, under that */
#define SCORE_TOTAL_COLUMN_PITCH 20
#define SCORE_TOTAL_MARK_PITCH 12
#define SCORE_TOTAL_LEFT_X 242
#define SCORE_TOTAL_RIGHT_X 385
#define SCORE_WINS_X 275
#define SCORE_TOTALS_RULE_X 94
#define SCORE_TOTALS_RULE_RISE 5
#define SCORE_TOTALS_RULE_WIDTH 453
/* The rule under each side's title. */
#define SCORE_TITLE_RULE_X 8
#define SCORE_TITLE_RULE_SIDE_2_X 413
#define SCORE_TITLE_RULE_Y 58
#define SCORE_TITLE_RULE_WIDTH 220

/* An ending's picture: files 51 on of the disc's data group. It is two
 * strips, one over the other; it stays six seconds, then each half goes
 * off its side with a red edge behind it. */
#define ENDING_FIRST_FILE CDROM_DATA_LAS_TIM
#define ENDING_HOLD_FRAMES (6 * FRAMES_PER_SECOND)
#define ENDING_SLIDE_SPEED 24
#define ENDING_EDGE_WIDTH 20
#define ENDING_STRIP_HEIGHT 64

/* The count of opponents beaten: a large digit and the words after it;
 * with two digits, both are KILL_TWO_SHIFT out from there. */
#define KILL_DIGIT_X 100
#define KILL_WORDS_X 147
#define KILL_DIGIT_WIDTH 32
#define KILL_TWO_SHIFT 16

/* Story mode's stage card: files 61 on. A place on its map is from the
 * map's corner; `place` has HUD_CARD_MARK_LAST for the last mark. */
#define STAGE_CARD_FIRST_FILE CDROM_DATA_STG_BIN
#define STAGE_CARD_MAP_X 55
#define STAGE_CARD_MAP_Y 42

/* The panel of the pause menu or of the menu after a fight: a darkened box
 * in the middle of the screen with a frame of light and dark lines, the
 * rows of the menu in it, and the row at the cursor lit while the others
 * are dimmed. The pause panel with options has three options with their
 * on / off settings over the menu's rows, and a rule and a footer under
 * them.
 * `cursor_and_options`: the cursor's row in the low half, the options'
 * settings from bit 16 up. */
s32 hud_DrawPanel(s32 panel, s32 cursor_and_options, s32 prims, s32 ot_index) {
    HudPanelPrims *block = (HudPanelPrims *)ADDR(prims);
    SpriteDraw draw;
    TILE *tile;
    s32 next_sprt;
    s16 selected_row;
    u16 y_base;
    DR_MODE *dr_mode;
    s32 size;
    s16 top;
    s16 i;
    s16 j;
    s16 x;
    s16 y;

    tile = block->tiles;
    next_sprt = prims + sizeof(block->tiles);
    y_base = 0;
    /* FAKE: (s16)cursor_and_options */
    selected_row = *(s16 *)&cursor_and_options;
    dr_mode = &block->draw_mode;
    size = (s32)(dr_mode + 2) - prims;
    top = (DISP_HEIGHT - g_hud_panel_sizes[panel].h) / 2;
    /* a tint that is never turned on */
    draw.col_b = DIM_GRAY;
    draw.col_g = DIM_GRAY;
    draw.col_r = DIM_GRAY;
    draw.has_color = 0;

    switch (panel) {
    case HUD_PANEL_PAUSE_OPTIONS:
        draw.header = &g_hud_panel_sheets[PANEL_SHEET_RULE_ENDS];
        draw.table = g_hud_panel_rule_end_cells;
        y_base = PANEL_OPTIONS_DROP;
        draw.y = top + PANEL_OPTIONS_RULE_Y;
        draw.x = 0;
        draw.semi = 0;
        draw.sprt_out = next_sprt;
        draw.ot_index = ot_index;
        g_hud_panel_rule_end_cells[0].x =
            (DISP_WIDTH - g_hud_panel_sizes[HUD_PANEL_PAUSE_OPTIONS].w) / 2;
        g_hud_panel_rule_end_cells[1].x =
            (g_hud_panel_sizes[HUD_PANEL_PAUSE_OPTIONS].w + DISP_WIDTH) / 2 -
            PANEL_END_INSET;
        next_sprt = sprite_Draw(&draw);
        draw.header = &g_hud_panel_sheets[PANEL_SHEET_FOOTER];
        draw.table = g_hud_panel_footer_cells;
        draw.x = 0;
        draw.y = 0;
        draw.sprt_out = next_sprt;
        draw.ot_index = ot_index;
        next_sprt = sprite_Draw(&draw);
        for (j = 0; j < PANEL_OPTIONS; j++) {
            if (selected_row == j + PANEL_ROWS) {
                draw.semi = 0;
            } else {
                draw.semi = 1;
            }
            draw.header = &g_hud_panel_option_sheet;
            draw.table = &g_hud_panel_option_cells[j * draw.header->count];
            draw.x = 0;
            draw.y = 0;
            draw.sprt_out = next_sprt;
            draw.ot_index = ot_index;
            next_sprt = sprite_Draw(&draw);
            for (i = 0; i < 2; i++) {
                /* the option's setting: on is the first of the two */
                if (((cursor_and_options >> (j + 16)) & 1) == i) {
                    draw.semi = 0;
                } else {
                    draw.semi = 1;
                }
                draw.header = &g_hud_panel_on_off_sheet;
                draw.table = &g_hud_panel_on_off_cells[i];
                draw.x = 0;
                draw.y = j * PANEL_OPTION_PITCH;
                draw.sprt_out = next_sprt;
                draw.ot_index = ot_index;
                next_sprt = sprite_Draw(&draw);
            }
        }
        for (i = 0; i < 2; i++) {
            SetTile(tile);
            if (i != 0) {
                setRGB0(tile, PANEL_DARK, PANEL_DARK, PANEL_DARK);
            } else {
                setRGB0(tile, PANEL_LIGHT, PANEL_LIGHT, PANEL_LIGHT);
            }
            tile->x0 = (DISP_WIDTH - g_hud_panel_sizes[panel].w) / 2;
            tile->y0 = top + PANEL_OPTIONS_RULE_Y + i;
            tile->w = g_hud_panel_sizes[panel].w;
            tile->h = 1;
            SetSemiTrans(tile, 0);
            AddPrim(g_disp_ot + ot_index, tile);
            tile++;
        }
        /* fall through */
    case HUD_PANEL_PAUSE:
        if (selected_row == 0) {
            draw.semi = 0;
        } else {
            draw.semi = 1;
        }
        draw.table = g_hud_panel_continue_cells;
        draw.header = &g_hud_panel_sheets[PANEL_SHEET_CONTINUE];
        draw.x = 0;
        draw.sprt_out = next_sprt;
        draw.y = y_base + top;
        draw.ot_index = ot_index;
        next_sprt = sprite_Draw(&draw);
        draw.header = &g_hud_panel_sheets[PANEL_SHEET_TITLE];
        draw.has_color = 0;
        draw.table = g_hud_panel_title_cells;
        draw.x = 0;
        draw.y = top;
        draw.semi = 0;
        draw.sprt_out = next_sprt;
        draw.ot_index = ot_index;
        next_sprt = sprite_Draw(&draw);
        top += PANEL_RULE_Y;
        draw.header = &g_hud_panel_sheets[PANEL_SHEET_RULE_ENDS];
        draw.table = g_hud_panel_rule_end_cells;
        draw.x = 0;
        draw.y = top;
        draw.semi = 0;
        draw.sprt_out = next_sprt;
        draw.ot_index = ot_index;
        g_hud_panel_rule_end_cells[0].x =
            (DISP_WIDTH - g_hud_panel_sizes[panel].w) / 2;
        g_hud_panel_rule_end_cells[1].x =
            (g_hud_panel_sizes[panel].w + DISP_WIDTH) / 2 - PANEL_END_INSET;
        next_sprt = sprite_Draw(&draw);
        for (i = 0; i < 2; i++) {
            SetTile(tile);
            if (i != 0) {
                setRGB0(tile, PANEL_DARK, PANEL_DARK, PANEL_DARK);
            } else {
                setRGB0(tile, PANEL_LIGHT, PANEL_LIGHT, PANEL_LIGHT);
            }
            tile->x0 = (DISP_WIDTH - g_hud_panel_sizes[panel].w) / 2;
            tile->y0 = top + i;
            tile->w = g_hud_panel_sizes[panel].w;
            tile->h = 1;
            SetSemiTrans(tile, 0);
            AddPrim(g_disp_ot + ot_index, tile);
            tile++;
        }
        break;
    case HUD_PANEL_AFTER_FIGHT:
        if (selected_row == 0) {
            draw.semi = 0;
        } else {
            draw.semi = 1;
        }
        draw.header = &g_hud_panel_sheets[PANEL_SHEET_REMATCH];
        draw.table = g_hud_panel_rematch_cells;
        draw.x = 0;
        draw.y = top;
        draw.sprt_out = next_sprt;
        draw.ot_index = ot_index;
        next_sprt = sprite_Draw(&draw);
        break;
    }

    /* the second and third rows; the panel's first is drawn above */
    draw.has_color = 0;
    for (j = 2; j < 4; j++) {
        if (selected_row == j - 1) {
            draw.semi = 0;
        } else {
            draw.semi = 1;
        }
        draw.header = &g_hud_panel_sheets[PANEL_SHEET_CONTINUE + j];
        draw.table = g_hud_panel_item_cells[j];
        draw.x = 0;
        draw.y = y_base + top;
        draw.sprt_out = next_sprt;
        draw.ot_index = ot_index;
        next_sprt = sprite_Draw(&draw);
    }

    /* the frame: the corner pieces, then the lines between them */
    draw.has_color = 0;
    draw.header = &g_hud_panel_sheets[PANEL_SHEET_TOP_CORNERS];
    draw.table = g_hud_panel_corner_cells[0];
    draw.x = 0;
    draw.y = (DISP_HEIGHT - g_hud_panel_sizes[panel].h) / 2;
    draw.semi = 0;
    draw.sprt_out = next_sprt;
    draw.ot_index = ot_index;
    g_hud_panel_corner_cells[0][0].x =
        (DISP_WIDTH - g_hud_panel_sizes[panel].w) / 2;
    g_hud_panel_corner_cells[0][1].x =
        (g_hud_panel_sizes[panel].w + DISP_WIDTH) / 2 - PANEL_END_INSET;
    next_sprt = sprite_Draw(&draw);
    draw.header = &g_hud_panel_sheets[PANEL_SHEET_BOTTOM_CORNERS];
    draw.table = g_hud_panel_corner_cells[1];
    draw.y = (g_hud_panel_sizes[panel].h + DISP_HEIGHT) / 2;
    draw.sprt_out = next_sprt;
    g_hud_panel_corner_cells[1][0].x =
        (DISP_WIDTH - g_hud_panel_sizes[panel].w) / 2;
    g_hud_panel_corner_cells[1][1].x =
        (g_hud_panel_sizes[panel].w + DISP_WIDTH) / 2 - PANEL_END_INSET;
    sprite_Draw(&draw);

    for (j = 0; j < 2; j++) {
        x = (DISP_WIDTH - g_hud_panel_sizes[panel].w) / 2;
        if (j != 0) {
            y = (DISP_HEIGHT - g_hud_panel_sizes[panel].h) / 2;
        } else {
            y = (g_hud_panel_sizes[panel].h + DISP_HEIGHT) / 2;
        }
        for (i = 0; i < 2; i++) {
            SetTile(tile);
            if (i != 0) {
                setRGB0(tile, PANEL_DARK, PANEL_DARK, PANEL_DARK);
            } else {
                setRGB0(tile, PANEL_LIGHT, PANEL_LIGHT, PANEL_LIGHT);
            }
            tile->x0 = x;
            tile->y0 = y + i;
            tile->w = g_hud_panel_sizes[panel].w;
            tile->h = 1;
            SetSemiTrans(tile, 0);
            AddPrim(g_disp_ot + ot_index, tile);
            tile++;
        }
        if (j != 0) {
            x = (DISP_WIDTH - g_hud_panel_sizes[panel].w) / 2;
        } else {
            x = (g_hud_panel_sizes[panel].w + DISP_WIDTH) / 2;
        }
        y = (DISP_HEIGHT - g_hud_panel_sizes[panel].h) / 2;
        for (i = 0; i < 2; i++) {
            SetTile(tile);
            if (j != 0) {
                if (i != 0) {
                    setRGB0(tile, PANEL_DARK, PANEL_DARK, PANEL_DARK);
                } else {
                    setRGB0(tile, PANEL_LIGHT, PANEL_LIGHT, PANEL_LIGHT);
                }
            } else {
                if (i != 0) {
                    setRGB0(tile, PANEL_LIGHT, PANEL_LIGHT, PANEL_LIGHT);
                } else {
                    setRGB0(tile, PANEL_DARK, PANEL_DARK, PANEL_DARK);
                }
            }
            tile->x0 = x + i * PANEL_SIDE_WIDTH;
            tile->y0 = y;
            tile->w = PANEL_SIDE_WIDTH;
            tile->h = g_hud_panel_sizes[panel].h + 2;
            SetSemiTrans(tile, 0);
            AddPrim(g_disp_ot + ot_index, tile);
            tile++;
        }
    }

    /* the box, which darkens the scene behind it */
    SetTile(tile);
    setRGB0(tile, 0, 0, 0);
    tile->x0 = (DISP_WIDTH - g_hud_panel_sizes[panel].w) / 2;
    tile->y0 = (DISP_HEIGHT - g_hud_panel_sizes[panel].h) / 2;
    tile->w = g_hud_panel_sizes[panel].w;
    tile->h = g_hud_panel_sizes[panel].h;
    SetSemiTrans(tile, 1);
    AddPrim(g_disp_ot + ot_index, tile);
    SetDrawMode(
        dr_mode, 1, 0,
        sprite_GetTPage(&g_hud_panel_sheets[PANEL_SHEET_TOP_CORNERS], 0), 0);
    AddPrim(g_disp_ot + ot_index, dr_mode);
    return size;
}

/* The loading screen: the title and the picture, from the first set of
 * sheets for the plain screen and the second for the next opponent. */
s32 hud_DrawLoadingPicture(s32 prims, s32 screen) {
    SpriteDraw draw;
    s32 sheet_row_index;
    sheet_row_index = screen;
    if (screen > 0) {
        sheet_row_index = screen - 1;
    }
    draw.has_color = 0;
    draw.header = &g_hud_loading_sheets[sheet_row_index][LOADING_SHEET_TITLE];
    draw.table = g_hud_loading_title_cells;
    draw.scale_y = SPRITE_SCALE_ONE;
    draw.scale_x = SPRITE_SCALE_ONE;
    draw.y = 0;
    draw.x = 0;
    draw.semi = 0;
    draw.ot_index = 1;
    draw.ft4_out = prims;
    prims = sprite_DrawScaled(&draw, SPRITE_QUAD_NORMAL);
    draw.has_color = 0;
    draw.header = &g_hud_loading_sheets[sheet_row_index][LOADING_SHEET_PICTURE];
    draw.table = g_hud_loading_picture_cells;
    draw.scale_y = SPRITE_SCALE_ONE;
    draw.scale_x = SPRITE_SCALE_ONE;
    draw.y = 0;
    draw.x = 0;
    draw.semi = 0;
    draw.ot_index = 1;
    draw.ft4_out = prims;
    return sprite_DrawScaled(&draw, SPRITE_QUAD_NORMAL);
}

/* Streaks of noise over the loading picture: around one place picked at
 * random, pairs of a long streak and a short one, each a little off from
 * it. Two pairs the first time, then four, six and eight, and round again. */
s32 hud_DrawLoadingNoise(s32 prims, s32 screen) {
    SpriteDraw draw;
    s32 quad_pairs_drawn;
    u32 base_x;
    u32 base_y;
    s32 ft4_cursor;
    s32 row_off;
    s32 x;
    s32 y;
    /* FAKE: a second name for the value on its way into draw */
    s32 long_work;
    /* FAKE: as long_work */
    s32 short_work;
    s32 life_size; /* FAKE: SPRITE_SCALE_ONE */
    s32 ot_entry;  /* FAKE: 1 */
    /* FAKE: &g_hud_loading_sheets[0][LOADING_SHEET_LONG_STREAK] */
    SpriteSheet *noise_sheets;
    /* the two sheets the second streak is picked from */
    SpriteSheet *short_streak_sheets;

    g_hud_noise_cycle %= NOISE_CYCLE;

    ft4_cursor = prims;
    /* from here on the screen's row of sheets */
    if (screen > 0) {
        screen -= 1;
    }

    g_hud_noise_seed ^= rand();
    base_x = NOISE_RANDOM(NOISE_AREA_WIDTH);
    g_hud_noise_seed ^= rand();
    base_y = NOISE_RANDOM(NOISE_AREA_HEIGHT);

    quad_pairs_drawn = 0;
    if (quad_pairs_drawn < ((g_hud_noise_cycle + 1) * 2)) {
        life_size = SPRITE_SCALE_ONE;
        ot_entry = 1;
        row_off = screen * sizeof(g_hud_loading_sheets[0]);
        noise_sheets = &g_hud_loading_sheets[0][LOADING_SHEET_LONG_STREAK];
        short_streak_sheets =
            &g_hud_loading_sheets[screen][LOADING_SHEET_SHORT_STREAKS];
        do {
            draw.has_color = 0;
            /* FAKE: the long streak's sheet of this set */
            draw.header = (SpriteSheet *)ADDR(row_off + (s32)noise_sheets);
            draw.table = &g_hud_noise_cells[0];
            g_hud_noise_seed ^= rand();
            draw.scale_y = life_size;
            draw.scale_x = life_size;
            g_hud_noise_seed ^= rand();
            x = base_x - NOISE_LONG_SPREAD_X / 2;
            x += NOISE_RANDOM(NOISE_LONG_SPREAD_X);
            /* FAKE: long_work holds the y base, then the cursor */
            long_work = base_y - NOISE_LONG_SPREAD_Y / 2;
            draw.x = x;
            g_hud_noise_seed ^= rand();
            y = long_work + NOISE_RANDOM(NOISE_LONG_SPREAD_Y);
            draw.semi = 0;
            draw.ot_index = ot_entry;
            long_work = ft4_cursor;
            draw.ft4_out = long_work;
            draw.y = y;
            ft4_cursor = sprite_DrawScaled(&draw, SPRITE_QUAD_NORMAL);

            draw.has_color = 0;
            draw.scale_y = life_size;
            draw.scale_x = life_size;
            draw.table = &g_hud_noise_cells[1];
            draw.header = short_streak_sheets + (g_hud_noise_seed & 1);
            g_hud_noise_seed ^= rand();
            x = base_x - NOISE_SHORT_SPREAD_X / 2;
            x += NOISE_RANDOM(NOISE_SHORT_SPREAD_X);
            /* FAKE: as long_work */
            short_work = base_y - NOISE_SHORT_SPREAD_Y / 2;
            draw.x = x;
            g_hud_noise_seed ^= rand();
            y = short_work + NOISE_RANDOM(NOISE_SHORT_SPREAD_Y);
            draw.semi = 0;
            draw.ot_index = ot_entry;
            short_work = ft4_cursor;
            draw.ft4_out = short_work;
            draw.y = y;
            ft4_cursor = sprite_DrawScaled(&draw, SPRITE_QUAD_NORMAL);
            quad_pairs_drawn += 1;
        } while (quad_pairs_drawn < ((g_hud_noise_cycle + 1) * 2));
    }
    g_hud_noise_cycle += 1;
    return ft4_cursor;
}

/* Chanbara: the time taken so far, as minutes, seconds and hundredths with
 * marks between them, and under it `count` less one (the opponents
 * before the one now faced), each with its label and a red rule. A 1 is
 * narrower than the other digits and is moved over to sit in the middle of
 * its place. */
s32 hud_DrawTimeAndCount(ClockTime *time, s32 count, s32 prims, s32 ot_index) {
    HudCounterPrims *block = (HudCounterPrims *)ADDR(prims);
    SpriteDraw draw;
    s16 digits[3];
    TILE *tile;
    s32 next_sprt;
    DR_MODE *dr_mode;
    /* where the block ends: the function returns its size */
    s32 end_addr;
    s16 i;
    s16 j;
    s16 digits_started;

    count--;
    tile = block->tiles;
    draw.has_color = 0;
    draw.y = 0;
    draw.x = 0;
    draw.ot_index = ot_index;
    draw.semi = 0;
    draw.header = &g_hud_counter_sheets[COUNTER_SHEET_DIGITS];
    next_sprt = prims + sizeof(block->tiles);
    dr_mode = &block->draw_mode;
    end_addr = prims + sizeof(HudCounterPrims);
    for (i = 0; i < 3; i++) {
        draw.table = &g_hud_time_cells[TIME_CELL_MARKS + i];
        draw.sprt_out = next_sprt;
        next_sprt = sprite_Draw(&draw);
    }

    draw.header = &g_hud_counter_sheets[COUNTER_SHEET_LABELS];
    for (i = 0; i < 2; i++) {
        draw.table = &g_hud_time_cells[TIME_CELL_LABELS + i];
        if (i != 0) {
            if (count == 1) {
                draw.table->w = LABEL_WIDTH_ONE;
            } else {
                draw.table->w = LABEL_WIDTH;
            }
        }
        draw.sprt_out = next_sprt;
        next_sprt = sprite_Draw(&draw);
    }

    draw.header = &g_hud_counter_sheets[COUNTER_SHEET_DIGITS];
    draw.has_color = 0;
    draw.y = COUNTER_DIGITS_Y;
    draw.semi = 0;
    draw.ot_index = ot_index;
    for (j = 0; j < 3; j++) {
        for (i = 0; i < 2; i++) {
            switch (j) {
            case 0:
                digits[i] = time->time_minutes;
                if (i != 0) {
                    digits[i] = digits[i] % 10; /* the ones */
                } else {
                    digits[0] = digits[0] / 10 % 10; /* the tens */
                }
                draw.table = &g_hud_digit_cells[digits[i]];
                if (digits[i] == 1) {
                    draw.x = i * DIGIT_PITCH + DIGIT_ONE_SHIFT;
                } else {
                    draw.x = i * DIGIT_PITCH;
                }
                draw.table->x = TIME_MINUTES_X;
                break;
            case 1:
                digits[i] = time->time_seconds;
                if (i != 0) {
                    digits[i] = digits[i] % 10; /* the ones */
                } else {
                    digits[0] = digits[0] / 10 % 10; /* the tens */
                }
                draw.table = &g_hud_digit_cells[digits[i]];
                if (digits[i] == 1) {
                    draw.x = i * DIGIT_PITCH + DIGIT_ONE_SHIFT;
                } else {
                    draw.x = i * DIGIT_PITCH;
                }
                draw.table->x = TIME_SECONDS_X;
                break;
            case 2:
                digits[i] = time->time_hundredths;
                if (i != 0) {
                    digits[i] = digits[i] % 10; /* the ones */
                } else {
                    digits[0] = digits[0] / 10 % 10; /* the tens */
                }
                draw.table = &g_hud_digit_cells[digits[i]];
                if (digits[i] == 1) {
                    draw.x = i * DIGIT_PITCH + DIGIT_ONE_SHIFT;
                } else {
                    draw.x = i * DIGIT_PITCH;
                }
                draw.table->x = TIME_HUNDREDTHS_X;
                break;
            }
            draw.sprt_out = next_sprt;
            next_sprt = sprite_Draw(&draw);
        }
    }

    draw.header = &g_hud_counter_sheets[COUNTER_SHEET_DIGITS];
    digits[0] = digits[1] = digits[2] = count;
    digits[2] = digits[2] % 10;      /* the ones */
    digits[1] = digits[1] / 10 % 10; /* the tens */
    digits[0] = digits[0] / 100;     /* the hundreds */
    digits[1] = digits[1] % 100;     /* changes nothing: it is under ten */
    draw.y = TIME_COUNT_Y;
    digits_started = 0;
    for (j = 0; j < 3; j++) {
        if (digits_started || digits[j] != 0 || j == 2) {
            draw.table = &g_hud_digit_cells[digits[j]];
            draw.table->x = TIME_COUNT_X;
            digits_started = 1;
            if (digits[j] == 1) {
                draw.x = j * TIME_COUNT_PITCH + DIGIT_ONE_SHIFT;
            } else {
                draw.x = j * TIME_COUNT_PITCH;
            }
            draw.sprt_out = next_sprt;
            next_sprt = sprite_Draw(&draw);
        }
    }

    draw.col_r = RULE_R;
    draw.col_b = RULE_B;
    draw.col_g = RULE_G;
    draw.has_color = 1;
    draw.x = 0;
    draw.semi = 0;
    draw.ot_index = ot_index;
    for (j = 0; j < 2; j++) {
        SetTile(tile);
        setRGB0(tile, RULE_R, RULE_G, RULE_B);
        tile->x0 = g_hud_time_rule_origins[j].x;
        tile->y0 = g_hud_time_rule_origins[j].y;
        tile->w = TIME_RULE_END_X - g_hud_time_rule_origins[j].x;
        tile->h = 1;
        SetSemiTrans(tile, 0);
        AddPrim(g_disp_ot + ot_index, tile);
        tile++;
        draw.y = g_hud_time_rule_origins[j].y;
        draw.header = &g_hud_counter_sheets[COUNTER_SHEET_RULE_END];
        draw.table = &g_hud_time_cells[TIME_CELL_RULE_END];
        draw.sprt_out = next_sprt;
        next_sprt = sprite_Draw(&draw);
        draw.header = &g_hud_counter_sheets[COUNTER_SHEET_RULE_END_2];
        draw.table = &g_hud_time_cells[TIME_CELL_RULE_END + 1];
        draw.sprt_out = next_sprt;
        next_sprt = sprite_Draw(&draw);
    }
    SetDrawMode(
        dr_mode, 1, 0,
        sprite_GetTPage(&g_hud_counter_sheets[COUNTER_SHEET_DIGITS], 0), 0);
    AddPrim(g_disp_ot + ot_index, dr_mode);
    return end_addr - prims;
}

/* How many fights each side has won: two digits at the left for
 * `left_count` and at the right for `right_count`, with labels and red
 * rules. With `left_count` negative only the right one is drawn, under the
 * label of the opponent count, and without a leading zero. */
s32 hud_DrawWinCounts(
    s32 left_count, s32 right_count, s32 prims, s32 ot_index) {
    HudCounterPrims *block = (HudCounterPrims *)ADDR(prims);
    SpriteDraw draw;
    s16 digits[2];
    TILE *tile;
    s32 next_sprt;
    DR_MODE *dr_mode;
    /* where the block ends: the function returns its size */
    s32 end_addr;
    s16 i;
    s16 j;

    tile = block->tiles;
    draw.has_color = 0;
    draw.y = 0;
    draw.x = 0;
    draw.semi = 0;
    draw.header = &g_hud_counter_sheets[COUNTER_SHEET_LABELS];
    next_sprt = prims + sizeof(block->tiles);
    dr_mode = &block->draw_mode;
    end_addr = prims + sizeof(HudCounterPrims);
    draw.ot_index = ot_index;
    for (i = 0; i < 2; i++) {
        if (left_count < 0) {
            draw.table = &g_hud_opponent_label_cell;
            if (right_count == 1) {
                draw.table->w = LABEL_WIDTH_ONE;
            } else {
                draw.table->w = LABEL_WIDTH;
            }
        } else {
            draw.table = &g_hud_win_count_cells[0][i];
        }
        draw.sprt_out = next_sprt;
        next_sprt = sprite_Draw(&draw);
        if (left_count < 0) {
            break;
        }
    }

    draw.header = &g_hud_counter_sheets[COUNTER_SHEET_DIGITS];
    draw.has_color = 0;
    draw.semi = 0;
    draw.y = COUNTER_DIGITS_Y;
    draw.ot_index = ot_index;
    for (j = 0; j < 2; j++) {
        if (j != 0) {
            digits[0] = digits[1] = left_count;
        } else {
            digits[0] = digits[1] = right_count;
        }
        digits[1] = digits[1] % 10;      /* the ones */
        digits[0] = digits[0] / 10 % 10; /* the tens */
        for (i = 0; i < 2; i++) {
            if (digits[i] == 0 && i == 0 && left_count < 0) {
                i++;
            }
            draw.table = &g_hud_digit_cells[digits[i]];
            if (j != 0) {
                draw.table->x = COUNTER_LEFT_X;
            } else {
                draw.table->x = COUNTER_RIGHT_X;
            }
            if (digits[i] == 1) {
                draw.x = i * DIGIT_PITCH + DIGIT_ONE_SHIFT;
            } else {
                draw.x = i * DIGIT_PITCH;
            }
            draw.sprt_out = next_sprt;
            next_sprt = sprite_Draw(&draw);
        }
        if (left_count < 0) {
            break;
        }
    }

    draw.col_r = RULE_R;
    draw.col_b = RULE_B;
    draw.col_g = RULE_G;
    draw.has_color = 1;
    draw.x = 0;
    draw.semi = 0;
    draw.ot_index = ot_index;
    for (j = 0; j < 2; j++) {
        SetTile(tile);
        setRGB0(tile, RULE_R, RULE_G, RULE_B);
        tile->x0 =
            COUNTER_RIGHT_X - j * (COUNTER_RIGHT_X - COUNTER_LEFT_RULE_X);
        tile->y0 = COUNTER_RULE_Y;
        tile->w = COUNTER_RULE_WIDTH;
        tile->h = 1;
        SetSemiTrans(tile, 0);
        AddPrim(g_disp_ot + ot_index, tile);
        tile++;
        draw.y = COUNTER_RULE_Y;
        draw.header = &g_hud_counter_sheets[COUNTER_SHEET_RULE_END];
        draw.table = &g_hud_win_count_cells[j + 1][0];
        draw.sprt_out = next_sprt;
        next_sprt = sprite_Draw(&draw);
        draw.header = &g_hud_counter_sheets[COUNTER_SHEET_RULE_END_2];
        draw.table = &g_hud_win_count_cells[j + 1][1];
        draw.sprt_out = next_sprt;
        next_sprt = sprite_Draw(&draw);
        if (left_count < 0) {
            break;
        }
    }
    SetDrawMode(
        dr_mode, 1, 0,
        sprite_GetTPage(&g_hud_counter_sheets[COUNTER_SHEET_DIGITS], 0), 0);
    AddPrim(g_disp_ot + ot_index, dr_mode);
    return end_addr - prims;
}

/* Against the CPU: `count` less one, the opponents before the one
 * now faced. */
s32 hud_DrawOpponentCount(s32 count, s32 prims, s32 ot_index) {
    return hud_DrawWinCounts(-1, count - 1, prims, ot_index);
}

/* The team battle's table. For each side, a title and a subtitle (one of
 * two, by the page of the character select its team came from). For each
 * match a row: in the middle who won (or that it has not been fought), with
 * a mark for each point; at the sides the two fighters' names, the loser's
 * dimmed, struck through if the match was not fought. Under the rows, each
 * side's points as marks and the number of matches it won. */
s32 hud_DrawTeamScores(u32 packed_scores, s32 prims, s32 ot_index) {
    HudTeamScorePrims *block = (HudTeamScorePrims *)ADDR(prims);
    /* each side's points in a match, then each side's total */
    s16 side_scores[2];
    s16 higher_score_match_counts[2];
    SpriteDraw draw;
    s16 unused[3]; /* nothing uses it; it only takes room in the frame */
    TILE *tile;
    s32 next_sprt;
    s32 quads;
    DR_MODE *dr_mode;
    /* where the block ends: the function returns its size */
    s32 end_addr;
    s16 i;
    s16 j;
    s16 k;
    s16 name_index;
    s16 totals_y;

    tile = block->tiles;
    draw.has_color = 0;
    draw.semi = 0;
    draw.y = 0;
    next_sprt = prims + sizeof(block->tiles);
    quads = (s32)block->quads;
    dr_mode = &block->draw_mode;
    end_addr = prims + sizeof(HudTeamScorePrims);
    draw.ot_index = ot_index;
    for (i = 0; i < 2; i++) {
        if (!(g_game_settings.team_clan_bits >> i & 1)) {
            draw.header = &g_hud_team_title_0_sheet;
        } else {
            draw.header = &g_hud_team_title_1_sheet;
        }
        draw.x = i * (DISP_WIDTH / 2);
        draw.table = g_hud_team_title_cells;
        draw.sprt_out = next_sprt;
        next_sprt = sprite_Draw(&draw);
        if (!(g_game_settings.team_clan_bits >> i & 1)) {
            draw.header = &g_hud_team_subtitle_0_sheet;
            draw.table = g_hud_team_subtitle_0_cells;
        } else {
            draw.header = &g_hud_team_subtitle_1_sheet;
            draw.table = g_hud_team_subtitle_1_cells;
        }
        draw.sprt_out = next_sprt;
        next_sprt = sprite_Draw(&draw);
    }

    draw.semi = 0;
    draw.has_color = 0;
    for (i = 0; i < g_game_settings.match_count_index + TEAM_MIN_MATCHES; i++) {
        side_scores[0] = (packed_scores >> (i * 4)) & 3;
        side_scores[1] = (packed_scores >> (i * 4 + 2)) & 3;
        if (g_game_settings.match_count_index == FIVE_MATCHES) {
            draw.y = i * SCORE_ROW_PITCH + SCORE_ROWS_Y_5;
        } else if (g_game_settings.match_count_index == FOUR_MATCHES) {
            draw.y = i * SCORE_ROW_PITCH + SCORE_ROWS_Y;
        } else {
            draw.y = i * SCORE_ROW_PITCH_3 + SCORE_ROWS_Y;
        }
        if (side_scores[0] == SCORE_UNFOUGHT ||
            side_scores[1] == SCORE_UNFOUGHT) {
            draw.header = &g_hud_team_unfought_sheet;
            draw.x = 0;
            draw.y += SCORE_UNFOUGHT_DROP;
            draw.table =
                &g_hud_team_unfought_cells[g_hud_team_unfought_cell_ids[i]];
            draw.sprt_out = next_sprt;
            next_sprt = sprite_Draw(&draw);
        } else {
            draw.header = &g_hud_team_result_sheet;
            draw.x = 0;
            draw.table = &g_hud_team_center_cell;
            draw.sprt_out = next_sprt;
            next_sprt = sprite_Draw(&draw);
            for (j = 0; j < 2; j++) {
                draw.x = j * SCORE_SIDE_2_X;
                if (side_scores[j] > side_scores[!j]) {
                    draw.table = &g_hud_team_won_cell;
                } else if (side_scores[j] < side_scores[!j]) {
                    draw.table = &g_hud_team_lost_cell;
                } else {
                    draw.table = &g_hud_team_drawn_cell;
                }
                draw.sprt_out = next_sprt;
                next_sprt = sprite_Draw(&draw);
            }
            draw.y += SCORE_MARK_DROP;
            for (j = 0; j < 2; j++) {
                draw.header = &g_hud_team_mark_sheet;
                draw.table = &g_hud_team_mark_cell;
                for (k = 0; k < side_scores[j]; k++) {
                    if (j != 0) {
                        draw.x = k * SCORE_MARK_PITCH + SCORE_MARK_RIGHT_X;
                    } else {
                        draw.x = (1 - k) * SCORE_MARK_PITCH + SCORE_MARK_LEFT_X;
                    }
                    draw.sprt_out = next_sprt;
                    next_sprt = sprite_Draw(&draw);
                }
            }
        }
    }

    draw.header = &g_hud_team_result_sheet;
    draw.x = 0;
    if (g_game_settings.match_count_index == FIVE_MATCHES) {
        totals_y = SCORE_TOTALS_Y_5;
    } else if (g_game_settings.match_count_index == FOUR_MATCHES) {
        totals_y = SCORE_TOTALS_Y_4;
    } else {
        totals_y = SCORE_TOTALS_Y_3;
    }
    draw.y = totals_y + SCORE_TOTALS_DROP;
    draw.table = &g_hud_team_center_cell;
    draw.sprt_out = next_sprt;
    next_sprt = sprite_Draw(&draw);
    /* FAKE: side_scores[0] = side_scores[1] = 0 */
    *(s32 *)side_scores = 0;
    for (i = 0; i < g_game_settings.match_count_index + TEAM_MIN_MATCHES; i++) {
        if (((packed_scores >> (i * 4)) & 3) != SCORE_UNFOUGHT) {
            side_scores[0] += (packed_scores >> (i * 4)) & 3;
        }
        if (((packed_scores >> (i * 4 + 2)) & 3) != SCORE_UNFOUGHT) {
            side_scores[1] += (packed_scores >> (i * 4 + 2)) & 3;
        }
    }
    for (j = 0; j < 2; j++) {
        draw.header = &g_hud_team_mark_sheet;
        draw.table = &g_hud_team_mark_cell;
        for (k = 0; k < side_scores[j]; k++) {
            if (j != 0) {
                draw.x =
                    (k >> 1) * SCORE_TOTAL_COLUMN_PITCH + SCORE_TOTAL_RIGHT_X;
            } else {
                draw.x =
                    SCORE_TOTAL_LEFT_X - (k >> 1) * SCORE_TOTAL_COLUMN_PITCH;
            }
            draw.y = totals_y + (k & 1) * SCORE_TOTAL_MARK_PITCH;
            draw.sprt_out = next_sprt;
            next_sprt = sprite_Draw(&draw);
        }
    }
    SetDrawMode(
        dr_mode, 1, 0, sprite_GetTPage(&g_hud_team_title_0_sheet, 0), 0);
    AddPrim(g_disp_ot + ot_index, dr_mode);
    /* one more DR_MODE, past the size this function returns: what the
     * caller builds next is written over it (the original's behaviour) */
    dr_mode++;

    draw.ot_index = ot_index;
    higher_score_match_counts[0] = higher_score_match_counts[1] = 0;
    for (i = 0; i < g_game_settings.match_count_index + TEAM_MIN_MATCHES; i++) {
        draw.header = &g_hud_name_sheet;
        draw.semi = 0;
        side_scores[0] = (packed_scores >> (i * 4)) & 3;
        side_scores[1] = (packed_scores >> (i * 4 + 2)) & 3;
        draw.col_r = draw.col_g = draw.col_b = DIM_GRAY;
        for (j = 0; j < 2; j++) {
            if (side_scores[j] <= side_scores[!j]) {
                if (side_scores[j] != SCORE_UNFOUGHT) {
                    draw.has_color = 1;
                } else {
                    draw.has_color = 0;
                }
            } else {
                if (side_scores[j] != SCORE_UNFOUGHT) {
                    higher_score_match_counts[j]++;
                }
                draw.has_color = 0;
            }
            name_index = g_game_settings.match_picks[j][i].fighter;
            if (name_index >= FIGHTERS_FIRST_TWELVE) {
                name_index -= FIGHTERS_UNNAMED;
            }
            draw.table = g_hud_name_cells[name_index];
            draw.x =
                j * (DISP_WIDTH / 2) + g_hud_team_name_x_offsets[name_index];
            if (g_game_settings.match_count_index == FIVE_MATCHES) {
                draw.y = i * SCORE_ROW_PITCH + SCORE_ROWS_Y_5 + SCORE_NAME_Y;
            } else if (g_game_settings.match_count_index == FOUR_MATCHES) {
                draw.y = i * SCORE_ROW_PITCH + SCORE_ROWS_Y + SCORE_NAME_Y;
            } else {
                draw.y = i * SCORE_ROW_PITCH_3 + SCORE_ROWS_Y + SCORE_NAME_Y;
            }
            draw.sprt_out = next_sprt;
            next_sprt = sprite_Draw(&draw);
            if (g_game_settings.match_picks[j][i].fighter ==
                FIGHTER_LONG_NAME) {
                draw.table = g_hud_name_8_tail_cells;
                draw.sprt_out = next_sprt;
                next_sprt = sprite_Draw(&draw);
            }
        }
        draw.has_color = 0;
        if (side_scores[0] == SCORE_UNFOUGHT) {
            /* a line through both names: the rule's end pieces, four times
             * as high */
            draw.y -= SCORE_NAME_Y;
            draw.scale_x = SPRITE_SCALE_ONE;
            draw.x = 0;
            draw.semi = 0;
            draw.scale_y = 4 * SPRITE_SCALE_ONE;
            draw.y += SCORE_STRIKE_DROP;
            for (j = 0; j < 2; j++) {
                draw.header = &g_hud_counter_sheets[COUNTER_SHEET_RULE_END + j];
                draw.table = g_hud_team_strike_cells[j];
                draw.ft4_out = quads;
                quads = sprite_DrawScaled(&draw, SPRITE_QUAD_NORMAL);
                draw.table = &g_hud_team_strike_cells[j][1];
                draw.ft4_out = quads;
                quads = sprite_DrawScaled(&draw, SPRITE_QUAD_NORMAL);
            }
        }
    }

    draw.header = &g_hud_counter_sheets[COUNTER_SHEET_DIGITS];
    draw.semi = 0;
    if (g_game_settings.match_count_index == FIVE_MATCHES) {
        draw.y = SCORE_TOTALS_Y_5 + SCORE_TOTALS_DROP;
    } else if (g_game_settings.match_count_index == FOUR_MATCHES) {
        draw.y = SCORE_TOTALS_Y_4 + SCORE_TOTALS_DROP;
    } else {
        draw.y = SCORE_TOTALS_Y_3 + SCORE_TOTALS_DROP;
    }
    for (j = 0; j < 2; j++) {
        draw.x = j * SCORE_SIDE_2_X + SCORE_WINS_X;
        if (higher_score_match_counts[j] == 1) {
            draw.x += DIGIT_ONE_SHIFT;
        }
        draw.table = &g_hud_digit_cells[higher_score_match_counts[j]];
        draw.table->x = draw.table->y = 0;
        draw.sprt_out = next_sprt;
        next_sprt = sprite_Draw(&draw);
    }

    SetTile(tile);
    setRGB0(tile, RULE_R, RULE_G, RULE_B);
    tile->x0 = SCORE_TITLE_RULE_X;
    tile->y0 = SCORE_TITLE_RULE_Y;
    tile->w = SCORE_TITLE_RULE_WIDTH;
    tile->h = 1;
    SetSemiTrans(tile, 0);
    AddPrim(g_disp_ot + ot_index, tile);
    tile++;
    SetTile(tile);
    setRGB0(tile, RULE_R, RULE_G, RULE_B);
    tile->x0 = SCORE_TITLE_RULE_SIDE_2_X;
    tile->y0 = SCORE_TITLE_RULE_Y;
    tile->w = SCORE_TITLE_RULE_WIDTH;
    tile->h = 1;
    SetSemiTrans(tile, 0);
    AddPrim(g_disp_ot + ot_index, tile);
    tile++;
    SetTile(tile);
    setRGB0(tile, RULE_R, RULE_G, RULE_B);
    if (g_game_settings.match_count_index == FIVE_MATCHES) {
        setXY0(tile, SCORE_TOTALS_RULE_X,
               SCORE_TOTALS_Y_5 - SCORE_TOTALS_RULE_RISE);
    } else if (g_game_settings.match_count_index == FOUR_MATCHES) {
        setXY0(tile, SCORE_TOTALS_RULE_X,
               SCORE_TOTALS_Y_4 - SCORE_TOTALS_RULE_RISE);
    } else {
        setXY0(tile, SCORE_TOTALS_RULE_X,
               SCORE_TOTALS_Y_3 - SCORE_TOTALS_RULE_RISE);
    }
    tile->w = SCORE_TOTALS_RULE_WIDTH;
    tile->h = 1;
    SetSemiTrans(tile, 0);
    AddPrim(g_disp_ot + ot_index, tile);
    SetDrawMode(dr_mode, 1, 0, sprite_GetTPage(&g_hud_name_sheet, 0), 0);
    AddPrim(g_disp_ot + ot_index, dr_mode);
#if RB_FIX_HUD_DRAW_MODE
    /* the second drawing mode is part of what was built */
    if (g_rb_settings.bugfixes) {
        return end_addr - prims + sizeof(DR_MODE);
    }
#endif
    return end_addr - prims;
}

/* The team battle's match: each side's points and warnings as rows of
 * marks, filled or empty, with labels and red rules; and in the middle the
 * time left, seconds and hundredths, three digits of seconds when the time
 * limit is the long one. */
s32 hud_DrawPointsAndTime(
    ClockTime *time, s32 packed_marks, s32 prims, s32 ot_index) {
    HudCounterPrims *block = (HudCounterPrims *)ADDR(prims);
    SpriteDraw draw;
    s16 digits[3];
    TILE *tile;
    s32 next_sprt;
    DR_MODE *dr_mode;
    /* where the block ends: the function returns its size */
    s32 end_addr;
    s16 mark_group_index;
    s16 j;
    s16 k;
    s16 side_index;
    s16 packed_side_mark_counts;
    s16 mark_slot_count;
    s32 x;

    draw.has_color = 0;
    draw.y = 0;
    draw.semi = 0;
    tile = block->tiles;
    next_sprt = prims + sizeof(block->tiles);
    dr_mode = &block->draw_mode;
    end_addr = prims + sizeof(HudCounterPrims);
    draw.ot_index = ot_index;
    for (mark_group_index = 0; mark_group_index < POINT_ROWS;
         mark_group_index++) {
        draw.header = &g_hud_point_sheets[mark_group_index];
        /* a byte for the points and one for the warnings */
        packed_side_mark_counts =
            (packed_marks >> (mark_group_index * 8)) & 0xFF;
        if (mark_group_index != 0) {
            mark_slot_count = WARNING_SLOTS;
        } else {
            mark_slot_count = g_game_settings.win_target_index + 1;
        }
        for (side_index = 0; side_index < 2; side_index++) {
            for (k = 0; k < mark_slot_count; k++) {
                if (side_index != 0) {
                    draw.x = mark_group_index * MARK_SHRINK + MARK_RIGHT_X -
                             (MARK_PITCH - mark_group_index * MARK_SHRINK) * k;
                } else {
                    draw.x = (MARK_PITCH - mark_group_index * MARK_SHRINK) * k;
                }
                draw.table = &g_hud_point_cells[mark_group_index][0];
                /* four bits a side: past its count the mark is empty */
                if (k >=
                    ((packed_side_mark_counts >> (side_index * 4)) & 0xF)) {
                    draw.table++;
                }
                draw.sprt_out = next_sprt;
                next_sprt = sprite_Draw(&draw);
            }
        }
    }
    SetDrawMode(dr_mode, 1, 0, sprite_GetTPage(&g_hud_point_sheets[0], 0), 0);
    AddPrim(g_disp_ot + ot_index, dr_mode);
    /* one more DR_MODE, past the size this function returns: what the
     * caller builds next is written over it (the original's behaviour) */
    dr_mode++;

    draw.has_color = 0;
    draw.y = 0;
    draw.semi = 0;
    draw.header = &g_hud_counter_sheets[COUNTER_SHEET_LABELS];
    draw.ot_index = ot_index;
    for (k = 0; k < 2; k++) {
        for (j = 0; j < 2; j++) {
            draw.x = j * POINT_LABEL_SIDE_2_X;
            draw.table = &g_hud_point_label_cells[k];
            draw.sprt_out = next_sprt;
            next_sprt = sprite_Draw(&draw);
        }
    }

    draw.col_r = RULE_R;
    draw.col_b = RULE_B;
    draw.col_g = RULE_G;
    draw.has_color = 1;
    draw.x = 0;
    draw.semi = 0;
    draw.ot_index = ot_index;
    for (k = 0; k < 2; k++) {
        for (j = 0; j < 2; j++) {
            SetTile(tile);
            setRGB0(tile, RULE_R, RULE_G, RULE_B);
            tile->x0 = COUNTER_LEFT_RULE_X + j * POINT_RULE_SIDE_2_X +
                       j * (k << 4); /* k times twice MARK_SHRINK */
            tile->y0 = k * POINT_ROW_PITCH + COUNTER_RULE_Y;
            tile->w = POINT_RULE_WIDTH - k * (2 * MARK_SHRINK);
            tile->h = 1;
            SetSemiTrans(tile, 0);
            AddPrim(g_disp_ot + ot_index, tile);
            tile++;
            draw.y = k * POINT_ROW_PITCH + COUNTER_RULE_Y;
            draw.header = &g_hud_counter_sheets[COUNTER_SHEET_RULE_END];
            draw.table = &g_hud_point_rule_cells[j][0];
            draw.sprt_out = next_sprt;
            next_sprt = sprite_Draw(&draw);
            draw.header = &g_hud_counter_sheets[COUNTER_SHEET_RULE_END_2];
            draw.table = &g_hud_point_rule_cells[j][1];
            draw.sprt_out = next_sprt;
            next_sprt = sprite_Draw(&draw);
        }
    }

    draw.header = &g_hud_counter_sheets[COUNTER_SHEET_DIGITS];
    draw.has_color = 0;
    draw.y = COUNTER_DIGITS_Y;
    draw.semi = 0;
    draw.ot_index = ot_index;
    for (j = 0; j < 2; j++) {
        /* Three turns for a number of two digits (the seconds have three
         * only under the two-minute limit): the third sets nothing, and
         * the digit before it is drawn again where it is. The original's
         * behaviour. */
        for (k = 0; k < 3; k++) {
            switch (j) {
            case 0:
                if (k < 2 || g_game_settings.time_limit_index ==
                                 TIME_LIMIT_TWO_MINUTES) {
                    digits[k] = time->time_seconds;
                    if (k == 0 && g_game_settings.time_limit_index ==
                                      TIME_LIMIT_TWO_MINUTES) {
                        digits[k] = digits[k] / 100; /* the hundreds */
                    } else if (
                        k == 0 || (k == 1 && g_game_settings.time_limit_index ==
                                                 TIME_LIMIT_TWO_MINUTES)) {
                        digits[k] = digits[k] / 10; /* the tens */
                    }
                    digits[k] = digits[k] % 10; /* the ones */
                    draw.table = &g_hud_digit_cells[digits[k]];
                    if (digits[k] == 1) {
                        draw.x = k * DIGIT_PITCH + DIGIT_ONE_SHIFT;
                    } else {
                        draw.x = k * DIGIT_PITCH;
                    }
                }
                break;
            case 1:
                if (k < 2) {
                    digits[k] = time->time_hundredths;
                    if (k != 0) {
                        digits[k] = digits[k] % 10; /* the ones */
                    } else {
                        digits[k] = digits[k] / 10 % 10; /* the tens */
                    }
                    x = (g_game_settings.time_limit_index ==
                         TIME_LIMIT_TWO_MINUTES)
                            ? k * DIGIT_PITCH + MATCH_TIME_HUNDREDTHS +
                                  DIGIT_PITCH
                            : k * DIGIT_PITCH + MATCH_TIME_HUNDREDTHS;
                    draw.table = &g_hud_digit_cells[digits[k]];
                    if (digits[k] == 1) {
                        draw.x = x + DIGIT_ONE_SHIFT;
                    } else {
                        draw.x = x;
                    }
                }
                break;
            }
            if (g_game_settings.time_limit_index == TIME_LIMIT_TWO_MINUTES) {
                draw.table->x = MATCH_TIME_X - MATCH_TIME_LONG_SHIFT;
            } else {
                draw.table->x = MATCH_TIME_X;
            }
            draw.sprt_out = next_sprt;
            next_sprt = sprite_Draw(&draw);
        }
        draw.table = &g_hud_time_separator_cell;
        if (g_game_settings.time_limit_index == TIME_LIMIT_TWO_MINUTES) {
            draw.x = j * MATCH_TIME_MARK_PITCH + MATCH_TIME_MARK_X +
                     MATCH_TIME_LONG_SHIFT;
        } else {
            draw.x = j * MATCH_TIME_MARK_PITCH + MATCH_TIME_MARK_X;
        }
        draw.sprt_out = next_sprt;
        next_sprt = sprite_Draw(&draw);
    }
    SetDrawMode(
        dr_mode, 1, 0,
        sprite_GetTPage(&g_hud_counter_sheets[COUNTER_SHEET_LABELS], 0), 0);
    AddPrim(g_disp_ot + ot_index, dr_mode);
#if RB_FIX_HUD_DRAW_MODE
    /* the second drawing mode is part of what was built */
    if (g_rb_settings.bugfixes) {
        return end_addr - prims + sizeof(DR_MODE);
    }
#endif
    return end_addr - prims;
}

/* A caption across the screen, stretched to twice its width, on a band. */
s32 hud_DrawCaption(s32 caption, s32 prims, s32 ot_index) {
    SpriteDraw draw;
    s32 quads = prims;
    /* where the block ends (ten quads): the function returns its size */
    s32 end_addr = prims + 10 * sizeof(POLY_FT4);

    draw.scale_x = 2 * SPRITE_SCALE_ONE;
    draw.scale_y = SPRITE_SCALE_ONE;
    draw.header = &g_hud_caption_sheets[caption];
    draw.has_color = 0;
    draw.y = 0;
    draw.x = 0;
    draw.semi = 0;
    draw.ot_index = ot_index;
    switch (caption) {
    case HUD_CAPTION_REMATCH:
        draw.table = g_hud_caption_rematch_cells;
        break;
    case HUD_CAPTION_IPPON:
        draw.table = &g_hud_caption_ippon_cell;
        break;
    case HUD_CAPTION_OUT_OF_BOUNDS:
        draw.table = g_hud_caption_out_cells;
        break;
    }
    draw.ft4_out = quads;
    quads = sprite_DrawScaled(&draw, SPRITE_QUAD_NORMAL);
    draw.header = &g_hud_caption_backing_sheets[caption];
    draw.table = &g_hud_caption_backing_cell;
    draw.ft4_out = quads;
    sprite_DrawScaled(&draw, SPRITE_QUAD_NORMAL);
    return end_addr - prims;
}

/* Reads ending picture `index` from the disc and puts it and its colour
 * table into video memory. `file_buffer` is where the file is read. */
void hud_LoadEndingPicture(s32 index, u8 *file_buffer) {
    RECT image_rect, clut_rect;
    s32 file_index;
    file_index =
        cdrom_GetFileIndex(CDROM_GROUP_DATA, index + ENDING_FIRST_FILE);
    cdrom_StartRead(file_index, (s32)file_buffer);
    cdrom_WaitIdle();
    cdrom_GetFileSize(file_index);
    image_rect = g_hud_ending_image_rect;
    clut_rect = g_hud_ending_clut_rect;
    /* a picture of sixteen colours */
    LoadImage(&image_rect, (u32 *)(file_buffer + TIM_PIXELS(16)));
    DrawSync(0);
    LoadImage(&clut_rect, (u32 *)(file_buffer + TIM_CLUT));
    DrawSync(0);
    g_hud_ending_frame = 0;
}

/* Draws the ending's picture over the scene, in two halves. For the first
 * six seconds it covers the screen. Then the halves slide apart, 24 pixels
 * a frame, each clipped to its side of the gap and with a red edge that
 * fades as it goes, and the scene shows between them. */
s32 hud_DrawEndingPicture(s32 prims, s32 ot_index) {
    HudEndingPrims *block = (HudEndingPrims *)ADDR(prims);
    SpriteDraw draw;
    RECT clip; /* the part of the screen a half may draw in */
    RECT *draw_clip;
    GpuDb *gpu_db;
    s32 next_sprt;
    DR_MODE *dr_mode;
    POLY_G4 *gradient_quad_cursor;
    DR_AREA *area;
    /* where the block ends: the function returns its size */
    s32 end_addr;
    s16 side_index;
    s16 sheet_index;
    s16 off;
    s16 x;
    u8 red; /* the edge's brightness */

    next_sprt = prims;
    dr_mode = &block->draw_mode;
    gradient_quad_cursor = block->gradients;
    area = block->areas;
    end_addr = prims + sizeof(HudEndingPrims);
    side_index = 0;
    gpu_db = &g_disp_db[g_disp_frame_count & 1];
    clip.x = gpu_db->draw.clip.x;
    clip.y = gpu_db->draw.clip.y;
    clip.w = gpu_db->draw.clip.w;
    clip.h = gpu_db->draw.clip.h;
    draw_clip = &gpu_db->draw.clip;
    SetDrawArea(area, &clip);
    AddPrim(g_disp_ot + ot_index, area);
    area++;
    draw.has_color = 0;
    draw.semi = 0;
    draw.x = 0;
    draw.ot_index = ot_index;
    draw.table = g_hud_ending_cells;
    off = (g_hud_ending_frame - ENDING_HOLD_FRAMES) * ENDING_SLIDE_SPEED;
    for (; side_index < 2; side_index++) {
        if (g_hud_ending_frame >= ENDING_HOLD_FRAMES + 1) {
            SetPolyG4(gradient_quad_cursor);
            SetSemiTrans(gradient_quad_cursor, 1);
            if (side_index != 0) {
                x = off + DISP_WIDTH / 2;
                clip.x = draw_clip->x + x;
                clip.y = draw_clip->y;
                clip.w = draw_clip->w / 2 - off;
                clip.h = draw_clip->h;
                setXY4(gradient_quad_cursor, x, 0, x, DISP_HEIGHT,
                       off + DISP_WIDTH / 2 + ENDING_EDGE_WIDTH, 0,
                       off + DISP_WIDTH / 2 + ENDING_EDGE_WIDTH, DISP_HEIGHT);
            } else {
                clip.x = draw_clip->x;
                clip.y = draw_clip->y;
                clip.w = draw_clip->w / 2 - off;
                clip.h = draw_clip->h;
                setXY4(gradient_quad_cursor, DISP_WIDTH / 2 - off, 0,
                       DISP_WIDTH / 2 - off, DISP_HEIGHT,
                       DISP_WIDTH / 2 - ENDING_EDGE_WIDTH - off, 0,
                       DISP_WIDTH / 2 - ENDING_EDGE_WIDTH - off, DISP_HEIGHT);
            }
            /* from full red to none as the half goes off the screen */
            red = ~((off * 255) / (DISP_WIDTH / 2));
            setRGB0(gradient_quad_cursor, red, 0, 0);
            setRGB1(gradient_quad_cursor, red, 0, 0);
            setRGB2(gradient_quad_cursor, 0, 0, 0);
            setRGB3(gradient_quad_cursor, 0, 0, 0);
            AddPrim(g_disp_ot + ot_index, gradient_quad_cursor);
            gradient_quad_cursor++;
        }
        for (sheet_index = 0; sheet_index < 2; sheet_index++) {
            draw.header = &g_hud_ending_sheets[sheet_index];
            draw.y = sheet_index * ENDING_STRIP_HEIGHT;
            draw.sprt_out = next_sprt;
            next_sprt = sprite_Draw(&draw);
        }
        if (g_hud_ending_frame >= ENDING_HOLD_FRAMES + 1) {
            SetDrawArea(area, &clip);
            AddPrim(g_disp_ot + ot_index, area);
            area++;
        }
    }
    SetDrawMode(
        dr_mode, 1, 0, sprite_GetTPage(g_hud_ending_sheets, SPRITE_ABR_ADD), 0);
    AddPrim(g_disp_ot + ot_index, dr_mode);
    if (off <= DISP_WIDTH / 2) {
        g_hud_ending_frame++;
    }
    return end_addr - prims;
}

/* How many opponents were beaten: one or two large digits and the words
 * around them, moved over when there are two. */
s32 hud_DrawKillCount(s32 count, s32 prims, s32 ot_index) {
    SpriteDraw draw;
    s16 digits[2];
    HudKillCountPrims *block = (HudKillCountPrims *)ADDR(prims);
    DR_MODE *dr_mode = &block->draw_mode;
    /* where the block ends: the function returns its size */
    s32 end_addr = prims + sizeof(HudKillCountPrims);
    s32 next_sprt = prims;
    s32 digit_index;

    draw.header = &g_hud_kill_count_sheets[0];
    draw.has_color = 0;
    draw.semi = 0;
    draw.y = 0;
    draw.ot_index = ot_index;
    if (count < 10) { /* one digit */
        draw.x = KILL_WORDS_X;
    } else {
        draw.x = KILL_WORDS_X + KILL_TWO_SHIFT;
    }
    draw.table = g_hud_kill_count_cells;
    draw.sprt_out = next_sprt;
    next_sprt = sprite_Draw(&draw);
    draw.header = &g_hud_kill_count_sheets[1];
    digits[0] = digits[1] = count;
    digits[1] = digits[1] / 10 % 10; /* the tens */
    digits[0] = digits[0] % 10;      /* the ones */
    for (digit_index = 0; digit_index < 2; digit_index++) {
        draw.table = &g_hud_large_digit_cells[digits[digit_index]];
        if (count < 10) { /* one digit */
            draw.x = KILL_DIGIT_X;
        } else {
            draw.x = (1 - digit_index) * KILL_DIGIT_WIDTH + KILL_DIGIT_X -
                     KILL_TWO_SHIFT;
        }
        draw.sprt_out = next_sprt;
        next_sprt = sprite_Draw(&draw);
        if (digits[1] == 0) {
            break;
        }
    }
    SetDrawMode(dr_mode, 1, 0, sprite_GetTPage(g_hud_kill_count_sheets, 0), 0);
    AddPrim(g_disp_ot + ot_index, dr_mode);
    return end_addr - prims;
}

/* Reads stage card `index` from the disc: two pictures, the stage's name
 * and a view of it, each with its colour table. */
void hud_LoadStageCard(s32 index, s32 *file_buffer) {
    RECT name_image_rect, name_clut_rect, picture_image_rect, picture_clut_rect;
    s32 file_index;
    u8 *src_first_image;
    u8 *src_second_image;
    file_index =
        cdrom_GetFileIndex(CDROM_GROUP_DATA, index + STAGE_CARD_FIRST_FILE);
    cdrom_StartRead(file_index, (s32)file_buffer);
    cdrom_WaitIdle();
    cdrom_GetFileSize(file_index);
    /* the file begins with where its two pictures are */
    file_buffer[0] = file_buffer[0] + (s32)file_buffer;
    file_buffer[1] = file_buffer[1] + (s32)file_buffer;
    name_image_rect = g_hud_card_name_image_rect;
    name_clut_rect = g_hud_card_name_clut_rect;
    src_first_image = (u8 *)ADDR(file_buffer[0]);
    /* the name: sixteen colours */
    LoadImage(&name_image_rect, (u32 *)(src_first_image + TIM_PIXELS(16)));
    DrawSync(0);
    LoadImage(&name_clut_rect, (u32 *)(src_first_image + TIM_CLUT));
    DrawSync(0);
    picture_image_rect = g_hud_card_picture_image_rect;
    picture_clut_rect = g_hud_card_picture_clut_rect;
    src_second_image = (u8 *)ADDR(file_buffer[1]);
    /* the view: thirty-two colours */
    LoadImage(&picture_image_rect, (u32 *)(src_second_image + TIM_PIXELS(32)));
    DrawSync(0);
    LoadImage(&picture_clut_rect, (u32 *)(src_second_image + TIM_CLUT));
    DrawSync(0);
}

/* One mark on the stage card's map. `place` picks where from the table of
 * places; with bit 15 set it is the last mark, which has a look of its own,
 * and the others have one of two: one for the characters before GENGORO,
 * who are the Narukagami's, and one for the Shainto's. */
s32 hud_DrawStageCardMark(s32 place, s32 prims, s32 ot_index) {
    SpriteDraw draw;
    HudCardMarkPrims *block = (HudCardMarkPrims *)ADDR(prims);
    DR_MODE *dr_mode;
    /* where the block ends: the function returns its size */
    s32 end_addr;
    dr_mode = &block->draw_mode;
    end_addr = prims + sizeof(HudCardMarkPrims);
    draw.has_color = 0;
    draw.semi = 0;
    draw.ot_index = ot_index;
    draw.x = (g_hud_card_mark_positions[place & (HUD_CARD_MARK_LAST - 1)] >>
              HUD_CARD_MARK_Y_BITS) +
             STAGE_CARD_MAP_X;
    draw.y = (g_hud_card_mark_positions[place & (HUD_CARD_MARK_LAST - 1)] &
              ((1 << HUD_CARD_MARK_Y_BITS) - 1)) +
             STAGE_CARD_MAP_Y;
    if (place & HUD_CARD_MARK_LAST) {
        draw.header = &g_hud_card_mark_sheets[STAGE_CARD_MARK_SHEET_LAST];
    } else if (
        g_game_settings.match_picks[0][0].fighter < FIGHTERS_FIRST_TWELVE) {
        draw.header = &g_hud_card_mark_sheets[STAGE_CARD_MARK_SHEET_NARUKAGAMI];
    } else {
        draw.header = &g_hud_card_mark_sheets[STAGE_CARD_MARK_SHEET_SHAINTO];
    }
    draw.table = &g_hud_card_mark_cell;
    draw.sprt_out = (s32)&block->sprite;
    sprite_Draw(&draw);
    SetDrawMode(dr_mode, 1, 0, sprite_GetTPage(draw.header, 0), 0);
    AddPrim(g_disp_ot + ot_index, dr_mode);
    return end_addr - prims;
}

/* Story mode's stage card: its title, the picture and name of the stage,
 * the map at twice its width, and the two ends of a red rule. */
s32 hud_DrawStageCard(s32 prims, s32 ot_index) {
    HudStageCardPrims *block = (HudStageCardPrims *)ADDR(prims);
    SpriteDraw draw;
    /* where the block ends: the function returns its size */
    s32 end_addr;
    s32 quads;
    s32 sheet_index;
    s32 i;
    s32 next_sprt;
    DR_MODE *dr_mode;
    next_sprt = prims;
    quads = (s32)block->quads;
    draw.has_color = 0;
    draw.semi = 0;
    draw.ot_index = ot_index;
    dr_mode = &block->draw_mode;
    end_addr = prims + sizeof(HudStageCardPrims);
    draw.scale_x = 2 * SPRITE_SCALE_ONE;
    draw.scale_y = SPRITE_SCALE_ONE;
    draw.y = 0;
    draw.x = 0;
    for (sheet_index = 0; sheet_index < STAGE_CARD_SHEETS; sheet_index++) {
        draw.header = &g_hud_card_sheets[sheet_index];
        switch (sheet_index) {
        case STAGE_CARD_SHEET_TITLE:
            draw.table = g_hud_card_title_cells;
            break;
        case STAGE_CARD_SHEET_PICTURE:
        case STAGE_CARD_SHEET_PICTURE + 1:
            draw.table = g_hud_card_picture_cells;
            break;
        case STAGE_CARD_SHEET_MAP:
            draw.table = &g_hud_card_map_cell;
            draw.ft4_out = quads;
            quads = sprite_DrawScaled(&draw, SPRITE_QUAD_NORMAL);
            break;
        }
        if (sheet_index != STAGE_CARD_SHEET_MAP) {
            draw.sprt_out = next_sprt;
            next_sprt = sprite_Draw(&draw);
        }
    }
    draw.header = &g_hud_card_name_sheet;
    draw.table = g_hud_card_name_cells;
    draw.sprt_out = next_sprt;
    next_sprt = sprite_Draw(&draw);
    draw.col_r = RULE_R;
    draw.col_b = RULE_B;
    draw.col_g = RULE_G;
    draw.has_color = 1;
    for (i = 0; i < 2; i++) {
        draw.header = &g_hud_counter_sheets[COUNTER_SHEET_RULE_END + i];
        draw.table = &g_hud_card_rule_cells[i];
        draw.sprt_out = next_sprt;
        next_sprt = sprite_Draw(&draw);
    }
    SetDrawMode(dr_mode, 1, 0, sprite_GetTPage(draw.header, 0), 0);
    AddPrim(g_disp_ot + ot_index, dr_mode);
    return end_addr - prims;
}

/* The best times' bars start again from nothing. */
void hud_ResetBestTimes(void) {
    g_hud_best_times_bar = 0;
    g_hud_best_times_row_bar = 0;
}

/* The best times' block of primitives: a hundred sprites, ten tiles and
 * fifty DR_MODEs. */
#define BEST_TIMES_TILES (100 * sizeof(SPRT))
#define BEST_TIMES_DRAW_MODES (BEST_TIMES_TILES + 10 * sizeof(TILE))
#define BEST_TIMES_SIZE (BEST_TIMES_DRAW_MODES + 50 * sizeof(DR_MODE))

/* The bar under the row a run made, whose rows are BEST_TIMES_ROW_PITCH apart,
 * and the bar under the total; each grows to its width in BEST_BAR_FRAMES.
 * Behind the table and behind the total, a box that darkens the scene. */
#define BEST_ROW_BAR_X 106
#define BEST_ROW_BAR_Y 91
#define BEST_ROW_BAR_WIDTH 426
#define BEST_TOTAL_BAR_X 158
#define BEST_TOTAL_BAR_Y 189
#define BEST_TOTAL_BAR_WIDTH 324
#define BEST_BAR_HEIGHT 2
#define BEST_BAR_FRAMES FRAMES_PER_SECOND
#define BEST_TABLE_BOX_X 63
#define BEST_TABLE_BOX_Y 45
#define BEST_TABLE_BOX_WIDTH 514
#define BEST_TABLE_BOX_HEIGHT 108
#define BEST_TOTAL_BOX_X 146
#define BEST_TOTAL_BOX_Y 170
#define BEST_TOTAL_BOX_WIDTH 348
#define BEST_TOTAL_BOX_HEIGHT 26

/* FAKE: ((TILE *)(tile))->r0 and so on */
#define TILE_R(tile) (*(u8 *)ADDR((tile) + 4))
#define TILE_G(tile) (*(u8 *)ADDR((tile) + 5))
#define TILE_B(tile) (*(u8 *)ADDR((tile) + 6))
#define TILE_X(tile) (*(s16 *)ADDR((tile) + 8))
#define TILE_Y(tile) (*(s16 *)ADDR((tile) + 10))
#define TILE_W(tile) (*(s16 *)ADDR((tile) + 12))
#define TILE_H(tile) (*(s16 *)ADDR((tile) + 14))

/* The best times after a chanbara run: the table itself (drawn by the
 * menu's function, from this file's sheets), a red bar growing under the
 * row `rank` if the run made the table, another under the total, and two
 * darkened boxes behind them. The bars take a second to reach full width. */
s32 hud_DrawBestTimes(s32 prims, s32 ot_index, s32 rank) {
    s32 sprt_out_addr;
    s32 dr_mode_out_addr;
    /* each bar's next length, then its width before the division */
    s32 row_work;
    s32 total_work;
    u16 row_bar;
    u16 total_bar;
    /* where the block ends: the function returns its size */
    s32 end_addr;
    s32 tile_out_addr;

    tile_out_addr = prims + BEST_TIMES_TILES;
    end_addr = prims + BEST_TIMES_SIZE;
    sprt_out_addr = prims;
    dr_mode_out_addr = prims + BEST_TIMES_DRAW_MODES;
#if RB_PORT
    hud_FillBestTimesSheets();
#endif
    menu_DrawBestTimes(&sprt_out_addr, &dr_mode_out_addr,
                       &g_hud_best_times_sheets, ot_index, rank);
    if (rank < (u32)BEST_TIMES_ROWS) {
        /* the row's bar, red */
        SetTile((TILE *)ADDR(tile_out_addr));
        TILE_R(prims + BEST_TIMES_TILES) = 0xFF;
        TILE_X(prims + BEST_TIMES_TILES) = BEST_ROW_BAR_X;
        TILE_G(prims + BEST_TIMES_TILES) = 0;
        TILE_B(prims + BEST_TIMES_TILES) = 0;
        TILE_Y(prims + BEST_TIMES_TILES) =
            rank * BEST_TIMES_ROW_PITCH + BEST_ROW_BAR_Y;
        row_bar = g_hud_best_times_row_bar;
        /* FAKE: the bar grows by one, and its width needs no row_work */
        row_work = row_bar + 1;
        g_hud_best_times_row_bar = row_work;
        row_work = ((s16)row_bar) * BEST_ROW_BAR_WIDTH;
        TILE_H(prims + BEST_TIMES_TILES) = BEST_BAR_HEIGHT;
        TILE_W(prims + BEST_TIMES_TILES) = row_work / BEST_BAR_FRAMES;
        if (g_hud_best_times_row_bar >= BEST_BAR_FRAMES + 1) {
            g_hud_best_times_row_bar = BEST_BAR_FRAMES;
        }
        SetSemiTrans((TILE *)ADDR(tile_out_addr), 0);
        AddPrim(g_disp_ot + ot_index, (void *)ADDR(tile_out_addr));
        tile_out_addr = prims + BEST_TIMES_TILES + sizeof(TILE);
    }
    /* the total's bar, red */
    SetTile((TILE *)ADDR(tile_out_addr));
    TILE_R(tile_out_addr) = 0xFF;
    TILE_X(tile_out_addr) = BEST_TOTAL_BAR_X;
    TILE_G(tile_out_addr) = 0;
    TILE_B(tile_out_addr) = 0;
    TILE_Y(tile_out_addr) = BEST_TOTAL_BAR_Y;
    total_bar = g_hud_best_times_bar;
    /* FAKE: as row_work */
    total_work = total_bar + 1;
    g_hud_best_times_bar = total_work;
    total_work = ((s16)total_bar) * BEST_TOTAL_BAR_WIDTH;
    TILE_H(tile_out_addr) = BEST_BAR_HEIGHT;
    TILE_W(tile_out_addr) = total_work / BEST_BAR_FRAMES;
    if (g_hud_best_times_bar >= BEST_BAR_FRAMES + 1) {
        g_hud_best_times_bar = BEST_BAR_FRAMES;
    }
    SetSemiTrans((TILE *)ADDR(tile_out_addr), 0);
    AddPrim(g_disp_ot + ot_index, (void *)ADDR(tile_out_addr));
    tile_out_addr += sizeof(TILE);

    /* the box behind the table */
    SetTile((TILE *)ADDR(tile_out_addr));
    TILE_X(tile_out_addr) = BEST_TABLE_BOX_X;
    TILE_Y(tile_out_addr) = BEST_TABLE_BOX_Y;
    TILE_W(tile_out_addr) = BEST_TABLE_BOX_WIDTH;
    TILE_R(tile_out_addr) = 0;
    TILE_G(tile_out_addr) = 0;
    TILE_B(tile_out_addr) = 0;
    TILE_H(tile_out_addr) = BEST_TABLE_BOX_HEIGHT;
    SetSemiTrans((TILE *)ADDR(tile_out_addr), 1);
    AddPrim(g_disp_ot + ot_index, (void *)ADDR(tile_out_addr));
    tile_out_addr += sizeof(TILE);

    /* and the one behind the total */
    SetTile((TILE *)ADDR(tile_out_addr));
    TILE_X(tile_out_addr) = BEST_TOTAL_BOX_X;
    TILE_Y(tile_out_addr) = BEST_TOTAL_BOX_Y;
    TILE_W(tile_out_addr) = BEST_TOTAL_BOX_WIDTH;
    TILE_R(tile_out_addr) = 0;
    TILE_G(tile_out_addr) = 0;
    TILE_B(tile_out_addr) = 0;
    TILE_H(tile_out_addr) = BEST_TOTAL_BOX_HEIGHT;
    SetSemiTrans((TILE *)ADDR(tile_out_addr), 1);
    AddPrim(g_disp_ot + ot_index, (void *)ADDR(tile_out_addr));

    SetDrawMode((void *)ADDR(dr_mode_out_addr), 1, 0, 0, 0);
    /* FAKE: AddPrim(...), not called as if it were undeclared */
    ((s32(*)())AddPrim)(g_disp_ot + ot_index, dr_mode_out_addr);
    dr_mode_out_addr += sizeof(DR_MODE);
    return end_addr - prims;
}

/* ---- the sheets and cells ----
 * A sheet is {tp0, tp1, cells, unk3, cx, cy, ubase, vbase}: its texture
 * page's bits, how many cells it has, where its colour table is in video
 * memory, and the place in the page its cells' u and v are measured from.
 * A cell is {x, y, u, v, w, h}: where it goes on the screen, and the
 * rectangle of texture it shows.
 *
 * The characters' names, as the team battle's scores and the best times
 * show them: twenty names of three cells, in the characters' order without
 * the two that have none. The three cells before them are the rest of
 * NIGHTSTALKER's name, which is too long for three; the best times count
 * the names from there, so the two tables must stay together. The names
 * over the rows are what the cells show (in GAMEDAN.BNK and GAMECHAN.BNK,
 * the pictures of team battle and of chanbara). */
static SpriteSheet g_hud_name_sheet = {0x1F, 0x00, 3, 0x00, 960, 511, 0, 119};
static SpriteCell g_hud_name_8_tail_cells[3] = {
    {48, 80, 176, 0, 16, 12},
    {64, 80, 192, 0, 16, 12},
    {80, 80, 208, 0, 48, 12},
};
/* The characters' names: three cells each; NIGHTSTALKER's has three more. */
static SpriteCell g_hud_name_cells[20][3] = {
    /* KANNUKI */
    {{0, 80, 0, 0, 32, 12}, {32, 80, 32, 0, 32, 12}, {64, 80, 64, 0, 11, 12}},
    /* TATSUMI */
    {{0, 80, 0, 24, 32, 12},
     {32, 80, 32, 24, 32, 12},
     {64, 80, 64, 24, 11, 12}},
    /* MIKADO */
    {{0, 80, 0, 12, 32, 12}, {32, 80, 32, 12, 32, 12}, {64, 80, 64, 12, 2, 12}},
    /* UTSUSEMI */
    {{0, 80, 0, 36, 32, 12},
     {32, 80, 32, 36, 32, 12},
     {64, 80, 64, 36, 22, 12}},
    /* RED SHADOW */
    {{0, 80, 0, 48, 32, 12},
     {32, 80, 32, 48, 32, 12},
     {64, 80, 64, 48, 53, 12}},
    /* TSUBAME */
    {{0, 80, 78, 84, 32, 12},
     {32, 80, 110, 84, 32, 12},
     {64, 80, 142, 84, 18, 12}},
    /* SAZANKA */
    {{0, 80, 128, 48, 32, 12},
     {32, 80, 160, 48, 32, 12},
     {64, 80, 192, 48, 16, 12}},
    /* SUMINAGASHI */
    {{0, 80, 0, 60, 32, 12},
     {32, 80, 32, 60, 32, 12},
     {64, 80, 64, 60, 55, 12}},
    /* NIGHTSTALKER */
    {{0, 80, 128, 0, 16, 12},
     {16, 80, 144, 0, 16, 12},
     {32, 80, 160, 0, 16, 12}},
    /* MATSUMUSHI */
    {{0, 80, 0, 72, 32, 12},
     {32, 80, 32, 72, 32, 12},
     {64, 80, 64, 72, 51, 12}},
    /* GENGORO */
    {{0, 80, 78, 96, 32, 12},
     {32, 80, 110, 96, 32, 12},
     {64, 80, 142, 96, 24, 12}},
    /* KAUN */
    {{0, 80, 0, 84, 16, 12},
     {16, 80, 16, 84, 16, 12},
     {32, 80, 32, 84, 16, 12}},
    /* JO */
    {{0, 80, 128, 72, 7, 12},
     {7, 80, 135, 72, 7, 12},
     {14, 80, 142, 72, 9, 12}},
    /* HONGOU */
    {{0, 80, 0, 108, 32, 12},
     {32, 80, 32, 108, 32, 12},
     {64, 80, 64, 108, 14, 12}},
    /* KATZE */
    {{0, 80, 128, 36, 16, 12},
     {16, 80, 144, 36, 16, 12},
     {32, 80, 160, 36, 21, 12}},
    /* HIGHWAYMAN */
    {{0, 80, 128, 12, 32, 12},
     {32, 80, 160, 12, 32, 12},
     {64, 80, 192, 12, 58, 12}},
    /* CHIHIRO */
    {{0, 80, 0, 96, 32, 12}, {32, 80, 32, 96, 32, 12}, {64, 80, 64, 96, 8, 12}},
    /* UTAMARU */
    {{0, 80, 128, 60, 32, 12},
     {32, 80, 160, 60, 32, 12},
     {64, 80, 192, 60, 22, 12}},
    /* TONY UMEDA */
    {{0, 80, 128, 24, 32, 12},
     {32, 80, 160, 24, 32, 12},
     {64, 80, 192, 24, 54, 12}},
    /* ISOHACHI */
    {{0, 80, 78, 108, 32, 12},
     {32, 80, 110, 108, 32, 12},
     {64, 80, 142, 108, 18, 12}},
};
/* Where each of the twenty names starts in a row of the best times. */
static s16 g_hud_best_times_name_xs[20] = {
    432, 432, 436, 426, 411, 428, 429, 410, 405, 412, /* KANNUKI.. */
    425, 445, 459, 430, 443, 408, 433, 426, 411, 428, /* GENGORO.. */
};

/* The best times: the title, the three ranks, and the marks between the
 * digits of a time (three for each row and three for the total). The cells
 * follow their sheet, as in a resource file. */
/* clang-format off */
static SpriteSheet g_hud_best_times_title_sheet = {
    0x1E, 0x00, 6, 0xFF, 944, 510, 0, 0,
    {{145, 58, 0, 0, 64, 12},
     {209, 58, 64, 0, 64, 12},
     {273, 58, 128, 0, 64, 12},
     {337, 58, 192, 0, 64, 12},
     {401, 58, 0, 12, 64, 12},
     {465, 58, 64, 12, 30, 12},
     {0, 0, 0, 0, 0, 0}},
};
static SpriteSheet g_hud_best_times_rank_sheet = {
    0x1E, 0x00, 3, 0xFF, 912, 511, 0, 40,
    {{107, 80, 0, 0, 21, 12},
     {107, 106, 21, 0, 21, 12},
     {107, 132, 42, 0, 21, 12}},
};
static SpriteSheet g_hud_best_times_mark_sheet = {
    0x1E, 0x00, 12, 0xFF, 896, 511, 250, 26,
    {{230, 78, 0, 0, 3, 4},
     {286, 78, 0, 0, 3, 4},
     {291, 78, 0, 0, 3, 4},
     {230, 104, 0, 0, 3, 4},
     {286, 104, 0, 0, 3, 4},
     {291, 104, 0, 0, 3, 4},
     {230, 130, 0, 0, 3, 4},
     {286, 130, 0, 0, 3, 4},
     {291, 130, 0, 0, 3, 4},
     {209, 176, 0, 0, 3, 4},
     {265, 176, 0, 0, 3, 4},
     {270, 176, 0, 0, 3, 4}},
};
/* clang-format on */
static SpriteSheet g_hud_best_times_digit_sheet = {
    0x1E, 0x00, 1, 0xFF, 896, 511, 0, 26};
#if RB_PORT
/* In the port the table is filled before it is used
 * (hud_FillBestTimesSheets): its members are addresses in four bytes, as
 * the main menu's file has the same table, and a 64-bit program's compiler
 * cannot put an address into such a word of a table it fills
 * (docs/PORT.md, "64 bits"). */
static BestTimesSheets g_hud_best_times_sheets;

/* The table of the fight's own best times, as the PlayStation's compiler
 * fills it: the program's sheets, where the main menu's file has its own. */
static void hud_FillBestTimesSheets(void) {
    BestTimesSheets *sheets = &g_hud_best_times_sheets;

    SET_PTR(sheets->own_cell_sheets[0], &g_hud_best_times_title_sheet);
    SET_PTR(sheets->own_cell_sheets[1], &g_hud_best_times_rank_sheet);
    SET_PTR(sheets->own_cell_sheets[2], &g_hud_best_times_mark_sheet);
    SET_PTR(sheets->character_sheet, &g_hud_name_sheet);
    SET_PTR(sheets->character_cells, g_hud_name_8_tail_cells);
    SET_PTR(sheets->digit_sheet, &g_hud_best_times_digit_sheet);
    SET_PTR(sheets->digit_cell, &g_hud_best_times_digit_cell);
    SET_PTR(sheets->character_x_offsets, g_hud_best_times_name_xs);
}
#else
static BestTimesSheets g_hud_best_times_sheets = {
    {&g_hud_best_times_title_sheet, &g_hud_best_times_rank_sheet,
     &g_hud_best_times_mark_sheet},
    &g_hud_name_sheet,
    g_hud_name_8_tail_cells,
    &g_hud_best_times_digit_sheet,
    &g_hud_best_times_digit_cell,
    g_hud_best_times_name_xs,
};
#endif
static SpriteSheet g_hud_panel_sheets[9] = {
    {0x1F, 0x00, 2, 0x00, 976, 509, 160, 28},
    {0x1F, 0x00, 2, 0x00, 976, 509, 160, 34},
    {0x1F, 0x00, 2, 0x00, 976, 509, 160, 42},
    {0x1F, 0x00, 3, 0x00, 976, 508, 0, 80},
    {0x1F, 0x00, 2, 0x00, 992, 508, 56, 106},
    {0x1F, 0x00, 2, 0x00, 992, 508, 149, 80},
    {0x1F, 0x00, 4, 0x00, 992, 508, 0, 0},
    {0x1F, 0x00, 4, 0x00, 992, 508, 0, 0},
    {0x1F, 0x00, 6, 0x00, 992, 508, 0, 0},
};
static SpriteSheet g_hud_panel_option_sheet = {
    0x1F, 0x00, 4, 0x00, 992, 508, 0, 0};
static SpriteSheet g_hud_panel_on_off_sheet = {
    0x1F, 0x00, 1, 0x00, 960, 511, 0, 0};

/* The panel. */
static SpriteCell g_hud_panel_corner_cells[2][2] = {
    {{0, 0, 0, 0, 16, 6}, {0, 0, 16, 0, 16, 6}},
    {{0, -6, 0, 0, 16, 8}, {0, -6, 16, 0, 16, 8}},
};
static SpriteCell g_hud_panel_rule_end_cells[2] = {
    {0, -4, 0, 0, 16, 10},
    {0, -4, 16, 0, 16, 10},
};
static SpriteCell g_hud_panel_title_cells[3] = {
    {247, 3, 0, 0, 64, 12},
    {311, 3, 64, 0, 64, 12},
    {375, 3, 128, 0, 20, 12},
};
static SpriteCell g_hud_panel_continue_cells[2] = {
    {265, 18, 0, 0, 64, 12},
    {329, 18, 64, 0, 45, 12},
};
static SpriteCell g_hud_panel_rematch_cells[2] = {
    {266, 4, 0, 0, 64, 12},
    {330, 4, 64, 0, 43, 12},
};
static SpriteCell g_hud_panel_to_select_cells[4] = {
    {200, 19, 0, 93, 64, 12},
    {264, 19, 64, 93, 53, 12},
    {317, 19, 0, 106, 57, 12},
    {374, 19, 169, 93, 65, 12},
};
static SpriteCell g_hud_panel_to_menu_cells[4] = {
    {203, 34, 0, 93, 64, 12},
    {267, 34, 64, 93, 64, 12},
    {331, 34, 128, 93, 64, 12},
    {395, 34, 192, 93, 42, 12},
};
static SpriteCell g_hud_panel_footer_cells[6] = {
    {159, 165, 0, 156, 192, 13},  {351, 165, 192, 156, 64, 13},
    {415, 165, 0, 169, 66, 13},   {182, 178, 0, 182, 192, 13},
    {374, 178, 192, 182, 64, 13}, {438, 178, 0, 195, 19, 13},
};
static SpriteCell g_hud_panel_option_cells[12] = {
    {180, 64, 0, 143, 64, 12},   {244, 64, 64, 143, 24, 12},
    {284, 64, 78, 169, 50, 12},  {334, 64, 128, 169, 26, 12},
    {180, 79, 91, 143, 64, 12},  {244, 79, 155, 143, 40, 12},
    {284, 79, 78, 169, 50, 12},  {334, 79, 128, 169, 26, 12},
    {180, 94, 156, 169, 12, 13}, {192, 94, 168, 169, 12, 13},
    {204, 94, 180, 169, 12, 13}, {216, 94, 192, 169, 29, 13},
};
static SpriteCell g_hud_panel_on_off_cells[2] = {
    {430, 63, 220, 42, 30, 12},
    {380, 63, 192, 42, 28, 12},
};
/* The cells of the panel's rows, by sheet from PANEL_SHEET_CONTINUE on;
 * hud_DrawPanel reads only the last two. */
static SpriteCell *g_hud_panel_item_cells[4] = {
    g_hud_panel_continue_cells,
    g_hud_panel_continue_cells,
    g_hud_panel_to_select_cells,
    g_hud_panel_to_menu_cells,
};
/* The size of each panel, by enum HudPanel. */
static ScreenSize g_hud_panel_sizes[3] = {
    {258, 65},
    {258, 51},
    {328, 155},
};

/* The loading picture, the counters. Two sets of five sheets, one for each
 * picture: the title, the picture, and three for the noise. */
static SpriteSheet g_hud_loading_sheets[2][5] = {
    {
        {0x1F, 0x00, 3, 0x00, 960, 510, 0, 100},
        {0x1F, 0x00, 6, 0x00, 976, 510, 0, 146},
        {0x1F, 0x00, 1, 0x00, 992, 510, 144, 146},
        {0x1F, 0x00, 1, 0x00, 992, 510, 144, 166},
        {0x1F, 0x00, 1, 0x00, 992, 510, 158, 166},
    },
    {
        {0x1E, 0x00, 3, 0x00, 896, 510, 0, 100},
        {0x1E, 0x00, 6, 0x00, 912, 510, 0, 146},
        {0x1E, 0x00, 1, 0x00, 928, 510, 144, 146},
        {0x1E, 0x00, 1, 0x00, 928, 510, 144, 166},
        {0x1E, 0x00, 1, 0x00, 928, 510, 158, 166},
    },
};
static SpriteCell g_hud_loading_title_cells[3] = {
    {329, 170, 0, 0, 64, 46},
    {393, 170, 64, 0, 64, 46},
    {457, 170, 128, 0, 52, 46},
};
static SpriteCell g_hud_loading_picture_cells[6] = {
    {476, 134, 0, 0, 16, 64},   {492, 134, 16, 0, 64, 64},
    {556, 134, 80, 0, 64, 64},  {476, 198, 0, 64, 16, 24},
    {492, 198, 16, 64, 64, 24}, {556, 198, 80, 64, 64, 24},
};
static SpriteCell g_hud_noise_cells[2] = {
    {0, 0, 0, 0, 36, 20},
    {0, 0, 0, 0, 14, 6},
};
static SpriteSheet g_hud_counter_sheets[4] = {
    {0x1F, 0x00, 1, 0x00, 960, 509, 0, 0},
    {0x1F, 0x00, 1, 0x00, 992, 509, 0, 0},
    {0x1F, 0x00, 1, 0x00, 960, 510, 0, 0},
    {0x1F, 0x00, 1, 0x00, 976, 510, 0, 0},
};
static SpriteCell g_hud_time_cells[7] = {
    {459, 22, 252, 0, 4, 5},    {513, 22, 252, 0, 4, 5},
    {508, 22, 252, 0, 4, 5},    {565, 23, 192, 28, 60, 14},
    {565, 42, 192, 42, 60, 14}, {568, 0, 192, 219, 35, 1},
    {603, 0, 227, 219, 29, 1},
};
static SpriteCell g_hud_digit_cells[10] = {
    {0, 0, 160, 0, 19, 14},  {0, 0, 179, 0, 15, 14},  {0, 0, 194, 0, 19, 14},
    {0, 0, 213, 0, 19, 14},  {0, 0, 232, 0, 19, 14},  {0, 0, 160, 14, 19, 14},
    {0, 0, 179, 14, 19, 14}, {0, 0, 198, 14, 19, 14}, {0, 0, 217, 14, 19, 14},
    {0, 0, 236, 14, 19, 14},
};

/* Small data. */
static SpriteCell g_hud_best_times_digit_cell = {179, 78, 0, 0, 24, 12};
/* The bursts of noise so far, of NOISE_CYCLE. */
static s32 g_hud_noise_cycle = 0;
/* Which of g_hud_team_unfought_cells each match's row shows. */
static u8 g_hud_team_unfought_cell_ids[8] = {0, 1, 2, 3, 4, 0, 0, 0};
static s32 g_hud_ending_frame = 0;
static RECT g_hud_ending_image_rect = {896, 0, 64, 256};
static RECT g_hud_ending_clut_rect = {992, 511, 16, 1};
static SpriteCell g_hud_card_mark_cell = {0, 0, 0, 0, 20, 14};
static RECT g_hud_card_name_image_rect = {960, 384, 64, 22};
static RECT g_hud_card_name_clut_rect = {960, 511, 16, 1};
static RECT g_hud_card_picture_image_rect = {896, 383, 64, 127};
static RECT g_hud_card_picture_clut_rect = {896, 511, 32, 1};
static s16 g_hud_best_times_row_bar = 0;
static s16 g_hud_best_times_bar = 0;
