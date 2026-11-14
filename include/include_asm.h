#ifndef INCLUDE_ASM_H
#define INCLUDE_ASM_H

/* A function that stays assembly: its listing is put in the file's .text
 * where the function is. A file that sets INCLUDE_ASM_USE_MACRO_INC before
 * this header gets macro.inc's label macros, the others labels.inc's. */

#ifndef INCLUDE_ASM
#define INCLUDE_ASM(FOLDER, NAME)                                              \
    __asm__(".section .text\n"                                                 \
            "    .set noat\n"                                                  \
            "    .set noreorder\n"                                             \
            "    .include \"" FOLDER "/" #NAME ".s\"\n"                        \
            "    .set reorder\n"                                               \
            "    .set at\n")
#endif
#ifndef INCLUDE_RODATA
#define INCLUDE_RODATA(FOLDER, NAME)                                           \
    __asm__(".section .rodata\n"                                               \
            "    .include \"" FOLDER "/" #NAME ".s\"\n"                        \
            ".section .text")
#endif

#if INCLUDE_ASM_USE_MACRO_INC
__asm__(".include \"include/macro.inc\"\n");
#else
__asm__(".include \"include/labels.inc\"\n");
#endif

#endif /* INCLUDE_ASM_H */
