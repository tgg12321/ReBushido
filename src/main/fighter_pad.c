/* The pad a fighter is played from (fighter.h). */
#define INCLUDE_ASM_USE_MACRO_INC 1
#include "common.h"
#include "include_asm.h"
#include <psxsdk/inline_o.h>
#include <psxsdk/gtemac.h>
#include "bb2.h"

void pad_ClearState(PadState *pad_state) {
    pad_state->held = 0;
    pad_state->pressed = 0;
    pad_state->released = 0;
    pad_state->unheld = -1;
}

/* ---- the fighter (fighter.h): its pad ---- */

/* A fighter's pad for this frame (fighter.h). The CPU's fighters get the
 * CPU's. A player's is its half of the pads' words with the face buttons
 * turned into the three actions by its button configuration (over the link
 * cable, the other console's player by the configuration that console
 * sent); the analog pads' stick buttons stand for R1 and the first action.
 * A team battle takes the pad away between rounds. The second fighter's
 * directions are mirrored, so that for both "right" is towards the other. */
void fighter_ReadPad(s32 fighter_index, PadState *pad_out) {
    s32 pad_words[4];
    s32 i;
    /* how far along the pads' words this player's half is, then how far
     * along a configuration byte its four bits are */
    s32 player_field_shift;
    s32 mapped_buttons;

    /* the other fighter's: set for the second fighter while practice's
     * round is 0 and the first pad's Select (bit 8) is pressed; nothing
     * reads it */
    g_fighters[fighter_index == 0].select_pressed =
        g_fight_mode == FIGHT_MODE_PRACTICE && g_fight_practice_round == 0 &&
        fighter_index == 0 && ((g_pad_state.pressed >> 8) & 1);
    if (g_fighters[fighter_index].is_cpu) {
#if RB_PORT
        /* the port's CPU, where the run asked for one of its levels */
        if (port_CpuPlays(fighter_index)) {
            port_CpuBuildPad(fighter_index, pad_out);
        } else {
            cpu_BuildPad(fighter_index, pad_out);
        }
        port_CpuLogPad(fighter_index, pad_out);
#else
        cpu_BuildPad(fighter_index, pad_out);
#endif
        return;
    }
    *pad_out = g_pad_state;
    player_field_shift = fighter_index * PAD_BITS;
    pad_words[0] = (g_pad_state.held >> player_field_shift) & 0xFFFF;
    pad_words[1] = (g_pad_state.pressed >> player_field_shift) & 0xFFFF;
    pad_words[2] = (g_pad_state.released >> player_field_shift) & 0xFFFF;
    pad_words[3] = (g_pad_state.unheld >> player_field_shift) & 0xFFFF;
    if (g_fight_mode == FIGHT_MODE_NETWORK) {
        player_field_shift = 0;
    } else {
        player_field_shift = fighter_index * PAD_CONFIG_BITS;
    }
    for (i = 0; i < 4; i++) {
        if (g_fight_mode == FIGHT_MODE_NETWORK &&
            fighter_index != g_link_side) {
            mapped_buttons = pad_words[i] & ~FIGHTER_PAD_ACTIONS;
            {
                u8 frontal_set = g_pad_remote_map_circle;
                if (((frontal_set & PAD_CONFIG_CIRCLE) &&
                     (pad_words[i] & PADRright)) ||
                    ((frontal_set & PAD_CONFIG_TRIANGLE) &&
                     (pad_words[i] & PADRup)) ||
                    ((frontal_set & PAD_CONFIG_CROSS) &&
                     (pad_words[i] & PADRdown)) ||
                    ((frontal_set & PAD_CONFIG_SQUARE) &&
                     (pad_words[i] & PADRleft))) {
                    mapped_buttons |= FIGHTER_PAD_FRONTAL_ATTACK;
                }
            }
            {
                u8 reverse_set = g_pad_remote_map_cross;
                if (((reverse_set & PAD_CONFIG_CIRCLE) &&
                     (pad_words[i] & PADRright)) ||
                    ((reverse_set & PAD_CONFIG_TRIANGLE) &&
                     (pad_words[i] & PADRup)) ||
                    ((reverse_set & PAD_CONFIG_CROSS) &&
                     (pad_words[i] & PADRdown)) ||
                    ((reverse_set & PAD_CONFIG_SQUARE) &&
                     (pad_words[i] & PADRleft))) {
                    mapped_buttons |= FIGHTER_PAD_REVERSE_ATTACK;
                }
            }
            {
                u8 stance_set = g_pad_remote_map_square;
                if (((stance_set & PAD_CONFIG_CIRCLE) &&
                     (pad_words[i] & PADRright)) ||
                    ((stance_set & PAD_CONFIG_TRIANGLE) &&
                     (pad_words[i] & PADRup)) ||
                    ((stance_set & PAD_CONFIG_CROSS) &&
                     (pad_words[i] & PADRdown)) ||
                    ((stance_set & PAD_CONFIG_SQUARE) &&
                     (pad_words[i] & PADRleft))) {
                    mapped_buttons |= FIGHTER_PAD_STANCE;
                }
            }
        } else {
            mapped_buttons = pad_words[i] & ~FIGHTER_PAD_ACTIONS;
            {
                u8 frontal_set =
                    g_saved_record.button_config[0] >> player_field_shift;
                if (((frontal_set & PAD_CONFIG_CIRCLE) &&
                     (pad_words[i] & PADRright)) ||
                    ((frontal_set & PAD_CONFIG_TRIANGLE) &&
                     (pad_words[i] & PADRup)) ||
                    ((frontal_set & PAD_CONFIG_CROSS) &&
                     (pad_words[i] & PADRdown)) ||
                    ((frontal_set & PAD_CONFIG_SQUARE) &&
                     (pad_words[i] & PADRleft))) {
                    mapped_buttons |= FIGHTER_PAD_FRONTAL_ATTACK;
                }
            }
            {
                u8 reverse_set =
                    g_saved_record.button_config[1] >> player_field_shift;
                if (((reverse_set & PAD_CONFIG_CIRCLE) &&
                     (pad_words[i] & PADRright)) ||
                    ((reverse_set & PAD_CONFIG_TRIANGLE) &&
                     (pad_words[i] & PADRup)) ||
                    ((reverse_set & PAD_CONFIG_CROSS) &&
                     (pad_words[i] & PADRdown)) ||
                    ((reverse_set & PAD_CONFIG_SQUARE) &&
                     (pad_words[i] & PADRleft))) {
                    mapped_buttons |= FIGHTER_PAD_REVERSE_ATTACK;
                }
            }
            {
                u8 stance_set =
                    g_saved_record.button_config[2] >> player_field_shift;
                if (((stance_set & PAD_CONFIG_CIRCLE) &&
                     (pad_words[i] & PADRright)) ||
                    ((stance_set & PAD_CONFIG_TRIANGLE) &&
                     (pad_words[i] & PADRup)) ||
                    ((stance_set & PAD_CONFIG_CROSS) &&
                     (pad_words[i] & PADRdown)) ||
                    ((stance_set & PAD_CONFIG_SQUARE) &&
                     (pad_words[i] & PADRleft))) {
                    mapped_buttons |= FIGHTER_PAD_STANCE;
                }
            }
        }
        if (mapped_buttons & PADi) {
            mapped_buttons = (mapped_buttons & ~PADi) | PADR1;
        }
        if (mapped_buttons & PADj) {
            mapped_buttons =
                (mapped_buttons & ~PADj) | FIGHTER_PAD_FRONTAL_ATTACK;
        }
        pad_words[i] = mapped_buttons;
    }
    pad_out->held = pad_words[0];
    pad_out->pressed = pad_words[1];
    pad_out->released = pad_words[2];
    pad_out->unheld = pad_words[3];
    if (g_fight_mode == FIGHT_MODE_TEAM &&
        (g_team_time_up_timer != 0 || g_team_break_timer != 0 ||
         g_team_out_timer != 0 || g_team_warning_timer != 0 ||
         g_team_ippon_timer != 0 || g_team_result_timer != 0)) {
        pad_ClearState(pad_out);
    }
    if (fighter_index == 1 && g_fight_mode != FIGHT_MODE_NETWORK) {
        pad_out->held =
            (pad_out->held & FIGHTER_PAD_BELOW_DIRECTIONS) |
            ((pad_out->held & PADLleft) >> 2) |
            ((pad_out->held & PADLright) << 2) |
            ((pad_out->held & PADLdown) >> 2) | ((pad_out->held & PADLup) << 2);
        pad_out->pressed =
            (pad_out->pressed & FIGHTER_PAD_BELOW_DIRECTIONS) |
            ((pad_out->pressed & PADLleft) >> 2) |
            ((pad_out->pressed & PADLright) << 2) |
            ((pad_out->pressed & PADLdown) >> 2) |
            ((pad_out->pressed & PADLup) << 2);
        pad_out->released =
            (pad_out->released & FIGHTER_PAD_BELOW_DIRECTIONS) |
            ((pad_out->released & PADLleft) >> 2) |
            ((pad_out->released & PADLright) << 2) |
            ((pad_out->released & PADLdown) >> 2) |
            ((pad_out->released & PADLup) << 2);
        pad_out->unheld =
            (pad_out->unheld & FIGHTER_PAD_BELOW_DIRECTIONS) |
            ((pad_out->unheld & PADLleft) >> 2) |
            ((pad_out->unheld & PADLright) << 2) |
            ((pad_out->unheld & PADLdown) >> 2) |
            ((pad_out->unheld & PADLup) << 2);
    }
#if RB_PORT
    port_CpuLogPad(fighter_index, pad_out);
#endif
}
