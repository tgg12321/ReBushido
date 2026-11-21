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
#include "update.h"

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

/* What marks the value built in, beside the cursor's setting. */
#define MENU_BUILT_IN "default"

enum {
    PAGE_MAIN,    /* the groups, Play online, back to the game, Quit */
    PAGE_GROUP,   /* one group's settings */
    PAGE_BUTTONS, /* one pad's */
    PAGE_ONLINE,  /* Play online: host or join a session of netplay */
    PAGE_UPDATE,  /* Updates: the newer release, its notes (update.h) */
    PAGE_REPLAYS, /* the replays kept, to watch (menu_replays.c) */
};

/* What can be typed into a row of the Play online page. */
#define TYPED_BYTES PORT_NET_ADDRESS_BYTES

/* What a row of a page is. */
enum {
    ROW_GROUP,   /* a group's page: `index` the group */
    ROW_CHOICE,  /* a setting of port_FindChoice: `index` */
    ROW_BUTTONS, /* a pad's buttons: `index` the pad */
    ROW_CLOSE,
    ROW_BUTTON, /* a button of the pad: `index` into g_port_menu_buttons */
    ROW_RESET,  /* the pad's buttons back to the layout's */
    ROW_BACK,
    ROW_ONLINE,   /* the Play online page */
    ROW_NET_ITEM, /* the Play online page's: `index` its item (menu_online.c) */
    ROW_QUIT,     /* the program ended, the settings kept */
    ROW_UPDATES,  /* the Updates page */
    ROW_UPDATE_CHECK, /* ask the releases now */
    ROW_UPDATE_NOW,   /* download the newer one, or restart into it */
    ROW_UPDATE_LATER, /* its notice not shown again */
    ROW_UPDATE_NOTE,  /* a line of its notes: `index`, -1 their heading */
    ROW_REPLAYS,      /* the Replays page */
    ROW_REPLAY,       /* a replay of it: `index` its row (menu_replays.c) */
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
    /* the updater's view the Updates page was built for */
    s32 update_generation;
    /* frames since the cursor last moved (its glow), and of the page's
     * fade in */
    s32 pulse;
    s32 fade;
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

    /* nothing of the updater's while Play online is up (update.h) */
    if (page == PAGE_ONLINE || g_menu.page == PAGE_ONLINE) {
        port_UpdateSetOnline(page == PAGE_ONLINE);
    }
    if (page != g_menu.page) {
        /* another page fades in, as the game's pages come up */
        g_menu.fade = 0;
    }
    g_menu.page = page;
    g_menu.pulse = 0;
    g_menu.row_count = 0;
    if (page == PAGE_MAIN) {
        for (group = 0; group < PORT_CHOICE_GROUPS; group++) {
            if (port_FindChoiceGroup(group)->count > 0 ||
                group == PORT_MENU_CONTROLS - 1) {
                port_MenuAddRow(ROW_GROUP, group);
            }
        }
        /* Replays above Play online: Play online stays the third row up
         * from the first, where the tests reach it (tools/port_net.py) */
        port_MenuAddRow(ROW_REPLAYS, 0);
        port_MenuAddRow(ROW_ONLINE, 0);
        port_MenuAddRow(ROW_UPDATES, 0);
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
    } else if (page == PAGE_UPDATE) {
        PortUpdateView view;

        port_UpdateGetView(&view);
        g_menu.update_generation = view.generation;
        port_MenuAddRow(ROW_UPDATE_CHECK, 0);
        if (view.status == PORT_UPDATE_AVAILABLE ||
            view.status == PORT_UPDATE_DOWNLOADING ||
            view.status == PORT_UPDATE_READY ||
            (view.status == PORT_UPDATE_FAILED && view.version[0] != 0 &&
             port_UpdateCompareVersions(view.version, view.own) > 0)) {
            port_MenuAddRow(ROW_UPDATE_NOW, 0);
        }
        if (view.status == PORT_UPDATE_AVAILABLE ||
            view.status == PORT_UPDATE_TOO_NEW) {
            port_MenuAddRow(ROW_UPDATE_LATER, 0);
        }
        if (view.version[0] != 0 && view.note_count > 0 &&
            port_UpdateCompareVersions(view.version, view.own) > 0) {
            port_MenuAddRow(ROW_UPDATE_NOTE, -1);
            for (i = 0; i < view.note_count; i++) {
                port_MenuAddRow(ROW_UPDATE_NOTE, i);
            }
        }
        port_MenuAddRow(ROW_BACK, 0);
    } else if (page == PAGE_ONLINE) {
        s32 items[PORT_ONLINE_ITEMS_MAX];
        s32 count = port_OnlineItems(items, PORT_ONLINE_ITEMS_MAX);

        for (i = 0; i < count; i++) {
            port_MenuAddRow(ROW_NET_ITEM, items[i]);
        }
        port_MenuAddRow(ROW_BACK, 0);
    } else if (page == PAGE_REPLAYS) {
        s32 count = port_ReplaysItems();

        for (i = 0; i < count && i < ROWS_MAX - 1; i++) {
            port_MenuAddRow(ROW_REPLAY, i);
        }
        if (count == 0) {
            port_MenuAddRow(ROW_REPLAY, -1);
        }
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
void port_MenuSetLine(const char *name, const char *value) {
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

/* A row of the Updates page, or the first page's row of it. */
static void port_MenuDescribeUpdate(
    const MenuRow *row, char *label, char *value) {
    PortUpdateView view;
    const char *status;

    port_UpdateGetView(&view);
    switch (view.status) {
    case PORT_UPDATE_OFF:
        status = "off";
        break;
    case PORT_UPDATE_CHECKING:
        status = "asking...";
        break;
    case PORT_UPDATE_UP_TO_DATE:
        status = "up to date";
        break;
    case PORT_UPDATE_AVAILABLE:
    case PORT_UPDATE_TOO_NEW:
        status = "a newer version";
        break;
    case PORT_UPDATE_DOWNLOADING:
        status = "downloading";
        break;
    case PORT_UPDATE_READY:
        status = "ready to install";
        break;
    case PORT_UPDATE_FAILED:
        status = "it did not work";
        break;
    default:
        status = "not asked yet";
        break;
    }
    switch (row->kind) {
    case ROW_UPDATES:
        snprintf(label, LABEL_BYTES, "Updates");
        if (view.status == PORT_UPDATE_AVAILABLE ||
            view.status == PORT_UPDATE_READY ||
            view.status == PORT_UPDATE_TOO_NEW) {
            snprintf(value, VALUE_BYTES, "%s: %s  ...", view.version, status);
        } else {
            snprintf(value, VALUE_BYTES, "%s  ...", status);
        }
        break;
    case ROW_UPDATE_CHECK:
        snprintf(label, LABEL_BYTES, "Check for updates now");
        if (view.status == PORT_UPDATE_OFF ||
            view.status == PORT_UPDATE_FAILED) {
            snprintf(value, VALUE_BYTES, "%s: %s", status, view.text);
        } else if (view.status == PORT_UPDATE_UP_TO_DATE) {
            snprintf(value, VALUE_BYTES, "up to date: %s", view.own);
        } else if (view.version[0] != 0 &&
                   port_UpdateCompareVersions(view.version, view.own) > 0) {
            snprintf(
                value, VALUE_BYTES, "%s; this is %s", view.version, view.own);
        } else {
            snprintf(value, VALUE_BYTES, "%s; this is %s", status, view.own);
        }
        break;
    case ROW_UPDATE_NOW:
        if (view.status == PORT_UPDATE_READY) {
            snprintf(
                label, LABEL_BYTES, "Restart and install %s", view.version);
        } else if (view.status == PORT_UPDATE_DOWNLOADING) {
            snprintf(label, LABEL_BYTES, "Downloading %s", view.version);
            snprintf(value, VALUE_BYTES, "%d%%", view.permille / 10);
        } else if (view.status == PORT_UPDATE_FAILED) {
            snprintf(
                label, LABEL_BYTES, "Try the update to %s again", view.version);
        } else {
            snprintf(label, LABEL_BYTES, "Update now to %s", view.version);
        }
        break;
    case ROW_UPDATE_LATER:
        snprintf(label, LABEL_BYTES, "Later");
        break;
    default:
        if (row->index < 0) {
            snprintf(label, LABEL_BYTES, "What's new in %s:", view.version);
        } else if (row->index < view.note_count) {
            snprintf(label, LABEL_BYTES, "    %s", view.notes[row->index]);
        } else {
            label[0] = 0;
        }
        break;
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
    case ROW_UPDATES:
    case ROW_UPDATE_CHECK:
    case ROW_UPDATE_NOW:
    case ROW_UPDATE_LATER:
    case ROW_UPDATE_NOTE:
        port_MenuDescribeUpdate(row, label, value);
        break;
    case ROW_REPLAYS:
        snprintf(label, LABEL_BYTES, "Replays");
        snprintf(value, VALUE_BYTES, "...");
        break;
    case ROW_REPLAY:
        port_ReplaysDescribe(
            row->index, label, LABEL_BYTES, value, VALUE_BYTES);
        break;
    case ROW_NET_ITEM:
        port_OnlineDescribe(
            row->index,
            g_menu.is_typing && row == &g_menu.rows[g_menu.cursor]
                ? g_menu.typed
                : NULL,
            label, LABEL_BYTES, value, VALUE_BYTES, is_arrows);
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
    case ROW_NET_ITEM:
        return port_OnlineHelp(row->index, g_menu.is_typing);
    case ROW_UPDATES:
        return "Whether there is a newer ReBushido, what is new in it, and "
               "installing it";
    case ROW_UPDATE_CHECK:
        return "Asks the releases now, whatever the Updates setting says";
    case ROW_UPDATE_NOW: {
        PortUpdateView view;

        port_UpdateGetView(&view);
        return view.status == PORT_UPDATE_READY
                   ? "The program ends, the new version is put in place and "
                     "started; your settings, cards and mods are kept"
               : view.status == PORT_UPDATE_DOWNLOADING
                   ? "Downloaded beside the program, each file held to the "
                     "release's signed manifest"
                   : "Downloads it beside the program; it is installed when "
                     "you restart";
    }
    case ROW_UPDATE_LATER:
        return "The main menu's notice is not shown again for this version";
    case ROW_UPDATE_NOTE:
        return "The release's notes, signed with it";
    case ROW_REPLAYS:
        return "The fights kept as replays: watch one again";
    case ROW_REPLAY:
        return port_ReplaysHelp(row->index);
    case ROW_QUIT:
        return "The program ends (closing the window does too); the "
               "settings are kept";
    default:
        return "";
    }
}

/* ---- the Play online page ---- */

/* What was typed into the row under the cursor, kept: FALSE if it is
 * not what that row takes (menu_online.c says why in the note). */
static s32 port_MenuKeepTyped(void) {
    const MenuRow *row = &g_menu.rows[g_menu.cursor];
    s32 page_cursor = g_menu.cursor;
    s32 what = port_OnlineKeep(
        row->index, g_menu.typed, g_menu.note, sizeof(g_menu.note));

    if (what == PORT_ONLINE_REBUILD && g_menu.is_open) {
        port_MenuBuild(PAGE_ONLINE, page_cursor);
    }
    return what >= 0;
}

void port_MenuLeave(void) { port_MenuClose(); }

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
        if (!port_MenuKeepTyped() && g_menu.note[0] == 0) {
            snprintf(
                g_menu.note, sizeof(g_menu.note), "Not kept: %s", g_menu.typed);
        }
    } else if (length + 1 < sizeof(g_menu.typed) &&
               port_OnlineTakes(g_menu.rows[g_menu.cursor].index, c)) {
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
    /* the game's own options page sounds its cursor as a value changes */
    port_PageSound(PORT_PAGE_SOUND_CURSOR);
    port_MenuSetLine(of->name, of->words[next]);
    if (of->takes == PORT_TAKES_NOW) {
        port_WindowApplyChoice(choice);
    }
}

static void port_MenuMove(s32 step) {
    if (g_menu.row_count > 1) {
        g_menu.cursor =
            (g_menu.cursor + step + g_menu.row_count) % g_menu.row_count;
        g_menu.pulse = 0;
        port_PageSound(PORT_PAGE_SOUND_CURSOR);
    }
}

/* Back a page: from a pad's buttons to the controls, from a group's page
 * or Play online to the first page, and from the first page (or a Play
 * online page the menu was opened on) out of the menu. */
static void port_MenuBack(void) {
    if (g_menu.page == PAGE_BUTTONS) {
        port_MenuBuild(PAGE_GROUP, g_menu.group_cursor);
    } else if (g_menu.page == PAGE_GROUP || g_menu.page == PAGE_UPDATE ||
               g_menu.page == PAGE_REPLAYS ||
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
    if (g_menu.page == PAGE_ONLINE) {
        port_UpdateSetOnline(FALSE);
    }
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
    case ROW_REPLAYS:
        g_menu.main_cursor = g_menu.cursor;
        port_MenuBuild(PAGE_REPLAYS, 0);
        break;
    case ROW_REPLAY:
        if (port_ReplaysAccept(row->index, g_menu.note, sizeof(g_menu.note))) {
            /* it plays from the game's next frame */
            port_MenuClose();
        }
        break;
    case ROW_NET_ITEM: {
        s32 page_cursor = g_menu.cursor;
        s32 what =
            port_OnlineAccept(row->index, g_menu.typed, sizeof(g_menu.typed),
                              g_menu.note, sizeof(g_menu.note));

        if (what == PORT_ONLINE_TYPE) {
            g_menu.is_typing = TRUE;
        } else if (what == PORT_ONLINE_REBUILD && g_menu.is_open) {
            port_MenuBuild(PAGE_ONLINE, page_cursor);
        }
        break;
    }
    case ROW_UPDATES:
        g_menu.main_cursor = g_menu.cursor;
        port_MenuBuild(PAGE_UPDATE, 0);
        break;
    case ROW_UPDATE_CHECK:
        port_UpdateCheckNow();
        break;
    case ROW_UPDATE_NOW: {
        PortUpdateView view;

        port_UpdateGetView(&view);
        if (view.status == PORT_UPDATE_READY) {
            port_MenuClose();
            port_Say("settings menu: restart and install %s", view.version);
            port_UpdateRestartNow();
        } else {
            port_UpdateDownloadNow();
        }
        break;
    }
    case ROW_UPDATE_LATER:
        port_UpdateLater();
        port_MenuBack();
        break;
    case ROW_UPDATE_NOTE:
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
            if (port_OnlineStep(row->index, what == PORT_MENU_LEFT ? -1 : 1) ==
                PORT_ONLINE_REBUILD) {
                port_MenuBuild(PAGE_ONLINE, g_menu.cursor);
            }
        }
        break;
    case PORT_MENU_ACCEPT:
        /* the game's sounds: a row taken, or a way back */
        port_PageSound(row->kind == ROW_BACK || row->kind == ROW_CLOSE
                           ? PORT_PAGE_SOUND_CANCEL
                           : PORT_PAGE_SOUND_CONFIRM);
        port_MenuAccept();
        break;
    case PORT_MENU_BACK:
        port_PageSound(PORT_PAGE_SOUND_CANCEL);
        port_MenuBack();
        break;
    case PORT_MENU_CLOSE:
        port_PageSound(PORT_PAGE_SOUND_CANCEL);
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
    g_menu.fade = 0;
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

/* The page's places, in the game's screen (menu.h, PORT_LOOK_...): the
 * rows' names at the left of the game's wide panel, their values at its
 * right, the first row's baseline and the rows' pitch, the help under
 * them and the keys under the panel. */
#define LOOK_LABEL_X 84
#define LOOK_VALUE_X 556
#define LOOK_ROW_Y 67
#define LOOK_ROW_PITCH 12
#define LOOK_ROWS_SHOWN 10
#define LOOK_HELP_Y 204
#define LOOK_NOTE_Y 215
#define LOOK_KEYS_Y 236
#define LOOK_HEAD_Y 57

/* The page's title, as the game's Option Menu has its own. */
static void port_MenuTitle(char *out, u32 room) {
    if (g_menu.page == PAGE_GROUP) {
        snprintf(out, room, "%s", port_FindChoiceGroup(g_menu.group)->title);
        return;
    }
    snprintf(out, room, "%s",
             g_menu.page == PAGE_MAIN      ? "Options"
             : g_menu.page == PAGE_ONLINE  ? "Play online"
             : g_menu.page == PAGE_UPDATE  ? "Updates"
             : g_menu.page == PAGE_REPLAYS ? "Replays"
             : g_menu.pad == 0             ? "Player 1's buttons"
                                           : "Player 2's buttons");
}

void port_MenuTick(void) {
    if (!g_menu.is_open) {
        return;
    }
    g_menu.pulse++;
    if (g_menu.fade < PORT_LOOK_FADE_FRAMES) {
        g_menu.fade++;
    }
}

void port_MenuDrawLook(PortLook *look, s32 is_own) {
    char label[LABEL_BYTES];
    char value[VALUE_BYTES];
    s32 i;
    s32 y;

    if (g_menu.page == PAGE_UPDATE) {
        PortUpdateView view;

        port_UpdateGetView(&view);
        if (view.generation != g_menu.update_generation) {
            port_MenuBuild(PAGE_UPDATE, g_menu.cursor);
        }
    }
    port_LookPanel(look, is_own);
    port_MenuTitle(label, sizeof(label));
    port_LookTitle(look, label);
    if (g_menu.page == PAGE_BUTTONS) {
        port_LookText(look, LOOK_VALUE_X, LOOK_HEAD_Y, PORT_LOOK_SIZE_NOTE,
                      PORT_FACE_TEXT, "keys   |   controller", PORT_LOOK_GREY,
                      PORT_LOOK_RIGHT);
    }
    /* the rows, scrolled to keep the cursor's in view */
    if (g_menu.cursor < g_menu.first) {
        g_menu.first = g_menu.cursor;
    }
    if (g_menu.cursor >= g_menu.first + LOOK_ROWS_SHOWN) {
        g_menu.first = g_menu.cursor - LOOK_ROWS_SHOWN + 1;
    }
    if (g_menu.first > 0) {
        port_LookText(
            look, PORT_LOOK_GAME_W / 2, LOOK_HEAD_Y, PORT_LOOK_SIZE_NOTE,
            PORT_FACE_TEXT, "more above", PORT_LOOK_GREY, PORT_LOOK_CENTRE);
    }
    y = LOOK_ROW_Y;
    for (i = g_menu.first;
         i < g_menu.row_count && i < g_menu.first + LOOK_ROWS_SHOWN; i++) {
        const MenuRow *row = &g_menu.rows[i];
        s32 is_cursor = i == g_menu.cursor;
        s32 is_arrows;
        s32 width;
        u32 color = is_cursor ? port_LookPulse(g_menu.pulse) : PORT_LOOK_ROW;

        port_MenuDescribe(row, label, value, &is_arrows);
        if (g_menu.page == PAGE_MAIN && row->kind == ROW_ONLINE) {
            /* the groups above, the rest below a rule */
            port_LookBox(look, LOOK_LABEL_X, y - LOOK_ROW_PITCH + 3,
                         LOOK_VALUE_X - LOOK_LABEL_X, 1, PORT_LOOK_RULE_FAINT);
            y += LOOK_ROW_PITCH / 3;
        }
        width = port_LookText(look, LOOK_LABEL_X, y, PORT_LOOK_SIZE_ROW,
                              PORT_FACE_BOLD, label, color, PORT_LOOK_LEFT);
        if (row->kind == ROW_CHOICE &&
            port_FindChoice(row->index)->takes == PORT_TAKES_RESTART &&
            row->index < CHOICES_KEPT &&
            port_MenuChoiceValue(row->index) != g_menu.start[row->index]) {
            /* changed, and waiting on the program's next start */
            port_LookText(look, LOOK_LABEL_X + width + 8, y,
                          PORT_LOOK_SIZE_NOTE, PORT_FACE_TEXT,
                          "(after a restart)", PORT_LOOK_WARN, PORT_LOOK_LEFT);
        }
        if (value[0] != 0) {
            char shown[VALUE_BYTES + 8];
            /* a setting's value: white as the game's set value is, or
             * gold where it is not the one built in */
            s32 built_in = row->kind == ROW_CHOICE
                               ? port_FindChoice(row->index)->built_in
                               : -1;
            u32 value_color =
                built_in >= 0 && port_MenuChoiceValue(row->index) != built_in
                    ? PORT_LOOK_GOLD
                    : PORT_LOOK_WHITE;

            if (is_cursor && is_arrows) {
                snprintf(shown, sizeof(shown), "<  %s  >", value);
            } else {
                snprintf(shown, sizeof(shown), "%s", value);
            }
            if (is_cursor && g_menu.is_capturing) {
                snprintf(shown, sizeof(shown), "press a key or a button...");
                value_color = PORT_LOOK_WARN;
            }
            port_LookText(look, LOOK_VALUE_X, y, PORT_LOOK_SIZE_TEXT + 1,
                          PORT_FACE_TEXT, shown, value_color, PORT_LOOK_RIGHT);
        }
        y += LOOK_ROW_PITCH;
    }
    if (g_menu.first + LOOK_ROWS_SHOWN < g_menu.row_count) {
        port_LookText(
            look, PORT_LOOK_GAME_W / 2, y, PORT_LOOK_SIZE_NOTE, PORT_FACE_TEXT,
            "more below", PORT_LOOK_GREY, PORT_LOOK_CENTRE);
    }
    /* the line about the cursor's row, and a note or the value built in */
    port_LookBox(look, LOOK_LABEL_X, LOOK_HELP_Y - 11,
                 LOOK_VALUE_X - LOOK_LABEL_X, 1, PORT_LOOK_RULE_FAINT);
    port_LookText(
        look, LOOK_LABEL_X, LOOK_HELP_Y, PORT_LOOK_SIZE_TEXT, PORT_FACE_TEXT,
        port_MenuHelp(), PORT_LOOK_WHITE, PORT_LOOK_LEFT);
    if (g_menu.note[0] != 0) {
        port_LookText(
            look, LOOK_LABEL_X, LOOK_NOTE_Y, PORT_LOOK_SIZE_TEXT,
            PORT_FACE_TEXT, g_menu.note, PORT_LOOK_WARN, PORT_LOOK_LEFT);
    } else if (g_menu.rows[g_menu.cursor].kind == ROW_CHOICE) {
        const PortChoice *of =
            port_FindChoice(g_menu.rows[g_menu.cursor].index);

        if (of->built_in >= 0) {
            snprintf(label, sizeof(label), "%s: %s", MENU_BUILT_IN,
                     of->shown[of->built_in]);
            port_LookText(
                look, LOOK_LABEL_X, LOOK_NOTE_Y, PORT_LOOK_SIZE_TEXT,
                PORT_FACE_TEXT, label, PORT_LOOK_GREY, PORT_LOOK_LEFT);
        }
    }
    port_LookText(
        look, LOOK_VALUE_X, LOOK_NOTE_Y, PORT_LOOK_SIZE_NOTE, PORT_FACE_TEXT,
        port_GetVersionText(), PORT_LOOK_OFF, PORT_LOOK_RIGHT);
    port_LookText(look, PORT_LOOK_GAME_W / 2, LOOK_KEYS_Y, PORT_LOOK_SIZE_NOTE,
                  PORT_FACE_TEXT,
                  "Cross / Enter: choose    Left, Right: change    "
                  "Circle / Escape: back    F1: close",
                  PORT_LOOK_GREY, PORT_LOOK_CENTRE);
    port_LookFade(look, g_menu.fade, PORT_LOOK_FADE_FRAMES);
}
