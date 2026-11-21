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

static struct {
    s32 drawn;
    s32 area_w;
    s32 area_h;
    s32 key; /* what the pixels were drawn for (port_MatchKey) */
    u32 *pixels;
    u32 room;
    PortRect box;
    s32 font_ok;
} g_match_page;

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
    return x * g_match_page.area_w / GAME_W - g_match_page.box.x;
}

static s32 port_MatchY(s32 y) {
    return y * g_match_page.area_h / GAME_H - g_match_page.box.y;
}

static s32 port_MatchSize(s32 lines) {
    s32 size = lines * g_match_page.area_h / GAME_H;

    return size < 6 ? 6 : size;
}

static void port_MatchRect(s32 x, s32 y, s32 w, s32 h, u32 color) {
    s32 x0 = port_MatchX(x);
    s32 y0 = port_MatchY(y);

    port_FillRect(
        g_match_page.pixels, g_match_page.box.w, g_match_page.box.h, x0, y0,
        port_MatchX(x + w) - x0, port_MatchY(y + h) - y0, color);
}

static void port_MatchFrame(s32 x, s32 y, s32 w, s32 h, u32 color) {
    s32 x0 = port_MatchX(x);
    s32 y0 = port_MatchY(y);
    s32 x1 = port_MatchX(x + w);
    s32 y1 = port_MatchY(y + h);
    s32 line = g_match_page.area_h >= 960 ? 2 : 1;
    u32 *p = g_match_page.pixels;
    s32 bw = g_match_page.box.w;
    s32 bh = g_match_page.box.h;

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
    port_TextDraw(g_match_page.pixels, g_match_page.box.w, g_match_page.box.h,
                  px, port_MatchY(y), text, &style);
}

/* A pip round (x, y) of the game's places, `r` lines out: filled, or a
 * ring. Its edge smoothed by how much of each pixel it covers. */
static void port_MatchPip(s32 x, s32 y, s32 r, u32 color, s32 is_filled) {
    s32 cx = port_MatchX(x);
    s32 cy = port_MatchY(y);
    float radius = (float)(r * g_match_page.area_h / GAME_H);
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
            port_FillRect(g_match_page.pixels, g_match_page.box.w,
                          g_match_page.box.h, cx + i, cy + j, 1, 1,
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
        w = port_TextMeasure(name, &style) * GAME_W / g_match_page.area_w;
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

    port_MatchRect(
        CREW_HUD_X, CREW_PICK_Y, CREW_HUD_W, CREW_PICK_H, COLOR_PANEL);
    port_MatchFrame(
        CREW_HUD_X, CREW_PICK_Y, CREW_HUD_W, CREW_PICK_H, COLOR_EDGE);
    snprintf(line, sizeof(line), "Crew %d: pick %d of %d", (int)count,
             (int)match->crew_picked + 1, (int)count);
    port_MatchText(GAME_W / 2, CREW_PICK_Y + 11, SIZE_HUD, PORT_FACE_BOLD, line,
                   COLOR_GOLD, 0);
    port_MatchText(CREW_HUD_X + 6, CREW_PICK_Y + 11, SIZE_HUD - 1,
                   PORT_FACE_BOLD, "1P", COLOR_SIDE_1, -1);
    port_MatchText(right, CREW_PICK_Y + 11, SIZE_HUD - 1, PORT_FACE_BOLD,
                   match->crew_sides > 1 ? "2P" : "CPU", COLOR_SIDE_2, 1);
    if (match->crew_sides < 2) {
        port_MatchText(
            right - 30, CREW_PICK_Y + 11, SIZE_HUD - 1, PORT_FACE_TEXT,
            "drawn at random, led by the opponent", COLOR_DIM, 1);
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

#define RULE_X 150
#define RULE_Y 40
#define RULE_W 340
#define RULE_H 156
#define RULE_TITLE 22 /* from the page's top */
#define RULE_FIRST 50 /* the first row's baseline */
#define RULE_ROW 20
#define RULE_ABOUT 100 /* the rule's words */
#define RULE_FOOT 146
#define COLOR_SHADE 0xA0000000U
#define COLOR_ROW 0x60E6C877U

static const char * const g_match_rule_words[PORT_MATCH_RULES][2] = {
    {"One fight, as the original game plays it.", ""},
    {"Each side has a number of lives; a fight lost takes one.",
     "The winner keeps his wounds into the next fight."},
    {"Fights until a side has won the number.", "Every fight starts unhurt."},
    {"Each side picks a crew of different fighters: its lives.",
     "The winner stays, wounds and all; the loser's next comes in."},
};

static void port_MatchRulePage(const PortMatch *match) {
    char value[32];
    s32 rule = match->page_rule < PORT_MATCH_RULES ? match->page_rule : 0;
    s32 rows = rule == PORT_MATCH_SINGLE ? 1 : PORT_MATCH_ROWS;
    s32 row;

    port_MatchRect(0, 0, GAME_W, GAME_H, COLOR_SHADE);
    port_MatchRect(RULE_X, RULE_Y, RULE_W, RULE_H, COLOR_PANEL);
    port_MatchFrame(RULE_X, RULE_Y, RULE_W, RULE_H, COLOR_EDGE);
    port_MatchText(GAME_W / 2, RULE_Y + RULE_TITLE, SIZE_TITLE, PORT_FACE_TITLE,
                   "Battle type", COLOR_GOLD, 0);
    port_MatchRect(
        RULE_X + 20, RULE_Y + RULE_TITLE + 5, RULE_W - 40, 1, COLOR_EDGE);
    for (row = 0; row < rows; row++) {
        s32 y = RULE_Y + RULE_FIRST + row * RULE_ROW;
        s32 is_on = match->rule_row == row;
        const char *label;

        if (row == PORT_MATCH_ROW_RULE) {
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
        if (is_on) {
            port_MatchRect(RULE_X + 16, y - 12, RULE_W - 32, 16, COLOR_ROW);
        }
        port_MatchText(RULE_X + 30, y, SIZE_NAME + 1, PORT_FACE_BOLD, label,
                       is_on ? COLOR_GOLD : COLOR_TEXT, -1);
        port_MatchText(RULE_X + RULE_W - 30, y, SIZE_NAME + 1, PORT_FACE_BOLD,
                       ">", is_on ? COLOR_GOLD : COLOR_DIM, 1);
        port_MatchText(RULE_X + RULE_W - 130, y, SIZE_NAME + 1, PORT_FACE_BOLD,
                       "<", is_on ? COLOR_GOLD : COLOR_DIM, -1);
        port_MatchText(RULE_X + RULE_W - 80, y, SIZE_NAME + 1, PORT_FACE_BOLD,
                       value, COLOR_TEXT, 0);
    }
    port_MatchText(GAME_W / 2, RULE_Y + RULE_ABOUT, SIZE_LINE - 1,
                   PORT_FACE_TEXT, g_match_rule_words[rule][0], COLOR_DIM, 0);
    port_MatchText(GAME_W / 2, RULE_Y + RULE_ABOUT + 12, SIZE_LINE - 1,
                   PORT_FACE_TEXT, g_match_rule_words[rule][1], COLOR_DIM, 0);
    if (port_SessionIsOn()) {
        port_MatchText(
            GAME_W / 2, RULE_Y + RULE_FOOT - 16, SIZE_LINE - 1, PORT_FACE_BOLD,
            "Player 1, the host, chooses", COLOR_SIDE_1, 0);
    }
    port_MatchText(
        GAME_W / 2, RULE_Y + RULE_FOOT, SIZE_LINE - 1, PORT_FACE_TEXT,
        "Cross: to the Character Select    Triangle: back", COLOR_TEXT, 0);
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
               port_SessionIsOn() << 16;
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
        *x = CREW_HUD_X;
        *y = CREW_PICK_Y;
        *w = CREW_HUD_W;
        *h = CREW_PICK_H;
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
    if (!g_match_page.font_ok) {
        g_match_page.font_ok = port_FontStart();
        if (!g_match_page.font_ok) {
            return FALSE;
        }
    }
    key = port_MatchKey(shown, match);
    if (!g_match_page.drawn || g_match_page.area_w != area_w ||
        g_match_page.area_h != area_h || g_match_page.key != key) {
        g_match_page.key = key;
        g_match_page.area_w = area_w;
        g_match_page.area_h = area_h;
        port_MatchArea(shown, match, &x, &y, &w, &h);
        g_match_page.box.x = x * area_w / GAME_W;
        g_match_page.box.y = y * area_h / GAME_H;
        g_match_page.box.w = (x + w) * area_w / GAME_W - g_match_page.box.x;
        g_match_page.box.h = (y + h) * area_h / GAME_H - g_match_page.box.y;
        need = (u32)(g_match_page.box.w * g_match_page.box.h);
        if (need > g_match_page.room) {
            free(g_match_page.pixels);
            g_match_page.pixels = malloc((size_t)need * sizeof(u32));
            g_match_page.room = g_match_page.pixels != NULL ? need : 0;
        }
        if (g_match_page.pixels == NULL || need == 0) {
            g_match_page.drawn = FALSE;
            return FALSE;
        }
        memset(g_match_page.pixels, 0, (size_t)need * sizeof(u32));
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
        g_match_page.drawn = TRUE;
    }
    *pixels = g_match_page.pixels;
    *box = g_match_page.box;
    return TRUE;
}
