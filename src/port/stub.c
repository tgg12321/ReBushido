/* The native port's count of what it does not do yet (port_platform.h):
 * every function of Sony's or the BIOS's that is still a stub says here
 * that it was called. What a run prints of this at its end is the list of
 * what the stages after this one have to write (docs/PORT.md). */
#include "common.h"
#include "bb2.h"

/* The stubs that have been called, in the order of their first calls. */
static PortStub *g_port_stub_first;
static PortStub *g_port_stub_last;

void port_CountStub(PortStub *stub) {
    if (stub->calls == 0) {
        if (g_port_stub_last != NULL) {
            g_port_stub_last->next = stub;
        } else {
            g_port_stub_first = stub;
        }
        g_port_stub_last = stub;
    }
    stub->calls++;
}

void port_PrintStubs(void) {
    const PortStub *stub;

    for (stub = g_port_stub_first; stub != NULL; stub = stub->next) {
        port_Print("stub %s %u\n", stub->name, stub->calls);
    }
}
