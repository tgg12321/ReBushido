#ifndef RB_SETTINGS_H
#define RB_SETTINGS_H

/* ReBushido's settings: what the player chose on the options page at boot.
 * A change that the player can turn off asks here while the game runs, as
 * well as being behind its switch in rebushido.h. As the game starts,
 * everything is as the original has it. */

#include "common.h"

/* The two ways the game can be drawn. */
enum RbGraphics {
    RB_GRAPHICS_ORIGINAL, /* as the retail game */
    RB_GRAPHICS_ENHANCED, /* what a PlayStation can add: a wide picture,
                           * the 640 by 480 mode, more frames a second */
};

typedef struct {
    u8 bugfixes; /* TRUE: the fixes of the original's bugs are on */
    u8 graphics; /* an RbGraphics */
} RbSettings;

extern RbSettings g_rb_settings;

extern void rb_ResetSettings(void);

#endif /* RB_SETTINGS_H */
