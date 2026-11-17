#ifndef PORT_GL_PAUSE_H
#define PORT_GL_PAUSE_H

/* The fight's last frame under the pause menu in the remastered picture
 * (port_remaster.h, "the pause"; docs/PORT.md, "The pause").
 *
 * The pause's first loop keeps the fight's last frame from the half of
 * the colour buffer it was drawn into, at the buffer's own resolution and
 * with a wider picture's bands; every loop puts it back where the game
 * draws that frame's table again, under the panel. A window resized
 * while the game is paused has a colour buffer of another size, and the
 * picture is stretched to it, smoothed. */

#include "common.h"

/* Loads the functions it uses and makes its framebuffer, and tells the
 * decoder it is the pause's holder. Called where the window starts the
 * backend, on the same context. Returns why it could not, or NULL; then
 * a pause draws the fight's table again as the console does. */
extern const char *port_GlPauseStart(void *(*getproc)(const char *name));

/* The holder told no more: the context is going. */
extern void port_GlPauseStop(void);

#endif /* PORT_GL_PAUSE_H */
