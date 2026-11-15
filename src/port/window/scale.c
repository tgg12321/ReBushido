/* The native port's window: where the picture goes in it, and video
 * memory's pixels as the window's (window.h; docs/PORT.md, "The window").
 * Whole-number arithmetic, with no SDL in it: tests/port/window.c holds
 * it to cases worked out by hand. */
#include "common.h"
#include "window.h"

/* Video memory, as port_gpu.h has it. */
#define VRAM_WIDTH 1024
#define VRAM_HEIGHT 512
/* A pixel's three channels, five bits each, the red lowest. */
#define CHANNEL_MAX 31
#define CHANNEL_BITS 5
#define BYTE_MAX 255
#define RED_SHIFT 16
#define GREEN_SHIFT 8
#define OPAQUE 0xFF000000U
/* The 24-bit mode keeps three bytes a pixel, the red first. */
#define RGB24_BYTES 3

/* The middle of the window: the largest rectangle of the mode's shape
 * (window.h). */
void port_FitPicture(s32 window_w, s32 window_h, s32 mode, PortRect *out) {
    s32 w = window_w;
    s32 h = window_h;

    if (window_w <= 0 || window_h <= 0) {
        out->x = 0;
        out->y = 0;
        out->w = 0;
        out->h = 0;
        return;
    }
    if (mode == PORT_SCALE_INTEGER && window_w >= PORT_PICTURE_UNIT_W &&
        window_h >= PORT_PICTURE_UNIT_H) {
        s32 across = window_w / PORT_PICTURE_UNIT_W;
        s32 down = window_h / PORT_PICTURE_UNIT_H;
        s32 times = across < down ? across : down;

        w = times * PORT_PICTURE_UNIT_W;
        h = times * PORT_PICTURE_UNIT_H;
    } else if (mode != PORT_SCALE_STRETCH) {
        /* a window too small for the picture once is fitted as well */
        if (window_w * PORT_PICTURE_ASPECT_H >=
            window_h * PORT_PICTURE_ASPECT_W) {
            w = window_h * PORT_PICTURE_ASPECT_W / PORT_PICTURE_ASPECT_H;
        } else {
            h = window_w * PORT_PICTURE_ASPECT_H / PORT_PICTURE_ASPECT_W;
        }
        if (w < 1) {
            w = 1;
        }
        if (h < 1) {
            h = 1;
        }
    }
    out->x = (window_w - w) / 2;
    out->y = (window_h - h) / 2;
    out->w = w;
    out->h = h;
}

/* A channel of five bits as a byte, as the port's PNGs have it. */
static u32 port_StretchChannel(u32 channel) {
    return (channel & CHANNEL_MAX) * BYTE_MAX / CHANNEL_MAX;
}

u32 port_ConvertPixel(u16 pixel) {
    return OPAQUE | (port_StretchChannel(pixel) << RED_SHIFT) |
           (port_StretchChannel((u32)pixel >> CHANNEL_BITS) << GREEN_SHIFT) |
           port_StretchChannel((u32)pixel >> (2 * CHANNEL_BITS));
}

/* A row after a row; a display that runs off an edge of video memory
 * goes on at the other (window.h). */
void port_ConvertDisplay(
    const u16 *vram, s32 x, s32 y, s32 w, s32 h, s32 is_rgb24, u32 *out) {
    s32 row;
    s32 column;

    for (row = 0; row < h; row++) {
        s32 line = (y + row) & (VRAM_HEIGHT - 1);
        const u16 *pixels = &vram[line * VRAM_WIDTH];

        if (is_rgb24) {
            const u8 *bytes = (const u8 *)pixels;

            for (column = 0; column < w; column++) {
                u32 at = (u32)(x * 2 + column * RGB24_BYTES);
                u32 red = bytes[at % (VRAM_WIDTH * 2)];
                u32 green = bytes[(at + 1) % (VRAM_WIDTH * 2)];
                u32 blue = bytes[(at + 2) % (VRAM_WIDTH * 2)];

                *out++ =
                    OPAQUE | (red << RED_SHIFT) | (green << GREEN_SHIFT) | blue;
            }
        } else {
            for (column = 0; column < w; column++) {
                *out++ =
                    port_ConvertPixel(pixels[(x + column) & (VRAM_WIDTH - 1)]);
            }
        }
    }
}
