#ifndef PORT_GL_MOVIE_H
#define PORT_GL_MOVIE_H

/* The movie made better in the window (docs/PORT.md, "The movie made
 * better"; the window's `movies = enhanced`): the software GPU's picture
 * of a movie frame drawn into the window on the card, in five passes,
 * instead of being stretched smooth.
 *
 *   colour    the colour of each 2 by 2 pixels, which the chip decodes at
 *             half the width and height and spreads over them flat, taken
 *             back out of the pixels and laid smoothly between the
 *             centres of its cells (the console's own size)
 *   blocks    the edges of the 8 by 8 blocks smoothed where a step across
 *             one is small and the sides of it are flat: what the
 *             quantisation left, as strong as the frame's own scale says
 *   ringing   the ripples beside an edge smoothed, an edge kept: each
 *             pixel the mean of its neighbours weighted by how near their
 *             brightness is to its own, as wide as the frame's scale says
 *   larger    to the window's size along the edges, not across them:
 *             AMD FidelityFX Super Resolution 1's EASU, written again in
 *             GLSL (MIT; the notice is in gl_movie.c)
 *   sharper   FSR 1's RCAS, lightly
 *
 * With a mod's movie (`movies/opening.rbmv`, made larger from the
 * player's disc by tools/port_movies.py) the frame on show is that mod's
 * frame of the same number, and only the last two passes are run.
 *
 * Nothing here is the game's state; none of it runs with `movies =
 * smooth` or `original`, or with the console's picture (F2). */

#include "common.h"

/* Loads the functions and makes the shaders. Called where the backend
 * starts (gl_draw.c). Returns why it could not, or NULL; without it the
 * movie is shown smoothed, as with `movies = smooth`. */
extern const char *port_GlMovieStart(void *(*getproc)(const char *name));

/* The movie's frame into the rectangle `x`, `y`, `w` by `h` of the
 * window (`y` from its top), the window's framebuffer bound and its whole
 * size the viewport: `pixels` the software GPU's picture of it
 * (0xAARRGGBB, a row after a row from the top), `number` and `scale` the
 * frame's number in the stream and its quantisation scale
 * (port_movie.h). FALSE if it cannot be drawn. Called by
 * port_GlShowMovie (gl.h), which takes the window and gives it back. */
extern s32 port_GlMovieDraw(
    const u32 *pixels, s32 pixels_w, s32 pixels_h, s32 window_w, s32 window_h,
    s32 x, s32 y, s32 w, s32 h, u32 number, u32 scale);

/* A mod's frame `number` made ready on the card, where a mod has the
 * movie: read, decoded and sent before the window is taken, so that the
 * card's time of the window's pass is the card's alone. The frame after it
 * is decoded the next time the same frame is asked for. */
extern void port_GlMoviePrepare(u32 number);

/* ---- a mod's movie (gl_movie_pack.c) ---- */

/* The file of a mod's movie: "RBMOVIE1", then six little-endian words --
 * the pictures' width and height, how many frames (numbered from 1), the
 * console's width and height they were made from, and 0 -- then for each
 * frame its place in the file and its size, both words, then the frames,
 * each a baseline JPEG. tools/port_movies.py writes it. */
#define PORT_GL_MOVIE_MAGIC "RBMOVIE1"
#define PORT_GL_MOVIE_MAGIC_BYTES 8
#define PORT_GL_MOVIE_HEAD_WORDS 6

/* The mod's movie of this key (`opening`), read and its frames' places
 * kept, or FALSE where no mod has one or it cannot be read. Once. */
extern s32 port_GlMoviePackOpen(const char *key);
/* Frame `number` of it, four bytes a pixel (red, green, blue and 255)
 * from the top, decoded, and its
 * size; NULL where it has no such frame. The frame after it is decoded
 * the next time the same frame is asked for, so that a frame is ready
 * when the movie reaches it. */
extern const u8 *port_GlMoviePackFrame(u32 number, s32 *w, s32 *h);

#endif /* PORT_GL_MOVIE_H */
