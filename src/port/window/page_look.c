/* The native port's window: the look of the game's own menu pages, which
 * the settings menu (menu.c) and the online pages (online_pages.c) are
 * drawn in (menu.h; docs/PORT.md, "The window's pages in the game's
 * look").
 *
 * They are modelled on the game's Option Menu (menu_pages.c,
 * menu_DrawOptions): its title in white over a grey rule above the wide
 * panel of slate blue the game lays over its backdrop of night sky,
 * clouds and moon; its rows in a dull gold, the cursor's in yellow that
 * swells and fades as the game's cursor does (MENU_PULSE: once round in
 * 32 frames, 47 either side of the plain tint, from an eighth of a turn,
 * started again on each move); a value that is set in white, one that is
 * not in grey. Where the game's own menu page is up under them (ReBushido's
 * top menu, which draws its backdrop and the wide panel and nothing else
 * while a page of the window is over it: port_top_menu.h), the window
 * draws the text alone; anywhere else (over the fight's pause, a load,
 * the boot) it draws the panel too, over the picture darkened.
 *
 * Everything is placed in the game's own screen, 640 by 240 shown 4:3,
 * and drawn at the window's resolution in the port's typeface (font.c),
 * the same over the console's picture and the remastered one. */
#include <stdlib.h>
#include <string.h>

#include "menu.h"

/* the game's MENU_PULSE (menu_pages.c): frames once round, the depth
 * either side of the plain tint, which is 0x80; and the sine of each of
 * the 32 frames, in thousandths, from an eighth of a turn round */
#define PULSE_FRAMES 32
#define PULSE_DEPTH 47
#define PULSE_PLAIN 128
#define PULSE_SINE_ONE 1000
static const s16 g_pulse_sine[PULSE_FRAMES] = {
    707,  831,  924,  981,  1000, 981,  924,  831,  707,  556,   383,
    195,  0,    -195, -383, -556, -707, -831, -924, -981, -1000, -981,
    -924, -831, -707, -556, -383, -195, 0,    195,  383,  556};
#define CHANNEL_MAX 255U
#define ALPHA_SHIFT 24
/* the shadow under the letters, down and right, in 14ths of their size */
#define SHADOW_SHARE 14
/* the grey rule under a title (menu_DrawTitleRule): reaches past the
 * title this far each side, in the game's places, a line of it light and
 * two fainter above */
#define RULE_REACH 10
#define RULE_GAP 3

static s32 port_LookX(const PortLook *look, s32 x) {
    return x * look->area_w / PORT_LOOK_GAME_W;
}

static s32 port_LookY(const PortLook *look, s32 y) {
    return y * look->area_h / PORT_LOOK_GAME_H;
}

s32 port_LookBegin(PortLook *look, s32 area_w, s32 area_h) {
    u32 need = (u32)(area_w > 0 && area_h > 0 ? area_w * area_h : 0);

    if (need == 0) {
        return FALSE;
    }
    if (!look->font_ok) {
        look->font_ok = port_FontStart();
        if (!look->font_ok) {
            return FALSE;
        }
    }
    if (need > look->room) {
        free(look->pixels);
        look->pixels = malloc((size_t)need * sizeof(u32));
        look->room = look->pixels != NULL ? need : 0;
    }
    if (look->pixels == NULL) {
        return FALSE;
    }
    look->area_w = area_w;
    look->area_h = area_h;
    memset(look->pixels, 0, (size_t)need * sizeof(u32));
    return TRUE;
}

void port_LookBox(PortLook *look, s32 x, s32 y, s32 w, s32 h, u32 color) {
    s32 left = port_LookX(look, x);
    s32 top = port_LookY(look, y);

    port_FillRect(
        look->pixels, look->area_w, look->area_h, left, top,
        port_LookX(look, x + w) - left, port_LookY(look, y + h) - top, color);
}

void port_LookPanel(PortLook *look, s32 is_own) {
    if (!is_own) {
        return;
    }
    /* the game's wide panel (menu_DrawPageFrame, MENU_PANEL_WIDE): its
     * slate blue laid half over what is under it, which the window has
     * darkened, as the game's backdrop is dark (sdl_window.c) */
    port_LookBox(look, PORT_LOOK_PANEL_X, PORT_LOOK_PANEL_Y, PORT_LOOK_PANEL_W,
                 PORT_LOOK_PANEL_H, PORT_LOOK_PANEL);
}

s32 port_LookText(PortLook *look, s32 x, s32 y, s32 lines, s32 face,
                  const char *text, u32 color, s32 align) {
    PortTextStyle style;
    s32 size = lines * look->area_h / PORT_LOOK_GAME_H;
    s32 px;
    s32 width;

    if (size < 6) {
        size = 6;
    }
    memset(&style, 0, sizeof(style));
    style.face = face;
    style.size = size;
    style.color = color;
    style.shadow = PORT_LOOK_SHADOW;
    style.shadow_px = size / SHADOW_SHARE > 1 ? size / SHADOW_SHARE : 1;
    width = port_TextMeasure(text, &style);
    px = port_LookX(look, x);
    if (align == PORT_LOOK_CENTRE) {
        px -= width / 2;
    } else if (align == PORT_LOOK_RIGHT) {
        px -= width;
    }
    port_TextDraw(look->pixels, look->area_w, look->area_h, px,
                  port_LookY(look, y), text, &style);
    return width * PORT_LOOK_GAME_W / look->area_w;
}

s32 port_LookMeasure(
    const PortLook *look, s32 lines, s32 face, const char *text) {
    PortTextStyle style;
    s32 size = lines * look->area_h / PORT_LOOK_GAME_H;

    memset(&style, 0, sizeof(style));
    style.face = face;
    style.size = size < 6 ? 6 : size;
    style.shadow_px =
        style.size / SHADOW_SHARE > 1 ? style.size / SHADOW_SHARE : 1;
    return port_TextMeasure(text, &style) * PORT_LOOK_GAME_W / look->area_w;
}

void port_LookTitle(PortLook *look, const char *title) {
    s32 width = port_LookText(
        look, PORT_LOOK_GAME_W / 2, PORT_LOOK_TITLE_Y, PORT_LOOK_SIZE_TITLE,
        PORT_FACE_TITLE, title, PORT_LOOK_WHITE, PORT_LOOK_CENTRE);
    s32 left = PORT_LOOK_GAME_W / 2 - width / 2 - RULE_REACH;
    s32 line = port_LookY(look, 1) > 1 ? port_LookY(look, 1) : 1;
    s32 x = port_LookX(look, left);
    s32 w = port_LookX(look, left + width + 2 * RULE_REACH) - x;
    s32 y = port_LookY(look, PORT_LOOK_RULE_Y);

    /* the light line, and the two fainter above it */
    port_FillRect(look->pixels, look->area_w, look->area_h, x, y, w, line,
                  PORT_LOOK_RULE);
    port_FillRect(look->pixels, look->area_w, look->area_h, x,
                  y - RULE_GAP * line / 2, w, line, PORT_LOOK_RULE_FAINT);
    port_FillRect(look->pixels, look->area_w, look->area_h, x,
                  y - RULE_GAP * line, w, line, PORT_LOOK_RULE_FAINTER);
}

u32 port_LookPulse(s32 frames) {
    s32 tint =
        PULSE_PLAIN +
        PULSE_DEPTH * g_pulse_sine[(frames < 0 ? 0 : frames) % PULSE_FRAMES] /
            PULSE_SINE_ONE;
    u32 out = PORT_LOOK_CURSOR & (CHANNEL_MAX << ALPHA_SHIFT);
    s32 shift;

    /* a sprite's tint over the plain one brightens it, as far as white */
    for (shift = 0; shift < ALPHA_SHIFT; shift += 8) {
        u32 channel = ((PORT_LOOK_CURSOR >> shift) & CHANNEL_MAX) * (u32)tint /
                      PULSE_PLAIN;

        out |= (channel > CHANNEL_MAX ? CHANNEL_MAX : channel) << shift;
    }
    return out;
}

void port_LookFade(PortLook *look, s32 shown, s32 of) {
    u32 count = (u32)(look->area_w * look->area_h);
    u32 i;

    if (shown >= of || of <= 0) {
        return;
    }
    if (shown < 0) {
        shown = 0;
    }
    for (i = 0; i < count; i++) {
        u32 alpha = (look->pixels[i] >> ALPHA_SHIFT) * (u32)shown / (u32)of;

        look->pixels[i] =
            (look->pixels[i] & 0x00FFFFFFU) | (alpha << ALPHA_SHIFT);
    }
}
