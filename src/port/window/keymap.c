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

/* The layout built in. The keys of the two pads are apart on one
 * keyboard; a controller's buttons are the PlayStation's by where they
 * are (the lowest of the four is Cross), and its left stick is the
 * direction buttons too. */
const char g_port_default_settings[] =
    "# ReBushido's window: the settings built in. Copy what you want to\n"
    "# change into rebushido.cfg, beside the program.\n"
    "\n"
    "scale = fit            # fit, integer or stretch\n"
    "filter = nearest       # nearest or linear\n"
    "fullscreen = off       # on or off: how it starts\n"
    "vsync = on             # on or off: wait for the display\n"
    "deadzone = 40          # how far a stick counts, in parts of 100\n"
    "\n"
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
    "p2.select = pad back\n";

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

/* `on` or `off`, and the like: 1 and which, or 0. */
static s32 port_ReadChoice(
    const char *value, const char *first, const char *second, s32 *which) {
    if (port_WordsMatch(value, first)) {
        *which = 0;
        return 1;
    }
    if (port_WordsMatch(value, second)) {
        *which = 1;
        return 1;
    }
    return 0;
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
    if ((name[1] != '1' && name[1] != '2') || name[2] != '.') {
        return "a pad is p1 or p2, then a dot and its button";
    }
    binding.pad = (u8)(name[1] - '1');
    binding.button = port_FindPadButton(name + 3);
    if (binding.button == 0) {
        return "no such button of a pad";
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

/* One line, `NAME = VALUE`: NULL, or why it cannot be used. */
static const char *port_ReadSetting(
    PortSettings *settings, char *line, PortNameLookup lookup, s32 is_default) {
    char *value = line;
    char *name;
    s32 number;

    while (*value != 0 && *value != '=') {
        value++;
    }
    if (*value == 0) {
        return "no = in it";
    }
    *value++ = 0;
    name = port_TrimText(line);
    value = port_TrimText(value);
    if (port_WordsMatch(name, "scale")) {
        if (port_WordsMatch(value, "fit")) {
            settings->scale = PORT_SCALE_FIT;
        } else if (port_WordsMatch(value, "integer")) {
            settings->scale = PORT_SCALE_INTEGER;
        } else if (port_WordsMatch(value, "stretch")) {
            settings->scale = PORT_SCALE_STRETCH;
        } else {
            return "scale is fit, integer or stretch";
        }
        return NULL;
    }
    if (port_WordsMatch(name, "filter")) {
        return port_ReadChoice(value, "nearest", "linear", &settings->is_linear)
                   ? NULL
                   : "filter is nearest or linear";
    }
    if (port_WordsMatch(name, "fullscreen")) {
        return port_ReadChoice(value, "off", "on", &settings->is_fullscreen)
                   ? NULL
                   : "fullscreen is on or off";
    }
    if (port_WordsMatch(name, "vsync")) {
        return port_ReadChoice(value, "off", "on", &settings->vsync)
                   ? NULL
                   : "vsync is on or off";
    }
    if (port_WordsMatch(name, "deadzone")) {
        if (!port_ReadDigits(value, &number) || number < 1 ||
            number >= PERCENT) {
            return "deadzone is a number from 1 to 99";
        }
        settings->deadzone = number;
        return NULL;
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
    static char text[sizeof(g_port_default_settings)];
    u32 i;

    for (i = 0; i < sizeof(text); i++) {
        text[i] = g_port_default_settings[i];
    }
    settings->count = 0;
    settings->scale = PORT_SCALE_FIT;
    settings->is_fullscreen = 0;
    settings->is_linear = 0;
    settings->vsync = 1;
    settings->deadzone = PORT_DEADZONE_DEFAULT;
    /* a name the window's library does not know leaves that one out */
    port_ReadLines(settings, text, lookup, NULL, 1);
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
