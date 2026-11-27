#ifndef PORT_MENU_H
#define PORT_MENU_H

/* The native port's settings menu (docs/PLAYING.md, "The settings menu"):
 * every setting of rebushido.cfg and the buttons of both pads, over the
 * game's picture, drawn by the window at its own resolution and in its
 * own font. Only the build with a window has it.
 *
 * The menu is none of the game's state. Over ReBushido's main menu and
 * the fight's pause it is open beside the game, which runs on under it
 * with no press given it, its music with it (sdl_menu.c; docs/PORT.md,
 * "The window's pages in the game's look"); anywhere else the game is
 * stopped at a vertical blank where it has just shown a frame
 * (port_RunMenu): nothing of the game runs, and when it closes the game
 * goes on from that blank as if the time had not passed. It is drawn in
 * the look of the game's own Option Menu (page_look.c), and its presses
 * play the game's own menu sounds (port_PageSound). What it changes is
 * the window's settings and the settings file, and one setting of the
 * game's own (the bug fixes, which the options page at boot sets as
 * well).
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

/* ---- the look of the game's menu pages (page_look.c) ---- */

/* The game's screen the pages are placed in: 640 by 240, shown 4:3. */
#define PORT_LOOK_GAME_W 640
#define PORT_LOOK_GAME_H 240
/* The game's wide panel (menu_pages.c, MENU_PANEL_WIDE: the Option
 * Menu's), and its title above it with the rule under. */
#define PORT_LOOK_PANEL_X 63
#define PORT_LOOK_PANEL_Y 48
#define PORT_LOOK_PANEL_W 514
#define PORT_LOOK_PANEL_H 176
#define PORT_LOOK_TITLE_Y 33
#define PORT_LOOK_RULE_Y 38
/* lines of text, in the game's lines: a title, a row, a line, a note */
#define PORT_LOOK_SIZE_TITLE 15
#define PORT_LOOK_SIZE_ROW 11
#define PORT_LOOK_SIZE_TEXT 9
#define PORT_LOOK_SIZE_NOTE 8
/* The colours, 0xAARRGGBB, measured from the game's Option Menu: a title
 * and a value set, white; a row, the dull gold of the game's rows laid
 * half over its panel; the cursor's, yellow (its pulse: port_LookPulse);
 * a value not set and a note, grey; a row that cannot be chosen, dark. */
#define PORT_LOOK_WHITE 0xFFFFFFFFU
#define PORT_LOOK_ROW 0xFFAEAE4AU
#define PORT_LOOK_CURSOR 0xFFFFFF31U
#define PORT_LOOK_GREY 0xFFACACB4U
#define PORT_LOOK_OFF 0xFF68687AU
#define PORT_LOOK_GOLD 0xFFE6C877U
#define PORT_LOOK_WARN 0xFFF0A848U
#define PORT_LOOK_GOOD 0xFF8CD88CU
#define PORT_LOOK_SHADOW 0xB0000010U
/* the game's panel, slate blue laid half over what is under it
 * (menu_SetPanelColor), over the picture darkened where the game does not
 * draw it itself; and the title's rule, a light line and two fainter */
#define PORT_LOOK_PANEL 0x804C4C6CU
#define PORT_LOOK_DARKEN 0xD0000008U
#define PORT_LOOK_RULE 0xFFC8C8D0U
#define PORT_LOOK_RULE_FAINT 0x90808090U
#define PORT_LOOK_RULE_FAINTER 0x50808090U
/* a page fades in over this many frames, as it opens or changes */
#define PORT_LOOK_FADE_FRAMES 8

enum {
    PORT_LOOK_LEFT = 0,
    PORT_LOOK_CENTRE = 1,
    PORT_LOOK_RIGHT = 2,
};

/* A page's pixels: the whole of the game's 4:3 area, `area_w` by
 * `area_h` of the window's pixels. */
typedef struct PortLook {
    u32 *pixels;
    u32 room;
    s32 area_w;
    s32 area_h;
    s32 font_ok;
} PortLook;

/* The pixels made ready, cleared: FALSE if they cannot be (no memory, no
 * typeface). */
extern s32 port_LookBegin(PortLook *look, s32 area_w, s32 area_h);
/* The game's backdrop where the window draws it itself (`is_own`): the
 * wide panel, over the picture the window has darkened
 * (PORT_LOOK_DARKEN); nothing where the game's own menu page is under it
 * and draws them. */
extern void port_LookPanel(PortLook *look, s32 is_own);
/* A box of the game's places filled with `color` over what is there. */
extern void port_LookBox(PortLook *look, s32 x, s32 y, s32 w, s32 h, u32 color);
/* A line of text `lines` of the game's lines high, its baseline at the
 * game's `y`, aligned on the game's `x` (PORT_LOOK_...), in a face of the
 * port's typeface with the shadow the game's lettering has: its width in
 * the game's places. */
extern s32 port_LookText(PortLook *look, s32 x, s32 y, s32 lines, s32 face,
                         const char *text, u32 color, s32 align);
extern s32 port_LookMeasure(
    const PortLook *look, s32 lines, s32 face, const char *text);
/* `text` cut where it is wider than `room` of the game's places, at a
 * space where one is near, with "..." after it (never longer than it
 * was): its width then. */
extern s32 port_LookFit(
    const PortLook *look, s32 lines, s32 face, char *text, s32 room);
/* A page's title, centred over the panel, with the game's rule under. */
extern void port_LookTitle(PortLook *look, const char *title);
/* The cursor's colour `frames` after it last moved: the game's glow. */
extern u32 port_LookPulse(s32 frames);
/* Everything drawn so far laid `shown` of `of` over the picture: a fade. */
extern void port_LookFade(PortLook *look, s32 shown, s32 of);

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
    PORT_MENU_OPEN_TRAINING = 2, /* the Training group's page */
    /* a local tournament's page, to begin one, and the bracket of the one
     * on (tournament_page.c) */
    PORT_MENU_OPEN_TOURNAMENT = 3,
    PORT_MENU_OPEN_BRACKET = 4,
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
/* The menu drawn into a page of the game's look (page_look.c): with the
 * picture darkened and the game's panel where `is_own`, its text alone
 * where the game's own menu page is under it. */
extern void port_MenuDrawLook(PortLook *look, s32 is_own);
/* A frame of the menu's: its cursor's glow and its fade move on. */
extern void port_MenuTick(void);
/* A run's test (--menu-keys): what the menu is told, a word at a time. */
extern s32 port_MenuWord(const char *word);

/* ---- the settings menu's Replays page (menu_replays.c) ---- */

/* The replays kept, read again: how many rows there are. */
extern s32 port_ReplaysItems(void);
/* A row's label and value (-1: the row that says there are none): the
 * fighters, and how long it is; and the smaller line under them, their
 * weapons, when it was, the mode and the stage. */
extern void port_ReplaysDescribe(
    s32 item, char *label, u32 label_room, char *value, u32 value_room);
extern void port_ReplaysDetail(s32 item, char *out, u32 room);
extern const char *port_ReplaysHelp(s32 item);
/* The row's replay played from the game's next frame: TRUE, and the menu
 * closes; FALSE with why in `note`. */
extern s32 port_ReplaysAccept(s32 item, char *note, u32 room);
/* The row's replay made a film of (Square or Delete), by another run of
 * the program (sdl_export.c): TRUE where it was begun, FALSE with why in
 * `note`. */
extern s32 port_ReplaysExport(s32 item, char *note, u32 room);
/* The page's row of the film: whether it is up (one is being made, or
 * the last one's end is still said), its label, value and help, and what
 * Cross does on it (stops the film, or puts the row away). */
extern s32 port_ReplaysFilmShown(void);
extern void port_ReplaysFilmDescribe(
    char *label, u32 label_room, char *value, u32 value_room);
extern const char *port_ReplaysFilmHelp(void);
extern void port_ReplaysFilmAccept(void);
/* The settings of a film shown on the page as well, by name. */
extern const char * const g_port_replays_film_settings[2];

/* ---- the settings menu's Stats page (menu_stats.c) ---- */

/* The page's views: the totals and the lists' rows, then each list. */
enum {
    PORT_STATS_VIEW_TOTALS = 0,
    PORT_STATS_VIEW_FIGHTERS = 1,
    PORT_STATS_VIEW_WEAPONS = 2,
    PORT_STATS_VIEW_STAGES = 3,
    PORT_STATS_VIEW_MODES = 4,
    PORT_STATS_VIEW_OPPONENTS = 5,
    PORT_STATS_VIEW_KILLS = 6,
    PORT_STATS_VIEW_HISTORY = 7,
    PORT_STATS_VIEW_TOURNAMENTS = 8, /* the local tournaments over */
    PORT_STATS_VIEWS = 9
};

/* The record counted again: the lines of a view (the totals' lines, a
 * list's rows, the history's fights, newest first). */
extern s32 port_StatsPageItems(s32 view);
/* A line's label and value (-1: the line that says there is nothing);
 * and a fight's of the history, its smaller line under them. */
extern void port_StatsPageDescribe(s32 view, s32 item, char *label,
                                   u32 label_room, char *value, u32 value_room);
extern void port_StatsPageDetail(s32 item, char *out, u32 room);
extern const char *port_StatsPageHelp(s32 view, s32 item);
/* A view's name (the rows of the totals' page that open the lists, and
 * the title). */
extern const char *port_StatsViewName(s32 view);
/* A fight of the history: its replay played (TRUE, the menu closes), or
 * FALSE with why in `note`. */
extern s32 port_StatsPageAccept(s32 item, char *note, u32 room);

/* ---- the settings menu's pages of a local tournament (tournament_page.c)
 * ---- */

/* The pages: the tournament's (to begin one), an entrant's, the
 * bracket's. */
enum {
    PORT_TOUR_PAGE_SETUP = 0,
    PORT_TOUR_PAGE_ENTRANT = 1,
    PORT_TOUR_PAGE_BRACKET = 2,
};

/* What a press on an item asks of the menu. */
enum {
    PORT_TOUR_NOTHING = 0,
    PORT_TOUR_REBUILD = 1, /* the page's rows are others now */
    PORT_TOUR_TYPE = 2,    /* the row is typed into, from `typed` */
    PORT_TOUR_ENTRANT = 3, /* the entrant's page */
    PORT_TOUR_BRACKET = 4, /* the bracket's page */
    PORT_TOUR_CLOSE = 5,   /* the menu closed: the game goes on */
};

#define PORT_TOUR_ITEMS_MAX 12

/* A page's items (the setup page's settings are the menu's own rows,
 * before them), and a number that changes where the bracket has. */
extern s32 port_TournamentPageItems(s32 page, s32 *items, s32 most);
extern u32 port_TournamentPageGeneration(void);
/* An item's label and value; `typed` what is being typed into it, or
 * NULL. */
extern void port_TournamentPageDescribe(
    s32 item, const char *typed, char *label, u32 label_room, char *value,
    u32 value_room, s32 *is_arrows);
extern const char *port_TournamentPageHelp(s32 item, s32 is_typing);
extern s32 port_TournamentPageAccept(
    s32 item, char *typed, u32 typed_room, char *note, u32 note_room);
/* Left or Right on an item (PORT_TOUR_...). */
extern s32 port_TournamentPageStep(s32 item, s32 step);
/* Whether a name's row takes the letter `c`, and what was typed kept. */
extern s32 port_TournamentPageTakes(s32 item, char c);
extern s32 port_TournamentPageKeep(s32 item, const char *typed);
extern const char *port_TournamentPageTitle(s32 page);
/* The menu opened on one of them: a Leave half pressed forgotten. */
extern void port_TournamentPageOpened(void);
/* The bracket's page drawn whole: the bracket, its rows (`items`, the
 * cursor's lit), the line under them (`note` where there is one). */
extern void port_TournamentDrawBracket(
    PortLook *look, const s32 *items, s32 count, s32 cursor, s32 pulse,
    const char *note);
/* A test's picture of the bracket (--menu-shots) due: once each time the
 * bracket's page has come up and faded in. */
extern s32 port_MenuBracketShotDue(void);

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

/* The game's own menu sounds, which the pages play as its menus do
 * (port_top_menu.h, PORT_TOP_SOUND_...): the cursor moved or a value
 * changed, a row taken, a way back. */
enum {
    PORT_PAGE_SOUND_CURSOR = 0,
    PORT_PAGE_SOUND_CONFIRM = 1,
    PORT_PAGE_SOUND_CANCEL = 2,
};

extern void port_PageSound(s32 sound);

/* A setting the menu changed that takes at once (PORT_TAKES_NOW). */
extern void port_WindowApplyChoice(s32 choice);
/* Whether the remastered picture is on, and changing it (F2). */
extern s32 port_WindowIsRemastered(void);
extern void port_WindowSetRemastered(s32 on);
/* A binding as the settings file writes its value (`key Z`, `pad a`). */
extern void port_DescribeBinding(
    const PortBinding *binding, char *out, u32 room);

#endif /* PORT_MENU_H */
