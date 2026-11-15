#ifndef PORT_GL_H
#define PORT_GL_H

/* The remastered renderer's backend: OpenGL 3.3 core (docs/PORT.md,
 * "Remastered"). It draws the primitives port_remaster.h decodes into a
 * colour buffer that is all of video memory at `scale` times its
 * resolution, reading texels from a mirror of the software GPU's video
 * memory, which stays the truth.
 *
 * Nothing here opens a window or a display: it is given a function that
 * looks GL's functions up (SDL_GL_GetProcAddress, eglGetProcAddress),
 * draws into a framebuffer object of its own, and hands back the
 * pixels. The window and the headless prover both use it. */

#include "common.h"
#include "port_remaster.h"

/* How the picture is drawn. */
typedef struct {
    s32 scale; /* across: 1 to PORT_REMASTER_SCALE_MAX */
    /* Down: 1 to PORT_REMASTER_SCALE_MAX, and 0 for the same as
     * across. A line of the console's picture is two lines of the
     * screen (240 of them fill a screen of four by three), so twice
     * `scale` is what gives the picture square pixels; the same as
     * `scale` is what the comparison with the software GPU needs. */
    s32 tall;
    s32 is_bilinear; /* texels smoothed, not blocks */
    /* The three things that make the hardware's picture the console's,
     * for the proof at scale 1 (docs/PORT.md, "Remastered"): the
     * corners where the PlayStation held them, the texture flat across
     * a polygon, and five bits a channel out with the chip's dither. */
    s32 is_exact;
} PortGlOptions;

/* Loads the functions, makes the shaders, the mirror of video memory
 * and the colour buffer, and attaches itself as the remastered
 * renderer's backend. Returns NULL, or why it could not. `getproc` is
 * the host's way of finding a GL function. */
extern const char *port_GlStart(
    void *(*getproc)(const char *name), const PortGlOptions *options);

/* The options again: the scale, the filter and the exact picture can
 * each change between frames. */
extern void port_GlSetOptions(const PortGlOptions *options);
extern void port_GlGetOptions(PortGlOptions *options);

/* Whether the backend is there and its context is current. */
extern s32 port_GlIsStarted(void);

/* The display's rectangle of the colour buffer, as 0xFFRRGGBB a pixel,
 * a row after a row from the top, into `out`: at most `room` pixels.
 * `out_w` and `out_h` are what it wrote. 0 if there is nothing to
 * read. */
extern s32 port_GlReadPicture(u32 *out, u32 room, s32 *out_w, s32 *out_h);

/* The picture is in the colour buffer; nothing is shown. A window calls
 * this and then draws the buffer into itself (and swaps); the prover
 * reads it back. */
extern void port_GlFinishFrame(void);

/* What the last frame cost, in microseconds of the PC's clock, as the
 * host measures it: the caller gives the time. */
extern void port_GlNoteFrameTime(u32 microseconds);
extern u32 port_GlGetFrameTime(void);
/* The vertices and the draws of the last frame. */
extern u32 port_GlGetVertexCount(void);
extern u32 port_GlGetDrawCount(void);
/* Pixels of the mirror of video memory sent to the card in the last
 * frame. */
extern u32 port_GlGetUploadCount(void);
/* Triangles of the last frame the chip itself would not have drawn:
 * more than 1023 pixels across, 511 down, or of no area. */
extern u32 port_GlGetDroppedCount(void);

#endif /* PORT_GL_H */
