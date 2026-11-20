/* The native port's settings menu (menu.h; docs/PLAYING.md, "The settings
 * menu"): its pages, what a press does on each, the settings file kept in
 * step, and the menu drawn into pixels the window lays over the game's
 * picture. Only the build with a window has this file; it holds nothing
 * of the game's state (sdl_window.c stops the game while it is open). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "menu.h"
#include "shared.h"
#include "sdl_window.h"

/* The settings file's text, as large as the program reads one. */
#define TEXT_BYTES 0x10000
/* how much room the text is given to grow in, a line at a time */
#define TEXT_ROOM (TEXT_BYTES + 0x4000)
/* a page's rows: the most is a group's, its settings and Back */
#define ROWS_MAX 96
#define LABEL_BYTES 160
#define VALUE_BYTES 160
#define BUTTONS 14
/* the most settings whose values as the program started are kept */
#define CHOICES_KEPT 64

/* The colours, 0xAARRGGBB. */
#define COLOR_PANEL 0xF60E121EU
#define COLOR_EDGE 0xFF6A5530U
#define COLOR_TITLE 0xFFE6C877U
#define COLOR_TEXT 0xFFEDEDF2U
#define COLOR_DIM 0xFF8E96AAU
#define COLOR_BAR 0xFF26304AU
#define COLOR_BAR_EDGE 0xFFE6C877U
#define COLOR_NOTE 0xFFF0A848U
#define COLOR_CAPTURE 0xF0080A12U
#define COLOR_DEFAULT 0xFF6E7A92U
#define COLOR_CHANGED 0xFFE6C877U

/* What marks the value built in, beside the cursor's setting. */
#define MENU_BUILT_IN "default"

enum {
    PAGE_MAIN,    /* the groups, Play online, back to the game, Quit */
    PAGE_GROUP,   /* one group's settings */
    PAGE_BUTTONS, /* one pad's */
    PAGE_ONLINE,  /* Play online: host or join a session of netplay */
};

/* What can be typed into a row of the Play online page. */
#define TYPED_BYTES PORT_NET_ADDRESS_BYTES
#define NET_ADDRESS_ALL "0.0.0.0"
#define NET_ADDRESS_LOCAL "127.0.0.1"
#define DECIMAL 10

/* What a row of a page is. */
enum {
    ROW_GROUP,   /* a group's page: `index` the group */
    ROW_CHOICE,  /* a setting of port_FindChoice: `index` */
    ROW_BUTTONS, /* a pad's buttons: `index` the pad */
    ROW_CLOSE,
    ROW_BUTTON, /* a button of the pad: `index` into g_port_menu_buttons */
    ROW_RESET,  /* the pad's buttons back to the layout's */
    ROW_BACK,
    ROW_ONLINE, /* the Play online page */
    /* the Play online page's */
    ROW_NET_ROLE,
    ROW_NET_ADDRESS, /* the host to join: typed */
    ROW_NET_PORT,    /* the port a host waits at */
    ROW_NET_LISTEN,
    ROW_NET_DELAY,
    ROW_NET_START,
    ROW_QUIT, /* the program ended, the settings kept */
};

typedef struct {
    s32 kind;
    s32 index;
} MenuRow;

/* The pads' buttons in the order the page shows them, and their names in
 * the file (window.h, port_FindPadButton) and on the page. */
static const char * const g_port_menu_buttons[BUTTONS][2] = {
    {"up", "Up"},         {"down", "Down"},
    {"left", "Left"},     {"right", "Right"},
    {"cross", "Cross"},   {"circle", "Circle"},
    {"square", "Square"}, {"triangle", "Triangle"},
    {"l1", "L1"},         {"r1", "R1"},
    {"l2", "L2"},         {"r2", "R2"},
    {"start", "Start"},   {"select", "Select"},
};

static struct {
    s32 is_open;
    s32 page;
    s32 group; /* PAGE_GROUP's */
    s32 pad;   /* PAGE_BUTTONS's */
    MenuRow rows[ROWS_MAX];
    s32 row_count;
    s32 cursor;
    s32 main_cursor;  /* where the cursor was on the first page */
    s32 group_cursor; /* ... and on the group's page */
    /* the page the next opening is on, and this one's (PORT_MENU_OPEN_...) */
    s32 open_on;
    s32 opened_on;
    s32 first; /* the first row in view */
    s32 is_capturing;
    /* a row of the Play online page being typed into, and what is typed */
    s32 is_typing;
    char typed[TYPED_BYTES];
    s32 changed; /* the file's text was changed since it was read */
    char note[LABEL_BYTES];
    char path[1024];
    char *text;
    /* each choice's value as the program started, for what waits on a
     * restart */
    s32 has_start;
    s32 start[CHOICES_KEPT];
    s32 font_ok;
} g_menu;

/* Defined further down. */
static void port_MenuClose(void);

/* ---- the pages ---- */

static void port_MenuAddRow(s32 kind, s32 index) {
    if (g_menu.row_count < ROWS_MAX) {
        g_menu.rows[g_menu.row_count].kind = kind;
        g_menu.rows[g_menu.row_count].index = index;
        g_menu.row_count++;
    }
}

static void port_MenuBuild(s32 page, s32 cursor) {
    s32 group;
    s32 i;

    g_menu.page = page;
    g_menu.row_count = 0;
    if (page == PAGE_MAIN) {
        for (group = 0; group < PORT_CHOICE_GROUPS; group++) {
            if (port_FindChoiceGroup(group)->count > 0 ||
                group == PORT_MENU_CONTROLS - 1) {
                port_MenuAddRow(ROW_GROUP, group);
            }
        }
        port_MenuAddRow(ROW_ONLINE, 0);
        port_MenuAddRow(ROW_CLOSE, 0);
        port_MenuAddRow(ROW_QUIT, 0);
    } else if (page == PAGE_GROUP) {
        const PortChoiceGroup *of = port_FindChoiceGroup(g_menu.group);

        for (i = 0; i < of->count; i++) {
            port_MenuAddRow(ROW_CHOICE, of->first + i);
        }
        if (g_menu.group == PORT_MENU_CONTROLS - 1) {
            port_MenuAddRow(ROW_BUTTONS, 0);
            port_MenuAddRow(ROW_BUTTONS, 1);
        }
        port_MenuAddRow(ROW_BACK, 0);
    } else if (page == PAGE_ONLINE) {
        port_MenuAddRow(ROW_NET_ROLE, 0);
        if (g_port_settings.net_role == 0) {
            port_MenuAddRow(ROW_NET_PORT, 0);
            port_MenuAddRow(ROW_NET_LISTEN, 0);
            port_MenuAddRow(ROW_NET_DELAY, 0);
        } else {
            port_MenuAddRow(ROW_NET_ADDRESS, 0);
            port_MenuAddRow(ROW_NET_LISTEN, 0);
        }
        port_MenuAddRow(ROW_NET_START, 0);
        port_MenuAddRow(ROW_BACK, 0);
    } else {
        for (i = 0; i < BUTTONS; i++) {
            port_MenuAddRow(ROW_BUTTON, i);
        }
        port_MenuAddRow(ROW_RESET, 0);
        port_MenuAddRow(ROW_BACK, 0);
    }
    g_menu.cursor = cursor;
    if (g_menu.cursor >= g_menu.row_count) {
        g_menu.cursor = g_menu.row_count - 1;
    }
    g_menu.first = 0;
}

/* ---- the settings file ---- */

static void port_MenuReadFile(void) {
    FILE *file = fopen(g_menu.path, "rb");
    size_t got = 0;

    if (g_menu.text == NULL) {
        g_menu.text = malloc(TEXT_ROOM);
        if (g_menu.text == NULL) {
            return;
        }
    }
    if (file != NULL) {
        got = fread(g_menu.text, 1, TEXT_BYTES, file);
        fclose(file);
    }
    g_menu.text[got] = 0;
    g_menu.changed = FALSE;
}

static void port_MenuWriteFile(void) {
    FILE *file;
    size_t size;

    if (!g_menu.changed || g_menu.text == NULL) {
        return;
    }
    size = strlen(g_menu.text);
    file = fopen(g_menu.path, "wb");
    if (file == NULL || fwrite(g_menu.text, 1, size, file) != size) {
        port_Say("settings menu: %s cannot be written", g_menu.path);
        if (file != NULL) {
            fclose(file);
        }
        return;
    }
    fclose(file);
    g_menu.changed = FALSE;
    port_Say("settings menu: the settings are kept in %s", g_menu.path);
}

/* A line of the file changed; the note says so where it cannot be. */
static void port_MenuSetLine(const char *name, const char *value) {
    if (g_menu.text == NULL ||
        !port_SetSettingText(g_menu.text, TEXT_ROOM, name, value)) {
        snprintf(g_menu.note, sizeof(g_menu.note),
                 "The settings file is full: %s is not kept", name);
        return;
    }
    g_menu.changed = TRUE;
}

/* The bindings of the settings, made again from the file's text: the
 * layout, then the file's own lines, as the program read them as it
 * started. The rest of the settings are left as they are. */
static void port_MenuReadBindings(void) {
    static PortSettings g_read;
    static char g_copy[TEXT_ROOM];
    s32 i;

    port_SetDefaultSettings(&g_read, port_LookUpName);
    memcpy(g_copy, g_menu.text, strlen(g_menu.text) + 1);
    port_ReadSettings(&g_read, g_copy, port_LookUpName, NULL);
    for (i = 0; i < g_read.count; i++) {
        g_port_settings.bindings[i] = g_read.bindings[i];
    }
    g_port_settings.count = g_read.count;
    g_port_settings.layout = g_read.layout;
    g_port_settings.own_controls = g_read.own_controls;
}

/* ---- what a row shows ---- */

static s32 port_MenuChoiceValue(s32 choice) {
    if (strcmp(port_FindChoice(choice)->name, "graphics") == 0) {
        return port_WindowIsRemastered() ? 1 : 0;
    }
    if (strcmp(port_FindChoice(choice)->name, "bugfixes") == 0) {
        /* the game's own, which the options page at boot may have set */
        return port_GetBugfixes() ? 1 : 0;
    }
    return port_GetChoice(&g_port_settings, choice);
}

/* What presses a pad's button, by kind: the keys (`keys` 1) or the
 * controller's, joined by commas; "nothing" for none. */
static void port_MenuBindingsOf(
    s32 pad, u32 button, s32 keys, char *out, u32 room) {
    char value[VALUE_BYTES];
    u32 used = 0;
    s32 i;

    out[0] = 0;
    for (i = 0; i < g_port_settings.count; i++) {
        const PortBinding *binding = &g_port_settings.bindings[i];
        const char *shown;

        if (binding->pad != pad || binding->button != button ||
            (binding->source == PORT_SOURCE_KEY) != keys) {
            continue;
        }
        port_DescribeBinding(binding, value, sizeof(value));
        /* `key Z` is Z; `pad a` is Pad a */
        shown = strncmp(value, "key ", 4) == 0 ? value + 4 : value;
        if (strncmp(value, "pad ", 4) == 0) {
            value[0] = 'P';
        }
        used += (u32)snprintf(
            out + used, room - used, "%s%s", used == 0 ? "" : ", ", shown);
        if (used >= room) {
            break;
        }
    }
    if (out[0] == 0) {
        snprintf(out, room, "nothing");
    }
}

static void port_MenuDescribe(
    const MenuRow *row, char *label, char *value, s32 *is_arrows) {
    *is_arrows = FALSE;
    value[0] = 0;
    switch (row->kind) {
    case ROW_GROUP:
        snprintf(
            label, LABEL_BYTES, "%s", port_FindChoiceGroup(row->index)->title);
        snprintf(value, VALUE_BYTES, "...");
        break;
    case ROW_CHOICE: {
        const PortChoice *choice = port_FindChoice(row->index);
        s32 now = port_MenuChoiceValue(row->index);

        snprintf(label, LABEL_BYTES, "%s", choice->label);
        snprintf(value, VALUE_BYTES, "%s",
                 now >= 0 ? choice->shown[now] : "(the file's own)");
        *is_arrows = TRUE;
        break;
    }
    case ROW_BUTTONS:
        snprintf(label, LABEL_BYTES, "Player %d's buttons", row->index + 1);
        snprintf(value, VALUE_BYTES, "...");
        break;
    case ROW_CLOSE:
        snprintf(label, LABEL_BYTES, "Back to the game");
        break;
    case ROW_QUIT:
        snprintf(label, LABEL_BYTES, "Quit ReBushido");
        break;
    case ROW_BUTTON: {
        u32 mask = port_FindPadButton(g_port_menu_buttons[row->index][0]);
        char keys[VALUE_BYTES];
        char pads[VALUE_BYTES];

        port_MenuBindingsOf(g_menu.pad, mask, TRUE, keys, sizeof(keys));
        port_MenuBindingsOf(g_menu.pad, mask, FALSE, pads, sizeof(pads));
        snprintf(label, LABEL_BYTES, "%s", g_port_menu_buttons[row->index][1]);
        snprintf(value, VALUE_BYTES, "%s   |   %s", keys, pads);
        break;
    }
    case ROW_RESET:
        snprintf(label, LABEL_BYTES, "All back to the layout's own");
        break;
    case ROW_ONLINE:
        snprintf(label, LABEL_BYTES, "Play online");
        snprintf(value, VALUE_BYTES, "...");
        break;
    case ROW_NET_ROLE:
        snprintf(label, LABEL_BYTES, "You");
        snprintf(value, VALUE_BYTES, "%s",
                 g_port_settings.net_role == 0 ? "Host a game" : "Join a game");
        *is_arrows = TRUE;
        break;
    case ROW_NET_ADDRESS:
        snprintf(label, LABEL_BYTES, "The host's address and port");
        if (g_menu.is_typing) {
            snprintf(value, VALUE_BYTES, "%s_", g_menu.typed);
        } else {
            snprintf(value, VALUE_BYTES, "%s",
                     g_port_settings.net_address[0] != 0
                         ? g_port_settings.net_address
                         : "(Enter, then type it)");
        }
        break;
    case ROW_NET_PORT:
        snprintf(label, LABEL_BYTES, "The port to wait at");
        if (g_menu.is_typing) {
            snprintf(value, VALUE_BYTES, "%s_", g_menu.typed);
        } else {
            snprintf(value, VALUE_BYTES, "%d", g_port_settings.net_port);
            *is_arrows = TRUE;
        }
        break;
    case ROW_NET_LISTEN:
        snprintf(label, LABEL_BYTES, "Through");
        snprintf(value, VALUE_BYTES, "%s",
                 g_port_settings.net_listen
                     ? "Every network of this PC"
                     : "This PC alone (" NET_ADDRESS_LOCAL ")");
        *is_arrows = TRUE;
        break;
    case ROW_NET_DELAY:
        snprintf(label, LABEL_BYTES, "Input delay");
        snprintf(value, VALUE_BYTES, "%d frame%s", g_port_settings.net_delay,
                 g_port_settings.net_delay == 1 ? "" : "s");
        *is_arrows = TRUE;
        break;
    case ROW_NET_START:
        snprintf(label, LABEL_BYTES, "%s",
                 g_port_settings.net_role == 0
                     ? "Host now (the game starts again)"
                     : "Join now (the game starts again)");
        break;
    default:
        snprintf(label, LABEL_BYTES, "Back");
        break;
    }
}

/* The line under the rows about the cursor's. */
static const char *port_MenuHelp(void) {
    const MenuRow *row = &g_menu.rows[g_menu.cursor];

    if (g_menu.is_capturing) {
        return "Press a key, or a button of a controller. Escape: leave it "
               "as it is";
    }
    switch (row->kind) {
    case ROW_GROUP:
        return port_FindChoiceGroup(row->index)->help;
    case ROW_CHOICE:
        return port_FindChoice(row->index)->help;
    case ROW_CLOSE:
        return "The settings closed; the game goes on where it was";
    case ROW_BACK:
        return "Back to the page before";
    case ROW_BUTTONS:
        return "Which keys and controller buttons press each button";
    case ROW_BUTTON:
        return "Enter / Cross: bind a key or a button.  Delete / Square: "
               "back to the layout's";
    case ROW_RESET:
        return "Every button of this player back to the layout's own";
    case ROW_ONLINE:
        return "Rollback netplay with another PC: host a game, or join one";
    case ROW_NET_ROLE:
        return "The host waits for the other player and plays player 1";
    case ROW_NET_ADDRESS:
        return g_menu.is_typing
                   ? "Type it: digits, dots and a colon.  Enter: "
                     "keep it.  Escape: leave it"
                   : "As 192.168.1.20:7845: the host's PC, and "
                     "the port it waits at";
    case ROW_NET_PORT:
        return g_menu.is_typing ? "Type it.  Enter: keep it.  Escape: leave it"
                                : "The other player joins this port of your PC "
                                  "(Enter: type it)";
    case ROW_NET_LISTEN:
        return "Every network: found from the internet, where the router "
               "lets it through; this PC alone: two windows here";
    case ROW_NET_DELAY:
        return "Frames a press counts later: fewer rollbacks with more; "
               "the host's is both players'";
    case ROW_NET_START:
        return "The program starts again as a session: both go to versus "
               "select";
    case ROW_QUIT:
        return "The program ends (closing the window does too); the "
               "settings are kept";
    default:
        return "";
    }
}

/* ---- the Play online page ---- */

/* What was typed into the row under the cursor, kept: FALSE if it is
 * not what that row takes. */
static s32 port_MenuKeepTyped(void) {
    const MenuRow *row = &g_menu.rows[g_menu.cursor];
    char value[VALUE_BYTES];
    s32 dots = 0;
    s32 colons = 0;
    s32 number = 0;
    s32 i;

    for (i = 0; g_menu.typed[i] != 0; i++) {
        char c = g_menu.typed[i];

        if (c == '.') {
            dots++;
        } else if (c == ':') {
            colons++;
        } else if (c < '0' || c > '9') {
            return FALSE;
        } else {
            number = number * DECIMAL + (c - '0');
        }
    }
    if (row->kind == ROW_NET_ADDRESS) {
        if (dots != 3 || colons != 1) {
            return FALSE;
        }
        snprintf(g_port_settings.net_address,
                 sizeof(g_port_settings.net_address), "%s", g_menu.typed);
        port_MenuSetLine("net_address", g_menu.typed);
        return TRUE;
    }
    if (dots != 0 || colons != 0 || i == 0 || number < 1 ||
        number > PORT_NET_PORT_MAX) {
        return FALSE;
    }
    g_port_settings.net_port = number;
    snprintf(value, sizeof(value), "%d", number);
    port_MenuSetLine("net_port", value);
    return TRUE;
}

/* The page's setting under the cursor stepped left or right. */
static void port_MenuStepOnline(const MenuRow *row, s32 step) {
    char value[VALUE_BYTES];

    switch (row->kind) {
    case ROW_NET_ROLE:
        g_port_settings.net_role = !g_port_settings.net_role;
        port_MenuSetLine(
            "net_role", g_port_settings.net_role ? "join" : "host");
        port_MenuBuild(PAGE_ONLINE, 0);
        break;
    case ROW_NET_PORT:
        g_port_settings.net_port += step;
        if (g_port_settings.net_port < 1) {
            g_port_settings.net_port = PORT_NET_PORT_MAX;
        } else if (g_port_settings.net_port > PORT_NET_PORT_MAX) {
            g_port_settings.net_port = 1;
        }
        snprintf(value, sizeof(value), "%d", g_port_settings.net_port);
        port_MenuSetLine("net_port", value);
        break;
    case ROW_NET_LISTEN:
        g_port_settings.net_listen = !g_port_settings.net_listen;
        port_MenuSetLine(
            "net_listen", g_port_settings.net_listen ? "all" : "local");
        break;
    case ROW_NET_DELAY:
        g_port_settings.net_delay =
            (g_port_settings.net_delay + step + PORT_NET_DELAY_MAX + 1) %
            (PORT_NET_DELAY_MAX + 1);
        snprintf(value, sizeof(value), "%d", g_port_settings.net_delay);
        port_MenuSetLine("net_delay", value);
        break;
    default:
        break;
    }
}

/* Start: the settings kept, the menu closed, and the program started
 * again as the session's host or guest (sdl_main.c). */
static void port_MenuStartOnline(void) {
    char address[VALUE_BYTES];

    if (g_port_settings.net_role == 0) {
        snprintf(
            address, sizeof(address), "%s:%d",
            g_port_settings.net_listen ? NET_ADDRESS_ALL : NET_ADDRESS_LOCAL,
            g_port_settings.net_port);
    } else if (g_port_settings.net_address[0] == 0) {
        snprintf(g_menu.note, sizeof(g_menu.note),
                 "Type the host's address and port first");
        return;
    } else {
        snprintf(address, sizeof(address), "%s", g_port_settings.net_address);
    }
    port_MenuClose();
    port_StartOnline(g_port_settings.net_role == 0, address,
                     g_port_settings.net_delay, !g_port_settings.net_listen);
}

s32 port_MenuIsTyping(void) { return g_menu.is_open && g_menu.is_typing; }

void port_MenuType(char c) {
    u32 length = (u32)strlen(g_menu.typed);

    if (!port_MenuIsTyping()) {
        return;
    }
    if (c == '\b') {
        if (length != 0) {
            g_menu.typed[length - 1] = 0;
        }
    } else if (c == '\n') {
        g_menu.is_typing = FALSE;
        if (!port_MenuKeepTyped()) {
            snprintf(
                g_menu.note, sizeof(g_menu.note), "Not kept: %s", g_menu.typed);
        }
    } else if (length + 1 < sizeof(g_menu.typed) &&
               ((c >= '0' && c <= '9') || c == '.' || c == ':')) {
        g_menu.typed[length] = c;
        g_menu.typed[length + 1] = 0;
    }
}

/* ---- presses ---- */

static void port_MenuStepChoice(s32 choice, s32 step) {
    const PortChoice *of = port_FindChoice(choice);
    s32 now = port_MenuChoiceValue(choice);
    s32 next = port_StepChoice(choice, now, step);

    if (strcmp(of->name, "graphics") == 0) {
        port_WindowSetRemastered(next == 1);
    } else if (!port_SetChoice(&g_port_settings, choice, next)) {
        return;
    }
    port_MenuSetLine(of->name, of->words[next]);
    if (of->takes == PORT_TAKES_NOW) {
        port_WindowApplyChoice(choice);
    }
}

static void port_MenuMove(s32 step) {
    if (g_menu.row_count > 0) {
        g_menu.cursor =
            (g_menu.cursor + step + g_menu.row_count) % g_menu.row_count;
    }
}

/* Back a page: from a pad's buttons to the controls, from a group's page
 * or Play online to the first page, and from the first page (or a Play
 * online page the menu was opened on) out of the menu. */
static void port_MenuBack(void) {
    if (g_menu.page == PAGE_BUTTONS) {
        port_MenuBuild(PAGE_GROUP, g_menu.group_cursor);
    } else if (g_menu.page == PAGE_GROUP ||
               (g_menu.page == PAGE_ONLINE &&
                g_menu.opened_on != PORT_MENU_OPEN_ONLINE)) {
        port_MenuBuild(PAGE_MAIN, g_menu.main_cursor);
    } else {
        port_MenuClose();
    }
}

/* The cursor's button of the pad's page: its name in the file
 * (`p1.cross`). */
static void port_MenuButtonName(char *out, u32 room) {
    snprintf(out, room, "p%d.%s", g_menu.pad + 1,
             g_port_menu_buttons[g_menu.rows[g_menu.cursor].index][0]);
}

static void port_MenuClose(void) {
    port_MenuWriteFile();
    g_menu.is_open = FALSE;
    g_menu.is_capturing = FALSE;
}

static void port_MenuAccept(void) {
    const MenuRow *row = &g_menu.rows[g_menu.cursor];

    switch (row->kind) {
    case ROW_GROUP:
        g_menu.main_cursor = g_menu.cursor;
        g_menu.group = row->index;
        port_MenuBuild(PAGE_GROUP, 0);
        break;
    case ROW_CHOICE:
        port_MenuStepChoice(row->index, 1);
        break;
    case ROW_BUTTONS:
        g_menu.group_cursor = g_menu.cursor;
        g_menu.pad = row->index;
        port_MenuBuild(PAGE_BUTTONS, 0);
        break;
    case ROW_CLOSE:
        port_MenuClose();
        break;
    case ROW_BUTTON:
        g_menu.is_capturing = TRUE;
        break;
    case ROW_RESET:
        if (g_menu.text != NULL) {
            port_ClearBindingsText(g_menu.text, g_menu.pad);
            g_menu.changed = TRUE;
            port_MenuReadBindings();
        }
        break;
    case ROW_ONLINE:
        g_menu.main_cursor = g_menu.cursor;
        port_MenuBuild(PAGE_ONLINE, 0);
        break;
    case ROW_NET_ROLE:
    case ROW_NET_LISTEN:
    case ROW_NET_DELAY:
        port_MenuStepOnline(row, 1);
        break;
    case ROW_NET_ADDRESS:
    case ROW_NET_PORT:
        g_menu.is_typing = TRUE;
        if (row->kind == ROW_NET_ADDRESS) {
            snprintf(g_menu.typed, sizeof(g_menu.typed), "%s",
                     g_port_settings.net_address);
        } else {
            snprintf(g_menu.typed, sizeof(g_menu.typed), "%d",
                     g_port_settings.net_port);
        }
        break;
    case ROW_NET_START:
        port_MenuStartOnline();
        break;
    case ROW_QUIT:
        port_MenuClose();
        port_Say("settings menu: Quit");
        port_Quit(PORT_EXIT_OK);
    default:
        port_MenuBack();
        break;
    }
}

void port_MenuPress(s32 what) {
    const MenuRow *row;

    if (!g_menu.is_open) {
        return;
    }
    if (g_menu.is_capturing) {
        /* the window gives the key or the button itself; anything else
         * leaves the button as it was */
        if (what == PORT_MENU_BACK || what == PORT_MENU_CLOSE) {
            g_menu.is_capturing = FALSE;
        }
        return;
    }
    if (g_menu.is_typing) {
        /* the window gives the letters (port_MenuType); Enter keeps them,
         * Escape leaves the row as it was */
        if (what == PORT_MENU_ACCEPT) {
            port_MenuType('\n');
        } else if (what == PORT_MENU_BACK || what == PORT_MENU_CLOSE) {
            g_menu.is_typing = FALSE;
        }
        return;
    }
    row = &g_menu.rows[g_menu.cursor];
    g_menu.note[0] = 0;
    switch (what) {
    case PORT_MENU_UP:
        port_MenuMove(-1);
        break;
    case PORT_MENU_DOWN:
        port_MenuMove(1);
        break;
    case PORT_MENU_LEFT:
    case PORT_MENU_RIGHT:
        if (row->kind == ROW_CHOICE) {
            port_MenuStepChoice(row->index, what == PORT_MENU_LEFT ? -1 : 1);
        } else if (g_menu.page == PAGE_ONLINE) {
            port_MenuStepOnline(row, what == PORT_MENU_LEFT ? -1 : 1);
        }
        break;
    case PORT_MENU_ACCEPT:
        port_MenuAccept();
        break;
    case PORT_MENU_BACK:
        port_MenuBack();
        break;
    case PORT_MENU_CLOSE:
        port_MenuClose();
        break;
    case PORT_MENU_CLEAR:
        if (row->kind == ROW_BUTTON && g_menu.text != NULL) {
            char name[32];

            port_MenuButtonName(name, sizeof(name));
            port_SetBindingText(g_menu.text, TEXT_ROOM, name, TRUE, NULL);
            port_SetBindingText(g_menu.text, TEXT_ROOM, name, FALSE, NULL);
            g_menu.changed = TRUE;
            port_MenuReadBindings();
        }
        break;
    default:
        break;
    }
}

s32 port_MenuIsCapturing(void) { return g_menu.is_open && g_menu.is_capturing; }

void port_MenuCaptured(s32 keys, const char *value) {
    char name[32];

    if (!port_MenuIsCapturing()) {
        return;
    }
    g_menu.is_capturing = FALSE;
    if (value == NULL || g_menu.text == NULL) {
        return;
    }
    port_MenuButtonName(name, sizeof(name));
    if (!port_SetBindingText(g_menu.text, TEXT_ROOM, name, keys, value)) {
        snprintf(g_menu.note, sizeof(g_menu.note),
                 "The settings file is full: %s is not kept", name);
        return;
    }
    g_menu.changed = TRUE;
    port_MenuReadBindings();
    snprintf(g_menu.note, sizeof(g_menu.note), "%s = %s", name, value);
}

void port_MenuOpen(const char *path) {
    s32 i;

    snprintf(g_menu.path, sizeof(g_menu.path), "%s", path);
    port_MenuReadFile();
    if (!g_menu.has_start) {
        for (i = 0; i < port_CountChoices() && i < CHOICES_KEPT; i++) {
            g_menu.start[i] = port_MenuChoiceValue(i);
        }
        g_menu.has_start = TRUE;
    }
    if (!g_menu.font_ok) {
        g_menu.font_ok = port_FontStart();
        if (!g_menu.font_ok) {
            port_Say("settings menu: its font cannot be read");
        }
    }
    g_menu.note[0] = 0;
    g_menu.is_capturing = FALSE;
    g_menu.is_typing = FALSE;
    g_menu.opened_on = g_menu.open_on;
    g_menu.open_on = PORT_MENU_OPEN_MAIN;
    port_MenuBuild(PAGE_MAIN, 0);
    if (g_menu.opened_on == PORT_MENU_OPEN_ONLINE) {
        /* Online Play: its page, Back out of the menu */
        for (i = 0; i < g_menu.row_count; i++) {
            if (g_menu.rows[i].kind == ROW_ONLINE) {
                g_menu.main_cursor = i;
            }
        }
        port_MenuBuild(PAGE_ONLINE, 0);
    }
    g_menu.is_open = TRUE;
}

void port_MenuOpenOn(s32 page) { g_menu.open_on = page; }

s32 port_MenuIsOpen(void) { return g_menu.is_open; }

/* ---- a run's test ---- */

s32 port_MenuWord(const char *word) {
    static const char * const g_words[] = {
        "up", "down", "left", "right", "accept", "back", "close", "clear"};
    u32 i;

    if (strncmp(word, "type:", 5) == 0) {
        /* what is typed into the row being typed into, and Enter */
        if (!port_MenuIsTyping()) {
            return FALSE;
        }
        g_menu.typed[0] = 0;
        for (word += 5; *word != 0; word++) {
            port_MenuType(*word);
        }
        port_MenuType('\n');
        return TRUE;
    }
    if (strncmp(word, "key:", 4) == 0 || strncmp(word, "pad:", 4) == 0) {
        /* a binding captured, as the window would give it: `key:Space` */
        char value[VALUE_BYTES];
        u32 at;

        snprintf(value, sizeof(value), "%.3s %s", word, word + 4);
        /* a space of the name is a _ of the word: `key:Keypad_5` */
        for (at = 0; value[at] != 0; at++) {
            if (value[at] == '_') {
                value[at] = ' ';
            }
        }
        port_MenuCaptured(word[0] == 'k', value);
        return TRUE;
    }
    for (i = 0; i < sizeof(g_words) / sizeof(g_words[0]); i++) {
        if (strcmp(word, g_words[i]) == 0) {
            port_MenuPress((s32)i);
            return TRUE;
        }
    }
    return FALSE;
}

/* ---- drawing ---- */

void port_MenuDraw(u32 *pixels, s32 w, s32 h, PortRect *out) {
    char label[LABEL_BYTES];
    char value[VALUE_BYTES];
    s32 line = h / 30;
    s32 size;
    s32 pad;
    s32 panel_w;
    s32 panel_h;
    s32 x;
    s32 y;
    s32 rows_shown;
    s32 i;

    if (line < 12) {
        line = 12;
    }
    size = line * 64 / 100;
    pad = line / 2;
    panel_w = line * 30;
    if (panel_w > w - 2 * pad) {
        panel_w = w - 2 * pad;
    }
    panel_h = h - 2 * pad;
    if (panel_h > line * 32) {
        panel_h = line * 32;
    }
    out->x = (w - panel_w) / 2;
    out->y = (h - panel_h) / 2;
    out->w = panel_w;
    out->h = panel_h;
    for (i = 0; i < panel_w * panel_h; i++) {
        pixels[i] = COLOR_PANEL;
    }
    port_FillRect(pixels, panel_w, panel_h, 0, 0, panel_w, 2, COLOR_EDGE);
    port_FillRect(
        pixels, panel_w, panel_h, 0, panel_h - 2, panel_w, 2, COLOR_EDGE);
    if (!g_menu.font_ok) {
        return;
    }
    port_FontSetSize(size);
    x = pad * 2;
    y = pad + port_FontAscent();
    if (g_menu.page == PAGE_GROUP) {
        snprintf(label, sizeof(label), "Settings: %s",
                 port_FindChoiceGroup(g_menu.group)->title);
    } else {
        snprintf(label, sizeof(label), "%s",
                 g_menu.page == PAGE_MAIN     ? "ReBushido: settings"
                 : g_menu.page == PAGE_ONLINE ? "ReBushido: play online"
                 : g_menu.pad == 0            ? "Controls: player 1's buttons"
                                              : "Controls: player 2's buttons");
    }
    port_FontDraw(
        pixels, panel_w, panel_h, x, y + line / 4, label, COLOR_TITLE);
    snprintf(value, sizeof(value), "%s", port_GetVersionText());
    port_FontDraw(
        pixels, panel_w, panel_h, panel_w - x - port_FontMeasure(value),
        y + line / 4, value, COLOR_DIM);
    if (g_menu.page == PAGE_BUTTONS) {
        snprintf(label, sizeof(label), "keys   |   controller");
        port_FontDraw(
            pixels, panel_w, panel_h, panel_w - x - port_FontMeasure(label),
            y + line * 5 / 4, label, COLOR_DIM);
    }
    /* the rows, scrolled to keep the cursor's in view */
    y = pad + line * 2 + line / 2;
    rows_shown = (panel_h - (y - pad) - line * 3) / line;
    if (rows_shown < 1) {
        rows_shown = 1;
    }
    if (g_menu.cursor < g_menu.first) {
        g_menu.first = g_menu.cursor;
    }
    if (g_menu.cursor >= g_menu.first + rows_shown) {
        g_menu.first = g_menu.cursor - rows_shown + 1;
    }

    for (i = g_menu.first;
         i < g_menu.row_count && i < g_menu.first + rows_shown; i++) {
        const MenuRow *row = &g_menu.rows[i];
        s32 is_cursor = i == g_menu.cursor;
        s32 baseline = y + (line + port_FontAscent()) / 2 - line / 10;
        s32 is_arrows;
        u32 color = is_cursor ? COLOR_TEXT : COLOR_DIM;

        port_MenuDescribe(row, label, value, &is_arrows);
        if (g_menu.page == PAGE_MAIN && row->kind == ROW_ONLINE) {
            /* the groups above, the rest below a rule */
            port_FillRect(pixels, panel_w, panel_h, x, y + line / 8,
                          panel_w - 2 * x, 1, COLOR_EDGE);
            y += line / 4;
            baseline += line / 4;
        }
        if (is_cursor) {
            port_FillRect(pixels, panel_w, panel_h, x - pad / 2, y + 1,
                          panel_w - 2 * x + pad, line - 2, COLOR_BAR);
            port_FillRect(pixels, panel_w, panel_h, x - pad / 2, y + 1,
                          pad / 4 > 2 ? pad / 4 : 2, line - 2, COLOR_BAR_EDGE);
        }
        port_FontDraw(
            pixels, panel_w, panel_h, x + pad, baseline, label, color);
        if (row->kind == ROW_CHOICE &&
            port_FindChoice(row->index)->takes == PORT_TAKES_RESTART &&
            row->index < CHOICES_KEPT &&
            port_MenuChoiceValue(row->index) != g_menu.start[row->index]) {
            /* changed, and waiting on the program's next start */
            port_FontDraw(pixels, panel_w, panel_h,
                          x + pad + port_FontMeasure(label) + pad, baseline,
                          "(after a restart)", COLOR_NOTE);
        }
        if (value[0] != 0) {
            char shown[VALUE_BYTES + 8];
            s32 width;
            /* a setting's value: the one built in, or another, which is
             * gold; the cursor's says which is built in */
            s32 built_in = row->kind == ROW_CHOICE
                               ? port_FindChoice(row->index)->built_in
                               : -1;
            s32 is_built_in =
                built_in >= 0 && port_MenuChoiceValue(row->index) == built_in;

            if (built_in >= 0 && !is_built_in && !is_cursor) {
                color = COLOR_CHANGED;
            }
            if (is_cursor && is_arrows) {
                snprintf(shown, sizeof(shown), "<  %s  >", value);
            } else {
                snprintf(shown, sizeof(shown), "%s", value);
            }
            if (is_cursor && g_menu.is_capturing) {
                snprintf(shown, sizeof(shown), "press a key or a button...");
            }
            width = port_FontMeasure(shown);
            port_FontDraw(
                pixels, panel_w, panel_h, panel_w - x - width, baseline, shown,
                is_cursor && g_menu.is_capturing ? COLOR_NOTE : color);
            if (is_cursor && built_in >= 0 && !g_menu.is_capturing) {
                char mark[VALUE_BYTES];

                if (is_built_in) {
                    snprintf(mark, sizeof(mark), "%s", MENU_BUILT_IN);
                } else {
                    snprintf(mark, sizeof(mark), "%s: %s", MENU_BUILT_IN,
                             port_FindChoice(row->index)->shown[built_in]);
                }
                port_FontDraw(
                    pixels, panel_w, panel_h,
                    panel_w - x - width - pad - port_FontMeasure(mark),
                    baseline, mark, COLOR_DEFAULT);
            }
        }
        y += line;
    }
    /* the line about the cursor's row, a note, and the keys */
    y = panel_h - pad - line * 2;
    port_FillRect(pixels, panel_w, panel_h, x, y - line / 4, panel_w - 2 * x, 1,
                  COLOR_EDGE);
    port_FontDraw(pixels, panel_w, panel_h, x, y + port_FontAscent(),
                  g_menu.note[0] != 0 ? g_menu.note : port_MenuHelp(),
                  g_menu.note[0] != 0 ? COLOR_NOTE : COLOR_TEXT);
    port_FontDraw(pixels, panel_w, panel_h, x, y + line + port_FontAscent(),
                  "Arrows / D-pad: choose, change    Enter / Cross: open    "
                  "Esc / Circle: back    F1: close",
                  COLOR_DIM);
}
