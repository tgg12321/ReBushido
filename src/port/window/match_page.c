/* The native port's window: the match rules laid over the game's picture
 * (window.h; include/port_match.h; docs/PLAYING.md, "Match rules"). On
 * the select screen, before the first grid, the rule's page; while a
 * crew is picked, a strip of the crews at the foot. Over a fight of a
 * match, light: each side's lives (Stock), wins (First to N) or crew's
 * fighters left (Crew) as small pips by its label, where the game's own
 * count of wins stands. After a fight of a match, in place of the game's
 * menu, a card: the fight's outcome and the score (the crews in full, and
 * the wounds a winner takes on), or the match's winner. Drawn
 * at the window's own resolution in the port's typeface, as the stage
 * pages' panel is (stage_page.c), none of it in the game's video memory;
 * everything it shows it reads of the game's state (src/port/match.c),
 * which the game's step alone writes. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "menu.h"
#include "port_match.h"
#include "port_duo.h"
#include "port_weapons.h"

/* The game's screen, whose places these are given in: 640 by 240, shown
 * 4:3, so that a unit across is half a unit down. */
#define GAME_W 640
#define GAME_H 240
/* versus the CPU (FightMode's FIGHT_MODE_VSCPU), whose second side is the
 * CPU */
#define MODE_VSCPU 1

/* The colours, 0xAARRGGBB, the stage pages' own. */
#define COLOR_PANEL 0xE80E121EU
#define COLOR_STRIP 0xB00E121EU
#define COLOR_EDGE 0xFF6A5530U
#define COLOR_GOLD 0xFFE6C877U
#define COLOR_TEXT 0xFFEDEDF2U
#define COLOR_DIM 0xFF8E96AAU
#define COLOR_LOST 0xFF4A5268U
/* a Crew's fallen fighter, and the line through his name */
#define COLOR_OUT 0xFF6A7288U
#define COLOR_STRUCK 0xFFB04A40U
/* each side's colour: the first's warm, the second's cool */
#define COLOR_SIDE_1 0xFFE8A05AU
#define COLOR_SIDE_2 0xFF7AB4F0U

/* The card after a fight: in the middle of the screen. */
#define CARD_X 150
#define CARD_Y 66
#define CARD_W 340
#define CARD_H 100
#define CARD_TITLE 87
#define CARD_SCORE 129
#define CARD_NAMES 110
#define CARD_PIPS 145
#define CARD_LINE 160
#define CARD_PIP 16
#define CARD_PIP_R 3
#define CARD_SIDE 90 /* each side's name and score from the middle */

#define SIZE_HUD 9
#define SIZE_TITLE 14
#define SIZE_SCORE 22
#define SIZE_NAME 10
#define SIZE_LINE 10

/* What a look is drawn into: its pixels, where they go in the area, and
 * what they were drawn for. */
typedef struct {
    s32 drawn;
    s32 area_w;
    s32 area_h;
    s32 key; /* what the pixels were drawn for (port_MatchKey) */
    u32 *pixels;
    u32 room;
    PortRect box;
    s32 font_ok;
} MatchCanvas;

/* The match's own looks; two on two's tags and band have canvases of
 * their own (below), drawn by the same hand through `g_canvas`. */
static MatchCanvas g_match_page;
static MatchCanvas *g_canvas = &g_match_page;

void port_MatchRuleName(char *out, s32 room, s32 rule, s32 count) {
    if (rule == PORT_MATCH_STOCK) {
        snprintf(out, (size_t)room, "Stock %d", (int)count);
    } else if (rule == PORT_MATCH_FIRST_TO) {
        snprintf(out, (size_t)room, "First to %d", (int)count);
    } else if (rule == PORT_MATCH_CREW) {
        snprintf(out, (size_t)room, "Crew %d", (int)count);
    } else {
        snprintf(out, (size_t)room, "Single fight");
    }
}

/* ---- places and shapes ---- */

static s32 port_MatchX(s32 x) {
    return x * g_canvas->area_w / GAME_W - g_canvas->box.x;
}

static s32 port_MatchY(s32 y) {
    return y * g_canvas->area_h / GAME_H - g_canvas->box.y;
}

static s32 port_MatchSize(s32 lines) {
    s32 size = lines * g_canvas->area_h / GAME_H;

    return size < 6 ? 6 : size;
}

static void port_MatchRect(s32 x, s32 y, s32 w, s32 h, u32 color) {
    s32 x0 = port_MatchX(x);
    s32 y0 = port_MatchY(y);

    port_FillRect(g_canvas->pixels, g_canvas->box.w, g_canvas->box.h, x0, y0,
                  port_MatchX(x + w) - x0, port_MatchY(y + h) - y0, color);
}

static void port_MatchFrame(s32 x, s32 y, s32 w, s32 h, u32 color) {
    s32 x0 = port_MatchX(x);
    s32 y0 = port_MatchY(y);
    s32 x1 = port_MatchX(x + w);
    s32 y1 = port_MatchY(y + h);
    s32 line = g_canvas->area_h >= 960 ? 2 : 1;
    u32 *p = g_canvas->pixels;
    s32 bw = g_canvas->box.w;
    s32 bh = g_canvas->box.h;

    port_FillRect(p, bw, bh, x0, y0, x1 - x0, line, color);
    port_FillRect(p, bw, bh, x0, y1 - line, x1 - x0, line, color);
    port_FillRect(p, bw, bh, x0, y0, line, y1 - y0, color);
    port_FillRect(p, bw, bh, x1 - line, y0, line, y1 - y0, color);
}

static void port_MatchStyle(
    PortTextStyle *style, s32 lines, s32 face, u32 color) {
    memset(style, 0, sizeof(*style));
    style->face = face;
    style->size = port_MatchSize(lines);
    style->color = color;
    style->shadow = 0xC0000000U;
    style->shadow_px = style->size >= 24 ? 2 : 1;
}

/* A text centred on `x` (or from it, or ending at it), its baseline at
 * `y`, in the game's places. */
static void port_MatchText(
    s32 x, s32 y, s32 lines, s32 face, const char *text, u32 color, s32 align) {
    PortTextStyle style;
    s32 px = port_MatchX(x);

    port_MatchStyle(&style, lines, face, color);
    if (align == 0) {
        px -= port_TextMeasure(text, &style) / 2;
    } else if (align > 0) {
        px -= port_TextMeasure(text, &style);
    }
    port_TextDraw(g_canvas->pixels, g_canvas->box.w, g_canvas->box.h, px,
                  port_MatchY(y), text, &style);
}

/* A pip round (x, y) of the game's places, `r` lines out: filled, or a
 * ring. Its edge smoothed by how much of each pixel it covers. */
static void port_MatchPip(s32 x, s32 y, s32 r, u32 color, s32 is_filled) {
    s32 cx = port_MatchX(x);
    s32 cy = port_MatchY(y);
    float radius = (float)(r * g_canvas->area_h / GAME_H);
    float ring = radius / 3.0f < 1.0f ? 1.0f : radius / 3.0f;
    s32 reach = (s32)radius + 2;
    s32 i;
    s32 j;

    for (j = -reach; j <= reach; j++) {
        for (i = -reach; i <= reach; i++) {
            float fx = (float)i + 0.5f;
            float fy = (float)j + 0.5f;
            float d2 = fx * fx + fy * fy;
            float d = d2;
            float cover;
            s32 k;

            /* a square root, as stage_page.c takes its own */
            for (k = 0, d = d2 > 1.0f ? d2 : 1.0f; k < 12; k++) {
                d = 0.5f * (d + d2 / d);
            }
            cover = radius + 0.5f - d;
            if (!is_filled) {
                float inner = d - (radius - ring) + 0.5f;

                cover = cover < inner ? cover : inner;
            }
            if (cover <= 0.0f) {
                continue;
            }
            if (cover > 1.0f) {
                cover = 1.0f;
            }
            port_FillRect(g_canvas->pixels, g_canvas->box.w, g_canvas->box.h,
                          cx + i, cy + j, 1, 1,
                          (u32)(cover * (float)(color >> 24)) << 24 |
                              (color & 0xFFFFFFU));
        }
    }
}

/* A side's pips from `x` towards `step`'s way: `filled` of `count`. */
static void port_MatchPips(
    s32 x, s32 y, s32 step, s32 r, s32 count, s32 filled, u32 color) {
    s32 i;

    for (i = 0; i < count; i++) {
        port_MatchPip(
            x + i * step, y, r, i < filled ? color : COLOR_LOST, i < filled);
    }
}

/* ---- the two looks ---- */

/* What each side has to show: its lives, or its wins. */
static s32 port_MatchHas(const PortMatch *match, s32 side) {
    return match->rule == PORT_MATCH_STOCK || match->rule == PORT_MATCH_CREW
               ? match->lives[side]
               : match->wins[side];
}

/* Over a fight of a match, light (the owner, 2026-11-20: "keep the gui
 * very light for actually in battle"): each side's lives, wins or crew's
 * fighters left as small pips by its label, where the game's own count
 * of wins stands (hud.c, COUNTER_LEFT_X and COUNTER_RIGHT_X), no names
 * and no rule; under the game's count of opponents versus the CPU. The
 * cards between fights say the rest. */
#define LIGHT_Y 24
#define LIGHT_Y_CPU 44
#define LIGHT_H 11
#define LIGHT_LEFT 72
#define LIGHT_RIGHT 568
#define LIGHT_LABEL 18 /* the label's room before the pips */
#define LIGHT_PIP 9
#define LIGHT_PIP_R 2
#define LIGHT_AREA_X (LIGHT_LEFT - 4)
#define LIGHT_AREA_W (LIGHT_RIGHT - LIGHT_LEFT + 8)
#define COLOR_LIGHT 0x700E121EU

static s32 port_MatchLightY(const PortMatch *match) {
    return match->mode == MODE_VSCPU ? LIGHT_Y_CPU : LIGHT_Y;
}

static void port_MatchLightHud(const PortMatch *match) {
    s32 top = port_MatchLightY(match);
    s32 count = match->count;
    s32 step = count > 8 ? LIGHT_PIP * 8 / count : LIGHT_PIP;
    s32 w = LIGHT_LABEL + count * step + 2;
    s32 mid = top + LIGHT_H / 2;

    /* a faint band under each side's, that they read over any stage */
    port_MatchRect(LIGHT_LEFT - 3, top, w, LIGHT_H, COLOR_LIGHT);
    port_MatchRect(LIGHT_RIGHT + 3 - w, top, w, LIGHT_H, COLOR_LIGHT);
    port_MatchText(LIGHT_LEFT, top + LIGHT_H - 2, SIZE_HUD - 1, PORT_FACE_BOLD,
                   "1P", COLOR_SIDE_1, -1);
    port_MatchText(LIGHT_RIGHT, top + LIGHT_H - 2, SIZE_HUD - 1, PORT_FACE_BOLD,
                   match->mode == MODE_VSCPU ? "CPU" : "2P", COLOR_SIDE_2, 1);
    port_MatchPips(LIGHT_LEFT + LIGHT_LABEL + step / 2 - 2, mid, step,
                   LIGHT_PIP_R, count, port_MatchHas(match, 0), COLOR_SIDE_1);
    port_MatchPips(
        LIGHT_RIGHT - LIGHT_LABEL - step / 2 + 2 -
            (match->mode == MODE_VSCPU ? 6 : 0),
        mid, -step, LIGHT_PIP_R, count, port_MatchHas(match, 1), COLOR_SIDE_2);
}

/* The wounds a fight's winner takes into the next, in a few words, or
 * nothing ("" when he is unhurt). */
static void port_MatchWoundWords(
    char *out, s32 room, const PortMatch *match, s32 side) {
    const s8 *wounds = match->wounds[side];
    const char *parts[PORT_WOUNDS];
    s32 n = 0;
    s32 i;

    out[0] = 0;
    if (!match->wounds_kept[side]) {
        return;
    }
    if (wounds[PORT_WOUND_LEGS] > 0) {
        parts[n++] = "leg";
    }
    if (wounds[PORT_WOUND_ARM] > 0) {
        parts[n++] = "sword arm";
    }
    if (!wounds[PORT_WOUND_OFF_ARM_OK]) {
        parts[n++] = "off arm";
    }
    if (wounds[PORT_WOUND_BODY] > 0) {
        parts[n++] = "body";
    }
    for (i = 0; i < n; i++) {
        size_t used = strlen(out);

        snprintf(out + used, (size_t)room - used, "%s%s", i == 0 ? "" : ", ",
                 parts[i]);
    }
}

static void port_MatchCard(const PortMatch *match) {
    char rule[24];
    char score[8];
    char line[64];
    s32 is_cpu = match->mode == MODE_VSCPU;
    const char *names[2] = {"Player 1", is_cpu ? "CPU" : "Player 2"};
    s32 count = match->count;
    s32 step = count > 5 ? CARD_PIP * 4 / count + 5 : CARD_PIP;
    s32 side;

    port_MatchRect(CARD_X, CARD_Y, CARD_W, CARD_H, COLOR_PANEL);
    port_MatchFrame(CARD_X, CARD_Y, CARD_W, CARD_H, COLOR_EDGE);
    port_MatchRuleName(rule, sizeof(rule), match->rule, count);
    if (match->card == PORT_MATCH_CARD_RESULT) {
        snprintf(line, sizeof(line), "%s: the match", rule);
    } else {
        snprintf(line, sizeof(line), "%s: fight %d", rule, (int)match->fights);
    }
    port_MatchText(GAME_W / 2, CARD_TITLE, SIZE_TITLE, PORT_FACE_TITLE, line,
                   COLOR_GOLD, 0);
    port_MatchRect(CARD_X + 20, CARD_TITLE + 5, CARD_W - 40, 1, COLOR_EDGE);
    for (side = 0; side < 2; side++) {
        s32 x = GAME_W / 2 + (side == 0 ? -CARD_SIDE : CARD_SIDE);
        u32 color = side == 0 ? COLOR_SIDE_1 : COLOR_SIDE_2;

        port_MatchText(
            x, CARD_NAMES, SIZE_NAME, PORT_FACE_BOLD, names[side], color, 0);
        snprintf(score, sizeof(score), "%d", (int)port_MatchHas(match, side));
        port_MatchText(x, CARD_SCORE + 3, SIZE_SCORE, PORT_FACE_TITLE, score,
                       COLOR_TEXT, 0);
        port_MatchPips(x - (count - 1) * step / 2, CARD_PIPS, step, CARD_PIP_R,
                       count, port_MatchHas(match, side), color);
    }
    port_MatchText(
        GAME_W / 2, CARD_SCORE, SIZE_NAME, PORT_FACE_TEXT,
        match->rule == PORT_MATCH_STOCK ? "lives" : "wins", COLOR_DIM, 0);
    if (match->card == PORT_MATCH_CARD_RESULT) {
        snprintf(
            line, sizeof(line), "%s wins the match", names[match->winner != 0]);
        port_MatchText(
            GAME_W / 2, CARD_LINE, SIZE_LINE + 2, PORT_FACE_BOLD, line,
            match->winner == 0 ? COLOR_SIDE_1 : COLOR_SIDE_2, 0);
    } else if (match->last == PORT_MATCH_DRAW) {
        port_MatchText(GAME_W / 2, CARD_LINE, SIZE_LINE, PORT_FACE_TEXT,
                       match->rule == PORT_MATCH_STOCK
                           ? "Both fell: a draw, a life from each"
                           : "Both fell: a draw, no point",
                       COLOR_TEXT, 0);
    } else {
        char wounds[48];

        port_MatchWoundWords(
            wounds, sizeof(wounds), match, match->last == PORT_MATCH_WON_P2);
        if (wounds[0] != 0) {
            snprintf(line, sizeof(line), "%s takes the fight, wounded: %s",
                     names[match->last == PORT_MATCH_WON_P2], wounds);
        } else {
            snprintf(line, sizeof(line), "%s takes the fight. Next fight...",
                     names[match->last == PORT_MATCH_WON_P2]);
        }
        port_MatchText(GAME_W / 2, CARD_LINE, SIZE_LINE, PORT_FACE_TEXT, line,
                       COLOR_TEXT, 0);
    }
}

/* ---- Crew: the crews' names ---- */

/* A text cut to `width` of the game's places, a dot where it was cut. */
static void port_MatchFit(
    char *out, s32 room, const char *text, s32 lines, s32 face, s32 width) {
    PortTextStyle style;
    s32 most = port_MatchX(width) - port_MatchX(0);
    s32 length;

    port_MatchStyle(&style, lines, face, 0);
    snprintf(out, (size_t)room, "%s", text);
    length = (s32)strlen(out);
    while (length > 1 && port_TextMeasure(out, &style) > most) {
        length--;
        out[length - 1] = '.';
        out[length] = 0;
    }
}

/* A place of a crew and how it stands: out (its fighter has fallen), in
 * play, or waiting. */
enum { CREW_OUT, CREW_IN_PLAY, CREW_WAITING };

static s32 port_MatchCrewStand(const PortMatch *match, s32 side, s32 place) {
    s32 lost = match->count - match->lives[side];

    return place < lost    ? CREW_OUT
           : place == lost ? CREW_IN_PLAY
                           : CREW_WAITING;
}

/* A crew's place as a name in a slot `width` wide: left from `x`, or
 * right to it (`align` 1); a fallen one dimmed and struck through, the
 * one in play in the side's colour. */
static void port_MatchCrewSlot(const PortMatch *match, s32 side, s32 place,
                               s32 x, s32 y, s32 width, s32 lines, s32 align) {
    char name[24];
    s32 stand = port_MatchCrewStand(match, side, place);
    u32 side_color = side == 0 ? COLOR_SIDE_1 : COLOR_SIDE_2;
    u32 color = stand == CREW_OUT       ? COLOR_OUT
                : stand == CREW_IN_PLAY ? side_color
                                        : COLOR_TEXT;
    s32 face = stand == CREW_IN_PLAY ? PORT_FACE_BOLD : PORT_FACE_TEXT;
    PortTextStyle style;
    s32 w;

    port_MatchFit(name, sizeof(name),
                  port_MatchCrewName(match->crew[side][place].fighter), lines,
                  face, width);
    port_MatchText(x, y, lines, face, name, color, align < 0 ? -1 : align);
    if (stand == CREW_OUT) {
        port_MatchStyle(&style, lines, face, color);
        w = port_TextMeasure(name, &style) * GAME_W / g_canvas->area_w;
        port_MatchRect(
            align > 0 ? x - w : x, y - (lines + 2) / 4, w, 1, COLOR_STRUCK);
    }
}

/* The strip over the select screen's foot and the pick's rows: names to
 * a row, two rows for a crew of more than five. */
#define CREW_HUD_X 16
#define CREW_HUD_W 608
#define CREW_HUD_SIDE 280 /* each side's names, from its end inwards */
#define CREW_SIZE_NAME 7

static s32 port_MatchCrewRows(s32 count) { return count > 5 ? 2 : 1; }

/* The card after a fight of a Crew match: each crew as a column of its
 * places, fallen, in play or waiting, and what the fight came to. */
#define CREW_CARD_X 120
#define CREW_CARD_W 400
#define CREW_CARD_Y 28
#define CREW_CARD_TITLE 21 /* from the card's top */
#define CREW_CARD_NAMES 38
#define CREW_CARD_FIRST 52
#define CREW_CARD_ROW 12
#define CREW_CARD_SIDE 100 /* each column's middle from the screen's */

static s32 port_MatchCrewCardH(const PortMatch *match) {
    return CREW_CARD_FIRST + (match->count - 1) * CREW_CARD_ROW + 42;
}

static void port_MatchCrewCard(const PortMatch *match) {
    char rule[24];
    char line[96];
    char name[48];
    s32 is_cpu = match->mode == MODE_VSCPU;
    const char *names[2] = {"Player 1", is_cpu ? "CPU" : "Player 2"};
    s32 h = port_MatchCrewCardH(match);
    s32 bottom = CREW_CARD_Y + h - 10;
    s32 side;
    s32 place;

    port_MatchRect(CREW_CARD_X, CREW_CARD_Y, CREW_CARD_W, h, COLOR_PANEL);
    port_MatchFrame(CREW_CARD_X, CREW_CARD_Y, CREW_CARD_W, h, COLOR_EDGE);
    port_MatchRuleName(rule, sizeof(rule), match->rule, match->count);
    if (match->card == PORT_MATCH_CARD_RESULT) {
        snprintf(line, sizeof(line), "%s: the match", rule);
    } else {
        snprintf(line, sizeof(line), "%s: fight %d", rule, (int)match->fights);
    }
    port_MatchText(GAME_W / 2, CREW_CARD_Y + CREW_CARD_TITLE, SIZE_TITLE,
                   PORT_FACE_TITLE, line, COLOR_GOLD, 0);
    port_MatchRect(CREW_CARD_X + 20, CREW_CARD_Y + CREW_CARD_TITLE + 5,
                   CREW_CARD_W - 40, 1, COLOR_EDGE);
    for (side = 0; side < 2; side++) {
        s32 x = GAME_W / 2 + (side == 0 ? -CREW_CARD_SIDE : CREW_CARD_SIDE);
        u32 color = side == 0 ? COLOR_SIDE_1 : COLOR_SIDE_2;

        snprintf(line, sizeof(line), "%s  %d left", names[side],
                 (int)match->lives[side]);
        port_MatchText(x, CREW_CARD_Y + CREW_CARD_NAMES, SIZE_NAME,
                       PORT_FACE_BOLD, line, color, 0);
        for (place = 0; place < match->count; place++) {
            s32 y = CREW_CARD_Y + CREW_CARD_FIRST + place * CREW_CARD_ROW;
            s32 stand = port_MatchCrewStand(match, side, place);

            port_MatchCrewSlot(
                match, side, place, x - 80, y, 90, SIZE_LINE, -1);
            port_MatchFit(name, sizeof(name),
                          port_NameWeapon(match->crew[side][place].weapon),
                          SIZE_LINE - 1, PORT_FACE_TEXT, 60);
            port_MatchText(x + 80, y, SIZE_LINE - 1, PORT_FACE_TEXT, name,
                           stand == CREW_OUT ? COLOR_LOST : COLOR_DIM, 1);
        }
    }
    if (match->card == PORT_MATCH_CARD_RESULT) {
        if (match->winner == PORT_MATCH_DRAWN) {
            snprintf(line, sizeof(line), "Both crews are out: a drawn match");
        } else {
            snprintf(line, sizeof(line), "%s's crew wins the match",
                     names[match->winner != 0]);
        }
        port_MatchText(GAME_W / 2, bottom, SIZE_LINE + 2, PORT_FACE_BOLD, line,
                       match->winner == PORT_MATCH_DRAWN ? COLOR_TEXT
                       : match->winner == 0              ? COLOR_SIDE_1
                                                         : COLOR_SIDE_2,
                       0);
        return;
    }
    if (match->last == PORT_MATCH_DRAW) {
        snprintf(line, sizeof(line), "Both fell: a fighter from each crew");
    } else {
        s32 loser = match->last == PORT_MATCH_WON_P1;
        s32 next = match->count - match->lives[loser];

        char wounds[48];

        snprintf(
            line, sizeof(line), "%s takes the fight. %s comes in",
            names[!loser],
            port_MatchCrewName(
                match->crew[loser][next < match->count ? next : 0].fighter));
        port_MatchWoundWords(wounds, sizeof(wounds), match, !loser);
        if (wounds[0] != 0) {
            char stays[96];

            snprintf(
                stays, sizeof(stays), "%s stays, wounded: %s",
                port_MatchCrewName(
                    match->crew[!loser][match->count - match->lives[!loser]]
                        .fighter),
                wounds);
            port_MatchText(GAME_W / 2, bottom - 12, SIZE_LINE - 1,
                           PORT_FACE_TEXT, stays, COLOR_STRUCK, 0);
        }
    }
    port_MatchText(
        GAME_W / 2, bottom, SIZE_LINE, PORT_FACE_TEXT, line, COLOR_TEXT, 0);
}

/* The strip over the select screen while a Crew match's later places are
 * picked: each crew so far, the place being picked marked. */
#define CREW_PICK_Y 202
#define CREW_PICK_H 36
/* And over the grid, a name a crew already has, greyed and tagged with
 * its side: the grid's names (select.c, GRID_COL_X, GRID_ROW_PITCH and
 * PANEL_Y) stand from these places, measured on the screen. */
#define PICK_AREA_Y 120
#define GRID_NAME_X 98
#define GRID_NAME_Y 128
#define GRID_NAME_W 80
#define GRID_NAME_H 12
#define GRID_NAME_PITCH 16
#define GRID_NAME_COLS 4
#define GRID_NAME_ROWS 5
#define COLOR_GREYED 0xC8D6CFC2U

static s32 port_MatchGridX(s32 col) { return col * 116 + (col >> 1) * 20; }

static void port_MatchCrewGrid(void) {
    s32 row;
    s32 col;

    for (row = 0; row < GRID_NAME_ROWS; row++) {
        for (col = 0; col < GRID_NAME_COLS; col++) {
            s32 marks = port_MatchCrewMarks(row, col);
            s32 x = GRID_NAME_X + port_MatchGridX(col);
            s32 y = GRID_NAME_Y + row * GRID_NAME_PITCH;
            s32 side;

            if (marks == 0) {
                continue;
            }
            port_MatchRect(x, y, GRID_NAME_W, GRID_NAME_H, COLOR_GREYED);
            for (side = 0; side < 2; side++) {
                if (marks & (1 << side)) {
                    port_MatchText(
                        x + GRID_NAME_W - 14 + side * 12, y + GRID_NAME_H - 2,
                        SIZE_HUD - 2, PORT_FACE_BOLD, side == 0 ? "1P" : "2P",
                        side == 0 ? COLOR_SIDE_1 : COLOR_SIDE_2, 1);
                }
            }
        }
    }
}

static void port_MatchCrewPick(const PortMatch *match) {
    char line[48];
    char name[32];
    s32 count = match->crew_size;
    s32 rows = port_MatchCrewRows(count);
    s32 per_row = count > 0 ? (count + rows - 1) / rows : 1;
    s32 slot = CREW_HUD_SIDE / per_row;
    s32 right = CREW_HUD_X + CREW_HUD_W - 6;
    s32 side;
    s32 place;

    port_MatchCrewGrid();
    port_MatchRect(
        CREW_HUD_X, CREW_PICK_Y, CREW_HUD_W, CREW_PICK_H, COLOR_PANEL);
    port_MatchFrame(
        CREW_HUD_X, CREW_PICK_Y, CREW_HUD_W, CREW_PICK_H, COLOR_EDGE);
    /* two on two's teams (port_duo.h): each side's first, then his
     * partner, whom the third and fourth pads pick where they play */
    s32 teams = match->page_rule != PORT_MATCH_CREW;
    s32 pads = teams && match->page_teams == PORT_TEAMS_TWO_PADS &&
               match->crew_picked > 0;

    if (teams) {
        snprintf(
            line, sizeof(line), "Two on two: %s",
            match->crew_picked == 0 ? "pick the fighter" : "pick the partner");
    } else {
        snprintf(line, sizeof(line), "Crew %d: pick %d of %d", (int)count,
                 (int)match->crew_picked + 1, (int)count);
    }
    port_MatchText(GAME_W / 2, CREW_PICK_Y + 11, SIZE_HUD, PORT_FACE_BOLD, line,
                   COLOR_GOLD, 0);
    port_MatchText(CREW_HUD_X + 6, CREW_PICK_Y + 11, SIZE_HUD - 1,
                   PORT_FACE_BOLD, pads ? "3P" : "1P", COLOR_SIDE_1, -1);
    port_MatchText(
        right, CREW_PICK_Y + 11, SIZE_HUD - 1, PORT_FACE_BOLD,
        match->crew_sides > 1 ? (pads ? "4P" : "2P") : "CPU", COLOR_SIDE_2, 1);
    if (match->crew_sides < 2) {
        port_MatchText(
            right - 30, CREW_PICK_Y + 11, SIZE_HUD - 1, PORT_FACE_TEXT,
            teams ? "its partner drawn at random"
                  : "drawn at random, led by the opponent",
            COLOR_DIM, 1);
    }
    for (side = 0; side < match->crew_sides && side < 2; side++) {
        u32 color = side == 0 ? COLOR_SIDE_1 : COLOR_SIDE_2;
        s32 align = side == 0 ? -1 : 1;

        for (place = 0; place < count; place++) {
            s32 col = place % per_row;
            s32 x =
                side == 0 ? CREW_HUD_X + 6 + col * slot : right - col * slot;
            /* a row of names, the weapons under them; a crew of more
             * than five two rows of names alone */
            s32 y = CREW_PICK_Y + 23 + (place / per_row) * 9;

            if (place < match->crew_picked) {
                port_MatchFit(
                    name, sizeof(name),
                    port_MatchCrewName(match->crew[side][place].fighter),
                    CREW_SIZE_NAME + 1, PORT_FACE_BOLD, slot - 4);
                port_MatchText(x, y, CREW_SIZE_NAME + 1, PORT_FACE_BOLD, name,
                               color, align);
                if (rows > 1) {
                    continue;
                }
                port_MatchFit(name, sizeof(name),
                              port_NameWeapon(match->crew[side][place].weapon),
                              CREW_SIZE_NAME, PORT_FACE_TEXT, slot - 4);
                port_MatchText(x, CREW_PICK_Y + 32, CREW_SIZE_NAME,
                               PORT_FACE_TEXT, name, COLOR_DIM, align);
            } else {
                snprintf(name, sizeof(name), "%d", (int)place + 1);
                port_MatchText(
                    x, y, CREW_SIZE_NAME + 1, PORT_FACE_BOLD, name,
                    place == match->crew_picked ? COLOR_GOLD : COLOR_LOST,
                    align);
            }
        }
    }
}

/* ---- the rule's page, before the first select ---- */

/* in the game's wide panel (menu.h, PORT_LOOK_PANEL_...): the rows'
 * names at its left and their values at its right, the first's baseline
 * and the pitch, the rule's words, the host's line and the keys under the
 * panel */
#define RULE_LABEL_X 96
#define RULE_VALUE_X 544
#define RULE_FIRST_Y 80
#define RULE_ROW 22
#define RULE_ABOUT_Y 160
#define RULE_HOST_Y 196
#define RULE_KEYS_Y 236

static const char * const g_match_rule_words[PORT_MATCH_RULES][2] = {
    {"One fight, as the original game plays it.", ""},
    {"Each side has a number of lives; a fight lost takes one.",
     "The winner keeps his wounds into the next fight."},
    {"Fights until a side has won the number.", "Every fight starts unhurt."},
    {"Each side picks a crew of different fighters: its lives.",
     "The winner stays, wounds and all; the loser's next comes in."},
};

/* The rule's page, in the look of the game's own pages (page_look.c;
 * docs/PORT.md, "The window's pages in the game's look"): the picture
 * darkened, the game's wide panel, the title over its rule, the rows in
 * the game's gold and the cursor's in its yellow, each value in white
 * between the arrows. Its presses sound as the game's menus' do, played
 * by the game (match.c, port_MatchRulePageStep). */
static void port_MatchRulePage(const PortMatch *match) {
    PortLook look;
    char value[32];
    s32 rule = match->page_rule < PORT_MATCH_RULES ? match->page_rule : 0;
    s32 shown_rows = 0;
    s32 row;

    /* the page's pixels are the whole 4:3 area (port_MatchArea) */
    memset(&look, 0, sizeof(look));
    look.pixels = g_canvas->pixels;
    look.room = g_canvas->room;
    look.area_w = g_canvas->area_w;
    look.area_h = g_canvas->area_h;
    look.font_ok = g_canvas->font_ok;
    /* twice: the select screen's lettering is bright under it */
    port_LookBox(&look, 0, 0, GAME_W, GAME_H, PORT_LOOK_DARKEN);
    port_LookBox(&look, 0, 0, GAME_W, GAME_H, PORT_LOOK_DARKEN);
    port_LookPanel(&look, TRUE);
    port_LookTitle(&look, "Battle type");
    for (row = 0; row < PORT_MATCH_ROWS; row++) {
        s32 y = RULE_FIRST_Y + shown_rows * RULE_ROW;
        s32 is_on = match->rule_row == row;
        const char *label;
        char shown[48];

        if (!port_MatchRuleHasRow(match, row)) {
            continue;
        }
        shown_rows++;
        if (row == PORT_MATCH_ROW_TEAMS) {
            static const char * const teams[PORT_TEAMS_CHOICES] = {
                "One on one", "Two on two, CPU partners",
                "Two on two, four players"};

            label = "Fighters";
            snprintf(
                value, sizeof(value), "%s",
                teams[match->page_teams < PORT_TEAMS_CHOICES ? match->page_teams
                                                             : 0]);
        } else if (row == PORT_MATCH_ROW_RULE) {
            label = "Rule";
            port_MatchRuleName(value, sizeof(value), rule, 0);
            if (rule != PORT_MATCH_SINGLE) {
                /* the name alone: the number is the next row's */
                char *space = strrchr(value, ' ');

                if (space != NULL) {
                    *space = 0;
                }
            }
        } else {
            label = rule == PORT_MATCH_STOCK      ? "Lives"
                    : rule == PORT_MATCH_FIRST_TO ? "Wins"
                                                  : "Crew size";
            snprintf(value, sizeof(value), "%d",
                     (int)(rule == PORT_MATCH_CREW ? match->page_crew
                                                   : match->page_count));
        }
        port_LookText(
            &look, RULE_LABEL_X, y, PORT_LOOK_SIZE_ROW + 1, PORT_FACE_BOLD,
            label, is_on ? PORT_LOOK_CURSOR : PORT_LOOK_ROW, PORT_LOOK_LEFT);
        snprintf(shown, sizeof(shown), is_on ? "<  %s  >" : "%s", value);
        port_LookText(&look, RULE_VALUE_X, y, PORT_LOOK_SIZE_ROW,
                      PORT_FACE_TEXT, shown, PORT_LOOK_WHITE, PORT_LOOK_RIGHT);
    }
    port_LookText(
        &look, RULE_LABEL_X, RULE_ABOUT_Y, PORT_LOOK_SIZE_TEXT, PORT_FACE_TEXT,
        g_match_rule_words[rule][0], PORT_LOOK_WHITE, PORT_LOOK_LEFT);
    port_LookText(&look, RULE_LABEL_X, RULE_ABOUT_Y + 12, PORT_LOOK_SIZE_TEXT,
                  PORT_FACE_TEXT,
                  match->page_teams != PORT_TEAMS_ONE && rule != PORT_MATCH_CREW
                      ? "Two on two: each side two of its fighter, all unhurt."
                      : g_match_rule_words[rule][1],
                  PORT_LOOK_WHITE, PORT_LOOK_LEFT);
    if (match->page_teams != PORT_TEAMS_ONE && rule != PORT_MATCH_CREW) {
        port_LookText(
            &look, RULE_LABEL_X, RULE_ABOUT_Y + 24, PORT_LOOK_SIZE_TEXT,
            PORT_FACE_TEXT, "R2 turns to the other enemy.", PORT_LOOK_WHITE,
            PORT_LOOK_LEFT);
    }
    if (port_SessionIsOn()) {
        port_LookText(&look, RULE_LABEL_X, RULE_HOST_Y, PORT_LOOK_SIZE_TEXT,
                      PORT_FACE_BOLD, "Player 1, the host, chooses",
                      COLOR_SIDE_1, PORT_LOOK_LEFT);
    }
    port_LookText(
        &look, GAME_W / 2, RULE_KEYS_Y, PORT_LOOK_SIZE_NOTE, PORT_FACE_TEXT,
        "Cross: to the Character Select    Up, Down: row    Left, Right: "
        "change    Triangle: back",
        PORT_LOOK_GREY, PORT_LOOK_CENTRE);
}

/* ---- what the window asks ---- */

/* What is shown, as one number: the look and everything it draws. */
static s32 port_MatchKey(s32 shown, const PortMatch *match) {
    s32 key = shown | match->rule << 2 | match->count << 4 |
              port_MatchHas(match, 0) << 8 | port_MatchHas(match, 1) << 12 |
              match->card << 16 | match->last << 18 | match->winner << 20 |
              (match->fights & 0x1F) << 21 | (match->mode == MODE_VSCPU) << 26;
    u32 crew = 2166136261U;
    const u8 *byte = &match->crew[0][0].fighter;
    s32 i;

    if (shown == PORT_MATCH_SHOWN_RULE) {
        return shown | match->page_rule << 3 | match->rule_row << 6 |
               match->page_count << 8 | match->page_crew << 12 |
               port_SessionIsOn() << 16 | match->page_teams << 17;
    }
    if (shown == PORT_MATCH_SHOWN_HUD) {
        return key;
    }
    for (i = 0; i < 2; i++) {
        key ^= (s32)((u32)(match->wounds_kept[i] * 0x10 |
                           match->wounds[i][PORT_WOUND_LEGS] * 0x3 |
                           match->wounds[i][PORT_WOUND_ARM] * 0x5 |
                           match->wounds[i][PORT_WOUND_BODY] * 0x7 |
                           match->wounds[i][PORT_WOUND_OFF_ARM_OK])
                     << (27 + i * 2));
    }
    if (match->rule != PORT_MATCH_CREW && shown != PORT_MATCH_SHOWN_PICK) {
        return key;
    }
    key ^= match->page_teams << 3;
    /* a Crew's names and the picks so far, folded in */
    for (i = 0; i < (s32)sizeof(match->crew); i++) {
        crew = (crew ^ byte[i]) * 16777619U;
    }
    crew = (crew ^ match->crew_picked) * 16777619U;
    crew = (crew ^ match->crew_size) * 16777619U;
    crew = (crew ^ match->crew_sides) * 16777619U;
    return key ^ (s32)(crew << 27) ^ (s32)(crew >> 5 << 28);
}

/* Where a look is, in the game's places. */
static void port_MatchArea(
    s32 shown, const PortMatch *match, s32 *x, s32 *y, s32 *w, s32 *h) {
    if (shown == PORT_MATCH_SHOWN_RULE) {
        *x = 0;
        *y = 0;
        *w = GAME_W;
        *h = GAME_H;
    } else if (shown == PORT_MATCH_SHOWN_PICK) {
        /* the strip, and the grid's names over it */
        *x = CREW_HUD_X;
        *y = PICK_AREA_Y;
        *w = CREW_HUD_W;
        *h = CREW_PICK_Y + CREW_PICK_H - PICK_AREA_Y;
    } else if (shown == PORT_MATCH_SHOWN_HUD) {
        *x = LIGHT_AREA_X;
        *y = port_MatchLightY(match);
        *w = LIGHT_AREA_W;
        *h = LIGHT_H;
    } else if (match->rule == PORT_MATCH_CREW) {
        *x = CREW_CARD_X;
        *y = CREW_CARD_Y;
        *w = CREW_CARD_W;
        *h = port_MatchCrewCardH(match);
    } else {
        *x = CARD_X;
        *y = CARD_Y;
        *w = CARD_W;
        *h = CARD_H;
    }
}

s32 port_MatchPixels(
    s32 area_w, s32 area_h, const u32 **pixels, PortRect *box) {
    const PortMatch *match = &g_port_match;
    s32 shown = port_MatchShown();
    s32 key;
    u32 need;
    s32 x;
    s32 y;
    s32 w;
    s32 h;

    if (shown == PORT_MATCH_SHOWN_NONE || area_w <= 0 || area_h <= 0) {
        return FALSE;
    }
    if (!g_canvas->font_ok) {
        g_canvas->font_ok = port_FontStart();
        if (!g_canvas->font_ok) {
            return FALSE;
        }
    }
    key = port_MatchKey(shown, match);
    if (!g_canvas->drawn || g_canvas->area_w != area_w ||
        g_canvas->area_h != area_h || g_canvas->key != key) {
        g_canvas->key = key;
        g_canvas->area_w = area_w;
        g_canvas->area_h = area_h;
        port_MatchArea(shown, match, &x, &y, &w, &h);
        g_canvas->box.x = x * area_w / GAME_W;
        g_canvas->box.y = y * area_h / GAME_H;
        g_canvas->box.w = (x + w) * area_w / GAME_W - g_canvas->box.x;
        g_canvas->box.h = (y + h) * area_h / GAME_H - g_canvas->box.y;
        need = (u32)(g_canvas->box.w * g_canvas->box.h);
        if (need > g_canvas->room) {
            free(g_canvas->pixels);
            g_canvas->pixels = malloc((size_t)need * sizeof(u32));
            g_canvas->room = g_canvas->pixels != NULL ? need : 0;
        }
        if (g_canvas->pixels == NULL || need == 0) {
            g_canvas->drawn = FALSE;
            return FALSE;
        }
        memset(g_canvas->pixels, 0, (size_t)need * sizeof(u32));
        if (shown == PORT_MATCH_SHOWN_RULE) {
            port_MatchRulePage(match);
        } else if (shown == PORT_MATCH_SHOWN_PICK) {
            port_MatchCrewPick(match);
        } else if (shown == PORT_MATCH_SHOWN_HUD) {
            port_MatchLightHud(match);
        } else if (match->rule == PORT_MATCH_CREW) {
            port_MatchCrewCard(match);
        } else {
            port_MatchCard(match);
        }
        g_canvas->drawn = TRUE;
    }
    *pixels = g_canvas->pixels;
    *box = g_canvas->box;
    return TRUE;
}

/* ---- two on two (include/port_duo.h) ----
 *
 * Over a fight of two on two, as light as the match's strip: a band
 * under the game's count of wins (or the match's strip), each team's
 * fighters by their pad's number (or CPU) with a pip that is filled
 * while he stands; and over each standing fighter's head a small tag of
 * the same, in his team's colour, with under it the numbers of the
 * players who have him as their target. The tags move with the fighters:
 * each is a small canvas of its own, drawn again only when what it says
 * changes, and laid where his head is on the screen (port_DuoShown). */
#define DUO_BAND_Y 38
#define DUO_BAND_Y_CPU 58
#define DUO_BAND_H 11
#define DUO_NAME_W 20 /* a fighter's name's room before his pip */
#define DUO_STEP 34   /* from one fighter of a team to the other */
#define DUO_BAND_W (2 * DUO_STEP + 4)
#define DUO_TAG_W 36
#define DUO_TAG_H 22
#define DUO_TAG_TEXT 9   /* the name's baseline in the tag */
#define DUO_TAG_MARKS 19 /* the targeters' numbers' */
#define DUO_TAGS PORT_DUO_FIGHTERS

static MatchCanvas g_duo_band;
static MatchCanvas g_duo_tags[DUO_TAGS];
static s32 g_duo_tag_now;

/* A fighter's name on the band and his tag: his pad's number, or CPU. */
static const char *port_DuoName(const PortDuoShown *shown, s32 who) {
    static const char * const names[PORT_DUO_FIGHTERS] = {
        "1P", "2P", "3P", "4P"};

    return shown->is_cpu[who] ? "CPU" : names[who];
}

static u32 port_DuoColor(s32 who) {
    return PORT_DUO_SLOT(who) == 0 ? COLOR_SIDE_1 : COLOR_SIDE_2;
}

/* The canvas made ready for an area and a box: its pixels cleared when
 * what it shows (`key`) or the area changed, TRUE then (it is to be
 * drawn). FALSE with nothing to draw into. */
static s32 port_DuoCanvas(MatchCanvas *canvas, s32 area_w, s32 area_h, s32 key,
                          s32 w, s32 h, s32 *fresh) {
    u32 need;

    *fresh = FALSE;
    if (!canvas->font_ok) {
        canvas->font_ok = port_FontStart();
        if (!canvas->font_ok) {
            return FALSE;
        }
    }
    if (canvas->drawn && canvas->area_w == area_w && canvas->area_h == area_h &&
        canvas->key == key) {
        return TRUE;
    }
    canvas->key = key;
    canvas->area_w = area_w;
    canvas->area_h = area_h;
    canvas->box.x = 0;
    canvas->box.y = 0;
    canvas->box.w = w * area_w / GAME_W;
    canvas->box.h = h * area_h / GAME_H;
    need = (u32)(canvas->box.w * canvas->box.h);
    if (need > canvas->room) {
        free(canvas->pixels);
        canvas->pixels = malloc((size_t)need * sizeof(u32));
        canvas->room = canvas->pixels != NULL ? need : 0;
    }
    if (canvas->pixels == NULL || need == 0) {
        canvas->drawn = FALSE;
        return FALSE;
    }
    memset(canvas->pixels, 0, (size_t)need * sizeof(u32));
    canvas->drawn = TRUE;
    *fresh = TRUE;
    return TRUE;
}

/* The band of both teams: who stands. */
static void port_DuoBand(const PortDuoShown *shown) {
    s32 side;
    s32 i;

    port_MatchRect(0, 0, DUO_BAND_W, DUO_BAND_H, COLOR_LIGHT);
    port_MatchRect(GAME_W - DUO_BAND_W, 0, DUO_BAND_W, DUO_BAND_H, COLOR_LIGHT);
    for (side = 0; side < PORT_DUO_TEAMS; side++) {
        for (i = 0; i < 2; i++) {
            s32 who = side + 2 * i;
            s32 x = side == 0 ? 3 + i * DUO_STEP
                              : GAME_W - 3 - DUO_NAME_W - 4 - i * DUO_STEP;

            port_MatchText(x, DUO_BAND_H - 2, SIZE_HUD - 1, PORT_FACE_BOLD,
                           port_DuoName(shown, who), port_DuoColor(who), -1);
            port_MatchPip(x + DUO_NAME_W + 1, DUO_BAND_H / 2, LIGHT_PIP_R,
                          shown->alive[who] ? port_DuoColor(who) : COLOR_LOST,
                          shown->alive[who]);
        }
    }
}

s32 port_DuoBandPixels(
    s32 area_w, s32 area_h, const u32 **pixels, PortRect *box) {
    PortDuoShown shown;
    s32 key = 0;
    s32 top;
    s32 fresh;
    s32 who;

    port_DuoShown(&shown);
    if (!shown.on || area_w <= 0 || area_h <= 0) {
        return FALSE;
    }
    for (who = 0; who < PORT_DUO_FIGHTERS; who++) {
        key = key * 4 + shown.alive[who] * 2 + shown.is_cpu[who];
    }
    if (!port_DuoCanvas(
            &g_duo_band, area_w, area_h, key, GAME_W, DUO_BAND_H, &fresh)) {
        return FALSE;
    }
    if (fresh) {
        g_canvas = &g_duo_band;
        port_DuoBand(&shown);
        g_canvas = &g_match_page;
    }
    top = g_port_match.mode == MODE_VSCPU ? DUO_BAND_Y_CPU : DUO_BAND_Y;
    *pixels = g_duo_band.pixels;
    *box = g_duo_band.box;
    box->y = top * area_h / GAME_H;
    return TRUE;
}

void port_DuoTagChoose(s32 who) { g_duo_tag_now = who; }

s32 port_DuoTagPixels(
    s32 area_w, s32 area_h, const u32 **pixels, PortRect *box) {
    PortDuoShown shown;
    MatchCanvas *canvas;
    s32 who = g_duo_tag_now;
    s32 key;
    s32 fresh;
    s32 other;
    s32 marks = 0;

    if (who < 0 || who >= DUO_TAGS) {
        return FALSE;
    }
    port_DuoShown(&shown);
    if (!shown.on || !shown.alive[who] || !shown.tag[who].seen || area_w <= 0 ||
        area_h <= 0) {
        return FALSE;
    }
    canvas = &g_duo_tags[who];
    /* the players who have him as their target */
    for (other = 0; other < PORT_DUO_FIGHTERS; other++) {
        if (!shown.is_cpu[other] && shown.alive[other] &&
            shown.target[other] == who) {
            marks |= 1 << other;
        }
    }
    key = marks * 2 + shown.is_cpu[who];
    if (!port_DuoCanvas(
            canvas, area_w, area_h, key, DUO_TAG_W, DUO_TAG_H, &fresh)) {
        return FALSE;
    }
    if (fresh) {
        char line[16];
        s32 used = 0;

        g_canvas = canvas;
        port_MatchRect(DUO_TAG_W / 2 - 9, 1, 18, DUO_TAG_TEXT + 1, COLOR_LIGHT);
        port_MatchText(
            DUO_TAG_W / 2, DUO_TAG_TEXT, SIZE_HUD - 1, PORT_FACE_BOLD,
            port_DuoName(&shown, who), port_DuoColor(who), 0);
        line[0] = 0;
        for (other = 0; other < PORT_DUO_FIGHTERS; other++) {
            if (marks & (1 << other)) {
                used += snprintf(line + used, sizeof(line) - (size_t)used,
                                 "%s%d", used == 0 ? "" : " ", (int)other + 1);
            }
        }
        if (used > 0) {
            port_MatchText(
                DUO_TAG_W / 2, DUO_TAG_MARKS, SIZE_HUD - 2, PORT_FACE_BOLD,
                line, port_DuoColor(PORT_DUO_SLOT(who) ^ 1), 0);
        }
        g_canvas = &g_match_page;
    }
    *pixels = canvas->pixels;
    *box = canvas->box;
    box->x = (shown.tag[who].x - DUO_TAG_W / 2) * area_w / GAME_W;
    box->y = (shown.tag[who].y - DUO_TAG_H) * area_h / GAME_H;
    /* kept in the area: a head off its edge has its tag at the edge */
    box->x = box->x < 0                        ? 0
             : box->x > area_w - canvas->box.w ? area_w - canvas->box.w
                                               : box->x;
    box->y = box->y < 0                        ? 0
             : box->y > area_h - canvas->box.h ? area_h - canvas->box.h
                                               : box->y;
    return TRUE;
}

/* ---- the mods' weapons on the weapon select (port_weapons.h) ---- */

#define PICK_W 170
#define PICK_H 16
#define PICK_Y 214
#define PICK_X_SIDE 180
#define PICK_X_FIRST 145

static MatchCanvas g_weapon_picks[PORT_WEAPON_SIDES];
static s32 g_weapon_pick_now;

void port_WeaponPickChoose(s32 side) { g_weapon_pick_now = side; }

/* A side's line under the weapon row, where the weapon under its cursor
 * has mods' weapons on it: the mod's weapon it shows (gold), or the
 * number of the mods' weapons there are to go round with L1 and R1. */
s32 port_WeaponPickPixels(
    s32 area_w, s32 area_h, const u32 **pixels, PortRect *box) {
    s32 side = g_weapon_pick_now;
    MatchCanvas *canvas;
    const char *name;
    char line[96];
    s32 count;
    s32 fresh;
    s32 key;

    if (side < 0 || side >= PORT_WEAPON_SIDES || area_w <= 0 || area_h <= 0) {
        return FALSE;
    }
    name = port_WeaponPickShown(side, &count);
    if (count == 0) {
        return FALSE;
    }
    /* what it shows: the variant's place, and whether it is picked */
    key = (s32)g_port_weapon_pick.variant[side] * 256 +
          (s32)g_port_weapon_pick.picked[side] * 2 +
          (g_port_weapon_pick.cursor_weapon[side] & 1) + count * 65536;
    canvas = &g_weapon_picks[side];
    if (!port_DuoCanvas(canvas, area_w, area_h, key, PICK_W, PICK_H, &fresh)) {
        return FALSE;
    }
    if (fresh) {
        g_canvas = canvas;
        port_MatchRect(0, 0, PICK_W, PICK_H, COLOR_STRIP);
        if (name != NULL) {
            snprintf(line, sizeof(line), "L1  %s  R1", name);
        } else {
            snprintf(line, sizeof(line), "L1 R1: %d more weapon%s", (int)count,
                     count == 1 ? "" : "s");
        }
        port_MatchText(PICK_W / 2, PICK_H - 4, SIZE_LINE, PORT_FACE_BOLD, line,
                       name != NULL ? COLOR_GOLD : COLOR_DIM, 0);
        g_canvas = &g_match_page;
    }
    *pixels = canvas->pixels;
    *box = canvas->box;
    box->x = (PICK_X_FIRST + side * PICK_X_SIDE) * area_w / GAME_W;
    box->y = PICK_Y * area_h / GAME_H;
    return TRUE;
}
