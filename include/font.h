#ifndef FONT_H
#define FONT_H

/* The built-in text font (font.c): 8x8 characters for the debug menu and
 * for messages a developer was meant to see. Up to 32 characters a frame. */

#include "common.h"
#include <psxsdk/libgpu.h>

extern void font_LoadImage(void);
/* Starts a frame's text: no characters yet, the cursor at the top left. */
extern void font_BeginFrame(void);
/* Call after the frame's text: sets the font's texture page for it. */
extern void font_AddTpage(void);
/* Prints at the cursor and moves it on. The format is printf's, with at
 * most one argument to a conversion; "~cRGB", three digits, sets the
 * colour. */
extern void font_Printf(u8 *format, s32 first_arg, ...);

#endif /* FONT_H */
