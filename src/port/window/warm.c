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
 * ended here, after the first picture, takes the driver's once on the
 * loading screen, and every thread that ends later costs nothing. */
#include <SDL.h>

#include "common.h"
#include "sdl_window.h"

static int SDLCALL port_WarmThread(void *unused) {
    (void)unused;
    return 0;
}

void port_WarmDriver(void) {
    static s32 is_warm;
    Uint64 from;
    SDL_Thread *thread;

    if (is_warm) {
        return;
    }
    is_warm = TRUE;
    from = SDL_GetPerformanceCounter();
    thread = SDL_CreateThread(port_WarmThread, "rebushido-warm", NULL);
    if (thread == NULL) {
        return;
    }
    SDL_WaitThread(thread, NULL);
    port_Note("graphics: a thread made and ended, for the driver's first "
              "time (%.1f ms)",
              (double)(SDL_GetPerformanceCounter() - from) * 1000.0 /
                  (double)SDL_GetPerformanceFrequency());
}
