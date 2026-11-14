.include "macro.inc"

.set noat
.set noreorder

.section .text, "ax"

/* GCC's hook for static constructors, called first thing in main. Empty. */
glabel __main
    jr         $ra
     nop
endlabel __main

/* Entry point: set the small-data pointer and go to main. main's return goes
 * straight back to whoever ran the overlay. */
glabel _start
    lui        $gp, %hi(_gp)
    addiu      $gp, $gp, %lo(_gp)
    j          main
     nop
endlabel _start
