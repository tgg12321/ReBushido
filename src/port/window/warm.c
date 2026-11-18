/* What the card's driver does once, the first time, done while the
 * program starts and not in a fight (docs/PORT.md, "Stalls").
 *
 * The first thread of the program to end after the window's context is
 * in use holds NVIDIA's OpenGL driver up 60 to 130 ms, once: the thread
 * that draws waits in the driver at its next call (a texture written, a
 * swap) for as long. No thread of the window's ends in play, but
 * Windows' own do: a thread of CRYPT32 that some library starts as the
 * program opens ends when it has been idle for half a minute, which was
 * the stall of about 100 ms some 30 seconds into every run, in the
 * middle of the first fight (tools/port_stalls.py found it: the
 * window's thread in nvoglv64.dll waiting, nothing of the program's own
 * busy, and that thread gone at the same blank). A thread made and
 * ended here, at the eighth picture, takes the driver's once on the
 * loading screen (73 to 85 ms there), and every thread that ends later
 * costs nothing. At the first picture it was not enough: that thread
 * ended in 0.4 ms and the one at 30 seconds still stalled, so what the
 * driver does is set up in its first pictures. */
#include <SDL.h>

#include "common.h"
#include "sdl_window.h"

/* the picture the thread is made at */
#define WARM_AT_SWAP 8

static int SDLCALL port_WarmThread(void *unused) {
    (void)unused;
    return 0;
}

void port_WarmDriver(void) {
    static s32 swaps;
    SDL_Thread *thread;

    if (swaps > WARM_AT_SWAP || ++swaps != WARM_AT_SWAP) {
        return;
    }
    thread = SDL_CreateThread(port_WarmThread, "rebushido-warm", NULL);
    if (thread == NULL) {
        return;
    }
    /* not waited for: the driver may hold the ending thread until this
     * one next calls it */
    SDL_DetachThread(thread);
    port_Note("graphics: a thread made and ended at picture %d, for the "
              "driver's first time",
              swaps);
}
