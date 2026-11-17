#ifndef PORT_MOVIE_H
#define PORT_MOVIE_H

/* The movie in the native port (docs/PORT.md, "The movie"): what the
 * port's own files share between them, and the names of Sony's that the
 * port defines for the movie player. The player's own files are compiled
 * into the port and take those names from `movovl/include/sdk.h`
 * instead; the port's files cannot include that header, because it and
 * Sony's libc.h declare printf otherwise than each other. */

#include "common.h"

/* ---- Sony's names that the port defines ---- */

/* libpress, the picture decompressor (src/port/mdec.c). The overlay's
 * own files take these from `movovl/include/sdk.h`, which the port's
 * files cannot include: it declares printf otherwise than Sony's libc.h
 * does. */
extern void DecDCTReset(s32 mode);
extern s32 DecDCTvlc(u32 *bs, u32 *buf);
extern s32 DecDCTBufSize(u32 *bs);
extern void DecDCTin(u32 *buf, s32 mode);
extern void DecDCTout(u32 *buf, s32 size);
extern s32 DecDCTinSync(s32 mode);
extern s32 DecDCToutSync(s32 mode);
extern void DecDCTinCallback(void (*func)(void));
extern void DecDCToutCallback(void (*func)(void));

/* libcd's streaming half (src/port/stream.c): what the port supplies of
 * it, and Sony's own StGetNext under the name the port's wrapper calls
 * it by (tools/port_build.py). */
extern void StCdInterrupt(void);
extern void StUnSetRing(void);
extern u32 StGetNext(u32 **addr, u32 **header);
extern u32 port_StGetNextRing(u32 **addr, u32 **header);

/* ---- the picture decompressor (src/port/mdec.c) ---- */

/* The chip as it is after a reset, with the two quantisation tables and
 * the cosine table it starts with. DecDCTReset(0) does this. */
extern void port_ResetMdec(void);
/* Builds the tables the bitstream decoder looks codes up in. Done when
 * it is first needed; the test calls it by itself. */
extern void port_BuildMdecTables(void);
/* How a frame's bitstream is read, for the test: the next `count` bits
 * of `data` from bit `at`, most significant first within each 16-bit
 * little-endian word. */
extern u32 port_ReadMdecBits(const u8 *data, u32 at, u32 count);
/* One AC code at bit `at` of `data`: its length in bits, and the
 * run-level halfword it means, which is 0xFE00 for the end of a block
 * and 0x7C1F for an escape. A length of 0 means the bits name no code. */
extern u32 port_NextMdecCode(const u8 *data, u32 at, u16 *value);
/* The 8x8 block of coefficients `codes` names, dequantised and inverse
 * transformed: the chip's own arithmetic. `chroma` chooses the
 * quantisation table. Returns how many halfwords it took. */
extern u32 port_MdecBlock(const u16 *codes, u32 count, s32 chroma, s32 *out);
/* One macroblock's pixels from six blocks' coefficients: Cr, Cb and the
 * four of luminance, 16 by 16 pixels, three bytes each. */
extern void port_MdecMacroblockRgb(
    const s32 *cr, const s32 *cb, const s32 **luma, u8 *rgb);

/* A line of what the chip decoded, for the end of a run. */
extern void port_PrintMdecSummary(void);

/* ---- the disc's stream (src/port/stream.c) ---- */

/* The lines a run that played a movie ends with: the sectors the drive
 * gave the stream, the frames the player took and what the chip made of
 * them. Nothing is printed by a run that played none. */
extern void port_PrintMovieSummary(void);

/* Whether Sony's streaming library has hung its callback on the drive:
 * a read that follows is a stream of a movie, not a file's sectors. */
extern s32 port_StreamArmed(void);
/* The drive begins (or stops) reading a stream at `sector`. Its sectors
 * then pass under the head at the disc's own speed, by the port's clock:
 * sound to the decoder, picture to the streaming library's ring. */
extern void port_StartDiscStream(s32 sector, s32 double_speed);
extern void port_StopDiscStream(void);
/* Gives the stream every sector that is due by now. 0 if no stream is
 * being read, and then the drive's sound goes the ordinary way (cd.c). */
extern s32 port_PumpStream(void);

/* ---- the drive (src/port/cd.c) ---- */

/* A raw sector of the disc in the drive, 2352 bytes: 1 if it was read
 * (port_disc.h). */
extern s32 port_DriveReadSector(s32 sector, u8 *raw);

/* ---- the pads (src/port/pad.c) ---- */

/* The BIOS's older pad reader, which Sony's libetc has under PadInit
 * and PadRead (movovl/sdk/libetc/pad.c): the movie player's way of
 * seeing the buttons. */
extern void PAD_init(s32 type, s32 *buf);
extern void PAD_dr(void);

/* ---- the player (src/port/movie.c) ---- */

/* The overlay's own main (movovl/src/main.c), compiled into the port:
 * the port's Exec calls it where the console loads and runs
 * MOVOVL.EXE. */
extern void port_MovieOverlayMain(s32 argc, void *argv);

/* TRUE while the player runs, the title picture after the movie
 * included: the window shows the software GPU's picture then. */
extern s32 port_MovieIsPlaying(void);

#endif /* PORT_MOVIE_H */
