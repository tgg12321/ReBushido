/* Quick boot: the fight a `make quickboot` build starts in place of the
 * boot image, the opening and the menus. A developer's tool. The native
 * port starts one the same way where its `--fight` asks (port_platform.h).
 * Without RB_QUICK_BOOT or RB_PORT this file is empty. */
#include "common.h"
#include "bb2.h"

#if RB_QUICK_BOOT || RB_PORT

/* Asks for a fight as the menus would have and starts its mode, which
 * chooses the screen: the way the original's own debug menu starts a
 * fight (fight_start.c). */
void rb_StartFight(s32 mode, const s32 *roster, const s32 *weapon, s32 stage,
                   s32 is_cpu2, s32 bugfixes) {
    /* What the main menu does when it first opens, which a fight counts
     * on: no stage is in memory, no sound bank but the common one. */
    snd_CloseAllVabs();
    g_fight_loaded_stage = -1;

    /* The rest of g_fight_config is as fight_InitConfig left it. */
    g_fight_config.mode = mode;
    g_fight_config.roster[0] = roster[0];
    g_fight_config.roster[1] = roster[1];
    g_fight_config.weapon[0] = weapon[0];
    g_fight_config.weapon[1] = weapon[1];
    g_fight_config.stage = stage;
    g_fight_config.is_cpu[1] = is_cpu2;
    /* there is no options page to turn the fixes on at */
    g_rb_settings.bugfixes = bugfixes;
    fight_StartMode();
}

#endif /* RB_QUICK_BOOT || RB_PORT */

#if RB_PORT
/* The port's --fight (port_platform.h): its fight started, where one was
 * asked for. */
void rb_PortQuickFight(void) {
    const PortFight *fight = port_QuickFight();

    if (fight != NULL) {
        rb_StartFight(fight->mode, fight->roster, fight->weapon, fight->stage,
                      fight->is_cpu[1], g_rb_settings.bugfixes);
    }
}
#endif

#if RB_QUICK_BOOT

/* What to start, which the Makefile gives from its QB_ variables
 * (tools/quickboot.py checks them). These are what it gives when the
 * variables are left alone. */
#ifndef RB_QB_MODE
#define RB_QB_MODE FIGHT_MODE_VS
#endif
#ifndef RB_QB_P1
#define RB_QB_P1 ROSTER_KANNUKI
#endif
#ifndef RB_QB_P2
#define RB_QB_P2 ROSTER_GENGORO
#endif
#ifndef RB_QB_WEAPON1
#define RB_QB_WEAPON1 WEAPON_ID_KATANA
#endif
#ifndef RB_QB_WEAPON2
#define RB_QB_WEAPON2 WEAPON_ID_KATANA
#endif
#ifndef RB_QB_STAGE
#define RB_QB_STAGE STAGE_BAMBOO_THICKET
#endif
#ifndef RB_QB_CPU2
#define RB_QB_CPU2 1
#endif
#ifndef RB_QB_BUGFIXES
#define RB_QB_BUGFIXES 0
#endif

/* The fight the build chose (rb_quickboot.h). */
const s8 g_rb_quick_boot[8] = {
    RB_QB_MODE,    RB_QB_P1,    RB_QB_P2,   RB_QB_WEAPON1,
    RB_QB_WEAPON2, RB_QB_STAGE, RB_QB_CPU2, RB_QB_BUGFIXES,
};

/* Starts the fight the build chose. */
void rb_QuickBoot(void) {
    static const s32 roster[2] = {RB_QB_P1, RB_QB_P2};
    static const s32 weapon[2] = {RB_QB_WEAPON1, RB_QB_WEAPON2};

    rb_StartFight(
        RB_QB_MODE, roster, weapon, RB_QB_STAGE, RB_QB_CPU2, RB_QB_BUGFIXES);
}

#endif /* RB_QUICK_BOOT */
