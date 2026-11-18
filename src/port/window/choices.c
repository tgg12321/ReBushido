/* The native port's window: the settings of rebushido.cfg as the settings
 * menu shows them (window.h; docs/PLAYING.md, "The settings menu"). A
 * value is set by reading the line the file would have for it, so the
 * file and the menu can never mean two things by one word. No SDL and no
 * C library: tests/port/settings.c runs it. */
#include "common.h"
#include "window.h"

#define LINE_BYTES_MAX 128

const PortChoice g_port_choices[] = {
    /* the window */
    {"fullscreen",
     "Full screen",
     "The whole screen, or a window (F11 does the same)",
     PORT_TAKES_NOW,
     2,
     {"off", "on"},
     {"Off", "On"}},
    {"scale",
     "Picture size",
     "As large as fits, whole multiples of 320x240, or all of the window",
     PORT_TAKES_NOW,
     3,
     {"fit", "integer", "stretch"},
     {"Fit", "Whole multiples", "Stretch"}},
    {"filter",
     "Pixels",
     "The console's picture: blocks, or smoothed as it is scaled",
     PORT_TAKES_NOW,
     2,
     {"nearest", "linear"},
     {"Sharp", "Smooth"}},
    {"vsync",
     "Wait for the display",
     "Vertical sync: off can tear the picture",
     PORT_TAKES_NOW,
     2,
     {"off", "on"},
     {"Off", "On"}},
    {"fps",
     "Counter",
     "Pictures and frames a second in a corner, and where the time goes (F3)",
     PORT_TAKES_NOW,
     3,
     {"off", "on", "times"},
     {"Off", "On", "With times"}},
    {"fps_corner",
     "Counter's corner",
     "Where the counter is",
     PORT_TAKES_NOW,
     4,
     {"top-left", "top-right", "bottom-left", "bottom-right"},
     {"Top left", "Top right", "Bottom left", "Bottom right"}},
    {"run_ahead",
     "Run ahead",
     "A press seen 1 or 2 frames sooner; the game computes the same",
     PORT_TAKES_NOW,
     3,
     {"0", "1", "2"},
     {"Off", "1 frame", "2 frames"}},
    {"low_latency",
     "Low latency",
     "Each picture waits for the card: none queued behind another",
     PORT_TAKES_NOW,
     2,
     {"off", "on"},
     {"Off", "On"}},
    {"quick_loading",
     "Quick loading",
     "The waits for the disc behind a loading picture hurried through",
     PORT_TAKES_NOW,
     2,
     {"off", "on"},
     {"Off", "On"}},
    /* the remastered picture */
    {"graphics",
     "Graphics",
     "The console's own picture, or the scene drawn again (F2)",
     PORT_TAKES_NOW,
     2,
     {"original", "remastered"},
     {"Original", "Remastered"}},
    {"resolution",
     "Resolution",
     "How many times the console's the scene is drawn at",
     PORT_TAKES_NOW,
     7,
     {"0", "1", "2", "3", "4", "6", "8"},
     {"The window's", "1x", "2x", "3x", "4x", "6x", "8x"}},
    {"antialiasing",
     "Antialiasing",
     "The scene's edges smoothed: 4 or 8 samples a pixel",
     PORT_TAKES_RESTART,
     3,
     {"off", "2", "4"},
     {"Off", "2x", "4x"}},
    {"draw_distance",
     "Draw distance",
     "How much of the stage is drawn, and its fog pushed out",
     PORT_TAKES_RESTART,
     3,
     {"original", "far", "max"},
     {"Original", "Far", "The whole stage"}},
    {"aspect",
     "Shape",
     "A wider view of the same scene, not a stretched picture",
     PORT_TAKES_RESTART,
     4,
     {"4:3", "16:10", "16:9", "21:9"},
     {"4:3", "16:10", "16:9", "21:9"}},
    {"stage_mesh",
     "Ground as one mesh",
     "The stage's ground from one mesh instead of the game's tiles",
     PORT_TAKES_NOW,
     2,
     {"off", "on"},
     {"Off", "On"}},
    {"stage_props_mesh",
     "Railings and walls as meshes",
     "What stands on the ground drawn still, from meshes of its own",
     PORT_TAKES_NOW,
     2,
     {"off", "on"},
     {"Off", "On"}},
    {"shadows",
     "Shadows",
     "The game's black ones, the ground darkened, or cast by the body",
     PORT_TAKES_NOW,
     3,
     {"original", "soft", "cast"},
     {"Original", "Soft", "Cast"}},
    {"sky",
     "Sky",
     "A night sky over the stages open to it, or the game's dark",
     PORT_TAKES_NOW,
     2,
     {"original", "night"},
     {"Original", "Night"}},
    {"see_through",
     "See through walls",
     "The stage thinned where it hides a fighter from the camera",
     PORT_TAKES_NOW,
     2,
     {"off", "on"},
     {"Off", "On"}},
    {"fighter_models",
     "Fighters' own models",
     "The fighters drawn from a mod's models where it has them",
     PORT_TAKES_NOW,
     2,
     {"off", "on"},
     {"Off", "On"}},
    {"smooth_light",
     "Smooth lighting",
     "The game's own lights worked out at each pixel of ground and body",
     PORT_TAKES_NOW,
     2,
     {"off", "on"},
     {"Off", "On"}},
    {"effects",
     "Effects",
     "Blood, sparks and dust: as placed, exactly placed, or smoothed too",
     PORT_TAKES_NOW,
     3,
     {"original", "exact", "smooth"},
     {"Original", "Exact", "Smooth"}},
    {"frame_rate",
     "Frame rate",
     "Pictures a second; the game itself steps as the console does",
     PORT_TAKES_RESTART,
     6,
     {"original", "display", "60", "120", "144", "240"},
     {"Original", "The display's", "60", "120", "144", "240"}},
    /* the mods */
    {"english",
     "English top menu",
     "The top menu's Japanese drawn again in English (Remastered)",
     PORT_TAKES_RESTART,
     2,
     {"off", "on"},
     {"Off", "On"}},
    {"ui",
     "Sharp menus",
     "The menus' lettering drawn again at the window's size (Remastered)",
     PORT_TAKES_RESTART,
     2,
     {"off", "on"},
     {"Off", "On"}},
    {"mods",
     "Mods",
     "The mods in mods/ beside the program",
     PORT_TAKES_RESTART,
     2,
     {"off", "on"},
     {"Off", "On"}},
    {"music_mods",
     "Mods' music",
     "A mod's music in place of the game's, where a mod has some",
     PORT_TAKES_NOW,
     2,
     {"off", "on"},
     {"Off", "On"}},
    /* the controls */
    {"deadzone",
     "Stick dead zone",
     "How far a stick is pushed before it counts",
     PORT_TAKES_NOW,
     7,
     {"20", "30", "40", "50", "60", "70", "80"},
     {"20%", "30%", "40%", "50%", "60%", "70%", "80%"}},
};

const s32 g_port_choice_count =
    (s32)(sizeof(g_port_choices) / sizeof(g_port_choices[0]));

const PortChoiceGroup g_port_choice_groups[PORT_CHOICE_GROUPS] = {
    {"The window", 0, 9},
    {"The remastered picture", 9, 14},
    {"Mods", 23, 4},
    {"Controls", 27, 1},
};

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

    if (choice < 0 || choice >= g_port_choice_count || value < 0 ||
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

    if (choice < 0 || choice >= g_port_choice_count) {
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

    if (choice < 0 || choice >= g_port_choice_count) {
        return -1;
    }
    count = g_port_choices[choice].count;
    if (value < 0) {
        return step > 0 ? 0 : count - 1;
    }
    return (value + step + count) % count;
}
