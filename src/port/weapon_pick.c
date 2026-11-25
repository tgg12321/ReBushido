/* The mods' new weapons on the weapon select (port_weapons.h; docs/MODS.md,
 * "A mod's weapon on the select screens"). A state file: what each side's
 * cursor shows and what each fighter picked are saved, put back and hashed
 * with the game's own variables, so a rollback puts the pick back, the
 * two machines of a session pick the same (their presses are the same,
 * and so are their weapons: the session's set-up holds them to it), and a
 * replay, which plays the presses again, picks it again. Nothing here
 * draws: the window lays the name over the weapon row from what this file
 * keeps (src/port/window/match_page.c). */
#include "common.h"
#include "bb2.h"

PortWeaponPick g_port_weapon_pick;

void port_WeaponPickReset(void) {
    PortWeaponPick *pick = &g_port_weapon_pick;
    s32 side;

    pick->is_started = TRUE;
    for (side = 0; side < PORT_WEAPON_SIDES; side++) {
        pick->variant[side] = 0;
        pick->cursor_weapon[side] = -1;
        pick->picked[side] = PORT_WEAPON_PICK_NONE;
    }
}

/* A frame of the weapon row (port_weapons.h): the variant goes back to
 * the game's own where the cursor moved to another weapon; L1 and R1 go
 * round the weapon's variants, the game's own among them. While a side
 * chooses, its fighter has picked nothing of a mod's. */
s32 port_WeaponPickStep(s32 side, s32 weapon_id, s32 back, s32 on) {
    PortWeaponPick *pick = &g_port_weapon_pick;
    s32 count;

    if (side < 0 || side >= PORT_WEAPON_SIDES) {
        return FALSE;
    }
    pick->row_frame = (u32)g_disp_frame_count;
    pick->picked[side] = PORT_WEAPON_PICK_NONE;
    if (pick->cursor_weapon[side] != weapon_id) {
        pick->cursor_weapon[side] = (s8)weapon_id;
        pick->variant[side] = 0;
    }
    count = port_WeaponVariantCount(weapon_id);
    if (count == 0 || (!back && !on)) {
        return FALSE;
    }
    pick->variant[side] =
        (s8)((pick->variant[side] + (on ? 1 : count)) % (count + 1));
    snd_Play(MENU_SOUND_CURSOR, SND_VOLUME_MAX, SND_VOLUME_MAX);
    return TRUE;
}

/* Cross on the weapon row (port_weapons.h): the variant under the
 * cursor, if the cursor is still on the weapon taken, is the fighter's. */
void port_WeaponPickTake(s32 side, s32 weapon_id) {
    PortWeaponPick *pick = &g_port_weapon_pick;
    const PortWeapon *weapon;

    if (side < 0 || side >= PORT_WEAPON_SIDES) {
        return;
    }
    pick->row_frame = (u32)g_disp_frame_count;
    weapon = pick->cursor_weapon[side] == weapon_id
                 ? port_WeaponVariant(weapon_id, pick->variant[side])
                 : NULL;
    pick->picked[side] = weapon != NULL ? (s8)(port_WeaponPlaceOf(weapon) + 1)
                                        : PORT_WEAPON_PICK_NONE;
    if (weapon != NULL) {
        port_Log("weapons: the fighter in place %d picks %s (\"%s\") on the "
                 "weapon select\n",
                 side + 1, weapon->key, weapon->name);
    }
}

/* ---- what the window draws ---- */

const char *port_WeaponPickShown(s32 side, s32 *count) {
    const PortWeaponPick *pick = &g_port_weapon_pick;
    const PortWeapon *weapon;

    *count = 0;
    if (side < 0 || side >= PORT_WEAPON_SIDES ||
        (u32)g_disp_frame_count - pick->row_frame > 2 ||
        pick->cursor_weapon[side] < 0) {
        return NULL;
    }
    *count = port_WeaponVariantCount(pick->cursor_weapon[side]);
    if (pick->picked[side] != PORT_WEAPON_PICK_NONE) {
        weapon = port_WeaponAt(pick->picked[side] - 1);
    } else {
        weapon =
            port_WeaponVariant(pick->cursor_weapon[side], pick->variant[side]);
    }
    return weapon != NULL ? weapon->name : NULL;
}
