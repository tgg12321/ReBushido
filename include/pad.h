#ifndef PAD_H
#define PAD_H

/* The two pads (pad.c). pad_Update reads both once a frame into g_pad_state;
 * everything else looks there. */

#include "common.h"

/* What is plugged into a port, as the controller reports it. The game keeps
 * PAD_TYPE_DIGITAL for the two analog types, which it reads as digital pads;
 * a mouse, a Namco gun or a multitap gives no buttons. */
enum PadType {
    PAD_TYPE_MOUSE = 1,
    PAD_TYPE_NEGCON = 2,
    PAD_TYPE_KONAMI_GUN = 3,
    PAD_TYPE_DIGITAL = 4,
    PAD_TYPE_ANALOG_STICK = 5,
    PAD_TYPE_NAMCO_GUN = 6,
    PAD_TYPE_ANALOG = 7,
    PAD_TYPE_MULTITAP = 8,
};

/* Both pads' buttons. Each word holds pad 1 in its low half and pad 2 in its
 * high half, a bit per button as in Sony's PADLup, PADRdown, PADstart... */
typedef struct PadState {
    s16 type[2];  /* enum PadType */
    s16 valid[2]; /* 0: nothing answered in that port */
    u32 held;     /* down now */
    u32 pressed;  /* down now and not last frame */
    u32 released; /* down last frame and not now */
    u32 unheld;   /* ~held */
} PadState;

/* A pad's half of a word, in bits. */
#define PAD_BITS 16
/* A button on either pad, as a mask for PadState's words. */
#define PAD_ANY(b) (((u32)(b) << PAD_BITS) | (b))
/* A button on pad `pad` (0 or 1). */
#define PAD_OF(b, pad) ((b) << ((pad) * PAD_BITS))

extern PadState g_pad_state;
/* The buffers the pad driver fills behind the game's back, and how much of
 * each it is given to fill. */
#define PAD_PACKET_SIZE 8
extern s32 g_pad_packets[2][9];

/* The button configuration: for each of the three attack actions (which the
 * game reads as circle, cross and square), the face buttons that trigger it.
 * Bit 0 is circle, 1 triangle, 2 cross, 3 square; player 1's set is in the
 * low nibble of each byte of SaveRecord.button_config and player 2's in the
 * high. Over the link cable the other console's set is kept here instead. */
#define PAD_CONFIG_CIRCLE 1
#define PAD_CONFIG_TRIANGLE 2
#define PAD_CONFIG_CROSS 4
#define PAD_CONFIG_SQUARE 8
#define PAD_CONFIG_BITS 4 /* a player's set, in bits */
/* The same set of buttons for both players, as a byte of the button
 * configuration. */
#define PAD_CONFIG_BOTH(set) ((set) | ((set) << PAD_CONFIG_BITS))
#define PAD_CONFIG_SET 0xF /* and as a mask */
extern u8 g_pad_remote_map_circle;
extern u8 g_pad_remote_map_cross;
extern u8 g_pad_remote_map_square;

/* Clears the buttons and marks both pads as plugged in (boot). */
extern void pad_Startup(void);
/* Clears the buttons. */
extern void pad_ResetState(void);
/* Reads both pads. Call once a frame. Takes one argument, nonzero to leave
 * out the "unplugged pad pauses the fight" check.
 * Declared without a prototype: the pause menu calls it with no argument. */
extern void pad_Update();

extern void pad_ResetButtonConfig(void);
/* Player 1's button configuration in twelve bits, for sending over the link
 * cable, and the other console's taken from the same. */
extern s32 pad_PackButtonConfig(void);
extern void pad_SetRemoteButtonConfig(s32 packed);
/* A pad with nothing pressed (fighter_pad.c). */
extern void pad_ClearState(PadState *pad_state);

#endif /* PAD_H */
