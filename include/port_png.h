#ifndef PORT_PNG_H
#define PORT_PNG_H

/* PNG pictures read and written by the native port's mods (docs/MODS.md):
 * a replacement texture a player made in any painting program is read,
 * and a texture of the game's is written out for one to be made from.
 * Nothing of a C library is used, so that every build of the port has it
 * and the host tests run it (tests/port/mods.c). The PlayStation builds
 * never see this header. */

#include "common.h"

/* The largest picture read, each way: far more than any texture of the
 * game at sixteen times, and small enough that its pixels are counted in
 * 32 bits. */
#define PORT_PNG_SIDE_MAX 8192

/* What a PNG file is, from its header. */
typedef struct {
    s32 w;
    s32 h;
    /* the memory port_PngDecode needs for its work, in bytes: the
     * compressed data gathered in one piece, and the rows unpacked */
    u32 work_bytes;
} PortPngInfo;

/* Reads the header of the PNG of `size` bytes at `file`: 1 and `info`,
 * or 0 and a reason in `problem` (`room` bytes, the 0 included) where it
 * is not a picture this reads. It reads every kind of PNG but the
 * interlaced: grey, grey and alpha, colours, colours and alpha, a
 * palette, at any depth, with a colour or a palette entry that is
 * transparent (tRNS). */
extern s32 port_PngMeasure(
    const u8 *file, u32 size, PortPngInfo *info, char *problem, u32 room);
/* Decodes it into `rgba`, four bytes a pixel (red, green, blue, alpha) a
 * row after a row from the top, using `work` (info.work_bytes of it):
 * 1, or 0 and a reason where the data are damaged. */
extern s32 port_PngDecode(
    const u8 *file, u32 size, u8 *work, u8 *rgba, char *problem, u32 room);

/* How many bytes the PNG port_PngEncode writes of a picture this size. */
extern u32 port_PngEncodedSize(s32 w, s32 h);
/* `rgba` (as above) as a PNG of 8 bits a channel and an alpha, its rows
 * stored and not packed, into `out`: returns how many bytes, which is
 * port_PngEncodedSize. */
extern u32 port_PngEncode(const u8 *rgba, s32 w, s32 h, u8 *out);

#endif /* PORT_PNG_H */
