/* LIBGTE REG09: ReadGeomScreen, the distance from the eye to the screen.
 * Assembly in Sony's library: a nop of padding to a multiple of 16 bytes,
 * which a compiled module never has. */
#include "include_asm.h"

#include "rebushido.h"

/* The native port has no assembly: the C here does what the listing does
 * (docs/PORT.md). */
#if RB_PORT
#include "psxsdk/libgte.h"
#include "port_gte.h"

/* In C: the distance to the projection plane, as it was set. */
s32 ReadGeomScreen(void) { return (s32)port_GteReadControl(PORT_GTE_PLANE); }
#else
INCLUDE_ASM("asm/funcs", ReadGeomScreen);
#endif
