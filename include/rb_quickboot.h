#ifndef RB_QUICKBOOT_H
#define RB_QUICKBOOT_H

/* Quick boot: a build that starts one fight as soon as the program has
 * set itself up, with no boot image, opening or menu. A developer's tool,
 * for trying a change and for the play tests; the ReBushido a player gets
 * does not have it (rebushido.h, RB_QUICK_BOOT). */

#include "common.h"

#if RB_QUICK_BOOT || RB_PORT
/* Sets up a fight's choice in g_fight_config, as the menus would, and
 * starts its mode, which chooses the screen: the quick-boot build's, and
 * the native port's `--fight` (port_platform.h). */
extern void rb_StartFight(s32 mode, const s32 *roster, const s32 *weapon,
                          s32 stage, s32 is_cpu2, s32 bugfixes);
#endif

#if RB_PORT
/* The fight the port's --fight asks for, started; nothing where none was
 * asked for. main calls it after choosing the movie's screen. */
extern void rb_PortQuickFight(void);
#endif

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
