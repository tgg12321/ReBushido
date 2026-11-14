/* LIBAPI PATCH: EnablePAD, DisablePAD and _patch_pad. _patch_pad finds the
 * BIOS's two routines and keeps their addresses, and EnablePAD / DisablePAD
 * jump straight to them. Assembly in Sony's library: the return address is
 * kept in a variable, not on the stack, the BIOS is called through $t2, the
 * two jump through $t1, and there are two addi. */
#define INCLUDE_ASM_USE_MACRO_INC 1
#include "include_asm.h"
#include "common.h"

/* _patch_pad's return address while it calls the BIOS, and the two routines
 * it found there. The names are not Sony's. */
s32 _patch_pad_ra;
s32 _patch_pad_enable;
s32 _patch_pad_disable;

INCLUDE_ASM("asm/funcs", EnablePAD);
INCLUDE_ASM("asm/funcs", DisablePAD);
INCLUDE_ASM("asm/funcs", _patch_pad);
