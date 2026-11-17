/* The pads: both controller ports read once a frame into g_pad_state, and
 * the button configuration (which face buttons make which attack). */
#include "common.h"
#include "bb2.h"

s32 g_pad_packets[2][9];
PadState g_pad_state;

/* A pad's packet as the driver leaves it: a status byte (0 when the pad
 * answered), the pad's type in the high nibble of the next, then two bytes
 * of buttons, a 0 bit for each one held. */
enum {
    PAD_PACKET_STATUS = 0,
    PAD_PACKET_TYPE = 1,
    PAD_PACKET_BUTTONS = 2,
};

#define PAD_PACKET_TYPE_SHIFT 4

/* Each action on its own button for both players: circle, cross, square. */
void pad_ResetButtonConfig(void) {
    g_saved_record.button_config[0] = PAD_CONFIG_BOTH(PAD_CONFIG_CIRCLE);
    g_saved_record.button_config[1] = PAD_CONFIG_BOTH(PAD_CONFIG_CROSS);
    g_saved_record.button_config[2] = PAD_CONFIG_BOTH(PAD_CONFIG_SQUARE);
}

s32 pad_PackButtonConfig(void) {
    return (g_saved_record.button_config[0] & PAD_CONFIG_SET) |
           ((g_saved_record.button_config[1] & PAD_CONFIG_SET)
            << PAD_CONFIG_BITS) |
           ((g_saved_record.button_config[2] & PAD_CONFIG_SET)
            << (2 * PAD_CONFIG_BITS));
}

void pad_SetRemoteButtonConfig(s32 packed) {
    g_pad_remote_map_circle = packed & PAD_CONFIG_SET;
    g_pad_remote_map_cross = (packed >> PAD_CONFIG_BITS) & PAD_CONFIG_SET;
    g_pad_remote_map_square =
        (packed >> (2 * PAD_CONFIG_BITS)) & PAD_CONFIG_SET;
}

/* Nothing held, and both pads taken for digital ones. */
void pad_ResetState(void) {
    g_pad_state.type[0] = PAD_TYPE_DIGITAL;
    g_pad_state.type[1] = PAD_TYPE_DIGITAL;
    g_pad_state.held = 0;
    g_pad_state.pressed = 0;
    g_pad_state.released = 0;
    g_pad_state.unheld = -1;
}

void pad_Startup(void) {
    pad_ResetState();
    g_pad_state.valid[0] = 1;
    g_pad_state.valid[1] = 1;
}

/* Reads both pads into g_pad_state. On the way the buttons pass through the
 * camera's pad control and, in a linked game, are exchanged with the other
 * console.
 *
 * During a fight a pad that has been pulled out counts as pressing Start,
 * which pauses the game: pad 1 in every mode, pad 2 in the two-player modes.
 * `no_unplug_pause` leaves that out (main passes it on frames that are not
 * shown). */
void pad_Update(s32 no_unplug_pause) {
    PadState pad;
    s32 pad_packets[4];
    s32 i;
    u32 held_bits;
    s32 previous_held;

    held_bits = 0;
#if RB_PORT
    /* the port writes the frame's input into the packets as they are
     * read, from its one input point (src/port/pad.c) */
    port_TakeInput();
#endif
    pad_packets[0] = g_pad_packets[0][0];
    pad_packets[1] = g_pad_packets[0][1];
    pad_packets[2] = g_pad_packets[1][0];
    pad_packets[3] = g_pad_packets[1][1];
    for (i = 0; i < 2; i++) {
        u8 *packet = (u8 *)pad_packets + i * PAD_PACKET_SIZE;
        s32 buttons;

        if (packet[PAD_PACKET_STATUS] == 0) {
            pad.type[i] = packet[PAD_PACKET_TYPE] >> PAD_PACKET_TYPE_SHIFT;
            pad.valid[i] = 1;

            switch (pad.type[i]) {
            case PAD_TYPE_ANALOG_STICK:
            case PAD_TYPE_ANALOG:
                pad.type[i] = PAD_TYPE_DIGITAL;
            case PAD_TYPE_NEGCON:
            case PAD_TYPE_KONAMI_GUN:
            case PAD_TYPE_DIGITAL:
                buttons = ~((packet[PAD_PACKET_BUTTONS] << 8) |
                            packet[PAD_PACKET_BUTTONS + 1]);
                break;
            case PAD_TYPE_MOUSE:
            case PAD_TYPE_NAMCO_GUN:
            case PAD_TYPE_MULTITAP:
            default:
                buttons = 0;
                break;
            }
        } else {
            pad.type[i] = PAD_TYPE_DIGITAL;
            pad.valid[i] = 0;
            buttons = 0;
        }

        /* pad 1 ends up in the low half, pad 2 in the high */
        held_bits = (held_bits >> PAD_BITS) | (buttons << PAD_BITS);
    }

    pad.held = held_bits;
    camera_ReadPad(&pad.held);

    if (g_sys_screen == SCREEN_FIGHT_UPDATE && !no_unplug_pause) {
        switch (g_fight_mode) {
        case FIGHT_MODE_VS:
        case FIGHT_MODE_TEAM:
            if (!g_pad_state.valid[1]) {
                pad.held |= PAD_ANY(PADstart);
            }
        case FIGHT_MODE_STORY:
        case FIGHT_MODE_VSCPU:
        case FIGHT_MODE_PRACTICE:
        case FIGHT_MODE_CHANBARA:
        case FIGHT_MODE_NETWORK:
            if (!g_pad_state.valid[0]) {
                pad.held |= PAD_ANY(PADstart);
            }
            break;
        }
    }

    link_ExchangePad(&pad);

    for (i = 0; i < 2; i++) {
        g_pad_state.type[i] = pad.type[i];
        g_pad_state.valid[i] = pad.valid[i];
    }

    previous_held = g_pad_state.held;
    g_pad_state.held = pad.held;
    g_pad_state.pressed = pad.held & ~previous_held;
    g_pad_state.unheld = ~pad.held;
    g_pad_state.released = ~pad.held & previous_held;
}
