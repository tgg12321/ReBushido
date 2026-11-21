/* The native port's settings menu in the window (menu.h, sdl_window.h;
 * docs/PLAYING.md, "The settings menu"): when it opens, the keys and the
 * controllers while it is open, and the loop it runs in with the game
 * stopped. Only the build with a window has this file.
 *
 * It is opened by F1 or a controller's guide button anywhere, by Select
 * on the three screens whose own loops do nothing with Select: the top
 * menu, ReBushido's options page at boot, and the fight's pause menu; and
 * by Options and Online Play on ReBushido's top menu, which ask for it
 * through the game's state (port_top_menu.h).
 * Asked for, it opens at the end of the first vertical blank where the
 * game has shown a new frame (the game's own frame boundary, where the
 * window has just shown it), or after a few blanks where the game shows
 * none, as on a load. Then the game is not run at all: the window loops
 * here, reading the PC's input and drawing the menu over the game's last
 * picture, until the menu closes, and the game goes on from that blank.
 * The game's clock is its vertical blanks, so it cannot tell the time
 * passed (src/port/window/pace.c lets the PC's time go), and what the
 * pads held as the menu closed is not given to it until let go. */
#include <SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "menu.h"
#include "port_net.h"
#include "port_online.h"
#include "port_remaster.h"
#include "port_spu.h"
#include "port_top_menu.h"
#include "shared.h"
#include "sdl_window.h"

/* The menu opens at the first blank where the game has shown a new
 * frame, or after this many where it shows none (a load, the options
 * page, which draws by itself); it is drawn about this often while open;
 * a stick pushed this far is a press in it, and binds an axis. */
#define WAIT_BLANKS 8
#define STEP_MS 16
#define STICK (PORT_AXIS_MAX / 2)
#define WORD_BYTES 64
#define PATH_BYTES 1024

static struct {
    s32 is_wanted;
    u32 asked_at;    /* the blank */
    s32 asked_frame; /* the game's frames shown then */
    u32 opened;
    u32 shots;
    /* the left stick's last direction, so that a push is one press */
    s32 stick[2];
    /* the times ReBushido's top menu has asked for the menu, as far as
     * the menu has followed them (port_top_menu.h) */
    s32 asks_seen;
    /* open beside the running game (the top menu's, the pause's) */
    s32 is_beside;
    u32 opened_at; /* SDL's ticks */
    /* a test's presses (--menu-keys), and blanks to wait (`wait:N`) */
    const char *words;
    s32 waiting;
} g_port_menu;

/* What the settings menu and the online pages share: the page's pixels,
 * whether the top menu is covered, whether their sounds are played, and a
 * test's picture to take. */
static struct {
    PortLook look;
    s32 covered;
    s32 sounds_on;
    char shot[PATH_BYTES];
    s32 has_shot;
    /* the stopped game's sound: SDL's ticks at its last blank, and the
     * blanks it has had */
    u32 sound_at;
    u32 sound_blanks;
} g_port_page;

static void port_MenuEvent(const SDL_Event *event);

/* ---- the pages over the game ---- */

/* The console's blanks a second, as the PC's milliseconds count them: the
 * stopped game's sound is made at this pace. */
#define BLANKS_A_SECOND 60
#define SOUND_BLANKS_MOST 6

s32 port_PageCanBeBeside(void) {
    PortGameState now;

    port_ReadGameState(&now);
    return (now.is_top_menu && now.is_top_menu_ours) || now.is_paused;
}

s32 port_PageIsBeside(void) {
    return (port_MenuIsOpen() && g_port_menu.is_beside) ||
           port_OnlinePagesAreBeside();
}

void port_PageCover(s32 on) {
    PortGameState now;

    port_ReadGameState(&now);
    on = on && now.is_top_menu && now.is_top_menu_ours;
    g_port_page.covered = on;
    if (on != port_TopMenuIsCovered()) {
        port_SetTopMenuCovered(on);
    }
}

void port_PageSoundsOn(s32 on) {
    g_port_page.sounds_on = on;
    g_port_page.sound_at = SDL_GetTicks();
    g_port_page.sound_blanks = 0;
}

void port_PageSound(s32 sound) {
    if (g_port_page.sounds_on) {
        port_AskMenuSound(sound);
    }
}

void port_PageKeepSound(void) {
    u32 now = SDL_GetTicks();
    u32 owed = (now - g_port_page.sound_at) * BLANKS_A_SECOND / 1000;
    u32 i;

    if (!g_port_page.sounds_on) {
        return;
    }
    if (g_port_window_options.is_fast) {
        /* a test's run: a blank a turn, whatever the time */
        owed = g_port_page.sound_blanks + 1;
    }
    for (i = 0; g_port_page.sound_blanks < owed && i < SOUND_BLANKS_MOST; i++) {
        port_RunStoppedSound();
        g_port_page.sound_blanks++;
    }
    if (g_port_page.sound_blanks < owed) {
        /* the PC was held up: what is owed past a few blanks is let go */
        g_port_page.sound_blanks = owed;
    }
}

s32 port_PagePixels(s32 area_w, s32 area_h, const u32 **pixels, PortRect *box) {
    if (!port_MenuIsOpen() && !port_OnlinePagesAreOpen()) {
        return FALSE;
    }
    if (!port_LookBegin(&g_port_page.look, area_w, area_h)) {
        return FALSE;
    }
    /* where the top menu is covered, the game draws its own backdrop and
     * panel under the page */
    if (port_MenuIsOpen()) {
        port_MenuDrawLook(&g_port_page.look, !g_port_page.covered);
    } else {
        port_OnlinePagesDraw(&g_port_page.look, !g_port_page.covered);
    }
    *pixels = g_port_page.look.pixels;
    box->x = 0;
    box->y = 0;
    box->w = area_w;
    box->h = area_h;
    return TRUE;
}

void port_PageShot(const char *path) {
    snprintf(g_port_page.shot, sizeof(g_port_page.shot), "%s", path);
    g_port_page.has_shot = TRUE;
}

const char *port_PageTakeShot(void) {
    if (!g_port_page.has_shot) {
        return NULL;
    }
    g_port_page.has_shot = FALSE;
    return g_port_page.shot;
}

s32 port_PageTakesEvent(const SDL_Event *event) {
    if (!port_PageIsBeside()) {
        return FALSE;
    }
    if (event->type == SDL_KEYDOWN || event->type == SDL_KEYUP) {
        SDL_Scancode code = event->key.keysym.scancode;

        if (code == SDL_SCANCODE_F2 || code == SDL_SCANCODE_F3 ||
            code == SDL_SCANCODE_F9 || code == SDL_SCANCODE_F12) {
            return FALSE;
        }
    }
    if (port_MenuIsOpen()) {
        port_MenuEvent(event);
    } else {
        port_OnlinePagesEvent(event);
    }
    return TRUE;
}

void port_AskForMenu(const char *why) {
    PortGameState now;

    if (g_port_menu.is_wanted || port_MenuIsOpen()) {
        return;
    }
    /* netplay's other program cannot wait while this one is stopped, and
     * a setting changed would be this one's alone (docs/NETPLAY.md,
     * section 12) */
    if (port_IsNetplayOn()) {
        /* online, the menu's key asks to leave the session (or the
         * watching): once shown on the line, twice left (port_online.h) */
        port_Note("settings menu: asked for by %s; not during netplay: a "
                  "leave asked",
                  why);
        port_OnlineLeave();
        return;
    }
    port_ReadGameState(&now);
    g_port_menu.is_wanted = TRUE;
    g_port_menu.asked_at = port_GetBlankCount();
    g_port_menu.asked_frame = now.frames;
    port_Note(
        "settings menu: asked for by %s; blank %u", why, g_port_menu.asked_at);
}

u32 port_MenuTakesPad(u32 held, u32 sent, u32 *out) {
    u32 select = port_FindPadButton("select");
    PortGameState now;

    *out = held;
    if (port_PageIsBeside()) {
        /* a page beside the game: nothing is the game's until let go */
        *out = 0;
        return held;
    }
    if (!(held & select) || (sent & select)) {
        return 0;
    }
    port_ReadGameState(&now);
    if (!now.is_top_menu && !now.is_options_page && !now.is_paused) {
        return 0;
    }
    /* the game has no use for Select here: it is the menu's */
    port_AskForMenu("Select");
    *out = held & ~select;
    return select;
}

/* ---- the PC's input while it is open ---- */

/* A direction of the left stick, once a push. */
static void port_MenuStick(s32 axis, s32 value) {
    s32 which = axis == SDL_CONTROLLER_AXIS_LEFTX ? 0 : 1;
    s32 now = value > STICK ? 1 : value < -STICK ? -1 : 0;

    if (now != 0 && now != g_port_menu.stick[which]) {
        port_MenuPress(which == 0 ? (now > 0 ? PORT_MENU_RIGHT : PORT_MENU_LEFT)
                                  : (now > 0 ? PORT_MENU_DOWN : PORT_MENU_UP));
    }
    g_port_menu.stick[which] = now;
}

/* A key while a row of the Play online page is typed into: a letter
 * (Shift for a capital), a digit, a dot, a colon (the key of ; and :),
 * brackets, a space, - and _, ! and ? (Shift with 1 and /), or Backspace.
 * TRUE if it was one; Enter and Escape are the menu's own
 * (port_MenuPress). What a row takes of them is its own
 * (menu_online.c). */
static s32 port_MenuTypedKey(SDL_Scancode code) {
    s32 shift = (SDL_GetModState() & KMOD_SHIFT) != 0;

    if (code >= SDL_SCANCODE_A && code <= SDL_SCANCODE_Z) {
        port_MenuType((char)((shift ? 'A' : 'a') + (code - SDL_SCANCODE_A)));
    } else if (code == SDL_SCANCODE_1 && shift) {
        port_MenuType('!');
    } else if (code == SDL_SCANCODE_SLASH && shift) {
        port_MenuType('?');
    } else if (code == SDL_SCANCODE_MINUS) {
        port_MenuType(shift ? '_' : '-');
    } else if (code == SDL_SCANCODE_LEFTBRACKET) {
        port_MenuType('[');
    } else if (code == SDL_SCANCODE_RIGHTBRACKET) {
        port_MenuType(']');
    } else if (code == SDL_SCANCODE_SPACE) {
        port_MenuType(' ');
    } else if (code >= SDL_SCANCODE_1 && code <= SDL_SCANCODE_9) {
        port_MenuType((char)('1' + (code - SDL_SCANCODE_1)));
    } else if (code == SDL_SCANCODE_0 || code == SDL_SCANCODE_KP_0) {
        port_MenuType('0');
    } else if (code >= SDL_SCANCODE_KP_1 && code <= SDL_SCANCODE_KP_9) {
        port_MenuType((char)('1' + (code - SDL_SCANCODE_KP_1)));
    } else if (code == SDL_SCANCODE_PERIOD || code == SDL_SCANCODE_KP_PERIOD) {
        port_MenuType('.');
    } else if (code == SDL_SCANCODE_SEMICOLON) {
        port_MenuType(':');
    } else if (code == SDL_SCANCODE_BACKSPACE) {
        port_MenuType('\b');
    } else {
        return FALSE;
    }
    return TRUE;
}

/* A key: the menu's own keys, whatever the pads' are, or the key to
 * bind. */
static void port_MenuKey(const SDL_KeyboardEvent *key) {
    SDL_Scancode code = key->keysym.scancode;
    s32 first = key->repeat == 0;
    char value[WORD_BYTES];

    if (key->type != SDL_KEYDOWN) {
        return;
    }
    if (port_MenuIsTyping() && port_MenuTypedKey(code)) {
        return;
    }
    if (port_MenuIsCapturing()) {
        if (!first) {
            return;
        }
        if (code == SDL_SCANCODE_ESCAPE) {
            port_MenuCaptured(TRUE, NULL);
            return;
        }
        snprintf(value, sizeof(value), "key %s", SDL_GetScancodeName(code));
        port_MenuCaptured(TRUE, value);
        return;
    }
    switch (code) {
    case SDL_SCANCODE_UP:
        port_MenuPress(PORT_MENU_UP);
        break;
    case SDL_SCANCODE_DOWN:
        port_MenuPress(PORT_MENU_DOWN);
        break;
    case SDL_SCANCODE_LEFT:
        port_MenuPress(PORT_MENU_LEFT);
        break;
    case SDL_SCANCODE_RIGHT:
        port_MenuPress(PORT_MENU_RIGHT);
        break;
    case SDL_SCANCODE_RETURN:
    case SDL_SCANCODE_KP_ENTER:
    case SDL_SCANCODE_SPACE:
        if (first && (key->keysym.mod & KMOD_ALT) == 0) {
            port_MenuPress(PORT_MENU_ACCEPT);
        } else if (first) {
            port_ToggleFullscreen();
        }
        break;
    case SDL_SCANCODE_ESCAPE:
    case SDL_SCANCODE_BACKSPACE:
        if (first) {
            port_MenuPress(PORT_MENU_BACK);
        }
        break;
    case SDL_SCANCODE_F1:
        if (first) {
            port_MenuPress(PORT_MENU_CLOSE);
        }
        break;
    case SDL_SCANCODE_F11:
        if (first) {
            port_ToggleFullscreen();
        }
        break;
    case SDL_SCANCODE_DELETE:
        port_MenuPress(PORT_MENU_CLEAR);
        break;
    default:
        break;
    }
}

/* A controller's button: by where it is, as the PlayStation's are (the
 * lowest of the four is Cross), or the button to bind. */
static void port_MenuButton(s32 button) {
    char value[WORD_BYTES];

    if (port_MenuIsCapturing()) {
        if (button == SDL_CONTROLLER_BUTTON_GUIDE) {
            port_MenuCaptured(FALSE, NULL);
            return;
        }
        snprintf(value, sizeof(value), "pad %s",
                 SDL_GameControllerGetStringForButton(
                     (SDL_GameControllerButton)button));
        port_MenuCaptured(FALSE, value);
        return;
    }
    switch (button) {
    case SDL_CONTROLLER_BUTTON_DPAD_UP:
        port_MenuPress(PORT_MENU_UP);
        break;
    case SDL_CONTROLLER_BUTTON_DPAD_DOWN:
        port_MenuPress(PORT_MENU_DOWN);
        break;
    case SDL_CONTROLLER_BUTTON_DPAD_LEFT:
        port_MenuPress(PORT_MENU_LEFT);
        break;
    case SDL_CONTROLLER_BUTTON_DPAD_RIGHT:
        port_MenuPress(PORT_MENU_RIGHT);
        break;
    case SDL_CONTROLLER_BUTTON_A:
        port_MenuPress(PORT_MENU_ACCEPT);
        break;
    case SDL_CONTROLLER_BUTTON_B:
    case SDL_CONTROLLER_BUTTON_Y:
        port_MenuPress(PORT_MENU_BACK);
        break;
    case SDL_CONTROLLER_BUTTON_X:
        port_MenuPress(PORT_MENU_CLEAR);
        break;
    case SDL_CONTROLLER_BUTTON_START:
    case SDL_CONTROLLER_BUTTON_BACK:
    case SDL_CONTROLLER_BUTTON_GUIDE:
        port_MenuPress(PORT_MENU_CLOSE);
        break;
    default:
        break;
    }
}

/* An axis: the left stick moves the cursor, and any stick or trigger
 * pushed far is what is bound. */
static void port_MenuAxis(s32 axis, s32 value) {
    char word[WORD_BYTES];

    if (!port_MenuIsCapturing()) {
        if (axis == SDL_CONTROLLER_AXIS_LEFTX ||
            axis == SDL_CONTROLLER_AXIS_LEFTY) {
            port_MenuStick(axis, value);
        }
        return;
    }
    if (value > STICK || value < -STICK) {
        snprintf(
            word, sizeof(word), "pad %c%s", value > 0 ? '+' : '-',
            SDL_GameControllerGetStringForAxis((SDL_GameControllerAxis)axis));
        port_MenuCaptured(FALSE, word);
    }
}

/* An event while the menu is open. What the keys and controllers hold is
 * followed all the same, so that a button let go in the menu is let go
 * for the game too; nothing is given to the game until the menu closes,
 * and then nothing that is still held (port_HoldBackPads). */
static void port_MenuEvent(const SDL_Event *event) {
    s32 pad;

    switch (event->type) {
    case SDL_QUIT:
        port_MenuPress(PORT_MENU_CLOSE);
        port_Quit(PORT_EXIT_OK);
    case SDL_KEYDOWN:
    case SDL_KEYUP:
        port_MenuKey(&event->key);
        if (event->type == SDL_KEYUP &&
            event->key.keysym.scancode == SDL_SCANCODE_ESCAPE) {
            /* Escape held into the menu is let go for the game too */
            port_TakeEscape(FALSE);
        }
        port_ApplyInput(
            &g_port_settings, PORT_SOURCE_KEY, 0,
            (s32)event->key.keysym.scancode, event->type == SDL_KEYDOWN);
        break;
    case SDL_CONTROLLERDEVICEADDED:
    case SDL_CONTROLLERDEVICEREMOVED:
        port_TakeControllerEvent(event);
        break;
    case SDL_CONTROLLERBUTTONDOWN:
    case SDL_CONTROLLERBUTTONUP:
        if (event->cbutton.state == SDL_PRESSED) {
            port_MenuButton(event->cbutton.button);
        }
        pad = port_PadOfController(event->cbutton.which);
        if (pad >= 0) {
            port_ApplyInput(
                &g_port_settings, PORT_SOURCE_BUTTON, pad,
                event->cbutton.button, event->cbutton.state == SDL_PRESSED);
        }
        break;
    case SDL_CONTROLLERAXISMOTION:
        port_MenuAxis(event->caxis.axis, event->caxis.value);
        pad = port_PadOfController(event->caxis.which);
        if (pad >= 0) {
            port_ApplyInput(&g_port_settings, PORT_SOURCE_AXIS, pad,
                            event->caxis.axis, event->caxis.value);
        }
        break;
    default:
        break;
    }
}

void port_DescribeBinding(const PortBinding *binding, char *out, u32 room) {
    const char *name;

    if (binding->source == PORT_SOURCE_KEY) {
        name = SDL_GetScancodeName((SDL_Scancode)binding->code);
        snprintf(out, room, "key %s", name != NULL ? name : "?");
    } else if (binding->source == PORT_SOURCE_BUTTON) {
        name = SDL_GameControllerGetStringForButton(
            (SDL_GameControllerButton)binding->code);
        snprintf(out, room, "pad %s", name != NULL ? name : "?");
    } else {
        name = SDL_GameControllerGetStringForAxis(
            (SDL_GameControllerAxis)binding->code);
        snprintf(out, room, "pad %c%s", binding->direction > 0 ? '+' : '-',
                 name != NULL ? name : "?");
    }
}

/* ---- the loop ---- */

/* The next word of a test's presses (--menu-keys) into `word`: FALSE when
 * there are none left. */
static s32 port_NextMenuWord(const char **at, char *word) {
    u32 length = 0;

    while (**at == ' ' || **at == ',') {
        (*at)++;
    }
    if (**at == 0) {
        return FALSE;
    }
    while (**at != 0 && **at != ' ' && **at != ',') {
        if (length < WORD_BYTES - 1) {
            word[length++] = **at;
        }
        (*at)++;
    }
    word[length] = 0;
    return TRUE;
}

/* The path of a test's next picture (--menu-shots), or NULL where none
 * was asked for. */
static const char *port_MenuShotPath(char *path, u32 room) {
    if (g_port_window_options.menu_shots == NULL) {
        return NULL;
    }
    port_MakeFolder(g_port_window_options.menu_shots);
    snprintf(path, room, "%s/menu-%03u.png", g_port_window_options.menu_shots,
             g_port_menu.shots++);
    return path;
}

/* A test's next word, at a turn of the menu's: a press, `shot` (the
 * window's picture as it shows the menu), or `wait:N` (N turns with
 * none). With none left, the menu is closed. TRUE where the word was
 * `shot`. */
static s32 port_MenuTestTurn(void) {
    char word[WORD_BYTES];

    if (g_port_menu.words == NULL) {
        return FALSE;
    }
    if (g_port_menu.waiting > 0) {
        g_port_menu.waiting--;
        return FALSE;
    }
    if (!port_NextMenuWord(&g_port_menu.words, word)) {
        g_port_menu.words = NULL;
        port_MenuPress(PORT_MENU_CLOSE);
        return FALSE;
    }
    if (strcmp(word, "shot") == 0) {
        return TRUE;
    }
    if (strncmp(word, "wait:", 5) == 0) {
        g_port_menu.waiting = atoi(word + 5);
    } else if (!port_MenuWord(word)) {
        port_Say("--menu-keys: no such press: %s", word);
    }
    return FALSE;
}

/* The menu opened: on the page asked for, its sounds played where the
 * game runs beside it, the top menu covered. */
static void port_OpenMenu(u32 blank, s32 is_beside) {
    const char *path = port_GetSettingsPath();

    g_port_menu.is_wanted = FALSE;
    g_port_menu.opened++;
    g_port_menu.stick[0] = 0;
    g_port_menu.stick[1] = 0;
    g_port_menu.is_beside = is_beside;
    g_port_menu.opened_at = SDL_GetTicks();
    g_port_menu.words = g_port_window_options.menu_keys;
    g_port_menu.waiting = 0;
    port_MenuOpen(path != NULL ? path : "rebushido.cfg");
    port_PageSoundsOn(is_beside);
    port_PageCover(is_beside);
    if (is_beside) {
        port_Note("settings menu: open (%u times now) beside the game, which "
                  "runs on under it; blank %u",
                  g_port_menu.opened, blank);
    } else {
        port_Note("settings menu: open (%u times now); the game is stopped at "
                  "blank %u",
                  g_port_menu.opened, blank);
    }
}

/* The menu closed: the key or the button that closed it is no press of
 * the game's. */
static void port_ClosedMenu(u32 blank) {
    port_HoldBackPads();
    port_PageSoundsOn(FALSE);
    port_PageCover(FALSE);
    port_Note("settings menu: closed after %u ms; the game goes on from "
              "blank %u",
              SDL_GetTicks() - g_port_menu.opened_at, blank);
    g_port_menu.is_beside = FALSE;
}

/* The menu, from the moment it opens to the moment it closes, with the
 * game stopped. Nothing of the game's runs in here; the sound card plays
 * what is queued and then silence. */
static void port_RunMenu(u32 blank) {
    char path[PATH_BYTES];

    port_OpenMenu(blank, FALSE);
    while (port_MenuIsOpen()) {
        SDL_Event event;
        s32 is_shot;

        while (SDL_PollEvent(&event)) {
            port_MenuEvent(&event);
        }
        is_shot = port_MenuIsOpen() && port_MenuTestTurn();
        if (!port_MenuIsOpen()) {
            break;
        }
        port_MenuTick();
        port_ShowPage(
            blank, is_shot ? port_MenuShotPath(path, sizeof(path)) : NULL);
        if (!g_port_window_options.is_fast) {
            SDL_Delay(STEP_MS);
        }
    }
    port_ClosedMenu(blank);
}

/* A blank's turn of the menu open beside the game: a test's word, the
 * glow and the fade on a frame. Its presses came with the window's events
 * (port_PageTakesEvent). */
static void port_MenuBesideTurn(u32 blank) {
    char path[PATH_BYTES];

    if (port_MenuIsOpen() && port_MenuTestTurn()) {
        const char *shot = port_MenuShotPath(path, sizeof(path));

        if (shot != NULL) {
            port_PageShot(shot);
        }
    }
    if (!port_MenuIsOpen()) {
        port_ClosedMenu(blank);
        return;
    }
    port_MenuTick();
    if (!port_PageCanBeBeside()) {
        /* the game left the screen under it (it cannot, with no press;
         * a state put back can): the menu goes with it */
        port_MenuPress(PORT_MENU_CLOSE);
        port_ClosedMenu(blank);
    }
}

/* Whether a test asked for the menu at this blank (--menu-at). */
static s32 port_IsMenuBlank(u32 blank) {
    const char *at = g_port_window_options.menu_at;

    while (at != NULL && *at != 0) {
        if ((u32)atoi(at) == blank) {
            return TRUE;
        }
        at = strchr(at, ',');
        if (at != NULL) {
            at++;
        }
    }
    return FALSE;
}

void port_MenuFollowAsks(void) {
    PortGameState now;

    port_ReadGameState(&now);
    g_port_menu.asks_seen = now.menu_asks;
}

void port_MaybeRunMenu(u32 blank) {
    PortGameState now;

    if (port_MenuIsOpen() && g_port_menu.is_beside) {
        port_MenuBesideTurn(blank);
        return;
    }
    if (g_port_menu.is_beside) {
        /* closed between two blanks, by a press read at a boundary */
        port_ClosedMenu(blank);
    }
    if (!port_MenuIsOpen() && !port_OnlinePagesAreOpen() &&
        !port_OnlineIsBusy() && port_TopMenuIsCovered()) {
        /* no page is up (a session's end with none to come back to, a
         * state put back): the top menu is the game's again */
        port_PageCover(FALSE);
    }
    port_ReadGameState(&now);
    if (now.menu_asks != g_port_menu.asks_seen) {
        /* Options or Online Play on ReBushido's top menu: the count only
         * goes forward, and a frame run again does not count twice.
         * Online Play is the online pages (online_pages.c) */
        g_port_menu.asks_seen = now.menu_asks;
        if (now.menu_ask_page == PORT_TOP_ASK_ONLINE) {
            port_AskForOnlinePages("Online Play");
        } else {
            port_MenuOpenOn(PORT_MENU_OPEN_MAIN);
            port_AskForMenu("Options");
        }
    }
    if (g_port_window_options.online_at > 0 &&
        blank == (u32)g_port_window_options.online_at) {
        port_AskForOnlinePages("--online-at");
    }
    if (port_MaybeRunOnlinePages(blank)) {
        return;
    }
    if (port_IsMenuBlank(blank)) {
        port_AskForMenu("--menu-at");
    }
    if (!g_port_menu.is_wanted) {
        return;
    }
    if (port_PageCanBeBeside()) {
        /* the top menu or the pause: the game goes on under the menu, its
         * music with it, as under a page of its own */
        port_OpenMenu(blank, TRUE);
        return;
    }
    if (now.frames != g_port_menu.asked_frame ||
        blank - g_port_menu.asked_at >= WAIT_BLANKS) {
        port_RunMenu(blank);
    }
}
