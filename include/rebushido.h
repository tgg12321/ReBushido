#ifndef REBUSHIDO_H
#define REBUSHIDO_H

/* ReBushido: the modded line of the game (docs/REBUSHIDO.md). This is its
 * master switch and the switch of each of its changes. With the master
 * switch off, which is how `make check` builds, every one of them is 0 and
 * the source builds the original executable byte for byte. */

/* The master switch. `make rebushido` sets it to 1; nothing else does. */
#ifndef REBUSHIDO
#define REBUSHIDO 0
#endif

/* ReBushido's version. The build adds the commit (src/rebushido/version.c). */
#define RB_VERSION "0.1-dev"

/* ---- the changes ---- */

/* One line a change: `#define RB_NAME (REBUSHIDO && 1)`, with a comment
 * saying what it is. The source asks `#if RB_NAME`, never `#ifdef`, and
 * tools/lint.py takes a name that is not defined here for a mistake. While
 * a change is being worked on its 1 can be made a 0 to build without it.
 *
 * The version text and the settings are in files that only the ReBushido
 * build has (src/rebushido/), which need no switch. */

/* Quick boot, a developer's tool and no part of the game: the program
 * skips the boot image, the opening and the menus and starts one fight,
 * which the build chose (src/rebushido/quickboot.c). It is off in the
 * ReBushido build too: only `make quickboot` turns it on, by giving
 * RB_QUICK_BOOT_BUILD (docs/REBUSHIDO.md, "Booting straight into a
 * fight"). */
#ifndef RB_QUICK_BOOT_BUILD
#define RB_QUICK_BOOT_BUILD 0
#endif
#define RB_QUICK_BOOT (REBUSHIDO && RB_QUICK_BOOT_BUILD)

/* ---- what only the ReBushido build has ---- */

#if REBUSHIDO
/* "ReBushido <version> (<commit>)". */
extern const char g_rb_version[];
#endif

#endif /* REBUSHIDO_H */
