/* The movie in the native port (docs/PORT.md, "The movie"): where the
 * game runs the player.
 *
 * On the console the game shuts its own systems down, reads
 * STR/MOVOVL.EXE into the memory its primitive buffers are in, and runs
 * it with the BIOS's Exec, handing it six words: the movie's place on
 * the disc, the frame to stop at, two pieces of memory to work in, the
 * title picture's place and the frame from which it is shown
 * (sys_ExecMovieOverlay, src/main/cdrom.c; docs/formats/MOVOVL.md).
 * When the player's main returns, Exec returns, and the game sets itself
 * up again.
 *
 * Here the player's own C is compiled into the port (movovl/src/), over
 * the port's libcd, libgpu, libetc and the port's picture decompressor
 * (mdec.c, stream.c), and Exec calls its main where the console would
 * jump into the program it has just read. The game's own code is
 * untouched: it still reads the player off the disc into that memory,
 * which is what the console has there while the movie runs, and still
 * passes the same six words.
 *
 * The player's own main is compiled under another name, because the
 * game's main is in the same program (tools/port_build.py). */
#include "common.h"
#include "port_movie.h"
#include "bb2.h"

/* TRUE while the player runs. The window shows the software GPU's own
 * picture then: the remastered picture has no 24-bit mode, and the title
 * picture is shown from video memory outside the game's display
 * (docs/PORT.md, "Remastered"). */
static s32 g_port_movie_is_playing;

s32 port_MovieIsPlaying(void) { return g_port_movie_is_playing; }

/* The game runs one program, the movie player. Its main takes the six
 * words as a structure of its own (movovl/src/main.c). */
s32 Exec(struct EXEC *exec, s32 argc, char **argv) {
    (void)exec;
    g_port_movie_is_playing = TRUE;
    port_MovieForgetShown();
    port_MovieOverlayMain(argc, argv);
    g_port_movie_is_playing = FALSE;
    return 1;
}
