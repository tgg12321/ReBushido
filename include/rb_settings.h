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
                           * the 640 by 480 mode, more frames a second;
                           * the options page calls it "Remastered" */
};

/* Every member is one byte whose 0 is the original's behaviour. */
typedef struct {
    u8 bugfixes; /* TRUE: the fixes of the original's bugs are on */
    u8 graphics; /* an RbGraphics */
} RbSettings;

extern RbSettings g_rb_settings;

/* Where each setting is kept in the settings' file on the memory card
 * (rb_save.h): its place among the file's values. A place is a setting's
 * for ever: a new setting takes the next one, and none is used again or
 * moved, so that a file written by any version is read rightly by any
 * other. */
enum RbSaveSlot {
    RB_SAVE_SLOT_BUGFIXES = 0,
    RB_SAVE_SLOT_GRAPHICS = 1,
    RB_SAVE_SLOT_COUNT,
};

/* A setting as the options page shows it and the file keeps it. */
#define RB_OPTION_CHOICES_MAX 4

typedef struct {
    const char *name;
    /* what each value is called, from 0 up */
    const char *choices[RB_OPTION_CHOICES_MAX];
    u8 choice_count;
    u8 save_slot; /* an RbSaveSlot */
    u8 *value;    /* the member of g_rb_settings */
} RbOption;

/* The settings, in the order the page shows them. A new one is a member
 * of RbSettings, an RbSaveSlot and a line of this table. */
#define RB_OPTION_COUNT 2
extern const RbOption g_rb_options[RB_OPTION_COUNT];

extern void rb_ResetSettings(void);
/* The setting's next choice (`step` 1) or the one before (-1), going
 * round at the ends. */
extern void rb_StepOption(const RbOption *option, s32 step);

#endif /* RB_SETTINGS_H */
