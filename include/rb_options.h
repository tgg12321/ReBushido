#ifndef RB_OPTIONS_H
#define RB_OPTIONS_H

/* The options page: what a ReBushido disc shows first, before the boot
 * image and the opening (options.c). The player sets ReBushido's settings
 * there; they are read from the settings' file before the page comes up
 * and written to it when the player leaves, if one was changed
 * (rb_save.h). The quick-boot build does not have the page. */

#include "common.h"

/* Where the page is, for a debugger or a play test to read. */
enum RbPageState {
    RB_PAGE_NOT_YET = 0, /* the program has not got there */
    RB_PAGE_LOADING = 1, /* reading the settings' file */
    RB_PAGE_UP = 2,      /* on show, taking the pad */
    RB_PAGE_SAVING = 3,  /* the player has left it; writing the file */
    RB_PAGE_DONE = 4,    /* gone: the game goes on */
};

extern u8 g_rb_page_state; /* an RbPageState */
extern u8 g_rb_page_row;   /* the setting the cursor is on */

/* Shows the page until the player leaves it. main calls it once, before
 * it shows the boot image. */
extern void rb_RunOptionsPage(void);
/* The row `step` rows on from `row`, going round at the ends. */
extern s32 rb_StepRow(s32 row, s32 step);

#endif /* RB_OPTIONS_H */
