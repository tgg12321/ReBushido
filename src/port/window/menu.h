#ifndef PORT_MENU_H
#define PORT_MENU_H

/* The native port's settings menu (docs/PLAYING.md, "The settings menu"):
 * every setting of rebushido.cfg and the buttons of both pads, over the
 * game's picture, drawn by the window at its own resolution and in its
 * own font. Only the build with a window has it.
 *
 * The menu is none of the game's state. While it is open the game is
 * stopped at a vertical blank where it has just shown a frame
 * (sdl_window.c, port_RunMenu): nothing of the game runs, and when it
 * closes the game goes on from that blank as if the time had not passed.
 * What it changes is the window's settings and the settings file, and
 * one setting of the game's own (the bug fixes, which the options page
 * at boot sets as well).
 *
 * Its pages: the first, a row for each group of settings (the game, the
 * display, the remastered picture, the controls, the mods), Play online,
 * back to the game and Quit; a group's page, its settings, each with its
 * value (the one built in marked) and a line about it under the rows; a
 * pad's buttons; and Play online. */

#include "common.h"
#include "window.h"

/* ---- the font (font.c) ---- */

/* The font's file, which the build puts into the program
 * (tools/port_sdl_build.py, from src/port/fonts/Marcellus-Regular.ttf). */
extern const unsigned char g_port_menu_font[];

/* FALSE if the font cannot be read. */
extern s32 port_FontStart(void);
/* The letters made at this many pixels from the top of the tallest to the
 * bottom of the deepest. */
extern void port_FontSetSize(s32 pixels);
/* From the top of the tallest letter to the baseline, in pixels. */
extern s32 port_FontAscent(void);
extern s32 port_FontMeasure(const char *text);
/* Text into pixels of 0xAARRGGBB, `w` by `h`, its baseline at `baseline`,
 * in `color` (its alpha is how much it covers). */
extern void port_FontDraw(u32 *pixels, s32 w, s32 h, s32 x, s32 baseline,
                          const char *text, u32 color);
/* A rectangle of `color` laid over them. */
extern void port_FillRect(
    u32 *pixels, s32 w, s32 h, s32 x, s32 y, s32 rw, s32 rh, u32 color);

/* ---- the menu (menu.c) ---- */

/* What the player does in the menu: the keys, the controllers' and the
 * pads' buttons, each come to one of these (sdl_window.c). */
enum {
    PORT_MENU_UP,
    PORT_MENU_DOWN,
    PORT_MENU_LEFT,
    PORT_MENU_RIGHT,
    PORT_MENU_ACCEPT, /* Enter, Cross */
    PORT_MENU_BACK,   /* Escape, Backspace, Circle, Triangle */
    PORT_MENU_CLOSE,  /* F1, Start, the controller's guide button */
    PORT_MENU_CLEAR,  /* Delete, Square: a button back to the layout's */
};

/* Opens it on its first page, or on the page port_MenuOpenOn said last
 * (and only that once). `path` is the settings file it writes, which need
 * not be there yet. */
extern void port_MenuOpen(const char *path);

/* The page the next opening is on: its first, or Play online, whose Back
 * then closes the menu (Options and Online Play on ReBushido's top
 * menu). */
enum {
    PORT_MENU_OPEN_MAIN = 0,
    PORT_MENU_OPEN_ONLINE = 1,
};

extern void port_MenuOpenOn(s32 page);
extern s32 port_MenuIsOpen(void);
extern void port_MenuPress(s32 what);
/* Whether it is waiting for a key or a button to bind, and what it is
 * given: `keys` 1 for a key, 0 for a controller's button or axis, and
 * the value the file writes for it (`key Space`, `pad a`, `pad +leftx`);
 * NULL to give up. */
extern s32 port_MenuIsCapturing(void);
extern void port_MenuCaptured(s32 keys, const char *value);
/* Whether a row of the Play online page is being typed into (the host's
 * address, the port), and a letter for it: a digit, `.` or `:`; `\b`
 * takes the last one away and `\n` keeps what is typed. */
extern s32 port_MenuIsTyping(void);
extern void port_MenuType(char c);
/* The menu over a picture `w` by `h`, the window's size: its rectangle
 * in `out`, and its pixels (0xAARRGGBB) into `pixels`, which has room for
 * `w` by `h`. The rest of the window is darkened by the window itself. */
extern void port_MenuDraw(u32 *pixels, s32 w, s32 h, PortRect *out);
/* A run's test (--menu-keys): what the menu is told, a word at a time. */
extern s32 port_MenuWord(const char *word);

/* ---- what the window does for it (sdl_window.c, sdl_menu.c) ---- */

/* A setting the menu changed that takes at once (PORT_TAKES_NOW). */
extern void port_WindowApplyChoice(s32 choice);
/* Whether the remastered picture is on, and changing it (F2). */
extern s32 port_WindowIsRemastered(void);
extern void port_WindowSetRemastered(s32 on);
/* A binding as the settings file writes its value (`key Z`, `pad a`). */
extern void port_DescribeBinding(
    const PortBinding *binding, char *out, u32 room);

#endif /* PORT_MENU_H */
