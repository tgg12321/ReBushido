/* The native port's window: the online pages, drawn over the game in the
 * top menu's look (sdl_window.h; docs/PLAYING.md, "Playing online";
 * docs/NETPLAY.md, section 15). Online Play on the top menu opens them:
 *
 *   Quick Match       the first open room of the lobby that is this
 *                     program's kind and has no password, or a room of
 *                     the player's own if there is none
 *   Host a room       a room at the lobby: its name, a password if wanted
 *   Browse rooms      the lobby's rooms: name, host, players, locked, how
 *                     long open, and whether it can be joined or why not
 *   Join by code      the six letters and digits a host reads out
 *   Join by address   the advanced way: host or join by an address typed
 *   Your name         what the lobby and the other player see
 *
 * Whatever is typed is typed on the keyboard, or with a pad on the
 * keyboard drawn on the page. A session asked for begins at the game's
 * next boundary, in this program (src/port/online.c): while the two meet
 * the pages show the host's room and its code and the way found, or the
 * guest's progress, and then the lobby before the fight, both players'
 * names and setup codes, each ready or leaving. A refusal or a failure
 * comes back here in plain words; a session's end does too, over the
 * player's own game, which is back where it was. Nothing of this is in
 * the game's video memory, and nothing the game computes changes. */
#include <SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "menu.h"
#include "port_net.h"
#include "port_online.h"
#include "port_room.h"
#include "shared.h"
#include "sdl_window.h"
#include "update.h"

/* The game's screen, whose places the pages are laid out in, as the top
 * menu's are: 640 by 240, shown 4:3. */
#define GAME_W 640
#define GAME_H 240
/* The page's panel, in the game's places. */
#define PANEL_X 40
#define PANEL_Y 14
#define PANEL_W 560
#define PANEL_H 212
/* Lines' heights, in the game's lines: the title, a row, a line of text,
 * a small note. */
#define SIZE_TITLE 17
#define SIZE_ROW 12
#define SIZE_TEXT 9
#define SIZE_NOTE 8
#define TITLE_Y 36
#define ROWS_Y 62
#define ROW_PITCH 15
#define ROWS_X 72
#define VALUE_X 276
#define HELP_Y 200
#define FOOT_Y 216
#define ROWS_SHOWN 9
/* The colours, 0xAARRGGBB: the top menu's (top_page.c), and the panel. */
#define COLOR_CURSOR 0xFFFFEC9CU
#define COLOR_ROW 0xFFD8B860U
#define COLOR_ROW_OFF 0xFF707070U
#define COLOR_TEXT 0xFFF2F2F2U
#define COLOR_DIM 0xFFA8ACB8U
#define COLOR_WARN 0xFFF0A848U
#define COLOR_GOOD 0xFF8CD88CU
#define COLOR_SHADOW 0xC0101838U
#define COLOR_PANEL 0xF00C1020U
#define COLOR_EDGE 0xFF6A5530U
#define COLOR_KEY 0xFF20283CU
#define COLOR_KEY_ON 0xFF6A5530U
#define ROOMS_MAX 12
#define TEXT_BYTES 400
#define WORD_BYTES 64
#define STEP_MS 16
#define STICK 16384
#define HEX_BYTES 100
#define ADDRESS_ALL "0.0.0.0"
#define ADDRESS_LOCAL "127.0.0.1"
#define CODE_LENGTH 6
#define PASSWORD_LENGTH 32
#define ADDRESS_LENGTH 47
#define SECONDS_A_MINUTE 60

enum {
    PAGE_MAIN,
    PAGE_HOST,
    PAGE_ROOMS,
    PAGE_ADDRESS,
    PAGE_KEYBOARD,
    PAGE_MESSAGE,
    /* a room of several: its members, its matches, the member's choices */
    PAGE_ROOM,
    /* while the two meet (drawn by the session's waits) */
    PAGE_MEETING,
};

/* What a press is. */
enum {
    PRESS_NONE,
    PRESS_UP,
    PRESS_DOWN,
    PRESS_LEFT,
    PRESS_RIGHT,
    PRESS_ACCEPT,
    PRESS_BACK,
    PRESS_DELETE,
    PRESS_SHIFT,
    PRESS_DONE,
};

/* What a field typed on the keyboard is for. */
enum {
    FIELD_NAME,
    FIELD_ROOM,
    FIELD_PASSWORD,      /* a room's to host */
    FIELD_ROOM_PASSWORD, /* a locked room's, to join it */
    FIELD_CODE,
    FIELD_ADDRESS,
};

/* The rows of the main page, the host's and the address page's. */
enum {
    MAIN_QUICK,
    MAIN_HOST,
    MAIN_BROWSE,
    MAIN_CODE,
    MAIN_ADDRESS,
    MAIN_NAME,
    MAIN_BACK,
    MAIN_ROWS
};

/* (a room's kind and rotation after Open, so that the rows above keep
 * their places) */
enum {
    HOST_NAME,
    HOST_PASSWORD,
    HOST_OPEN,
    HOST_KIND,
    HOST_ROTATION,
    HOST_BACK,
    HOST_ROWS
};

/* The room of several's page. */
enum { ROOM_WATCH, ROOM_PLAY, ROOM_RULE, ROOM_SIDE, ROOM_LEAVE, ROOM_ROWS };

/* A room's match begins this long after the page says so. */
#define ROOM_NOTICE_MS 2000
/* The room's page: its members in two columns, its matches, its rows. */
#define ROOM_MEMBERS_Y 56
#define ROOM_MEMBER_PITCH 11
#define ROOM_COLUMN_W 260
#define ROOM_MATCH_Y 104
#define ROOM_ROWS_Y 128
/* --online-keys' `until:room`: the room's page up, with another member */
#define UNTIL_ROOM 100
/* ... `until:message`: a message up */
#define UNTIL_MESSAGE 101

enum {
    ADDRESS_JOIN_AT,
    ADDRESS_JOIN,
    ADDRESS_HOST,
    ADDRESS_BACK,
    ADDRESS_ROWS
};

/* The on-screen keyboard: its rows, a key a letter, but the last row's. */
static const char * const g_keys[] = {
    "ABCDEFGHIJKLM", "NOPQRSTUVWXYZ", "0123456789.:-", "_!?[]"};
#define KEY_ROWS 4
#define KEY_COLUMNS 13
/* the last row's after its letters: Space, Delete, Shift, Done */
#define KEY_SPECIALS 4

static struct {
    s32 is_open;
    s32 wanted; /* the pages open at the next blank */
    s32 page;
    s32 back_to; /* the page a message or the keyboard goes back to */
    s32 cursor[PAGE_MEETING + 1];
    char note[TEXT_BYTES]; /* what went wrong, or what happened */
    u32 note_color;
    /* the keyboard */
    s32 field;
    char typed[WORD_BYTES];
    s32 key_row;
    s32 key_column;
    s32 lower;
    /* what the player has typed, kept while the program runs */
    char password[PASSWORD_LENGTH + 1];
    char room_password[PASSWORD_LENGTH + 1];
    char code[CODE_LENGTH + 1];
    /* a room to host: of several (else of two), and its rotation */
    s32 group;
    s32 rotation;
    /* in a room of several: a match it asks for, and when it begins */
    PortRoomNext room_next;
    u32 room_due;
    s32 room_waiting;
    /* the lobby's rooms */
    PortRoom rooms[ROOMS_MAX];
    s32 room_count;
    s32 total;
    s32 listed;
    s32 pending_room; /* a locked room chosen: its password next */
    char setup[PORT_SETUP_CODE_BYTES];
    /* the meeting: the player's answer in the lobby */
    s32 meeting;
    s32 ready;
    s32 leave;
    char status[TEXT_BYTES];
    char failed[TEXT_BYTES];
    /* the session's hooks the meeting replaces */
    void (*wait_hook)(const char *status);
    void (*fail_hook)(const char *why);
    /* a test's presses (--online-keys), one a turn */
    const char *words;
    s32 waiting; /* turns left of a `wait:N` */
    s32 until;   /* a PORT_SESSION_PHASE_ an `until:` waits for, or -1 */
    u32 shots;
    s32 stick[2];
    /* the pixels */
    u32 *pixels;
    u32 room;
    PortRect place;
    s32 area_w;
    s32 area_h;
    s32 font_ok;
} g_pages = {.until = -1, .rotation = PORT_ROOM_RULE_FIXED};

/* ---- places and letters ---- */

static s32 port_PageX(s32 x) {
    return x * g_pages.area_w / GAME_W - g_pages.place.x;
}

static s32 port_PageY(s32 y) {
    return y * g_pages.area_h / GAME_H - g_pages.place.y;
}

enum {
    ALIGN_LEFT = 0,
    ALIGN_CENTRE = 1,
    ALIGN_RIGHT = 2,
};

/* A line of the page's text, `lines` of the game's lines high, its
 * baseline at the game's `y`: in the port's typeface with a shadow, as the
 * top menu's (top_page.c). */
static void port_PageText(
    s32 x, s32 y, s32 lines, const char *text, u32 color, s32 align) {
    PortTextStyle style;
    s32 size = lines * g_pages.area_h / GAME_H;
    s32 px;

    if (size < 6) {
        size = 6;
    }
    memset(&style, 0, sizeof(style));
    style.face = lines == SIZE_TITLE ? PORT_FACE_TITLE
                 : lines == SIZE_ROW ? PORT_FACE_BOLD
                                     : PORT_FACE_TEXT;
    style.size = size;
    style.color = color;
    style.shadow = COLOR_SHADOW;
    style.shadow_px = size / 14 > 1 ? size / 14 : 1;
    px = port_PageX(x);
    if (align == ALIGN_CENTRE) {
        px -= port_TextMeasure(text, &style) / 2;
    } else if (align == ALIGN_RIGHT) {
        px -= port_TextMeasure(text, &style);
    }
    port_TextDraw(g_pages.pixels, g_pages.place.w, g_pages.place.h, px,
                  port_PageY(y), text, &style);
}

/* A box of the game's places, filled. */
static void port_PageBox(s32 x, s32 y, s32 w, s32 h, u32 color) {
    s32 left = port_PageX(x);
    s32 top = port_PageY(y);

    port_FillRect(g_pages.pixels, g_pages.place.w, g_pages.place.h, left, top,
                  port_PageX(x + w) - left, port_PageY(y + h) - top, color);
}

/* `text` broken between words into lines of at most `width` of the
 * game's places, from `y` down: the lines drawn, at most `most`. */
static s32 port_PageWrapped(
    s32 x, s32 y, s32 width, s32 most, const char *text, u32 color) {
    char line[TEXT_BYTES];
    PortTextStyle style;
    s32 lines = 0;

    memset(&style, 0, sizeof(style));
    style.face = PORT_FACE_TEXT;
    style.size = SIZE_TEXT * g_pages.area_h / GAME_H;
    while (*text != 0 && lines < most) {
        s32 fits = 0;
        s32 end = 0;

        while (*text == ' ') {
            text++;
        }
        for (;;) {
            while (text[end] != 0 && text[end] != ' ' &&
                   end + 1 < (s32)sizeof(line)) {
                end++;
            }
            memcpy(line, text, (size_t)end);
            line[end] = 0;
            if (fits != 0 && port_TextMeasure(line, &style) >
                                 width * g_pages.area_w / GAME_W) {
                break;
            }
            fits = end;
            if (text[end] == 0 || end + 1 >= (s32)sizeof(line)) {
                break;
            }
            end++;
        }
        if (fits == 0) {
            break;
        }
        memcpy(line, text, (size_t)fits);
        line[fits] = 0;
        port_PageText(
            x, y + lines * (SIZE_TEXT + 3), SIZE_TEXT, line, color, ALIGN_LEFT);
        lines++;
        text += fits;
    }
    return lines;
}

/* ---- what the pages show ---- */

static const char *port_PagesSetup(void) {
    if (g_pages.setup[0] == 0) {
        port_SessionSetupCode(g_pages.setup);
    }
    return g_pages.setup;
}

static const char *port_PlayerName(void) {
    return g_port_settings.net_player[0] != 0 ? g_port_settings.net_player
                                              : "Player";
}

/* Which part of a room's setup code is not this program's, in a few
 * words; NULL if none. */
static const char *port_PagesDiffer(const char *theirs) {
    static const char * const parts[] = {
        "another version", "another build", "another disc image",
        "other gameplay mods"};
    const char *mine = port_PagesSetup();
    s32 part = 0;
    s32 i;

    for (i = 0; mine[i] != 0 || theirs[i] != 0; i++) {
        if (mine[i] == '-') {
            part++;
        }
        if (mine[i] != theirs[i]) {
            return parts[part < 4 ? part : 3];
        }
    }
    return NULL;
}

static s32 port_PageRowCount(void) {
    switch (g_pages.page) {
    case PAGE_MAIN:
        return MAIN_ROWS;
    case PAGE_HOST:
        return HOST_ROWS;
    case PAGE_ADDRESS:
        return ADDRESS_ROWS;
    case PAGE_ROOMS:
        /* the rooms, Look again, Back */
        return g_pages.room_count + 2;
    case PAGE_MESSAGE:
        return 1;
    case PAGE_ROOM:
        return ROOM_ROWS;
    case PAGE_MEETING:
        return 2;
    default:
        return 0;
    }
}

/* The open match beside the room's that this member may join: its place
 * among the sides, or -1. */
static s32 port_PageOpenSide(const PortRoomView *view) {
    s32 i;

    for (i = 0; i < view->side_count; i++) {
        if (view->sides[i].b == PORT_ROOM_NOBODY &&
            view->sides[i].a != view->you) {
            return i;
        }
    }
    return -1;
}

/* Whether this member is in a match now, the rotation's or beside it. */
static s32 port_PageIsBusy(const PortRoomView *view) {
    s32 i;

    for (i = 0; i < view->count; i++) {
        if (view->members[i].id == view->you) {
            return (view->members[i].flags & PORT_ROOM_BUSY) != 0;
        }
    }
    return FALSE;
}

/* A row of the room of several's page. */
static void port_PageRoomRow(s32 row, char *name, u32 name_room, char *value,
                             u32 value_room, const char **help) {
    PortRoomView view;
    s32 side;

    port_RoomGet(&view);
    side = port_PageOpenSide(&view);
    switch (row) {
    case ROOM_WATCH:
        snprintf(name, name_room, "Watch the matches");
        snprintf(value, value_room, "%s", view.watching ? "on" : "off");
        *help = "The room's matches you are not in, live, as their players "
                "play them";
        break;
    case ROOM_PLAY:
        snprintf(name, name_room, "Play in the rotation");
        snprintf(
            value, value_room, "%s", view.plays ? "yes" : "no: a spectator");
        *help = "Whether the lobby picks you for the room's matches";
        break;
    case ROOM_RULE:
        snprintf(name, name_room, "Rotation");
        snprintf(value, value_room, "%s%s",
                 view.rule == PORT_ROOM_RULE_WINNER  ? "the winner stays"
                 : view.rule == PORT_ROOM_RULE_FIXED ? "a fixed turn"
                                                     : "none",
                 view.you == view.host ? "" : "   (the host's)");
        *help = view.you == view.host
                    ? "A fixed turn: A+B, B+C, C+A. The winner stays: the "
                      "loser waits"
                    : "Only the room's host sets it";
        break;
    case ROOM_SIDE:
        if (side >= 0) {
            snprintf(name, name_room, "Join a match beside");
            snprintf(
                value, value_room, "%s's", port_RoomPlayer(view.sides[side].a));
            *help = "A match of your own beside the room's, now";
        } else {
            snprintf(name, name_room, "A match beside");
            *help = "A match beside the room's: another member joins it "
                    "from this row";
        }
        if (port_PageIsBusy(&view)) {
            snprintf(value, value_room, "(you are in a match)");
        }
        break;
    default:
        snprintf(name, name_room, "Leave the room");
        *help = "Back to the online pages";
        break;
    }
}

/* A row's name and value, and the line of help under the rows. */
static void port_PageRow(s32 row, char *name, u32 name_room, char *value,
                         u32 value_room, const char **help) {
    value[0] = 0;
    *help = "";
    switch (g_pages.page) {
    case PAGE_MAIN: {
        static const char * const names[] = {
            "Quick Match",     "Host a room", "Browse rooms", "Join by code",
            "Join by address", "Your name",   "Back"};
        static const char * const helps[] = {
            "Joins the first open room that can be joined, or opens one "
            "of your own",
            "A room at the lobby that the other player joins from Browse "
            "rooms or by its code",
            "The rooms open at the lobby now",
            "The six letters and digits the host reads out to you",
            "For those who know their address: host or join without the "
            "lobby",
            "What the lobby and the other player see",
            "Back to the top menu"};

        snprintf(name, name_room, "%s", names[row]);
        *help = helps[row];
        if (row == MAIN_NAME) {
            snprintf(value, value_room, "%s", port_PlayerName());
        }
        break;
    }
    case PAGE_HOST:
        if (row == HOST_NAME) {
            snprintf(name, name_room, "Room's name");
            snprintf(value, value_room, "%s",
                     g_port_settings.net_room[0] != 0 ? g_port_settings.net_room
                                                      : port_PlayerName());
            *help = "What the other player sees in Browse rooms";
        } else if (row == HOST_PASSWORD) {
            snprintf(name, name_room, "Password");
            snprintf(value, value_room, "%s",
                     g_pages.password[0] != 0 ? "(set)" : "none: open to all");
            *help = "Kept only while the program runs; it never goes over "
                    "the network as it is";
        } else if (row == HOST_OPEN) {
            snprintf(name, name_room, "Open the room");
            snprintf(value, value_room, "%s",
                     g_pages.group ? "of several" : "for two players");
            *help = g_pages.group ? "Then the room's page: its members, its "
                                    "matches and who watches"
                                  : "Then wait for the other player here";
        } else if (row == HOST_KIND) {
            snprintf(name, name_room, "Room for");
            snprintf(value, value_room, "%s",
                     g_pages.group ? "several: up to eight, players and "
                                     "spectators"
                                   : "two players");
            *help = "Several: matches in turn, the others watching them";
        } else if (row == HOST_ROTATION) {
            snprintf(name, name_room, "Rotation");
            snprintf(
                value, value_room, "%s",
                !g_pages.group ? "(a room of several's)"
                : g_pages.rotation == PORT_ROOM_RULE_WINNER ? "the winner stays"
                : g_pages.rotation == PORT_ROOM_RULE_FIXED
                    ? "a fixed turn: A+B, B+C, C+A"
                    : "none: matches beside it alone");
            *help = "Who plays next, as the lobby picks it";
        } else {
            snprintf(name, name_room, "Back");
        }
        break;
    case PAGE_ADDRESS:
        if (row == ADDRESS_JOIN_AT) {
            snprintf(name, name_room, "The host's address");
            snprintf(value, value_room, "%s",
                     g_port_settings.net_address[0] != 0
                         ? g_port_settings.net_address
                         : "(type it)");
            *help = "As 192.168.1.20:7845 or [2001:db8::7]:7845";
        } else if (row == ADDRESS_JOIN) {
            snprintf(name, name_room, "Join there");
        } else if (row == ADDRESS_HOST) {
            snprintf(name, name_room, "Host here");
            snprintf(value, value_room, "port %d", g_port_settings.net_port);
            *help = "Wait for the other player at this PC's port (the "
                    "router asked to open it, as the settings say)";
        } else {
            snprintf(name, name_room, "Back");
        }
        break;
    case PAGE_ROOMS:
        if (row < g_pages.room_count) {
            const PortRoom *room = &g_pages.rooms[row];
            const char *differs = port_PagesDiffer(room->setup);

            s32 most = room->group ? PORT_ROOM_MEMBERS : 2;

            snprintf(name, name_room, "%s%s%s", room->locked ? "[locked] " : "",
                     room->group ? "[several] " : "", room->name);
            if (differs != NULL) {
                snprintf(value, value_room, "%s   %d/%d   not for you: %s",
                         room->player, room->players, most, differs);
            } else if (room->players >= most) {
                snprintf(value, value_room, "%s   %d/%d   full", room->player,
                         most, most);
            } else {
                snprintf(value, value_room,
                         "%s   %d/%d   ping ~%u ms   open %u min", room->player,
                         room->players, most, room->ping,
                         room->age / SECONDS_A_MINUTE);
            }
            *help = differs != NULL ? "Its setup code is not yours: it "
                                      "cannot be joined"
                    : room->locked  ? "Locked: its password is asked for"
                    : room->group   ? "Enter this room of several"
                                    : "Join this room";
        } else if (row == g_pages.room_count) {
            snprintf(name, name_room, "Look again");
            snprintf(value, value_room, "%d room%s open", g_pages.total,
                     g_pages.total == 1 ? "" : "s");
        } else {
            snprintf(name, name_room, "Back");
        }
        break;
    case PAGE_MESSAGE:
        snprintf(name, name_room, "OK");
        break;
    case PAGE_ROOM:
        port_PageRoomRow(row, name, name_room, value, value_room, help);
        break;
    default:
        name[0] = 0;
        break;
    }
}

static void port_PageTitle(const char *title) {
    port_PageText(
        GAME_W / 2, TITLE_Y, SIZE_TITLE, title, COLOR_ROW, ALIGN_CENTRE);
    port_PageBox(PANEL_X + 24, TITLE_Y + 6, PANEL_W - 48, 1, COLOR_EDGE);
}

static void port_PageFoot(const char *left) {
    char line[TEXT_BYTES];

    snprintf(line, sizeof(line), "Your setup code %s", port_PagesSetup());
    port_PageText(PANEL_X + 18, FOOT_Y, SIZE_NOTE, left, COLOR_DIM, ALIGN_LEFT);
    port_PageText(PANEL_X + PANEL_W - 18, FOOT_Y, SIZE_NOTE, line, COLOR_DIM,
                  ALIGN_RIGHT);
}

/* The rows of a page with rows, the cursor's lit. */
static void port_PageDrawRows(void) {
    s32 rows = port_PageRowCount();
    s32 first = 0;
    s32 i;
    const char *help = "";

    if (g_pages.cursor[g_pages.page] >= ROWS_SHOWN) {
        first = g_pages.cursor[g_pages.page] - ROWS_SHOWN + 1;
    }
    for (i = first; i < rows && i < first + ROWS_SHOWN; i++) {
        char name[TEXT_BYTES];
        char value[TEXT_BYTES];
        const char *row_help;
        /* (the room's page has its members and matches above its rows) */
        s32 y = (g_pages.page == PAGE_ROOM ? ROOM_ROWS_Y : ROWS_Y) +
                (i - first) * ROW_PITCH;
        s32 is_cursor = i == g_pages.cursor[g_pages.page];
        u32 color = is_cursor ? COLOR_CURSOR : COLOR_ROW;

        port_PageRow(i, name, sizeof(name), value, sizeof(value), &row_help);
        if (g_pages.page == PAGE_ROOMS && i < g_pages.room_count &&
            port_PagesDiffer(g_pages.rooms[i].setup) != NULL) {
            color = is_cursor ? COLOR_CURSOR : COLOR_ROW_OFF;
        }
        if (is_cursor) {
            port_PageBox(
                ROWS_X - 12, y - 10, PANEL_W - 2 * (ROWS_X - 12 - PANEL_X), 13,
                0x40FFEC9CU);
            help = row_help;
        }
        port_PageText(ROWS_X, y, SIZE_ROW, name, color, ALIGN_LEFT);
        if (value[0] != 0) {
            port_PageText(VALUE_X, y, SIZE_TEXT, value,
                          is_cursor ? COLOR_TEXT : COLOR_DIM, ALIGN_LEFT);
        }
    }
    if (g_pages.note[0] != 0) {
        port_PageWrapped(ROWS_X, HELP_Y - 12, PANEL_W - 2 * (ROWS_X - PANEL_X),
                         1, g_pages.note, g_pages.note_color);
    }
    port_PageText(ROWS_X, HELP_Y, SIZE_TEXT, help, COLOR_DIM, ALIGN_LEFT);
}

static const char *port_FieldTitle(s32 field) {
    switch (field) {
    case FIELD_NAME:
        return "Your name";
    case FIELD_ROOM:
        return "The room's name";
    case FIELD_PASSWORD:
        return "The room's password";
    case FIELD_ROOM_PASSWORD:
        return "This room's password";
    case FIELD_CODE:
        return "The room's code";
    default:
        return "The host's address and port";
    }
}

/* The keyboard: what is typed, and the keys, the cursor's lit. */
static void port_PageKeyboard(void) {
    char shown[WORD_BYTES + 2];
    s32 row;
    s32 i;

    port_PageTitle(port_FieldTitle(g_pages.field));
    if (g_pages.field == FIELD_PASSWORD ||
        g_pages.field == FIELD_ROOM_PASSWORD) {
        for (i = 0; g_pages.typed[i] != 0; i++) {
            shown[i] = '*';
        }
        shown[i++] = '_';
        shown[i] = 0;
    } else {
        snprintf(shown, sizeof(shown), "%s_", g_pages.typed);
    }
    port_PageBox(ROWS_X, 52, PANEL_W - 2 * (ROWS_X - PANEL_X), 16, 0xC0000000U);
    port_PageText(ROWS_X + 6, 64, SIZE_ROW, shown, COLOR_TEXT, ALIGN_LEFT);
    for (row = 0; row < KEY_ROWS; row++) {
        s32 count = (s32)strlen(g_keys[row]);
        s32 columns = row == KEY_ROWS - 1 ? count + KEY_SPECIALS : count;

        for (i = 0; i < columns; i++) {
            static const char * const specials[] = {
                "Space", "Delete", "a/A", "Done"};
            char letter[8];
            s32 is_on = row == g_pages.key_row && i == g_pages.key_column;
            s32 x = ROWS_X + i * 36;
            s32 w = 32;
            s32 y = 82 + row * 24;

            if (i >= count) {
                /* the specials, wider */
                x = ROWS_X + count * 36 + (i - count) * 62;
                w = 58;
                snprintf(letter, sizeof(letter), "%s", specials[i - count]);
            } else {
                char c = g_keys[row][i];

                if (g_pages.lower && c >= 'A' && c <= 'Z') {
                    c = (char)(c - 'A' + 'a');
                }
                letter[0] = c;
                letter[1] = 0;
            }
            port_PageBox(x, y, w, 20, is_on ? COLOR_KEY_ON : COLOR_KEY);
            port_PageText(x + w / 2, y + 15, SIZE_ROW, letter,
                          is_on ? COLOR_CURSOR : COLOR_TEXT, ALIGN_CENTRE);
        }
    }
    port_PageText(ROWS_X, HELP_Y, SIZE_TEXT,
                  "Type, or Cross a key.  Square: delete.  Triangle: a/A.  "
                  "Start: done.  Circle: leave",
                  COLOR_DIM, ALIGN_LEFT);
}

/* The room of several: its name and code, its members (who plays, who
 * watches, who is in a match, their wins and their round trips), the
 * rotation's match and the last result, the matches beside it. */
static void port_PageRoom(void) {
    PortRoomView view;
    char line[TEXT_BYTES];
    s32 i;

    port_RoomGet(&view);
    snprintf(line, sizeof(line), "%s   %s", view.name, view.code);
    port_PageTitle(line);
    for (i = 0; i < view.count; i++) {
        const PortRoomMember *member = &view.members[i];
        s32 x = ROWS_X + (i % 2) * ROOM_COLUMN_W;
        s32 y = ROOM_MEMBERS_Y + (i / 2) * ROOM_MEMBER_PITCH;

        snprintf(line, sizeof(line), "%s%s  %s  %d won  %u ms", member->player,
                 member->id == view.you ? " (you)" : "",
                 (member->flags & PORT_ROOM_BUSY)    ? "in a match"
                 : (member->flags & PORT_ROOM_PLAYS) ? "plays"
                                                     : "watches",
                 member->wins, member->rtt);
        port_PageText(
            x, y, SIZE_TEXT, line,
            (member->flags & PORT_ROOM_HOST) ? COLOR_CURSOR : COLOR_TEXT,
            ALIGN_LEFT);
    }
    if (g_pages.room_waiting) {
        snprintf(line, sizeof(line), "%s %s vs %s: in a moment",
                 g_pages.room_next.what == PORT_ROOM_NEXT_WATCH ? "Watching"
                                                                : "Your match,",
                 g_pages.room_next.first, g_pages.room_next.second);
    } else if (view.match.number != 0) {
        snprintf(line, sizeof(line), "Match %d: %s vs %s", view.match.number,
                 port_RoomPlayer(view.match.a), port_RoomPlayer(view.match.b));
    } else {
        snprintf(line, sizeof(line), "%s",
                 view.rule == PORT_ROOM_RULE_OFF
                     ? "No rotation: matches beside the room's alone"
                     : "The next match when two players are free");
    }
    port_PageText(GAME_W / 2, ROOM_MATCH_Y, SIZE_ROW, line,
                  g_pages.room_waiting ? COLOR_GOOD : COLOR_TEXT, ALIGN_CENTRE);
    if (view.last_number != 0) {
        snprintf(line, sizeof(line), "Match %d: %s", view.last_number,
                 view.last_winner == PORT_ROOM_WON_FIRST
                     ? port_RoomPlayer(view.last_a)
                 : view.last_winner == PORT_ROOM_WON_SECOND
                     ? port_RoomPlayer(view.last_b)
                     : "nobody");
        snprintf(line + strlen(line), sizeof(line) - strlen(line), " won");
    } else {
        line[0] = 0;
    }
    for (i = 0; i < view.side_count; i++) {
        snprintf(line + strlen(line), sizeof(line) - strlen(line),
                 "%sBeside: %s vs %s", line[0] != 0 ? "   " : "",
                 port_RoomPlayer(view.sides[i].a),
                 view.sides[i].b == PORT_ROOM_NOBODY
                     ? "(waiting)"
                     : port_RoomPlayer(view.sides[i].b));
    }
    port_PageText(GAME_W / 2, ROOM_MATCH_Y + ROOM_MEMBER_PITCH + 2, SIZE_NOTE,
                  line, COLOR_DIM, ALIGN_CENTRE);
    port_PageDrawRows();
}

/* While the two meet: the host's room and its code and the way, or the
 * guest's progress; then the lobby before the fight. */
static void port_PageMeeting(void) {
    PortSessionLobby lobby;
    char line[TEXT_BYTES];
    s32 is_host;
    s32 y = 66;

    port_GetSessionLobby(&lobby);
    is_host = lobby.role == PORT_SESSION_HOST;
    if (lobby.phase == PORT_SESSION_PHASE_READY) {
        const char *me = lobby.name[0] != 0 ? lobby.name : "You";
        const char *them =
            lobby.peer_name[0] != 0 ? lobby.peer_name : "The other player";

        port_PageTitle("Ready to fight");
        port_PageText(
            GAME_W / 4 + 20, y, SIZE_ROW, me, COLOR_CURSOR, ALIGN_CENTRE);
        port_PageText(
            GAME_W * 3 / 4 - 20, y, SIZE_ROW, them, COLOR_CURSOR, ALIGN_CENTRE);
        port_PageText(GAME_W / 4 + 20, y + 14, SIZE_TEXT,
                      is_host ? "Player 1 (the host)" : "Player 2", COLOR_DIM,
                      ALIGN_CENTRE);
        port_PageText(GAME_W * 3 / 4 - 20, y + 14, SIZE_TEXT,
                      is_host ? "Player 2" : "Player 1 (the host)", COLOR_DIM,
                      ALIGN_CENTRE);
        port_PageText(GAME_W / 4 + 20, y + 30, SIZE_ROW,
                      g_pages.ready ? "Ready" : "Not ready",
                      g_pages.ready ? COLOR_GOOD : COLOR_WARN, ALIGN_CENTRE);
        port_PageText(GAME_W * 3 / 4 - 20, y + 30, SIZE_ROW,
                      lobby.peer_ready ? "Ready" : "Not ready",
                      lobby.peer_ready ? COLOR_GOOD : COLOR_WARN, ALIGN_CENTRE);
        snprintf(line, sizeof(line), "Setup codes match: %s", lobby.setup);
        port_PageText(
            GAME_W / 2, y + 50, SIZE_TEXT, line, COLOR_TEXT, ALIGN_CENTRE);
        port_PageText(GAME_W / 2, y + 62, SIZE_TEXT,
                      "The host's cards, unlocks and settings are both "
                      "players'",
                      COLOR_DIM, ALIGN_CENTRE);
        if (lobby.path[0] != 0) {
            snprintf(line, sizeof(line), "The way between you: %s", lobby.path);
            port_PageText(
                GAME_W / 2, y + 74, SIZE_TEXT, line, COLOR_DIM, ALIGN_CENTRE);
        }
        y = ROWS_Y + 7 * ROW_PITCH - 4;
        port_PageText(
            ROWS_X + 120, y, SIZE_ROW,
            g_pages.ready ? "Ready: waiting for the other" : "Ready",
            g_pages.cursor[PAGE_MEETING] == 0 ? COLOR_CURSOR : COLOR_ROW,
            ALIGN_LEFT);
        port_PageText(
            ROWS_X + 120, y + ROW_PITCH, SIZE_ROW, "Leave",
            g_pages.cursor[PAGE_MEETING] == 1 ? COLOR_CURSOR : COLOR_ROW,
            ALIGN_LEFT);
        port_PageText(ROWS_X, HELP_Y, SIZE_TEXT,
                      "Both ready: on to versus select.  After a fight, Main "
                      "menu ends the session for both.",
                      COLOR_DIM, ALIGN_LEFT);
        return;
    }
    if (is_host && (lobby.phase == PORT_SESSION_PHASE_WAITING ||
                    lobby.phase == PORT_SESSION_PHASE_LOBBY_SERVER)) {
        port_PageTitle("Waiting for a player");
        if (lobby.code[0] != 0) {
            port_PageText(GAME_W / 2, y + 4, SIZE_TEXT,
                          "The room's code, for the other player's Join by "
                          "code:",
                          COLOR_DIM, ALIGN_CENTRE);
            port_PageText(GAME_W / 2, y + 30, SIZE_TITLE + 6, lobby.code,
                          COLOR_CURSOR, ALIGN_CENTRE);
            y += 46;
        }
    } else if (lobby.phase == PORT_SESSION_PHASE_CARDS) {
        port_PageTitle(is_host ? "The other player has come" : "Let in");
        snprintf(line, sizeof(line), "%s the host's memory cards: %d%%",
                 is_host ? "Sending" : "Receiving", lobby.percent);
        port_PageText(
            GAME_W / 2, y + 10, SIZE_ROW, line, COLOR_TEXT, ALIGN_CENTRE);
        y += 24;
    } else if (lobby.phase == PORT_SESSION_PHASE_WATCHING) {
        port_PageTitle("Watching");
    } else if (port_OnlineIsRoomMatch()) {
        port_PageTitle("Your match in the room");
    } else {
        port_PageTitle(is_host ? "Opening the room" : "Joining");
    }
    y = y + 10;
    y += (port_PageWrapped(ROWS_X, y, PANEL_W - 2 * (ROWS_X - PANEL_X), 4,
                           g_pages.status, COLOR_TEXT)) *
         (SIZE_TEXT + 3);
    if (lobby.mapped[0] != 0 && is_host) {
        port_PageWrapped(ROWS_X, y + 6, PANEL_W - 2 * (ROWS_X - PANEL_X), 2,
                         lobby.mapped, COLOR_DIM);
    }
    port_PageText(ROWS_X + 120, ROWS_Y + 8 * ROW_PITCH - 4, SIZE_ROW,
                  lobby.phase == PORT_SESSION_PHASE_WATCHING ? "Stop watching"
                  : is_host                                  ? "Close the room"
                                                             : "Give up",
                  COLOR_CURSOR, ALIGN_LEFT);
    port_PageText(ROWS_X, HELP_Y, SIZE_TEXT,
                  "Circle, Escape or Cross here: back to the online pages",
                  COLOR_DIM, ALIGN_LEFT);
}

static void port_PageDraw(void) {
    s32 window_w = 0;
    s32 window_h = 0;
    u32 need;

    port_GetWindowSize(&window_w, &window_h);
    if (window_w <= 0 || window_h <= 0) {
        return;
    }
    if (!g_pages.font_ok) {
        g_pages.font_ok = port_FontStart();
        if (!g_pages.font_ok) {
            return;
        }
    }
    /* the game's 4:3 picture, as the window shows it */
    g_pages.area_h = window_h;
    g_pages.area_w = window_h * 4 / 3 < window_w ? window_h * 4 / 3 : window_w;
    g_pages.place.x = PANEL_X * g_pages.area_w / GAME_W;
    g_pages.place.y = PANEL_Y * g_pages.area_h / GAME_H;
    g_pages.place.w =
        (PANEL_X + PANEL_W) * g_pages.area_w / GAME_W - g_pages.place.x;
    g_pages.place.h =
        (PANEL_Y + PANEL_H) * g_pages.area_h / GAME_H - g_pages.place.y;
    need = (u32)(g_pages.place.w * g_pages.place.h);
    if (need > g_pages.room) {
        free(g_pages.pixels);
        g_pages.pixels = malloc((size_t)need * sizeof(u32));
        g_pages.room = g_pages.pixels != NULL ? need : 0;
    }
    if (g_pages.pixels == NULL) {
        return;
    }
    for (need = 0; need < (u32)(g_pages.place.w * g_pages.place.h); need++) {
        g_pages.pixels[need] = COLOR_PANEL;
    }
    port_PageBox(PANEL_X, PANEL_Y, PANEL_W, 1, COLOR_EDGE);
    port_PageBox(PANEL_X, PANEL_Y + PANEL_H - 1, PANEL_W, 1, COLOR_EDGE);
    switch (g_pages.page) {
    case PAGE_MAIN:
        port_PageTitle("Online Play");
        port_PageDrawRows();
        port_PageFoot(g_port_settings.net_lobby[0] != 0
                          ? "The lobby: Quick Match, rooms and codes"
                          : "No lobby server is set (the settings' Play "
                            "online page)");
        break;
    case PAGE_HOST:
        port_PageTitle("Host a room");
        port_PageDrawRows();
        port_PageFoot("");
        break;
    case PAGE_ROOMS:
        port_PageTitle("Rooms");
        if (g_pages.room_count == 0) {
            port_PageText(GAME_W / 2, ROWS_Y + 2 * ROW_PITCH + 60, SIZE_TEXT,
                          g_pages.listed ? "No rooms are open just now: "
                                           "Quick Match or Host a room opens "
                                           "one"
                                         : "",
                          COLOR_DIM, ALIGN_CENTRE);
        }
        port_PageDrawRows();
        port_PageFoot("Name   host   players   open for");
        break;
    case PAGE_ADDRESS:
        port_PageTitle("Join by address");
        port_PageDrawRows();
        port_PageFoot("No lobby: the other player's router must let you "
                      "through");
        break;
    case PAGE_KEYBOARD:
        port_PageKeyboard();
        break;
    case PAGE_ROOM:
        port_PageRoom();
        port_PageFoot("A room of several: players and spectators");
        break;
    case PAGE_MESSAGE:
        port_PageTitle("Online Play");
        port_PageWrapped(ROWS_X, ROWS_Y + 4, PANEL_W - 2 * (ROWS_X - PANEL_X),
                         6, g_pages.note, g_pages.note_color);
        port_PageText(GAME_W / 2, ROWS_Y + 8 * ROW_PITCH - 4, SIZE_ROW, "OK",
                      COLOR_CURSOR, ALIGN_CENTRE);
        port_PageFoot("");
        break;
    default:
        port_PageMeeting();
        port_PageFoot("");
        break;
    }
}

/* The page drawn over the game's stopped picture and shown; for a test, a
 * picture of the window first. */
static void port_PageShow(s32 is_shot) {
    char path[TEXT_BYTES];

    port_PageDraw();
    if (g_pages.pixels == NULL) {
        return;
    }
    if (!is_shot || g_port_window_options.menu_shots == NULL) {
        port_ShowMenu(
            port_GetBlankCount(), g_pages.pixels, &g_pages.place, NULL);
        return;
    }
    port_MakeFolder(g_port_window_options.menu_shots);
    snprintf(path, sizeof(path), "%s/online-%03u.png",
             g_port_window_options.menu_shots, g_pages.shots++);
    port_ShowMenu(port_GetBlankCount(), g_pages.pixels, &g_pages.place, path);
    port_Note("online pages: a picture of the window, %s", path);
}

/* ---- the pages' work ---- */

static void port_PageNote(const char *text, u32 color) {
    snprintf(g_pages.note, sizeof(g_pages.note), "%s", text);
    g_pages.note_color = color;
}

static void port_PageMessage(const char *text, u32 color) {
    port_Note("online pages: %s", text);
    port_PageNote(text, color);
    if (g_pages.page != PAGE_MESSAGE) {
        g_pages.back_to = g_pages.page;
    }
    g_pages.page = PAGE_MESSAGE;
}

static void port_PageGo(s32 page) {
    g_pages.page = page;
    g_pages.note[0] = 0;
}

static s32 port_PagesLobby(PortNetAddress *lobby) {
    return g_port_settings.net_lobby[0] != 0 &&
           port_ParseNetAddress(g_port_settings.net_lobby, lobby) &&
           lobby->port != 0;
}

static void port_PagesListRooms(void) {
    PortNetAddress lobby;

    g_pages.room_count = 0;
    g_pages.listed = TRUE;
    if (!port_PagesLobby(&lobby)) {
        port_PageNote("No lobby server is set: the settings menu's Play "
                      "online page has it.",
                      COLOR_WARN);
        return;
    }
    g_pages.room_count =
        port_RelayListRooms(&lobby, g_pages.rooms, ROOMS_MAX, &g_pages.total);
    if (g_pages.room_count < 0) {
        g_pages.room_count = 0;
        g_pages.total = 0;
        port_PageNote("The lobby server did not answer. Is this PC on the "
                      "internet?",
                      COLOR_WARN);
    }
    port_Note("online pages: the lobby has %d rooms", g_pages.total);
}

static void port_PutHex(char *out, const u8 *bytes, u32 count) {
    u32 i;

    for (i = 0; i < count; i++) {
        sprintf(out + 2 * i, "%02x", bytes[i]);
    }
}

/* The session asked for, as the settings menu's page asks it
 * (sdl_main.c, port_StartOnline): TRUE where it begins. */
static s32 port_PagesStart(s32 role, const PortRoom *room, const char *code) {
    static char bind[PORT_NET_ADDRESS_BYTES];
    static char key_text[HEX_BYTES];
    static char code_text[PORT_RELAY_CODE_BYTES];
    PortOnlineStart start;
    s32 is_local = !g_port_settings.net_listen;
    const char *problem;

    memset(&start, 0, sizeof(start));
    start.role = role;
    start.delay = g_port_settings.net_delay;
    start.portmap = g_port_settings.net_portmap && !is_local;
    start.player = port_PlayerName();
    snprintf(
        bind, sizeof(bind), "%s:%d", is_local ? ADDRESS_LOCAL : ADDRESS_ALL,
        role == PORT_NET_ROLE_JOIN || role == PORT_NET_ROLE_ROOMS
            ? (is_local ? 0 : g_port_settings.net_port)
            : g_port_settings.net_port);
    start.bind = bind;
    if (role == PORT_NET_ROLE_ROOM || role == PORT_NET_ROLE_ROOMS) {
        PortNetAddress lobby;

        if (!port_PagesLobby(&lobby)) {
            port_PageMessage("No lobby server is set: the settings menu's "
                             "Play online page has it.",
                             COLOR_WARN);
            return FALSE;
        }
        start.lobby = g_port_settings.net_lobby;
    }
    if (role == PORT_NET_ROLE_ROOM) {
        start.room = g_port_settings.net_room[0] != 0 ? g_port_settings.net_room
                                                      : port_PlayerName();
        if (g_pages.password[0] != 0) {
            u8 salt[PORT_RELAY_SALT_BYTES];
            u8 key[PORT_RELAY_KEY_BYTES];
            u64 seed =
                SDL_GetPerformanceCounter() ^ ((u64)SDL_GetTicks() << 32);
            u32 i;

            for (i = 0; i < sizeof(salt); i++) {
                seed = seed * 6364136223846793005ULL + 1442695040888963407ULL;
                salt[i] = (u8)(seed >> 56);
            }
            port_RelayRoomKey(salt, g_pages.password, key);
            port_PutHex(key_text, salt, sizeof(salt));
            key_text[2 * sizeof(salt)] = ':';
            port_PutHex(key_text + 2 * sizeof(salt) + 1, key, sizeof(key));
            start.key = key_text;
        }
    } else if (role == PORT_NET_ROLE_ROOMS) {
        snprintf(code_text, sizeof(code_text), "%s",
                 room != NULL ? room->code : code);
        start.code = code_text;
        if (room != NULL && room->locked) {
            u8 key[PORT_RELAY_KEY_BYTES];

            port_RelayRoomKey(room->salt, g_pages.room_password, key);
            port_PutHex(key_text, room->salt, PORT_RELAY_SALT_BYTES);
            key_text[2 * PORT_RELAY_SALT_BYTES] = ':';
            port_PutHex(
                key_text + 2 * PORT_RELAY_SALT_BYTES + 1, key, sizeof(key));
            start.key = key_text;
        } else if (room == NULL && g_pages.room_password[0] != 0) {
            /* a code typed: the password, if the room asks for one */
            start.password = g_pages.room_password;
        }
    } else if (role == PORT_NET_ROLE_JOIN) {
        start.address = g_port_settings.net_address;
    }
    problem = port_StartOnline(&start);
    if (problem != NULL) {
        port_PageMessage(problem, COLOR_WARN);
        return FALSE;
    }
    g_pages.is_open = FALSE;
    return TRUE;
}

/* A room of several opened (`code` NULL) or entered: its page, or a
 * message of why not. TRUE where it was. */
static s32 port_PagesRoom(const char *code, const char *password) {
    PortNetAddress lobby;
    char why[TEXT_BYTES];
    s32 ok;

    if (!port_PagesLobby(&lobby)) {
        port_PageMessage("No lobby server is set: the settings menu's Play "
                         "online page has it.",
                         COLOR_WARN);
        return FALSE;
    }
    ok =
        code == NULL
            ? port_RoomOpen(
                  g_port_settings.net_lobby,
                  g_port_settings.net_room[0] != 0 ? g_port_settings.net_room
                                                   : port_PlayerName(),
                  port_PlayerName(), g_pages.password, g_pages.rotation, TRUE,
                  why, sizeof(why))
            : port_RoomEnter(g_port_settings.net_lobby, code, port_PlayerName(),
                             password, TRUE, why, sizeof(why));
    if (!ok) {
        port_PageMessage(why, COLOR_WARN);
        g_pages.back_to = PAGE_MAIN;
        return FALSE;
    }
    port_Note("online pages: in room %s, a room of several",
              code != NULL ? code : "(opened)");
    g_pages.room_waiting = FALSE;
    g_pages.cursor[PAGE_ROOM] = 0;
    port_PageGo(PAGE_ROOM);
    return TRUE;
}

/* What is chosen on the room's page. */
static void port_PagesRoomAccept(s32 row) {
    PortRoomView view;
    s32 side;

    port_RoomGet(&view);
    switch (row) {
    case ROOM_WATCH:
        port_RoomSetWatching(!view.watching);
        break;
    case ROOM_PLAY:
        port_RoomSetPlays(!view.plays);
        break;
    case ROOM_RULE:
        if (view.you != view.host) {
            port_PageNote(
                "Only the room's host sets its rotation.", COLOR_WARN);
            break;
        }
        port_RoomSetRule(
            view.rule == PORT_ROOM_RULE_FIXED    ? PORT_ROOM_RULE_WINNER
            : view.rule == PORT_ROOM_RULE_WINNER ? PORT_ROOM_RULE_OFF
                                                 : PORT_ROOM_RULE_FIXED);
        break;
    case ROOM_SIDE:
        side = port_PageOpenSide(&view);
        if (port_PageIsBusy(&view)) {
            port_PageNote("You are in a match already.", COLOR_WARN);
        } else if (side >= 0) {
            port_RoomSideJoin(side);
        } else {
            port_RoomSideOpen();
            port_PageNote("Your match beside the room's is open: another "
                          "member joins it from this row.",
                          COLOR_GOOD);
        }
        break;
    default:
        port_RoomLeave();
        port_PageGo(PAGE_MAIN);
        break;
    }
}

/* The room's turn while the pages are up: the room stayed in, and a match
 * this member is to play or watch begun, after a moment's notice on the
 * page. */
static void port_PagesRoomTurn(void) {
    PortRoomView view;
    PortRoomNext next;
    PortOnlineAsk how;
    const char *problem;

    if (!port_RoomIsOn()) {
        return;
    }
    port_RoomTick();
    port_RoomGet(&view);
    if (!view.in_room) {
        port_RoomLeave();
        port_PageMessage("You are no longer in the room: the lobby let you "
                         "go (nothing was heard from this PC for a while).",
                         COLOR_WARN);
        g_pages.back_to = PAGE_MAIN;
        return;
    }
    if (g_pages.page != PAGE_ROOM) {
        return;
    }
    if (!g_pages.room_waiting) {
        if (port_RoomNext(&next) == PORT_ROOM_NEXT_NONE) {
            return;
        }
        g_pages.room_next = next;
        g_pages.room_waiting = TRUE;
        g_pages.room_due = SDL_GetTicks() +
                           (g_port_window_options.is_fast ? 0 : ROOM_NOTICE_MS);
        port_Note("online pages: room: %s match %d (%s vs %s) next",
                  next.what == PORT_ROOM_NEXT_WATCH ? "watching" : "playing",
                  next.match.number, next.first, next.second);
        return;
    }
    if ((s32)(SDL_GetTicks() - g_pages.room_due) < 0) {
        return;
    }
    g_pages.room_waiting = FALSE;
    memset(&how, 0, sizeof(how));
    snprintf(how.lobby, sizeof(how.lobby), "%s", g_port_settings.net_lobby);
    snprintf(how.player, sizeof(how.player), "%s", port_PlayerName());
    snprintf(
        how.bind, sizeof(how.bind), "%s",
        g_port_settings.net_listen ? ADDRESS_ALL ":0" : ADDRESS_LOCAL ":0");
    how.delay = (u32)g_port_settings.net_delay;
    problem = port_OnlineAskRoom(&g_pages.room_next, &how);
    if (problem != NULL) {
        port_PageNote(problem, COLOR_WARN);
        return;
    }
    g_pages.is_open = FALSE;
}

/* Quick Match: the first room that can be joined, or one of the player's
 * own. */
static void port_PagesQuickMatch(void) {
    s32 i;

    port_PagesListRooms();
    if (g_pages.note[0] != 0) {
        port_PageMessage(g_pages.note, COLOR_WARN);
        return;
    }
    for (i = 0; i < g_pages.room_count; i++) {
        const PortRoom *room = &g_pages.rooms[i];

        if (!room->locked && room->players < 2 &&
            port_PagesDiffer(room->setup) == NULL) {
            port_Note("online pages: Quick Match joins room %s (%s)",
                      room->code, room->name);
            g_pages.room_password[0] = 0;
            port_PagesStart(PORT_NET_ROLE_ROOMS, room, NULL);
            return;
        }
    }
    port_Note("online pages: Quick Match found no room to join: it opens "
              "one");
    g_pages.password[0] = 0;
    port_PagesStart(PORT_NET_ROLE_ROOM, NULL, NULL);
}

static void port_PagesType(s32 field, const char *start) {
    g_pages.field = field;
    snprintf(g_pages.typed, sizeof(g_pages.typed), "%s", start);
    g_pages.back_to = g_pages.page;
    g_pages.key_row = 0;
    g_pages.key_column = 0;
    g_pages.page = PAGE_KEYBOARD;
}

static u32 port_FieldLength(s32 field) {
    switch (field) {
    case FIELD_NAME:
        return PORT_NET_PLAYER_BYTES - 1;
    case FIELD_ROOM:
        return PORT_NET_ROOM_BYTES - 1;
    case FIELD_CODE:
        return CODE_LENGTH;
    case FIELD_ADDRESS:
        return ADDRESS_LENGTH;
    default:
        return PASSWORD_LENGTH;
    }
}

/* Whether the field takes the letter `c`: a code's are capitals and
 * digits, an address's digits, hex and its marks. */
static s32 port_FieldTakes(s32 field, char c) {
    switch (field) {
    case FIELD_CODE:
        return (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9');
    case FIELD_ADDRESS:
        return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') ||
               (c >= 'A' && c <= 'F') || c == '.' || c == ':' || c == '[' ||
               c == ']';
    case FIELD_PASSWORD:
    case FIELD_ROOM_PASSWORD:
        return c > ' ' && c <= '~';
    default:
        return port_IsNameLetter(c);
    }
}

static void port_KeyboardType(char c) {
    u32 length = (u32)strlen(g_pages.typed);

    if (c == '\b') {
        if (length > 0) {
            g_pages.typed[length - 1] = 0;
        }
        return;
    }
    if (g_pages.field == FIELD_CODE && c >= 'a' && c <= 'z') {
        c = (char)(c - 'a' + 'A');
    }
    if (length < port_FieldLength(g_pages.field) &&
        length + 1 < sizeof(g_pages.typed) &&
        port_FieldTakes(g_pages.field, c)) {
        g_pages.typed[length] = c;
        g_pages.typed[length + 1] = 0;
    }
}

static void port_PagesChooseRoom(s32 which);

/* What was typed kept, and what it was typed for done. */
static void port_KeyboardDone(void) {
    const char *typed = g_pages.typed;

    g_pages.page = g_pages.back_to;
    switch (g_pages.field) {
    case FIELD_NAME:
        snprintf(g_port_settings.net_player, sizeof(g_port_settings.net_player),
                 "%s", typed);
        port_MenuSetLine("net_player", typed);
        break;
    case FIELD_ROOM:
        snprintf(g_port_settings.net_room, sizeof(g_port_settings.net_room),
                 "%s", typed);
        port_MenuSetLine("net_room", typed);
        break;
    case FIELD_PASSWORD:
        snprintf(g_pages.password, sizeof(g_pages.password), "%s", typed);
        break;
    case FIELD_ROOM_PASSWORD:
        snprintf(
            g_pages.room_password, sizeof(g_pages.room_password), "%s", typed);
        if (g_pages.pending_room > 0) {
            s32 which = g_pages.pending_room - 1;

            g_pages.pending_room = 0;
            if (g_pages.rooms[which].group) {
                port_PagesRoom(
                    g_pages.rooms[which].code, g_pages.room_password);
            } else {
                port_PagesStart(
                    PORT_NET_ROLE_ROOMS, &g_pages.rooms[which], NULL);
            }
        } else if (g_pages.code[0] != 0) {
            port_PagesStart(PORT_NET_ROLE_ROOMS, NULL, g_pages.code);
        }
        break;
    case FIELD_CODE:
        if (strlen(typed) != CODE_LENGTH) {
            port_PageMessage(
                "A room's code is six letters and digits.", COLOR_WARN);
            break;
        }
        snprintf(g_pages.code, sizeof(g_pages.code), "%s", typed);
        g_pages.room_password[0] = 0;
        /* a room of several's code: the lobby's list says which it is */
        port_PagesListRooms();
        g_pages.note[0] = 0;
        {
            s32 i;

            for (i = 0; i < g_pages.room_count; i++) {
                if (g_pages.rooms[i].group &&
                    strcmp(g_pages.rooms[i].code, g_pages.code) == 0) {
                    port_PagesChooseRoom(i);
                    return;
                }
            }
        }
        port_PagesStart(PORT_NET_ROLE_ROOMS, NULL, g_pages.code);
        break;
    default: {
        PortNetAddress at;

        if (!port_ParseNetAddress(typed, &at) || at.port == 0) {
            port_PageMessage("That is not an address and a port, as "
                             "192.168.1.20:7845.",
                             COLOR_WARN);
            break;
        }
        snprintf(g_port_settings.net_address,
                 sizeof(g_port_settings.net_address), "%s", typed);
        port_MenuSetLine("net_address", typed);
        break;
    }
    }
}

static void port_KeyboardPress(s32 press) {
    s32 count = (s32)strlen(g_keys[g_pages.key_row]);
    s32 columns =
        g_pages.key_row == KEY_ROWS - 1 ? count + KEY_SPECIALS : count;

    switch (press) {
    case PRESS_UP:
        g_pages.key_row = (g_pages.key_row + KEY_ROWS - 1) % KEY_ROWS;
        break;
    case PRESS_DOWN:
        g_pages.key_row = (g_pages.key_row + 1) % KEY_ROWS;
        break;
    case PRESS_LEFT:
        g_pages.key_column = (g_pages.key_column + columns - 1) % columns;
        break;
    case PRESS_RIGHT:
        g_pages.key_column = (g_pages.key_column + 1) % columns;
        break;
    case PRESS_DELETE:
        port_KeyboardType('\b');
        break;
    case PRESS_SHIFT:
        g_pages.lower = !g_pages.lower;
        break;
    case PRESS_DONE:
        port_KeyboardDone();
        return;
    case PRESS_BACK:
        g_pages.page = g_pages.back_to;
        g_pages.pending_room = 0;
        return;
    case PRESS_ACCEPT:
        if (g_pages.key_column < count) {
            char c = g_keys[g_pages.key_row][g_pages.key_column];

            if (g_pages.lower && c >= 'A' && c <= 'Z') {
                c = (char)(c - 'A' + 'a');
            }
            port_KeyboardType(c);
        } else {
            s32 special = g_pages.key_column - count;

            if (special == 0) {
                port_KeyboardType(' ');
            } else if (special == 1) {
                port_KeyboardType('\b');
            } else if (special == 2) {
                g_pages.lower = !g_pages.lower;
            } else {
                port_KeyboardDone();
                return;
            }
        }
        break;
    default:
        break;
    }
    count = (s32)strlen(g_keys[g_pages.key_row]);
    columns = g_pages.key_row == KEY_ROWS - 1 ? count + KEY_SPECIALS : count;
    if (g_pages.key_column >= columns) {
        g_pages.key_column = columns - 1;
    }
}

static void port_PagesChooseRoom(s32 which) {
    const PortRoom *room = &g_pages.rooms[which];
    const char *differs = port_PagesDiffer(room->setup);
    char text[TEXT_BYTES];

    if (differs != NULL) {
        snprintf(text, sizeof(text),
                 "Room %s is not for you: %s (its setup code %s, yours %s).",
                 room->name, differs, room->setup, port_PagesSetup());
        port_PageNote(text, COLOR_WARN);
        return;
    }
    if (room->players >= (room->group ? PORT_ROOM_MEMBERS : 2)) {
        port_PageNote("That room is full.", COLOR_WARN);
        return;
    }
    if (room->locked) {
        g_pages.pending_room = which + 1;
        port_PagesType(FIELD_ROOM_PASSWORD, "");
        return;
    }
    g_pages.room_password[0] = 0;
    if (room->group) {
        port_PagesRoom(room->code, "");
        return;
    }
    port_PagesStart(PORT_NET_ROLE_ROOMS, room, NULL);
}

static void port_PagesAccept(void) {
    s32 row = g_pages.cursor[g_pages.page];

    switch (g_pages.page) {
    case PAGE_MAIN:
        switch (row) {
        case MAIN_QUICK:
            port_PagesQuickMatch();
            break;
        case MAIN_HOST:
            port_PageGo(PAGE_HOST);
            break;
        case MAIN_BROWSE:
            port_PageGo(PAGE_ROOMS);
            port_PagesListRooms();
            break;
        case MAIN_CODE:
            port_PagesType(FIELD_CODE, "");
            break;
        case MAIN_ADDRESS:
            port_PageGo(PAGE_ADDRESS);
            break;
        case MAIN_NAME:
            port_PagesType(FIELD_NAME, g_port_settings.net_player);
            break;
        default:
            g_pages.is_open = FALSE;
            break;
        }
        break;
    case PAGE_HOST:
        if (row == HOST_NAME) {
            port_PagesType(FIELD_ROOM, g_port_settings.net_room);
        } else if (row == HOST_PASSWORD) {
            port_PagesType(FIELD_PASSWORD, g_pages.password);
        } else if (row == HOST_OPEN && g_pages.group) {
            port_PagesRoom(NULL, NULL);
        } else if (row == HOST_OPEN) {
            port_PagesStart(PORT_NET_ROLE_ROOM, NULL, NULL);
        } else if (row == HOST_KIND) {
            g_pages.group = !g_pages.group;
        } else if (row == HOST_ROTATION) {
            g_pages.rotation =
                (g_pages.rotation + 1) % (PORT_ROOM_RULE_WINNER + 1);
        } else {
            port_PageGo(PAGE_MAIN);
        }
        break;
    case PAGE_ROOM:
        port_PagesRoomAccept(row);
        break;
    case PAGE_ADDRESS:
        if (row == ADDRESS_JOIN_AT) {
            port_PagesType(FIELD_ADDRESS, g_port_settings.net_address);
        } else if (row == ADDRESS_JOIN) {
            port_PagesStart(PORT_NET_ROLE_JOIN, NULL, NULL);
        } else if (row == ADDRESS_HOST) {
            port_PagesStart(PORT_NET_ROLE_HOST, NULL, NULL);
        } else {
            port_PageGo(PAGE_MAIN);
        }
        break;
    case PAGE_ROOMS:
        if (row < g_pages.room_count) {
            port_PagesChooseRoom(row);
        } else if (row == g_pages.room_count) {
            g_pages.note[0] = 0;
            port_PagesListRooms();
        } else {
            port_PageGo(PAGE_MAIN);
        }
        break;
    case PAGE_MESSAGE:
        g_pages.page = g_pages.back_to;
        g_pages.note[0] = 0;
        break;
    default:
        break;
    }
}

static void port_PagesPress(s32 press) {
    s32 rows = port_PageRowCount();
    s32 page = g_pages.page;
    s32 *cursor = &g_pages.cursor[page];

    if (press == PRESS_NONE) {
        return;
    }
    if (g_pages.page == PAGE_KEYBOARD) {
        port_KeyboardPress(press);
        return;
    }
    if (press == PRESS_UP && rows > 0) {
        *cursor = (*cursor + rows - 1) % rows;
    } else if (press == PRESS_DOWN && rows > 0) {
        *cursor = (*cursor + 1) % rows;
    } else if (press == PRESS_ACCEPT || press == PRESS_DONE) {
        port_PagesAccept();
    } else if (press == PRESS_BACK) {
        if (g_pages.page == PAGE_MAIN) {
            g_pages.is_open = FALSE;
        } else if (g_pages.page == PAGE_ROOM) {
            /* nobody leaves a room by a slip: Back goes to its Leave row */
            *cursor = ROOM_LEAVE;
        } else if (g_pages.page == PAGE_MESSAGE) {
            g_pages.page = g_pages.back_to;
            g_pages.note[0] = 0;
        } else {
            port_PageGo(PAGE_MAIN);
        }
    }
    if (g_pages.page == page && *cursor >= port_PageRowCount()) {
        *cursor = 0;
    }
}

/* ---- the PC's input while they are up ---- */

/* A key typed into the keyboard page: TRUE if it was one. */
static s32 port_PagesTypedKey(SDL_Scancode code) {
    s32 shift = (SDL_GetModState() & KMOD_SHIFT) != 0;
    char c = 0;

    if (code >= SDL_SCANCODE_A && code <= SDL_SCANCODE_Z) {
        c = (char)((shift ? 'A' : 'a') + (code - SDL_SCANCODE_A));
    } else if (code == SDL_SCANCODE_1 && shift) {
        c = '!';
    } else if (code == SDL_SCANCODE_SLASH && shift) {
        c = '?';
    } else if (code == SDL_SCANCODE_MINUS) {
        c = shift ? '_' : '-';
    } else if (code == SDL_SCANCODE_LEFTBRACKET) {
        c = '[';
    } else if (code == SDL_SCANCODE_RIGHTBRACKET) {
        c = ']';
    } else if (code == SDL_SCANCODE_SPACE) {
        c = ' ';
    } else if (code >= SDL_SCANCODE_1 && code <= SDL_SCANCODE_9) {
        c = (char)('1' + (code - SDL_SCANCODE_1));
    } else if (code == SDL_SCANCODE_0 || code == SDL_SCANCODE_KP_0) {
        c = '0';
    } else if (code >= SDL_SCANCODE_KP_1 && code <= SDL_SCANCODE_KP_9) {
        c = (char)('1' + (code - SDL_SCANCODE_KP_1));
    } else if (code == SDL_SCANCODE_PERIOD || code == SDL_SCANCODE_KP_PERIOD) {
        c = '.';
    } else if (code == SDL_SCANCODE_SEMICOLON) {
        c = ':';
    } else if (code == SDL_SCANCODE_BACKSPACE) {
        c = '\b';
    } else {
        return FALSE;
    }
    port_KeyboardType(c);
    return TRUE;
}

static s32 port_PagesKey(const SDL_KeyboardEvent *key) {
    SDL_Scancode code = key->keysym.scancode;

    if (key->type != SDL_KEYDOWN) {
        return PRESS_NONE;
    }
    if (g_pages.page == PAGE_KEYBOARD && !g_pages.meeting &&
        port_PagesTypedKey(code)) {
        return PRESS_NONE;
    }
    switch (code) {
    case SDL_SCANCODE_UP:
        return PRESS_UP;
    case SDL_SCANCODE_DOWN:
        return PRESS_DOWN;
    case SDL_SCANCODE_LEFT:
        return PRESS_LEFT;
    case SDL_SCANCODE_RIGHT:
        return PRESS_RIGHT;
    case SDL_SCANCODE_RETURN:
    case SDL_SCANCODE_KP_ENTER:
        if (key->repeat != 0) {
            return PRESS_NONE;
        }
        if ((key->keysym.mod & KMOD_ALT) != 0) {
            port_ToggleFullscreen();
            return PRESS_NONE;
        }
        return g_pages.page == PAGE_KEYBOARD ? PRESS_DONE : PRESS_ACCEPT;
    case SDL_SCANCODE_SPACE:
        return key->repeat == 0 ? PRESS_ACCEPT : PRESS_NONE;
    case SDL_SCANCODE_ESCAPE:
    case SDL_SCANCODE_BACKSPACE:
        return key->repeat == 0 ? PRESS_BACK : PRESS_NONE;
    case SDL_SCANCODE_F11:
        port_ToggleFullscreen();
        return PRESS_NONE;
    default:
        return PRESS_NONE;
    }
}

/* A controller's button, by where it is, as the PlayStation's are. */
static s32 port_PagesButton(s32 button) {
    switch (button) {
    case SDL_CONTROLLER_BUTTON_DPAD_UP:
        return PRESS_UP;
    case SDL_CONTROLLER_BUTTON_DPAD_DOWN:
        return PRESS_DOWN;
    case SDL_CONTROLLER_BUTTON_DPAD_LEFT:
        return PRESS_LEFT;
    case SDL_CONTROLLER_BUTTON_DPAD_RIGHT:
        return PRESS_RIGHT;
    case SDL_CONTROLLER_BUTTON_A:
        return PRESS_ACCEPT;
    case SDL_CONTROLLER_BUTTON_B:
        return PRESS_BACK;
    case SDL_CONTROLLER_BUTTON_X:
        return PRESS_DELETE;
    case SDL_CONTROLLER_BUTTON_Y:
        return PRESS_SHIFT;
    case SDL_CONTROLLER_BUTTON_START:
        return PRESS_DONE;
    default:
        return PRESS_NONE;
    }
}

/* An event while the pages are up: a press, or none. What the keys and
 * controllers hold is followed all the same, as the settings menu does,
 * so that nothing still held reaches the game after (port_HoldBackPads). */
static s32 port_PagesEvent(const SDL_Event *event) {
    s32 press = PRESS_NONE;
    s32 pad;

    switch (event->type) {
    case SDL_QUIT:
        port_Quit(PORT_EXIT_OK);
    case SDL_KEYDOWN:
    case SDL_KEYUP:
        press = port_PagesKey(&event->key);
        if (event->type == SDL_KEYUP &&
            event->key.keysym.scancode == SDL_SCANCODE_ESCAPE) {
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
            press = port_PagesButton(event->cbutton.button);
        }
        pad = port_PadOfController(event->cbutton.which);
        if (pad >= 0) {
            port_ApplyInput(
                &g_port_settings, PORT_SOURCE_BUTTON, pad,
                event->cbutton.button, event->cbutton.state == SDL_PRESSED);
        }
        break;
    case SDL_CONTROLLERAXISMOTION: {
        s32 axis = event->caxis.axis;
        s32 which = axis == SDL_CONTROLLER_AXIS_LEFTX ? 0 : 1;
        s32 now = event->caxis.value > STICK    ? 1
                  : event->caxis.value < -STICK ? -1
                                                : 0;

        if ((axis == SDL_CONTROLLER_AXIS_LEFTX ||
             axis == SDL_CONTROLLER_AXIS_LEFTY) &&
            now != 0 && now != g_pages.stick[which]) {
            press = which == 0 ? (now > 0 ? PRESS_RIGHT : PRESS_LEFT)
                               : (now > 0 ? PRESS_DOWN : PRESS_UP);
        }
        if (axis == SDL_CONTROLLER_AXIS_LEFTX ||
            axis == SDL_CONTROLLER_AXIS_LEFTY) {
            g_pages.stick[which] = now;
        }
        pad = port_PadOfController(event->caxis.which);
        if (pad >= 0) {
            port_ApplyInput(&g_port_settings, PORT_SOURCE_AXIS, pad,
                            event->caxis.axis, event->caxis.value);
        }
        break;
    }
    default:
        break;
    }
    return press;
}

/* A test's next word (--online-keys): a press, `type:TEXT`, `shot`, or a
 * wait (`wait:N` turns, `until:lobby|waiting|cards`). *is_shot where a
 * picture is wanted. */
static s32 port_PagesWord(s32 *is_shot) {
    char word[WORD_BYTES];
    u32 length = 0;

    static const struct {
        const char *name;
        s32 press;
    } presses[] = {{"up", PRESS_UP},         {"down", PRESS_DOWN},
                   {"left", PRESS_LEFT},     {"right", PRESS_RIGHT},
                   {"accept", PRESS_ACCEPT}, {"back", PRESS_BACK},
                   {"delete", PRESS_DELETE}, {"shift", PRESS_SHIFT},
                   {"done", PRESS_DONE}};

    u32 i;

    *is_shot = FALSE;
    if (g_pages.waiting > 0) {
        g_pages.waiting--;
        return PRESS_NONE;
    }
    if (g_pages.until == UNTIL_MESSAGE) {
        /* a message up on the pages (a session's end, a refusal) */
        if (g_pages.page != PAGE_MESSAGE || !g_pages.is_open) {
            return PRESS_NONE;
        }
        g_pages.until = -1;
    } else if (g_pages.until == UNTIL_ROOM) {
        /* a room of several's page up, with someone else in the room */
        PortRoomView view;

        port_RoomGet(&view);
        if (g_pages.page != PAGE_ROOM || !g_pages.is_open || view.count < 2) {
            return PRESS_NONE;
        }
        g_pages.until = -1;
    } else if (g_pages.until >= 0) {
        PortSessionLobby lobby;

        port_GetSessionLobby(&lobby);
        if (lobby.phase < g_pages.until) {
            return PRESS_NONE;
        }
        g_pages.until = -1;
    }
    if (g_pages.words == NULL) {
        return PRESS_NONE;
    }
    while (*g_pages.words == ' ' || *g_pages.words == ',') {
        g_pages.words++;
    }
    while (
        *g_pages.words != 0 && *g_pages.words != ' ' && *g_pages.words != ',') {
        if (length < sizeof(word) - 1) {
            word[length++] = *g_pages.words;
        }
        g_pages.words++;
    }
    word[length] = 0;
    if (length == 0) {
        g_pages.words = NULL;
        return PRESS_NONE;
    }
    port_Note("online pages: --online-keys %s", word);
    if (strcmp(word, "shot") == 0) {
        *is_shot = TRUE;
        return PRESS_NONE;
    }
    if (strncmp(word, "type:", 5) == 0) {
        for (i = 5; word[i] != 0; i++) {
            port_KeyboardType(word[i]);
        }
        return PRESS_NONE;
    }
    if (strncmp(word, "wait:", 5) == 0) {
        g_pages.waiting = atoi(word + 5);
        return PRESS_NONE;
    }
    if (strncmp(word, "until:", 6) == 0) {
        g_pages.until =
            strcmp(word + 6, "lobby") == 0     ? PORT_SESSION_PHASE_READY
            : strcmp(word + 6, "cards") == 0   ? PORT_SESSION_PHASE_CARDS
            : strcmp(word + 6, "room") == 0    ? UNTIL_ROOM
            : strcmp(word + 6, "message") == 0 ? UNTIL_MESSAGE
                                               : PORT_SESSION_PHASE_WAITING;
        return PRESS_NONE;
    }
    for (i = 0; i < sizeof(presses) / sizeof(presses[0]); i++) {
        if (strcmp(word, presses[i].name) == 0) {
            return presses[i].press;
        }
    }
    port_Say("--online-keys: no such word: %s", word);
    return PRESS_NONE;
}

/* A test's words (--online-keys), taken where the pages or a meeting
 * first come up: the settings menu's Play online page meets with no
 * pages before. */
static void port_PagesTakeWords(void) {
    if (g_pages.words == NULL) {
        g_pages.words = g_port_window_options.online_keys;
        g_port_window_options.online_keys = NULL;
    }
}

/* ---- the loop ---- */

/* The pages, from the moment they open to the moment they close or a
 * session is asked for. Nothing of the game's runs in here. */
static void port_RunPages(u32 blank) {
    u32 started = SDL_GetTicks();

    g_pages.is_open = TRUE;
    g_pages.wanted = FALSE;
    port_PagesTakeWords();
    /* nothing downloads or asks while online play is up (update.h) */
    port_UpdateSetOnline(TRUE);
    port_Note("online pages: open; the game is stopped at blank %u", blank);
    while (g_pages.is_open) {
        SDL_Event event;
        s32 is_shot = FALSE;

        while (SDL_PollEvent(&event)) {
            port_PagesPress(port_PagesEvent(&event));
        }
        if (g_pages.is_open) {
            port_PagesPress(port_PagesWord(&is_shot));
        }
        if (g_pages.is_open) {
            port_PagesRoomTurn();
        }
        if (!g_pages.is_open) {
            break;
        }
        port_PageShow(is_shot);
        if (!g_port_window_options.is_fast) {
            SDL_Delay(STEP_MS);
        }
    }
    port_HoldBackPads();
    if (!port_OnlineIsBusy()) {
        /* the updater may look again (update.h) */
        port_UpdateSetOnline(FALSE);
    }
    port_Note("online pages: closed after %u ms%s", SDL_GetTicks() - started,
              port_OnlineIsBusy() ? "; a session begins at the game's next "
                                    "boundary"
                                  : "");
}

/* ---- the meeting, at the game's boundary (online.c) ---- */

/* The session's status, now and then: the page drawn, the player's
 * answer read. */
static void port_PagesWait(const char *status) {
    SDL_Event event;
    PortSessionLobby lobby;
    s32 is_shot = FALSE;
    s32 press = PRESS_NONE;

    if (strcmp(status, g_pages.status) != 0) {
        snprintf(g_pages.status, sizeof(g_pages.status), "%s", status);
        port_Note("netplay: %s", status);
    }
    while (SDL_PollEvent(&event)) {
        s32 got = port_PagesEvent(&event);

        if (got != PRESS_NONE) {
            press = got;
        }
    }
    if (press == PRESS_NONE) {
        press = port_PagesWord(&is_shot);
    }
    port_GetSessionLobby(&lobby);
    if (lobby.phase == PORT_SESSION_PHASE_READY) {
        s32 *cursor = &g_pages.cursor[PAGE_MEETING];

        if (press == PRESS_UP || press == PRESS_DOWN) {
            *cursor = 1 - *cursor;
        } else if (press == PRESS_ACCEPT || press == PRESS_DONE) {
            if (*cursor == 0) {
                g_pages.ready = TRUE;
            } else {
                g_pages.leave = TRUE;
            }
        } else if (press == PRESS_BACK) {
            g_pages.leave = TRUE;
        }
    } else if (press == PRESS_BACK || press == PRESS_ACCEPT) {
        port_CancelSession();
    }
    port_PageShow(is_shot);
}

static void port_PagesFail(const char *why) {
    port_Note("netplay: could not play online: %s", why);
    snprintf(g_pages.failed, sizeof(g_pages.failed), "%s", why);
}

static s32 port_PagesReady(void) {
    if (port_OnlineIsRoomMatch() && !g_pages.leave) {
        /* a room's match: the room has decided, nobody says ready */
        return PORT_SESSION_READY;
    }
    return g_pages.leave   ? PORT_SESSION_LEAVE
           : g_pages.ready ? PORT_SESSION_READY
                           : PORT_SESSION_NOT_YET;
}

/* What the session begun in the program says (port_online.h). */
static void port_PagesOnline(s32 event, const char *why) {
    switch (event) {
    case PORT_ONLINE_MEETING:
        port_UpdateSetOnline(TRUE);
        port_PagesTakeWords();
        g_pages.wait_hook = g_port_net_wait_hook;
        g_pages.fail_hook = g_port_net_fail_hook;
        g_port_net_wait_hook = port_PagesWait;
        g_port_net_fail_hook = port_PagesFail;
        g_port_session_ready_hook = port_PagesReady;
        g_pages.meeting = TRUE;
        g_pages.ready = FALSE;
        g_pages.leave = FALSE;
        g_pages.failed[0] = 0;
        g_pages.status[0] = 0;
        g_pages.cursor[PAGE_MEETING] = 0;
        g_pages.page = PAGE_MEETING;
        port_PageShow(FALSE);
        break;
    case PORT_ONLINE_BEGINS:
        g_port_net_wait_hook = g_pages.wait_hook;
        g_port_net_fail_hook = g_pages.fail_hook;
        g_port_session_ready_hook = NULL;
        g_pages.meeting = FALSE;
        g_pages.page = PAGE_MAIN;
        port_HoldBackPads();
        port_WindowStateReplaced();
        port_Say("online: the session begins in this program: the game from "
                 "power on, as both players' machines");
        break;
    case PORT_ONLINE_ENDS:
        port_WindowStateReplaced();
        port_HoldBackPads();
        port_PageMessage(
            why != NULL ? why : "The session is over.", COLOR_TEXT);
        /* a room's match over: back to the room (the command line's has
         * no pages) */
        g_pages.back_to = port_RoomIsOn() ? PAGE_ROOM : PAGE_MAIN;
        g_pages.wanted = !port_OnlineIsRoomAuto();
        port_Say("online: the session has ended: %s; back to your own game",
                 why != NULL ? why : "");
        break;
    default:
        g_port_net_wait_hook = g_pages.wait_hook;
        g_port_net_fail_hook = g_pages.fail_hook;
        g_port_session_ready_hook = NULL;
        g_pages.meeting = FALSE;
        port_HoldBackPads();
        g_pages.page = port_RoomIsOn() ? PAGE_ROOM : PAGE_MAIN;
        if (why != NULL) {
            port_PageMessage(
                g_pages.failed[0] != 0 ? g_pages.failed
                : port_RoomIsOn()      ? why
                                       : "The session could not be set up: the "
                                         "log says why.",
                COLOR_WARN);
            g_pages.back_to = port_RoomIsOn() ? PAGE_ROOM : PAGE_MAIN;
        }
        g_pages.wanted = !port_OnlineIsRoomAuto();
        break;
    }
}

/* ---- what the window asks ---- */

void port_StartOnlinePages(void) { g_port_online_hook = port_PagesOnline; }

void port_AskForOnlinePages(const char *why) {
    if (g_pages.is_open || port_OnlineIsBusy() || port_IsNetplayOn()) {
        port_Note("online pages: asked for by %s; not now", why);
        return;
    }
    port_Note("online pages: asked for by %s", why);
    g_pages.page = PAGE_MAIN;
    g_pages.note[0] = 0;
    g_pages.wanted = TRUE;
}

s32 port_MaybeRunOnlinePages(u32 blank) {
    if (!g_pages.wanted || port_IsNetplayOn() || port_OnlineIsBusy()) {
        return FALSE;
    }
    port_RunPages(blank);
    return TRUE;
}
