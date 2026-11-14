#ifndef RB_QUICKBOOT_H
#define RB_QUICKBOOT_H

/* Quick boot: a build that starts one fight as soon as the program has
 * set itself up, with no boot image, opening or menu. A developer's tool,
 * for trying a change and for the play tests; the ReBushido a player gets
 * does not have it (rebushido.h, RB_QUICK_BOOT). */

#include "common.h"

#if RB_QUICK_BOOT
/* Sets up the fight the build chose and makes its screen the first. main
 * calls it in place of showing the boot image and choosing the movie's
 * screen. */
extern void rb_QuickBoot(void);
/* The fight the build chose, for a debugger or a play test to read: the
 * mode, the two roster entries, the two weapons, the stage, and whether
 * the CPU plays the second fighter. */
extern const s8 g_rb_quick_boot[7];
#endif

#endif /* RB_QUICKBOOT_H */
