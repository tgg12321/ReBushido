/* LIBSN SNMAIN: the program's entry point, which clears .bss, sets up the
 * stack and the heap and calls main(), and the walkers of the constructor and
 * destructor tables. Assembly in SN Systems' library: it sets $sp, $fp and
 * $gp, keeps its return address in a variable, has two addi, and the walkers
 * save $s0 at 4($sp), below where the compiler starts. */
#include "common.h"
#include "include_asm.h"
#include <psxsdk/libsn.h>
#include <psxsdk/libgpu.h>

extern void sys_PollHost(void);
extern s32 g_sys_data_start[];
/* The linker's own numbers (bb2.ld): a length is the symbol's value, not
 * something stored at it. */
extern u8 main_TEXT_SIZE[];
extern u8 main_DATA_LEN[];
extern u8 main_BSS_SIZE[];
extern u8 main_BSS_START[];

/* __SN_ENTRY_POINT's return address while it calls main(). The name is a
 * guess at Sony's. */
s32 __ra_temp;

/* Set once __main has run the constructors. */
s32 __initialised = 0;
/* The heap __SN_ENTRY_POINT hands to InitHeap: what is left of RAM after the
 * program and its stack. */
u32 __heapbase = 0;
u32 __heapsize = 0;
/* Where the linker put each section. .text starts at its first function,
 * .data at the (empty) constructor table and runs to the small data, .bss at
 * its first object. */
u32 __text = (u32)sys_PollHost;
u32 __textlen = (u32)main_TEXT_SIZE;
u32 __data = (u32)g_sys_data_start;
u32 __datalen = (u32)main_DATA_LEN;
u32 __bss = (u32)main_BSS_START;
u32 __bsslen = (u32)main_BSS_SIZE;

INCLUDE_ASM("asm/funcs", __SN_ENTRY_POINT);
/* Calls each entry of the constructor table, once. The count is a link-time
 * constant in its code: 0 here, as the game has no constructors. */
INCLUDE_ASM("asm/funcs", __main);
INCLUDE_ASM("asm/funcs", __do_global_dtors);
