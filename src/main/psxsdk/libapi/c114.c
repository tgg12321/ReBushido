/* LIBAPI C114: _96_remove, the BIOS A(0x72) trampoline. Assembly in Sony's
 * library like the other trampolines (bios.h); written out here because of
 * the stamp in front of it and its three nops of padding. */
#include "include_asm.h"

__asm__(
    ".section .text\n"
    "    .set noat\n"
    "    .set noreorder\n"
    /* Sony's stamp (the letters Ps and a build number) at the head of the
       module; nothing reads it. */
    "    .word 0x15007350\n"
    "    .word 0x0040809C\n"
    "glabel _96_remove\n"
    "    addiu $t2, $zero, 0xA0\n"
    "    jr $t2\n"
    "    addiu $t1, $zero, 0x72\n"
    "    nop\n"
    "    nop\n"
    "    nop\n"
    "endlabel _96_remove\n"
    "    .set reorder\n"
    "    .set at\n");
