/* LIBGTE MSC00: InitGeom, which turns the GTE on and sets it up. The listing
 * begins with Sony's stamp, two words nothing reads. Assembly in Sony's
 * library: the return address is kept in a variable, not on the stack, and
 * there are two nops of padding to a multiple of 16 bytes. */
#include "include_asm.h"
#include "common.h"

/* The return address, kept while the BIOS is called; sixteen bytes of the
 * module's data. The name is not Sony's. */
s32 _InitGeom_ra[4] = {0, 0, 0, 0};

#include "rebushido.h"

/* The native port has no assembly: the C here does what the listing does
 * (docs/PORT.md). */
#if RB_PORT
#include "psxsdk/libgte.h"
#include "port_gte.h"

/* In C: the values the library starts the GTE with. The average depths
 * are a third and a quarter of the depths' sum, a sixteenth of each; the
 * projection plane is 1000 away; the depth cue, and no offset. What the
 * assembly does besides is the console's business and has no place
 * here: it patches the kernel's exception handler and switches the
 * coprocessor on. */
void InitGeom(void) {
    port_GteWriteControl(PORT_GTE_ZSF3, 0x155);
    port_GteWriteControl(PORT_GTE_ZSF4, 0x100);
    port_GteWriteControl(PORT_GTE_PLANE, 1000);
    port_GteWriteControl(PORT_GTE_DQA, (u32)-0x1062);
    port_GteWriteControl(PORT_GTE_DQB, 0x1400000);
    port_GteWriteControl(PORT_GTE_OFX, 0);
    port_GteWriteControl(PORT_GTE_OFY, 0);
}
#else
INCLUDE_ASM("asm/funcs", InitGeom);
#endif
