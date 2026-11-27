/* The native port's window: what training lays over its fight
 * (include/port_training.h; docs/PLAYING.md, "Training"). The input
 * display: a column for each pad under its count of wins, the newest
 * press on top, each line the frames it was held, the direction (an
 * arrow) and the buttons (the PlayStation's four shapes in their colours,
 * the shoulder buttons by name), as the fighters were given them; and a
 * line at the foot of what training is doing (recording, playing back,
 * frozen, slow); and the hit areas over the fighters: each point of the
 * body a disc where a blade wounds, a darker one in it where a blade kills
 * (the head, the chest, the hips), and each weapon's blade from the hand
 * to the point, lit while it is in its frames of hitting. Drawn at the window's
 * own resolution, as the match's strip is (match_page.c), from training's state
 * (src/port/training.c), which the game's step alone writes. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "menu.h"
#include "port_training.h"

/* The game's screen, whose places these are given in: 640 by 240, shown
 * 4:3, so that a unit across is half a unit down. */
#define GAME_W 640
#define GAME_H 240

/* Sony's pad bits (libetc.h), which a hosted file does not include. */
#define PAD_L2 0x0001
#define PAD_R2 0x0002
#define PAD_L1 0x0004
#define PAD_R1 0x0008
#define PAD_TRIANGLE 0x0010
#define PAD_CIRCLE 0x0020
#define PAD_CROSS 0x0040
#define PAD_SQUARE 0x0080
#define PAD_SELECT 0x0100
#define PAD_START 0x0800
#define PAD_UP 0x1000
#define PAD_RIGHT 0x2000
#define PAD_DOWN 0x4000
#define PAD_LEFT 0x8000

/* The columns: under each side's count of wins, a line a press. */
#define COLUMN_W 104
#define COLUMN_X_1 10
#define COLUMN_X_2 (GAME_W - COLUMN_W - 10)
#define COLUMN_Y 34
#define LINE_H 11
#define LINES 12
#define FRAMES_W 22 /* the frames' digits, right-aligned in this */
#define ARROW_X 30  /* the arrow's middle */
#define BUTTONS_X 42
#define BUTTON_STEP 12
#define SHOULDER_STEP 14
/* The line of what training does, at the foot, in the middle. */
#define STATUS_W 220
#define STATUS_H 14
#define STATUS_Y (GAME_H - STATUS_H - 8)

/* The colours, 0xAARRGGBB, the match's strip's own and the shapes'. */
#define COLOR_STRIP 0x900E121EU
#define COLOR_TEXT 0xFFEDEDF2U
#define COLOR_DIM 0xFF8E96AAU
#define COLOR_ARROW 0xFFE6C877U
#define COLOR_TRIANGLE 0xFF3FD7A0U
#define COLOR_CIRCLE 0xFFF0605AU
#define COLOR_CROSS 0xFF7AA8F0U
#define COLOR_SQUARE 0xFFE58AD0U
#define COLOR_RECORD 0xFFF05A5AU
#define COLOR_SIDE_1 0xFFE8A05AU
#define COLOR_SIDE_2 0xFF7AB4F0U
#define SIZE_LINE 8
#define SIZE_STATUS 10
/* The hit areas: a wound's disc by its group (the head, the chest and
 * hips, an arm, a leg), the core that kills, a blade's line by side, and
 * the line while it can hit. */
#define COLOR_AREA_HEAD 0x50F0D060U
#define COLOR_AREA_BODY 0x48F0C050U
#define COLOR_AREA_ARM 0x4060C0F0U
#define COLOR_AREA_LEG 0x4050E0A0U
#define COLOR_AREA_EDGE 0xC0FFFFFFU
#define COLOR_AREA_CORE 0x90F03030U
#define COLOR_BLADE_1 0xE0E8A05AU
#define COLOR_BLADE_2 0xE07AB4F0U
#define COLOR_BLADE_HITS 0xFFFF4040U
/* the room round the areas' box, in the game's places */
#define AREA_MARGIN 8

typedef struct {
    s32 drawn;
    s32 area_w;
    s32 area_h;
    u32 key;
    u32 *pixels;
    u32 room;
    PortRect box;
} TrainingCanvas;

/* The two columns and the line of what training does. */
#define CANVASES PORT_TRAINING_CANVASES
#define CANVAS_STATUS PORT_TRAINING_CANVAS_STATUS
#define CANVAS_AREAS PORT_TRAINING_CANVAS_AREAS
static TrainingCanvas g_training_canvases[CANVASES];
static TrainingCanvas *g_canvas;
static s32 g_training_now;
static s32 g_training_font_ok;

/* ---- places and shapes ---- */

static s32 port_TrainingX(s32 x) {
    return x * g_canvas->area_w / GAME_W - g_canvas->box.x;
}

static s32 port_TrainingY(s32 y) {
    return y * g_canvas->area_h / GAME_H - g_canvas->box.y;
}

/* A pixel's colour laid over what is there by `cover` (0 to 1). */
static void port_TrainingDot(s32 x, s32 y, u32 color, float cover) {
    u32 alpha = (u32)((float)(color >> 24) * cover);

    if (alpha == 0) {
        return;
    }
    port_FillRect(g_canvas->pixels, g_canvas->box.w, g_canvas->box.h, x, y, 1,
                  1, (alpha << 24) | (color & 0xFFFFFFU));
}

/* A line of the window's pixels from (x0, y0) to (x1, y1), `width` wide,
 * its edge smoothed by how near each pixel's middle is to it. */
static void port_TrainingLine(
    float x0, float y0, float x1, float y1, float width, u32 color) {
    float dx = x1 - x0;
    float dy = y1 - y0;
    float length2 = dx * dx + dy * dy;
    float half = width / 2.0f;
    s32 left = (s32)((x0 < x1 ? x0 : x1) - half - 1.0f);
    s32 right = (s32)((x0 > x1 ? x0 : x1) + half + 1.0f);
    s32 top = (s32)((y0 < y1 ? y0 : y1) - half - 1.0f);
    s32 bottom = (s32)((y0 > y1 ? y0 : y1) + half + 1.0f);
    s32 x;
    s32 y;

    for (y = top; y <= bottom; y++) {
        for (x = left; x <= right; x++) {
            float px = (float)x + 0.5f - x0;
            float py = (float)y + 0.5f - y0;
            float along = length2 > 0.0f ? (px * dx + py * dy) / length2 : 0.0f;
            float ex;
            float ey;
            float d2;
            float d;
            float cover;
            s32 k;

            along = along < 0.0f ? 0.0f : along > 1.0f ? 1.0f : along;
            ex = px - along * dx;
            ey = py - along * dy;
            d2 = ex * ex + ey * ey;
            /* a square root, as match_page.c takes its own */
            for (k = 0, d = d2 > 1.0f ? d2 : 1.0f; k < 10; k++) {
                d = 0.5f * (d + d2 / d);
            }
            if (d2 < 1.0f) {
                d = d2;
            }
            cover = half + 0.5f - d;
            if (cover <= 0.0f) {
                continue;
            }
            port_TrainingDot(x, y, color, cover > 1.0f ? 1.0f : cover);
        }
    }
}

/* A ring round (cx, cy) of the window's pixels. */
static void port_TrainingRing(
    float cx, float cy, float r, float width, u32 color) {
    s32 reach = (s32)(r + width) + 2;
    s32 x;
    s32 y;

    for (y = -reach; y <= reach; y++) {
        for (x = -reach; x <= reach; x++) {
            float fx = (float)x + 0.5f + (cx - (float)(s32)cx);
            float fy = (float)y + 0.5f + (cy - (float)(s32)cy);
            float d2 = fx * fx + fy * fy;
            float d = d2 > 1.0f ? d2 : 1.0f;
            float cover;
            s32 k;

            for (k = 0; k < 10; k++) {
                d = 0.5f * (d + d2 / d);
            }
            cover = width / 2.0f + 0.5f - (d > r ? d - r : r - d);
            if (cover > 0.0f) {
                port_TrainingDot((s32)cx + x, (s32)cy + y, color,
                                 cover > 1.0f ? 1.0f : cover);
            }
        }
    }
}

static void port_TrainingText(
    s32 x, s32 y, s32 lines, const char *text, u32 color, s32 align) {
    PortTextStyle style;
    s32 px = port_TrainingX(x);

    memset(&style, 0, sizeof(style));
    style.face = PORT_FACE_BOLD;
    style.size = lines * g_canvas->area_h / GAME_H;
    if (style.size < 6) {
        style.size = 6;
    }
    style.color = color;
    style.shadow = 0xC0000000U;
    style.shadow_px = style.size >= 24 ? 2 : 1;
    if (align == 0) {
        px -= port_TextMeasure(text, &style) / 2;
    } else if (align > 0) {
        px -= port_TextMeasure(text, &style);
    }
    port_TextDraw(g_canvas->pixels, g_canvas->box.w, g_canvas->box.h, px,
                  port_TrainingY(y), text, &style);
}

static void port_TrainingRect(s32 x, s32 y, s32 w, s32 h, u32 color) {
    s32 x0 = port_TrainingX(x);
    s32 y0 = port_TrainingY(y);

    port_FillRect(
        g_canvas->pixels, g_canvas->box.w, g_canvas->box.h, x0, y0,
        port_TrainingX(x + w) - x0, port_TrainingY(y + h) - y0, color);
}

/* A disc round (cx, cy) of the window's pixels, filled and edged. */
static void port_TrainingDisc(float cx, float cy, float r, u32 fill, u32 edge) {
    s32 reach = (s32)r + 2;
    s32 x;
    s32 y;

    if (r < 0.5f) {
        return;
    }
    for (y = -reach; y <= reach; y++) {
        for (x = -reach; x <= reach; x++) {
            float fx = (float)((s32)cx + x) + 0.5f - cx;
            float fy = (float)((s32)cy + y) + 0.5f - cy;
            float d2 = fx * fx + fy * fy;
            float d = d2 > 1.0f ? d2 : 1.0f;
            float inside;
            s32 k;

            for (k = 0; k < 10; k++) {
                d = 0.5f * (d + d2 / d);
            }
            if (d2 < 1.0f) {
                d = d2;
            }
            inside = r + 0.5f - d;
            if (inside <= 0.0f) {
                continue;
            }
            port_TrainingDot(
                (s32)cx + x, (s32)cy + y, fill, inside > 1.0f ? 1.0f : inside);
            if (edge != 0 && inside < 1.5f) {
                port_TrainingDot((s32)cx + x, (s32)cy + y, edge,
                                 inside > 1.0f ? 2.0f - inside : inside);
            }
        }
    }
}

/* ---- a press ---- */

/* The arrow of the direction held, round (x, y) of the game's places, as
 * the screen has it: right is right. */
static void port_TrainingArrow(s32 x, s32 y, u16 buttons) {
    float scale = (float)g_canvas->area_h / GAME_H;
    float cx = (float)port_TrainingX(x);
    float cy = (float)port_TrainingY(y);
    float dx =
        (float)(((buttons & PAD_RIGHT) != 0) - ((buttons & PAD_LEFT) != 0));
    float dy = (float)(((buttons & PAD_DOWN) != 0) - ((buttons & PAD_UP) != 0));
    float reach = 3.6f * scale;
    float unit;
    float tx;
    float ty;
    float width = scale * 0.9f;

    if (dx == 0.0f && dy == 0.0f) {
        port_TrainingRing(cx, cy, 0.9f * scale, width, COLOR_DIM);
        return;
    }
    /* across, a unit is half a unit down: the arrow's length in pixels */
    unit = dx != 0.0f && dy != 0.0f ? 0.7071f : 1.0f;
    tx = cx + dx * unit * reach;
    ty = cy + dy * unit * reach;
    port_TrainingLine(cx - dx * unit * reach, cy - dy * unit * reach, tx, ty,
                      width, COLOR_ARROW);
    /* the head: two strokes back from the point, a quarter turn apart */
    port_TrainingLine(
        tx, ty, tx - (dx - dy) * unit * reach * 0.55f,
        ty - (dy + dx) * unit * reach * 0.55f, width, COLOR_ARROW);
    port_TrainingLine(
        tx, ty, tx - (dx + dy) * unit * reach * 0.55f,
        ty - (dy - dx) * unit * reach * 0.55f, width, COLOR_ARROW);
}

/* One of the four shapes round (x, y) of the game's places. */
static void port_TrainingShape(s32 x, s32 y, u16 button) {
    float scale = (float)g_canvas->area_h / GAME_H;
    float cx = (float)port_TrainingX(x);
    float cy = (float)port_TrainingY(y);
    float r = 3.2f * scale;
    float width = scale * 0.85f;

    if (button == PAD_CIRCLE) {
        port_TrainingRing(cx, cy, r * 0.85f, width, COLOR_CIRCLE);
    } else if (button == PAD_CROSS) {
        port_TrainingLine(cx - r * 0.7f, cy - r * 0.7f, cx + r * 0.7f,
                          cy + r * 0.7f, width, COLOR_CROSS);
        port_TrainingLine(cx - r * 0.7f, cy + r * 0.7f, cx + r * 0.7f,
                          cy - r * 0.7f, width, COLOR_CROSS);
    } else if (button == PAD_SQUARE) {
        float s = r * 0.72f;

        port_TrainingLine(cx - s, cy - s, cx + s, cy - s, width, COLOR_SQUARE);
        port_TrainingLine(cx + s, cy - s, cx + s, cy + s, width, COLOR_SQUARE);
        port_TrainingLine(cx + s, cy + s, cx - s, cy + s, width, COLOR_SQUARE);
        port_TrainingLine(cx - s, cy + s, cx - s, cy - s, width, COLOR_SQUARE);
    } else {
        float top = cy - r * 0.85f;
        float foot = cy + r * 0.6f;

        port_TrainingLine(cx, top, cx + r * 0.85f, foot, width, COLOR_TRIANGLE);
        port_TrainingLine(
            cx + r * 0.85f, foot, cx - r * 0.85f, foot, width, COLOR_TRIANGLE);
        port_TrainingLine(cx - r * 0.85f, foot, cx, top, width, COLOR_TRIANGLE);
    }
}

/* A line of the display: the frames, the arrow, the buttons. */
static void port_TrainingPress(
    s32 x, s32 y, const PortTrainingPress *press, s32 is_newest) {
    static const u16 g_shapes[] = {
        PAD_TRIANGLE, PAD_CIRCLE, PAD_CROSS, PAD_SQUARE};

    static const struct {
        u16 bit;
        const char *name;
    } g_shoulders[] = {{PAD_L1, "L1"}, {PAD_R1, "R1"},    {PAD_L2, "L2"},
                       {PAD_R2, "R2"}, {PAD_START, "St"}, {PAD_SELECT, "Se"}};

    char frames[8];
    s32 at = x + BUTTONS_X;
    u32 i;

    snprintf(frames, sizeof(frames), "%u",
             (unsigned)(press->frames > 999 ? 999 : press->frames));
    port_TrainingText(x + FRAMES_W, y + LINE_H - 3, SIZE_LINE, frames,
                      is_newest ? COLOR_TEXT : COLOR_DIM, 1);
    port_TrainingArrow(x + ARROW_X, y + LINE_H / 2, press->buttons);
    for (i = 0; i < sizeof(g_shapes) / sizeof(g_shapes[0]); i++) {
        if (press->buttons & g_shapes[i]) {
            port_TrainingShape(at, y + LINE_H / 2, g_shapes[i]);
            at += BUTTON_STEP;
        }
    }
    for (i = 0; i < sizeof(g_shoulders) / sizeof(g_shoulders[0]); i++) {
        if (press->buttons & g_shoulders[i].bit) {
            port_TrainingText(at - 4, y + LINE_H - 3, SIZE_LINE - 1,
                              g_shoulders[i].name, COLOR_TEXT, -1);
            at += SHOULDER_STEP;
        }
    }
}

/* ---- what is shown ---- */

static s32 port_TrainingShown(void) {
    return g_port_training.is_on && !g_port_training.in_pause &&
           port_IsFightScreen();
}

/* A key of what a canvas shows: drawn again when it changes. */
static u32 port_TrainingKey(s32 which) {
    const PortTraining *training = &g_port_training;
    u32 key = 2166136261U;
    s32 i;

    if (which == CANVAS_AREAS) {
        return g_port_training_areas.frame;
    }
    if (which == CANVAS_STATUS) {
        key ^= (u32)training->recording | (u32)training->playing << 4 |
               (u32)training->frozen << 8 | (u32)training->slow << 12 |
               (u32)training->dummy << 16 | (u32)training->record_at << 20;
        return key;
    }
    for (i = 0; i < training->shown_count[which]; i++) {
        key = (key ^ training->shown[which][i].buttons) * 16777619U;
        key = (key ^ training->shown[which][i].frames) * 16777619U;
    }
    return key ^ (u32)training->shown_count[which];
}

static void port_TrainingColumn(s32 pad) {
    const PortTraining *training = &g_port_training;
    s32 x = pad == 0 ? COLUMN_X_1 : COLUMN_X_2;
    s32 count =
        training->shown_count[pad] < LINES ? training->shown_count[pad] : LINES;
    s32 i;

    port_TrainingRect(
        x, COLUMN_Y, COLUMN_W, 4 + LINE_H * (count + 1), COLOR_STRIP);
    port_TrainingRect(
        x, COLUMN_Y, COLUMN_W, 1, pad == 0 ? COLOR_SIDE_1 : COLOR_SIDE_2);
    port_TrainingText(
        x + 4, COLUMN_Y + LINE_H - 2, SIZE_LINE, pad == 0 ? "P1" : "Dummy",
        pad == 0 ? COLOR_SIDE_1 : COLOR_SIDE_2, -1);
    port_TrainingText(x + COLUMN_W - 4, COLUMN_Y + LINE_H - 2, SIZE_LINE - 1,
                      "frames", COLOR_DIM, 1);
    for (i = 0; i < count; i++) {
        port_TrainingPress(x, COLUMN_Y + 3 + LINE_H * (i + 1),
                           &training->shown[pad][i], i == 0);
    }
}

/* The hit areas, as the last frame of the fight noted them. */
static void port_TrainingAreas(void) {
    static const u32 g_fills[4] = {
        COLOR_AREA_HEAD, COLOR_AREA_BODY, COLOR_AREA_ARM, COLOR_AREA_LEG};
    const PortTrainingAreas *areas = &g_port_training_areas;
    float per_unit = (float)g_canvas->area_w / GAME_W;
    s32 side;
    s32 i;

    for (side = 0; side < 2; side++) {
        /* the limbs first, the body over them, the cores last */
        s32 group;

        for (group = 3; group >= 0; group--) {
            for (i = 0; i < PORT_TRAINING_BODY_POINTS; i++) {
                const PortTrainingArea *area = &areas->body[side][i];

                if (!area->seen || area->group != group) {
                    continue;
                }
                port_TrainingDisc((float)port_TrainingX(area->x),
                                  (float)port_TrainingY(area->y),
                                  (float)area->radius * per_unit,
                                  g_fills[group], COLOR_AREA_EDGE);
            }
        }
        for (i = 0; i < PORT_TRAINING_BODY_POINTS; i++) {
            const PortTrainingArea *area = &areas->body[side][i];

            if (area->seen && area->core_radius > 0) {
                port_TrainingDisc(
                    (float)port_TrainingX(area->x),
                    (float)port_TrainingY(area->y),
                    (float)area->core_radius * per_unit, COLOR_AREA_CORE, 0);
            }
        }
    }
    for (side = 0; side < 2; side++) {
        u32 color = areas->blade_hits[side] ? COLOR_BLADE_HITS
                    : side == 0             ? COLOR_BLADE_1
                                            : COLOR_BLADE_2;
        float width = (areas->blade_hits[side] ? 2.6f : 1.6f) *
                      (float)g_canvas->area_h / GAME_H;

        if (!areas->blade_live[side]) {
            continue;
        }
        for (i = 0; i + 1 < areas->blade_points[side]; i++) {
            if (!areas->blade_seen[side][i] ||
                !areas->blade_seen[side][i + 1]) {
                continue;
            }
            port_TrainingLine(
                (float)port_TrainingX(areas->blade[side][i][0]),
                (float)port_TrainingY(areas->blade[side][i][1]),
                (float)port_TrainingX(areas->blade[side][i + 1][0]),
                (float)port_TrainingY(areas->blade[side][i + 1][1]), width,
                color);
        }
        for (i = 0; i < areas->blade_points[side]; i++) {
            if (areas->blade_seen[side][i]) {
                port_TrainingDisc(
                    (float)port_TrainingX(areas->blade[side][i][0]),
                    (float)port_TrainingY(areas->blade[side][i][1]),
                    width * 1.2f, color, 0);
            }
        }
    }
}

/* Where the hit areas are on the game's screen, with room round them. */
static void port_TrainingAreasBox(s32 *x, s32 *y, s32 *w, s32 *h) {
    const PortTrainingAreas *areas = &g_port_training_areas;
    s32 left = 0x7FFF;
    s32 top = 0x7FFF;
    s32 right = -0x7FFF;
    s32 bottom = -0x7FFF;
    s32 side;
    s32 i;

    for (side = 0; side < 2; side++) {
        for (i = 0; i < PORT_TRAINING_BODY_POINTS; i++) {
            const PortTrainingArea *area = &areas->body[side][i];

            if (area->seen) {
                left = area->x - area->radius < left ? area->x - area->radius
                                                     : left;
                right = area->x + area->radius > right ? area->x + area->radius
                                                       : right;
                top = area->y - area->radius / 2 < top
                          ? area->y - area->radius / 2
                          : top;
                bottom = area->y + area->radius / 2 > bottom
                             ? area->y + area->radius / 2
                             : bottom;
            }
        }
        for (i = 0; i < areas->blade_points[side]; i++) {
            if (areas->blade_seen[side][i]) {
                const s16 *at = areas->blade[side][i];

                left = at[0] < left ? at[0] : left;
                right = at[0] > right ? at[0] : right;
                top = at[1] < top ? at[1] : top;
                bottom = at[1] > bottom ? at[1] : bottom;
            }
        }
    }
    /* never wider than three screens nor taller than two */
    left = left < -GAME_W ? -GAME_W : left;
    right = right > 2 * GAME_W ? 2 * GAME_W : right;
    top = top < -GAME_H / 2 ? -GAME_H / 2 : top;
    bottom = bottom > GAME_H + GAME_H / 2 ? GAME_H + GAME_H / 2 : bottom;
    *x = left - AREA_MARGIN;
    *y = top - AREA_MARGIN;
    *w = right > left ? right - left + 2 * AREA_MARGIN : 0;
    *h = bottom > top ? bottom - top + 2 * AREA_MARGIN : 0;
}

static void port_TrainingStatus(void) {
    static const char * const g_dummies[PORT_TRAINING_DUMMIES] = {
        "Dummy stands", "Dummy guards", "Dummy plays back"};
    const PortTraining *training = &g_port_training;
    char line[96];
    u32 color = COLOR_TEXT;

    if (training->recording) {
        snprintf(line, sizeof(line), "Recording slot %d   %d.%d s",
                 training->recording, training->record_at / 30,
                 training->record_at % 30 / 3);
        color = COLOR_RECORD;
    } else if (training->playing) {
        snprintf(line, sizeof(line), "Playing slot %d", training->playing);
    } else {
        snprintf(
            line, sizeof(line), "%s",
            g_dummies[training->dummy < PORT_TRAINING_DUMMIES ? training->dummy
                                                              : 0]);
    }
    if (training->frozen) {
        snprintf(line + strlen(line), sizeof(line) - strlen(line), "   Frozen");
    } else if (training->slow > 1) {
        snprintf(line + strlen(line), sizeof(line) - strlen(line),
                 "   Slow 1/%d", training->slow);
    }
    port_TrainingRect(
        (GAME_W - STATUS_W) / 2, STATUS_Y, STATUS_W, STATUS_H, COLOR_STRIP);
    if (training->recording) {
        port_TrainingRing((float)port_TrainingX((GAME_W - STATUS_W) / 2 + 8),
                          (float)port_TrainingY(STATUS_Y + STATUS_H / 2),
                          1.6f * g_canvas->area_h / GAME_H,
                          3.2f * g_canvas->area_h / GAME_H, COLOR_RECORD);
    }
    port_TrainingText(
        GAME_W / 2, STATUS_Y + STATUS_H - 4, SIZE_STATUS, line, color, 0);
}

void port_TrainingCanvasChoose(s32 which) { g_training_now = which; }

s32 port_TrainingPixels(
    s32 area_w, s32 area_h, const u32 **pixels, PortRect *box) {
    s32 which = g_training_now;
    u32 key;
    u32 need;
    s32 x;
    s32 y;
    s32 w;
    s32 h;

    if (which < 0 || which >= CANVASES || !port_TrainingShown() ||
        area_w <= 0 || area_h <= 0) {
        return FALSE;
    }
    if (which < CANVAS_STATUS && g_port_training.shown_count[which] == 0) {
        return FALSE;
    }
    if (which == CANVAS_AREAS && !g_port_training_areas.on) {
        return FALSE;
    }
    if (!g_training_font_ok) {
        g_training_font_ok = port_FontStart();
        if (!g_training_font_ok) {
            return FALSE;
        }
    }
    g_canvas = &g_training_canvases[which];
    key = port_TrainingKey(which);
    if (!g_canvas->drawn || g_canvas->area_w != area_w ||
        g_canvas->area_h != area_h || g_canvas->key != key) {
        g_canvas->key = key;
        g_canvas->area_w = area_w;
        g_canvas->area_h = area_h;
        if (which == CANVAS_AREAS) {
            port_TrainingAreasBox(&x, &y, &w, &h);
        } else if (which == CANVAS_STATUS) {
            x = (GAME_W - STATUS_W) / 2;
            y = STATUS_Y;
            w = STATUS_W;
            h = STATUS_H;
        } else {
            x = which == 0 ? COLUMN_X_1 : COLUMN_X_2;
            y = COLUMN_Y;
            w = COLUMN_W;
            h = 4 + LINE_H * (LINES + 1);
        }
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
        if (which == CANVAS_AREAS) {
            port_TrainingAreas();
        } else if (which == CANVAS_STATUS) {
            port_TrainingStatus();
        } else {
            port_TrainingColumn(which);
        }
        g_canvas->drawn = TRUE;
    }
    *pixels = g_canvas->pixels;
    *box = g_canvas->box;
    return TRUE;
}
