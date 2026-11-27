/* The native port's window: the settings of rebushido.cfg as the settings
 * menu shows them (window.h; docs/PLAYING.md, "The settings menu"), made
 * from the list of the settings (settings_list.h). A value is set by
 * reading the line the file would have for it, so the file and the menu
 * can never mean two things by one word. No SDL and no
 * C library: tests/port/settings.c runs it. */
#include "common.h"
#include "window.h"

#define LINE_BYTES_MAX 128
/* The most settings the menu has room for. */
#define PORT_SETTINGS_IN_MENU_MAX 80

/* The menu's groups, by PORT_MENU_... less one: each one's title and the
 * line about it on the menu's first page. */
static const char * const g_port_group_titles[PORT_CHOICE_GROUPS][2] = {
    {"The game",
     "Bug fixes, the game's own options, the stages and unlocks, the menus"},
    {"Display",
     "The window, the picture's size, the counter, input lag and loading"},
    {"The remastered picture",
     "The scene drawn again: resolution, shadows, light, sky and effects"},
    {"Controls", "The keys and controller buttons of both players"},
    {"Mods", "The mods beside the program, and the menus the program ships"},
    {"Training",
     "The dummy, its recordings, slow motion, the presses and hit areas"},
};

/* What the menu shows of a switch. */
static const char * const g_port_switch_choices[PORT_CHOICES_MAX] = {
    "off", "on"};
static const char * const g_port_switch_shown[PORT_CHOICES_MAX] = {"Off", "On"};

static PortChoice g_port_choices[PORT_SETTINGS_IN_MENU_MAX];
static s32 g_port_choice_count;
static PortChoiceGroup g_port_choice_groups[PORT_CHOICE_GROUPS];

/* Whether two words are the same, letter for letter. */
static s32 port_TextsMatch(const char *a, const char *b) {
    while (*a != 0 && *a == *b) {
        a++;
        b++;
    }
    return *a == *b;
}

/* Whether setting `a` comes before setting `b` in the menu. */
static s32 port_ComesBefore(const PortSetting *a, const PortSetting *b) {
    return a->menu != b->menu ? a->menu < b->menu : a->rank < b->rank;
}

/* A setting of settings_list.h as the menu shows it. */
static void port_MakeChoice(PortChoice *choice, const PortSetting *setting) {
    const char * const *words = setting->choices;
    const char * const *shown = setting->shown;
    s32 i;

    if (words[0] == NULL) {
        words = setting->read == PORT_READ_SWITCH ? g_port_switch_choices
                                                  : setting->words;
    }
    if (shown[0] == NULL && setting->read == PORT_READ_SWITCH) {
        shown = g_port_switch_shown;
    }
    choice->name = setting->name;
    choice->label = setting->label;
    choice->help = setting->help;
    choice->takes = setting->takes;
    choice->count = 0;
    choice->built_in = -1;
    for (i = 0; i < PORT_CHOICES_MAX && words[i] != NULL; i++) {
        choice->words[i] = words[i];
        choice->shown[i] = shown[i];
        if (port_TextsMatch(words[i], setting->value)) {
            choice->built_in = i;
        }
        choice->count++;
    }
}

/* The menu's settings, made the first time they are asked for: those of
 * settings_list.h that have a group, by group and rank, and the list's
 * order where two ranks are the same. */
static void port_ListChoices(void) {
    const PortSetting *order[PORT_SETTINGS_IN_MENU_MAX];
    s32 count = 0;
    s32 i;
    s32 j;

    if (g_port_choice_count != 0) {
        return;
    }
    for (i = 0; i < g_port_setting_count; i++) {
        const PortSetting *setting = &g_port_setting_list[i];

        if (setting->name == NULL || setting->menu <= PORT_MENU_NONE ||
            setting->menu > PORT_CHOICE_GROUPS ||
            count == PORT_SETTINGS_IN_MENU_MAX) {
            continue;
        }
        /* each after those that come before it or tie with it */
        for (j = count; j > 0 && port_ComesBefore(setting, order[j - 1]); j--) {
            order[j] = order[j - 1];
        }
        order[j] = setting;
        count++;
    }
    for (i = 0; i < PORT_CHOICE_GROUPS; i++) {
        g_port_choice_groups[i].title = g_port_group_titles[i][0];
        g_port_choice_groups[i].help = g_port_group_titles[i][1];
        g_port_choice_groups[i].first = count;
        g_port_choice_groups[i].count = 0;
    }
    for (i = count - 1; i >= 0; i--) {
        PortChoiceGroup *group = &g_port_choice_groups[order[i]->menu - 1];

        port_MakeChoice(&g_port_choices[i], order[i]);
        group->first = i;
        group->count++;
    }
    /* a group with no setting begins where the next does */
    for (i = PORT_CHOICE_GROUPS - 2; i >= 0; i--) {
        if (g_port_choice_groups[i].count == 0) {
            g_port_choice_groups[i].first = g_port_choice_groups[i + 1].first;
        }
    }
    g_port_choice_count = count;
}

const PortChoice *port_FindChoice(s32 choice) {
    port_ListChoices();
    if (choice < 0 || choice >= port_CountChoices()) {
        return NULL;
    }
    return &g_port_choices[choice];
}

s32 port_CountChoices(void) {
    port_ListChoices();
    return g_port_choice_count;
}

const PortChoiceGroup *port_FindChoiceGroup(s32 group) {
    port_ListChoices();
    if (group < 0 || group >= PORT_CHOICE_GROUPS) {
        return NULL;
    }
    return &g_port_choice_groups[group];
}

/* `name = word` into `line`: FALSE if it does not fit. */
static s32 port_ChoiceLine(char *line, const char *name, const char *word) {
    u32 at = 0;
    u32 i;

    for (i = 0; name[i] != 0; i++) {
        if (at >= LINE_BYTES_MAX - 4) {
            return FALSE;
        }
        line[at++] = name[i];
    }
    line[at++] = ' ';
    line[at++] = '=';
    line[at++] = ' ';
    for (i = 0; word[i] != 0; i++) {
        if (at >= LINE_BYTES_MAX - 1) {
            return FALSE;
        }
        line[at++] = word[i];
    }
    line[at] = 0;
    return TRUE;
}

s32 port_SetChoice(PortSettings *settings, s32 choice, s32 value) {
    char line[LINE_BYTES_MAX];

    if (choice < 0 || choice >= port_CountChoices() || value < 0 ||
        value >= g_port_choices[choice].count ||
        !port_ChoiceLine(line, g_port_choices[choice].name,
                         g_port_choices[choice].words[value])) {
        return FALSE;
    }
    /* none of these lines is a pad's, so nothing is looked up by name */
    return port_ReadSettings(settings, line, NULL, NULL) == 0;
}

/* Whether two settings are the same, byte for byte. */
static s32 port_SettingsMatch(
    const PortSettings *first, const PortSettings *second) {
    const u8 *a = (const u8 *)first;
    const u8 *b = (const u8 *)second;
    u32 i;

    for (i = 0; i < sizeof(PortSettings); i++) {
        if (a[i] != b[i]) {
            return FALSE;
        }
    }
    return TRUE;
}

s32 port_GetChoice(const PortSettings *settings, s32 choice) {
    /* a copy as large as the settings, which are mostly the bindings:
     * kept off the stack */
    static PortSettings g_try;
    s32 value;

    if (choice < 0 || choice >= port_CountChoices()) {
        return -1;
    }
    for (value = 0; value < g_port_choices[choice].count; value++) {
        g_try = *settings;
        if (port_SetChoice(&g_try, choice, value) &&
            port_SettingsMatch(&g_try, settings)) {
            return value;
        }
    }
    return -1;
}

s32 port_StepChoice(s32 choice, s32 value, s32 step) {
    s32 count;

    if (choice < 0 || choice >= port_CountChoices()) {
        return -1;
    }
    count = g_port_choices[choice].count;
    if (value < 0) {
        return step > 0 ? 0 : count - 1;
    }
    return (value + step + count) % count;
}
