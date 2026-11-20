/* Netplay in the native port's window (sdl_window.h; docs/NETPLAY.md,
 * section 13; docs/PLAYING.md, "Playing online"): what the window hangs
 * on netplay (port_net.h) and the line it draws over the picture. Only
 * the build with a window has this file. Nothing here changes what the
 * game computes:
 *
 *   - the line: who this player is, the round trip, the longest rollback
 *     of the last seconds and the delay; or what is being waited for; or
 *     that the other player has gone. It is drawn in the settings menu's
 *     font at the window's own resolution, as crisp in 4K as at 960 by
 *     720, and none of it is in the game's video memory;
 *   - the waits: while netplay waits on the other program the window's
 *     events are read (closing the window ends the program) and its picture is
 *     shown again with the line saying so; before the game starts, while
 *     the session is set up, a panel says how far it has got;
 *   - time sync: where this program is ahead of the other, the window's
 *     blanks are held back (pace.c, port_PaceHold);
 *   - a rollback's sound: what the queue holds of the blanks run again
 *     and has not played is taken back and the corrected sound queued in
 *     its place (sdl_window.c, port_SoundRerunBegins), so that a rollback
 *     neither repeats a sound nor drops one that is still to come. */
#include <SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "menu.h"
#include "port_net.h"
#include "portmap.h"
#include "port_spu.h"
#include "shared.h"
#include "sdl_window.h"

#define LINE_BYTES 400
/* A failure's panel: the least time it is shown before a key closes it,
 * and the most. */
#define FAIL_LEAST_MS 600
#define FAIL_MOST_MS 600000
#define PANEL_LINES_MAX 8
/* The line's height against the window's, its letters against the line,
 * and the least it is. */
#define LINE_PART 34
#define LINE_LEAST 16
#define LETTER_PERCENT 66
#define PERCENT 100
/* The waits are drawn about this often; the line says the other has gone
 * for this long. */
#define WAIT_DRAW_MS 16
#define ALONE_SHOWN_MS 8000
#define MS_PER_SECOND 1000
/* Colours, 0xAARRGGBB: the box under the line, its letters, and a wait's. */
#define COLOR_BOX 0xB0000000U
#define COLOR_TEXT 0xFFEDEDF2U
#define COLOR_WAIT 0xFFF0A848U
#define COLOR_PANEL 0xF60E121EU
#define COLOR_EDGE 0xFF6A5530U
#define COLOR_TITLE 0xFFE6C877U
#define COLOR_DIM 0xFF8E96AAU

static struct {
    u32 *pixels;
    u32 room;
    s32 font_ok;
    s32 font_tried;
    u32 drawn_at; /* SDL's ticks at the last wait drawn */
    char status[LINE_BYTES];
} g_net;

/* Room for `w` by `h` pixels: FALSE if there is none. */
static s32 port_NetRoom(s32 w, s32 h) {
    if (g_net.room < (u32)(w * h)) {
        free(g_net.pixels);
        g_net.room = (u32)(w * h);
        g_net.pixels = malloc((size_t)g_net.room * sizeof(u32));
        if (g_net.pixels == NULL) {
            g_net.room = 0;
            return FALSE;
        }
    }
    return TRUE;
}

static s32 port_NetFont(void) {
    if (!g_net.font_tried) {
        g_net.font_tried = TRUE;
        g_net.font_ok = port_FontStart();
    }
    return g_net.font_ok;
}

/* The line's words now: "" where there is nothing to say. */
static u32 port_NetWords(char *out, u32 room) {
    PortNetplayStats stats;

    port_GetNetplayStats(&stats);
    out[0] = 0;
    if (!stats.is_on) {
        /* a replay's line, where one plays (sdl_replay.c) */
        return port_ReplayWords(out, room);
    }
    if (stats.leave_ms != 0) {
        /* the leave asked once: the second leaves (port_online.h) */
        snprintf(out, room, "%s",
                 stats.watching ? "Press F1 or Escape again to stop watching"
                                : "Press F1 or Escape again to leave the "
                                  "fight: the other player wins it");
        return COLOR_WAIT;
    }
    if (stats.watching) {
        snprintf(out, room,
                 "Watching   |   %u frames behind the players   |   F1 twice: "
                 "stop",
                 stats.behind);
        return stats.state == PORT_NETPLAY_WAITING ? COLOR_WAIT : COLOR_TEXT;
    }
    if (stats.state == PORT_NETPLAY_ALONE) {
        if (stats.alone_ms < ALONE_SHOWN_MS) {
            snprintf(out, room,
                     "The other player has left: you play on "
                     "alone");
        }
        return COLOR_WAIT;
    }
    if (stats.state == PORT_NETPLAY_WAITING ||
        stats.state == PORT_NETPLAY_LOADING) {
        snprintf(out, room, "%s   %u.%u s",
                 stats.state == PORT_NETPLAY_LOADING
                     ? "Loading: waiting for the other player"
                     : "Waiting for the other player",
                 stats.waited_ms / MS_PER_SECOND,
                 stats.waited_ms % MS_PER_SECOND / (MS_PER_SECOND / 10));
        return COLOR_WAIT;
    }
    snprintf(out, room,
             "Online: player %d   |   %s   |   ping %u ms   |   rollback %u   "
             "|   delay %u",
             stats.pad + 1, port_GetSessionPath(), stats.ping_ms,
             stats.rollback, stats.delay);
    return COLOR_TEXT;
}

void port_DrawNetLine(void) {
    char words[LINE_BYTES];
    u32 color = port_NetWords(words, sizeof(words));
    s32 window_w = 0;
    s32 window_h = 0;
    s32 line;
    s32 pad;
    s32 box_w;
    s32 i;

    /* the router's lease asked again when half of it has gone */
    port_PortMapTick(SDL_GetTicks());
    if (words[0] == 0 || !port_NetFont()) {
        return;
    }
    port_GetWindowSize(&window_w, &window_h);
    line = window_h / LINE_PART;
    if (line < LINE_LEAST) {
        line = LINE_LEAST;
    }
    pad = line / 3;
    port_FontSetSize(line * LETTER_PERCENT / PERCENT);
    box_w = port_FontMeasure(words) + 2 * pad;
    if (box_w > window_w) {
        box_w = window_w;
    }
    if (box_w <= 0 || !port_NetRoom(box_w, line)) {
        return;
    }
    for (i = 0; i < box_w * line; i++) {
        g_net.pixels[i] = COLOR_BOX;
    }
    port_FontDraw(g_net.pixels, box_w, line, pad,
                  (line + port_FontAscent()) / 2 - line / 10, words, color);
    port_LayPixels(g_net.pixels, box_w, line, (window_w - box_w) / 2, pad);
}

/* `text` drawn into the panel's pixels in lines no wider than `width`,
 * broken between words, from `y` down by `step`: the lines drawn, at
 * most `most`. With no `pixels`, only counted. */
static s32 port_NetDrawWrapped(
    u32 *pixels, s32 w, s32 h, s32 x, s32 y, s32 step, s32 width, s32 most,
    const char *text, u32 color) {
    char line[LINE_BYTES];
    s32 lines = 0;

    while (*text != 0 && lines < most) {
        s32 length = 0;
        s32 fits = 0;

        while (*text == ' ') {
            text++;
        }
        /* the most words that fit */
        for (;;) {
            s32 end = length;

            while (text[end] != 0 && text[end] != ' ' &&
                   end + 1 < (s32)sizeof(line)) {
                end++;
            }
            memcpy(line, text, (size_t)end);
            line[end] = 0;
            if (fits != 0 && port_FontMeasure(line) > width) {
                break;
            }
            fits = end;
            if (text[end] == 0 || end + 1 >= (s32)sizeof(line)) {
                break;
            }
            length = end + 1;
        }
        if (fits == 0) {
            break;
        }
        memcpy(line, text, (size_t)fits);
        line[fits] = 0;
        if (pixels != NULL) {
            port_FontDraw(pixels, w, h, x, y + lines * step + port_FontAscent(),
                          line, color);
        }
        lines++;
        text += fits;
    }
    return lines;
}

/* The panel of the session's set-up, before the game has a picture: a
 * title, what is being done (or what went wrong), and a last line. */
static void port_NetPanelOf(
    const char *title, const char *status, u32 status_color, const char *last) {
    PortTextStyle heading;
    PortRect place;
    s32 window_w = 0;
    s32 window_h = 0;
    s32 line;
    s32 small;
    s32 pad;
    s32 lines;
    s32 i;

    port_GetWindowSize(&window_w, &window_h);
    if (window_w <= 0 || window_h <= 0 || !port_NetFont()) {
        return;
    }
    line = window_h / LINE_PART * 2;
    if (line < LINE_LEAST) {
        line = LINE_LEAST;
    }
    pad = line / 2;
    small = line * 3 / 4;
    place.w = line * 26 < window_w - 2 * pad ? line * 26 : window_w - 2 * pad;
    if (place.w <= 2 * pad) {
        return;
    }
    port_FontSetSize(line * LETTER_PERCENT / PERCENT * 3 / 4);
    lines = port_NetDrawWrapped(NULL, 0, 0, 0, 0, small, place.w - 2 * pad,
                                PANEL_LINES_MAX, status, status_color);
    if (lines == 0) {
        lines = 1;
    }
    place.h = 2 * pad + line + lines * small + small + pad / 2;
    place.x = (window_w - place.w) / 2;
    place.y = (window_h - place.h) / 2;
    if (!port_NetRoom(place.w, place.h)) {
        return;
    }
    for (i = 0; i < place.w * place.h; i++) {
        g_net.pixels[i] = COLOR_PANEL;
    }
    port_FillRect(g_net.pixels, place.w, place.h, 0, 0, place.w, 2, COLOR_EDGE);
    port_FillRect(
        g_net.pixels, place.w, place.h, 0, place.h - 2, place.w, 2, COLOR_EDGE);
    memset(&heading, 0, sizeof(heading));
    heading.face = PORT_FACE_BOLD;
    heading.size = line * LETTER_PERCENT / PERCENT;
    heading.color = COLOR_TITLE;
    port_TextDraw(g_net.pixels, place.w, place.h, pad,
                  pad + port_TextAscent(&heading), title, &heading);
    port_FontSetSize(line * LETTER_PERCENT / PERCENT * 3 / 4);
    port_NetDrawWrapped(
        g_net.pixels, place.w, place.h, pad, pad + line, small,
        place.w - 2 * pad, PANEL_LINES_MAX, status, status_color);
    port_FontDraw(g_net.pixels, place.w, place.h, pad,
                  pad + line + lines * small + pad / 2 + port_FontAscent(),
                  last, COLOR_DIM);
    port_ShowMenu(port_GetBlankCount(), g_net.pixels, &place, NULL);
}

static void port_NetPanel(const char *status) {
    port_NetPanelOf("ReBushido: playing online", status, COLOR_TEXT,
                    "Close the window to give up");
}

/* The session could not be set up (port_net.h): what went wrong, on the
 * screen until a key says it has been read (or the window is closed);
 * then the program ends, as it does with no window. */
static void port_NetFail(const char *why) {
    u32 shown_at = SDL_GetTicks();

    port_Note("netplay: could not play online: %s", why);
    for (;;) {
        const Uint8 *keys;
        u32 now = SDL_GetTicks();

        port_PollWindow();
        port_NetPanelOf("Could not play online", why, COLOR_WAIT,
                        "Enter or Escape: close   (the log has it too)");
        keys = SDL_GetKeyboardState(NULL);
        if (now - shown_at > FAIL_LEAST_MS &&
            (keys[SDL_SCANCODE_RETURN] || keys[SDL_SCANCODE_ESCAPE] ||
             keys[SDL_SCANCODE_SPACE] || keys[SDL_SCANCODE_KP_ENTER])) {
            break;
        }
        if (now - shown_at > FAIL_MOST_MS) {
            break;
        }
        SDL_Delay(WAIT_DRAW_MS);
    }
}

/* Netplay waits on the network (port_net.h): the window's events read,
 * and its picture shown again with what is waited for, now and then. */
static void port_NetWait(const char *status) {
    PortNetplayStats stats;
    u32 now = SDL_GetTicks();

    if (now - g_net.drawn_at < WAIT_DRAW_MS) {
        return;
    }
    g_net.drawn_at = now;
    port_PollWindow();
    port_GetNetplayStats(&stats);
    if (stats.is_on) {
        port_ShowStill();
        return;
    }
    if (strcmp(status, g_net.status) != 0) {
        snprintf(g_net.status, sizeof(g_net.status), "%s", status);
        port_Note("netplay: %s", status);
    }
    port_NetPanel(status);
}

static void port_NetRerun(s32 begins, u32 blanks) {
    if (begins) {
        port_SoundRerunBegins(blanks);
    } else {
        port_SoundRerunEnds();
    }
}

void port_StartNetWindow(void) {
    g_port_net_hold_hook = port_HoldPace;
    g_port_net_wait_hook = port_NetWait;
    g_port_net_fail_hook = port_NetFail;
    g_port_net_rerun_hook = port_NetRerun;
    g_port_rerun_sound_hook = port_SoundRerun;
    /* a player whose partner goes is not thrown out of the game */
    port_SetNetplayGoesOnAlone(TRUE);
}
