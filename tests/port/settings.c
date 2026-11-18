/* The settings menu's arithmetic, without SDL: rebushido.cfg changed a
 * line at a time (src/port/window/settext.c) and the menu's settings and
 * their values (src/port/window/choices.c), held to cases worked out by
 * hand and to the settings file's own reader (keymap.c). */
#include "common.h"
#include "src/port/window/settext.c"
#include "src/port/window/choices.c"
#include "src/port/window/keymap.c"
#include "runtime/runtime.h"

#define ROOM 512

static char g_test_text[ROOM];

static void test_Set(const char *text) {
    u32 i;

    for (i = 0; text[i] != 0; i++) {
        g_test_text[i] = text[i];
    }
    g_test_text[i] = 0;
}

static s32 test_Is(const char *want) {
    u32 i;

    for (i = 0; want[i] != 0; i++) {
        if (g_test_text[i] != want[i]) {
            test_Print("got:\n%s\nwanted:\n%s\n", g_test_text, want);
            return FALSE;
        }
    }
    if (g_test_text[i] != 0) {
        test_Print("got:\n%s\nwanted:\n%s\n", g_test_text, want);
    }
    return g_test_text[i] == 0;
}

/* ---- one setting's line ---- */

static void test_SettingText(void) {
    /* the value changed in place, the comment kept in its column */
    test_Set("# mine\nscale = fit            # fit, integer or stretch\n");
    CHECK(port_SetSettingText(g_test_text, ROOM, "scale", "stretch"));
    CHECK(
        test_Is("# mine\nscale = stretch        # fit, integer or stretch\n"));
    /* shorter again: the column holds */
    CHECK(port_SetSettingText(g_test_text, ROOM, "scale", "fit"));
    CHECK(
        test_Is("# mine\nscale = fit            # fit, integer or stretch\n"));
    /* longer than the spaces before the comment: one space is kept */
    test_Set("fps = off # no counter\n");
    CHECK(port_SetSettingText(g_test_text, ROOM, "fps", "on"));
    CHECK(test_Is("fps = on  # no counter\n"));
    CHECK(port_SetSettingText(g_test_text, ROOM, "fps", "a-long-value"));
    CHECK(test_Is("fps = a-long-value # no counter\n"));
    /* no comment */
    test_Set("deadzone = 40\nfps = on\n");
    CHECK(port_SetSettingText(g_test_text, ROOM, "deadzone", "5"));
    CHECK(test_Is("deadzone = 5\nfps = on\n"));
    /* the last line of a name is the one that counts, and is changed */
    test_Set("fps = on\nscale = fit\nfps = off\n");
    CHECK(port_SetSettingText(g_test_text, ROOM, "fps", "on"));
    CHECK(test_Is("fps = on\nscale = fit\nfps = on\n"));
    /* letters of either case, spaces anywhere, a line of Windows' */
    test_Set("  FPS=off\r\nscale = fit\r\n");
    CHECK(port_SetSettingText(g_test_text, ROOM, "fps", "on"));
    CHECK(test_Is("  FPS=on\r\nscale = fit\r\n"));
    /* a name that only begins the same, a comment, a line with no `=`:
     * none of them is the setting, which is added at the end */
    test_Set("fps_corner = top-left\n# fps = on\nfps\n");
    CHECK(port_SetSettingText(g_test_text, ROOM, "fps", "off"));
    CHECK(test_Is("fps_corner = top-left\n# fps = on\nfps\nfps = off\n"));
    /* a last line without its end is given one first */
    test_Set("scale = fit");
    CHECK(port_SetSettingText(g_test_text, ROOM, "aspect", "4:3"));
    CHECK(test_Is("scale = fit\naspect = 4:3\n"));
    /* an empty file */
    test_Set("");
    CHECK(port_SetSettingText(g_test_text, ROOM, "mods", "off"));
    CHECK(test_Is("mods = off\n"));
    /* an empty value is given one before its comment */
    test_Set("mods =   # nothing yet\n");
    CHECK(port_SetSettingText(g_test_text, ROOM, "mods", "on"));
    CHECK(test_Is("mods = on # nothing yet\n"));
    test_Set("mods =\nfps=\n");
    CHECK(port_SetSettingText(g_test_text, ROOM, "mods", "on"));
    CHECK(port_SetSettingText(g_test_text, ROOM, "fps", "off"));
    CHECK(test_Is("mods = on\nfps= off\n"));
    /* what does not fit changes nothing */
    test_Set("fps = on\n");
    CHECK(!port_SetSettingText(g_test_text, 12, "fps", "a-long-value"));
    CHECK(test_Is("fps = on\n"));
    CHECK(!port_SetSettingText(g_test_text, 12, "scale", "fit"));
    CHECK(test_Is("fps = on\n"));
    CHECK(port_SetSettingText(g_test_text, 10, "fps", "no"));
    CHECK(test_Is("fps = no\n"));
}

/* ---- the buttons' lines ---- */

static void test_BindingText(void) {
    /* a key: every key line of the button and its none go, the
     * controller's line stays, the new line is at the end */
    test_Set("controls = original\np1.cross = key Z\np1.cross = pad a\n"
             "p1.cross = none\np1.cross = key X  # second\n"
             "p1.cross+circle = key Q\np2.cross = key G\n");
    CHECK(
        port_SetBindingText(g_test_text, ROOM, "p1.cross", TRUE, "key Space"));
    CHECK(test_Is("controls = original\np1.cross = pad a\n"
                  "p1.cross+circle = key Q\np2.cross = key G\n"
                  "p1.cross = key Space\n"));
    /* a controller's: the pad lines go, the keys stay; a name is matched in
     * letters of either case and written as it is given */
    CHECK(port_SetBindingText(
        g_test_text, ROOM, "P1.Cross", FALSE, "pad +righttrigger"));
    CHECK(test_Is("controls = original\np1.cross+circle = key Q\n"
                  "p2.cross = key G\np1.cross = key Space\n"
                  "P1.Cross = pad +righttrigger\n"));
    /* none: nothing at all presses it */
    CHECK(port_SetBindingText(g_test_text, ROOM, "p2.cross", TRUE, "none"));
    CHECK(test_Is("controls = original\np1.cross+circle = key Q\n"
                  "p1.cross = key Space\nP1.Cross = pad +righttrigger\n"
                  "p2.cross = none\n"));
    /* no value: the layout's own again */
    CHECK(port_SetBindingText(g_test_text, ROOM, "p1.cross", TRUE, NULL));
    CHECK(test_Is("controls = original\np1.cross+circle = key Q\n"
                  "P1.Cross = pad +righttrigger\np2.cross = none\n"));
    /* what does not fit leaves the lines as they were */
    test_Set("p1.cross = key Z\n");
    CHECK(!port_SetBindingText(
        g_test_text, 20, "p1.cross", TRUE, "key Left Shift"));
    CHECK(test_Is("p1.cross = key Z\n"));
    /* a pad's every line, or both pads' */
    test_Set("fps = on\np1.cross = key Z\np2.l1 = key R\np1x = 3\n"
             "p1.l1+r1 = key Q\n");
    port_ClearBindingsText(g_test_text, 0);
    CHECK(test_Is("fps = on\np2.l1 = key R\np1x = 3\n"));
    port_ClearBindingsText(g_test_text, -1);
    CHECK(test_Is("fps = on\np1x = 3\n"));
}

/* ---- what the reader makes of the lines written ---- */

static s32 test_LookUp(s32 source, const char *name) {
    static const char * const keys[] = {"Z", "X", "Space", "Return"};
    u32 i;

    for (i = 0; i < sizeof(keys) / sizeof(keys[0]); i++) {
        const char *a = keys[i];
        const char *b = name;

        while (*a != 0 && *a == *b) {
            a++;
            b++;
        }
        if (*a == 0 && *b == 0) {
            return source == PORT_SOURCE_KEY ? 100 + (s32)i : (s32)i;
        }
    }
    return source == PORT_SOURCE_KEY ? -1 : 0;
}

static PortSettings g_test_settings;

static s32 test_KeysOf(u32 button) {
    s32 count = 0;
    s32 i;

    for (i = 0; i < g_test_settings.count; i++) {
        const PortBinding *binding = &g_test_settings.bindings[i];

        count += binding->pad == 0 && binding->button == button &&
                 binding->source == PORT_SOURCE_KEY;
    }
    return count;
}

static void test_ReadBack(void) {
    static char copy[ROOM];
    u32 i;

    test_Set("controls = original\np1.cross = key X\np1.cross = key Z\n");
    CHECK(
        port_SetBindingText(g_test_text, ROOM, "p1.cross", TRUE, "key Space"));
    for (i = 0; i <= ROOM - 1 && g_test_text[i] != 0; i++) {
        copy[i] = g_test_text[i];
    }
    copy[i] = 0;
    port_SetDefaultSettings(&g_test_settings, test_LookUp);
    CHECK_EQ(port_ReadSettings(&g_test_settings, copy, test_LookUp, NULL), 0);
    /* Space alone presses Cross, and nothing else of the file's */
    CHECK_EQ(test_KeysOf(port_FindPadButton("cross")), 1);
    CHECK_EQ(
        port_FindBinding(&g_test_settings, TRUE, 0, port_FindPadButton("cross"))
            ->code,
        102);
}

/* ---- the menu's settings ---- */

static void test_Choices(void) {
    PortSettings *settings = &g_test_settings;
    s32 group;
    s32 choice;
    s32 value;
    s32 next = 0;

    /* the groups take every setting once, in order */
    for (group = 0; group < PORT_CHOICE_GROUPS; group++) {
        CHECK_EQ(g_port_choice_groups[group].first, next);
        next += g_port_choice_groups[group].count;
    }
    CHECK_EQ(next, g_port_choice_count);
    /* every value of every setting is a line the reader takes, and reads
     * back as that value */
    for (choice = 0; choice < g_port_choice_count; choice++) {
        CHECK(g_port_choices[choice].count >= 2);
        CHECK(g_port_choices[choice].count <= PORT_CHOICES_MAX);
        for (value = 0; value < g_port_choices[choice].count; value++) {
            port_SetDefaultSettings(settings, test_LookUp);
            CHECK(port_SetChoice(settings, choice, value));
            if (port_GetChoice(settings, choice) != value) {
                test_Print(
                    "%s = %s reads back as %d\n", g_port_choices[choice].name,
                    g_port_choices[choice].words[value],
                    port_GetChoice(settings, choice));
            }
            CHECK_EQ(port_GetChoice(settings, choice), value);
        }
    }
    /* the settings built in, as the menu shows them */
    port_SetDefaultSettings(settings, test_LookUp);
    for (choice = 0; choice < g_port_choice_count; choice++) {
        static const struct {
            const char *name;
            s32 value;
        } built_in[] = {
            {"fullscreen", 1},    {"scale", 0},
            {"filter", 0},        {"vsync", 1},
            {"fps", 1},           {"fps_corner", 0},
            {"graphics", 1},      {"resolution", 0},
            {"antialiasing", 1},  {"draw_distance", 2},
            {"aspect", 2},        {"stage_mesh", 1},
            {"shadows", 2},       {"frame_rate", 1},
            {"english", 1},       {"ui", 1},
            {"mods", 1},          {"music_mods", 1},
            {"deadzone", 2},      {"sky", 1},
            {"see_through", 1},   {"fighter_models", 1},
            {"quick_loading", 1}, {"smooth_light", 1},
            {"effects", 2},       {"run_ahead", 2},
            {"low_latency", 1},   {"stage_props_mesh", 1},
        };

        u32 i;

        for (i = 0; i < sizeof(built_in) / sizeof(built_in[0]); i++) {
            const char *a = built_in[i].name;
            const char *b = g_port_choices[choice].name;

            while (*a != 0 && *a == *b) {
                a++;
                b++;
            }
            if (*a == 0 && *b == 0) {
                CHECK_EQ(port_GetChoice(settings, choice), built_in[i].value);
                break;
            }
        }
        CHECK(i < sizeof(built_in) / sizeof(built_in[0]));
    }
    /* a value of the file's own that is none of the menu's */
    test_Set("shadows = cast:50\naspect = 2:1\n");
    port_ReadSettings(settings, g_test_text, test_LookUp, NULL);
    for (choice = 0; choice < g_port_choice_count; choice++) {
        const char *name = g_port_choices[choice].name;

        if (name[0] == 's' && name[1] == 'h') {
            CHECK_EQ(port_GetChoice(settings, choice), -1);
            /* stepped from it: the first, or the last */
            CHECK_EQ(port_StepChoice(choice, -1, 1), 0);
            CHECK_EQ(port_StepChoice(choice, -1, -1),
                     g_port_choices[choice].count - 1);
        }
        if (name[0] == 'a' && name[1] == 's') {
            CHECK_EQ(port_GetChoice(settings, choice), -1);
            CHECK(port_SetChoice(settings, choice, 0));
            CHECK_EQ(settings->aspect_w, 4);
            CHECK_EQ(settings->aspect_h, 3);
        }
    }
    /* stepping goes round both ways */
    CHECK_EQ(port_StepChoice(1, 2, 1), 0);
    CHECK_EQ(port_StepChoice(1, 0, -1), 2);
    CHECK_EQ(port_StepChoice(1, 1, 1), 2);
    CHECK_EQ(port_StepChoice(-1, 0, 1), -1);
    CHECK_EQ(port_GetChoice(settings, g_port_choice_count), -1);
    CHECK(!port_SetChoice(settings, 0, 9));
}

void test_Main(void) {
    test_SettingText();
    test_BindingText();
    test_ReadBack();
    test_Choices();
}
