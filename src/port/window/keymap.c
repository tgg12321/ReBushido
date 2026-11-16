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
    "fps = on               # on or off: the counter in the corner (F3)\n"
    "fps_corner = top-left  # top-left, top-right, bottom-left,\n"
    "                       # bottom-right\n"
    "\n"
    "# The remastered picture (F2 changes picture while the game runs).\n"
    "# These four are a run's: changing one means starting it again.\n"
    "graphics = follow      # original, remastered, or follow (the\n"
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
    "frame_rate = original  # original: one picture a blank, as the\n"
    "                       # console; display: as many as the screen\n"
    "                       # shows; or a number a second. The game\n"
    "                       # steps at the console's rate either way\n"
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
    if (port_WordsMatch(name, "fps")) {
        return port_ReadChoice(value, "off", "on", &settings->fps)
                   ? NULL
                   : "fps is on or off";
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
    if (port_WordsMatch(name, "stage_mesh")) {
        return port_ReadChoice(value, "off", "on", &settings->stage_mesh)
                   ? NULL
                   : "stage_mesh is on or off";
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
    settings->scale = PORT_SCALE_FIT;
    settings->is_fullscreen = 0;
    settings->is_linear = 0;
    settings->vsync = 1;
    settings->deadzone = PORT_DEADZONE_DEFAULT;
    settings->fps = 1;
    settings->fps_corner = PORT_CORNER_TOP_LEFT;
    settings->graphics = PORT_GRAPHICS_FOLLOW;
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
    settings->frame_rate = PORT_FRAME_RATE_ORIGINAL;
    /* The ground drawn as one mesh, which is what Remastered means by
     * the stage now: it has been seen on a real card and is measured
     * stage by stage (src/port/gl/gl_stage.h, g_stage_proved). A stage
     * the measurements do not hold keeps the game's own tiles of its own
     * accord, and the run's log says which path each stage took. */
    settings->stage_mesh = 1;
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
