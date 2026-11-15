#ifndef PORT_GPU_H
#define PORT_GPU_H

/* The PlayStation's GPU in C, for the native port (docs/PORT.md, "The
 * picture"): its video memory, and what the words written to its two
 * ports do to it. Sony's libgpu in the port (src/port/gpu.c) writes the
 * words Sony's own writes; src/port/raster.c is the chip. Nothing here
 * opens a window: the picture is this memory, which a run can write to
 * files (src/port/picture.c). The PlayStation builds never see this
 * header. */

#include "common.h"

/* ---- video memory ---- */

/* 1024 by 512 pixels of 16 bits: five of red in the low bits, five of
 * green, five of blue, and the mask bit on top. */
#define PORT_VRAM_WIDTH 1024
#define PORT_VRAM_HEIGHT 512
#define PORT_VRAM_PIXELS (PORT_VRAM_WIDTH * PORT_VRAM_HEIGHT)
#define PORT_VRAM_BYTES (PORT_VRAM_PIXELS * 2)
#define PORT_PIXEL_MASK_BIT 0x8000
extern u16 g_port_vram[PORT_VRAM_PIXELS];

/* ---- the two ports ---- */

/* A word to GP0: a drawing command, one of its arguments, or two pixels
 * of a copy into video memory. */
extern void port_WriteGp0(u32 word);
/* A word to GP1: a display command. */
extern void port_WriteGp1(u32 word);
/* A word from GPUREAD: two pixels of a copy out of video memory, or
 * what GP1's command 0x10 asked for. */
extern u32 port_ReadGpu(void);
/* GPUSTAT: the drawing mode in its low eleven bits, as E1 has them, the
 * mask's two settings, the display's mode; the GPU is always ready. */
extern u32 port_GetGpuStatus(void);

/* Two things the port's GPU does as the console is best known to do
 * them, and PCSX-Redux's software GPU does otherwise:
 *
 *   - with dithering on, a textured polygon of one colour whose texture
 *     is multiplied by the colour is dithered. The emulator dithers only
 *     the polygons with a colour for each corner;
 *   - a colour or a texture coordinate changes across a polygon by steps
 *     of twelve bits of fraction, so over hundreds of pixels it falls
 *     behind by a part of a step. The emulator's falls behind by
 *     nothing that shows.
 *
 * 1 here makes the port's GPU do both as the emulator, so that the two
 * pictures can be compared pixel for pixel (a plan's `gpu emulator`). It
 * is for that and nothing else, and it is off unless asked for. */
extern void port_SetGpuLikeEmulator(s32 is_on);

/* ---- what the drawing commands have set ---- */

/* The drawing state a primitive is painted with. The remastered
 * renderer reads it to paint a primitive as the software GPU has just
 * painted it (port_remaster.h); nothing else needs it. */
typedef struct {
    u32 mode;         /* E1's bits, with a textured polygon's own page */
    s32 window_and_u; /* E2: a texture point is anded with the one and */
    s32 window_or_u;  /* then ored with the other */
    s32 window_and_v;
    s32 window_or_v;
    s32 clip_left; /* E3 and E4: the drawing area, its last pixels in */
    s32 clip_top;
    s32 clip_right;
    s32 clip_bottom;
    s32 offset_x; /* E5 */
    s32 offset_y;
    s32 mask_set;   /* E6: ored into every pixel drawn */
    s32 mask_check; /* ... and a pixel that has this is left alone */
} PortGpuState;

extern void port_GetGpuState(PortGpuState *state);

/* ---- what is on show ---- */

typedef struct {
    s32 x; /* the rectangle of video memory that is displayed */
    s32 y;
    s32 w;
    s32 h;
    s32 is_on;    /* 0 while the display is blanked (GP1's command 3) */
    s32 is_rgb24; /* the 24-bit mode: three bytes a pixel */
} PortDisplay;

extern void port_GetDisplay(PortDisplay *display);

/* ---- ordering tables (src/port/gpu.c) ---- */

/* A primitive's link is 24 bits of an address. The port's memory is in
 * several places whose addresses differ above those bits, so each place
 * primitives may be is named once, and DrawOTag finds a link's address
 * among them. Two places whose low 24 bits overlap cannot both be
 * named. */
extern void port_AddLinkRegion(u32 address, u32 size);

/* ---- pictures (src/port/picture.c) ---- */

/* The displayed rectangle as the emulator's script writes a screenshot:
 * the bytes of video memory a row after a row, which are 16-bit pixels,
 * or red, green and blue where the display is in the 24-bit mode the
 * movie uses. Returns 1 if all was written; the size is the display's,
 * and port_GetDisplay says which depth. */
extern s32 port_WriteDisplayPixels(s32 file);
/* The same as a PNG, each line twice where the picture is 640 or 512
 * across and half as high. */
extern s32 port_WriteDisplayPng(s32 file);
/* All of video memory, 1,048,576 bytes. */
extern s32 port_WriteVram(s32 file);

#endif /* PORT_GPU_H */
