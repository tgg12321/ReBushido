#ifndef PORT_PROFILE_H
#define PORT_PROFILE_H

/* The native port's own measure of where a blank's time goes
 * (src/port/profile.c; docs/PORT.md, "Where the time goes"): the time is
 * the PC's, counted into one part at a time, and nothing of the game
 * reads it. Off, which is every run but one given --profile, a switch is
 * one test of a flag. */

#include "common.h"

/* The parts a blank's time is counted into. Each is its own time alone:
 * the software GPU's drawing is not the game's too. */
typedef enum {
    PORT_PROFILE_GAME,     /* the game's code, and whatever is not below */
    PORT_PROFILE_RASTER,   /* the software GPU drawing into video memory */
    PORT_PROFILE_REMASTER, /* the side channel: primitives decoded */
    PORT_PROFILE_CARD,     /* the card's batches and video memory sent */
    PORT_PROFILE_SOUND,    /* the sound chip and the disc's sound mixed */
    PORT_PROFILE_FINISH,   /* the card's frame finished: shadows, resolve */
    PORT_PROFILE_PICTURE,  /* the window's picture of the frame made */
    PORT_PROFILE_TWEEN,    /* pictures between two frames drawn */
    PORT_PROFILE_SWAP,     /* a picture handed to the display */
    PORT_PROFILE_WAIT,     /* waiting for the blank's or a picture's time */
    PORT_PROFILE_WINDOW,   /* the rest of the window's turn: input, log */
    PORT_PROFILE_PARTS
} PortProfilePart;

/* Whether the time is being counted (the window's --profile). */
extern s32 g_port_profile_on;

/* From now the time is `part`'s: the part it was is returned, to be
 * switched back to. */
extern s32 port_ProfileSwitch(s32 part);
/* Each part's time since the last take, in the host's ticks
 * (port_host.h), into `ticks` (PORT_PROFILE_PARTS of them); and from
 * zero again. */
extern void port_ProfileTake(u32 *ticks);
/* A part's short name, for a report. */
extern const char *port_ProfileName(s32 part);

#endif /* PORT_PROFILE_H */
