/* The native port's window: the PC's keys and controllers as the two
 * pads' buttons, and the settings file that says which (window.h;
 * docs/PORT.md, "The window"). No SDL in it: a key's or a button's name
 * is turned into its number by a function the caller gives, so
 * tests/port/window.c runs it with names of its own.
 *
 * The game takes a pad for digital whatever kind it is (pad.c reads the
 * buttons of a digital pad, of both analog kinds and of two more, and
 * never a stick), so the port's pads are digital ones and a stick
 * pushed past the dead zone is the direction's button. */
#include "common.h"
#include "window.h"
#include "bb2.h"
/* After port_platform.h, which bb2.h brings: shared.h says the same
 * things a second time on purpose, and the compiler stops here if the
 * two have come to differ. */
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wredundant-decls"
#include "shared.h"
#pragma GCC diagnostic pop

/* Sony's masks (libetc.h), by the names a settings file has. */
typedef struct {
    const char *name;
    u32 mask;
} PortButtonName;

static const PortButtonName g_port_pad_buttons[] = {
    {"up", PADLup},       {"down", PADLdown},    {"left", PADLleft},
    {"right", PADLright}, {"triangle", PADRup},  {"cross", PADRdown},
    {"square", PADRleft}, {"circle", PADRright}, {"l1", PADL1},
    {"l2", PADL2},        {"r1", PADR1},         {"r2", PADR2},
    {"start", PADstart},  {"select", PADselect},
};

#define PAD_BUTTONS (sizeof(g_port_pad_buttons) / sizeof(g_port_pad_buttons[0]))
#define PERCENT 100
#define LINE_BYTES_MAX 128

/* The layouts a settings file can start from (`controls = NAME`): each
 * the text of a settings file with nothing but pN.BUTTON lines in it.
 * Another layout is another text and a line of g_port_layouts.
 *
 * original: the keys of the two pads apart on one keyboard; a
 * controller's buttons the PlayStation's by where they are (the lowest
 * of the four is Cross), and its left stick the direction buttons too. */
static const char g_port_layout_original[] =
    "# Player one, on the keyboard\n"
    "p1.up = key Up\n"
    "p1.down = key Down\n"
    "p1.left = key Left\n"
    "p1.right = key Right\n"
    "p1.cross = key Z\n"
    "p1.circle = key X\n"
    "p1.square = key A\n"
    "p1.triangle = key S\n"
    "p1.l1 = key Q\n"
    "p1.r1 = key W\n"
    "p1.l2 = key 1\n"
    "p1.r2 = key 2\n"
    "p1.start = key Return\n"
    "p1.select = key Right Shift\n"
    "\n"
    "# Player two, on the keyboard\n"
    "p2.up = key I\n"
    "p2.down = key K\n"
    "p2.left = key J\n"
    "p2.right = key L\n"
    "p2.cross = key G\n"
    "p2.circle = key H\n"
    "p2.square = key T\n"
    "p2.triangle = key Y\n"
    "p2.l1 = key R\n"
    "p2.r1 = key U\n"
    "p2.l2 = key 5\n"
    "p2.r2 = key 6\n"
    "p2.start = key P\n"
    "p2.select = key O\n"
    "\n"
    "# Player one's controller: the first plugged in\n"
    "p1.up = pad dpup\n"
    "p1.down = pad dpdown\n"
    "p1.left = pad dpleft\n"
    "p1.right = pad dpright\n"
    "p1.up = pad -lefty\n"
    "p1.down = pad +lefty\n"
    "p1.left = pad -leftx\n"
    "p1.right = pad +leftx\n"
    "p1.cross = pad a\n"
    "p1.circle = pad b\n"
    "p1.square = pad x\n"
    "p1.triangle = pad y\n"
    "p1.l1 = pad leftshoulder\n"
    "p1.r1 = pad rightshoulder\n"
    "p1.l2 = pad +lefttrigger\n"
    "p1.r2 = pad +righttrigger\n"
    "p1.start = pad start\n"
    "p1.select = pad back\n"
    "\n"
    "# Player two's controller: the second\n"
    "p2.up = pad dpup\n"
    "p2.down = pad dpdown\n"
    "p2.left = pad dpleft\n"
    "p2.right = pad dpright\n"
    "p2.up = pad -lefty\n"
    "p2.down = pad +lefty\n"
    "p2.left = pad -leftx\n"
    "p2.right = pad +leftx\n"
    "p2.cross = pad a\n"
    "p2.circle = pad b\n"
    "p2.square = pad x\n"
    "p2.triangle = pad y\n"
    "p2.l1 = pad leftshoulder\n"
    "p2.r1 = pad rightshoulder\n"
    "p2.l2 = pad +lefttrigger\n"
    "p2.r2 = pad +righttrigger\n"
    "p2.start = pad start\n"
    "p2.select = pad back\n"
    "\n"
    "# Two on two's third and fourth players: the third and fourth\n"
    "# controllers plugged in\n"
    "p3.up = pad dpup\n"
    "p3.down = pad dpdown\n"
    "p3.left = pad dpleft\n"
    "p3.right = pad dpright\n"
    "p3.up = pad -lefty\n"
    "p3.down = pad +lefty\n"
    "p3.left = pad -leftx\n"
    "p3.right = pad +leftx\n"
    "p3.cross = pad a\n"
    "p3.circle = pad b\n"
    "p3.square = pad x\n"
    "p3.triangle = pad y\n"
    "p3.l1 = pad leftshoulder\n"
    "p3.r1 = pad rightshoulder\n"
    "p3.l2 = pad +lefttrigger\n"
    "p3.r2 = pad +righttrigger\n"
    "p3.start = pad start\n"
    "p3.select = pad back\n"
    "p4.up = pad dpup\n"
    "p4.down = pad dpdown\n"
    "p4.left = pad dpleft\n"
    "p4.right = pad dpright\n"
    "p4.up = pad -lefty\n"
    "p4.down = pad +lefty\n"
    "p4.left = pad -leftx\n"
    "p4.right = pad +leftx\n"
    "p4.cross = pad a\n"
    "p4.circle = pad b\n"
    "p4.square = pad x\n"
    "p4.triangle = pad y\n"
    "p4.l1 = pad leftshoulder\n"
    "p4.r1 = pad rightshoulder\n"
    "p4.l2 = pad +lefttrigger\n"
    "p4.r2 = pad +righttrigger\n"
    "p4.start = pad start\n"
    "p4.select = pad back\n";

const PortLayout g_port_layouts[PORT_LAYOUTS] = {
    {"original", g_port_layout_original},
};

/* The most a layout's text may be: it is read from a copy. */
#define LAYOUT_BYTES_MAX 8192

/* ---- text ---- */

static char port_LowerCase(char letter) {
    return letter >= 'A' && letter <= 'Z' ? (char)(letter - 'A' + 'a') : letter;
}

/* Whether two words are the same, letters of either case. */
static s32 port_WordsMatch(const char *first, const char *second) {
    while (*first != 0 && port_LowerCase(*first) == port_LowerCase(*second)) {
        first++;
        second++;
    }
    return *first == 0 && *second == 0;
}

static s32 port_IsSpace(char letter) {
    return letter == ' ' || letter == '\t' || letter == '\r';
}

/* The text without the spaces before it and after it, cut in place. */
static char *port_TrimText(char *text) {
    char *end;

    while (port_IsSpace(*text)) {
        text++;
    }
    end = text;
    while (*end != 0) {
        end++;
    }
    while (end > text && port_IsSpace(end[-1])) {
        end--;
    }
    *end = 0;
    return text;
}

/* A whole number of decimal digits: 1 and the number, or 0. */
static s32 port_ReadDigits(const char *text, s32 *value) {
    s32 total = 0;
    s32 digits = 0;

    for (; *text >= '0' && *text <= '9'; text++) {
        total = total * 10 + (*text - '0');
        if (++digits > 6) {
            return 0;
        }
    }
    *value = total;
    return digits != 0 && *text == 0;
}

/* `W:H`, the picture's shape, into the settings: 1 if it is one. */
static s32 port_ReadShape(const char *text, PortSettings *settings) {
    s32 across = 0;
    s32 down = 0;
    s32 digits = 0;

    for (; *text >= '0' && *text <= '9'; text++) {
        across = across * 10 + (*text - '0');
        digits++;
    }
    if (digits == 0 || digits > 4 || *text != ':') {
        return 0;
    }
    text++;
    digits = 0;
    for (; *text >= '0' && *text <= '9'; text++) {
        down = down * 10 + (*text - '0');
        digits++;
    }
    if (digits == 0 || digits > 4 || *text != 0 || across <= 0 || down <= 0) {
        return 0;
    }
    /* a picture narrower than the console's is the console's: the scene
     * is never cut to less than the console showed */
    if (across * PORT_ASPECT_H_ORIGINAL < down * PORT_ASPECT_W_ORIGINAL) {
        across = PORT_ASPECT_W_ORIGINAL;
        down = PORT_ASPECT_H_ORIGINAL;
    }
    settings->aspect_w = across;
    settings->aspect_h = down;
    return 1;
}

/* ---- the buttons' names ---- */

u32 port_FindPadButton(const char *name) {
    u32 i;

    for (i = 0; i < PAD_BUTTONS; i++) {
        if (port_WordsMatch(g_port_pad_buttons[i].name, name)) {
            return g_port_pad_buttons[i].mask;
        }
    }
    return 0;
}

u32 port_FindPadButtons(const char *names) {
    char name[LINE_BYTES_MAX];
    u32 buttons = 0;

    while (*names != 0) {
        u32 length = 0;
        u32 button;

        while (*names != 0 && *names != '+') {
            if (length == sizeof(name) - 1) {
                return 0;
            }
            name[length++] = *names++;
        }
        name[length] = 0;
        button = port_FindPadButton(name);
        if (button == 0) {
            return 0;
        }
        buttons |= button;
        if (*names == '+') {
            names++;
            if (*names == 0) {
                return 0;
            }
        }
    }
    return buttons;
}

const char *port_NamePadButton(u32 button) {
    u32 i;

    for (i = 0; i < PAD_BUTTONS; i++) {
        if (g_port_pad_buttons[i].mask == button) {
            return g_port_pad_buttons[i].name;
        }
    }
    return NULL;
}

/* ---- a settings file ---- */

/* Whether a binding is a key's (1) or a controller's (0): a line of a
 * file takes away the built-in ones of its own kind. */
static s32 port_IsKey(s32 source) { return source == PORT_SOURCE_KEY; }

/* Takes the built-in bindings of a pad's button away: the keys, or the
 * controller's buttons and axes. */
static void port_DropDefaults(
    PortSettings *settings, s32 pad, u32 button, s32 keys) {
    s32 kept = 0;
    s32 i;

    for (i = 0; i < settings->count; i++) {
        const PortBinding *binding = &settings->bindings[i];

        if (binding->is_default && binding->pad == pad &&
            binding->button == button && port_IsKey(binding->source) == keys) {
            continue;
        }
        settings->bindings[kept++] = *binding;
    }
    settings->count = kept;
}

/* Takes a key, or a button or an axis's direction of a pad's
 * controller, away from whatever it presses as built in: a file that
 * gives it a new button means that button alone. */
static void port_FreeControl(PortSettings *settings, const PortBinding *taken) {
    s32 kept = 0;
    s32 i;

    for (i = 0; i < settings->count; i++) {
        const PortBinding *binding = &settings->bindings[i];

        if (binding->is_default && binding->source == taken->source &&
            binding->code == taken->code &&
            binding->direction == taken->direction &&
            (taken->source == PORT_SOURCE_KEY || binding->pad == taken->pad)) {
            continue;
        }
        settings->bindings[kept++] = *binding;
    }
    settings->count = kept;
}

/* `pN.BUTTON = key NAME`, `= pad NAME`, `= pad +AXIS` or `= none`: NULL,
 * or why not. */
static const char *port_ReadBinding(
    PortSettings *settings, const char *name, char *value,
    PortNameLookup lookup, s32 is_default) {
    PortBinding binding;
    char *what = value;
    s32 keys;

    if (name[0] != 'p' && name[0] != 'P') {
        return "not a setting";
    }
    if (name[1] < '1' || name[1] >= '1' + PORT_WINDOW_PADS || name[2] != '.') {
        return "a pad is p1 to p4, then a dot and its button";
    }
    binding.pad = (u8)(name[1] - '1');
    binding.button = port_FindPadButtons(name + 3);
    if (binding.button == 0) {
        return "no such button of a pad (several are joined by +)";
    }
    if (!is_default) {
        settings->own_controls++;
    }
    binding.is_default = (u8)is_default;
    binding.is_down = 0;
    binding.direction = 0;
    /* the first word says what kind; the rest is the name */
    while (*what != 0 && !port_IsSpace(*what)) {
        what++;
    }
    if (*what != 0) {
        *what++ = 0;
    }
    what = port_TrimText(what);
    if (port_WordsMatch(value, "none") && *what == 0) {
        if (!is_default) {
            port_DropDefaults(settings, binding.pad, binding.button, 1);
            port_DropDefaults(settings, binding.pad, binding.button, 0);
        }
        return NULL;
    }
    if (port_WordsMatch(value, "key")) {
        binding.source = PORT_SOURCE_KEY;
    } else if (port_WordsMatch(value, "pad")) {
        binding.source = PORT_SOURCE_BUTTON;
        if (*what == '+' || *what == '-') {
            binding.source = PORT_SOURCE_AXIS;
            binding.direction = *what == '+' ? 1 : -1;
            what++;
        }
    } else {
        return "after the = comes `key`, `pad` or `none`";
    }
    binding.code = *what != 0 ? lookup(binding.source, what) : -1;
    if (binding.code == PORT_NAME_RESERVED) {
        return "that key is the window's own (Escape: pause and back)";
    }
    if (binding.code < 0) {
        return binding.source == PORT_SOURCE_KEY
                   ? "no key of that name"
                   : "no button or axis of a controller of that name";
    }
    keys = port_IsKey(binding.source);
    if (!is_default) {
        port_DropDefaults(settings, binding.pad, binding.button, keys);
        port_FreeControl(settings, &binding);
    }
    if (settings->count == PORT_BINDINGS_MAX) {
        return "too many keys and buttons";
    }
    settings->bindings[settings->count++] = binding;
    return NULL;
}

static s32 port_ReadLines(
    PortSettings *settings, char *text, PortNameLookup lookup,
    PortComplaint complain, s32 is_default);

/* `controls = NAME`: the built-in bindings are those of that layout. A
 * file's own pN lines are read over it, so they come after it. NULL, or
 * why not. */
static const char *port_ReadLayout(
    PortSettings *settings, const char *value, PortNameLookup lookup) {
    static char text[LAYOUT_BYTES_MAX];
    const char *from;
    s32 kept = 0;
    s32 which;
    u32 i;

    for (which = 0; which < PORT_LAYOUTS; which++) {
        if (port_WordsMatch(value, g_port_layouts[which].name)) {
            break;
        }
    }
    if (which == PORT_LAYOUTS) {
        return "no layout of that name (original)";
    }
    if (settings->own_controls != 0) {
        return "controls comes before the file's own pN lines";
    }
    from = g_port_layouts[which].text;
    for (i = 0; from[i] != 0; i++) {
        if (i == sizeof(text) - 1) {
            return "the layout is too long to read";
        }
        text[i] = from[i];
    }
    text[i] = 0;
    for (i = 0; i < (u32)settings->count; i++) {
        if (!settings->bindings[i].is_default) {
            settings->bindings[kept++] = settings->bindings[i];
        }
    }
    settings->count = kept;
    settings->layout = which;
    port_ReadLines(settings, text, lookup, NULL, 1);
    return NULL;
}

/* `A.B.C.D:PORT` or `[IPv6]:PORT` into `out` (PORT_NET_ADDRESS_BYTES), as
 * it was written: 1, or 0 if it is not one. Only its form is looked at
 * here; the port reads it again as it starts (port_ParseNetAddress). An
 * empty one is kept only where `empty` says it may be. */
static s32 port_ReadNetAddressOf(const char *value, char *out, s32 empty) {
    s32 colons = 0;
    s32 i;

    for (i = 0; value[i] != 0; i++) {
        char c = value[i];

        if (i == PORT_NET_ADDRESS_BYTES - 1) {
            return 0;
        }
        if (c == ':') {
            colons++;
        } else if (
            !((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') ||
              (c >= 'A' && c <= 'F') || c == '.' || c == '[' || c == ']')) {
            return 0;
        }
    }
    if (i == 0 ? !empty : colons == 0) {
        return 0;
    }
    for (i = 0; value[i] != 0; i++) {
        out[i] = value[i];
    }
    out[i] = 0;
    return 1;
}

static s32 port_ReadNetAddress(const char *value, char *out) {
    return port_ReadNetAddressOf(value, out, FALSE);
}

/* A name the lobby shows into `out` of `room` bytes: 1, or 0 if it has
 * a letter it cannot show (port_IsNameLetter). */
static s32 port_ReadNetName(const char *value, char *out, u32 room) {
    u32 i;

    for (i = 0; value[i] != 0; i++) {
        if (i + 1 >= room || !port_IsNameLetter(value[i])) {
            return 0;
        }
    }
    for (i = 0; value[i] != 0; i++) {
        out[i] = value[i];
    }
    out[i] = 0;
    return 1;
}

s32 port_IsNameLetter(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
           (c >= '0' && c <= '9') || c == ' ' || c == '-' || c == '_' ||
           c == '.' || c == '!' || c == '?';
}

/* ---- the settings' own readers (settings_list.h) ---- */

/* `draw_distance`: the renderer's own names, so that they are said in one
 * place, and letters of either case as every other value here. `lookup`
 * and `why` are not used. */
static s32 port_ReadDistance(PortSettings *settings, const char *value,
                             PortNameLookup lookup, const char **why) {
    s32 distance;

    (void)lookup;
    (void)why;
    for (distance = 0; distance < PORT_DISTANCE_KINDS; distance++) {
        if (port_WordsMatch(value, port_NameRemasterDistance(distance))) {
            settings->distance = distance;
            return TRUE;
        }
    }
    return FALSE;
}

/* `aspect`, W:H. `lookup` and `why` are not used. */
static s32 port_ReadAspect(PortSettings *settings, const char *value,
                           PortNameLookup lookup, const char **why) {
    (void)lookup;
    (void)why;
    return port_ReadShape(value, settings);
}

/* `shadows`: original, soft, or cast, cast:N or a number
 * (port_remaster.h). `lookup` and `why` are not used. */
static s32 port_ReadShadows(PortSettings *settings, const char *value,
                            PortNameLookup lookup, const char **why) {
    s32 shadows;

    (void)lookup;
    (void)why;
    if (port_WordsMatch(value, "original")) {
        settings->shadows = 0;
        return TRUE;
    }
    if (port_WordsMatch(value, "soft")) {
        settings->shadows = PORT_SHADOWS_SOFT;
        return TRUE;
    }
    shadows = port_ReadRemasterShadows(value);
    if (shadows < 0) {
        return FALSE;
    }
    settings->shadows = shadows;
    return TRUE;
}

/* `effects`, by the renderer's names. `lookup` and `why` are not used. */
static s32 port_ReadEffects(PortSettings *settings, const char *value,
                            PortNameLookup lookup, const char **why) {
    s32 effects = port_ReadRemasterEffects(value);

    (void)lookup;
    (void)why;
    if (effects < 0) {
        return FALSE;
    }
    settings->effects = effects;
    return TRUE;
}

/* `net_address`, `net_lobby`, `net_punch_to`, `net_room` and
 * `net_player`. `lookup` and `why` are not used. */
static s32 port_ReadNetSetting(PortSettings *settings, const char *value,
                               PortNameLookup lookup, const char **why) {
    (void)lookup;
    (void)why;
    return port_ReadNetAddress(value, settings->net_address);
}

static s32 port_ReadNetLobby(PortSettings *settings, const char *value,
                             PortNameLookup lookup, const char **why) {
    (void)lookup;
    (void)why;
    return port_ReadNetAddressOf(value, settings->net_lobby, TRUE);
}

static s32 port_ReadNetPunchTo(PortSettings *settings, const char *value,
                               PortNameLookup lookup, const char **why) {
    (void)lookup;
    (void)why;
    return port_ReadNetAddressOf(value, settings->net_punch_to, TRUE);
}

static s32 port_ReadNetRoom(PortSettings *settings, const char *value,
                            PortNameLookup lookup, const char **why) {
    (void)lookup;
    (void)why;
    return port_ReadNetName(
        value, settings->net_room, sizeof(settings->net_room));
}

static s32 port_ReadNetPlayer(PortSettings *settings, const char *value,
                              PortNameLookup lookup, const char **why) {
    (void)lookup;
    (void)why;
    return port_ReadNetName(
        value, settings->net_player, sizeof(settings->net_player));
}

/* `controls`: a layout's bindings, which say why not themselves. */
static s32 port_ReadControls(PortSettings *settings, const char *value,
                             PortNameLookup lookup, const char **why) {
    *why = port_ReadLayout(settings, value, lookup);
    return *why == NULL;
}

/* ---- the list of the settings ---- */

#define PORT_SETTING_AT(member) ((s32) __builtin_offsetof(PortSettings, member))

const PortSetting g_port_setting_list[] = {
#define PORT_SETTING(member, ...) {.at = PORT_SETTING_AT(member), __VA_ARGS__},
#define PORT_SETTING_CUSTOM(...) {.at = -1, __VA_ARGS__},
#define PORT_SETTINGS_HEADING(text) {.at = -1, .note = text},
#include "settings_list.h"
#undef PORT_SETTING
#undef PORT_SETTING_CUSTOM
#undef PORT_SETTINGS_HEADING
};

const s32 g_port_setting_count =
    (s32)(sizeof(g_port_setting_list) / sizeof(g_port_setting_list[0]));

/* What a switch is read from, and what a line that is none says. */
static const char * const g_port_switch_words[PORT_CHOICES_MAX] = {"off", "on"};

#define SWITCH_IS "on or off"
#define WHY_BYTES_MAX 160

/* The built-in text's comments begin at this column, two spaces after a
 * line longer than that. */
#define NOTE_COLUMN 23
#define NOTE_GAP_MIN 2

/* The text the built-in settings begin with. */
static const char g_port_settings_title[] =
    "# ReBushido's window: the settings built in. Copy what you want to\n"
    "# change into rebushido.cfg, beside the program.\n"
    "\n";

/* `NAME is ...`, what a line that cannot be read is told. */
static const char *port_SayWhatItIs(const PortSetting *setting) {
    static char why[WHY_BYTES_MAX];
    const char *is = setting->is != NULL ? setting->is : SWITCH_IS;
    const char *from;
    u32 at = 0;

    for (from = setting->name; *from != 0 && at < WHY_BYTES_MAX - 1; from++) {
        why[at++] = *from;
    }
    for (from = " is "; *from != 0 && at < WHY_BYTES_MAX - 1; from++) {
        why[at++] = *from;
    }
    for (from = is; *from != 0 && at < WHY_BYTES_MAX - 1; from++) {
        why[at++] = *from;
    }
    why[at] = 0;
    return why;
}

/* A setting's value into its member: NULL, or why not. */
static const char *port_ReadEntry(
    const PortSetting *setting, PortSettings *settings, const char *value,
    PortNameLookup lookup) {
    const char * const *words = setting->words;
    const char *why = NULL;
    s32 *member;
    s32 number;
    s32 i;

    if (setting->read == PORT_READ_BY) {
        if (setting->reader(settings, value, lookup, &why)) {
            return NULL;
        }
        return why != NULL ? why : port_SayWhatItIs(setting);
    }
    if (setting->at < 0) {
        return port_SayWhatItIs(setting);
    }
    member = (s32 *)((u8 *)settings + setting->at);
    if (setting->read == PORT_READ_SWITCH) {
        words = g_port_switch_words;
    }
    for (i = 0; i < PORT_CHOICES_MAX && words[i] != NULL; i++) {
        if (port_WordsMatch(value, words[i])) {
            *member =
                setting->read == PORT_READ_NUMBER ? setting->values[i] : i;
            return NULL;
        }
    }
    if (setting->read == PORT_READ_NUMBER && port_ReadDigits(value, &number) &&
        number >= setting->least && number <= setting->most) {
        *member = number;
        return NULL;
    }
    return port_SayWhatItIs(setting);
}

/* A letter at `at` of `out`, if it fits before its last byte. */
static u32 port_AddLetter(char *out, u32 at, char letter) {
    if (at < PORT_DEFAULT_SETTINGS_BYTES - 1) {
        out[at++] = letter;
    }
    return at;
}

/* `text` at `at` of `out`, as much as fits. */
static u32 port_AddText(char *out, u32 at, const char *text) {
    for (; *text != 0; text++) {
        at = port_AddLetter(out, at, *text);
    }
    return at;
}

/* A comment's lines, each `# line` after `indent` spaces, the first
 * after what is on its line already. */
static u32 port_AddNote(char *out, u32 at, const char *note, u32 indent) {
    u32 i;

    while (*note != 0) {
        at = port_AddText(out, at, "# ");
        for (; *note != 0 && *note != '\n'; note++) {
            at = port_AddLetter(out, at, *note);
        }
        at = port_AddText(out, at, "\n");
        if (*note == '\n') {
            note++;
            for (i = 0; i < indent; i++) {
                at = port_AddLetter(out, at, ' ');
            }
        }
    }
    return at;
}

const char *port_GetDefaultSettings(void) {
    static char text[PORT_DEFAULT_SETTINGS_BYTES];
    s32 i;
    u32 at;

    if (text[0] != 0) {
        return text;
    }
    at = port_AddText(text, 0, g_port_settings_title);
    for (i = 0; i < g_port_setting_count; i++) {
        const PortSetting *setting = &g_port_setting_list[i];
        u32 line_at = at;
        u32 spaces;

        if (setting->name == NULL) {
            /* a heading */
            at = port_AddText(text, at, "\n");
            at = port_AddNote(text, at, setting->note, 0);
            continue;
        }
        at = port_AddText(text, at, setting->name);
        at = port_AddText(text, at, " = ");
        at = port_AddText(text, at, setting->value);
        if (setting->note == NULL) {
            at = port_AddText(text, at, "\n");
            continue;
        }
        spaces = at - line_at + NOTE_GAP_MIN < NOTE_COLUMN
                     ? NOTE_COLUMN - (at - line_at)
                     : NOTE_GAP_MIN;
        for (; spaces > 0; spaces--) {
            at = port_AddLetter(text, at, ' ');
        }
        at = port_AddNote(text, at, setting->note, NOTE_COLUMN);
    }
    text[at] = 0;
    return text;
}

/* One line, `NAME = VALUE`: NULL, or why it cannot be used. */
static const char *port_ReadSetting(
    PortSettings *settings, char *line, PortNameLookup lookup, s32 is_default) {
    char *value = line;
    char *name;
    s32 i;

    while (*value != 0 && *value != '=') {
        value++;
    }
    if (*value == 0) {
        return "no = in it";
    }
    *value++ = 0;
    name = port_TrimText(line);
    value = port_TrimText(value);
    for (i = 0; i < g_port_setting_count; i++) {
        const PortSetting *setting = &g_port_setting_list[i];

        if (setting->name != NULL && port_WordsMatch(name, setting->name)) {
            return port_ReadEntry(setting, settings, value, lookup);
        }
    }
    return port_ReadBinding(settings, name, value, lookup, is_default);
}

/* Every line of a text: what is after a # is a comment. */
static s32 port_ReadLines(
    PortSettings *settings, char *text, PortNameLookup lookup,
    PortComplaint complain, s32 is_default) {
    s32 unused = 0;
    s32 number = 0;

    while (*text != 0) {
        char *line = text;
        char *comment;
        const char *why;
        char shown[LINE_BYTES_MAX];
        s32 i;

        while (*text != 0 && *text != '\n') {
            text++;
        }
        if (*text != 0) {
            *text++ = 0;
        }
        number++;
        for (comment = line; *comment != 0; comment++) {
            if (*comment == '#') {
                *comment = 0;
                break;
            }
        }
        line = port_TrimText(line);
        if (*line == 0) {
            continue;
        }
        /* as it was, for the complaint: the line is cut up as it is read */
        for (i = 0; i < LINE_BYTES_MAX - 1 && line[i] != 0; i++) {
            shown[i] = line[i];
        }
        shown[i] = 0;
        why = port_ReadSetting(settings, line, lookup, is_default);
        if (why != NULL) {
            unused++;
            if (complain != NULL) {
                complain(number, why, shown);
            }
        }
    }
    return unused;
}

void port_SetDefaultSettings(PortSettings *settings, PortNameLookup lookup) {
    char value[LINE_BYTES_MAX];
    s32 i;

    settings->count = 0;
    settings->own_controls = 0;
    settings->layout = 0;
    settings->net_address[0] = 0;
    settings->net_lobby[0] = 0;
    settings->net_room[0] = 0;
    settings->net_player[0] = 0;
    settings->net_punch_to[0] = 0;
    /* each setting's value as its line of the built-in text has it; a
     * name the window's library does not know leaves that binding out */
    for (i = 0; i < g_port_setting_count; i++) {
        const PortSetting *setting = &g_port_setting_list[i];
        u32 j;

        if (setting->name == NULL) {
            continue;
        }
        for (j = 0; j < sizeof(value) - 1 && setting->value[j] != 0; j++) {
            value[j] = setting->value[j];
        }
        value[j] = 0;
        port_ReadEntry(setting, settings, value, lookup);
    }
}

s32 port_ReadSettings(PortSettings *settings, char *text, PortNameLookup lookup,
                      PortComplaint complain) {
    return port_ReadLines(settings, text, lookup, complain, 0);
}

/* ---- what is held ---- */

void port_ApplyInput(
    PortSettings *settings, s32 source, s32 pad, s32 code, s32 value) {
    s32 threshold = PORT_AXIS_MAX * settings->deadzone / PERCENT;
    s32 i;

    for (i = 0; i < settings->count; i++) {
        PortBinding *binding = &settings->bindings[i];

        if (binding->source != source || binding->code != code ||
            (source != PORT_SOURCE_KEY && binding->pad != pad)) {
            continue;
        }
        if (source == PORT_SOURCE_AXIS) {
            binding->is_down =
                (u8)(binding->direction > 0 ? value >= threshold
                                            : value <= -threshold);
        } else {
            binding->is_down = (u8)(value != 0);
        }
    }
}

void port_ReleaseController(PortSettings *settings, s32 pad) {
    s32 i;

    for (i = 0; i < settings->count; i++) {
        PortBinding *binding = &settings->bindings[i];

        if (binding->source != PORT_SOURCE_KEY && binding->pad == pad) {
            binding->is_down = 0;
        }
    }
}

u32 port_GetHeldButtons(const PortSettings *settings, s32 pad) {
    u32 held = 0;
    s32 i;

    for (i = 0; i < settings->count; i++) {
        const PortBinding *binding = &settings->bindings[i];

        if (binding->pad == pad && binding->is_down) {
            held |= binding->button;
        }
    }
    return held;
}

const PortBinding *port_FindBinding(
    const PortSettings *settings, s32 keys, s32 pad, u32 button) {
    s32 i;

    for (i = 0; i < settings->count; i++) {
        const PortBinding *binding = &settings->bindings[i];

        if (binding->pad == pad && binding->button == button &&
            port_IsKey(binding->source) == keys) {
            return binding;
        }
    }
    return NULL;
}
