#ifndef PORT_PLATFORM_H
#define PORT_PLATFORM_H

/* What the native port's platform layer has to supply, as far as the
 * game's own C asks for it by a name of the port's (docs/PORT.md lists
 * the rest: the BIOS calls and Sony's libraries, which keep the names
 * Sony's headers declare). Nothing here is written yet: these are the
 * declarations alone. The PlayStation builds never see this header. */

/* The game cannot go on (sys_Trap): on the console it writes where there
 * is no memory, and the machine stops. Does not return. */
extern void port_Trap(void);

#endif /* PORT_PLATFORM_H */
