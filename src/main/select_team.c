/* Team battle's character select, drawn from D_SEL.BIN.
 *
 * A team battle is three to five matches between two teams, the first
 * fighters of each against each other, then the second, and so on. The
 * screen opens on the rules, which either pad sets: how many matches, how
 * many points win a match, the time limit. Then each side has a panel of
 * its own, half the screen, and goes through the steps (enum TeamStep) on
 * it: the clan (one side picks, the other gets the other clan); a fighter
 * for each match from the clan's ten; a weapon for each of them, every
 * weapon once. When both sides are ready the screen fades and the picks go
 * to the settings.
 *
 * A panel changes pages by sliding (enum TeamPanelMove): everything on it
 * is drawn through a draw area that closes from below and a draw offset
 * that pushes the page up. */
#include "common.h"
#include "bb2.h"

/* The resource file. */
#define TEAM_FILE CDROM_DATA_D_SEL_BIN

/* The second side's panel is this far right of the first's. */
#define TEAM_SIDE_X 240

/* ---- where things are on the screen (640 x 240) ----
 * A sheet's cells have their places in them, for the first side's panel;
 * these are the places the code works out. */

/* The rule under the rules page's title (the main menu's, at its height). */
#define TEAM_RULE_X 188
#define TEAM_RULE_WIDTH 264

/* A panel: its paper, the window its page is seen through, and how fast a
 * page slides. The rail at its top is stretched this far across; a second
 * rail closes over the page from below: it is the top one as far down as
 * the panel is high and TEAM_PANEL_RAIL_GAP more, less what is covered. */
#define TEAM_PANEL_X 98
#define TEAM_PANEL_Y 20
#define TEAM_PANEL_WIDTH 204
#define TEAM_PANEL_HEIGHT 200
#define TEAM_PANEL_WINDOW_X 94
#define TEAM_PANEL_WINDOW_WIDTH 212
#define TEAM_PANEL_SPEED 10
#define TEAM_PANEL_RAIL_WIDTH 206
#define TEAM_PANEL_RAIL_GAP 10

/* The height of a team's place on the fighter page and on the weapon page,
 * the bars behind a place (of TeamSelectFile.bar_rects, from the first)
 * and where the first place's are. */
#define TEAM_PLACE_HEIGHT 17
#define TEAM_PLACE_TALL_HEIGHT 34
#define TEAM_PLACE_BARS 5
#define TEAM_PLACE_TALL_BARS 8
#define TEAM_PLACES_Y 124
#define TEAM_TALL_PLACES_Y 43
/* The clan page: the clans' two rows, and the first clan's emblem. */
#define TEAM_CLAN_ROW_HEIGHT 90
#define TEAM_CLAN_EMBLEM_X 157
#define TEAM_CLAN_EMBLEM_Y 54

/* The fighter page: the grid of two columns of five, and the clan's
 * emblem. */
#define TEAM_GRID_ROWS 5
#define TEAM_GRID_COL_WIDTH 100
#define TEAM_GRID_ROW_HEIGHT 16
#define TEAM_EMBLEM_X 136
#define TEAM_EMBLEM_Y 51

/* What a place of a team holds when the cursor is on a fighter that cannot
 * be taken. The name sheet it stands for is the line that strikes names
 * out, which has one header and is read here as if it had a name's three. */
#define TEAM_NO_FIGHTER TEAM_FIGHTERS

/* The weapon the second clan has in place of the first clan's last. */
#define TEAM_WEAPON_SECOND_CLAN 5

/* The refusal: a sound of the menu's bank. */
#define TEAM_SOUND_REFUSED 4

/* The ordering table. Each side's panel has ten entries, drawn from the
 * last down: the page is clipped and pushed by what is in the last, and the
 * first puts the draw area and offset back. */
enum {
    TEAM_LAYER_FADE = 0,
    TEAM_LAYER_RULES = 2, /* and the panels' rails, which nothing clips */
    TEAM_LAYER_SIDE_1 = 4,
    TEAM_LAYER_SIDE_2 = 14,
    TEAM_LAYER_RULES_PANEL = 30,
    TEAM_LAYER_FRAME = 31,
    TEAM_LAYER_BACKDROP = 32,
};

/* A panel's entries, counted from its first. */
enum {
    TEAM_PANEL_LAYER_STRIKE = 5,
    TEAM_PANEL_LAYER_TEXT = 6,
    TEAM_PANEL_LAYER_BARS = 7,
    TEAM_PANEL_LAYER_PAGE = 8,
    TEAM_PANEL_LAYER_PAPER = 9, /* and the clipping */
};

/* A sheet that a side's cursor can be on has three headers before its
 * cells: plain, and in each side's colour. */
#define TEAM_LOOKS 3

/* TeamSelectFile.name_sheets. A name has the three headers, and its cells
 * are the name three times: on the grid, in a place of the team, and in a
 * tall place. */
enum TeamNameSheet {
    TEAM_NAME_SHEET_EMBLEMS = 0, /* the first clan's, then the second's */
    TEAM_NAME_SHEET_FIRST = 1,
    TEAM_NAME_SHEET_STRIKE = 21,
};

/* TeamSelectFile.panel_sheets (the split between them is a reading). */
enum TeamPanelSheet {
    TEAM_PANEL_SHEET_FRAME_1 = 0,
    TEAM_PANEL_SHEET_FRAME_2 = 1,
    TEAM_PANEL_SHEET_RAIL = 2, /* then cells that are stretched along it */
    TEAM_PANEL_SHEET_RAIL_ENDS = 3,
};

/* TeamSelectFile.rule_sheets. */
enum TeamRuleSheet {
    TEAM_RULE_SHEET_ROWS = 0,    /* the five rows' names */
    TEAM_RULE_SHEET_MATCHES = 5, /* three, four, five */
    TEAM_RULE_SHEET_POINTS = 8,  /* one, two */
    TEAM_RULE_SHEET_TIMES = 10,  /* two for each number of points */
    TEAM_RULE_SHEET_TITLE = 14,
    TEAM_RULE_SHEETS = 15,
};

/* TeamSelectFile.clan_sheets: two that make the page, then each side's
 * cursor. TeamSelectFile.weapon_sheets: two a weapon, then the cursor. */
#define TEAM_CLAN_SHEET_CURSORS 2
#define TEAM_WEAPON_SHEET_CURSOR 12

/* The sine the cursors' glow swings with: once round in TEAM_PULSE_FRAMES
 * frames, from an eighth of a turn round, less one. */
#define TEAM_PULSE_FRAMES 32
#define TEAM_PULSE_SIN                                                         \
    rsin(((g_select_team->pulse_frames & (TEAM_PULSE_FRAMES - 1)) << 7) +      \
         (ANGLE_TURN / 8 - 1))
/* The brightness of the thing under a cursor: 32 either side of `center`
 * (SPRITE_GLOW). The line that strikes a name out swings 48 either side
 * of its own. */
#define TEAM_GLOW(center) SPRITE_GLOW(TEAM_PULSE_SIN, 32, center)
#define TEAM_STRIKE_GLOW SPRITE_GLOW(TEAM_PULSE_SIN, 48, 0xC0)

/* A sheet's tint: this much leaves it as it is, less darkens it. */
#define TEAM_TINT_PLAIN 0x80
#define TEAM_TINT_DIM 0x40 /* a rule's value that is not the one set */
/* The centre of the glow of a name or a weapon in a team's place. */
#define TEAM_TINT_PLACE 0xB0

/* The screen fades out this much a frame, up to TEAM_FADE_FULL. */
#define TEAM_FADE_STEP 8
#define TEAM_FADE_FULL 0xFF

/* This file's variables, in the order the original has them. */
/* how fast each cursor's nudge moves */
static s16 g_select_team_nudge_steps[2][2];
static PrimPool *g_select_team_pools;
static s8 g_select_team_time_choice; /* which of the two time limits */

/* What is behind both panels: the menu's backdrop of three slanted bands
 * and the frame over it, and on the rules page the see-through panel the
 * rules are written on. */
static void select_DrawTeamFrame(MenuDrawCtx *ctx, s32 step) {
    SpriteDraw draw;
    s32 i;
    SpriteSheetSlot *sheets;
    TILE *tile;
    POLY_F4 *band;

    if (step == TEAM_STEP_RULES) {
        tile = ctx->prims.tile;
        SetTile(tile);
        menu_SetPanelColor(tile);
        setXY0(tile, MENU_PANEL_WIDE_X, MENU_PANEL_Y);
        setWH(tile, MENU_PANEL_WIDE_WIDTH, MENU_PANEL_HEIGHT);
        SetSemiTrans(tile, 1);
        AddPrim(g_disp_ot + TEAM_LAYER_RULES_PANEL, tile);
        tile++;
        ctx->prims.tile = tile;
    }
    draw.ot_index = TEAM_LAYER_FRAME;
    draw.has_color = 0;
    draw.semi = 0;
    sheets = PTR(ctx->file.team_select->frame_sheets);
    draw.x = 0;
    draw.y = 0;
    for (i = 0; i < 3; i++) {
        draw.header = PTR(sheets[i]);
        draw.table = draw.header->cells;
        draw.sprt_out = ctx->prims.sprt;
        ctx->prims.sprt = sprite_Draw(&draw);
    }

    draw.header = PTR(*sheets);
    SetDrawMode(ctx->prims.mode, 1, 0, sprite_GetTPage(draw.header, 0), 0);
    AddPrim(g_disp_ot + TEAM_LAYER_FRAME, ctx->prims.mode);
    band = ctx->prims.f4;
    ctx->prims.mode++;
    SetPolyF4(band);
    menu_SetBackdropColor(band);
    /* left of the column */
    setXY4(band, 0, MENU_BACKDROP_SLANT_LEFT_Y, MENU_BACKDROP_COLUMN_LEFT, 0, 0,
           DISP_HEIGHT - 1, MENU_BACKDROP_COLUMN_LEFT, DISP_HEIGHT - 1);
    SetSemiTrans(band, 0);
    AddPrim(g_disp_ot + TEAM_LAYER_BACKDROP, band);
    band++;

    SetPolyF4(band);
    menu_SetBackdropColor(band);
    /* right of it */
    setXY4(band, MENU_BACKDROP_COLUMN_RIGHT, 0, DISP_WIDTH - 1, 0,
           MENU_BACKDROP_COLUMN_RIGHT, DISP_HEIGHT - 1, DISP_WIDTH - 1,
           MENU_BACKDROP_SLANT_RIGHT_Y);
    SetSemiTrans(band, 0);
    AddPrim(g_disp_ot + TEAM_LAYER_BACKDROP, band);
    band++;

    SetPolyF4(band);
    menu_SetBackdropColor(band);
    /* the column */
    setXY4(band, MENU_BACKDROP_COLUMN_LEFT, 0, MENU_BACKDROP_COLUMN_RIGHT, 0,
           MENU_BACKDROP_COLUMN_LEFT, DISP_HEIGHT - 1,
           MENU_BACKDROP_COLUMN_RIGHT, DISP_HEIGHT - 1);
    SetSemiTrans(band, 0);
    AddPrim(g_disp_ot + TEAM_LAYER_BACKDROP, band);
    band++;

    ctx->prims.f4 = band;
}

/* ---- the rules ---- */

/* Draws the rules page: the five rows' names, the values each rule can
 * have, and the title with its rule. The row under the cursor and each
 * rule's value now are lit; the rest are dim. The row under the cursor
 * glows and is nudged, and so is its value. Of the four time limits only
 * the two that go with the number of points are drawn. */
static void select_DrawTeamRules(MenuDrawCtx *ctx) {
    SpriteDraw draw;
    s16 lit;
    s16 i;
    SpriteSheetSlot *sheets;
    s16 rule[4];

    lit = (1 << g_select_team->cursor[0]) +
          (1 << (g_select_team->match_count_index + TEAM_RULE_SHEET_MATCHES)) +
          (1 << (g_select_team->win_target_index + TEAM_RULE_SHEET_POINTS)) +
          (1 << (g_select_team->time_sheet + (TEAM_RULE_SHEET_TIMES - 1)));
    draw.ot_index = TEAM_LAYER_RULES;
    sheets = PTR(ctx->file.team_select->rule_sheets);
    for (i = 0; i < TEAM_RULE_SHEETS; i++) {
        draw.x = 0;
        draw.y = 0;
        draw.semi = 0;
        if ((lit >> i) & 1) {
            if (i < TEAM_RULE_SHEET_MATCHES) {
                draw.col_b = TEAM_GLOW(TEAM_TINT_PLAIN);
                draw.y = g_select_team->nudge[0][1];
            } else if (i < TEAM_RULE_SHEET_POINTS) {
                if (g_select_team->cursor[0] == TEAM_RULE_MATCHES) {
                    draw.col_b = TEAM_GLOW(TEAM_TINT_PLAIN);
                    draw.x = g_select_team->nudge[0][0];
                    draw.y = g_select_team->nudge[0][1];
                } else {
                    draw.col_b = TEAM_TINT_PLAIN;
                }
            } else if (i < TEAM_RULE_SHEET_TIMES) {
                if (g_select_team->cursor[0] == TEAM_RULE_POINTS) {
                    draw.col_b = TEAM_GLOW(TEAM_TINT_PLAIN);
                    draw.x = g_select_team->nudge[0][0];
                    draw.y = g_select_team->nudge[0][1];
                } else {
                    draw.col_b = TEAM_TINT_PLAIN;
                }
            } else if (i < TEAM_RULE_SHEET_TITLE) {
                if (g_select_team->cursor[0] == TEAM_RULE_TIME) {
                    draw.col_b = TEAM_GLOW(TEAM_TINT_PLAIN);
                    draw.x = g_select_team->nudge[0][0];
                    draw.y = g_select_team->nudge[0][1];
                } else {
                    draw.col_b = TEAM_TINT_PLAIN;
                }
            }
            draw.has_color = 1;
            draw.col_g = draw.col_b;
            draw.col_r = draw.col_b;
        } else {
            draw.col_b = TEAM_TINT_DIM;
            draw.col_g = TEAM_TINT_DIM;
            draw.col_r = TEAM_TINT_DIM;
            if (i < TEAM_RULE_SHEET_MATCHES) {
                draw.has_color = 0;
                draw.semi = 1;
            } else if (i < TEAM_RULE_SHEET_TITLE) {
                draw.has_color = 1;
            } else {
                draw.has_color = 0;
            }
        }
        if ((u16)(i - TEAM_RULE_SHEET_TIMES) >= 4 ||
            g_select_team_time_sheets[g_select_team->win_target_index][0] +
                    (TEAM_RULE_SHEET_TIMES - 1) ==
                i ||
            g_select_team_time_sheets[g_select_team->win_target_index][1] +
                    (TEAM_RULE_SHEET_TIMES - 1) ==
                i) {
            draw.header = PTR(sheets[i]);
            draw.table = draw.header->cells;
            draw.sprt_out = ctx->prims.sprt;
            ctx->prims.sprt = sprite_Draw(&draw);
        }
    }

    draw.header = PTR(sheets[0]);
    SetDrawMode(ctx->prims.mode, 1, 0, sprite_GetTPage(draw.header, 0), 0);
    AddPrim(g_disp_ot + TEAM_LAYER_RULES, ctx->prims.mode);
    ctx->prims.mode++;
    rule[2] = TEAM_RULE_WIDTH;
    rule[0] = TEAM_RULE_X;
    rule[1] = MENU_TITLE_RULE_Y;
    rule[3] = 1;
    /* The rule is drawn by the main menu's function, which wants its own
     * context and is handed this one as it is: a word out, so the tile is
     * built where this screen keeps its draw modes (the original's
     * behaviour). */
    menu_DrawTitleRule((MenuCtx *)ctx, rule, TEAM_LAYER_RULES);
}

/* The pad on the rules page; either pad will do. Up and down move between
 * the rows, left and right change a rule. The cross button on a rule goes
 * to "OK"; on "OK" it sends both sides on to the clans. Returns -1 when the
 * screen is left, by its last row or the triangle button. */
static s32 select_HandleTeamRules(u32 pressed) {
    TeamSelect *team;
    s32 either_pad;
    s32 events;
    s32 row;
    s32 result;
    s32 i;
    u8 points;
    s32 sound;

    team = g_select_team;
    result = PAGE_RUNNING;
    if (team->panel_move.word == 0) {
        either_pad = (pressed & 0xFFFF) | (pressed >> PAD_BITS);
        events = menu_StepNudge(
            &either_pad, 0, team->nudge[0], g_select_team_nudge_steps[0]);
        switch (NUDGE_DOWN(events)) {
        case NUDGE_PLUS:
            g_select_team->pulse_frames = 0;
            g_select_team->cursor[0] += 1;
            if (g_select_team->cursor[0] > TEAM_RULE_EXIT) {
                g_select_team->cursor[0] = TEAM_RULE_MATCHES;
            }
            snd_Play(MENU_SOUND_CURSOR, SND_VOLUME_MAX, SND_VOLUME_MAX);
            break;
        case NUDGE_MINUS:
            g_select_team->pulse_frames = 0;
            g_select_team->cursor[0] -= 1;
            if (g_select_team->cursor[0] < 0) {
                g_select_team->cursor[0] = TEAM_RULE_EXIT;
            }
            snd_Play(MENU_SOUND_CURSOR, SND_VOLUME_MAX, SND_VOLUME_MAX);
            break;
        }

        row = g_select_team->cursor[0];
        switch (row) {
        case TEAM_RULE_MATCHES:
            /* As many matches as the smaller roster has fighters for; with
             * no choice the buttons are refused. */
            switch (NUDGE_ACROSS(events)) {
            case NUDGE_PLUS:
                if (g_select_team->match_count_index ==
                    g_select_team->max_match_count_index) {
                    g_select_team->match_count_index = 0;
                } else {
                    g_select_team->match_count_index += 1;
                }
                /* FAKE: set in both cases; read it as set once at rule_sound */
                sound = TEAM_SOUND_REFUSED;
                goto rule_sound;
            case NUDGE_MINUS:
                if (g_select_team->match_count_index == 0) {
                    g_select_team->match_count_index =
                        g_select_team->max_match_count_index;
                } else {
                    g_select_team->match_count_index -= 1;
                }
                sound = TEAM_SOUND_REFUSED;
            rule_sound:
                if (g_select_team->max_match_count_index != 0) {
                    sound = MENU_SOUND_CURSOR;
                }
                snd_Play(sound, SND_VOLUME_MAX, SND_VOLUME_MAX);
                break;
            }
            if (pressed & PAD_ANY(PADRdown)) {
                snd_Play(MENU_SOUND_CONFIRM, SND_VOLUME_MAX, SND_VOLUME_MAX);
                g_select_team->cursor[0] = TEAM_RULE_OK;
            }
            break;
        case TEAM_RULE_POINTS:
            if (NUDGE_ACROSS(events) != 0) {
                g_select_team->win_target_index += 1;
                g_select_team->win_target_index &= 1;
                g_select_team->time_sheet =
                    g_select_team_time_sheets[g_select_team->win_target_index]
                                             [g_select_team_time_choice & 1];
                snd_Play(MENU_SOUND_CURSOR, SND_VOLUME_MAX, SND_VOLUME_MAX);
            }
            if (pressed & PAD_ANY(PADRdown)) {
                snd_Play(MENU_SOUND_CONFIRM, SND_VOLUME_MAX, SND_VOLUME_MAX);
                g_select_team->cursor[0] = TEAM_RULE_OK;
            }
            break;
        case TEAM_RULE_TIME:
            if (NUDGE_ACROSS(events) != 0) {
                points = g_select_team->win_target_index;
                g_select_team_time_choice += 1;
                g_select_team->time_sheet =
                    g_select_team_time_sheets[points]
                                             [g_select_team_time_choice & 1];
                snd_Play(MENU_SOUND_CURSOR, SND_VOLUME_MAX, SND_VOLUME_MAX);
            }
            if (pressed & PAD_ANY(PADRdown)) {
                snd_Play(MENU_SOUND_CONFIRM, SND_VOLUME_MAX, SND_VOLUME_MAX);
                g_select_team->cursor[0] = TEAM_RULE_OK;
            }
            break;
        case TEAM_RULE_OK:
            if (pressed & PAD_ANY(PADRdown)) {
                snd_Play(MENU_SOUND_CONFIRM, SND_VOLUME_MAX, SND_VOLUME_MAX);
                for (i = 0; i < 2; i++) {
                    g_select_team->panel_move.half[i] = TEAM_PANEL_COVER;
                    g_select_team->next_step[i] = TEAM_STEP_CLAN;
                    g_select_team->next_cursor[i] = 0;
                }
            }
            break;
        case TEAM_RULE_EXIT:
            if (pressed & PAD_ANY(PADRdown)) {
                snd_Play(MENU_SOUND_CONFIRM, SND_VOLUME_MAX, SND_VOLUME_MAX);
                result = PAGE_CANCELED;
            }
            break;
        }
        if (pressed & PAD_ANY(PADRup)) {
            snd_Play(MENU_SOUND_CANCEL, SND_VOLUME_MAX, SND_VOLUME_MAX);
            result = PAGE_CANCELED;
        }
    }
    return result;
}

/* ---- a side's panel ---- */

/* The coloured bars behind each place of a side's team, on the fighter
 * page or, `tall`, on the weapon page. */
static void select_DrawTeamBars(MenuDrawCtx *ctx, s32 side, s32 tall) {
    TILE *tile;
    ColorRect *bar;
    s16 i;
    s16 j;
    s16 bars;
    s32 ot_index;

    bars = TEAM_PLACE_BARS;
    if (tall) {
        bars = TEAM_PLACE_TALL_BARS;
    }
    tile = ctx->prims.tile;
    for (i = 0; i < g_select_team->match_count_index + TEAM_MIN_MATCHES; i++) {
        bar = PTR(PTR(g_select_team->file)->bar_rects);
        for (j = 0; j < bars; j++) {
            SetTile(tile);
            tile->r0 = bar->r;
            tile->g0 = bar->g;
            tile->b0 = bar->b;
            tile->w = bar->w;
            tile->h = bar->h;
            SetSemiTrans(tile, 0);
            if (tall) {
                tile->x0 = bar->x + side * TEAM_SIDE_X;
                tile->y0 =
                    bar->y + i * TEAM_PLACE_TALL_HEIGHT + TEAM_TALL_PLACES_Y;
            } else {
                tile->x0 = bar->x + side * TEAM_SIDE_X;
                tile->y0 = bar->y + i * TEAM_PLACE_HEIGHT + TEAM_PLACES_Y;
            }
            ot_index = TEAM_LAYER_SIDE_1 + TEAM_PANEL_LAYER_BARS;
            if (side != 0) {
                ot_index = TEAM_LAYER_SIDE_2 + TEAM_PANEL_LAYER_BARS;
            }
            AddPrim(g_disp_ot + ot_index, tile);
            tile++;
            bar++;
        }
    }
    ctx->prims.tile = tile;
}

/* The heading of a side's page. */
static void select_DrawTeamHeading(MenuDrawCtx *ctx, s32 side, s16 heading) {
    SpriteDraw draw;
    s32 ot_index;

    ot_index = TEAM_LAYER_SIDE_1 + TEAM_PANEL_LAYER_PAGE;
    draw.semi = 0;
    draw.has_color = 0;
    draw.header = PTR(PTR(ctx->file.team_select->heading_sheets)[heading]);
    draw.x = side * TEAM_SIDE_X;
    draw.y = 0;
    draw.table = draw.header->cells;
    if (side != 0) {
        ot_index = TEAM_LAYER_SIDE_2 + TEAM_PANEL_LAYER_PAGE;
    }
    draw.ot_index = ot_index;
    draw.sprt_out = ctx->prims.sprt;
    ctx->prims.sprt = sprite_Draw(&draw);
    SetDrawMode(ctx->prims.mode, 1, 0, sprite_GetTPage(draw.header, 0), 0);
    AddPrim(g_disp_ot + ot_index, ctx->prims.mode);
    ctx->prims.mode++;
}

/* A side's panel: its paper, the rails at its top and at the edge that
 * closes over it, its frame, and the draw area and draw offset that clip
 * and push everything drawn on it. */
static void select_DrawTeamPanel(MenuDrawCtx *ctx, s32 side) {
    SpriteDraw draw;
    RECT rect;
    u16 offset[2];
    SpriteSheetSlot *sheets;
    TILE *paper;
    s32 ot_base;
    s32 clip_x;
    SpriteCell *cells;
    s16 i;

    paper = ctx->prims.tile;
    SetTile(paper);
    SetSemiTrans(paper, 0);
    setRGB0(paper, SELECT_PAPER_R, SELECT_PAPER_G, SELECT_PAPER_B);
    setXY0(paper, side * TEAM_SIDE_X + TEAM_PANEL_X, TEAM_PANEL_Y);
    setWH(paper, TEAM_PANEL_WIDTH, TEAM_PANEL_HEIGHT);
    if (side != 0) {
        ot_base = TEAM_LAYER_SIDE_2;
    } else {
        ot_base = TEAM_LAYER_SIDE_1;
    }
    AddPrim(g_disp_ot + ot_base + TEAM_PANEL_LAYER_PAPER, paper);
    paper++;
    ctx->prims.tile = paper;

    /* The rail at the top, and the one that comes up with the cover. */
    sheets = PTR(ctx->file.team_select->panel_sheets);
    draw.semi = 0;
    draw.has_color = 0;
    draw.x = side * TEAM_SIDE_X;
    draw.ot_index = TEAM_LAYER_RULES;
    for (i = 0; i < 2; i++) {
        draw.y = (TEAM_PANEL_HEIGHT + TEAM_PANEL_RAIL_GAP -
                  g_select_team->panel_cover[side]) *
                 i;
        draw.header = PTR(sheets[TEAM_PANEL_SHEET_RAIL_ENDS]);
        cells = draw.header->cells;
        draw.table = cells;
        draw.sprt_out = ctx->prims.sprt;
        ctx->prims.sprt = sprite_Draw(&draw);
        draw.header = PTR(sheets[TEAM_PANEL_SHEET_RAIL]);
        cells = draw.header->cells;
        draw.table = cells;
        draw.sprt_out = ctx->prims.sprt;
        ctx->prims.sprt = sprite_Draw(&draw);
        draw.table += draw.header->count;
        draw.scale_x = TEAM_PANEL_RAIL_WIDTH * SPRITE_SCALE_ONE;
        draw.scale_y = SPRITE_SCALE_ONE;
        draw.ft4_out = ctx->prims.ft4;
        ctx->prims.ft4 = sprite_DrawScaled(&draw, SPRITE_QUAD_NORMAL);
    }

    draw.x = side * TEAM_SIDE_X;
    draw.y = 0;
    draw.header = PTR(sheets[TEAM_PANEL_SHEET_FRAME_1]);
    cells = draw.header->cells;
    draw.table = cells;
    if (side != 0) {
        draw.ot_index = TEAM_LAYER_SIDE_2 + TEAM_PANEL_LAYER_PAGE;
    } else {
        draw.ot_index = TEAM_LAYER_SIDE_1 + TEAM_PANEL_LAYER_PAGE;
    }
    draw.sprt_out = ctx->prims.sprt;
    ctx->prims.sprt = sprite_Draw(&draw);
    draw.header = PTR(sheets[TEAM_PANEL_SHEET_FRAME_2]);
    cells = draw.header->cells;
    draw.table = cells;
    draw.sprt_out = ctx->prims.sprt;
    ctx->prims.sprt = sprite_Draw(&draw);
    SetDrawMode(ctx->prims.mode, 1, 0, sprite_GetTPage(draw.header, 0), 0);
    AddPrim(g_disp_ot + draw.ot_index, ctx->prims.mode);
    ctx->prims.mode++;

    /* (the original sets ot_base again here, to what it was) */
    if (side != 0) {
        ot_base = TEAM_LAYER_SIDE_2;
        clip_x = TEAM_PANEL_WINDOW_X + TEAM_SIDE_X;
    } else {
        ot_base = TEAM_LAYER_SIDE_1;
        clip_x = TEAM_PANEL_WINDOW_X;
    }
    /* Before the page: the draw area cut down to what is not covered. */
    rect.x = PTR(g_select_team->db)->draw.clip.x + clip_x;
    rect.y = PTR(g_select_team->db)->draw.clip.y + TEAM_PANEL_Y;
    rect.w = TEAM_PANEL_WINDOW_WIDTH;
    rect.h = TEAM_PANEL_HEIGHT - g_select_team->panel_cover[side];
    SetDrawArea(ctx->prims.area, &rect);
    AddPrim(g_disp_ot + ot_base + TEAM_PANEL_LAYER_PAPER, ctx->prims.area);
    ctx->prims.area++;

    /* After it: the whole screen again. */
    rect.x = PTR(g_select_team->db)->draw.clip.x;
    rect.y = PTR(g_select_team->db)->draw.clip.y;
    rect.w = PTR(g_select_team->db)->draw.clip.w;
    rect.h = PTR(g_select_team->db)->draw.clip.h;
    SetDrawArea(ctx->prims.area, &rect);
    AddPrim(g_disp_ot + ot_base, ctx->prims.area);
    ctx->prims.area++;

    /* The same for the draw offset, which pushes the page up. */
    offset[0] = PTR(g_select_team->db)->draw.ofs[0];
    offset[1] =
        PTR(g_select_team->db)->draw.ofs[1] - g_select_team->panel_scroll[side];
    SetDrawOffset(ctx->prims.offset, offset);
    AddPrim(g_disp_ot + ot_base + TEAM_PANEL_LAYER_PAPER, ctx->prims.offset);
    ctx->prims.offset++;

    offset[0] = PTR(g_select_team->db)->draw.ofs[0];
    offset[1] = PTR(g_select_team->db)->draw.ofs[1];
    SetDrawOffset(ctx->prims.offset, offset);
    AddPrim(g_disp_ot + ot_base, ctx->prims.offset);
    ctx->prims.offset++;
}

/* Moves each side's panel a frame. When a page has slid away or been
 * covered the side changes step and the second half of the move starts. */
static void select_StepTeamPanels(void) {
    TeamSelect *team;
    s32 i;

    team = g_select_team;
    for (i = 0; i < 2; i++) {
        switch ((u8)team->panel_move.half[i]) {
        case TEAM_PANEL_SLIDE_AWAY:
            team->panel_scroll[i] += TEAM_PANEL_SPEED;
            team->panel_cover[i] += TEAM_PANEL_SPEED;
            if (team->panel_cover[i] >= TEAM_PANEL_HEIGHT) {
                /* to TEAM_PANEL_REVEAL */
                if ((team->panel_move.half[i] >> 8) == 0) {
                    team->panel_move.half[i] += 1;
                }
                team->panel_scroll[i] = 0;
                team->panel_cover[i] = TEAM_PANEL_HEIGHT;
                team->step.half[i] = team->next_step[i];
                team->cursor[i] = team->next_cursor[i];
            }
            break;
        case TEAM_PANEL_COVER:
            team->panel_cover[i] += TEAM_PANEL_SPEED;
            if (team->panel_cover[i] >= TEAM_PANEL_HEIGHT) {
                team->panel_scroll[i] = TEAM_PANEL_HEIGHT;
                team->panel_cover[i] = TEAM_PANEL_HEIGHT;
                team->step.half[i] = team->next_step[i];
                team->cursor[i] = team->next_cursor[i];
                /* to TEAM_PANEL_SLIDE_BACK */
                if ((team->panel_move.half[i] >> 8) == 0) {
                    team->panel_move.half[i] += 1;
                }
            }
            break;
        case TEAM_PANEL_REVEAL:
            team->panel_cover[i] -= TEAM_PANEL_SPEED;
            if (team->panel_cover[i] <= 0) {
                team->panel_scroll[i] = 0;
                team->panel_cover[i] = 0;
                team->panel_move.half[i] = TEAM_PANEL_STILL;
            }
            break;
        case TEAM_PANEL_SLIDE_BACK:
            team->panel_scroll[i] -= TEAM_PANEL_SPEED;
            team->panel_cover[i] -= TEAM_PANEL_SPEED;
            if (team->panel_cover[i] <= 0) {
                team->panel_scroll[i] = 0;
                team->panel_cover[i] = 0;
                team->panel_move.half[i] = TEAM_PANEL_STILL;
            }
            break;
        }
    }
}

/* ---- the clans ---- */

/* Draws a side's clan page: the side's cursor on the row of its clan,
 * glowing; the two clans' emblems; and the page itself. */
static void select_DrawTeamClans(MenuDrawCtx *ctx, s32 side) {
    SpriteDraw draw;
    SpriteSheetSlot *sheets;
    s16 i;
    SpriteCell *cells;
    s32 no_abr;

    no_abr = 0;
    draw.semi = 0;
    if (side != 0) {
        draw.ot_index = TEAM_LAYER_SIDE_2 + TEAM_PANEL_LAYER_PAGE;
    } else {
        draw.ot_index = TEAM_LAYER_SIDE_1 + TEAM_PANEL_LAYER_PAGE;
    }
    sheets = PTR(ctx->file.team_select->clan_sheets);
    draw.header = PTR(sheets[side + TEAM_CLAN_SHEET_CURSORS]);
    draw.x = side * TEAM_SIDE_X;
    cells = draw.header->cells;
    draw.table = cells;
    draw.y = g_select_team->clan[side] * TEAM_CLAN_ROW_HEIGHT +
             g_select_team->nudge[side][1];
    draw.col_r = draw.col_g = draw.col_b = TEAM_GLOW(TEAM_TINT_PLAIN);
    draw.has_color = 1;
    draw.sprt_out = ctx->prims.sprt;
    ctx->prims.sprt = sprite_Draw(&draw);
    draw.has_color = 0;
    sheets = PTR(ctx->file.team_select->name_sheets);
    i = 0;
    draw.header = PTR(sheets[TEAM_NAME_SHEET_EMBLEMS]);
    draw.x = side * TEAM_SIDE_X + TEAM_CLAN_EMBLEM_X;
    draw.y = TEAM_CLAN_EMBLEM_Y;
    cells = draw.header->cells;
    draw.table = cells;
    draw.sprt_out = ctx->prims.sprt;
    ctx->prims.sprt = sprite_Draw(&draw);
    draw.y = TEAM_CLAN_EMBLEM_Y + TEAM_CLAN_ROW_HEIGHT;
    draw.table += draw.header->count;
    draw.sprt_out = ctx->prims.sprt;
    ctx->prims.sprt = sprite_Draw(&draw);
    SetDrawMode(ctx->prims.mode, 1, 0, sprite_GetTPage(draw.header, no_abr), 0);
    AddPrim(g_disp_ot + draw.ot_index, ctx->prims.mode);
    ctx->prims.mode++;
    sheets = PTR(ctx->file.team_select->clan_sheets);
    do {
        draw.header = PTR(sheets[i]);
        draw.x = side * TEAM_SIDE_X;
        draw.y = 0;
        cells = draw.header->cells;
        if (side != 0) {
            draw.ot_index = TEAM_LAYER_SIDE_2 + TEAM_PANEL_LAYER_PAGE;
        } else {
            draw.ot_index = TEAM_LAYER_SIDE_1 + TEAM_PANEL_LAYER_PAGE;
        }
        draw.table = cells;
        draw.sprt_out = ctx->prims.sprt;
        ctx->prims.sprt = sprite_Draw(&draw);
        SetDrawMode(
            ctx->prims.mode, 1, 0, sprite_GetTPage(draw.header, no_abr), 0);
        AddPrim(g_disp_ot + draw.ot_index, ctx->prims.mode);
        i++;
        ctx->prims.mode++;
    } while (i < TEAM_CLAN_SHEET_CURSORS);
}

/* A side's pad on the clan page. Up or down changes the clan. The cross
 * button takes it, gives the other side the other clan and sends both on
 * to their fighters. The triangle button goes back to the rules, if the
 * other side is on this page too. */
static void select_HandleTeamClans(s32 pressed, s32 side) {
    s16 i;
    TeamSelect *moving;
    TeamSelect *team;

    moving = g_select_team;
    if (moving->panel_move.word != 0) {
        return;
    }
    if (NUDGE_DOWN(menu_StepNudge(&pressed, side, moving->nudge[side],
                                  g_select_team_nudge_steps[side])) != 0) {
        g_select_team->pulse_frames = 0;
        g_select_team->clan[side] = g_select_team->clan[side] + 1;
        g_select_team->clan[side] &= 1;
        snd_Play(MENU_SOUND_CURSOR, SND_VOLUME_MAX, SND_VOLUME_MAX);
    }
    team = g_select_team;
    team->cursor[side] = team->clan[side];
    if (pressed & PAD_OF(PADRdown, side)) {
        for (i = 0; i < 2; i++) {
            team->next_cursor[i] = 0;
            team->panel_move.half[i] = TEAM_PANEL_SLIDE_AWAY;
            team->next_step[i] = TEAM_STEP_FIGHTERS;
        }
        g_select_team->clan[(side + 1) & 1] =
            (g_select_team->clan[side] + 1) & 1;
        snd_Play(MENU_SOUND_CONFIRM, SND_VOLUME_MAX, SND_VOLUME_MAX);
        return;
    }
    if (pressed & PAD_OF(PADRup, side)) {
        if (team->step.half[(side != 0) ? 0 : 1] == TEAM_STEP_CLAN) {
            for (i = 0; i < 2; i++) {
                team->next_cursor[i] = 0;
                team->panel_move.half[i] = TEAM_PANEL_COVER;
                team->next_step[i] = TEAM_STEP_RULES;
            }
            snd_Play(MENU_SOUND_CANCEL, SND_VOLUME_MAX, SND_VOLUME_MAX);
        } else {
            snd_Play(TEAM_SOUND_REFUSED, SND_VOLUME_MAX, SND_VOLUME_MAX);
        }
    }
}

/* ---- the fighters ---- */

/* Strikes out the name at a place of the grid (`grid_index` counts through
 * both clans' grids). The line glows if the side's cursor is there. */
static void select_DrawTeamStrike(
    MenuDrawCtx *ctx, s32 grid_index, s32 side, s32 semi) {
    SpriteDraw draw;
    s16 column;
    s16 row;
    draw.semi = semi;
    draw.col_r = draw.col_g = draw.col_b = TEAM_STRIKE_GLOW;
    draw.header =
        PTR(PTR(ctx->file.team_select->name_sheets)[TEAM_NAME_SHEET_STRIKE]);
    draw.table = draw.header->cells;
    if (grid_index < TEAM_CLAN_FIGHTERS) {
        column = grid_index / TEAM_GRID_ROWS;
        row = grid_index % TEAM_GRID_ROWS;
    } else {
        column = (grid_index - TEAM_CLAN_FIGHTERS) / TEAM_GRID_ROWS;
        row = (grid_index - TEAM_CLAN_FIGHTERS) % TEAM_GRID_ROWS;
    }
    if (g_select_team->fighter_column.half[side] == column &&
        g_select_team->fighter_row.half[side] == row) {
        draw.has_color = 1;
    } else {
        draw.has_color = 0;
    }
    draw.x = side * TEAM_SIDE_X + column * TEAM_GRID_COL_WIDTH;
    draw.y = row * TEAM_GRID_ROW_HEIGHT;
    if (side != 0) {
        draw.ot_index = TEAM_LAYER_SIDE_2 + TEAM_PANEL_LAYER_STRIKE;
    } else {
        draw.ot_index = TEAM_LAYER_SIDE_1 + TEAM_PANEL_LAYER_STRIKE;
    }
    draw.sprt_out = ctx->prims.sprt;
    ctx->prims.sprt = sprite_Draw(&draw);
}

/* Draws a side's fighter page: the clan's emblem; the grid of the clan's
 * ten names, the one under the cursor in the side's colour, the ones
 * already in the team struck out lightly and the locked ones struck out;
 * the team so far, with the fighter under the cursor in the place being
 * filled; and the places' numbers. `fighters` is the side's team. */
static void select_DrawTeamFighters(
    MenuDrawCtx *ctx, s32 clan, s16 *fighters, s32 side) {
    SpriteDraw draw;
    SpriteSheetSlot *sheets;
    s32 no_abr;
    s16 i;
    /* Where a sheet has three headers these are the third header's cells. */
    SpriteCell *cells;

    no_abr = 0;
    draw.semi = 0;
    draw.has_color = 0;
    sheets = PTR(ctx->file.team_select->name_sheets);
    draw.header = PTR(sheets[TEAM_NAME_SHEET_EMBLEMS]);
    draw.x = side * TEAM_SIDE_X + TEAM_EMBLEM_X;
    draw.y = TEAM_EMBLEM_Y;
    cells = draw.header->cells;
    draw.table = cells;
    if (side != 0) {
        draw.ot_index = TEAM_LAYER_SIDE_2 + TEAM_PANEL_LAYER_PAGE;
    } else {
        draw.ot_index = TEAM_LAYER_SIDE_1 + TEAM_PANEL_LAYER_PAGE;
    }
    if (clan != 0) {
        draw.table += draw.header->count;
    }
    draw.sprt_out = ctx->prims.sprt;
    ctx->prims.sprt = sprite_Draw(&draw);
    SetDrawMode(ctx->prims.mode, 1, 0, sprite_GetTPage(draw.header, no_abr), 0);
    AddPrim(g_disp_ot + draw.ot_index, ctx->prims.mode);
    ctx->prims.mode++;

    draw.col_r = draw.col_g = draw.col_b = TEAM_GLOW(TEAM_TINT_PLACE);
    if (side != 0) {
        draw.ot_index = TEAM_LAYER_SIDE_2 + TEAM_PANEL_LAYER_TEXT;
    } else {
        draw.ot_index = TEAM_LAYER_SIDE_1 + TEAM_PANEL_LAYER_TEXT;
    }
    draw.x = side * TEAM_SIDE_X;
    /* The grid; i counts through both clans' grids. */
    for (i = clan * TEAM_CLAN_FIGHTERS;
         i < clan * TEAM_CLAN_FIGHTERS + TEAM_CLAN_FIGHTERS; i++) {
        u8 fighter = ((SelectCell *)g_select_team_grid)[i].value;

        if (g_select_team_fighter_flags[fighter] & SELECT_FIGHTER_OPEN) {
            draw.header = PTR(sheets[fighter + TEAM_NAME_SHEET_FIRST]);
            cells = draw.header[TEAM_LOOKS - 1].cells;
            draw.table = cells;
            if (g_select_team_grid[clan]
                                  [g_select_team->fighter_column.half[side] *
                                       TEAM_GRID_ROWS +
                                   g_select_team->fighter_row.half[side]]
                                      .value ==
                ((SelectCell *)g_select_team_grid)[i].value) {
                draw.has_color = 1;
                draw.header += 1 + side;
            } else {
                draw.has_color = 0;
            }
            draw.y = 0;
            draw.sprt_out = ctx->prims.sprt;
            ctx->prims.sprt = sprite_Draw(&draw);
            if (g_select_team_fighter_flags
                    [((SelectCell *)g_select_team_grid)[i].value] &
                (SELECT_FIGHTER_TAKEN << side)) {
                select_DrawTeamStrike(ctx, i, side, 1);
            }
        } else {
            select_DrawTeamStrike(ctx, i, side, 0);
        }
    }

    /* The team so far. */
    for (i = 0; i < g_select_team->cursor[side] + 1; i++) {
        if (fighters[i] >= 0) {
            draw.header = PTR(sheets[fighters[i] + TEAM_NAME_SHEET_FIRST]);
            cells = draw.header[TEAM_LOOKS - 1].cells;
            if (i != g_select_team->cursor[side]) {
                draw.has_color = 0;
            } else {
                draw.has_color = 1;
                draw.header += 1 + side;
            }
            draw.y = i * TEAM_PLACE_HEIGHT;
            draw.table = cells;
            draw.table += draw.header->count;
            draw.sprt_out = ctx->prims.sprt;
            ctx->prims.sprt = sprite_Draw(&draw);
        }
    }

    /* The places' numbers. */
    draw.has_color = 0;
    for (i = 0; i < g_select_team->match_count_index + TEAM_MIN_MATCHES; i++) {
        sheets = PTR(ctx->file.team_select
                         ->slot_sheets[g_select_team->match_count_index]);
        draw.header = PTR(sheets[i]);
        cells = draw.header[TEAM_LOOKS - 1].cells;
        if (i == g_select_team->cursor[side]) {
            draw.header += 1 + side;
        }
        draw.table = cells;
        draw.x = side * TEAM_SIDE_X;
        draw.y = i * TEAM_PLACE_HEIGHT;
        if (side != 0) {
            draw.ot_index = TEAM_LAYER_SIDE_2 + TEAM_PANEL_LAYER_TEXT;
        } else {
            draw.ot_index = TEAM_LAYER_SIDE_1 + TEAM_PANEL_LAYER_TEXT;
        }
        draw.sprt_out = ctx->prims.sprt;
        ctx->prims.sprt = sprite_Draw(&draw);
    }

    /* The texture page for the names, and for the strikes over them. */
    sheets = PTR(ctx->file.team_select->name_sheets);
    draw.header = PTR(sheets[TEAM_NAME_SHEET_FIRST]);
    SetDrawMode(ctx->prims.mode, 1, 0, sprite_GetTPage(draw.header, no_abr), 0);
    AddPrim(g_disp_ot + draw.ot_index, ctx->prims.mode);
    ctx->prims.mode++;
    SetDrawMode(ctx->prims.mode, 1, 0, sprite_GetTPage(draw.header, no_abr), 0);
    AddPrim(g_disp_ot + draw.ot_index - 1, ctx->prims.mode);
    ctx->prims.mode++;
}

/* The cursor is on a fighter that cannot be taken: the place being filled
 * shows the strike, and the cross button is refused. */
static inline void select_ShowTeamNoFighter(
    s32 pressed, s16 *fighters, s32 side) {
    fighters[g_select_team->cursor[side]] = TEAM_NO_FIGHTER;
    if (pressed & PAD_OF(PADRdown, side)) {
        snd_Play(TEAM_SOUND_REFUSED, SND_VOLUME_MAX, SND_VOLUME_MAX);
    }
}

/* A side's pad on the fighter page. The directions move the cursor over
 * the grid, wrapping; the cross button puts the fighter under it in the
 * team's next place, and when the team is full the side goes on to the
 * weapons, all of them to give out. The triangle button takes the last
 * fighter back, or with none taken goes back to the clans, if the other
 * side has none either and is on this page. */
static void select_HandleTeamFighters(
    s32 pressed, s32 clan, s16 *fighters, s32 side) {
    u8 *flags;
    s32 events;
    s32 taken;
    s32 i;

    if (g_select_team->panel_move.half[side] != TEAM_PANEL_STILL) {
        return;
    }

    if (pressed & PAD_OF(PADRup, side)) {
        snd_Play(MENU_SOUND_CANCEL, SND_VOLUME_MAX, SND_VOLUME_MAX);
        if (g_select_team->cursor[side] != 0) {
            fighters[g_select_team->cursor[side]] = -1;
            g_select_team->cursor[side]--;
            g_select_team_fighter_flags
                [fighters[g_select_team->cursor[side]]] &=
                ~(SELECT_FIGHTER_TAKEN << side);
            return;
        }
        if (g_select_team->cursor[0] != 0 || g_select_team->cursor[1] != 0) {
            return;
        }
        if (g_select_team->step.half[(side != 0) ? 0 : 1] ==
            TEAM_STEP_FIGHTERS) {
            g_select_team->panel_move.half[1] = TEAM_PANEL_COVER;
            g_select_team->panel_move.half[0] = TEAM_PANEL_COVER;
            g_select_team->next_step[1] = TEAM_STEP_CLAN;
            g_select_team->next_step[0] = TEAM_STEP_CLAN;
        }
        return;
    }

    events = menu_StepNudge(&pressed, side, g_select_team->nudge[side],
                            g_select_team_nudge_steps[side]);
    if (pressed & PAD_OF(PADLup | PADLright | PADLdown | PADLleft, side)) {
        snd_Play(MENU_SOUND_CURSOR, SND_VOLUME_MAX, SND_VOLUME_MAX);
    }
    {
        s32 across;

        across = NUDGE_ACROSS(events);
        if (across < 3 && across != 0) {
            g_select_team->fighter_column.half[side] =
                (g_select_team->fighter_column.half[side] + 1) & 1;
        }
    }

    switch (NUDGE_DOWN(events)) {
    case NUDGE_PLUS: {
        s16 row;

        row = g_select_team->fighter_row.half[side];
        if (row == TEAM_GRID_ROWS - 1) {
            g_select_team->fighter_row.half[side] = 0;
        } else {
            g_select_team->fighter_row.half[side] = row + 1;
        }
    } break;
    case NUDGE_MINUS: {
        s16 row;

        row = g_select_team->fighter_row.half[side];
        if (row == 0) {
            g_select_team->fighter_row.half[side] = TEAM_GRID_ROWS - 1;
        } else {
            g_select_team->fighter_row.half[side] = row - 1;
        }
    } break;
    }

    {
        u8 fighter;

        fighter =
            g_select_team_grid[clan][g_select_team->fighter_column.half[side] *
                                         TEAM_GRID_ROWS +
                                     g_select_team->fighter_row.half[side]]
                .value;
        flags = &g_select_team_fighter_flags[fighter];
        if (*flags & SELECT_FIGHTER_OPEN) {
            taken = SELECT_FIGHTER_TAKEN << side;
            if (!(*flags & taken)) {
                u16 place;

                fighters[g_select_team->cursor[side]] = fighter;
                if (pressed & PAD_OF(PADRdown, side)) {
                    snd_Play(
                        MENU_SOUND_CONFIRM, SND_VOLUME_MAX, SND_VOLUME_MAX);
                    *flags |= taken;
                    place = g_select_team->cursor[side];
                    g_select_team->cursor[side] = place + 1;
                    if (g_select_team->cursor[side] ==
                        g_select_team->match_count_index + TEAM_MIN_MATCHES) {
                        g_select_team->panel_move.half[side] =
                            TEAM_PANEL_SLIDE_AWAY;
                        g_select_team->cursor[side] = place;
                        g_select_team->next_cursor[side] = 0;
                        g_select_team->next_step[side] = TEAM_STEP_WEAPONS;
                        for (i = 0; i < g_select_team->weapons_left_count[side];
                             i++) {
                            g_select_team->weapons_left[side][i] = i;
                        }
                        if (clan != 0) {
                            g_select_team
                                ->weapons_left[side][TEAM_WEAPONS - 1] =
                                TEAM_WEAPON_SECOND_CLAN;
                        }
                    }
                }
                return;
            }
            select_ShowTeamNoFighter(pressed, fighters, side);
        } else {
            select_ShowTeamNoFighter(pressed, fighters, side);
        }
    }
}

/* ---- the weapons ---- */

/* Draws a side's weapon page: the cursor's bar on the place being armed;
 * the team's names; the weapon of each place armed so far, and in the
 * place being armed the weapon under the cursor, glowing; and the places'
 * numbers. Once the side is ready every place is in the side's colour.
 * `clan` is not used. */
static void select_DrawTeamWeapons(
    MenuDrawCtx *ctx, s32 clan, s16 *fighters, s32 side) {
    SpriteDraw draw;
    s32 ot_index;
    SpriteSheetSlot *sheets;
    /* Where a sheet has three headers these are the third header's cells. */
    SpriteCell *cells;
    s16 i;
    s32 no_abr;
    u16 weapon;

    no_abr = 0;
    ot_index = TEAM_LAYER_SIDE_1 + TEAM_PANEL_LAYER_TEXT;
    draw.semi = 0;
    if (side != 0) {
        ot_index = TEAM_LAYER_SIDE_2 + TEAM_PANEL_LAYER_TEXT;
    }
    sheets = PTR(ctx->file.team_select->weapon_sheets);
    if (g_select_team->step.half[side] < TEAM_STEP_READY) {
        draw.header = PTR(sheets[TEAM_WEAPON_SHEET_CURSOR]);
        draw.has_color = 0;
        draw.x = side * TEAM_SIDE_X;
        cells = draw.header->cells;
        draw.table = cells;
        draw.y = g_select_team->cursor[side] * TEAM_PLACE_TALL_HEIGHT;
        draw.ot_index = ot_index;
        draw.sprt_out = ctx->prims.sprt;
        ctx->prims.sprt = sprite_Draw(&draw);
        SetDrawMode(
            ctx->prims.mode, 1, 0, sprite_GetTPage(draw.header, no_abr), 0);
        AddPrim(g_disp_ot + ot_index, ctx->prims.mode);
        ctx->prims.mode++;
    }

    draw.has_color = 0;
    draw.col_r = draw.col_g = draw.col_b = TEAM_GLOW(TEAM_TINT_PLACE);
    sheets = PTR(ctx->file.team_select->name_sheets);
    for (i = 0; i < g_select_team->match_count_index + TEAM_MIN_MATCHES; i++) {
        draw.has_color = 0;
        draw.header = PTR(sheets[fighters[i] + TEAM_NAME_SHEET_FIRST]);
        cells = draw.header[TEAM_LOOKS - 1].cells;
        if (g_select_team->cursor[side] == i ||
            g_select_team->step.half[side] >= TEAM_STEP_READY) {
            draw.header += 1 + side;
        }
        draw.table = cells;
        draw.table += draw.header->count * 2;
        draw.x = side * TEAM_SIDE_X;
        draw.y = i * TEAM_PLACE_TALL_HEIGHT;
        draw.ot_index = ot_index;
        draw.sprt_out = ctx->prims.sprt;
        ctx->prims.sprt = sprite_Draw(&draw);
    }

    sheets = PTR(ctx->file.team_select->weapon_sheets);
    for (i = 0; i < g_select_team->cursor[side] + 1; i++) {
        if (g_select_team->cursor[side] != i ||
            g_select_team->step.half[side] >= TEAM_STEP_READY) {
            weapon = g_select_team->weapons[side][i];
            draw.has_color = 0;
        } else {
            weapon =
                g_select_team
                    ->weapons_left[side][g_select_team->weapon_cursor[side]];
            draw.has_color = 1;
        }
        draw.header = PTR(sheets[(s16)weapon * 2]);
        cells = draw.header[TEAM_LOOKS - 1].cells;
        if (g_select_team->cursor[side] == i ||
            g_select_team->step.half[side] >= TEAM_STEP_READY) {
            draw.header += 1 + side;
        }
        draw.x = side * TEAM_SIDE_X;
        draw.table = cells;
        draw.y = i * TEAM_PLACE_TALL_HEIGHT;
        draw.ot_index = ot_index;
        draw.sprt_out = ctx->prims.sprt;
        ctx->prims.sprt = sprite_Draw(&draw);
        draw.header = PTR(sheets[(s16)weapon * 2 + 1]);
        draw.has_color = 0;
        cells = draw.header->cells;
        draw.table = cells;
        draw.sprt_out = ctx->prims.sprt;
        ctx->prims.sprt = sprite_Draw(&draw);
    }

    sheets = PTR(
        ctx->file.team_select->slot_sheets[g_select_team->match_count_index]);
    for (i = 0; i < g_select_team->match_count_index + TEAM_MIN_MATCHES; i++) {
        draw.has_color = 0;
        draw.header = PTR(sheets[i]);
        cells = draw.header[TEAM_LOOKS - 1].cells;
        if (g_select_team->cursor[side] == i ||
            g_select_team->step.half[side] >= TEAM_STEP_READY) {
            draw.header += 1 + side;
        }
        draw.table = cells;
        draw.table += draw.header->count;
        draw.x = side * TEAM_SIDE_X;
        draw.y = i * TEAM_PLACE_TALL_HEIGHT;
        draw.sprt_out = ctx->prims.sprt;
        ctx->prims.sprt = sprite_Draw(&draw);
    }

    sheets = PTR(ctx->file.team_select->name_sheets);
    draw.header = PTR(sheets[TEAM_NAME_SHEET_FIRST]);
    SetDrawMode(ctx->prims.mode, 1, 0, sprite_GetTPage(draw.header, no_abr), 0);
    AddPrim(g_disp_ot + ot_index, ctx->prims.mode);
    ctx->prims.mode++;
}

/* A side's pad on the weapon page. Left and right go through the weapons
 * not yet given out; the cross button gives the one under the cursor to
 * the place being armed, and after the last place the side is ready. The
 * triangle button takes the last weapon back, into its place in the order,
 * or from the first place goes back to the fighters with the last of them
 * to pick again. */
static void select_HandleTeamWeapons(
    s32 pressed, s32 clan, s16 *fighters, s32 side) {
    s16 i;
    s16 j;

    if (g_select_team->panel_move.half[side] != TEAM_PANEL_STILL) {
        return;
    }
    if (pressed & PAD_OF(PADLright | PADLleft, side)) {
        snd_Play(MENU_SOUND_CURSOR, SND_VOLUME_MAX, SND_VOLUME_MAX);
    }
    switch (
        NUDGE_ACROSS(menu_StepNudge(&pressed, side, g_select_team->nudge[side],
                                    g_select_team_nudge_steps[side]))) {
    case NUDGE_PLUS:
        g_select_team->weapon_cursor[side]++;
        if (g_select_team->weapon_cursor[side] >=
            g_select_team->weapons_left_count[side]) {
            g_select_team->weapon_cursor[side] = 0;
        }
        break;
    case NUDGE_MINUS:
        g_select_team->weapon_cursor[side]--;
        if (g_select_team->weapon_cursor[side] < 0) {
            g_select_team->weapon_cursor[side] =
                g_select_team->weapons_left_count[side] - 1;
        }
        break;
    }

    if (pressed & PAD_OF(PADRdown, side)) {
        if (g_select_team->cursor[side] <
            g_select_team->match_count_index + TEAM_MIN_MATCHES) {
            snd_Play(MENU_SOUND_CONFIRM, SND_VOLUME_MAX, SND_VOLUME_MAX);
            g_select_team->weapons[side][g_select_team->cursor[side]] =
                g_select_team
                    ->weapons_left[side][g_select_team->weapon_cursor[side]];
            if (g_select_team->cursor[side] ==
                g_select_team->match_count_index + (TEAM_MIN_MATCHES - 1)) {
                g_select_team->step.half[side] = TEAM_STEP_READY;
                return;
            }
            g_select_team->weapons_left_count[side]--;
            for (i = g_select_team->weapon_cursor[side];
                 i < g_select_team->weapons_left_count[side]; i++) {
                g_select_team->weapons_left[side][i] =
                    g_select_team->weapons_left[side][i + 1];
            }
            g_select_team->weapon_cursor[side] = 0;
            g_select_team->cursor[side]++;
        }
    } else if (pressed & PAD_OF(PADRup, side)) {
        snd_Play(MENU_SOUND_CANCEL, SND_VOLUME_MAX, SND_VOLUME_MAX);
        if (g_select_team->cursor[side] == 0) {
            g_select_team->panel_move.half[side] = TEAM_PANEL_COVER;
            g_select_team->next_step[side] = TEAM_STEP_FIGHTERS;
            g_select_team->next_cursor[side] =
                g_select_team->match_count_index + (TEAM_MIN_MATCHES - 1);
            g_select_team_fighter_flags
                [fighters[g_select_team->next_cursor[side]]] &=
                ~(SELECT_FIGHTER_TAKEN << side);
            g_select_team->weapons_left_count[side] = TEAM_WEAPONS;
            for (i = 0; i < g_select_team->weapons_left_count[side]; i++) {
                g_select_team->weapons_left[side][i] = i;
            }
            if (clan != 0) {
                g_select_team->weapons_left[side][TEAM_WEAPONS - 1] =
                    TEAM_WEAPON_SECOND_CLAN;
            }
            return;
        }
        for (i = 0; i < g_select_team->weapons_left_count[side]; i++) {
            if (g_select_team->weapons_left[side][i] >
                g_select_team->weapons[side][g_select_team->cursor[side] - 1]) {
                for (j = g_select_team->weapons_left_count[side] - 1; j >= i;
                     j--) {
                    g_select_team->weapons_left[side][j + 1] =
                        g_select_team->weapons_left[side][j];
                }
                g_select_team->weapons_left[side][i] =
                    g_select_team
                        ->weapons[side][g_select_team->cursor[side] - 1];
                break;
            }
            if (i == g_select_team->weapons_left_count[side] - 1) {
                g_select_team->weapons_left[side][i + 1] =
                    g_select_team
                        ->weapons[side][g_select_team->cursor[side] - 1];
            }
        }
        g_select_team->weapons_left_count[side]++;
        g_select_team->cursor[side]--;
    }
}

/* ---- the end ---- */

/* Fades the screen out a step. When it is dark the rules and both teams
 * go to the settings, and the result is 1. The time limit is counted from
 * the shortest: one point a match offers the first two, two points the
 * last two. */
static s32 select_FadeTeamOut(MenuDrawCtx *ctx) {
    TILE *tile;
    GameSettings *settings;
    u16 *fade;
    s16 level;
    s16 i;
    s16 j;
    s32 time_limit;
    s32 done;

    done = 0;
    fade = &g_select_team->fade;
    level = *fade + TEAM_FADE_STEP;
    *fade = level;
    if (level >= TEAM_FADE_FULL) {
        *fade = TEAM_FADE_FULL;
        settings = PTR(g_select_team->settings);
        settings->match_count_index = g_select_team->match_count_index;
        done = 1;
        if (g_select_team->time_sheet < 3) {
            time_limit = g_select_team->time_sheet - 1;
        } else {
            time_limit = g_select_team->time_sheet - 2;
        }
        settings->time_limit_index = time_limit;
        settings->win_target_index = g_select_team->win_target_index;
        settings->team_clan_bits =
            g_select_team->clan[0] + g_select_team->clan[1] * 2;
        for (i = 0; i < 2; i++) {
            for (j = 0; j < g_select_team->match_count_index + TEAM_MIN_MATCHES;
                 j++) {
                /* The table's second column is by fighter of the screen,
                 * through both clans. */
                settings->match_picks[i][j].fighter =
                    g_select_team_grid[0][g_select_team->fighters[i][j]]
                        .fighter;
                settings->match_picks[i][j].weapon =
                    g_select_team->weapons[i][j];
            }
        }
    }
    tile = ctx->prims.tile;
    SetTile(tile);
    tile->r0 = *fade;
    tile->g0 = *fade;
    tile->b0 = *fade;
    tile->x0 = 0;
    tile->y0 = 0;
    tile->w = DISP_WIDTH;
    tile->h = DISP_HEIGHT;
    SetSemiTrans(tile, 1);
    AddPrim(g_disp_ot + TEAM_LAYER_FADE, tile);
    tile++;
    ctx->prims.tile = tile;
    /* The tile is taken away from what is under it. */
    SetDrawMode(ctx->prims.mode, 1, 0, SPRITE_ABR_SUBTRACT, 0);
    AddPrim(g_disp_ot + TEAM_LAYER_FADE, ctx->prims.mode);
    ctx->prims.mode++;
    return done;
}

/* ---- the screen ---- */

/* Turns the offsets in the file's ten lists into addresses. Returns where
 * the file's image began, which is free once it is in video memory. */
s32 select_RelocateTeamFile(TeamSelectFile *file) {
    sprite_RelocateList(file, PTR(file->name_sheets));
    sprite_RelocateList(file, PTR(file->panel_sheets));
    sprite_RelocateList(file, PTR(file->heading_sheets));
    sprite_RelocateList(file, PTR(file->slot_sheets[0]));
    sprite_RelocateList(file, PTR(file->slot_sheets[1]));
    sprite_RelocateList(file, PTR(file->slot_sheets[2]));
    sprite_RelocateList(file, PTR(file->clan_sheets));
    sprite_RelocateList(file, PTR(file->weapon_sheets));
    sprite_RelocateList(file, PTR(file->rule_sheets));
    sprite_RelocateList(file, PTR(file->frame_sheets));
    return file->head.free_ram_start;
}

/* The pool of frame `frame` (0 or 1). */
static PrimPool *select_GetTeamPrimPool(s32 frame) {
    return &g_select_team_pools[frame];
}

static inline void select_ClearNudgeSteps(s16 *steps) {
    steps[1] = 0;
    steps[0] = 0;
}

/* Loads D_SEL.BIN to `memory` (the two frames' pool records come first, the
 * work area after the pools) and starts the screen at the rules: each side
 * with its own clan, nobody in a team, every weapon to give out. `unlocks`
 * has a bit for each of the twenty fighters that can be picked; a team can
 * have no more fighters than the smaller clan has unlocked. */
s32 select_LoadTeam(s32 memory, GameSettings *settings, s32 unlocks) {
    s16 open_fighters[2] = {0, 0}; /* of each clan */
    /* The file, then the work area. */
    void *work;
    s32 free_ram;
    s16 side;
    s16 i;

    ClearOTagR(g_disp_ot, DISP_OT_SIZE);
    work = (void *)ADDR(memory + 2 * sizeof(PrimPool));
    g_select_team_pools = (PrimPool *)ADDR(memory);
    resource_FreeAll();
    sprite_LoadFile(TEAM_FILE, work);
    free_ram = select_RelocateTeamFile(work);
    {
        TeamSelectFile *file = work;
        work = (void *)ADDR(sprite_InitPools(free_ram, g_select_team_pools));
        g_select_team = work;
        SET_PTR(g_select_team->file, file);
        /* FAKE: a store nothing reads */
        work = file;
        g_select_team->pool_frame = 0;
        g_select_team->pulse_frames = 0;
    }
    for (side = 0; side < 2; side++) {
        TeamSelect *team = g_select_team;
        i = 0;
        team->panel_move.half[side] = TEAM_PANEL_STILL;
        team->panel_scroll[side] = 0;
        team->panel_cover[side] = 0;
        team->step.half[side] = TEAM_STEP_RULES;
        team->cursor[side] = 0;
        select_ClearNudgeSteps(g_select_team_nudge_steps[side]);
        team->nudge[side][1] = 0;
        team->nudge[side][0] = 0;
        team->clan[side] = side;
        do {
            g_select_team->fighters[side][i] = -1;
            g_select_team->weapons[side][i] = 0;
            i++;
        } while (i < TEAM_MAX_MATCHES);
        g_select_team->weapon_cursor[side] = 0;
        g_select_team->weapons_left_count[side] = TEAM_WEAPONS;
        /* Which of the clan's ten are unlocked; nobody is taken. */
        for (i = 0; i < TEAM_CLAN_FIGHTERS; i++) {
            s16 fighter = i + (side * TEAM_CLAN_FIGHTERS);
            s32 bit = 1 << fighter;
            g_select_team_fighter_flags[fighter] =
                g_select_team_fighter_flags[fighter] &
                ~(SELECT_FIGHTER_OPEN | SELECT_FIGHTER_TAKEN |
                  SELECT_FIGHTER_TAKEN << 1);
            if (unlocks & bit) {
                g_select_team_fighter_flags[fighter] =
                    g_select_team_fighter_flags[fighter] | SELECT_FIGHTER_OPEN;
                open_fighters[side] += 1;
            }
        }
    }
    /* No more matches than the smaller clan has fighters for. */
    {
        TeamSelect *team = g_select_team;
        team->fighter_row.word = 0;
        team->fighter_column.word = 0;
        if (open_fighters[0] < open_fighters[1]) {
            team->max_match_count_index = open_fighters[0] - TEAM_MIN_MATCHES;
        } else {
            team->max_match_count_index = open_fighters[1] - TEAM_MIN_MATCHES;
        }
    }
    if (g_select_team->max_match_count_index >
        TEAM_MAX_MATCHES - TEAM_MIN_MATCHES) {
        g_select_team->max_match_count_index =
            TEAM_MAX_MATCHES - TEAM_MIN_MATCHES;
    }
    /* The rules start at three matches of two points, the longer time. */
    {
        TeamSelect *team = g_select_team;
        SET_PTR(team->settings, settings);
        team->match_count_index = 0;
    }
    g_select_team->win_target_index = 1;
    g_select_team->time_sheet =
        g_select_team_time_sheets[g_select_team->win_target_index][1];
    g_select_team_time_choice = 1;
    return 1;
}

/* Runs a frame of each side's step: its pad, then its page. Returns what
 * select_UpdateTeam does. */
static s32 select_RunTeamSteps(s32 pressed, MenuDrawCtx *ctx) {
    s32 result;
    s16 i;

    result = PAGE_RUNNING;
    /* both sides ready */
    if (g_select_team->step.word == (TEAM_STEP_READY << 16 | TEAM_STEP_READY)) {
        g_select_team->step.half[1] = TEAM_STEP_FADE;
        g_select_team->step.half[0] = TEAM_STEP_FADE;
        g_select_team->fade = 0;
    }
    select_DrawTeamFrame(ctx, g_select_team->step.half[0]);
    if (g_select_team->step.half[0] != TEAM_STEP_FADE) {
        select_StepTeamPanels();
    }

    for (i = 0; i < 2; i++) {
        switch (g_select_team->step.half[i]) {
        case TEAM_STEP_RULES:
            if (i == 0) {
                result = select_HandleTeamRules(pressed);
                select_DrawTeamRules(ctx);
            }
            break;
        case TEAM_STEP_CLAN:
            select_HandleTeamClans(pressed, i);
            select_DrawTeamHeading(ctx, i, (g_select_team->step.half[i] - 1));
            select_DrawTeamClans(ctx, i);
            select_DrawTeamPanel(ctx, i);
            break;
        case TEAM_STEP_FIGHTERS:
            select_HandleTeamFighters(
                pressed, g_select_team->clan[i], g_select_team->fighters[i], i);
            select_DrawTeamHeading(ctx, i, (g_select_team->step.half[i] - 1));
            select_DrawTeamBars(ctx, i, 0);
            select_DrawTeamFighters(
                ctx, g_select_team->clan[i], g_select_team->fighters[i], i);
            select_DrawTeamPanel(ctx, i);
            break;
        case TEAM_STEP_WEAPONS:
            select_HandleTeamWeapons(
                pressed, g_select_team->clan[i], g_select_team->fighters[i], i);
            select_DrawTeamHeading(ctx, i, (g_select_team->step.half[i] - 1));
            select_DrawTeamBars(ctx, i, 1);
            select_DrawTeamWeapons(
                ctx, g_select_team->clan[i], g_select_team->fighters[i], i);
            select_DrawTeamPanel(ctx, i);
            break;
        case TEAM_STEP_READY:
            /* The triangle button lets the last weapon be picked again. */
            select_DrawTeamHeading(ctx, i, TEAM_STEP_WEAPONS - 1);
            select_DrawTeamBars(ctx, i, 1);
            select_DrawTeamWeapons(
                ctx, g_select_team->clan[i], g_select_team->fighters[i], i);
            select_DrawTeamPanel(ctx, i);
            if (pressed & PAD_OF(PADRup, i)) {
                snd_Play(MENU_SOUND_CANCEL, SND_VOLUME_MAX, SND_VOLUME_MAX);
                g_select_team->step.half[i] = TEAM_STEP_WEAPONS;
            }
            break;
        case TEAM_STEP_FADE:
            select_DrawTeamHeading(ctx, i, TEAM_STEP_WEAPONS - 1);
            select_DrawTeamBars(ctx, i, 1);
            select_DrawTeamWeapons(
                ctx, g_select_team->clan[i], g_select_team->fighters[i], i);
            result = select_FadeTeamOut(ctx);
            select_DrawTeamPanel(ctx, i);
            break;
        }
    }
    return result;
}

/* A frame of the screen: starts the frame's pool and runs the steps.
 * Returns 1 when both teams are picked, -1 when the screen is left, 0
 * otherwise. `held` is not used. */
s32 select_UpdateTeam(s32 held, s32 pressed) {
    MenuDrawCtx ctx;
    PrimPool *pool;
    s32 pool_frame;
    SET_PTR(g_select_team->db, &g_disp_db[g_disp_frame_count & 1]);
    pool_frame = g_select_team->pool_frame + 1;
    g_select_team->pulse_frames = g_select_team->pulse_frames + 1;
    g_select_team->pool_frame = pool_frame;
    pool = select_GetTeamPrimPool(pool_frame & 1);
    SET_PTR(g_select_team->pool, pool);
    ctx.file.team_select = PTR(g_select_team->file);
    ctx.prims.ft4 = pool->ft4;
    ctx.prims.f4 = PTR(pool->f4);
    ctx.prims.g4 = PTR(pool->g4);
    ctx.prims.sprt = pool->sprt;
    ctx.prims.tile = PTR(pool->tile);
    ctx.prims.mode = PTR(pool->mode);
    ctx.prims.area = PTR(pool->area);
    ctx.prims.offset = PTR(pool->offset);
    ctx.tail = PTR(pool->tail_region);
    return select_RunTeamSteps(pressed, &ctx);
}

/* The work area, in small data so that this file reaches it through the
 * global pointer. */
TeamSelect *g_select_team;
