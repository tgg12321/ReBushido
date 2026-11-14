#ifndef MODERN_SHIM_H
#define MODERN_SHIM_H

/* What tools/modern/check.py puts ahead of every file it gives to the host's
 * gcc. The game's build never includes this: it is only there so a modern
 * compiler can read the source for its warnings. Nothing compiled with it
 * is linked or run. */

/* A function that stays assembly has no body to read: it becomes a
 * declaration of nothing, and its listing is not looked at. */
#define INCLUDE_ASM_H
#define INCLUDE_ASM(FOLDER, NAME) struct modern_included_asm
#define INCLUDE_RODATA(FOLDER, NAME) struct modern_included_asm

/* A GTE operation (Sony's inline_c.h and inline_o.h, which check.py rewrites
 * to this): MIPS coprocessor instructions the host compiler cannot take.
 * It becomes a call that may read and write anything its arguments point
 * at, which is what the instructions do behind the compiler's back. */
extern void modern_gte(const char *op, ...);

#endif /* MODERN_SHIM_H */
