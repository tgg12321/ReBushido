#ifndef PORT_DEFLATE_H
#define PORT_DEFLATE_H

/* A zlib stream written (RFC 1950 and 1951): the native port's own
 * compressor, for a replay's start (port_replay.h), since the headless
 * program has no C library to link zlib into. png.c reads the stream
 * back (port_Inflate, port_png.h). Matches are found by a hash of three
 * bytes and a chain of the window's earlier places, and each block's
 * codes are built for it (dynamic Huffman), or the block is stored where
 * that is shorter.
 *
 * It is done a slice at a time, so that a window that compresses while
 * the game runs never stops a frame for long: begin, then as many calls
 * as it takes, each taking about `budget` bytes of the input. */

#include "common.h"

typedef struct {
    const u8 *in;
    u32 in_size;
    u32 in_at;
    u8 *out;
    u32 out_room;
    u32 out_at;
    u32 bits;
    s32 bit_count;
    u32 adler_a; /* the stream's sum (Adler-32), of the input so far */
    u32 adler_b;
    s32 failed; /* the output had no room left */
} PortDeflate;

/* The most a stream of `size` bytes can take: every block stored. */
extern u32 port_DeflateBound(u32 size);
/* A stream of `in` (`in_size` bytes) into `out`, which has room for
 * `out_room` bytes (port_DeflateBound's is always enough). */
extern void port_DeflateBegin(
    PortDeflate *z, const void *in, u32 in_size, void *out, u32 out_room);
/* About `budget` more bytes of the input compressed: TRUE once the
 * stream is whole, its length in z->out_at (z->failed where it had no
 * room). */
extern s32 port_DeflateSome(PortDeflate *z, u32 budget);

#endif /* PORT_DEFLATE_H */
