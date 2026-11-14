/* Stand-ins for two routines of the game's that are assembly (math_asm.c),
 * which tested code calls. Included by a test after runtime/runtime.h. */

/* sys_Trap stops the game where it cannot go on, by writing where there is
 * no memory. Here it is counted, and the tested function carries on: a test
 * says how many it expects. */
static s32 g_test_traps;

void sys_Trap() { g_test_traps++; }

/* mem_MoveWords moves `size` bytes, rounded up to whole words, between two
 * places that may overlap. */
void mem_MoveWords(s32 src, s32 dest, s32 size) {
    test_Copy((void *)dest, (void *)src, (u32)(size + 3) & ~3U);
}
