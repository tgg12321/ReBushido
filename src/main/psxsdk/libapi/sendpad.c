/* LIBAPI SENDPAD: _SendPAD, _send_pad and _send_pad_patch. _send_pad_patch is
 * not a function: it is the four instructions _send_pad writes over two
 * places in the BIOS's controller code, with three words of padding after
 * them. Assembly in Sony's library: the return address is kept in a variable,
 * not on the stack, the BIOS is called through $t2, and there is an addi. */
#define INCLUDE_ASM_USE_MACRO_INC 1
#include "include_asm.h"
#include "common.h"

/* _send_pad's return address while it calls the BIOS, and the routine it
 * found there, which _SendPAD jumps to. The names are not Sony's. */
s32 _send_pad_ra;
s32 _send_pad_entry;

INCLUDE_ASM("asm/funcs", _SendPAD);
INCLUDE_ASM("asm/funcs", _send_pad);
INCLUDE_ASM("asm/funcs", _send_pad_patch);
