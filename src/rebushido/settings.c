/* ReBushido's settings: the player's choices that a change asks about while
 * the game runs. Nothing sets them yet but the defaults here; the options
 * page at boot will. */
#include "common.h"
#include "bb2.h"

/* As the original: no fix on, the retail picture. */
RbSettings g_rb_settings = {FALSE, RB_GRAPHICS_ORIGINAL};

/* Puts every setting back to what the original game does. */
void rb_ResetSettings(void) {
    g_rb_settings.bugfixes = FALSE;
    g_rb_settings.graphics = RB_GRAPHICS_ORIGINAL;
}
