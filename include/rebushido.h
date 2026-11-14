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

/* ReBushido's version: the number here is the one place it is written.
 * The build adds the commit (src/rebushido/version.c). Every build has
 * -dev after the number but a release's, which `make rebushido-patch
 * RB_RELEASE=1` builds by giving RB_VERSION_SUFFIX as nothing
 * (docs/REBUSHIDO.md, "Making a release"). */
#define RB_VERSION_NUMBER "0.1"
#ifndef RB_VERSION_SUFFIX
#define RB_VERSION_SUFFIX "-dev"
#endif
#define RB_VERSION RB_VERSION_NUMBER RB_VERSION_SUFFIX

/* ---- the changes ---- */

/* One line a change: `#define RB_NAME (REBUSHIDO && 1)`, with a comment
 * saying what it is. The source asks `#if RB_NAME`, never `#ifdef`, and
 * tools/lint.py takes a name that is not defined here for a mistake. While
 * a change is being worked on its 1 can be made a 0 to build without it.
 *
 * The version text and the settings are in files that only the ReBushido
 * build has (src/rebushido/), which need no switch. */

/* The options page: before the boot image the program shows a page where
 * the player sets ReBushido's settings, read from and written to a file
 * of ReBushido's own on the memory card (src/rebushido/options.c, save.c;
 * docs/REBUSHIDO.md, "The options page"). The quick-boot build has no
 * page. */
#define RB_OPTIONS_PAGE (REBUSHIDO && 1)

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

/* ---- the fixes of the original's bugs ---- */

/* Each is on only while the player's "Bugfixes" setting is
 * (g_rb_settings.bugfixes); docs/REBUSHIDO.md, "The fixes", has the table. */

/* A fighter's first frame gives all three of its weapon's points a last
 * place: the blade's far end and a pole arm's shaft were swept from where
 * the last fight left them, or from what lay on the scratchpad
 * (fighter_Update). */
#define RB_FIX_FIRST_FRAME_SWEEP (REBUSHIDO && 1)

/* A blade's hit gives fighter_TakeHit a place for the word it clears
 * first: it was written at address 0 (fight_TestBladesOnBodies). */
#define RB_FIX_HIT_NULL_WRITE (REBUSHIDO && 1)

/* The vanisher comes back at its own stage's start position off the
 * shrine's stage: the table was read with half its stride
 * (fighter_Vanish). */
#define RB_FIX_VANISH_PLACE (REBUSHIDO && 1)

/* A splash asked for while twelve are alive is not made: it was written
 * over the next table (eff_SpawnSplash). */
#define RB_FIX_SPLASH_POOL (REBUSHIDO && 1)

/* An object launched while twelve are loose takes the last place: it was
 * written after the pool (object_Launch). */
#define RB_FIX_OBJECT_POOL (REBUSHIDO && 1)

/* The muzzle flashes keep an ordering-table index for every quad of all
 * six: there was room for two flashes' and a fourth flash sent the
 * third's quads anywhere (eff_DrawMuzzleFlashes). */
#define RB_FIX_MUZZLE_FLASH_INDICES (REBUSHIDO && 1)

/* The points and the team scores count their second drawing mode in the
 * size they return: the caller built its next primitive over it
 * (hud_DrawPointsAndTime, hud_DrawTeamScores). */
#define RB_FIX_HUD_DRAW_MODE (REBUSHIDO && 1)

/* The ending's picture and the count of opponents beaten move the
 * primitive cursor on by words, not by four times as much
 * (cutscene_EndingUpdate, fight_KillCountUpdate). */
#define RB_FIX_PRIM_CURSOR (REBUSHIDO && 1)

/* The boot image's fade walks the picture's pixels: it began 20 bytes in,
 * so the first ten pixels never faded, and ran 20 bytes past the end
 * (disp_ShowBootImage). */
#define RB_FIX_BOOT_FADE (REBUSHIDO && 1)

/* The first frame tells pad_Update that no frame was skipped: it was told
 * whatever the register held (main). */
#define RB_FIX_FIRST_FRAME_PAD (REBUSHIDO && 1)

/* Coming back to chanbara's page from a fight starts the music once
 * (menu_EnterMode). */
#define RB_FIX_CHANBARA_MUSIC (REBUSHIDO && 1)

/* A link that gave up trades nothing: the other side's unlocks, its
 * buttons and the seed were set from what was received last
 * (link_Connect). */
#define RB_FIX_LINK_GIVE_UP (REBUSHIDO && 1)

/* The castle rooftop's backdrop meshes move by as far as the stage did:
 * the distance was not passed (resource_OnStageMoved). */
#define RB_FIX_ROOFTOP_REBASE (REBUSHIDO && 1)

/* A splash's drops begin to fade at full brightness: the first frame of
 * the fade had a blue that did not fit its byte (eff_DrawSplashes). */
#define RB_FIX_SPLASH_FADE (REBUSHIDO && 1)

/* The dirt puff is drawn with its frame's 32 rows of texture, not 33
 * (eff_SetUpParticles). */
#define RB_FIX_DIRT_PUFF_ROW (REBUSHIDO && 1)

/* ---- what only the ReBushido build has ---- */

#if REBUSHIDO
/* "ReBushido <version> (<commit>)". */
extern const char g_rb_version[];
#endif

#endif /* REBUSHIDO_H */
