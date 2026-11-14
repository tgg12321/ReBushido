#ifndef BIOS_H
#define BIOS_H

/* A jump into the BIOS: a stub that jumps to one of the kernel's three
 * vectors (0xA0, 0xB0, 0xC0) with the function's number in $t1, and one nop
 * of padding. The compiler cannot produce it, so it is assembly, as it was
 * in Sony's libraries. */

#define BIOS_FUNCTION(name, vector, id)                                        \
    __asm__(".section .text\n"                                                 \
            "    .set noat\n"                                                  \
            "    .set noreorder\n"                                             \
            "glabel " #name "\n"                                               \
            "    addiu $t2, $zero, " #vector "\n"                              \
            "    jr    $t2\n"                                                  \
            "    addiu $t1, $zero, " #id "\n"                                  \
            "    nop\n"                                                        \
            "endlabel " #name "\n"                                             \
            "    .set reorder\n"                                               \
            "    .set at\n")

#define BIOS_A_FUNCTION(name, id) BIOS_FUNCTION(name, 0xA0, id)
#define BIOS_B_FUNCTION(name, id) BIOS_FUNCTION(name, 0xB0, id)
#define BIOS_C_FUNCTION(name, id) BIOS_FUNCTION(name, 0xC0, id)

#endif /* BIOS_H */
