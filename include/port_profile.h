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
    PORT_PROFILE_AHEAD,    /* frames run ahead, and the state kept and put
                            * back (step.c) */
    PORT_PROFILE_CLOTH,    /* the HD cloth: the body posed on the processor
                            * and the cloth kept out of it (gl_fighter.c) */
    PORT_PROFILE_SHADOW,   /* the shadows cast from the fighters' faces,
                            * gathered and laid down (gl_shadow.c) */
    PORT_PROFILE_PARTS
} PortProfilePart;

/* Whether the time is being counted (the window's --profile). */
extern s32 g_port_profile_on;

/* From now the time is `part`'s: the part it was is returned, to be
 * switched back to. */
extern s32 port_ProfileSwitch(s32 part);
/* The clock the parts are counted by, in the host's ticks: the wall's
 * (port_HostTicks), or the window's thread's own processor time where a
 * run asks for it (RB_PROFILE_CPU; tools/port_budget.py), which another
 * program on the machine does not lengthen. */
extern u32 (*g_port_profile_clock)(void);
/* Each part's time since the last take, in the host's ticks
 * (port_host.h), into `ticks` (PORT_PROFILE_PARTS of them); and from
 * zero again. */
extern void port_ProfileTake(u32 *ticks);
/* Where the window runs the step on a thread of its own (docs/PORT.md,
 * "The step on a thread of its own"), that thread's parts are counted
 * apart, by its own clock: this says which thread asks (0 the window's,
 * 1 the step's); NULL, every switch is the window's. */
#define PORT_PROFILE_THREADS 2
extern s32 (*g_port_profile_thread)(void);
/* The step's thread's parts since the last take, as port_ProfileTake;
 * the window's thread takes them while the step's is still. */
extern void port_ProfileTakeStep(u32 *ticks);
/* The part the time is counted to now (the sampler's: sampler.c). */
extern s32 port_ProfilePart(void);
/* A part's short name, for a report. */
extern const char *port_ProfileName(s32 part);

#endif /* PORT_PROFILE_H */
