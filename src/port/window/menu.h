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

/* ---- the lettering (font.c; docs/PORT.md, "The port's lettering") ---- */

/* The port's one typeface, Shippori Mincho, in three weights: the files,
 * which the build puts into the program (tools/port_sdl_build.py, from
 * src/port/fonts/). */
extern const unsigned char g_port_font_text[];
extern const unsigned char g_port_font_bold[];
extern const unsigned char g_port_font_title[];

enum {
    PORT_FACE_TEXT,  /* Regular: lines of the settings menu, notes */
    PORT_FACE_BOLD,  /* Bold: headings, a menu's lines, names */
    PORT_FACE_TITLE, /* ExtraBold: titles, the top menu's rows */
    PORT_FACES
};

/* How a text is drawn. A style of zeros but its face, size and colour is
 * plain letters. */
typedef struct {
    s32 face;    /* PORT_FACE_ */
    s32 size;    /* pixels from the top of the tallest letter to the bottom of
                    the deepest */
    u32 color;   /* 0xAARRGGBB; its alpha is how much it covers */
    u32 outline; /* an edge all round the letters, in this colour ... */
    s32 outline_px; /* ... this many pixels wide (0: none) */
    u32 shadow;     /* a shadow of the letters (and their edge) ... */
    s32 shadow_px;  /* ... this many pixels down and right (0: none) */
} PortTextStyle;

/* FALSE if the typeface cannot be read. Every call below draws nothing
 * and measures 0 until this has been TRUE. */
extern s32 port_FontStart(void);
/* Text (UTF-8: ASCII and Latin-1 are drawn, any other letter is `?`) into
 * pixels of 0xAARRGGBB, `w` by `h`: `x` is the left of its edge, if it
 * has one, and `baseline` the baseline. The letters are drawn from the
 * typeface's outlines at the style's size, kerned, and kept for the next
 * call of the same face and size. */
extern void port_TextDraw(u32 *pixels, s32 w, s32 h, s32 x, s32 baseline,
                          const char *text, const PortTextStyle *style);
/* How many pixels wide it is, its edge and shadow with it. */
extern s32 port_TextMeasure(const char *text, const PortTextStyle *style);
/* From the top of the tallest letter to the baseline, in pixels. */
extern s32 port_TextAscent(const PortTextStyle *style);

/* The same, for plain letters in the Regular face at one size set
 * beforehand. */
extern void port_FontSetSize(s32 pixels);
extern s32 port_FontAscent(void);
extern s32 port_FontMeasure(const char *text);
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

/* ---- the settings menu's Play online page (menu_online.c) ---- */

/* The page's rows, each an item of its own (ONLINE_..., menu_online.c),
 * which menu.c lays out and moves the cursor over. */
#define PORT_ONLINE_ITEMS_MAX 40

/* What a press on an item asks of the menu. */
enum {
    PORT_ONLINE_NOTHING = 0,
    PORT_ONLINE_REBUILD = 1, /* the page's rows are others now */
    PORT_ONLINE_TYPE = 2,    /* the row is typed into, from `typed` */
};

extern s32 port_OnlineItems(s32 *items, s32 most);
/* An item's label and value; `typed` is what is being typed into it, or
 * NULL. */
extern void port_OnlineDescribe(
    s32 item, const char *typed, char *label, u32 label_room, char *value,
    u32 value_room, s32 *is_arrows);
extern const char *port_OnlineHelp(s32 item, s32 is_typing);
extern s32 port_OnlineAccept(
    s32 item, char *typed, u32 typed_room, char *note, u32 note_room);
extern s32 port_OnlineStep(s32 item, s32 step);
/* What was typed into an item kept: PORT_ONLINE_..., or -1 with `note`
 * saying why not. */
extern s32 port_OnlineKeep(
    s32 item, const char *typed, char *note, u32 note_room);
/* Whether the item takes the letter `c` as it is typed into. */
extern s32 port_OnlineTakes(s32 item, char c);
/* A setting's line of the settings file written (menu.c), and the menu
 * closed, as before the program starts again. */
extern void port_MenuSetLine(const char *name, const char *value);
extern void port_MenuLeave(void);

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
