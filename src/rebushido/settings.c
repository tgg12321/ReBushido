/* ReBushido's settings: the player's choices that a change asks about while
 * the game runs, and the table that names them for the options page
 * (options.c) and the settings' file (save.c). */
#include "common.h"
#include "bb2.h"

/* As the original: no fix on, the retail picture. */
RbSettings g_rb_settings = {FALSE, RB_GRAPHICS_ORIGINAL};

const RbOption g_rb_options[RB_OPTION_COUNT] = {
    {"Bugfixes",
     {"Off", "On"},
     2,
     RB_SAVE_SLOT_BUGFIXES,
     &g_rb_settings.bugfixes},
    {"Graphics",
     {"Original", "Remastered"},
     2,
     RB_SAVE_SLOT_GRAPHICS,
     &g_rb_settings.graphics},
};

/* Puts every setting back to what the original game does. */
void rb_ResetSettings(void) {
    s32 i;

    for (i = 0; i < RB_OPTION_COUNT; i++) {
        *g_rb_options[i].value = 0;
    }
}

void rb_StepOption(const RbOption *option, s32 step) {
    s32 value = *option->value + step;

    if (value < 0) {
        value = option->choice_count - 1;
    } else if (value >= option->choice_count) {
        value = 0;
    }
    *option->value = value;
}
