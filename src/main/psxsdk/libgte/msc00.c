/* LIBGTE MSC00: InitGeom, which turns the GTE on and sets it up. The listing
 * begins with Sony's stamp, two words nothing reads. Assembly in Sony's
 * library: the return address is kept in a variable, not on the stack, and
 * there are two nops of padding to a multiple of 16 bytes. */
#include "include_asm.h"
#include "common.h"

/* The return address, kept while the BIOS is called; sixteen bytes of the
 * module's data. The name is not Sony's. */
s32 _InitGeom_ra[4] = {0, 0, 0, 0};

INCLUDE_ASM("asm/funcs", InitGeom);
