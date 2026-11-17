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

/* The settings built in, the layout of the keys and buttons apart. */
const char g_port_default_settings[] =
    "# ReBushido's window: the settings built in. Copy what you want to\n"
    "# change into rebushido.cfg, beside the program.\n"
    "\n"
    "scale = fit            # fit, integer or stretch\n"
    "filter = nearest       # nearest or linear\n"
    "fullscreen = on        # on or off: how it starts\n"
    "vsync = on             # on or off: wait for the display\n"
    "deadzone = 40          # how far a stick counts, in parts of 100\n"
    "fps = on               # on or off: the counter in the corner (F3)\n"
    "fps_corner = top-left  # top-left, top-right, bottom-left,\n"
    "                       # bottom-right\n"
    "run_ahead = 2          # 0, 1 or 2: frames run ahead and put back,\n"
    "                       # so that a press is seen that many of the\n"
    "                       # game's frames sooner; what the game\n"
    "                       # computes is the same\n"
    "low_latency = on       # on: each picture waits for the card, so\n"
    "                       # none queues behind another; off lets the\n"
    "                       # driver queue them\n"
    "\n"
    "# The remastered picture (F2 changes picture while the game runs).\n"
    "# These four are a run's: changing one means starting it again.\n"
    "graphics = remastered  # original, remastered, or follow (the\n"
    "                       # options page at boot says which)\n"
    "resolution = 0         # how many times the console's; 0 is the\n"
    "                       # window's own size\n"
    "draw_distance = max    # original, far or max: how much of the stage\n"
    "antialiasing = 2       # off, 2 or 4: the scene's edges smoothed\n"
    "aspect = 16:9          # 16:9 for a wider view of the same scene,\n"
    "                       # or 4:3 as the console\n"
    "stage_mesh = on        # the stage's ground drawn from one static\n"
    "                       # mesh instead of the game's own tiles; off\n"
    "                       # for the tiles. A stage the mesh is not\n"
    "                       # measured good on keeps the tiles either way\n"
    "fighter_models = on    # the fighters drawn from models of their own\n"
    "                       # where a mod's models/ has one (M100.glb);\n"
    "                       # off for the game's own\n"
    "shadows = cast         # original: the game's own black shadows;\n"
    "                       # soft: the ground under them darkened;\n"
    "                       # cast: cast from the fighters' own\n"
    "                       # bodies, darkened (cast:N, N hundredths)\n"
    "frame_rate = display   # original: one picture a blank, as the\n"
    "                       # console; display: as many as the screen\n"
    "                       # shows; or a number a second. The game\n"
    "                       # steps at the console's rate either way\n"
    "sky = night            # night: a night sky over the stages open\n"
    "                       # to it; original: the game's flat colour\n"
    "                       # of the distance\n"
    "see_through = on       # the stage's walls thinned where they hide\n"
    "                       # a fighter from the camera; off as the\n"
    "                       # console\n"
    "mods = on              # the mods in mods/ beside the program\n"
    "                       # (docs/MODS.md); off for the game's own\n"
    "english = on           # the top menu in English, a mod the program\n"
    "                       # ships (mods/english/); off for Japanese\n"
    "ui = on                # the menus' lettering drawn again, sharp at\n"
    "                       # any size: a mod the program ships (mods/ui/)\n"
    "music_mods = on        # a mod's music in place of the game's; off\n"
    "                       # for the game's own (docs/MODS.md)\n"
    "\n"
    "# Playing online (docs/PLAYING.md): what the settings menu's Play\n"
    "# online page starts with, and keeps.\n"
    "net_role = host        # host: wait for the other player; join: go to\n"
    "                       # the host at net_address\n"
    "net_address = 127.0.0.1:7845  # the host's address and port\n"
    "net_port = 7845        # the port a host waits at\n"
    "net_listen = all       # all: a host is found at every address of this\n"
    "                       # PC; local: on this PC alone (127.0.0.1)\n"
    "net_delay = 2          # frames a press counts later, 0 to 8: the\n"
    "                       # host's is both players'\n"
    "\n"
    "# The keys and the controllers' buttons: a layout to start from, and\n"
    "# lines of pN.BUTTON for what to change in it (below)\n"
    "controls = original    # original is the only one so far\n";

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
    "p2.select = pad back\n";

const PortLayout g_port_layouts[PORT_LAYOUTS] = {
    {"original", g_port_layout_original},
};

/* The most a layout's text may be: it is read from a copy. */
#define LAYOUT_BYTES_MAX 4096

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

/* `A.B.C.D:PORT` into `out` (PORT_NET_ADDRESS_BYTES), as it was written:
 * 1, or 0 if it is not one. Only its form is looked at here; the port
 * reads it again as it starts (port_ParseNetAddress). */
static s32 port_ReadNetAddress(const char *value, char *out) {
    s32 dots = 0;
    s32 colons = 0;
    s32 i;

    for (i = 0; value[i] != 0; i++) {
        if (i == PORT_NET_ADDRESS_BYTES - 1) {
            return 0;
        }
        if (value[i] == '.') {
            dots++;
        } else if (value[i] == ':') {
            colons++;
        } else if (value[i] < '0' || value[i] > '9') {
            return 0;
        }
    }
    if (dots != 3 || colons != 1) {
        return 0;
    }
    for (i = 0; value[i] != 0; i++) {
        out[i] = value[i];
    }
    out[i] = 0;
    return 1;
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
    if (port_WordsMatch(name, "fps")) {
        return port_ReadChoice(value, "off", "on", &settings->fps)
                   ? NULL
                   : "fps is on or off";
    }
    if (port_WordsMatch(name, "run_ahead")) {
        if (!port_ReadDigits(value, &number) || number < 0 ||
            number > PORT_RUN_AHEAD_MAX) {
            return "run_ahead is 0, 1 or 2";
        }
        settings->run_ahead = number;
        return NULL;
    }
    if (port_WordsMatch(name, "low_latency")) {
        return port_ReadChoice(value, "off", "on", &settings->low_latency)
                   ? NULL
                   : "low_latency is on or off";
    }
    if (port_WordsMatch(name, "fps_corner")) {
        static const char * const corners[] = {
            "top-left", "top-right", "bottom-left", "bottom-right"};
        s32 corner;

        for (corner = 0; corner <= PORT_CORNER_BOTTOM_RIGHT; corner++) {
            if (port_WordsMatch(value, corners[corner])) {
                settings->fps_corner = corner;
                return NULL;
            }
        }
        return "fps_corner is top-left, top-right, bottom-left or "
               "bottom-right";
    }
    if (port_WordsMatch(name, "deadzone")) {
        if (!port_ReadDigits(value, &number) || number < 1 ||
            number >= PERCENT) {
            return "deadzone is a number from 1 to 99";
        }
        settings->deadzone = number;
        return NULL;
    }
    if (port_WordsMatch(name, "graphics")) {
        if (port_WordsMatch(value, "original")) {
            settings->graphics = PORT_GRAPHICS_ORIGINAL;
        } else if (port_WordsMatch(value, "remastered")) {
            settings->graphics = PORT_GRAPHICS_REMASTERED;
        } else if (port_WordsMatch(value, "follow")) {
            settings->graphics = PORT_GRAPHICS_FOLLOW;
        } else {
            return "graphics is original, remastered or follow";
        }
        return NULL;
    }
    if (port_WordsMatch(name, "resolution")) {
        if (!port_ReadDigits(value, &number) || number < 0 ||
            number > PORT_REMASTER_SCALE_MAX) {
            return "resolution is 0 (the window's own size) or a number up "
                   "to 8";
        }
        settings->resolution = number;
        return NULL;
    }
    if (port_WordsMatch(name, "draw_distance")) {
        /* the renderer's own names, so that they are said in one place,
         * and letters of either case as every other value here */
        for (number = 0; number < PORT_DISTANCE_KINDS; number++) {
            if (port_WordsMatch(value, port_NameRemasterDistance(number))) {
                settings->distance = number;
                return NULL;
            }
        }
        return "draw_distance is original, far or max";
    }
    if (port_WordsMatch(name, "antialiasing")) {
        if (port_WordsMatch(value, "off")) {
            settings->supersample = 1;
            return NULL;
        }
        if (!port_ReadDigits(value, &number) || number < 1 ||
            number > PORT_SUPERSAMPLE_MAX) {
            return "antialiasing is off, 2 or 4";
        }
        settings->supersample = number;
        return NULL;
    }
    if (port_WordsMatch(name, "aspect")) {
        return port_ReadShape(value, settings)
                   ? NULL
                   : "aspect is W:H, as 4:3 or 16:9";
    }
    if (port_WordsMatch(name, "mods")) {
        return port_ReadChoice(value, "off", "on", &settings->mods)
                   ? NULL
                   : "mods is on or off";
    }
    if (port_WordsMatch(name, "english")) {
        return port_ReadChoice(value, "off", "on", &settings->english)
                   ? NULL
                   : "english is on or off";
    }
    if (port_WordsMatch(name, "ui")) {
        return port_ReadChoice(value, "off", "on", &settings->ui)
                   ? NULL
                   : "ui is on or off";
    }
    if (port_WordsMatch(name, "music_mods")) {
        return port_ReadChoice(value, "off", "on", &settings->music_mods)
                   ? NULL
                   : "music_mods is on or off";
    }
    if (port_WordsMatch(name, "stage_mesh")) {
        return port_ReadChoice(value, "off", "on", &settings->stage_mesh)
                   ? NULL
                   : "stage_mesh is on or off";
    }
    if (port_WordsMatch(name, "sky")) {
        return port_ReadChoice(value, "original", "night", &settings->sky)
                   ? NULL
                   : "sky is original or night";
    }
    if (port_WordsMatch(name, "see_through")) {
        return port_ReadChoice(value, "off", "on", &settings->see_through)
                   ? NULL
                   : "see_through is on or off";
    }
    if (port_WordsMatch(name, "fighter_models")) {
        return port_ReadChoice(value, "off", "on", &settings->fighter_models)
                   ? NULL
                   : "fighter_models is on or off";
    }
    if (port_WordsMatch(name, "shadows")) {
        if (port_WordsMatch(value, "original")) {
            settings->shadows = 0;
            return NULL;
        }
        if (port_WordsMatch(value, "soft")) {
            settings->shadows = PORT_SHADOWS_SOFT;
            return NULL;
        }
        /* cast, cast:N, or a number (port_remaster.h) */
        number = port_ReadRemasterShadows(value);
        if (number < 0) {
            return "shadows is original, soft, cast, cast:N, or how much of "
                   "the ground a shadow takes away, in hundredths";
        }
        settings->shadows = number;
        return NULL;
    }
    if (port_WordsMatch(name, "controls")) {
        return port_ReadLayout(settings, value, lookup);
    }
    if (port_WordsMatch(name, "net_role")) {
        return port_ReadChoice(value, "host", "join", &settings->net_role)
                   ? NULL
                   : "net_role is host or join";
    }
    if (port_WordsMatch(name, "net_listen")) {
        return port_ReadChoice(value, "local", "all", &settings->net_listen)
                   ? NULL
                   : "net_listen is all or local";
    }
    if (port_WordsMatch(name, "net_port")) {
        if (!port_ReadDigits(value, &number) || number < 1 ||
            number > PORT_NET_PORT_MAX) {
            return "net_port is a number from 1 to 65535";
        }
        settings->net_port = number;
        return NULL;
    }
    if (port_WordsMatch(name, "net_delay")) {
        if (!port_ReadDigits(value, &number) || number < 0 ||
            number > PORT_NET_DELAY_MAX) {
            return "net_delay is a number from 0 to 8";
        }
        settings->net_delay = number;
        return NULL;
    }
    if (port_WordsMatch(name, "net_address")) {
        return port_ReadNetAddress(value, settings->net_address)
                   ? NULL
                   : "net_address is four numbers and a port, as "
                     "192.168.1.20:7845";
    }
    if (port_WordsMatch(name, "frame_rate")) {
        if (port_WordsMatch(value, "original")) {
            settings->frame_rate = PORT_FRAME_RATE_ORIGINAL;
            return NULL;
        }
        if (port_WordsMatch(value, "display")) {
            settings->frame_rate = PORT_FRAME_RATE_DISPLAY;
            return NULL;
        }
        if (!port_ReadDigits(value, &number) || number < 1 ||
            number > PORT_FRAME_RATE_MAX) {
            return "frame_rate is original, display, or a number of "
                   "pictures a second";
        }
        settings->frame_rate = number;
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
    settings->own_controls = 0;
    settings->layout = 0;
    settings->scale = PORT_SCALE_FIT;
    settings->is_fullscreen = 1;
    settings->is_linear = 0;
    settings->vsync = 1;
    settings->deadzone = PORT_DEADZONE_DEFAULT;
    settings->fps = 1;
    settings->fps_corner = PORT_CORNER_TOP_LEFT;
    /* a press seen two frames sooner, and no picture queued behind
     * another (docs/PORT.md, "Input lag") */
    settings->run_ahead = PORT_RUN_AHEAD_DEFAULT;
    settings->low_latency = 1;
    /* every new feature on in the builds the owner plays (2026-11-17) */
    settings->graphics = PORT_GRAPHICS_REMASTERED;
    settings->resolution = PORT_RESOLUTION_WINDOW;
    /* the whole stage, every frame: nothing of it comes and goes
     * as the camera turns (docs/PORT.md, "How far the stage is
     * drawn") */
    settings->distance = PORT_DISTANCE_MAX;
    settings->supersample = 2;
    /* the remastered picture is a wider view of the same scene, with the
     * fight's HUD at its edges (port_remaster.h); "Graphics: Original"
     * is the console's four by three whatever this says */
    settings->aspect_w = PORT_ASPECT_W_WIDE;
    settings->aspect_h = PORT_ASPECT_H_WIDE;
    settings->frame_rate = PORT_FRAME_RATE_DISPLAY;
    /* The ground drawn as one mesh, which is what Remastered means by
     * the stage now: it has been seen on a real card and is measured
     * stage by stage (src/port/gl/gl_stage.h, g_stage_proved). A stage
     * the measurements do not hold keeps the game's own tiles of its own
     * accord, and the run's log says which path each stage took. */
    settings->stage_mesh = 1;
    /* a fighter with a model of his own in mods/fighters/ is drawn from
     * it; with none there, nothing changes (gl_fighter.h) */
    settings->fighter_models = 1;
    /* the shadows cast from the fighters' own bodies (gl_shadow.h) */
    settings->shadows = port_ReadRemasterShadows("cast");
    /* A sky over the stages open to it, and the walls thinned where they
     * hide a fighter: neither changes what the game computes, and both
     * are the owner's to confirm on a real card (gl_sky.h, gl_see.h). */
    settings->sky = 1;
    settings->see_through = 1;
    /* the mods beside the program, where there are any (docs/MODS.md) */
    settings->mods = 1;
    /* the English top menu the program ships (docs/MODS.md) */
    settings->english = 1;
    /* and the menus' lettering drawn again (docs/MODS.md) */
    settings->ui = 1;
    /* the Play online page's (the text below says the same) */
    settings->net_role = 0;
    settings->net_port = PORT_NET_PORT_DEFAULT;
    settings->net_listen = 1;
    settings->net_delay = PORT_NET_DELAY_DEFAULT;
    settings->net_address[0] = 0;
    /* a mod's music, where a mod has some (docs/MODS.md) */
    settings->music_mods = 1;
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
