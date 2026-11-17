/* The game as a step the host calls (port_platform.h; docs/NETPLAY.md,
 * "The step"). The game's main never returns: it runs on its own stack
 * as a fiber, and gives the host the turn at each frame's boundary,
 * which is where it reads the pads. The host's loop (main.c) then owns
 * the time: it runs a step, and between two steps it may save the
 * state, put an earlier one back, or set the next frame's input. */
#include "common.h"
#include "port_host.h"
#include "bb2.h"

u32 g_port_steps;

void port_StartGame(void (*function)(void)) {
    port_HostStartGame(function, PORT_STACK_ADDR + PORT_STACK_SIZE);
}

void port_StepGame(void) {
    port_HostEnterGame();
    g_port_steps++;
}

void port_EndStep(void) { port_HostLeaveGame(); }
